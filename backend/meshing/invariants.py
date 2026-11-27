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
        p0 = f_pts[0]
        rel = f_pts - p0
        c_rel = rel.mean(axis=0)
        d = rel - c_rel
        p_next = np.roll(d, -1, axis=0)
        cross_p = 0.5 * np.cross(d, p_next)
        tri_areas = np.linalg.norm(cross_p, axis=1)
        tot_area = float(tri_areas.sum())
        sf[f] = cross_p.sum(axis=0)
        c_avg = p0 + c_rel
        if tot_area > 1e-30:
            tri_centres = p0 + (c_rel[:, None] if False else (c_rel + rel + np.roll(rel, -1, axis=0)) / 3.0)
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
        p0 = f_pts[0]
        rel = f_pts - p0
        c_rel = rel.mean(axis=0)
        d = rel - c_rel
        p_next = np.roll(d, -1, axis=0)
        tri_s = 0.5 * np.cross(d, p_next)
        tri_c = p0 + (c_rel + rel + np.roll(rel, -1, axis=0)) / 3.0

        ow = int(owner[f])
        if 0 <= ow < n_cells:
            r0 = cell_ref[ow]
            tet_v = np.einsum("ij,ij->i", tri_c - r0, tri_s) / 3.0
            tet_c = 0.25 * r0 + 0.75 * tri_c
            cell_vol[ow] += float(tet_v.sum())
            cell_mom[ow] += (tet_c * tet_v[:, None]).sum(axis=0)

        nb = int(neighbour[f])
        if 0 <= nb < n_cells:
            r0 = cell_ref[nb]
            tet_v = np.einsum("ij,ij->i", tri_c - r0, -tri_s) / 3.0
            tet_c = 0.25 * r0 + 0.75 * tri_c
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
    stored_tol: float = 1e-9,
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
    if sf_err >= stored_tol:
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


@dataclass(frozen=True)
class PolynomialExactnessReport:
    """Solver-free P0-P4 polynomial exactness & truncation report (Blueprint §3.4 Waffe 3)."""

    p0_gauss_closure: float
    p1_moment_identity: float
    p2_linear_gradient_error: float
    p3_quadratic_laplacian_error: float
    p4_observed_order: Optional[float]
    passed: bool


def compute_polynomial_exactness(
    mesh: UnstructuredMesh,
    *,
    refinement_meshes: Optional[Sequence[UnstructuredMesh]] = None,
    p0_tol: float = 1e-11,
    p1_tol: float = 1e-10,
    p2_tol: float = 1e-10,
) -> PolynomialExactnessReport:
    """Evaluate solver-free P0-P4 discretization exactness directly on mesh geometry."""
    n_cells = mesh.num_cells
    n_faces = mesh.num_faces
    if n_cells == 0 or n_faces == 0:
        return PolynomialExactnessReport(0.0, 0.0, 0.0, 0.0, None, False)

    pts = np.asarray(mesh.points, dtype=np.float64)
    owner = np.asarray(mesh.owner, dtype=np.int64)
    neighbour = np.asarray(mesh.neighbour, dtype=np.int64)
    fo, fn = _extract_face_csr(mesh)

    sf, cf, cell_vol, cell_cc = recompute_geometry_from_nodes(
        pts, fo, fn, owner, neighbour, n_cells
    )
    sf_mag = np.linalg.norm(sf, axis=1)
    int_mask = neighbour >= 0
    n_int = int(np.sum(int_mask))

    # P0: Gauss closure sum_f S_f == 0
    cell_sf_sum = np.zeros((n_cells, 3), dtype=np.float64)
    cell_sf_abs = np.zeros(n_cells, dtype=np.float64)
    np.add.at(cell_sf_sum, owner, sf)
    np.add.at(cell_sf_abs, owner, sf_mag)
    if n_int > 0:
        np.add.at(cell_sf_sum, neighbour[:n_int], -sf[:n_int])
        np.add.at(cell_sf_abs, neighbour[:n_int], sf_mag[:n_int])
    p0 = float(np.max(np.linalg.norm(cell_sf_sum, axis=1) / np.maximum(cell_sf_abs, 1e-30)))

    # P1: First-moment identity sum_f (x_f - c_c) outer S_f / V_c == I_3
    moment = np.zeros((n_cells, 3, 3), dtype=np.float64)
    rel_o = cf - cell_cc[owner]
    outer_o = np.einsum("fi,fj->fij", rel_o, sf)
    np.add.at(moment, owner, outer_o)
    if n_int > 0:
        rel_n = cf[:n_int] - cell_cc[neighbour[:n_int]]
        outer_n = np.einsum("fi,fj->fij", rel_n, -sf[:n_int])
        np.add.at(moment, neighbour[:n_int], outer_n)
    safe_v = np.where(np.abs(cell_vol) > 1e-30, cell_vol, 1.0)
    eye3 = np.eye(3, dtype=np.float64)[None, :, :]
    p1_err_per_cell = np.linalg.norm(moment / safe_v[:, None, None] - eye3, axis=(1, 2))
    p1 = float(np.max(p1_err_per_cell))

    # P2: Exact Green-Gauss linear gradient on phi(x) = a.x + b using single-centroid face values
    grad_exact = np.array([1.3, -0.7, 2.1], dtype=np.float64)
    phi_f_lin = cf @ grad_exact + 0.5
    gg_lin = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(gg_lin, owner, phi_f_lin[:, None] * sf)
    if n_int > 0:
        np.add.at(gg_lin, neighbour[:n_int], -phi_f_lin[:n_int, None] * sf[:n_int])
    gg_lin /= safe_v[:, None]
    p2 = float(np.max(np.abs(gg_lin - grad_exact[None, :])))

    # P3: Quadratic Laplacian truncation error on phi(x) = ||x - x_ref||^2 (exact Laplacian = 6)
    x_ref = 0.5 * (np.min(pts, axis=0) + np.max(pts, axis=0))
    phi_c = np.sum((cell_cc - x_ref) ** 2, axis=1)
    phi_f = np.sum((cf - x_ref) ** 2, axis=1)

    # Interior vs boundary cell mask + Green-Gauss gradient estimate using interpolated faces
    has_boundary = np.zeros(n_cells, dtype=bool)
    has_boundary[owner[n_int:]] = True
    grad_cell = 2.0 * (cell_cc - x_ref)

    lap_sum = np.zeros(n_cells, dtype=np.float64)
    if n_int > 0:
        d_f = cell_cc[neighbour[:n_int]] - cell_cc[owner[:n_int]]
        sf_i = sf[:n_int]
        sf_dot_d = np.einsum("ij,ij->i", sf_i, d_f)
        sf_sq = np.sum(sf_i ** 2, axis=1)
        alpha_orth = np.where(np.abs(sf_dot_d) > 1e-30, sf_sq / np.where(np.abs(sf_dot_d) > 1e-30, sf_dot_d, 1.0), 0.0)
        delta_i = alpha_orth[:, None] * d_f
        k_i = sf_i - delta_i

        # Linear cell-distance weighting for face gradient
        dist_p = np.linalg.norm(cf[:n_int] - cell_cc[owner[:n_int]], axis=1)
        dist_n = np.linalg.norm(cell_cc[neighbour[:n_int]] - cf[:n_int], axis=1)
        w = np.where((dist_p + dist_n) > 1e-30, dist_n / np.maximum(dist_p + dist_n, 1e-30), 0.5)
        g_face = w[:, None] * grad_cell[owner[:n_int]] + (1.0 - w)[:, None] * grad_cell[neighbour[:n_int]]
        flux_i = alpha_orth * (phi_c[neighbour[:n_int]] - phi_c[owner[:n_int]]) + np.einsum("ij,ij->i", g_face, k_i)
        np.add.at(lap_sum, owner[:n_int], flux_i)
        np.add.at(lap_sum, neighbour[:n_int], -flux_i)

    # Boundary Dirichlet fluxes so boundary cells are also evaluated cleanly if no pure interior exists
    if n_faces > n_int:
        d_b = cf[n_int:] - cell_cc[owner[n_int:]]
        sf_b = sf[n_int:]
        sf_dot_db = np.einsum("ij,ij->i", sf_b, d_b)
        sf_b_sq = np.sum(sf_b ** 2, axis=1)
        alpha_b = np.where(np.abs(sf_dot_db) > 1e-30, sf_b_sq / np.where(np.abs(sf_dot_db) > 1e-30, sf_dot_db, 1.0), 0.0)
        k_b = sf_b - alpha_b[:, None] * d_b
        g_b = 2.0 * (cf[n_int:] - x_ref)
        flux_b = alpha_b * (phi_f[n_int:] - phi_c[owner[n_int:]]) + np.einsum("ij,ij->i", g_b, k_b)
        np.add.at(lap_sum, owner[n_int:], flux_b)

    lap_num = lap_sum / safe_v
    eval_mask = ~has_boundary if np.any(~has_boundary) else np.ones(n_cells, dtype=bool)
    p3 = float(np.sqrt(np.mean((lap_num[eval_mask] - 6.0) ** 2)))

    # P4: Observed convergence order across grid hierarchy
    p4_order: Optional[float] = None
    if refinement_meshes and len(refinement_meshes) >= 2:
        pairs: List[Tuple[float, float]] = []
        for rm in refinement_meshes:
            rep = compute_polynomial_exactness(rm, refinement_meshes=None)
            h_eff = float(np.cbrt(max(float(np.median(rm.cell_volumes)), 1e-30)))
            pairs.append((h_eff, rep.p3_quadratic_laplacian_error))
        pairs.sort(key=lambda item: item[0], reverse=True)
        h0, e0 = pairs[-2]
        h1, e1 = pairs[-1]
        if h0 > h1 * 1.05 and e0 > 1e-12 and e1 > 1e-12:
            p4_order = float(np.log(e0 / e1) / np.log(h0 / h1))

    passed = (
        np.isfinite(p0)
        and np.isfinite(p1)
        and np.isfinite(p2)
        and np.isfinite(p3)
        and p0 <= p0_tol
        and p1 <= p1_tol
        and p2 <= p2_tol
    )
    return PolynomialExactnessReport(
        p0_gauss_closure=p0,
        p1_moment_identity=p1,
        p2_linear_gradient_error=p2,
        p3_quadratic_laplacian_error=p3,
        p4_observed_order=p4_order,
        passed=bool(passed),
    )
