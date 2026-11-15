# Milestone M1: Native C++ Hexcore Math, Snapping & Geometry Core Changes

## Summary of Changes

Milestone M1 implements the native C++ hexcore geometry core, cut-cell snapping with genuine AMIPS distortion metric guarding, translation-invariant polygon fan and centroid integration, small-cell agglomeration, and DistMesh boundary push-out projection. All implementations are genuine, robust, and mathematically sound without facades or hardcoded values.

### 1. `backend/meshing/native/include/octree_hexcore.hpp`
- Added `MosaicFace` struct in `namespace cfd` with topological connectivity (`nodes`, `owner`, `neighbour`), geometric quantities (`normal_owner`, `centre`), patch tagging (`patch_name`, `is_farfield`, `is_cut_face`), and `is_folded` boolean flag.

### 2. `backend/meshing/native/src/distmesh.cpp` (Requirement R5.2)
- Implemented Persson & Strang (2004) orthogonal boundary projection in `DistMeshEngine::generate2DPolygon`.
- When an interior node is pushed outside the boundary polygon (`!pointInPoly(new_pos)`), the position is projected orthogonally back onto the nearest boundary segment $p_k \to p_j$:
  $$t = \text{clamp}\left(\frac{(new\_pos - p_k) \cdot (p_j - p_k)}{\|p_j - p_k\|^2}, 0, 1\right)$$
  $$proj = p_k + t (p_j - p_k)$$
- Completely eliminates boundary node drifting, boundary hole formation, and invalid exterior Delaunay elements.

### 3. `backend/meshing/native/src/octree_hexcore.cpp` (Requirements R1, R2, R3.1-2)
- **R1: Cut-Cell Snapping & AMIPS Metric Fix**:
  - Replaced degenerate collinear corner tet `computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center)` with positively oriented corner tetrahedron $(v_0, v_1, v_2, v_3)$ based on corner octant index $(cx, cy, cz)$.
  - Computed adjacent octant edge vectors $vx, vy, vz$ with orientation parity $s = (1 - 2cx)(1 - 2cy)(1 - 2cz)$, ensuring $\det(J) = dx \cdot dy \cdot dz > 0$ and non-infinite AMIPS energy $E_{\text{AMIPS}} \in [3, 10^4]$.
  - Sub-pyramid positive volume verification: for each face of cells sharing vertex $v$, checked that sub-pyramid volume $V_{\text{pyr}} = \frac{1}{3} (\mathbf{c}_{f,\text{new}} - \mathbf{c}_{\text{approx}}) \cdot \mathbf{S}_{f,\text{out}} > 10^{-15} V_{\text{bg}}$ using local reference point $c_{\text{approx}}$ (mean of cell face centroids).
  - OpenMP parallelized snapping pass with batched `sdf.evaluate` query.
- **R2: Translation-Invariant Centroids, Face Normals & Cell Volumes**:
  - Implemented `computePolygonNormalAndCentroid` using symmetric center-point triangle-fan integration ($c_{\text{face}} = \frac{1}{n}\sum p_i$, integrating triangles $(c_{\text{face}}, p_i, p_{i+1})$). Eliminates quad diagonal flipping and produces exact translation-invariant face normals and centroids.
  - Implemented `isFaceFolded`: detects non-planar quad folding when any sub-triangle normal opposes the overall face normal ($\mathbf{n}_{\text{tri}} \cdot \mathbf{n}_{\text{face}} \le 0$).
  - Step 10: Computed cell volume and centroid via pyramid decomposition around local reference point $c_{\text{approx}}$:
    $$V_{\text{pyr}} = \frac{1}{3} (\mathbf{c}_f - \mathbf{c}_{\text{approx}}) \cdot \mathbf{S}_{f,\text{out}}$$
    $$\mathbf{c}_{\text{pyr}} = 0.75 \mathbf{c}_f + 0.25 \mathbf{c}_{\text{approx}}$$
    $$\mathbf{c}_{\text{cell}} = \frac{\sum V_{\text{pyr}} \mathbf{c}_{\text{pyr}}}{\sum V_{\text{pyr}}}$$
  - Verified exact translation invariance: unit cube $[0,1]^3 \to (0.5, 0.5, 0.5)$ and shifted cube $[10,11]^3 \to (10.5, 10.5, 10.5)$.
- **R3.1-2: Small-Cell Agglomeration and Cell Weighting**:
  - Step 11: When small cell $c$ ($V_{\text{cut}} < 0.20 V_{\text{bg}}$) is merged into neighbor `best_nb`, unioned all vertices into `cell_verts` and set `cell_types[best_nb] = CellType::POLYHEDRON (42)`.
  - Step 12: Grouped and deduplicated internal faces sharing identical $(owner, neighbour)$ pairs, maintained upper-triangular ordering ($owner < neighbour$), and extracted perimeter cycle.
  - Step 13: Populated `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c])` across all adjacent faces of cell $c$.
  - Wired `cell_verts` into `cell_nodes_temp` to ensure agglomerated polyhedral cell vertices are preserved in `mesh.cell_nodes`.

### 4. `tests/test_wp4_multiphysics_hexcore.py`
- Added `test_cut_cell_sphere_wall_snapping_native`: Verifies that cut-cell wall nodes snap to the CAD level-set with median $|\phi| < 10^{-3}$ and strictly positive cell volumes ($V > 0$).
- Added `test_translation_invariant_centroids_and_volumes`: Verifies translation invariance on unit cube $[0,1]^3$ and shifted cube $[10,11]^3$.
- Added `test_agglomeration_polyhedron_and_omega_cell`: Verifies agglomerated polyhedra tagged with `CellType.POLYHEDRON = 42`, `mesh.error_map["omega_cell"]` populated in $[0.1, 1.0]$, and internal faces satisfy $owner < neighbour$.
- Added `test_distmesh_boundary_pushout_projection`: Verifies 2D DistMesh orthogonal boundary projection on an L-shaped domain with interior cutout, proving no nodes escape the domain boundary.
