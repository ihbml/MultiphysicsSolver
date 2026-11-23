"""Tier 2 boundary, corner, and negative-path E2E tests for Step 3 (F8-F14)."""

from __future__ import annotations

import copy
import logging
from pathlib import Path
import sys
from typing import Dict

import numpy as np
import pytest

from backend.core.case_generator import CaseGenerator
from backend.core.materials import Material, MaterialDatabase
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    extract_cht_twin_faces,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import InflationConfig, UnstructuredMesh
from backend.meshing.polymesh_io import (
    export_openfoam_multiregion_polymesh,
    export_openfoam_polymesh,
)
from backend.physics_modules.monolithic_fsi import EulerianMonolithicFSI
from backend.physics_modules.registry import ModuleRegistry
from backend.runner.wsl_runner import WslSimulationRunner
from tests.e2e_mesher_suite.conftest import SurfaceMesh
from tests.e2e_mesher_suite.oracles import (
    assert_cht_1to1_conformal,
    read_fluent_msh_h5_summary,
    run_subprocess_bounded,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier2]


def _make_two_region_cht_mesh() -> UnstructuredMesh:
    """Create a 2-region 2x1x1 hex mesh with extracted CHT twin faces."""
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
    raw = compute_difvm_topology_python(pts, cells, regions=np.array([0, 1], dtype=np.int32))
    return extract_cht_twin_faces(raw)


class TestE2EMesherTier2F8WslRunnerDivergenceAndTimeouts:
    """F8 Tier 2: WslSimulationRunner divergence/NaN/FATAL detection, invalid ranks, subprocess timeout (>=5 tests)."""

    def test_f8_b01_parse_log_text_raises_floating_point_error_on_nan_courant(self) -> None:
        """Verify WslSimulationRunner.parse_log_text raises FloatingPointError on NaN Courant when requested."""
        runner = WslSimulationRunner()
        nan_log = "Time = 0.002\nCourant Number mean: 0.12 max: nan\n"
        with pytest.raises(FloatingPointError):
            runner.parse_log_text(nan_log, raise_on_divergence=True)

    def test_f8_b02_parse_log_text_detects_foam_fatal_error(self) -> None:
        """Verify WslSimulationRunner.parse_log_text captures FOAM FATAL ERROR and raises when requested."""
        runner = WslSimulationRunner()
        fatal_log = (
            "Time = 0.001\n"
            "--> FOAM FATAL ERROR:\n"
            "Continuity error cannot be removed\n"
        )
        with pytest.raises(FloatingPointError):
            runner.parse_log_text(fatal_log, raise_on_divergence=True)

    def test_f8_b03_run_parallel_rejects_non_positive_mpi_ranks(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify WslSimulationRunner.run_parallel raises ValueError when ranks <= 0."""
        runner = WslSimulationRunner()
        with pytest.raises(ValueError, match="ranks"):
            runner.run_parallel(str(tmp_path), solver="simpleFoam", ranks=0)
        with pytest.raises(ValueError, match="ranks"):
            runner.run_parallel(str(tmp_path), solver="simpleFoam", ranks=-4)

    def test_f8_b04_to_wsl_path_handles_empty_string_and_forward_slashes(self) -> None:
        """Verify WslSimulationRunner.to_wsl_path returns '' on empty string and normalizes Windows slashes."""
        runner = WslSimulationRunner()
        assert runner.to_wsl_path("") == ""
        assert runner.to_wsl_path("C:/Users/Test/case") == "/mnt/c/Users/Test/case"

    def test_f8_b05_run_subprocess_bounded_enforces_hard_timeout(self) -> None:
        """Verify run_subprocess_bounded raises AssertionError when child process exceeds timeout_s."""
        with pytest.raises(AssertionError, match="timed out"):
            run_subprocess_bounded(
                [sys.executable, "-c", "import time; time.sleep(5)"],
                timeout_s=0.25,
            )


class TestE2EMesherTier2F9ChtNegativeAndOrphanInvariants:
    """F9 Tier 2: Zero-area twin faces, flipped normals, orphan coupled faces, single-region no-op (>=5 tests)."""

    def test_f9_b01_assert_cht_1to1_conformal_rejects_zero_area_twin_faces(self) -> None:
        """Verify assert_cht_1to1_conformal rejects degenerate zero-area CHT twin faces."""
        m = _make_two_region_cht_mesh()
        fa, fb = m.baffles[0]["twin_pairs"][0]
        m.face_areas[fa] = 0.0
        m.face_areas[fb] = 0.0
        with pytest.raises(AssertionError, match="zero-area"):
            assert_cht_1to1_conformal(m, expected_regions=2)

    def test_f9_b02_assert_cht_1to1_conformal_rejects_non_opposite_normals(self) -> None:
        """Verify assert_cht_1to1_conformal rejects twin faces where S_B != -S_A."""
        m = _make_two_region_cht_mesh()
        fa, fb = m.baffles[0]["twin_pairs"][0]
        m.face_areas[fb] = m.face_areas[fa].copy()
        with pytest.raises(AssertionError, match="S_B == -S_A"):
            assert_cht_1to1_conformal(m, expected_regions=2)

    def test_f9_b03_assert_cht_1to1_conformal_rejects_orphan_coupled_boundary_faces(self) -> None:
        """Verify assert_cht_1to1_conformal detects unmatched coupledWall boundary faces."""
        m = _make_two_region_cht_mesh()
        m.boundaries["coupledWall_orphan"] = (m.num_internal_faces, 1)
        with pytest.raises(AssertionError, match="orphan"):
            assert_cht_1to1_conformal(m, expected_regions=2)

    def test_f9_b04_assert_cht_1to1_conformal_rejects_single_region_mesh(self) -> None:
        """Verify assert_cht_1to1_conformal fails on a single-region mesh."""
        m = _make_two_region_cht_mesh()
        m.cell_regions[:] = 0
        with pytest.raises(AssertionError, match="is_multiregion"):
            assert_cht_1to1_conformal(m)

    def test_f9_b05_extract_cht_twin_faces_noop_on_single_region_mesh(self) -> None:
        """Verify extract_cht_twin_faces returns a single-region mesh unchanged with 0 baffles."""
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
        m_single = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        out = extract_cht_twin_faces(m_single)
        assert len(out.baffles) == 0
        assert out.num_cells == 1


class TestE2EMesherTier2F10BoundaryLayerPinchingAndCollisionCorners:
    """F10 Tier 2: Concave corners, extreme narrow clearance, opposite_walls gap geometry (>=5 tests)."""

    def test_f10_b01_inflate_boundary_layers_ultra_narrow_clearance_clamps_safely(self) -> None:
        """Verify ultra-narrow clearance (smaller than first_layer_height) does not create inverted cells."""
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
            num_layers=4,
            first_layer_height=0.05,
            growth_rate=1.2,
            pinching=True,
        )
        clearances = np.full(4, 0.01, dtype=np.float64)
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            [[0, 1, 2, 3]],
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
            clearances=clearances,
        )
        assert np.all(bl_mesh.cell_volumes > 0.0)

    def test_f10_b02_inflate_boundary_layers_l_shaped_concave_corner(self) -> None:
        """Verify boundary layer inflation on an L-shaped 90-degree corner produces positive volumes."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 1.0],
                [0.0, 1.0, 1.0],
            ],
            dtype=np.float64,
        )
        wall_faces = [[0, 1, 2, 3], [0, 3, 5, 4]]
        inf_cfg = InflationConfig(
            num_layers=2,
            first_layer_height=0.01,
            growth_rate=1.2,
            pinching=True,
            pinching_threshold_angle=60.0,
        )
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(wall_pts, wall_faces, config=inf_cfg)
        assert bl_mesh.num_cells > 0
        assert np.all(bl_mesh.cell_volumes > 0.0)

    def test_f10_b03_opposite_walls_surfaces_load_and_preserve_gap_separation(
        self,
        opposite_walls_surfaces: Dict[str, SurfaceMesh],
    ) -> None:
        """Verify opposite_walls tutorial asset contains all 4 gap surfaces with distinct bounding boxes."""
        assert set(opposite_walls_surfaces.keys()) == {
            "gap0",
            "gap1",
            "gap_transformed0",
            "gap_transformed1",
        }
        b0 = opposite_walls_surfaces["gap0"].get_bounding_box()
        b1 = opposite_walls_surfaces["gap1"].get_bounding_box()
        assert not np.allclose(b0.center, b1.center)

    def test_f10_b04_single_layer_inflation_boundary_case(self) -> None:
        """Verify num_layers=1 boundary layer inflation produces exactly 1 cell per wall face."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        inf_cfg = InflationConfig(num_layers=1, first_layer_height=0.025, growth_rate=1.2)
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            [[0, 1, 2, 3]],
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
        )
        assert bl_mesh.num_cells == 1
        assert np.isclose(float(bl_mesh.cell_volumes[0]), 0.025, rtol=1e-5)

    def test_f10_b05_variable_clearance_across_wall_vertices_produces_positive_volumes(self) -> None:
        """Verify spatially varying medial-axis clearance across a wall quad keeps all cell volumes > 0."""
        wall_pts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        clearances = np.array([0.02, 0.04, 0.08, 0.06], dtype=np.float64)
        inf_cfg = InflationConfig(num_layers=3, first_layer_height=0.01, growth_rate=1.2)
        engine = MeshEngine(use_native=True)
        bl_mesh = engine.inflate_boundary_layers(
            wall_pts,
            [[0, 1, 2, 3]],
            config=inf_cfg,
            nominal_normal=np.array([0.0, 0.0, 1.0], dtype=np.float64),
            clearances=clearances,
        )
        assert np.all(bl_mesh.cell_volumes > 0.0)


class TestE2EMesherTier2F11MeshValidationInvariantRejection:
    """F11 Tier 2: UnstructuredMesh.validate() rejection of closure, projection, aspect ratio, contiguity (>=5 tests)."""

    def test_f11_b01_validate_rejects_non_contiguous_internal_and_boundary_faces(self) -> None:
        """Verify UnstructuredMesh.validate rejects boundary faces placed before internal faces."""
        m = _make_two_region_cht_mesh()
        # Swap face 0 (boundary or internal) so neighbour ordering is non-contiguous
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
        m2 = compute_difvm_topology_python(
            pts,
            [[0, 1, 2, 3, 4, 5, 6, 7], [1, 8, 9, 2, 5, 10, 11, 6]],
        )
        m2.neighbour[0] = -1
        m2.neighbour[1] = 1
        with pytest.raises(ValueError, match="Contiguous block ordering"):
            m2.validate()

    def test_f11_b02_validate_rejects_overlapping_boundary_patches(self) -> None:
        """Verify UnstructuredMesh.validate raises ValueError when two boundary patches overlap."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.boundaries = {"p1": (0, 4), "p2": (2, 4)}
        with pytest.raises(ValueError, match="overlaps"):
            m.validate()

    def test_f11_b03_validate_rejects_gauss_closure_violation(self) -> None:
        """Verify UnstructuredMesh.validate raises ValueError when sum(sigma_cf * S_f) != 0."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.face_areas[0, 0] += 0.5
        with pytest.raises(ValueError, match="Gauss divergence"):
            m.validate()

    def test_f11_b04_validate_rejects_non_collinear_delta_f_projection(self) -> None:
        """Verify UnstructuredMesh.validate rejects internal delta_f that is not collinear with d_f."""
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
        m = compute_difvm_topology_python(
            pts,
            [[0, 1, 2, 3, 4, 5, 6, 7], [1, 8, 9, 2, 5, 10, 11, 6]],
        )
        # Rotate delta_f[0] into Y while compensating k_f[0] so delta_f + k_f == S_f still holds
        shift = np.array([0.0, 0.25, 0.0], dtype=np.float64)
        m.delta_f[0] += shift
        m.k_f[0] -= shift
        with pytest.raises(ValueError, match="collinear|projection|over-relaxation"):
            m.validate()

    def test_f11_b05_validate_rejects_cell_centroid_outside_bounding_box(self) -> None:
        """Verify UnstructuredMesh.validate raises ValueError when cell_centres lie outside cell vertices."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.cell_centres[0] = np.array([10.0, 10.0, 10.0], dtype=np.float64)
        with pytest.raises(ValueError):
            m.validate()


class TestE2EMesherTier2F12FluentH5EdgeAndErrorHandling:
    """F12 Tier 2: Missing file, non-positive chunk_size, chunk-size invariance, corrupt HDF5 (>=5 tests)."""

    def test_f12_b01_read_fluent_msh_h5_summary_raises_on_missing_path(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify read_fluent_msh_h5_summary raises FileNotFoundError on missing .msh.h5 file."""
        with pytest.raises(FileNotFoundError):
            read_fluent_msh_h5_summary(tmp_path / "missing_fluent.msh.h5")

    def test_f12_b02_read_fluent_msh_h5_summary_rejects_non_positive_chunk_size(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify read_fluent_msh_h5_summary raises ValueError when chunk_size <= 0."""
        with pytest.raises(ValueError, match="chunk_size"):
            read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"], chunk_size=0)
        with pytest.raises(ValueError, match="chunk_size"):
            read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"], chunk_size=-100)

    @pytest.mark.fluent_ref
    def test_f12_b03_read_fluent_msh_h5_summary_chunk_size_invariance_on_fltg_1_1(
        self,
        require_fluent_h5: Dict[str, Path],
    ) -> None:
        """Verify read_fluent_msh_h5_summary yields identical counts across different chunk sizes."""
        s1 = read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"], chunk_size=2_000_000)
        s2 = read_fluent_msh_h5_summary(require_fluent_h5["FLTG_1_1"], chunk_size=750_000)
        assert s1["cellCount"] == s2["cellCount"] == 7_606_273
        assert s1["ctype_counts"] == s2["ctype_counts"]
        assert np.allclose(s1["bbox_min"], s2["bbox_min"])
        assert np.allclose(s1["bbox_max"], s2["bbox_max"])

    def test_f12_b04_read_fluent_msh_h5_summary_synthetic_mixed_and_uniform_sections(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify read_fluent_msh_h5_summary parses a synthetic HDF5 with alphanumeric section keys."""
        import h5py

        h5_file = tmp_path / "synthetic.msh.h5"
        with h5py.File(h5_file, "w") as f:
            m = f.create_group("meshes/1")
            m.attrs["cellCount"] = np.array([5], dtype=np.int64)
            m.attrs["faceCount"] = np.array([20], dtype=np.int64)
            m.attrs["nodeCount"] = np.array([8], dtype=np.int64)
            m.attrs["edgeCount"] = np.array([0], dtype=np.int64)
            m.attrs["dimension"] = np.array([3], dtype=np.int32)
            m.attrs["units"] = np.array([b"mm"])
            m.attrs["version"] = np.array([2], dtype=np.int32)

            cz = m.create_group("cells/zoneTopology")
            cz.attrs["nZones"] = np.array([2], dtype=np.int32)
            cz.create_dataset("name", data=np.array([b"solid;fluid"]))
            cz.create_dataset("id", data=np.array([1, 2], dtype=np.int32))
            cz.create_dataset("minId", data=np.array([1, 3], dtype=np.int64))
            cz.create_dataset("maxId", data=np.array([2, 5], dtype=np.int64))
            cz.create_dataset("cellType", data=np.array([0, 7], dtype=np.int32))

            ct = m.create_group("cells/ctype")
            s1 = ct.create_group("1")
            s1.attrs["elementType"] = np.array([0], dtype=np.int32)
            s1.create_dataset("cell-types", data=np.array([4, 7], dtype=np.int16))
            s2 = ct.create_group("2_uniform")
            s2.attrs["elementType"] = np.array([7], dtype=np.int32)
            s2.attrs["minId"] = np.array([3], dtype=np.int64)
            s2.attrs["maxId"] = np.array([5], dtype=np.int64)

            fz = m.create_group("faces/zoneTopology")
            fz.attrs["nZones"] = np.array([1], dtype=np.int32)
            fz.create_dataset("name", data=np.array([b"wall"]))
            fz.create_dataset("id", data=np.array([10], dtype=np.int32))
            fz.create_dataset("zoneType", data=np.array([3], dtype=np.int32))
            fz.create_dataset("faceType", data=np.array([0], dtype=np.int32))
            fz.create_dataset("shadowZoneId", data=np.array([0], dtype=np.int32))
            fz.create_dataset("minId", data=np.array([1], dtype=np.int64))
            fz.create_dataset("maxId", data=np.array([20], dtype=np.int64))

            nc = m.create_group("nodes/coords")
            nc.create_dataset("1", data=np.ones((8, 3), dtype=np.float64))

        summary = read_fluent_msh_h5_summary(h5_file, chunk_size=2)
        assert summary["cellCount"] == 5
        assert summary["ctype_counts"] == {4: 1, 7: 4}

    def test_f12_b05_read_fluent_msh_h5_summary_detects_ctype_count_mismatch(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify read_fluent_msh_h5_summary asserts when sum(ctype_counts) != cellCount."""
        import h5py

        h5_file = tmp_path / "mismatch.msh.h5"
        with h5py.File(h5_file, "w") as f:
            m = f.create_group("meshes/1")
            m.attrs["cellCount"] = np.array([10], dtype=np.int64)
            m.attrs["faceCount"] = np.array([10], dtype=np.int64)
            m.attrs["nodeCount"] = np.array([4], dtype=np.int64)
            cz = m.create_group("cells/zoneTopology")
            cz.attrs["nZones"] = np.array([1], dtype=np.int32)
            cz.create_dataset("name", data=np.array([b"z1"]))
            cz.create_dataset("id", data=np.array([1], dtype=np.int32))
            cz.create_dataset("minId", data=np.array([1], dtype=np.int64))
            cz.create_dataset("maxId", data=np.array([2], dtype=np.int64))
            cz.create_dataset("cellType", data=np.array([0], dtype=np.int32))
            ct = m.create_group("cells/ctype")
            s1 = ct.create_group("1")
            s1.create_dataset("cell-types", data=np.array([4, 4], dtype=np.int16))

        with pytest.raises(AssertionError, match="cellCount"):
            read_fluent_msh_h5_summary(h5_file)


class TestE2EMesherTier2F13PolymeshExportNegativeChecks:
    """F13 Tier 2: export_openfoam_polymesh & export_openfoam_multiregion_polymesh negative checks (>=5 tests)."""

    def test_f13_b01_export_polymesh_rejects_mesh_without_explicit_faces(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_polymesh raises ValueError when mesh.faces is None."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.faces = None
        with pytest.raises(ValueError, match="faces"):
            export_openfoam_polymesh(m, tmp_path / "no_faces")

    def test_f13_b02_export_polymesh_rejects_faces_length_mismatch(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_polymesh raises ValueError when len(mesh.faces) != num_faces."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.faces = m.faces[:2]  # type: ignore[index]
        with pytest.raises(ValueError, match="faces"):
            export_openfoam_polymesh(m, tmp_path / "bad_len")

    def test_f13_b03_export_multiregion_polymesh_rejects_empty_regions(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_multiregion_polymesh raises ValueError on empty regions dict."""
        with pytest.raises(ValueError, match="empty"):
            export_openfoam_multiregion_polymesh(tmp_path / "empty_reg", regions={})

    def test_f13_b04_export_multiregion_polymesh_rejects_unknown_region_in_properties(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_multiregion_polymesh rejects region_properties referencing unknown region."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        with pytest.raises(ValueError):
            export_openfoam_multiregion_polymesh(
                tmp_path / "bad_props",
                regions={"fluid": m},
                region_properties={"fluid": ["fluid"], "solid": ["ghost_region"]},
            )

    def test_f13_b05_export_polymesh_infers_special_patch_types(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify export_openfoam_polymesh writes symmetry, cyclic, empty, and mappedWall types."""
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
        m = compute_difvm_topology_python(pts, [[0, 1, 2, 3, 4, 5, 6, 7]])
        m.boundaries = {
            "symmetry_plane": (0, 2),
            "frontAndBack": (2, 2),
            "coupledWall_to_solid": (4, 2),
        }
        p_dir = export_openfoam_polymesh(
            m,
            tmp_path / "special_patches",
            patch_types={
                "symmetry_plane": "symmetry",
                "frontAndBack": "empty",
                "coupledWall_to_solid": "mappedWall",
            },
        )
        bnd_txt = (p_dir / "boundary").read_text(encoding="utf-8")
        assert "symmetry" in bnd_txt
        assert "empty" in bnd_txt
        assert "mappedWall" in bnd_txt


class TestE2EMesherTier2F14MultiPhysicsNegativeAndConflictChecks:
    """F14 Tier 2: Physics module dependency warnings, CaseGenerator invalid weights, FSI invalid parameters (>=5 tests)."""

    def test_f14_b01_species_without_energy_reports_dependency_warning(self) -> None:
        """Verify enabling Species & Chemistry while disabling Energy produces a dependency warning."""
        reg = ModuleRegistry()
        reg.disable_module("Energy")
        reg.enable_module("Species & Chemistry")
        warnings = reg.validate_configuration()
        assert len(warnings) >= 1
        assert any("Energy" in w for w in warnings)

    def test_f14_b02_module_registry_export_ui_schema_raises_on_unknown_id(self) -> None:
        """Verify ModuleRegistry.export_ui_schema raises KeyError on unknown module_id."""
        reg = ModuleRegistry()
        with pytest.raises(KeyError):
            reg.export_ui_schema(module_id="NonExistentPhysicsModule")

    def test_f14_b03_case_generator_rejects_non_positive_mpi_ranks_or_weights(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify CaseGenerator.create_case raises ValueError when mpi_ranks <= 0 or weights <= 0."""
        reg = ModuleRegistry()
        gen = CaseGenerator(reg)
        with pytest.raises(ValueError):
            gen.create_case("bad_ranks", str(tmp_path), mpi_ranks=0)
        with pytest.raises(ValueError):
            gen.create_case("bad_pweight", str(tmp_path), p_weight=0.0)
        with pytest.raises(ValueError):
            gen.create_case("bad_eweight", str(tmp_path), e_weight=-1.0)

    def test_f14_b04_eulerian_monolithic_fsi_rejects_non_positive_densities_or_modulus(self) -> None:
        """Verify EulerianMonolithicFSI raises ValueError when rho_s <= 0, rho_f <= 0, or G_s <= 0."""
        with pytest.raises(ValueError):
            EulerianMonolithicFSI(rho_s=0.0, rho_f=1000.0, G_s=1e4)
        with pytest.raises(ValueError):
            EulerianMonolithicFSI(rho_s=1000.0, rho_f=-5.0, G_s=1e4)
        with pytest.raises(ValueError):
            EulerianMonolithicFSI(rho_s=1000.0, rho_f=1000.0, G_s=0.0)

    def test_f14_b05_material_database_detects_missing_properties_and_rejects_bad_type(self) -> None:
        """Verify Material.missing_properties identifies absent keys and MaterialDatabase.add_material checks types."""
        mat = Material(name="CustomAlloy", category="solid")
        mat.set_property("thermal", "thermal_conductivity", 25.0)
        missing = mat.missing_properties(["thermal_conductivity", "electrical_conductivity"])
        assert missing == ["electrical_conductivity"]

        db = MaterialDatabase()
        with pytest.raises(TypeError):
            db.add_material({"not": "a_material_instance"})  # type: ignore[arg-type]
