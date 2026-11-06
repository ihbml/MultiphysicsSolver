# BRIEFING — 2026-11-06T20:55:34Z

## Mission
Resolve 6 critical mathematical and paper-fidelity audit defects (R1–R6) and 4 static audit issues (R7) across C++ and Python meshing backend, recompile native DLL, commit changes, build automated checkMesh and simpleFoam acceptance pipeline in WSL (R10), and pass independent 3-agent audit committee with unanimous 3:0 approval (R12) under hardware/concurrency limits (R13).

## 🔒 My Identity
- Archetype: teamwork_orchestrator
- Roles: orchestrator, user_liaison, human_reporter, successor
- Working directory: G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator
- Original parent: parent
- Original parent conversation ID: 584a77b3-7b1d-4e75-9096-393cce404053
- Current parent conversation ID: e7db9f73-f27c-4afa-834a-f9373fdadaa0

## 🔒 My Workflow
- **Pattern**: Project Pattern (Survey → Decompose & Delegate / Iteration Loop)
- **Scope document**: G:\Datein\Projects\CFDSolver\PROJECT.md
1. **Decompose**:
   - Survey codebase via 3 Explorers:
     - Explorer 1: Native C++ octree hexcore & DistMesh math/snapping/AMIPS/volumes (R1, R2, R3.1-2, R5.2-3)
     - Explorer 2: Python meshing backend, dispatch, WSRD, DistMesh, GCI, static audit fixes (R3.3, R4, R5.1, R6.1-2, R7.1-4)
     - Explorer 3: WSL OpenFOAM checkMesh/simpleFoam pipeline, CAD benchmarks, build/compile environment (R6.3-4, R8, R10, R11, R13)
   - Establish updated PROJECT.md with Feature Inventory, Milestones, Interface Contracts, Code Layout.
2. **Dispatch & Execute**:
   - Implementation Track & E2E Testing Track
   - Milestones:
     - M1: Native C++ Hexcore Math & Snapping Fixes (R1, R2, R3.1-2)
     - M2: Python & Native DistMesh, Domain Dispatch, GCI & Static Fixes (R3.3, R4, R5, R6, R7)
     - M3: Native DLL Recompilation, Strict Native & Regression Testing (R11, R13)
     - M4: WSL OpenFOAM checkMesh & simpleFoam Acceptance Pipeline (R8, R10)
     - M5: Independent 3-Agent Audit Committee Certification (R12)
3. **On failure** (in this order):
   - Retry: nudge stuck agent or re-send task
   - Replace: spawn fresh agent with partial progress
   - Skip: proceed without (auditor is NEVER skippable)
   - Redistribute: split stuck agent's remaining work
   - Redesign: re-partition decomposition
4. **Succession**: At 16 spawns, write handoff.md, cancel crons, spawn successor.
- **Work items**:
  1. Survey phase (3 Explorers) [in-progress]
  2. Plan & Decompose in PROJECT.md [pending]
  3. M1: Native Hexcore Math & Geometry Fixes [pending]
  4. M2: DistMesh, Dispatch, GCI & Static Fixes [pending]
  5. M3: DLL Compilation & Test Verification [pending]
  6. M4: WSL Acceptance Pipeline & CAD Benchmarks [pending]
  7. M5: Independent 3-Agent Audit Committee (3:0 Unanimous) [pending]
- **Current phase**: 0 (Survey)
- **Current focus**: Survey phase monitoring

## 🔒 Key Constraints
- NEVER write, modify, or create source code files directly.
- NEVER run build/test commands yourself — require workers to do so.
- NEVER investigate or explore the problem at the code level — dispatch Explorers.
- Use file-editing tools ONLY for metadata/state files (.md) in .agents/teamwork/orchestrator/ or PROJECT.md.
- Zero-Shortcut Policy: genuine implementations, no magic heuristics, no silent clamps, no bare excepts.
- Forensic Auditor INTEGRITY VIOLATION is a binary veto.
- Hardware/Concurrency Guard (R13): Protect host stability with Ansys Fluent active in background (~95% RAM).
  - Set thread limits: OMP_NUM_THREADS=8, MKL_NUM_THREADS=8, OPENBLAS_NUM_THREADS=8.
  - CMake builds with `--parallel 8` (pin to E-cores or idle P-cores).
  - Run pytest strictly sequentially on individual test files with small memory footprints (no -n auto, no xdist).
  - Restrict initial test mesh sizes to <= 30,000 cells.
- Open Software Architecture (OSA) compliance & Google C++ / Python style guides.
- Independent 3-agent audit committee (Paper-Fidelity Challenger, Code & Architecture Auditor, Mathematics & Numerics Auditor) requires 100% unanimous approval (3:0 APPROVE).

## Current Parent
- Conversation ID: e7db9f73-f27c-4afa-834a-f9373fdadaa0
- Updated: 2026-11-06T20:55:34Z

## Key Decisions Made
- Initiated Survey phase with 3 focused Explorers across C++ native hexcore, Python backend/DistMesh/GCI, and WSL/OpenFOAM/CAD environment.

## Team Roster
| Agent | Type | Work Item | Status | Conv ID |
|---|---|---|---|---|
| explorer_audit_survey_1 | teamwork_explorer | C++ Hexcore Math & Snapping (R1, R2, R3.1-2, R5.2-3) | in-progress | 70985a3c-5023-4ce4-b1d8-e39799c019ed |
| explorer_audit_survey_2 | teamwork_explorer | Python Backend, Sizing, GCI, Static Audit (R3.3, R4, R5.1-2, R6.1-2, R7.1-4) | in-progress | acf7a1b5-7ee2-499e-9f5b-5473e48710ed |
| explorer_audit_survey_3 | teamwork_explorer | WSL Pipeline, CAD Benchmarks, Build & Safety (R6.3-4, R8, R10, R11, R13) | in-progress | f2aaf6bc-2d11-4797-83c0-cb9c52df0628 |

## Succession Status
- Succession required: no
- Spawn count: 3 / 16
- Pending subagents: 70985a3c-5023-4ce4-b1d8-e39799c019ed, acf7a1b5-7ee2-499e-9f5b-5473e48710ed, f2aaf6bc-2d11-4797-83c0-cb9c52df0628
- Predecessor: none
- Successor: not yet spawned

## Active Timers
- Heartbeat cron: 0e9d9a36-56a7-4f1c-8222-640d7206b910/task-38
- Safety timer: 0e9d9a36-56a7-4f1c-8222-640d7206b910/task-52

## Artifact Index
- G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md — Original User Request
- G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\DISPATCH.md — Incoming Dispatch
- G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\BRIEFING.md — Persistent Working Memory
- G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\progress.md — Liveness & Progress
