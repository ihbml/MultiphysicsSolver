# Task Assignment: Anti-Cheat & Code Cleanliness Auditor (Phase 1 Gate)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m1_phase1\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Findings K7, K6, L4, L5, M9, N1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md`
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\handoff.md`

## Audit Mission
Perform rigorous forensic anti-cheat and integrity audit on Phase 1 changes:
1. Verify that `requirements.txt` is genuine UTF-8 without BOM (no corrupted encoding, no fake content).
2. Verify that `README.md` and `ROADMAP.md` have genuinely removed fake claims about FVM solvers, Fortran acceleration, and optimization modules (or marked them as planned), and that all `file:///G:/` links are replaced with relative paths.
3. Verify that `docs/TEAMWORK_DEVELOPMENT_HISTORY.md` contains the consolidated swarm coordination history and TEST_READY.md, AND that `SUGGESTIONS_BACKEND.md`, `SUGGESTIONS_FRONTEND.md`, `COMMUNICATION_BACKEND_FRONTEND.md`, and `FRONTEND_REQUIREMENTS_ON_BACKEND.md` are STRICTLY UNTOUCHED in repository root.
4. Verify that in `tests/test_wp4_multiphysics_hexcore.py`, all local tautological copies (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`) have been completely deleted, and genuine canonical imports from `backend/` are used.
5. Verify that no fake, mocked, or trivial test assertions remain that pass without genuine computation.
6. Verify that `models.py` (`UnstructuredMesh.validate()`) genuinely checks cell bounding box containment and divergence theorem global centroid balance without hardcoded passes.

## Execution Constraints
- Ansys running in background: throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Low-resource sequential testing only.

## Output
Write `handoff.md` with explicit binary verdict: `APPROVE` or `REQUEST_CHANGES` (or `INTEGRITY VIOLATION`). Send message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.

## 2026-11-10T00:32:21Z
[Message from orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c]
You are Agent 1 (Anti-Cheat & Code Cleanliness Auditor) on the 3-agent review panel for Phase 1.
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m1_phase1\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_auditor_m1_phase1\DISPATCH.md
Read the worker changes at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md and handoff.md.
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md.

Audit Mission:
Perform rigorous forensic anti-cheat verification:
1. Verify requirements.txt is true UTF-8 without BOM.
2. Verify README.md and ROADMAP.md have removed fake solver claims and converted file:///G:/ links to relative paths.
3. Verify docs/TEAMWORK_DEVELOPMENT_HISTORY.md contains consolidated history, and SUGGESTIONS_BACKEND.md, SUGGESTIONS_FRONTEND.md, COMMUNICATION_BACKEND_FRONTEND.md, FRONTEND_REQUIREMENTS_ON_BACKEND.md are strictly untouched in root.
4. Verify tests/test_wp4_multiphysics_hexcore.py deleted all 4 local tautological helper copies and uses canonical backend imports; verify no tautological tests remain.
5. Verify models.py (UnstructuredMesh.validate()) implements real bounding box containment and divergence theorem balance checks.

Deliver handoff.md with clear verdict (APPROVE / REQUEST_CHANGES / INTEGRITY VIOLATION) and send message to orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.
