# Dispatch: Mathematical Fidelity Critic for Phase 3 (Milestone M3_PHASE3)

## Identity
- Role: Mathematical Fidelity & Paper Conformance Critic
- Working Directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m3_phase3
- Original Request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
- Scope Document: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md
- Worker Handoff: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\handoff.md
- Worker Changes: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\changes.md

## Mission
Perform mathematical and physical rigor verification on Phase 3 changes (K3, K4, K9, M6, H10):
1. **Mathematical Soundness of K3**: Verify that the divergence theorem surface integral $V_{\text{surf}} = \frac{1}{3}\sum (\mathbf{c}_f \cdot \mathbf{S}_f)$ correctly detects non-cuboids. Verify that an L-profile with dimensions 2x2 with a 1x1 cutout (true volume = 3.0) yields an unstructured mesh volume $\approx 3.0$ and rejects the bounding box volume 4.0.
2. **K4 Wind Tunnel Domain Verification**: Verify that requesting `domain_mode="external_fluid"` without explicit padding creates the canonical 16L wind tunnel domain bounding box rather than a solid body mesh.
3. **K9 Geometric Curvature Preservation**: Verify that `_has_curved_entities` correctly flags B-Spline / cylindrical entities and enforces OpenCASCADE/Gmsh, preserving true CAD surface area ($\ge 312{,}000\text{ mm}^2$ on `BladeFullRev1CFD.stp`).
4. **M6 & H10 Numerical Consistency**: Verify that octree depths $>4$ are supported up to 16, that degenerate cut cells with $|\text{vol}| \le 10^{-15}$ receive volume 0.0 without corrupting cell arrays, and that isotropic cell spacing $dx = dy = dz = h_0$ is maintained without tuned factors $0.11 \cdot h_{\min}$ or $h_0/3$.

## Constraints
- Ansys running in background: execute low-resource sequential tests only (`OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `.venv\Scripts\python.exe -m pytest ...`).
- Output verdict strictly as `APPROVE` or `REQUEST_CHANGES` (with detailed mathematical critique) in `handoff.md`.

## 2026-11-10T01:51:02Z
You are the Mathematical Fidelity Critic for Phase 3 (Milestone M3_PHASE3).
Your working directory is: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m3_phase3
Read your DISPATCH.md at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m3_phase3\DISPATCH.md
Read the original user request at: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Read the scope document at: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md
Read the worker handoff and changes at:
- g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\handoff.md
- g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\changes.md

Perform mathematical and paper-conformance critique of K3, K4, K9, M6, and H10.
Verify that:
- K3: The divergence theorem surface integral volume vs bounding-box volume test ($|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$) and 6-face-cluster check reliably reject non-box polyhedra (L-profile, U-profile, etc.).
- K4: External fluid default generates a physical farfield wind-tunnel domain rather than solid.
- K9: Curved STEP models strictly require Gmsh/OpenCASCADE, preventing surface area loss.
- M6/H10: Octree refinement depth > 4 is supported without clamps, degenerate cut cells are given 0.0 volume, 500k RAM cap is replaced with explicit exception, and magic numbers (0.11*h_min, h0/3) are removed.
Write your critique and findings to `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m3_phase3\handoff.md`.
Deliver your verdict (APPROVE or REQUEST_CHANGES) via send_message to your caller orchestrator.
Do not overload host resources (Ansys running in background). Throttle threads: OMP_NUM_THREADS=2.
