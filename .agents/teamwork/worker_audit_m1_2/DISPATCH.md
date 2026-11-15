## 2026-11-07T00:52:26Z
You are worker_audit_m1_2, an expert C++20 and Python computational geometry and CFD meshing engineer.
Your working directory is `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_audit_m1_2\`.

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Context & Inputs
- Read `ORIGINAL_REQUEST.md` at `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`.
- Read `PROJECT.md` at `g:\Datein\Projects\CFDSolver\PROJECT.md`.
- Read previous worker handoff at `g:\Datein\Projects\CFDSolver\.agents\teamwork\worker_audit_m1_1\handoff.md`.
- Read gate review findings in `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\GATE_STATUS.md` and `g:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_m1_audit_1\handoff.md`.

## Exclusive File Ownership
You exclusively own and may edit:
1. `backend/meshing/native/src/octree_hexcore.cpp`
2. `backend/meshing/native/include/octree_hexcore.hpp`
3. `tests/test_wp4_multiphysics_hexcore.py`
Do NOT modify any other files.

## Remediation Tasks (Milestone M1 Iteration 2)
1. **Boundary Wall Face Normal Outward Orientation & Gauss Area Closure**:
   In `octree_hexcore.cpp:718` (Step 8) and `lines 907-911` (Step 9), the outward normal check evaluates:
   `Vec3 c_owner = nodes[active_leaves[mf.owner]].center;`
   `if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) { mf.normal_owner = -mf.normal_owner; std::reverse(mf.nodes.begin(), mf.nodes.end()); }`
   `nodes[...].center` is the center of the uncut background Cartesian octree box. On boundary cut-cells, this background center lies outside the truncated cell, making `mf.centre - c_owner` point inward. This incorrectly flips ~43% of boundary wall faces inward, destroying discrete Gauss divergence closure ($\sum_{f \in c} \mathbf{S}_f = 0$).
   Fix: Replace `c_owner` with the cell's true internal reference centroid `cell_approx_center[mf.owner]` ($c_{\text{approx}}$). Ensure `cell_approx_center` is computed/available before this orientation step, or compute the polygon face centroid mean for the cell first. With `c_approx`, `dot(mf.normal_owner, mf.centre - c_approx)` properly points outward toward the face, strictly ensuring all boundary faces point outward and internal faces point from owner to neighbour.
   Verify: Relative Gauss divergence closure $\max_c \|\sum_{f \in c} \mathbf{S}_f\| / V_c^{2/3} < 10^{-11}$.

2. **Remove AMIPS Inversion Bypass**:
   In `octree_hexcore.cpp:835-842`, remove `if (!inside_cell)`. Any test vertex position that produces an inverted corner tet or $E_{\text{AMIPS}} > 10^4$ must be rejected, without bypassing based on whether the vertex is inside the background bounding box.

3. **Fix `farfield_bbox` Array Unpacking**:
   In `octree_hexcore.cpp:237-239`, ensure `farfield_bbox` is correctly mapped from ctypes array `[xmin, ymin, zmin, xmax, ymax, zmax]` so `dom_min = (bbox[0], bbox[1], bbox[2])` and `dom_max = (bbox[3], bbox[4], bbox[5])`.

4. **Replace Synthetic Test with Genuine Mesh Gauss Closure Test**:
   In `tests/test_wp4_multiphysics_hexcore.py:481`, `test_mosaic_relative_gauss_closure` tested a hardcoded 9-element array instead of actual mesher output. Replace it with a genuine test calling `generate_hexcore_native` (e.g. on a sphere or cube) and asserting that the mesh satisfies relative Gauss closure $\max_c \|\sum_{f \in c} \mathbf{S}_f\| / V_c^{2/3} < 10^{-11}$.

5. **Build and Test Verification (R13 Concurrency & RAM Limits)**:
   - Build native DLL:
     `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release --parallel 8`
   - Run unit tests sequentially with thread limits:
     `$env:OMP_NUM_THREADS = "8"`
     `.venv\Scripts\pytest.exe tests/test_wp4_multiphysics_hexcore.py -v`
   - Run challenger tests:
     `.venv\Scripts\pytest.exe tests/test_challenger_m1_audit_2.py -v`
   - Verify 100% of tests pass.

6. **Documentation & Handoff**:
   - Write `changes.md` in your working directory documenting the exact code changes and mathematical rationale.
   - Write `handoff.md` in your working directory with sections: Observation, Logic Chain, Caveats, Conclusion, Verification Method.
   - Send completion message to parent orchestrator.


## 2026-11-07T00:58:29Z
**Context**: Permission to update `tests/test_challenger_m1_audit_2.py`
**Content**: Permission is GRANTED. You are authorized to edit `tests/test_challenger_m1_audit_2.py` to update `test_wall_face_inward_orientation_defect_detection` (lines 230-250) so it asserts that the defect is FIXED:
- Change assertion to: `assert inward_wall_faces == 0, f"Found {inward_wall_faces} inverted wall faces!"`
- Update the docstring/test name if needed to reflect that wall face normals must strictly point outward.
**Action**: Apply the fix in `octree_hexcore.cpp`, update `tests/test_challenger_m1_audit_2.py` and `tests/test_wp4_multiphysics_hexcore.py`, recompile the DLL, run all tests, and deliver your handoff report.
