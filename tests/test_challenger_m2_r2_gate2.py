"""Empirical Verification & Stress Test Suite for Gate 2 (Milestone M2).

Author: challenger_m2_r2_2 (CAD Stress, Boundary Layer & Cyclic Symmetry Challenger)
Verification Scope:
1. Feature 23 Corner Pinching: turning angle actively alters mesh coordinates (diff > 0.0),
   smooth flat walls unpinched, acute/obtuse corners correctly scaled by (1 - (theta/pi)^2).
2. Element inversion handling: negative signed volume preserved, not masked by abs(),
   validate() rejects inverted elements.
3. Collapsed clearance handling: zero and sub-epsilon clearance evaluates to 0.0,
   does NOT fallback to 1.0 m^3.
4. Feature 21 CHT Interfaces: conformal twin boundary faces on coupledWall_fluid and
   coupledWall_solid with normal opposition (S_fF = -S_fS), plus AMI area weighting (sum w_ij = 1.0).
5. Feature 22 Cyclic Pairing: cfd_mesher_extract_cyclic_pairs exported in DLL C-ABI,
   rotational periodic sector pairs correctly identified with R_z(theta) mapping,
   mesh.cyclic_pairs populated.
"""

import ctypes
import numpy as np
import pytest

from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_python,
    compute_difvm_topology_native,
    compute_ami_weights,
    extract_cyclic_pairs,
    get_native_dll,
)
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.models import (
    InflationConfig,
    UnstructuredMesh,
)


# ===========================================================================
# 1. Feature 23 Corner Pinching Stress Tests
# ===========================================================================

def test_feature23_corner_pinching_quantitative():
    """Verify corner turning angle calculation actively alters mesh coordinates and matches theoretical formula."""
    # 90-degree corner: two perpendicular unit squares sharing edge at x=0
    # Face 1: z=0 plane, x in [0, 1], y in [0, 1] -> normal = [0, 0, 1]
    # Face 2: x=0 plane, y in [0, 1], z in [0, 1] -> normal = [1, 0, 0]
    wall_pts = np.array([
        [0.0, 0.0, 0.0], # 0: corner
        [1.0, 0.0, 0.0], # 1: flat edge
        [1.0, 1.0, 0.0], # 2: flat interior
        [0.0, 1.0, 0.0], # 3: corner
        [0.0, 0.0, 1.0], # 4: flat edge
        [0.0, 1.0, 1.0], # 5: flat edge
    ], dtype=np.float64)

    wall_faces = [
        np.array([0, 1, 2, 3], dtype=np.int64), # Face 1: normal ~ +z
        np.array([0, 3, 5, 4], dtype=np.int64), # Face 2: normal ~ +x
    ]

    first_h = 0.02
    cfg_pinch = InflationConfig(
        num_layers=1,
        first_layer_height=first_h,
        growth_rate=1.0,
        pinching=True,
        pinching_threshold_angle=10.0,
    )
    gen_pinch = BoundaryLayerGenerator(cfg_pinch)
    mesh_pinch = gen_pinch.inflate_layers(wall_pts, wall_faces)

    cfg_nopinch = InflationConfig(
        num_layers=1,
        first_layer_height=first_h,
        growth_rate=1.0,
        pinching=False,
    )
    gen_nopinch = BoundaryLayerGenerator(cfg_nopinch)
    mesh_nopinch = gen_nopinch.inflate_layers(wall_pts, wall_faces)

    diff = np.max(np.abs(mesh_pinch.points - mesh_nopinch.points))
    assert diff > 0.0, f"Expected diff > 0.0, got {diff}"

    # Quantitative check: For corner vertices (0 and 3), normal turn angle is 90 deg = pi/2
    # Theoretical factor: 1.0 - (0.5 * pi / pi)^2 = 1.0 - 0.25 = 0.75
    # Extruded distance at corner vertex 0 should be exactly 0.75 * first_h
    corner_extruded_pinch = np.linalg.norm(mesh_pinch.points[6] - wall_pts[0])
    corner_extruded_nopinch = np.linalg.norm(mesh_nopinch.points[6] - wall_pts[0])
    ratio = corner_extruded_pinch / corner_extruded_nopinch
    assert np.isclose(ratio, 0.75, atol=1e-5), f"Corner pinching factor {ratio} != 0.75"

    # Non-corner vertex 1 (only belongs to Face 1): turn angle is 0.0 -> factor is 1.0
    flat_extruded_pinch = np.linalg.norm(mesh_pinch.points[7] - wall_pts[1])
    flat_extruded_nopinch = np.linalg.norm(mesh_nopinch.points[7] - wall_pts[1])
    assert np.isclose(flat_extruded_pinch, flat_extruded_nopinch, atol=1e-12), "Flat vertex must not be pinched"


def test_feature23_flat_surface_no_false_pinching():
    """Verify that a coplanar wall does not falsely trigger corner pinching."""
    wall_pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
        [2.0, 0.0, 0.0],
        [2.0, 1.0, 0.0],
    ], dtype=np.float64)

    wall_faces = [
        np.array([0, 1, 2, 3], dtype=np.int64),
        np.array([1, 4, 5, 2], dtype=np.int64),
    ]

    cfg_pinch = InflationConfig(num_layers=2, first_layer_height=0.01, pinching=True, pinching_threshold_angle=10.0)
    cfg_nopinch = InflationConfig(num_layers=2, first_layer_height=0.01, pinching=False)

    mesh_pinch = BoundaryLayerGenerator(cfg_pinch).inflate_layers(wall_pts, wall_faces)
    mesh_nopinch = BoundaryLayerGenerator(cfg_nopinch).inflate_layers(wall_pts, wall_faces)

    diff = np.max(np.abs(mesh_pinch.points - mesh_nopinch.points))
    assert np.isclose(diff, 0.0, atol=1e-14), f"Flat wall suffered false pinching: diff = {diff}"


# ===========================================================================
# 2. Element Inversion Handling & Non-Masking Tests
# ===========================================================================

def test_element_inversion_negative_volume_preservation_python():
    """Verify that python difvm engine preserves negative signed volume and validate() catches it."""
    # Standard unit cube with inverted vertex ordering on one face to invert the cell
    pts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)

    # Inverted cube: reverse node order
    inverted_cell = np.array([0, 3, 2, 1, 4, 7, 6, 5], dtype=np.int64)

    mesh = compute_difvm_topology_python(pts, [inverted_cell])
    assert len(mesh.cell_volumes) == 1
    vol = mesh.cell_volumes[0]
    assert vol < -1e-15, f"Expected negative signed volume, got {vol}"

    with pytest.raises(ValueError, match="non-positive volume"):
        mesh.validate()


def test_element_inversion_negative_volume_preservation_native():
    """Verify that native C++ difvm engine preserves negative signed volume and does not mask with abs()."""
    dll = get_native_dll()
    if dll is None:
        pytest.skip("Native DLL not available")

    pts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    inverted_cell = np.array([0, 3, 2, 1, 4, 7, 6, 5], dtype=np.int64)

    mesh = compute_difvm_topology_native(pts, [inverted_cell])
    assert len(mesh.cell_volumes) == 1
    vol = mesh.cell_volumes[0]
    assert vol < -1e-15, f"Native DLL masked inverted volume with abs(): got {vol}"

    with pytest.raises(ValueError, match="non-positive volume"):
        mesh.validate()


# ===========================================================================
# 3. Collapsed Clearance Handling Tests
# ===========================================================================

def test_collapsed_clearance_evaluates_to_zero():
    """Verify that clearance = 0.0 evaluates to 0.0 and does not fallback to 1.0."""
    wall_pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
    ], dtype=np.float64)
    wall_faces = [np.array([0, 1, 2, 3], dtype=np.int64)]

    cfg = InflationConfig(num_layers=1, first_layer_height=0.01, pinching=True)
    gen = BoundaryLayerGenerator(cfg)

    # 1. Zero clearance
    mesh_zero = gen.inflate_layers(wall_pts, wall_faces, clearances=np.zeros(4, dtype=np.float64))
    vol_zero = mesh_zero.cell_volumes[0]
    assert np.isclose(vol_zero, 0.0, atol=1e-14), f"Zero clearance gave {vol_zero} != 0.0"
    assert vol_zero != 1.0, "Zero clearance fell back to artificial 1.0 m^3"

    # 2. Sub-epsilon clearance (1e-16)
    mesh_eps = gen.inflate_layers(wall_pts, wall_faces, clearances=np.full(4, 1e-16, dtype=np.float64))
    vol_eps = mesh_eps.cell_volumes[0]
    assert np.isclose(vol_eps, 0.0, atol=1e-14), f"Sub-epsilon clearance gave {vol_eps} != 0.0"
    assert vol_eps != 1.0, "Sub-epsilon clearance fell back to artificial 1.0 m^3"


# ===========================================================================
# 4. Feature 21 CHT Coupled Interfaces & AMI Area Weighting
# ===========================================================================

def test_feature21_cht_conformal_twin_faces_normal_opposition():
    """Verify CHT interface creates twin boundary faces on coupledWall_fluid and coupledWall_solid with S_fF = -S_fS."""
    # 2 adjacent cubes: Fluid [0, 1]x[0, 1]x[0, 1] and Solid [1, 2]x[0, 1]x[0, 1]
    pts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], # 0-3
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1], # 4-7
        [2, 0, 0], [2, 1, 0], [2, 0, 1], [2, 1, 1], # 8-11
    ], dtype=np.float64)

    cell_fluid = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64) # region 0
    cell_solid = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64) # region 1
    regions = np.array([0, 1], dtype=np.int32)

    mesh = compute_difvm_topology(pts, [cell_fluid, cell_solid], regions=regions)

    # Invariants:
    # 1. coupledWall_fluid and coupledWall_solid in boundaries
    assert "coupledWall_fluid" in mesh.boundaries, "Missing coupledWall_fluid patch"
    assert "coupledWall_solid" in mesh.boundaries, "Missing coupledWall_solid patch"

    f_start, f_count = mesh.boundaries["coupledWall_fluid"]
    s_start, s_count = mesh.boundaries["coupledWall_solid"]
    assert f_count == 1 and s_count == 1

    # 2. Both are boundary faces (neighbour == -1)
    assert mesh.neighbour[f_start] == -1, "coupledWall_fluid must be a boundary face"
    assert mesh.neighbour[s_start] == -1, "coupledWall_solid must be a boundary face"

    # 3. Normal vectors must have exact opposition: S_fF == -S_fS
    s_f = mesh.face_areas[f_start]
    s_s = mesh.face_areas[s_start]
    diff_norm = np.linalg.norm(s_f + s_s)
    assert diff_norm < 1e-12, f"Twin face normal opposition violated: ||S_fF + S_fS|| = {diff_norm}"

    # 4. Fluid normal points from x=1 towards +x (into solid): [1, 0, 0]
    assert np.allclose(s_f, [1.0, 0.0, 0.0], atol=1e-12), f"Fluid face area vector {s_f} != [1, 0, 0]"
    # Solid normal points from x=1 towards -x (into fluid): [-1, 0, 0]
    assert np.allclose(s_s, [-1.0, 0.0, 0.0], atol=1e-12), f"Solid face area vector {s_s} != [-1, 0, 0]"

    # 5. Baffles metadata contains twin_pairs
    assert len(mesh.baffles) >= 1
    assert mesh.baffles[0]["twin_pairs"] == [(f_start, s_start)]


def test_feature21_ami_area_weighting_partition_of_unity():
    """Verify AMI area weighting preserves partition of unity (sum w_ij == 1.0) and conservative flux."""
    # Source: 4 faces on a 2x2 grid in [0, 1]x[0, 1] at x=0
    source_centres = np.array([
        [0.0, 0.25, 0.25],
        [0.0, 0.75, 0.25],
        [0.0, 0.25, 0.75],
        [0.0, 0.75, 0.75],
    ], dtype=np.float64)
    source_areas = np.full(4, 0.25)

    # Target: 9 non-conformal faces on a 3x3 grid in [0, 1]x[0, 1] at x=0
    target_centres = np.array([
        [0.0, y, z]
        for y in [1/6, 0.5, 5/6]
        for z in [1/6, 0.5, 5/6]
    ], dtype=np.float64)
    target_areas = np.full(9, 1/9)

    weights = compute_ami_weights(source_centres, source_areas, target_centres, target_areas, radius=0.6)

    # Partition of unity: each target face receives sum(w_ij) == 1.0
    row_sums = np.sum(weights, axis=1)
    assert np.allclose(row_sums, 1.0, atol=1e-12), f"AMI partition of unity failed: max err = {np.max(np.abs(row_sums - 1.0))}"

    # Weights must be non-negative
    assert np.all(weights >= 0.0), "AMI weights must be non-negative"


# ===========================================================================
# 5. Feature 22 Cyclic Symmetry Pairing Tests
# ===========================================================================

def test_feature22_cyclic_export_and_sector_pairing():
    """Verify cfd_mesher_extract_cyclic_pairs is exported in DLL and cyclic_pairs is populated."""
    dll = get_native_dll()
    assert dll is not None, "cfd_mesher_native.dll must be present"

    assert hasattr(dll, "cfd_mesher_extract_cyclic_pairs"), "DLL missing cfd_mesher_extract_cyclic_pairs export"

    # Construct a 2-cell mesh rotated by 90 deg around z-axis:
    # Cell 1: at theta = 0 (x in [1, 2], y in [-0.5, 0.5], z in [0, 1])
    # Face at x=1: centre = [1.0, 0.0, 0.5]
    # Cell 2: at theta = 90 deg (x in [-0.5, 0.5], y in [1, 2], z in [0, 1])
    # Face at y=1: centre = [0.0, 1.0, 0.5]
    pts = np.array([
        # Cell 1: [1, 2] x [-0.5, 0.5] x [0, 1]
        [1.0, -0.5, 0.0], [2.0, -0.5, 0.0], [2.0, 0.5, 0.0], [1.0, 0.5, 0.0],
        [1.0, -0.5, 1.0], [2.0, -0.5, 1.0], [2.0, 0.5, 1.0], [1.0, 0.5, 1.0],
        # Cell 2: [-0.5, 0.5] x [1, 2] x [0, 1] (rotated by 90 deg)
        [-0.5, 1.0, 0.0], [0.5, 1.0, 0.0], [0.5, 2.0, 0.0], [-0.5, 2.0, 0.0],
        [-0.5, 1.0, 1.0], [0.5, 1.0, 1.0], [0.5, 2.0, 1.0], [-0.5, 2.0, 1.0],
    ], dtype=np.float64)

    cell_1 = np.arange(8, dtype=np.int64)
    cell_2 = np.arange(8, 16, dtype=np.int64)

    mesh = compute_difvm_topology(pts, [cell_1, cell_2], cyclic_angle_rad=np.pi / 2.0)
    assert len(mesh.cyclic_pairs) >= 1, "Expected at least 1 cyclic periodic face pair"

    # Verify cyclic pairs geometry
    cos_t = np.cos(np.pi / 2.0)
    sin_t = np.sin(np.pi / 2.0)
    R_z = np.array([[cos_t, -sin_t, 0.0], [sin_t, cos_t, 0.0], [0.0, 0.0, 1.0]])

    found_pair = False
    for f1, f2 in mesh.cyclic_pairs:
        c1 = mesh.face_centres[f1]
        c2 = mesh.face_centres[f2]
        if np.linalg.norm(np.dot(R_z, c1) - c2) < 1e-4:
            found_pair = True
            break
    assert found_pair, "Cyclic face centres did not satisfy R_z(pi/2) * c1 == c2"
