# Task Assignment: Worker M3_PHASE3 (Abkürzungen & Heuristiken entfernen)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (Verbatim record)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Findings K3, K4, K9, M6, H10)
3. `g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md`
4. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md` (Exact diff blueprints and lines)
5. `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\handoff.md`

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Exclusive File Ownership
- `backend/meshing/mesh_engine.py`
- `backend/geometry/parsers/step_parser.py`
- `backend/meshing/native/src/octree_hexcore.cpp`
- Recompilation of `backend/meshing/cfd_mesher_native.dll`
- Affected tests in `tests/test_mesh_engine.py` and `tests/test_wp4_multiphysics_hexcore.py`

## Hardware & Execution Guardrails
- Ansys running in background: throttle threads `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`.
- Low-resource sequential testing only (meshes $\le 30{,}000$ cells).
- Python environment: `.\\.venv\\Scripts\\python.exe`.

## Objectives
1. **K3 (Polycube Shortcut Removal):**
   - In `backend/meshing/mesh_engine.py:231-284` (`_detect_oriented_box_axes`):
     - Ensure non-box geometries (such as L- and U-profiles) are meshed by the general geometry meshing pipeline rather than as solid bounding boxes.
     - Verify surface volume vs bounding box volume ($|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$) and verify face count equals 6 planar faces before declaring a geometry as a box.
     - Verify: L-profile test yields geometric volume $\approx 3.0$ (not bounding box volume $4.0$).
2. **K4 (Compatibility Shield Removal):**
   - In `backend/meshing/mesh_engine.py:121-131`:
     - Remove the backward compatibility shield that silently forces solid meshes when `external_fluid` is requested without farfield parameters.
     - Default to `FarfieldPadding()` when parameters are omitted in `external_fluid` mode.
     - Update affected existing tests to explicitly specify `domain_mode="solid_only"` where a solid mesh was intended.
3. **K9 (STEP Engine Default to OpenCASCADE/Gmsh):**
   - In `backend/geometry/parsers/step_parser.py:84-95` (`engine="auto"`):
     - Make Gmsh/OpenCASCADE the primary parsing path so curved surfaces (e.g., `BladeFullRev1CFD.stp`) are preserved rather than flattened by `_parse_with_brep`.
     - Allow B-Rep parser fallback only when Gmsh is not installed and all faces are planar.
4. **M6 & H10 (Clamps and Scaling Removal):**
   - In `backend/meshing/native/src/octree_hexcore.cpp:362`: eliminate hard `max_level <= 4` clamp (support configurable depth up to 16).
   - In `octree_hexcore.cpp:947-954`: eliminate silent $10^{-15}$ background volume overwrite for collapsed cells; report true volume and let validation flag defects.
   - In `backend/meshing/mesh_engine.py:319-333`: eliminate 500k-cell RAM downscaling cap. Raise explicit exception if resolution exceeds hardware limits instead of silently coarsening.
   - In `mesh_engine.py:937-940` and `981-983`: eliminate tuned magic numbers ($0.11 \cdot h_{\min}$, $h_0/3$).
   - Recompile `cfd_mesher_native.dll` via CMake with `--parallel 2`.
5. **Verification:**
   - Run tests sequentially with `OMP_NUM_THREADS=2`.
   - Verify L-profile volume $\approx 3.0$.
   - Verify `BladeFullRev1CFD.stp` mesh/surface area preservation.

## Deliverables
Write `changes.md` and `handoff.md` in your working directory. Send completion message to orchestrator `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`.


## 2026-11-10T01:15:52Z
You are Worker M3_PHASE3 (teamwork_worker).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m3_phase3\DISPATCH.md
Read the survey report and blueprints at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\survey_report.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md before starting.

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Scope (Phase 3):
1. K3: Remove or safeguard _detect_oriented_box_axes in mesh_engine.py so non-box geometries (L-profile, U-profile) are meshed with true geometric volume (~3.0), not bounding box (4.0).
2. K4: Remove compatibility shield in mesh_engine.py:121-131; default to FarfieldPadding() when parameters omitted in external_fluid mode, and update affected tests to domain_mode="solid_only".
3. K9: In step_parser.py:84-95 (engine="auto"), make Gmsh/OpenCASCADE primary path so curved surfaces (BladeFullRev1CFD.stp) are preserved without flattening.
4. M6 & H10: Remove max_level <= 4 clamp and 10^-15 silent volume overwrite in octree_hexcore.cpp; remove 500k RAM silent cap and 0.11*h_min, h0/3 magic numbers in mesh_engine.py. Recompile native DLL with CMake.
5. Verify changes and run sequential pytest with OMP_NUM_THREADS=2.

Deliver changes.md and handoff.md, then send completion message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c.


## 2026-11-10T01:26:11Z
**Context**: Phase 3 Worker Status Check
**Content**: Safety timer fired at 01:26Z. Checking on progress of K3, K4, K9, M6/H10 implementations and CMake compilation.
**Action**: Please report your current step, progress, and update your progress.md.
