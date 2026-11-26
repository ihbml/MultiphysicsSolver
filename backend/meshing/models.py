"""Data models for CFDSolver In-Memory Multiphysics Mesher Backbone.

Facade module re-exporting configuration, mesh, and quality metric types from:
- _models_config.py
- _models_quality.py
- _models_mesh.py
"""

from __future__ import annotations

from backend.meshing._models_config import (
    CellType,
    CurvatureSizingConfig,
    FarfieldPadding,
    InflationConfig,
    MeshingConfig,
    QualityConfig,
    SizingParameters,
)
from backend.meshing._models_mesh import (
    CSRSequence,
    SPHParticleSet,
    UnstructuredMesh,
)
from backend.meshing._models_quality import (
    MeshQualityReport,
    _extract_polyhedron_faces_py,
    _triangulate_face_3d,
    check_pyramid_base_convexity,
    check_pyramid_base_fictitious_hex,
    compute_cell_j_ens,
    compute_element_normalized_scaled_jacobian,
    compute_mesh_j_ens,
    compute_nodal_scaled_jacobian,
    compute_prism_jacobian_bound,
)

__all__ = [
    "FarfieldPadding",
    "CellType",
    "InflationConfig",
    "CurvatureSizingConfig",
    "QualityConfig",
    "MeshingConfig",
    "SizingParameters",
    "MeshQualityReport",
    "CSRSequence",
    "UnstructuredMesh",
    "SPHParticleSet",
    "compute_nodal_scaled_jacobian",
    "compute_element_normalized_scaled_jacobian",
    "check_pyramid_base_fictitious_hex",
    "compute_prism_jacobian_bound",
    "check_pyramid_base_convexity",
    "compute_cell_j_ens",
    "compute_mesh_j_ens",
    "_extract_polyhedron_faces_py",
    "_triangulate_face_3d",
]
