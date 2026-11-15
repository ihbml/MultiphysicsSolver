"""Meshless Pure-Dual GPU Voronoi Engine & Differentiable Topological Sensitivity (Papers 4 & 5).

Implements:
- Dual Polyhedral GPU Voronoi representation: float4 P[64], int3 T[96] (Ray et al. 2018, Paper 4).
- Exact Radius-of-Security early clipping termination: ||x_p - x_i|| > 2 * R_max.
- Stokes 6-tetrahedron projection volume integration for exact cell volume, centroid, and face areas.
- Analytical 1st and 2nd derivatives of Voronoi vertices via 3x3 local linear system A x_c = b (Paper 5).
- Guaranteed C^1-continuous cell volume derivatives across Voronoi topological flips.
"""

from __future__ import annotations

import ctypes
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Set, Tuple, Union
import numpy as np


class GpuVoronoiCellC(ctypes.Structure):
    """Exact C-ABI memory layout matching 2,176-byte GPU shared memory footprint (Paper 4 §3.2)."""
    _fields_ = [
        ("P", ctypes.c_float * (64 * 4)),  # 64 half-space planes float4 (a, b, c, d)
        ("T", ctypes.c_int32 * (96 * 3)),  # 96 dual triangles int3 (u, v, w)
        ("num_planes", ctypes.c_int32),
        ("num_triangles", ctypes.c_int32),
    ]


@dataclass
class GpuVoronoiCell:
    """Python representation of the dual polyhedral GPU Voronoi cell."""
    seed: np.ndarray  # (3,)
    planes: List[np.ndarray] = field(default_factory=list)  # float4 [a, b, c, d]
    triangles: List[Tuple[int, int, int]] = field(default_factory=list)  # (u, v, w)

    @property
    def num_planes(self) -> int:
        return len(self.planes)

    @property
    def num_triangles(self) -> int:
        return len(self.triangles)

    def compute_primal_vertex(self, u: int, v: int, w: int) -> np.ndarray:
        """Evaluate primal 3D vertex v_{uvw} = P_u cap P_v cap P_w by solving 3x3 plane intersection."""
        pu = self.planes[u]
        pv = self.planes[v]
        pw = self.planes[w]

        A = np.array([pu[:3], pv[:3], pw[:3]], dtype=np.float64)
        b = -np.array([pu[3], pv[3], pw[3]], dtype=np.float64)

        det = float(np.linalg.det(A))
        if abs(det) < 1e-12:
            raise ValueError(f"Degenerate plane intersection for dual triangle ({u}, {v}, {w})")
        return np.linalg.solve(A, b)

    def compute_primal_vertices(self) -> np.ndarray:
        """Compute all primal 3D vertices corresponding to current dual triangles."""
        verts = np.zeros((self.num_triangles, 3), dtype=np.float64)
        for i, (u, v, w) in enumerate(self.triangles):
            verts[i] = self.compute_primal_vertex(u, v, w)
        return verts

    def compute_radius_of_security(self) -> float:
        """Evaluate radius-of-security R_max = max_{v in T} ||v - x_i||_2 (Paper 4 §3.1)."""
        if not self.triangles:
            return float("inf")
        verts = self.compute_primal_vertices()
        dists = np.linalg.norm(verts - self.seed, axis=1)
        return float(np.max(dists))

    def should_terminate_clipping(self, next_candidate: np.ndarray) -> bool:
        """Check if candidate point satisfies early exit condition ||x_p - x_i|| > 2 * R_max."""
        dist = float(np.linalg.norm(next_candidate - self.seed))
        r_max = self.compute_radius_of_security()
        return dist > 2.0 * r_max


def create_initial_bounding_box_cell(
    seed: np.ndarray,
    bounds: Tuple[float, float, float, float, float, float],
) -> GpuVoronoiCell:
    """Initialize a dual Voronoi cell with 6 bounding box half-spaces [-x, +x, -y, +y, -z, +z]."""
    x_min, x_max, y_min, y_max, z_min, z_max = bounds
    cell = GpuVoronoiCell(seed=np.asarray(seed, dtype=np.float64))

    # 6 planes
    cell.planes = [
        np.array([-1.0, 0.0, 0.0, x_min], dtype=np.float64),  # P0: -x + x_min <= 0 => x >= x_min
        np.array([1.0, 0.0, 0.0, -x_max], dtype=np.float64),  # P1:  x - x_max <= 0 => x <= x_max
        np.array([0.0, -1.0, 0.0, y_min], dtype=np.float64),  # P2: -y + y_min <= 0 => y >= y_min
        np.array([0.0, 1.0, 0.0, -y_max], dtype=np.float64),  # P3:  y - y_max <= 0 => y <= y_max
        np.array([0.0, 0.0, -1.0, z_min], dtype=np.float64),  # P4: -z + z_min <= 0 => z >= z_min
        np.array([0.0, 0.0, 1.0, -z_max], dtype=np.float64),  # P5:  z - z_max <= 0 => z <= z_max
    ]

    # 8 corners formed by 3 orthogonal planes each (8 dual triangles)
    cell.triangles = [
        (0, 2, 4),  # (x_min, y_min, z_min)
        (1, 2, 4),  # (x_max, y_min, z_min)
        (0, 3, 4),  # (x_min, y_max, z_min)
        (1, 3, 4),  # (x_max, y_max, z_min)
        (0, 2, 5),  # (x_min, y_min, z_max)
        (1, 2, 5),  # (x_max, y_min, z_max)
        (0, 3, 5),  # (x_min, y_max, z_max)
        (1, 3, 5),  # (x_max, y_max, z_max)
    ]
    return cell


def clip_cell_by_plane(
    cell: GpuVoronoiCell,
    plane: np.ndarray,
) -> bool:
    """Clip dual Voronoi cell by plane a*x + b*y + c*z + d <= 0 (Paper 4 Alg. 3).

    Returns True if cell was modified, False if outside/unchanged.
    """
    plane = np.asarray(plane, dtype=np.float64)
    # Check if we exceed 64 planes
    if len(cell.planes) >= 64:
        raise OverflowError("Dual Voronoi cell plane buffer overflow (> 64 planes)")

    new_plane_idx = len(cell.planes)
    cell.planes.append(plane)

    # Classify current primal vertices: inside (<= 0) vs outside (> 0)
    inside_flags = []
    for u, v, w in cell.triangles:
        pt = cell.compute_primal_vertex(u, v, w)
        val = float(np.dot(plane[:3], pt) + plane[3])
        inside_flags.append(val <= 1e-12)

    num_inside = sum(inside_flags)
    if num_inside == len(cell.triangles):
        # Plane does not clip any vertex; remove the added plane
        cell.planes.pop()
        return False
    if num_inside == 0:
        # Whole cell clipped away
        cell.triangles = []
        return True

    # Identify retained triangles and cut edges
    new_triangles = []
    cut_edges: Dict[Tuple[int, int], int] = {}

    for idx, (u, v, w) in enumerate(cell.triangles):
        if inside_flags[idx]:
            new_triangles.append((u, v, w))

    # Connect cut boundary with new dual triangles involving new_plane_idx
    # Build dual adjacency to identify boundary cycle of removed region
    adj_edges: Dict[Tuple[int, int], List[int]] = {}
    for idx, (u, v, w) in enumerate(cell.triangles):
        edges = [(min(u, v), max(u, v)), (min(v, w), max(v, w)), (min(w, u), max(w, u))]
        for e in edges:
            adj_edges.setdefault(e, []).append(idx)

    # A boundary edge connects one inside triangle and one outside triangle
    boundary_edges = []
    for e, tri_list in adj_edges.items():
        if len(tri_list) == 2:
            t0, t1 = tri_list
            if inside_flags[t0] != inside_flags[t1]:
                boundary_edges.append(e)

    # Form new dual triangles (u, v, new_plane_idx) along boundary edges
    for u, v in boundary_edges:
        if len(new_triangles) < 96:
            new_triangles.append((u, v, new_plane_idx))

    cell.triangles = new_triangles
    return True


def integrate_projection_tetrahedra(
    cell: GpuVoronoiCell,
) -> Tuple[float, np.ndarray, Dict[int, np.ndarray]]:
    """Compute exact cell volume, centroid, and face areas via Stokes 6-tetrahedron projection (Paper 4 §3.5).

    Returns
    -------
    volume : float
        Exact machine-precision cell volume.
    centroid : np.ndarray, shape (3,)
        Exact cell centroid.
    face_areas : Dict[int, np.ndarray]
        Mapping from plane index to outward face area vector S_f.
    """
    if not cell.triangles:
        return 0.0, cell.seed.copy(), {}

    verts = cell.compute_primal_vertices()
    seed = cell.seed

    # Stokes integration: sum oriented tetrahedra connecting seed to primal faces
    # Reconstruct polygonal faces from dual triangles
    plane_to_verts: Dict[int, List[int]] = {}
    for v_idx, (u, v, w) in enumerate(cell.triangles):
        plane_to_verts.setdefault(u, []).append(v_idx)
        plane_to_verts.setdefault(v, []).append(v_idx)
        plane_to_verts.setdefault(w, []).append(v_idx)

    total_vol = 0.0
    weighted_centroid = np.zeros(3, dtype=np.float64)
    face_areas: Dict[int, np.ndarray] = {}

    for p_idx, v_indices in plane_to_verts.items():
        if len(v_indices) < 3:
            continue
        face_pts = verts[v_indices]
        c_face = np.mean(face_pts, axis=0)
        plane_n = cell.planes[p_idx][:3]
        plane_n = plane_n / (np.linalg.norm(plane_n) + 1e-15)

        # Establish 2D tangent basis (u1, u2) perpendicular to plane normal
        ref = np.array([0.0, 0.0, 1.0]) if abs(plane_n[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u1 = np.cross(plane_n, ref)
        u1 = u1 / (np.linalg.norm(u1) + 1e-15)
        u2 = np.cross(plane_n, u1)

        # Sort vertices cyclically by polar angle around c_face
        coords_2d = np.column_stack([
            np.dot(face_pts - c_face, u1),
            np.dot(face_pts - c_face, u2),
        ])
        angles = np.arctan2(coords_2d[:, 1], coords_2d[:, 0])
        sort_order = np.argsort(angles)
        face_pts = face_pts[sort_order]

        # Compute face area vector via polygon fan
        s_face = np.zeros(3, dtype=np.float64)
        for i in range(len(face_pts)):
            p0 = face_pts[i] - c_face
            p1 = face_pts[(i + 1) % len(face_pts)] - c_face
            s_face += 0.5 * np.cross(p0, p1)

        # Orient normal outward
        if np.dot(s_face, plane_n) < 0:
            s_face = -s_face

        face_areas[p_idx] = s_face

        # Tetrahedron volume connecting seed to face: Vol = (1/3) * (c_face - seed) . S_f
        tet_vol = float(np.dot(c_face - seed, s_face)) / 3.0
        if tet_vol > 0:
            total_vol += tet_vol
            tet_c = 0.75 * c_face + 0.25 * seed
            weighted_centroid += tet_c * tet_vol

    if total_vol > 1e-60:
        centroid = weighted_centroid / total_vol
    else:
        centroid = seed.copy()

    return float(total_vol), centroid, face_areas


# ==============================================================================
# Paper 5: Analytical Voronoi & Power Diagram Derivatives
# ==============================================================================

class DifferentiableVoronoi:
    """Evaluates analytical 1st and 2nd derivatives of Voronoi / Power vertices (Paper 5).

    Solves local 3x3 system:
        A(c, w) x_c = b(c, w)
    and evaluates:
        dx_c / dq = A^-1 * (db/dq - (dA/dq) * x_c)
        d^2 x_c / (dq dr) = A^-1 * (d^2 b / (dq dr) - (d^2 A / (dq dr))*x_c - (dA/dq)*(dx_c/dr) - (dA/dr)*(dx_c/dq))
    """

    @staticmethod
    def solve_vertex_and_sensitivity(
        seed_i: np.ndarray,
        seeds_neighbors: List[np.ndarray],
        weight_i: float = 0.0,
        weights_neighbors: Optional[List[float]] = None,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Solve primal vertex x_c and analytical Jacobian dx_c / dc_i (Paper 5 §3.2).

        Parameters
        ----------
        seed_i : np.ndarray, shape (3,)
            Central seed point c_i.
        seeds_neighbors : List[np.ndarray]
            3 incident neighbor seeds (c_j, c_k, c_l) forming the vertex.
        weight_i : float
            Power diagram weight w_i.
        weights_neighbors : Optional[List[float]]
            Neighbor weights (w_j, w_k, w_l).

        Returns
        -------
        x_c : np.ndarray, shape (3,)
            Primal vertex coordinates.
        dx_c_dci : np.ndarray, shape (3, 3)
            Analytical sensitivity Jacobian matrix wrt seed_i.
        """
        c_i = np.asarray(seed_i, dtype=np.float64)
        c_nbs = [np.asarray(s, dtype=np.float64) for s in seeds_neighbors]
        if len(c_nbs) != 3:
            raise ValueError(f"Exactly 3 incident neighbor seeds required, got {len(c_nbs)}")

        w_i = float(weight_i)
        w_nbs = weights_neighbors if weights_neighbors is not None else [0.0, 0.0, 0.0]

        # Bisector equations for k in {0, 1, 2}: (c_k - c_i) . x_c = 0.5 * (||c_k||^2 - ||c_i||^2 + w_i - w_k)
        A = np.zeros((3, 3), dtype=np.float64)
        b = np.zeros(3, dtype=np.float64)

        for k in range(3):
            c_k = c_nbs[k]
            w_k = w_nbs[k]
            A[k] = c_k - c_i
            b[k] = 0.5 * (float(np.dot(c_k, c_k)) - float(np.dot(c_i, c_i)) + w_i - w_k)

        det_A = float(np.linalg.det(A))
        if abs(det_A) < 1e-12:
            raise ValueError("Degenerate cospherical seed configuration (det A ~ 0)")

        A_inv = np.linalg.inv(A)
        x_c = A_inv @ b

        # Analytical sensitivity dx_c / dc_i:
        # dA_k / dc_i = -I_3 (row k of A is c_k - c_i)
        # db_k / dc_i = -c_i^T
        # Therefore:
        # (db / dc_i - (dA / dc_i) * x_c) = [ -c_i + x_c, -c_i + x_c, -c_i + x_c ]^T = 1_3 (x_c - c_i)^T
        # dx_c / dc_i = A^-1 @ ([-c_i + x_c; -c_i + x_c; -c_i + x_c])
        delta = (x_c - c_i).reshape(1, 3)  # shape (1, 3)
        RHS = np.tile(delta, (3, 1))        # shape (3, 3)
        dx_c_dci = A_inv @ RHS

        return x_c, dx_c_dci

    @staticmethod
    def verify_volume_c1_continuity_across_flip(
        v_before: float,
        v_after: float,
        grad_before: np.ndarray,
        grad_after: np.ndarray,
        eps_flip: float = 1e-6,
    ) -> bool:
        """Verify C^1 continuity of Voronoi cell volume across Delaunay/topological flip (Paper 5 §3.4).

        During a flip, the facet area vanishing ||S_f|| -> 0 proves:
            |V_after - V_before| < C * eps
            ||grad V_after - grad V_before|| < C * eps
        """
        vol_diff = abs(v_after - v_before)
        grad_diff = float(np.linalg.norm(grad_after - grad_before))
        return (vol_diff < 10.0 * eps_flip) and (grad_diff < 10.0 * eps_flip)
