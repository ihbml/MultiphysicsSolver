# Task Assignment: Survey Explorer 3 (Phase 5 & System/Test Infrastructure)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_3\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (MUST read verbatim)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Code review findings K8, H5, H6, M10, and testing/benchmark status)
3. Existing files in project:
   - `backend/native_cpp/smr_catalysis/smrKineticsSource.C`
   - `backend/meshing/polymesh_io.py`
   - `backend/core/case_generator.py`
   - WSL runner scripts or test utilities (e.g. `scripts/run_wsl_checkmesh.sh`, `tests/test_wsl_openfoam_acceptance.py` or wherever WSL runner is)
   - Current test suite execution status using `.\\.venv\\Scripts\\python.exe -m pytest` with OMP_NUM_THREADS=2.

## Objective
Map the exact code locations, current implementations, diffs needed, and verification strategy for:
1. Phase 5: Physik & OpenFOAM-Schnittstelle
   - K8: `backend/native_cpp/smr_catalysis/smrKineticsSource.C` - stoichiometrically coupled reaction rates for species (CH4, H2O, H2, CO, CO2) and consistent enthalpy sink q = -r*dH(T) with reversible Xu-Froment kinetics. Properly evaluate `fieldi` in `addSup` so enthalpy sinks are not erroneously added to multiple fields.
   - H5 & H6: `polymesh_io.py` and `case_generator.py` - multi-region export for `chtMultiRegionFoam` including `regionProperties`, correct patch types (`patch` for farfield, `wall` for body, `mappedWall` with required keys), and complete `0/` and `fvSolution` files.
   - M10: WSL runner - `shlex.quote` command arguments, verify process return codes, handle paths without drive letters safely.
2. Baseline & Test Verification:
   - Survey available CAD fixtures in `resources/CADTestSamples/` (`BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `TestCAD.obj`, `TestCADSTPAP214.stp`).
   - Identify which test files currently fail or pass under sequential `.\\.venv\\Scripts\\python.exe -m pytest` with `OMP_NUM_THREADS=2`.

## Constraints
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Ansys is running in background. No heavy benchmarks.

## Deliverables
Write detailed findings to `survey_report.md` and `handoff.md` in your working directory. Send completion message back to orchestrator.


## 2026-11-09T23:50:45Z
[Message] timestamp=2026-11-09T23:50:45Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH content=You are Survey Explorer 3 (teamwork_explorer).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_3\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_3\DISPATCH.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md and c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md before starting work.

Focus Scope: Phase 5 & System/Test Infrastructure
1. Phase 5: Physik & OpenFOAM-Schnittstelle (K8, H5, H6, M10)
   - K8: backend/native_cpp/smr_catalysis/smrKineticsSource.C - stoichiometrically coupled reaction rates for species (CH4, H2O, H2, CO, CO2) and consistent enthalpy sink q = -r*dH(T) with reversible Xu-Froment kinetics. Properly evaluate fieldi in addSup so enthalpy sinks are not erroneously added to multiple fields.
   - H5 & H6: backend/meshing/polymesh_io.py and backend/core/case_generator.py - multi-region export for chtMultiRegionFoam including regionProperties, correct patch types (patch for farfield, wall for body, mappedWall with required keys), complete 0/ and fvSolution files.
   - M10: WSL runner - apply shlex.quote to command arguments, verify process return codes, and handle paths without drive letters safely.
2. System & Test Baseline:
   - Identify CAD test samples in resources/CADTestSamples/ (e.g., BladeFullRev1CFD.stp, BladeRev1CFD.obj, TestCAD.obj, TestCADSTPAP214.stp).
   - Check current test suite status and baseline test discovery with .\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests with OMP_NUM_THREADS=2.
   - Note which external fixtures are present or missing.

Constraints:
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Low-resource testing only.
- Write survey_report.md and handoff.md in your working directory.
- Send a completion message via send_message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c when finished.


## 2026-11-10T00:01:07Z
[Message] timestamp=2026-11-10T00:01:07Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH content=**Context**: Survey Progress Check
**Content**: Heartbeat safety timer fired. Checking on status of test suite run and survey report synthesis.
**Action**: Please report current status or estimated time to completion of `survey_report.md` and `handoff.md`.
