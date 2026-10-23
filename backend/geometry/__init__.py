"""CFDSolver CAD & Geometry Ingestion Subsystem."""

from backend.geometry.models import SurfaceMesh, AuditReport, BoundingBox
from backend.geometry.pipeline import CADGeometryEngine
from backend.geometry.parsers.base_parser import BaseParser
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.bridge.cad_bridge import CADBridge
from backend.geometry.bridge.nx_translator import (
    NXTranslator,
    NativeCADBridgeUnavailableError,
    NXLicenseError,
    NXTranslationError,
)
from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.audit.quality_metrics import QualityMetrics
from backend.geometry.transformation.scaler import GeometryScaler
from backend.geometry.exporter.openfoam_exporter import OpenFOAMExporter

__all__ = [
    "SurfaceMesh",
    "AuditReport",
    "BoundingBox",
    "CADGeometryEngine",
    "BaseParser",
    "STLParser",
    "OBJParser",
    "STEPParser",
    "CADBridge",
    "NXTranslator",
    "NativeCADBridgeUnavailableError",
    "NXLicenseError",
    "NXTranslationError",
    "SurfaceAuditor",
    "QualityMetrics",
    "GeometryScaler",
    "OpenFOAMExporter",
]
