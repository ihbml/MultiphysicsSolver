"""Tests for Phase 4 Numerics & Topology (Milestone M4_PHASE4).

Covers:
- H3: 2D Triangle Centroids & Models Check 7 planar moment balance
- H4: Explicit Polyhedral Topology & ALE Preservation (nv=8 coplanar, face-based adjacency)
- H2: Full Celik et al. (2008) GCI (divergence detection, iterative transcendental p, cell count r)
- H7: Supermesh-AMI Area-Conservative Weights (Sutherland-Hodgman clipping, partition of unity)
- H8: Physical SPH-FVM Coupling & Conservative P2M (boundary area vectors, volume conservation)
- M1: True Aspect Ratio (1.0 for unit cube, bounding box spans) in Python & Native
- M2: Background-Relative wsrd_alpha
"""

import math
import numpy as np
import pytest

from backend.meshing.models import UnstructuredMesh, CellType
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_python,
    compute_ami_weights,
    _classify_cell_type,
    _extract_cell_faces_py,
    get_native_dll,
)
from backend.meshing.moving_mesh import ALEMeshMover
from backend.meshing.gci import (
    compute_asme_vv20_gci,
    generate_3grid_gci_hierarchy,
    GCIHierarchy,
    GCIResult,
)
from backend.meshing.coupling import (
    SPHFVMCoupler,
    compute_fvm_boundary_area_vectors,
    interpolate_p2m_conservative,
    SPHKernel,
)


def is_native_available() -> bool:
    return get_native_dll(strict_native=False) is not None


# ---------------------------------------------------------------------------
# H3: 2D Triangle Centroids & Models Check 7
# ---------------------------------------------------------------------------

def test_h3_2d_quad_centroid_python():
    """Verify test quadrilateral (0,0)-(4,0)-(1,1)-(0,1) evaluates to exact centroid (1.4, 0.4)."""
    # 2D quad in XY plane (z=0)
    pts = np.array([
        [0.0, 0.0, 0.0],
        [4.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
    ], dtype=np.float64)
    cells = [np.array([0, 1, 2, 3], dtype=np.int64)]

    mesh = compute_difvm_topology_python(pts, cells)
    c = mesh.cell_centres[0]

    np.testing.assert_allclose(c[:2], [1.4, 0.4], atol=1e-12)
    assert abs(c[2]) < 1e-12


def test_h3_2d_quad_centroid_native():
    """Verify native implementation computes exact centroid (1.4, 0.4) for test quad."""
    if not is_native_available():
        pytest.skip("Native library not available")

    pts = np.array([
        [0.0, 0.0, 0.0],
        [4.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
    ], dtype=np.float64)
    cells = [np.array([0, 1, 2, 3], dtype=np.int64)]

    mesh = compute_difvm_topology(pts, cells, use_native=True)
    c = mesh.cell_centres[0]

    np.testing.assert_allclose(c[:2], [1.4, 0.4], atol=1e-12)
    assert abs(c[2]) < 1e-12


def test_h3_check7_planar_2d_z_nonzero():
    """Verify Check 7 in UnstructuredMesh.validate() passes for planar 2D mesh at z0 != 0."""
    z0 = 5.0
    pts = np.array([
        [0.0, 0.0, z0],
        [1.0, 0.0, z0],
        [1.0, 1.0, z0],
        [0.0, 1.0, z0],
    ], dtype=np.float64)
    cells = [np.array([0, 1, 2, 3], dtype=np.int64)]

    mesh = compute_difvm_topology(pts, cells, use_native=False)
    mesh.is_2d = True

    # validate() should pass without raising ValueError
    assert mesh.validate() is True


# ---------------------------------------------------------------------------
# H4: Explicit Polyhedral Topology & ALE Preservation
# ---------------------------------------------------------------------------

def test_h4_coplanar_octagon_classified_as_polygon():
    """Verify nv=8 coplanar 2D element is classified as POLYGON, not HEXAHEDRON."""
    theta = np.linspace(0, 2 * np.pi, 9)[:-1]
    pts = np.zeros((8, 3), dtype=np.float64)
    pts[:, 0] = np.cos(theta)
    pts[:, 1] = np.sin(theta)
    pts[:, 2] = 0.0  # Planar in XY

    node_indices = np.arange(8, dtype=np.int64)
    ctype = _classify_cell_type(node_indices, pts)
    assert ctype == CellType.POLYGON

    faces = _extract_cell_faces_py(node_indices, pts)
    # A 2D polygon with 8 vertices has 8 edge faces
    assert len(faces) == 8


def test_h4_ale_preserves_explicit_topology():
    """Verify ALEMeshMover.deform_mesh updates coordinates without wiping out topology."""
    # Create 2x2 2D mesh
    nx, ny = 3, 3
    x = np.linspace(0, 1, nx)
    y = np.linspace(0, 1, ny)
    xx, yy = np.meshgrid(x, y)
    pts = np.column_stack([xx.ravel(), yy.ravel(), np.zeros(nx * ny)])

    cells = []
    for j in range(ny - 1):
        for i in range(nx - 1):
            n0 = j * nx + i
            n1 = j * nx + (i + 1)
            n2 = (j + 1) * nx + (i + 1)
            n3 = (j + 1) * nx + i
            cells.append(np.array([n0, n1, n2, n3], dtype=np.int64))

    mesh = compute_difvm_topology(pts, cells, use_native=False)

    orig_num_faces = len(mesh.faces)
    orig_owner = mesh.owner.copy()
    orig_neighbour = mesh.neighbour.copy()
    orig_cell_types = mesh.cell_types.copy()
    orig_boundaries = dict(mesh.boundaries)

    mover = ALEMeshMover(stiffness_exponent=1.0)

    # Deform boundary nodes slightly (e.g., nodes at x=1)
    dx = np.zeros_like(pts)
    moving_nodes = np.where(pts[:, 0] > 0.99)[0]
    dx[moving_nodes, 0] = 0.05
    fixed_nodes = np.where(pts[:, 0] < 0.01)[0]

    deformed_mesh = mover.deform_mesh(mesh, dx, fixed_nodes=fixed_nodes)

    assert len(deformed_mesh.faces) == orig_num_faces
    np.testing.assert_array_equal(deformed_mesh.owner, orig_owner)
    np.testing.assert_array_equal(deformed_mesh.neighbour, orig_neighbour)
    np.testing.assert_array_equal(deformed_mesh.cell_types, orig_cell_types)
    assert deformed_mesh.boundaries == orig_boundaries
    # Points should have moved
    assert not np.allclose(deformed_mesh.points, pts)


# ---------------------------------------------------------------------------
# H2: Full Celik et al. (2008) GCI
# ---------------------------------------------------------------------------

def test_h2_celik_gci_divergence():
    """Verify divergence (|R| >= 1) sets convergence_type='divergence', apparent_order=nan, gci=inf."""
    f1, f2, f3 = 10.0, 5.0, 1.0  # eps_21 = -5.0, eps_32 = -4.0, R = 1.25 >= 1.0
    res = compute_asme_vv20_gci(f1, f2, f3, r21=2.0, r32=2.0)

    assert res.convergence_type == "divergence"
    assert math.isnan(res.apparent_order_p)
    assert math.isinf(res.gci_fine)
    assert math.isinf(res.gci_medium)


def test_h2_celik_gci_oscillatory_divergence():
    """Verify oscillatory divergence (R <= -1) sets convergence_type='divergence'."""
    f1, f2, f3 = 10.0, 2.0, 6.0  # eps_21 = -8.0, eps_32 = 4.0, R = -2.0 <= -1.0
    res = compute_asme_vv20_gci(f1, f2, f3, r21=2.0, r32=2.0)

    assert res.convergence_type == "divergence"
    assert math.isnan(res.apparent_order_p)
    assert math.isinf(res.gci_fine)


def test_h2_celik_gci_monotonic_uniform_r():
    """Verify standard monotonic convergence with r21 == r32 yields exact logarithmic p."""
    r = 2.0
    p_exact = 2.0
    f_exact = 1.0
    # phi = f_exact + C * h^p
    h1 = 0.25
    h2 = h1 * r
    h3 = h2 * r
    f1 = f_exact + 1.0 * (h1 ** p_exact)
    f2 = f_exact + 1.0 * (h2 ** p_exact)
    f3 = f_exact + 1.0 * (h3 ** p_exact)

    res = compute_asme_vv20_gci(f1, f2, f3, r21=r, r32=r)

    assert res.convergence_type == "monotonic"
    assert not math.isnan(res.apparent_order_p)
    assert abs(res.apparent_order_p - p_exact) < 1e-4
    assert abs(res.phi_extrapolated - f_exact) < 1e-4
    assert res.gci_fine > 0.0


def test_h2_celik_gci_transcendental_iteration():
    """Verify fixed-point iteration for non-uniform refinement ratios r21 != r32."""
    p_exact = 1.85
    f_exact = 2.5
    h1 = 0.1
    r21 = 1.45
    r32 = 1.35
    h2 = h1 * r21
    h3 = h2 * r32

    f1 = f_exact + 0.5 * (h1 ** p_exact)
    f2 = f_exact + 0.5 * (h2 ** p_exact)
    f3 = f_exact + 0.5 * (h3 ** p_exact)

    res = compute_asme_vv20_gci(f1, f2, f3, r21=r21, r32=r32)

    assert res.convergence_type == "monotonic"
    assert abs(res.apparent_order_p - p_exact) < 1e-3
    assert abs(res.phi_extrapolated - f_exact) < 1e-3


def test_h2_effective_r_from_cell_counts():
    """Verify generate_3grid_gci_hierarchy calculates r = (N1 / N2)^(1/dim)."""
    def make_box_mesh(h: float) -> UnstructuredMesh:
        n = max(1, int(round(1.0 / h)))
        x = np.linspace(0.0, 1.0, n + 1)
        y = np.linspace(0.0, 1.0, n + 1)
        z = np.linspace(0.0, 1.0, n + 1)
        grid = np.stack(np.meshgrid(x, y, z, indexing="ij"), axis=-1).reshape(-1, 3)

        def vid(i, j, k):
            return i * (n + 1) * (n + 1) + j * (n + 1) + k

        cells = []
        for i in range(n):
            for j in range(n):
                for k in range(n):
                    c = [
                        vid(i, j, k),
                        vid(i + 1, j, k),
                        vid(i + 1, j + 1, k),
                        vid(i, j + 1, k),
                        vid(i, j, k + 1),
                        vid(i + 1, j, k + 1),
                        vid(i + 1, j + 1, k + 1),
                        vid(i, j + 1, k + 1),
                    ]
                    cells.append(np.array(c, dtype=np.int64))
        return compute_difvm_topology_python(grid, cells)

    hierarchy = generate_3grid_gci_hierarchy(make_box_mesh, base_h0=0.25, r=1.41421356)
    expected_r21 = (hierarchy.fine.num_cells / hierarchy.medium.num_cells) ** (1.0 / 3.0)
    expected_r32 = (hierarchy.medium.num_cells / hierarchy.coarse.num_cells) ** (1.0 / 3.0)
    assert abs(hierarchy.r21 - expected_r21) < 1e-12
    assert abs(hierarchy.r32 - expected_r32) < 1e-12


# ---------------------------------------------------------------------------
# H7: Supermesh-AMI Area-Conservative Weights
# ---------------------------------------------------------------------------

def test_h7_supermesh_ami_partition_and_conservation():
    """Verify Supermesh-AMI weights satisfy sum_j w_ij = 1.0 and sum_j w_ij * A_i = A_i."""
    # Target face: full square [0, 1] x [0, 1] (Area = 1.0)
    target_centres = np.array([[0.5, 0.5, 0.0]])
    target_areas = np.array([[0.0, 0.0, 1.0]])
    target_polygons = [np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
    ])]

    # Source faces: 2 quads splitting the unit square horizontally: [0, 0.5] and [0.5, 1.0]
    source_centres = np.array([[0.5, 0.25, 0.0], [0.5, 0.75, 0.0]])
    source_areas = np.array([[0.0, 0.0, 0.5], [0.0, 0.0, 0.5]])
    source_polygons = [
        np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 0.5, 0.0], [0.0, 0.5, 0.0]]),
        np.array([[0.0, 0.5, 0.0], [1.0, 0.5, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]]),
    ]

    weights = compute_ami_weights(
        source_centres,
        source_areas,
        target_centres,
        target_areas,
        source_polygons=source_polygons,
        target_polygons=target_polygons,
    )

    # weights shape: (n_target, n_source) = (1, 2)
    # Target 0 receives 0.5 from source 0 and 0.5 from source 1
    assert abs(np.sum(weights[0]) - 1.0) < 1e-12
    assert abs(weights[0, 0] - 0.5) < 1e-12
    assert abs(weights[0, 1] - 0.5) < 1e-12


def test_h7_supermesh_ami_unequal_overlap():
    """Verify Sutherland-Hodgman clipping on unequal overlaps."""
    target_centres = np.array([[0.5, 0.5, 0.0]])
    target_areas = np.array([[0.0, 0.0, 1.0]])
    target_polygons = [np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
    ])]

    # Source face 0: Area 0.25; Source face 1: Area 0.75
    source_centres = np.array([[0.5, 0.125, 0.0], [0.5, 0.625, 0.0]])
    source_areas = np.array([[0.0, 0.0, 0.25], [0.0, 0.0, 0.75]])
    source_polygons = [
        np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 0.25, 0.0], [0.0, 0.25, 0.0]]),
        np.array([[0.0, 0.25, 0.0], [1.0, 0.25, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]]),
    ]

    weights = compute_ami_weights(
        source_centres,
        source_areas,
        target_centres,
        target_areas,
        source_polygons=source_polygons,
        target_polygons=target_polygons,
    )

    assert abs(np.sum(weights[0]) - 1.0) < 1e-12
    assert abs(weights[0, 0] - 0.25) < 1e-12
    assert abs(weights[0, 1] - 0.75) < 1e-12


# ---------------------------------------------------------------------------
# H8: Physical SPH-FVM Coupling & Conservative P2M
# ---------------------------------------------------------------------------

def test_h8_physical_sph_boundary_area_vectors():
    """Verify compute_fvm_boundary_area_vectors correctly accumulates wall face areas."""
    # Create simple mesh with 1 cell and 6 boundary faces (unit cube)
    pts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ])
    cells = [np.arange(8, dtype=np.int64)]
    mesh = compute_difvm_topology(pts, cells, use_native=False)

    # Particle near x=0 face
    x_p = np.array([[0.05, 0.5, 0.5]])
    kernel = SPHKernel(kernel_type="wendland")
    A_is = compute_fvm_boundary_area_vectors(x_p, h=0.2, mesh=mesh, kernel=kernel)

    assert A_is.shape == (1, 3)
    # The normal of the face at x=0 points outwards (-x direction in face_areas or towards boundary)
    assert np.linalg.norm(A_is[0]) > 0.0


def test_h8_conservative_p2m_volume_conservation():
    """Verify interpolate_p2m_conservative strictly conserves integral quantity sum V_c phi_c == sum V_p phi_p."""
    pts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ])
    cells = [np.arange(8, dtype=np.int64)]
    mesh = compute_difvm_topology(pts, cells, use_native=False)

    np.random.seed(42)
    n_particles = 20
    x_p = np.random.uniform(0.1, 0.9, size=(n_particles, 3))
    v_p = np.full(n_particles, 0.05)  # Particle volumes
    phi_p = np.random.uniform(1.0, 5.0, size=n_particles)  # Particle field

    phi_c = interpolate_p2m_conservative(x_p, phi_p, v_p, h=0.5, mesh=mesh)

    total_p = float(np.sum(v_p * phi_p))
    total_c = float(np.sum(mesh.cell_volumes * phi_c))

    assert abs(total_p - total_c) < 1e-12


# ---------------------------------------------------------------------------
# M1 & M2: True Aspect Ratio & Background-Relative wsrd_alpha
# ---------------------------------------------------------------------------

def test_m1_aspect_ratio_unit_cube_python():
    """Verify aspect ratio of a unit cube is exactly 1.0 (not sqrt(3))."""
    pts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ], dtype=np.float64)
    cells = [np.arange(8, dtype=np.int64)]

    mesh = compute_difvm_topology_python(pts, cells)
    assert abs(mesh.error_map["aspect_ratio"][0] - 1.0) < 1e-12


def test_m1_aspect_ratio_unit_cube_native():
    """Verify aspect ratio of a unit cube in native C++ is exactly 1.0."""
    if not is_native_available():
        pytest.skip("Native library not available")

    pts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ], dtype=np.float64)
    cells = [np.arange(8, dtype=np.int64)]

    mesh = compute_difvm_topology(pts, cells, use_native=True)
    assert abs(mesh.error_map["aspect_ratio"][0] - 1.0) < 1e-12


def test_m1_aspect_ratio_elongated_box():
    """Verify aspect ratio of a 2 x 1 x 1 box is 2.0 in both Python and Native."""
    pts = np.array([
        [0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [2.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [2.0, 0.0, 1.0], [2.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    ], dtype=np.float64)
    cells = [np.arange(8, dtype=np.int64)]

    mesh_py = compute_difvm_topology_python(pts, cells)
    assert abs(mesh_py.error_map["aspect_ratio"][0] - 2.0) < 1e-12

    if is_native_available():
        mesh_cpp = compute_difvm_topology(pts, cells, use_native=True)
        assert abs(mesh_cpp.error_map["aspect_ratio"][0] - 2.0) < 1e-12


def test_m2_wsrd_alpha_relative_to_background():
    """Verify wsrd_alpha scales with background volume rather than median."""
    # 2 cells: cell 0 has vol = 1.0, cell 1 has vol = 0.1
    pts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
        [1.1, 0.0, 0.0], [1.1, 1.0, 0.0], [1.1, 0.0, 1.0], [1.1, 1.0, 1.0],
    ], dtype=np.float64)
    cells = [
        np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64),  # volume 1.0
        np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64), # volume 0.1
    ]

    mesh_py = compute_difvm_topology_python(pts, cells, v_bg=1.0)
    # Cell 0: alpha = 1.0 / 1.0 = 1.0
    # Cell 1: alpha = 0.1 / 1.0 = 0.1
    np.testing.assert_allclose(mesh_py.error_map["wsrd_alpha"], [1.0, 0.1], atol=1e-6)

    if is_native_available():
        mesh_cpp = compute_difvm_topology(pts, cells, use_native=True)
        # Background reference is max positive volume (1.0)
        np.testing.assert_allclose(mesh_cpp.error_map["wsrd_alpha"], [1.0, 0.1], atol=1e-6)
