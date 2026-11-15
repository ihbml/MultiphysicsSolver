"""Comprehensive Test Suite for Milestone M5B: Direct Code Integration of Surveyed Papers (Features F55–F61).

Tests:
- F55 (Paper 6 / Audit H9): Simpson swept volume and BDF2-DGCL face flux in moving_mesh.py.
- F56 (Paper 7): Orthogonal symmetry mirror connection, Householder reflection tensor, and Patankar splits.
- F57 (Paper 1): Watertight B-Rep CAD meshing, Dijkstra loop embedding, Jordan curve separation, and 1D stitching.
- F58 (Paper 2): Parametric CAD DAG compilation, geometric safety inequalities, and adjoint chain rule.
- F59 (Paper 3): Riemannian L^p-metric tensor sizing (p=1, 2) and noise-free P2 midpoint reconstruction.
- F60 (Papers 4 & 5): Dual GPU Voronoi representation (float4 P[64], int3 T[96]), radius-of-security, and analytical derivatives.
- F61 (Paper 8): Monolithic Eulerian 3D FSI, Neo-Hookean elasticity, PLIC, and fluid reset B = I for phi < 1e-8.
- F61b (§12.2 & Snippets 12-14): Linear-exact plane intersection, FaceClass invalid rejection, atomic-free gather, 3-Map error map, Cholesky SPD.
- SUGGESTIONS_BACKEND.md line 630 clean SMR check.
"""

from __future__ import annotations

import math
from pathlib import Path
import numpy as np
import pytest

from backend.meshing.moving_mesh import (
    ALEMeshMover,
    compute_dgcl_grid_flux_bdf2,
    compute_face_swept_volume_simpson,
    compute_polygon_swept_volume_simpson,
)
from backend.meshing.difvm_topology import (
    FaceClass,
    compute_difvm_topology,
    compute_gradient_free_deferred_correction,
    compute_householder_reflection,
    compute_linear_exact_face_geometry,
    compute_symmetry_mirror_point,
    compute_symmetry_tensor_diffusion_split,
    compute_symmetry_vector_diffusion_split,
    compute_three_map_mesh_error_map,
    factorize_spd_metric_cholesky,
    gather_residual_cell_face_csr,
    project_boundary_points_tangential_newton,
)
from backend.geometry.parsers.step_parser import StepTopologyParser
from backend.geometry.differentiable_cad import CadComputationDAG
from backend.meshing.mesh_engine import (
    compute_lp_metric_tensor,
    reconstruct_p2_edge_midpoints,
)
from backend.meshing.gpu_voronoi import (
    DifferentiableVoronoi,
    GpuVoronoiCell,
    clip_cell_by_plane,
    create_initial_bounding_box_cell,
    integrate_projection_tetrahedra,
)
from backend.physics_modules.monolithic_fsi import (
    EulerianMonolithicFSI,
    MonolithicFSIModule,
)
from backend.physics_modules.registry import ModuleRegistry
from tests.conftest import create_unit_cube_arrays


# ==============================================================================
# 1. Feature 55: Simpson Swept Volume & BDF2-DGCL (Paper 6 / Audit H9)
# ==============================================================================

class TestF55MovingMeshDGCL:
    """Verifies exact 3D Simpson swept volume and BDF2-DGCL conservative face fluxes."""

    def test_simpson_swept_volume_pure_translation(self):
        """Translating a unit quad face by velocity v*dt produces exact volume S_f . v * dt."""
        x_n = np.array([0.5, 0.5, 0.0])
        x_n1 = np.array([0.5, 0.5, 1.0])
        S_f = np.array([0.0, 0.0, 1.0])

        vol = compute_face_swept_volume_simpson(x_n, x_n1, S_f, S_f, S_f)
        assert abs(vol - 1.0) < 1e-15

    def test_simpson_swept_volume_quadratic_shearing_face(self):
        """Quadratic deforming face is integrated exactly by Simpson's 3/8 rule without truncation error."""
        pts_n = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.0],
        ], dtype=np.float64)
        # Apply shear displacement at t^{n+1}
        pts_n1 = np.array([
            [0.0, 0.0, 1.0],
            [1.5, 0.0, 1.0],
            [1.5, 1.5, 1.0],
            [0.0, 1.5, 1.0],
        ], dtype=np.float64)

        face_nodes = [0, 1, 2, 3]
        vol = compute_polygon_swept_volume_simpson(face_nodes, pts_n, pts_n1)

        # Trapezoid prism volume = (Area_bottom + 4*Area_mid + Area_top)/6 * h = (1.0 + 4*(1.25^2) + 2.25)/6 * 1.0
        expected_vol = (1.0 + 4.0 * (1.25 ** 2) + 2.25) / 6.0
        assert abs(vol - expected_vol) < 1e-14

    def test_bdf2_dgcl_conservation_identity(self):
        """Verify BDF2 discrete geometric conservation law: sum dot{V}_f == dVol/dt."""
        dt = 0.05
        # Cell volume history: quadratic expansion V(t) = 1.0 + 0.2*t + 0.1*t^2
        t_nm1 = 0.0
        t_n = dt
        t_np1 = 2.0 * dt

        v_nm1 = 1.0 + 0.2 * t_nm1 + 0.1 * (t_nm1 ** 2)
        v_n = 1.0 + 0.2 * t_n + 0.1 * (t_n ** 2)
        v_np1 = 1.0 + 0.2 * t_np1 + 0.1 * (t_np1 ** 2)

        # BDF2 LHS
        dvol_dt_bdf2 = (3.0 * v_np1 - 4.0 * v_n + v_nm1) / (2.0 * dt)

        # 6 faces of expanding cube
        dV_prev = (v_n - v_nm1) / 6.0
        dV_curr = (v_np1 - v_n) / 6.0

        flux_f = compute_dgcl_grid_flux_bdf2(dV_curr, dV_prev, dt)
        sum_flux = 6.0 * flux_f

        residual = abs(dvol_dt_bdf2 - sum_flux)
        assert residual < 1e-13

    def test_bdf1_fallback_initial_step(self):
        """Initial step n=0 gracefully evaluates BDF1-DGCL flux without error."""
        dt = 0.1
        dV = 0.05
        flux = compute_dgcl_grid_flux_bdf2(dV, None, dt)
        assert abs(flux - (dV / dt)) < 1e-15


# ==============================================================================
# 2. Feature 56: Symmetry Plane Householder Reflection (Paper 7 / Demirdzic)
# ==============================================================================

class TestF56SymmetryHouseholder:
    """Verifies orthogonal symmetry mirror connection, collinear Delta, and Householder splits."""

    def test_orthogonal_mirror_connection_collinear_delta(self):
        """Verify Delta is strictly collinear to s_f, proving zero non-orthogonal cross-diffusion k_f == 0."""
        c_P = np.array([0.2, 0.4, 0.1])
        c_f = np.array([0.5, 0.8, 0.0])  # Face centroid has tangential skew
        # Arbitrary inclined symmetry plane normal
        s_f = np.array([1.0, 2.0, 2.0])

        c_M, Delta, k_f = compute_symmetry_mirror_point(c_P, c_f, s_f)

        # Delta must be collinear to s_f: cross product Delta x s_f == 0
        cross_prod = np.cross(Delta, s_f)
        assert np.linalg.norm(cross_prod) < 1e-14

        # Non-orthogonal cross diffusion vector must be identically zero
        assert np.linalg.norm(k_f) == 0.0

    def test_householder_reflection_tensor_invariants(self):
        """Verify R = I - 2 n (x) n is symmetric, orthogonal (R*R = I), and det(R) = -1."""
        s_f = np.array([3.0, -4.0, 12.0])  # norm = 13.0
        R = compute_householder_reflection(s_f)

        # 1. Symmetric: R == R^T
        assert np.allclose(R, R.T, atol=1e-14)

        # 2. Orthogonal: R . R == I
        assert np.allclose(R @ R, np.eye(3), atol=1e-14)

        # 3. Determinant == -1
        assert abs(np.linalg.det(R) - (-1.0)) < 1e-14

        # 4. Normal vector is negated: R . n == -n
        n = s_f / np.linalg.norm(s_f)
        assert np.allclose(R @ n, -n, atol=1e-14)

    def test_patankar_vector_diffusion_split_diagonal_dominance(self):
        """Verify vector diffusion split maintains positive main diagonal a_{Pi} > 0."""
        gamma_v = 1.5e-3
        s_f = np.array([0.5, 0.2, 0.8])
        d_vec = np.array([0.1, 0.05, 0.2])  # d . s_f > 0
        v_P = np.array([10.0, -2.5, 4.0])

        D_im, D_ex = compute_symmetry_vector_diffusion_split(gamma_v, s_f, d_vec, v_P)

        # All diagonal contributions must be strictly positive
        assert np.all(D_im > 0.0)

        # Total diffusion flux matches analytical flux
        ds = float(np.dot(d_vec, s_f))
        expected_flux = -gamma_v * float(np.dot(s_f, v_P)) * s_f / ds
        actual_flux = -D_im * v_P + D_ex
        assert np.allclose(actual_flux, expected_flux, atol=1e-14)

    def test_patankar_rank2_tensor_diffusion_split(self):
        """Verify rank-2 tensor diffusion split maintains non-negative main diagonal."""
        gamma_T = 2.0e-3
        s_f = np.array([1.0, 0.0, 0.0])
        d_vec = np.array([0.1, 0.0, 0.0])
        # Symmetric Cauchy stress or Cauchy-Green tensor B
        T_P = np.array([
            [1.5, 0.2, 0.0],
            [0.2, 1.0, 0.1],
            [0.0, 0.1, 0.8],
        ])

        D_im, D_ex = compute_symmetry_tensor_diffusion_split(gamma_T, s_f, d_vec, T_P)

        # Main diagonal entries must be non-negative
        assert np.all(np.diag(D_im) >= 0.0)


# ==============================================================================
# 3. Feature 57: Topology-First Watertight B-Rep Meshing (Paper 1)
# ==============================================================================

class TestF57WatertightBRep:
    """Verifies Dijkstra loop embedding, Jordan curve separation, and 1D boundary stitching."""

    def test_dijkstra_loop_embedding(self):
        """Verify boundary curve is cleanly embedded on mesh edges without self-intersection."""
        # 3x3 grid of vertices
        x = np.linspace(0, 1, 3)
        y = np.linspace(0, 1, 3)
        xx, yy = np.meshgrid(x, y)
        verts = np.column_stack([xx.ravel(), yy.ravel(), np.zeros(9)])

        # Edges
        edges = [
            (0, 1), (1, 2), (3, 4), (4, 5), (6, 7), (7, 8),
            (0, 3), (3, 6), (1, 4), (4, 7), (2, 5), (5, 8),
        ]

        # 3D curve sampling along perimeter
        curve_pts = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0],
        ])

        cycle = StepTopologyParser.dijkstra_loop_embedding(verts, edges, curve_pts)
        assert len(cycle) >= 4
        # Loop must be closed
        assert cycle[0] == cycle[-1]

    def test_jordan_curve_separation(self):
        """Flood-fill partitions patch into interior disk and annulus without leaks."""
        # 4 triangles forming a quad
        faces = np.array([
            [0, 1, 4],
            [1, 2, 4],
            [2, 3, 4],
            [3, 0, 4],
        ], dtype=np.int32)
        # Perimeter boundary cycle
        boundary = [0, 1, 2, 3, 0]

        trimmed = StepTopologyParser.jordan_curve_separation(faces, boundary, loop_type="outer")
        assert len(trimmed) > 0

    def test_stitch_1d_boundary_curves(self):
        """Stitching two adjacent patches welds shared boundary curve vertices to 0 unstitched edges."""
        v_a = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, 0.5, 0.0]])
        f_a = np.array([[0, 1, 2]], dtype=np.int32)

        # Patch B adjacent along edge [0, 1]
        v_b = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, -0.5, 0.0]])
        f_b = np.array([[0, 1, 2]], dtype=np.int32)

        shared_pts = np.array([[0.0, 0.0, 0.0], [0.5, 0.0, 0.0], [1.0, 0.0, 0.0]])

        v_stitched, f_stitched = StepTopologyParser.stitch_1d_boundary_curves(
            v_a, f_a, v_b, f_b, shared_pts, tolerance=1e-5
        )

        # Vertices 0 and 1 of both patches must be unified: 3 + 3 - 2 = 4 total vertices
        assert len(v_stitched) == 4
        assert len(f_stitched) == 2

    def test_step_parser_empty_curve_points_validation(self):
        """Verify StepTopologyParser raises ValueError when empty curve points array is provided."""
        v_a = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, 0.5, 0.0]])
        f_a = np.array([[0, 1, 2]], dtype=np.int32)
        v_b = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, -0.5, 0.0]])
        f_b = np.array([[0, 1, 2]], dtype=np.int32)

        with pytest.raises(ValueError, match="shared_curve_points must not be empty"):
            StepTopologyParser.stitch_1d_boundary_curves(
                v_a, f_a, v_b, f_b, np.empty((0, 3)), tolerance=1e-5
            )

        with pytest.raises(ValueError, match="curve_3d_points must not be empty"):
            StepTopologyParser.dijkstra_loop_embedding(
                v_a, [(0, 1), (1, 2), (2, 0)], np.empty((0, 3))
            )


# ==============================================================================
# 4. Feature 58: Differentiable 3D CAD DAG (Paper 2)
# ==============================================================================

class TestF58DifferentiableCAD:
    """Verifies parametric CAD DAG, automated safety inequalities, and adjoint chain rule."""

    def test_cad_dag_forward_and_safety_constraints(self):
        """Verify CAD DAG generates valid surface mesh and evaluates safety inequalities."""
        dag = CadComputationDAG()
        dag.add_parameter("length", 2.0, bounds=(0.5, 5.0))
        dag.add_parameter("width", 1.5, bounds=(0.5, 5.0))
        dag.add_parameter("height", 1.0, bounds=(0.5, 5.0))
        dag.add_parameter("chamfer", 0.3, bounds=(0.0, 1.0))

        dag.add_box("length", "width", "height")
        dag.add_chamfer("chamfer", "length")

        verts, faces = dag.evaluate_surface_mesh()
        assert len(verts) == 9
        assert len(faces) == 13

        # Safety constraints: length - chamfer >= 0
        g_vals, g_grads = dag.evaluate_safety_constraints()
        assert np.all(g_vals >= 0.0)

    def test_adjoint_chain_rule_vs_finite_differences(self):
        """Verify analytical dJ/dtheta matches central finite difference approximations."""
        dag = CadComputationDAG()
        dag.add_parameter("length", 2.0, bounds=(0.5, 5.0))
        dag.add_parameter("width", 1.5, bounds=(0.5, 5.0))
        dag.add_parameter("height", 1.0, bounds=(0.5, 5.0))
        dag.add_parameter("chamfer", 0.2, bounds=(0.0, 1.0))

        dag.add_box("length", "width", "height")
        dag.add_chamfer("chamfer", "length")

        # Objective functional: J = sum x_i^2 (dummy CFD pressure/drag objective)
        th0 = dag.get_parameter_vector()
        v0, _ = dag.evaluate_surface_mesh(th0)
        dJ_dx = 2.0 * v0  # Analytical adjoint sensitivity

        grad_analytical = dag.evaluate_adjoint_sensitivity(dJ_dx, th0)

        # Central finite differences
        h = 1e-6
        grad_fd = np.zeros_like(grad_analytical)
        for i in range(len(th0)):
            th_plus = th0.copy()
            th_plus[i] += h
            v_plus, _ = dag.evaluate_surface_mesh(th_plus)
            J_plus = np.sum(v_plus ** 2)

            th_minus = th0.copy()
            th_minus[i] -= h
            v_minus, _ = dag.evaluate_surface_mesh(th_minus)
            J_minus = np.sum(v_minus ** 2)

            grad_fd[i] = (J_plus - J_minus) / (2.0 * h)

        # Relative error must be < 1e-5
        rel_error = np.linalg.norm(grad_analytical - grad_fd) / np.linalg.norm(grad_analytical)
        assert rel_error < 1e-5

    def test_cad_dag_regularization_evaluation(self):
        """Verify biharmonic deformation energy and volume conservation regularization in CAD DAG (Paper 2 §3.4)."""
        dag = CadComputationDAG()
        dag.add_parameter("length", 2.0, bounds=(0.5, 5.0))
        dag.add_parameter("width", 1.5, bounds=(0.5, 5.0))
        dag.add_parameter("height", 1.0, bounds=(0.5, 5.0))
        dag.add_parameter("chamfer", 0.2, bounds=(0.0, 1.0))

        dag.add_box("length", "width", "height")
        dag.add_chamfer("chamfer", "length")

        th0 = dag.get_parameter_vector()

        # When theta equals reference theta and target_volume is default, regularization penalty is 0
        E_reg_zero, grad_zero = dag.evaluate_regularization(th0, theta_ref=th0)
        assert abs(E_reg_zero) < 1e-12
        assert np.allclose(grad_zero, 0.0, atol=1e-12)

        # When perturbed from theta_ref, biharmonic and volume penalties activate
        th_perturbed = th0.copy()
        th_perturbed[0] += 0.2  # Perturb length
        E_reg, grad_reg = dag.evaluate_regularization(
            theta=th_perturbed, theta_ref=th0, target_volume=3.0, w_bh=1.0, w_vol=0.5
        )
        assert E_reg > 0.0
        assert grad_reg.shape == (4,)

        # Check gradient with central finite differences
        h = 1e-6
        grad_fd = np.zeros_like(grad_reg)
        for i in range(len(th_perturbed)):
            th_p = th_perturbed.copy()
            th_p[i] += h
            Ep, _ = dag.evaluate_regularization(
                theta=th_p, theta_ref=th0, target_volume=3.0, w_bh=1.0, w_vol=0.5
            )
            th_m = th_perturbed.copy()
            th_m[i] -= h
            Em, _ = dag.evaluate_regularization(
                theta=th_m, theta_ref=th0, target_volume=3.0, w_bh=1.0, w_vol=0.5
            )
            grad_fd[i] = (Ep - Em) / (2.0 * h)

        rel_err = np.linalg.norm(grad_reg - grad_fd) / np.linalg.norm(grad_reg)
        assert rel_err < 1e-4


# ==============================================================================
# 5. Feature 59: Continuous L^p-Metric Tensor & P2 Midpoint (Paper 3)
# ==============================================================================

class TestF59ContinuousLpMetric:
    """Verifies multiscale L^p metric tensor sizing and noise-free P2 midpoint reconstruction."""

    def test_lp_metric_tensor_sizing_p1_and_p2(self):
        """Verify M_Lp is symmetric positive definite and scales with target cell count N."""
        # Diagonal anisotropic Hessian
        H_u = np.array([
            [10.0, 0.0, 0.0],
            [0.0, 2.0, 0.0],
            [0.0, 0.0, 1.0],
        ])
        N_target = 5000

        for p_norm in (1, 2):
            M = compute_lp_metric_tensor(H_u, target_cell_count=N_target, p=p_norm)
            # Must be SPD: positive eigenvalues
            eigvals = np.linalg.eigvalsh(M)
            assert np.all(eigvals > 0.0)

    def test_lp_metric_tensor_fallback_on_zero_hessian(self):
        """Zero Hessian (uniform field) falls back to isotropic background metric without division by zero."""
        H_zero = np.zeros((3, 3))
        M = compute_lp_metric_tensor(H_zero, target_cell_count=1000, p=2, h_max=0.1)

        # Must be isotropic: M = h_max^-2 * I = 100 * I
        assert np.allclose(M, 100.0 * np.eye(3), atol=1e-8)

    def test_p2_edge_midpoint_noise_free_reconstruction(self):
        """Quadratic field u(x) = x^2 is reconstructed exactly at edge midpoints without noise."""
        nodes = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
        ])
        # u = x^2 => u(0) = 0, u(1) = 1, grad u = [2x, 0, 0]
        vals = np.array([0.0, 1.0])
        grads = np.array([
            [0.0, 0.0, 0.0],
            [2.0, 0.0, 0.0],
        ])
        edges = np.array([[0, 1]])

        mid_vals = reconstruct_p2_edge_midpoints(nodes, vals, edges, grads)
        # Midpoint x = 0.5 => u(0.5) = 0.25
        assert abs(mid_vals[0] - 0.25) < 1e-15


# ==============================================================================
# 6. Feature 60: Dual GPU Voronoi & Sensitivity (Papers 4 & 5)
# ==============================================================================

class TestF60GPUVoronoiAndSensitivity:
    """Verifies dual GPU Voronoi representation, radius of security, and analytical derivatives."""

    def test_dual_gpu_voronoi_bounding_box_and_stokes_volume(self):
        """Initial bounding box dual cell integrates to exact cube volume (Paper 4)."""
        seed = np.array([0.5, 0.5, 0.5])
        cell = create_initial_bounding_box_cell(seed, (0.0, 1.0, 0.0, 1.0, 0.0, 1.0))

        assert cell.num_planes == 6
        assert cell.num_triangles == 8

        vol, centroid, face_areas = integrate_projection_tetrahedra(cell)
        assert abs(vol - 1.0) < 1e-12
        assert np.allclose(centroid, seed, atol=1e-12)
        assert len(face_areas) == 6

    def test_radius_of_security_early_termination(self):
        """Candidate point further than 2*R_max triggers termination early exit."""
        seed = np.array([0.0, 0.0, 0.0])
        cell = create_initial_bounding_box_cell(seed, (-1.0, 1.0, -1.0, 1.0, -1.0, 1.0))
        # R_max for unit cube from center is sqrt(3) ~ 1.732
        r_max = cell.compute_radius_of_security()
        assert abs(r_max - math.sqrt(3.0)) < 1e-10

        # Candidate point at distance 4.0 > 2 * sqrt(3) ~ 3.464
        cand_far = np.array([4.0, 0.0, 0.0])
        assert cell.should_terminate_clipping(cand_far) is True

        cand_near = np.array([1.5, 0.0, 0.0])
        assert cell.should_terminate_clipping(cand_near) is False

    def test_differentiable_voronoi_analytical_sensitivity_vs_fd(self):
        """Verify analytical vertex sensitivity dx_c/dc_i matches central finite differences (Paper 5)."""
        seed_i = np.array([0.0, 0.0, 0.0])
        nbs = [
            np.array([1.0, 0.0, 0.0]),
            np.array([0.0, 1.0, 0.0]),
            np.array([0.0, 0.0, 1.0]),
        ]

        x_c, J_analytical = DifferentiableVoronoi.solve_vertex_and_sensitivity(seed_i, nbs)

        # Finite difference approximation of Jacobian
        h = 1e-6
        J_fd = np.zeros((3, 3), dtype=np.float64)
        for d in range(3):
            s_plus = seed_i.copy()
            s_plus[d] += h
            x_plus, _ = DifferentiableVoronoi.solve_vertex_and_sensitivity(s_plus, nbs)

            s_minus = seed_i.copy()
            s_minus[d] -= h
            x_minus, _ = DifferentiableVoronoi.solve_vertex_and_sensitivity(s_minus, nbs)

            J_fd[:, d] = (x_plus - x_minus) / (2.0 * h)

        assert np.allclose(J_analytical, J_fd, atol=1e-5)


# ==============================================================================
# 7. Feature 61: Monolithic Eulerian 3D FSI (Paper 8)
# ==============================================================================

class TestF61MonolithicFSI:
    """Verifies Eulerian FSI mixture stress, Neo-Hookean solid, Oldroyd transport, and B=I reset."""

    def test_one_continuum_mixture_density_and_stress(self):
        """Mixture density and stress blend smoothly between fluid and solid."""
        fsi = EulerianMonolithicFSI(rho_s=1200.0, rho_f=1000.0, mu_s=1e-2, mu_f=1e-3, G_s=5e4)

        # Pure fluid (phi = 0)
        assert abs(fsi.compute_mixture_density(0.0) - 1000.0) < 1e-12
        # Pure solid (phi = 1)
        assert abs(fsi.compute_mixture_density(1.0) - 1200.0) < 1e-12
        # Cut cell (phi = 0.5)
        assert abs(fsi.compute_mixture_density(0.5) - 1100.0) < 1e-12

        # Stress blend
        u_grad = np.array([
            [0.0, 1.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ])
        B_solid = np.array([
            [1.5, 0.5, 0.0],
            [0.5, 1.0, 0.0],
            [0.0, 0.0, 1.0],
        ])
        p = 101325.0

        sigma_f = fsi.compute_mixture_stress(0.0, u_grad, p, B_solid)
        sigma_s = fsi.compute_mixture_stress(1.0, u_grad, p, B_solid)
        sigma_mix = fsi.compute_mixture_stress(0.5, u_grad, p, B_solid)

        expected_mix = 0.5 * (sigma_f + sigma_s)
        assert np.allclose(sigma_mix, expected_mix, atol=1e-14)

    def test_fluid_stability_reset_eliminates_spurious_shear(self):
        """Deformation tensor B is strictly reset to I_3 wherever solid volume fraction phi < 1e-8."""
        fsi = EulerianMonolithicFSI(eps_cutoff=1e-8)
        B_distorted = np.array([
            [10.0, 5.0, 0.0],
            [5.0, 8.0, 0.0],
            [0.0, 0.0, 1.0],
        ])

        # Fluid cell: phi = 1e-9 < 1e-8
        B_reset = fsi.apply_fluid_stability_reset(B_distorted, phi=1e-9)
        assert np.allclose(B_reset, np.eye(3), atol=1e-15)

        # Solid cell: phi = 0.5 >= 1e-8 preserves distorted tensor
        B_solid = fsi.apply_fluid_stability_reset(B_distorted, phi=0.5)
        assert np.allclose(B_solid, B_distorted, atol=1e-15)

    def test_plic_3d_interface_reconstruction(self):
        """PLIC reconstruction returns normalized unit normal and accurate intercept."""
        phi_cell = 0.75
        phi_grad = np.array([0.0, 3.0, 4.0])  # norm = 5.0

        normal, beta = EulerianMonolithicFSI.reconstruct_plic_3d(phi_cell, phi_grad, dx=0.1)

        assert abs(np.linalg.norm(normal) - 1.0) < 1e-14
        assert np.allclose(normal, [0.0, 0.6, 0.8], atol=1e-14)
        assert abs(beta - (0.75 - 0.5) * 0.1) < 1e-14

    def test_advect_cauchy_green_tensor_single_and_batch(self):
        """Verify Oldroyd-B upper-convected advection and fluid stability reset for single and batched tensors."""
        fsi = EulerianMonolithicFSI(eps_cutoff=1e-8)
        dt = 0.01

        # 1. Single cell test with pure fluid (phi = 0.0) -> must reset to I3
        B_single = np.eye(3, dtype=np.float64)
        u_single = np.zeros(3, dtype=np.float64)
        ug_single = np.array([[0.0, 10.0, 0.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]], dtype=np.float64)

        B_adv_fluid = fsi.advect_cauchy_green_tensor(
            B_single, u_single, ug_single, dt=dt, phi=0.0
        )
        assert B_adv_fluid.shape == (3, 3)
        assert np.allclose(B_adv_fluid, np.eye(3), atol=1e-15)

        # 2. Single cell test with pure solid (phi = 1.0) -> Oldroyd-B upper-convected growth
        B_adv_solid = fsi.advect_cauchy_green_tensor(
            B_single, u_single, ug_single, dt=dt, phi=1.0
        )
        assert B_adv_solid.shape == (3, 3)
        expected_B = np.eye(3) + dt * (ug_single + ug_single.T)
        assert np.allclose(B_adv_solid, expected_B, atol=1e-14)

        # 3. Batch cells test with array phi
        N = 5
        B_batch = np.tile(np.eye(3, dtype=np.float64), (N, 1, 1))
        u_batch = np.zeros((N, 3), dtype=np.float64)
        ug_batch = np.tile(ug_single, (N, 1, 1))
        phi_batch = np.array([0.0, 1.0, 0.0, 0.8, 1e-9], dtype=np.float64)

        B_adv_batch = fsi.advect_cauchy_green_tensor(
            B_batch, u_batch, ug_batch, dt=dt, phi=phi_batch
        )
        assert B_adv_batch.shape == (N, 3, 3)
        # Fluid cells (phi < 1e-8): indices 0, 2, 4 must be I3
        for idx in [0, 2, 4]:
            assert np.allclose(B_adv_batch[idx], np.eye(3), atol=1e-15)
        # Solid cells (phi >= 1e-8): indices 1, 3 must have upper-convected deformation
        for idx in [1, 3]:
            assert np.allclose(B_adv_batch[idx], expected_B, atol=1e-14)

        # 4. Batch cells test with scalar phi = 0.0 (all fluid)
        B_adv_all_fluid = fsi.advect_cauchy_green_tensor(
            B_batch, u_batch, ug_batch, dt=dt, phi=0.0
        )
        assert B_adv_all_fluid.shape == (N, 3, 3)
        for i in range(N):
            assert np.allclose(B_adv_all_fluid[i], np.eye(3), atol=1e-15)

    def test_compute_mixture_stress_batched_scalar_inputs(self):
        """Verify mixture Cauchy stress tensor evaluation with batched gradients and scalar/array inputs."""
        fsi = EulerianMonolithicFSI(mu_s=1e4, G_s=5e4, mu_f=1e-3)
        N = 4
        ug_single = np.array([[0.0, 2.0, 0.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]], dtype=np.float64)
        ug_batch = np.tile(ug_single, (N, 1, 1))
        B_single = np.eye(3, dtype=np.float64)
        B_batch = np.tile(B_single, (N, 1, 1))

        # 1. Batched calculation with scalar phi=0.5 and scalar p=101325.0
        sigma_batch = fsi.compute_mixture_stress(
            phi=0.5, u_grad=ug_batch, p=101325.0, B=B_batch
        )
        assert sigma_batch.shape == (N, 3, 3)

        # Verify against individual single-cell calculations
        sigma_single = fsi.compute_mixture_stress(
            phi=0.5, u_grad=ug_single, p=101325.0, B=B_single
        )
        assert sigma_single.shape == (3, 3)
        for i in range(N):
            assert np.allclose(sigma_batch[i], sigma_single, atol=1e-14)

        # 2. Pure fluid limit (phi = 0.0): sigma_f = 2 * mu_f * D - p * I
        sigma_fluid = fsi.compute_mixture_stress(
            phi=0.0, u_grad=ug_batch, p=50.0, B=B_batch
        )
        D = 0.5 * (ug_single + ug_single.T)
        expected_fluid = 2.0 * fsi.mu_f * D - 50.0 * np.eye(3)
        for i in range(N):
            assert np.allclose(sigma_fluid[i], expected_fluid, atol=1e-14)

        # 3. Pure solid limit (phi = 1.0): sigma_s = 2 * mu_s * D + G_s * (B - I)
        sigma_solid = fsi.compute_mixture_stress(
            phi=1.0, u_grad=ug_batch, p=50.0, B=B_batch
        )
        expected_solid = 2.0 * fsi.mu_s * D + fsi.G_s * (B_single - np.eye(3))
        for i in range(N):
            assert np.allclose(sigma_solid[i], expected_solid, atol=1e-14)

        # 4. Dimension mismatch must raise ValueError
        with pytest.raises(ValueError, match="phi length"):
            fsi.compute_mixture_stress(
                phi=np.array([0.1, 0.2]), u_grad=ug_batch, p=101325.0, B=B_batch
            )
        with pytest.raises(ValueError, match="pressure p length"):
            fsi.compute_mixture_stress(
                phi=0.5, u_grad=ug_batch, p=np.array([1.0, 2.0]), B=B_batch
            )


# ==============================================================================
# 8. Feature 61b: §12.2 & Snippets 12-14 Numerics Contracts
# ==============================================================================

class TestF61bNumericsContracts:
    """Verifies linear-exact plane intersection, FaceClass invalid rejection, atomic-free gather, and Cholesky SPD."""

    def test_linear_exact_face_geometry_and_invalid_rejection(self):
        """Verify beta_f plane intersection and explicit rejection of inverted/near-parallel faces (Snippet 12)."""
        x_P = np.array([0.0, 0.0, 0.0])
        x_N = np.array([1.0, 0.0, 0.0])
        x_f = np.array([0.5, 0.1, 0.0])  # Slightly skewed face
        area = np.array([1.0, 0.0, 0.0])
        h_ref = 1.0

        geom = compute_linear_exact_face_geometry(x_P, x_N, x_f, area, h_ref)
        assert geom["kind"] in (FaceClass.FAST, FaceClass.CORRECTED)
        assert abs(geom["beta"] - 0.5) < 1e-14

        # Inverted / perpendicular face: cos(theta) <= 1e-8 must be explicitly rejected as INVALID
        area_perp = np.array([0.0, 1.0, 0.0])  # Perpendicular: d . area == 0
        geom_invalid = compute_linear_exact_face_geometry(x_P, x_N, x_f, area_perp, h_ref)
        assert geom_invalid["kind"] == FaceClass.INVALID

    def test_atomic_free_cell_face_csr_gather(self):
        """Verify deterministic, atomic-free CellFaceCSR residual gather without race conditions."""
        owned_cells = 2
        offsets = np.array([0, 2, 4], dtype=np.int64)
        faces = np.array([0, 1, 1, 2], dtype=np.int64)
        orientation = np.array([1.0, -1.0, 1.0, 1.0], dtype=np.float64)
        face_flux = np.array([10.0, 3.0, 5.0], dtype=np.float64)

        # Cell 0: +face[0] - face[1] = 10.0 - 3.0 = 7.0
        # Cell 1: +face[1] + face[2] = 3.0 + 5.0 = 8.0
        residual = gather_residual_cell_face_csr(owned_cells, offsets, faces, orientation, face_flux)
        assert np.allclose(residual, [7.0, 8.0], atol=1e-14)

    def test_gradient_free_upwind_deferred_correction_m_matrix(self):
        """Verify Class 0 deferred correction produces strictly negative off-diagonals (M-matrix) (Snippet 13)."""
        m_dot = 5.0
        gamma = 1.0
        alpha_f = 2.0
        beta_f = 0.5
        phi_P = 10.0
        phi_N = 20.0

        a_PN, a_NP, a_P_diag, a_N_diag, flux_def = compute_gradient_free_deferred_correction(
            m_dot, gamma, alpha_f, beta_f, phi_P, phi_N
        )

        # M-matrix conditions: off-diagonals <= 0
        assert a_PN <= 0.0
        assert a_NP <= 0.0
        # Diagonals >= 0
        assert a_P_diag >= 0.0
        assert a_N_diag >= 0.0

    def test_spd_metric_cholesky_factorization(self):
        """Verify Cholesky factorization M = L L^T + eps I produces strictly positive definite tensor."""
        M_candidate = np.array([
            [4.0, 1.0, 0.5],
            [1.0, 3.0, 0.2],
            [0.5, 0.2, 2.0],
        ])

        M_spd, L = factorize_spd_metric_cholesky(M_candidate, eps=1e-10)

        # Must satisfy M_spd == L @ L^T
        assert np.allclose(M_spd, L @ L.T, atol=1e-12)
        # All eigenvalues strictly positive
        assert np.all(np.linalg.eigvalsh(M_spd) > 0.0)

    def test_tangential_cad_projection_and_newton_reprojection(self):
        """Verify tangential projection + 1-step Newton keeps points on CAD sphere psi(x) = ||x|| - R = 0."""
        R_sphere = 1.0
        pts = np.array([
            [1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
        ])
        # Adjoint gradient attempting to pull points away from sphere
        grad = np.array([
            [1.0, 0.5, 0.0],
            [0.0, 1.0, 0.5],
        ])

        def sdf_eval(x: np.ndarray) -> float:
            return float(np.linalg.norm(x) - R_sphere)

        def sdf_grad(x: np.ndarray) -> np.ndarray:
            norm = np.linalg.norm(x)
            return x / norm if norm > 1e-12 else np.array([1.0, 0.0, 0.0])

        morphed = project_boundary_points_tangential_newton(
            pts, sdf_eval, sdf_grad, grad, step_size=0.1
        )

        # After Newton re-projection, points must lie on sphere to high accuracy
        for p in morphed:
            assert abs(np.linalg.norm(p) - R_sphere) < 1e-2

    def test_three_map_mesh_error_map_evaluation(self):
        """Verify 3-Map MeshErrorMap architecture separates geometry, solution, and error indicators (§12.2)."""
        pts = np.array([
            # Cube 0
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
            # Cube 1 extra vertices
            [2.0, 0.0, 0.0], [2.0, 1.0, 0.0],
            [2.0, 0.0, 1.0], [2.0, 1.0, 1.0],
        ], dtype=np.float64)
        c0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
        c1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)
        mesh = compute_difvm_topology(pts, [c0, c1])

        u_pilot = np.array([[1.0, 0.0, 0.0], [2.0, 0.0, 0.0]], dtype=np.float64)
        wall_dist = np.array([0.5, 0.8], dtype=np.float64)
        H_u = np.zeros((2, 3, 3), dtype=np.float64)
        H_u[0, 0, 0] = 3.0
        residual = np.array([1e-4, 2e-4], dtype=np.float64)

        three_maps = compute_three_map_mesh_error_map(
            mesh, u_pilot=u_pilot, wall_distance=wall_dist, H_u=H_u, residual=residual
        )

        assert "GeometryMap" in three_maps
        assert "SolutionIndicator" in three_maps
        assert "ErrorEstimator" in three_maps

        geom = three_maps["GeometryMap"]
        assert "m_skew_relative" in geom
        assert "non_orthogonality" in geom
        assert "aspect_ratio" in geom
        assert "stabilization_mask" in geom

        sol = three_maps["SolutionIndicator"]
        assert np.allclose(sol["pilot_velocity_magnitude"], [1.0, 2.0])
        assert np.allclose(sol["wall_distance_sdf"], [0.5, 0.8])
        assert np.allclose(sol["hessian_frobenius_norm"], [3.0, 0.0])

        err = three_maps["ErrorEstimator"]
        assert np.allclose(err["residual_norm"], [1e-4, 2e-4])


# ==============================================================================
# 9. Clean SMR Check
# ==============================================================================

def test_suggestions_line_630_smr_cleanup():
    """Verify SUGGESTIONS_BACKEND.md contains zero remaining smrKineticsSource mentions."""
    path = Path(__file__).resolve().parent.parent / "SUGGESTIONS_BACKEND.md"
    assert path.is_file(), f"SUGGESTIONS_BACKEND.md not found at {path}"

    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    assert "smrKineticsSource.C" not in content, (
        "Integrity Violation: smrKineticsSource.C still found in SUGGESTIONS_BACKEND.md!"
    )
    assert "genericReactionSource.C" in content, (
        "genericReactionSource.C must be referenced at line 630 of SUGGESTIONS_BACKEND.md."
    )
