# BRIEFING — 2026-11-12T02:16:00Z

## Mission
Mathematical and numerical verification of Milestone M5A (genericReactionSource, multi-region/case generation, WSL telemetry).

## 🔒 My Identity
- Archetype: critic
- Roles: reviewer, critic, specialist
- Working directory: g:\Datein\Projects\CFDSolver
- State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a
- Original parent: 9f5c9a1b-0af4-4d60-8a57-51e97c47f400
- Milestone: M5A
- Instance: 1 of 1

## 🔒 Key Constraints
- Review-only — do NOT modify implementation code
- Sequential test execution under OMP_NUM_THREADS=2, MKL_NUM_THREADS=2, OPENBLAS_NUM_THREADS=2 via .\.venv\Scripts\python.exe -m pytest ...
- Verify mathematical, numerical, and paper fidelity of M5A work products

## Current Parent
- Conversation ID: 9f5c9a1b-0af4-4d60-8a57-51e97c47f400
- Updated: 2026-11-12T02:16:00Z

## Review Scope
- **Files to review**:
  - `backend/native_cpp/smr_catalysis/genericReactionSource.C`
  - `backend/native_cpp/smr_catalysis/genericReactionSource.H`
  - `backend/core/case_generator.py`
  - `backend/runner/wsl_runner.py`
  - `backend/meshing/polymesh_io.py`
  - `tests/test_generic_reaction_and_openfoam_audits.py`
- **Interface contracts**: `SCOPE.md` and `ORIGINAL_REQUEST.md`
- **Review criteria**: Mathematical correctness, stoichiometry, Arrhenius kinetics, diagonal stabilization, mass conservation, multi-region decomposition/boundary conditions, residual/Courant metrics, test execution and coverage

## Review Checklist
- **Items reviewed**:
  - `genericReactionSource.C` and `.H`: Reviewed lines 1-259
  - `case_generator.py`: Reviewed lines 1-594
  - `wsl_runner.py`: Reviewed lines 1-276
  - `polymesh_io.py`: Reviewed lines 170-450
  - `tests/test_generic_reaction_and_openfoam_audits.py`: Reviewed lines 1-664
- **Verdict**: REQUEST_CHANGES
- **Unverified claims**: Live execution under WSL2 OpenFOAM binary (mock/unit tests verified in Python)

## Attack Surface
- **Hypotheses tested**:
  - Sign convention of energy source term in `addSup`: FAILED (line 201 has `-=` instead of `+=`, inverting heat release)
  - Array bounds safety in `genericReactionSource.C` for `arrheniusEa_` and `reactionDeltaH_`: FAILED (warning emitted, but out-of-bounds access possible in `addSup`)
  - Species mass conservation $\sum \dot{\omega}_s M_s = 0$: PASSED
  - Species diagonal stabilization $- \partial \dot{m}_s / \partial Y_s > 0$: PASSED
  - Multi-region `regionProperties` syntax & decomposition: PASSED
  - 0/ fields SI dimensions & boundary condition formulations: PASSED
  - WSL runner path escaping & residual/Courant parsing: PASSED
- **Vulnerabilities found**:
  - Critical: Inverted energy source sign in `genericReactionSource.C` (line 201)
  - Major: Missing bounds check on kinetic parameter lists in `genericReactionSource.C` (lines 166, 187)
  - Minor: Missing positive value validation for `p_weight` / `e_weight` in `case_generator.py`
- **Untested angles**:
  - Conjugate heat transfer coupling across regions during live OpenFOAM run

## Loaded Skills
- None

## Key Decisions Made
- Issued verdict REQUEST_CHANGES due to critical mathematical sign inversion in `genericReactionSource.C` line 201 and potential out-of-bounds crash in kinetic array indexing.

## Artifact Index
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\handoff.md` — Final critique and verification report
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\progress.md` — Liveness heartbeat
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\DISPATCH.md` — Inbound dispatches
