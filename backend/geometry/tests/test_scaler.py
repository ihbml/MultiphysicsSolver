"""Unit tests for GeometryScaler."""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.transformation.scaler import GeometryScaler

SAMPLES_DIR = Path("resources/CADTestSamples")


def test_scaler_mm_to_m_transformation():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    scaler = GeometryScaler(default_scale_factor=0.001)
    scaled = scaler.scale(mesh, scale_factor=0.001, verify_invariants=True)

    # CAD BBox: [-115.845593, 115.845593] x [-115.845593, 115.845593] x [0, 200] mm
    # CFD BBox: [-0.115846, 0.115846] x [-0.115846, 0.115846] x [0, 0.2] m
    bbox = scaled.get_bounding_box()
    assert pytest.approx(bbox.min_pt[0], abs=1e-5) == -0.115846
    assert pytest.approx(bbox.max_pt[0], abs=1e-5) == 0.115846
    assert pytest.approx(bbox.min_pt[1], abs=1e-5) == -0.115846
    assert pytest.approx(bbox.max_pt[1], abs=1e-5) == 0.115846
    assert pytest.approx(bbox.min_pt[2], abs=1e-5) == 0.0
    assert pytest.approx(bbox.max_pt[2], abs=1e-5) == 0.200000

    # Volume invariant: 200^3 mm^3 = 8,000,000 mm^3 -> 0.008 m^3
    assert scaled.metadata.get("units") == "m"
    assert scaled.metadata.get("scale_factor") == 0.001


def test_scaler_negative_factor_rejected():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    scaler = GeometryScaler()
    with pytest.raises(ValueError):
        scaler.scale(mesh, scale_factor=-0.001)
    with pytest.raises(ValueError):
        scaler.scale(mesh, scale_factor=0.0)
