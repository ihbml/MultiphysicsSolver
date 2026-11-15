# Dispatch: Orchestrator Generation 8 (Successor)

## Identity & Context
- Archetype: orchestrator
- Roles: orchestrator, user_liaison, human_reporter, successor
- Working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8
- Original Parent ID: 9afe4847-0716-4e2d-83e7-14376ba72fd4
- Predecessor Handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\handoff.md
- Scope Document: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md
- Gate Status: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\GATE_STATUS.md
- Original User Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

## Mission
Resume execution of the 5-phase CFDSolver remediation project. Milestones M1_PHASE1, M2_PHASE2, and M3_PHASE3 are certified DONE with 3:0 unanimous APPROVE.

Execute:
1. **Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2)**:
   - H3: 2D triangle centroids in `difvm_topology.py:1244` and `difvm_mesh.cpp:454` with $(2/3)\mathbf{c}_f + (1/3)\mathbf{c}_{\text{approx}}$ or Shoelace / Green's theorem. Arbitrary 2D quad $(0,0)-(4,0)-(1,1)-(0,1)$ yields exact $(1.4, 0.4)$. Adjust `models.py` Check 7 for 2D planar meshes with $z_0 \ne 0$.
   - H4: Eliminate `len(verts) == 8` heuristic in `moving_mesh.py`, `difvm_topology.py`, `difvm_mesh.cpp`. Explicitly preserve face lists and owner/neighbour connectivity during mesh motion.
   - H2: Full Celik et al. (2008) GCI implementation in `gci.py`: divergence detection ($R \ge 1$), iterative apparent order $p$, effective refinement ratio $r = (N_1/N_2)^{1/\text{dim}}$, remove clamps.
   - H7: Supermesh AMI polygon overlap weights in `difvm_topology.py:1788-1815` ($\sum w \cdot A = A$).
   - H8: SPH-FVM coupling & mass-conservative P2M interpolation in `coupling.py`.
   - M1 & M2: Aspect ratio definition (1.0 for unit cube) and `wsrd_alpha` relative to background octree cell volume.
   - Recompile `cfd_mesher_native.dll`.
   - Run 3-agent review panel (Anti-Cheat, Math Critic, Code Quality). Require 3:0 unanimous APPROVE.
2. **Milestone M5_PHASE5: Physik & OpenFOAM-Schnittstelle (K8, H5, H6, M10)**:
   - K8: Reversible SMR Xu-Froment kinetics & consistent enthalpy sink $q = -r \cdot \Delta H(T)$ in `smrKineticsSource.C`.
   - H5 & H6: Valid `chtMultiRegionFoam` exports in `polymesh_io.py` and `case_generator.py`.
   - M10: Safe WSL runner in `wsl_runner.py`.
   - Run 3-agent review panel (3:0 unanimous APPROVE).
3. **Milestone M6_FINAL: Final System Acceptance & Certification**:
   - Sequential regression test suite under `OMP_NUM_THREADS=2`.
   - Final unanimous sign-off and git commit.
   - Report final completion to parent `9afe4847-0716-4e2d-83e7-14376ba72fd4`.

## Critical Constraints
- Throttling: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`, grids $\le 30{,}000$ cells, sequential tests only.
- 3-agent review panel required per milestone (unanimous 3:0 PASS).
- Root communication markdown files (`SUGGESTIONS_BACKEND.md`, etc.) MUST remain untouched in root.


## 2026-11-10T16:19:13Z
[Message from 9afe4847-0716-4e2d-83e7-14376ba72fd4]
You are the Project Orchestrator (orchestrator_8) for CFDSolver.

Working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8
Authoritative Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Predecessor Dispatch & Scope: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\DISPATCH.md and g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md

Context & Mission:
Resumption after API quota reset. Milestones M1_PHASE1, M2_PHASE2, and M3_PHASE3 have been completed and certified with unanimous 3:0 PASS.

Your mission:
1. Execute Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2):
   - H3: 2D triangle centroids in backend/meshing/difvm_topology.py:1244 and backend/meshing/native/src/difvm_mesh.cpp:454 using Green's theorem weights (2/3) c_f + (1/3) c_approx. Verify test quadrilateral (0,0)-(4,0)-(1,1)-(0,1) evaluates to exact centroid (1.4, 0.4). In backend/meshing/models.py Check 7 (UnstructuredMesh.validate()), adjust boundary surface integral calculation for planar 2D meshes located at z0 != 0.
   - H4: Eliminate len(verts) == 8 guesswork in moving_mesh.py, difvm_topology.py, and difvm_mesh.cpp. Build node adjacency from explicit faces mesh.faces. In moving_mesh.py deform_mesh, preserve mesh.faces, owner, neighbour, cell_types, and boundary patches.
   - H2: Full Celik et al. (2008) GCI in backend/meshing/gci.py: divergence detection R >= 1 (convergence_type='divergence', apparent_order_p=nan, gci_fine=inf), fixed-point iteration for apparent order p when r21 != r32, effective grid refinement ratio r = (N1/N2)^(1/dim), remove artificial clamps.
   - H7: Supermesh-AMI area-conservative weights in difvm_topology.py (Sutherland-Hodgman polygon clipping, sum w_ij * A_i = A_i).
   - H8: Physical SPH-FVM coupling & conservative P2M interpolation in coupling.py (A_iS from actual FVM boundary geometry, volume-weighted P2M satisfying sum V_c phi_c = sum V_p phi_p).
   - M1 & M2: True aspect ratio definition (1.0 for unit cube) and wsrd_alpha defined relative to background octree cell volume.
   - Recompile backend/meshing/cfd_mesher_native.dll in MSVC 2022 C++20 Release mode.
2. Run verification tests sequentially under OMP_NUM_THREADS=2.
3. Convene the 3-agent gate review committee (Anti-Cheat / Cleanliness, Math Critic / Paper Fidelity, Code Quality / Architecture) to audit Milestone M4_PHASE4. Unanimous 3:0 PASS required.
4. CRITICAL USER DIRECTIVE: "Nach dem Pass gerne wieder pausieren" — As soon as Phase 4 achieves the unanimous 3:0 PASS gate certificate, cleanly stop and PAUSE execution before starting Phase 5 to conserve user token limits. Write handoff.md and notify the Sentinel.

Strict Constraints:
- Host Protection: Ansys is running in background. Strictly enforce OMP_NUM_THREADS=2, MKL_NUM_THREADS=2, OPENBLAS_NUM_THREADS=2.
- All tests must run strictly sequentially on small test grids (<= 30,000 cells).
- Strict Zero-Shortcut Policy: throw exceptions before crashes, no raw pointer leaks, strict bounds checking, no heuristic clamps, no swallowed exceptions.
- Root markdown communication files (SUGGESTIONS_*.md, COMMUNICATION_*.md) MUST remain untouched.
- Python environment: .\.venv\Scripts\python.exe.

Communicate all progress, reports, and gate status to the Sentinel.


## 2026-11-12T01:26:57Z
[Message from 5c95e7e5-efcd-4486-8f6a-022d520361f2]
Implement production-grade generalization of the CFD and multiphysics solver by completely excising specific SMR-Xu-Froment kinetics (Audit-K8) across documentation, requirements, frontend contracts, and codebase in favor of generic, data-driven multiphysics and chemistry interfaces. Directly integrate applicable methods from the 8 newly surveyed 2026/recent papers, `SUGGESTIONS_BACKEND.md` (§10 & §11.2), and the backend-facing IO/IPC interface contracts from `FRONTEND_REQUIREMENTS_ON_BACKEND.md` and `COMMUNICATION_BACKEND_FRONTEND.md` (rendering and UI remain strictly with the frontend), completing Phase 5 (Audits H5, H6, M10, H9) under strict hardware limits and 3-agent 3:0 PASS review criteria.

Integrity mode: development

## Requirements

### R1. SMR Removal, Frontend Alignment, and Multiphysics Generalization
Completely excise all SMR-Xu-Froment-Kinetics (K8) and Steam Methane Reforming specifics across all requirements, suggestions, frontend documentation, and codebase (including `smrKineticsSource.C`, Phase 5 K8 audit, Section 9.4.1 in `SUGGESTIONS_BACKEND.md`, `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 164 species bias, `COMMUNICATION_BACKEND_FRONTEND.md`, `SCOPE.md`, `backend/OPENFOAM_BACKEND_ATLAS.md`). Replace them with generic, data-driven multiphysics and configurable species chemistry interfaces, recording the update in the backend-frontend handover log in `COMMUNICATION_BACKEND_FRONTEND.md`.

### R2. Direct Code Integration of Surveyed Papers
Incorporate applicable methods from the 8 papers in `resources/papers/` directly into code modules:
- Paper 1 (`CADWatertightFineMeshing_2604.02141v1.pdf`): Topology-first B-Rep meshing, Jordan curve loop separation, and 1D boundary stitching in `step_parser.py` for watertight CAD import.
- Paper 2 (`Differtialble3DCAD_2110.01182v1.pdf`): Compiled CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$, geometric safety inequalities $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$, and adjoint chain rule interfaces.
- Paper 3 (`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`): $L^p$-metric tensor sizing $\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ and noise-free $P_2$ edge midpoint reconstruction in `mesh_engine.py` and `octree_hexcore.cpp`.
- Paper 4 (`MeshlessVoronoiOnGPU_3272127.3275092.pdf`): Pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`) with radius-of-security termination.
- Paper 5 (`VoronoiDiagrams_2404.18629v1.pdf`): Analytical 1st and 2nd derivatives of Voronoi/power diagram vertices via $A\mathbf{x}_c = b$ ($C^1$ volumes across topology flips).
- Paper 6 (`DiscreteGCL_1709.04788v4.pdf`): Exact 3D swept volume via Simpson's rule $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ and BDF2-DGCL $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$ in `backend/meshing/moving_mesh.py` (resolving Audit-H9).
- Paper 7 (`Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf`): Orthogonal symmetry plane connection ($\boldsymbol{\Delta} \parallel \mathbf{s}$, zero cross-diffusion) and Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vectors and stress tensors in `polymesh_io.py`, `difvm_topology.py`, and `case_generator.py`.
- Paper 8 (`FSI_Monolithic_2505.22920v1.pdf`): Monolithic 3D Eulerian FSI with $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$, Neo-Hookean solid, 3D geometric PLIC, and fluid reset $\mathbf{B} = \mathbf{I}$ for $\phi < 10^{-8}$.

### R3. Backend Architecture Synthesis and Super-Elements Integration
Incorporate applicable architecture and meshing elements from `SUGGESTIONS_BACKEND.md` (§10 & §11.2):
- Section 10 (`SuperElements_Db_115554.pdf`, Stromberg 2023): Implement `constexpr` 23-class super-element integration in `octree_hexcore.cpp`, asymmetric residual volume voting ($RV_{\text{missing}}^2 + RV_{\text{excess}}$), top-down DOF hierarchy, and $J_{\text{ENS}}$ / pyramid base check in `models.py` / `difvm_mesh.cpp`.
- Section 11.2 (Carbon's Architecture Synthesis): Establish unified C++20/23 core (mesher, FVM/FEM, LLVM Enzyme AD) with Rust orchestration; enforce strict C-ABI Structure-of-Arrays (SoA) data lake interfaces (`(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)`) with embedded expression engine (Rhai / SIMD bytecode) for generic boundary conditions and source terms; implement native binary container format `.mphys` (64-byte `MPHYS001` header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, FlatBuffers/Parquet footer `MPHYSEOF`) in `backend/native_rust/`; and provide backend C-ABI SoA zero-copy buffer export interfaces (renderers, shaders, and presentation display logic are strictly out of scope).

### R4. Backend-to-Frontend Interfaces (IPC, Fast IO) and Phase 5 Completion
Implement strictly backend-side input/output contracts, parsers, binary data descriptors/buffers, schemas, and JSON-RPC/NDJSON IPC endpoints required by `FRONTEND_REQUIREMENTS_ON_BACKEND.md`, and complete open Phase 5 audits. All frontend UI (`egui`), GPU renderers (`wgpu`), shaders, colormaps, viewports, and visual presentation logic are strictly out of scope and remain entirely owned by the frontend.
- Fast IO Extensions (`backend/native_rust/foam_fast_io`): Implement `boundaryField` parsing in `src/fields.rs` (`pub boundary_fields: HashMap<String, PatchFieldValues>`) supporting `uniform` and `nonuniform List<scalar|vector>` with fallback to patch `owner` cell values for `zeroGradient`/`calculated`, implement `list_time_dirs(case_path: &Path) -> Vec<TimeStepInfo>` with reconstructed vs processor status, and add `format binary;` header support in `FoamStream` (binary scalar/vector lists and `points`/`faces` via slice / mmap).
- IPC & Binary Buffer Contracts: Implement JSON-RPC 2.0 / NDJSON IPC (stdin/stdout or named pipe, diagnostics strictly on stderr, async `job_id`, clean cancellation) covering `system.hello`, `schema.get` (`ModuleRegistry.export_ui_schema()`), `geometry.import`/`geometry.patches.update`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`/`case.generate`, `solver.start`/`write_now`/`stop_graceful`/`abort`, and `results.list_times`/`list_fields`/`load`. Adhere to 7-element SI dimensions `[kg, m, s, K, mol, A, cd]`, binary geometry buffers (Paket 1: vertices, normals, triangles, face groups, audit JSON), binary mesh quality fields (Paket 2: skewness, non-orthogonality, aspect ratio, volume, wsrd weights, amips energy, folded faces), and live NDJSON telemetry streaming (Paket 4: `iter` events with residuals, Courant numbers, and `n_nan_cells`).
- Open Phase 5 Audits: Audit H5 (`polymesh_io.py`: multi-region export for `chtMultiRegionFoam`, `regionProperties`, `mappedWall`, `cyclic`), Audit H6 (`case_generator.py` & `registry.py`: runnable `0/` and `fvSolution` files for general solvers), and Audit M10 (`wsl_runner.py`: `shlex.quote`, exit code check, path safety, residual parsing).

### R5. Execution Constraints, Zero-Shortcut Policy, and Review Governance
- Strictly observe hardware limits: `OMP_NUM_THREADS=2` (max 4), `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Strictly sequential Pytest test execution (`.\\.venv\\Scripts\\python.exe -m pytest ...`).
- Test meshes must not exceed 30,000 cells. No HPC or heavy full-load benchmarks.
- Strictly enforce Zero-Shortcut-Policy: throw exceptions before crashes, no raw pointer leaks, strict bounds checks, no heuristic clamps, no swallowed exceptions.
- Mandatory 3-member review panel requiring unanimous 3:0 PASS per milestone:
  1. `1x Anti-Cheat & Forensik`: Forensic code-path verification enforcing Zero-Shortcut-Policy, detecting and rejecting stubs, dummy/mock buffers, silent fallbacks, tautological tests, heuristic clamps, remaining SMR-K8 hardcodings, or out-of-scope frontend UI/renderer code.
  2. `1x Mathematik & Paper-Treue`: Mathematical verification of exact formulas, conservation laws (Simpson/BDF2 DGCL, Super-Elements 23-class tables & $J_{\text{ENS}}$, Householder $\mathbf{R}$, $L^p$-metric sizing, Voronoi/CAD derivatives, PLIC/Eulerian FSI), and 7-element SI dimension vectors against the referenced papers and `SUGGESTIONS_BACKEND.md`.
  3. `1x Code-Qualität & Stabilität`: Memory, FFI, and C-ABI safety (zero raw-pointer leaks, strict bounds checks, fail-fast exceptions instead of swallowed errors), clean Rust/C++20/Python contracts, sequential pytest stability, and strict hardware throttling (`OMP_NUM_THREADS=2`, test meshes $\le 30{,}000$ cells).
