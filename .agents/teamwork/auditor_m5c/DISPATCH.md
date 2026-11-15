# Dispatch for auditor_m5c

## Role & Mission
You are `auditor_m5c`, a `teamwork_auditor` performing Anti-Cheat & Forensic code-path verification for Milestone M5C (Features F62 & F63).

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md`
- SUGGESTIONS_BACKEND.md: `g:\Datein\Projects\CFDSolver\SUGGESTIONS_BACKEND.md` (§10 & §11.2)
- Reference Spickzettel Snippet 8: `g:\Datein\Projects\CFDSolver\resources\reference_spickzettel\INDEX.md`
- Tests: `tests/test_super_elements_and_architecture_m5c.py`, `tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`

## Forensic Verification Mandate
1. **Zero-Shortcut & Anti-Cheat Audit**:
   - Inspect all newly created and modified files (`backend/meshing/super_elements.py`, `backend/meshing/models.py`, `backend/meshing/native/src/difvm_mesh.cpp`, `backend/meshing/native/include/difvm_mesh.hpp`, `backend/native_rust/c_abi_lake.py`, `backend/native_rust/expression_engine.py`, `backend/native_rust/mphys_container.py`).
   - Confirm zero stubs, zero dummy/mock buffers, zero fake returns, zero tautological assertions.
   - Confirm zero heuristic clamps.
   - Confirm fail-fast error checking.
2. **SMR Excision Audit**:
   - Verify zero occurrences of `smrKineticsSource` or SMR kinetics in any of the newly modified/created files.
3. **Out-of-Scope Boundary Audit**:
   - Verify zero frontend UI (`egui`), shaders, or `wgpu` renderers introduced in backend/tests.
4. **Execution & Test Verification**:
   - Run pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
   - Verify all 104 tests pass genuinely.

Deliver verdict as CLEAN (PASS) or INTEGRITY VIOLATION in `handoff.md` and report via send_message.


## 2026-11-14T00:40:37Z
Sender: ea8f7ac6-187f-461c-b8f2-aeda0693e3d8
Content:
You are auditor_m5c, a teamwork_auditor performing Anti-Cheat & Forensic code-path verification for Milestone M5C.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5c

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5c\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md
6. g:\Datein\Projects\CFDSolver\SUGGESTIONS_BACKEND.md (§10 & §11.2)

Tasks:
- Inspect newly created and modified files: backend/meshing/super_elements.py, backend/meshing/models.py, backend/meshing/native/src/difvm_mesh.cpp, backend/native_rust/c_abi_lake.py, backend/native_rust/expression_engine.py, backend/native_rust/mphys_container.py.
- Verify Zero-Shortcut Policy: no stubs, no fake returns, no tautological assertions, no silent clamps, no heuristic clamps.
- Verify SMR excision across codebase (zero occurrences of smrKineticsSource or SMR kinetics).
- Verify zero out-of-scope frontend UI/renderer code.
- Run sequential pytest under OMP_NUM_THREADS=2 across all test suites:
  .\\.venv\\Scripts\\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
- Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m5c\handoff.md with explicit CLEAN (PASS) or INTEGRITY VIOLATION verdict.
- Report verdict and summary via send_message to orchestrator.
