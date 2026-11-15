# Dispatch: Phase 4 Numerics & Topology Worker (worker_m4_phase4)

## Target & Mission
Implement Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2).
Authoritative Request: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
Survey Blueprint: `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md`
Project Scope: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Detailed Requirements
1. **H3: 2D Triangle Centroids & Models Check 7**:
   - In `backend/meshing/difvm_topology.py:1244` and `backend/meshing/native/src/difvm_mesh.cpp:454`, change 2D sub-triangle centroid weights from 3D pyramid weights ($0.75/0.25$) to 2D Green's theorem / triangle weights:
     `pyr_c = tf["centre"] * (2.0 / 3.0) + c_approx * (1.0 / 3.0)`.
   - Verify test quadrilateral $(0,0)-(4,0)-(1,1)-(0,1)$ evaluates to exact centroid $(1.4, 0.4)$.
   - In `backend/meshing/models.py` Check 7 (`UnstructuredMesh.validate()`), adjust boundary surface integral calculation for planar 2D meshes located at $z_0 \ne 0$ so that centroid moment balance correctly matches the $z$-coordinate.
2. **H4: Explicit Polyhedral Topology & ALE Preservation**:
   - In `backend/meshing/moving_mesh.py`, `backend/meshing/difvm_topology.py`, and `backend/meshing/native/src/difvm_mesh.cpp`, eliminate `len(verts) == 8` guesswork.
   - Build node adjacency from explicit faces `mesh.faces`.
   - In `moving_mesh.py` `deform_mesh`, preserve `mesh.faces`, `owner`, `neighbour`, `cell_types`, and `boundaries` without calling `compute_difvm_topology` from scratch.
3. **H2: Full Celik et al. (2008) GCI**:
   - In `backend/meshing/gci.py`, implement full Celik et al. formulation:
     - Divergence detection: $R = \epsilon_{21}/\epsilon_{32} \ge 1$ or $\le -1 \implies$ `convergence_type='divergence'`, `apparent_order_p=nan`, `gci_fine=inf`.
     - Fixed-point iteration for apparent order $p$ when $r_{21} \ne r_{32}$ with transcendental $q(p)$ and sign $s$.
     - Effective refinement ratio $r = (N_1/N_2)^{1/\text{dim}}$ derived from actual grid cell counts.
     - Remove all artificial clamps.
4. **H7: Supermesh-AMI Area-Conservative Weights**:
   - In `backend/meshing/difvm_topology.py:1788-1815`, replace inverse-distance weighting with Sutherland-Hodgman polygon clipping supermesh intersection.
   - Weights must satisfy exact conservation: $\sum_j w_{ij} A_i = A_i$ and $\sum_j w_{ij} = 1.0$.
5. **H8: Physical SPH-FVM Coupling & Conservative P2M**:
   - In `backend/meshing/coupling.py`:
     - Calculate boundary area vectors $A_{iS}$ from actual FVM boundary geometry: $A_{iS} = \sum_{f \in \text{wall}} \mathbf{S}_f W(\|x_i - \mathbf{c}_f\|, h)$.
     - Implement volume-weighted conservative P2M satisfying $\sum V_c \phi_c = \sum V_p \phi_p$.
6. **M1 & M2: True Aspect Ratio & Background-Relative wsrd_alpha**:
   - Aspect ratio definition in `difvm_topology.py` and `difvm_mesh.cpp` must use bounding box spans or edge lengths ($1.0$ for unit cube, not $\sqrt{3} \approx 1.732$).
   - In `difvm_topology.py` and `difvm_mesh.cpp`, define `wsrd_alpha` relative to background cell volume $V_i^{\text{bg}}$ ($V_i / V_i^{\text{bg}}$) rather than global median volume.
7. **Native Compilation & Verification**:
   - Recompile `backend/meshing/cfd_mesher_native.dll` in Release mode using MSVC 2022 C++20 via CMake.
   - Run verification tests sequentially under `OMP_NUM_THREADS=2`.
   - Ensure all affected tests pass with 0 errors.

## Constraints & Environment
- Low-Resource Throttling: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Sequential test runs only via `.\\.venv\\Scripts\\python.exe -m pytest ...`.
- Small test grids ($\le 30{,}000$ cells).
- Output report in `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md`.
