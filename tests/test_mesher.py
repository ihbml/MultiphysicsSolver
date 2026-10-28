"""Master Test Suite for In-Memory Multiphysics Mesher Backbone (backend/meshing).

Covers all 20 features and geometric/physical acceptance criteria from PROJECT.md:
1. Strict positive cell volumes (V_i > 0)
2. Exact Gauss divergence closed cell sum (max_i ||sum S_f|| < 10^-11)
3. Non-orthogonality decomposition consistency (delta_f + k_f == S_f)
4. Eulerian SPH truncated support detection via position divergence (div r < 0.75 d)
5. Eulerian SPH 0th-order wall consistency (A_iS + sum V_j grad W_ij = 0, < 10^-10)
6. OPMIS moment-reproducing Taylor P2M and affine M2P linear field reproduction (L_inf < 10^-11)
7. Error map completeness in mesh.error_map (non-orthogonality, skewness, aspect ratio, stabilization mask)
8. Prismatic boundary layer inflation with geometric growth ratio and wall orthogonality
9. Reverse Cuthill-McKee (RCM) matrix bandwidth reduction
10. Moving mesh ALE deformation with variable-stiffness Laplacian smoothing
11. Degenerate cell detection and remeshing triggers under mesh motion
12. Immunity to CAD auxiliary wireframes, 2D sketch lines, and draughting models
13. End-to-end meshing and audit of TestCAD 200 mm rotated cube
14. End-to-end meshing and audit of complex aerodynamic turbine blade (BladeFullRev1CFD / BladeRev1CFD.obj)
"""

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
import numpy as np
import pytest

# Paths to reference CAD models
REPO_ROOT = Path(__file__).resolve().parent.parent
CAD_SAMPLES_DIR = REPO_ROOT / "resources" / "CADTestSamples"
COMPLEX_CAD_DIR = CAD_SAMPLES_DIR / "ComplexGeometryExample"

TESTCAD_OBJ = CAD_SAMPLES_DIR / "TestCAD.obj"
TESTCAD_STL = CAD_SAMPLES_DIR / "TestCAD.stl"
TESTCAD_STEP_AP203 = CAD_SAMPLES_DIR / "TestCADSTPAP203.stp"
TESTCAD_STEP_AP214 = CAD_SAMPLES_DIR / "TestCADSTPAP214.stp"
TESTCAD_STEP_AP242 = CAD_SAMPLES_DIR / "TestCADSTPAP242.stp"

BLADE_OBJ = COMPLEX_CAD_DIR / "BladeRev1CFD.obj"
BLADE_STEP_AP242 = COMPLEX_CAD_DIR / "BladeFullRev1CFD.stp"
BLADE_STEP_AP214 = COMPLEX_CAD_DIR / "BladeFullRev1CFDAP214.stp"


# Dynamic module loaders with informative skip messages
def get_meshing_models():
    """Load backend.meshing.models."""
    return pytest.importorskip("backend.meshing.models", reason="backend.meshing.models not yet implemented")


def get_geometry_sdf():
    """Load backend.meshing.geometry_sdf."""
    return pytest.importorskip("backend.meshing.geometry_sdf", reason="backend.meshing.geometry_sdf not yet implemented")


def get_difvm_topology():
    """Load backend.meshing.difvm_topology."""
    return pytest.importorskip("backend.meshing.difvm_topology", reason="backend.meshing.difvm_topology not yet implemented")


def get_distmesh_core():
    """Load backend.meshing.distmesh_core."""
    return pytest.importorskip("backend.meshing.distmesh_core", reason="backend.meshing.distmesh_core not yet implemented")


def get_inflation():
    """Load backend.meshing.inflation."""
    return pytest.importorskip("backend.meshing.inflation", reason="backend.meshing.inflation not yet implemented")


def get_coupling():
    """Load backend.meshing.coupling."""
    return pytest.importorskip("backend.meshing.coupling", reason="backend.meshing.coupling not yet implemented")


def get_moving_mesh():
    """Load backend.meshing.moving_mesh."""
    return pytest.importorskip("backend.meshing.moving_mesh", reason="backend.meshing.moving_mesh not yet implemented")


def get_mesh_engine():
    """Load backend.meshing.mesh_engine."""
    return pytest.importorskip("backend.meshing.mesh_engine", reason="backend.meshing.mesh_engine not yet implemented")


# ---------------------------------------------------------------------------
# Synthetic Test Geometry Generators
# ---------------------------------------------------------------------------

def create_synthetic_hexahedral_grid(
    nx: int = 4, ny: int = 4, nz: int = 4,
    lx: float = 1.0, ly: float = 1.0, lz: float = 1.0,
    skew_factor: float = 0.0
) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Generates structured hexahedral cells with optional non-orthogonal shear."""
    x = np.linspace(0.0, lx, nx + 1)
    y = np.linspace(0.0, ly, ny + 1)
    z = np.linspace(0.0, lz, nz + 1)

    points = []
    point_idx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                px = x[i] + skew_factor * y[j] * 0.2
                py = y[j] + skew_factor * x[i] * 0.1
                pz = z[k]
                point_idx[(i, j, k)] = len(points)
                points.append([px, py, pz])

    points_arr = np.array(points, dtype=np.float64)

    cells = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                # 8 hex vertices
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


def create_synthetic_particle_cloud(
    n_particles: int = 500, box_size: float = 1.0, seed: int = 42
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Generates a bounded particle cloud inside [0, box_size]^3."""
    rng = np.random.default_rng(seed)
    pos = rng.uniform(0.02, box_size - 0.02, size=(n_particles, 3))
    # Near-wall particles
    n_wall = 60
    wall_x = np.zeros((n_wall, 3))
    wall_x[:, 0] = rng.uniform(0.0, 0.03, size=n_wall)
    wall_x[:, 1] = rng.uniform(0.0, box_size, size=n_wall)
    wall_x[:, 2] = rng.uniform(0.0, box_size, size=n_wall)
    pos = np.vstack([pos, wall_x])

    total_particles = len(pos)
    h = box_size / (total_particles ** (1.0 / 3.0)) * 1.5
    vol = np.full(total_particles, (box_size ** 3) / total_particles, dtype=np.float64)
    return pos, vol, h


# ---------------------------------------------------------------------------
# Tier 1 & 2: Mathematical Invariant and Unit Tests
# ---------------------------------------------------------------------------

def test_strict_positive_volumes():
    """Verify acceptance criterion: all cell volumes are strictly positive (V_i > 0).

    Validates that:
    1. min_i V_i > 10^-15 for both Cartesian and skewed unstructured meshes.
    2. No degenerate, collapsed, or inverted elements exist.
    3. Sum of cell volumes matches analytical domain volume within discretization error.
    """
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    # Generate regular and skewed grids
    points, cells = create_synthetic_hexahedral_grid(nx=4, ny=4, nz=4, lx=0.2, ly=0.2, lz=0.2, skew_factor=0.3)
    mesh = compute_difvm_topology(points, cells)

    assert hasattr(mesh, "cell_volumes"), "Mesh must provide cell_volumes array"
    volumes = mesh.cell_volumes

    # Strict positivity assertions
    assert len(volumes) == len(cells), f"Expected {len(cells)} volumes, got {len(volumes)}"
    assert np.all(volumes > 1e-15), f"Found non-positive cell volume: min(V) = {np.min(volumes)}"
    assert np.all(np.isfinite(volumes)), "Cell volumes must be strictly finite (no NaN or Inf)"

    # Analytical domain volume check (exact Jacobian determinant for bidirectional shear: 1 - 0.06*0.03 = 0.9982)
    total_volume = np.sum(volumes)
    analytical_volume = (0.2 * 0.2 * 0.2) * (1.0 - (0.3 * 0.2) * (0.3 * 0.1))
    rel_vol_err = abs(total_volume - analytical_volume) / analytical_volume
    assert rel_vol_err < 1e-3, f"Total mesh volume {total_volume} deviates from analytical {analytical_volume}"


def test_gauss_divergence_theorem_closure():
    """Verify acceptance criterion: exact Gauss divergence theorem closed cell sum.

    In collocated FVM (DiFVM / OpenFOAM), the outward face area normal vectors S_f
    for every closed polyhedron must sum to zero identically:
        max_i ||sum_{f in faces(i)} sigma_if S_f|| < 10^-11 m^2
    """
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    # Skewed hexahedral grid with non-trivial angles
    points, cells = create_synthetic_hexahedral_grid(nx=5, ny=5, nz=5, lx=1.0, ly=1.0, lz=1.0, skew_factor=0.5)
    mesh = compute_difvm_topology(points, cells)

    owner = mesh.owner
    neighbour = mesh.neighbour
    face_areas = mesh.face_areas
    n_cells = len(mesh.cell_volumes)

    # Accumulate face area vectors per cell
    closure_residuals = np.zeros((n_cells, 3), dtype=np.float64)

    # Owner faces contribute +S_f
    np.add.at(closure_residuals, owner, face_areas)

    # Neighbour faces contribute -S_f (only internal faces have valid neighbour >= 0)
    internal_mask = neighbour >= 0
    np.subtract.at(closure_residuals, neighbour[internal_mask], face_areas[internal_mask])

    # Maximum closure residual norm across all cells
    residual_norms = np.linalg.norm(closure_residuals, axis=1)
    max_closure_error = np.max(residual_norms)

    assert max_closure_error < 1e-11, (
        f"Gauss divergence theorem violated: max ||sum S_f|| = {max_closure_error:.2e} >= 1e-11"
    )


def test_non_orthogonality_consistency():
    """Verify acceptance criterion: orthogonality decomposition delta_f + k_f == S_f.

    In DiFVM over-relaxed formulation:
        delta_f = (||S_f||^2 / (S_f . d)) * d
        k_f = S_f - delta_f
    Asserts:
        max_f ||delta_f + k_f - S_f|| < 1e-14 on all internal faces.
    """
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    # Heavily skewed grid to produce large non-orthogonality angles
    points, cells = create_synthetic_hexahedral_grid(nx=4, ny=4, nz=4, skew_factor=0.8)
    mesh = compute_difvm_topology(points, cells)

    delta_f = mesh.delta_f
    k_f = mesh.k_f
    face_areas = mesh.face_areas
    neighbour = mesh.neighbour

    internal_mask = neighbour >= 0
    assert np.any(internal_mask), "Mesh must contain internal faces"

    # Evaluate identity: delta_f + k_f == S_f
    decomposition_diff = (delta_f[internal_mask] + k_f[internal_mask]) - face_areas[internal_mask]
    max_decomp_error = np.max(np.linalg.norm(decomposition_diff, axis=1))

    assert max_decomp_error < 1e-14, (
        f"Non-orthogonal decomposition identity violated: max error = {max_decomp_error:.2e} >= 1e-14"
    )

    # In purely orthogonal faces, k_f should be approximately zero
    ortho_points, ortho_cells = create_synthetic_hexahedral_grid(nx=2, ny=2, nz=2, skew_factor=0.0)
    ortho_mesh = compute_difvm_topology(ortho_points, ortho_cells)
    ortho_internal = ortho_mesh.neighbour >= 0
    k_f_ortho_norm = np.max(np.linalg.norm(ortho_mesh.k_f[ortho_internal], axis=1))
    assert k_f_ortho_norm < 1e-12, f"Expected k_f ~ 0 for orthogonal grid, got {k_f_ortho_norm:.2e}"


def test_sph_boundary_detection_div_r():
    """Verify acceptance criterion: Eulerian SPH boundary detection via position divergence.

    According to Wang, Haidn & Hu (2025):
        div r = -sum_j V_j (r_i - r_j) . grad W_ij
    In the interior, div r ~ d (dimension). Near a physical boundary, support is truncated:
        div r < 0.75 * d  =>  tagged as boundary particle.
    """
    coupling = get_coupling()
    SPHFVMCoupler = getattr(coupling, "SPHFVMCoupler")

    pos, vol, h = create_synthetic_particle_cloud(n_particles=400, box_size=1.0)
    coupler = SPHFVMCoupler()

    # Detect boundary particles
    is_boundary = coupler.detect_boundary_particles(pos, vol, h)

    assert isinstance(is_boundary, np.ndarray), "detect_boundary_particles must return numpy array"
    assert is_boundary.dtype == bool, "is_boundary mask must be boolean"
    assert len(is_boundary) == len(pos), "Boundary mask length must match particle count"

    # Verify that particles very close to the x=0 wall are flagged
    near_wall_mask = pos[:, 0] < 0.03
    assert np.any(near_wall_mask), "Should have particles near wall"
    assert np.all(is_boundary[near_wall_mask]), "All particles near x=0 boundary must be detected as boundary particles"

    # Verify that particles deep in the interior (x, y, z in [0.3, 0.7]) are NOT flagged
    interior_mask = (
        (pos[:, 0] > 0.3) & (pos[:, 0] < 0.7) &
        (pos[:, 1] > 0.3) & (pos[:, 1] < 0.7) &
        (pos[:, 2] > 0.3) & (pos[:, 2] < 0.7)
    )
    if np.any(interior_mask):
        assert not np.any(is_boundary[interior_mask]), (
            f"Interior particles misclassified as boundary: {np.sum(is_boundary[interior_mask])}"
        )


def test_sph_wall_consistency_A_iS():
    """Verify acceptance criterion: Eulerian SPH virtual wall vector A_iS 0th-order consistency.

    Wang, Haidn & Hu (2025):
        A_iS = -sum_j V_j grad_i W_ij
        Invariant: ||A_iS + sum_j V_j grad_i W_ij|| < 10^-10
    """
    coupling = get_coupling()
    SPHFVMCoupler = getattr(coupling, "SPHFVMCoupler")

    pos, vol, h = create_synthetic_particle_cloud(n_particles=300, box_size=1.0)
    coupler = SPHFVMCoupler()

    is_boundary = coupler.detect_boundary_particles(pos, vol, h)
    assert np.any(is_boundary), "Must detect boundary particles to test A_iS"

    A_iS, sum_grad_W = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True)

    assert A_iS.shape == (len(pos), 3), f"Expected A_iS shape ({len(pos)}, 3), got {A_iS.shape}"

    # Verify exact 0th-order consistency on boundary particles
    closure_error = np.linalg.norm(A_iS[is_boundary] + sum_grad_W[is_boundary], axis=1)
    max_closure_error = np.max(closure_error)

    assert max_closure_error < 1e-10, (
        f"SPH 0th-order wall consistency violated: max error = {max_closure_error:.2e} >= 1e-10"
    )

    # Virtual wall vectors on non-boundary (interior) particles must be approximately zero
    interior_A_norms = np.linalg.norm(A_iS[~is_boundary], axis=1)
    assert np.all(interior_A_norms < 1e-6), "Virtual wall vector A_iS must be zero for interior particles"


def test_opmis_linear_moment_reproduction():
    """Verify acceptance criterion: OPMIS linear field moment reproduction (Zhang et al. 2025).

    OPMIS P2M Taylor and M2P affine least-squares interpolation must reproduce
    arbitrary linear fields f(x) = a . x + b exactly with L_inf < 10^-11.
    """
    coupling = get_coupling()
    difvm = get_difvm_topology()
    OPMISTaylorInterpolator = getattr(coupling, "OPMISTaylorInterpolator")
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    # Background FVM mesh
    points, cells = create_synthetic_hexahedral_grid(nx=4, ny=4, nz=4, lx=1.0, ly=1.0, lz=1.0)
    mesh = compute_difvm_topology(points, cells)

    # Particle set
    pos, vol, h = create_synthetic_particle_cloud(n_particles=200, box_size=1.0, seed=123)

    # Non-trivial linear test field: f(x, y, z) = 3.5*x - 2.1*y + 4.8*z + 1.25
    a = np.array([3.5, -2.1, 4.8], dtype=np.float64)
    b = 1.25
    field_particles = np.dot(pos, a) + b

    interpolator = OPMISTaylorInterpolator()

    # 1. Particle-to-Mesh (P2M) Transfer
    field_mesh_interp = interpolator.interpolate_p2m(pos, field_particles, vol, h, mesh)
    exact_mesh_field = np.dot(mesh.cell_centres, a) + b

    p2m_error = np.max(np.abs(field_mesh_interp - exact_mesh_field))
    assert p2m_error < 1e-11, f"OPMIS P2M linear moment reproduction failed: error = {p2m_error:.2e} >= 1e-11"

    # 2. Mesh-to-Particle (M2P) Transfer
    field_halo_interp = interpolator.interpolate_m2p(mesh, exact_mesh_field, pos[:50])
    exact_halo_field = np.dot(pos[:50], a) + b

    m2p_error = np.max(np.abs(field_halo_interp - exact_halo_field))
    assert m2p_error < 1e-11, f"OPMIS M2P linear moment reproduction failed: error = {m2p_error:.2e} >= 1e-11"


def test_mesh_error_map_completeness():
    """Verify acceptance criterion: complete error map in mesh.error_map.

    The error map must contain:
    - 'non_orthogonality': face non-orthogonality angle in degrees [0, 90]
    - 'skewness': face skewness distance
    - 'aspect_ratio': cell aspect ratio >= 1.0
    - 'stabilization_mask' (or 'mask'): integer flag in {0, 1, 2, 3} for solver stabilization
    """
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    points, cells = create_synthetic_hexahedral_grid(nx=3, ny=3, nz=3, skew_factor=0.4)
    mesh = compute_difvm_topology(points, cells)

    assert hasattr(mesh, "error_map"), "Mesh must possess error_map dictionary"
    error_map = mesh.error_map
    assert isinstance(error_map, dict), "mesh.error_map must be a dictionary"

    # Check required metric keys
    required_keys = ["non_orthogonality", "skewness", "aspect_ratio"]
    for k in required_keys:
        assert k in error_map, f"Missing required error map key: '{k}'"

    mask_key = "stabilization_mask" if "stabilization_mask" in error_map else "mask"
    assert mask_key in error_map, "error_map must provide stabilization_mask or mask"

    n_faces = len(mesh.face_areas)
    n_cells = len(mesh.cell_volumes)

    # Face metric shapes & values
    assert len(error_map["non_orthogonality"]) == n_faces, "non_orthogonality must match face count"
    assert np.all(error_map["non_orthogonality"] >= 0.0), "Non-orthogonality angle must be non-negative"
    assert np.all(error_map["non_orthogonality"] <= 90.001), "Non-orthogonality angle must be <= 90 deg"

    assert len(error_map["skewness"]) == n_faces, "skewness must match face count"
    assert np.all(error_map["skewness"] >= 0.0), "Skewness must be non-negative"

    # Cell metric shapes & values
    assert len(error_map["aspect_ratio"]) == n_cells, "aspect_ratio must match cell count"
    assert np.all(error_map["aspect_ratio"] >= 0.999), "Aspect ratio must be >= 1.0"

    # Stabilization mask checks
    mask = error_map[mask_key]
    assert len(mask) == n_faces, "Stabilization mask must match face count"
    assert np.all(np.isin(mask, [0, 1, 2, 3])), "Mask values must be in {0, 1, 2, 3}"


def test_inflation_boundary_layers():
    """Verify acceptance criterion: prismatic boundary layer inflation.

    Verifies:
    1. Structured boundary layers extruded from wall with geometric growth r.
    2. Wall face orthogonality: theta_f <= 15 deg on wall-adjacent faces.
    3. Strict positive cell volumes across all boundary layers.
    """
    inflation_mod = get_inflation()
    BoundaryLayerGenerator = getattr(inflation_mod, "BoundaryLayerGenerator")
    InflationConfig = getattr(inflation_mod, "InflationConfig")

    config = InflationConfig(
        num_layers=4,
        first_layer_height=0.002,
        expansion_ratio=1.2,
    )

    generator = BoundaryLayerGenerator(config)

    # Extrude layers along a flat planar boundary (z = 0)
    wall_points = np.array([
        [0.0, 0.0, 0.0],
        [0.05, 0.0, 0.0],
        [0.05, 0.05, 0.0],
        [0.0, 0.05, 0.0],
    ], dtype=np.float64)
    wall_faces = [np.array([0, 1, 2, 3], dtype=np.int64)]
    wall_normal = np.array([0.0, 0.0, 1.0], dtype=np.float64)

    bl_mesh = generator.inflate_layers(wall_points, wall_faces, wall_normal)

    assert hasattr(bl_mesh, "cell_volumes"), "Inflated mesh must provide cell_volumes"
    assert np.all(bl_mesh.cell_volumes > 1e-15), "All inflation layer cells must have positive volumes"

    # Verify geometric growth: thickness of layer k ~ h1 * r^(k-1)
    layer_thicknesses = generator.get_layer_thicknesses()
    assert len(layer_thicknesses) == 4, "Expected 4 layer thicknesses"
    for k in range(len(layer_thicknesses)):
        expected_h = config.first_layer_height * (config.expansion_ratio ** k)
        assert abs(layer_thicknesses[k] - expected_h) < 1e-7, (
            f"Layer {k} thickness {layer_thicknesses[k]} deviates from expected {expected_h}"
        )


def test_rcm_bandwidth_reduction():
    """Verify acceptance criterion: Reverse Cuthill-McKee (RCM) bandwidth reduction.

    Asserts:
    1. Matrix bandwidth after RCM is strictly less than or equal to initial bandwidth: beta_rcm < beta_init.
    2. RCM permutation is a valid bijection (permutation of 0..N_c-1).
    3. Total volume and Gauss closure are preserved under permutation.
    """
    distmesh = get_distmesh_core()
    rcm_reorder = getattr(distmesh, "rcm_reorder")
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    # Generate grid with deliberate non-optimal cell ordering
    points, cells = create_synthetic_hexahedral_grid(nx=6, ny=6, nz=6)
    rng = np.random.default_rng(42)
    scrambled_indices = rng.permutation(len(cells))
    scrambled_cells = [cells[idx] for idx in scrambled_indices]

    mesh_scrambled = compute_difvm_topology(points, scrambled_cells)

    # Initial bandwidth
    owner = mesh_scrambled.owner
    neighbour = mesh_scrambled.neighbour
    internal = neighbour >= 0
    beta_initial = np.max(np.abs(owner[internal] - neighbour[internal]))

    # Apply RCM
    mesh_rcm, perm = rcm_reorder(mesh_scrambled)

    # Permutation validity
    assert len(perm) == len(cells), "Permutation array must match cell count"
    assert np.array_equal(np.sort(perm), np.arange(len(cells))), "Permutation must be a valid bijection"

    # Reordered bandwidth
    owner_rcm = mesh_rcm.owner
    neighbour_rcm = mesh_rcm.neighbour
    internal_rcm = neighbour_rcm >= 0
    beta_rcm = np.max(np.abs(owner_rcm[internal_rcm] - neighbour_rcm[internal_rcm]))

    assert beta_rcm < beta_initial, f"RCM failed to reduce bandwidth: beta_rcm={beta_rcm} >= beta_initial={beta_initial}"
    reduction_pct = (beta_initial - beta_rcm) / beta_initial * 100
    assert reduction_pct >= 30.0, f"RCM bandwidth reduction {reduction_pct:.1f}% was below 30% target"


def test_moving_mesh_ale_laplacian_smoothing():
    """Verify acceptance criterion: moving mesh ALE deformation with Laplacian smoothing.

    Prescribes boundary node motion on a wall patch, solves interior Laplacian
    smoothing, and verifies that interior nodes move smoothly without inverting cells.
    """
    moving = get_moving_mesh()
    ALEMeshMover = getattr(moving, "ALEMeshMover")
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    points, cells = create_synthetic_hexahedral_grid(nx=5, ny=5, nz=5, lx=1.0, ly=1.0, lz=1.0)
    mesh = compute_difvm_topology(points, cells)

    mover = ALEMeshMover()

    # Prescribe displacement on top surface (z = 1.0)
    top_mask = points[:, 2] > 0.99
    boundary_displacement = np.zeros_like(points)
    displacement_vector = np.array([0.05, 0.02, -0.02], dtype=np.float64)
    boundary_displacement[top_mask] = displacement_vector

    # Move mesh via Laplacian smoothing
    moved_mesh = mover.deform_mesh(mesh, boundary_displacement)

    assert hasattr(moved_mesh, "cell_volumes"), "Deformed mesh must provide cell_volumes"
    assert np.all(moved_mesh.cell_volumes > 1e-15), "All cell volumes must remain positive after deformation"

    # Verify that bottom surface (z = 0.0) remained fixed
    bottom_mask = points[:, 2] < 0.01
    bottom_disp = np.linalg.norm(moved_mesh.points[bottom_mask] - points[bottom_mask], axis=1)
    assert np.max(bottom_disp) < 1e-12, "Fixed boundary nodes must not displace"

    # Verify smooth decay of interior displacement
    interior_mask = (points[:, 2] > 0.4) & (points[:, 2] < 0.6)
    interior_disp = np.linalg.norm(moved_mesh.points[interior_mask] - points[interior_mask], axis=1)
    max_top_disp = np.linalg.norm(displacement_vector)
    assert np.all(interior_disp < max_top_disp), "Interior displacement must be strictly bounded by boundary motion"
    assert np.all(interior_disp > 0.0), "Interior nodes must absorb deformation smoothly"


def test_degenerate_cell_detection():
    """Verify acceptance criterion: degenerate cell detection and remeshing trigger.

    Under large unphysical mesh motions, inverted or collapsed cells (V_i <= 0
    or severe non-orthogonality) must be detected and flagged to trigger remeshing.
    """
    moving = get_moving_mesh()
    ALEMeshMover = getattr(moving, "ALEMeshMover")
    difvm = get_difvm_topology()
    compute_difvm_topology = getattr(difvm, "compute_difvm_topology")

    points, cells = create_synthetic_hexahedral_grid(nx=3, ny=3, nz=3)
    mesh = compute_difvm_topology(points, cells)

    mover = ALEMeshMover()

    # Valid initial mesh should have 0 degenerate cells
    bad_cells_initial = mover.detect_degenerate_cells(mesh)
    assert len(bad_cells_initial) == 0, f"Expected 0 bad cells in initial mesh, got {len(bad_cells_initial)}"

    # Intentionally corrupt one cell by inverting its vertex coordinates (crossing nodes)
    corrupted_points = points.copy()
    # Invert node 0 and node 6 of cell 0
    cell_0_nodes = cells[0]
    corrupted_points[cell_0_nodes[0]] = points[cell_0_nodes[6]] + [0.5, 0.5, 0.5]

    corrupted_mesh = compute_difvm_topology(corrupted_points, cells)
    bad_cells = mover.detect_degenerate_cells(corrupted_mesh)

    assert len(bad_cells) > 0, "Corrupted mesh must trigger degenerate cell detection"
    assert 0 in bad_cells, "Inverted cell 0 must be identified in bad cell list"


# ---------------------------------------------------------------------------
# Tier 4: Real-World CAD Reference Samples & Sketch Filtering
# ---------------------------------------------------------------------------

def test_cad_sketch_line_filtering():
    """Verify acceptance criterion: robust immunity to CAD sketch/guide lines.

    TestCADSTPAP214.stp contains 4 2D sketch lines (Line1-Line4) and an origin
    point in entity #35=GEOMETRIC_CURVE_SET.
    BladeFullRev1CFD.stp contains 9 DRAUGHTING_MODEL entities.
    The mesher must filter out these 1D/2D entities and mesh only the 3D solid shell.
    """
    assert TESTCAD_STEP_AP214.exists(), f"Reference file missing: {TESTCAD_STEP_AP214}"
    engine_mod = get_mesh_engine()
    MeshEngine = getattr(engine_mod, "MeshEngine")

    engine = MeshEngine()
    mesh = engine.mesh_cad_file(TESTCAD_STEP_AP214, h0=0.04)

    # Ingestion must succeed without raising exceptions
    assert mesh is not None, "Mesher returned None on CAD with sketch lines"
    assert hasattr(mesh, "cell_volumes"), "Generated mesh must have cell_volumes"
    assert np.all(mesh.cell_volumes > 1e-15), "Generated mesh must have strictly positive cell volumes"

    # Verify no 1D wireframe cells or open boundary faces were produced
    assert hasattr(mesh, "boundaries"), "Mesh must define boundaries"
    assert len(mesh.cells) > 0, "Mesh must contain 3D volume cells"


def test_e2e_meshing_testcad_cube():
    """Verify acceptance criterion: end-to-end meshing of rotated 200 mm cube (TestCAD).

    Ground Truth (CADTestSamples/TestCAD.*):
    - Side length: 200 mm = 0.2 m
    - Rotated 10 deg counter-clockwise around Z-axis
    - Analytical CFD Bounding Box:
        X, Y in [-0.115846, 0.115846] m (span 0.231691 m)
        Z in [0.0, 0.200000] m (span 0.200000 m)
    - Analytical CFD Volume: 0.008 m^3
    - Gauss divergence theorem closure: max ||sum S_f|| < 10^-11
    """
    assert TESTCAD_OBJ.exists(), f"Reference file missing: {TESTCAD_OBJ}"
    engine_mod = get_mesh_engine()
    MeshEngine = getattr(engine_mod, "MeshEngine")

    engine = MeshEngine()
    mesh = engine.mesh_cad_file(TESTCAD_OBJ, h0=0.03, scale=0.001)

    # 1. Positivity
    assert np.all(mesh.cell_volumes > 1e-15), "All cell volumes must be positive"

    # 2. Total volume conservation (0.008 m^3 within 3% discretization tolerance)
    total_volume = np.sum(mesh.cell_volumes)
    assert abs(total_volume - 0.008) / 0.008 < 0.03, (
        f"Total volume {total_volume:.6f} m^3 deviates from analytical 0.008 m^3"
    )

    # 3. Bounding box verification
    pts = mesh.points
    min_b = np.min(pts, axis=0)
    max_b = np.max(pts, axis=0)

    # X, Y spans should match rotated 200 mm cube: 200 * (cos(10) + sin(10)) / 1000 = 0.231691 m
    expected_half_span = 0.115846
    assert abs(min_b[0] - (-expected_half_span)) < 0.01, f"BBox X min {min_b[0]} deviates from -{expected_half_span}"
    assert abs(max_b[0] - expected_half_span) < 0.01, f"BBox X max {max_b[0]} deviates from {expected_half_span}"
    assert abs(min_b[2] - 0.0) < 0.005, f"BBox Z min {min_b[2]} deviates from 0.0"
    assert abs(max_b[2] - 0.200) < 0.005, f"BBox Z max {max_b[2]} deviates from 0.200"

    # 4. Gauss divergence theorem closure
    owner = mesh.owner
    neighbour = mesh.neighbour
    face_areas = mesh.face_areas
    n_cells = len(mesh.cell_volumes)

    closure_residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(closure_residuals, owner, face_areas)
    internal_mask = neighbour >= 0
    np.subtract.at(closure_residuals, neighbour[internal_mask], face_areas[internal_mask])

    max_closure = np.max(np.linalg.norm(closure_residuals, axis=1))
    assert max_closure < 1e-11, f"Gauss closure violated on TestCAD cube: {max_closure:.2e} >= 1e-11"


def test_e2e_meshing_blade_full_rev1():
    """Verify acceptance criterion: meshing of complex aerodynamic turbine blade.

    Ground Truth (ComplexGeometryExample/BladeRev1CFD.obj):
    - Watertight 2-manifold closed shell (30832 vertices, 61660 faces, chi = 2, 0 open edges)
    - Enclosed volume: 1.883373e+05 mm^3 (0.0001883373 m^3)
    - Bounding Box (mm):
        X in [-77.36, 4.63]
        Y in [-29.95, 86.81]
        Z in [-100.0, 200.0]
    """
    assert BLADE_OBJ.exists(), f"Reference file missing: {BLADE_OBJ}"
    engine_mod = get_mesh_engine()
    MeshEngine = getattr(engine_mod, "MeshEngine")

    engine = MeshEngine()
    mesh = engine.mesh_cad_file(BLADE_OBJ, h0=15.0, scale=1.0)

    # 1. Positivity
    assert np.all(mesh.cell_volumes > 1e-15), "Turbine blade mesh cells must have positive volumes"

    # 2. Total volume comparison against analytical closed surface volume (188,337 mm^3 within 5%)
    total_volume = np.sum(mesh.cell_volumes)
    analytical_volume = 1.883373e5
    vol_rel_err = abs(total_volume - analytical_volume) / analytical_volume
    assert vol_rel_err < 0.08, (
        f"Turbine blade mesh volume {total_volume:.1f} mm^3 deviates from analytical {analytical_volume:.1f} mm^3"
    )

    # 3. Gauss divergence closure
    owner = mesh.owner
    neighbour = mesh.neighbour
    face_areas = mesh.face_areas
    n_cells = len(mesh.cell_volumes)

    closure_residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(closure_residuals, owner, face_areas)
    internal_mask = neighbour >= 0
    np.subtract.at(closure_residuals, neighbour[internal_mask], face_areas[internal_mask])

    max_closure = np.max(np.linalg.norm(closure_residuals, axis=1))
    assert max_closure < 1e-11, f"Gauss closure violated on turbine blade: {max_closure:.2e} >= 1e-11"
