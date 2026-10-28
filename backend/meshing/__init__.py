"""In-Memory Multiphysics Mesher Backbone Package."""

from backend.meshing.models import (
    UnstructuredMesh,
    SPHParticleSet,
    MeshQualityReport,
    SizingParameters,
    InflationConfig,
    CurvatureSizingConfig,
    QualityConfig,
    MeshingConfig,
)
from backend.meshing.geometry_sdf import (
    BaseSDF,
    SDFNode,
    BoxSDF,
    SphereSDF,
    CylinderSDF,
    PolygonSDF,
    NACA4AirfoilSDF,
    CSGUnion,
    CSGIntersection,
    CSGDifference,
    CSGComplement,
    SDFEngine,
    smooth_min,
    smooth_max,
    CADMeshSDF,
    CurvatureMethod,
    CurvatureRefinement,
    ProximityRefinement,
    AdaptiveSizeFunction,
)
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    get_native_dll,
)
from backend.meshing.distmesh_core import (
    distmesh_2d,
    rcm_reorder,
    compute_bandwidth,
)
from backend.meshing.inflation import (
    BoundaryLayerGenerator,
)
from backend.meshing.mesh_engine import (
    MeshEngine,
)
from backend.meshing.coupling import (
    SPHKernel,
    SPHFVMCoupler,
    OPMISTaylorInterpolator,
)
from backend.meshing.moving_mesh import (
    ALEMeshMover,
)

__all__ = [
    "UnstructuredMesh",
    "SPHParticleSet",
    "MeshQualityReport",
    "SizingParameters",
    "InflationConfig",
    "CurvatureSizingConfig",
    "QualityConfig",
    "MeshingConfig",
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
    "smooth_min",
    "smooth_max",
    "CADMeshSDF",
    "CurvatureMethod",
    "CurvatureRefinement",
    "ProximityRefinement",
    "AdaptiveSizeFunction",
    "compute_difvm_topology",
    "compute_difvm_topology_native",
    "compute_difvm_topology_python",
    "get_native_dll",
    "distmesh_2d",
    "rcm_reorder",
    "compute_bandwidth",
    "BoundaryLayerGenerator",
    "MeshEngine",
    "SPHKernel",
    "SPHFVMCoupler",
    "OPMISTaylorInterpolator",
    "ALEMeshMover",
]
