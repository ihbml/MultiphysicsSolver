# Handoff Report: Milestone R1 (Phase 1 Audit Findings Remediation)

## 1. Observation

Direct observations from source inspection, compilation checks, and test runner execution:

### 1.1 Finding 1: Python/C++ Parity in MeshErrorMap
- **Python implementation** (`backend/meshing/difvm_topology.py:989`):
  ```python
  beta = max(0.0, min(1.0, cos_theta / (1.0 + skew)))
  ```
  and lines 1034–1037:
  ```python
  v_mean = float(np.mean(cell_volumes)) if n_cells > 0 else 1.0
  omega_cell = np.clip(cell_volumes / (v_mean + 1e-15), 0.1, 1.0)
  wsrd_alpha = np.ones(n_cells, dtype=np.float64)
  ```
- **C++ implementation** (`backend/meshing/native/src/difvm_mesh.cpp:495-497`):
  ```cpp
  double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
  double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
  mesh.beta_upwind[f] = std::min(beta_ortho, beta_skew);
  ```
  and lines 533–558:
  ```cpp
  // 7. Cell-wise factors: omega_cell and wsrd_alpha
  std::vector<double> min_beta_per_cell(n_cells, 1.0);
  for (int64_t f = 0; f < n_faces; ++f) {
      int64_t o = mesh.owner[f];
      int64_t nb = mesh.neighbour[f];
      min_beta_per_cell[o] = std::min(min_beta_per_cell[o], mesh.beta_upwind[f]);
      if (nb >= 0) {
          min_beta_per_cell[nb] = std::min(min_beta_per_cell[nb], mesh.beta_upwind[f]);
      }
  }
  for (int64_t c = 0; c < n_cells; ++c) {
      mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c]);
  }

  // wsrd_alpha: Berger-Giuliani volume ratio against median positive cell volume
  std::vector<double> pos_vols;
  for (double v : mesh.cell_volumes) {
      if (v > 1e-60) pos_vols.push_back(v);
  }
  double v_target = 1.0;
  if (!pos_vols.empty()) {
      std::sort(pos_vols.begin(), pos_vols.end());
      v_target = pos_vols[pos_vols.size() / 2];
  }
  for (int64_t c = 0; c < n_cells; ++c) {
      mesh.wsrd_alpha[c] = std::clamp(mesh.cell_volumes[c] / (v_target > 1e-60 ? v_target : 1.0), 0.0, 1.0);
  }
  ```
  In Python, `omega_cell` and `wsrd_alpha` were swapped and simplified: `omega_cell` used volume ratio against mean, while `wsrd_alpha` was set to all ones. In C++, `omega_cell` is the incident face minimum of `beta_upwind` clamped to `[0.1, 1.0]`, and `wsrd_alpha` is the Berger-Giuliani volume ratio clamped to `[0.0, 1.0]` against median positive cell volume. Furthermore, `beta_upwind` in Python used a heuristic `cos_theta / (1 + skew)` instead of the dual-clamped `min(beta_ortho, beta_skew)`.

### 1.2 Finding 2: Slow Python Pre-Loop in `compute_difvm_topology_native`
- **Python implementation** (`backend/meshing/difvm_topology.py:464-478`):
  ```python
  # Strict manifoldness check: reject meshes where a face is shared by >2 cells
  face_seen: Dict[Tuple[int, ...], int] = {}
  for c in cells:
      cfaces = _extract_cell_faces_py(np.asarray(c, dtype=np.int64), pts)
      for fn in cfaces:
          k = tuple(sorted(fn))
          if k in face_seen:
              if face_seen[k] >= 2:
                  raise ValueError(
                      f"Non-manifold mesh detected: face shared by >2 cells ({k})"
                  )
              face_seen[k] += 1
          else:
              face_seen[k] = 1
  ```
- **C++ implementation** (`backend/meshing/native/src/difvm_mesh.cpp:264-266`):
  ```cpp
  if (temp_faces[f_idx].neighbour != -1) {
      throw std::runtime_error("Non-manifold mesh detected: face shared by >2 cells");
  }
  ```
  and `backend/meshing/native/src/cfd_mesher_native.cpp:160-165`:
  ```cpp
  } catch (const std::exception&) {
    return -2;
  }
  ```
  The Python pre-loop redundant loop duplicates work already performed inside the native kernel at C++ speed. When the native kernel encounters a face shared by >2 cells, it throws `std::runtime_error`, which `cfd_mesher_build_from_cells` catches and returns `-2`.

### 1.3 Finding 3: Outward Orientation of all Cell Faces
- **Python implementation** (`backend/meshing/difvm_topology.py:306-386`):
  `_extract_cell_faces_py` returns face node lists based on canonical indices, but does not test whether the face area normal vectors point away from the cell centroid `cell_c`. If cells are passed with clockwise node ordering (e.g. 2D quads/triangles or inverted 3D elements), inward-pointing normals are created.
- **C++ implementation** (`backend/meshing/native/src/difvm_mesh.cpp:98-172`):
  `extractCellFaces` constructs face node lists without testing the dot product between the face area normal and `(fc - cell_c)`.

### 1.4 Finding 4: Boundary Patch Coverage in `validate()` & Thread-Local `prev_facet` in `MeshSDF::evaluate`
- **Python validation** (`backend/meshing/models.py:364-375`):
  `UnstructuredMesh.validate()` checks `cell_volumes > 0`, block ordering (`neighbour[:n_int] >= 0` and `neighbour[n_int:] == -1`), `owner < neighbour`, Gauss divergence, and projection collinearity, but does NOT inspect `self.boundaries` to verify that patch ranges `[start, start + count)` stay in `[n_int, n_faces)`, do not overlap, and sum to `n_bnd`.
- **C++ SDF evaluator** (`backend/meshing/native/src/sdf_engine.cpp:453`):
  ```cpp
  #ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 64)
  #endif
    for (int64_t i = 0; i < n_points; ++i) {
      ...
      tree_.closest_point(p, -1, q, facet, bary, dist);
  ```
  `closest_point` was invoked with hardcoded `-1`, defeating the spatial coherence caching mechanism implemented in `TriangleAABBTree::closest_point` (`sdf_engine.cpp:243-252`).

### 1.5 Finding 5: Optional `seed_signs` Filter in `voromesh_bisector_loss`
- **Python implementation** (`backend/meshing/geometry_sdf.py:1057`):
  `voromesh_bisector_loss(points, seeds, k=16, use_native=True)` lacks the `seed_signs` parameter. It considers all $k$ nearest neighbors of the closest seed regardless of whether they cross the domain boundary.
- **C++ implementation** (`backend/meshing/native/src/sdf_engine.cpp:592-607`):
  `MeshSDF::voromesh_loss` iterates over all $k$ neighbors without checking if generator seeds have opposing signs (`seed_signs[i] * seed_signs[j] < 0`).
- **C-ABI** (`backend/meshing/native/include/cfd_mesher_native.h:170`, `src/cfd_mesher_native.cpp:533`):
  `cfd_sdf_voromesh_loss` accepts 8 arguments without a `seed_signs` pointer.

### 1.6 Build and Test Environment Verification
- **CMake / MSVC**:
  Located at: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
  Native DLL compiles via:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release`
  Target artifact: `G:\Datein\Projects\CFDSolver\backend\meshing\cfd_mesher_native.dll`.
- **Python / Pytest**:
  Python 3.14.7 executable at `.\.venv\Scripts\python.exe`.
  Baseline test collection: 567 tests collected in 0.81s (`.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests --collect-only`).
  `tests/test_wp1_topology_polymesh.py` and `tests/test_wp2_mesh_sdf.py`: 15 passed in 4.13s.

---

## 2. Logic Chain

### 2.1 MeshErrorMap Parity (Finding 1)
1. In FVM on arbitrary polyhedra, the local upwind blending factor $\beta_{upwind}$ dictates convection stabilization. To guarantee numerical stability, it must be throttled by non-orthogonality angle $\theta$ and cell skewness:
   $$\beta_{ortho} = \text{clamp}\left(\frac{75^\circ - \theta}{35^\circ}, 0, 1\right), \quad \beta_{skew} = \text{clamp}\left(\frac{1.0 - \text{skew}}{0.6}, 0, 1\right), \quad \beta_{upwind} = \min(\beta_{ortho}, \beta_{skew})$$
   When $S_f \cdot d_f \le 0$ or face is folded, escalation to Tier 3 enforces $\beta_{upwind} = 0.0$.
2. The cell-wise damping coefficient $\omega_{cell}$ ensures cell-bounded convergence by taking the minimum $\beta_{upwind}$ over all incident faces (both internal where cell is owner or neighbour, and boundary where cell is owner):
   $$\omega_c = \max\left(0.1, \min_{f \in \mathcal{F}(c)} \beta_{upwind, f}\right)$$
   In Python, this is computed vectorially in $O(N_f)$ via `np.minimum.at(min_beta, owner, beta_upwind)` and `np.minimum.at(min_beta, neighbour[int_mask], beta_upwind[int_mask])`, followed by `np.clip(min_beta, 0.1, 1.0)`.
3. The Berger-Giuliani WSRD weight factor $\alpha_c$ scales with cell volume relative to the domain median positive cell volume:
   $$\alpha_c = \text{clip}\left(\frac{V_c}{V_{median} + 10^{-30}}, 0.0, 1.0\right)$$
4. Aligning Python with C++ eliminates dual-engine drift and ensures identical convergence across pure-Python and native solvers.

### 2.2 Removing the Python Pre-Loop (Finding 2)
1. `compute_difvm_topology_native` previously executed an $O(N_c)$ pure-Python loop calling `_extract_cell_faces_py` and building a dictionary `face_seen` to detect non-manifold faces shared by $>2$ cells.
2. In C++, `DiFVMMesh::build_from_cells` (`difvm_mesh.cpp:264`) performs this exact check during face map construction in compiled C++:
   ```cpp
   if (temp_faces[f_idx].neighbour != -1) {
       throw std::runtime_error("Non-manifold mesh detected: face shared by >2 cells");
   }
   ```
3. In `cfd_mesher_native.cpp:160`, any `std::exception` thrown by `buildTopology` is caught and translated to return code `-2`.
4. In `compute_difvm_topology_native`, inspecting `res == -2` and raising `ValueError("Non-manifold mesh: a face is shared by more than 2 cells.")` maintains exact semantic compatibility while eliminating redundant Python overhead.

### 2.3 Outward Cell Face Orientation (Finding 3)
1. Standard FVM topological conventions (OpenFOAM, DiFVM) require that the face normal of a cell boundary points strictly outward from the cell interior.
2. For any cell $c$, its geometric centroid is $c_c = \frac{1}{N_v} \sum_{v \in c} x_v$.
3. For each face $f$ of cell $c$, with centroid $f_c$ and face normal $f_n$:
   - In 3D: $f_n = \frac{1}{2} \sum_{i=1}^{n-2} (x_{i} - x_0) \times (x_{i+1} - x_0)$.
   - In 2D: for edge $(p_0, p_1)$ with $\Delta x = x_1 - x_0, \Delta y = y_1 - y_0$, $f_n = [\Delta y, -\Delta x, 0]^T$.
4. The vector from cell centroid to face centroid is $d_{out} = f_c - c_c$.
5. If $f_n \cdot d_{out} < 0$, the normal vector points inward toward the cell center. Reversing the node sequence ($[v_{n-1}, \dots, v_0]$ via `f_nodes[::-1]` in Python and `std::reverse` in C++) reverses the cross-product sign (and edge tangent vector), forcing $f_n \cdot d_{out} > 0$.

### 2.4 Boundary Coverage & Thread-Local `prev_facet` (Finding 4)
1. In `UnstructuredMesh.validate()`, if `self.boundaries` is populated, every boundary face index must belong to exactly one patch. The valid boundary index interval is $[n_{int}, n_{faces})$. Checking bounds $[start, start + count) \subseteq [n_{int}, n_{faces})$, disjointness (no overlap), and total count $\sum count = n_{bnd}$ prevents corrupted OpenFOAM boundary definitions.
2. In `MeshSDF::evaluate`, `TriangleAABBTree::closest_point` checks if `prev_facet` provides a tight upper bound $d_{sq} = \|p - q_{prev}\|^2$ to prune branch-and-bound AABB traversal.
3. In multi-threaded OpenMP execution, sharing a single mutable `prev_facet` would introduce data races. Declaring `int32_t prev_facet = -1;` within the thread-private scope of `#pragma omp parallel` guarantees race-free spatial coherence for contiguous point queries assigned to each thread.

### 2.5 Optional `seed_signs` Filter in `voromesh_bisector_loss` (Finding 5)
1. In VoroMesh (Maruani et al. 2023), the surface boundary is defined exclusively by Voronoi facets separating interior seeds ($s_i = -1$) from exterior seeds ($s_j = +1$).
2. Internal Voronoi facets separating two interior seeds ($(-1)(-1) > 0$) or two exterior seeds ($(+1)(+1) > 0$) are not part of the boundary.
3. Adding optional parameter `seed_signs: Optional[np.ndarray] = None` allows callers to filter neighbor pairs $(i, j)$ so only pairs with `seed_signs[i] * seed_signs[j] < 0` are evaluated.
4. If no neighbor among $k$ possesses an opposing sign, falling back to all $k$ neighbors maintains numerical continuity and prevents infinite loss.
5. Implementing this filter with identical semantics across Python, C-ABI, and C++ preserves 100% dual-engine parity down to machine precision.

---

## 3. Caveats

1. **Strictly Read-Only Phase**: This report provides an exact blueprint. No production source files were modified during this investigation.
2. **C-ABI Signature Extension**: Updating `cfd_sdf_voromesh_loss` to accept `const double* seed_signs` extends the C-ABI function signature from 8 to 9 parameters. Recompilation of `cfd_mesher_native.dll` and updating `geometry_sdf.py`'s `ctypes.argtypes` must occur concurrently to prevent ABI mismatch.
3. **OpenMP Environment**: In single-threaded builds or when `_OPENMP` is undefined, the thread-private block compiles cleanly as a sequential block with stack-allocated `prev_facet`.

---

## 4. Conclusion & Concrete Blueprint

Below is the exact, diff-ready blueprint for implementing all 5 audit points.

### 4.1 Code Blueprint: Finding 1 (Parity in MeshErrorMap)
**Target File**: `backend/meshing/difvm_topology.py` (lines 988–1038)

```python
<<<<
            else:
                if theta_deg >= 75.0 or skew >= 1.0:
                    tier = 3
                elif theta_deg >= 65.0 or skew >= 0.7:
                    tier = 2
                elif theta_deg >= 40.0 or skew >= 0.4:
                    tier = 1
                beta = max(0.0, min(1.0, cos_theta / (1.0 + skew)))
            stab_mask[new_idx] = tier
            beta_upwind[new_idx] = beta
====
            else:
                beta_ortho = max(0.0, min(1.0, (75.0 - theta_deg) / 35.0))
                beta_skew = max(0.0, min(1.0, (1.0 - skew) / 0.6))
                beta = min(beta_ortho, beta_skew)

                if theta_deg >= 75.0 or skew >= 1.0:
                    tier = 3
                    beta = 0.0
                elif theta_deg >= 65.0 or skew >= 0.7:
                    tier = 2
                elif theta_deg >= 40.0 or skew >= 0.4:
                    tier = 1
            stab_mask[new_idx] = tier
            beta_upwind[new_idx] = beta
>>>>
```

and lines 1034–1037:
```python
<<<<
    v_mean = float(np.mean(cell_volumes)) if n_cells > 0 else 1.0
    omega_cell = np.clip(cell_volumes / (v_mean + 1e-15), 0.1, 1.0)
    wsrd_alpha = np.ones(n_cells, dtype=np.float64)
====
    # 7. Cell-wise factors: omega_cell and wsrd_alpha
    min_beta_per_cell = np.ones(n_cells, dtype=np.float64)
    if n_faces > 0:
        np.minimum.at(min_beta_per_cell, owner, beta_upwind)
        int_mask = neighbour >= 0
        if np.any(int_mask):
            np.minimum.at(min_beta_per_cell, neighbour[int_mask], beta_upwind[int_mask])
    omega_cell = np.clip(min_beta_per_cell, 0.1, 1.0)

    # wsrd_alpha: Berger-Giuliani volume ratio against median positive cell volume
    pos_vols = cell_volumes[cell_volumes > 1e-60]
    v_target = float(np.median(pos_vols)) if len(pos_vols) > 0 else 1.0
    v_denom = v_target if v_target > 1e-60 else 1.0
    wsrd_alpha = np.clip(cell_volumes / (v_denom + 1e-30), 0.0, 1.0)
>>>>
```

---

### 4.2 Code Blueprint: Finding 2 (Remove Slow Python Pre-Loop)
**Target File**: `backend/meshing/difvm_topology.py` (lines 464–511)

```python
<<<<
    # Strict manifoldness check: reject meshes where a face is shared by >2 cells
    face_seen: Dict[Tuple[int, ...], int] = {}
    for c in cells:
        cfaces = _extract_cell_faces_py(np.asarray(c, dtype=np.int64), pts)
        for fn in cfaces:
            k = tuple(sorted(fn))
            if k in face_seen:
                if face_seen[k] >= 2:
                    raise ValueError(
                        f"Non-manifold mesh detected: face shared by >2 cells ({k})"
                    )
                face_seen[k] += 1
            else:
                face_seen[k] = 1

    # Convert cells to CSR offsets and flat_nodes
====
    # Convert cells to CSR offsets and flat_nodes (manifoldness validated in native C++)
>>>>
```

and lines 508–511:
```python
<<<<
        if res != 0:
            if res == -2:
                raise ValueError("Non-manifold mesh detected: face shared by >2 cells")
            raise RuntimeError(f"Native cfd_mesher_build_from_cells failed with code {res}")
====
        if res != 0:
            if res == -2:
                raise ValueError("Non-manifold mesh: a face is shared by more than 2 cells.")
            raise RuntimeError(f"Native cfd_mesher_build_from_cells failed with code {res}")
>>>>
```

---

### 4.3 Code Blueprint: Finding 3 (Outward Cell Face Orientation)
**Target File**: `backend/meshing/difvm_topology.py` (`_extract_cell_faces_py`, lines 306–386)

```python
<<<<
def _extract_cell_faces_py(
    cell_nodes: np.ndarray,
    points: Optional[np.ndarray] = None,
) -> List[List[int]]:
...
    else:
        # Fallback polygon
        return [[int(c[i]), int(c[(i + 1) % nv])] for i in range(nv)]
====
def _extract_cell_faces_py(
    cell_nodes: np.ndarray,
    points: Optional[np.ndarray] = None,
) -> List[List[int]]:
...
    # (Assemble unoriented faces into local variable `faces`)
...
    if points is not None:
        cell_c = np.mean(points[cell_nodes], axis=0)
        oriented_faces: List[List[int]] = []
        for fn in faces:
            norm_v, cent_v = _compute_polygon_normal_and_centroid_py(fn, points)
            if float(np.dot(norm_v, cent_v - cell_c)) < 0.0:
                fn = list(reversed(fn))
            oriented_faces.append(fn)
        return oriented_faces
    return faces
>>>>
```

**Target File**: `backend/meshing/native/src/difvm_mesh.cpp` (`extractCellFaces`, lines 98–172)

```cpp
<<<<
  }
  return faces;
}
====
  }

  // Ensure all extracted face normals point strictly outward from cell centroid
  Vec3 cell_c(0, 0, 0);
  for (int64_t v : cell) {
    cell_c = cell_c + node_coords[v];
  }
  if (!cell.empty()) {
    cell_c = cell_c * (1.0 / static_cast<double>(cell.size()));
  }

  for (auto& f : faces) {
    auto [fn, fc] = computePolygonNormalAndCentroid(f, node_coords);
    if (dot(fn, fc - cell_c) < 0.0) {
      std::reverse(f.begin(), f.end());
    }
  }

  return faces;
}
>>>>
```

---

### 4.4 Code Blueprint: Finding 4 (Boundary Coverage & Thread-Local `prev_facet`)
**Target File**: `backend/meshing/models.py` (`UnstructuredMesh.validate()`, around line 372)

```python
<<<<
        # 1. Contiguous block ordering: all internal faces must precede boundary faces
        if n_faces > 0:
            if not np.all(self.neighbour[:n_int] >= 0) or not np.all(self.neighbour[n_int:] == -1):
                raise ValueError("Contiguous block ordering violated: all internal faces must precede boundary faces")
====
        # 1. Contiguous block ordering: all internal faces must precede boundary faces
        if n_faces > 0:
            if not np.all(self.neighbour[:n_int] >= 0) or not np.all(self.neighbour[n_int:] == -1):
                raise ValueError("Contiguous block ordering violated: all internal faces must precede boundary faces")

        # 1b. Boundary patch coverage validation
        if self.boundaries:
            n_bnd = self.num_boundary_faces
            coverage = np.zeros(n_bnd, dtype=bool)
            total_patch_faces = 0

            for pname, (start_f, count_f) in self.boundaries.items():
                if count_f < 0:
                    raise ValueError(f"Boundary patch '{pname}' has negative count {count_f}")
                if start_f < n_int or start_f + count_f > n_faces:
                    raise ValueError(
                        f"Boundary patch '{pname}' range [{start_f}, {start_f + count_f}) "
                        f"lies outside boundary range [{n_int}, {n_faces})"
                    )
                rel_start = start_f - n_int
                rel_end = rel_start + count_f
                if np.any(coverage[rel_start:rel_end]):
                    raise ValueError(
                        f"Boundary patch '{pname}' overlaps with another patch in range [{start_f}, {start_f + count_f})"
                    )
                coverage[rel_start:rel_end] = True
                total_patch_faces += count_f

            if total_patch_faces != n_bnd:
                raise ValueError(
                    f"Boundary patches cover {total_patch_faces} faces, expected {n_bnd} faces"
                )
>>>>
```

**Target File**: `backend/meshing/native/src/sdf_engine.cpp` (`MeshSDF::evaluate`, lines 431–490)

```cpp
<<<<
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    ...
    Vec3 q, bary;
    int32_t facet = -1;
    double dist = 0.0;

    tree_.closest_point(p, -1, q, facet, bary, dist);
====
#ifdef _OPENMP
#pragma omp parallel
{
  int32_t prev_facet = -1;
  #pragma omp for schedule(dynamic, 64)
#else
  int32_t prev_facet = -1;
#endif
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    ...
    Vec3 q, bary;
    int32_t facet = -1;
    double dist = 0.0;

    tree_.closest_point(p, prev_facet, q, facet, bary, dist);
    prev_facet = facet;
    ...
  }
#ifdef _OPENMP
}
#endif
>>>>
```

---

### 4.5 Code Blueprint: Finding 5 (Optional `seed_signs` in `voromesh_bisector_loss`)
**Target File**: `backend/meshing/geometry_sdf.py` (lines 1057–1145 and line 1647)

```python
def voromesh_bisector_loss(
    points: np.ndarray,
    seeds: np.ndarray,
    k: int = 16,
    seed_signs: Optional[np.ndarray] = None,
    use_native: bool = True,
) -> Tuple[float, np.ndarray, np.ndarray]:
...
    # In native invocation:
    c_signs = (
        np.ascontiguousarray(seed_signs, dtype=np.float64).ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        if seed_signs is not None
        else None
    )
    res = dll.cfd_sdf_voromesh_loss(
        c_points,
        ctypes.c_int64(n_pts),
        c_seeds,
        ctypes.c_int64(n_seeds),
        ctypes.c_int32(k_eff),
        ctypes.byref(out_loss),
        out_dists.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
        out_grads.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
        c_signs,
    )
...
    # In pure Python fallback:
    pts_exp = pts[:, None, :]
    h = np.sum((pts_exp - midpoints) * normals, axis=-1)
    d = np.abs(h)

    if seed_signs is not None:
        s_signs = np.asarray(seed_signs, dtype=np.float64)
        qi_signs = s_signs[nearest_seed_idx, None]
        qj_signs = s_signs[pt_nbr_indices]
        valid_pairs = (qi_signs * qj_signs) < 0.0
        d_eff = np.where(valid_pairs, d, np.inf)
        has_valid = np.any(valid_pairs, axis=1)
        best_k = np.zeros(n_pts, dtype=np.int64)
        if np.any(has_valid):
            best_k[has_valid] = np.argmin(d_eff[has_valid], axis=1)
        if not np.all(has_valid):
            best_k[~has_valid] = np.argmin(d[~has_valid], axis=1)
    else:
        best_k = np.argmin(d, axis=1)
```

and in `CADMeshSDF`:
```python
    def compute_voromesh_bisector_loss(
        self,
        seeds: np.ndarray,
        k: int = 16,
        seed_signs: Optional[np.ndarray] = None,
    ) -> Tuple[float, np.ndarray, np.ndarray]:
        """Compute differentiable VoroMesh bisector loss from mesh vertices to seeds."""
        return voromesh_bisector_loss(
            self.vertices, seeds, k=k, seed_signs=seed_signs, use_native=self.use_native
        )
```

and CABI signature setup (`geometry_sdf.py:1188`):
```python
        dll.cfd_sdf_voromesh_loss.argtypes = [
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.c_int32,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),  # seed_signs
        ]
```

**Target File**: `backend/meshing/native/include/sdf_engine.hpp`
```cpp
  static int32_t voromesh_loss(
      const double* points, int64_t n_points,
      const double* seeds, int64_t n_seeds,
      int32_t k,
      double* out_loss, double* out_dists, double* out_grads,
      const double* seed_signs = nullptr
  );
```

**Target File**: `backend/meshing/native/src/sdf_engine.cpp` (`MeshSDF::voromesh_loss`)
```cpp
    for (int32_t j_idx : nbrs) {
      if (seed_signs != nullptr) {
        if (seed_signs[nearest_seed] * seed_signs[j_idx] >= 0.0) {
          continue;
        }
      }
      const Vec3& qj = s[j_idx];
      Vec3 mid = (qi + qj) * 0.5;
      Vec3 delta = qj - qi;
      double delta_len = delta.norm();
      if (delta_len < 1e-15) continue;
      Vec3 n = delta / delta_len;

      double h = dot(x - mid, n);
      double abs_h = std::abs(h);
      if (abs_h < min_h_abs) {
        min_h_abs = abs_h;
        best_h = h;
        best_n = n;
      }
    }

    if (seed_signs != nullptr && std::isinf(min_h_abs)) {
      // Fallback if none of the k neighbors had opposite sign
      for (int32_t j_idx : nbrs) {
        const Vec3& qj = s[j_idx];
        Vec3 mid = (qi + qj) * 0.5;
        Vec3 delta = qj - qi;
        double delta_len = delta.norm();
        if (delta_len < 1e-15) continue;
        Vec3 n = delta / delta_len;

        double h = dot(x - mid, n);
        double abs_h = std::abs(h);
        if (abs_h < min_h_abs) {
          min_h_abs = abs_h;
          best_h = h;
          best_n = n;
        }
      }
    }
```

**Target Files**: `cfd_mesher_native.h` and `cfd_mesher_native.cpp`
```cpp
CFD_EXPORT int32_t cfd_sdf_voromesh_loss(
    const double* points, int64_t n_points,
    const double* seeds, int64_t n_seeds,
    int32_t k,
    double* out_loss, double* out_dists, double* out_grads,
    const double* seed_signs
);
```

---

## 5. Verification Method

### 5.1 Native Compilation Command
Execute in Powershell (root: `G:\Datein\Projects\CFDSolver`):
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release
```
**Expected Outcome**: Clean build, exit code 0, generating `backend/meshing/cfd_mesher_native.dll`.

### 5.2 Unit & Parity Verification Commands
1. **WP1 Topology & MeshErrorMap Suite**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v
   ```
   **Expected Outcome**: 11/11 tests pass.
2. **WP2 SDF & VoroMesh Suite**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py tests/test_adversarial_wp2_gate.py -v
   ```
   **Expected Outcome**: All tests pass, including analytical gradient tests and Python/C++ dual-engine parity down to `1e-12`.
3. **Full 567-Test Regression Suite**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests
   ```
   **Expected Outcome**: 567/567 passed, 0 failures, 0 regressions.

### 5.3 Invalidation Conditions
- Any drift between Python and native `beta_upwind`, `omega_cell`, or `wsrd_alpha` $> 10^{-12}$.
- Failure to raise `ValueError` on non-manifold input when calling native `compute_difvm_topology_native`.
- Face area normal vectors pointing inward for CW-ordered polygon cells.
- Missing boundary coverage exception when boundary patch ranges overlap or sum $\ne n_{bnd}$.
- Thread contention or data races in `MeshSDF::evaluate`.
