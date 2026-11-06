## 2026-10-30T22:49:16Z
[Message] timestamp=2026-10-30T22:49:16Z sender=456f82a0-65f8-4e06-85a8-2c3526e591b9 priority=MESSAGE_PRIORITY_HIGH content=You are explorer_phase1_survey_2, a teamwork_explorer subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_2
You MUST write all your files (BRIEFING.md, progress.md, survey_wp2.md, handoff.md) into this directory. Do NOT write source code or test files. You are a read-only exploration agent.

## Authoritative Reference & Request
Read the original user request BEFORE doing anything else:
G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

Also read the incoming dispatch:
G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\DISPATCH.md

## Mission: Survey WP2
Conduct a thorough, deep investigation of Work Package 2 (WP2):
- Exaktes Mesh-SDF mit AABB-Baum, Pseudonormalen, Winding Numbers & VoroMesh.

Key files to inspect:
- `backend/meshing/geometry_sdf.py`
- `backend/meshing/native/include/sdf_engine.hpp`
- `backend/meshing/native/src/sdf_engine.cpp`
- Reference documents:
  - `resources/papers/literature_survey_2026.md` (insb. §9.3 & §9.8)
  - `BACKEND_SUGGESTIONS.md` (Abschnitt 4: Audit-Befunde K1–K8, Bug A–Bug F, H1–H11; Abschnitte 5 & 6)
  - `resources/reference_spickzettel/INDEX.md` (CAD/libigl/, Meshing/fTetWild/)
  - Scientific papers and literature cited:
    - Bærentzen & Aanæs (2005): "Signed distance computation using the angle weighted pseudonormal"
    - Oosterom & Strackee / Jacobson et al. (2013): "Robust Inside-Outside Segmentation using Generalized Winding Numbers"
    - Maruani et al. (2023): "VoroMesh: Learning Watertight Surface Meshes with Voronoi Diagrams" (differentiable bisector loss over k=16 nearest seed points)

Investigate every requirement for R2 & Gate WP2:
1. Replace centroid-KDTree in CADMeshSDF (geometry_sdf.py and C++ pendant in sdf_engine.hpp / sdf_engine.cpp) with a BVH / AABB tree over triangle bounding boxes, with prev_facet spatial-coherence-hint caching.
2. Angle-weighted pseudonormals according to Bærentzen & Aanæs (2005):
   - Face normal n_f in triangle interior
   - Edge normal n_e on edges (weighted by dihedral angle / angle between faces)
   - Angle-weighted vertex normal n_v on vertices (weighted by interior corner angles)
   - Exact sign determination: sign(s) = sign((x - p) . n_pseudo) without sign flips along edges and corner diagonals (especially for sharp 30 deg and 90 deg edges).
3. Generalized Winding Number W(x) = 1/(4*pi) * sum_t Omega_t(x) according to Oosterom & Strackee / Jacobson et al. (2013) as robust inside/outside classification for non-watertight "dirty CAD" meshes. Solid angle Omega_t(x) formulas and vectorized / SIMD formulation.
4. Differentiable VoroMesh bisector loss according to Maruani et al. (2023) over k=16 nearest seed points for watertight, self-intersection-free Voronoi hulls. Continuous distance values and gradient properties.
5. Unit tests and verification scenarios: Anisotropic box/wedge with sharp 30° and 90° edges, mesh with small gaps. Sign flip audit along edges and diagonals.

## Deliverables
Write:
1. `survey_wp2.md` in your working directory with detailed findings, existing implementation gaps in geometry_sdf.py and sdf_engine.cpp, mathematical formulations, and concrete implementation design for each requirement.
2. `handoff.md` with:
   - Observation: exact existing code analysis with line numbers.
   - Logic Chain: step-by-step technical plan for WP2 implementation.
   - Caveats: edge cases, performance considerations, compatibility.
   - Conclusion: readiness assessment for Worker dispatch.
   - Verification Method: concrete test scenarios for Gate WP2.

When done, send a concise summary to the orchestrator via `send_message`.


## 2026-10-30T22:51:11Z
[Message] timestamp=2026-10-30T22:51:11Z sender=456f82a0-65f8-4e06-85a8-2c3526e591b9 priority=MESSAGE_PRIORITY_HIGH content=**Context**: Phase 1 Survey
**Content**: Wichtige Benutzer-Direktive zur Priorität & Referenzen:
Das CFDSolver-Projekt selbst ist und bleibt die "Single Source of Truth".
Die Spickzettel und LMArena-Dokumente dürfen NICHT 1:1 unkritisch übernommen werden.
Sie dienen als Inspiration und Referenz: Nur Teile, Blöcke oder Skripte, die nach sorgfältiger Prüfung für mathematisch/technisch gut und passend befunden wurden, dürfen selektiv übernommen werden, um die Entwicklung zu erleichtern oder zu beschleunigen.
**Action**: Berücksichtige diese Direktive bei deiner Analyse und Berichterstellung.
