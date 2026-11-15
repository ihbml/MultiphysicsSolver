# Dispatch for reviewer_m5b

## Role & Mission
You are `reviewer_m5b`, a `teamwork_reviewer` performing Code Quality, Software Architecture, Memory Safety & Regression Testing for Milestone M5B (Features F55–F61, §12.2 contracts, and Snippets 12–14).

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it1\handoff.md`
- Codebase: `backend/meshing/`, `backend/geometry/`, `backend/physics_modules/`
- Tests: `tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`

## Code Quality & Stability Review Mandate
1. **Architecture & Clean Contracts**:
   - Verify modular design and clean interface contracts in `backend/meshing/moving_mesh.py`, `difvm_topology.py`, `polymesh_io.py`, `case_generator.py`, `step_parser.py`, `differentiable_cad.py`, `mesh_engine.py`, `gpu_voronoi.py`, `monolithic_fsi.py`.
   - Check error handling: fail-fast exceptions (`ValueError`, `RuntimeError`) on invalid inputs, zero swallowed exceptions or silent failure modes.
2. **Memory Safety & Hardware Throttling**:
   - Verify compliance with strict hardware bounds: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
   - Ensure grids $\le 30{,}000$ cells, zero memory leaks, ctypes / numpy array boundary safety.
   - Verify that sequential test execution is strictly adhered to.
3. **Adversarial Edge-Case Stress Testing**:
   - Check edge cases: degenerate cells, inverted faces ($\cos\theta_f \le 10^{-8}$), pure fluid ($\phi = 0$) and pure solid ($\phi = 1$) limits in FSI, uniform Hessian fields in metric sizing, non-convex polygons in Voronoi.
4. **Full Test Execution**:
   - Run sequential pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
   - Verify 100% test pass with zero regressions.

Deliver verdict (APPROVE or REQUEST_CHANGES) in `handoff.md` and report via send_message.


## 2026-11-13T23:31:35Z
You are reviewer_m5b, a teamwork_reviewer performing Code Quality, Software Architecture, Memory Safety & Regression Testing for Milestone M5B.

Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5b

Primary Inputs to Read:
1. g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. g:\Datein\Projects\CFDSolver\PROJECT.md
3. g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md
4. g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5b\DISPATCH.md
5. g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it1\handoff.md
6. g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\report.md

Tasks:
- Review codebase modifications in backend/meshing/, backend/geometry/, backend/physics_modules/.
- Verify code quality, modular architecture, and exception safety (fail-fast exceptions on invalid inputs, zero swallowed errors).
- Verify hardware throttling: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2, OPENBLAS_NUM_THREADS=2, test grids <= 30,000 cells.
- Verify adversarial robustness: test corner cases, degenerate geometry, boundary conditions.
- Run test suites sequentially under OMP_NUM_THREADS=2:
  .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
- Deliver structured handoff report in g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5b\handoff.md with explicit APPROVE or REQUEST_CHANGES verdict.
- Report verdict and summary via send_message to orchestrator.
