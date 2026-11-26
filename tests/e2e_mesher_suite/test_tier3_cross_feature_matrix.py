"""Tier 3 pairwise combinatorial cross-feature E2E tests across Steps 1-3, I/O, IPC, and Multi-Physics (>=20 tests)."""

from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import Any, Dict

import numpy as np
import pytest

from backend.core.case_generator import CaseGenerator
from backend.meshing.invariants import assert_fvm_invariants
from backend.core.materials import MaterialDatabase
from backend.geometry.models import SurfaceMesh
from backend.geometry.pipeline import CADGeometryEngine
from backend.ipc.binary_buffers import (
    pack_geometry_buffer,
    pack_mesh_quality_buffer,
    unpack_geometry_buffer,
    unpack_mesh_quality_buffer,
)
from backend.ipc.service import IPCService
from backend.meshing import compute_bandwidth, rcm_reorder
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    extract_cht_twin_faces,
    generate_hexcore_native,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CellType,
    CurvatureSizingConfig,
    FarfieldPadding,
    InflationConfig,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.meshing.polymesh_io import (
    export_openfoam_multiregion_polymesh,
    export_openfoam_polymesh,
    extrude_2d_to_3d_slab,
)
from backend.native_rust.c_abi_lake import export_fields_to_soa_lake, export_mesh_to_soa_lake
from backend.native_rust.foam_reader import FoamReader
from backend.native_rust.mphys_container import CompressionType, MPhysReader, MPhysWriter
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
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier3]


class TestE2EMesherTier3CrossFeatureMatrix:
    """Pairwise cross-feature combination tests across F1-F16 (>=20 tests)."""

    def test_t3_01_f2_external_fluid_plus_f1_j_ens_plus_f6_linear_exactness(
        self,
        naca0012_surface: SurfaceMesh,
    ) -> None:
        """F2 + F1 + F6: Graded external_fluid farfield on NACA0012 with J_ENS and linear-field exactness."""
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=2.0, downstream=3.0, lateral_y=2.0, lateral_z=2.0),
            sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=1.0, grading=0.35),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(naca0012_surface, h0=0.25, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)
        assert_linear_field_exact(mesh)

    def test_t3_02_f3_solid_only_plus_f5_cabi_config_plus_f6_rcm_reorder(self) -> None:
        """F3 + F5 + F6: solid_only cut-cell octree with C-ABI sizing forwarding and RCM bandwidth reordering."""
        v, f = create_unit_cube_arrays()
        ang = np.deg2rad(18.0)
        rot = np.array(
            [[np.cos(ang), -np.sin(ang), 0.0], [np.sin(ang), np.cos(ang), 0.0], [0.0, 0.0, 1.0]],
            dtype=np.float64,
        )
        v_rot = ((v - 0.5) * np.array([0.04, 0.02, 0.006])) @ rot.T
        cfg = MeshingConfig(
            domain_mode="solid_only",
            rcm_reorder=True,
            sizing=CurvatureSizingConfig(h0=0.005, h_min=0.0025, h_max=0.01, alpha=0.18, beta=0.22),
        )
        mesh = generate_hexcore_native(v_rot, f, h0=0.005, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        reordered, _ = rcm_reorder(mesh)
        assert compute_bandwidth(reordered.owner, reordered.neighbour, reordered.num_cells) <= compute_bandwidth(
            mesh.owner, mesh.neighbour, mesh.num_cells
        )

    def test_t3_03_f9_cht_twin_faces_plus_f13_multiregion_polymesh_plus_f15_foam_reader(
        self,
        tmp_path: Path,
    ) -> None:
        """F9 + F13 + F15: 2-region CHT twin-face extraction -> multi-region polyMesh export -> FoamReader reload."""
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
        cht_mesh = extract_cht_twin_faces(
            compute_difvm_topology_python(pts, cells, regions=np.array([0, 1], dtype=np.int32))
        )
        assert_cht_1to1_conformal(cht_mesh, expected_regions=2)

        m_reg0 = compute_difvm_topology_python(pts[:8], [[0, 1, 2, 3, 4, 5, 6, 7]])
        m_reg0.boundaries = {"coupledWall_to_solid": (0, 1), "wall": (1, 5)}
        m_reg1 = compute_difvm_topology_python(pts, [[1, 8, 9, 2, 5, 10, 11, 6]])
        m_reg1.boundaries = {"coupledWall_to_fluid": (0, 1), "wall": (1, 5)}

        case_dir = tmp_path / "t3_03_cht"
        exported = export_openfoam_multiregion_polymesh(
            case_dir,
            regions={"fluid": m_reg0, "solid": m_reg1},
            region_properties={"fluid": ["fluid"], "solid": ["solid"]},
        )
        fm_fluid = FoamReader.load_mesh(exported["fluid"])
        fm_solid = FoamReader.load_mesh(exported["solid"])
        assert fm_fluid.n_cells == 1 and fm_solid.n_cells == 1

    def test_t3_04_f10_boundary_layer_inflation_plus_f11_merging_plus_f1_j_ens(self) -> None:
        """F10 + F11 + F1: Boundary layer inflation combined with small-cell merging and J_ENS check."""
        v, f = create_unit_cube_arrays()
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            small_cell_ratio=0.20,
            sizing=CurvatureSizingConfig(h0=0.5, h_min=0.25, h_max=0.5),
            inflation=InflationConfig(num_layers=2, first_layer_height=0.02, growth_rate=1.2),
        )
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)

    def test_t3_05_f6_native_hexcore_plus_f15_cabi_span_lake_plus_mphys_roundtrip(
        self,
        tmp_path: Path,
    ) -> None:
        """F6 + F15: Native hexcore mesh -> 64B-aligned CAbiSpan SoA lake -> .mphys container round-trip."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        lake = export_mesh_to_soa_lake(mesh)
        for span, _ in lake.values():
            assert span.is_aligned_64()

        mphys_file = tmp_path / "t3_05.mphys"
        temp_field = np.linspace(300.0, 800.0, mesh.num_cells, dtype=np.float64)
        with MPhysWriter(mphys_file, compression_method=CompressionType.ZLIB) as writer:
            writer.write_mesh_epoch(mesh)
            writer.write_field_step(0, 0.0, 1e-4, {"T": temp_field})

        with MPhysReader(mphys_file) as reader:
            epoch = reader.read_mesh_epoch()
            assert np.isclose(np.sum(epoch["cell_volumes"]), np.sum(mesh.cell_volumes))
            step0 = reader.read_field_step(0)
            assert np.allclose(np.sort(step0["T"]), np.sort(temp_field))

    def test_t3_06_f15_cad_ingest_and_prepare_plus_f3_solid_only_plus_f13_polymesh(
        self,
        tmp_path: Path,
    ) -> None:
        """F15 + F3 + F13: CADGeometryEngine.ingest_and_prepare -> MeshEngine solid_only -> polyMesh roundtrip."""
        cad_eng = CADGeometryEngine()
        case_dir = tmp_path / "t3_06_case"
        exported_stl, audit = cad_eng.ingest_and_prepare(
            ASSET_PATHS["TestCAD_obj"],
            case_dir,
            scale_factor=0.001,
        )
        assert exported_stl.is_file()
        assert audit.volume > 0.0

        mesher = MeshEngine(use_native=True, strict_native=True)
        bbox = cad_eng.current_mesh.get_bounding_box()  # type: ignore[union-attr]
        h0 = float(bbox.diagonal) * 0.25
        mesh = mesher.mesh_hexcore(
            cad_eng.current_mesh,  # type: ignore[arg-type]
            h0=h0,
            domain_mode="solid_only",
        )
        assert_unstructured_mesh_valid(mesh)
        assert_fvm_invariants(mesh, expect_patches=("wall",))
        assert_polymesh_roundtrip(mesh, case_dir)

    def test_t3_07_f14_turbulence_komegasst_plus_f13_polymesh_plus_f15_foam_field_io(
        self,
        tmp_path: Path,
    ) -> None:
        """F14 + F13 + F15: Mesh export + CaseGenerator kOmegaSST + FoamReader.load_field & write_field."""
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
        mesh = compute_difvm_topology_python(
            pts,
            [[0, 1, 2, 3, 4, 5, 6, 7], [1, 8, 9, 2, 5, 10, 11, 6]],
        )
        mesh.boundaries = {"inlet": (1, 5), "outlet": (6, 5)}

        reg = ModuleRegistry()
        reg.enable_module("Viscous & Turbulence", variant="kOmegaSST")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            "t3_07_turb",
            str(tmp_path),
            solver_name="simpleFoam",
            patches={"inlet": "patch", "outlet": "patch"},
        )
        export_openfoam_polymesh(mesh, case_path)

        p_out = case_path / "0" / "p_custom"
        FoamReader.write_field(
            p_out,
            field_name="p",
            dimensions=[0, 2, -2, 0, 0, 0, 0],
            internal_field=np.array([10.0, 20.0], dtype=np.float64),
        )
        loaded_p = FoamReader.load_field(p_out, n_cells=2)
        assert np.allclose(loaded_p["internal_field"], [10.0, 20.0])

    def test_t3_08_f14_chemical_reacting_plus_energy_thermophysical_coexistence(
        self,
        tmp_path: Path,
    ) -> None:
        """F14 (Energy + Species & Chemistry): Verify custom species fields and non-conflicting thermophysicalProperties."""
        reg = ModuleRegistry()
        reg.enable_module("Energy", variant="sensibleEnthalpy")
        reg.enable_module("Species & Chemistry", variant="finiteRateKinetics")
        sp_mod = reg.get_module("Species & Chemistry")
        assert sp_mod is not None
        sp_mod.config.parameters["species"] = ["CH4", "O2", "CO2", "H2O", "N2"]

        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            "t3_08_reacting",
            str(tmp_path),
            solver_name="reactingFoam",
            patches={"inlet": "patch", "outlet": "patch", "wall": "wall"},
        )
        for sp_name in ("CH4", "O2", "CO2", "H2O", "N2"):
            assert (case_path / "0" / sp_name).is_file()
        thermo_txt = (case_path / "constant" / "thermophysicalProperties").read_text(encoding="utf-8")
        assert "multiComponentMixture" in thermo_txt, (
            "EnergyModule overwrote SpeciesChemistryModule multiComponentMixture in thermophysicalProperties"
        )

    def test_t3_09_f14_mhd_module_plus_materials_plus_case_generator_initial_fields(
        self,
        tmp_path: Path,
    ) -> None:
        """F14 (MHD + Materials + CaseGenerator): Verify mhdFoam case emits 0/B and 0/electricPotential_V."""
        db = MaterialDatabase()
        mat = db.get_material("copper") or db.list_materials(category="solid")[0]
        assert mat is not None

        reg = ModuleRegistry()
        reg.enable_module("Magnetohydrodynamics (MHD)")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            "t3_09_mhd",
            str(tmp_path),
            solver_name="mhdFoam",
            patches={"inlet": "patch", "outlet": "patch", "wall": "wall"},
        )
        assert (case_path / "constant" / "mhdProperties").is_file()
        assert (case_path / "0" / "B").is_file(), "CaseGenerator failed to emit 0/B for MHD module"
        assert (case_path / "0" / "electricPotential_V").is_file(), (
            "CaseGenerator failed to emit 0/electricPotential_V for MHD module"
        )

    def test_t3_10_f14_monolithic_fsi_plus_plic_3d_plus_case_generator_fields(
        self,
        tmp_path: Path,
    ) -> None:
        """F14 (Monolithic FSI + PLIC 3D + CaseGenerator): Verify fsiFoam case emits 0/phi_solid and 0/B_tensor."""
        fsi = EulerianMonolithicFSI(rho_s=2700.0, rho_f=1000.0, G_s=5e4)
        normal, alpha = fsi.reconstruct_plic_3d(0.5, np.array([1.0, 0.0, 0.0]), dx=0.1)
        assert np.isclose(np.linalg.norm(normal), 1.0)
        assert np.isfinite(alpha)

        reg = ModuleRegistry()
        reg.enable_module("Monolithic Eulerian FSI")
        gen = CaseGenerator(reg)
        case_path = gen.create_case(
            "t3_10_fsi",
            str(tmp_path),
            solver_name="fsiFoam",
            patches={"wall": "wall"},
        )
        assert (case_path / "constant" / "fsiProperties").is_file()
        assert (case_path / "0" / "phi_solid").is_file(), (
            "CaseGenerator failed to emit 0/phi_solid for Monolithic Eulerian FSI"
        )
        assert (case_path / "0" / "B_tensor").is_file(), (
            "CaseGenerator failed to emit 0/B_tensor for Monolithic Eulerian FSI"
        )

    def test_t3_11_f15_ipc_geometry_import_to_mesh_generate_non_dummy_quality(
        self,
        tmp_path: Path,
    ) -> None:
        """F15 + F1 + F6: IPCService geometry.import -> mesh.generate uses hexcore and returns real quality metrics."""
        svc = IPCService()
        imp_res = svc.endpoint_geometry_import(
            {"file_path": str(ASSET_PATHS["TestCAD_obj"]), "scale_factor": 0.001}
        )
        geom_id = imp_res["geometry_id"]
        msh_res = svc.endpoint_mesh_generate(
            {"geometry_id": geom_id, "base_cell_size_m": 0.08, "domain_mode": "solid_only"}
        )
        assert msh_res["cell_count"] > 0
        # Verify quality buffer is populated with real non-dummy values
        q_buf_path = msh_res.get("quality_buffer_path") or msh_res.get("buffer_path")
        if q_buf_path and Path(q_buf_path).is_file():
            unpacked = unpack_mesh_quality_buffer(Path(q_buf_path).read_bytes())
            assert np.all(unpacked["volume"] > 0.0)
            assert not np.all(unpacked["non_orthogonality"] == 0.0), (
                "IPCService.endpoint_mesh_generate emitted all-zero dummy non_orthogonality in CFDMSH02"
            )

    @pytest.mark.slow
    def test_t3_12_f7_gci_3grid_hierarchy_plus_f6_linear_exactness_on_all_levels(self) -> None:
        """F7 + F6: All 3 grids in MeshEngine.generate_gci_hierarchy satisfy linear-field exactness."""
        v, f = create_unit_cube_arrays()
        surf = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
        engine = MeshEngine(use_native=True, strict_native=True)
        hierarchy = engine.generate_gci_hierarchy(
            surf,
            base_h0=0.5,
            r=1.25,
            config=MeshingConfig(domain_mode="internal_fluid"),
        )
        for level_mesh in hierarchy:
            assert_unstructured_mesh_valid(level_mesh)
            assert_linear_field_exact(level_mesh)

    def test_t3_13_f13_extrude_2d_polygon_to_3d_slab_plus_polymesh_roundtrip(
        self,
        tmp_path: Path,
    ) -> None:
        """F13 + F15: MeshEngine.mesh_2d_polygon -> extrude_2d_to_3d_slab -> polyMesh round-trip."""
        poly_xy = np.array(
            [[0.0, 0.0], [1.0, 0.0], [1.0, 0.6], [0.0, 0.6]],
            dtype=np.float64,
        )
        engine = MeshEngine(use_native=True)
        cfg_2d = MeshingConfig(sizing=CurvatureSizingConfig(h0=0.25, h_min=0.15, h_max=0.35))
        mesh_2d = engine.mesh_2d_polygon(poly_xy, config=cfg_2d)
        slab_3d = extrude_2d_to_3d_slab(mesh_2d, thickness=0.1)
        assert slab_3d.topological_dim == 3
        assert_unstructured_mesh_valid(slab_3d)
        assert_polymesh_roundtrip(slab_3d, tmp_path / "t3_13_slab")

    def test_t3_14_f9_cyclic_pair_extraction_plus_f6_topology_validation(self) -> None:
        """F9 + F6: Rotational cyclic pair detection on a 90-degree sector mesh."""
        from backend.meshing.difvm_topology import extract_cyclic_pairs

        pts = np.array(
            [
                [1.0, 0.0, 0.0],
                [2.0, 0.0, 0.0],
                [0.0, 2.0, 0.0],
                [0.0, 1.0, 0.0],
                [1.0, 0.0, 1.0],
                [2.0, 0.0, 1.0],
                [0.0, 2.0, 1.0],
                [0.0, 1.0, 1.0],
            ],
            dtype=np.float64,
        )
        mesh = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        pairs = extract_cyclic_pairs(mesh, sector_angle_rad=float(np.pi / 2.0), tolerance=1e-3)
        assert len(pairs) >= 1

    @pytest.mark.slow
    def test_t3_15_f6_openmp_thread_invariance_on_cut_cell_polyhedra_plus_f1_j_ens(
        self,
        restore_native_env: None,
    ) -> None:
        """F6 + F1 + F11: Thread-count determinism (1 vs 2 threads) on cut-cell polyhedra with J_ENS match."""
        from tests.test_asme_vv import create_geodesic_icosphere

        v_sph, f_sph = create_geodesic_icosphere(radius=0.45, subdivisions=2)
        os.environ["OMP_NUM_THREADS"] = "1"
        m1 = generate_hexcore_native(
            np.asarray(v_sph, dtype=np.float64),
            np.asarray(f_sph, dtype=np.int32),
            h0=0.25,
            domain_mode="internal_fluid",
        )
        j1 = assert_j_ens_geometric_not_dummy(m1)

        os.environ["OMP_NUM_THREADS"] = "2"
        m2 = generate_hexcore_native(
            np.asarray(v_sph, dtype=np.float64),
            np.asarray(f_sph, dtype=np.int32),
            h0=0.25,
            domain_mode="internal_fluid",
        )
        j2 = assert_j_ens_geometric_not_dummy(m2)

        assert m1.num_cells == m2.num_cells
        assert np.allclose(j1, j2, atol=1e-13)

    def test_t3_16_f12_fluent_h5_summary_plus_f9_blade_rev1_zone_topology_alignment(
        self,
        require_fluent_h5: Dict[str, Path],
        blade_rev1_surface: SurfaceMesh,
    ) -> None:
        """F12 + F9: Cross-check BladeRev1CFD 4-body CAD components against FLTG.msh.h5 10-zone topology."""
        from tests.e2e_mesher_suite.oracles import read_fluent_msh_h5_summary

        summary = read_fluent_msh_h5_summary(require_fluent_h5["FLTG"])
        n_comp = count_surface_components_scipy(
            blade_rev1_surface.vertices,
            blade_rev1_surface.faces,
        )
        assert n_comp == 4
        zone_names = summary["cell_zones"]["zone_names"]
        assert "blade" in zone_names
        assert any("leadingchamber" in z for z in zone_names)
        assert any("middlechamber" in z for z in zone_names)
        assert any("trailingchamber" in z for z in zone_names)

    def test_t3_17_f15_ipc_case_validate_and_generate_across_solver_families(
        self,
        tmp_path: Path,
    ) -> None:
        """F15 + F14: IPCService case.validate and case.generate across reacting, multiphase, and CHT."""
        svc = IPCService()
        for mods, expected_solver in [
            (["Energy", "Species & Chemistry"], "reactingFoam"),
            (["Multiphase"], "interFoam"),
        ]:
            val_res = svc.endpoint_case_validate({"active_modules": mods})
            assert val_res.get("valid") is True
            assert val_res.get("recommended_solver") == expected_solver

        gen_res = svc.endpoint_case_generate(
            {
                "case_name": "ipc_case_t3_17",
                "target_dir": str(tmp_path),
                "solver_name": "chtMultiRegionFoam",
                "active_modules": ["Energy", "Viscous & Turbulence"],
            }
        )
        assert Path(gen_res["case_dir"]).is_dir()

    def test_t3_18_f15_foam_reader_zero_gradient_owner_cell_fallback_with_polymesh(
        self,
        tmp_path: Path,
    ) -> None:
        """F15 + F13: FoamReader.load_field resolves zeroGradient boundary values from owner cells."""
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
        mesh = compute_difvm_topology_python(
            pts,
            [[0, 1, 2, 3, 4, 5, 6, 7], [1, 8, 9, 2, 5, 10, 11, 6]],
        )
        mesh.boundaries = {"inlet": (1, 5), "outlet": (6, 5)}
        poly_dir = export_openfoam_polymesh(mesh, tmp_path / "t3_18_case")
        foam_mesh = FoamReader.load_mesh(poly_dir)

        t_file = tmp_path / "t3_18_case" / "0" / "T"
        FoamReader.write_field(
            t_file,
            field_name="T",
            dimensions=[0, 0, 0, 1, 0, 0, 0],
            internal_field=np.array([350.0, 450.0], dtype=np.float64),
            boundary_fields={
                "inlet": {"type": "zeroGradient"},
                "outlet": {"type": "calculated"},
            },
        )
        loaded = FoamReader.load_field(t_file, n_cells=2, mesh=foam_mesh)
        assert loaded["boundary_fields"]["inlet"]["source"] == "owner_cell_fallback"
        assert len(loaded["boundary_fields"]["inlet"]["values"]) == 5

    def test_t3_19_f8_wsl_runner_log_stream_plus_f15_telemetry_formatting(
        self,
        tmp_path: Path,
    ) -> None:
        """F8 + F15: WslSimulationRunner.parse_log_file -> IPC IterationEvent telemetry serialization."""
        from backend.ipc.telemetry import IterationEvent

        log_path = tmp_path / "log.simpleFoam"
        log_path.write_text(
            "Time = 1\n"
            "Courant Number mean: 0.02 max: 0.15\n"
            "DICPCG:  Solving for p, Initial residual = 0.08, Final residual = 1e-07, No Iterations 12\n",
            encoding="utf-8",
        )
        runner = WslSimulationRunner()
        parsed = runner.parse_log_file(str(log_path))
        assert len(parsed) == 1
        evt = IterationEvent(
            step=1,
            time=parsed[0].time,
            dt=1.0,
            courant_max=parsed[0].courant_max or 0.0,
            residuals=parsed[0].residuals,
        )
        payload = evt.to_dict()
        assert payload["type"] == "iter"
        assert np.isclose(payload["courant_max"], 0.15)

    @pytest.mark.slow
    def test_t3_20_f1_f3_f6_f13_end_to_end_step_cad_to_validated_polymesh(
        self,
        tmp_path: Path,
    ) -> None:
        """F1 + F3 + F6 + F13 + F15: STEP AP214 load -> scale -> native solid_only hexcore -> linear exact -> polyMesh."""
        cad = CADGeometryEngine()
        cad.load(ASSET_PATHS["TestCAD_stp"])
        cad.scale(0.001)
        assert cad.current_mesh is not None
        bbox = cad.current_mesh.get_bounding_box()
        h0 = float(bbox.diagonal) * 0.25

        mesher = MeshEngine(use_native=True, strict_native=True)
        mesh = mesher.mesh_hexcore(cad.current_mesh, h0=h0, domain_mode="solid_only")
        assert_unstructured_mesh_valid(mesh)
        assert_j_ens_geometric_not_dummy(mesh)
        assert_linear_field_exact(mesh)
        assert_fvm_invariants(mesh, expect_patches=("wall",))
        assert_polymesh_roundtrip(mesh, tmp_path / "t3_20_step_case")
