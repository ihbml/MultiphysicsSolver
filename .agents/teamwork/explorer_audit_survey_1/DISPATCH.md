## 2026-11-06T20:58:45Z
You are explorer_audit_survey_1, a teamwork_explorer.
Your working directory is: g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_audit_survey_1\
You MUST read the original user request before starting:
g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

Mission: Survey C++ Native Octree Hexcore & Mesh Generation Mechanics for Requirements R1, R2, R3.1-2, R5.2-3.
Scope boundaries: Read-only exploration and analysis. DO NOT modify any source code files.

Key Focus Areas:
1. R1: Cut-Cell Snapping & AMIPS Metric Fix in Octree Hexcore (`backend/meshing/native/src/octree_hexcore.cpp:645-648`).
   - Analyze `computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center)`.
   - Identify why it produces collinear tetrahedra with det_j = 0.
   - For each corner vertex v in adjacent octant onode, determine corner index c_idx in 0..7 and construct positively oriented corner tetrahedron from test_pos and 3 adjacent octant edge neighbor nodes, ensuring det_j = dx * dy * dz > 0 and finite AMIPS energy.
   - Analyze cell sub-pyramids sharing vertex v retaining positive volume after snapping.
   - Review `tests/test_wp4_multiphysics_hexcore.py` and specify sphere test verifying wall nodes snap to |phi| < 1e-3.

2. R2: Translation-Invariant Polygon Centroids, Face Normals, and Cell Volumes (`backend/meshing/native/src/octree_hexcore.cpp:667-708`).
   - Step 9: Analyze `mf.normal_owner` and `mf.centre` via triangle-fan integration (area-weighted sub-triangle centroids as in `computePolygonNormalAndCentroid` in `difvm_mesh.cpp:36-78`). Check flagging of inverted sub-triangles (`mf.is_folded`).
   - Step 10: Analyze local reference point `c_approx` (mean of face centroids) and pyramid volume integration as in `difvm_mesh.cpp:444-456`:
     `pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0`
     `pyr_c = mf.centre * 0.75 + c_approx * 0.25`
     Verify unit cube [0,1]^3 and shifted cube [10,11]^3 centroid calculation invariance.

3. R3.1-2: Small-Cell Agglomeration and Cell Weighting (`backend/meshing/native/src/octree_hexcore.cpp:742-847, 861, 1020`).
   - Analyze merging small cell c into best_nb: union vertex sets into `cell_nodes`, set `cell_types = POLYHEDRON (42)`.
   - Analyze merge & deduplication of internal faces sharing same (owner, neighbour) pair and enforce owner < neighbour.
   - Analyze `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c])` across all adjacent faces as in `difvm_mesh.cpp:666-677`.

4. R5.2-3: Native DistMesh Boundary Projection & Cell Filtering (`backend/meshing/native/src/distmesh.cpp:239-388, 734-739`).
   - Analyze interior node push-out handling: orthogonal projection of `new_pos` onto nearest polygon boundary segment.
   - Analyze cell filtering against surface triangles via MeshSDF (phi < 0 at centroid) or delegation to OctreeHexcore (domain_mode = 2).

Deliverable:
Write a comprehensive report to `g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_audit_survey_1\survey_report.md` and complete a structured handoff in `g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_audit_survey_1\handoff.md`.
Communicate completion back to orchestrator via send_message.
