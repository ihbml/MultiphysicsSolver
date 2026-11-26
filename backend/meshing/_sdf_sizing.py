"""Curvature, proximity, and graded adaptive element sizing fields."""

from __future__ import annotations

from enum import Enum
from typing import Any, Callable, Dict, Optional, Tuple
import numpy as np
from scipy.spatial import cKDTree

from backend.meshing.models import SizingParameters


class CurvatureMethod(str, Enum):
    """Method for evaluating surface curvature."""

    SDF_LAPLACIAN = "sdf_laplacian"
    SURFACE_MESH = "surface_mesh"
    HYBRID = "hybrid"
    WEINGARTEN = "weingarten"


class CurvatureRefinement:
    """Computes continuous curvature kappa(x) and curvature-adapted size h_curv(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
        method: CurvatureMethod = CurvatureMethod.HYBRID,
        feature_graph: Optional[Any] = None,
    ) -> None:
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim
        self.surface_mesh = surface_mesh
        self.method = CurvatureMethod(method)
        self.feature_graph = feature_graph
        self.wall_curvature_field: Optional[Any] = None
        self._mesh_kdtree: Optional[Any] = None
        self._mesh_vertex_curvatures: Optional[np.ndarray] = None

        if self.surface_mesh is not None:
            self._precompute_surface_curvatures()

    def _precompute_surface_curvatures(self) -> None:
        """Precompute vertex curvatures on triangulated SurfaceMesh."""
        mesh = self.surface_mesh
        verts = mesh.vertices
        faces = mesh.faces

        if self.method == CurvatureMethod.WEINGARTEN:
            from backend.meshing.feature_graph import extract_feature_graph
            from backend.meshing.weingarten import compute_weingarten_curvature

            if self.feature_graph is None:
                self.feature_graph = extract_feature_graph(
                    verts,
                    faces,
                    h_min=self.params.h_min,
                    patches=getattr(mesh, "patches", None),
                )
            field = compute_weingarten_curvature(
                verts,
                faces,
                feature_graph=self.feature_graph,
                h_min=self.params.h_min,
            )
            self.wall_curvature_field = field
            self._mesh_vertex_curvatures = np.abs(field.mean_curvature)
            self._mesh_kdtree = cKDTree(verts)
            return

        normals = mesh.normals

        n_verts = len(verts)
        vert_normals = np.zeros((n_verts, 3), dtype=np.float64)
        np.add.at(vert_normals, faces[:, 0], normals)
        np.add.at(vert_normals, faces[:, 1], normals)
        np.add.at(vert_normals, faces[:, 2], normals)
        norms = np.linalg.norm(vert_normals, axis=1, keepdims=True)
        norms[norms < 1e-15] = 1.0
        vert_normals /= norms

        curv = np.zeros(n_verts, dtype=np.float64)
        degree = np.zeros(n_verts, dtype=np.float64)

        for edge_a, edge_b in ((0, 1), (1, 2), (2, 0)):
            va = faces[:, edge_a]
            vb = faces[:, edge_b]
            dn = np.linalg.norm(vert_normals[va] - vert_normals[vb], axis=1)
            dx = np.linalg.norm(verts[va] - verts[vb], axis=1)
            dx[dx < 1e-15] = 1e-15
            local_k = dn / dx
            np.add.at(curv, va, local_k)
            np.add.at(curv, vb, local_k)
            np.add.at(degree, va, 1.0)
            np.add.at(degree, vb, 1.0)

        degree[degree < 1.0] = 1.0
        self._mesh_vertex_curvatures = curv / degree
        self._mesh_kdtree = cKDTree(verts)

    def evaluate_curvature(self, points: np.ndarray) -> np.ndarray:
        """Evaluate curvature magnitude kappa(x) for an array of coordinates."""
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        if (
            self.method == CurvatureMethod.WEINGARTEN
            and self._mesh_kdtree is not None
            and self._mesh_vertex_curvatures is not None
        ):
            dists, idxs = self._mesh_kdtree.query(pts, k=1)
            mesh_k = self._mesh_vertex_curvatures[idxs]
            decay = 1.0 / (1.0 + dists / self.params.h0)
            kappa = mesh_k * decay
            return kappa[0] if single_point else kappa

        n_pts, d = pts.shape
        delta = self.params.fd_delta

        phi_0_eval, _ = self.sdf_evaluator(pts)
        phi_0 = np.atleast_1d(phi_0_eval)
        laplacian = np.zeros(n_pts, dtype=np.float64)

        for axis in range(d):
            e_k = np.zeros((1, d), dtype=np.float64)
            e_k[0, axis] = delta
            phi_pos, _ = self.sdf_evaluator(pts + e_k)
            phi_neg, _ = self.sdf_evaluator(pts - e_k)
            diff2 = np.atleast_1d(phi_pos) - 2.0 * phi_0 + np.atleast_1d(phi_neg)
            diff2 = np.where(np.abs(diff2) < 1e-15, 0.0, diff2)
            laplacian += diff2 / (delta * delta)

        kappa = np.abs(laplacian) if d == 2 else 0.5 * np.abs(laplacian)

        if self._mesh_kdtree is not None and self._mesh_vertex_curvatures is not None:
            dists, idxs = self._mesh_kdtree.query(pts, k=1)
            mesh_k = self._mesh_vertex_curvatures[idxs]
            decay = 1.0 / (1.0 + dists / self.params.h0)
            kappa = np.maximum(kappa, mesh_k * decay)

        return kappa[0] if single_point else kappa

    def evaluate_h_curv(self, points: np.ndarray) -> np.ndarray:
        """Compute local curvature-adapted element size h_curv(x)."""
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        kappa = self.evaluate_curvature(pts)
        denom = np.maximum(kappa, self.params.eps_curv)
        h_curv = self.params.alpha / denom
        h_clipped = np.clip(h_curv, self.params.h_min, self.params.h_max)

        return h_clipped[0] if single_point else h_clipped


class ProximityRefinement:
    """Detects opposing walls and computes proximity-adapted size h_prox(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        domain_is_negative: Optional[bool] = None,
        inside_is_negative: Optional[bool] = None,
    ) -> None:
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim
        if inside_is_negative is not None and domain_is_negative is None:
            self.domain_is_negative = not inside_is_negative
        elif domain_is_negative is not None:
            self.domain_is_negative = domain_is_negative
        else:
            eval_target = getattr(self.sdf_evaluator, "__self__", None)
            if eval_target is not None and getattr(eval_target, "inside_is_negative", None) is True:
                self.domain_is_negative = True
            else:
                self.domain_is_negative = None

    def evaluate_gap_distance(self, points: np.ndarray) -> np.ndarray:
        """Compute narrow gap distance d_gap(x) along the normal ray."""
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape

        phi_0_eval, grad_0 = self.sdf_evaluator(pts)
        phi_0 = np.atleast_1d(phi_0_eval)

        is_external = np.zeros(n_pts, dtype=bool)
        for i in range(n_pts):
            if self.domain_is_negative is True:
                is_external[i] = False
            elif self.domain_is_negative is False:
                is_external[i] = True
            else:
                is_external[i] = (phi_0[i] >= 0.0)

        for i in range(n_pts):
            if np.linalg.norm(grad_0[i]) < 1e-6:
                delta = 1e-4
                best_g = None
                best_diff = -float("inf")
                for axis in range(d):
                    for sign_step in (-1.0, 1.0):
                        step_pt = np.copy(pts[i : i + 1])
                        step_pt[0, axis] += sign_step * delta
                        phi_step, grad_step = self.sdf_evaluator(step_pt)
                        g_step = grad_step[0]
                        g_norm = np.linalg.norm(g_step)
                        if g_norm > 1e-6:
                            p_val = float(np.atleast_1d(phi_step)[0])
                            diff = (phi_0[i] - p_val) if is_external[i] else (p_val - phi_0[i])
                            if diff > best_diff:
                                best_diff = diff
                                best_g = g_step / g_norm
                if best_g is not None:
                    grad_0[i] = best_g

        grad_norms = np.linalg.norm(grad_0, axis=1, keepdims=True)
        grad_norms[grad_norms < 1e-12] = 1.0
        n_start = grad_0 / grad_norms

        r_max = float(self.params.max_gap_search or (self.params.h_max / self.params.beta))
        n_steps = max(24, self.params.ray_march_steps * 3)

        u_samples = np.linspace(1.0 / n_steps, 1.0, n_steps)
        t_samples = (u_samples**2) * r_max

        v_ray = np.where(is_external[:, None], n_start, -n_start)
        ray_pts = pts[:, None, :] + (t_samples[None, :, None]) * v_ray[:, None, :]
        flat_ray_pts = ray_pts.reshape(-1, d)

        phi_ray_flat, grad_ray_flat = self.sdf_evaluator(flat_ray_pts)
        phi_ray = np.atleast_1d(phi_ray_flat).reshape(n_pts, n_steps)
        grad_ray = grad_ray_flat.reshape(n_pts, n_steps, d)

        d_near = np.abs(phi_0)
        d_gap = np.full(n_pts, r_max, dtype=np.float64)

        for i in range(n_pts):
            if self.domain_is_negative is True and phi_0[i] > 0.0:
                continue
            if self.domain_is_negative is False and phi_0[i] < 0.0:
                continue

            crossings = np.where(phi_ray[i] <= 0.0)[0] if is_external[i] else np.where(phi_ray[i] >= 0.0)[0]
            if len(crossings) == 0:
                continue

            first_cross = crossings[0]
            if first_cross == 0:
                t_prev, phi_prev = 0.0, phi_0[i]
                t_curr, phi_curr = t_samples[0], phi_ray[i, 0]
                hit_grad = grad_ray[i, 0]
            else:
                t_prev, t_curr = t_samples[first_cross - 1], t_samples[first_cross]
                phi_prev, phi_curr = phi_ray[i, first_cross - 1], phi_ray[i, first_cross]
                hit_grad = grad_ray[i, first_cross]

            denom = phi_curr - phi_prev
            frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
            frac = np.clip(frac, 0.0, 1.0)
            t_star = t_prev + frac * (t_curr - t_prev)

            hit_norm = np.linalg.norm(hit_grad)
            if hit_norm > 1e-12:
                n_hit = hit_grad / hit_norm
                if np.dot(n_start[i], n_hit) <= self.params.theta_opp_threshold:
                    d_gap[i] = d_near[i] + t_star

        return d_gap[0] if single_point else d_gap

    def evaluate_h_prox(self, points: np.ndarray) -> np.ndarray:
        """Compute local proximity-adapted element size h_prox(x)."""
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        d_gap = self.evaluate_gap_distance(pts)
        h_prox = self.params.beta * d_gap
        h_clipped = np.clip(h_prox, self.params.h_min, self.params.h_max)

        return h_clipped[0] if single_point else h_clipped


class AdaptiveSizeFunction:
    """Unified adaptive sizing field h(x) combining curvature, proximity, and grading."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
        curvature_method: CurvatureMethod = CurvatureMethod.HYBRID,
        feature_graph: Optional[Any] = None,
    ) -> None:
        self.params = params or SizingParameters()
        self.dim = dim
        self.sdf_evaluator = sdf_evaluator
        self.curvature_engine = CurvatureRefinement(
            sdf_evaluator=sdf_evaluator,
            params=self.params,
            dim=dim,
            surface_mesh=surface_mesh,
            method=curvature_method,
            feature_graph=feature_graph,
        )
        self.proximity_engine = ProximityRefinement(
            sdf_evaluator=sdf_evaluator, params=self.params, dim=dim
        )

    def evaluate(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        h_curv = self.curvature_engine.evaluate_h_curv(pts)
        h_prox = self.proximity_engine.evaluate_h_prox(pts)
        h_clamped = np.clip(np.minimum(h_curv, h_prox), self.params.h_min, self.params.h_max)

        return h_clamped[0] if single_point else h_clamped

    def evaluate_with_breakdown(self, points: np.ndarray) -> Dict[str, np.ndarray]:
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            pts = pts.reshape(1, -1)

        kappa = self.curvature_engine.evaluate_curvature(pts)
        h_curv = self.curvature_engine.evaluate_h_curv(pts)
        d_gap = self.proximity_engine.evaluate_gap_distance(pts)
        h_prox = self.proximity_engine.evaluate_h_prox(pts)
        h = np.clip(np.minimum(h_curv, h_prox), self.params.h_min, self.params.h_max)

        return {
            "h": h,
            "h_curv": h_curv,
            "h_prox": h_prox,
            "kappa": kappa,
            "d_gap": d_gap,
        }

    def apply_grading_filter(
        self, points: np.ndarray, sizes: np.ndarray, max_iterations: int = 3
    ) -> np.ndarray:
        """Enforce Lipschitz grading continuity ||grad h|| <= G across discrete points."""
        pts = np.asarray(points, dtype=np.float64)
        h = np.copy(sizes)
        n = len(h)
        if n < 2:
            return h

        tree = cKDTree(pts)
        k_neighbors = min(16, n)
        g = self.params.grading

        for _ in range(max_iterations):
            dists, idxs = tree.query(pts, k=k_neighbors)
            neighbor_h = h[idxs]
            limit_h = neighbor_h + g * dists
            min_limit = np.min(limit_h, axis=1)
            h = np.minimum(h, min_limit)

        return np.clip(h, self.params.h_min, self.params.h_max)
