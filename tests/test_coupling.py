"""Comprehensive Unit Test Suite for Hybrid SPH-FVM Coupling & OPMIS (Features 12-14)."""

import numpy as np
import pytest

from backend.meshing.coupling import (
    OPMISTaylorInterpolator,
    SPHFVMCoupler,
    SPHKernel,
)
from backend.meshing.difvm_topology import compute_difvm_topology


# ---------------------------------------------------------------------------
# SPH Kernel Unit Tests (Wendland C2 & Cubic Spline)
# ---------------------------------------------------------------------------

class TestSPHKernel:
    """Test mathematical and geometric invariants of SPH kernels."""

    @pytest.mark.parametrize("dim", [2, 3])
    @pytest.mark.parametrize("kernel_type", ["wendland", "cubic"])
    def test_kernel_compact_support(self, dim: int, kernel_type: str):
        """Kernel and its derivative must vanish identically outside support radius."""
        h = 0.15
        kernel = SPHKernel(kernel_type=kernel_type, support_factor=2.0)
        R = 2.0 * h

        # Outside support
        r_outside = np.array([R + 0.001, R + 0.05, 2.0 * R, 10.0 * R])
        w_out = kernel.evaluate(r_outside, h, dim=dim)
        dw_out = kernel.derivative(r_outside, h, dim=dim)

        assert np.all(w_out == 0.0), f"Kernel {kernel_type} non-zero outside support"
        assert np.all(dw_out == 0.0), f"Derivative {kernel_type} non-zero outside support"

    @pytest.mark.parametrize("dim", [2, 3])
    def test_wendland_positivity_and_monotonicity(self, dim: int):
        """Wendland C2 kernel must be strictly positive and monotonically decreasing inside support."""
        h = 0.2
        kernel = SPHKernel(kernel_type="wendland", support_factor=2.85)
        R = 2.85 * h

        r = np.linspace(0.001, R - 0.001, 100)
        w = kernel.evaluate(r, h, dim=dim)
        dw = kernel.derivative(r, h, dim=dim)

        assert np.all(w > 0.0), "Wendland C2 must be strictly positive for r < R"
        assert np.all(dw <= 0.0), "Wendland C2 derivative must be non-positive (monotonically decreasing)"

    def test_spatial_gradient_radial_direction(self):
        """Kernel gradient grad_i W_ij must point in direction -(r_i - r_j)."""
        h = 0.2
        kernel = SPHKernel(kernel_type="wendland", support_factor=2.5)

        r_vec = np.array([[0.1, 0.0, 0.0], [0.0, -0.15, 0.0], [0.05, 0.05, 0.05]])
        grad_W = kernel.gradient(r_vec, h, dim=3)

        assert grad_W.shape == r_vec.shape
        # Direction of grad_W should be anti-parallel to r_vec (pointing towards r_j)
        for i in range(len(r_vec)):
            dot_prod = np.dot(r_vec[i], grad_W[i])
            assert dot_prod < 0.0, f"Gradient not anti-parallel to displacement at index {i}"


# ---------------------------------------------------------------------------
# SPH-FVM Coupler: Divergence & Virtual Wall Vector Tests
# ---------------------------------------------------------------------------

class TestSPHFVMCoupler:
    """Test boundary detection via position divergence and 0th-order wall consistency."""

    def test_interior_divergence_matches_dimension_2d(self):
        """In 2D continuum bulk, div r ~ 2.0 within discretization error."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)

        # Uniform 2D lattice
        nx = 20
        ny = 20
        dx = 0.05
        x = np.linspace(0.0, (nx - 1) * dx, nx)
        y = np.linspace(0.0, (ny - 1) * dx, ny)
        X, Y = np.meshgrid(x, y)
        pos = np.column_stack([X.ravel(), Y.ravel()])

        total_pts = len(pos)
        vol = np.full(total_pts, dx * dx, dtype=np.float64)
        h = dx * 1.5

        div_r = coupler.compute_position_divergence(pos, vol, h, dim=2)

        # Center interior particles
        center_mask = (
            (pos[:, 0] > 0.3 * (nx * dx)) & (pos[:, 0] < 0.7 * (nx * dx)) &
            (pos[:, 1] > 0.3 * (ny * dx)) & (pos[:, 1] < 0.7 * (ny * dx))
        )
        assert np.any(center_mask)
        mean_center_div = np.mean(div_r[center_mask])
        assert abs(mean_center_div - 2.0) < 0.15, f"Expected 2D div r ~ 2.0, got {mean_center_div:.3f}"

    def test_boundary_detection_synthetic_cloud_3d(self):
        """Wall particles near boundary must be flagged; deep interior must not be flagged."""
        coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)

        rng = np.random.default_rng(42)
        pos = rng.uniform(0.02, 0.98, size=(400, 3))
        wall_pts = np.zeros((60, 3))
        wall_pts[:, 0] = rng.uniform(0.0, 0.03, size=60)
        wall_pts[:, 1] = rng.uniform(0.0, 1.0, size=60)
        wall_pts[:, 2] = rng.uniform(0.0, 1.0, size=60)
        pos = np.vstack([pos, wall_pts])

        n_tot = len(pos)
        h = 1.0 / (n_tot ** (1.0 / 3.0)) * 1.5
        vol = np.full(n_tot, 1.0 / n_tot, dtype=np.float64)

        is_bnd = coupler.detect_boundary_particles(pos, vol, h)

        # Wall particles
        near_wall = pos[:, 0] < 0.03
        assert np.all(is_bnd[near_wall]), "All near-wall particles must be detected"

        # Bulk interior particles
        deep_interior = (
            (pos[:, 0] > 0.35) & (pos[:, 0] < 0.65) &
            (pos[:, 1] > 0.35) & (pos[:, 1] < 0.65) &
            (pos[:, 2] > 0.35) & (pos[:, 2] < 0.65)
        )
        assert not np.any(is_bnd[deep_interior]), "Deep interior particles must not be detected as boundary"

    def test_zeroth_order_consistency_A_iS(self):
        """Invariant: ||A_iS + sum_j V_j grad_i W_ij|| < 10^-10 for all boundary particles."""
        coupler = SPHFVMCoupler()

        rng = np.random.default_rng(123)
        pos = rng.uniform(0.02, 0.98, size=(250, 3))
        n_wall = 50
        wall = np.zeros((n_wall, 3))
        wall[:, 0] = rng.uniform(0.0, 0.02, size=n_wall)
        wall[:, 1] = rng.uniform(0.0, 1.0, size=n_wall)
        wall[:, 2] = rng.uniform(0.0, 1.0, size=n_wall)
        pos = np.vstack([pos, wall])

        n_tot = len(pos)
        h = 1.0 / (n_tot ** (1.0 / 3.0)) * 1.5
        vol = np.full(n_tot, 1.0 / n_tot, dtype=np.float64)

        A_iS, sum_grad = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True)
        is_bnd = coupler.detect_boundary_particles(pos, vol, h)

        # Exact consistency check on boundary particles
        bnd_res = np.linalg.norm(A_iS[is_bnd] + sum_grad[is_bnd], axis=1)
        assert np.max(bnd_res) < 1e-10, f"0th-order consistency violated: {np.max(bnd_res):.2e}"

        # Interior particles must have vanishing A_iS
        int_norm = np.linalg.norm(A_iS[~is_bnd], axis=1)
        assert np.all(int_norm < 1e-6), "Interior particles should have A_iS ~ 0"


# ---------------------------------------------------------------------------
# OPMIS Taylor Interpolator: Linear Moment Reproduction Tests
# ---------------------------------------------------------------------------

class TestOPMISTaylorInterpolator:
    """Test exact linear field moment reproduction for P2M and M2P transfers."""

    @pytest.fixture
    def synthetic_mesh_and_particles(self):
        """Create structured hexahedral grid and bounded particle set."""
        nx, ny, nz = 4, 4, 4
        x = np.linspace(0.0, 1.0, nx + 1)
        y = np.linspace(0.0, 1.0, ny + 1)
        z = np.linspace(0.0, 1.0, nz + 1)
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
                        point_idx[(i, j, k)], point_idx[(i + 1, j, k)], point_idx[(i + 1, j + 1, k)], point_idx[(i, j + 1, k)],
                        point_idx[(i, j, k + 1)], point_idx[(i + 1, j, k + 1)], point_idx[(i + 1, j + 1, k + 1)], point_idx[(i, j + 1, k + 1)],
                    ]
                    cells.append(np.array(c, dtype=np.int64))

        mesh = compute_difvm_topology(points_arr, cells)

        rng = np.random.default_rng(999)
        pos = rng.uniform(0.02, 0.98, size=(250, 3))
        wall = np.zeros((50, 3))
        wall[:, 0] = rng.uniform(0.0, 0.03, size=50)
        wall[:, 1] = rng.uniform(0.0, 1.0, size=50)
        wall[:, 2] = rng.uniform(0.0, 1.0, size=50)
        pos = np.vstack([pos, wall])

        n_tot = len(pos)
        h = 1.0 / (n_tot ** (1.0 / 3.0)) * 1.5
        vol = np.full(n_tot, 1.0 / n_tot, dtype=np.float64)

        return mesh, pos, vol, h

    def test_p2m_scalar_linear_reproduction(self, synthetic_mesh_and_particles):
        """P2M must reproduce arbitrary 3D linear field with L_inf < 10^-11."""
        mesh, pos, vol, h = synthetic_mesh_and_particles
        interpolator = OPMISTaylorInterpolator()

        # Arbitrary linear field: phi(x, y, z) = -1.75*x + 3.2*y - 4.1*z + 0.8
        a = np.array([-1.75, 3.2, -4.1], dtype=np.float64)
        b = 0.8
        field_p = np.dot(pos, a) + b

        interp_mesh = interpolator.interpolate_p2m(pos, field_p, vol, h, mesh)
        exact_mesh = np.dot(mesh.cell_centres, a) + b

        error = np.max(np.abs(interp_mesh - exact_mesh))
        assert error < 1e-11, f"P2M linear moment reproduction error {error:.2e} >= 1e-11"

    def test_p2m_vector_field_reproduction(self, synthetic_mesh_and_particles):
        """P2M must reproduce arbitrary vector linear fields."""
        mesh, pos, vol, h = synthetic_mesh_and_particles
        interpolator = OPMISTaylorInterpolator()

        # Vector field with 3 linear components (velocity u, v, w)
        A_mat = np.array([
            [1.2, -0.5, 0.3],
            [-0.8, 2.1, -1.4],
            [0.5, 0.9, 1.7],
        ], dtype=np.float64)
        b_vec = np.array([0.1, -0.2, 0.4], dtype=np.float64)

        field_p = pos @ A_mat.T + b_vec  # (N, 3)
        interp_mesh = interpolator.interpolate_p2m(pos, field_p, vol, h, mesh)
        exact_mesh = mesh.cell_centres @ A_mat.T + b_vec

        error = np.max(np.abs(interp_mesh - exact_mesh))
        assert error < 1e-11, f"P2M vector reproduction error {error:.2e} >= 1e-11"

    def test_m2p_affine_linear_reproduction(self, synthetic_mesh_and_particles):
        """M2P must reproduce arbitrary linear fields with L_inf < 10^-11."""
        mesh, pos, vol, h = synthetic_mesh_and_particles
        interpolator = OPMISTaylorInterpolator()

        a = np.array([2.4, -3.1, 1.8], dtype=np.float64)
        b = -0.5
        exact_mesh = np.dot(mesh.cell_centres, a) + b

        query_pts = pos[:40]
        interp_pts = interpolator.interpolate_m2p(mesh, exact_mesh, query_pts)
        exact_pts = np.dot(query_pts, a) + b

        error = np.max(np.abs(interp_pts - exact_pts))
        assert error < 1e-11, f"M2P affine least-squares error {error:.2e} >= 1e-11"
