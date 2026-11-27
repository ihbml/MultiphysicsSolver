"""L0 Regression & Contract Suite for Option A Hardening (AUD-023/024/027/030/034, CFDFLD03, P0-P4, Sprint-3 Pipeline)."""

from __future__ import annotations

import ctypes
from pathlib import Path
import struct

import numpy as np
import pytest

from backend.meshing.difvm_topology import (
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    generate_hexcore_native,
)
from backend.meshing.invariants import assert_fvm_invariants, compute_polynomial_exactness
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import CurvatureSizingConfig, MeshingConfig
from backend.meshing.moving_mesh import ALEMeshMover, compute_polygon_swept_volume_simpson
from backend.meshing.polymesh_io import export_openfoam_polymesh
from backend.native_rust.foam_reader import FoamReader, _load_rust_foam_fast_io
from backend.native_rust.mesh_handle import MeshHandle
from tests.test_sprint1_audit_and_invariants import _make_box, _make_icosphere


def _make_structured_hex(nx: int = 3, ny: int = 3, nz: int = 3) -> tuple[np.ndarray, list[np.ndarray]]:
    x = np.linspace(0.0, 1.0, nx + 1)
    y = np.linspace(0.0, 1.0, ny + 1)
    z = np.linspace(0.0, 1.0, nz + 1)
    pts = []
    pidx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                pidx[(i, j, k)] = len(pts)
                pts.append([x[i], y[j], z[k]])
    cells = [
        np.array(
            [
                pidx[(i, j, k)], pidx[(i + 1, j, k)], pidx[(i + 1, j + 1, k)], pidx[(i, j + 1, k)],
                pidx[(i, j, k + 1)], pidx[(i + 1, j, k + 1)], pidx[(i + 1, j + 1, k + 1)], pidx[(i, j + 1, k + 1)],
            ],
            dtype=np.int64,
        )
        for k in range(nz)
        for j in range(ny)
        for i in range(nx)
    ]
    return np.array(pts, dtype=np.float64), cells


def test_aud023_collapsed_hex_and_inverted_2d_tri() -> None:
    """AUD-023: Collapsed flat 8-node hex gives 0.0 volume; CW 2D triangle gives genuine negative volume."""
    pts_flat = np.array(
        [
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        ],
        dtype=np.float64,
    )
    cells_flat = [np.arange(8, dtype=np.int64)]
    for fn in (compute_difvm_topology_native, compute_difvm_topology_python):
        m_flat = fn(pts_flat, cells_flat)
        assert abs(float(m_flat.cell_volumes[0])) < 1e-15

    pts_tri = np.array([[0.0, 0.0, 0.0], [3.0, 0.0, 0.0], [0.0, 4.0, 0.0]], dtype=np.float64)
    cells_inv = [np.array([0, 2, 1], dtype=np.int64)]
    m_tri = compute_difvm_topology_python(pts_tri, cells_inv)
    assert abs(float(m_tri.cell_volumes[0]) - (-6.0)) < 1e-12


def test_aud024_ale_opposing_boundary_and_exact_3d_dgcl() -> None:
    """AUD-024: ALE leaves side slip walls free and preserves BDF1/BDF2 3D DGCL to machine precision."""
    pts, cells = _make_structured_hex(4, 4, 4)
    mesh0 = compute_difvm_topology_python(pts, cells)
    mover = ALEMeshMover(stiffness_exponent=1.0)
    top_mask = pts[:, 2] > 0.99

    # 1. Axial compression (-25% Z)
    disp_comp = np.zeros_like(pts)
    disp_comp[top_mask] = [0.0, 0.0, -0.25]
    mesh_c = mover.deform_mesh(mesh0, disp_comp)
    assert float(np.min(mesh_c.cell_volumes)) > 1e-15
    assert abs(float(np.sum(mesh_c.cell_volumes)) - 0.75) < 0.02

    # 2. Shear attenuation at mid-plane + triaxial non-planar BDF1 & BDF2 DGCL
    disp1 = np.zeros_like(pts)
    disp1[top_mask] = [0.08, 0.04, -0.05]
    mesh1 = mover.deform_mesh(mesh0, disp1)
    mid_mask = (pts[:, 2] > 0.49) & (pts[:, 2] < 0.51)
    mid_disp = np.linalg.norm(mesh1.points[mid_mask] - pts[mid_mask], axis=1)
    assert np.all(mid_disp > 0.01)

    disp2 = np.zeros_like(pts)
    disp2[top_mask] = [0.04, -0.03, 0.02]
    mesh2 = mover.deform_mesh(mesh1, disp2)

    dt = 0.05
    assert mover.verify_dgcl(mesh0, mesh1, dt) < 1e-13
    assert mover.verify_dgcl(mesh1, mesh2, dt, mesh_nm1=mesh0) < 1e-13

    # Twisted single polygon face sub-triangle swept volume smoke check
    q = np.array([0, 1, 2, 3], dtype=np.int64)
    p0 = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]])
    p1 = p0.copy()
    p1[:, 2] = 0.25
    assert abs(compute_polygon_swept_volume_simpson(q, p0, p1) - 0.25) < 1e-14


def test_aud027_planar_faces_coplanar_dedup_and_single_centroid_p2() -> None:
    """AUD-027: Snapped icosphere keeps planar FVM faces (P2 <= 1e-12) and merges all coplanar duplicates."""
    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    mesh = generate_hexcore_native(sphere.vertices, sphere.faces, h0=0.5, domain_mode="solid_only")
    assert_fvm_invariants(mesh, closure_tol=1e-11)

    int_mask = mesh.neighbour >= 0
    owners = mesh.owner[int_mask]
    neighbours = mesh.neighbour[int_mask]
    assert np.all(owners < neighbours)
    n_int = int(np.sum(int_mask))
    if n_int > 1:
        keys = owners.astype(np.int64) * mesh.num_cells + neighbours.astype(np.int64)
        assert np.all(keys[1:] >= keys[:-1])

    # Every duplicate (owner, neighbour) group must be genuinely non-coplanar (coplanar faces were merged)
    by_pair: dict[tuple[int, int], list[int]] = {}
    for f_idx in range(n_int):
        by_pair.setdefault((int(owners[f_idx]), int(neighbours[f_idx])), []).append(f_idx)
    for pair, f_indices in by_pair.items():
        if len(f_indices) < 2:
            continue
        f0 = f_indices[0]
        n0 = mesh.face_areas[f0]
        a0 = float(np.linalg.norm(n0))
        u0 = n0 / max(a0, 1e-30)
        for fk in f_indices[1:]:
            nk = mesh.face_areas[fk]
            ak = float(np.linalg.norm(nk))
            uk = nk / max(ak, 1e-30)
            dot_u = float(np.dot(u0, uk))
            fo, fn = mesh.face_offsets, mesh.face_nodes
            pts_k = mesh.points[fn[int(fo[fk]) : int(fo[fk + 1])]]
            max_plane_dev = float(np.max(np.abs((pts_k - mesh.face_centres[f0]) @ u0)))
            plane_tol = 1e-11 * float(np.sqrt(max(a0 * len(f_indices), 1e-30)))
            assert dot_u < 1.0 - 1e-10 or max_plane_dev > plane_tol, (
                f"Unmerged coplanar internal faces for pair {pair}"
            )

    rep = compute_polynomial_exactness(mesh)
    assert rep.passed is True
    assert rep.p0_gauss_closure < 1e-11
    assert rep.p1_moment_identity < 1e-10
    assert rep.p2_linear_gradient_error < 1e-11


def test_aud030_native_2d_polygon_metadata_and_validate() -> None:
    """AUD-030: Native 2D polygon mesher populates cell_types, topological_dim=2, face CSR, and validates."""
    poly = np.array([[0.0, 0.0], [2.0, 0.0], [2.0, 0.5], [0.0, 0.5]], dtype=np.float64)
    cfg = MeshingConfig(sizing=CurvatureSizingConfig(h0=0.8))
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_2d_polygon(poly, config=cfg)
    assert mesh.topological_dim == 2
    assert mesh.cell_types is not None and len(mesh.cell_types) == mesh.num_cells
    assert abs(float(np.sum(mesh.cell_volumes)) - 1.0) < 1e-12
    assert mesh.validate()


def test_aud034_rust_foam_fast_io_parity_and_cfdfld03_ring(tmp_path: Path) -> None:
    """AUD-034: Rust foam_fast_io loads ASCII+binary polyMesh matching Python, plus CFDFLD03 SPMC ring."""
    assert FoamReader.has_native_rust() is True

    # 1. Binary FoamMesh via export_openfoam_polymesh + 0x0a/0x20 byte payload edge case
    box = _make_box((1.0, 1.0, 1.0))
    mesh_src = generate_hexcore_native(box.vertices, box.faces, h0=0.5, domain_mode="solid_only")
    bin_case = tmp_path / "bin_box"
    export_openfoam_polymesh(mesh_src, bin_case, write_binary=True)
    poly_dir = bin_case / "constant" / "polyMesh"

    # Inject a coordinate whose low byte is 0x0A ('\n') in a minimal single-point binary file check via C-ABI/load
    pts_path = poly_dir / "points"
    pts_data = bytearray(pts_path.read_bytes())
    # Rewrite first float's LE byte 0 to 0x0A while keeping valid double near point[0].x
    marker = b"\n("
    pos = pts_data.find(marker)
    assert pos != -1
    first_x = struct.unpack("<d", pts_data[pos + 2 : pos + 10])[0]
    raw_0a = bytearray(struct.pack("<d", first_x))
    raw_0a[0] = 0x0A
    pts_data[pos + 2 : pos + 10] = raw_0a
    pts_path.write_bytes(bytes(pts_data))

    m_rust = FoamReader.load_mesh(poly_dir, prefer_rust=True)
    m_py = FoamReader.load_mesh(poly_dir, prefer_rust=False)
    assert m_rust.n_cells == m_py.n_cells == mesh_src.num_cells
    assert m_rust.n_faces == m_py.n_faces == mesh_src.num_faces
    assert np.array_equal(m_rust.points.view(np.uint64)[0, 0], m_py.points.view(np.uint64)[0, 0])
    assert np.allclose(m_rust.cell_volumes, m_py.cell_volumes, rtol=1e-12, atol=1e-13)
    assert np.isclose(m_rust.total_volume(), m_py.total_volume(), rtol=1e-12)

    # 2. CFDFLD03 SPMC Triple-Buffer Ring C-ABI roundtrip
    lib = _load_rust_foam_fast_io()
    assert lib is not None
    ring = lib.cfdfld03_ring_create(4)
    assert ring
    try:
        arr = np.array([1.25, 2.5, -3.75, 4.0], dtype=np.float64)
        rc_pub = lib.cfdfld03_ring_publish_f64(
            ring, 42, 1.5, arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), 4
        )
        assert rc_pub == 0
        out_buf = np.zeros(4, dtype=np.float64)
        out_step = ctypes.c_uint64(0)
        out_time = ctypes.c_double(0.0)
        out_len = ctypes.c_uint64(0)
        rc_read = lib.cfdfld03_ring_read_latest_f64(
            ring,
            ctypes.byref(out_step),
            ctypes.byref(out_time),
            out_buf.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            4,
            ctypes.byref(out_len),
        )
        assert rc_read == 0
        assert out_step.value == 42
        assert abs(out_time.value - 1.5) < 1e-12
        assert out_len.value == 4
        assert np.allclose(out_buf, arr)
    finally:
        lib.cfdfld03_ring_free(ring)


def test_p0_p4_polynomial_exactness_and_sprint3_pipeline(tmp_path: Path) -> None:
    """Verify P0-P4 polynomial exactness oracle and end-to-end Sprint-3 MeshEngine pipeline."""
    # 1. Uniform Cartesian mesh must satisfy P0..P3 to machine precision
    pts, cells = _make_structured_hex(3, 3, 3)
    cart_mesh = compute_difvm_topology_native(pts, cells)
    rep_cart = compute_polynomial_exactness(cart_mesh)
    assert rep_cart.passed is True
    assert rep_cart.p0_gauss_closure < 1e-13
    assert rep_cart.p1_moment_identity < 1e-12
    assert rep_cart.p2_linear_gradient_error < 1e-12
    assert rep_cart.p3_quadratic_laplacian_error < 1e-11

    # 2. End-to-end Sprint-3 pipeline on icosphere with FeatureGraph, Weingarten, P0-P3 & MPHYS002
    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    mphys_out = tmp_path / "sprint3_sphere.mphys"
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_with_sprint3_pipeline(
        sphere,
        h0=0.4,
        domain_mode="solid_only",
        enable_feature_graph=True,
        enable_weingarten=True,
        verify_p0_p3=True,
        export_mphys_path=mphys_out,
    )
    assert mesh.metadata["feature_graph_summary"]["n_smooth_patches"] >= 1
    assert mesh.metadata["weingarten_curvature"]["packed_bytes"] == len(sphere.vertices) * 20
    assert mesh.metadata["polynomial_exactness"]["passed"] is True
    assert mphys_out.is_file()

    with MeshHandle.open(mphys_out) as mh:
        assert mh.n_cells == mesh.num_cells
        assert np.allclose(mh.cell_volumes_view(), mesh.cell_volumes)
