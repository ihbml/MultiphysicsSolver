"""Unit tests for STLParser, OBJParser, and STEPParser on real reference CAD samples."""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.parsers.obj_parser import OBJParser
from backend.geometry.parsers.step_parser import STEPParser
from backend.geometry.parsers.stl_parser import STLParser

SAMPLES_DIR = Path("resources/CADTestSamples")


def test_stl_parser_binary():
    path = SAMPLES_DIR / "TestCAD.stl"
    assert path.exists(), f"Sample missing: {path}"

    parser = STLParser()
    mesh = parser.parse(path)

    # 12 facets welded into exactly 8 unique cube corners
    assert len(mesh.vertices) == 8
    assert len(mesh.faces) == 12
    bbox = mesh.get_bounding_box()
    assert pytest.approx(bbox.span[0], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[1], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[2], rel=1e-5) == 200.0


def test_stl_parser_ascii(tmp_path):
    # Create an ASCII STL
    ascii_stl = """solid cube
  facet normal 0.0 0.0 1.0
    outer loop
      vertex 0.0 0.0 0.0
      vertex 10.0 0.0 0.0
      vertex 0.0 10.0 0.0
    endloop
  endfacet
endsolid cube
"""
    p = tmp_path / "test_ascii.stl"
    p.write_text(ascii_stl, encoding="utf-8")

    parser = STLParser()
    mesh = parser.parse(p)
    assert len(mesh.vertices) == 3
    assert len(mesh.faces) == 1
    assert "cube" in mesh.patches


def test_obj_parser():
    path = SAMPLES_DIR / "TestCAD.obj"
    assert path.exists(), f"Sample missing: {path}"

    parser = OBJParser()
    mesh = parser.parse(path)

    assert len(mesh.vertices) == 8
    assert len(mesh.faces) == 12
    assert "TestCAD" in mesh.patches
    bbox = mesh.get_bounding_box()
    assert pytest.approx(bbox.span[0], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[1], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[2], rel=1e-5) == 200.0


def test_obj_parser_quad_triangulation(tmp_path):
    # Create an OBJ with a quad face
    quad_obj = """v 0 0 0
v 10 0 0
v 10 10 0
v 0 10 0
f 1 2 3 4
"""
    p = tmp_path / "quad.obj"
    p.write_text(quad_obj, encoding="utf-8")

    parser = OBJParser()
    mesh = parser.parse(p)

    assert len(mesh.vertices) == 4
    # 1 quad triangulates into 2 triangles
    assert len(mesh.faces) == 2


@pytest.mark.parametrize("step_file", ["TestCADSTPAP203.stp", "TestCADSTPAP214.stp", "TestCADSTPAP242.stp"])
def test_step_parser_brep_engine(step_file):
    path = SAMPLES_DIR / step_file
    assert path.exists(), f"Sample missing: {path}"

    parser = STEPParser(engine="brep")
    mesh = parser.parse(path)

    assert len(mesh.vertices) == 8
    assert len(mesh.faces) == 12
    assert mesh.metadata.get("units") == "mm"
    bbox = mesh.get_bounding_box()
    assert pytest.approx(bbox.span[0], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[1], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[2], rel=1e-5) == 200.0


@pytest.mark.parametrize("step_file", ["TestCADSTPAP203.stp", "TestCADSTPAP214.stp", "TestCADSTPAP242.stp"])
def test_step_parser_gmsh_engine(step_file):
    path = SAMPLES_DIR / step_file
    assert path.exists(), f"Sample missing: {path}"

    parser = STEPParser(engine="gmsh", min_edge=0.005, max_edge=0.05)
    mesh = parser.parse(path)

    assert len(mesh.vertices) >= 8
    assert len(mesh.faces) >= 12
    bbox = mesh.get_bounding_box()
    assert pytest.approx(bbox.span[0], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[1], rel=1e-5) == 231.69119
    assert pytest.approx(bbox.span[2], rel=1e-5) == 200.0
