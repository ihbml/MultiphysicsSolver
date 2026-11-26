"""Coupled 2D Finite-Area Surface Mesh (faMesh) with Hinge-Unfolded Geodesics.

Implements Blueprint §1.1.7:
- FiniteAreaMesh extraction from UnstructuredMesh boundary patches or SurfaceMesh.
- Exact 2D hinge triangle/polygon unfolding across shared surface edges, yielding
  geodesic centroid-to-centroid distances d_geo, tangent-plane edge vectors, and
  parallel-transport operators between adjacent face tangent planes.
- Discrete surface divergence operator check_surface_divergence(field).
"""

from __future__ import annotations

from dataclasses import dataclass, field
import heapq
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.meshing.models import UnstructuredMesh


def _face_area_centre_normal(pts: np.ndarray, nodes: np.ndarray) -> Tuple[float, np.ndarray, np.ndarray]:
    """Compute (scalar_area, centroid, unit_normal) for a 3D polygonal surface face."""
    f_pts = pts[nodes]
    if len(f_pts) < 3:
        return 0.0, np.mean(f_pts, axis=0), np.array([0.0, 0.0, 1.0], dtype=np.float64)
    p0 = f_pts[0]
    rel = f_pts - p0
    c_rel = rel.mean(axis=0)
    d = rel - c_rel
    cross_p = 0.5 * np.cross(d, np.roll(d, -1, axis=0))
    tri_areas = np.linalg.norm(cross_p, axis=1)
    tot_area = float(tri_areas.sum())
    sf = cross_p.sum(axis=0)
    sf_norm = float(np.linalg.norm(sf))
    if tot_area > 1e-30:
        tri_c = p0 + (c_rel + rel + np.roll(rel, -1, axis=0)) / 3.0
        centre = (tri_c * tri_areas[:, None]).sum(axis=0) / tot_area
    else:
        centre = p0 + c_rel
    unit_n = (sf / sf_norm) if sf_norm > 1e-30 else np.array([0.0, 0.0, 1.0], dtype=np.float64)
    return float(sf_norm), centre, unit_n


@dataclass
class FiniteAreaMesh:
    """Finite-Area surface mesh (faMesh) with exact 2D hinge-unfolded geodesics."""

    patch_name: str
    points: np.ndarray                 # (N_v, 3) float64
    faces: List[np.ndarray]            # N_f face vertex loops (int64)
    face_areas: np.ndarray             # (N_f,) float64 scalar face areas
    face_centres: np.ndarray           # (N_f, 3) float64 face centroids
    edge_owners: np.ndarray            # (N_e,) int64 owner face ID
    edge_neighbours: np.ndarray        # (N_e,) int64 neighbour face ID (-1 on patch boundary)
    edge_vectors: np.ndarray           # (N_e, 3) float64 outward edge-length normal in owner plane
    edge_lengths: np.ndarray           # (N_e,) float64 geometric edge lengths
    geodesic_edge_lengths: np.ndarray  # (N_e,) float64 unfolded centroid-to-centroid geodesic distances
    unfolded_edge_vectors: np.ndarray  # (N_e, 3) float64 unfolded P->N vector in owner tangent plane
    face_normals: np.ndarray = field(default_factory=lambda: np.empty((0, 3), dtype=np.float64))
    edge_vectors_neighbour: np.ndarray = field(default_factory=lambda: np.empty((0, 3), dtype=np.float64))
    hinge_rotations: np.ndarray = field(default_factory=lambda: np.empty((0, 3, 3), dtype=np.float64))

    @classmethod
    def from_points_faces(
        cls,
        points: np.ndarray,
        faces: Sequence[Sequence[int]],
        patch_name: str = "wall",
    ) -> "FiniteAreaMesh":
        """Construct a FiniteAreaMesh from compacted patch vertices and face vertex loops."""
        pts = np.asarray(points, dtype=np.float64)
        face_list = [np.asarray(f, dtype=np.int64).ravel() for f in faces]
        n_faces = len(face_list)

        face_areas = np.zeros(n_faces, dtype=np.float64)
        face_centres = np.zeros((n_faces, 3), dtype=np.float64)
        face_normals = np.zeros((n_faces, 3), dtype=np.float64)

        edge_map: Dict[Tuple[int, int], List[Tuple[int, int, int]]] = {}
        for f_idx, fn in enumerate(face_list):
            area, centre, normal = _face_area_centre_normal(pts, fn)
            face_areas[f_idx] = area
            face_centres[f_idx] = centre
            face_normals[f_idx] = normal
            m = len(fn)
            for k in range(m):
                u, v = int(fn[k]), int(fn[(k + 1) % m])
                ekey = (u, v) if u < v else (v, u)
                edge_map.setdefault(ekey, []).append((f_idx, u, v))

        int_records: List[Tuple[int, int, int, int]] = []
        bnd_records: List[Tuple[int, int, int, int]] = []
        for ekey, entries in edge_map.items():
            if len(entries) == 2:
                (fa, ua, va), (fb, ub, vb) = entries[0], entries[1]
                if fa < fb:
                    int_records.append((fa, fb, ua, va))
                else:
                    int_records.append((fb, fa, ub, vb))
            else:
                for fa, ua, va in entries:
                    bnd_records.append((fa, -1, ua, va))

        int_records.sort(key=lambda r: (r[0], r[1]))
        all_records = int_records + bnd_records
        n_edges = len(all_records)

        edge_owners = np.zeros(n_edges, dtype=np.int64)
        edge_neighbours = np.full(n_edges, -1, dtype=np.int64)
        edge_lengths = np.zeros(n_edges, dtype=np.float64)
        edge_vectors = np.zeros((n_edges, 3), dtype=np.float64)
        edge_vectors_nbr = np.zeros((n_edges, 3), dtype=np.float64)
        geodesic_lengths = np.zeros(n_edges, dtype=np.float64)
        unfolded_vecs = np.zeros((n_edges, 3), dtype=np.float64)
        hinge_rots = np.repeat(np.eye(3, dtype=np.float64)[None, :, :], n_edges, axis=0)

        for e_idx, (p_id, n_id, ua, ub) in enumerate(all_records):
            edge_owners[e_idx] = p_id
            edge_neighbours[e_idx] = n_id
            pa = pts[ua]
            pb = pts[ub]
            e_vec = pb - pa
            e_len = float(np.linalg.norm(e_vec))
            edge_lengths[e_idx] = e_len
            if e_len <= 1e-15:
                continue

            t_e = e_vec / e_len
            x_e = 0.5 * (pa + pb)
            c_p = face_centres[p_id]
            n_p = face_normals[p_id]

            m_p = np.cross(t_e, n_p)
            mp_norm = float(np.linalg.norm(m_p))
            m_p = m_p / mp_norm if mp_norm > 1e-15 else np.zeros(3, dtype=np.float64)
            if float(np.dot(m_p, x_e - c_p)) < 0.0:
                m_p = -m_p
            edge_vectors[e_idx] = e_len * m_p

            if n_id >= 0:
                c_n = face_centres[n_id]
                n_n = face_normals[n_id]
                m_n = np.cross(t_e, n_n)
                mn_norm = float(np.linalg.norm(m_n))
                m_n = m_n / mn_norm if mn_norm > 1e-15 else m_p.copy()
                if float(np.dot(m_n, c_n - x_e)) < 0.0:
                    m_n = -m_n
                # Outward normal from N at edge e points opposite to inward m_n
                edge_vectors_nbr[e_idx] = -e_len * m_n

                # Exact 2D hinge unfolding across edge (pa, pb) into P's tangent plane
                s_p = float(np.dot(c_p - pa, t_e))
                eta_p = float(np.dot(c_p - pa, m_p))
                s_n = float(np.dot(c_n - pa, t_e))
                eta_n = float(np.dot(c_n - pa, m_n))

                d_unfolded = (s_n - s_p) * t_e + (eta_n - eta_p) * m_p
                d_geo = float(np.linalg.norm(d_unfolded))
                geodesic_lengths[e_idx] = d_geo
                unfolded_vecs[e_idx] = d_unfolded

                # Hinge rotation R_{P->N} preserving orientation along hinge tangent t_e
                n_p_frame = np.cross(t_e, m_p)
                n_n_frame = np.cross(t_e, m_n)
                basis_p = np.column_stack([t_e, m_p, n_p_frame])
                basis_n = np.column_stack([t_e, m_n, n_n_frame])
                hinge_rots[e_idx] = basis_n @ basis_p.T
            else:
                d_bnd = (x_e - c_p) - float(np.dot(x_e - c_p, n_p)) * n_p
                geodesic_lengths[e_idx] = float(np.linalg.norm(d_bnd))
                unfolded_vecs[e_idx] = d_bnd

        return cls(
            patch_name=patch_name,
            points=pts,
            faces=face_list,
            face_areas=face_areas,
            face_centres=face_centres,
            edge_owners=edge_owners,
            edge_neighbours=edge_neighbours,
            edge_vectors=edge_vectors,
            edge_lengths=edge_lengths,
            geodesic_edge_lengths=geodesic_lengths,
            unfolded_edge_vectors=unfolded_vecs,
            face_normals=face_normals,
            edge_vectors_neighbour=edge_vectors_nbr,
            hinge_rotations=hinge_rots,
        )

    @classmethod
    def from_unstructured_mesh(
        cls, mesh: UnstructuredMesh, patch_name: str = "wall"
    ) -> "FiniteAreaMesh":
        """Extract a FiniteAreaMesh from a named boundary patch of an UnstructuredMesh."""
        if patch_name not in mesh.boundaries:
            raise ValueError(
                f"Patch '{patch_name}' not found in mesh.boundaries: {sorted(mesh.boundaries.keys())}"
            )
        start, count = mesh.boundaries[patch_name]
        if count <= 0:
            raise ValueError(f"Patch '{patch_name}' has zero faces.")

        raw_faces: List[np.ndarray] = []
        if mesh.face_offsets is not None and mesh.face_nodes is not None:
            fo = mesh.face_offsets
            fn = mesh.face_nodes
            for fi in range(start, start + count):
                raw_faces.append(np.asarray(fn[int(fo[fi]) : int(fo[fi + 1])], dtype=np.int64))
        elif mesh.faces is not None:
            for fi in range(start, start + count):
                raw_faces.append(np.asarray(mesh.faces[fi], dtype=np.int64))
        else:
            raise ValueError("UnstructuredMesh lacks face connectivity.")

        used_verts = np.unique(np.concatenate(raw_faces))
        remap = {int(v): i for i, v in enumerate(used_verts)}
        local_faces = [np.array([remap[int(v)] for v in f], dtype=np.int64) for f in raw_faces]
        return cls.from_points_faces(mesh.points[used_verts], local_faces, patch_name=patch_name)

    @classmethod
    def from_surface_mesh(
        cls, surface: SurfaceMesh, patch_name: str = "wall"
    ) -> "FiniteAreaMesh":
        """Construct a FiniteAreaMesh from a triangulated SurfaceMesh."""
        if surface.patches and patch_name in surface.patches:
            f_idx = np.asarray(surface.patches[patch_name], dtype=np.int64)
            sel_faces = surface.faces[f_idx]
        else:
            sel_faces = surface.faces
        if len(sel_faces) == 0:
            raise ValueError(f"SurfaceMesh has zero faces for patch '{patch_name}'.")
        used_verts, inv = np.unique(sel_faces, return_inverse=True)
        local_faces = inv.reshape(-1, sel_faces.shape[1]).astype(np.int64)
        return cls.from_points_faces(
            surface.vertices[used_verts], [f for f in local_faces], patch_name=patch_name
        )

    def check_surface_divergence(self, field: Optional[np.ndarray] = None) -> np.ndarray:
        """Compute discrete surface divergence (or geometric edge-sum closure if field is None).

        For a per-face vector field (N_f, 3), projects onto each face tangent plane,
        parallel-transports neighbour values across the hinge rotation R_{P->N}, and
        evaluates (1 / A_P) sum_{e in dP} (u_e . L_{P,e}).
        """
        n_faces = len(self.faces)
        n_edges = len(self.edge_owners)
        div = np.zeros(n_faces, dtype=np.float64)

        if field is None:
            vec_sum = np.zeros((n_faces, 3), dtype=np.float64)
            for e_idx in range(n_edges):
                p_id = int(self.edge_owners[e_idx])
                n_id = int(self.edge_neighbours[e_idx])
                vec_sum[p_id] += self.edge_vectors[e_idx]
                if n_id >= 0:
                    vec_sum[n_id] += self.edge_vectors_neighbour[e_idx]
            norms = np.linalg.norm(vec_sum, axis=1)
            safe_a = np.where(self.face_areas > 1e-30, self.face_areas, 1.0)
            return norms / safe_a

        arr = np.asarray(field, dtype=np.float64)
        if arr.ndim == 1 and len(arr) == n_faces:
            vec_sum = np.zeros((n_faces, 3), dtype=np.float64)
            for e_idx in range(n_edges):
                p_id = int(self.edge_owners[e_idx])
                n_id = int(self.edge_neighbours[e_idx])
                val_e = 0.5 * (arr[p_id] + arr[n_id]) if n_id >= 0 else arr[p_id]
                vec_sum[p_id] += val_e * self.edge_vectors[e_idx]
                if n_id >= 0:
                    vec_sum[n_id] += val_e * self.edge_vectors_neighbour[e_idx]
            safe_a = np.where(self.face_areas > 1e-30, self.face_areas, 1.0)
            return np.linalg.norm(vec_sum, axis=1) / safe_a

        if arr.ndim == 2 and arr.shape == (n_faces, 3):
            # Project onto each face tangent plane
            n_dot = np.sum(arr * self.face_normals, axis=1, keepdims=True)
            u_tan = arr - n_dot * self.face_normals
            flux_sum = np.zeros(n_faces, dtype=np.float64)
            for e_idx in range(n_edges):
                p_id = int(self.edge_owners[e_idx])
                n_id = int(self.edge_neighbours[e_idx])
                if n_id >= 0:
                    u_n_in_p = self.hinge_rotations[e_idx].T @ u_tan[n_id]
                    u_e_p = 0.5 * (u_tan[p_id] + u_n_in_p)
                    flux = float(np.dot(u_e_p, self.edge_vectors[e_idx]))
                    flux_sum[p_id] += flux
                    flux_sum[n_id] -= flux
                else:
                    flux = float(np.dot(u_tan[p_id], self.edge_vectors[e_idx]))
                    flux_sum[p_id] += flux
            safe_a = np.where(self.face_areas > 1e-30, self.face_areas, 1.0)
            div = flux_sum / safe_a
            return div

        raise ValueError(f"Unsupported field shape {arr.shape} for {n_faces} surface faces.")

    def geodesic_distance(self, src_face: int, dst_face: int) -> float:
        """Shortest surface path across unfolded centroid-to-centroid edge lengths."""
        n_faces = len(self.faces)
        if not (0 <= src_face < n_faces) or not (0 <= dst_face < n_faces):
            raise IndexError(f"Face index out of bounds [0, {n_faces})")
        if src_face == dst_face:
            return 0.0
        adj: List[List[Tuple[int, float]]] = [[] for _ in range(n_faces)]
        for e_idx in range(len(self.edge_owners)):
            p_id = int(self.edge_owners[e_idx])
            n_id = int(self.edge_neighbours[e_idx])
            if n_id >= 0:
                w = float(self.geodesic_edge_lengths[e_idx])
                adj[p_id].append((n_id, w))
                adj[n_id].append((p_id, w))

        dists = np.full(n_faces, np.inf, dtype=np.float64)
        dists[src_face] = 0.0
        pq: List[Tuple[float, int]] = [(0.0, src_face)]
        while pq:
            d, cur = heapq.heappop(pq)
            if cur == dst_face:
                return float(d)
            if d > dists[cur]:
                continue
            for nbr, w in adj[cur]:
                nd = d + w
                if nd < dists[nbr]:
                    dists[nbr] = nd
                    heapq.heappush(pq, (nd, nbr))
        return float(dists[dst_face])
