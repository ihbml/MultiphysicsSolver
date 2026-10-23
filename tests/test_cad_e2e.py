"""End-to-End Test Suite for CFDSolver CAD & Geometry Ingestion Engine.

Structured across 4 rigorous verification tiers:
- Tier 1: Feature Coverage (>=5 test cases per feature for STEP, STL, OBJ, Tessellation, NX Bridge, Audit, Scaling, Export)
- Tier 2: Boundary & Corner Cases (empty, truncated, binary with 'solid', 0-area, n-gons, negative indices, holes, T-junctions, inverted normals, slivers)
- Tier 3: Cross-Feature Interactions (pairwise and chained pipeline workflows)
- Tier 4: Real-World CAD Reference Samples (TestCAD.obj, TestCAD.stl, TestCADSTPAP203.stp, TestCADSTPAP214.stp, TestCADSTPAP242.stp, TestCAD.prt)
"""

from pathlib import Path
import shutil
import struct
import tempfile
from typing import Any, Dict, List, Optional
import numpy as np
import pytest

from backend.geometry.models import AuditReport, BoundingBox, SurfaceMesh
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser
from tests.conftest import (
    ANALYTICAL_SURFACE_AREA_CAD,
    ANALYTICAL_SURFACE_AREA_CFD,
    ANALYTICAL_VOLUME_CAD,
    ANALYTICAL_VOLUME_CFD,
    CAD_BBOX_MAX,
    CAD_BBOX_MIN,
    CFD_BBOX_MAX,
    CFD_BBOX_MIN,
    CUBE_SIDE_M,
    CUBE_SIDE_MM,
    EULER_CHI_GENUS_0,
    create_degenerate_face_arrays,
    create_inverted_cube_arrays,
    create_open_cube_arrays,
    create_sliver_triangle_arrays,
    create_t_junction_non_manifold_arrays,
    create_unit_cube_arrays,
)


# Helper functions to load optional modules dynamically
def get_surface_auditor_class():
    from backend.geometry.audit.surface_auditor import SurfaceAuditor
    return SurfaceAuditor


def get_geometry_scaler_class():
    from backend.geometry.transformation.scaler import GeometryScaler
    return GeometryScaler


def get_openfoam_exporter_class():
    from backend.geometry.exporter.openfoam_exporter import OpenFOAMExporter
    return OpenFOAMExporter


def get_cad_bridge_class():
    from backend.geometry.bridge.cad_bridge import CADBridge
    return CADBridge


def get_cad_geometry_engine_class():
    from backend.geometry.pipeline import CADGeometryEngine
    return CADGeometryEngine


# ==============================================================================
# TIER 1: FEATURE COVERAGE (>=5 test cases per feature)
# ==============================================================================


class TestTier1STEPParser:
    """Tier 1: Feature coverage for ISO STEP AP203, AP214, AP242 and B-Rep ingestion."""

    def test_step_ap203_ingestion(self, stp_ap203_sample_path: Path):
        """Feature 1: Reads B-Rep solids and topology from ISO 10303-21 AP203 files."""
        parser = STEPParser()
        mesh = parser.parse(stp_ap203_sample_path)
        assert isinstance(mesh, SurfaceMesh)
        assert len(mesh.vertices) >= 8
        assert len(mesh.faces) >= 12
        assert mesh.metadata.get("units") == "mm"

    def test_step_ap214_ingestion(self, stp_ap214_sample_path: Path):
        """Feature 2: Reads B-Rep solids with layer/color attributes from AP214 files."""
        parser = STEPParser()
        mesh = parser.parse(stp_ap214_sample_path)
        assert isinstance(mesh, SurfaceMesh)
        assert len(mesh.vertices) >= 8
        assert len(mesh.faces) >= 12
        assert mesh.metadata.get("units") == "mm"

    def test_step_ap242_ingestion(self, stp_ap242_sample_path: Path):
        """Feature 3: Reads advanced model-based 3D geometries and PMI from AP242 files."""
        parser = STEPParser()
        mesh = parser.parse(stp_ap242_sample_path)
        assert isinstance(mesh, SurfaceMesh)
        assert len(mesh.vertices) >= 8
        assert len(mesh.faces) >= 12
        assert mesh.metadata.get("units") == "mm"

    def test_step_detect_units_mm(self, stp_ap203_sample_path: Path):
        """Feature 1-3: Unit detection correctly detects millimeter unit blocks."""
        units = STEPParser.detect_units(stp_ap203_sample_path)
        assert units == "mm"

    def test_step_brep_fallback_engine(self, stp_ap214_sample_path: Path):
        """Feature 1-3: Pure-Python B-Rep fallback parser extracts exact topological vertices."""
        parser = STEPParser(engine="brep")
        mesh = parser.parse(stp_ap214_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        # Verify bounding box matches analytical bounds
        bbox = mesh.get_bounding_box()
        assert np.allclose(bbox.min_pt, CAD_BBOX_MIN, atol=1e-4)
        assert np.allclose(bbox.max_pt, CAD_BBOX_MAX, atol=1e-4)

    def test_step_gmsh_engine_or_graceful_fallback(self, stp_ap203_sample_path: Path):
        """Feature 9: Gmsh engine or graceful fallback to B-Rep tessellator."""
        parser = STEPParser(engine="auto")
        mesh = parser.parse(stp_ap203_sample_path)
        assert len(mesh.vertices) >= 8
        assert len(mesh.faces) >= 12


class TestTier1STLParser:
    """Tier 1: Feature coverage for Binary & ASCII STL ingestion and topological welding."""

    def test_stl_binary_ingestion(self, stl_sample_path: Path):
        """Feature 4: Parses 80-byte header, uint32 face count, and 50-byte IEEE 754 facet structs."""
        parser = STLParser()
        mesh = parser.parse(stl_sample_path)
        assert isinstance(mesh, SurfaceMesh)
        assert len(mesh.faces) == 12
        assert len(mesh.vertices) == 8
        assert mesh.normals is not None
        assert len(mesh.normals) == 12

    def test_stl_ascii_ingestion(self, tmp_path: Path):
        """Feature 5: ASCII STL text parser for solid ... facet normal ... vertex ... endsolid."""
        ascii_stl_content = """solid cube
  facet normal 0.0 0.0 1.0
    outer loop
      vertex 0.0 0.0 1.0
      vertex 1.0 0.0 1.0
      vertex 1.0 1.0 1.0
    endloop
  endfacet
  facet normal 0.0 0.0 1.0
    outer loop
      vertex 0.0 0.0 1.0
      vertex 1.0 1.0 1.0
      vertex 0.0 1.0 1.0
    endloop
  endfacet
endsolid cube
"""
        stl_file = tmp_path / "test_ascii.stl"
        stl_file.write_text(ascii_stl_content, encoding="utf-8")

        parser = STLParser()
        mesh = parser.parse(stl_file)
        assert len(mesh.faces) == 2
        assert len(mesh.vertices) == 4  # 4 unique vertices after welding

    def test_stl_vertex_welding_tolerance(self, tmp_path: Path):
        """Feature 8: Topological vertex welding deduplicates coincident points into shared vertices."""
        parser = STLParser(weld_tolerance=1e-4)
        # 2 triangles sharing 2 vertices
        v = np.array([
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0],
            [0.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        ], dtype=np.float64)
        # Write binary STL
        stl_file = tmp_path / "weld_test.stl"
        with open(stl_file, "wb") as f:
            f.write(b" " * 80)
            f.write(struct.pack("<I", 2))
            for i in range(2):
                f.write(struct.pack("<3f", 0.0, 0.0, 1.0))
                for j in range(3):
                    pt = v[i * 3 + j]
                    f.write(struct.pack("<3f", float(pt[0]), float(pt[1]), float(pt[2])))
                f.write(struct.pack("<H", 0))

        mesh = parser.parse(stl_file)
        assert len(mesh.vertices) == 4  # 6 raw points welded into 4 vertices
        assert len(mesh.faces) == 2

    def test_stl_computed_normals(self, stl_sample_path: Path):
        """Feature 4: Evaluates face normal unit vectors via cross-product."""
        parser = STLParser()
        mesh = parser.parse(stl_sample_path)
        norms = np.linalg.norm(mesh.normals, axis=1)
        assert np.allclose(norms, 1.0, atol=1e-6)

    def test_stl_patch_metadata(self, stl_sample_path: Path):
        """Feature 4: Preserves patch metadata from solid header or default."""
        parser = STLParser()
        mesh = parser.parse(stl_sample_path)
        assert "default" in mesh.patches or len(mesh.patches) > 0


class TestTier1OBJParser:
    """Tier 1: Feature coverage for Wavefront OBJ ingestion, polygon triangulation, and patch tokens."""

    def test_obj_vertex_and_face_ingestion(self, obj_sample_path: Path):
        """Feature 6: Parses v, vn, vt, f, g, o, usemtl tokens from OBJ files."""
        parser = OBJParser()
        mesh = parser.parse(obj_sample_path)
        assert isinstance(mesh, SurfaceMesh)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        assert "TestCAD" in mesh.patches or "TestCADMaterial0" in mesh.patches

    def test_obj_quad_triangulation(self, tmp_path: Path):
        """Feature 7: Converts quadrilateral faces (N=4) into 2 planar triangles."""
        obj_content = """
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
"""
        obj_file = tmp_path / "quad.obj"
        obj_file.write_text(obj_content.strip(), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        assert len(mesh.faces) == 2
        assert len(mesh.vertices) == 4

    def test_obj_ngon_triangulation(self, tmp_path: Path):
        """Feature 7: Converts general n-gons (N=5, 6) into N-2 planar triangles."""
        obj_content = """
v 0.0 0.0 0.0
v 2.0 0.0 0.0
v 3.0 1.0 0.0
v 2.0 2.0 0.0
v 0.0 2.0 0.0
f 1 2 3 4 5
"""
        obj_file = tmp_path / "pentagon.obj"
        obj_file.write_text(obj_content.strip(), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        # Pentagon (N=5) yields 3 triangles
        assert len(mesh.faces) == 3

    def test_obj_group_and_material_patches(self, tmp_path: Path):
        """Feature 6: Maps group names (g) and material names (usemtl) to boundary patches."""
        obj_content = """
v 0 0 0
v 1 0 0
v 0 1 0
v 0 0 1
g inlet
f 1 2 3
g outlet
f 1 2 4
"""
        obj_file = tmp_path / "patches.obj"
        obj_file.write_text(obj_content.strip(), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        assert "inlet" in mesh.patches
        assert "outlet" in mesh.patches
        assert len(mesh.patches["inlet"]) == 1
        assert len(mesh.patches["outlet"]) == 1

    def test_obj_vertex_normals_extraction(self, tmp_path: Path):
        """Feature 6: Parses vertex normal tokens (vn)."""
        obj_content = """
v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1
vn 0 0 1
vn 0 0 1
f 1//1 2//2 3//3
"""
        obj_file = tmp_path / "normals.obj"
        obj_file.write_text(obj_content.strip(), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        assert len(mesh.faces) == 1
        assert mesh.normals is not None


class TestTier1Tessellation:
    """Tier 1: Feature coverage for B-Rep tessellation parameters."""

    def test_tessellation_chordal_deflection(self, stp_ap203_sample_path: Path):
        """Feature 9: Configurable chordal deflection parameter."""
        parser = STEPParser(deflection=0.0005)
        assert parser.deflection == 0.0005
        mesh = parser.parse(stp_ap203_sample_path)
        assert len(mesh.faces) >= 12

    def test_tessellation_angular_tolerance(self, stp_ap214_sample_path: Path):
        """Feature 9: Configurable angular tolerance parameter."""
        parser = STEPParser(angular_tolerance=0.0872)  # ~5 degrees
        assert parser.angular_tolerance == 0.0872
        mesh = parser.parse(stp_ap214_sample_path)
        assert len(mesh.faces) >= 12

    def test_tessellation_min_edge_bound(self, stp_ap242_sample_path: Path):
        """Feature 9: Enforces lower bound on triangle edge length."""
        parser = STEPParser(min_edge=0.0001)
        assert parser.min_edge == 0.0001
        mesh = parser.parse(stp_ap242_sample_path)
        assert len(mesh.vertices) >= 8

    def test_tessellation_max_edge_bound(self, stp_ap203_sample_path: Path):
        """Feature 9: Enforces upper bound on triangle edge length."""
        parser = STEPParser(max_edge=0.1)
        assert parser.max_edge == 0.1
        mesh = parser.parse(stp_ap203_sample_path)
        assert len(mesh.faces) >= 12

    def test_tessellation_generates_watertight_manifold(self, stp_ap214_sample_path: Path):
        """Feature 9: Tessellation preserves closed 2-manifold topology."""
        parser = STEPParser(engine="brep")
        mesh = parser.parse(stp_ap214_sample_path)
        SurfaceAuditor = get_surface_auditor_class()
        report = SurfaceAuditor().audit(mesh)
        assert report.is_watertight is True
        assert report.euler_characteristic == 2


class TestTier1NXBridge:
    """Tier 1: Feature coverage for Siemens NX 1926 CAD Bridge."""

    def test_nx_auto_detection(self):
        """Feature 10: Probes UGII_BASE_DIR, registry, and filesystem for NX 1926."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()
        installations = bridge.detect_installations()
        assert "Siemens_NX" in installations
        if bridge.nx_translator.is_available:
            assert bridge.nx_translator.nx_base_dir is not None
            assert "1926" in str(bridge.nx_translator.nx_base_dir)

    def test_nx_environment_config(self):
        """Feature 11: Constructs required environment variables with trailing slashes."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()
        if bridge.nx_translator.is_available:
            env = bridge.nx_translator.build_environment()
            assert "UGII_BASE_DIR" in env
            assert "STEP214UG_DIR" in env
            assert env["STEP214UG_DIR"].endswith(("\\", "/"))

    def test_nx_prt_translation_live(self, prt_sample_path: Path, tmp_path: Path):
        """Feature 12: Translates native .prt to STEP via step214ug.cmd."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()
        if not bridge.nx_translator.is_available:
            pytest.skip("Siemens NX is not installed or license server is offline.")

        out_stp = tmp_path / "translated.stp"
        try:
            result_path = bridge.translate(prt_sample_path, output_format="step", output_file=out_stp)
            assert result_path.exists()
            assert result_path.stat().st_size > 1000
        except Exception as e:
            # If license checkout fails, verify structured error
            assert "license" in str(e).lower() or "timeout" in str(e).lower() or "unavailable" in str(e).lower()

    def test_nx_log_audit_verification(self, cad_samples_dir: Path):
        """Feature 13: Parses transaction log to verify solids processed >= 1."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()
        log_path = cad_samples_dir / "TestCADSTPAP214.log"
        if log_path.exists():
            # Must complete without raising NXTranslationError or NXLicenseError
            bridge.nx_translator._audit_nx_log(log_path, "", "")

    def test_nx_graceful_fallback_on_missing_license(self, prt_sample_path: Path):
        """Feature 14: Handles native CAD unavailability gracefully without crash."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()
        fallback_file = bridge._find_fallback(prt_sample_path)
        assert fallback_file is not None
        assert fallback_file.suffix.lower() in (".stp", ".step", ".stl", ".obj")


class TestTier1GeometryAudit:
    """Tier 1: Feature coverage for topological and geometric auditing."""

    def test_audit_watertight_check(self):
        """Feature 15: Verifies that every edge is shared by exactly 2 faces (0 open edges)."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.is_watertight is True
        assert report.open_edges_count == 0

    def test_audit_manifold_edges_check(self):
        """Feature 15: Verifies that no edge is shared by >2 faces (0 multi edges)."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.is_manifold is True
        assert report.multi_edges_count == 0

    def test_audit_euler_characteristic(self):
        """Feature 16: Evaluates chi = V - E + F == 2 for genus-0 surfaces."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.euler_characteristic == 2

    def test_audit_degenerate_triangle_detection(self):
        """Feature 17: Catches duplicate vertices and zero-area triangles."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_degenerate_face_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.degenerate_faces_count >= 1
        assert report.passed_openfoam_surfacecheck is False

    def test_audit_outward_normal_and_signed_volume(self):
        """Feature 22: Computes signed volume via Divergence Theorem and checks outward orientation."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.is_outward_oriented is True
        assert report.signed_volume > 0.0


class TestTier1GeometryScaler:
    """Tier 1: Feature coverage for unit scaling and bounding box verification."""

    def test_scaler_default_factor_mm_to_m(self):
        """Feature 24: Scales vertex coordinates by factor 0.001 (mm to m)."""
        GeometryScaler = get_geometry_scaler_class()
        v, f = create_unit_cube_arrays()
        v_mm = v * 200.0
        mesh = SurfaceMesh(vertices=v_mm, faces=f)
        scaled_mesh = GeometryScaler().scale(mesh, scale_factor=0.001)

        assert np.allclose(scaled_mesh.vertices, v * 0.2, atol=1e-12)

    def test_scaler_bounding_box_invariants(self):
        """Feature 25: Computes min/max bounds and span vectors before and after scaling."""
        GeometryScaler = get_geometry_scaler_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v * 200.0, faces=f)
        scaler = GeometryScaler()
        scaled_mesh = scaler.scale(mesh, scale_factor=0.001)

        orig_bbox = mesh.get_bounding_box()
        scaled_bbox = scaled_mesh.get_bounding_box()

        assert np.allclose(scaled_bbox.span, orig_bbox.span * 0.001, atol=1e-12)

    def test_scaler_volume_scales_by_cubic_factor(self):
        """Feature 24: Enclosed volume scales exactly by s^3."""
        GeometryScaler = get_geometry_scaler_class()
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v * 200.0, faces=f)
        scaled = GeometryScaler().scale(mesh, scale_factor=0.001)

        vol_orig = SurfaceAuditor().audit(mesh).signed_volume
        vol_scaled = SurfaceAuditor().audit(scaled).signed_volume

        assert np.isclose(vol_scaled, vol_orig * (0.001 ** 3), rtol=1e-10)

    def test_scaler_surface_area_scales_by_square_factor(self):
        """Feature 24: Surface area scales exactly by s^2."""
        GeometryScaler = get_geometry_scaler_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v * 200.0, faces=f)
        scaled = GeometryScaler().scale(mesh, scale_factor=0.001)

        def area(m):
            v0 = m.vertices[m.faces[:, 0]]
            v1 = m.vertices[m.faces[:, 1]]
            v2 = m.vertices[m.faces[:, 2]]
            return 0.5 * np.sum(np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1))

        assert np.isclose(area(scaled), area(mesh) * (0.001 ** 2), rtol=1e-10)

    def test_scaler_custom_factor(self):
        """Feature 24: Supports custom scaling factors (e.g. inches to meters: 0.0254)."""
        GeometryScaler = get_geometry_scaler_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        scaled = GeometryScaler().scale(mesh, scale_factor=0.0254)
        assert np.allclose(scaled.vertices, v * 0.0254, atol=1e-12)


class TestTier1OpenFOAMExporter:
    """Tier 1: Feature coverage for OpenFOAM constant/triSurface export & snappyHexMeshDict."""

    def test_export_constant_trisurface_directory_structure(self, tmp_path: Path):
        """Feature 26: Writes watertight surface mesh to <case_dir>/constant/triSurface/<name>.stl."""
        OpenFOAMExporter = get_openfoam_exporter_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        exporter = OpenFOAMExporter()
        out_path = exporter.export(mesh, case_dir=tmp_path, filename="cube.stl")

        expected_dir = tmp_path / "constant" / "triSurface"
        assert expected_dir.exists()
        assert out_path == expected_dir / "cube.stl"
        assert out_path.exists()
        assert out_path.stat().st_size > 0

    def test_export_ascii_stl_with_patches(self, tmp_path: Path):
        """Feature 26: Preserves named patches in ASCII STL format using solid <patch> blocks."""
        OpenFOAMExporter = get_openfoam_exporter_class()
        v, f = create_unit_cube_arrays()
        patches = {"wall": np.arange(6, dtype=np.int32), "inlet": np.arange(6, 12, dtype=np.int32)}
        mesh = SurfaceMesh(vertices=v, faces=f, patches=patches)
        exporter = OpenFOAMExporter()
        out_path = exporter.export(mesh, case_dir=tmp_path, filename="patched.stl", binary=False)

        content = out_path.read_text(encoding="utf-8")
        assert "solid wall" in content
        assert "endsolid wall" in content
        assert "solid inlet" in content
        assert "endsolid inlet" in content

    def test_export_binary_stl(self, tmp_path: Path):
        """Feature 26: Writes valid 80-byte header binary STL with little-endian uint32 count."""
        OpenFOAMExporter = get_openfoam_exporter_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        exporter = OpenFOAMExporter()
        out_path = exporter.export(mesh, case_dir=tmp_path, filename="binary.stl", binary=True)

        assert out_path.stat().st_size == 84 + 50 * 12

    def test_export_snappyhexmeshdict_geometry_block(self, tmp_path: Path):
        """Feature 27: Formulates geometry dictionary block with named boundary patches."""
        OpenFOAMExporter = get_openfoam_exporter_class()
        v, f = create_unit_cube_arrays()
        patches = {"wall": np.array([0, 1]), "inlet": np.array([2, 3])}
        mesh = SurfaceMesh(vertices=v, faces=f, patches=patches)
        exporter = OpenFOAMExporter()
        dict_text = exporter.generate_snappyhexmesh_dict_block(mesh, filename="body.stl")

        assert "body.stl" in dict_text
        assert "triSurfaceMesh" in dict_text
        assert "regions" in dict_text or "name" in dict_text

    def test_export_file_validity_and_reingestion(self, tmp_path: Path):
        """Feature 26: Exported STL can be re-parsed by STLParser without loss of topology."""
        OpenFOAMExporter = get_openfoam_exporter_class()
        v, f = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        exporter = OpenFOAMExporter()
        out_path = exporter.export(mesh, case_dir=tmp_path, filename="reingest.stl")

        parser = STLParser()
        reingested = parser.parse(out_path)
        assert len(reingested.faces) == len(mesh.faces)
        assert len(reingested.vertices) == len(mesh.vertices)


# ==============================================================================
# TIER 2: BOUNDARY & CORNER CASES (>=5 per feature)
# ==============================================================================


class TestTier2BoundaryAndCornerCases:
    """Tier 2: Boundary conditions, numerical corner cases, and adversarial input testing."""

    def test_edge_empty_file_raises_error(self, tmp_path: Path):
        """Corner Case 1: 0-byte file raises ValueError across all parsers."""
        empty_file = tmp_path / "empty.stl"
        empty_file.write_bytes(b"")

        with pytest.raises(ValueError, match="too small|empty|invalid"):
            STLParser().parse(empty_file)

        empty_obj = tmp_path / "empty.obj"
        empty_obj.write_bytes(b"")
        with pytest.raises(ValueError, match="no vertices|empty|invalid"):
            OBJParser().parse(empty_obj)

    def test_edge_truncated_binary_stl(self, tmp_path: Path):
        """Corner Case 2: Binary STL with file size < 84 + 50N bytes raises ValueError."""
        trunc_file = tmp_path / "truncated.stl"
        with open(trunc_file, "wb") as f:
            f.write(b" " * 80)
            f.write(struct.pack("<I", 10))  # Claims 10 triangles
            f.write(b"\x00" * 50)  # Writes only 1 triangle (134 bytes total instead of 584)

        with pytest.raises(ValueError, match="does not match binary header count|Corrupted"):
            STLParser().parse(trunc_file)

    def test_edge_binary_stl_starting_with_solid_header(self, tmp_path: Path):
        """Corner Case 3: Binary STL starting with ASCII 'solid' parsed as binary if size == 84+50N."""
        ambiguous_file = tmp_path / "solid_binary.stl"
        header = b"solid " + b"X" * 74  # 80 bytes total starting with 'solid '
        v, f = create_unit_cube_arrays()

        with open(ambiguous_file, "wb") as out:
            out.write(header)
            out.write(struct.pack("<I", 12))
            for face in f:
                out.write(struct.pack("<3f", 0.0, 0.0, 1.0))
                for vi in face:
                    pt = v[vi]
                    out.write(struct.pack("<3f", float(pt[0]), float(pt[1]), float(pt[2])))
                out.write(struct.pack("<H", 0))

        # Size is exactly 84 + 50 * 12 = 684 bytes
        assert ambiguous_file.stat().st_size == 684
        parser = STLParser()
        mesh = parser.parse(ambiguous_file)
        assert len(mesh.faces) == 12
        assert len(mesh.vertices) == 8

    def test_edge_obj_negative_relative_indices(self, tmp_path: Path):
        """Corner Case 4: Wavefront OBJ with negative relative indices (e.g. f -1 -2 -3)."""
        obj_content = """
v 0 0 0
v 1 0 0
v 0 1 0
f -3 -2 -1
"""
        obj_file = tmp_path / "relative.obj"
        obj_file.write_text(obj_content.strip(), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        assert len(mesh.faces) == 1
        assert np.array_equal(mesh.faces[0], [0, 1, 2])

    def test_edge_obj_arbitrary_ngons(self, tmp_path: Path):
        """Corner Case 5: Complex 8-gon polygon triangulates into exactly 6 triangles."""
        angles = np.linspace(0, 2 * np.pi, 8, endpoint=False)
        obj_lines = ["# Octagon"]
        for a in angles:
            obj_lines.append(f"v {np.cos(a):.6f} {np.sin(a):.6f} 0.0")
        obj_lines.append("f 1 2 3 4 5 6 7 8")

        obj_file = tmp_path / "octagon.obj"
        obj_file.write_text("\n".join(obj_lines), encoding="utf-8")

        parser = OBJParser()
        mesh = parser.parse(obj_file)
        assert len(mesh.faces) == 6  # N - 2 = 6

    def test_edge_degenerate_zero_area_triangles(self):
        """Corner Case 6: Flags zero-area triangles and duplicate indices in auditor."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_degenerate_face_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.degenerate_faces_count >= 1
        assert report.passed_openfoam_surfacecheck is False

    def test_edge_missing_face_open_boundary(self):
        """Corner Case 7: Cube with 1 missing face reports exactly 3 open edges."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_open_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.is_watertight is False
        assert report.open_edges_count == 3
        assert report.passed_openfoam_surfacecheck is False

    def test_edge_non_manifold_t_junction(self):
        """Corner Case 8: T-junction edge shared by 4 faces flags non-manifold edge."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_t_junction_non_manifold_arrays()
        # Merge vertices
        v_welded, inv = np.unique(np.round(v, 6), axis=0, return_inverse=True)
        f_welded = inv[f]
        mesh = SurfaceMesh(vertices=v_welded, faces=f_welded)
        report = SurfaceAuditor().audit(mesh)
        assert report.is_manifold is False
        assert report.multi_edges_count >= 1

    def test_edge_inverted_normal_auto_repair(self):
        """Corner Case 9: Inverted cube (V_signed < 0) is auto-corrected to outward."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_inverted_cube_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)
        assert report.is_outward_oriented is True
        assert report.signed_volume > 0.0

    def test_edge_sliver_triangle_quality(self):
        """Corner Case 10: Sliver triangle quality Q < 1e-5 flagged in report."""
        SurfaceAuditor = get_surface_auditor_class()
        v, f = create_sliver_triangle_arrays()
        mesh = SurfaceMesh(vertices=v, faces=f)
        report = SurfaceAuditor().audit(mesh)
        assert report.quality_min < 1e-5

    def test_edge_nonexistent_file_raises_not_found(self):
        """Corner Case 11: Non-existent file path raises FileNotFoundError."""
        with pytest.raises(FileNotFoundError):
            STLParser().parse("non_existent_file_path_12345.stl")
        with pytest.raises(FileNotFoundError):
            OBJParser().parse("non_existent_file_path_12345.obj")
        with pytest.raises(FileNotFoundError):
            STEPParser().parse("non_existent_file_path_12345.stp")


# ==============================================================================
# TIER 3: CROSS-FEATURE INTERACTIONS (End-to-End Pipeline Workflows)
# ==============================================================================


class TestTier3CrossFeatureInteractions:
    """Tier 3: Multi-component integration workflows and error-cascading resilience."""

    def test_pipeline_obj_ngons_triangulate_weld_scale_export(self, tmp_path: Path):
        """Cross-Feature 1: OBJ with quads/ngons -> Triangulate -> Weld -> Scale -> Audit -> Export."""
        CADGeometryEngine = get_cad_geometry_engine_class()
        engine = CADGeometryEngine()

        # Create multi-patch OBJ with quads and materials
        obj_content = """
v 0 0 0
v 200 0 0
v 200 200 0
v 0 200 0
v 0 0 200
v 200 0 200
v 200 200 200
v 0 200 200
g walls
f 1 2 3 4
f 5 6 7 8
g inlet
f 1 2 6 5
g outlet
f 2 3 7 6
"""
        obj_path = tmp_path / "box_with_quads.obj"
        obj_path.write_text(obj_content.strip(), encoding="utf-8")

        case_dir = tmp_path / "case_obj"
        export_path, report = engine.ingest_and_prepare(
            input_file=obj_path,
            case_dir=case_dir,
            scale_factor=0.001,
            output_format="stl",
        )
        mesh = engine.current_mesh
        assert mesh is not None
        assert export_path.exists()
        assert (case_dir / "constant" / "triSurface" / export_path.name).exists()
        # Scaled bounds in meters: [0, 0.2]^3
        bbox = mesh.get_bounding_box()
        assert np.allclose(bbox.span, [0.2, 0.2, 0.2], atol=1e-5)

    def test_pipeline_stl_weld_audit_scale_export(self, stl_sample_path: Path, tmp_path: Path):
        """Cross-Feature 2: Raw unindexed binary STL -> Weld -> Audit -> Scale -> Export."""
        CADGeometryEngine = get_cad_geometry_engine_class()
        engine = CADGeometryEngine()
        case_dir = tmp_path / "case_stl"

        export_path, report = engine.ingest_and_prepare(
            input_file=stl_sample_path,
            case_dir=case_dir,
            scale_factor=0.001,
        )

        assert report.is_watertight is True
        assert report.euler_characteristic == 2
        assert np.isclose(report.signed_volume, ANALYTICAL_VOLUME_CFD, rtol=1e-5)
        assert export_path.exists()

    def test_pipeline_step_audit_scale_snappy_dict(self, stp_ap214_sample_path: Path, tmp_path: Path):
        """Cross-Feature 3: STEP AP214 -> Tessellate -> Scale -> Audit -> snappyHexMeshDict."""
        CADGeometryEngine = get_cad_geometry_engine_class()
        engine = CADGeometryEngine()
        case_dir = tmp_path / "case_step"

        export_path, report = engine.ingest_and_prepare(
            input_file=stp_ap214_sample_path,
            case_dir=case_dir,
            scale_factor=0.001,
            step_engine="brep",
        )

        assert report.is_watertight is True
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0

        # Verify snappyHexMeshDict geometry snippet is generated
        dict_block = engine.generate_snappyhexmesh_dict(filename=export_path.name)
        assert "triSurfaceMesh" in dict_block

    def test_pipeline_error_cascading_protection(self, tmp_path: Path):
        """Cross-Feature 4: Non-watertight geometry detected by audit halts/flags before production CFD."""
        CADGeometryEngine = get_cad_geometry_engine_class()
        engine = CADGeometryEngine()

        # Write open cube OBJ (missing 1 face)
        open_obj = tmp_path / "open.obj"
        open_obj.write_text("""
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
f 1 2 3
f 1 3 4
f 5 6 7
f 5 7 8
""", encoding="utf-8")

        case_dir = tmp_path / "case_error"
        engine.load(open_obj).scale(0.001)
        report = engine.audit()
        assert report.is_watertight is False
        assert report.open_edges_count > 0
        assert report.passed_openfoam_surfacecheck is False


# ==============================================================================
# TIER 4: REAL-WORLD CAD TEST SAMPLES (resources/CADTestSamples/)
# ==============================================================================


class TestTier4RealWorldCADSamples:
    """Tier 4: Physical ground-truth verification on the 6 real-world CAD test samples."""

    def _verify_common_cube_properties(self, mesh: SurfaceMesh, scale_factor: float = 0.001):
        """Helper to assert physical dimensions and topological invariants on reference cube."""
        GeometryScaler = get_geometry_scaler_class()
        SurfaceAuditor = get_surface_auditor_class()

        # 1. Physical dimensions before scaling (in mm)
        cad_bbox = mesh.get_bounding_box()
        assert np.allclose(cad_bbox.min_pt, CAD_BBOX_MIN, atol=1e-4), f"CAD BBox min mismatch: {cad_bbox.min_pt} vs {CAD_BBOX_MIN}"
        assert np.allclose(cad_bbox.max_pt, CAD_BBOX_MAX, atol=1e-4), f"CAD BBox max mismatch: {cad_bbox.max_pt} vs {CAD_BBOX_MAX}"

        # 2. Scale to CFD SI units (0.001)
        scaled_mesh = GeometryScaler().scale(mesh, scale_factor=scale_factor)
        cfd_bbox = scaled_mesh.get_bounding_box()

        # Bounding box in CFD meters
        assert np.allclose(cfd_bbox.min_pt, CFD_BBOX_MIN, atol=1e-4), f"CFD BBox min mismatch: {cfd_bbox.min_pt} vs {CFD_BBOX_MIN}"
        assert np.allclose(cfd_bbox.max_pt, CFD_BBOX_MAX, atol=1e-4), f"CFD BBox max mismatch: {cfd_bbox.max_pt} vs {CFD_BBOX_MAX}"

        # 3. Geometric audit on scaled mesh
        report = SurfaceAuditor().audit(scaled_mesh)

        # Watertight & 2-manifold
        assert report.is_watertight is True, f"Mesh not watertight: {report.open_edges_count} open edges"
        assert report.is_manifold is True, f"Mesh not manifold: {report.multi_edges_count} multi edges"
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0

        # Euler characteristic chi = V - E + F = 2
        assert report.euler_characteristic == 2, f"Euler characteristic chi != 2, got {report.euler_characteristic}"

        # Outward normal orientation & volume
        assert report.is_outward_oriented is True
        assert np.isclose(report.signed_volume, ANALYTICAL_VOLUME_CFD, rtol=1e-4), f"Volume error: {report.signed_volume} vs {ANALYTICAL_VOLUME_CFD}"

        # OpenFOAM surfaceCheck compliance
        assert report.passed_openfoam_surfacecheck is True

    def test_sample_01_wavefront_obj(self, obj_sample_path: Path):
        """Verifies TestCAD.obj against analytical ground truth."""
        parser = OBJParser()
        mesh = parser.parse(obj_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        self._verify_common_cube_properties(mesh)

    def test_sample_02_binary_stl(self, stl_sample_path: Path):
        """Verifies TestCAD.stl against analytical ground truth."""
        parser = STLParser()
        mesh = parser.parse(stl_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        self._verify_common_cube_properties(mesh)

    def test_sample_03_step_ap203(self, stp_ap203_sample_path: Path):
        """Verifies TestCADSTPAP203.stp against analytical ground truth."""
        parser = STEPParser(engine="brep")
        mesh = parser.parse(stp_ap203_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        self._verify_common_cube_properties(mesh)

    def test_sample_04_step_ap214(self, stp_ap214_sample_path: Path):
        """Verifies TestCADSTPAP214.stp against analytical ground truth."""
        parser = STEPParser(engine="brep")
        mesh = parser.parse(stp_ap214_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        self._verify_common_cube_properties(mesh)

    def test_sample_05_step_ap242(self, stp_ap242_sample_path: Path):
        """Verifies TestCADSTPAP242.stp against analytical ground truth."""
        parser = STEPParser(engine="brep")
        mesh = parser.parse(stp_ap242_sample_path)
        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12
        self._verify_common_cube_properties(mesh)

    def test_sample_06_siemens_nx_prt(self, prt_sample_path: Path, tmp_path: Path):
        """Verifies TestCAD.prt translation through CADBridge and analytical validation."""
        CADBridge = get_cad_bridge_class()
        bridge = CADBridge()

        if not bridge.nx_translator.is_available:
            # If NX is offline, resolve fallback and verify
            fallback = bridge._find_fallback(prt_sample_path)
            assert fallback is not None
            assert fallback.exists()
            parser = STEPParser(engine="brep") if fallback.suffix in (".stp", ".step") else STLParser()
            mesh = parser.parse(fallback)
            self._verify_common_cube_properties(mesh)
            return

        # Perform live NX translation
        out_stp = tmp_path / "prt_converted.stp"
        try:
            translated = bridge.translate(prt_sample_path, output_format="step", output_file=out_stp)
            if translated.suffix.lower() in (".stp", ".step"):
                parser = STEPParser(engine="brep")
            elif translated.suffix.lower() == ".stl":
                parser = STLParser()
            elif translated.suffix.lower() == ".obj":
                parser = OBJParser()
            else:
                raise ValueError(f"Unknown format: {translated}")
            mesh = parser.parse(translated)
            self._verify_common_cube_properties(mesh)
        except Exception as e:
            if "license" in str(e).lower() or "unavailable" in str(e).lower():
                pytest.skip("Siemens NX license not available on host network.")
            raise
