"""Pytest configuration, authoritative reference ground truth, and synthetic mesh fixtures."""

import os
from pathlib import Path
from typing import Dict, Tuple
import numpy as np
import pytest

# Project root directory
PROJECT_ROOT = Path(__file__).resolve().parent.parent
SAMPLES_DIR = PROJECT_ROOT / "resources" / "CADTestSamples"

# Authoritative analytical properties of reference CAD cube:
# Side = 200 mm, rotated 10 deg counter-clockwise around Z axis, base at Z=0.
CUBE_SIDE_MM = 200.0
CUBE_SIDE_M = 0.200
ROTATION_ANGLE_DEG = 10.0
ROTATION_ANGLE_RAD = np.radians(ROTATION_ANGLE_DEG)

# Analytical exact extents
# cos(10) * 100 + sin(10) * 100 = 98.4807753 + 17.3648178 = 115.8455931 mm
HALF_SPAN_MM = (CUBE_SIDE_MM / 2.0) * (np.cos(ROTATION_ANGLE_RAD) + np.sin(ROTATION_ANGLE_RAD))
HALF_SPAN_M = HALF_SPAN_MM * 0.001

CAD_BBOX_MIN = np.array([-HALF_SPAN_MM, -HALF_SPAN_MM, 0.0], dtype=np.float64)
CAD_BBOX_MAX = np.array([HALF_SPAN_MM, HALF_SPAN_MM, CUBE_SIDE_MM], dtype=np.float64)

CFD_BBOX_MIN = np.array([-HALF_SPAN_M, -HALF_SPAN_M, 0.0], dtype=np.float64)
CFD_BBOX_MAX = np.array([HALF_SPAN_M, HALF_SPAN_M, CUBE_SIDE_M], dtype=np.float64)

ANALYTICAL_VOLUME_CAD = CUBE_SIDE_MM ** 3  # 8,000,000 mm^3
ANALYTICAL_VOLUME_CFD = CUBE_SIDE_M ** 3   # 0.008 m^3

ANALYTICAL_SURFACE_AREA_CAD = 6.0 * (CUBE_SIDE_MM ** 2)  # 240,000 mm^2
ANALYTICAL_SURFACE_AREA_CFD = 6.0 * (CUBE_SIDE_M ** 2)   # 0.24 m^2

EULER_CHI_GENUS_0 = 2


@pytest.fixture(scope="session")
def cad_samples_dir() -> Path:
    """Path to real-world CAD test samples directory."""
    assert SAMPLES_DIR.exists(), f"Samples dir not found: {SAMPLES_DIR}"
    return SAMPLES_DIR


@pytest.fixture(scope="session")
def obj_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCAD.obj."""
    p = cad_samples_dir / "TestCAD.obj"
    assert p.exists()
    return p


@pytest.fixture(scope="session")
def stl_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCAD.stl."""
    p = cad_samples_dir / "TestCAD.stl"
    assert p.exists()
    return p


@pytest.fixture(scope="session")
def stp_ap203_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCADSTPAP203.stp."""
    p = cad_samples_dir / "TestCADSTPAP203.stp"
    assert p.exists()
    return p


@pytest.fixture(scope="session")
def stp_ap214_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCADSTPAP214.stp."""
    p = cad_samples_dir / "TestCADSTPAP214.stp"
    assert p.exists()
    return p


@pytest.fixture(scope="session")
def stp_ap242_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCADSTPAP242.stp."""
    p = cad_samples_dir / "TestCADSTPAP242.stp"
    assert p.exists()
    return p


@pytest.fixture(scope="session")
def prt_sample_path(cad_samples_dir: Path) -> Path:
    """Path to TestCAD.prt."""
    p = cad_samples_dir / "TestCAD.prt"
    assert p.exists()
    return p


# Synthetic Geometry Generators for Boundary & Corner Testing


def create_unit_cube_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates a canonical watertight, 2-manifold genus-0 unit cube [0, 1]^3.

    Returns:
        (vertices, faces) where vertices shape is (8, 3), faces shape is (12, 3).
        Normals point consistently outward.
    """
    vertices = np.array(
        [
            [0.0, 0.0, 0.0],  # 0
            [1.0, 0.0, 0.0],  # 1
            [1.0, 1.0, 0.0],  # 2
            [0.0, 1.0, 0.0],  # 3
            [0.0, 0.0, 1.0],  # 4
            [1.0, 0.0, 1.0],  # 5
            [1.0, 1.0, 1.0],  # 6
            [0.0, 1.0, 1.0],  # 7
        ],
        dtype=np.float64,
    )

    # 12 triangular faces with outward counter-clockwise winding
    faces = np.array(
        [
            # Bottom face (z = 0, normal [0, 0, -1])
            [0, 2, 1],
            [0, 3, 2],
            # Top face (z = 1, normal [0, 0, 1])
            [4, 5, 6],
            [4, 6, 7],
            # Front face (y = 0, normal [0, -1, 0])
            [0, 1, 5],
            [0, 5, 4],
            # Back face (y = 1, normal [0, 1, 0])
            [2, 3, 7],
            [2, 7, 6],
            # Left face (x = 0, normal [-1, 0, 0])
            [0, 4, 7],
            [0, 7, 3],
            # Right face (x = 1, normal [1, 0, 0])
            [1, 2, 6],
            [1, 6, 5],
        ],
        dtype=np.int32,
    )
    return vertices, faces


def create_open_cube_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates a cube with 1 missing face (open hole, boundary edges)."""
    vertices, faces = create_unit_cube_arrays()
    # Remove the last triangle
    return vertices, faces[:-1]


def create_inverted_cube_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates a cube with inverted winding (normals pointing inward, V_signed < 0)."""
    vertices, faces = create_unit_cube_arrays()
    inverted_faces = faces.copy()
    inverted_faces[:, [1, 2]] = inverted_faces[:, [2, 1]]
    return vertices, inverted_faces


def create_t_junction_non_manifold_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates two cubes sharing a single edge, creating an edge with 4 incident faces."""
    v1, f1 = create_unit_cube_arrays()
    v2, f2 = create_unit_cube_arrays()
    # Translate second cube along [1, 1, 0] so edge (1,1,0)-(1,1,1) is shared
    v2_shifted = v2 + np.array([1.0, 0.0, 0.0])
    # Vertex indices for shared edge: v1[1] and v1[5] correspond to v2[0] and v2[4]
    # Merge unique vertices
    all_v = np.vstack([v1, v2_shifted])
    all_f = np.vstack([f1, f2 + len(v1)])
    return all_v, all_f


def create_degenerate_face_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates a mesh containing collapsed (duplicate vertices) and 0-area triangles."""
    vertices = np.array(
        [
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.5, 0.5, 0.0],
            [0.0, 0.0, 0.0],  # Duplicate of 0
        ],
        dtype=np.float64,
    )
    faces = np.array(
        [
            [0, 1, 2],  # valid
            [0, 0, 1],  # collapsed: duplicate index
            [0, 1, 3],  # zero area: vertex 0 and 3 are coincident
        ],
        dtype=np.int32,
    )
    return vertices, faces


def create_sliver_triangle_arrays() -> Tuple[np.ndarray, np.ndarray]:
    """Generates a sliver triangle with extremely high aspect ratio (Q < 1e-5)."""
    vertices = np.array(
        [
            [0.0, 0.0, 0.0],
            [1000.0, 0.0, 0.0],
            [500.0, 1e-6, 0.0],  # Height = 1e-6, base = 1000
        ],
        dtype=np.float64,
    )
    faces = np.array([[0, 1, 2]], dtype=np.int32)
    return vertices, faces
