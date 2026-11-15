# Dispatch: Worker M5A Fix (Milestone M5A Iteration 2 Remediation)

## Mission
Remediate the exact findings identified by `critic_m5a` and `reviewer_m5a`:

1. **`genericReactionSource.C`: Memory Lifecycle / Pointer Safety**:
   - In `backend/native_cpp/smr_catalysis/genericReactionSource.C`, replace local `PtrList<const volScalarField> Y` with non-owning `List<const volScalarField*> Y(nSpecies, static_cast<const volScalarField*>(nullptr))`.
   - Populate `Y[s] = &mesh_.lookupObject<volScalarField>(speciesNames_[s]);` when found.
   - Do NOT use `PtrList` because its destructor calls `delete` on the mesh-registered fields, corrupting the mesh objectRegistry.

2. **`genericReactionSource.C`: Energy Source Matrix Assembly Sign**:
   - In line 201 of `backend/native_cpp/smr_catalysis/genericReactionSource.C`, change:
     ```cpp
     source[cellI] -= (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
     ```
     to:
     ```cpp
     source[cellI] += (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
     ```
     (Must use `+=` exactly as line 252 does for species mass sources, ensuring exothermic reactions add heat and linear relaxation perturbation cancels properly).

3. **`genericReactionSource.C`: Fail-Fast Bounds & Input Checking in `read()`**:
   - In `genericReactionSource.C`, upgrade the size mismatch warning in `read()`:
     ```cpp
     if (arrheniusEa_.size() != nReactions || reactionDeltaH_.size() != nReactions)
     {
         FatalErrorInFunction
             << "Size mismatch in kinetic parameters: arrheniusA (" << nReactions
             << "), arrheniusEa (" << arrheniusEa_.size()
             << "), reactionDeltaH (" << reactionDeltaH_.size() << ")"
             << exit(FatalError);
     }
     ```

4. **`genericReactionSource.C`: Safe min/max/clamp**:
   - Replace unqualified `clamp` with `Foam::min(Tmax_, Foam::max(Tmin_, T[cellI]))` or `std::clamp` (with `<algorithm>`).

5. **`case_generator.py`: Explicit `U` Solver Entry in `fvSolution`**:
   - In `backend/core/case_generator.py` `_write_fv_solution`, explicitly declare:
     ```foam
         U
         {
             solver          smoothSolver;
             smoother        symGaussSeidel;
             tolerance       1e-8;
             relTol          0.1;
         }
         "(h|e|T|k|omega|epsilon|alphat|nut|Yi|Ydefault.*)"
         {
             $U;
         }
         "(U|h|e|T|k|omega|epsilon|alphat|nut|Yi|Ydefault.*)Final"
         {
             $U;
             relTol          0;
         }
     ```
     This ensures `$U;` dictionary macro expansion succeeds at runtime in OpenFOAM.

6. **`case_generator.py`: Core Weights Validation**:
   - In `_write_decompose_par_dict`, validate:
     ```python
     if p_weight <= 0 or e_weight <= 0:
         raise ValueError(f"processor weights must be strictly positive, got p_weight={p_weight}, e_weight={e_weight}")
     ```

7. **Verification & Tests**:
   - Update `tests/test_generic_reaction_and_openfoam_audits.py` to add tests for the remediated behaviors:
     - Check `source[cellI] +=` in `genericReactionSource.C`.
     - Check `List<const volScalarField*>` non-owning pointers in `genericReactionSource.C`.
     - Check explicit `U` entry in `fvSolution` solvers dictionary.
     - Check `p_weight <= 0` / `e_weight <= 0` raises `ValueError`.
   - Run tests sequentially under throttled settings:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest tests/test_generic_reaction_and_openfoam_audits.py -v`
   - Run full regression tests to ensure 100% green.

## File Ownership
You exclusively own:
- `backend/native_cpp/smr_catalysis/genericReactionSource.C`
- `backend/native_cpp/smr_catalysis/genericReactionSource.H`
- `backend/core/case_generator.py`
- `tests/test_generic_reaction_and_openfoam_audits.py`

## Authoritative Inputs
- `ORIGINAL_REQUEST.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- `SCOPE.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`
- `GATE_STATUS.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\GATE_STATUS.md`
- Reviewer Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5a\handoff.md`
- Critic Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\handoff.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Produce `handoff.md` in your state folder when complete.


## 2026-11-12T02:22:05Z
[Message] timestamp=2026-11-12T02:22:05Z sender=9f5c9a1b-0af4-4d60-8a57-51e97c47f400 priority=MESSAGE_PRIORITY_HIGH content=You are worker_m5a_fix.
Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5a_fix
Authoritative request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Scope document: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md
Dispatch file: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5a_fix\DISPATCH.md
Gate status: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\GATE_STATUS.md
Reviewer handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m5a\handoff.md
Critic handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\handoff.md

You MUST read ORIGINAL_REQUEST.md, SCOPE.md, and DISPATCH.md before starting work.
