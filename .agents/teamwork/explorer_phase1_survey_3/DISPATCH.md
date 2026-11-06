## 2026-10-30T22:49:26Z

You are explorer_phase1_survey_3, a teamwork_explorer subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_3
You MUST write all your files (BRIEFING.md, progress.md, survey_baseline_verification.md, handoff.md) into this directory. Do NOT write source code or test files. You are a read-only exploration agent.

## Authoritative Reference & Request
Read the original user request BEFORE doing anything else:
G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

Also read the incoming dispatch:
G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\DISPATCH.md

## Mission: Survey Baseline, Build System, Test Suites & Zero-Shortcut Integrity
Conduct a thorough investigation of the baseline codebase, tests, build configurations, and Zero-Shortcut audit:

Key areas to inspect:
1. All existing test suites:
   - `tests/` (inspect test structure, `tests/test_mesher.py`, `tests/test_difvm_topology.py`, etc.)
   - `backend/geometry/tests/`
   - Determine exact test runner commands and baseline status (all 540 existing tests must remain green).
2. Native CMake build system:
   - `backend/meshing/native/CMakeLists.txt`
   - Compiler flags: `/fp:precise` on MSVC, `-fno-fast-math -ffp-contract=off` on GCC/Clang.
   - Platform support: building and loading `.dll` (Windows), `.so` (Linux), `.dylib` (macOS).
   - Python ctypes / C-ABI bridge in `backend/meshing/` (how native library is located, loaded, and freed).
3. Zero-Shortcut scan across `backend/meshing/`:
   - Identify any bare `except Exception: pass` or `except: pass`.
   - Identify any geometry heuristics (e.g., `if len(verts) == 8 and len(faces) == 12:`).
   - Identify any magic thresholds or silent resolution clamps (e.g., overwriting `h0` silently instead of raising exception or warning).
   - Identify any dummy/facade implementations or unscaled mm constants.
4. Acceptance criteria test design for Gate WP1 and Gate WP2:
   - 2D quad meshes: L-shaped planar mesh at z=5.0 (verifying QUAD=9 instead of TETRA=10).
   - Scaled 3D meshes: scale-invariance across $10^{-6}\text{ m}$ to $10^{3}\text{ m}$.
   - Non-manifold checks: >2 cells sharing a face deterministically raising `ValueError`.
   - CHT two-region meshes: `extract_cht_twin_faces` coupledWall boundary patch generation.
   - Anisotropic wedge/box with 30° and 90° sharp edges: angle-weighted pseudonormal verification without sign flips.
   - Small gap non-watertight CAD mesh: Generalized Winding Numbers verification.
   - VoroMesh bisector loss continuous distance field verification.

## Deliverables
Write:
1. `survey_baseline_verification.md` in your working directory detailing test runner commands, baseline test suite status, build system requirements, zero-shortcut audit findings, and verification matrices for Gates WP1 and WP2.
2. `handoff.md` with:
   - Observation: exact existing code/test analysis.
   - Logic Chain: test architecture and build setup plan.
   - Caveats: environment considerations (Windows MSVC vs GCC, paths, ctypes).
   - Conclusion: readiness assessment for Test Track and implementation verification.
   - Verification Method: exact commands and test scripts to be executed by Workers.

When done, send a concise summary to the orchestrator via `send_message`.

## 2026-10-30T22:51:14Z

**Context**: Phase 1 Survey
**Content**: Wichtige Benutzer-Direktive zur Priorität & Referenzen:
Das CFDSolver-Projekt selbst ist und bleibt die "Single Source of Truth".
Die Spickzettel und LMArena-Dokumente dürfen NICHT 1:1 unkritisch übernommen werden.
Sie dienen als Inspiration und Referenz: Nur Teile, Blöcke oder Skripte, die nach sorgfältiger Prüfung für mathematisch/technisch gut und passend befunden wurden, dürfen selektiv übernommen werden, um die Entwicklung zu erleichtern oder zu beschleunigen.
**Action**: Berücksichtige diese Direktive bei deiner Analyse und Berichterstellung.
