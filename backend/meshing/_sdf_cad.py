"""CADMeshSDF surface SDF evaluator and CADFeatureFilter geometric sanitizer."""

from __future__ import annotations

import ctypes
from typing import Dict, List, Optional, Tuple
import numpy as np
from scipy.spatial import cKDTree

from backend.geometry.models import BoundingBox, SurfaceMesh
from backend.meshing._sdf_bvh import (
    TriangleAABBTree,
    _get_sdf_dll,
    compute_angle_weighted_pseudonormals,
    compute_mesh_winding_number,
    voromesh_bisector_loss,
)
from backend.meshing._sdf_primitives import BaseSDF


class CADMeshSDF(BaseSDF):
    """Continuous Signed Distance Field (SDF) and outward normal gradient evaluator
    derived from a triangulated SurfaceMesh using spatial AABB tree and pseudonormals.
    """

    def __init__(
        self,
        mesh: SurfaceMesh,
        k_candidates: Optional[int] = None,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
        sign_method: Optional[str] = None,
        use_native: bool = True,
    ) -> None:
        if len(mesh.faces) == 0:
            raise ValueError("Cannot initialize CADMeshSDF from empty surface mesh.")

        raw_verts = mesh.vertices.astype(np.float64)
        raw_faces = mesh.faces.astype(np.int32)

        if auto_compact:
            used_indices, inverse = np.unique(raw_faces, return_inverse=True)
            self.vertices = np.ascontiguousarray(raw_verts[used_indices], dtype=np.float64)
            self.faces = np.ascontiguousarray(inverse.reshape(raw_faces.shape), dtype=np.int32)
        else:
            self.vertices = np.ascontiguousarray(raw_verts, dtype=np.float64)
            self.faces = np.ascontiguousarray(raw_faces, dtype=np.int32)

        v0_init = self.vertices[self.faces[:, 0]]
        v1_init = self.vertices[self.faces[:, 1]]
        v2_init = self.vertices[self.faces[:, 2]]
        signed_vol = float(np.sum(np.sum(v0_init * np.cross(v1_init, v2_init), axis=-1)) / 6.0)
        if signed_vol < -1e-8:
            self.faces = np.ascontiguousarray(self.faces[:, [0, 2, 1]], dtype=np.int32)

        self._legacy_k = k_candidates
        self.k_candidates = min(max(1, k_candidates if k_candidates is not None else 8), len(self.faces))
        self.inside_is_negative = inside_is_negative
        if sign_method is not None:
            self.sign_method = sign_method
        elif k_candidates is not None:
            self.sign_method = "legacy"
        else:
            self.sign_method = "pseudonormal"
        self.use_native = use_native
        self._prev_facet_hint = -1
        self._native_engine: Optional[ctypes.c_void_p] = None

        self.tri_v0 = self.vertices[self.faces[:, 0]]
        self.tri_v1 = self.vertices[self.faces[:, 1]]
        self.tri_v2 = self.vertices[self.faces[:, 2]]

        cross = np.cross(self.tri_v1 - self.tri_v0, self.tri_v2 - self.tri_v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        lengths[lengths < 1e-20] = 1.0
        self.normals = cross / lengths
        self.centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0
        self.tree = cKDTree(self.centroids)

        (
            self.face_normals,
            self.vertex_pseudonormals,
            self.edge_pseudonormals,
            self.face_edge_map,
        ) = compute_angle_weighted_pseudonormals(self.vertices, self.faces)
        self.aabb_tree = TriangleAABBTree(self.vertices, self.faces)

        if self.use_native:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_create"):
                try:
                    c_verts = self.vertices.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    c_faces = self.faces.ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
                    self._native_engine = dll.cfd_sdf_create(
                        c_verts, ctypes.c_int64(len(self.vertices)),
                        c_faces, ctypes.c_int64(len(self.faces))
                    )
                except Exception:
                    self._native_engine = None

    def __del__(self) -> None:
        if getattr(self, "_native_engine", None):
            try:
                dll = _get_sdf_dll()
                if dll is not None and hasattr(dll, "cfd_sdf_free"):
                    dll.cfd_sdf_free(self._native_engine)
            except Exception:
                pass
            self._native_engine = None

    @classmethod
    def from_surface_mesh(
        cls,
        mesh: SurfaceMesh,
        k_candidates: Optional[int] = None,
        inside_is_negative: bool = True,
        auto_compact: bool = True,
        sign_method: Optional[str] = None,
        use_native: bool = True,
    ) -> "CADMeshSDF":
        """Factory constructor creating CADMeshSDF from SurfaceMesh."""
        return cls(
            mesh,
            k_candidates=k_candidates,
            inside_is_negative=inside_is_negative,
            auto_compact=auto_compact,
            sign_method=sign_method,
            use_native=use_native,
        )

    def evaluate(self, points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluate continuous signed distance phi(x) and outward normal gradient grad_phi(x)."""
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts)
        n_pts = len(p)

        p_max = np.max(np.abs(p), axis=1)
        extreme_mask = p_max >= 1e30
        if np.any(extreme_mask):
            phi_arr = np.empty(n_pts, dtype=np.float64)
            grad_arr = np.empty((n_pts, 3), dtype=np.float64)

            norm_indices = np.where(~extreme_mask)[0]
            if len(norm_indices) > 0:
                p_save = self._evaluate_standard(p[norm_indices])
                phi_arr[norm_indices] = p_save[0]
                grad_arr[norm_indices] = p_save[1]

            ext_indices = np.where(extreme_mask)[0]
            for idx in ext_indices:
                p_vec = p[idx]
                m_coord = float(np.max(np.abs(p_vec)))
                if m_coord < 1e-20:
                    m_coord = 1.0
                scaled_p = p_vec / m_coord
                unit_norm = float(np.linalg.norm(scaled_p))
                if unit_norm < 1e-20:
                    unit_norm = 1.0
                unit_grad = scaled_p / unit_norm
                p_norm = m_coord * unit_norm
                if not np.isfinite(p_norm):
                    p_norm = 1.7e308
                phi_arr[idx] = p_norm if self.inside_is_negative else -p_norm
                grad_arr[idx] = unit_grad if self.inside_is_negative else -unit_grad

            if is_single:
                return float(phi_arr[0]), grad_arr[0]
            return phi_arr, grad_arr

        phi, grad = self._evaluate_standard(p)
        if is_single:
            return float(phi[0]), grad[0]
        return phi, grad

    def _evaluate_standard(self, p: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        n_pts = len(p)

        if self.sign_method == "legacy":
            return self._evaluate_legacy_kdtree(p)

        if self.sign_method == "winding_number":
            dists, qs, tids, _ = self.aabb_tree.query(p, prev_facet=self._prev_facet_hint)
            self._prev_facet_hint = int(tids[-1]) if n_pts > 0 else -1

            wn = self.winding_number(p)
            sign = np.where(wn >= 0.5, -1.0, 1.0)
            if not self.inside_is_negative:
                sign = -sign

            phi = sign * dists
            diff = p - qs
            diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
            diff_norm_safe = np.where(diff_norm < 1e-15, 1.0, diff_norm)
            unit_diff = diff / diff_norm_safe
            fallback_normals = self.face_normals[tids] if self.inside_is_negative else -self.face_normals[tids]
            grad = np.where(diff_norm > 1e-12, sign[:, None] * unit_diff, fallback_normals)
            g_len = np.linalg.norm(grad, axis=1, keepdims=True)
            grad = grad / np.where(g_len < 1e-15, 1.0, g_len)
            return phi, grad

        if self.use_native and self._native_engine:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_evaluate"):
                try:
                    p_contig = np.ascontiguousarray(p, dtype=np.float64)
                    c_points = p_contig.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    out_phi = np.ascontiguousarray(np.empty(n_pts, dtype=np.float64))
                    out_grad = np.ascontiguousarray(np.empty((n_pts, 3), dtype=np.float64))
                    res = dll.cfd_sdf_evaluate(
                        self._native_engine,
                        c_points,
                        ctypes.c_int64(n_pts),
                        out_phi.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                        out_grad.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    )
                    if res == 0:
                        if not self.inside_is_negative:
                            out_phi = -out_phi
                            out_grad = -out_grad
                        return out_phi, out_grad
                except Exception:
                    pass

        dists, qs, tids, barys = self.aabb_tree.query(p, prev_facet=self._prev_facet_hint)
        self._prev_facet_hint = int(tids[-1]) if n_pts > 0 else -1

        u, v, w = barys[:, 0], barys[:, 1], barys[:, 2]
        eps = 1e-5
        m_v0 = u > 1.0 - 2.0 * eps
        m_v1 = v > 1.0 - 2.0 * eps
        m_v2 = w > 1.0 - 2.0 * eps
        not_vert = ~(m_v0 | m_v1 | m_v2)
        m_e0 = not_vert & (w <= eps)
        m_e1 = not_vert & (u <= eps)
        m_e2 = not_vert & (v <= eps)
        m_face = ~(m_v0 | m_v1 | m_v2 | m_e0 | m_e1 | m_e2)

        n_pseudo = np.empty((n_pts, 3), dtype=np.float64)
        if np.any(m_v0):
            n_pseudo[m_v0] = self.vertex_pseudonormals[self.faces[tids[m_v0], 0]]
        if np.any(m_v1):
            n_pseudo[m_v1] = self.vertex_pseudonormals[self.faces[tids[m_v1], 1]]
        if np.any(m_v2):
            n_pseudo[m_v2] = self.vertex_pseudonormals[self.faces[tids[m_v2], 2]]
        if np.any(m_e0):
            n_pseudo[m_e0] = self.edge_pseudonormals[self.face_edge_map[tids[m_e0], 0]]
        if np.any(m_e1):
            n_pseudo[m_e1] = self.edge_pseudonormals[self.face_edge_map[tids[m_e1], 1]]
        if np.any(m_e2):
            n_pseudo[m_e2] = self.edge_pseudonormals[self.face_edge_map[tids[m_e2], 2]]
        if np.any(m_face):
            n_pseudo[m_face] = self.face_normals[tids[m_face]]

        diff = p - qs
        diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
        diff_norm_safe = np.where(diff_norm < 1e-15, 1.0, diff_norm)
        unit_diff = diff / diff_norm_safe

        dot_val = np.sum(diff * n_pseudo, axis=1)
        sign = np.where(dot_val >= 0.0, 1.0, -1.0)
        if not self.inside_is_negative:
            sign = -sign

        phi = sign * dists
        fallback_normal = n_pseudo if self.inside_is_negative else -n_pseudo
        grad = np.where(diff_norm > 1e-12, sign[:, None] * unit_diff, fallback_normal)
        g_len = np.linalg.norm(grad, axis=1, keepdims=True)
        grad = grad / np.where(g_len < 1e-15, 1.0, g_len)
        return phi, grad

    def _evaluate_legacy_kdtree(self, p: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Legacy M1 centroid KDTree candidate projection for backward compatibility."""
        n_pts = len(p)
        _, cand_idx = self.tree.query(p, k=self.k_candidates)
        if self.k_candidates == 1:
            cand_idx = cand_idx[:, None]

        num_faces = len(self.tri_v0)
        cand_idx = np.clip(cand_idx, 0, num_faces - 1)

        p_flat = np.repeat(p, self.k_candidates, axis=0)
        idx_flat = cand_idx.flatten()

        a_pts = self.tri_v0[idx_flat]
        b_pts = self.tri_v1[idx_flat]
        c_pts = self.tri_v2[idx_flat]

        u = b_pts - a_pts
        v = c_pts - a_pts
        w = p_flat - a_pts

        a_coeff = np.sum(u * u, axis=-1)
        b_coeff = np.sum(u * v, axis=-1)
        c_coeff = np.sum(v * v, axis=-1)
        d_coeff = np.sum(u * w, axis=-1)
        e_coeff = np.sum(v * w, axis=-1)

        det = a_coeff * c_coeff - b_coeff * b_coeff
        scale = a_coeff * c_coeff
        tol = np.maximum(1e-20, 1e-12 * scale)
        det_safe = np.where(np.abs(det) < tol, np.where(det >= 0.0, tol, -tol), det)

        s_param = (c_coeff * d_coeff - b_coeff * e_coeff) / det_safe
        t_param = (a_coeff * e_coeff - b_coeff * d_coeff) / det_safe

        inside_mask = (s_param >= 0.0) & (t_param >= 0.0) & (s_param + t_param <= 1.0)
        q_inside = a_pts + s_param[:, None] * u + t_param[:, None] * v

        s1 = np.clip(d_coeff / np.maximum(a_coeff, 1e-20), 0.0, 1.0)
        q1 = a_pts + s1[:, None] * u
        d1_sq = np.sum((p_flat - q1) ** 2, axis=-1)

        t2 = np.clip(e_coeff / np.maximum(c_coeff, 1e-20), 0.0, 1.0)
        q2 = a_pts + t2[:, None] * v
        d2_sq = np.sum((p_flat - q2) ** 2, axis=-1)

        w_b = p_flat - b_pts
        e_bc = c_pts - b_pts
        bc_len_sq = np.sum(e_bc * e_bc, axis=-1)
        t3 = np.clip(np.sum(w_b * e_bc, axis=-1) / np.maximum(bc_len_sq, 1e-20), 0.0, 1.0)
        q3 = b_pts + t3[:, None] * e_bc
        d3_sq = np.sum((p_flat - q3) ** 2, axis=-1)

        best_edge_sq = np.minimum(d1_sq, np.minimum(d2_sq, d3_sq))
        q_edge = np.where(
            d1_sq[:, None] == best_edge_sq[:, None],
            q1,
            np.where(d2_sq[:, None] == best_edge_sq[:, None], q2, q3),
        )

        q_cand = np.where(inside_mask[:, None], q_inside, q_edge)
        dist_cand_sq = np.sum((p_flat - q_cand) ** 2, axis=-1)

        dist_cand_sq = dist_cand_sq.reshape(n_pts, self.k_candidates)
        q_cand = q_cand.reshape(n_pts, self.k_candidates, 3)
        idx_cand = idx_flat.reshape(n_pts, self.k_candidates)

        min_k = np.argmin(dist_cand_sq, axis=1)
        rows = np.arange(n_pts)

        best_dist_sq = dist_cand_sq[rows, min_k]
        best_q = q_cand[rows, min_k]
        best_face_id = idx_cand[rows, min_k]
        unsigned_dist = np.sqrt(best_dist_sq)

        best_normal = self.normals[best_face_id]
        diff = p - best_q
        dot = np.sum(diff * best_normal, axis=1)

        sign = np.where(dot < 0.0, -1.0, 1.0)
        if not self.inside_is_negative:
            sign = -sign

        phi = sign * unsigned_dist

        diff_max = np.max(np.abs(diff), axis=1, keepdims=True)
        diff_max_safe = np.where(diff_max < 1e-20, 1.0, diff_max)
        scaled_diff = diff / diff_max_safe
        scaled_norm = np.linalg.norm(scaled_diff, axis=1, keepdims=True)
        diff_norm = diff_max * scaled_norm
        boundary_normal = best_normal if self.inside_is_negative else -best_normal
        unit_diff = scaled_diff / np.maximum(scaled_norm, 1e-20)
        grad_phi = np.where(diff_norm > 1e-12, sign[:, None] * unit_diff, boundary_normal)

        grad_len = np.linalg.norm(grad_phi, axis=1, keepdims=True)
        grad_len[grad_len < 1e-15] = 1.0
        grad_phi = grad_phi / grad_len
        return phi, grad_phi

    def distance(self, points: np.ndarray) -> np.ndarray:
        phi, _ = self.evaluate(points)
        return np.atleast_1d(phi)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        _, grad_phi = self.evaluate(points)
        return grad_phi

    def winding_number(self, points: np.ndarray) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        p = np.atleast_2d(pts)

        if self.use_native and self._native_engine:
            dll = _get_sdf_dll()
            if dll is not None and hasattr(dll, "cfd_sdf_winding_number"):
                try:
                    p_contig = np.ascontiguousarray(p, dtype=np.float64)
                    c_points = p_contig.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                    out_wn = np.ascontiguousarray(np.empty(len(p), dtype=np.float64))
                    res = dll.cfd_sdf_winding_number(
                        self._native_engine,
                        c_points,
                        ctypes.c_int64(len(p)),
                        out_wn.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    )
                    if res == 0:
                        return float(out_wn[0]) if is_single else out_wn
                except Exception:
                    pass

        return compute_mesh_winding_number(points, self.tri_v0, self.tri_v1, self.tri_v2)

    def compute_winding_numbers(self, points: np.ndarray) -> np.ndarray:
        return self.winding_number(points)

    def compute_voromesh_bisector_loss(
        self,
        seeds: np.ndarray,
        k: int = 16,
        seed_signs: Optional[np.ndarray] = None,
    ) -> Tuple[float, np.ndarray, np.ndarray]:
        return voromesh_bisector_loss(
            self.vertices, seeds, k=k, seed_signs=seed_signs, use_native=self.use_native
        )

    def project_to_boundary(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        phi, grad = self.evaluate(pts)
        return pts - phi * grad if is_single else pts - phi[:, None] * grad

    def project(
        self, points: np.ndarray, max_iter: int = 5, eps: float = 1e-6, tol: float = 1e-10
    ) -> np.ndarray:
        return self.project_to_boundary(points, max_iter=max_iter, eps=eps, tol=tol)

    def project_to_surface(self, points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        pts = np.asarray(points, dtype=np.float64)
        is_single = (pts.ndim == 1)
        phi, grad = self.evaluate(pts)
        proj = pts - phi * grad if is_single else pts - phi[:, None] * grad
        return proj, phi

    def get_bounding_box(self) -> BoundingBox:
        return BoundingBox.from_points(self.vertices)


class CADFeatureFilter:
    """Geometric filter pruning CAD wireframes, orphan vertices, and non-manifold sheets."""

    @staticmethod
    def filter_orphan_vertices(mesh: SurfaceMesh) -> SurfaceMesh:
        if len(mesh.faces) == 0:
            return mesh

        used_indices, inverse = np.unique(mesh.faces, return_inverse=True)
        if len(used_indices) == len(mesh.vertices):
            return mesh

        compact_vertices = mesh.vertices[used_indices]
        compact_faces = inverse.reshape(mesh.faces.shape).astype(np.int32)
        new_patches = {k: np.copy(v) for k, v in mesh.patches.items()}

        return SurfaceMesh(
            vertices=compact_vertices,
            faces=compact_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )

    @staticmethod
    def weld_coincident_vertices(mesh: SurfaceMesh, tol: float = 1e-7) -> SurfaceMesh:
        if len(mesh.vertices) <= 1:
            return mesh

        tree = cKDTree(mesh.vertices)
        pairs = tree.query_pairs(r=tol)
        if not pairs:
            return mesh

        num_verts = len(mesh.vertices)
        parent = np.arange(num_verts, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        for i, j in pairs:
            root_i, root_j = find(i), find(j)
            if root_i < root_j:
                parent[root_j] = root_i
            elif root_j < root_i:
                parent[root_i] = root_j

        roots = np.array([find(i) for i in range(num_verts)], dtype=np.int32)
        unique_roots, inv = np.unique(roots, return_inverse=True)

        return SurfaceMesh(
            vertices=mesh.vertices[unique_roots],
            faces=inv[mesh.faces].astype(np.int32),
            normals=None,
            patches={k: np.copy(v) for k, v in mesh.patches.items()},
            metadata=dict(mesh.metadata),
        )

    @staticmethod
    def filter_degenerate_faces(mesh: SurfaceMesh, min_area: float = 1e-14) -> SurfaceMesh:
        if len(mesh.faces) == 0:
            return mesh

        f = mesh.faces
        non_collapsed = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 2] != f[:, 0])
        v0, v1, v2 = mesh.vertices[f[:, 0]], mesh.vertices[f[:, 1]], mesh.vertices[f[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)

        valid_mask = non_collapsed & (areas >= min_area)
        if np.all(valid_mask):
            return mesh

        new_faces = mesh.faces[valid_mask]
        face_map = np.full(len(mesh.faces), -1, dtype=np.int32)
        face_map[valid_mask] = np.arange(np.count_nonzero(valid_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < len(mesh.faces))
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        filtered = SurfaceMesh(
            vertices=mesh.vertices,
            faces=new_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(filtered)

    @staticmethod
    def extract_dominant_manifold_shell(mesh: SurfaceMesh, weld_tol: float = 1e-7) -> SurfaceMesh:
        if len(mesh.faces) <= 1:
            return mesh

        if weld_tol > 0.0 and len(mesh.vertices) > 1:
            mesh = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)

        num_faces = len(mesh.faces)
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        for f_idx, face in enumerate(mesh.faces):
            for e in (
                (min(face[0], face[1]), max(face[0], face[1])),
                (min(face[1], face[2]), max(face[1], face[2])),
                (min(face[2], face[0]), max(face[2], face[0])),
            ):
                edge_to_faces.setdefault(e, []).append(f_idx)

        parent = np.arange(num_faces, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        def union(i: int, j: int) -> None:
            ri, rj = find(i), find(j)
            if ri != rj:
                parent[ri] = rj

        for f_list in edge_to_faces.values():
            if len(f_list) == 2:
                union(f_list[0], f_list[1])

        v0, v1, v2 = mesh.vertices[mesh.faces[:, 0]], mesh.vertices[mesh.faces[:, 1]], mesh.vertices[mesh.faces[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)

        comp_areas: Dict[int, float] = {}
        for f_idx in range(num_faces):
            r = find(f_idx)
            comp_areas[r] = comp_areas.get(r, 0.0) + areas[f_idx]

        dominant_root = max(comp_areas.keys(), key=lambda r: comp_areas[r])
        dominant_mask = np.array([find(f_idx) == dominant_root for f_idx in range(num_faces)], dtype=bool)
        if np.all(dominant_mask):
            return mesh

        face_map = np.full(num_faces, -1, dtype=np.int32)
        face_map[dominant_mask] = np.arange(np.count_nonzero(dominant_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < num_faces)
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        dom_mesh = SurfaceMesh(
            vertices=mesh.vertices,
            faces=mesh.faces[dominant_mask],
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(dom_mesh)

    @staticmethod
    def sanitize(mesh: SurfaceMesh, min_area: float = 1e-14, weld_tol: float = 1e-7) -> SurfaceMesh:
        clean = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)
        clean = CADFeatureFilter.filter_degenerate_faces(clean, min_area=min_area)
        clean = CADFeatureFilter.extract_dominant_manifold_shell(clean, weld_tol=0.0)
        return CADFeatureFilter.filter_orphan_vertices(clean)
