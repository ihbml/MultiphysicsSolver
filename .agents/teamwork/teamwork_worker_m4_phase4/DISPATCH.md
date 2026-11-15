# Dispatch: Worker M4 (Milestone M4_PHASE4 — Mathematik & Topologie korrigieren)

## Identity
- Role: Phase 4 Implementation Worker
- Working Directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m4_phase4
- Original Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
- Scope Document: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md
- Technical Blueprint: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md

## Mission & Implementation Requirements

You are assigned exclusive write ownership of Phase 4 modules:
- `backend/meshing/difvm_topology.py`
- `backend/meshing/native/src/difvm_mesh.cpp`
- `backend/meshing/native/include/difvm_mesh.hpp`
- `backend/meshing/moving_mesh.py`
- `backend/meshing/gci.py`
- `backend/meshing/coupling.py`
- `backend/meshing/native/src/octree_hexcore.cpp`
- `models.py` (adjust Check 7 moment balance for 2D planar meshes with $z_0 \ne 0$)
- Test files: `tests/test_mesh_engine.py`, `tests/test_wp4_multiphysics_hexcore.py`, and any new M4 tests.

Implement all Phase 4 audit items strictly adhering to the mathematical specifications:

### 1. H3: 2D Cell Centroids (`difvm_topology.py:1244`, `difvm_mesh.cpp:454`)
- In 3D, pyramid decomposition uses apex-to-base centroid weights $0.75 \mathbf{c}_f + 0.25 \mathbf{c}_{\text{approx}}$.
- In 2D, polygon triangulation via Green's theorem strictly requires triangle centroid weights:
  $$\mathbf{c}_{\text{tri}} = \frac{2}{3} \mathbf{c}_f + \frac{1}{3} \mathbf{c}_{\text{approx}}$$
  where $\mathbf{c}_f = (\mathbf{v}_1 + \mathbf{v}_2)/2$ is the edge midpoint.
- Update both `backend/meshing/difvm_topology.py` and `backend/meshing/native/src/difvm_mesh.cpp`.
- Verify the test quadrilateral $(0,0)-(4,0)-(1,1)-(0,1)$ analytically yields exact centroid **$(1.4, 0.4)$**.
- In `models.py:346-353` Check 7 (`UnstructuredMesh.validate()`), handle 2D planar meshes positioned at arbitrary $z_0 \ne 0$ where Gauss divergence surface moment evaluates $\frac{1}{d+1} \int_{\partial \Omega} \mathbf{x} (\mathbf{x} \cdot \mathbf{n}) dS$.

### 2. H4: Polyhedral Topology & Elimination of `len(verts) == 8` Heuristics
- In `backend/meshing/moving_mesh.py`, `backend/meshing/difvm_topology.py`, and `backend/meshing/native/src/difvm_mesh.cpp`:
  - Eliminate `len(verts) == 8` guessing. A coplanar 8-node polygon in 2D is NOT a 3D hex.
  - In `moving_mesh.py:62-114` (`_build_node_adjacency`), build node adjacency directly from explicit face loops (`mesh.faces`).
  - In `moving_mesh.py:279-283` (`deform_mesh`), do NOT discard `mesh.faces`, `owner`, `neighbour`, `cell_types`, or boundary patches. Preserve topological connectivity and call `new_mesh.recompute_geometry()`. Ensure octree cut polyhedra do not shrink or lose faces during ALE mesh deformation.

### 3. H2: Celik et al. (2008) Grid Convergence Index (`backend/meshing/gci.py`)
- Implement full Celik et al. (2008) formulation in `backend/meshing/gci.py`:
  - Detect convergence ratio $R = \epsilon_{21} / \epsilon_{32} = (\phi_2 - \phi_1) / (\phi_3 - \phi_2)$.
  - If $|R| \ge 1.0$, flag divergence: return `convergence_type="divergence"`, `apparent_order_p=nan`, `gci_fine=inf`. Never invert the ratio or return false convergence.
  - Implement fixed-point iteration for apparent order $p$ when $r_{21} \ne r_{32}$ with transcendental term $q(p) = \ln((r_{21}^p - s) / (r_{32}^p - s))$.
  - In `generate_3grid_gci_hierarchy`, compute effective grid refinement ratio from actual cell counts: $r = (N_1 / N_2)^{1/\text{dim}}$.
  - Remove artificial clamps on $p$ or GCI.

### 4. H7: Supermesh-AMI Area-Conservative Weights (`difvm_topology.py:1788-1815`)
- Replace heuristic Inverse Distance Weighting (IDW) in `compute_ami_weights` with geometric polygon overlap area clipping (Supermesh AMI).
- For target face polygon $P_i$ and candidate source face polygon $P_j$, compute intersection area $A_{ij} = \text{Area}(P_i \cap P_j)$ via Sutherland-Hodgman polygon clipping on the interface plane.
- Weight $w_{ij} = A_{ij} / A_i$.
- Enforce exact conservation: $\sum_j w_{ij} = 1.0$ and $\sum_j w_{ij} A_i = A_i$.

### 5. H8: SPH-FVM Coupling & Mass-Conservative P2M (`backend/meshing/coupling.py`)
- In `compute_boundary_area_vectors`, do NOT set $A_{iS} = -\sum V_j \nabla W_{ij}$. Compute $A_{iS}$ by integrating the kernel over actual FVM boundary wall faces:
  $$A_{iS} = \sum_{f \in \text{wall}} \mathbf{S}_f W(\|x_i - \mathbf{c}_f\|, h)$$
- In `OPMISTaylorInterpolator.interpolate_p2m`, replace unweighted arithmetic mean (`np.mean`) with volume-weighted conservative interpolation satisfying $\sum_c V_c \phi_c = \sum_p V_p \phi_p$.

### 6. M1 & M2: Aspect Ratio & Background-Relative `wsrd_alpha`
- **M1 (Aspect Ratio)**: Replace pairwise vertex distance ratio ($\max(d)/\min(d)$) in `difvm_topology.py`, `difvm_mesh.cpp`, and `octree_hexcore.cpp` with edge-length / bounding-box definition $AR = \max(L_x, L_y, L_z) / \min(L_x, L_y, L_z)$. For a unit cube $[0, 1]^3$, aspect ratio MUST equal **1.00** (not 1.732).
- **M2 (`wsrd_alpha`)**: In `difvm_topology.py` and `difvm_mesh.cpp`, define cut-cell volume fraction relative to its own background Cartesian/octree cell ($V_i / V_i^{\text{bg}}$) rather than dividing by global median volume.

### 7. Native Recompilation
- Recompile `backend/meshing/cfd_mesher_native.dll` via MSVC CMake Release mode with OpenMP enabled.
- Verify 0 compilation errors and clean DLL generation.

## Zero-Tolerance Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Host Hardware Constraints
- Ansys is running in the background: strictly throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Sequential test runs only: `.\\.venv\\Scripts\\python.exe -m pytest ...`. Grids $\le 30{,}000$ cells.

## Deliverables
- Write `changes.md` and `handoff.md` to `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m4_phase4\`.
- Send completion message to caller orchestrator via `send_message`.
