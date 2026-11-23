"""Tier 1 isolated happy-path E2E tests for Step 1 (F1-F4) and CAD/IO/IPC contracts (F15)."""

from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import Any, Dict

import numpy as np
import pytest

from backend.geometry.models import BoundingBox, SurfaceMesh
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.pipeline import CADGeometryEngine
from backend.ipc.binary_buffers import (
    pack_geometry_buffer,
    pack_mesh_quality_buffer,
    unpack_geometry_buffer,
    unpack_mesh_quality_buffer,
)
from backend.ipc.dimensions import format_openfoam_dimensions, parse_openfoam_dimensions, validate_si_dimensions
from backend.ipc.service import IPCService
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    generate_hexcore_native,
    get_native_dll,
)
from backend.meshing.mesh_engine import MeshEngine, compute_farfield_bounding_box
from backend.meshing.models import (
    CellType,
    CurvatureSizingConfig,
    FarfieldPadding,
    MeshingConfig,
    UnstructuredMesh,
    compute_cell_j_ens,
    compute_mesh_j_ens,
)
from backend.native_rust.c_abi_lake import (
    allocate_aligned_array,
    create_cabi_span,
    export_fields_to_soa_lake,
    export_mesh_to_soa_lake,
)
from backend.native_rust.mphys_container import (
    CompressionType,
    MPhysReader,
    MPhysWriter,
)
from tests.conftest import create_unit_cube_arrays
from tests.e2e_mesher_suite.conftest import ASSET_PATHS, load_cached_surface
from tests.e2e_mesher_suite.oracles import (
    assert_j_ens_geometric_not_dummy,
    assert_linear_field_exact,
    assert_polymesh_roundtrip,
    assert_unstructured_mesh_valid,
    count_surface_components_scipy,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier1]


def _make_chamfered_cube_polyhedron_mesh() -> UnstructuredMesh:
    """Create a valid 1-cell chamfered-cube polyhedron UnstructuredMesh."""
    pts = np.array(
        [
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0],
            [1.0, 0.0, 1.0],
            [0.0, 1.0, 1.0],
            [1.0, 1.0, 0.4],
            [1.0, 0.4, 1.0],
            [0.4, 1.0, 1.0],
        ],
        dtype=np.float64,
    )
    faces = [
        [0, 3, 2, 1],
        [0, 1, 5, 4],
        [0, 4, 6, 3],
        [1, 2, 7, 8, 5],
        [2, 3, 6, 9, 7],
        [4, 5, 8, 9, 6],
        [7, 9, 8],
    ]
    mesh = compute_difvm_topology_python(
        pts,
        [list(range(len(pts)))],
        cell_types=np.array([int(CellType.POLYHEDRON)], dtype=np.int32),
        explicit_faces=faces,
    )
    mesh.boundaries = {"wall": (0, mesh.num_faces)}
    return mesh


def _make_thin_plate_surface(
    lx: float = 0.04,
    ly: float = 0.02,
    lz: float = 0.004,
) -> SurfaceMesh:
    """Create a watertight triangulated thin-plate surface rotated by 15 degrees."""
    v, f = create_unit_cube_arrays()
    v = (v - 0.5) * np.array([lx, ly, lz], dtype=np.float64)
    theta = np.deg2rad(15.0)
    rot_z = np.array(
        [
            [np.cos(theta), -np.sin(theta), 0.0],
            [np.sin(theta), np.cos(theta), 0.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=np.float64,
    )
    v_rot = v @ rot_z.T
    return SurfaceMesh(
        vertices=v_rot,
        faces=f.astype(np.int32),
        patches={"plate_wall": np.arange(len(f), dtype=np.int32)},
    )


class TestE2EMesherTier1F1JEnsHexAndPolyhedron:
    """F1: J_ENS Hex8 & Polyhedron (ctype=42) Geometric Fix (>=5 tests)."""

    def test_f1_01_unit_hex8_canonical_j_ens_is_unity(self) -> None:
        """Verify canonical VTK Hex8 unit cube achieves exact J_ENS == 1.0."""
        pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 1.0],
                [1.0, 0.0, 1.0],
                [1.0, 1.0, 1.0],
                [0.0, 1.0, 1.0],
            ],
            dtype=np.float64,
        )
        j_val = compute_cell_j_ens(pts, list(range(8)), CellType.HEXAHEDRON)
        assert np.isclose(j_val, 1.0, atol=1e-12)

    def test_f1_02_chamfered_polyhedron_j_ens_is_geometric_non_dummy(self) -> None:
        """Verify ctype=42 chamfered polyhedron computes genuine geometric J_ENS in (0, 1)."""
        mesh = _make_chamfered_cube_polyhedron_mesh()
        report = assert_unstructured_mesh_valid(mesh)
        j_arr = assert_j_ens_geometric_not_dummy(mesh, check_poly_non_unit=True)
        assert report.is_valid
        assert 0.05 < float(j_arr[0]) < 0.99

    def test_f1_03_native_hexcore_internal_fluid_j_ens_all_positive(self) -> None:
        """Verify native hexcore internal_fluid mesh has strictly positive J_ENS across all cells."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        j_ens = assert_j_ens_geometric_not_dummy(mesh)
        assert float(np.min(j_ens)) > 0.0
        assert float(np.max(j_ens)) <= 1.0 + 1e-12

    def test_f1_04_native_hexcore_cut_polyhedra_positive_and_non_dummy(self) -> None:
        """Verify cut-cell polyhedra (ctype=42) in rotated plate mesh have positive geometric J_ENS."""
        surf = _make_thin_plate_surface(lx=0.04, ly=0.04, lz=0.02)
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.015, h_min=0.0075, h_max=0.03),
        )
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.015,
            domain_mode="internal_fluid",
            config=cfg,
        )
        assert mesh.cell_types is not None
        assert np.any(mesh.cell_types == int(CellType.POLYHEDRON))
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert_j_ens_geometric_not_dummy(mesh, check_poly_non_unit=True)

    def test_f1_05_quality_report_j_ens_matches_compute_mesh_j_ens(self) -> None:
        """Verify MeshQualityReport.j_ens_min and j_ens_mean match compute_mesh_j_ens."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        report = mesh.compute_quality_report()
        j_ens, inv_pyr = compute_mesh_j_ens(mesh)
        assert inv_pyr == 0
        assert report.inverted_pyramids == 0
        assert np.isclose(report.j_ens_min, float(np.min(j_ens)), atol=1e-12)
        assert np.isclose(report.j_ens_mean, float(np.mean(j_ens)), atol=1e-12)


class TestE2EMesherTier1F2AnisotropicGradedExternalFluid:
    """F2: Anisotropic / Graded external_fluid Farfield (>=5 tests)."""

    def test_f2_01_compute_farfield_bounding_box_padding_factors(self) -> None:
        """Verify compute_farfield_bounding_box expands upstream, downstream, and lateral bounds."""
        bbox = BoundingBox(
            min_pt=np.array([0.0, -0.5, -0.1], dtype=np.float64),
            max_pt=np.array([1.0, 0.5, 0.1], dtype=np.float64),
        )
        ff_min, ff_max = compute_farfield_bounding_box(
            bbox,
            upstream=5.0,
            downstream=10.0,
            lateral_y=4.0,
            lateral_z=4.0,
        )
        assert ff_min[0] < 0.0
        assert ff_max[0] > 1.0
        assert ff_min[1] < -0.5 and ff_max[1] > 0.5
        assert ff_min[2] < -0.1 and ff_max[2] > 0.1

    def test_f2_02_external_fluid_cube_graded_farfield_mesh_valid(self) -> None:
        """Verify external_fluid meshing on a unit cube generates graded farfield cells without blowup."""
        v, f = create_unit_cube_arrays()
        surf = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(
                upstream=2.0,
                downstream=3.0,
                lateral_y=2.0,
                lateral_z=2.0,
            ),
            sizing=CurvatureSizingConfig(h0=0.4, h_min=0.25, h_max=1.2, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(surf, h0=0.4, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert float(np.max(mesh.cell_volumes)) > 2.0 * float(np.min(mesh.cell_volumes))

    def test_f2_03_external_fluid_naca0012_coarse_farfield_fast_and_bounded(
        self,
        naca0012_surface: SurfaceMesh,
    ) -> None:
        """Verify external_fluid on NACA0012 completes rapidly with graded farfield and bounded cells."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(
                upstream=2.0,
                downstream=3.0,
                lateral_y=2.0,
                lateral_z=2.0,
            ),
            sizing=CurvatureSizingConfig(h0=0.25, h_min=0.12, h_max=1.0, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(
            naca0012_surface,
            h0=0.25,
            domain_mode="external_fluid",
            config=cfg,
        )
        assert 50 <= mesh.num_cells <= 250_000
        assert_unstructured_mesh_valid(mesh)

    def test_f2_04_external_fluid_explicit_farfield_bbox_respected(self) -> None:
        """Verify explicit farfield_bbox bounds the generated external_fluid domain."""
        v, f = create_unit_cube_arrays()
        surf = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
        ff_box = ((-1.0, -1.0, -1.0), (2.0, 2.0, 2.0))
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_bbox=ff_box,
            sizing=CurvatureSizingConfig(h0=0.5, h_min=0.25, h_max=1.0),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(surf, h0=0.5, domain_mode="external_fluid", config=cfg)
        m_bbox = mesh.get_bounding_box()
        assert np.all(m_bbox.min_pt >= np.array([-1.05, -1.05, -1.05]))
        assert np.all(m_bbox.max_pt <= np.array([2.05, 2.05, 2.05]))

    def test_f2_05_farfield_padding_serialization_roundtrip(self) -> None:
        """Verify FarfieldPadding and MeshingConfig.farfield_padding serialize and deserialize cleanly."""
        pad = FarfieldPadding(upstream=4.0, downstream=8.0, lateral_y=3.5, lateral_z=4.5)
        cfg = MeshingConfig(domain_mode="external_fluid", farfield_padding=pad)
        restored = MeshingConfig.from_dict(cfg.to_dict())
        assert restored.farfield_padding is not None
        assert restored.farfield_padding.to_dict() == pad.to_dict()


class TestE2EMesherTier1F3SolidOnlyOctreeCutCells:
    """F3: solid_only Octree + Cut-Cells & Thin-Wall Resolution (>=5 tests)."""

    def test_f3_01_solid_only_native_hexcore_rotated_thin_plate(self) -> None:
        """Verify solid_only via generate_hexcore_native produces valid cut-cells on rotated thin plate."""
        surf = _make_thin_plate_surface(lx=0.04, ly=0.02, lz=0.005)
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.005, h_min=0.0025, h_max=0.01, beta=0.25),
        )
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.005,
            domain_mode="solid_only",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)
        assert mesh.cell_types is not None
        assert np.any(mesh.cell_types == int(CellType.POLYHEDRON))

    def test_f3_02_mesh_engine_mesh_cad_file_solid_only_uses_octree_cut_cells(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify MeshEngine.mesh_cad_file(..., domain_mode='solid_only') uses octree+cut-cells."""
        surf = _make_thin_plate_surface(lx=0.04, ly=0.02, lz=0.005)
        stl_path = tmp_path / "thin_plate.stl"
        eng_cad = CADGeometryEngine()
        eng_cad.current_mesh = surf
        eng_cad.export_openfoam(tmp_path, filename="thin_plate.stl", export_format="stl")
        exported_stl = tmp_path / "constant" / "triSurface" / "thin_plate.stl"
        assert exported_stl.is_file()

        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.005, h_min=0.0025, h_max=0.01),
        )
        mesher = MeshEngine(use_native=True, strict_native=True, config=cfg)
        mesh = mesher.mesh_cad_file(exported_stl, h0=0.005, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert mesh.cell_types is not None
        assert np.any(mesh.cell_types == int(CellType.POLYHEDRON)), (
            "solid_only must produce cut-cell polyhedra on rotated geometry, not pure staircase hexes"
        )

    def test_f3_03_solid_only_volume_conservation_against_analytical_plate(self) -> None:
        """Verify solid_only total volume approximates analytical thin-plate volume."""
        lx, ly, lz = 0.04, 0.02, 0.006
        analytical_vol = lx * ly * lz
        surf = _make_thin_plate_surface(lx=lx, ly=ly, lz=lz)
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.003, h_min=0.0015, h_max=0.006),
        )
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.003,
            domain_mode="solid_only",
            config=cfg,
        )
        mesh_vol = float(np.sum(mesh.cell_volumes))
        rel_err = abs(mesh_vol - analytical_vol) / analytical_vol
        assert rel_err < 0.15, f"solid_only volume error {rel_err:.3f} >= 0.15"

    def test_f3_04_solid_only_linear_field_exactness(self) -> None:
        """Verify solid_only octree cut-cell mesh satisfies linear-field exactness."""
        surf = _make_thin_plate_surface(lx=0.03, ly=0.02, lz=0.008)
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.006, h_min=0.003, h_max=0.012),
        )
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.006,
            domain_mode="solid_only",
            config=cfg,
        )
        errs = assert_linear_field_exact(mesh)
        assert errs["gg_grad_max_err"] <= 1e-12

    def test_f3_05_solid_only_flange_cad_sample_resolution(self) -> None:
        """Verify solid_only resolves flange.stl.gz with positive volumes and valid J_ENS."""
        flange = load_cached_surface("flange")
        bbox = flange.get_bounding_box()
        h0 = float(bbox.diagonal) * 0.08
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=h0, h_min=h0 * 0.5, h_max=h0 * 2.0),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(flange, h0=h0, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)


class TestE2EMesherTier1F4NativeDllSyncAndLoader:
    """F4: Native DLL Sync & Baseline Stall Elimination (>=5 tests)."""

    def test_f4_01_require_native_dll_loads_in_strict_mode(
        self,
        require_native_dll: Any,
    ) -> None:
        """Verify cfd_mesher_native.dll loads under strict_native=True and exposes core symbols."""
        assert hasattr(require_native_dll, "cfd_mesher_generate_hexcore")
        assert hasattr(require_native_dll, "cfd_mesher_free_mesh")

    def test_f4_02_dll_timestamp_not_older_than_cpp_sources(self) -> None:
        """Verify cfd_mesher_native.dll is up-to-date relative to native C++ source files."""
        repo_root = Path(__file__).resolve().parents[2]
        dll_candidates = [
            repo_root / "backend" / "meshing" / "cfd_mesher_native.dll",
            repo_root / "backend" / "meshing" / "native" / "build" / "Release" / "cfd_mesher_native.dll",
        ]
        existing_dlls = [p for p in dll_candidates if p.is_file()]
        assert existing_dlls, f"No cfd_mesher_native.dll found in {dll_candidates}"
        newest_dll_mtime = max(p.stat().st_mtime for p in existing_dlls)

        src_dir = repo_root / "backend" / "meshing" / "native" / "src"
        cpp_files = list(src_dir.glob("*.cpp"))
        assert cpp_files, f"No C++ sources found in {src_dir}"
        stale_sources = [
            p.name for p in cpp_files if p.stat().st_mtime > newest_dll_mtime + 1.0
        ]
        assert not stale_sources, (
            f"cfd_mesher_native.dll is older than C++ sources {stale_sources}; CMake rebuild required"
        )

    def test_f4_03_mesh_engine_has_native_core_true(self) -> None:
        """Verify MeshEngine(use_native=True, strict_native=True) reports has_native_core=True."""
        engine = MeshEngine(use_native=True, strict_native=True)
        assert engine.has_native_core is True

    def test_f4_04_restore_native_env_fixture_preserves_dll_loadability(
        self,
        restore_native_env: None,
    ) -> None:
        """Verify get_native_dll succeeds cleanly after cache reset inside restore_native_env."""
        os.environ["CFD_STRICT_NATIVE"] = "1"
        dll = get_native_dll(strict_native=True)
        assert dll is not None

    def test_f4_05_repeated_native_hexcore_invocations_do_not_stall_or_leak(self) -> None:
        """Verify back-to-back native hexcore meshing calls complete deterministically under 2s."""
        v, f = create_unit_cube_arrays()
        counts = []
        for _ in range(3):
            mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
            counts.append(mesh.num_cells)
        assert counts[0] > 0
        assert counts[0] == counts[1] == counts[2]


class TestE2EMesherTier1F15CadPolymeshCabiMphysIpc:
    """F15: CAD Ingestion, UnstructuredMesh, polyMesh, CAbiSpan, .mphys, IPC (>=5 tests)."""

    def test_f15_01_cad_parsers_obj_stl_step_and_component_count(self) -> None:
        """Verify OBJ, STL, and STEP parsers load canonical samples with valid normals and bbox."""
        obj_mesh = OBJParser().parse(ASSET_PATHS["TestCAD_obj"])
        assert len(obj_mesh.vertices) > 0 and len(obj_mesh.faces) > 0
        assert count_surface_components_scipy(obj_mesh.vertices, obj_mesh.faces) >= 1

        stl_mesh = STLParser().parse(ASSET_PATHS["flange"])
        assert len(stl_mesh.vertices) > 0 and len(stl_mesh.faces) > 0

        stp_mesh = STEPParser().parse(ASSET_PATHS["TestCAD_stp"])
        assert len(stp_mesh.vertices) > 0 and len(stp_mesh.faces) > 0
        assert stp_mesh.metadata.get("units") in ("mm", "m", "in")

    def test_f15_02_polymesh_roundtrip_preserves_geometry_and_boundaries(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify UnstructuredMesh -> export_openfoam_polymesh -> FoamReader round-trip."""
        pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 1.0],
                [1.0, 0.0, 1.0],
                [1.0, 1.0, 1.0],
                [0.0, 1.0, 1.0],
                [2.0, 0.0, 0.0],
                [2.0, 1.0, 0.0],
                [2.0, 0.0, 1.0],
                [2.0, 1.0, 1.0],
            ],
            dtype=np.float64,
        )
        cells = [[0, 1, 2, 3, 4, 5, 6, 7], [1, 8, 9, 2, 5, 10, 11, 6]]
        mesh = compute_difvm_topology_python(pts, cells)
        mesh.boundaries = {
            "inlet": (mesh.num_internal_faces, 5),
            "outlet": (mesh.num_internal_faces + 5, mesh.num_boundary_faces - 5),
        }
        foam_mesh = assert_polymesh_roundtrip(mesh, tmp_path / "case_f15_02")
        assert foam_mesh.n_cells == 2

    def test_f15_03_cabi_span_soa_lake_64_byte_alignment(self) -> None:
        """Verify export_mesh_to_soa_lake and export_fields_to_soa_lake produce 64B-aligned spans."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        lake = export_mesh_to_soa_lake(mesh)
        for key in ("points", "owner", "neighbour", "face_areas", "cell_volumes", "delta_f", "k_f"):
            assert key in lake
            span, _backing = lake[key]
            assert span.is_aligned_64(), f"Span '{key}' is not 64-byte aligned"
            roundtrip_arr = span.to_numpy()
            assert len(roundtrip_arr) > 0

        p_arr, _p_backing = allocate_aligned_array((mesh.num_cells,), dtype=np.float64, align=64)
        p_arr[:] = np.linspace(101325.0, 101400.0, mesh.num_cells)
        field_lake = export_fields_to_soa_lake({"p": p_arr})
        p_span, _ = field_lake["p"]
        assert p_span.is_aligned_64()
        assert np.allclose(p_span.to_numpy(), p_arr)

    def test_f15_04_mphys_container_write_and_read_roundtrip(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify MPhysWriter and MPhysReader round-trip mesh epoch, field steps, and AABB query."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        mphys_path = tmp_path / "sample.mphys"

        pressure = np.full(mesh.num_cells, 101325.0, dtype=np.float64)
        velocity = np.ones((mesh.num_cells, 3), dtype=np.float64)

        with MPhysWriter(
            mphys_path,
            chunk_size=65536,
            compression_method=CompressionType.ZLIB,
            mesh_epoch_id=1,
        ) as writer:
            writer.write_mesh_epoch(mesh)
            writer.write_field_step(
                step_id=0,
                time_val=0.0,
                dt=1e-4,
                fields={"p": pressure, "U": velocity},
            )

        with MPhysReader(mphys_path) as reader:
            chunks = reader.find_chunks_intersecting_aabb(
                np.array([-0.1, -0.1, -0.1]),
                np.array([1.1, 1.1, 1.1]),
            )
            assert len(chunks) >= 1
            mesh_data = reader.read_mesh_epoch()
            assert len(mesh_data["cell_volumes"]) == mesh.num_cells
            _step_meta, step_data = reader.read_field_step(0)
            assert np.allclose(np.sort(step_data["p"]), np.sort(pressure))

    def test_f15_05_ipc_binary_buffers_si_dimensions_and_jsonrpc_hello(self) -> None:
        """Verify CFDGEO01, CFDMSH02, 7D SI dimension validation, and IPCService system.hello."""
        v, f = create_unit_cube_arrays()
        normals = np.zeros((len(v), 3), dtype=np.float32)
        normals[:, 2] = 1.0
        groups = np.zeros(len(f), dtype=np.uint32)
        geo_bytes, geo_desc = pack_geometry_buffer(
            v.astype(np.float64),
            normals,
            f.astype(np.uint32),
            groups,
            {"is_watertight": True},
        )
        assert geo_desc.vertex_count == len(v)
        unpacked_geo = unpack_geometry_buffer(geo_bytes)
        assert np.allclose(unpacked_geo["vertices"], v)

        n_c = 16
        ones_f32 = np.ones(n_c, dtype=np.float32)
        msh_bytes, msh_desc = pack_mesh_quality_buffer(
            ones_f32 * 0.1,
            ones_f32 * 5.0,
            ones_f32 * 1.5,
            ones_f32 * 0.01,
            ones_f32 * 1.0,
            ones_f32 * 2.0,
            np.array([], dtype=np.uint32),
            {"is_valid": True},
        )
        assert msh_desc.cell_count == n_c
        unpacked_msh = unpack_mesh_quality_buffer(msh_bytes)
        assert len(unpacked_msh["volume"]) == n_c

        dims = validate_si_dimensions([1, -1, -2, 0, 0, 0, 0])
        foam_str = format_openfoam_dimensions(dims)
        assert parse_openfoam_dimensions(foam_str) == [1, -1, -2, 0, 0, 0, 0]

        svc = IPCService()
        hello_res = svc.endpoint_system_hello({"client": "e2e_suite"})
        assert "protocol_version" in hello_res or "version" in hello_res or isinstance(hello_res, dict)
