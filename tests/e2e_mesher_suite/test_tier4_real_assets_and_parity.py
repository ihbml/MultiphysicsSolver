"""Tier 4 real-world CAD, OpenFOAM tutorial, and Ansys Fluent (.msh.h5) parity scenarios (10+ scenarios)."""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Dict

import numpy as np
import pytest

from backend.core.case_generator import CaseGenerator
from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import generate_hexcore_native
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CellType,
    CurvatureSizingConfig,
    FarfieldPadding,
    InflationConfig,
    MeshingConfig,
)
from backend.meshing.polymesh_io import (
    export_openfoam_multiregion_polymesh,
    export_openfoam_polymesh,
)
from backend.native_rust.c_abi_lake import export_mesh_to_soa_lake
from backend.native_rust.mphys_container import CompressionType, MPhysReader, MPhysWriter
from backend.physics_modules.registry import ModuleRegistry
from backend.runner.wsl_runner import WslSimulationRunner
from tests.e2e_mesher_suite.conftest import ASSET_PATHS, load_cached_surface
from tests.e2e_mesher_suite.oracles import (
    assert_cht_1to1_conformal,
    assert_j_ens_geometric_not_dummy,
    assert_linear_field_exact,
    assert_polymesh_roundtrip,
    assert_unstructured_mesh_valid,
    count_surface_components_scipy,
    read_fluent_msh_h5_summary,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier4]


class TestE2EMesherTier4Scenario1Naca0012ExternalAero:
    """Scenario 1: NACA0012.obj.gz External Aerodynamics (external_fluid graded farfield + BL + checkMesh)."""

    def test_s01_naca0012_external_fluid_graded_farfield_and_j_ens(
        self,
        naca0012_surface: SurfaceMesh,
    ) -> None:
        """Verify NACA0012 external_fluid mesh has graded farfield, positive ctype=42 J_ENS, and linear exactness."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=3.0, downstream=5.0, lateral_y=2.5, lateral_z=2.5),
            sizing=CurvatureSizingConfig(h0=0.20, h_min=0.08, h_max=0.80, grading=0.35),
            inflation=InflationConfig(num_layers=2, first_layer_height=0.005, growth_rate=1.2),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(naca0012_surface, h0=0.20, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert_j_ens_geometric_not_dummy(mesh)
        assert_linear_field_exact(mesh)

    @pytest.mark.wsl_checkmesh
    def test_s01_naca0012_polymesh_and_wsl_checkmesh(
        self,
        naca0012_surface: SurfaceMesh,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify NACA0012 external_fluid mesh passes polyMesh roundtrip and WSL2 checkMesh."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=2.0, downstream=3.0, lateral_y=2.0, lateral_z=2.0),
            sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=1.0, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(naca0012_surface, h0=0.25, domain_mode="external_fluid", config=cfg)
        case_dir = tmp_path / "naca0012_case"
        assert_polymesh_roundtrip(mesh, case_dir)
        runner = WslSimulationRunner()
        res = runner.run_mesh(str(case_dir), mesh_command="checkMesh", check=True)
        assert res.returncode == 0 and "Mesh OK" in res.stdout


class TestE2EMesherTier4Scenario2MotorBikeVehicleAero:
    """Scenario 2: motorBike.obj.gz Non-Watertight 67-Patch Vehicle Aero (graded farfield + 5-12M scaling)."""

    @pytest.mark.slow
    def test_s02_motorbike_67_patch_ingestion_and_graded_external_fluid_mesh(
        self,
        motorbike_surface: SurfaceMesh,
    ) -> None:
        """Verify motorBike.obj.gz preserves 67 patches and meshes cleanly in external_fluid mode."""
        assert len(motorbike_surface.patches) == 67
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=1.5, downstream=2.5, lateral_y=1.5, lateral_z=1.5),
            sizing=CurvatureSizingConfig(h0=0.50, h_min=0.25, h_max=1.50, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(motorbike_surface, h0=0.50, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)

    @pytest.mark.slow
    @pytest.mark.scale_5m
    def test_s02_motorbike_5m_to_12m_multicore_scaling(
        self,
        motorbike_surface: SurfaceMesh,
    ) -> None:
        """Verify motorBike.obj.gz scales to >= 5,000,000 cells in native C++/OpenMP octree."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=2.0, downstream=4.0, lateral_y=2.0, lateral_z=2.0),
            sizing=CurvatureSizingConfig(h0=0.04, h_min=0.008, h_max=0.32, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(motorbike_surface, h0=0.04, domain_mode="external_fluid", config=cfg)
        assert 5_000_000 <= mesh.num_cells <= 15_000_000
        assert np.all(mesh.cell_volumes > 0.0)


class TestE2EMesherTier4Scenario3DtcScaledShipHullHydrodynamics:
    """Scenario 3: DTC-scaled.stl.gz Open Ship Hull Hydrodynamics (external_fluid + VoF Multiphase + 5-12M)."""

    @pytest.mark.slow
    def test_s03_dtc_scaled_external_fluid_and_interfoam_multiphase_case(
        self,
        dtc_scaled_surface: SurfaceMesh,
        tmp_path: Path,
    ) -> None:
        """Verify DTC-scaled ship hull meshes in external_fluid and pairs with Multiphase interFoam case."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=1.0, downstream=1.5, lateral_y=1.0, lateral_z=1.0),
            sizing=CurvatureSizingConfig(h0=0.80, h_min=0.40, h_max=2.40, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(dtc_scaled_surface, h0=0.80, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)

        reg = ModuleRegistry()
        reg.enable_module("Multiphase", variant="VoF_InterfaceTracking")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            "dtc_hull_vof",
            str(tmp_path),
            solver_name="interFoam",
            patches={"wall": "wall"},
        )
        assert (case_path / "0" / "alpha.water").is_file()

    @pytest.mark.slow
    @pytest.mark.scale_5m
    def test_s03_dtc_scaled_5m_cell_scaling(
        self,
        dtc_scaled_surface: SurfaceMesh,
    ) -> None:
        """Verify DTC-scaled.stl.gz scales to >= 5,000,000 cells cleanly."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=1.5, downstream=2.5, lateral_y=1.5, lateral_z=1.5),
            sizing=CurvatureSizingConfig(h0=0.06, h_min=0.015, h_max=0.48, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(dtc_scaled_surface, h0=0.06, domain_mode="external_fluid", config=cfg)
        assert mesh.num_cells >= 5_000_000
        assert np.all(mesh.cell_volumes > 0.0)


@pytest.mark.fluent_ref
class TestE2EMesherTier4Scenario4BladeRev1VsFltgParity:
    """Scenario 4: BladeRev1CFD.obj 4-Body Solid+Cooling Channels vs FLTG.msh.h5 (10 zones, ~12.13M target)."""

    def test_s04_blade_rev1_solid_only_thin_wall_cut_cells_and_j_ens(
        self,
        blade_rev1_surface: SurfaceMesh,
    ) -> None:
        """Verify BladeRev1CFD.obj scaled to meters meshes with Octree+Cut-Cells and positive ctype=42 J_ENS."""
        surf_m = blade_rev1_surface.copy()
        surf_m.vertices = surf_m.vertices * 0.001
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.025, h_min=0.0125, h_max=0.05, beta=0.25),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(surf_m, h0=0.025, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert_j_ens_geometric_not_dummy(mesh, check_poly_non_unit=True)
        assert_linear_field_exact(mesh)

    def test_s04_blade_rev1_10_zone_cht_auto_detection_and_fltg_parity(
        self,
        blade_rev1_surface: SurfaceMesh,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify BladeRev1CFD.obj CHT multi-zone auto-detection matches FLTG.msh.h5 10-zone topology."""
        fltg_ref = read_fluent_msh_h5_summary(require_fluent_h5["FLTG"])
        assert fltg_ref["cell_zones"]["nZones"] == 10
        assert fltg_ref["cellCount"] == 12_129_682

        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(
            domain_mode="cht_coupled",
            cht_multiregion=True,
            sizing=CurvatureSizingConfig(h0=0.025, h_min=0.01, h_max=0.05),
            inflation=InflationConfig(num_layers=3, first_layer_height=5e-4, growth_rate=1.2),
        )
        mesh = engine.mesh_cad_file(
            ASSET_PATHS["BladeRev1CFD"],
            h0=0.025,
            scale=0.001,
            domain_mode="cht_coupled",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)
        assert mesh.num_regions == 10, (
            f"Expected 10 auto-detected CHT zones matching FLTG.msh.h5, got {mesh.num_regions}"
        )
        assert_cht_1to1_conformal(mesh, expected_regions=10)

    @pytest.mark.slow
    @pytest.mark.scale_5m
    def test_s04_blade_rev1_12m_cell_fluent_scale_parity(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify BladeRev1CFD.obj scales to the ~12.13M cell regime of FLTG.msh.h5."""
        fltg_ref = read_fluent_msh_h5_summary(require_fluent_h5["FLTG"])
        target_cells = fltg_ref["cellCount"]
        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(
            domain_mode="cht_coupled",
            cht_multiregion=True,
            sizing=CurvatureSizingConfig(h0=0.0025, h_min=0.0005, h_max=0.005),
            inflation=InflationConfig(num_layers=5, first_layer_height=1e-4, growth_rate=1.2),
        )
        mesh = engine.mesh_cad_file(
            ASSET_PATHS["BladeRev1CFD"],
            h0=0.0025,
            scale=0.001,
            domain_mode="cht_coupled",
            config=cfg,
        )
        assert 5_000_000 <= mesh.num_cells <= int(target_cells * 1.5)


@pytest.mark.fluent_ref
class TestE2EMesherTier4Scenario5BladeFullStepVsFltg11Parity:
    """Scenario 5: BladeFullRev1CFD.stp AP242 2-Solid Assembly vs FLTG 1.1.msh.h5 (6 zones, ~7.61M target)."""

    @pytest.mark.slow
    def test_s05_blade_full_step_solid_only_cut_cells_and_j_ens(
        self,
        blade_full_surface: SurfaceMesh,
    ) -> None:
        """Verify BladeFullRev1CFD.stp meshes in solid_only mode with positive J_ENS on ctype=42 polyhedra."""
        surf_m = blade_full_surface.copy()
        surf_m.vertices = surf_m.vertices * 0.001
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.04, h_min=0.02, h_max=0.08, beta=0.25),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(surf_m, h0=0.04, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert_j_ens_geometric_not_dummy(mesh, check_poly_non_unit=True)

    @pytest.mark.slow
    def test_s05_blade_full_step_6_zone_cht_occ_fragment_and_fltg_1_1_parity(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify BladeFullRev1CFD.stp occ.fragment() CHT auto-detection matches FLTG 1.1.msh.h5 6-zone topology."""
        fltg11_ref = read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"])
        assert fltg11_ref["cell_zones"]["nZones"] == 6
        assert fltg11_ref["cellCount"] == 7_606_273

        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(
            domain_mode="cht_coupled",
            cht_multiregion=True,
            sizing=CurvatureSizingConfig(h0=0.04, h_min=0.015, h_max=0.08),
            inflation=InflationConfig(num_layers=3, first_layer_height=5e-4, growth_rate=1.2),
        )
        mesh = engine.mesh_cad_file(
            ASSET_PATHS["BladeFullRev1CFD"],
            h0=0.04,
            scale=0.001,
            domain_mode="cht_coupled",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)
        assert mesh.num_regions == 6, (
            f"Expected 6 auto-detected CHT zones matching FLTG 1.1.msh.h5, got {mesh.num_regions}"
        )
        assert_cht_1to1_conformal(mesh, expected_regions=6)

    @pytest.mark.slow
    @pytest.mark.scale_5m
    def test_s05_blade_full_step_7_6m_cell_fluent_scale_parity(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify BladeFullRev1CFD.stp scales to the ~7.61M cell regime of FLTG 1.1.msh.h5."""
        fltg11_ref = read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"])
        target_cells = fltg11_ref["cellCount"]
        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(
            domain_mode="cht_coupled",
            cht_multiregion=True,
            sizing=CurvatureSizingConfig(h0=0.0035, h_min=0.0008, h_max=0.008),
        )
        mesh = engine.mesh_cad_file(
            ASSET_PATHS["BladeFullRev1CFD"],
            h0=0.0035,
            scale=0.001,
            domain_mode="cht_coupled",
            config=cfg,
        )
        assert 5_000_000 <= mesh.num_cells <= int(target_cells * 1.5)


class TestE2EMesherTier4Scenario6SnappyMultiRegionHeater5RegionCht:
    """Scenario 6: snappyMultiRegionHeater 5-Region CHT Benchmark (mappedWall + regionProperties + checkMesh)."""

    def test_s06_snappy_multi_region_heater_5_region_meshing_and_conformal_interfaces(
        self,
        snappy_heater_surfaces: Dict[str, SurfaceMesh],
        tmp_path: Path,
    ) -> None:
        """Verify snappyMultiRegionHeater 5 STL regions mesh into a 5-region CHT domain with 1:1 twin faces."""
        expected_stems = {"bottomAir", "heater", "leftSolid", "rightSolid", "topAir"}
        assert set(snappy_heater_surfaces.keys()) == expected_stems

        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_multi_region(snappy_heater_surfaces, h0=0.015)
        assert_unstructured_mesh_valid(mesh)
        assert mesh.num_regions == 5
        assert_cht_1to1_conformal(mesh, expected_regions=5)


class TestE2EMesherTier4Scenario7CpuCabinetAndMrfRegion:
    """Scenario 7: cpuCabinet Electronics Cooling + MRF_region (CHT + Thermal/Turbulence + checkMesh)."""

    def test_s07_cpu_cabinet_and_mrf_region_meshing_and_thermal_case(
        self,
        cpu_cabinet_surfaces: Dict[str, SurfaceMesh],
        tmp_path: Path,
    ) -> None:
        """Verify cpuCabinet (Cabinet_withMesh + MRF_region) meshes cleanly and generates buoyant thermal case."""
        assert set(cpu_cabinet_surfaces.keys()) == {"Cabinet_withMesh", "MRF_region"}
        cabinet = cpu_cabinet_surfaces["Cabinet_withMesh"]
        mrf = cpu_cabinet_surfaces["MRF_region"]
        assert len(cabinet.patches) == 7

        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.03, h_min=0.015, h_max=0.06),
        )
        m_cab = engine.mesh_hexcore(cabinet, h0=0.03, domain_mode="internal_fluid", config=cfg)
        m_mrf = engine.mesh_hexcore(mrf, h0=0.02, domain_mode="internal_fluid")
        assert_unstructured_mesh_valid(m_cab)
        assert_unstructured_mesh_valid(m_mrf)

        reg = ModuleRegistry()
        reg.enable_module("Energy", variant="sensibleEnthalpy")
        reg.enable_module("Viscous & Turbulence", variant="kOmegaSST")
        gen = CaseGenerator(reg)
        case_dir = gen.create_case(
            "cpu_cabinet_case",
            str(tmp_path),
            solver_name="buoyantPimpleFoam",
            patches={p: "wall" for p in cabinet.patches},
        )
        assert (case_dir / "0" / "T").is_file()


class TestE2EMesherTier4Scenario8OppositeWallsNarrowGapCollision:
    """Scenario 8: opposite_walls Narrow-Gap Boundary Layer Collision & Step-Down (gap0/1, gap_transformed0/1)."""

    def test_s08_opposite_walls_narrow_gap_medial_axis_bl_collision_avoidance(
        self,
        opposite_walls_surfaces: Dict[str, SurfaceMesh],
    ) -> None:
        """Verify opposite_walls gap0/gap1 and gap_transformed0/1 inflate without prism collision or inversion."""
        g0 = opposite_walls_surfaces["gap0"]
        g1 = opposite_walls_surfaces["gap1"]
        gt0 = opposite_walls_surfaces["gap_transformed0"]
        gt1 = opposite_walls_surfaces["gap_transformed1"]

        # Compute minimum gap distance between gap0 and gap1 vertices
        dists = np.linalg.norm(g0.vertices[:, None, :] - g1.vertices[None, :, :], axis=2)
        min_gap = float(np.min(dists))
        assert min_gap > 0.0

        # Request BL total thickness exceeding half-gap so medial-axis clearance must retract/chop
        inf_cfg = InflationConfig(
            num_layers=5,
            first_layer_height=min_gap * 0.25,
            growth_rate=1.3,
            pinching=True,
        )
        engine = MeshEngine(use_native=True)
        clearances_0 = np.min(dists, axis=1) * 0.45
        bl_0 = engine.inflate_boundary_layers(
            g0.vertices,
            g0.faces.tolist(),
            config=inf_cfg,
            clearances=clearances_0,
        )
        assert bl_0.num_cells > 0
        assert np.all(bl_0.cell_volumes > 0.0)

        dists_t = np.linalg.norm(gt0.vertices[:, None, :] - gt1.vertices[None, :, :], axis=2)
        clearances_t0 = np.min(dists_t, axis=1) * 0.45
        bl_t0 = engine.inflate_boundary_layers(
            gt0.vertices,
            gt0.faces.tolist(),
            config=inf_cfg,
            clearances=clearances_t0,
        )
        assert bl_t0.num_cells > 0
        assert np.all(bl_t0.cell_volumes > 0.0)


class TestE2EMesherTier4Scenario9CycloneInternalSwirlingFlow:
    """Scenario 9: cyclone.stl.gz Internal Swirling Flow (internal_fluid + J_ENS + kOmegaSST/LES + checkMesh)."""

    def test_s09_cyclone_internal_fluid_octree_cut_cells_and_les_case(
        self,
        cyclone_surface: SurfaceMesh,
        tmp_path: Path,
    ) -> None:
        """Verify cyclone.stl.gz meshes in internal_fluid mode with positive J_ENS and pairs with LES case."""
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.15, h_min=0.075, h_max=0.30),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(cyclone_surface, h0=0.15, domain_mode="internal_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh, require_positive_j_ens=True)
        assert_j_ens_geometric_not_dummy(mesh)
        assert_linear_field_exact(mesh)

    @pytest.mark.wsl_checkmesh
    def test_s09_cyclone_wsl_checkmesh_validation(
        self,
        cyclone_surface: SurfaceMesh,
        tmp_path: Path,
        require_wsl_checkmesh: Dict[str, Any],
    ) -> None:
        """Verify exported cyclone.stl.gz polyMesh passes WSL2 OpenFOAM checkMesh."""
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.15, h_min=0.075, h_max=0.30),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(cyclone_surface, h0=0.15, domain_mode="internal_fluid", config=cfg)
        case_dir = tmp_path / "cyclone_checkmesh"
        assert_polymesh_roundtrip(mesh, case_dir)
        runner = WslSimulationRunner()
        res = runner.run_mesh(str(case_dir), mesh_command="checkMesh", check=True)
        assert res.returncode == 0 and "Mesh OK" in res.stdout


class TestE2EMesherTier4Scenario10MultiPhysicsFullChain:
    """Scenario 10: Multi-Physics Full Chain across Fluid, Chemical, MHD, FSI, CHT + CAbiSpan/.mphys/IPC."""

    def test_s10_full_multiphysics_pipeline_cad_to_cabi_mphys_and_5_solvers(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify end-to-end CAD -> native hexcore -> CAbiSpan -> .mphys -> polyMesh -> 5 solver families."""
        surf = load_cached_surface("TestCAD_obj")
        surf.vertices = surf.vertices * 0.001
        bbox = surf.get_bounding_box()
        h0 = float(bbox.diagonal) * 0.25

        mesher = MeshEngine(use_native=True, strict_native=True)
        mesh = mesher.mesh_hexcore(surf, h0=h0, domain_mode="internal_fluid")
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)
        assert_linear_field_exact(mesh)

        # 1. CAbiSpan 64-byte alignment
        lake = export_mesh_to_soa_lake(mesh)
        for name, (span, _) in lake.items():
            assert span.is_aligned_64(), f"CAbiSpan {name} not 64B aligned"

        # 2. .mphys container persistence
        mphys_path = tmp_path / "multiphysics_s10.mphys"
        with MPhysWriter(mphys_path, compression_method=CompressionType.ZLIB) as writer:
            writer.write_mesh_epoch(mesh)
            writer.write_field_step(
                0,
                0.0,
                1e-4,
                {
                    "p": np.full(mesh.num_cells, 101325.0, dtype=np.float64),
                    "T": np.full(mesh.num_cells, 300.0, dtype=np.float64),
                },
            )
        with MPhysReader(mphys_path) as reader:
            assert len(reader.read_mesh_epoch()["cell_volumes"]) == mesh.num_cells

        # 3. Multi-solver CaseGenerator compatibility across Fluid, Reacting, MHD, FSI, CHT
        solver_configs = [
            ("fluid_simple", "simpleFoam", ["Viscous & Turbulence"]),
            ("chem_reacting", "reactingFoam", ["Energy", "Species & Chemistry"]),
            ("em_mhd", "mhdFoam", ["Magnetohydrodynamics (MHD)"]),
            ("mech_fsi", "fsiFoam", ["Monolithic Eulerian FSI"]),
            ("thermal_cht", "chtMultiRegionFoam", ["Energy", "Radiation"]),
        ]
        for case_name, solver_name, mod_names in solver_configs:
            reg = ModuleRegistry()
            for m_name in mod_names:
                reg.enable_module(m_name)
            gen = CaseGenerator(reg)
            c_dir = gen.create_case(
                case_name,
                str(tmp_path),
                solver_name=solver_name,
                patches={"wall": "wall"},
            )
            assert (c_dir / "system" / "controlDict").is_file()
            assert (c_dir / "0" / "U").is_file()
