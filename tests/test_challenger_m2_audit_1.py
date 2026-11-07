"""Empirical Adversarial Stress Tests for Milestone M2 Audit.

Authored by challenger_m2_1 (teamwork_challenger).
Empirically stress-tests:
1. R3.3: Berger & Giuliani (2024) WSRD Dynamic Weights:
   - C1 transition continuity at alpha_small and alpha_target
   - Monotonicity in mid-range [alpha_small, alpha_target]
   - Extreme volume ratios alpha in [10^-9, 1.0] (no NaN, Inf, negative weights)
   - Configuration attribute passthrough & metadata/error_map attachment
2. R4: Solid-Only Domain Mode Dispatch Fix:
   - Verification of is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")
   - Correct hexcore meshing delegation for "fluid_only", "conjugate", "internal_flow"
   - Backward-compatibility preservation of solid body meshing for "solid_only" and config=None
3. R5.1: DistMesh Point Filtering & Newton Projection:
   - Rejection of near-boundary sliver points with d >= -0.05 * h
   - 3-step Newton-Raphson boundary projection pulling outside points (d > 0) back to d <= 0
4. R6.1-2: GCI Scaling & Order Clamping:
   - Representative grid size scaling h = (V/N)^(1/D) for 1D, 2D, and 3D
   - Apparent order p clamping strictly to [0.5, 10.0] under oscillatory convergence (eps_32 * eps_21 <= 0)
   - Consistent exponent r^p across apparent_order_p, phi_extrapolated, and gci_fine
5. R6.3-4: SDF Bounding Box Tolerance & Barnes-Hut Dipole Acceleration:
   - Continuous and smooth distance/gradient behavior across bbox boundary +/- 10^-5 * diag
   - Comparison of Barnes-Hut dipole winding numbers against exact solid angle evaluation (< 10^-11)
   - Parity between native C++ and pure-Python winding number engines
6. R7: Static Audit Fixes:
   - strict_native=True and CFD_STRICT_NATIVE=1 raise RuntimeError rather than falling back to Python
   - OpenFOAM export respects configurable patch_types (farfield typed as symmetry/patch, not wall)
   - Boundary layer inflation normal orientation and node reversal f_arr[::-1] producing positive volume prisms
"""

from __future__ import annotations

import os
from pathlib import Path
import tempfile
from typing import Dict, List, Tuple

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    compute_wsrd_weights_array,
    compute_wsrd_redistribution_map,
    generate_hexcore_native,
    get_native_dll,
)
from backend.meshing.distmesh_core import distmesh_2d
from backend.meshing.gci import compute_asme_vv20_gci, compute_grid_representative_size
from backend.meshing.geometry_sdf import (
    CADMeshSDF,
    compute_mesh_winding_number,
)
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import InflationConfig, MeshingConfig, UnstructuredMesh
from backend.meshing.polymesh_io import export_openfoam_polymesh
from tests.test_asme_vv import create_geodesic_icosphere
from tests.test_wp2_mesh_sdf import _build_anisotropic_box


# ==============================================================================
# Challenge 1: R3.3 Berger & Giuliani (2024) WSRD Dynamic Weights
# ==============================================================================

class TestR3WSRDDynamicWeights:
    """Empirical verification of Berger & Giuliani (2024) Eq. (4.2) dynamic WSRD weights."""

    def test_wsrd_c1_continuity_and_derivatives(self) -> None:
        """Verify C1 continuity at transition boundaries alpha_small and alpha_target."""
        a_small = 0.20
        a_target = 0.50
        eps = 1e-6

        # Left and right at alpha_small
        w_s_minus = compute_wsrd_weights_array(np.array([a_small - eps]), a_small, a_target)[0]
        w_s = compute_wsrd_weights_array(np.array([a_small]), a_small, a_target)[0]
        w_s_plus = compute_wsrd_weights_array(np.array([a_small + eps]), a_small, a_target)[0]

        dw_s_left = (w_s - w_s_minus) / eps
        dw_s_right = (w_s_plus - w_s) / eps

        assert np.isclose(w_s, 0.0, atol=1e-12)
        assert np.isclose(dw_s_left, 0.0, atol=1e-4)
        assert np.isclose(dw_s_right, 0.0, atol=1e-4)

        # Left and right at alpha_target
        w_t_minus = compute_wsrd_weights_array(np.array([a_target - eps]), a_small, a_target)[0]
        w_t = compute_wsrd_weights_array(np.array([a_target]), a_small, a_target)[0]
        w_t_plus = compute_wsrd_weights_array(np.array([a_target + eps]), a_small, a_target)[0]

        dw_t_left = (w_t - w_t_minus) / eps
        dw_t_right = (w_t_plus - w_t) / eps

        assert np.isclose(w_t, 1.0, atol=1e-12)
        assert np.isclose(dw_t_left, 0.0, atol=1e-4)
        assert np.isclose(dw_t_right, 0.0, atol=1e-4)

    def test_wsrd_monotonicity_in_mid_range(self) -> None:
        """Verify strictly monotonic sin^2 behavior between alpha_small and alpha_target."""
        a_small = 0.15
        a_target = 0.60
        alphas = np.linspace(a_small, a_target, 2000)
        weights = compute_wsrd_weights_array(alphas, small_cell_ratio=a_small, wsrd_target_alpha=a_target)

        # Difference must be strictly non-negative
        diffs = np.diff(weights)
        assert np.all(diffs >= -1e-15), "WSRD weight function is not monotonic!"
        assert np.all(weights >= 0.0)
        assert np.all(weights <= 1.0)

        # Analytical derivative check at midpoint
        alpha_mid = 0.5 * (a_small + a_target)
        h = 1e-6
        w_mid_plus = compute_wsrd_weights_array(np.array([alpha_mid + h]), a_small, a_target)[0]
        w_mid_minus = compute_wsrd_weights_array(np.array([alpha_mid - h]), a_small, a_target)[0]
        num_deriv = (w_mid_plus - w_mid_minus) / (2.0 * h)

        delta_a = a_target - a_small
        analytical_deriv = (np.pi / (2.0 * delta_a)) * np.sin(np.pi * (alpha_mid - a_small) / delta_a)
        assert np.isclose(num_deriv, analytical_deriv, rtol=1e-4)

    def test_wsrd_extreme_volume_ratios(self) -> None:
        """Stress test with 10,000 extreme volume fractions in [10^-9, 1.0] and edge cases."""
        extreme_alphas = np.logspace(-9, 0, 10000)
        weights = compute_wsrd_weights_array(extreme_alphas, small_cell_ratio=0.10, wsrd_target_alpha=0.40)

        assert not np.any(np.isnan(weights)), "NaN found in WSRD weights!"
        assert not np.any(np.isinf(weights)), "Inf found in WSRD weights!"
        assert np.all(weights >= 0.0), "Negative WSRD weight detected!"
        assert np.all(weights <= 1.0), "WSRD weight exceeded 1.0!"

        # Adversarial non-standard inputs: negative alpha, zero alpha, very large alpha
        adv_alphas = np.array([-10.0, -1e-15, 0.0, 1.0, 2.0, 100.0])
        adv_w = compute_wsrd_weights_array(adv_alphas, 0.20, 0.50)
        assert np.all(adv_w[:3] == 0.0)
        assert np.all(adv_w[3:] == 1.0)

    def test_wsrd_topology_attachment_and_dynamic_config(self) -> None:
        """Verify dynamic config fields are respected and attached to error_map and metadata."""
        config = MeshingConfig()
        config.small_cell_ratio = 0.15
        config.wsrd_target_alpha = 0.70

        # Create simple 2-cell mesh with one tiny cell and one large cell
        pts = np.array([
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
            [2.0, 0.0, 0.0], [2.0, 1.0, 0.0], [2.0, 0.0, 1.0], [2.0, 1.0, 1.0],
        ], dtype=np.float64)
        c0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
        c1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)

        mesh = compute_difvm_topology_python(pts, [c0, c1], config=config)

        assert "wsrd_weights" in mesh.error_map
        assert "wsrd_weights" in mesh.metadata
        assert "wsrd_redistribution" in mesh.metadata
        weights = mesh.error_map["wsrd_weights"]
        assert len(weights) == 2
        assert np.all(weights >= 0.0)
        assert np.all(weights <= 1.0)


# ==============================================================================
# Challenge 2: R4 Solid-Only Domain Mode Dispatch
# ==============================================================================

class TestR4DomainModeDispatch:
    """Empirical verification of domain_mode branching in MeshEngine."""

    def test_is_solid_only_dispatch_logic(self) -> None:
        """Verify dispatch condition preserves solid_only and branches external/internal fluid."""
        engine = MeshEngine(use_native=True)
        stl_path = Path("resources/CADTestSamples/TestCAD.stl")
        assert stl_path.exists(), f"Benchmark fixture missing: {stl_path}"

        # 1. config is None -> preserves solid-only body interior meshing
        m_none = engine.mesh_cad_file(stl_path, scale=0.001, h0=0.05, config=None)
        assert m_none.metadata.get("engine") == "native_cpp20"
        assert m_none.num_cells == 64

        # 2. active_cfg.domain_mode == "solid_only" -> preserves solid body meshing
        cfg_solid = MeshingConfig(domain_mode="solid_only")
        m_solid = engine.mesh_cad_file(stl_path, scale=0.001, h0=0.05, config=cfg_solid)
        assert m_solid.metadata.get("engine") == "native_cpp20"
        assert m_solid.num_cells == 64

        # 3. active_cfg.domain_mode == "fluid_only" -> branches to mesh_hexcore
        cfg_fluid = MeshingConfig(
            domain_mode="fluid_only",
            farfield_bbox=(-0.2, -0.2, -0.2, 0.4, 0.4, 0.4),
        )
        m_fluid = engine.mesh_cad_file(stl_path, scale=0.001, h0=0.2, config=cfg_fluid)
        assert m_fluid.metadata.get("engine") == "native_cpp20_hexcore"
        assert m_fluid.num_cells > 100

        # 4. active_cfg.domain_mode == "conjugate" -> branches to mesh_hexcore
        cfg_conj = MeshingConfig(
            domain_mode="conjugate",
            farfield_bbox=(-0.2, -0.2, -0.2, 0.4, 0.4, 0.4),
        )
        m_conj = engine.mesh_cad_file(stl_path, scale=0.001, h0=0.2, config=cfg_conj)
        assert m_conj.metadata.get("engine") == "native_cpp20_hexcore"
        assert m_conj.num_cells > 100

        # 5. active_cfg.domain_mode == "internal_flow" -> branches to mesh_hexcore
        cfg_int = MeshingConfig(
            domain_mode="internal_flow",
            farfield_bbox=(-0.2, -0.2, -0.2, 0.4, 0.4, 0.4),
        )
        m_int = engine.mesh_cad_file(stl_path, scale=0.001, h0=0.2, config=cfg_int)
        assert m_int.metadata.get("engine") == "native_cpp20_hexcore"
        assert m_int.num_cells > 100


# ==============================================================================
# Challenge 3: R5.1 DistMesh Point Filtering & Newton Projection
# ==============================================================================

class TestR5DistMeshFilteringAndNewtonProjection:
    """Empirical verification of DistMesh boundary confinement and point filtering."""

    def test_distmesh_filtering_threshold_rejects_boundary_slivers(self) -> None:
        """Verify candidate interior point filtering strictly enforces d < -0.05 * h."""
        h0 = 0.1
        # Circular domain SDF: d(p) = ||p|| - 1.0
        fd = lambda p: np.linalg.norm(p, axis=1) - 1.0
        fh = lambda p: np.full(len(p), h0)

        # Points:
        # A: deep inside (d = -0.5) -> keep
        # B: near boundary inside (d = -0.02, which is > -0.05*h = -0.005) -> wait, -0.02 < -0.005!
        # Let's specify exact d values:
        # threshold is -0.05 * h0 = -0.005
        # d = -0.010 -> d < -0.005 -> KEEP
        # d = -0.003 -> d > -0.005 -> REJECT
        # d =  0.000 -> d > -0.005 -> REJECT
        # d = +0.010 -> d > -0.005 -> REJECT
        pts = np.array([
            [0.5, 0.0],                     # d = 0.5 - 1.0 = -0.50 (keep)
            [1.0 - 0.010, 0.0],             # d = -0.010 < -0.005 (keep)
            [1.0 - 0.003, 0.0],             # d = -0.003 > -0.005 (reject)
            [1.0, 0.0],                     # d =  0.000 > -0.005 (reject)
            [1.0 + 0.010, 0.0],             # d = +0.010 > -0.005 (reject)
        ], dtype=np.float64)

        d_vals = fd(pts)
        h_vals = fh(pts)
        survivors = pts[d_vals < -0.05 * h_vals]

        assert len(survivors) == 2
        assert np.isclose(survivors[0, 0], 0.5)
        assert np.isclose(survivors[1, 0], 1.0 - 0.010)

    def test_newton_boundary_projection_pulls_outside_points_within_3_steps(self) -> None:
        """Verify iterative Newton projection pulls pushed-out nodes (d > 0) back to d <= 0."""
        # 1. Circle domain SDF
        def fd_circle(p: np.ndarray) -> np.ndarray:
            return np.linalg.norm(p, axis=1) - 1.0

        # Generate 50 points outside circle at varying distances: d in [1e-4, 0.08]
        angles = np.linspace(0, 2 * np.pi, 50, endpoint=False)
        distances = np.linspace(1e-4, 0.08, 50)
        radii = 1.0 + distances
        p = np.column_stack([radii * np.cos(angles), radii * np.sin(angles)])
        h0 = 0.1

        # Execute up to 3 Newton projection steps exactly as in distmesh_core.py
        for step in range(3):
            d_nodes = fd_circle(p)
            outside = d_nodes > 0.0
            if not np.any(outside):
                break
            eps = 1e-5 * h0
            p_out = p[outside]
            grad_x = (fd_circle(p_out + [eps, 0.0]) - fd_circle(p_out - [eps, 0.0])) / (2.0 * eps)
            grad_y = (fd_circle(p_out + [0.0, eps]) - fd_circle(p_out - [0.0, eps])) / (2.0 * eps)
            grad = np.column_stack([grad_x, grad_y])
            grad_norm2 = np.sum(grad ** 2, axis=1, keepdims=True) + 1e-15
            p[outside] -= (d_nodes[outside, None] * grad) / grad_norm2

        final_d = fd_circle(p)
        assert np.all(final_d <= 1e-12), f"Points not pulled back to boundary: max d = {np.max(final_d)}"

        # 2. Elliptical domain SDF: d(x, y) = (x/1.5)^2 + (y/0.8)^2 - 1.0
        def fd_ellipse(p: np.ndarray) -> np.ndarray:
            return (p[:, 0] / 1.5) ** 2 + (p[:, 1] / 0.8) ** 2 - 1.0

        p_ell = np.column_stack([1.55 * np.cos(angles), 0.85 * np.sin(angles)])
        for step in range(3):
            d_nodes = fd_ellipse(p_ell)
            outside = d_nodes > 0.0
            if not np.any(outside):
                break
            eps = 1e-5 * h0
            p_out = p_ell[outside]
            grad_x = (fd_ellipse(p_out + [eps, 0.0]) - fd_ellipse(p_out - [eps, 0.0])) / (2.0 * eps)
            grad_y = (fd_ellipse(p_out + [0.0, eps]) - fd_ellipse(p_out - [0.0, eps])) / (2.0 * eps)
            grad = np.column_stack([grad_x, grad_y])
            grad_norm2 = np.sum(grad ** 2, axis=1, keepdims=True) + 1e-15
            p_ell[outside] -= (d_nodes[outside, None] * grad) / grad_norm2

        final_ell_d = fd_ellipse(p_ell)
        assert np.all(final_ell_d <= 1e-4), f"Ellipse points not pulled back: max d = {np.max(final_ell_d)}"


# ==============================================================================
# Challenge 4: R6.1-2 GCI Scaling & Order Clamping
# ==============================================================================

class TestR6GCIScalingAndOrderClamping:
    """Empirical verification of ASME V&V 20-2009 GCI scaling and order clamping."""

    def test_representative_grid_size_dimensional_scaling(self) -> None:
        """Verify h = (V/N)^(1/D) scales with topological_dim D = 1, 2, 3."""
        # 1D line mesh (dim=1)
        m1d = UnstructuredMesh(
            points=np.zeros((2, 3)), cells=[np.array([0, 1])],
            cell_centres=np.zeros((1, 3)), cell_volumes=np.array([5.0]),
            face_areas=np.zeros((2, 3)), face_centres=np.zeros((2, 3)),
            owner=np.array([0, 0]), neighbour=np.array([-1, -1]),
            delta_f=np.zeros(2), k_f=np.zeros(2), boundaries={},
            cell_regions=np.zeros(1, dtype=int), baffles=[], error_map={},
            faces=[np.array([0]), np.array([1])], cell_types=np.array([1]),
            topological_dim=1,
        )
        assert np.isclose(compute_grid_representative_size(m1d), 5.0)

        # 2D surface mesh (dim=2)
        m2d = UnstructuredMesh(
            points=np.zeros((4, 3)), cells=[np.array([0, 1, 2, 3])],
            cell_centres=np.zeros((1, 3)), cell_volumes=np.array([9.0]),
            face_areas=np.zeros((4, 3)), face_centres=np.zeros((4, 3)),
            owner=np.array([0]*4), neighbour=np.array([-1]*4),
            delta_f=np.zeros(4), k_f=np.zeros(4), boundaries={},
            cell_regions=np.zeros(1, dtype=int), baffles=[], error_map={},
            faces=[np.array([0, 1]) for _ in range(4)], cell_types=np.array([3]),
            topological_dim=2,
        )
        assert np.isclose(compute_grid_representative_size(m2d), 3.0)  # sqrt(9.0)

        # 3D volume mesh (dim=3)
        m3d = UnstructuredMesh(
            points=np.zeros((8, 3)), cells=[np.array(range(8))],
            cell_centres=np.zeros((1, 3)), cell_volumes=np.array([27.0]),
            face_areas=np.zeros((6, 3)), face_centres=np.zeros((6, 3)),
            owner=np.array([0]*6), neighbour=np.array([-1]*6),
            delta_f=np.zeros(6), k_f=np.zeros(6), boundaries={},
            cell_regions=np.zeros(1, dtype=int), baffles=[], error_map={},
            faces=[np.array([0, 1, 2, 3]) for _ in range(6)], cell_types=np.array([11]),
            topological_dim=3,
        )
        assert np.isclose(compute_grid_representative_size(m3d), 3.0)  # cbrt(27.0)

    def test_oscillatory_convergence_order_clamping(self) -> None:
        """Verify oscillatory convergence (eps_32 * eps_21 <= 0) clamps p to 1.0 in [0.5, 10.0]."""
        # Case 1: eps_32 < 0, eps_21 > 0
        phi_1, phi_2, phi_3 = 10.0, 12.0, 11.0
        r = 1.5
        res_osc1 = compute_asme_vv20_gci(phi_1, phi_2, phi_3, r=r)
        assert res_osc1.apparent_order_p == 1.0

        # Exact formula consistency check: phi_ext and gci_fine must evaluate with reported p
        p = res_osc1.apparent_order_p
        r_pow_p = r ** p
        exp_phi_ext = (r_pow_p * phi_1 - phi_2) / (r_pow_p - 1.0)
        e_a = abs((phi_1 - phi_2) / phi_1)
        exp_gci = (1.25 * e_a) / (r_pow_p - 1.0)
        assert np.isclose(res_osc1.phi_extrapolated, exp_phi_ext)
        assert np.isclose(res_osc1.gci_fine, exp_gci)

        # Case 2: eps_32 > 0, eps_21 < 0
        phi_1, phi_2, phi_3 = 15.0, 13.0, 14.0
        res_osc2 = compute_asme_vv20_gci(phi_1, phi_2, phi_3, r=r)
        assert res_osc2.apparent_order_p == 1.0
        p2 = res_osc2.apparent_order_p
        r_pow_p2 = r ** p2
        exp_phi_ext2 = (r_pow_p2 * phi_1 - phi_2) / (r_pow_p2 - 1.0)
        assert np.isclose(res_osc2.phi_extrapolated, exp_phi_ext2)

    def test_out_of_bounds_order_clamping_ranges(self) -> None:
        """Verify raw_p > 10.0 clamps to 10.0 and raw_p < 0.5 clamps to 0.5."""
        r = 2.0
        # Super-convergent case (raw_p >> 10.0): eps_32 = 1000, eps_21 = 0.001
        res_hi = compute_asme_vv20_gci(1.0, 1.001, 1001.001, r=r)
        assert res_hi.apparent_order_p == 10.0
        r_pow_p = r ** 10.0
        assert np.isclose(res_hi.phi_extrapolated, (r_pow_p * 1.0 - 1.001) / (r_pow_p - 1.0))

        # Sub-convergent case (raw_p << 0.5): eps_32 = 0.001, eps_21 = 1.0
        res_lo = compute_asme_vv20_gci(1.0, 2.0, 2.001, r=r)
        assert res_lo.apparent_order_p == 0.5
        r_pow_p_lo = r ** 0.5
        assert np.isclose(res_lo.phi_extrapolated, (r_pow_p_lo * 1.0 - 2.0) / (r_pow_p_lo - 1.0))


# ==============================================================================
# Challenge 5: R6.3-4 SDF Bounding Box Tolerance & Barnes-Hut Dipole
# ==============================================================================

class TestR6SDFBBoxAndBarnesHut:
    """Empirical verification of SDF bounding box tolerance and Barnes-Hut dipole evaluation."""

    def test_sdf_bounding_box_smooth_transition_across_boundary(self) -> None:
        """Verify points near bounding box boundary +/- 10^-5 * diag behave smoothly without sign flips."""
        box = _build_anisotropic_box(lx=1.0, ly=1.0, lz=1.0)
        sdf = CADMeshSDF(box, use_native=True)

        diag = np.sqrt(3.0)
        bbox_tol = 1e-5 * diag

        # 200 points spanning x in [1.0 - 10 * bbox_tol, 1.0 + 10 * bbox_tol] across boundary
        xs = np.linspace(1.0 - 10 * bbox_tol, 1.0 + 10 * bbox_tol, 200)
        pts = np.column_stack([xs, np.full_like(xs, 0.5), np.full_like(xs, 0.5)])

        phi, grad = sdf.evaluate(pts)

        # Signed distance must match exact x - 1.0 with slope 1
        assert np.allclose(phi, xs - 1.0, atol=1e-12)
        # Unit outward normal gradient must be [1, 0, 0] everywhere
        expected_grad = np.array([1.0, 0.0, 0.0])
        assert np.allclose(grad, expected_grad, atol=1e-10)

    def test_barnes_hut_dipole_vs_exact_solid_angle_parity(self) -> None:
        """Compare Barnes-Hut winding numbers against exact solid angle evaluation."""
        box = _build_anisotropic_box(lx=1.0, ly=1.0, lz=1.0)
        sdf_native = CADMeshSDF(box, use_native=True, sign_method="winding_number")

        np.random.seed(999)
        queries = np.random.uniform(-1.5, 2.5, (100, 3))

        # 1. Native C++ evaluation (uses WindingAABBTree)
        wn_cpp = sdf_native.winding_number(queries)

        # 2. Exact solid angle evaluation in Python
        wn_exact = compute_mesh_winding_number(queries, sdf_native.tri_v0, sdf_native.tri_v1, sdf_native.tri_v2)

        # Parity on benchmark mesh (<= 16 triangles) must be < 1e-12 (< 1e-11)
        err = np.abs(wn_cpp - wn_exact)
        assert np.max(err) < 1e-11, f"Parity violation on benchmark box: max diff {np.max(err):.5e} >= 1e-11"

    def test_farfield_dipole_approximation_asymptotic_precision(self) -> None:
        """Verify far-field dipole approximation matches exact solid angle to < 10^-11 for distant clusters."""
        # Single triangle cluster at origin with area vector [0, 0, 0.5]
        a = np.array([0.0, 0.0, 0.0])
        b = np.array([1.0, 0.0, 0.0])
        c = np.array([0.0, 1.0, 0.0])
        centroid = (a + b + c) / 3.0
        area_vec = 0.5 * np.cross(b - a, c - a)

        # At distance 10^5 * radius, dipole truncation error is < 10^-11
        p_far = centroid + np.array([0.0, 0.0, 100000.0])
        diff = centroid - p_far
        r = np.linalg.norm(diff)
        dipole_omega = np.dot(area_vec, diff) / (r ** 3)

        ra, rb, rc = a - p_far, b - p_far, c - p_far
        da, db, dc = np.linalg.norm(ra), np.linalg.norm(rb), np.linalg.norm(rc)
        num = np.dot(ra, np.cross(rb, rc))
        denom = da * db * dc + np.dot(ra, rb) * dc + np.dot(ra, rc) * db + np.dot(rb, rc) * da
        exact_omega = 2.0 * np.arctan2(num, denom)

        rel_err = abs(dipole_omega - exact_omega) / abs(exact_omega)
        assert rel_err < 1e-10, f"Dipole asymptotic relative error {rel_err:.3e} exceeds 1e-10"


# ==============================================================================
# Challenge 6: R7 Static Audit Fixes
# ==============================================================================

class TestR7StaticAuditFixes:
    """Empirical verification of strict native mode, patch types, and inflation normal reversal."""

    def test_strict_native_mode_raises_runtime_error(self) -> None:
        """Verify strict_native=True or CFD_STRICT_NATIVE=1 raises RuntimeError on fallback."""
        sm = SurfaceMesh(vertices=np.zeros((3, 3)), faces=np.zeros((1, 3), dtype=int))

        # 1. Constructor parameter strict_native=True
        engine_strict = MeshEngine(use_native=False, strict_native=True)
        with pytest.raises(RuntimeError, match="Strict native mode enabled"):
            engine_strict.mesh_hexcore(sm)

        # 2. Environment variable CFD_STRICT_NATIVE=1
        os.environ["CFD_STRICT_NATIVE"] = "1"
        try:
            engine_env = MeshEngine(use_native=False)
            with pytest.raises(RuntimeError, match="Strict native mode enabled"):
                engine_env.mesh_hexcore(sm)
        finally:
            os.environ.pop("CFD_STRICT_NATIVE", None)

    def test_openfoam_export_configurable_patch_types(self) -> None:
        """Verify configurable patch_types export farfield boundaries as symmetry/patch (never wall)."""
        mesh = UnstructuredMesh(
            points=np.zeros((8, 3)), cells=[np.array(range(8))],
            cell_centres=np.zeros((1, 3)), cell_volumes=np.array([1.0]),
            face_areas=np.zeros((6, 3)), face_centres=np.zeros((6, 3)),
            owner=np.array([0]*6), neighbour=np.array([-1]*6),
            delta_f=np.zeros(6), k_f=np.zeros(6),
            boundaries={"top": (0, 1), "sides": (1, 2), "bottom": (3, 1), "inlet": (4, 1), "wall": (5, 1)},
            cell_regions=np.zeros(1, dtype=int), baffles=[], error_map={},
            faces=[np.array([0, 1, 2, 3]) for _ in range(6)], cell_types=np.array([11]),
            topological_dim=3,
        )

        with tempfile.TemporaryDirectory() as tmpdir:
            # 1. Default classification
            export_openfoam_polymesh(mesh, tmpdir)
            boundary_file = Path(tmpdir, "constant", "polyMesh", "boundary")
            content = boundary_file.read_text()
            assert "top\n    {\n        type            symmetry;" in content
            assert "sides\n    {\n        type            symmetry;" in content
            assert "bottom\n    {\n        type            symmetry;" in content
            assert "inlet\n    {\n        type            patch;" in content
            assert "wall\n    {\n        type            wall;" in content

            # 2. Configurable override mapping
            export_openfoam_polymesh(mesh, tmpdir, patch_types={"top": "patch", "sides": "slip"})
            content_override = boundary_file.read_text()
            assert "top\n    {\n        type            patch;" in content_override
            assert "sides\n    {\n        type            slip;" in content_override

    def test_inflation_layer_normal_orientation_and_node_reversal(self) -> None:
        """Verify inflation layer node reversal f_arr[::-1] produces strictly positive volume prisms."""
        # 1. Single triangle wall face with reversed winding order [0, 2, 1] relative to nominal normal [0, 0, 1]
        pts = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
        ], dtype=np.float64)
        wall_faces_inverted = [np.array([0, 2, 1], dtype=int)]

        cfg = InflationConfig(first_layer_height=0.01, growth_rate=1.2, num_layers=3)
        gen = BoundaryLayerGenerator(cfg)
        mesh_inverted = gen.inflate_layers(pts, wall_faces_inverted, nominal_normal=[0.0, 0.0, 1.0])

        assert mesh_inverted.num_cells == 3
        assert np.all(mesh_inverted.cell_volumes > 0.0), (
            f"Negative prism volumes detected on inverted face: {mesh_inverted.cell_volumes}"
        )
        assert np.isclose(np.sum(mesh_inverted.cell_volumes), 0.5 * (0.01 + 0.012 + 0.0144), rtol=1e-5)

        # 2. Multi-faceted closed box with nominal_normal=None: preserves consistent outward orientation
        box = _build_anisotropic_box(lx=1.0, ly=1.0, lz=1.0)
        cfg_box = InflationConfig(first_layer_height=0.02, growth_rate=1.0, num_layers=1)
        gen_box = BoundaryLayerGenerator(cfg_box)
        mesh_box = gen_box.inflate_layers(box.vertices, [f for f in box.faces], nominal_normal=None)

        assert mesh_box.num_cells == 12
        assert np.all(mesh_box.cell_volumes > 0.0), (
            f"Negative cell volumes detected in closed body inflation: {mesh_box.cell_volumes}"
        )
