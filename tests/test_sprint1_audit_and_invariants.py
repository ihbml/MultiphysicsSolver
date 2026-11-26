"""Fast L0 Verification Gate for Critical P0 Fixes, C-ABI Handshake & FVM Invariants."""

from __future__ import annotations

from pathlib import Path
import struct
import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.step_parser import STEPParser
from backend.meshing.difvm_topology import (
    compute_difvm_topology_native,
    generate_hexcore_native,
    get_native_dll,
)
from backend.meshing.invariants import assert_fvm_invariants, mesh_fingerprint
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import MeshingConfig
from backend.meshing.polymesh_io import export_openfoam_polymesh
from backend.native_rust.foam_reader import FoamReader
from tools.abi_gen import verify_abi_against_schema

pytestmark = pytest.mark.l0


def _make_icosphere(radius: float = 0.5, center: tuple[float, float, float] = (0.0, 0.0, 0.0), subdivisions: int = 2) -> SurfaceMesh:
    t = (1.0 + np.sqrt(5.0)) / 2.0
    verts = np.array(
        [
            [-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0],
            [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
            [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1],
        ],
        dtype=np.float64,
    )
    verts /= np.linalg.norm(verts, axis=1, keepdims=True)
    faces = np.array(
        [
            [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
            [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
            [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
            [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
        ],
        dtype=np.int32,
    )
    for _ in range(subdivisions):
        edge_mid: dict[tuple[int, int], int] = {}
        v_list = verts.tolist()
        new_faces = []
        for i0, i1, i2 in faces:
            mids = []
            for a, b in ((int(i0), int(i1)), (int(i1), int(i2)), (int(i2), int(i0))):
                key = (min(a, b), max(a, b))
                if key not in edge_mid:
                    m = 0.5 * ( np.array(v_list[a]) + np.array(v_list[b]) )
                    m /= np.linalg.norm(m)
                    edge_mid[key] = len(v_list)
                    v_list.append(m.tolist())
                mids.append(edge_mid[key])
            m01, m12, m20 = mids
            new_faces.extend(
                [[i0, m01, m20], [i1, m12, m01], [i2, m20, m12], [m01, m12, m20]]
            )
        verts = np.array(v_list, dtype=np.float64)
        faces = np.array(new_faces, dtype=np.int32)
    verts = verts * radius + np.asarray(center, dtype=np.float64)
    return SurfaceMesh(vertices=verts, faces=faces)


def _make_box(half_extents: tuple[float, float, float] = (1.0, 1.0, 1.0)) -> SurfaceMesh:
    hx, hy, hz = half_extents
    verts = np.array(
        [
            [-hx, -hy, -hz], [hx, -hy, -hz], [hx, hy, -hz], [-hx, hy, -hz],
            [-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz],
        ],
        dtype=np.float64,
    )
    faces = np.array(
        [
            [0, 2, 1], [0, 3, 2],
            [4, 5, 6], [4, 6, 7],
            [0, 1, 5], [0, 5, 4],
            [2, 3, 7], [2, 7, 6],
            [0, 4, 7], [0, 7, 3],
            [1, 2, 6], [1, 6, 5],
        ],
        dtype=np.int32,
    )
    return SurfaceMesh(vertices=verts, faces=faces)


def test_c_abi_schema_and_offset_handshake() -> None:
    res = verify_abi_against_schema(require_dll=True)
    assert res["abi_version"] == 2
    assert res["native_checked"] is True


def test_aud001_and_aud003_external_fluid_patches_and_shifted_bbox() -> None:
    sphere = _make_icosphere(radius=0.5, center=(0.0, 50.0, 100.0), subdivisions=2)
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        sphere, h0=0.25, domain_mode="external_fluid"
    )
    assert_fvm_invariants(
        mesh,
        expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"),
    )
    # Verify AUD-003: shifted domain bounds match [-5, 10] around (0, 50, 100), NOT scrambled
    pmin = mesh.points.min(axis=0)
    pmax = mesh.points.max(axis=0)
    assert np.allclose(pmin, [-5.5, 44.5, 94.5], atol=0.3)
    assert np.allclose(pmax, [10.5, 55.5, 105.5], atol=0.3)
    fp = mesh_fingerprint(mesh)
    assert fp["counts"].endswith(f"/{mesh.num_cells}")


def test_aud004_and_aud025_cht_winding_number_and_patch_naming() -> None:
    # 12-triangle coarse outer cube [-1,1]^3 (vol=8.0) + 320-triangle inner sphere r=0.3 (vol~0.113)
    box = _make_box((1.0, 1.0, 1.0))
    cavity = _make_icosphere(radius=0.3, subdivisions=2)
    combined_v = np.vstack([box.vertices, cavity.vertices])
    combined_f = np.vstack([box.faces, cavity.faces + len(box.vertices)]).astype(np.int32)
    surf = SurfaceMesh(vertices=combined_v, faces=combined_f)

    cfg = MeshingConfig(domain_mode="cht_coupled")
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(surf, h0=0.2, config=cfg)

    vol_reg0 = float(mesh.cell_volumes[mesh.cell_regions == 0].sum())
    vol_reg1 = float(mesh.cell_volumes[mesh.cell_regions == 1].sum())
    assert 0.05 < vol_reg1 < 0.22, f"Chamber volume {vol_reg1} should approximate 4/3*pi*0.3^3=0.113"
    assert 7.75 < vol_reg0 < 7.96, f"Solid volume {vol_reg0} should approximate 8 - 0.113 = 7.887"

    # AUD-014: Solid region (0) owns coupledWall_solid, Fluid chamber (1) owns coupledWall_fluid
    s_start, s_cnt = mesh.boundaries["coupledWall_solid"]
    f_start, f_cnt = mesh.boundaries["coupledWall_fluid"]
    assert s_cnt == f_cnt and s_cnt > 0
    assert np.all(mesh.cell_regions[mesh.owner[s_start : s_start + s_cnt]] == 0)
    assert np.all(mesh.cell_regions[mesh.owner[f_start : f_start + f_cnt]] == 1)


def test_aud005_step_unit_detection_beyond_64kb_and_whitespace(tmp_path: Path) -> None:
    padding = "/* " + ("X" * 80_000) + " */\n"
    stp = tmp_path / "meter_tail.stp"
    stp.write_text(
        "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n"
        + padding
        + "#999 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT( $ , .METRE. ) );\nENDSEC;\nEND-ISO-10303-21;\n",
        encoding="utf-8",
    )
    assert STEPParser.detect_units(stp) == "m"


def test_aud007_and_aud008_c_abi_bounds_and_domain_mode_precedence() -> None:
    sphere = _make_icosphere(radius=0.5, subdivisions=1)
    bad_faces = sphere.faces.copy()
    bad_faces[0, 2] = -4
    with pytest.raises(RuntimeError, match="triangle_vertex_index_out_of_bounds"):
        generate_hexcore_native(sphere.vertices, bad_faces, h0=0.25, domain_mode="solid_only")

    nan_pts = sphere.vertices.copy()
    nan_pts[0, 0] = np.nan
    with pytest.raises(RuntimeError, match="non_finite_vertex_coordinate"):
        generate_hexcore_native(nan_pts, sphere.faces, h0=0.25, domain_mode="solid_only")

    cube_pts = _make_box((0.5, 0.5, 0.5)).vertices
    with pytest.raises(RuntimeError):
        compute_difvm_topology_native(cube_pts, [np.arange(8, dtype=np.int64), np.array([], dtype=np.int64)])

    # AUD-008: Explicit domain_mode="solid_only" overrides MeshingConfig() default ("external_fluid")
    m_solid = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        sphere, h0=0.25, domain_mode="solid_only", config=MeshingConfig()
    )
    assert float(m_solid.cell_volumes.sum()) < 1.0
    assert set(m_solid.boundaries.keys()) == {"wall"}

    with pytest.raises(ValueError, match="Unsupported domain_mode"):
        MeshEngine().mesh_hexcore(sphere, h0=0.25, domain_mode="solid")


def test_aud015_openfoam_binary_preserves_leading_whitespace_bytes(tmp_path: Path) -> None:
    # Construct a float64 whose first little-endian byte is 0x20 (' ')
    raw_bytes = bytearray(struct.pack("<d", 1.5))
    raw_bytes[0] = 0x20
    val_0x20 = struct.unpack("<d", bytes(raw_bytes))[0]

    box = _make_box((0.5, 0.5, 0.5))
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        box, h0=0.5, domain_mode="solid_only"
    )
    mesh.points[0, 0] = val_0x20

    poly_dir = export_openfoam_polymesh(mesh, tmp_path / "bin_case", write_binary=True)
    raw_points_file = (poly_dir / "points").read_bytes()
    assert b'arch        "LSB;label=64;scalar=64";' in raw_points_file

    parsed_pts = FoamReader.parse_points(raw_points_file)
    assert parsed_pts[0, 0] == val_0x20


def test_aud026_cad_disk_cache_roundtrip(tmp_path: Path) -> None:
    from tests.e2e_mesher_suite.conftest import _load_surface_npz, _save_surface_npz

    box = _make_box((0.5, 0.25, 0.1))
    box.patches = {
        "inlet": np.array([0, 1], dtype=np.int32),
        "wall": np.arange(2, len(box.faces), dtype=np.int32),
    }
    box.metadata = {"units": "m", "source": "unit_test"}

    cache_file = tmp_path / "box_cache.npz"
    _save_surface_npz(cache_file, box)
    restored = _load_surface_npz(cache_file)
    assert restored is not None
    assert np.array_equal(restored.vertices, box.vertices)
    assert np.array_equal(restored.faces, box.faces)
    assert restored.normals is not None and box.normals is not None
    assert np.array_equal(restored.normals, box.normals)
    assert set(restored.patches.keys()) == {"inlet", "wall"}
    assert np.array_equal(restored.patches["inlet"], box.patches["inlet"])
    assert np.array_equal(restored.patches["wall"], box.patches["wall"])
    assert restored.metadata == box.metadata


def test_aud026_no_empty_boundary_masking_in_e2e_suite() -> None:
    suite_dir = Path(__file__).resolve().parent / "e2e_mesher_suite"
    offenders: list[str] = []
    for py_file in sorted(suite_dir.glob("*.py")):
        text = py_file.read_text(encoding="utf-8")
        if '"" in mesh.boundaries' in text or "'' in mesh.boundaries" in text:
            offenders.append(py_file.name)
    assert offenders == [], f"Empty-string boundary masking still present in: {offenders}"
