## 2026-10-28T00:25:23Z
You are spec_miner_mesh_1.
Your working directory is: G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_mesh_1
You MUST read:
1. G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md (specifically the section "## Follow-up — 2026-10-28T00:22:07Z")
2. Reference snippets & templates in G:\Datein\Projects\CFDSolver\resources\reference_spickzettel\LMArena\ (inspect all subdirectories, especially 2/backend for Rust core, 1/native for C++20 core, and any relevant meshing/DiFVM/SPH snippets).

Your mission is to perform a comprehensive specification extraction of all mathematical formulas, algorithms, data structures, and invariants required for the In-Memory Multiphysics Mesher Backbone:
1. DiFVM (Du et al., 2026) Graph-Topology & SoA:
   - Precise definitions of owner, neighbour, face_areas (S_f), face_centres, cell_centres, cell_volumes.
   - Vectorized definitions of orthogonal projection delta_f and over-relaxed correction k_f such that delta_f + k_f == S_f.
   - Exact definitions of cell & face quality metrics: non-orthogonality angle, skewness, aspect ratio, and how they form the error-map stabilization mask for PISO/SIMPLE.
2. In-Memory Unstructured Meshing & Boundary Layers (DistMesh, Bowyer-Watson, RCM):
   - Force equilibrium formulation (Persson & Strang, 2004) in 2D and 3D, bar forces, boundary projection using continuous SDF, node redistribution.
   - Delaunay triangulation / Bowyer-Watson kernel.
   - Inflation & buffer layers: structured prismatic/quad boundary layers at physical walls with geometric expansion ratio for y+ ~ 1 resolution and smooth transition to interior unstructured mesh.
   - Reverse Cuthill-McKee (RCM) bandwidth reduction algorithm and permutation mapping.
3. Hybrid Eulerian SPH-FVM Coupling (Wang et al., 2025 & Zhang et al., 2025 OPMIS):
   - Incomplete support detection: div r < 0.75 d.
   - Virtual FVM wall boundary vector: A_iS = -sum_j V_j grad W_ij (0th-order consistency < 10^-10, A_iS + sum_j V_j grad W_ij = 0).
   - OPMIS partitioning: Core particles, Halo/Buffer particles, FVM mesh cells.
   - Moment-reproducing Taylor interpolation (P2M and M2P) guaranteeing exact reconstruction of linear fields f(x) = a · x + b.
4. Moving Mesh & Dynamic Re-Adaption:
   - ALE mesh deformation: boundary node displacement, interior Laplacian smoothing.
   - Degenerate cell detection and remeshing triggers.
5. Python-Bridge & Pure-Python Fallback:
   - Requirements for backend/meshing/mesh_engine.py so it runs with NumPy/SciPy without native binaries compiled.

Produce a detailed specification report: G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_mesh_1\spec_report.md
When finished, send a handoff message to the orchestrator (conversation ID: 4ce92fc5-81ba-402c-a361-d39a59aea46d).
