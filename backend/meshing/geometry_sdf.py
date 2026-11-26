"""Continuous Signed Distance Field (SDF) Engine, Analytical Primitives & CAD Feature Filtering.

Facade module re-exporting symbols from:
- _sdf_primitives.py
- _sdf_bvh.py
- _sdf_cad.py
- _sdf_sizing.py
"""

from __future__ import annotations

from backend.meshing._sdf_bvh import (
    TriangleAABBTree,
    _AABBNode,
    _get_sdf_dll,
    _point_to_triangle_distance_exact,
    compute_angle_weighted_pseudonormals,
    compute_mesh_winding_number,
    voromesh_bisector_loss,
)
from backend.meshing._sdf_cad import CADFeatureFilter, CADMeshSDF
from backend.meshing._sdf_primitives import (
    BaseSDF,
    BoxSDF,
    CSGComplement,
    CSGDifference,
    CSGIntersection,
    CSGUnion,
    CylinderSDF,
    NACA4AirfoilSDF,
    PolygonSDF,
    SDFEngine,
    SDFNode,
    SphereSDF,
    smooth_max,
    smooth_min,
)
from backend.meshing._sdf_sizing import (
    AdaptiveSizeFunction,
    CurvatureMethod,
    CurvatureRefinement,
    ProximityRefinement,
)
from backend.meshing.feature_graph import FeatureGraph, extract_feature_graph
from backend.meshing.native_loader import get_native_dll
from backend.meshing.weingarten import (
    WallCurvatureField,
    compute_ccbc_corrections,
    compute_weingarten_curvature,
)

__all__ = [
    "smooth_min",
    "smooth_max",
    "BaseSDF",
    "SDFNode",
    "BoxSDF",
    "SphereSDF",
    "CylinderSDF",
    "PolygonSDF",
    "NACA4AirfoilSDF",
    "CSGUnion",
    "CSGIntersection",
    "CSGDifference",
    "CSGComplement",
    "SDFEngine",
    "_get_sdf_dll",
    "_point_to_triangle_distance_exact",
    "compute_angle_weighted_pseudonormals",
    "_AABBNode",
    "TriangleAABBTree",
    "compute_mesh_winding_number",
    "voromesh_bisector_loss",
    "CADMeshSDF",
    "CADFeatureFilter",
    "CurvatureMethod",
    "CurvatureRefinement",
    "ProximityRefinement",
    "AdaptiveSizeFunction",
    "FeatureGraph",
    "extract_feature_graph",
    "WallCurvatureField",
    "compute_weingarten_curvature",
    "compute_ccbc_corrections",
    "get_native_dll",
]
