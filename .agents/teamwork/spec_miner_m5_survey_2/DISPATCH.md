# Dispatch: Spec Miner M5 Survey 2 (Paper Integration & Super-Elements Architecture)

## Mission
Investigate and enumerate exact mathematical formulations, algorithms, data structures, and integration points for:
1. **R2: Direct Code Integration of Surveyed Papers**:
   Examine the 8 papers located in `resources/papers/`:
   - Paper 1 (`CADWatertightFineMeshing_2604.02141v1.pdf`): Topology-first B-Rep meshing, Jordan curve loop separation, and 1D boundary stitching in `backend/geometry/parsers/step_parser.py` for watertight CAD import.
   - Paper 2 (`Differtialble3DCAD_2110.01182v1.pdf`): Compiled CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$, geometric safety inequalities $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$, and adjoint chain rule interfaces.
   - Paper 3 (`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`): $L^p$-metric tensor sizing $\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ and noise-free $P_2$ edge midpoint reconstruction in `backend/meshing/mesh_engine.py` and `backend/meshing/native/src/octree_hexcore.cpp`.
   - Paper 4 (`MeshlessVoronoiOnGPU_3272127.3275092.pdf`): Pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`) with radius-of-security termination.
   - Paper 5 (`VoronoiDiagrams_2404.18629v1.pdf`): Analytical 1st and 2nd derivatives of Voronoi/power diagram vertices via $A\mathbf{x}_c = b$ ($C^1$ volumes across topology flips).
   - Paper 6 (`DiscreteGCL_1709.04788v4.pdf`): Exact 3D swept volume via Simpson's rule $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ and BDF2-DGCL $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$ in `backend/meshing/moving_mesh.py` (resolving Audit-H9).
   - Paper 7 (`Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf`): Orthogonal symmetry plane connection ($\boldsymbol{\Delta} \parallel \mathbf{s}$, zero cross-diffusion) and Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vectors and stress tensors in `backend/meshing/polymesh_io.py`, `backend/meshing/difvm_topology.py`, and `backend/meshing/case_generator.py`.
   - Paper 8 (`FSI_Monolithic_2505.22920v1.pdf`): Monolithic 3D Eulerian FSI with $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$, Neo-Hookean solid, 3D geometric PLIC, and fluid reset $\mathbf{B} = \mathbf{I}$ for $\phi < 10^{-8}$.
2. **R3: Backend Architecture Synthesis and Super-Elements Integration**:
   - Section 10 in `SUGGESTIONS_BACKEND.md` (`SuperElements_Db_115554.pdf`, Stromberg 2023): `constexpr` 23-class super-element integration in `octree_hexcore.cpp`, asymmetric residual volume voting ($RV_{\text{missing}}^2 + RV_{\text{excess}}$), top-down DOF hierarchy, and $J_{\text{ENS}}$ / pyramid base check in `models.py` / `difvm_mesh.cpp`.
   - Section 11.2 in `SUGGESTIONS_BACKEND.md` (Carbon's Architecture Synthesis): Unified C++20/23 core with Rust orchestration; strict C-ABI Structure-of-Arrays (SoA) data lake interfaces (`(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)`) with embedded expression engine (Rhai / SIMD bytecode) for generic BCs/sources; native binary container format `.mphys` (64-byte `MPHYS001` header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, FlatBuffers/Parquet footer `MPHYSEOF`) in `backend/native_rust/`; backend C-ABI SoA zero-copy buffer export interfaces. Note: renderers/shaders/presentation display logic are strictly out of scope.

## Authoritative Inputs
- Read `ORIGINAL_REQUEST.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- Papers: `g:\Datein\Projects\CFDSolver\resources\papers\`
- Backend suggestions: `g:\Datein\Projects\CFDSolver\SUGGESTIONS_BACKEND.md`
- Existing backend code in `backend/`
- State folder: `g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2`

## Output Requirements
Write `report.md` and `handoff.md` in your state folder:
- Mathematical equations, definitions, invariants, and algorithms extracted from each paper and suggestion section.
- Mapping to target files and classes in `backend/`.
- Concrete implementation specifications and tests to write.
- Send completion message to orchestrator.


## 2026-11-12T01:31:04Z
You are spec_miner_m5_survey_2.
Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2
Authoritative request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Dispatch file: g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\DISPATCH.md

You MUST read ORIGINAL_REQUEST.md before starting work. Do NOT summarize or filter it.

Your mission:
Survey and extract exact mathematical formulations, algorithms, invariants, and code integration designs for:
1. Direct Code Integration of 8 Surveyed Papers (resources/papers/):
   - Paper 1 (CADWatertightFineMeshing_2604.02141v1.pdf): Topology-first B-Rep meshing, Jordan curve loop separation, and 1D boundary stitching in backend/geometry/parsers/step_parser.py for watertight CAD import.
   - Paper 2 (Differtialble3DCAD_2110.01182v1.pdf): Compiled CAD DAG x_surf = F(theta_CAD), geometric safety inequalities g_m(theta_CAD) >= 0, and adjoint chain rule interfaces.
   - Paper 3 (Continuous_Mesh_Framework_Part_II_Validations_and_.pdf): L^p-metric tensor sizing M_Lp(x) = D_Lp det(|H_u|)^(-1/(2p+3)) |H_u| and noise-free P2 edge midpoint reconstruction in backend/meshing/mesh_engine.py and backend/meshing/native/src/octree_hexcore.cpp.
   - Paper 4 (MeshlessVoronoiOnGPU_3272127.3275092.pdf): Pure dual convex 3D polyhedron representation (float4 P[64], int3 T[96]) with radius-of-security termination.
   - Paper 5 (VoronoiDiagrams_2404.18629v1.pdf): Analytical 1st and 2nd derivatives of Voronoi/power diagram vertices via A x_c = b (C^1 volumes across topology flips).
   - Paper 6 (DiscreteGCL_1709.04788v4.pdf): Exact 3D swept volume via Simpson's rule Delta V_f^{n,n+1} = (1/6)(x_f^{n+1} - x_f^n) . (S_f^n + 4 S_f^{n+1/2} + S_f^{n+1}) and BDF2-DGCL dot{V}_f^{n+1} = (3 Delta V_f^{n,n+1} - Delta V_f^{n-1,n}) / (2 Delta t) in backend/meshing/moving_mesh.py (resolving Audit-H9).
   - Paper 7 (Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf): Orthogonal symmetry plane connection (Delta || s, zero cross-diffusion) and Householder reflection tensor R = I - 2 n (x) n for vectors and stress tensors in backend/meshing/polymesh_io.py, backend/meshing/difvm_topology.py, and backend/meshing/case_generator.py.
   - Paper 8 (FSI_Monolithic_2505.22920v1.pdf): Monolithic 3D Eulerian FSI with sigma_{ij} = phi sigma_{ij}^s + (1-phi) sigma_{ij}^f, Neo-Hookean solid, 3D geometric PLIC, and fluid reset B = I for phi < 10^{-8}.
2. Backend Architecture Synthesis & Super-Elements (SUGGESTIONS_BACKEND.md §10 & §11.2):
   - Section 10 (SuperElements_Db_115554.pdf, Stromberg 2023): constexpr 23-class super-element integration in octree_hexcore.cpp, asymmetric residual volume voting (RV_missing^2 + RV_excess), top-down DOF hierarchy, and J_ENS / pyramid base check in models.py / difvm_mesh.cpp.
   - Section 11.2 (Carbon's Architecture Synthesis): Unified C++20/23 core with Rust orchestration; strict C-ABI Structure-of-Arrays (SoA) data lake interfaces ((void* ptr, size_t len, uint32_t dtype, uint32_t device_id)) with embedded expression engine (Rhai / SIMD bytecode) for generic BCs/sources; native binary container format .mphys (64-byte MPHYS001 header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, FlatBuffers/Parquet footer MPHYSEOF) in backend/native_rust/; backend C-ABI SoA zero-copy buffer export interfaces. Note: renderers/shaders/presentation display logic are strictly out of scope.
