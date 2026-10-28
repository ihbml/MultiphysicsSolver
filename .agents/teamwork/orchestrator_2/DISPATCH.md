# Dispatch Log

## 2026-10-28T00:23:34Z
You are the Project Orchestrator for the following mission:

## Mission
Develop the Industrial In-Memory Multiphysics Mesher Backbone (Rust & C++ with Python Bridge) for 2D and 3D geometries, moving mesh, non-conformal interfaces, and hybrid FVM-SPH coupling.

## Working Directory & Coordination Files
- Your assigned working directory: `G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_2`
- You must create and continuously maintain `BRIEFING.md` and `progress.md` in your working directory.
- The authoritative user request is in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (see the section `## Follow-up — 2026-10-28T00:22:07Z`).
- Target codebase directory: `G:\Datein\Projects\CFDSolver\backend\meshing`
- Full project root: `G:\Datein\Projects\CFDSolver`

## Key Requirements & Scientific Foundations
1. **DiFVM (Du et al., 2026):** Vectorized Graph-Based Finite Volume Solver for Differentiable CFD, arXiv:2603.15920.
2. **Eulerian SPH-FVM (Wang, Haidn & Hu, 2025):** Boundary Condition from FVM into Eulerian SPH, Comput. Phys. Commun. 307, 109429.
3. **OPMIS (Zhang, Ming, Wang & Liang, 2025):** Adaptive SPH-FVM Method with Optimized Particle-Mesh Interpolation, Int. J. Multiphase Flow 189, 105256.
4. **DistMesh (Persson & Strang, 2004):** A Simple Mesh Generator in MATLAB, SIAM Review 46(2), 329–345.
5. **Reference templates & code snippets:** `resources/reference_spickzettel/LMArena/` (notably `2/backend` for Rust core & `1/native` for C++20 core).
6. **CAD Samples:** `resources/CADTestSamples/TestCAD.*` and `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.*`.

## Scope & Deliverables
- **R1. Geometry-, SDF- & Feature-Engine (2D & 3D):** Continuous SDF and normal gradients from analytical primitives and CAD meshes (`backend/geometry/`). Filter CAD sketch/guide lines. Curvature & proximity refinement.
- **R2. In-Memory Unstructured Meshing Core & Boundary Layers (Rust & C++):** DistMesh force equilibrium / Bowyer-Watson Delaunay. Inflation & buffer layers for y+ ~ 1. RCM bandwidth reduction.
- **R3. DiFVM Graph-Topology & SoA:** owner, neighbour, face_areas (S_f), face_centres, cell_centres, cell_volumes, orthogonal projection delta_f, over-relaxed correction k_f. Error-map (skewness, non-orthogonality, aspect ratio) in SoA. Compatibility with `backend/native_rust/foam_fast_io/` FoamMesh and C-ABI (`extern "C"`).
- **R4. Hybrid Eulerian SPH-FVM Coupling:** div r < 0.75 d incomplete support detection, boundary vector A_iS = -sum V_j grad W_ij (0th-order consistency < 10^-10), OPMIS partitioning & moment-reproducing Taylor interpolation (P2M / M2P).
- **R5. Moving Mesh & Dynamic Re-Adaption:** ALE boundary node displacement with interior Laplacian smoothing, degenerate cell detection and remeshing trigger.
- **R6. Python-Bridge & Transparent Fallback:** `backend/meshing/mesh_engine.py` for testing and frontend when native libraries are not compiled.

## Requested Team & Verification Protocol
- Full teamwork pipeline with dedicated Critic / Adversarial Validator.
- Acceptance criteria: Strict positive volumes, Gauss divergence sum < 10^-11, delta_f + k_f == S_f, SPH wall consistency < 10^-10, exact moment reproduction for linear fields, error map in `mesh.error_map`, robust meshing of unit cube and BladeFullRev1CFD, 100% pass on `tests/test_mesher.py` (>= 10 unit & E2E tests), and all repository regression tests passing (> 240 tests).
- When all deliverables and acceptance criteria are satisfied and certified by your Critic/Validator, send your final victory report back to the Sentinel.

## 2026-10-28T00:56:40Z
URGENT ARCHITECTURAL DIRECTIVE FROM USER & SENTINEL:

The user has reviewed current code generation and requested:
"Gibt es ein Grund warum er das in Python macht? Wenn nicht dann gerne die performantere Version benutzen"

Mandate for the Orchestrator, Workers, and Reviewers:
1. Python is ONLY for the top-level test runner, CLI wrapper, and transparent fallback bridge.
2. The primary compute-heavy meshing engine MUST be implemented natively in **Rust** (or C++20) for maximum performance:
   - High-performance SDF / Level-Set queries over analytical bodies and 3D CAD meshes (AABB tree, Rayon multi-threading).
   - DistMesh iterative force-equilibrium and Bowyer-Watson / Delaunay triangulation (refer directly to `resources/reference_spickzettel/LMArena/2/backend/src/mesh.rs` and `LMArena/1/native/src/mesh.cpp`).
   - SPH neighbor search, virtual boundary closure vectors A_iS, and OPMIS matrix inverses.
3. Expose the native Rust/C++ kernel via standard C-ABI (`extern "C"`) so `backend/meshing` Python bridge loads the native shared library for zero-overhead execution while keeping all existing and new tests passing.

Please adjust your milestone execution immediately: prioritize and build out the native Rust/C++ engine, integrate it via C-ABI/ctypes/cffi with the Python bridge, and verify with tests. This requirement is now recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.

## 2026-10-28T00:58:10Z
URGENT ADDITIONAL USER DIRECTIVE ON PERFORMANCE TOLERANCE (0.1% RULE):

The user has explicitly specified:
"Also C++ können sie auch benutzen oder auch Python aber nur, wenn die Rechenzeit nur um 0,1% länger dauert"

Strict Implication for Orchestrator, Workers, Reviewers, and Auditors:
- Python is STRICTLY PROHIBITED for any loop or numerical calculation where its overhead exceeds 0.1% relative to compiled C++ / Rust. Since native compiled code is typically 10x to 100x faster than interpreted/pure Python loops, all core compute operations (geometry/SDF evaluation, triangle projections, DistMesh Delaunay triangulation and spring force relaxation, inflation extrusion, RCM reordering, SPH neighbor search and divergence, virtual wall vector A_iS, OPMIS Taylor matrix solving, and ALE Laplacian smoothing) MUST be compiled in C++20 or Rust.
- C++20 and Rust are both explicitly approved for high-performance implementation.
- Python is ONLY permitted for high-level CLI routing, configuration parsing, test orchestration, and ctypes/C-ABI glue to the native library.
- Ensure all workers and sub-orchestrators adhere to this exact <= 0.1% rule. Recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.

## 2026-10-28T01:01:51Z
HOST TOOLCHAIN DISCOVERY & COMPILATION INSTRUCTIONS:

Microsoft Visual Studio 2022 Community is confirmed installed and available on the host:
- MSVC C++20/C++23 Compiler (`cl.exe`): `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl.exe`
- MSVC x64 Environment Setup: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`
- CMake: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

Notes for Workers & Implementers:
1. Rust is not installed on this host system.
2. Therefore, **Modern C++20** is the designated native implementation language to achieve maximum performance and satisfy the user's strict 0.1% overhead rule.
3. Build the native core in `backend/meshing/native/` using MSVC / CMake into a Windows DLL (e.g. `cfd_mesher_native.dll`), exposing C-ABI (`extern "C"`) functions.
4. Load the DLL via `ctypes` in `backend/meshing/mesh_engine.py` (and relevant bridge modules) so that high-performance C++20 execution is seamless, zero-overhead, and passes all tests.

This information is also recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.

## 2026-10-28T01:29:49Z
NEW USER REQUIREMENT: CONJUGATE HEAT TRANSFER (CHT) & COUPLED MULTI-REGION GEOMETRY

The user explicitly requested:
"Ah und es sollte möglich sein CHT (Hea Transfer) über coupled geometry zu verdwenden, das kann im Mesher sein oder in dem dann CAD Programm"

Mandate for the Mesher Architecture (Milestones M2 / M3):
1. **Multi-Region Meshing Support:**
   - Partition and mesh multiple adjacent domains (e.g. Fluid zone $\Omega_F$ and Solid zone $\Omega_S$, e.g. turbine blade cooling channels, heat exchangers).
   - Cell tagging: Store `region_id` / `zone_name` in the DiFVM SoA graph topology (`cell_regions`).
2. **Coupled Interface & Baffle Identification:**
   - Extract and classify the shared fluid-solid interface $\Gamma_{FS} = \partial \Omega_F \cap \partial \Omega_S$.
   - Boundary patch classification as `coupledWall` / `mappedWall` matching OpenFOAM multi-region CHT conventions (`chtMultiRegionFoam`).
   - Support conformal twin-faces (direct face-to-face flux transfer) and non-conformal interfaces (AMI/GGI area-weighted conservative interpolation) to guarantee temperature and heat flux continuity ($T_F = T_S, -k_F \nabla T_F \cdot \mathbf{n} = -k_S \nabla T_S \cdot \mathbf{n}$).

Please incorporate these CHT multi-region contracts into your data structures (`models.py`, C++20 core, and `PROJECT.md`). Recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.

## 2026-10-28T01:34:04Z
PRIORITY MANDATE: CHECKPOINTING, DOCUMENTATION TRACEABILITY & FEATURES 22-23

1. **Token Budget & Atomic Checkpointing Protocol:**
   - 5h budget has ~29% remaining.
   - Strictly enforce atomic checkpointing at milestone boundaries: ensure each milestone produces clean, self-contained, compilable, and passing code so progress is indelibly preserved.

2. **Permanent Documentation & Traceability Mirroring:**
   - All specifications (`spec_report.md`, `m1_remediation_plan.md`), architecture decision records (ADRs), mathematical derivations, sign convention analyses, and critic audit reports generated inside `.agents/teamwork/` MUST be consolidated and mirrored into the project repository under `backend/meshing/docs/` (or `docs/mesher/`).
   - Repository documentation must provide complete mathematical traceability for all features.

3. **Approved Features 22 & 23 to Incorporate:**
   - **Feature 22 (Rotational/Cyclic Periodic Symmetry):** For turbomachinery CFD (`BladeFullRev1CFD`), support cyclic boundary face pairs with rotation matrix mapping $\mathbf{x}_{\text{target}} = \mathbf{R}_z(\theta) \mathbf{x}_{\text{source}}$ and rotational transformation of area vectors $\mathbf{S}_f$ and fluxes.
   - **Feature 23 (Boundary Layer Pinching & Layer Termination):** Detect sharp corners/trailing edges and narrow gaps where boundary layer extrusion would self-intersect or invert ($V_c \le 0$), smoothly tapering and terminating layer growth.

Incorporate these rules and features into `PROJECT.md` and instruct your workers/docs agents accordingly. Recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.

## 2026-10-28T01:35:20Z
USER CLARIFICATION ON RUN CONTINUITY & FULL-CAPACITY EXECUTION:

The user explicitly clarified:
"Also er kann das schon ausschöpfen, nur das der Lauf nicht abbricht"

Directives for Orchestrator:
1. **Full Capacity Delivery:** Do NOT artificially hold back or throttle work. Drive implementation vigorously at full capacity across M1, M2, and M3 to deliver the native C++20 engine, CHT multi-region, cyclic symmetry, and SPH-FVM coupling.
2. **Zero Abort Risk:** The core requirement is simply to prevent an uncontrolled termination. Maintain clean, atomic milestone checkpoints so every stage is compilable, all tests pass, and the workspace remains completely stable and instantly resumable.

Recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`. Proceed with full confidence.

## 2026-10-28T02:00:09Z
NEW USER DIRECTIVE: FRONTEND & DECLARATIVE MESHING CONFIGURATION

The user explicitly requested:
"Kann das einstellbar gemacht werden, also das ich einstellen kann, wie groß die Auflösung sein soll und die Wachstumsrate und andere Input Parameter? Das wird es dann auch für das Frontend brauchen"

Mandate for Python Models (`models.py`) and Native C++20 Core:
1. **Declarative Configuration Schema (`models.py`):**
   - Provide `MeshingConfig`, `InflationConfig`, `CurvatureSizingConfig`, `QualityConfig` dataclasses with `.to_dict()` and `.from_dict()` for frontend serialization.
   - Sizing parameters: `min_size` ($h_{\min}$), `max_size` ($h_{\max}$), `growth_rate` (default 1.2).
   - Curvature/Proximity: `curvature_normal_angle` (e.g. 6.0 deg), `cells_per_gap` (default 1-2).
   - Inflation layers: `n_layers` (default 10), `first_layer_height`, `layer_growth_rate` (default 1.05), `transition_ratio` (default 0.1), `pinching` (bool).
   - Quality gates: `min_orthogonal_quality` (default 0.2), `max_skewness` (default 0.7).
2. **C++20 Native C-ABI Config Struct (`MeshingConfigC`):**
   - Provide corresponding C++20 `struct MeshingConfig` and a standard C-ABI `MeshingConfigC` struct so the Python bridge and frontend can pass arbitrary user parameters directly into the native C++ DLL without re-compilation.

Recorded in `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`. Ensure this is incorporated into Milestone M2 deliverables.
