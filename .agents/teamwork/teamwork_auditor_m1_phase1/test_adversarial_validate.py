import sys
import os
sys.path.insert(0, os.getcwd())
import numpy as np
import pytest
from backend.meshing.models import UnstructuredMesh, CellType

def test_validate_adversarial():
    # Construct a valid single cube mesh in [0, 1]^3
    # Vertices
    points = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0]
    ], dtype=np.float64)
    
    cells = [np.arange(8, dtype=np.int64)]
    owner = np.zeros(6, dtype=np.int64)
    neighbour = np.full(6, -1, dtype=np.int64)
    
    # 6 boundary faces
    # 0: z=0, 1: z=1, 2: y=0, 3: y=1, 4: x=0, 5: x=1
    face_areas = np.array([
        [0.0, 0.0, -1.0],
        [0.0, 0.0, 1.0],
        [0.0, -1.0, 0.0],
        [0.0, 1.0, 0.0],
        [-1.0, 0.0, 0.0],
        [1.0, 0.0, 0.0]
    ], dtype=np.float64)
    
    face_centres = np.array([
        [0.5, 0.5, 0.0],
        [0.5, 0.5, 1.0],
        [0.5, 0.0, 0.5],
        [0.5, 1.0, 0.5],
        [0.0, 0.5, 0.5],
        [1.0, 0.5, 0.5]
    ], dtype=np.float64)
    
    cell_centres = np.array([[0.5, 0.5, 0.5]], dtype=np.float64)
    cell_volumes = np.array([1.0], dtype=np.float64)
    delta_f = np.zeros((6, 3), dtype=np.float64)
    k_f = np.zeros((6, 3), dtype=np.float64)
    boundaries = {"wall": (0, 6)}
    faces = [
        np.array([0, 3, 2, 1]),
        np.array([4, 5, 6, 7]),
        np.array([0, 1, 5, 4]),
        np.array([2, 3, 7, 6]),
        np.array([0, 4, 7, 3]),
        np.array([1, 2, 6, 5]),
    ]
    
    def make_mesh(cc=None, cv=None, pts=None):
        return UnstructuredMesh(
            points=points.copy() if pts is None else pts,
            cells=[c.copy() for c in cells],
            owner=owner.copy(),
            neighbour=neighbour.copy(),
            face_areas=face_areas.copy(),
            face_centres=face_centres.copy(),
            cell_centres=cell_centres.copy() if cc is None else cc,
            cell_volumes=cell_volumes.copy() if cv is None else cv,
            delta_f=delta_f.copy(),
            k_f=k_f.copy(),
            boundaries=boundaries.copy(),
            faces=[f.copy() for f in faces],
            topological_dim=3
        )
    
    # 1. Clean mesh must PASS
    mesh = make_mesh()
    assert mesh.validate() is True
    print("Test 1 (Clean mesh): PASSED")
    
    # 2. Check 6 failure: Centroid outside bounding box
    mesh_outside = make_mesh(cc=np.array([[1.5, 0.5, 0.5]]))
    try:
        mesh_outside.validate()
        raise AssertionError("Failed to catch centroid outside bbox!")
    except ValueError as e:
        assert "lies outside its bounding box" in str(e)
        print(f"Test 2 (Centroid outside bbox): Caught expected error -> {e}")
        
    # 3. Check 7 failure: Centroid inside bbox, but violates Gauss divergence balance
    # Shift centroid from 0.5 to 0.7 (still inside [0, 1]^3 bbox, but M_cells != M_boundary)
    mesh_unbalanced = make_mesh(cc=np.array([[0.7, 0.5, 0.5]]))
    try:
        mesh_unbalanced.validate()
        raise AssertionError("Failed to catch Gauss divergence theorem balance violation!")
    except ValueError as e:
        assert "Global centroid divergence theorem balance violated" in str(e)
        print(f"Test 3 (Gauss divergence balance): Caught expected error -> {e}")

    # 4. Check 6 elongation failure
    # Extreme elongated points
    pts_elongated = points.copy()
    pts_elongated[:, 0] *= 2000.0 # Aspect ratio 2000 > max_aspect_ratio 1000
    mesh_elongated = make_mesh(pts=pts_elongated, cc=np.array([[1000.0, 0.5, 0.5]]))
    try:
        mesh_elongated.validate()
        raise AssertionError("Failed to catch cell elongation exceeding max_aspect_ratio!")
    except ValueError as e:
        assert "elongation" in str(e) and "exceeds maximum allowed" in str(e)
        print(f"Test 4 (Elongation): Caught expected error -> {e}")

if __name__ == "__main__":
    test_validate_adversarial()
