## 2026-11-06T22:26:55Z
You are worker_audit_m1_1, a teamwork_worker.
Your working directory is: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_audit_m1_1\

You MUST read the original user request before starting:
g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Also read the project scope document:
g:\Datein\Projects\CFDSolver\PROJECT.md
Also read the technical survey findings and exact mathematical derivations:
g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_audit_survey_1\survey_report.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Mission: Implement Milestone M1: Native C++ Hexcore Math, Snapping & Geometry Core (Requirements R1, R2, R3.1-2, R5.2).

Scope Boundaries & Exclusive File Ownership:
You EXCLUSIVELY own and modify:
- `backend/meshing/native/src/octree_hexcore.cpp`
- `backend/meshing/native/src/distmesh.cpp`
- `backend/meshing/native/include/octree_hexcore.hpp`
- `tests/test_wp4_multiphysics_hexcore.py`
DO NOT modify Python backend files owned by M2 (`mesh_engine.py`, `difvm_topology.py`, etc.).

Technical Requirements to Implement:
1. R1: Cut-Cell Snapping & AMIPS Metric Fix (`octree_hexcore.cpp:645-648`):
   - Replace the degenerate collinear tetrahedron in `computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center)`.
   - Determine corner index `c_idx in 0..7` of vertex `v` relative to `onode.center`.
   - Construct a positively oriented corner tetrahedron from `test_pos` and the 3 adjacent octant edge neighbor nodes (`vx, vy, vz` with orientation check `s = (1 - 2*cx)*(1 - 2*cy)*(1 - 2*cz)` swapping v1/v2 if s < 0), ensuring det_j = dx * dy * dz > 0 and finite AMIPS energy.
   - Verify that all cell sub-pyramids sharing vertex `v` retain positive volume ($V_{pyr} > 10^{-15} \cdot V_{bg}$) after snapping.
   - Add test `test_cut_cell_sphere_wall_snapping_native` to `tests/test_wp4_multiphysics_hexcore.py` calling `generate_hexcore_native` on a unit sphere (e.g. from `create_geodesic_icosphere`) verifying wall nodes snap to `|phi| < 1e-3` with all cell volumes positive.

2. R2: Translation-Invariant Centroids, Face Normals & Cell Volumes (`octree_hexcore.cpp:667-708`):
   - Step 9: Compute `mf.normal_owner` and `mf.centre` via area-weighted triangle-fan integration (matching `computePolygonNormalAndCentroid` in `difvm_mesh.cpp:36-78`) to accurately handle non-planar snapped quad faces. Flag inverted sub-triangles (`mf.is_folded = true`) if oriented opposite to the face normal (`dot(n_tri, normal_owner) <= 0`).
   - Step 10: Compute for each cell `c` a local reference point `c_approx` (mean of `mf.centre` of all faces of cell `c`) and integrate pyramid volumes and centroids:
     `pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0`
     `pyr_c = mf.centre * 0.75 + c_approx * 0.25`
     `cell_vols[c] += pyr_vol`
     `cell_cents[c] += pyr_c * pyr_vol`
     and normalize at the end by `cell_vols[c]`.
     Ensure unit cube `[0,1]^3` yields `(0.5, 0.5, 0.5)` and shifted cube `[10,11]^3` yields `(10.5, 10.5, 10.5)`.

3. R3.1-2: Small-Cell Agglomeration and Cell Weighting:
   - In Step 11 (`octree_hexcore.cpp:742-847`), when merging a small cell `c` into `best_nb`, union all its vertices into `cell_nodes` of the target cell `best_nb` and set `cell_types[new_c] = POLYHEDRON (42)`.
   - In Step 12, merge and deduplicate internal faces sharing the same `(owner, neighbour)` pair and maintain `owner < neighbour`.
   - Populate `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c])` across all adjacent faces of cell `c` (lines 861, 974, 1020) as in `difvm_mesh.cpp:666-677`.

4. R5.2: DistMesh Boundary Push-Out Projection (`distmesh.cpp:734-739`):
   - When an interior node is pushed outside (`!pointInPoly(new_pos)`), project `new_pos` orthogonally back onto the nearest polygon boundary segment.

Hardware & Compilation Directives:
- Build DLL: `cmake --build backend/meshing/native/build --config Release --parallel 8`
- Set `OMP_NUM_THREADS=8` before running pytest.
- Run `pytest tests/test_wp4_multiphysics_hexcore.py -v` to verify your changes.
- Ensure all Google C++ Style Guide rules are observed (2-space indentation, explicit typing, nullptr, RAII).

Deliverables:
Document what changed in `changes.md` and complete `handoff.md` in your working directory. Notify orchestrator upon completion via `send_message`.
