"""Pure-Python / NumPy DiFVM graph-topology fallback and element classification."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.meshing.models import CellType, MeshingConfig, UnstructuredMesh
from backend.meshing.topology_coupling import extract_cht_twin_faces
from backend.meshing.topology_wsrd import (
    compute_wsrd_redistribution_map,
    compute_wsrd_weights_array,
)


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


def _compute_polygon_normal_and_centroid_py(
    face_nodes: List[int], points: np.ndarray
) -> Tuple[np.ndarray, np.ndarray]:
    """Compute face area normal vector S_f and centroid c_f via triangle fan."""
    n = len(face_nodes)
    if n < 3:
        if n == 2:
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


def _extract_cell_faces_py(
    cell_nodes: np.ndarray,
    points: Optional[np.ndarray] = None,
) -> List[List[int]]:
    """Extract ordered face vertex lists for standard 2D and 3D cells."""
    nv = len(cell_nodes)
    c = cell_nodes
    if nv == 8:
        if points is not None:
            pts_cell = points[c]
            span = np.max(pts_cell, axis=0) - np.min(pts_cell, axis=0)
            max_span = float(np.max(span))
            min_span = float(np.min(span))
            if max_span > 1e-12 and (min_span / max_span) < 1e-5:
                return [[int(c[i]), int(c[(i + 1) % nv])] for i in range(nv)]
        faces = [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],
            [int(c[4]), int(c[5]), int(c[6]), int(c[7])],
            [int(c[0]), int(c[1]), int(c[5]), int(c[4])],
            [int(c[1]), int(c[2]), int(c[6]), int(c[5])],
            [int(c[2]), int(c[3]), int(c[7]), int(c[6])],
            [int(c[3]), int(c[0]), int(c[4]), int(c[7])],
        ]
    elif nv == 4:
        if points is not None:
            p0, p1, p2, p3 = points[int(c[0])], points[int(c[1])], points[int(c[2])], points[int(c[3])]
            pts_quad = points[c]
            diffs = pts_quad[:, None, :] - pts_quad[None, :, :]
            max_dist = float(np.max(np.linalg.norm(diffs, axis=-1)))
            l_scale = max(max_dist, 1e-12)
            v_tet = abs(float(np.dot(p1 - p0, np.cross(p2 - p0, p3 - p0)))) / 6.0
            v_norm = v_tet / (l_scale ** 3)
            if v_norm < 1e-6:
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
            faces = [
                [int(c[0]), int(c[2]), int(c[1])],
                [int(c[0]), int(c[1]), int(c[3])],
                [int(c[1]), int(c[2]), int(c[3])],
                [int(c[2]), int(c[0]), int(c[3])],
            ]
    elif nv == 6:
        faces = [
            [int(c[0]), int(c[2]), int(c[1])],
            [int(c[3]), int(c[4]), int(c[5])],
            [int(c[0]), int(c[1]), int(c[4]), int(c[3])],
            [int(c[1]), int(c[2]), int(c[5]), int(c[4])],
            [int(c[2]), int(c[0]), int(c[3]), int(c[5])],
        ]
    elif nv == 5:
        faces = [
            [int(c[0]), int(c[3]), int(c[2]), int(c[1])],
            [int(c[0]), int(c[1]), int(c[4])],
            [int(c[1]), int(c[2]), int(c[4])],
            [int(c[2]), int(c[3]), int(c[4])],
            [int(c[3]), int(c[0]), int(c[4])],
        ]
    elif nv == 3:
        faces = [
            [int(c[0]), int(c[1])],
            [int(c[1]), int(c[2])],
            [int(c[2]), int(c[0])],
        ]
    else:
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

    cell_centres = np.zeros((n_cells, 3), dtype=np.float64)
    cell_volumes = np.zeros(n_cells, dtype=np.float64)

    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        c_approx = np.mean(pts[cell_arr], axis=0)
        ctype = cell_types_arr[c_idx]
        is_cell_2d = ctype in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value)
        div_factor = 2.0 if is_cell_2d else 3.0

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
            pyr_c = tf["centre"] * (2.0 / 3.0) + c_approx * (1.0 / 3.0) if is_cell_2d else tf["centre"] * 0.75 + c_approx * 0.25
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

    internal_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] >= 0]
    boundary_indices = [i for i, tf in enumerate(temp_faces) if tf["neighbour"] < 0]
    internal_indices.sort(key=lambda idx: (temp_faces[idx]["owner"], temp_faces[idx]["neighbour"]))

    boundaries: Dict[str, Tuple[int, int]] = {}
    n_int = len(internal_indices)

    if boundary_patches is not None and len(boundary_patches) > 0:
        first_val = next(iter(boundary_patches.values()))
        if isinstance(first_val, list):
            patch_key_map: Dict[Tuple[int, ...], str] = {}
            for pname, plist in boundary_patches.items():
                for p_fnodes in plist:
                    patch_key_map[tuple(sorted(int(x) for x in p_fnodes))] = pname

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
            patch_groups_ids.setdefault(pname, []).append(b_idx)

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

        if nb >= 0:
            c_nb = cell_centres[nb]
            df = c_nb - c_o
            df_norm = np.linalg.norm(df)
            sf_norm = np.linalg.norm(sf)
            sf_dot_df = float(np.dot(sf, df))

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

            cos_theta = 1.0
            if sf_norm > 1e-15 and df_norm > 1e-15:
                cos_theta = float(np.clip(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0))
            theta_deg = float(np.arccos(cos_theta) * 180.0 / np.pi)
            non_ortho[new_idx] = theta_deg

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

    aspect_ratio = np.ones(n_cells, dtype=np.float64)
    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        if len(cell_arr) > 0:
            cpts = pts[cell_arr]
            spans = np.max(cpts, axis=0) - np.min(cpts, axis=0)
            valid_spans = spans[spans > 1e-12]
            if len(valid_spans) >= 2:
                aspect_ratio[c_idx] = max(1.0, float(np.max(valid_spans) / max(float(np.min(valid_spans)), 1e-15)))

    min_beta_per_cell = np.ones(n_cells, dtype=np.float64)
    if n_faces > 0:
        np.minimum.at(min_beta_per_cell, owner, beta_upwind)
        int_mask = neighbour >= 0
        if np.any(int_mask):
            np.minimum.at(min_beta_per_cell, neighbour[int_mask], beta_upwind[int_mask])
    omega_cell = np.clip(min_beta_per_cell, 0.1, 1.0)

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
        baffles=[],
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
