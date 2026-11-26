"""Fast L0 & L1 Regression Gates for Sprint 2C + 2D Scale, CSR, Orientation, CHT, OpenFOAM, QEF & Expression Hardening."""

from __future__ import annotations

from pathlib import Path
import time
import numpy as np
import pytest

from backend.core.case_generator import CaseGenerator
from backend.geometry.models import SurfaceMesh
from backend.ipc.service import IPCService
from backend.meshing.invariants import assert_fvm_invariants, mesh_fingerprint
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CSRSequence,
    CurvatureSizingConfig,
    FarfieldPadding,
    MeshingConfig,
)
from backend.meshing.polymesh_io import (
    export_multiregion_openfoam_case,
    export_openfoam_polymesh,
    split_regions,
)
from backend.native_rust.expression_engine import ExpressionEngine, MAX_EXPR_LENGTH
from backend.native_rust.foam_reader import FoamReader
from backend.physics_modules.registry import ModuleRegistry
from tests.test_sprint1_audit_and_invariants import _make_box, _make_icosphere

pytestmark = pytest.mark.l0


def test_aud019_csr_unstructured_mesh_zero_list_allocs(tmp_path: Path) -> None:
    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        sphere, h0=0.25, domain_mode="external_fluid"
    )
    assert isinstance(mesh.faces, CSRSequence)
    assert isinstance(mesh.cells, CSRSequence)
    assert mesh.face_offsets is not None and mesh.face_nodes is not None
    assert mesh.cell_offsets is not None and mesh.cell_nodes is not None
    assert len(mesh.faces) == mesh.num_faces
    assert len(mesh.cells) == mesh.num_cells
    assert_fvm_invariants(mesh, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))

    poly_dir = export_openfoam_polymesh(mesh, tmp_path / "csr_case", write_binary=False)
    loaded = FoamReader.load_mesh(poly_dir)
    assert loaded.n_cells == mesh.num_cells
    assert loaded.n_faces == mesh.num_faces


def test_aud009_scale_invariance_1e_3_to_1e3() -> None:
    analytic_area_unit = 4.0 * np.pi * (0.5 ** 2)
    cell_counts: list[int] = []
    rel_area_errs: list[float] = []
    engine = MeshEngine(use_native=True, strict_native=True)

    for scale in (1e-3, 1.0, 1e3):
        sphere = _make_icosphere(radius=0.5 * scale, subdivisions=2)
        h0 = 0.20 * scale
        mesh = engine.mesh_hexcore(sphere, h0=h0, domain_mode="external_fluid", config=None)
        assert_fvm_invariants(mesh, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
        cell_counts.append(mesh.num_cells)
        ws, wc = mesh.boundaries["wall"]
        wall_area = float(np.linalg.norm(mesh.face_areas[ws : ws + wc], axis=1).sum())
        analytic_area = analytic_area_unit * (scale ** 2)
        rel_err = abs(wall_area - analytic_area) / analytic_area
        rel_area_errs.append(rel_err)

    # Same relative cell count & geometric accuracy across 6 orders of magnitude in scale
    assert max(cell_counts) - min(cell_counts) <= max(2, int(0.05 * min(cell_counts))), (
        f"Cell counts varied across scales: {cell_counts}"
    )
    assert max(rel_area_errs) < 0.08, f"Relative wall area error too high: {rel_area_errs}"
    assert max(rel_area_errs) - min(rel_area_errs) < 1e-4, (
        f"Relative wall area errors drifted across scales: {rel_area_errs}"
    )


def test_aud010_inverted_and_mixed_shell_orientation() -> None:
    sphere = _make_icosphere(radius=0.5, subdivisions=2)
    inverted = SurfaceMesh(vertices=sphere.vertices.copy(), faces=sphere.faces[:, [0, 2, 1]].copy())
    pad = FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0)
    cfg = MeshingConfig(
        domain_mode="external_fluid",
        farfield_padding=pad,
        sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=0.5),
    )
    eng = MeshEngine(use_native=True, strict_native=True)
    m_normal = eng.mesh_hexcore(sphere, h0=0.25, domain_mode="external_fluid", config=cfg)
    m_inv = eng.mesh_hexcore(inverted, h0=0.25, domain_mode="external_fluid", config=cfg)

    assert_fvm_invariants(m_inv, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
    assert mesh_fingerprint(m_inv) == mesh_fingerprint(m_normal)
    assert m_inv.metadata["shell_orientation"]["flipped_components"] == 1

    # Mixed orientation: two separated spheres, one normal and one inverted
    s_a = _make_icosphere(radius=0.35, center=(-1.0, 0.0, 0.0), subdivisions=2)
    s_b = _make_icosphere(radius=0.35, center=(1.0, 0.0, 0.0), subdivisions=2)
    mixed = SurfaceMesh(
        vertices=np.vstack([s_a.vertices, s_b.vertices]),
        faces=np.vstack([s_a.faces, s_b.faces[:, [0, 2, 1]] + len(s_a.vertices)]).astype(np.int32),
    )
    m_mixed = eng.mesh_hexcore(mixed, h0=0.25, domain_mode="external_fluid", config=cfg)
    assert_fvm_invariants(m_mixed, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
    assert m_mixed.metadata["shell_orientation"]["flipped_components"] == 1


def test_aud016_strict_native_default_without_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("CFD_STRICT_NATIVE", raising=False)
    eng_default = MeshEngine()
    assert eng_default.strict_native is True
    eng_py = MeshEngine(use_native=False)
    assert eng_py.strict_native is False
    eng_fb = MeshEngine(use_native=True, allow_python_fallback=True)
    assert eng_fb.strict_native is False


def test_aud014_split_regions_and_mapped_wall_export(tmp_path: Path) -> None:
    box = _make_box((1.0, 1.0, 1.0))
    cavity = _make_icosphere(radius=0.3, subdivisions=2)
    surf = SurfaceMesh(
        vertices=np.vstack([box.vertices, cavity.vertices]),
        faces=np.vstack([box.faces, cavity.faces + len(box.vertices)]).astype(np.int32),
    )
    cfg = MeshingConfig(
        domain_mode="cht_coupled",
        sizing=CurvatureSizingConfig(h0=0.2, h_min=0.1, h_max=0.4),
    )
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(surf, h0=0.2, config=cfg)

    regions = split_regions(mesh, region_names={0: "solid", 1: "fluid"})
    assert set(regions.keys()) == {"solid", "fluid"}

    for rname, rmesh in regions.items():
        assert_fvm_invariants(rmesh)
        other = "fluid" if rname == "solid" else "solid"
        mapped_patch = f"{rname}_to_{other}"
        assert mapped_patch in rmesh.boundaries
        spec = rmesh.metadata["patch_specs"][mapped_patch]
        assert spec["type"] == "mappedWall"
        assert spec["sampleMode"] == "nearestPatchFace"
        assert spec["sampleRegion"] == other
        assert spec["samplePatch"] == f"{other}_to_{rname}"
        # Compacted vertices only: every point index referenced by faces/cells
        used = np.unique(rmesh.face_nodes) if rmesh.face_nodes is not None else np.unique(np.concatenate(list(rmesh.faces)))
        assert len(used) == rmesh.num_points

    case_dir = tmp_path / "cht_case"
    export_multiregion_openfoam_case(mesh, case_dir, region_names={0: "solid", 1: "fluid"}, binary=False)
    reg_props = (case_dir / "constant" / "regionProperties").read_text(encoding="utf-8")
    assert "fluid" in reg_props and "solid" in reg_props

    for rname, other in (("solid", "fluid"), ("fluid", "solid")):
        bnd_txt = (case_dir / "constant" / rname / "polyMesh" / "boundary").read_text(encoding="utf-8")
        assert "mappedWall" in bnd_txt
        assert "nearestPatchFace" in bnd_txt
        assert f"sampleRegion    {other};" in bnd_txt or f"sampleRegion {other};" in bnd_txt


def test_aud022_openfoam_simplefoam_and_cht_case_templates(tmp_path: Path) -> None:
    reg = ModuleRegistry()
    gen = CaseGenerator(reg)
    sf_dir = gen.create_case(
        "sf_run",
        str(tmp_path),
        solver_name="simpleFoam",
        end_time=100,
        delta_t=1,
        write_interval=50,
    )
    ctrl = (sf_dir / "system" / "controlDict").read_text(encoding="utf-8")
    schemes = (sf_dir / "system" / "fvSchemes").read_text(encoding="utf-8")
    soln = (sf_dir / "system" / "fvSolution").read_text(encoding="utf-8")
    u_file = (sf_dir / "0" / "U").read_text(encoding="utf-8")
    assert "simpleFoam" in ctrl and "deltaT          1;" in ctrl
    assert "steadyState" in schemes
    assert "div((nuEff*dev2(T(grad(U)))))" in schemes
    assert "SIMPLE" in soln and "pRefCell" in soln
    assert (sf_dir / "constant" / "transportProperties").is_file()
    assert (sf_dir / "constant" / "turbulenceProperties").is_file()
    assert "uniform (1 0 0)" in u_file

    cht_dir = gen.create_case(
        "cht_run",
        str(tmp_path),
        solver_name="chtMultiRegionFoam",
        regions={"fluid": "fluid", "solid": "solid"},
    )
    assert (cht_dir / "constant" / "regionProperties").is_file()
    assert (cht_dir / "constant" / "fluid" / "thermophysicalProperties").is_file()
    assert (cht_dir / "constant" / "solid" / "thermophysicalProperties").is_file()
    assert (cht_dir / "system" / "fluid" / "fvSchemes").is_file()
    assert (cht_dir / "0" / "solid" / "T").is_file()


def test_aud035_expression_engine_let_chain_no_dos_and_ipc_missing_geo() -> None:
    lines = ["let x0 = x + x;"] + [f"let x{i} = x{i - 1} + x{i - 1};" for i in range(1, 25)] + ["x24"]
    expr = " ".join(lines)

    t0 = time.perf_counter()
    eng = ExpressionEngine(expr)
    dt = time.perf_counter() - t0
    assert dt < 0.25, f"Compiling 25 chained let statements took too long: {dt:.3f}s"

    val = eng.evaluate({"x": np.array([1.0, 2.0, -0.5])})
    assert np.allclose(val, np.array([1.0, 2.0, -0.5]) * (2 ** 25))

    with pytest.raises(ValueError, match="exceeds maximum"):
        ExpressionEngine(" + ".join(["x"] * (MAX_EXPR_LENGTH // 2 + 10)))

    with pytest.raises(ZeroDivisionError):
        ExpressionEngine("1.0 / x").evaluate({"x": 0.0})

    ipc = IPCService()
    with pytest.raises(KeyError, match="not found"):
        ipc.endpoint_mesh_generate({"geometry_id": "nonexistent_geo_123"})


def test_qef_sharp_edge_preservation_rotated_box() -> None:
    # Unit box rotated 22 degrees around Z -> cuts through grid diagonals cleanly
    box = _make_box((0.5, 0.5, 0.5))
    theta = np.radians(22.0)
    rz = np.array(
        [
            [np.cos(theta), -np.sin(theta), 0.0],
            [np.sin(theta), np.cos(theta), 0.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=np.float64,
    )
    rot_box = SurfaceMesh(vertices=box.vertices @ rz.T, faces=box.faces.copy())
    cfg = MeshingConfig(
        domain_mode="external_fluid",
        farfield_padding=FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0),
        sizing=CurvatureSizingConfig(h0=0.20, h_min=0.10, h_max=0.40),
    )
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        rot_box, h0=0.20, domain_mode="external_fluid", config=cfg
    )
    assert_fvm_invariants(mesh, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
    ws, wc = mesh.boundaries["wall"]
    wall_area = float(np.linalg.norm(mesh.face_areas[ws : ws + wc], axis=1).sum())
    assert abs(wall_area - 6.0) / 6.0 < 0.05, f"Rotated box wall area drifted: {wall_area}"


def test_python_monolith_line_budgets() -> None:
    meshing_dir = Path(__file__).resolve().parents[1] / "backend" / "meshing"
    checked = [
        "models.py",
        "_models_config.py",
        "_models_quality.py",
        "_models_mesh.py",
        "difvm_topology.py",
        "native_loader.py",
        "topology_wsrd.py",
        "topology_coupling.py",
        "topology_python.py",
        "mesh_engine.py",
        "_engine_zones.py",
        "_engine_python_fallback.py",
        "geometry_sdf.py",
        "_sdf_primitives.py",
        "_sdf_bvh.py",
        "_sdf_cad.py",
        "_sdf_sizing.py",
    ]
    too_large: dict[str, int] = {}
    for fname in checked:
        p = meshing_dir / fname
        assert p.is_file(), f"Missing decomposed module: {p}"
        n_lines = len(p.read_text(encoding="utf-8").splitlines())
        if n_lines >= 800:
            too_large[fname] = n_lines
    assert not too_large, f"Modules exceeded 800-line ceiling (AGENTS.md Rule 6): {too_large}"
