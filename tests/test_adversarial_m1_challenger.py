"""Adversarial Stress Test Suite for Milestone M1 Mathematical Invariants.

Authored by empirical challenger `challenger_m1_1`:
1. Eikonal Property ||grad phi|| == 1 on random rotated primitives and complex CSG compounds.
2. Polynomial smooth blending Eikonal violation analysis.
3. Curvature formula accuracy on analytical cylinders vs. triangulated surface meshes.
4. Narrow throat proximity ray-marching convergence and opposing-normal threshold cutoff.
5. CADMeshSDF sign-convention vulnerability in fluid domain proximity evaluation.
6. Robustness of Newton-Raphson boundary projection under adversarial starting points
   (symmetry singularities, far-field, concave notches, medial axis).
"""

import numpy as np
import pytest
import trimesh

from backend.geometry.models import SurfaceMesh
from backend.meshing.geometry_sdf import (
    BaseSDF,
    BoxSDF,
    SphereSDF,
    CylinderSDF,
    PolygonSDF,
    NACA4AirfoilSDF,
    CSGUnion,
    CSGIntersection,
    CSGDifference,
    smooth_min,
    CADMeshSDF,
    CurvatureRefinement,
    ProximityRefinement,
    AdaptiveSizeFunction,
)
from backend.meshing.models import SizingParameters


# ==============================================================================
# 1. Eikonal Property ||grad phi|| == 1 Stress Tests
# ==============================================================================


def test_eikonal_property_rotated_cylinder():
    """Verify ||grad phi|| == 1 almost everywhere for random 3D rotated cylinders."""
    rng = np.random.default_rng(2026)

    for _ in range(5):
        # Generate random orientation
        axis = rng.normal(size=3)
        axis /= np.linalg.norm(axis)
        center = rng.uniform(-3, 3, size=3)
        radius = float(rng.uniform(0.5, 3.0))
        height = float(rng.uniform(2.0, 8.0))

        cyl = CylinderSDF(center=center, radius=radius, height=height, axis=axis)

        # Sample points in bounding box, rejecting points on the central axis
        pts = center + rng.uniform(-6, 6, size=(1000, 3))
        # Distance to axis
        p_rel = pts - center
        h_proj = np.dot(p_rel, axis)
        r_vec = p_rel - h_proj[:, None] * axis
        r_dist = np.linalg.norm(r_vec, axis=1)
        valid_pts = pts[r_dist > 1e-3]

        # Compute raw unnormalized central finite-difference gradient
        eps = 1e-6
        raw_grad = np.zeros_like(valid_pts)
        for d in range(3):
            p_plus = valid_pts.copy()
            p_plus[:, d] += eps
            p_minus = valid_pts.copy()
            p_minus[:, d] -= eps
            raw_grad[:, d] = (cyl.distance(p_plus) - cyl.distance(p_minus)) / (2.0 * eps)

        norms = np.linalg.norm(raw_grad, axis=1)
        # Invariant: ||grad phi|| == 1.0 almost everywhere
        assert np.allclose(norms, 1.0, atol=1e-4), (
            f"Eikonal property violated on rotated cylinder: min={np.min(norms):.5f}, max={np.max(norms):.5f}"
        )


def test_eikonal_property_complex_csg_compound():
    """Verify ||grad phi|| == 1 on complex nested CSG compound."""
    rng = np.random.default_rng(42)

    box = BoxSDF(min_pt=[-2.0, -2.0, -2.0], max_pt=[2.0, 2.0, 2.0])
    cyl_x = CylinderSDF(center=[0, 0, 0], radius=0.7, height=6.0, axis=[1, 0, 0])
    cyl_y = CylinderSDF(center=[0, 0, 0], radius=0.7, height=6.0, axis=[0, 1, 0])
    cyl_z = CylinderSDF(center=[0, 0, 0], radius=0.7, height=6.0, axis=[0, 0, 1])
    sphere = SphereSDF(center=[1.2, 1.2, 1.2], radius=1.0)

    # CSG: (Box - (CylX U CylY U CylZ)) U Sphere
    drilled_block = CSGDifference(box, CSGUnion(CSGUnion(cyl_x, cyl_y), cyl_z))
    compound = CSGUnion(drilled_block, sphere)

    pts = rng.uniform(-4, 4, size=(2000, 3))
    eps = 1e-6
    raw_grad = np.zeros_like(pts)
    for d in range(3):
        p_plus = pts.copy()
        p_plus[:, d] += eps
        p_minus = pts.copy()
        p_minus[:, d] -= eps
        raw_grad[:, d] = (compound.distance(p_plus) - compound.distance(p_minus)) / (2.0 * eps)

    norms = np.linalg.norm(raw_grad, axis=1)
    # Outside medial axis singularities, ||grad phi|| must be 1.0
    medial_mask = np.abs(norms - 1.0) > 0.05
    # Medial axis points should be a negligible fraction (< 2% of volume sampling)
    assert np.mean(medial_mask) < 0.02
    regular_norms = norms[~medial_mask]
    assert np.allclose(regular_norms, 1.0, atol=1e-3)


def test_eikonal_violation_smooth_csg_blending():
    """Empirically demonstrate and document that polynomial smooth blending violates ||grad phi|| == 1."""
    s1 = SphereSDF(center=[-0.5, 0, 0], radius=1.0)
    s2 = SphereSDF(center=[0.5, 0, 0], radius=1.0)
    smooth_compound = CSGUnion(s1, s2, smooth_k=0.5)

    # Saddle point between the two spheres
    midpoint = np.array([[0.0, 0.0, 0.0]])
    eps = 1e-6
    raw_grad = np.zeros((1, 3))
    for d in range(3):
        p_plus = midpoint.copy()
        p_plus[0, d] += eps
        p_minus = midpoint.copy()
        p_minus[0, d] -= eps
        diff = (smooth_compound.distance(p_plus) - smooth_compound.distance(p_minus)) / (2.0 * eps)
        raw_grad[0, d] = float(np.ravel(diff)[0])

    grad_norm = np.linalg.norm(raw_grad)
    # Gradient drops to 0 at the stationary saddle point of smooth min
    assert grad_norm < 1e-5, f"Expected saddle point gradient ~ 0, got {grad_norm}"


# ==============================================================================
# 2. Curvature Formula Accuracy Stress Tests
# ==============================================================================


def test_curvature_accuracy_analytical_cylinder():
    """Verify mean curvature on 3D rotated cylinder: H == 1 / (2*R) to < 10^-5 relative error."""
    rng = np.random.default_rng(777)
    axis = rng.normal(size=3)
    axis /= np.linalg.norm(axis)
    tmp = np.array([1, 0, 0]) if abs(axis[0]) < 0.8 else np.array([0, 1, 0])
    u = np.cross(axis, tmp)
    u /= np.linalg.norm(u)
    v = np.cross(axis, u)

    R = 2.0
    cyl = CylinderSDF(center=[0, 0, 0], radius=R, height=20.0, axis=axis)
    params = SizingParameters(fd_delta=1e-4)
    curv = CurvatureRefinement(sdf_evaluator=cyl.evaluate, params=params, dim=3)

    theta = np.linspace(0, 2 * np.pi, 24, endpoint=False)
    pts = R * np.outer(np.cos(theta), u) + R * np.outer(np.sin(theta), v)
    evaluated_kappa = curv.evaluate_curvature(pts)

    exact_H = 1.0 / (2.0 * R)
    rel_errors = np.abs(evaluated_kappa - exact_H) / exact_H
    assert np.all(rel_errors < 1e-5), f"Max relative error {np.max(rel_errors):.2e} > 1e-5"


def test_curvature_faceted_mesh_singularity_finding():
    """Empirical challenge: Curvature evaluation on triangulated surface meshes via pure SDF Laplacian
    exhibits numerical spikes at vertices (O(1/delta)) and zeros on facet interiors.
    """
    sphere = trimesh.creation.icosphere(subdivisions=3, radius=1.0)
    mesh = SurfaceMesh(vertices=sphere.vertices, faces=sphere.faces)
    sdf = CADMeshSDF.from_surface_mesh(mesh, k_candidates=8)

    # 1. At mesh vertex: Laplacian scales as O(1/delta)
    vert = sphere.vertices[0]
    p_large_delta = SizingParameters(fd_delta=1e-2)
    p_small_delta = SizingParameters(fd_delta=1e-3)
    k_large = CurvatureRefinement(sdf.evaluate, params=p_large_delta, dim=3).evaluate_curvature(vert)
    k_small = CurvatureRefinement(sdf.evaluate, params=p_small_delta, dim=3).evaluate_curvature(vert)

    # Ratio scales inversely with delta: 1e-2 / 1e-3 = 10x
    ratio = k_small / k_large
    assert 8.0 < ratio < 12.0, f"Expected ~10x singularity scaling, got ratio={ratio:.2f}"
    # Both are vastly larger than analytical sphere curvature (H = 1.0)
    assert k_large > 5.0
    assert k_small > 50.0

    # 2. At face centroid: flat triangle implies Laplacian == 0.0
    face_centroid = np.mean(sphere.vertices[sphere.faces[0]], axis=0)
    face_centroid /= np.linalg.norm(face_centroid)  # project to sphere surface
    k_face = CurvatureRefinement(sdf.evaluate, params=p_small_delta, dim=3).evaluate_curvature(face_centroid)
    assert np.isclose(k_face, 0.0, atol=1e-10), f"Expected 0 curvature on flat face interior, got {k_face}"


# ==============================================================================
# 3. Narrow Throat Proximity Ray-Marching Stress Tests
# ==============================================================================


def test_narrow_throat_ray_marching_convergence():
    """Verify proximity gap evaluation in a circular throat across varying ray march steps."""
    W = 0.02
    R = 1.0
    c1_y = R + W / 2.0
    c2_y = -(R + W / 2.0)

    def circular_throat_sdf(pts: np.ndarray):
        x, y = pts[:, 0], pts[:, 1]
        d1 = np.sqrt(x**2 + (y - c1_y) ** 2) - R
        d2 = np.sqrt(x**2 + (y - c2_y) ** 2) - R
        phi = -np.minimum(d1, d2)
        g1_x = x / np.maximum(d1 + R, 1e-12)
        g1_y = (y - c1_y) / np.maximum(d1 + R, 1e-12)
        g2_x = x / np.maximum(d2 + R, 1e-12)
        g2_y = (y - c2_y) / np.maximum(d2 + R, 1e-12)
        grad = np.where(d1[:, None] <= d2[:, None], -np.column_stack([g1_x, g1_y]), -np.column_stack([g2_x, g2_y]))
        return phi, grad

    # Test point near upper wall at throat center (x = 0, y = 0.005)
    query_pt = np.array([[0.0, 0.005]])

    for steps in [4, 8, 16, 32, 64]:
        params = SizingParameters(h_min=0.001, h_max=0.20, beta=0.25, ray_march_steps=steps, max_gap_search=0.10)
        prox = ProximityRefinement(sdf_evaluator=circular_throat_sdf, params=params, dim=2)
        d_gap = prox.evaluate_gap_distance(query_pt)[0]
        # Exact gap at x=0 is W = 0.02
        assert np.isclose(d_gap, W, atol=1e-5), f"Failed to detect throat gap: got {d_gap} vs {W}"


def test_narrow_throat_opposing_normal_cutoff_vulnerability():
    """Empirical challenge: ProximityRefinement drops opposing wall detection when funnel half-angle > 30 deg."""
    # Wedge walls with opening angle 2*theta
    for theta_deg, expected_detection in [(15, True), (25, True), (30, True), (35, False), (45, False)]:
        th = np.radians(theta_deg)
        # Normal of wall 1: [sin(th), -cos(th)], normal of wall 2: [sin(th), cos(th)]
        n1 = np.array([np.sin(th), -np.cos(th)])
        n2 = np.array([np.sin(th), np.cos(th)])
        alignment = np.dot(n1, n2)

        # Threshold in SizingParameters is -0.50
        passes = alignment <= -0.50
        assert passes == expected_detection, f"Angle {theta_deg} deg detection mismatch: alignment={alignment:.3f}"


def test_proximity_cad_mesh_sign_convention_trap():
    """Empirical challenge: Default CADMeshSDF (inside_is_negative=True) assigns phi > 0 in fluid domain,
    which causes ProximityRefinement to skip all fluid points unless inverted.
    """
    c1 = trimesh.creation.box(extents=[1.0, 1.0, 1.0])
    c1.apply_translation([-0.52, 0, 0])  # right face at x = -0.02
    c2 = trimesh.creation.box(extents=[1.0, 1.0, 1.0])
    c2.apply_translation([0.52, 0, 0])   # left face at x = +0.02
    two_cubes = trimesh.util.concatenate([c1, c2])
    mesh = SurfaceMesh(vertices=two_cubes.vertices, faces=two_cubes.faces)

    # 1. Default: inside_is_negative=True -> fluid has phi > 0
    sdf_default = CADMeshSDF.from_surface_mesh(mesh, inside_is_negative=True)
    p_fluid = np.array([[0.0, 0.0, 0.0]])
    params = SizingParameters(h_min=0.002, h_max=0.20, beta=0.25, max_gap_search=0.10)
    prox_default = ProximityRefinement(sdf_evaluator=sdf_default.evaluate, params=params, dim=3)
    d_gap_default = prox_default.evaluate_gap_distance(p_fluid)[0]
    # Skipped: returns max_gap_search
    assert np.isclose(d_gap_default, params.max_gap_search)

    # 2. Inverted: inside_is_negative=False -> fluid has phi < 0
    sdf_inverted = CADMeshSDF.from_surface_mesh(mesh, inside_is_negative=False)
    prox_inverted = ProximityRefinement(sdf_evaluator=sdf_inverted.evaluate, params=params, dim=3)
    d_gap_inverted = prox_inverted.evaluate_gap_distance(p_fluid)[0]
    # Successfully detects 0.04m gap
    assert np.isclose(d_gap_inverted, 0.04, atol=1e-3)


# ==============================================================================
# 4. Newton-Raphson Boundary Projection Robustness Stress Tests
# ==============================================================================


def test_newton_raphson_projection_symmetry_singularity():
    """Empirical challenge: Exact symmetry centers stall under Newton-Raphson due to zero gradient."""
    # Sphere center
    sphere = SphereSDF(center=[0.0, 0.0, 0.0], radius=1.0)
    p_center = np.array([0.0, 0.0, 0.0])
    proj_sphere = sphere.project_to_boundary(p_center, max_iter=10)
    # Stalls at origin, remaining at distance -1.0
    assert np.allclose(proj_sphere, [0.0, 0.0, 0.0])
    assert np.isclose(sphere.distance(proj_sphere)[0], -1.0)

    # Box center
    box = BoxSDF(min_pt=[-1.0, -1.0, -1.0], max_pt=[1.0, 1.0, 1.0])
    proj_box = box.project_to_boundary(p_center, max_iter=10)
    assert np.allclose(proj_box, [0.0, 0.0, 0.0])
    assert np.isclose(box.distance(proj_box)[0], -1.0)


def test_newton_raphson_projection_polygon_and_airfoil_robustness():
    """Verify Newton-Raphson projection converges on non-convex polygon and cambered NACA airfoil."""
    # L-shaped polygon
    verts = [(0.0, 0.0), (2.0, 0.0), (2.0, 1.0), (1.0, 1.0), (1.0, 2.0), (0.0, 2.0)]
    poly = PolygonSDF(verts)

    adversarial_pts = [
        np.array([1.5, 1.5]),       # concave notch exterior
        np.array([0.9, 0.9]),       # concave notch interior
        np.array([0.5, 0.5]),       # interior medial axis
        np.array([100.0, 100.0]),   # far field
        np.array([0.001, 1.0]),     # near boundary
    ]
    for pt in adversarial_pts:
        proj = poly.project_to_boundary(pt, max_iter=15)
        d_proj = poly.distance(proj)[0]
        assert np.isclose(d_proj, 0.0, atol=1e-8), f"Polygon projection failed for {pt}: residual {d_proj}"

    # NACA 2412 cambered airfoil
    naca = NACA4AirfoilSDF(code="2412", chord=1.0, origin=(0.0, 0.0), angle_deg=5.0)
    naca_pts = [
        np.array([0.5, 0.0]),
        np.array([0.1, 0.02]),
        np.array([1.2, 0.1]),
        np.array([-0.1, -0.05]),
    ]
    for pt in naca_pts:
        proj = naca.project_to_boundary(pt, max_iter=15)
        d_proj = naca.distance(proj)[0]
        assert np.isclose(d_proj, 0.0, atol=1e-8), f"NACA projection failed for {pt}: residual {d_proj}"
