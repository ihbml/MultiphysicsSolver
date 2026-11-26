"""DiFVM Graph-Topology & Polyhedral Structure of Arrays (SoA) Engine Facade.

Orchestrates native C++20 C-ABI topology/hexcore generation and pure-Python fallback,
re-exporting all topology, WSRD, coupling, and loader symbols from focused submodules:
- native_loader.py
- topology_wsrd.py
- topology_coupling.py
- topology_python.py
"""

from __future__ import annotations

import ctypes
import logging
import os
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.meshing.models import CellType, MeshingConfig, UnstructuredMesh
from backend.meshing.native_loader import (
    BoundaryPatchC,
    MeshingConfigC,
    _env_strict_native,
    _find_native_library,
    _reset_native_dll_cache,
    _verify_loaded_dll_abi,
    get_last_native_error,
    get_native_dll,
    get_native_load_error,
    raise_native_status_error,
    require_native_dll,
)
from backend.meshing.topology_coupling import (
    _clip_polygon_sutherland_hodgman,
    _polygon_area_2d,
    compute_ami_weights,
    extract_cht_twin_faces,
    extract_cyclic_pairs,
)
from backend.meshing.topology_python import (
    _classify_cell_type,
    _compute_polygon_normal_and_centroid_py,
    _extract_cell_faces_py,
    _is_face_folded_py,
    compute_difvm_topology_python,
)
from backend.meshing.topology_wsrd import (
    FaceClass,
    compute_amips_energy,
    compute_gradient_free_deferred_correction,
    compute_householder_reflection,
    compute_linear_exact_face_geometry,
    compute_symmetry_mirror_point,
    compute_symmetry_tensor_diffusion_split,
    compute_symmetry_vector_diffusion_split,
    compute_three_map_mesh_error_map,
    compute_wsrd_redistribution_map,
    compute_wsrd_weights,
    compute_wsrd_weights_array,
    factorize_spd_metric_cholesky,
    gather_residual_cell_face_csr,
    project_boundary_points_tangential_newton,
)

logger = logging.getLogger(__name__)


def _copy_c_array(ptr: Any, shape: Tuple[int, ...], default: Optional[np.ndarray] = None) -> np.ndarray:
    """Copy a C buffer pointer into an owning NumPy array."""
    if ptr:
        return np.copy(np.ctypeslib.as_array(ptr, shape=shape))
    if default is not None:
        return default
    raise RuntimeError(f"Expected non-null C-ABI pointer for shape {shape}")


def _extract_native_soa_fields(
    dll: ctypes.CDLL,
    mesh_handle: Any,
    actual_n_cells: int,
    actual_n_faces: int,
) -> Dict[str, Any]:
    """Copy SoA face/cell geometry and stabilization fields from native handle."""
    owner = _copy_c_array(dll.cfd_mesher_get_owner_ptr(mesh_handle), (actual_n_faces,))
    neighbour = _copy_c_array(dll.cfd_mesher_get_neighbour_ptr(mesh_handle), (actual_n_faces,))
    face_areas = _copy_c_array(dll.cfd_mesher_get_face_areas_ptr(mesh_handle), (actual_n_faces, 3))
    face_centres = _copy_c_array(dll.cfd_mesher_get_face_centres_ptr(mesh_handle), (actual_n_faces, 3))
    cell_centres = _copy_c_array(dll.cfd_mesher_get_cell_centres_ptr(mesh_handle), (actual_n_cells, 3))
    cell_volumes = _copy_c_array(dll.cfd_mesher_get_cell_volumes_ptr(mesh_handle), (actual_n_cells,))
    delta_f = _copy_c_array(dll.cfd_mesher_get_delta_f_ptr(mesh_handle), (actual_n_faces, 3))
    k_f = _copy_c_array(dll.cfd_mesher_get_k_f_ptr(mesh_handle), (actual_n_faces, 3))

    non_ortho = _copy_c_array(
        dll.cfd_mesher_get_non_orthogonality_ptr(mesh_handle),
        (actual_n_faces,),
        np.zeros(actual_n_faces, dtype=np.float64),
    )
    skewness = _copy_c_array(
        dll.cfd_mesher_get_skewness_ptr(mesh_handle),
        (actual_n_faces,),
        np.zeros(actual_n_faces, dtype=np.float64),
    )
    aspect_ratio = _copy_c_array(
        dll.cfd_mesher_get_aspect_ratio_ptr(mesh_handle),
        (actual_n_cells,),
        np.ones(actual_n_cells, dtype=np.float64),
    )
    stab_mask = _copy_c_array(
        dll.cfd_mesher_get_stabilization_mask_ptr(mesh_handle),
        (actual_n_faces,),
        np.zeros(actual_n_faces, dtype=np.int32),
    )
    cell_regions = _copy_c_array(
        dll.cfd_mesher_get_cell_regions_ptr(mesh_handle),
        (actual_n_cells,),
        np.zeros(actual_n_cells, dtype=np.int32),
    )

    m_skew = _copy_c_array(
        dll.cfd_mesher_get_m_skew_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_m_skew_ptr") else None,
        (actual_n_faces, 3),
        np.zeros((actual_n_faces, 3), dtype=np.float64),
    )
    k_f_comp = _copy_c_array(
        dll.cfd_mesher_get_k_f_comp_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_k_f_comp_ptr") else None,
        (actual_n_faces, 3),
        np.zeros((actual_n_faces, 3), dtype=np.float64),
    )
    beta_upwind = _copy_c_array(
        dll.cfd_mesher_get_beta_upwind_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_beta_upwind_ptr") else None,
        (actual_n_faces,),
        np.ones(actual_n_faces, dtype=np.float64),
    )
    omega_cell = _copy_c_array(
        dll.cfd_mesher_get_omega_cell_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_omega_cell_ptr") else None,
        (actual_n_cells,),
        np.ones(actual_n_cells, dtype=np.float64),
    )
    wsrd_alpha = _copy_c_array(
        dll.cfd_mesher_get_wsrd_alpha_ptr(mesh_handle) if hasattr(dll, "cfd_mesher_get_wsrd_alpha_ptr") else None,
        (actual_n_cells,),
        np.ones(actual_n_cells, dtype=np.float64),
    )

    return {
        "owner": owner,
        "neighbour": neighbour,
        "face_areas": face_areas,
        "face_centres": face_centres,
        "cell_centres": cell_centres,
        "cell_volumes": cell_volumes,
        "delta_f": delta_f,
        "k_f": k_f,
        "non_ortho": non_ortho,
        "skewness": skewness,
        "aspect_ratio": aspect_ratio,
        "stab_mask": stab_mask,
        "cell_regions": cell_regions,
        "m_skew": m_skew,
        "k_f_comp": k_f_comp,
        "beta_upwind": beta_upwind,
        "omega_cell": omega_cell,
        "wsrd_alpha": wsrd_alpha,
    }


def _extract_native_patches(
    dll: ctypes.CDLL, mesh_handle: Any, neighbour: np.ndarray, actual_n_faces: int
) -> Tuple[Dict[str, Tuple[int, int]], List[Dict[str, Any]]]:
    """Read BoundaryPatchC records from native mesh handle."""
    boundaries: Dict[str, Tuple[int, int]] = {}
    baffles: List[Dict[str, Any]] = []
    n_patches = dll.cfd_mesher_get_n_patches(mesh_handle)
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
    return boundaries, baffles


def _build_wsrd_and_error_map(
    soa: Dict[str, Any],
    actual_n_cells: int,
    actual_n_faces: int,
    config: Optional[MeshingConfig],
) -> Tuple[np.ndarray, Dict[int, Dict[str, Any]], Dict[str, np.ndarray]]:
    small_cell_ratio = float(getattr(config, "small_cell_ratio", 0.20)) if config is not None else 0.20
    wsrd_target_alpha = float(getattr(config, "wsrd_target_alpha", 0.50)) if config is not None else 0.50
    wsrd_alpha = soa["wsrd_alpha"]
    wsrd_weights = compute_wsrd_weights_array(
        wsrd_alpha, small_cell_ratio=small_cell_ratio, wsrd_target_alpha=wsrd_target_alpha
    )
    wsrd_redist_map = compute_wsrd_redistribution_map(
        wsrd_alpha,
        soa["owner"],
        soa["neighbour"],
        actual_n_cells,
        actual_n_faces,
        small_cell_ratio=small_cell_ratio,
        wsrd_target_alpha=wsrd_target_alpha,
    )
    error_map = {
        "non_orthogonality": soa["non_ortho"],
        "non_ortho": soa["non_ortho"],
        "skewness": soa["skewness"],
        "skew": soa["skewness"],
        "aspect_ratio": soa["aspect_ratio"],
        "stabilization_mask": soa["stab_mask"],
        "mask": soa["stab_mask"],
        "m_skew": soa["m_skew"],
        "k_f_comp": soa["k_f_comp"],
        "beta_upwind": soa["beta_upwind"],
        "omega_cell": soa["omega_cell"],
        "wsrd_alpha": wsrd_alpha,
        "wsrd_weights": wsrd_weights,
    }
    return wsrd_weights, wsrd_redist_map, error_map


def _copy_csr_buffers(
    dll: ctypes.CDLL, mesh_handle: Any, actual_n_faces: int, actual_n_cells: int
) -> Tuple[Optional[np.ndarray], Optional[np.ndarray], Optional[np.ndarray], Optional[np.ndarray]]:
    """Copy contiguous CSR face/cell buffers once into owning int64 arrays before free."""
    face_offsets: Optional[np.ndarray] = None
    face_nodes: Optional[np.ndarray] = None
    if hasattr(dll, "cfd_mesher_get_face_offsets_ptr") and hasattr(dll, "cfd_mesher_get_face_nodes_ptr"):
        fo_ptr = dll.cfd_mesher_get_face_offsets_ptr(mesh_handle)
        fn_ptr = dll.cfd_mesher_get_face_nodes_ptr(mesh_handle)
        n_fnodes = dll.cfd_mesher_get_n_face_nodes(mesh_handle) if hasattr(dll, "cfd_mesher_get_n_face_nodes") else 0
        if fo_ptr:
            face_offsets = np.array(
                np.ctypeslib.as_array(fo_ptr, shape=(actual_n_faces + 1,)), dtype=np.int64, copy=True
            )
            if fn_ptr and n_fnodes > 0:
                face_nodes = np.array(
                    np.ctypeslib.as_array(fn_ptr, shape=(n_fnodes,)), dtype=np.int64, copy=True
                )
            else:
                face_nodes = np.empty(0, dtype=np.int64)

    cell_offsets: Optional[np.ndarray] = None
    cell_nodes: Optional[np.ndarray] = None
    co_ptr = dll.cfd_mesher_get_cell_offsets_ptr(mesh_handle)
    cn_ptr = dll.cfd_mesher_get_cell_nodes_ptr(mesh_handle)
    if co_ptr:
        cell_offsets = np.array(
            np.ctypeslib.as_array(co_ptr, shape=(actual_n_cells + 1,)), dtype=np.int64, copy=True
        )
        total_cnodes = int(cell_offsets[-1]) if len(cell_offsets) > 0 else 0
        if cn_ptr and total_cnodes > 0:
            cell_nodes = np.array(
                np.ctypeslib.as_array(cn_ptr, shape=(total_cnodes,)), dtype=np.int64, copy=True
            )
        else:
            cell_nodes = np.empty(0, dtype=np.int64)

    return face_offsets, face_nodes, cell_offsets, cell_nodes


def compute_difvm_topology_native(
    points: np.ndarray,
    cells: List[np.ndarray],
    regions: Optional[np.ndarray] = None,
    config: Optional[MeshingConfig] = None,
) -> UnstructuredMesh:
    """Build UnstructuredMesh using native C++20 core (cfd_mesher_native shared library)."""
    dll = get_native_dll(strict_native=True)
    if dll is None:
        raise RuntimeError(f"Native library cfd_mesher_native is not available: {get_native_load_error()}")

    pts = np.ascontiguousarray(points, dtype=np.float64)
    if pts.ndim == 2 and pts.shape[1] == 2:
        pts = np.hstack([pts, np.zeros((len(pts), 1), dtype=np.float64)])

    n_points = len(pts)
    n_cells = len(cells)

    offsets = np.zeros(n_cells + 1, dtype=np.int64)
    cur = 0
    for i, c in enumerate(cells):
        offsets[i] = cur
        cur += len(c)
    offsets[-1] = cur
    flat_nodes = np.ascontiguousarray(np.concatenate(cells), dtype=np.int64) if cur > 0 else np.empty(0, dtype=np.int64)

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
            detail = get_last_native_error(dll, mesh_handle)
            if res == -2 and not any(tok in detail.lower() for tok in ("alloc", "memory", "budget")):
                raise ValueError(
                    "Non-manifold mesh: a face is shared by more than 2 cells."
                    + (f" ({detail})" if detail else "")
                )
            raise_native_status_error(dll, mesh_handle, "cfd_mesher_build_from_cells", res)

        actual_n_cells = dll.cfd_mesher_get_n_cells(mesh_handle)
        actual_n_faces = dll.cfd_mesher_get_n_faces(mesh_handle)

        soa = _extract_native_soa_fields(dll, mesh_handle, actual_n_cells, actual_n_faces)
        face_offsets, face_nodes, cell_offsets, cell_nodes = _copy_csr_buffers(
            dll, mesh_handle, actual_n_faces, actual_n_cells
        )
        if cell_offsets is None or cell_nodes is None:
            cell_offsets = offsets
            cell_nodes = flat_nodes

        cell_types: Optional[np.ndarray] = None
        if hasattr(dll, "cfd_mesher_get_cell_types_ptr"):
            ct_ptr = dll.cfd_mesher_get_cell_types_ptr(mesh_handle)
            if ct_ptr:
                cell_types = np.copy(np.ctypeslib.as_array(ct_ptr, shape=(actual_n_cells,)))
        if cell_types is None:
            cell_types = np.array([_classify_cell_type(c, pts).value for c in cells], dtype=np.int32)

        topological_dim = 3
        if hasattr(dll, "cfd_mesher_get_topological_dim"):
            topological_dim = dll.cfd_mesher_get_topological_dim(mesh_handle)
        if topological_dim <= 0:
            topological_dim = (
                2
                if all(t in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value) for t in cell_types)
                else 3
            )

        faces_list: Optional[List[np.ndarray]] = None
        if face_offsets is None or face_nodes is None or not hasattr(dll, "cfd_mesher_get_face_offsets_ptr"):
            py_mesh = compute_difvm_topology_python(pts, cells, regions=regions)
            faces_list = list(py_mesh.faces) if py_mesh.faces is not None else None
            face_offsets, face_nodes = None, None
            cell_types = py_mesh.cell_types
            topological_dim = py_mesh.topological_dim
            soa["m_skew"] = py_mesh.error_map["m_skew"]
            soa["k_f_comp"] = py_mesh.error_map["k_f_comp"]
            soa["beta_upwind"] = py_mesh.error_map["beta_upwind"]
            soa["omega_cell"] = py_mesh.error_map["omega_cell"]
            soa["wsrd_alpha"] = py_mesh.error_map["wsrd_alpha"]
            soa["stab_mask"] = py_mesh.error_map["stabilization_mask"]
            soa["non_ortho"] = py_mesh.error_map["non_ortho"]
            soa["skewness"] = py_mesh.error_map["skew"]
            soa["aspect_ratio"] = py_mesh.error_map["aspect_ratio"]
            soa["owner"] = py_mesh.owner
            soa["neighbour"] = py_mesh.neighbour
            soa["face_areas"] = py_mesh.face_areas
            soa["face_centres"] = py_mesh.face_centres
            soa["cell_centres"] = py_mesh.cell_centres
            soa["cell_volumes"] = py_mesh.cell_volumes
            soa["delta_f"] = py_mesh.delta_f
            soa["k_f"] = py_mesh.k_f
            boundaries = dict(py_mesh.boundaries)
            baffles = list(py_mesh.baffles)
        else:
            boundaries, baffles = _extract_native_patches(dll, mesh_handle, soa["neighbour"], actual_n_faces)

        wsrd_weights, wsrd_redist_map, error_map = _build_wsrd_and_error_map(
            soa, actual_n_cells, actual_n_faces, config
        )

        mesh = UnstructuredMesh(
            points=pts,
            cells=[] if cell_offsets is not None else cells,
            owner=soa["owner"],
            neighbour=soa["neighbour"],
            face_areas=soa["face_areas"],
            face_centres=soa["face_centres"],
            cell_centres=soa["cell_centres"],
            cell_volumes=soa["cell_volumes"],
            delta_f=soa["delta_f"],
            k_f=soa["k_f"],
            boundaries=boundaries,
            cell_regions=soa["cell_regions"],
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
            face_offsets=face_offsets,
            face_nodes=face_nodes,
            cell_offsets=cell_offsets,
            cell_nodes=cell_nodes,
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
        raise RuntimeError(f"Native cfd_mesher_generate_hexcore is not available: {get_native_load_error()}")

    pts = np.ascontiguousarray(surf_points, dtype=np.float64)
    faces = np.ascontiguousarray(surf_faces, dtype=np.int32)
    if pts.ndim != 2 or pts.shape[1] != 3:
        raise ValueError(f"surf_points must be Nx3 float64 array, got shape {pts.shape}")
    if faces.ndim != 2 or faces.shape[1] != 3:
        raise ValueError(f"surf_faces must be Mx3 int32 array, got shape {faces.shape}")

    bbox_ptr = None
    bbox_arr = None
    if farfield_bbox is not None:
        raw_bb = farfield_bbox
        if hasattr(raw_bb, "min_pt") and hasattr(raw_bb, "max_pt"):
            flat_bb = list(raw_bb.min_pt) + list(raw_bb.max_pt)
        elif len(raw_bb) == 2:
            flat_bb = list(raw_bb[0]) + list(raw_bb[1])
        elif len(raw_bb) == 6:
            flat_bb = list(raw_bb)
        else:
            raise ValueError(
                f"farfield_bbox must be ((xmin,ymin,zmin),(xmax,ymax,zmax)) or 6-tuple "
                f"(xmin,ymin,zmin,xmax,ymax,zmax), got {farfield_bbox!r}"
            )
        bbox_arr = np.ascontiguousarray(flat_bb, dtype=np.float64)
        if not np.all(np.isfinite(bbox_arr)):
            raise ValueError("farfield_bbox contains non-finite coordinates.")
        if np.any(bbox_arr[:3] >= bbox_arr[3:]):
            raise ValueError(
                f"Inverted farfield_bbox (expected [xmin,ymin,zmin,xmax,ymax,zmax] with min < max): {bbox_arr.tolist()}"
            )
        bbox_ptr = bbox_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

    mode_map = {"external_fluid": 0, "internal_fluid": 1, "solid_only": 2, "cht_coupled": 3}
    if domain_mode not in mode_map:
        raise ValueError(f"Unknown domain_mode {domain_mode!r}; expected one of {sorted(mode_map.keys())}.")
    d_mode = mode_map[domain_mode]

    cfg_c = None
    cfg_ptr = None
    if config is not None:
        cfg_c = MeshingConfigC(
            h_min=float(config.sizing.h_min) if config.sizing.h_min is not None else float(h0) * 0.25,
            h_max=float(config.sizing.h_max) if config.sizing.h_max is not None else float(h0) * 4.0,
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
        if res != 0:
            raise_native_status_error(dll, mesh_handle, "cfd_mesher_generate_hexcore", res)

        actual_n_cells = dll.cfd_mesher_get_n_cells(mesh_handle)
        actual_n_faces = dll.cfd_mesher_get_n_faces(mesh_handle)
        actual_n_points = dll.cfd_mesher_get_n_points(mesh_handle)

        pts_out = _copy_c_array(dll.cfd_mesher_get_points_ptr(mesh_handle), (actual_n_points, 3))
        soa = _extract_native_soa_fields(dll, mesh_handle, actual_n_cells, actual_n_faces)
        cell_types = _copy_c_array(
            dll.cfd_mesher_get_cell_types_ptr(mesh_handle),
            (actual_n_cells,),
            np.full(actual_n_cells, CellType.HEXAHEDRON.value, dtype=np.int32),
        )
        topological_dim = dll.cfd_mesher_get_topological_dim(mesh_handle)

        fo, fn, co, cn = _copy_csr_buffers(dll, mesh_handle, actual_n_faces, actual_n_cells)
        boundaries, baffles = _extract_native_patches(dll, mesh_handle, soa["neighbour"], actual_n_faces)
        wsrd_weights, wsrd_redist_map, error_map = _build_wsrd_and_error_map(
            soa, actual_n_cells, actual_n_faces, config
        )

        mesh = UnstructuredMesh(
            points=pts_out,
            cells=[],
            owner=soa["owner"],
            neighbour=soa["neighbour"],
            face_areas=soa["face_areas"],
            face_centres=soa["face_centres"],
            cell_centres=soa["cell_centres"],
            cell_volumes=soa["cell_volumes"],
            delta_f=soa["delta_f"],
            k_f=soa["k_f"],
            boundaries=boundaries,
            cell_regions=soa["cell_regions"],
            baffles=baffles,
            error_map=error_map,
            faces=None,
            cell_types=cell_types,
            topological_dim=topological_dim,
            metadata={
                "engine": "native_cpp20_hexcore",
                "wsrd_weights": wsrd_weights,
                "wsrd_redistribution": wsrd_redist_map,
                "rcm_reordered": bool(config is None or config.rcm_reorder),
            },
            face_offsets=fo,
            face_nodes=fn,
            cell_offsets=co,
            cell_nodes=cn,
        )
        if len(np.unique(soa["cell_regions"])) > 1:
            mesh = extract_cht_twin_faces(mesh)
        return mesh
    finally:
        dll.cfd_mesher_free(mesh_handle)


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
    """Build UnstructuredMesh in Structure of Arrays (SoA) layout."""
    is_strict = strict_native or _env_strict_native()
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
        except (ValueError, MemoryError):
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
        raise RuntimeError(f"Strict native execution requested but native library is unavailable: {get_native_load_error()}")

    if mesh is None:
        if is_strict and use_native:
            raise RuntimeError(f"Strict native execution requested but native library is unavailable: {get_native_load_error()}")
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


def rcm_reorder(mesh: UnstructuredMesh) -> Tuple[UnstructuredMesh, np.ndarray]:
    """Forwarding wrapper for reverse Cuthill-McKee reordering."""
    from backend.meshing.distmesh_core import rcm_reorder as _rcm
    return _rcm(mesh)


def hilbert_sfc_reorder(mesh: UnstructuredMesh) -> Tuple[UnstructuredMesh, np.ndarray]:
    """Forwarding wrapper for 3D Hilbert/Morton space-filling curve reordering."""
    from backend.meshing.distmesh_core import hilbert_sfc_reorder as _sfc
    return _sfc(mesh)
