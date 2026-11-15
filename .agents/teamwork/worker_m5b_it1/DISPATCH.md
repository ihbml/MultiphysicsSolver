# Dispatch for worker_m5b_it1

## Mission & Scope
Implement Milestone M5B: Direct Code Integration of Surveyed Papers (Features F55–F61) and Incorporation of FVM/GPU Numerics & HPC Contracts (§12.2, Snippets 12–14, and line 630 SMR cleanup in SUGGESTIONS_BACKEND.md).

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Detailed Objectives

1. **Paper 6 / Audit H9 (`DiscreteGCL_1709.04788v4.pdf`) in `backend/meshing/moving_mesh.py`**:
   - Exact 3D swept volume via Simpson's rule:
     $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$
   - BDF2-DGCL grid volume flux:
     $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$
   - Eliminate any heuristic clamps (such as `0.85 * max_d` boundary heuristic).
   - Verify discrete geometric conservation law: $\sum_{f \in \text{cell}} \dot{V}_f^{n+1} = \frac{3 V^{n+1} - 4 V^n + V^{n-1}}{2 \Delta t}$ with machine precision (residual $< 10^{-12}$).

2. **Paper 7 (`Demirdzic-2022`) in `polymesh_io.py`, `difvm_topology.py`, and `case_generator.py`**:
   - Construct the mirror cell center:
     $\boldsymbol{\Delta} = \mathbf{c}_f + \frac{(\mathbf{c}_f - \mathbf{c}_P)\cdot\mathbf{s}_f}{\mathbf{s}_f\cdot\mathbf{s}_f}\mathbf{s}_f$
     ensuring $\boldsymbol{\Delta} - \mathbf{c}_P \parallel \mathbf{s}_f$ (collinear, zero non-orthogonal cross-diffusion: $\mathbf{k}_f \equiv \mathbf{0}$).
   - Householder reflection tensor:
     $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ where $\mathbf{n} = \mathbf{s}_f / \|\mathbf{s}_f\|$.
   - Vector diffusion split:
     $D_{vi,\text{im}} = \Gamma_v \frac{s_i^2}{\mathbf{d}\cdot\mathbf{s}}$ on matrix diagonal $a_{Pi}$,
     $D_{vi,\text{ex}} = -\frac{\Gamma_v}{\mathbf{d}\cdot\mathbf{s}}((\mathbf{s}\cdot\mathbf{v}_P) - s_i v_{Pi}) s_i$ on RHS.
   - Symmetrical rank-2 tensor diffusion split:
     $D_{Tij,\text{im}} = \Gamma_T \frac{s_i^2 + s_j^2}{\mathbf{d}\cdot\mathbf{s}}$.

3. **Paper 1 (`CADWatertightFineMeshing_2604.02141v1.pdf`) in `step_parser.py`**:
   - Topology-first B-Rep meshing using 3D topological entities $(\mathcal{V}^b, \mathcal{E}^b, \mathcal{L}^b, \mathcal{F}^b)$ as strict constraints.
   - Jordan curve loop separation: distinguish Outer Bound (disk) and Inner Bounds (holes/annulus) based on winding orientation and enclosed domain.
   - 1D parameter boundary curve stitching: stitch shared edges across adjacent patches using parameter point union to ensure 100% watertightness without cracks.

4. **Paper 2 (`Differtialble3DCAD_2110.01182v1.pdf`) in `differentiable_cad.py`**:
   - Parametric CAD DAG compilation: $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$.
   - Adjoint vector-Jacobian chain rule evaluation: $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}} = (\frac{\partial J}{\partial \mathbf{x}})^T \frac{\partial \mathbf{x}}{\partial \boldsymbol{\theta}}$ in $\mathcal{O}(N_v)$ time.
   - Compiling automatic geometric safety inequality constraints: $g_m(\boldsymbol{\theta}) \ge 0$ (e.g. chamfer/fillet radius $\le$ adjacent edge length) to prevent inverted CAD states.

5. **Paper 3 (`Continuous_Mesh_Framework_Part_II`) in `mesh_engine.py` & `octree_hexcore.cpp`**:
   - $L^p$-metric tensor sizing:
     $\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ ($p=1, 2$) without heuristic clamps.
   - Noise-free $P_2$ edge midpoint reconstruction.

6. **Papers 4 & 5 (Ray 2018 / Numerow 2024) in `backend/meshing/gpu_voronoi.py`**:
   - Dual GPU Voronoi representation: `float4 P[64]`, `int3 T[96]` with radius-of-security termination.
   - Analytical 1st and 2nd derivatives across topology flips via $A \mathbf{x}_c = b \implies \frac{\partial \mathbf{x}_c}{\partial \mathbf{x}_k} = A^{-1} \frac{\partial b}{\partial \mathbf{x}_k} - A^{-1} \frac{\partial A}{\partial \mathbf{x}_k}\mathbf{x}_c$ ensuring $C^1$ volumes.

7. **Paper 8 (`FSI_Monolithic_2505.22920v1.pdf`) in `backend/physics_modules/monolithic_fsi.py`**:
   - Monolithic 3D Eulerian FSI: one-continuum mixture stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$.
   - Neo-Hookean solid elasticity with Oldroyd-B transport of left Cauchy-Green tensor $\mathbf{B}$.
   - 3D geometric PLIC interface reconstruction.
   - Crucial stability reset: $\mathbf{B} = \mathbf{I}$ whenever solid volume fraction $\phi < 10^{-8}$.

8. **Incorporation of §12.2 Numerics Contracts & Snippets 12–14**:
   - Linear-exact plane intersection $\beta_f = \frac{(\mathbf{x}_f - \mathbf{x}_P)\cdot\mathbf{S}_f}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\alpha_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\mathbf{m}_f^{\text{skew}} = \mathbf{x}_f - (\mathbf{x}_P + \beta_f\mathbf{d}_f)$, $\mathbf{k}_f^{\text{corr}} = \mathbf{S}_f - \alpha_f\mathbf{d}_f$.
   - Explicit `FaceClass::invalid` rejection for $\cos\theta_f \le 10^{-8}$ or non-finite values (no silent clamps).
   - Gradient-free convective upwind deferred correction in Class 0 (M-matrix dominance without loading $\nabla\phi$).
   - Atomic-free `CellFaceCSR` gather pattern.
   - 3-Map `MeshErrorMap` separation (`GeometryMap`, `SolutionIndicator`, `ErrorEstimator`) and SPD Cholesky metric factorization $\mathcal{M}(\mathbf{x}) = \mathbf{L}\mathbf{L}^T + \epsilon\mathbf{I} \succ 0$.
   - Tangential CAD projection + 1-step Newton re-projection onto $\psi(\mathbf{x}) = 0$.

9. **SMR Excision in `SUGGESTIONS_BACKEND.md:630`**:
   - Clean up the remaining passing mention of `smrKineticsSource.C` in `SUGGESTIONS_BACKEND.md:630` (e.g. replace with `genericReactionSource.C`) so zero SMR references remain anywhere in the project.

## Verification Requirements
- Create `tests/test_paper_integration_m5b.py` covering all features F55–F61 and §12 additions.
- Run sequential pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
  `.\\.venv\\Scripts\\python.exe -m pytest tests/test_paper_integration_m5b.py -v`
- Run existing regression tests (including `tests/test_generic_reaction_and_openfoam_audits.py` and `tests/test_wp4_multiphysics_hexcore.py`) to verify no regressions.
- Write full self-contained `handoff.md` with Observation, Logic Chain, Caveats, Conclusion, and Verification Method.
