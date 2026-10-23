"""Adversarial stress-test suite for SurfaceAuditor and OpenFOAM surfaceCheck compliance.

Constructed by Challenger 1 (Geometry Audit & OpenFOAM surfaceCheck).
Adversarially tests synthetic corner cases, numerical stability, and failure modes:
1. Clean watertight invariant checks (cube, tetrahedron, octahedron, icosphere, torus)
2. Non-manifold edges (3-face Y-junction, 4-face X-junction)
3. T-junctions (geometric edge-split vertices with open boundaries)
4. Bow-tie / pinch vertices (two polyhedra touching at a single vertex)
5. Single-face and multi-face holes (open boundaries, non-watertight)
6. Multiple connected components (disjoint, nested, mixed-orientation components)
7. Inverted normals and auto-repair (full inversion, mixed winding, auto_repair=False)
8. Degenerate triangles (duplicate indices, collinear points, coincident coordinates, area < 1e-14)
9. Sliver triangles (needle and cap slivers with Q < 1e-5, quality histogram validation)
10. Self-intersecting meshes (intersecting pairs, interpenetrating cubes, coplanar/parallel edge cases)
"""

from typing import List, Tuple
import numpy as np
import pytest

from backend.geometry.audit.quality_metrics import QualityMetrics
from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import AuditReport, SurfaceMesh


# Helpers to generate synthetic geometries


def make_regular_tetrahedron() -> Tuple[np.ndarray, np.ndarray]:
    """Generates canonical regular tetrahedron centered at origin."""
    s = 1.0 / np.sqrt(3.0)
    vertices = np.array(
        [
            [1.0, 1.0, 1.0],
            [-1.0, -1.0, 1.0],
            [-1.0, 1.0, -1.0],
            [1.0, -1.0, -1.0],
        ],
        dtype=np.float64,
    ) * s
    # 4 faces with outward winding
    faces = np.array(
        [
            [0, 2, 1],
            [0, 1, 3],
            [0, 3, 2],
            [1, 2, 3],
        ],
        dtype=np.int32,
    )
    return vertices, faces


def make_regular_octahedron() -> Tuple[np.ndarray, np.ndarray]:
    """Generates regular octahedron with 6 vertices and 8 faces."""
    vertices = np.array(
        [
            [1.0, 0.0, 0.0],   # 0
            [-1.0, 0.0, 0.0],  # 1
            [0.0, 1.0, 0.0],   # 2
            [0.0, -1.0, 0.0],  # 3
            [0.0, 0.0, 1.0],   # 4 top
            [0.0, 0.0, -1.0],  # 5 bottom
        ],
        dtype=np.float64,
    )
    faces = np.array(
        [
            [4, 0, 2],
            [4, 2, 1],
            [4, 1, 3],
            [4, 3, 0],
            [5, 2, 0],
            [5, 1, 2],
            [5, 3, 1],
            [5, 0, 3],
        ],
        dtype=np.int32,
    )
    return vertices, faces


def make_torus(r_major: float = 1.0, r_minor: float = 0.3, n_major: int = 8, n_minor: int = 8) -> Tuple[np.ndarray, np.ndarray]:
    """Generates a genus-1 torus with chi = 0."""
    u = np.linspace(0, 2 * np.pi, n_major, endpoint=False)
    v = np.linspace(0, 2 * np.pi, n_minor, endpoint=False)
    u_grid, v_grid = np.meshgrid(u, v, indexing="ij")
    u_flat = u_grid.flatten()
    v_flat = v_grid.flatten()

    x = (r_major + r_minor * np.cos(v_flat)) * np.cos(u_flat)
    y = (r_major + r_minor * np.cos(v_flat)) * np.sin(u_flat)
    z = r_minor * np.sin(v_flat)
    vertices = np.column_stack([x, y, z])

    faces = []
    for i in range(n_major):
        i_next = (i + 1) % n_major
        for j in range(n_minor):
            j_next = (j + 1) % n_minor
            p0 = i * n_minor + j
            p1 = i_next * n_minor + j
            p2 = i_next * n_minor + j_next
            p3 = i * n_minor + j_next
            faces.append([p0, p1, p2])
            faces.append([p0, p2, p3])

    return vertices, np.array(faces, dtype=np.int32)


# Test Classes


class TestAdversarialWatertightAndEuler:
    """Stress-tests topological closedness, Euler characteristic chi, and boundary edges."""

    def test_clean_polyhedra_invariants(self):
        """Watertight genus-0 polyhedra satisfy chi = 2, open = 0, multi = 0, V > 0."""
        auditor = SurfaceAuditor()

        # 1. Tetrahedron
        v_tet, f_tet = make_regular_tetrahedron()
        mesh_tet = SurfaceMesh(vertices=v_tet, faces=f_tet)
        rep_tet = auditor.audit(mesh_tet)
        assert rep_tet.is_watertight is True
        assert rep_tet.is_manifold is True
        assert rep_tet.euler_characteristic == 2
        assert rep_tet.open_edges_count == 0
        assert rep_tet.multi_edges_count == 0
        assert rep_tet.signed_volume > 0.0
        assert rep_tet.is_outward_oriented is True
        assert rep_tet.passed_openfoam_surfacecheck is True

        # 2. Octahedron
        v_oct, f_oct = make_regular_octahedron()
        mesh_oct = SurfaceMesh(vertices=v_oct, faces=f_oct)
        rep_oct = auditor.audit(mesh_oct)
        assert rep_oct.is_watertight is True
        assert rep_oct.is_manifold is True
        assert rep_oct.euler_characteristic == 2
        assert rep_oct.open_edges_count == 0
        assert rep_oct.multi_edges_count == 0
        assert rep_oct.signed_volume > 0.0
        assert rep_oct.passed_openfoam_surfacecheck is True

    def test_torus_genus_1_fails_genus_0_surfacecheck(self):
        """A torus has chi = 0, open = 0, multi = 0, but fails OpenFOAM snappy check because chi != 2."""
        auditor = SurfaceAuditor()
        v_tor, f_tor = make_torus()
        mesh_tor = SurfaceMesh(vertices=v_tor, faces=f_tor)
        report = auditor.audit(mesh_tor)

        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        assert report.euler_characteristic == 0  # chi = V - E + F = 0 for torus
        assert report.passed_openfoam_surfacecheck is False  # Must be False for non-genus-0


class TestAdversarialNonManifoldEdges:
    """Stress-tests detection of non-manifold edges (> 2 faces meeting at an edge)."""

    def test_three_faces_sharing_edge_y_junction(self):
        """A 3-face Y-junction has exactly one multi-edge with 3 incident faces."""
        vertices = np.array(
            [
                [0.0, 0.0, 0.0],  # 0
                [0.0, 0.0, 1.0],  # 1 (shared edge 0-1)
                [1.0, 0.0, 0.0],  # 2 fin 1
                [-1.0, 0.0, 0.0], # 3 fin 2
                [0.0, 1.0, 0.0],  # 4 fin 3
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 1, 2],
                [0, 1, 3],
                [0, 1, 4],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=vertices, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.is_manifold is False
        assert report.is_watertight is False
        assert report.multi_edges_count == 1
        assert (0, 1) in report.details["multi_edges"]
        assert report.open_edges_count == 6  # Outer edges are open
        assert report.passed_openfoam_surfacecheck is False

    def test_four_faces_sharing_edge_x_junction(self):
        """Two closed cubes sharing a single edge create an edge with 4 incident faces."""
        from tests.conftest import create_t_junction_non_manifold_arrays

        all_v, all_f = create_t_junction_non_manifold_arrays()
        # Merge coincident vertices along the shared edge
        _, inv = np.unique(np.round(all_v, 6), axis=0, return_inverse=True)
        merged_faces = inv[all_f]
        unique_v = np.unique(np.round(all_v, 6), axis=0)

        mesh = SurfaceMesh(vertices=unique_v, faces=merged_faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.is_manifold is False
        assert report.multi_edges_count >= 1
        assert report.passed_openfoam_surfacecheck is False


class TestAdversarialBowTieVertices:
    """Stress-tests bow-tie / pinch vertices (two solids meeting at a single vertex)."""

    def test_two_tetrahedra_touching_at_single_vertex(self):
        """Two tetrahedra meeting only at vertex 0 have manifold edges but non-manifold vertex.

        Euler chi = 7 - 12 + 8 = 3 != 2. Must be rejected by OpenFOAM surfaceCheck.
        """
        vertices = np.array(
            [
                [0.0, 0.0, 0.0],   # 0 (Pinch / Bow-tie vertex)
                [1.0, 0.0, 0.0],   # 1
                [0.5, 1.0, 0.0],   # 2
                [0.5, 0.5, 1.0],   # 3
                [-1.0, 0.0, 0.0],  # 4
                [-0.5, -1.0, 0.0], # 5
                [-0.5, -0.5, -1.0],# 6
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                # Tet 1
                [0, 2, 1],
                [0, 3, 2],
                [0, 1, 3],
                [1, 2, 3],
                # Tet 2
                [0, 5, 4],
                [0, 6, 5],
                [0, 4, 6],
                [4, 5, 6],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=vertices, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        # Every edge has exactly 2 incident faces
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        # But Euler characteristic is 7 - 12 + 8 = 3
        assert report.euler_characteristic == 3
        # Must fail OpenFOAM surfaceCheck due to euler_chi != 2
        assert report.passed_openfoam_surfacecheck is False

    def test_limitation_is_manifold_only_checks_edges_not_vertices(self):
        """Documents structural limitation: SurfaceAuditor.is_manifold flags True on vertex bow-ties."""
        vertices = np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.5, 1.0, 0.0],
                [0.5, 0.5, 1.0],
                [-1.0, 0.0, 0.0],
                [-0.5, -1.0, 0.0],
                [-0.5, -0.5, -1.0],
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 2, 1],
                [0, 3, 2],
                [0, 1, 3],
                [1, 2, 3],
                [0, 5, 4],
                [0, 6, 5],
                [0, 4, 6],
                [4, 5, 6],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=vertices, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        # NOTE: SurfaceAuditor defines is_manifold = (len(multi_edges) == 0).
        # Thus, vertex bow-ties are reported as is_manifold=True despite being non-manifold at vertex 0!
        assert report.is_manifold is True


class TestAdversarialTJunctions:
    """Stress-tests geometric T-junctions where a vertex splits an edge geometrically without topological welding."""

    def test_unwelded_edge_split_t_junction_detected(self):
        """One square quad split into 2 triangles, adjacent to a face with an edge midpoint vertex."""
        vertices = np.array(
            [
                [0.0, 0.0, 0.0],  # 0
                [1.0, 0.0, 0.0],  # 1 (T-junction midpoint on edge 0-2)
                [2.0, 0.0, 0.0],  # 2
                [2.0, 2.0, 0.0],  # 3
                [0.0, 2.0, 0.0],  # 4
                [1.0, -1.0, 0.0], # 5
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 2, 3],
                [0, 3, 4],
                [0, 1, 5],
                [1, 2, 5],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=vertices, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        # Edge (0, 2) is incident to 1 face; edges (0, 1) and (1, 2) are incident to 1 face.
        # Open edges are detected!
        assert report.is_watertight is False
        assert report.open_edges_count >= 3
        assert report.passed_openfoam_surfacecheck is False


class TestAdversarialHoles:
    """Stress-tests single-face and multi-face boundary holes."""

    def test_single_face_hole_in_watertight_cube(self):
        """Removing exactly 1 face leaves 3 open boundary edges and drops chi to 1."""
        from tests.conftest import create_unit_cube_arrays

        verts, faces = create_unit_cube_arrays()
        open_mesh = SurfaceMesh(vertices=verts, faces=faces[:-1])

        auditor = SurfaceAuditor()
        report = auditor.audit(open_mesh)

        assert report.is_watertight is False
        assert report.open_edges_count == 3
        assert report.multi_edges_count == 0
        assert report.euler_characteristic == 1  # 8 - 18 + 11 = 1
        assert report.passed_openfoam_surfacecheck is False

    def test_two_adjacent_faces_hole_leaves_four_open_edges(self):
        """Removing 2 adjacent triangles leaves 4 open edges and 1 unreferenced internal edge."""
        from tests.conftest import create_unit_cube_arrays

        verts, faces = create_unit_cube_arrays()
        # Faces 0 and 1 form the bottom face [0,2,1] and [0,3,2]
        # Removing both leaves open square boundary (4 open edges, 17 remaining edges)
        two_hole_mesh = SurfaceMesh(vertices=verts, faces=faces[2:])

        auditor = SurfaceAuditor()
        report = auditor.audit(two_hole_mesh)

        assert report.is_watertight is False
        assert report.open_edges_count == 4
        assert report.multi_edges_count == 0
        # 8 referenced vertices, 17 remaining edges, 10 remaining faces: 8 - 17 + 10 = 1
        assert report.euler_characteristic == 1
        assert report.passed_openfoam_surfacecheck is False


class TestAdversarialConnectedComponents:
    """Stress-tests meshes with multiple components and mixed orientations."""

    def test_two_disjoint_watertight_cubes(self):
        """Two separated cubes yield 2 connected components, chi = 4, 2 normal zones."""
        from tests.conftest import create_unit_cube_arrays

        v1, f1 = create_unit_cube_arrays()
        v2, f2 = create_unit_cube_arrays()
        v2_shifted = v2 + np.array([5.0, 0.0, 0.0])

        all_v = np.vstack([v1, v2_shifted])
        all_f = np.vstack([f1, f2 + len(v1)])

        mesh = SurfaceMesh(vertices=all_v, faces=all_f)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.details["connected_components"] == 2
        assert report.normal_zones_count == 2
        assert report.euler_characteristic == 4  # 16 - 36 + 24 = 4
        assert report.passed_openfoam_surfacecheck is False

    def test_nested_cubes_with_mixed_orientations(self):
        """Outer cube outward oriented, inner cube inverted oriented."""
        from tests.conftest import create_unit_cube_arrays

        v_out, f_out = create_unit_cube_arrays()
        v_out = v_out * 2.0

        v_in, f_in = create_unit_cube_arrays()
        f_in_inv = f_in.copy()
        f_in_inv[:, [1, 2]] = f_in_inv[:, [2, 1]]
        v_in = v_in * 0.5 + np.array([0.75, 0.75, 0.75])

        all_v = np.vstack([v_out, v_in])
        all_f = np.vstack([f_out, f_in_inv + len(v_out)])

        mesh = SurfaceMesh(vertices=all_v, faces=all_f)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        assert report.is_watertight is True
        assert report.normal_zones_count == 2
        assert report.details["connected_components"] == 2
        assert report.passed_openfoam_surfacecheck is False

    def test_multi_component_global_auto_repair_inversion_hazard(self):
        """Verify per-component normal auto-repair: inverts only inverted components without corrupting outward ones."""
        from tests.conftest import create_unit_cube_arrays

        # Component 1: Outward cube (V = +1.0)
        v1, f1 = create_unit_cube_arrays()

        # Component 2: Inverted large cube (V = -8.0)
        v2, f2 = create_unit_cube_arrays()
        v2 = v2 * 2.0 + np.array([5.0, 0.0, 0.0])
        f2_inv = f2.copy()
        f2_inv[:, [1, 2]] = f2_inv[:, [2, 1]]

        all_v = np.vstack([v1, v2])
        all_f = np.vstack([f1, f2_inv + len(v1)])

        mesh = SurfaceMesh(vertices=all_v, faces=all_f)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        # Per-component auto-repair leaves component 1 positive and repairs component 2
        v1_after = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[:len(f1)])
        v2_after = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces[len(f1):])
        assert v1_after == 1.0, "Component 1 must remain outward oriented"
        assert v2_after == 8.0, "Component 2 must be repaired to outward orientation"


class TestAdversarialInvertedNormals:
    """Stress-tests normal detection, auto-repair, and mixed winding."""

    def test_fully_inverted_watertight_cube_without_auto_repair(self):
        """When auto_repair_normals=False, inverted cube is flagged with negative volume."""
        from tests.conftest import create_inverted_cube_arrays

        verts, faces = create_inverted_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor(auto_repair_normals=False)
        report = auditor.audit(mesh)

        assert report.is_outward_oriented is False
        assert report.signed_volume < 0.0
        assert report.passed_openfoam_surfacecheck is False

    def test_fully_inverted_watertight_cube_with_auto_repair(self):
        """When auto_repair_normals=True, inverted cube is auto-flipped to outward."""
        from tests.conftest import create_inverted_cube_arrays

        verts, faces = create_inverted_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        assert report.is_outward_oriented is True
        assert report.signed_volume > 0.0
        assert report.passed_openfoam_surfacecheck is True

    def test_single_face_flipped_creates_inconsistent_edges(self):
        """Inverting a single face in a watertight mesh creates 3 inconsistent half-edges."""
        from tests.conftest import create_unit_cube_arrays

        verts, faces = create_unit_cube_arrays()
        corrupted_faces = faces.copy()
        corrupted_faces[0, [1, 2]] = corrupted_faces[0, [2, 1]]

        mesh = SurfaceMesh(vertices=verts, faces=corrupted_faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.details["inconsistent_edges_count"] == 3
        assert report.normal_zones_count == 2
        assert report.passed_openfoam_surfacecheck is False


class TestAdversarialDegenerateTriangles:
    """Stress-tests detection of zero-area, duplicate index, collinear and coincident triangles."""

    def test_duplicate_vertex_indices(self):
        """Triangles like [0, 0, 1] or [1, 2, 1] must be flagged as illegal."""
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], dtype=np.float64)
        faces = np.array([[0, 0, 1], [0, 1, 2], [1, 2, 1]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        illegal = QualityMetrics.find_illegal_faces(mesh)
        assert 0 in illegal
        assert 2 in illegal
        assert 1 not in illegal

    def test_collinear_vertices_zero_area(self):
        """Three distinct vertices on a line produce area = 0.0 and must be flagged."""
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [2.0, 0.0, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        illegal = QualityMetrics.find_illegal_faces(mesh)
        assert 0 in illegal

    def test_microscopic_area_under_tolerance(self):
        """A triangle with area 1e-16 < 1e-14 must be flagged as degenerate."""
        verts = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.5, 1e-16, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        illegal = QualityMetrics.find_illegal_faces(mesh, area_tol=1e-14)
        assert 0 in illegal

    def test_coincident_coordinates_different_indices(self):
        """Two vertices with different indices but identical coordinates yield area = 0."""
        verts = np.array(
            [
                [0.0, 0.0, 0.0],  # 0
                [0.0, 0.0, 0.0],  # 1 coincident with 0
                [1.0, 0.0, 0.0],  # 2
            ],
            dtype=np.float64,
        )
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        illegal = QualityMetrics.find_illegal_faces(mesh)
        assert 0 in illegal


class TestAdversarialSliverTriangles:
    """Stress-tests equilateral quality metric Q across needles, caps, and histograms."""

    def test_needle_sliver_quality_under_1e_5(self):
        """Needle triangle (base=1000, height=1e-6) has Q << 1e-5."""
        verts = np.array([[0.0, 0.0, 0.0], [1000.0, 0.0, 0.0], [500.0, 1e-6, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        q_min, q_max, hist, bad = QualityMetrics.compute_equilateral_quality(mesh)
        assert q_min < 1e-5
        assert len(hist) == 20
        assert hist[0] == 1  # Landed in lowest bin [0.0, 0.05)

    def test_cap_sliver_quality(self):
        """Flat obtuse cap triangle with one large angle."""
        verts = np.array([[0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [1.0, 1e-7, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        q_min, q_max, hist, bad = QualityMetrics.compute_equilateral_quality(mesh)
        assert q_min < 1e-5

    def test_perfect_equilateral_triangle_quality_is_one(self):
        """Equilateral triangle has Q exactly 1.0."""
        s = 2.0
        h = s * np.sqrt(3.0) / 2.0
        verts = np.array([[0.0, 0.0, 0.0], [s, 0.0, 0.0], [s / 2.0, h, 0.0]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        q_min, q_max, hist, bad = QualityMetrics.compute_equilateral_quality(mesh)
        assert np.isclose(q_min, 1.0, atol=1e-12)
        assert hist[-1] == 1  # Landed in highest bin [0.95, 1.0]

    def test_quality_histogram_bins_and_sum(self):
        """Histogram always contains exactly 20 bins whose sum equals total face count."""
        from tests.conftest import create_unit_cube_arrays

        verts, faces = create_unit_cube_arrays()
        mesh = SurfaceMesh(vertices=verts, faces=faces)

        _, _, hist, _ = QualityMetrics.compute_equilateral_quality(mesh)
        assert len(hist) == 20
        assert sum(hist) == len(faces)

    def test_defect_sliver_mesh_passes_surfacecheck_unflagged(self):
        """Verify that SurfaceAuditor rejects meshes with sliver triangles Q < 1e-5 in OpenFOAM surfaceCheck."""
        verts = np.array(
            [
                [0.0, 0.0, 0.0],
                [1000.0, 0.0, 0.0],
                [500.0, 1e-4, 0.0],
                [500.0, 1e-5, 1e-4],
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 2, 1],
                [0, 1, 3],
                [0, 3, 2],
                [1, 2, 3],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        # Verified that Q < 1e-5
        assert report.quality_min < 1e-5
        # Enforced: passed_openfoam_surfacecheck must reject meshes with bad faces
        assert report.passed_openfoam_surfacecheck is False


class TestAdversarialSelfIntersections:
    """Stress-tests triangle-triangle intersection detection and numerical stability."""

    def test_two_intersecting_triangles_with_padding_faces_detected(self):
        """When total faces >= 4, intersecting triangles are detected by Moeller test."""
        verts = np.array(
            [
                [-1.0, -1.0, 0.0],  # 0
                [1.0, -1.0, 0.0],   # 1
                [0.0, 1.0, 0.0],    # 2
                [0.0, -0.5, -1.0],  # 3
                [0.0, -0.5, 1.0],   # 4
                [0.0, 0.5, 0.0],    # 5
                # Padding non-overlapping faces to satisfy n_faces >= 4 heuristic
                [10.0, 10.0, 10.0], # 6
                [11.0, 10.0, 10.0], # 7
                [10.0, 11.0, 10.0], # 8
                [20.0, 20.0, 20.0], # 9
                [21.0, 20.0, 20.0], # 10
                [20.0, 21.0, 20.0], # 11
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 1, 2],  # T1
                [3, 4, 5],  # T2
                [6, 7, 8],
                [9, 10, 11],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        count = auditor._detect_self_intersections(mesh)
        assert count == 1, f"Expected 1 self-intersection, got {count}"

    def test_defect_n_faces_under_4_silently_ignores_intersections(self):
        """Verify that meshes with n_faces in [2, 3] are audited for self-intersections."""
        verts = np.array(
            [
                [-1.0, -1.0, 0.0],
                [1.0, -1.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.0, -0.5, -1.0],
                [0.0, -0.5, 1.0],
                [0.0, 0.5, 0.0],
            ],
            dtype=np.float64,
        )
        faces = np.array([[0, 1, 2], [3, 4, 5]], dtype=np.int32)
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        count = auditor._detect_self_intersections(mesh)
        assert count == 1, "Meshes with n_faces in [2, 3] must be checked for self-intersections"

    def test_interpenetrating_cubes_detected(self):
        """Two interpenetrating closed cubes must produce self-intersections."""
        from tests.conftest import create_unit_cube_arrays

        v1, f1 = create_unit_cube_arrays()
        v2, f2 = create_unit_cube_arrays()
        v2_shifted = v2 + np.array([0.5, 0.5, 0.5])

        all_v = np.vstack([v1, v2_shifted])
        all_f = np.vstack([f1, f2 + len(v1)])

        mesh = SurfaceMesh(vertices=all_v, faces=all_f)
        auditor = SurfaceAuditor()
        report = auditor.audit(mesh)

        assert report.self_intersections_count > 0
        assert report.passed_openfoam_surfacecheck is False

    def test_defect_moeller_division_by_zero_and_missed_intersection_when_edge_in_plane(self):
        """Verify that Moeller test detects piercing intersection when edge lies on adjacent plane without NaN divide."""
        # Triangle 1 in XY plane (Z=0) with edge along X-axis
        # Triangle 2 in XZ plane (Y=0) piercing Triangle 1
        verts = np.array(
            [
                [-1.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.0, 2.0, 0.0],
                [-0.5, 0.0, -1.0],
                [0.5, 0.0, -1.0],
                [0.0, 0.0, 1.0],
                [10.0, 10.0, 10.0],
                [11.0, 10.0, 10.0],
                [10.0, 11.0, 10.0],
                [20.0, 20.0, 20.0],
                [21.0, 20.0, 20.0],
                [20.0, 21.0, 20.0],
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 1, 2],
                [3, 4, 5],
                [6, 7, 8],
                [9, 10, 11],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        count = auditor._detect_self_intersections(mesh)
        assert count == 1, "Moeller test with edge in plane must detect piercing intersection without NaN divide"

    def test_defect_coplanar_overlapping_triangles_missed(self):
        """Documents defect: coplanar overlapping triangles return False unconditionally."""
        t1 = np.array([[0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [0.0, 2.0, 0.0]])
        t2 = np.array([[0.5, 0.5, 0.0], [2.5, 0.5, 0.0], [0.5, 2.5, 0.0]])
        res = SurfaceAuditor._triangles_intersect_moeller(t1, t2)
        # Returns False unconditionally because np.all(dist1 == 0.0)
        assert res is False

    def test_defect_vertex_sharing_penetrating_triangles_missed(self):
        """Documents defect: triangles sharing 1 vertex are skipped despite intersecting interior."""
        verts = np.array(
            [
                [0.0, 0.0, 0.0],  # 0 shared vertex
                [1.0, 0.0, 0.0],
                [0.0, 1.0, 0.0],
                [0.5, 0.5, -1.0],
                [0.5, 0.5, 1.0],
                [10.0, 10.0, 10.0],
                [11.0, 10.0, 10.0],
                [10.0, 11.0, 10.0],
                [20.0, 20.0, 20.0],
                [21.0, 20.0, 20.0],
                [20.0, 21.0, 20.0],
            ],
            dtype=np.float64,
        )
        faces = np.array(
            [
                [0, 1, 2],
                [0, 3, 4],  # Penetrates T1 at (0.5, 0.5, 0.0)
                [5, 6, 7],
                [8, 9, 10],
            ],
            dtype=np.int32,
        )
        mesh = SurfaceMesh(vertices=verts, faces=faces)
        auditor = SurfaceAuditor()
        count = auditor._detect_self_intersections(mesh)
        # Skipped due to len(set(f_i).intersection(f_j)) > 0
        assert count == 0
