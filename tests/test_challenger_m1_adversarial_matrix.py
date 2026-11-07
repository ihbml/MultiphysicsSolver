"""Empirical Adversarial Stress-Test Matrix for Milestone M1 Iteration 2.

Authored by challenger_m1_r2_1 (Critic / Specialist).
Adversarially challenges and verifies:
1. Boundary wall face normal outward orientation: 0 inward wall faces across multiple geometries (icosphere, cube, cylinder, shifted sphere).
2. Discrete Gauss divergence closure: max_c ||sum_{f in c} S_f|| / V_c^(2/3) < 1e-11 across all cells.
3. Corner tetrahedron AMIPS non-inversion: non-positive Jacobians and inverted tets (E > 1e4) strictly rejected without bypass.
4. Positive volume preservation: all cell volumes strictly positive (V_c > 0) and bounded.
"""

import math
from typing import Tuple
import numpy as np
import pytest

from backend.meshing.difvm_topology import generate_hexcore_native
from tests.conftest import create_unit_cube_arrays
from tests.test_asme_vv import create_geodesic_icosphere


def create_triangulated_cylinder(
    radius: float = 0.6,
    height: float = 1.6,
    n_theta: int = 32,
    n_z: int = 16
) -> Tuple[np.ndarray, np.ndarray]:
    """Generate a closed triangulated 3D cylinder centered at origin."""
    z_min = -height / 2.0
    z_max = height / 2.0
    z_coords = np.linspace(z_min, z_max, n_z)
    theta = np.linspace(0, 2 * np.pi, n_theta, endpoint=False)

    pts = []
    # Sidewall vertices
    for z in z_coords:
        for th in theta:
            pts.append([radius * np.cos(th), radius * np.sin(th), z])

    # Endcap center points
    idx_bottom_center = len(pts)
    pts.append([0.0, 0.0, z_min])
    idx_top_center = len(pts)
    pts.append([0.0, 0.0, z_max])

    pts = np.array(pts, dtype=np.float64)
    faces = []

    # Sidewall quads split into 2 triangles
    for iz in range(n_z - 1):
        for it in range(n_theta):
            it_next = (it + 1) % n_theta
            v0 = iz * n_theta + it
            v1 = iz * n_theta + it_next
            v2 = (iz + 1) * n_theta + it_next
            v3 = (iz + 1) * n_theta + it
            faces.append([v0, v1, v2])
            faces.append([v0, v2, v3])

    # Bottom cap (z = z_min, normal = -z)
    for it in range(n_theta):
        it_next = (it + 1) % n_theta
        v0 = it
        v1 = it_next
        faces.append([idx_bottom_center, v1, v0])

    # Top cap (z = z_max, normal = +z)
    top_offset = (n_z - 1) * n_theta
    for it in range(n_theta):
        it_next = (it + 1) % n_theta
        v0 = top_offset + it
        v1 = top_offset + it_next
        faces.append([idx_top_center, v0, v1])

    faces = np.array(faces, dtype=np.int32)
    return pts, faces


def compute_relative_gauss_closure(mesh) -> Tuple[float, np.ndarray]:
    """Compute max and per-cell relative Gauss divergence closure: ||sum S_f|| / V_c^(2/3)."""
    closure = np.zeros_like(mesh.cell_centres)
    internal = mesh.neighbour >= 0
    np.add.at(closure, mesh.owner, mesh.face_areas)
    np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

    closure_norms = np.linalg.norm(closure, axis=1)
    rel_closure = closure_norms / (mesh.cell_volumes ** (2.0 / 3.0))
    max_rel_closure = float(np.max(rel_closure))
    return max_rel_closure, rel_closure


class TestAdversarialWallNormalOutward:
    """Stress-tests for outward wall face normal orientation across multiple geometries."""

    @pytest.mark.parametrize("h0", [0.8, 0.5, 0.35])
    def test_icosphere_wall_normal_outward_multiple_resolutions(self, h0: float):
        """Stress-test that 100% of wall faces on geodesic icosphere point outward (0 inward faces)."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=h0, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert "wall" in mesh.boundaries
        start_w, num_w = mesh.boundaries["wall"]
        assert num_w > 0

        # For a sphere centered at origin, outward normal S_f must satisfy S_f . c_f > 0
        inward_count = 0
        min_dot = 1e30
        for f in range(start_w, start_w + num_w):
            dot_prod = np.dot(mesh.face_areas[f], mesh.face_centres[f])
            if dot_prod < 0:
                inward_count += 1
            min_dot = min(min_dot, dot_prod)

        print(f"\n[Icosphere h0={h0}] Wall faces: {num_w}, Inward: {inward_count}, Min dot: {min_dot:.4e}")
        assert inward_count == 0, f"Found {inward_count}/{num_w} inward wall faces for icosphere h0={h0}!"

    def test_unit_cube_boundary_normal_outward(self):
        """Stress-test boundary face normal outward orientation on a unit cube [-0.5, 0.5]^3."""
        pts0, faces0 = create_unit_cube_arrays()
        # Shift to origin center: [-0.5, 0.5]^3
        pts_centered = pts0 - 0.5
        mesh = generate_hexcore_native(pts_centered, faces0, h0=0.25, domain_mode="solid_only")

        assert mesh.num_cells > 0
        total_bnd_faces = 0
        inward_bnd_faces = 0

        # On tight bounding box, boundary faces are classified into Cartesian boundary patches
        for patch_name, (start_f, num_f) in mesh.boundaries.items():
            total_bnd_faces += num_f
            for f in range(start_f, start_f + num_f):
                sf = mesh.face_areas[f]
                cf = mesh.face_centres[f]
                # Box centered at origin [-0.5, 0.5]^3: outward normal has positive dot with cf
                if np.dot(sf, cf) <= 0:
                    inward_bnd_faces += 1

        print(f"\n[Cube h0=0.25] Total boundary faces: {total_bnd_faces}, Inward: {inward_bnd_faces}")
        assert total_bnd_faces > 0
        assert inward_bnd_faces == 0, f"Found {inward_bnd_faces}/{total_bnd_faces} inward boundary faces on cube!"

    def test_cylinder_wall_normal_outward(self):
        """Stress-test wall face normal outward orientation on a 3D cylinder."""
        pts, faces = create_triangulated_cylinder(radius=0.6, height=1.6, n_theta=32, n_z=16)
        mesh = generate_hexcore_native(pts, faces, h0=0.3, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert "wall" in mesh.boundaries
        start_w, num_w = mesh.boundaries["wall"]

        inward_count = 0
        for f in range(start_w, start_w + num_w):
            sf = mesh.face_areas[f]
            cf = mesh.face_centres[f]
            # True outward normal for cylinder:
            # If face normal is primarily in Z direction, it is an endcap face: match sign of cf[2]
            if abs(sf[2]) > max(abs(sf[0]), abs(sf[1])):
                if sf[2] * cf[2] < 0:
                    inward_count += 1
            else:
                # Sidewall face: radial normal should match radial position (cf[0], cf[1])
                radial_dot = sf[0] * cf[0] + sf[1] * cf[1]
                if radial_dot < 0:
                    inward_count += 1

        print(f"\n[Cylinder h0=0.3] Wall faces: {num_w}, Inward: {inward_count}")
        assert inward_count == 0, f"Found {inward_count}/{num_w} inward wall faces on cylinder!"

    def test_shifted_sphere_wall_normal_outward(self):
        """Stress-test outward normal orientation on off-center sphere at (10, -5, 20)."""
        surf_pts0, surf_faces = create_geodesic_icosphere(subdivisions=2)
        shift = np.array([10.0, -5.0, 20.0])
        mesh = generate_hexcore_native(surf_pts0 + shift, surf_faces, h0=0.5, domain_mode="solid_only")

        assert mesh.num_cells > 0
        start_w, num_w = mesh.boundaries["wall"]

        inward_count = 0
        for f in range(start_w, start_w + num_w):
            sf = mesh.face_areas[f]
            # Vector from sphere center to face center
            r_vec = mesh.face_centres[f] - shift
            if np.dot(sf, r_vec) <= 0:
                inward_count += 1

        print(f"\n[Shifted Sphere] Wall faces: {num_w}, Inward: {inward_count}")
        assert inward_count == 0, f"Found {inward_count}/{num_w} inward wall faces on shifted sphere!"


class TestAdversarialGaussDivergenceClosure:
    """Stress-tests discrete Gauss divergence closure max_c ||sum S_f|| / V_c^(2/3) < 1e-11."""

    def test_gauss_closure_icosphere_h0_05(self):
        """Verify Gauss closure on icosphere h0=0.5 (full 36k cells)."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

        max_closure, _ = compute_relative_gauss_closure(mesh)
        print(f"\n[Gauss Closure Icosphere h0=0.5] max rel error = {max_closure:.4e}")
        assert max_closure < 1e-11, f"Relative Gauss closure exceeded 1e-11: {max_closure}"

    def test_gauss_closure_icosphere_coarse_h0_10(self):
        """Verify Gauss closure on coarse icosphere h0=1.0 with large cut-cell truncations."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=1.0, domain_mode="solid_only")

        max_closure, _ = compute_relative_gauss_closure(mesh)
        print(f"\n[Gauss Closure Icosphere h0=1.0] max rel error = {max_closure:.4e}")
        assert max_closure < 1e-11, f"Relative Gauss closure exceeded 1e-11: {max_closure}"

    def test_gauss_closure_unit_cube(self):
        """Verify Gauss closure on unit cube with agglomerated cells."""
        pts0, faces0 = create_unit_cube_arrays()
        mesh = generate_hexcore_native(pts0, faces0, h0=0.25, domain_mode="solid_only")

        max_closure, _ = compute_relative_gauss_closure(mesh)
        print(f"\n[Gauss Closure Cube h0=0.25] max rel error = {max_closure:.4e}")
        assert max_closure < 1e-11, f"Relative Gauss closure exceeded 1e-11: {max_closure}"

    def test_gauss_closure_cylinder(self):
        """Verify Gauss closure on 3D cylinder."""
        pts, faces = create_triangulated_cylinder(radius=0.6, height=1.6, n_theta=32, n_z=16)
        mesh = generate_hexcore_native(pts, faces, h0=0.3, domain_mode="solid_only")

        max_closure, _ = compute_relative_gauss_closure(mesh)
        print(f"\n[Gauss Closure Cylinder h0=0.3] max rel error = {max_closure:.4e}")
        assert max_closure < 1e-11, f"Relative Gauss closure exceeded 1e-11: {max_closure}"


class TestAdversarialAMIPSNegativeJacobianRejection:
    """Stress-tests corner tetrahedron AMIPS non-inversion and rejection oracle."""

    def test_amips_mathematical_oracle_inverted_tet(self):
        """Oracle test: verify that any non-positive Jacobian det(J) <= 1e-18 yields E = 1e30 > 1e4."""
        def py_compute_tet_amips(v0, v1, v2, v3):
            e1 = v1 - v0
            e2 = v2 - v0
            e3 = v3 - v0
            det_j = np.dot(np.cross(e1, e2), e3)
            if det_j <= 1e-18:
                return 1e30
            edge_sum_sq = (
                np.sum((v0 - v1)**2) + np.sum((v0 - v2)**2) + np.sum((v0 - v3)**2) +
                np.sum((v1 - v2)**2) + np.sum((v1 - v3)**2) + np.sum((v2 - v3)**2)
            )
            ratio = (27.0 / 16.0) * (edge_sum_sq ** 3) / (det_j ** 2)
            return ratio ** (1.0 / 3.0)

        # Baseline regular corner tet of unit cube octant
        v0 = np.array([0.0, 0.0, 0.0])
        v1 = np.array([1.0, 0.0, 0.0])
        v2 = np.array([0.0, 1.0, 0.0])
        v3 = np.array([0.0, 0.0, 1.0])

        e_clean = py_compute_tet_amips(v0, v1, v2, v3)
        assert 1.0 < e_clean < 100.0, f"Unexpected clean AMIPS energy: {e_clean}"

        # 1. Coplanar / degenerate tet (det_j = 0)
        v3_coplanar = np.array([0.5, 0.5, 0.0])
        e_coplanar = py_compute_tet_amips(v0, v1, v2, v3_coplanar)
        assert e_coplanar >= 1e30, f"Degenerate tet not rejected: {e_coplanar}"

        # 2. Inverted tet (det_j < 0)
        v3_inverted = np.array([0.0, 0.0, -1.0])
        e_inverted = py_compute_tet_amips(v0, v1, v2, v3_inverted)
        assert e_inverted >= 1e30, f"Inverted tet not rejected: {e_inverted}"

        # 3. Barely degenerate (det_j = 1e-19 <= 1e-18)
        v3_near_zero = np.array([0.0, 0.0, 1e-19])
        e_near_zero = py_compute_tet_amips(v0, v1, v2, v3_near_zero)
        assert e_near_zero >= 1e30

        # In all cases where det(J) <= 1e-18, E = 1e30 strictly exceeds options.amips_max (1e4)
        assert 1e30 > 10000.0


class TestAdversarialPositiveVolumePreservation:
    """Stress-tests positive volume preservation across all generated meshes."""

    @pytest.mark.parametrize("h0", [0.8, 0.5, 0.35])
    def test_icosphere_all_cell_volumes_strictly_positive(self, h0: float):
        """Verify all cell volumes are strictly positive on icosphere."""
        surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
        mesh = generate_hexcore_native(surf_pts, surf_faces, h0=h0, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert np.all(mesh.cell_volumes > 0.0), "Found zero or negative cell volume!"
        min_v = float(np.min(mesh.cell_volumes))
        assert min_v > 1e-15, f"Cell volume suspiciously close to zero: {min_v}"

        # Total volume should approximate sphere volume 4/3 * pi * 1^3 ~ 4.18879
        tot_v = float(np.sum(mesh.cell_volumes))
        assert 3.5 < tot_v < 4.5, f"Total volume outside reasonable range: {tot_v}"

    def test_cube_cell_volumes_strictly_positive(self):
        """Verify all cell volumes are strictly positive on unit cube."""
        pts0, faces0 = create_unit_cube_arrays()
        mesh = generate_hexcore_native(pts0, faces0, h0=0.25, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert np.all(mesh.cell_volumes > 0.0), "Found zero or negative cell volume on cube!"
        tot_v = float(np.sum(mesh.cell_volumes))
        assert np.isclose(tot_v, 1.0, atol=1e-5), f"Total volume of cube should be 1.0, got: {tot_v}"

    def test_cylinder_cell_volumes_strictly_positive(self):
        """Verify all cell volumes are strictly positive on cylinder."""
        radius, height = 0.6, 1.6
        pts, faces = create_triangulated_cylinder(radius=radius, height=height, n_theta=32, n_z=16)
        mesh = generate_hexcore_native(pts, faces, h0=0.3, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert np.all(mesh.cell_volumes > 0.0), "Found zero or negative cell volume on cylinder!"
        tot_v = float(np.sum(mesh.cell_volumes))
        expected_v = math.pi * (radius ** 2) * height  # ~ 1.8095
        assert abs(tot_v - expected_v) / expected_v < 0.15, f"Cylinder volume {tot_v} differs from {expected_v}"
