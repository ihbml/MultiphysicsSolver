# Task Assignment: Worker M1_PHASE1 (Ehrlichkeit, Test-Integrität & Doku-Archiv)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Findings K7, K6, L4, L5, M9, N1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md` (Scope and contracts)
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\survey_report.md` (Exact diff blueprints and line numbers)
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\handoff.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Exclusive File Ownership
The Worker exclusively owns and may modify:
- `README.md`
- `ROADMAP.md`
- `requirements.txt`
- `pytest.ini`
- `.gitignore`
- `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`
- `tests/test_wp4_multiphysics_hexcore.py`
- `models.py`
- `backend/geometry/tests/test_bridge.py`

STRICT CONSTRAINT: Do NOT touch or move `SUGGESTIONS_BACKEND.md`, `SUGGESTIONS_FRONTEND.md`, `COMMUNICATION_BACKEND_FRONTEND.md`, or `FRONTEND_REQUIREMENTS_ON_BACKEND.md`. They must remain in root.

## Hardware & Execution Guardrails
- Ansys is running in background.
- Throttle threads: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Run tests strictly sequentially using `.\\.venv\\Scripts\\python.exe -m pytest ...`.
- No large meshes (>30,000 cells).

## Objectives
1. **L5 & L4 & K7 & Doc Bundling:**
   - Convert `requirements.txt` to UTF-8 without BOM.
   - Replace absolute `file:///G:/...` links in `README.md` and `ROADMAP.md` with relative links.
   - Update `README.md` and `ROADMAP.md` to accurately state implementation status (CAD ingestion + mesher prototype; physics modules are templates; remove or clearly mark nonexistent FVM solvers, Fortran modules, and optimization directories as planned).
   - Consolidate the `.agents/teamwork/` history and `TEST_READY.md` into `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`.
2. **K6 (Tautological Tests Elimination):**
   - In `tests/test_wp4_multiphysics_hexcore.py:96-234`, delete local tautological copies (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`) and import the canonical functions directly from `backend/meshing/gci.py`, `backend/meshing/difvm_topology.py`, and `backend/meshing/mesh_engine.py`.
   - Update test methods to test genuine backend functionality.
3. **M9 & UnstructuredMesh.validate():**
   - In `models.py` (`UnstructuredMesh.validate()`):
     - Check cell elongation and containment: verify each cell centroid $C_i$ lies within its cell bounding box $[\min(v), \max(v)] \pm \epsilon$.
     - Verify total centroid $\sum V_i \mathbf{C}_i$ matches boundary surface integral $\frac{1}{d+1} \sum_{f \in \partial \Omega} \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f)$.
   - In `pytest.ini`: update `testpaths = tests backend/geometry/tests`.
   - Add `@pytest.mark.skipif` guards for missing external CAD/NX fixtures in `backend/geometry/tests/test_bridge.py` and OpenFOAM tutorial STL assets in `tests/test_wp4_multiphysics_hexcore.py`.
   - In `.gitignore`: whitelist `!resources/CADTestSamples/**/*.obj` so CAD fixtures are not ignored.
4. **Verification:**
   - Execute `.\\.venv\\Scripts\\python.exe -m pytest tests/test_wp4_multiphysics_hexcore.py` and `backend/geometry/tests` with `OMP_NUM_THREADS=2`.
   - Document commands and results in `handoff.md`.

## Deliverables
Write `changes.md` and `handoff.md` in your working directory. Send completion message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.


## 2026-11-10T00:02:55Z
[Message] timestamp=2026-11-10T00:02:55Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH content=You are Worker M1_PHASE1 (teamwork_worker).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\DISPATCH.md
Read the survey report and blueprints at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\survey_report.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md before starting.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Scope (Phase 1):
1. Convert requirements.txt from UTF-16LE to UTF-8 without BOM.
2. Update README.md and ROADMAP.md to accurately reflect current prototype status; replace all file:///G:/ links with relative paths; mark nonexistent FVM solvers/Fortran/optimization as planned.
3. Bundle .agents/teamwork/ coordination history and TEST_READY.md into docs/TEAMWORK_DEVELOPMENT_HISTORY.md. STRICT: leave SUGGESTIONS_BACKEND.md, SUGGESTIONS_FRONTEND.md, COMMUNICATION_BACKEND_FRONTEND.md, and FRONTEND_REQUIREMENTS_ON_BACKEND.md untouched in root.
4. In tests/test_wp4_multiphysics_hexcore.py: delete local tautological copies (compute_amips_energy, compute_wsrd_weights, compute_asme_vv20_gci, compute_farfield_bounding_box) and import directly from backend/ modules. Replace tautological tests with real tests.
5. In models.py (UnstructuredMesh.validate()): implement cell bounding box containment check and global centroid divergence theorem balance check.
6. In pytest.ini: update testpaths = tests backend/geometry/tests. Add @pytest.mark.skipif guards for missing external NX/OpenFOAM fixtures. Whitelist *.obj in resources/CADTestSamples/ in .gitignore.
7. Verify with sequential pytest using .\.venv\Scripts\python.exe with OMP_NUM_THREADS=2.

Write changes.md and handoff.md, then send completion message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.

## 2026-11-10T00:28:40Z
[Message] timestamp=2026-11-10T00:28:40Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH content=**Context**: Phase 1 Worker Progress Check
**Content**: Heartbeat check at 00:28Z. Checking on progress of Tasks 4-8.
**Action**: Please report your current step, test results so far, and estimated time to completion of `changes.md` and `handoff.md`.
