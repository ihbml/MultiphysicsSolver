## 2026-10-31T00:49:30Z
You are worker_phase1_wp2_1, a teamwork_worker subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_phase1_wp2_1
Write all your agent metadata (BRIEFING.md, progress.md, handoff.md) into this directory.

## MANDATORY INTEGRITY WARNING
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Authoritative Reference Documents
Read these before starting work:
1. `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
2. `G:\Datein\Projects\CFDSolver\PROJECT.md`
3. `G:\Datein\Projects\CFDSolver\TEST_INFRA.md`
4. `G:\Datein\Projects\CFDSolver\TEST_READY.md`
5. Survey reports:
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_2\survey_wp2.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_2\handoff.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_3\survey_baseline_verification.md`

## File Ownership
You exclusively own and modify:
- `backend/meshing/geometry_sdf.py`
- `backend/meshing/native/include/sdf_engine.hpp`
- `backend/meshing/native/src/sdf_engine.cpp`
- `backend/meshing/native/include/cfd_mesher_native.h` (add SDF C-ABI exports)
- `backend/meshing/native/src/cfd_mesher_native.cpp` (add SDF C-ABI implementations with try/catch)
- `backend/meshing/native/CMakeLists.txt` (include `src/sdf_engine.cpp` in cfd_mesher_native)
Do NOT modify any files in `tests/` or WP1 files (`models.py`, `polymesh_io.py`, `difvm_topology.py`).

## Implementation Tasks (WP2: Features F11–F14)
Follow the architectural and mathematical specifications in `survey_wp2.md`:

1. Triangle AABB Tree & Coherence Caching (`TriangleAABBTree` in `geometry_sdf.py` and `sdf_engine.hpp`/`sdf_engine.cpp`):
   - Replace the centroid-cKDTree in `CADMeshSDF` with an axis-aligned bounding box tree over triangle bounds $[\min(v_0, v_1, v_2), \max(v_0, v_1, v_2)]$.
   - Implement branch-and-bound closest triangle search with `prev_facet` spatial-coherence-hint caching (provides an immediate tight upper bound $d_{\min}^2$ for coherent query streams).
   - Exact point-to-triangle projection computing barycentric coordinates $\lambda_0, \lambda_1, \lambda_2 \ge 0, \sum \lambda_i = 1$ and closest surface point $q = \sum \lambda_i v_i$.

2. Angle-Weighted Pseudonormals (Bærentzen & Aanæs 2005):
   - Classify closest point on triangle:
     - Interior ($\min \lambda_i > \epsilon$): face normal $n_f = (v_1 - v_0) \times (v_2 - v_0) / \|(v_1 - v_0) \times (v_2 - v_0)\|$.
     - Edge (one $\lambda_i \le \epsilon$): dihedral-weighted edge normal $n_e = (n_{f1} + n_{f2}) / \|n_{f1} + n_{f2}\|$.
     - Vertex (two $\lambda_i \le \epsilon$): corner angle-weighted vertex normal $n_v = \sum_f \alpha_{f,v} n_f / \|\sum_f \alpha_{f,v} n_f\|$, where $\alpha_{f,v}$ is the interior angle of face $f$ at vertex $v$.
   - Exact sign determination: $\operatorname{sign}(s) = \operatorname{sign}((p - q) \cdot n_{\text{pseudo}})$.
   - Guarantees zero sign flips along sharp $30^\circ$ and $90^\circ$ edges and corner diagonals.

3. Generalized Winding Numbers $W(x) = \frac{1}{4\pi}\sum_{t=1}^{N_f} \Omega_t(x)$ (Oosterom & Strackee 1983 / Jacobson et al. 2013):
   - Analytical solid angle formula:
     $$\Omega_t(x) = 2 \operatorname{atan2}(\det[r_a, r_b, r_c],\, d_a d_b d_c + (r_a \cdot r_b) d_c + (r_a \cdot r_c) d_b + (r_b \cdot r_c) d_a)$$
     where $r_a = v_a - x$, $d_a = \|r_a\|$.
   - Vectorized, chunked implementation with IEEE-754 epsilon regularization ($\epsilon = 10^{-30}$) for query points close to triangles.
   - Robust inside/outside classification on non-watertight "dirty CAD" meshes with small gaps ($10^{-4}\text{ m}$).

4. Differentiable VoroMesh Bisector Loss (Maruani et al. 2023):
   - Implement `compute_voromesh_bisector_loss(seeds: np.ndarray, k: int = 16)`:
     - Distance from query point $x$ to Voronoi boundary is distance to nearest bisector plane $H_{i_x, j}$ among $k=16$ nearest generator seeds.
     - Continuous distance values and unit gradient norm $\|\nabla_x d\| = 1.0$ almost everywhere.

5. C++20 Kernels & C-ABI Parity:
   - Implement `backend/meshing/native/include/sdf_engine.hpp` and `backend/meshing/native/src/sdf_engine.cpp`.
   - Export C-ABI functions in `cfd_mesher_native.h` and `cfd_mesher_native.cpp` wrapped in `try { ... } catch (...) { return -2; }`.
   - Maintain pure-Python fallback in `geometry_sdf.py` for full cross-platform compatibility.

6. Code Style:
   - C++20: Strict Google C++ Style Guide (2-space, `nullptr`, `#pragma once`, clean types).
   - Python: Strict Google Python Style Guide (complete type annotations, Google docstrings with Args/Returns/Raises, PEP8, no wildcard imports).

## Verification Method
1. Run authoritative WP2 acceptance tests:
   `.\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py -v`
   (All 4 tests must pass with 100% success).
2. Run WP1 acceptance tests to ensure no regressions:
   `.\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v`
   (All 11 tests must pass).
3. Run full baseline regression suite:
   `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests --ignore=tests/test_wp1_topology_polymesh.py --ignore=tests/test_wp2_mesh_sdf.py -q`
   (All 540 baseline tests must pass).
4. Author `handoff.md` in your working directory with detailed findings, build/test commands, and pass counts.
Send completion message to orchestrator when finished.

## 2026-10-31T01:02:29Z
**Context**: WP2 Implementation Status Check
**Content**: Heartbeat check: please provide an update on your implementation progress across TriangleAABBTree, angle-weighted pseudonormals, Generalized Winding Numbers, and VoroMesh bisector loss.
**Action**: Please update progress.md and report current status and ETA.
