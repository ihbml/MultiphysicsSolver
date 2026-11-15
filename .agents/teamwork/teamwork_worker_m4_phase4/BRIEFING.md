# BRIEFING — 2026-11-10T02:15:00Z

## Mission
Implement all Phase 4 (Milestone M4_PHASE4) numerics and topology remediations (H3, H4, H2, H7, H8, M1, M2), recompile native DLL, and verify with tests under strict resource throttling.

## 🔒 My Identity
- Archetype: teamwork_worker
- Roles: implementer, qa, specialist
- Working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_worker_m4_phase4
- Original parent: e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c
- Milestone: M4_PHASE4

## 🔒 Key Constraints
- Ansys running in background: strictly throttle OMP_NUM_THREADS=2 and MKL_NUM_THREADS=2.
- Sequential test execution only (`pytest ...`), grids <= 30k cells.
- Zero-tolerance integrity: No cheating, no fake results, no shortcuts or heuristics. Independent auditor verification.
- Recompile cfd_mesher_native.dll in MSVC Release mode with OpenMP.
- Write changes.md, handoff.md, notify caller orchestrator via send_message.

## Current Parent
- Conversation ID: e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c
- Updated: not yet

## Task Summary
- **What to build**:
  - H3: 2D cell centroids (2/3 cf + 1/3 c_approx) in difvm_topology.py and difvm_mesh.cpp. Quadrilateral (0,0)-(4,0)-(1,1)-(0,1) must equal (1.4, 0.4). Update models.py Check 7 for 2D planar meshes with z offset.
  - H4: Eliminate len(verts) == 8 guessing. In moving_mesh.py, build node adjacency from explicit faces and preserve mesh.faces/owner/neighbour/cell_types during ALE deformation.
  - H2: Full Celik (2008) GCI in gci.py: divergence detection (|R| >= 1), iterative apparent order p, effective r from cell counts, remove artificial clamps.
  - H7: Supermesh AMI area-conservative polygon overlap weights in difvm_topology.py (sum w * A = A).
  - H8: SPH-FVM coupling & mass-conservative P2M interpolation in coupling.py.
  - M1 & M2: Aspect ratio definition (1.0 for unit cube) and wsrd_alpha relative to background octree cell volume in difvm_topology.py, difvm_mesh.cpp, octree_hexcore.cpp.
  - Recompile cfd_mesher_native.dll.
- **Success criteria**: All Phase 4 requirements verified, zero regressions, full test suite passes cleanly.
- **Interface contracts**: g:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_7\SCOPE.md
- **Code layout**: SCOPE.md § Code Layout

## Key Decisions Made
- Proceed with thorough investigation of each affected file before making minimal surgical edits.
- Ensure all tests test real behavior.

## Artifact Index
- DISPATCH.md — assignment dispatch
- BRIEFING.md — persistent working memory
- progress.md — liveness heartbeat
- changes.md — record of changes made
- handoff.md — 5-component handoff report

## Change Tracker
- **Files modified**: None yet
- **Build status**: Pending
- **Pending issues**: None

## Quality Status
- **Build/test result**: Not yet executed
- **Lint status**: 0 violations
- **Tests added/modified**: None yet

## Loaded Skills
- None
