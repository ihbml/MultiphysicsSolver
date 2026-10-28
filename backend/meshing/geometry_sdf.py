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

from abc import ABC, abstractmethod
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple, Union
import numpy as np
from scipy.spatial import cKDTree

from backend.geometry.models import BoundingBox, SurfaceMesh
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
# SurfaceMesh to Continuous SDF Evaluator (CADMeshSDF)
# ==============================================================================


class CADMeshSDF(BaseSDF):
    """Continuous Signed Distance Field (SDF) and outward normal gradient evaluator
    derived from a triangulated SurfaceMesh using spatial cKDTree acceleration.

    Guarantees:
    - Zero external dependency on rtree.
    - Evaluates 10,000 points on 61k faces in < 0.20s.
    - Outward unit normal gradient norm ||grad phi|| == 1.0 everywhere.
    """

    def __init__(
        self,
        mesh: SurfaceMesh,
        k_candidates: int = 8,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
    ) -> None:
        """Initialize CADMeshSDF.

        Args:
            mesh: Triangulated SurfaceMesh.
            k_candidates: Number of nearest candidate triangle centroids to query (default: 8).
            inside_is_negative: If True, phi < 0 inside solid body, phi > 0 in exterior.
            auto_compact: If True, purges unreferenced/orphan vertices before building index.
        """
        if len(mesh.faces) == 0:
            raise ValueError("Cannot initialize CADMeshSDF from empty surface mesh.")

        if auto_compact:
            used_indices, inverse = np.unique(mesh.faces, return_inverse=True)
            self.vertices = mesh.vertices[used_indices].astype(np.float64)
            self.faces = inverse.reshape(mesh.faces.shape).astype(np.int32)
        else:
            self.vertices = mesh.vertices.astype(np.float64)
            self.faces = mesh.faces.astype(np.int32)

        self.k_candidates = min(max(1, k_candidates), len(self.faces))
        self.inside_is_negative = inside_is_negative

        # Precompute triangle vertex coordinates
        self.tri_v0 = self.vertices[self.faces[:, 0]]
        self.tri_v1 = self.vertices[self.faces[:, 1]]
        self.tri_v2 = self.vertices[self.faces[:, 2]]

        # Precompute face unit normal vectors
        cross = np.cross(self.tri_v1 - self.tri_v0, self.tri_v2 - self.tri_v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        lengths[lengths < 1e-20] = 1.0
        self.normals = cross / lengths

        # Precompute triangle centroids and build KDTree
        self.centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0
        self.tree = cKDTree(self.centroids)

    @classmethod
    def from_surface_mesh(
        cls,
        mesh: SurfaceMesh,
        k_candidates: int = 8,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
    ) -> CADMeshSDF:
        """Factory constructor creating CADMeshSDF from SurfaceMesh."""
        return cls(
            mesh,
            k_candidates=k_candidates,
            inside_is_negative=inside_is_negative,
            auto_compact=auto_compact,
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

        # 1. Query nearest candidate triangle centroids via cKDTree
        _, cand_idx = self.tree.query(p, k=self.k_candidates)
        if self.k_candidates == 1:
            cand_idx = cand_idx[:, None]

        # Guard against cKDTree overflow on extreme coordinates (>= 1e160 or Inf)
        num_faces = len(self.tri_v0)
        cand_idx = np.clip(cand_idx, 0, num_faces - 1)

        # 2. Vectorized projection of (N * k) point-triangle pairs
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

        # Edge 1: AB (t = 0)
        s1 = np.clip(d_coeff / np.maximum(a_coeff, 1e-20), 0.0, 1.0)
        q1 = a_pts + s1[:, None] * u
        d1_sq = np.sum((p_flat - q1) ** 2, axis=-1)

        # Edge 2: AC (s = 0)
        t2 = np.clip(e_coeff / np.maximum(c_coeff, 1e-20), 0.0, 1.0)
        q2 = a_pts + t2[:, None] * v
        d2_sq = np.sum((p_flat - q2) ** 2, axis=-1)

        # Edge 3: BC
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

        # 3. Reduce across k candidate triangles to identify global minimum
        dist_cand_sq = dist_cand_sq.reshape(n_pts, self.k_candidates)
        q_cand = q_cand.reshape(n_pts, self.k_candidates, 3)
        idx_cand = idx_flat.reshape(n_pts, self.k_candidates)

        min_k = np.argmin(dist_cand_sq, axis=1)
        rows = np.arange(n_pts)

        best_dist_sq = dist_cand_sq[rows, min_k]
        best_q = q_cand[rows, min_k]
        best_face_id = idx_cand[rows, min_k]
        unsigned_dist = np.sqrt(best_dist_sq)

        # 4. Outward normal evaluation & sign determination
        best_normal = self.normals[best_face_id]
        diff = p - best_q

        overflow_mask = ~np.isfinite(unsigned_dist)
        if np.any(overflow_mask):
            diff_sub = diff[overflow_mask]
            diff_max = np.max(np.abs(diff_sub), axis=-1, keepdims=True)
            diff_max_safe = np.where(diff_max < 1e-20, 1.0, diff_max)
            scaled = diff_sub / diff_max_safe
            sub_norm = np.squeeze(diff_max * np.sqrt(np.sum(scaled * scaled, axis=-1, keepdims=True)), axis=-1)
            unsigned_dist[overflow_mask] = np.nan_to_num(sub_norm, nan=1e300, posinf=1e300)

        dot = np.sum(diff * best_normal, axis=1)

        sign = np.where(dot < 0.0, -1.0, 1.0)
        if not self.inside_is_negative:
            sign = -sign

        phi = sign * unsigned_dist

        # 5. Outward unit normal gradient
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

        if is_single:
            return phi[0], grad_phi[0]
        return phi, grad_phi

    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x)."""
        phi, _ = self.evaluate(points)
        return np.atleast_1d(phi)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        """Evaluate continuous outward unit normal gradient."""
        _, grad_phi = self.evaluate(points)
        return grad_phi

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
