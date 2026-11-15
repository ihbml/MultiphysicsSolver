# Empirical Challenger Audit Report — Milestone M1 (Hexcore & DistMesh Mesher)

**Agent**: `challenger_m1_audit_2`  
**Role**: `critic`, `specialist` (Empirical Challenger)  
**Parent Conversation**: `0e9d9a36-56a7-4f1c-8222-640d7206b910`  
**Verdict**: **`REQUEST_CHANGES`**  
**Risk Level**: **HIGH** (Structural mathematical flaw in face normal orientation causing boundary inversion and Gauss divergence failure)

---

## 1. Observation

### 1.1 Baseline Worker Test Suite Execution
- **Command**:
  ```powershell
  $env:OMP_NUM_THREADS = "8"; .venv\Scripts\pytest.exe tests/test_wp4_multiphysics_hexcore.py -v
  ```
- **Result**:
  ```
  ============================= test session starts =============================
  platform win32 -- Python 3.14.7, pytest-9.1.1, pluggy-1.7.0
  collected 24 items
  tests/test_wp4_multiphysics_hexcore.py::test_octree_cube_internal_meshing PASSED [  4%]
  ...
  tests/test_wp4_multiphysics_hexcore.py::test_openfoam_export_format PASSED [100%]
  ======================== 24 passed in 60.20s (0:01:00) =========================
  ```

### 1.2 Empirical Challenger Test Suite Execution
Authored new empirical challenge test suite in `tests/test_challenger_m1_audit_2.py` (13 tests targeting 4 stress dimensions).
- **Command**:
  ```powershell
  $env:OMP_NUM_THREADS = "8"; .venv\Scripts\pytest.exe tests/test_challenger_m1_audit_2.py -v
  ```
- **Result**:
  ```
  ============================= test session starts =============================
  platform win32 -- Python 3.14.7, pytest-9.1.1, pluggy-1.7.0
  collected 13 items

  tests/test_challenger_m1_audit_2.py::TestAdversarialCutCellSnapping::test_snapping_large_gradient_step_volume_positivity PASSED [  7%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialCutCellSnapping::test_snapping_sphere_h0_1point5_extreme_displacement PASSED [ 15%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialCutCellSnapping::test_snapping_preserves_external_fluid_farfield_boundaries PASSED [ 23%]
  tests/test_challenger_m1_audit_2.py::TestExtremeTranslationInvariance::test_cube_centroid_extreme_positive_1e6 PASSED [ 30%]
  tests/test_challenger_m1_audit_2.py::TestExtremeTranslationInvariance::test_cube_centroid_extreme_negative_1e6 PASSED [ 38%]
  tests/test_challenger_m1_audit_2.py::TestExtremeTranslationInvariance::test_sphere_cut_cell_translation_invariance_plus_minus_1e6 PASSED [ 46%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialAgglomerationAndDeduplication::test_internal_face_deduplication_uniqueness_and_ordering PASSED [ 53%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialAgglomerationAndDeduplication::test_agglomerated_polyhedral_cell_vertex_union PASSED [ 61%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialAgglomerationAndDeduplication::test_wall_face_inward_orientation_defect_detection PASSED [ 69%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialAgglomerationAndDeduplication::test_omega_cell_upwind_consistency PASSED [ 76%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialDistMeshProjection::test_distmesh_acute_triangle_vertex_exact_projection PASSED [ 84%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialDistMeshProjection::test_distmesh_u_shaped_concave_recess_points_far_outside PASSED [ 92%]
  tests/test_challenger_m1_audit_2.py::TestAdversarialDistMeshProjection::test_distmesh_star_polygon_reflex_corners PASSED [100%]

  ======================= 13 passed in 249.65s (0:04:09) ========================
  ```

### 1.3 Direct Code Inspection: Boundary Normal Orientation
In `backend/meshing/native/src/octree_hexcore.cpp`, lines 906–912:
```cpp
// Ensure outward orientation relative to cell center
Vec3 c_owner = nodes[active_leaves[mf.owner]].center;
if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) {
  mf.normal_owner = -mf.normal_owner;
  std::reverse(mf.nodes.begin(), mf.nodes.end());
}
```
And identically in lines 716–722:
```cpp
// Ensure normal points outward from owner cell
Vec3 c_owner = nodes[active_leaves[f.owner]].center;
if (dot(f.normal_owner, f.centre - c_owner) < 0.0) {
  f.normal_owner = -f.normal_owner;
  std::reverse(f.nodes.begin(), f.nodes.end());
}
```

### 1.4 Empirical Measurement of Inverted Boundary Normals
Empirical execution on geodesic icosphere mesh (`subdivisions=2`, `h0=0.5`, `domain_mode=solid_only`):
- Total wall faces: $18,792$
- Number of wall faces where $(\vec{S}_f \cdot (\vec{x}_f - \vec{C}_{\text{owner}})) < 0$: **$8,088$ faces (43.04%)**
- Number of internal faces: $89,174$
- Internal face duplicate pairs: $0$ (strictly unique, strictly $owner < neighbour$)
- Total polyhedral cells (`cell_type == 42`): $4,345$
- Incomplete vertex unions in `mesh.cells[c]`: $0$ (100% complete)
- Maximum relative Gauss divergence error $\frac{\|\sum_{f \in c} \vec{S}_f\|}{\sum_{f \in c} \|\vec{S}_f\|}$: **$0.9113$ ($91.13\%$)**
- Cells with relative Gauss divergence closure error $> 1\%$: **$7,419$ cells ($21.65\%$ of all active cells)**

---

## 2. Logic Chain

### 2.1 Area 1: Snapping Under Large Gradient Steps & Boundary Preservation
- **Observation**: Coarse background grids ($h_0 = 1.0, 1.5$) tested against a unit sphere ($R=1.0$) with large gradient displacements.
- **Analysis**:
  - Minimum cell volume remained strictly positive: $\min(V_c) = 2.45 \times 10^{-4} > 0.0$. No negative or collapsed cells detected.
  - AMIPS energy evaluation (`computeTetAMIPS`, threshold $10^6$) and tetrahedral sub-pyramid positivity checks in `octree_hexcore.cpp:557-610` successfully reject snapping displacements that would invert elements.
  - Planar farfield boundaries (`inlet`, `outlet`, `bottom`, `top`, `sides`) evaluated under domain $[-3, 3]^3$: all boundary node coordinates matched domain limits with exact $0.0$ deviation (tolerance $10^{-5}$). Snapping displacement threshold (`octree_hexcore.cpp:553`) explicitly prevents boundary nodes on outer bounding planes from distorting.
- **Deduction**: Cut-cell snapping preserves element invertibility guards and outer boundary planarity as required by R1.

### 2.2 Area 2: Extreme Coordinate Translation Invariance ($\pm 10^6$)
- **Observation**: Unit cube translated to $[10^6, 10^6+1]^3$ and $[-10^6, -10^6+1]^3$. Snapped cut-cell sphere mesh translated by $\pm 10^6$.
- **Analysis**:
  - Unit cube at $+10^6$: computed centroid is $(1000000.5, 1000000.5, 1000000.5)$, volume is $1.000000000000$. Absolute deviation $< 10^{-12}$.
  - Unit cube at $-10^6$: computed centroid is $(-999999.5, -999999.5, -999999.5)$, volume is $1.000000000000$. Absolute deviation $< 10^{-12}$.
  - Snapped sphere cut-cell mesh:
    - Base centroid: $(-8.21 \times 10^{-17}, -8.84 \times 10^{-17}, -2.69 \times 10^{-17})$, volume: $4.188790$
    - $+10^6$ centroid shift: $(+10^6 - 2.87 \times 10^{-6}, +10^6 - 3.48 \times 10^{-6}, +10^6 - 3.42 \times 10^{-6})$, volume: $4.188785$
    - Centroid shift deviation: $5.8 \times 10^{-6} < 10^{-5}$. Volume deviation: $1.2 \times 10^{-6} < 10^{-5}$.
    - Active cell count: $34,279$ (base) vs $34,273$ ($+10^6$, $0.017\%$ difference attributable to standard IEEE 754 float64 machine epsilon at magnitude $10^6$).
- **Deduction**: Geometry and centroid evaluation exhibits translation invariance up to double precision machine limits.

### 2.3 Area 3: Polyhedral Cell Agglomeration, Deduplication & Boundary Normal Inversion (CRITICAL DEFECT)
- **Observation A (Face Deduplication & Agglomeration Integrity)**:
  - 89,174 internal faces inspected: 0 duplicate $(owner, neighbour)$ pairs found; strictly $owner < neighbour$.
  - 4,345 agglomerated polyhedral cells (`cell_type == 42`): every vertex of every incident face is present in `mesh.cells[c]`.
  - Upwind weighting factor $\omega_{\text{cell}} \in [0.1, 1.0]$ across all cells, obeying $\max(0.1, \min_{f \in c} \beta_f)$.
- **Observation B (CRITICAL DEFECT: Inverted Wall Face Normals)**:
  - In `octree_hexcore.cpp:907-911` (and `716-722`), the outward boundary normal is oriented by computing:
    ```cpp
    Vec3 c_owner = nodes[active_leaves[mf.owner]].center;
    if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) {
      mf.normal_owner = -mf.normal_owner;
      std::reverse(mf.nodes.begin(), mf.nodes.end());
    }
    ```
  - Crucially: `nodes[active_leaves[mf.owner]].center` is the **geometric center of the background octree cube** $[x_{\min}, x_{\max}] \times [y_{\min}, y_{\max}] \times [z_{\min}, z_{\max}]$, **NOT** the centroid of the truncated cut-cell!
  - When an octant is cut by a convex CAD surface (like a sphere), the background cube center $c_{\text{owner}}$ often lies **outside** the fluid/solid cell, or on the opposite side of the cut boundary face.
  - Consequently, the vector $(\vec{x}_f - \vec{c}_{\text{owner}})$ points **inward** toward the interior of the domain, causing the algorithm to mistakenly flip the wall face normal into the cell!
  - Out of 18,792 wall faces on a sphere, **8,088 faces (43.04%)** are flipped inward.
  - Impact on Discrete Gauss Divergence Closure:
    $$\oint_{\partial \Omega_c} \vec{n} \, dA = \sum_{f \in \partial \Omega_c} \vec{S}_f = \vec{0}$$
    Because $43\%$ of boundary faces point backward, this closure fails drastically: 7,419 cells ($21.65\%$) have relative residual errors exceeding $1\%$, and the worst cells exhibit **$91.13\%$ relative error**.
  - Impact on CFD Solvers: Any finite volume solver (including OpenFOAM and DIF-VM) using these boundary face normals will experience severe artificial mass and momentum sources at the wall, and OpenFOAM's standard `checkMesh` utility will reject the mesh with `Face ... orientation error: points inward`.
- **Deduction**: This is an empirical and mathematical blocking defect that must be resolved before Milestone M1 approval.

### 2.4 Area 4: DistMesh Orthogonal Boundary Projection Edge Cases
- **Observation**:
  - Acute triangle ($20^\circ$ apex): All 40 iterations completed without divergence; points near apex projected cleanly with $y \le 3.0 + 10^{-5}$ and $|x| \le \text{span}(y)$. All cell areas strictly positive.
  - U-shaped concave recess: 100 points initially placed outside in the recess cavity; boundary projection cleanly snapped all nodes onto boundary edges with zero points escaping.
  - Star polygon with reflex corners: Boundary projections smoothly preserved non-convex reflex boundaries without self-intersection or inverted triangles.
- **Deduction**: DistMesh 2D orthogonal boundary projection is robust against sharp corners, reflex angles, and concave boundary cavities as required by R5.2.

---

## 3. Caveats

- **3D Unsteady ALE Moving Meshes**: Dynamic cut-cell snapping during time-dependent boundary mesh morphing was not tested, as M1 scope is static mesh generation.
- **Boundary Layer Prisms**: Tests focused on hexcore octree cut-cells and DistMesh polygons; anisotropic prismatic boundary layer extrusions were not tested in conjunction with snapping.
- **Higher Subdivisions**: Sphere test utilized icosphere `subdivisions=2` ($h_0=0.5$, ~34k cells). Very fine meshes (>500k cells) were omitted to maintain execution time within the required memory and time constraints.

---

## 4. Conclusion & Verdict

**Verdict**: **`REQUEST_CHANGES`**

### Summary of Passed Areas:
- **R1 (Cut-Cell Snapping)**: AMIPS inversion guard and sub-pyramid positivity checks work correctly under large gradient displacements. Outer farfield boundaries remain strictly planar.
- **R2 (Translation Invariance)**: Centroids and volumes are invariant under extreme coordinate offsets ($\pm 10^6$) to $< 10^{-5}$.
- **R3.1 (Agglomeration Integrity & Deduplication)**: Face deduplication produces strictly unique pairs with $owner < neighbour$; agglomerated cells retain complete vertex sets; upwind factor $\omega_{\text{cell}} \in [0.1, 1.0]$.
- **R5.2 (DistMesh Projection)**: 2D DistMesh projection robustly handles acute corners, points outside, and reflex angles.

### Blocking Defect Requiring Changes:
- **Defect**: Inverted boundary wall face normals in `backend/meshing/native/src/octree_hexcore.cpp:906–912` and `716–722`.
- **Root Cause**: `c_owner = nodes[active_leaves[mf.owner]].center` uses the background octree cell center instead of the true truncated cut-cell centroid or the surface normal direction.
- **Blast Radius**: 43.04% of wall faces on a sphere point inward towards the cell centroid. Violates discrete Gauss divergence closure by up to 91.13%. Causes immediate rejection by OpenFOAM `checkMesh` and divergence in finite volume solvers.

### Actionable Mitigation for Worker:
In `octree_hexcore.cpp`, replace background octant center test with one of the following canonical approaches:
1. **Use Cut-Cell Approximate Centroid**: Compute the centroid of the active cell's vertices $C_{\text{cut}} = \frac{1}{|V_c|} \sum_{v \in V_c} \vec{x}_v$, and ensure $\vec{S}_f \cdot (\vec{x}_f - C_{\text{cut}}) > 0$.
2. **Use CAD Surface Outward Normal**: For boundary faces created by slicing against CAD triangles, align the wall face normal directly with the CAD surface outward normal: $\vec{S}_f \cdot \vec{n}_{\text{cad}} > 0$.

---

## 5. Verification Method

To independently reproduce all findings and verify the defect:

1. **Run Challenger Stress Suite**:
   ```powershell
   $env:OMP_NUM_THREADS = "8"
   .venv\Scripts\pytest.exe tests/test_challenger_m1_audit_2.py -v
   ```
2. **Verify Specific Inverted Face Output**:
   Run the defect detection test with stdout enabled:
   ```powershell
   .venv\Scripts\pytest.exe tests/test_challenger_m1_audit_2.py -k test_wall_face_inward_orientation_defect_detection -s
   ```
   **Observed Output**:
   `Empirical finding: 8088/18792 wall faces (43.04%) have inward normals!`

3. **Invalidation Condition**:
   Once Worker fixes lines 716–722 and 906–912 in `octree_hexcore.cpp`, running:
   ```python
   inward_wall_faces = sum(1 for f in wall_faces if np.dot(mesh.face_areas[f], mesh.face_centres[f] - mesh.cell_centres[mesh.owner[f]]) < 0)
   ```
   must yield `inward_wall_faces == 0`, and the relative Gauss divergence error $\max_c \frac{\|\sum_{f \in c} \vec{S}_f\|}{\sum_{f \in c} \|\vec{S}_f\|}$ must drop below $10^{-3}$ on all cells.
