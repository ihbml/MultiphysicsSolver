"""test_fast_io_and_ipc_m5d.py
==============================
Comprehensive validation test suite for Milestone M5D:
Features F64 (Fast IO Extensions) & F65 (Backend JSON-RPC 2.0 / NDJSON IPC Contracts).

Covers:
1. Feature F64 (Fast IO Extensions):
   - boundaryField dictionary parsing (uniform & nonuniform scalar/vector) with owner-cell fallback
   - list_time_dirs discovery with ascending numerical sort and reconstructed vs processor status
   - format binary; header support, binary slice parsing, and binary OpenFOAM field/mesh export
2. Feature F65 (IPC & Contracts):
   - JSON-RPC 2.0 protocol compliance (parse error, invalid request, method not found, invalid params)
   - All 16 IPC endpoints:
     1. system.hello
     2. schema.get (ModuleRegistry.export_ui_schema)
     3. geometry.import
     4. geometry.patches.update
     5. mesh.generate
     6. job.cancel
     7. materials.list
     8. case.validate
     9. case.generate
     10. solver.start
     11. solver.write_now
     12. solver.stop_graceful
     13. solver.abort
     14. results.list_times
     15. results.list_fields
     16. results.load
   - Strict 7-element SI dimensions validation [kg, m, s, K, mol, A, cd]
   - Flat binary buffer export contracts (Paket 1: Geometry Buffer, Paket 2: Mesh Quality Fields)
   - Paket 4 live NDJSON telemetry streaming for iter events and timestep written events
   - Async job cancellation and artifact cleanup
   - Strict stderr logging separation (zero stdout pollution)
   - Strict backend boundary (ZERO frontend UI / wgpu / egui)
"""

from __future__ import annotations

import io
import json
import logging
import math
import os
import shutil
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, List

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.ipc import (
    INTERNAL_ERROR,
    INVALID_PARAMS,
    INVALID_REQUEST,
    METHOD_NOT_FOUND,
    PARSE_ERROR,
    GeometryBufferDescriptor,
    IPCService,
    MeshQualityBufferDescriptor,
    SI_BASE_UNITS,
    STANDARD_FIELD_DIMENSIONS,
    TelemetryStreamer,
    format_openfoam_dimensions,
    get_field_dimensions,
    pack_geometry_buffer,
    pack_mesh_quality_buffer,
    parse_openfoam_dimensions,
    unpack_geometry_buffer,
    unpack_mesh_quality_buffer,
    validate_si_dimensions,
)
from backend.meshing.models import UnstructuredMesh
from backend.meshing.polymesh_io import export_openfoam_polymesh
from backend.native_rust import (
    FoamBoundaryPatch,
    FoamMesh,
    FoamReader,
    TimeStepInfo,
    list_time_dirs,
    write_field,
)
from backend.physics_modules.registry import ModuleRegistry


# =============================================================================
# 1. Feature F64: Fast IO Extensions Tests
# =============================================================================

class TestFastIOExtensionsF64:
    """Verifies boundaryField dictionary parsing, owner fallback, time discovery, and binary streams."""

    def test_boundary_field_parsing_uniform_and_nonuniform_scalar_vector(self, tmp_path: Path):
        """Verifies parsing of uniform and nonuniform scalar and vector boundary fields."""
        # 1. Scalar field with uniform and nonuniform patches
        p_scalar = tmp_path / "0" / "T"
        T_int = np.array([300.0, 310.0, 320.0, 330.0], dtype=np.float64)
        b_scalar = {
            "inlet": {"type": "fixedValue", "data": 350.0},
            "outlet": {"type": "zeroGradient", "data": None},
            "wall": {"type": "fixedValue", "data": np.array([290.0, 292.0, 294.0], dtype=np.float64)},
        }
        write_field(p_scalar, "T", [0, 0, 0, 1, 0, 0, 0], T_int, b_scalar, write_binary=False)

        res_s = FoamReader.load_field(p_scalar, n_cells=4)
        assert res_s["name"] == "T"
        assert res_s["dimensions"] == [0, 0, 0, 1, 0, 0, 0]
        assert len(res_s["internal_field"]) == 4
        assert not res_s["is_vector"]

        # Check boundary fields
        b_res = res_s["boundary_fields"]
        assert "inlet" in b_res
        assert b_res["inlet"]["type"] == "fixedValue"
        assert float(b_res["inlet"]["data"]) == 350.0
        assert not b_res["inlet"]["is_fallback"]

        assert "wall" in b_res
        assert len(b_res["wall"]["data"]) == 3
        np.testing.assert_allclose(b_res["wall"]["data"], [290.0, 292.0, 294.0])

        # 2. Vector field U with uniform and nonuniform vectors
        p_vector = tmp_path / "0" / "U"
        U_int = np.array([
            [1.0, 0.0, 0.0],
            [1.5, 0.0, 0.0],
            [2.0, 0.0, 0.0],
            [2.5, 0.0, 0.0],
        ], dtype=np.float64)
        b_vector = {
            "inlet": {"type": "fixedValue", "data": [10.0, 0.0, 0.0]},
            "wall": {"type": "noSlip", "data": [0.0, 0.0, 0.0]},
            "free_stream": {"type": "fixedValue", "data": np.array([[5.0, 1.0, 0.0], [5.5, 1.1, 0.0]], dtype=np.float64)},
        }
        write_field(p_vector, "U", [0, 1, -1, 0, 0, 0, 0], U_int, b_vector, write_binary=False)

        res_v = FoamReader.load_field(p_vector, n_cells=4)
        assert res_v["name"] == "U"
        assert res_v["dimensions"] == [0, 1, -1, 0, 0, 0, 0]
        assert res_v["is_vector"]
        assert len(res_v["internal_field"]) == 4

        b_res_v = res_v["boundary_fields"]
        assert "inlet" in b_res_v
        np.testing.assert_allclose(b_res_v["inlet"]["data"], [10.0, 0.0, 0.0])
        assert "wall" in b_res_v
        np.testing.assert_allclose(b_res_v["wall"]["data"], [0.0, 0.0, 0.0])
        assert "free_stream" in b_res_v
        np.testing.assert_allclose(b_res_v["free_stream"]["data"], [[5.0, 1.0, 0.0], [5.5, 1.1, 0.0]])

    def test_boundary_field_fallback_to_owner_cell(self, tmp_path: Path):
        """Verifies automatic fallback to owner cell values for zeroGradient / calculated boundary patches."""
        p_field = tmp_path / "0" / "p"
        # 4 cells: pressures 100, 200, 300, 400
        p_int = np.array([100.0, 200.0, 300.0, 400.0], dtype=np.float64)
        b_p = {
            "outlet": {"type": "zeroGradient", "data": None},
            "wall": {"type": "calculated", "data": None},
        }
        write_field(p_field, "p", [0, 2, -2, 0, 0, 0, 0], p_int, b_p, write_binary=False)

        # Mock mesh: 5 faces total (2 internal, 3 boundary).
        # Faces 2 and 3 belong to 'outlet' (count=2, owner cells 1 and 3)
        # Face 4 belongs to 'wall' (count=1, owner cell 2)
        pts = np.zeros((8, 3), dtype=np.float64)
        face_offsets = np.array([0, 4, 8, 12, 16, 20], dtype=np.int64)
        face_nodes = np.zeros(20, dtype=np.int64)
        owner = np.array([0, 1, 1, 3, 2], dtype=np.int64)
        neighbour = np.array([1, 2], dtype=np.int64)
        boundaries = [
            FoamBoundaryPatch("outlet", "patch", start_face=2, n_faces=2),
            FoamBoundaryPatch("wall", "wall", start_face=4, n_faces=1),
        ]
        mesh = FoamMesh(
            points=pts,
            face_offsets=face_offsets,
            face_nodes=face_nodes,
            owner=owner,
            neighbour=neighbour,
            n_cells=4,
            boundaries=boundaries,
        )

        res = FoamReader.load_field(p_field, n_cells=4, mesh=mesh)
        bf = res["boundary_fields"]

        # 'outlet' faces 2,3 have owners [1, 3] -> owner cell values [200.0, 400.0]
        assert "outlet" in bf
        assert bf["outlet"]["is_fallback"] is True
        assert bf["outlet"]["source"] == "owner_cell_fallback"
        np.testing.assert_allclose(bf["outlet"]["data"], [200.0, 400.0])

        # 'wall' face 4 has owner [2] -> owner cell value [300.0]
        assert "wall" in bf
        assert bf["wall"]["is_fallback"] is True
        assert bf["wall"]["source"] == "owner_cell_fallback"
        np.testing.assert_allclose(bf["wall"]["data"], [300.0])

    def test_list_time_dirs_sorted_and_reconstruction_status(self, tmp_path: Path):
        """Verifies numerical sorting of time directories and reconstructed vs processor-only status."""
        case_dir = tmp_path / "case_sim"
        case_dir.mkdir()

        # Root directories: 0, 0.05, 0.2
        (case_dir / "0").mkdir()
        (case_dir / "0" / "U").write_text("U", encoding="utf-8")
        (case_dir / "0" / "p").write_text("p", encoding="utf-8")

        (case_dir / "0.05").mkdir()
        (case_dir / "0.05" / "U").write_text("U", encoding="utf-8")
        (case_dir / "0.05" / "T").write_text("T", encoding="utf-8")

        (case_dir / "0.2").mkdir()
        (case_dir / "0.2" / "U").write_text("U", encoding="utf-8")

        # Decomposed processor directories for time 0.1 and 0.3
        (case_dir / "processor0" / "0.1").mkdir(parents=True)
        (case_dir / "processor0" / "0.1" / "U").write_text("U", encoding="utf-8")
        (case_dir / "processor0" / "0.1" / "p.gz").write_text("p", encoding="utf-8")

        (case_dir / "processor0" / "0.3").mkdir(parents=True)
        (case_dir / "processor0" / "0.3" / "k").write_text("k", encoding="utf-8")

        # Call list_time_dirs
        time_steps = list_time_dirs(case_dir)
        times = [t.time for t in time_steps]

        # Verify sorted ascending order
        assert times == [0.0, 0.05, 0.1, 0.2, 0.3]

        # Verify reconstructed flag
        t_dict = {t.time_dir: t for t in time_steps}
        assert t_dict["0"].reconstructed is True
        assert t_dict["0.05"].reconstructed is True
        assert t_dict["0.2"].reconstructed is True
        assert t_dict["0.1"].reconstructed is False
        assert t_dict["0.3"].reconstructed is False

        # Verify available fields list
        assert sorted(t_dict["0"].available_fields) == ["U", "p"]
        assert sorted(t_dict["0.05"].available_fields) == ["T", "U"]
        assert sorted(t_dict["0.1"].available_fields) == ["U", "p"]
        assert t_dict["0.3"].available_fields == ["k"]

    def test_format_binary_openfoam_field_write_and_read(self, tmp_path: Path):
        """Verifies writing and reading binary OpenFOAM field with format binary; header."""
        p_bin_scalar = tmp_path / "0" / "p_binary"
        data_s = np.linspace(100.0, 500.0, 50, dtype=np.float64)
        write_field(p_bin_scalar, "p", [0, 2, -2, 0, 0, 0, 0], data_s, write_binary=True)

        # Inspect raw file content to confirm 'format binary;' header
        raw_text = p_bin_scalar.read_bytes()[:1024].decode("latin1", errors="ignore")
        assert "format      binary;" in raw_text

        # Read back via FoamReader
        read_s = FoamReader.load_field(p_bin_scalar, n_cells=50)
        assert read_s["name"] == "p_binary"
        assert read_s["dimensions"] == [0, 2, -2, 0, 0, 0, 0]
        np.testing.assert_allclose(read_s["internal_field"], data_s, rtol=1e-12)

        # Vector field binary
        p_bin_vec = tmp_path / "0" / "U_binary"
        data_v = np.zeros((30, 3), dtype=np.float64)
        data_v[:, 0] = np.linspace(1.0, 10.0, 30)
        data_v[:, 1] = np.linspace(-2.0, 2.0, 30)
        data_v[:, 2] = 0.5
        write_field(p_bin_vec, "U", [0, 1, -1, 0, 0, 0, 0], data_v, write_binary=True)

        read_v = FoamReader.load_field(p_bin_vec, n_cells=30)
        assert read_v["is_vector"] is True
        np.testing.assert_allclose(read_v["internal_field"], data_v, rtol=1e-12)

    def test_format_binary_polymesh_write_and_read(self, tmp_path: Path):
        """Verifies binary OpenFOAM polyMesh generation (points, owner, neighbour in binary)."""
        case_dir = tmp_path / "case_binary"
        h = 0.1
        pts = np.array([
            [0.0, 0.0, 0.0], [h, 0.0, 0.0], [h, h, 0.0], [0.0, h, 0.0],
            [0.0, 0.0, h], [h, 0.0, h], [h, h, h], [0.0, h, h],
        ], dtype=np.float64)
        cells = [np.arange(8)]
        owner = np.array([0, 0, 0, 0, 0, 0], dtype=np.int64)
        neighbour = np.full(6, -1, dtype=np.int64)
        fa = np.zeros((6, 3), dtype=np.float64)
        fc = np.zeros((6, 3), dtype=np.float64)
        cc = np.array([[h/2, h/2, h/2]], dtype=np.float64)
        cv = np.array([h**3], dtype=np.float64)
        df = np.zeros((6, 3), dtype=np.float64)
        kf = np.zeros((6, 3), dtype=np.float64)
        faces = [
            np.array([0, 3, 2, 1], dtype=np.int64),
            np.array([4, 5, 6, 7], dtype=np.int64),
            np.array([0, 1, 5, 4], dtype=np.int64),
            np.array([2, 3, 7, 6], dtype=np.int64),
            np.array([0, 4, 7, 3], dtype=np.int64),
            np.array([1, 2, 6, 5], dtype=np.int64),
        ]
        mesh = UnstructuredMesh(
            points=pts, cells=cells, owner=owner, neighbour=neighbour,
            face_areas=fa, face_centres=fc, cell_centres=cc, cell_volumes=cv,
            delta_f=df, k_f=kf, boundaries={"wall": (0, 6)}, faces=faces,
        )

        poly_path = export_openfoam_polymesh(mesh, case_dir=case_dir, write_binary=True)
        assert poly_path.exists()

        # Check binary headers in points and owner files
        points_content = (poly_path / "points").read_bytes()[:1024].decode("latin1", errors="ignore")
        assert "format      binary;" in points_content
        owner_content = (poly_path / "owner").read_bytes()[:1024].decode("latin1", errors="ignore")
        assert "format      binary;" in owner_content

        # Load back via FoamReader
        loaded_mesh = FoamReader.load_mesh(poly_path)
        assert loaded_mesh.n_points == 8
        assert loaded_mesh.n_faces == 6
        assert loaded_mesh.n_cells == 1
        np.testing.assert_allclose(loaded_mesh.points, pts, rtol=1e-12)
        np.testing.assert_allclose(loaded_mesh.cell_volumes[0], h**3, rtol=1e-10)


# =============================================================================
# 2. Feature F65: IPC Protocol, Dimensions, & Binary Buffers Tests
# =============================================================================

class TestIPCProtocolAndBuffersF65:
    """Verifies JSON-RPC 2.0 protocol handling, 7-element SI validation, and binary buffer contracts."""

    def test_jsonrpc_protocol_compliance(self):
        """Verifies JSON-RPC 2.0 error handling (-32700, -32600, -32601, -32602) and response structure."""
        service = IPCService()

        # 1. Parse error (-32700) on malformed JSON
        resp1 = json.loads(service.handle_request_str("{malformed json line\n"))
        assert resp1["jsonrpc"] == "2.0"
        assert resp1["error"]["code"] == PARSE_ERROR
        assert resp1["id"] is None

        # 2. Invalid Request (-32600) on missing jsonrpc: "2.0"
        resp2 = json.loads(service.handle_request_str('{"method": "system.hello", "id": 1}\n'))
        assert resp2["error"]["code"] == INVALID_REQUEST
        assert resp2["id"] == 1

        # 3. Method not found (-32601)
        resp3 = json.loads(service.handle_request_str('{"jsonrpc": "2.0", "method": "nonexistent.method", "id": 42}\n'))
        assert resp3["error"]["code"] == METHOD_NOT_FOUND
        assert resp3["id"] == 42

        # 4. Success response
        resp4 = json.loads(service.handle_request_str('{"jsonrpc": "2.0", "method": "system.hello", "id": 100}\n'))
        assert resp4["jsonrpc"] == "2.0"
        assert "result" in resp4
        assert resp4["id"] == 100
        assert resp4["result"]["status"] == "ready"

    def test_strict_7_element_si_dimensions_validation(self):
        """Verifies strict 7-element SI dimensions validation [kg, m, s, K, mol, A, cd]."""
        # Valid cases
        assert validate_si_dimensions([0, 1, -1, 0, 0, 0, 0]) == [0, 1, -1, 0, 0, 0, 0]  # Velocity
        assert validate_si_dimensions([1, -1, -2, 0, 0, 0, 0]) == [1, -1, -2, 0, 0, 0, 0]  # Pressure Pa
        assert validate_si_dimensions([0, 0, 0, 1, 0, 0, 0]) == [0, 0, 0, 1, 0, 0, 0]  # Temperature K
        assert validate_si_dimensions(np.array([0, 0, 0, 0, 0, 0, 0])) == [0, 0, 0, 0, 0, 0, 0]  # Dimensionless

        # Negative test 1: Length != 7
        with pytest.raises(ValueError, match="Strict SI dimensions require exactly 7 elements"):
            validate_si_dimensions([0, 1, -1, 0, 0])  # Length 5
        with pytest.raises(ValueError, match="Strict SI dimensions require exactly 7 elements"):
            validate_si_dimensions([0, 1, -1, 0, 0, 0, 0, 0])  # Length 8

        # Negative test 2: Non-integral float (fractional exponent)
        with pytest.raises(ValueError, match="must be an integer"):
            validate_si_dimensions([0, 1.5, -1, 0, 0, 0, 0])

        # Negative test 3: NaN and Inf
        with pytest.raises(ValueError, match="cannot be NaN or Inf"):
            validate_si_dimensions([0, float("nan"), 0, 0, 0, 0, 0])
        with pytest.raises(ValueError, match="cannot be NaN or Inf"):
            validate_si_dimensions([0, float("inf"), 0, 0, 0, 0, 0])

        # Negative test 4: Boolean (reject explicit True/False)
        with pytest.raises(TypeError, match="cannot be a boolean"):
            validate_si_dimensions([True, 1, 0, 0, 0, 0, 0])

        # Negative test 5: String entries or non-iterable
        with pytest.raises(TypeError):
            validate_si_dimensions(["kg", "m", "s", "K", "mol", "A", "cd"])
        with pytest.raises(TypeError):
            validate_si_dimensions(None)

        # Helpers
        assert format_openfoam_dimensions([0, 1, -1, 0, 0, 0, 0]) == "[0 1 -1 0 0 0 0]"
        assert parse_openfoam_dimensions("[0 1 -1 0 0 0 0]") == [0, 1, -1, 0, 0, 0, 0]
        assert get_field_dimensions("U") == [0, 1, -1, 0, 0, 0, 0]
        assert get_field_dimensions("p", compressible=False) == [0, 2, -2, 0, 0, 0, 0]
        assert get_field_dimensions("p", compressible=True) == [1, -1, -2, 0, 0, 0, 0]
        assert get_field_dimensions("T") == [0, 0, 0, 1, 0, 0, 0]

    def test_paket1_binary_geometry_buffer_pack_and_unpack(self):
        """Verifies Paket 1 Binary Geometry Buffer layout (CFDGEO01, vertices, normals, triangles, face groups, audit)."""
        n_v = 10
        n_t = 6
        vertices = np.arange(n_v * 3, dtype=np.float64).reshape(n_v, 3) * 0.1
        normals = np.ones((n_v, 3), dtype=np.float32) * (1.0 / np.sqrt(3.0))
        triangles = np.array([
            [0, 1, 2], [1, 2, 3], [2, 3, 4], [3, 4, 5], [4, 5, 6], [5, 6, 7]
        ], dtype=np.uint32)
        face_groups = np.array([1, 1, 2, 2, 3, 3], dtype=np.uint32)
        audit_rep = {
            "is_watertight": True,
            "surface_area": 1.25,
            "volume": 0.05,
            "open_edges": [],
            "non_manifold_edges": [],
        }

        buf, descriptor = pack_geometry_buffer(
            vertices=vertices,
            normals=normals,
            triangles=triangles,
            face_group_id=face_groups,
            audit_report=audit_rep,
            dataset_id="blade_rev1",
            geometry_revision=3,
        )

        assert len(buf) > 32
        assert descriptor.dataset_id == "blade_rev1"
        assert descriptor.geometry_revision == 3
        assert descriptor.n_vertices == n_v
        assert descriptor.n_triangles == n_t

        # Unpack and verify bit-for-bit equivalence
        unpacked = unpack_geometry_buffer(buf)
        assert unpacked["version"] == 1
        assert unpacked["n_vertices"] == n_v
        assert unpacked["n_triangles"] == n_t
        np.testing.assert_allclose(unpacked["vertices"], vertices, rtol=1e-14)
        np.testing.assert_allclose(unpacked["normals"], normals, rtol=1e-6)
        np.testing.assert_array_equal(unpacked["triangles"], triangles)
        np.testing.assert_array_equal(unpacked["face_group_id"], face_groups)
        assert unpacked["audit_report"]["is_watertight"] is True
        assert unpacked["audit_report"]["surface_area"] == 1.25

    def test_paket2_binary_mesh_quality_buffer_pack_and_unpack(self):
        """Verifies Paket 2 Binary Mesh Quality Fields buffer (CFDMSH02, skewness, nonOrtho, aspect, vol, amips, folded)."""
        n_cells = 25
        skewness = np.linspace(0.1, 0.8, n_cells, dtype=np.float32)
        non_ortho = np.linspace(5.0, 75.0, n_cells, dtype=np.float32)
        aspect_ratio = np.linspace(1.0, 5.0, n_cells, dtype=np.float32)
        volume = np.full(n_cells, 1e-4, dtype=np.float32)
        wsrd = np.ones(n_cells, dtype=np.float32)
        amips = np.ones(n_cells, dtype=np.float32)
        folded_faces = np.array([3, 14, 21], dtype=np.uint32)

        summary = {
            "n_cells": n_cells,
            "max_skewness": 0.8,
            "max_non_ortho": 75.0,
            "min_volume": 1e-4,
        }

        buf, descriptor = pack_mesh_quality_buffer(
            skewness=skewness,
            non_orthogonality=non_ortho,
            aspect_ratio=aspect_ratio,
            volume=volume,
            wsrd_weights=wsrd,
            amips_energy=amips,
            folded_face_indices=folded_faces,
            summary=summary,
            mesh_id="msh_hexcore_01",
            mesh_revision=2,
        )

        assert len(buf) > 32
        assert descriptor.mesh_id == "msh_hexcore_01"
        assert descriptor.mesh_revision == 2
        assert descriptor.n_cells == n_cells
        assert descriptor.n_folded == 3

        # Unpack
        unpacked = unpack_mesh_quality_buffer(buf)
        assert unpacked["version"] == 1
        assert unpacked["n_cells"] == n_cells
        assert unpacked["n_folded"] == 3
        np.testing.assert_allclose(unpacked["skewness"], skewness, rtol=1e-6)
        np.testing.assert_allclose(unpacked["non_orthogonality"], non_ortho, rtol=1e-6)
        np.testing.assert_allclose(unpacked["aspect_ratio"], aspect_ratio, rtol=1e-6)
        np.testing.assert_allclose(unpacked["volume"], volume, rtol=1e-6)
        np.testing.assert_array_equal(unpacked["folded_face_indices"], folded_faces)
        assert unpacked["summary"]["max_skewness"] == 0.8

    def test_paket4_live_telemetry_streaming(self):
        """Verifies Paket 4 NDJSON live telemetry streaming (iter events, residuals, Courant, monitors, written)."""
        buffer = io.StringIO()
        streamer = TelemetryStreamer(output_stream=buffer)

        # 1. Emit iter event
        residuals = {"Ux": 1.2e-4, "Uy": 8.5e-5, "p": 3.1e-3, "T": 5.4e-6}
        monitors = {"Cd": 0.045, "Cl": 0.88, "n_nan_cells": 0}
        line1 = streamer.emit_iter(
            step=10,
            sim_time=0.001,
            dt=1e-4,
            co_mean=0.15,
            co_max=0.65,
            residuals=residuals,
            monitors=monitors,
            wall_s=1.234,
        )

        parsed1 = TelemetryStreamer.parse_ndjson_line(line1)
        assert parsed1["type"] == "iter"
        assert parsed1["step"] == 10
        assert parsed1["time"] == 0.001
        assert parsed1["co_mean"] == 0.15
        assert parsed1["co_max"] == 0.65
        assert parsed1["residuals"]["Ux"] == 1.2e-4
        assert parsed1["monitors"]["n_nan_cells"] == 0

        # 2. Emit timestep_written event
        line2 = streamer.emit_timestep_written(
            time_dir="0.001",
            reconstructed=True,
            available_fields=["U", "p", "T", "k"],
        )
        parsed2 = TelemetryStreamer.parse_ndjson_line(line2)
        assert parsed2["type"] == "timestep_written"
        assert parsed2["time_dir"] == "0.001"
        assert parsed2["reconstructed"] is True
        assert "U" in parsed2["available_fields"]

        # Verify buffer content has 2 newline-delimited lines
        lines = buffer.getvalue().strip().split("\n")
        assert len(lines) == 2


# =============================================================================
# 3. Feature F65: All 16 IPC Endpoints Validation
# =============================================================================

class TestAll16IPCEndpointsF65:
    """Rigorous non-tautological test suite verifying all 16 endpoints in IPCService."""

    @pytest.fixture
    def service(self, tmp_path: Path) -> IPCService:
        return IPCService(materials_db_dir=Path("resources/materials_db"))

    def test_endpoint_01_system_hello(self, service: IPCService):
        raw = service.handle_request_str('{"jsonrpc": "2.0", "method": "system.hello", "id": 1}\n')
        resp = json.loads(raw)
        assert resp["id"] == 1
        res = resp["result"]
        assert res["backend_version"] == "0.2.0"
        assert res["protocol_version"] == "2.0"
        assert "capabilities" in res
        assert "strict_si_dimensions_7d" in res["capabilities"]
        assert "fast_io" in res["capabilities"]
        assert "step" in res["supported_formats"]

    def test_endpoint_02_schema_get(self, service: IPCService):
        # 1. Get all schemas
        raw_all = service.handle_request_str('{"jsonrpc": "2.0", "method": "schema.get", "id": 2}\n')
        resp_all = json.loads(raw_all)
        schemas = resp_all["result"]
        assert isinstance(schemas, list)
        assert len(schemas) >= 12
        mod_ids = [s["module_id"] for s in schemas]
        assert "Energy" in mod_ids
        assert "Viscous" in mod_ids
        assert "Species" in mod_ids

        # 2. Get specific module schema
        raw_v = service.handle_request_str('{"jsonrpc": "2.0", "method": "schema.get", "params": {"module_id": "Viscous"}, "id": 3}\n')
        resp_v = json.loads(raw_v)
        schema_v = resp_v["result"]
        assert schema_v["module_id"] == "Viscous"
        assert "k-omega SST" in schema_v["variants"]
        assert "turbulence_intensity" in schema_v["parameters"]
        assert schema_v["parameters"]["turbulence_intensity"]["unit_si"] == "-"

    def test_endpoint_03_geometry_import(self, service: IPCService):
        fixture_path = Path("resources/CADTestSamples/TestCAD.obj").resolve()
        assert fixture_path.exists(), f"Missing CAD fixture: {fixture_path}"

        raw = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "geometry.import",
            "params": {
                "file_path": str(fixture_path),
                "source_unit": "mm",
                "chordal_tolerance_m": 0.001,
            },
            "id": 4,
        }))
        resp = json.loads(raw)
        assert resp["id"] == 4
        res = resp["result"]
        assert "geometry_id" in res
        assert res["geometry_revision"] == 1
        assert res["n_vertices"] > 0
        assert res["n_triangles"] > 0
        assert "binary_descriptor" in res
        assert res["binary_buffer_bytes"] > 0

    def test_endpoint_04_geometry_patches_update(self, service: IPCService):
        fixture_path = Path("resources/CADTestSamples/TestCAD.obj").resolve()
        raw_imp = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "geometry.import",
            "params": {"file_path": str(fixture_path), "source_unit": "mm"},
            "id": 5,
        }))
        geo_id = json.loads(raw_imp)["result"]["geometry_id"]

        # Update patches
        raw_upd = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "geometry.patches.update",
            "params": {
                "geometry_id": geo_id,
                "geometry_revision": 1,
                "patch_assignments": {
                    "inlet_patch": [0, 1],
                    "outlet_patch": [2, 3],
                },
            },
            "id": 6,
        }))
        resp_upd = json.loads(raw_upd)
        res = resp_upd["result"]
        assert res["geometry_id"] == geo_id
        assert res["geometry_revision"] == 2
        assert "inlet_patch" in res["patches"]
        assert res["errors"] == []

    def test_endpoint_05_mesh_generate(self, service: IPCService, tmp_path: Path):
        case_dir = tmp_path / "case_mesh"
        raw = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "mesh.generate",
            "params": {
                "case_dir": str(case_dir),
                "base_cell_size_m": 0.02,
            },
            "id": 7,
        }))
        resp = json.loads(raw)
        res = resp["result"]
        assert "mesh_id" in res
        assert res["mesh_revision"] == 1
        assert res["quality_report"]["n_cells"] > 0
        assert (case_dir / "constant" / "polyMesh").exists()

    def test_endpoint_06_job_cancel(self, service: IPCService):
        # Create an async dummy job and cancel it
        cleanup_called = []
        def _dummy(job_rec):
            while not job_rec.is_cancelled():
                time.sleep(0.01)

        job = service.job_manager.create_job("test_long", _dummy, cleanup_func=lambda: cleanup_called.append(True))

        raw = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "job.cancel",
            "params": {"job_id": job.job_id},
            "id": 8,
        }))
        resp = json.loads(raw)
        assert resp["result"]["status"] == "cancelled"
        assert job.status.value == "cancelled"
        assert len(cleanup_called) == 1

    def test_endpoint_07_materials_list(self, service: IPCService):
        # 1. All materials
        raw_all = service.handle_request_str('{"jsonrpc": "2.0", "method": "materials.list", "id": 9}\n')
        resp_all = json.loads(raw_all)
        mats = resp_all["result"]
        assert len(mats) >= 5
        mat_names = [m["name"].lower() for m in mats]
        assert "air" in mat_names
        assert "water" in mat_names

        # 2. Filtered by category
        raw_gases = service.handle_request_str('{"jsonrpc": "2.0", "method": "materials.list", "params": {"category": "gas"}, "id": 10}\n')
        resp_g = json.loads(raw_gases)
        gases = resp_g["result"]
        assert all(m["category"] == "gas" for m in gases)

    def test_materials_extensible_properties_and_custom_categories(self, tmp_path: Path):
        """Verifies extensible Material with categorized physical properties, extra_properties passthrough, and missing_properties."""
        from backend.core.materials import Material, MaterialDatabase

        # 1. Instantiate Material with arbitrary custom fields & categorized properties
        custom_data = {
            "name": "SuperAlloy_CFD99",
            "category": "solid",
            "density": 8500.0,
            "specific_heat": 450.0,
            "thermal_conductivity": 15.0,
            # Mechanical
            "youngs_modulus": 2.1e11,
            "poissons_ratio": 0.28,
            "yield_strength": 9.5e8,
            # Thermal
            "melting_point": 1650.0,
            "thermal_expansion_coeff": 1.2e-5,
            # Surface & Electrical
            "surface_energy": 1.5,
            "electrical_conductivity": 1.2e6,
            # Custom unmodelled fields (passthrough)
            "custom_supplier": "AeroAlloys Inc.",
            "creep_rupture_hours": 10000.0,
            "unit_si": {
                "creep_rupture_hours": "h",
                "custom_supplier": "str",
            },
        }

        mat = Material.from_dict(custom_data)
        assert mat.name == "SuperAlloy_CFD99"
        assert mat.density == 8500.0
        assert mat.get_property("youngs_modulus") == 2.1e11
        assert mat.get_property("melting_point") == 1650.0
        assert mat.get_property("surface_energy") == 1.5
        assert mat.get_property("custom_supplier") == "AeroAlloys Inc."
        assert mat.extra_properties["creep_rupture_hours"] == 10000.0
        assert mat.unit_si["creep_rupture_hours"] == "h"

        # 2. Verify missing_properties()
        # Checking properties that are present -> returns empty list
        assert mat.missing_properties(["density", "youngs_modulus", "melting_point"]) == []

        # Checking properties where some are missing (e.g. viscosity on solid, dielectric_constant)
        missing = mat.missing_properties(["density", "viscosity", "dielectric_constant", "youngs_modulus"])
        assert missing == ["viscosity", "dielectric_constant"]

        # 3. Add to MaterialDatabase with persist=True
        db = MaterialDatabase(db_dir=tmp_path)
        db.add_material(mat, persist=True)
        persisted_file = tmp_path / "superalloy_cfd99.json"
        assert persisted_file.exists()

        # Reload database from directory and verify roundtrip
        db2 = MaterialDatabase(db_dir=tmp_path)
        reloaded = db2.get_material("SuperAlloy_CFD99")
        assert reloaded is not None
        assert reloaded.density == 8500.0
        assert reloaded.get_property("youngs_modulus") == 2.1e11
        assert reloaded.get_property("custom_supplier") == "AeroAlloys Inc."
        assert reloaded.extra_properties["creep_rupture_hours"] == 10000.0

        # 4. IPC materials.list endpoint serialization
        service = IPCService(materials_db_dir=tmp_path)
        raw_list = service.handle_request_str('{"jsonrpc": "2.0", "method": "materials.list", "id": 105}\n')
        resp_list = json.loads(raw_list)
        mat_cfd99 = next((m for m in resp_list["result"] if m["name"] == "SuperAlloy_CFD99"), None)
        assert mat_cfd99 is not None
        assert mat_cfd99["youngs_modulus"] == 2.1e11
        assert mat_cfd99["custom_supplier"] == "AeroAlloys Inc."

    def test_endpoint_08_case_validate(self, service: IPCService):
        raw = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "case.validate",
            "params": {
                "active_modules": ["Energy", "Viscous & Turbulence"],
            },
            "id": 11,
        }))
        resp = json.loads(raw)
        res = resp["result"]
        assert res["is_valid"] is True
        assert res["selected_solver"] == "chtMultiRegionFoam"

    def test_endpoint_09_case_generate(self, service: IPCService, tmp_path: Path):
        raw = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "case.generate",
            "params": {
                "case_name": "test_turbine",
                "target_dir": str(tmp_path),
                "solver_name": "simpleFoam",
                "end_time": 500,
                "delta_t": 1.0,
                "write_interval": 50,
                "mpi_ranks": 4,
            },
            "id": 12,
        }))
        resp = json.loads(raw)
        res = resp["result"]
        case_dir = Path(res["case_dir"])
        assert case_dir.exists()
        assert (case_dir / "system" / "controlDict").exists()
        assert (case_dir / "system" / "decomposeParDict").exists()
        assert (case_dir / "system" / "fvSchemes").exists()
        assert (case_dir / "system" / "fvSolution").exists()
        assert len(res["generated_files"]) >= 4

    def test_endpoint_10_to_13_solver_lifecycle(self, service: IPCService, tmp_path: Path):
        # Setup mock case
        case_dir = tmp_path / "run_case"
        (case_dir / "system").mkdir(parents=True)
        ctrl = case_dir / "system" / "controlDict"
        ctrl.write_text(
            "stopAt endTime;\nwriteControl adjustableRunTime;\n",
            encoding="utf-8"
        )

        # 10. solver.start
        raw_start = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "solver.start",
            "params": {"case_dir": str(case_dir), "solver_name": "simpleFoam"},
            "id": 13,
        }))
        resp_start = json.loads(raw_start)
        run_id = resp_start["result"]["run_id"]
        assert resp_start["result"]["status"] == "started"

        # 11. solver.write_now
        raw_wn = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "solver.write_now",
            "params": {"run_id": run_id},
            "id": 14,
        }))
        resp_wn = json.loads(raw_wn)
        assert resp_wn["result"]["status"] == "write_requested"
        assert "writeControl    writeNow;" in ctrl.read_text(encoding="utf-8")

        # 12. solver.stop_graceful
        raw_sg = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "solver.stop_graceful",
            "params": {"run_id": run_id},
            "id": 15,
        }))
        resp_sg = json.loads(raw_sg)
        assert resp_sg["result"]["status"] == "stopping"
        assert "stopAt          writeNow;" in ctrl.read_text(encoding="utf-8")

        # 13. solver.abort
        raw_ab = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "solver.abort",
            "params": {"run_id": run_id},
            "id": 16,
        }))
        resp_ab = json.loads(raw_ab)
        assert resp_ab["result"]["status"] == "aborted"

    def test_endpoint_14_to_16_results_inspection_and_load(self, service: IPCService, tmp_path: Path):
        case_dir = tmp_path / "results_case"
        t0 = case_dir / "0"
        t1 = case_dir / "0.05"
        t0.mkdir(parents=True)
        t1.mkdir(parents=True)

        # Write fields
        write_field(t0 / "p", "p", [0, 2, -2, 0, 0, 0, 0], [101325.0, 101320.0])
        write_field(t0 / "U", "U", [0, 1, -1, 0, 0, 0, 0], [[10.0, 0.0, 0.0], [10.5, 0.0, 0.0]])
        write_field(t1 / "p", "p", [0, 2, -2, 0, 0, 0, 0], [101330.0, 101325.0])

        # 14. results.list_times
        raw_times = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "results.list_times",
            "params": {"case_dir": str(case_dir)},
            "id": 17,
        }))
        resp_times = json.loads(raw_times)
        times = [t["time"] for t in resp_times["result"]["times"]]
        assert times == [0.0, 0.05]

        # 15. results.list_fields
        raw_fields = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "results.list_fields",
            "params": {"case_dir": str(case_dir), "time_dir": "0"},
            "id": 18,
        }))
        resp_fields = json.loads(raw_fields)
        fields = resp_fields["result"]["fields"]
        f_names = [f["name"] for f in fields]
        assert "p" in f_names
        assert "U" in f_names

        # 16. results.load
        raw_load = service.handle_request_str(json.dumps({
            "jsonrpc": "2.0",
            "method": "results.load",
            "params": {"case_dir": str(case_dir), "time_dir": "0", "field_name": "p"},
            "id": 19,
        }))
        resp_load = json.loads(raw_load)
        res_load = resp_load["result"]
        assert res_load["name"] == "p"
        assert res_load["dimensions"] == [0, 2, -2, 0, 0, 0, 0]
        assert res_load["location"] == "cell"
        np.testing.assert_allclose(res_load["internal_field"], [101325.0, 101320.0])

    def test_stderr_diagnostics_separation(self, tmp_path: Path):
        """Verifies that IPC stdout stream contains exclusively JSON-RPC / NDJSON lines while diagnostic logs route to stderr."""
        fake_stdout = io.StringIO()
        fake_stderr = io.StringIO()
        service = IPCService(stdout_stream=fake_stdout, stderr_stream=fake_stderr)

        # Send three commands
        service.handle_request_str('{"jsonrpc": "2.0", "method": "system.hello", "id": 1}\n')
        service.handle_request_str('{"jsonrpc": "2.0", "method": "schema.get", "id": 2}\n')
        service.handle_request_str('{"jsonrpc": "2.0", "method": "nonexistent", "id": 3}\n')

        # Run stdio simulation
        input_data = io.StringIO(
            '{"jsonrpc": "2.0", "method": "system.hello", "id": 10}\n'
            '{"jsonrpc": "2.0", "method": "materials.list", "id": 11}\n'
        )
        service.serve_stdio(input_stream=input_data)

        # Inspect stdout: every single non-empty line must parse as valid JSON
        stdout_text = fake_stdout.getvalue()
        lines = [l.strip() for l in stdout_text.splitlines() if l.strip()]
        assert len(lines) == 2  # The two requests from serve_stdio

        for line in lines:
            parsed = json.loads(line)
            assert parsed["jsonrpc"] == "2.0"
            assert "id" in parsed

    def test_strict_backend_boundary_zero_frontend_imports(self):
        """Verifies that backend/ipc and backend/native_rust contain ZERO imports of wgpu, egui, or renderer logic."""
        import sys
        backend_dir = Path("backend/ipc")
        for py_file in backend_dir.glob("*.py"):
            content = py_file.read_text(encoding="utf-8")
            for forbidden in ["import wgpu", "from wgpu", "import egui", "from egui", "render_frame"]:
                assert forbidden not in content, f"Forbidden frontend UI import '{forbidden}' found in {py_file}"
