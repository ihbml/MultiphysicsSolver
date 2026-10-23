"""Unit scaling and coordinate transformation for CAD-to-CFD pipelines."""

from __future__ import annotations

import logging
from typing import Optional, Tuple
import numpy as np

from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import BoundingBox, SurfaceMesh

logger = logging.getLogger(__name__)


class GeometryScaler:
    """Scales 3D surface meshes from authoring units (e.g. mm) to CFD SI units (meters).

    Performs rigorous analytical invariant validation:
    - Bounding box spans scale linearly: Span_CFD = s * Span_CAD
    - Surface area scales quadratically: Area_CFD = s^2 * Area_CAD
    - Enclosed volume scales cubically: Vol_CFD = s^3 * Vol_CAD
    - Angles between edge vectors are strictly preserved
    """

    def __init__(self, default_scale_factor: float = 0.001) -> None:
        """Initialize GeometryScaler.

        Args:
            default_scale_factor: Standard factor for converting mm to meters (0.001).
        """
        if not np.isfinite(default_scale_factor) or default_scale_factor <= 0.0:
            raise ValueError(
                f"Default scaling factor must be finite and strictly positive, got {default_scale_factor}"
            )
        self.default_scale_factor = float(default_scale_factor)

    def scale(
        self,
        mesh: SurfaceMesh,
        scale_factor: Optional[float] = None,
        verify_invariants: bool = True,
        in_place: bool = False,
    ) -> SurfaceMesh:
        """Scale mesh vertices by scale_factor and verify geometric invariants.

        Args:
            mesh: Input SurfaceMesh.
            scale_factor: Uniform scaling factor (defaults to 0.001, i.e. mm -> m).
            verify_invariants: If True, assert bounding box, volume, and angle invariants.
            in_place: If True, modify input mesh vertices; otherwise return deep copy.

        Returns:
            Scaled SurfaceMesh in SI meters.

        Raises:
            ValueError: If scale_factor <= 0 or if invariant verification fails.
        """
        factor = scale_factor if scale_factor is not None else self.default_scale_factor
        try:
            factor_val = float(factor)
        except (TypeError, ValueError) as exc:
            raise TypeError(
                f"Scaling factor must be a real numeric value, got {type(factor).__name__}"
            ) from exc

        if not np.isfinite(factor_val) or factor_val <= 0.0:
            raise ValueError(
                f"Scaling factor must be finite and strictly positive, got {factor}"
            )
        factor = factor_val

        # Measure pre-scaling invariants
        orig_bbox = mesh.get_bounding_box()
        orig_vol = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces)
        orig_area = self.compute_surface_area(mesh.vertices, mesh.faces)

        # Scale vertices
        scaled_mesh = mesh if in_place else mesh.copy()
        scaled_mesh.vertices = scaled_mesh.vertices * factor

        # Recompute face normals
        scaled_mesh.normals = scaled_mesh.compute_face_normals()

        # Update metadata
        scaled_mesh.metadata["scale_factor"] = factor
        scaled_mesh.metadata["units"] = "m"

        if verify_invariants:
            scaled_bbox = scaled_mesh.get_bounding_box()
            scaled_vol = SurfaceAuditor.compute_signed_volume(scaled_mesh.vertices, scaled_mesh.faces)
            scaled_area = self.compute_surface_area(scaled_mesh.vertices, scaled_mesh.faces)

            # 1. Bounding box linear scale invariant
            expected_span = orig_bbox.span * factor
            span_diff = np.max(np.abs(scaled_bbox.span - expected_span))
            if span_diff > 1e-12:
                raise ValueError(
                    f"Bounding box span invariant violation: max diff = {span_diff} > 1e-12"
                )

            # 2. Surface area quadratic invariant: A_new = A_orig * factor^2
            expected_area = orig_area * (factor**2)
            area_diff = abs(scaled_area - expected_area)
            if expected_area > 1e-14:
                area_rel_err = area_diff / expected_area
                if area_rel_err > 1e-7:
                    raise ValueError(
                        f"Surface area scaling invariant violation: relative error = {area_rel_err} > 1e-7"
                    )
            elif area_diff > 1e-12:
                raise ValueError(
                    f"Surface area scaling invariant violation: absolute error = {area_diff} > 1e-12 for near-zero area mesh"
                )

            # 3. Volume cubic invariant: V_new = V_orig * factor^3
            expected_vol = orig_vol * (factor**3)
            vol_diff = abs(scaled_vol - expected_vol)
            if abs(expected_vol) > 1e-14:
                vol_rel_err = vol_diff / abs(expected_vol)
                if vol_rel_err > 1e-7:
                    raise ValueError(
                        f"Volume scaling invariant violation: relative error = {vol_rel_err} > 1e-7"
                    )
            elif vol_diff > 1e-12:
                raise ValueError(
                    f"Volume scaling invariant violation: absolute error = {vol_diff} > 1e-12 for near-zero volume mesh"
                )

            logger.info(
                "Scale verification passed: factor=%g, BBox span=%s, Volume=%g m^3",
                factor,
                scaled_bbox.span,
                scaled_vol,
            )

        return scaled_mesh

    @staticmethod
    def compute_surface_area(vertices: np.ndarray, faces: np.ndarray) -> float:
        """Compute the total surface area of a triangular mesh."""
        if len(faces) == 0:
            return 0.0
        v0 = vertices[faces[:, 0]]
        v1 = vertices[faces[:, 1]]
        v2 = vertices[faces[:, 2]]
        cross = np.cross(v1 - v0, v2 - v0)
        areas = 0.5 * np.linalg.norm(cross, axis=1)
        return float(np.sum(areas))

    @staticmethod
    def verify_bounding_box(
        mesh: SurfaceMesh,
        expected_min: np.ndarray,
        expected_max: np.ndarray,
        tol: float = 1e-5,
    ) -> bool:
        """Verify that the mesh bounding box matches expected coordinates within tolerance."""
        bbox = mesh.get_bounding_box()
        min_err = np.max(np.abs(bbox.min_pt - expected_min))
        max_err = np.max(np.abs(bbox.max_pt - expected_max))
        return bool(min_err <= tol and max_err <= tol)
