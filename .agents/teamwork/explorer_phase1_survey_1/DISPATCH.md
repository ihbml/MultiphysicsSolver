## 2026-10-30T22:49:07Z
Sender: 456f82a0-65f8-4e06-85a8-2c3526e591b9
Priority: MESSAGE_PRIORITY_HIGH

You are explorer_phase1_survey_1, a teamwork_explorer subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_1
You MUST write all your files (BRIEFING.md, progress.md, survey_wp1.md, handoff.md) into this directory. Do NOT write source code or test files. You are a read-only exploration agent.

## Authoritative Reference & Request
Read the original user request BEFORE doing anything else:
G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

Also read the incoming dispatch:
G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\DISPATCH.md

## Mission: Survey WP1
Conduct a thorough, deep investigation of Work Package 1 (WP1):
- Topologie-Kern, Zelltyp-Eindeutigkeit, faces-Konnektivität, MeshErrorMap & polyMesh-Export.

Key files to inspect:
- `backend/meshing/models.py`
- `backend/meshing/difvm_topology.py`
- `backend/meshing/mesh_engine.py`
- `backend/meshing/native/include/difvm_mesh.hpp`
- `backend/meshing/native/include/cfd_mesher_native.h`
- `backend/meshing/native/src/difvm_mesh.cpp`
- `backend/meshing/native/src/cfd_mesher_native.cpp`
- Reference documents:
  - `BACKEND_SUGGESTIONS.md` (Abschnitt 4: Audit-Befunde K1–K8, Bug A–Bug F, H1–H11; Abschnitte 5 & 6: M2.5/M3+)
  - `resources/papers/literature_survey_2026.md` (insb. §6 & §9.4)
  - `ARCHITECTURE.md` (Abschnitt 4.2 zur CMS-Geometrie-Fehlerkompensationskarte MeshErrorMap und Standalone In-Memory Graph-FVM SoA)
  - `resources/reference_spickzettel/LMArena/` (`Opus46DiFVM.md`, `Haiku55medium.md`, `FableDiFVM51low.md`, `GPT55highDiFVM.md`, `DiFVMAgentModeUnkown/`, `DiFVMAgentModeUnkonwnbutHigh/`)

Investigate every requirement for R1 & Gate WP1:
1. Explicit face-to-node connectivity in UnstructuredMesh (models.py) and NativeMeshData (difvm_mesh.hpp / cfd_mesher_native.h / cfd_mesher_native.cpp) with consistent right-hand rule: internal face normal S_f strictly points from owner[f] to neighbour[f] (owner[f] < neighbour[f]), boundary faces point outward. C-ABI getters for face_offsets and face_nodes.
2. backend/meshing/polymesh_io.py: export_openfoam_polymesh (and mesh.to_openfoam_polymesh(case_dir)) for standard-conforming constant/polyMesh/ (points, faces, owner, neighbour only for internal faces, boundary with correct patch types: wall, patch, symmetry, empty, mappedWall, cyclic) and extrude_2d_to_3d_slab for 1-cell thick 3D prisms/hexes with frontAndBack (type empty;).
3. Explicit CellType enum (VTK compatible: TRIANGLE=5, POLYGON=7, QUAD=9, TETRA=10, HEXAHEDRON=12, WEDGE=13, PYRAMID=14, POLYHEDRON=42), cell_types and topological_dim (2 or 3) in compute_difvm_topology (Python & C++). Coplanar 4-node elements are QUAD, never TETRA (even when z != 0). Support general 3D polyhedra (POLYHEDRON=42).
4. Manifoldness check: If >2 cells share the same face, deterministically raise ValueError. Upper triangular ordering (owner < neighbour).
5. Repair extract_cht_twin_faces: Cleanly remove coupled faces from internal block, reindex, append as contiguous boundary patches (coupledWall_regionA_to_B, coupledWall_regionB_to_A).
6. Pass through named boundary patches (face_patch_ids / patch_names) cleanly through compute_difvm_topology.
7. Repair rcm_reorder and add hilbert_sfc_reorder (3D Morton/Hilbert SFC with inner_cells vs halo_adjacent_cells metadata) losslessly preserving boundaries, baffles, cyclic_pairs, and metadata.
8. Extend mesh.error_map (Python & C++) with CMS compensation fields: m_skew, k_f_comp (alpha_max=1.5), beta_upwind, omega_cell, wsrd_alpha (Berger-Giuliani WSRD weights). Clamp cos_theta to [-1.0, 1.0]; if S_f . d_f <= 0 immediately escalate to stabilization_mask = 3.
9. Vectorized, scale-invariant UnstructuredMesh.validate(): np.add.at / np.subtract.at instead of loops; relative Gauss-Closure (tol_rel = 1e-11) relative to cell surface area; exact projection identity (collinearity ||delta_f x d_f|| / (||delta_f|| ||d_f||) < 1e-12 and over-relaxation delta_f . S_f = ||S_f||^2 for theta_f < 89 deg); owner < neighbour; contiguous blocks. Unified CurvatureSizingConfig and SizingParameters.
10. C-ABI in cfd_mesher_native.cpp: wrap every extern "C" function in try/catch (return -2 on exception), check array bounds, Python try...finally: cfd_mesher_free(m_ptr).

## Deliverables
Write:
1. `survey_wp1.md` in your working directory with detailed findings, existing implementation gaps, required file modifications, and concrete implementation design for each requirement.
2. `handoff.md` with:
   - Observation: exact existing code analysis with line numbers.
   - Logic Chain: step-by-step technical plan for WP1 implementation.
   - Caveats: edge cases, performance considerations, compatibility.
   - Conclusion: readiness assessment for Worker dispatch.
   - Verification Method: concrete test scenarios for Gate WP1.

When done, send a concise summary to the orchestrator via `send_message`.


## 2026-10-30T22:51:07Z
Sender: 456f82a0-65f8-4e06-85a8-2c3526e591b9
Priority: MESSAGE_PRIORITY_HIGH

**Context**: Phase 1 Survey
**Content**: Wichtige Benutzer-Direktive zur Priorität & Referenzen:
Das CFDSolver-Projekt selbst ist und bleibt die "Single Source of Truth".
Die Spickzettel und LMArena-Dokumente dürfen NICHT 1:1 unkritisch übernommen werden.
Sie dienen als Inspiration und Referenz: Nur Teile, Blöcke oder Skripte, die nach sorgfältiger Prüfung für mathematisch/technisch gut und passend befunden wurden, dürfen selektiv übernommen werden, um die Entwicklung zu erleichtern oder zu beschleunigen.
**Action**: Berücksichtige diese Direktive bei deiner Analyse und Berichterstellung.


## 2026-10-30T23:00:04Z
Sender: 456f82a0-65f8-4e06-85a8-2c3526e591b9
Priority: MESSAGE_PRIORITY_HIGH

**Context**: WP1 Survey Status Check
**Content**: Heartbeat check: explorer_phase1_survey_2 and explorer_phase1_survey_3 have completed their surveys. Please update on your current investigation status and ETA for survey_wp1.md and handoff.md.
**Action**: Update your progress.md and report current status.
