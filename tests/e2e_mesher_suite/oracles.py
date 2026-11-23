"""Opaque-box mathematical, topological, HDF5, polyMesh, and subprocess oracles."""

from __future__ import annotations

import logging
from pathlib import Path
import subprocess
from typing import Any, Dict, List, Mapping, Optional, Sequence, Set, Tuple, Union

import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import connected_components

from backend.meshing.models import (
    CellType,
    MeshQualityReport,
    UnstructuredMesh,
    compute_cell_j_ens,
    compute_mesh_j_ens,
)
from backend.meshing.polymesh_io import export_openfoam_polymesh
from backend.native_rust.foam_reader import FoamMesh, FoamReader

logger = logging.getLogger(__name__)


def assert_unstructured_mesh_valid(
    mesh: UnstructuredMesh,
    require_positive_j_ens: bool = True,
    check_polyhedron_j_ens: bool = True,
) -> MeshQualityReport:
    """Validate geometric, topological, and Jacobian invariants of an UnstructuredMesh.

    Args:
        mesh: Mesh to validate.
        require_positive_j_ens: Whether to enforce ``j_ens > 0.0`` across checked cells.
        check_polyhedron_j_ens: When False, ``CellType.POLYHEDRON`` cells are excluded
            from the strict ``j_ens > 0.0`` check (finite check still applies).

    Returns:
        Computed ``MeshQualityReport`` for the validated mesh.
    """
    assert isinstance(mesh, UnstructuredMesh), f"Expected UnstructuredMesh, got {type(mesh)}"
    assert mesh.num_cells > 0, f"Mesh must have >0 cells, got {mesh.num_cells}"
    assert mesh.num_faces > 0, f"Mesh must have >0 faces, got {mesh.num_faces}"
    assert mesh.num_points > 0, f"Mesh must have >0 points, got {mesh.num_points}"

    for arr_name in (
        "points",
        "face_areas",
        "face_centres",
        "cell_centres",
        "cell_volumes",
        "delta_f",
        "k_f",
    ):
        arr = getattr(mesh, arr_name)
        assert np.all(np.isfinite(arr)), f"Non-finite values detected in mesh.{arr_name}"

    assert np.all(mesh.cell_volumes > 0.0), (
        f"Non-positive cell volume detected: min={float(np.min(mesh.cell_volumes)):.6e}"
    )

    is_valid_flag = mesh.validate()
    assert is_valid_flag is True, "mesh.validate() did not return True"

    report = mesh.compute_quality_report()
    assert report.is_valid, (
        f"MeshQualityReport.is_valid is False: min_vol={report.min_cell_volume}, "
        f"max_closure={report.max_closed_cell_divergence}, "
        f"inv_pyr={report.inverted_pyramids}"
    )
    assert report.inverted_pyramids == 0, (
        f"Inverted pyramids detected in report: {report.inverted_pyramids}"
    )

    j_ens = report.details.get("j_ens") if isinstance(report.details, dict) else None
    if j_ens is None or len(j_ens) != mesh.num_cells:
        j_ens, inv_pyr = compute_mesh_j_ens(mesh)
    else:
        j_ens = np.asarray(j_ens, dtype=np.float64)
        inv_pyr = int(report.inverted_pyramids)
    assert inv_pyr == 0, f"compute_mesh_j_ens reported {inv_pyr} inverted pyramids"
    assert len(j_ens) == mesh.num_cells, (
        f"j_ens length {len(j_ens)} != num_cells {mesh.num_cells}"
    )
    assert np.all(np.isfinite(j_ens)), "Non-finite values in compute_mesh_j_ens output"

    if require_positive_j_ens:
        if check_polyhedron_j_ens or mesh.cell_types is None:
            bad = np.where(j_ens <= 0.0)[0]
            assert len(bad) == 0, (
                f"Found {len(bad)} cells with J_ENS <= 0.0 (min={float(np.min(j_ens)):.6e})"
            )
        else:
            non_poly_mask = mesh.cell_types != int(CellType.POLYHEDRON)
            if np.any(non_poly_mask):
                bad = np.where(non_poly_mask & (j_ens <= 0.0))[0]
                assert len(bad) == 0, (
                    f"Found {len(bad)} non-polyhedron cells with J_ENS <= 0.0 "
                    f"(min={float(np.min(j_ens[non_poly_mask])):.6e})"
                )

    return report


def _is_orthogonal_box_8_cell(pts: np.ndarray) -> bool:
    """Return True if an 8-point cell is a right-angled cuboid regardless of vertex order."""
    if pts.shape != (8, 3):
        return False
    center = np.mean(pts, axis=0)
    radii = np.linalg.norm(pts - center, axis=1)
    max_r = float(np.max(radii))
    if max_r <= 1e-15:
        return False
    # In a right-angled cuboid, all 8 corner-to-centroid distances are identical.
    if float(np.max(radii) - np.min(radii)) > 1e-6 * max_r:
        return False
    # Order-independent cuboid check: from pts[0], find 3 mutually orthogonal
    # edge vectors u, v, w among the 7 nonzero displacement vectors d_i = pts[i] - pts[0]
    # whose combinations {u, v, w, u+v, u+w, v+w, u+v+w} match all 7 vectors.
    diffs = pts[1:] - pts[0]
    dists = np.linalg.norm(diffs, axis=1)
    if float(np.min(dists)) <= 1e-15:
        return False
    order = np.argsort(dists)
    tol_len = 1e-5 * max_r
    for i in range(7):
        u = diffs[order[i]]
        nu = float(dists[order[i]])
        for j in range(i + 1, 7):
            v = diffs[order[j]]
            nv = float(dists[order[j]])
            if abs(float(np.dot(u, v))) > 1e-6 * nu * nv:
                continue
            for k in range(j + 1, 7):
                w = diffs[order[k]]
                nw = float(dists[order[k]])
                if abs(float(np.dot(u, w))) > 1e-6 * nu * nw:
                    continue
                if abs(float(np.dot(v, w))) > 1e-6 * nv * nw:
                    continue
                expected = np.stack([u, v, w, u + v, u + w, v + w, u + v + w], axis=0)
                matched = np.zeros(7, dtype=bool)
                all_found = True
                for exp_vec in expected:
                    d_to_exp = np.linalg.norm(diffs - exp_vec[None, :], axis=1)
                    candidates = np.where((d_to_exp <= tol_len) & (~matched))[0]
                    if len(candidates) == 0:
                        all_found = False
                        break
                    matched[int(candidates[0])] = True
                if all_found and bool(np.all(matched)):
                    return True
    return False


def _probe_compute_cell_j_ens_not_stubbed() -> None:
    """Verify compute_cell_j_ens detects inverted cells and non-unit polyhedra."""
    inv_tet_pts = np.array(
        [
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.0, 0.0, 1.0],
            [0.0, 1.0, 0.0],
        ],
        dtype=np.float64,
    )
    j_inv_tet = compute_cell_j_ens(inv_tet_pts, [0, 1, 2, 3], CellType.TETRA)
    assert j_inv_tet < 0.0, (
        f"compute_cell_j_ens failed to detect inverted tetrahedron: J_ENS={j_inv_tet}"
    )

    inv_hex_pts = np.array(
        [
            [0.0, 0.0, 1.0],
            [1.0, 0.0, 1.0],
            [1.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.0],
        ],
        dtype=np.float64,
    )
    j_inv_hex = compute_cell_j_ens(inv_hex_pts, list(range(8)), CellType.HEXAHEDRON)
    assert j_inv_hex < 0.0, (
        f"compute_cell_j_ens failed to detect inverted hexahedron: J_ENS={j_inv_hex}"
    )

    # Unit cube with corner (1, 1, 1) chamfered off by plane x + y + z = 2.4
    chamfer_pts = np.array(
        [
            [0.0, 0.0, 0.0],  # 0
            [1.0, 0.0, 0.0],  # 1
            [1.0, 1.0, 0.0],  # 2
            [0.0, 1.0, 0.0],  # 3
            [0.0, 0.0, 1.0],  # 4
            [1.0, 0.0, 1.0],  # 5
            [0.0, 1.0, 1.0],  # 6
            [1.0, 1.0, 0.4],  # 7 (x=1, y=1, z=0.4)
            [1.0, 0.4, 1.0],  # 8 (x=1, y=0.4, z=1)
            [0.4, 1.0, 1.0],  # 9 (x=0.4, y=1, z=1)
        ],
        dtype=np.float64,
    )
    chamfer_faces = [
        [0, 3, 2, 1],       # bottom z=0
        [0, 1, 5, 4],       # front y=0
        [0, 4, 6, 3],       # left x=0
        [1, 2, 7, 8, 5],    # right x=1
        [2, 3, 6, 9, 7],    # back y=1
        [4, 5, 8, 9, 6],    # top z=1
        [7, 9, 8],          # chamfer cut plane
    ]
    j_chamfer = compute_cell_j_ens(
        chamfer_pts,
        list(range(len(chamfer_pts))),
        CellType.POLYHEDRON,
        cell_faces=chamfer_faces,
    )
    assert 0.0 < j_chamfer < 0.999, (
        f"compute_cell_j_ens returned dummy/invalid value {j_chamfer} for chamfered polyhedron"
    )


def assert_j_ens_geometric_not_dummy(
    mesh: UnstructuredMesh,
    check_poly_non_unit: bool = True,
) -> np.ndarray:
    """Verify J_ENS is geometrically computed in (0, 1] and not a dummy 1.0 stub.

    Args:
        mesh: Volumetric ``UnstructuredMesh`` to evaluate.
        check_poly_non_unit: If True and non-cubic polyhedra are present, assert
            their ``J_ENS`` values do not all collapse to literal ``1.0``.

    Returns:
        Per-cell ``J_ENS`` array of shape ``(mesh.num_cells,)``.
    """
    _probe_compute_cell_j_ens_not_stubbed()

    j_ens, inv_pyr = compute_mesh_j_ens(mesh)
    assert len(j_ens) == mesh.num_cells
    assert np.all(np.isfinite(j_ens)), "Non-finite J_ENS values encountered"
    assert inv_pyr == 0, f"Inverted pyramids found: {inv_pyr}"
    assert np.all(j_ens > 0.0), f"Non-positive J_ENS found: min={float(np.min(j_ens)):.6e}"
    assert np.all(j_ens <= 1.0 + 1e-12), (
        f"J_ENS exceeded 1.0 upper bound: max={float(np.max(j_ens)):.6e}"
    )

    if check_poly_non_unit and mesh.cell_types is not None:
        poly_indices = np.where(mesh.cell_types == int(CellType.POLYHEDRON))[0]
        if len(poly_indices) > 0:
            non_cubic_poly = [
                int(c_idx)
                for c_idx in poly_indices
                if len(mesh.cells[int(c_idx)]) != 8
                or not _is_orthogonal_box_8_cell(mesh.points[mesh.cells[int(c_idx)]])
            ]
            if non_cubic_poly:
                poly_vals = j_ens[non_cubic_poly]
                assert not np.allclose(poly_vals, 1.0, atol=1e-9), (
                    f"All {len(non_cubic_poly)} non-cubic POLYHEDRON cells returned "
                    f"dummy J_ENS=1.0 (min={float(np.min(poly_vals))}, "
                    f"max={float(np.max(poly_vals))})"
                )

    return j_ens


def assert_linear_field_exact(
    mesh: UnstructuredMesh,
    grad: Tuple[float, float, float] = (1.3, -0.7, 0.5),
    phi0: float = 2.5,
    tol: float = 1e-12,
) -> Dict[str, float]:
    """Verify linear-field exactness of face interpolation, Green-Gauss gradient, and Laplacian.

    Note:
        Green-Gauss gradient exactness using lumped ``phi(c_f) * S_f`` assumes
        planar polygonal faces where ``sum_t phi(c_t) * S_t == phi(c_f) * S_f``.

    Args:
        mesh: Mesh under test.
        grad: Constant linear gradient vector ``g``.
        phi0: Constant scalar offset.
        tol: Maximum allowed error across all three metrics.

    Returns:
        Dictionary with ``interp_max_err``, ``gg_grad_max_err``, and
        ``laplacian_max_err``.
    """
    g = np.asarray(grad, dtype=np.float64)
    assert g.shape == (3,)

    phi_c = phi0 + mesh.cell_centres @ g
    phi_f_exact = phi0 + mesh.face_centres @ g

    internal = mesh.neighbour >= 0
    if np.any(internal):
        owners_int = mesh.owner[internal]
        neighs_int = mesh.neighbour[internal]
        c_p = mesh.cell_centres[owners_int]
        c_n = mesh.cell_centres[neighs_int]
        c_f = mesh.face_centres[internal]
        s_f = mesh.face_areas[internal]

        d_f = c_n - c_p
        ds = np.sum(d_f * s_f, axis=1)
        assert np.all(np.abs(ds) > 1e-30), (
            f"Degenerate d_f . S_f detected on internal faces: min|ds|={float(np.min(np.abs(ds)))}"
        )

        beta_f = np.sum((c_f - c_p) * s_f, axis=1) / ds
        m_skew = c_f - (c_p + beta_f[:, None] * d_f)
        phi_f_interp = (
            phi_c[owners_int]
            + (beta_f[:, None] * d_f + m_skew) @ g
        )
        interp_max_err = float(
            np.max(np.abs((phi_f_interp - phi_c[owners_int]) - ((c_f - c_p) @ g)))
        )
    else:
        interp_max_err = 0.0

    # Scale-invariant surface area per cell
    cell_surface_area = np.zeros(mesh.num_cells, dtype=np.float64)
    face_area_mag = np.linalg.norm(mesh.face_areas, axis=1)
    np.add.at(cell_surface_area, mesh.owner, face_area_mag)
    if np.any(internal):
        np.add.at(cell_surface_area, mesh.neighbour[internal], face_area_mag[internal])
    area_scale = np.where(cell_surface_area > 1e-30, cell_surface_area, 1.0)

    # Verify scale-invariant Gauss face-area closure so broken closure is caught standalone
    closure_vec = np.zeros((mesh.num_cells, 3), dtype=np.float64)
    np.add.at(closure_vec, mesh.owner, mesh.face_areas)
    if np.any(internal):
        np.subtract.at(closure_vec, mesh.neighbour[internal], mesh.face_areas[internal])
    closure_rel_err = float(np.max(np.linalg.norm(closure_vec, axis=1) / area_scale))
    assert closure_rel_err <= tol, (
        f"Gauss face-area closure error {closure_rel_err:.3e} > tol {tol:.3e}"
    )

    # Green-Gauss cell gradient using cell-relative face displacements (c_f - c_c) @ g
    # to eliminate O(phi0 / h) floating-point cancellation on fine/mm/um-scale meshes.
    gg_num = np.zeros((mesh.num_cells, 3), dtype=np.float64)
    owner_dphi = ((mesh.face_centres - mesh.cell_centres[mesh.owner]) @ g)[:, None] * mesh.face_areas
    np.add.at(gg_num, mesh.owner, owner_dphi)
    if np.any(internal):
        neigh_int = mesh.neighbour[internal]
        neigh_dphi = (
            (mesh.face_centres[internal] - mesh.cell_centres[neigh_int]) @ g
        )[:, None] * mesh.face_areas[internal]
        np.subtract.at(gg_num, neigh_int, neigh_dphi)
    gg_grad = gg_num / mesh.cell_volumes[:, None]
    gg_grad_max_err = float(np.max(np.linalg.norm(gg_grad - g[None, :], axis=1)))

    # Verify non-orthogonal face decomposition delta_f + k_f == S_f on internal faces
    # (or boundary faces with non-zero k_f when n_int == 0)
    face_mag_scale = np.where(face_area_mag > 1e-30, face_area_mag, 1.0)
    faces_to_check = (
        internal
        if np.any(internal)
        else np.any(np.abs(mesh.k_f) > 1e-12, axis=1)
    )
    if np.any(faces_to_check):
        decomp_diff = np.linalg.norm(
            (mesh.delta_f[faces_to_check] + mesh.k_f[faces_to_check])
            - mesh.face_areas[faces_to_check],
            axis=1,
        )
        decomp_rel_err = float(np.max(decomp_diff / face_mag_scale[faces_to_check]))
        assert decomp_rel_err <= tol, (
            f"Face decomposition delta_f + k_f != S_f relative error "
            f"{decomp_rel_err:.3e} > tol {tol:.3e}"
        )

    # Laplacian / diffusive flux conservation: 1/V_c * sum sigma_cf (alpha_f (phi_N - phi_P) + k_f . g) == 0
    lap_div = np.zeros(mesh.num_cells, dtype=np.float64)
    if np.any(internal):
        owners_int = mesh.owner[internal]
        neighs_int = mesh.neighbour[internal]
        d_f = mesh.cell_centres[neighs_int] - mesh.cell_centres[owners_int]
        s_f = mesh.face_areas[internal]
        ds = np.sum(d_f * s_f, axis=1)
        alpha_f = np.sum(s_f * s_f, axis=1) / ds
        expected_delta_f = alpha_f[:, None] * d_f
        delta_f_err = float(
            np.max(
                np.linalg.norm(mesh.delta_f[internal] - expected_delta_f, axis=1)
                / face_mag_scale[internal]
            )
        )
        assert delta_f_err <= tol, (
            f"Internal delta_f orthogonal vector mismatch {delta_f_err:.3e} > tol {tol:.3e}"
        )
        flux_int = alpha_f * (d_f @ g) + (
            mesh.k_f[internal] @ g
        )
        np.add.at(lap_div, owners_int, flux_int)
        np.subtract.at(lap_div, neighs_int, flux_int)

    bnd = ~internal
    if np.any(bnd):
        flux_bnd = mesh.face_areas[bnd] @ g
        np.add.at(lap_div, mesh.owner[bnd], flux_bnd)

    # Scale-invariant normalization by total cell surface area * ||g||
    # (avoids O(1/h^2) floating-point cancellation blowup when dividing by V_c).
    grad_scale = max(float(np.linalg.norm(g)), 1e-12)
    flux_scale = np.where(
        cell_surface_area > 1e-30,
        cell_surface_area * grad_scale,
        mesh.cell_volumes * grad_scale,
    )
    laplacian_max_err = float(np.max(np.abs(lap_div) / flux_scale))

    assert interp_max_err <= tol, (
        f"Linear face interpolation error {interp_max_err:.3e} > tol {tol:.3e}"
    )
    assert gg_grad_max_err <= tol, (
        f"Green-Gauss gradient error {gg_grad_max_err:.3e} > tol {tol:.3e}"
    )
    assert laplacian_max_err <= tol, (
        f"Laplacian flux conservation error {laplacian_max_err:.3e} > tol {tol:.3e}"
    )

    return {
        "interp_max_err": interp_max_err,
        "gg_grad_max_err": gg_grad_max_err,
        "laplacian_max_err": laplacian_max_err,
    }


def count_surface_components_scipy(
    vertices: np.ndarray,
    faces: np.ndarray,
) -> int:
    """Count connected components of a triangulated surface using SciPy csgraph.

    Only vertices referenced by ``faces`` participate in the component count so
    that unreferenced vertices do not create spurious singleton components.

    Args:
        vertices: Vertex coordinates array of shape ``(V, 3)``.
        faces: Triangle vertex index array of shape ``(F, 3)``.

    Returns:
        Number of connected surface components.
    """
    verts = np.asarray(vertices)
    tris = np.asarray(faces, dtype=np.int64)
    if verts.ndim != 2 or verts.shape[1] != 3 or len(verts) == 0:
        raise ValueError(f"Expected non-empty vertices of shape (V, 3), got {verts.shape}")
    if tris.ndim != 2 or tris.shape[1] != 3 or len(tris) == 0:
        raise ValueError(f"Expected non-empty faces of shape (F, 3), got {tris.shape}")

    min_idx = int(np.min(tris))
    max_idx = int(np.max(tris))
    if min_idx < 0 or max_idx >= len(verts):
        raise ValueError(
            f"Face vertex indices out of bounds [0, {len(verts)}): min={min_idx}, max={max_idx}"
        )

    used_verts, inverse = np.unique(tris.ravel(), return_inverse=True)
    remapped = inverse.reshape(-1, 3)
    n_used = len(used_verts)

    row = np.concatenate(
        [remapped[:, 0], remapped[:, 1], remapped[:, 2], remapped[:, 1], remapped[:, 2], remapped[:, 0]]
    )
    col = np.concatenate(
        [remapped[:, 1], remapped[:, 2], remapped[:, 0], remapped[:, 0], remapped[:, 1], remapped[:, 2]]
    )
    data = np.ones(len(row), dtype=np.int8)
    adj = csr_matrix((data, (row, col)), shape=(n_used, n_used))
    n_components = int(connected_components(adj, directed=False, return_labels=False))
    return n_components


def _attr_scalar_int(attrs: Any, key: str, default: int = 0) -> int:
    if key not in attrs:
        return default
    return int(np.asarray(attrs[key]).reshape(-1)[0])


def _attr_scalar_str(attrs: Any, key: str, default: str = "") -> str:
    if key not in attrs:
        return default
    val = np.asarray(attrs[key]).reshape(-1)[0]
    if isinstance(val, (bytes, np.bytes_)):
        return bytes(val).decode("utf-8", errors="replace")
    return str(val)


def _decode_semicolon_names(dataset: Any) -> List[str]:
    raw = dataset[()]
    if isinstance(raw, np.ndarray):
        raw = raw.reshape(-1)[0]
    if isinstance(raw, (bytes, np.bytes_)):
        text = bytes(raw).decode("utf-8", errors="replace")
    else:
        text = str(raw)
    return [part for part in text.split(";") if part]


def read_fluent_msh_h5_summary(
    h5_path: Union[str, Path],
    chunk_size: int = 1_000_000,
) -> Dict[str, Any]:
    """Read structural summary of an Ansys Fluent ``.msh.h5`` file in bounded chunks.

    Handles both mixed sections containing ``cell-types`` (``elementType == 0``)
    and homogeneous sections without ``cell-types`` (``elementType != 0``, as in
    ``FLTG 1.1.msh.h5`` sections 4..6), reads ``/special/boundary_layer_flags``
    from the HDF5 root, and computes global node bounding boxes via chunked slicing.

    Args:
        h5_path: Path to the Fluent ``.msh.h5`` file.
        chunk_size: Maximum number of rows read per HDF5 slice.

    Returns:
        Dictionary with mesh counts, cell/face zone topologies, ``ctype_counts``,
        ``boundary_layer_flags`` summary, and ``bbox_min`` / ``bbox_max``.
    """
    import h5py

    path = Path(h5_path)
    if not path.is_file():
        raise FileNotFoundError(f"Fluent HDF5 file not found: {path}")
    if chunk_size <= 0:
        raise ValueError(f"chunk_size must be positive, got {chunk_size}")

    with h5py.File(path, "r") as handle:
        mesh_grp = handle["meshes/1"]
        cell_count = _attr_scalar_int(mesh_grp.attrs, "cellCount")
        face_count = _attr_scalar_int(mesh_grp.attrs, "faceCount")
        node_count = _attr_scalar_int(mesh_grp.attrs, "nodeCount")
        edge_count = _attr_scalar_int(mesh_grp.attrs, "edgeCount")
        dimension = _attr_scalar_int(mesh_grp.attrs, "dimension")
        units = _attr_scalar_str(mesh_grp.attrs, "units")
        version = _attr_scalar_int(mesh_grp.attrs, "version")

        # Cells zoneTopology
        cz_grp = mesh_grp["cells/zoneTopology"]
        cell_zones = {
            "nZones": _attr_scalar_int(cz_grp.attrs, "nZones"),
            "zone_names": _decode_semicolon_names(cz_grp["name"]),
            "ids": [int(x) for x in cz_grp["id"][()]],
            "minId": [int(x) for x in cz_grp["minId"][()]],
            "maxId": [int(x) for x in cz_grp["maxId"][()]],
            "cellType": [int(x) for x in cz_grp["cellType"][()]],
        }

        # Cells ctype (chunked for elementType==0; attr range for homogeneous elementType!=0)
        ctype_counts: Dict[int, int] = {}
        ctype_grp = mesh_grp["cells/ctype"]
        for sec_key in sorted(
            ctype_grp.keys(),
            key=lambda k: (0, int(k)) if k.isdigit() else (1, k),
        ):
            sec = ctype_grp[sec_key]
            elem_type = _attr_scalar_int(sec.attrs, "elementType", default=0)
            if "cell-types" in sec:
                ds = sec["cell-types"]
                total_len = int(ds.shape[0])
                for start in range(0, total_len, chunk_size):
                    chunk = ds[start : min(start + chunk_size, total_len)]
                    vals, counts = np.unique(chunk, return_counts=True)
                    for val, cnt in zip(vals, counts):
                        key_int = int(val)
                        ctype_counts[key_int] = ctype_counts.get(key_int, 0) + int(cnt)
            else:
                min_id = _attr_scalar_int(sec.attrs, "minId")
                max_id = _attr_scalar_int(sec.attrs, "maxId")
                sec_count = (max_id - min_id + 1) if max_id >= min_id and min_id > 0 else 0
                ctype_counts[elem_type] = ctype_counts.get(elem_type, 0) + int(sec_count)

        assert sum(ctype_counts.values()) == cell_count, (
            f"Sum of ctype counts {sum(ctype_counts.values())} != cellCount {cell_count} in {path}"
        )

        # Faces zoneTopology
        fz_grp = mesh_grp["faces/zoneTopology"]
        face_zones = {
            "nZones": _attr_scalar_int(fz_grp.attrs, "nZones"),
            "zone_names": _decode_semicolon_names(fz_grp["name"]),
            "ids": [int(x) for x in fz_grp["id"][()]],
            "zoneType": [int(x) for x in fz_grp["zoneType"][()]],
            "faceType": [int(x) for x in fz_grp["faceType"][()]],
            "shadowZoneId": [int(x) for x in fz_grp["shadowZoneId"][()]],
            "minId": [int(x) for x in fz_grp["minId"][()]],
            "maxId": [int(x) for x in fz_grp["maxId"][()]],
        }

        # Root /special/boundary_layer_flags
        bl_summary: Dict[str, Any] = {"present": False, "cells": {}, "faces": {}}
        if "special" in handle and "boundary_layer_flags" in handle["special"]:
            bl_root = handle["special/boundary_layer_flags"]
            bl_summary["present"] = True
            for sub_name in ("cells", "faces"):
                if sub_name not in bl_root:
                    continue
                sub_grp = bl_root[sub_name]
                topo_ids: List[int] = (
                    [int(x) for x in sub_grp["topology"][()]] if "topology" in sub_grp else []
                )
                zone_stats: Dict[str, Dict[str, int]] = {}
                total_nonzero = 0
                for item_key in sub_grp.keys():
                    if item_key == "topology":
                        continue
                    item = sub_grp[item_key]
                    if "data" not in item:
                        continue
                    ds = item["data"]
                    n_rows = int(ds.shape[0])
                    nz = 0
                    for start in range(0, n_rows, chunk_size):
                        chunk = ds[start : min(start + chunk_size, n_rows)]
                        nz += int(np.count_nonzero(chunk))
                    zone_stats[item_key] = {"count": n_rows, "nonzero": nz}
                    total_nonzero += nz
                bl_summary[sub_name] = {
                    "topology_zone_ids": topo_ids,
                    "zones": zone_stats,
                    "total_nonzero": total_nonzero,
                }

        # Global bounding box over chunked node coordinate datasets
        coords_grp = mesh_grp["nodes/coords"]
        bbox_min = np.full(3, np.inf, dtype=np.float64)
        bbox_max = np.full(3, -np.inf, dtype=np.float64)
        nodes_seen = 0
        for coord_key in sorted(
            coords_grp.keys(),
            key=lambda k: (0, int(k)) if k.isdigit() else (1, k),
        ):
            ds = coords_grp[coord_key]
            n_rows = int(ds.shape[0])
            for start in range(0, n_rows, chunk_size):
                chunk = np.asarray(
                    ds[start : min(start + chunk_size, n_rows), :], dtype=np.float64
                )
                if len(chunk) > 0:
                    bbox_min = np.minimum(bbox_min, np.min(chunk, axis=0))
                    bbox_max = np.maximum(bbox_max, np.max(chunk, axis=0))
                    nodes_seen += len(chunk)

        assert nodes_seen == node_count, (
            f"Node count mismatch while scanning coords: {nodes_seen} != {node_count}"
        )

    return {
        "path": str(path),
        "cellCount": cell_count,
        "faceCount": face_count,
        "nodeCount": node_count,
        "edgeCount": edge_count,
        "dimension": dimension,
        "units": units,
        "version": version,
        "cell_zones": cell_zones,
        "face_zones": face_zones,
        "ctype_counts": ctype_counts,
        "boundary_layer_flags": bl_summary,
        "bbox_min": bbox_min.tolist(),
        "bbox_max": bbox_max.tolist(),
    }


def run_subprocess_bounded(
    cmd: Sequence[str],
    timeout_s: float = 120.0,
    cwd: Optional[Union[str, Path]] = None,
    env: Optional[Mapping[str, str]] = None,
) -> subprocess.CompletedProcess[str]:
    """Execute a subprocess with a strict wall-clock timeout and captured text output.

    Args:
        cmd: Command sequence to execute.
        timeout_s: Hard timeout in seconds.
        cwd: Optional working directory.
        env: Optional environment mapping.

    Returns:
        ``subprocess.CompletedProcess[str]`` from the finished process.

    Raises:
        AssertionError: If the subprocess exceeds ``timeout_s``.
    """
    cmd_list = [str(part) for part in cmd]
    try:
        return subprocess.run(
            cmd_list,
            cwd=str(cwd) if cwd is not None else None,
            env=dict(env) if env is not None else None,
            capture_output=True,
            text=True,
            stdin=subprocess.DEVNULL,
            timeout=timeout_s,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise AssertionError(
            f"Subprocess timed out after {timeout_s:.1f}s: {cmd_list}\n"
            f"stdout={exc.stdout!r}\nstderr={exc.stderr!r}"
        ) from exc


def assert_polymesh_roundtrip(
    mesh: UnstructuredMesh,
    case_dir: Union[str, Path],
    region_name: Optional[str] = None,
    tol: float = 1e-9,
) -> FoamMesh:
    """Export an UnstructuredMesh to OpenFOAM polyMesh, read it back, and verify fidelity.

    Args:
        mesh: Source ``UnstructuredMesh`` (must have explicit ``faces`` connectivity).
        case_dir: Target OpenFOAM case directory.
        region_name: Optional multi-region subdirectory name.
        tol: Floating-point tolerance for coordinates and volumes.

    Returns:
        Reloaded ``FoamMesh`` instance.
    """
    poly_dir = export_openfoam_polymesh(mesh, case_dir, region_name=region_name)
    foam_mesh = FoamReader.load_mesh(poly_dir)

    assert foam_mesh.n_cells == mesh.num_cells, (
        f"polyMesh cell count mismatch: {foam_mesh.n_cells} != {mesh.num_cells}"
    )
    assert foam_mesh.n_points == mesh.num_points, (
        f"polyMesh point count mismatch: {foam_mesh.n_points} != {mesh.num_points}"
    )
    assert foam_mesh.n_faces == mesh.num_faces, (
        f"polyMesh face count mismatch: {foam_mesh.n_faces} != {mesh.num_faces}"
    )
    assert foam_mesh.n_internal_faces == mesh.num_internal_faces, (
        f"polyMesh internal face count mismatch: "
        f"{foam_mesh.n_internal_faces} != {mesh.num_internal_faces}"
    )

    assert np.allclose(foam_mesh.points, mesh.points, atol=tol, rtol=tol), (
        "polyMesh points roundtrip mismatch"
    )
    assert np.array_equal(foam_mesh.owner, mesh.owner), "polyMesh owner array mismatch"
    assert np.array_equal(
        foam_mesh.neighbour, mesh.neighbour[: mesh.num_internal_faces]
    ), "polyMesh neighbour array mismatch"

    expected_tot_vol = float(np.sum(mesh.cell_volumes))
    assert np.isclose(foam_mesh.total_volume(), expected_tot_vol, rtol=1e-7, atol=tol), (
        f"polyMesh total volume mismatch: {foam_mesh.total_volume()} != {expected_tot_vol}"
    )
    assert np.allclose(foam_mesh.cell_volumes, mesh.cell_volumes, rtol=1e-7, atol=tol), (
        "polyMesh per-cell volumes roundtrip mismatch"
    )

    actual_boundaries = {
        patch.name: (int(patch.start_face), int(patch.n_faces))
        for patch in foam_mesh.boundaries
    }
    expected_boundaries = (
        dict(mesh.boundaries)
        if mesh.boundaries
        else (
            {"wall": (mesh.num_internal_faces, mesh.num_boundary_faces)}
            if mesh.num_boundary_faces > 0
            else {}
        )
    )
    assert actual_boundaries == expected_boundaries, (
        f"polyMesh boundary patches mismatch: {actual_boundaries} != {expected_boundaries}"
    )

    # Validate non-overlapping partition coverage of [n_internal_faces, n_faces)
    n_int = int(foam_mesh.n_internal_faces)
    n_tot = int(foam_mesh.n_faces)
    covered_faces: Set[int] = set()
    total_patch_faces = 0
    for patch in foam_mesh.boundaries:
        p_name = str(patch.name)
        assert p_name.strip() != "", "Empty boundary patch name in polyMesh"
        sf = int(patch.start_face)
        nf = int(patch.n_faces)
        assert nf >= 0, f"Negative n_faces={nf} on boundary patch '{p_name}'"
        assert sf >= n_int, (
            f"Patch '{p_name}' start_face {sf} < n_internal_faces {n_int}"
        )
        assert sf + nf <= n_tot, (
            f"Patch '{p_name}' range [{sf}, {sf + nf}) exceeds n_faces {n_tot}"
        )
        patch_range = set(range(sf, sf + nf))
        overlap = covered_faces & patch_range
        assert not overlap, (
            f"Overlapping boundary face indices on patch '{p_name}': {sorted(overlap)[:5]}"
        )
        covered_faces |= patch_range
        total_patch_faces += nf

    expected_bnd_faces = n_tot - n_int
    assert total_patch_faces == expected_bnd_faces and len(covered_faces) == expected_bnd_faces, (
        f"Boundary patches cover {len(covered_faces)} faces (sum={total_patch_faces}), "
        f"expected exact partition of {expected_bnd_faces} boundary faces [{n_int}, {n_tot})"
    )

    # Verify face_offsets / face_nodes round-trip fidelity against mesh.faces
    if mesh.faces is not None:
        assert len(foam_mesh.face_offsets) == mesh.num_faces + 1, (
            f"face_offsets length {len(foam_mesh.face_offsets)} != {mesh.num_faces + 1}"
        )
        expected_offsets = np.zeros(mesh.num_faces + 1, dtype=np.int64)
        expected_nodes_list: List[int] = []
        for f_idx, f_nodes in enumerate(mesh.faces):
            expected_offsets[f_idx] = len(expected_nodes_list)
            expected_nodes_list.extend(int(v) for v in f_nodes)
        expected_offsets[mesh.num_faces] = len(expected_nodes_list)
        assert np.array_equal(foam_mesh.face_offsets, expected_offsets), (
            "polyMesh face_offsets roundtrip mismatch"
        )
        assert np.array_equal(
            foam_mesh.face_nodes, np.asarray(expected_nodes_list, dtype=np.int64)
        ), "polyMesh face_nodes roundtrip mismatch"

    return foam_mesh


def assert_cht_1to1_conformal(
    mesh: UnstructuredMesh,
    expected_regions: Optional[int] = None,
    tol: float = 1e-10,
) -> None:
    """Verify 1:1 conformal twin-face CHT interface invariants on a multi-region mesh.

    Args:
        mesh: Multi-region ``UnstructuredMesh`` with extracted CHT baffles.
        expected_regions: Optional exact number of distinct ``cell_regions`` expected.
        tol: Geometric comparison tolerance for collocated centres and opposite normals.
    """
    assert mesh.is_multiregion, "CHT mesh must report is_multiregion=True"
    assert mesh.cell_regions is not None, "CHT mesh must have cell_regions"

    unique_regions = np.unique(mesh.cell_regions)
    if expected_regions is not None:
        assert len(unique_regions) == expected_regions, (
            f"Expected {expected_regions} CHT regions, found {len(unique_regions)}: {unique_regions}"
        )
    else:
        assert len(unique_regions) >= 2, (
            f"Expected >= 2 CHT regions, found {len(unique_regions)}"
        )

    assert len(mesh.baffles) > 0, "CHT mesh must contain non-empty baffles"

    verified_coupled_faces: Set[int] = set()
    verified_pair_count = 0

    for idx, baffle in enumerate(mesh.baffles):
        twin_pairs = baffle.get("twin_pairs")
        assert twin_pairs and len(twin_pairs) > 0, (
            f"Baffle[{idx}] ({baffle.get('name')}) missing non-empty 'twin_pairs'"
        )
        reg_a_expected = baffle.get("region_a")
        reg_b_expected = baffle.get("region_b")

        for fa, fb in twin_pairs:
            fa_i = int(fa)
            fb_i = int(fb)
            assert 0 <= fa_i < mesh.num_faces and 0 <= fb_i < mesh.num_faces, (
                f"Twin pair ({fa_i}, {fb_i}) out of face bounds [0, {mesh.num_faces})"
            )
            assert fa_i != fb_i, f"Degenerate self-twin face {fa_i}"
            assert mesh.neighbour[fa_i] == -1 and mesh.neighbour[fb_i] == -1, (
                f"Twin faces ({fa_i}, {fb_i}) must be boundary faces (neighbour == -1), "
                f"got ({mesh.neighbour[fa_i]}, {mesh.neighbour[fb_i]})"
            )

            reg_a = int(mesh.cell_regions[mesh.owner[fa_i]])
            reg_b = int(mesh.cell_regions[mesh.owner[fb_i]])
            assert reg_a != reg_b, (
                f"Twin faces ({fa_i}, {fb_i}) belong to same region {reg_a}"
            )
            if reg_a_expected is not None and reg_b_expected is not None:
                assert reg_a == int(reg_a_expected) and reg_b == int(reg_b_expected), (
                    f"Twin faces ({fa_i}, {fb_i}) regions ({reg_a}, {reg_b}) != "
                    f"expected ({reg_a_expected}, {reg_b_expected})"
                )

            area_a = float(np.linalg.norm(mesh.face_areas[fa_i]))
            area_b = float(np.linalg.norm(mesh.face_areas[fb_i]))
            assert area_a > 1e-30 and area_b > 1e-30, (
                f"Degenerate zero-area twin face pair ({fa_i}, {fb_i}): "
                f"||S_A||={area_a:.3e}, ||S_B||={area_b:.3e}"
            )
            assert np.allclose(mesh.face_areas[fb_i], -mesh.face_areas[fa_i], atol=tol, rtol=tol), (
                f"Twin faces ({fa_i}, {fb_i}) violate S_B == -S_A: "
                f"{mesh.face_areas[fa_i]} vs {mesh.face_areas[fb_i]}"
            )
            assert np.allclose(
                mesh.face_centres[fa_i], mesh.face_centres[fb_i], atol=tol, rtol=tol
            ), (
                f"Twin faces ({fa_i}, {fb_i}) centres not collocated: "
                f"{mesh.face_centres[fa_i]} vs {mesh.face_centres[fb_i]}"
            )

            if mesh.faces is not None:
                nodes_a = {int(v) for v in mesh.faces[fa_i]}
                nodes_b = {int(v) for v in mesh.faces[fb_i]}
                assert nodes_a == nodes_b, (
                    f"Twin faces ({fa_i}, {fb_i}) node sets differ: {nodes_a} vs {nodes_b}"
                )

            assert fa_i not in verified_coupled_faces and fb_i not in verified_coupled_faces, (
                f"Duplicate twin face index encountered in ({fa_i}, {fb_i})"
            )
            verified_coupled_faces.add(fa_i)
            verified_coupled_faces.add(fb_i)
            verified_pair_count += 1

    # Verify zero unmatched/orphan coupledWall/mappedWall faces in mesh.boundaries
    patch_types = mesh.metadata.get("patch_types", {})
    coupled_boundary_faces: Set[int] = set()
    for patch_name, (start_f, count_f) in mesh.boundaries.items():
        ptype = str(patch_types.get(patch_name, ""))
        p_low = patch_name.lower()
        if (
            "coupled" in p_low
            or "mapped" in p_low
            or ptype in ("coupledWall", "mappedWall")
        ):
            for f_idx in range(int(start_f), int(start_f) + int(count_f)):
                coupled_boundary_faces.add(f_idx)

    orphan_faces = coupled_boundary_faces - verified_coupled_faces
    assert not orphan_faces, (
        f"Found {len(orphan_faces)} unmatched/orphan coupled boundary faces"
    )
    missing_from_patches = verified_coupled_faces - coupled_boundary_faces
    assert not missing_from_patches, (
        f"Found {len(missing_from_patches)} twin faces missing from coupled boundaries"
    )
    assert len(verified_coupled_faces) == 2 * verified_pair_count
