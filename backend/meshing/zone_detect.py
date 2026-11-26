"""Topological Ray-Blocked ZoneDetect for zero-leak chamber isolation and thin-wall baffles."""

from __future__ import annotations

from collections import deque
import ctypes
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple
import numpy as np

from backend.meshing.models import UnstructuredMesh
from backend.meshing.native_loader import get_native_dll
from backend.meshing.topology_coupling import extract_cht_twin_faces


@dataclass
class ZoneDetectResult:
    """Output of topological BVH ray-blocked zone detection."""

    mesh: UnstructuredMesh
    num_zones: int
    cell_regions: np.ndarray
    zone_cell_counts: Dict[int, int]
    blocked_faces: np.ndarray
    n_blocked_faces: int
    thermal_resistance_m2k_w: Optional[float] = None
    baffles: List[Dict[str, Any]] = field(default_factory=list)
    used_native: bool = False


def _get_segment_blocked_dll() -> Optional[ctypes.CDLL]:
    dll = get_native_dll(strict_native=False)
    if (
        dll is not None
        and hasattr(dll, "cfd_sdf_create")
        and hasattr(dll, "cfd_sdf_free")
        and hasattr(dll, "cfd_sdf_segment_blocked")
        and not getattr(dll, "_zone_detect_cabi_configured", False)
    ):
        dll.cfd_sdf_create.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_int64,
        ]
        dll.cfd_sdf_create.restype = ctypes.c_void_p

        dll.cfd_sdf_free.argtypes = [ctypes.c_void_p]
        dll.cfd_sdf_free.restype = None

        dll.cfd_sdf_segment_blocked.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_uint8),
        ]
        dll.cfd_sdf_segment_blocked.restype = ctypes.c_int32
        setattr(dll, "_zone_detect_cabi_configured", True)
    return dll


def _segments_blocked_python_fallback(
    verts: np.ndarray,
    tris: np.ndarray,
    pts_a: np.ndarray,
    pts_b: np.ndarray,
) -> np.ndarray:
    """Vectorized Moller-Trumbore fallback when native DLL is unavailable."""
    n_seg = len(pts_a)
    blocked = np.zeros(n_seg, dtype=np.uint8)
    if n_seg == 0 or len(tris) == 0:
        return blocked

    v0 = verts[tris[:, 0]]
    v1 = verts[tris[:, 1]]
    v2 = verts[tris[:, 2]]
    e1 = v1 - v0
    e2 = v2 - v0
    tri_min = np.minimum(np.minimum(v0, v1), v2) - 1e-12
    tri_max = np.maximum(np.maximum(v0, v1), v2) + 1e-12

    seg_min = np.minimum(pts_a, pts_b)
    seg_max = np.maximum(pts_a, pts_b)
    dirs = pts_b - pts_a

    batch = max(1, 50_000 // max(len(tris), 1))
    for s0 in range(0, n_seg, batch):
        s1 = min(n_seg, s0 + batch)
        a_b = pts_a[s0:s1, None, :]
        d_b = dirs[s0:s1, None, :]
        overlap = np.all(
            (seg_max[s0:s1, None, :] >= tri_min[None, :, :])
            & (seg_min[s0:s1, None, :] <= tri_max[None, :, :]),
            axis=2,
        )
        if not np.any(overlap):
            continue

        pvec = np.cross(d_b, e2[None, :, :])
        det = np.einsum("mk,smk->sm", e1, pvec)
        valid = overlap & (np.abs(det) > 1e-30)
        inv_det = np.where(valid, 1.0 / np.where(valid, det, 1.0), 0.0)

        tvec = a_b - v0[None, :, :]
        u = np.einsum("smk,smk->sm", tvec, pvec) * inv_det
        valid &= (u >= -1e-11) & (u <= 1.0 + 1e-11)

        qvec = np.cross(tvec, e1[None, :, :])
        v = np.einsum("smk,smk->sm", d_b, qvec) * inv_det
        valid &= (v >= -1e-11) & ((u + v) <= 1.0 + 1e-11)

        t = np.einsum("mk,smk->sm", e2, qvec) * inv_det
        hit = valid & (t > 1e-9) & (t < 1.0 - 1e-9)
        blocked[s0:s1] = np.any(hit, axis=1).astype(np.uint8)
    return blocked


def _query_segments_blocked(
    barrier_vertices: np.ndarray,
    barrier_faces: np.ndarray,
    pts_a: np.ndarray,
    pts_b: np.ndarray,
) -> Tuple[np.ndarray, bool]:
    """Query BVH segment intersections via native C-ABI with pure-Python fallback."""
    n_seg = len(pts_a)
    if n_seg == 0 or len(barrier_faces) == 0:
        return np.zeros(n_seg, dtype=np.uint8), False

    dll = _get_segment_blocked_dll()
    if (
        dll is not None
        and hasattr(dll, "cfd_sdf_create")
        and hasattr(dll, "cfd_sdf_segment_blocked")
    ):
        v_c = np.ascontiguousarray(barrier_vertices, dtype=np.float64)
        f_c = np.ascontiguousarray(barrier_faces, dtype=np.int32)
        a_c = np.ascontiguousarray(pts_a, dtype=np.float64)
        b_c = np.ascontiguousarray(pts_b, dtype=np.float64)
        out_blocked = np.zeros(n_seg, dtype=np.uint8)

        eng = dll.cfd_sdf_create(
            v_c.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            len(v_c),
            f_c.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            len(f_c),
        )
        if eng:
            try:
                rc = dll.cfd_sdf_segment_blocked(
                    eng,
                    a_c.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    b_c.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                    n_seg,
                    out_blocked.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)),
                )
                if rc == 0:
                    return out_blocked, True
            finally:
                dll.cfd_sdf_free(eng)

    return _segments_blocked_python_fallback(barrier_vertices, barrier_faces, pts_a, pts_b), False


def detect_zones_ray_blocked(
    mesh: UnstructuredMesh,
    barrier_vertices: np.ndarray,
    barrier_faces: np.ndarray,
    thin_wall_thickness_m: Optional[float] = None,
    thermal_conductivity_w_mk: Optional[float] = None,
    extract_twins: bool = False,
) -> ZoneDetectResult:
    """Detect topologically isolated zones via ray-blocked internal face flood-fill.

    Tests segment ``cell_centres[owner[f]] -> cell_centres[neighbour[f]]`` against the
    triangulated barrier surface for every internal face ``f < n_internal``. Flood-fill
    only traverses unblocked internal faces, guaranteeing zero leakage even when wall
    thickness ``tau < h0``.
    """
    verts = np.asarray(barrier_vertices, dtype=np.float64)
    tris = np.asarray(barrier_faces, dtype=np.int32)
    if verts.ndim != 2 or verts.shape[1] != 3:
        raise ValueError(f"barrier_vertices must have shape (N, 3), got {verts.shape}")
    if tris.ndim != 2 or tris.shape[1] != 3:
        raise ValueError(f"barrier_faces must have shape (M, 3), got {tris.shape}")

    if thin_wall_thickness_m is not None and thin_wall_thickness_m <= 0.0:
        raise ValueError(f"thin_wall_thickness_m must be positive, got {thin_wall_thickness_m}")
    if thermal_conductivity_w_mk is not None and thermal_conductivity_w_mk <= 0.0:
        raise ValueError(
            f"thermal_conductivity_w_mk must be positive, got {thermal_conductivity_w_mk}"
        )

    thermal_resistance: Optional[float] = None
    if thin_wall_thickness_m is not None and thermal_conductivity_w_mk is not None:
        thermal_resistance = float(thin_wall_thickness_m) / float(thermal_conductivity_w_mk)

    n_cells = mesh.num_cells
    n_internal = mesh.num_internal_faces
    if n_cells == 0:
        empty_reg = np.empty(0, dtype=np.int32)
        mesh.cell_regions = empty_reg
        return ZoneDetectResult(
            mesh=mesh,
            num_zones=0,
            cell_regions=empty_reg,
            zone_cell_counts={},
            blocked_faces=np.empty(0, dtype=np.int64),
            n_blocked_faces=0,
            thermal_resistance_m2k_w=thermal_resistance,
            baffles=[],
            used_native=False,
        )

    int_indices = np.where(mesh.neighbour >= 0)[0].astype(np.int64)
    owner_int = mesh.owner[int_indices]
    neigh_int = mesh.neighbour[int_indices]
    n_internal = len(int_indices)
    pts_a = mesh.cell_centres[owner_int] if n_internal > 0 else np.empty((0, 3), dtype=np.float64)
    pts_b = mesh.cell_centres[neigh_int] if n_internal > 0 else np.empty((0, 3), dtype=np.float64)

    blocked_mask, used_native = _query_segments_blocked(verts, tris, pts_a, pts_b)
    blocked_pos = np.where(blocked_mask != 0)[0]
    unblocked_pos = np.where(blocked_mask == 0)[0]
    blocked_faces = int_indices[blocked_pos]

    # Build unblocked adjacency CSR for fast O(N) BFS flood-fill
    deg = np.zeros(n_cells, dtype=np.int64)
    if len(unblocked_pos) > 0:
        uo = owner_int[unblocked_pos]
        un = neigh_int[unblocked_pos]
        np.add.at(deg, uo, 1)
        np.add.at(deg, un, 1)

    adj_offsets = np.zeros(n_cells + 1, dtype=np.int64)
    np.cumsum(deg, out=adj_offsets[1:])
    adj_neighbors = np.empty(int(adj_offsets[-1]), dtype=np.int64)
    cursor = adj_offsets[:-1].copy()
    for pos in unblocked_pos:
        o = int(owner_int[pos])
        nb = int(neigh_int[pos])
        adj_neighbors[cursor[o]] = nb
        cursor[o] += 1
        adj_neighbors[cursor[nb]] = o
        cursor[nb] += 1

    cell_regions = np.full(n_cells, -1, dtype=np.int32)
    zone_counts: Dict[int, int] = {}
    zone_id = 0
    for seed in range(n_cells):
        if cell_regions[seed] >= 0:
            continue
        cell_regions[seed] = zone_id
        q: deque[int] = deque([seed])
        cnt = 0
        while q:
            cur = q.popleft()
            cnt += 1
            s_idx = int(adj_offsets[cur])
            e_idx = int(adj_offsets[cur + 1])
            for k in range(s_idx, e_idx):
                nbr = int(adj_neighbors[k])
                if cell_regions[nbr] < 0:
                    cell_regions[nbr] = zone_id
                    q.append(nbr)
        zone_counts[zone_id] = cnt
        zone_id += 1

    mesh.cell_regions = cell_regions

    pair_faces_map: Dict[Tuple[int, int], List[int]] = {}
    for f_idx in blocked_faces:
        f_i = int(f_idx)
        zo = int(cell_regions[int(mesh.owner[f_i])])
        zn = int(cell_regions[int(mesh.neighbour[f_i])])
        pair = (min(zo, zn), max(zo, zn))
        pair_faces_map.setdefault(pair, []).append(f_i)

    baffle_records: List[Dict[str, Any]] = []
    for za, zb in sorted(pair_faces_map.keys()):
        f_list = pair_faces_map[(za, zb)]
        rec: Dict[str, Any] = {
            "name": f"zone{za}_to_zone{zb}",
            "type": "baffle" if za == zb else "coupledWall",
            "region_a": za,
            "region_b": zb,
            "num_faces": len(f_list),
            "face_indices": np.asarray(f_list, dtype=np.int64),
        }
        if thin_wall_thickness_m is not None:
            rec["thin_wall_thickness_m"] = float(thin_wall_thickness_m)
        if thermal_conductivity_w_mk is not None:
            rec["thermal_conductivity_w_mk"] = float(thermal_conductivity_w_mk)
        if thermal_resistance is not None:
            rec["thermal_resistance_m2k_w"] = thermal_resistance
        baffle_records.append(rec)

    zone_meta: Dict[str, Any] = {
        "num_zones": zone_id,
        "zone_cell_counts": zone_counts,
        "n_blocked_faces": int(len(blocked_faces)),
        "used_native": used_native,
        "thin_wall_thickness_m": float(thin_wall_thickness_m) if thin_wall_thickness_m is not None else None,
        "thermal_conductivity_w_mk": (
            float(thermal_conductivity_w_mk) if thermal_conductivity_w_mk is not None else None
        ),
        "thermal_resistance_m2k_w": thermal_resistance,
        "interfaces": [
            {k: (v.tolist() if isinstance(v, np.ndarray) else v) for k, v in rec.items() if k != "face_indices"}
            for rec in baffle_records
        ],
    }
    mesh.metadata["zone_detect"] = zone_meta

    out_mesh = mesh
    if extract_twins and zone_id > 1:
        if "cht_zone_names" not in mesh.metadata:
            mesh.metadata["cht_zone_names"] = [f"zone{z}" for z in range(zone_id)]
        out_mesh = extract_cht_twin_faces(mesh)
        out_mesh.metadata["zone_detect"] = zone_meta
        pair_lookup = {(r["region_a"], r["region_b"]): r for r in baffle_records}
        for b_entry in out_mesh.baffles:
            ra = int(b_entry.get("region_a", -1))
            rb = int(b_entry.get("region_b", -1))
            src = pair_lookup.get((min(ra, rb), max(ra, rb)))
            if src is not None:
                if "thin_wall_thickness_m" in src:
                    b_entry["thin_wall_thickness_m"] = src["thin_wall_thickness_m"]
                if "thermal_conductivity_w_mk" in src:
                    b_entry["thermal_conductivity_w_mk"] = src["thermal_conductivity_w_mk"]
                if "thermal_resistance_m2k_w" in src:
                    b_entry["thermal_resistance_m2k_w"] = src["thermal_resistance_m2k_w"]
        final_baffles = out_mesh.baffles
    else:
        mesh.baffles = baffle_records
        final_baffles = baffle_records

    return ZoneDetectResult(
        mesh=out_mesh,
        num_zones=zone_id,
        cell_regions=cell_regions,
        zone_cell_counts=zone_counts,
        blocked_faces=blocked_faces,
        n_blocked_faces=int(len(blocked_faces)),
        thermal_resistance_m2k_w=thermal_resistance,
        baffles=final_baffles,
        used_native=used_native,
    )
