"""Weingarten quadratic Monge jet curvature estimation and CCBC wall corrections."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Set, Tuple
import numpy as np

from backend.meshing.feature_graph import FeatureGraph


@dataclass
class WallCurvatureField:
    """Per-vertex principal curvatures, principal direction e1, mean and Gaussian curvatures."""

    k1: np.ndarray
    k2: np.ndarray
    e1: np.ndarray
    mean_curvature: np.ndarray = field(default=None)  # type: ignore[assignment]
    gaussian_curvature: np.ndarray = field(default=None)  # type: ignore[assignment]

    def __post_init__(self) -> None:
        self.k1 = np.asarray(self.k1, dtype=np.float64)
        self.k2 = np.asarray(self.k2, dtype=np.float64)
        self.e1 = np.asarray(self.e1, dtype=np.float64)
        if self.mean_curvature is None:
            self.mean_curvature = 0.5 * (self.k1 + self.k2)
        else:
            self.mean_curvature = np.asarray(self.mean_curvature, dtype=np.float64)
        if self.gaussian_curvature is None:
            self.gaussian_curvature = self.k1 * self.k2
        else:
            self.gaussian_curvature = np.asarray(self.gaussian_curvature, dtype=np.float64)

    def packed_20b(self) -> bytes:
        """Pack [k1, k2, e1x, e1y, e1z] as little-endian float32 (20 bytes per vertex)."""
        n = len(self.k1)
        if n == 0:
            return b""
        buf = np.empty(n, dtype=np.dtype("<5f"))
        buf[:, 0] = self.k1.astype(np.float32, copy=False)
        buf[:, 1] = self.k2.astype(np.float32, copy=False)
        buf[:, 2] = self.e1[:, 0].astype(np.float32, copy=False)
        buf[:, 3] = self.e1[:, 1].astype(np.float32, copy=False)
        buf[:, 4] = self.e1[:, 2].astype(np.float32, copy=False)
        return buf.tobytes()


@dataclass
class CCBCCorrection:
    """Curvature-corrected boundary condition (CCBC) wall quantities."""

    kappa_n: Any
    dp_dn: Any
    y_eff: Any
    kappa_mean: Any

    def keys(self) -> Tuple[str, ...]:
        return ("kappa_n", "dp_dn", "y_eff", "kappa_mean")

    def __contains__(self, key: object) -> bool:
        return isinstance(key, str) and hasattr(self, key)

    def __getitem__(self, key: str) -> Any:
        if hasattr(self, key):
            return getattr(self, key)
        raise KeyError(key)

    def get(self, key: str, default: Any = None) -> Any:
        return getattr(self, key, default)


def _orthonormal_frame(n_v: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """Build right-handed tangent basis (t1, t2) orthogonal to unit normal n_v."""
    ref = np.array([1.0, 0.0, 0.0]) if abs(float(n_v[0])) < 0.9 else np.array([0.0, 1.0, 0.0])
    t1 = np.cross(ref, n_v)
    t1_len = float(np.linalg.norm(t1))
    if t1_len < 1e-20:
        ref = np.array([0.0, 0.0, 1.0])
        t1 = np.cross(ref, n_v)
        t1_len = float(np.linalg.norm(t1))
    t1 = t1 / max(t1_len, 1e-20)
    t2 = np.cross(n_v, t1)
    t2_len = float(np.linalg.norm(t2))
    if t2_len > 1e-20:
        t2 = t2 / t2_len
    return t1, t2


def _gather_patch_neighbors(
    seed_v: int,
    verts: np.ndarray,
    patch_adj: Dict[int, Set[int]],
    rho: float,
    min_neighbors: int = 6,
    max_neighbors: int = 64,
) -> List[int]:
    """Gather patch-local neighbors within radius rho, expanding BFS to reach min_neighbors."""
    one_ring = sorted(patch_adj.get(seed_v, ()))
    if not one_ring:
        return []

    p_seed = verts[seed_v]
    visited: Set[int] = {seed_v}
    collected: List[int] = []
    frontier: deque[int] = deque()

    for u in one_ring:
        visited.add(u)
        collected.append(u)
        frontier.append(u)

    overflow_queue: deque[int] = deque()
    while frontier and len(collected) < max_neighbors:
        u = frontier.popleft()
        for w in patch_adj.get(u, ()):
            if w in visited:
                continue
            visited.add(w)
            dist = float(np.linalg.norm(verts[w] - p_seed))
            if dist <= rho:
                collected.append(w)
                frontier.append(w)
                if len(collected) >= max_neighbors:
                    break
            else:
                overflow_queue.append(w)

    while overflow_queue and len(collected) < min_neighbors:
        w = overflow_queue.popleft()
        collected.append(w)
        for nxt in patch_adj.get(w, ()):
            if nxt not in visited:
                visited.add(nxt)
                overflow_queue.append(nxt)

    return collected


def _fit_weingarten_at_vertex(
    p_v: np.ndarray,
    n_v: np.ndarray,
    nbr_pts: np.ndarray,
    k_cap: float,
) -> Tuple[float, float, np.ndarray]:
    """Fit Cazals/Pouget quadratic Monge jet and extract (k1, k2, e1)."""
    t1, t2 = _orthonormal_frame(n_v)
    m = len(nbr_pts)
    if m < 3:
        return 0.0, 0.0, t1

    rel = nbr_pts - p_v[None, :]
    u = rel @ t1
    v = rel @ t2
    z = rel @ n_v

    rms = float(np.sqrt(np.mean(u * u + v * v)))
    if rms < 1e-15:
        return 0.0, 0.0, t1
    s = max(rms, 1e-12)

    us = np.concatenate(([0.0], u / s))
    vs = np.concatenate(([0.0], v / s))
    zs = np.concatenate(([0.0], z / s))
    weights = 1.0 / (1.0 + us * us + vs * vs)

    design = np.column_stack([
        np.ones_like(us),
        us,
        vs,
        0.5 * us * us,
        us * vs,
        0.5 * vs * vs,
    ])
    w_design = design * weights[:, None]
    ata = w_design.T @ design
    atb = w_design.T @ zs

    if m >= 5:
        reg = np.diag([1e-10, 1e-10, 1e-10, 1e-8, 1e-8, 1e-8])
    else:
        reg = np.diag([1e-8, 1e-8, 1e-8, 1e-3, 1e-3, 1e-3])

    try:
        coeffs = np.linalg.solve(ata + reg, atb)
    except np.linalg.LinAlgError:
        coeffs, _, _, _ = np.linalg.lstsq(ata + reg, atb, rcond=None)

    g_u = float(coeffs[1])
    g_v = float(coeffs[2])
    a_coef = float(coeffs[3]) / s
    b_coef = float(coeffs[4]) / s
    c_coef = float(coeffs[5]) / s

    first_e = 1.0 + g_u * g_u
    first_f = g_u * g_v
    first_g = 1.0 + g_v * g_v
    det_i = first_e * first_g - first_f * first_f
    ell = float(np.sqrt(max(det_i, 1e-30)))

    sec_l = a_coef / ell
    sec_m = b_coef / ell
    sec_n = c_coef / ell

    w00 = (first_g * sec_l - first_f * sec_m) / det_i
    w01 = (first_g * sec_m - first_f * sec_n) / det_i
    w10 = (-first_f * sec_l + first_e * sec_m) / det_i
    w11 = (-first_f * sec_m + first_e * sec_n) / det_i

    trace = w00 + w11
    disc = max((w00 - w11) ** 2 + 4.0 * w01 * w10, 0.0)
    sqrt_disc = float(np.sqrt(disc))
    lam_a = 0.5 * (trace + sqrt_disc)
    lam_b = 0.5 * (trace - sqrt_disc)

    if abs(lam_a) > abs(lam_b) or (abs(lam_a) == abs(lam_b) and lam_a >= lam_b):
        kv1, kv2 = lam_a, lam_b
    else:
        kv1, kv2 = lam_b, lam_a

    cand0 = (-w01, w00 - kv1)
    cand1 = (w11 - kv1, -w10)
    n0_sq = cand0[0] * cand0[0] + cand0[1] * cand0[1]
    n1_sq = cand1[0] * cand1[0] + cand1[1] * cand1[1]
    if max(n0_sq, n1_sq) < 1e-24:
        alpha, beta = 1.0, 0.0
    elif n0_sq >= n1_sq:
        alpha, beta = cand0
    else:
        alpha, beta = cand1

    r_u = t1 + g_u * n_v
    r_v = t2 + g_v * n_v
    e1_vec = alpha * r_u + beta * r_v
    e1_norm = float(np.linalg.norm(e1_vec))
    e1_unit = e1_vec / e1_norm if e1_norm > 1e-20 else t1

    kv1 = float(np.clip(kv1, -k_cap, k_cap))
    kv2 = float(np.clip(kv2, -k_cap, k_cap))
    return kv1, kv2, e1_unit


def compute_weingarten_curvature(
    vertices: np.ndarray,
    faces: np.ndarray,
    feature_graph: Optional[FeatureGraph] = None,
    h_min: float = 0.01,
    cap_factor: float = 2.0,
) -> WallCurvatureField:
    """Compute patch-isolated Weingarten principal curvatures on a triangulated surface."""
    verts = np.asarray(vertices, dtype=np.float64)
    tris = np.asarray(faces, dtype=np.int64)
    n_verts = len(verts)
    n_faces = len(tris)

    k1 = np.zeros(n_verts, dtype=np.float64)
    k2 = np.zeros(n_verts, dtype=np.float64)
    e1 = np.zeros((n_verts, 3), dtype=np.float64)
    e1[:, 0] = 1.0

    if n_verts == 0 or n_faces == 0:
        return WallCurvatureField(k1=k1, k2=k2, e1=e1)

    if feature_graph is not None:
        tri_patch = np.asarray(feature_graph.tri_smooth_patch, dtype=np.int32)
    else:
        tri_patch = np.zeros(n_faces, dtype=np.int32)

    v0 = verts[tris[:, 0]]
    v1 = verts[tris[:, 1]]
    v2 = verts[tris[:, 2]]
    cross = np.cross(v1 - v0, v2 - v0)
    double_area = np.linalg.norm(cross, axis=1)
    areas = 0.5 * double_area
    normals = np.zeros_like(cross)
    valid = double_area > 1e-20
    normals[valid] = cross[valid] / double_area[valid, None]

    vert_faces: List[List[int]] = [[] for _ in range(n_verts)]
    patch_vert_adj: Dict[int, Dict[int, Set[int]]] = defaultdict(lambda: defaultdict(set))

    for fi in range(n_faces):
        a, b, c = int(tris[fi, 0]), int(tris[fi, 1]), int(tris[fi, 2])
        p_id = int(tri_patch[fi])
        vert_faces[a].append(fi)
        vert_faces[b].append(fi)
        vert_faces[c].append(fi)
        adj = patch_vert_adj[p_id]
        adj[a].add(b)
        adj[a].add(c)
        adj[b].add(a)
        adj[b].add(c)
        adj[c].add(a)
        adj[c].add(b)

    k_cap = 1.0 / max(float(cap_factor) * float(h_min), 1e-12)

    for v in range(n_verts):
        inc = vert_faces[v]
        if not inc:
            continue

        patch_areas: Dict[int, float] = defaultdict(float)
        for fi in inc:
            patch_areas[int(tri_patch[fi])] += max(float(areas[fi]), 1e-30)

        primary_patch = min(
            patch_areas.keys(),
            key=lambda pid: (-patch_areas[pid], pid),
        )

        n_sum = np.zeros(3, dtype=np.float64)
        for fi in inc:
            if int(tri_patch[fi]) == primary_patch:
                n_sum += areas[fi] * normals[fi]
        n_len = float(np.linalg.norm(n_sum))
        if n_len < 1e-20:
            for fi in inc:
                if areas[fi] > 1e-20:
                    n_sum = normals[fi]
                    n_len = float(np.linalg.norm(n_sum))
                    break
        if n_len < 1e-20:
            continue
        n_v = n_sum / n_len

        adj_p = patch_vert_adj[primary_patch]
        one_ring = adj_p.get(v, ())
        if not one_ring:
            continue
        edge_lengths = [float(np.linalg.norm(verts[u] - verts[v])) for u in one_ring]
        e_bar = float(np.mean(edge_lengths)) if edge_lengths else float(h_min)
        rho = max(2.0 * e_bar, 1.5 * float(h_min))

        nbr_ids = _gather_patch_neighbors(v, verts, adj_p, rho=rho)
        kv1, kv2, e1_v = _fit_weingarten_at_vertex(
            p_v=verts[v],
            n_v=n_v,
            nbr_pts=verts[nbr_ids],
            k_cap=k_cap,
        )
        k1[v] = kv1
        k2[v] = kv2
        e1[v] = e1_v

    return WallCurvatureField(
        k1=k1,
        k2=k2,
        e1=e1,
        mean_curvature=0.5 * (k1 + k2),
        gaussian_curvature=k1 * k2,
    )


def compute_ccbc_corrections(
    k1: Any,
    k2: Any,
    e1: Any,
    u_tangent: Any,
    rho: Any,
    y1: Any,
    h_lat: Any,
    normal: Optional[Any] = None,
) -> CCBCCorrection:
    """Compute normal curvature, wall pressure gradient, and sagitta effective first cell height."""
    k1_arr = np.asarray(k1, dtype=np.float64)
    k2_arr = np.asarray(k2, dtype=np.float64)
    e1_arr = np.asarray(e1, dtype=np.float64)
    u_arr = np.asarray(u_tangent, dtype=np.float64)
    rho_arr = np.asarray(rho, dtype=np.float64)
    y1_arr = np.asarray(y1, dtype=np.float64)
    h_lat_arr = np.asarray(h_lat, dtype=np.float64)

    scalar_mode = k1_arr.ndim == 0 and u_arr.ndim == 1

    e1_norm = np.linalg.norm(e1_arr, axis=-1, keepdims=True)
    e1_unit = e1_arr / np.maximum(e1_norm, 1e-20)

    if normal is not None:
        n_arr = np.asarray(normal, dtype=np.float64)
        n_norm = np.linalg.norm(n_arr, axis=-1, keepdims=True)
        n_unit = n_arr / np.maximum(n_norm, 1e-20)
        e2_unit = np.cross(n_unit, e1_unit)
        e2_norm = np.linalg.norm(e2_unit, axis=-1, keepdims=True)
        e2_unit = e2_unit / np.maximum(e2_norm, 1e-20)
        u1 = np.sum(u_arr * e1_unit, axis=-1)
        u2 = np.sum(u_arr * e2_unit, axis=-1)
        ut_sq = u1 * u1 + u2 * u2
        u2_sq = u2 * u2
    else:
        u1 = np.sum(u_arr * e1_unit, axis=-1)
        ut_sq = np.sum(u_arr * u_arr, axis=-1)
        u2_sq = np.maximum(ut_sq - u1 * u1, 0.0)

    kappa_n = np.where(
        ut_sq > 1e-30,
        (k1_arr * (u1 * u1) + k2_arr * u2_sq) / np.maximum(ut_sq, 1e-30),
        0.0,
    )
    dp_dn = rho_arr * kappa_n * ut_sq
    kappa_mean = 0.5 * (k1_arr + k2_arr)
    y_eff = y1_arr + kappa_mean * (h_lat_arr ** 2) / 8.0

    if scalar_mode:
        return CCBCCorrection(
            kappa_n=float(kappa_n),
            dp_dn=float(dp_dn),
            y_eff=float(y_eff),
            kappa_mean=float(kappa_mean),
        )

    return CCBCCorrection(
        kappa_n=kappa_n,
        dp_dn=dp_dn,
        y_eff=y_eff,
        kappa_mean=kappa_mean,
    )
