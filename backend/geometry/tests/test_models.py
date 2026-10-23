"""Unit tests for models.py (BoundingBox, SurfaceMesh, AuditReport)."""

import numpy as np
import pytest

from backend.geometry.models import BoundingBox, SurfaceMesh, AuditReport


def test_bounding_box_creation_and_properties():
    pts = np.array([
        [-10.0, -20.0, 0.0],
        [10.0, 20.0, 50.0],
    ])
    bbox = BoundingBox.from_points(pts)

    assert np.allclose(bbox.min_pt, [-10.0, -20.0, 0.0])
    assert np.allclose(bbox.max_pt, [10.0, 20.0, 50.0])
    assert np.allclose(bbox.span, [20.0, 40.0, 50.0])
    assert np.allclose(bbox.center, [0.0, 0.0, 25.0])
    assert pytest.approx(bbox.volume) == 20.0 * 40.0 * 50.0
    assert pytest.approx(bbox.diagonal) == np.linalg.norm([20.0, 40.0, 50.0])


def test_bounding_box_scaling():
    bbox = BoundingBox(min_pt=np.array([0.0, 0.0, 0.0]), max_pt=np.array([100.0, 100.0, 100.0]))
    scaled = bbox.scale(0.001)

    assert np.allclose(scaled.min_pt, [0.0, 0.0, 0.0])
    assert np.allclose(scaled.max_pt, [0.1, 0.1, 0.1])
    assert np.allclose(scaled.span, [0.1, 0.1, 0.1])


def test_bounding_box_validation_error():
    with pytest.raises(ValueError):
        BoundingBox(min_pt=np.array([1.0, 2.0]), max_pt=np.array([3.0, 4.0]))


def test_surface_mesh_creation_and_normals():
    # Unit right-angled triangle in XY plane at Z=0
    vertices = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
    ])
    faces = np.array([[0, 1, 2]], dtype=np.int32)
    mesh = SurfaceMesh(vertices=vertices, faces=faces)

    assert mesh.normals is not None
    assert len(mesh.normals) == 1
    # Cross product (1,0,0) x (0,1,0) = (0,0,1)
    assert np.allclose(mesh.normals[0], [0.0, 0.0, 1.0])


def test_surface_mesh_copy():
    vertices = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]])
    faces = np.array([[0, 1, 2]], dtype=np.int32)
    mesh = SurfaceMesh(vertices=vertices, faces=faces, patches={"wall": np.array([0])})

    copied = mesh.copy()
    copied.vertices[0, 0] = 999.0
    assert mesh.vertices[0, 0] == 0.0
    assert copied.vertices[0, 0] == 999.0
