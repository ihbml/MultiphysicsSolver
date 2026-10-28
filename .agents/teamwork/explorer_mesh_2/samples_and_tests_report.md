# CAD Sample Files, Sketch Line Filtering, and Test Infrastructure Report

**Author**: `explorer_mesh_2`  
**Date**: 2026-10-28  
**Scope**: In-depth empirical investigation of CAD reference samples, sketch/wireframe filtering architectures, repository test suites, and acceptance criteria verification requirements for the CFDSolver Multiphysics In-Memory Mesher.

---

## Executive Summary

1. **CAD Reference Samples**:
   - `TestCAD.*` represents a $200\,\text{mm} \times 200\,\text{mm} \times 200\,\text{mm}$ cube rotated $10^\circ$ counter-clockwise around the Z-axis. Crucially, `TestCADSTPAP214.stp` contains **standalone 2D sketch lines and a sketch point** encapsulated inside a `GEOMETRIC_CURVE_SET` (`Line1` to `Line4`, entities `#121`–`#124`), alongside the 3D solid body (`MANIFOLD_SOLID_BREP`).
   - `ComplexGeometryExample/` contains the aerodynamic turbine blade in Siemens NX native part format (`BladeFullRev1CFD.prt`, `BladeRev1CFD.prt`), ISO STEP standards (`BladeFullRev1CFD.stp` in AP242, `BladeFullRev1CFDAP203.stp`, `BladeFullRev1CFDAP214.stp`), and Wavefront OBJ (`BladeRev1CFD.obj`). **No `.stl` file is present in `ComplexGeometryExample`.**
   - Direct empirical topology analysis of `BladeRev1CFD.obj` verifies it is an **ideal, watertight 2-manifold closed surface**: 30,832 vertices, 61,660 triangular faces, 92,490 edges, **0 open edges**, **0 multi-edges**, Euler characteristic $\chi = V - E + F = 2$, and outward-oriented positive volume $1.883373 \times 10^5\,\text{mm}^3$ ($0.0001883373\,\text{m}^3$).
   - `BladeFullRev1CFD.stp` (AP242) contains 2 `MANIFOLD_SOLID_BREP` bodies (`Linked Body (4)` and `Extrude (11)`), 158 `ADVANCED_FACE` surfaces, plus 9 auxiliary `DRAUGHTING_MODEL`s containing camera models, view volumes, planar boxes, and constructive geometry representations.
2. **Repository Test Infrastructure & Regression Suite**:
   - `pytest.ini` configures `pythonpath = .` and default `testpaths = backend/geometry/tests` (37 tests).
   - Executing the complete repository test suite (`pytest tests/ backend/geometry/tests/`) collects **277 regression tests**, and **all 277 tests PASS in 23.95 seconds**. This strictly confirms the "> 240 regression tests" baseline.
   - `tests/test_mesher.py` does **not yet exist**. A comprehensive specification of 12 unit and end-to-end tests has been designed to verify 100% of the acceptance criteria.
3. **Acceptance Criteria Invariants**:
   - Exact mathematical formulations, reference code foundations from `resources/reference_spickzettel/LMArena/` (Wang et al. 2025, Zhang et al. 2025, Du et al. 2026, Persson & Strang 2004), and programmatic validation methods have been formulated for:
     - Strict positive cell volumes: $V_i > 0 \quad \forall i$
     - Exact Gauss divergence closed cell sum: $\max_i \|\sum_{f \in \text{faces}(i)} \mathbf{S}_f\| < 10^{-11}$
     - Orthogonality decomposition consistency: $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$
     - Eulerian SPH wall consistency: $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0} \quad (< 10^{-10})$ with boundary detector $\text{div }\mathbf{r} < 0.75 d$
     - OPMIS linear field moment reproduction: $|f_{\text{interp}}(\mathbf{x}) - f(\mathbf{x})| < 10^{-12}$
     - Error map in `mesh.error_map` (non-orthogonality, skewness, aspect ratio, solver stabilization masks).

---

## 1. CAD Sample Files Analysis

### 1.1 `resources/CADTestSamples/TestCAD.*` (Rotated Cube Reference)

#### File Inventory
| File | Format | Size | Description / Origin |
|---|---|---|---|
| `TestCAD.obj` | Wavefront OBJ | 504 B | 8 vertices, 12 triangular faces. ASCII. |
| `TestCAD.stl` | Binary STL | 684 B | 80-byte header + 12 triangular facets (50 bytes each). |
| `TestCADSTPAP203.stp` | STEP AP203 | 11,409 B | ISO 10303-21 standard exchange file. |
| `TestCADSTPAP214.stp` | STEP AP214 | 12,814 B | Exported from Siemens PLM Software NX 1938. |
| `TestCADSTPAP242.stp` | STEP AP242 | 34,611 B | Model-based 3D engineering schema with semantic metadata. |
| `TestCAD.prt` | Siemens NX Native | 108,806 B | Native Siemens NX 1938 Part file. |
| `TestCAD.igs` | IGES | 36,818 B | Initial Graphics Exchange Specification. |
| `TestCAD.CATPart` | CATIA V5 | 58,140 B | CATIA V5 part file. |

#### Geometric Properties (Ground Truth)
- **Geometry**: Equilateral cube of side length $L = 200.0\,\text{mm}$ ($0.200\,\text{m}$).
- **Orientation**: Rotated $10.0^\circ$ ($\approx 0.1745329\,\text{rad}$) counter-clockwise about the Z-axis. Base located at $Z = 0.0\,\text{mm}$, top at $Z = 200.0\,\text{mm}$.
- **Centroid**: $(0.0, 0.0, 100.0)\,\text{mm}$ in CAD coordinates; $(0.0, 0.0, 0.1)\,\text{m}$ in CFD coordinates.
- **Bounding Box (CAD mm)**:
  - $\text{Half-Span} = \frac{200}{2} (\cos 10^\circ + \sin 10^\circ) = 100 \times (0.98480775 + 0.17364818) = 115.845593\,\text{mm}$.
  - Min: $[-115.845593, -115.845593, 0.0]\,\text{mm}$
  - Max: $[+115.845593, +115.845593, 200.0]\,\text{mm}$
- **Bounding Box (CFD m)**:
  - Min: $[-0.115846, -0.115846, 0.0]\,\text{m}$
  - Max: $[+0.115846, +0.115846, 0.200]\,\text{m}$
- **Analytical Volume**:
  - CAD: $V_{\text{CAD}} = 200^3 = 8,000,000.0\,\text{mm}^3$
  - CFD: $V_{\text{CFD}} = 0.2^3 = 0.008\,\text{m}^3$
- **Analytical Surface Area**:
  - CAD: $A_{\text{CAD}} = 6 \times 200^2 = 240,000.0\,\text{mm}^2$
  - CFD: $A_{\text{CFD}} = 6 \times 0.2^2 = 0.240\,\text{m}^2$

#### Discovery of Standalone Sketch Entities in `TestCADSTPAP214.stp`
Inspection of `TestCADSTPAP214.log` and the raw STEP entity graph revealed:
```step
! SUMMARY- Entities Processed Count
! SUMMARY- Point .......................: 1
! SUMMARY- Line ........................: 4
! SUMMARY- Solid Body ..................: 1
```
In the STEP file itself:
- Entity `#35=GEOMETRIC_CURVE_SET('None', (#219, #121, #122, #123, #124))`
- Entities `#121`, `#122`, `#123`, `#124` are `TRIMMED_CURVE` entities named `'Line1'`, `'Line2'`, `'Line3'`, `'Line4'` referencing underlying `LINE` entities (`#125`–`#128`).
- Entity `#219` is a standalone `CARTESIAN_POINT` representing the sketch origin.
- Assigned to `#158=PRESENTATION_LAYER_ASSIGNMENT('1', 'Layer 1', (#121, #122, #123, #124, ...))`.
- These 4 curves and 1 point are **not part of the solid cube's boundary representation** (`MANIFOLD_SOLID_BREP`). They are the 2D sketch used to create the extrusion!

---

### 1.2 `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.*`

#### File Inventory
| File | Format | Size | Description / Origin |
|---|---|---|---|
| `BladeFullRev1CFD.prt` | Siemens NX Part | 2,124,411 B | Native assembly / full turbine blade model with extrusion & linked bodies. |
| `BladeRev1CFD.prt` | Siemens NX Part | 1,460,247 B | Component part of the blade aerodynamic profile. |
| `BladeFullRev1CFD.stp` | STEP AP242 | 5,287,020 B | AP242 export from NX 1938 with PMI, drafting views, and 2 solid bodies. |
| `BladeFullRev1CFDAP203.stp` | STEP AP203 | 3,006,191 B | AP203 export containing 9 solid bodies. |
| `BladeFullRev1CFDAP214.stp` | STEP AP214 | 3,000,503 B | AP214 export containing 9 solid bodies. |
| `BladeRev1CFD.obj` | Wavefront OBJ | 2,284,472 B | High-resolution polygonal surface triangulation of the aerodynamic blade. |
| `bladerev1cfd.mtl` | MTL Material | 224 B | Material definition for `BladeRev1CFD.obj`. |
| `Blade*.log` | Translation Logs | Various | Official NX translator logs documenting entity counts and conversions. |

> **Critical Note on STL**: There is **no `.stl` file** inside `ComplexGeometryExample`. The polygonal format provided for the complex geometry is `BladeRev1CFD.obj`.

#### Detailed Topological Verification of `BladeRev1CFD.obj`
We executed an automated topological audit on `BladeRev1CFD.obj`:
```
Vertices: 30,832
Faces: 61,660 (all triangular)
Edges: 92,490
Open boundary edges (incident to 1 face): 0
Non-manifold multi-edges (incident to >2 faces): 0
Euler-Poincaré Characteristic: chi = V - E + F = 30832 - 92490 + 61660 = 2
Enclosed Signed Volume: +1.883373e+05 mm^3 (+0.0001883373 m^3)
Normal Orientation: Consistently outward-oriented
Bounding Box (CAD mm):
  X: [-77.364941,   +4.631219] mm  (span:  81.996 mm)
  Y: [-29.945928,  +86.810913] mm  (span: 116.757 mm)
  Z: [-100.000000, +200.000000] mm (span: 300.000 mm)
```
**Verdict**: `BladeRev1CFD.obj` is an **authoritative, watertight, 2-manifold genus-0 surface** representing the true aerodynamic surface of the blade. It serves as an immediate, fast, and pristine reference surface for SDF evaluation, DistMesh point seeding, and surface audits.

#### Deep Entity Analysis of `BladeFullRev1CFD.stp` (AP242)
Entity graph traversal revealed:
- **Solid Bodies (`MANIFOLD_SOLID_BREP`)**:
  - `#3929 = MANIFOLD_SOLID_BREP('Linked Body (4)', #3912)`
  - `#3930 = MANIFOLD_SOLID_BREP('Extrude (11)', #3913)`
  - Both reference `#3912` and `#3913`, which are closed topological shells (`CLOSED_SHELL`).
  - Total `ADVANCED_FACE` count: 158.
  - Total `B_SPLINE_CURVE_WITH_KNOTS`: 201.
  - Total `B_SPLINE_SURFACE_WITH_KNOTS`: 44.
- **Drafting & Auxiliary Entities (Fremd-Elemente)**:
  - 9 `DRAUGHTING_MODEL` entities: `'Top'`, `'Front'`, `'Right'`, `'Back'`, `'Bottom'`, `'Left'`, `'Isometric'`, `'Trimetric'`, and `''`.
  - 8 `CAMERA_MODEL_D3` entities.
  - 8 `VIEW_VOLUME` entities.
  - 8 `PLANAR_BOX` entities.
  - 8 `MECHANICAL_DESIGN_AND_DRAUGHTING_RELATIONSHIP` entities.
  - 1 `CONSTRUCTIVE_GEOMETRY_REPRESENTATION` entity.
  - 160 `GEOMETRIC_ITEM_SPECIFIC_USAGE` & 160 `SHAPE_ASPECT` entities.

#### Deep Entity Analysis of `BladeFullRev1CFDAP214.stp` (AP214)
- 9 `MANIFOLD_SOLID_BREP` entities (`#151` to `#159`), each containing a `CLOSED_SHELL` (`#160` to `#168`).
- 235 `ADVANCED_FACE` surfaces.
- 646 `EDGE_CURVE` entities.

---

### 1.3 How the Mesher Must Filter Auxiliary Features & Sketches

CAD models often contain unwanted wireframes, 2D sketches, datum geometry, and drafting views that will crash unconstrained meshers or produce degenerate zero-volume elements. The mesher must implement the following multi-layer filtering pipeline:

```
[ Raw CAD File (STEP / OBJ / STL) ]
                 │
                 ▼
┌──────────────────────────────────────────────┐
│  Layer 1: Semantic & Entity-Type Filtering   │
│  - STEP: Discard GEOMETRIC_CURVE_SET,        │
│    DRAUGHTING_MODEL, CAMERA_MODEL_D3.        │
│  - OBJ: Parse only 'f' (faces) and 'v';      │
│    discard 'l' (lines) and 'p' (points).     │
└──────────────────────────────────────────────┘
                 │
                 ▼
┌──────────────────────────────────────────────┐
│  Layer 2: Topological Boundary Traversal     │
│  - Extract ONLY faces belonging to           │
│    MANIFOLD_SOLID_BREP -> CLOSED_SHELL.      │
│  - Reject open curves/wires not bounding     │
│    a valid 2D surface.                       │
└──────────────────────────────────────────────┘
                 │
                 ▼
┌──────────────────────────────────────────────┐
│  Layer 3: Graph Adjacency & Manifold Check   │
│  - Build edge-to-face adjacency:             │
│    - Interior edges: exactly 2 incident faces│
│    - Open edges: 1 face (boundary/crack)     │
│    - Non-manifold edges: >2 faces            │
│    - Wireframe lines: 0 faces -> DISCARD     │
└──────────────────────────────────────────────┘
                 │
                 ▼
┌──────────────────────────────────────────────┐
│  Layer 4: Connected Component Decomposition  │
│  - Partition mesh faces into components C_k. │
│  - Compute signed volume V(C_k) and Euler chi│
│  - Discard components with V(C_k) == 0       │
│    (zero-thickness sheets/baffles).          │
│  - Retain the dominant closed body           │
│    (chi = 2, open edges = 0, V > 0).         │
└──────────────────────────────────────────────┘
                 │
                 ▼
[ Clean Watertight 3D Aerodynamic Boundary Surface ]
```

1. **STEP Parser Filtering**:
   - In pure Python B-Rep parsing (`step_parser.py`): Extract only `ADVANCED_FACE` entities referenced by `CLOSED_SHELL` in `MANIFOLD_SOLID_BREP`. Never ingest raw `LINE` or `TRIMMED_CURVE` entities that are not part of an `EDGE_CURVE` inside an `EDGE_LOOP` of an `ADVANCED_FACE`.
   - In OpenCASCADE / Gmsh: Query topological entities only at `dim=3` (`TopoDS_Solid`) and extract their exterior bounding shells (`dim=2`). Discard any `dim=1` (curves) or `dim=0` (points).
2. **OBJ Parser Filtering**:
   - OBJ files can contain lines (`l v1 v2`) or points (`p v1`). The parser must ignore `l` and `p` tokens, extracting only polygons (`f v1 v2 v3 ...`) and triangulating quads/n-gons.
3. **Discrete Mesh Filtering**:
   - Filter out degenerate faces where surface area is below numerical threshold ($A < 10^{-14}\,\text{m}^2$).
   - Filter out isolated edges that have 0 incident faces.
   - Separate multi-body assemblies: In `BladeFullRev1CFDAP214.stp`, there are 9 solid bodies. The mesher must allow selecting the main aerodynamic body or computing a boolean union of all solid bodies into a single domain.

---

## 2. CFDSolver Test Suites & Test Runner Infrastructure

### 2.1 Repository Test Inventory

The repository possesses two distinct test suites:

#### Suite A: `backend/geometry/tests/` (Unit Tests for Geometry Module)
Configured in `pytest.ini` (`testpaths = backend/geometry/tests`). Contains 7 test modules:
1. `test_models.py`: BoundingBox creation, span, center, diagonal, uniform scaling, SurfaceMesh creation, face normals, deep copy (5 tests).
2. `test_scaler.py`: Conversion from mm to m, scaling factor validation, rejection of non-positive/non-finite factors (2 tests).
3. `test_parsers.py`: Binary/ASCII STL parsing, OBJ quad triangulation, STEP B-Rep and Gmsh parsing for AP203/AP214/AP242 (10 tests).
4. `test_audit.py`: SurfaceAuditor closedness, Euler characteristic, normal auto-repair, open edge boundary detection, equilateral triangle quality, degenerate face detection (5 tests).
5. `test_bridge.py`: Local CAD bridge detection (NX 1926, Inventor, Ansys), translator execution, CLI fallback (4 tests).
6. `test_exporter.py`: Export to OpenFOAM `constant/triSurface/*.stl` (ASCII & binary) and `*.obj`, `snappyHexMeshDict` block generation (4 tests).
7. `test_pipeline.py`: End-to-end execution on all 6 reference samples (`TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203/214/242.stp`, `TestCAD.prt`), fluent API chaining (7 tests).

**Total in `backend/geometry/tests/`**: 37 tests.

#### Suite B: `tests/` (Adversarial, E2E, SurfaceCheck & ASME V&V Suites)
Contains 8 comprehensive test modules:
1. `test_cad_e2e.py`: Tier 1 (feature coverage), Tier 2 (boundary & corner cases: degenerate, empty, sliver, holes), Tier 3 (cross-feature interactions), Tier 4 (all reference CAD samples), programmatic OpenFOAM surfaceCheck, ASME V&V assertions.
2. `test_surface_check.py`: Formal implementation of all 10 OpenFOAM `surfaceCheck` checks (illegal faces, tri quality, edge magnitudes, close unconnected points, open edges, multiple edges, connected components, normal orientation zones, outward orientation via signed volume, self-intersections).
3. `test_adversarial_audit.py`: Adversarial stress tests (3-face Y-junctions, 4-face X-junctions, T-junctions, bow-tie pinch vertices, single/multi-face holes, disconnected components, Moeller self-intersection checks).
4. `test_adversarial_challenger_2.py`: Scaler input validation (rejecting NaN, Inf, string, negative factors), cKDTree vertex welding, tolerance limits ($10^{-15}$ to $10^{-8}$), real CAD sample pipeline verification.
5. `test_rechallenge_r2_1.py`: Zero-division guards in Moeller intersection algorithm, sliver triangle rejection threshold ($Q < 10^{-5}$), multi-body normal auto-repair, fuzzing permutations (5000 random triangle pairs).
6. `test_empirical_challenger_r2_2.py`: Advanced numerical precision and bounding box invariance.
7. `test_asme_vv.py`: ASME V&V 10 / 20 verification (double precision preservation, rigid-body translation/rotation invariance, analytical volume/area benchmarks, spatial discretization convergence).
8. `test_foam_reader.py`: OpenFOAM `polyMesh` and Field reader tests (synthetic cube, OpenFOAM `skewnessCavity` tutorial mesh with 2500 cells and 8812 faces, `naca0012` mesh, uniform and nonuniform scalar/vector fields, Gauss divergence closure).

### 2.2 Pytest Configuration & Test Runner

- **Configuration File (`pytest.ini`)**:
  ```ini
  [pytest]
  pythonpath = .
  testpaths = backend/geometry/tests
  ```
- **Invocation Modes**:
  1. Default: `pytest` runs only `backend/geometry/tests/` (37 tests).
  2. Full Regression Suite:
     ```powershell
     & "G:\Datein\Projects\CFDSolver\.venv\Scripts\pytest.exe" tests backend/geometry/tests
     ```
  3. Standalone Runner:
     ```powershell
     python tests/run_e2e_tests.py -v --json-report tests/report.json
     ```

### 2.3 Verification of the > 240 Regression Tests Baseline

We executed the full regression suite using Python 3.14.7 and pytest 9.1.1:
```
Target: tests/ and backend/geometry/tests/
Collected: 277 items
Result: 277 PASSED in 23.95s (100% PASS RATE)
```
**Conclusion**: The repository's regression suite contains **277 active tests** (well exceeding the $> 240$ threshold), all of which pass without failures or errors.

### 2.4 Status of `tests/test_mesher.py`

- **Current Status**: `tests/test_mesher.py` does **NOT yet exist**.
- **Required Tests Planned for `tests/test_mesher.py`**:
  To satisfy the prompt's requirement of "mindestens 10 eigenständige Unit- und E2E-Tests" and certify all acceptance criteria, the following 12 tests must be implemented:

| # | Test Function Name | Tested Component | Acceptance Criterion Verified |
|---|---|---|---|
| 1 | `test_strict_positive_volumes` | In-Memory Mesher Core | $V_i > 0 \quad \forall i$ in 2D and 3D. No inverted, collapsed, or negative volume cells. |
| 2 | `test_gauss_divergence_theorem_closure` | DiFVM Topology Generator | $\max_i \|\sum_{f \in \text{faces}(i)} \mathbf{S}_f\| < 10^{-11}$ across all internal cells. |
| 3 | `test_non_orthogonality_consistency` | FVM Geometric Engine | $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ identically on all internal faces. |
| 4 | `test_sph_boundary_detection_div_r` | Eulerian SPH Engine (Wang et al.) | Wall particle detection via position divergence $\text{div }\mathbf{r} < 0.75 d$. |
| 5 | `test_sph_wall_consistency_A_iS` | Eulerian SPH Engine (Wang et al.) | $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$ ($< 10^{-10}$) at physical boundaries. |
| 6 | `test_opmis_linear_moment_reproduction` | Hybrid Coupling (Zhang et al.) | Exact reconstruction of linear field $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$ for P2M and M2P. |
| 7 | `test_mesh_error_map_completeness` | Quality & Error Map Engine | `mesh.error_map` populated with non-orthogonality, skewness, aspect ratio, and solver masks. |
| 8 | `test_inflation_boundary_layers` | Boundary Layer Generator | Prismatic layers at wall patches with geometric growth factor and wall-orthogonal alignment. |
| 9 | `test_cad_sketch_line_filtering` | CAD Ingestion / Feature Filter | Robustly filters out `GEOMETRIC_CURVE_SET` sketch lines from `TestCADSTPAP214.stp` without failure. |
| 10 | `test_e2e_meshing_testcad_cube` | End-to-End Mesher Pipeline | End-to-end meshing and topological audit of the 200 mm rotated cube (`TestCAD.*`). |
| 11 | `test_e2e_meshing_blade_full_rev1` | End-to-End Mesher Pipeline | End-to-end meshing, SDF extraction, and audit of `BladeFullRev1CFD` / `BladeRev1CFD.obj`. |
| 12 | `test_moving_mesh_ale_laplacian_smoothing` | Moving Mesh Engine (ALE) | Boundary node displacement with Laplacian smoothing preserving cell convexity. |

---

## 3. Acceptance Criteria Verification Requirements & Mathematical Formulations

### 3.1 Strict Positive Cell Volumes ($V_i > 0\,\forall i$)
- **Physical Rationale**: A finite-volume cell with zero or negative volume represents an inverted or degenerate cell, which causes division-by-zero or unphysical negative densities/pressures in PISO/SIMPLE solvers.
- **Computation**:
  - In 2D: Triangle signed area via cross product:
    $$V_i = \frac{1}{2} |(x_1 - x_0)(y_2 - y_0) - (y_1 - y_0)(x_2 - x_0)| > 0$$
  - In 3D: Polyhedral cell volume via surface divergence theorem:
    $$V_i = \frac{1}{3} \sum_{f \in \text{faces}(i)} (\mathbf{c}_f \cdot \mathbf{S}_f) > 0$$
- **Verification Threshold**: $\min_i V_i > 0.0$ and $\min_i V_i \ge \epsilon_{\text{vol}} > 0$.

### 3.2 Exact Gauss Divergence Closed Cell Sum ($\max_i \|\sum_{f} \mathbf{S}_f\| < 10^{-11}$)
- **Physical Rationale**: By Gauss's theorem applied to a constant vector field $\mathbf{c}$:
  $$\int_{\Omega_i} \nabla \cdot \mathbf{c} \, dV = \oint_{\partial \Omega_i} \mathbf{c} \cdot d\mathbf{S} = \mathbf{c} \cdot \sum_{f \in \text{faces}(i)} \mathbf{S}_f = 0 \implies \sum_{f \in \text{faces}(i)} \mathbf{S}_f = \mathbf{0}$$
- **Computation**:
  For each face $f$ with area vector $\mathbf{S}_f$ pointing from owner $O_f$ to neighbour $N_f$:
  $$\mathbf{R}_i = \sum_{f \in \text{faces}(i)} \pm \mathbf{S}_f, \quad \text{where } + \text{ if } i = O_f, \; - \text{ if } i = N_f$$
- **Verification Threshold**:
  $$\max_{i=1 \dots N_{\text{cells}}} \|\mathbf{R}_i\| < 10^{-11}\,\text{m}^2$$

### 3.3 Non-Orthogonality Decomposition Consistency ($\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$)
- **Physical Rationale**: In collocated finite volume methods (DiFVM, OpenFOAM), the face area vector $\mathbf{S}_f$ is decomposed into an orthogonal component $\boldsymbol{\Delta}_f$ parallel to the cell center connecting vector $\mathbf{d} = \mathbf{c}_N - \mathbf{c}_O$ and a non-orthogonal correction vector $\mathbf{k}_f$.
- **Formulation (Over-Relaxed Approach)**:
  $$\boldsymbol{\Delta}_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}} \mathbf{d}$$
  $$\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f$$
- **Identity**:
  $$\boldsymbol{\Delta}_f + \mathbf{k}_f = \boldsymbol{\Delta}_f + (\mathbf{S}_f - \boldsymbol{\Delta}_f) \equiv \mathbf{S}_f$$
- **Verification Threshold**:
  $$\max_{f \in \text{internal faces}} \|\boldsymbol{\Delta}_f + \mathbf{k}_f - \mathbf{S}_f\| < 10^{-14}$$

### 3.4 Eulerian SPH Wall Consistency ($\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$)
- **Physical Rationale** (Wang, Haidn & Hu, 2025): In the fluid interior, kernel symmetry guarantees $\sum_j V_j \nabla_i W_{ij} \approx \mathbf{0}$ (zeroth-order consistency). Near a physical wall, the particle support is truncated, creating an unphysical artificial boundary force. Wang et al. formulate the virtual FVM-equivalent boundary closure vector $\mathbf{A}_{iS}$:
  $$\mathbf{A}_{iS} \equiv -\sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij}$$
- **Boundary Detection Criterion**:
  $$\text{div }\mathbf{r}_i = \sum_{j \in \mathcal{N}(i)} V_j (\mathbf{r}_j - \mathbf{r}_i) \cdot \nabla_i W_{ij}$$
  Particles with $\text{div }\mathbf{r}_i < 0.75 d$ (where $d=2$ or $3$ is spatial dimension) are flagged as boundary particles.
- **Verification Threshold**:
  For all boundary particles $i$:
  $$\|\mathbf{A}_{iS} + \sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij}\| < 10^{-10}$$

### 3.5 OPMIS Linear Field Moment Reproduction
- **Physical Rationale** (Zhang, Ming, Wang & Liang, 2025): In hybrid SPH-FVM coupling, transferring state variables between particles and mesh cells (P2M and M2P) must not introduce artificial numerical diffusion or gradient distortion. The Optimized Particle-Mesh Interpolation Scheme (OPMIS) reproduces exact linear fields $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$.
- **Formulation**:
  Weights $w_j(\mathbf{x})$ satisfy:
  $$\sum_j w_j(\mathbf{x}) = 1, \quad \sum_j w_j(\mathbf{x}) (\mathbf{x}_j - \mathbf{x}) = \mathbf{0}$$
- **Verification Method**:
  Define arbitrary non-trivial linear test field:
  $$f(x, y, z) = 3.5 x - 2.1 y + 4.8 z + 1.25$$
  Interpolate $f$ at target locations $\mathbf{x}_{\text{target}}$:
  $$f_{\text{interp}}(\mathbf{x}_{\text{target}}) = \sum_j w_j(\mathbf{x}_{\text{target}}) f(\mathbf{x}_j)$$
  Compute maximum absolute error:
  $$E_{\text{linear}} = \max_{\text{targets}} |f_{\text{interp}}(\mathbf{x}_{\text{target}}) - f(\mathbf{x}_{\text{target}})| < 10^{-12}$$

### 3.6 Complete Error Map in `mesh.error_map`
- **Data Model**:
  `mesh.error_map` must expose a dictionary or structured object containing:
  - `non_orthogonality`: Face-based non-orthogonality angle in degrees $\theta_f = \arccos\left(\frac{\mathbf{d} \cdot \mathbf{S}_f}{\|\mathbf{d}\| \|\mathbf{S}_f\|}\right)$.
  - `skewness`: Face skewness metric measuring off-center face piercing $\delta_s / \|\mathbf{d}\|$.
  - `aspect_ratio`: Cell-based aspect ratio $L_{\max} / L_{\min}$.
  - `cell_volumes`: Array of cell volumes $V_i$.
  - `face_areas`: Array of face area magnitudes $\|\mathbf{S}_f\|$.
  - `quality`: Cell shape quality metric $Q \in (0, 1]$.
  - `stabilization_mask`: Boolean array flagging cells/faces with $\theta_f > 70^\circ$, $\text{skew} > 4.0$, or $Q < 0.05$ to enable over-relaxed deferral corrections in the linear solver.

---

## 4. Recommendations for Implementer & Critic

1. **CAD Engine Ingestion Route**:
   - For `BladeRev1CFD.obj`: Ingest directly using `backend/geometry/parsers/obj_parser.py`. It is already 100% verified as a watertight 2-manifold surface.
   - For STEP files (`BladeFullRev1CFD.stp` and `TestCADSTPAP214.stp`): Ensure that the STEP parser skips auxiliary entities (`GEOMETRIC_CURVE_SET`, `DRAUGHTING_MODEL`, `CAMERA_MODEL_D3`) and extracts only closed solid shells (`CLOSED_SHELL` in `MANIFOLD_SOLID_BREP`).
2. **Mesh Topology Data Structure**:
   - Build on the Structure of Arrays (SoA) layout established in `backend/native_rust/foam_reader.py` (`FoamMesh`) to ensure zero-cost interoperability with the existing Rust FFI and OpenFOAM export pipeline.
3. **Regression Suite Protection**:
   - All changes must be verified against the 277 regression tests:
     `pytest tests/ backend/geometry/tests/`
   - Create `tests/test_mesher.py` implementing the 12 specified test cases.
