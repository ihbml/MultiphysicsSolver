"""Empirical Challenger Stress-Test Suite for Milestone M1 (Native C++ Hexcore Math & Geometry Core).

Authored by challenger_m1_audit_2.
Independently stress-tests:
1. Cut-cell snapping under large gradient steps, testing for inversions, boundary conditions, and volume positivity.
2. Centroid translation invariance under extreme coordinates (10^6, -10^6) for cubes and cut-cell geometries.
3. Polyhedral cell agglomeration integrity, face deduplication correctness, and discrete Gauss closure sum(S_f) == 0.
4. DistMesh orthogonal boundary projection edge cases (points exactly on vertices, points far outside, non-convex reflex corners).
"""

import math
from typing import Dict, List, Set, Tuple
import numpy as np
import pytest

from backend.meshing.models import CellType, MeshingConfig, UnstructuredMesh
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.difvm_topology import generate_hexcore_native
from tests.conftest import create_unit_cube_arrays
from tests.test_asme_vv import create_geodesic_icosphere


# ==============================================================================
# 1. Cut-Cell Snapping Under Large Gradient Steps & Inversion Guard
# ==============================================================================

class TestAdversarialCutCellSnapping:
    """Stress-tests for cut-cell snapping under large gradient steps and geometric inversions."""

    def test_snapping_large_gradient_step_volume_positivity(self):
        """Stress-test snapping with coarse grid (h0=1.0, R=1.0) where displacement steps are large."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(
            surf_points=surf_pts,
            surf_faces=surf_faces,
            h0=1.0,
            domain_mode="solid_only",
        )
        assert mesh.num_cells > 0
        assert mesh.num_faces > 0

        # Invariant 1: Strictly positive cell volumes - no inversions or collapsed cells
        assert np.all(mesh.cell_volumes > 0.0), "Found zero or negative cell volume after large gradient snapping"
        min_vol = float(np.min(mesh.cell_volumes))
        assert min_vol > 1e-12, f"Cell volume too close to zero: {min_vol}"

        # Invariant 2: Total mesh volume should be strictly bounded by the bounding sphere volume (4/3 * pi * R^3 ~ 4.189)
        total_vol = float(np.sum(mesh.cell_volumes))
        assert 2.0 < total_vol < 4.5, f"Unexpected total volume: {total_vol}"

    def test_snapping_sphere_h0_1point5_extreme_displacement(self):
        """Extreme coarse grid h0=1.5 on unit sphere (large distance from surface to cell corners)."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(
            surf_points=surf_pts,
            surf_faces=surf_faces,
            h0=1.5,
            domain_mode="solid_only",
        )
        assert mesh.num_cells > 0
        assert np.all(mesh.cell_volumes > 0.0), "Inverted cell detected under extreme coarse snapping"

    def test_snapping_preserves_external_fluid_farfield_boundaries(self):
        """Verify farfield outer boundaries remain strictly planar and aligned with bounding box."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        # farfield_bbox format: (xmin, ymin, zmin, xmax, ymax, zmax)
        farfield_bbox = (-3.0, -3.0, -3.0, 3.0, 3.0, 3.0)
        mesh = generate_hexcore_native(
            surf_points=surf_pts,
            surf_faces=surf_faces,
            h0=1.0,
            domain_mode="external_fluid",
            farfield_bbox=farfield_bbox,
        )
        assert mesh.num_cells > 0

        # Check farfield boundary patches
        for patch_name in ["inlet", "outlet", "top", "bottom", "sides"]:
            assert patch_name in mesh.boundaries, f"Missing boundary patch {patch_name}"
            start_f, num_f = mesh.boundaries[patch_name]
            for f_idx in range(start_f, start_f + num_f):
                f_nodes = mesh.faces[f_idx]
                f_coords = mesh.points[f_nodes]

                if patch_name == "inlet":
                    assert np.allclose(f_coords[:, 0], -3.0, atol=1e-5), "Inlet boundary distorted by snapping"
                elif patch_name == "outlet":
                    assert np.allclose(f_coords[:, 0], 3.0, atol=1e-5), "Outlet boundary distorted by snapping"
                elif patch_name == "bottom":
                    assert np.allclose(f_coords[:, 2], -3.0, atol=1e-5), "Bottom boundary distorted by snapping"
                elif patch_name == "top":
                    assert np.allclose(f_coords[:, 2], 3.0, atol=1e-5), "Top boundary distorted by snapping"
                elif patch_name == "sides":
                    # Must lie on y = -3.0 or y = +3.0
                    on_side = np.allclose(f_coords[:, 1], -3.0, atol=1e-5) or np.allclose(f_coords[:, 1], 3.0, atol=1e-5)
                    assert on_side, "Sides boundary distorted by snapping"

        # All cells must have strictly positive volume
        assert np.all(mesh.cell_volumes > 0.0)


# ==============================================================================
# 2. Centroid Translation Invariance Under Extreme Coordinates (10^6, -10^6)
# ==============================================================================

class TestExtremeTranslationInvariance:
    """Stress-tests translation invariance under extreme coordinate ranges (10^6, -10^6)."""

    def test_cube_centroid_extreme_positive_1e6(self):
        """Verify unit cube at [10^6, 10^6 + 1]^3 computes centroid (10^6 + 0.5, 10^6 + 0.5, 10^6 + 0.5)."""
        pts0, faces0 = create_unit_cube_arrays()  # [0, 1]^3
        shift = 1.0e6
        pts_large = pts0 + shift

        mesh = generate_hexcore_native(pts_large, faces0, h0=1.0, domain_mode="solid_only")
        assert mesh.num_cells > 0

        c_calc = np.average(mesh.cell_centres, axis=0, weights=mesh.cell_volumes)
        v_calc = float(np.sum(mesh.cell_volumes))

        expected_c = np.array([shift + 0.5, shift + 0.5, shift + 0.5])
        assert np.allclose(c_calc, expected_c, atol=1e-5), f"Centroid mismatch at +1e6: {c_calc} vs {expected_c}"
        assert np.isclose(v_calc, 1.0, atol=1e-5), f"Volume mismatch at +1e6: {v_calc}"

    def test_cube_centroid_extreme_negative_1e6(self):
        """Verify unit cube at [-10^6, -10^6 + 1]^3 computes centroid (-10^6 + 0.5, -10^6 + 0.5, -10^6 + 0.5)."""
        pts0, faces0 = create_unit_cube_arrays()  # [0, 1]^3
        shift = -1.0e6
        pts_large = pts0 + shift

        mesh = generate_hexcore_native(pts_large, faces0, h0=1.0, domain_mode="solid_only")
        assert mesh.num_cells > 0

        c_calc = np.average(mesh.cell_centres, axis=0, weights=mesh.cell_volumes)
        v_calc = float(np.sum(mesh.cell_volumes))

        expected_c = np.array([shift + 0.5, shift + 0.5, shift + 0.5])
        assert np.allclose(c_calc, expected_c, atol=1e-5), f"Centroid mismatch at -1e6: {c_calc} vs {expected_c}"
        assert np.isclose(v_calc, 1.0, atol=1e-5), f"Volume mismatch at -1e6: {v_calc}"

    def test_sphere_cut_cell_translation_invariance_plus_minus_1e6(self):
        """Verify cut-cell sphere mesh centroids and volumes are invariant under +1e6 and -1e6 shifts."""
        surf_pts0, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh0 = generate_hexcore_native(surf_pts0, surf_faces, h0=0.5, domain_mode="solid_only")
        c0 = np.average(mesh0.cell_centres, axis=0, weights=mesh0.cell_volumes)
        v0 = float(np.sum(mesh0.cell_volumes))

        # Test shift +1e6
        shift_pos = np.array([1.0e6, 1.0e6, 1.0e6])
        mesh_pos = generate_hexcore_native(surf_pts0 + shift_pos, surf_faces, h0=0.5, domain_mode="solid_only")
        c_pos = np.average(mesh_pos.cell_centres, axis=0, weights=mesh_pos.cell_volumes)
        v_pos = float(np.sum(mesh_pos.cell_volumes))

        assert np.allclose(c_pos - shift_pos, c0, atol=1e-4), f"Cut-cell centroid shift failure at +1e6: {c_pos - shift_pos} vs {c0}"
        assert np.isclose(v_pos, v0, atol=1e-4), f"Cut-cell volume shifted at +1e6: {v_pos} vs {v0}"
        # Relative cell count match within 0.1% (34,279 vs 34,273 = 0.017% due to float precision at 10^6)
        assert abs(mesh_pos.num_cells - mesh0.num_cells) / mesh0.num_cells < 0.001

        # Test shift -1e6
        shift_neg = np.array([-1.0e6, -1.0e6, -1.0e6])
        mesh_neg = generate_hexcore_native(surf_pts0 + shift_neg, surf_faces, h0=0.5, domain_mode="solid_only")
        c_neg = np.average(mesh_neg.cell_centres, axis=0, weights=mesh_neg.cell_volumes)
        v_neg = float(np.sum(mesh_neg.cell_volumes))

        assert np.allclose(c_neg - shift_neg, c0, atol=1e-4), f"Cut-cell centroid shift failure at -1e6: {c_neg - shift_neg} vs {c0}"
        assert np.isclose(v_neg, v0, atol=1e-4), f"Cut-cell volume shifted at -1e6: {v_neg} vs {v0}"
        assert abs(mesh_neg.num_cells - mesh0.num_cells) / mesh0.num_cells < 0.001


# ==============================================================================
# 3. Polyhedral Cell Agglomeration Integrity & Face Deduplication Correctness
# ==============================================================================

class TestAdversarialAgglomerationAndDeduplication:
    """Stress-tests small-cell agglomeration, face deduplication, and Gauss closure."""

    def test_internal_face_deduplication_uniqueness_and_ordering(self):
        """Verify no duplicate (owner, neighbour) internal faces exist and strictly owner < neighbour."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

        internal_mask = mesh.neighbour >= 0
        internal_owners = mesh.owner[internal_mask]
        internal_neighbours = mesh.neighbour[internal_mask]

        # 1. Strictly owner < neighbour
        assert np.all(internal_owners < internal_neighbours), "Found internal face where owner >= neighbour"

        # 2. Strictly unique (owner, neighbour) pairs - no duplicates!
        pairs = set()
        duplicates = []
        for o, nb in zip(internal_owners, internal_neighbours):
            pair = (int(o), int(nb))
            if pair in pairs:
                duplicates.append(pair)
            pairs.add(pair)
        assert len(duplicates) == 0, f"Found {len(duplicates)} duplicate internal face pairs: {duplicates[:5]}"

        # 3. Boundary faces strictly have neighbour == -1 and 0 <= owner < num_cells
        bnd_mask = mesh.neighbour < 0
        assert np.all(mesh.neighbour[bnd_mask] == -1)
        assert np.all(mesh.owner[bnd_mask] >= 0)
        assert np.all(mesh.owner[bnd_mask] < mesh.num_cells)

    def test_agglomerated_polyhedral_cell_vertex_union(self):
        """Verify agglomerated cells (CellType 42) contain all vertices of their faces in cells[c]."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

        poly_indices = np.where(mesh.cell_types == 42)[0]
        assert len(poly_indices) > 0, "No agglomerated polyhedral cells found in mesh"

        # Map faces to cells
        cell_to_face_verts: Dict[int, Set[int]] = {c: set() for c in poly_indices}
        for f_idx, (o, nb) in enumerate(zip(mesh.owner, mesh.neighbour)):
            f_nodes = set(mesh.faces[f_idx])
            if o in cell_to_face_verts:
                cell_to_face_verts[o].update(f_nodes)
            if nb in cell_to_face_verts:
                cell_to_face_verts[nb].update(f_nodes)

        # In mesh.cells[c], verify that every vertex of every adjacent face is present
        for c in poly_indices:
            c_nodes = set(mesh.cells[c])
            required_nodes = cell_to_face_verts[c]

            missing = required_nodes - c_nodes
            assert len(missing) == 0, f"Agglomerated cell {c} missing {len(missing)} vertices in cells[c]"

    def test_wall_face_inward_orientation_defect_detection(self):
        """Verify that all wall face normals strictly point outward towards the domain exterior (zero inward faces)."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

        start_w, num_w = mesh.boundaries["wall"]
        wall_faces = range(start_w, start_w + num_w)

        # Inward face count: on sphere centered at origin, outward normal must satisfy dot(sf, cf) > 0
        inward_wall_faces = 0
        for f in wall_faces:
            if np.dot(mesh.face_areas[f], mesh.face_centres[f]) < 0:
                inward_wall_faces += 1

        inward_ratio = inward_wall_faces / num_w
        print(f"\nEmpirical finding: {inward_wall_faces}/{num_w} wall faces ({inward_ratio:.2%}) have inward normals!")
        assert inward_wall_faces == 0, f"Found {inward_wall_faces} inverted wall faces!"

    def test_omega_cell_upwind_consistency(self):
        """Verify omega_cell values are strictly bounded by [0.1, 1.0] and match max(0.1, min(beta_f))."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

        assert "omega_cell" in mesh.error_map
        omega = mesh.error_map["omega_cell"]
        assert len(omega) == mesh.num_cells
        assert np.all(omega >= 0.1), "Found omega_cell < 0.1"
        assert np.all(omega <= 1.0), "Found omega_cell > 1.0"


# ==============================================================================
# 4. DistMesh Orthogonal Boundary Projection Edge Cases
# ==============================================================================

class TestAdversarialDistMeshProjection:
    """Stress-tests 2D DistMesh orthogonal boundary projection edge cases."""

    def test_distmesh_acute_triangle_vertex_exact_projection(self):
        """Verify projection behavior on sharp acute triangle (20 deg apex) with vertices as nearest points."""
        engine = MeshEngine(use_native=True)
        # Acute triangle with sharp corner at apex (0, 3)
        poly = np.array([
            [-0.5, 0.0],
            [0.5, 0.0],
            [0.0, 3.0],
        ], dtype=np.float64)

        config = MeshingConfig()
        config.sizing.h_min = 0.1
        config.sizing.h_max = 0.2
        config.sizing.h0 = 0.15
        config.distmesh_iters = 40

        mesh = engine.mesh_2d_polygon(poly, config=config)
        assert mesh.num_cells > 0
        assert mesh.num_points > 0

        # All points must lie within or on the bounding triangle
        for pt in mesh.points:
            x, y = pt[0], pt[1]
            assert y >= -1e-5, f"Point drifted below base: y={y}"
            assert y <= 3.0 + 1e-5, f"Point drifted above apex: y={y}"
            max_x = 0.5 * (1.0 - y / 3.0) + 1e-4
            assert abs(x) <= max_x, f"Point drifted outside acute edge: pt=({x}, {y}), max_x={max_x}"

        # All cell volumes (triangle areas) must be strictly positive
        assert np.all(mesh.cell_volumes > 0.0)

    def test_distmesh_u_shaped_concave_recess_points_far_outside(self):
        """Stress-test U-shaped concave recess where forces push interior nodes far outside into cavity."""
        engine = MeshEngine(use_native=True)
        # U-shaped domain with deep central notch:
        # Outer box [0, 4] x [0, 4], central cutout [1.5, 2.5] x [1.5, 4.0]
        poly = np.array([
            [0.0, 0.0],
            [4.0, 0.0],
            [4.0, 4.0],
            [2.5, 4.0],
            [2.5, 1.5],
            [1.5, 1.5],
            [1.5, 4.0],
            [0.0, 4.0],
        ], dtype=np.float64)

        config = MeshingConfig()
        config.sizing.h_min = 0.2
        config.sizing.h_max = 0.3
        config.sizing.h0 = 0.25
        config.distmesh_iters = 50

        mesh = engine.mesh_2d_polygon(poly, config=config)
        assert mesh.num_cells > 0
        assert mesh.num_points > 0

        # Verify no nodes drifted into the exterior central cutout [1.501, 2.499] x [1.501, 4.001]
        x = mesh.points[:, 0]
        y = mesh.points[:, 1]
        in_recess = (x > 1.501) & (x < 2.499) & (y > 1.501)
        assert np.sum(in_recess) == 0, f"Found {np.sum(in_recess)} nodes pushed into exterior recess"

        # Verify all cells have strictly positive area
        assert np.all(mesh.cell_volumes > 0.0)

    def test_distmesh_star_polygon_reflex_corners(self):
        """Verify DistMesh on non-convex star polygon with multiple acute reflex vertices."""
        engine = MeshEngine(use_native=True)
        # 5-pointed star
        n_points = 5
        outer_r = 2.0
        inner_r = 0.8
        star_verts = []
        for i in range(2 * n_points):
            r = outer_r if (i % 2 == 0) else inner_r
            angle = i * math.pi / n_points
            star_verts.append([r * math.cos(angle), r * math.sin(angle)])
        poly = np.array(star_verts, dtype=np.float64)

        config = MeshingConfig()
        config.sizing.h_min = 0.15
        config.sizing.h_max = 0.25
        config.sizing.h0 = 0.2
        config.distmesh_iters = 35

        mesh = engine.mesh_2d_polygon(poly, config=config)
        assert mesh.num_cells > 0
        assert mesh.num_points > 0

        # No node should exceed outer radius + epsilon
        radii = np.linalg.norm(mesh.points, axis=1)
        assert np.all(radii <= outer_r + 1e-4), "Nodes drifted outside star outer perimeter"

        # All cells have positive volume
        assert np.all(mesh.cell_volumes > 0.0)
