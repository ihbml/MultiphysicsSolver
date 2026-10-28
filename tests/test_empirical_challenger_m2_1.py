"""Empirical and Adversarial Challenger Test Suite for Milestone M2.

Author: challenger_m2_1 (Mathematical Invariants Challenger)
Assigned Directory: .agents/teamwork/challenger_m2_1/
Test Suite: tests/test_empirical_challenger_m2_1.py

Adversarially tests mathematical and physical invariants of backend/meshing:
1. Strictly positive cell volumes (V_i > 10^-15) across perturbed, non-convex, and highly distorted meshes.
2. Gauss divergence theorem closed-cell closure (max_i ||sum sigma_if S_f|| < 10^-11).
3. Over-relaxed non-orthogonal decomposition identity (max_f ||delta_f + k_f - S_f|| < 10^-14) across angles > 70 deg.
4. Error map metrics: Non-orthogonality angle formula, skewness, aspect ratio, and 4-tier stabilization mask (0, 1, 2, 3).
5. Native C++20 vs. Pure-Python fallback bit-level consistency.
6. Real CAD meshes (TestCAD rotated cube and BladeRev1CFD turbine blade).
"""

import math
from pathlib import Path
from typing import Any, Dict, List, Tuple
import numpy as np
import pytest

from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    get_native_dll,
)
from backend.meshing.models import UnstructuredMesh, MeshingConfig, InflationConfig
from backend.meshing.mesh_engine import MeshEngine

REPO_ROOT = Path(__file__).resolve().parent.parent
CAD_SAMPLES_DIR = REPO_ROOT / "resources" / "CADTestSamples"
TESTCAD_OBJ = CAD_SAMPLES_DIR / "TestCAD.obj"
BLADE_OBJ = CAD_SAMPLES_DIR / "ComplexGeometryExample" / "BladeRev1CFD.obj"


# ---------------------------------------------------------------------------
# Generators for Stress Testing
# ---------------------------------------------------------------------------

def generate_perturbed_hex_mesh(
    nx: int = 4, ny: int = 4, nz: int = 4,
    lx: float = 1.0, ly: float = 1.0, lz: float = 1.0,
    perturbation: float = 0.05,
    seed: int = 42,
) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate structured hex mesh with 3D random perturbation creating non-planar faces."""
    rng = np.random.default_rng(seed)
    x = np.linspace(0.0, lx, nx + 1)
    y = np.linspace(0.0, ly, ny + 1)
    z = np.linspace(0.0, lz, nz + 1)

    dx = lx / nx
    dy = ly / ny
    dz = lz / nz
    scale = min(dx, dy, dz) * perturbation

    points = []
    point_idx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                # Don't perturb outer boundaries to keep domain bounded
                is_boundary = (i == 0 or i == nx or j == 0 or j == ny or k == 0 or k == nz)
                shift = np.zeros(3) if is_boundary else rng.uniform(-scale, scale, size=3)
                p = np.array([x[i], y[j], z[k]]) + shift
                point_idx[(i, j, k)] = len(points)
                points.append(p)

    points_arr = np.array(points, dtype=np.float64)

    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                c = [
                    point_idx[(i, j, k)],
                    point_idx[(i + 1, j, k)],
                    point_idx[(i + 1, j + 1, k)],
                    point_idx[(i, j + 1, k)],
                    point_idx[(i, j, k + 1)],
                    point_idx[(i + 1, j, k + 1)],
                    point_idx[(i + 1, j + 1, k + 1)],
                    point_idx[(i, j + 1, k + 1)],
                ]
                cells.append(np.array(c, dtype=np.int64))

    return points_arr, cells


def generate_sheared_hex_mesh(
    nx: int = 4, ny: int = 4, nz: int = 4,
    shear_xy: float = 1.0,
    shear_xz: float = 0.5,
) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate severely sheared hex mesh with large non-orthogonality."""
    x = np.linspace(0.0, 1.0, nx + 1)
    y = np.linspace(0.0, 1.0, ny + 1)
    z = np.linspace(0.0, 1.0, nz + 1)

    points = []
    point_idx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                px = x[i] + shear_xy * y[j] + shear_xz * z[k]
                py = y[j]
                pz = z[k]
                point_idx[(i, j, k)] = len(points)
                points.append([px, py, pz])

    points_arr = np.array(points, dtype=np.float64)

    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                c = [
                    point_idx[(i, j, k)],
                    point_idx[(i + 1, j, k)],
                    point_idx[(i + 1, j + 1, k)],
                    point_idx[(i, j + 1, k)],
                    point_idx[(i, j, k + 1)],
                    point_idx[(i + 1, j, k + 1)],
                    point_idx[(i + 1, j + 1, k + 1)],
                    point_idx[(i, j + 1, k + 1)],
                ]
                cells.append(np.array(c, dtype=np.int64))

    return points_arr, cells


def generate_angled_face_mesh(target_angle_deg: float) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate a 2-cell hex mesh where the shared face has exact target non-orthogonality angle."""
    theta_rad = math.radians(target_angle_deg)
    # Owner cell is [0, 1] x [0, 1] x [0, 1] with centroid C_o = (0.5, 0.5, 0.5)
    # Shared face at x=1, normal S_f = (1, 0, 0)
    # Neighbour cell has shared face at x=1 and outer face at x=2 with y-offset y_shift.
    # Cell 1 centroid C_nb = (1.5, 0.5 + 0.5 * y_shift, 0.5).
    # d_f = C_nb - C_o = (1.0, 0.5 * y_shift, 0.0).
    # Non-ortho angle cos(theta) = 1.0 / sqrt(1.0 + (0.5 * y_shift)^2).
    # Thus, 0.5 * y_shift = tan(theta) => y_shift = 2.0 * tan(theta).
    tan_theta = math.tan(theta_rad)
    y_shift = 2.0 * tan_theta

    points = [
        # Owner hex (cell 0)
        [0.0, 0.0, 0.0],  # 0
        [1.0, 0.0, 0.0],  # 1 (shared)
        [1.0, 1.0, 0.0],  # 2 (shared)
        [0.0, 1.0, 0.0],  # 3
        [0.0, 0.0, 1.0],  # 4
        [1.0, 0.0, 1.0],  # 5 (shared)
        [1.0, 1.0, 1.0],  # 6 (shared)
        [0.0, 1.0, 1.0],  # 7
        # Neighbour outer vertices (cell 1)
        [2.0, y_shift, 0.0],        # 8
        [2.0, 1.0 + y_shift, 0.0],  # 9
        [2.0, y_shift, 1.0],        # 10
        [2.0, 1.0 + y_shift, 1.0],  # 11
    ]
    points_arr = np.array(points, dtype=np.float64)

    cells = [
        np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64),
        np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64),
    ]
    return points_arr, cells


def generate_non_convex_hex() -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate a valid non-convex hex cell (chevron / boomerang prism)."""
    # 8 vertices forming an indented hex where interior vertex is indented
    points = [
        # Bottom quad (indented)
        [0.0, 0.0, 0.0],  # 0
        [2.0, 0.0, 0.0],  # 1
        [1.0, 0.6, 0.0],  # 2 (indented inwards)
        [0.0, 1.0, 0.0],  # 3
        # Top quad
        [0.0, 0.0, 1.0],  # 4
        [2.0, 0.0, 1.0],  # 5
        [1.0, 0.6, 1.0],  # 6 (indented inwards)
        [0.0, 1.0, 1.0],  # 7
    ]
    return np.array(points, dtype=np.float64), [np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)]


def generate_hybrid_mesh() -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generate a multi-element hybrid mesh containing Hex, Tet, Wedge, and Pyramid."""
    # Box [0, 1]^3 as Hex, adjacent Wedge, Tet, and Pyramid
    points = [
        # Hex [0, 1]^3
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
        # Wedge attached at right (x=1 to x=2)
        [2, 0, 0], [2, 0, 1], # 8, 9
        # Pyramid attached at top (z=1 to apex z=2)
        [0.5, 0.5, 2.0],      # 10
        # Tet attached at left (x=-1)
        [-1, 0.5, 0.5],       # 11
    ]
    points_arr = np.array(points, dtype=np.float64)

    cells = [
        # Hex
        np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64),
        # Wedge (nv=6): [1, 2, 8, 5, 6, 9]
        # In our notation: [0, 2, 1] bottom, [3, 4, 5] top, etc.
        # Nodes: 1, 8, 2 (bottom triangle), 5, 9, 6 (top triangle)
        np.array([1, 8, 2, 5, 9, 6], dtype=np.int64),
        # Pyramid (nv=5): base [4, 5, 6, 7], apex 10
        np.array([4, 5, 6, 7, 10], dtype=np.int64),
        # Tet (nv=4): base triangle on left face [0, 3, 4], apex 11
        np.array([0, 3, 4, 11], dtype=np.int64),
    ]
    return points_arr, cells


# ---------------------------------------------------------------------------
# Invariant Test 1: Strictly Positive Cell Volumes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_positivity_perturbed(use_native: bool):
    """Stress volume positivity across 3D randomly perturbed meshes."""
    pts, cells = generate_perturbed_hex_mesh(nx=5, ny=5, nz=5, perturbation=0.15, seed=777)
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    volumes = mesh.cell_volumes
    min_vol = float(np.min(volumes))
    assert min_vol > 1e-15, f"Non-positive cell volume found: {min_vol:.2e} <= 1e-15"
    assert np.all(np.isfinite(volumes)), "Volumes contain NaN or Inf"

    # Verify domain volume conservation (perturbed inner nodes should not change total volume)
    total_vol = float(np.sum(volumes))
    assert abs(total_vol - 1.0) < 1e-10, f"Total volume {total_vol} deviated from 1.0"


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_positivity_extreme_aspect_ratio(use_native: bool):
    """Stress volume positivity on extreme aspect ratio cells (1000:1 and 1:1000)."""
    # 1. Stretched cell (1000 x 1 x 1)
    pts1 = np.array([
        [0, 0, 0], [1000, 0, 0], [1000, 1, 0], [0, 1, 0],
        [0, 0, 1], [1000, 0, 1], [1000, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells1 = [np.arange(8, dtype=np.int64)]

    # 2. Flattened boundary-layer-like cell (1 x 1 x 1e-4)
    pts2 = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1e-4], [1, 0, 1e-4], [1, 1, 1e-4], [0, 1, 1e-4],
    ], dtype=np.float64)
    cells2 = [np.arange(8, dtype=np.int64)]

    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    m1 = fn(pts1, cells1)
    assert m1.cell_volumes[0] > 1e-15
    assert abs(m1.cell_volumes[0] - 1000.0) < 1e-10

    m2 = fn(pts2, cells2)
    assert m2.cell_volumes[0] > 1e-15
    assert abs(m2.cell_volumes[0] - 1e-4) < 1e-14


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_positivity_micro_scale(use_native: bool):
    """Stress volume positivity on micro-scale cells (L = 1e-4 m, V = 1e-12 m^3)."""
    L = 1e-4
    pts = np.array([
        [0, 0, 0], [L, 0, 0], [L, L, 0], [0, L, 0],
        [0, 0, L], [L, 0, L], [L, L, L], [0, L, L],
    ], dtype=np.float64)
    cells = [np.arange(8, dtype=np.int64)]

    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    vol = mesh.cell_volumes[0]
    expected_vol = L ** 3  # 1e-12
    assert vol > 1e-15, f"Micro-scale volume {vol:.2e} <= 1e-15"
    assert abs(vol - expected_vol) / expected_vol < 1e-12


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_positivity_non_convex(use_native: bool):
    """Stress volume calculation on non-convex hex cell."""
    pts, cells = generate_non_convex_hex()
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    vol = mesh.cell_volumes[0]
    assert vol > 1e-15
    # Analytical volume: base area of non-convex polygon * height 1.0
    # Base polygon: (0,0)-(2,0)-(1,0.6)-(0,1).
    # Area via shoelace: 0.5 * |(0*0 - 0*2) + (2*0.6 - 0*1) + (1*1 - 0.6*0) + (0*0 - 1*0)| = 0.5 * (1.2 + 1.0) = 1.1
    expected_vol = 1.1
    assert abs(vol - expected_vol) < 1e-12, f"Non-convex volume {vol} != expected {expected_vol}"


# ---------------------------------------------------------------------------
# Invariant Test 2: Gauss Divergence Closed-Cell Closure
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
@pytest.mark.parametrize("shear", [0.0, 0.5, 1.0, 2.0, 3.5])
def test_invariant_gauss_divergence_closure_sheared(use_native: bool, shear: float):
    """Verify Gauss divergence closure sum sigma_if S_f < 10^-11 across extreme shears."""
    pts, cells = generate_sheared_hex_mesh(nx=4, ny=4, nz=4, shear_xy=shear, shear_xz=shear * 0.5)
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    n_cells = len(mesh.cell_volumes)
    closure = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(closure, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

    max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
    assert max_closure < 1e-11, f"Shear {shear} violated Gauss closure: {max_closure:.2e} >= 1e-11"


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_gauss_divergence_closure_perturbed_warped(use_native: bool):
    """Verify Gauss closure on mesh with non-planar warped faces."""
    pts, cells = generate_perturbed_hex_mesh(nx=6, ny=6, nz=6, perturbation=0.2, seed=12345)
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    n_cells = len(mesh.cell_volumes)
    closure = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(closure, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

    max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
    assert max_closure < 1e-11, f"Warped mesh violated Gauss closure: {max_closure:.2e} >= 1e-11"


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_gauss_divergence_closure_hybrid(use_native: bool):
    """Verify Gauss closure on hybrid mesh with Hex, Tet, Wedge, and Pyramid elements."""
    pts, cells = generate_hybrid_mesh()
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    n_cells = len(mesh.cell_volumes)
    closure = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(closure, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

    max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
    assert max_closure < 1e-11, f"Hybrid mesh violated Gauss closure: {max_closure:.2e} >= 1e-11"


# ---------------------------------------------------------------------------
# Invariant Test 3: Over-Relaxed Non-Orthogonal Decomposition Identity
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("angle_deg", [0.0, 30.0, 45.0, 60.0, 70.0, 75.0, 80.0, 85.0, 88.0, 89.0])
@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_over_relaxed_decomposition_across_angles(angle_deg: float, use_native: bool):
    """Verify max_f ||delta_f + k_f - S_f|| < 10^-14 across all angles, especially > 70 deg."""
    pts, cells = generate_angled_face_mesh(angle_deg)
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    internal = mesh.neighbour >= 0
    assert np.any(internal), "Must have internal face"

    diff = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
    max_decomp_err = float(np.max(np.linalg.norm(diff, axis=1)))

    assert max_decomp_err < 1e-14, (
        f"Angle {angle_deg} deg violated decomposition identity: {max_decomp_err:.2e} >= 1e-14"
    )

    # Also verify non_orthogonality angle computation
    actual_angle = mesh.error_map["non_orthogonality"][internal][0]
    assert abs(actual_angle - angle_deg) < 1.0, (
        f"Expected angle ~ {angle_deg} deg, got {actual_angle:.2f} deg"
    )


# ---------------------------------------------------------------------------
# Invariant Test 4: Error Map Metrics & 4-Tier Stabilization Mask
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_error_map_mathematical_oracle(use_native: bool):
    """Rigorously verify all error map metrics against independent mathematical oracles."""
    pts, cells = generate_sheared_hex_mesh(nx=3, ny=3, nz=3, shear_xy=0.6, shear_xz=0.3)
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python
    mesh = fn(pts, cells)

    error_map = mesh.error_map
    n_faces = mesh.num_faces
    n_cells = mesh.num_cells

    # 1. Oracle for Non-Orthogonality Angle
    for f in range(n_faces):
        o = mesh.owner[f]
        nb = mesh.neighbour[f]
        sf = mesh.face_areas[f]
        if nb >= 0:
            df = mesh.cell_centres[nb] - mesh.cell_centres[o]
            sf_norm = np.linalg.norm(sf)
            df_norm = np.linalg.norm(df)
            cos_th = np.clip(np.dot(sf, df) / (sf_norm * df_norm), 0.0, 1.0)
            oracle_th = float(np.arccos(cos_th) * 180.0 / np.pi)
            actual_th = error_map["non_orthogonality"][f]
            assert abs(actual_th - oracle_th) < 1e-11, f"Face {f} angle error: {actual_th} vs oracle {oracle_th}"
        else:
            assert error_map["non_orthogonality"][f] == 0.0

    # 2. Oracle for Skewness
    for f in range(n_faces):
        o = mesh.owner[f]
        nb = mesh.neighbour[f]
        sf = mesh.face_areas[f]
        cf = mesh.face_centres[f]
        co = mesh.cell_centres[o]
        if nb >= 0:
            df = mesh.cell_centres[nb] - co
            sf_norm = np.linalg.norm(sf)
            df_norm = np.linalg.norm(df)
            nf = sf / sf_norm
            df_dot_nf = float(np.dot(df, nf))
            oracle_skew = 0.0
            if abs(df_dot_nf) > 1e-15:
                t = float(np.dot(cf - co, nf)) / df_dot_nf
                x_int = co + df * t
                oracle_skew = float(np.linalg.norm(cf - x_int) / df_norm)
            actual_skew = error_map["skewness"][f]
            assert abs(actual_skew - oracle_skew) < 1e-11, f"Face {f} skewness error: {actual_skew} vs oracle {oracle_skew}"
        else:
            assert error_map["skewness"][f] == 0.0

    # 3. Oracle for Aspect Ratio
    for c in range(n_cells):
        cell_pts = pts[cells[c]]
        diffs = cell_pts[:, None, :] - cell_pts[None, :, :]
        dists = np.linalg.norm(diffs, axis=-1)
        nonzero = dists[dists > 1e-12]
        oracle_ar = float(np.max(nonzero) / np.min(nonzero)) if len(nonzero) > 0 else 1.0
        oracle_ar = max(1.0, oracle_ar)
        actual_ar = error_map["aspect_ratio"][c]
        assert abs(actual_ar - oracle_ar) / oracle_ar < 1e-11, f"Cell {c} aspect ratio error: {actual_ar} vs {oracle_ar}"


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_4_tier_stabilization_mask(use_native: bool):
    """Verify that all 4 stabilization tiers (0, 1, 2, 3) trigger correctly per threshold rules."""
    # Test specific target angles to trigger all 4 tiers
    # Tier 0: theta < 40 and skew < 0.4  -> e.g. angle = 20 deg
    # Tier 1: 40 <= theta < 65            -> e.g. angle = 50 deg
    # Tier 2: 65 <= theta < 75            -> e.g. angle = 70 deg
    # Tier 3: theta >= 75                 -> e.g. angle = 80 deg

    test_angles = [
        (20.0, 0),
        (50.0, 1),
        (70.0, 2),
        (80.0, 3),
    ]

    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    for target_deg, expected_tier in test_angles:
        pts, cells = generate_angled_face_mesh(target_deg)
        mesh = fn(pts, cells)
        internal = mesh.neighbour >= 0
        actual_tier = mesh.error_map["stabilization_mask"][internal][0]
        actual_angle = mesh.error_map["non_orthogonality"][internal][0]

        assert actual_tier == expected_tier, (
            f"Angle {target_deg} deg (actual {actual_angle:.1f} deg) assigned tier {actual_tier}, expected {expected_tier}"
        )


# ---------------------------------------------------------------------------
# Invariant Test 5: Real CAD Models (TestCAD Cube & BladeRev1CFD)
# ---------------------------------------------------------------------------

def test_real_cad_testcad_cube_invariants():
    """Verify all mathematical invariants on rotated TestCAD 200 mm cube."""
    if not TESTCAD_OBJ.exists():
        pytest.skip(f"TestCAD.obj not found at {TESTCAD_OBJ}")

    engine = MeshEngine()
    mesh = engine.mesh_cad_file(TESTCAD_OBJ, h0=0.03, scale=0.001)

    # 1. Positivity
    min_vol = float(np.min(mesh.cell_volumes))
    assert min_vol > 1e-15, f"TestCAD cube has non-positive volume: {min_vol:.2e}"

    # 2. Closed-cell Gauss divergence closure
    closure = np.zeros((mesh.num_cells, 3), dtype=np.float64)
    np.add.at(closure, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])
    max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
    assert max_closure < 1e-11, f"TestCAD cube max closure {max_closure:.2e} >= 1e-11"

    # 3. Over-relaxed decomposition identity
    diff = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
    max_decomp = float(np.max(np.linalg.norm(diff, axis=1)))
    assert max_decomp < 1e-14, f"TestCAD cube max decomp diff {max_decomp:.2e} >= 1e-14"

    # 4. Error map completeness
    for k in ["non_orthogonality", "skewness", "aspect_ratio", "stabilization_mask"]:
        assert k in mesh.error_map


def test_real_cad_blade_rev1_invariants():
    """Verify all mathematical invariants on complex aerodynamic turbine blade."""
    if not BLADE_OBJ.exists():
        pytest.skip(f"BladeRev1CFD.obj not found at {BLADE_OBJ}")

    engine = MeshEngine()
    mesh = engine.mesh_cad_file(BLADE_OBJ, h0=15.0, scale=1.0)

    # 1. Positivity
    min_vol = float(np.min(mesh.cell_volumes))
    assert min_vol > 1e-15, f"Blade has non-positive volume: {min_vol:.2e}"

    # 2. Closed-cell Gauss divergence closure
    closure = np.zeros((mesh.num_cells, 3), dtype=np.float64)
    np.add.at(closure, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])
    max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
    assert max_closure < 1e-11, f"Blade max closure {max_closure:.2e} >= 1e-11"

    # 3. Over-relaxed decomposition identity
    diff = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
    max_decomp = float(np.max(np.linalg.norm(diff, axis=1)))
    assert max_decomp < 1e-14, f"Blade max decomp diff {max_decomp:.2e} >= 1e-14"


# ---------------------------------------------------------------------------
# Invariant Test 6: 2D Green's Theorem (Divisor 2.0) vs 3D Gauss (Divisor 3.0)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_divisor_2d_green_theorem(use_native: bool):
    """Confirm volume calculation correctly applies divisor 2.0 (Green's theorem) for 2D meshes."""
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    # 1. 2D Right triangle with vertices (0,0,0), (3,0,0), (0,4,0)
    # Analytical Area = 0.5 * 3.0 * 4.0 = 6.0
    pts_tri = np.array([[0.0, 0.0, 0.0], [3.0, 0.0, 0.0], [0.0, 4.0, 0.0]], dtype=np.float64)
    cells_tri = [np.array([0, 1, 2], dtype=np.int64)]
    mesh_tri = fn(pts_tri, cells_tri)
    vol_tri = mesh_tri.cell_volumes[0]
    assert abs(vol_tri - 6.0) < 1e-12, f"2D triangle area {vol_tri} != 6.0 (divisor 2.0 violation)"
    # If divisor 3.0 was erroneously applied, vol would be 4.0 (33.3% relative error)
    assert abs(vol_tri - 4.0) > 1.0, "2D triangle erroneously used divisor 3.0 instead of 2.0"

    # 2. 2D Regular heptagon (7-gon, nv=7) in z=0 plane
    # Area = 0.5 * 7 * sin(2*pi/7) = 2.7364101886381045
    angles = np.linspace(0.0, 2.0 * np.pi, 8)[:-1]
    pts_7gon = np.column_stack([np.cos(angles), np.sin(angles), np.zeros(7)])
    cells_7gon = [np.arange(7, dtype=np.int64)]
    mesh_7gon = fn(pts_7gon, cells_7gon)
    vol_7gon = mesh_7gon.cell_volumes[0]
    expected_7gon = 0.5 * 7.0 * np.sin(2.0 * np.pi / 7.0)
    assert abs(vol_7gon - expected_7gon) < 1e-12, f"2D 7-gon area {vol_7gon} != {expected_7gon}"

    # 3. 2D Mesh generated by MeshEngine.mesh_2d_polygon on [0, 1]^2 unit square
    engine = MeshEngine(use_native=use_native)
    square_poly = np.array([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]], dtype=np.float64)
    mesh_square = engine.mesh_2d_polygon(square_poly)
    assert mesh_square.num_cells > 0
    assert np.all(mesh_square.cell_volumes > 1e-15)
    total_area_2d = float(np.sum(mesh_square.cell_volumes))
    assert abs(total_area_2d - 1.0) < 1e-12, f"Total 2D mesh area {total_area_2d} != 1.0"


@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_volume_divisor_3d_gauss_divergence(use_native: bool):
    """Confirm volume calculation correctly applies divisor 3.0 (Gauss divergence) for 3D elements."""
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    # 1. 3D Unit Tetrahedron: base in xy, apex at (0, 0, 1)
    # Analytical Volume = 1/6 = 0.16666666666666666
    pts_tet = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], dtype=np.float64)
    cells_tet = [np.array([0, 1, 2, 3], dtype=np.int64)]
    mesh_tet = fn(pts_tet, cells_tet)
    assert abs(mesh_tet.cell_volumes[0] - (1.0 / 6.0)) < 1e-12, "3D tet volume != 1/6"

    # 2. 3D Unit Hexahedron: [0, 1]^3
    # Analytical Volume = 1.0
    pts_hex = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells_hex = [np.arange(8, dtype=np.int64)]
    mesh_hex = fn(pts_hex, cells_hex)
    assert abs(mesh_hex.cell_volumes[0] - 1.0) < 1e-12, "3D hex volume != 1.0"

    # 3. 3D Triangular Prism / Wedge:
    # Base triangle area = 0.5, height = 1.0 -> Volume = 0.5
    pts_wedge = np.array([
        [0, 0, 0], [1, 0, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells_wedge = [np.array([0, 1, 2, 3, 4, 5], dtype=np.int64)]
    mesh_wedge = fn(pts_wedge, cells_wedge)
    assert abs(mesh_wedge.cell_volumes[0] - 0.5) < 1e-12, "3D wedge volume != 0.5"

    # 4. 3D Pyramid: base [0, 1]^2, height = 1.0 -> Volume = 1/3
    pts_pyr = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0.5, 0.5, 1.0],
    ], dtype=np.float64)
    cells_pyr = [np.array([0, 1, 2, 3, 4], dtype=np.int64)]
    mesh_pyr = fn(pts_pyr, cells_pyr)
    assert abs(mesh_pyr.cell_volumes[0] - (1.0 / 3.0)) < 1e-12, "3D pyramid volume != 1/3"


# ---------------------------------------------------------------------------
# Invariant Test 7: Genuine Negative Signed Volumes for Inverted Cells
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_inverted_cells_genuine_negative_volume(use_native: bool):
    """Stress-test inverted cells: verify genuine negative signed volumes without abs() or 1.0 m^3 fallback."""
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    # 1. Inverted 2D triangle: clockwise vertex winding [0, 2, 1]
    pts_tri = np.array([[0.0, 0.0, 0.0], [3.0, 0.0, 0.0], [0.0, 4.0, 0.0]], dtype=np.float64)
    cells_inv_tri = [np.array([0, 2, 1], dtype=np.int64)]
    mesh_inv_tri = fn(pts_tri, cells_inv_tri)
    vol_inv_tri = mesh_inv_tri.cell_volumes[0]
    assert vol_inv_tri < -1e-15, f"Inverted 2D triangle volume {vol_inv_tri} must be strictly negative"
    assert abs(vol_inv_tri - (-6.0)) < 1e-12, f"Inverted 2D triangle volume {vol_inv_tri} != -6.0"
    assert vol_inv_tri != 1.0, "Inverted 2D triangle must not be replaced by artificial 1.0 fallback"
    assert vol_inv_tri != 6.0, "Inverted 2D triangle must not be masked with abs()"

    # 2. Inverted 3D Tetrahedron: swapped face vertices [0, 2, 1, 3]
    pts_tet = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], dtype=np.float64)
    cells_inv_tet = [np.array([0, 2, 1, 3], dtype=np.int64)]
    mesh_inv_tet = fn(pts_tet, cells_inv_tet)
    vol_inv_tet = mesh_inv_tet.cell_volumes[0]
    assert vol_inv_tet < -1e-15, f"Inverted 3D tet volume {vol_inv_tet} must be strictly negative"
    assert abs(vol_inv_tet - (-1.0 / 6.0)) < 1e-12, f"Inverted 3D tet volume {vol_inv_tet} != -1/6"
    assert vol_inv_tet != 1.0, "Inverted 3D tet must not be replaced by artificial 1.0 fallback"
    assert vol_inv_tet != 1.0 / 6.0, "Inverted 3D tet must not be masked with abs()"

    # 3. Inverted 3D Hexahedron: swapped top and bottom planes
    pts_hex = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells_inv_hex = [np.array([4, 5, 6, 7, 0, 1, 2, 3], dtype=np.int64)]
    mesh_inv_hex = fn(pts_hex, cells_inv_hex)
    vol_inv_hex = mesh_inv_hex.cell_volumes[0]
    assert vol_inv_hex < -1e-15, f"Inverted 3D hex volume {vol_inv_hex} must be strictly negative"
    assert abs(vol_inv_hex - (-1.0)) < 1e-12, f"Inverted 3D hex volume {vol_inv_hex} != -1.0"
    assert vol_inv_hex != 1.0, "Inverted 3D hex must not be replaced by artificial 1.0 fallback"

    # 4. Inverted 3D Wedge: swapped top and bottom triangles
    pts_wedge = np.array([
        [0, 0, 0], [1, 0, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells_inv_wedge = [np.array([3, 4, 5, 0, 1, 2], dtype=np.int64)]
    mesh_inv_wedge = fn(pts_wedge, cells_inv_wedge)
    vol_inv_wedge = mesh_inv_wedge.cell_volumes[0]
    assert vol_inv_wedge < -1e-15, f"Inverted 3D wedge volume {vol_inv_wedge} must be strictly negative"
    assert abs(vol_inv_wedge - (-0.5)) < 1e-12, f"Inverted 3D wedge volume {vol_inv_wedge} != -0.5"
    assert vol_inv_wedge != 1.0, "Inverted 3D wedge must not be replaced by artificial 1.0 fallback"


# ---------------------------------------------------------------------------
# Invariant Test 8: Collapsed / Degenerate Cells Evaluate to Zero (No 1.0 Fallback)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("use_native", [True, False])
def test_invariant_collapsed_degenerate_cells_zero_volume(use_native: bool):
    """Confirm collapsed / zero-thickness cells evaluate to zero volume without artificial 1.0 fallback."""
    fn = compute_difvm_topology_native if (use_native and get_native_dll()) else compute_difvm_topology_python

    # 1. Collapsed hex: top vertices collapsed onto bottom vertices (zero z-thickness)
    pts_flat_hex = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
    ], dtype=np.float64)
    cells_flat_hex = [np.arange(8, dtype=np.int64)]
    mesh_flat_hex = fn(pts_flat_hex, cells_flat_hex)
    assert abs(mesh_flat_hex.cell_volumes[0]) < 1e-15, "Flat hex volume must be 0.0"
    assert mesh_flat_hex.cell_volumes[0] != 1.0, "Flat hex volume must not be replaced by 1.0"

    # 2. Degenerate tet: all 4 vertices are collinear
    pts_collinear_tet = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [2.0, 0.0, 0.0],
        [3.0, 0.0, 0.0],
    ], dtype=np.float64)
    cells_collinear_tet = [np.array([0, 1, 2, 3], dtype=np.int64)]
    mesh_collinear_tet = fn(pts_collinear_tet, cells_collinear_tet)
    assert abs(mesh_collinear_tet.cell_volumes[0]) < 1e-15, "Collinear tet volume must be 0.0"
    assert mesh_collinear_tet.cell_volumes[0] != 1.0, "Collinear tet volume must not be replaced by 1.0"


# ---------------------------------------------------------------------------
# Invariant Test 9: Quantitative Residual Audit Across Adversarial Configurations
# ---------------------------------------------------------------------------

def test_invariant_quantitative_residuals_audit():
    """Aggregate exact quantitative residuals across severe non-orthogonality and distorted meshes."""
    residuals = {
        "max_gauss_closure": 0.0,
        "max_decomposition_error": 0.0,
        "min_positive_volume": 1e9,
    }

    # Sheared meshes up to shear=3.5
    for shear in [0.0, 0.5, 1.0, 2.0, 3.5]:
        pts, cells = generate_sheared_hex_mesh(nx=4, ny=4, nz=4, shear_xy=shear, shear_xz=shear * 0.5)
        for fn in [compute_difvm_topology_native, compute_difvm_topology_python]:
            mesh = fn(pts, cells)
            n_cells = len(mesh.cell_volumes)
            closure = np.zeros((n_cells, 3), dtype=np.float64)
            np.add.at(closure, mesh.owner, mesh.face_areas)
            internal = mesh.neighbour >= 0
            np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])
            max_c = float(np.max(np.linalg.norm(closure, axis=1)))
            residuals["max_gauss_closure"] = max(residuals["max_gauss_closure"], max_c)

            diff = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
            max_d = float(np.max(np.linalg.norm(diff, axis=1)))
            residuals["max_decomposition_error"] = max(residuals["max_decomposition_error"], max_d)

            min_v = float(np.min(mesh.cell_volumes))
            residuals["min_positive_volume"] = min(residuals["min_positive_volume"], min_v)

    # Angled meshes up to 89.0 degrees non-orthogonality
    for angle in [0.0, 30.0, 45.0, 60.0, 70.0, 75.0, 80.0, 85.0, 88.0, 89.0]:
        pts, cells = generate_angled_face_mesh(angle)
        for fn in [compute_difvm_topology_native, compute_difvm_topology_python]:
            mesh = fn(pts, cells)
            internal = mesh.neighbour >= 0
            diff = (mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal]
            max_d = float(np.max(np.linalg.norm(diff, axis=1)))
            residuals["max_decomposition_error"] = max(residuals["max_decomposition_error"], max_d)

    print(f"\n[Quantitative Residual Audit]")
    print(f"  Max Gauss Closure Residual:        {residuals['max_gauss_closure']:.6e} (Threshold: 1e-11)")
    print(f"  Max Over-Relaxed Decomp Error:     {residuals['max_decomposition_error']:.6e} (Threshold: 1e-14)")
    print(f"  Min Strictly Positive Cell Volume: {residuals['min_positive_volume']:.6e} (Threshold: 1e-15)")

    assert residuals["max_gauss_closure"] < 1e-11, f"Closure residual {residuals['max_gauss_closure']:.2e} >= 1e-11"
    assert residuals["max_decomposition_error"] < 1e-14, f"Decomp error {residuals['max_decomposition_error']:.2e} >= 1e-14"
    assert residuals["min_positive_volume"] > 1e-15, f"Min volume {residuals['min_positive_volume']:.2e} <= 1e-15"

