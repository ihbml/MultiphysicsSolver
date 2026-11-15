## 2026-11-07T02:06:54Z
You are worker_audit_m2_1, an expert Python and C++ CFD meshing and computational geometry engineer.
Your working directory is `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_audit_m2_1\`.

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Context & Inputs
- Read `ORIGINAL_REQUEST.md` at `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.
- Read `PROJECT.md` at `g:\Datein\Projects\CFDSolver\PROJECT.md`.
- Read Explorer 2 findings at `g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_audit_survey_2\survey_report.md` and `handoff.md`.

## Exclusive File Ownership
You exclusively own and may modify:
1. `backend/meshing/mesh_engine.py`
2. `backend/meshing/difvm_topology.py`
3. `backend/meshing/distmesh_core.py`
4. `backend/meshing/gci.py`
5. `backend/meshing/polymesh_io.py`
6. `backend/meshing/inflation.py`
7. `backend/meshing/geometry_sdf.py`
8. `backend/meshing/native/src/sdf_engine.cpp`
Do NOT modify any other files.

## Detailed Tasks: Milestone M2

### 1. R3.3 Dynamic Berger-Giuliani (2024) WSRD Weights
In `backend/meshing/mesh_engine.py:750-768` and `backend/meshing/difvm_topology.py:956-972`:
Compute `wsrd_weights` according to Berger & Giuliani (2024) Eq. (4.2):
$$w_i = \sin^2\left(\frac{\pi}{2} \frac{\alpha_i - \alpha_{\text{small}}}{\alpha_{\text{target}} - \alpha_{\text{small}}}\right)$$
where $\alpha_{\text{small}} = active\_cfg.small\_cell\_ratio$ and $\alpha_{\text{target}} = active\_cfg.wsrd\_target\_alpha$ (use dynamic configuration attributes instead of hardcoded 0.20 and 0.50).
Set $w_i = 0$ for $\alpha_i < \alpha_{\text{small}}$ and $w_i = 1$ for $\alpha_i \ge \alpha_{\text{target}}$.
Store in `mesh.error_map["wsrd_weights"]` and `mesh.metadata["wsrd_weights"]`.

### 2. R4 Solid-Only Domain Mode Dispatch Fix
In `backend/meshing/mesh_engine.py:126-130`:
Correct the `is_solid_only` dispatch branch so that:
`is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")`
When `config is not None` and `active_cfg.domain_mode != "solid_only"` (e.g. "fluid_only", "conjugate", "internal_flow", etc.), call `self.mesh_hexcore(clean_mesh, scale=1.0, h0=effective_h0, config=active_cfg)`.

### 3. R5.1 DistMesh Point Filtering & Newton Projection
- In `backend/meshing/distmesh_core.py:734`:
  Change point filtering to: `p = p[d < -0.05 * h_vals]`.
- In `backend/meshing/distmesh_core.py:827-839`:
  Execute up to 3 Newton projection steps for points with `d_nodes > 0.0`.

### 4. R6.1-2 GCI Scaling & Order Clamping
- In `backend/meshing/gci.py:113-114`:
  Compute representative grid size with exponent `1.0 / max(1, mesh.topological_dim)`.
- In `backend/meshing/gci.py:148-156`:
  Clamp $p$ to `[0.5, 10.0]` only when outside range or when oscillatory convergence occurs (`eps_32 * eps_21 <= 0`), maintaining consistent exponent in `phi_extrapolated` and `gci_fine`.

### 5. R6.3-4 SDF Bounding Box Tolerance & Barnes-Hut Dipole Acceleration
- In `backend/meshing/native/src/sdf_engine.cpp:479-484`:
  Apply `1e-5 * diag` tolerance on outside bounding box check.
- In `backend/meshing/geometry_sdf.py:989-1049` and `backend/meshing/native/src/sdf_engine.cpp:503-540`:
  Accelerate `winding_number` evaluation via AABB Barnes-Hut far-field dipole approximation for nodes where `dist > 2.0 * node_radius`.

### 6. R7 Static Audit Detail Fixes
- `backend/meshing/difvm_topology.py:778-784`:
  Fix attribute lookups: use `config.sizing.alpha` / `config.sizing.beta` and `config.inflation.pinching` / `config.inflation.pinching_threshold_angle`.
- `backend/meshing/mesh_engine.py:465`:
  Introduce `strict_native: bool = False` in `__init__` and support `CFD_STRICT_NATIVE=1` environment variable so `mesh_hexcore` never falls back silently to Python when strict native is enabled.
- `backend/meshing/polymesh_io.py:150-163`:
  Support configurable `patch_types` exporting `top`, `sides`, and `bottom` (without ground plane) as `symmetry` or `patch` instead of `wall`.
- `backend/meshing/inflation.py:104`:
  Replace global `nominal_normal` flip with consistent outward normal orientation per wall face, and reverse node ordering `f_arr[::-1]` when normal is flipped.

## Verification Commands (R13 Limits)
- If `sdf_engine.cpp` was modified, recompile native DLL:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release --parallel 8`
- Run unit tests sequentially with thread limits:
  `$env:OMP_NUM_THREADS = "8"`
  `.venv\Scripts\pytest.exe tests/test_wp1_topology_polymesh.py -v`
  `.venv\Scripts\pytest.exe tests/test_wp2_mesh_sdf.py -v`
  `.venv\Scripts\pytest.exe tests/test_wp3_robust_2d_mesher.py -v`
  `.venv\Scripts\pytest.exe tests/test_asme_vv.py -v`

## Deliverables
- Write `changes.md` in your working directory documenting the exact code changes and rationale.
- Write `handoff.md` with sections: Observation, Logic Chain, Caveats, Conclusion, Verification Method.
- Send completion message to parent orchestrator.


## 2026-11-07T02:15:34Z
**Context**: Milestone M2 Scope & Completion Directive
**Content**: User Directive received:
After completing Milestone M2 (covering DistMesh, GCI, SDF-BBox, dynamic WSRD, and static audit/API fixes), we will pause execution cleanly to conserve token limits.
Once your code modifications are implemented and verified via unit tests (`test_wp1_topology_polymesh.py`, `test_wp2_mesh_sdf.py`, `test_wp3_robust_2d_mesher.py`, `test_asme_vv.py`), please commit all verified changes to git (`git add ...` and `git commit -m "..."`) and deliver your handoff report.
**Action**: Implement M2 changes, verify, git commit, and deliver handoff.md.
