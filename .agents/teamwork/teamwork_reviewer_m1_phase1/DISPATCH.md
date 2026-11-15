# Task Assignment: Code Quality & Stability Reviewer (Phase 1 Gate)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_reviewer_m1_phase1\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Findings K7, K6, L4, L5, M9)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md`
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\handoff.md`

## Review Mission
Evaluate code quality, test execution stability, and interface compliance:
1. Verify that `pytest.ini` properly discovers `tests` and `backend/geometry/tests`.
2. Verify that `test_bridge.py` skips Siemens NX cleanly without raising uncaught exceptions when NX is absent.
3. Verify that `tests/test_wp4_multiphysics_hexcore.py` executes sequentially and reliably with `OMP_NUM_THREADS=2`.
4. Inspect code style, error handling, typing, and docstrings across modified files (`models.py`, `difvm_topology.py`, `mesh_engine.py`, `test_wp4_multiphysics_hexcore.py`).
5. Ensure zero swallowed exceptions, no unhandled NoneTypes, and clean fail-fast behavior.

## Execution Constraints
- Ansys running in background: throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Low-resource sequential testing only.

## Output
Write `handoff.md` with explicit binary verdict: `APPROVE` or `REQUEST_CHANGES`. Send message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.

## 2026-11-10T00:32:32Z
From: e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c
Priority: HIGH
Content:
You are Agent 3 (Code Quality & Stability Reviewer) on the 3-agent review panel for Phase 1.
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_reviewer_m1_phase1\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_reviewer_m1_phase1\DISPATCH.md
Read the worker changes at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md and handoff.md.
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md.

Review Mission:
Evaluate software engineering quality and stability:
1. Verify pytest.ini testpaths and discovery.
2. Verify test_bridge.py cleanly skips NX when unavailable.
3. Verify test_wp4_multiphysics_hexcore.py runs sequentially without failures under OMP_NUM_THREADS=2.
4. Verify error handling, typed exceptions, zero raw pointer leaks, and clean fail-fast behavior across modified files.

Deliver handoff.md with clear verdict (APPROVE / REQUEST_CHANGES) and send message to orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.
