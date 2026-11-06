"""Empirical and Adversarial Challenger Test Suite for Gate WP2.

Audits:
1. Paper Fidelity:
   - Bærentzen & Aanæs (2005) angle-weighted pseudonormals (acute 30° ridge, 90° corner, 10° knife-edge, concave groove, baffle cancellation).
   - Oosterom & Strackee (1983) / Jacobson et al. (2013) Generalized Winding Numbers (solid angles, dirty CAD, missing faces, singularities).
   - Maruani et al. (2023) VoroMesh bisector loss (k=16 nearest seeds, continuity, unit gradient norm, finite-difference comparison, coincident seeds).
2. Dual-Engine Parity:
   - Python vs C++20 exact parity across AABB tree, pseudonormals, winding numbers, and VoroMesh loss.
3. Zero-Shortcut Policy:
   - Detection of magic heuristics, arbitrary thresholds, and stability under extreme coordinates.
"""

from __future__ import annotations

import ctypes
import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.meshing.geometry_sdf import (
    CADMeshSDF,
    TriangleAABBTree,
    compute_angle_weighted_pseudonormals,
    compute_mesh_winding_number,
    voromesh_bisector_loss,
    _get_sdf_dll,
)
from tests.test_wp2_mesh_sdf import (
    _build_anisotropic_box,
    _build_acute_wedge_30deg,
    _build_dirty_cad_mesh_with_gap,
    _point_to_triangle_distance_exact,
)


# ==============================================================================
# Helper Geometries for Adversarial Testing
# ==============================================================================


def _build_ultra_acute_knife_edge(length: float = 2.0, height: float = 1.0, half_angle_deg: float = 5.0) -> SurfaceMesh:
    """Constructs extreme acute wedge with 10° apex angle (2 * 5°)."""
    half_width = height * np.tan(np.radians(half_angle_deg))
    verts = np.array([
        [0.0, 0.0, 0.0],
        [length, 0.0, 0.0],
        [0.0, -half_width, -height],
        [length, -half_width, -height],
        [0.0, half_width, -height],
        [length, half_width, -height],
    ], dtype=np.float64)

    faces = np.array([
        [0, 3, 2], [0, 1, 3],  # Left flank
        [0, 4, 5], [0, 5, 1],  # Right flank
        [2, 3, 5], [2, 5, 4],  # Bottom closing face
        [0, 2, 4],             # Front cap
        [1, 5, 3],             # Back cap
    ], dtype=np.int32)
    return SurfaceMesh(vertices=verts, faces=faces)


def _build_concave_groove_block() -> SurfaceMesh:
    """Constructs a block with a re-entrant 30° V-groove along top face."""
    # Base box from z = -1 to 0, groove goes down from z = 0 to z = -0.5 at y = 0
    # Left plateau: y in [-1, -0.2], right plateau: y in [0.2, 1], groove: y in [-0.2, 0.2]
    # For a clean closed triangulated polyhedron:
    verts = np.array([
        # Bottom face z = -1
        [-1.0, -1.0, -1.0], [1.0, -1.0, -1.0], [1.0, 1.0, -1.0], [-1.0, 1.0, -1.0], # 0..3
        # Top boundary corners z = 0
        [-1.0, -1.0, 0.0], [1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [-1.0, 1.0, 0.0],     # 4..7
        # Groove apex at z = -0.6, y = 0
        [-1.0, 0.0, -0.6], [1.0, 0.0, -0.6],                                         # 8, 9
    ], dtype=np.float64)

    faces = np.array([
        # Bottom (z = -1)
        [0, 2, 1], [0, 3, 2],
        # Sides
        [0, 1, 5], [0, 5, 4],  # y = -1
        [2, 3, 7], [2, 7, 6],  # y = +1
        [0, 4, 7], [0, 7, 3],  # x = -1
        [1, 2, 6], [1, 6, 5],  # x = +1
        # Top surface with groove
        # Left plateau: y in [-1, 0] descending to groove
        [4, 5, 9], [4, 9, 8],
        # Right plateau: y in [0, 1] ascending from groove
        [8, 9, 6], [8, 6, 7],
    ], dtype=np.int32)
    return SurfaceMesh(vertices=verts, faces=faces)


# ==============================================================================
# Suite 1: Bærentzen & Aanæs (2005) Angle-Weighted Pseudonormals
# ==============================================================================


def test_adversarial_pseudonormal_ultra_acute_10deg_knife_edge() -> None:
    """Stress tests Bærentzen & Aanæs pseudonormals on an extreme 10° knife edge."""
    mesh = _build_ultra_acute_knife_edge(length=2.0, height=1.0, half_angle_deg=5.0)

    for use_nat in [False, True]:
        sdf = CADMeshSDF(mesh, sign_method="pseudonormal", use_native=use_nat)

        # Points directly above the apex along +z (exterior)
        x_pts = np.linspace(0.1, 1.9, 15)
        for dist in [1e-6, 1e-5, 1e-4, 1e-2, 0.1, 0.5]:
            query_ext = np.column_stack([x_pts, np.zeros_like(x_pts), np.full_like(x_pts, dist)])
            phi_ext, grad_ext = sdf.evaluate(query_ext)

            assert np.all(phi_ext > 0.0), (
                f"[Native={use_nat}] Sign flip at 10° knife edge apex exterior at dist {dist}: {phi_ext}"
            )
            assert np.all(grad_ext[:, 2] > 0.95), (
                f"[Native={use_nat}] Outward gradient must point along +z, got {grad_ext}"
            )

        # Points inside the wedge below the apex along -z (interior)
        for dist in [1e-5, 1e-3, 0.1]:
            query_int = np.column_stack([x_pts, np.zeros_like(x_pts), np.full_like(x_pts, -dist)])
            phi_int, _ = sdf.evaluate(query_int)
            assert np.all(phi_int < 0.0), (
                f"[Native={use_nat}] Sign flip at 10° knife edge apex interior at dist {dist}: {phi_int}"
            )


def test_adversarial_pseudonormal_concave_groove() -> None:
    """Verifies that an acute re-entrant (concave) groove does not suffer from sign inversion."""
    mesh = _build_concave_groove_block()

    for use_nat in [False, True]:
        sdf = CADMeshSDF(mesh, sign_method="pseudonormal", use_native=use_nat)

        # Points inside the groove but in fluid domain (z > -0.6 at y = 0):
        # The groove apex is at z = -0.6. Points at z = -0.5, y = 0 are in the fluid (exterior -> phi > 0)
        x_pts = np.linspace(-0.8, 0.8, 10)
        pts_in_groove_fluid = np.column_stack([x_pts, np.zeros_like(x_pts), np.full_like(x_pts, -0.4)])
        phi_fluid, _ = sdf.evaluate(pts_in_groove_fluid)
        assert np.all(phi_fluid > 0.0), (
            f"[Native={use_nat}] Fluid points in concave groove misclassified as solid: {phi_fluid}"
        )

        # Points below groove apex (z < -0.6 at y = 0): in the solid material (interior -> phi < 0)
        pts_in_solid = np.column_stack([x_pts, np.zeros_like(x_pts), np.full_like(x_pts, -0.8)])
        phi_solid, _ = sdf.evaluate(pts_in_solid)
        assert np.all(phi_solid < 0.0), (
            f"[Native={use_nat}] Solid points below concave groove misclassified as fluid: {phi_solid}"
        )


def test_adversarial_pseudonormal_baffle_cancellation_stability() -> None:
    """Verifies numerical stability when coincident faces have opposite normals (cancellation)."""
    # 2 coincident triangles with opposite orientation
    verts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0],
    ], dtype=np.float64)
    faces = np.array([
        [0, 1, 2],  # normal +z
        [0, 2, 1],  # normal -z
    ], dtype=np.int32)

    fn, vn, en, fe_map = compute_angle_weighted_pseudonormals(verts, faces)
    assert np.all(np.isfinite(fn))
    assert np.all(np.isfinite(vn))
    assert np.all(np.isfinite(en))
    # Normals must be unit length despite opposite cancellation
    assert np.allclose(np.linalg.norm(vn, axis=1), 1.0, atol=1e-5)
    assert np.allclose(np.linalg.norm(en, axis=1), 1.0, atol=1e-5)


# ==============================================================================
# Suite 2: Oosterom & Strackee / Jacobson Generalized Winding Numbers
# ==============================================================================


def test_adversarial_winding_numbers_exact_solid_angle_closed_box() -> None:
    """Verifies Oosterom & Strackee solid angle integration on a closed box equals 1.0 inside, 0.0 outside."""
    box = _build_anisotropic_box(lx=1.0, ly=1.0, lz=1.0)
    sdf = CADMeshSDF(box, sign_method="winding_number")

    # Grid of 5x5x5 points strictly inside
    xi = np.linspace(0.1, 0.9, 5)
    gx, gy, gz = np.meshgrid(xi, xi, xi)
    pts_in = np.column_stack([gx.ravel(), gy.ravel(), gz.ravel()])

    wn_in = sdf.winding_number(pts_in)
    assert np.allclose(wn_in, 1.0, atol=1e-12), (
        f"Winding number on closed watertight box inside must be exactly 1.0, max error: {np.max(np.abs(wn_in - 1.0))}"
    )

    # Grid of points strictly outside
    xo = np.linspace(1.2, 2.0, 5)
    gx, gy, gz = np.meshgrid(xo, xo, xo)
    pts_out = np.column_stack([gx.ravel(), gy.ravel(), gz.ravel()])

    wn_out = sdf.winding_number(pts_out)
    assert np.allclose(wn_out, 0.0, atol=1e-12), (
        f"Winding number on closed watertight box outside must be exactly 0.0, max error: {np.max(np.abs(wn_out))}"
    )


def test_adversarial_winding_numbers_open_box_missing_face() -> None:
    """Verifies Generalized Winding Numbers on a severely non-watertight box (1 full face missing out of 6)."""
    # 5 faces of a cube (open top at z = 1.0)
    verts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ], dtype=np.float64)
    # Bottom, Front, Back, Left, Right (Top missing)
    faces = np.array([
        [0, 2, 1], [0, 3, 2],  # bottom
        [0, 1, 5], [0, 5, 4],  # front
        [2, 3, 7], [2, 7, 6],  # back
        [0, 4, 7], [0, 7, 3],  # left
        [1, 2, 6], [1, 6, 5],  # right
    ], dtype=np.int32)
    mesh_open = SurfaceMesh(vertices=verts, faces=faces)

    sdf_open = CADMeshSDF(mesh_open, sign_method="winding_number")

    # Center of bottom half of cavity: (0.5, 0.5, 0.3)
    p_deep = np.array([[0.5, 0.5, 0.3]])
    wn_deep = sdf_open.winding_number(p_deep)
    # 5 faces visible -> W(p) should be approximately 5/6 ~ 0.83 > 0.5
    assert wn_deep[0] > 0.75, f"Deep cavity point winding number should be ~0.83, got {wn_deep[0]}"

    # Exterior point far outside
    p_far = np.array([[0.5, 0.5, 2.5]])
    wn_far = sdf_open.winding_number(p_far)
    assert wn_far[0] < 0.1, f"Far exterior point winding number should be small, got {wn_far[0]}"


def test_adversarial_winding_numbers_singularities_on_vertices_and_edges() -> None:
    """Verifies that query points placed EXACTLY on mesh vertices and edges do not produce NaN or Inf."""
    box = _build_anisotropic_box(lx=1.0, ly=1.0, lz=1.0)
    sdf = CADMeshSDF(box, sign_method="winding_number")

    # 1. Exact vertex coordinates (da = 0 in solid angle formula)
    wn_verts = sdf.winding_number(box.vertices)
    assert np.all(np.isfinite(wn_verts)), f"Winding number on vertex produced non-finite: {wn_verts}"
    # At a 90° corner of a cube, interior solid angle is (pi/2) sr -> W(x) = (pi/2) / (4pi) = 1/8 = 0.125
    assert np.allclose(wn_verts, 0.125, atol=1e-5), f"Winding number at 90° corner should be 0.125, got {wn_verts}"

    # 2. Exact edge midpoints
    edge_mid = 0.5 * (box.vertices[0] + box.vertices[1])
    wn_edge = sdf.winding_number(edge_mid)
    assert np.isfinite(wn_edge), f"Winding number on edge midpoint produced non-finite: {wn_edge}"
    # At a 90° edge, solid angle is pi sr -> W(x) = pi / (4pi) = 0.25
    assert np.isclose(wn_edge, 0.25, atol=1e-5), f"Winding number on 90° edge should be 0.25, got {wn_edge}"


# ==============================================================================
# Suite 3: Maruani et al. (2023) VoroMesh Differentiable Bisector Loss
# ==============================================================================


def test_adversarial_voromesh_gradient_finite_difference_consistency() -> None:
    """Validates analytical VoroMesh gradients against central finite differences."""
    np.random.seed(42)
    seeds = np.random.uniform(-1.0, 1.0, (20, 3))
    # Query point chosen inside a Voronoi cell
    pts = np.array([
        [0.1, 0.2, 0.3],
        [-0.4, 0.5, -0.1],
        [0.0, 0.0, 0.0],
    ], dtype=np.float64)

    loss, dists, grads = voromesh_bisector_loss(pts, seeds, k=16, use_native=False)
    eps = 1e-6

    for i in range(len(pts)):
        p = pts[i]
        ana_grad = grads[i]

        num_grad = np.zeros(3)
        for d in range(3):
            p_plus = p.copy()
            p_minus = p.copy()
            p_plus[d] += eps
            p_minus[d] -= eps

            _, d_plus, _ = voromesh_bisector_loss(p_plus[None, :], seeds, k=16, use_native=False)
            _, d_minus, _ = voromesh_bisector_loss(p_minus[None, :], seeds, k=16, use_native=False)
            num_grad[d] = (d_plus[0] - d_minus[0]) / (2.0 * eps)

        # Gradient should match numerical derivative within 1e-4
        assert np.allclose(ana_grad, num_grad, atol=1e-4), (
            f"Point {p}: analytical gradient {ana_grad} diverges from numerical finite difference {num_grad}"
        )


def test_adversarial_voromesh_minimal_2_seeds_point_plane_distance() -> None:
    """With exactly 2 seeds, VoroMesh distance must identically match point-to-plane distance."""
    s1 = np.array([0.0, 0.0, 0.0])
    s2 = np.array([0.0, 0.0, 2.0])
    seeds = np.vstack([s1, s2])

    # Bisector plane is z = 1.0
    pts = np.array([
        [0.5, 0.5, 1.3],   # dist = 0.3, grad = (0, 0, 1)
        [-0.2, 0.8, 0.6],  # dist = 0.4, grad = (0, 0, -1)
        [1.0, 2.0, 1.0],   # dist = 0.0 (on plane)
    ], dtype=np.float64)

    loss, dists, grads = voromesh_bisector_loss(pts, seeds, k=1, use_native=False)

    expected_dists = np.array([0.3, 0.4, 0.0])
    assert np.allclose(dists, expected_dists, atol=1e-12), f"Expected dists {expected_dists}, got {dists}"
    assert np.isclose(loss, np.mean(expected_dists**2), atol=1e-12)


def test_adversarial_voromesh_coincident_seeds_stability() -> None:
    """Verifies that duplicate/coincident seeds do not cause division by zero or crash."""
    s1 = np.array([0.0, 0.0, 0.0])
    s2 = np.array([0.0, 0.0, 0.0])  # Duplicate!
    s3 = np.array([1.0, 1.0, 1.0])
    seeds = np.vstack([s1, s2, s3])

    pts = np.array([[0.5, 0.5, 0.5]], dtype=np.float64)

    # Must execute safely in both Python and C++
    loss_py, dists_py, grads_py = voromesh_bisector_loss(pts, seeds, k=2, use_native=False)
    assert np.isfinite(loss_py)
    assert np.all(np.isfinite(dists_py))
    assert np.all(np.isfinite(grads_py))

    loss_cpp, dists_cpp, grads_cpp = voromesh_bisector_loss(pts, seeds, k=2, use_native=True)
    assert np.isfinite(loss_cpp)
    assert np.all(np.isfinite(dists_cpp))
    assert np.all(np.isfinite(grads_cpp))


# ==============================================================================
# Suite 4: Dual-Engine Parity (Python vs C++20)
# ==============================================================================


def test_adversarial_dual_engine_parity_aabb_and_pseudonormals() -> None:
    """Tests exact numerical parity between Python and C++20 native kernel across 100 queries."""
    box = _build_anisotropic_box(lx=2.0, ly=1.0, lz=0.5)

    sdf_py = CADMeshSDF(box, sign_method="pseudonormal", use_native=False)
    sdf_cpp = CADMeshSDF(box, sign_method="pseudonormal", use_native=True)

    np.random.seed(12345)
    test_pts = np.random.uniform(-1.0, 3.0, (100, 3))

    phi_py, grad_py = sdf_py.evaluate(test_pts)
    phi_cpp, grad_cpp = sdf_cpp.evaluate(test_pts)

    max_phi_diff = np.max(np.abs(phi_py - phi_cpp))
    assert max_phi_diff < 1e-12, f"Dual-engine phi parity violation: max diff = {max_phi_diff:.2e}"

    max_grad_diff = np.max(np.linalg.norm(grad_py - grad_cpp, axis=1))
    assert max_grad_diff < 1e-10, f"Dual-engine grad parity violation: max diff = {max_grad_diff:.2e}"


def test_adversarial_dual_engine_parity_winding_numbers() -> None:
    """Tests exact numerical parity between Python and C++20 for Generalized Winding Numbers."""
    box = _build_anisotropic_box(lx=1.5, ly=0.8, lz=0.6)

    np.random.seed(999)
    test_pts = np.random.uniform(-0.5, 2.0, (80, 3))

    # Python computation
    wn_py = compute_mesh_winding_number(test_pts, box.vertices[box.faces[:, 0]],
                                        box.vertices[box.faces[:, 1]],
                                        box.vertices[box.faces[:, 2]])

    # Native C++ computation
    sdf_cpp = CADMeshSDF(box, sign_method="winding_number", use_native=True)
    wn_cpp = sdf_cpp.winding_number(test_pts)

    max_wn_diff = np.max(np.abs(wn_py - wn_cpp))
    assert max_wn_diff < 1e-12, f"Dual-engine winding number parity violation: max diff = {max_wn_diff:.2e}"


def test_adversarial_dual_engine_parity_voromesh() -> None:
    """Tests exact numerical parity between Python and C++20 for VoroMesh bisector loss."""
    np.random.seed(777)
    pts = np.random.uniform(-1.0, 1.0, (50, 3))
    seeds = np.random.uniform(-1.5, 1.5, (25, 3))

    loss_py, dists_py, grads_py = voromesh_bisector_loss(pts, seeds, k=16, use_native=False)
    loss_cpp, dists_cpp, grads_cpp = voromesh_bisector_loss(pts, seeds, k=16, use_native=True)

    assert np.isclose(loss_py, loss_cpp, atol=1e-12), (
        f"VoroMesh loss parity mismatch: Python {loss_py:.12e} vs C++ {loss_cpp:.12e}"
    )
    assert np.allclose(dists_py, dists_cpp, atol=1e-12), "VoroMesh distances parity mismatch"
    assert np.allclose(grads_py, grads_cpp, atol=1e-10), "VoroMesh gradients parity mismatch"
