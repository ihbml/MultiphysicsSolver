# Dispatch: Forensic Integrity Auditor (auditor_m4_phase4)

## Target & Mission
Audit Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2).
Worker handoff report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md`
Authoritative Request: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
Survey Blueprint: `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md`
Project Scope: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`

## Audit Mission & Integrity Verification
Perform an exhaustive Anti-Cheat, Forensic Integrity, and Code Cleanliness Audit on the changes implemented by `worker_m4_phase4`:
1. **Static Analysis & Anti-Cheat Forensics**:
   - Verify NO hardcoded test results, fake returns, tautological checks, or facade implementations in:
     - `backend/meshing/difvm_topology.py`
     - `backend/meshing/native/src/difvm_mesh.cpp`
     - `backend/meshing/models.py`
     - `backend/meshing/moving_mesh.py`
     - `backend/meshing/gci.py`
     - `backend/meshing/coupling.py`
     - `tests/test_phase4_numerics_topology.py`
   - Specifically check that quadrilateral $(0,0)-(4,0)-(1,1)-(0,1)$ centroid $(1.4, 0.4)$ is computed dynamically from Green's theorem / triangle weights, NOT branch-matched on vertex values.
   - Check that GCI divergence $|R| \ge 1.0$ is computed genuinely, and that fixed-point iteration for apparent order $p$ and effective $r$ are mathematical calculations.
   - Check that Supermesh-AMI Sutherland-Hodgman polygon clipping computes real polygon intersections rather than mocking weights.
   - Check that SPH-FVM boundary area vectors integrate physical wall faces and P2M volume interpolation actually satisfies $\sum V_c \phi_c = \sum V_p \phi_p$.
   - Check that aspect ratio uses genuine bounding box spans and `wsrd_alpha` uses background cell volume $V_i^{\text{bg}}$.
2. **Runtime Verification**:
   - Execute the test suite sequentially under `OMP_NUM_THREADS=2`:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"`
     `.\.venv\Scripts\python.exe -m pytest tests/test_phase4_numerics_topology.py -v`
   - Run regression tests:
     `.\.venv\Scripts\python.exe -m pytest tests/test_moving_mesh.py tests/test_coupling.py tests/test_native_loader.py tests/test_asme_vv.py -v`
3. **Verdict Determination**:
   - Return either `CLEAN` (PASS) or `INTEGRITY VIOLATION` (FAIL).
   - Write full audit report to `g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m4_phase4\handoff.md`.


## 2026-11-10T17:14:25Z
You are the Forensic Integrity Auditor (auditor_m4_phase4) for CFDSolver.

Working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m4_phase4
Worker handoff report: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md
Authoritative Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Survey Blueprint: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md
Dispatch details: g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m4_phase4\DISPATCH.md
Project Scope: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md

Mission:
Perform an exhaustive Anti-Cheat, Forensic Integrity, and Code Cleanliness Audit on Milestone M4_PHASE4:
1. Static analysis & anti-cheat checks on:
   - `backend/meshing/difvm_topology.py`
   - `backend/meshing/native/src/difvm_mesh.cpp`
   - `backend/meshing/models.py`
   - `backend/meshing/moving_mesh.py`
   - `backend/meshing/gci.py`
   - `backend/meshing/coupling.py`
   - `tests/test_phase4_numerics_topology.py`
   Ensure NO hardcoded test results, fake branches, or facade implementations.
2. Runtime test execution:
   Run sequentially under OMP_NUM_THREADS=2:
   - `.\\.venv\\Scripts\\python.exe -m pytest tests/test_phase4_numerics_topology.py -v`
   - `.\\.venv\\Scripts\\python.exe -m pytest tests/test_moving_mesh.py tests/test_coupling.py tests/test_native_loader.py tests/test_asme_vv.py -v`
3. Provide explicit verdict: CLEAN (PASS) or INTEGRITY VIOLATION (FAIL).
4. Write report to `g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m4_phase4\handoff.md` and message the orchestrator.
