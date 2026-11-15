## 2026-11-14T23:26:00Z
You are critic_m5d_gen10, a teamwork_critic conducting the Mathematics & Specification Fidelity Review for Milestone M5D: Backend Fast IO & JSON-RPC/NDJSON IPC Contracts.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5d_gen10

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md (MANDATORY: read before starting work)
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_10\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5d_gen10\handoff.md
5. g:\Datein\Projects\CFDSolver\FRONTEND_REQUIREMENTS_ON_BACKEND.md
6. g:\Datein\Projects\CFDSolver\COMMUNICATION_BACKEND_FRONTEND.md

Scope of Review:
1. Mathematics, SI Dimensions & Protocol Specification Fidelity:
   - Verify strict 7-element SI dimensions validation `[kg, m, s, K, mol, A, cd]`:
     - Sequence length must be exactly 7.
     - Dimensional conversion to/from OpenFOAM format string `[kg m s K mol A cd]`.
     - Invalid length or non-numeric types raise error / code -32005.
   - Verify binary buffer layouts for Paket 1 (`CFDGEO01`) and Paket 2 (`CFDMSH02`):
     - Little-endian floats, offsets, strides, JSON metadata length header.
     - Field quality arrays: skewness, non-orthogonality, aspect ratio, volume, WSRD weights, AMIPS energy, folded faces.
   - Verify Paket 4 live NDJSON telemetry format:
     - `iter` events contain iteration, residuals map, Courant numbers (`co_max`, `co_mean`), `n_nan_cells`, execution time.
   - Verify Fast IO mathematical fidelity:
     - Boundary field owner cell value fallback for `zeroGradient` and `calculated` patch types.
     - Numerical time directory sorting (proper float/scientific notation ordering: e.g. 0.001 < 0.01 < 0.1 < 1 < 10).
   - Verify Material Database physical properties & SI units:
     - Categorized groups (surface, electrical, magnetic, mechanical, optical, thermal, manufacturing, radiological).
     - Standard SI unit mapping.
     - `Material.missing_properties(required: Sequence[str]) -> List[str]`.
2. Verification Execution:
   - Run tests:
     .\\.venv\\Scripts\\python.exe -m pytest tests/test_fast_io_and_ipc_m5d.py -v
3. Deliver verdict:
   - Write structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5d_gen10\handoff.md with explicit verdict: APPROVE or REQUEST_CHANGES.
   - Message orchestrator with verdict.
