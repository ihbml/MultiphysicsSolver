# Dispatch: Mathematical Rigor & Paper Conformance Critic (critic_m4_phase4)

## Target & Mission
Evaluate Milestone M4_PHASE4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2).
Worker handoff report: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m4_phase4\handoff.md`
Authoritative Request: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
Survey Blueprint: `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md`
Project Scope: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_8\SCOPE.md`

## Evaluation Scope & Criteria
Verify strict mathematical rigor, paper conformance, and formula fidelity:
1. **H3 Green's Theorem & Centroid Weights**:
   - Verify that 2D sub-triangle centroid weights $(2/3) \mathbf{c}_f + (1/3) \mathbf{c}_{\text{approx}}$ are mathematically correct for 2D polygons per Green's theorem.
   - Verify test quad $(0,0)-(4,0)-(1,1)-(0,1)$ evaluates analytically and numerically to exact $(1.4, 0.4)$.
   - Verify that `models.py` Check 7 moment balance formulation correctly handles planar 2D meshes at arbitrary $z_0 \ne 0$.
2. **H4 Polyhedral Topology & ALE Preservation**:
   - Verify coplanarity check for 8-node polygons and node adjacency construction from explicit faces `mesh.faces`.
   - Verify that ALE deformation (`moving_mesh.py`) preserves face connectivity and geometric fields without topology loss.
3. **H2 Celik et al. (2008) Paper Fidelity**:
   - Compare `backend/meshing/gci.py` against Celik et al. (2008) "Procedure for Estimation and Reporting of Uncertainty Due to Discretization in CFD Applications":
     - Eq. (10) for apparent order $p$ fixed-point iteration when $r_{21} \ne r_{32}$.
     - Definition of effective grid refinement ratio $r = (N_1 / N_2)^{1/\text{dim}}$.
     - Divergence detection for $R \ge 1.0$ or $R \le -1.0$.
     - Verification that all artificial clamps were removed.
4. **H7 Supermesh-AMI Area Conservation**:
   - Verify that Sutherland-Hodgman polygon clipping computes true supermesh intersections and weights satisfy $\sum_j w_{ij} = 1.0$ and $\sum_j w_{ij} A_i = A_i$.
5. **H8 SPH-FVM Coupling & Conservative P2M**:
   - Verify physical boundary area vector integration over FVM wall faces $\mathbf{A}_{iS} = \sum_{f \in \text{wall}} \mathbf{S}_f W(\|\mathbf{x}_i - \mathbf{c}_f\|, h)$.
   - Verify volume-weighted P2M interpolation conservative identity $\sum_c V_c \phi_c = \sum_p V_p \phi_p$.
6. **M1 & M2 Aspect Ratio & wsrd_alpha**:
   - Verify aspect ratio definition evaluates to $1.0$ for a unit cube.
   - Verify `wsrd_alpha` definition uses background octree cell volume $V_i^{\text{bg}}$.
7. **Runtime & Verdict**:
   - Run verification tests sequentially under `OMP_NUM_THREADS=2`.
   - Provide explicit verdict: `APPROVE` or `REQUEST_CHANGES`.
   - Write full review to `g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m4_phase4\handoff.md`.


## 2026-11-10T17:14:33Z
Received invocation from parent (82c23776-586f-47db-a38f-1586e5bf3366):
Evaluate Milestone M4_PHASE4 for mathematical rigor, paper conformance, and formula fidelity (H3, H4, H2, H7, H8, M1, M2).
Sequential tests under OMP_NUM_THREADS=2.
Explicit verdict: APPROVE or REQUEST_CHANGES.
Handoff to g:\Datein\Projects\CFDSolver\.agents\teamwork\critic_m4_phase4\handoff.md and notify orchestrator.
