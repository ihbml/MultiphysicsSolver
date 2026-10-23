"""Unit tests for OpenFOAMExporter."""

from pathlib import Path
import numpy as np
import pytest

from backend.geometry.exporter.openfoam_exporter import OpenFOAMExporter
from backend.geometry.parsers.stl_parser import STLParser

SAMPLES_DIR = Path("resources/CADTestSamples")


def test_openfoam_export_ascii_stl(tmp_path):
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    case_dir = tmp_path / "case"
    out_stl = OpenFOAMExporter.export(
        mesh=mesh,
        case_dir=case_dir,
        filename="cube.stl",
        export_format="stl",
        binary=False,
    )

    assert out_stl.exists()
    assert out_stl.parent == case_dir / "constant" / "triSurface"
    assert out_stl.stat().st_size > 0

    content = out_stl.read_text(encoding="utf-8")
    assert content.startswith("solid ")
    assert "endsolid " in content
    assert "facet normal " in content
    assert "vertex " in content


def test_openfoam_export_binary_stl(tmp_path):
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    case_dir = tmp_path / "case"
    out_stl = OpenFOAMExporter.export(
        mesh=mesh,
        case_dir=case_dir,
        filename="cube_bin.stl",
        export_format="stl",
        binary=True,
    )

    assert out_stl.exists()
    # 84 + 50 * 12 = 684 bytes
    assert out_stl.stat().st_size == 684


def test_openfoam_export_obj(tmp_path):
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)

    case_dir = tmp_path / "case"
    out_obj = OpenFOAMExporter.export(
        mesh=mesh,
        case_dir=case_dir,
        filename="cube.obj",
        export_format="obj",
    )

    assert out_obj.exists()
    content = out_obj.read_text(encoding="utf-8")
    assert "v " in content
    assert "f " in content


def test_snappyhexmesh_dict_block_generation():
    path = SAMPLES_DIR / "TestCAD.stl"
    mesh = STLParser().parse(path)
    mesh.patches = {"wall_bottom": np.array([0, 1]), "wall_top": np.array([2, 3])}

    block = OpenFOAMExporter.generate_snappyhexmesh_dict_block(
        mesh=mesh,
        filename="cube.stl",
        geometry_name="cube",
    )

    assert "geometry" in block
    assert "cube.stl" in block
    assert "type triSurfaceMesh;" in block
    assert "name cube;" in block
    assert "wall_bottom" in block
    assert "wall_top" in block
