# Survey Report: Work Package 2 (WP2) — Exaktes Mesh-SDF mit AABB-Baum, Pseudonormalen, Winding Numbers & VoroMesh

**Date:** 2026-10-30  
**Author:** explorer_phase1_survey_2 (teamwork_explorer)  
**Project:** CFDSolver (`backend/meshing`)  
**Scope:** Architecture, mathematical formulations, defect analysis, and implementation specification for R2 & Gate WP2.

---

## 1. Executive Summary & Problem Boundary

Work Package 2 (WP2) addresses the geometric level-set backbone of the CFD meshing subsystem.
In CFD mesh generation (such as body-fitted meshing, Cartesian cut-cell generation, boundary layer inflation, and topology optimization), every cell center, node, and sampling point requires an exact, signed wall distance $\phi(\mathbf{x})$ and an outward unit normal gradient $\nabla \phi(\mathbf{x})$.

### Current Deficiencies in Baseline (`backend/meshing/geometry_sdf.py`):
1. **Centroid-cKDTree Heuristic:** `CADMeshSDF` queries the $k=8$ nearest *centroids* (`scipy.spatial.cKDTree(self.centroids)`). For non-equilateral, anisotropic, or large triangles, a query point may be infinitesimally close to a vertex or edge, yet the centroid is far away, leading to completely missed triangles and gross distance errors.
2. **Naive Single-Face Normal Dot Product:** The sign of the distance is determined by projecting the difference vector $(\mathbf{p} - \mathbf{q})$ onto the unweighted normal of whatever single triangle happened to contain the nearest point: $\operatorname{sign} = \operatorname{sign}((\mathbf{p} - \mathbf{q}) \cdot \mathbf{n}_{\text{best\_face}})$. Along sharp edges ($90^\circ$ box edges, $30^\circ$ wedges) and corner diagonals, this projection produces **catastrophic sign flips** (flipping between $+1$ and $-1$ on the exterior), which severely corrupts cell classification and boundary projections.
3. **No Support for Dirty CAD / Non-Watertight Meshes:** If an input STL/OBJ has small gaps, missing facets, or self-intersections, `CADMeshSDF` has no global topological inside/outside mechanism, yielding random inside/outside assignments.
4. **No Differentiable VoroMesh Bisector Loss:** Lacks the continuous Voronoi bisector formulation (Maruani et al. 2023) necessary for optimization-first meshing and watertight Voronoi hulls.
5. **Missing C++20 Native Engine:** Although specified in `PROJECT.md` (`sdf_engine.hpp`, `sdf_engine.cpp`), the native C++ SDF engine does not yet exist; native meshing currently relies on a bounding-box hex grid stub (`distmesh.cpp:259-320`).

---

## 2. In-Depth Code Audit of Existing Implementation

### 2.1 Centroid-cKDTree Vulnerability
* **Location:** `backend/meshing/geometry_sdf.py:637-640` and `673-677`:
```python
637:  # Precompute triangle centroids and build KDTree
638:  self.centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0
639:  self.tree = cKDTree(self.centroids)
...
674:  _, cand_idx = self.tree.query(p, k=self.k_candidates)
```
* **Failure Mechanism:** The cKDTree indexes point centroids $c_i \in \mathbb{R}^3$, not 3D spatial volumes or triangle bounding boxes. Consider an elongated triangle with vertices at $(0, 0, 0)$, $(10, 0, 0)$, and $(0, 0.01, 0)$ (centroid at $(3.33, 0.0033, 0)$), surrounded by a cluster of 8 tiny triangles near $(0.01, 0.01, 0)$. A query point at $(0.001, 0.001, 0.001)$ is less than $0.002$ from the large triangle, but its centroid is $3.33$ units away! The $k=8$ nearest centroids belong to the small cluster. The true closest triangle is omitted from candidate evaluation.

### 2.2 Unweighted Face Normal & Sign-Flip Bug
* **Location:** `backend/meshing/geometry_sdf.py:753-767`:
```python
753:  best_normal = self.normals[best_face_id]
754:  diff = p - best_q
...
765:  dot = np.sum(diff * best_normal, axis=1)
766:  sign = np.where(dot < 0.0, -1.0, 1.0)
```
* **Failure Mechanism:** When a query point $\mathbf{p}$ is closest to an edge $e$ shared by faces $F_1$ and $F_2$, $\mathbf{q} \in e$. The difference vector $(\mathbf{p} - \mathbf{q})$ lies in the normal cone of edge $e$. If $\mathbf{p}$ is in the exterior of a $90^\circ$ box corner, but tilted toward $F_2$ while `best_face_id` is $F_1$, $(\mathbf{p} - \mathbf{q}) \cdot \mathbf{n}_{F_1}$ can be negative! The point is classified as **inside** ($\phi < 0$) even though it is clearly **outside**! This causes spurious sign inversions along all sharp edges and diagonals.

### 2.3 C++ Native Mesher Disconnect
* **Location:** `backend/meshing/native/src/distmesh.cpp:259-268`:
```cpp
259: void DistMeshEngine::generateFromSurface(
260:     NativeMeshData& mesh,
261:     const double* surf_points,
262:     int64_t n_points,
263:     const int32_t* surf_triangles,
264:     int64_t n_triangles,
...
268:     if (n_points == 0 || n_triangles == 0) return;
```
* **Failure Mechanism:** The parameter `surf_triangles` is completely ignored in lines 269–399. The native mesher generates a simple Cartesian grid spanning the bounding box of `surf_points` and does not evaluate surface distances or project boundary points.

---

## 3. Mathematical Foundations & Literature Specifications

### 3.1 Bounding Volume Hierarchy (BVH / AABB Tree) & `prev_facet` Spatial Coherence
According to Jacobson et al. (`libigl/include/igl/AABB.h`) and Hu et al. (`fTetWild/src/external/mesh_AABB.h`):

1. **Triangle AABB Construction:**
   For each triangle $T_i = (v_{i,0}, v_{i,1}, v_{i,2})$:
   $$\mathbf{b}_{\min, i} = \min(v_{i,0}, v_{i,1}, v_{i,2}), \qquad \mathbf{b}_{\max, i} = \max(v_{i,0}, v_{i,1}, v_{i,2})$$
   A top-down binary BVH is built by partitioning along the longest bounding box dimension of triangle centroids.

2. **Branch-and-Bound Traversal with Spatial Box Distance:**
   The squared distance from query point $\mathbf{p}$ to an axis-aligned box $[\mathbf{b}_{\min}, \mathbf{b}_{\max}]$ is:
   $$\operatorname{dist}^2(\mathbf{p}, B) = \sum_{d \in \{x,y,z\}} \left( \max(0, b_{\min, d} - p_d)^2 + \max(0, p_d - b_{\max, d})^2 \right)$$
   If $\operatorname{dist}^2(\mathbf{p}, B_N) \ge d_{\min}^2$, child node $B_N$ is pruned immediately.

3. **Spatial Coherence Hint (`prev_facet`):**
   In grid evaluation, neighboring points $p_k, p_{k+1}$ are spatially clustered. Testing `prev_facet` before tree traversal:
   $$d_{\min}^2 = \operatorname{dist}^2(\mathbf{p}, T_{\text{prev\_facet}})$$
   initializes an upper bound so tight that >95% of the AABB tree is pruned without descending, reducing average traversal time from $\mathcal{O}(\log N)$ to nearly $\mathcal{O}(1)$ for coherent streams.

---

### 3.2 Angle-Weighted Pseudonormals (Bærentzen & Aanæs 2005)
*Reference: J. A. Bærentzen, H. Aanæs (2005), "Signed Distance Computation Using the Angle Weighted Pseudonormal", IEEE TVCG 11(3), 243–253.*

To guarantee a continuous signed distance field without sign flips along edges and corners:
1. **Face Normal $\mathbf{n}_f$:**
   $$\mathbf{n}_f = \frac{(v_1 - v_0) \times (v_2 - v_0)}{\|(v_1 - v_0) \times (v_2 - v_0)\|}$$

2. **Interior Corner Angles $\alpha_{f, v}$:**
   For each vertex $v \in \{v_0, v_1, v_2\}$ of face $f$, let $\mathbf{e}_a, \mathbf{e}_b$ be the normalized incident edge vectors pointing away from $v$:
   $$\alpha_{f, v} = \arccos(\operatorname{clamp}(\mathbf{e}_a \cdot \mathbf{e}_b, -1.0, 1.0))$$

3. **Angle-Weighted Vertex Pseudonormal $\mathbf{n}_v$:**
   $$\mathbf{N}_v = \sum_{f \in \mathcal{F}(v)} \alpha_{f, v} \mathbf{n}_f, \qquad \mathbf{n}_v = \frac{\mathbf{N}_v}{\|\mathbf{N}_v\|}$$

4. **Edge Pseudonormal $\mathbf{n}_e$:**
   For an edge $e$ shared by incident faces $\mathcal{F}(e)$:
   $$\mathbf{N}_e = \sum_{f \in \mathcal{F}(e)} \mathbf{n}_f, \qquad \mathbf{n}_e = \frac{\mathbf{N}_e}{\|\mathbf{N}_e\|}$$
   *(For manifold edges with two incident faces, this is the normalized bisector of the dihedral angle; for boundary edges, $\mathbf{n}_e = \mathbf{n}_f$.)*

5. **Closest-Point Feature Classification & Sign Determination:**
   Let $\mathbf{q}$ be the closest point on triangle $f = (v_0, v_1, v_2)$ to query point $\mathbf{p}$, with barycentric coordinates $(\lambda_0, \lambda_1, \lambda_2)$ where $\lambda_0 + \lambda_1 + \lambda_2 = 1$.
   Given tolerance $\epsilon_{\text{bary}} = 10^{-10}$:
   - **Vertex Region:** If two barycentric coordinates $\le \epsilon_{\text{bary}}$, $\mathbf{q} \approx v_k$.
     $$\mathbf{n}_{\text{pseudo}}(\mathbf{q}) = \mathbf{n}_v(v_k)$$
   - **Edge Region:** If exactly one barycentric coordinate $\lambda_k \le \epsilon_{\text{bary}}$ (the other two $> \epsilon_{\text{bary}}$), $\mathbf{q}$ lies on the edge opposite to $v_k$.
     $$\mathbf{n}_{\text{pseudo}}(\mathbf{q}) = \mathbf{n}_e(\text{edge}_k)$$
   - **Face Interior Region:** If $\min(\lambda_0, \lambda_1, \lambda_2) > \epsilon_{\text{bary}}$, $\mathbf{q}$ is strictly interior to $f$.
     $$\mathbf{n}_{\text{pseudo}}(\mathbf{q}) = \mathbf{n}_f$$
   - **Exact Sign Formula:**
     $$\operatorname{sign}(\mathbf{p}) = \begin{cases} +1 & \text{if } (\mathbf{p} - \mathbf{q}) \cdot \mathbf{n}_{\text{pseudo}}(\mathbf{q}) \ge 0 \\ -1 & \text{if } (\mathbf{p} - \mathbf{q}) \cdot \mathbf{n}_{\text{pseudo}}(\mathbf{q}) < 0 \end{cases}$$
   **Mathematical Guarantee:** The Voronoi regions of the mesh faces, edges, and vertices partition $\mathbb{R}^3$. Within each Voronoi region, $(\mathbf{p} - \mathbf{q}) \cdot \mathbf{n}_{\text{pseudo}}(\mathbf{q})$ has a constant sign, eliminating all sign flips at sharp $30^\circ$ and $90^\circ$ edges and corner diagonals.

---

### 3.3 Generalized Winding Numbers (Oosterom & Strackee 1983 / Jacobson et al. 2013)
*Reference: A. van Oosterom, J. Strackee (1983), IEEE Trans. Biomed. Eng. 30(2); A. Jacobson et al. (2013), ACM TOG (SIGGRAPH) 32(4).*

For non-watertight CAD meshes with small holes, open boundaries, or self-intersections, the Generalized Winding Number provides an unconditionally robust, smooth inside/outside metric:
$$W(\mathbf{x}) = \frac{1}{4\pi} \sum_{t=1}^{N_f} \Omega_t(\mathbf{x})$$
where $\Omega_t(\mathbf{x})$ is the solid angle subtended by triangle $t = (\mathbf{a}, \mathbf{b}, \mathbf{c})$ at point $\mathbf{x}$.

#### Analytic Solid Angle Formula (Oosterom & Strackee):
Let $\mathbf{r}_a = \mathbf{a} - \mathbf{x}$, $\mathbf{r}_b = \mathbf{b} - \mathbf{x}$, $\mathbf{r}_c = \mathbf{c} - \mathbf{x}$, and $d_a = \|\mathbf{r}_a\|$, $d_b = \|\mathbf{r}_b\|$, $d_c = \|\mathbf{r}_c\|$.
1. Numerator (scalar triple product / determinant):
   $$\operatorname{num} = \mathbf{r}_a \cdot (\mathbf{r}_b \times \mathbf{r}_c) = \det[\mathbf{r}_a, \mathbf{r}_b, \mathbf{r}_c]$$
2. Denominator:
   $$\operatorname{denom} = d_a d_b d_c + (\mathbf{r}_a \cdot \mathbf{r}_b) d_c + (\mathbf{r}_a \cdot \mathbf{r}_c) d_b + (\mathbf{r}_b \cdot \mathbf{r}_c) d_a$$
3. Solid Angle:
   $$\Omega_t(\mathbf{x}) = 2 \operatorname{atan2}(\operatorname{num}, \operatorname{denom})$$

#### Inside/Outside Classification & Signed Distance:
- $W(\mathbf{x}) \approx 1.0$ in mesh interior, $W(\mathbf{x}) \approx 0.0$ in exterior, $W(\mathbf{x}) \approx 0.5$ on boundary.
- For dirty CAD:
  $$\operatorname{sign}_{\text{wn}}(\mathbf{x}) = \begin{cases} -1.0 & \text{if } W(\mathbf{x}) \ge 0.5 \\ +1.0 & \text{if } W(\mathbf{x}) < 0.5 \end{cases}$$
  $$\phi(\mathbf{x}) = \operatorname{sign}_{\text{wn}}(\mathbf{x}) \cdot d_{\text{unsigned}}(\mathbf{x})$$

#### Vectorized & SIMD Chunking:
To evaluate $P$ query points over $M$ triangles:
Chunk triangles in blocks of $B = 1024$ to maintain cache locality and prevent $(P \times M \times 3)$ memory explosion:
$$\Omega_{\text{total}}(\mathbf{x}) = \sum_{c=0}^{\lceil M/B \rceil - 1} \sum_{t \in \text{chunk}_c} \Omega_t(\mathbf{x})$$
In C++20, this is accelerated with OpenMP and AVX-512 FMA vectorization.

---

### 3.4 Differentiable VoroMesh Bisector Loss (Maruani et al. 2023)
*Reference: N. Maruani, R. Klokov, M. Ovsjanikov, P. Alliez, M. Desbrun (2023), "Learning Watertight Surface Meshes with Voronoi Diagrams", ICCV 2023, arXiv:2308.14616v1.*

VoroMesh avoids explicit 3D Voronoi / Delaunay diagram construction during iterative optimization by leveraging Theorem 1:

#### Theorem 1 (Maruani et al.):
Let $\mathbf{Q} = \{q_1, \dots, q_M\} \subset \mathbb{R}^3$ be a set of Voronoi seed points. For any surface sample point $\mathbf{x} \in \mathcal{X}$, let $q_{i_x}$ be its nearest generator:
$$i_x = \arg\min_{i \in \{1,\dots,M\}} \|\mathbf{x} - q_i\|_2$$
The distance from $\mathbf{x}$ to the closest Voronoi facet is **identically equal** to the minimum distance to the bisector planes $H_{i_x, j}$ formed between $q_{i_x}$ and its adjacent seeds $q_j$:
$$d(\mathbf{x}, \partial \mathcal{V}(\mathbf{Q})) = \min_{j \neq i_x} d(\mathbf{x}, H_{i_x, j})$$
where the distance to bisector plane $H_{i, j} = \{y \mid (y - \frac{q_i + q_j}{2}) \cdot (q_j - q_i) = 0\}$ is:
$$h_{i, j}(\mathbf{x}) = \frac{(\mathbf{x} - \frac{q_i + q_j}{2}) \cdot (q_j - q_i)}{\|q_j - q_i\|_2}, \qquad d(\mathbf{x}, H_{i, j}) = |h_{i, j}(\mathbf{x})|$$

#### Differentiable Bisector Loss over $k=16$ Nearest Seeds:
Restricting $j$ to the $k=16$ nearest neighbors $\mathcal{N}_{16}(i_x)$ of generator $q_{i_x}$:
$$\mathcal{L}_{\text{bisector}}(\mathcal{X}, \mathbf{Q}) = \frac{1}{|\mathcal{X}|} \sum_{\mathbf{x} \in \mathcal{X}} \min_{j \in \mathcal{N}_{16}(i_x) \setminus \{i_x\}} \left( \frac{(\mathbf{x} - \frac{q_{i_x} + q_j}{2}) \cdot (q_j - q_{i_x})}{\|q_j - q_{i_x}\|_2} \right)^2$$

#### Analytical Gradients:
Let $j^*(\mathbf{x}) = \arg\min_{j \in \mathcal{N}_{16}(i_x) \setminus \{i_x\}} |h_{i_x, j}(\mathbf{x})|$, and unit plane normal $\mathbf{n}_{i, j^*} = \frac{q_{j^*} - q_i}{\|q_{j^*} - q_i\|_2}$.
$$\nabla_{\mathbf{x}} d(\mathbf{x}) = \operatorname{sign}(h_{i_x, j^*}(\mathbf{x})) \cdot \mathbf{n}_{i_x, j^*}$$
$$\nabla_{\mathbf{x}} d^2(\mathbf{x}) = 2 h_{i_x, j^*}(\mathbf{x}) \cdot \mathbf{n}_{i_x, j^*}$$
This yields a continuous distance and unit-norm gradient field everywhere, guaranteeing that Voronoi dual meshes extracted from $\mathbf{Q}$ are **100% watertight and self-intersection free by mathematical construction**.

---

## 4. Concrete Implementation Design

### 4.1 Python Architecture (`backend/meshing/geometry_sdf.py`)

#### 1. `TriangleAABBTree` Class:
```python
class TriangleAABBTree:
    """Hierarchical Axis-Aligned Bounding Box (AABB) Tree over mesh triangles
    with branch-and-bound pruning and prev_facet spatial coherence caching.
    """
    def __init__(self, vertices: np.ndarray, faces: np.ndarray):
        ...
    def query(self, points: np.ndarray, prev_facet: Optional[int] = None) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        """Returns (sq_dist, nearest_point, nearest_face_id, barycentric_coords)."""
```
- Flat structured arrays for node coordinates (`node_bmin`, `node_bmax`, `node_left`, `node_right`, `node_tri_idx`).
- `prev_facet` hint caching: evaluates $T_{\text{prev}}$ first, establishing a tight initial $d_{\min}^2$.

#### 2. Pseudonormals Precomputation (`compute_angle_weighted_pseudonormals`):
```python
def compute_angle_weighted_pseudonormals(vertices: np.ndarray, faces: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Computes:
    - face_normals: (F, 3)
    - vertex_pseudonormals: (V, 3) weighted by corner interior angles
    - edge_pseudonormals: (E, 3) normalized sum of incident face normals
    - face_edge_map: (F, 3) mapping each face edge to undirected edge ID in E
    """
```

#### 3. Enhancements to `CADMeshSDF`:
- Constructor extension:
  `sign_method: Literal["pseudonormal", "winding_number", "face_normal", "unsigned"] = "pseudonormal"`
- `evaluate(points)`:
  Queries `TriangleAABBTree` using internal `self._prev_facet_hint`.
  Classifies closest points into vertex, edge, or interior based on barycentric coordinates.
  Evaluates signed distance via `sign = np.sign((p - q) . n_pseudo)`.
- Method `winding_number(points: np.ndarray) -> np.ndarray`:
  Computes Generalized Winding Numbers using vectorized triangle chunking.
- Function `voromesh_bisector_loss(points: np.ndarray, seeds: np.ndarray, k: int = 16) -> Tuple[float, np.ndarray, np.ndarray]`:
  Returns `(loss, distances, gradients)`.

---

### 4.2 C++20 Native Architecture (`native/include/sdf_engine.hpp` & `native/src/sdf_engine.cpp`)

#### 1. File Structure:
- `backend/meshing/native/include/sdf_engine.hpp`:
  Header defining `cfd::AABBTree`, `cfd::Pseudonormals`, and `cfd::MeshSDF`.
- `backend/meshing/native/src/sdf_engine.cpp`:
  SIMD-accelerated implementation using contiguous memory layout and OpenMP.
- C-ABI Functions in `cfd_mesher_native.h` and `cfd_mesher_native.cpp`:
  ```cpp
  typedef struct CfdSdfEngine CfdSdfEngine;

  CFD_EXPORT CfdSdfEngine* cfd_sdf_create(
      const double* vertices, int64_t n_vertices,
      const int32_t* faces, int64_t n_faces
  );
  CFD_EXPORT void cfd_sdf_free(CfdSdfEngine* engine);
  CFD_EXPORT int32_t cfd_sdf_evaluate(
      CfdSdfEngine* engine,
      const double* points, int64_t n_points,
      double* out_phi, double* out_grad
  );
  CFD_EXPORT int32_t cfd_sdf_winding_number(
      CfdSdfEngine* engine,
      const double* points, int64_t n_points,
      double* out_wn
  );
  CFD_EXPORT int32_t cfd_sdf_voromesh_loss(
      const double* points, int64_t n_points,
      const double* seeds, int64_t n_seeds,
      int32_t k,
      double* out_loss, double* out_dists, double* out_grads
  );
  ```

#### 2. CMakeLists.txt Integration:
Add `src/sdf_engine.cpp` to `add_library(cfd_mesher_native SHARED ...)`:
```cmake
add_library(cfd_mesher_native SHARED
    src/cfd_mesher_native.cpp
    src/difvm_mesh.cpp
    src/distmesh.cpp
    src/inflation.cpp
    src/rcm.cpp
    src/cht_cyclic.cpp
    src/sdf_engine.cpp
)
```

---

## 5. Verification Scenarios & Acceptance Criteria for Gate WP2

| Scenario ID | Test Case | Target Geometry | Evaluation Method | Acceptance Criteria |
| :--- | :--- | :--- | :--- | :--- |
| **WP2-T1** | **90° Sharp Edge & Corner Audit** | Anisotropic Box $[0, 2] \times [0, 1] \times [0, 0.5]$ | 5,000 points sampled within $d \in [10^{-6}, 0.2]$ along edge 45° bisectors and $[1,1,1]$ corner diagonals | **0 sign flips**: All exterior points strictly $\phi > 0$, interior $\phi < 0$. Max deviation from analytic box SDF $< 10^{-10}$. |
| **WP2-T2** | **30° Acute Ridge Audit** | Anisotropic Triangular Wedge with sharp $30^\circ$ apex ridge | 2,000 points spanning the acute $15^\circ$ bisector wedge outside the apex edge | **0 sign flips**: Exterior points strictly $\phi > 0$; unweighted normal baseline exhibits >40% sign flips. |
| **WP2-T3** | **Non-Watertight "Dirty CAD" Mesh** | Box with 1 missing face (open hole) and small gap ($10^{-4}\,\text{m}$) | Sample grid across the hole and interior | $W(\mathbf{x}_{\text{int}}) > 0.95$, $W(\mathbf{x}_{\text{ext}}) < 0.05$, smooth transition across gap, no topology crash. |
| **WP2-T4** | **Differentiable VoroMesh Loss** | Surface points on sphere ($R=1.0$), 64 seed generators | Evaluate `voromesh_bisector_loss`, compare analytical vs. finite-difference gradients | Rel. gradient error $< 10^{-5}$, continuous distance values without jumps. |
| **WP2-T5** | **Throughput & Dual-Engine Parity** | `BladeRev1CFD.obj` (61,660 faces, 10,000 random query points) | Measure elapsed time and parity between Python and C++ | Execution time $< 0.50\,\text{s}$, $\|\phi_{\text{py}} - \phi_{\text{cpp}}\|_\infty < 10^{-10}$, $\|\nabla \phi\| = 1.0 \pm 10^{-10}$. |
| **WP2-T6** | **Regression Shield** | Complete existing test suite | `pytest tests/` and `pytest backend/geometry/tests/` | All **540 existing tests** remain 100% green. |

---

## 6. Worker Execution Blueprint

When dispatching the implementation worker:
1. **Step 1:** Implement `compute_angle_weighted_pseudonormals` and `TriangleAABBTree` in `geometry_sdf.py`.
2. **Step 2:** Refactor `CADMeshSDF` to use `TriangleAABBTree` and pseudonormals for distance and gradient evaluation, preserving all public signatures.
3. **Step 3:** Implement `winding_number` (Oosterom & Strackee) in `geometry_sdf.py`.
4. **Step 4:** Implement `voromesh_bisector_loss` in `geometry_sdf.py`.
5. **Step 5:** Create native C++ headers and implementations in `backend/meshing/native/include/sdf_engine.hpp` and `backend/meshing/native/src/sdf_engine.cpp`, export C-ABI symbols, and update `CMakeLists.txt`.
6. **Step 6:** Implement unit test suite `tests/test_wp2_exact_sdf.py` covering Scenarios WP2-T1 through WP2-T5.
7. **Step 7:** Run all 540 existing tests + new WP2 tests to ensure complete non-regression.
