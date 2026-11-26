"""Triangle AABB tree, pseudonormals, winding numbers, and VoroMesh bisector loss."""

from __future__ import annotations

import ctypes
from typing import Dict, List, Optional, Tuple, Union
import numpy as np
from scipy.spatial import cKDTree

from backend.meshing.native_loader import get_native_dll

_SDF_CABI_CONFIGURED: bool = False


def _get_sdf_dll() -> Optional[ctypes.CDLL]:
    global _SDF_CABI_CONFIGURED
    dll = get_native_dll()
    if dll is not None and hasattr(dll, "cfd_sdf_create") and not getattr(dll, "_sdf_cabi_configured", False):
        dll.cfd_sdf_create.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_int64,
        ]
        dll.cfd_sdf_create.restype = ctypes.c_void_p

        dll.cfd_sdf_free.argtypes = [ctypes.c_void_p]
        dll.cfd_sdf_free.restype = None

        dll.cfd_sdf_evaluate.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_evaluate.restype = ctypes.c_int32

        dll.cfd_sdf_winding_number.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_winding_number.restype = ctypes.c_int32

        dll.cfd_sdf_voromesh_loss.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.c_int32,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_sdf_voromesh_loss.restype = ctypes.c_int32
        setattr(dll, "_sdf_cabi_configured", True)
        _SDF_CABI_CONFIGURED = True
    return dll


def _point_to_triangle_distance_exact(
    p: np.ndarray,
    a: np.ndarray,
    b: np.ndarray,
    c: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray, float]:
    """Exact point-to-triangle projection using Voronoi feature regions."""
    ab = b - a
    ac = c - a
    ap = p - a

    d1 = float(np.dot(ab, ap))
    d2 = float(np.dot(ac, ap))
    if d1 <= 0.0 and d2 <= 0.0:
        bary = np.array([1.0, 0.0, 0.0], dtype=np.float64)
        return a.copy(), bary, float(np.linalg.norm(p - a))

    bp = p - b
    d3 = float(np.dot(ab, bp))
    d4 = float(np.dot(ac, bp))
    if d3 >= 0.0 and d4 <= d3:
        bary = np.array([0.0, 1.0, 0.0], dtype=np.float64)
        return b.copy(), bary, float(np.linalg.norm(p - b))

    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        denom = d1 - d3
        v = d1 / denom if abs(denom) > 1e-30 else 0.0
        q = a + v * ab
        bary = np.array([1.0 - v, v, 0.0], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    cp = p - c
    d5 = float(np.dot(ab, cp))
    d6 = float(np.dot(ac, cp))
    if d6 >= 0.0 and d5 <= d6:
        bary = np.array([0.0, 0.0, 1.0], dtype=np.float64)
        return c.copy(), bary, float(np.linalg.norm(p - c))

    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        denom = d2 - d6
        w = d2 / denom if abs(denom) > 1e-30 else 0.0
        q = a + w * ac
        bary = np.array([1.0 - w, 0.0, w], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        denom = (d4 - d3) + (d5 - d6)
        w = (d4 - d3) / denom if abs(denom) > 1e-30 else 0.0
        q = b + w * (c - b)
        bary = np.array([0.0, 1.0 - w, w], dtype=np.float64)
        return q, bary, float(np.linalg.norm(p - q))

    denom_sum = va + vb + vc
    inv_denom = 1.0 / denom_sum if abs(denom_sum) > 1e-30 else 0.0
    v = vb * inv_denom
    w = vc * inv_denom
    u = 1.0 - v - w
    q = a + v * ab + w * ac
    bary = np.array([u, v, w], dtype=np.float64)
    return q, bary, float(np.linalg.norm(p - q))


def compute_angle_weighted_pseudonormals(
    vertices: np.ndarray, faces: np.ndarray
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Compute face normals, angle-weighted vertex/edge pseudonormals, and face-edge map."""
    verts = np.asarray(vertices, dtype=np.float64)
    f_arr = np.asarray(faces, dtype=np.int32)
    n_faces = len(f_arr)
    n_verts = len(verts)

    v0 = verts[f_arr[:, 0]]
    v1 = verts[f_arr[:, 1]]
    v2 = verts[f_arr[:, 2]]

    e01 = v1 - v0
    e02 = v2 - v0
    cross_f = np.cross(e01, e02)
    lengths_f = np.linalg.norm(cross_f, axis=1, keepdims=True)
    lengths_f_safe = np.where(lengths_f < 1e-20, 1.0, lengths_f)
    face_normals = cross_f / lengths_f_safe
    zero_mask = (lengths_f.squeeze(-1) < 1e-20)
    if np.any(zero_mask):
        face_normals[zero_mask] = np.array([0.0, 0.0, 1.0])

    vertex_pseudonormals = np.zeros((n_verts, 3), dtype=np.float64)

    d01 = np.linalg.norm(e01, axis=1)
    d02 = np.linalg.norm(e02, axis=1)
    d01_safe = np.where(d01 < 1e-20, 1.0, d01)
    d02_safe = np.where(d02 < 1e-20, 1.0, d02)
    cos0 = np.sum(e01 * e02, axis=1) / (d01_safe * d02_safe)
    alpha0 = np.arccos(np.clip(cos0, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 0], alpha0[:, None] * face_normals)

    e12 = v2 - v1
    e10 = -e01
    d12 = np.linalg.norm(e12, axis=1)
    d12_safe = np.where(d12 < 1e-20, 1.0, d12)
    cos1 = np.sum(e12 * e10, axis=1) / (d12_safe * d01_safe)
    alpha1 = np.arccos(np.clip(cos1, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 1], alpha1[:, None] * face_normals)

    e20 = -e02
    e21 = -e12
    cos2 = np.sum(e20 * e21, axis=1) / (d02_safe * d12_safe)
    alpha2 = np.arccos(np.clip(cos2, -1.0, 1.0))
    np.add.at(vertex_pseudonormals, f_arr[:, 2], alpha2[:, None] * face_normals)

    v_norms = np.linalg.norm(vertex_pseudonormals, axis=1, keepdims=True)
    v_norms_safe = np.where(v_norms < 1e-6, 1.0, v_norms)
    vertex_pseudonormals = vertex_pseudonormals / v_norms_safe
    v_zero = (v_norms.squeeze(-1) < 1e-6)
    if np.any(v_zero):
        vertex_pseudonormals[v_zero] = np.array([0.0, 0.0, 1.0])

    edge_map: Dict[Tuple[int, int], int] = {}
    face_edge_map = np.zeros((n_faces, 3), dtype=np.int32)
    edge_normal_list: List[np.ndarray] = []

    for f_idx in range(n_faces):
        nodes = f_arr[f_idx]
        e_pairs = [
            (min(nodes[0], nodes[1]), max(nodes[0], nodes[1])),
            (min(nodes[1], nodes[2]), max(nodes[1], nodes[2])),
            (min(nodes[2], nodes[0]), max(nodes[2], nodes[0])),
        ]
        fn = face_normals[f_idx]
        for loc_idx, pair in enumerate(e_pairs):
            if pair not in edge_map:
                edge_id = len(edge_normal_list)
                edge_map[pair] = edge_id
                edge_normal_list.append(fn.copy())
            else:
                edge_id = edge_map[pair]
                edge_normal_list[edge_id] += fn
            face_edge_map[f_idx, loc_idx] = edge_id

    edge_pseudonormals = np.array(edge_normal_list, dtype=np.float64)
    e_norms = np.linalg.norm(edge_pseudonormals, axis=1, keepdims=True)
    e_norms_safe = np.where(e_norms < 1e-6, 1.0, e_norms)
    edge_pseudonormals = edge_pseudonormals / e_norms_safe
    e_zero = (e_norms.squeeze(-1) < 1e-6)
    if np.any(e_zero):
        edge_pseudonormals[e_zero] = np.array([0.0, 0.0, 1.0])

    return face_normals, vertex_pseudonormals, edge_pseudonormals, face_edge_map


class _AABBNode:
    __slots__ = ("bmin", "bmax", "left", "right", "tri_indices")

    def __init__(
        self,
        bmin: np.ndarray,
        bmax: np.ndarray,
        left: Optional["_AABBNode"] = None,
        right: Optional["_AABBNode"] = None,
        tri_indices: Optional[np.ndarray] = None,
    ) -> None:
        self.bmin = bmin
        self.bmax = bmax
        self.left = left
        self.right = right
        self.tri_indices = tri_indices


class TriangleAABBTree:
    """Hierarchical Axis-Aligned Bounding Box (AABB) Tree over mesh triangles."""

    def __init__(
        self,
        vertices: np.ndarray,
        faces: np.ndarray,
        leaf_size: int = 8,
    ) -> None:
        self.vertices = np.asarray(vertices, dtype=np.float64)
        self.faces = np.asarray(faces, dtype=np.int32)
        if len(self.faces) == 0:
            raise ValueError("Cannot construct TriangleAABBTree from empty faces.")

        self.tri_v0 = self.vertices[self.faces[:, 0]]
        self.tri_v1 = self.vertices[self.faces[:, 1]]
        self.tri_v2 = self.vertices[self.faces[:, 2]]

        self.tri_bmin = np.minimum(self.tri_v0, np.minimum(self.tri_v1, self.tri_v2))
        self.tri_bmax = np.maximum(self.tri_v0, np.maximum(self.tri_v1, self.tri_v2))
        self.tri_centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0

        all_indices = np.arange(len(self.faces), dtype=np.int32)
        self.root = self._build(all_indices, leaf_size)

    def _build(self, indices: np.ndarray, leaf_size: int) -> _AABBNode:
        bmin = np.min(self.tri_bmin[indices], axis=0)
        bmax = np.max(self.tri_bmax[indices], axis=0)

        if len(indices) <= leaf_size:
            return _AABBNode(bmin, bmax, tri_indices=indices)

        c_bmin = np.min(self.tri_centroids[indices], axis=0)
        c_bmax = np.max(self.tri_centroids[indices], axis=0)
        extent = c_bmax - c_bmin
        axis = int(np.argmax(extent))

        if extent[axis] <= 1e-15:
            return _AABBNode(bmin, bmax, tri_indices=indices)

        sorted_order = np.argsort(self.tri_centroids[indices, axis])
        sorted_indices = indices[sorted_order]
        mid = len(indices) // 2

        left_node = self._build(sorted_indices[:mid], leaf_size)
        right_node = self._build(sorted_indices[mid:], leaf_size)
        return _AABBNode(bmin, bmax, left=left_node, right=right_node)

    def _point_to_tri(self, p: np.ndarray, tid: int) -> Tuple[np.ndarray, np.ndarray, float]:
        return _point_to_triangle_distance_exact(p, self.tri_v0[tid], self.tri_v1[tid], self.tri_v2[tid])

    def closest_point(
        self, p: np.ndarray, prev_facet: Optional[int] = None
    ) -> Tuple[np.ndarray, int, np.ndarray, float]:
        """Find closest point on triangulated mesh using branch-and-bound traversal."""
        pt = np.asarray(p, dtype=np.float64)

        best_dist = float("inf")
        best_dist_sq = float("inf")
        best_q: Optional[np.ndarray] = None
        best_tid: int = -1
        best_bary: Optional[np.ndarray] = None

        if prev_facet is not None and 0 <= prev_facet < len(self.faces):
            q, bary, d = self._point_to_tri(pt, prev_facet)
            best_dist = d
            best_dist_sq = d * d
            best_q = q
            best_tid = prev_facet
            best_bary = bary

        stack = [self.root]
        px, py, pz = pt[0], pt[1], pt[2]

        while stack:
            node = stack.pop()

            bmin = node.bmin
            bmax = node.bmax
            dx = max(0.0, bmin[0] - px, px - bmax[0])
            dy = max(0.0, bmin[1] - py, py - bmax[1])
            dz = max(0.0, bmin[2] - pz, pz - bmax[2])
            box_dist_sq = dx * dx + dy * dy + dz * dz

            if box_dist_sq >= best_dist_sq:
                continue

            if node.tri_indices is not None:
                for tid in node.tri_indices:
                    q, bary, d = self._point_to_tri(pt, int(tid))
                    d_sq = d * d
                    if d_sq < best_dist_sq:
                        best_dist = d
                        best_dist_sq = d_sq
                        best_q = q
                        best_tid = int(tid)
                        best_bary = bary
            else:
                left = node.left
                right = node.right

                dx_l = max(0.0, left.bmin[0] - px, px - left.bmax[0])
                dy_l = max(0.0, left.bmin[1] - py, py - left.bmax[1])
                dz_l = max(0.0, left.bmin[2] - pz, pz - left.bmax[2])
                dl_sq = dx_l * dx_l + dy_l * dy_l + dz_l * dz_l

                dx_r = max(0.0, right.bmin[0] - px, px - right.bmax[0])
                dy_r = max(0.0, right.bmin[1] - py, py - right.bmax[1])
                dz_r = max(0.0, right.bmin[2] - pz, pz - right.bmax[2])
                dr_sq = dx_r * dx_r + dy_r * dy_r + dz_r * dz_r

                if dl_sq < dr_sq:
                    if dr_sq < best_dist_sq:
                        stack.append(right)
                    if dl_sq < best_dist_sq:
                        stack.append(left)
                else:
                    if dl_sq < best_dist_sq:
                        stack.append(left)
                    if dr_sq < best_dist_sq:
                        stack.append(right)

        return best_q, best_tid, best_bary, best_dist

    def query(
        self, points: np.ndarray, prev_facet: Optional[int] = None
    ) -> Union[
        Tuple[float, np.ndarray, int, np.ndarray],
        Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray],
    ]:
        """Query closest point(s) on the mesh."""
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            q, tid, bary, dist = self.closest_point(pts, prev_facet=prev_facet)
            return dist, q, tid, bary

        n_pts = len(pts)
        dists = np.empty(n_pts, dtype=np.float64)
        closest_pts = np.empty((n_pts, 3), dtype=np.float64)
        facet_ids = np.empty(n_pts, dtype=np.int32)
        bary_coords = np.empty((n_pts, 3), dtype=np.float64)

        hint = prev_facet if (prev_facet is not None and 0 <= prev_facet < len(self.faces)) else -1
        for i in range(n_pts):
            q, tid, bary, d = self.closest_point(pts[i], prev_facet=hint)
            dists[i] = d
            closest_pts[i] = q
            facet_ids[i] = tid
            bary_coords[i] = bary
            hint = tid

        return dists, closest_pts, facet_ids, bary_coords


def compute_mesh_winding_number(
    points: np.ndarray,
    tri_v0: np.ndarray,
    tri_v1: np.ndarray,
    tri_v2: np.ndarray,
    chunk_size: int = 16,
) -> np.ndarray:
    """Compute Generalized Winding Numbers via Barnes-Hut far-field dipole approximation."""
    pts = np.asarray(points, dtype=np.float64)
    is_single = (pts.ndim == 1)
    p = np.atleast_2d(pts)
    n_pts = len(p)
    n_tri = len(tri_v0)

    if n_tri == 0:
        return 0.0 if is_single else np.zeros(n_pts, dtype=np.float64)

    total_solid_angles = np.zeros(n_pts, dtype=np.float64)

    if n_tri <= 16:
        effective_chunk = n_tri
        use_dipole = False
    else:
        effective_chunk = max(8, min(chunk_size, 64))
        use_dipole = True

    for start_t in range(0, n_tri, effective_chunk):
        end_t = min(start_t + effective_chunk, n_tri)
        a = tri_v0[start_t:end_t]
        b = tri_v1[start_t:end_t]
        c = tri_v2[start_t:end_t]

        if use_dipole:
            cluster_pts = np.vstack([a, b, c])
            c_bmin = np.min(cluster_pts, axis=0)
            c_bmax = np.max(cluster_pts, axis=0)
            node_radius = 0.5 * float(np.linalg.norm(c_bmax - c_bmin))
            node_centroid = 0.5 * (c_bmin + c_bmax)
            area_vecs = 0.5 * np.cross(b - a, c - a)
            cluster_area_vec = np.sum(area_vecs, axis=0)

            diff = node_centroid[None, :] - p
            dists = np.linalg.norm(diff, axis=-1)

            far_mask = dists > (2.0 * node_radius)
            if np.any(far_mask):
                d_far = dists[far_mask]
                r3 = d_far ** 3
                dipole_omega = np.sum(diff[far_mask] * cluster_area_vec[None, :], axis=-1) / (r3 + 1e-30)
                total_solid_angles[far_mask] += dipole_omega

            near_indices = np.where(~far_mask)[0]
            if len(near_indices) == 0:
                continue
            sub_p = p[near_indices]
        else:
            near_indices = np.arange(n_pts)
            sub_p = p

        ra = a[None, :, :] - sub_p[:, None, :]
        rb = b[None, :, :] - sub_p[:, None, :]
        rc = c[None, :, :] - sub_p[:, None, :]

        da = np.linalg.norm(ra, axis=-1)
        db = np.linalg.norm(rb, axis=-1)
        dc = np.linalg.norm(rc, axis=-1)

        rb_cross_rc = np.cross(rb, rc)
        num = np.sum(ra * rb_cross_rc, axis=-1)

        ra_dot_rb = np.sum(ra * rb, axis=-1)
        ra_dot_rc = np.sum(ra * rc, axis=-1)
        rb_dot_rc = np.sum(rb * rc, axis=-1)

        denom = da * db * dc + ra_dot_rb * dc + ra_dot_rc * db + rb_dot_rc * da
        omega = 2.0 * np.arctan2(num, denom)
        total_solid_angles[near_indices] += np.sum(omega, axis=1)

    wn = total_solid_angles / (4.0 * np.pi)
    return wn[0] if is_single else wn


def voromesh_bisector_loss(
    points: np.ndarray,
    seeds: np.ndarray,
    k: int = 16,
    seed_signs: Optional[np.ndarray] = None,
    use_native: bool = True,
) -> Tuple[float, np.ndarray, np.ndarray]:
    """Differentiable VoroMesh bisector loss following Maruani et al. (2023)."""
    pts = np.asarray(points, dtype=np.float64)
    s_arr = np.asarray(seeds, dtype=np.float64)
    n_pts = len(pts)
    n_seeds = len(s_arr)

    if n_seeds < 2:
        raise ValueError("At least 2 seeds required for Voronoi bisector loss.")

    k_eff = min(k, n_seeds - 1)

    if use_native:
        dll = _get_sdf_dll()
        if dll is not None and hasattr(dll, "cfd_sdf_voromesh_loss"):
            try:
                c_points = pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                c_seeds = s_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
                out_loss = ctypes.c_double(0.0)
                out_dists = np.empty(n_pts, dtype=np.float64)
                out_grads = np.empty((n_pts, 3), dtype=np.float64)
                c_signs = (
                    np.ascontiguousarray(seed_signs, dtype=np.float64).ctypes.data_as(
                        ctypes.POINTER(ctypes.c_double)
                    )
                    if seed_signs is not None
                    else None
                )

                res = dll.cfd_sdf_voromesh_loss(
                    c_points,
                    ctypes.c_int64(n_pts),
                    c_seeds,
                    ctypes.c_int64(n_seeds),
                    ctypes.c_int32(k_eff),
                    ctypes.byref(out_loss),
                    out_dists.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    out_grads.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    c_signs,
                )
                if res == 0:
                    return float(out_loss.value), out_dists, out_grads
            except Exception:
                pass

    seed_tree = cKDTree(s_arr)
    _, nearest_seed_idx = seed_tree.query(pts, k=1)
    if nearest_seed_idx.ndim == 0:
        nearest_seed_idx = np.array([nearest_seed_idx])

    _, seed_neighbors = seed_tree.query(s_arr, k=k_eff + 1)
    nbr_indices = seed_neighbors[:, 1:]

    qi = s_arr[nearest_seed_idx]
    pt_nbr_indices = nbr_indices[nearest_seed_idx]
    qj = s_arr[pt_nbr_indices]

    qi_exp = qi[:, None, :]
    midpoints = 0.5 * (qi_exp + qj)
    delta = qj - qi_exp
    delta_len = np.linalg.norm(delta, axis=-1, keepdims=True)
    delta_len_safe = np.where(delta_len < 1e-15, 1.0, delta_len)
    normals = delta / delta_len_safe

    pts_exp = pts[:, None, :]
    h = np.sum((pts_exp - midpoints) * normals, axis=-1)
    d = np.abs(h)

    if seed_signs is not None:
        s_signs = np.asarray(seed_signs, dtype=np.float64)
        qi_signs = s_signs[nearest_seed_idx, None]
        qj_signs = s_signs[pt_nbr_indices]
        valid_pairs = (qi_signs * qj_signs) < 0.0
        d_eff = np.where(valid_pairs, d, np.inf)
        has_valid = np.any(valid_pairs, axis=1)
        best_k = np.zeros(n_pts, dtype=np.int64)
        if np.any(has_valid):
            best_k[has_valid] = np.argmin(d_eff[has_valid], axis=1)
        if not np.all(has_valid):
            best_k[~has_valid] = np.argmin(d[~has_valid], axis=1)
    else:
        best_k = np.argmin(d, axis=1)

    rows = np.arange(n_pts)
    distances = d[rows, best_k]
    best_h = h[rows, best_k]
    best_normal = normals[rows, best_k]

    sign = np.where(best_h >= 0.0, 1.0, -1.0)[:, None]
    gradients = sign * best_normal
    loss = float(np.mean(distances ** 2))
    return loss, distances, gradients
