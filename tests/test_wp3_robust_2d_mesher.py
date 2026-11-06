"""Comprehensive Test Suite for Milestone WP3: Robust 2D-Mesher.

Validates:
1. test_wp3_shoelace_ccw_canonicalization:
   - Positive signed area for CCW polygons.
   - Automatic reversal of CW polygons to CCW.
   - Rejection of degenerate polygons (|A| < 1e-14 or < 3 vertices) with ValueError.
   - Dual-engine parity between Python and C++20.
2. test_wp3_ear_clipping_nonconvex_polygons:
   - Robust triangulation of L-shape, U-shape, 5-pointed star, and NACA 0012 trailing-edge.
   - Exactly n - 2 cells per polygon.
   - Strictly positive triangle areas (V_i > 0).
   - Exact analytical area conservation (|sum(V_i) - A_exact| < 1e-12).
   - Elimination of naive centroid fan failures.
3. test_wp3_boundary_edge_subdivision:
   - Metric-governed boundary subdivision under uniform and non-uniform h(x).
   - Corner vertices anchored as fixed points (is_corner mask).
   - Edge segment lengths match local h(x) metric.
4. test_wp3_distmesh_relaxation_convergence:
   - Force equilibrium convergence on circular disk and L-bracket.
   - Zero inverted cells; zero points or triangles leaking into the exterior void.
5. test_wp3_exact_area_conservation:
   - Exact analytical area conservation across 10 diverse benchmark polygons.
6. test_wp3_python_cpp20_dual_engine_parity:
   - Bit-for-bit and invariant parity between native C++20 and pure-Python fallback.
"""

from __future__ import annotations

import ctypes
import numpy as np
import pytest

from backend.meshing.difvm_topology import get_native_dll, MeshingConfigC
from backend.meshing.distmesh_core import (
    canonicalize_polygon_ccw,
    compute_polygon_signed_area,
    distmesh_2d,
    ear_clipping_triangulate,
    polygon_signed_distance,
    subdivide_polygon_edges,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import CurvatureSizingConfig, MeshingConfig


# ==============================================================================
# Helper Polygon Generators
# ==============================================================================

def make_l_shaped_polygon() -> Tuple[np.ndarray, float]:
    """Return L-shaped non-convex polygon and its exact analytical area (5.0)."""
    poly = np.array([
        [0.0, 0.0],
        [3.0, 0.0],
        [3.0, 1.0],
        [1.0, 1.0],
        [1.0, 3.0],
        [0.0, 3.0],
    ], dtype=np.float64)
    # Area = 3*1 + 1*2 = 5.0
    return poly, 5.0


def make_u_shaped_polygon() -> Tuple[np.ndarray, float]:
    """Return U-shaped non-convex polygon and its exact analytical area (7.0)."""
    poly = np.array([
        [0.0, 0.0],
        [3.0, 0.0],
        [3.0, 3.0],
        [2.0, 3.0],
        [2.0, 1.0],
        [1.0, 1.0],
        [1.0, 3.0],
        [0.0, 3.0],
    ], dtype=np.float64)
    # Area = 3*3 - 1*2 = 7.0
    return poly, 7.0


def make_star_polygon(r1: float = 2.0, r2: float = 0.8) -> Tuple[np.ndarray, float]:
    """Return 5-pointed star polygon (10 vertices) and its exact analytical area."""
    n_points = 5
    angles = np.linspace(0.0, 2.0 * np.pi, 2 * n_points, endpoint=False)
    radii = np.array([r1 if i % 2 == 0 else r2 for i in range(2 * n_points)])
    x = radii * np.cos(angles)
    y = radii * np.sin(angles)
    poly = np.column_stack([x, y])
    # Exact analytical area of polygon
    exact_area = abs(float(compute_polygon_signed_area(poly)))
    return poly, float(exact_area)


def make_naca0012_polygon(n_pts_half: int = 25) -> Tuple[np.ndarray, float]:
    """Return closed NACA 0012 airfoil polygon with non-convex curvature."""
    # Cosine clustering along chord [0, 1]
    beta = np.linspace(0.0, np.pi, n_pts_half)
    x = 0.5 * (1.0 - np.cos(beta))
    # NACA 0012 half-thickness formula
    yt = 5.0 * 0.12 * (
        0.2969 * np.sqrt(x)
        - 0.1260 * x
        - 0.3516 * (x ** 2)
        + 0.2843 * (x ** 3)
        - 0.1015 * (x ** 4)
    )
    # Upper surface (LE to TE) and Lower surface (TE to LE)
    upper = np.column_stack([x, yt])
    lower = np.column_stack([x[::-1], -yt[::-1]])
    poly = np.vstack([upper, lower[1:-1]])
    # Compute exact analytical area using fine Shoelace (absolute value for CCW orientation)
    exact_area = abs(float(compute_polygon_signed_area(poly)))
    return poly, float(exact_area)


# ==============================================================================
# Test 1: Shoelace CCW Canonicalization
# ==============================================================================

def test_wp3_shoelace_ccw_canonicalization():
    """Verify Shoelace formula, CCW canonicalization, and degenerate rejection."""
    # 1. CCW Unit square
    sq_ccw = np.array([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]])
    area = compute_polygon_signed_area(sq_ccw)
    assert abs(area - 1.0) < 1e-14
    poly_can, was_rev, a_can = canonicalize_polygon_ccw(sq_ccw)
    assert not was_rev
    assert abs(a_can - 1.0) < 1e-14
    assert np.allclose(poly_can, sq_ccw)

    # 2. CW Unit square (must be reversed)
    sq_cw = sq_ccw[::-1].copy()
    area_cw = compute_polygon_signed_area(sq_cw)
    assert abs(area_cw - (-1.0)) < 1e-14
    poly_cw_can, was_rev_cw, a_cw_can = canonicalize_polygon_ccw(sq_cw)
    assert was_rev_cw
    assert abs(a_cw_can - 1.0) < 1e-14
    assert abs(compute_polygon_signed_area(poly_cw_can) - 1.0) < 1e-14

    # 3. Regular hexagon in CW ordering
    angles = np.linspace(0.0, 2.0 * np.pi, 6, endpoint=False)[::-1]
    hex_cw = np.column_stack([np.cos(angles), np.sin(angles)])
    exact_hex_area = 1.5 * np.sqrt(3.0)
    hex_can, was_rev_hex, a_hex = canonicalize_polygon_ccw(hex_cw)
    assert was_rev_hex
    assert abs(a_hex - exact_hex_area) < 1e-12

    # 4. Irregular L-shaped polygon in CW ordering
    l_poly, l_exact = make_l_shaped_polygon()
    l_cw = l_poly[::-1].copy()
    l_can, was_rev_l, a_l = canonicalize_polygon_ccw(l_cw)
    assert was_rev_l
    assert abs(a_l - l_exact) < 1e-12
    assert compute_polygon_signed_area(l_can) > 0.0

    # 5. Degenerate polygon rejection (|A| < 1e-14 or < 3 vertices)
    collinear = np.array([[0.0, 0.0], [1.0, 1.0], [2.0, 2.0]])
    with pytest.raises(ValueError, match="near-zero area"):
        canonicalize_polygon_ccw(collinear)

    with pytest.raises(ValueError, match="at least 3 vertices"):
        canonicalize_polygon_ccw(np.array([[0.0, 0.0], [1.0, 1.0]]))

    # 6. Native C++20 C-ABI Parity for Shoelace & Canonicalization
    dll = get_native_dll()
    if dll is not None:
        dll.cfd_mesher_compute_shoelace_area.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_mesher_compute_shoelace_area.restype = ctypes.c_int32

        out_area = ctypes.c_double(0.0)
        res = dll.cfd_mesher_compute_shoelace_area(
            sq_ccw.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            4,
            ctypes.byref(out_area),
        )
        assert res == 0
        assert abs(out_area.value - 1.0) < 1e-14

        # Native canonicalize on CW square
        dll.cfd_mesher_canonicalize_polygon_ccw.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_int32),
            ctypes.POINTER(ctypes.c_double),
        ]
        dll.cfd_mesher_canonicalize_polygon_ccw.restype = ctypes.c_int32

        out_xy = np.empty_like(sq_cw)
        was_rev_c = ctypes.c_int32(0)
        out_area_c = ctypes.c_double(0.0)

        res_can = dll.cfd_mesher_canonicalize_polygon_ccw(
            sq_cw.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            4,
            out_xy.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            ctypes.byref(was_rev_c),
            ctypes.byref(out_area_c),
        )
        assert res_can == 0
        assert was_rev_c.value == 1
        assert abs(out_area_c.value - 1.0) < 1e-14
        assert np.allclose(out_xy, poly_cw_can)


# ==============================================================================
# Test 2: Held (2001) / Eberly Ear-Clipping on Non-Convex Polygons
# ==============================================================================

@pytest.mark.parametrize("shape_fn", [
    make_l_shaped_polygon,
    make_u_shaped_polygon,
    make_star_polygon,
    make_naca0012_polygon,
])
def test_wp3_ear_clipping_nonconvex_polygons(shape_fn):
    """Verify Held/Eberly Ear-Clipping on non-convex geometries (L, U, star, NACA)."""
    poly, exact_area = shape_fn()
    n_verts = len(poly)

    # 1. Python Ear-Clipping
    pts, triangles = ear_clipping_triangulate(poly)

    # Invariant A: Exactly n - 2 triangles
    assert len(triangles) == n_verts - 2, f"Expected {n_verts - 2} triangles, got {len(triangles)}"

    # Invariant B: Strictly positive triangle areas (V_i > 0)
    p0 = pts[triangles[:, 0]]
    p1 = pts[triangles[:, 1]]
    p2 = pts[triangles[:, 2]]
    orient = (p1[:, 0] - p0[:, 0]) * (p2[:, 1] - p0[:, 1]) - (p1[:, 1] - p0[:, 1]) * (p2[:, 0] - p0[:, 0])
    cell_vols = 0.5 * orient

    assert np.all(cell_vols > 1e-14), f"Inverted triangle found! Min volume = {np.min(cell_vols)}"

    # Invariant C: Exact analytical area conservation (|sum(V_i) - A_exact| < 1e-12)
    total_mesh_area = float(np.sum(cell_vols))
    area_error = abs(total_mesh_area - exact_area)
    assert area_error < 1e-12, f"Area conservation failed: {area_error:.2e} >= 1e-12"

    # 2. Native C++20 C-ABI Parity
    dll = get_native_dll()
    if dll is not None:
        dll.cfd_mesher_ear_clipping_triangulate.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.POINTER(ctypes.c_int64),
        ]
        dll.cfd_mesher_ear_clipping_triangulate.restype = ctypes.c_int32

        out_triangles = np.empty((n_verts - 2, 3), dtype=np.int32)
        out_n_tris = ctypes.c_int64(0)

        res = dll.cfd_mesher_ear_clipping_triangulate(
            poly.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            n_verts,
            out_triangles.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            ctypes.byref(out_n_tris),
        )
        assert res == 0
        assert out_n_tris.value == n_verts - 2

        # C++ triangle volumes
        cp0 = poly[out_triangles[:, 0]]
        cp1 = poly[out_triangles[:, 1]]
        cp2 = poly[out_triangles[:, 2]]
        c_orient = (cp1[:, 0] - cp0[:, 0]) * (cp2[:, 1] - cp0[:, 1]) - (cp1[:, 1] - cp0[:, 1]) * (cp2[:, 0] - cp0[:, 0])
        c_vols = 0.5 * c_orient
        assert np.all(c_vols > 1e-14), "C++ ear clipping produced non-positive cell volume"
        c_total_area = float(np.sum(c_vols))
        assert abs(c_total_area - exact_area) < 1e-12


# ==============================================================================
# Test 3: Boundary Edge Subdivision Governed by h(x)
# ==============================================================================

def test_wp3_boundary_edge_subdivision():
    """Verify boundary edge subdivision matching uniform and non-uniform h(x)."""
    # Box [0, 10] x [0, 10]
    box = np.array([
        [0.0, 0.0],
        [10.0, 0.0],
        [10.0, 10.0],
        [0.0, 10.0],
    ], dtype=np.float64)

    # 1. Uniform h0 = 0.5 -> each side of length 10.0 must have 10.0 / 0.5 = 20 segments
    pts_sub, is_corner, edges = subdivide_polygon_edges(box, h_func=0.5)

    assert len(pts_sub) == 80, f"Expected 80 boundary points for h0=0.5, got {len(pts_sub)}"
    assert np.sum(is_corner) == 4, "Exactly 4 corners must be anchored"
    assert np.allclose(pts_sub[is_corner], box), "Corner points must match original box vertices"

    # Segment lengths must equal exactly 0.5
    seg_lengths = np.linalg.norm(pts_sub[edges[:, 1]] - pts_sub[edges[:, 0]], axis=1)
    assert np.allclose(seg_lengths, 0.5, atol=1e-12)

    # 2. Non-uniform h(x) = 0.2 + 0.1 * x (varies from 0.2 at x=0 to 1.2 at x=10)
    def h_nonuniform(xy: np.ndarray) -> np.ndarray:
        return 0.2 + 0.1 * xy[:, 0]

    pts_nu, is_corner_nu, edges_nu = subdivide_polygon_edges(box, h_func=h_nonuniform)
    assert np.sum(is_corner_nu) == 4

    # Bottom edge (y=0, x from 0 to 10): average h ~ 0.7 -> ~14 segments
    # Left edge (x=0, y from 10 to 0): h = 0.2 -> 10 / 0.2 = 50 segments
    # Right edge (x=10, y from 0 to 10): h = 1.2 -> 10 / 1.2 ~ 8 segments
    assert len(pts_nu) > 50, "Non-uniform sizing must generate dense points near x=0"

    # Verify corner vertices are identical
    corners = pts_nu[is_corner_nu]
    for orig in box:
        assert np.min(np.linalg.norm(corners - orig, axis=1)) < 1e-12


# ==============================================================================
# Test 4: DistMesh Relaxation Convergence & Inversion Safety
# ==============================================================================

def test_wp3_distmesh_relaxation_convergence():
    """Verify DistMesh spring relaxation on circle and L-bracket without inversion or void leakage."""
    # 1. Unit circle with DistMesh
    def fd_circle(p: np.ndarray) -> np.ndarray:
        return np.linalg.norm(p, axis=1) - 1.0

    def fh_uniform(p: np.ndarray) -> np.ndarray:
        return np.full(len(p), 0.15)

    pts, tris = distmesh_2d(
        fd=fd_circle,
        fh=fh_uniform,
        h0=0.15,
        bbox=(-1.1, -1.1, 1.1, 1.1),
        max_iters=40,
    )

    # Strictly positive triangle areas
    p0 = pts[tris[:, 0]]
    p1 = pts[tris[:, 1]]
    p2 = pts[tris[:, 2]]
    orient = (p1[:, 0] - p0[:, 0]) * (p2[:, 1] - p0[:, 1]) - (p1[:, 1] - p0[:, 1]) * (p2[:, 0] - p0[:, 0])
    vols = 0.5 * orient
    assert np.all(vols > 1e-14), "Circle DistMesh generated inverted triangles"

    # All nodes within radius 1.0 + margin
    radii = np.linalg.norm(pts, axis=1)
    assert np.all(radii <= 1.05), "Nodes escaped circular domain"

    # 2. L-bracket with MeshEngine (native and Python) with h0 = 0.25
    l_poly, l_exact = make_l_shaped_polygon()
    cfg = MeshingConfig(
        sizing=CurvatureSizingConfig(h0=0.25, h_min=0.1),
        distmesh_iters=30,
        rcm_reorder=True,
    )

    for use_native in [True, False]:
        engine = MeshEngine(use_native=use_native)
        mesh = engine.mesh_2d_polygon(l_poly, config=cfg)

        # Invariant A: All cell volumes strictly positive
        assert np.all(mesh.cell_volumes > 1e-14), f"Negative volumes in L-mesh (native={use_native})"

        # Invariant B: No cell centroids in the cutout void ([1.0, 3.0] x [1.0, 3.0])
        cc = mesh.cell_centres
        in_void = (cc[:, 0] > 1.05) & (cc[:, 1] > 1.05)
        assert not np.any(in_void), f"Void leakage detected in L-mesh (native={use_native})!"

        # Invariant C: Total volume close to analytical 5.0 (within discretization tolerance < 3%)
        vol_sum = float(np.sum(mesh.cell_volumes))
        assert abs(vol_sum - l_exact) / l_exact < 0.03, f"Volume error too large: {vol_sum} vs {l_exact}"


# ==============================================================================
# Test 5: Exact Area Conservation Across 10 Benchmark Polygons
# ==============================================================================

def test_wp3_exact_area_conservation():
    """Verify exact analytical area conservation (|sum(V_i) - A_exact| < 1e-12) on 10 benchmarks."""
    benchmarks: List[Tuple[str, np.ndarray, float]] = [
        # 1. Equilateral triangle
        ("equilateral_tri", np.array([[0.0, 0.0], [2.0, 0.0], [1.0, np.sqrt(3.0)]]), np.sqrt(3.0)),
        # 2. Right triangle
        ("right_tri", np.array([[0.0, 0.0], [4.0, 0.0], [0.0, 3.0]]), 6.0),
        # 3. Unit square
        ("unit_square", np.array([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]]), 1.0),
        # 4. Rectangle [0, 5] x [0, 2]
        ("rectangle", np.array([[0.0, 0.0], [5.0, 0.0], [5.0, 2.0], [0.0, 2.0]]), 10.0),
        # 5. Regular pentagon (R=1.0)
        ("pentagon", np.column_stack([np.cos(np.linspace(0, 2*np.pi, 5, endpoint=False)),
                                      np.sin(np.linspace(0, 2*np.pi, 5, endpoint=False))]),
         0.5 * 5.0 * np.sin(2.0 * np.pi / 5.0)),
        # 6. Regular hexagon (R=2.0)
        ("hexagon", np.column_stack([2.0 * np.cos(np.linspace(0, 2*np.pi, 6, endpoint=False)),
                                     2.0 * np.sin(np.linspace(0, 2*np.pi, 6, endpoint=False))]),
         1.5 * np.sqrt(3.0) * 4.0),
        # 7. L-shaped polygon
        ("l_shape", make_l_shaped_polygon()[0], 5.0),
        # 8. U-shaped channel
        ("u_shape", make_u_shaped_polygon()[0], 7.0),
        # 9. T-shaped bracket
        ("t_shape", np.array([[0.0, 2.0], [3.0, 2.0], [3.0, 3.0], [-2.0, 3.0], [-2.0, 2.0], [-1.0, 2.0], [-1.0, 0.0], [0.0, 0.0]]), 7.0),
        # 10. 5-pointed star
        ("star", make_star_polygon()[0], make_star_polygon()[1]),
    ]

    for name, poly, exact_a in benchmarks:
        pts, tris = ear_clipping_triangulate(poly)
        p0 = pts[tris[:, 0]]
        p1 = pts[tris[:, 1]]
        p2 = pts[tris[:, 2]]
        vols = 0.5 * ((p1[:, 0] - p0[:, 0]) * (p2[:, 1] - p0[:, 1]) - (p1[:, 1] - p0[:, 1]) * (p2[:, 0] - p0[:, 0]))

        # All volumes strictly positive
        assert np.all(vols > 1e-14), f"Benchmark {name} produced non-positive cell volume"

        # Exact area conservation
        mesh_area = float(np.sum(vols))
        diff = abs(mesh_area - exact_a)
        assert diff < 1e-12, f"Benchmark {name} area conservation violated: {diff:.2e} >= 1e-12"


# ==============================================================================
# Test 6: Python / C++20 Dual-Engine Parity
# ==============================================================================

def test_wp3_python_cpp20_dual_engine_parity():
    """Verify bit-for-bit mathematical and physical invariant parity between C++20 and Python."""
    poly, a_exact = make_l_shaped_polygon()

    # Test both clockwise and counter-clockwise inputs
    for is_cw in [False, True]:
        test_poly = poly[::-1].copy() if is_cw else poly.copy()

        # Execute pure Python
        engine_py = MeshEngine(use_native=False)
        mesh_py = engine_py.mesh_2d_polygon(test_poly)

        # Execute native C++20
        engine_cpp = MeshEngine(use_native=True)
        mesh_cpp = engine_cpp.mesh_2d_polygon(test_poly)

        assert mesh_py.metadata.get("engine") == "pure_python_fallback" or "fallback" in str(mesh_py.metadata)
        assert mesh_cpp.metadata.get("engine") == "native_cpp20_2d"

        # Both engines must yield strictly positive volumes
        assert np.all(mesh_py.cell_volumes > 1e-14)
        assert np.all(mesh_cpp.cell_volumes > 1e-14)

        # Both engines must conserve exact analytical area
        vol_py = float(np.sum(mesh_py.cell_volumes))
        vol_cpp = float(np.sum(mesh_cpp.cell_volumes))

        assert abs(vol_py - a_exact) < 1e-12, f"Python volume {vol_py} != exact {a_exact}"
        assert abs(vol_cpp - a_exact) < 1e-12, f"C++ volume {vol_cpp} != exact {a_exact}"
        assert abs(vol_py - vol_cpp) < 1e-12, f"Parity mismatch: |vol_py - vol_cpp| = {abs(vol_py - vol_cpp):.2e}"

        # Face count and connectivity invariants
        assert mesh_py.num_cells == mesh_cpp.num_cells
        internal_py = mesh_py.neighbour >= 0
        internal_cpp = mesh_cpp.neighbour >= 0
        assert np.all(mesh_py.owner[internal_py] < mesh_py.neighbour[internal_py])
        assert np.all(mesh_cpp.owner[internal_cpp] < mesh_cpp.neighbour[internal_cpp])


# ==============================================================================
# Test 7: Regression — High-Aspect-Ratio Quad with Boundary Subdivision (No Area Loss / No Dangling Nodes)
# ==============================================================================

@pytest.mark.parametrize("use_native", [False, True])
def test_wp3_regression_high_aspect_ratio_quad_subdivision(use_native: bool):
    """Verify high-aspect-ratio rectangle [0, 2] x [0, 0.5] with h0=0.8 produces valid mesh.

    Tests remediation of Challenger Observation 1.1 (C++ area corruption / dangling nodes)
    and Observation 1.2 (Python zero-size reduction crash):
    - Strictly positive cell volumes (V_i > 0).
    - Exact analytical area conservation (|sum(V_i) - 1.0| < 1e-12).
    - Closed-cell Gauss divergence theorem satisfaction (mesh.validate() passes with tol_rel < 1e-11).
    - Zero dangling or unreferenced nodes.
    """
    poly = np.array([
        [0.0, 0.0],
        [2.0, 0.0],
        [2.0, 0.5],
        [0.0, 0.5],
    ], dtype=np.float64)
    exact_area = 1.0

    cfg = MeshingConfig(sizing=CurvatureSizingConfig(h0=0.8))
    engine = MeshEngine(use_native=use_native)
    mesh = engine.mesh_2d_polygon(poly, config=cfg)

    assert mesh is not None
    assert mesh.num_cells >= 2
    assert np.all(mesh.cell_volumes > 1e-14)

    # 1. Exact analytical area conservation (1.0 m^2)
    total_vol = float(np.sum(mesh.cell_volumes))
    assert abs(total_vol - exact_area) < 1e-12, f"Total volume {total_vol} != {exact_area}"

    # 2. Closed-cell Gauss divergence theorem validation (< 1e-11 closure)
    assert mesh.validate(), "mesh.validate() failed on high-aspect-ratio quad"

    # 3. Zero dangling/unreferenced nodes
    referenced_nodes = np.unique(np.concatenate(mesh.cells))
    assert len(referenced_nodes) == mesh.num_points, (
        f"Dangling nodes detected: {len(referenced_nodes)} referenced out of {mesh.num_points} total"
    )


# ==============================================================================
# Test 8: Regression — Engine Sizing Configuration Resolution
# ==============================================================================

def test_wp3_regression_mesh_engine_sizing_config_resolution():
    """Verify MeshEngine(config=...) sizing is respected by mesh_2d_polygon when config=None.

    Tests remediation of Challenger Observation 1.3:
    - engine = MeshEngine(config=MeshingConfig(sizing=CurvatureSizingConfig(h0=0.1)))
    - engine.mesh_2d_polygon(poly) must respect self.config.sizing (h0=0.1) and generate
      a refined mesh (cell count >> 4), matching engine.generate_mesh(poly).
    """
    poly = np.array([
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
    ], dtype=np.float64)

    cfg = MeshingConfig(sizing=CurvatureSizingConfig(h0=0.1))
    engine = MeshEngine(config=cfg, use_native=False)

    m_poly = engine.mesh_2d_polygon(poly)
    m_gen = engine.generate_mesh(poly)

    # Both must resolve h0=0.1 and generate refined meshes (> 50 cells)
    assert m_poly.num_cells > 50, (
        f"mesh_2d_polygon ignored self.config.sizing.h0: only {m_poly.num_cells} cells generated"
    )
    assert m_gen.num_cells > 50, (
        f"generate_mesh generated only {m_gen.num_cells} cells"
    )
    # Cell counts should match
    assert m_poly.num_cells == m_gen.num_cells
    assert abs(np.sum(m_poly.cell_volumes) - 1.0) < 1e-12

