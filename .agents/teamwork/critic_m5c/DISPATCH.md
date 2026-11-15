# Dispatch for critic_m5c

## Role & Mission
You are `critic_m5c`, a `teamwork_critic` performing Mathematical Verification & Paper Fidelity Analysis for Milestone M5C (Features F62 & F63).

## Input Files
- ORIGINAL_REQUEST: `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
- PROJECT: `g:\Datein\Projects\CFDSolver\PROJECT.md`
- SCOPE: `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_9\SCOPE.md`
- Worker Handoff: `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_m5c\handoff.md`
- SUGGESTIONS_BACKEND.md: `g:\Datein\Projects\CFDSolver\SUGGESTIONS_BACKEND.md` (§10 & §11.2)
- Reference Spickzettel Snippet 8: `g:\Datein\Projects\CFDSolver\resources\reference_spickzettel\INDEX.md`
- Stromberg Paper: `resources/papers/SuperElements_Db_115554.pdf`
- Tests: `tests/test_super_elements_and_architecture_m5c.py`

## Mathematical Verification Mandate
1. **Feature F62 (Combinatorial 23-Class Super-Elements, Stromberg 2023 §10)**:
   - Check 24 $SO(3)$ proper rotations and 23 symmetry-reduced canonical classes covering all 256 configurations.
   - Check candidate nodes (35 nodes in $[-1, 1]^3$ including 8 octant support points $(\pm 0.5, \pm 0.5, \pm 0.5)$).
   - Check Scaled Jacobian minimum: $\min SJ \ge 0.302$ across all 23 classes, zero hanging nodes and edges.
   - Check asymmetric residual volume voting cost function: $R(e, D) = (RV_{\text{missing}}(e, D))^2 + RV_{\text{excess}}(e, D)$.
   - Check $\mathcal{O}(n_{\text{elem}})$ majority node voting: $\sum_{e \ni N} v(e, N) > 0$.
   - Check top-down entity mapping (0 DOF corner, 1 DOF curve, 2 DOF surface, 3 DOF volume) with defeaturing ($< \frac{1}{2} l_{\min}$).
   - Check Element Normalized Scaled Jacobian $J_{\text{ENS}} \in [-1, 1]$ across Hex8, Prism6, Tet4, Pyr5.
   - Check fictitious pyramid base hexahedron of height $\bar{l}_{\text{base}}$ detecting warped/folded bases with $J_{\text{ENS}} \le 0$.
2. **Feature F63 (Architecture Synthesis & `.mphys` Container §11.2)**:
   - Check C-ABI SoA `CAbiSpan` `(ptr, len, dtype, device_id)` layout, 64-byte alignment, and zero-copy export.
   - Check expression engine SIMD/bytecode evaluation of mathematical functions, vector broadcasting, and Rhai syntax preprocessing.
   - Check `.mphys` container binary layout: 64-byte `MPHYS001` header, 3D Morton/Hilbert space-filling curve sorting, 65,536-cell chunking with spatial AABB index, 7-element SI dimensions `[kg, m, s, K, mol, A, cd]`, and Parquet-style `MPHYSEOF` footer trailer with sub-millisecond open time.
3. **Execution**:
   - Run tests sequentially under `OMP_NUM_THREADS=2`:
     `.\.venv\Scripts\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py -v`

Deliver verdict (APPROVE or REQUEST_CHANGES) in `handoff.md` and report via send_message.
