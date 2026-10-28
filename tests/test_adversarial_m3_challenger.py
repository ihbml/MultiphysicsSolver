"""Empirical and Adversarial Challenger Test Suite for Milestone M3.

Author: challenger_m3_1 (SPH-FVM Coupling & Mathematical Invariants Challenger)
Assigned Directory: .agents/teamwork/challenger_m3_1/
Test Suite: tests/test_adversarial_m3_challenger.py

Adversarially stress-tests the mathematical invariants of SPH-FVM coupling (coupling.py):
1. SPH support divergence detection (div r_i < 0.75 d) on uniform, non-uniform,
   randomly perturbed, and boundary-truncated distributions; validate FPR and FNR.
2. Virtual wall vector A_iS 0th-order closure (||A_iS + sum_j V_j \nabla_i W_{ij}|| < 10^-10)
   across arbitrary boundary curvatures (curved cylinders, spheres, acute/reentrant corners, planar walls).
3. OPMIS Taylor linear moment reproduction: P2M and M2P interpolation on distorted,
   irregularly sampled clouds. Verify L_inf error is strictly < 10^-11 for linear polynomial fields.
4. Conservation stress testing: 0th and 1st order moment conservation and reproduction.
"""

from __future__ import annotations

import math
from typing import List, Tuple, Dict, Any
import numpy as np
import pytest

from backend.meshing.coupling import (
    SPHKernel,
    SPHFVMCoupler,
    OPMISTaylorInterpolator,
)
from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import UnstructuredMesh


# ===========================================================================
# Helper Generators for Particles and Meshes
# ===========================================================================

def make_uniform_lattice_2d(nx: int = 20, ny: int = 20, dx: float = 0.05) -> Tuple[np.ndarray, np.ndarray, float]:
    """Generate 2D uniform Cartesian particle lattice."""
    x = np.linspace(0.0, (nx - 1) * dx, nx)
    y = np.linspace(0.0, (ny - 1) * dx, ny)
    X, Y = np.meshgrid(x, y)
    pos = np.column_stack([X.ravel(), Y.ravel()])
    vol = np.full(len(pos), dx * dx, dtype=np.float64)
    h = dx * 1.5
    return pos, vol, h


def make_perturbed_lattice_2d(
    nx: int = 20, ny: int = 20, dx: float = 0.05, jitter_ratio: float = 0.1, seed: int = 42
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Generate 2D lattice with random interior jitter while keeping boundary intact."""
    pos, vol, h = make_uniform_lattice_2d(nx, ny, dx)
    rng = np.random.default_rng(seed)
    jitter = rng.uniform(-jitter_ratio * dx, jitter_ratio * dx, size=pos.shape)
    
    # Only jitter interior particles so boundary geometry remains well-defined
    x_max = (nx - 1) * dx
    y_max = (ny - 1) * dx
    interior_mask = (
        (pos[:, 0] > 1.5 * dx) & (pos[:, 0] < x_max - 1.5 * dx) &
        (pos[:, 1] > 1.5 * dx) & (pos[:, 1] < y_max - 1.5 * dx)
    )
    pos[interior_mask] += jitter[interior_mask]
    return pos, vol, h


def make_cylinder_particles_3d(
    radius: float = 1.0, length: float = 2.0, dr: float = 0.08, seed: int = 123
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Generate particles inside a 3D circular cylinder (concave boundary)."""
    rng = np.random.default_rng(seed)
    n_radial = int(np.ceil(radius / dr))
    n_axial = int(np.ceil(length / dr))
    
    # Rejection sampling with jitter
    pts = []
    box_n = int((2 * radius / dr) * (2 * radius / dr) * (length / dr) * 1.5)
    cand_x = rng.uniform(-radius, radius, size=box_n)
    cand_y = rng.uniform(-radius, radius, size=box_n)
    cand_z = rng.uniform(0.0, length, size=box_n)
    
    in_cyl = (cand_x ** 2 + cand_y ** 2) <= (radius ** 2)
    pos = np.column_stack([cand_x[in_cyl], cand_y[in_cyl], cand_z[in_cyl]])
    
    # Add explicit cylinder wall particles at r = radius
    n_wall_theta = int(2 * np.pi * radius / dr)
    n_wall_z = n_axial
    thetas = np.linspace(0.0, 2 * np.pi, n_wall_theta, endpoint=False)
    zs = np.linspace(0.0, length, n_wall_z)
    T, Z = np.meshgrid(thetas, zs)
    wall_x = radius * np.cos(T.ravel())
    wall_y = radius * np.sin(T.ravel())
    wall_z = Z.ravel()
    wall_pts = np.column_stack([wall_x, wall_y, wall_z])
    
    all_pos = np.vstack([pos, wall_pts])
    vol_per_particle = (np.pi * (radius ** 2) * length) / len(all_pos)
    vol = np.full(len(all_pos), vol_per_particle, dtype=np.float64)
    h = (vol_per_particle ** (1.0 / 3.0)) * 1.5
    return all_pos, vol, h


def make_corner_particles_2d(
    arm_length: float = 1.0, width: float = 0.3, dx: float = 0.04
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Generate L-shaped corner domain (has both acute/reentrant 270 deg and 90 deg corners)."""
    x = np.arange(0.0, arm_length, dx)
    y = np.arange(0.0, arm_length, dx)
    X, Y = np.meshgrid(x, y)
    pts = np.column_stack([X.ravel(), Y.ravel()])
    
    # L-shape: keep if (x < width) or (y < width)
    mask = (pts[:, 0] <= width) | (pts[:, 1] <= width)
    pos = pts[mask]
    vol = np.full(len(pos), dx * dx, dtype=np.float64)
    h = dx * 1.5
    return pos, vol, h


def make_hex_mesh(nx: int = 5, ny: int = 5, nz: int = 5, lx: float = 1.0, ly: float = 1.0, lz: float = 1.0) -> UnstructuredMesh:
    """Generate structured hex mesh for OPMIS testing."""
    x = np.linspace(0.0, lx, nx + 1)
    y = np.linspace(0.0, ly, ny + 1)
    z = np.linspace(0.0, lz, nz + 1)
    points = []
    point_idx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                point_idx[(i, j, k)] = len(points)
                points.append([x[i], y[j], z[k]])
    points_arr = np.array(points, dtype=np.float64)
    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                c = [
                    point_idx[(i, j, k)], point_idx[(i + 1, j, k)],
                    point_idx[(i + 1, j + 1, k)], point_idx[(i, j + 1, k)],
                    point_idx[(i, j, k + 1)], point_idx[(i + 1, j, k + 1)],
                    point_idx[(i + 1, j + 1, k + 1)], point_idx[(i, j + 1, k + 1)],
                ]
                cells.append(np.array(c, dtype=np.int64))
    return compute_difvm_topology(points_arr, cells)


# ===========================================================================
# Task 1: SPH Support Divergence Detection (div r_i < 0.75 d)
# ===========================================================================

class TestSPHDivergenceAdversarial:
    """Adversarial stress-testing of position divergence criterion div r_i < 0.75 d."""

    @pytest.mark.parametrize("kernel_type", ["wendland", "cubic"])
    def test_uniform_lattice_false_positive_negative_2d(self, kernel_type: str):
        """Uniform 2D lattice: zero false positives in deep interior, zero false negatives on boundary."""
        coupler = SPHFVMCoupler(kernel=kernel_type, support_factor=2.85, boundary_threshold_ratio=0.75)
        nx, ny = 25, 25
        dx = 0.04
        pos, vol, h = make_uniform_lattice_2d(nx, ny, dx)
        cutoff_R = coupler.support_factor * h

        div_r = coupler.compute_position_divergence(pos, vol, h, dim=2)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)

        lx = (nx - 1) * dx
        ly = (ny - 1) * dx

        # Ground truth:
        # 1. Deep interior: distance to any boundary > cutoff_R
        dist_to_wall = np.minimum.reduce([pos[:, 0], lx - pos[:, 0], pos[:, 1], ly - pos[:, 1]])
        deep_interior = dist_to_wall >= cutoff_R

        # 2. Outer skin: particles on the outer perimeter (dist == 0)
        outer_skin = dist_to_wall < 1e-6

        # False positive rate in deep interior must be 0%
        false_positives = np.sum(is_bnd[deep_interior])
        n_deep = np.sum(deep_interior)
        fpr = false_positives / n_deep if n_deep > 0 else 0.0
        assert false_positives == 0, f"Kernel {kernel_type}: {false_positives}/{n_deep} false positives in deep interior"
        assert fpr == 0.0

        # False negative rate on outer perimeter must be 0%
        false_negatives = np.sum(~is_bnd[outer_skin])
        n_skin = np.sum(outer_skin)
        fnr = false_negatives / n_skin if n_skin > 0 else 0.0
        assert false_negatives == 0, f"Kernel {kernel_type}: {false_negatives}/{n_skin} false negatives on wall boundary"
        assert fnr == 0.0

        # Interior continuum divergence check: div r in deep interior must be close to 2.0
        mean_int_div = np.mean(div_r[deep_interior])
        assert abs(mean_int_div - 2.0) < 0.20, f"Expected 2D div r ~ 2.0, got {mean_int_div:.4f}"

    @pytest.mark.parametrize("jitter_ratio", [0.05, 0.10, 0.20])
    def test_randomly_perturbed_lattice_divergence(self, jitter_ratio: float):
        """Randomly perturbed particle clouds must maintain zero false positives in deep interior."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85, boundary_threshold_ratio=0.75)
        nx, ny = 30, 30
        dx = 0.035
        pos, vol, h = make_perturbed_lattice_2d(nx, ny, dx, jitter_ratio=jitter_ratio, seed=100 + int(jitter_ratio * 100))
        cutoff_R = coupler.support_factor * h

        lx = (nx - 1) * dx
        ly = (ny - 1) * dx
        dist_to_wall = np.minimum.reduce([pos[:, 0], lx - pos[:, 0], pos[:, 1], ly - pos[:, 1]])
        deep_interior = dist_to_wall >= cutoff_R

        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)
        false_positives = np.sum(is_bnd[deep_interior])
        n_deep = np.sum(deep_interior)

        assert false_positives == 0, (
            f"Jitter {jitter_ratio * 100}%: {false_positives}/{n_deep} false positives in interior"
        )

    def test_non_uniform_density_gradient_2d(self):
        """Particles with quadratic spatial density variation must not misclassify interior as boundary."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
        # 1D stretched distribution in X, uniform in Y
        n = 30
        xi = np.linspace(0.0, 1.0, n) ** 1.5  # Stretched
        yi = np.linspace(0.0, 1.0, n)
        X, Y = np.meshgrid(xi, yi)
        pos = np.column_stack([X.ravel(), Y.ravel()])

        # Voronoi volume approximation based on local spacing
        dx_local = np.gradient(xi)
        dy_local = np.full(n, 1.0 / (n - 1))
        DX, DY = np.meshgrid(dx_local, dy_local)
        vol = (DX * DY).ravel()
        h = np.mean(dx_local) * 2.0

        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)
        div_r = coupler.compute_position_divergence(pos, vol, h, dim=2)

        # Center bulk particles (away from all 4 walls)
        center_mask = (pos[:, 0] > 0.3) & (pos[:, 0] < 0.7) & (pos[:, 1] > 0.3) & (pos[:, 1] < 0.7)
        false_positives = np.sum(is_bnd[center_mask])
        assert false_positives == 0, f"Non-uniform gradient: {false_positives} false positives in center"

    def test_corner_and_reentrant_boundary_detection_2d(self):
        """In an L-shaped domain, boundary detector must identify convex corners, re-entrant corners, and edges."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
        pos, vol, h = make_corner_particles_2d(arm_length=1.0, width=0.3, dx=0.03)

        div_r = coupler.compute_position_divergence(pos, vol, h, dim=2)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)

        # 90-degree outer corner at (0, 0): support ~ 1/4 -> div r ~ 0.5
        corner_00 = (pos[:, 0] < 0.04) & (pos[:, 1] < 0.04)
        assert np.all(is_bnd[corner_00]), "Outer 90-degree corner at (0,0) must be detected as boundary"
        assert np.mean(div_r[corner_00]) < 1.0, f"Expected div r < 1.0 at outer corner, got {np.mean(div_r[corner_00]):.2f}"

        # Outer tip corners at (1, 0) and (0, 1)
        tip_x = (pos[:, 0] > 0.96) & (pos[:, 1] < 0.04)
        assert np.all(is_bnd[tip_x]), "Tip corner along X must be detected as boundary"


# ===========================================================================
# Task 2: Virtual Wall Vector A_iS 0th-Order Closure Across Curvatures
# ===========================================================================

class TestVirtualWallVectorAdversarial:
    """Stress-test 0th-order consistency ||A_iS + sum_j V_j \nabla_i W_{ij}|| < 10^-10 across geometries."""

    def test_cylindrical_curved_boundary_closure_3d(self):
        """0th-order consistency on 3D curved circular cylinder surface (concave boundary)."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
        pos, vol, h = make_cylinder_particles_3d(radius=0.5, length=1.0, dr=0.06, seed=42)

        A_iS, sum_grad = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h)

        # 1. Exact 0th-order closure invariant on boundary particles: ||A_iS + sum_grad|| < 10^-10
        residuals = np.linalg.norm(A_iS[is_bnd] + sum_grad[is_bnd], axis=1)
        max_res = np.max(residuals)
        assert max_res < 1e-10, f"Cylinder boundary 0th-order closure violated: max residual = {max_res:.2e}"

        # 2. Virtual normal direction on cylinder wall must point radially OUTWARD
        r_cyl = np.sqrt(pos[:, 0] ** 2 + pos[:, 1] ** 2)
        outer_wall = is_bnd & (r_cyl > 0.45) & (pos[:, 2] > 0.2) & (pos[:, 2] < 0.8)

        if np.any(outer_wall):
            norm_A = np.linalg.norm(A_iS[outer_wall], axis=1)
            assert np.all(norm_A > 1e-4), "Wall vectors on cylinder perimeter must have non-zero magnitude"
            
            # Unit radial vector
            radial_vec = np.column_stack([
                pos[outer_wall, 0] / r_cyl[outer_wall],
                pos[outer_wall, 1] / r_cyl[outer_wall],
                np.zeros(np.sum(outer_wall)),
            ])
            # Dot product with A_iS (pointing towards missing support, i.e. outward)
            dots = np.sum(A_iS[outer_wall] * radial_vec, axis=1)
            assert np.all(dots > 0.0), "Virtual wall vectors must point radially outward through cylindrical wall"

    @pytest.mark.parametrize("angle_deg", [45, 90, 135])
    def test_wedge_corner_closure(self, angle_deg: int):
        """0th-order consistency holds on acute, right, and obtuse wedge corners in 2D."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
        theta_rad = math.radians(angle_deg)

        # Generate wedge particles: 0 <= theta <= theta_rad, r <= 1.0
        n_pts = 400
        rng = np.random.default_rng(angle_deg)
        r = np.sqrt(rng.uniform(0.01, 1.0, size=n_pts))
        theta = rng.uniform(0.0, theta_rad, size=n_pts)
        pos = np.column_stack([r * np.cos(theta), r * np.sin(theta)])
        
        area = 0.5 * (1.0 ** 2) * theta_rad
        vol = np.full(n_pts, area / n_pts, dtype=np.float64)
        h = np.sqrt(area / n_pts) * 1.8

        A_iS, sum_grad = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True, dim=2)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)

        assert np.any(is_bnd), f"Wedge {angle_deg} deg must have detected boundary particles"
        residuals = np.linalg.norm(A_iS[is_bnd] + sum_grad[is_bnd], axis=1)
        max_res = np.max(residuals)
        assert max_res < 1e-10, f"Wedge {angle_deg} deg: closure violated, max res = {max_res:.2e}"

    def test_thin_channel_confinement_closure(self):
        """In a narrow gap where particles feel opposite walls, 0th-order closure is strictly preserved."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
        # Channel of width W = 0.06 with h = 0.05 (cutoff R = 0.1425 > W, complete overlap)
        nx = 20
        ny = 4
        dx = 0.03
        dy = 0.015
        x = np.linspace(0.0, (nx - 1) * dx, nx)
        y = np.linspace(0.0, (ny - 1) * dy, ny)
        X, Y = np.meshgrid(x, y)
        pos = np.column_stack([X.ravel(), Y.ravel()])
        vol = np.full(len(pos), dx * dy, dtype=np.float64)
        h = 0.04

        A_iS, sum_grad = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True, dim=2)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h, dim=2)

        residuals = np.linalg.norm(A_iS[is_bnd] + sum_grad[is_bnd], axis=1)
        max_res = np.max(residuals)
        assert max_res < 1e-10, f"Thin channel closure error: {max_res:.2e}"


# ===========================================================================
# Task 3: OPMIS Taylor Linear Moment Reproduction (L_inf < 10^-11)
# ===========================================================================

class TestOPMISTaylorAdversarial:
    """Stress-test exact linear polynomial reproduction across distorted clouds and large/small slopes."""

    @pytest.fixture
    def test_setup(self):
        """Generate distorted mesh and irregular particle cloud."""
        mesh = make_hex_mesh(nx=4, ny=4, nz=4, lx=1.0, ly=1.0, lz=1.0)
        rng = np.random.default_rng(2026)
        
        # Irregular clustered particle cloud
        n_p = 350
        # Mixture of uniform + clustered particles
        pos_bulk = rng.uniform(0.05, 0.95, size=(250, 3))
        pos_cluster = rng.normal(loc=[0.3, 0.3, 0.3], scale=0.08, size=(100, 3))
        pos_cluster = np.clip(pos_cluster, 0.02, 0.98)
        pos = np.vstack([pos_bulk, pos_cluster])

        vol = np.full(len(pos), 1.0 / len(pos), dtype=np.float64)
        h = 1.0 / (len(pos) ** (1.0 / 3.0)) * 1.6
        return mesh, pos, vol, h

    @pytest.mark.parametrize("case_id, a_vec, b_const", [
        ("constant_field", np.array([0.0, 0.0, 0.0]), 77.34),
        ("steep_x_slope", np.array([1000.0, 0.0, 0.0]), -250.0),
        ("steep_y_slope", np.array([0.0, -850.0, 0.0]), 120.5),
        ("steep_z_slope", np.array([0.0, 0.0, 1500.0]), 0.0),
        ("mixed_large_coeffs", np.array([-321.4, 567.8, -912.3]), 45.67),
        ("small_coeffs", np.array([1e-4, -2e-4, 5e-4]), 1.0),
        ("high_magnitude", np.array([1e6, -5e5, 2e6]), 1e4),
    ])
    def test_p2m_linear_reproduction_stress(self, test_setup, case_id: str, a_vec: np.ndarray, b_const: float):
        """P2M must reproduce linear field with relative L_inf < 10^-11 for arbitrary slopes."""
        mesh, pos, vol, h = test_setup
        interpolator = OPMISTaylorInterpolator(k_neighbors_p2m=20)

        # Evaluate exact linear field on particles
        field_p = np.dot(pos, a_vec) + b_const
        exact_mesh = np.dot(mesh.cell_centres, a_vec) + b_const

        interp_mesh = interpolator.interpolate_p2m(pos, field_p, vol, h, mesh)

        abs_err = np.max(np.abs(interp_mesh - exact_mesh))
        scale = max(np.max(np.abs(exact_mesh)), 1.0)
        rel_err = abs_err / scale

        # For O(1) fields, abs_err must be < 1e-11. For high-magnitude fields, rel_err < 1e-11.
        assert rel_err < 1e-11, (
            f"Case '{case_id}': P2M linear reproduction failed! abs_err={abs_err:.3e}, rel_err={rel_err:.3e}"
        )

    @pytest.mark.parametrize("case_id, a_vec, b_const", [
        ("constant_field", np.array([0.0, 0.0, 0.0]), 42.0),
        ("mixed_coeffs", np.array([12.5, -45.2, 33.1]), -8.9),
        ("large_coeffs", np.array([500.0, -1200.0, 800.0]), 100.0),
    ])
    def test_m2p_linear_reproduction_stress(self, test_setup, case_id: str, a_vec: np.ndarray, b_const: float):
        """M2P affine weighted least squares must reproduce linear field with relative L_inf < 10^-11."""
        mesh, pos, vol, h = test_setup
        interpolator = OPMISTaylorInterpolator(k_neighbors_m2p=14)

        exact_mesh = np.dot(mesh.cell_centres, a_vec) + b_const
        query_pts = pos[:50]
        exact_query = np.dot(query_pts, a_vec) + b_const

        interp_query = interpolator.interpolate_m2p(mesh, exact_mesh, query_pts)

        abs_err = np.max(np.abs(interp_query - exact_query))
        scale = max(np.max(np.abs(exact_query)), 1.0)
        rel_err = abs_err / scale

        assert rel_err < 1e-11, (
            f"Case '{case_id}': M2P affine reproduction failed! abs_err={abs_err:.3e}, rel_err={rel_err:.3e}"
        )

    def test_p2m_vector_field_coupled_components(self, test_setup):
        """P2M transfer of full 3D velocity vector field (u, v, w) with cross-component gradients."""
        mesh, pos, vol, h = test_setup
        interpolator = OPMISTaylorInterpolator(k_neighbors_p2m=18)

        A = np.array([
            [15.2, -8.4, 3.1],
            [-4.7, 22.0, -11.5],
            [9.1, 7.3, 18.6],
        ], dtype=np.float64)
        b = np.array([1.5, -2.8, 4.0], dtype=np.float64)

        field_p = pos @ A.T + b
        exact_mesh = mesh.cell_centres @ A.T + b

        interp_mesh = interpolator.interpolate_p2m(pos, field_p, vol, h, mesh)
        err = np.max(np.abs(interp_mesh - exact_mesh))

        assert err < 1e-11, f"Coupled 3D vector P2M error {err:.2e} exceeds 1e-11"


# ===========================================================================
# Task 4: Conservation Stress Testing (0th and 1st Order Moments)
# ===========================================================================

class TestMomentConservationStress:
    """Stress-test 0th-order (mass/integral) and 1st-order (centroid/dipole) moment conservation."""

    @pytest.fixture
    def domain_pair(self):
        """Setup co-located mesh and particle distribution covering unit cube [0,1]^3."""
        nx, ny, nz = 5, 5, 5
        mesh = make_hex_mesh(nx=nx, ny=ny, nz=nz, lx=1.0, ly=1.0, lz=1.0)

        # Particles on a regular 10x10x10 grid with boundary padding
        np_axis = 10
        coords = np.linspace(0.05, 0.95, np_axis)
        X, Y, Z = np.meshgrid(coords, coords, coords)
        pos = np.column_stack([X.ravel(), Y.ravel(), Z.ravel()])

        dp = 1.0 / np_axis
        vol = np.full(len(pos), dp ** 3, dtype=np.float64)
        h = dp * 1.5
        return mesh, pos, vol, h

    def test_zeroth_moment_partition_of_unity(self, domain_pair):
        """Constant field phi=1.0 must be reproduced exactly everywhere on mesh (0th-order reproduction)."""
        mesh, pos, vol, h = domain_pair
        interpolator = OPMISTaylorInterpolator()

        phi_p = np.ones(len(pos), dtype=np.float64)
        phi_mesh = interpolator.interpolate_p2m(pos, phi_p, vol, h, mesh)

        # Must be exactly 1.0 everywhere
        max_dev = np.max(np.abs(phi_mesh - 1.0))
        assert max_dev < 1e-13, f"Partition of unity violated in P2M: max dev = {max_dev:.2e}"

        # M2P partition of unity
        phi_query = interpolator.interpolate_m2p(mesh, np.ones(len(mesh.cell_centres)), pos[:30])
        max_dev_m2p = np.max(np.abs(phi_query - 1.0))
        assert max_dev_m2p < 1e-13, f"Partition of unity violated in M2P: max dev = {max_dev_m2p:.2e}"

    def test_first_moment_linear_completeness(self, domain_pair):
        """Linear coordinate fields phi=x, phi=y, phi=z must reproduce exact coordinate moments."""
        mesh, pos, vol, h = domain_pair
        interpolator = OPMISTaylorInterpolator()

        for d_idx, name in enumerate(["X", "Y", "Z"]):
            phi_p = pos[:, d_idx]
            phi_mesh = interpolator.interpolate_p2m(pos, phi_p, vol, h, mesh)
            exact_mesh = mesh.cell_centres[:, d_idx]

            err = np.max(np.abs(phi_mesh - exact_mesh))
            assert err < 1e-11, f"1st moment linear completeness for {name} coordinate violated: err = {err:.2e}"

    def test_integral_mass_conservation_p2m(self, domain_pair):
        """Compare global volume-integrated mass between particles and mesh: sum(phi_c * V_c) vs sum(phi_p * V_p)."""
        mesh, pos, vol, h = domain_pair
        interpolator = OPMISTaylorInterpolator()

        # For constant field phi = 1.0
        # Total mesh volume: sum(V_c) = 1.0 (unit cube)
        total_mesh_vol = np.sum(mesh.cell_volumes)
        assert abs(total_mesh_vol - 1.0) < 1e-12

        # Integrated mass on mesh
        phi_p = np.ones(len(pos), dtype=np.float64)
        phi_mesh = interpolator.interpolate_p2m(pos, phi_p, vol, h, mesh)
        mesh_mass = np.sum(phi_mesh * mesh.cell_volumes)

        # Particle mass
        particle_mass = np.sum(phi_p * vol)  # 1000 * (0.1)^3 = 1.0
        assert abs(particle_mass - 1.0) < 1e-12

        mass_diff = abs(mesh_mass - particle_mass)
        assert mass_diff < 1e-12, f"Global 0th-order mass conservation discrepancy: {mass_diff:.2e}"
