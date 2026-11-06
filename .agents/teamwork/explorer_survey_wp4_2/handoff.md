# Handoff Report: Work Package 4 (WP4) Survey & Architecture

**Agent**: `explorer_survey_wp4_2`  
**Working Directory**: `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_survey_wp4_2`  
**Date**: 2026-11-04  
**Type**: Hard Handoff (Investigation & Architecture Survey Complete)  

---

## 1. Observation

Direct observations from the CFDSolver codebase and benchmark assets:

1. **Audit Defect K4 (`SUGGESTIONS_BACKEND.md:55` & `backend/meshing/mesh_engine.py:120-130, 303-316`)**:
   - `mesh_engine.py:120`: `if len(verts) != 8: return False` attempts to identify box geometry purely by vertex count, failing on general polyhedra.
   - `mesh_engine.py:304`: `if phi_c[idx] < threshold:` discretizes cells where signed distance is negative (the solid interior), omitting the external fluid domain $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$ ($\phi > 0$).
2. **Audit Defect K5 (`backend/meshing/native/src/distmesh.cpp:259-275`)**:
   - `distmesh.cpp:267-275`: Surface triangles parameter `surf_triangles` is ignored. Cell counts are clamped by `nx = std::clamp(nx, 2, 40)`, building an equidistant bounding box hex grid regardless of surface shape.
3. **Boundary Patch Lumping (`backend/meshing/difvm_topology.py:680-684, 903-905`)**:
   - `difvm_topology.py:683`: `boundaries["wall"] = (internal_count, boundary_count)` groups all boundary faces into a single patch, discarding `SurfaceMesh.patches` and Farfield patch identities.
4. **CAD Benchmark Assets**:
   - `resources/CADTestSamples/TestCAD.stp`: Rotated 200 mm cube, CCW rotation $10^\circ$ around Z. Analytical volume at `scale=0.001`: $0.008\text{ m}^3$. Extents: $X, Y \in [-0.115846, 0.115846]\text{ m}$, $Z \in [0.0, 0.200]\text{ m}$.
   - `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`: AP242 STEP file with 99,257 lines, full 36-blade rotor assembly, solid volume ~ $5.56 \times 10^6\text{ mm}^3 = 0.00556\text{ m}^3$ at `scale=0.001`. Contains 9 `DRAUGHTING_MODEL` entities requiring sanitization.
   - `OpenFOAM_base/openfoam/tutorials/resources/geometry/snappyMultiRegionHeater`: 5 `.stl.gz` geometries (`bottomAir`, `heater`, `leftSolid`, `rightSolid`, `topAir`) representing a 5-region CHT coupled domain.
5. **CHT Coupled Interface Architecture (`backend/meshing/difvm_topology.py:1113-1280`)**:
   - `extract_cht_twin_faces` isolates coupled internal faces where $\text{region}(\text{owner}) \ne \text{region}(\text{neighbour})$, moving them out of the internal face block to the end of the face list as collocated twin patches (`coupledWall_fluid` and `coupledWall_solid`) with opposite face area vectors $\mathbf{S}_{f, A} = -\mathbf{S}_{f, B}$ and reversed node connectivity `faces[f][::-1]`.
6. **Resource Directives**:
   - Ansys Fluent is active on the system; RAM is at ~95%. User has mandated max 16 threads (`OMP_NUM_THREADS=16`) and strict RAM safety (moderate unit test mesh sizes $N \le 5{,}000$).

---

## 2. Logic Chain

1. **From Observation 1 & 2 to Domain Modes**:
   Because `mesh_engine.py` currently inverts the domain (meshing the solid interior) and uses `len(verts) == 8` heuristics, a robust declarative configuration must be introduced into `MeshingConfig`:
   `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"`.
   In `external_fluid`, the mesher must mesh $\phi(\mathbf{x}) > 0$ within a defined Farfield bounding box.
2. **From Observation 1 & 3 to Farfield Padding and Patch Naming**:
   For external aerodynamics, Farfield boundaries must be computed from the characteristic reference length $L_{\text{ref}} = \text{span}[0]$:
   - Upstream ($-X$): $5.0 \cdot L_{\text{ref}}$ (patch `inlet`)
   - Downstream ($+X$): $10.0 \cdot L_{\text{ref}}$ (patch `outlet`)
   - Lateral $Y$ ($\pm Y$): $5.0 \cdot L_{\text{ref}}$ (patch `sides`)
   - Lateral $Z$ ($\pm Z$): $5.0 \cdot L_{\text{ref}}$ (patches `bottom` and `top`)
   - Body boundary: mapped from `SurfaceMesh.patches` (or default `wall`).
3. **From Observation 4 & 5 to Multiphysics CHT Coupling**:
   Multi-region setups like `snappyMultiRegionHeater` require each cell to carry its `cell_regions` tag (0 for fluid, 1 for heater, etc.). Conformal interfaces between regions must be separated from the internal face block using `extract_cht_twin_faces`, ensuring contiguous face ordering:
   $$[0, n_{\text{clean\_int}}) \to [n_{\text{clean\_int}}, n_{\text{clean\_int}} + n_{\text{orig\_bnd}}) \to [start_a, start_a + n_{\text{coupled}}) \to [start_b, start_b + n_{\text{coupled}})$$
4. **From ASME V&V 20 to Deterministic GCI 3-Grid Generation**:
   To establish a spatial discretization uncertainty $\text{GCI} < 1\%$, the mesher must deterministically generate 3 topologically congruent grids (coarse $h_3$, medium $h_2$, fine $h_1$) with refinement ratio $r = \sqrt{2} \approx 1.414 \in [1.3, 1.5]$. All 3 grids must maintain identical boundary patch names in `boundaries.keys()`, identical `cell_regions`, and strictly positive cell volumes $V_i > 0$.
5. **From Observation 6 to Test Suite Design**:
   The test suite `tests/test_wp4_multiphysics_hexcore.py` must enforce `OMP_NUM_THREADS=16` and use moderate mesh sizes ($N \le 5{,}000$) to guarantee zero OOM risk and prevent perturbing Ansys Fluent.

---

## 3. Caveats

- **Active Ansys Simulation**: High-resolution fine meshes ($N > 10^6$ cells) must not be run while Ansys Fluent is active on the machine. Tests must use moderate nominal element sizes ($h_0 \approx 0.03\text{--}0.05\text{ m}$ for TestCAD, $h_0 \approx 0.015\text{ m}$ for the turbine blade).
- **Gmsh / OpenCASCADE Dependency**: STEP parsing supports both native Gmsh and pure-Python B-Rep traversal. For complex trimmed B-Spline CAD surfaces, Gmsh OpenCASCADE is required for boundary triangulation.

---

## 4. Conclusion

Work Package 4 architecture is fully specified and ready for implementation:
1. `MeshingConfig` schema defined with `domain_mode`, `FarfieldPadding`, `farfield_bbox`, and `refinement_zones`.
2. Farfield generator and patch assignment logic mapped with exact OpenFOAM patch types.
3. 3D Mosaic Poly-Hexcore 2:1 balanced octree transition defined using `CellType::POLYHEDRON = 42` to eliminate hanging nodes while preserving exact Gauss closure $< 10^{-11}$.
4. Cut-cell snapping with Berger-Giuliani Small-Cell Merging ($\alpha_i < 0.20$) and WSRD weights ($0.20 \le \alpha_i < 0.50$).
5. CHT multi-region coupling with collocated twin patches and normal opposition.
6. Automated 3-grid GCI hierarchy formulated for $\text{GCI} < 1\%$.
7. Complete test suite architecture designed for `tests/test_wp4_multiphysics_hexcore.py`.

Full technical details, equations, and code structures are documented in:
`G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_survey_wp4_2\analysis.md`.

---

## 5. Verification Method

To independently verify the observations and analysis:
1. **Inspect Audit Findings & Defective Lines**:
   - `view_file` on `backend/meshing/mesh_engine.py:118-132` (observe `len(verts) == 8` heuristic).
   - `view_file` on `backend/meshing/mesh_engine.py:303-316` (observe `phi_c[idx] < threshold` solid interior meshing).
   - `view_file` on `backend/meshing/native/src/distmesh.cpp:259-275` (observe bounding-box stub and resolution clamp).
2. **Inspect CAD Benchmarks**:
   - `view_file` on `resources/CADTestSamples/TestCAD.stp` (observe 200 mm cube).
   - `view_file` on `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp` (observe AP242 turbine rotor).
   - `list_dir` on `OpenFOAM_base/openfoam/tutorials/resources/geometry/snappyMultiRegionHeater` (observe 5 CHT region files).
3. **Inspect CHT Coupling Implementation**:
   - `view_file` on `backend/meshing/difvm_topology.py:1113-1280` (`extract_cht_twin_faces` contiguous twin-patch extraction).
