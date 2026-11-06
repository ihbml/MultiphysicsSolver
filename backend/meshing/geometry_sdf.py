"""Continuous Signed Distance Field (SDF) Engine, Analytical Primitives & CAD Feature Filtering.

Milestone M1 Implementation for CFDSolver In-Memory Multiphysics Mesher Backbone:
- BaseSDF (SDFNode): Abstract base class for continuous level set geometry.
- Analytical Primitives: BoxSDF (2D/3D), SphereSDF (2D/3D), CylinderSDF (3D),
  PolygonSDF (2D), NACA4AirfoilSDF (parameterized 4-digit airfoil profiles).
- Constructive Solid Geometry (CSG): CSGUnion, CSGIntersection, CSGDifference,
  CSGComplement, and polynomial smooth blending (smooth_min, smooth_max).
- CADMeshSDF: Vectorized continuous signed distance field and outward normal gradients
  from triangulated SurfaceMesh accelerated via scipy.spatial.cKDTree (zero rtree dependency).
- CADFeatureFilter: Geometric sanitizer pruning orphan sketch nodes, wireframes,
  degenerate zero-area facets, and isolating dominant 2-manifold shells.
- CurvatureRefinement: Evaluates level set curvature kappa(x) and h_curv(x).
- ProximityRefinement: Evaluates narrow opposing wall gap thickness d_gap(x) and h_prox(x).
- AdaptiveSizeFunction: High-level sizing field h(x) with Lipschitz grading smoother.
"""

from __future__ import annotations

import ctypes
from abc import ABC, abstractmethod
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple, Union
import numpy as np
from scipy.spatial import cKDTree

from backend.geometry.models import BoundingBox, SurfaceMesh
from backend.meshing.difvm_topology import get_native_dll
from backend.meshing.models import SizingParameters


# ==============================================================================
# Polynomial Smooth Blending Operators
# ==============================================================================


def smooth_min(a: np.ndarray, b: np.ndarray, k: float = 0.0) -> np.ndarray:
    """Evaluate polynomial smooth minimum between two distance fields with blending radius k."""
    if k <= 1e-12:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def smooth_max(a: np.ndarray, b: np.ndarray, k: float = 0.0) -> np.ndarray:
    """Evaluate polynomial smooth maximum between two distance fields with blending radius k."""
    return -smooth_min(-a, -b, k)


# ==============================================================================
# Base SDF Abstract Class
# ==============================================================================


class BaseSDF(ABC):
    """Abstract base class for all continuous signed distance field evaluators."""

    @abstractmethod
    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x) for points of shape (N, D) or (D,).

        Sign convention:
        - phi(x) < 0: strictly inside the solid body / obstacle.
        - phi(x) == 0: on the physical boundary / wall.
        - phi(x) > 0: outside the solid body (in the fluid domain).
        """
        pass

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        """Evaluate outward unit normal gradient grad phi(x) / ||grad phi(x)||."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        if is_single:
            pts = pts[None, :]

        dim = pts.shape[-1]
        grad = np.zeros_like(pts)
        for d in range(dim):
            p_plus = np.copy(pts)
            p_minus = np.copy(pts)
            p_plus[:, d] += eps
            p_minus[:, d] -= eps
            grad[:, d] = (self.distance(p_plus) - self.distance(p_minus)) / (2.0 * eps)

        norms = np.linalg.norm(grad, axis=-1, keepdims=True)
        norms = np.where(norms < 1e-12, 1.0, norms)
        unit_grad = grad / norms
        return unit_grad[0] if is_single else unit_grad

    def evaluate(self, points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Simultaneously evaluate signed distance phi(x) and outward unit normal gradient."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        d = self.distance(pts)
        g = self.gradient(pts)
        if is_single:
            return float(d[0]), g
        return d, g

    def project_to_boundary(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        """Project points onto the zero level set boundary phi(x) == 0 via Newton-Raphson."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts).copy()

        for _ in range(max_iter):
            val = self.distance(p)
            if np.max(np.abs(val)) < tol:
                break
            dim = p.shape[-1]
            grad = np.zeros_like(p)
            for d in range(dim):
                p_plus = np.copy(p)
                p_minus = np.copy(p)
                p_plus[:, d] += eps
                p_minus[:, d] -= eps
                grad[:, d] = (self.distance(p_plus) - self.distance(p_minus)) / (2.0 * eps)

            gnorm_sq = np.sum(grad**2, axis=-1, keepdims=True)
            gnorm_sq = np.where(gnorm_sq < 1e-16, 1.0, gnorm_sq)
            p -= (val[:, None] / gnorm_sq) * grad

        return p[0] if is_single else p

    def project(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        """Alias for project_to_boundary."""
        return self.project_to_boundary(points, max_iter=max_iter, eps=eps, tol=tol)

    @abstractmethod
    def get_bounding_box(self) -> BoundingBox:
        """Compute spatial bounding box enclosing the geometry."""
        pass


# Backward-compatible alias
SDFNode = BaseSDF


# ==============================================================================
# Analytical Primitives
# ==============================================================================


class BoxSDF(BaseSDF):
    """Axis-Aligned Box / Cube SDF in 2D or 3D."""

    def __init__(
        self, min_pt: Union[List[float], np.ndarray], max_pt: Union[List[float], np.ndarray]
    ) -> None:
        self.min_pt = np.asarray(min_pt, dtype=np.float64)
        self.max_pt = np.asarray(max_pt, dtype=np.float64)
        if self.min_pt.shape != self.max_pt.shape:
            raise ValueError(f"Shape mismatch: min_pt {self.min_pt.shape} vs max_pt {self.max_pt.shape}")
        self.center = 0.5 * (self.min_pt + self.max_pt)
        self.half_extents = 0.5 * (self.max_pt - self.min_pt)
        if np.any(self.half_extents <= 0.0):
            raise ValueError(f"Box extents must be strictly positive: {self.half_extents}")

    def distance(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        p = np.atleast_2d(pts)

        q = np.abs(p - self.center) - self.half_extents
        outside = np.linalg.norm(np.maximum(q, 0.0), axis=-1)
        inside = np.minimum(np.max(q, axis=-1), 0.0)
        dist = outside + inside
        return np.atleast_1d(dist)

    def get_bounding_box(self) -> BoundingBox:
        dim = len(self.min_pt)
        if dim == 3:
            return BoundingBox(min_pt=self.min_pt, max_pt=self.max_pt)
        min_3d = np.array([self.min_pt[0], self.min_pt[1], 0.0])
        max_3d = np.array([self.max_pt[0], self.max_pt[1], 0.0])
        return BoundingBox(min_pt=min_3d, max_pt=max_3d)


class SphereSDF(BaseSDF):
    """Sphere (3D) or Circle (2D) SDF."""

    def __init__(self, center: Union[List[float], np.ndarray], radius: float) -> None:
        self.center = np.asarray(center, dtype=np.float64)
        self.radius = float(radius)
        if self.radius <= 0.0:
            raise ValueError(f"Radius must be strictly positive: {self.radius}")

    def distance(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        p = np.atleast_2d(pts)
        dist = np.linalg.norm(p - self.center, axis=-1) - self.radius
        return np.atleast_1d(dist)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts)
        diff = p - self.center
        norms = np.linalg.norm(diff, axis=-1, keepdims=True)
        norms = np.where(norms < 1e-14, 1.0, norms)
        grad = diff / norms
        return grad[0] if is_single else grad

    def get_bounding_box(self) -> BoundingBox:
        dim = len(self.center)
        r_vec = np.full(dim, self.radius)
        lo = self.center - r_vec
        hi = self.center + r_vec
        if dim == 3:
            return BoundingBox(min_pt=lo, max_pt=hi)
        return BoundingBox(min_pt=np.append(lo, 0.0), max_pt=np.append(hi, 0.0))


class CylinderSDF(BaseSDF):
    """Finite Capped or Infinite Cylinder SDF in 3D."""

    def __init__(
        self,
        center: Union[List[float], np.ndarray] = (0.0, 0.0, 0.0),
        radius: float = 1.0,
        height: Optional[float] = None,
        axis: Union[List[float], np.ndarray] = (0.0, 0.0, 1.0),
    ) -> None:
        self.center = np.asarray(center, dtype=np.float64)
        self.radius = float(radius)
        self.height = float(height) if height is not None else None
        axis_arr = np.asarray(axis, dtype=np.float64)
        norm_axis = np.linalg.norm(axis_arr)
        if norm_axis < 1e-12:
            self.axis = np.array([0.0, 0.0, 1.0])
        else:
            self.axis = axis_arr / norm_axis

    def distance(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        p = np.atleast_2d(pts)

        p_rel = p - self.center
        h_proj = np.dot(p_rel, self.axis)
        r_vec = p_rel - h_proj[:, None] * self.axis
        r_dist = np.linalg.norm(r_vec, axis=-1)

        dr = r_dist - self.radius
        if self.height is None:
            dist = dr
        else:
            da = np.abs(h_proj) - 0.5 * self.height
            d_out = np.linalg.norm(np.column_stack([np.maximum(dr, 0.0), np.maximum(da, 0.0)]), axis=-1)
            d_in = np.minimum(np.maximum(dr, da), 0.0)
            dist = d_out + d_in

        return np.atleast_1d(dist)

    def get_bounding_box(self) -> BoundingBox:
        h = self.height if self.height is not None else 100.0
        ext = np.full(3, self.radius + 0.5 * h)
        return BoundingBox(min_pt=self.center - ext, max_pt=self.center + ext)


class PolygonSDF(BaseSDF):
    """Arbitrary 2D Simple Polygon SDF with exact Euclidean distance and ray-cast parity."""

    def __init__(self, vertices: Union[List[Tuple[float, float]], np.ndarray]) -> None:
        self.vertices = np.asarray(vertices, dtype=np.float64)
        if self.vertices.ndim != 2 or self.vertices.shape[1] != 2 or len(self.vertices) < 3:
            raise ValueError(f"Polygon requires at least 3 vertices of shape (N, 2), got {self.vertices.shape}")

    def distance(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        p = np.atleast_2d(pts)
        M = len(p)
        N = len(self.vertices)

        min_dist_sq = np.full(M, np.inf)
        inside = np.zeros(M, dtype=bool)

        px = p[:, 0]
        py = p[:, 1]

        for i in range(N):
            a = self.vertices[i]
            b = self.vertices[(i + 1) % N]
            e = b - a
            w = p - a
            e_dot_e = np.dot(e, e)
            if e_dot_e < 1e-30:
                d_sq = np.sum(w**2, axis=-1)
            else:
                t = np.clip(np.sum(w * e, axis=-1) / e_dot_e, 0.0, 1.0)
                proj = a + t[:, None] * e
                d_sq = np.sum((p - proj) ** 2, axis=-1)
            min_dist_sq = np.minimum(min_dist_sq, d_sq)

            # Ray-casting point-in-polygon test
            ay, by = a[1], b[1]
            ax, bx = a[0], b[0]
            cond_y = ((ay > py) != (by > py))
            dy = by - ay
            if abs(dy) > 1e-30:
                x_intersect = ax + (bx - ax) * (py - ay) / dy
                cond_x = (px < x_intersect)
                inside ^= (cond_y & cond_x)

        dist = np.sqrt(min_dist_sq)
        signed_dist = np.where(inside, -dist, dist)
        return np.atleast_1d(signed_dist)

    def get_bounding_box(self) -> BoundingBox:
        lo = np.min(self.vertices, axis=0)
        hi = np.max(self.vertices, axis=0)
        return BoundingBox(min_pt=np.append(lo, 0.0), max_pt=np.append(hi, 0.0))


class NACA4AirfoilSDF(PolygonSDF):
    """NACA 4-Digit Airfoil Profile SDF."""

    def __init__(
        self,
        code: str = "2412",
        chord: float = 1.0,
        origin: Tuple[float, float] = (0.0, 0.0),
        angle_deg: float = 0.0,
        num_points: int = 140,
        closed_te: bool = True,
    ) -> None:
        poly = self.generate_profile(code, chord, origin, angle_deg, num_points, closed_te)
        super().__init__(poly)
        self.code = code
        self.chord = chord
        self.origin = origin
        self.angle_deg = angle_deg

    @staticmethod
    def generate_profile(
        code: str,
        chord: float,
        origin: Tuple[float, float],
        angle_deg: float,
        num_points: int,
        closed_te: bool,
    ) -> np.ndarray:
        m = int(code[0]) / 100.0
        p = int(code[1]) / 10.0
        t = int(code[2:4]) / 100.0

        beta = np.linspace(0.0, np.pi, num_points)
        xc = 0.5 * (1.0 - np.cos(beta))
        a4 = 0.1036 if closed_te else 0.1015
        yt = 5.0 * t * (0.2969 * np.sqrt(xc) - 0.1260 * xc - 0.3516 * (xc**2) + 0.2843 * (xc**3) - a4 * (xc**4))

        yc = np.zeros_like(xc)
        dyc = np.zeros_like(xc)
        if p > 1e-9 and m > 1e-9:
            idx_fwd = xc < p
            idx_aft = ~idx_fwd
            yc[idx_fwd] = (m / (p**2)) * (2.0 * p * xc[idx_fwd] - xc[idx_fwd] ** 2)
            dyc[idx_fwd] = (2.0 * m / (p**2)) * (p - xc[idx_fwd])
            yc[idx_aft] = (m / ((1.0 - p) ** 2)) * ((1.0 - 2.0 * p) + 2.0 * p * xc[idx_aft] - xc[idx_aft] ** 2)
            dyc[idx_aft] = (2.0 * m / ((1.0 - p) ** 2)) * (p - xc[idx_aft])

        th = np.arctan(dyc)
        xu = xc - yt * np.sin(th)
        yu = yc + yt * np.cos(th)
        xl = xc + yt * np.sin(th)
        yl = yc - yt * np.cos(th)

        pts_up = np.column_stack([xu, yu])
        pts_lo = np.column_stack([xl, yl])
        poly = np.vstack([pts_up, pts_lo[::-1]])

        rad = np.radians(angle_deg)
        ca, sa = np.cos(rad), np.sin(rad)
        R = np.array([[ca, -sa], [sa, ca]])
        poly = (poly * chord) @ R.T + np.asarray(origin)
        return poly


# ==============================================================================
# Constructive Solid Geometry (CSG)
# ==============================================================================


class CSGUnion(BaseSDF):
    """CSG Union (A or B) with optional polynomial smooth blending k."""

    def __init__(self, a: BaseSDF, b: BaseSDF, smooth_k: float = 0.0) -> None:
        self.a = a
        self.b = b
        self.k = float(smooth_k)

    def distance(self, points: np.ndarray) -> np.ndarray:
        return np.atleast_1d(smooth_min(self.a.distance(points), self.b.distance(points), self.k))

    def get_bounding_box(self) -> BoundingBox:
        bb_a = self.a.get_bounding_box()
        bb_b = self.b.get_bounding_box()
        return BoundingBox(
            min_pt=np.minimum(bb_a.min_pt, bb_b.min_pt),
            max_pt=np.maximum(bb_a.max_pt, bb_b.max_pt),
        )


class CSGIntersection(BaseSDF):
    """CSG Intersection (A and B) with optional polynomial smooth blending k."""

    def __init__(self, a: BaseSDF, b: BaseSDF, smooth_k: float = 0.0) -> None:
        self.a = a
        self.b = b
        self.k = float(smooth_k)

    def distance(self, points: np.ndarray) -> np.ndarray:
        return np.atleast_1d(smooth_max(self.a.distance(points), self.b.distance(points), self.k))

    def get_bounding_box(self) -> BoundingBox:
        bb_a = self.a.get_bounding_box()
        bb_b = self.b.get_bounding_box()
        return BoundingBox(
            min_pt=np.maximum(bb_a.min_pt, bb_b.min_pt),
            max_pt=np.minimum(bb_a.max_pt, bb_b.max_pt),
        )


class CSGDifference(BaseSDF):
    """CSG Difference (A minus B) with optional polynomial smooth blending k."""

    def __init__(self, a: BaseSDF, b: BaseSDF, smooth_k: float = 0.0) -> None:
        self.a = a
        self.b = b
        self.k = float(smooth_k)

    def distance(self, points: np.ndarray) -> np.ndarray:
        return np.atleast_1d(smooth_max(self.a.distance(points), -self.b.distance(points), self.k))

    def get_bounding_box(self) -> BoundingBox:
        return self.a.get_bounding_box()


class CSGComplement(BaseSDF):
    """Invert domain: interior becomes exterior, exterior becomes interior."""

    def __init__(self, child: BaseSDF) -> None:
        self.child = child

    def distance(self, points: np.ndarray) -> np.ndarray:
        return np.atleast_1d(-self.child.distance(points))

    def get_bounding_box(self) -> BoundingBox:
        return self.child.get_bounding_box()


class SDFEngine(BaseSDF):
    """Unified Facade for continuous SDF evaluation and JSON/dict geometry tree building.

    Implements the PROJECT.md interface contract:
        geometry_sdf.SDFEngine.evaluate(points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]
    """

    _default_root: Optional[BaseSDF] = None

    def __init__(self, root: Optional[Union[BaseSDF, Dict[str, Any]]] = None) -> None:
        if root is None:
            self.root: Optional[BaseSDF] = None
        elif isinstance(root, dict):
            self.root = SDFEngine.from_dict(root)
        elif isinstance(root, BaseSDF):
            self.root = root
        else:
            raise TypeError(f"Unsupported root geometry type: {type(root)}")

    @classmethod
    def set_default_geometry(cls, geom: Union[BaseSDF, Dict[str, Any]]) -> None:
        """Register a default geometry for class-level evaluation."""
        if isinstance(geom, dict):
            cls._default_root = cls.from_dict(geom)
        else:
            cls._default_root = geom

    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x)."""
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.distance(points)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        """Evaluate outward unit normal gradient."""
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.gradient(points, eps=eps)

    def evaluate(
        self,
        points_or_geom: Optional[Union[np.ndarray, BaseSDF, Dict[str, Any]]] = None,
        points: Optional[np.ndarray] = None,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluate continuous signed distance and unit normal gradient: (phi, grad_phi).

        Supports:
        - Instance call: engine.evaluate(points) or engine.evaluate(geom, points)
        - Class call with geom: SDFEngine.evaluate(geom, points)
        - Class call with default geom: SDFEngine.evaluate(points)
        """
        if isinstance(self, SDFEngine):
            if points is not None and points_or_geom is not None:
                geom = points_or_geom
                if isinstance(geom, dict):
                    geom = SDFEngine.from_dict(geom)
                return geom.evaluate(points)
            pts = points_or_geom if points is None else points
            root = self.root or self._default_root
            if root is None:
                raise RuntimeError("SDFEngine has no root geometry assigned to evaluate.")
            return root.evaluate(np.asarray(pts))

        if isinstance(self, BaseSDF):
            pts = points_or_geom if points is None else points
            return self.evaluate(np.asarray(pts))
        if isinstance(self, dict):
            geom = SDFEngine.from_dict(self)
            pts = points_or_geom if points is None else points
            return geom.evaluate(np.asarray(pts))

        pts = self
        root = SDFEngine._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned to evaluate.")
        return root.evaluate(np.asarray(pts))

    def get_bounding_box(self) -> BoundingBox:
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.get_bounding_box()

    @staticmethod
    def from_dict(spec: Dict[str, Any]) -> BaseSDF:
        """Construct an SDF tree from a declarative dictionary specification."""
        kind = spec.get("kind", "").lower()
        if kind in ("box", "cube", "rect"):
            return BoxSDF(min_pt=spec["min"], max_pt=spec["max"])
        elif kind in ("sphere", "circle", "disc"):
            return SphereSDF(center=spec["center"], radius=spec["radius"])
        elif kind in ("cylinder",):
            return CylinderSDF(
                center=spec.get("center", (0.0, 0.0, 0.0)),
                radius=spec.get("radius", 1.0),
                height=spec.get("height", None),
                axis=spec.get("axis", (0.0, 0.0, 1.0)),
            )
        elif kind in ("polygon",):
            return PolygonSDF(vertices=spec["points"])
        elif kind in ("airfoil", "naca"):
            return NACA4AirfoilSDF(
                code=spec.get("naca", "2412"),
                chord=spec.get("chord", 1.0),
                origin=spec.get("origin", (0.0, 0.0)),
                angle_deg=spec.get("angle", 0.0),
                num_points=spec.get("samples", 140),
                closed_te=spec.get("closed_te", True),
            )
        elif kind in ("union",):
            a = SDFEngine.from_dict(spec["a"])
            b = SDFEngine.from_dict(spec["b"])
            return CSGUnion(a, b, smooth_k=spec.get("smooth", 0.0))
        elif kind in ("intersection",):
            a = SDFEngine.from_dict(spec["a"])
            b = SDFEngine.from_dict(spec["b"])
            return CSGIntersection(a, b, smooth_k=spec.get("smooth", 0.0))
        elif kind in ("difference",):
            a = SDFEngine.from_dict(spec["a"])
            b = SDFEngine.from_dict(spec["b"])
            return CSGDifference(a, b, smooth_k=spec.get("smooth", 0.0))
        elif kind in ("complement", "invert"):
            child = SDFEngine.from_dict(spec["child"])
            return CSGComplement(child)
        else:
            raise ValueError(f"Unknown SDF geometry kind: '{kind}'")


# ==============================================================================
# WP2: Exact Point-to-Triangle Projection Oracle (Eberly / Ericson)
# ==============================================================================


def _point_to_triangle_distance_exact(
    p: np.ndarray,
    a: np.ndarray,
    b: np.ndarray,
    c: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Exact point-to-triangle projection using Voronoi feature regions.

    Args:
        p: (3,) float64 query point.
        a: (3,) float64 first vertex.
        b: (3,) float64 second vertex.
        c: (3,) float64 third vertex.

    Returns:
        Tuple containing:
        - closest_point: (3,) float64 point on triangle surface.
        - barycentric_coords: (3,) float64 coordinates (u, v, w) summing to 1.0.
        - distance: Euclidean distance between p and closest_point.
    """
    ab = b - a
    ac = c - a
    ap = p - a

    d1 = float(np.dot(ab, ap))
    d2 = float(np.dot(ac, ap))
    if d1 <= 0.0 and d2 <= 0.0:
        bary = np.array([1.0, 0.0, 0.0], dtype=np.float64)
        return a.copy(), bary, float(np.linalg.norm(p - a))

    bp = p - b
    d3 = float(np.dot(ab, bp))
    d4 = float(np.dot(ac, bp))
    if d3 >= 0.0 and d4 <= d3:
        bary = np.array([0.0, 1.0, 0.0], dtype=np.float64)
        return b.copy(), bary, float(np.linalg.norm(p - b))

    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        denom = d1 - d3
        v = d1 / denom if abs(denom) > 1e-30 else 0.0
        q = a + v * ab
        bary = np.array([1.0 - v, v, 0.0], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    cp = p - c
    d5 = float(np.dot(ab, cp))
    d6 = float(np.dot(ac, cp))
    if d6 >= 0.0 and d5 <= d6:
        bary = np.array([0.0, 0.0, 1.0], dtype=np.float64)
        return c.copy(), bary, float(np.linalg.norm(p - c))

    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        denom = d2 - d6
        w = d2 / denom if abs(denom) > 1e-30 else 0.0
        q = a + w * ac
        bary = np.array([1.0 - w, 0.0, w], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        denom = (d4 - d3) + (d5 - d6)
        w = (d4 - d3) / denom if abs(denom) > 1e-30 else 0.0
        q = b + w * (c - b)
        bary = np.array([0.0, 1.0 - w, w], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    denom_sum = va + vb + vc
    inv_denom = 1.0 / denom_sum if abs(denom_sum) > 1e-30 else 0.0
    v = vb * inv_denom
    w = vc * inv_denom
    u = 1.0 - v - w
    q = a + v * ab + w * ac
    bary = np.array([u, v, w], dtype=np.float64)
    return q, bary, float(np.linalg.norm(p - q))


# ==============================================================================
# WP2: Angle-Weighted Pseudonormals (Bærentzen & Aanæs 2005)
# ==============================================================================


def compute_angle_weighted_pseudonormals(
    vertices: np.ndarray, faces: np.ndarray
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Computes face normals, angle-weighted vertex pseudonormals, edge pseudonormals,
    and face-to-edge mapping following Bærentzen & Aanæs (2005).

    Args:
        vertices: (V, 3) float64 coordinates.
        faces: (F, 3) int32 face node indices.

    Returns:
        Tuple containing:
        - face_normals: (F, 3) float64 outward unit normals of faces.
        - vertex_pseudonormals: (V, 3) float64 angle-weighted outward unit normals of vertices.
        - edge_pseudonormals: (E, 3) float64 outward unit normals of undirected edges.
        - face_edge_map: (F, 3) int32 mapping (face_id, local_edge_idx) to edge_id in [0, E).
          local_edge 0 connects (v0, v1), 1 connects (v1, v2), 2 connects (v2, v0).
    """
    verts = np.asarray(vertices, dtype=np.float64)
    f_arr = np.asarray(faces, dtype=np.int32)
    n_faces = len(f_arr)
    n_verts = len(verts)

    # 1. Face unit normals
    v0 = verts[f_arr[:, 0]]
    v1 = verts[f_arr[:, 1]]
    v2 = verts[f_arr[:, 2]]

    e01 = v1 - v0
    e02 = v2 - v0
    cross_f = np.cross(e01, e02)
    lengths_f = np.linalg.norm(cross_f, axis=1, keepdims=True)
    lengths_f_safe = np.where(lengths_f < 1e-20, 1.0, lengths_f)
    face_normals = cross_f / lengths_f_safe
    zero_mask = (lengths_f.squeeze(-1) < 1e-20)
    if np.any(zero_mask):
        face_normals[zero_mask] = np.array([0.0, 0.0, 1.0])

    # 2. Angle-weighted vertex pseudonormals
    vertex_pseudonormals = np.zeros((n_verts, 3), dtype=np.float64)

    # Corner 0: at v0, edges (v1 - v0) and (v2 - v0)
    d01 = np.linalg.norm(e01, axis=1)
    d02 = np.linalg.norm(e02, axis=1)
    d01_safe = np.where(d01 < 1e-20, 1.0, d01)
    d02_safe = np.where(d02 < 1e-20, 1.0, d02)
    cos0 = np.sum(e01 * e02, axis=1) / (d01_safe * d02_safe)
    alpha0 = np.arccos(np.clip(cos0, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 0], alpha0[:, None] * face_normals)

    # Corner 1: at v1, edges (v2 - v1) and (v0 - v1)
    e12 = v2 - v1
    e10 = -e01
    d12 = np.linalg.norm(e12, axis=1)
    d12_safe = np.where(d12 < 1e-20, 1.0, d12)
    cos1 = np.sum(e12 * e10, axis=1) / (d12_safe * d01_safe)
    alpha1 = np.arccos(np.clip(cos1, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 1], alpha1[:, None] * face_normals)

    # Corner 2: at v2, edges (v0 - v2) and (v1 - v2)
    e20 = -e02
    e21 = -e12
    cos2 = np.sum(e20 * e21, axis=1) / (d02_safe * d12_safe)
    alpha2 = np.arccos(np.clip(cos2, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 2], alpha2[:, None] * face_normals)

    v_norms = np.linalg.norm(vertex_pseudonormals, axis=1, keepdims=True)
    v_norms_safe = np.where(v_norms < 1e-6, 1.0, v_norms)
    vertex_pseudonormals = vertex_pseudonormals / v_norms_safe
    v_zero = (v_norms.squeeze(-1) < 1e-6)
    if np.any(v_zero):
        vertex_pseudonormals[v_zero] = np.array([0.0, 0.0, 1.0])

    # 3. Edge pseudonormals and face-to-edge mapping
    edge_map: Dict[Tuple[int, int], int] = {}
    face_edge_map = np.zeros((n_faces, 3), dtype=np.int32)
    edge_normal_list: List[np.ndarray] = []

    for f_idx in range(n_faces):
        nodes = f_arr[f_idx]
        e_pairs = [
            (min(nodes[0], nodes[1]), max(nodes[0], nodes[1])),
            (min(nodes[1], nodes[2]), max(nodes[1], nodes[2])),
            (min(nodes[2], nodes[0]), max(nodes[2], nodes[0])),
        ]
        fn = face_normals[f_idx]
        for loc_idx, pair in enumerate(e_pairs):
            if pair not in edge_map:
                edge_id = len(edge_normal_list)
                edge_map[pair] = edge_id
                edge_normal_list.append(fn.copy())
            else:
                edge_id = edge_map[pair]
                edge_normal_list[edge_id] += fn
            face_edge_map[f_idx, loc_idx] = edge_id

    edge_pseudonormals = np.array(edge_normal_list, dtype=np.float64)
    e_norms = np.linalg.norm(edge_pseudonormals, axis=1, keepdims=True)
    e_norms_safe = np.where(e_norms < 1e-6, 1.0, e_norms)
    edge_pseudonormals = edge_pseudonormals / e_norms_safe
    e_zero = (e_norms.squeeze(-1) < 1e-6)
    if np.any(e_zero):
        edge_pseudonormals[e_zero] = np.array([0.0, 0.0, 1.0])

    return face_normals, vertex_pseudonormals, edge_pseudonormals, face_edge_map


# ==============================================================================
# WP2: Triangle AABB Tree & Coherence Caching
# ==============================================================================


class _AABBNode:
    __slots__ = ("bmin", "bmax", "left", "right", "tri_indices")

    def __init__(
        self,
        bmin: np.ndarray,
        bmax: np.ndarray,
        left: Optional[_AABBNode] = None,
        right: Optional[_AABBNode] = None,
        tri_indices: Optional[np.ndarray] = None,
    ) -> None:
        self.bmin = bmin
        self.bmax = bmax
        self.left = left
        self.right = right
        self.tri_indices = tri_indices


class TriangleAABBTree:
    """Hierarchical Axis-Aligned Bounding Box (AABB) Tree over mesh triangles
    with branch-and-bound pruning and prev_facet spatial coherence caching.
    """

    def __init__(
        self,
        vertices: np.ndarray,
        faces: np.ndarray,
        leaf_size: int = 8,
    ) -> None:
        self.vertices = np.asarray(vertices, dtype=np.float64)
        self.faces = np.asarray(faces, dtype=np.int32)
        if len(self.faces) == 0:
            raise ValueError("Cannot construct TriangleAABBTree from empty faces.")

        self.tri_v0 = self.vertices[self.faces[:, 0]]
        self.tri_v1 = self.vertices[self.faces[:, 1]]
        self.tri_v2 = self.vertices[self.faces[:, 2]]

        self.tri_bmin = np.minimum(self.tri_v0, np.minimum(self.tri_v1, self.tri_v2))
        self.tri_bmax = np.maximum(self.tri_v0, np.maximum(self.tri_v1, self.tri_v2))
        self.tri_centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0

        all_indices = np.arange(len(self.faces), dtype=np.int32)
        self.root = self._build(all_indices, leaf_size)

    def _build(self, indices: np.ndarray, leaf_size: int) -> _AABBNode:
        bmin = np.min(self.tri_bmin[indices], axis=0)
        bmax = np.max(self.tri_bmax[indices], axis=0)

        if len(indices) <= leaf_size:
            return _AABBNode(bmin, bmax, tri_indices=indices)

        c_bmin = np.min(self.tri_centroids[indices], axis=0)
        c_bmax = np.max(self.tri_centroids[indices], axis=0)
        extent = c_bmax - c_bmin
        axis = int(np.argmax(extent))

        if extent[axis] <= 1e-15:
            return _AABBNode(bmin, bmax, tri_indices=indices)

        sorted_order = np.argsort(self.tri_centroids[indices, axis])
        sorted_indices = indices[sorted_order]
        mid = len(indices) // 2

        left_node = self._build(sorted_indices[:mid], leaf_size)
        right_node = self._build(sorted_indices[mid:], leaf_size)
        return _AABBNode(bmin, bmax, left=left_node, right=right_node)

    def _point_to_tri(self, p: np.ndarray, tid: int) -> Tuple[np.ndarray, np.ndarray, float]:
        va = self.tri_v0[tid]
        vb = self.tri_v1[tid]
        vc = self.tri_v2[tid]
        return _point_to_triangle_distance_exact(p, va, vb, vc)

    def closest_point(
        self, p: np.ndarray, prev_facet: Optional[int] = None
    ) -> Tuple[np.ndarray, int, np.ndarray, float]:
        """Find closest point on triangulated mesh using branch-and-bound traversal.

        Args:
            p: 3D query coordinates, shape (3,).
            prev_facet: Optional previous closest facet ID as spatial coherence hint.

        Returns:
            Tuple of:
            - closest_point: (3,) float64 coordinates on triangle.
            - facet_id: int index of closest triangle in self.faces.
            - barycentric_coords: (3,) float64 barycentric coordinates.
            - distance: Euclidean distance from p to closest_point.
        """
        pt = np.asarray(p, dtype=np.float64)

        best_dist = float("inf")
        best_dist_sq = float("inf")
        best_q: Optional[np.ndarray] = None
        best_tid: int = -1
        best_bary: Optional[np.ndarray] = None

        # 1. Evaluate spatial coherence hint (prev_facet) first
        if prev_facet is not None and 0 <= prev_facet < len(self.faces):
            q, bary, d = self._point_to_tri(pt, prev_facet)
            best_dist = d
            best_dist_sq = d * d
            best_q = q
            best_tid = prev_facet
            best_bary = bary

        # 2. Branch-and-bound traversal
        stack = [self.root]
        px, py, pz = pt[0], pt[1], pt[2]

        while stack:
            node = stack.pop()

            bmin = node.bmin
            bmax = node.bmax
            dx = max(0.0, bmin[0] - px, px - bmax[0])
            dy = max(0.0, bmin[1] - py, py - bmax[1])
            dz = max(0.0, bmin[2] - pz, pz - bmax[2])
            box_dist_sq = dx * dx + dy * dy + dz * dz

            if box_dist_sq >= best_dist_sq:
                continue

            if node.tri_indices is not None:
                for tid in node.tri_indices:
                    q, bary, d = self._point_to_tri(pt, int(tid))
                    d_sq = d * d
                    if d_sq < best_dist_sq:
                        best_dist = d
                        best_dist_sq = d_sq
                        best_q = q
                        best_tid = int(tid)
                        best_bary = bary
            else:
                left = node.left
                right = node.right

                dx_l = max(0.0, left.bmin[0] - px, px - left.bmax[0])
                dy_l = max(0.0, left.bmin[1] - py, py - left.bmax[1])
                dz_l = max(0.0, left.bmin[2] - pz, pz - left.bmax[2])
                dl_sq = dx_l * dx_l + dy_l * dy_l + dz_l * dz_l

                dx_r = max(0.0, right.bmin[0] - px, px - right.bmax[0])
                dy_r = max(0.0, right.bmin[1] - py, py - right.bmax[1])
                dz_r = max(0.0, right.bmin[2] - pz, pz - right.bmax[2])
                dr_sq = dx_r * dx_r + dy_r * dy_r + dz_r * dz_r

                if dl_sq < dr_sq:
                    if dr_sq < best_dist_sq:
                        stack.append(right)
                    if dl_sq < best_dist_sq:
                        stack.append(left)
                else:
                    if dl_sq < best_dist_sq:
                        stack.append(left)
                    if dr_sq < best_dist_sq:
                        stack.append(right)

        return best_q, best_tid, best_bary, best_dist

    def query(
        self, points: np.ndarray, prev_facet: Optional[int] = None
    ) -> Union[
        Tuple[float, np.ndarray, int, np.ndarray],
        Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray],
    ]:
        """Query closest point(s) on the mesh.

        Args:
            points: Query point coordinates, shape (3,) or (N, 3).
            prev_facet: Optional initial facet hint.

        Returns:
            If 1D: (distance, closest_point, facet_id, barycentric_coords).
            If 2D: (distances, closest_points, facet_ids, barycentric_coords).
        """
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            q, tid, bary, dist = self.closest_point(pts, prev_facet=prev_facet)
            return dist, q, tid, bary

        n_pts = len(pts)
        dists = np.empty(n_pts, dtype=np.float64)
        closest_pts = np.empty((n_pts, 3), dtype=np.float64)
        facet_ids = np.empty(n_pts, dtype=np.int32)
        bary_coords = np.empty((n_pts, 3), dtype=np.float64)

        hint = prev_facet if (prev_facet is not None and 0 <= prev_facet < len(self.faces)) else -1
        for i in range(n_pts):
            q, tid, bary, d = self.closest_point(pts[i], prev_facet=hint)
            dists[i] = d
            closest_pts[i] = q
            facet_ids[i] = tid
            bary_coords[i] = bary
            hint = tid

        return dists, closest_pts, facet_ids, bary_coords


# ==============================================================================
# WP2: Generalized Winding Numbers (Oosterom & Strackee 1983 / Jacobson et al. 2013)
# ==============================================================================


def compute_mesh_winding_number(
    points: np.ndarray,
    tri_v0: np.ndarray,
    tri_v1: np.ndarray,
    tri_v2: np.ndarray,
    chunk_size: int = 1024,
) -> np.ndarray:
    """Computes Generalized Winding Numbers W(x) = (1 / 4pi) sum Omega_t(x)
    following Oosterom & Strackee (1983) and Jacobson et al. (2013).

    Args:
        points: (N, 3) or (3,) query point coordinates.
        tri_v0: (T, 3) first vertex of all triangles.
        tri_v1: (T, 3) second vertex of all triangles.
        tri_v2: (T, 3) third vertex of all triangles.
        chunk_size: Block size of triangles to evaluate per chunk.

    Returns:
        Winding numbers of shape (N,) or scalar float if 1D input.
    """
    pts = np.asarray(points, dtype=np.float64)
    is_single = (pts.ndim == 1)
    p = np.atleast_2d(pts)
    n_pts = len(p)
    n_tri = len(tri_v0)

    total_solid_angles = np.zeros(n_pts, dtype=np.float64)

    for start_t in range(0, n_tri, chunk_size):
        end_t = min(start_t + chunk_size, n_tri)
        a = tri_v0[start_t:end_t]
        b = tri_v1[start_t:end_t]
        c = tri_v2[start_t:end_t]

        ra = a[None, :, :] - p[:, None, :]
        rb = b[None, :, :] - p[:, None, :]
        rc = c[None, :, :] - p[:, None, :]

        da = np.linalg.norm(ra, axis=-1)
        db = np.linalg.norm(rb, axis=-1)
        dc = np.linalg.norm(rc, axis=-1)

        rb_cross_rc = np.cross(rb, rc)
        num = np.sum(ra * rb_cross_rc, axis=-1)

        ra_dot_rb = np.sum(ra * rb, axis=-1)
        ra_dot_rc = np.sum(ra * rc, axis=-1)
        rb_dot_rc = np.sum(rb * rc, axis=-1)

        denom = (
            da * db * dc
            + ra_dot_rb * dc
            + ra_dot_rc * db
            + rb_dot_rc * da
        )

        omega = 2.0 * np.arctan2(num, denom)
        total_solid_angles += np.sum(omega, axis=1)

    wn = total_solid_angles / (4.0 * np.pi)
    return wn[0] if is_single else wn


# ==============================================================================
# WP2: Differentiable VoroMesh Bisector Loss (Maruani et al. 2023)
# ==============================================================================


def voromesh_bisector_loss(
    points: np.ndarray,
    seeds: np.ndarray,
    k: int = 16,
    seed_signs: Optional[np.ndarray] = None,
    use_native: bool = True,
) -> Tuple[float, np.ndarray, np.ndarray]:
    """Differentiable VoroMesh bisector loss following Maruani et al. (2023).

    Args:
        points: (N, 3) surface query points.
        seeds: (M, 3) Voronoi generator seeds.
        k: Number of nearest neighbor seeds to consider for bisector planes (default: 16).
        seed_signs: Optional (M,) array of seed sign indicators (+1/-1). If provided,
            only neighbor pairs with opposing signs are evaluated, with fallback.
        use_native: Whether to use compiled C++ acceleration if available.

    Returns:
        Tuple containing:
        - loss: Scalar mean squared distance to closest Voronoi bisector plane.
        - distances: (N,) float64 Euclidean distance to closest bisector plane.
        - gradients: (N, 3) float64 outward unit normal gradients ||grad_x d|| == 1.0.
    """
    pts = np.asarray(points, dtype=np.float64)
    s_arr = np.asarray(seeds, dtype=np.float64)
    n_pts = len(pts)
    n_seeds = len(s_arr)

    if n_seeds < 2:
        raise ValueError("At least 2 seeds required for Voronoi bisector loss.")

    k_eff = min(k, n_seeds - 1)

    # 1. Native C++ acceleration attempt
    if use_native:
        dll = _get_sdf_dll()
        if dll is not None and hasattr(dll, "cfd_sdf_voromesh_loss"):
            try:
                c_points = pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                c_seeds = s_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                out_loss = ctypes.c_double(0.0)
                out_dists = np.empty(n_pts, dtype=np.float64)
                out_grads = np.empty((n_pts, 3), dtype=np.float64)
                c_signs = (
                    np.ascontiguousarray(seed_signs, dtype=np.float64).ctypes.data_as(
                        ctypes.POINTER(ctypes.c_double)
                    )
                    if seed_signs is not None
                    else None
                )

                res = dll.cfd_sdf_voromesh_loss(
                    c_points,
                    ctypes.c_int64(n_pts),
                    c_seeds,
                    ctypes.c_int64(n_seeds),
                    ctypes.c_int32(k_eff),
                    ctypes.byref(out_loss),
                    out_dists.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    out_grads.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    c_signs,
                )
                if res == 0:
                    return float(out_loss.value), out_dists, out_grads
            except Exception:
                pass

    # 2. Pure Python fallback
    seed_tree = cKDTree(s_arr)
    _, nearest_seed_idx = seed_tree.query(pts, k=1)
    if nearest_seed_idx.ndim == 0:
        nearest_seed_idx = np.array([nearest_seed_idx])

    _, seed_neighbors = seed_tree.query(s_arr, k=k_eff + 1)
    nbr_indices = seed_neighbors[:, 1:]

    qi = s_arr[nearest_seed_idx]
    pt_nbr_indices = nbr_indices[nearest_seed_idx]
    qj = s_arr[pt_nbr_indices]

    qi_exp = qi[:, None, :]
    midpoints = 0.5 * (qi_exp + qj)
    delta = qj - qi_exp
    delta_len = np.linalg.norm(delta, axis=-1, keepdims=True)
    delta_len_safe = np.where(delta_len < 1e-15, 1.0, delta_len)
    normals = delta / delta_len_safe

    pts_exp = pts[:, None, :]
    h = np.sum((pts_exp - midpoints) * normals, axis=-1)
    d = np.abs(h)

    if seed_signs is not None:
        s_signs = np.asarray(seed_signs, dtype=np.float64)
        qi_signs = s_signs[nearest_seed_idx, None]
        qj_signs = s_signs[pt_nbr_indices]
        valid_pairs = (qi_signs * qj_signs) < 0.0
        d_eff = np.where(valid_pairs, d, np.inf)
        has_valid = np.any(valid_pairs, axis=1)
        best_k = np.zeros(n_pts, dtype=np.int64)
        if np.any(has_valid):
            best_k[has_valid] = np.argmin(d_eff[has_valid], axis=1)
        if not np.all(has_valid):
            best_k[~has_valid] = np.argmin(d[~has_valid], axis=1)
    else:
        best_k = np.argmin(d, axis=1)

    rows = np.arange(n_pts)

    distances = d[rows, best_k]
    best_h = h[rows, best_k]
    best_normal = normals[rows, best_k]

    sign = np.where(best_h >= 0.0, 1.0, -1.0)[:, None]
    gradients = sign * best_normal

    loss = float(np.mean(distances ** 2))
    return loss, distances, gradients


# ==============================================================================
# Native C-ABI Helper
# ==============================================================================


_SDF_CABI_CONFIGURED: bool = False


def _get_sdf_dll() -> Optional[ctypes.CDLL]:
    global _SDF_CABI_CONFIGURED
    dll = get_native_dll()
    if dll is not None and hasattr(dll, "cfd_sdf_create") and not _SDF_CABI_CONFIGURED:
        dll.cfd_sdf_create.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_int64,
        ]
        dll.cfd_sdf_create.restype = ctypes.c_void_p

        dll.cfd_sdf_free.argtypes = [ctypes.c_void_p]
        dll.cfd_sdf_free.restype = None

        dll.cfd_sdf_evaluate.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_evaluate.restype = ctypes.c_int32

        dll.cfd_sdf_winding_number.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_winding_number.restype = ctypes.c_int32

        dll.cfd_sdf_voromesh_loss.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.c_int32,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_voromesh_loss.restype = ctypes.c_int32
        _SDF_CABI_CONFIGURED = True
    return dll


# ==============================================================================
# SurfaceMesh to Continuous SDF Evaluator (CADMeshSDF)
# ==============================================================================


class CADMeshSDF(BaseSDF):
    """Continuous Signed Distance Field (SDF) and outward normal gradient evaluator
    derived from a triangulated SurfaceMesh using spatial AABB tree and pseudonormals.

    Guarantees:
    - Zero sign flips along 30° and 90° sharp edges/corners (Bærentzen & Aanæs 2005).
    - Generalized Winding Numbers on dirty / non-watertight CAD meshes (Jacobson et al. 2013).
    - Dual-engine parity between Python and C++20 native kernels.
    - Outward unit normal gradient norm ||grad phi|| == 1.0 everywhere.
    """

    def __init__(
        self,
        mesh: SurfaceMesh,
        k_candidates: Optional[int] = None,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
        sign_method: Optional[str] = None,
        use_native: bool = True,
    ) -> None:
        """Initialize CADMeshSDF.

        Args:
            mesh: Triangulated SurfaceMesh.
            k_candidates: Optional number of nearest candidate triangle centroids.
                If explicitly provided without specifying sign_method, enables legacy M1 candidate search.
            inside_is_negative: If True, phi < 0 inside solid body, phi > 0 in exterior.
            auto_compact: If True, purges unreferenced/orphan vertices before building index.
            sign_method: 'pseudonormal', 'winding_number', or 'face_normal'.
            use_native: If True, uses compiled C++20 acceleration when available.
        """
        if len(mesh.faces) == 0:
            raise ValueError("Cannot initialize CADMeshSDF from empty surface mesh.")

        raw_verts = mesh.vertices.astype(np.float64)
        raw_faces = mesh.faces.astype(np.int32)

        if auto_compact:
            used_indices, inverse = np.unique(raw_faces, return_inverse=True)
            self.vertices = np.ascontiguousarray(raw_verts[used_indices], dtype=np.float64)
            self.faces = np.ascontiguousarray(inverse.reshape(raw_faces.shape), dtype=np.int32)
        else:
            self.vertices = np.ascontiguousarray(raw_verts, dtype=np.float64)
            self.faces = np.ascontiguousarray(raw_faces, dtype=np.int32)

        # Consistent outward face orientation for closed/oriented meshes
        v0_init = self.vertices[self.faces[:, 0]]
        v1_init = self.vertices[self.faces[:, 1]]
        v2_init = self.vertices[self.faces[:, 2]]
        signed_vol = float(np.sum(np.sum(v0_init * np.cross(v1_init, v2_init), axis=-1)) / 6.0)
        if signed_vol < -1e-8:
            self.faces = np.ascontiguousarray(self.faces[:, [0, 2, 1]], dtype=np.int32)

        self._legacy_k = k_candidates
        self.k_candidates = min(max(1, k_candidates if k_candidates is not None else 8), len(self.faces))
        self.inside_is_negative = inside_is_negative
        if sign_method is not None:
            self.sign_method = sign_method
        elif k_candidates is not None:
            self.sign_method = "legacy"
        else:
            self.sign_method = "pseudonormal"
        self.use_native = use_native
        self._prev_facet_hint = -1
        self._native_engine: Optional[ctypes.c_void_p] = None

        # Precompute triangle vertex coordinates
        self.tri_v0 = self.vertices[self.faces[:, 0]]
        self.tri_v1 = self.vertices[self.faces[:, 1]]
        self.tri_v2 = self.vertices[self.faces[:, 2]]

        # Precompute face unit normal vectors and centroids (backward compatibility)
        cross = np.cross(self.tri_v1 - self.tri_v0, self.tri_v2 - self.tri_v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        lengths[lengths < 1e-20] = 1.0
        self.normals = cross / lengths
        self.centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0
        self.tree = cKDTree(self.centroids)

        # Precompute Angle-Weighted Pseudonormals & Triangle AABB Tree
        (
            self.face_normals,
            self.vertex_pseudonormals,
            self.edge_pseudonormals,
            self.face_edge_map,
        ) = compute_angle_weighted_pseudonormals(self.vertices, self.faces)
        self.aabb_tree = TriangleAABBTree(self.vertices, self.faces)

        # Initialize native C-ABI engine if available
        if self.use_native:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_create"):
                try:
                    c_verts = self.vertices.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    c_faces = self.faces.ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
                    self._native_engine = dll.cfd_sdf_create(
                        c_verts, ctypes.c_int64(len(self.vertices)),
                        c_faces, ctypes.c_int64(len(self.faces))
                    )
                except Exception:
                    self._native_engine = None

    def __del__(self) -> None:
        if getattr(self, "_native_engine", None):
            try:
                dll = _get_sdf_dll()
                if dll is not None and hasattr(dll, "cfd_sdf_free"):
                    dll.cfd_sdf_free(self._native_engine)
            except Exception:
                pass
            self._native_engine = None

    @classmethod
    def from_surface_mesh(
        cls,
        mesh: SurfaceMesh,
        k_candidates: Optional[int] = None,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
        sign_method: Optional[str] = None,
        use_native: bool = True,
    ) -> CADMeshSDF:
        """Factory constructor creating CADMeshSDF from SurfaceMesh."""
        return cls(
            mesh,
            k_candidates=k_candidates,
            inside_is_negative=inside_is_negative,
            auto_compact=auto_compact,
            sign_method=sign_method,
            use_native=use_native,
        )

    def evaluate(self, points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluate continuous signed distance phi(x) and outward normal gradient grad_phi(x).

        Args:
            points: Array of 3D query coordinates, shape (3,) or (N, 3), float64.

        Returns:
            Tuple of:
            - phi: Signed distance values, shape (N,), float64 (or scalar if 1D input).
            - grad_phi: Outward unit normal gradient vectors, shape (N, 3), float64.
        """
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts)
        n_pts = len(p)

        # Guard against extreme large coordinates (>= 1e30)
        p_max = np.max(np.abs(p), axis=1)
        extreme_mask = p_max >= 1e30
        if np.any(extreme_mask):
            phi_arr = np.empty(n_pts, dtype=np.float64)
            grad_arr = np.empty((n_pts, 3), dtype=np.float64)

            # Normal points
            norm_indices = np.where(~extreme_mask)[0]
            if len(norm_indices) > 0:
                p_sub = p[norm_indices]
                p_save = self._evaluate_standard(p_sub)
                phi_arr[norm_indices] = p_save[0]
                grad_arr[norm_indices] = p_save[1]

            # Extreme points: approximate analytically with safe scaled norm
            ext_indices = np.where(extreme_mask)[0]
            for idx in ext_indices:
                p_vec = p[idx]
                m_coord = float(np.max(np.abs(p_vec)))
                if m_coord < 1e-20:
                    m_coord = 1.0
                scaled_p = p_vec / m_coord
                unit_norm = float(np.linalg.norm(scaled_p))
                if unit_norm < 1e-20:
                    unit_norm = 1.0
                unit_grad = scaled_p / unit_norm
                p_norm = m_coord * unit_norm
                if not np.isfinite(p_norm):
                    p_norm = 1.7e308
                phi_arr[idx] = p_norm if self.inside_is_negative else -p_norm
                grad_arr[idx] = unit_grad if self.inside_is_negative else -unit_grad

            if is_single:
                return float(phi_arr[0]), grad_arr[0]
            return phi_arr, grad_arr

        phi, grad = self._evaluate_standard(p)
        if is_single:
            return float(phi[0]), grad[0]
        return phi, grad

    def _evaluate_standard(self, p: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        n_pts = len(p)

        # 1. Legacy M1 candidate KD-Tree heuristic for backward compatibility
        if self.sign_method == "legacy":
            return self._evaluate_legacy_kdtree(p)

        # 2. Generalized Winding Numbers
        if self.sign_method == "winding_number":
            dists, qs, tids, _ = self.aabb_tree.query(p, prev_facet=self._prev_facet_hint)
            self._prev_facet_hint = int(tids[-1]) if n_pts > 0 else -1

            wn = self.winding_number(p)
            sign = np.where(wn >= 0.5, -1.0, 1.0)
            if not self.inside_is_negative:
                sign = -sign

            phi = sign * dists
            diff = p - qs
            diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
            diff_norm_safe = np.where(diff_norm < 1e-15, 1.0, diff_norm)
            unit_diff = diff / diff_norm_safe
            fallback_normals = self.face_normals[tids] if self.inside_is_negative else -self.face_normals[tids]
            grad = np.where(diff_norm > 1e-12, sign[:, None] * unit_diff, fallback_normals)
            g_len = np.linalg.norm(grad, axis=1, keepdims=True)
            grad = grad / np.where(g_len < 1e-15, 1.0, g_len)
            return phi, grad

        # 3. Angle-Weighted Pseudonormals (Bærentzen & Aanæs 2005)
        # Try native C-ABI kernel first
        if self.use_native and self._native_engine:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_evaluate"):
                try:
                    p_contig = np.ascontiguousarray(p, dtype=np.float64)
                    c_points = p_contig.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    out_phi = np.ascontiguousarray(np.empty(n_pts, dtype=np.float64))
                    out_grad = np.ascontiguousarray(np.empty((n_pts, 3), dtype=np.float64))
                    res = dll.cfd_sdf_evaluate(
                        self._native_engine,
                        c_points,
                        ctypes.c_int64(n_pts),
                        out_phi.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                        out_grad.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    )
                    if res == 0:
                        if not self.inside_is_negative:
                            out_phi = -out_phi
                            out_grad = -out_grad
                        return out_phi, out_grad
                except Exception:
                    pass

        # Pure-Python AABB tree + Pseudonormals evaluation
        dists, qs, tids, barys = self.aabb_tree.query(p, prev_facet=self._prev_facet_hint)
        self._prev_facet_hint = int(tids[-1]) if n_pts > 0 else -1

        # Classify closest points into vertex, edge, or interior
        u = barys[:, 0]
        v = barys[:, 1]
        w = barys[:, 2]
        eps = 1e-5

        m_v0 = (u > 1.0 - 2.0 * eps)
        m_v1 = (v > 1.0 - 2.0 * eps)
        m_v2 = (w > 1.0 - 2.0 * eps)

        not_vert = ~(m_v0 | m_v1 | m_v2)
        m_e0 = not_vert & (w <= eps)
        m_e1 = not_vert & (u <= eps)
        m_e2 = not_vert & (v <= eps)
        m_face = ~(m_v0 | m_v1 | m_v2 | m_e0 | m_e1 | m_e2)

        n_pseudo = np.empty((n_pts, 3), dtype=np.float64)
        if np.any(m_v0):
            n_pseudo[m_v0] = self.vertex_pseudonormals[self.faces[tids[m_v0], 0]]
        if np.any(m_v1):
            n_pseudo[m_v1] = self.vertex_pseudonormals[self.faces[tids[m_v1], 1]]
        if np.any(m_v2):
            n_pseudo[m_v2] = self.vertex_pseudonormals[self.faces[tids[m_v2], 2]]
        if np.any(m_e0):
            n_pseudo[m_e0] = self.edge_pseudonormals[self.face_edge_map[tids[m_e0], 0]]
        if np.any(m_e1):
            n_pseudo[m_e1] = self.edge_pseudonormals[self.face_edge_map[tids[m_e1], 1]]
        if np.any(m_e2):
            n_pseudo[m_e2] = self.edge_pseudonormals[self.face_edge_map[tids[m_e2], 2]]
        if np.any(m_face):
            n_pseudo[m_face] = self.face_normals[tids[m_face]]

        diff = p - qs
        diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
        diff_norm_safe = np.where(diff_norm < 1e-15, 1.0, diff_norm)
        unit_diff = diff / diff_norm_safe

        dot_val = np.sum(diff * n_pseudo, axis=1)
        sign = np.where(dot_val >= 0.0, 1.0, -1.0)
        if not self.inside_is_negative:
            sign = -sign

        phi = sign * dists
        fallback_normal = n_pseudo if self.inside_is_negative else -n_pseudo
        grad = np.where(diff_norm > 1e-12, sign[:, None] * unit_diff, fallback_normal)
        g_len = np.linalg.norm(grad, axis=1, keepdims=True)
        grad = grad / np.where(g_len < 1e-15, 1.0, g_len)
        return phi, grad

    def _evaluate_legacy_kdtree(self, p: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Legacy M1 centroid KDTree candidate projection for backward compatibility."""
        n_pts = len(p)
        _, cand_idx = self.tree.query(p, k=self.k_candidates)
        if self.k_candidates == 1:
            cand_idx = cand_idx[:, None]

        num_faces = len(self.tri_v0)
        cand_idx = np.clip(cand_idx, 0, num_faces - 1)

        p_flat = np.repeat(p, self.k_candidates, axis=0)
        idx_flat = cand_idx.flatten()

        a_pts = self.tri_v0[idx_flat]
        b_pts = self.tri_v1[idx_flat]
        c_pts = self.tri_v2[idx_flat]

        u = b_pts - a_pts
        v = c_pts - a_pts
        w = p_flat - a_pts

        a_coeff = np.sum(u * u, axis=-1)
        b_coeff = np.sum(u * v, axis=-1)
        c_coeff = np.sum(v * v, axis=-1)
        d_coeff = np.sum(u * w, axis=-1)
        e_coeff = np.sum(v * w, axis=-1)

        det = a_coeff * c_coeff - b_coeff * b_coeff
        scale = a_coeff * c_coeff
        tol = np.maximum(1e-20, 1e-12 * scale)
        det_safe = np.where(np.abs(det) < tol, np.where(det >= 0.0, tol, -tol), det)

        s_param = (c_coeff * d_coeff - b_coeff * e_coeff) / det_safe
        t_param = (a_coeff * e_coeff - b_coeff * d_coeff) / det_safe

        inside_mask = (s_param >= 0.0) & (t_param >= 0.0) & (s_param + t_param <= 1.0)
        q_inside = a_pts + s_param[:, None] * u + t_param[:, None] * v

        s1 = np.clip(d_coeff / np.maximum(a_coeff, 1e-20), 0.0, 1.0)
        q1 = a_pts + s1[:, None] * u
        d1_sq = np.sum((p_flat - q1) ** 2, axis=-1)

        t2 = np.clip(e_coeff / np.maximum(c_coeff, 1e-20), 0.0, 1.0)
        q2 = a_pts + t2[:, None] * v
        d2_sq = np.sum((p_flat - q2) ** 2, axis=-1)

        w_b = p_flat - b_pts
        e_bc = c_pts - b_pts
        bc_len_sq = np.sum(e_bc * e_bc, axis=-1)
        t3 = np.clip(np.sum(w_b * e_bc, axis=-1) / np.maximum(bc_len_sq, 1e-20), 0.0, 1.0)
        q3 = b_pts + t3[:, None] * e_bc
        d3_sq = np.sum((p_flat - q3) ** 2, axis=-1)

        best_edge_sq = np.minimum(d1_sq, np.minimum(d2_sq, d3_sq))
        q_edge = np.where(
            d1_sq[:, None] == best_edge_sq[:, None],
            q1,
            np.where(d2_sq[:, None] == best_edge_sq[:, None], q2, q3),
        )

        q_cand = np.where(inside_mask[:, None], q_inside, q_edge)
        dist_cand_sq = np.sum((p_flat - q_cand) ** 2, axis=-1)

        dist_cand_sq = dist_cand_sq.reshape(n_pts, self.k_candidates)
        q_cand = q_cand.reshape(n_pts, self.k_candidates, 3)
        idx_cand = idx_flat.reshape(n_pts, self.k_candidates)

        min_k = np.argmin(dist_cand_sq, axis=1)
        rows = np.arange(n_pts)

        best_dist_sq = dist_cand_sq[rows, min_k]
        best_q = q_cand[rows, min_k]
        best_face_id = idx_cand[rows, min_k]
        unsigned_dist = np.sqrt(best_dist_sq)

        best_normal = self.normals[best_face_id]
        diff = p - best_q
        dot = np.sum(diff * best_normal, axis=1)

        sign = np.where(dot < 0.0, -1.0, 1.0)
        if not self.inside_is_negative:
            sign = -sign

        phi = sign * unsigned_dist

        diff_max = np.max(np.abs(diff), axis=1, keepdims=True)
        diff_max_safe = np.where(diff_max < 1e-20, 1.0, diff_max)
        scaled_diff = diff / diff_max_safe
        scaled_norm = np.linalg.norm(scaled_diff, axis=1, keepdims=True)
        diff_norm = diff_max * scaled_norm
        boundary_normal = best_normal if self.inside_is_negative else -best_normal
        unit_diff = scaled_diff / np.maximum(scaled_norm, 1e-20)
        grad_phi = np.where(
            diff_norm > 1e-12,
            sign[:, None] * unit_diff,
            boundary_normal,
        )

        grad_len = np.linalg.norm(grad_phi, axis=1, keepdims=True)
        grad_len[grad_len < 1e-15] = 1.0
        grad_phi = grad_phi / grad_len
        return phi, grad_phi

    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x)."""
        phi, _ = self.evaluate(points)
        return np.atleast_1d(phi)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        """Evaluate continuous outward unit normal gradient."""
        _, grad_phi = self.evaluate(points)
        return grad_phi

    def winding_number(self, points: np.ndarray) -> np.ndarray:
        """Computes Generalized Winding Numbers W(x) for query points."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts)

        if self.use_native and self._native_engine:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_winding_number"):
                try:
                    p_contig = np.ascontiguousarray(p, dtype=np.float64)
                    c_points = p_contig.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    out_wn = np.ascontiguousarray(np.empty(len(p), dtype=np.float64))
                    res = dll.cfd_sdf_winding_number(
                        self._native_engine,
                        c_points,
                        ctypes.c_int64(len(p)),
                        out_wn.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    )
                    if res == 0:
                        return float(out_wn[0]) if is_single else out_wn
                except Exception:
                    pass

        return compute_mesh_winding_number(points, self.tri_v0, self.tri_v1, self.tri_v2)

    def compute_winding_numbers(self, points: np.ndarray) -> np.ndarray:
        """Alias for winding_number."""
        return self.winding_number(points)

    def compute_voromesh_bisector_loss(
        self,
        seeds: np.ndarray,
        k: int = 16,
        seed_signs: Optional[np.ndarray] = None,
    ) -> Tuple[float, np.ndarray, np.ndarray]:
        """Compute VoroMesh bisector loss from mesh vertices to seed generators."""
        return voromesh_bisector_loss(
            self.vertices, seeds, k=k, seed_signs=seed_signs, use_native=self.use_native
        )

    def project_to_boundary(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        """Project query points onto the zero level set surface."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        phi, grad = self.evaluate(pts)
        if is_single:
            proj = pts - phi * grad
        else:
            proj = pts - phi[:, None] * grad
        return proj

    def project(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        """Alias for project_to_boundary."""
        return self.project_to_boundary(points, max_iter=max_iter, eps=eps, tol=tol)

    def project_to_surface(self, points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Project query points onto the zero level set surface and return (proj, phi)."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        phi, grad = self.evaluate(pts)
        if is_single:
            proj = pts - phi * grad
        else:
            proj = pts - phi[:, None] * grad
        return proj, phi

    def get_bounding_box(self) -> BoundingBox:
        """Compute spatial bounding box enclosing the surface mesh."""
        return BoundingBox.from_points(self.vertices)



# ==============================================================================
# CAD Feature Filter
# ==============================================================================


class CADFeatureFilter:
    """Robust geometric filter pruning CAD wireframe curves, 2D sketches,
    orphan vertices, degenerate zero-area facets, and non-manifold sheets.
    """

    @staticmethod
    def filter_orphan_vertices(mesh: SurfaceMesh) -> SurfaceMesh:
        """Purge unreferenced / orphan vertices not belonging to any triangular facet."""
        if len(mesh.faces) == 0:
            return mesh

        used_indices, inverse = np.unique(mesh.faces, return_inverse=True)
        if len(used_indices) == len(mesh.vertices):
            return mesh  # Already compact

        compact_vertices = mesh.vertices[used_indices]
        compact_faces = inverse.reshape(mesh.faces.shape).astype(np.int32)
        new_patches = {k: np.copy(v) for k, v in mesh.patches.items()}

        return SurfaceMesh(
            vertices=compact_vertices,
            faces=compact_faces,
            normals=None,  # Automatically recomputed
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )

    @staticmethod
    def weld_coincident_vertices(
        mesh: SurfaceMesh, tol: float = 1e-7
    ) -> SurfaceMesh:
        """Weld duplicate / coincident vertices within spatial tolerance using cKDTree."""
        if len(mesh.vertices) <= 1:
            return mesh

        tree = cKDTree(mesh.vertices)
        pairs = tree.query_pairs(r=tol)
        if not pairs:
            return mesh

        num_verts = len(mesh.vertices)
        parent = np.arange(num_verts, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        for i, j in pairs:
            root_i = find(i)
            root_j = find(j)
            if root_i < root_j:
                parent[root_j] = root_i
            elif root_j < root_i:
                parent[root_i] = root_j

        roots = np.array([find(i) for i in range(num_verts)], dtype=np.int32)
        unique_roots, inv = np.unique(roots, return_inverse=True)

        welded_vertices = mesh.vertices[unique_roots]
        welded_faces = inv[mesh.faces].astype(np.int32)
        new_patches = {k: np.copy(v) for k, v in mesh.patches.items()}

        return SurfaceMesh(
            vertices=welded_vertices,
            faces=welded_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )

    @staticmethod
    def filter_degenerate_faces(
        mesh: SurfaceMesh, min_area: float = 1e-14
    ) -> SurfaceMesh:
        """Remove zero-area triangles and collapsed vertices, re-indexing patches."""
        if len(mesh.faces) == 0:
            return mesh

        f = mesh.faces
        non_collapsed = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 2] != f[:, 0])

        v0 = mesh.vertices[f[:, 0]]
        v1 = mesh.vertices[f[:, 1]]
        v2 = mesh.vertices[f[:, 2]]
        cross = np.cross(v1 - v0, v2 - v0)
        areas = 0.5 * np.linalg.norm(cross, axis=1)

        valid_mask = non_collapsed & (areas >= min_area)
        if np.all(valid_mask):
            return mesh

        new_faces = mesh.faces[valid_mask]

        # Remap patch face indices
        face_map = np.full(len(mesh.faces), -1, dtype=np.int32)
        face_map[valid_mask] = np.arange(np.count_nonzero(valid_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < len(mesh.faces))
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        filtered = SurfaceMesh(
            vertices=mesh.vertices,
            faces=new_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(filtered)

    @staticmethod
    def extract_dominant_manifold_shell(
        mesh: SurfaceMesh, weld_tol: float = 1e-7
    ) -> SurfaceMesh:
        """Extract the dominant 2-manifold surface shell across strict 2-manifold edges
        (len == 2), discarding non-manifold fins (>2 faces) and re-indexing patches.
        """
        if len(mesh.faces) <= 1:
            return mesh

        # Weld coincident vertices first
        if weld_tol > 0.0 and len(mesh.vertices) > 1:
            mesh = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)

        num_faces = len(mesh.faces)

        # Build adjacency graph between faces sharing undirected edges
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        for f_idx, face in enumerate(mesh.faces):
            e0 = (min(face[0], face[1]), max(face[0], face[1]))
            e1 = (min(face[1], face[2]), max(face[1], face[2]))
            e2 = (min(face[2], face[0]), max(face[2], face[0]))
            for e in (e0, e1, e2):
                edge_to_faces.setdefault(e, []).append(f_idx)

        # Disjoint set union (Union-Find) on faces across strict 2-manifold edges
        parent = np.arange(num_faces, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        def union(i: int, j: int) -> None:
            root_i = find(i)
            root_j = find(j)
            if root_i != root_j:
                parent[root_i] = root_j

        # Strict 2-manifold adjacency: only join faces sharing an edge with exactly 2 faces
        for f_list in edge_to_faces.values():
            if len(f_list) == 2:
                union(f_list[0], f_list[1])

        # Compute face areas
        v0 = mesh.vertices[mesh.faces[:, 0]]
        v1 = mesh.vertices[mesh.faces[:, 1]]
        v2 = mesh.vertices[mesh.faces[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)

        # Sum areas per component
        comp_areas: Dict[int, float] = {}
        for f_idx in range(num_faces):
            r = find(f_idx)
            comp_areas[r] = comp_areas.get(r, 0.0) + areas[f_idx]

        dominant_root = max(comp_areas.keys(), key=lambda r: comp_areas[r])
        dominant_mask = np.array([find(f_idx) == dominant_root for f_idx in range(num_faces)], dtype=bool)

        if np.all(dominant_mask):
            return mesh

        # Remap patch face indices
        face_map = np.full(num_faces, -1, dtype=np.int32)
        face_map[dominant_mask] = np.arange(np.count_nonzero(dominant_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < num_faces)
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        dom_mesh = SurfaceMesh(
            vertices=mesh.vertices,
            faces=mesh.faces[dominant_mask],
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(dom_mesh)

    @staticmethod
    def sanitize(
        mesh: SurfaceMesh, min_area: float = 1e-14, weld_tol: float = 1e-7
    ) -> SurfaceMesh:
        """Execute complete sanitization chain:
        1. Weld coincident vertices within spatial tolerance.
        2. Remove degenerate and zero-area facets.
        3. Extract dominant 2-manifold shell.
        4. Compact and purge orphan sketch vertices.
        """
        clean = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)
        clean = CADFeatureFilter.filter_degenerate_faces(clean, min_area=min_area)
        clean = CADFeatureFilter.extract_dominant_manifold_shell(clean, weld_tol=0.0)
        clean = CADFeatureFilter.filter_orphan_vertices(clean)
        return clean


# ==============================================================================
# Curvature Refinement
# ==============================================================================


class CurvatureMethod(str, Enum):
    """Method for evaluating surface curvature."""

    SDF_LAPLACIAN = "sdf_laplacian"
    SURFACE_MESH = "surface_mesh"
    HYBRID = "hybrid"


class CurvatureRefinement:
    """Computes continuous curvature kappa(x) and curvature-adapted size h_curv(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
    ) -> None:
        """Initialize curvature evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
            surface_mesh: Optional SurfaceMesh for hybrid evaluation.
        """
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim
        self.surface_mesh = surface_mesh
        self._mesh_kdtree: Optional[Any] = None
        self._mesh_vertex_curvatures: Optional[np.ndarray] = None

        if self.surface_mesh is not None:
            self._precompute_surface_curvatures()

    def _precompute_surface_curvatures(self) -> None:
        """Precompute vertex curvatures on triangulated SurfaceMesh."""
        mesh = self.surface_mesh
        verts = mesh.vertices  # (V, 3)
        faces = mesh.faces  # (F, 3)
        normals = mesh.normals  # (F, 3)

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
        """Evaluate curvature magnitude kappa(x) for an array of coordinates.

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            kappa: Array of shape (N,), float64 non-negative curvature values.
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape
        delta = self.params.fd_delta

        # Vectorized finite difference Laplacian
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

        if d == 2:
            kappa = np.abs(laplacian)
        else:
            kappa = 0.5 * np.abs(laplacian)  # Mean curvature |H|

        # Hybrid blend if SurfaceMesh is present
        if self._mesh_kdtree is not None and self._mesh_vertex_curvatures is not None:
            dists, idxs = self._mesh_kdtree.query(pts, k=1)
            mesh_k = self._mesh_vertex_curvatures[idxs]
            decay = 1.0 / (1.0 + dists / self.params.h0)
            kappa = np.maximum(kappa, mesh_k * decay)

        return kappa[0] if single_point else kappa

    def evaluate_h_curv(self, points: np.ndarray) -> np.ndarray:
        """Compute local curvature-adapted element size h_curv(x).

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            h_curv: Local element sizes in [h_min, h_max].
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        kappa = self.evaluate_curvature(pts)
        denom = np.maximum(kappa, self.params.eps_curv)
        h_curv = self.params.alpha / denom
        h_clipped = np.clip(h_curv, self.params.h_min, self.params.h_max)

        return h_clipped[0] if single_point else h_clipped


# ==============================================================================
# Proximity Refinement
# ==============================================================================


class ProximityRefinement:
    """Detects opposing walls and computes proximity-adapted size h_prox(x).

    Supports:
    - External aerodynamic flow domains where fluid has phi > 0 (marching along +n_start).
    - Internal channel flows where fluid has phi < 0 (marching along -n_start).
    - Both inside_is_negative=True and inside_is_negative=False sign conventions.
    """

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        domain_is_negative: Optional[bool] = None,
        inside_is_negative: Optional[bool] = None,
    ) -> None:
        """Initialize proximity evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
            domain_is_negative: If True, fluid domain is phi < 0. If False, fluid is phi > 0.
                                If None, auto-detects per query point.
            inside_is_negative: Convenient alias; sets domain_is_negative = not inside_is_negative.
        """
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
        """Compute narrow gap distance d_gap(x) along the normal ray.

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            d_gap: Array of shape (N,), float64 gap distance. Clamped to max_gap_search
                   if no opposing wall exists within range.
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape

        phi_0_eval, grad_0 = self.sdf_evaluator(pts)
        phi_0 = np.atleast_1d(phi_0_eval)

        # Determine ray marching direction per point based on domain sign:
        # External fluid (phi > 0): ray marches along +n_start (into fluid, away from solid body)
        # Internal channel (phi < 0): ray marches along -n_start (into channel cavity)
        is_external = np.zeros(n_pts, dtype=bool)
        for i in range(n_pts):
            if self.domain_is_negative is True:
                is_external[i] = False
            elif self.domain_is_negative is False:
                is_external[i] = True
            else:
                is_external[i] = (phi_0[i] >= 0.0)

        # Guard against zero gradient on medial axis / symmetry plane
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

        # Use non-linear (quadratic) distribution to densely sample near-wall region
        u_samples = np.linspace(1.0 / n_steps, 1.0, n_steps)  # shape (S,)
        t_samples = (u_samples**2) * r_max

        v_ray = np.where(is_external[:, None], n_start, -n_start)

        # Construct candidate ray evaluation points: shape (N, S, d)
        ray_pts = pts[:, None, :] + (t_samples[None, :, None]) * v_ray[:, None, :]
        flat_ray_pts = ray_pts.reshape(-1, d)

        # Batch evaluate all sample points
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

            if is_external[i]:
                # External: marching into fluid (phi > 0), look for solid crossing where phi <= 0
                crossings = np.where(phi_ray[i] <= 0.0)[0]
            else:
                # Internal: marching into channel (phi < 0), look for boundary crossing where phi >= 0
                crossings = np.where(phi_ray[i] >= 0.0)[0]

            if len(crossings) == 0:
                continue

            first_cross = crossings[0]
            if first_cross == 0:
                t_prev = 0.0
                phi_prev = phi_0[i]
                t_curr = t_samples[0]
                phi_curr = phi_ray[i, 0]
                hit_grad = grad_ray[i, 0]
            else:
                t_prev = t_samples[first_cross - 1]
                t_curr = t_samples[first_cross]
                phi_prev = phi_ray[i, first_cross - 1]
                phi_curr = phi_ray[i, first_cross]
                hit_grad = grad_ray[i, first_cross]

            denom = phi_curr - phi_prev
            frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
            frac = np.clip(frac, 0.0, 1.0)
            t_star = t_prev + frac * (t_curr - t_prev)

            hit_norm = np.linalg.norm(hit_grad)
            if hit_norm > 1e-12:
                n_hit = hit_grad / hit_norm
                alignment = np.dot(n_start[i], n_hit)
                if alignment <= self.params.theta_opp_threshold:
                    d_gap[i] = d_near[i] + t_star

        return d_gap[0] if single_point else d_gap

    def evaluate_h_prox(self, points: np.ndarray) -> np.ndarray:
        """Compute local proximity-adapted element size h_prox(x).

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            h_prox: Local element sizes in [h_min, h_max].
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        d_gap = self.evaluate_gap_distance(pts)
        h_prox = self.params.beta * d_gap
        h_clipped = np.clip(h_prox, self.params.h_min, self.params.h_max)

        return h_clipped[0] if single_point else h_clipped


# ==============================================================================
# Adaptive Size Function
# ==============================================================================


class AdaptiveSizeFunction:
    """Unified adaptive sizing field h(x) combining curvature, proximity, and grading."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
    ) -> None:
        """Initialize adaptive sizing engine.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing configuration parameters.
            dim: Spatial dimension (2 or 3).
            surface_mesh: Optional SurfaceMesh for CAD geometry integration.
        """
        self.params = params or SizingParameters()
        self.dim = dim
        self.sdf_evaluator = sdf_evaluator
        self.curvature_engine = CurvatureRefinement(
            sdf_evaluator=sdf_evaluator, params=self.params, dim=dim, surface_mesh=surface_mesh
        )
        self.proximity_engine = ProximityRefinement(
            sdf_evaluator=sdf_evaluator, params=self.params, dim=dim
        )

    def evaluate(self, points: np.ndarray) -> np.ndarray:
        """Evaluate combined element sizing field h(x).

        h(x) = max(h_min, min(h_max, h_curv(x), h_prox(x)))

        Args:
            points: Coordinate array of shape (N, d) or (d,).

        Returns:
            h: Target element sizes in [h_min, h_max].
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        h_curv = self.curvature_engine.evaluate_h_curv(pts)
        h_prox = self.proximity_engine.evaluate_h_prox(pts)

        h_combined = np.minimum(h_curv, h_prox)
        h_clamped = np.clip(h_combined, self.params.h_min, self.params.h_max)

        return h_clamped[0] if single_point else h_clamped

    def evaluate_with_breakdown(self, points: np.ndarray) -> Dict[str, np.ndarray]:
        """Evaluate all sizing components for diagnostics, audit, and plotting.

        Returns:
            Dictionary containing 'h', 'h_curv', 'h_prox', 'kappa', 'd_gap'.
        """
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
        """Enforce Lipschitz grading continuity ||grad h|| <= G across discrete points.

        Args:
            points: Array of shape (N, d).
            sizes: Initial sizing array of shape (N,).
            max_iterations: Number of KDTree relaxation sweeps.

        Returns:
            sizes_graded: Filtered sizing array satisfying metric grading.
        """
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
            neighbor_h = h[idxs]  # shape (N, k)
            limit_h = neighbor_h + g * dists
            min_limit = np.min(limit_h, axis=1)
            h = np.minimum(h, min_limit)

        return np.clip(h, self.params.h_min, self.params.h_max)
