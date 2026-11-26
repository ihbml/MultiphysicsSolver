"""Base SDF interface, analytical primitives, CSG operators, and SDFEngine facade."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import BoundingBox


def smooth_min(a: np.ndarray, b: np.ndarray, k: float = 0.0) -> np.ndarray:
    """Evaluate polynomial smooth minimum between two distance fields with blending radius k."""
    if k <= 1e-12:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def smooth_max(a: np.ndarray, b: np.ndarray, k: float = 0.0) -> np.ndarray:
    """Evaluate polynomial smooth maximum between two distance fields with blending radius k."""
    return -smooth_min(-a, -b, k)


class BaseSDF(ABC):
    """Abstract base class for all continuous signed distance field evaluators."""

    @abstractmethod
    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x) for points of shape (N, D) or (D,)."""
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


SDFNode = BaseSDF


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
    """Unified Facade for continuous SDF evaluation and JSON/dict geometry tree building."""

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
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.distance(points)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.gradient(points, eps=eps)

    def evaluate(
        self,
        points_or_geom: Optional[Union[np.ndarray, BaseSDF, Dict[str, Any]]] = None,
        points: Optional[np.ndarray] = None,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluate continuous signed distance and unit normal gradient: (phi, grad_phi)."""
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
            return CSGUnion(SDFEngine.from_dict(spec["a"]), SDFEngine.from_dict(spec["b"]), smooth_k=spec.get("smooth", 0.0))
        elif kind in ("intersection",):
            return CSGIntersection(SDFEngine.from_dict(spec["a"]), SDFEngine.from_dict(spec["b"]), smooth_k=spec.get("smooth", 0.0))
        elif kind in ("difference",):
            return CSGDifference(SDFEngine.from_dict(spec["a"]), SDFEngine.from_dict(spec["b"]), smooth_k=spec.get("smooth", 0.0))
        elif kind in ("complement", "invert"):
            return CSGComplement(SDFEngine.from_dict(spec["child"]))
        else:
            raise ValueError(f"Unknown SDF geometry kind: '{kind}'")
