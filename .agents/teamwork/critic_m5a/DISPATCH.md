# Dispatch: Critic M5A (Mathematik & Paper-Treue Verification)

## Mission
Perform mathematical and numerical verification of Milestone M5A:
1. Verify `genericReactionSource.C` and `.H`:
   - Check stoichiometric rate laws, species molar masses, Arrhenius reaction rates $k = A \exp(-E_a / (R T))$.
   - Verify implicit diagonal stabilization: check that for exothermic reactions ($\Delta H_r > 0$, energy sink) $\partial S_h / \partial h < 0$, contributing positive values to `eqn.diag()`.
   - Check species conservation: $\sum_k \dot{\omega}_k M_k = 0$ for complete mass balance.
2. Verify Multi-Region & Case Generation Mathematics (H5 & H6):
   - Check `regionProperties` generation: valid fluid/solid region partitions.
   - Check `processorWeights` decomposition: verify $\sum_{i=0}^{N-1} w_i > 0$ and weights match rank counts.
   - Check `0/` fields boundary condition formulations (`fixedValue`, `zeroGradient`, `mappedWall`, `calculated`).
3. Verify WSL Runner Numerical Telemetry (M10):
   - Check residual calculation formulas: initial residual, final residual, iterations.
   - Check Courant number monitoring: mean and max Courant numbers, NaN/divergence detection.
4. Run verification tests sequentially under `OMP_NUM_THREADS=2`.

## Authoritative Inputs
- Read `ORIGINAL_REQUEST.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- Scope document: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`
- Worker handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5a\handoff.md`

## Output Requirements
Produce `handoff.md` in your state folder with explicit verdict: `APPROVE` or `REQUEST_CHANGES`.
Send completion message to orchestrator.


## 2026-11-12T02:08:56Z
You are critic_m5a.
Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a
Authoritative request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Scope document: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md
Dispatch file: g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a\DISPATCH.md
Worker handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5a\handoff.md
