"""Fast L0 Verification Gate for Sprint 3: FeatureGraph, Weingarten, Strategy-C BL, faMesh, ZoneDetect, Linear Morton Octree, mmap MeshHandle, Subprocess IPC & ModuleSpec Registry."""

from __future__ import annotations

import ctypes
from pathlib import Path
import struct
import time
import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.ipc.async_jobs import JobStatus, SubprocessJobManager
from backend.meshing.bl_strategy_c import (
    bl_prep_thickness_field,
    generate_offset_bl_mesh,
    solve_strand_growth_rates,
)
from backend.meshing.difvm_topology import compute_difvm_topology_native, get_native_dll
from backend.meshing.famesh_geodesic import FiniteAreaMesh
from backend.meshing.feature_graph import extract_feature_graph
from backend.meshing.invariants import assert_fvm_invariants
from backend.meshing.weingarten import compute_ccbc_corrections, compute_weingarten_curvature
from backend.meshing.zone_detect import detect_zones_ray_blocked
from backend.native_rust.mesh_handle import MeshHandle, write_mesh_chunked
from backend.physics_modules.plugin_registry import (
    BoundaryConditionSpec,
    ModuleSpec,
    ParamSpec,
    Registry,
    get_bc_registry,
    get_material_law_registry,
    get_module_spec_registry,
    spec_to_json_schema,
    spec_to_ui_schema,
    verify_derivative_parity,
)
from backend.physics_modules.registry import ModuleRegistry
from tests.test_sprint1_audit_and_invariants import _make_box, _make_icosphere

pytestmark = pytest.mark.l0


def _subproc_square(x: int) -> int:
    return x * x


def _subproc_sleep_forever(sec: float = 30.0) -> int:
    time.sleep(sec)
    return 1


def _subproc_allocate_mb(mb: int) -> int:
    buf = bytearray(mb * 1024 * 1024)
    buf[0] = 1
    buf[-1] = 2
    time.sleep(0.6)
    return len(buf)


def test_sprint3_feature_graph_and_weingarten_curvature() -> None:
    sphere = _make_icosphere(radius=2.0, subdivisions=2)
    fg = extract_feature_graph(sphere.vertices, sphere.faces, h_min=0.05)
    assert len(fg.sharp_edges) == 0
    assert fg.patch_count == 1

    wf = compute_weingarten_curvature(sphere.vertices, sphere.faces, feature_graph=fg, h_min=0.05)
    # Sphere R=2 -> principal curvatures magnitude ~ 0.5 (sign depends on local normal convention)
    assert abs(float(np.median(np.abs(wf.k1))) - 0.5) < 0.08
    assert abs(float(np.median(np.abs(wf.k2))) - 0.5) < 0.08
    packed = wf.packed_20b()
    assert len(packed) == 20 * len(sphere.vertices)
    k1_0, k2_0, ex, ey, ez = struct.unpack("<5f", packed[:20])
    assert abs(abs(k1_0) - abs(float(wf.k1[0]))) < 1e-4
    assert abs(np.linalg.norm([ex, ey, ez]) - 1.0) < 1e-4

    # Cube with 6 planar faces must yield 6 smooth patches and 8 corners with zero crease leakage
    box = _make_box((0.5, 0.5, 0.5))
    fg_box = extract_feature_graph(box.vertices, box.faces, h_min=0.1)
    assert fg_box.patch_count == 6
    assert len(fg_box.feature_corners) == 8
    wf_box = compute_weingarten_curvature(box.vertices, box.faces, feature_graph=fg_box, h_min=0.1)
    assert float(np.max(np.abs(wf_box.k1))) < 1e-8

    ccbc = compute_ccbc_corrections(0.5, 0.5, np.array([1.0, 0.0, 0.0]), np.array([0.0, 10.0, 0.0]), rho=1.2, y1=1e-4, h_lat=0.02)
    assert ccbc.dp_dn != 0.0 and ccbc.y_eff != 1e-4


def test_sprint3_strategy_c_bl_and_famesh_geodesics() -> None:
    sphere = _make_icosphere(radius=0.5, subdivisions=2)

    def sphere_sdf(pts: np.ndarray) -> np.ndarray:
        p = np.atleast_2d(pts)
        return np.linalg.norm(p, axis=1) - 0.5

    delta, t_reach = bl_prep_thickness_field(
        sphere.vertices, sphere.faces, sphere_sdf, n_layers=3, y1=0.01, growth_rate=1.2
    )
    assert np.all(delta > 0.0) and np.all(t_reach > 0.0)
    g, y1_eff, bl_scale = solve_strand_growth_rates(np.array([0.05, 0.01]), y1=0.01, n_layers=4, delta_target=0.05)
    assert bl_scale[0] == pytest.approx(1.0, abs=1e-5)
    assert bl_scale[1] < 0.25 and g[1] == pytest.approx(1.0, abs=1e-5) and y1_eff[1] == pytest.approx(0.0025, abs=1e-5)

    bl_mesh = generate_offset_bl_mesh(sphere, h0=0.20, n_layers=2, y1=0.01, growth_rate=1.2, domain_mode="external_fluid")
    assert_fvm_invariants(bl_mesh, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
    assert "bl_scale" in bl_mesh.error_map
    assert bl_mesh.metadata.get("engine") == "bl_strategy_c"
    assert "bl_strand_set" in bl_mesh.metadata
    assert int(np.sum(bl_mesh.cell_types == 13) + np.sum(bl_mesh.cell_types == 12)) > 0

    fa = FiniteAreaMesh.from_unstructured_mesh(bl_mesh, patch_name="wall")
    assert len(fa.faces) > 0 and len(fa.edge_owners) > 0
    chord_dists = np.linalg.norm(
        fa.face_centres[fa.edge_neighbours] - fa.face_centres[fa.edge_owners],
        axis=1,
    )
    assert np.all(fa.geodesic_edge_lengths >= chord_dists - 1e-12)
    div_arr = fa.check_surface_divergence(np.ones(len(fa.faces), dtype=np.float64))
    assert float(np.max(np.abs(div_arr))) < 1e-10
    assert fa.geodesic_distance(0, min(5, len(fa.faces) - 1)) >= 0.0


def test_sprint3_ray_blocked_zone_detect_native() -> None:
    # Build 4x2x2 Cartesian mesh in [0, 2] x [0, 1] x [0, 1] with a zero-thickness baffle at x = 1.0
    nx, ny, nz = 4, 2, 2
    xs = np.linspace(0.0, 2.0, nx + 1)
    ys = np.linspace(0.0, 1.0, ny + 1)
    zs = np.linspace(0.0, 1.0, nz + 1)
    gx, gy, gz = np.meshgrid(xs, ys, zs, indexing="ij")
    pts = np.column_stack([gx.ravel(), gy.ravel(), gz.ravel()])

    def pid(i: int, j: int, k: int) -> int:
        return (i * (ny + 1) + j) * (nz + 1) + k

    cells = []
    for i in range(nx):
        for j in range(ny):
            for k in range(nz):
                cells.append(
                    np.array(
                        [
                            pid(i, j, k), pid(i + 1, j, k), pid(i + 1, j + 1, k), pid(i, j + 1, k),
                            pid(i, j, k + 1), pid(i + 1, j, k + 1), pid(i + 1, j + 1, k + 1), pid(i, j + 1, k + 1),
                        ],
                        dtype=np.int64,
                    )
                )
    mesh = compute_difvm_topology_native(pts, cells)

    baffle_verts = np.array([[1.0, -0.5, -0.5], [1.0, 1.5, -0.5], [1.0, 1.5, 1.5], [1.0, -0.5, 1.5]], dtype=np.float64)
    baffle_tris = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.int32)

    zres = detect_zones_ray_blocked(
        mesh,
        baffle_verts,
        baffle_tris,
        thin_wall_thickness_m=0.002,
        thermal_conductivity_w_mk=200.0,
        extract_twins=True,
    )
    assert zres.used_native is True
    assert zres.num_zones == 2
    assert zres.n_blocked_faces == 4
    assert zres.thermal_resistance_m2k_w == pytest.approx(0.002 / 200.0)
    assert_fvm_invariants(zres.mesh)


def test_sprint3_linear_morton_octree_lt_600_b_per_cell() -> None:
    dll = get_native_dll(strict_native=True)
    assert hasattr(dll, "cfd_morton_octree_build_stats")
    dll.cfd_morton_octree_build_stats.argtypes = [
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int64,
        ctypes.POINTER(ctypes.c_int32),
        ctypes.c_int64,
        ctypes.c_double,
        ctypes.c_int32,
        ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_uint64),
        ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_int32),
    ]
    dll.cfd_morton_octree_build_stats.restype = ctypes.c_int32

    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    pts = np.ascontiguousarray(sphere.vertices, dtype=np.float64)
    tris = np.ascontiguousarray(sphere.faces, dtype=np.int32)
    bbox = np.array([-2.0, -2.0, -2.0, 2.0, 2.0, 2.0], dtype=np.float64)

    n_leaves = ctypes.c_int64()
    n_cells = ctypes.c_int64()
    n_reg = ctypes.c_int64()
    n_gen = ctypes.c_int64()
    n_verts = ctypes.c_int64()
    peak_bytes = ctypes.c_uint64()
    build_s = ctypes.c_double()
    bal_ok = ctypes.c_int32()

    rc = dll.cfd_morton_octree_build_stats(
        pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
        len(pts),
        tris.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
        len(tris),
        0.125,
        2,
        bbox.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
        ctypes.byref(n_leaves),
        ctypes.byref(n_cells),
        ctypes.byref(n_reg),
        ctypes.byref(n_gen),
        ctypes.byref(n_verts),
        ctypes.byref(peak_bytes),
        ctypes.byref(build_s),
        ctypes.byref(bal_ok),
    )
    assert rc == 0
    assert bal_ok.value == 1
    assert n_cells.value > 10_000
    assert n_reg.value > n_gen.value
    b_per_cell = peak_bytes.value / float(n_cells.value)
    assert b_per_cell < 600.0, f"Morton octree exceeded 600 B/cell budget: {b_per_cell:.1f} B/cell"


def test_sprint3_mphys_mmap_mesh_handle(tmp_path: Path) -> None:
    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    from backend.meshing.mesh_engine import MeshEngine
    from backend.meshing.models import CurvatureSizingConfig, FarfieldPadding, MeshingConfig

    cfg = MeshingConfig(
        domain_mode="external_fluid",
        farfield_padding=FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0),
        sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=0.50),
    )
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(sphere, h0=0.25, config=cfg)
    out_file = tmp_path / "sphere.mphys"
    write_mesh_chunked(mesh, out_file, chunk_size=512)

    with MeshHandle.open(out_file) as handle:
        assert handle.open_time_ms < 50.0
        assert handle.n_cells == mesh.num_cells
        assert handle.n_faces == mesh.num_faces
        assert handle.num_chunks >= 2
        vols = handle.cell_volumes_view()
        assert np.shares_memory(vols, handle.cell_volumes_view())
        assert np.allclose(vols, mesh.cell_volumes)
        c0 = handle.read_chunk(0)
        assert len(c0["cell_volumes"]) == 512
        restored = handle.to_unstructured_mesh()
        assert restored.num_cells == mesh.num_cells and np.allclose(restored.points, mesh.points)


def test_sprint3_subprocess_ipc_cancel_and_ram_limit() -> None:
    mgr = SubprocessJobManager(poll_interval_s=0.05)
    try:
        rec_ok = mgr.create_job("square", _subproc_square, args=(12,))
        deadline = time.time() + 10.0
        while rec_ok.status in (JobStatus.QUEUED, JobStatus.RUNNING) and time.time() < deadline:
            time.sleep(0.05)
        assert rec_ok.status == JobStatus.COMPLETED and rec_ok.result == 144

        rec_cancel = mgr.create_job("sleep", _subproc_sleep_forever, args=(30.0,))
        time.sleep(0.25)
        assert mgr.cancel_job(rec_cancel.job_id) is True
        assert rec_cancel.status == JobStatus.CANCELLED and rec_cancel.result is None

        rec_oom = mgr.create_job("alloc", _subproc_allocate_mb, args=(120,), max_ram_mb=20.0)
        deadline = time.time() + 10.0
        while rec_oom.status in (JobStatus.QUEUED, JobStatus.RUNNING) and time.time() < deadline:
            time.sleep(0.05)
        assert rec_oom.status == JobStatus.FAILED
        assert "Memory limit exceeded" in (rec_oom.error or "")
    finally:
        mgr.close()


def test_sprint3_declarative_registry_and_derivative_parity() -> None:
    mod_specs = get_module_spec_registry()
    assert mod_specs.get("Viscous & Turbulence").id == "viscous"
    assert mod_specs.get("viscous").id == "viscous"
    ui_schema = spec_to_ui_schema(mod_specs.get("viscous"))
    json_schema = spec_to_json_schema(mod_specs.get("viscous"))
    assert ui_schema["module_id"] == "viscous"
    assert json_schema["$schema"].startswith("https://json-schema.org/")

    bc_reg = get_bc_registry()
    curv_bc = bc_reg.get("curvatureCorrectedSlipWall")
    assert isinstance(curv_bc, BoundaryConditionSpec) and curv_bc.needs_wall_curvature is True

    law_reg = get_material_law_registry()
    for law in law_reg.list_specs():
        verify_derivative_parity(law, T_samples=(300.0, 500.0), rtol=1e-5)

    custom = Registry[ModuleSpec]("custom_test")
    custom.register(
        ModuleSpec(
            id="custom_demo",
            display_name="Demo",
            category="Test",
            aliases=("Demo Alias",),
            params=(ParamSpec(name="alpha", type="float", default=1.0, unit_si="1"),),
        )
    )
    assert custom.get("Demo Alias").id == "custom_demo"

    legacy_reg = ModuleRegistry()
    assert legacy_reg.get_module("viscous") is legacy_reg.get_module("Viscous & Turbulence")
