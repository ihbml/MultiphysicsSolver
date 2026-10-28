# Handoff Report — explorer_mesh_2

**Task**: Investigation of CAD Sample Files, Sketch Line Filtering, and Test Infrastructure  
**Working Directory**: `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_mesh_2`  
**Recipient**: Orchestrator (`4ce92fc5-81ba-402c-a361-d39a59aea46d`)  
**Date**: 2026-10-28  

---

## 1. Observation

1. **CAD Test Samples in `resources/CADTestSamples/`**:
   - `TestCAD.obj`: 8 vertices, 12 triangular faces, representing a $200\,\text{mm}$ cube rotated $10^\circ$ counter-clockwise around the Z-axis ($Z \in [0, 200]\,\text{mm}$).
   - `TestCADSTPAP214.stp`: Line 48 (`ADVANCED_FACE: 6`), Line 52 (`CLOSED_SHELL: 1`), Line 62 (`MANIFOLD_SOLID_BREP: 1`).
   - Line 35: `#35=GEOMETRIC_CURVE_SET('None', (#219, #121, #122, #123, #124))` containing 4 standalone sketch curves (`#121`–`#124`, `TRIMMED_CURVE` named `'Line1'`, `'Line2'`, `'Line3'`, `'Line4'`) and a point `#219` assigned to layer `'Layer 1'` (`#158=PRESENTATION_LAYER_ASSIGNMENT`), unreferenced by any solid body.
2. **Complex Geometry Samples in `resources/CADTestSamples/ComplexGeometryExample/`**:
   - Formats present: `BladeFullRev1CFD.prt`, `BladeRev1CFD.prt` (Siemens NX native part), `BladeFullRev1CFD.stp` (AP242), `BladeFullRev1CFDAP203.stp`, `BladeFullRev1CFDAP214.stp`, `BladeRev1CFD.obj`, `bladerev1cfd.mtl`, and translation `.log` files. No `.stl` file exists in `ComplexGeometryExample`.
   - `BladeFullRev1CFD.stp` (AP242, 51,859 entities): `#3929 = MANIFOLD_SOLID_BREP('Linked Body (4)', #3912)` and `#3930 = MANIFOLD_SOLID_BREP('Extrude (11)', #3913)`. Contains 158 `ADVANCED_FACE` surfaces, but also 9 `DRAUGHTING_MODEL` entities (`'Top'`, `'Front'`, `'Right'`, `'Back'`, `'Bottom'`, `'Left'`, `'Isometric'`, `'Trimetric'`, `''`), 8 camera models, 8 view volumes, and 1 constructive geometry representation.
   - `BladeRev1CFD.obj`: 30,832 vertices, 61,660 faces, 92,490 edges. Automated audit verified: 0 open edges, 0 multi-edges, Euler characteristic $\chi = 2$, positive signed volume $V = +1.883373 \times 10^5\,\text{mm}^3$, bounding box $[-77.36, -29.95, -100.0]$ to $[4.63, 86.81, 200.0]\,\text{mm}$.
3. **Repository Test Suites**:
   - `pytest.ini`: `pythonpath = .`, `testpaths = backend/geometry/tests`.
   - Execution command: `& "G:\Datein\Projects\CFDSolver\.venv\Scripts\pytest.exe" -q tests backend/geometry/tests`
   - Result: `277 passed in 23.95s` (verifies the "> 240 regression tests" requirement).
   - `tests/test_mesher.py` does not currently exist.
4. **Reference Implementation Spickzettel**:
   - `resources/reference_spickzettel/LMArena/1/native/src/core.hpp` and `2/backend/src/`:
     - Explicit implementation of Wang, Haidn & Hu (2025) boundary condition: $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ with $\text{div }\mathbf{r} < 0.75 d$.
     - Explicit implementation of Zhang et al. (2025) OPMIS interpolation with linear moment reproduction.
     - Collocated polyhedral topology (DiFVM, Du et al. 2026) in Structure of Arrays: `owner`, `neighbour`, `face_areas`, `cell_volumes`, $\boldsymbol{\Delta}_f$, and $\mathbf{k}_f$.
   - Existing repository data model in `backend/native_rust/foam_reader.py` (`FoamMesh`): lines 173–176 verify $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$.

---

## 2. Logic Chain

1. **CAD Sketch Line & Auxiliary Wireframe Filtering**:
   - From Observation 1 & 2: CAD exports (specifically NX STEP AP214 and AP242) bundle drafting models, camera views, and 2D sketches (`GEOMETRIC_CURVE_SET`) alongside solid bodies (`MANIFOLD_SOLID_BREP`).
   - If a naive mesher attempts to tessellate all curves or entities, it will ingest non-manifold open lines and produce non-watertight surfaces or crash.
   - Therefore, the mesher must enforce a topological filter:
     - Discard all 1D curves that do not bound a 2D `ADVANCED_FACE`.
     - Extract only faces that belong to a closed shell (`CLOSED_SHELL`) of a `MANIFOLD_SOLID_BREP`.
     - For OBJ files, parse only `f` (faces) and discard `l` (lines) and `p` (points).
     - Decompose connected components and retain the primary 2-manifold closed body ($\chi = 2, V > 0$).
2. **Regression Testing & `tests/test_mesher.py`**:
   - From Observation 3: The regression suite currently contains 277 tests across `tests/` and `backend/geometry/tests/`, all passing.
   - `tests/test_mesher.py` is missing and must be authored to implement the 12 required tests covering all acceptance criteria (positive volume, Gauss divergence closure, orthogonality consistency, SPH wall consistency, OPMIS moment reproduction, error map, and CAD robustness).
3. **Acceptance Invariants Consistency**:
   - From Observation 4: The mathematical formulations for DiFVM, Eulerian SPH boundary consistency, and OPMIS linear field reproduction are fully consistent between the references and the repository's `FoamMesh` architecture.

---

## 3. Caveats

1. Gmsh OpenCASCADE meshing on the full STEP file `BladeFullRev1CFD.stp` with ultra-fine deflection ($0.05\,\text{mm}$) on 158 B-spline faces requires considerable CPU time; for unit testing and fast E2E verification, `BladeRev1CFD.obj` (already verified as watertight with 61,660 faces) or coarse deflection parameters ($5\,\text{mm}$) should be preferred.
2. In `BladeFullRev1CFDAP214.stp`, there are 9 solid bodies (`MANIFOLD_SOLID_BREP`), whereas `BladeRev1CFD.obj` represents the single aerodynamic blade profile. The mesher must allow specifying which body/patch to mesh or performing boolean union.

---

## 4. Conclusion

- The CAD samples in `resources/CADTestSamples` provide both canonical analytical benchmarks (`TestCAD.*`, 200 mm rotated cube) and realistic complex industrial geometry (`BladeRev1CFD.obj` / `BladeFullRev1CFD.stp`).
- The existence of 2D sketch lines (`GEOMETRIC_CURVE_SET`) in `TestCADSTPAP214.stp` and drafting models in `BladeFullRev1CFD.stp` proves the necessity of the proposed 4-layer sketch filtering architecture.
- The repository test suite is healthy: **277 regression tests collected and passing in 23.95 seconds**.
- `tests/test_mesher.py` must be authored containing at least 10 (specified 12) tests directly testing the acceptance criteria.
- Full investigation report is available at `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_mesh_2\samples_and_tests_report.md`.

---

## 5. Verification Method

To independently verify these findings, run:

1. **Verify Regression Suite (277 tests)**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\pytest.exe" tests backend/geometry/tests
   ```
   *Expected Output*: `277 passed in ~24s`.

2. **Verify `BladeRev1CFD.obj` Watertight Topology ($\chi = 2, 0$ open edges)**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -c "from backend.geometry.parsers.obj_parser import OBJParser; from collections import defaultdict; m = OBJParser().parse('resources/CADTestSamples/ComplexGeometryExample/BladeRev1CFD.obj'); e2f = defaultdict(list); [e2f[(min(u,v), max(u,v))].append(i) for i, (u,v,w) in enumerate(m.faces) for u,v in [(u,v),(v,w),(w,u)]]; open_e = [e for e, fl in e2f.items() if len(fl) == 1]; print(f'Verts: {len(m.vertices)}, Faces: {len(m.faces)}, Edges: {len(e2f)}, Open Edges: {len(open_e)}, Chi: {len(m.vertices) - len(e2f) + len(m.faces)}')"
   ```
   *Expected Output*: `Verts: 30832, Faces: 61660, Edges: 92490, Open Edges: 0, Chi: 2`.

3. **Verify Standalone Sketch Lines in `TestCADSTPAP214.stp`**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -c "with open('resources/CADTestSamples/TestCADSTPAP214.stp') as f: print([l.strip() for l in f if 'GEOMETRIC_CURVE_SET' in l or 'TRIMMED_CURVE' in l][:5])"
   ```
