# Task Assignment: Anti-Cheat Auditor (Phase 2 Gate)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m2_phase2\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Finding H1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\changes.md`
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\handoff.md`

## Audit Mission
Forensic anti-cheat verification of Phase 2 deliverables:
1. Verify `backend/meshing/native/CMakeLists.txt` genuinely includes `find_package(OpenMP REQUIRED)` and links `OpenMP::OpenMP_CXX`. Verify no commented-out stubs or mock flags.
2. Verify in `difvm_topology.py` and `mesh_engine.py` that native loading failures are NOT silently caught and suppressed. Ensure fail-fast `RuntimeError` is raised whenever `strict_native=True` or `CFD_STRICT_NATIVE=1`.
3. Verify that `tests/test_native_loader.py` tests genuine fail-fast mechanics without hardcoded test pass values.
4. Execute test commands sequentially with `OMP_NUM_THREADS=2`.

## Deliverables
Write `handoff.md` with explicit binary verdict: `APPROVE` or `REQUEST_CHANGES` (or `INTEGRITY VIOLATION`). Send message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.

## 2026-11-10T01:02:43Z
[Message] timestamp=2026-11-10T01:02:43Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH
Content:
You are Agent 1 (Anti-Cheat & Cleanliness Auditor) on the 3-agent review panel for Phase 2.
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m2_phase2\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m2_phase2\DISPATCH.md
Read the worker changes at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\changes.md and handoff.md.
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md.

Audit Mission:
Forensic anti-cheat verification:
1. Verify CMakeLists.txt genuinely links OpenMP::OpenMP_CXX.
2. Verify native loader in difvm_topology.py and mesh_engine.py does not swallow errors when strict_native=True or CFD_STRICT_NATIVE=1.
3. Verify test_native_loader.py tests genuine error handling without cheats.

Deliver handoff.md with clear verdict (APPROVE / REQUEST_CHANGES / INTEGRITY VIOLATION) and send message to orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.
