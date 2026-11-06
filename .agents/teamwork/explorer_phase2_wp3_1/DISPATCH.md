## 2026-11-03T01:13:20Z
You are an Explorer agent for Milestone WP3 (Robust 2D-Mesher).
Your working directory is: G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase2_wp3_1\

Read ORIGINAL_REQUEST.md at:
G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

CRITICAL RESOURCE LIMITS (Background Ansys simulation running!):
1. CPU Thread Limit: Max 16 cores/threads for all runs, OpenMP parallelizations, and tests (DO NOT use all 28 cores!). Always set $env:OMP_NUM_THREADS="16".
2. RAM Usage: ~95% occupied by Ansys. Work extremely memory-efficiently.

Inspect the existing codebase in backend/meshing/:
- backend/meshing/distmesh_core.py
- backend/meshing/mesh_engine.py
- backend/meshing/models.py
- backend/meshing/native/ (difvm_mesh, native exports)
- tests/ (existing 2D mesh tests)

Milestone WP3 Requirements:
1. Constrained Delaunay / DistMesh with adaptive sizing function h(x) and boundary edge subdivision governed by h(x):
   - Adaptive sizing h(x) (curvature-based, distance-to-boundary, user sizing functions).
   - Equispaced or adaptive resampling of boundary edges so boundary segments match local target edge length h(x).
   - Force-displacement relaxation (Persson & Strang 2004 DistMesh) in 2D with robust convergence.
2. Shoelace CCW-Canonicalization of Polygons and Boundaries:
   - Automatic detection of polygon orientation via Shoelace formula: signed area A = 0.5 * sum(x_i * y_{i+1} - x_{i+1} * y_i).
   - If A < 0 (clockwise), automatically reverse vertex ordering to counter-clockwise (CCW).
   - Preserve hole orientation (holes clockwise vs outer boundary CCW) according to standard 2D polygon conventions.
3. Ear-Clipping / Constrained Delaunay Boundary Fidelity without Blind Centroid Fan:
   - Replace or eliminate naive centroid fan (which creates inverted or non-convex / concavities).
   - Ear-clipping algorithm (Held 2001 / Eberly) or Constrained Delaunay Triangulation guaranteeing boundary preservation for arbitrary non-convex polygons.
   - Strictly positive cell areas V_i > 0 for all triangles (no inverted triangles).
   - Exact analytical area conservation: sum(V_i) == analytical polygon area within float precision (|sum(V_i) - A_exact| < 1e-12).

Your task:
Analyze current 2D mesher implementation in backend/meshing/distmesh_core.py, backend/meshing/mesh_engine.py, etc. Identify shortcomings (e.g. centroid fan fallback, lack of boundary edge subdivision according to h(x), CW polygon handling, area conservation).
Produce an exact, mathematically rigorous architectural and implementation blueprint in:
G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase2_wp3_1\handoff.md
Detail the required functions, C++20/Python interfaces, and verification test cases.
Notify me when done via send_message. Do NOT modify any source code files.
