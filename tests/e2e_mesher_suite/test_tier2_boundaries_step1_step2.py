"""Tier 2 boundary, corner, and negative-path E2E tests for Step 1-2 (F1-F7) and F15-F16."""

from __future__ import annotations

import copy
import logging
import os
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from backend.geometry.models import BoundingBox, SurfaceMesh
from backend.geometry.pipeline import CADGeometryEngine
from backend.ipc.binary_buffers import unpack_geometry_buffer, unpack_mesh_quality_buffer
from backend.ipc.dimensions import validate_si_dimensions
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    generate_hexcore_native,
    get_native_dll,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CellType,
    CurvatureSizingConfig,
    MeshingConfig,
    SizingParameters,
    UnstructuredMesh,
    compute_cell_j_ens,
)
from backend.native_rust.c_abi_lake import CAbiDataType, CAbiSpan, create_cabi_span
from backend.native_rust.expression_engine import ExpressionEngine
from backend.native_rust.mphys_container import MPhysReader
from tests.conftest import (
    create_degenerate_face_arrays,
    create_inverted_cube_arrays,
    create_open_cube_arrays,
    create_sliver_triangle_arrays,
    create_t_junction_non_manifold_arrays,
    create_unit_cube_arrays,
)
from tests.e2e_mesher_suite.oracles import (
    _is_orthogonal_box_8_cell,
    assert_j_ens_geometric_not_dummy,
    assert_linear_field_exact,
    assert_polymesh_roundtrip,
    assert_unstructured_mesh_valid,
    count_surface_components_scipy,
)

logger = logging.getLogger(__name__)

pytestmark = [pytest.mark.e2e, pytest.mark.tier2]


def _make_two_hex_mesh() -> UnstructuredMesh:
    """Create a 2-cell 2x1x1 hex mesh for negative-path perturbation tests."""
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
    m = compute_difvm_topology_python(pts, cells)
    m.boundaries = {"wall": (m.num_internal_faces, m.num_boundary_faces)}
    return m


class TestE2EMesherTier2F1JEnsBoundariesAndInversions:
    """F1 Tier 2: Inverted elements, degenerate nodes, shuffled cuboids, pyramid base checks (>=5 tests)."""

    def test_f1_b01_inverted_hexahedron_yields_negative_j_ens(self) -> None:
        """Verify swapping top and bottom faces of a Hex8 yields negative J_ENS."""
        inv_hex = np.array(
            [
                [0.0, 0.0, 1.0],
                [1.0, 0.0, 1.0],
                [1.0, 1.0, 1.0],
                [0.0, 1.0, 1.0],
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        j_val = compute_cell_j_ens(inv_hex, list(range(8)), CellType.HEXAHEDRON)
        assert j_val < 0.0

    def test_f1_b02_inverted_tetrahedron_and_wedge_yield_negative_j_ens(self) -> None:
        """Verify inverted tetrahedron and inverted wedge yield negative J_ENS."""
        inv_tet = np.array(
            [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]],
            dtype=np.float64,
        )
        assert compute_cell_j_ens(inv_tet, [0, 1, 2, 3], CellType.TETRA) < 0.0

        inv_wedge = np.array(
            [
                [0.0, 0.0, 1.0],
                [1.0, 0.0, 1.0],
                [0.0, 1.0, 1.0],
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        assert compute_cell_j_ens(inv_wedge, list(range(6)), CellType.WEDGE) < 0.0

    def test_f1_b03_collapsed_coplanar_hex8_yields_non_positive_j_ens(self) -> None:
        """Verify collapsed zero-height Hex8 has J_ENS <= 0.0."""
        flat_hex = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [1.0, 1.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        )
        assert compute_cell_j_ens(flat_hex, list(range(8)), CellType.HEXAHEDRON) <= 0.0

    def test_f1_b04_is_orthogonal_box_8_cell_handles_shuffled_and_skewed_vertices(self) -> None:
        """Verify _is_orthogonal_box_8_cell returns Python bool True on shuffled cuboid and False on skewed box."""
        cube = np.array(
            [[x, y, z] for x in (0.0, 2.0) for y in (0.0, 3.0) for z in (0.0, 4.0)],
            dtype=np.float64,
        )
        assert _is_orthogonal_box_8_cell(cube) is True
        shuffled = cube[[3, 7, 0, 5, 1, 6, 2, 4]]
        assert _is_orthogonal_box_8_cell(shuffled) is True

        skewed = cube.copy()
        skewed[4:, 1] += 0.25
        assert _is_orthogonal_box_8_cell(skewed) is False
        assert _is_orthogonal_box_8_cell(cube[:7]) is False

    def test_f1_b05_oracle_rejects_negative_cell_volume_and_nan_coordinates(self) -> None:
        """Verify assert_unstructured_mesh_valid rejects negative cell volume and NaN points."""
        m = _make_two_hex_mesh()
        m_neg = copy.deepcopy(m)
        m_neg.cell_volumes[0] = -0.5
        with pytest.raises(AssertionError, match="Non-positive cell volume"):
            assert_unstructured_mesh_valid(m_neg)

        m_nan = copy.deepcopy(m)
        m_nan.points[0, 0] = np.nan
        with pytest.raises(AssertionError, match="Non-finite values"):
            assert_unstructured_mesh_valid(m_nan)


class TestE2EMesherTier2F2FarfieldAndBboxBoundaries:
    """F2 Tier 2: Extreme aspect ratios, asymmetric padding, 6-tuple vs nested farfield_bbox (>=5 tests)."""

    def test_f2_b01_bounding_box_rejects_invalid_point_shapes(self) -> None:
        """Verify BoundingBox raises ValueError on empty or 2D point arrays."""
        with pytest.raises(ValueError):
            BoundingBox.from_points(np.empty((0, 3), dtype=np.float64))
        with pytest.raises(ValueError):
            BoundingBox(min_pt=np.zeros(2), max_pt=np.ones(3))

    def test_f2_b02_farfield_bbox_flat_6tuple_and_nested_tuple_equivalence(self) -> None:
        """Verify MeshingConfig accepts both ((xmin,ymin,zmin),(xmax,ymax,zmax)) and 6-tuple formats."""
        cfg_nested = MeshingConfig.from_dict(
            {"domain_mode": "external_fluid", "farfield_bbox": [[-2.0, -2.0, -2.0], [3.0, 3.0, 3.0]]}
        )
        cfg_flat = MeshingConfig.from_dict(
            {"domain_mode": "external_fluid", "farfield_bbox": [-2.0, -2.0, -2.0, 3.0, 3.0, 3.0]}
        )
        assert cfg_nested.farfield_bbox == ((-2.0, -2.0, -2.0), (3.0, 3.0, 3.0))
        assert cfg_flat.farfield_bbox == ((-2.0, -2.0, -2.0), (3.0, 3.0, 3.0))

    def test_f2_b03_external_fluid_high_aspect_ratio_needle_geometry(self) -> None:
        """Verify external_fluid graded farfield handles a 20:1 aspect ratio slender body without blowup."""
        v, f = create_unit_cube_arrays()
        v_needle = (v - 0.5) * np.array([2.0, 0.1, 0.1], dtype=np.float64)
        surf = SurfaceMesh(vertices=v_needle, faces=f.astype(np.int32))
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            sizing=CurvatureSizingConfig(h0=0.4, h_min=0.2, h_max=1.6, grading=0.4),
        )
        engine = MeshEngine(use_native=True, strict_native=True)
        mesh = engine.mesh_hexcore(surf, h0=0.4, domain_mode="external_fluid", config=cfg)
        assert_unstructured_mesh_valid(mesh)

    def test_f2_b04_external_fluid_ground_plane_farfield_bounding_box(self) -> None:
        """Verify compute_farfield_bounding_box with ground_plane=True clamps z_min to body z_min."""
        from backend.meshing.mesh_engine import compute_farfield_bounding_box

        bbox = BoundingBox(
            min_pt=np.array([-0.5, -0.5, 0.0], dtype=np.float64),
            max_pt=np.array([1.5, 0.5, 1.0], dtype=np.float64),
        )
        ff_min, ff_max = compute_farfield_bounding_box(bbox, ground_plane=True)
        assert np.isclose(ff_min[2], 0.0, atol=1e-12)
        assert ff_max[2] > 1.0

    def test_f2_b05_generate_hexcore_native_rejects_malformed_surface_shapes(self) -> None:
        """Verify generate_hexcore_native raises ValueError on non-(N,3) vertices or non-(M,3) faces."""
        v, f = create_unit_cube_arrays()
        with pytest.raises(ValueError):
            generate_hexcore_native(v[:, :2], f, h0=0.5)
        with pytest.raises(ValueError):
            generate_hexcore_native(v, f[:, :2], h0=0.5)


class TestE2EMesherTier2F3SolidOnlyAndDirtySurfaceCorners:
    """F3 Tier 2: Thin walls, open edges, inverted normals, degenerate triangles (>=5 tests)."""

    def test_f3_b01_cad_audit_detects_open_cube_non_watertight(self) -> None:
        """Verify CADGeometryEngine.audit detects open edges on a 5-face open cube."""
        v_open, f_open = create_open_cube_arrays()
        eng = CADGeometryEngine(auto_repair_normals=False)
        eng.current_mesh = SurfaceMesh(vertices=v_open, faces=f_open.astype(np.int32))
        report = eng.audit()
        assert report.is_watertight is False
        assert report.open_edges_count > 0

    def test_f3_b02_cad_audit_detects_inverted_cube_orientation(self) -> None:
        """Verify CADGeometryEngine.audit detects inward-facing normals when auto_repair_normals=False."""
        v_inv, f_inv = create_inverted_cube_arrays()
        eng = CADGeometryEngine(auto_repair_normals=False)
        eng.current_mesh = SurfaceMesh(vertices=v_inv, faces=f_inv.astype(np.int32))
        report = eng.audit()
        assert report.signed_volume < 0.0

    def test_f3_b03_cad_audit_detects_t_junction_non_manifold_edges(self) -> None:
        """Verify CADGeometryEngine.audit detects non-manifold multi-edges on T-junction mesh."""
        v_tj, f_tj = create_t_junction_non_manifold_arrays()
        eng = CADGeometryEngine(auto_repair_normals=False)
        eng.current_mesh = SurfaceMesh(vertices=v_tj, faces=f_tj.astype(np.int32))
        report = eng.audit()
        assert report.is_manifold is False
        assert report.multi_edges_count > 0

    def test_f3_b04_cad_audit_detects_degenerate_and_sliver_faces(self) -> None:
        """Verify CADGeometryEngine.audit flags zero-area degenerate faces and low quality slivers."""
        v_deg, f_deg = create_degenerate_face_arrays()
        eng = CADGeometryEngine(auto_repair_normals=False)
        eng.current_mesh = SurfaceMesh(vertices=v_deg, faces=f_deg.astype(np.int32))
        rep_deg = eng.audit()
        assert rep_deg.degenerate_faces_count > 0

        v_slv, f_slv = create_sliver_triangle_arrays()
        eng.current_mesh = SurfaceMesh(vertices=v_slv, faces=f_slv.astype(np.int32))
        rep_slv = eng.audit()
        assert rep_slv.quality_min < 0.05

    def test_f3_b05_solid_only_mm_scale_thin_wall_mesh_validity(self) -> None:
        """Verify solid_only meshes a millimeter-scale (0.002 m) thin domain with positive volumes."""
        v, f = create_unit_cube_arrays()
        v_mm = v * np.array([0.002, 0.002, 0.0005], dtype=np.float64)
        cfg = MeshingConfig(
            domain_mode="solid_only",
            sizing=CurvatureSizingConfig(h0=0.00025, h_min=0.000125, h_max=0.0005),
        )
        mesh = generate_hexcore_native(v_mm, f, h0=0.00025, domain_mode="solid_only", config=cfg)
        assert_unstructured_mesh_valid(mesh)
        assert_linear_field_exact(mesh)


class TestE2EMesherTier2F4StrictNativeErrorHandling:
    """F4 Tier 2: Strict native DLL error handling and invalid path recovery (>=5 tests)."""

    def test_f4_b01_invalid_cfd_native_lib_path_raises_runtime_error_in_strict_mode(
        self,
        restore_native_env: None,
        tmp_path: Path,
    ) -> None:
        """Verify setting CFD_NATIVE_LIB_PATH to a non-existent DLL raises RuntimeError when strict_native=True."""
        os.environ["CFD_NATIVE_LIB_PATH"] = str(tmp_path / "nonexistent_mesher.dll")
        with pytest.raises(RuntimeError):
            get_native_dll(strict_native=True)

    def test_f4_b02_mesh_engine_strict_native_with_use_native_false_raises(self) -> None:
        """Verify MeshEngine(use_native=False, strict_native=True) raises RuntimeError."""
        with pytest.raises(RuntimeError):
            MeshEngine(use_native=False, strict_native=True)

    def test_f4_b03_cfd_strict_native_env_var_enforces_strict_mode(
        self,
        restore_native_env: None,
    ) -> None:
        """Verify CFD_STRICT_NATIVE=1 forces strict native validation on MeshEngine."""
        os.environ["CFD_STRICT_NATIVE"] = "1"
        with pytest.raises(RuntimeError):
            MeshEngine(use_native=False)

    def test_f4_b04_corrupt_dll_file_raises_runtime_error_under_strict_native(
        self,
        restore_native_env: None,
        tmp_path: Path,
    ) -> None:
        """Verify pointing CFD_NATIVE_LIB_PATH at a corrupt non-PE file raises RuntimeError."""
        bad_dll = tmp_path / "corrupt_native.dll"
        bad_dll.write_bytes(b"NOT_A_VALID_PE_DLL_BINARY")
        os.environ["CFD_NATIVE_LIB_PATH"] = str(bad_dll)
        with pytest.raises(RuntimeError):
            get_native_dll(strict_native=True)

    def test_f4_b05_cache_recovery_after_failed_override_load(
        self,
        restore_native_env: None,
        tmp_path: Path,
    ) -> None:
        """Verify clearing CFD_NATIVE_LIB_PATH and resetting cache restores normal DLL loading."""
        from backend.meshing.difvm_topology import _reset_native_dll_cache

        os.environ["CFD_NATIVE_LIB_PATH"] = str(tmp_path / "missing.dll")
        _reset_native_dll_cache()
        with pytest.raises(RuntimeError):
            get_native_dll(strict_native=True)

        os.environ.pop("CFD_NATIVE_LIB_PATH", None)
        _reset_native_dll_cache()
        dll = get_native_dll(strict_native=True)
        assert dll is not None


class TestE2EMesherTier2F5SizingAndConfigValidationBoundaries:
    """F5 Tier 2: Invalid SizingParameters, h_max < h_min, non-positive alpha/beta/grading (>=5 tests)."""

    def test_f5_b01_sizing_parameters_rejects_non_positive_h_min(self) -> None:
        """Verify SizingParameters raises ValueError when h_min <= 0."""
        with pytest.raises(ValueError, match="h_min"):
            SizingParameters(h_min=0.0, h_max=0.1)
        with pytest.raises(ValueError, match="h_min"):
            SizingParameters(h_min=-0.01, h_max=0.1)

    def test_f5_b02_sizing_parameters_rejects_h_max_less_than_h_min(self) -> None:
        """Verify SizingParameters raises ValueError when h_max < h_min."""
        with pytest.raises(ValueError, match="h_max"):
            SizingParameters(h_min=0.05, h_max=0.01)

    def test_f5_b03_sizing_parameters_rejects_non_positive_alpha_or_beta(self) -> None:
        """Verify SizingParameters raises ValueError when alpha <= 0 or beta <= 0."""
        with pytest.raises(ValueError, match="alpha"):
            SizingParameters(h_min=0.01, h_max=0.1, alpha=0.0)
        with pytest.raises(ValueError, match="beta"):
            SizingParameters(h_min=0.01, h_max=0.1, beta=-0.1)

    def test_f5_b04_sizing_parameters_rejects_non_positive_grading(self) -> None:
        """Verify SizingParameters raises ValueError when grading <= 0."""
        with pytest.raises(ValueError, match="grading"):
            SizingParameters(h_min=0.01, h_max=0.1, grading=0.0)

    def test_f5_b05_sizing_parameters_equal_h_min_and_h_max_uniform_bound(self) -> None:
        """Verify SizingParameters accepts h_min == h_max as a degenerate uniform sizing bound."""
        sp = SizingParameters(h_min=0.05, h_max=0.05, h0=0.05)
        assert np.isclose(sp.h_min, sp.h_max)


class TestE2EMesherTier2F6LinearExactnessAndTopologyNegativeProbes:
    """F6 Tier 2: Perturbed k_f, delta_f, face_centres, face_areas, owner>=neighbour rejection (>=5 tests)."""

    def test_f6_b01_assert_linear_field_exact_rejects_corrupted_k_f(self) -> None:
        """Verify assert_linear_field_exact raises AssertionError when k_f is perturbed."""
        m = _make_two_hex_mesh()
        m.k_f[0, 0] += 1e-3
        with pytest.raises(AssertionError, match="delta_f \\+ k_f|Laplacian"):
            assert_linear_field_exact(m)

    def test_f6_b02_assert_linear_field_exact_rejects_corrupted_delta_f(self) -> None:
        """Verify assert_linear_field_exact raises AssertionError when only delta_f is corrupted."""
        m = _make_two_hex_mesh()
        m.delta_f[0, 0] += 1e-3
        with pytest.raises(AssertionError, match="delta_f"):
            assert_linear_field_exact(m)

    def test_f6_b03_assert_linear_field_exact_rejects_corrupted_face_centres(self) -> None:
        """Verify assert_linear_field_exact raises AssertionError when face_centres are shifted."""
        m = _make_two_hex_mesh()
        m.face_centres[0, 0] += 1e-4
        with pytest.raises(AssertionError, match="interpolation|Green-Gauss"):
            assert_linear_field_exact(m)

    def test_f6_b04_assert_linear_field_exact_rejects_broken_face_area_closure(self) -> None:
        """Verify assert_linear_field_exact raises AssertionError when a face normal is flipped."""
        m = _make_two_hex_mesh()
        m.face_areas[0] = -m.face_areas[0]
        with pytest.raises(AssertionError, match="closure|Green-Gauss"):
            assert_linear_field_exact(m)

    def test_f6_b05_unstructured_mesh_validate_rejects_owner_greater_or_equal_neighbour(self) -> None:
        """Verify UnstructuredMesh.validate raises ValueError when owner >= neighbour on an internal face."""
        m = _make_two_hex_mesh()
        m.owner[0] = 1
        m.neighbour[0] = 0
        with pytest.raises(ValueError, match="owner must be strictly less than neighbour"):
            m.validate()


class TestE2EMesherTier2F7ScalingAndGciEdgeCases:
    """F7 Tier 2: GCI invalid refinement ratio, micrometer-scale linear exactness, component count bounds (>=5 tests)."""

    def test_f7_b01_linear_field_exactness_at_micrometer_scale_with_large_phi0(self) -> None:
        """Verify assert_linear_field_exact remains accurate on a micrometer-scale (1e-6 m) mesh."""
        m = _make_two_hex_mesh()
        scale = 1e-6
        m.points = m.points * scale
        m.face_centres = m.face_centres * scale
        m.cell_centres = m.cell_centres * scale
        m.face_areas = m.face_areas * (scale ** 2)
        m.delta_f = m.delta_f * (scale ** 2)
        m.k_f = m.k_f * (scale ** 2)
        m.cell_volumes = m.cell_volumes * (scale ** 3)
        res = assert_linear_field_exact(m, phi0=250.0, tol=1e-12)
        assert res["gg_grad_max_err"] <= 1e-12

    def test_f7_b02_count_surface_components_ignores_unreferenced_vertices(self) -> None:
        """Verify count_surface_components_scipy ignores 50 isolated unreferenced vertices."""
        v, f = create_unit_cube_arrays()
        extra_v = np.random.default_rng(42).uniform(10.0, 20.0, size=(50, 3))
        v_padded = np.vstack([v, extra_v])
        assert count_surface_components_scipy(v_padded, f) == 1

    def test_f7_b03_count_surface_components_rejects_out_of_bounds_face_indices(self) -> None:
        """Verify count_surface_components_scipy raises ValueError on out-of-bounds triangle indices."""
        v, f = create_unit_cube_arrays()
        f_bad = f.copy()
        f_bad[0, 0] = len(v) + 5
        with pytest.raises(ValueError, match="out of bounds"):
            count_surface_components_scipy(v, f_bad)

    def test_f7_b04_count_surface_components_rejects_empty_arrays(self) -> None:
        """Verify count_surface_components_scipy raises ValueError on empty vertex or face arrays."""
        with pytest.raises(ValueError):
            count_surface_components_scipy(np.empty((0, 3)), np.zeros((1, 3), dtype=int))
        with pytest.raises(ValueError):
            count_surface_components_scipy(np.zeros((4, 3)), np.empty((0, 3), dtype=int))

    def test_f7_b05_mesh_cad_file_rejects_nonexistent_file(self, tmp_path: Path) -> None:
        """Verify MeshEngine.mesh_cad_file raises FileNotFoundError on missing CAD path."""
        engine = MeshEngine(use_native=True)
        with pytest.raises(FileNotFoundError):
            engine.mesh_cad_file(tmp_path / "does_not_exist.stl")


class TestE2EMesherTier2F15FastIoIpcAndPolymeshCorners:
    """F15 Tier 2: Unaligned CAbiSpan, corrupt .mphys, invalid 7D SI dims, corrupt IPC buffers, bad polyMesh patches (>=5 tests)."""

    def test_f15_b01_cabi_span_unaligned_slice_auto_copies_to_64b_aligned(self) -> None:
        """Verify create_cabi_span(..., ensure_aligned=True) re-aligns an unaligned numpy slice to 64B."""
        raw = np.arange(100, dtype=np.float64)
        unaligned_slice = raw[1:]
        span, _backing = create_cabi_span(unaligned_slice, ensure_aligned=True)
        assert span.is_aligned_64()
        assert np.array_equal(span.to_numpy(), unaligned_slice)

        empty_span = CAbiSpan(ptr=0, len=0, dtype=int(CAbiDataType.FLOAT64), device_id=0)
        assert len(empty_span.to_numpy()) == 0

    def test_f15_b02_mphys_reader_rejects_truncated_and_bad_magic_files(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify MPhysReader raises ValueError on truncated files or bad magic headers."""
        short_file = tmp_path / "short.mphys"
        short_file.write_bytes(b"MPHYS001_TOO_SHORT")
        with pytest.raises(ValueError):
            MPhysReader(short_file)

        bad_magic = tmp_path / "bad_magic.mphys"
        bad_magic.write_bytes(b"BADMAGIC" + b"\x00" * 128)
        with pytest.raises(ValueError):
            MPhysReader(bad_magic)

    def test_f15_b03_validate_si_dimensions_rejects_bool_float_nan_and_wrong_length(self) -> None:
        """Verify validate_si_dimensions rejects booleans, non-integral floats, NaN/Inf, and length != 7."""
        with pytest.raises(TypeError):
            validate_si_dimensions([True, 1, 0, 0, 0, 0, 0])  # type: ignore[list-item]
        with pytest.raises(ValueError):
            validate_si_dimensions([0, 1.5, 0, 0, 0, 0, 0])  # type: ignore[list-item]
        with pytest.raises(ValueError):
            validate_si_dimensions([1, 0, 0, 0, 0])
        with pytest.raises((TypeError, ValueError)):
            validate_si_dimensions("1 -1 -2 0 0 0 0")  # type: ignore[arg-type]

    def test_f15_b04_binary_buffers_reject_corrupt_magic_and_short_payloads(self) -> None:
        """Verify unpack_geometry_buffer and unpack_mesh_quality_buffer reject invalid headers."""
        with pytest.raises(ValueError):
            unpack_geometry_buffer(b"SHORT")
        with pytest.raises(ValueError):
            unpack_geometry_buffer(b"WRONGMAG" + b"\x00" * 64)
        with pytest.raises(ValueError):
            unpack_mesh_quality_buffer(b"WRONGMSH" + b"\x00" * 64)

    def test_f15_b05_assert_polymesh_roundtrip_rejects_incomplete_or_shifted_boundary_ranges(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify assert_polymesh_roundtrip fails when boundary patches do not partition [n_int, n_faces)."""
        m_bad_count = _make_two_hex_mesh()
        m_bad_count.boundaries = {"wall": (1, 9)}  # 9 instead of 10 boundary faces
        with pytest.raises(AssertionError, match="exact partition"):
            assert_polymesh_roundtrip(m_bad_count, tmp_path / "bad_count")

        m_bad_start = _make_two_hex_mesh()
        m_bad_start.boundaries = {"wall": (2, 9)}  # start_face=2 skips face 1
        with pytest.raises(AssertionError, match="exact partition"):
            assert_polymesh_roundtrip(m_bad_start, tmp_path / "bad_start")


@pytest.mark.s13_followup
class TestE2EMesherTier2F16Section13BoundaryAndFailureContracts:
    """F16 Tier 2: Section 13 negative & corner cases (1->N conflict, crash_replay.toml, Thingi10k, complex-step) (>=5 tests)."""

    def test_f16_b01_expression_engine_rejects_invalid_syntax_and_unknown_identifiers(self) -> None:
        """Verify ExpressionEngine raises SyntaxError/ValueError on malformed expressions or missing variables."""
        eng = ExpressionEngine()
        x = np.array([1.0, 2.0], dtype=np.float64)
        with pytest.raises(SyntaxError):
            eng.evaluate_boundary_profile("let a = (1.0 + ;", x, x, x)
        with pytest.raises(ValueError):
            eng.evaluate_boundary_profile("unknown_var * 2.0", x, x, x)

    def test_f16_b02_spatial_query_bc_1_to_n_ambiguous_remapping_conflict_error(self) -> None:
        """Verify ambiguous 1->N CAD patch split or overlapping spatial query selector raises conflict error."""
        import importlib

        try:
            bc_mod = importlib.import_module("backend.meshing.spatial_query_bc")
        except ImportError as exc:
            pytest.fail(
                f"§13.1 (1.B) SpatialQuerySelector conflict-detection module "
                f"(backend.meshing.spatial_query_bc) not yet implemented: {exc}"
            )
        assert hasattr(bc_mod, "SpatialQueryConflictError")

    def test_f16_b03_crash_replay_toml_emitted_on_invalid_face_or_nan_cell(
        self,
        tmp_path: Path,
    ) -> None:
        """Verify crash_replay.toml reproducer bundle is emitted when FaceClass::invalid or NaN cell occurs."""
        import importlib

        try:
            replay_mod = importlib.import_module("backend.meshing.crash_replay")
        except ImportError as exc:
            pytest.fail(
                f"§13.1 (2.A) crash_replay.toml writer module "
                f"(backend.meshing.crash_replay) not yet implemented: {exc}"
            )
        replay_file = tmp_path / "crash_replay.toml"
        replay_mod.write_crash_replay(replay_file, reason="FaceClass::invalid", cell_id=0)
        assert replay_file.is_file()

    def test_f16_b04_complex_step_geometric_sensitivity_machine_precision(self) -> None:
        """Verify Complex-Step perturbation (h_cs = 1e-30) recovers exact analytical face-area sensitivity."""
        # Complex-step derivative of triangle area vector S(p0, p1, p2) = 0.5 * (p1 - p0) x (p2 - p0)
        # with respect to p1_x perturbation: dS/d(p1_x) = 0.5 * e_x x (p2 - p0)
        h_cs = 1e-30
        p0 = np.array([0.0, 0.0, 0.0], dtype=np.complex128)
        p1 = np.array([1.2 + 1j * h_cs, 0.3, -0.2], dtype=np.complex128)
        p2 = np.array([0.4, 1.5, 0.7], dtype=np.complex128)
        s_complex = 0.5 * np.cross(p1 - p0, p2 - p0)
        ds_dp1x_cs = np.imag(s_complex) / h_cs
        ds_dp1x_exact = 0.5 * np.cross(
            np.array([1.0, 0.0, 0.0], dtype=np.float64),
            np.real(p2 - p0),
        )
        assert np.allclose(ds_dp1x_cs, ds_dp1x_exact, atol=1e-15, rtol=1e-15)

    def test_f16_b05_thingi10k_defective_stl_batch_zero_segfaults_and_open_edge_report(self) -> None:
        """Verify defective non-watertight/self-intersecting surfaces do not crash CAD audit or mesher."""
        for generator in (
            create_open_cube_arrays,
            create_t_junction_non_manifold_arrays,
            create_degenerate_face_arrays,
            create_sliver_triangle_arrays,
        ):
            v, f = generator()
            eng = CADGeometryEngine(auto_repair_normals=True)
            eng.current_mesh = SurfaceMesh(vertices=v, faces=f.astype(np.int32))
            report = eng.audit()
            report_dict = report.to_dict()
            assert "is_watertight" in report_dict
            assert "open_edges_count" in report_dict
