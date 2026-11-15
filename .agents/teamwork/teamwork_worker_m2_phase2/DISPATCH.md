# Task Assignment: Worker M2_PHASE2 (Build & Plattform - OpenMP & Native Loader)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Finding H1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_1\survey_report.md` (Finding H1 details)

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Exclusive File Ownership
- `backend/meshing/native/CMakeLists.txt`
- `backend/meshing/difvm_topology.py` (loader functions `get_native_dll`)
- `backend/meshing/mesh_engine.py` (loader functions)

## Hardware & Execution Guardrails
- Ansys running in background: throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Low-resource sequential testing only.

## Objectives
1. **CMakeLists.txt OpenMP Linkage (Finding H1):**
   - In `backend/meshing/native/CMakeLists.txt`:
     - Add `find_package(OpenMP REQUIRED)`.
     - Explicitly link OpenMP: `target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)`.
     - Ensure both MSVC and GCC/Clang configurations correctly link OpenMP without undefined symbols (e.g. `GOMP_*`).
2. **Native Loader Robustness (Finding H1):**
   - In `difvm_topology.py` (`get_native_dll`) and `mesh_engine.py`:
     - Do NOT silently swallow errors with `except Exception: return None`.
     - Record and log any DLL load exceptions (`ctypes.cdll.LoadLibrary` failures, `OSError`).
     - When `CFD_STRICT_NATIVE=1` or `strict_native=True`, explicitly raise `RuntimeError` detailing the load failure instead of silently falling back to Python.
3. **Verification:**
   - Verify CMakeLists.txt configuration syntax.
   - Verify native library loader behavior: test loading with `CFD_STRICT_NATIVE=1` and ensure explicit error reporting if a bad path is supplied.
   - Run sequential tests with `OMP_NUM_THREADS=2`.

## Deliverables
Write `changes.md` and `handoff.md` in your working directory. Send completion message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.


## 2026-11-10T00:44:14Z
You are Worker M2_PHASE2 (teamwork_worker).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m2_phase2\DISPATCH.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md before starting.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Scope (Phase 2):
1. In backend/meshing/native/CMakeLists.txt: add find_package(OpenMP REQUIRED) and target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX).
2. In difvm_topology.py and mesh_engine.py: update native library loaders so load errors are captured/logged and never silently swallowed when CFD_STRICT_NATIVE=1 or strict_native=True (fail-fast with RuntimeError).
3. Verify changes and run sequential pytest with OMP_NUM_THREADS=2.

Deliver changes.md and handoff.md, then send completion message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.


## 2026-11-10T00:54:30Z
**Context**: Worker M2_PHASE2 Status Check
**Content**: Safety timer fired at 00:54Z. Checking on progress of CMakeLists.txt OpenMP linkage and loader fail-fast implementation.
**Action**: Please report your current step, progress, and update your progress.md.
