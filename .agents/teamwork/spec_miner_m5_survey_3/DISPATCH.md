## 2026-11-12T01:31:12Z
You are spec_miner_m5_survey_3.
Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_3
Authoritative request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Dispatch file: g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_3\DISPATCH.md

You MUST read ORIGINAL_REQUEST.md before starting work. Do NOT summarize or filter it.

Your mission:
Survey and extract exact requirements, schemas, binary buffer layouts, and endpoint definitions for:
1. Fast IO Extensions (backend/native_rust/foam_fast_io):
   - boundaryField parsing in src/fields.rs (pub boundary_fields: HashMap<String, PatchFieldValues>) supporting uniform and nonuniform List<scalar|vector> with fallback to patch owner cell values for zeroGradient/calculated.
   - list_time_dirs(case_path: &Path) -> Vec<TimeStepInfo> with reconstructed vs processor status.
   - format binary; header support in FoamStream (binary scalar/vector lists and points/faces via slice / mmap).
2. IPC & Binary Buffer Contracts (FRONTEND_REQUIREMENTS_ON_BACKEND.md & COMMUNICATION_BACKEND_FRONTEND.md):
   - JSON-RPC 2.0 / NDJSON IPC (stdin/stdout or named pipe, diagnostics strictly on stderr, async job_id, clean cancellation):
     - Endpoints: system.hello, schema.get (ModuleRegistry.export_ui_schema()), geometry.import/geometry.patches.update, mesh.generate, job.cancel, materials.list, case.validate/case.generate, solver.start/write_now/stop_graceful/abort, and results.list_times/list_fields/load.
     - 7-element SI dimensions validation: [kg, m, s, K, mol, A, cd].
     - Binary geometry buffers (Paket 1: vertices, normals, triangles, face groups, audit JSON).
     - Binary mesh quality fields (Paket 2: skewness, non-orthogonality, aspect ratio, volume, wsrd weights, amips energy, folded faces).
     - Live NDJSON telemetry streaming (Paket 4: iter events with residuals, Courant numbers, and n_nan_cells).
3. Strict Backend Boundary Verification:
   - Ensure all frontend UI (egui), GPU renderers (wgpu), shaders, colormaps, viewports, and visual presentation logic are strictly out of scope and remain entirely owned by the frontend.

Maintain progress in progress.md in your state folder.
Produce a comprehensive report at g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_3\report.md and handoff.md.
When finished, send a message to the caller with a summary of findings and report path.
