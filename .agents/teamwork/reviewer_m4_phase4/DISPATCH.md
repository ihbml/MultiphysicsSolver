# Dispatch: Code Quality & Stability Reviewer (reviewer_m4_phase4)

## Target & Mission
Evaluate Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2).
Worker handoff report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md`
Authoritative Request: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
Survey Blueprint: `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md`
Project Scope: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`

## Evaluation Scope & Criteria
Verify code quality, software architecture, numerical stability, boundary edge cases, and test completeness:
1. **Code Architecture & Cleanliness**:
   - Inspect changes in:
     - `backend/meshing/difvm_topology.py`
     - `backend/meshing/native/src/difvm_mesh.cpp`
     - `backend/meshing/models.py`
     - `backend/meshing/moving_mesh.py`
     - `backend/meshing/gci.py`
     - `backend/meshing/coupling.py`
   - Check error handling, boundary conditions, edge cases (zero areas, degenerate cells, non-convex polygons, colinear points).
   - Verify native C++ memory management, no raw pointer leaks, bounds checking, MSVC 2022 compatibility.
2. **Test Completeness & Regressions**:
   - Verify that test suite `tests/test_phase4_numerics_topology.py` covers all H3, H4, H2, H7, H8, M1, M2 requirements.
   - Run tests sequentially under `OMP_NUM_THREADS=2`:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"`
     `.\.venv\Scripts\python.exe -m pytest tests/test_phase4_numerics_topology.py -v`
     `.\.venv\Scripts\python.exe -m pytest tests/test_moving_mesh.py tests/test_coupling.py tests/test_native_loader.py tests/test_asme_vv.py tests/test_wp1_topology_polymesh.py -v`
3. **Verdict Determination**:
   - Provide explicit verdict: `APPROVE` or `REQUEST_CHANGES`.
   - Write full review report to `g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m4_phase4\handoff.md`.


## 2026-11-10T17:14:47Z
You are the Code Quality & Stability Reviewer (reviewer_m4_phase4) for CFDSolver.

Working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m4_phase4
Worker handoff report: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md
Authoritative Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Survey Blueprint: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md
Dispatch details: g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m4_phase4\DISPATCH.md
Project Scope: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md

Mission:
Evaluate Milestone M4_PHASE4 for code quality, architecture, numerical stability, and test coverage:
1. Code quality, exception safety, boundary handling in:
   - `backend/meshing/difvm_topology.py`
   - `backend/meshing/native/src/difvm_mesh.cpp`
   - `backend/meshing/models.py`
   - `backend/meshing/moving_mesh.py`
   - `backend/meshing/gci.py`
   - `backend/meshing/coupling.py`
2. Test suite coverage and execution:
   Run sequentially under OMP_NUM_THREADS=2:
   - `.\\.venv\\Scripts\\python.exe -m pytest tests/test_phase4_numerics_topology.py -v`
   - `.\\.venv\\Scripts\\python.exe -m pytest tests/test_moving_mesh.py tests/test_coupling.py tests/test_native_loader.py tests/test_asme_vv.py tests/test_wp1_topology_polymesh.py -v`
3. Provide explicit verdict: APPROVE or REQUEST_CHANGES.
4. Write report to `g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m4_phase4\handoff.md` and message the orchestrator.
