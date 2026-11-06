## 2026-10-30T23:04:00Z
You are worker_phase1_wp1_1, a teamwork_worker subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_phase1_wp1_1
Write all your agent metadata (BRIEFING.md, progress.md, handoff.md) into this directory.

## MANDATORY INTEGRITY WARNING
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Authoritative Reference Documents
Read these before starting work:
1. `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
2. `G:\Datein\Projects\CFDSolver\PROJECT.md`
3. `G:\Datein\Projects\CFDSolver\TEST_INFRA.md`
4. Survey reports:
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_1\survey_wp1.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_1\handoff.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_3\survey_baseline_verification.md`

## File Ownership
You exclusively own and modify:
- `backend/meshing/models.py`
- `backend/meshing/difvm_topology.py`
- `backend/meshing/polymesh_io.py`
- `backend/meshing/mesh_engine.py`
- `backend/meshing/distmesh_core.py`
- `backend/meshing/native/include/difvm_mesh.hpp`
- `backend/meshing/native/src/difvm_mesh.cpp`
- `backend/meshing/native/include/cfd_mesher_native.h`
- `backend/meshing/native/src/cfd_mesher_native.cpp`
- `backend/meshing/native/CMakeLists.txt`
Do NOT modify any files in `tests/` or `backend/meshing/geometry_sdf.py`.

## Implementation Tasks (WP1: Features F1–F10)
Follow the architectural and mathematical specifications in `survey_wp1.md`:

1. `backend/meshing/models.py`:
   - Add VTK-compatible `CellType(IntEnum)`: TRIANGLE=5, POLYGON=7, QUAD=9, TETRA=10, HEXAHEDRON=12, WEDGE=13, PYRAMID=14, POLYHEDRON=42.
   - Extend `UnstructuredMesh` dataclass with: `faces: Optional[List[np.ndarray]] = None`, `cell_types: Optional[np.ndarray] = None`, `topological_dim: int = 3`.
   - Add `to_openfoam_polymesh(self, case_dir: str) -> None` calling `polymesh_io.export_openfoam_polymesh`.
   - Vectorize `validate()` using `np.add.at` / `np.subtract.at`.
   - Replace absolute Gauss-closure with relative Gauss-closure: normalize cell flux residual sum_{f in cell} S_f by total cell surface area sum_{f in cell} ||S_f||; assert tol_rel < 10^{-11}.
   - Add true projection identity tests: collinearity ||delta_f x d_f|| / (||delta_f|| ||d_f||) < 10^{-12} and over-relaxation delta_f . S_f = ||S_f||^2 for theta_f < 89 deg. Retain existing non-orthogonal check for backwards compatibility.
   - Assert owner < neighbour on all internal faces and contiguous block ordering.
   - Unify `CurvatureSizingConfig` and `SizingParameters`.

2. `backend/meshing/difvm_topology.py`:
   - Face-to-node connectivity: extract and preserve `faces: List[np.ndarray]` in `compute_difvm_topology`.
   - Enforce consistent right-hand rule: internal face normal S_f strictly points from owner[f] to neighbour[f] (owner < neighbour). Boundary face normal points outward from owner cell.
   - Coplanar 4-vertex element discrimination: compute normalized volume V_box / L^3. If V_box / L^3 < 10^{-6}, classify as `CellType.QUAD` (9) and extract 4 perimeter boundary edges/faces, regardless of z-coordinate (planar meshes at z = 5.0 must be QUAD, never TETRA!). If V_box / L^3 >= 10^{-4}, classify as `CellType.TETRA` (10). Support general 3D polyhedra (`CellType.POLYHEDRON = 42`).
   - Strict manifoldness: If >2 cells share the same canonical face key, deterministically raise `ValueError("Non-manifold mesh detected: face shared by >2 cells")`.
   - Pass through named boundary patches (`face_patch_ids` / `patch_names` or `boundaries`).
   - Fix `extract_cht_twin_faces`: remove coupled faces from internal block, reindex faces so internal faces remain 0 ... n_int_clean - 1, and append coupled twin faces as contiguous boundary patches (`coupledWall_regionA_to_B` and `coupledWall_regionB_to_A`).
   - Extend `error_map` with CMS compensation fields: m_f^skew = c_f - x_int, damped k_f^comp with alpha_max=1.5, beta_f^upwind, cell volume weight omega_i^cell, alpha_i^wsrd with Berger-Giuliani WSRD weights. Clamp cos(theta_f) in [-1.0, 1.0]. For inverted faces (S_f . d_f <= 0), immediately escalate to `stabilization_mask = 3` and set beta_f = 0.0.

3. `backend/meshing/polymesh_io.py`:
   - Implement `export_openfoam_polymesh(mesh: UnstructuredMesh, case_dir: str)`: write standard OpenFOAM `constant/polyMesh/` files (`points`, `faces`, `owner`, `neighbour` with length matching internal faces exactly!, and `boundary` with proper types `wall`, `patch`, `symmetry`, `empty`, `mappedWall`, `cyclic`).
   - Implement `extrude_2d_to_3d_slab(mesh_2d: UnstructuredMesh, thickness: float = 1.0) -> UnstructuredMesh`: extrude planar 2D mesh into a 1-cell thick 3D mesh with `frontAndBack` patch (`type empty;`).

4. `backend/meshing/distmesh_core.py`:
   - Fix `rcm_reorder` to losslessly preserve `boundaries`, `baffles`, `cyclic_pairs`, and `metadata` by reindexing existing arrays via permutation vector instead of calling `compute_difvm_topology` from scratch.
   - Implement `hilbert_sfc_reorder(mesh, ...)` using 3D Morton/Hilbert space-filling curve, computing `inner_cells` and `halo_adjacent_cells` metadata.

5. Native C++20 Core & C-ABI:
   - `difvm_mesh.hpp`: add `face_offsets` and `face_nodes` CSR to `NativeMeshData`, export `CellType` enum.
   - `difvm_mesh.cpp`: fix 4-node element handling (remove dead code, implement coplanarity test), right-hand rule, CMS fields in error_map.
   - `cfd_mesher_native.h`: export C-ABI getters for `face_offsets` and `face_nodes`.
   - `cfd_mesher_native.cpp`: wrap every `extern "C"` function in `try { ... } catch (...) { return -2; }` with full array bounds checking.
   - `CMakeLists.txt`: replace `/fp:fast` with `/fp:precise` on MSVC, replace `-ffast-math` with `-fno-fast-math -ffp-contract=off` on GCC/Clang, support `.dll`, `.so`, `.dylib`.
   - Python ctypes loader in `mesh_engine.py`: wrap calls in `try ... finally: cfd_mesher_free(m_ptr)`.
   - Zero-Shortcut cleanup in `mesh_engine.py`: remove bare `except: pass` in line 460, remove cube heuristic in line 99, eliminate silent clamps and unscaled mm constants.

## Build and Verification
1. Build the native library using CMake:
   `cmake -B backend/meshing/native/build -S backend/meshing/native -DCMAKE_BUILD_TYPE=Release`
   `cmake --build backend/meshing/native/build --config Release`
   Copy or ensure `cfd_mesher_native.dll` is available to Python ctypes.
2. Run unit tests and regression tests:
   `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests`
   Ensure ALL 540 baseline tests pass with zero regressions.
3. Write `handoff.md` in your working directory documenting:
   - What was implemented in each file.
   - Build and test commands executed and their output.
   - Verification results.

Send a completion message to the orchestrator when finished.

## 2026-10-30T23:11:11Z
**Context**: Implementation Standards & Code Style
**Content**: Verbindliche Benutzer-Vorgabe zu Code-Stil und Formatierung:
Sowohl im C++20-Code als auch im Python-Code ist strikt der Google Style Guide einzuhalten:
1. C++20: Google C++ Style Guide (Namenskonventionen, keine unbedachten using namespace std; in Headern, saubere Include-Guards, 2-Space/Clang-Format-Konformität, nullptr statt NULL/0, explizite Typen).
2. Python: Google Python Style Guide (Typannotationen für alle öffentlichen Funktionen, Google-Style Docstrings mit Args/Returns/Raises, PEP8/Black-Konformität, keine Wildcard-Imports).
Das unabhängige Audit-Gremium (insb. Agent 2 Code-Auditor) wird dies strikt prüfen.
**Action**: Setze dies in allen neu implementierten und angepassten Dateien lückenlos um.


## 2026-10-30T23:17:33Z
**Context**: WP1 Implementation Status Query
**Content**: Heartbeat check: test_writer_phase1_1 has completed authoring tests/test_wp1_topology_polymesh.py and tests/test_wp2_mesh_sdf.py and published TEST_READY.md.
Please provide a quick status update on your current implementation progress (which task/files you are currently working on).
**Action**: Update your progress.md and report current progress.

## 2026-10-30T23:47:50Z
**Context**: WP1 Worker Status Check
**Content**: Heartbeat check: observed that native C++ CMake project has compiled and core Python modules have been updated. Are the pytest test suites running, and what is your ETA for handoff.md?
**Action**: Please update progress.md and send a brief status report.

## 2026-10-30T23:59:11Z
**Context**: Revival after Server Restart — Finalizing WP1 Handoff
**Content**: The server restarted. Please resume work:
1. Complete/verify the 540-test baseline regression run (`.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests --ignore=tests/test_wp1_topology_polymesh.py --ignore=tests/test_wp2_mesh_sdf.py -q`).
2. Verify all `tests/test_wp1_topology_polymesh.py` tests pass.
3. Author your formal `handoff.md` in `G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_phase1_wp1_1\handoff.md` with:
   - What was implemented in each file
   - Build commands and outputs
   - Test results (WP1 tests + 540 baseline regression tests)
   - Layout compliance and verification
4. Send a completion message to the orchestrator once `handoff.md` is ready so we can convene the 3-Agent Audit Committee for Gate WP1.
**Action**: Finish verification, write handoff.md, and notify orchestrator upon completion.

## 2026-10-31T00:32:19Z
**Context**: WP1 Handoff Delivery
**Content**: Heartbeat check: are the regression tests complete and is handoff.md ready to be delivered?
**Action**: Please deliver handoff.md or report current status.
