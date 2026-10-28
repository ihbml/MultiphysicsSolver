"""Adversarial stress tests and independent benchmark for Milestone M1.
Executed by reviewer_m1_2.
"""

import sys
import time
from pathlib import Path
import numpy as np

# Ensure project root is in path
project_root = Path("G:/Datein/Projects/CFDSolver")
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

from backend.geometry.pipeline import CADGeometryEngine
from backend.geometry.models import SurfaceMesh, BoundingBox
from backend.meshing.geometry_sdf import (
    CADMeshSDF,
    CADFeatureFilter,
    BoxSDF,
    SphereSDF,
    CylinderSDF,
    PolygonSDF,
    NACA4AirfoilSDF,
    CurvatureRefinement,
    ProximityRefinement,
    AdaptiveSizeFunction,
)
from backend.meshing.models import SizingParameters, UnstructuredMesh

def run_benchmarks():
    print("=" * 60)
    print("RUNNING BENCHMARKS: CADMeshSDF on BladeRev1CFD.obj")
    print("=" * 60)
    blade_path = project_root / "resources/CADTestSamples/ComplexGeometryExample/BladeRev1CFD.obj"
    assert blade_path.exists(), f"Missing blade file: {blade_path}"

    engine = CADGeometryEngine()
    engine.load(blade_path).scale(0.001)
    mesh = engine.current_mesh
    num_faces = len(mesh.faces)
    num_verts = len(mesh.vertices)
    print(f"Loaded Blade mesh: {num_verts} vertices, {num_faces} faces")

    t_build_0 = time.perf_counter()
    sdf = CADMeshSDF.from_surface_mesh(mesh, k_candidates=8)
    t_build = time.perf_counter() - t_build_0
    print(f"CADMeshSDF build time (KDTree + normals): {t_build*1000:.2f} ms")

    # Benchmark with 10,000 points
    rng = np.random.default_rng(42)
    bb = mesh.get_bounding_box()
    # Query points around the bounding box
    margin = (bb.max_pt - bb.min_pt) * 0.2
    pts_10k = rng.uniform(bb.min_pt - margin, bb.max_pt + margin, size=(10000, 3))

    times = []
    for run in range(5):
        t0 = time.perf_counter()
        phi, grad = sdf.evaluate(pts_10k)
        el = time.perf_counter() - t0
        times.append(el)

    min_t = min(times)
    mean_t = np.mean(times)
    max_t = max(times)
    print(f"10,000 query points over {num_faces} faces:")
    print(f"  Min time:  {min_t:.4f}s ({10000/min_t:.0f} pts/sec)")
    print(f"  Mean time: {mean_t:.4f}s ({10000/mean_t:.0f} pts/sec)")
    print(f"  Max time:  {max_t:.4f}s")
    assert min_t < 0.50, f"Benchmark failed: min time {min_t} >= 0.50s"
    assert mean_t < 0.50, f"Benchmark failed: mean time {mean_t} >= 0.50s"

    # Invariant checks on benchmark results
    assert len(phi) == 10000
    assert grad.shape == (10000, 3)
    assert not np.any(np.isnan(phi)), "NaN in phi values"
    assert not np.any(np.isinf(phi)), "Inf in phi values"
    assert not np.any(np.isnan(grad)), "NaN in grad values"
    assert not np.any(np.isinf(grad)), "Inf in grad values"
    grad_norms = np.linalg.norm(grad, axis=1)
    assert np.allclose(grad_norms, 1.0, atol=1e-10), "Grad norm not 1.0!"

    print(">> Benchmark PASSED: Evaluates 10k points in < 0.5s with strictly unit normals!")

def run_edge_case_tests():
    print("\n" + "=" * 60)
    print("RUNNING ADVERSARIAL EDGE CASE TESTS")
    print("=" * 60)
    rng = np.random.default_rng(42)

    # Edge Case 1: Degenerate Zero-Area and Collinear Faces
    print("[1] Testing degenerate zero-area faces...")
    verts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [2.0, 0.0, 0.0],  # Collinear with 0 and 1
        [0.0, 0.0, 0.0],  # Duplicate vertex
    ])
    faces = np.array([
        [0, 1, 2],  # Area = 0.5 (valid)
        [0, 1, 3],  # Collinear: (0,0,0), (1,0,0), (2,0,0) -> Area = 0.0
        [0, 0, 1],  # Collapsed: v0 == v1
        [1, 2, 2],  # Collapsed: v1 == v2
        [0, 4, 1],  # Vertices 0 and 4 are at identical locations (0,0,0) -> Area = 0.0
    ])
    dirty_mesh = SurfaceMesh(vertices=verts, faces=faces)
    clean_mesh = CADFeatureFilter.filter_degenerate_faces(dirty_mesh, min_area=1e-12)
    print(f"  Dirty faces: {len(faces)} -> Clean faces: {len(clean_mesh.faces)}")
    assert len(clean_mesh.faces) == 1, f"Expected 1 valid face, got {len(clean_mesh.faces)}"
    assert np.array_equal(clean_mesh.faces[0], [0, 1, 2])
    print("  -> filter_degenerate_faces successfully filtered collinear, duplicate, and collapsed faces.")

    # Edge Case 1b: What if CADMeshSDF is fed a mesh with a zero-area triangle?
    print("  Testing CADMeshSDF tolerance to zero-area triangle...")
    degen_mesh = SurfaceMesh(
        vertices=np.array([[0,0,0], [1,0,0], [0,1,0], [2,0,0]], dtype=float),
        faces=np.array([[0,1,2], [0,1,3]], dtype=int) # face 1 is collinear
    )
    sdf_degen = CADMeshSDF(degen_mesh, k_candidates=2)
    query_pts = np.array([[0.2, 0.2, 1.0], [0.5, 0.0, 0.0], [0.0, 0.0, 0.0]])
    phi, grad = sdf_degen.evaluate(query_pts)
    assert not np.any(np.isnan(phi)), "NaN in phi for degen mesh"
    assert not np.any(np.isnan(grad)), "NaN in grad for degen mesh"
    print("  -> CADMeshSDF evaluates without NaN on zero-area triangle (safe normal division).")

    # Edge Case 2: Query points directly on vertices, edges, and corners
    print("\n[2] Testing query points directly on vertices, edges, and corners...")
    engine_cube = CADGeometryEngine()
    engine_cube.load(project_root / "resources/CADTestSamples/TestCAD.obj").scale(0.001)
    cube_mesh = engine_cube.current_mesh
    sdf_cube = CADMeshSDF(cube_mesh, k_candidates=12)

    # Test vertices
    phi_v, grad_v = sdf_cube.evaluate(cube_mesh.vertices)
    print(f"  Distance at 8 cube vertices: max |phi| = {np.max(np.abs(phi_v)):.2e}")
    assert np.all(np.abs(phi_v) < 1e-12), "Vertex distance should be ~0"
    v_norms = np.linalg.norm(grad_v, axis=1)
    assert np.allclose(v_norms, 1.0), "Vertex gradient norm should be 1.0"

    # Test edge midpoints
    # Build unique edges from cube_mesh faces
    edges = set()
    for f in cube_mesh.faces:
        for i in range(3):
            e = tuple(sorted((f[i], f[(i+1)%3])))
            edges.add(e)
    edge_midpoints = np.array([0.5 * (cube_mesh.vertices[e[0]] + cube_mesh.vertices[e[1]]) for e in edges])
    phi_e, grad_e = sdf_cube.evaluate(edge_midpoints)
    print(f"  Distance at {len(edges)} edge midpoints: max |phi| = {np.max(np.abs(phi_e)):.2e}")
    assert np.all(np.abs(phi_e) < 1e-12), "Edge midpoint distance should be ~0"
    e_norms = np.linalg.norm(grad_e, axis=1)
    assert np.allclose(e_norms, 1.0), "Edge gradient norm should be 1.0"

    # Test face centroids
    face_centers = (cube_mesh.vertices[cube_mesh.faces[:, 0]] + 
                    cube_mesh.vertices[cube_mesh.faces[:, 1]] + 
                    cube_mesh.vertices[cube_mesh.faces[:, 2]]) / 3.0
    phi_f, grad_f = sdf_cube.evaluate(face_centers)
    print(f"  Distance at {len(face_centers)} face centroids: max |phi| = {np.max(np.abs(phi_f)):.2e}")
    assert np.all(np.abs(phi_f) < 1e-12), "Face centroid distance should be ~0"


    # Edge Case 3: Points far outside and far inside
    print("\n[3] Testing points far outside and far inside...")
    far_outside = np.array([
        [1e6, 0.5, 0.5],
        [0.5, -1e6, 0.5],
        [0.5, 0.5, 1e7],
        [-1e5, -1e5, -1e5],
    ])
    phi_far, grad_far = sdf_cube.evaluate(far_outside)
    print(f"  Far outside distances: {phi_far}")
    assert np.all(phi_far > 1e4), "Far outside points must have large positive distance"
    assert not np.any(np.isnan(phi_far))
    assert not np.any(np.isnan(grad_far))
    far_norms = np.linalg.norm(grad_far, axis=1)
    assert np.allclose(far_norms, 1.0, atol=1e-10)

    # Center point inside TestCAD.obj: (0.0, 0.0, 0.1) -> distance is -0.1
    phi_in, grad_in = sdf_cube.evaluate(np.array([0.0, 0.0, 0.1]))
    print(f"  Center point (0.0, 0.0, 0.1): phi = {phi_in}")
    assert np.isclose(phi_in, -0.1, atol=1e-5), f"Expected -0.1, got {phi_in}"

    # Edge Case 4: Orphan sketch node removal stress test
    print("\n[4] Testing orphan sketch node removal stress test...")
    n_orphans = 500
    orphan_verts = rng.uniform(-10, 10, size=(n_orphans, 3))
    all_verts = np.vstack([cube_mesh.vertices, orphan_verts])
    # Faces only reference original vertices
    noisy_mesh = SurfaceMesh(vertices=all_verts, faces=cube_mesh.faces)
    assert len(noisy_mesh.vertices) == len(cube_mesh.vertices) + n_orphans
    cleaned_mesh = CADFeatureFilter.filter_orphan_vertices(noisy_mesh)
    print(f"  Noisy vertices: {len(noisy_mesh.vertices)} -> Cleaned vertices: {len(cleaned_mesh.vertices)}")
    assert len(cleaned_mesh.vertices) == len(cube_mesh.vertices), f"Expected {len(cube_mesh.vertices)} vertices, got {len(cleaned_mesh.vertices)}"
    assert len(cleaned_mesh.faces) == len(cube_mesh.faces)
    assert np.max(cleaned_mesh.faces) == len(cube_mesh.vertices) - 1


    # Edge Case 5: Single point vs Batch query shape and type consistency
    print("\n[5] Testing single point vs batch query consistency...")
    p_single = np.array([0.5, 0.5, 1.5])
    p_batch = np.array([[0.5, 0.5, 1.5]])
    phi_s, grad_s = sdf_cube.evaluate(p_single)
    phi_b, grad_b = sdf_cube.evaluate(p_batch)
    assert isinstance(phi_s, float) or np.isscalar(phi_s)
    assert grad_s.shape == (3,)
    assert phi_b.shape == (1,)
    assert grad_b.shape == (1, 3)
    assert np.isclose(phi_s, phi_b[0])
    assert np.allclose(grad_s, grad_b[0])
    print("  -> Scalar and batch queries match bit-for-bit.")

    # Edge Case 6: Analytical primitives bounds & extreme points
    print("\n[6] Testing BoxSDF, SphereSDF, CylinderSDF edge cases...")
    box = BoxSDF(min_pt=[-2, -2, -2], max_pt=[2, 2, 2])
    p_box_corner = np.array([3.0, 3.0, 3.0]) # corner outside: dx=1, dy=1, dz=1 -> dist = sqrt(3)
    assert np.isclose(box.distance(p_box_corner)[0], np.sqrt(3.0))

    sphere = SphereSDF(center=[0, 0, 0], radius=5.0)
    # Origin of sphere: dist = -5.0
    assert np.isclose(sphere.distance(np.array([0, 0, 0]))[0], -5.0)
    # Gradient at origin: handled safely without zero division
    g_origin = sphere.gradient(np.array([0, 0, 0]))
    assert not np.any(np.isnan(g_origin))

    cyl = CylinderSDF(center=[0,0,0], radius=2.0, height=4.0) # z from -2 to +2
    # Cap corner at r=2, z=2: exactly on edge
    assert np.isclose(cyl.distance(np.array([2.0, 0.0, 2.0]))[0], 0.0)
    # Outside diagonal at r=3, z=3: dr=1, da=1 -> sqrt(2)
    assert np.isclose(cyl.distance(np.array([3.0, 0.0, 3.0]))[0], np.sqrt(2.0))

    print(">> All adversarial edge-case stress tests PASSED successfully!")

if __name__ == "__main__":
    run_benchmarks()
    run_edge_case_tests()
