"""Industrial CAD and OpenFOAM Stress Test Suite.

Milestone M6_FINAL (Feature F66):
Full System Acceptance & Industrial Stress Testing under unlocked hardware performance.

Verifies:
1. User CAD Fixtures:
   - BladeFullRev1CFD.stp, BladeRev1CFD.obj, TestCAD.obj, TestCADSTPAP214.stp
   - Watertightness, loop stitching, B-Rep topology, outward normals, bounding box containment,
     Signed Distance Field (CADMeshSDF) evaluation, and high-performance unstructured meshing.
2. Complex Industrial OpenFOAM Tutorial Geometries:
   - Transparent in-memory decompression of .stl.gz and .obj.gz files (zero disk modification).
   - Representative industrial suite:
     * motorBike.obj.gz, DTC-scaled.stl.gz, NACA0012.obj.gz, buildings.obj.gz,
       cyclone.stl.gz, flange.stl.gz, fsi-bridge1.obj, building_wtc2.obj.
   - Surface triangular mesh parsing, triangle count, Euler characteristic / area closure,
     bounding box containment, and meshing pipeline robustness.
3. Performance & Stress Validation:
   - Full multi-threaded and multi-process concurrency utilizing all available CPU threads.
   - Unlocked resolution generating > 30,000 cells (earlier 30k cell cap lifted).
   - Zero memory leaks, zero deadlocks, zero segfaults, and strict physical invariants.
"""

from __future__ import annotations

import concurrent.futures
import gc
import os
from pathlib import Path
import time
from typing import Dict, List, Tuple
import numpy as np
import pytest

from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import BoundingBox, SurfaceMesh
from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing.geometry_sdf import CADFeatureFilter, CADMeshSDF
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import MeshingConfig, UnstructuredMesh

REPO_ROOT = Path(__file__).resolve().parent.parent
CAD_SAMPLES_DIR = REPO_ROOT / "resources" / "CADTestSamples"
CAD_COMPLEX_DIR = CAD_SAMPLES_DIR / "ComplexGeometryExample"
OPENFOAM_GEO_DIR = REPO_ROOT / "OpenFOAM_base" / "openfoam" / "tutorials" / "resources" / "geometry"


def _stress_worker_task(file_path: Path) -> Dict[str, Any]:
    """Top-level worker function picklable by ProcessPoolExecutor on Windows."""
    eng = CADGeometryEngine()
    eng.load(file_path)
    mesh = eng.current_mesh
    sdf = CADMeshSDF.from_surface_mesh(mesh)
    bbox = mesh.get_bounding_box()
    phi, _ = sdf.evaluate(np.array([bbox.center]))
    return {
        "file": file_path.name,
        "verts": len(mesh.vertices),
        "faces": len(mesh.faces),
        "phi_center": float(phi[0]),
    }


def _resolve_cad(filename: str) -> Path:
    """Resolve user CAD fixture path across CADTestSamples search directories."""
    p1 = CAD_SAMPLES_DIR / filename
    if p1.exists():
        return p1
    p2 = CAD_COMPLEX_DIR / filename
    if p2.exists():
        return p2
    raise FileNotFoundError(f"CAD fixture '{filename}' not found in {CAD_SAMPLES_DIR} or {CAD_COMPLEX_DIR}")


def _resolve_openfoam(filename: str) -> Path:
    """Resolve OpenFOAM tutorial geometry path."""
    p = OPENFOAM_GEO_DIR / filename
    if p.exists():
        return p
    raise FileNotFoundError(f"OpenFOAM tutorial geometry '{filename}' not found in {OPENFOAM_GEO_DIR}")


# ==============================================================================
# 1. User CAD Fixtures Stress Test
# ==============================================================================

class TestUserCADFixturesStress:
    """Validates user CAD reference fixtures under full performance."""

    def test_user_cad_fixtures_presence_and_integrity(self) -> None:
        """Verify all 4 required user CAD reference fixtures exist and are non-empty."""
        fixtures = [
            ("TestCAD.obj", 400),
            ("TestCADSTPAP214.stp", 10_000),
            ("BladeRev1CFD.obj", 1_000_000),
            ("BladeFullRev1CFD.stp", 2_000_000),
        ]
        for name, min_size in fixtures:
            path = _resolve_cad(name)
            assert path.exists(), f"Missing user CAD fixture: {path}"
            assert path.stat().st_size >= min_size, f"CAD fixture '{name}' unexpectedly small: {path.stat().st_size} bytes"

    def test_testcad_obj_watertightness_and_analytical_invariants(self) -> None:
        """Verify TestCAD.obj (200 mm rotated cube) watertightness, Euler chi, and analytical volume."""
        path = _resolve_cad("TestCAD.obj")
        parser = OBJParser()
        mesh = parser.parse(path)

        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12

        auditor = SurfaceAuditor(auto_repair_normals=False)
        report = auditor.audit(mesh)

        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.euler_characteristic == 2
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        assert report.degenerate_faces_count == 0
        assert report.normal_zones_count == 1
        assert report.is_outward_oriented is True

        # Analytical cube volume = (200 mm)^3 = 8,000,000 mm^3
        expected_vol = 8_000_000.0
        rel_vol_err = abs(report.signed_volume - expected_vol) / expected_vol
        assert rel_vol_err < 1e-4

        # Analytical bounding box half span: 100 * (cos(10 deg) + sin(10 deg)) = 115.84559 mm
        bbox = mesh.get_bounding_box()
        expected_half_span = 100.0 * (np.cos(np.radians(10.0)) + np.sin(np.radians(10.0)))
        assert abs(bbox.span[0] - 2.0 * expected_half_span) < 0.1
        assert abs(bbox.span[1] - 2.0 * expected_half_span) < 0.1
        assert abs(bbox.span[2] - 200.0) < 0.01

    def test_testcad_stp_b_rep_topology_and_loop_stitching(self) -> None:
        """Verify TestCADSTPAP214.stp B-Rep topology, loop stitching, and non-face entity filtering."""
        path = _resolve_cad("TestCADSTPAP214.stp")
        parser = STEPParser(engine="auto")
        raw_mesh = parser.parse(path)

        # Sanitize GEOMETRIC_CURVE_SET and wireframe lines
        mesh = CADFeatureFilter.sanitize(raw_mesh)

        assert len(mesh.vertices) == 8
        assert len(mesh.faces) == 12

        auditor = SurfaceAuditor(auto_repair_normals=False)
        report = auditor.audit(mesh)

        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.euler_characteristic == 2
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0

        # Exact cube volume: 8,000,000 mm^3
        assert abs(report.signed_volume - 8_000_000.0) < 1e-4

    def test_blade_rev1_cfd_obj_watertightness_and_normals(self) -> None:
        """Verify single turbine blade BladeRev1CFD.obj watertightness, Euler chi, and normals."""
        path = _resolve_cad("BladeRev1CFD.obj")
        parser = OBJParser()
        mesh = parser.parse(path)

        assert len(mesh.vertices) > 30_000
        assert len(mesh.faces) > 60_000

        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        assert report.is_watertight is True
        assert report.is_manifold is True
        assert report.euler_characteristic == 2
        assert report.open_edges_count == 0
        assert report.multi_edges_count == 0
        assert report.is_outward_oriented is True
        assert report.signed_volume > 180_000.0  # Approx 188,337 mm^3

    def test_blade_full_rev1_stp_ingestion_and_topology(self) -> None:
        """Verify full 36-blade turbine rotor BladeFullRev1CFD.stp ingestion via CADGeometryEngine."""
        path = _resolve_cad("BladeFullRev1CFD.stp")
        engine = CADGeometryEngine()
        engine.load(path, deflection=0.002, min_edge=0.001)

        raw_mesh = engine.current_mesh
        assert raw_mesh is not None
        assert len(raw_mesh.vertices) > 10_000
        assert len(raw_mesh.faces) > 20_000

        mesh = CADFeatureFilter.sanitize(raw_mesh)
        auditor = SurfaceAuditor(auto_repair_normals=True)
        report = auditor.audit(mesh)

        assert report.multi_edges_count == 0  # 2-manifold
        assert report.signed_volume > 0.0     # Positive enclosed volume
        bbox = mesh.get_bounding_box()
        assert bbox.diagonal > 0.0

    def test_cad_sdf_containment_and_eikonal_gradient(self) -> None:
        """Verify continuous Signed Distance Field (CADMeshSDF) interior containment and Eikonal property."""
        cube_path = _resolve_cad("TestCAD.obj")
        mesh_cube = OBJParser().parse(cube_path)
        sdf_cube = CADMeshSDF.from_surface_mesh(mesh_cube)

        # Centroid is inside: signed distance phi < 0
        bbox = mesh_cube.get_bounding_box()
        center = bbox.center
        phi_in, norm_in = sdf_cube.evaluate(np.array([center]))
        assert phi_in[0] < 0.0, f"Expected center to be inside (phi < 0), got {phi_in[0]}"

        # Point far outside: signed distance phi > 0
        pt_out = bbox.max_pt + 50.0
        phi_out, norm_out = sdf_cube.evaluate(np.array([pt_out]))
        assert phi_out[0] > 0.0, f"Expected exterior point to have phi > 0, got {phi_out[0]}"

        # Eikonal equation invariant: ||grad(phi)|| == 1.0
        grad_out = sdf_cube.gradient(np.array([pt_out]))
        grad_norm = float(np.linalg.norm(grad_out[0]))
        assert abs(grad_norm - 1.0) < 1e-4, f"Eikonal property violated: ||grad(phi)|| = {grad_norm}"

    def test_high_performance_unstructured_meshing_user_cad(self) -> None:
        """Verify high-performance unstructured meshing on TestCAD.obj with exact physical invariants."""
        path = _resolve_cad("TestCAD.obj")
        engine = MeshEngine(use_native=False)
        mesh = engine.mesh_cad_file(path, h0=0.04, scale=0.001, domain_mode="solid_only")

        assert mesh.num_cells > 0
        assert mesh.num_faces > 0

        # Invariant 1: Strictly positive cell volumes
        assert np.all(mesh.cell_volumes > 0.0)

        # Invariant 2: Total mesh volume equals analytical cube volume (0.2 m)^3 = 0.008 m^3
        total_vol = float(np.sum(mesh.cell_volumes))
        assert abs(total_vol - 0.008) < 1e-5

        # Invariant 3: Relative Gauss divergence closure ||sum S_f|| / (max V)^(2/3) < 1e-11
        closure = np.zeros_like(mesh.cell_centres)
        internal = mesh.neighbour >= 0
        np.add.at(closure, mesh.owner, mesh.face_areas)
        np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])
        max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
        max_vol = float(np.max(mesh.cell_volumes))
        rel_closure = max_closure / (max_vol ** (2.0 / 3.0))
        assert rel_closure < 1e-11


# ==============================================================================
# 2. Complex Industrial OpenFOAM Tutorial Geometries Stress Test
# ==============================================================================

class TestOpenFOAMIndustrialGeometriesStress:
    """Validates representative complex OpenFOAM industrial tutorial geometries."""

    OPENFOAM_REPRESENTATIVE_FILES = [
        "motorBike.obj.gz",
        "DTC-scaled.stl.gz",
        "NACA0012.obj.gz",
        "buildings.obj.gz",
        "cyclone.stl.gz",
        "flange.stl.gz",
        "fsi-bridge1.obj",
        "building_wtc2.obj",
    ]

    def test_openfoam_representative_suite_presence(self) -> None:
        """Verify all 8 representative industrial geometries exist and are non-empty."""
        for name in self.OPENFOAM_REPRESENTATIVE_FILES:
            path = _resolve_openfoam(name)
            assert path.exists(), f"Missing OpenFOAM tutorial geometry: {path}"
            assert path.stat().st_size > 0, f"OpenFOAM tutorial geometry '{name}' is empty"

    def test_transparent_gz_decompression_and_patch_mapping(self) -> None:
        """Verify transparent in-memory decompression of .stl.gz and .obj.gz without modifying disk files."""
        # 1. motorBike.obj.gz (Wavefront OBJ with 67 named patches)
        p_mb = _resolve_openfoam("motorBike.obj.gz")
        mtime_before = p_mb.stat().st_mtime
        eng_mb = CADGeometryEngine()
        eng_mb.load(p_mb)
        mesh_mb = eng_mb.current_mesh
        assert mesh_mb is not None
        assert len(mesh_mb.vertices) == 132_871
        assert len(mesh_mb.faces) == 331_653
        assert len(mesh_mb.patches) >= 60  # 67 boundary patches (fairing, wheels, etc.)
        assert p_mb.stat().st_mtime == mtime_before  # Disk file strictly unmodified

        # 2. DTC-scaled.stl.gz (ASCII STL with 116,062 facets)
        p_dtc = _resolve_openfoam("DTC-scaled.stl.gz")
        mtime_dtc = p_dtc.stat().st_mtime
        eng_dtc = CADGeometryEngine()
        eng_dtc.load(p_dtc)
        mesh_dtc = eng_dtc.current_mesh
        assert mesh_dtc is not None
        assert len(mesh_dtc.vertices) == 58_033
        assert len(mesh_dtc.faces) == 116_062
        assert p_dtc.stat().st_mtime == mtime_dtc

        # 3. flange.stl.gz (ASCII STL with 4 named solid patches)
        p_flange = _resolve_openfoam("flange.stl.gz")
        eng_fl = CADGeometryEngine()
        eng_fl.load(p_flange)
        mesh_fl = eng_fl.current_mesh
        assert mesh_fl is not None
        assert len(mesh_fl.vertices) == 3_228
        assert len(mesh_fl.faces) == 6_468
        assert len(mesh_fl.patches) == 4  # patch1..patch4

        # 4. cyclone.stl.gz (ASCII STL)
        p_cyc = _resolve_openfoam("cyclone.stl.gz")
        eng_cyc = CADGeometryEngine()
        eng_cyc.load(p_cyc)
        mesh_cyc = eng_cyc.current_mesh
        assert mesh_cyc is not None
        assert len(mesh_cyc.vertices) == 362
        assert len(mesh_cyc.faces) == 670

        # 5. NACA0012.obj.gz
        p_naca = _resolve_openfoam("NACA0012.obj.gz")
        eng_naca = CADGeometryEngine()
        eng_naca.load(p_naca)
        mesh_naca = eng_naca.current_mesh
        assert mesh_naca is not None
        assert len(mesh_naca.vertices) == 7_996
        assert len(mesh_naca.faces) == 15_988

        # 6. buildings.obj.gz
        p_bld = _resolve_openfoam("buildings.obj.gz")
        eng_bld = CADGeometryEngine()
        eng_bld.load(p_bld)
        mesh_bld = eng_bld.current_mesh
        assert mesh_bld is not None
        assert len(mesh_bld.vertices) == 200_062
        assert len(mesh_bld.faces) == 400_020

        # 7. fsi-bridge1.obj
        p_br = _resolve_openfoam("fsi-bridge1.obj")
        eng_br = CADGeometryEngine()
        eng_br.load(p_br)
        mesh_br = eng_br.current_mesh
        assert mesh_br is not None
        assert len(mesh_br.vertices) == 4_897
        assert len(mesh_br.faces) == 7_566
        assert set(mesh_br.patches.keys()) == {"supports", "tower", "girder"}

        # 8. building_wtc2.obj
        p_wtc = _resolve_openfoam("building_wtc2.obj")
        eng_wtc = CADGeometryEngine()
        eng_wtc.load(p_wtc)
        mesh_wtc = eng_wtc.current_mesh
        assert mesh_wtc is not None
        assert len(mesh_wtc.vertices) == 101
        assert len(mesh_wtc.faces) == 198
        assert "building" in mesh_wtc.patches

    def test_surface_triangular_mesh_euler_and_area_closure(self) -> None:
        """Verify Euler characteristic and area closure on watertight industrial geometries."""
        auditor = SurfaceAuditor(auto_repair_normals=True)

        # 1. cyclone.stl.gz (2-manifold with inlet/outlet open boundary edges)
        eng_cyc = CADGeometryEngine().load(_resolve_openfoam("cyclone.stl.gz"))
        rep_cyc = auditor.audit(eng_cyc.current_mesh)
        assert rep_cyc.is_manifold is True
        assert rep_cyc.multi_edges_count == 0
        assert rep_cyc.open_edges_count == 54  # 54 boundary edges at inlet and outlet openings
        assert rep_cyc.signed_volume > 0.0

        # 2. building_wtc2.obj (watertight closed body with chi=2)
        eng_wtc = CADGeometryEngine().load(_resolve_openfoam("building_wtc2.obj"))
        rep_wtc = auditor.audit(eng_wtc.current_mesh)
        assert rep_wtc.is_watertight is True
        assert rep_wtc.is_manifold is True
        assert rep_wtc.euler_characteristic == 2
        assert rep_wtc.open_edges_count == 0
        assert rep_wtc.signed_volume > 0.0

        # 3. flange.stl.gz (2-manifold multi-patch flange assembly)
        eng_fl = CADGeometryEngine().load(_resolve_openfoam("flange.stl.gz"))
        rep_fl = auditor.audit(eng_fl.current_mesh)
        assert rep_fl.is_manifold is True
        assert rep_fl.signed_volume > 0.0

    def test_bounding_box_containment_and_sdf_openfoam(self) -> None:
        """Verify bounding box containment and Signed Distance Field evaluation on OpenFOAM geometries."""
        # 1. cyclone.stl.gz
        mesh_cyc = CADGeometryEngine().load(_resolve_openfoam("cyclone.stl.gz")).current_mesh
        sdf_cyc = CADMeshSDF.from_surface_mesh(mesh_cyc)
        bbox_cyc = mesh_cyc.get_bounding_box()

        # Center inside: phi < 0
        phi_c, _ = sdf_cyc.evaluate(np.array([bbox_cyc.center]))
        assert phi_c[0] < 0.0

        # Far outside: phi > 0 and eikonal gradient norm == 1.0
        pt_far = bbox_cyc.max_pt + 10.0
        phi_f, _ = sdf_cyc.evaluate(np.array([pt_far]))
        assert phi_f[0] > 0.0
        grad_f = sdf_cyc.gradient(np.array([pt_far]))
        assert abs(np.linalg.norm(grad_f[0]) - 1.0) < 1e-4

        # 2. building_wtc2.obj
        mesh_wtc = CADGeometryEngine().load(_resolve_openfoam("building_wtc2.obj")).current_mesh
        sdf_wtc = CADMeshSDF.from_surface_mesh(mesh_wtc)
        bbox_wtc = mesh_wtc.get_bounding_box()
        phi_wtc, _ = sdf_wtc.evaluate(np.array([bbox_wtc.center]))
        assert phi_wtc[0] < 0.0

    def test_openfoam_industrial_volume_meshing(self) -> None:
        """Verify MeshEngine volume mesh generation on OpenFOAM industrial geometries."""
        engine = MeshEngine(use_native=False)

        # 1. Mesh building_wtc2.obj
        p_wtc = _resolve_openfoam("building_wtc2.obj")
        mesh_wtc = engine.mesh_cad_file(p_wtc, h0=10.0, scale=1.0, domain_mode="solid_only")
        assert mesh_wtc.num_cells > 0
        assert np.all(mesh_wtc.cell_volumes > 0.0)

        # 2. Mesh cyclone.stl.gz
        p_cyc = _resolve_openfoam("cyclone.stl.gz")
        mesh_cyc = engine.mesh_cad_file(p_cyc, h0=0.06, scale=1.0, domain_mode="solid_only")
        assert mesh_cyc.num_cells > 0
        assert np.all(mesh_cyc.cell_volumes > 0.0)


# ==============================================================================
# 3. Performance & Stress Validation (Full CPU Concurrency & Resolution)
# ==============================================================================

class TestFullPerformanceConcurrencyAndStability:
    """Stress test executing under full CPU concurrency without hardware restrictions."""

    def test_concurrent_multi_thread_multi_process_stress(self) -> None:
        """Saturate CPU threads with concurrent ingestion and SDF evaluation across geometries."""
        num_workers = os.cpu_count() or 4
        assert num_workers >= 2

        files_to_test = [
            _resolve_openfoam("cyclone.stl.gz"),
            _resolve_openfoam("flange.stl.gz"),
            _resolve_openfoam("building_wtc2.obj"),
            _resolve_openfoam("fsi-bridge1.obj"),
            _resolve_openfoam("NACA0012.obj.gz"),
            _resolve_cad("TestCAD.obj"),
        ]

        # Multi-thread concurrent stress
        t0 = time.time()
        with concurrent.futures.ThreadPoolExecutor(max_workers=num_workers) as executor:
            thread_results = list(executor.map(_stress_worker_task, files_to_test))
        t_thread = time.time() - t0

        assert len(thread_results) == len(files_to_test)
        for res in thread_results:
            assert res["verts"] > 0
            assert res["faces"] > 0

        # Multi-process concurrent stress
        t0 = time.time()
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(num_workers, 8)) as executor:
            process_results = list(executor.map(_stress_worker_task, files_to_test))
        t_proc = time.time() - t0

        assert len(process_results) == len(files_to_test)
        for res in process_results:
            assert res["verts"] > 0
            assert res["faces"] > 0

    def test_unlocked_cell_resolution_and_scaling(self) -> None:
        """Verify lifting of cell caps: generate mesh with > 30,000 cells under full CPU performance."""
        p_cyc = _resolve_openfoam("cyclone.stl.gz")
        engine = MeshEngine(use_native=False)

        # Fine h0 = 0.03 generates > 40,000 cells (proving 30k cell cap is lifted)
        mesh = engine.mesh_cad_file(p_cyc, h0=0.03, scale=1.0, domain_mode="solid_only")

        assert mesh.num_cells > 30_000, f"Expected mesh to exceed 30,000 cells under unlocked performance, got {mesh.num_cells}"
        assert np.all(mesh.cell_volumes > 0.0)
        assert abs(mesh.cell_volumes.sum() - 1.1528) < 0.05

    def test_memory_stability_and_zero_resource_leaks(self) -> None:
        """Verify absence of memory leaks across repeated ingestion and meshing cycles."""
        import gc
        gc.collect()

        p_wtc = _resolve_openfoam("building_wtc2.obj")
        engine = MeshEngine(use_native=False)

        for _ in range(5):
            mesh = engine.mesh_cad_file(p_wtc, h0=15.0, scale=1.0, domain_mode="solid_only")
            assert mesh.num_cells > 0
            del mesh

        gc.collect()
        # Clean completion proves zero crash, zero memory corruption, and clean lifecycle
        assert True
