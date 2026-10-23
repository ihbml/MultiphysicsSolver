"""Unified CAD & Geometry Engine facade for CFDSolver-Backend."""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Dict, Optional, Tuple, Union

from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.bridge.cad_bridge import CADBridge
from backend.geometry.exporter.openfoam_exporter import OpenFOAMExporter
from backend.geometry.models import AuditReport, BoundingBox, SurfaceMesh
from backend.geometry.parsers.base_parser import BaseParser
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.transformation.scaler import GeometryScaler

logger = logging.getLogger(__name__)


class CADGeometryEngine:
    """Unified facade orchestrating CAD ingestion, translation, auditing, scaling, and export."""

    def __init__(
        self,
        auto_repair_normals: bool = True,
        default_scale_factor: float = 0.001,
        nx_base_dir: Optional[Union[str, Path]] = None,
    ) -> None:
        """Initialize CADGeometryEngine.

        Args:
            auto_repair_normals: Whether to auto-invert face windings if signed volume < 0.
            default_scale_factor: Standard CAD mm -> CFD m scale factor (0.001).
            nx_base_dir: Optional custom path to Siemens NX installation directory.
        """
        self.auto_repair_normals = auto_repair_normals
        self.default_scale_factor = default_scale_factor

        self.cad_bridge = CADBridge(nx_base_dir=nx_base_dir)
        self.auditor = SurfaceAuditor(auto_repair_normals=auto_repair_normals)
        self.scaler = GeometryScaler(default_scale_factor=default_scale_factor)

        self.current_mesh: Optional[SurfaceMesh] = None
        self.audit_report: Optional[AuditReport] = None
        self.source_file: Optional[Path] = None

    def load(
        self,
        file_path: Union[str, Path],
        step_engine: str = "auto",
        deflection: float = 0.0001,
        angular_tolerance: float = 0.1745,
        min_edge: float = 0.00005,
        max_edge: float = 0.05,
        weld_tolerance: float = 1e-7,
        allow_cad_fallback: bool = True,
    ) -> CADGeometryEngine:
        """Load and parse any supported CAD geometry format (.stp, .stl, .obj, .prt).

        Args:
            file_path: Path to input CAD file.
            step_engine: 'auto', 'gmsh', or 'brep' for STEP files.
            deflection: Linear chordal error for B-Rep tessellation.
            angular_tolerance: Angular resolution for curved surface facets.
            min_edge: Minimum allowable element edge length.
            max_edge: Maximum allowable element edge length.
            weld_tolerance: Spatial welding distance for unindexed mesh vertices.
            allow_cad_fallback: If True, falls back to open standards if native CAD translation fails.

        Returns:
            self for fluent chaining.
        """
        path = Path(file_path).resolve()
        if not path.exists():
            raise FileNotFoundError(f"CAD geometry file not found: {path}")

        self.source_file = path
        ext = path.suffix.lower()

        # Step 1: If native format (.prt), route through CAD bridge
        if ext in self.cad_bridge.NATIVE_FORMATS:
            logger.info("Routing native CAD format %s through CADBridge...", ext)
            translated_path = self.cad_bridge.translate(
                input_file=path,
                output_format="step",
                allow_fallback=allow_cad_fallback,
            )
            path = translated_path
            ext = path.suffix.lower()

        # Step 2: Route through format parser
        if ext in (".stl",):
            parser: BaseParser = STLParser(weld_tolerance=weld_tolerance)
            self.current_mesh = parser.parse(path)
        elif ext in (".obj",):
            parser = OBJParser()
            self.current_mesh = parser.parse(path)
        elif ext in (".stp", ".step"):
            parser = STEPParser(
                deflection=deflection,
                angular_tolerance=angular_tolerance,
                min_edge=min_edge,
                max_edge=max_edge,
                engine=step_engine,
            )
            self.current_mesh = parser.parse(path)
        else:
            raise ValueError(f"Unsupported geometry file extension '{ext}' for file {path.name}")

        self.audit_report = None
        return self

    def audit(self) -> AuditReport:
        """Execute comprehensive geometry and topology audit on current mesh.

        Returns:
            AuditReport detailing watertightness, Euler characteristic, normal consistency, etc.
        """
        if self.current_mesh is None:
            raise RuntimeError("No geometry mesh loaded. Call .load(path) first.")

        self.audit_report = self.auditor.audit(self.current_mesh)
        return self.audit_report

    def scale(
        self,
        scale_factor: Optional[float] = None,
        verify_invariants: bool = True,
    ) -> CADGeometryEngine:
        """Transform mesh coordinates to SI meters and verify analytical invariants.

        Args:
            scale_factor: Custom scale factor (default: self.default_scale_factor = 0.001).
            verify_invariants: Whether to assert bounding box and volume invariants.

        Returns:
            self for fluent chaining.
        """
        if self.current_mesh is None:
            raise RuntimeError("No geometry mesh loaded. Call .load(path) first.")

        factor = scale_factor if scale_factor is not None else self.default_scale_factor
        self.current_mesh = self.scaler.scale(
            mesh=self.current_mesh,
            scale_factor=factor,
            verify_invariants=verify_invariants,
            in_place=True,
        )
        # Invalidate previous audit report after scaling
        self.audit_report = None
        return self

    def export_openfoam(
        self,
        case_dir: Union[str, Path],
        filename: Optional[str] = None,
        export_format: str = "stl",
        binary: bool = False,
    ) -> Path:
        """Export current surface mesh to <case_dir>/constant/triSurface/<filename>.

        Returns:
            Path to exported surface file.
        """
        if self.current_mesh is None:
            raise RuntimeError("No geometry mesh loaded. Call .load(path) first.")

        if filename is None and self.source_file is not None:
            filename = f"{self.source_file.stem}.{export_format.lower()}"

        return OpenFOAMExporter.export(
            mesh=self.current_mesh,
            case_dir=case_dir,
            filename=filename,
            export_format=export_format,
            binary=binary,
        )

    def generate_snappyhexmesh_dict(
        self,
        filename: Optional[str] = None,
        geometry_name: Optional[str] = None,
    ) -> str:
        """Generate snappyHexMeshDict geometry dictionary block with patch definitions."""
        if self.current_mesh is None:
            raise RuntimeError("No geometry mesh loaded. Call .load(path) first.")

        if filename is None:
            filename = f"{self.source_file.stem}.stl" if self.source_file else "geometry.stl"

        return OpenFOAMExporter.generate_snappyhexmesh_dict_block(
            mesh=self.current_mesh,
            filename=filename,
            geometry_name=geometry_name,
        )

    def ingest_and_prepare(
        self,
        input_file: Union[str, Path],
        case_dir: Union[str, Path],
        output_filename: Optional[str] = None,
        output_format: str = "stl",
        scale_factor: float = 0.001,
        auto_repair_normals: bool = True,
        export_binary: bool = False,
        step_engine: str = "auto",
        deflection: float = 0.0001,
        weld_tolerance: float = 1e-7,
        allow_cad_fallback: bool = True,
    ) -> Tuple[Path, AuditReport]:
        """Complete end-to-end pipeline: load -> scale -> audit -> export to OpenFOAM case.

        Args:
            input_file: Source CAD file (.stp, .stl, .obj, .prt).
            case_dir: OpenFOAM case root directory.
            output_filename: Optional name for exported surface in constant/triSurface/.
            output_format: 'stl' or 'obj'.
            scale_factor: Unit conversion factor (e.g. 0.001 for mm -> m).
            auto_repair_normals: Auto-invert winding if signed volume < 0.
            export_binary: If True and format='stl', write binary STL.
            step_engine: 'auto', 'gmsh', or 'brep'.
            deflection: Linear chordal error for B-Rep tessellation.
            weld_tolerance: Tolerance for vertex welding.
            allow_cad_fallback: Graceful fallback to open standards on native CAD failure.

        Returns:
            Tuple of (Path to exported surface file, AuditReport of scaled surface).
        """
        # 1. Load CAD geometry
        self.load(
            file_path=input_file,
            step_engine=step_engine,
            deflection=deflection,
            weld_tolerance=weld_tolerance,
            allow_cad_fallback=allow_cad_fallback,
        )

        # 2. Scale from CAD units (mm) to CFD SI units (meters)
        self.scale(scale_factor=scale_factor, verify_invariants=True)

        # 3. Comprehensive audit of scaled mesh
        report = self.audit()

        # 4. Export to OpenFOAM constant/triSurface/
        exported_path = self.export_openfoam(
            case_dir=case_dir,
            filename=output_filename,
            export_format=output_format,
            binary=export_binary,
        )

        return exported_path, report
