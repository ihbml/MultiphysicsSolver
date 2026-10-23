"""Data models for CFDSolver CAD & Geometry Ingestion Engine.

Defines core geometric representation structures:
- BoundingBox: Axis-aligned spatial bounding volume
- SurfaceMesh: Triangulated surface representation with boundary patches
- AuditReport: Comprehensive topological and geometric verification report
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional
import numpy as np


@dataclass
class BoundingBox:
    """Axis-Aligned Bounding Box (AABB) in 3D Euclidean space."""

    min_pt: np.ndarray  # shape (3,), float64
    max_pt: np.ndarray  # shape (3,), float64

    def __post_init__(self) -> None:
        self.min_pt = np.asarray(self.min_pt, dtype=np.float64)
        self.max_pt = np.asarray(self.max_pt, dtype=np.float64)
        if self.min_pt.shape != (3,) or self.max_pt.shape != (3,):
            raise ValueError(f"BoundingBox points must have shape (3,), got min={self.min_pt.shape}, max={self.max_pt.shape}")

    @property
    def span(self) -> np.ndarray:
        """Vector span (dimensions) along X, Y, Z axes."""
        return self.max_pt - self.min_pt

    @property
    def center(self) -> np.ndarray:
        """Geometric centroid of the bounding box."""
        return (self.min_pt + self.max_pt) / 2.0

    @property
    def diagonal(self) -> float:
        """Length of the diagonal spanning min_pt to max_pt."""
        return float(np.linalg.norm(self.span))

    @property
    def volume(self) -> float:
        """Volume enclosed by the axis-aligned bounding box."""
        sp = self.span
        return float(sp[0] * sp[1] * sp[2])

    @classmethod
    def from_points(cls, points: np.ndarray) -> BoundingBox:
        """Compute bounding box enclosing an array of 3D points."""
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim != 2 or pts.shape[1] != 3 or len(pts) == 0:
            raise ValueError(f"Expected non-empty point array of shape (N, 3), got shape {pts.shape}")
        min_pt = np.min(pts, axis=0)
        max_pt = np.max(pts, axis=0)
        return cls(min_pt=min_pt, max_pt=max_pt)

    def scale(self, factor: float) -> BoundingBox:
        """Return a new BoundingBox scaled uniformly by factor."""
        return BoundingBox(min_pt=self.min_pt * factor, max_pt=self.max_pt * factor)


@dataclass
class SurfaceMesh:
    """Triangulated surface mesh with optional patch assignments and metadata."""

    vertices: np.ndarray  # shape (V, 3), float64
    faces: np.ndarray  # shape (F, 3), int32
    normals: Optional[np.ndarray] = None  # shape (F, 3), float64
    patches: Dict[str, np.ndarray] = field(default_factory=dict)  # patch_name -> face_indices array
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self.vertices = np.asarray(self.vertices, dtype=np.float64)
        self.faces = np.asarray(self.faces, dtype=np.int32)

        if self.vertices.ndim != 2 or self.vertices.shape[1] != 3:
            raise ValueError(f"Vertices must have shape (V, 3), got {self.vertices.shape}")
        if self.faces.ndim != 2 or self.faces.shape[1] != 3:
            raise ValueError(f"Faces must have shape (F, 3), got {self.faces.shape}")

        if self.normals is not None:
            self.normals = np.asarray(self.normals, dtype=np.float64)
            if self.normals.ndim != 2 or self.normals.shape[1] != 3 or len(self.normals) != len(self.faces):
                raise ValueError(
                    f"Normals must have shape ({len(self.faces)}, 3), got {self.normals.shape}"
                )
        else:
            self.normals = self.compute_face_normals()

    def compute_face_normals(self) -> np.ndarray:
        """Compute unit normal vector for each triangular face."""
        if len(self.faces) == 0:
            return np.empty((0, 3), dtype=np.float64)

        v0 = self.vertices[self.faces[:, 0]]
        v1 = self.vertices[self.faces[:, 1]]
        v2 = self.vertices[self.faces[:, 2]]

        cross = np.cross(v1 - v0, v2 - v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        # Avoid division by zero for degenerate facets
        lengths[lengths < 1e-15] = 1.0
        return cross / lengths

    def get_bounding_box(self) -> BoundingBox:
        """Calculate the axis-aligned bounding box of the mesh."""
        return BoundingBox.from_points(self.vertices)

    def copy(self) -> SurfaceMesh:
        """Produce a deep copy of the surface mesh."""
        new_patches = {k: np.copy(v) for k, v in self.patches.items()}
        return SurfaceMesh(
            vertices=np.copy(self.vertices),
            faces=np.copy(self.faces),
            normals=np.copy(self.normals) if self.normals is not None else None,
            patches=new_patches,
            metadata=dict(self.metadata),
        )


@dataclass
class AuditReport:
    """Comprehensive topological and geometric audit report for OpenFOAM snappyHexMesh."""

    is_watertight: bool
    is_manifold: bool
    euler_characteristic: int
    open_edges_count: int
    multi_edges_count: int
    degenerate_faces_count: int
    normal_zones_count: int
    signed_volume: float
    is_outward_oriented: bool
    quality_min: float
    quality_histogram: List[int]
    self_intersections_count: int
    bounding_box_min: np.ndarray
    bounding_box_max: np.ndarray
    passed_openfoam_surfacecheck: bool
    details: Dict[str, Any] = field(default_factory=dict)
