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

## Verification Resources
- Existing Pytest test suite (`.\\.venv\\Scripts\\python.exe -m pytest ...`).
- CAD Fixtures: `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `resources/CADTestSamples/TestCAD.obj`, `TestCADSTPAP214.stp`.

## Acceptance Criteria

### Generalization & Frontend Alignment
- [ ] Zero references to `SMR-Xu-Froment-Kinetik`, `smrKineticsSource.C`, or hardcoded SMR reaction kinetics remain in code, configurations, documentation, or `FRONTEND_REQUIREMENTS_ON_BACKEND.md`.
- [ ] Handover changelog in `COMMUNICATION_BACKEND_FRONTEND.md` documents the generic solver interfaces.
- [ ] Generic multiphysics and chemistry interfaces instantiate and execute successfully without SMR dependencies.

### Paper Integration & Phase 5 Audits
- [ ] Moving mesh swept volume in `moving_mesh.py` satisfies discrete geometric conservation law (DGCL) via Simpson's rule and BDF2 time stepping (Audit-H9 resolved).
- [ ] Householder reflection tensor $\mathbf{R}$ is implemented and unit-tested for vectors and rank-2 tensors on symmetry boundaries in `polymesh_io.py`, `difvm_topology.py`, and `case_generator.py`.
- [ ] $L^p$-metric sizing in `mesh_engine.py` / `octree_hexcore.cpp` generates valid metric tensors without heuristic clamps.
- [ ] Watertight CAD parser handles topological loop stitching and Jordan curve separation on provided STEP fixtures.
- [ ] Multi-region export (Audit-H5), runnable general case generation (Audit-H6), and WSL runner security (Audit-M10) pass validation.

### Architecture Synthesis & Super-Elements
- [ ] Super-elements integration (`SUGGESTIONS_BACKEND.md` §10) evaluated and implemented where applicable (e.g. `constexpr` 23-class super-elements, residual volume voting, and $J_{\text{ENS}}$ / pyramid checks in `octree_hexcore.cpp` / `models.py` / `difvm_mesh.cpp`).
- [ ] Architecture synthesis components from `SUGGESTIONS_BACKEND.md` §11.2 (C-ABI SoA data lake interfaces, expression engine foundations for generic boundary/source terms, and `.mphys` container specification/stubs in `backend/native_rust/`) are implemented and validated.

### Frontend IPC & Fast IO
- [ ] `foam_fast_io` parses `boundaryField` (uniform and nonuniform scalar/vector with owner cell fallback), lists sorted time directories with reconstruction status, and parses `format binary;` OpenFOAM streams.
- [ ] Local IPC exposes JSON-RPC 2.0 / NDJSON commands with async `job_id`, diagnostics strictly on stderr, and 7-element SI dimension validation.
- [ ] Flat binary buffers for geometry (Paket 1) and mesh quality fields (Paket 2) match documented binary layouts.
- [ ] Live telemetry streaming via NDJSON emits formatted `iter` progress events with Courant numbers and NaN cell monitors.
- [ ] Strictly backend-side boundaries maintained: no frontend UI (`egui`), shaders, `wgpu` renderers, or viewport display code introduced.

### Quality, Stability, and Review Governance Compliance
- [ ] All tests in the pytest suite pass sequentially under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2` with all test meshes $\le 30{,}000$ cells.
- [ ] Each milestone receives an unanimous 3:0 PASS from the 3-member review panel:
  - [ ] `1x Anti-Cheat & Forensik` confirms zero shortcuts, no dummy/mock buffers, no tautological tests, no heuristic clamps, no remaining SMR-K8 code, and strict backend boundary.
  - [ ] `1x Mathematik & Paper-Treue` confirms exact mathematical formulation, discrete conservation (DGCL), and valid SI dimension vectors against the papers.
  - [ ] `1x Code-Qualität & Stabilität` confirms memory safety, FFI/C-ABI boundaries, sequential test execution, and hardware limits compliance.
- [ ] Zero-shortcut compliance verified: no heuristic clamps, no swallowed exceptions, bounds checked.
