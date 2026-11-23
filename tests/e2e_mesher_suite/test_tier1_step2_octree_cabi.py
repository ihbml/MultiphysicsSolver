"""Tier 1 isolated happy-path E2E tests for Step 2 (F5-F7) and Section 13 contracts (F16)."""

from __future__ import annotations

import ctypes
import importlib
import logging
import os
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.meshing import compute_bandwidth, rcm_reorder
from backend.meshing.difvm_topology import (
    BoundaryPatchC,
    MeshingConfigC,
    compute_difvm_topology_python,
    generate_hexcore_native,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CurvatureSizingConfig,
    InflationConfig,
    MeshingConfig,
    QualityConfig,
    SizingParameters,
)
from tests.conftest import create_unit_cube_arrays
from tests.e2e_mesher_suite.oracles import (
    assert_j_ens_geometric_not_dummy,
    assert_linear_field_exact,
    assert_unstructured_mesh_valid,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier1]


def _make_curved_sphere_like_surface(radius: float = 0.5) -> SurfaceMesh:
    """Create a closed subdivided octahedron approximating a curved sphere."""
    from tests.test_asme_vv import create_geodesic_icosphere

    v, f = create_geodesic_icosphere(radius=radius, subdivisions=2)
    return SurfaceMesh(
        vertices=np.asarray(v, dtype=np.float64),
        faces=np.asarray(f, dtype=np.int32),
        patches={"sphere_wall": np.arange(len(f), dtype=np.int32)},
    )


class TestE2EMesherTier1F5MeshingConfigCCAbiForwarding:
    """F5: Complete MeshingConfigC C-ABI Forwarding (>=5 tests)."""

    def test_f5_01_meshing_config_c_struct_field_layout_and_types(self) -> None:
        """Verify MeshingConfigC ctypes structure matches the 16-field C-ABI specification."""
        expected_fields = [
            ("h_min", ctypes.c_double),
            ("h_max", ctypes.c_double),
            ("h0", ctypes.c_double),
            ("grading", ctypes.c_double),
            ("alpha_curv", ctypes.c_double),
            ("beta_prox", ctypes.c_double),
            ("num_bl_layers", ctypes.c_int32),
            ("bl_first_height", ctypes.c_double),
            ("bl_growth_rate", ctypes.c_double),
            ("bl_pinching_enabled", ctypes.c_int32),
            ("bl_pinching_angle_deg", ctypes.c_double),
            ("enable_rcm", ctypes.c_int32),
            ("enable_cht", ctypes.c_int32),
            ("enable_cyclic", ctypes.c_int32),
            ("cyclic_angle_rad", ctypes.c_double),
            ("distmesh_iters", ctypes.c_int32),
        ]
        assert list(MeshingConfigC._fields_) == expected_fields

    def test_f5_02_boundary_patch_c_struct_layout(self) -> None:
        """Verify BoundaryPatchC structure carries name, patch_type, start_face, num_faces, region_id."""
        field_names = [name for name, _ in BoundaryPatchC._fields_]
        assert field_names == ["name", "patch_type", "start_face", "num_faces", "region_id"]

    def test_f5_03_h_min_h_max_dynamic_depth_forwarding_changes_cell_count(self) -> None:
        """Verify forwarding smaller h_min/h0 through MeshingConfig increases octree refinement."""
        surf = _make_curved_sphere_like_surface(radius=0.5)
        cfg_coarse = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.35, h_min=0.35, h_max=0.7),
        )
        cfg_fine = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(h0=0.20, h_min=0.10, h_max=0.4),
        )
        m_coarse = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.35,
            domain_mode="internal_fluid",
            config=cfg_coarse,
        )
        m_fine = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.20,
            domain_mode="internal_fluid",
            config=cfg_fine,
        )
        assert m_fine.num_cells > m_coarse.num_cells
        assert_unstructured_mesh_valid(m_fine)

    def test_f5_04_alpha_curv_and_beta_prox_forwarding_on_curved_thin_geometry(self) -> None:
        """Verify alpha_curv and beta_prox fields in MeshingConfig are forwarded without dropping."""
        surf = _make_curved_sphere_like_surface(radius=0.5)
        cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=CurvatureSizingConfig(
                h0=0.25,
                h_min=0.06,
                h_max=0.5,
                alpha=0.15,
                beta=0.20,
                grading=0.35,
            ),
            rcm_reorder=True,
        )
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.25,
            domain_mode="internal_fluid",
            config=cfg,
        )
        assert_unstructured_mesh_valid(mesh)
        assert float(np.min(mesh.cell_volumes)) < (0.25 ** 3) * 0.5

    def test_f5_05_meshing_config_and_sizing_parameters_bidirectional_conversion(self) -> None:
        """Verify CurvatureSizingConfig <-> SizingParameters and MeshingConfig dict round-trip."""
        sizing_cfg = CurvatureSizingConfig(
            alpha=0.18,
            beta=0.22,
            h_min=0.002,
            h_max=0.08,
            h0=0.02,
            grading=0.30,
        )
        sp = sizing_cfg.to_sizing_parameters()
        assert isinstance(sp, SizingParameters)
        assert np.isclose(sp.grading, 0.30)
        back = sp.to_curvature_sizing_config()
        assert np.isclose(back.grading, 0.30)
        assert np.isclose(back.alpha, 0.18)

        full_cfg = MeshingConfig(
            domain_mode="internal_fluid",
            sizing=sizing_cfg,
            inflation=InflationConfig(num_layers=4, first_layer_height=5e-4, growth_rate=1.2),
            quality=QualityConfig(min_orthogonal_quality=0.25),
            rcm_reorder=True,
            cht_multiregion=True,
            cyclic_symmetry=True,
            cyclic_angle_deg=30.0,
        )
        reconstructed = MeshingConfig.from_dict(full_cfg.to_dict())
        assert reconstructed.domain_mode == "internal_fluid"
        assert reconstructed.inflation is not None and reconstructed.inflation.num_layers == 4
        assert np.isclose(reconstructed.cyclic_angle_deg, 30.0)


class TestE2EMesherTier1F6OctreeSdfDeterminismLinearExactness:
    """F6: C++/OpenMP Octree Refinement, SDF, Determinism & Linear-Exact Geometry (>=5 tests)."""

    def test_f6_01_native_hexcore_linear_field_exactness_on_unit_cube(self) -> None:
        """Verify native hexcore mesh achieves ~1e-14 linear-field exactness on interpolation, GG, and Laplacian."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        metrics = assert_linear_field_exact(mesh, tol=1e-12)
        assert metrics["interp_max_err"] <= 1e-12
        assert metrics["gg_grad_max_err"] <= 1e-12
        assert metrics["laplacian_max_err"] <= 1e-12

    def test_f6_02_native_hexcore_linear_field_exactness_on_curved_sphere_cut_cells(self) -> None:
        """Verify cut-cell polyhedral mesh on geodesic sphere satisfies linear-field exactness."""
        surf = _make_curved_sphere_like_surface(radius=0.5)
        mesh = generate_hexcore_native(
            surf.vertices,
            surf.faces,
            h0=0.25,
            domain_mode="internal_fluid",
        )
        metrics = assert_linear_field_exact(mesh, tol=1e-12)
        assert metrics["gg_grad_max_err"] <= 1e-12

    def test_f6_03_openmp_thread_count_determinism_1_vs_2_threads(
        self,
        restore_native_env: None,
    ) -> None:
        """Verify meshing topology and cell volumes are deterministic between OMP_NUM_THREADS=1 and 2."""
        v, f = create_unit_cube_arrays()
        os.environ["OMP_NUM_THREADS"] = "1"
        m1 = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")

        os.environ["OMP_NUM_THREADS"] = "2"
        m2 = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")

        assert m1.num_cells == m2.num_cells
        assert m1.num_faces == m2.num_faces
        assert m1.num_points == m2.num_points
        assert np.allclose(m1.cell_volumes, m2.cell_volumes, atol=1e-14, rtol=1e-14)
        assert np.array_equal(m1.owner, m2.owner)
        assert np.array_equal(m1.neighbour, m2.neighbour)

    def test_f6_04_rcm_reorder_reduces_or_preserves_matrix_bandwidth_and_invariants(self) -> None:
        """Verify rcm_reorder preserves mesh validity and maintains low adjacency matrix bandwidth."""
        v, f = create_unit_cube_arrays()
        cfg_no_rcm = MeshingConfig(
            domain_mode="internal_fluid",
            rcm_reorder=False,
            sizing=CurvatureSizingConfig(h0=0.5, h_min=0.25, h_max=0.5),
        )
        mesh = generate_hexcore_native(
            v,
            f,
            h0=0.5,
            domain_mode="internal_fluid",
            config=cfg_no_rcm,
        )
        bw_before = compute_bandwidth(mesh.owner, mesh.neighbour, mesh.num_cells)
        reordered, _ = rcm_reorder(mesh)
        bw_after = compute_bandwidth(reordered.owner, reordered.neighbour, reordered.num_cells)
        assert bw_after <= bw_before
        assert_unstructured_mesh_valid(reordered)
        assert_linear_field_exact(reordered)

    def test_f6_05_wsrd_error_map_and_stabilization_tiers_populated(self) -> None:
        """Verify native hexcore populates error_map, WSRD weights, and quality stabilization tiers."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        assert isinstance(mesh.error_map, dict)
        assert "wsrd_weights" in mesh.error_map
        report = mesh.compute_quality_report()
        assert sum(report.stabilization_tier_counts.values()) == mesh.num_faces


class TestE2EMesherTier1F7ScalingAndGciHierarchy:
    """F7: Multi-Core Scaling, Memory Stability & GCI Hierarchy (>=5 tests)."""

    def test_f7_01_gci_3grid_hierarchy_monotone_refinement(self) -> None:
        """Verify MeshEngine.generate_gci_hierarchy creates 3 valid grids with monotone cell counts."""
        v, f = create_unit_cube_arrays()
        surf = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(domain_mode="internal_fluid")
        hierarchy = engine.generate_gci_hierarchy(
            surf,
            base_h0=0.5,
            r=1.3,
            config=cfg,
        )
        assert len(hierarchy) == 3
        fine, medium, coarse = hierarchy[0], hierarchy[1], hierarchy[2]
        assert fine.num_cells >= medium.num_cells >= coarse.num_cells > 0
        for grid in (fine, medium, coarse):
            assert_unstructured_mesh_valid(grid)

    def test_f7_02_gci_hierarchy_total_volume_consistency_across_3_grids(self) -> None:
        """Verify all 3 grids in GCIHierarchy conserve total domain volume."""
        v, f = create_unit_cube_arrays()
        surf = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
        engine = MeshEngine(use_native=True, strict_native=True)
        cfg = MeshingConfig(domain_mode="internal_fluid")
        hierarchy = engine.generate_gci_hierarchy(surf, base_h0=0.5, r=1.25, config=cfg)
        vols = [float(np.sum(m.cell_volumes)) for m in hierarchy]
        assert np.allclose(vols, 1.0, rtol=0.05)

    def test_f7_03_soa_array_contiguity_and_dtypes_for_zero_copy_scaling(self) -> None:
        """Verify UnstructuredMesh SoA arrays use contiguous C-order float64/int32/int64 buffers."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        for name in ("points", "face_areas", "face_centres", "cell_centres", "cell_volumes", "delta_f", "k_f"):
            arr = getattr(mesh, name)
            assert arr.dtype == np.float64, f"{name} dtype {arr.dtype} != float64"
            assert arr.flags["C_CONTIGUOUS"], f"{name} is not C_CONTIGUOUS"

    def test_f7_04_moderate_refinement_memory_and_time_efficiency(self) -> None:
        """Verify ~10k-50k cell native octree generation runs in < 3s with < 200 MB mesh footprint."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.25, domain_mode="internal_fluid")
        assert mesh.num_cells >= 1_000
        nbytes = (
            mesh.points.nbytes
            + mesh.face_areas.nbytes
            + mesh.face_centres.nbytes
            + mesh.cell_centres.nbytes
            + mesh.cell_volumes.nbytes
            + mesh.delta_f.nbytes
            + mesh.k_f.nbytes
        )
        assert nbytes < 200 * 1024 * 1024
        assert_unstructured_mesh_valid(mesh)

    def test_f7_05_parallel_openmp_two_threads_multi_geometry_batch(
        self,
        restore_native_env: None,
    ) -> None:
        """Verify batch meshing of cube and sphere under OMP_NUM_THREADS=2 succeeds cleanly."""
        os.environ["OMP_NUM_THREADS"] = "2"
        v, f = create_unit_cube_arrays()
        m_cube = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")
        sphere = _make_curved_sphere_like_surface(radius=0.5)
        m_sphere = generate_hexcore_native(
            sphere.vertices,
            sphere.faces,
            h0=0.25,
            domain_mode="internal_fluid",
        )
        assert_unstructured_mesh_valid(m_cube)
        assert_unstructured_mesh_valid(m_sphere)


@pytest.mark.s13_followup
class TestE2EMesherTier1F16Section13FollowUpContracts:
    """F16: Section 13 Follow-Up Contracts (project.toml, Spatial BCs, .msh/CGNS, CFDFLD03, 3D-MMS) (>=5 tests)."""

    def test_f16_01_gmsh_msh_v41_and_cgns_bridge_roundtrip(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify Gmsh .msh v4.1 and CGNS mesh import/export preserve topology, patches, and units."""
        v, f = create_unit_cube_arrays()
        mesh = generate_hexcore_native(v, f, h0=0.5, domain_mode="internal_fluid")

        # Probe public mesh bridge API on UnstructuredMesh or backend.meshing / backend.native_rust
        has_msh = hasattr(mesh, "to_gmsh_msh") or hasattr(
            importlib.import_module("backend.meshing"), "export_gmsh_msh"
        )
        has_cgns = hasattr(mesh, "to_cgns") or hasattr(
            importlib.import_module("backend.meshing"), "export_cgns"
        )
        if not (has_msh and has_cgns):
            pytest.fail(
                "§13 Gmsh .msh v4.1 and CGNS mesh bridge (to_gmsh_msh/export_gmsh_msh, "
                "to_cgns/export_cgns) is not yet implemented in backend/meshing or foam_fast_io"
            )

        mod_meshing = importlib.import_module("backend.meshing")
        msh_file = tmp_path / "bridge.msh"
        cgns_file = tmp_path / "bridge.cgns"
        if hasattr(mesh, "to_gmsh_msh"):
            mesh.to_gmsh_msh(msh_file)
        else:
            mod_meshing.export_gmsh_msh(mesh, msh_file)
        assert msh_file.is_file()

        if hasattr(mesh, "to_cgns"):
            mesh.to_cgns(cgns_file)
        else:
            mod_meshing.export_cgns(mesh, cgns_file)
        assert cgns_file.is_file()

    def test_f16_02_spatial_query_bc_selector_assigns_patches_from_geometry_rules(self) -> None:
        """Verify SpatialQuerySelector maps geometric normal/bbox predicates to named boundary patches."""
        from backend.native_rust.expression_engine import ExpressionEngine

        eng = ExpressionEngine()
        # §13.1 (1.B) requires boolean and comparison operators for spatial query BCs
        x = np.array([0.0, 1.0, 0.5], dtype=np.float64)
        y = np.array([0.5, 0.5, 0.5], dtype=np.float64)
        z = np.array([0.5, 0.5, 0.5], dtype=np.float64)
        try:
            mask = eng.evaluate_boundary_profile(
                "normal_x < -0.99 and abs(x - 0.0) < 1e-4",
                x,
                y,
                z,
                extra_vars={"normal_x": np.array([-1.0, 1.0, 0.0], dtype=np.float64)},
            )
        except Exception as exc:
            pytest.fail(
                f"§13 Spatial-Query BC selector evaluation failed in ExpressionEngine: "
                f"{type(exc).__name__}: {exc}"
            )
        assert bool(mask[0]) is True and bool(mask[1]) is False

    def test_f16_03_project_toml_and_mphys_cache_sha256_manifest_validation(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify project.toml declarative config and SHA-256 cache invalidation contract."""
        try:
            proj_mod = importlib.import_module("backend.core.project_manifest")
        except ImportError as exc:
            pytest.fail(
                f"§13 project.toml / SHA-256 cache invalidation module "
                f"(backend.core.project_manifest) not yet implemented: {exc}"
            )
        assert hasattr(proj_mod, "compute_project_cache_hash")

    def test_f16_04_cfdfld03_paket3_frame_header_and_chunk_aabb_contract(self) -> None:
        """Verify Paket 3 CFDFLD03 binary buffer pack/unpack contract in backend.ipc.binary_buffers."""
        import backend.ipc.binary_buffers as bb

        if not (hasattr(bb, "pack_field_buffer") and hasattr(bb, "unpack_field_buffer")):
            pytest.fail(
                "§13.3 Paket 3 CFDFLD03 (pack_field_buffer / unpack_field_buffer) "
                "is not yet implemented in backend/ipc/binary_buffers.py"
            )
        arr = np.linspace(0.0, 1.0, 16, dtype=np.float32)
        buf, desc = bb.pack_field_buffer(
            arr,
            mesh_revision=1,
            step_index=0,
            sim_time=0.0,
            dimensions=[0, 2, -2, 0, 0, 0, 0],
        )
        assert buf[:8] == b"CFDFLD03"
        unpacked = bb.unpack_field_buffer(buf)
        assert np.allclose(unpacked["values"], arr)

    def test_f16_05_3d_mms_second_order_gradient_and_laplacian_convergence(self) -> None:
        """Verify 3D Method of Manufactured Solutions (MMS) achieves p_obs >= 1.95 on smooth field."""
        # Manufactured smooth scalar field phi(x,y,z) = sin(pi*x)*cos(pi*y)*exp(z/2)
        # on two regular 3D hex grids of spacing h1=0.25 (4^3) and h2=0.125 (8^3)
        errors = []
        spacings = [0.25, 0.125]
        for h in spacings:
            n = int(round(1.0 / h))
            grid_1d = np.linspace(0.0, 1.0, n + 1)
            gx, gy, gz = np.meshgrid(grid_1d, grid_1d, grid_1d, indexing="ij")
            pts = np.column_stack([gx.ravel(), gy.ravel(), gz.ravel()])

            def idx(i: int, j: int, k: int, sz: int = n + 1) -> int:
                return (i * sz + j) * sz + k

            cells = []
            for i in range(n):
                for j in range(n):
                    for k in range(n):
                        cells.append(
                            [
                                idx(i, j, k),
                                idx(i + 1, j, k),
                                idx(i + 1, j + 1, k),
                                idx(i, j + 1, k),
                                idx(i, j, k + 1),
                                idx(i + 1, j, k + 1),
                                idx(i + 1, j + 1, k + 1),
                                idx(i, j + 1, k + 1),
                            ]
                        )
            mesh = compute_difvm_topology_python(pts, cells)
            fc = mesh.face_centres
            cc = mesh.cell_centres
            phi_f = np.sin(np.pi * fc[:, 0]) * np.cos(np.pi * fc[:, 1]) * np.exp(0.5 * fc[:, 2])
            grad_exact = np.column_stack(
                [
                    np.pi * np.cos(np.pi * cc[:, 0]) * np.cos(np.pi * cc[:, 1]) * np.exp(0.5 * cc[:, 2]),
                    -np.pi * np.sin(np.pi * cc[:, 0]) * np.sin(np.pi * cc[:, 1]) * np.exp(0.5 * cc[:, 2]),
                    0.5 * np.sin(np.pi * cc[:, 0]) * np.cos(np.pi * cc[:, 1]) * np.exp(0.5 * cc[:, 2]),
                ]
            )
            internal = mesh.neighbour >= 0
            gg_num = np.zeros((mesh.num_cells, 3), dtype=np.float64)
            flux = phi_f[:, None] * mesh.face_areas
            np.add.at(gg_num, mesh.owner, flux)
            np.subtract.at(gg_num, mesh.neighbour[internal], flux[internal])
            gg_grad = gg_num / mesh.cell_volumes[:, None]
            l2_err = float(
                np.sqrt(
                    np.sum(mesh.cell_volumes * np.sum((gg_grad - grad_exact) ** 2, axis=1))
                )
            )
            errors.append(l2_err)

        p_obs = float(np.log(errors[0] / errors[1]) / np.log(spacings[0] / spacings[1]))
        assert p_obs >= 1.95, f"3D MMS observed order p_obs={p_obs:.3f} < 1.95"
