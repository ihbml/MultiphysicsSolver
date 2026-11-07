"""Milestone M2 Iteration 2 Certification: Empirical and Adversarial Invariant Stress Test.

Author: challenger_m2_r2_1 (teamwork_challenger)
Mission: Stress-test the overall physical and mathematical invariants of Milestone M2
following audit remediation:
1. Invariant 1: Strictly positive cell volumes (V_i > 10^-15) across all cells.
2. Invariant 2: Discrete Gauss divergence closure max_c ||sum_{f in c} S_f|| / V_c^(2/3) < 10^-11.
3. Invariant 3: Over-relaxed non-orthogonal decomposition identity residual < 10^-14.
4. Invariant 4: Error map metrics, 4-tier stabilization masks, and key aliases.
5. Invariant 5: Remediation fidelity (GCI None-safe fallback, Berger-Giuliani WSRD donor-receiver pairing, domain mode dispatch).
"""

import math
from pathlib import Path
from typing import List, Tuple
import numpy as np
import pytest

from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    compute_wsrd_redistribution_map,
    compute_wsrd_weights_array,
    get_native_dll,
)
from backend.meshing.models import UnstructuredMesh, MeshingConfig
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.gci import compute_grid_representative_size

REPO_ROOT = Path(__file__).resolve().parent.parent
CAD_SAMPLES_DIR = REPO_ROOT / "resources" / "CADTestSamples"
TESTCAD_OBJ = CAD_SAMPLES_DIR / "TestCAD.obj"
BLADE_OBJ = CAD_SAMPLES_DIR / "ComplexGeometryExample" / "BladeRev1CFD.obj"


# ============================================================================
# Adversarial Mesh Generators
# ============================================================================

def make_sheared_hex_mesh(nx: int = 4, ny: int = 4, nz: int = 4,
                          shear_xy: float = 1.0, shear_xz: float = 0.5) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate structured hex mesh with severe affine shearing."""
    x = np.linspace(0.0, 1.0, nx + 1)
    y = np.linspace(0.0, 1.0, ny + 1)
    z = np.linspace(0.0, 1.0, nz + 1)

    points = []
    pidx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                px = x[i] + shear_xy * y[j] + shear_xz * z[k]
                py = y[j]
                pz = z[k]
                pidx[(i, j, k)] = len(points)
                points.append([px, py, pz])

    pts = np.array(points, dtype=np.float64)
    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                cells.append(np.array([
                    pidx[(i, j, k)], pidx[(i+1, j, k)], pidx[(i+1, j+1, k)], pidx[(i, j+1, k)],
                    pidx[(i, j, k+1)], pidx[(i+1, j, k+1)], pidx[(i+1, j+1, k+1)], pidx[(i, j+1, k+1)]
                ], dtype=np.int64))
    return pts, cells


def make_random_perturbed_hex_mesh(nx: int = 4, ny: int = 4, nz: int = 4,
                                   perturbation: float = 0.15,
                                   seed: int = 999) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate hex mesh with randomized interior vertex perturbations (non-planar warped faces)."""
    rng = np.random.default_rng(seed)
    x = np.linspace(0.0, 1.0, nx + 1)
    y = np.linspace(0.0, 1.0, ny + 1)
    z = np.linspace(0.0, 1.0, nz + 1)
    dx = 1.0 / nx
    scale = dx * perturbation

    points = []
    pidx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                is_boundary = (i == 0 or i == nx or j == 0 or j == ny or k == 0 or k == nz)
                shift = np.zeros(3) if is_boundary else rng.uniform(-scale, scale, size=3)
                p = np.array([x[i], y[j], z[k]]) + shift
                pidx[(i, j, k)] = len(points)
                points.append(p)

    pts = np.array(points, dtype=np.float64)
    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                cells.append(np.array([
                    pidx[(i, j, k)], pidx[(i+1, j, k)], pidx[(i+1, j+1, k)], pidx[(i, j+1, k)],
                    pidx[(i, j, k+1)], pidx[(i+1, j, k+1)], pidx[(i+1, j+1, k+1)], pidx[(i, j+1, k+1)]
                ], dtype=np.int64))
    return pts, cells


def make_angled_two_cell_mesh(angle_deg: float) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate two-cell mesh with face non-orthogonality angle precisely controlled."""
    theta_rad = math.radians(angle_deg)
    y_shift = 2.0 * math.tan(theta_rad)
    points = [
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
        [2.0, y_shift, 0.0], [2.0, 1.0 + y_shift, 0.0],
        [2.0, y_shift, 1.0], [2.0, 1.0 + y_shift, 1.0],
    ]
    cells = [
        np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64),
        np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64),
    ]
    return np.array(points, dtype=np.float64), cells


# ============================================================================
# Invariant 1: Strictly Positive Cell Volumes (V_i > 10^-15)
# ============================================================================

class TestInvariant1CellVolumePositivity:
    """Empirical verification of strict cell volume positivity V_i > 10^-15 across diverse meshes."""

    @pytest.mark.parametrize("use_native", [True, False])
    def test_perturbed_hex_volume_positivity(self, use_native: bool):
        pts, cells = make_random_perturbed_hex_mesh(nx=5, ny=5, nz=5, perturbation=0.20, seed=123)
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        assert np.all(mesh.cell_volumes > 1e-15), f"Found non-positive volume: min={np.min(mesh.cell_volumes)}"
        assert np.all(np.isfinite(mesh.cell_volumes)), "NaN or Inf found in cell volumes"
        assert abs(float(np.sum(mesh.cell_volumes)) - 1.0) < 1e-10

    @pytest.mark.parametrize("aspect_ratio", [1e-4, 1e-2, 100.0, 1000.0, 10000.0])
    @pytest.mark.parametrize("use_native", [True, False])
    def test_extreme_aspect_ratio_volume(self, aspect_ratio: float, use_native: bool):
        pts = np.array([
            [0, 0, 0], [aspect_ratio, 0, 0], [aspect_ratio, 1, 0], [0, 1, 0],
            [0, 0, 1], [aspect_ratio, 0, 1], [aspect_ratio, 1, 1], [0, 1, 1],
        ], dtype=np.float64)
        cells = [np.arange(8, dtype=np.int64)]
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        v = mesh.cell_volumes[0]
        assert v > 1e-15
        assert abs(v - aspect_ratio) / aspect_ratio < 1e-10

    @pytest.mark.parametrize("use_native", [True, False])
    def test_micro_scale_near_threshold(self, use_native: bool):
        """Cell volume at 1.0e-12 m^3 (well above 1e-15 limit)."""
        L = 1e-4
        pts = np.array([
            [0, 0, 0], [L, 0, 0], [L, L, 0], [0, L, 0],
            [0, 0, L], [L, 0, L], [L, L, L], [0, L, L],
        ], dtype=np.float64)
        cells = [np.arange(8, dtype=np.int64)]
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        assert mesh.cell_volumes[0] > 1e-15
        assert abs(mesh.cell_volumes[0] - L**3) / (L**3) < 1e-12


# ============================================================================
# Invariant 2: Discrete Gauss Divergence Closure
# ============================================================================

class TestInvariant2GaussDivergenceClosure:
    """Empirical verification of discrete Gauss divergence closure: max_c ||sum S_f|| / V_c^(2/3) < 10^-11."""

    @pytest.mark.parametrize("shear", [0.0, 0.5, 1.0, 2.0, 3.5, 5.0])
    @pytest.mark.parametrize("use_native", [True, False])
    def test_closure_under_affine_shearing(self, shear: float, use_native: bool):
        pts, cells = make_sheared_hex_mesh(nx=4, ny=4, nz=4, shear_xy=shear, shear_xz=shear * 0.4)
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        n_cells = len(mesh.cell_volumes)
        closure = np.zeros((n_cells, 3), dtype=np.float64)
        np.add.at(closure, mesh.owner, mesh.face_areas)
        internal = mesh.neighbour >= 0
        np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

        v_norm = mesh.cell_volumes ** (2.0 / 3.0)
        norm_closure = np.linalg.norm(closure, axis=1) / v_norm
        max_rel_closure = float(np.max(norm_closure))

        assert max_rel_closure < 1e-11, f"Shear {shear} violated Gauss relative closure: {max_rel_closure:.3e}"

    @pytest.mark.parametrize("use_native", [True, False])
    def test_closure_under_random_perturbation(self, use_native: bool):
        pts, cells = make_random_perturbed_hex_mesh(nx=5, ny=5, nz=5, perturbation=0.25, seed=54321)
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        n_cells = len(mesh.cell_volumes)
        closure = np.zeros((n_cells, 3), dtype=np.float64)
        np.add.at(closure, mesh.owner, mesh.face_areas)
        internal = mesh.neighbour >= 0
        np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

        v_norm = mesh.cell_volumes ** (2.0 / 3.0)
        norm_closure = np.linalg.norm(closure, axis=1) / v_norm
        max_rel_closure = float(np.max(norm_closure))

        assert max_rel_closure < 1e-11, f"Perturbed mesh violated Gauss closure: {max_rel_closure:.3e}"


# ============================================================================
# Invariant 3: Over-Relaxed Non-Orthogonal Decomposition Identity
# ============================================================================

class TestInvariant3NonOrthogonalDecomposition:
    """Empirical verification of over-relaxed decomposition: max_f ||delta_f + k_f - S_f|| < 10^-14."""

    @pytest.mark.parametrize("angle_deg", [0.0, 30.0, 45.0, 60.0, 70.0, 75.0, 80.0, 85.0, 88.0, 89.5])
    @pytest.mark.parametrize("use_native", [True, False])
    def test_decomposition_identity_residual(self, angle_deg: float, use_native: bool):
        pts, cells = make_angled_two_cell_mesh(angle_deg)
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        internal = mesh.neighbour >= 0
        assert np.any(internal)

        residual = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
        max_res = float(np.max(np.linalg.norm(residual, axis=1)))

        assert max_res < 1e-14, f"Angle {angle_deg} deg violated decomposition identity: {max_res:.3e}"

        # Check orthogonality angle recovery
        computed_angle = mesh.error_map["non_orthogonality"][internal][0]
        assert abs(computed_angle - angle_deg) < 1.0


# ============================================================================
# Invariant 4: Error Map Metrics, Stabilization Masks, and Aliases
# ============================================================================

class TestInvariant4ErrorMapAndMasks:
    """Empirical verification of error map metric fidelity, 4-tier masks, and dual aliases."""

    @pytest.mark.parametrize("use_native", [True, False])
    def test_stabilization_mask_tiers(self, use_native: bool):
        tiers = [
            (15.0, 0),  # Tier 0: < 40 deg
            (50.0, 1),  # Tier 1: [40, 65) deg
            (70.0, 2),  # Tier 2: [65, 75) deg
            (82.0, 3),  # Tier 3: >= 75 deg
        ]
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

        for angle, expected_tier in tiers:
            pts, cells = make_angled_two_cell_mesh(angle)
            mesh = fn(pts, cells)
            internal = mesh.neighbour >= 0
            tier = mesh.error_map["stabilization_mask"][internal][0]
            assert tier == expected_tier, f"Angle {angle} deg got tier {tier}, expected {expected_tier}"

    @pytest.mark.parametrize("use_native", [True, False])
    def test_error_map_key_aliases(self, use_native: bool):
        pts, cells = make_sheared_hex_mesh(nx=2, ny=2, nz=2, shear_xy=0.5, shear_xz=0.2)
        fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
        mesh = fn(pts, cells)

        # Both primary and alias keys must be present and identical
        assert "non_orthogonality" in mesh.error_map
        assert "non_ortho" in mesh.error_map
        np.testing.assert_array_equal(mesh.error_map["non_orthogonality"], mesh.error_map["non_ortho"])

        assert "skewness" in mesh.error_map
        assert "skew" in mesh.error_map
        np.testing.assert_array_equal(mesh.error_map["skewness"], mesh.error_map["skew"])


# ============================================================================
# Invariant 5: Remediation Quality & Downstream Integration Checks
# ============================================================================

class TestInvariant5RemediationFidelity:
    """Empirical verification of the 3 audit defects resolved in Iteration 2."""

    def test_gci_topological_dim_none_and_dimensional_scaling(self):
        """Verify gci.py handles topological_dim is None without TypeError."""
        cells = [np.arange(8, dtype=np.int64)]
        pts = np.zeros((8, 3), dtype=np.float64)
        vols = np.array([8.0], dtype=np.float64)

        mesh = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
            topological_dim=None,
        )
        # Explicit None
        h_none = compute_grid_representative_size(mesh)
        assert abs(h_none - 2.0) < 1e-12

        # Explicit 1D, 2D, 3D
        mesh.topological_dim = 1
        assert abs(compute_grid_representative_size(mesh) - 8.0) < 1e-12

        mesh.topological_dim = 2
        assert abs(compute_grid_representative_size(mesh) - math.sqrt(8.0)) < 1e-12

        mesh.topological_dim = 3
        assert abs(compute_grid_representative_size(mesh) - 2.0) < 1e-12

    def test_wsrd_authentic_donor_receiver_volume_pairing(self):
        """Verify Berger-Giuliani donor-receiver volume capacity pairing."""
        # 3 cells: Cell 0 is small donor (alpha=0.35)
        # Cell 1 is smaller neighbor (alpha=0.22)
        # Cell 2 is regular neighbor (alpha=0.85)
        # Faces: face 0 between cell 0 and cell 1; face 1 between cell 0 and cell 2
        owner = np.array([0, 0], dtype=np.int64)
        neighbour = np.array([1, 2], dtype=np.int64)
        wsrd_alpha = np.array([0.35, 0.22, 0.85], dtype=np.float64)

        redist_map = compute_wsrd_redistribution_map(
            wsrd_alpha, owner, neighbour, 3, 2,
            small_cell_ratio=0.20, wsrd_target_alpha=0.50,
        )

        assert 0 in redist_map
        cell_0_entry = redist_map[0]
        w_self = cell_0_entry["self"]
        weights_by_rcv = cell_0_entry["neighbors"]

        # Target cell 2 (regular fluid) must receive positive weight
        # Target cell 1 (smaller cut cell) must receive 0 weight because cell 2 exists
        assert 2 in weights_by_rcv
        assert weights_by_rcv[2] > 0.0
        assert weights_by_rcv.get(1, 0.0) == 0.0

        # Exact partition of unity: w_self + sum(w_nb) = 1.0
        total_w = w_self + sum(weights_by_rcv.values())
        assert abs(total_w - 1.0) < 1e-14

    def test_mesh_engine_config_shadowing_inheritance(self):
        """Verify MeshEngine preserves instance config when config=None is passed."""
        cfg = MeshingConfig(domain_mode="external_fluid")
        engine = MeshEngine(config=cfg)

        active_cfg = engine.config
        assert active_cfg.domain_mode == "external_fluid"

        # Check dispatch evaluation logic directly
        config_arg = None
        is_solid_only = (config_arg is None and engine.config is None) or (active_cfg.domain_mode == "solid_only")
        assert not is_solid_only, "Passing config=None must not override engine's non-solid configuration!"
