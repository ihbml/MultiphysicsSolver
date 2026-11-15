# Scope: CFDSolver Multiphysics Generalization, Paper Integration & Backend Interface Contracts (Generation 10)

## Architecture & System Overview
This scope executes the completion of Phase 5 (Milestone M5D) and final system acceptance (Milestone M6_FINAL).
Prior milestones M1_PHASE1 through M4_PHASE4, M5A, M5B, and M5C are fully certified with unanimous 3:0 PASS.

Per updated user directives:
1. **NO HARDWARE RESTRICTIONS**: Ansys is no longer running. The earlier thread restrictions (`OMP_NUM_THREADS=2`, etc.) and the 30,000-cell mesh cap are lifted. Full CPU threads and full-scale high-resolution meshes may be used.
2. **STRESS TEST FIXTURES**: M6_FINAL includes a comprehensive stress test covering both user CAD fixtures (`BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `TestCAD.obj`, `TestCADSTPAP214.stp`) and complex industrial geometries from `OpenFOAM_base/openfoam/tutorials/resources/geometry` (handling `.stl.gz` and `.obj.gz` transparently).
3. **FINISH THROUGH M6_FINAL**: Complete M5D (3:0 PASS) -> Complete M6_FINAL (3:0 PASS) -> Final report to Sentinel.

All frontend UI (`egui`), GPU renderers (`wgpu`), shaders, and display viewports remain strictly out of scope and owned by the frontend.

---

## Active & Upcoming Features
| # | Feature | Description | Milestone | Status |
|---|---------|-------------|-----------|--------|
| F64 | Fast IO Extensions | In `backend/native_rust/foam_fast_io`, `boundaryField` parsing in `fields.rs` (`uniform` and `nonuniform List<T>` with patch owner cell fallback for `zeroGradient`/`calculated`); `list_time_dirs` sorting numeric time directories with reconstructed vs processor status; `format binary;` header support in `FoamStream` and binary scalar/vector decoders. | M5D | In-Progress (Code written) |
| F65 | Backend JSON-RPC 2.0 / NDJSON IPC Contracts & Telemetry | Implement JSON-RPC 2.0 / NDJSON IPC backend service covering all 16 endpoints (`system.hello`, `schema.get`, `geometry.import`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`/`case.generate`, `solver.*`, `results.*`); strict 7-element SI dimensions validation `[kg, m, s, K, mol, A, cd]`; Paket 1 binary geometry buffers; Paket 2 binary mesh quality fields; Paket 4 live NDJSON telemetry streaming (`iter` events with Courant numbers and NaN monitors); strict stderr diagnostics separation; zero frontend UI code. | M5D | In-Progress |
| F66 | Full System Acceptance & Industrial CAD/OpenFOAM Stress Testing | Full regression suite execution across all modules; comprehensive high-performance stress test on user CAD fixtures and OpenFOAM tutorial industrial geometries (`.stl.gz`, `.obj.gz`); unanimous 3:0 PASS gate sign-off across Anti-Cheat, Math Critic, and Code Quality Reviewer. | M6_FINAL | DONE (3:0 UNANIMOUS PASS) |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1_PHASE1 | Ehrlichkeit, Test-Integrität & Doku-Archiv | Features F37, F38, F39 | none | DONE (3:0 UNANIMOUS PASS) |
| M2_PHASE2 | Build & Plattform (OpenMP) | Feature F40 | none | DONE (3:0 UNANIMOUS PASS) |
| M3_PHASE3 | Abkürzungen & Heuristiken entfernen | Features F41, F42, F43, F44 | M1_PHASE1, M2_PHASE2 | DONE (3:0 UNANIMOUS PASS) |
| M4_PHASE4 | Mathematik & Topologie korrigieren | Features F45, F46, F47, F48, F49, F50 | M2_PHASE2, M3_PHASE3 | DONE (3:0 UNANIMOUS PASS) |
| M5A | SMR Excision, Multiphysics Chemistry & OpenFOAM Audits | Features F51, F52, F53, F54 | M1_PHASE1, M4_PHASE4 | DONE (3:0 UNANIMOUS PASS) |
| M5B | Paper Integration (Meshing, DGCL H9, Symmetry, FSI) | Features F55, F56, F57, F58, F59, F60, F61, F61b | M4_PHASE4, M5A | DONE (3:0 UNANIMOUS PASS) |
| M5C | Architecture Synthesis & Super-Elements Integration | Features F62, F63 | M5B | DONE (3:0 UNANIMOUS PASS) |
| M5D | Backend Fast IO & JSON-RPC/NDJSON IPC Contracts | Features F64, F65 | M5A, M5C | DONE (3:0 UNANIMOUS PASS) |
| M6_FINAL | Full System Acceptance & Industrial Stress Testing | Feature F66 | M1–M5D | DONE (3:0 UNANIMOUS PASS) |
