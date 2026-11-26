"""Mesh quality report dataclass and Stromberg (2023) J_ENS / pyramid quality metrics."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.meshing._models_config import CellType

if TYPE_CHECKING:
    from backend.meshing._models_mesh import UnstructuredMesh


@dataclass
class MeshQualityReport:
    """Comprehensive topological and numerical quality report for an UnstructuredMesh."""

    num_cells: int
    num_faces: int
    num_internal_faces: int
    num_boundary_faces: int
    num_points: int
    min_cell_volume: float
    max_cell_volume: float
    total_volume: float
    max_closed_cell_divergence: float
    non_ortho_max: float
    non_ortho_mean: float
    skewness_max: float
    skewness_mean: float
    aspect_ratio_max: float
    aspect_ratio_mean: float
    stabilization_tier_counts: Dict[int, int] = field(default_factory=dict)
    is_valid: bool = True
    details: Dict[str, Any] = field(default_factory=dict)
    j_ens_min: float = 1.0
    j_ens_mean: float = 1.0
    inverted_pyramids: int = 0


def compute_nodal_scaled_jacobian(
    e1: np.ndarray, e2: np.ndarray, e3: np.ndarray, max_edge_len: float
) -> float:
    """Aspect-ratio-sensitive nodal Scaled Jacobian (Stromberg 2023, Eq. 3.30).

    J_{s,i} = det([e1, e2, e3]) / (max_k l_k)^3
    """
    det_j = float(np.dot(e1, np.cross(e2, e3)))
    return det_j / (float(max_edge_len) ** 3 + 1e-30)


def compute_element_normalized_scaled_jacobian(
    js_min: float, cell_type: Union[CellType, int]
) -> float:
    """Element Normalized Scaled Jacobian J_ENS in [-1, 1] (Lobos 2015 / Stromberg 2023, Eq. 3.32).

    J_{S,opt}: Hex8 = 1.0, Prism6 = sqrt(3)/2 (~0.866025), Tet4 & Pyr5 = sqrt(2)/2 (~0.707107)
    """
    ctype = CellType(cell_type) if not isinstance(cell_type, CellType) else cell_type
    if ctype == CellType.HEXAHEDRON:
        js_opt = 1.0
    elif ctype == CellType.WEDGE:
        js_opt = 0.8660254037844386  # sqrt(3) / 2
    else:  # TETRA (10) or PYRAMID (14)
        js_opt = 0.7071067811865476  # sqrt(2) / 2

    if js_min > js_opt:
        return float((1.0 + js_opt) - js_min)
    elif js_min < -js_opt:
        return float(-(1.0 + js_opt) - js_min)
    else:
        return float(js_min / js_opt)


def check_pyramid_base_fictitious_hex(base_pts: np.ndarray, apex_pt: np.ndarray) -> float:
    """Erects a fictitious hexahedron of height l_bar_base over a 4-node pyramid base (Stromberg p. 38).

    Returns minimum nodal Scaled Jacobian of the fictitious hexahedron.
    Folded, non-convex, or inverted quadrilateral bases yield min SJ <= 0.
    """
    base = np.asarray(base_pts, dtype=np.float64)
    apex = np.asarray(apex_pt, dtype=np.float64)
    if len(base) != 4:
        raise ValueError(f"Pyramid base must have exactly 4 nodes, got {len(base)}")

    e01 = base[1] - base[0]
    e12 = base[2] - base[1]
    e23 = base[3] - base[2]
    e30 = base[0] - base[3]
    edge_lens = [
        float(np.linalg.norm(e01)),
        float(np.linalg.norm(e12)),
        float(np.linalg.norm(e23)),
        float(np.linalg.norm(e30)),
    ]
    h_bar = float(np.mean(edge_lens))

    n_base = np.cross(base[2] - base[0], base[3] - base[1])
    n_norm = float(np.linalg.norm(n_base))
    if n_norm < 1e-12:
        return -1.0

    n_base /= n_norm
    base_center = np.mean(base, axis=0)
    if np.dot(apex - base_center, n_base) < 0.0:
        n_base = -n_base

    top = base + h_bar * n_base
    hex_pts = np.vstack([base, top])

    hex_edges = list(edge_lens)
    for k in range(4):
        hex_edges.append(float(np.linalg.norm(hex_pts[k + 4] - hex_pts[k])))
        hex_edges.append(float(np.linalg.norm(hex_pts[((k + 1) % 4) + 4] - hex_pts[k + 4])))
    max_e = max(hex_edges)

    sjs = [
        compute_nodal_scaled_jacobian(hex_pts[1] - hex_pts[0], hex_pts[3] - hex_pts[0], hex_pts[4] - hex_pts[0], max_e),
        compute_nodal_scaled_jacobian(hex_pts[2] - hex_pts[1], hex_pts[0] - hex_pts[1], hex_pts[5] - hex_pts[1], max_e),
        compute_nodal_scaled_jacobian(hex_pts[3] - hex_pts[2], hex_pts[1] - hex_pts[2], hex_pts[6] - hex_pts[2], max_e),
        compute_nodal_scaled_jacobian(hex_pts[0] - hex_pts[3], hex_pts[2] - hex_pts[3], hex_pts[7] - hex_pts[3], max_e),
        compute_nodal_scaled_jacobian(hex_pts[7] - hex_pts[4], hex_pts[5] - hex_pts[4], hex_pts[0] - hex_pts[4], max_e),
        compute_nodal_scaled_jacobian(hex_pts[4] - hex_pts[5], hex_pts[6] - hex_pts[5], hex_pts[1] - hex_pts[5], max_e),
        compute_nodal_scaled_jacobian(hex_pts[5] - hex_pts[6], hex_pts[7] - hex_pts[6], hex_pts[2] - hex_pts[6], max_e),
        compute_nodal_scaled_jacobian(hex_pts[6] - hex_pts[7], hex_pts[4] - hex_pts[7], hex_pts[3] - hex_pts[7], max_e),
    ]
    return float(min(sjs))


def compute_prism_jacobian_bound(
    points: np.ndarray,
    wedge_nodes: Union[List[int], Tuple[int, ...], np.ndarray],
) -> float:
    """Compute continuous quadratic prism Jacobian bound min_{t in [0,1]} det J_i(t) (Snippet 25).

    Evaluates det J_i(t) = A*t^2 + B*t + C at each of the 6 corners across the prism thickness
    t in [0, 1], including any interior extremum t* = -B / (2A) in (0, 1) when A > 0.
    """
    if len(wedge_nodes) != 6:
        raise ValueError(f"Prism/wedge must have 6 nodes, got {len(wedge_nodes)}")
    pts = np.asarray(points, dtype=np.float64)
    idx = [int(n) for n in wedge_nodes]
    a = pts[idx[1]] - pts[idx[0]]
    b = pts[idx[2]] - pts[idx[0]]
    at = pts[idx[4]] - pts[idx[3]]
    bt = pts[idx[5]] - pts[idx[3]]
    da = at - a
    db = bt - b
    result = 1.0
    max_a = float(max(np.linalg.norm(a), np.linalg.norm(at)))
    max_b = float(max(np.linalg.norm(b), np.linalg.norm(bt)))

    def _det3(u: np.ndarray, v: np.ndarray, w: np.ndarray) -> float:
        return float(np.dot(u, np.cross(v, w)))

    for i in range(3):
        d = pts[idx[i + 3]] - pts[idx[i]]
        a_coef = _det3(da, db, d)
        b_coef = _det3(da, b, d) + _det3(a, db, d)
        c_coef = _det3(a, b, d)
        m = min(c_coef, a_coef + b_coef + c_coef)
        if a_coef > 0.0:
            t_star = -b_coef / (2.0 * a_coef)
            if 0.0 < t_star < 1.0:
                m = min(m, (a_coef * t_star + b_coef) * t_star + c_coef)
        denom = max_a * max_b * float(np.linalg.norm(d))
        bound_i = (m / denom) if denom > 0.0 else -1.0
        result = min(result, bound_i)
    return float(result)


def check_pyramid_base_convexity(base_pts: np.ndarray, apex_pt: np.ndarray) -> bool:
    """Verify 4-node pyramid quad base convexity and positive split-tetrahedra volumes."""
    base = np.asarray(base_pts, dtype=np.float64)
    apex = np.asarray(apex_pt, dtype=np.float64)
    if base.shape != (4, 3):
        raise ValueError(f"Pyramid base must have shape (4, 3), got {base.shape}")
    if apex.shape != (3,):
        raise ValueError(f"Pyramid apex must have shape (3,), got {apex.shape}")

    n_base = np.cross(base[2] - base[0], base[3] - base[1])
    n_norm = float(np.linalg.norm(n_base))
    if n_norm <= 1e-12:
        return False
    n_base = n_base / n_norm
    base_center = np.mean(base, axis=0)
    if float(np.dot(apex - base_center, n_base)) < 0.0:
        n_base = -n_base

    for k in range(4):
        curr = base[k]
        nxt = base[(k + 1) % 4]
        prv = base[(k + 3) % 4]
        if float(np.dot(np.cross(nxt - curr, prv - curr), n_base)) < 0.0:
            return False

    tet1_vol = float(np.dot(np.cross(base[1] - base[0], base[2] - base[0]), apex - base[0]))
    tet2_vol = float(np.dot(np.cross(base[2] - base[0], base[3] - base[0]), apex - base[0]))
    return (tet1_vol > 0.0) and (tet2_vol > 0.0)


def _extract_polyhedron_faces_py(
    points: np.ndarray,
    cell_nodes: Union[List[int], np.ndarray],
) -> List[List[int]]:
    """Extract outward-oriented planar faces for a standalone polyhedron via supporting planes."""
    uniq_nodes: List[int] = list(dict.fromkeys(int(v) for v in cell_nodes))
    nv = len(uniq_nodes)
    if nv < 4:
        return []

    node_pts = points[uniq_nodes]
    cell_c_est = np.mean(node_pts, axis=0)
    l_scale = max(1e-12, float(np.max(np.linalg.norm(node_pts - cell_c_est, axis=1))))

    unique_faces: set = set()
    faces: List[List[int]] = []

    for i in range(nv):
        pi = points[uniq_nodes[i]]
        for j in range(i + 1, nv):
            pj = points[uniq_nodes[j]]
            for k in range(j + 1, nv):
                pk = points[uniq_nodes[k]]
                fn = np.cross(pj - pi, pk - pi)
                fn_norm = float(np.linalg.norm(fn))
                if fn_norm < 1e-9 * l_scale * l_scale:
                    continue
                fn = fn / fn_norm
                if float(np.dot(fn, pi - cell_c_est)) < 0.0:
                    fn = -fn

                signed_d = (node_pts - pi) @ fn
                if np.any(signed_d > 1e-4 * l_scale):
                    continue

                on_plane_idx = np.where(np.abs(signed_d) <= 1e-4 * l_scale)[0]
                if len(on_plane_idx) < 3:
                    continue

                on_plane = [uniq_nodes[m] for m in on_plane_idx]
                on_pts = points[on_plane]
                fc = np.mean(on_pts, axis=0)
                u_vec = on_pts[0] - fc
                u_norm = float(np.linalg.norm(u_vec))
                if u_norm <= 1e-15:
                    continue
                u_ax = u_vec / u_norm
                v_ax = np.cross(fn, u_ax)

                diffs = on_pts - fc
                angles = np.arctan2(diffs @ v_ax, diffs @ u_ax)
                order = np.argsort(angles)
                ordered_face = [on_plane[idx] for idx in order]

                key = tuple(sorted(ordered_face))
                if key not in unique_faces:
                    unique_faces.add(key)
                    faces.append(ordered_face)

    if len(faces) < 4:
        return []

    natural_vol = 0.0
    for f in faces:
        f_pts = points[f]
        fc = np.mean(f_pts, axis=0)
        fn = np.zeros(3, dtype=np.float64)
        for idx in range(len(f)):
            a = f_pts[idx] - fc
            b = f_pts[(idx + 1) % len(f)] - fc
            fn += 0.5 * np.cross(a, b)
        natural_vol += float(np.dot(fc - cell_c_est, fn)) / 3.0

    if natural_vol < -1e-15:
        faces = [f[::-1] for f in faces]

    return faces


def _triangulate_face_3d(
    points: np.ndarray,
    face: Union[List[int], np.ndarray],
) -> Tuple[List[Tuple[np.ndarray, np.ndarray, np.ndarray]], np.ndarray]:
    """Triangulate a 3D polygonal face using centroid-fan (star-shaped) or 2D ear-clipping."""
    m = len(face)
    if m < 3:
        return [], np.zeros(3, dtype=np.float64)

    if m == 3:
        a = points[int(face[0])]
        b = points[int(face[1])]
        c = points[int(face[2])]
        sf = 0.5 * np.cross(b - a, c - a)
        return [(a, b, c)], sf

    f_pts = points[np.asarray(face, dtype=np.int64)]
    p_bar = np.mean(f_pts, axis=0)

    sf_raw = np.zeros(3, dtype=np.float64)
    sub_sfs = []
    for i in range(m):
        pi = f_pts[i]
        pj = f_pts[(i + 1) % m]
        sub_sf = 0.5 * np.cross(pi - p_bar, pj - p_bar)
        sub_sfs.append(sub_sf)
        sf_raw += sub_sf

    area_raw = float(np.linalg.norm(sf_raw))
    if area_raw <= 1e-30:
        return [], np.zeros(3, dtype=np.float64)
    n_hat = sf_raw / area_raw

    star_shaped = all(float(np.dot(sub_sf, n_hat)) > 1e-12 * area_raw for sub_sf in sub_sfs)
    if star_shaped:
        tris = [(p_bar, f_pts[i], f_pts[(i + 1) % m]) for i in range(m)]
        return tris, sf_raw

    p0 = f_pts[0]
    best_edge = np.zeros(3, dtype=np.float64)
    best_len2 = -1.0
    for i in range(m):
        e = f_pts[(i + 1) % m] - f_pts[i]
        e_proj = e - n_hat * float(np.dot(e, n_hat))
        l2 = float(np.dot(e_proj, e_proj))
        if l2 > best_len2:
            best_len2 = l2
            best_edge = e_proj

    if best_len2 > 1e-30:
        u_ax = best_edge / np.sqrt(best_len2)
    else:
        cand = np.array([1.0, 0.0, 0.0]) if abs(n_hat[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
        u_ax = np.cross(n_hat, cand)
        u_ax = u_ax / max(1e-30, float(np.linalg.norm(u_ax)))
    v_ax = np.cross(n_hat, u_ax)

    diffs = f_pts - p0
    px = diffs @ u_ax
    py = diffs @ v_ax
    max_r = max(1e-15, float(np.max(np.hypot(px, py))))
    px = px / max_r
    py = py / max_r

    area2 = 0.0
    for i in range(m):
        j = (i + 1) % m
        area2 += float(px[i] * py[j] - px[j] * py[i])

    reversed_flag = area2 < 0.0
    if reversed_flag:
        px = px[::-1].copy()
        py = py[::-1].copy()

    def _orient2d(ia: int, ib: int, ic: int) -> float:
        return float((px[ib] - px[ia]) * (py[ic] - py[ia]) - (py[ib] - py[ia]) * (px[ic] - px[ia]))

    def _pt_in_tri(ik: int, ia: int, ib: int, ic: int) -> bool:
        return _orient2d(ia, ib, ik) >= -1e-12 and _orient2d(ib, ic, ik) >= -1e-12 and _orient2d(ic, ia, ik) >= -1e-12

    rem = list(range(m))
    local_tris: List[Tuple[int, int, int]] = []
    max_iters = m * m
    iters = 0
    while len(rem) > 3 and iters < max_iters:
        iters += 1
        cur_m = len(rem)
        found = False
        for i in range(cur_m):
            prev_i = rem[(i + cur_m - 1) % cur_m]
            curr_i = rem[i]
            next_i = rem[(i + 1) % cur_m]
            if _orient2d(prev_i, curr_i, next_i) <= 1e-12:
                continue
            is_ear = True
            for j in range(cur_m):
                k = rem[j]
                if k == prev_i or k == curr_i or k == next_i:
                    continue
                if _pt_in_tri(k, prev_i, curr_i, next_i):
                    is_ear = False
                    break
            if is_ear:
                local_tris.append((prev_i, curr_i, next_i))
                rem.pop(i)
                found = True
                break
        if not found:
            best_pos = 0
            best_det = -1e30
            for i in range(cur_m):
                prev_i = rem[(i + cur_m - 1) % cur_m]
                curr_i = rem[i]
                next_i = rem[(i + 1) % cur_m]
                det = _orient2d(prev_i, curr_i, next_i)
                if det > best_det:
                    best_det = det
                    best_pos = i
            prev_i = rem[(best_pos + cur_m - 1) % cur_m]
            curr_i = rem[best_pos]
            next_i = rem[(best_pos + 1) % cur_m]
            local_tris.append((prev_i, curr_i, next_i))
            rem.pop(best_pos)

    if len(rem) == 3:
        local_tris.append((rem[0], rem[1], rem[2]))

    out_tris: List[Tuple[np.ndarray, np.ndarray, np.ndarray]] = []
    out_sf = np.zeros(3, dtype=np.float64)
    for t0, t1, t2 in local_tris:
        i0 = (m - 1 - t0) if reversed_flag else t0
        i1 = (m - 1 - t2) if reversed_flag else t1
        i2 = (m - 1 - t1) if reversed_flag else t2
        a = f_pts[i0]
        b = f_pts[i1]
        c = f_pts[i2]
        out_sf += 0.5 * np.cross(b - a, c - a)
        out_tris.append((a, b, c))

    return out_tris, out_sf


def compute_cell_j_ens(
    points: np.ndarray,
    cell_nodes: Union[List[int], np.ndarray],
    cell_type: CellType,
    cell_faces: Optional[List[Union[List[int], np.ndarray]]] = None,
) -> float:
    """Compute Element Normalized Scaled Jacobian J_ENS for a single cell."""
    pts = points[cell_nodes]
    n_v = len(pts)

    if cell_type == CellType.HEXAHEDRON and n_v == 8:
        pairs = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]
        max_e = max(np.linalg.norm(pts[j] - pts[i]) for i, j in pairs)
        sjs = [
            compute_nodal_scaled_jacobian(pts[1] - pts[0], pts[3] - pts[0], pts[4] - pts[0], max_e),
            compute_nodal_scaled_jacobian(pts[2] - pts[1], pts[0] - pts[1], pts[5] - pts[1], max_e),
            compute_nodal_scaled_jacobian(pts[3] - pts[2], pts[1] - pts[2], pts[6] - pts[2], max_e),
            compute_nodal_scaled_jacobian(pts[0] - pts[3], pts[2] - pts[3], pts[7] - pts[3], max_e),
            compute_nodal_scaled_jacobian(pts[7] - pts[4], pts[5] - pts[4], pts[0] - pts[4], max_e),
            compute_nodal_scaled_jacobian(pts[4] - pts[5], pts[6] - pts[5], pts[1] - pts[5], max_e),
            compute_nodal_scaled_jacobian(pts[5] - pts[6], pts[7] - pts[6], pts[2] - pts[6], max_e),
            compute_nodal_scaled_jacobian(pts[6] - pts[7], pts[4] - pts[7], pts[3] - pts[7], max_e),
        ]
        return compute_element_normalized_scaled_jacobian(min(sjs), CellType.HEXAHEDRON)

    elif cell_type == CellType.WEDGE and n_v == 6:
        pairs = [(0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5)]
        max_e = max(np.linalg.norm(pts[j] - pts[i]) for i, j in pairs)
        sjs = [
            compute_nodal_scaled_jacobian(pts[1] - pts[0], pts[2] - pts[0], pts[3] - pts[0], max_e),
            compute_nodal_scaled_jacobian(pts[2] - pts[1], pts[0] - pts[1], pts[4] - pts[1], max_e),
            compute_nodal_scaled_jacobian(pts[0] - pts[2], pts[1] - pts[2], pts[5] - pts[2], max_e),
            compute_nodal_scaled_jacobian(pts[5] - pts[3], pts[4] - pts[3], pts[0] - pts[3], max_e),
            compute_nodal_scaled_jacobian(pts[3] - pts[4], pts[5] - pts[4], pts[1] - pts[4], max_e),
            compute_nodal_scaled_jacobian(pts[4] - pts[5], pts[3] - pts[5], pts[2] - pts[5], max_e),
        ]
        return compute_element_normalized_scaled_jacobian(min(sjs), CellType.WEDGE)

    elif cell_type == CellType.PYRAMID and n_v == 5:
        base_pts = pts[:4]
        apex = pts[4]
        fict_hex_sj = check_pyramid_base_fictitious_hex(base_pts, apex)
        if fict_hex_sj <= 0.0:
            return float(fict_hex_sj)

        pairs = [(0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4)]
        max_e = max(np.linalg.norm(pts[j] - pts[i]) for i, j in pairs)
        sjs = [
            compute_nodal_scaled_jacobian(pts[1] - pts[0], pts[3] - pts[0], pts[4] - pts[0], max_e),
            compute_nodal_scaled_jacobian(pts[2] - pts[1], pts[0] - pts[1], pts[4] - pts[1], max_e),
            compute_nodal_scaled_jacobian(pts[3] - pts[2], pts[1] - pts[2], pts[4] - pts[2], max_e),
            compute_nodal_scaled_jacobian(pts[0] - pts[3], pts[2] - pts[3], pts[4] - pts[3], max_e),
        ]
        e0, e1, e2, e3 = pts[0] - apex, pts[1] - apex, pts[2] - apex, pts[3] - apex
        sjs.append(compute_nodal_scaled_jacobian(e1, e0, e2, max_e))
        sjs.append(compute_nodal_scaled_jacobian(e2, e1, e3, max_e))
        sjs.append(compute_nodal_scaled_jacobian(e3, e2, e0, max_e))
        sjs.append(compute_nodal_scaled_jacobian(e0, e3, e1, max_e))
        sjs.append(fict_hex_sj)
        return compute_element_normalized_scaled_jacobian(min(sjs), CellType.PYRAMID)

    elif cell_type == CellType.TETRA and n_v == 4:
        e1 = pts[1] - pts[0]
        e2 = pts[2] - pts[0]
        e3 = pts[3] - pts[0]
        edges = [
            np.linalg.norm(pts[1] - pts[0]), np.linalg.norm(pts[2] - pts[0]), np.linalg.norm(pts[3] - pts[0]),
            np.linalg.norm(pts[2] - pts[1]), np.linalg.norm(pts[3] - pts[1]), np.linalg.norm(pts[3] - pts[2]),
        ]
        max_e = max(edges)
        sj = compute_nodal_scaled_jacobian(e1, e2, e3, max_e)
        return compute_element_normalized_scaled_jacobian(sj, CellType.TETRA)

    elif cell_type == CellType.POLYHEDRON:
        if n_v < 4:
            return 0.0

        faces_list = (
            list(cell_faces)
            if cell_faces is not None and len(cell_faces) > 0
            else _extract_polyhedron_faces_py(points, cell_nodes)
        )
        if len(faces_list) < 4:
            return 0.0

        ref_pts = [points[int(v)] for f in faces_list for v in f]
        if not ref_pts:
            return 0.0
        p_ref = np.mean(ref_pts, axis=0)

        all_tris: List[Tuple[np.ndarray, np.ndarray, np.ndarray]] = []
        r_close = np.zeros(3, dtype=np.float64)
        a_tot = 0.0
        v_p = 0.0
        c_p_num = np.zeros(3, dtype=np.float64)

        for f in faces_list:
            face_tris, sf_f = _triangulate_face_3d(points, f)
            a_tot += float(np.linalg.norm(sf_f))
            r_close += sf_f
            for a, b, c in face_tris:
                all_tris.append((a, b, c))
                da = a - p_ref
                db = b - p_ref
                dc = c - p_ref
                vt_ref = float(np.dot(da, np.cross(db, dc))) / 6.0
                v_p += vt_ref
                c_p_num += ((da + db + dc) * 0.25) * vt_ref

        if not all_tris or a_tot <= 1e-30 or abs(v_p) <= 1e-30 or abs(v_p) <= 1e-14 * (a_tot ** 1.5):
            return 0.0

        c_p = p_ref + (c_p_num / v_p)

        all_pos = True
        has_pos = False
        min_j_all = 1.0
        min_j_pos = 1.0
        sum_abs_v = 0.0

        for a, b, c in all_tris:
            e1 = a - c_p
            e2 = b - c_p
            e3 = c - c_p
            l_max = max(
                float(np.linalg.norm(e1)),
                float(np.linalg.norm(e2)),
                float(np.linalg.norm(e3)),
                float(np.linalg.norm(b - a)),
                float(np.linalg.norm(c - b)),
                float(np.linalg.norm(a - c)),
            )
            det_t = float(np.dot(e1, np.cross(e2, e3)))
            vt = det_t / 6.0
            js_t = det_t / (l_max ** 3 + 1e-30)
            j_ens_t = compute_element_normalized_scaled_jacobian(js_t, CellType.TETRA)

            sum_abs_v += abs(vt)
            if vt <= 0.0:
                all_pos = False
            else:
                has_pos = True
                if j_ens_t < min_j_pos:
                    min_j_pos = j_ens_t
            if j_ens_t < min_j_all:
                min_j_all = j_ens_t

        if v_p < 0.0:
            return float(max(-1.0, min_j_all))

        is_closed = float(np.linalg.norm(r_close)) <= 1e-3 * a_tot
        if all_pos:
            return float(min_j_pos)
        if is_closed and has_pos:
            convexity_ratio = v_p / (sum_abs_v + 1e-30)
            return float(min_j_pos * convexity_ratio)
        return float(max(-1.0, min_j_all))

    return 1.0


def compute_mesh_j_ens(mesh: "UnstructuredMesh") -> Tuple[np.ndarray, int]:
    """Compute Element Normalized Scaled Jacobian J_ENS across all cells in mesh."""
    n_cells = mesh.num_cells
    if n_cells == 0 or len(mesh.cells) != n_cells:
        return np.ones(0, dtype=np.float64), 0

    cell_faces_map: Optional[List[List[Union[List[int], np.ndarray]]]] = None
    if mesh.faces is not None and len(mesh.faces) == mesh.num_faces and mesh.num_faces > 0:
        cell_faces_map = [[] for _ in range(n_cells)]
        for f_idx in range(mesh.num_faces):
            fn = mesh.faces[f_idx]
            o = int(mesh.owner[f_idx])
            if 0 <= o < n_cells:
                cell_faces_map[o].append(fn)
            nb = int(mesh.neighbour[f_idx])
            if 0 <= nb < n_cells:
                cell_faces_map[nb].append(fn[::-1])

    j_ens = np.ones(n_cells, dtype=np.float64)
    inv_pyr = 0

    for i in range(n_cells):
        nodes = mesh.cells[i]
        n_v = len(nodes)
        ctype = mesh.cell_types[i] if mesh.cell_types is not None else (
            CellType.HEXAHEDRON if n_v == 8 else
            CellType.WEDGE if n_v == 6 else
            CellType.PYRAMID if n_v == 5 else
            CellType.TETRA if n_v == 4 else
            CellType.POLYHEDRON
        )
        cfaces = cell_faces_map[i] if cell_faces_map is not None else None
        val = compute_cell_j_ens(mesh.points, nodes, CellType(ctype), cell_faces=cfaces)
        j_ens[i] = val
        if ctype == CellType.PYRAMID and val <= 0.0:
            inv_pyr += 1

    return j_ens, inv_pyr
