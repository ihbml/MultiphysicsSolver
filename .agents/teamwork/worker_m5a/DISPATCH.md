# Dispatch: Worker M5A (SMR Excision, Multiphysics Chemistry & OpenFOAM Audits H5, H6, M10)

## Mission
Implement Milestone M5A:
1. **SMR Excision & Multiphysics Generalization (Feature F51 / R1)**:
   - Excise SMR-Xu-Froment across all 13 mapped files:
     - In `backend/native_cpp/smr_catalysis/`: Replace `smrKineticsSource.C` and `.H` with generic data-driven `genericReactionSource.C` and `genericReactionSource.H`. Update `Make/files` to produce `libgenericReactionSource`.
     - In `backend/physics_modules/registry.py`: Parameterize `SpeciesChemistryModule` to accept arbitrary user-configured species lists (`config.parameters.get("species", ["Ydefault"])`).
     - In `COMMUNICATION_BACKEND_FRONTEND.md`: Document generic solver interfaces in the handover changelog, clean lines 56 & 70.
     - In `FRONTEND_REQUIREMENTS_ON_BACKEND.md`: Clean line 164 species bias.
     - In `SUGGESTIONS_BACKEND.md`: Clean Section 9.4.1.
     - In `backend/OPENFOAM_BACKEND_ATLAS.md`: Replace SMR references with generic chemistry.
     - In `SCOPE.md`: Already updated.
2. **Audit H5 Multi-Region Export (Feature F52)**:
   - In `backend/meshing/polymesh_io.py`: Implement `export_openfoam_multiregion_polymesh` exporting each region to `constant/<region>/polyMesh`, generating `constant/regionProperties` (`regions ( fluid (...) solid (...) );`), and supporting `mappedWall` (`sampleMode`, `sampleRegion`, `samplePatch`) and `cyclic` (`neighbourPatch`) dictionaries.
3. **Audit H6 Runnable Case Generation & Dynamic Decomposition (Feature F53)**:
   - In `backend/core/case_generator.py` and `backend/physics_modules/registry.py`:
     - Write valid initial fields into `0/` directory based on active physics modules.
     - Make `processorWeights` dynamic: allocate array length exactly matching `mpi_ranks` (not hardcoded 16).
     - Add `SIMPLE` block with relaxation factors and equation solvers for `p`, `U`, etc., alongside `PIMPLE` block.
4. **Audit M10 Hardened WSL Runner (Feature F54)**:
   - In `backend/runner/wsl_runner.py`:
     - Apply `shlex.quote` to bash command arguments and case directory paths.
     - Check `proc.returncode` and raise `RuntimeError` on solver errors.
     - Support paths without Windows drive letters.
     - Implement live regex parsing for residuals (`Time`, `Initial residual`, `Final residual`), `Courant Number max`, and check for `NaN`.
5. **Testing**:
   - Write comprehensive tests in `tests/test_generic_reaction_and_openfoam_audits.py`.
   - Run tests sequentially under throttled settings:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest tests/test_generic_reaction_and_openfoam_audits.py`
     and verify regression across existing tests.

## File Ownership
You exclusively own:
- `backend/native_cpp/smr_catalysis/`
- `backend/physics_modules/registry.py`
- `backend/meshing/polymesh_io.py`
- `backend/core/case_generator.py`
- `backend/runner/wsl_runner.py`
- `COMMUNICATION_BACKEND_FRONTEND.md`
- `FRONTEND_REQUIREMENTS_ON_BACKEND.md`
- `SUGGESTIONS_BACKEND.md`
- `backend/OPENFOAM_BACKEND_ATLAS.md`
- `tests/test_generic_reaction_and_openfoam_audits.py`

## Authoritative Inputs
- Read `ORIGINAL_REQUEST.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- Read `SCOPE.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`
- Read Explorer Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m5_survey_1\handoff.md`
- Read Explorer Report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m5_survey_1\report.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Constraints
- Throttling: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Sequential test runs only. Small test grids $\le 30{,}000$ cells.
- Zero-Shortcut Policy: throw exceptions before crashes, no raw pointer leaks, strict bounds checks, no heuristic clamps, no swallowed exceptions.
- Output report in `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5a\handoff.md`.


## 2026-11-12T02:06:12Z
**Context**: Liveness Check for Milestone M5A
**Content**: Status query: It has been ~19 minutes since dispatch. Please report your current progress, files touched so far, and ETA for handoff.
**Action**: Update `progress.md` and reply with your current status.
