# Dispatch: Worker M5B (Paper Integration — Meshing, DGCL H9, Symmetry, FSI)

## Mission
Implement Milestone M5B incorporating methods from the surveyed papers directly into code modules:

1. **Feature F55 (Paper 6 & Audit H9: Exact Swept Volume & BDF2-DGCL)**:
   - In `backend/meshing/moving_mesh.py`:
     - Implement Simpson's rule exact 3D swept volume:
       $$\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$$
       where $\mathbf{x}_f^{n+1/2} = \frac{1}{2}(\mathbf{x}_f^n + \mathbf{x}_f^{n+1})$ and face area vector $\mathbf{S}_f^{n+1/2}$ is computed from midpoint vertex positions.
     - Implement BDF2-DGCL grid flux:
       $$\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$$
     - Eliminate the $0.85 \cdot d_{\max}$ boundary heuristic; identify Dirichlet fixed boundary nodes topologically from boundary face connectivity (`neighbour == -1` not in moving patch).
     - Verify DGCL conservation on 3D deforming hexahedron / polyhedral grids: $\sum_f \dot{V}_f - \frac{\mathrm{d}V_c}{\mathrm{d}t} < 10^{-12}$.

2. **Feature F56 (Paper 7: CC-FVM Symmetry Boundary & Householder Reflection Tensor)**:
   - In `backend/meshing/difvm_topology.py`, `backend/meshing/polymesh_io.py`, and `backend/core/case_generator.py`:
     - Implement orthogonal mirror center $\boldsymbol{\Delta} = 2 \frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$, proving $\boldsymbol{\Delta} \parallel \mathbf{s}$ and cross-diffusion $\mathbf{k}_f \equiv \mathbf{0}$.
     - Implement Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vector fields ($\mathbf{u}_{\text{mirror}} = \mathbf{R} \mathbf{u}_P$) and rank-2 stress tensors ($\boldsymbol{\sigma}_{\text{mirror}} = \mathbf{R} \boldsymbol{\sigma}_P \mathbf{R}^T$).
     - Implement unconditionally diagonally dominant Patankar split for symmetry boundaries.

3. **Feature F57 (Paper 1: Watertight B-Rep CAD Meshing)**:
   - In `backend/geometry/parsers/step_parser.py`:
     - Discard invalid 2D `PCURVE`s; perform 3D loop embedding on un-trimmed patch surface discretizations via Dijkstra shortest paths.
     - Implement combinatorial Jordan curve loop separation distinguishing outer loop (disk) from inner hole loops (annulus).
     - Implement 1D boundary parameter stitching across shared adjacent face curves so seams match node-for-node without cracks.

4. **Feature F58 (Paper 2: Differentiable 3D CAD DAG & Chain Rule)**:
   - In `backend/geometry/differentiable_cad.py`:
     - Compile CAD construction DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$.
     - Formulate geometric safety inequalities $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$ (e.g. non-self-intersection, feature clearance).
     - Implement reverse-mode adjoint vector-Jacobian product (VJP) chain rule $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}} = (\frac{\partial J}{\partial \mathbf{x}})^T \frac{\partial \mathbf{x}}{\partial \boldsymbol{\theta}}$.

5. **Feature F59 (Paper 3: Continuous Mesh $L^p$-Metric Sizing)**:
   - In `backend/meshing/mesh_engine.py` and `backend/meshing/native/src/octree_hexcore.cpp`:
     - Implement optimal continuous $L^p$-metric tensor sizing:
       $$\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$$
       for $p=1$ and $p=2$, without arbitrary heuristic clamps.
     - Implement noise-free $P_2$ edge midpoint reconstruction from cell vertex gradients.

6. **Feature F60 (Papers 4 & 5: Dual GPU Voronoi & Analytical Derivatives)**:
   - In `backend/meshing/difvm_topology.py` and `backend/meshing/voronoi_derivatives.py`:
     - Implement pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`) with radius-of-security early termination $\|\mathbf{x}_p - \mathbf{x}_i\| > 2 R_{\max}$.
     - Implement analytical vertex sensitivity $\mathbf{A} \mathbf{x}_c = \mathbf{b} \implies \frac{\partial \mathbf{x}_c}{\partial \mathbf{x}_k} = -\mathbf{A}^{-1} \frac{\partial \mathbf{A}}{\partial \mathbf{x}_k} \mathbf{x}_c + \mathbf{A}^{-1} \frac{\partial \mathbf{b}}{\partial \mathbf{x}_k}$, guaranteeing $C^1$ cell volume continuity across topology flips.

7. **Feature F61 (Paper 8: Monolithic Eulerian 3D FSI)**:
   - In `backend/physics_modules/fsi_monolithic.py`:
     - Implement one-continuum mixture stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$.
     - Implement Neo-Hookean solid elasticity $\boldsymbol{\sigma}^s = \frac{\mu_s}{J}(\mathbf{B} - \mathbf{I}) + \frac{\lambda_s}{J}(\ln J)\mathbf{I}$.
     - Implement 3D geometric PLIC interface reconstruction.
     - Implement crucial stability fluid reset: $\mathbf{B} \leftarrow \mathbf{I}$ whenever solid volume fraction $\phi < 10^{-8}$.

8. **Testing & Verification**:
   - Write comprehensive unit tests in `tests/test_paper_integration_m5b.py`.
   - Run tests sequentially under throttled settings:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest tests/test_paper_integration_m5b.py -v`
   - Verify regression baseline passes across existing tests.

## File Ownership
You exclusively own:
- `backend/meshing/moving_mesh.py`
- `backend/meshing/difvm_topology.py`
- `backend/geometry/parsers/step_parser.py`
- `backend/geometry/differentiable_cad.py`
- `backend/meshing/mesh_engine.py`
- `backend/meshing/voronoi_derivatives.py`
- `backend/physics_modules/fsi_monolithic.py`
- `tests/test_paper_integration_m5b.py`

## Authoritative Inputs
- Read `ORIGINAL_REQUEST.md`: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- Scope document: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`
- Spec Miner 2 Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\handoff.md`
- Spec Miner 2 Report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_m5_survey_2\report.md`
- Reference Compendium: `resources/papers/literature_survey_2026.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Constraints
- Throttling: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Sequential pytest execution. Test meshes $\le 30{,}000$ cells.
- Zero-Shortcut Policy: throw exceptions before crashes, no raw pointer leaks, strict bounds checks, no heuristic clamps, no swallowed exceptions.
- Output report in `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5b\handoff.md`.
