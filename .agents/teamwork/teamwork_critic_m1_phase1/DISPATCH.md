# Task Assignment: Mathematical Fidelity & Paper Conformance Critic (Phase 1 Gate)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m1_phase1\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Findings K6, M9, K1)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md`
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\handoff.md`

## Review Mission
Evaluate mathematical rigor and paper conformance of Phase 1 implementations:
1. In `models.py` (`UnstructuredMesh.validate()`):
   - Check the cell bounding box containment condition: does it strictly ensure $C_i \in [\min(v) - \epsilon, \max(v) + \epsilon]$ for all cells?
   - Check the global centroid moment balance via Gauss divergence theorem:
     $$\mathbf{M}_{\text{cells}} = \sum_{c \in \Omega} V_c \mathbf{C}_c \approx \frac{1}{d+1} \sum_{f \in \partial \Omega} \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f)$$
     Is the surface integral correctly computed across boundary faces? Does it mathematically guarantee detection of the K1 4/3 centroid error?
2. In `backend/meshing/difvm_topology.py` and `backend/meshing/mesh_engine.py`:
   - Verify that `compute_wsrd_weights` matches Berger & Giuliani (2024) Eq. (4.2).
   - Verify that `compute_amips_energy` and `compute_farfield_bounding_box` are mathematically sound and canonical.
3. In `tests/test_wp4_multiphysics_hexcore.py`:
   - Verify that replaced tests test true mathematical properties (e.g. 2:1 balancing volume ratios, level-set projection convergence $|\phi| < 10^{-4}$, WSRD weights).

## Execution Constraints
- Ansys running in background: throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Low-resource sequential testing only.

## Output
Write `handoff.md` with explicit binary verdict: `APPROVE` or `REQUEST_CHANGES`. Send message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.


## 2026-11-10T00:32:26Z
[Message from e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c]
You are Agent 2 (Mathematical Fidelity & Paper Conformance Critic) on the 3-agent review panel for Phase 1.
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m1_phase1\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_critic_m1_phase1\DISPATCH.md
Read the worker changes at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m1_phase1\changes.md and handoff.md.
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md.

Review Mission:
Evaluate mathematical correctness:
1. In models.py (UnstructuredMesh.validate()): inspect bounding box containment check and Gauss divergence theorem moment identity for global centroid. Verify mathematical validity and ability to detect K1 4/3 centroid error.
2. In backend/meshing/difvm_topology.py and mesh_engine.py: verify canonical compute_wsrd_weights matches Berger & Giuliani (2024) Eq. (4.2); verify canonical compute_amips_energy and compute_farfield_bounding_box.
3. In tests/test_wp4_multiphysics_hexcore.py: verify the genuine mathematical properties of replaced tests.

Deliver handoff.md with clear verdict (APPROVE / REQUEST_CHANGES) and send message to orchestrator e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.
