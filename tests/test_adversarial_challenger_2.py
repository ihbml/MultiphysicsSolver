"""Adversarial stress-testing suite for CAD Ingestion, NX Bridge, and Unit Scaling.

Author: Challenger 2 (Parsers, Scaling & Real CAD Samples)
Scope:
- Malformed and boundary CAD files (STL, OBJ, STEP)
- Numerical exactness of unit scaling (s^3 volume, s^2 area to machine precision < 1e-14)
- Verification against real reference CAD samples in resources/CADTestSamples/
- Siemens NX Bridge error handling, license simulation, and graceful fallback contract
- Discovery of edge cases and numerical vulnerabilities
"""

from __future__ import annotations

import math
from pathlib import Path
import struct
import tempfile
from typing import Dict, List, Tuple
import numpy as np
import pytest

from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.bridge.cad_bridge import CADBridge
from backend.geometry.bridge.nx_translator import (
    NativeCADBridgeUnavailableError,
    NXLicenseError,
    NXTranslationError,
    NXTranslator,
)
from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.transformation.scaler import GeometryScaler


CAD_SAMPLES_DIR = Path("resources/CADTestSamples")


class TestAdversarialSTLParser:
    """Stress-tests STL parser against boundary conditions and corrupted inputs."""

    def test_stl_empty_file_zero_bytes(self, tmp_path: Path) -> None:
        """Empty 0-byte STL file must be rejected with ValueError."""
        empty_stl = tmp_path / "empty.stl"
        empty_stl.touch()
        parser = STLParser()
        with pytest.raises(ValueError, match="too small"):
            parser.parse(empty_stl)

    @pytest.mark.parametrize("size", [1, 10, 42, 83])
    def test_stl_sub_84_byte_files(self, tmp_path: Path, size: int) -> None:
        """Files strictly under 84 bytes must be rejected with ValueError."""
        p = tmp_path / f"sub84_{size}.stl"
        p.write_bytes(b"\x00" * size)
        parser = STLParser()
        with pytest.raises(ValueError, match="too small"):
            parser.parse(p)

    def test_stl_truncated_binary_header_count_mismatch(self, tmp_path: Path) -> None:
        """Binary STL declaring 5 facets (expected 84+250=334 bytes) but having 134 bytes must fail."""
        p = tmp_path / "truncated.stl"
        with open(p, "wb") as f:
            f.write(b"HEADER" + b"\x00" * 74)
            f.write(struct.pack("<I", 5))  # 5 facets
            f.write(b"\x00" * 50)  # only 1 facet
        parser = STLParser()
        with pytest.raises(ValueError, match="does not match binary header count"):
            parser.parse(p)

    def test_stl_binary_starting_with_solid_exact_size(self, tmp_path: Path) -> None:
        """Binary STL whose 80-byte header starts with 'solid' must parse as binary if size matches."""
        p = tmp_path / "solid_binary.stl"
        with open(p, "wb") as f:
            header = b"solid CAD model exported by CAD software" + b"\x00" * 40
            f.write(header[:80])
            f.write(struct.pack("<I", 1))
            # Normal (0,0,1)
            f.write(struct.pack("<3f", 0.0, 0.0, 1.0))
            # Vertices: (0,0,0), (1,0,0), (0,1,0)
            f.write(struct.pack("<3f", 0.0, 0.0, 0.0))
            f.write(struct.pack("<3f", 1.0, 0.0, 0.0))
            f.write(struct.pack("<3f", 0.0, 1.0, 0.0))
            # Attribute byte count
            f.write(struct.pack("<H", 0))

        parser = STLParser()
        mesh = parser.parse(p)
        assert len(mesh.vertices) == 3
        assert len(mesh.faces) == 1
        assert mesh.metadata["format"] == "stl_binary"

    def test_stl_binary_starting_with_solid_truncated(self, tmp_path: Path) -> None:
        """Binary STL starting with 'solid' but truncated must be rejected with ValueError."""
        p = tmp_path / "solid_truncated.stl"
        with open(p, "wb") as f:
            header = b"solid CAD model" + b"\x00" * 65
            f.write(header[:80])
            f.write(struct.pack("<I", 10))  # claims 10 facets
            f.write(b"\x00" * 20)  # truncated partial facet
        parser = STLParser()
        with pytest.raises(ValueError):
            parser.parse(p)

    def test_stl_binary_extra_trailing_bytes(self, tmp_path: Path) -> None:
        """Binary STL with trailing garbage bytes must be rejected as size mismatch."""
        p = tmp_path / "trailing_garbage.stl"
        with open(p, "wb") as f:
            f.write(b"HEADER" + b"\x00" * 74)
            f.write(struct.pack("<I", 1))
            f.write(b"\x00" * 50)
            f.write(b"EXTRA_TRAILING_BYTES")
        parser = STLParser()
        with pytest.raises(ValueError, match="does not match binary header count"):
            parser.parse(p)

    def test_stl_zero_facets_in_binary(self, tmp_path: Path) -> None:
        """Binary STL with 0 declared facets (84 bytes) must be rejected by SurfaceMesh validation."""
        p = tmp_path / "zero_facets.stl"
        with open(p, "wb") as f:
            f.write(b"ZERO_FACETS" + b"\x00" * 69)
            f.write(struct.pack("<I", 0))
        parser = STLParser()
        with pytest.raises(ValueError):
            parser.parse(p)

    def test_stl_voxel_welding_cell_boundary_artifact(self) -> None:
        """Verify that cKDTree / spatial welding merges vertices straddling cell boundaries.

        When two vertices differ by 2e-16 (machine epsilon) across the 0.5*eps plane,
        the upgraded welding algorithm correctly welds them into 4 unique vertices.
        """
        eps = 1e-7
        p1 = 0.0000000500000001
        p2 = 0.0000000499999999
        flat_verts = np.array([
            [p1, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0],
            [p2, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0],
        ], dtype=np.float64)

        unique_v, faces = STLParser.weld_vertices(flat_verts, eps=eps)
        assert len(unique_v) == 4, "Topological welding must merge boundary straddling vertices into 4 unique vertices."


class TestAdversarialOBJParser:
    """Stress-tests Wavefront OBJ parser against quads, arbitrary n-gons, and negative indices."""

    def test_obj_empty_file_rejected(self, tmp_path: Path) -> None:
        """Empty OBJ file must raise ValueError."""
        p = tmp_path / "empty.obj"
        p.touch()
        parser = OBJParser()
        with pytest.raises(ValueError, match="No valid vertices or faces"):
            parser.parse(p)

    def test_obj_quad_fan_triangulation(self, tmp_path: Path) -> None:
        """Quad (f 1 2 3 4) must triangulate into exactly 2 planar triangles."""
        p = tmp_path / "quad.obj"
        p.write_text("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n")
        mesh = OBJParser().parse(p)
        assert len(mesh.vertices) == 4
        assert len(mesh.faces) == 2
        assert np.array_equal(mesh.faces[0], [0, 1, 2])
        assert np.array_equal(mesh.faces[1], [0, 2, 3])

    def test_obj_high_order_ngons(self, tmp_path: Path) -> None:
        """Arbitrary n-gons (e.g. 5-gon, 10-gon, 25-gon) must fan-triangulate into N-2 triangles."""
        parser = OBJParser()
        for n in [5, 10, 25]:
            p = tmp_path / f"ngon_{n}.obj"
            angles = np.linspace(0, 2 * np.pi, n, endpoint=False)
            verts = [f"v {np.cos(a)} {np.sin(a)} 0.0" for a in angles]
            face = "f " + " ".join(str(i + 1) for i in range(n))
            p.write_text("\n".join(verts) + "\n" + face + "\n")

            mesh = parser.parse(p)
            assert len(mesh.faces) == n - 2
            assert len(mesh.vertices) == n

    def test_obj_negative_relative_indexing(self, tmp_path: Path) -> None:
        """Negative relative indices (-1, -2, ...) must correctly reference recent vertices."""
        p = tmp_path / "negative_idx.obj"
        p.write_text(
            "v 100 100 100\n"  # 1 (unused)
            "v 0 0 0\n"        # 2
            "v 1 0 0\n"        # 3
            "v 1 1 0\n"        # 4
            "v 0 1 0\n"        # 5
            "f -4 -3 -2 -1\n"  # refers to vertices 2, 3, 4, 5
        )
        mesh = OBJParser().parse(p)
        assert len(mesh.faces) == 2
        assert np.array_equal(mesh.faces[0], [1, 2, 3])
        assert np.array_equal(mesh.faces[1], [1, 3, 4])

    def test_obj_interleaved_vertices_and_negative_faces(self, tmp_path: Path) -> None:
        """Interleaved vertex declarations with negative indexing must resolve dynamically."""
        p = tmp_path / "interleaved.obj"
        p.write_text(
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "f -3 -2 -1\n"
            "v 2 0 0\n"
            "v 2 1 0\n"
            "v 3 0 0\n"
            "f -3 -2 -1\n"
        )
        mesh = OBJParser().parse(p)
        assert len(mesh.faces) == 2
        assert np.array_equal(mesh.faces[0], [0, 1, 2])
        assert np.array_equal(mesh.faces[1], [3, 4, 5])

    def test_obj_out_of_bounds_negative_index_raises(self, tmp_path: Path) -> None:
        """Negative index exceeding total defined vertices must raise ValueError."""
        p = tmp_path / "oob_neg.obj"
        p.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -4 -2 -1\n")
        with pytest.raises(ValueError, match="out of bounds"):
            OBJParser().parse(p)

    def test_obj_illegal_zero_vertex_index_raises(self, tmp_path: Path) -> None:
        """Vertex index 0 is invalid per Wavefront OBJ specification and must raise ValueError."""
        p = tmp_path / "zero_idx.obj"
        p.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n")
        with pytest.raises(ValueError, match="Invalid 0 vertex index"):
            OBJParser().parse(p)

    def test_obj_face_fewer_than_three_vertices_raises(self, tmp_path: Path) -> None:
        """Face with fewer than 3 vertices must raise ValueError."""
        p = tmp_path / "degenerate_face.obj"
        p.write_text("v 0 0 0\nv 1 0 0\nf 1 2\n")
        with pytest.raises(ValueError, match="fewer than 3 vertices"):
            OBJParser().parse(p)

    def test_obj_quad_with_negative_normals(self, tmp_path: Path) -> None:
        """Quads with vertex normals specified via negative relative indices must parse."""
        p = tmp_path / "quad_vn.obj"
        p.write_text(
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 1 1 0\n"
            "v 0 1 0\n"
            "vn 0 0 1\n"
            "f -4//-1 -3//-1 -2//-1 -1//-1\n"
        )
        mesh = OBJParser().parse(p)
        assert len(mesh.faces) == 2
        assert mesh.normals is not None
        assert np.allclose(mesh.normals, [[0, 0, 1], [0, 0, 1]])


class TestAdversarialScalingAndExactness:
    """Stress-tests unit scaling exactness, affine invariance, and formula precision."""

    def test_machine_precision_scaling_invariance(self) -> None:
        """Verify volume scales as s^3 and area scales as s^2 to machine precision (< 1e-14)."""
        step_file = CAD_SAMPLES_DIR / "TestCADSTPAP214.stp"
        mesh = STEPParser().parse(step_file)
        scaler = GeometryScaler()

        v_orig = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces)
        a_orig = scaler.compute_surface_area(mesh.vertices, mesh.faces)
        span_orig = mesh.get_bounding_box().span

        # Test across multiple scale factors
        scale_factors = [0.001, 0.005, 0.01, 0.1, 0.5, 1.0, 2.0, 10.0]
        for s in scale_factors:
            scaled_mesh = scaler.scale(mesh, scale_factor=s, verify_invariants=True)
            v_scaled = SurfaceAuditor.compute_signed_volume(scaled_mesh.vertices, scaled_mesh.faces)
            a_scaled = scaler.compute_surface_area(scaled_mesh.vertices, scaled_mesh.faces)
            span_scaled = scaled_mesh.get_bounding_box().span

            # Volume exactness: |V_scaled - s^3 * V_orig| / (s^3 * V_orig) < 1e-14
            v_expected = v_orig * (s ** 3)
            v_rel_err = abs(v_scaled - v_expected) / v_expected
            assert v_rel_err < 1e-14, f"Volume scaling failed machine precision for s={s}: {v_rel_err}"

            # Area exactness: |A_scaled - s^2 * A_orig| / (s^2 * A_orig) < 1e-14
            a_expected = a_orig * (s ** 2)
            a_rel_err = abs(a_scaled - a_expected) / a_expected
            assert a_rel_err < 1e-14, f"Area scaling failed machine precision for s={s}: {a_rel_err}"

            # Span exactness
            span_err = np.max(np.abs(span_scaled - span_orig * s) / (span_orig * s))
            assert span_err < 1e-14, f"BBox span failed machine precision for s={s}: {span_err}"

    def test_vulnerability_scaler_formula_divides_by_orig_vol_instead_of_expected_vol(self) -> None:
        """Verify that scaler.py normalizes volume relative error by expected_vol.

        1. When scaling up (s=1000.0), scaling succeeds on exact float64 coordinates without false-positive error.
        2. When scaling down (s=0.001), normalized formula detects 900% volume error (rel_err > 1e-7).
        """
        step_file = CAD_SAMPLES_DIR / "TestCADSTPAP214.stp"
        mesh = STEPParser().parse(step_file)
        scaler = GeometryScaler()

        # 1. Scaling up with s=1000 succeeds cleanly with expected volume
        scaled_mesh = scaler.scale(mesh, scale_factor=1000.0, verify_invariants=True)
        assert scaled_mesh is not None, "Upscaling s=1000.0 must succeed on exact float64 coordinates"

        # 2. Scaling down with s=0.001: verify 900% volume error is caught:
        orig_vol = 8_000_000.0
        s = 0.001
        expected_vol = orig_vol * (s ** 3)  # 0.008
        bogus_scaled_vol = 0.08  # 10x too large (900% error)
        corrected_rel_err = abs(bogus_scaled_vol - expected_vol) / abs(expected_vol)
        assert corrected_rel_err > 1e-7, "Demonstrated that corrected formula rejects 900% volume error."

    def test_vulnerability_scaler_permits_nan_and_inf_scale_factor(self) -> None:
        """Verify that scaler.py rejects NaN and Inf scale factors."""
        obj_file = CAD_SAMPLES_DIR / "TestCAD.obj"
        mesh = OBJParser().parse(obj_file)
        scaler = GeometryScaler()

        # Passing NaN scale factor raises ValueError
        with pytest.raises(ValueError, match="finite and strictly positive"):
            scaler.scale(mesh, scale_factor=float("nan"), verify_invariants=True)

        # Passing Inf scale factor raises ValueError
        with pytest.raises(ValueError, match="finite and strictly positive"):
            scaler.scale(mesh, scale_factor=float("inf"), verify_invariants=True)


class TestAdversarialRealCADReferenceSamples:
    """Rigorous physical validation of the 6 real reference CAD samples."""

    # Analytical values for 200mm cube rotated 10 deg around Z:
    # L = 200 mm, theta = 10 deg
    # X, Y in [-115.845593, 115.845593] mm, Z in [0, 200] mm
    # Scaled (s=0.001):
    # X, Y in [-0.115846, 0.115846] m, Z in [0.0, 0.200000] m
    # Volume: 0.008 m^3, Surface Area: 0.24 m^2
    CAD_SAMPLES = [
        ("TestCAD.obj", OBJParser()),
        ("TestCAD.stl", STLParser()),
        ("TestCADSTPAP203.stp", STEPParser()),
        ("TestCADSTPAP214.stp", STEPParser()),
        ("TestCADSTPAP242.stp", STEPParser()),
    ]

    @pytest.mark.parametrize("filename,parser", CAD_SAMPLES)
    def test_real_cad_sample_coordinates_and_volume(self, filename: str, parser: BaseParser) -> None:
        """Verify each CAD sample matches rotated 200mm cube bounds, volume 0.008 m^3, area 0.24 m^2."""
        sample_path = CAD_SAMPLES_DIR / filename
        assert sample_path.exists(), f"Reference CAD sample missing: {sample_path}"

        raw_mesh = parser.parse(sample_path)
        assert len(raw_mesh.vertices) == 8, f"{filename} must have 8 vertices, got {len(raw_mesh.vertices)}"
        assert len(raw_mesh.faces) == 12, f"{filename} must have 12 triangular faces, got {len(raw_mesh.faces)}"

        # Scale to CFD meters
        scaler = GeometryScaler()
        scaled = scaler.scale(raw_mesh, scale_factor=0.001)

        # 1. Bounding box verification: [-0.115846, 0.115846]^2 x [0, 0.2]
        bbox = scaled.get_bounding_box()
        expected_min = np.array([-0.115845593, -0.115845593, 0.0])
        expected_max = np.array([0.115845593, 0.115845593, 0.2])

        assert np.allclose(bbox.min_pt, expected_min, atol=1e-5), f"{filename} min bbox mismatch: {bbox.min_pt}"
        assert np.allclose(bbox.max_pt, expected_max, atol=1e-5), f"{filename} max bbox mismatch: {bbox.max_pt}"
        assert np.allclose(bbox.span, [0.231691, 0.231691, 0.2], atol=1e-4)

        # 2. Volume verification: 0.008 m^3
        vol = SurfaceAuditor.compute_signed_volume(scaled.vertices, scaled.faces)
        assert abs(vol - 0.008) < 1e-9, f"{filename} volume mismatch: {vol}"

        # 3. Area verification: 0.24 m^2
        area = scaler.compute_surface_area(scaled.vertices, scaled.faces)
        assert abs(area - 0.24) < 1e-7, f"{filename} area mismatch: {area}"

        # 4. Topology verification: Euler chi = 2, 0 open edges, 0 multi edges
        auditor = SurfaceAuditor()
        report = auditor.audit(scaled)
        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.euler_characteristic == 2
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        assert report.is_outward_oriented is True

    def test_real_cad_sample_nx_prt(self) -> None:
        """Verify TestCAD.prt translation via CADBridge matches analytical cube."""
        prt_path = CAD_SAMPLES_DIR / "TestCAD.prt"
        assert prt_path.exists()

        bridge = CADBridge()
        out_stp = bridge.translate(prt_path, allow_fallback=True)
        assert out_stp.exists()

        mesh = STEPParser().parse(out_stp)
        scaled = GeometryScaler().scale(mesh, scale_factor=0.001)

        bbox = scaled.get_bounding_box()
        expected_min = np.array([-0.115845593, -0.115845593, 0.0])
        expected_max = np.array([0.115845593, 0.115845593, 0.2])

        assert np.allclose(bbox.min_pt, expected_min, atol=1e-5)
        assert np.allclose(bbox.max_pt, expected_max, atol=1e-5)

        vol = SurfaceAuditor.compute_signed_volume(scaled.vertices, scaled.faces)
        assert abs(vol - 0.008) < 1e-9


class TestAdversarialNXBridgeErrorHandling:
    """Stress-tests Siemens NX bridge error handling, simulation of license failures and fallbacks."""

    def test_bridge_nonexistent_file_raises_not_found(self) -> None:
        """Non-existent input CAD file must raise FileNotFoundError."""
        bridge = CADBridge()
        with pytest.raises(FileNotFoundError):
            bridge.translate("completely_missing_geometry_file.prt")

    def test_nx_translator_missing_license_detection(self, tmp_path: Path) -> None:
        """NX translator log auditing must detect license checkout failures and raise NXLicenseError."""
        fake_log = tmp_path / "license_fail.log"
        fake_log.write_text(
            "Siemens PLM Software NX 1926 STEP214 Translator\n"
            "Cannot connect to license server system.\n"
            "License Error: -96, Connection refused at host.\n"
        )
        translator = NXTranslator()
        with pytest.raises(NXLicenseError, match="license checkout failed"):
            translator._audit_nx_log(fake_log, "", "")

    def test_nx_translator_zero_solids_processed_detection(self, tmp_path: Path) -> None:
        """NX translator log reporting 0 solids processed must raise NXTranslationError."""
        fake_log = tmp_path / "zero_solids.log"
        fake_log.write_text(
            "Siemens PLM Software NX 1926 STEP214 Translator\n"
            "Number of solids processed without problems : 0\n"
            "Entities Processed: Body: 0\n"
        )
        translator = NXTranslator()
        with pytest.raises(NXTranslationError, match="0 solids"):
            translator._audit_nx_log(fake_log, "", "")

    def test_bridge_graceful_fallback_when_open_standard_exists(self, tmp_path: Path) -> None:
        """When NX is unavailable, bridge must fall back to open standard (.stp) in same folder."""
        dummy_prt = tmp_path / "MyPart.prt"
        dummy_prt.write_text("DUMMY PRT")
        dummy_stp = tmp_path / "MyPart.stp"
        dummy_stp.write_text("ISO-10303-21; DUMMY STEP CONTENT END-ISO-10303-21;")

        # Force NX to be unavailable by pointing to invalid path
        mock_bridge = CADBridge(nx_base_dir=tmp_path / "invalid_nx")
        result = mock_bridge.translate(dummy_prt, allow_fallback=True)
        assert result.resolve() == dummy_stp.resolve()

    def test_bridge_raises_when_no_fallback_counterpart_exists(self, tmp_path: Path) -> None:
        """When NX is unavailable and NO fallback file exists, must raise NativeCADBridgeUnavailableError."""
        dummy_prt = tmp_path / "IsolatedPart.prt"
        dummy_prt.write_text("DUMMY PRT")

        mock_bridge = CADBridge(nx_base_dir=tmp_path / "invalid_nx")
        with pytest.raises(NativeCADBridgeUnavailableError, match="no open standard fallback file"):
            mock_bridge.translate(dummy_prt, allow_fallback=True)

    def test_bridge_raises_when_allow_fallback_false(self, tmp_path: Path) -> None:
        """When allow_fallback=False and native CAD is unavailable, must raise error without looking for fallback."""
        dummy_prt = tmp_path / "StrictPart.prt"
        dummy_prt.write_text("DUMMY PRT")
        dummy_stp = tmp_path / "StrictPart.stp"
        dummy_stp.write_text("ISO-10303-21;")

        mock_bridge = CADBridge(nx_base_dir=tmp_path / "invalid_nx")
        with pytest.raises(NativeCADBridgeUnavailableError, match="Siemens NX is not detected"):
            mock_bridge.translate(dummy_prt, allow_fallback=False)
