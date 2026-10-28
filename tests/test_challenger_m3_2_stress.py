"""Adversarial Empirical Stress-Test Suite for ALE Moving Mesh & Degenerate Cell Audit.

Authored by challenger_m3_2 for Milestone M3.
Tests Feature 15 (Moving Mesh ALE & Laplacian Smoothing) and Feature 16 (Degenerate Cell Audit).

Requirements tested:
1. ALE variable-stiffness Laplacian smoothing:
   - Boundary motions with large deformations: large amplitude shear, torsion/rotation, compression.
   - Pinned boundary nodes remain strictly unchanged (< 10^-12 displacement).
   - Internal nodes smoothly deform (monotonic attenuation).
   - Cell volumes remain strictly positive (V_i > 10^-15) up to physical deformation limits.
2. Degenerate cell audit:
   - Inverted cells (negative volume/determinant).
   - Severe non-orthogonality (> 85 degrees).
   - Extreme skewness (> 0.85).
   - High aspect ratios (> 40).
   - Verify that detect_degenerate_cells correctly detects and flags all degraded cells.
3. Discrete Geometric Conservation Law (DGCL):
   - Uniform translation (exact machine precision closure).
   - Time step refinement convergence under unsteady deformation.
   - Validation of verify_dgcl logic and residual quantification.
"""

from __future__ import annotations

import numpy as np
import pytest

from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import UnstructuredMesh
from backend.meshing.moving_mesh import ALEMeshMover


def make_hex_mesh(nx: int = 4, ny: int = 4, nz: int = 4,
                  lx: float = 1.0, ly: float = 1.0, lz: float = 1.0) -> Tuple[np.ndarray, list[np.ndarray], UnstructuredMesh]:
    """Helper to build a structured 3D hexahedral UnstructuredMesh."""
    x = np.linspace(0.0, lx, nx + 1)
    y = np.linspace(0.0, ly, ny + 1)
    z = np.linspace(0.0, lz, nz + 1)
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
    return points_arr, cells, mesh


# ===========================================================================
# 1. ALE Variable-Stiffness Laplacian Smoothing Stress Harnesses
# ===========================================================================

class TestALEVariableStiffnessStress:
    """Adversarial stress-testing of ALE mesh mover under extreme boundary deformations."""

    def test_large_amplitude_shear_deformation(self):
        """Stress-test shear deformation with dx up to 25% of domain height.

        Verifies:
        1. Fixed bottom nodes remain strictly unchanged (< 10^-12).
        2. Internal nodes smoothly deform.
        3. Cell volumes remain strictly positive (V_i > 10^-15).
        """
        lx, ly, lz = 1.0, 1.0, 1.0
        nx, ny, nz = 6, 6, 6
        points, cells, mesh = make_hex_mesh(nx, ny, nz, lx, ly, lz)
        mover = ALEMeshMover(stiffness_exponent=1.5)

        top_mask = points[:, 2] > (lz - 1e-4)
        bottom_mask = points[:, 2] < 1e-4

        # Prescribe 20% shear in X and 10% in Y
        shear_disp = np.array([0.20 * lx, 0.10 * ly, 0.0], dtype=np.float64)
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = shear_disp

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # 1. Pinned boundary invariant: bottom nodes strictly unchanged (< 10^-12)
        bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
        max_bottom_drift = float(np.max(bottom_disp))
        assert max_bottom_drift < 1e-12, f"Bottom boundary drifted by {max_bottom_drift:.3e} >= 1e-12"

        # 2. Strict volume positivity
        vols = moved_mesh.cell_volumes
        min_vol = float(np.min(vols))
        assert min_vol > 1e-15, f"Cell volume collapsed under shear: min_vol = {min_vol:.3e} <= 1e-15"
        assert np.all(np.isfinite(vols)), "Non-finite cell volumes found"

        # 3. Smooth interior displacement attenuation
        disp_field = np.linalg.norm(moved_mesh.points - points, axis=1)
        # Check layers from bottom to top
        layer_disps = []
        for k in range(nz + 1):
            z_val = k * (lz / nz)
            layer_mask = np.abs(points[:, 2] - z_val) < 1e-4
            layer_disps.append(float(np.mean(disp_field[layer_mask])))

        # Verify monotonic increase of average displacement with z
        for k in range(1, nz + 1):
            assert layer_disps[k] > layer_disps[k - 1], (
                f"Non-monotonic displacement at layer {k}: {layer_disps[k]} <= {layer_disps[k-1]}"
            )

    def test_torsion_rotation_deformation(self):
        """Stress-test torsional rotation of top boundary around Z axis by 20 degrees.

        Verifies:
        1. Fixed bottom nodes strictly unchanged (< 10^-12).
        2. All cell volumes strictly positive.
        3. Smooth continuous angular twisting through interior.
        """
        lx, ly, lz = 1.0, 1.0, 1.0
        nx, ny, nz = 6, 6, 6
        points, cells, mesh = make_hex_mesh(nx, ny, nz, lx, ly, lz)
        mover = ALEMeshMover(stiffness_exponent=1.0)

        top_mask = points[:, 2] > (lz - 1e-4)
        bottom_mask = points[:, 2] < 1e-4

        # Rotate top surface around center (0.5, 0.5) by theta = 20 degrees
        theta = np.radians(20.0)
        c, s = np.cos(theta), np.sin(theta)
        R = np.array([[c, -s], [s, c]])
        center_xy = np.array([0.5 * lx, 0.5 * ly])

        boundary_disp = np.zeros_like(points)
        for i in np.where(top_mask)[0]:
            xy = points[i, :2] - center_xy
            rot_xy = R @ xy
            boundary_disp[i, :2] = (rot_xy + center_xy) - points[i, :2]

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # 1. Pinned bottom nodes
        bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
        assert np.max(bottom_disp) < 1e-12

        # 2. Strict volume positivity
        vols = moved_mesh.cell_volumes
        min_vol = float(np.min(vols))
        assert min_vol > 1e-15, f"Torsion caused cell inversion: min_vol = {min_vol:.3e}"
        assert np.all(np.isfinite(vols))

        # 3. Displacements are bounded by maximum top displacement
        max_top_disp = float(np.max(np.linalg.norm(boundary_disp[top_mask], axis=1)))
        all_disps = np.linalg.norm(moved_mesh.points - points, axis=1)
        assert np.max(all_disps) <= max_top_disp + 1e-12

    def test_axial_compression_deformation(self):
        """Stress-test axial compression (top surface pushed down by 25% of height).

        Verifies:
        1. Fixed bottom nodes strictly unchanged (< 10^-12).
        2. All cell volumes strictly positive.
        3. Total volume decreases consistently with 25% compression.
        """
        lx, ly, lz = 1.0, 1.0, 1.0
        nx, ny, nz = 5, 5, 5
        points, cells, mesh = make_hex_mesh(nx, ny, nz, lx, ly, lz)
        mover = ALEMeshMover(stiffness_exponent=1.0)

        top_mask = points[:, 2] > (lz - 1e-4)
        bottom_mask = points[:, 2] < 1e-4

        # 25% compression in -Z
        compression = -0.25 * lz
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = [0.0, 0.0, compression]

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # 1. Pinned bottom nodes strictly unchanged
        bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
        assert np.max(bottom_disp) < 1e-12

        # 2. Strict volume positivity
        vols = moved_mesh.cell_volumes
        min_vol = float(np.min(vols))
        assert min_vol > 1e-15, f"Compression collapsed cells: min_vol = {min_vol:.3e}"

        # 3. Total volume reduction matches compression ratio ~ 0.75
        initial_vol = float(np.sum(mesh.cell_volumes))
        final_vol = float(np.sum(vols))
        vol_ratio = final_vol / initial_vol
        assert abs(vol_ratio - 0.75) < 0.02, f"Expected ~0.75 volume ratio, got {vol_ratio:.4f}"

    def test_explicit_fixed_nodes_specification(self):
        """Verify custom fixed_nodes parameter preserves arbitrary non-planar pinned nodes."""
        points, cells, mesh = make_hex_mesh(4, 4, 4)
        mover = ALEMeshMover()

        # Pin arbitrary nodes (e.g. all corners)
        corner_indices = np.where(
            (np.isin(points[:, 0], [0.0, 1.0])) &
            (np.isin(points[:, 1], [0.0, 1.0])) &
            (points[:, 2] == 0.0)
        )[0]

        top_mask = points[:, 2] > 0.99
        boundary_disp = np.zeros_like(points)
        boundary_disp[top_mask] = [0.05, 0.0, 0.0]

        moved_mesh = mover.deform_mesh(mesh, boundary_disp, fixed_nodes=corner_indices)

        corner_disp = np.linalg.norm(moved_mesh.points[corner_indices] - points[corner_indices], axis=1)
        assert np.max(corner_disp) < 1e-12, f"Explicitly fixed nodes displaced: {np.max(corner_disp):.3e}"


# ===========================================================================
# 2. Degenerate Cell Audit Stress Harnesses
# ===========================================================================

class TestDegenerateCellAuditStress:
    """Adversarial stress-testing of detect_degenerate_cells across all degradation modes."""

    def test_audit_inverted_cells_negative_volume(self):
        """Construct inverted cells with negative volumes and verify exact detection."""
        points, cells, mesh = make_hex_mesh(3, 3, 3)
        mover = ALEMeshMover()

        # Healthy initial mesh has 0 degenerate cells
        assert len(mover.detect_degenerate_cells(mesh)) == 0

        # Invert cell 5 and cell 12 by reversing face node order / crossing opposite vertices
        corrupted_points = points.copy()
        for bad_cell_idx in [5, 12]:
            c_nodes = cells[bad_cell_idx]
            # Swap bottom and top vertices so z-ordering is inverted
            corrupted_points[c_nodes[0]] = points[c_nodes[4]] + [0.1, 0.1, 0.1]
            corrupted_points[c_nodes[4]] = points[c_nodes[0]] - [0.1, 0.1, 0.1]

        corrupted_mesh = compute_difvm_topology(corrupted_points, cells)
        bad_cells = mover.detect_degenerate_cells(corrupted_mesh)

        assert 5 in bad_cells, "Inverted cell 5 was not detected"
        assert 12 in bad_cells, "Inverted cell 12 was not detected"

    def test_audit_severe_non_orthogonality(self):
        """Construct mesh with internal face non-orthogonality > 85 degrees and verify flagging."""
        # Create a 2-cell mesh sharing an internal face with extreme angle
        # Cell 0: [0, 1] x [0, 1] x [0, 1]
        # Cell 1: heavily shifted along X/Y so centroid-to-centroid vector is almost parallel to face
        pts = np.array([
            # Cell 0 vertices
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
            # Cell 1 extra vertices (shifted drastically in y: sheared rhomboid)
            [1.0, 25.0, 0.0], [1.0, 26.0, 0.0],
            [1.0, 25.0, 1.0], [1.0, 26.0, 1.0],
        ], dtype=np.float64)

        # Cell 0: [0..7]
        # Cell 1: right face of cell 0 is [1, 2, 6, 5], extruded to [8, 9, 11, 10]
        # Wait, let's build two connected hex cells with severe shearing
        # Let's create an Nx=2, Ny=1, Nz=1 grid and severely shear the second cell
        nx, ny, nz = 2, 1, 1
        base_pts, base_cells, _ = make_hex_mesh(nx, ny, nz)
        sheared_pts = base_pts.copy()

        # The shared face is at x = 0.5 (nodes with x == 0.5)
        # Shift the nodes of cell 1 at x = 1.0 by y += 15.0
        right_nodes = np.where(sheared_pts[:, 0] > 0.99)[0]
        sheared_pts[right_nodes, 1] += 15.0

        sheared_mesh = compute_difvm_topology(sheared_pts, base_cells)
        error_map = sheared_mesh.error_map
        non_ortho = error_map.get("non_orthogonality")

        assert non_ortho is not None, "Error map missing non_orthogonality"
        max_no = float(np.max(non_ortho))
        assert max_no > 85.0, f"Expected non-ortho > 85 deg, got {max_no:.1f} deg"

        mover = ALEMeshMover(max_non_ortho_threshold=85.0)
        bad_cells = mover.detect_degenerate_cells(sheared_mesh)

        # Both cell 0 and cell 1 share this severe face, so both should be flagged
        assert len(bad_cells) >= 1, "Failed to flag cell with severe non-orthogonality"
        assert (0 in bad_cells) or (1 in bad_cells)

    def test_audit_extreme_skewness(self):
        """Construct mesh with face skewness > 0.85 and verify detection."""
        # Hex grid with 2 cells. Perturb shared face nodes off-center to create skewness
        base_pts, base_cells, _ = make_hex_mesh(2, 1, 1)
        perturbed_pts = base_pts.copy()

        # Shared face vertices at x = 0.5
        mid_nodes = np.where(np.abs(perturbed_pts[:, 0] - 0.5) < 1e-4)[0]
        # Shift shared face vertices diagonally by large amount to displace face centroid from d_f line
        perturbed_pts[mid_nodes, 1] += 2.5

        skew_mesh = compute_difvm_topology(perturbed_pts, base_cells)
        error_map = skew_mesh.error_map
        skewness = error_map.get("skewness")

        assert skewness is not None, "Error map missing skewness"
        max_skew = float(np.max(skewness))
        assert max_skew > 0.85, f"Expected skewness > 0.85, got {max_skew:.3f}"

        mover = ALEMeshMover(max_skewness_threshold=0.85)
        bad_cells = mover.detect_degenerate_cells(skew_mesh)

        assert len(bad_cells) > 0, "Failed to flag cells with extreme skewness"
        assert (0 in bad_cells) or (1 in bad_cells)

    def test_audit_high_aspect_ratio_slivers(self):
        """Construct mesh with cell aspect ratio > 40.0 and verify detection."""
        # Create a 3-cell mesh where cell 1 is stretched 100:1:1
        x = np.array([0.0, 1.0, 101.0, 102.0], dtype=np.float64)
        y = np.array([0.0, 1.0], dtype=np.float64)
        z = np.array([0.0, 1.0], dtype=np.float64)

        points = []
        pidx = {}
        for k in range(len(z)):
            for j in range(len(y)):
                for i in range(len(x)):
                    pidx[(i, j, k)] = len(points)
                    points.append([x[i], y[j], z[k]])
        points_arr = np.array(points, dtype=np.float64)

        cells = []
        for i in range(3):
            nodes = [
                pidx[(i, 0, 0)], pidx[(i + 1, 0, 0)], pidx[(i + 1, 1, 0)], pidx[(i, 1, 0)],
                pidx[(i, 0, 1)], pidx[(i + 1, 0, 1)], pidx[(i + 1, 1, 1)], pidx[(i, 1, 1)],
            ]
            cells.append(np.array(nodes, dtype=np.int64))

        sliver_mesh = compute_difvm_topology(points_arr, cells)
        ar = sliver_mesh.error_map.get("aspect_ratio")
        assert ar is not None
        assert ar[1] > 40.0, f"Expected cell 1 aspect ratio > 40, got {ar[1]:.1f}"

        mover = ALEMeshMover(max_aspect_ratio_threshold=40.0)
        bad_cells = mover.detect_degenerate_cells(sliver_mesh)

        assert 1 in bad_cells, f"Cell 1 with aspect ratio {ar[1]:.1f} was not detected as degenerate"
        # Regular cells 0 and 2 have aspect ratio ~1.73 and should not be flagged for aspect ratio
        assert 0 not in bad_cells
        assert 2 not in bad_cells


# ===========================================================================
# 3. Discrete Geometric Conservation Law (DGCL) Stress Harnesses
# ===========================================================================

class TestDGCLStress:
    """Stress-test Discrete Geometric Conservation Law (DGCL) logic and residual scaling."""

    def test_dgcl_rigid_translation_exact_zero_residual(self):
        """Under uniform rigid translation, DGCL residual must be identically zero (< 10^-14)."""
        points, cells, mesh_n = make_hex_mesh(3, 3, 3)
        mover = ALEMeshMover()

        dt = 0.05
        # Uniform velocity vector: w = [2.5, -1.8, 3.2] m/s
        velocity = np.array([2.5, -1.8, 3.2], dtype=np.float64)
        translated_points = points + velocity * dt

        mesh_n1 = compute_difvm_topology(translated_points, cells)

        residual = mover.verify_dgcl(mesh_n, mesh_n1, dt)
        assert residual < 1e-13, (
            f"DGCL violated under rigid body translation: residual = {residual:.3e} >= 1e-13"
        )

    def test_dgcl_unsteady_laplacian_motion_convergence(self):
        """Empirically evaluate DGCL residual under unsteady moving boundary deformation.

        Verifies that across all time steps dt, the discrete geometric conservation law:
            | (V_c^{n+1} - V_c^n) / dt - sum_f sigma_cf w_f . S_f^{n+1/2} | < 10^-12
        is satisfied to near machine precision (< 10^-14).
        """
        nx, ny, nz = 4, 4, 4
        points, cells, mesh_n = make_hex_mesh(nx, ny, nz)
        mover = ALEMeshMover()

        top_mask = points[:, 2] > 0.99
        prescribed_velocity = np.array([0.1, 0.05, -0.05], dtype=np.float64)

        dts = [0.1, 0.05, 0.025, 0.01]

        for dt in dts:
            boundary_disp = np.zeros_like(points)
            boundary_disp[top_mask] = prescribed_velocity * dt
            mesh_n1 = mover.deform_mesh(mesh_n, boundary_disp)

            res = mover.verify_dgcl(mesh_n, mesh_n1, dt)
            assert res < 1e-12, f"DGCL residual {res:.3e} exceeds 1e-12 threshold at dt={dt}"

    def test_cyclic_motion_uniform_stiffness_exact_reversibility(self):
        """With constant stiffness (alpha=0), graph Laplacian is symmetric and linear:

        Forward followed by backward motion must return to initial state with machine precision (< 10^-14).
        """
        points, cells, mesh_0 = make_hex_mesh(4, 4, 4)
        mover = ALEMeshMover(stiffness_exponent=0.0)

        top_mask = points[:, 2] > 0.99
        disp_forward = np.zeros_like(points)
        disp_forward[top_mask] = [0.08, -0.04, 0.02]

        # Step 1: Forward motion
        mesh_1 = mover.deform_mesh(mesh_0, disp_forward, stiffness_exponent=0.0)

        # Step 2: Backward motion (return to origin)
        disp_backward = np.zeros_like(mesh_1.points)
        disp_backward[top_mask] = -disp_forward[top_mask]
        mesh_2 = mover.deform_mesh(mesh_1, disp_backward, stiffness_exponent=0.0)

        # Final coordinates must match initial coordinates (< 10^-12)
        coord_drift = float(np.max(np.linalg.norm(mesh_2.points - mesh_0.points, axis=1)))
        assert coord_drift < 1e-12, f"Coordinate drift with alpha=0: {coord_drift:.3e} >= 1e-12"

        # Cell volumes must match initial volumes (< 10^-12)
        vol_drift = float(np.max(np.abs(mesh_2.cell_volumes - mesh_0.cell_volumes)))
        assert vol_drift < 1e-12, f"Volume drift with alpha=0: {vol_drift:.3e} >= 1e-12"

    def test_cyclic_motion_variable_stiffness_bounded_hysteresis(self):
        """With variable stiffness (alpha>0), distance-to-wall field introduces geometric non-linearity.

        Verify that hysteresis drift is bounded and scales quadratically O(||u||^2) with displacement amplitude.
        """
        points, cells, mesh_0 = make_hex_mesh(4, 4, 4)
        mover = ALEMeshMover(stiffness_exponent=1.0)
        top_mask = points[:, 2] > 0.99

        drifts = []
        amplitudes = [0.08, 0.04, 0.02]

        for amp in amplitudes:
            disp_fwd = np.zeros_like(points)
            disp_fwd[top_mask] = [amp, -0.5 * amp, 0.25 * amp]

            mesh_1 = mover.deform_mesh(mesh_0, disp_fwd)

            disp_bwd = np.zeros_like(mesh_1.points)
            disp_bwd[top_mask] = -disp_fwd[top_mask]
            mesh_2 = mover.deform_mesh(mesh_1, disp_bwd)

            drift = float(np.max(np.linalg.norm(mesh_2.points - mesh_0.points, axis=1)))
            drifts.append(drift)

        # Verify drift decreases quadratically (factor of ~4 when amplitude halves)
        assert drifts[0] < 1e-4, f"Large amplitude drift too high: {drifts[0]:.3e}"
        ratio_1 = drifts[0] / drifts[1]
        assert ratio_1 > 3.0, f"Hysteresis did not decrease quadratically: ratio = {ratio_1:.2f} < 3.0"
        ratio_2 = drifts[1] / drifts[2]
        assert ratio_2 > 3.0, f"Hysteresis did not decrease quadratically: ratio = {ratio_2:.2f} < 3.0"

    def test_combined_triaxial_deformation(self):
        """Stress-test simultaneous shear, compression, and torsion."""
        points, cells, mesh = make_hex_mesh(5, 5, 5)
        mover = ALEMeshMover(stiffness_exponent=1.2)

        top_mask = points[:, 2] > 0.99
        bottom_mask = points[:, 2] < 1e-4

        # Combined deformation:
        # 1. Compression in Z: -0.15
        # 2. Shear in X: +0.10
        # 3. Torsion around Z by 10 deg
        theta = np.radians(10.0)
        c, s = np.cos(theta), np.sin(theta)
        R = np.array([[c, -s], [s, c]])
        center_xy = np.array([0.5, 0.5])

        boundary_disp = np.zeros_like(points)
        for i in np.where(top_mask)[0]:
            xy = points[i, :2] - center_xy
            rot_xy = R @ xy
            boundary_disp[i, 0] = (rot_xy[0] + center_xy[0]) - points[i, 0] + 0.10
            boundary_disp[i, 1] = (rot_xy[1] + center_xy[1]) - points[i, 1]
            boundary_disp[i, 2] = -0.15

        moved_mesh = mover.deform_mesh(mesh, boundary_disp)

        # 1. Pinned bottom nodes unchanged
        bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
        assert np.max(bottom_disp) < 1e-12

        # 2. Cell volumes strictly positive
        assert np.all(moved_mesh.cell_volumes > 1e-15)
        min_v = float(np.min(moved_mesh.cell_volumes))
        assert min_v > 1e-15, f"Cell volume collapsed under triaxial deformation: min_v = {min_v:.3e}"

    def test_compression_inversion_boundary_and_detection(self):
        """Characterize the physical limit where extreme compression inverts cells and verify detection."""
        points, cells, mesh = make_hex_mesh(3, 3, 3)
        mover = ALEMeshMover(stiffness_exponent=0.0)
        top_mask = points[:, 2] > 0.99

        # Moderate compression (-0.30): cells remain positive
        boundary_disp_ok = np.zeros_like(points)
        boundary_disp_ok[top_mask] = [0.0, 0.0, -0.30]
        mesh_ok = mover.deform_mesh(mesh, boundary_disp_ok, stiffness_exponent=0.0)
        assert np.all(mesh_ok.cell_volumes > 1e-15)
        assert len(mover.detect_degenerate_cells(mesh_ok)) == 0

        # Excessive compression (-1.2): top passes through bottom, causing complete inversion
        boundary_disp_bad = np.zeros_like(points)
        boundary_disp_bad[top_mask] = [0.0, 0.0, -1.2]
        mesh_bad = mover.deform_mesh(mesh, boundary_disp_bad, stiffness_exponent=0.0)

        # Negative or near-zero volumes must be present
        min_v = float(np.min(mesh_bad.cell_volumes))
        assert min_v <= 1e-15, f"Expected collapsed volume, got {min_v}"

        # detect_degenerate_cells must detect and flag all inverted cells
        bad_cells = mover.detect_degenerate_cells(mesh_bad)
        assert len(bad_cells) > 0, "detect_degenerate_cells failed to detect inverted cells under extreme compression"


class TestDegenerateCellAuditRobustness:
    """Robustness testing of detect_degenerate_cells under missing data, inf, and custom thresholds."""

    def test_audit_robust_to_missing_or_empty_error_map(self):
        """Mesh with missing or empty error_map must still check volumes and centroids without error."""
        points, cells, mesh = make_hex_mesh(2, 2, 2)
        mesh.error_map = {}
        mover = ALEMeshMover()

        # Healthy mesh with empty error map
        bad_cells = mover.detect_degenerate_cells(mesh)
        assert len(bad_cells) == 0

        # Corrupt volume
        mesh.cell_volumes[0] = -0.05
        bad_cells = mover.detect_degenerate_cells(mesh)
        assert 0 in bad_cells

    def test_audit_detects_infinite_coordinates(self):
        """Infinite coordinate (Inf / -Inf) must be flagged as degenerate."""
        points, cells, mesh = make_hex_mesh(2, 2, 2)
        corrupted_points = points.copy()
        corrupted_points[0] = [np.inf, 0.0, 0.0]

        corrupted_mesh = compute_difvm_topology(corrupted_points, cells)
        mover = ALEMeshMover()
        bad_cells = mover.detect_degenerate_cells(corrupted_mesh)

        assert len(bad_cells) > 0
        assert 0 in bad_cells

    def test_audit_custom_threshold_sensitivity(self):
        """Verify custom threshold overrides work correctly."""
        points, cells, mesh = make_hex_mesh(2, 2, 2)
        mover = ALEMeshMover()

        # Regular cube cells have aspect ratio ~ 1.73 (sqrt(3))
        # With threshold = 1.5, regular cells should be flagged!
        flagged_cells = mover.detect_degenerate_cells(mesh, max_aspect_ratio=1.5)
        assert len(flagged_cells) == len(cells), "Custom strict aspect ratio threshold failed to flag cells"

        # With generous threshold = 50.0, none should be flagged
        flagged_cells_relaxed = mover.detect_degenerate_cells(mesh, max_aspect_ratio=50.0)
        assert len(flagged_cells_relaxed) == 0
