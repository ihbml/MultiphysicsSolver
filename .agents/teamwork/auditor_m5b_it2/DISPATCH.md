# Dispatch for auditor_m5b_it2

## Role & Mission
You are `auditor_m5b_it2`, a `teamwork_auditor` performing Anti-Cheat & Forensic code-path verification for Milestone M5B Iteration 2.

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it2\handoff.md`
- Tests: `tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`

## Forensic Verification Mandate
1. **Zero-Shortcut & Anti-Cheat Audit**:
   - Inspect all modifications made by `worker_m5b_it2` in `backend/physics_modules/monolithic_fsi.py`, `backend/geometry/parsers/step_parser.py`, and `tests/test_paper_integration_m5b.py`.
   - Confirm zero stubs, zero dummy/mock buffers, zero fake returns, zero tautological assertions.
   - Confirm zero heuristic clamps.
   - Confirm fail-fast error checking (e.g. `ValueError` on mismatched batch sizes or empty arrays).
2. **SMR Excision Audit**:
   - Verify that line 630 of `SUGGESTIONS_BACKEND.md` contains `genericReactionSource.C`.
   - Verify zero occurrences of `smrKineticsSource` in codebase.
3. **Out-of-Scope Boundary Audit**:
   - Verify zero frontend UI (`egui`), shaders, or `wgpu` renderers introduced in backend/tests.
4. **Execution & Test Verification**:
   - Run pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
   - Verify all 80 tests pass genuinely.

Deliver verdict as CLEAN (PASS) or INTEGRITY VIOLATION in `handoff.md` and report via send_message.


## 2026-11-13T23:56:06Z
You are auditor_m5b_it2, a teamwork_auditor performing Anti-Cheat & Forensic code-path verification for Milestone M5B Iteration 2.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b_it2

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b_it2\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it2\handoff.md

Tasks:
- Inspect modified codebase files and verify Zero-Shortcut Policy (no stubs, no fake returns, no tautological assertions, no silent clamps, no heuristic clamps).
- Verify SMR excision across SUGGESTIONS_BACKEND.md:630 and all codebase files (zero occurrences of smrKineticsSource or SMR kinetics).
- Verify zero out-of-scope frontend UI/renderer code.
- Run test suites sequentially under OMP_NUM_THREADS=2:
  .\\.venv\\Scripts\\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
- Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b_it2\handoff.md with explicit CLEAN (PASS) or INTEGRITY VIOLATION verdict.
- Report verdict and summary via send_message to orchestrator.
