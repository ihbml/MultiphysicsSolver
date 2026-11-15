# Task Assignment: Survey Explorer 1 (Phase 1 & Phase 2)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (MUST read verbatim)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Code review findings K7, K6, L4, L5, M9, H1)
3. Existing files in project:
   - `README.md`, `ROADMAP.md`, `requirements.txt`, `pytest.ini`
   - `tests/test_wp4_multiphysics_hexcore.py` (specifically lines 90-200)
   - `models.py` or unstructured mesh models (`UnstructuredMesh.validate()`)
   - `backend/meshing/native/CMakeLists.txt` and native library loader (e.g., `difvm_topology.py`, `mesh_engine.py`)

## Objective
Map the exact code locations, current implementations, diffs needed, and verification strategy for:
1. Phase 1:
   - K7, L4, L5: Clean up README.md and ROADMAP.md to reflect actual prototype status (remove or mark nonexistent FVM solvers, Fortran, optimization dirs). Replace absolute file:///G:/ links with relative paths. Convert requirements.txt to UTF-8.
   - K6: Identify all tautological helper copies in `tests/test_wp4_multiphysics_hexcore.py` (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`, etc.) and determine their canonical locations in `backend/` for direct import.
   - M9: Check `UnstructuredMesh.validate()` in `models.py` (or relevant module): design the cell elongation and containment checks (centroid inside cell bbox, sum Vi Ci vs boundary integral). Update `pytest.ini` (`testpaths = tests backend/geometry/tests`) and identify missing external CAD/NX fixtures needing `@pytest.mark.skipif`.
2. Phase 2:
   - H1: OpenMP linkage in `backend/meshing/native/CMakeLists.txt` (`find_package(OpenMP REQUIRED)`, `target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)`). Check native library loader to ensure failures are raised explicitly rather than silently falling back to Python.

## Constraints
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Ansys is running in background. No heavy benchmarks.

## Deliverables
Write detailed findings to `survey_report.md` and `handoff.md` in your working directory. Send completion message back to orchestrator.

## 2026-11-09T23:50:31Z
[Message] sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH
Content:
You are Survey Explorer 1 (teamwork_explorer).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\DISPATCH.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md and c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md before starting work.

Focus Scope: Phase 1 & Phase 2
1. Phase 1: Ehrlichkeit & Test-Integrität (K7, K6, L4, L5, M9)
   - K7, L4, L5: Inspect README.md, ROADMAP.md, requirements.txt. Identify nonexistent modules/solvers/Fortran/optimization references to remove/mark as planned; locate absolute file:///G:/ links to convert to relative paths; verify UTF-16 to UTF-8 conversion for requirements.txt.
   - K6: Inspect tests/test_wp4_multiphysics_hexcore.py (lines 90-200 and test cases 355-600, 767-797). Identify all tautological helper copies (compute_amips_energy, compute_wsrd_weights, compute_asme_vv20_gci, compute_farfield_bounding_box, etc.) and map where their canonical counterparts are in backend/ for direct import.
   - M9: Inspect models.py or unstructured mesh models (UnstructuredMesh.validate()). Design the elongation and containment checks (centroid in cell bounding box, sum Vi Ci vs boundary integral). Inspect pytest.ini (update testpaths to tests and backend/geometry/tests) and identify missing external CAD/NX fixtures needing @pytest.mark.skipif.
2. Phase 2: Build & Plattform (H1)
   - H1: Inspect backend/meshing/native/CMakeLists.txt and native library loaders (difvm_topology.py, mesh_engine.py). Specify exact CMake changes (find_package(OpenMP REQUIRED), OpenMP::OpenMP_CXX link) and ensure native loader errors are not swallowed.

Constraints:
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Low-resource testing only.
- Write survey_report.md and handoff.md in your working directory.
- Send a completion message via send_message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c when finished.

## 2026-11-09T23:51:51Z
[Message] sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH
**Context**: Phase 1 Doc & Repo Hygiene Scope Update
**Content**: Zusatz-Auftrag vom Nutzer: Die zahlreichen losen Markdown-Dateien im Root-Verzeichnis (wie SUGGESTIONS_BACKEND.md, SUGGESTIONS_FRONTEND.md, COMMUNICATION_BACKEND_FRONTEND.md, FRONTEND_REQUIREMENTS_ON_BACKEND.md, TEST_READY.md, TEST_INFRA.md, DEAD_ENDS.md etc.) sollen nicht gelöscht, sondern gebündelt / archiviert werden (z. B. in `docs/DEVELOPMENT_HISTORY.md` oder `docs/archive/`). Im Root sollen nur Kern-Dokumente verbleiben (README.md, ARCHITECTURE.md, ROADMAP.md).
**Action**: Include an inventory of all loose root Markdown files in your survey report and specify the consolidation structure and target paths for the Phase 1 Worker.

## 2026-11-09T23:53:11Z
[Message] sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH
**Context**: CRITICAL USER CLARIFICATION on Doc Bundling / Repo Hygiene
**Content**: Der Nutzer hat folgendes verbindlich präzisiert:
Die Frontend/Backend-Kommunikationsdokumente:
- SUGGESTIONS_BACKEND.md
- SUGGESTIONS_FRONTEND.md
- COMMUNICATION_BACKEND_FRONTEND.md
- FRONTEND_REQUIREMENTS_ON_BACKEND.md
MÜSSEN UNANGETASTET BLEIBEN und genau so im Root bestehen bleiben wie sie sind!

NUR die Dokumente von Teamwork (die vielen Agenten-Handoffs, Briefings, Dispatches und Progress-Dateien aus `.agents/teamwork/` sowie Teamwork-Testberichte wie TEST_READY.md) sollen in eine zentrale Datei (z. B. `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`) gebündelt werden, damit die verstreuten Agenten-Einzeldateien chronologisch nachvollziehbar zusammengefasst werden.
**Action**: Update your survey plan and recommendations accordingly. Keep SUGGESTIONS_BACKEND.md, SUGGESTIONS_FRONTEND.md, COMMUNICATION_BACKEND_FRONTEND.md, and FRONTEND_REQUIREMENTS_ON_BACKEND.md strictly untouched in root. Plan the consolidation strictly for .agents/teamwork/ history and TEST_READY.md into docs/.
