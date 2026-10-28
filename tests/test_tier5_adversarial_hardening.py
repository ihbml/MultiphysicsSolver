"""Phase 2 Adversarial Coverage Hardening Test Suite (Tier 5).

Milestone: M4
Target Subsystem: backend/meshing (C++20 DLL, geometry_sdf.py, mesh_engine.py, coupling.py, moving_mesh.py)
Author: challenger_m4_1 (Empirical Challenger: critic, specialist)

This test suite executes rigorous white-box adversarial stress tests verifying:
1. Pathological and degenerate boundary geometries (zero thickness, self-touching, high-curvature corners).
2. Extreme cell deformations and sliver cells (aspect ratios > 100, up to 10,000+).
3. Solver stabilization mask verification (error_map with bitmask flags 0, 1, 2, 3 for non-orthogonality, skewness, and critical stabilization).
4. Cyclic boundary symmetry matching under numerical perturbations and sector transformations.
5. High-frequency ALE mesh oscillations and Discrete Geometric Conservation Law (DGCL).

Acceptance Criteria:
- Zero crashes, segmentation faults, memory corruption, or infinite loops.
- Mathematical invariants strictly held (Gauss divergence closure < 10^-11, internal non-ortho decomposition < 10^-14).
- 100% agreement between native C++20 core (cfd_mesher_native.dll) and pure-Python fallback.
"""

from __future__ import annotations

import ctypes
import math
from typing import Any, Dict, List, Optional, Set, Tuple

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.meshing.coupling import (
    OPMISTaylorInterpolator,
    SPHFVMCoupler,
    SPHKernel,
)
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    extract_cyclic_pairs,
    get_native_dll,
)
from backend.meshing.geometry_sdf import (
    BaseSDF,
    BoxSDF,
    CADFeatureFilter,
    CADMeshSDF,
    CSGDifference,
    CSGIntersection,
    CSGUnion,
    CurvatureRefinement,
    PolygonSDF,
    SphereSDF,
    smooth_max,
    smooth_min,
)
from backend.meshing.models import (
    CurvatureSizingConfig,
    InflationConfig,
    MeshingConfig,
    QualityConfig,
    SizingParameters,
    UnstructuredMesh,
)
from backend.meshing.moving_mesh import ALEMeshMover


# ==============================================================================
# Helper Mesh Generators
# ==============================================================================

def make_sheared_two_cell_hex_mesh(theta_deg: float) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Construct a 2-cell hexahedral mesh with mathematically isolated non-orthogonality.
    
    Produces exact non-orthogonality angle theta_deg on the shared internal face,
    while maintaining zero skewness (Q_skew < 1e-15).
    """
    s = float(np.tan(np.deg2rad(theta_deg)))
    pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, s, 0.0],
        [1.0, 1.0 + s, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [1.0, s, 1.0],
        [1.0, 1.0 + s, 1.0],
        [0.0, 1.0, 1.0],
        [2.0, 2.0 * s, 0.0],
        [2.0, 1.0 + 2.0 * s, 0.0],
        [2.0, 2.0 * s, 1.0],
        [2.0, 1.0 + 2.0 * s, 1.0],
    ], dtype=np.float64)
    c0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
    c1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)
    return pts, [c0, c1]


def make_skewed_two_cell_hex_mesh(target_skewness: float) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Construct a 2-cell hexahedral mesh with mathematically isolated skewness.
    
    By vertically shifting the shared interface by dy = 2.0 * target_skewness,
    the shared face center is offset while the centroid-to-centroid vector d_f
    remains strictly normal to the face (theta = 0.0 deg, non-ortho = 0.0).
    """
    dy = float(2.0 * target_skewness)
    pts = np.array([
        [-1.0, 0.0, 0.0],
        [-1.0, 1.0, 0.0],
        [-1.0, 1.0, 1.0],
        [-1.0, 0.0, 1.0],
        [0.0, dy, 0.0],
        [0.0, 1.0 + dy, 0.0],
        [0.0, 1.0 + dy, 1.0],
        [0.0, dy, 1.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [1.0, 1.0, 1.0],
        [1.0, 0.0, 1.0],
    ], dtype=np.float64)
    c0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
    c1 = np.array([4, 5, 6, 7, 8, 9, 10, 11], dtype=np.int64)
    return pts, [c0, c1]


def make_sliver_hex_mesh(aspect_ratio_target: float) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Construct a 2-cell hexahedral mesh with an extreme aspect ratio."""
    dz = float(1.0 / aspect_ratio_target)
    pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, dz],
        [1.0, 0.0, dz],
        [1.0, 1.0, dz],
        [0.0, 1.0, dz],
        [2.0, 0.0, 0.0],
        [2.0, 1.0, 0.0],
        [2.0, 0.0, dz],
        [2.0, 1.0, dz],
    ], dtype=np.float64)
    c0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
    c1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)
    return pts, [c0, c1]


def make_cyclic_sector_mesh(
    sector_angle_rad: float = np.pi / 2.0,
    r_in: float = 1.0,
    r_out: float = 2.0,
    z_height: float = 1.0,
) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Construct a two-cell mesh rotated by sector_angle_rad around the z-axis."""
    cos_t = np.cos(sector_angle_rad)
    sin_t = np.sin(sector_angle_rad)
    R_z = np.array([
        [cos_t, -sin_t, 0.0],
        [sin_t,  cos_t, 0.0],
        [0.0,    0.0,   1.0],
    ], dtype=np.float64)

    # Cell 1 at theta = 0
    c1_pts = np.array([
        [r_in,  -0.5, 0.0],
        [r_out, -0.5, 0.0],
        [r_out,  0.5, 0.0],
        [r_in,   0.5, 0.0],
        [r_in,  -0.5, z_height],
        [r_out, -0.5, z_height],
        [r_out,  0.5, z_height],
        [r_in,   0.5, z_height],
    ], dtype=np.float64)

    # Cell 2 rotated by sector_angle_rad
    c2_pts = np.dot(c1_pts, R_z.T)

    all_pts = np.vstack([c1_pts, c2_pts])
    c0 = np.arange(8, dtype=np.int64)
    c1 = np.arange(8, 16, dtype=np.int64)
    return all_pts, [c0, c1]


# ==============================================================================
# 1. Pathological and Degenerate Boundary Geometries
# ==============================================================================

class TestAdversarialDegenerateBoundaries:
    """Stress tests on pathological boundaries: zero thickness, contact singularities, cusps."""

    def test_zero_thickness_planar_baffle_sanitization(self):
        """Verify CADFeatureFilter strips non-manifold zero-thickness sheets and fins."""
        # Cube (8 nodes, 12 triangular facets) + zero-thickness fin (node 8 attached to edge 1-2)
        cube_v = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
            [2, 0, 0],  # Fin node extending out
        ], dtype=np.float64)

        cube_f = [
            [0, 2, 1], [0, 3, 2],  # Bottom (-z)
            [4, 5, 6], [4, 6, 7],  # Top (+z)
            [0, 1, 5], [0, 5, 4],  # Front (-y)
            [1, 2, 6], [1, 6, 5],  # Right (+x)
            [2, 3, 7], [2, 7, 6],  # Back (+y)
            [3, 0, 4], [3, 4, 7],  # Left (-x)
            [1, 2, 8],             # Non-manifold fin facet
        ]
        sm = SurfaceMesh(vertices=cube_v, faces=np.array(cube_f, dtype=np.int32))
        assert len(sm.faces) == 13
        assert len(sm.vertices) == 9

        # Sanitization must purge the fin and the orphan vertex
        sanitized = CADFeatureFilter.sanitize(sm)
        assert len(sanitized.faces) == 12, "Non-manifold fin must be stripped"
        assert len(sanitized.vertices) == 8, "Orphan vertex 8 must be purged"

        # Verify Euler characteristic for closed simple polyhedron: V - E + F = 2
        edges = set()
        for face in sanitized.faces:
            for i in range(3):
                e = (min(face[i], face[(i + 1) % 3]), max(face[i], face[(i + 1) % 3]))
                edges.add(e)
        chi = len(sanitized.vertices) - len(edges) + len(sanitized.faces)
        assert chi == 2, f"Euler characteristic must equal 2 for sanitized cube, got {chi}"

    def test_cad_mesh_sdf_zero_thickness_plate_queries(self):
        """Verify CADMeshSDF gracefully evaluates distance and gradients on zero-thickness plate."""
        verts = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.0],
        ], dtype=np.float64)
        faces = np.array([
            [0, 1, 2],
            [0, 2, 3],
            [0, 2, 1],  # Opposing face
            [0, 3, 2],
        ], dtype=np.int32)
        sm = SurfaceMesh(vertices=verts, faces=faces)

        sdf = CADMeshSDF.from_surface_mesh(sm)
        test_pts = np.array([
            [0.5, 0.5, 0.05],    # Above plate
            [0.5, 0.5, -0.05],   # Below plate
            [0.5, 0.5, 0.0],     # Exactly on plate
            [10.0, 10.0, 10.0],  # Distant point
            [1e-8, 1e-8, 1e-8],  # Near corner
        ], dtype=np.float64)

        d, g = sdf.evaluate(test_pts)
        assert np.all(np.isfinite(d)), "Distances must be finite"
        assert np.all(np.isfinite(g)), "Gradients must be finite"

        # Distance exactly on plate must be 0.0
        assert math.isclose(d[2], 0.0, abs_tol=1e-12)
        # Distance at z=0.05 must equal 0.05
        assert math.isclose(abs(d[0]), 0.05, abs_tol=1e-6)
        # Distance at z=-0.05 must equal 0.05
        assert math.isclose(abs(d[1]), 0.05, abs_tol=1e-6)

        # Gradient norms must be normalized (norm == 1)
        gnorms = np.linalg.norm(g, axis=-1)
        assert np.allclose(gnorms, 1.0, atol=1e-6), "Gradients must be unit vectors"

    def test_self_touching_geometry_double_cone_singularity(self):
        """Stress-test level set queries and Newton-Raphson projection at a contact singularity."""
        # Two unit spheres touching tangentially at origin (0, 0, 0)
        s1 = SphereSDF(center=np.array([-1.0, 0.0, 0.0]), radius=1.0)
        s2 = SphereSDF(center=np.array([1.0, 0.0, 0.0]), radius=1.0)
        union = CSGUnion(s1, s2)

        pts = np.array([
            [0.0, 0.0, 0.0],     # Exact contact singularity
            [0.0, 0.01, 0.0],    # In the crevice between spheres
            [0.0, -0.01, 0.0],
            [1e-6, 0.0, 0.0],
        ], dtype=np.float64)

        d, g = union.evaluate(pts)
        assert np.all(np.isfinite(d)), "Distances near contact point must be finite"
        assert np.all(np.isfinite(g)), "Gradients near contact point must be finite"
        assert math.isclose(d[0], 0.0, abs_tol=1e-12), "Singularity point distance must be 0"

        # Newton-Raphson projection onto zero-level set must converge without hanging or crashing
        proj = union.project_to_boundary(pts, max_iter=10)
        assert np.all(np.isfinite(proj)), "Boundary projections must be finite"
        proj_dist = union.distance(proj)
        assert np.all(np.abs(proj_dist) < 1e-4), "Projected points must lie on boundary"

    def test_knife_edge_and_razor_corner_curvature_clamping(self):
        """Stress-test curvature evaluation on razor corner (apex angle 0.1 deg)."""
        theta = float(np.deg2rad(0.1))
        poly = np.array([
            [0.0, 0.0],
            [1.0, float(np.tan(theta / 2.0))],
            [1.0, float(-np.tan(theta / 2.0))],
        ], dtype=np.float64)

        sdf = PolygonSDF(poly)
        pts = np.array([
            [0.0, 0.0],      # Exact apex
            [-0.005, 0.0],   # Right outside tip
            [0.005, 0.0],    # Inside razor wedge
            [1e-6, 0.0],
        ], dtype=np.float64)

        h_min = 0.001
        h_max = 0.100
        params = SizingParameters(h_min=h_min, h_max=h_max, alpha=0.1)
        curv_engine = CurvatureRefinement(sdf.evaluate, params=params, dim=2)

        h_vals = curv_engine.evaluate_h_curv(pts)
        assert np.all(np.isfinite(h_vals)), "Sizing function must remain finite"
        assert np.all(h_vals >= h_min), f"Sizing must respect h_min={h_min}"
        assert np.all(h_vals <= h_max), f"Sizing must respect h_max={h_max}"

    def test_csg_smooth_blending_extreme_limits(self):
        """Verify smooth_min and smooth_max behavior at extreme blending parameters and values."""
        a = np.array([10.0, -5.0, 0.0, 1e20, -1e20], dtype=np.float64)
        b = np.array([5.0, -10.0, 0.0, 1e20, -1e20], dtype=np.float64)

        # k -> 0 must approach standard minimum
        s_min_k0 = smooth_min(a, b, k=0.0)
        assert np.allclose(s_min_k0, np.minimum(a, b))

        # k -> 0 for smooth_max must approach standard maximum
        s_max_k0 = smooth_max(a, b, k=0.0)
        assert np.allclose(s_max_k0, np.maximum(a, b))

        # k > 0 must be strictly <= min(a, b)
        s_min_smooth = smooth_min(a[:3], b[:3], k=1.0)
        assert np.all(s_min_smooth <= np.minimum(a[:3], b[:3]) + 1e-12)

    def test_collinear_degenerate_facets_filtering(self):
        """Verify CADFeatureFilter cleanly removes zero-area collinear triangles."""
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [2.0, 0.0, 0.0], [0.0, 1.0, 0.0]], dtype=np.float64)
        # Face 0 is collinear: (0, 1, 2) has area 0
        # Face 1 is valid: (0, 1, 3) has area 0.5
        faces = np.array([[0, 1, 2], [0, 1, 3]], dtype=np.int32)
        sm = SurfaceMesh(vertices=verts, faces=faces)

        clean = CADFeatureFilter.filter_degenerate_faces(sm)
        assert len(clean.faces) == 1, "Collinear zero-area facet must be stripped"
        assert len(clean.vertices) == 3, "Orphan collinear vertex must be purged"



# ==============================================================================
# 2. Extreme Cell Deformations and Sliver Cells (Aspect Ratio > 100)
# ==============================================================================

class TestAdversarialExtremeCellDeformationsAndSlivers:
    """Stress tests on extreme cell aspect ratios, sliver cells, and degenerate volumes."""

    @pytest.mark.parametrize("target_ar", [100.0, 500.0, 1000.0, 10000.0])
    def test_extreme_aspect_ratio_hex_sliver_cells_native_and_python(self, target_ar: float):
        """Verify volume positivity, Gauss divergence, and error_map aspect ratio on extreme slivers."""
        pts, cells = make_sliver_hex_mesh(target_ar)

        for engine_type, use_nat in [("python", False), ("native", True)]:
            mesh = compute_difvm_topology(pts, cells, use_native=use_nat)

            # 1. Volume positivity
            assert np.all(mesh.cell_volumes > 0.0), f"[{engine_type}] Volumes must be strictly positive"
            assert np.all(np.isfinite(mesh.cell_volumes)), f"[{engine_type}] Volumes must be finite"

            # 2. Gauss divergence theorem closed cell sum (< 10^-11)
            for c in range(len(cells)):
                s_sum = np.zeros(3, dtype=np.float64)
                for f in range(len(mesh.owner)):
                    if mesh.owner[f] == c:
                        s_sum += mesh.face_areas[f]
                    elif mesh.neighbour[f] == c:
                        s_sum -= mesh.face_areas[f]
                div_err = float(np.linalg.norm(s_sum))
                assert div_err < 1e-11, f"[{engine_type}] Gauss divergence error {div_err} >= 1e-11 on AR={target_ar}"

            # 3. Non-orthogonal decomposition identity on internal faces
            int_mask = mesh.neighbour >= 0
            if np.any(int_mask):
                decomp_err = np.linalg.norm(
                    mesh.delta_f[int_mask] + mesh.k_f[int_mask] - mesh.face_areas[int_mask],
                    axis=1
                )
                assert np.max(decomp_err) < 1e-14, f"[{engine_type}] Internal decomp error >= 1e-14"

            # 4. Aspect ratio in error_map must be >= target_ar
            ar_arr = mesh.error_map["aspect_ratio"]
            assert np.all(ar_arr >= target_ar * 0.99), (
                f"[{engine_type}] Reported aspect ratio {ar_arr} must be >= {target_ar}"
            )

            # 5. Degenerate cell detector must flag these slivers (AR > 40)
            mover = ALEMeshMover(max_aspect_ratio_threshold=40.0)
            bad_cells = mover.detect_degenerate_cells(mesh)
            assert set(bad_cells) == {0, 1}, f"[{engine_type}] All sliver cells must be flagged by detector"

    def test_sliver_tetrahedron_cells(self):
        """Verify tetrahedral sliver cell with near-coplanar apex (height = 1e-4)."""
        h_apex = 1e-4
        pts = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.5, float(np.sqrt(3) / 2.0), 0.0],
            [0.5, float(np.sqrt(3) / 6.0), h_apex],
        ], dtype=np.float64)
        c0 = np.array([0, 1, 2, 3], dtype=np.int64)

        mesh = compute_difvm_topology(pts, [c0])
        assert mesh.cell_volumes[0] > 0.0, "Tetrahedron volume must be strictly positive"
        assert math.isclose(mesh.cell_volumes[0], (np.sqrt(3) / 4.0) * h_apex / 3.0, rel_tol=1e-4)

        # Gauss divergence closure
        s_sum = np.sum(mesh.face_areas, axis=0)
        assert np.linalg.norm(s_sum) < 1e-11, "Closed tetrahedron must have zero face area vector sum"

    def test_degenerate_zero_and_inverted_volume_cell_audit(self):
        """Verify that zero-thickness and inverted cells are safely detected and flagged."""
        # 1. Zero-volume flat cell (z = 0 for all nodes)
        pts_zero = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        ], dtype=np.float64)
        c0 = np.arange(8, dtype=np.int64)
        mesh_zero = compute_difvm_topology(pts_zero, [c0])

        mover = ALEMeshMover()
        bad_zero = mover.detect_degenerate_cells(mesh_zero)
        assert 0 in bad_zero, "Zero-volume collapsed cell must be flagged as degenerate"

        # 2. Inverted cell (top and bottom coordinates reversed, yielding negative volume)
        pts_inv = np.array([
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],  # Bottom at z=1
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],  # Top at z=0 (inverted)
        ], dtype=np.float64)
        mesh_inv = compute_difvm_topology(pts_inv, [c0])
        assert mesh_inv.cell_volumes[0] < 0.0, "Inverted cell must yield signed negative volume"

        bad_inv = mover.detect_degenerate_cells(mesh_inv)
        assert 0 in bad_inv, "Inverted negative-volume cell must be flagged as degenerate"


# ==============================================================================
# 3. Solver Stabilization Mask Verification (Tiers 0, 1, 2, 3)
# ==============================================================================

class TestAdversarialSolverStabilizationMaskVerification:
    """Stress tests on error_map stabilization masks across non-orthogonality and skewness tiers."""

    @pytest.mark.parametrize("theta_deg,expected_tier", [
        (25.0, 0),  # Tier 0: theta < 40 deg
        (50.0, 1),  # Tier 1: 40 <= theta < 65 deg
        (70.0, 2),  # Tier 2: 65 <= theta < 75 deg
        (80.0, 3),  # Tier 3: theta >= 75 deg
    ])
    def test_stabilization_mask_pure_non_orthogonality_tiers(self, theta_deg: float, expected_tier: int):
        """Verify solver stabilization mask on pure non-orthogonality (skewness == 0)."""
        pts, cells = make_sheared_two_cell_hex_mesh(theta_deg)

        for engine_type, use_nat in [("python", False), ("native", True)]:
            mesh = compute_difvm_topology(pts, cells, use_native=use_nat)

            # Internal face is face 0
            computed_non_ortho = float(mesh.error_map["non_orthogonality"][0])
            computed_skewness = float(mesh.error_map["skewness"][0])
            mask_tier = int(mesh.error_map["stabilization_mask"][0])
            mask_alias = int(mesh.error_map["mask"][0])

            assert math.isclose(computed_non_ortho, theta_deg, abs_tol=1e-6), (
                f"[{engine_type}] Computed non-ortho {computed_non_ortho} != {theta_deg}"
            )
            assert math.isclose(computed_skewness, 0.0, abs_tol=1e-12), (
                f"[{engine_type}] Skewness {computed_skewness} must be 0 for pure shear"
            )
            assert mask_tier == expected_tier, (
                f"[{engine_type}] Expected Tier {expected_tier} for theta={theta_deg}, got {mask_tier}"
            )
            assert mask_alias == mask_tier, f"[{engine_type}] mask alias must equal stabilization_mask"

    @pytest.mark.parametrize("skewness_val,expected_tier", [
        (0.15, 0),  # Tier 0: skew < 0.4
        (0.50, 1),  # Tier 1: 0.4 <= skew < 0.7
        (0.85, 2),  # Tier 2: 0.7 <= skew < 1.0
        (1.20, 3),  # Tier 3: skew >= 1.0
    ])
    def test_stabilization_mask_pure_skewness_tiers(self, skewness_val: float, expected_tier: int):
        """Verify solver stabilization mask on pure skewness (non-orthogonality == 0)."""
        pts, cells = make_skewed_two_cell_hex_mesh(skewness_val)

        for engine_type, use_nat in [("python", False), ("native", True)]:
            mesh = compute_difvm_topology(pts, cells, use_native=use_nat)

            computed_non_ortho = float(mesh.error_map["non_orthogonality"][0])
            computed_skewness = float(mesh.error_map["skewness"][0])
            mask_tier = int(mesh.error_map["stabilization_mask"][0])

            assert math.isclose(computed_non_ortho, 0.0, abs_tol=1e-12), (
                f"[{engine_type}] Non-ortho {computed_non_ortho} must be 0 for pure skew"
            )
            assert math.isclose(computed_skewness, skewness_val, abs_tol=1e-6), (
                f"[{engine_type}] Computed skewness {computed_skewness} != {skewness_val}"
            )
            assert mask_tier == expected_tier, (
                f"[{engine_type}] Expected Tier {expected_tier} for skew={skewness_val}, got {mask_tier}"
            )

    def test_stabilization_mask_threshold_boundary_continuity(self):
        """Verify stabilization tier step transitions at exact boundary thresholds."""
        # Test just below and just above 40 deg
        pts_39, cells_39 = make_sheared_two_cell_hex_mesh(39.99)
        pts_40, cells_40 = make_sheared_two_cell_hex_mesh(40.01)

        m39 = compute_difvm_topology(pts_39, cells_39)
        m40 = compute_difvm_topology(pts_40, cells_40)
        assert m39.error_map["stabilization_mask"][0] == 0, "39.99 deg must be Tier 0"
        assert m40.error_map["stabilization_mask"][0] == 1, "40.01 deg must be Tier 1"

        # Test just below and just above 65 deg
        pts_64, cells_64 = make_sheared_two_cell_hex_mesh(64.99)
        pts_65, cells_65 = make_sheared_two_cell_hex_mesh(65.01)

        m64 = compute_difvm_topology(pts_64, cells_64)
        m65 = compute_difvm_topology(pts_65, cells_65)
        assert m64.error_map["stabilization_mask"][0] == 1, "64.99 deg must be Tier 1"
        assert m65.error_map["stabilization_mask"][0] == 2, "65.01 deg must be Tier 2"

        # Test just below and just above 75 deg
        pts_74, cells_74 = make_sheared_two_cell_hex_mesh(74.99)
        pts_75, cells_75 = make_sheared_two_cell_hex_mesh(75.01)

        m74 = compute_difvm_topology(pts_74, cells_74)
        m75 = compute_difvm_topology(pts_75, cells_75)
        assert m74.error_map["stabilization_mask"][0] == 2, "74.99 deg must be Tier 2"
        assert m75.error_map["stabilization_mask"][0] == 3, "75.01 deg must be Tier 3"

    def test_error_map_key_completeness_and_aliases(self):
        """Verify presence of canonical keys and mask alias in error_map for both backends."""
        pts, cells = make_sheared_two_cell_hex_mesh(50.0)
        for use_nat in [False, True]:
            mesh = compute_difvm_topology(pts, cells, use_native=use_nat)
            for k in ["non_orthogonality", "skewness", "aspect_ratio", "stabilization_mask"]:
                assert k in mesh.error_map, f"Missing canonical key '{k}' in error_map"
            assert "mask" in mesh.error_map, "Missing 'mask' alias in error_map"
            assert np.array_equal(mesh.error_map["mask"], mesh.error_map["stabilization_mask"])




# ==============================================================================
# 4. Cyclic Boundary Symmetry Matching under Numerical Perturbations
# ==============================================================================

class TestAdversarialCyclicBoundarySymmetryPerturbations:
    """Stress tests on rotational cyclic periodic symmetry under noise and angular transforms."""

    @pytest.mark.parametrize("noise_amp", [0.0, 1e-7, 1e-5, 5e-5])
    def test_cyclic_matching_under_subtolerance_perturbations(self, noise_amp: float):
        """Verify cyclic periodic face pairing is 100% stable under noise below tolerance (1e-4)."""
        pts, cells = make_cyclic_sector_mesh(sector_angle_rad=np.pi / 2.0)
        rng = np.random.default_rng(1234)
        pts_pert = pts + rng.uniform(-noise_amp, noise_amp, size=pts.shape)

        mesh = compute_difvm_topology(pts_pert, cells, cyclic_angle_rad=np.pi / 2.0)
        assert len(mesh.cyclic_pairs) == 6, (
            f"Under noise {noise_amp}, expected 6 periodic face pairs, got {len(mesh.cyclic_pairs)}"
        )

        # Verify geometric transform R_z(theta) * c1 == c2 within tolerance
        cos_t = np.cos(np.pi / 2.0)
        sin_t = np.sin(np.pi / 2.0)
        R_z = np.array([[cos_t, -sin_t, 0.0], [sin_t, cos_t, 0.0], [0.0, 0.0, 1.0]])

        for f1, f2 in mesh.cyclic_pairs:
            c1 = mesh.face_centres[f1]
            c2 = mesh.face_centres[f2]
            err = np.linalg.norm(np.dot(R_z, c1) - c2)
            assert err < 1e-4, f"Periodic face center transform error {err} >= 1e-4"

            # Area vector transformation: magnitude must match and R_z * S1 == S2
            s1 = mesh.face_areas[f1]
            s2 = mesh.face_areas[f2]
            assert math.isclose(np.linalg.norm(s1), np.linalg.norm(s2), rel_tol=1e-3)
            rot_s1 = np.dot(R_z, s1)
            assert np.linalg.norm(rot_s1 - s2) < 1e-3, "Area vector must rotate by R_z"

    def test_cyclic_matching_tolerance_boundary_adaptation(self):
        """Verify that when noise exceeds default tolerance, custom tolerance restores pairing."""
        pts, cells = make_cyclic_sector_mesh(sector_angle_rad=np.pi / 2.0)
        rng = np.random.default_rng(42)
        noise = 2e-4  # Exceeds default tolerance of 1e-4
        pts_pert = pts + rng.uniform(-noise, noise, size=pts.shape)

        mesh = compute_difvm_topology(pts_pert, cells, cyclic_angle_rad=0.0)

        # Default tolerance of 1e-4 should miss pairs
        pairs_default = extract_cyclic_pairs(mesh, np.pi / 2.0, tolerance=1e-4)
        assert len(pairs_default) < 6, "Default tolerance must reject pairs displaced by 2e-4"

        # Adapted tolerance of 5e-4 must successfully recover all 6 pairs
        pairs_adapted = extract_cyclic_pairs(mesh, np.pi / 2.0, tolerance=5e-4)
        assert len(pairs_adapted) == 6, "Custom tolerance must recover all 6 pairs"

    @pytest.mark.parametrize("sector_deg", [30.0, 45.0, 60.0, 90.0])
    def test_cyclic_arbitrary_sector_angles(self, sector_deg: float):
        """Verify cyclic periodic pairing across various turbomachinery sector angles."""
        angle_rad = float(np.deg2rad(sector_deg))
        pts, cells = make_cyclic_sector_mesh(sector_angle_rad=angle_rad)

        mesh = compute_difvm_topology(pts, cells, cyclic_angle_rad=angle_rad)
        assert len(mesh.cyclic_pairs) == 6, f"Expected 6 pairs for sector angle {sector_deg} deg"

    def test_cyclic_native_dll_and_python_parity(self):
        """Verify cfd_mesher_extract_cyclic_pairs in C++ DLL matches Python extract_cyclic_pairs."""
        dll = get_native_dll()
        assert dll is not None, "cfd_mesher_native.dll must be loaded"

        pts, cells = make_cyclic_sector_mesh(sector_angle_rad=np.pi / 2.0)
        mesh_py = compute_difvm_topology_python(pts, cells)
        py_pairs = extract_cyclic_pairs(mesh_py, np.pi / 2.0, tolerance=1e-4)

        # Query native DLL
        pts_flat = pts.astype(np.float64).ravel()
        cell_nodes = np.concatenate(cells).astype(np.int64)
        cell_offsets = np.array([0, 8, 16], dtype=np.int64)

        handle = dll.cfd_mesher_create()
        dll.cfd_mesher_build_from_cells(
            handle,
            pts_flat.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(pts),
            cell_offsets.ctypes.data_as(ctypes.POINTER(ctypes.c_int64)),
            cell_nodes.ctypes.data_as(ctypes.POINTER(ctypes.c_int64)),
            len(cells),
            None,
        )

        max_p = 100
        out1 = (ctypes.c_int64 * max_p)()
        out2 = (ctypes.c_int64 * max_p)()
        n_native = dll.cfd_mesher_extract_cyclic_pairs(handle, np.pi / 2.0, 1e-4, out1, out2, max_p)
        dll.cfd_mesher_free(handle)

        assert n_native == len(py_pairs), f"Native count {n_native} != Python count {len(py_pairs)}"
        native_pairs = [(int(out1[i]), int(out2[i])) for i in range(n_native)]
        assert sorted(native_pairs) == sorted(py_pairs), "Native and Python cyclic pairs must match identically"

    def test_cyclic_null_and_boundary_c_abi_safety(self):
        """Verify C-ABI null pointer guards and error handling for cyclic pairing."""
        dll = get_native_dll()
        assert dll is not None
        # Passing NULL handle or null output arrays must return 0 without crashing
        ret = dll.cfd_mesher_extract_cyclic_pairs(None, 0.0, 1e-4, None, None, 0)
        assert ret == 0, "C-ABI must return 0 when null handle is passed"



# ==============================================================================
# 5. High-Frequency ALE Mesh Oscillations and DGCL
# ==============================================================================

class TestAdversarialHighFrequencyALEMeshOscillations:
    """Stress tests on moving mesh ALE Laplacian smoothing under high-frequency boundary vibrations."""

    def _build_cube_hex_mesh(self, nx: int = 4, ny: int = 4, nz: int = 4) -> Tuple[np.ndarray, UnstructuredMesh]:
        x = np.linspace(0.0, 1.0, nx + 1)
        y = np.linspace(0.0, 1.0, ny + 1)
        z = np.linspace(0.0, 1.0, nz + 1)
        points = []
        pidx = {}
        for k in range(nz + 1):
            for j in range(ny + 1):
                for i in range(nx + 1):
                    pidx[(i, j, k)] = len(points)
                    points.append([x[i], y[j], z[k]])
        points_arr = np.array(points, dtype=np.float64)

        cells = []
        for k in range(nz):
            for j in range(ny):
                for i in range(nx):
                    nodes = [
                        pidx[(i, j, k)], pidx[(i + 1, j, k)], pidx[(i + 1, j + 1, k)], pidx[(i, j + 1, k)],
                        pidx[(i, j, k + 1)], pidx[(i + 1, j, k + 1)], pidx[(i + 1, j + 1, k + 1)], pidx[(i, j + 1, k + 1)],
                    ]
                    cells.append(np.array(nodes, dtype=np.int64))

        mesh = compute_difvm_topology(points_arr, cells)
        return points_arr, mesh

    def test_ale_high_frequency_spatial_oscillation(self):
        """Stress-test high-frequency spatial boundary ripple u_z = A * sin(k*pi*x) * cos(k*pi*y)."""
        pts, mesh = self._build_cube_hex_mesh(nx=4, ny=4, nz=4)
        mover = ALEMeshMover()

        top_mask = np.isclose(pts[:, 2], 1.0)
        fixed_mask = np.isclose(pts[:, 2], 0.0)
        fixed_indices = np.where(fixed_mask)[0]

        # Moderate amplitude high-frequency wave (k=4)
        k_freq = 4.0
        A_mod = 0.02
        bnd_disp = np.zeros_like(pts)
        bnd_disp[top_mask, 2] = A_mod * np.sin(k_freq * np.pi * pts[top_mask, 0]) * np.cos(k_freq * np.pi * pts[top_mask, 1])

        deformed = mover.deform_mesh(mesh, bnd_disp, fixed_nodes=fixed_indices)

        # 1. Pinned nodes must experience zero displacement (< 10^-12)
        pinned_disp = np.linalg.norm(deformed.points[fixed_indices] - pts[fixed_indices], axis=1)
        assert np.max(pinned_disp) < 1e-12, "Fixed nodes must have zero displacement"

        # 2. Interior nodes smoothly deform and cell volumes remain positive
        assert np.all(deformed.cell_volumes > 0.0), "All volumes must remain positive under moderate wave"
        bad_cells = mover.detect_degenerate_cells(deformed)
        assert len(bad_cells) == 0, "No degenerate cells should be flagged for moderate wave"

        # Extreme amplitude causing severe local compression
        A_ext = 0.60
        bnd_disp[top_mask, 2] = -A_ext * (np.sin(2.0 * np.pi * pts[top_mask, 0]) ** 2)
        deformed_ext = mover.deform_mesh(mesh, bnd_disp, fixed_nodes=fixed_indices)

        # Engine must not crash, and degenerate cell detector must identify the compressed cells
        bad_compressed = mover.detect_degenerate_cells(deformed_ext)
        assert len(bad_compressed) > 0, "Compressed/inverted cells must be detected and flagged"

    def test_ale_multi_cycle_temporal_oscillation_dgcl(self):
        """Simulate multi-step high-frequency boundary oscillations and verify DGCL execution."""
        pts, mesh = self._build_cube_hex_mesh(nx=3, ny=3, nz=3)
        mover = ALEMeshMover()

        top_mask = np.isclose(pts[:, 2], 1.0)
        fixed_indices = np.where(np.isclose(pts[:, 2], 0.0))[0]

        n_steps = 15
        omega = 25.0
        dt = 0.01
        A = 0.015

        current_mesh = mesh
        for step in range(n_steps):
            t = step * dt
            bnd_disp = np.zeros_like(pts)
            bnd_disp[top_mask, 0] = A * np.sin(omega * t)
            bnd_disp[top_mask, 1] = A * np.cos(omega * t)

            next_mesh = mover.deform_mesh(current_mesh, bnd_disp, fixed_nodes=fixed_indices)

            # Grid velocity
            w = mover.compute_mesh_velocity(current_mesh.points, next_mesh.points, dt)
            assert np.all(np.isfinite(w)), "Grid velocity must be finite"

            # Verify DGCL runs stably
            dgcl_res = mover.verify_dgcl(current_mesh, next_mesh, dt)
            assert np.isfinite(dgcl_res), "DGCL residual must be finite"

            current_mesh = next_mesh

        assert np.all(current_mesh.cell_volumes > 0.0), "Volumes must remain positive after multi-cycle vibration"

    def test_ale_extreme_torsion_rotation(self):
        """Stress-test torsional twist of boundary nodes around the z-axis."""
        pts, mesh = self._build_cube_hex_mesh(nx=4, ny=4, nz=4)
        mover = ALEMeshMover()

        top_mask = np.isclose(pts[:, 2], 1.0)
        fixed_indices = np.where(np.isclose(pts[:, 2], 0.0))[0]

        # Twist top face around center (0.5, 0.5, 1.0) by angle theta
        theta = float(np.deg2rad(45.0))
        top_pts = pts[top_mask]
        xc, yc = 0.5, 0.5
        rx = top_pts[:, 0] - xc
        ry = top_pts[:, 1] - yc
        new_rx = rx * np.cos(theta) - ry * np.sin(theta)
        new_ry = rx * np.sin(theta) + ry * np.cos(theta)

        bnd_disp = np.zeros_like(pts)
        bnd_disp[top_mask, 0] = (xc + new_rx) - top_pts[:, 0]
        bnd_disp[top_mask, 1] = (yc + new_ry) - top_pts[:, 1]

        deformed = mover.deform_mesh(mesh, bnd_disp, fixed_nodes=fixed_indices)

        # Pinned bottom nodes must have zero displacement (< 10^-12)
        pinned_disp = np.linalg.norm(deformed.points[fixed_indices] - pts[fixed_indices], axis=1)
        assert np.max(pinned_disp) < 1e-12, "Fixed bottom nodes must remain undisturbed"

        # Quality audit must run without crash or hang
        bad_cells = mover.detect_degenerate_cells(deformed)
        assert isinstance(bad_cells, list)


# ==============================================================================
# 6. SPH-FVM Coupling and Robustness Under Adversarial Inputs
# ==============================================================================

class TestAdversarialCouplingHardening:
    """Stress tests on SPH boundary detection, virtual wall vector, and OPMIS Taylor solver."""

    def test_sph_wall_vector_zero_and_micro_volume_particles(self):
        """Verify SPHFVMCoupler computes A_iS consistently even with micro-volume particles."""
        coupler = SPHFVMCoupler()
        # 10 particles on a line with decreasing volumes down to 1e-20
        pos = np.column_stack([np.linspace(0.0, 0.5, 10), np.zeros(10), np.zeros(10)])
        vol = np.logspace(-3, -20, 10)
        h = 0.1

        A_iS, sum_grad = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True, dim=3)
        assert np.all(np.isfinite(A_iS)), "Virtual wall vectors must remain finite"
        assert np.all(np.isfinite(sum_grad)), "Kernel gradient sums must remain finite"

        # On boundary particles, A_iS + sum_grad must be < 10^-10
        div_r = coupler.compute_position_divergence(pos, vol, h, dim=3)
        bnd_mask = div_r < 0.75 * 3
        if np.any(bnd_mask):
            closure_err = np.linalg.norm(A_iS[bnd_mask] + sum_grad[bnd_mask], axis=1)
            assert np.max(closure_err) < 1e-10, "0th-order wall consistency must hold"

    def test_opmis_taylor_linear_field_reproduction_adversarial_jitter(self):
        """Stress-test OPMIS Taylor P2M interpolation on randomly perturbed 3D particle cloud."""
        interp = OPMISTaylorInterpolator(k_neighbors_p2m=20)
        nx, ny, nz = 3, 3, 3
        pts, mesh = TestAdversarialHighFrequencyALEMeshOscillations()._build_cube_hex_mesh(nx, ny, nz)

        # Randomly jittered particles in the domain
        rng = np.random.default_rng(999)
        n_p = 250
        pos = rng.uniform(0.05, 0.95, size=(n_p, 3))
        vol = np.full(n_p, 1.0 / n_p, dtype=np.float64)
        h = 0.25

        # Linear polynomial: f(x, y, z) = 3.5 * x - 2.1 * y + 1.8 * z + 7.2
        a = np.array([3.5, -2.1, 1.8], dtype=np.float64)
        b = 7.2
        field_particles = np.dot(pos, a) + b

        interp_mesh = interp.interpolate_p2m(pos, field_particles, vol, h, mesh)
        assert np.all(np.isfinite(interp_mesh)), "Interpolated mesh field must be finite"

        # Compare at mesh cell centroids
        expected_mesh = np.dot(mesh.cell_centres, a) + b
        diff = np.abs(interp_mesh - expected_mesh)
        max_err = float(np.max(diff))
        assert max_err < 1e-11, f"OPMIS Taylor linear reproduction L_inf error {max_err} must be < 1e-11"

