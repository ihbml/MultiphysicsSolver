"""Comprehensive Unit Test Suite for Moving Mesh ALE & Degenerate Cell Detection (Features 15-16)."""

import numpy as np
import pytest

from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import UnstructuredMesh
from backend.meshing.moving_mesh import ALEMeshMover


def create_test_hex_grid(nx=4, ny=4, nz=4, lx=1.0, ly=1.0, lz=1.0):
    """Generate structured hexahedral cells."""
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
                    point_idx[(i, j, k)], point_idx[(i + 1, j, k)], point_idx[(i + 1, j + 1, k)], point_idx[(i, j + 1, k)],
                    point_idx[(i, j, k + 1)], point_idx[(i + 1, j, k + 1)], point_idx[(i + 1, j + 1, k + 1)], point_idx[(i, j + 1, k + 1)],
                ]
                cells.append(np.array(c, dtype=np.int64))
    return points_arr, cells


class TestALEMeshMover:
    """Test ALE Laplacian smoothing, volume positivity, and fixed boundaries."""

    def test_laplacian_smoothing_preserves_fixed_boundary(self):
        """Fixed boundary nodes must have strictly zero displacement (< 10^-12)."""
        points, cells = create_test_hex_grid(nx=5, ny=5, nz=5, lx=1.0, ly=1.0, lz=1.0)
        mesh = compute_difvm_topology(points, cells)
        mover = ALEMeshMover(stiffness_exponent=1.0)

        top_mask = points[:, 2] > 0.99
        disp_vec = np.array([0.04, -0.03, 0.01], dtype=np.float64)
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = disp_vec

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # Bottom nodes must remain fixed
        bottom_mask = points[:, 2] < 0.01
        bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
        assert np.max(bottom_disp) < 1e-12, f"Fixed boundary moved: {np.max(bottom_disp):.2e}"

    def test_laplacian_smoothing_positive_volumes(self):
        """All cell volumes must remain strictly positive (V_i > 10^-15) after deformation."""
        points, cells = create_test_hex_grid(nx=4, ny=4, nz=4, lx=0.5, ly=0.5, lz=0.5)
        mesh = compute_difvm_topology(points, cells)
        mover = ALEMeshMover()

        # Prescribe displacement
        top_mask = points[:, 2] > 0.49
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = [0.03, 0.01, -0.02]

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        assert hasattr(moved_mesh, "cell_volumes")
        assert np.all(moved_mesh.cell_volumes > 1e-15), (
            f"Found non-positive volume: min={np.min(moved_mesh.cell_volumes)}"
        )
        assert np.all(np.isfinite(moved_mesh.cell_volumes))

    def test_smooth_displacement_attenuation(self):
        """Displacement must decay monotonically and smoothly from moving wall to fixed wall."""
        points, cells = create_test_hex_grid(nx=6, ny=6, nz=6, lx=1.0, ly=1.0, lz=1.0)
        mesh = compute_difvm_topology(points, cells)
        mover = ALEMeshMover()

        top_mask = points[:, 2] > 0.99
        disp_vec = np.array([0.05, 0.0, 0.0], dtype=np.float64)
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = disp_vec

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # Check interior displacement at z ~ 0.5
        mid_mask = (points[:, 2] > 0.49) & (points[:, 2] < 0.51)
        assert np.any(mid_mask)
        mid_disp = np.linalg.norm(moved_mesh.points[mid_mask] - points[mid_mask], axis=1)

        max_disp = np.linalg.norm(disp_vec)
        assert np.all(mid_disp < max_disp)
        assert np.all(mid_disp > 0.0)
        # Midpoint should be approximately half
        assert np.all(np.abs(mid_disp - 0.5 * max_disp) < 0.05 * max_disp)

    def test_zero_displacement_is_identity(self):
        """Prescribing zero displacement should return identical mesh without distortion."""
        points, cells = create_test_hex_grid(nx=3, ny=3, nz=3)
        mesh = compute_difvm_topology(points, cells)
        mover = ALEMeshMover()

        boundary_disp = np.zeros_like(points)
        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        disp = np.max(np.linalg.norm(moved_mesh.points - points, axis=1))
        assert disp < 1e-15, f"Mesh drifted under zero displacement: {disp:.2e}"


class TestDegenerateCellDetection:
    """Test detection of inverted, high-skewness, and collapsed cells."""

    def test_clean_mesh_has_zero_degenerate_cells(self):
        """A valid hexahedral mesh must have exactly 0 degenerate cells."""
        points, cells = create_test_hex_grid(nx=3, ny=3, nz=3)
        mesh = compute_difvm_topology(points, cells)
        mover = ALEMeshMover()

        bad_cells = mover.detect_degenerate_cells(mesh)
        assert len(bad_cells) == 0, f"Expected 0 bad cells, found {len(bad_cells)}"

    def test_detect_inverted_cell_negative_volume(self):
        """Crossed vertex coordinates resulting in negative volume must be detected."""
        points, cells = create_test_hex_grid(nx=3, ny=3, nz=3)
        mover = ALEMeshMover()

        # Invert cell 2 by crossing diagonal vertices
        corrupted_points = points.copy()
        c2_nodes = cells[2]
        corrupted_points[c2_nodes[0]] = points[c2_nodes[6]] + [0.4, 0.4, 0.4]

        corrupted_mesh = compute_difvm_topology(corrupted_points, cells)
        bad_cells = mover.detect_degenerate_cells(corrupted_mesh)

        assert len(bad_cells) > 0, "Failed to detect corrupted mesh"
        assert 2 in bad_cells, "Inverted cell 2 was not identified"

    def test_detect_nan_centroid_corrupted_cell(self):
        """NaN coordinate must trigger degenerate cell detection."""
        points, cells = create_test_hex_grid(nx=3, ny=3, nz=3)
        mover = ALEMeshMover()

        corrupted_points = points.copy()
        corrupted_points[cells[1][0]] = [np.nan, 0.0, 0.0]

        corrupted_mesh = compute_difvm_topology(corrupted_points, cells)
        bad_cells = mover.detect_degenerate_cells(corrupted_mesh)

        assert len(bad_cells) > 0
        assert 1 in bad_cells


class TestMeshVelocityAndDGCL:
    """Test grid velocity calculation and Discrete Geometric Conservation Law."""

    def test_grid_velocity_calculation(self):
        """Grid velocity w = (x^{n+1} - x^n) / dt."""
        mover = ALEMeshMover()
        p0 = np.array([[0.0, 0.0, 0.0], [1.0, 1.0, 1.0]])
        p1 = np.array([[0.1, 0.2, 0.3], [1.05, 1.1, 1.2]])
        dt = 0.01

        w = mover.compute_mesh_velocity(p0, p1, dt)
        expected_w = (p1 - p0) / dt
        assert np.allclose(w, expected_w)

    def test_invalid_dt_raises_error(self):
        """dt <= 0 must raise ValueError."""
        mover = ALEMeshMover()
        p = np.zeros((4, 3))
        with pytest.raises(ValueError):
            mover.compute_mesh_velocity(p, p, dt=0.0)
        with pytest.raises(ValueError):
            mover.compute_mesh_velocity(p, p, dt=-0.1)
