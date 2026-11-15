# Dispatch for reviewer_m5c

## Role & Mission
You are `reviewer_m5c`, a `teamwork_reviewer` performing Code Quality, Software Architecture, Memory Safety & Regression Testing for Milestone M5C (Features F62 & F63).

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md`
- Codebase: `backend/meshing/`, `backend/native_rust/`, `backend/models.py`
- Tests: `tests/test_super_elements_and_architecture_m5c.py`, `tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`

## Code Quality & Stability Review Mandate
1. **Architecture & Clean Contracts**:
   - Verify modular design and clean interface contracts in `super_elements.py`, `models.py`, `c_abi_lake.py`, `expression_engine.py`, `mphys_container.py`, `difvm_mesh.cpp`.
   - Check error handling: fail-fast exceptions on invalid inputs, bounds checks, zero swallowed errors.
2. **Memory Safety & Alignment**:
   - Verify 64-byte alignment in `aligned_alloc_64` and `CAbiSpan` zero-copy extraction.
   - Verify that `.mphys` container reader/writer safely handles chunk boundaries, corrupt headers, and invalid footer magic without crashes.
3. **Hardware Throttling & Regression Immunity**:
   - Ensure `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
   - Run sequential pytest:
     `.\.venv\Scripts\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
   - Confirm all 104 tests pass cleanly with 0 failures and 0 regressions.

Deliver verdict (APPROVE or REQUEST_CHANGES) in `handoff.md` and report via send_message.


## 2026-11-14T00:40:52Z
You are reviewer_m5c, a teamwork_reviewer performing Code Quality, Software Architecture, Memory Safety & Regression Testing for Milestone M5C.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5c

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5c\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md
6. Codebase: backend/meshing/, backend/native_rust/, backend/models.py

Tasks:
- Review code quality, modular architecture, and exception safety in super_elements.py, models.py, c_abi_lake.py, expression_engine.py, mphys_container.py, difvm_mesh.cpp.
- Verify memory safety: 64-byte alignment in aligned_alloc_64 and CAbiSpan zero-copy extraction.
- Verify hardware throttling: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2, OPENBLAS_NUM_THREADS=2, test grids <= 30,000 cells.
- Perform adversarial stress testing: invalid bytecode, corrupt .mphys headers, invalid chunk sizes, out-of-bounds entity degrees.
- Run full sequential test suites under OMP_NUM_THREADS=2:
  .\.venv\Scripts\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
- Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5c\handoff.md with explicit APPROVE or REQUEST_CHANGES verdict.
- Report verdict and summary via send_message to orchestrator.
