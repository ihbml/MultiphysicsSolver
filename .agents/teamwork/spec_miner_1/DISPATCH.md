## 2026-10-19T13:49:23Z
Sender: 7a18387b-ee6c-468c-b5fd-4828b87a1558
Priority: MESSAGE_PRIORITY_HIGH
Content:
Identity: Specification & Criteria Miner
Working directory: G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_1
Original request path: G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

Instructions:
1. You MUST read G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md before starting.
2. Extract, analyze, and formalize all requirements and acceptance criteria:
   - R1: Format specifications for STEP (AP203, AP214, AP242), STL (ASCII & Binary), OBJ (Wavefront). Tessellation parameters (linear deflection, angular tolerance).
   - R2: Native CAD Bridge specification: detection logic for Siemens NX 1926 installation, CLI invocation syntax for step203ug.exe/step214ug.exe/step242.exe, exit codes, timeout handling, and graceful fallback when native CAD is not available.
   - R3: Geometry Audit & OpenFOAM Export:
     - Definition of Watertightness / 2-manifold surface.
     - Euler characteristic check (V - E + F = 2 for genus-0 closed surfaces).
     - Non-manifold edge count = 0, boundary edge count = 0.
     - Normal orientation consistency and outward-pointing test (e.g. ray-casting or signed volume).
     - Scaling transformation (e.g. 200 mm -> 0.2 m, scale factor 0.001) with bounding box verification.
     - OpenFOAM export structure: constant/triSurface/ directory layout, snappyHexMeshDict patch naming.
   - R4: Independent Test Suite & ASME V&V Verification:
     - ASME V&V principles applicable to surface mesh validation.
     - Exact algorithmic checks corresponding to OpenFOAM surfaceCheck (unclosed edges, degenerate faces, aspect ratio, self-intersections).
3. Maintain your progress.md in your working directory.
4. Write your detailed specification report to G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_1\spec_requirements.md and create a complete handoff.md in G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_1\handoff.md.
5. When done, send a message to your caller with your findings and report path.
