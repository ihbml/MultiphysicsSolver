# Milestone M1 Adversarial Challenge & Audit Report

**Agent**: `challenger_m1_audit_1`  
**Role**: Empirical Challenger (critic, specialist)  
**Target Milestone**: M1 (Native C++ Hexcore Math, Snapping & Geometry Core)  
**Verdict**: **REQUEST_CHANGES**  
**Overall Risk Assessment**: **HIGH / CRITICAL**

---

## Executive Summary & Verdict

Milestone M1 made excellent architectural progress on R1 (corner tet Jacobian), R2 (centroid translation invariance on integer offsets), R3.1-2 (small-cell agglomeration, cell_types == 42, face deduplication, and $\omega_{\text{cell}}$), and R5.2 (DistMesh non-convex polygon projection).

However, rigorous empirical stress testing uncovered a **blocking physical invariant defect**:
- In `backend/meshing/native/src/octree_hexcore.cpp:907-911`, normal orientation checks against the background octree cell center (`nodes[active_leaves[mf.owner]].center`) result in **43.06% of boundary wall faces having their normals inverted** (pointing inward into the domain).
- This normal inversion directly causes **2,696 cells to severely violate Gauss area conservation / Gauss closure** ($\max(\text{rel\_closure}) = 12.046 \gg 10^{-11}$).
- Unbalanced edge topology analysis confirms that adjacent faces within these cells have conflicting edge directions (`forward=2, reverse=0`).
- This directly violates the mandatory project invariant in `PROJECT.md:114` ("Physical Invariants: Strict $V_i > 0$, relative Gauss closure $< 10^{-11}$") and will cause OpenFOAM `checkMesh` in WSL (M4) to fail with fatal non-closed cell and inward-pointing pyramid errors.

Therefore, approval cannot be granted in this state. Milestone M1 requires changes before gate approval.

---

## 1. Observation

### Exact File Paths and Code Locations
1. `backend/meshing/native/src/octree_hexcore.cpp:907-911` (and line 718):
   ```cpp
   Vec3 c_owner = nodes[active_leaves[mf.owner]].center;
   if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) {
     mf.normal_owner = -mf.normal_owner;
     std::reverse(mf.nodes.begin(), mf.nodes.end());
   }
   ```
2. `backend/meshing/native/src/octree_hexcore.cpp:808-834`:
   Corner tetrahedron construction for corner index $c\_idx \in 0..7$ and adjacent edge neighbors with parity factor $s = (1 - 2cx)(1 - 2cy)(1 - 2cz)$.
3. `backend/meshing/native/src/octree_hexcore.cpp:1034-1151`:
   Internal face grouping by `{new_o, new_nb}` with $new\_o < new\_nb$, collapsing duplicates into a single perimeter edge cycle.
4. `backend/meshing/native/src/distmesh.cpp:735-756`:
   Orthogonal segment projection onto the nearest polygon edge for points pushed outside during spring relaxation.

### Quantitative Empirical Test Results
Independent test suite executed via:
`$env:OMP_NUM_THREADS = "8"; .venv\Scripts\pytest.exe tests/test_challenger_m1_audit.py -v`

1. **R1 Corner Tetrahedron Jacobian (all 8 corners)**:
   - Evaluated all 8 octant corner indices $c\_idx \in 0..7$ across aspect ratios $hx/hy \in [0.05, 40.0]$.
   - `det_j` matches analytical element volume $hx \cdot hy \cdot hz > 0$ to within $10^{-12}$.
   - Small inward perturbations retain $\det(J) > 0$ and finite AMIPS energy.
   - Result: **PASSED** (1/1).

2. **R1 Sphere Wall Snapping**:
   - Geodesic icosphere (subdivision 4, $h_0 = 0.5$):
     - Median $|\phi| = 0.000760 < 10^{-3}$.
     - Minimum cell volume: $1.81 \times 10^{-6} > 0.0$ (0 negative volume cells).
     - Result: **PASSED** (1/1).
   - Geodesic icosphere (subdivision 3, $h_0 = 0.4$):
     - Median $|\phi| = 0.00302 > 10^{-3}$ due to flat discrete triangle sagitta $\Delta r \approx 0.003$.

3. **R2 Centroid Translation Invariance**:
   - Evaluated unit cubes $[x_0, x_0 + 1]^3$:
     - Shift $[0, 1]^3$: $\Delta c = 0.0 < 10^{-12}$, $\Delta V = 0.0 < 10^{-12}$ (PASSED).
     - Shift $[10, 11]^3$: $\Delta c = 0.0 < 10^{-12}$, $\Delta V = 0.0 < 10^{-12}$ (PASSED).
     - Shift $[-50, -49]^3$: $\Delta c = 0.0 < 10^{-12}$, $\Delta V = 0.0 < 10^{-12}$ (PASSED).
     - Shift $[1000, 1001]^3$: $\Delta c = 0.0 < 10^{-12}$, $\Delta V = 0.0 < 10^{-12}$ (PASSED).
     - Shift $[-123.456, 789.012, 345.678]^3$:
       - Absolute centroid error: $5.97 \times 10^{-11} \ge 10^{-12}$.
       - Relative centroid error: $6.66 \times 10^{-14} \ll 10^{-12}$.
   - Result: **PASSED on integer/dyadic shifts; roundoff on non-dyadic float shifts**.

4. **R3.1-2 Small-Cell Agglomeration & Face Deduplication**:
   - Agglomerated cells: `poly_count = np.sum(mesh.cell_types == 42) > 0`.
   - Internal face ordering: 100% of internal faces have $owner < neighbour$ (0 violations).
   - Duplicate face pairs: 0 duplicate $(owner, neighbour)$ pairs in `mesh.faces`.
   - Upwind factor: $\omega_{\text{cell}} \in [0.1, 1.0]$ across all cells.
   - Result: **PASSED** (1/1).

5. **R5.2 DistMesh Non-Convex Polygon Push-Out**:
   - L-shaped domain with re-entrant corner: 0 points leaked into $[1, 2] \times [1, 2]$ cutout; all $V_i > 0$.
   - U-shaped domain with central notch: 0 points leaked into central slot; all $V_i > 0$.
   - Result: **PASSED** (2/2).

6. **Physical Invariant Audit: Relative Gauss Closure & Wall Normal Orientation**:
   - On the snapped sphere mesh ($N = 34,792$ cells):
     - Total boundary wall faces: $18,672$.
     - **Inward-pointing wall faces**: **8,040 (43.06%)**!
     - Cells violating Gauss closure ($\text{rel\_closure} > 10^{-10}$): **2,696 cells**.
     - $\max(\text{rel\_closure}) = 12.046 \gg 10^{-11}$.
     - Topological edge cycle verification on bad cells (e.g., cell 3081): 6 edges with `forward=2, reverse=0` due to opposing normal orientation of adjacent faces.
   - Result: **FAILED (CRITICAL DEFECT)**.

---

## 2. Logic Chain

1. **Premise**: In a topologically valid polyhedral mesh, every bounded cell must form an orientable closed 2-manifold with all bounding face normal vectors pointing consistently outwards from the cell. By the divergence theorem, the sum of area-weighted face normals must sum to zero:
   $$\oint_{\partial C} d\mathbf{S} = \sum_{f \in C} \pm \mathbf{S}_f = \mathbf{0}$$
   Normalized by cell volume, `PROJECT.md:114` requires $\max_c \|\sum_{f \in C} \mathbf{S}_f\| / V_c^{2/3} < 10^{-11}$.

2. **Mechanism of Failure**:
   - In `octree_hexcore.cpp:717-722` and `octree_hexcore.cpp:907-911`, face normal orientation is governed by:
     ```cpp
     Vec3 c_owner = nodes[active_leaves[mf.owner]].center;
     if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) {
       mf.normal_owner = -mf.normal_owner;
       std::reverse(mf.nodes.begin(), mf.nodes.end());
     }
     ```
   - For an un-cut Cartesian cube, $\mathbf{c}_{\text{face}} - \mathbf{c}_{\text{owner}}$ points along the coordinate axis towards the face, so $\mathbf{n}_{\text{face}} \cdot (\mathbf{c}_{\text{face}} - \mathbf{c}_{\text{owner}}) > 0$.
   - However, when a cell is cut by a curved CAD surface (e.g. sphere) and its boundary vertices snap towards the level set, $\mathbf{c}_{\text{owner}}$ is the center of the *original un-snapped background octree box*.
   - When the cut-cell's geometry is tilted or its background box center lies outside the truncated cell, the vector $\mathbf{c}_{\text{face}} - \mathbf{c}_{\text{owner}}$ forms an obtuse angle with the true outward normal.
   - The conditional `dot(...) < 0.0` erroneously triggers, **inverting the normal and reversing the vertex winding**.
   - As directly verified, this inverts 43.06% of boundary wall faces.

3. **Consequences**:
   - The inverted wall faces point inward towards the center of the sphere instead of outward.
   - Cells adjacent to these faces have their face area vectors added with opposite sign, yielding non-zero Gauss closure residuals up to $12.046$.
   - Any OpenFOAM `checkMesh` run will flag these cells as non-closed and fail validation.

---

## 3. Caveats

1. For flat planar test models (e.g. `TestCAD.stp`, unit cubes), no cut-cell snapping occurs; $\mathbf{c}_{\text{owner}}$ is symmetric, so Gauss closure holds perfectly ($< 10^{-14}$). The defect only manifests on curved geometries where cut-cell snapping is active.
2. For non-dyadic float shifts ($[-123.456, 789.012, 345.678]$), the centroid error of $5.97 \times 10^{-11}$ is entirely due to IEEE 754 64-bit floating-point cancellation on coordinate values $\sim 10^3$, as confirmed by the relative error of $6.66 \times 10^{-14}$. This is expected numerical behavior in 64-bit precision.

---

## 4. Conclusion & Required Remediations

**Verdict**: **REQUEST_CHANGES**

To achieve gate approval, the following remediations must be applied to `backend/meshing/native/src/octree_hexcore.cpp`:

1. **Wall Face Normal Alignment Fix**:
   - Replace the check `dot(mf.normal_owner, mf.centre - c_owner) < 0.0` in lines 718 and 908 with a robust outward normal check.
   - For boundary wall faces (`mf.neighbour == -1`):
     - Compute the surface gradient $\nabla \phi$ from `sdf.evaluate(...)` at `mf.centre`. On a solid boundary, $\nabla \phi$ points consistently outward from the geometry.
     - Alternatively, use the cell reference centroid `cell_approx_center[mf.owner]` (which is strictly inside the cut cell) rather than `nodes[active_leaves[mf.owner]].center`.
   - Ensure that 100% of wall faces satisfy $\mathbf{S}_f \cdot (\mathbf{c}_f - \mathbf{c}_{\text{geom}}) > 0$ on convex bodies.
2. **Gauss Closure Enforcement**:
   - Verify that after snapping and agglomeration, relative Gauss closure satisfies:
     $$\frac{\|\sum_{f \in c} \pm \mathbf{S}_f\|}{V_c^{2/3}} < 10^{-11} \quad \forall c$$
   - Add this invariant check to `tests/test_wp4_multiphysics_hexcore.py`.

---

## 5. Verification Method

To independently verify all findings and reproduce the defect:

1. **Environment Setup**:
   ```powershell
   $env:OMP_NUM_THREADS = "8"
   ```
2. **Execute Full Adversarial Challenge Suite**:
   ```powershell
   .venv\Scripts\pytest.exe tests/test_challenger_m1_audit.py -v
   ```
   *Expected Result*: 11 passed in ~88s, explicitly confirming the 43.06% wall face normal inversion and Gauss closure violation in `test_audit_defect_wall_face_normal_inversion_and_gauss_closure`.

3. **Inspect Inward Wall Face Percentage**:
   ```powershell
   .venv\Scripts\python.exe -c "
   import numpy as np
   from tests.test_asme_vv import create_geodesic_icosphere
   from backend.meshing.difvm_topology import generate_hexcore_native

   surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=4)
   mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode='solid_only')

   start_f, num_f = mesh.boundaries['wall']
   inward = sum(1 for f in range(start_f, start_f + num_f) if np.dot(mesh.face_areas[f], mesh.face_centres[f]) < 0)
   print(f'Total: {num_f}, Inward: {inward} ({inward/num_f:.2%})')
   "
   ```
   *Expected Output*: `Total: 18672, Inward: 8040 (43.06%)`.
