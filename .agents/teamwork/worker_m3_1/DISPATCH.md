## 2026-10-28T06:11:43Z
[Message] timestamp=2026-10-28T06:11:43Z sender=3e8c9327-d904-45bd-a4e2-64ef970ab34d priority=MESSAGE_PRIORITY_HIGH content=You are worker_m3_1, the Implementation Worker for Milestone M3 (Multiphysics Eulerian SPH-FVM Coupling & ALE Moving Mesh).

## Working Directory
Your assigned working directory is: `G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m3_1`
Maintain your `progress.md` with timestamps during execution.
Write your final report to: `G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m3_1\handoff.md`.

## Required Reading
You MUST read:
1. `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (authoritative user requirements)
2. `G:\Datein\Projects\CFDSolver\PROJECT.md` (master architecture, contracts)
3. `G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_mesh_1\spec_report.md` (detailed formulas for SPH divergence, A_iS, OPMIS Taylor moment-reproduction, and ALE Laplacian smoothing)
4. `G:\Datein\Projects\CFDSolver\tests\test_mesher.py` (inspect lines 260-375 and lines 510-585 for the exact contract requirements of the 5 skipped tests)

## MANDATORY INTEGRITY WARNING
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Objective & Scope of Milestone M3
Implement Features 12 to 16 in `backend/meshing/coupling.py` and `backend/meshing/moving_mesh.py`:

1. **`backend/meshing/coupling.py` (Features 12, 13, 14)**:
   - SPH Kernel: Implement Wendland C2 / cubic spline kernel $W(r, h)$ and spatial gradient $\nabla_i W_{ij}$ in 2D and 3D.
   - `SPHFVMCoupler`:
     * `detect_boundary_particles(pos, vol, h) -> np.ndarray`: Evaluates position divergence $\text{div }\mathbf{r}_i = -\sum_j V_j (\mathbf{r}_i - \mathbf{r}_j) \cdot \nabla_i W_{ij}$. In bulk interior, $\text{div }\mathbf{r} \approx d$. Tags boundary particles where $\text{div }\mathbf{r}_i < 0.75 d$. Returns boolean array of shape `(N,)`.
     * `compute_virtual_wall_vector(pos, vol, h, return_sum_grad=False) -> Union[np.ndarray, Tuple[np.ndarray, np.ndarray]]`: Evaluates $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ for boundary particles (and $\approx \mathbf{0}$ for interior). Guarantees exact 0th-order consistency $\|\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij}\| < 10^{-10}$.
   - `OPMISTaylorInterpolator`:
     * `interpolate_p2m(pos, field_particles, vol, h, mesh) -> np.ndarray`: Moment-reproducing Taylor interpolation from particles to mesh cell centres. Evaluates moment matrix $\mathbf{M}_{p, i} = \sum_j w_{ij} (\mathbf{x}_j - \mathbf{x}_i) \otimes (\mathbf{x}_j - \mathbf{x}_i)$, inverts, evaluates particle gradients $\nabla \phi_i$, and reconstructs field at cell centres $\phi(\mathbf{x}_c) = \phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i)$. Guarantees exact linear field reproduction with $L_\infty < 10^{-11}$.
     * `interpolate_m2p(mesh, field_mesh, query_points) -> np.ndarray`: Affine weighted least-squares interpolation from mesh cell centres to query points, reproducing arbitrary linear fields with $L_\infty < 10^{-11}$.

2. **`backend/meshing/moving_mesh.py` (Features 15, 16)**:
   - `ALEMeshMover`:
     * `deform_mesh(mesh: UnstructuredMesh, boundary_displacement: np.ndarray) -> UnstructuredMesh`: Solves variable-stiffness Laplacian smoothing $\nabla \cdot (\gamma \nabla \mathbf{u}) = \mathbf{0}$ for interior node displacements with boundary Dirichlet conditions. Updates points, recomputes DiFVM topology (`compute_difvm_topology`), and guarantees positive cell volumes $V_i > 10^{-15}$ and fixed boundaries remain fixed ($< 10^{-12}$).
     * `detect_degenerate_cells(mesh: UnstructuredMesh) -> List[int]`: Returns list of corrupted/inverted cell indices ($V_i \le 0$ or severe non-orthogonality / skewness).

3. **Package Integration & Documentation**:
   - Update `backend/meshing/__init__.py` to export `SPHFVMCoupler`, `OPMISTaylorInterpolator`, `ALEMeshMover`.
   - Update `backend/meshing/docs/COUPLING_SPH_FVM.md` and `ALE_MOVING_MESH.md`.
   - Unskip the 5 tests in `tests/test_mesher.py` (remove `@pytest.mark.skip`).
   - Author comprehensive unit tests in `tests/test_coupling.py` and `tests/test_moving_mesh.py`.

4. **Verification & Testing**:
   - Run: `& "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/test_mesher.py -v` -> Verify **all 14 tests pass (14 passed, 0 skipped, 0 failed)**!
   - Run: `& "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/test_coupling.py tests/test_moving_mesh.py -v`.
   - Run full regression suite: `& "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/ backend/geometry/tests/ -q`. Ensure >420 tests pass with ZERO regressions!

Write your complete handoff to `handoff.md` and notify via `send_message`.
