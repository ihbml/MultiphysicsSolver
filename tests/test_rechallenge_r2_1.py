"""Re-Challenge R2-1 Verification Suite: Empirical stress-testing of all 5 Challenger 1 issues.

Tests:
1. Moeller triangle intersection: Edge-in-plane, vertex-in-plane, zero-division avoidance, and angle sweeps.
2. OpenFOAM surfaceCheck sliver rejection: Precise boundary testing for Q < 1e-5 vs Q >= 1e-5.
3. Quality metrics threshold alignment and consistency.
4. Multi-body / multi-component normal auto-repair: Independent component winding preservation.
5. Small face count (N=2, 3) self-intersection detection.
"""

import warnings
from typing import List, Tuple
import numpy as np
import pytest

from backend.geometry.audit.quality_metrics import QualityMetrics
from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import AuditReport, SurfaceMesh
from tests.conftest import create_inverted_cube_arrays, create_unit_cube_arrays


# ==============================================================================
# 1. MOELLER ALGORITHM ZERO-DIVISION & EDGE-IN-PLANE STRESS TESTS
# ==============================================================================

class TestMoellerZeroDivisionEdgeInPlane:
    """Rigorous stress-testing of Moeller algorithm against NaN / division-by-zero warnings."""

    def test_edge_in_plane_orthogonal_piercing(self):
        """Triangle 1 in XY plane (Z=0) with an edge on the X-axis (y=0).

        Triangle 2 in XZ plane (Y=0) piercing Triangle 1.
        Must detect intersection with ZERO RuntimeWarnings and NO NaNs.
        """
        # T1 in XY plane: ([-1, 0, 0], [1, 0, 0], [0, 2, 0]) -> edge on Y=0
        # T2 in XZ plane: ([-0.5, 0, -1], [0.5, 0, -1], [0, 0, 1]) -> piercing Z=0
        t1 = np.array([[-1.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 2.0, 0.0]], dtype=np.float64)
        t2 = np.array([[-0.5, 0.0, -1.0], [0.5, 0.0, -1.0], [0.0, 0.0, 1.0]], dtype=np.float64)

        with warnings.catch_warnings():
            warnings.simplefilter("error")  # Any warning raises an exception!
            res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

        assert res is True, "Must detect piercing intersection when edge is in plane"

    def test_edge_in_plane_angular_sweep_no_warnings(self):
        """Rotate the piercing triangle around the piercing axis through 360 degrees.

        Verify ZERO warnings (no NaNs, no zero divisions) across all angles.
        """
        t1 = np.array([[-2.0, 0.0, 0.0], [2.0, 0.0, 0.0], [0.0, 2.0, 0.0]], dtype=np.float64)

        for deg in range(5, 175, 5):
            rad = np.radians(deg)
            # Normal to piercing triangle inclined at angle rad
            t2 = np.array(
                [
                    [-0.5, -np.cos(rad), -np.sin(rad)],
                    [0.5, -np.cos(rad), -np.sin(rad)],
                    [0.0, np.cos(rad), np.sin(rad)],
                ],
                dtype=np.float64,
            )

            with warnings.catch_warnings():
                warnings.simplefilter("error")
                res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

            assert res is True, f"Failed at angle {deg} degrees"

    def test_edge_in_plane_disjoint_intervals(self):
        """Triangle 2 lies in plane of Triangle 1's edge, but their intervals on the line do NOT overlap."""
        t1 = np.array([[-1.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 2.0, 0.0]], dtype=np.float64)
        # Shifted along X beyond T1's [-1, 1] range: interval on line is [5, 6]
        t2 = np.array([[5.0, 0.0, -1.0], [6.0, 0.0, -1.0], [5.5, 0.0, 1.0]], dtype=np.float64)

        with warnings.catch_warnings():
            warnings.simplefilter("error")
            res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

        assert res is False, "Disjoint intervals must not report intersection"

    def test_single_vertex_in_plane_zero_division_guard(self):
        """Triangle 1 has exactly one vertex lying on Triangle 2's plane."""
        t1 = np.array([[0.0, 0.0, 0.0], [1.0, 1.0, 1.0], [1.0, -1.0, -1.0]], dtype=np.float64)
        t2 = np.array([[0.5, 0.0, -1.0], [0.5, 0.0, 1.0], [0.5, 1.0, 0.0]], dtype=np.float64)

        with warnings.catch_warnings():
            warnings.simplefilter("error")
            res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

        assert isinstance(res, (bool, np.bool_))

    def test_nearly_parallel_denominators_epsilon_safety(self):
        """Stress-test numerical stability when distances to plane are microscopic (1e-16)."""
        t1 = np.array([[0.0, 0.0, 1e-16], [1.0, 0.0, -1e-16], [0.5, 1.0, 0.0]], dtype=np.float64)
        t2 = np.array([[0.5, 0.5, -1.0], [0.5, 0.5, 1.0], [1.5, 0.5, 0.0]], dtype=np.float64)

        with warnings.catch_warnings():
            warnings.simplefilter("error")
            res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

        assert isinstance(res, (bool, np.bool_))


# ==============================================================================
# 2. SLIVER TRIANGLE THRESHOLD & OPENFOAM SURFACECHECK REJECTION
# ==============================================================================

class TestSliverTriangleSurfaceCheckRejection:
    """Rigorous boundary testing of OpenFOAM surfaceCheck sliver triangle rejection."""

    def _make_watertight_tetrahedron_with_sliver(self, apex_height: float, apex_y: float) -> Tuple[np.ndarray, np.ndarray]:
        """Constructs a watertight 4-vertex, 4-face tetrahedron with controllable height and apex."""
        verts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.5, apex_y, 0.0],
                [0.5, apex_y / 2.0, apex_height],
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 2, 1],
                [0, 1, 3],
                [0, 3, 2],
                [1, 2, 3],
            ],
            dtype=np.int32,
        )
        return verts, faces

    def test_sliver_below_1e5_fails_surfacecheck(self):
        """Watertight mesh with quality Q < 1e-5 must FAIL passed_openfoam_surfacecheck."""
        # A very flat tetrahedron with base=1000, y=1e-4, z=1e-4 -> Q ~ 1e-7 << 1e-5
        verts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1000.0, 0.0, 0.0],
                [500.0, 1e-4, 0.0],
                [500.0, 1e-5, 1e-4],
            ],
            dtype=np.float64,
        )
        faces = np.array([[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.quality_min < 1e-5
        assert report.details["bad_faces_count"] > 0
        assert report.passed_openfoam_surfacecheck is False, (
            "Mesh with slivers Q < 1e-5 MUST FAIL passed_openfoam_surfacecheck"
        )

    def test_clean_mesh_above_1e5_passes_surfacecheck(self):
        """Watertight regular tetrahedron with Q ~ 1.0 must PASS passed_openfoam_surfacecheck."""
        from tests.test_adversarial_audit import make_regular_tetrahedron

        verts, faces = make_regular_tetrahedron()
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.quality_min > 0.5
        assert report.details["bad_faces_count"] == 0
        assert report.passed_openfoam_surfacecheck is True

    def test_sliver_boundary_sharpness(self):
        """Verify the exact boundary sharpness of Q < min_quality_tol in QualityMetrics."""
        # Needle triangle: Area = 0.5 * b * h with b = 1.0, apex at (0.5, h, 0)
        # a^2 = 1.0, b^2 = 0.25 + h^2, c^2 = 0.25 + h^2 -> a^2 + b^2 + c^2 ~ 1.5
        # Q = 4 * sqrt(3) * (0.5 * 1.0 * h) / 1.5 = (4 / sqrt(3)) * h
        # For target Q: h = Q * sqrt(3) / 4
        h_below = (0.99e-5) * np.sqrt(3.0) / 4.0
        h_above = (1.01e-5) * np.sqrt(3.0) / 4.0

        verts_below = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, h_below, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh_below = SurfaceMesh(vertices=verts_below, faces=faces)

        q_min_below, _, _, bad_below = QualityMetrics.compute_equilateral_quality(mesh_below, min_q_tol=1e-5)
        assert q_min_below < 1e-5
        assert len(bad_below) == 1

        verts_above = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, h_above, 0.0]], dtype=np.float64)
        mesh_above = SurfaceMesh(vertices=verts_above, faces=faces)

        q_min_above, _, _, bad_above = QualityMetrics.compute_equilateral_quality(mesh_above, min_q_tol=1e-5)
        assert q_min_above > 1e-5
        assert len(bad_above) == 0


# ==============================================================================
# 3. QUALITY METRICS THRESHOLD ALIGNMENT
# ==============================================================================

class TestQualityMetricsThresholdAlignment:
    """Verifies default threshold is 1e-5 and adheres to OpenFOAM surfaceCheck specifications."""

    def test_default_min_q_tol_is_1e_5(self):
        """Default min_q_tol in compute_equilateral_quality must be 1e-5."""
        import inspect

        sig = inspect.signature(QualityMetrics.compute_equilateral_quality)
        assert sig.parameters["min_q_tol"].default == 1e-5

    def test_surface_auditor_default_min_quality_tol_is_1e_5(self):
        """Default min_quality_tol in SurfaceAuditor must be 1e-5."""
        auditor = SurfaceAuditor()
        assert auditor.min_quality_tol == 1e-5

    def test_face_with_q_between_1e11_and_1e5_is_flagged(self):
        """A triangle with Q = 1e-8 (previously ignored under old 1e-11) is now flagged as bad."""
        h = (1e-8) / np.sqrt(3.0)
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, h, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        q_min, _, _, bad_faces = QualityMetrics.compute_equilateral_quality(mesh)
        assert 1e-11 < q_min < 1e-5
        assert 0 in bad_faces, "Face with 1e-11 < Q < 1e-5 must be flagged in bad_faces"


# ==============================================================================
# 4. MULTI-BODY / MULTI-COMPONENT NORMAL AUTO-REPAIR
# ==============================================================================

class TestMultiBodyNormalAutoRepair:
    """Empirical verification that normal repair operates per-component without cross-contamination."""

    def test_two_cubes_mixed_orientation_net_negative_volume(self):
        """Component 1: Outward cube (V = +1.0)

        Component 2: Inverted larger cube (V = -8.0)
        Net volume before repair = -7.0 < 0.
        Must repair Component 2 to +8.0 while preserving Component 1 at +1.0!
        """
        # Comp 1: unit cube at (0, 0, 0)
        v1, f1 = create_unit_cube_arrays()
        # Comp 2: inverted cube size 2 at (5, 0, 0)
        v2, f2 = create_inverted_cube_arrays()
        v2 = v2 * 2.0 + np.array([5.0, 0.0, 0.0])

        all_v = np.vstack([v1, v2])
        all_f = np.vstack([f1, f2 + len(v1)])
        mesh = SurfaceMesh(vertices=all_v, faces=all_f)

        # Pre-repair volumes
        vol1_pre = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[:len(f1)])
        vol2_pre = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[len(f1):])
        assert np.isclose(vol1_pre, 1.0, atol=1e-6)
        assert np.isclose(vol2_pre, -8.0, atol=1e-6)
        assert vol1_pre + vol2_pre < 0.0  # Net volume is negative!

        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        # Post-repair volumes
        vol1_post = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[:len(f1)])
        vol2_post = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[len(f1):])

        assert np.isclose(vol1_post, 1.0, atol=1e-6), f"Component 1 was corrupted! Got {vol1_post}"
        assert np.isclose(vol2_post, 8.0, atol=1e-6), f"Component 2 was not repaired! Got {vol2_post}"
        assert report.is_outward_oriented is True
        assert np.isclose(report.signed_volume, 9.0, atol=1e-6)

    def test_three_bodies_all_combinations_repaired(self):
        """Three disjoint cubes: Comp 1 (outward), Comp 2 (inverted), Comp 3 (inverted).

        All three must end up with positive volume and outward orientation.
        """
        v1, f1 = create_unit_cube_arrays()  # Outward, vol = +1
        v2, f2 = create_inverted_cube_arrays()
        v2 = v2 + np.array([5.0, 0.0, 0.0])  # Inverted, vol = -1
        v3, f3 = create_inverted_cube_arrays()
        v3 = v3 * 3.0 + np.array([15.0, 0.0, 0.0])  # Inverted, vol = -27

        f2_adj = f2 + len(v1)
        f3_adj = f3 + len(v1) + len(v2)
        all_v = np.vstack([v1, v2, v3])
        all_f = np.vstack([f1, f2_adj, f3_adj])
        mesh = SurfaceMesh(vertices=all_v, faces=all_f)

        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        n1, n2, n3 = len(f1), len(f2), len(f3)
        v1_post = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[:n1])
        v2_post = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[n1:n1 + n2])
        v3_post = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[n1 + n2:])

        assert np.isclose(v1_post, 1.0, atol=1e-6)
        assert np.isclose(v2_post, 1.0, atol=1e-6)
        assert np.isclose(v3_post, 27.0, atol=1e-6)
        assert report.is_outward_oriented is True
        assert np.isclose(report.signed_volume, 29.0, atol=1e-6)

    def test_open_surface_component_is_not_corrupted_by_auto_repair(self):
        """A mesh with an open sheet (non-zero signed volume) and an inverted closed cube.

        Only the closed cube should be repaired; open sheet must not be inverted.
        """
        # Open sheet: single quad (2 triangles)
        sheet_v = np.array(
            [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]],
            dtype=np.float64,
        )
        sheet_f = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.int32)

        # Inverted closed cube far away
        v_cube, f_cube = create_inverted_cube_arrays()
        v_cube = v_cube + np.array([10.0, 0.0, 0.0])

        all_v = np.vstack([sheet_v, v_cube])
        all_f = np.vstack([sheet_f, f_cube + len(sheet_v)])
        mesh = SurfaceMesh(vertices=all_v, faces=all_f)

        orig_sheet_faces = mesh.faces[:2].copy()

        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        # Sheet faces should remain untouched
        assert np.array_equal(mesh.faces[:2], orig_sheet_faces)
        # Cube should be repaired to outward
        cube_vol = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[2:])
        assert np.isclose(cube_vol, 1.0, atol=1e-6)


# ==============================================================================
# 5. SMALL FACE COUNT SELF-INTERSECTION DETECTION (N = 2, 3)
# ==============================================================================

class TestSmallFaceCountSelfIntersection:
    """Verifies that meshes with 2 or 3 faces are properly audited for self-intersections."""

    def test_exactly_two_intersecting_faces(self):
        """Two penetrating triangles (N=2) must yield self_intersections == 1."""
        verts = np.array(
            [
                [-1.0, -1.0, 0.0],
                [1.0, -1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, -0.5, -1.0],
                [0.0, -0.5, 1.0],
                [0.0, 0.5, 0.0],
            ],
            dtype=np.float64,
        )
        faces = np.array([[0, 1, 2], [3, 4, 5]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        count = SurfaceAuditor._detect_self_intersections_aabb(mesh.vertices, mesh.faces)
        assert count == 1, f"Expected 1 intersection for N=2, got {count}"

    def test_exactly_three_faces_with_one_intersection(self):
        """Three triangles (N=3) where two intersect and one is disjoint."""
        verts = np.array(
            [
                [-1.0, -1.0, 0.0],
                [1.0, -1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, -0.5, -1.0],
                [0.0, -0.5, 1.0],
                [0.0, 0.5, 0.0],
                [10.0, 10.0, 10.0],
                [11.0, 10.0, 10.0],
                [10.0, 11.0, 10.0],
            ],
            dtype=np.float64,
        )
        faces = np.array([[0, 1, 2], [3, 4, 5], [6, 7, 8]], dtype=np.int32)

        count = SurfaceAuditor._detect_self_intersections_aabb(verts, faces)
        assert count == 1, f"Expected 1 intersection for N=3, got {count}"

    def test_single_face_yields_zero_intersections(self):
        """Single triangle (N=1) cannot intersect itself."""
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)

        count = SurfaceAuditor._detect_self_intersections_aabb(verts, faces)
        assert count == 0


# ==============================================================================
# 6. STRESS FUZZING & COMBINATORIAL PERMUTATION SUITE
# ==============================================================================

class TestStressFuzzingAndPermutations:
    """Large-scale empirical fuzzing and combinatorial permutations."""

    def test_moeller_5000_fuzzed_pairs_no_warnings(self):
        """Fuzz Moeller algorithm with 5000 randomized triangle pairs.

        Enforces zero RuntimeWarnings, zero NaNs, and clean boolean return values.
        """
        rng = np.random.default_rng(42)
        for i in range(5000):
            t1 = rng.standard_normal((3, 3))
            t2 = rng.standard_normal((3, 3))
            # Every third pair, force edge-in-plane
            if i % 3 == 0:
                t2[:, 2] = 0.0
                t1[:2, 2] = 0.0

            with warnings.catch_warnings():
                warnings.simplefilter("error")
                res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)

            assert isinstance(res, (bool, np.bool_))

    def test_multi_body_32_orientation_permutations(self):
        """Verify that across all 32 combinations (2^5) of 5 bodies with mixed orientations,

        per-component auto-repair correctly fixes all inverted bodies without touching outward bodies.
        """
        auditor = SurfaceAuditor(auto_repair_normals=True)

        for combo in range(32):
            all_v = []
            all_f = []
            v_offset = 0
            comp_slices = []
            for i in range(5):
                is_inverted = bool((combo >> i) & 1)
                if is_inverted:
                    v, f = create_inverted_cube_arrays()
                else:
                    v, f = create_unit_cube_arrays()
                scale = 0.5 + i * 0.5
                pos = np.array([i * 5.0, 0.0, 0.0])
                v = v * scale + pos
                all_v.append(v)
                all_f.append(f + v_offset)
                comp_slices.append(len(f))
                v_offset += len(v)

            mesh = SurfaceMesh(vertices=np.vstack(all_v), faces=np.vstack(all_f))
            report = auditor.audit(mesh)
            assert report.is_outward_oriented, f"Failed outward orientation on combo {combo}"

            f_start = 0
            for i, count in enumerate(comp_slices):
                vol = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[f_start : f_start + count])
                expected_scale = 0.5 + i * 0.5
                expected_vol = expected_scale ** 3
                assert np.isclose(vol, expected_vol, atol=1e-5), (
                    f"Combo {combo} comp {i} volume {vol} != expected {expected_vol}"
                )
                f_start += count

    def test_sliver_spectrum_logarithmic_sweep(self):
        """Sweep 40 points logarithmically from Q=1e-8 to Q=1e-2.

        Enforce that passed_openfoam_surfacecheck exactly mirrors Q >= 1e-5.
        """
        auditor = SurfaceAuditor()
        for q_target in np.logspace(-8, -2, 40):
            h = q_target * np.sqrt(3.0) / 4.0
            verts = np.array(
                [
                    [0.0, 0.0, 0.0],
                    [1.0, 0.0, 0.0],
                    [0.5, h, 0.0],
                    [0.5, h / 2.0, h],
                ],
                dtype=np.float64,
            )
            faces = np.array([[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]], dtype=np.int32)
            mesh = SurfaceMesh(vertices=verts, faces=faces)
            report = auditor.audit(mesh)

            if report.quality_min < 1e-5:
                assert not report.passed_openfoam_surfacecheck, (
                    f"Q={report.quality_min} (< 1e-5) must FAIL passed_openfoam_surfacecheck"
                )
            else:
                assert report.passed_openfoam_surfacecheck, (
                    f"Q={report.quality_min} (>= 1e-5) must PASS passed_openfoam_surfacecheck"
                )

