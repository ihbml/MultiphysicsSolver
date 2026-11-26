"""Conformal 3D Boundary Layer Generation via Strategy C (Blueprint §1.1).

Implements StrandSet, bl_prep_thickness_field, solve_strand_growth_rates,
generate_conformal_bl_strategy_c (Snippet 25 prism Jacobian validity loop & LDU
conformal stitching), and generate_offset_bl_mesh.
"""

from __future__ import annotations

from dataclasses import dataclass
import heapq
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple, Union

import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.meshing._engine_zones import normalize_closed_shell_orientations
from backend.meshing._models_quality import compute_prism_jacobian_bound
from backend.meshing.geometry_sdf import CADMeshSDF
from backend.meshing.invariants import recompute_geometry_from_nodes
from backend.meshing.models import CellType, MeshingConfig, UnstructuredMesh


@dataclass
class StrandSet:
    """Wall-to-cap strand descriptors for conformal Strategy C boundary layers."""

    cap_vertex: np.ndarray    # int64 (M,), vertex IDs on cap psi = 0
    foot: np.ndarray          # float64 (M, 3), projected wall footpoints on phi = 0
    growth: np.ndarray        # float64 (M,), solved local growth rate g_v
    scale: np.ndarray         # float32 (M,), bl_scale = L_v / delta_target in (0, 1]
    smooth_patch: np.ndarray  # int32 (M,), smooth surface patch ID per strand
    n_layers: int

    def __post_init__(self) -> None:
        self.cap_vertex = np.asarray(self.cap_vertex, dtype=np.int64).ravel()
        self.foot = np.asarray(self.foot, dtype=np.float64).reshape(-1, 3)
        self.growth = np.asarray(self.growth, dtype=np.float64).ravel()
        self.scale = np.asarray(self.scale, dtype=np.float32).ravel()
        self.smooth_patch = np.asarray(self.smooth_patch, dtype=np.int32).ravel()
        self.n_layers = int(self.n_layers)


def _target_thickness(y1: float, growth_rate: float, n_layers: int) -> float:
    n = int(n_layers)
    if n <= 0:
        raise ValueError(f"n_layers must be positive, got {n_layers}")
    if y1 <= 0.0:
        raise ValueError(f"y1 must be positive, got {y1}")
    return float(n * y1) if abs(growth_rate - 1.0) < 1e-9 else float(y1 * (growth_rate ** n - 1.0) / (growth_rate - 1.0))


def _eval_sdf(sdf_fn: Any, points: np.ndarray, fd_eps: float = 1e-6) -> Tuple[np.ndarray, np.ndarray]:
    """Evaluate (phi, grad_phi) for any BaseSDF, CADMeshSDF, or Python callable."""
    pts = np.atleast_2d(np.asarray(points, dtype=np.float64))
    if hasattr(sdf_fn, "evaluate"):
        res = sdf_fn.evaluate(pts)
        return np.atleast_1d(np.asarray(res[0], dtype=np.float64)).ravel(), np.atleast_2d(np.asarray(res[1], dtype=np.float64)).reshape(len(pts), 3)

    def _call_phi(p: np.ndarray) -> Tuple[np.ndarray, Optional[np.ndarray]]:
        if hasattr(sdf_fn, "distance"):
            return np.atleast_1d(np.asarray(sdf_fn.distance(p), dtype=np.float64)).ravel(), None
        out = sdf_fn(p)
        if isinstance(out, tuple) and len(out) == 2:
            return np.atleast_1d(np.asarray(out[0], dtype=np.float64)).ravel(), np.atleast_2d(np.asarray(out[1], dtype=np.float64)).reshape(len(p), 3)
        return np.atleast_1d(np.asarray(out, dtype=np.float64)).ravel(), None

    phi, grad = _call_phi(pts)
    if grad is None:
        grad = np.zeros((len(pts), 3), dtype=np.float64)
        for ax in range(3):
            p_plus, p_minus = pts.copy(), pts.copy()
            p_plus[:, ax] += fd_eps
            p_minus[:, ax] -= fd_eps
            grad[:, ax] = (_call_phi(p_plus)[0] - _call_phi(p_minus)[0]) / (2.0 * fd_eps)
    return phi, grad


def _compute_vertex_normals(vertices: np.ndarray, faces: Sequence[Sequence[int]]) -> np.ndarray:
    """Compute area-weighted unit vertex normals from surface faces."""
    v_normals = np.zeros_like(vertices, dtype=np.float64)
    for f in faces:
        if len(f) < 3:
            continue
        fn = np.cross(vertices[int(f[1])] - vertices[int(f[0])], vertices[int(f[2])] - vertices[int(f[0])])
        for vi in f:
            v_normals[int(vi)] += fn
    norms = np.linalg.norm(v_normals, axis=1, keepdims=True)
    return v_normals / np.where(norms > 1e-15, norms, 1.0)


def bl_prep_thickness_field(
    vertices: np.ndarray,
    faces: Sequence[Sequence[int]],
    sdf_fn: Any,
    n_layers: int,
    y1: float,
    growth_rate: float,
    eta: float = 0.4,
    max_grad: float = 0.3,
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute reach-limited, Lipschitz-smooth BL thickness field on wall vertices."""
    pts = np.asarray(vertices, dtype=np.float64)
    n_verts = len(pts)
    if n_verts == 0:
        return np.empty(0, dtype=np.float64), np.empty(0, dtype=np.float64)

    delta_soll = _target_thickness(y1, growth_rate, n_layers)
    eta_val = max(float(eta), 1e-6)
    t_max, eps = delta_soll / eta_val, 0.1

    _, grad = _eval_sdf(sdf_fn, pts)
    g_norm = np.linalg.norm(grad, axis=1, keepdims=True)
    v_normals = _compute_vertex_normals(pts, faces)
    normals = np.where(g_norm > 1e-12, grad / np.maximum(g_norm, 1e-12), v_normals)
    n_len = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.where(n_len > 1e-12, normals / np.maximum(n_len, 1e-12), np.array([[0.0, 0.0, 1.0]]))

    phi_max, _ = _eval_sdf(sdf_fn, pts + t_max * normals)
    ok_max = phi_max >= (1.0 - eps) * t_max
    t_reach = np.where(ok_max, t_max, 0.0)
    if not np.all(ok_max):
        idx_bis = np.where(~ok_max)[0]
        lo, hi = np.zeros(len(idx_bis), dtype=np.float64), np.full(len(idx_bis), t_max, dtype=np.float64)
        p_sub, n_sub = pts[idx_bis], normals[idx_bis]
        for _ in range(8):
            mid = 0.5 * (lo + hi)
            phi_mid, _ = _eval_sdf(sdf_fn, p_sub + mid[:, None] * n_sub)
            valid = phi_mid >= (1.0 - eps) * mid
            lo, hi = np.where(valid, mid, lo), np.where(valid, hi, mid)
        t_reach[idx_bis] = lo

    delta = np.maximum(np.minimum(delta_soll, eta_val * t_reach), 0.0)
    adj: List[List[Tuple[int, float]]] = [[] for _ in range(n_verts)]
    seen_edges: Set[Tuple[int, int]] = set()
    for f in faces:
        m = len(f)
        for i in range(m):
            u, v = int(f[i]), int(f[(i + 1) % m])
            if u == v:
                continue
            key = (u, v) if u < v else (v, u)
            if key not in seen_edges:
                seen_edges.add(key)
                cap = float(max_grad) * float(np.linalg.norm(pts[u] - pts[v]))
                adj[u].append((v, cap))
                adj[v].append((u, cap))

    pq: List[Tuple[float, int]] = [(float(delta[i]), i) for i in range(n_verts)]
    heapq.heapify(pq)
    while pq:
        d_u, u = heapq.heappop(pq)
        if d_u > delta[u] + 1e-15:
            continue
        for v, cap in adj[u]:
            cand = d_u + cap
            if cand < delta[v] - 1e-15:
                delta[v] = cand
                heapq.heappush(pq, (cand, v))
    return delta, t_reach


def solve_strand_growth_rates(
    L_v: Union[float, np.ndarray], y1: float, n_layers: int, delta_target: float
) -> Tuple[Union[float, np.ndarray], Union[float, np.ndarray], Union[float, np.ndarray]]:
    """Solve y1 * (g^N - 1) / (g - 1) = L_v per strand via bisection (BL-2)."""
    is_scalar = np.ndim(L_v) == 0
    lengths = np.atleast_1d(np.asarray(L_v, dtype=np.float64)).ravel()
    n, y1_val, d_targ = int(n_layers), float(y1), max(float(delta_target), 1e-15)
    if n <= 0:
        raise ValueError(f"n_layers must be positive, got {n_layers}")

    growth = np.ones(len(lengths), dtype=np.float64)
    y1_eff = np.full(len(lengths), y1_val, dtype=np.float64)
    bl_scale = np.clip(lengths / d_targ, 0.0, 1.0).astype(np.float32)

    if n == 1:
        y1_eff = np.maximum(lengths, 0.0)
    else:
        short_mask = lengths <= (n * y1_val)
        y1_eff[short_mask] = np.maximum(lengths[short_mask], 0.0) / float(n)
        for idx in np.where(~short_mask)[0]:
            target, lo, hi = float(lengths[idx]), 1.0, 2.0
            while y1_val * (hi ** n - 1.0) / (hi - 1.0) < target and hi < 1e6:
                hi *= 2.0
            for _ in range(60):
                mid = 0.5 * (lo + hi)
                s_val = y1_val * float(n) if (mid - 1.0 < 1e-12) else y1_val * (mid ** n - 1.0) / (mid - 1.0)
                if s_val < target:
                    lo = mid
                else:
                    hi = mid
            growth[idx] = 0.5 * (lo + hi)

    return (float(growth[0]), float(y1_eff[0]), float(bl_scale[0])) if is_scalar else (growth, y1_eff, bl_scale)


def _extract_mesh_faces(mesh: UnstructuredMesh) -> List[np.ndarray]:
    if mesh.face_offsets is not None and mesh.face_nodes is not None and len(mesh.face_offsets) == mesh.num_faces + 1:
        fo, fn = np.asarray(mesh.face_offsets, dtype=np.int64), np.asarray(mesh.face_nodes, dtype=np.int64)
        return [fn[int(fo[i]) : int(fo[i + 1])] for i in range(mesh.num_faces)]
    if mesh.faces is None or len(mesh.faces) != mesh.num_faces:
        raise ValueError("core_mesh lacks face connectivity.")
    return [np.asarray(f, dtype=np.int64) for f in mesh.faces]


def _build_tangent_basis(normals_list: List[np.ndarray]) -> np.ndarray:
    basis: List[np.ndarray] = []
    for n in normals_list:
        v = np.asarray(n, dtype=np.float64).copy()
        for q in basis:
            v -= float(np.dot(v, q)) * q
        nv = float(np.linalg.norm(v))
        if nv > 1e-6:
            basis.append(v / nv)
            if len(basis) == 3:
                break
    return np.column_stack(basis) if basis else np.zeros((3, 0), dtype=np.float64)


def _project_to_sdf_constrained(
    pts: np.ndarray, sdf_fn: Any, constraint_bases: Dict[int, np.ndarray], max_iter: int = 6
) -> np.ndarray:
    out = np.array(pts, dtype=np.float64, copy=True)
    for _ in range(max_iter):
        phi, grad = _eval_sdf(sdf_fn, out)
        if np.max(np.abs(phi)) < 1e-12:
            break
        for i in range(len(out)):
            g = grad[i] - (constraint_bases[i] @ (constraint_bases[i].T @ grad[i])) if i in constraint_bases else grad[i]
            denom = float(np.dot(g, g))
            if denom > 1e-16:
                out[i] -= (float(phi[i]) / denom) * g
    return out


def _strand_layer_positions(
    foot: np.ndarray, cap: np.ndarray, growth: np.ndarray, y1_eff: np.ndarray, n_layers: int
) -> np.ndarray:
    coords = np.zeros((len(foot), n_layers, 3), dtype=np.float64)
    coords[:, 0, :] = foot
    if n_layers == 1:
        return coords
    diff = cap - foot
    lengths = np.linalg.norm(diff, axis=1)
    dirs = diff / np.where(lengths > 1e-15, lengths, 1.0)[:, None]
    cum_h = np.zeros((len(foot), n_layers), dtype=np.float64)
    for k in range(1, n_layers):
        cum_h[:, k] = cum_h[:, k - 1] + y1_eff * (growth ** (k - 1))
        coords[:, k, :] = foot + np.where(lengths > 1e-15, np.minimum(cum_h[:, k], lengths), 0.0)[:, None] * dirs
    return coords


def _check_bl_stack_validity(
    all_pts: np.ndarray, cap_faces: List[np.ndarray], cap_to_strand: Dict[int, int], n_layers: int, base_off: int
) -> Tuple[bool, Set[int]]:
    bad_strands: Set[int] = set()
    for f_nodes in cap_faces:
        m = len(f_nodes)
        s_ids = [cap_to_strand[int(v)] for v in f_nodes]
        for k in range(n_layers):
            bot = [base_off + s * n_layers + k for s in s_ids]
            top = [int(f_nodes[idx]) if k == n_layers - 1 else (base_off + s_ids[idx] * n_layers + k + 1) for idx in range(m)]
            for j in range(1, m - 1):
                wedge = [bot[0], bot[j + 1], bot[j], top[0], top[j + 1], top[j]]
                bound = compute_prism_jacobian_bound(all_pts, wedge)
                p0, p1, p2, q0, q1, q2 = (all_pts[idx] for idx in wedge)
                if not np.isfinite(bound) or bound <= 1e-9 or any(
                    float(np.dot(b - a, np.cross(c - a, d - a))) <= 1e-18
                    for a, b, c, d in ((p0, p1, p2, q0), (p1, p2, q0, q1), (p2, q0, q1, q2))
                ):
                    bad_strands.update(s_ids)
                    break
            if bad_strands and s_ids[0] in bad_strands:
                break
    return len(bad_strands) == 0, bad_strands


def _compute_fvm_metrics(
    sf: np.ndarray, cf: np.ndarray, cell_cc: np.ndarray, owner: np.ndarray, neighbour: np.ndarray
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    n_faces = len(sf)
    delta_f, k_f = np.zeros((n_faces, 3), dtype=np.float64), np.zeros((n_faces, 3), dtype=np.float64)
    non_ortho, skewness = np.zeros(n_faces, dtype=np.float64), np.zeros(n_faces, dtype=np.float64)
    stab_mask = np.zeros(n_faces, dtype=np.int32)

    int_mask = neighbour >= 0
    if np.any(int_mask):
        idx_i = np.where(int_mask)[0]
        ow_i, nb_i, s_i = owner[idx_i], neighbour[idx_i], sf[idx_i]
        df = cell_cc[nb_i] - cell_cc[ow_i]
        sf_dot_df = np.einsum("ij,ij->i", s_i, df)
        valid = sf_dot_df > 1e-15
        delta = np.where(valid[:, None], df * (np.einsum("ij,ij->i", s_i, s_i) / np.where(valid, sf_dot_df, 1.0))[:, None], s_i)
        delta_f[idx_i], k_f[idx_i] = delta, np.where(valid[:, None], s_i - delta, 0.0)

        s_norm, df_norm = np.linalg.norm(s_i, axis=1), np.linalg.norm(df, axis=1)
        theta = np.degrees(np.arccos(np.clip(sf_dot_df / np.maximum(s_norm * df_norm, 1e-30), -1.0, 1.0)))
        non_ortho[idx_i] = np.where((s_norm > 1e-15) & (df_norm > 1e-15), theta, 0.0)

        nf = s_i / np.maximum(s_norm, 1e-30)[:, None]
        df_dot_nf = np.einsum("ij,ij->i", df, nf)
        t_proj = np.einsum("ij,ij->i", cf[idx_i] - cell_cc[ow_i], nf) / np.where(np.abs(df_dot_nf) > 1e-15, df_dot_nf, 1.0)
        skew = np.linalg.norm(cf[idx_i] - (cell_cc[ow_i] + df * t_proj[:, None]), axis=1) / np.maximum(df_norm, 1e-30)
        skew = np.where((np.abs(df_dot_nf) > 1e-15) & (df_norm > 1e-15), skew, 0.0)
        skewness[idx_i] = skew

        mask_i = np.zeros(len(idx_i), dtype=np.int32)
        mask_i[(theta >= 40.0) | (skew >= 0.4)] = 1
        mask_i[(theta >= 65.0) | (skew >= 0.7)] = 2
        mask_i[(theta >= 75.0) | (skew >= 1.0) | (~valid)] = 3
        stab_mask[idx_i] = mask_i

    bnd_mask = ~int_mask
    if np.any(bnd_mask):
        delta_f[bnd_mask] = cf[bnd_mask] - cell_cc[owner[bnd_mask]]
    return delta_f, k_f, non_ortho, skewness, stab_mask


def generate_conformal_bl_strategy_c(
    core_mesh: UnstructuredMesh,
    wall_sdf_fn: Any,
    n_layers: int,
    y1: float,
    growth_rate: float,
    wall_patch: str = "wall",
    farfield_patches: Tuple[str, ...] = ("inlet", "outlet", "sides", "top", "bottom"),
    sharp_trailing_edge_deg: float = 120.0,
) -> UnstructuredMesh:
    """Extrude conformal wall-ward BL stack from core_mesh[wall_patch] and stitch LDU-cleanly."""
    if n_layers <= 0:
        raise ValueError(f"n_layers must be positive, got {n_layers}")
    if wall_patch not in core_mesh.boundaries:
        raise ValueError(f"wall_patch '{wall_patch}' not in core_mesh.boundaries: {sorted(core_mesh.boundaries)}")
    w_start, w_count = core_mesh.boundaries[wall_patch]
    if w_count <= 0:
        raise ValueError(f"wall_patch '{wall_patch}' has zero faces.")

    core_faces = _extract_mesh_faces(core_mesh)
    core_pts = np.array(core_mesh.points, dtype=np.float64, copy=True)
    n_core_pts, n_core_cells = len(core_pts), core_mesh.num_cells
    cap_faces = [core_faces[fi] for fi in range(w_start, w_start + w_count)]
    cap_core_owners = [int(core_mesh.owner[fi]) for fi in range(w_start, w_start + w_count)]

    cap_verts_list: List[int] = []
    cap_to_strand: Dict[int, int] = {}
    for fn in cap_faces:
        for v in fn:
            vi = int(v)
            if vi not in cap_to_strand:
                cap_to_strand[vi] = len(cap_verts_list)
                cap_verts_list.append(vi)
    cap_verts = np.array(cap_verts_list, dtype=np.int64)
    n_strands = len(cap_verts)

    ff_set = set(farfield_patches)
    strand_ff_normals: Dict[int, List[np.ndarray]] = {}
    ff_edge_patch: Dict[Tuple[int, int], str] = {}
    for pname, (ps, pc) in core_mesh.boundaries.items():
        if pname == wall_patch or pname not in ff_set:
            continue
        for fi in range(ps, ps + pc):
            fn, sf_vec = core_faces[fi], core_mesh.face_areas[fi]
            sf_norm = float(np.linalg.norm(sf_vec))
            unit_n = sf_vec / sf_norm if sf_norm > 1e-15 else None
            for k in range(len(fn)):
                u, v = int(fn[k]), int(fn[(k + 1) % len(fn)])
                if u in cap_to_strand and v in cap_to_strand:
                    ff_edge_patch.setdefault((min(u, v), max(u, v)), pname)
                if unit_n is not None and u in cap_to_strand:
                    strand_ff_normals.setdefault(cap_to_strand[u], []).append(unit_n)

    constraint_bases = {s: _build_tangent_basis(nl) for s, nl in strand_ff_normals.items()}
    cap_edge_faces: Dict[Tuple[int, int], List[Tuple[int, int, int]]] = {}
    cap_face_normals = np.zeros((w_count, 3), dtype=np.float64)
    strand_adj: List[Set[int]] = [set() for _ in range(n_strands)]
    for i, fn in enumerate(cap_faces):
        n_vec = np.cross(core_pts[fn[1]] - core_pts[fn[0]], core_pts[fn[2]] - core_pts[fn[0]])
        if float(np.linalg.norm(n_vec)) > 1e-15:
            cap_face_normals[i] = n_vec / float(np.linalg.norm(n_vec))
        for k in range(len(fn)):
            u, v = int(fn[k]), int(fn[(k + 1) % len(fn)])
            su, sv = cap_to_strand[u], cap_to_strand[v]
            strand_adj[su].add(sv)
            strand_adj[sv].add(su)
            cap_edge_faces.setdefault((min(u, v), max(u, v)), []).append((i, u, v))

    trailing_edge_indices = [
        e_idx for e_idx, (_, ents) in enumerate(cap_edge_faces.items())
        if len(ents) == 2 and float(np.degrees(np.arccos(np.clip(np.dot(cap_face_normals[ents[0][0]], cap_face_normals[ents[1][0]]), -1.0, 1.0)))) > sharp_trailing_edge_deg
    ]

    cap_pos = core_pts[cap_verts].copy()
    foot = _project_to_sdf_constrained(cap_pos, wall_sdf_fn, constraint_bases)
    delta_target = _target_thickness(y1, growth_rate, n_layers)

    all_pts = np.zeros((n_core_pts + n_strands * n_layers, 3), dtype=np.float64)
    all_pts[:n_core_pts] = core_pts
    growth = np.ones(n_strands, dtype=np.float64)
    bl_scale = np.ones(n_strands, dtype=np.float32)

    for outer_it in range(3):
        valid, bad_strands = False, set()
        for inner_it in range(6):
            lengths = np.linalg.norm(cap_pos - foot, axis=1)
            growth, y1_eff, bl_scale = solve_strand_growth_rates(lengths, y1, n_layers, delta_target)
            all_pts[n_core_pts:] = _strand_layer_positions(foot, cap_pos, growth, y1_eff, n_layers).reshape(-1, 3)
            valid, bad_strands = _check_bl_stack_validity(all_pts, cap_faces, cap_to_strand, n_layers, n_core_pts)
            if valid or inner_it == 5:
                break
            relax_set = set(bad_strands)
            for s in list(bad_strands):
                relax_set.update(strand_adj[s])
            new_foot = foot.copy()
            _, g_wall = _eval_sdf(wall_sdf_fn, foot)
            for s in relax_set:
                if not strand_adj[s]:
                    continue
                disp = 0.5 * (np.mean(foot[list(strand_adj[s])], axis=0) - foot[s])
                nw = g_wall[s]
                if float(np.linalg.norm(nw)) > 1e-12:
                    nw = nw / float(np.linalg.norm(nw))
                    disp -= float(np.dot(disp, nw)) * nw
                if s in constraint_bases:
                    disp -= constraint_bases[s] @ (constraint_bases[s].T @ disp)
                new_foot[s] = foot[s] + disp
            foot = _project_to_sdf_constrained(new_foot, wall_sdf_fn, constraint_bases, max_iter=3)
        if valid or outer_it == 2:
            break
        for s in bad_strands:
            if s not in constraint_bases:
                cap_pos[s] = foot[s] + 0.7 * (cap_pos[s] - foot[s])
                all_pts[cap_verts[s]] = cap_pos[s]

    core_cells_list = list(core_mesh.cells) if core_mesh.cells is not None and len(core_mesh.cells) == n_core_cells else [[] for _ in range(n_core_cells)]
    core_ctypes = np.asarray(core_mesh.cell_types, dtype=np.int32) if core_mesh.cell_types is not None and len(core_mesh.cell_types) == n_core_cells else np.full(n_core_cells, CellType.POLYHEDRON.value, dtype=np.int32)
    core_regions = np.asarray(core_mesh.cell_regions, dtype=np.int32) if core_mesh.cell_regions is not None and len(core_mesh.cell_regions) == n_core_cells else np.zeros(n_core_cells, dtype=np.int32)

    def _bl_cell(fi_local: int, k_l: int) -> int:
        return n_core_cells + fi_local * n_layers + k_l

    def _nid(cap_v: int, k_l: int) -> int:
        return cap_v if k_l == n_layers else n_core_pts + cap_to_strand[cap_v] * n_layers + k_l

    bl_cells, bl_ctypes, bl_regions, bl_cell_scales = [], [], [], []
    for i, fn in enumerate(cap_faces):
        m_f, reg_i = len(fn), int(core_regions[cap_core_owners[i]])
        mean_scale = float(np.mean([bl_scale[cap_to_strand[int(v)]] for v in fn]))
        ctype = CellType.WEDGE.value if m_f == 3 else (CellType.HEXAHEDRON.value if m_f == 4 else CellType.POLYHEDRON.value)
        for k in range(n_layers):
            bl_cells.append(np.array([_nid(int(v), k) for v in fn] + [_nid(int(v), k + 1) for v in fn], dtype=np.int64))
            bl_ctypes.append(ctype)
            bl_regions.append(reg_i)
            bl_cell_scales.append(mean_scale)

    all_cells = core_cells_list + bl_cells
    n_total_cells = len(all_cells)
    all_ctypes = np.concatenate([core_ctypes, np.array(bl_ctypes, dtype=np.int32)])
    all_regions = np.concatenate([core_regions, np.array(bl_regions, dtype=np.int32)])

    int_entries: List[Tuple[int, int, np.ndarray]] = [
        (int(core_mesh.owner[f]), int(core_mesh.neighbour[f]), core_faces[f]) for f in range(core_mesh.num_internal_faces)
    ]
    for i, fn in enumerate(cap_faces):
        int_entries.append((cap_core_owners[i], _bl_cell(i, n_layers - 1), fn))
        rev_fn = fn[::-1]
        for k in range(n_layers - 1):
            int_entries.append((_bl_cell(i, k), _bl_cell(i, k + 1), np.array([_nid(int(v), k + 1) for v in rev_fn], dtype=np.int64)))

    patch_faces: Dict[str, List[Tuple[int, np.ndarray]]] = {
        pname: [(int(core_mesh.owner[fi]), core_faces[fi]) for fi in range(ps, ps + pc)]
        for pname, (ps, pc) in core_mesh.boundaries.items() if pname != wall_patch
    }
    wall_bnd = patch_faces.setdefault(wall_patch, [])
    for i, fn in enumerate(cap_faces):
        wall_bnd.append((_bl_cell(i, 0), np.array([_nid(int(v), 0) for v in fn], dtype=np.int64)))

    for ekey, entries in cap_edge_faces.items():
        if len(entries) == 2:
            (ia, ua, va), (ib, ub, vb) = entries[0], entries[1]
            if ia > ib:
                ia, ua, va, ib, ub, vb = ib, ub, vb, ia, ua, va
            for k in range(n_layers):
                int_entries.append((_bl_cell(ia, k), _bl_cell(ib, k), np.array([_nid(ua, k), _nid(ua, k + 1), _nid(va, k + 1), _nid(va, k)], dtype=np.int64)))
        else:
            plist = patch_faces.setdefault(ff_edge_patch.get(ekey, "sides"), [])
            for i_s, us, vs in entries:
                for k in range(n_layers):
                    plist.append((_bl_cell(i_s, k), np.array([_nid(us, k), _nid(us, k + 1), _nid(vs, k + 1), _nid(vs, k)], dtype=np.int64)))

    int_entries.sort(key=lambda item: (item[0], item[1]))
    all_face_list = [item[2] for item in int_entries]
    owner_list = [item[0] for item in int_entries]
    neighbour_list = [item[1] for item in int_entries]
    boundaries: Dict[str, Tuple[int, int]] = {}
    cursor = len(int_entries)
    for pname, plist in patch_faces.items():
        if not plist:
            continue
        boundaries[pname] = (cursor, len(plist))
        for ow, fn_arr in plist:
            owner_list.append(ow)
            neighbour_list.append(-1)
            all_face_list.append(fn_arr)
        cursor += len(plist)

    owner_arr, neighbour_arr = np.array(owner_list, dtype=np.int64), np.array(neighbour_list, dtype=np.int64)
    face_offsets = np.zeros(len(all_face_list) + 1, dtype=np.int64)
    np.cumsum([len(f) for f in all_face_list], out=face_offsets[1:])
    face_nodes = np.concatenate(all_face_list) if face_offsets[-1] > 0 else np.empty(0, dtype=np.int64)
    cell_offsets = np.zeros(n_total_cells + 1, dtype=np.int64)
    np.cumsum([len(c) for c in all_cells], out=cell_offsets[1:])
    cell_nodes = np.concatenate([np.asarray(c, dtype=np.int64) for c in all_cells]) if cell_offsets[-1] > 0 else np.empty(0, dtype=np.int64)

    sf, cf, cell_vol, cell_cc = recompute_geometry_from_nodes(all_pts, face_offsets, face_nodes, owner_arr, neighbour_arr, n_total_cells)
    delta_f, k_f, non_ortho, skewness, stab_mask = _compute_fvm_metrics(sf, cf, cell_cc, owner_arr, neighbour_arr)

    aspect_ratio = np.ones(n_total_cells, dtype=np.float64)
    for ci in range(n_total_cells):
        cn = cell_nodes[int(cell_offsets[ci]) : int(cell_offsets[ci + 1])]
        if len(cn) > 0:
            sp = np.max(all_pts[cn], axis=0) - np.min(all_pts[cn], axis=0)
            vsp = sp[sp > 1e-12]
            if len(vsp) >= 2:
                aspect_ratio[ci] = max(1.0, float(np.max(vsp) / max(float(np.min(vsp)), 1e-15)))

    bl_scale_cell = np.ones(n_total_cells, dtype=np.float32)
    if bl_cell_scales:
        bl_scale_cell[n_core_cells:] = np.array(bl_cell_scales, dtype=np.float32)
    strand_set = StrandSet(cap_verts, foot, growth, bl_scale, np.zeros(n_strands, dtype=np.int32), n_layers)

    return UnstructuredMesh(
        points=all_pts, cells=[], owner=owner_arr, neighbour=neighbour_arr,
        face_areas=sf, face_centres=cf, cell_centres=cell_cc, cell_volumes=cell_vol,
        delta_f=delta_f, k_f=k_f, boundaries=boundaries, cell_regions=all_regions,
        error_map={
            "non_orthogonality": non_ortho, "non_ortho": non_ortho,
            "skewness": skewness, "skew": skewness, "aspect_ratio": aspect_ratio,
            "stabilization_mask": stab_mask, "mask": stab_mask, "bl_scale": bl_scale_cell,
        },
        cell_types=all_ctypes, topological_dim=3,
        face_offsets=face_offsets, face_nodes=face_nodes,
        cell_offsets=cell_offsets, cell_nodes=cell_nodes,
        metadata={"engine": "bl_strategy_c", "bl_strand_set": strand_set, "bl_scale_strands": bl_scale, "trailing_edge_wedge_indices": trailing_edge_indices},
    )


def generate_offset_bl_mesh(
    surface: SurfaceMesh,
    h0: float,
    n_layers: int,
    y1: float,
    growth_rate: float,
    domain_mode: str = "external_fluid",
    config: Optional[MeshingConfig] = None,
) -> UnstructuredMesh:
    """Build offset surface psi = phi - delta = 0, mesh core, and stitch Strategy C BL."""
    from backend.meshing.mesh_engine import MeshEngine

    norm_faces, _ = normalize_closed_shell_orientations(surface.vertices, surface.faces)
    clean_surf = SurfaceMesh(vertices=np.asarray(surface.vertices, dtype=np.float64), faces=norm_faces, patches=dict(surface.patches) if surface.patches else {})
    raw_sdf = CADMeshSDF.from_surface_mesh(clean_surf)
    sign = -1.0 if domain_mode == "internal_fluid" else 1.0

    def fluid_sdf_fn(pts: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        phi, grad = raw_sdf.evaluate(pts)
        return sign * phi, sign * grad

    delta_field, _ = bl_prep_thickness_field(clean_surf.vertices, clean_surf.faces, fluid_sdf_fn, n_layers, y1, growth_rate)
    _, grad_v = fluid_sdf_fn(clean_surf.vertices)
    g_norm = np.linalg.norm(grad_v, axis=1, keepdims=True)
    v_norm = _compute_vertex_normals(clean_surf.vertices, clean_surf.faces)
    if domain_mode == "internal_fluid":
        v_norm = -v_norm
    push_dir = np.where(g_norm > 1e-12, grad_v / np.maximum(g_norm, 1e-12), v_norm)
    offset_surf = SurfaceMesh(vertices=clean_surf.vertices + delta_field[:, None] * push_dir, faces=clean_surf.faces.copy(), patches=dict(clean_surf.patches))

    core_cfg = MeshingConfig.from_dict({**config.to_dict(), "domain_mode": domain_mode, "inflation": None}) if config is not None else MeshingConfig(domain_mode=domain_mode, rcm_reorder=False)
    core_mesh = MeshEngine(use_native=True).mesh_hexcore(offset_surf, h0=h0, domain_mode=domain_mode, config=core_cfg)
    return generate_conformal_bl_strategy_c(core_mesh=core_mesh, wall_sdf_fn=fluid_sdf_fn, n_layers=n_layers, y1=y1, growth_rate=growth_rate, wall_patch="wall")
