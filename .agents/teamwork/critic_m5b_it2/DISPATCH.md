# Dispatch for critic_m5b_it2

## Role & Mission
You are `critic_m5b_it2`, a `teamwork_critic` performing Mathematical Verification & Paper Fidelity Analysis for Milestone M5B Iteration 2.

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b_it2\handoff.md`
- Prior Critic Findings: `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m5b\handoff.md`
- Tests: `tests/test_paper_integration_m5b.py`

## Mathematical Verification Mandate
1. **Verify Remediation of F61 (`monolithic_fsi.py`)**:
   - Check `advect_cauchy_green_tensor` and `apply_fluid_stability_reset`: verify that single tensors `(3, 3)` and batched tensors `(N, 3, 3)` with scalar `phi` (`0.0`, `1.0`) evaluate without `TypeError` or shape mismatch. Verify that fluid reset to $\mathbf{I}_3$ triggers when $\phi < 10^{-8}$.
   - Check `compute_mixture_stress`: verify that batched velocity gradient `(N, 3, 3)` with scalar `phi` and `p` evaluates cleanly with correct shape `(N, 3, 3)` and mathematically exact mixture stress $\boldsymbol{\sigma} = \phi\boldsymbol{\sigma}^s + (1-\phi)\boldsymbol{\sigma}^f$.
2. **Verify Remediation of Coverage Gaps**:
   - Verify newly added tests in `tests/test_paper_integration_m5b.py`:
     - `test_advect_cauchy_green_tensor_single_and_batch`
     - `test_compute_mixture_stress_batched_scalar_inputs`
     - `test_three_map_mesh_error_map_evaluation`
     - `test_cad_dag_regularization_evaluation`
     - `test_step_parser_empty_curve_points_validation`
3. **Verify Features F55–F61b Mathematical Invariants**:
   - F55: Exact Simpson swept volume & BDF2-DGCL face flux conservation to machine precision ($< 10^{-13}$).
   - F56: Demirdzic symmetry mirror connection $\boldsymbol{\Delta} \parallel \mathbf{s}_f \implies \mathbf{k}_f \equiv \mathbf{0}$, Householder tensor $\det\mathbf{R} = -1$, and Patankar splits $D_{\text{im}} > 0$.
   - F57: B-Rep Dijkstra loop embedding, Jordan curve loop separation, 1D boundary stitching.
   - F58: Parametric CAD DAG forward evaluation, safety inequalities $g_m \ge 0$, reverse-mode VJP.
   - F59: Continuous Riemannian $L^p$-metric sizing $\mathcal{M}_{L^p}(\mathbf{x})$ and noise-free $P_2$ midpoint Hermite reconstruction.
   - F60: Dual GPU Voronoi (float4 P[64], int3 T[96]), radius-of-security, Stokes 6-tet volume, analytical sensitivity $d x_c / d c_i$, and $C^1$ volume continuity across flips.
   - F61b: Linear-exact plane intersection $\beta_f, \alpha_f$, explicit `FaceClass::INVALID` rejection, gradient-free M-matrix deferred correction, 3-Map separation, and SPD Cholesky factorization.
4. **Execution**:
   - Run tests sequentially under `OMP_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v`

Deliver verdict (APPROVE or REQUEST_CHANGES) in `handoff.md` and report via send_message.
