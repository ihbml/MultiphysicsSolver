"""Independent Victory Auditor Verification Script.

Executes direct empirical verification of the CAD & Geometry Ingestion Engine
against real reference CAD parts, analytical ground truth, and adversarial stress tests.
"""

import sys
import tempfile
from pathlib import Path
import numpy as np

# Ensure repository root is on sys.path
repo_root = Path(r"G:\Datein\Projects\CFDSolver")
if str(repo_root) not in sys.path:
    sys.path.insert(0, str(repo_root))

from backend.geometry.pipeline import CADGeometryEngine
from backend.geometry.models import SurfaceMesh
from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.transformation.scaler import GeometryScaler
from backend.geometry.exporter.openfoam_exporter import OpenFOAMExporter

samples_dir = repo_root / "resources" / "CADTestSamples"

def test_real_cad_samples():
    print("=== Testing Real CAD Reference Samples ===")
    sample_files = [
        "TestCAD.obj",
        "TestCAD.stl",
        "TestCADSTPAP203.stp",
        "TestCADSTPAP214.stp",
        "TestCADSTPAP242.stp",
        "TestCAD.prt",
    ]
    
    expected_cad_half_span = 100.0 * (np.cos(np.radians(10)) + np.sin(np.radians(10)))  # ~115.845593
    expected_cfd_half_span = expected_cad_half_span * 0.001                             # ~0.115845593
    expected_cfd_volume = 0.008000                                                      # 0.2^3
    expected_cfd_area = 0.240000                                                        # 6 * 0.2^2

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)

        for filename in sample_files:
            file_path = samples_dir / filename
            assert file_path.exists(), f"Reference sample missing: {file_path}"
            
            engine = CADGeometryEngine()
            # Ingest, scale mm->m, audit, and export
            exported_path, report = engine.ingest_and_prepare(
                input_file=file_path,
                case_dir=tmp_path / filename,
                output_filename=f"{Path(filename).stem}.stl",
                scale_factor=0.001,
            )

            mesh = engine.current_mesh
            assert mesh is not None, f"Mesh failed to load for {filename}"

            # Check vertex and face counts
            assert len(mesh.vertices) == 8, f"{filename}: expected 8 vertices, got {len(mesh.vertices)}"
            assert len(mesh.faces) == 12, f"{filename}: expected 12 faces, got {len(mesh.faces)}"

            # Check CFD bounding box
            bbox = mesh.get_bounding_box()
            assert np.isclose(bbox.min_pt[0], -expected_cfd_half_span, atol=1e-5), f"{filename}: X min mismatch"
            assert np.isclose(bbox.max_pt[0], expected_cfd_half_span, atol=1e-5), f"{filename}: X max mismatch"
            assert np.isclose(bbox.min_pt[1], -expected_cfd_half_span, atol=1e-5), f"{filename}: Y min mismatch"
            assert np.isclose(bbox.max_pt[1], expected_cfd_half_span, atol=1e-5), f"{filename}: Y max mismatch"
            assert np.isclose(bbox.min_pt[2], 0.0, atol=1e-5), f"{filename}: Z min mismatch"
            assert np.isclose(bbox.max_pt[2], 0.2, atol=1e-5), f"{filename}: Z max mismatch"

            # Check CFD Volume and Area
            assert np.isclose(report.signed_volume, expected_cfd_volume, atol=1e-6), f"{filename}: Volume mismatch ({report.signed_volume} vs {expected_cfd_volume})"
            area = GeometryScaler.compute_surface_area(mesh.vertices, mesh.faces)
            assert np.isclose(area, expected_cfd_area, atol=1e-6), f"{filename}: Area mismatch ({area} vs {expected_cfd_area})"

            # Check Topology
            assert report.is_watertight is True, f"{filename}: Not watertight"
            assert report.is_manifold is True, f"{filename}: Not manifold"
            assert report.euler_characteristic == 2, f"{filename}: Euler chi {report.euler_characteristic} != 2"
            assert report.open_edges_count == 0, f"{filename}: Open edges {report.open_edges_count} > 0"
            assert report.multi_edges_count == 0, f"{filename}: Multi edges {report.multi_edges_count} > 0"
            assert report.is_outward_oriented is True, f"{filename}: Not outward oriented"
            assert report.passed_openfoam_surfacecheck is True, f"{filename}: Failed OpenFOAM surfaceCheck"

            # Check Exported OpenFOAM structure
            assert exported_path.exists(), f"{filename}: Export file does not exist: {exported_path}"
            assert exported_path.stat().st_size > 0, f"{filename}: Exported file is empty"
            assert "constant" in exported_path.parts and "triSurface" in exported_path.parts, f"{filename}: Invalid directory structure"

            # Check snappyHexMeshDict block
            dict_block = engine.generate_snappyhexmesh_dict()
            assert "type triSurfaceMesh;" in dict_block
            assert "regions" in dict_block

            print(f"  [PASS] {filename}: V={len(mesh.vertices)}, F={len(mesh.faces)}, Vol={report.signed_volume:.6f} m^3, chi={report.euler_characteristic}, OpenFOAM=PASS")

def test_adversarial_stress():
    print("\n=== Testing Adversarial Edge Cases ===")
    auditor = SurfaceAuditor(auto_repair_normals=True)

    # 1. Punctured Cube (missing face)
    cube_verts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]
    ], dtype=np.float64)
    cube_faces = np.array([
        [0, 2, 1], [0, 3, 2], # bottom
        [4, 5, 6], [4, 6, 7], # top
        [0, 1, 5], [0, 5, 4], # front
        [2, 3, 7], [2, 7, 6], # back
        [0, 4, 7], [0, 7, 3], # left
        [1, 2, 6]             # right missing [1, 6, 5]
    ], dtype=np.int32)
    punctured_mesh = SurfaceMesh(vertices=cube_verts, faces=cube_faces)
    punctured_report = auditor.audit(punctured_mesh)
    assert punctured_report.is_watertight is False, "Punctured cube must NOT be watertight"
    assert punctured_report.open_edges_count == 3, f"Expected 3 open edges, got {punctured_report.open_edges_count}"
    assert punctured_report.passed_openfoam_surfacecheck is False
    print("  [PASS] Punctured cube correctly rejected with 3 open edges.")

    # 2. Non-manifold T-junction (3 faces on 1 edge)
    t_faces = np.vstack([cube_faces, np.array([[0, 2, 4]], dtype=np.int32)])
    t_mesh = SurfaceMesh(vertices=cube_verts, faces=t_faces)
    t_report = auditor.audit(t_mesh)
    assert t_report.is_manifold is False, "T-junction mesh must NOT be manifold"
    assert t_report.multi_edges_count >= 1, "T-junction must have at least 1 multi edge"
    assert t_report.passed_openfoam_surfacecheck is False
    print("  [PASS] Non-manifold T-junction correctly rejected.")

    # 3. Inverted Normals (inside-out cube)
    inverted_faces = np.array([
        [0, 1, 2], [0, 2, 3], # inverted bottom
        [4, 6, 5], [4, 7, 6], # inverted top
        [0, 5, 1], [0, 4, 5], # inverted front
        [2, 7, 3], [2, 6, 7], # inverted back
        [0, 7, 4], [0, 3, 7], # inverted left
        [1, 6, 2], [1, 5, 6]  # inverted right
    ], dtype=np.int32)
    inv_mesh = SurfaceMesh(vertices=cube_verts, faces=inverted_faces)
    inv_report = auditor.audit(inv_mesh)
    # Auditor with auto_repair_normals=True must detect negative volume and flip winding to outward
    assert inv_report.is_outward_oriented is True, "Auto-repair must restore outward orientation"
    assert inv_report.signed_volume > 0, f"Signed volume must be positive after repair, got {inv_report.signed_volume}"
    print(f"  [PASS] Inverted cube correctly auto-repaired to outward orientation (Vol={inv_report.signed_volume:.4f} m^3).")

    # 4. Self-intersecting triangles
    inter_verts = np.array([
        [-1, 0, 0], [1, 0, 0], [0, 0, 1],
        [0, -1, 0.5], [0, 1, 0.5], [0, 0, -0.5]
    ], dtype=np.float64)
    inter_faces = np.array([[0, 1, 2], [3, 4, 5]], dtype=np.int32)
    inter_mesh = SurfaceMesh(vertices=inter_verts, faces=inter_faces)
    inter_count = SurfaceAuditor._detect_self_intersections_aabb(inter_mesh.vertices, inter_mesh.faces)
    assert inter_count == 1, f"Expected 1 self-intersection, got {inter_count}"
    print("  [PASS] Moeller self-intersection test successfully detected intersecting triangle pair.")

if __name__ == "__main__":
    test_real_cad_samples()
    test_adversarial_stress()
    print("\n>>> ALL INDEPENDENT VICTORY AUDITOR CHECKS PASSED SUCCESSFULLY! <<<")
