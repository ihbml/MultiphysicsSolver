"""Topological and geometric surface auditor for CFD and OpenFOAM snappyHexMesh."""

from __future__ import annotations

from collections import deque
import logging
from typing import Any, Dict, List, Optional, Set, Tuple
import numpy as np

from backend.geometry.audit.quality_metrics import QualityMetrics
from backend.geometry.models import AuditReport, SurfaceMesh

logger = logging.getLogger(__name__)

try:
    import trimesh
    HAS_TRIMESH = True
except ImportError:
    HAS_TRIMESH = False


class SurfaceAuditor:
    """Comprehensive auditor verifying 2-manifold closedness, Euler characteristic,

    normal consistency, signed volume outwardness, and OpenFOAM surfaceCheck criteria.
    """

    def __init__(
        self,
        auto_repair_normals: bool = True,
        area_tol: float = 1e-14,
        min_quality_tol: float = 1e-5,
    ) -> None:
        """Initialize SurfaceAuditor.

        Args:
            auto_repair_normals: If True, automatically invert winding when signed volume is negative.
            area_tol: Minimum allowable triangle area to avoid degenerate facets.
            min_quality_tol: Minimum equilateral triangle quality threshold (OpenFOAM surfaceCheck).
        """
        self.auto_repair_normals = auto_repair_normals
        self.area_tol = area_tol
        self.min_quality_tol = min_quality_tol

    def audit(self, mesh: SurfaceMesh) -> AuditReport:
        """Execute complete topological and geometric audit on the given surface mesh.

        Args:
            mesh: Input SurfaceMesh (modified in-place if auto_repair_normals is True).

        Returns:
            AuditReport containing full diagnostic results.
        """
        v_count = len(mesh.vertices)
        f_count = len(mesh.faces)

        if v_count == 0 or f_count == 0:
            raise ValueError("Cannot audit empty surface mesh.")

        # 1. Edge-Face Adjacency and 2-Manifold Analysis
        edge_to_faces, directed_edges = self._build_edge_structures(mesh.faces)
        num_edges = len(edge_to_faces)

        open_edges: List[Tuple[int, int]] = []
        multi_edges: List[Tuple[int, int]] = []

        for edge, faces in edge_to_faces.items():
            if len(faces) == 1:
                open_edges.append(edge)
            elif len(faces) > 2:
                multi_edges.append(edge)

        is_watertight = (len(open_edges) == 0 and len(multi_edges) == 0)
        is_manifold = (len(multi_edges) == 0)

        # 2. Euler-Poincaré Characteristic: chi = V - E + F
        # Use referenced vertex count in case there are unreferenced vertices
        referenced_verts = set(mesh.faces.flatten())
        effective_v = len(referenced_verts)
        euler_chi = effective_v - num_edges + f_count

        # 3. Connected Components and Normal Orientation Zones
        components, normal_zones_count, inconsistent_edges = self._analyze_normal_consistency(
            mesh.faces, edge_to_faces, directed_edges
        )
        num_connected_parts = len(components)

        # 4. Per-Component Signed Volume and Normal Auto-Repair
        comp_volumes: List[float] = []
        repaired_any = False

        for comp_idx, comp_face_list in enumerate(components):
            comp_f = np.array(comp_face_list, dtype=np.int32)
            v_k = self.compute_signed_volume(mesh.vertices, mesh.faces[comp_f])

            # Check if this specific component has open boundary edges
            comp_set = set(comp_face_list)
            comp_open_edges = [
                e for e in open_edges
                if any(fi in comp_set for fi in edge_to_faces[e])
            ]
            is_comp_closed = len(comp_open_edges) == 0

            if v_k < 0.0 and is_comp_closed and self.auto_repair_normals:
                logger.info(
                    "Component %d: negative signed volume (%e). Inverting winding for component.",
                    comp_idx,
                    v_k,
                )
                sub = mesh.faces[comp_f].copy()
                sub[:, [1, 2]] = sub[:, [2, 1]]
                mesh.faces[comp_f] = sub
                v_k = -v_k
                repaired_any = True

            comp_volumes.append(v_k)

        if repaired_any:
            mesh.normals = mesh.compute_face_normals()
            edge_to_faces, directed_edges = self._build_edge_structures(mesh.faces)
            components, normal_zones_count, inconsistent_edges = self._analyze_normal_consistency(
                mesh.faces, edge_to_faces, directed_edges
            )

        signed_volume = float(np.sum(comp_volumes)) if comp_volumes else 0.0
        is_outward = all(v > 0.0 for v in comp_volumes) if comp_volumes else False

        # 5. Quality Metrics & OpenFOAM surfaceCheck
        illegal_faces = QualityMetrics.find_illegal_faces(mesh, area_tol=self.area_tol)
        q_min, q_max, q_hist, bad_faces = QualityMetrics.compute_equilateral_quality(
            mesh, min_q_tol=self.min_quality_tol
        )
        _, min_edge_len, max_edge_len, avg_edge_len = QualityMetrics.compute_edge_lengths(mesh)
        close_points = QualityMetrics.find_close_unconnected_points(mesh)

        # 6. Self-Intersection Detection
        self_intersections_count = self._detect_self_intersections(mesh)

        # 7. Bounding Box
        bbox = mesh.get_bounding_box()

        # 8. OpenFOAM surfaceCheck verdict
        # Strict OpenFOAM surfaceCheck requirements:
        # - 0 illegal faces
        # - 0 open edges
        # - 0 multi edges
        # - 0 bad quality faces (Q >= min_quality_tol)
        # - 1 normal zone (consistent outward normals)
        # - Euler chi == 2 for genus-0 solid body
        # - 0 self-intersections
        # - 0 close unconnected points
        passed_openfoam = (
            len(illegal_faces) == 0
            and len(open_edges) == 0
            and len(multi_edges) == 0
            and len(bad_faces) == 0
            and normal_zones_count == 1
            and is_outward
            and (euler_chi == 2)
            and (self_intersections_count == 0)
            and (len(close_points) == 0)
        )

        details: Dict[str, Any] = {
            "num_vertices": v_count,
            "num_faces": f_count,
            "num_edges": num_edges,
            "connected_components": num_connected_parts,
            "min_edge_length": min_edge_len,
            "max_edge_length": max_edge_len,
            "avg_edge_length": avg_edge_len,
            "bad_faces_count": len(bad_faces),
            "close_points_count": len(close_points),
            "inconsistent_edges_count": len(inconsistent_edges),
            "open_edges": open_edges,
            "multi_edges": multi_edges,
            "illegal_faces": illegal_faces,
            "close_points": close_points,
        }

        return AuditReport(
            is_watertight=is_watertight,
            is_manifold=is_manifold,
            euler_characteristic=euler_chi,
            open_edges_count=len(open_edges),
            multi_edges_count=len(multi_edges),
            degenerate_faces_count=len(illegal_faces),
            normal_zones_count=normal_zones_count,
            signed_volume=signed_volume,
            is_outward_oriented=is_outward,
            quality_min=q_min,
            quality_histogram=q_hist,
            self_intersections_count=self_intersections_count,
            bounding_box_min=bbox.min_pt,
            bounding_box_max=bbox.max_pt,
            passed_openfoam_surfacecheck=passed_openfoam,
            details=details,
        )

    @staticmethod
    def compute_signed_volume(vertices: np.ndarray, faces: np.ndarray) -> float:
        """Compute the signed enclosed volume of a surface mesh using Gauss Divergence Theorem.

        V = 1/6 * sum_i (v0_i . (v1_i x v2_i))

        Positive volume indicates outward-pointing facet normals.
        """
        if len(faces) == 0 or len(vertices) == 0:
            return 0.0
        v0 = vertices[faces[:, 0]]
        v1 = vertices[faces[:, 1]]
        v2 = vertices[faces[:, 2]]
        cross = np.cross(v0, v1)
        signed_vol = float(np.sum(cross * v2) / 6.0)
        return signed_vol

    @staticmethod
    def _build_edge_structures(
        faces: np.ndarray,
    ) -> Tuple[Dict[Tuple[int, int], List[int]], Dict[Tuple[int, int], List[int]]]:
        """Build undirected and directed edge maps."""
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        directed_edges: Dict[Tuple[int, int], List[int]] = {}

        for fi, f in enumerate(faces):
            for i in range(3):
                u, v = int(f[i]), int(f[(i + 1) % 3])
                undirected = (min(u, v), max(u, v))
                edge_to_faces.setdefault(undirected, []).append(fi)
                directed_edges.setdefault((u, v), []).append(fi)

        return edge_to_faces, directed_edges

    @staticmethod
    def _analyze_normal_consistency(
        faces: np.ndarray,
        edge_to_faces: Dict[Tuple[int, int], List[int]],
        directed_edges: Dict[Tuple[int, int], List[int]],
    ) -> Tuple[List[List[int]], int, List[Tuple[int, int]]]:
        """Analyze connected components and normal orientation zones across shared manifold edges."""
        num_faces = len(faces)
        visited = set()
        components: List[List[int]] = []
        inconsistent_edges: List[Tuple[int, int]] = []

        # Build face adjacency graph across manifold edges
        face_neighbors: Dict[int, List[int]] = {i: [] for i in range(num_faces)}
        for edge, f_list in edge_to_faces.items():
            if len(f_list) == 2:
                f0, f1 = f_list[0], f_list[1]
                face_neighbors[f0].append(f1)
                face_neighbors[f1].append(f0)
            elif len(f_list) > 2:
                # Non-manifold edge
                for f0 in f_list:
                    for f1 in f_list:
                        if f0 != f1:
                            face_neighbors[f0].append(f1)

        # Check half-edge traversal consistency
        for (u, v), f_list in directed_edges.items():
            # If both faces traverse u -> v in the same direction, that edge is inconsistent!
            if len(f_list) > 1:
                inconsistent_edges.append((u, v))

        # BFS for connected components
        for start_face in range(num_faces):
            if start_face not in visited:
                comp: List[int] = []
                queue = deque([start_face])
                visited.add(start_face)
                while queue:
                    curr = queue.popleft()
                    comp.append(curr)
                    for nbr in face_neighbors[curr]:
                        if nbr not in visited:
                            visited.add(nbr)
                            queue.append(nbr)
                components.append(comp)

        # Orientation zones: connected parts + any inconsistent edge partitions
        normal_zones = len(components) + (1 if len(inconsistent_edges) > 0 else 0)

        return components, normal_zones, inconsistent_edges

    def _detect_self_intersections(self, mesh: SurfaceMesh) -> int:
        """Detect self-intersecting triangle pairs using AABB pruning and Moeller intersection test."""
        return self._detect_self_intersections_aabb(mesh.vertices, mesh.faces)

    @staticmethod
    def _detect_self_intersections_aabb(vertices: np.ndarray, faces: np.ndarray) -> int:
        """Bounding box filter for self-intersection candidates."""
        n_faces = len(faces)
        if n_faces < 2:
            return 0

        # Compute AABB for each triangle
        tri_pts = vertices[faces]  # (F, 3, 3)
        min_boxes = np.min(tri_pts, axis=1)  # (F, 3)
        max_boxes = np.max(tri_pts, axis=1)  # (F, 3)

        overlap_count = 0
        for i in range(n_faces):
            f_i = faces[i]
            # Find candidate overlapping boxes for j > i
            # Boxes overlap if min_a <= max_b and max_a >= min_b for all 3 axes
            overlap_mask = np.all(min_boxes[i] <= max_boxes[i + 1:], axis=1) & np.all(
                max_boxes[i] >= min_boxes[i + 1:], axis=1
            )
            candidate_indices = np.where(overlap_mask)[0] + (i + 1)

            for j in candidate_indices:
                f_j = faces[j]
                # If they share vertices, they are topologically adjacent; skip
                if len(set(f_i).intersection(f_j)) > 0:
                    continue
                # Detailed triangle-triangle intersection test (Moeller algorithm)
                if SurfaceAuditor._triangles_intersect_moeller(tri_pts[i], tri_pts[j]):
                    overlap_count += 1

        return overlap_count

    @staticmethod
    def _triangles_intersect_moeller(t1: np.ndarray, t2: np.ndarray, eps: float = 1e-9) -> bool:
        """Moeller's triangle-triangle intersection test in 3D Euclidean space."""
        # Plane equation of triangle 2: N2 . X + d2 = 0
        n2 = np.cross(t2[1] - t2[0], t2[2] - t2[0])
        n2_len = np.linalg.norm(n2)
        if n2_len < eps:
            return False
        n2 /= n2_len
        d2 = -np.dot(n2, t2[0])

        # Distances of t1 vertices to plane 2
        dist1 = np.dot(t1, n2) + d2
        dist1[np.abs(dist1) < eps] = 0.0

        if (dist1[0] > 0 and dist1[1] > 0 and dist1[2] > 0) or (
            dist1[0] < 0 and dist1[1] < 0 and dist1[2] < 0
        ):
            return False  # All vertices on same side of plane 2
        if np.all(dist1 == 0.0):
            return False  # Co-planar

        # Plane equation of triangle 1: N1 . X + d1 = 0
        n1 = np.cross(t1[1] - t1[0], t1[2] - t1[0])
        n1_len = np.linalg.norm(n1)
        if n1_len < eps:
            return False
        n1 /= n1_len
        d1 = -np.dot(n1, t1[0])

        # Distances of t2 vertices to plane 1
        dist2 = np.dot(t2, n1) + d1
        dist2[np.abs(dist2) < eps] = 0.0

        if (dist2[0] > 0 and dist2[1] > 0 and dist2[2] > 0) or (
            dist2[0] < 0 and dist2[1] < 0 and dist2[2] < 0
        ):
            return False  # All vertices on same side of plane 1
        if np.all(dist2 == 0.0):
            return False  # Co-planar

        # Direction of intersection line
        d_line = np.cross(n1, n2)
        d_len = np.linalg.norm(d_line)
        if d_len < eps:
            return False
        d_line /= d_len

        def get_interval(tri: np.ndarray, dists: np.ndarray) -> Tuple[float, float]:
            zero_idx = np.where(np.abs(dists) < eps)[0]
            if len(zero_idx) == 2:
                # Both vertices lie on the intersection line
                t_a = float(np.dot(tri[zero_idx[0]], d_line))
                t_b = float(np.dot(tri[zero_idx[1]], d_line))
                return min(t_a, t_b), max(t_a, t_b)
            elif len(zero_idx) == 1:
                # One vertex lies on the intersection line
                z = zero_idx[0]
                o1, o2 = (z + 1) % 3, (z + 2) % 3
                t_a = float(np.dot(tri[z], d_line))
                if dists[o1] * dists[o2] < 0:
                    denom = dists[o2] - dists[o1]
                    denom = 1e-15 if abs(denom) < 1e-15 else denom
                    p_b = tri[o1] + (tri[o2] - tri[o1]) * (-dists[o1] / denom)
                    t_b = float(np.dot(p_b, d_line))
                else:
                    t_b = t_a
                return min(t_a, t_b), max(t_a, t_b)
            else:
                # Standard case: one vertex on one side, two on the other
                if dists[0] * dists[1] > 0:
                    i0, i1, i2 = 2, 0, 1
                elif dists[0] * dists[2] > 0:
                    i0, i1, i2 = 1, 0, 2
                else:
                    i0, i1, i2 = 0, 1, 2
                denom1 = dists[i1] - dists[i0]
                denom2 = dists[i2] - dists[i0]
                denom1 = 1e-15 if abs(denom1) < 1e-15 else denom1
                denom2 = 1e-15 if abs(denom2) < 1e-15 else denom2
                p_a = tri[i0] + (tri[i1] - tri[i0]) * (-dists[i0] / denom1)
                p_b = tri[i0] + (tri[i2] - tri[i0]) * (-dists[i0] / denom2)
                t_a = float(np.dot(p_a, d_line))
                t_b = float(np.dot(p_b, d_line))
                return min(t_a, t_b), max(t_a, t_b)

        min1, max1 = get_interval(t1, dist1)
        min2, max2 = get_interval(t2, dist2)

        return max(min1, min2) < min(max1, max2) - eps
