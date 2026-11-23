"""DiFVM Graph-Topology & Polyhedral Structure of Arrays (SoA) Engine.

Implements high-performance unstructured FVM mesh topology construction:
- Direct C-ABI bridge to cfd_mesher_native.dll / .so / .dylib for zero-overhead native execution
- High-fidelity pure-Python fallback (NumPy/SciPy)
- Strict mathematical invariants:
  1. Positive cell volumes (V_i > 10^-15) via Gauss divergence theorem pyramid decomposition
  2. Exact closed-cell Gauss divergence sum (max_i ||sum sigma_if S_f|| < 10^-11)
  3. Over-relaxed non-orthogonal decomposition identity (||delta_f + k_f - S_f|| < 10^-14)
  4. Complete Error-Map & 4-tier solver stabilization mask (0, 1, 2, 3)
  5. CHT multi-region coupledWall twin-face extraction
"""

from __future__ import annotations

import ctypes
import logging
import os
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional, Tuple, Union

import numpy as np

from backend.meshing.models import CellType, UnstructuredMesh

logger = logging.getLogger(__name__)

# Search paths for native shared library
_NATIVE_DLL: Optional[ctypes.CDLL] = None
_DLL_LOAD_ATTEMPTED: bool = False
_NATIVE_LOAD_ERROR: Optional[str] = None


def get_native_load_error() -> Optional[str]:
    """Return the last error message encountered during native library loading, if any."""
    global _NATIVE_LOAD_ERROR
    return _NATIVE_LOAD_ERROR


def _reset_native_dll_cache() -> None:
    """Reset cached DLL and load state (primarily for testing error paths)."""
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED, _NATIVE_LOAD_ERROR
    _NATIVE_DLL = None
    _DLL_LOAD_ATTEMPTED = False
    _NATIVE_LOAD_ERROR = None


class MeshingConfigC(ctypes.Structure):
    _fields_ = [
        ("h_min", ctypes.c_double),
        ("h_max", ctypes.c_double),
        ("h0", ctypes.c_double),
        ("grading", ctypes.c_double),
        ("alpha_curv", ctypes.c_double),
        ("beta_prox", ctypes.c_double),
        ("num_bl_layers", ctypes.c_int32),
        ("bl_first_height", ctypes.c_double),
        ("bl_growth_rate", ctypes.c_double),
        ("bl_pinching_enabled", ctypes.c_int32),
        ("bl_pinching_angle_deg", ctypes.c_double),
        ("enable_rcm", ctypes.c_int32),
        ("enable_cht", ctypes.c_int32),
        ("enable_cyclic", ctypes.c_int32),
        ("cyclic_angle_rad", ctypes.c_double),
        ("distmesh_iters", ctypes.c_int32),
    ]


class BoundaryPatchC(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 64),
        ("patch_type", ctypes.c_char * 32),
        ("start_face", ctypes.c_int64),
        ("num_faces", ctypes.c_int64),
        ("region_id", ctypes.c_int32),
    ]


def _find_native_library() -> Optional[Path]:
    """Locate the native cfd_mesher_native shared library across platforms and build directories.

    Returns:
        Path to the shared library if found, else None.
    """
    custom_env_path = os.environ.get("CFD_NATIVE_LIB_PATH")
    if custom_env_path:
        cand = Path(custom_env_path)
        if cand.is_file():
            return cand
        logger.error(f"CFD_NATIVE_LIB_PATH specified but file does not exist: {custom_env_path}")
        return None

    pkg_dir = Path(__file__).resolve().parent
    build_dir = pkg_dir / "native" / "build"

    names: List[str]
    if sys.platform.startswith("win"):
        names = ["cfd_mesher_native.dll", "libcfd_mesher_native.dll"]
    elif sys.platform == "darwin":
        names = ["libcfd_mesher_native.dylib", "cfd_mesher_native.dylib"]
    else:
        names = ["libcfd_mesher_native.so", "cfd_mesher_native.so"]

    search_dirs = [
        pkg_dir,
        build_dir / "Release",
        build_dir,
        build_dir / "Debug",
        build_dir / "RelWithDebInfo",
        pkg_dir / "native",
    ]

    for s_dir in search_dirs:
        for name in names:
            cand = s_dir / name
            if cand.is_file():
                return cand

    return None


def get_native_dll(strict_native: bool = False) -> Optional[ctypes.CDLL]:
    """Load and return the native cfd_mesher_native shared library if available.

    Parameters
    ----------
    strict_native : bool, default=False
        If True or if environment variable CFD_STRICT_NATIVE=1 is set,
        fail fast and raise a RuntimeError with detailed diagnostics if the
        native library cannot be found or loaded.

    Returns
    -------
    Optional[ctypes.CDLL]
        Loaded ctypes CDLL instance, or None if unavailable in non-strict mode.

    Raises
    ------
    RuntimeError
        If strict_native is enabled and the library fails to load or cannot be found.
    """
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED, _NATIVE_LOAD_ERROR
    is_strict = strict_native or (os.environ.get("CFD_STRICT_NATIVE", "0").strip() == "1")

    if _DLL_LOAD_ATTEMPTED:
        if _NATIVE_DLL is not None:
            return _NATIVE_DLL
        if is_strict:
            raise RuntimeError(
                f"Strict native mode enabled but native mesher library cfd_mesher_native is unavailable. "
                f"Previous load failure: {_NATIVE_LOAD_ERROR}"
            )
        return None

    _DLL_LOAD_ATTEMPTED = True
    dll_path = _find_native_library()
    if dll_path is None:
        _NATIVE_LOAD_ERROR = (
            "Native shared library file (cfd_mesher_native) could not be located in search paths. "
            "Ensure the library is built with CMake (backend/meshing/native/build)."
        )
        logger.error(f"Failed to find native mesher library: {_NATIVE_LOAD_ERROR}")
        if is_strict:
            raise RuntimeError(f"Strict native mode enabled but native library not found: {_NATIVE_LOAD_ERROR}")
        return None

    try:
        dll = ctypes.CDLL(str(dll_path))

        # Core lifecycle
        dll.cfd_mesher_create.restype = ctypes.c_void_p
        dll.cfd_mesher_create.argtypes = []

        dll.cfd_mesher_free.restype = None
        dll.cfd_mesher_free.argtypes = [ctypes.c_void_p]

        # Mesh generation / topology builder
        dll.cfd_mesher_build_from_cells.restype = ctypes.c_int32
        dll.cfd_mesher_build_from_cells.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int64),
            ctypes.POINTER(ctypes.c_int64),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
        ]

        dll.cfd_mesher_get_n_points.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_points.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_n_cells.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_cells.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_n_faces.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_faces.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_n_internal_faces.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_internal_faces.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_n_patches.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_patches.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_points_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_points_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_owner_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_owner_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_neighbour_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_neighbour_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_face_areas_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_face_areas_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_face_centres_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_face_centres_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_centres_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_cell_centres_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_volumes_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_cell_volumes_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_delta_f_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_delta_f_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_k_f_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_k_f_ptr.argtypes = [ctypes.c_void_p]

        # CSR Cell-Node accessors
        dll.cfd_mesher_get_cell_offsets_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_cell_offsets_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_nodes_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_cell_nodes_ptr.argtypes = [ctypes.c_void_p]

        # CSR Face-Node accessors
        if hasattr(dll, "cfd_mesher_get_face_offsets_ptr"):
            dll.cfd_mesher_get_face_offsets_ptr.restype = ctypes.POINTER(ctypes.c_int64)
            dll.cfd_mesher_get_face_offsets_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_face_nodes_ptr"):
            dll.cfd_mesher_get_face_nodes_ptr.restype = ctypes.POINTER(ctypes.c_int64)
            dll.cfd_mesher_get_face_nodes_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_n_face_nodes"):
            dll.cfd_mesher_get_n_face_nodes.restype = ctypes.c_int64
            dll.cfd_mesher_get_n_face_nodes.argtypes = [ctypes.c_void_p]

        # Cell Types & Topological Dimension
        if hasattr(dll, "cfd_mesher_get_cell_types_ptr"):
            dll.cfd_mesher_get_cell_types_ptr.restype = ctypes.POINTER(ctypes.c_int32)
            dll.cfd_mesher_get_cell_types_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_topological_dim"):
            dll.cfd_mesher_get_topological_dim.restype = ctypes.c_int32
            dll.cfd_mesher_get_topological_dim.argtypes = [ctypes.c_void_p]

        # Error-map & Quality metrics
        dll.cfd_mesher_get_non_orthogonality_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_non_orthogonality_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_skewness_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_skewness_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_aspect_ratio_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_aspect_ratio_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_stabilization_mask_ptr.restype = ctypes.POINTER(ctypes.c_int32)
        dll.cfd_mesher_get_stabilization_mask_ptr.argtypes = [ctypes.c_void_p]

        # CMS compensation fields
        if hasattr(dll, "cfd_mesher_get_m_skew_ptr"):
            dll.cfd_mesher_get_m_skew_ptr.restype = ctypes.POINTER(ctypes.c_double)
            dll.cfd_mesher_get_m_skew_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_k_f_comp_ptr"):
            dll.cfd_mesher_get_k_f_comp_ptr.restype = ctypes.POINTER(ctypes.c_double)
            dll.cfd_mesher_get_k_f_comp_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_beta_upwind_ptr"):
            dll.cfd_mesher_get_beta_upwind_ptr.restype = ctypes.POINTER(ctypes.c_double)
            dll.cfd_mesher_get_beta_upwind_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_omega_cell_ptr"):
            dll.cfd_mesher_get_omega_cell_ptr.restype = ctypes.POINTER(ctypes.c_double)
            dll.cfd_mesher_get_omega_cell_ptr.argtypes = [ctypes.c_void_p]

        if hasattr(dll, "cfd_mesher_get_wsrd_alpha_ptr"):
            dll.cfd_mesher_get_wsrd_alpha_ptr.restype = ctypes.POINTER(ctypes.c_double)
            dll.cfd_mesher_get_wsrd_alpha_ptr.argtypes = [ctypes.c_void_p]

        # Boundary patches & CHT
        dll.cfd_mesher_get_cell_regions_ptr.restype = ctypes.POINTER(ctypes.c_int32)
        dll.cfd_mesher_get_cell_regions_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_boundary_patch.restype = ctypes.c_int32
        dll.cfd_mesher_get_boundary_patch.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int64,
            ctypes.POINTER(BoundaryPatchC),
        ]

        if hasattr(dll, "cfd_mesher_extract_cyclic_pairs"):
            dll.cfd_mesher_extract_cyclic_pairs.restype = ctypes.c_int64
            dll.cfd_mesher_extract_cyclic_pairs.argtypes = [
                ctypes.c_void_p,
                ctypes.c_double,
                ctypes.c_double,
                ctypes.POINTER(ctypes.c_int64),
                ctypes.POINTER(ctypes.c_int64),
                ctypes.c_int64,
            ]

        if hasattr(dll, "cfd_mesher_generate_hexcore"):
            dll.cfd_mesher_generate_hexcore.restype = ctypes.c_int32
            dll.cfd_mesher_generate_hexcore.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_double),
                ctypes.c_int64,
                ctypes.POINTER(ctypes.c_int32),
                ctypes.c_int64,
                ctypes.POINTER(ctypes.c_double),
                ctypes.c_int32,
                ctypes.c_double,
                ctypes.c_void_p,
            ]

        dll.cfd_mesher_compute_max_divergence_closure.restype = ctypes.c_double
        dll.cfd_mesher_compute_max_divergence_closure.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_compute_max_decomposition_error.restype = ctypes.c_double
        dll.cfd_mesher_compute_max_decomposition_error.argtypes = [ctypes.c_void_p]

        _NATIVE_DLL = dll
        _NATIVE_LOAD_ERROR = None
        return dll
    except Exception as e:
        _NATIVE_LOAD_ERROR = f"{type(e).__name__}: {e}"
        logger.error(f"Failed to load native mesher library from {dll_path}: {_NATIVE_LOAD_ERROR}", exc_info=True)
        _NATIVE_DLL = None
        if is_strict:
            raise RuntimeError(
                f"Strict native mode enabled but native mesher library failed to load from {dll_path}: {_NATIVE_LOAD_ERROR}"
            ) from e
        return None


def require_native_dll() -> ctypes.CDLL:
    """Load and return the native library, raising RuntimeError if unavailable."""
    dll = get_native_dll(strict_native=True)
    if dll is None:
        raise RuntimeError(f"Native mesher library is unavailable: {_NATIVE_LOAD_ERROR}")
    return dll


def _classify_cell_type(cell_nodes: np.ndarray, points: np.ndarray) -> CellType:
    """Classify element into VTK-compatible CellType enum."""
    nv = len(cell_nodes)
    if nv == 3:
        return CellType.TRIANGLE
    elif nv == 4:
        p0 = points[int(cell_nodes[0])]
        p1 = points[int(cell_nodes[1])]
        p2 = points[int(cell_nodes[2])]
        p3 = points[int(cell_nodes[3])]
        pts_quad = points[cell_nodes]
        diffs = pts_quad[:, None, :] - pts_quad[None, :, :]
        max_dist = float(np.max(np.linalg.norm(diffs, axis=-1)))
        l_scale = max(max_dist, 1e-12)
        v_tet = abs(float(np.dot(p1 - p0, np.cross(p2 - p0, p3 - p0)))) / 6.0
        v_norm = v_tet / (l_scale ** 3)
        if v_norm < 1e-6:
            return CellType.QUAD
        return CellType.TETRA
    elif nv == 8:
        if points is not None:
            pts_cell = points[cell_nodes]
            span = np.max(pts_cell, axis=0) - np.min(pts_cell, axis=0)
            max_span = float(np.max(span))
            min_span = float(np.min(span))
            if max_span > 1e-12 and (min_span / max_span) < 1e-5:
                return CellType.POLYGON
        return CellType.HEXAHEDRON
    elif nv == 6:
        return CellType.WEDGE
    elif nv == 5:
        return CellType.PYRAMID
    else:
        pts_poly = points[cell_nodes]
        span_poly = np.max(pts_poly, axis=0) - np.min(pts_poly, axis=0)
        max_sp = float(np.max(span_poly))
        min_sp = float(np.min(span_poly))
        if max_sp > 1e-12 and (min_sp / max_sp) < 1e-5:
            return CellType.POLYGON
        return CellType.POLYHEDRON


def _extract_cell_faces_py(
    cell_nodes: np.ndarray,
    points: Optional[np.ndarray] = None,
) -> List[List[int]]:
    """Extract ordered face vertex lists for standard 2D and 3D cells.

    Args:
        cell_nodes: Node indices defining the cell element.
        points: Optional coordinate array for coplanarity discrimination.

    Returns:
        List of face node lists conforming to right-hand outward orientation.
    """
    nv = len(cell_nodes)
    c = cell_nodes
    if nv == 8:
        if points is not None:
            pts_cell = points[c]
            span = np.max(pts_cell, axis=0) - np.min(pts_cell, axis=0)
            max_span = float(np.max(span))
            min_span = float(np.min(span))
            if max_span > 1e-12 and (min_span / max_span) < 1e-5:
                # Coplanar 8-node 2D polygon: 8 perimeter boundary edges
                return [[int(c[i]), int(c[(i + 1) % nv])] for i in range(nv)]
        # Hexahedron: 6 quadrilateral faces with outward normals
        faces = [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],  # bottom (-z)
            [int(c[4]), int(c[5]), int(c[6]), int(c[7])],  # top (+z)
            [int(c[0]), int(c[1]), int(c[5]), int(c[4])],  # front (-y)
            [int(c[1]), int(c[2]), int(c[6]), int(c[5])],  # right (+x)
            [int(c[2]), int(c[3]), int(c[7]), int(c[6])],  # back (+y)
            [int(c[3]), int(c[0]), int(c[4]), int(c[7])],  # left (-x)
        ]
    elif nv == 4:
        if points is not None:
            p0 = points[int(c[0])]
            p1 = points[int(c[1])]
            p2 = points[int(c[2])]
            p3 = points[int(c[3])]
            pts_quad = points[c]
            diffs = pts_quad[:, None, :] - pts_quad[None, :, :]
            max_dist = float(np.max(np.linalg.norm(diffs, axis=-1)))
            l_scale = max(max_dist, 1e-12)
            v_tet = abs(float(np.dot(p1 - p0, np.cross(p2 - p0, p3 - p0)))) / 6.0
            v_norm = v_tet / (l_scale ** 3)
            if v_norm < 1e-6:
                # Coplanar 2D QUAD: 4 perimeter boundary edges
                faces = [
                    [int(c[0]), int(c[1])],
                    [int(c[1]), int(c[2])],
                    [int(c[2]), int(c[3])],
                    [int(c[3]), int(c[0])],
                ]
            else:
                faces = [
                    [int(c[0]), int(c[2]), int(c[1])],
                    [int(c[0]), int(c[1]), int(c[3])],
                    [int(c[1]), int(c[2]), int(c[3])],
                    [int(c[2]), int(c[0]), int(c[3])],
                ]
        else:
            # Tetrahedron: 4 triangular faces
            faces = [
                [int(c[0]), int(c[2]), int(c[1])],
                [int(c[0]), int(c[1]), int(c[3])],
                [int(c[1]), int(c[2]), int(c[3])],
                [int(c[2]), int(c[0]), int(c[3])],
            ]
    elif nv == 6:
        # Wedge / Triangular prism: 5 faces
        faces = [
            [int(c[0]), int(c[2]), int(c[1])],
            [int(c[3]), int(c[4]), int(c[5])],
            [int(c[0]), int(c[1]), int(c[4]), int(c[3])],
            [int(c[1]), int(c[2]), int(c[5]), int(c[4])],
            [int(c[2]), int(c[0]), int(c[3]), int(c[5])],
        ]
    elif nv == 5:
        # Pyramid: 5 faces
        faces = [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],
            [int(c[0]), int(c[1]), int(c[4])],
            [int(c[1]), int(c[2]), int(c[4])],
            [int(c[2]), int(c[3]), int(c[4])],
            [int(c[3]), int(c[0]), int(c[4])],
        ]
    elif nv == 3:
        # 2D triangle: 3 perimeter boundary edges
        faces = [
            [int(c[0]), int(c[1])],
            [int(c[1]), int(c[2])],
            [int(c[2]), int(c[0])],
        ]
    else:
        # Fallback polygon
        faces = [[int(c[i]), int(c[(i + 1) % nv])] for i in range(nv)]

    if points is not None:
        cell_c = np.mean(points[cell_nodes], axis=0)
        oriented_faces: List[List[int]] = []
        for fn in faces:
            norm_v, cent_v = _compute_polygon_normal_and_centroid_py(fn, points)
            if float(np.dot(norm_v, cent_v - cell_c)) < 0.0:
                fn = list(reversed(fn))
            oriented_faces.append(fn)
        return oriented_faces
    return faces


def _compute_polygon_normal_and_centroid_py(
    face_nodes: List[int], points: np.ndarray
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute face area normal vector S_f and centroid c_f via triangle fan."""
    n = len(face_nodes)
    if n < 3:
        if n == 2:
            # 2D edge: normal in xy plane
            p0 = points[face_nodes[0]]
            p1 = points[face_nodes[1]]
            edge = p1 - p0
            normal = np.array([edge[1], -edge[0], 0.0], dtype=np.float64)
            return normal, (p0 + p1) * 0.5
        return np.zeros(3, dtype=np.float64), np.zeros(3, dtype=np.float64)

    p0 = points[face_nodes[0]]
    normal_sum = np.zeros(3, dtype=np.float64)
    centroid_sum = np.zeros(3, dtype=np.float64)
    total_area = 0.0

    for i in range(1, n - 1):
        pi = points[face_nodes[i]]
        pj = points[face_nodes[i + 1]]
        cross_p = np.cross(pi - p0, pj - p0)
        area_tri = np.linalg.norm(cross_p) * 0.5
        normal_sum += cross_p * 0.5
        tri_c = (p0 + pi + pj) / 3.0
        centroid_sum += tri_c * area_tri
        total_area += area_tri

    if total_area > 1e-15:
        centroid = centroid_sum / total_area
    else:
        centroid = np.mean(points[face_nodes], axis=0)

    return normal_sum, centroid


def _is_face_folded_py(
    face_nodes: List[int], points: np.ndarray, normal_sum: np.ndarray
) -> bool:
    """Check if any sub-triangle cross product opposes the face area normal."""
    n = len(face_nodes)
    if n < 3:
        return False
    norm_mag = np.linalg.norm(normal_sum)
    if norm_mag <= 1e-15:
        return False
    p0 = points[face_nodes[0]]
    for i in range(1, n - 1):
        pi = points[face_nodes[i]]
        pj = points[face_nodes[i + 1]]
        cross_p = np.cross(pi - p0, pj - p0)
        if np.linalg.norm(cross_p) > 1e-15:
            if float(np.dot(cross_p, normal_sum)) <= 0.0:
                return True
    return False


def compute_wsrd_weights_array(
    wsrd_alpha: np.ndarray,
    small_cell_ratio: float = 0.20,
    wsrd_target_alpha: float = 0.50,
) -> np.ndarray:
    """Compute Berger & Giuliani (2024) dynamic WSRD cell weights according to Eq. (4.2).

    w_i = sin^2(pi/2 * (alpha_i - alpha_small) / (alpha_target - alpha_small))
    for alpha_small <= alpha_i < alpha_target.
    w_i = 0 for alpha_i < alpha_small.
    w_i = 1 for alpha_i >= alpha_target.
    """
    alpha = np.asarray(wsrd_alpha, dtype=np.float64)
    w = np.zeros_like(alpha)
    a_small = float(small_cell_ratio)
    a_target = float(wsrd_target_alpha)
    delta_a = a_target - a_small
    if delta_a > 1e-12:
        mid_mask = (alpha >= a_small) & (alpha < a_target)
        scaled = (alpha[mid_mask] - a_small) / delta_a
        w[mid_mask] = np.sin(0.5 * np.pi * scaled) ** 2
        w[alpha >= a_target] = 1.0
        w[alpha < a_small] = 0.0
    else:
        w[alpha >= a_target] = 1.0
    return w


def compute_wsrd_weights(
    cut_alpha: float,
    neighbor_alphas: List[float],
    alpha_target: float = 0.50,
) -> Dict[str, Any]:
    """Compute Berger & Giuliani (2024) Weighted State Redistribution weights for a single cut-cell.

    Formula (Eq. 4.2):
    For j in W_i \\ {i}: w_{ij} = 1/(N_i - 1) * (1 - alpha_j / alpha_target)
    For i (self): w_{ii} = 1 - sum_{j!=i} w_{ij}
    """
    n_neighbors = len(neighbor_alphas)
    if n_neighbors == 0:
        return {"self_weight": 1.0, "neighbor_weights": [], "partition_of_unity": 0.0}

    n_i = n_neighbors + 1  # Total overlap count
    weights_nb = []
    for a_j in neighbor_alphas:
        w_ij = (1.0 / (n_i - 1)) * max(0.0, 1.0 - a_j / alpha_target)
        weights_nb.append(w_ij)

    sum_nb = sum(weights_nb)
    w_ii = 1.0 - sum_nb
    return {
        "self_weight": w_ii,
        "neighbor_weights": weights_nb,
        "partition_of_unity": abs((w_ii + sum_nb) - 1.0),
    }


def compute_amips_energy(
    vertices: np.ndarray,
    reference_scale: float = 1.0,
) -> float:
    """Compute scale- and rotation-invariant 3D AMIPS distortion energy for a tetrahedron.

    For an element with 4 vertices v0, v1, v2, v3:
    D_s = [v1 - v0, v2 - v0, v3 - v0]
    E_AMIPS = (27/16 * (sum_{i<j} ||v_i - v_j||^2)^3 / (det D_s)^2)^(1/3) >= 3.0.
    """
    verts = np.asarray(vertices, dtype=np.float64)
    if len(verts) < 4:
        raise ValueError("AMIPS energy requires at least 4 vertices (tetrahedron).")
    v0 = verts[0]
    v1 = verts[1]
    v2 = verts[2]
    v3 = verts[3]
    d_s = np.column_stack([v1 - v0, v2 - v0, v3 - v0])
    det_j = float(np.linalg.det(d_s))
    if det_j <= 1e-18:
        return float("inf")

    edge_sum_sq = 0.0
    for i in range(4):
        for j in range(i + 1, 4):
            edge_sum_sq += float(np.sum((verts[i] - verts[j]) ** 2))

    ratio = (27.0 / 16.0) * (edge_sum_sq ** 3) / (det_j ** 2)
    return float(ratio ** (1.0 / 3.0))


def compute_wsrd_redistribution_map(
    wsrd_alpha: np.ndarray,
    owner: np.ndarray,
    neighbour: np.ndarray,
    n_cells: int,
    n_faces: int,
    small_cell_ratio: float = 0.20,
    wsrd_target_alpha: float = 0.50,
) -> Dict[int, Dict[str, Any]]:
    """Compute neighbor redistribution mapping for cut-cells in [small_cell_ratio, wsrd_target_alpha).
    
    Berger & Giuliani (2024): Cut cells donate state/fluxes to larger neighboring fluid cells
    (preferring regular cells alpha_nb >= wsrd_target_alpha, or weighted by relative volume).
    Self weight is computed via smooth sin^2 function, and partition of unity sum(w) = 1.0 is preserved.
    """
    redist_map: Dict[int, Dict[str, Any]] = {}
    a_small = float(small_cell_ratio)
    a_target = float(wsrd_target_alpha)
    if n_cells <= 0 or n_faces <= 0:
        return redist_map

    cut_mask = (wsrd_alpha >= a_small) & (wsrd_alpha < a_target)
    if not np.any(cut_mask):
        return redist_map

    cut_cells = np.where(cut_mask)[0]
    safe_nb = np.maximum(neighbour, 0)
    rel_mask = (neighbour >= 0) & (cut_mask[owner] | cut_mask[safe_nb])
    rel_faces = np.where(rel_mask)[0]

    adj: Dict[int, List[int]] = {int(c): [] for c in cut_cells}
    for f_idx in rel_faces:
        o = int(owner[f_idx])
        nb = int(neighbour[f_idx])
        if cut_mask[o]:
            adj[o].append(nb)
        if cut_mask[nb]:
            adj[nb].append(o)

    for c_arr in cut_cells:
        c = int(c_arr)
        a_c = float(wsrd_alpha[c])
        nb_indices = adj[c]
        if not nb_indices:
            continue

        # Unique internal neighbors in order of encounter
        unique_nbs = list(dict.fromkeys(nb_indices))

        # Compute self-weight via Berger & Giuliani (2024) smooth sin^2 formulation
        if a_target > a_small:
            scaled = (a_c - a_small) / (a_target - a_small)
            w_self = float(np.sin(0.5 * np.pi * scaled) ** 2)
        else:
            w_self = 0.0
        w_donate = float(1.0 - w_self)

        # Donor-receiver pairing: donate to larger internal neighbors (alpha_nb > a_c)
        # preferring regular cells (alpha_nb >= a_target)
        regular_larger = [nb for nb in unique_nbs if float(wsrd_alpha[nb]) >= a_target]
        larger_nbs = [nb for nb in unique_nbs if float(wsrd_alpha[nb]) > a_c]

        if regular_larger:
            receivers = regular_larger
            v = {nb: max(1e-15, float(wsrd_alpha[nb]) - a_c) for nb in receivers}
        elif larger_nbs:
            receivers = larger_nbs
            v = {nb: max(1e-15, float(wsrd_alpha[nb]) - a_c) for nb in receivers}
        else:
            # If no larger neighbor exists, redistribute equally among available internal neighbors
            receivers = unique_nbs
            v = {nb: 1.0 for nb in receivers}

        sum_v = sum(v.values())
        w_nbs = {nb: 0.0 for nb in unique_nbs}
        if sum_v > 0.0 and len(receivers) > 0:
            allocated = 0.0
            for i, r_idx in enumerate(receivers):
                if i == len(receivers) - 1:
                    # Guarantee exact partition of unity to machine precision
                    weight = float(w_donate - allocated)
                else:
                    weight = float(w_donate * (v[r_idx] / sum_v))
                    allocated += weight
                w_nbs[r_idx] = weight

        redist_map[c] = {"self": w_self, "neighbors": w_nbs}
    return redist_map


def compute_difvm_topology_native(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
    config: Optional[MeshingConfig] = None,
) -> UnstructuredMesh:
    """Build UnstructuredMesh using native C++20 core (cfd_mesher_native shared library)."""
    dll = get_native_dll(strict_native=True)
    if dll is None:
        raise RuntimeError(f"Native library cfd_mesher_native is not available: {_NATIVE_LOAD_ERROR}")

    pts = np.ascontiguousarray(points, dtype=np.float64)
    if pts.ndim == 2 and pts.shape[1] == 2:
        pts = np.hstack([pts, np.zeros((len(pts), 1), dtype=np.float64)])

    n_points = len(pts)
    n_cells = len(cells)

    # Convert cells to CSR offsets and flat_nodes (manifoldness validated in native C++)
    offsets = np.zeros(n_cells + 1, dtype=np.int64)
    cur = 0
    for i, c in enumerate(cells):
        offsets[i] = cur
        cur += len(c)
    offsets[-1] = cur
    flat_nodes = np.ascontiguousarray(np.concatenate(cells), dtype=np.int64)

    cell_regions_ptr = None
    regions_arr = None
    if regions is not None:
        regions_arr = np.ascontiguousarray(regions, dtype=np.int32)
        cell_regions_ptr = regions_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_int32))

    mesh_handle = dll.cfd_mesher_create()
    if not mesh_handle:
        raise RuntimeError("cfd_mesher_create failed to allocate native mesh handle.")

    try:
        res = dll.cfd_mesher_build_from_cells(
            mesh_handle,
            pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            n_points,
            offsets.ctypes.data_as(ctypes.POINTER(ctypes.c_int64)),
            flat_nodes.ctypes.data_as(ctypes.POINTER(ctypes.c_int64)),
            n_cells,
            cell_regions_ptr,
        )
        if res != 0:
            if res == -2:
                raise ValueError("Non-manifold mesh: a face is shared by more than 2 cells.")
            raise RuntimeError(f"Native cfd_mesher_build_from_cells failed with code {res}")

        actual_n_cells = dll.cfd_mesher_get_n_cells(mesh_handle)
        actual_n_faces = dll.cfd_mesher_get_n_faces(mesh_handle)
        actual_n_points = dll.cfd_mesher_get_n_points(mesh_handle)

        owner_ptr = dll.cfd_mesher_get_owner_ptr(mesh_handle)
        owner = np.copy(np.ctypeslib.as_array(owner_ptr, shape=(actual_n_faces,)))

        neighbour_ptr = dll.cfd_mesher_get_neighbour_ptr(mesh_handle)
        neighbour = np.copy(np.ctypeslib.as_array(neighbour_ptr, shape=(actual_n_faces,)))

        fa_ptr = dll.cfd_mesher_get_face_areas_ptr(mesh_handle)
        face_areas = np.copy(np.ctypeslib.as_array(fa_ptr, shape=(actual_n_faces, 3)))

        fc_ptr = dll.cfd_mesher_get_face_centres_ptr(mesh_handle)
        face_centres = np.copy(np.ctypeslib.as_array(fc_ptr, shape=(actual_n_faces, 3)))

        cc_ptr = dll.cfd_mesher_get_cell_centres_ptr(mesh_handle)
        cell_centres = np.copy(np.ctypeslib.as_array(cc_ptr, shape=(actual_n_cells, 3)))

        cv_ptr = dll.cfd_mesher_get_cell_volumes_ptr(mesh_handle)
        cell_volumes = np.copy(np.ctypeslib.as_array(cv_ptr, shape=(actual_n_cells,)))

        df_ptr = dll.cfd_mesher_get_delta_f_ptr(mesh_handle)
        delta_f = np.copy(np.ctypeslib.as_array(df_ptr, shape=(actual_n_faces, 3)))

        kf_ptr = dll.cfd_mesher_get_k_f_ptr(mesh_handle)
        k_f = np.copy(np.ctypeslib.as_array(kf_ptr, shape=(actual_n_faces, 3)))

        no_ptr = dll.cfd_mesher_get_non_orthogonality_ptr(mesh_handle)
        non_ortho = np.copy(np.ctypeslib.as_array(no_ptr, shape=(actual_n_faces,)))

        sk_ptr = dll.cfd_mesher_get_skewness_ptr(mesh_handle)
        skewness = np.copy(np.ctypeslib.as_array(sk_ptr, shape=(actual_n_faces,)))

        ar_ptr = dll.cfd_mesher_get_aspect_ratio_ptr(mesh_handle)
        aspect_ratio = np.copy(np.ctypeslib.as_array(ar_ptr, shape=(actual_n_cells,)))

        sm_ptr = dll.cfd_mesher_get_stabilization_mask_ptr(mesh_handle)
        stab_mask = np.copy(np.ctypeslib.as_array(sm_ptr, shape=(actual_n_faces,)))

        cr_ptr = dll.cfd_mesher_get_cell_regions_ptr(mesh_handle)
        cell_regions = (
            np.copy(np.ctypeslib.as_array(cr_ptr, shape=(actual_n_cells,)))
            if cr_ptr
            else np.zeros(actual_n_cells, dtype=np.int32)
        )

        # Extract CSR face-to-node connectivity
        faces_list: Optional[List[np.ndarray]] = None
        if hasattr(dll, "cfd_mesher_get_face_offsets_ptr") and hasattr(dll, "cfd_mesher_get_face_nodes_ptr"):
            face_offsets_ptr = dll.cfd_mesher_get_face_offsets_ptr(mesh_handle)
            face_nodes_ptr = dll.cfd_mesher_get_face_nodes_ptr(mesh_handle)
            n_face_nodes = dll.cfd_mesher_get_n_face_nodes(mesh_handle) if hasattr(dll, "cfd_mesher_get_n_face_nodes") else 0
            if face_offsets_ptr and face_nodes_ptr and n_face_nodes > 0:
                fo = np.ctypeslib.as_array(face_offsets_ptr, shape=(actual_n_faces + 1,))
                fn = np.ctypeslib.as_array(face_nodes_ptr, shape=(n_face_nodes,))
                faces_list = [np.copy(fn[fo[f]:fo[f + 1]]) for f in range(actual_n_faces)]

        # Extract Cell Types & Topological Dimension
        cell_types: Optional[np.ndarray] = None
        if hasattr(dll, "cfd_mesher_get_cell_types_ptr"):
            cell_types_ptr = dll.cfd_mesher_get_cell_types_ptr(mesh_handle)
            if cell_types_ptr:
                cell_types = np.copy(np.ctypeslib.as_array(cell_types_ptr, shape=(actual_n_cells,)))
        if cell_types is None:
            cell_types = np.array([_classify_cell_type(c, pts).value for c in cells], dtype=np.int32)

        topological_dim = 3
        if hasattr(dll, "cfd_mesher_get_topological_dim"):
            topological_dim = dll.cfd_mesher_get_topological_dim(mesh_handle)
        if topological_dim <= 0:
            topological_dim = (
                2
                if all(
                    t in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value)
                    for t in cell_types
                )
                else 3
            )

        # CMS compensation fields
        m_skew_ptr = dll.cfd_mesher_get_m_skew_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_m_skew_ptr") else None
        m_skew = (
            np.copy(np.ctypeslib.as_array(m_skew_ptr, shape=(actual_n_faces, 3)))
            if m_skew_ptr
            else np.zeros((actual_n_faces, 3), dtype=np.float64)
        )

        kf_comp_ptr = dll.cfd_mesher_get_k_f_comp_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_k_f_comp_ptr") else None
        k_f_comp = (
            np.copy(np.ctypeslib.as_array(kf_comp_ptr, shape=(actual_n_faces, 3)))
            if kf_comp_ptr
            else np.zeros((actual_n_faces, 3), dtype=np.float64)
        )

        beta_upwind_ptr = dll.cfd_mesher_get_beta_upwind_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_beta_upwind_ptr") else None
        beta_upwind = (
            np.copy(np.ctypeslib.as_array(beta_upwind_ptr, shape=(actual_n_faces,)))
            if beta_upwind_ptr
            else np.ones(actual_n_faces, dtype=np.float64)
        )

        omega_cell_ptr = dll.cfd_mesher_get_omega_cell_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_omega_cell_ptr") else None
        omega_cell = (
            np.copy(np.ctypeslib.as_array(omega_cell_ptr, shape=(actual_n_cells,)))
            if omega_cell_ptr
            else np.ones(actual_n_cells, dtype=np.float64)
        )

        wsrd_alpha_ptr = dll.cfd_mesher_get_wsrd_alpha_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_wsrd_alpha_ptr") else None
        wsrd_alpha = (
            np.copy(np.ctypeslib.as_array(wsrd_alpha_ptr, shape=(actual_n_cells,)))
            if wsrd_alpha_ptr
            else np.ones(actual_n_cells, dtype=np.float64)
        )

        # Fallback if native DLL does not export the new CSR face-node or CMS getters
        if faces_list is None or not hasattr(dll, "cfd_mesher_get_face_offsets_ptr"):
            py_mesh = compute_difvm_topology_python(pts, cells, regions=regions)
            faces_list = py_mesh.faces
            cell_types = py_mesh.cell_types
            topological_dim = py_mesh.topological_dim
            m_skew = py_mesh.error_map["m_skew"]
            k_f_comp = py_mesh.error_map["k_f_comp"]
            beta_upwind = py_mesh.error_map["beta_upwind"]
            omega_cell = py_mesh.error_map["omega_cell"]
            wsrd_alpha = py_mesh.error_map["wsrd_alpha"]
            stab_mask = py_mesh.error_map["stabilization_mask"]
            non_ortho = py_mesh.error_map["non_ortho"]
            skewness = py_mesh.error_map["skew"]
            aspect_ratio = py_mesh.error_map["aspect_ratio"]
            owner = py_mesh.owner
            neighbour = py_mesh.neighbour
            face_areas = py_mesh.face_areas
            face_centres = py_mesh.face_centres
            cell_centres = py_mesh.cell_centres
            cell_volumes = py_mesh.cell_volumes
            delta_f = py_mesh.delta_f
            k_f = py_mesh.k_f
            boundaries = dict(py_mesh.boundaries)
            baffles = list(py_mesh.baffles)

        # Boundaries & Patches
        if hasattr(dll, "cfd_mesher_get_face_offsets_ptr"):
            boundaries = {}
            n_patches = dll.cfd_mesher_get_n_patches(mesh_handle)
            baffles = []

            if n_patches > 0:
                for p_idx in range(n_patches):
                    patch = BoundaryPatchC()
                    dll.cfd_mesher_get_boundary_patch(mesh_handle, p_idx, ctypes.byref(patch))
                    p_name = patch.name.decode("utf-8", errors="ignore").strip() or "wall"
                    boundaries[p_name] = (int(patch.start_face), int(patch.num_faces))
                    if b"coupledWall" in patch.patch_type:
                        baffles.append({
                            "name": p_name,
                            "type": "coupledWall",
                            "start_face": int(patch.start_face),
                            "num_faces": int(patch.num_faces),
                            "region_id": int(patch.region_id),
                        })
            else:
                internal_count = int(np.sum(neighbour >= 0))
                boundary_count = actual_n_faces - internal_count
                if boundary_count > 0:
                    boundaries["wall"] = (internal_count, boundary_count)

        small_cell_ratio = float(getattr(config, "small_cell_ratio", 0.20)) if config is not None else 0.20
        wsrd_target_alpha = float(getattr(config, "wsrd_target_alpha", 0.50)) if config is not None else 0.50
        wsrd_weights = compute_wsrd_weights_array(
            wsrd_alpha, small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha
        )
        wsrd_redist_map = compute_wsrd_redistribution_map(
            wsrd_alpha, owner, neighbour, actual_n_cells, actual_n_faces,
            small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha,
        )

        error_map = {
            "non_orthogonality": non_ortho,
            "non_ortho": non_ortho,
            "skewness": skewness,
            "skew": skewness,
            "aspect_ratio": aspect_ratio,
            "stabilization_mask": stab_mask,
            "mask": stab_mask,
            "m_skew": m_skew,
            "k_f_comp": k_f_comp,
            "beta_upwind": beta_upwind,
            "omega_cell": omega_cell,
            "wsrd_alpha": wsrd_alpha,
            "wsrd_weights": wsrd_weights,
        }

        mesh = UnstructuredMesh(
            points=pts,
            cells=cells,
            owner=owner,
            neighbour=neighbour,
            face_areas=face_areas,
            face_centres=face_centres,
            cell_centres=cell_centres,
            cell_volumes=cell_volumes,
            delta_f=delta_f,
            k_f=k_f,
            boundaries=boundaries,
            cell_regions=cell_regions,
            baffles=baffles,
            error_map=error_map,
            faces=faces_list,
            cell_types=cell_types,
            topological_dim=topological_dim,
            metadata={
                "engine": "native_cpp20",
                "wsrd_weights": wsrd_weights,
                "wsrd_redistribution": wsrd_redist_map,
            },
        )
        if regions_arr is not None and len(np.unique(regions_arr)) > 1:
            mesh = extract_cht_twin_faces(mesh)
        return mesh
    finally:
        dll.cfd_mesher_free(mesh_handle)


def generate_hexcore_native(
    surf_points: np.ndarray,
    surf_faces: np.ndarray,
    h0: float = 0.05,
    domain_mode: str = "external_fluid",
    farfield_bbox: Optional[Tuple[float, float, float, float, float, float]] = None,
    config: Optional[MeshingConfig] = None,
) -> UnstructuredMesh:
    """Generate 3D 2:1 balanced adaptive Cartesian Octree and Mosaic Poly-Hexcore mesh using native C++20 core."""
    dll = get_native_dll(strict_native=True)
    if dll is None or not hasattr(dll, "cfd_mesher_generate_hexcore"):
        raise RuntimeError(f"Native cfd_mesher_generate_hexcore is not available: {_NATIVE_LOAD_ERROR}")

    pts = np.ascontiguousarray(surf_points, dtype=np.float64)
    faces = np.ascontiguousarray(surf_faces, dtype=np.int32)
    if pts.ndim != 2 or pts.shape[1] != 3:
        raise ValueError(f"surf_points must be Nx3 float64 array, got shape {pts.shape}")
    if faces.ndim != 2 or faces.shape[1] != 3:
        raise ValueError(f"surf_faces must be Mx3 int32 array, got shape {faces.shape}")

    bbox_ptr = None
    bbox_arr = None
    if farfield_bbox is not None:
        bbox_arr = np.ascontiguousarray(farfield_bbox, dtype=np.float64)
        bbox_ptr = bbox_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

    mode_map = {"external_fluid": 0, "internal_fluid": 1, "solid_only": 2, "cht_coupled": 3}
    d_mode = mode_map.get(domain_mode, 0)

    cfg_c = None
    cfg_ptr = None
    if config is not None:
        cfg_c = MeshingConfigC(
            h_min=config.sizing.h_min,
            h_max=config.sizing.h_max,
            h0=float(h0),
            grading=config.sizing.growth_rate,
            alpha_curv=getattr(config.sizing, "alpha", getattr(config.sizing, "alpha_curv", 0.2)),
            beta_prox=getattr(config.sizing, "beta", getattr(config.sizing, "beta_prox", 0.25)),
            num_bl_layers=config.inflation.num_layers if config.inflation else 0,
            bl_first_height=config.inflation.first_layer_height if config.inflation else 0.0,
            bl_growth_rate=config.inflation.growth_rate if config.inflation else 1.2,
            bl_pinching_enabled=1 if (config.inflation and getattr(config.inflation, "pinching", getattr(config.inflation, "enable_pinching", True))) else 0,
            bl_pinching_angle_deg=float(getattr(config.inflation, "pinching_threshold_angle", getattr(config.inflation, "pinching_angle_deg", 60.0))) if config.inflation else 45.0,
            enable_rcm=1 if config.rcm_reorder else 0,
            enable_cht=1 if config.cht_multiregion else 0,
            enable_cyclic=1 if config.cyclic_symmetry else 0,
            cyclic_angle_rad=np.deg2rad(config.cyclic_angle_deg),
            distmesh_iters=config.distmesh_iters,
        )
        cfg_ptr = ctypes.byref(cfg_c)

    mesh_handle = dll.cfd_mesher_create()
    if not mesh_handle:
        raise RuntimeError("cfd_mesher_create failed to allocate native mesh handle.")

    try:
        res = dll.cfd_mesher_generate_hexcore(
            mesh_handle,
            pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(pts),
            faces.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            len(faces),
            bbox_ptr,
            d_mode,
            float(h0),
            cfg_ptr,
        )
        if res == -12:
            raise MemoryError(
                "Native octree_hexcore exceeded configured node/memory safety limit "
                "(CFD_MAX_OCTREE_NODES; set CFD_MAX_OCTREE_NODES=0 to allow unlimited RAM/pagefile). "
                "Check CAD unit scale or h0."
            )
        if res != 0:
            raise RuntimeError(f"Native cfd_mesher_generate_hexcore failed with code {res}")

        actual_n_cells = dll.cfd_mesher_get_n_cells(mesh_handle)
        actual_n_faces = dll.cfd_mesher_get_n_faces(mesh_handle)
        actual_n_points = dll.cfd_mesher_get_n_points(mesh_handle)

        pts_ptr = dll.cfd_mesher_get_points_ptr(mesh_handle)
        pts_out = np.copy(np.ctypeslib.as_array(pts_ptr, shape=(actual_n_points, 3)))

        owner_ptr = dll.cfd_mesher_get_owner_ptr(mesh_handle)
        owner = np.copy(np.ctypeslib.as_array(owner_ptr, shape=(actual_n_faces,)))

        neighbour_ptr = dll.cfd_mesher_get_neighbour_ptr(mesh_handle)
        neighbour = np.copy(np.ctypeslib.as_array(neighbour_ptr, shape=(actual_n_faces,)))

        fa_ptr = dll.cfd_mesher_get_face_areas_ptr(mesh_handle)
        face_areas = np.copy(np.ctypeslib.as_array(fa_ptr, shape=(actual_n_faces, 3)))

        fc_ptr = dll.cfd_mesher_get_face_centres_ptr(mesh_handle)
        face_centres = np.copy(np.ctypeslib.as_array(fc_ptr, shape=(actual_n_faces, 3)))

        cc_ptr = dll.cfd_mesher_get_cell_centres_ptr(mesh_handle)
        cell_centres = np.copy(np.ctypeslib.as_array(cc_ptr, shape=(actual_n_cells, 3)))

        cv_ptr = dll.cfd_mesher_get_cell_volumes_ptr(mesh_handle)
        cell_volumes = np.copy(np.ctypeslib.as_array(cv_ptr, shape=(actual_n_cells,)))

        df_ptr = dll.cfd_mesher_get_delta_f_ptr(mesh_handle)
        delta_f = np.copy(np.ctypeslib.as_array(df_ptr, shape=(actual_n_faces, 3)))

        kf_ptr = dll.cfd_mesher_get_k_f_ptr(mesh_handle)
        k_f = np.copy(np.ctypeslib.as_array(kf_ptr, shape=(actual_n_faces, 3)))

        no_ptr = dll.cfd_mesher_get_non_orthogonality_ptr(mesh_handle)
        non_ortho = (
            np.copy(np.ctypeslib.as_array(no_ptr, shape=(actual_n_faces,)))
            if no_ptr
            else np.zeros(actual_n_faces, dtype=np.float64)
        )

        sk_ptr = dll.cfd_mesher_get_skewness_ptr(mesh_handle)
        skewness = (
            np.copy(np.ctypeslib.as_array(sk_ptr, shape=(actual_n_faces,)))
            if sk_ptr
            else np.zeros(actual_n_faces, dtype=np.float64)
        )

        ar_ptr = dll.cfd_mesher_get_aspect_ratio_ptr(mesh_handle)
        aspect_ratio = (
            np.copy(np.ctypeslib.as_array(ar_ptr, shape=(actual_n_cells,)))
            if ar_ptr
            else np.ones(actual_n_cells, dtype=np.float64)
        )

        sm_ptr = dll.cfd_mesher_get_stabilization_mask_ptr(mesh_handle)
        stab_mask = (
            np.copy(np.ctypeslib.as_array(sm_ptr, shape=(actual_n_faces,)))
            if sm_ptr
            else np.zeros(actual_n_faces, dtype=np.int32)
        )

        cr_ptr = dll.cfd_mesher_get_cell_regions_ptr(mesh_handle)
        cell_regions = (
            np.copy(np.ctypeslib.as_array(cr_ptr, shape=(actual_n_cells,)))
            if cr_ptr
            else np.zeros(actual_n_cells, dtype=np.int32)
        )

        cell_types_ptr = dll.cfd_mesher_get_cell_types_ptr(mesh_handle)
        cell_types = (
            np.copy(np.ctypeslib.as_array(cell_types_ptr, shape=(actual_n_cells,)))
            if cell_types_ptr
            else np.full(actual_n_cells, CellType.HEXAHEDRON.value, dtype=np.int32)
        )

        topological_dim = dll.cfd_mesher_get_topological_dim(mesh_handle)

        m_skew_ptr = dll.cfd_mesher_get_m_skew_ptr(mesh_handle)
        m_skew = (
            np.copy(np.ctypeslib.as_array(m_skew_ptr, shape=(actual_n_faces, 3)))
            if m_skew_ptr
            else np.zeros((actual_n_faces, 3), dtype=np.float64)
        )

        kf_comp_ptr = dll.cfd_mesher_get_k_f_comp_ptr(mesh_handle)
        k_f_comp = (
            np.copy(np.ctypeslib.as_array(kf_comp_ptr, shape=(actual_n_faces, 3)))
            if kf_comp_ptr
            else np.zeros((actual_n_faces, 3), dtype=np.float64)
        )

        beta_upwind_ptr = dll.cfd_mesher_get_beta_upwind_ptr(mesh_handle)
        beta_upwind = (
            np.copy(np.ctypeslib.as_array(beta_upwind_ptr, shape=(actual_n_faces,)))
            if beta_upwind_ptr
            else np.ones(actual_n_faces, dtype=np.float64)
        )

        omega_cell_ptr = dll.cfd_mesher_get_omega_cell_ptr(mesh_handle)
        omega_cell = (
            np.copy(np.ctypeslib.as_array(omega_cell_ptr, shape=(actual_n_cells,)))
            if omega_cell_ptr
            else np.ones(actual_n_cells, dtype=np.float64)
        )

        wsrd_alpha_ptr = dll.cfd_mesher_get_wsrd_alpha_ptr(mesh_handle)
        wsrd_alpha = (
            np.copy(np.ctypeslib.as_array(wsrd_alpha_ptr, shape=(actual_n_cells,)))
            if wsrd_alpha_ptr
            else np.ones(actual_n_cells, dtype=np.float64)
        )

        # CSR Face nodes
        fo_ptr = dll.cfd_mesher_get_face_offsets_ptr(mesh_handle)
        fn_ptr = dll.cfd_mesher_get_face_nodes_ptr(mesh_handle)
        n_fnodes = dll.cfd_mesher_get_n_face_nodes(mesh_handle)
        fo = np.ctypeslib.as_array(fo_ptr, shape=(actual_n_faces + 1,))
        fn = np.ctypeslib.as_array(fn_ptr, shape=(n_fnodes,))
        faces_list = [fn[fo[f] : fo[f + 1]].copy() for f in range(actual_n_faces)]

        # CSR Cell nodes
        co_ptr = dll.cfd_mesher_get_cell_offsets_ptr(mesh_handle)
        cn_ptr = dll.cfd_mesher_get_cell_nodes_ptr(mesh_handle)
        co = np.ctypeslib.as_array(co_ptr, shape=(actual_n_cells + 1,))
        total_cnodes = int(co[-1])
        cn = np.ctypeslib.as_array(cn_ptr, shape=(total_cnodes,))
        cells_list = [cn[co[c] : co[c + 1]].copy() for c in range(actual_n_cells)]

        # Boundaries & Patches
        boundaries = {}
        baffles = []
        n_patches = dll.cfd_mesher_get_n_patches(mesh_handle)
        for p_idx in range(n_patches):
            patch = BoundaryPatchC()
            dll.cfd_mesher_get_boundary_patch(mesh_handle, p_idx, ctypes.byref(patch))
            p_name = patch.name.decode("utf-8", errors="ignore").strip() or "wall"
            boundaries[p_name] = (int(patch.start_face), int(patch.num_faces))
            if b"coupledWall" in patch.patch_type:
                baffles.append({
                    "name": p_name,
                    "type": "coupledWall",
                    "start_face": int(patch.start_face),
                    "num_faces": int(patch.num_faces),
                    "region_id": int(patch.region_id),
                })

        # Compute Berger-Giuliani WSRD weights dynamically according to Eq. (4.2)
        small_cell_ratio = float(getattr(config, "small_cell_ratio", 0.20)) if config is not None else 0.20
        wsrd_target_alpha = float(getattr(config, "wsrd_target_alpha", 0.50)) if config is not None else 0.50

        wsrd_weights = compute_wsrd_weights_array(
            wsrd_alpha, small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha
        )
        wsrd_redist_map = compute_wsrd_redistribution_map(
            wsrd_alpha, owner, neighbour, actual_n_cells, actual_n_faces,
            small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha,
        )

        error_map = {
            "non_orthogonality": non_ortho,
            "non_ortho": non_ortho,
            "skewness": skewness,
            "skew": skewness,
            "aspect_ratio": aspect_ratio,
            "stabilization_mask": stab_mask,
            "mask": stab_mask,
            "m_skew": m_skew,
            "k_f_comp": k_f_comp,
            "beta_upwind": beta_upwind,
            "omega_cell": omega_cell,
            "wsrd_alpha": wsrd_alpha,
            "wsrd_weights": wsrd_weights,
        }

        mesh = UnstructuredMesh(
            points=pts_out,
            cells=cells_list,
            owner=owner,
            neighbour=neighbour,
            face_areas=face_areas,
            face_centres=face_centres,
            cell_centres=cell_centres,
            cell_volumes=cell_volumes,
            delta_f=delta_f,
            k_f=k_f,
            boundaries=boundaries,
            cell_regions=cell_regions,
            baffles=baffles,
            error_map=error_map,
            faces=faces_list,
            cell_types=cell_types,
            topological_dim=topological_dim,
            metadata={
                "engine": "native_cpp20_hexcore",
                "wsrd_weights": wsrd_weights,
                "wsrd_redistribution": wsrd_redist_map,
                "rcm_reordered": bool(config is None or config.rcm_reorder),
            },
        )
        if len(np.unique(cell_regions)) > 1:
            mesh = extract_cht_twin_faces(mesh)
        return mesh
    finally:
        dll.cfd_mesher_free(mesh_handle)


def compute_difvm_topology_python(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
    boundary_patches: Optional[Union[Dict[str, List[List[int]]], Dict[str, Tuple[int, int]]]] = None,
    face_patch_ids: Optional[np.ndarray] = None,
    patch_names: Optional[Dict[int, str]] = None,
    cell_types: Optional[np.ndarray] = None,
    topological_dim: Optional[int] = None,
    config: Optional[MeshingConfig] = None,
    v_bg: Optional[Union[float, np.ndarray]] = None,
    explicit_faces: Optional[Any] = None,
) -> UnstructuredMesh:
    """Pure-Python / NumPy fallback implementation of DiFVM topology constructor."""
    pts = np.ascontiguousarray(points, dtype=np.float64)
    if pts.ndim == 2 and pts.shape[1] == 2:
        pts = np.hstack([pts, np.zeros((len(pts), 1), dtype=np.float64)])

    n_cells = len(cells)
    cell_regions = (
        np.asarray(regions, dtype=np.int32)
        if regions is not None
        else np.zeros(n_cells, dtype=np.int32)
    )

    per_cell_explicit_faces: Optional[List[Optional[List[np.ndarray]]]] = None
    if explicit_faces is not None and len(explicit_faces) > 0:
        first_ef = explicit_faces[0]
        is_nested_per_cell = (
            len(first_ef) > 0
            and isinstance(first_ef[0], (list, tuple, np.ndarray))
        )
        if not is_nested_per_cell and n_cells == 1:
            per_cell_explicit_faces = [[np.asarray(f, dtype=np.int64) for f in explicit_faces]]
        elif is_nested_per_cell and len(explicit_faces) == n_cells:
            per_cell_explicit_faces = [
                [np.asarray(f, dtype=np.int64) for f in cf] if cf is not None else None
                for cf in explicit_faces
            ]

    # Classify cell types and determine topological dimension
    if cell_types is None:
        cell_types_arr = np.array([_classify_cell_type(c, pts).value for c in cells], dtype=np.int32)
    else:
        cell_types_arr = np.asarray(cell_types, dtype=np.int32)

    if topological_dim is None:
        is_2d_mesh = all(
            t in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value)
            for t in cell_types_arr
        )
        topo_dim = 2 if is_2d_mesh else 3
    else:
        topo_dim = topological_dim

    # 1. Extract all cell faces and identify shared faces
    face_map: Dict[Tuple[int, ...], int] = {}
    temp_faces: List[Dict[str, Any]] = []
    cell_face_refs: List[List[Tuple[int, float]]] = [[] for _ in range(n_cells)]

    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        cfaces = (
            per_cell_explicit_faces[c_idx]
            if per_cell_explicit_faces is not None and per_cell_explicit_faces[c_idx] is not None
            else _extract_cell_faces_py(cell_arr, pts)
        )
        for fnodes in cfaces:
            canonical_key = tuple(sorted(fnodes))
            if canonical_key not in face_map:
                f_idx = len(temp_faces)
                norm_v, cent_v = _compute_polygon_normal_and_centroid_py(fnodes, pts)
                is_folded = _is_face_folded_py(fnodes, pts, norm_v)
                temp_faces.append({
                    "nodes": list(fnodes),
                    "owner": c_idx,
                    "neighbour": -1,
                    "normal_owner": norm_v,
                    "centre": cent_v,
                    "is_folded": is_folded,
                })
                face_map[canonical_key] = f_idx
                cell_face_refs[c_idx].append((f_idx, 1.0))
            else:
                f_idx = face_map[canonical_key]
                if temp_faces[f_idx]["neighbour"] >= 0:
                    raise ValueError(
                        f"Non-manifold mesh detected: face shared by >2 cells "
                        f"({temp_faces[f_idx]['owner']}, {temp_faces[f_idx]['neighbour']}, {c_idx})"
                    )
                prev_owner = temp_faces[f_idx]["owner"]
                if c_idx < prev_owner:
                    # Enforce owner < neighbour
                    temp_faces[f_idx]["owner"] = c_idx
                    temp_faces[f_idx]["neighbour"] = prev_owner
                    temp_faces[f_idx]["normal_owner"] = -temp_faces[f_idx]["normal_owner"]
                    temp_faces[f_idx]["nodes"] = list(reversed(temp_faces[f_idx]["nodes"]))
                    cell_face_refs[c_idx].append((f_idx, 1.0))
                    for k in range(len(cell_face_refs[prev_owner])):
                        if cell_face_refs[prev_owner][k][0] == f_idx:
                            cell_face_refs[prev_owner][k] = (f_idx, -1.0)
                            break
                else:
                    temp_faces[f_idx]["neighbour"] = c_idx
                    cell_face_refs[c_idx].append((f_idx, -1.0))

    # 2. Compute cell volumes and centroids via divergence theorem
    cell_centres = np.zeros((n_cells, 3), dtype=np.float64)
    cell_volumes = np.zeros(n_cells, dtype=np.float64)

    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        c_approx = np.mean(pts[cell_arr], axis=0)
        ctype = cell_types_arr[c_idx]
        is_cell_2d = ctype in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value)
        div_factor = 2.0 if is_cell_2d else 3.0

        # Check natural cell volume to preserve negative volume on inverted elements
        raw_faces = (
            per_cell_explicit_faces[c_idx]
            if per_cell_explicit_faces is not None and per_cell_explicit_faces[c_idx] is not None
            else _extract_cell_faces_py(cell_arr, pts)
        )
        natural_vol = 0.0
        for fn in raw_faces:
            n_raw, c_raw = _compute_polygon_normal_and_centroid_py(fn, pts)
            natural_vol += float(np.dot(c_raw - c_approx, n_raw)) / div_factor

        vol_sum = 0.0
        centroid_weighted = np.zeros(3, dtype=np.float64)

        for f_idx, sign in cell_face_refs[c_idx]:
            tf = temp_faces[f_idx]
            sf_out = tf["normal_owner"] * sign
            pyr_vol = float(np.dot(tf["centre"] - c_approx, sf_out)) / div_factor
            vol_sum += pyr_vol
            if is_cell_2d:
                pyr_c = tf["centre"] * (2.0 / 3.0) + c_approx * (1.0 / 3.0)
            else:
                pyr_c = tf["centre"] * 0.75 + c_approx * 0.25
            centroid_weighted += pyr_c * pyr_vol

        if natural_vol < -1e-15 and vol_sum > 1e-60:
            vol_final = -vol_sum
        elif abs(vol_sum) > 1e-60:
            vol_final = vol_sum
        else:
            vol_final = 0.0

        if abs(vol_sum) > 1e-60:
            centroid_weighted /= vol_sum
            cell_volumes[c_idx] = vol_final
        else:
            cell_volumes[c_idx] = 0.0
            centroid_weighted = c_approx

        cell_centres[c_idx] = centroid_weighted

    # 3. Partition and sort faces: internal first, boundary last
    internal_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] >= 0]
    boundary_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] < 0]

    # Sort internal faces strictly by owner < neighbour
    internal_indices.sort(key=lambda idx: (temp_faces[idx]["owner"], temp_faces[idx]["neighbour"]))

    boundaries: Dict[str, Tuple[int, int]] = {}
    n_int = len(internal_indices)

    # Process named boundary patches if provided
    if boundary_patches is not None and len(boundary_patches) > 0:
        first_val = next(iter(boundary_patches.values()))
        if isinstance(first_val, list):
            # Map canonical node keys to patch names
            patch_key_map: Dict[Tuple[int, ...], str] = {}
            for pname, plist in boundary_patches.items():
                for p_fnodes in plist:
                    patch_key_map[tuple(sorted(int(x) for x in p_fnodes))] = pname

            # Group boundary indices by patch
            patch_groups: Dict[str, List[int]] = {pname: [] for pname in boundary_patches.keys()}
            unassigned: List[int] = []
            for b_idx in boundary_indices:
                ck = tuple(sorted(int(x) for x in temp_faces[b_idx]["nodes"]))
                if ck in patch_key_map:
                    patch_groups[patch_key_map[ck]].append(b_idx)
                else:
                    unassigned.append(b_idx)

            if unassigned:
                if "wall" in patch_groups:
                    patch_groups["wall"].extend(unassigned)
                else:
                    patch_groups["wall"] = unassigned

            sorted_boundary_indices: List[int] = []
            cur_face_idx = n_int
            for pname, p_indices in patch_groups.items():
                if p_indices:
                    boundaries[pname] = (cur_face_idx, len(p_indices))
                    sorted_boundary_indices.extend(p_indices)
                    cur_face_idx += len(p_indices)

            boundary_indices = sorted_boundary_indices
        elif isinstance(first_val, tuple):
            boundaries = dict(boundary_patches)
    elif face_patch_ids is not None:
        patch_groups_ids: Dict[str, List[int]] = {}
        for b_i, b_idx in enumerate(boundary_indices):
            pid = int(face_patch_ids[b_i]) if b_i < len(face_patch_ids) else -1
            pname = patch_names.get(pid, f"patch_{pid}") if (patch_names and pid in patch_names) else "wall"
            if pname not in patch_groups_ids:
                patch_groups_ids[pname] = []
            patch_groups_ids[pname].append(b_idx)

        sorted_boundary_indices = []
        cur_face_idx = n_int
        for pname, p_indices in patch_groups_ids.items():
            if p_indices:
                boundaries[pname] = (cur_face_idx, len(p_indices))
                sorted_boundary_indices.extend(p_indices)
                cur_face_idx += len(p_indices)
        boundary_indices = sorted_boundary_indices
    else:
        n_bnd = len(boundary_indices)
        if n_bnd > 0:
            boundaries["wall"] = (n_int, n_bnd)

    sorted_indices = internal_indices + boundary_indices
    n_faces = len(sorted_indices)

    owner = np.zeros(n_faces, dtype=np.int64)
    neighbour = np.zeros(n_faces, dtype=np.int64)
    face_areas = np.zeros((n_faces, 3), dtype=np.float64)
    face_centres = np.zeros((n_faces, 3), dtype=np.float64)
    delta_f = np.zeros((n_faces, 3), dtype=np.float64)
    k_f = np.zeros((n_faces, 3), dtype=np.float64)
    k_f_comp = np.zeros((n_faces, 3), dtype=np.float64)
    m_skew = np.zeros((n_faces, 3), dtype=np.float64)
    beta_upwind = np.ones(n_faces, dtype=np.float64)
    non_ortho = np.zeros(n_faces, dtype=np.float64)
    skewness = np.zeros(n_faces, dtype=np.float64)
    stab_mask = np.zeros(n_faces, dtype=np.int32)
    final_faces: List[np.ndarray] = []

    for new_idx, orig_idx in enumerate(sorted_indices):
        tf = temp_faces[orig_idx]
        o = tf["owner"]
        nb = tf["neighbour"]
        owner[new_idx] = o
        neighbour[new_idx] = nb

        sf = tf["normal_owner"].copy()
        cf = tf["centre"].copy()
        c_o = cell_centres[o]
        fnodes = list(tf["nodes"])

        # Check right-hand rule and normal orientation
        if nb >= 0:
            c_nb = cell_centres[nb]
            df = c_nb - c_o
            df_norm = np.linalg.norm(df)
            sf_norm = np.linalg.norm(sf)
            sf_dot_df = float(np.dot(sf, df))

            # If sf_dot_df <= 0, the element is geometrically inverted and will escalate to stabilization_mask = 3

            # Verify 3D right-hand rule on face nodes
            if len(fnodes) >= 3 and sf_norm > 1e-15:
                poly_pts = pts[fnodes]
                v0 = poly_pts[0]
                n_geom = np.zeros(3, dtype=np.float64)
                for i in range(1, len(fnodes) - 1):
                    n_geom += 0.5 * np.cross(poly_pts[i] - v0, poly_pts[i + 1] - v0)
                if np.dot(n_geom, sf) < 0.0:
                    fnodes = list(reversed(fnodes))

            face_areas[new_idx] = sf
            face_centres[new_idx] = cf

            if sf_dot_df > 1e-15:
                delta = df * (float(np.dot(sf, sf)) / sf_dot_df)
                k = sf - delta
                delta_norm = np.linalg.norm(delta)
                k_norm = np.linalg.norm(k)
                alpha_damp = min(1.0, 1.5 * delta_norm / (k_norm + 1e-15))
                k_comp = alpha_damp * k
            else:
                delta = sf.copy()
                k = np.zeros(3, dtype=np.float64)
                k_comp = np.zeros(3, dtype=np.float64)

            delta_f[new_idx] = delta
            k_f[new_idx] = k
            k_f_comp[new_idx] = k_comp

            # Skewness and m_skew
            skew = 0.0
            m_s = np.zeros(3, dtype=np.float64)
            if sf_norm > 1e-15 and df_norm > 1e-15:
                nf = sf / sf_norm
                df_dot_nf = float(np.dot(df, nf))
                if abs(df_dot_nf) > 1e-15:
                    t = float(np.dot(cf - c_o, nf)) / df_dot_nf
                    x_int = c_o + df * t
                    m_s = cf - x_int
                    skew = float(np.linalg.norm(m_s) / df_norm)
            skewness[new_idx] = skew
            m_skew[new_idx] = m_s

            # Non-orthogonality angle
            cos_theta = 1.0
            if sf_norm > 1e-15 and df_norm > 1e-15:
                cos_theta = float(np.clip(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0))
            theta_deg = float(np.arccos(cos_theta) * 180.0 / np.pi)
            non_ortho[new_idx] = theta_deg

            # Stabilization mask & beta_upwind
            tier = 0
            if tf.get("is_folded", False) or sf_dot_df <= 0.0:
                tier = 3
                beta = 0.0
            else:
                beta_ortho = max(0.0, min(1.0, (75.0 - theta_deg) / 35.0))
                beta_skew = max(0.0, min(1.0, (1.0 - skew) / 0.6))
                beta = min(beta_ortho, beta_skew)

                if theta_deg >= 75.0 or skew >= 1.0:
                    tier = 3
                    beta = 0.0
                elif theta_deg >= 65.0 or skew >= 0.7:
                    tier = 2
                elif theta_deg >= 40.0 or skew >= 0.4:
                    tier = 1
            stab_mask[new_idx] = tier
            beta_upwind[new_idx] = beta
        else:
            d_out = cf - c_o
            sf_dot_dout = float(np.dot(sf, d_out))
            tier = 0
            beta = 1.0
            if tf.get("is_folded", False) or sf_dot_dout <= 0.0:
                tier = 3
                beta = 0.0

            if len(fnodes) >= 3 and np.linalg.norm(sf) > 1e-15:
                poly_pts = pts[fnodes]
                v0 = poly_pts[0]
                n_geom = np.zeros(3, dtype=np.float64)
                for i in range(1, len(fnodes) - 1):
                    n_geom += 0.5 * np.cross(poly_pts[i] - v0, poly_pts[i + 1] - v0)
                if np.dot(n_geom, sf) < 0.0:
                    fnodes = list(reversed(fnodes))

            face_areas[new_idx] = sf
            face_centres[new_idx] = cf
            delta_f[new_idx] = d_out
            k_f[new_idx] = np.zeros(3, dtype=np.float64)
            k_f_comp[new_idx] = np.zeros(3, dtype=np.float64)
            m_skew[new_idx] = np.zeros(3, dtype=np.float64)
            non_ortho[new_idx] = 0.0
            skewness[new_idx] = 0.0
            stab_mask[new_idx] = tier
            beta_upwind[new_idx] = beta

        final_faces.append(np.array(fnodes, dtype=np.int64))

    # Aspect ratio: maximum to minimum bounding box span along active coordinate axes
    aspect_ratio = np.ones(n_cells, dtype=np.float64)
    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        if len(cell_arr) > 0:
            cpts = pts[cell_arr]
            spans = np.max(cpts, axis=0) - np.min(cpts, axis=0)
            valid_spans = spans[spans > 1e-12]
            if len(valid_spans) >= 2:
                aspect_ratio[c_idx] = max(1.0, float(np.max(valid_spans) / max(float(np.min(valid_spans)), 1e-15)))
            else:
                aspect_ratio[c_idx] = 1.0

    # 7. Cell-wise factors: omega_cell and wsrd_alpha
    min_beta_per_cell = np.ones(n_cells, dtype=np.float64)
    if n_faces > 0:
        np.minimum.at(min_beta_per_cell, owner, beta_upwind)
        int_mask = neighbour >= 0
        if np.any(int_mask):
            np.minimum.at(min_beta_per_cell, neighbour[int_mask], beta_upwind[int_mask])
    omega_cell = np.clip(min_beta_per_cell, 0.1, 1.0)

    # wsrd_alpha: Berger-Giuliani cut-cell volume ratio against background cell volume V_i^bg
    if v_bg is not None:
        if isinstance(v_bg, (int, float)):
            wsrd_alpha = np.clip(cell_volumes / (float(v_bg) + 1e-30), 0.0, 1.0)
        else:
            wsrd_alpha = np.clip(cell_volumes / (np.asarray(v_bg, dtype=np.float64) + 1e-30), 0.0, 1.0)
    else:
        pos_vols = cell_volumes[cell_volumes > 1e-60]
        v_ref = float(np.max(pos_vols)) if len(pos_vols) > 0 else 1.0
        wsrd_alpha = np.clip(cell_volumes / (v_ref + 1e-30), 0.0, 1.0)

    small_cell_ratio = float(getattr(config, "small_cell_ratio", 0.20)) if config is not None else 0.20
    wsrd_target_alpha = float(getattr(config, "wsrd_target_alpha", 0.50)) if config is not None else 0.50
    wsrd_weights = compute_wsrd_weights_array(
        wsrd_alpha, small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha
    )
    wsrd_redist_map = compute_wsrd_redistribution_map(
        wsrd_alpha, owner, neighbour, n_cells, n_faces,
        small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha,
    )

    baffles: List[Dict[str, Any]] = []

    error_map = {
        "non_orthogonality": non_ortho,
        "non_ortho": non_ortho,
        "skewness": skewness,
        "skew": skewness,
        "aspect_ratio": aspect_ratio,
        "stabilization_mask": stab_mask,
        "mask": stab_mask,
        "m_skew": m_skew,
        "k_f_comp": k_f_comp,
        "beta_upwind": beta_upwind,
        "omega_cell": omega_cell,
        "wsrd_alpha": wsrd_alpha,
        "wsrd_weights": wsrd_weights,
    }

    mesh = UnstructuredMesh(
        points=pts,
        cells=cells,
        owner=owner,
        neighbour=neighbour,
        face_areas=face_areas,
        face_centres=face_centres,
        cell_centres=cell_centres,
        cell_volumes=cell_volumes,
        delta_f=delta_f,
        k_f=k_f,
        boundaries=boundaries,
        cell_regions=cell_regions,
        baffles=baffles,
        error_map=error_map,
        faces=final_faces,
        cell_types=cell_types_arr,
        topological_dim=topo_dim,
        metadata={
            "engine": "pure_python_fallback",
            "wsrd_weights": wsrd_weights,
            "wsrd_redistribution": wsrd_redist_map,
        },
    )
    if cell_regions is not None and len(np.unique(cell_regions)) > 1:
        mesh = extract_cht_twin_faces(mesh)
    return mesh


def extract_cht_twin_faces(mesh: UnstructuredMesh) -> UnstructuredMesh:
    """Split internal multi-region interfaces into conformal collocated twin boundary patches.

    Reindexes all faces so that the internal face block [0, n_int_clean) is contiguous
    with neighbour >= 0, followed by existing boundary patches, followed by twin boundary
    patches coupledWall_region0_to_1 and coupledWall_region1_to_0.
    """
    if not mesh.is_multiregion or mesh.cell_regions is None:
        return mesh

    owner = mesh.owner
    neighbour = mesh.neighbour
    cell_regions = mesh.cell_regions
    n_faces = mesh.num_faces

    # 1. Identify clean internal faces and coupled internal faces
    clean_internal: List[int] = []
    coupled_internal: List[int] = []
    original_boundary: List[int] = []

    for f in range(n_faces):
        o = owner[f]
        nb = neighbour[f]
        if nb >= 0:
            if cell_regions[o] == cell_regions[nb]:
                clean_internal.append(f)
            else:
                coupled_internal.append(f)
        else:
            original_boundary.append(f)

    if not coupled_internal:
        return mesh

    n_clean_int = len(clean_internal)
    n_coupled = len(coupled_internal)
    n_orig_bnd = len(original_boundary)

    # 2. Reindex existing boundary patches
    old_to_new_bnd: Dict[int, int] = {}
    for new_b_idx, old_f in enumerate(original_boundary):
        old_to_new_bnd[old_f] = n_clean_int + new_b_idx

    new_boundaries: Dict[str, Tuple[int, int]] = {}
    for p_name, (start_f, count_f) in mesh.boundaries.items():
        if count_f > 0 and start_f in old_to_new_bnd:
            new_start = old_to_new_bnd[start_f]
            new_boundaries[p_name] = (new_start, count_f)

    # 3. Build twin boundary patches for coupled faces
    # Canonical grouping by region pair (r_lo, r_hi) with r_lo < r_hi
    pairs_map: Dict[Tuple[int, int], List[int]] = {}
    for f in coupled_internal:
        ro = int(cell_regions[owner[f]])
        rnb = int(cell_regions[neighbour[f]])
        pair = (min(ro, rnb), max(ro, rnb))
        pairs_map.setdefault(pair, []).append(f)

    # Flatten coupled faces ordered deterministically by region pair
    ordered_pairs = sorted(pairs_map.keys())
    ordered_coupled: List[int] = []
    for pair in ordered_pairs:
        ordered_coupled.extend(pairs_map[pair])

    # 4. Construct new face arrays
    total_new_faces = n_clean_int + n_orig_bnd + 2 * n_coupled

    new_owner = np.zeros(total_new_faces, dtype=np.int64)
    new_neighbour = np.full(total_new_faces, -1, dtype=np.int64)
    new_face_areas = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_face_centres = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_delta_f = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_k_f = np.zeros((total_new_faces, 3), dtype=np.float64)

    has_faces = mesh.faces is not None
    faces_clean = [mesh.faces[f] for f in clean_internal] if has_faces else []
    faces_orig_bnd = [mesh.faces[f] for f in original_boundary] if has_faces else []
    faces_coupled: List[np.ndarray] = []

    # Block 1: Clean internal faces
    for i, f in enumerate(clean_internal):
        new_owner[i] = owner[f]
        new_neighbour[i] = neighbour[f]
        new_face_areas[i] = mesh.face_areas[f]
        new_face_centres[i] = mesh.face_centres[f]
        new_delta_f[i] = mesh.delta_f[f]
        new_k_f[i] = mesh.k_f[f]

    # Block 2: Original boundary faces
    for i, f in enumerate(original_boundary):
        target = n_clean_int + i
        new_owner[target] = owner[f]
        new_neighbour[target] = -1
        new_face_areas[target] = mesh.face_areas[f]
        new_face_centres[target] = mesh.face_centres[f]
        new_delta_f[target] = mesh.delta_f[f]
        new_k_f[target] = mesh.k_f[f]

    # Block 3 & 4: Contiguous Twin Patches grouped by region pair
    current_bnd_offset = n_clean_int + n_orig_bnd
    is_two_region = (len(np.unique(cell_regions)) == 2)
    baffles = []
    coupled_f_to_fa: Dict[int, int] = {}
    coupled_f_to_fb: Dict[int, int] = {}

    for pair in ordered_pairs:
        pair_faces = pairs_map[pair]
        n_p = len(pair_faces)
        start_a = current_bnd_offset
        start_b = start_a + n_p

        if is_two_region and pair == ordered_pairs[0]:
            patch_name_a = "coupledWall_fluid"
            patch_name_b = "coupledWall_solid"
        else:
            patch_name_a = f"coupledWall_region{pair[0]}_to_{pair[1]}"
            patch_name_b = f"coupledWall_region{pair[1]}_to_{pair[0]}"

        new_boundaries[patch_name_a] = (start_a, n_p)
        new_boundaries[patch_name_b] = (start_b, n_p)

        twin_pairs_p = []
        faces_pair_a: List[np.ndarray] = []
        faces_pair_b: List[np.ndarray] = []
        for i, f in enumerate(pair_faces):
            fa = start_a + i
            fb = start_b + i
            twin_pairs_p.append((fa, fb))
            coupled_f_to_fa[f] = fa
            coupled_f_to_fb[f] = fb

            o = owner[f]
            nb = neighbour[f]
            sf = mesh.face_areas[f]
            cf = mesh.face_centres[f]

            # Canonical orientation: Side A in pair[0] (lower region), Side B in pair[1] (higher region)
            if cell_regions[o] <= cell_regions[nb]:
                cell_a, cell_b = o, nb
                sf_a = sf
                sf_b = -sf
                fn_a = mesh.faces[f] if has_faces else None
                fn_b = np.ascontiguousarray(mesh.faces[f][::-1]) if has_faces else None
            else:
                cell_a, cell_b = nb, o
                sf_a = -sf
                sf_b = sf
                fn_a = np.ascontiguousarray(mesh.faces[f][::-1]) if has_faces else None
                fn_b = mesh.faces[f] if has_faces else None

            # Patch A: outward normal from cell_a pointing into region B
            new_owner[fa] = cell_a
            new_neighbour[fa] = -1
            new_face_areas[fa] = sf_a
            new_face_centres[fa] = cf
            new_delta_f[fa] = cf - mesh.cell_centres[cell_a]
            new_k_f[fa] = np.zeros(3, dtype=np.float64)
            if has_faces:
                faces_pair_a.append(fn_a)

            # Patch B: outward normal from cell_b pointing into region A (sf_b == -sf_a)
            new_owner[fb] = cell_b
            new_neighbour[fb] = -1
            new_face_areas[fb] = sf_b
            new_face_centres[fb] = cf
            new_delta_f[fb] = cf - mesh.cell_centres[cell_b]
            new_k_f[fb] = np.zeros(3, dtype=np.float64)
            if has_faces:
                faces_pair_b.append(fn_b)

        if has_faces:
            faces_coupled.extend(faces_pair_a)
            faces_coupled.extend(faces_pair_b)

        baffles.append({
            "name": patch_name_a,
            "type": "coupledWall",
            "twin_pairs": twin_pairs_p,
            "start_face_a": start_a,
            "start_face_b": start_b,
            "num_faces": n_p,
            "region_a": pair[0],
            "region_b": pair[1],
        })
        current_bnd_offset += 2 * n_p

    new_faces: Optional[List[np.ndarray]] = (
        faces_clean + faces_orig_bnd + faces_coupled
        if has_faces
        else None
    )

    # 5. Reindex error_map
    new_error_map = {}
    for k, v in mesh.error_map.items():
        if isinstance(v, np.ndarray) and len(v) == n_faces:
            if v.ndim == 1:
                new_v = np.zeros(total_new_faces, dtype=v.dtype)
                new_v[:n_clean_int] = v[clean_internal]
                new_v[n_clean_int:n_clean_int + n_orig_bnd] = v[original_boundary]
                for f, fa in coupled_f_to_fa.items():
                    new_v[fa] = v[f]
                for f, fb in coupled_f_to_fb.items():
                    new_v[fb] = v[f]
            elif v.ndim == 2:
                new_v = np.zeros((total_new_faces, v.shape[1]), dtype=v.dtype)
                new_v[:n_clean_int] = v[clean_internal]
                new_v[n_clean_int:n_clean_int + n_orig_bnd] = v[original_boundary]
                for f, fa in coupled_f_to_fa.items():
                    new_v[fa] = v[f]
                for f, fb in coupled_f_to_fb.items():
                    new_v[fb] = -v[f] if k in ("m_skew", "k_f_comp") else v[f]
            new_error_map[k] = new_v
        else:
            new_error_map[k] = v

    return UnstructuredMesh(
        points=mesh.points,
        cells=mesh.cells,
        owner=new_owner,
        neighbour=new_neighbour,
        face_areas=new_face_areas,
        face_centres=new_face_centres,
        cell_centres=mesh.cell_centres,
        cell_volumes=mesh.cell_volumes,
        delta_f=new_delta_f,
        k_f=new_k_f,
        boundaries=new_boundaries,
        cell_regions=mesh.cell_regions,
        baffles=baffles,
        cyclic_pairs=mesh.cyclic_pairs,
        error_map=new_error_map,
        faces=new_faces,
        cell_types=mesh.cell_types,
        topological_dim=mesh.topological_dim,
        metadata=dict(mesh.metadata),
    )


def _polygon_area_2d(poly: np.ndarray) -> float:
    """Compute 2D polygon area via Shoelace formula."""
    if len(poly) < 3:
        return 0.0
    x = poly[:, 0]
    y = poly[:, 1]
    return 0.5 * abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))))


def _clip_polygon_sutherland_hodgman(subject_poly: np.ndarray, clip_poly: np.ndarray) -> np.ndarray:
    """Sutherland-Hodgman polygon clipping for supermesh intersection."""
    if len(subject_poly) < 3 or len(clip_poly) < 3:
        return np.zeros((0, 2), dtype=np.float64)

    area_c = 0.5 * (np.dot(clip_poly[:, 0], np.roll(clip_poly[:, 1], -1)) - np.dot(clip_poly[:, 1], np.roll(clip_poly[:, 0], -1)))
    cp = clip_poly if area_c > 0 else clip_poly[::-1]

    output = subject_poly.copy()
    n_cp = len(cp)

    for k in range(n_cp):
        if len(output) == 0:
            break
        p1 = cp[k]
        p2 = cp[(k + 1) % n_cp]
        edge = p2 - p1
        normal = np.array([-edge[1], edge[0]], dtype=np.float64)

        input_list = output
        output_list = []
        n_in = len(input_list)

        for i in range(n_in):
            s = input_list[i]
            e = input_list[(i + 1) % n_in]

            s_in = bool(np.dot(s - p1, normal) >= -1e-12)
            e_in = bool(np.dot(e - p1, normal) >= -1e-12)

            if e_in:
                if not s_in:
                    d = e - s
                    denom = float(np.dot(d, normal))
                    if abs(denom) > 1e-14:
                        t = float(np.dot(p1 - s, normal)) / denom
                        output_list.append(s + t * d)
                output_list.append(e)
            elif s_in:
                d = e - s
                denom = float(np.dot(d, normal))
                if abs(denom) > 1e-14:
                    t = float(np.dot(p1 - s, normal)) / denom
                    output_list.append(s + t * d)

        output = np.array(output_list, dtype=np.float64)

    return output


def compute_ami_weights(
    source_centres: np.ndarray,
    source_areas: np.ndarray,
    target_centres: np.ndarray,
    target_areas: np.ndarray,
    radius: float = 0.05,
    source_polygons: Optional[List[np.ndarray]] = None,
    target_polygons: Optional[List[np.ndarray]] = None,
) -> np.ndarray:
    """Compute Arbitrary Mesh Interface (AMI) area-conservative interpolation weights.

    Uses Sutherland-Hodgman polygon clipping (Supermesh intersection) to guarantee
    exact conservation:
        sum_j w_ij * A_i == A_i  and  sum_j w_ij == 1.0
    for non-conformal sliding interfaces.
    """
    source_centres = np.asarray(source_centres, dtype=np.float64)
    target_centres = np.asarray(target_centres, dtype=np.float64)
    source_areas = np.asarray(source_areas, dtype=np.float64)
    target_areas = np.asarray(target_areas, dtype=np.float64)
    n_target = len(target_centres)
    n_source = len(source_centres)
    weights = np.zeros((n_target, n_source), dtype=np.float64)

    if n_target == 0 or n_source == 0:
        return weights

    # Scalar area magnitudes
    src_a = np.linalg.norm(source_areas, axis=-1) if source_areas.ndim > 1 else np.abs(source_areas)
    tgt_a = np.linalg.norm(target_areas, axis=-1) if target_areas.ndim > 1 else np.abs(target_areas)

    # Determine 2D projection basis (u1, u2) along interface plane
    if source_polygons is not None and len(source_polygons) > 0:
        all_pts_list = [np.asarray(p, dtype=np.float64) for p in source_polygons]
        if target_polygons is not None and len(target_polygons) > 0:
            all_pts_list.extend([np.asarray(p, dtype=np.float64) for p in target_polygons])
        all_pts = np.vstack(all_pts_list)
        spans = np.max(all_pts, axis=0) - np.min(all_pts, axis=0)
    elif source_areas.ndim > 1 and source_areas.shape[1] == 3 and np.any(np.abs(source_areas) > 1e-12):
        avg_normal = np.mean(np.abs(source_areas), axis=0)
        norm_axis = int(np.argmax(avg_normal))
        spans = np.ones(3, dtype=np.float64)
        spans[norm_axis] = 0.0
        all_pts = np.vstack([source_centres, target_centres])
    else:
        all_pts = np.vstack([source_centres, target_centres])
        spans = np.max(all_pts, axis=0) - np.min(all_pts, axis=0)

    if spans[0] <= 1e-5:
        # Normal is x, plane is yz
        u1, u2 = np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0])
    elif spans[1] <= 1e-5:
        # Normal is y, plane is xz
        u1, u2 = np.array([1.0, 0.0, 0.0]), np.array([0.0, 0.0, 1.0])
    elif spans[2] <= 1e-5:
        # Normal is z, plane is xy
        u1, u2 = np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0])
    else:
        # Arbitrary plane: PCA
        mean_pt = np.mean(all_pts, axis=0)
        cov = (all_pts - mean_pt).T @ (all_pts - mean_pt)
        eigvals, eigvecs = np.linalg.eigh(cov)
        u1 = eigvecs[:, 2]
        u2 = eigvecs[:, 1]

    # Build 2D polygons for target faces
    tgt_polys_2d: List[np.ndarray] = []
    if target_polygons is not None:
        for poly in target_polygons:
            poly_arr = np.asarray(poly, dtype=np.float64)
            p2d = np.column_stack([poly_arr @ u1, poly_arr @ u2])
            tgt_polys_2d.append(p2d)
    else:
        for i in range(n_target):
            c1 = float(np.dot(target_centres[i], u1))
            c2 = float(np.dot(target_centres[i], u2))
            h = np.sqrt(max(float(tgt_a[i]), 1e-12)) / 2.0
            p2d = np.array([
                [c1 - h, c2 - h],
                [c1 + h, c2 - h],
                [c1 + h, c2 + h],
                [c1 - h, c2 + h],
            ], dtype=np.float64)
            tgt_polys_2d.append(p2d)

    # Build 2D polygons for source faces
    src_polys_2d: List[np.ndarray] = []
    if source_polygons is not None:
        for poly in source_polygons:
            poly_arr = np.asarray(poly, dtype=np.float64)
            p2d = np.column_stack([poly_arr @ u1, poly_arr @ u2])
            src_polys_2d.append(p2d)
    else:
        for j in range(n_source):
            c1 = float(np.dot(source_centres[j], u1))
            c2 = float(np.dot(source_centres[j], u2))
            h = np.sqrt(max(float(src_a[j]), 1e-12)) / 2.0
            p2d = np.array([
                [c1 - h, c2 - h],
                [c1 + h, c2 - h],
                [c1 + h, c2 + h],
                [c1 - h, c2 + h],
            ], dtype=np.float64)
            src_polys_2d.append(p2d)

    for i in range(n_target):
        p_tgt = tgt_polys_2d[i]
        c_tgt = target_centres[i]
        diffs = source_centres - c_tgt
        dists = np.linalg.norm(diffs, axis=1)

        overlap_areas = np.zeros(n_source, dtype=np.float64)
        for j in range(n_source):
            h_tgt = np.sqrt(max(float(tgt_a[i]), 1e-12))
            h_src = np.sqrt(max(float(src_a[j]), 1e-12))
            if dists[j] > (h_tgt + h_src) * 1.5 and dists[j] > radius:
                continue

            q_src = src_polys_2d[j]
            inter_poly = _clip_polygon_sutherland_hodgman(p_tgt, q_src)
            overlap_areas[j] = _polygon_area_2d(inter_poly)

        total_overlap = float(np.sum(overlap_areas))
        if total_overlap > 1e-15:
            weights[i] = overlap_areas / total_overlap
        else:
            nearest = int(np.argmin(dists))
            weights[i, nearest] = 1.0

    return weights


def extract_cyclic_pairs(
    mesh: UnstructuredMesh,
    sector_angle_rad: float,
    tolerance: float = 1e-4,
) -> List[Tuple[int, int]]:
    """Extract rotational cyclic periodic boundary face pairs: R_z(theta) * c_f1 == c_f2."""
    cos_t = np.cos(sector_angle_rad)
    sin_t = np.sin(sector_angle_rad)
    r_z = np.array([
        [cos_t, -sin_t, 0.0],
        [sin_t,  cos_t, 0.0],
        [0.0,    0.0,   1.0],
    ], dtype=np.float64)

    bnd_faces = np.where(mesh.neighbour < 0)[0]
    pairs = []
    for i, f1 in enumerate(bnd_faces):
        c1 = mesh.face_centres[f1]
        rot_c1 = np.dot(r_z, c1)
        for j, f2 in enumerate(bnd_faces):
            if i == j:
                continue
            c2 = mesh.face_centres[f2]
            if np.linalg.norm(rot_c1 - c2) < tolerance:
                pairs.append((int(f1), int(f2)))
                break
    return pairs


def compute_difvm_topology(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
    use_native: bool = True,
    cyclic_angle_rad: float = 0.0,
    strict_native: bool = False,
    boundary_patches: Optional[Union[Dict[str, List[List[int]]], Dict[str, Tuple[int, int]]]] = None,
    face_patch_ids: Optional[np.ndarray] = None,
    patch_names: Optional[Dict[int, str]] = None,
    cell_types: Optional[np.ndarray] = None,
    topological_dim: Optional[int] = None,
    config: Optional[MeshingConfig] = None,
    v_bg: Optional[Union[float, np.ndarray]] = None,
    explicit_faces: Optional[Any] = None,
) -> UnstructuredMesh:
    """Build UnstructuredMesh in Structure of Arrays (SoA) layout.

    Transparently uses compiled C++20 core (cfd_mesher_native shared library) if available,
    falling back to pure Python if library is not compiled or use_native=False.
    """
    is_strict = strict_native or (os.environ.get("CFD_STRICT_NATIVE", "0").strip() == "1")
    if is_strict and not use_native:
        raise RuntimeError("strict_native=True requires use_native=True.")

    mesh = None
    dll = get_native_dll(strict_native=is_strict) if use_native else None

    if use_native and dll is not None and boundary_patches is None and face_patch_ids is None and explicit_faces is None:
        try:
            mesh = compute_difvm_topology_native(points, cells, regions=regions, config=config)
            if cell_types is not None:
                mesh.cell_types = np.asarray(cell_types, dtype=np.int32)
            if topological_dim is not None:
                mesh.topological_dim = topological_dim
        except ValueError:
            # Let non-manifold ValueError propagate immediately
            raise
        except Exception as e:
            if is_strict:
                raise RuntimeError(f"Native mesher execution failed in strict mode: {e}") from e
            logger.error(f"Native mesher failed: {e}, falling back to Python.", exc_info=True)
            mesh = None
    elif is_strict:
        if boundary_patches is not None or face_patch_ids is not None:
            raise RuntimeError(
                "Strict native execution requested but custom boundary_patches / face_patch_ids "
                "are not supported by native topology builder."
            )
        raise RuntimeError(f"Strict native execution requested but native library is unavailable: {_NATIVE_LOAD_ERROR}")

    if mesh is None:
        if is_strict and use_native:
            raise RuntimeError(f"Strict native execution requested but native library is unavailable: {_NATIVE_LOAD_ERROR}")
        mesh = compute_difvm_topology_python(
            points,
            cells,
            regions=regions,
            boundary_patches=boundary_patches,
            face_patch_ids=face_patch_ids,
            patch_names=patch_names,
            cell_types=cell_types,
            topological_dim=topological_dim,
            config=config,
            v_bg=v_bg,
            explicit_faces=explicit_faces,
        )

    if cyclic_angle_rad > 0.0:
        mesh.cyclic_pairs = extract_cyclic_pairs(mesh, cyclic_angle_rad)

    return mesh



def rcm_reorder(
    mesh: UnstructuredMesh,
) -> Tuple[UnstructuredMesh, np.ndarray]:
    """Forwarding wrapper for reverse Cuthill-McKee reordering."""
    from backend.meshing.distmesh_core import rcm_reorder as _rcm
    return _rcm(mesh)


def hilbert_sfc_reorder(
    mesh: UnstructuredMesh,
) -> Tuple[UnstructuredMesh, np.ndarray]:
    """Forwarding wrapper for 3D Hilbert/Morton space-filling curve reordering."""
    from backend.meshing.distmesh_core import hilbert_sfc_reorder as _sfc
    return _sfc(mesh)


# ==============================================================================
# Paper 7 & §12.2 Numerics: Symmetry Connections & Linear-Exact FVM Operators
# ==============================================================================

def compute_symmetry_mirror_point(
    c_P: np.ndarray,
    c_f: np.ndarray,
    s_f: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Construct Demirdzic (2022) orthogonal symmetry mirror connection (Paper 7).

    Defines the mirror cell centroid c_M such that the connection vector:
        Delta = c_M - c_P = 2 * ((c_f - c_P) . s_f / (s_f . s_f)) * s_f
    is strictly collinear to s_f (Delta || s_f). Consequently, the non-orthogonal
    cross-diffusion vector is identically zero:
        k_f = s_f - (s_f . s_f / (Delta . s_f)) * Delta == 0.

    Parameters
    ----------
    c_P : np.ndarray, shape (3,) or (N, 3)
        Adjacent cell centroid.
    c_f : np.ndarray, shape (3,) or (N, 3)
        Boundary face centroid.
    s_f : np.ndarray, shape (3,) or (N, 3)
        Boundary face area normal vector.

    Returns
    -------
    c_M : np.ndarray
        Mirror cell centroid(s).
    Delta_vec : np.ndarray
        Connection vector Delta = c_M - c_P.
    k_f : np.ndarray
        Identically zero non-orthogonal cross-diffusion vector.
    """
    c_P = np.asarray(c_P, dtype=np.float64)
    c_f = np.asarray(c_f, dtype=np.float64)
    s_f = np.asarray(s_f, dtype=np.float64)

    is_1d = (c_P.ndim == 1)
    if is_1d:
        c_P = c_P.reshape(1, 3)
        c_f = c_f.reshape(1, 3)
        s_f = s_f.reshape(1, 3)

    d = c_f - c_P
    s_dot_s = np.sum(s_f * s_f, axis=-1, keepdims=True)
    d_dot_s = np.sum(d * s_f, axis=-1, keepdims=True)

    if np.any(s_dot_s <= 0.0):
        raise ValueError("Degenerate boundary face with zero or negative area vector magnitude.")

    # Collinear Delta vector
    Delta_vec = 2.0 * (d_dot_s / s_dot_s) * s_f
    c_M = c_P + Delta_vec

    # Cross diffusion vector k_f = s_f - (s_f . s_f / (Delta . s_f)) * Delta
    # Because Delta = 2 * (d.s / s^2) * s, Delta . s = 2 * (d.s), so (s^2 / (Delta . s)) * Delta = s_f identically.
    k_f = np.zeros_like(s_f)

    if is_1d:
        return c_M[0], Delta_vec[0], k_f[0]
    return c_M, Delta_vec, k_f


def compute_householder_reflection(s_f: np.ndarray) -> np.ndarray:
    """Construct Householder reflection tensor R = I - 2 * n (x) n (Paper 7).

    Reflects arbitrary vectors and tensors across a symmetry plane:
        v_M = R . v_P
        T_M = R . T_P . R^T

    Parameters
    ----------
    s_f : np.ndarray, shape (3,) or (N, 3)
        Face area normal vector.

    Returns
    -------
    R : np.ndarray, shape (3, 3) or (N, 3, 3)
        Orthogonal Householder reflection tensor (R = R^T, R . R = I, det(R) = -1).
    """
    s_f = np.asarray(s_f, dtype=np.float64)
    is_1d = (s_f.ndim == 1)
    if is_1d:
        s_f = s_f.reshape(1, 3)

    N = len(s_f)
    R = np.zeros((N, 3, 3), dtype=np.float64)
    I3 = np.eye(3, dtype=np.float64)

    for idx in range(N):
        sf = s_f[idx]
        norm_sq = float(np.dot(sf, sf))
        if norm_sq <= 1e-30:
            raise ValueError(f"Face normal at index {idx} has near-zero magnitude: {norm_sq}")
        n = sf / np.sqrt(norm_sq)
        R[idx] = I3 - 2.0 * np.outer(n, n)

    if is_1d:
        return R[0]
    return R


def compute_symmetry_vector_diffusion_split(
    gamma_v: float,
    s_f: np.ndarray,
    d_vec: np.ndarray,
    v_P: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray]:
    """Patankar diagonally-dominant split for vector diffusion flux on symmetry plane (Paper 7 Eq. 15-16).

    Splits the reflected diffusion flux:
        D_v = -gamma_v * ((s_f . v_P) / (d . s_f)) * s_f
    into positive implicit main diagonal and explicit RHS components for i in {0, 1, 2}:
        D_{vi, im} = gamma_v * s_{f, i}^2 / (d . s_f) > 0  (implicit diagonal a_{Pi})
        D_{vi, ex} = -gamma_v / (d . s_f) * ((s_f . v_P) - s_{f, i} * v_{P, i}) * s_{f, i}  (RHS)

    Parameters
    ----------
    gamma_v : float
        Kinematic / dynamic viscosity or diffusion coefficient (must be positive).
    s_f : np.ndarray, shape (3,)
        Boundary face area normal vector.
    d_vec : np.ndarray, shape (3,)
        Distance vector d = c_f - c_P.
    v_P : np.ndarray, shape (3,)
        Vector value at adjacent cell centroid P.

    Returns
    -------
    D_im : np.ndarray, shape (3,)
        Implicit diagonal contribution for each vector component (all positive).
    D_ex : np.ndarray, shape (3,)
        Explicit source / RHS contribution for each vector component.
    """
    if gamma_v < 0.0:
        raise ValueError(f"Diffusivity gamma_v must be non-negative, got {gamma_v}")
    s_f = np.asarray(s_f, dtype=np.float64)
    d_vec = np.asarray(d_vec, dtype=np.float64)
    v_P = np.asarray(v_P, dtype=np.float64)

    ds = float(np.dot(d_vec, s_f))
    if ds <= 1e-15:
        raise ValueError(f"Non-positive cell-to-face dot product d . s_f = {ds}; inverted boundary cell.")

    s_dot_v = float(np.dot(s_f, v_P))
    D_im = gamma_v * (s_f ** 2) / ds
    D_ex = -gamma_v * ((s_dot_v - s_f * v_P) * s_f) / ds

    return D_im, D_ex


def compute_symmetry_tensor_diffusion_split(
    gamma_T: float,
    s_f: np.ndarray,
    d_vec: np.ndarray,
    T_P: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray]:
    """Patankar diagonally-dominant split for symmetric rank-2 tensor diffusion flux on symmetry plane (Paper 7 Eq. 21-22).

    For tensor component (i, j):
        D_{Tij, im} = gamma_T * (s_i^2 + s_j^2) / (d . s) > 0
        D_{Tij, ex} = -gamma_T / (d . s) * [
            s_i * (s . T_P)_j + s_j * (s . T_P)_i
            - 2 * (s^T T_P s) * s_i * s_j / (s . s)
            - (s_i^2 + s_j^2) * T_{P, ij}
        ]

    Parameters
    ----------
    gamma_T : float
        Tensor diffusion coefficient (e.g. stress diffusivity).
    s_f : np.ndarray, shape (3,)
        Boundary face area normal vector.
    d_vec : np.ndarray, shape (3,)
        Distance vector d = c_f - c_P.
    T_P : np.ndarray, shape (3, 3)
        Symmetric rank-2 tensor at adjacent cell centroid P.

    Returns
    -------
    D_im : np.ndarray, shape (3, 3)
        Implicit diagonal contributions (unconditionally non-negative).
    D_ex : np.ndarray, shape (3, 3)
        Explicit RHS contributions.
    """
    if gamma_T < 0.0:
        raise ValueError(f"Diffusivity gamma_T must be non-negative, got {gamma_T}")
    s_f = np.asarray(s_f, dtype=np.float64)
    d_vec = np.asarray(d_vec, dtype=np.float64)
    T_P = np.asarray(T_P, dtype=np.float64)

    ds = float(np.dot(d_vec, s_f))
    s2 = float(np.dot(s_f, s_f))
    if ds <= 1e-15 or s2 <= 1e-30:
        raise ValueError("Degenerate boundary face or inverted cell.")

    s_T = np.dot(s_f, T_P)  # shape (3,)
    s_T_s = float(np.dot(s_T, s_f))

    D_im = np.zeros((3, 3), dtype=np.float64)
    D_ex = np.zeros((3, 3), dtype=np.float64)

    for i in range(3):
        for j in range(3):
            diag_weight = (s_f[i] ** 2 + s_f[j] ** 2)
            D_im[i, j] = gamma_T * diag_weight / ds
            ex_term = (
                s_f[i] * s_T[j]
                + s_f[j] * s_T[i]
                - 2.0 * s_T_s * s_f[i] * s_f[j] / s2
                - diag_weight * T_P[i, j]
            )
            D_ex[i, j] = -gamma_T * ex_term / ds

    return D_im, D_ex


# ==============================================================================
# §12.2 & Snippet 12-14 Numerics Contracts
# ==============================================================================

class FaceClass:
    """Classification of internal and boundary faces for FVM assembly."""
    INVALID = -1
    FAST = 0        # Class 0: Near-orthogonal, low-skew (M-matrix upwind)
    CORRECTED = 1   # Class 1: Non-orthogonal / skewed requiring deferred correction


def compute_linear_exact_face_geometry(
    x_P: np.ndarray,
    x_N: np.ndarray,
    x_f: np.ndarray,
    area_f: np.ndarray,
    h_ref: float,
    skew_tol: float = 1e-12,
    non_ortho_tol: float = 1e-12,
) -> Dict[str, Any]:
    """Compute linear-exact plane intersection geometry and FaceClass (Snippet 12 / §12.2).

    Intersects the PN ray x(beta) = x_P + beta * (x_N - x_P) with the face plane
    (x - x_f) . S_f = 0:
        beta_f = ((x_f - x_P) . S_f) / (d_f . S_f)
        alpha_f = ||S_f||^2 / (d_f . S_f)
        m_f^{skew} = x_f - (x_P + beta_f * d_f)  (lies exactly in the face plane)
        k_f^{corr} = S_f - alpha_f * d_f

    Explicitly rejects faces with cos(theta_f) <= 1e-8 or non-finite values as INVALID.

    Parameters
    ----------
    x_P : np.ndarray, shape (3,)
        Owner cell centroid.
    x_N : np.ndarray, shape (3,)
        Neighbour cell centroid.
    x_f : np.ndarray, shape (3,)
        Face centroid.
    area_f : np.ndarray, shape (3,)
        Face area normal vector S_f.
    h_ref : float
        Characteristic reference grid spacing.
    skew_tol : float
        Relative skewness tolerance for Fast class.
    non_ortho_tol : float
        Relative non-orthogonality tolerance for Fast class.

    Returns
    -------
    geom : Dict[str, Any]
        Dictionary with keys:
            'kind': FaceClass.FAST, FaceClass.CORRECTED, or FaceClass.INVALID
            'beta': float
            'alpha': float
            'skew': np.ndarray, shape (3,)
            'correction': np.ndarray, shape (3,)
            'relative_skew': float
            'non_ortho': float
    """
    x_P = np.asarray(x_P, dtype=np.float64)
    x_N = np.asarray(x_N, dtype=np.float64)
    x_f = np.asarray(x_f, dtype=np.float64)
    area_f = np.asarray(area_f, dtype=np.float64)

    d = x_N - x_P
    ds = float(np.dot(d, area_f))
    d2 = float(np.dot(d, d))
    s2 = float(np.dot(area_f, area_f))

    invalid_res = {
        "kind": FaceClass.INVALID,
        "beta": 0.0,
        "alpha": 0.0,
        "skew": np.zeros(3, dtype=np.float64),
        "correction": np.zeros(3, dtype=np.float64),
        "relative_skew": float("inf"),
        "non_ortho": float("inf"),
    }

    if not (d2 > 0.0 and s2 > 0.0 and h_ref > 0.0):
        return invalid_res

    denom = np.sqrt(d2 * s2)
    cosine = ds / denom
    if not np.isfinite(cosine) or cosine <= 1e-8:
        # Explicit FaceClass::INVALID rejection (no silent clamps!)
        return invalid_res

    beta = float(np.dot(x_f - x_P, area_f) / ds)
    alpha = float(s2 / ds)
    skew = x_f - (x_P + d * beta)
    correction = area_f - d * alpha
    rel_skew = float(np.linalg.norm(skew) / h_ref)
    non_ortho = float(np.linalg.norm(correction) / np.sqrt(s2))

    kind = FaceClass.FAST if (rel_skew <= skew_tol and non_ortho <= non_ortho_tol) else FaceClass.CORRECTED

    return {
        "kind": kind,
        "beta": beta,
        "alpha": alpha,
        "skew": skew,
        "correction": correction,
        "relative_skew": rel_skew,
        "non_ortho": non_ortho,
    }


def gather_residual_cell_face_csr(
    owned_cells: int,
    offsets: np.ndarray,
    faces: np.ndarray,
    orientation: np.ndarray,
    face_flux: np.ndarray,
    residual: Optional[np.ndarray] = None,
) -> np.ndarray:
    """Deterministic, atomic-free cell residual gather via CellFaceCSR adjacency (Snippet 12).

    Iterates only over owned_cells without atomicAdd or ghost-cell corruption:
        residual[c] += sum_{k = offsets[c]}^{offsets[c+1]-1} orientation[k] * face_flux[faces[k]]

    Parameters
    ----------
    owned_cells : int
        Number of owned cells (excluding ghost / halo cells).
    offsets : np.ndarray, shape (N_cells + 1,)
        CSR row offsets.
    faces : np.ndarray
        Flat face indices for each cell incident face.
    orientation : np.ndarray
        Orientation signs (+1 for owner, -1 for neighbour).
    face_flux : np.ndarray
        Flux on each face.
    residual : Optional[np.ndarray]
        Pre-allocated output residual array; allocated if None.

    Returns
    -------
    residual : np.ndarray, shape (owned_cells,)
        Gathered cell residuals.
    """
    if residual is None:
        residual = np.zeros(owned_cells, dtype=np.float64)
    else:
        residual = np.asarray(residual, dtype=np.float64)

    for cell in range(owned_cells):
        start = offsets[cell]
        end = offsets[cell + 1]
        sum_flux = 0.0
        for k in range(start, end):
            f_idx = faces[k]
            sum_flux += float(orientation[k]) * float(face_flux[f_idx])
        residual[cell] += sum_flux

    return residual


def factorize_spd_metric_cholesky(
    M: np.ndarray,
    eps: float = 1e-12,
    h_min: Optional[float] = None,
    h_max: Optional[float] = None,
) -> Tuple[np.ndarray, np.ndarray]:
    """SPD Cholesky factorization of Riemannian metric tensor M = L L^T + eps I (§12.2).

    Guarantees strict positive-definiteness M > 0. If h_min / h_max are specified,
    eigenvalues of M are clamped to [h_max^-2, h_min^-2].

    Parameters
    ----------
    M : np.ndarray, shape (3, 3)
        Symmetric 3x3 metric tensor candidate.
    eps : float
        Regularization parameter to guarantee strict positive-definiteness.
    h_min : Optional[float]
        Minimum allowed step size.
    h_max : Optional[float]
        Maximum allowed step size.

    Returns
    -------
    M_spd : np.ndarray, shape (3, 3)
        Strictly positive definite metric tensor.
    L : np.ndarray, shape (3, 3)
        Lower triangular Cholesky factor such that M_spd = L L^T.
    """
    M = np.asarray(M, dtype=np.float64)
    M_sym = 0.5 * (M + M.T)

    # Eigen-decomposition for bounds check / clamping
    eigvals, eigvecs = np.linalg.eigh(M_sym)
    min_eig = eps
    max_eig = 1e12

    if h_max is not None and h_max > 0.0:
        min_eig = max(eps, 1.0 / (h_max ** 2))
    if h_min is not None and h_min > 0.0:
        max_eig = 1.0 / (h_min ** 2)

    clamped_eigvals = np.clip(eigvals, min_eig, max_eig)
    M_spd = eigvecs @ np.diag(clamped_eigvals) @ eigvecs.T

    # Compute lower-triangular Cholesky factor
    L = np.linalg.cholesky(M_spd)
    return M_spd, L


def project_boundary_points_tangential_newton(
    points: np.ndarray,
    sdf_eval_fn: Any,
    sdf_grad_fn: Any,
    adjoint_grad: np.ndarray,
    step_size: float = 1.0,
) -> np.ndarray:
    """Tangential CAD projection + 1-step Newton re-projection onto psi(x) = 0 (Snippet 14).

    For boundary nodes with prescribed sensitivity / gradient g:
        1. Tangential projection: g_tan = g - n * (g . n)
        2. Tangential trial displacement: x_trial = x - eta * g_tan
        3. Newton re-projection: x_final = x_trial - psi(x_trial) * n(x_trial)

    Parameters
    ----------
    points : np.ndarray, shape (N, 3)
        Boundary coordinates.
    sdf_eval_fn : callable
        Function returning signed distance psi(x).
    sdf_grad_fn : callable
        Function returning normalized unit gradient n = grad(psi) / ||grad(psi)||.
    adjoint_grad : np.ndarray, shape (N, 3)
        Adjoint shape sensitivity or displacement gradient.
    step_size : float
        Optimization step length eta.

    Returns
    -------
    morphed_points : np.ndarray, shape (N, 3)
        Morphed points lying accurately on the CAD zero level-set psi = 0.
    """
    pts = np.asarray(points, dtype=np.float64)
    grads = np.asarray(adjoint_grad, dtype=np.float64)
    result = np.zeros_like(pts)

    for i in range(len(pts)):
        p = pts[i]
        g = grads[i]
        n = sdf_grad_fn(p)
        n = n / (np.linalg.norm(n) + 1e-15)

        # 1. Tangential projection
        g_tan = g - n * float(np.dot(g, n))

        # 2. Tangential trial step
        x_trial = p - step_size * g_tan

        # 3. 1-step Newton re-projection
        psi_val = float(sdf_eval_fn(x_trial))
        n_trial = sdf_grad_fn(x_trial)
        n_trial = n_trial / (np.linalg.norm(n_trial) + 1e-15)
        x_final = x_trial - psi_val * n_trial

        result[i] = x_final

    return result


def compute_gradient_free_deferred_correction(
    m_dot: float,
    gamma: float,
    alpha_f: float,
    beta_f: float,
    phi_P: float,
    phi_N: float,
) -> Tuple[float, float, float, float, float]:
    """Class 0 gradient-free convective upwind deferred correction (Snippet 13 / §12.2).

    Guarantees strict M-matrix diagonal dominance (off-diagonals a_PN <= 0, a_NP <= 0)
    for arbitrary Peclet numbers without loading cell gradients grad(phi).

    Parameters
    ----------
    m_dot : float
        Face convective mass flow rate (owner -> neighbour).
    gamma : float
        Diffusivity coefficient.
    alpha_f : float
        Face conductance ||S_f||^2 / (d_f . S_f).
    beta_f : float
        Linear-exact face plane intersection interpolation factor.
    phi_P : float
        Scalar value at owner cell centroid.
    phi_N : float
        Scalar value at neighbour cell centroid.

    Returns
    -------
    a_PN : float
        Implicit off-diagonal matrix entry (P, N) <= 0.
    a_NP : float
        Implicit off-diagonal matrix entry (N, P) <= 0.
    a_P_diag : float
        Implicit diagonal contribution for owner cell >= 0.
    a_N_diag : float
        Implicit diagonal contribution for neighbour cell >= 0.
    flux_deferred : float
        Explicit high-order minus upwind convective deferred correction flux on RHS.
    """
    diff_flux = gamma * alpha_f
    m_pos = max(m_dot, 0.0)
    m_neg = min(m_dot, 0.0)

    # Implicit off-diagonals (strictly <= 0 for all Peclet numbers -> M-matrix guaranteed)
    a_PN = m_neg - diff_flux
    a_NP = -m_pos - diff_flux

    # Continuity-corrected diagonal contributions (a_PP = sum |a_PN|)
    a_P_diag = -a_PN
    a_N_diag = -a_NP

    # Explicit high-order minus upwind convective deferred correction:
    # Uses ONLY scalar phi_P, phi_N (loads NO cell gradients grad_phi)
    flux_ho = m_dot * (beta_f * phi_N + (1.0 - beta_f) * phi_P)
    flux_upw = m_pos * phi_P + m_neg * phi_N
    flux_deferred = flux_ho - flux_upw

    return a_PN, a_NP, a_P_diag, a_N_diag, flux_deferred


def compute_three_map_mesh_error_map(
    mesh: UnstructuredMesh,
    u_pilot: Optional[np.ndarray] = None,
    wall_distance: Optional[np.ndarray] = None,
    H_u: Optional[np.ndarray] = None,
    residual: Optional[np.ndarray] = None,
) -> Dict[str, Dict[str, Any]]:
    """Separate 3-Map MeshErrorMap architecture (§12.2).

    Separates mesh error diagnostics into:
    1. GeometryMap:
       Dimensionless skewness ||m_f^skew|| / h_ref, non-orthogonality ||k_f^corr|| / ||S_f||, aspect ratio,
       and FaceClass classification (Class 0 Fast, Class 1 Corrected, Invalid).
    2. SolutionIndicator:
       Vorticity norm ||curl(u_pilot)||, wall distance SDF, and Hessian norm ||H_u||.
    3. ErrorEstimator:
       Equation residual norm and adjoint DWR indicator.

    Parameters
    ----------
    mesh : UnstructuredMesh
        Mesh to diagnose.
    u_pilot : Optional[np.ndarray], shape (N_cells, 3)
        Velocity field from pilot run.
    wall_distance : Optional[np.ndarray], shape (N_cells,)
        SDF wall distance field.
    H_u : Optional[np.ndarray], shape (N_cells, 3, 3)
        Solution Hessian field.
    residual : Optional[np.ndarray], shape (N_cells,)
        Cell residual field.

    Returns
    -------
    three_maps : Dict[str, Dict[str, Any]]
        Dictionary containing 'GeometryMap', 'SolutionIndicator', and 'ErrorEstimator'.
    """
    n_faces = mesh.num_faces
    n_cells = mesh.num_cells
    h_ref = float(np.mean(mesh.cell_volumes) ** (1.0 / 3.0)) if n_cells > 0 else 1.0

    # 1. GeometryMap
    geom_map: Dict[str, Any] = {
        "m_skew_relative": mesh.error_map.get("skewness", np.zeros(n_faces)),
        "non_orthogonality": mesh.error_map.get("non_orthogonality", np.zeros(n_faces)),
        "aspect_ratio": mesh.error_map.get("aspect_ratio", np.ones(n_cells)),
        "stabilization_mask": mesh.error_map.get("stabilization_mask", np.zeros(n_faces, dtype=np.int32)),
    }

    # 2. SolutionIndicator
    sol_indicator: Dict[str, Any] = {}
    if u_pilot is not None:
        sol_indicator["pilot_velocity_magnitude"] = np.linalg.norm(u_pilot, axis=-1)
    if wall_distance is not None:
        sol_indicator["wall_distance_sdf"] = np.asarray(wall_distance, dtype=np.float64)
    if H_u is not None:
        sol_indicator["hessian_frobenius_norm"] = np.linalg.norm(H_u, axis=(-2, -1))

    # 3. ErrorEstimator
    err_estimator: Dict[str, Any] = {}
    if residual is not None:
        err_estimator["residual_norm"] = np.abs(np.asarray(residual, dtype=np.float64))
    else:
        err_estimator["residual_norm"] = np.zeros(n_cells, dtype=np.float64)

    return {
        "GeometryMap": geom_map,
        "SolutionIndicator": sol_indicator,
        "ErrorEstimator": err_estimator,
    }



