# Orchestrator Soft Handoff: Succession to Generation 8 (Orchestrator 8)

## 1. Milestone State
| Milestone | Name | Status | Verdict | Key Artifacts |
|-----------|------|--------|---------|---------------|
| Survey | Full 5-phase survey & decomposition | COMPLETED | Merged | `.agents/teamwork/orchestrator_7/SCOPE.md` |
| M1_PHASE1 | Ehrlichkeit, Test-Integrität & Doku-Archiv | DONE | 3:0 UNANIMOUS PASS | `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`, `requirements.txt` (UTF-8), `models.py` (Checks 6 & 7) |
| M2_PHASE2 | Build & Plattform (OpenMP) | DONE | 3:0 UNANIMOUS PASS | `CMakeLists.txt` (OpenMP::OpenMP_CXX), `difvm_topology.py` (fail-fast loader) |
| M3_PHASE3 | Abkürzungen & Heuristiken entfernen | DONE | 3:0 UNANIMOUS PASS | `mesh_engine.py` (K3/K4/M6/H10), `step_parser.py` (K9), `octree_hexcore.cpp` (M6/H10) |
| M4_PHASE4 | Mathematik & Topologie korrigieren | PLANNED (READY TO START) | - | Survey blueprint: `teamwork_explorer_survey_2/survey_report.md` |
| M5_PHASE5 | Physik & OpenFOAM-Schnittstelle | PLANNED | - | Survey blueprint: `teamwork_explorer_survey_3/survey_report.md` |
| M6_FINAL | Final System Acceptance & Certification | PLANNED | - | Full regression & 3:0 gate |

## 2. Active Subagents
- **None**: All 15 spawned subagents (surveyors 1-3, workers m1-m3, auditors m1-m3, critics m1-m3, reviewers m1-m3) have successfully delivered their reports and retired.

## 3. Pending Decisions & Context
- **Ansys Background Process**: Ansys is actively running on the host machine.
  - Throttling is MANDATORY: `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
  - Sequential pytest runs only: `.\\.venv\\Scripts\\python.exe -m pytest ...`.
  - Max cell count for tests: $\le 30{,}000$ cells.
- **Root Markdown Preservation Rule**:
  - `SUGGESTIONS_BACKEND.md`, `SUGGESTIONS_FRONTEND.md`, `COMMUNICATION_BACKEND_FRONTEND.md`, `FRONTEND_REQUIREMENTS_ON_BACKEND.md` MUST remain untouched in the project root.
- **Review Panel Requirement**:
  - Every milestone requires 3-agent unanimous approval (3:0 PASS): 1x `teamwork_auditor` (Anti-Cheat), 1x `teamwork_critic` (Math / Conformance), 1x `teamwork_reviewer` (Quality & Stability).

## 4. Remaining Work (Concrete Next Steps for Orchestrator 8)

### Milestone M4_PHASE4: Mathematik & Topologie korrigieren
- Features to implement (see detailed blueprints in `teamwork_explorer_survey_2/survey_report.md`):
  1. **H3 (2D Centroids)**:
     - In `backend/meshing/difvm_topology.py:1244` and `backend/meshing/native/src/difvm_mesh.cpp:454`, change sub-triangle centroid weights from 3D pyramid weights ($0.75 / 0.25$) to 2D triangle weights ($(2/3) \mathbf{c}_f + (1/3) \mathbf{c}_{\text{approx}}$) or direct Shoelace / Green's theorem.
     - Verify arbitrary 2D quad $(0,0)-(4,0)-(1,1)-(0,1)$ yields exact centroid $(1.4, 0.4)$.
     - Handle planar 2D meshes with $z_0 \ne 0$ in `models.py` Check 7 moment integration ($\frac{1}{3} z_0 A$ vs $z_0 A$).
  2. **H4 (Explicit Polyhedron Topology)**:
     - In `backend/meshing/moving_mesh.py`, `backend/meshing/difvm_topology.py`, and `backend/meshing/native/src/difvm_mesh.cpp`, eliminate `len(verts) == 8` guessing.
     - Preserve explicit `mesh.faces`, `owner`, `neighbour`, and `cell_types` so ALE mesh motion preserves polyhedra without shrinking volume from 63.5 to 31.2.
  3. **H2 (Celik-GCI)**:
     - In `backend/meshing/gci.py`, implement full Celik et al. (2008) formulation: detect divergence ($R = \epsilon_{21}/\epsilon_{32} \ge 1$), compute iterative apparent order $p$ with transcendental $q(p)$, compute effective refinement ratio $r = (N_1/N_2)^{1/\text{dim}}$, and eliminate artificial clamps.
  4. **H7 (Supermesh-AMI)**:
     - In `difvm_topology.py:1788-1815`, replace inverse-distance weighting with area-conservative polygon overlap weights satisfying $\sum w \cdot A = A$.
  5. **H8 (SPH-FVM Coupling & P2M)**:
     - In `backend/meshing/coupling.py`, compute boundary area vectors $A_{iS}$ from actual FVM boundary geometry rather than defining it as $-\sum V_j \nabla W_{ij}$. Implement mass-conservative P2M interpolation using cell volume weighting.
  6. **M1 & M2 (Aspect Ratio & wsrd_alpha)**:
     - Correct aspect ratio definition in `difvm_topology.py`, `difvm_mesh.cpp`, and `octree_hexcore.cpp` to use edge lengths or OpenFOAM bounding box definition (yielding 1.0 for a unit cube).
     - Redefine `wsrd_alpha` as cut-cell volume relative to background octree cell volume rather than global median volume.
  7. **Native Recompilation**: Recompile `cfd_mesher_native.dll` via CMake Release mode.
- Dispatch `teamwork_worker_m4_phase4`, followed by 3-agent review panel (`auditor_m4`, `critic_m4`, `reviewer_m4`). Require unanimous 3:0 APPROVE.

### Milestone M5_PHASE5: Physik & OpenFOAM-Schnittstelle
- Features to implement (see `teamwork_explorer_survey_3/survey_report.md`):
  1. **K8 (SMR Kinetics)**: `backend/native_cpp/smr_catalysis/smrKineticsSource.C`. Stoichiometric species coupling for CH4, H2O, H2, CO, CO2; reversible Xu-Froment kinetics $(1 - Q/K_{\text{eq}})$; consistent enthalpy sink $q = -r \cdot \Delta H(T)$; properly evaluate `fieldi` in `addSup`.
  2. **H5 & H6 (OpenFOAM Export & Case Generator)**: Multi-region export for `chtMultiRegionFoam` with `regionProperties`, correct patch types (`patch` for farfield, `wall` for body, `mappedWall` with mandatory keys `sampleMode`, `sampleRegion`, `samplePatch`), dynamic `processorWeights` from `mpi_ranks`, complete `0/` and `fvSolution` files.
  3. **M10 (WSL Runner)**: `shlex.quote` arguments, check process return codes, handle Windows drive letters safely.
- Dispatch `teamwork_worker_m5_phase5`, followed by 3-agent review panel.

### Milestone M6_FINAL: Final System Acceptance & Certification
- Full regression test run under `OMP_NUM_THREADS=2`.
- Final 3-agent sign-off.
- Final commit and completion message to parent `9afe4847-0716-4e2d-83e7-14376ba72fd4`.

## 5. Key Artifacts
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\GATE_STATUS.md`
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\BRIEFING.md`
- `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\progress.md`
