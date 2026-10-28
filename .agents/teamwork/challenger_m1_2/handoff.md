# Milestone M1 Adversarial Challenge Report: CADMeshSDF & CADFeatureFilter

**Agent:** `challenger_m1_2` (Adversarial Critic / Empirical Challenger)  
**Parent / Caller:** `4ce92fc5-81ba-402c-a361-d39a59aea46d`  
**Date:** 2026-10-28  
**Scope:** Independent adversarial stress-testing of `CADMeshSDF` and `CADFeatureFilter` on real and synthetic dirty CAD geometry, singular boundary points, extreme numerical scaling, and degenerate topology.

---

## 1. Observation

1. **TestCADSTPAP214.stp Orphan Node Purging (CONFIRMED PASS)**:
   - File: `resources/CADTestSamples/TestCADSTPAP214.stp`
   - When parsed and combined with auxiliary sketch nodes (from entity `#35 = GEOMETRIC_CURVE_SET`), the raw mesh contains 37 vertices and 12 faces (29 orphan sketch nodes).
   - `CADFeatureFilter.filter_orphan_vertices(dirty_mesh)` purges all 29 orphan nodes, reducing vertices from 37 to 8.
   - `SurfaceAuditor.audit()` confirms:
     - `is_watertight: True`
     - `open_edges_count: 0`
     - `multi_edges_count: 0`
     - `euler_characteristic: 2` ($\chi = V - E + F = 8 - 18 + 12 = 2$).
   - Verified programmatically in `tests/test_adversarial_m1.py::test_step_ap214_orphan_sketch_nodes_purged_chi2`.

2. **Degenerate & Collinear Facet Filtering (CONFIRMED PASS)**:
   - In `backend/meshing/geometry_sdf.py` lines 763–790 (`CADFeatureFilter.filter_degenerate_faces`):
   - Collapsed index triangles `[0, 0, 1]`, `[2, 3, 3]`, `[4, 4, 4]` and collinear triangle `[0, 8, 1]` with area $0.0 < 10^{-14}$ were added to a unit cube.
   - `CADFeatureFilter.filter_degenerate_faces(dirty_mesh, min_area=1e-14)` cleanly removed all 4 degenerate triangles and unreferenced vertex 8, restoring 12 faces, 8 vertices, $\chi = 2$, and watertightness.
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_feature_filter_degenerate_and_collinear_faces`.

3. **Singular Boundary Queries (CONFIRMED PASS FOR CONVEX/MACRO BODIES)**:
   - Evaluated `CADMeshSDF` on all 8 vertices, 12 edge midpoints, and 12 face centers of a unit cube:
   - Vertex distances: $|\phi| = 0.000000 \times 10^0 < 10^{-12}$, unit normal gradient $\|\nabla \phi\| = 1.000000 \pm 10^{-10}$.
   - Edge midpoint distances: $|\phi| = 0.000000 \times 10^0 < 10^{-12}$, unit normal gradient $\|\nabla \phi\| = 1.000000 \pm 10^{-10}$.
   - Face centers: $|\phi| < 10^{-12}$, outward normal matches face normal $[0, 0, 1]$.
   - Coplanar exterior query at $(1.5, 0.5, 1.0)$ in $Z=1$ plane: $\phi = +0.500000$, $\nabla \phi = [1, 0, 0]$ (pointing away from edge into fluid).
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_mesh_sdf_singular_queries_on_cube`.

4. **[CRITICAL BUG] CADMeshSDF Scale Sensitivity on Micro-Geometries & Boundary Layers**:
   - File: `backend/meshing/geometry_sdf.py` line 615:
     ```python
     det = a_coeff * c_coeff - b_coeff * b_coeff
     det_safe = np.where(np.abs(det) < 1e-15, 1e-15, det)
     ```
   - For an equilateral triangle with edge length $L = 10^{-4}$ m (0.1 mm, standard CFD boundary layer resolution):
     - $a\_coeff = L^2 = 10^{-8}$, $c\_coeff = 10^{-8}$, $det = 0.75 \times 10^{-16} < 10^{-15}$.
     - `det_safe` clamps $det$ to $10^{-15}$ (13.3x too large).
   - Querying a point at distance $0.1 L = 10^{-5}$ m directly above the triangle centroid yields:
     - Expected distance: $1.000000 \times 10^{-5}$ m.
     - Computed `phi`: $5.433308 \times 10^{-5}$ m (**relative error of 443%**).
     - Expected normal gradient: $[0.0, 0.0, 1.0]$.
     - Computed normal gradient: $[0.851231, 0.491458, 0.184050]$ (**tilted 79.4° away from physical normal**).
   - For $L = 10^{-5}$ m (10 $\mu$m): relative distance error is **486%**, gradient is $[0.85332, 0.49266, 0.17067]$.
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_mesh_sdf_scale_sensitivity_micro_elements`.

5. **[CRASH] CADMeshSDF Uncaught IndexError at Extreme Large Coordinates**:
   - File: `backend/meshing/geometry_sdf.py` line 592 & 600:
     ```python
     _, cand_idx = self.tree.query(p, k=self.k_candidates)
     ...
     a_pts = self.tri_v0[idx_flat]
     ```
   - Querying coordinates $p \ge 10^{160}$ causes distance squared $(p - c)^2 > 1.79 \times 10^{308}$, overflowing float64 to `inf`.
   - `scipy.spatial.cKDTree.query()` returns index `len(self.centroids)` (out-of-range flag for no neighbor found).
   - Execution crashes with:
     `IndexError: index 1 is out of bounds for axis 0 with size 1`
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_mesh_sdf_extreme_large_coords_overflow`.

6. **[ALGORITHMIC DEFECT] Centroid KDTree Failure on Disparate Triangle Sizes**:
   - File: `backend/meshing/geometry_sdf.py` lines 556–557, 592:
     ```python
     self.centroids = (self.tri_v0 + self.tri_v1 + self.tri_v2) / 3.0
     self.tree = cKDTree(self.centroids)
     _, cand_idx = self.tree.query(p, k=self.k_candidates)
     ```
   - On a mesh containing one large triangle $T_0$ (vertices $(0,0,0), (100,0,0), (0,100,0)$) and 12 small triangles near $(0.5, 0.5, 2.0)$:
   - For query point $p = (0.1, 0.1, 0.01)$ (distance $0.01$ m outside $T_0$):
     - The centroid of $T_0$ is at $(33.3, 33.3, 0)$, distance $\approx 47$ m away.
     - The 8 nearest centroids queried by `cKDTree` are all small triangles at distance $\approx 2.0$ m.
     - $T_0$ is completely omitted from the $k=8$ candidate set.
     - Expected signed distance: $+0.010000$ m (exterior).
     - Computed signed distance with $k=8$: **$-2.068840$ m** (**magnitude 200x error and WRONG SIGN: classified as solid interior**).
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_mesh_sdf_centroid_kdtree_disparate_sizes_vulnerability`.

7. **[TOPOLOGICAL LEAK] CADFeatureFilter Retains Connected Non-Manifold Fins**:
   - File: `backend/meshing/geometry_sdf.py` lines 829–833:
     ```python
     for f_list in edge_to_faces.values():
         if len(f_list) > 1:
             first = f_list[0]
             for other in f_list[1:]:
                 union(first, other)
     ```
   - Attaching a 1-face planar fin to a cube along edge $(0, 1)$ results in 3 faces sharing that edge.
   - `CADFeatureFilter.sanitize()` unions all 3 faces together. The resulting mesh retains the fin:
     - `len(faces) = 13`
     - `is_manifold: False`
     - `multi_edges_count: 1`
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_feature_filter_non_manifold_edges_vulnerability`.

8. **[DATA DESTRUCTION] CADFeatureFilter Discards Valid Surfaces on Split Seams**:
   - When duplicate vertices exist at identical coordinates (split seam on bottom face of cube):
   - `CADFeatureFilter.sanitize()` does not weld coincident vertices.
   - `extract_dominant_manifold_shell()` treats the bottom face as a separate component (area 1.0) and discards it in favor of the rest of the cube (area 5.0).
   - Result: closed cube is opened into a hollow box with 10 faces, `open_edges_count = 4`, and $\chi = 1$.
   - Verified programmatically in `tests/test_adversarial_m1.py::test_cad_feature_filter_duplicate_vertices_vulnerability`.

---

## 2. Logic Chain

1. **Derivation of Scale Sensitivity Bug**:
   - Barycentric coordinates solve $\begin{bmatrix} a & b \\ b & c \end{bmatrix} \begin{bmatrix} s \\ t \end{bmatrix} = \begin{bmatrix} d \\ e \end{bmatrix}$, with determinant $\text{det} = ac - b^2$.
   - Because $a, b, c \sim L^2$, $\text{det} \sim L^4$.
   - The implementation in `geometry_sdf.py:615` clamps $\text{det}$ to `1e-15` without normalizing by the triangle scale $ac$.
   - When $L \le 1.7 \times 10^{-4}$ m (0.17 mm), $L^4 < 10^{-15}$. The denominator is artificially inflated by orders of magnitude, collapsing $s$ and $t$ toward zero.
   - The projection algorithm misidentifies interior points as edge projections, calculating distances to incorrect features and producing severe vector distortion (79.4° tilt).
   - Because CFD meshes routinely operate at $y^+ \sim 1$ scales ($10^{-5}$ to $10^{-4}$ m in SI units), this bug directly invalidates boundary layer inflation and near-wall particle detection in downstream Milestones M2 and M3.

2. **Derivation of Centroid KDTree Heuristic Defect**:
   - A KD-tree over triangle centroids only finds triangles whose centroids are spatially close.
   - For an anisotropic or multi-scale CAD surface (e.g. turbine blade leading edge vs main foil chord), the closest point on a large facet can be arbitrarily far from its centroid.
   - If $k=8$ is fixed, the nearest candidates may all belong to fine nearby clusters, completely missing the large facet that is actually closest to the query point.
   - This causes false negative distances (labeling exterior fluid as solid obstacle) and corrupts DistMesh node insertion.

3. **Derivation of Non-Manifold Fin Retention**:
   - `CADFeatureFilter.extract_dominant_manifold_shell()` defines surface connectivity across ANY edge shared by $>1$ face.
   - A 2-manifold surface strictly requires every internal edge to be shared by *exactly 2* faces (`len(f_list) == 2`).
   - By unioning across non-manifold edges (`len(f_list) > 2`), non-manifold fins remain topologically connected to the dominant shell and survive filtering.

---

## 3. Caveats

- **Clean CAD vs Dirty CAD**: For well-formed, uniform 2-manifold meshes in the meter scale ($0.01$ m to $10$ m), `CADMeshSDF` and `CADFeatureFilter` operate with high accuracy (passing all 27 tests in `test_geometry_sdf.py`).
- **Performance**: The pure NumPy/KDTree implementation is fast (10,000 queries in < 0.20s), but the KDTree heuristic is the root cause of the disparate-size candidate omission.
- **Scope**: Did not evaluate non-linear NURBS trim surfaces directly without tessellation.

---

## 4. Conclusion

### Explicit Verdict: **REQUEST_CHANGES**

Milestone M1 cannot be certified in its current state due to high-severity numerical and topological bugs that will directly break downstream Milestones M2 (Boundary Layers & DistMesh) and M3 (SPH Near-Wall Coupling).

### Required Changes for Implementer (Worker):

1. **Fix CADMeshSDF Scale Sensitivity (P0 - Critical)**:
   In `backend/meshing/geometry_sdf.py`:
   - Replace absolute clamping `det_safe = np.where(np.abs(det) < 1e-15, 1e-15, det)` with scale-relative clamping:
     ```python
     scale = np.maximum(a_coeff * c_coeff, 1e-30)
     det_safe = np.where(np.abs(det) < 1e-12 * scale, np.sign(det) * 1e-12 * scale + 1e-30, det)
     ```
   - In `CADMeshSDF.__init__`, replace `lengths[lengths < 1e-15] = 1.0` with `lengths[lengths < 1e-30] = 1.0` (or normalize by bounding box scale).
   - Ensure elements with $L \le 0.1$ mm evaluate distances with relative error $< 10^{-4}$ and unit normal gradient.

2. **Fix CADMeshSDF Extreme Coordinate IndexError (P1 - High)**:
   In `backend/meshing/geometry_sdf.py:592`:
   - Clip or filter candidate indices: `cand_idx = np.clip(cand_idx, 0, len(self.tri_v0) - 1)` to prevent `IndexError` when distance overflows float64.

3. **Harden CADMeshSDF Candidate Selection on Multi-Scale Meshes (P1 - High)**:
   - For multi-scale meshes, augment centroid KDTree search with maximum triangle circumradius padding, or increase adaptive candidate pool when size variation is detected.

4. **Harden CADFeatureFilter Against Non-Manifold Fins (P2 - Medium)**:
   In `backend/meshing/geometry_sdf.py:829`:
   - Only propagate adjacency across strict 2-manifold edges:
     ```python
     for e, f_list in edge_to_faces.items():
         if len(f_list) == 2:
             union(f_list[0], f_list[1])
     ```
     Faces attached solely via non-manifold multi-edges (`len > 2`) or open edges (`len == 1`) will be isolated into separate components and pruned by dominant area selection.

5. **Add Coincident Vertex Welding to CADFeatureFilter (P2 - Medium)**:
   - Before extracting manifold shells, weld vertices with identical coordinates within spatial tolerance ($10^{-7}$ m) using `scipy.spatial.cKDTree` to prevent discarding valid faces across split seams.

---

## 5. Verification Method

To independently verify all findings and execute the empirical adversarial test suite:

```powershell
& "C:\Program Files\ANSYS Inc\v252\Lumerical\python-3.13.1\python.exe" -c "
import sys
sys.path.insert(0, 'G:/Datein/Projects/CFDSolver')
sys.path.append('G:/Datein/Projects/CFDSolver/.venv/Lib/site-packages')
import pytest
sys.exit(pytest.main(['tests/test_adversarial_m1.py', '-v']))
"
```

*Expected Result*:
```text
tests/test_adversarial_m1.py::test_step_ap214_orphan_sketch_nodes_purged_chi2 PASSED
tests/test_adversarial_m1.py::test_cad_feature_filter_degenerate_and_collinear_faces PASSED
tests/test_adversarial_m1.py::test_cad_feature_filter_non_manifold_edges_vulnerability PASSED
tests/test_adversarial_m1.py::test_cad_feature_filter_duplicate_vertices_vulnerability PASSED
tests/test_adversarial_m1.py::test_cad_mesh_sdf_singular_queries_on_cube PASSED
tests/test_adversarial_m1.py::test_cad_mesh_sdf_scale_sensitivity_micro_elements PASSED
tests/test_adversarial_m1.py::test_cad_mesh_sdf_extreme_large_coords_overflow PASSED
tests/test_adversarial_m1.py::test_cad_mesh_sdf_centroid_kdtree_disparate_sizes_vulnerability PASSED

============================== 8 passed in 1.09s ==============================
```

All 8 tests run in 1.09s and programmatically prove each of the observations detailed above.
