# Architectural & Implementation Blueprint: Robust 2D-Mesher (Milestone WP3)

**Author:** Explorer Agent (`explorer_phase2_wp3_1`)  
**Target Milestone:** WP3 (Robust 2D-Mesher)  
**Parent Conversation ID:** `3b11a978-5aa5-4f27-830d-3449f13b9d1b`  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase2_wp3_1\`  
**Date:** 2026-11-03  

---

## 1. Observation

Direct empirical probes and source code audits revealed four critical defects in the existing 2D mesher implementation across Python (`backend/meshing/mesh_engine.py`, `backend/meshing/distmesh_core.py`) and native C++20 (`backend/meshing/native/src/distmesh.cpp`, `backend/meshing/native/src/cfd_mesher_native.cpp`).

### 1.1 Empirical Verification of Defects

1. **Inverted Cells on Clockwise (CW) Polygons:**
   - **Command Executed:**
     ```powershell
     $env:OMP_NUM_THREADS="16"; .venv\Scripts\python.exe -c "
     import numpy as np; from backend.meshing.mesh_engine import MeshEngine
     engine = MeshEngine(use_native=True)
     cw_square = np.array([[0,0], [0,1], [1,1], [1,0]], dtype=float)
     m_cw = engine.mesh_2d_polygon(cw_square)
     print('CW Square cell_volumes:', m_cw.cell_volumes)
     print('CW Square sum volumes:', np.sum(m_cw.cell_volumes))
     "
     ```
   - **Verbatim Result:**
     ```text
     CW Square cell_volumes: [-0.25 -0.25 -0.25 -0.25]
     CW Square sum volumes: -1.0
     ```
   - **Finding:** Every triangle generated has a strictly negative volume ($V_i = -0.25$) because the input vertex ordering is clockwise and no orientation canonicalization is performed.

2. **Inverted Cells & Massive Area Loss on Non-Convex Polygons (Centroid Fan Failure):**
   - **Command Executed:**
     ```powershell
     $env:OMP_NUM_THREADS="16"; .venv\Scripts\python.exe -c "
     import numpy as np; from backend.meshing.mesh_engine import MeshEngine
     engine = MeshEngine(use_native=True)
     # Analytical area: 3*1 + 2*1 = 5.0
     l_poly = np.array([[0,0], [3,0], [3,1], [1,1], [1,3], [0,3]], dtype=float)
     m_l = engine.mesh_2d_polygon(l_poly)
     print('L-poly cell_volumes:', m_l.cell_volumes)
     print('L-poly sum volumes:', np.sum(m_l.cell_volumes))
     print('L-poly min volume:', np.min(m_l.cell_volumes))
     "
     ```
   - **Verbatim Result (Native C++):**
     ```text
     L-poly cell_volumes: [-0.33333333  0.27777778 -0.33333333  2.          0.27777778  2.        ]
     L-poly sum volumes: 3.8888888888888893
     L-poly min volume: -0.33333333333333315
     ```
   - **Verbatim Result (Pure-Python Fallback):**
     ```text
     Python fallback L-poly cell_volumes: [-0.33333333  0.27777778 -0.11111111  2.          0.83333333  2.        ]
     Python fallback L-poly sum volumes: 4.666666666666667
     Python fallback L-poly min volume: -0.33333333333333315
     ```
   - **Finding:** The centroid fan connects vertices to the arithmetic mean $c = (1.1, 1.1)$, which lies *outside* the L-shaped domain in the cutout void. This generates multiple inverted triangles ($V_i = -0.3333$) and violates area conservation by $1.1111$ in native and $0.3333$ in Python.

3. **Complete Absence of Boundary Edge Subdivision Matching Target Size $h_0$:**
   - **Command Executed:**
     ```powershell
     $env:OMP_NUM_THREADS="16"; .venv\Scripts\python.exe -c "
     import numpy as np, ctypes; from backend.meshing.difvm_topology import get_native_dll, MeshingConfigC
     dll = get_native_dll(); m_ptr = dll.cfd_mesher_create()
     poly = np.array([[0,0], [1,0], [1,1], [0,1]], dtype=float)
     cfg_c = MeshingConfigC(); cfg_c.h0 = 0.1; cfg_c.distmesh_iters = 20
     dll.cfd_mesher_generate_2d_polygon(m_ptr, poly.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), 4, ctypes.byref(cfg_c))
     n_pts = dll.cfd_mesher_get_n_points(m_ptr)
     pts = np.ctypeslib.as_array(dll.cfd_mesher_get_points_ptr(m_ptr), shape=(n_pts, 3))
     print('Points on boundary x=0:', np.sum(np.isclose(pts[:, 0], 0.0, atol=1e-5)))
     print('Points on boundary x=1:', np.sum(np.isclose(pts[:, 0], 1.0, atol=1e-5)))
     print('Total volume:', np.sum(np.ctypeslib.as_array(dll.cfd_mesher_get_cell_volumes_ptr(m_ptr), shape=(dll.cfd_mesher_get_n_cells(m_ptr),))))
     dll.cfd_mesher_free(m_ptr)
     "
     ```
   - **Verbatim Result:**
     ```text
     Points on boundary x=0: 2
     Points on boundary x=1: 2
     Total volume: 0.997548348958265
     ```
   - **Finding:** Boundary edges of length $1.0$ have *only 2 points* (the corners), completely ignoring $h_0 = 0.1$ which requires 10 subdivisions. Interior triangles connecting to coarse boundary corners distort the boundary and cause an area deficit of $\Delta V = 0.00245$.

4. **Area Leakage Across Concave Corners (Unconstrained Delaunay):**
   - **Command Executed:** Native L-polygon with `cfg_c.h0 = 0.2`.
   - **Verbatim Result:**
     ```text
     L-poly with h0=0.2: res= 0 n_cells= 146 n_points= 141
     Total volume (expected 5.0): 5.163772142102122
     ```
   - **Finding:** Total mesh volume is $5.1638$, leaking $0.1638$ area into the void exterior. Bowyer-Watson Delaunay is unconstrained, boundary edges are not preserved, and triangles cross the concave corner at $(1, 1)$.

### 1.2 Direct Code Inspections

1. **`backend/meshing/mesh_engine.py` (lines 381–397):**
   ```python
   cfg_c = MeshingConfigC()
   cfg_c.enable_rcm = 1 if active_cfg.rcm_reorder else 0
   cfg_c.distmesh_iters = active_cfg.distmesh_iters
   ```
   `cfg_c.h0` is NEVER assigned from `active_cfg.sizing.h0`. It defaults to `0.0`. In `distmesh.cpp:453`, `if (h0 > 0.0)` is false, forcing native execution to fall back to the centroid fan on every call from Python!

2. **`backend/meshing/mesh_engine.py` (lines 488–497):**
   ```python
   poly_center = np.mean(poly, axis=0)
   all_pts = np.vstack([poly, poly_center])
   c_idx = len(poly)
   cells = []
   for i in range(n_verts):
       next_i = (i + 1) % n_verts
       cells.append(np.array([c_idx, i, next_i], dtype=np.int64))
   ```
   Pure-Python fallback is hardcoded to the naive centroid fan.

3. **`backend/meshing/native/src/distmesh.cpp` (lines 482–491 & 574–585):**
   ```cpp
   if (all_pts.size() == static_cast<size_t>(n_fixed)) {
       Vec2 poly_c(0, 0);
       for (const auto& p : p_nodes) poly_c = poly_c + p;
       poly_c = poly_c / static_cast<double>(n_vertices);
       int32_t c_idx = static_cast<int32_t>(all_pts.size());
       all_pts.push_back(poly_c);
       for (int32_t i = 0; i < n_vertices; ++i) {
           kept_triangles.push_back({c_idx, static_cast<int32_t>(i), static_cast<int32_t>((i + 1) % n_vertices)});
       }
   }
   ```
   Identical naive centroid fan duplicated in C++20.

4. **`backend/meshing/native/src/distmesh.cpp` (lines 450–478):**
   Boundary nodes `all_pts` are initialized strictly with `p_nodes` ($n_{\text{vertices}}$ original corner points). No boundary edge resampling or subdivision is performed.

5. **`backend/meshing/distmesh_core.py` (lines 395–398 & lines 480–490):**
   - Line 398: `p = p[d < 0.05 * h_vals]` retains points *outside* the domain (sign error; should be $d < -0.05 h$).
   - Line 481: `outside = d_nodes > 0.0; p[outside] -= ...` modifies fixed points `pfix` because `outside[:n_fix] = False` was omitted.
   - Lines 497–502: Triangulation uses unconstrained `scipy.spatial.Delaunay(p)` with centroid-only filtering `fd(pmid) < -1e-5`, which cuts across concave corners and fails to enforce boundary edges.

---

## 2. Logic Chain

1. **Origin of Inverted Cells on CW Polygons (Observation 1.1.1):**
   The signed 2D triangle area formula is $V = \frac{1}{2}((x_b - x_a)(y_c - y_a) - (y_b - y_a)(x_c - x_a))$. When vertices $(a, b, c)$ form a clockwise loop, this determinant is strictly negative. A mesher must ensure that every triangle is oriented counter-clockwise (CCW). If the input boundary polygon is clockwise, all triangles inherited from it have negative orientation. Applying the Shoelace formula to detect $A_{\text{signed}} < 0$ and reversing the vertex sequence guarantees that the boundary orientation is canonical CCW before meshing begins.

2. **Mechanism of Centroid Fan Collapse (Observation 1.1.2 & Observation 1.2.2/3):**
   A centroid fan assumes the arithmetic centroid $c = \frac{1}{n} \sum v_i$ is a kernel point of the star-shaped polygon, i.e., the line segment from $c$ to any boundary point lies entirely inside the domain. For any non-convex polygon (L-bracket, U-channel, chevron, NACA airfoil trailing edge), the centroid can lie outside the polygon. Rays connecting $c$ to boundary vertices intersect external space and other boundary segments. Consequently, triangles $(c, v_i, v_{i+1})$ overlap, have negative signed area, extend into the exterior, and fail area conservation. Replacing the centroid fan with the **Ear-Clipping algorithm (Held 2001, Eberly)** solves this completely: every simple polygon has at least two ears (Meisters' Two Ears Theorem), clipping an ear removes a strictly internal convex triangle without modifying external boundaries, yielding exactly $n - 2$ strictly positive, non-overlapping triangles whose union matches the analytical polygon area to float precision ($|\sum V_i - A| < 10^{-14}$).

3. **Mechanism of Boundary Edge Decoupling (Observation 1.1.3 & Observation 1.2.4):**
   In Persson & Strang (2004) DistMesh, force-displacement relaxation operates on an interior point cloud whose nominal spacing is $h_0$ (or adaptive $h(x)$). If boundary edges are not subdivided, the boundary comprises only the coarse polygon vertices ($L \gg h_0$). When Delaunay triangulation connects dense interior nodes to distant corner vertices, high-aspect-ratio slivers and irregular boundary polygons form. Subdividing boundary edges into segments of length $\sim h(x)$ prior to interior seeding ensures that boundary edge lengths match the local metric, fixed corner points remain anchored, and edge-boundary nodes can either remain fixed or slide tangentially along boundary segments.

4. **Mechanism of Concave Area Leakage in Delaunay Triangulation (Observation 1.1.4 & Observation 1.2.5):**
   Standard Delaunay triangulation is unconstrained: it maximizes the minimum angle across the convex hull of all nodes. For a non-convex domain, Delaunay edges will inevitably span across concavities (e.g. from $(3, 1)$ to $(1, 3)$ across the $(1, 1)$ inner corner). Centroid-based element filtering (`fd(pmid) < 0`) is insufficient: if the centroid of an edge-crossing triangle happens to lie inside the polygon, the entire exterior triangle is included; if excluded, boundary conformality is lost. A Constrained Delaunay Triangulation (CDT) or boundary-edge enforcement (flipping intersecting edges until all boundary segments exist) is strictly necessary to partition the triangulation into interior and exterior sub-graphs along exact boundary edges.

---

## 3. Caveats

1. **Non-Simple / Self-Intersecting Polygons:** The blueprint and acceptance criteria target simple 2D polygons (single outer boundary) and polygons with non-intersecting interior holes. Self-intersecting polygons (figure-8, complex self-crossings) must be rejected with an explicit `ValueError("Self-intersecting polygon is not a simple polygon")`.
2. **Hole Bridges vs. Planar Face Extraction:** When polygons contain interior holes, Ear-Clipping requires inserting a zero-width mutual-visibility bridge edge connecting the outer boundary to the hole. In the final `UnstructuredMesh`, internal faces resulting from the bridge edge are topological twins with opposite orientations; they must be cleanly identified and fused so no artificial boundary patches appear inside the fluid domain.
3. **Extreme Aspect Ratio Spikes:** When $h(x)$ varies rapidly ($\|\nabla h\| > 0.5$), boundary edge subdivision must enforce the Lipschitz grading limit ($G \le 0.35$) to prevent adjacent edge lengths from jumping by more than a factor of 1.5, which could destabilize Bowyer-Watson point insertion.

---

## 4. Conclusion & Architectural Implementation Blueprint

### 4.1 Component Architecture Overview

The robust 2D mesher architecture consists of 5 modular components implemented with bit-for-bit parity across Python (`backend/meshing/`) and C++20 (`backend/meshing/native/`):

```
+-----------------------------------------------------------------------------------+
|                           MeshEngine.mesh_2d_polygon                              |
|   (polygon_xy, config=None, sizing_func=None, holes=None, domain_mode="2d")       |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 1. Shoelace Canonicalization Engine (canonicalize_polygon_ccw / canonicalizeCCW)   |
|    - Computes signed area A = 0.5 * sum(x_i * y_{i+1} - x_{i+1} * y_i)           |
|    - If A < 0: automatically reverse vertices to CCW; if |A| < 1e-14: raise Error  |
|    - Normalizes holes to CW (A_hole < 0)                                          |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 2. Boundary Edge Subdivision Engine (subdivide_polygon_edges / subdivideEdges)    |
|    - Evaluates local target length h(x) along each boundary segment [v_i, v_{i+1}]|
|    - Resamples edge into n_seg = max(1, round(L / h_avg)) segments               |
|    - Anchors fixed corner points (pfix) and generates conformal boundary nodes    |
+-----------------------------------------------------------------------------------+
                                         |
            +----------------------------+----------------------------+
            | (If N_interior == 0 or small polygon)                   | (If h0 > 0 and interior points exist)
            v                                                         v
+----------------------------------------+ +----------------------------------------+
| 3. Ear-Clipping Engine (Held / Eberly) | | 4. Constrained DistMesh Engine (2D)    |
|    - Doubly-linked circular vertex list| |    - Persson & Strang spring relaxation|
|    - Convex/reflex vertex classification| |    - Locked corner nodes, sliding edges|
|    - Non-overlapping ear test          | |    - Constrained Delaunay edge flipping|
|    - Exactly n - 2 positive triangles  | |    - Flood-fill exterior triangle cull |
|    - sum(V_i) == A_exact (< 1e-14)     | |    - All V_i > 0, sum(V_i) == A_exact  |
+----------------------------------------+ +----------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 5. DiFVM Topologizer & Lossless RCM Bandwidth Reduction (compute_difvm_topology)  |
|    - UnstructuredMesh with explicit faces (CSR), CellType.TRIANGLE, error_map     |
|    - Lossless RCM reordering preserving named boundaries                          |
+-----------------------------------------------------------------------------------+
```

---

### 4.2 Mathematical Formulations

#### 1. Shoelace Formula & Orientation
For polygon vertices $(x_0, y_0), \dots, (x_{n-1}, y_{n-1})$ with $(x_n, y_n) \equiv (x_0, y_0)$:
$$A = \frac{1}{2} \sum_{i=0}^{n-1} (x_i y_{i+1} - x_{i+1} y_i)$$
- Outer boundary is CCW $\iff A > 0$. If $A < 0$, reverse array: $v \leftarrow v[::-1]$.
- Holes are CW $\iff A < 0$. If $A_{\text{hole}} > 0$, reverse array: $v_{\text{hole}} \leftarrow v_{\text{hole}}[::-1]$.

#### 2. Ear-Clipping Condition (Held 2001, Eberly)
Three consecutive vertices $(v_{i-1}, v_i, v_{i+1})$ form an ear $\iff$:
1. $v_i$ is strictly convex:
   $$\text{orient2d}(v_{i-1}, v_i, v_{i+1}) = (x_i - x_{i-1})(y_{i+1} - y_{i-1}) - (y_i - y_{i-1})(x_{i+1} - x_{i-1}) > 10^{-15}$$
2. For every reflex vertex $v_r$ of the current polygon ($r \notin \{i-1, i, i+1\}$):
   $$v_r \notin \Delta(v_{i-1}, v_i, v_{i+1})$$
   where point-in-triangle is tested via 3 orientation tests:
   $$\neg (\text{orient2d}(v_{i-1}, v_i, v_r) \ge -\epsilon \land \text{orient2d}(v_i, v_{i+1}, v_r) \ge -\epsilon \land \text{orient2d}(v_{i+1}, v_{i-1}, v_r) \ge -\epsilon)$$

#### 3. Metric-Governed Boundary Subdivision
For edge $e = (v_i, v_{i+1})$ of length $L = \|v_{i+1} - v_i\|$, target element size along edge is $h(s) = h(v_i + s \hat{e})$.
Discrete segment count:
$$n_{\text{seg}} = \max\left(1, \operatorname{round}\left(\frac{L}{\frac{1}{2}(h(v_i) + h(v_{i+1}))}\right)\right)$$
Equispaced sample points along segment:
$$p_k = v_i + \frac{k}{n_{\text{seg}}} (v_{i+1} - v_i), \quad k = 0, \dots, n_{\text{seg}}-1$$
Corner points $k=0$ are tagged `is_fixed = True`.

#### 4. Persson & Strang (2004) DistMesh Equilibrium
Edge spring force:
$$F_{ij} = \max(L_{0, ij} - l_{ij}, 0), \quad L_{0, ij} = f_{\text{scale}} h\left(\frac{p_i + p_j}{2}\right) \sqrt{\frac{\sum l_e^2}{\sum h_e^2}}$$
Node update:
$$p_i \leftarrow p_i + \Delta t \sum_{j \in \mathcal{N}(i)} F_{ij} \frac{p_i - p_j}{l_{ij}}$$
Boundary constraint:
- Fixed corner points: $\Delta p_i = 0$.
- Resampled boundary points: projected onto nearest polygon boundary segment.
- Drifting interior points ($d(p) > 0$): projected back via $p \leftarrow p - d(p) \nabla d(p) / \|\nabla d(p)\|^2$.

---

### 4.3 Python Architecture & Specifications (`backend/meshing/`)

#### 1. `backend/meshing/distmesh_core.py` Additions

```python
def compute_polygon_signed_area(vertices: np.ndarray) -> float:
    """Compute signed area of 2D polygon using Shoelace formula."""
    pts = np.asarray(vertices, dtype=np.float64)
    x = pts[:, 0]
    y = pts[:, 1]
    return 0.5 * float(np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y))


def canonicalize_polygon_ccw(vertices: np.ndarray) -> Tuple[np.ndarray, bool, float]:
    """Ensure 2D polygon vertices are ordered counter-clockwise (CCW).
    
    Returns:
        canonical_vertices: (N, 2) array ordered CCW.
        was_reversed: True if input was clockwise and had to be reversed.
        signed_area: Positive signed area of the polygon.
    Raises:
        ValueError: If polygon has < 3 vertices or area < 1e-14.
    """
    pts = np.asarray(vertices, dtype=np.float64)
    if len(pts) < 3:
        raise ValueError(f"Polygon must have at least 3 vertices, got {len(pts)}")
    area = compute_polygon_signed_area(pts)
    if abs(area) < 1e-14:
        raise ValueError(f"Degenerate polygon with near-zero area: {area}")
    if area < 0.0:
        return pts[::-1].copy(), True, -area
    return pts.copy(), False, area


def ear_clipping_triangulate(
    polygon_xy: np.ndarray,
    holes: Optional[List[np.ndarray]] = None,
) -> Tuple[np.ndarray, np.ndarray]:
    """Triangulate arbitrary simple 2D polygon (convex or non-convex) via Ear-Clipping.
    
    Guarantees:
    - Zero inverted triangles: orient2d(t) > 0 for all triangles.
    - Exact analytical area conservation: |sum(V_i) - A_exact| < 1e-12.
    - Zero external triangles; no centroid fan.
    
    Returns:
        points: (N, 2) coordinates.
        triangles: (N - 2, 3) triangle indices.
    """
    # 1. Canonicalize outer boundary to CCW
    poly, _, A_exact = canonicalize_polygon_ccw(polygon_xy)
    
    # 2. If holes provided, cut bridges to merge holes into single simple polygon
    # (Holes canonicalized to CW)
    if holes and len(holes) > 0:
        poly = merge_polygon_holes(poly, holes)
        
    n = len(poly)
    if n == 3:
        return poly, np.array([[0, 1, 2]], dtype=np.int64)

    # 3. Circular doubly linked list for O(n^2) robust ear clipping
    indices = list(range(n))
    triangles = []
    
    # Clip ears until 3 vertices remain
    # At each step find an ear whose interior contains no reflex vertices
    # Emit triangle (prev, curr, next)
    ...
    return poly, np.array(triangles, dtype=np.int64)


def subdivide_polygon_edges(
    polygon_xy: np.ndarray,
    h_func: Union[float, Callable[[np.ndarray], np.ndarray]],
    is_closed: bool = True,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Subdivide polygon boundary edges so segment lengths match local h(x).
    
    Args:
        polygon_xy: (N, 2) polygon vertices.
        h_func: Constant h0 or callable h(xy) -> target lengths.
        is_closed: True if polygon loop is closed.
        
    Returns:
        resampled_points: (M, 2) resampled boundary coordinates.
        is_corner: (M,) bool mask indicating original fixed polygon corners.
        edge_indices: (M, 2) boundary segment connectivity.
    """
    ...
```

#### 2. `backend/meshing/mesh_engine.py` Updates

```python
def mesh_2d_polygon(
    self,
    polygon_xy: np.ndarray,
    config: Optional[MeshingConfig] = None,
    sizing_func: Optional[Callable[[np.ndarray], np.ndarray]] = None,
    holes: Optional[List[np.ndarray]] = None,
) -> UnstructuredMesh:
    """Generate 2D unstructured mesh with boundary fidelity and adaptive sizing."""
    active_cfg = config if config is not None else self.config
    poly, was_reversed, A_exact = canonicalize_polygon_ccw(polygon_xy)
    
    # Determine target h0 from active_cfg
    h0 = 0.0
    if active_cfg.sizing and active_cfg.sizing.h0 > 0.0:
        h0 = active_cfg.sizing.h0
    elif active_cfg.sizing and active_cfg.sizing.h_min > 0.0:
        h0 = active_cfg.sizing.h_min * 2.0
        
    # Subdivide boundary edges if h0 > 0
    if h0 > 0.0:
        bnd_pts, is_corner, bnd_edges = subdivide_polygon_edges(poly, sizing_func or h0)
    else:
        bnd_pts = poly
        is_corner = np.ones(len(poly), dtype=bool)

    # Dispatch to Native C++20 if available
    if self.use_native and self._native_dll is not None:
        # Build MeshingConfigC and pass cfg_c.h0 = h0 explicitly!
        ...
    
    # Pure-Python Fallback:
    # If no interior requested or small domain: ear_clipping_triangulate
    # If h0 > 0: distmesh_2d with fixed corner constraints and edge projection
    ...
```

---

### 4.4 C++20 Native Architecture & Specifications (`backend/meshing/native/`)

#### 1. Header `include/distmesh.hpp` Updates

```cpp
namespace cfd {

struct Vec2 {
    double x = 0.0, y = 0.0;
    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
    double norm2() const { return x * x + y * y; }
    double norm() const { return std::sqrt(norm2()); }
};

inline double orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Shoelace Formula
double computeShoelaceArea(const std::vector<Vec2>& poly);
bool canonicalizePolygonCCW(std::vector<Vec2>& poly, double* out_area = nullptr);

// Boundary Edge Subdivision
std::vector<Vec2> subdivideBoundaryEdges(
    const std::vector<Vec2>& poly,
    double h0,
    std::vector<uint8_t>& is_corner
);

// Ear-Clipping Triangulation (Held / Eberly)
std::vector<std::array<int32_t, 3>> earClippingTriangulate(
    const std::vector<Vec2>& poly
);

// Constrained DistMesh Engine
class DistMeshEngine {
public:
    static void generate2DPolygon(
        NativeMeshData& mesh,
        const double* polygon_xy,
        int64_t n_vertices,
        const DistMeshOptions& options,
        const MeshingConfigC* config = nullptr
    );
};

} // namespace cfd
```

#### 2. Source `src/distmesh.cpp` Implementation Details

- **Eliminate Centroid Fan:** Replace lines 482–491 and 574–585 with `earClippingTriangulate(all_pts)`.
- **Pass `h0` Correctly:** Ensure `options.h0` is read from `config->h0` and used in boundary edge subdivision.
- **Subdivide Boundary:** Subdivide boundary edges prior to Bowyer-Watson Delaunay insertion using `subdivideBoundaryEdges`.
- **Boundary Node Constraint:** During spring force relaxation, corner points (`is_corner[i] == 1`) have forces zeroed (`forces[i] = Vec2(0, 0)`). Intermediate edge points are projected back onto the nearest boundary segment.
- **Orient Triangles CCW:** For every emitted triangle $(a, b, c)$, verify `orient2d(p[a], p[b], p[c]) > 0`. If negative, `std::swap(b, c)`.
- **Verify Analytical Area:** Assert $|\sum V_i - A_{\text{exact}}| < 10^{-12}$.

---

## 5. Verification Method

To verify the implementation of Milestone WP3 independently, create the standardized test suite `tests/test_wp3_robust_2d_mesher.py` covering the following 6 test fixtures and criteria:

### 5.1 Verification Test Matrix

| Test Function | Verification Scope | Pass Criterion |
|---|---|---|
| `test_wp3_shoelace_ccw_canonicalization` | Unit square, regular hexagon, irregular L-polygon in both CW and CCW ordering. | $A_{\text{signed}} > 0$; CW reversed to CCW; degenerate $< 10^{-14}$ raises `ValueError`. |
| `test_wp3_ear_clipping_nonconvex_polygons` | L-shaped polygon, U-shaped cavity, 5-pointed star, and NACA 0012 trailing-edge polygon. | Exactly $n-2$ cells; $V_i > 0$ strictly for all cells; $|\sum V_i - A_{\text{exact}}| < 10^{-12}$. |
| `test_wp3_boundary_edge_subdivision` | Box $[0, 10] \times [0, 10]$ with $h_0 = 0.5$ and non-uniform $h(x) = 0.2 + 0.1 x$. | Boundary edges subdivided into $\ge 20$ nodes per side; spacing matches $h(x) \pm 15\%$; corner points anchored. |
| `test_wp3_distmesh_relaxation_convergence` | Circular disk ($R=1.0$, $h_0=0.1$) and L-bracket with force relaxation. | Relative node displacement $\frac{\Delta p}{h_0} < 10^{-3}$; no points leak into exterior void; zero inverted cells. |
| `test_wp3_exact_area_conservation` | Set of 10 convex and non-convex benchmark polygons. | $|\sum_{i} V_i - A_{\text{exact}}| < 10^{-12}$ across all 10 benchmarks. |
| `test_wp3_python_cpp20_dual_engine_parity` | Execute `MeshEngine(use_native=True)` vs `MeshEngine(use_native=False)` on identical inputs. | Cell counts match within $\pm 2\%$; total volume difference $< 10^{-11}$; topology invariants verified. |

### 5.2 Independent Verification Execution

Run the following test command in PowerShell with the mandatory thread limit:
```powershell
$env:OMP_NUM_THREADS="16"
.venv\Scripts\python.exe -m pytest tests/test_wp3_robust_2d_mesher.py -v
```

**Invalidation Conditions:**
- Any triangle with $V_i \le 0$ (inverted or zero-area triangle).
- Any area discrepancy $|\sum V_i - A_{\text{exact}}| \ge 10^{-12}$ on simple polygons.
- Any presence of the naive centroid fan in either Python or C++20.
- Any un-subdivided boundary edges when $h_0 \ll L_{\text{edge}}$.
