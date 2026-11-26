"""Zone decomposition, closed-shell winding normalization, farfield bounds, and Lp metrics."""

from __future__ import annotations

import ctypes
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.meshing.models import CurvatureSizingConfig, FarfieldPadding, MeshingConfig
from backend.meshing.native_loader import get_native_dll


def default_config_for_h0(h0: float, domain_mode: str = "external_fluid") -> MeshingConfig:
    """Create a scale-invariant default MeshingConfig relative to h0 (AUD-009)."""
    h0_f = float(h0)
    return MeshingConfig(
        domain_mode=domain_mode,  # type: ignore[arg-type]
        sizing=CurvatureSizingConfig(
            h0=h0_f,
            h_min=h0_f * 0.25,
            h_max=h0_f * 4.0,
        ),
    )


def normalize_closed_shell_orientations(
    vertices: np.ndarray,
    faces: np.ndarray,
) -> Tuple[np.ndarray, Dict[str, Any]]:
    """Normalize winding of closed triangulated shell components to outward orientation (AUD-010).

    Connected components are built over triangles sharing an undirected edge.
    A component is closed when every undirected edge incident to its triangles
    appears an even number of times (typically 2). Closed components with negative
    signed volume relative to their vertex centroid have their triangle winding
    flipped [i0, i2, i1]. Open components are left untouched and their open edge
    count is reported in metadata['surface_open_edges'].
    """
    verts = np.asarray(vertices, dtype=np.float64)
    faces_out = np.asarray(faces, dtype=np.int32).copy()
    n_faces = len(faces_out)
    if n_faces == 0 or len(verts) == 0:
        return faces_out, {
            "num_components": 0,
            "closed_components": 0,
            "open_components": 0,
            "flipped_components": 0,
            "flipped_faces": 0,
            "surface_open_edges": 0,
        }

    edge_to_tris: Dict[Tuple[int, int], List[int]] = {}
    tri_edges: List[Tuple[Tuple[int, int], Tuple[int, int], Tuple[int, int]]] = []
    for f_idx in range(n_faces):
        a = int(faces_out[f_idx, 0])
        b = int(faces_out[f_idx, 1])
        c = int(faces_out[f_idx, 2])
        e0 = (a, b) if a < b else (b, a)
        e1 = (b, c) if b < c else (c, b)
        e2 = (c, a) if c < a else (a, c)
        tri_edges.append((e0, e1, e2))
        edge_to_tris.setdefault(e0, []).append(f_idx)
        edge_to_tris.setdefault(e1, []).append(f_idx)
        edge_to_tris.setdefault(e2, []).append(f_idx)

    visited = np.zeros(n_faces, dtype=bool)
    comps: List[np.ndarray] = []
    for seed in range(n_faces):
        if visited[seed]:
            continue
        visited[seed] = True
        queue = [seed]
        q_head = 0
        while q_head < len(queue):
            cur = queue[q_head]
            q_head += 1
            for e in tri_edges[cur]:
                for nbr in edge_to_tris[e]:
                    if not visited[nbr]:
                        visited[nbr] = True
                        queue.append(nbr)
        comps.append(np.asarray(queue, dtype=np.int64))

    closed_components = 0
    open_components = 0
    flipped_components = 0
    flipped_faces = 0
    total_open_edges = 0

    for comp_idx in comps:
        comp_edges = set()
        for f_idx in comp_idx:
            e0, e1, e2 = tri_edges[int(f_idx)]
            comp_edges.add(e0)
            comp_edges.add(e1)
            comp_edges.add(e2)

        comp_open_edges = sum(1 for e in comp_edges if (len(edge_to_tris[e]) % 2) != 0)
        if comp_open_edges == 0:
            closed_components += 1
            comp_tris = faces_out[comp_idx]
            comp_verts = np.unique(comp_tris.ravel())
            centroid = np.mean(verts[comp_verts], axis=0)
            v0 = verts[comp_tris[:, 0]] - centroid
            v1 = verts[comp_tris[:, 1]] - centroid
            v2 = verts[comp_tris[:, 2]] - centroid
            signed_vol = float(np.sum(np.einsum("ij,ij->i", v0, np.cross(v1, v2))) / 6.0)
            if signed_vol < 0.0:
                faces_out[comp_idx] = comp_tris[:, [0, 2, 1]]
                flipped_components += 1
                flipped_faces += len(comp_idx)
        else:
            open_components += 1
            total_open_edges += comp_open_edges

    return faces_out, {
        "num_components": len(comps),
        "closed_components": closed_components,
        "open_components": open_components,
        "flipped_components": flipped_components,
        "flipped_faces": flipped_faces,
        "surface_open_edges": total_open_edges,
    }


def _connected_face_components(
    faces: np.ndarray,
    subset_indices: Optional[np.ndarray] = None,
) -> List[np.ndarray]:
    """Find connected surface face components via shared vertex indices."""
    if subset_indices is None:
        subset_indices = np.arange(len(faces), dtype=np.int64)
    n_sub = len(subset_indices)
    if n_sub == 0:
        return []
    vert_to_local: Dict[int, List[int]] = {}
    for l_idx, g_idx in enumerate(subset_indices):
        for v in faces[g_idx]:
            vert_to_local.setdefault(int(v), []).append(l_idx)
    visited = np.zeros(n_sub, dtype=bool)
    comps: List[np.ndarray] = []
    for seed in range(n_sub):
        if visited[seed]:
            continue
        visited[seed] = True
        queue = [seed]
        q_head = 0
        while q_head < len(queue):
            cur = queue[q_head]
            q_head += 1
            for v in faces[subset_indices[cur]]:
                for nbr in vert_to_local.get(int(v), ()):
                    if not visited[nbr]:
                        visited[nbr] = True
                        queue.append(nbr)
        comps.append(subset_indices[np.asarray(queue, dtype=np.int64)])
    return comps


def _signed_shell_volume(verts: np.ndarray, tri_faces: np.ndarray) -> float:
    """Compute signed volume of a triangulated shell relative to its centroid."""
    if len(tri_faces) == 0:
        return 0.0
    v0 = verts[tri_faces[:, 0]]
    v1 = verts[tri_faces[:, 1]]
    v2 = verts[tri_faces[:, 2]]
    ref = v0.mean(axis=0)
    return float(np.sum(np.einsum("ij,ij->i", v0 - ref, np.cross(v1 - ref, v2 - ref))) / 6.0)


def _generalized_winding_number(
    verts: np.ndarray, tri_faces: np.ndarray, query_pts: np.ndarray
) -> np.ndarray:
    """Evaluate exact solid-angle generalized winding number w(x) in R^3 (Jacobson et al.)."""
    pts = np.ascontiguousarray(query_pts, dtype=np.float64)
    if len(pts) == 0 or len(tri_faces) == 0:
        return np.zeros(len(pts), dtype=np.float64)

    f_oriented = np.ascontiguousarray(tri_faces, dtype=np.int32)
    if _signed_shell_volume(verts, f_oriented) < 0.0:
        f_oriented = np.ascontiguousarray(f_oriented[:, [0, 2, 1]], dtype=np.int32)

    dll = get_native_dll(strict_native=False)
    if dll is not None and hasattr(dll, "cfd_sdf_create") and hasattr(dll, "cfd_sdf_winding_number"):
        dll.cfd_sdf_create.restype = ctypes.c_void_p
        dll.cfd_sdf_create.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_int64,
        ]
        dll.cfd_sdf_free.restype = None
        dll.cfd_sdf_free.argtypes = [ctypes.c_void_p]
        dll.cfd_sdf_winding_number.restype = ctypes.c_int32
        dll.cfd_sdf_winding_number.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
        ]
        v_c = np.ascontiguousarray(verts, dtype=np.float64)
        eng = dll.cfd_sdf_create(
            v_c.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(v_c),
            f_oriented.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            len(f_oriented),
        )
        if eng:
            try:
                out_wn = np.zeros(len(pts), dtype=np.float64)
                rc = dll.cfd_sdf_winding_number(
                    eng,
                    pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    len(pts),
                    out_wn.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                )
                if rc == 0:
                    return out_wn
            finally:
                dll.cfd_sdf_free(eng)

    v0 = verts[f_oriented[:, 0]]
    v1 = verts[f_oriented[:, 1]]
    v2 = verts[f_oriented[:, 2]]
    wn = np.zeros(len(pts), dtype=np.float64)
    batch = max(1, 200_000 // max(len(f_oriented), 1))
    for i0 in range(0, len(pts), batch):
        q = pts[i0 : i0 + batch, None, :]
        a = v0[None, :, :] - q
        b = v1[None, :, :] - q
        c = v2[None, :, :] - q
        la = np.linalg.norm(a, axis=-1)
        lb = np.linalg.norm(b, axis=-1)
        lc = np.linalg.norm(c, axis=-1)
        num = np.einsum("pmi,pmi->pm", a, np.cross(b, c))
        den = (
            la * lb * lc
            + np.einsum("pmi,pmi->pm", a, b) * lc
            + np.einsum("pmi,pmi->pm", b, c) * la
            + np.einsum("pmi,pmi->pm", c, a) * lb
        )
        wn[i0 : i0 + batch] = np.sum(2.0 * np.arctan2(num, den), axis=1) / (4.0 * np.pi)
    return wn


def _decompose_cht_shell_zones(surface: SurfaceMesh) -> List[Tuple[str, np.ndarray]]:
    """Decompose a CHT shell assembly into outer solid and nested cavities/inserts via Winding Number."""
    verts = np.asarray(surface.vertices, dtype=np.float64)
    faces = np.asarray(surface.faces, dtype=np.int64)
    if len(faces) == 0 or len(verts) == 0:
        return []
    comps = _connected_face_components(faces)
    if len(comps) < 2:
        return [("solid_0", np.arange(len(faces), dtype=np.int64))]

    n_c = len(comps)
    sample_pts = np.array([verts[faces[c[0]]].mean(axis=0) for c in comps], dtype=np.float64)
    abs_vols = [abs(_signed_shell_volume(verts, faces[c])) for c in comps]

    depth = np.zeros(n_c, dtype=np.int32)
    for i in range(n_c):
        if len(comps[i]) < 4:
            continue
        w = np.abs(_generalized_winding_number(verts, faces[comps[i]], sample_pts))
        for j in range(n_c):
            if i != j and abs_vols[i] > abs_vols[j] and w[j] > 0.5:
                depth[j] += 1

    root_candidates = [i for i in range(n_c) if depth[i] == 0]
    solid_idx = max(root_candidates, key=lambda idx: abs_vols[idx]) if root_candidates else 0
    solid_faces = comps[solid_idx]
    solid_verts = verts[np.unique(faces[solid_faces].ravel())]
    bmin = solid_verts.min(axis=0)
    bmax = solid_verts.max(axis=0)
    bspan = np.maximum(bmax - bmin, 1e-12)

    zones: List[Tuple[str, np.ndarray]] = [(f"solid_{solid_idx}", solid_faces)]
    chamber_count = 0
    duct_count = 0
    nested_solid_count = 1

    other_indices = [i for i in range(n_c) if i != solid_idx]
    other_indices.sort(key=lambda idx: (int(depth[idx]), -abs_vols[idx]))

    for c_idx in other_indices:
        comp_f_idx = comps[c_idx]
        if len(comp_f_idx) < 4:
            continue
        if depth[c_idx] >= 2 and (depth[c_idx] % 2 == 0):
            zones.append((f"solid_insert_{nested_solid_count}", comp_f_idx))
            nested_solid_count += 1
            continue

        comp_tris = faces[comp_f_idx]
        fc = verts[comp_tris].mean(axis=1)
        tol_vec = 1e-3 * bspan
        out_bbox_mask = np.any((fc < (bmin - tol_vec)) | (fc > (bmax + tol_vec)), axis=1)
        if not np.any(out_bbox_mask):
            zones.append((f"chamber_{chamber_count}", comp_f_idx))
            chamber_count += 1
        else:
            zones.append((f"duct_{duct_count}", comp_f_idx))
            duct_count += 1

    return zones


def compute_farfield_bounding_box(
    body_bbox: Union[Any, Tuple[Tuple[float, float, float], Tuple[float, float, float]]],
    upstream: float = 5.0,
    downstream: float = 10.0,
    lateral_y: float = 5.0,
    lateral_z: float = 5.0,
    ground_plane: bool = False,
) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
    """Compute standard aerodynamic farfield domain extents from body bounding box."""
    if hasattr(body_bbox, "min_pt") and hasattr(body_bbox, "max_pt"):
        b_min = np.asarray(body_bbox.min_pt, dtype=np.float64)
        b_max = np.asarray(body_bbox.max_pt, dtype=np.float64)
    else:
        b_min = np.asarray(body_bbox[0], dtype=np.float64)
        b_max = np.asarray(body_bbox[1], dtype=np.float64)
    span = b_max - b_min
    if np.any(span < 0):
        raise ValueError(f"Inverted body bounding box: min={b_min}, max={b_max}")
    if span[2] < 1e-12:
        raise ValueError("Planar geometry provided to 3D farfield mesher; use 2D mesher instead.")

    l_ref = float(span[0]) if span[0] > 1e-6 else float(np.max(span))

    ff_min = np.array([
        b_min[0] - upstream * l_ref,
        b_min[1] - lateral_y * l_ref,
        b_min[2] if ground_plane else b_min[2] - lateral_z * l_ref,
    ], dtype=np.float64)

    ff_max = np.array([
        b_max[0] + downstream * l_ref,
        b_max[1] + lateral_y * l_ref,
        b_max[2] + lateral_z * l_ref,
    ], dtype=np.float64)

    return (
        (float(ff_min[0]), float(ff_min[1]), float(ff_min[2])),
        (float(ff_max[0]), float(ff_max[1]), float(ff_max[2])),
    )


def compute_farfield_bounds_from_config(
    geom_bmin: np.ndarray,
    geom_bmax: np.ndarray,
    config: MeshingConfig,
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute the external farfield bounding box [ff_min, ff_max] honoring config.farfield_bbox."""
    span = geom_bmax - geom_bmin
    if np.any(span <= 0.0):
        raise ValueError(f"Invalid geometry bounds: span must be positive, got {span}")

    if config.farfield_bbox is not None:
        raw = config.farfield_bbox
        if hasattr(raw, "min_pt") and hasattr(raw, "max_pt"):
            ff_min = np.asarray(raw.min_pt, dtype=np.float64)
            ff_max = np.asarray(raw.max_pt, dtype=np.float64)
        elif len(raw) == 2:
            ff_min = np.asarray(raw[0], dtype=np.float64)
            ff_max = np.asarray(raw[1], dtype=np.float64)
        elif len(raw) == 6:
            ff_min = np.asarray(raw[:3], dtype=np.float64)
            ff_max = np.asarray(raw[3:], dtype=np.float64)
        else:
            raise ValueError(f"Unrecognized farfield_bbox format: {raw}")

        if np.any(ff_min >= ff_max):
            raise ValueError(
                f"Inverted farfield_bbox: min bounds {ff_min} must be strictly less than max bounds {ff_max}"
            )
        if np.any(ff_min > geom_bmin) or np.any(ff_max < geom_bmax):
            raise ValueError("Geometry penetrates farfield boundary box; increase farfield_bbox.")
        return ff_min, ff_max

    l_ref = float(span[0]) if span[0] > 1e-6 else float(np.max(span))
    pad = config.farfield_padding if config.farfield_padding is not None else FarfieldPadding()
    ff_min = np.array([
        geom_bmin[0] - float(pad.upstream) * l_ref,
        geom_bmin[1] - float(pad.lateral_y) * l_ref,
        geom_bmin[2] - float(pad.lateral_z) * l_ref,
    ], dtype=np.float64)
    ff_max = np.array([
        geom_bmax[0] + float(pad.downstream) * l_ref,
        geom_bmax[1] + float(pad.lateral_y) * l_ref,
        geom_bmax[2] + float(pad.lateral_z) * l_ref,
    ], dtype=np.float64)
    return ff_min, ff_max


def compute_lp_metric_tensor(
    H_u: np.ndarray,
    target_cell_count: int,
    p: int = 2,
    cell_volumes: Optional[np.ndarray] = None,
    h_min: Optional[float] = None,
    h_max: Optional[float] = None,
) -> np.ndarray:
    """Evaluate optimal L^p Riemannian metric tensor field M_{L^p}(x) (Paper 3 Thm. 2.1)."""
    H_u = np.asarray(H_u, dtype=np.float64)
    is_single = (H_u.ndim == 2)
    if is_single:
        H_u = H_u.reshape(1, 3, 3)

    N_cells = len(H_u)
    if target_cell_count <= 0:
        raise ValueError(f"target_cell_count must be positive, got {target_cell_count}")
    if p not in (1, 2):
        raise ValueError(f"Supported L^p exponents are p=1 and p=2, got p={p}")

    vols = np.asarray(cell_volumes, dtype=np.float64) if cell_volumes is not None else np.ones(N_cells, dtype=np.float64)
    if len(vols) != N_cells:
        raise ValueError(f"cell_volumes length {len(vols)} does not match Hessian count {N_cells}")

    det_exp_point = -1.0 / (2.0 * p + 3.0)
    det_exp_integral = float(p) / (2.0 * p + 3.0)

    abs_H = np.zeros_like(H_u)
    dets = np.zeros(N_cells, dtype=np.float64)
    eps_reg = 1e-14

    for i in range(N_cells):
        H_sym = 0.5 * (H_u[i] + H_u[i].T)
        eigvals, eigvecs = np.linalg.eigh(H_sym)
        abs_eigvals = np.abs(eigvals)
        abs_H[i] = eigvecs @ np.diag(abs_eigvals) @ eigvecs.T
        dets[i] = max(float(np.prod(abs_eigvals)), eps_reg)

    integral_val = float(np.sum(vols * np.power(dets, det_exp_integral)))
    if integral_val <= 0.0 or not np.isfinite(integral_val):
        bg_h = h_max if h_max is not None and h_max > 0.0 else 1.0
        bg_M = (1.0 / (bg_h ** 2)) * np.eye(3, dtype=np.float64)
        result = np.tile(bg_M, (N_cells, 1, 1))
        return result[0] if is_single else result

    D_Lp = (float(target_cell_count) ** (2.0 / 3.0)) * (integral_val ** (-2.0 / 3.0))
    M_out = np.zeros_like(abs_H)
    for i in range(N_cells):
        M_cell = D_Lp * (dets[i] ** det_exp_point) * abs_H[i]
        M_sym = 0.5 * (M_cell + M_cell.T)
        eigvals, eigvecs = np.linalg.eigh(M_sym)
        min_ev = 1.0 / (h_max ** 2) if (h_max is not None and h_max > 0.0) else 1e-8
        max_ev = 1.0 / (h_min ** 2) if (h_min is not None and h_min > 0.0) else 1e8
        M_out[i] = eigvecs @ np.diag(np.clip(eigvals, min_ev, max_ev)) @ eigvecs.T

    return M_out[0] if is_single else M_out


def reconstruct_p2_edge_midpoints(
    node_coords: np.ndarray,
    node_values: np.ndarray,
    edges: np.ndarray,
    nodal_gradients: np.ndarray,
) -> np.ndarray:
    """Noise-free P2 edge midpoint value reconstruction (Paper 3 §3.1)."""
    coords = np.asarray(node_coords, dtype=np.float64)
    vals = np.asarray(node_values, dtype=np.float64)
    edg = np.asarray(edges, dtype=np.int64)
    grads = np.asarray(nodal_gradients, dtype=np.float64)

    N_edges = len(edg)
    mid_vals = np.zeros(N_edges, dtype=np.float64)
    for e_idx in range(N_edges):
        p, q = edg[e_idx]
        dx = coords[q] - coords[p]
        mid_vals[e_idx] = 0.5 * (vals[p] + vals[q]) + (1.0 / 8.0) * float(np.dot(grads[p] - grads[q], dx))
    return mid_vals
