# Handoff Report: Milestone M5B Iteration 2 Targeted Remediations

**Agent**: `worker_m5b_it2`  
**Milestone**: M5B (Direct Code Integration of Surveyed Papers F55–F61, §12.2 Numerics Contracts)  
**Parent Conversation ID**: `ea8f7ac6-187f-461c-b8f2-aeda0693e3d8`  
**Date**: 2026-11-14  
**Handoff Type**: Hard  

---

## 1. Observation

Direct code observations from Iteration 1 review findings and subsequent remediations:

1. **Broadcasting Defects in `backend/physics_modules/monolithic_fsi.py`**:
   - In `apply_fluid_stability_reset`: Calling with single tensor `B` of shape `(3, 3)` via `advect_cauchy_green_tensor` resulted in `B_next` having shape `(1, 3, 3)`. Because `B_out.ndim == 3`, `is_single` evaluated to `False`. When `phi` was a scalar float (e.g. `0.0`), `phi_arr = np.asarray(phi)` was 0-D (`shape ()`), causing `len(phi_arr)` at line 209 to throw `TypeError: len() of unsized object`.
   - In `compute_mixture_stress`: When `u_grad` was batched `(N, 3, 3)` and `phi` or `p` was a scalar float, `is_single` was `False`, leaving `phi_val` and `p_val` as 0-D arrays. Attempting to index `phi_val[i]` or `p_val[i]` at lines 163–164 threw `IndexError: invalid index to scalar variable`.

2. **Missing Validation on Empty Point Arrays in `backend/geometry/parsers/step_parser.py`**:
   - In `StepTopologyParser.stitch_1d_boundary_curves`: Calling with empty `shared_curve_points = np.empty((0, 3))` passed directly to `scipy.spatial.cKDTree`, raising `ValueError: Points array is empty` without domain-specific validation.
   - In `StepTopologyParser.dijkstra_loop_embedding`: Passing empty `curve_3d_points` similarly bypassed fail-fast domain validation.

3. **Missing Test Coverage in `tests/test_paper_integration_m5b.py`**:
   - `advect_cauchy_green_tensor` was previously never executed in tests with `phi` or batched inputs.
   - `compute_mixture_stress` was only tested with scalar inputs on single tensors.
   - `compute_three_map_mesh_error_map` was imported but lacked test coverage.
   - `evaluate_regularization` in `CadComputationDAG` was not exercised.

4. **Remediation Code Modifications**:
   - In `backend/physics_modules/monolithic_fsi.py`:
     - Updated `apply_fluid_stability_reset` to convert `phi` to a 1D array using `np.atleast_1d`. If `len(phi_arr) == 1` and `N > 1`, `phi_arr` is broadcast via `np.broadcast_to(phi_arr, (N,))`. Dimension mismatch throws `ValueError`.
     - Updated `compute_mixture_stress` to broadcast `phi_val` and `p_val` to length `N` when `len == 1` and `N > 1`. Also tiled `B_mat` if shape `(3, 3)` was supplied for batch $N > 1$. Mismatches throw `ValueError`.
     - Updated `advect_cauchy_green_tensor` signature to accept `phi: Optional[Union[float, np.ndarray]] = None`.
   - In `backend/geometry/parsers/step_parser.py`:
     - Added empty check for `shared_curve_points` in `stitch_1d_boundary_curves`: throws `ValueError("shared_curve_points must not be empty.")`.
     - Added empty checks for `curve_3d_points` and `vertices` in `dijkstra_loop_embedding`: throws `ValueError`.
   - In `tests/test_paper_integration_m5b.py`:
     - Added `test_advect_cauchy_green_tensor_single_and_batch` verifying Oldroyd-B advection and fluid reset for single and batched tensors with scalar and array `phi`.
     - Added `test_compute_mixture_stress_batched_scalar_inputs` verifying mixture stress calculation for batched gradients with scalar `phi` and `p`, pure fluid limit, pure solid limit, and validation errors.
     - Added `test_three_map_mesh_error_map_evaluation` verifying `GeometryMap`, `SolutionIndicator`, and `ErrorEstimator` structures and numerical values on an unstructured two-cube mesh.
     - Added `test_cad_dag_regularization_evaluation` verifying biharmonic and volume conservation regularization energies and finite difference gradient agreement.
     - Added `test_step_parser_empty_curve_points_validation` verifying `ValueError` on empty curve arrays.

5. **Test Suite Verification Results**:
   - Target test suite `tests/test_paper_integration_m5b.py`:
     - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v`
     - Result: `33 passed in 0.64s`.
   - Full sequential test suite:
     - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v`
     - Result: `80 passed in 195.83s (0:03:15)`. Zero failures, zero warnings.
   - Independent verification snippet from review panel: PASSED (`Verification passed!`).

---

## 2. Logic Chain

1. **Root Cause Identification**:
   - The adversarial stress-testing in Iteration 1 accurately identified that `monolithic_fsi.py` assumed homogeneous dimensionality across tensor and scalar fields, failing on 0-D scalar floats passed into batch routines.
   - `step_parser.py` lacked fail-fast guard clauses for empty array inputs before invoking `scipy.spatial.cKDTree`.
2. **Implementation of Minimal Compliant Fixes**:
   - In `monolithic_fsi.py`, adopting `np.atleast_1d` combined with `np.broadcast_to` guarantees that both scalar floats (`phi=0.0`, `p=101325.0`) and 1D arrays of matching length $N$ evaluate without indexing or sizing exceptions.
   - In `step_parser.py`, explicitly validating that points arrays are non-empty prevents opaque lower-level scipy exceptions and enforces the fail-fast principle.
3. **Comprehensive Coverage & Regression Immunity**:
   - Adding 5 dedicated test functions directly tests the previously untested code paths in `monolithic_fsi.py`, `step_parser.py`, `difvm_topology.py`, and `differentiable_cad.py`.
   - Running the full suite under `OMP_NUM_THREADS=2` confirms that all 80 tests across M5B and prior regressions pass without regressions.

---

## 3. Caveats

- **No Caveats**: All requested remediations from the Iteration 1 review panel have been fully implemented and verified against both unit and end-to-end integration tests.

---

## 4. Conclusion

All defects identified by `critic_m5b` and `reviewer_m5b` have been resolved with clean, mathematically robust logic:
- Array broadcasting in `apply_fluid_stability_reset` and `compute_mixture_stress` supports scalar floats, single cells, and batched cell arrays seamlessly.
- Empty curve point validation in `StepTopologyParser` is active and guarded.
- All four requested test functions (plus boundary validation tests) have been added to `tests/test_paper_integration_m5b.py`.
- 80/80 tests pass sequentially under `OMP_NUM_THREADS=2`.

Milestone M5B is complete and ready for final audit.

---

## 5. Verification Method

1. **Verify M5B Target Suite**:
   ```powershell
   $env:OMP_NUM_THREADS = "2"
   .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v
   ```
   *Expected Result*: 33 passed in < 1s.

2. **Verify Full Sequential Regression Suite**:
   ```powershell
   $env:OMP_NUM_THREADS = "2"
   $env:MKL_NUM_THREADS = "2"
   $env:OPENBLAS_NUM_THREADS = "2"
   .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py tests/test_generic_reaction_and_openfoam_audits.py tests/test_wp4_multiphysics_hexcore.py -v
   ```
   *Expected Result*: 80 passed in ~195s.

3. **Verify Independent Stress Test**:
   ```powershell
   .\.venv\Scripts\python.exe -c "
   import numpy as np
   from backend.physics_modules.monolithic_fsi import EulerianMonolithicFSI

   fsi = EulerianMonolithicFSI()
   B_single = np.eye(3)
   u_single = np.zeros(3)
   ug_single = np.array([[0, 1e5, 0], [0, 0, 0], [0, 0, 0]])

   # 1. Single cell pure fluid -> must reset to I
   B_adv = fsi.advect_cauchy_green_tensor(B_single, u_single, ug_single, dt=0.01, phi=0.0)
   assert np.allclose(B_adv, np.eye(3))

   # 2. Batch cells with scalar phi
   B_batch = np.tile(np.eye(3), (5, 1, 1))
   ug_batch = np.tile(ug_single, (5, 1, 1))
   sigma = fsi.compute_mixture_stress(0.5, ug_batch, 101325.0, B_batch)
   assert sigma.shape == (5, 3, 3)
   print('Verification passed!')
   "
   ```
   *Expected Result*: Prints `Verification passed!`.
