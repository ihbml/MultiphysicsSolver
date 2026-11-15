## 2026-11-14T00:54:40Z
You are worker_m5d, a teamwork_worker implementing Milestone M5D: Backend Fast IO & JSON-RPC/NDJSON IPC Contracts (Features F64 & F65).

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5d

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md (MANDATORY: read before starting work)
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5d\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\FRONTEND_REQUIREMENTS_ON_BACKEND.md
6. g:\Datein\Projects\CFDSolver\COMMUNICATION_BACKEND_FRONTEND.md
7. g:\Datein\Projects\CFDSolver\SUGGESTIONS_BACKEND.md
8. Prior handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md

Tasks:
1. Feature F64 (Fast IO Extensions):
   - In backend/native_rust/foam_fast_io/src/fields.rs, parser.rs, lib.rs (and Python bindings/polymesh_io.py):
     - Implement boundaryField dictionary parsing (uniform and nonuniform List<scalar|vector>, fallback to owner cell values for zeroGradient/calculated).
     - Implement list_time_dirs(case_path) sorting numeric times and tagging reconstructed vs processor_only.
     - Implement format binary; header support in FoamStream and binary list decoders.
2. Feature F65 (Backend JSON-RPC 2.0 / NDJSON IPC Contracts & Telemetry):
   - In backend/ipc/:
     - Implement JSON-RPC 2.0 service over stdin/stdout, pipes, or sockets with strict stderr logging separation.
     - Implement handlers for all 16 endpoints (system.hello, schema.get, geometry.import, mesh.generate, job.cancel, materials.list, case.validate, case.generate, solver.start, solver.pause, solver.resume, solver.stop, solver.status, results.list_steps, results.get_field, results.get_probe).
     - Implement strict 7-element SI dimensions validation [kg, m, s, K, mol, A, cd].
     - Implement binary buffer export contracts for Paket 1 (Geometry Buffer) and Paket 2 (Mesh Quality Field Buffer).
     - Implement Paket 4 live NDJSON telemetry streaming for iter events.
   - Maintain strict backend boundary: ZERO frontend UI, egui, or wgpu rendering code.
3. Tests & Verification:
   - Create tests/test_fast_io_and_ipc_m5d.py with comprehensive, non-tautological test cases.
   - Run tests sequentially under OMP_NUM_THREADS=2 across all suites:
     .\\.venv\\Scripts\\python.exe -m pytest tests/test_fast_io_and_ipc_m5d.py tests/test_super_elements_and_architecture_m5c.py tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
   - Verify 0 failures, 0 regressions.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5d\handoff.md and report completion via send_message to orchestrator.
