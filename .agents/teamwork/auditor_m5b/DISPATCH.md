# Dispatch for auditor_m5b

## Role & Mission
You are `auditor_m5b`, a `teamwork_auditor` performing Anti-Cheat & Forensic code-path verification for Milestone M5B (Features F55–F61, §12.2 contracts, and line 630 SMR cleanup in SUGGESTIONS_BACKEND.md).

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it1\handoff.md`
- Spec Miner Report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\report.md`
- Spickzettel Snippets 9–14: `g:\Datein\Projects\CFDSolver\resources\reference_spickzettel\INDEX.md`
- Tests: `tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`

## Forensic Verification Mandate
1. **Zero-Shortcut & Anti-Cheat Audit**:
   - Inspect all modified files (`backend/meshing/moving_mesh.py`, `difvm_topology.py`, `polymesh_io.py`, `case_generator.py`, `step_parser.py`, `differentiable_cad.py`, `mesh_engine.py`, `gpu_voronoi.py`, `monolithic_fsi.py`).
   - Confirm zero stubs, zero dummy/mock buffers, zero fake returns, zero tautological assertions (`assert True` or comparing a variable against itself).
   - Confirm zero heuristic clamps (e.g. `0.85*max_d` or `1e-15` silent volume overwrites).
   - Confirm fail-fast error checking (throwing `ValueError`/`RuntimeError` on invalid inputs like $\cos\theta_f \le 10^{-8}$ rather than silently clamping).
2. **SMR Excision Audit**:
   - Verify that line 630 in `SUGGESTIONS_BACKEND.md` was cleaned up to `genericReactionSource.C`.
   - Run grep / scan across codebase and configurations to verify zero remaining occurrences of `smrKineticsSource` or hardcoded SMR kinetics.
3. **Out-of-Scope Boundary Audit**:
   - Verify that no frontend UI (`egui`), shaders, `wgpu` renderers, or viewport display code were added.
4. **Execution & Test Verification**:
   - Run pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
   - Verify all tests pass genuinely.

Deliver verdict as CLEAN (PASS) or INTEGRITY VIOLATION in `handoff.md` and report via send_message.


## 2026-11-13T23:31:21Z
Sender: ea8f7ac6-187f-461c-b8f2-aeda0693e3d8
Content: You are auditor_m5b, a teamwork_auditor performing Anti-Cheat & Forensic code-path verification for Milestone M5B.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it1\handoff.md
6. g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\report.md

Tasks:
- Inspect modified codebase files and verify Zero-Shortcut Policy (no stubs, no fake returns, no tautological assertions, no silent clamps, no heuristic clamps).
- Verify SMR excision across SUGGESTIONS_BACKEND.md:630 and all codebase files (zero occurrences of smrKineticsSource or SMR kinetics).
- Verify zero out-of-scope frontend UI/renderer code.
- Run test suites sequentially under OMP_NUM_THREADS=2:
  .\\.venv\\Scripts\\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
- Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5b\handoff.md with explicit CLEAN (PASS) or INTEGRITY VIOLATION verdict.
- Report verdict and summary via send_message to orchestrator.
