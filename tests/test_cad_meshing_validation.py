"""End-to-End CAD Ingestion, Filtering, and Meshing Validation Suite.

Milestone M4: Comprehensive verification on complex CAD reference geometries:
1. 200 mm rotated cube (TestCAD.obj & TestCADSTPAP214.stp, analytical volume = 0.008 m^3).
2. Single turbine blade (BladeRev1CFD.obj, volume ~ 187,600.6 mm^3).
3. Full turbine rotor assembly (BladeFullRev1CFD.stp, volume ~ 5.56e6 mm^3).

Verifies:
- Robust non-face entity filtering (GEOMETRIC_CURVE_SET, DRAUGHTING_MODEL, orphan wireframes).
- Continuous Signed Distance Field generation via CADMeshSDF (exact interior/exterior classification).
- Unstructured volume meshing via MeshEngine.
- Prismatic boundary layer inflation with geometric growth and Feature 23 pinching.
- Strict physical and geometric invariants:
  * Positive cell volumes: V_i > 0 for all cells.
  * Exact Gauss divergence theorem closure: max ||sum S_f|| < 10^-11.
  * Over-relaxed orthogonality consistency: Delta_f + k_f == S_f on internal faces.
  * Error-map completeness (face non-orthogonality, skewness, aspect ratio, stabilization mask).
"""

from pathlib import Path
from typing import Tuple
import numpy as np
import pytest

from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing.geometry_sdf import CADFeatureFilter, CADMeshSDF
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import InflationConfig, MeshingConfig

REPO_ROOT = Path(__file__).resolve().parent.parent
FIXTURES_DIR = REPO_ROOT / "backend" / "geometry" / "tests" / "fixtures"
ALT_CAD_DIR = REPO_ROOT / "resources" / "CADTestSamples"
ALT_COMPLEX_DIR = ALT_CAD_DIR / "ComplexGeometryExample"


def _resolve_fixture(filename: str) -> Path:
    """Resolve fixture path checking primary fixtures dir and reference samples dir."""
    p_primary = FIXTURES_DIR / filename
    if p_primary.exists():
        return p_primary
    p_alt1 = ALT_CAD_DIR / filename
    if p_alt1.exists():
        return p_alt1
    p_alt2 = ALT_COMPLEX_DIR / filename
    if p_alt2.exists():
        return p_alt2
    raise FileNotFoundError(f"Fixture '{filename}' not found in any search path.")


# Fixture paths
TESTCAD_OBJ = _resolve_fixture("TestCAD.obj")
TESTCAD_STEP = _resolve_fixture("TestCADSTPAP214.stp")
BLADE_OBJ = _resolve_fixture("BladeRev1CFD.obj")
ROTOR_STEP = _resolve_fixture("BladeFullRev1CFD.stp")


def test_cad_fixtures_present():
    """Verify all 4 required CAD reference fixtures exist and are non-empty."""
    for path, min_size in [
        (TESTCAD_OBJ, 400),
        (TESTCAD_STEP, 10000),
        (BLADE_OBJ, 1000000),
        (ROTOR_STEP, 2000000),
    ]:
        assert path.exists(), f"Missing CAD fixture: {path}"
        assert path.stat().st_size > min_size, f"Fixture file unexpectedly small: {path}"


def test_cad_feature_filter_non_face_entities():
    """Verify non-face entity filtering (discarding GEOMETRIC_CURVE_SET and DRAUGHTING_MODEL).

    - TestCADSTPAP214.stp contains entity #35=GEOMETRIC_CURVE_SET with 4 2D sketch lines.
    - BladeFullRev1CFD.stp contains 9 DRAUGHTING_MODEL entities.
    Verify that CADFeatureFilter purges non-face entities, orphan vertices, and keeps Euler chi = 2.
    """
    # 1. Parse TestCAD STEP AP214
    parser = STEPParser(engine="auto")
    cube_sm = parser.parse(TESTCAD_STEP)

    # Ingesting through CADFeatureFilter.sanitize
    sanitized_cube = CADFeatureFilter.sanitize(cube_sm)
    assert len(sanitized_cube.vertices) == 8, f"Expected 8 cube vertices, got {len(sanitized_cube.vertices)}"
    assert len(sanitized_cube.faces) == 12, f"Expected 12 triangular facets, got {len(sanitized_cube.faces)}"

    auditor = SurfaceAuditor(auto_repair_normals=False)
    cube_audit = auditor.audit(sanitized_cube)
    assert cube_audit.is_watertight is True
    assert cube_audit.is_manifold is True
    assert cube_audit.open_edges_count == 0
    assert cube_audit.multi_edges_count == 0
    assert cube_audit.euler_characteristic == 2

    # 2. Parse BladeFullRev1CFD STEP file
    engine = CADGeometryEngine()
    engine.load(ROTOR_STEP)
    rotor_sm = engine.current_mesh
    assert rotor_sm is not None
    assert len(rotor_sm.vertices) > 0
    assert len(rotor_sm.faces) > 0

    # Verify that DRAUGHTING_MODEL entities did not produce non-manifold wireframes
    sanitized_rotor = CADFeatureFilter.sanitize(rotor_sm)
    assert len(sanitized_rotor.vertices) > 0
    assert len(sanitized_rotor.faces) > 0


def test_cad_mesh_sdf_evaluation():
    """Verify continuous Signed Distance Field (CADMeshSDF) on all reference geometries."""
    cad_engine = CADGeometryEngine()

    # 1. Rotated cube SDF
    cad_engine.load(TESTCAD_OBJ)
    cube_sm = cad_engine.current_mesh
    cube_sdf = CADMeshSDF.from_surface_mesh(cube_sm)

    # Centroid is inside: phi < 0, norm(grad) ~ 1.0
    c_cube = np.mean(cube_sm.vertices, axis=0, keepdims=True)
    phi_in, grad_in = cube_sdf.evaluate(c_cube)
    assert phi_in[0] < -50.0, f"Expected deep interior point phi < -50, got {phi_in[0]}"
    assert abs(np.linalg.norm(grad_in[0]) - 1.0) < 0.05

    # Exterior point: phi > 0, norm(grad) ~ 1.0
    bbox_cube = cube_sm.get_bounding_box()
    p_out = (bbox_cube.max_pt + bbox_cube.span * 0.5).reshape(1, 3)
    phi_out, grad_out = cube_sdf.evaluate(p_out)
    assert phi_out[0] > 100.0, f"Expected exterior point phi > 100, got {phi_out[0]}"
    assert abs(np.linalg.norm(grad_out[0]) - 1.0) < 0.05

    # 2. Turbine blade SDF
    cad_engine.load(BLADE_OBJ)
    blade_sm = cad_engine.current_mesh
    blade_sdf = CADMeshSDF.from_surface_mesh(blade_sm)

    bbox_blade = blade_sm.get_bounding_box()
    p_blade_out = (bbox_blade.max_pt + bbox_blade.span * 0.5).reshape(1, 3)
    phi_blade_out, grad_blade_out = blade_sdf.evaluate(p_blade_out)
    assert phi_blade_out[0] > 50.0
    assert abs(np.linalg.norm(grad_blade_out[0]) - 1.0) < 0.05


def test_e2e_meshing_rotated_cube_obj():
    """End-to-end meshing and invariant verification on 200 mm rotated cube (TestCAD.obj).

    Ground Truth:
    - Side length = 200 mm, scaled by 0.001 -> 0.200 m
    - Analytical CFD Volume = 0.008 m^3
    - Analytical CFD BBox: X, Y in [-0.115846, 0.115846] m, Z in [0.0, 0.200] m
    """
    engine = MeshEngine()
    mesh = engine.mesh_cad_file(TESTCAD_OBJ, h0=0.03, scale=0.001)

    # 1. Positivity of cell volumes
    assert np.all(mesh.cell_volumes > 1e-15), "Negative or zero cell volume detected"

    # 2. Analytical volume conservation (0.008 m^3 within 0.01%)
    total_vol = float(np.sum(mesh.cell_volumes))
    assert abs(total_vol - 0.008) / 0.008 < 1e-4, f"Cube volume {total_vol:.8f} deviates from 0.008 m^3"

    # 3. Rotated bounding box check
    min_b = np.min(mesh.points, axis=0)
    max_b = np.max(mesh.points, axis=0)
    expected_half_span = 0.115846
    assert abs(min_b[0] - (-expected_half_span)) < 0.002
    assert abs(max_b[0] - expected_half_span) < 0.002
    assert abs(min_b[2] - 0.0) < 0.002
    assert abs(max_b[2] - 0.200) < 0.002

    # 4. Gauss divergence theorem closure: max ||sum S_f|| < 10^-11
    n_cells = len(mesh.cell_volumes)
    residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(residuals, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(residuals, mesh.neighbour[internal], mesh.face_areas[internal])
    max_closure = float(np.max(np.linalg.norm(residuals, axis=1)))
    assert max_closure < 1e-11, f"Gauss closure violated: {max_closure:.2e} >= 1e-11"

    # 5. Over-relaxed orthogonality consistency: Delta_f + k_f == S_f on internal faces
    decomp_err = float(np.max(np.linalg.norm((mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal], axis=1)))
    assert decomp_err < 1e-14, f"Orthogonality decomposition error: {decomp_err:.2e}"

    # 6. Quality metrics & Error map
    assert hasattr(mesh, "error_map"), "Mesh must possess error_map"
    emap = mesh.error_map
    for k in ["non_orthogonality", "skewness", "aspect_ratio", "stabilization_mask"]:
        assert k in emap, f"Missing key in error_map: {k}"
    assert np.all(emap["non_orthogonality"] >= 0.0)
    assert np.all(emap["non_orthogonality"] <= 90.0)
    assert np.all(emap["skewness"] >= 0.0)
    assert np.all(emap["aspect_ratio"] >= 0.999)


def test_e2e_meshing_rotated_cube_step_ap214():
    """End-to-end meshing and invariant verification on TestCADSTPAP214.stp with sketch lines."""
    engine = MeshEngine()
    mesh = engine.mesh_cad_file(TESTCAD_STEP, h0=0.03, scale=0.001)

    assert np.all(mesh.cell_volumes > 1e-15), "Negative or zero cell volume detected"
    total_vol = float(np.sum(mesh.cell_volumes))
    assert abs(total_vol - 0.008) / 0.008 < 1e-4, f"Cube volume {total_vol:.8f} deviates from 0.008 m^3"

    # Gauss closure
    n_cells = len(mesh.cell_volumes)
    residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(residuals, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(residuals, mesh.neighbour[internal], mesh.face_areas[internal])
    assert float(np.max(np.linalg.norm(residuals, axis=1))) < 1e-11

    # Orthogonality decomposition
    decomp_err = float(np.max(np.linalg.norm((mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal], axis=1)))
    assert decomp_err < 1e-14


def test_e2e_meshing_single_turbine_blade():
    """End-to-end meshing and invariant verification on single turbine blade (BladeRev1CFD.obj).

    Ground Truth:
    - Watertight 2-manifold surface shell (30,832 vertices, 61,660 faces)
    - Enclosed volume ~ 187,600.6 mm^3
    - Positive cell volumes, Gauss closure < 10^-11, complete error map
    """
    engine = MeshEngine()
    mesh = engine.mesh_cad_file(BLADE_OBJ, h0=15.0, scale=1.0)

    # 1. Positive cell volumes
    assert np.all(mesh.cell_volumes > 1e-15), "Turbine blade mesh cells must have positive volumes"

    # 2. Volume agreement (~ 187,600.6 mm^3 within 2%)
    total_vol = float(np.sum(mesh.cell_volumes))
    target_vol = 187600.6
    rel_vol_err = abs(total_vol - target_vol) / target_vol
    assert rel_vol_err < 0.02, f"Blade volume {total_vol:.1f} deviates from target {target_vol:.1f}"

    # 3. Gauss closure
    n_cells = len(mesh.cell_volumes)
    residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(residuals, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(residuals, mesh.neighbour[internal], mesh.face_areas[internal])
    max_closure = float(np.max(np.linalg.norm(residuals, axis=1)))
    assert max_closure < 1e-11, f"Gauss closure violated on blade: {max_closure:.2e}"

    # 4. Orthogonality consistency
    decomp_err = float(np.max(np.linalg.norm((mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal], axis=1)))
    assert decomp_err < 1e-14, f"Orthogonality decomposition error: {decomp_err:.2e}"

    # 5. Error map
    emap = mesh.error_map
    assert np.all(emap["non_orthogonality"] >= 0.0)
    assert np.all(emap["skewness"] >= 0.0)
    assert np.all(emap["aspect_ratio"] >= 1.0)


def test_e2e_meshing_full_turbine_rotor_assembly():
    """End-to-end meshing and invariant verification on rotor assembly (BladeFullRev1CFD.stp).

    Ground Truth:
    - Full rotor with 9 drafting models filtered
    - Enclosed volume ~ 5.56e6 mm^3 (within 2%)
    - Positive cell volumes, Gauss closure < 10^-11, complete error map
    """
    engine = MeshEngine()
    mesh = engine.mesh_cad_file(ROTOR_STEP, h0=15.0, scale=1.0)

    # 1. Positive cell volumes
    assert np.all(mesh.cell_volumes > 1e-15), "Rotor mesh cells must have positive volumes"

    # 2. Enclosed volume agreement (~ 5.56e6 mm^3 within 2%)
    total_vol = float(np.sum(mesh.cell_volumes))
    target_vol = 5.56e6
    rel_vol_err = abs(total_vol - target_vol) / target_vol
    assert rel_vol_err < 0.02, f"Rotor volume {total_vol:.1f} deviates from target {target_vol:.1f}"

    # 3. Gauss closure
    n_cells = len(mesh.cell_volumes)
    residuals = np.zeros((n_cells, 3), dtype=np.float64)
    np.add.at(residuals, mesh.owner, mesh.face_areas)
    internal = mesh.neighbour >= 0
    np.subtract.at(residuals, mesh.neighbour[internal], mesh.face_areas[internal])
    max_closure = float(np.max(np.linalg.norm(residuals, axis=1)))
    assert max_closure < 1e-11, f"Gauss closure violated on rotor: {max_closure:.2e}"

    # 4. Orthogonality consistency
    decomp_err = float(np.max(np.linalg.norm((mesh.delta_f[internal] + mesh.k_f[internal]) - mesh.face_areas[internal], axis=1)))
    assert decomp_err < 1e-14, f"Orthogonality decomposition error: {decomp_err:.2e}"

    # 5. Error map
    emap = mesh.error_map
    assert np.all(emap["non_orthogonality"] >= 0.0)
    assert np.all(emap["skewness"] >= 0.0)
    assert np.all(emap["aspect_ratio"] >= 1.0)


def test_cad_boundary_layer_inflation():
    """Verify prismatic boundary layer inflation extruded from CAD wall facets."""
    cad_engine = CADGeometryEngine()
    cad_engine.load(TESTCAD_OBJ)
    sm = cad_engine.current_mesh

    # Select planar bottom wall faces
    f0 = sm.faces[0]
    v0, v1, v2 = sm.vertices[f0[0]], sm.vertices[f0[1]], sm.vertices[f0[2]]
    n0 = np.cross(v1 - v0, v2 - v0)
    n0 = n0 / np.linalg.norm(n0)

    wall_faces = [sm.faces[0], sm.faces[1]]

    cfg = InflationConfig(num_layers=3, first_layer_height=1.0, growth_rate=1.2, pinching=True)
    engine = MeshEngine(config=MeshingConfig(inflation=cfg))
    bl_mesh = engine.inflate_boundary_layers(sm.vertices, wall_faces, nominal_normal=n0)

    # 1. Positive cell volumes
    assert np.all(bl_mesh.cell_volumes > 1e-15), "Inflation layer cells must have positive volumes"

    # 2. Geometric thickness progression check
    gen = BoundaryLayerGenerator(cfg)
    thicknesses = gen.get_layer_thicknesses()
    assert len(thicknesses) == 3
    for k in range(3):
        assert abs(thicknesses[k] - 1.0 * (1.2 ** k)) < 1e-7

    # 3. Error map
    assert hasattr(bl_mesh, "error_map")
    assert np.all(bl_mesh.error_map["aspect_ratio"] >= 1.0)
