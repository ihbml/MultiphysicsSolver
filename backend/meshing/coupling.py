"""Hybrid Eulerian SPH-FVM Coupling & OPMIS Taylor Interpolation.

Implements Features 12, 13, and 14 from PROJECT.md:
- Feature 12: Incomplete SPH kernel support detection via position divergence:
  div r_i = -sum_j V_j (r_i - r_j) . grad_i W_ij < 0.75 * d  (Wang et al., 2025)
- Feature 13: Virtual FVM wall boundary area vector:
  A_iS = -sum_j V_j grad_i W_ij achieving exact 0th-order consistency:
  ||A_iS + sum_j V_j grad_i W_ij|| < 10^-10
- Feature 14: Adaptive OPMIS partitioning & moment-reproducing Taylor interpolation:
  * P2M: Moment-corrected particle gradient reconstruction reproducing linear fields (L_inf < 10^-11)
  * M2P: Affine weighted least-squares reproducing arbitrary linear fields (L_inf < 10^-11)
  (Zhang, Ming, Wang & Liang, 2025)
"""

from __future__ import annotations

import math
from typing import Any, Callable, Dict, List, Optional, Tuple, Union

import numpy as np
from scipy.spatial import cKDTree

from backend.meshing.models import SPHParticleSet, UnstructuredMesh


# ---------------------------------------------------------------------------
# SPH Kernel Functions & Derivatives (2D and 3D)
# ---------------------------------------------------------------------------

class SPHKernel:
    """Wendland C2 and Cubic Spline kernel evaluator in 2D and 3D."""

    def __init__(self, kernel_type: str = "wendland", support_factor: float = 2.85) -> None:
        """Initialize kernel.

        Parameters
        ----------
        kernel_type : str
            "wendland" (Wendland C2) or "cubic" (Monaghan cubic spline).
        support_factor : float
            Multiplier kappa relating smoothing length h to compact support cutoff radius R = kappa * h.
            Default is 2.85 for Wendland C2.
        """
        self.kernel_type = kernel_type.lower()
        self.support_factor = float(support_factor)

    def normalization_constant(self, h: float, dim: int) -> float:
        """Compute kernel normalization constant alpha_d such that integral W dV = 1."""
        R = self.support_factor * h
        if self.kernel_type == "wendland":
            if dim == 2:
                # 2D Wendland C2: alpha_2 = 7 / (pi * R^2)
                return 7.0 / (math.pi * (R ** 2))
            elif dim == 3:
                # 3D Wendland C2: alpha_3 = 21 / (2 * pi * R^3)
                return 21.0 / (2.0 * math.pi * (R ** 3))
            else:
                raise ValueError(f"Unsupported spatial dimension: {dim}")
        elif self.kernel_type == "cubic":
            if dim == 2:
                # 2D Cubic spline with support 2h: alpha_2 = 10 / (7 * pi * h^2)
                return 10.0 / (7.0 * math.pi * (h ** 2))
            elif dim == 3:
                # 3D Cubic spline with support 2h: alpha_3 = 1 / (pi * h^3)
                return 1.0 / (math.pi * (h ** 3))
            else:
                raise ValueError(f"Unsupported spatial dimension: {dim}")
        else:
            raise ValueError(f"Unknown kernel type: {self.kernel_type}")

    def evaluate(self, r: Union[float, np.ndarray], h: float, dim: int = 3) -> Union[float, np.ndarray]:
        """Evaluate radial kernel W(r, h)."""
        r_arr = np.asarray(r, dtype=np.float64)
        scalar_input = r_arr.ndim == 0
        r_flat = np.atleast_1d(r_arr)

        R = self.support_factor * h
        alpha = self.normalization_constant(h, dim)
        w = np.zeros_like(r_flat)

        if self.kernel_type == "wendland":
            mask = (r_flat >= 0.0) & (r_flat < R)
            if np.any(mask):
                q = r_flat[mask] / R
                w[mask] = alpha * ((1.0 - q) ** 4) * (1.0 + 4.0 * q)
        elif self.kernel_type == "cubic":
            q = r_flat / h
            m1 = (q >= 0.0) & (q < 1.0)
            m2 = (q >= 1.0) & (q < 2.0)
            if np.any(m1):
                w[m1] = alpha * (1.0 - 1.5 * (q[m1] ** 2) + 0.75 * (q[m1] ** 3))
            if np.any(m2):
                w[m2] = alpha * 0.25 * ((2.0 - q[m2]) ** 3)

        return float(w[0]) if scalar_input else w

    def derivative(self, r: Union[float, np.ndarray], h: float, dim: int = 3) -> Union[float, np.ndarray]:
        """Evaluate spatial derivative dW/dr(r, h). Returns negative or zero."""
        r_arr = np.asarray(r, dtype=np.float64)
        scalar_input = r_arr.ndim == 0
        r_flat = np.atleast_1d(r_arr)

        R = self.support_factor * h
        alpha = self.normalization_constant(h, dim)
        dw = np.zeros_like(r_flat)

        if self.kernel_type == "wendland":
            mask = (r_flat > 1e-15) & (r_flat < R)
            if np.any(mask):
                q = r_flat[mask] / R
                dw[mask] = -20.0 * (alpha / R) * q * ((1.0 - q) ** 3)
        elif self.kernel_type == "cubic":
            q = r_flat / h
            m1 = (q > 1e-15) & (q < 1.0)
            m2 = (q >= 1.0) & (q < 2.0)
            if np.any(m1):
                dw[m1] = (alpha / h) * (-3.0 * q[m1] + 2.25 * (q[m1] ** 2))
            if np.any(m2):
                dw[m2] = -0.75 * (alpha / h) * ((2.0 - q[m2]) ** 2)

        return float(dw[0]) if scalar_input else dw

    def gradient(self, r_vec: np.ndarray, h: float, dim: int = 3) -> np.ndarray:
        """Evaluate spatial gradient grad_i W_ij = (dW/dr) * (r_vec / r).

        Parameters
        ----------
        r_vec : np.ndarray
            Displacement vector r_i - r_j of shape (..., dim).
        h : float
            Smoothing length.
        dim : int
            Spatial dimension (2 or 3).

        Returns
        -------
        grad_W : np.ndarray
            Gradient vector pointing away from r_j towards r_i (direction -(r_i - r_j)).
        """
        r_vec = np.asarray(r_vec, dtype=np.float64)
        r = np.linalg.norm(r_vec, axis=-1, keepdims=True)
        dw = self.derivative(r[..., 0], h, dim)[..., np.newaxis]
        safe_r = np.maximum(r, 1e-15)
        grad = np.where(r > 1e-15, dw * (r_vec / safe_r), 0.0)
        return grad


# ---------------------------------------------------------------------------
# SPHFVMCoupler: Divergence of Position & Virtual Wall Vector A_iS
# ---------------------------------------------------------------------------

class SPHFVMCoupler:
    """Hybrid Eulerian SPH-FVM interface coupler (Wang, Haidn & Hu, 2025).

    Detects truncated kernel support near boundaries via divergence of position
    and constructs virtual wall area normal vectors restoring 0th-order consistency.
    """

    def __init__(
        self,
        kernel: str = "wendland",
        support_factor: float = 2.85,
        boundary_threshold_ratio: float = 0.75,
    ) -> None:
        """Initialize SPH-FVM coupler.

        Parameters
        ----------
        kernel : str
            Kernel type: "wendland" (Wendland C2) or "cubic" (Cubic spline).
        support_factor : float
            Cutoff radius multiplier R = support_factor * h (default: 2.85).
        boundary_threshold_ratio : float
            Threshold factor for boundary detection: div r < boundary_threshold_ratio * d.
            Default is 0.75 as established by Wang et al. (2025).
        """
        self.kernel = SPHKernel(kernel_type=kernel, support_factor=support_factor)
        self.support_factor = float(support_factor)
        self.boundary_threshold_ratio = float(boundary_threshold_ratio)

    def compute_position_divergence(
        self,
        pos: np.ndarray,
        vol: np.ndarray,
        h: float,
        dim: Optional[int] = None,
    ) -> np.ndarray:
        """Compute the discrete position divergence div r_i for all particles.

        div r_i = -sum_{j in N(i)} V_j (r_i - r_j) . grad_i W_ij

        In the interior of a continuum domain with full kernel support,
        div r_i ~ d (where d is the dimension: 2 in 2D, 3 in 3D).
        Near physical boundaries, kernel truncation causes div r_i to drop
        significantly below d (e.g. ~ 0.5 * d on flat walls).

        Parameters
        ----------
        pos : np.ndarray
            Particle positions, shape (N, dim).
        vol : np.ndarray
            Particle volumes V_j, shape (N,).
        h : float
            Smoothing length.
        dim : Optional[int]
            Spatial dimension (inferred from pos if None).

        Returns
        -------
        div_r : np.ndarray
            Discrete position divergence for each particle, shape (N,).
        """
        pos = np.asarray(pos, dtype=np.float64)
        vol = np.asarray(vol, dtype=np.float64)
        n_particles, d = pos.shape
        if dim is not None:
            d = int(dim)

        R = self.support_factor * h
        tree = cKDTree(pos)
        div_r = np.zeros(n_particles, dtype=np.float64)

        # Efficient spatial neighbor query via cKDTree
        for i in range(n_particles):
            neighbors = tree.query_ball_point(pos[i], R)
            if len(neighbors) <= 1:
                continue

            nbr_pos = pos[neighbors]
            nbr_vol = vol[neighbors]

            # Vector displacement r_i - r_j
            disp = pos[i] - nbr_pos
            dist = np.linalg.norm(disp, axis=1)

            valid = (dist > 1e-15) & (dist < R)
            if not np.any(valid):
                continue

            valid_dist = dist[valid]
            valid_disp = disp[valid]
            valid_vol = nbr_vol[valid]

            dw = self.kernel.derivative(valid_dist, h, d)
            # grad_i W_ij = (dw / r) * (r_i - r_j)
            # - (r_i - r_j) . grad_i W_ij = - r * dw (since dw <= 0, this is positive)
            contributions = -valid_vol * valid_dist * dw
            div_r[i] = np.sum(contributions)

        return div_r

    def detect_boundary_particles(
        self,
        pos: np.ndarray,
        vol: np.ndarray,
        h: float,
        dim: Optional[int] = None,
    ) -> np.ndarray:
        """Detect particles with truncated kernel support near physical boundaries.

        Criterion (Wang, Haidn & Hu, 2025):
            is_boundary = div r_i < 0.75 * d

        Parameters
        ----------
        pos : np.ndarray
            Particle positions, shape (N, dim).
        vol : np.ndarray
            Particle volumes, shape (N,).
        h : float
            Smoothing length.
        dim : Optional[int]
            Spatial dimension.

        Returns
        -------
        is_boundary : np.ndarray
            Boolean array of shape (N,), True for boundary particles.
        """
        pos = np.asarray(pos, dtype=np.float64)
        d = int(dim) if dim is not None else pos.shape[1]
        div_r = self.compute_position_divergence(pos, vol, h, dim=d)
        threshold = self.boundary_threshold_ratio * d
        return div_r < threshold

    def compute_virtual_wall_vector(
        self,
        pos: np.ndarray,
        vol: np.ndarray,
        h: float,
        return_sum_grad: bool = False,
        dim: Optional[int] = None,
    ) -> Union[np.ndarray, Tuple[np.ndarray, np.ndarray]]:
        """Compute the virtual FVM wall boundary area vector A_iS.

        Wang, Haidn & Hu (2025):
            A_iS = -sum_j V_j grad_i W_ij  for boundary particles (div r < 0.75 * d)
            A_iS ~ 0                       for interior particles (full support)

        Exact 0th-order consistency invariant:
            ||A_iS + sum_j V_j grad_i W_ij|| < 10^-10 on boundary particles.

        Parameters
        ----------
        pos : np.ndarray
            Particle positions, shape (N, dim).
        vol : np.ndarray
            Particle volumes, shape (N,).
        h : float
            Smoothing length.
        return_sum_grad : bool
            If True, also returns the kernel gradient sum array.
        dim : Optional[int]
            Spatial dimension.

        Returns
        -------
        A_iS : np.ndarray
            Virtual wall area vector for each particle, shape (N, dim).
        sum_grad_W : np.ndarray (optional)
            Kernel gradient sum for each particle, shape (N, dim).
        """
        pos = np.asarray(pos, dtype=np.float64)
        vol = np.asarray(vol, dtype=np.float64)
        n_particles, d = pos.shape
        if dim is not None:
            d = int(dim)

        R = self.support_factor * h
        tree = cKDTree(pos)

        sum_grad_W = np.zeros_like(pos)
        div_r = np.zeros(n_particles, dtype=np.float64)

        for i in range(n_particles):
            neighbors = tree.query_ball_point(pos[i], R)
            if len(neighbors) <= 1:
                continue

            nbr_pos = pos[neighbors]
            nbr_vol = vol[neighbors]

            disp = pos[i] - nbr_pos
            dist = np.linalg.norm(disp, axis=1)

            valid = (dist > 1e-15) & (dist < R)
            if not np.any(valid):
                continue

            valid_dist = dist[valid]
            valid_disp = disp[valid]
            valid_vol = nbr_vol[valid]

            dw = self.kernel.derivative(valid_dist, h, d)
            # grad_i W_ij = (dw / r) * (r_i - r_j)
            grad_W = (dw / valid_dist)[:, np.newaxis] * valid_disp
            weighted_grad = valid_vol[:, np.newaxis] * grad_W

            sum_grad_W[i] = np.sum(weighted_grad, axis=0)
            div_r[i] = -np.sum(valid_vol * np.sum(valid_disp * grad_W, axis=1))

        # Identify boundary particles
        threshold = self.boundary_threshold_ratio * d
        is_boundary = div_r < threshold

        # Virtual wall vector A_iS:
        # Exactly cancels the gradient sum for boundary particles, zero for interior
        A_iS = np.zeros_like(sum_grad_W)
        A_iS[is_boundary] = -sum_grad_W[is_boundary]

        if return_sum_grad:
            return A_iS, sum_grad_W
        return A_iS

    def compute_kernel_gradient_matrix(
        self,
        pos: np.ndarray,
        vol: np.ndarray,
        h: float,
        dim: Optional[int] = None,
    ) -> np.ndarray:
        """Compute 1st-order kernel gradient correction tensor B_i = M_i^-1.

        M_i = sum_j V_j (x_j - x_i) (x) grad_i W_ij
        """
        pos = np.asarray(pos, dtype=np.float64)
        vol = np.asarray(vol, dtype=np.float64)
        n_particles, d = pos.shape
        if dim is not None:
            d = int(dim)

        R = self.support_factor * h
        tree = cKDTree(pos)
        B_mats = np.zeros((n_particles, d, d), dtype=np.float64)

        for i in range(n_particles):
            neighbors = tree.query_ball_point(pos[i], R)
            M = np.zeros((d, d), dtype=np.float64)
            for j in neighbors:
                if i == j:
                    continue
                disp = pos[j] - pos[i]
                r = float(np.linalg.norm(disp))
                if r < 1e-15 or r >= R:
                    continue
                dw = self.kernel.derivative(r, h, d)
                grad_W = (dw / r) * (pos[i] - pos[j])
                # M_i += V_j (x_j - x_i) (x) grad_i W_ij
                M += vol[j] * np.outer(disp, grad_W)

            det = np.linalg.det(M)
            if abs(det) > 1e-14:
                B_mats[i] = np.linalg.inv(M)
            else:
                B_mats[i] = np.eye(d)

        return B_mats


# ---------------------------------------------------------------------------
# OPMISTaylorInterpolator: Moment-Reproducing P2M & Affine M2P
# ---------------------------------------------------------------------------

class OPMISTaylorInterpolator:
    """Adaptive SPH-FVM Taylor & Affine Interpolator (Zhang et al., 2025 OPMIS).

    Guarantees exact linear field reproduction f(x) = a . x + b with L_inf < 10^-11
    in both particle-to-mesh (P2M) and mesh-to-particle (M2P) directions.
    """

    def __init__(self, k_neighbors_p2m: int = 16, k_neighbors_m2p: int = 12) -> None:
        self.k_neighbors_p2m = int(k_neighbors_p2m)
        self.k_neighbors_m2p = int(k_neighbors_m2p)

    def interpolate_p2m(
        self,
        pos: np.ndarray,
        field_particles: np.ndarray,
        vol: np.ndarray,
        h: float,
        mesh: UnstructuredMesh,
        k_neighbors: Optional[int] = None,
    ) -> np.ndarray:
        """Particle-to-Mesh (P2M) Taylor Interpolation.

        Evaluates particle gradients grad phi_i via the local moment matrix:
            M_{p, i} = sum_j w_ij (x_j - x_i) (x) (x_j - x_i)
            grad phi_i = M_{p, i}^-1 sum_j w_ij (x_j - x_i) (phi_j - phi_i)

        And reconstructs field values at mesh cell centroids:
            phi(x_c) = 1/|P_c| sum_{i in P_c} [ phi_i + grad phi_i . (x_c - x_i) ]

        Mathematical Invariant:
            For any linear field f(x) = a . x + b, grad phi_i == a exactly,
            and phi(x_c) == f(x_c) to machine precision (L_inf < 10^-11).

        Parameters
        ----------
        pos : np.ndarray
            Particle positions, shape (N, dim).
        field_particles : np.ndarray
            Scalar or vector field values on particles, shape (N,) or (N, D).
        vol : np.ndarray
            Particle volumes, shape (N,).
        h : float
            Smoothing length.
        mesh : UnstructuredMesh
            Target FVM mesh.
        k_neighbors : Optional[int]
            Number of neighbor particles for Taylor reconstruction.

        Returns
        -------
        field_mesh : np.ndarray
            Interpolated field values at mesh cell centroids, shape (N_c,) or (N_c, D).
        """
        pos = np.asarray(pos, dtype=np.float64)
        field_particles = np.asarray(field_particles, dtype=np.float64)
        n_particles, dim = pos.shape
        k_p2m = k_neighbors if k_neighbors is not None else self.k_neighbors_p2m
        k_query = min(max(k_p2m, dim + 5), n_particles)

        tree = cKDTree(pos)
        is_vector = field_particles.ndim == 2
        d_val = field_particles.shape[1] if is_vector else 1
        phi_2d = field_particles if is_vector else field_particles[:, np.newaxis]

        # 1. Compute moment-corrected particle gradients grad phi_i
        gradients = np.zeros((n_particles, dim, d_val), dtype=np.float64)

        for i in range(n_particles):
            dists, idxs = tree.query(pos[i], k=k_query)
            dists = np.atleast_1d(dists)
            idxs = np.atleast_1d(idxs)

            disp = pos[idxs] - pos[i]
            # Weights avoiding zero at r=0
            weights = 1.0 / (dists ** 2 + 1e-8)
            W_mat = weights[:, np.newaxis]

            # Weighted linear least-squares moment matrix: X^T W X
            XtW = disp.T * weights[np.newaxis, :]  # (dim, K)
            M = XtW @ disp  # (dim, dim)

            # Differences in scalar/vector values: delta phi_j = phi_j - phi_i
            delta_phi = phi_2d[idxs] - phi_2d[i]  # (K, d_val)
            RHS = XtW @ delta_phi  # (dim, d_val)

            try:
                # Regularized inversion to handle collinear boundary configurations
                grad_i = np.linalg.solve(M + 1e-12 * np.eye(dim), RHS)
            except np.linalg.LinAlgError:
                grad_i, _, _, _ = np.linalg.lstsq(M, RHS, rcond=1e-10)

            gradients[i] = grad_i

        # 2. Reconstruct field at mesh cell centroids
        cell_cc = mesh.cell_centres
        n_cells = len(cell_cc)
        k_eval = min(8, n_particles)
        _, cell_nbr_idxs = tree.query(cell_cc, k=k_eval)

        field_mesh = np.zeros((n_cells, d_val), dtype=np.float64)

        for c in range(n_cells):
            nbrs = np.atleast_1d(cell_nbr_idxs[c])
            xc = cell_cc[c]

            reconstructions = []
            for pi in nbrs:
                dx = xc - pos[pi]  # (dim,)
                # phi_i + grad phi_i . (xc - xi)
                # gradients[pi] is (dim, d_val)
                val_rec = phi_2d[pi] + (dx @ gradients[pi])  # (d_val,)
                reconstructions.append(val_rec)

            field_mesh[c] = np.mean(reconstructions, axis=0)

        if not is_vector:
            return field_mesh[:, 0]
        return field_mesh

    def interpolate_m2p(
        self,
        mesh: UnstructuredMesh,
        field_mesh: np.ndarray,
        query_points: np.ndarray,
        k_neighbors: Optional[int] = None,
    ) -> np.ndarray:
        """Mesh-to-Particle (M2P) Affine Weighted Least-Squares Interpolation.

        Fits an affine reproducing moving least squares polynomial around each
        query point x_p using the centroids and values of surrounding mesh cells:
            min sum_{k in M_p} w_k ( c_0 + g . (x_k - x_p) - phi_k )^2

        The constant term c_0 is the exact reproduced field value at x_p.

        Mathematical Invariant:
            For any linear field f(x) = a . x + b, c_0 == f(x_p) with L_inf < 10^-11.

        Parameters
        ----------
        mesh : UnstructuredMesh
            Source FVM mesh.
        field_mesh : np.ndarray
            Field values at mesh cell centroids, shape (N_c,) or (N_c, D).
        query_points : np.ndarray
            Target query positions, shape (M, dim).
        k_neighbors : Optional[int]
            Number of neighboring cell centroids to include in affine stencil.

        Returns
        -------
        field_query : np.ndarray
            Interpolated field values at query points, shape (M,) or (M, D).
        """
        query_points = np.asarray(query_points, dtype=np.float64)
        field_mesh = np.asarray(field_mesh, dtype=np.float64)
        n_queries, dim = query_points.shape
        cell_cc = mesh.cell_centres
        n_cells = len(cell_cc)

        k_m2p = k_neighbors if k_neighbors is not None else self.k_neighbors_m2p
        k_query = min(max(k_m2p, dim + 4), n_cells)

        tree = cKDTree(cell_cc)
        is_vector = field_mesh.ndim == 2
        d_val = field_mesh.shape[1] if is_vector else 1
        f_mesh_2d = field_mesh if is_vector else field_mesh[:, np.newaxis]

        field_query = np.zeros((n_queries, d_val), dtype=np.float64)
        dists, idxs = tree.query(query_points, k=k_query)

        for p in range(n_queries):
            xp = query_points[p]
            nbr_indices = np.atleast_1d(idxs[p])
            nbr_cc = cell_cc[nbr_indices]

            disp = nbr_cc - xp  # shape (K, dim)
            dist_sq = np.sum(disp ** 2, axis=1)

            # Inverse distance squared weights
            weights = 1.0 / (dist_sq + 1e-8)
            W_sqrt = np.sqrt(weights)[:, np.newaxis]

            # Affine design matrix: [1, dx_1, dx_2, ...] of shape (K, dim + 1)
            A = np.column_stack([np.ones(len(disp)), disp])
            Aw = A * W_sqrt
            yw = f_mesh_2d[nbr_indices] * W_sqrt

            # Solve weighted least-squares
            try:
                coeff, _, _, _ = np.linalg.lstsq(Aw, yw, rcond=1e-12)
                # First coefficient corresponds to column of 1s (value at xp)
                field_query[p] = coeff[0]
            except np.linalg.LinAlgError:
                # Fallback to Shepard weighting
                total_w = np.sum(weights)
                field_query[p] = np.sum(f_mesh_2d[nbr_indices] * weights[:, np.newaxis], axis=0) / max(total_w, 1e-15)

        if not is_vector:
            return field_query[:, 0]
        return field_query
