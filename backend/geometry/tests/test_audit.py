"""Unit tests for SurfaceAuditor and QualityMetrics."""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.audit.quality_metrics import QualityMetrics
from backend.geometry.audit.surface_auditor import SurfaceAuditor
from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.stl_parser import STLParser

SAMPLES_DIR = Path("resources/CADTestSamples")


def test_surface_auditor_watertight_and_euler():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    auditor = SurfaceAuditor()
    report = auditor.audit(mesh)

    assert report.is_watertight is True
    assert report.is_manifold is True
    assert report.open_edges_count == 0
    assert report.multi_edges_count == 0
    assert report.euler_characteristic == 2
    assert report.normal_zones_count == 1
    assert report.is_outward_oriented is True
    assert report.signed_volume > 0.0
    assert report.self_intersections_count == 0
    assert report.passed_openfoam_surfacecheck is True


def test_surface_auditor_auto_repair_normals():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    # Intentionally invert face winding so normals point inward: (v0, v1, v2) -> (v0, v2, v1)
    mesh.faces[:, [1, 2]] = mesh.faces[:, [2, 1]]
    mesh.normals = mesh.compute_face_normals()

    # Pre-check: signed volume must be negative
    vol_pre = SurfaceAuditor.compute_signed_volume(mesh.vertices, mesh.faces)
    assert vol_pre < 0.0

    # With auto_repair_normals=True, auditor should invert winding back to positive
    auditor = SurfaceAuditor(auto_repair_normals=True)
    report = auditor.audit(mesh)

    assert report.signed_volume > 0.0
    assert report.is_outward_oriented is True
    assert report.passed_openfoam_surfacecheck is True


def test_surface_auditor_open_boundary_detection():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    # Remove 1 face to create an open hole
    mesh.faces = mesh.faces[:-1]
    mesh.normals = mesh.compute_face_normals()

    auditor = SurfaceAuditor()
    report = auditor.audit(mesh)

    assert report.is_watertight is False
    assert report.open_edges_count == 3  # The 3 edges of the missing face
    assert report.euler_characteristic != 2
    assert report.passed_openfoam_surfacecheck is False


def test_quality_metrics_equilateral_quality():
    # Equilateral triangle in XY plane: (0, 0), (1, 0), (0.5, sqrt(3)/2)
    h = np.sqrt(3.0) / 2.0
    vertices = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.5, h, 0.0],
    ])
    faces = np.array([[0, 1, 2]], dtype=np.int32)
    mesh = SurfaceMesh(vertices=vertices, faces=faces)

    q_min, q_max, hist, bad = QualityMetrics.compute_equilateral_quality(mesh)
    assert pytest.approx(q_min, rel=1e-5) == 1.0
    assert pytest.approx(q_max, rel=1e-5) == 1.0
    assert len(bad) == 0
    # Last bin (0.95 - 1.0) must contain the face
    assert hist[-1] == 1


def test_quality_metrics_illegal_faces():
    # Triangle with duplicate vertex index and triangle with zero area
    vertices = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [2.0, 0.0, 0.0],  # Collinear points!
    ])
    faces = np.array([
        [0, 0, 1],  # Duplicate vertex index
        [0, 1, 2],  # Collinear (zero area)
    ], dtype=np.int32)
    mesh = SurfaceMesh(vertices=vertices, faces=faces)

    illegal = QualityMetrics.find_illegal_faces(mesh)
    assert len(illegal) == 2
