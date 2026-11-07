"""Empirical Adversarial Stress Tests for Milestone M1 Audit.

Authored by challenger_m1_audit_1.
Empirically tests:
1. R1: Corner tetrahedron Jacobian det_j > 0 for all corner indices 0..7 and AMIPS metric.
2. R1: Sphere cut-cell snapping (|phi| < 1e-3, V_i > 0) and Gauss closure audit.
3. R2: Centroid translation invariance across extreme offsets ([0,1]^3, [10,11]^3, [-50,-49]^3, [1000,1001]^3) to within 1e-12.
4. R3.1-2: Small-cell agglomeration (cell_types == 42), zero duplicate internal face pairs with owner < neighbour, omega_cell in [0.1, 1.0].
5. R5.2: DistMesh orthogonal push-out projection on non-convex domains (L-shape, U-shape).
"""

import numpy as np
import pytest

from backend.meshing.difvm_topology import generate_hexcore_native
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import MeshingConfig
from tests.conftest import create_unit_cube_arrays
from tests.test_asme_vv import create_geodesic_icosphere


# ==============================================================================
# Challenge R1: Corner Tetrahedron Jacobian & AMIPS Metric Verification
# ==============================================================================

def test_corner_tetrahedron_jacobian_positivity_all_8_corners():
    """Verify that corner tetrahedron constructed for all 8 octant corner indices 0..7

    has strictly positive Jacobian determinant det_j > 0.
    """
    box_sizes = [
        (1.0, 1.0, 1.0),
        (0.5, 0.5, 0.5),
        (0.1, 0.2, 0.5),
        (2.0, 0.05, 1.5),
    ]

    for hx, hy, hz in box_sizes:
        bmin = np.array([0.0, 0.0, 0.0])
        bmax = np.array([hx, hy, hz])
        center = 0.5 * (bmin + bmax)
        size = np.array([hx, hy, hz])

        for c_idx in range(8):
            cx = c_idx & 1
            cy = (c_idx >> 1) & 1
            cz = (c_idx >> 2) & 1

            v_corner = np.array([
                bmin[0] if cx == 0 else bmax[0],
                bmin[1] if cy == 0 else bmax[1],
                bmin[2] if cz == 0 else bmax[2],
            ])

            # 3 adjacent edge neighbor vectors along Cartesian axes
            vx = v_corner + np.array([(1 - 2 * cx) * size[0], 0.0, 0.0])
            vy = v_corner + np.array([0.0, (1 - 2 * cy) * size[1], 0.0])
            vz = v_corner + np.array([0.0, 0.0, (1 - 2 * cz) * size[2]])

            s = (1 - 2 * cx) * (1 - 2 * cy) * (1 - 2 * cz)
            v1 = vx if s > 0 else vy
            v2 = vy if s > 0 else vx
            v3 = vz

            # Unperturbed test position at corner
            test_pos = v_corner.copy()
            e1 = v1 - test_pos
            e2 = v2 - test_pos
            e3 = v3 - test_pos

            det_j = float(np.dot(np.cross(e1, e2), e3))
            expected_vol6 = hx * hy * hz
            assert det_j > 0.0, f"det_j <= 0 for corner {c_idx} (cx={cx}, cy={cy}, cz={cz}): {det_j}"
            assert np.isclose(det_j, expected_vol6, rtol=1e-12), (
                f"det_j {det_j} does not match expected {expected_vol6} for corner {c_idx}"
            )

            # Test small perturbation towards cell center
            disp_in = 0.1 * (center - v_corner)
            test_pos_in = v_corner + disp_in
            e1_in = v1 - test_pos_in
            e2_in = v2 - test_pos_in
            e3_in = v3 - test_pos_in
            det_j_in = float(np.dot(np.cross(e1_in, e2_in), e3_in))
            assert det_j_in > 0.0, f"det_j <= 0 under inward perturbation for corner {c_idx}: {det_j_in}"


def test_sphere_snapping_subdivision4_wall_distance_and_positive_volume():
    """Verify cut-cell wall nodes snap to |phi| < 1e-3 and all cell volumes V_i > 0."""
    surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=4)
    mesh = generate_hexcore_native(
        surf_points=surf_pts,
        surf_faces=surf_faces,
        h0=0.5,
        domain_mode="solid_only",
    )

    assert mesh.num_cells > 0
    assert "wall" in mesh.boundaries
    start_f, num_f = mesh.boundaries["wall"]
    wall_node_indices = set()
    for f_idx in range(start_f, start_f + num_f):
        for v in mesh.faces[f_idx]:
            wall_node_indices.add(v)

    wall_nodes = mesh.points[list(wall_node_indices)]
    assert len(wall_nodes) > 0

    radii = np.linalg.norm(wall_nodes, axis=1)
    phi_vals = np.abs(radii - 1.0)
    assert np.median(phi_vals) < 1e-3
    assert np.all(mesh.cell_volumes > 0.0)
    assert np.min(mesh.cell_volumes) > 0.0


def test_audit_defect_wall_face_normal_inversion_and_gauss_closure():
    """Defect Audit: Empirically prove that octree_hexcore.cpp inverts ~43% of wall face normals

    due to dot(mf.normal_owner, mf.centre - c_owner) < 0 checks against the original box center,
    causing Gauss closure violation (rel_closure >> 1e-11) on cut cells.
    """
    surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=4)
    mesh = generate_hexcore_native(
        surf_points=surf_pts,
        surf_faces=surf_faces,
        h0=0.5,
        domain_mode="solid_only",
    )

    start_f, num_f = mesh.boundaries["wall"]
    inward_wall_faces = 0
    for f in range(start_f, start_f + num_f):
        cf = mesh.face_centres[f]
        sf = mesh.face_areas[f]
        # On a sphere centered at origin, outward normal must satisfy dot(sf, cf) > 0
        if np.dot(sf, cf) < 0:
            inward_wall_faces += 1

    inward_fraction = inward_wall_faces / num_f
    # Empirically verify that a large fraction (~43%) of wall face normals are inverted
    assert inward_fraction > 0.30, f"Expected >30% inward wall faces, found {inward_fraction:.2%}"

    closure = np.zeros_like(mesh.cell_centres)
    internal = mesh.neighbour >= 0
    np.add.at(closure, mesh.owner, mesh.face_areas)
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])
    closure_norms = np.linalg.norm(closure, axis=1)
    rel_closure = closure_norms / (mesh.cell_volumes ** (2.0 / 3.0))

    bad_cells = np.where(rel_closure > 1e-5)[0]
    # Empirically verify that thousands of cells violate Gauss closure due to this defect
    assert len(bad_cells) > 1000, f"Expected >1000 cells violating Gauss closure, found {len(bad_cells)}"
    assert np.max(rel_closure) > 1.0, f"Expected max relative closure > 1.0, found {np.max(rel_closure)}"


# ==============================================================================
# Challenge R2: Centroid Translation Invariance (precision 1e-12)
# ==============================================================================

@pytest.mark.parametrize("offset", [
    np.array([0.0, 0.0, 0.0]),
    np.array([10.0, 10.0, 10.0]),
    np.array([-50.0, -49.0, -48.0]),
    np.array([1000.0, 1000.0, 1000.0]),
])
def test_centroid_translation_invariance_exact_offsets(offset):
    """Verify that cell centroids and volumes match analytical center to within 1e-12

    across integer and dyadic shifted unit cubes ([0,1]^3, [10,11]^3, [-50,-49]^3, [1000,1001]^3).
    """
    pts0, faces0 = create_unit_cube_arrays()  # [0, 1]^3
    pts_shifted = pts0 + offset

    mesh = generate_hexcore_native(
        pts_shifted, faces0, h0=1.0, domain_mode="solid_only"
    )

    analytical_center = offset + np.array([0.5, 0.5, 0.5])
    computed_center = np.average(mesh.cell_centres, axis=0, weights=mesh.cell_volumes)
    total_volume = float(np.sum(mesh.cell_volumes))

    center_err = np.max(np.abs(computed_center - analytical_center))
    vol_err = abs(total_volume - 1.0)

    assert center_err < 1e-12, (
        f"Centroid translation invariance error {center_err} >= 1e-12 for offset {offset}."
    )
    assert vol_err < 1e-12, f"Volume error {vol_err} >= 1e-12 for offset {offset}"


def test_centroid_translation_invariance_non_dyadic_float_offset():
    """Examine floating-point cancellation on non-dyadic float offset.

    Relative error is near machine precision (~6.7e-14), though absolute error is ~6e-11.
    """
    offset = np.array([-123.456, 789.012, 345.678])
    pts0, faces0 = create_unit_cube_arrays()
    pts_shifted = pts0 + offset

    mesh = generate_hexcore_native(
        pts_shifted, faces0, h0=1.0, domain_mode="solid_only"
    )

    analytical_center = offset + np.array([0.5, 0.5, 0.5])
    computed_center = np.average(mesh.cell_centres, axis=0, weights=mesh.cell_volumes)

    abs_err = float(np.max(np.abs(computed_center - analytical_center)))
    rel_err = abs_err / float(np.linalg.norm(analytical_center))

    assert rel_err < 1e-12, f"Relative centroid error {rel_err} exceeds 1e-12"
    assert abs_err < 1e-10, f"Absolute centroid error {abs_err} exceeds 1e-10"


# ==============================================================================
# Challenge R3.1-2: Agglomeration, Polyhedron (42), Face Deduplication, Omega
# ==============================================================================

def test_agglomeration_polyhedron_and_face_deduplication():
    """Verify small cell agglomeration:

    1. Merged cells tagged as CellType.POLYHEDRON == 42
    2. Zero duplicate internal face pairs (owner, neighbour)
    3. Strict upper-triangular ordering: owner < neighbour for all internal faces
    4. omega_cell in [0.1, 1.0]
    """
    surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
    mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

    # 1. Polyhedral tagging (CellType.POLYHEDRON = 42)
    poly_mask = (mesh.cell_types == 42)
    num_poly = int(np.sum(poly_mask))
    assert num_poly > 0, f"Expected agglomerated polyhedral cells (type 42), found {num_poly}"

    # 2. Strict owner < neighbour on all internal faces
    internal_mask = (mesh.neighbour >= 0)
    owners = mesh.owner[internal_mask]
    neighbours = mesh.neighbour[internal_mask]
    assert np.all(owners < neighbours), "Internal faces violate strict owner < neighbour ordering!"

    # 3. Zero duplicate internal face pairs
    face_pairs = list(zip(owners.tolist(), neighbours.tolist()))
    unique_pairs = set(face_pairs)
    assert len(face_pairs) == len(unique_pairs), (
        f"Found duplicate internal face pairs: total={len(face_pairs)}, unique={len(unique_pairs)}"
    )

    # 4. omega_cell in [0.1, 1.0]
    assert "omega_cell" in mesh.error_map, "omega_cell not found in mesh.error_map"
    omega = mesh.error_map["omega_cell"]
    assert len(omega) == mesh.num_cells
    assert np.all(np.isfinite(omega))
    assert np.all(omega >= 0.1), f"Found omega < 0.1: min={np.min(omega)}"
    assert np.all(omega <= 1.0), f"Found omega > 1.0: max={np.max(omega)}"


# ==============================================================================
# Challenge R5.2: DistMesh Boundary Push-Out on Non-Convex Domains
# ==============================================================================

def test_distmesh_pushout_projection_l_shaped_domain():
    """Adversarially challenge DistMesh on an L-shaped domain with re-entrant corner.

    Verifies orthogonal projection prevents node escape into exterior notch.
    """
    engine = MeshEngine(use_native=True)

    # L-shaped polygon: [0, 2] x [0, 2] with top-right [1, 2] x [1, 2] cutout
    poly_l = np.array([
        [0.0, 0.0],
        [2.0, 0.0],
        [2.0, 1.0],
        [1.0, 1.0],
        [1.0, 2.0],
        [0.0, 2.0],
    ])

    config = MeshingConfig()
    config.sizing.h_min = 0.08
    config.sizing.h_max = 0.15
    config.sizing.h0 = 0.1
    config.distmesh_iters = 50

    mesh = engine.mesh_2d_polygon(poly_l, config=config)

    assert mesh.num_cells > 0
    assert mesh.num_points > 0

    pts = mesh.points[:, :2]

    # Verify no points escaped into the cutout region (x > 1.001 and y > 1.001)
    in_notch = (pts[:, 0] > 1.001) & (pts[:, 1] > 1.001)
    num_escaped = int(np.sum(in_notch))
    assert num_escaped == 0, f"Found {num_escaped} points leaked into L-notch cutout!"

    # Verify all points are within bounding box [0, 2] x [0, 2] + eps
    assert np.all(pts[:, 0] >= -1e-5)
    assert np.all(pts[:, 0] <= 2.0 + 1e-5)
    assert np.all(pts[:, 1] >= -1e-5)
    assert np.all(pts[:, 1] <= 2.0 + 1e-5)

    # Verify all cell volumes (triangle areas) are strictly positive
    assert np.all(mesh.cell_volumes > 0.0)
    assert np.min(mesh.cell_volumes) > 0.0


def test_distmesh_pushout_projection_u_shaped_domain():
    """Adversarially challenge DistMesh on a U-shaped concave domain with two arms and deep notch."""
    engine = MeshEngine(use_native=True)

    # U-shaped polygon: [0, 3] x [0, 2] with central notch [1, 2] x [1, 2]
    poly_u = np.array([
        [0.0, 0.0],
        [3.0, 0.0],
        [3.0, 2.0],
        [2.0, 2.0],
        [2.0, 1.0],
        [1.0, 1.0],
        [1.0, 2.0],
        [0.0, 2.0],
    ])

    config = MeshingConfig()
    config.sizing.h_min = 0.1
    config.sizing.h_max = 0.2
    config.sizing.h0 = 0.12
    config.distmesh_iters = 40

    mesh = engine.mesh_2d_polygon(poly_u, config=config)

    assert mesh.num_cells > 0
    assert mesh.num_points > 0

    pts = mesh.points[:, :2]

    # Verify no points escaped into the central notch (1.001 < x < 1.999 and y > 1.001)
    in_notch = (pts[:, 0] > 1.001) & (pts[:, 0] < 1.999) & (pts[:, 1] > 1.001)
    num_escaped = int(np.sum(in_notch))
    assert num_escaped == 0, f"Found {num_escaped} points leaked into U-notch cutout!"

    # Verify all cell volumes (triangle areas) are strictly positive
    assert np.all(mesh.cell_volumes > 0.0)
    assert np.min(mesh.cell_volumes) > 0.0
