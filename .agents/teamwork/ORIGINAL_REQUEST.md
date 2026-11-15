# Original User Request

## Initial Request — 2026-11-12T01:24:42Z

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


## Follow-up — 2026-11-13T23:09:29Z

Quota has reset and the server restarted. Please resume execution from Milestone M5B (`F55`–`F61`, noting that `M1_PHASE1`–`M4_PHASE4` and `M5A` are already `DONE` with 3:0 UNANIMOUS PASS, and `worker_m5b` had not yet modified code before the 429 stop).

In addition to the approved task prompt, the user explicitly requested incorporating the newly added backend suggestions and cheat sheets (`SUGGESTIONS_BACKEND.md` §12 and `resources/reference_spickzettel/INDEX.md` Category 7b & Snippets 12–14, Stand 2026-11-13) into M5B/M5C/M5D where applicable:
1. **Six FVM/GPU Numerics & HPC Contracts (`SUGGESTIONS_BACKEND.md` §12.2 / `MesherAndSolverSol61Max` + `SolverGemini4ArgonHighCUDA.md` + `MesherAndSolverStep5Preview`):**
   - `base-all` contract across all 4 Hilbert quadrants (`Seg 0..3`) and compact 6% `MeshCorrectionSoA` (`seg1_corr_offset`, `seg3_corr_offset`) with strictly separated `_x`/`_y`/`_z` arrays (forbidding 24-byte `double3` structs).
   - Linear-exact plane intersection $\beta_f = \frac{(\mathbf{x}_f-\mathbf{x}_P)\cdot\mathbf{S}_f}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\alpha_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\mathbf{m}_f^{\text{skew}} = \mathbf{x}_f - (\mathbf{x}_P + \beta_f\mathbf{d}_f)$, $\mathbf{k}_f^{\text{corr}} = \mathbf{S}_f - \alpha_f\mathbf{d}_f$, and explicit `FaceClass::invalid` rejection for $\cos\theta_f \le 10^{-8}$ or non-finite values (no silent `1e-15` clamps).
   - Gradient-free convective upwind deferred correction in Class 0 (M-matrix dominance without loading $\nabla\phi$), continuity diagonal correction, and diagonal-only CSR reset.
   - Atomic-free `CellFaceCSR`-gather pattern (no ghost-row out-of-bounds writes, Enzyme AD warp-atomic-stall free).
   - Full-stencil inner/halo classification (`Seg 1` only when $P, N$ and both gradient stencils are strictly local) + 2-stage halo contract (`halo_phi` + `halo_grad`).
   - 3-Map `MeshErrorMap` separation (`GeometryMap`, `SolutionIndicator`, `ErrorEstimator`), SPD Cholesky metric factorization $\mathcal{M}(\mathbf{x}) = \mathbf{L}\mathbf{L}^T + \epsilon\mathbf{I} \succ 0$, and tangential CAD projection + 1-step Newton re-projection onto $\psi(\mathbf{x})=0$ (Snippet 14).
2. **Reactive Multiscale Chemistry & Coupling Contracts (`SUGGESTIONS_BACKEND.md` §12.3 / `Fable52HighChemSolver` + `BlackglasIChemSugg.txt` + `ChatGPTSol61SearchMaxChemSugg.txt`):**
   - Operator splitting + DLBFoam active-cell dynamic load balancing ($\|\dot{\boldsymbol{\omega}}_i\| > \varepsilon_{\text{react}}$), AoT / analytical sparse Jacobian $J_{kj} = \frac{\partial\dot{\omega}_k}{\partial Y_j}$ interfaces, and explicit `consistent` (intensive) vs `conservative` (extensive) coupling contracts.
   - Note: Also clean up the remaining passing mention of `smrKineticsSource.C` in `SUGGESTIONS_BACKEND.md:630` under R1 so zero SMR references remain anywhere.

Keep all existing constraints active: `OMP_NUM_THREADS=2` (max 4), `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`, sequential pytest (`.\\.venv\\Scripts\\python.exe -m pytest ...`), grids $\le 30{,}000$ cells, zero frontend UI/renderer code, Zero-Shortcut-Policy, and unanimous 3:0 PASS per milestone (`1x Anti-Cheat & Forensik`, `1x Mathematik & Paper-Treue`, `1x Code-Qualität & Stabilität`).


## Follow-up — 2026-11-14T00:49:06Z

USER DIRECTIVE (Pause Point): As soon as Milestone M5D completes and achieves its 3:0 UNANIMOUS PASS gate certification (completing all of M5: M5A, M5B, M5C, M5D), STOP / PAUSE immediately BEFORE starting M6_FINAL and report M5D 3:0 PASS completion to me. Do NOT proceed into M6_FINAL yet — we will commit, push to Git, and pause after M5D passes.


## Follow-up — 2026-11-14T22:58:16Z

Resume Teamwork execution after server restart and complete Milestone M5D (`F64` + `F65`) AND Milestone M6_FINAL (`F66`) to finish the mesher and full backend suite completely.

UPDATED USER DIRECTIVES (superseding earlier Ansys throttling and M5D pause):
1. **NO HARDWARE RESTRICTIONS — FULL PERFORMANCE UNLOCKED**: Ansys is NO LONGER running. Lift the `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2` limits and lift the `<= 30,000 cells` mesh cap. You may use full CPU threads and full-scale high-resolution meshes.
2. **FULL-PERFORMANCE STRESS TEST WITH OPENFOAM & USER CAD FIXTURES (for M6_FINAL / stress validation)**:
   - Run a comprehensive full-load stress test using BOTH:
     a) The user's CAD fixtures (`resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `resources/CADTestSamples/TestCAD.obj`, `TestCADSTPAP214.stp`)
     b) Representative complex industrial geometries from `G:\Datein\Projects\CFDSolver\OpenFOAM_base\openfoam\tutorials\resources\geometry` (130 files, mostly gzip-compressed `.stl.gz` and `.obj.gz` like `motorBike.obj.gz`, `DTC-scaled.stl.gz`, `NACA0012.obj.gz`, `buildings.obj.gz`, `cyclone.stl.gz`, `flange.stl.gz`, plus uncompressed `fsi-bridge1.obj`, `building_wtc2.obj` — handle `.gz` decompression cleanly in the test/pipeline without modifying or requiring manual extraction by the user).
3. **FINISH THROUGH M6_FINAL**: Remove the earlier pause-before-M6_FINAL flag. Complete M5D (with 3:0 PASS) and then execute M6_FINAL (full regression + full-performance OpenFOAM/CAD stress test + final 3:0 UNANIMOUS PASS certification across `1x Anti-Cheat & Forensik`, `1x Mathematik & Paper-Treue`, `1x Code-Qualität & Stabilität`). Once M6_FINAL is certified 3:0 PASS, report completion so I can commit & push to Git and pause.

Continue from current state: M5D has Feature F64 (`foam_fast_io`) implemented; finish Feature F65 (IPC service + tests) and proceed through M5D gate -> M6_FINAL.


## Follow-up — 2026-11-14T23:16:31Z

USER ADDITION for M5D (`backend/core/materials.py` & `materials.list`):
The user requested extending `Material` and `MaterialDatabase` in `backend/core/materials.py` so custom materials AND arbitrary/extended physical properties can be added effortlessly:
1. **Extensible `extra_properties: Dict[str, Any]` passthrough**: When loading `.json` files from `resources/materials_db/` (or via `Material`), capture any additional/custom JSON keys (including custom `unit_si` overrides) into `extra_properties` and serialize them cleanly in `to_dict()` / `materials.list` instead of dropping unknown keys.
2. **Categorized Physical Properties Schema / Optional Fields**: Support optional structured property groups or canonical optional fields covering Surface (`specific_surface_area`, `surface_energy`, `surface_tension`), Electrical (`electrical_conductivity`, `electrical_resistivity`, `permittivity`, `dielectric_constant`, `dielectric_strength`, `seebeck_coefficient`, `capacitance`, etc.), Magnetic (`magnetic_permeability`, `curie_temperature`, `magnetocaloric_coefficient`, `hall_coefficient`, etc.), Mechanical (`youngs_modulus`, `bulk_modulus`, `shear_modulus`, `poissons_ratio`, `yield_strength`, `tensile_strength`, `compressive_strength`, `fracture_toughness`, `hardness`, `speed_of_sound`, `mass_diffusivity`, `surface_roughness`, `friction_coefficient`, `acoustical_absorption`, etc.), Optical (`refractive_index`, `emissivity`, `absorptivity`, `reflectivity`, `transmittance`), Thermal (`thermal_expansion_coeff`, `thermal_diffusivity`, `melting_point`, `boiling_point`, `critical_temperature`, `triple_point`, `glass_transition_temperature`, `heat_of_vaporization`, `vapor_pressure`, `flash_point`), Manufacturing (`castability`, `machinability_rating`), and Radiological (`attenuation_coefficient`, `half_life`, `neutron_cross_section`, `specific_activity`) — stored cleanly with SI units (either as optional typed fields or categorized `properties: Dict[str, Dict[str, Any]]` + `extra_properties` passthrough) and a `MaterialDatabase.add_material(..., persist: bool = False)` helper so new materials and new properties can be added anytime via JSON or API.

Keep all M5D tests and the 3:0 PASS gate criteria active.


## Follow-up — 2026-11-14T23:16:50Z

USER ADDITION for M5D (`backend/core/materials.py` & `materials.list`):
The user requested extending `Material` and `MaterialDatabase` in `backend/core/materials.py` so custom materials AND arbitrary/extended physical properties can be added effortlessly:
1. **Extensible `extra_properties: Dict[str, Any]` passthrough**: When loading `.json` files from `resources/materials_db/` (or via `Material`), capture any additional/custom JSON keys (including custom `unit_si` overrides) into `extra_properties` and serialize them cleanly in `to_dict()` / `materials.list` instead of dropping unknown keys.
2. **Categorized Physical Properties Schema / Optional Fields**: Support optional structured property groups or canonical optional fields covering Surface (`specific_surface_area`, `surface_energy`, `surface_tension`), Electrical (`electrical_conductivity`, `electrical_resistivity`, `permittivity`, `dielectric_constant`, `dielectric_strength`, `seebeck_coefficient`, `capacitance`, etc.), Magnetic (`magnetic_permeability`, `curie_temperature`, `magnetocaloric_coefficient`, `hall_coefficient`, etc.), Mechanical (`youngs_modulus`, `bulk_modulus`, `shear_modulus`, `poissons_ratio`, `yield_strength`, `tensile_strength`, `compressive_strength`, `fracture_toughness`, `hardness`, `speed_of_sound`, `mass_diffusivity`, `surface_roughness`, `friction_coefficient`, `acoustical_absorption`, etc.), Optical (`refractive_index`, `emissivity`, `absorptivity`, `reflectivity`, `transmittance`), Thermal (`thermal_expansion_coeff`, `thermal_diffusivity`, `melting_point`, `boiling_point`, `critical_temperature`, `triple_point`, `glass_transition_temperature`, `heat_of_vaporization`, `vapor_pressure`, `flash_point`), Manufacturing (`castability`, `machinability_rating`), and Radiological (`attenuation_coefficient`, `half_life`, `neutron_cross_section`, `specific_activity`) — stored cleanly with SI units (either as optional typed fields or categorized `properties: Dict[str, Dict[str, Any]]` + `extra_properties` passthrough) and a `MaterialDatabase.add_material(..., persist: bool = False)` helper so new materials and new properties can be added anytime via JSON or API.

Please convey this requirement to your active worker for M5D and ensure unit tests in `tests/test_fast_io_and_ipc_m5d.py` cover these extended materials capabilities.


## Follow-up — 2026-11-14T23:19:37Z

CLARIFICATION on `Material` properties:
All extended/categorized properties MUST be strictly optional (`None` / omitted by default) — a material does NOT need all properties populated to be valid in `MaterialDatabase`. Only the specific properties required by the active solver/physics modules need to be present when validating a case (e.g., provide `Material.missing_properties(required: Sequence[str]) -> List[str]` so `case.validate` checks only what the active program/modules actually require).


## Follow-up — 2026-11-14T23:19:49Z

USER CLARIFICATION on `Material` properties:
All extended/categorized properties MUST be strictly optional (`None` / omitted by default) — a material does NOT need all properties populated to be valid in `MaterialDatabase`. Only the specific properties required by the active solver/physics modules need to be present when validating a case (e.g., provide `Material.missing_properties(required: Sequence[str]) -> List[str]` so `case.validate` checks only what the active program/modules actually require).

Please ensure `Material` implements this `missing_properties` helper and that properties default to `None`.
