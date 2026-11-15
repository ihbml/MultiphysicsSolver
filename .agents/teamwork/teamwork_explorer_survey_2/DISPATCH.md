# Task Assignment: Survey Explorer 2 (Phase 3 & Phase 4)

## Working Directory
`g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\`

## Mandatory Inputs to Read
1. `g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md` (MUST read verbatim)
2. `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md` (Code review findings K3, K4, K9, M6, H10, H3, H4, H2, H7, H8, M1, M2)
3. Existing files in project:
   - `backend/meshing/mesh_engine.py`
   - `backend/geometry/parsers/step_parser.py`
   - `backend/meshing/native/src/octree_hexcore.cpp`
   - `backend/meshing/difvm_topology.py` and `backend/meshing/native/src/difvm_mesh.cpp`
   - `backend/meshing/moving_mesh.py`
   - `backend/meshing/gci.py`
   - `backend/meshing/coupling.py`

## Objective
Map the exact code locations, current implementations, diffs needed, and verification strategy for:
1. Phase 3: Abkürzungen & Heuristiken entfernen
   - K3: `mesh_engine.py:231-284` `_detect_oriented_box_axes` - remove or safeguard so L- and U-profiles are not meshed as solid bounding boxes.
   - K4: `mesh_engine.py:121-131` compatibility shield - remove silent override of external_fluid, default to FarfieldPadding(), update affected tests to domain_mode="solid_only".
   - K9: `step_parser.py:84-95` `engine="auto"` - make Gmsh/OpenCASCADE primary path so curved surfaces are not flattened by `_parse_with_brep`.
   - M6 & H10: `octree_hexcore.cpp` max_level <= 4 clamp, 10^-15 overwrite; `mesh_engine.py` 500k limit and 0.11*h_min, h0/3 tuning factors.
2. Phase 4: Mathematik & Topologie korrigieren
   - H3: `difvm_topology.py:1126` and `difvm_mesh.cpp:454` 2D centroid weights ((2/3) cf + (1/3) c_approx or Shoelace). Verify test quad (0,0)-(4,0)-(1,1)-(0,1) gives (1.4, 0.4).
   - H4: `moving_mesh.py`, `difvm_topology.py`, `difvm_mesh.cpp` - eliminate len(verts)==8 heuristic; preserve explicit faces, owner/neighbour connectivity for polyhedra so mesh deformation does not destroy octree polyhedra.
   - H2: `backend/meshing/gci.py` Celik et al. (2008) formulation: divergence detection R>=1, iterative apparent order p, effective r=(N1/N2)^(1/dim).
   - H7: `difvm_topology.py:1655-1682` Supermesh-AMI polygon overlap weights satisfying sum w*A = A.
   - H8: `backend/meshing/coupling.py` non-tautological A_iS from FVM boundary geometry, conservative P2M with cell volume weighting.
   - M1 & M2: Aspect ratio (edge length or OpenFOAM definition, 1.0 for unit cube) in `difvm_topology.py`, `difvm_mesh.cpp`, `octree_hexcore.cpp`; wsrd_alpha relative to background octree cell volume.

## Constraints
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Ansys is running in background. No heavy benchmarks.

## Deliverables
Write detailed findings to `survey_report.md` and `handoff.md` in your working directory. Send completion message back to orchestrator.

## 2026-11-09T23:50:39Z
[Message] timestamp=2026-11-09T23:50:39Z sender=e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c priority=MESSAGE_PRIORITY_HIGH content=You are Survey Explorer 2 (teamwork_explorer).
Your working directory: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\
Read your task assignment at: g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\DISPATCH.md
MANDATORY: Read g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md and c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md before starting work.

Focus Scope: Phase 3 & Phase 4
1. Phase 3: Abkürzungen & Heuristiken entfernen (K3, K4, K9, M6, H10)
   - K3: backend/meshing/mesh_engine.py:231-284 _detect_oriented_box_axes - locate polycube bounding-box shortcut and detail how to remove or safeguard so non-box shapes like L-/U-profiles mesh geometrically.
   - K4: mesh_engine.py:121-131 - backward compatibility shield silently overriding external_fluid; specify default to FarfieldPadding() and which tests need domain_mode="solid_only".
   - K9: backend/geometry/parsers/step_parser.py:84-95 - engine="auto" defaulting to Python B-Rep and flattening curved surfaces (BladeFullRev1CFD.stp); specify switching default to Gmsh/OpenCASCADE.
   - M6 & H10: octree_hexcore.cpp max_level <= 4 clamp, 10^-15 overwrite; mesh_engine.py 500k RAM cap and 0.11*h_min, h0/3 tuning factors - map exact line numbers and cleanup.
2. Phase 4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2)
   - H3: difvm_topology.py:1126 and difvm_mesh.cpp:454 - 2D centroid weights ((2/3) cf + (1/3) c_approx or Green/Shoelace) instead of 3D pyramid weights (0.75/0.25). Verify quadrilateral (0,0)-(4,0)-(1,1)-(0,1) yielding (1.4, 0.4).
   - H4: moving_mesh.py, difvm_topology.py, difvm_mesh.cpp - eliminate len(verts)==8 heuristic; preserve explicit face lists and owner/neighbour connectivity for polyhedra.
   - H2: backend/meshing/gci.py - implement full Celik et al. (2008) formulation: divergence detection R>=1, iterative apparent order p, effective r=(N1/N2)^(1/dim).
   - H7: difvm_topology.py:1655-1682 - replace inverse-distance weighting with area-conservative Supermesh-AMI polygon overlap weights (sum w*A = A).
   - H8: backend/meshing/coupling.py - boundary area vectors A_iS from actual FVM boundary geometry, mass-conservative P2M interpolation using cell volume weighting.
   - M1 & M2: difvm_topology.py, difvm_mesh.cpp, octree_hexcore.cpp - correct aspect ratio (1.0 for unit cube) and wsrd_alpha relative to background octree cell volume.

Constraints:
- Read-only exploration! DO NOT modify source files.
- Thread limits: OMP_NUM_THREADS=2, MKL_NUM_THREADS=2.
- Low-resource testing only.
- Write survey_report.md and handoff.md in your working directory.
- Send a completion message via send_message to caller e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c when finished.
