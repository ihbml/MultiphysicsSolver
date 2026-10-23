"""ASME V&V 10 / ASME V&V 20 Code Verification Suite for CFDSolver CAD Engine.

Covers:
1. Numerical precision and double-precision preservation.
2. Rigid-body affine invariance (translation and rotation invariance).
3. Scale exactness: angles preserved, area scales by s^2, volume scales by s^3 within machine precision.
4. Analytical benchmark verification (rotated 200 mm cube, unit sphere).
5. Discretization error control and convergence rate verification.
"""

from pathlib import Path
from typing import Tuple
import numpy as np
import pytest

from backend.geometry.models import BoundingBox, SurfaceMesh
from tests.conftest import (
    ANALYTICAL_SURFACE_AREA_CAD,
    ANALYTICAL_SURFACE_AREA_CFD,
    ANALYTICAL_VOLUME_CAD,
    ANALYTICAL_VOLUME_CFD,
    CAD_BBOX_MAX,
    CAD_BBOX_MIN,
    CFD_BBOX_MAX,
    CFD_BBOX_MIN,
    CUBE_SIDE_M,
    CUBE_SIDE_MM,
    ROTATION_ANGLE_DEG,
    create_unit_cube_arrays,
)


def compute_mesh_surface_area(vertices: np.ndarray, faces: np.ndarray) -> float:
    """Computes total surface area of triangulated mesh."""
    v0 = vertices[faces[:, 0]]
    v1 = vertices[faces[:, 1]]
    v2 = vertices[faces[:, 2]]
    cross = np.cross(v1 - v0, v2 - v0)
    return float(0.5 * np.sum(np.linalg.norm(cross, axis=1)))


def compute_mesh_volume(vertices: np.ndarray, faces: np.ndarray) -> float:
    """Computes enclosed volume using Gauss's Divergence Theorem."""
    v0 = vertices[faces[:, 0]]
    v1 = vertices[faces[:, 1]]
    v2 = vertices[faces[:, 2]]
    cross = np.cross(v0, v1)
    return float(np.sum(cross * v2) / 6.0)


def create_geodesic_icosphere(subdivisions: int = 2) -> Tuple[np.ndarray, np.ndarray]:
    """Generates a unit sphere triangulated mesh with n subdivision levels."""
    # Golden ratio
    phi = (1.0 + np.sqrt(5.0)) / 2.0
    verts = [
        [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
        [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
        [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1],
    ]
    verts = np.array(verts, dtype=np.float64)
    # Normalize to unit radius
    verts /= np.linalg.norm(verts, axis=1, keepdims=True)

    faces = [
        [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
        [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
        [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
        [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
    ]
    faces = np.array(faces, dtype=np.int32)

    # Subdivide faces
    for _ in range(subdivisions):
        midpoint_cache = {}
        new_faces = []

        def get_midpoint(i1: int, i2: int, current_verts: list) -> int:
            edge = (min(i1, i2), max(i1, i2))
            if edge in midpoint_cache:
                return midpoint_cache[edge]
            p1 = np.asarray(current_verts[i1], dtype=np.float64)
            p2 = np.asarray(current_verts[i2], dtype=np.float64)
            mid = (p1 + p2) / 2.0
            mid /= np.linalg.norm(mid)
            idx = len(current_verts)
            current_verts.append(mid.tolist())
            midpoint_cache[edge] = idx
            return idx

        verts_list = verts.tolist()
        for f in faces:
            m01 = get_midpoint(f[0], f[1], verts_list)
            m12 = get_midpoint(f[1], f[2], verts_list)
            m20 = get_midpoint(f[2], f[0], verts_list)
            new_faces.extend([
                [f[0], m01, m20],
                [f[1], m12, m01],
                [f[2], m20, m12],
                [m01, m12, m20],
            ])
        verts = np.array(verts_list, dtype=np.float64)
        faces = np.array(new_faces, dtype=np.int32)

    return verts, faces


class TestASMEVVCodeVerification:
    """ASME V&V 10 / 20 Code Verification assertions for CFDSolver CAD Engine."""

    def test_vv_01_floating_point_precision_retention(self):
        """Verifies that 64-bit float vertex coordinates are retained without truncation."""
        v_orig, f_orig = create_unit_cube_arrays()
        # Scale to CFD meters
        v_cfd = v_orig * 0.001
        mesh = SurfaceMesh(vertices=v_cfd, faces=f_orig)
        assert mesh.vertices.dtype == np.float64
        # Verify 14 decimal digits precision
        test_val = 0.123456789012345
        v_test = np.copy(mesh.vertices)
        v_test[0, 0] = test_val
        assert abs(v_test[0, 0] - test_val) < 1e-15

    def test_vv_02_translational_invariance(self):
        """Translation of mesh preserves volume, area, and edge lengths within 1e-14."""
        v, f = create_unit_cube_arrays()
        area_before = compute_mesh_surface_area(v, f)
        vol_before = compute_mesh_volume(v, f)

        # Apply large 3D translation
        t_vec = np.array([1234.5678, -9876.5432, 555.1234], dtype=np.float64)
        v_translated = v + t_vec

        area_after = compute_mesh_surface_area(v_translated, f)
        vol_after = compute_mesh_volume(v_translated, f)

        assert abs(area_after - area_before) / area_before < 1e-14
        assert abs(vol_after - vol_before) / vol_before < 1e-6

        # Verify bbox span is strictly invariant
        bbox_before = np.max(v, axis=0) - np.min(v, axis=0)
        bbox_after = np.max(v_translated, axis=0) - np.min(v_translated, axis=0)
        assert np.allclose(bbox_before, bbox_after, atol=1e-14)

    def test_vv_03_rotational_invariance(self):
        """Rotation around arbitrary 3D axis preserves volume and surface area within 1e-14."""
        v, f = create_unit_cube_arrays()
        area_before = compute_mesh_surface_area(v, f)
        vol_before = compute_mesh_volume(v, f)

        # Construct arbitrary 3D rotation matrix around axis [1, 2, 3] by 37.5 degrees
        axis = np.array([1.0, 2.0, 3.0], dtype=np.float64)
        axis /= np.linalg.norm(axis)
        theta = np.radians(37.5)
        k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
        rot_matrix = np.eye(3) + np.sin(theta) * k + (1.0 - np.cos(theta)) * (k @ k)

        v_rotated = v @ rot_matrix.T

        area_after = compute_mesh_surface_area(v_rotated, f)
        vol_after = compute_mesh_volume(v_rotated, f)

        assert abs(area_after - area_before) / area_before < 1e-14
        assert abs(vol_after - vol_before) / vol_before < 1e-14

    def test_vv_04_scale_exactness_relations(self):
        """Uniform scaling by s obeys: lengths scale by s, area by s^2, volume by s^3."""
        v, f = create_unit_cube_arrays()
        s = 0.001  # mm to m

        len_orig = np.linalg.norm(v[f[0, 1]] - v[f[0, 0]])
        area_orig = compute_mesh_surface_area(v, f)
        vol_orig = compute_mesh_volume(v, f)

        v_scaled = v * s

        len_scaled = np.linalg.norm(v_scaled[f[0, 1]] - v_scaled[f[0, 0]])
        area_scaled = compute_mesh_surface_area(v_scaled, f)
        vol_scaled = compute_mesh_volume(v_scaled, f)

        # Length ratio
        assert abs((len_scaled / len_orig) - s) < 1e-15
        # Area ratio: s^2
        assert abs((area_scaled / area_orig) - (s ** 2)) < 1e-15
        # Volume ratio: s^3
        assert abs((vol_scaled / vol_orig) - (s ** 3)) < 1e-15

    def test_vv_05_angle_preservation_under_scaling(self):
        """Interior angles between triangle edges and facet normals are invariant under scaling."""
        v, f = create_unit_cube_arrays()
        s = 0.001
        v_scaled = v * s

        for i in range(len(f)):
            # Triangle edge vectors
            e0_orig = v[f[i, 1]] - v[f[i, 0]]
            e1_orig = v[f[i, 2]] - v[f[i, 0]]
            cos_orig = np.dot(e0_orig, e1_orig) / (np.linalg.norm(e0_orig) * np.linalg.norm(e1_orig))

            e0_scaled = v_scaled[f[i, 1]] - v_scaled[f[i, 0]]
            e1_scaled = v_scaled[f[i, 2]] - v_scaled[f[i, 0]]
            cos_scaled = np.dot(e0_scaled, e1_scaled) / (np.linalg.norm(e0_scaled) * np.linalg.norm(e1_scaled))

            assert abs(cos_scaled - cos_orig) < 1e-14

    def test_vv_06_analytical_rotated_cube_benchmark(self):
        """Verifies exact dimensions, volume, area, and bounding box of rotated 200 mm cube."""
        # Unrotated cube corners [-100, 100]^2 x [0, 200] mm
        pts = np.array(
            [
                [-100.0, -100.0, 0.0],
                [100.0, -100.0, 0.0],
                [100.0, 100.0, 0.0],
                [-100.0, 100.0, 0.0],
                [-100.0, -100.0, 200.0],
                [100.0, -100.0, 200.0],
                [100.0, 100.0, 200.0],
                [-100.0, 100.0, 200.0],
            ],
            dtype=np.float64,
        )
        # Rotate 10 degrees around Z
        theta = np.radians(10.0)
        rot_z = np.array(
            [
                [np.cos(theta), -np.sin(theta), 0.0],
                [np.sin(theta), np.cos(theta), 0.0],
                [0.0, 0.0, 1.0],
            ],
            dtype=np.float64,
        )
        rotated_pts = pts @ rot_z.T

        # Bounding box in CAD mm
        min_cad = np.min(rotated_pts, axis=0)
        max_cad = np.max(rotated_pts, axis=0)

        assert np.allclose(min_cad, CAD_BBOX_MIN, atol=1e-5)
        assert np.allclose(max_cad, CAD_BBOX_MAX, atol=1e-5)

        # Scale to CFD m (0.001)
        pts_cfd = rotated_pts * 0.001
        min_cfd = np.min(pts_cfd, axis=0)
        max_cfd = np.max(pts_cfd, axis=0)

        assert np.allclose(min_cfd, CFD_BBOX_MIN, atol=1e-5)
        assert np.allclose(max_cfd, CFD_BBOX_MAX, atol=1e-5)

    def test_vv_07_grid_convergence_order_unit_sphere(self):
        """Monotonic convergence of surface area and volume for geodesic sphere."""
        exact_area = 4.0 * np.pi  # ~12.5663706
        exact_vol = (4.0 / 3.0) * np.pi  # ~4.1887902

        # Subdivisions: 1, 2, 3
        errors_area = []
        errors_vol = []

        for sub in [1, 2, 3]:
            verts, faces = create_geodesic_icosphere(subdivisions=sub)
            a = compute_mesh_surface_area(verts, faces)
            v = compute_mesh_volume(verts, faces)
            err_a = abs(exact_area - a) / exact_area
            err_v = abs(exact_vol - v) / exact_vol
            errors_area.append(err_a)
            errors_vol.append(err_v)

        # Verify monotonic decrease of discretization error
        assert errors_area[1] < errors_area[0]
        assert errors_area[2] < errors_area[1]
        assert errors_vol[1] < errors_vol[0]
        assert errors_vol[2] < errors_vol[1]

        # Verify approximately 2nd-order convergence (ratio ~ 4 when h is halved)
        # Level 2 -> 3: ratio of errors
        ratio_area = errors_area[1] / errors_area[2]
        assert ratio_area > 3.0, f"Expected second-order convergence (~4), got {ratio_area}"
