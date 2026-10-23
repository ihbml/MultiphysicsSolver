"""End-to-end and ASME V&V verification tests for CADGeometryEngine."""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.pipeline import CADGeometryEngine

SAMPLES_DIR = Path("resources/CADTestSamples")

ALL_SAMPLES = [
    "TestCAD.obj",
    "TestCAD.stl",
    "TestCADSTPAP203.stp",
    "TestCADSTPAP214.stp",
    "TestCADSTPAP242.stp",
    "TestCAD.prt",
]


@pytest.mark.parametrize("sample_name", ALL_SAMPLES)
def test_pipeline_end_to_end_on_all_samples(sample_name, tmp_path):
    src = SAMPLES_DIR / sample_name
    assert src.exists(), f"Sample file missing: {src}"

    engine = CADGeometryEngine(auto_repair_normals=True)
    exported_path, report = engine.ingest_and_prepare(
        input_file=src,
        case_dir=tmp_path,
        scale_factor=0.001,
    )

    # 1. Output file verification
    assert exported_path.exists()
    assert exported_path.stat().st_size > 0
    assert exported_path.parent == tmp_path / "constant" / "triSurface"

    # 2. Topological 2-manifold closedness verification
    assert report.is_watertight is True
    assert report.is_manifold is True
    assert report.open_edges_count == 0
    assert report.multi_edges_count == 0
    assert report.euler_characteristic == 2

    # 3. Normal vector outward orientation verification
    assert report.is_outward_oriented is True
    assert report.normal_zones_count == 1
    assert report.signed_volume > 0.0

    # 4. OpenFOAM surfaceCheck compliance
    assert report.passed_openfoam_surfacecheck is True

    # 5. ASME V&V analytical cube metrics (scaled to meters)
    # Side length L = 0.2 m => Volume = 0.008 m^3, Area = 0.24 m^2
    assert pytest.approx(report.signed_volume, rel=1e-4) == 0.008

    # Bounding box spans: delta_X = delta_Y = 0.231691 m, delta_Z = 0.200000 m
    bbox_span = report.bounding_box_max - report.bounding_box_min
    assert pytest.approx(bbox_span[0], abs=1e-5) == 0.231691
    assert pytest.approx(bbox_span[1], abs=1e-5) == 0.231691
    assert pytest.approx(bbox_span[2], abs=1e-5) == 0.200000

    # 6. snappyHexMeshDict configuration verification
    dict_block = engine.generate_snappyhexmesh_dict()
    assert "geometry" in dict_block
    assert "type triSurfaceMesh;" in dict_block


def test_fluent_pipeline_api(tmp_path):
    src = SAMPLES_DIR / "TestCAD.stl"
    engine = (
        CADGeometryEngine()
        .load(src)
        .scale(0.001)
    )
    report = engine.audit()
    assert report.is_watertight is True
    out_path = engine.export_openfoam(tmp_path)
    assert out_path.exists()
