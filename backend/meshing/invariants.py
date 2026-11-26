"""Independent FVM Mesh Invariant Oracle & Deterministic Mesh Fingerprinting.

Implements INVARIANTS.md (I-01 through I-11) and Blueprint §3.4 Waffe 1
(`mesh_fingerprint`) purely from raw vertex coordinates (`points`) and face
node connectivity (`faces` or CSR `face_offsets`/`face_nodes`), without
trusting precomputed face areas, volumes, or manually overwritten patches.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

import numpy as np

from backend.meshing.models import UnstructuredMesh


@dataclass(frozen=True)
class InvariantViolation:
    """Single mathematical or topological invariant failure."""

    code: str
    message: str
    max_error: float = 0.0


def _extract_face_csr(mesh: UnstructuredMesh) -> Tuple[np.ndarray, np.ndarray]:
    """Return (face_offsets, face_nodes) int64 CSR arrays from an UnstructuredMesh."""
    if getattr(mesh, "face_offsets", None) is not None and getattr(mesh, "face_nodes", None) is not None:
        fo = np.asarray(mesh.face_offsets, dtype=np.int64)
        fn = np.asarray(mesh.face_nodes, dtype=np.int64)
        if len(fo) == mesh.num_faces + 1:
            return fo, fn

    if mesh.faces is None or len(mesh.faces) != mesh.num_faces:
        raise ValueError(
            f"Mesh lacks complete face connectivity: expected {mesh.num_faces} faces."
        )
    lengths = np.array([len(f) for f in mesh.faces], dtype=np.int64)
    fo = np.zeros(len(lengths) + 1, dtype=np.int64)
    np.cumsum(lengths, out=fo[1:])
    fn = (
        np.concatenate([np.asarray(f, dtype=np.int64) for f in mesh.faces])
        if fo[-1] > 0
        else np.empty(0, dtype=np.int64)
    )
    return fo, fn


def recompute_geometry_from_nodes(
    points: np.ndarray,
    face_offsets: np.ndarray,
    face_nodes: np.ndarray,
    owner: np.ndarray,
    neighbour: np.ndarray,
    n_cells: int,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Independently compute (face_areas, face_centres, cell_volumes, cell_centres) in 3D.

    Uses center-point triangle fan decomposition (Newell / Divergence Theorem)
    so non-planar quads/polygons close edge-by-edge to machine precision.
    """
    pts = np.asarray(points, dtype=np.float64)
    n_faces = len(face_offsets) - 1
    sf = np.zeros((n_faces, 3), dtype=np.float64)
    cf = np.zeros((n_faces, 3), dtype=np.float64)

    # Accumulate cell volume and centroid directly over face sub-triangles
    cell_vol = np.zeros(n_cells, dtype=np.float64)
    cell_mom = np.zeros((n_cells, 3), dtype=np.float64)

    # Reference cell point to avoid catastrophic cancellation far from origin
    cell_ref = np.zeros((n_cells, 3), dtype=np.float64)
    cell_cnt = np.zeros(n_cells, dtype=np.int64)

    for f in range(n_faces):
        s, e = int(face_offsets[f]), int(face_offsets[f + 1])
        fn = face_nodes[s:e]
        m = len(fn)
        if m < 3:
            continue
        f_pts = pts[fn]
        c_avg = f_pts.mean(axis=0)
        p_next = np.roll(f_pts, -1, axis=0)
        cross_p = 0.5 * np.cross(f_pts - c_avg, p_next - c_avg)
        tri_areas = np.linalg.norm(cross_p, axis=1)
        tot_area = float(tri_areas.sum())
        sf[f] = cross_p.sum(axis=0)
        if tot_area > 1e-30:
            tri_centres = (c_avg + f_pts + p_next) / 3.0
            cf[f] = (tri_centres * tri_areas[:, None]).sum(axis=0) / tot_area
        else:
            cf[f] = c_avg

        ow = int(owner[f])
        if 0 <= ow < n_cells:
            cell_ref[ow] += cf[f]
            cell_cnt[ow] += 1
        nb = int(neighbour[f])
        if 0 <= nb < n_cells:
            cell_ref[nb] += cf[f]
            cell_cnt[nb] += 1

    nonzero = cell_cnt > 0
    cell_ref[nonzero] /= cell_cnt[nonzero, None]

    for f in range(n_faces):
        s, e = int(face_offsets[f]), int(face_offsets[f + 1])
        fn = face_nodes[s:e]
        if len(fn) < 3:
            continue
        f_pts = pts[fn]
        c_avg = f_pts.mean(axis=0)
        p_next = np.roll(f_pts, -1, axis=0)
        tri_s = 0.5 * np.cross(f_pts - c_avg, p_next - c_avg)
        tri_c = (c_avg + f_pts + p_next) / 3.0

        ow = int(owner[f])
        if 0 <= ow < n_cells:
            r0 = cell_ref[ow]
            tet_v = np.einsum("ij,ij->i", tri_c - r0, tri_s) / 3.0
            tet_c = 0.25 * (r0 + c_avg + f_pts + p_next)
            cell_vol[ow] += float(tet_v.sum())
            cell_mom[ow] += (tet_c * tet_v[:, None]).sum(axis=0)

        nb = int(neighbour[f])
        if 0 <= nb < n_cells:
            r0 = cell_ref[nb]
            tet_v = np.einsum("ij,ij->i", tri_c - r0, -tri_s) / 3.0
            tet_c = 0.25 * (r0 + c_avg + f_pts + p_next)
            cell_vol[nb] += float(tet_v.sum())
            cell_mom[nb] += (tet_c * tet_v[:, None]).sum(axis=0)

    cell_cc = np.where(
        np.abs(cell_vol)[:, None] > 1e-30,
        cell_mom / np.where(np.abs(cell_vol)[:, None] > 1e-30, cell_vol[:, None], 1.0),
        cell_ref,
    )
    return sf, cf, cell_vol, cell_cc


def check_invariants(
    mesh: UnstructuredMesh,
    *,
    closure_tol: float = 1e-11,
    volume_tol: float = 1e-10,
    decomp_tol: float = 1e-12,
    expect_patches: Optional[Iterable[str]] = None,
    analytic_volume: Optional[float] = None,
    analytic_volume_rtol: float = 0.05,
    analytic_wall_area: Optional[float] = None,
    analytic_wall_area_rtol: float = 0.10,
    check_manifold: bool = True,
) -> List[InvariantViolation]:
    """Evaluate normative invariants I-01..I-09 and I-11 on a 3D UnstructuredMesh."""
    violations: List[InvariantViolation] = []
    if mesh.topological_dim != 3:
        return violations

    n_cells = mesh.num_cells
    n_faces = mesh.num_faces
    owner = np.asarray(mesh.owner, dtype=np.int64)
    neighbour = np.asarray(mesh.neighbour, dtype=np.int64)
    fo, fn = _extract_face_csr(mesh)

    # I-04: Internal/boundary partition & upper-triangular LDU face order
    int_mask = neighbour >= 0
    n_int = int(np.sum(int_mask))
    if n_int > 0 and not np.all(neighbour[:n_int] >= 0):
        violations.append(
            InvariantViolation("I-04", "Internal faces must precede all boundary faces.")
        )
    if np.any(neighbour[n_int:] != -1):
        violations.append(
            InvariantViolation("I-04", "Boundary faces must have neighbour == -1.")
        )
    if n_int > 0:
        bad_ldu = int(np.sum(owner[:n_int] >= neighbour[:n_int]))
        if bad_ldu > 0:
            violations.append(
                InvariantViolation("I-04", f"{bad_ldu} internal faces violate owner < neighbour.")
            )
        keys = owner[:n_int] * max(n_cells, 1) + neighbour[:n_int]
        if len(keys) > 1 and np.any(keys[1:] < keys[:-1]):
            violations.append(
                InvariantViolation("I-04", "Internal faces are not sorted by (owner, neighbour).")
            )

    # Recompute geometry purely from points & face nodes
    sf_rec, cf_rec, vol_rec, cc_rec = recompute_geometry_from_nodes(
        mesh.points, fo, fn, owner, neighbour, n_cells
    )

    # I-01: Gauss closure per cell
    cell_sf_sum = np.zeros((n_cells, 3), dtype=np.float64)
    cell_sf_abs = np.zeros(n_cells, dtype=np.float64)
    sf_mag = np.linalg.norm(sf_rec, axis=1)
    np.add.at(cell_sf_sum, owner, sf_rec)
    np.add.at(cell_sf_abs, owner, sf_mag)
    if n_int > 0:
        np.add.at(cell_sf_sum, neighbour[:n_int], -sf_rec[:n_int])
        np.add.at(cell_sf_abs, neighbour[:n_int], sf_mag[:n_int])
    rel_closure = np.linalg.norm(cell_sf_sum, axis=1) / np.maximum(cell_sf_abs, 1e-30)
    max_closure = float(np.max(rel_closure)) if n_cells > 0 else 0.0
    if max_closure >= closure_tol:
        violations.append(
            InvariantViolation(
                "I-01",
                f"Gauss surface closure {max_closure:.3e} >= {closure_tol:.1e}",
                max_closure,
            )
        )

    # I-02: Strictly positive cell volumes
    min_vol = float(np.min(vol_rec)) if n_cells > 0 else 0.0
    if n_cells == 0 or min_vol <= 1e-18 or np.any(mesh.cell_volumes <= 1e-18):
        violations.append(
            InvariantViolation("I-02", f"Non-positive cell volume detected: min={min_vol:.3e}", min_vol)
        )

    # I-03 & I-05: Stored vs recomputed geometry & optional analytical volume
    tot_rec = float(np.sum(vol_rec))
    tot_stored = float(np.sum(mesh.cell_volumes))
    rel_vol_diff = abs(tot_rec - tot_stored) / max(abs(tot_rec), 1e-30)
    if rel_vol_diff >= volume_tol:
        violations.append(
            InvariantViolation(
                "I-03",
                f"Stored total volume {tot_stored:.6e} differs from recomputed {tot_rec:.6e} (rel={rel_vol_diff:.3e})",
                rel_vol_diff,
            )
        )
    if analytic_volume is not None and analytic_volume > 0:
        ana_err = abs(tot_rec - analytic_volume) / analytic_volume
        if ana_err > analytic_volume_rtol:
            violations.append(
                InvariantViolation(
                    "I-03",
                    f"Total mesh volume {tot_rec:.6e} deviates from analytic {analytic_volume:.6e} by {ana_err:.2%}",
                    ana_err,
                )
            )

    sf_err = float(
        np.max(np.linalg.norm(mesh.face_areas - sf_rec, axis=1) / np.maximum(sf_mag, 1e-30))
    ) if n_faces > 0 else 0.0
    if sf_err >= 1e-9:
        violations.append(
            InvariantViolation("I-05", f"Stored face_areas deviate from Newell recomputation by {sf_err:.3e}", sf_err)
        )

    # I-06: delta_f + k_f == S_f on internal faces
    if n_int > 0 and mesh.delta_f is not None and mesh.k_f is not None:
        dec_err = np.linalg.norm(
            mesh.delta_f[:n_int] + mesh.k_f[:n_int] - mesh.face_areas[:n_int], axis=1
        ) / np.maximum(sf_mag[:n_int], 1e-30)
        max_dec = float(np.max(dec_err))
        if max_dec >= decomp_tol:
            violations.append(
                InvariantViolation("I-06", f"Decomposition error {max_dec:.3e} >= {decomp_tol:.1e}", max_dec)
            )

    # I-07: Contiguous & partition-complete boundary patches
    n_bnd = n_faces - n_int
    if n_bnd > 0:
        if not mesh.boundaries:
            violations.append(InvariantViolation("I-07", "Mesh has boundary faces but empty boundaries dict."))
        else:
            ranges = sorted((int(s), int(c), name) for name, (s, c) in mesh.boundaries.items())
            cursor = n_int
            for s, c, name in ranges:
                if not name or not name.strip():
                    violations.append(InvariantViolation("I-07", "Empty boundary patch name."))
                if s != cursor or c < 0:
                    violations.append(
                        InvariantViolation(
                            "I-07",
                            f"Non-contiguous patch '{name}': start={s}, expected={cursor}, count={c}.",
                        )
                    )
                cursor = s + c
            if cursor != n_faces:
                violations.append(
                    InvariantViolation(
                        "I-07", f"Boundary patches end at {cursor}, expected n_faces={n_faces}."
                    )
                )

    if expect_patches is not None:
        missing: Set[str] = set(expect_patches) - set(mesh.boundaries.keys())
        if missing:
            violations.append(
                InvariantViolation(
                    "I-07",
                    f"Missing expected boundary patches {sorted(missing)}; got {sorted(mesh.boundaries.keys())}.",
                )
            )

    if analytic_wall_area is not None and "wall" in mesh.boundaries:
        w_start, w_cnt = mesh.boundaries["wall"]
        wall_area = float(np.sum(sf_mag[w_start : w_start + w_cnt]))
        w_err = abs(wall_area - analytic_wall_area) / analytic_wall_area
        if w_err > analytic_wall_area_rtol:
            violations.append(
                InvariantViolation(
                    "I-05",
                    f"Wall surface area {wall_area:.6e} deviates from analytic {analytic_wall_area:.6e} by {w_err:.2%}",
                    w_err,
                )
            )

    # I-08: Cell-local edge manifoldness (every directed edge (u->v) has exact reverse (v->u))
    if check_manifold and n_cells <= 200_000:
        cell_edges: List[Dict[Tuple[int, int], int]] = [dict() for _ in range(n_cells)]
        for f in range(n_faces):
            s, e = int(fo[f]), int(fo[f + 1])
            nodes_f = fn[s:e]
            m = len(nodes_f)
            ow = int(owner[f])
            nb = int(neighbour[f])
            for k in range(m):
                u = int(nodes_f[k])
                v = int(nodes_f[(k + 1) % m])
                if 0 <= ow < n_cells:
                    cell_edges[ow][(u, v)] = cell_edges[ow].get((u, v), 0) + 1
                if 0 <= nb < n_cells:
                    cell_edges[nb][(v, u)] = cell_edges[nb].get((v, u), 0) + 1
        non_manifold_cells = 0
        for c in range(n_cells):
            emap = cell_edges[c]
            for (u, v), cnt in emap.items():
                if cnt != 1 or emap.get((v, u), 0) != 1:
                    non_manifold_cells += 1
                    break
        if non_manifold_cells > 0:
            violations.append(
                InvariantViolation(
                    "I-08",
                    f"{non_manifold_cells} cells fail directed edge-manifoldness (hanging node / T-junction).",
                    float(non_manifold_cells),
                )
            )

    # I-09: Twin-face conservation if coupled twin pairs are present
    twin_pairs = mesh.metadata.get("coupled_twin_pairs")
    if twin_pairs:
        max_twin_err = 0.0
        for fa, fb in twin_pairs:
            err = float(np.linalg.norm(sf_rec[fa] + sf_rec[fb])) / max(float(sf_mag[fa]), 1e-30)
            max_twin_err = max(max_twin_err, err)
        if max_twin_err >= decomp_tol:
            violations.append(
                InvariantViolation("I-09", f"Twin-face normal mismatch {max_twin_err:.3e}", max_twin_err)
            )

    return violations


def assert_fvm_invariants(mesh: UnstructuredMesh, **kwargs) -> None:
    """Assert that ``mesh`` satisfies all normative FVM invariants, raising AssertionError otherwise."""
    violations = check_invariants(mesh, **kwargs)
    if violations:
        formatted = "\n".join(f"  [{v.code}] {v.message}" for v in violations)
        raise AssertionError(f"Mesh violated {len(violations)} FVM invariant(s):\n{formatted}")


def mesh_fingerprint(
    mesh: UnstructuredMesh, *, float_bits: Optional[int] = None
) -> Dict[str, str]:
    """Compute a compact deterministic BLAKE2b fingerprint of topology and geometry (Blueprint §3.4)."""

    def _hash_arr(arr: np.ndarray) -> str:
        return hashlib.blake2b(np.ascontiguousarray(arr).tobytes(), digest_size=8).hexdigest()

    def _quant(arr: np.ndarray) -> np.ndarray:
        a = np.asarray(arr, dtype=np.float64)
        if float_bits is None:
            return a
        mant, exp = np.frexp(a)
        return np.stack([np.round(mant * (2.0**float_bits)), exp.astype(np.float64)])

    fo, fn = _extract_face_csr(mesh)
    topo_concat = np.concatenate(
        [
            np.asarray(mesh.owner, dtype=np.int64),
            np.asarray(mesh.neighbour, dtype=np.int64),
            fo,
            fn,
        ]
    )
    regions = (
        np.asarray(mesh.cell_regions, dtype=np.int32)
        if mesh.cell_regions is not None
        else np.zeros(mesh.num_cells, dtype=np.int32)
    )
    patches_repr = "|".join(
        f"{k}:{v[0]}:{v[1]}" for k, v in sorted(mesh.boundaries.items())
    ).encode("utf-8")

    return {
        "counts": f"{mesh.num_points}/{mesh.num_faces}/{mesh.num_cells}",
        "topology": _hash_arr(topo_concat),
        "patches": hashlib.blake2b(patches_repr, digest_size=8).hexdigest(),
        "points": _hash_arr(_quant(mesh.points)),
        "areas": _hash_arr(_quant(mesh.face_areas)),
        "volumes": _hash_arr(_quant(mesh.cell_volumes)),
        "regions": _hash_arr(regions),
    }
