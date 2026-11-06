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


def get_native_dll() -> Optional[ctypes.CDLL]:
    """Load and return the native cfd_mesher_native shared library if available."""
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED
    if _DLL_LOAD_ATTEMPTED:
        return _NATIVE_DLL

    _DLL_LOAD_ATTEMPTED = True
    dll_path = _find_native_library()
    if dll_path is None:
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
        return dll
    except Exception as e:
        logger.warning(f"Failed to load native mesher library from {dll_path}: {e}")
        _NATIVE_DLL = None
        return None


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
        return CellType.HEXAHEDRON
    elif nv == 6:
        return CellType.WEDGE
    elif nv == 5:
        return CellType.PYRAMID
    else:
        pts_poly = points[cell_nodes]
        if np.all(np.abs(pts_poly[:, 2] - pts_poly[0, 2]) < 1e-8):
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


def compute_difvm_topology_native(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
) -> UnstructuredMesh:
    """Build UnstructuredMesh using native C++20 core (cfd_mesher_native shared library)."""
    dll = get_native_dll()
    if dll is None:
        raise RuntimeError("Native library cfd_mesher_native is not available.")

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
                    p_name = patch.name.decode("utf-8", errors="ignore")
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
            metadata={"engine": "native_cpp20"},
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
    dll = get_native_dll()
    if dll is None or not hasattr(dll, "cfd_mesher_generate_hexcore"):
        raise RuntimeError("Native cfd_mesher_generate_hexcore is not available.")

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
            alpha_curv=config.sizing.alpha_curv,
            beta_prox=config.sizing.beta_prox,
            num_bl_layers=config.inflation.num_layers if config.inflation else 0,
            bl_first_height=config.inflation.first_layer_height if config.inflation else 0.0,
            bl_growth_rate=config.inflation.growth_rate if config.inflation else 1.2,
            bl_pinching_enabled=1 if (config.inflation and config.inflation.enable_pinching) else 0,
            bl_pinching_angle_deg=config.inflation.pinching_angle_deg if config.inflation else 45.0,
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
            p_name = patch.name.decode("utf-8", errors="ignore")
            boundaries[p_name] = (int(patch.start_face), int(patch.num_faces))
            if b"coupledWall" in patch.patch_type:
                baffles.append({
                    "name": p_name,
                    "type": "coupledWall",
                    "start_face": int(patch.start_face),
                    "num_faces": int(patch.num_faces),
                    "region_id": int(patch.region_id),
                })

        # Compute Berger-Giuliani WSRD weights for cut-cells with 0.20 <= alpha < 0.50
        wsrd_weights = {}
        for c in range(actual_n_cells):
            a_c = float(wsrd_alpha[c])
            if 0.20 <= a_c < 0.50:
                nb_indices = []
                for f_idx in range(actual_n_faces):
                    if owner[f_idx] == c and neighbour[f_idx] >= 0:
                        nb_indices.append(int(neighbour[f_idx]))
                    elif neighbour[f_idx] == c:
                        nb_indices.append(int(owner[f_idx]))
                if nb_indices:
                    n_nb = len(nb_indices)
                    w_nbs = [float((1.0 / n_nb) * max(0.0, 1.0 - wsrd_alpha[nb] / 0.50)) for nb in nb_indices]
                    w_self = float(1.0 - sum(w_nbs))
                    wsrd_weights[c] = {"self": w_self, "neighbors": dict(zip(nb_indices, w_nbs))}

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
            metadata={"engine": "native_cpp20_hexcore", "wsrd_weights": wsrd_weights},
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
        cfaces = _extract_cell_faces_py(cell_arr, pts)
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
        raw_faces = _extract_cell_faces_py(cell_arr)
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

    # Aspect ratio
    aspect_ratio = np.ones(n_cells, dtype=np.float64)
    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        cpts = pts[cell_arr]
        diffs = cpts[:, None, :] - cpts[None, :, :]
        dists = np.linalg.norm(diffs, axis=-1)
        nonzero_dists = dists[dists > 1e-12]
        if len(nonzero_dists) > 0:
            aspect_ratio[c_idx] = max(1.0, float(np.max(nonzero_dists) / np.min(nonzero_dists)))

    # 7. Cell-wise factors: omega_cell and wsrd_alpha
    min_beta_per_cell = np.ones(n_cells, dtype=np.float64)
    if n_faces > 0:
        np.minimum.at(min_beta_per_cell, owner, beta_upwind)
        int_mask = neighbour >= 0
        if np.any(int_mask):
            np.minimum.at(min_beta_per_cell, neighbour[int_mask], beta_upwind[int_mask])
    omega_cell = np.clip(min_beta_per_cell, 0.1, 1.0)

    # wsrd_alpha: Berger-Giuliani volume ratio against median positive cell volume
    pos_vols = cell_volumes[cell_volumes > 1e-60]
    v_target = float(np.median(pos_vols)) if len(pos_vols) > 0 else 1.0
    v_denom = v_target if v_target > 1e-60 else 1.0
    wsrd_alpha = np.clip(cell_volumes / (v_denom + 1e-30), 0.0, 1.0)

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
        metadata={"engine": "pure_python_fallback"},
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
    faces_coupled_a: List[np.ndarray] = []
    faces_coupled_b: List[np.ndarray] = []

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
                faces_coupled_a.append(fn_a)

            # Patch B: outward normal from cell_b pointing into region A (sf_b == -sf_a)
            new_owner[fb] = cell_b
            new_neighbour[fb] = -1
            new_face_areas[fb] = sf_b
            new_face_centres[fb] = cf
            new_delta_f[fb] = cf - mesh.cell_centres[cell_b]
            new_k_f[fb] = np.zeros(3, dtype=np.float64)
            if has_faces:
                faces_coupled_b.append(fn_b)

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
        faces_clean + faces_orig_bnd + faces_coupled_a + faces_coupled_b
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


def compute_ami_weights(
    source_centres: np.ndarray,
    source_areas: np.ndarray,
    target_centres: np.ndarray,
    target_areas: np.ndarray,
    radius: float = 0.05,
) -> np.ndarray:
    """Compute Arbitrary Mesh Interface (AMI) interpolation weights for non-conformal sliding interfaces.

    Guarantees partition of unity: sum_j w_ij == 1.0 for each target face i.
    """
    n_target = len(target_centres)
    n_source = len(source_centres)
    weights = np.zeros((n_target, n_source), dtype=np.float64)

    for i in range(n_target):
        diffs = source_centres - target_centres[i]
        dists = np.linalg.norm(diffs, axis=1)
        in_range = dists < radius
        if np.any(in_range):
            inv_d = 1.0 / (dists[in_range] + 1e-12)
            w = inv_d / np.sum(inv_d)
            weights[i, in_range] = w
        else:
            nearest = np.argmin(dists)
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
) -> UnstructuredMesh:
    """Build UnstructuredMesh in Structure of Arrays (SoA) layout.

    Transparently uses compiled C++20 core (cfd_mesher_native shared library) if available,
    falling back to pure Python if library is not compiled or use_native=False.
    """
    mesh = None
    if use_native and get_native_dll() is not None and boundary_patches is None and face_patch_ids is None:
        try:
            mesh = compute_difvm_topology_native(points, cells, regions=regions)
            if cell_types is not None:
                mesh.cell_types = np.asarray(cell_types, dtype=np.int32)
            if topological_dim is not None:
                mesh.topological_dim = topological_dim
        except ValueError:
            # Let non-manifold ValueError propagate immediately
            raise
        except Exception as e:
            if strict_native:
                raise RuntimeError(f"Native mesher execution failed in strict mode: {e}") from e
            logger.warning(f"Native mesher failed: {e}, falling back to Python.")
            mesh = None

    if mesh is None:
        if strict_native and use_native:
            raise RuntimeError("Strict native execution requested but native library is unavailable.")
        mesh = compute_difvm_topology_python(
            points,
            cells,
            regions=regions,
            boundary_patches=boundary_patches,
            face_patch_ids=face_patch_ids,
            patch_names=patch_names,
            cell_types=cell_types,
            topological_dim=topological_dim,
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

