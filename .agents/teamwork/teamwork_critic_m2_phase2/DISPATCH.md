# Task Assignment: Platform & Build Conformance Critic (Phase 2 Gate)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m2_phase2\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Finding H1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\changes.md`
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\handoff.md`

## Review Mission
Evaluate build system conformance, OpenMP portability, and native loader architecture:
1. Verify `CMakeLists.txt` uses CMake best practices (`find_package(OpenMP REQUIRED)`, `target_link_libraries(... OpenMP::OpenMP_CXX)`). Proves that `libgomp` will be linked under GCC/Linux (resolving `undefined symbol: GOMP_*`) and `vcomp.lib` under MSVC.
2. Verify native loader exception handling in `difvm_topology.py` and `mesh_engine.py`:
   - Are loading errors captured with detailed diagnostics?
   - Is `require_native_dll` / `strict_native` clean and fail-fast?
   - Does it preserve non-strict fallback while prominently logging errors?
3. Run tests sequentially with `OMP_NUM_THREADS=2`.

## Deliverables
Write `handoff.md` with explicit binary verdict: `APPROVE` or `REQUEST_CHANGES`. Send message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.


## 2026-11-10T01:02:48Z
You are Agent 2 (Platform & Build Conformance Critic) on the 3-agent review panel for Phase 2.
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m2_phase2\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m2_phase2\DISPATCH.md
Read the worker changes at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\changes.md and handoff.md.
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md.

Review Mission:
Evaluate build system and loader architecture:
1. Verify CMake discovery of OpenMP via find_package(OpenMP REQUIRED) and target_link_libraries(... OpenMP::OpenMP_CXX).
2. Verify native loader exception handling in difvm_topology.py and mesh_engine.py.
3. Run tests sequentially with OMP_NUM_THREADS=2.

Deliver handoff.md with clear verdict (APPROVE / REQUEST_CHANGES) and send message to orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.
