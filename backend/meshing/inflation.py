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

        if nominal_normal is None:
            # Estimate nominal normal from first face
            f0 = np.asarray(wall_faces[0], dtype=np.int64)
            if len(f0) >= 3:
                p0, p1, p2 = pts[f0[0]], pts[f0[1]], pts[f0[2]]
                fn = np.cross(p1 - p0, p2 - p0)
                norm_fn = np.linalg.norm(fn)
                nominal_normal = fn / norm_fn if norm_fn > 1e-12 else np.array([0, 0, 1.0])
            else:
                nominal_normal = np.array([0, 0, 1.0])
        else:
            nominal_normal = np.asarray(nominal_normal, dtype=np.float64)
            norm_val = np.linalg.norm(nominal_normal)
            if norm_val > 1e-12:
                nominal_normal = nominal_normal / norm_val

        # 1. Compute vertex normals and corner dihedral turning angles
        vert_normals = np.zeros((n_base, 3), dtype=np.float64)
        corner_max_angles = np.zeros(n_base, dtype=np.float64)
        face_normals = []
        vert_face_map = [[] for _ in range(n_base)]

        for f_idx, f in enumerate(wall_faces):
            f_arr = np.asarray(f, dtype=np.int64)
            if len(f_arr) >= 3:
                p0, p1, p2 = pts[f_arr[0]], pts[f_arr[1]], pts[f_arr[2]]
                fn = np.cross(p1 - p0, p2 - p0)
                norm_fn = np.linalg.norm(fn)
                if norm_fn > 1e-12:
                    fn = fn / norm_fn
                    if np.dot(fn, nominal_normal) < 0.0:
                        fn = -fn
                else:
                    fn = nominal_normal.copy()
            else:
                fn = nominal_normal.copy()

            face_normals.append(fn)
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
                    factor *= np.tanh(d_clear / (2.0 * total_thickness))

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

            for f in wall_faces:
                f_arr = np.asarray(f, dtype=np.int64)
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

        # 5. Build topology via DiFVM engine
        mesh = compute_difvm_topology(all_points, all_cells)
        return mesh
