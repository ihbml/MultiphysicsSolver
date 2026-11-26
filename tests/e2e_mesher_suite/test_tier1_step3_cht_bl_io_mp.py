"""Tier 1 isolated happy-path E2E tests for Step 3 (F8-F14): WSL, CHT, BL, Merging, Fluent, Multi-Physics."""

from __future__ import annotations

import importlib
import logging
from pathlib import Path
from typing import Any, Dict

import numpy as np
import pytest

from backend.core.case_generator import CaseGenerator
from backend.meshing.invariants import assert_fvm_invariants
from backend.core.materials import MaterialDatabase
from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    extract_cht_twin_faces,
    generate_hexcore_native,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CellType,
    CurvatureSizingConfig,
    InflationConfig,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.meshing.polymesh_io import (
    export_openfoam_multiregion_polymesh,
    export_openfoam_polymesh,
)
from backend.physics_modules.monolithic_fsi import EulerianMonolithicFSI
from backend.physics_modules.registry import ModuleRegistry
from backend.runner.wsl_runner import WslSimulationRunner
from tests.conftest import create_unit_cube_arrays
from tests.e2e_mesher_suite.conftest import ASSET_PATHS, load_cached_surface
from tests.e2e_mesher_suite.oracles import (
    assert_cht_1to1_conformal,
    assert_j_ens_geometric_not_dummy,
    assert_linear_field_exact,
    assert_polymesh_roundtrip,
    assert_unstructured_mesh_valid,
    count_surface_components_scipy,
    read_fluent_msh_h5_summary,
    run_subprocess_bounded,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier1]


def _make_two_region_hex_mesh() -> UnstructuredMesh:
    """Create a 2-region 2x1x1 hexahedral mesh with an internal interface at x=1.0."""
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
    regions = np.array([0, 1], dtype=np.int32)
    return compute_difvm_topology_python(pts, cells, regions=regions)


def _make_three_region_hex_mesh() -> UnstructuredMesh:
    """Create a 3-region 3x1x1 hexahedral mesh with interfaces at x=1.0 and x=2.0."""
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
            [3.0, 0.0, 0.0],
            [3.0, 1.0, 0.0],
            [3.0, 0.0, 1.0],
            [3.0, 1.0, 1.0],
        ],
        dtype=np.float64,
    )
    cells = [
        [0, 1, 2, 3, 4, 5, 6, 7],
        [1, 8, 9, 2, 5, 10, 11, 6],
        [8, 12, 13, 9, 10, 14, 15, 11],
    ]
    regions = np.array([0, 1, 2], dtype=np.int32)
    return compute_difvm_topology_python(pts, cells, regions=regions)


class TestE2EMesherTier1F8WslToolchainAndRunner:
    """F8: WSL2 OpenFOAM (checkMesh) Toolchain & WslSimulationRunner (>=5 tests)."""

    def test_f8_01_wsl_simulation_runner_to_wsl_path_conversion(self) -> None:
        """Verify WslSimulationRunner.to_wsl_path converts Windows drive paths to /mnt/<drive>/..."""
        runner = WslSimulationRunner()
        wsl_path = runner.to_wsl_path(r"G:\Datein\Projects\CFDSolver\case1")
        assert wsl_path == "/mnt/g/Datein/Projects/CFDSolver/case1"
        assert runner.to_wsl_path("/mnt/c/existing/path") == "/mnt/c/existing/path"

    def test_f8_02_wsl_simulation_runner_build_command_quoting(self) -> None:
        """Verify WslSimulationRunner.build_command sources OpenFOAM bashrc and changes to case dir."""
        runner = WslSimulationRunner(bashrc_path="/usr/lib/openfoam/openfoam/etc/bashrc")
        cmd = runner.build_command("/mnt/g/test case", "checkMesh")
        assert "/usr/lib/openfoam/openfoam/etc/bashrc" in cmd
        assert "checkMesh" in cmd

    def test_f8_03_wsl_simulation_runner_parse_log_text_extracts_telemetry(self) -> None:
        """Verify WslSimulationRunner.parse_log_text parses Time, Courant numbers, and solver residuals."""
        runner = WslSimulationRunner()
        sample_log = (
            "Time = 0.001\n"
            "Courant Number mean: 0.05 max: 0.42\n"
            "DICPCG:  Solving for p, Initial residual = 0.125, Final residual = 1.2e-07, No Iterations 18\n"
            "smoothSolver:  Solving for Ux, Initial residual = 0.04, Final residual = 3.1e-08, No Iterations 5\n"
        )
        steps = runner.parse_log_text(sample_log)
        assert len(steps) == 1
        assert np.isclose(steps[0].time, 0.001)
        assert np.isclose(steps[0].courant_max, 0.42)
        assert "p" in steps[0].residuals

    @pytest.mark.wsl_checkmesh
    def test_f8_04_run_wsl_checkmesh_script_exists_and_executable(
        self,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify scripts/run_wsl_checkmesh.sh and WSL2 OpenFOAM checkMesh binary are ready."""
        repo_root = Path(__file__).resolve().parents[2]
        script_path = repo_root / "scripts" / "run_wsl_checkmesh.sh"
        assert script_path.is_file(), f"Required M3 script missing: {script_path}"
        assert require_wsl_checkmesh["mode"] in ("wrapper_script", "wsl_direct")

    @pytest.mark.wsl_checkmesh
    def test_f8_05_wsl_runner_executes_checkmesh_on_unit_hex_case(
        self,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify WslSimulationRunner runs checkMesh on a clean 2-cell hex polyMesh case."""
        mesh = _make_two_region_hex_mesh()
        mesh.cell_regions = np.zeros(mesh.num_cells, dtype=np.int32)
        mesh.boundaries = {"wall": (mesh.num_internal_faces, mesh.num_boundary_faces)}
        case_dir = tmp_path / "wsl_unit_hex"
        export_openfoam_polymesh(mesh, case_dir)
        (case_dir / "system").mkdir(parents=True, exist_ok=True)
        (case_dir / "system" / "controlDict").write_text(
            "FoamFile { version 2.0; format ascii; class dictionary; object controlDict; }\n"
            "application simpleFoam;\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n"
            "endTime 1;\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval 1;\n",
            encoding="utf-8",
        )
        runner = WslSimulationRunner()
        res = runner.run_mesh(str(case_dir), mesh_command="checkMesh", check=True)
        assert res.returncode == 0
        assert "Mesh OK" in res.stdout


class TestE2EMesherTier1F9MultiZoneChtAnd1To1Stitching:
    """F9: Multi-Zone CHT Auto-Detection & 1:1 Conformal Interface Stitching (>=5 tests)."""

    def test_f9_01_extract_cht_twin_faces_two_region_conformal(self) -> None:
        """Verify extract_cht_twin_faces creates 1:1 conformal twin faces with S_B == -S_A on 2 regions."""
        raw_mesh = _make_two_region_hex_mesh()
        cht_mesh = extract_cht_twin_faces(raw_mesh)
        assert_cht_1to1_conformal(cht_mesh, expected_regions=2)
        assert_unstructured_mesh_valid(cht_mesh)

    def test_f9_02_extract_cht_twin_faces_three_region_conformal_node_sets(self) -> None:
        """Verify extract_cht_twin_faces preserves identical node sets across >=3 CHT regions."""
        raw_mesh = _make_three_region_hex_mesh()
        cht_mesh = extract_cht_twin_faces(raw_mesh)
        assert_cht_1to1_conformal(cht_mesh, expected_regions=3)
        assert_unstructured_mesh_valid(cht_mesh)

    def test_f9_03_blade_rev1_obj_4_shell_component_detection(
        self,
        blade_rev1_surface: SurfaceMesh,
    ) -> None:
        """Verify BladeRev1CFD.obj has 4 topological surface components (1 blade + 3 cooling channels)."""
        n_comp = count_surface_components_scipy(
            blade_rev1_surface.vertices,
            blade_rev1_surface.faces,
        )
        assert n_comp == 4

    def test_f9_04_export_openfoam_multiregion_polymesh_creates_region_properties(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_multiregion_polymesh writes constant/<region>/polyMesh and regionProperties."""
        m_fluid = _make_two_region_hex_mesh()
        m_fluid.cell_regions = np.zeros(m_fluid.num_cells, dtype=np.int32)
        m_fluid.boundaries = {"wall": (m_fluid.num_internal_faces, m_fluid.num_boundary_faces)}
        m_solid = _make_two_region_hex_mesh()
        m_solid.cell_regions = np.zeros(m_solid.num_cells, dtype=np.int32)
        m_solid.boundaries = {"wall": (m_solid.num_internal_faces, m_solid.num_boundary_faces)}

        case_dir = tmp_path / "cht_multiregion_case"
        exported = export_openfoam_multiregion_polymesh(
            case_dir,
            regions={"fluid": m_fluid, "solid": m_solid},
            region_properties={"fluid": ["fluid"], "solid": ["solid"]},
        )
        assert "fluid" in exported and "solid" in exported
        assert (case_dir / "constant" / "fluid" / "polyMesh" / "points").is_file()
        assert (case_dir / "constant" / "solid" / "polyMesh" / "points").is_file()
        reg_props = case_dir / "constant" / "regionProperties"
        assert reg_props.is_file()
        content = reg_props.read_text(encoding="utf-8")
        assert "fluid" in content and "solid" in content

    def test_f9_05_mesh_multi_region_concentric_fluid_solid_domains(self) -> None:
        """Verify MeshEngine.mesh_multi_region produces multi-region CHT mesh with twin interfaces."""
        v_outer, f_outer = create_unit_cube_arrays()
        v_inner = (v_outer - 0.5) * 0.4 + 0.5
        s_outer = SurfaceMesh(vertices=v_outer, faces=f_outer.astype(np.int32))
        s_inner = SurfaceMesh(vertices=v_inner, faces=f_outer.astype(np.int32))

        engine = MeshEngine(use_native=True)
        mesh = engine.mesh_multi_region(
            {"fluid": s_outer, "solid": s_inner},
            h0=0.2,
        )
        assert mesh.num_regions >= 2
        assert_unstructured_mesh_valid(mesh)


class TestE2EMesherTier1F10BoundaryLayerInflationAndStitching:
    """F10: Collision-Free Medial-Axis Prism BL + Layer-Chopping + Hexcore Stitching (>=5 tests)."""

    def test_f10_01_inflate_boundary_layers_planar_quad_wall(self) -> None:
        """Verify MeshEngine.inflate_boundary_layers generates positive-volume prism layers on a planar wall."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        wall_faces = [[0, 1, 2, 3]]
        inf_cfg = InflationConfig(
            num_layers=4,
            first_layer_height=0.01,
            growth_rate=1.2,
            pinching=True,
        )
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            wall_faces,
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
        )
        assert bl_mesh.num_cells == 4
        assert np.all(bl_mesh.cell_volumes > 0.0)

    def test_f10_02_inflation_layer_thickness_geometric_progression(self) -> None:
        """Verify prism layer volumes scale geometrically by growth_rate across layers."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        g_rate = 1.25
        inf_cfg = InflationConfig(
            num_layers=3,
            first_layer_height=0.02,
            growth_rate=g_rate,
            pinching=False,
        )
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            [[0, 1, 2, 3]],
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
        )
        vols = np.sort(bl_mesh.cell_volumes)
        assert len(vols) == 3
        assert np.isclose(vols[1] / vols[0], g_rate, rtol=0.05)
        assert np.isclose(vols[2] / vols[1], g_rate, rtol=0.05)

    def test_f10_03_medial_axis_clearance_layer_chopping_in_narrow_gap(self) -> None:
        """Verify narrow clearance triggers collision-free layer retraction/chopping without negative volumes."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        inf_cfg = InflationConfig(
            num_layers=5,
            first_layer_height=0.02,
            growth_rate=1.3,
            pinching=True,
        )
        clearances = np.full(4, 0.05, dtype=np.float64)
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            [[0, 1, 2, 3]],
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
            clearances=clearances,
        )
        assert bl_mesh.num_cells > 0
        assert np.all(bl_mesh.cell_volumes > 0.0)
        assert float(np.max(bl_mesh.points[:, 2])) <= 0.05 + 1e-6

    def test_f10_04_inflation_config_expansion_ratio_and_growth_rate_sync(self) -> None:
        """Verify InflationConfig synchronizes expansion_ratio and growth_rate."""
        cfg = InflationConfig(num_layers=5, first_layer_height=1e-3, expansion_ratio=1.35)
        assert np.isclose(cfg.growth_rate, 1.35)
        assert np.isclose(cfg.expansion_ratio or 0.0, 1.35)
        restored = InflationConfig.from_dict(cfg.to_dict())
        assert np.isclose(restored.growth_rate, 1.35)

    def test_f10_05_hexcore_with_inflation_config_produces_valid_stitched_mesh(self) -> None:
        """Verify MeshingConfig with InflationConfig generates a valid volumetric mesh on unit cube."""
        v, f = create_unit_cube_arrays()
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.5, h_min=0.25, h_max=0.5),
            inflation=InflationConfig(num_layers=2, first_layer_height=0.02, growth_rate=1.2),
        )
        mesh = generate_hexcore_native(
            v,
            f,
            h0=0.5,
            domain_mode="internal_fluid",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)


class TestE2EMesherTier1F11SmallCutCellMergingAndDegenerateElimination:
    """F11: Small-Cut-Cell Merging & Degenerate Cell Elimination (>=5 tests)."""

    def test_f11_01_small_cell_ratio_config_eliminates_tiny_sliver_volumes(self) -> None:
        """Verify small_cell_ratio merging prevents arbitrarily tiny cut-cell volumes on shifted plane cut."""
        v, f = create_unit_cube_arrays()
        v_shifted = v * 0.502 + 0.001
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            small_cell_ratio=0.20,
            sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=0.5),
        )
        mesh = generate_hexcore_native(
            v_shifted,
            f,
            h0=0.25,
            domain_mode="internal_fluid",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)
        assert float(np.min(mesh.cell_volumes)) > 1e-9

    def test_f11_02_cut_cell_merging_preserves_gauss_closure_and_positive_j_ens(self) -> None:
        """Verify cut-cell mesh after small-cell merging satisfies Gauss closure and J_ENS > 0."""
        from tests.test_asme_vv import create_geodesic_icosphere

        v_sph, f_sph = create_geodesic_icosphere(radius=0.47, subdivisions=2)
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            small_cell_ratio=0.20,
            sizing=CurvatureSizingConfig(h0=0.20, h_min=0.10, h_max=0.40),
        )
        mesh = generate_hexcore_native(
            np.asarray(v_sph, dtype=np.float64),
            np.asarray(f_sph, dtype=np.int32),
            h0=0.20,
            domain_mode="internal_fluid",
            config=cfg,
        )
        report = assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert report.max_closed_cell_divergence < 1e-11

    def test_f11_03_cut_cell_merging_preserves_linear_field_exactness(self) -> None:
        """Verify merged cut-cell polyhedra maintain linear-field gradient and Laplacian exactness."""
        from tests.test_asme_vv import create_geodesic_icosphere

        v_sph, f_sph = create_geodesic_icosphere(radius=0.45, subdivisions=2)
        mesh = generate_hexcore_native(
            np.asarray(v_sph, dtype=np.float64),
            np.asarray(f_sph, dtype=np.int32),
            h0=0.25,
            domain_mode="internal_fluid",
        )
        assert_linear_field_exact(mesh, tol=1e-12)

    @pytest.mark.slow
    def test_f11_04_zero_inverted_pyramids_on_rotated_cube_cut_cells(self) -> None:
        """Verify rotated cube cut-cell mesh has zero inverted pyramids and finite quality metrics."""
        v, f = create_unit_cube_arrays()
        ang = np.deg2rad(22.5)
        rot = np.array(
            [
                [np.cos(ang), -np.sin(ang), 0.0],
                [np.sin(ang), np.cos(ang), 0.0],
                [0.0, 0.0, 1.0],
            ],
            dtype=np.float64,
        )
        v_rot = (v - 0.5) @ rot.T
        mesh = generate_hexcore_native(v_rot, f, h0=0.35, domain_mode="internal_fluid")
        report = assert_unstructured_mesh_valid(mesh)
        assert report.inverted_pyramids == 0

    def test_f11_05_upper_triangular_owner_less_than_neighbour_after_merging(self) -> None:
        """Verify owner < neighbour and contiguous internal/boundary ordering hold after merging."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v * 0.73, f, h0=0.3, domain_mode="internal_fluid")
        n_int = mesh.num_internal_faces
        assert np.all(mesh.neighbour[:n_int] >= 0)
        assert np.all(mesh.neighbour[n_int:] == -1)
        assert np.all(mesh.owner[:n_int] < mesh.neighbour[:n_int])


@pytest.mark.fluent_ref
class TestE2EMesherTier1F12FluentMshH5QuantitativeParity:
    """F12: Fluent .msh.h5 Quantitative Parity Verification (fluent_io.py) (>=5 tests)."""

    def test_f12_01_read_fltg_msh_h5_summary_10_zones_12m_cells(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify FLTG.msh.h5 oracle summary extracts 12,129,682 cells across 10 zones (ctype 4 and 7)."""
        summary = read_fluent_msh_h5_summary(require_fluent_h5["FLTG"])
        assert summary["cellCount"] == 12_129_682
        assert summary["cell_zones"]["nZones"] == 10
        assert set(summary["ctype_counts"].keys()) == {4, 7}
        assert sum(summary["ctype_counts"].values()) == 12_129_682
        assert summary["boundary_layer_flags"]["present"] is True

    def test_f12_02_read_fltg_1_1_msh_h5_summary_6_zones_7_6m_cells(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify FLTG 1.1.msh.h5 oracle summary extracts 7,606,273 cells across 6 zones."""
        summary = read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"])
        assert summary["cellCount"] == 7_606_273
        assert summary["cell_zones"]["nZones"] == 6
        assert set(summary["ctype_counts"].keys()) == {4, 7}
        assert sum(summary["ctype_counts"].values()) == 7_606_273
        assert summary["boundary_layer_flags"]["present"] is True

    def test_f12_03_fluent_io_module_import_and_reference_extractor(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify backend.meshing.fluent_io module exists and extracts Fluent HDF5 reference metrics."""
        try:
            fluent_io = importlib.import_module("backend.meshing.fluent_io")
        except ImportError as exc:
            pytest.fail(
                f"M3 module backend.meshing.fluent_io is not yet implemented: {exc}"
            )
        extractor = (
            getattr(fluent_io, "read_fluent_msh_h5_metrics", None)
            or getattr(fluent_io, "extract_fluent_reference_metrics", None)
            or getattr(fluent_io, "read_fluent_msh_h5_summary", None)
        )
        assert callable(extractor), (
            "backend.meshing.fluent_io must expose a callable reference extractor"
        )
        metrics = extractor(require_fluent_h5["FLTG_1_1"])
        assert isinstance(metrics, dict)

    def test_f12_04_fluent_io_parity_comparison_contract(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify backend.meshing.fluent_io exposes quantitative parity comparator against Fluent .msh.h5."""
        try:
            fluent_io = importlib.import_module("backend.meshing.fluent_io")
        except ImportError as exc:
            pytest.fail(
                f"M3 module backend.meshing.fluent_io is not yet implemented: {exc}"
            )
        comparator = (
            getattr(fluent_io, "compare_mesh_to_fluent_reference", None)
            or getattr(fluent_io, "evaluate_fluent_parity", None)
            or getattr(fluent_io, "compare_fluent_parity", None)
        )
        assert callable(comparator), (
            "backend.meshing.fluent_io must expose a parity comparison function"
        )

    def test_f12_05_fluent_h5_bounding_box_matches_cad_assemblies_in_mm(
        self,
        require_fluent_h5: Dict[str, Path],
        blade_rev1_surface: SurfaceMesh,
    ) -> None:
        """Verify BladeRev1CFD.obj XY bounding box in mm aligns with FLTG.msh.h5 reference."""
        summary = read_fluent_msh_h5_summary(require_fluent_h5["FLTG"])
        cad_bbox = blade_rev1_surface.get_bounding_box()
        h5_min = np.asarray(summary["bbox_min"], dtype=np.float64)
        h5_max = np.asarray(summary["bbox_max"], dtype=np.float64)
        assert np.isclose(cad_bbox.min_pt[1], h5_min[1], atol=0.5)
        assert np.isclose(cad_bbox.max_pt[0], h5_max[0], atol=0.5)
        assert np.isclose(cad_bbox.max_pt[1], h5_max[1], atol=0.5)
        assert np.isclose(cad_bbox.max_pt[2], h5_max[2], atol=0.5)


class TestE2EMesherTier1F13CheckMeshCrossValidation:
    """F13: WSL2 checkMesh Cross-Validation & polyMesh Export Fidelity (>=5 tests)."""

    def test_f13_01_native_hexcore_polymesh_roundtrip_has_valid_named_boundaries(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify native hexcore mesh exports non-empty boundary patch names and passes polyMesh roundtrip."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        assert "" not in mesh.boundaries, (
            f"generate_hexcore_native emitted empty boundary patch name '': {mesh.boundaries}"
        )
        foam_mesh = assert_polymesh_roundtrip(mesh, tmp_path / "hexcore_poly")
        assert foam_mesh.n_cells == mesh.num_cells

    def test_f13_02_chamfered_polyhedron_polymesh_roundtrip(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify ctype=42 polyhedron exports to polyMesh and reloads with identical volume."""
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
        foam_mesh = assert_polymesh_roundtrip(mesh, tmp_path / "poly_cell_case")
        assert foam_mesh.n_cells == 1

    @pytest.mark.wsl_checkmesh
    def test_f13_03_wsl_checkmesh_passes_on_exported_polyhedron_cell(
        self,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify WSL2 checkMesh passes with 0 errors on exported ctype=42 polyhedron."""
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
        case_dir = tmp_path / "wsl_poly_case"
        export_openfoam_polymesh(mesh, case_dir)
        (case_dir / "system").mkdir(parents=True, exist_ok=True)
        (case_dir / "system" / "controlDict").write_text(
            "FoamFile { version 2.0; format ascii; class dictionary; object controlDict; }\n"
            "application simpleFoam;\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n"
            "endTime 1;\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval 1;\n",
            encoding="utf-8",
        )
        runner = WslSimulationRunner()
        res = runner.run_mesh(str(case_dir), mesh_command="checkMesh", check=True)
        assert res.returncode == 0
        assert "Mesh OK" in res.stdout

    @pytest.mark.wsl_checkmesh
    def test_f13_04_wsl_checkmesh_passes_on_native_hexcore_cube(
        self,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify WSL2 checkMesh passes on an exported native hexcore mesh."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        assert_fvm_invariants(mesh, expect_patches=("wall",))
        case_dir = tmp_path / "wsl_hexcore_cube"
        export_openfoam_polymesh(mesh, case_dir)
        (case_dir / "system").mkdir(parents=True, exist_ok=True)
        (case_dir / "system" / "controlDict").write_text(
            "FoamFile { version 2.0; format ascii; class dictionary; object controlDict; }\n"
            "application simpleFoam;\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n"
            "endTime 1;\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval 1;\n",
            encoding="utf-8",
        )
        runner = WslSimulationRunner()
        res = runner.run_mesh(str(case_dir), mesh_command="checkMesh", check=True)
        assert res.returncode == 0
        assert "Mesh OK" in res.stdout

    @pytest.mark.wsl_checkmesh
    def test_f13_05_wsl_checkmesh_passes_on_multiregion_cht_export(
        self,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify WSL2 checkMesh -region passes on each region of a multi-region CHT case."""
        m_a = _make_two_region_hex_mesh()
        m_a.cell_regions = np.zeros(m_a.num_cells, dtype=np.int32)
        m_a.boundaries = {"wall": (m_a.num_internal_faces, m_a.num_boundary_faces)}
        m_b = _make_two_region_hex_mesh()
        m_b.cell_regions = np.zeros(m_b.num_cells, dtype=np.int32)
        m_b.boundaries = {"wall": (m_b.num_internal_faces, m_b.num_boundary_faces)}
        case_dir = tmp_path / "wsl_cht_regions"
        export_openfoam_multiregion_polymesh(
            case_dir,
            regions={"fluid": m_a, "solid": m_b},
            region_properties={"fluid": ["fluid"], "solid": ["solid"]},
        )
        (case_dir / "system").mkdir(parents=True, exist_ok=True)
        (case_dir / "system" / "controlDict").write_text(
            "FoamFile { version 2.0; format ascii; class dictionary; object controlDict; }\n"
            "application chtMultiRegionFoam;\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n"
            "endTime 1;\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval 1;\n",
            encoding="utf-8",
        )
        runner = WslSimulationRunner()
        for reg in ("fluid", "solid"):
            res = runner.run_mesh(str(case_dir), mesh_command=f"checkMesh -region {reg}", check=True)
            assert res.returncode == 0
            assert "Mesh OK" in res.stdout


class TestE2EMesherTier1F14MultiTurbulenceAndMultiPhysicsCompatibility:
    """F14: Multi-Turbulence & Multi-Physics Solver Compatibility (>=5 tests)."""

    def test_f14_01_module_registry_13_modules_and_turbulence_variants(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify ModuleRegistry registers 13 modules and generates valid OpenFOAM turbulenceProperties."""
        reg = ModuleRegistry()
        all_mods = reg.get_all_modules()
        assert len(all_mods) == 13

        # Test LES WALE and Laminar switching in Viscous & Turbulence module
        reg.enable_module("Viscous & Turbulence", variant="LES WALE")
        turb_mod = reg.get_module("Viscous & Turbulence")
        assert turb_mod is not None
        entries = turb_mod.generate_dictionary_entries(str(tmp_path))
        turb_dict = entries["constant/turbulenceProperties"]
        assert "simulationType  LES;" in turb_dict or "simulationType LES;" in turb_dict, (
            f"LES WALE variant must emit simulationType LES, got:\n{turb_dict}"
        )

    def test_f14_02_case_generator_energy_module_writes_temperature_field(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify enabling 'Energy' in ModuleRegistry causes CaseGenerator to write 0/T and 0/alphat."""
        reg = ModuleRegistry()
        reg.enable_module("Energy", variant="sensibleEnthalpy")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            case_name="energy_test",
            target_dir=str(tmp_path),
            solver_name="simpleFoam",
            patches={"inlet": "patch", "outlet": "patch", "wall": "wall"},
        )
        assert (case_path / "0" / "U").is_file()
        assert (case_path / "0" / "p").is_file()
        assert (case_path / "0" / "T").is_file(), (
            "CaseGenerator failed to emit 0/T when EnergyModule ('Energy') is enabled"
        )

    def test_f14_03_case_generator_multiphase_module_writes_alpha_water_field(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify enabling 'Multiphase' in ModuleRegistry causes CaseGenerator to write 0/alpha.water."""
        reg = ModuleRegistry()
        reg.enable_module("Multiphase", variant="VoF_InterfaceTracking")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            case_name="vof_test",
            target_dir=str(tmp_path),
            solver_name="interFoam",
            patches={"inlet": "patch", "outlet": "patch", "wall": "wall"},
        )
        assert (case_path / "0" / "alpha.water").is_file(), (
            "CaseGenerator failed to emit 0/alpha.water when MultiphaseModule ('Multiphase') is enabled"
        )

    def test_f14_04_species_chemistry_and_mhd_module_dictionary_generation(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify Species & Chemistry and Magnetohydrodynamics (MHD) modules emit solver dictionaries."""
        reg = ModuleRegistry()
        reg.enable_module("Energy")
        reg.enable_module("Species & Chemistry", variant="finiteRateKinetics")
        reg.enable_module("Magnetohydrodynamics (MHD)")
        warnings = reg.validate_configuration()
        assert warnings == []

        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            case_name="chem_mhd_case",
            target_dir=str(tmp_path),
            solver_name="reactingFoam",
            patches={"inlet": "patch", "outlet": "patch", "wall": "wall"},
        )
        assert (case_path / "constant" / "chemistryProperties").is_file()
        assert (case_path / "constant" / "mhdProperties").is_file()

    def test_f14_05_monolithic_eulerian_fsi_and_material_database_integration(self) -> None:
        """Verify EulerianMonolithicFSI stress/PLIC calculations and MaterialDatabase properties."""
        db = MaterialDatabase()
        fluids = db.list_materials(category="fluid")
        solids = db.list_materials(category="solid")
        assert len(fluids) >= 2 and len(solids) >= 2

        fsi = EulerianMonolithicFSI(rho_s=7800.0, rho_f=1000.0, mu_s=1e-2, mu_f=1e-3, G_s=1e5)
        phi = np.array([0.0, 0.5, 1.0], dtype=np.float64)
        rho_mix = fsi.compute_mixture_density(phi)
        assert np.allclose(rho_mix, [1000.0, 4400.0, 7800.0])

        u_grad = np.zeros((3, 3, 3), dtype=np.float64)
        p = np.array([101325.0, 101325.0, 101325.0], dtype=np.float64)
        b_ten = np.broadcast_to(np.eye(3, dtype=np.float64), (3, 3, 3)).copy()
        stress = fsi.compute_mixture_stress(phi, u_grad, p, b_ten)
        assert stress.shape == (3, 3, 3)
        assert np.all(np.isfinite(stress))
