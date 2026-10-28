"""DiFVM Graph-Topology & Polyhedral Structure of Arrays (SoA) Engine.

Implements high-performance unstructured FVM mesh topology construction:
- Direct C-ABI bridge to cfd_mesher_native.dll for zero-overhead native execution
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
import os
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

import numpy as np

from backend.meshing.models import UnstructuredMesh

# Path to native shared library
DLL_DIR = Path(__file__).resolve().parent
NATIVE_DLL_PATH = DLL_DIR / "cfd_mesher_native.dll"

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


def get_native_dll() -> Optional[ctypes.CDLL]:
    """Load and return the native cfd_mesher_native.dll if available."""
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED
    if _DLL_LOAD_ATTEMPTED:
        return _NATIVE_DLL

    _DLL_LOAD_ATTEMPTED = True
    if not NATIVE_DLL_PATH.exists():
        return None

    try:
        # Load DLL
        dll = ctypes.CDLL(str(NATIVE_DLL_PATH))

        # Setup function signatures
        dll.cfd_mesher_create.restype = ctypes.c_void_p
        dll.cfd_mesher_create.argtypes = []

        dll.cfd_mesher_free.restype = None
        dll.cfd_mesher_free.argtypes = [ctypes.c_void_p]

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

        dll.cfd_mesher_get_non_orthogonality_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_non_orthogonality_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_skewness_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_skewness_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_aspect_ratio_ptr.restype = ctypes.POINTER(ctypes.c_double)
        dll.cfd_mesher_get_aspect_ratio_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_stabilization_mask_ptr.restype = ctypes.POINTER(ctypes.c_int32)
        dll.cfd_mesher_get_stabilization_mask_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_regions_ptr.restype = ctypes.POINTER(ctypes.c_int32)
        dll.cfd_mesher_get_cell_regions_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_offsets_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_cell_offsets_ptr.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_get_cell_nodes_ptr.restype = ctypes.POINTER(ctypes.c_int64)
        dll.cfd_mesher_get_cell_nodes_ptr.argtypes = [ctypes.c_void_p]

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

        dll.cfd_mesher_get_boundary_patch.restype = ctypes.c_int32
        dll.cfd_mesher_get_boundary_patch.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int64,
            ctypes.POINTER(BoundaryPatchC),
        ]

        dll.cfd_mesher_compute_max_divergence_closure.restype = ctypes.c_double
        dll.cfd_mesher_compute_max_divergence_closure.argtypes = [ctypes.c_void_p]

        dll.cfd_mesher_compute_max_decomposition_error.restype = ctypes.c_double
        dll.cfd_mesher_compute_max_decomposition_error.argtypes = [ctypes.c_void_p]

        _NATIVE_DLL = dll
        return dll
    except Exception:
        _NATIVE_DLL = None
        return None


def _extract_cell_faces_py(cell_nodes: np.ndarray) -> List[List[int]]:
    """Extract ordered face vertex lists for standard 2D and 3D cells."""
    nv = len(cell_nodes)
    c = cell_nodes
    if nv == 8:
        # Hexahedron: 6 faces with outward normals
        return [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],  # bottom (-z)
            [int(c[4]), int(c[5]), int(c[6]), int(c[7])],  # top (+z)
            [int(c[0]), int(c[1]), int(c[5]), int(c[4])],  # front (-y)
            [int(c[1]), int(c[2]), int(c[6]), int(c[5])],  # right (+x)
            [int(c[2]), int(c[3]), int(c[7]), int(c[6])],  # back (+y)
            [int(c[3]), int(c[0]), int(c[4]), int(c[7])],  # left (-x)
        ]
    elif nv == 4:
        # Tetrahedron: 4 faces
        return [
            [int(c[0]), int(c[2]), int(c[1])],
            [int(c[0]), int(c[1]), int(c[3])],
            [int(c[1]), int(c[2]), int(c[3])],
            [int(c[2]), int(c[0]), int(c[3])],
        ]
    elif nv == 6:
        # Wedge / Triangular prism: 5 faces
        return [
            [int(c[0]), int(c[2]), int(c[1])],
            [int(c[3]), int(c[4]), int(c[5])],
            [int(c[0]), int(c[1]), int(c[4]), int(c[3])],
            [int(c[1]), int(c[2]), int(c[5]), int(c[4])],
            [int(c[2]), int(c[0]), int(c[3]), int(c[5])],
        ]
    elif nv == 5:
        # Pyramid: 5 faces
        return [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],
            [int(c[0]), int(c[1]), int(c[4])],
            [int(c[1]), int(c[2]), int(c[4])],
            [int(c[2]), int(c[3]), int(c[4])],
            [int(c[3]), int(c[0]), int(c[4])],
        ]
    elif nv == 3:
        # 2D triangle
        return [
            [int(c[0]), int(c[1])],
            [int(c[1]), int(c[2])],
            [int(c[2]), int(c[0])],
        ]
    else:
        # Fallback polygon
        return [[int(c[i]), int(c[(i + 1) % nv])] for i in range(nv)]


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


def compute_difvm_topology_native(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
) -> UnstructuredMesh:
    """Build UnstructuredMesh using native C++20 core (cfd_mesher_native.dll)."""
    dll = get_native_dll()
    if dll is None:
        raise RuntimeError("Native library cfd_mesher_native.dll is not available.")

    pts = np.ascontiguousarray(points, dtype=np.float64)
    if pts.ndim == 2 and pts.shape[1] == 2:
        pts = np.hstack([pts, np.zeros((len(pts), 1), dtype=np.float64)])

    n_points = len(pts)
    n_cells = len(cells)

    # Convert cells to CSR offsets and flat_nodes
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
            raise RuntimeError(f"Native cfd_mesher_build_from_cells failed with code {res}")

        actual_n_cells = dll.cfd_mesher_get_n_cells(mesh_handle)
        actual_n_faces = dll.cfd_mesher_get_n_faces(mesh_handle)
        actual_n_points = dll.cfd_mesher_get_n_points(mesh_handle)

        # Extract arrays by copying from native memory
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

        # Boundaries & Patches
        boundaries: Dict[str, Tuple[int, int]] = {}
        n_patches = dll.cfd_mesher_get_n_patches(mesh_handle)
        baffles: List[Dict[str, Any]] = []

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
            # Compute boundary range directly
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
        }

        # Check for CHT multi-region internal coupled faces if regions provided
        if regions_arr is not None and len(np.unique(regions_arr)) > 1 and not baffles:
            coupled_mask = (neighbour >= 0) & (cell_regions[owner] != cell_regions[neighbour])
            coupled_indices = np.where(coupled_mask)[0]
            if len(coupled_indices) > 0:
                baffles.append({
                    "name": "coupledWall",
                    "type": "coupledWall",
                    "face_indices": coupled_indices.tolist(),
                    "num_faces": len(coupled_indices),
                })

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
            metadata={"engine": "native_cpp20"},
        )
        if regions_arr is not None and len(np.unique(regions_arr)) > 1:
            mesh = extract_cht_twin_faces(mesh)
        return mesh
    finally:
        dll.cfd_mesher_free(mesh_handle)


def compute_difvm_topology_python(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
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

    # 1. Extract all cell faces and identify shared faces
    face_map: Dict[Tuple[int, ...], int] = {}
    temp_faces: List[Dict[str, Any]] = []
    cell_face_refs: List[List[Tuple[int, float]]] = [[] for _ in range(n_cells)]

    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        cfaces = _extract_cell_faces_py(cell_arr)
        for fnodes in cfaces:
            canonical_key = tuple(sorted(fnodes))
            if canonical_key not in face_map:
                f_idx = len(temp_faces)
                norm_v, cent_v = _compute_polygon_normal_and_centroid_py(fnodes, pts)
                temp_faces.append({
                    "nodes": fnodes,
                    "owner": c_idx,
                    "neighbour": -1,
                    "normal_owner": norm_v,
                    "centre": cent_v,
                })
                face_map[canonical_key] = f_idx
                cell_face_refs[c_idx].append((f_idx, 1.0))
            else:
                f_idx = face_map[canonical_key]
                temp_faces[f_idx]["neighbour"] = c_idx
                cell_face_refs[c_idx].append((f_idx, -1.0))

    # 2. Compute cell volumes and centroids via divergence theorem
    cell_centres = np.zeros((n_cells, 3), dtype=np.float64)
    cell_volumes = np.zeros(n_cells, dtype=np.float64)

    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        c_approx = np.mean(pts[cell_arr], axis=0)
        is_2d = np.all(np.abs(pts[cell_arr, 2]) < 1e-6)
        div_factor = 2.0 if is_2d else 3.0

        vol_sum = 0.0
        centroid_weighted = np.zeros(3, dtype=np.float64)

        for f_idx, sign in cell_face_refs[c_idx]:
            tf = temp_faces[f_idx]
            sf_out = tf["normal_owner"] * sign
            pyr_vol = float(np.dot(tf["centre"] - c_approx, sf_out)) / div_factor
            vol_sum += pyr_vol
            pyr_c = tf["centre"] * 0.75 + c_approx * 0.25
            centroid_weighted += pyr_c * pyr_vol

        if vol_sum > 1e-15:
            centroid_weighted /= vol_sum
            cell_volumes[c_idx] = vol_sum
        elif vol_sum < -1e-15:
            # Retain genuine negative volume to allow detection of inverted elements
            centroid_weighted /= vol_sum
            cell_volumes[c_idx] = vol_sum
        else:
            cell_volumes[c_idx] = 0.0
            centroid_weighted = c_approx

        cell_centres[c_idx] = centroid_weighted

    # 3. Partition and sort faces: internal first, boundary last
    internal_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] >= 0]
    boundary_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] < 0]

    # Sort internal faces by owner
    internal_indices.sort(key=lambda idx: (temp_faces[idx]["owner"], temp_faces[idx]["neighbour"]))

    sorted_indices = internal_indices + boundary_indices
    n_faces = len(sorted_indices)

    owner = np.zeros(n_faces, dtype=np.int64)
    neighbour = np.zeros(n_faces, dtype=np.int64)
    face_areas = np.zeros((n_faces, 3), dtype=np.float64)
    face_centres = np.zeros((n_faces, 3), dtype=np.float64)
    delta_f = np.zeros((n_faces, 3), dtype=np.float64)
    k_f = np.zeros((n_faces, 3), dtype=np.float64)
    non_ortho = np.zeros(n_faces, dtype=np.float64)
    skewness = np.zeros(n_faces, dtype=np.float64)
    stab_mask = np.zeros(n_faces, dtype=np.int32)

    for new_idx, orig_idx in enumerate(sorted_indices):
        tf = temp_faces[orig_idx]
        o = tf["owner"]
        nb = tf["neighbour"]
        owner[new_idx] = o
        neighbour[new_idx] = nb

        sf = tf["normal_owner"]
        cf = tf["centre"]
        face_areas[new_idx] = sf
        face_centres[new_idx] = cf

        c_o = cell_centres[o]

        if nb >= 0:
            c_nb = cell_centres[nb]
            df = c_nb - c_o
            df_norm = np.linalg.norm(df)
            sf_norm = np.linalg.norm(sf)
            sf_dot_df = float(np.dot(sf, df))

            if abs(sf_dot_df) > 1e-15:
                delta = df * (float(np.dot(sf, sf)) / sf_dot_df)
                k = sf - delta
            else:
                delta = sf.copy()
                k = np.zeros(3, dtype=np.float64)

            delta_f[new_idx] = delta
            k_f[new_idx] = k

            # Non-orthogonality angle
            cos_theta = 1.0
            if sf_norm > 1e-15 and df_norm > 1e-15:
                cos_theta = np.clip(sf_dot_df / (sf_norm * df_norm), 0.0, 1.0)
            theta_deg = float(np.arccos(cos_theta) * 180.0 / np.pi)
            non_ortho[new_idx] = theta_deg

            # Skewness
            skew = 0.0
            if sf_norm > 1e-15 and df_norm > 1e-15:
                nf = sf / sf_norm
                df_dot_nf = float(np.dot(df, nf))
                if abs(df_dot_nf) > 1e-15:
                    t = float(np.dot(cf - c_o, nf)) / df_dot_nf
                    x_int = c_o + df * t
                    skew = float(np.linalg.norm(cf - x_int) / df_norm)
            skewness[new_idx] = skew

            # Stabilization mask
            tier = 0
            if theta_deg >= 75.0 or skew >= 1.0:
                tier = 3
            elif theta_deg >= 65.0 or skew >= 0.7:
                tier = 2
            elif theta_deg >= 40.0 or skew >= 0.4:
                tier = 1
            stab_mask[new_idx] = tier
        else:
            df = cf - c_o
            delta_f[new_idx] = df
            k_f[new_idx] = np.zeros(3, dtype=np.float64)
            non_ortho[new_idx] = 0.0
            skewness[new_idx] = 0.0
            stab_mask[new_idx] = 0

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

    boundaries: Dict[str, Tuple[int, int]] = {}
    n_int = len(internal_indices)
    n_bnd = len(boundary_indices)
    if n_bnd > 0:
        boundaries["wall"] = (n_int, n_bnd)

    baffles: List[Dict[str, Any]] = []
    if len(np.unique(cell_regions)) > 1:
        coupled_mask = (neighbour >= 0) & (cell_regions[owner] != cell_regions[neighbour])
        coupled_indices = np.where(coupled_mask)[0]
        if len(coupled_indices) > 0:
            baffles.append({
                "name": "coupledWall",
                "type": "coupledWall",
                "face_indices": coupled_indices.tolist(),
                "num_faces": len(coupled_indices),
            })

    error_map = {
        "non_orthogonality": non_ortho,
        "non_ortho": non_ortho,
        "skewness": skewness,
        "skew": skewness,
        "aspect_ratio": aspect_ratio,
        "stabilization_mask": stab_mask,
        "mask": stab_mask,
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
        metadata={"engine": "pure_python_fallback"},
    )
    if cell_regions is not None and len(np.unique(cell_regions)) > 1:
        mesh = extract_cht_twin_faces(mesh)
    return mesh


def extract_cht_twin_faces(mesh: UnstructuredMesh) -> UnstructuredMesh:
    """Split internal multi-region interfaces into conformal collocated twin boundary patches."""
    if not mesh.is_multiregion or mesh.cell_regions is None:
        return mesh

    owner = list(mesh.owner)
    neighbour = list(mesh.neighbour)
    face_areas = list(mesh.face_areas)
    face_centres = list(mesh.face_centres)
    delta_f = list(mesh.delta_f)
    k_f = list(mesh.k_f)

    # Find internal faces separating two different regions
    coupled_internal = []
    for f in range(mesh.num_faces):
        o = owner[f]
        nb = neighbour[f]
        if nb >= 0 and mesh.cell_regions[o] != mesh.cell_regions[nb]:
            coupled_internal.append(f)

    if not coupled_internal:
        return mesh

    # Convert coupled internal faces into twin boundary faces
    start_solid = len(owner)
    twin_pairs = []

    for f in coupled_internal:
        o = owner[f]
        nb = neighbour[f]
        sf = face_areas[f]
        cf = face_centres[f]

        # Convert original face to fluid boundary face
        neighbour[f] = -1
        # Add twin solid boundary face with opposing normal: S_fS = -S_fF
        twin_f = len(owner)
        owner.append(nb)
        neighbour.append(-1)
        face_areas.append(-sf)
        face_centres.append(cf)
        delta_f.append(cf - mesh.cell_centres[nb])
        k_f.append(np.zeros(3, dtype=np.float64))
        twin_pairs.append((f, twin_f))

    boundaries = dict(mesh.boundaries)
    boundaries["coupledWall_fluid"] = (coupled_internal[0], len(coupled_internal))
    boundaries["coupledWall_solid"] = (start_solid, len(coupled_internal))

    baffles = [
        {
            "name": "coupledWall",
            "type": "coupledWall",
            "twin_pairs": twin_pairs,
            "face_indices": coupled_internal,
            "num_faces": len(twin_pairs),
        }
    ]

    new_error_map = dict(mesh.error_map)
    n_added = len(coupled_internal)
    for k in ["non_orthogonality", "non_ortho", "skewness", "skew", "stabilization_mask", "mask"]:
        if k in new_error_map and isinstance(new_error_map[k], np.ndarray):
            arr = new_error_map[k]
            if len(arr) == len(mesh.owner):
                pad = np.zeros(n_added, dtype=arr.dtype)
                new_error_map[k] = np.concatenate([arr, pad])

    return UnstructuredMesh(
        points=mesh.points,
        cells=mesh.cells,
        owner=np.array(owner, dtype=np.int64),
        neighbour=np.array(neighbour, dtype=np.int64),
        face_areas=np.array(face_areas, dtype=np.float64),
        face_centres=np.array(face_centres, dtype=np.float64),
        cell_centres=mesh.cell_centres,
        cell_volumes=mesh.cell_volumes,
        delta_f=np.array(delta_f, dtype=np.float64),
        k_f=np.array(k_f, dtype=np.float64),
        boundaries=boundaries,
        cell_regions=mesh.cell_regions,
        baffles=baffles,
        cyclic_pairs=mesh.cyclic_pairs,
        error_map=new_error_map,
        metadata=mesh.metadata,
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
    R_z = np.array([
        [cos_t, -sin_t, 0.0],
        [sin_t,  cos_t, 0.0],
        [0.0,    0.0,   1.0],
    ], dtype=np.float64)

    bnd_faces = np.where(mesh.neighbour < 0)[0]
    pairs = []
    for i, f1 in enumerate(bnd_faces):
        c1 = mesh.face_centres[f1]
        rot_c1 = np.dot(R_z, c1)
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
) -> UnstructuredMesh:
    """Build UnstructuredMesh in Structure of Arrays (SoA) layout.

    Transparently uses compiled C++20 core (cfd_mesher_native.dll) if available,
    falling back to pure Python if DLL is not compiled or use_native=False.
    """
    mesh = None
    if use_native and get_native_dll() is not None:
        try:
            mesh = compute_difvm_topology_native(points, cells, regions=regions)
        except Exception:
            mesh = None
    if mesh is None:
        mesh = compute_difvm_topology_python(points, cells, regions=regions)

    if cyclic_angle_rad > 0.0:
        mesh.cyclic_pairs = extract_cyclic_pairs(mesh, cyclic_angle_rad)

    return mesh
