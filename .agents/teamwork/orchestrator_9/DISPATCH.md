# Dispatch Instructions — Orchestrator Generation 9

## 2026-11-13T23:15:00Z

Resume execution of CFDSolver generalization, paper integration, and Phase 5 completion starting directly from Milestone M5B (Features F55–F61).

Prior Certified Status:
- M1_PHASE1 through M4_PHASE4: Certified DONE with unanimous 3:0 PASS (63/63 tests passing).
- M5A (SMR Excision, Multiphysics Chemistry & OpenFOAM Audits H5, H6, M10): Certified DONE with unanimous 3:0 PASS (52/52 tests passing).

Current Milestone Objectives (M5B: Direct Code Integration of Surveyed Papers):
1. Paper 6 / Audit H9 (DiscreteGCL_1709.04788v4.pdf): Exact 3D swept volume via Simpson's rule and BDF2-DGCL in backend/meshing/moving_mesh.py.
2. Paper 7 (Demirdzic-2022): Orthogonal symmetry connection and Householder reflection tensor R = I - 2n(x)n for vectors and stress tensors in polymesh_io.py, difvm_topology.py, and case_generator.py.
3. Paper 1 (CADWatertightFineMeshing_2604.02141v1.pdf): Topology-first B-Rep meshing, Jordan curve loop separation, and 1D boundary stitching in step_parser.py for watertight CAD import.
4. Paper 2 (Differtialble3DCAD_2110.01182v1.pdf): Compiled CAD DAG x_surf = F(theta_CAD), geometric safety inequalities g_m(theta_CAD) >= 0, and adjoint chain rule interfaces in backend/geometry/differentiable_cad.py.
5. Paper 3 (Continuous_Mesh_Framework_Part_II): L^p-metric tensor sizing and noise-free P2 edge midpoint reconstruction in mesh_engine.py / octree_hexcore.cpp.
6. Papers 4 & 5 (Ray 2018 / Numerow 2024): Dual GPU Voronoi representation (float4 P[64], int3 T[96]) and analytical 1st/2nd derivatives across topology flips in backend/meshing/gpu_voronoi.py.
7. Paper 8 (FSI_Monolithic_2505.22920v1.pdf): Monolithic Eulerian 3D FSI with Neo-Hookean solid, 3D geometric PLIC, and fluid reset B = I for phi < 1e-8 in backend/physics_modules/monolithic_fsi.py.

Incorporation of New Additions (Stand 2026-11-13, SUGGESTIONS_BACKEND.md §12 & Reference Snippets 12–14):
- Six FVM/GPU Numerics & HPC Contracts (§12.2): base-all Hilbert quadrants, compact 6% MeshCorrectionSoA (strictly separated _x/_y/_z arrays), linear-exact plane intersection & skewness vector, M-matrix deferred correction, atomic-free CellFaceCSR gather, 2-stage halo contract, 3-Map MeshErrorMap & SPD Cholesky factorization.
- Reactive Multiscale Chemistry & Coupling Contracts (§12.3): Operator splitting + DLBFoam active-cell balancing, AoT sparse Jacobian J_kj, explicit consistent/conservative coupling.
- Clean up remaining passing mention of smrKineticsSource.C in SUGGESTIONS_BACKEND.md:630 under R1.

Constraints & Governance:
- Host protection: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2, OPENBLAS_NUM_THREADS=2.
- Sequential pytest execution (.\\.venv\\Scripts\\python.exe -m pytest ...).
- Small test grids (<= 30,000 cells). No HPC/heavy benchmarks.
- Zero-Shortcut Policy: throw exceptions before crashes, strict bounds checks, no heuristic clamps, no swallowed exceptions.
- Rendering and UI remain strictly out of scope (backend IO/IPC contracts only).
- Mandatory 3-member review panel requiring unanimous 3:0 PASS per milestone (1x Anti-Cheat & Forensik, 1x Mathematik & Paper-Treue, 1x Code-Qualität & Stabilität).


## 2026-11-14T00:09:12Z

Received and acknowledged: Milestone M5B is fully certified with unanimous 3:0 PASS (Auditor, Critic, Reviewer). Gate status and scope recorded.
Please proceed immediately with Milestone M5C: Architecture Synthesis & Super-Elements Integration (Features F62 & F63) under all established hardware limits and 3:0 gate review criteria.


## 2026-11-14T00:49:27Z
Sender: 5c95e7e5-efcd-4486-8f6a-022d520361f2
Content:
CRITICAL USER DIRECTIVE (Pause Point):
The user has established an explicit pause boundary:
"As soon as Milestone M5D completes and achieves its 3:0 UNANIMOUS PASS gate certification (completing all of M5: M5A, M5B, M5C, M5D), STOP / PAUSE immediately BEFORE starting M6_FINAL and report M5D 3:0 PASS completion to me. Do NOT proceed into M6_FINAL yet — we will commit, push to Git, and pause after M5D passes."

Ensure this constraint is recorded in your BRIEFING.md, SCOPE.md, and workflow:
- Complete Milestone M5C and achieve 3:0 certification.
- Execute Milestone M5D (Features F64 & F65) and achieve 3:0 certification.
- IMMEDIATELY STOP / PAUSE after M5D is certified 3:0 PASS. DO NOT dispatch or start M6_FINAL. Report M5 completion to Sentinel.
