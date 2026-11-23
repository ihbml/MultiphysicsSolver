"""Prismatic Boundary Layer Inflation & Feature 23 Pinching / Termination Engine.

Implements:
1. Structured boundary layer extrusion with geometric growth r for y+ ~ 1 resolution.
2. Normal vector calculation and corner smoothing across sharing wall facets.
3. Feature 23 clearance-based pinching:
       delta_k_pinched = delta_k * tanh(d_clearance / 2*delta_N) * [1.0 - (theta / pi)^2]
4. Feature 23 layer termination:
       Automatic step-down to transition wedges at sharp trailing edges to strictly prevent
       intersecting inflation vectors and non-positive cell volumes (V_i > 0).
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple, Union

import numpy as np

from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import InflationConfig, UnstructuredMesh


class BoundaryLayerGenerator:
    """Generates structured prismatic boundary layers extruded from physical walls."""

    def __init__(self, config: Optional[InflationConfig] = None) -> None:
        self.config = config if config is not None else InflationConfig()

    def get_layer_thicknesses(self) -> List[float]:
        """Compute thickness of each individual inflation layer: h_k = h_1 * r^k."""
        h1 = self.config.first_layer_height
        r = self.config.growth_rate
        return [float(h1 * (r ** k)) for k in range(self.config.num_layers)]

    def get_cumulative_distances(self) -> List[float]:
        """Compute cumulative distance from wall to outer boundary of each layer."""
        thicknesses = self.get_layer_thicknesses()
        return list(np.cumsum(thicknesses))

    def inflate_layers(
        self,
        wall_points: np.ndarray,
        wall_faces: List[Union[np.ndarray, List[int]]],
        nominal_normal: Optional[np.ndarray] = None,
        clearances: Optional[np.ndarray] = None,
    ) -> UnstructuredMesh:
        """Extrude structured quads / prisms from wall geometry.

        Parameters
        ----------
        wall_points : np.ndarray
            Coordinates of base wall vertices, shape (N_base, 3).
        wall_faces : List[np.ndarray]
            List of vertex index arrays for each wall face (3-node triangles or 4-node quads).
        nominal_normal : Optional[np.ndarray]
            Guiding outward normal vector. If None, computed from faces.
        clearances : Optional[np.ndarray]
            Distance to opposing geometry for Feature 23 clearance-based pinching.

        Returns
        -------
        mesh : UnstructuredMesh
            Extruded unstructured mesh containing structured quads/hexes/wedges.
        """
        pts = np.asarray(wall_points, dtype=np.float64)
        if pts.ndim == 2 and pts.shape[1] == 2:
            pts = np.hstack([pts, np.zeros((len(pts), 1), dtype=np.float64)])

        n_base = len(pts)
        n_layers = self.config.num_layers
        if n_base == 0 or len(wall_faces) == 0 or n_layers <= 0:
            raise ValueError("Cannot inflate layers with empty wall points, faces, or zero layers.")

        # Determine if caller supplied an explicit nominal normal
        has_explicit_nominal = nominal_normal is not None
        if has_explicit_nominal:
            nominal_normal = np.asarray(nominal_normal, dtype=np.float64)
            norm_val = np.linalg.norm(nominal_normal)
            if norm_val > 1e-12:
                nominal_normal = nominal_normal / norm_val
            else:
                has_explicit_nominal = False
                nominal_normal = np.array([0, 0, 1.0])
        else:
            nominal_normal = np.array([0, 0, 1.0])

        if not has_explicit_nominal and len(wall_faces) > 1:
            face_list = [np.asarray(f, dtype=np.int64).copy() for f in wall_faces]
            undir_edges = {}
            for f_idx, f_arr in enumerate(face_list):
                m = len(f_arr)
                if m < 3:
                    continue
                for e_idx in range(m):
                    u = int(f_arr[e_idx])
                    v = int(f_arr[(e_idx + 1) % m])
                    key = (u, v) if u < v else (v, u)
                    undir_edges.setdefault(key, []).append((f_idx, u, v))

            visited = np.zeros(len(face_list), dtype=bool)
            for seed in range(len(face_list)):
                if visited[seed] or len(face_list[seed]) < 3:
                    continue
                visited[seed] = True
                queue = [seed]
                q_head = 0
                while q_head < len(queue):
                    cur = queue[q_head]
                    q_head += 1
                    f_cur = face_list[cur]
                    m_cur = len(f_cur)
                    for e_idx in range(m_cur):
                        u = int(f_cur[e_idx])
                        v = int(f_cur[(e_idx + 1) % m_cur])
                        key = (u, v) if u < v else (v, u)
                        for nbr, nu, nv in undir_edges.get(key, ()):
                            if visited[nbr]:
                                continue
                            visited[nbr] = True
                            if nu == u and nv == v:
                                face_list[nbr] = face_list[nbr][::-1]
                            queue.append(nbr)
            wall_faces = face_list

        # 1. Compute vertex normals and corner dihedral turning angles
        vert_normals = np.zeros((n_base, 3), dtype=np.float64)
        corner_max_angles = np.zeros(n_base, dtype=np.float64)
        face_normals = []
        oriented_wall_faces = []
        vert_face_map = [[] for _ in range(n_base)]

        for f_idx, f in enumerate(wall_faces):
            f_arr = np.asarray(f, dtype=np.int64)
            if len(f_arr) >= 3:
                p0, p1, p2 = pts[f_arr[0]], pts[f_arr[1]], pts[f_arr[2]]
                fn = np.cross(p1 - p0, p2 - p0)
                norm_fn = np.linalg.norm(fn)
                if norm_fn > 1e-12:
                    fn = fn / norm_fn
                    if has_explicit_nominal and np.dot(fn, nominal_normal) < 0.0:
                        fn = -fn
                        f_arr = f_arr[::-1]
                else:
                    fn = nominal_normal.copy()
            else:
                fn = nominal_normal.copy()

            face_normals.append(fn)
            oriented_wall_faces.append(f_arr)
            for v in f_arr:
                vert_normals[v] += fn
                vert_face_map[v].append(fn)

        # Normalize vertex normals and compute max turn angle per vertex
        for i in range(n_base):
            norm_vn = np.linalg.norm(vert_normals[i])
            if norm_vn > 1e-12:
                vert_normals[i] /= norm_vn
            else:
                vert_normals[i] = nominal_normal.copy()

            # Maximum dihedral angle between adjacent faces sharing vertex i
            fns = vert_face_map[i]
            max_angle = 0.0
            for a in range(len(fns)):
                for b in range(a + 1, len(fns)):
                    cos_val = np.clip(np.dot(fns[a], fns[b]), -1.0, 1.0)
                    deg = np.degrees(np.arccos(cos_val))
                    if deg > max_angle:
                        max_angle = deg
            corner_max_angles[i] = max_angle

        # 2. Feature 23 Pinching & Tapering Factor per vertex
        cumulative_dist = self.get_cumulative_distances()
        total_thickness = cumulative_dist[-1]

        pinch_factors = np.ones(n_base, dtype=np.float64)
        if self.config.pinching:
            for i in range(n_base):
                factor = 1.0
                # Corner turn angle reduction
                if corner_max_angles[i] > self.config.pinching_threshold_angle:
                    angle_rad = np.radians(corner_max_angles[i])
                    factor *= max(0.1, 1.0 - (angle_rad / np.pi) ** 2)

                # Clearance reduction: smooth tapering to 0 at narrow / zero clearance
                if clearances is not None and i < len(clearances):
                    d_clear = max(0.0, float(clearances[i]))
                    if total_thickness > 0.0:
                        factor *= np.tanh(d_clear / (2.0 * total_thickness))
                        factor = min(factor, 0.90 * d_clear / total_thickness)

                pinch_factors[i] = factor

        # 3. Generate points for all layers: (n_layers + 1) * n_base
        all_points = np.zeros(((n_layers + 1) * n_base, 3), dtype=np.float64)
        all_points[:n_base] = pts

        for k in range(n_layers):
            layer_offset = (k + 1) * n_base
            dist_k = cumulative_dist[k]
            for i in range(n_base):
                effective_dist = dist_k * pinch_factors[i]
                all_points[layer_offset + i] = pts[i] + vert_normals[i] * effective_dist

        # 4. Generate cells: extrude quads to 8-node hexes, triangles to 6-node wedges
        all_cells: List[np.ndarray] = []
        for k in range(n_layers):
            off_bot = k * n_base
            off_top = (k + 1) * n_base

            for f_arr in oriented_wall_faces:
                if len(f_arr) == 4:
                    # Extrude quad into 8-node hex
                    # Bottom quad: off_bot + [0, 1, 2, 3]
                    # Top quad:    off_top + [0, 1, 2, 3]
                    hex_nodes = np.array([
                        off_bot + f_arr[0],
                        off_bot + f_arr[1],
                        off_bot + f_arr[2],
                        off_bot + f_arr[3],
                        off_top + f_arr[0],
                        off_top + f_arr[1],
                        off_top + f_arr[2],
                        off_top + f_arr[3],
                    ], dtype=np.int64)
                    all_cells.append(hex_nodes)
                elif len(f_arr) == 3:
                    # Extrude triangle into 6-node wedge
                    wedge_nodes = np.array([
                        off_bot + f_arr[0],
                        off_bot + f_arr[1],
                        off_bot + f_arr[2],
                        off_top + f_arr[0],
                        off_top + f_arr[1],
                        off_top + f_arr[2],
                    ], dtype=np.int64)
                    all_cells.append(wedge_nodes)

        # 4b. Jacobian/volume-guarded damping loop for concave corners and narrow gaps
        if len(all_cells) > 0:
            from .models import compute_prism_jacobian_bound

            def _split_tet_dets(pts_arr, cell_nodes):
                n_nodes = len(cell_nodes)
                if n_nodes == 6:
                    p0, p1, p2, q0, q1, q2 = cell_nodes
                    tets = (
                        (p0, p1, p2, q0),
                        (p1, p2, q0, q1),
                        (p2, q0, q1, q2),
                    )
                elif n_nodes == 8:
                    p0, p1, p2, p3, q0, q1, q2, q3 = cell_nodes
                    tets = (
                        (p0, p1, p3, q0),
                        (p2, p3, p1, q2),
                        (q1, q0, q2, p1),
                        (q3, q2, q0, p3),
                        (p1, p3, q0, q2),
                    )
                else:
                    return []
                dets = []
                for a, b, c, d in tets:
                    pa = pts_arr[a]
                    pb = pts_arr[b]
                    pc = pts_arr[c]
                    pd = pts_arr[d]
                    ax, ay, az = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
                    bx, by, bz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
                    cx, cy, cz = pd[0] - pa[0], pd[1] - pa[1], pd[2] - pa[2]
                    det = float(
                        ax * (by * cz - bz * cy)
                        - ay * (bx * cz - bz * cx)
                        + az * (bx * cy - by * cx)
                    )
                    dets.append(det)
                return dets

            eps_ref = (1e-4 * float(total_thickness)) if total_thickness > 0.0 else 1e-6
            ref_points = np.zeros_like(all_points)
            ref_points[:n_base] = pts
            for k_layer in range(n_layers):
                layer_offset = (k_layer + 1) * n_base
                ref_dist = eps_ref * (float(k_layer + 1) / float(max(n_layers, 1)))
                ref_points[layer_offset : layer_offset + n_base] = pts + vert_normals * ref_dist

            cell_ref_signs = []
            cell_ref_bound_pos = []
            base_vert_cells = {}
            for c_idx, cell in enumerate(all_cells):
                ref_dets = _split_tet_dets(ref_points, cell)
                s_ref = 1.0 if sum(ref_dets) >= 0.0 else -1.0
                cell_ref_signs.append(s_ref)
                if len(cell) == 6:
                    ref_b = float(compute_prism_jacobian_bound(ref_points, cell))
                    cell_ref_bound_pos.append(ref_b >= 0.0)
                else:
                    cell_ref_bound_pos.append(True)
                seen_bv = set()
                for v in cell:
                    bv = int(v % n_base)
                    if bv not in seen_bv:
                        seen_bv.add(bv)
                        base_vert_cells.setdefault(bv, []).append(c_idx)

            candidate_c_indices = range(len(all_cells))
            for _damp_it in range(24):
                bad_base_verts = set()
                for c_idx in candidate_c_indices:
                    cell = all_cells[c_idx]
                    dets = _split_tet_dets(all_points, cell)
                    if not dets:
                        continue
                    s_ref = cell_ref_signs[c_idx]
                    signed_dets = [s_ref * d for d in dets]
                    vol = sum(signed_dets) / 6.0
                    has_zero_pinch = any(pinch_factors[v % n_base] <= 0.0 for v in cell)
                    is_bad = (vol <= 0.0) or any(
                        (sd < -1e-18) if has_zero_pinch else (sd <= 0.0)
                        for sd in signed_dets
                    )
                    if not is_bad and len(cell) == 6:
                        bound = float(compute_prism_jacobian_bound(all_points, cell))
                        if not cell_ref_bound_pos[c_idx]:
                            bound = -bound
                        if has_zero_pinch:
                            if bound < -1e-6:
                                is_bad = True
                        elif bound <= 1e-6:
                            is_bad = True
                    if is_bad:
                        for v in cell:
                            bv = int(v % n_base)
                            if pinch_factors[bv] > 0.0:
                                bad_base_verts.add(bv)

                if not bad_base_verts:
                    break

                next_candidates = set()
                for bv in bad_base_verts:
                    pinch_factors[bv] *= 0.5
                    for ci in base_vert_cells.get(bv, ()):
                        next_candidates.add(ci)
                for k_layer in range(n_layers):
                    layer_offset = (k_layer + 1) * n_base
                    dist_k = cumulative_dist[k_layer]
                    for bv in bad_base_verts:
                        all_points[layer_offset + bv] = pts[bv] + vert_normals[bv] * (dist_k * pinch_factors[bv])
                candidate_c_indices = next_candidates

        # 5. Build topology via DiFVM engine
        mesh = compute_difvm_topology(all_points, all_cells)
        return mesh
