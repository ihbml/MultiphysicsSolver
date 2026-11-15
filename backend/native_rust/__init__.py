"""Native Rust modules and high-speed I/O interfaces for CFDSolver."""

from .foam_reader import FoamReader, FoamMesh, FoamBoundaryPatch, TimeStepInfo, list_time_dirs, write_field
from .c_abi_lake import (
    CAbiSpan,
    CAbiDataType,
    CAbiDeviceId,
    allocate_aligned_array,
    create_cabi_span,
    export_mesh_to_soa_lake,
    export_fields_to_soa_lake,
)
from .expression_engine import ExpressionEngine
from .mphys_container import MPhysWriter, MPhysReader, CompressionType

__all__ = [
    "FoamReader",
    "FoamMesh",
    "FoamBoundaryPatch",
    "TimeStepInfo",
    "list_time_dirs",
    "write_field",
    "CAbiSpan",
    "CAbiDataType",
    "CAbiDeviceId",
    "allocate_aligned_array",
    "create_cabi_span",
    "export_mesh_to_soa_lake",
    "export_fields_to_soa_lake",
    "ExpressionEngine",
    "MPhysWriter",
    "MPhysReader",
    "CompressionType",
]
