# Milestone M1 Forensic Audit Report

**Work Product**: Milestone M1 — Native C++ Hexcore Math, Snapping & Geometry Core  
**Files Audited**:
- `backend/meshing/native/src/octree_hexcore.cpp`
- `backend/meshing/native/include/octree_hexcore.hpp`
- `backend/meshing/native/src/distmesh.cpp`
- `tests/test_wp4_multiphysics_hexcore.py`  
**Profile**: General Project (Integrity mode: Development)  
**Verdict**: **CLEAN**

---

### Forensic Phase Results

| Check | Requirement | Result | Forensic Evidence |
|---|---|---|---|
| **Check 1** | Hardcoded Output Detection | **PASS** | Grep and AST inspection confirmed zero hardcoded test outputs, lookup tables, or special-cased test branch literals (e.g. `[10.5, 10.5, 10.5]`). |
| **Check 2** | Facade / Dummy Stubs | **PASS** | No stub functions or dummy returns. Real AMIPS $(27/16)(tr(E)^3)/\det(J)^2$, triangle-fan integration, pyramid decomposition, and segment projection are fully implemented. |
| **Check 3** | Pre-populated Artifact Detection | **PASS** | No pre-existing test logs, fake attestation outputs, or cache files found in `backend/meshing/` or `tests/`. |
| **Check 4** | Independent Build & Test | **PASS** | Native DLL compiled cleanly via MSVC 2022 C++20 Release with `--parallel 8` (0 warnings, exit code 0). Pytest executed 24/24 unit tests in `tests/test_wp4_multiphysics_hexcore.py` (24 passed in 57.45s). |
| **Check 5** | R1: Corner Tet AMIPS & Positive Sub-Pyramids | **PASS** | In `octree_hexcore.cpp:808-879`, corner indexing $cx, cy, cz \in \{0, 1\}$ and adjacent edge neighbors $vx, vy, vz$ construct a positively oriented corner tet with parity-swapped $v_1, v_2$ when $s < 0$, guaranteeing $\det(J) = dx \cdot dy \cdot dz > 0$ and finite AMIPS energy. Sub-pyramid volume check enforces $V_{\text{pyr}} > 10^{-15} V_{\text{bg}}$ for all cells sharing vertex $v$. |
| **Check 6** | R2: Translation-Invariant Centroids & Volumes | **PASS** | In `octree_hexcore.cpp:93-186, 901-953`, `computePolygonNormalAndCentroid` and `isFaceFolded` evaluate area-weighted triangle fans around the polygon arithmetic mean. Cell volumes and centroids are integrated via local reference point $c_{\text{approx}}$ and pyramid decomposition, strictly invariant under spatial translation. |
| **Check 7** | R3.1-2: Agglomeration, Polyhedron (42), Face Deduplication & $\omega_{\text{cell}}$ | **PASS** | In `octree_hexcore.cpp:995-1021, 1034-1150, 1324-1337`, merged cells receive `CellType::POLYHEDRON = 42`, union vertex sets from all merged sub-cells, group internal dividing faces by $(owner, neighbour)$ with $owner < neighbour$, collapse duplicates into perimeter boundary cycles, and compute $\omega_{\text{cell}}[c] = \max(0.1, \min_f \beta_f) \in [0.1, 1.0]$. |
| **Check 8** | R5.2: DistMesh Orthogonal Push-Out Projection | **PASS** | In `distmesh.cpp:735-756`, when an interior node moves outside the polygon, it is orthogonally projected onto the nearest boundary segment $p_k \to p_j$ using clamped projection parameter $t = \text{clamp}(ap \cdot ab / \|ab\|^2, 0.0, 1.0)$. |

---

## 1. Observation

### Source Code Observations
1. **R1 Corner Tet AMIPS (`octree_hexcore.cpp:808-844`)**:
   ```cpp
   const OctreeNode& onode = nodes[active_leaves[cell_idx]];
   int cx = (pos.x >= onode.center.x) ? 1 : 0;
   int cy = (pos.y >= onode.center.y) ? 1 : 0;
   int cz = (pos.z >= onode.center.z) ? 1 : 0;

   Vec3 v_corner(cx == 0 ? onode.bmin.x : onode.bmax.x,
                 cy == 0 ? onode.bmin.y : onode.bmax.y,
                 cz == 0 ? onode.bmin.z : onode.bmax.z);

   Vec3 vx = v_corner + Vec3((1 - 2 * cx) * onode.size.x, 0, 0);
   Vec3 vy = v_corner + Vec3(0, (1 - 2 * cy) * onode.size.y, 0);
   Vec3 vz = v_corner + Vec3(0, 0, (1 - 2 * cz) * onode.size.z);

   int s = (1 - 2 * cx) * (1 - 2 * cy) * (1 - 2 * cz);
   Vec3 v1 = (s > 0) ? vx : vy;
   Vec3 v2 = (s > 0) ? vy : vx;
   Vec3 v3 = vz;

   double tet_energy = computeTetAMIPS(test_pos, v1, v2, v3);
   ```
   Directly eliminates collinear tetrahedron degeneracy ($\det(J) \equiv 0$) by constructing the non-degenerate simplex from orthogonal adjacent edges with correct orientation parity.

2. **R1 Sub-Pyramid Volume Check (`octree_hexcore.cpp:848-879`)**:
   ```cpp
   for (int64_t f_idx : it_faces->second) {
     const auto& mf = mosaic_faces[f_idx];
     auto [sf_new, cf_new] = computePolygonNormalAndCentroidReplaced(
         mf.nodes, node_coords, v, test_pos);

     int64_t o = mf.owner;
     if (o >= 0) {
       const OctreeNode& onode = nodes[active_leaves[o]];
       Vec3 ref_c = cell_approx_center[o];
       double pyr_vol = dot(cf_new - ref_c, sf_new) / 3.0;
       if (pyr_vol <= 1e-15 * onode.v_bg) { valid = false; break; }
     }
     int64_t nb = mf.neighbour;
     if (nb >= 0) {
       const OctreeNode& onode = nodes[active_leaves[nb]];
       Vec3 ref_c = cell_approx_center[nb];
       double pyr_vol = dot(cf_new - ref_c, -sf_new) / 3.0;
       if (pyr_vol <= 1e-15 * onode.v_bg) { valid = false; break; }
     }
   }
   ```
   Guarantees that no sub-pyramid sharing vertex $v$ in any adjacent owner or neighbour cell inverts or collapses below $10^{-15} V_{\text{bg}}$.

3. **R2 Triangle-Fan & Reference Point Pyramids (`octree_hexcore.cpp:93-186, 917-953`)**:
   ```cpp
   Vec3 c_face(0, 0, 0);
   for (int64_t idx : face_nodes) c_face += points[idx];
   c_face /= static_cast<double>(n);

   for (size_t i = 0; i < n; ++i) {
     const Vec3& pi = points[face_nodes[i]];
     const Vec3& pj = points[face_nodes[(i + 1) % n]];
     Vec3 cross_p = cross(pi - c_face, pj - c_face);
     double area_tri = cross_p.norm() * 0.5;
     normal_sum += cross_p * 0.5;
     Vec3 tri_c = (c_face + pi + pj) / 3.0;
     centroid_sum += tri_c * area_tri;
     total_area += area_tri;
   }
   ```
   And for cell volumes:
   ```cpp
   double pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0;
   vol_sum += pyr_vol;
   Vec3 pyr_c = mf.centre * 0.75 + c_approx * 0.25;
   cent_sum += pyr_c * pyr_vol;
   ```
   Uses area-weighted polygon centroid integration and local reference point $c_{\text{approx}}$ pyramid decomposition.

4. **R3.1-2 Small-Cell Merging & Cell Weighting (`octree_hexcore.cpp:995-1001, 1034-1150, 1324-1337`)**:
   - `cell_types[best_nb] = CellType::POLYHEDRON;` (42).
   - Union of vertices:
     `cell_verts[best_nb].insert(cell_verts[best_nb].end(), cell_verts[c].begin(), cell_verts[c].end());`
     deduplicated via `std::sort` and `std::unique`.
   - Internal faces grouped into `internal_face_groups[{new_o, new_nb}]` with $new\_o < new\_nb$. Pairs with $> 1$ face merged, perimeter edges extracted, and collapsed into a single outer face cycle.
   - `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c]);` across all adjacent faces.

5. **R5.2 DistMesh Boundary Push-Out Projection (`distmesh.cpp:735-756`)**:
   ```cpp
   double min_d2 = 1e30;
   Vec2 best_proj = all_pts[i];
   for (size_t j = 0, k = p_nodes.size() - 1; j < p_nodes.size(); k = j++) {
     Vec2 ab = p_nodes[j] - p_nodes[k];
     Vec2 ap = new_pos - p_nodes[k];
     double ab2 = ab.norm2();
     double t = (ab2 > 1e-15) ? std::clamp((ap.x * ab.x + ap.y * ab.y) / ab2, 0.0, 1.0) : 0.0;
     Vec2 proj = p_nodes[k] + ab * t;
     double d2 = (new_pos - proj).norm2();
     if (d2 < min_d2) {
       min_d2 = d2;
       best_proj = proj;
     }
   }
   all_pts[i] = best_proj;
   ```
   Proper orthogonal segment projection using bounded projection parameter $t \in [0, 1]$.

### Empirical Execution Observations
1. **Compilation**:
   `cmake --build backend/meshing/native/build --config Release --parallel 8`
   Output: `cfd_mesher_native.vcxproj -> G:\Datein\Projects\CFDSolver\backend\meshing\cfd_mesher_native.dll`
   Result: Exit code 0, 0 compiler warnings.

2. **Official Test Suite**:
   `pytest tests/test_wp4_multiphysics_hexcore.py -v`
   Result: 24 passed in 57.45s (100% pass rate).

3. **Adversarial Stress Testing**:
   - Arbitrary translation shifts (e.g. $[123.456, -789.012, 42.0]$): cell volumes and centroids strictly translation invariant, total volume $1.00000000$, centroid matches analytical $[123.956, -788.512, 42.500]$ within machine precision.
   - Scaled sphere snapping ($R=1.5$): all cells strictly positive volume, median distance to surface $< 0.05$.
   - Non-convex star polygon pushout: all points bounded, 0 points leaked into exterior.

---

## 2. Logic Chain

1. **Absence of Cheating**:
   - Every algorithm implements its mathematical specification directly in C++20 without shortcuts or hardcoded outputs.
   - Code inspection reveals zero lookup tables, fake pre-populated values, or test-matching branch literals.
2. **Fidelity to Ground-Truth Requirements**:
   - R1: Eliminates collinear tetrahedron degeneracy by indexing corner nodes ($0..7$), selecting the three orthogonal edges, and swapping orientation when $s < 0$. Positive sub-pyramid checks guard against cut-cell self-intersection.
   - R2: Center-point triangle fan treats non-planar quads symmetrically. Local reference point pyramid integration ensures strict translation invariance.
   - R3.1-2: Topological cell agglomeration sets cell type 42, unions node sets, deduplicates internal faces, and stores minimum upwind factor in `omega_cell`.
   - R5.2: Orthogonal segment projection enforces Dirichlet boundary confinement for points pushed outside the domain during DistMesh force iterations.
3. **Empirical Verification**:
   - Compilation and execution of native code were independently executed by the auditor.
   - Tests pass cleanly, demonstrating end-to-end functionality across both C++ and Python ctypes interfaces.

---

## 3. Caveats

1. **Surface Discretization Error (Sagitta)**:
   In test `test_cut_cell_sphere_wall_snapping_native`, geodesic icospheres with subdivision level 4 (5,120 triangles) have maximum chord sagitta $\approx 0.0006 < 10^{-3}$, yielding median $|\phi| < 10^{-3}$. When tested with coarser icospheres (subdivision level 3, 1,280 triangles), the geometric sagitta of the surface triangles themselves is $\approx 0.003$. Because `MeshSDF` evaluates distance to the input discrete surface triangles, wall nodes snap accurately to the triangles; the remaining deviation relative to an ideal analytical sphere is purely the triangulation discretization error.
2. **Floating-Point Coordinates and Absolute Tolerances**:
   When testing translation invariance with large coordinate offsets (e.g. $\sim 1000$), floating-point machine precision (53-bit IEEE-754 mantissa, $\epsilon \approx 2.22 \times 10^{-16}$) introduces an absolute uncertainty of $\sim 1000 \times \epsilon \approx 2 \times 10^{-13}$. Under multi-stage summation of thousands of faces, absolute errors of $\sim 5 \times 10^{-11}$ may occur, satisfying the required engineering tolerance ($10^{-5}$) while exceeding overly restrictive assertions of $< 10^{-12}$.
3. **UnstructuredMesh Python API**:
   The Python `UnstructuredMesh` class exposes cell node connectivity through `mesh.cells` (a list of numpy arrays) rather than raw internal CSR arrays `mesh.cell_offsets` and `mesh.cell_nodes`.

---

## 4. Conclusion

Milestone M1 has been audited for integrity and correctness. The work product is **completely genuine**, implements all mathematical and geometric requirements authentically, contains zero cheating, zero facades, and zero hardcoded outputs.

**Final Forensic Verdict**: **CLEAN**

---

## 5. Verification Method

To independently reproduce this forensic audit:

1. **Verify Source Code Cleanliness**:
   ```bash
   grep -rn "10.5" backend/meshing/native/src/
   # Expected: No hits in source code
   ```
2. **Compile Native DLL**:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release --parallel 8
   # Expected: Exit code 0, 0 compiler warnings
   ```
3. **Execute Full Test Suite**:
   ```powershell
   $env:OMP_NUM_THREADS = "8"
   .venv\Scripts\pytest.exe tests/test_wp4_multiphysics_hexcore.py -v
   # Expected: 24 passed in ~57s
   ```
4. **Execute Specific M1 Integrity Tests**:
   ```powershell
   $env:OMP_NUM_THREADS = "8"
   .venv\Scripts\pytest.exe tests/test_wp4_multiphysics_hexcore.py -k "test_cut_cell_sphere_wall_snapping_native or test_translation_invariant_centroids_and_volumes or test_agglomeration_polyhedron_and_omega_cell or test_distmesh_boundary_pushout_projection" -v
   # Expected: 4 passed
   ```
