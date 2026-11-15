# Handoff Report: Critic M5A (Mathematik & Paper-Treue Verification)

**Agent:** critic_m5a  
**Working Directory:** `g:\Datein\Projects\CFDSolver`  
**State Folder:** `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5a`  
**Date:** 2026-11-12  
**Verdict:** **REQUEST_CHANGES**  
**Handoff Type:** Hard  

---

## 1. Observation

### 1.1 `genericReactionSource.C` Energy Source Sign Inversion vs Species Equation
In `backend/native_cpp/smr_catalysis/genericReactionSource.C`:
- **Lines 187–203 (Energy equation assembly)**:
  ```cpp
  const scalar deltaH = reactionDeltaH_[r];
  // Heat source/sink [W/m^3]
  const scalar q_reaction = -rateProgress * deltaH;

  totalQ += q_reaction;

  // Analytical Jacobian: dq/dT = q * (Ea / T^2), dq/dh = (dq/dT) / Cp
  const scalar dq_dh = q_reaction * (arrheniusEa_[r] / (Tc * Tc * cpVal));
  if (dq_dh < 0.0)
  {
      totalDqDh += dq_dh;
  }
  // ...
  source[cellI] -= (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
  diag[cellI]   -= totalDqDh * V[cellI]; // Add positive quantity to diagonal
  ```
- **Lines 237–254 (Species mass balance equation assembly)**:
  ```cpp
  const scalar massRate = nu_sr * molarMass_s * rateProgress;
  totalMassSource += massRate;

  if (nu_sr < -1e-6 && Y[s] != nullptr)
  {
      const scalar yVal = max(1e-6, (*Y[s])[cellI]);
      const scalar dRate_dY = (-nu_sr) * rateProgress / yVal;
      const scalar dMass_dY = nu_sr * molarMass_s * dRate_dY; // Negative
      totalDiagJac += dMass_dY;
  }
  // ...
  source[cellI] += (totalMassSource - totalDiagJac * eqn.psi()[cellI]) * V[cellI];
  diag[cellI]   -= totalDiagJac * V[cellI];
  ```
Notice that line 201 applies `-=` to `source[cellI]`, whereas line 252 applies `+=` to `source[cellI]`.

### 1.2 Unchecked Array Indexing in `genericReactionSource.C`
In `backend/native_cpp/smr_catalysis/genericReactionSource.C`:
- **Lines 62–70**:
  ```cpp
  const label nReactions = arrheniusA_.size();
  if (arrheniusEa_.size() != nReactions || reactionDeltaH_.size() != nReactions)
  {
      WarningInFunction
          << "Size mismatch in kinetic parameters: arrheniusA (" << nReactions
          << "), arrheniusEa (" << arrheniusEa_.size()
          << "), reactionDeltaH (" << reactionDeltaH_.size() << ")"
          << endl;
  }
  ```
- **Lines 164–187**:
  ```cpp
  for (label r = 0; r < nReactions; ++r)
  {
      scalar k_rate = arrheniusA_[r] * Foam::exp(-arrheniusEa_[r] / Tc);
      // ...
      const scalar deltaH = reactionDeltaH_[r];
  ```
`read()` emits only `WarningInFunction` and returns `true`. If `arrheniusEa_` or `reactionDeltaH_` has fewer elements than `arrheniusA_`, lines 166 and 187 perform out-of-bounds array reads.

### 1.3 `processorWeights` Decomposition in `case_generator.py`
In `backend/core/case_generator.py`, lines 107–120:
```python
def _write_decompose_par_dict(self, system_dir: Path, ranks: int, p_weight: float, e_weight: float):
    if ranks <= 0:
        raise ValueError(f"mpi_ranks must be a positive integer, got {ranks}")

    n_p = min(ranks, 8)
    n_e = max(0, ranks - 8)
    weights = [p_weight] * n_p + [e_weight] * n_e
```
For all $N \ge 1$: $\text{len}(\text{weights}) = \min(N, 8) + \max(0, N - 8) = N$. With default $p\_weight = 1.5, e\_weight = 1.0$, $\sum_{i=0}^{N-1} w_i > 0$. However, non-positive caller inputs (`p_weight <= 0` or `e_weight <= 0`) are unvalidated.

### 1.4 `0/` Field Boundaries & 7-Element SI Dimensions in `case_generator.py`
In `backend/core/case_generator.py`:
- Velocity $U$: `[0 1 -1 0 0 0 0]`, inlet `fixedValue`, wall `noSlip`, symmetry `symmetryPlane`.
- Pressure $p$: incompressible `[0 2 -2 0 0 0 0]`, compressible `[1 -1 -2 0 0 0 0]`, inlet `zeroGradient`, outlet `fixedValue`, wall `zeroGradient`.
- Temperature $T$: `[0 0 0 1 0 0 0]`, inlet `fixedValue`, outlet `zeroGradient`, mappedWall `compressible::turbulentTemperatureCoupledBaffleMixed`.
- Turbulence $k, \omega, \epsilon$: standard SI dimensions, wall functions on wall patches (`kqRWallFunction`, `omegaWallFunction`, `epsilonWallFunction`).
- Diffusivity $\alpha_t, \nu_t$: inlet `type calculated; value uniform 0;`.
- Species $Y_i$: `[0 0 0 0 0 0 0]`.

### 1.5 WSL Runner Telemetry & Convergence Monitoring in `wsl_runner.py`
In `backend/runner/wsl_runner.py`:
- `TIME_PATTERN`, `SOLVING_PATTERN`, `COURANT_PATTERN`, `NAN_PATTERN` reliably extract `time`, `courant_mean`, `courant_max`, `initial_residual`, `final_residual`, `iterations`.
- Catches `NaN`, `Infinity`, and `Floating point exception`, raising `FloatingPointError` when `raise_on_divergence=True`.
- Drive paths converted to `/mnt/<drive>/...`, quoted with `shlex.quote`.

### 1.6 Empirical Pytest Execution
Ran sequentially under thread constraints:
```powershell
$env:OMP_NUM_THREADS='2'; $env:MKL_NUM_THREADS='2'; $env:OPENBLAS_NUM_THREADS='2'
.\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py tests/test_phase4_numerics_topology.py tests/test_generic_reaction_and_openfoam_audits.py -v
```
Result: **51 passed in 1.11s** (22 new M5A tests + 29 regression baseline tests).

---

## 2. Logic Chain

### 2.1 Derivation of OpenFOAM `fvMatrix` Semi-Implicit Linearization
1. OpenFOAM finite-volume discretization of a scalar PDE $\mathcal{L}(\psi) = S(\psi)$ yields:
   $$A_P \psi_P + \sum_{N} A_N \psi_N = b_P^{\text{orig}} + \int_{V_P} S(\psi) \, dV$$
   where `eqn.diag()[P]` corresponds to $A_P$ on the LHS, and `eqn.source()[P]` corresponds to the RHS vector $b_P$.
2. Approximating the integral over cell volume $V_P$:
   $$\int_{V_P} S(\psi) \, dV \approx S_P(\psi_P) V_P$$
3. First-order Taylor series linearization of $S_P(\psi_P)$ around the current iterate $\psi_P^*$:
   $$S_P(\psi_P) \approx S_P(\psi_P^*) + \left.\frac{\partial S}{\partial \psi}\right|_P (\psi_P - \psi_P^*) = \left[ S_P(\psi_P^*) - \left.\frac{\partial S}{\partial \psi}\right|_P \psi_P^* \right] + \left.\frac{\partial S}{\partial \psi}\right|_P \psi_P$$
4. Transferring the linear implicit term to the LHS:
   $$\left( A_P - \left.\frac{\partial S}{\partial \psi}\right|_P V_P \right) \psi_P + \sum_{N} A_N \psi_N = b_P^{\text{orig}} + \left( S_P(\psi_P^*) - \left.\frac{\partial S}{\partial \psi}\right|_P \psi_P^* \right) V_P$$
5. For Scarborough / M-matrix diagonal dominance:
   $$\Delta A_P = - \left.\frac{\partial S}{\partial \psi}\right|_P V_P \ge 0 \iff \left.\frac{\partial S}{\partial \psi}\right|_P \le 0$$
6. Therefore, the exact OpenFOAM matrix element updates MUST be:
   - `diag[P]   -= dS_dpsi * V[P];` (subtracting negative derivative adds positive quantity to diagonal)
   - `source[P] += (S_val - dS_dpsi * psi_star) * V[P];` (adding explicit remainder to RHS)

### 2.2 Proof of Energy Equation Defect in `genericReactionSource.C`
1. In `genericReactionSource.C` line 252 (Species mass equation), the update is:
   ```cpp
   source[cellI] += (totalMassSource - totalDiagJac * eqn.psi()[cellI]) * V[cellI];
   diag[cellI]   -= totalDiagJac * V[cellI];
   ```
   This matches step 6 exactly.
2. In `genericReactionSource.C` line 201 (Energy equation), the update is:
   ```cpp
   source[cellI] -= (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
   diag[cellI]   -= totalDqDh * V[cellI];
   ```
3. Expanding the resulting linear system from line 201:
   $$\left( A_P - \left.\frac{\partial q}{\partial h}\right|_P V_P \right) h_P + \sum_{N} A_N h_N = b_P^{\text{orig}} - \left( Q_P - \left.\frac{\partial q}{\partial h}\right|_P h_P^* \right) V_P$$
   Rearranging to compare LHS and RHS:
   $$A_P h_P + \sum_{N} A_N h_N = b_P^{\text{orig}} - Q_P V_P + \left.\frac{\partial q}{\partial h}\right|_P V_P (h_P^* + h_P)$$
4. Consequences of the `-=` sign error:
   - For an exothermic reaction ($\Delta H_r < 0 \implies Q_P = -r \Delta H_r > 0$): the source $-Q_P V_P$ is subtracted from the RHS, causing the fluid to falsely cool down instead of heat up.
   - For an endothermic reaction ($\Delta H_r > 0 \implies Q_P < 0$): the term $-Q_P V_P$ is positive, causing the fluid to falsely heat up.
   - The linear relaxation term $\left.\frac{\partial q}{\partial h}\right|_P V_P (h_P^* + h_P)$ has the inverted sign and does not cancel at convergence ($h_P = h_P^*$), doubling the perturbation error and destabilizing convergence.

### 2.3 Proof of Species Conservation
1. Reaction progress rate: $\mathcal{R}_r = k_r \prod_{k, \nu_{k,r} < 0} [C_k]^{-\nu_{k,r}}$.
2. Species mass source: $\dot{m}_s = \sum_{r} \nu_{s,r} M_s \mathcal{R}_r$.
3. Summing over all species:
   $$\sum_{s=1}^{N_s} \dot{m}_s = \sum_{r=1}^{N_r} \mathcal{R}_r \left( \sum_{s=1}^{N_s} \nu_{s,r} M_s \right) = 0$$
   since by stoichiometric mass balance, $\sum_{s} \nu_{s,r} M_s \equiv 0$ for each elementary reaction.
4. Species conservation is mathematically sound.

### 2.4 Bounds Checking and Zero-Shortcut Compliance
1. In `genericReactionSource.C`, when `arrheniusEa_.size() != arrheniusA_.size()` or `reactionDeltaH_.size() != arrheniusA_.size()`, the code logs a `WarningInFunction` and continues.
2. In `addSup`, the loops access `arrheniusEa_[r]` and `reactionDeltaH_[r]` without guarding index bounds against `arrheniusEa_.size()` or `reactionDeltaH_.size()`.
3. Under Zero-Shortcut Policy ("throw exceptions before crashes, strict bounds checks"), this must call `FatalErrorInFunction` or return `false` during `read()`, rather than permitting out-of-bounds memory reads during solver execution.

---

## 3. Caveats

1. **WSL OpenFOAM Runtime**: The OpenFOAM native library `libgenericReactionSource.so` was reviewed through source analysis and Python unit/mock tests. Live OpenFOAM binary execution requires a functional WSL2 OpenFOAM environment on the target host.
2. **PIMPLE Multi-Stage Telemetry**: In `wsl_runner.py`, `curr.residuals[field_name]` overwrites previous outer iterations within the same time step. This captures the final convergence state of the time step, but does not preserve intermediate outer corrector iterations.
3. **Subsequent Paper Milestones**: Mesh deformation DGCL (Paper 6 / Audit H9), Householder symmetry (Paper 7), B-Rep watertight CAD (Paper 1), and Eulerian FSI (Paper 8) are assigned to Milestone M5B.

---

## 4. Conclusion

Milestone M5A has achieved comprehensive architectural generalization, complete SMR-Xu-Froment excision, valid multi-region export, and robust WSL runner hardening. However, **mathematical verification identified a critical sign error and a bounds check defect in `genericReactionSource.C`**.

Therefore, the verdict is **REQUEST_CHANGES**.

### Required Action Items for Worker M5A:
1. **Critical — Fix Energy Source Assembly Sign (`genericReactionSource.C:201`)**:
   Change:
   ```cpp
   source[cellI] -= (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
   ```
   to:
   ```cpp
   source[cellI] += (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];
   ```
   This ensures identical mathematical consistency with species line 252, proper Patankar diagonal stabilization, and correct physical heating/cooling direction.
2. **Major — Enforce Strict Fail-Fast Validation in `genericReactionSource.C:read`**:
   Replace `WarningInFunction` with `FatalErrorInFunction` (or return `false`) if `arrheniusEa_.size() != nReactions` or `reactionDeltaH_.size() != nReactions`, preventing out-of-bounds memory reads.
3. **Minor — Validate Core Weights in `case_generator.py`**:
   Add `if p_weight <= 0 or e_weight <= 0: raise ValueError(...)` in `_write_decompose_par_dict`.

---

## 5. Verification Method

### 5.1 Sign Inversion Verification
Inspect `backend/native_cpp/smr_catalysis/genericReactionSource.C` lines 201 and 252:
```powershell
Select-String -Path backend/native_cpp/smr_catalysis/genericReactionSource.C -Pattern "source\[cellI\]"
```
Both occurrences MUST use `+=`.

### 5.2 Test Suite Execution
Run the full sequential verification test suite under hardware-throttled environment:
```powershell
$env:OMP_NUM_THREADS='2'; $env:MKL_NUM_THREADS='2'; $env:OPENBLAS_NUM_THREADS='2'
.\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py tests/test_phase4_numerics_topology.py tests/test_generic_reaction_and_openfoam_audits.py -v
```

### 5.3 Invalidation Conditions
This critique is invalidated only if an authoritative OpenFOAM specification or solver convention proves that `EEqn` defines `eqn.source()` on the LHS with reversed sign relative to `YEqn` and `semiImplicitSource.C`.
