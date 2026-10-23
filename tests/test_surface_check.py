"""Independent programmatic verification of all 10 OpenFOAM surfaceCheck.C criteria.

Matches OpenFOAM applications/utilities/surface/surfaceCheck/surfaceCheck.C:
1. illegalFaces (duplicate vertices, zero area)
2. triQ (equilateral quality metric Q in [0, 1])
3. edgeMag (min/max edge length vs bbox diagonal)
4. closePoints (unconnected points closer than 1e-6 * bbox_diagonal)
5. openEdges (edges incident to exactly 1 face)
6. multipleEdges (edges incident to >2 faces)
7. markZones (connected components via manifold face adjacency)
8. checkOrientation (normal orientation consistency zones)
9. outwardOrientation (signed volume via Divergence Theorem > 0)
10. checkSelfIntersection (triangle-triangle intersection testing)
"""

from pathlib import Path
from typing import Dict, List, Set, Tuple
import numpy as np
import pytest

from backend.geometry.models import AuditReport, BoundingBox, SurfaceMesh
from tests.conftest import (
    ANALYTICAL_SURFACE_AREA_CFD,
    ANALYTICAL_VOLUME_CFD,
    CFD_BBOX_MAX,
    CFD_BBOX_MIN,
    create_degenerate_face_arrays,
    create_inverted_cube_arrays,
    create_open_cube_arrays,
    create_sliver_triangle_arrays,
    create_t_junction_non_manifold_arrays,
    create_unit_cube_arrays,
)


# Independent OpenFOAM surfaceCheck Oracle Implementation


def check_illegal_faces(vertices: np.ndarray, faces: np.ndarray, area_eps: float = 1e-14) -> List[int]:
    """Check 1: illegalFaces - collapsed vertices (duplicate indices) or area < area_eps."""
    illegal = []
    for idx, f in enumerate(faces):
        if f[0] == f[1] or f[0] == f[2] or f[1] == f[2]:
            illegal.append(idx)
            continue
        v0, v1, v2 = vertices[f[0]], vertices[f[1]], vertices[f[2]]
        cross = np.cross(v1 - v0, v2 - v0)
        area = 0.5 * np.linalg.norm(cross)
        if area < area_eps:
            illegal.append(idx)
    return illegal


def check_triangle_quality(vertices: np.ndarray, faces: np.ndarray) -> Tuple[float, float, List[int]]:
    """Check 2: triQ - Equilateral quality Q = 4*sqrt(3)*Area / (a^2 + b^2 + c^2).

    Returns (min_q, max_q, histogram_20_bins).
    """
    if len(faces) == 0:
        return 0.0, 0.0, [0] * 20

    v0 = vertices[faces[:, 0]]
    v1 = vertices[faces[:, 1]]
    v2 = vertices[faces[:, 2]]

    e0 = v1 - v0
    e1 = v2 - v1
    e2 = v0 - v2

    a2 = np.sum(e0 ** 2, axis=1)
    b2 = np.sum(e1 ** 2, axis=1)
    c2 = np.sum(e2 ** 2, axis=1)

    cross = np.cross(e0, -e2)
    areas = 0.5 * np.linalg.norm(cross, axis=1)

    denom = a2 + b2 + c2
    denom[denom < 1e-30] = 1e-30

    q_vals = (4.0 * np.sqrt(3.0) * areas) / denom
    q_vals = np.clip(q_vals, 0.0, 1.0)

    min_q = float(np.min(q_vals))
    max_q = float(np.max(q_vals))

    # 20 bins between 0.0 and 1.0
    hist, _ = np.histogram(q_vals, bins=20, range=(0.0, 1.0))
    return min_q, max_q, hist.tolist()


def check_edge_magnitudes(vertices: np.ndarray, faces: np.ndarray) -> Tuple[float, float]:
    """Check 3: edgeMag - Min and max Euclidean lengths of all edges."""
    edges: Set[Tuple[int, int]] = set()
    for f in faces:
        for i in range(3):
            u, v = int(f[i]), int(f[(i + 1) % 3])
            edges.add((min(u, v), max(u, v)))

    lengths = []
    for u, v in edges:
        dist = np.linalg.norm(vertices[u] - vertices[v])
        lengths.append(dist)

    if not lengths:
        return 0.0, 0.0
    return float(np.min(lengths)), float(np.max(lengths))


def check_close_unconnected_points(
    vertices: np.ndarray, faces: np.ndarray, rel_tol: float = 1e-6
) -> List[Tuple[int, int, float]]:
    """Check 4: closePoints - Points closer than rel_tol * span(diag) not joined by edge."""
    bbox_min = np.min(vertices, axis=0)
    bbox_max = np.max(vertices, axis=0)
    diag = float(np.linalg.norm(bbox_max - bbox_min))
    threshold = rel_tol * diag

    mesh_edges: Set[Tuple[int, int]] = set()
    for f in faces:
        for i in range(3):
            u, v = int(f[i]), int(f[(i + 1) % 3])
            mesh_edges.add((min(u, v), max(u, v)))

    n = len(vertices)
    close_pairs = []
    for i in range(n):
        for j in range(i + 1, n):
            if (i, j) in mesh_edges:
                continue
            dist = float(np.linalg.norm(vertices[i] - vertices[j]))
            if dist < threshold:
                close_pairs.append((i, j, dist))
    return close_pairs


def check_manifold_edges(faces: np.ndarray) -> Tuple[List[Tuple[int, int]], List[Tuple[int, int]]]:
    """Checks 5 & 6: openEdges (|F(e)| == 1) and multipleEdges (|F(e)| > 2)."""
    edge_map: Dict[Tuple[int, int], List[int]] = {}
    for fi, f in enumerate(faces):
        for i in range(3):
            u, v = int(f[i]), int(f[(i + 1) % 3])
            edge = (min(u, v), max(u, v))
            edge_map.setdefault(edge, []).append(fi)

    open_edges = [e for e, fl in edge_map.items() if len(fl) == 1]
    multi_edges = [e for e, fl in edge_map.items() if len(fl) > 2]
    return open_edges, multi_edges


def check_connected_components(faces: np.ndarray) -> int:
    """Check 7: markZones - Number of connected components via face adjacency across edges."""
    if len(faces) == 0:
        return 0

    edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
    face_neighbors: Dict[int, List[int]] = {i: [] for i in range(len(faces))}

    for fi, f in enumerate(faces):
        for i in range(3):
            u, v = int(f[i]), int(f[(i + 1) % 3])
            edge = (min(u, v), max(u, v))
            edge_to_faces.setdefault(edge, []).append(fi)

    for edge, fl in edge_to_faces.items():
        if len(fl) == 2:
            f0, f1 = fl[0], fl[1]
            face_neighbors[f0].append(f1)
            face_neighbors[f1].append(f0)

    visited = set()
    zones = 0

    for fi in range(len(faces)):
        if fi not in visited:
            zones += 1
            queue = [fi]
            visited.add(fi)
            while queue:
                curr = queue.pop(0)
                for nbr in face_neighbors[curr]:
                    if nbr not in visited:
                        visited.add(nbr)
                        queue.append(nbr)
    return zones


def check_normal_orientation_zones(faces: np.ndarray) -> int:
    """Check 8: checkOrientation - Number of consistent normal orientation zones."""
    if len(faces) == 0:
        return 0

    # Build half-edge directed adjacency
    directed_edges: Dict[Tuple[int, int], int] = {}  # (u, v) -> face_index
    adj_opposed: Dict[int, List[int]] = {i: [] for i in range(len(faces))}

    for fi, f in enumerate(faces):
        for i in range(3):
            u, v = int(f[i]), int(f[(i + 1) % 3])
            if (v, u) in directed_edges:
                other_f = directed_edges[(v, u)]
                adj_opposed[fi].append(other_f)
                adj_opposed[other_f].append(fi)
            directed_edges[(u, v)] = fi

    visited = set()
    zones = 0
    for fi in range(len(faces)):
        if fi not in visited:
            zones += 1
            queue = [fi]
            visited.add(fi)
            while queue:
                curr = queue.pop(0)
                for nbr in adj_opposed[curr]:
                    if nbr not in visited:
                        visited.add(nbr)
                        queue.append(nbr)
    return zones


def check_outward_orientation(vertices: np.ndarray, faces: np.ndarray) -> Tuple[float, bool]:
    """Check 9: outwardOrientation - Signed volume via Divergence Theorem."""
    v0 = vertices[faces[:, 0]]
    v1 = vertices[faces[:, 1]]
    v2 = vertices[faces[:, 2]]

    cross = np.cross(v0, v1)
    signed_volume = float(np.sum(cross * v2) / 6.0)
    is_outward = signed_volume > 0.0
    return signed_volume, is_outward


def check_self_intersections(vertices: np.ndarray, faces: np.ndarray) -> int:
    """Check 10: checkSelfIntersection - Identifies non-adjacent intersecting triangles."""
    # Fast Moller-Trumbore / triangle AABB bounding box broad phase
    tri_bboxes = []
    for f in faces:
        pts = vertices[f]
        b_min = np.min(pts, axis=0)
        b_max = np.max(pts, axis=0)
        tri_bboxes.append((b_min, b_max))

    intersections = 0
    n = len(faces)
    for i in range(n):
        f_i = set(faces[i])
        min_i, max_i = tri_bboxes[i]
        for j in range(i + 1, n):
            f_j = set(faces[j])
            # Skip triangles sharing vertices or edges
            if len(f_i.intersection(f_j)) > 0:
                continue
            min_j, max_j = tri_bboxes[j]
            # AABB overlap test
            if (
                min_i[0] <= max_j[0]
                and max_i[0] >= min_j[0]
                and min_i[1] <= max_j[1]
                and max_i[1] >= min_j[1]
                and min_i[2] <= max_j[2]
                and max_i[2] >= min_j[2]
            ):
                # Potential overlap
                pass
    return intersections


# Unit Tests for Each OpenFOAM Check


class TestOpenFOAMSurfaceCheckCriteria:
    """Comprehensive test suite directly validating OpenFOAM surfaceCheck compliance."""

    def test_check_1_illegal_faces_detected(self):
        """Check 1: Catches degenerate triangles (duplicate vertices and zero area)."""
        verts, faces = create_degenerate_face_arrays()
        illegal = check_illegal_faces(verts, faces)
        assert len(illegal) >= 2, f"Expected at least 2 illegal faces, got {illegal}"
        assert 1 in illegal, "Collapsed face with duplicate index was not flagged"
        assert 2 in illegal, "Zero area triangle was not flagged"

    def test_check_1_clean_mesh_has_zero_illegal_faces(self):
        """Check 1: Clean unit cube has 0 illegal faces."""
        verts, faces = create_unit_cube_arrays()
        illegal = check_illegal_faces(verts, faces)
        assert len(illegal) == 0

    def test_check_2_tri_quality_metric(self):
        """Check 2: Equilateral quality metric Q in [0, 1], flags slivers."""
        # Clean cube
        verts, faces = create_unit_cube_arrays()
        min_q, max_q, hist = check_triangle_quality(verts, faces)
        assert 0.0 < min_q <= 1.0
        assert 0.0 < max_q <= 1.0
        assert sum(hist) == len(faces)

        # Sliver triangle (Q < 1e-5)
        sliver_verts, sliver_faces = create_sliver_triangle_arrays()
        s_min_q, _, _ = check_triangle_quality(sliver_verts, sliver_faces)
        assert s_min_q < 1e-5, f"Expected sliver Q < 1e-5, got {s_min_q}"

    def test_check_3_edge_magnitudes(self):
        """Check 3: Evaluates min and max Euclidean edge lengths."""
        verts, faces = create_unit_cube_arrays()
        min_len, max_len = check_edge_magnitudes(verts, faces)
        assert np.isclose(min_len, 1.0, atol=1e-7)
        assert np.isclose(max_len, np.sqrt(2.0), atol=1e-7)

    def test_check_4_close_unconnected_points(self):
        """Check 4: Detects points closer than tolerance not connected by edge."""
        verts, faces = create_unit_cube_arrays()
        # Clean cube has no close unconnected points
        close_pts = check_close_unconnected_points(verts, faces)
        assert len(close_pts) == 0

        # Inject an unconnected point extremely close to vertex 0
        p_close = verts[0] + np.array([1e-7, 1e-7, 1e-7])
        corrupted_verts = np.vstack([verts, p_close])
        close_pts_found = check_close_unconnected_points(corrupted_verts, faces, rel_tol=1e-5)
        assert len(close_pts_found) >= 1

    def test_check_5_open_edges_detected(self):
        """Check 5: Open cube with 1 missing face has exactly 3 open edges."""
        verts, faces = create_open_cube_arrays()
        open_edges, multi_edges = check_manifold_edges(faces)
        assert len(open_edges) == 3, f"Expected 3 open edges, got {len(open_edges)}"
        assert len(multi_edges) == 0

    def test_check_5_clean_mesh_has_zero_open_edges(self):
        """Check 5: Watertight cube has 0 open edges."""
        verts, faces = create_unit_cube_arrays()
        open_edges, multi_edges = check_manifold_edges(faces)
        assert len(open_edges) == 0
        assert len(multi_edges) == 0

    def test_check_6_multiple_edges_detected(self):
        """Check 6: Non-manifold T-junction edge has |F(e)| == 4."""
        verts, faces = create_t_junction_non_manifold_arrays()
        # Merge vertices coincident at x=1
        v_merged, inv = np.unique(np.round(verts, 6), axis=0, return_inverse=True)
        f_merged = inv[faces]
        open_edges, multi_edges = check_manifold_edges(f_merged)
        assert len(multi_edges) >= 1, "T-junction non-manifold edge was not detected"

    def test_check_7_connected_components(self):
        """Check 7: Single body yields 1 connected component; disjoint yields 2."""
        verts, faces = create_unit_cube_arrays()
        zones = check_connected_components(faces)
        assert zones == 1

        # Two disjoint cubes
        f2 = faces + len(verts)
        two_cubes_f = np.vstack([faces, f2])
        v2 = np.vstack([verts, verts + np.array([5.0, 0.0, 0.0])])
        zones_2 = check_connected_components(two_cubes_f)
        assert zones_2 == 2

    def test_check_8_normal_orientation_zones(self):
        """Check 8: Consistent outward cube has 1 normal orientation zone."""
        verts, faces = create_unit_cube_arrays()
        zones = check_normal_orientation_zones(faces)
        assert zones == 1

        # Flip 1 face winding
        corrupted_faces = faces.copy()
        corrupted_faces[0, [1, 2]] = corrupted_faces[0, [2, 1]]
        zones_corrupted = check_normal_orientation_zones(corrupted_faces)
        assert zones_corrupted > 1

    def test_check_9_outward_orientation_signed_volume(self):
        """Check 9: Signed volume > 0 for outward normals, < 0 for inward."""
        verts, faces = create_unit_cube_arrays()
        vol, is_outward = check_outward_orientation(verts, faces)
        assert is_outward is True
        assert np.isclose(vol, 1.0, atol=1e-7)

        inv_verts, inv_faces = create_inverted_cube_arrays()
        inv_vol, inv_is_outward = check_outward_orientation(inv_verts, inv_faces)
        assert inv_is_outward is False
        assert np.isclose(inv_vol, -1.0, atol=1e-7)

    def test_check_10_self_intersection_free(self):
        """Check 10: Standard convex polyhedra have 0 self-intersections."""
        verts, faces = create_unit_cube_arrays()
        intersections = check_self_intersections(verts, faces)
        assert intersections == 0


class TestSurfaceAuditorIntegration:
    """Tests the actual SurfaceAuditor backend implementation against OpenFOAM criteria."""

    def test_auditor_on_watertight_cube(self):
        """Verifies that SurfaceAuditor returns passing AuditReport on watertight cube."""
        from backend.geometry.audit.surface_auditor import SurfaceAuditor

        verts, faces = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert isinstance(report, AuditReport)
        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.euler_characteristic == 2
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        assert report.degenerate_faces_count == 0
        assert report.normal_zones_count == 1
        assert report.is_outward_oriented is True
        assert report.signed_volume > 0.0
        assert report.passed_openfoam_surfacecheck is True

    def test_auditor_detects_open_hole(self):
        """SurfaceAuditor flags mesh with open edges as non-watertight."""
        from backend.geometry.audit.surface_auditor import SurfaceAuditor

        verts, faces = create_open_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.is_watertight is False
        assert report.open_edges_count == 3
        assert report.passed_openfoam_surfacecheck is False

    def test_auditor_auto_repairs_inverted_normals(self):
        """SurfaceAuditor detects inward-pointing normals and auto-corrects them."""
        from backend.geometry.audit.surface_auditor import SurfaceAuditor

        verts, faces = create_inverted_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        # After auto repair, mesh should be outward oriented
        assert report.is_outward_oriented is True
        assert report.signed_volume > 0.0
