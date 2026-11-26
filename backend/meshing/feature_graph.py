"""Scale-aware surface feature graph extraction with windowed normal jumps and Canny hysteresis."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Set, Tuple, Union
import numpy as np

EdgeKey = Tuple[int, int]


@dataclass
class FeatureGraph:
    """Extracted topological and geometric feature decomposition of a surface mesh."""

    sharp_edges: np.ndarray
    bc_edges: np.ndarray
    feature_corners: np.ndarray
    tri_smooth_patch: np.ndarray
    edge_windowed_angles_deg: Dict[EdgeKey, float] = field(default_factory=dict)
    patch_count: int = 0
    sharp_edge_set: Set[EdgeKey] = field(default_factory=set)
    bc_edge_set: Set[EdgeKey] = field(default_factory=set)


def _canonical_edge(u: int, v: int) -> EdgeKey:
    return (u, v) if u < v else (v, u)


def _parse_face_patches(
    patches: Optional[Union[Dict[str, np.ndarray], Iterable[int], np.ndarray]],
    n_faces: int,
) -> Optional[np.ndarray]:
    """Normalize optional patch mapping or array into per-face integer patch IDs."""
    if patches is None:
        return None
    if isinstance(patches, dict):
        if not patches:
            return None
        face_patch = np.full(n_faces, -1, dtype=np.int32)
        for pid, (_, f_indices) in enumerate(patches.items()):
            idx = np.asarray(f_indices, dtype=np.int64)
            valid = idx[(idx >= 0) & (idx < n_faces)]
            face_patch[valid] = pid
        return face_patch
    arr = np.asarray(patches, dtype=np.int32)
    if arr.shape != (n_faces,):
        raise ValueError(f"patches array must have shape ({n_faces},), got {arr.shape}")
    return arr


def _point_to_segment_distance(pt: np.ndarray, a: np.ndarray, ab: np.ndarray, ab_len2: float) -> float:
    """Exact Euclidean distance from point pt to segment [a, a + ab]."""
    if ab_len2 <= 1e-30:
        return float(np.linalg.norm(pt - a))
    t = float(np.dot(pt - a, ab)) / ab_len2
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    proj = a + t * ab
    return float(np.linalg.norm(pt - proj))


def _windowed_dihedral_deg(
    edge: EdgeKey,
    f0: int,
    f1: int,
    verts: np.ndarray,
    normals: np.ndarray,
    areas: np.ndarray,
    centroids: np.ndarray,
    face_adj: List[List[Tuple[int, EdgeKey]]],
    bc_edge_set: Set[EdgeKey],
    half_width: float,
    theta_raw: float,
) -> float:
    """Compute area-weighted windowed normal jump angle (deg) across edge over radius half_width."""
    if half_width <= 0.0:
        return theta_raw

    p_a = verts[edge[0]]
    ab = verts[edge[1]] - p_a
    ab_len2 = float(np.dot(ab, ab))

    side: Dict[int, int] = {f0: 0, f1: 1}
    queue: deque[int] = deque((f0, f1))
    sum_n = [areas[f0] * normals[f0], areas[f1] * normals[f1]]

    while queue:
        fc = queue.popleft()
        sc = side[fc]
        for fn, shared_edge in face_adj[fc]:
            if fn in side:
                continue
            if shared_edge == edge or shared_edge in bc_edge_set:
                continue
            if _point_to_segment_distance(centroids[fn], p_a, ab, ab_len2) > half_width:
                continue
            side[fn] = sc
            sum_n[sc] = sum_n[sc] + areas[fn] * normals[fn]
            queue.append(fn)

    n0_len = float(np.linalg.norm(sum_n[0]))
    n1_len = float(np.linalg.norm(sum_n[1]))
    if n0_len < 1e-20 or n1_len < 1e-20:
        return theta_raw

    cos_w = float(np.clip(np.dot(sum_n[0] / n0_len, sum_n[1] / n1_len), -1.0, 1.0))
    return float(np.degrees(np.arccos(cos_w)))


def _prune_short_isolated_chains(
    sharp_edge_set: Set[EdgeKey],
    bc_edge_set: Set[EdgeKey],
    verts: np.ndarray,
    min_chain_length: float,
) -> Set[EdgeKey]:
    """Discard connected non-BC sharp chains whose total arc length is below min_chain_length."""
    if not sharp_edge_set or min_chain_length <= 0.0:
        return sharp_edge_set

    bc_verts: Set[int] = set()
    for u, v in bc_edge_set:
        bc_verts.add(u)
        bc_verts.add(v)

    vert_edges: Dict[int, List[EdgeKey]] = defaultdict(list)
    for e in sharp_edge_set:
        vert_edges[e[0]].append(e)
        vert_edges[e[1]].append(e)

    visited_edges: Set[EdgeKey] = set()
    kept_edges: Set[EdgeKey] = set()

    for seed in sharp_edge_set:
        if seed in visited_edges:
            continue
        comp_edges: List[EdgeKey] = []
        comp_verts: Set[int] = set()
        queue: deque[EdgeKey] = deque([seed])
        visited_edges.add(seed)

        while queue:
            cur = queue.popleft()
            comp_edges.append(cur)
            comp_verts.add(cur[0])
            comp_verts.add(cur[1])
            for v in cur:
                for nbr_e in vert_edges[v]:
                    if nbr_e not in visited_edges:
                        visited_edges.add(nbr_e)
                        queue.append(nbr_e)

        touches_bc = not comp_verts.isdisjoint(bc_verts)
        if touches_bc:
            kept_edges.update(comp_edges)
            continue

        arc_len = 0.0
        for u, v in comp_edges:
            arc_len += float(np.linalg.norm(verts[v] - verts[u]))

        if arc_len >= min_chain_length:
            kept_edges.update(comp_edges)

    return kept_edges


def _assign_smooth_patches(
    n_faces: int,
    face_adj: List[List[Tuple[int, EdgeKey]]],
    hard_edges: Set[EdgeKey],
) -> Tuple[np.ndarray, int]:
    """Flood-fill triangles across non-hard edges to label smooth patches."""
    tri_smooth_patch = np.full(n_faces, -1, dtype=np.int32)
    patch_id = 0

    for start_f in range(n_faces):
        if tri_smooth_patch[start_f] != -1:
            continue
        tri_smooth_patch[start_f] = patch_id
        queue: deque[int] = deque([start_f])
        while queue:
            fc = queue.popleft()
            for fn, shared_edge in face_adj[fc]:
                if tri_smooth_patch[fn] != -1 or shared_edge in hard_edges:
                    continue
                tri_smooth_patch[fn] = patch_id
                queue.append(fn)
        patch_id += 1

    return tri_smooth_patch, patch_id


def _detect_feature_corners(
    hard_edges: Set[EdgeKey],
    verts: np.ndarray,
    corner_turn_deg: float = 35.0,
) -> np.ndarray:
    """Identify feature corner vertices from hard-edge valence >= 3 or degree == 2 tangent turn."""
    if not hard_edges:
        return np.empty(0, dtype=np.int64)

    vert_nbrs: Dict[int, List[int]] = defaultdict(list)
    for u, v in hard_edges:
        vert_nbrs[u].append(v)
        vert_nbrs[v].append(u)

    corners: List[int] = []
    for v, nbrs in vert_nbrs.items():
        if len(nbrs) >= 3:
            corners.append(v)
        elif len(nbrs) == 2:
            d1 = verts[v] - verts[nbrs[0]]
            d2 = verts[nbrs[1]] - verts[v]
            n1 = float(np.linalg.norm(d1))
            n2 = float(np.linalg.norm(d2))
            if n1 > 1e-20 and n2 > 1e-20:
                cos_turn = float(np.clip(np.dot(d1 / n1, d2 / n2), -1.0, 1.0))
                if float(np.degrees(np.arccos(cos_turn))) > corner_turn_deg:
                    corners.append(v)

    corners.sort()
    return np.asarray(corners, dtype=np.int64)


def extract_feature_graph(
    vertices: np.ndarray,
    faces: np.ndarray,
    h_min: float,
    patches: Optional[Union[Dict[str, np.ndarray], Iterable[int], np.ndarray]] = None,
    sharp_angle_deg: float = 35.0,
    candidate_angle_deg: float = 10.0,
    hysteresis_ratio: float = 0.6,
) -> FeatureGraph:
    """Extract scale-aware sharp edges, BC edges, corners, and smooth patches from a surface mesh."""
    verts = np.asarray(vertices, dtype=np.float64)
    tris = np.asarray(faces, dtype=np.int64)
    n_faces = len(tris)

    if n_faces == 0:
        return FeatureGraph(
            sharp_edges=np.empty((0, 2), dtype=np.int64),
            bc_edges=np.empty((0, 2), dtype=np.int64),
            feature_corners=np.empty(0, dtype=np.int64),
            tri_smooth_patch=np.empty(0, dtype=np.int32),
            edge_windowed_angles_deg={},
            patch_count=0,
            sharp_edge_set=set(),
            bc_edge_set=set(),
        )

    v0 = verts[tris[:, 0]]
    v1 = verts[tris[:, 1]]
    v2 = verts[tris[:, 2]]
    cross = np.cross(v1 - v0, v2 - v0)
    double_area = np.linalg.norm(cross, axis=1)
    areas = 0.5 * double_area
    normals = np.zeros_like(cross)
    valid = double_area > 1e-20
    normals[valid] = cross[valid] / double_area[valid, None]
    centroids = (v0 + v1 + v2) / 3.0

    face_patch_id = _parse_face_patches(patches, n_faces)

    edge_faces: Dict[EdgeKey, List[int]] = defaultdict(list)
    for fi in range(n_faces):
        a, b, c = int(tris[fi, 0]), int(tris[fi, 1]), int(tris[fi, 2])
        edge_faces[_canonical_edge(a, b)].append(fi)
        edge_faces[_canonical_edge(b, c)].append(fi)
        edge_faces[_canonical_edge(c, a)].append(fi)

    bc_edge_set: Set[EdgeKey] = set()
    face_adj: List[List[Tuple[int, EdgeKey]]] = [[] for _ in range(n_faces)]
    internal_manifold_edges: List[Tuple[EdgeKey, int, int]] = []

    for edge, adj in edge_faces.items():
        if len(adj) != 2:
            bc_edge_set.add(edge)
            continue
        f0, f1 = adj[0], adj[1]
        if f0 == f1:
            bc_edge_set.add(edge)
            continue
        face_adj[f0].append((f1, edge))
        face_adj[f1].append((f0, edge))
        if face_patch_id is not None and face_patch_id[f0] != face_patch_id[f1]:
            bc_edge_set.add(edge)
        else:
            internal_manifold_edges.append((edge, f0, f1))

    half_w = max(float(h_min), 0.0)
    edge_windowed_angles_deg: Dict[EdgeKey, float] = {}
    for edge, f0, f1 in internal_manifold_edges:
        if areas[f0] <= 1e-20 or areas[f1] <= 1e-20:
            continue
        cos_raw = float(np.clip(np.dot(normals[f0], normals[f1]), -1.0, 1.0))
        theta_raw = float(np.degrees(np.arccos(cos_raw)))
        if theta_raw <= candidate_angle_deg:
            continue
        theta_w = _windowed_dihedral_deg(
            edge=edge,
            f0=f0,
            f1=f1,
            verts=verts,
            normals=normals,
            areas=areas,
            centroids=centroids,
            face_adj=face_adj,
            bc_edge_set=bc_edge_set,
            half_width=half_w,
            theta_raw=theta_raw,
        )
        edge_windowed_angles_deg[edge] = theta_w

    low_thresh = float(hysteresis_ratio) * float(sharp_angle_deg)
    seed_edges = [e for e, tw in edge_windowed_angles_deg.items() if tw >= sharp_angle_deg]
    elig_edges = {e for e, tw in edge_windowed_angles_deg.items() if tw >= low_thresh}

    elig_vert_edges: Dict[int, List[EdgeKey]] = defaultdict(list)
    for e in elig_edges:
        elig_vert_edges[e[0]].append(e)
        elig_vert_edges[e[1]].append(e)

    sharp_edge_set: Set[EdgeKey] = set(seed_edges)
    queue: deque[EdgeKey] = deque(seed_edges)
    while queue:
        cur = queue.popleft()
        for v in cur:
            for nbr_e in elig_vert_edges[v]:
                if nbr_e not in sharp_edge_set:
                    sharp_edge_set.add(nbr_e)
                    queue.append(nbr_e)

    sharp_edge_set = _prune_short_isolated_chains(
        sharp_edge_set=sharp_edge_set,
        bc_edge_set=bc_edge_set,
        verts=verts,
        min_chain_length=2.0 * half_w,
    )

    hard_edges = sharp_edge_set | bc_edge_set
    tri_smooth_patch, patch_count = _assign_smooth_patches(
        n_faces=n_faces,
        face_adj=face_adj,
        hard_edges=hard_edges,
    )
    feature_corners = _detect_feature_corners(hard_edges=hard_edges, verts=verts)

    sharp_arr = (
        np.asarray(sorted(sharp_edge_set), dtype=np.int64)
        if sharp_edge_set
        else np.empty((0, 2), dtype=np.int64)
    )
    bc_arr = (
        np.asarray(sorted(bc_edge_set), dtype=np.int64)
        if bc_edge_set
        else np.empty((0, 2), dtype=np.int64)
    )

    return FeatureGraph(
        sharp_edges=sharp_arr,
        bc_edges=bc_arr,
        feature_corners=feature_corners,
        tri_smooth_patch=tri_smooth_patch,
        edge_windowed_angles_deg=edge_windowed_angles_deg,
        patch_count=patch_count,
        sharp_edge_set=sharp_edge_set,
        bc_edge_set=bc_edge_set,
    )
