"""Empirical Adversarial Challenge Suite for Milestone M2.

Author: challenger_m2_2 (CAD Stress, Boundary Layer & Cyclic Symmetry Challenger)
Focus:
1. Feature 23: Boundary Layer Inflation, Corner Pinching (< 20 deg, > 150 deg),
   Clearance Pinching, Step-Down Wedge Termination, and Cell Inversion (V_i > 0).
2. Feature 21: Conjugate Heat Transfer (CHT), Interface Detection,
   Twin-Face Collocation, Normal Opposition (S_fF = -S_fS), and AMI Area Weighting.
3. Feature 22: Rotational Cyclic Symmetry R_z(theta), Boundary Pairing, and Flux Pairing.
4. CAD Samples: Ingestion, Wireframe/Sketch Line Filtering, and Volume Conservation
   for TestCAD.obj / TestCADSTPAP214.stp and BladeRev1CFD.obj / BladeFullRev1CFD.stp.
"""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_python,
    _compute_polygon_normal_and_centroid_py,
    _extract_cell_faces_py,
    get_native_dll,
)
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    InflationConfig,
    MeshingConfig,
    UnstructuredMesh,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
CAD_DIR = REPO_ROOT / "resources" / "CADTestSamples"
COMPLEX_CAD_DIR = CAD_DIR / "ComplexGeometryExample"

TESTCAD_OBJ = CAD_DIR / "TestCAD.obj"
TESTCAD_STEP_AP214 = CAD_DIR / "TestCADSTPAP214.stp"
BLADE_OBJ = COMPLEX_CAD_DIR / "BladeRev1CFD.obj"
BLADE_STEP_AP242 = COMPLEX_CAD_DIR / "BladeFullRev1CFD.stp"


def compute_true_signed_cell_volume(cell_nodes: np.ndarray, points: np.ndarray) -> float:
    """Computes the mathematically exact signed volume of a polyhedral cell without abs()."""
    cfaces = _extract_cell_faces_py(cell_nodes)
    c_approx = np.mean(points[cell_nodes], axis=0)
    vol_sum = 0.0
    for fnodes in cfaces:
        norm, cent = _compute_polygon_normal_and_centroid_py(fnodes, points)
        vol_sum += np.dot(cent - c_approx, norm) / 3.0
    return float(vol_sum)


# ===========================================================================
# 1. Feature 23: Boundary Layer Inflation, Corners, Pinching & Termination
# ===========================================================================

def test_feature23_corner_pinching_active():
    """Verify Feature 23: corner pinching is active and tapers layer heights at corners."""
    wall_pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, 1.0, 1.0],
    ], dtype=np.float64)

    wall_faces = [
        np.array([0, 1, 2, 3], dtype=np.int64),
        np.array([0, 3, 5, 4], dtype=np.int64),
    ]

    cfg_pinch = InflationConfig(num_layers=3, first_layer_height=0.01, growth_rate=1.2, pinching=True, pinching_threshold_angle=10.0)
    gen_pinch = BoundaryLayerGenerator(cfg_pinch)
    mesh_pinch = gen_pinch.inflate_layers(wall_pts, wall_faces)

    cfg_nopinch = InflationConfig(num_layers=3, first_layer_height=0.01, growth_rate=1.2, pinching=False)
    gen_nopinch = BoundaryLayerGenerator(cfg_nopinch)
    mesh_nopinch = gen_nopinch.inflate_layers(wall_pts, wall_faces)

    diff = np.max(np.abs(mesh_pinch.points - mesh_nopinch.points))
    print(f"\n[Feature 23 Corner Pinching Active] max|pts_pinch - pts_nopinch| = {diff:.6e}")
    assert diff > 0.0, "Corner pinching must actively taper layer heights at corners"


def test_feature23_sharp_acute_corner_cell_inversion():
    """Empirical adversarial test demonstrating element inversion on sharp corners (< 20 deg).

    At a 15 degree acute wedge corner, unpinched normal extrusion causes face winding
    and extrusion discordance, resulting in negative signed volume cells.
    DiFVM topology engine conceals this defect via std::abs(vol_sum).
    """
    theta = np.radians(15.0)
    p0 = np.array([0.0, 0.0, 0.0])
    p1 = np.array([1.0, 0.0, 0.0])
    p2 = np.array([1.0, 0.0, 1.0])
    p3 = np.array([0.0, 0.0, 1.0])
    p4 = np.array([np.cos(theta), np.sin(theta), 0.0])
    p5 = np.array([np.cos(theta), np.sin(theta), 1.0])

    pts = np.array([p0, p1, p2, p3, p4, p5])
    faces = [[0, 1, 2, 3], [0, 3, 5, 4]]

    cfg = InflationConfig(num_layers=2, first_layer_height=0.1, growth_rate=1.2, pinching=True)
    gen = BoundaryLayerGenerator(cfg)
    mesh = gen.inflate_layers(pts, faces)

    signed_volumes = [compute_true_signed_cell_volume(c, mesh.points) for c in mesh.cells]
    print(f"\n[Feature 23 Acute 15 deg Corner] Signed volumes: {signed_volumes}")
    print(f"[Feature 23 Acute 15 deg Corner] Masked mesh.cell_volumes: {mesh.cell_volumes.tolist()}")

    # Inverted cells exist: signed volume is negative!
    inverted_cells = [i for i, v in enumerate(signed_volumes) if v < 0.0]
    print(f"[Feature 23 Acute 15 deg Corner] Inverted cell indices: {inverted_cells}")
    assert len(inverted_cells) > 0, "Expected inverted cells on sharp 15 deg corner without operative pinching"


def test_feature23_narrow_clearance_collapse_and_real_volume():
    """Verify that critically narrow clearances collapse to zero thickness and are NOT masked with artificial 1.0 m^3."""
    wall_pts = np.array([
        [0.0, 0.0, 0.0],
        [0.1, 0.0, 0.0],
        [0.1, 0.1, 0.0],
        [0.0, 0.1, 0.0],
    ], dtype=np.float64)
    wall_faces = [np.array([0, 1, 2, 3], dtype=np.int64)]

    cfg = InflationConfig(num_layers=1, first_layer_height=0.001, pinching=True)
    gen = BoundaryLayerGenerator(cfg)

    # 1. Zero clearance: pinched to 0 thickness -> cell volume is 0.0 (no artificial fallback)
    mesh_zero = gen.inflate_layers(wall_pts, wall_faces, clearances=np.full(4, 0.0))
    print(f"\n[Feature 23 Zero Clearance] Cell volume = {mesh_zero.cell_volumes[0]:.6e}")
    assert np.isclose(mesh_zero.cell_volumes[0], 0.0, atol=1e-12), "Zero clearance must yield 0 volume without artificial fallback"

    # 2. Collapsed clearance: pinched to near-0 -> volume < 1e-10, NOT replaced by 1.0 m^3
    mesh_tiny = gen.inflate_layers(wall_pts, wall_faces, clearances=np.full(4, 1e-14))
    print(f"[Feature 23 Collapsed Clearance] Cell volume = {mesh_tiny.cell_volumes[0]:.6e} (real volume, not 1.0 m^3)")
    assert mesh_tiny.cell_volumes[0] < 1e-10, "Collapsed cell must retain physical volume near zero"
    assert mesh_tiny.cell_volumes[0] != 1.0, "Collapsed cell must not be masked as 1.0 m^3"


# ===========================================================================
# 2. Feature 21: Conjugate Heat Transfer (CHT) Multi-Region Coupling & AMI
# ===========================================================================

def test_feature21_cht_coupled_wall_structure():
    """Verify Feature 21 CHT coupledWall twin-face and normal opposition invariants.

    Expected Invariants from Specification:
    1. Fluid patch coupledWall_fluid with normal S_fF pointing into solid.
    2. Solid patch coupledWall_solid with normal S_fS pointing into fluid.
    3. Normal opposition: S_fF == - S_fS.
    """
    pts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
        [2, 0, 0], [2, 1, 0], [2, 0, 1], [2, 1, 1],
    ], dtype=np.float64)

    cell_0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64) # Fluid
    cell_1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64) # Solid
    regions = np.array([0, 1], dtype=np.int32)

    mesh = compute_difvm_topology(pts, [cell_0, cell_1], regions=regions)

    print(f"\n[Feature 21 CHT Interface] n_faces={len(mesh.face_areas)}, internal_faces={np.sum(mesh.neighbour >= 0)}")
    print(f"[Feature 21 CHT Interface] baffles={mesh.baffles}")
    print(f"[Feature 21 CHT Interface] boundaries={mesh.boundaries}")

    assert "coupledWall_fluid" in mesh.boundaries, "Conformal twin boundary patch coupledWall_fluid must exist"
    start_f, n_f = mesh.boundaries["coupledWall_fluid"]
    start_s, n_s = mesh.boundaries["coupledWall_solid"]
    assert n_f == 1 and n_s == 1
    f_fluid = start_f
    f_solid = start_s
    assert mesh.neighbour[f_fluid] == -1, "Twin face must be a boundary face"
    assert mesh.neighbour[f_solid] == -1, "Twin face must be a boundary face"
    s_fluid = mesh.face_areas[f_fluid]
    s_solid = mesh.face_areas[f_solid]
    assert np.allclose(s_fluid, -s_solid, atol=1e-12), "Conformal twin faces must have opposite normal vectors (S_fF = -S_fS)"


def test_feature21_ami_area_weighting_status():
    """Verify that Feature 21 Arbitrary Mesh Interface (AMI) area weighting is implemented."""
    import backend.meshing.difvm_topology as dt
    import backend.meshing.mesh_engine as me
    import backend.meshing.models as mm

    all_symbols = dir(dt) + dir(me) + dir(mm)
    ami_symbols = [s for s in all_symbols if "ami" in s.lower()]
    print(f"\n[Feature 21 AMI Status] Detected AMI symbols in codebase: {ami_symbols}")
    assert len(ami_symbols) > 0, "Expected AMI implementations in backend.meshing"
    assert "compute_ami_weights" in ami_symbols


# ===========================================================================
# 3. Feature 22: Rotational Cyclic Symmetry
# ===========================================================================

def test_feature22_rotational_cyclic_pairing_status():
    """Verify Feature 22 Rotational Cyclic Symmetry pairing is exported and functional."""
    dll = get_native_dll()
    assert dll is not None, "Native DLL must be present"

    # Check export table
    has_cyclic_export = hasattr(dll, "cfd_mesher_extract_cyclic_pairs") or hasattr(dll, "extractCyclicPairs")
    print(f"\n[Feature 22 Cyclic Export] DLL has cyclic export: {has_cyclic_export}")
    assert has_cyclic_export, "DLL must export C-ABI for cyclic pairing"

    # Check generated mesh cyclic_pairs
    points = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells = [np.arange(8)]
    mesh = compute_difvm_topology(points, cells)
    print(f"[Feature 22 Cyclic Pairs] mesh.cyclic_pairs: {mesh.cyclic_pairs}")
    assert isinstance(mesh.cyclic_pairs, list)


# ===========================================================================
# 4. CAD Samples Ingestion, Filtering & Volume Conservation
# ===========================================================================

def test_cad_testcad_cube_filtering_and_volume():
    """Verify TestCAD rotated cube ingestion, wireframe filtering, and exact volume."""
    engine = MeshEngine()

    # OBJ: 200 mm cube with scale=0.001 -> volume = 0.008 m^3
    mesh_obj = engine.mesh_cad_file(TESTCAD_OBJ, h0=0.03, scale=0.001)
    vol_obj = np.sum(mesh_obj.cell_volumes)
    assert abs(vol_obj - 0.008) / 0.008 < 0.03, f"TestCAD.obj volume {vol_obj} != 0.008"

    # STEP AP214 (with sketch lines): must filter sketch lines and achieve 0.008 m^3 when scale=0.001
    mesh_step = engine.mesh_cad_file(TESTCAD_STEP_AP214, h0=0.04, scale=0.001)
    vol_step = np.sum(mesh_step.cell_volumes)
    print(f"\n[CAD TestCAD Cube] OBJ vol = {vol_obj:.6f} m^3, STEP AP214 vol = {vol_step:.6f} m^3")
    assert abs(vol_step - 0.008) / 0.008 < 0.03, f"TestCADSTPAP214.stp volume {vol_step} != 0.008"


def test_cad_blade_obj_vs_step_scale_discrepancy():
    """Verify BladeRev1CFD.obj volume matches 188,337.3 mm^3 within 8% and document STEP assembly difference."""
    engine = MeshEngine()

    # OBJ is single blade
    mesh_obj = engine.mesh_cad_file(BLADE_OBJ, h0=15.0, scale=1.0)
    vol_obj = np.sum(mesh_obj.cell_volumes)
    analytical_vol = 188337.3
    rel_err_obj = abs(vol_obj - analytical_vol) / analytical_vol
    print(f"\n[CAD Blade OBJ] Volume = {vol_obj:.1f} mm^3, rel_err = {rel_err_obj * 100:.2f}%")
    assert rel_err_obj < 0.08, f"Blade OBJ volume error {rel_err_obj * 100:.2f}% >= 8%"

    # STEP AP242 contains full rotor assembly
    mesh_step = engine.mesh_cad_file(BLADE_STEP_AP242, h0=15.0, scale=1.0)
    vol_step = np.sum(mesh_step.cell_volumes)
    print(f"[CAD Blade STEP AP242] Full assembly volume = {vol_step:.1f} mm^3 (Assembly != Single Blade)")
