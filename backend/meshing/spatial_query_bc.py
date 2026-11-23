"""Spatial-Query Boundary Condition Selector and 1->N Patch Conflict Detector.

Implements §13.1 (1.B) rule-based boundary patch assignment using boolean/comparison
predicates evaluated over face centres and outward surface normals, with explicit
conflict detection on overlapping or ambiguous 1->N CAD/mesh remappings.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np

from backend.meshing.models import UnstructuredMesh
from backend.native_rust.expression_engine import ExpressionEngine


class SpatialQueryConflictError(ValueError):
    """Raised when a face or CAD patch matches ambiguous or overlapping BC rules."""


@dataclass(frozen=True)
class SpatialQueryRule:
    """Declarative boundary condition assignment rule over surface geometry."""

    patch_name: str
    expression: Optional[str] = None
    patch_type: str = "wall"
    priority: int = 0
    aabb_min: Optional[Tuple[float, float, float]] = None
    aabb_max: Optional[Tuple[float, float, float]] = None
    predicate: Optional[Callable[[np.ndarray, np.ndarray], np.ndarray]] = None


class SpatialQuerySelector:
    """Assigns boundary patches via geometric spatial-query expressions and conflict checks."""

    def __init__(
        self,
        rules: Optional[Sequence[SpatialQueryRule]] = None,
        *,
        strict_disjoint: bool = True,
        require_full_coverage: bool = False,
        default_patch: str = "defaultFaces",
    ) -> None:
        self.rules: List[SpatialQueryRule] = list(rules or [])
        self.strict_disjoint = strict_disjoint
        self.require_full_coverage = require_full_coverage
        self.default_patch = default_patch

    def add_rule(self, rule: SpatialQueryRule) -> None:
        """Append a boundary assignment rule."""
        self.rules.append(rule)

    def evaluate_rule(
        self,
        rule: SpatialQueryRule,
        face_centres: np.ndarray,
        face_normals: np.ndarray,
        face_area_mag: Optional[np.ndarray] = None,
    ) -> np.ndarray:
        """Evaluate boolean selection mask (N_bnd,) for a single rule."""
        n_faces = len(face_centres)
        if n_faces == 0:
            return np.zeros(0, dtype=bool)

        mask = np.ones(n_faces, dtype=bool)
        if rule.aabb_min is not None:
            bmin = np.asarray(rule.aabb_min, dtype=np.float64)
            mask &= np.all(face_centres >= bmin[None, :] - 1e-12, axis=1)
        if rule.aabb_max is not None:
            bmax = np.asarray(rule.aabb_max, dtype=np.float64)
            mask &= np.all(face_centres <= bmax[None, :] + 1e-12, axis=1)

        if rule.expression:
            eng = ExpressionEngine()
            areas = face_area_mag if face_area_mag is not None else np.ones(n_faces, dtype=np.float64)
            extra = {
                "normal_x": face_normals[:, 0],
                "normal_y": face_normals[:, 1],
                "normal_z": face_normals[:, 2],
                "nx": face_normals[:, 0],
                "ny": face_normals[:, 1],
                "nz": face_normals[:, 2],
                "area": areas,
            }
            expr_val = eng.evaluate_boundary_profile(
                rule.expression,
                face_centres[:, 0],
                face_centres[:, 1],
                face_centres[:, 2],
                extra_vars=extra,
            )
            mask &= np.asarray(expr_val, dtype=bool)

        if rule.predicate is not None:
            pred_val = np.asarray(rule.predicate(face_centres, face_normals), dtype=bool)
            mask &= pred_val

        return mask

    def assign_patches(
        self,
        mesh: UnstructuredMesh,
        rules: Optional[Sequence[SpatialQueryRule]] = None,
    ) -> Dict[str, Tuple[int, int]]:
        """Evaluate spatial query rules over boundary faces and detect 1->N overlap conflicts.

        Args:
            mesh: UnstructuredMesh with contiguous boundary faces at ``[n_int, n_faces)``.
            rules: Optional rules overriding ``self.rules``.

        Returns:
            Dictionary mapping patch name -> ``(global_start_face, count)`` and
            per-patch boundary face indices when checking disjointness.

        Raises:
            SpatialQueryConflictError: On duplicate patch names, overlapping rules at equal
                priority (ambiguous 1->N assignment), or uncovered faces under strict coverage.
        """
        active_rules = list(rules) if rules is not None else list(self.rules)
        if not active_rules:
            return dict(mesh.boundaries)

        bnd_mask = mesh.neighbour < 0
        bnd_indices = np.where(bnd_mask)[0]
        n_bnd = len(bnd_indices)
        if n_bnd == 0:
            return {}

        # Verify unique rule patch names
        seen_names: set[str] = set()
        for r in active_rules:
            if r.patch_name in seen_names:
                raise SpatialQueryConflictError(
                    f"Duplicate patch rule name '{r.patch_name}' triggers ambiguous 1->N remapping."
                )
            seen_names.add(r.patch_name)

        cf = mesh.face_centres[bnd_indices]
        sf = mesh.face_areas[bnd_indices]
        area_mag = np.linalg.norm(sf, axis=1)
        normals = np.divide(
            sf,
            area_mag[:, None],
            out=np.zeros_like(sf),
            where=area_mag[:, None] > 1e-30,
        )

        match_counts = np.zeros(n_bnd, dtype=np.int32)
        assigned = np.full(n_bnd, -1, dtype=np.int32)
        priorities = np.full(n_bnd, -10**9, dtype=np.int64)

        for r_idx, rule in enumerate(active_rules):
            rmask = self.evaluate_rule(rule, cf, normals, area_mag)
            overlap = rmask & (assigned >= 0)
            if np.any(overlap):
                if self.strict_disjoint or np.any(priorities[overlap] == rule.priority):
                    conflict_idx = int(np.where(overlap)[0][0])
                    prev_name = active_rules[int(assigned[conflict_idx])].patch_name
                    raise SpatialQueryConflictError(
                        f"Ambiguous 1->N boundary conflict on face {int(bnd_indices[conflict_idx])}: "
                        f"matched both '{prev_name}' and '{rule.patch_name}'."
                    )
            take = rmask & ((assigned < 0) | (rule.priority > priorities))
            match_counts[rmask] += 1
            assigned[take] = r_idx
            priorities[take] = rule.priority

        if self.require_full_coverage and np.any(assigned < 0):
            missing = int(np.sum(assigned < 0))
            raise SpatialQueryConflictError(
                f"{missing} boundary faces did not match any spatial-query BC rule."
            )

        # Group faces cleanly by rule (for contiguous meshes or reordered ranges)
        n_int = int(np.sum(mesh.neighbour >= 0))
        out_boundaries: Dict[str, Tuple[int, int]] = {}
        patch_types: Dict[str, str] = {}
        cursor = n_int
        for r_idx, rule in enumerate(active_rules):
            cnt = int(np.sum(assigned == r_idx))
            if cnt > 0:
                out_boundaries[rule.patch_name] = (cursor, cnt)
                patch_types[rule.patch_name] = rule.patch_type
                cursor += cnt

        unmatched = int(np.sum(assigned < 0))
        if unmatched > 0:
            out_boundaries[self.default_patch] = (cursor, unmatched)
            patch_types[self.default_patch] = "patch"

        mesh.metadata["spatial_query_patch_types"] = patch_types
        return out_boundaries


def check_1_to_n_patch_remapping(
    source_patches: Sequence[str],
    target_patches: Sequence[str],
) -> Dict[str, str]:
    """Validate 1:1 injectivity between CAD surface patches and solver patches."""
    if len(source_patches) != len(target_patches):
        raise SpatialQueryConflictError(
            f"1->N remapping count mismatch: {len(source_patches)} sources vs {len(target_patches)} targets"
        )
    mapping: Dict[str, str] = {}
    used_targets: set[str] = set()
    for src, dst in zip(source_patches, target_patches):
        if src in mapping and mapping[src] != dst:
            raise SpatialQueryConflictError(
                f"Ambiguous 1->N patch split for '{src}': '{mapping[src]}' vs '{dst}'"
            )
        if dst in used_targets and mapping.get(src) != dst:
            raise SpatialQueryConflictError(
                f"Many-to-one patch collision onto '{dst}' from '{src}'"
            )
        mapping[src] = dst
        used_targets.add(dst)
    return mapping


__all__ = [
    "SpatialQueryConflictError",
    "SpatialQueryRule",
    "SpatialQuerySelector",
    "check_1_to_n_patch_remapping",
]
