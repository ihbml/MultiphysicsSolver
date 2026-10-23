"""OpenFOAM surfaceCheck geometric quality metrics for triangulated surface meshes."""

from __future__ import annotations

from typing import Any, Dict, List, Set, Tuple
import numpy as np

from backend.geometry.models import SurfaceMesh

try:
    from scipy.spatial import cKDTree
    HAS_CKDTREE = True
except ImportError:
    HAS_CKDTREE = False


class QualityMetrics:
    """Evaluates geometric quality metrics matching OpenFOAM surfaceCheck utility."""

    @staticmethod
    def compute_edge_lengths(mesh: SurfaceMesh) -> Tuple[np.ndarray, float, float, float]:
        """Compute Euclidean lengths of all unique mesh edges.

        Returns:
            Tuple of (all_lengths_array, min_length, max_length, avg_length).
        """
        if len(mesh.faces) == 0:
            return np.empty(0, dtype=np.float64), 0.0, 0.0, 0.0

        # Extract unique edges
        unique_edges: Set[Tuple[int, int]] = set()
        for f in mesh.faces:
            for i in range(3):
                u, v = int(f[i]), int(f[(i + 1) % 3])
                unique_edges.add((min(u, v), max(u, v)))

        edge_array = np.array(list(unique_edges), dtype=np.int32)
        v0 = mesh.vertices[edge_array[:, 0]]
        v1 = mesh.vertices[edge_array[:, 1]]
        lengths = np.linalg.norm(v1 - v0, axis=1)

        min_len = float(np.min(lengths)) if len(lengths) > 0 else 0.0
        max_len = float(np.max(lengths)) if len(lengths) > 0 else 0.0
        avg_len = float(np.mean(lengths)) if len(lengths) > 0 else 0.0

        return lengths, min_len, max_len, avg_len

    @staticmethod
    def find_illegal_faces(mesh: SurfaceMesh, area_tol: float = 1e-14) -> List[int]:
        """Detect illegal triangles: duplicate vertex indices or near-zero area.

        Matches OpenFOAM surfaceCheck illegalFaces rule.

        Args:
            mesh: Input SurfaceMesh.
            area_tol: Minimum allowable face area in square meters.

        Returns:
            List of illegal face indices.
        """
        illegal: List[int] = []
        if len(mesh.faces) == 0:
            return illegal

        # 1. Topological check: duplicate vertex index in face
        f = mesh.faces
        dup_mask = (f[:, 0] == f[:, 1]) | (f[:, 0] == f[:, 2]) | (f[:, 1] == f[:, 2])
        illegal.extend(np.where(dup_mask)[0].tolist())

        # 2. Geometric check: triangle area < area_tol
        v0 = mesh.vertices[f[:, 0]]
        v1 = mesh.vertices[f[:, 1]]
        v2 = mesh.vertices[f[:, 2]]
        cross = np.cross(v1 - v0, v2 - v0)
        areas = 0.5 * np.linalg.norm(cross, axis=1)

        zero_area_mask = (areas < area_tol) & (~dup_mask)
        illegal.extend(np.where(zero_area_mask)[0].tolist())

        return sorted(list(set(illegal)))

    @staticmethod
    def compute_equilateral_quality(
        mesh: SurfaceMesh, min_q_tol: float = 1e-5
    ) -> Tuple[float, float, List[int], List[int]]:
        """Compute OpenFOAM equilateral triangle quality Q = 4*sqrt(3)*Area / (a^2 + b^2 + c^2).

        Q is in [0, 1] where 1.0 is a perfect equilateral triangle.
        Values < min_q_tol (default 1e-5) are flagged as bad faces in OpenFOAM.

        Returns:
            Tuple of (quality_min, quality_max, histogram_20_bins, bad_faces_indices).
        """
        if len(mesh.faces) == 0:
            return 0.0, 0.0, [0] * 20, []

        v0 = mesh.vertices[mesh.faces[:, 0]]
        v1 = mesh.vertices[mesh.faces[:, 1]]
        v2 = mesh.vertices[mesh.faces[:, 2]]

        # Edge vectors
        e0 = v1 - v0
        e1 = v2 - v1
        e2 = v0 - v2

        # Squared edge lengths
        a2 = np.sum(e0 * e0, axis=1)
        b2 = np.sum(e1 * e1, axis=1)
        c2 = np.sum(e2 * e2, axis=1)

        # Cross product and Area
        cross = np.cross(e0, -e2)
        areas = 0.5 * np.linalg.norm(cross, axis=1)

        denominator = a2 + b2 + c2
        # Avoid division by zero
        safe_denom = np.where(denominator < 1e-18, 1e-18, denominator)

        # Q = 4 * sqrt(3) * Area / (a^2 + b^2 + c^2)
        q = (4.0 * np.sqrt(3.0) * areas) / safe_denom
        q = np.clip(q, 0.0, 1.0)

        q_min = float(np.min(q))
        q_max = float(np.max(q))

        # Flag bad faces (Q < min_q_tol)
        bad_faces = np.where(q < min_q_tol)[0].tolist()

        # 20 histogram bins between 0 and 1
        # Bins: [0, 0.05), [0.05, 0.10), ..., [0.95, 1.0]
        bin_counts, _ = np.histogram(q, bins=20, range=(0.0, 1.0))
        histogram = [int(c) for c in bin_counts]

        return q_min, q_max, histogram, bad_faces

    @staticmethod
    def find_close_unconnected_points(
        mesh: SurfaceMesh, tol_rel: float = 1e-6
    ) -> List[Tuple[int, int]]:
        """Identify point pairs closer than tol_rel * diag(BBox) not connected by a mesh edge.

        Matches OpenFOAM surfaceCheck closePoints check.

        Returns:
            List of (vertex_u, vertex_v) pairs.
        """
        if len(mesh.vertices) < 2:
            return []

        bbox = mesh.get_bounding_box()
        dist_threshold = max(tol_rel * bbox.diagonal, 1e-12)

        # Collect existing mesh edges
        mesh_edges: Set[Tuple[int, int]] = set()
        for f in mesh.faces:
            for i in range(3):
                u, v = int(f[i]), int(f[(i + 1) % 3])
                mesh_edges.add((min(u, v), max(u, v)))

        close_pairs: List[Tuple[int, int]] = []

        if HAS_CKDTREE:
            tree = cKDTree(mesh.vertices)
            # Find all pairs within distance
            pairs = tree.query_pairs(r=dist_threshold)
            for u, v in pairs:
                edge = (min(u, v), max(u, v))
                if edge not in mesh_edges:
                    close_pairs.append(edge)
        else:
            # Fallback for moderate vertex counts
            n = len(mesh.vertices)
            if n <= 1000:
                diff = mesh.vertices[:, np.newaxis, :] - mesh.vertices[np.newaxis, :, :]
                dists = np.linalg.norm(diff, axis=-1)
                iu = np.triu_indices(n, k=1)
                close_mask = dists[iu] < dist_threshold
                idx_u = iu[0][close_mask]
                idx_v = iu[1][close_mask]
                for u, v in zip(idx_u, idx_v):
                    edge = (min(int(u), int(v)), max(int(u), int(v)))
                    if edge not in mesh_edges:
                        close_pairs.append(edge)

        return close_pairs
