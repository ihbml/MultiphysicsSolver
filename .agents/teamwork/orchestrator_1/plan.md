# Master Plan: CAD & Geometry Ingestion Engine for CFDSolver-Backend

## Objectives
1. Build an industrial-grade CAD & Geometry Ingestion Engine in `backend/geometry`.
2. Support ISO STEP (AP203/214/242), STL, OBJ, with robust tessellation.
3. Native CAD Bridge for Siemens NX 1926 (`step203ug.exe`, `step214ug.exe`, `step242.exe`).
4. Geometry Audit & OpenFOAM Preparation:
   - Manifold & watertight checks (0 non-manifold edges, 0 holes, Euler characteristic check).
   - Normal consistency & orientation outwards.
   - Robust scaling (e.g. 200 mm -> 0.2 m) with bounding box checks.
   - Export to `constant/triSurface/*.stl` and `*.obj` with clean patch names for snappyHexMeshDict.
5. Independent Test Suite & ASME V&V Verification:
   - Verification against real CAD test samples in `resources/CADTestSamples/`.
   - Programmatic verification matching OpenFOAM `surfaceCheck` criteria.

## Orchestration Phases
- **Phase 0: Survey**
  - Explorer 1: Inspect `backend/geometry` and Python/C++/Rust ecosystem, dependencies, build setup, installed tools.
  - Explorer 2: Inspect `resources/CADTestSamples/` files (headers, formats, dimensions, topology) and Siemens NX 1926 installation paths.
  - Spec Miner: Map exact requirements from `ORIGINAL_REQUEST.md` to technical interface specifications and ASME V&V / surfaceCheck criteria.
- **Phase 1: Architecture & Decomposition**
  - Formulate `PROJECT.md` (architecture, milestones, interface contracts, code layout).
  - Formulate `TEST_INFRA.md` (test architecture, tier breakdown, test runner).
- **Phase 2: Execution**
  - Track 1 (Implementation): Worker -> Reviewers -> Challengers -> Auditor gate cycle.
  - Track 2 (E2E Testing & Validation): Independent Validator / Test Writer.
- **Phase 3: Integration & Final Verification**
  - 100% test pass on real test samples.
  - Verification report & final handoff.
