"""CFDSolver JSON-RPC 2.0 and NDJSON IPC contracts package."""

from .dimensions import (
    SI_BASE_UNITS,
    STANDARD_FIELD_DIMENSIONS,
    validate_si_dimensions,
    get_field_dimensions,
    format_openfoam_dimensions,
    parse_openfoam_dimensions,
)
from .binary_buffers import (
    GeometryBufferDescriptor,
    MeshQualityBufferDescriptor,
    pack_geometry_buffer,
    unpack_geometry_buffer,
    pack_mesh_quality_buffer,
    unpack_mesh_quality_buffer,
)
from .telemetry import (
    IterationEvent,
    TimestepWrittenEvent,
    TelemetryStreamer,
)
from .async_jobs import (
    JobStatus,
    JobRecord,
    AsyncJobManager,
)
from .protocol import (
    PARSE_ERROR,
    INVALID_REQUEST,
    METHOD_NOT_FOUND,
    INVALID_PARAMS,
    INTERNAL_ERROR,
    make_success_response,
    make_error_response,
    parse_jsonrpc_request,
    serialize_ndjson,
)
from .service import IPCService

__all__ = [
    "IPCService",
    "SI_BASE_UNITS",
    "STANDARD_FIELD_DIMENSIONS",
    "validate_si_dimensions",
    "get_field_dimensions",
    "format_openfoam_dimensions",
    "parse_openfoam_dimensions",
    "GeometryBufferDescriptor",
    "MeshQualityBufferDescriptor",
    "pack_geometry_buffer",
    "unpack_geometry_buffer",
    "pack_mesh_quality_buffer",
    "unpack_mesh_quality_buffer",
    "IterationEvent",
    "TimestepWrittenEvent",
    "TelemetryStreamer",
    "JobStatus",
    "JobRecord",
    "AsyncJobManager",
    "make_success_response",
    "make_error_response",
    "parse_jsonrpc_request",
    "serialize_ndjson",
]
