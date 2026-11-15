# Forensic Integrity Audit Report: Milestone M5B Iteration 2

**Agent**: `auditor_m5b_it2`  
**Milestone**: M5B (Surveyed Paper Integration F55–F61 & §12.2 Numerics Contracts)  
**Parent Conversation ID**: `ea8f7ac6-187f-461c-b8f2-aeda0693e3d8`  
**Date**: 2026-11-14  
**Audit Type**: Forensic Integrity & Anti-Cheat Verification  
**Profile**: General Project (Development Mode with Zero-Shortcut Policy)  
**Verdict**: **CLEAN (PASS)**

---

## 1. Observation

Direct code observations, forensic inspections, and empirical execution results:

### A. Code Inspection & Remediation Audit
1. **`backend/physics_modules/monolithic_fsi.py`**:
   - `compute_mixture_stress` (lines 144–170): Scalar inputs `phi` and `p` are converted using `np.atleast_1d` and broadcast via `np.broadcast_to(..., (N,))` when batch size $N > 1$. Tensors $B$ of shape `(3, 3)` are broadcast via `np.tile(..., (N, 1, 1))`. Dimension mismatches strictly throw `ValueError(f"phi length ... does not match velocity gradient batch size {N}")` and `ValueError(f"pressure p length ... does not match batch size {N}")`.
   - `apply_fluid_stability_reset` (lines 212–231): Converts `phi` with `np.atleast_1d` and broadcasts to `(N,)` for batched tensors. Successfully handles both 0-D scalar floats and 1D arrays without `TypeError: len() of unsized object`.
   - `advect_cauchy_green_tensor` (lines 233–298): Signature accepts `phi: Optional[Union[float, np.ndarray]] = None` and applies `apply_fluid_stability_reset` whenever `phi is not None`. Checks `if dt <= 0.0: raise ValueError(...)`.
   - Physics Formulation: Incompressible Neo-Hookean solid elasticity $\sigma_{ij}^s = 2\mu^s D_{ij} + G^s (B_{ij} - \delta_{ij})$ (Paper 8 Eq. 14), fluid stress $\sigma_{ij}^f = 2\mu^f D_{ij} - p\delta_{ij}$, one-continuum mixture stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$ (Paper 8 Eq. 7), Oldroyd-B upper-convected deformation transport (Paper 8 Eq. 16), and 3D PLIC interface reconstruction (Paper 8 §3.3).
   - Zero stubs, zero dummy buffers, zero heuristic clamps.

2. **`backend/geometry/parsers/step_parser.py`**:
   - `StepTopologyParser.dijkstra_loop_embedding` (lines 465–471): Added fail-fast validations:
     ```python
     if len(curve_pts) == 0 or curve_pts.size == 0:
         raise ValueError("curve_3d_points must not be empty.")
     if len(v_arr) == 0 or v_arr.size == 0:
         raise ValueError("vertices must not be empty.")
     ```
   - `StepTopologyParser.stitch_1d_boundary_curves` (lines 642–645): Added fail-fast validation:
     ```python
     if len(curve_pts) == 0 or curve_pts.size == 0:
         raise ValueError("shared_curve_points must not be empty.")
     ```
   - Prevents unhandled lower-level `scipy.spatial.cKDTree` exceptions and enforces robust input guarding.

3. **`tests/test_paper_integration_m5b.py`**:
   - Added 5 new, non-tautological test cases:
     - `test_advect_cauchy_green_tensor_single_and_batch` (lines 589–638)
     - `test_compute_mixture_stress_batched_scalar_inputs` (lines 639–688)
     - `test_three_map_mesh_error_map_evaluation` (lines 790–832)
     - `test_cad_dag_regularization_evaluation` (lines 363–402)
     - `test_step_parser_empty_curve_points_validation` (lines 281–297)
   - All tests use genuine analytical formulas, physical conservation laws, central finite differences ($h=10^{-6}$), or explicit `pytest.raises(ValueError)` checks.

### B. SMR Excision Verification
- Inspected `SUGGESTIONS_BACKEND.md` line 630: Verbatim content references `genericReactionSource.C`.
- Codebase Search: `git grep -i "smrKinetics"` yielded zero matches in code, tests, or configurations. Remaining occurrences are exclusively in historical requirements (`ORIGINAL_REQUEST.md`, `prompt_draft.md`) and handover logs (`COMMUNICATION_BACKEND_FRONTEND.md`).
- `git grep -i "Xu-Froment" backend/ tests/` returned 0 matches.
- `git grep -i "Steam Methane" backend/ tests/` returned 0 matches.

### C. Out-of-Scope Boundary Verification
- Searched `backend/` and `tests/` for frontend presentation artifacts:
  - `git grep -i "egui" backend/ tests/` $\implies$ 0 matches.
  - `git grep -i "wgpu" backend/ tests/` $\implies$ 0 matches.
  - `git grep -i "shader" backend/ tests/` $\implies$ 0 matches.
- Strict backend boundary maintained.

### D. Empirical Test Execution
1. **Target Suite (`tests/test_paper_integration_m5b.py`)**:
   - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v`
   - Result: `33 passed in 0.62s`.
2. **Full Sequential Regression Suite (`M5B + M5A + Prior Regression`)**:
   - Command: `cmd /c "set OMP_NUM_THREADS=2 && set MKL_NUM_THREADS=2 && set OPENBLAS_NUM_THREADS=2 && .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v"`
   - Result: `80 passed in 208.73s (0:03:28)`.
   - Exit code: 0. Zero failures, zero warnings.

---

## 2. Logic Chain

1. **Defect Remediation Verification**:
   - Iteration 1 review revealed broadcasting errors in `monolithic_fsi.py` when handling 0-D scalar floats, and lack of guard clauses on empty point arrays in `step_parser.py`.
   - Direct source code inspection confirms that `np.atleast_1d` + `np.broadcast_to` cleanly resolves the scalar broadcasting issue without altering physical behavior.
   - Guard clauses raising `ValueError` in `step_parser.py` satisfy the fail-fast principle.
2. **Anti-Cheat & Zero-Shortcut Assessment**:
   - Every module implements genuine physics from the surveyed literature (exact Simpson swept volume, BDF2-DGCL flux, Householder $\mathbf{R}$, Demirdzic orthogonal mirror $\boldsymbol{\Delta} \parallel \mathbf{s}_f$ with $\mathbf{k}_f \equiv \mathbf{0}$, Neo-Hookean solid elasticity, Oldroyd-B transport, PLIC reconstruction, M-matrix Class 0 deferred correction, linear-exact plane intersection with `FaceClass.INVALID` rejection).
   - No stubs, dummy buffers, or heuristic clamps exist in the audited code paths.
   - Tests execute real computations and assert against independent analytical references or finite differences.
3. **Hardware Constraint & Stability Verification**:
   - Pytest executed sequentially under strict thread caps (`OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`).
   - All 80 tests across M5B, M5A, and earlier milestones passed cleanly with deterministic execution.

---

## 3. Caveats

- **No Caveats**: All code paths, test suites, and boundary constraints for Milestone M5B have been verified empirically and found to be strictly compliant.

---

## 4. Conclusion

- **Verdict**: **CLEAN (PASS)**
- All requirements of Milestone M5B (Features F55–F61, §12.2 numerics contracts, SMR excision, zero frontend UI code) and all Iteration 2 remediations have been verified with complete integrity.

---

## 5. Verification Method

To independently reproduce the empirical verification:

1. **Verify M5B Target Suite**:
   ```powershell
   $env:OMP_NUM_THREADS = "2"
   .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v
   ```
   *Expected*: `33 passed in ~0.6s`.

2. **Verify Full Sequential Regression Suite**:
   ```cmd
   set OMP_NUM_THREADS=2
   set MKL_NUM_THREADS=2
   set OPENBLAS_NUM_THREADS=2
   .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
   ```
   *Expected*: `80 passed in ~208s`.

3. **Verify SMR Excision**:
   ```powershell
   git grep -i "smrKinetics" backend/ tests/
   ```
   *Expected*: Exit code 1 (no matches).
