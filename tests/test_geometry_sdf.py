"""Comprehensive Unit and Verification Tests for Milestone M1:
Geometry, SDF Engine, Analytical Primitives, CADMeshSDF, and CAD Feature Filtering.

Tests cover:
- Analytical Primitives: BoxSDF (2D & 3D), SphereSDF (2D & 3D), CylinderSDF (capped & infinite),
  PolygonSDF, and NACA4AirfoilSDF (0012 & 2412).
- CSG Operations: CSGUnion, CSGIntersection, CSGDifference, CSGComplement,
  smooth_min, smooth_max, and SDFEngine.from_dict.
- Vectorized gradient calculation, unit norm invariants, and Newton-Raphson boundary projection.
- CADMeshSDF: SurfaceMesh distance evaluation on TestCAD.obj (rotated cube), unit gradient verification,
  vectorized speed benchmark on BladeRev1CFD.obj (10,000 points in < 0.5s), and projection idempotency.
- CADFeatureFilter: Pruning orphan sketch nodes on TestCADSTPAP214.stp (Euler invariant chi=2),
  degenerate zero-area face removal, and dominant 2-manifold shell isolation.
- Curvature Refinement: 2D circle analytical curvature (1/R), 3D sphere mean curvature (1/R),
  planar boundary zero curvature.
- Proximity Refinement: Parallel channel gap thickness and sizing, open domain non-trigger,
  opposing normal compatibility filter.
- Adaptive Size Function: Combined bounds enforcement, Lipschitz grading smoother,
  vectorized 10,000 points throughput.
- Data Models: UnstructuredMesh validation (positive volume, divergence theorem,
  over-relaxed decomposition), SPHParticleSet, and MeshQualityReport.
"""

from pathlib import Path
import time
import numpy as np
import pytest

from backend.geometry.pipeline import CADGeometryEngine
from backend.geometry.models import SurfaceMesh
from backend.meshing.models import (
    UnstructuredMesh,
    SPHParticleSet,
    MeshQualityReport,
    SizingParameters,
)
from backend.meshing.geometry_sdf import (
    BaseSDF,
    BoxSDF,
    SphereSDF,
    CylinderSDF,
    PolygonSDF,
    NACA4AirfoilSDF,
    CSGUnion,
    CSGIntersection,
    CSGDifference,
    CSGComplement,
    SDFEngine,
    smooth_min,
    smooth_max,
    CADMeshSDF,
    CADFeatureFilter,
    CurvatureRefinement,
    ProximityRefinement,
    AdaptiveSizeFunction,
)

CAD_DIR = Path("resources/CADTestSamples")
TESTCAD_OBJ = CAD_DIR / "TestCAD.obj"
TESTCAD_STEP_AP214 = CAD_DIR / "TestCADSTPAP214.stp"
BLADE_OBJ = CAD_DIR / "ComplexGeometryExample" / "BladeRev1CFD.obj"


# ==============================================================================
# 1. Analytical Primitives Tests
# ==============================================================================


def test_box_sdf_2d_and_3d():
    """Verify BoxSDF signed distance at interior, boundary, face, and corner points in 2D and 3D."""
    # 2D Box: [0, 2] x [0, 1]
    box2d = BoxSDF(min_pt=[0.0, 0.0], max_pt=[2.0, 1.0])
    pts2d = np.array([
        [1.0, 0.5],    # center
        [0.0, 0.5],    # on boundary
        [2.5, 0.5],    # outside face
        [-1.0, -1.0],  # outside corner
    ])
    d2d = box2d.distance(pts2d)
    assert np.isclose(d2d[0], -0.5)
    assert np.isclose(d2d[1], 0.0)
    assert np.isclose(d2d[2], 0.5)
    assert np.isclose(d2d[3], np.sqrt(1.0**2 + 1.0**2))

    # 3D Cube: [-1, 1]^3
    cube3d = BoxSDF(min_pt=[-1.0, -1.0, -1.0], max_pt=[1.0, 1.0, 1.0])
    pts3d = np.array([
        [0.0, 0.0, 0.0],  # center
        [1.0, 0.0, 0.0],  # face
        [2.0, 0.0, 0.0],  # outside face
        [2.0, 2.0, 2.0],  # outside corner
    ])
    d3d = cube3d.distance(pts3d)
    assert np.isclose(d3d[0], -1.0)
    assert np.isclose(d3d[1], 0.0)
    assert np.isclose(d3d[2], 1.0)
    assert np.isclose(d3d[3], np.sqrt(3.0))

    # Single point query check
    assert np.isclose(cube3d.distance(np.array([0.0, 0.0, 0.0])), -1.0)

    # Unit gradient check
    grad = cube3d.gradient(np.array([[2.0, 0.0, 0.0]]))
    assert np.allclose(grad[0], [1.0, 0.0, 0.0])


def test_sphere_sdf_distances_and_analytical_gradient():
    """Verify SphereSDF distances and exact analytical unit normal gradient vector."""
    sphere = SphereSDF(center=[0.0, 0.0, 0.0], radius=1.0)
    pts = np.array([
        [0.0, 0.0, 0.0],  # center
        [1.0, 0.0, 0.0],  # surface
        [2.0, 0.0, 0.0],  # exterior
        [0.0, 3.0, 0.0],  # exterior Y
    ])
    d = sphere.distance(pts)
    assert np.isclose(d[0], -1.0)
    assert np.isclose(d[1], 0.0)
    assert np.isclose(d[2], 1.0)
    assert np.isclose(d[3], 2.0)

    # Gradient check on exterior/surface points
    g = sphere.gradient(pts[1:])
    assert np.allclose(g[0], [1.0, 0.0, 0.0])
    assert np.allclose(g[1], [1.0, 0.0, 0.0])
    assert np.allclose(g[2], [0.0, 1.0, 0.0])

    # Gradient norm must be exactly 1.0
    assert np.allclose(np.linalg.norm(g, axis=1), 1.0)

    # BoundingBox check
    bb = sphere.get_bounding_box()
    assert np.allclose(bb.min_pt, [-1.0, -1.0, -1.0])
    assert np.allclose(bb.max_pt, [1.0, 1.0, 1.0])


def test_cylinder_sdf_capped_and_infinite():
    """Verify finite capped cylinder and infinite cylinder distance evaluations."""
    # Finite capped cylinder: center=(0,0,0), radius=1, height=2, axis=Z
    cyl = CylinderSDF(center=[0.0, 0.0, 0.0], radius=1.0, height=2.0, axis=[0.0, 0.0, 1.0])
    pts = np.array([
        [0.0, 0.0, 0.0],  # center (inside)
        [1.0, 0.0, 0.0],  # radial wall
        [0.0, 0.0, 1.0],  # top cap
        [1.0, 0.0, 1.0],  # circular rim
        [2.0, 0.0, 2.0],  # exterior diagonal
    ])
    d = cyl.distance(pts)
    assert np.isclose(d[0], -1.0)
    assert np.isclose(d[1], 0.0)
    assert np.isclose(d[2], 0.0)
    assert np.isclose(d[3], 0.0)
    assert np.isclose(d[4], np.sqrt(1.0**2 + 1.0**2))

    # Infinite cylinder (height=None)
    cyl_inf = CylinderSDF(center=[0.0, 0.0, 0.0], radius=1.0, height=None, axis=[0.0, 0.0, 1.0])
    pts_inf = np.array([
        [0.0, 0.0, 100.0],  # on axis far away -> inside
        [2.0, 0.0, 100.0],  # radial distance 2 -> dist = 1.0
    ])
    d_inf = cyl_inf.distance(pts_inf)
    assert np.isclose(d_inf[0], -1.0)
    assert np.isclose(d_inf[1], 1.0)


def test_polygon_sdf_arbitrary_2d():
    """Verify PolygonSDF on a non-convex L-shaped 2D polygon."""
    # L-shaped polygon vertices
    verts = [
        (0.0, 0.0),
        (2.0, 0.0),
        (2.0, 1.0),
        (1.0, 1.0),
        (1.0, 2.0),
        (0.0, 2.0),
    ]
    poly = PolygonSDF(verts)

    pts = np.array([
        [0.5, 0.5],   # inside lower branch
        [0.5, 1.5],   # inside upper branch
        [1.5, 1.5],   # outside in the inner corner notch
        [0.0, 0.5],   # exactly on left edge
        [-1.0, -1.0], # outside corner
    ])
    d = poly.distance(pts)
    assert d[0] < 0.0  # inside
    assert d[1] < 0.0  # inside
    assert d[2] > 0.0  # outside notch
    assert np.isclose(d[2], 0.5)  # distance to (1,1) inner corner
    assert np.isclose(d[3], 0.0)  # boundary
    assert d[4] > 0.0  # outside


def test_naca4_airfoil_sdf_0012_and_2412():
    """Verify NACA 0012 (symmetric) and NACA 2412 (cambered) signed distance and bounds."""
    # NACA 0012 symmetric
    naca0012 = NACA4AirfoilSDF(code="0012", chord=1.0, origin=(0.0, 0.0))
    bb0012 = naca0012.get_bounding_box()
    assert bb0012.min_pt[0] <= 0.01
    assert bb0012.max_pt[0] >= 0.99

    # Mid-chord interior point for 0012: (0.5, 0.0) is on camber line inside
    d_in_0012 = naca0012.distance(np.array([[0.5, 0.0]]))
    assert d_in_0012[0] < 0.0

    # NACA 2412 cambered with angle of attack
    naca2412 = NACA4AirfoilSDF(code="2412", chord=1.0, origin=(0.0, 0.0), angle_deg=5.0)
    p_far = np.array([[0.5, 2.0]])
    assert naca2412.distance(p_far)[0] > 1.0


# ==============================================================================
# 2. CSG Boolean Operations & Smooth Blending
# ==============================================================================


def test_csg_boolean_exact_operations():
    """Verify Union, Intersection, Difference, and Complement CSG operations."""
    s1 = SphereSDF(center=[0.0, 0.0], radius=1.0)
    s2 = SphereSDF(center=[1.5, 0.0], radius=1.0)

    # Union
    union = CSGUnion(s1, s2, smooth_k=0.0)
    # Point at (0.75, 0.0): dist to s1 is -0.25, dist to s2 is -0.25 -> min is -0.25
    assert np.isclose(union.distance(np.array([0.75, 0.0]))[0], -0.25)
    # Point at (3.0, 0.0): dist to s1 is 2.0, dist to s2 is 0.5 -> min is 0.5
    assert np.isclose(union.distance(np.array([3.0, 0.0]))[0], 0.5)

    # Intersection
    inter = CSGIntersection(s1, s2, smooth_k=0.0)
    # Point at (0.75, 0.0): max is -0.25 (both inside)
    assert np.isclose(inter.distance(np.array([0.75, 0.0]))[0], -0.25)
    # Point at (-0.5, 0.0): dist to s1 is -0.5, dist to s2 is 1.0 -> max is 1.0 (outside intersection)
    assert np.isclose(inter.distance(np.array([-0.5, 0.0]))[0], 1.0)

    # Difference: s1 minus s2 (s1 and not s2)
    diff = CSGDifference(s1, s2, smooth_k=0.0)
    # Point at (-0.5, 0.0): dist to s1 is -0.5, -dist(s2) is -1.0 -> max is -0.5 (inside difference)
    assert np.isclose(diff.distance(np.array([-0.5, 0.0]))[0], -0.5)
    # Point at (0.75, 0.0): dist to s1 is -0.25, -dist(s2) is +0.25 -> max is +0.25 (outside difference)
    assert np.isclose(diff.distance(np.array([0.75, 0.0]))[0], 0.25)

    # Complement
    comp = CSGComplement(s1)
    # Center was -1.0 -> now +1.0
    assert np.isclose(comp.distance(np.array([0.0, 0.0]))[0], 1.0)


def test_csg_polynomial_smooth_blending():
    """Verify polynomial smooth blending filleting behavior (smooth_min / smooth_max)."""
    s1 = SphereSDF(center=[0.0, 0.0], radius=1.0)
    s2 = SphereSDF(center=[1.5, 0.0], radius=1.0)

    # Exact union at midpoint (0.75, 0.0) is -0.25
    u_exact = CSGUnion(s1, s2, smooth_k=0.0)
    d_exact = u_exact.distance(np.array([0.75, 0.0]))[0]

    # Smooth union creates a fillet that deepens the negative distance
    u_smooth = CSGUnion(s1, s2, smooth_k=0.2)
    d_smooth = u_smooth.distance(np.array([0.75, 0.0]))[0]

    assert d_smooth < d_exact
    assert np.isclose(d_smooth, d_exact - 0.25 * 0.2)  # theoretical shift: -k/4 for a==b


def test_sdf_engine_from_dict_declarative_tree():
    """Verify building composite SDF trees via declarative dictionary specifications."""
    spec = {
        "kind": "difference",
        "smooth": 0.0,
        "a": {"kind": "box", "min": [0.0, 0.0], "max": [4.0, 2.0]},
        "b": {"kind": "sphere", "center": [2.0, 1.0], "radius": 0.5},
    }
    tree = SDFEngine.from_dict(spec)
    assert isinstance(tree, CSGDifference)

    # Center of cylinder obstacle: inside obstacle, so OUTSIDE fluid domain (phi > 0)
    d_obstacle = tree.distance(np.array([[2.0, 1.0]]))
    assert d_obstacle[0] > 0.0

    # Interior fluid domain point: (0.5, 0.5)
    d_fluid = tree.distance(np.array([[0.5, 0.5]]))
    assert d_fluid[0] < 0.0


def test_newton_raphson_projection_to_boundary():
    """Verify iterative projection to boundary zero level set phi(x) == 0."""
    sphere = SphereSDF(center=[0.0, 0.0, 0.0], radius=1.0)
    query_pts = np.array([
        [2.5, 0.0, 0.0],
        [0.5, 0.5, 0.5],
        [0.0, 3.0, 0.0],
    ])
    proj = sphere.project_to_boundary(query_pts)

    # All projected points must have phi == 0.0 and distance from center == 1.0
    d_proj = sphere.distance(proj)
    assert np.allclose(d_proj, 0.0, atol=1e-9)
    assert np.allclose(np.linalg.norm(proj, axis=1), 1.0)


# ==============================================================================
# 3. CADMeshSDF & Real Geometry Verification
# ==============================================================================


def test_cad_mesh_sdf_evaluation_testcad_cube():
    """Verify CADMeshSDF continuous distance and normal gradient on rotated 200 mm cube."""
    engine = CADGeometryEngine()
    engine.load(TESTCAD_OBJ).scale(0.001)  # scale 200 mm -> 0.2 m
    mesh = engine.current_mesh

    sdf = CADMeshSDF.from_surface_mesh(mesh)

    # Centroid point inside: (0, 0, 0.1) m -> phi = -0.1 m
    phi_c, grad_c = sdf.evaluate(np.array([0.0, 0.0, 0.1]))
    assert pytest.approx(phi_c, abs=1e-5) == -0.1
    assert pytest.approx(np.linalg.norm(grad_c), abs=1e-10) == 1.0

    # Point outside: (0, 0, 0.3) m -> phi = +0.1 m
    phi_out, grad_out = sdf.evaluate(np.array([0.0, 0.0, 0.3]))
    assert pytest.approx(phi_out, abs=1e-5) == 0.1
    assert pytest.approx(grad_out[2], abs=1e-5) == 1.0  # Points along +Z normal

    # Points on surface: (0, 0, 0.2) m -> phi == 0.0
    phi_surf, grad_surf = sdf.evaluate(np.array([0.0, 0.0, 0.2]))
    assert abs(phi_surf) < 1e-6
    assert pytest.approx(grad_surf[2], abs=1e-5) == 1.0


def test_cad_mesh_sdf_blade_vectorized_throughput_and_unit_norm():
    """Verify performance requirement: 10,000 points evaluate in < 0.5s on 61k faces with unit gradient."""
    engine = CADGeometryEngine()
    engine.load(BLADE_OBJ).scale(0.001)
    mesh = engine.current_mesh
    assert len(mesh.faces) == 61660

    sdf = CADMeshSDF.from_surface_mesh(mesh, k_candidates=8)

    np.random.seed(42)
    pts10k = np.random.uniform(-0.1, 0.1, (10000, 3))

    t0 = time.perf_counter()
    phi, grad = sdf.evaluate(pts10k)
    elapsed = time.perf_counter() - t0

    assert elapsed < 0.50, f"Query took {elapsed:.4f}s >= 0.50s threshold"
    assert len(phi) == 10000
    assert grad.shape == (10000, 3)

    # Verify unit normal gradient norm invariant
    norms = np.linalg.norm(grad, axis=1)
    assert np.allclose(norms, 1.0, atol=1e-10)


def test_cad_mesh_sdf_projection_idempotency():
    """Verify projection to surface lands on zero level set: phi(x_proj) == 0."""
    engine = CADGeometryEngine()
    engine.load(TESTCAD_OBJ).scale(0.001)
    sdf = CADMeshSDF(engine.current_mesh)

    test_pts = np.array([
        [0.05, 0.05, 0.25],
        [-0.05, -0.05, -0.05],
        [0.15, 0.0, 0.1],
    ])
    proj_pts, _ = sdf.project_to_surface(test_pts)

    # Evaluate projected points
    proj_phi, _ = sdf.evaluate(proj_pts)
    assert np.all(np.abs(proj_phi) < 1e-6), f"Projected points not on zero level set: {proj_phi}"


# ==============================================================================
# 4. CAD Feature Filter & Sketch Line Sanitization
# ==============================================================================


def test_cad_feature_filter_orphan_vertices_step_ap214():
    """Verify 2D sketch curves and orphan nodes in TestCADSTPAP214.stp are purged."""
    engine = CADGeometryEngine()
    engine.load(TESTCAD_STEP_AP214, step_engine="gmsh")
    clean_mesh = CADFeatureFilter.filter_orphan_vertices(engine.current_mesh)

    # Pruned mesh must have zero unreferenced vertices
    used_v = len(set(clean_mesh.faces.flatten()))
    assert len(clean_mesh.vertices) == used_v

    # Audit must verify watertightness and Euler characteristic chi = 2
    report = engine.auditor.audit(clean_mesh)
    assert report.is_watertight is True
    assert report.open_edges_count == 0
    assert report.euler_characteristic == 2


def test_cad_feature_filter_degenerate_faces():
    """Verify removal of zero-area and collapsed triangle facets."""
    verts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [2.0, 0.0, 0.0],
    ])
    faces = np.array([
        [0, 1, 2],  # valid triangle
        [0, 0, 1],  # collapsed triangle v0 == v1
        [1, 1, 2],  # collapsed triangle v0 == v1
    ])
    mesh = SurfaceMesh(vertices=verts, faces=faces)
    clean = CADFeatureFilter.filter_degenerate_faces(mesh)

    assert len(clean.faces) == 1
    assert np.array_equal(clean.faces[0], [0, 1, 2])
    assert len(clean.vertices) == 3  # vertex 3 pruned as orphan


def test_cad_feature_filter_extract_dominant_manifold_shell():
    """Verify isolating dominant 2-manifold shell from disconnected auxiliary wireframe triangles."""
    # Main cube (12 faces)
    engine = CADGeometryEngine()
    engine.load(TESTCAD_OBJ)
    cube_mesh = engine.current_mesh

    # Add detached auxiliary wireframe triangle far away
    stray_v = np.array([[100.0, 100.0, 100.0], [101.0, 100.0, 100.0], [100.0, 101.0, 100.0]])
    combined_v = np.vstack([cube_mesh.vertices, stray_v])
    stray_f = np.array([[len(cube_mesh.vertices), len(cube_mesh.vertices) + 1, len(cube_mesh.vertices) + 2]])
    combined_f = np.vstack([cube_mesh.faces, stray_f])

    dirty_mesh = SurfaceMesh(vertices=combined_v, faces=combined_f)
    clean_mesh = CADFeatureFilter.extract_dominant_manifold_shell(dirty_mesh)

    assert len(clean_mesh.faces) == len(cube_mesh.faces)
    assert len(clean_mesh.vertices) == len(cube_mesh.vertices)


def test_cad_feature_filter_sanitize_pipeline():
    """Verify complete .sanitize() pipeline removes degenerates, isolates shell, and compacts."""
    verts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [10.0, 10.0, 10.0],
    ])
    faces = np.array([
        [0, 1, 2],
        [0, 0, 0],
    ])
    mesh = SurfaceMesh(vertices=verts, faces=faces)
    sanitized = CADFeatureFilter.sanitize(mesh)
    assert len(sanitized.faces) == 1
    assert len(sanitized.vertices) == 3


# ==============================================================================
# 5. Curvature Refinement Tests
# ==============================================================================


def make_circle_sdf(radius: float = 1.0, center=(0.0, 0.0)):
    """Creates a 2D analytical circle SDF where phi < 0 inside."""
    cx, cy = center

    def sdf(pts: np.ndarray):
        x = pts[:, 0] - cx
        y = pts[:, 1] - cy
        r = np.sqrt(x**2 + y**2)
        phi = r - radius
        r_safe = np.maximum(r, 1e-12)
        grad = np.column_stack([x / r_safe, y / r_safe])
        return phi, grad

    return sdf


def make_sphere_sdf(radius: float = 1.0, center=(0.0, 0.0, 0.0)):
    """Creates a 3D analytical sphere SDF where phi < 0 inside."""
    cx, cy, cz = center

    def sdf(pts: np.ndarray):
        x = pts[:, 0] - cx
        y = pts[:, 1] - cy
        z = pts[:, 2] - cz
        r = np.sqrt(x**2 + y**2 + z**2)
        phi = r - radius
        r_safe = np.maximum(r, 1e-12)
        grad = np.column_stack([x / r_safe, y / r_safe, z / r_safe])
        return phi, grad

    return sdf


def test_curvature_refinement_analytical_circle_2d():
    """Verify exact 2D circle curvature kappa = 1/R within 1%."""
    R = 2.0
    sdf = make_circle_sdf(radius=R)
    curv_engine = CurvatureRefinement(sdf_evaluator=sdf, dim=2)

    theta = np.linspace(0, 2 * np.pi, 20, endpoint=False)
    pts = np.column_stack([R * np.cos(theta), R * np.sin(theta)])

    kappa = curv_engine.evaluate_curvature(pts)
    expected_kappa = 1.0 / R
    assert np.all(np.abs(kappa - expected_kappa) / expected_kappa < 0.01)


def test_curvature_refinement_analytical_sphere_3d():
    """Verify exact 3D sphere mean curvature |H| = 1/R within 2%."""
    R = 1.0
    sdf = make_sphere_sdf(radius=R)
    curv_engine = CurvatureRefinement(sdf_evaluator=sdf, dim=3)

    pts = np.array([[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]])
    H = curv_engine.evaluate_curvature(pts)
    assert np.all(np.abs(H - 1.0 / R) < 0.02)


def test_curvature_refinement_flat_boundary_zero():
    """Verify flat boundary produces kappa ~ 0 and assigns h_max."""
    def flat_sdf(pts):
        phi = pts[:, 0] - 1.0
        grad = np.column_stack([np.ones(len(pts)), np.zeros(len(pts))])
        return phi, grad

    params = SizingParameters(h_min=0.01, h_max=0.20, alpha=0.20)
    curv_engine = CurvatureRefinement(sdf_evaluator=flat_sdf, params=params, dim=2)

    pts = np.array([[1.0, 0.0], [1.0, 0.5], [1.0, -0.5]])
    kappa = curv_engine.evaluate_curvature(pts)
    h_curv = curv_engine.evaluate_h_curv(pts)

    assert np.all(kappa < 1e-5)
    assert np.allclose(h_curv, params.h_max)


# ==============================================================================
# 6. Proximity Refinement Tests
# ==============================================================================


def make_channel_sdf(width: float = 0.04):
    """Creates a 2D parallel channel: 0 <= y <= width (phi < 0 inside)."""
    half_w = width / 2.0
    cy = half_w

    def sdf(pts: np.ndarray):
        y = pts[:, 1]
        dist_center = np.abs(y - cy)
        phi = dist_center - half_w  # < 0 in [0, width]
        grad_y = np.where(y >= cy, 1.0, -1.0)
        grad = np.column_stack([np.zeros_like(grad_y), grad_y])
        return phi, grad

    return sdf


def test_proximity_refinement_parallel_channel_gap():
    """Verify narrow channel gap distance d_gap == width and h_prox == beta * width."""
    W = 0.04
    beta = 0.25
    sdf = make_channel_sdf(width=W)
    params = SizingParameters(h_min=0.002, h_max=0.20, beta=beta)
    prox_engine = ProximityRefinement(sdf_evaluator=sdf, params=params, dim=2)

    pts = np.array([[0.1, 0.005], [0.1, 0.020], [0.1, 0.035]])
    d_gap = prox_engine.evaluate_gap_distance(pts)
    h_prox = prox_engine.evaluate_h_prox(pts)

    assert np.all(np.abs(d_gap - W) < 0.003)
    assert np.allclose(h_prox, beta * W, atol=0.001)


def test_proximity_refinement_open_domain_no_false_trigger():
    """Verify open domain without opposing walls assigns h_max."""
    def open_sdf(pts):
        phi = -pts[:, 0]  # < 0 for x > 0
        grad = np.column_stack([-np.ones(len(pts)), np.zeros(len(pts))])
        return phi, grad

    params = SizingParameters(h_max=0.150, beta=0.25)
    prox_engine = ProximityRefinement(sdf_evaluator=open_sdf, params=params, dim=2)

    pts = np.array([[1.0, 0.0], [2.0, 0.0]])
    h_prox = prox_engine.evaluate_h_prox(pts)
    assert np.allclose(h_prox, params.h_max)


def test_proximity_refinement_opposing_normal_compatibility():
    """Verify ray hitting a non-opposing perpendicular wall does not trigger false proximity."""
    # L-shaped level set where wall at x=0 and wall at y=0 meet at 90 degrees
    def corner_sdf(pts):
        # Interior is x > 0 and y > 0
        phi = -np.minimum(pts[:, 0], pts[:, 1])
        grad_x = np.where(pts[:, 0] <= pts[:, 1], -1.0, 0.0)
        grad_y = np.where(pts[:, 0] > pts[:, 1], -1.0, 0.0)
        return phi, np.column_stack([grad_x, grad_y])

    params = SizingParameters(h_max=0.20, theta_opp_threshold=-0.50)
    prox_engine = ProximityRefinement(sdf_evaluator=corner_sdf, params=params, dim=2)

    # Point near x=0 wall: inward normal points along +X, hitting nothing opposing
    pts = np.array([[0.01, 1.0]])
    h_prox = prox_engine.evaluate_h_prox(pts)
    assert np.isclose(h_prox[0], params.h_max)


# ==============================================================================
# 7. Adaptive Size Function Tests
# ==============================================================================


def test_adaptive_size_function_combined_strict_bounds():
    """Verify combined sizing satisfies h_min <= h(x) <= h_max everywhere."""
    W = 0.04
    sdf = make_channel_sdf(width=W)
    params = SizingParameters(h_min=0.005, h_max=0.150, alpha=0.20, beta=0.25)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=sdf, params=params, dim=2)

    rng = np.random.default_rng(42)
    random_pts = rng.uniform(-0.1, 0.1, size=(500, 2))

    h = size_fn.evaluate(random_pts)
    assert np.all(h >= params.h_min - 1e-12)
    assert np.all(h <= params.h_max + 1e-12)

    # Breakdown check
    breakdown = size_fn.evaluate_with_breakdown(random_pts[:10])
    assert "h" in breakdown
    assert "h_curv" in breakdown
    assert "h_prox" in breakdown


def test_adaptive_size_function_lipschitz_grading_smoother():
    """Verify Lipschitz grading smoother limits spatial sizing gradient: ||grad h|| <= G."""
    params = SizingParameters(h_min=0.01, h_max=0.20, grading=0.30)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=make_circle_sdf(), params=params, dim=2)

    x = np.linspace(0.0, 1.0, 50)
    pts = np.column_stack([x, np.zeros_like(x)])
    sizes = np.where(x < 0.5, 0.01, 0.20)

    graded_sizes = size_fn.apply_grading_filter(pts, sizes, max_iterations=5)

    dx = np.diff(x)
    dh = np.abs(np.diff(graded_sizes))
    assert np.all(dh / dx <= params.grading * 1.15)


def test_adaptive_size_function_throughput_10k_points():
    """Verify throughput: 10,000 points evaluate in < 0.50 s."""
    sdf = make_circle_sdf(radius=1.0)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=sdf, dim=2)

    rng = np.random.default_rng(123)
    pts = rng.uniform(-1.0, 1.0, size=(10000, 2))

    t0 = time.perf_counter()
    h = size_fn.evaluate(pts)
    elapsed = time.perf_counter() - t0

    assert len(h) == 10000
    assert elapsed < 0.50, f"Vectorized evaluation took {elapsed:.4f}s >= 0.50s"


# ==============================================================================
# 8. UnstructuredMesh & Multiphysics Data Models Tests
# ==============================================================================


def test_unstructured_mesh_data_model_validation():
    """Verify UnstructuredMesh invariant checking and failure detection."""
    # Construct a valid single-cell cube mesh
    pts = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    # 6 boundary faces
    owner = np.array([0, 0, 0, 0, 0, 0], dtype=np.int64)
    neighbour = np.array([-1, -1, -1, -1, -1, -1], dtype=np.int64)
    face_areas = np.array([
        [0, 0, -1], [0, 0, 1],
        [0, -1, 0], [0, 1, 0],
        [-1, 0, 0], [1, 0, 0],
    ], dtype=np.float64)
    face_centres = np.array([
        [0.5, 0.5, 0.0], [0.5, 0.5, 1.0],
        [0.5, 0.0, 0.5], [0.5, 1.0, 0.5],
        [0.0, 0.5, 0.5], [1.0, 0.5, 0.5],
    ], dtype=np.float64)
    cell_centres = np.array([[0.5, 0.5, 0.5]], dtype=np.float64)
    cell_volumes = np.array([1.0], dtype=np.float64)
    delta_f = np.copy(face_areas)
    k_f = np.zeros_like(face_areas)

    mesh = UnstructuredMesh(
        points=pts,
        cells=[np.arange(8)],
        owner=owner,
        neighbour=neighbour,
        face_areas=face_areas,
        face_centres=face_centres,
        cell_centres=cell_centres,
        cell_volumes=cell_volumes,
        delta_f=delta_f,
        k_f=k_f,
        boundaries={"wall": (0, 6)},
    )
    assert mesh.validate() is True
    assert mesh.num_cells == 1
    assert mesh.num_faces == 6
    assert mesh.num_boundary_faces == 6
    assert mesh.num_internal_faces == 0

    # Quality report check
    report = mesh.compute_quality_report()
    assert report.is_valid is True
    assert np.isclose(report.total_volume, 1.0)
    assert report.max_closed_cell_divergence < 1e-12

    # Invariant failure: negative volume
    mesh.cell_volumes[0] = -1.0
    with pytest.raises(ValueError, match="non-positive volume"):
        mesh.validate()

    # Restore volume, break non-orthogonal decomposition: delta_f + k_f != S_f
    mesh.cell_volumes[0] = 1.0
    mesh.k_f[0] = np.array([10.0, 0.0, 0.0])
    with pytest.raises(ValueError, match="Non-orthogonal decomposition identity failed"):
        mesh.validate()


def test_sph_particle_set_data_model():
    """Verify SPHParticleSet data container initialization and bounding box."""
    pos = np.array([[0.0, 0.0, 0.0], [1.0, 1.0, 1.0]], dtype=np.float64)
    vel = np.zeros_like(pos)
    masses = np.array([0.001, 0.001], dtype=np.float64)
    densities = np.array([1000.0, 1000.0], dtype=np.float64)
    volumes = masses / densities
    h = np.array([0.05, 0.05], dtype=np.float64)

    particles = SPHParticleSet(
        positions=pos,
        velocities=vel,
        masses=masses,
        densities=densities,
        volumes=volumes,
        smoothing_lengths=h,
    )
    assert particles.num_particles == 2
    bb = particles.get_bounding_box()
    assert np.allclose(bb.min_pt, [0.0, 0.0, 0.0])
    assert np.allclose(bb.max_pt, [1.0, 1.0, 1.0])


# ==============================================================================
# 8. Milestone M1 Remediation Verification Unit Tests
# ==============================================================================


def test_proximity_refinement_external_flow_spheres():
    """Verify gap detection between two spheres in an external flow domain (phi > 0)."""
    s1 = SphereSDF(center=[-1.05, 0.0, 0.0], radius=1.0)
    s2 = SphereSDF(center=[1.05, 0.0, 0.0], radius=1.0)
    union = CSGUnion(s1, s2)
    params = SizingParameters(h_min=0.005, h_max=0.20, beta=0.25)
    prox = ProximityRefinement(sdf_evaluator=union.evaluate, params=params, dim=3)

    gap = prox.evaluate_gap_distance(np.array([[0.0, 0.0, 0.0]]))
    h_prox = prox.evaluate_h_prox(np.array([[0.0, 0.0, 0.0]]))
    assert np.isclose(gap[0], 0.10, atol=0.01)
    assert np.isclose(h_prox[0], 0.25 * 0.10, atol=0.005)


def test_sdf_engine_evaluate_interface_contract():
    """Verify SDFEngine.evaluate interface contract per PROJECT.md on instance & class level."""
    s = SphereSDF(center=[0.0, 0.0, 0.0], radius=1.0)
    engine = SDFEngine(s)
    pts = np.array([[2.0, 0.0, 0.0], [0.0, 2.0, 0.0]])

    phi, grad = engine.evaluate(pts)
    assert np.allclose(phi, [1.0, 1.0])
    assert np.allclose(grad[0], [1.0, 0.0, 0.0])
    assert np.allclose(grad[1], [0.0, 1.0, 0.0])

    # Class-level convenience
    phi_c, grad_c = SDFEngine.evaluate(s, pts)
    assert np.allclose(phi, phi_c)
    assert np.allclose(grad, grad_c)


def test_cad_feature_filter_patches_preservation_and_reindexing():
    """Verify SurfaceMesh patches are preserved and re-indexed across feature filtering."""
    verts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [99.0, 99.0, 99.0],  # orphan vertex
    ])
    faces = np.array([
        [0, 1, 2],  # face 0 (valid)
        [0, 2, 3],  # face 1 (valid)
        [0, 0, 1],  # face 2 (degenerate)
    ])
    patches = {
        "inlet": np.array([0]),
        "outlet": np.array([1, 2]),
    }
    mesh = SurfaceMesh(vertices=verts, faces=faces, patches=patches)
    clean = CADFeatureFilter.filter_degenerate_faces(mesh)

    assert len(clean.faces) == 2
    assert "inlet" in clean.patches
    assert "outlet" in clean.patches
    assert np.array_equal(clean.patches["inlet"], [0])
    assert np.array_equal(clean.patches["outlet"], [1])  # face 2 pruned, face 1 preserved


def test_cad_mesh_sdf_inside_is_negative_false_boundary_normal():
    """Verify outward gradient orientation when inside_is_negative == False on boundary."""
    mesh = SurfaceMesh(
        vertices=np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]),
        faces=np.array([[0, 1, 2]]),
    )
    sdf_true = CADMeshSDF(mesh, inside_is_negative=True)
    sdf_false = CADMeshSDF(mesh, inside_is_negative=False)

    pt = np.array([0.2, 0.2, 0.0])
    phi_t, grad_t = sdf_true.evaluate(pt)
    phi_f, grad_f = sdf_false.evaluate(pt)

    assert abs(phi_t) < 1e-12
    assert abs(phi_f) < 1e-12
    assert np.allclose(grad_t, [0.0, 0.0, 1.0])
    assert np.allclose(grad_f, [0.0, 0.0, -1.0])


def test_cad_mesh_sdf_scale_sensitivity_micro_elements_fixed():
    """Verify sub-millimeter elements (L <= 0.1 mm) project without distortion."""
    L = 1e-4
    v0 = np.array([0.0, 0.0, 0.0])
    v1 = np.array([L, 0.0, 0.0])
    v2 = np.array([L / 2.0, L * np.sqrt(3.0) / 2.0, 0.0])
    mesh = SurfaceMesh(vertices=np.array([v0, v1, v2]), faces=np.array([[0, 1, 2]]))
    sdf = CADMeshSDF(mesh, k_candidates=1)

    centroid = (v0 + v1 + v2) / 3.0
    q = np.array([centroid[0], centroid[1], 0.1 * L])
    phi_q, grad_q = sdf.evaluate(q)

    assert abs(phi_q - 0.1 * L) / (0.1 * L) < 1e-5
    assert np.isclose(grad_q[2], 1.0, atol=1e-5)


def test_cad_mesh_sdf_extreme_coords_overflow_guarded():
    """Verify extreme coordinates (>= 1e160) do not crash with IndexError."""
    mesh = SurfaceMesh(
        vertices=np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]),
        faces=np.array([[0, 1, 2]]),
    )
    sdf = CADMeshSDF(mesh, k_candidates=1)
    phi, grad = sdf.evaluate(np.array([1e160, 0.0, 0.0]))
    assert np.isfinite(phi)
    assert np.isclose(np.linalg.norm(grad), 1.0, atol=1e-10)


def test_cad_feature_filter_strict_manifold_and_vertex_welding():
    """Verify non-manifold fin is pruned and split seams are welded."""
    # Cube with duplicate vertices and an attached fin
    cube = SurfaceMesh(
        vertices=np.array([
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
            [0.5, -0.5, 0.5],  # fin vertex 8
        ]),
        faces=np.array([
            [0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
            [0, 1, 5], [0, 5, 4], [2, 3, 7], [2, 7, 6],
            [0, 4, 7], [0, 7, 3], [1, 2, 6], [1, 6, 5],
            [0, 1, 8],  # non-manifold fin sharing edge (0, 1)
        ]),
    )
    clean = CADFeatureFilter.sanitize(cube)
    assert len(clean.faces) == 12  # fin pruned
    assert len(clean.vertices) == 8
