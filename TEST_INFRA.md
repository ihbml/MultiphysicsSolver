# Test Infrastructure & Verification Architecture: CAD & Geometry Ingestion Engine

**Author:** Independent Validator & Critic (`e2e_validator_1`)  
**Target Module:** `backend/geometry`  
**Test Suite Root:** `tests/`  
**Status:** Authoritative Test Infrastructure Baseline  

---

## 1. Test Philosophy & Design Principles

The verification framework for the CFDSolver CAD & Geometry Ingestion Engine adheres to the rigorous standards of **ASME V&V 10** (*Verification and Validation in Computational Solid Mechanics*) and **ASME V&V 20** (*Verification and Validation in Computational Fluid Dynamics and Heat Transfer*), combined with exact programmatic compliance with OpenFOAM's native surface diagnostic utility (`surfaceCheck.C`).

```
+---------------------------------------------------------------------------------------------------+
|                                 4-Tier Verification Hierarchy                                     |
|                                                                                                   |
|  [Tier 1: Feature Coverage]  ──> >=5 isolated test cases per feature (STEP, STL, OBJ, Audit, etc.)|
|  [Tier 2: Boundary & Corner] ──> >=5 edge/failure cases per feature (truncated, sliver, non-man.)|
|  [Tier 3: Cross-Feature]     ──> Pairwise & end-to-end pipeline integrations (NX->STEP->Scale->FOAM)|
|  [Tier 4: Real-World CAD]    ──> Physical ground-truth validation on reference parts (TestCAD.*)   |
|                                                                                                   |
|  [OpenFOAM surfaceCheck]     ──> 10 algorithmic checks matching OpenFOAM v2606 surfaceCheck.C    |
|  [ASME V&V 10 / 20]          ──> Affine invariance, analytical volumes, machine-precision scaling|
+---------------------------------------------------------------------------------------------------+
```

### Core Tenets:
1. **Independent Validation**: The test suite is authored, maintained, and verified independently from the implementation code (`backend/geometry/`). No tests are tailored to accommodate implementation defects or shortcuts.
2. **Authoritative Ground Truth Derivation**:
   - For real-world CAD samples (`resources/CADTestSamples/`), analytical geometric properties are derived from first principles: an exact $200.0\text{ mm}$ cube rotated $10.0^\circ$ around the Z-axis, yielding CAD bounding box $[-115.845593, 115.845593]^2 \times [0.0, 200.0]\text{ mm}$ and CFD bounding box $[-0.115846, 0.115846]^2 \times [0.0, 0.200000]\text{ m}$, volume $V = 0.008\text{ m}^3$, surface area $A = 0.24\text{ m}^2$, and Euler characteristic $\chi = 2$.
   - Topological invariants: 2-manifold closedness ($|F(e)| = 2$, 0 open edges, 0 multi edges).
   - Divergence Theorem signed volume: $V_{\text{signed}} = \frac{1}{6} \sum \mathbf{v}_1 \cdot (\mathbf{v}_2 \times \mathbf{v}_3) > 0$.
3. **No Facade Testing**: Every test exercises real geometry parsing, vertex welding, topological graphs, surface normal calculation, and spatial bounding boxes.
4. **Adversarial & Fault-Tolerant Verification**: Explicit tests for malformed inputs (truncated STL headers, negative indices, non-manifold edges, T-junctions, inverted normals, degenerate triangles, corrupted STEP entities) ensure graceful failure handling and structured exceptions.

---

## 2. Feature Inventory & Verification Matrix

The test suite systematically covers all 27 features identified in `PROJECT.md` across 4 testing tiers:

| # | Feature | Test Module | Verification Method | Pass/Fail Criteria | Tier Coverage |
|---|---|---|---|---|---|
| 1 | STEP AP203 Ingestion | `test_cad_e2e.py` | B-Rep parsing, entity topology extraction | 8 vertices, 6 faces/12 triangles, valid BBox | Tier 1, 2, 4 |
| 2 | STEP AP214 Ingestion | `test_cad_e2e.py` | Automotive schema parsing, layer/color attributes | Watertight shell, unit recognition (mm) | Tier 1, 2, 4 |
| 3 | STEP AP242 Ingestion | `test_cad_e2e.py` | Model-based engineering schema, PMI entities | Watertight shell, analytical BBox | Tier 1, 2, 4 |
| 4 | Binary STL Ingestion | `test_cad_e2e.py` | 80-byte header, uint32 count, 50-byte facet loop | Exact vertex extraction, 12 facets | Tier 1, 2, 4 |
| 5 | ASCII STL Ingestion | `test_cad_e2e.py` | Regex/token parser (`solid`...`endsolid`) | Exact triangle coordinates, welded vertices | Tier 1, 2 |
| 6 | Wavefront OBJ Ingestion | `test_cad_e2e.py` | `v`, `vn`, `vt`, `f`, `g`, `o`, `usemtl` parsing | 8 vertices, 12 faces, patch mappings | Tier 1, 2, 4 |
| 7 | Polygon Triangulation | `test_cad_e2e.py` | Quads and general n-gons ($N \ge 4$) to triangles | Fan triangulation, $N-2$ triangles per polygon | Tier 1, 2, 3 |
| 8 | Topological Vertex Welding | `test_cad_e2e.py` | Spatial voxel hashing with tolerance $\epsilon$ | Deduplicates $3F$ coords to $V$ unique vertices | Tier 1, 2, 3 |
| 9 | B-Rep Tessellation | `test_cad_e2e.py` | Chordal deflection ($\delta_{\text{lin}}$) & angular ($\alpha_{\text{ang}}$) | Discretization accuracy, no self-intersections | Tier 1, 2 |
| 10 | Siemens NX Auto-Detection | `test_cad_e2e.py` | Probes `UGII_BASE_DIR`, registry, filesystem | Returns valid installation info or None | Tier 1, 2 |
| 11 | NX Environment Configurator | `test_cad_e2e.py` | Checks `STEP214UG_DIR`, `ROSE`, trailing slashes | Valid environment dictionary generated | Tier 1, 2 |
| 12 | Native `.prt` Translation | `test_cad_e2e.py` | Wraps `step214ug.cmd` on `TestCAD.prt` | Output STEP generated, exit code 0 | Tier 1, 3, 4 |
| 13 | NX Log Audit Verification | `test_cad_e2e.py` | Parses transaction log for `solids processed >= 1` | Confirms success or detects license error | Tier 1, 2 |
| 14 | Graceful Fallback Handler | `test_cad_e2e.py` | Handles NX unavailability without crashing | Structured exception / fallback to STEP/STL | Tier 1, 2 |
| 15 | 2-Manifold Edge Check | `test_surface_check.py` | Edge-to-face adjacency graph ($|F(e)| == 2$) | 0 open edges, 0 multi edges | Tier 1, 2, 4 |
| 16 | Euler Characteristic | `test_surface_check.py` | Evaluates $\chi = V - E + F == 2$ (genus 0) | Exact integer $\chi = 2$ | Tier 1, 2, 4 |
| 17 | Degenerate Triangle Check | `test_surface_check.py` | Identifies duplicate vertices or zero area | 0 degenerate / collapsed triangles | Tier 1, 2, 4 |
| 18 | Triangle Quality Metric | `test_surface_check.py` | Computes $Q = 4\sqrt{3} A / (a^2+b^2+c^2)$ | $Q_{\min} > 10^{-5}$, 20-bin histogram | Tier 1, 2, 4 |
| 19 | Close Unconnected Points | `test_surface_check.py` | Detects points $< 10^{-6}\text{diag}$ without edge | 0 close unconnected points | Tier 1, 2, 4 |
| 20 | Connected Components | `test_surface_check.py` | Breadth-first face traversal across shared edges | Exactly 1 connected component | Tier 1, 2, 4 |
| 21 | Normal Consistency Check | `test_surface_check.py` | Opposing half-edge traversal across shared edges | 1 consistent normal orientation zone | Tier 1, 2, 4 |
| 22 | Outward Normal Verification | `test_surface_check.py` | Signed volume $V = \frac{1}{6} \sum \mathbf{v}_1 \cdot (\mathbf{v}_2 \times \mathbf{v}_3)$ | $V_{\text{signed}} > 0$; auto-inverts winding if $<0$ | Tier 1, 2, 4 |
| 23 | Self-Intersection Detection | `test_surface_check.py` | Triangle-triangle intersection queries | 0 self-intersections | Tier 1, 2, 4 |
| 24 | Unit Scaling (CAD mm -> CFD m) | `test_cad_e2e.py` | Coordinate scaling with factor $s = 0.001$ | Dimensions match analytical bounds in meters | Tier 1, 2, 3, 4 |
| 25 | Bounding Box Verifier | `test_cad_e2e.py` | Compares $[\mathbf{x}_{\min}, \mathbf{x}_{\max}]$ before & after scaling | Ratio matches $s$ within $10^{-12}$ | Tier 1, 2, 4 |
| 26 | OpenFOAM Exporter | `test_cad_e2e.py` | Exports `<case>/constant/triSurface/*.stl` | File created, valid syntax, watertight | Tier 1, 2, 3, 4 |
| 27 | snappyHexMeshDict Config | `test_cad_e2e.py` | Formats OpenFOAM dictionary geometry block | Valid FoamFile / triSurfaceMesh block syntax | Tier 1, 3 |

---

## 3. 4-Tier Testing Methodology

### Tier 1: Feature Coverage (>=5 test cases per feature)
Each individual feature is tested in isolation with dedicated test methods:
- **STEP AP203**: Valid solid ingestion, entity counting, B-Rep vertex points, length unit extraction, plane surfaces.
- **STEP AP214**: Automotive schema parsing, advanced faces, layer assignments, unit contexts, manifold solid.
- **STEP AP242**: PMI annotations, draughting model handling, vertex extraction, tessellated representations, bounding box.
- **STL Binary**: Header inspection, facet count, coordinate parsing, attribute byte handling, vertex deduplication.
- **STL ASCII**: Keyword parsing, multi-word patch naming, exponential notation, normal extraction, outer loop verification.
- **OBJ Ingestion**: Geometric vertices, vertex normals, texture coordinates, face indexing, group tokens (`g`), material tokens (`usemtl`).
- **Polygon Triangulation**: Triangles (pass-through), quads (2 triangles), pentagons (3 triangles), arbitrary n-gons, non-convex polygons.
- **Vertex Welding**: Exact coincident vertices, vertices within tolerance $\epsilon$, vertices outside tolerance, topological edge graph reconstruction, face index remapping.
- **B-Rep Tessellation**: Chordal deflection tolerance, angular deviation, edge length bounds, planar face subdivision, curved surface discretization.
- **NX CAD Bridge**: Detection of NX 1926 install dir, registry lookup fallback, environment dictionary assembly, CLI translator invocation, log parsing.
- **Geometry Audit**: Watertightness, 2-manifold edge adjacency, Euler characteristic, degenerate area, normal orientation.
- **Geometry Scaling**: Default factor (0.001), custom factor, bounding box span verification, volume scaling ($s^3$), surface area scaling ($s^2$).
- **OpenFOAM Export**: `constant/triSurface/` directory creation, multi-solid ASCII STL export, binary STL export, `snappyHexMeshDict` block generation, patch preservation.

### Tier 2: Boundary & Corner Cases (>=5 test cases per feature)
Adversarial tests designed to probe failure handling, robustness, and numerical limits:
- **Corrupted / Empty Files**: Zero-byte file, headers with 0 triangles, EOF before declared triangle count, non-existent file.
- **Truncated STL**: File size strictly less than $84 + 50N$ bytes, partial float records.
- **Ambiguous Formats**: Binary STL starting with ASCII characters `"solid "` (checks file size vs header heuristic).
- **Degenerate Geometry**: Triangles with duplicate vertices $(v_0 == v_1)$, collinear vertices, area $< 10^{-14}\text{ m}^2$, sliver triangles with equilateral quality $Q < 10^{-5}$.
- **Topological Violations**: Missing faces (open edges), T-junctions (edges shared by 3 or 4 faces), pinched bow-tie vertices (non-manifold vertices).
- **Orientation Defects**: Inverted facet winding (clockwise ordering yielding negative volume), mixed normal orientation zones.
- **Index Outliers**: OBJ with negative relative indices (`f -1 -2 -3`), 1-based indexing, mixed vertex/normal tokens.
- **Close Unconnected Points**: Point pairs separated by $< 10^{-6}\text{diag}$ without an edge.
- **Extreme Scale Coordinates**: Microscopic coordinates ($10^{-9}\text{ m}$), macroscopic coordinates ($10^6\text{ mm}$), zero bounding box.

### Tier 3: Cross-Feature Interactions
Verifies end-to-end integration workflows where features interact:
- **Pipeline 1**: Native NX Translation $\to$ STEP AP214 Ingestion $\to$ B-Rep Tessellation $\to$ Topological Welding $\to$ Unit Scaling $\to$ Geometry Audit $\to$ OpenFOAM Export.
- **Pipeline 2**: Wavefront OBJ with n-gons & materials $\to$ Fan Triangulation $\to$ Named Patch Mapping $\to$ Scaling $\to$ SurfaceCheck Verification $\to$ `snappyHexMeshDict` generation.
- **Pipeline 3**: Unindexed Binary STL $\to$ Spatial Hashing Welding $\to$ Normal Auto-Orientation Repair $\to$ Scaling $\to$ Watertight Export.
- **Pipeline 4 (Error Cascading)**: Corrupted input CAD gracefully caught at ingestion stage without propagating invalid memory state to auditor or exporter.

### Tier 4: Real-World CAD Reference Samples (`resources/CADTestSamples/`)
Rigorous physical validation against the 6 actual CAD reference files:
1. `TestCAD.obj` (Wavefront OBJ)
2. `TestCAD.stl` (Binary STL)
3. `TestCADSTPAP203.stp` (STEP AP203)
4. `TestCADSTPAP214.stp` (STEP AP214)
5. `TestCADSTPAP242.stp` (STEP AP242)
6. `TestCAD.prt` (Siemens NX Native Part via `CADBridge`)

#### Exact Acceptance Dimensions (Rotated 200 mm Cube):
- Cube dimensions: $L = 200.0\text{ mm} = 0.200\text{ m}$.
- Rotation: $\theta = 10.0^\circ$ around Z-axis.
- CAD BBox: $X, Y \in [-115.845593, 115.845593]\text{ mm}$, $Z \in [0.0, 200.0]\text{ mm}$.
- CFD BBox ($s = 0.001$):
  $$X \in [-0.115846, 0.115846]\text{ m} \quad (\Delta X = 0.231691\text{ m})$$
  $$Y \in [-0.115846, 0.115846]\text{ m} \quad (\Delta Y = 0.231691\text{ m})$$
  $$Z \in [0.000000, 0.200000]\text{ m} \quad (\Delta Z = 0.200000\text{ m})$$
- Enclosed Volume: $V = 0.008\text{ m}^3$ ($8.0 \times 10^6\text{ mm}^3$).
- Total Surface Area: $A = 0.240\text{ m}^2$ ($2.4 \times 10^5\text{ mm}^2$).
- Euler-Poincaré Characteristic: $\chi = V - E + F = 2$.
- Boundary Edges ($|F(e)| == 1$): $0$.
- Multiply-Connected Edges ($|F(e)| > 2$): $0$.
- Normal Orientation: Consistently outward ($V_{\text{signed}} > 0$).
- Export Path: `<case_dir>/constant/triSurface/<name>.stl`.

---

## 4. Programmatic OpenFOAM `surfaceCheck` Verification

To ensure exported geometries load seamlessly into OpenFOAM's `snappyHexMesh`, the test harness (`tests/test_surface_check.py`) implements the exact algorithmic logic of OpenFOAM's `applications/utilities/surface/surfaceCheck/surfaceCheck.C`:

```
+----------------------------------------------------------------------------------------------------+
| Check | OpenFOAM C++ Routine        | Algorithmic Test Assertion                                   |
|-------+-----------------------------+--------------------------------------------------------------|
|   1   | illegalFaces / validTri     | Duplicate vertices (v0==v1 etc.) or Area < 1e-14 m^2 == 0    |
|   2   | triQ / triangle::quality    | Equilateral Q = 4*sqrt(3)*Area / (a^2+b^2+c^2) > 1e-5        |
|   3   | edgeMag                     | min(edgeMag) > 1e-6 * span(diag), max(edgeMag) < 2 * span    |
|   4   | closePoints                 | Count of point pairs dist < 1e-6*diag without edge == 0      |
|   5   | openEdges                   | Count of edges with |F(e)| == 1 is strictly 0                |
|   6   | multipleEdges               | Count of edges with |F(e)| > 2 is strictly 0                 |
|   7   | markZones                   | Connected components via face adjacency across edges == 1    |
|   8   | checkOrientation            | Number of normal orientation zones == 1                      |
|   9   | outwardOrientation          | Signed volume V_signed = (1/6)*sum(v0 . (v1 x v2)) > 0       |
|  10   | checkSelfIntersection       | Triangle-triangle intersection queries == 0                  |
+----------------------------------------------------------------------------------------------------+
```

---

## 5. ASME V&V 10 / V&V 20 Code Verification Methodology

The test suite in `tests/test_asme_vv.py` enforces computational physics verification principles:
1. **Affine Invariance**:
   - Rigid-body translations ($\mathbf{x}' = \mathbf{x} + \mathbf{t}$) preserve surface area, volume, Euler characteristic, and edge lengths to machine precision ($\epsilon \le 10^{-14}$).
   - Rigid-body rotations ($\mathbf{x}' = \mathbf{R} \mathbf{x}$) preserve volume, area, and internal triangle quality $Q$ to machine precision ($\epsilon \le 10^{-14}$).
2. **Scaling Exactness**:
   - Scaling vertices by scalar factor $s = 0.001$:
     $$\|\mathbf{v}'_i - \mathbf{v}'_j\| = s \|\mathbf{v}_i - \mathbf{v}_j\|$$
     $$\text{Area}(\mathcal{M}') = s^2 \text{Area}(\mathcal{M})$$
     $$\text{Volume}(\mathcal{M}') = s^3 \text{Volume}(\mathcal{M})$$
     $$\frac{\mathbf{u}' \cdot \mathbf{v}'}{\|\mathbf{u}'\|\|\mathbf{v}'\|} = \frac{\mathbf{u} \cdot \mathbf{v}}{\|\mathbf{u}\|\|\mathbf{v}\|}$$
3. **Discretization Error & Convergence Verification**:
   - Evaluates chordal deviation $\delta$ and facet count $N_f$ on benchmark analytical primitives (unit sphere, circular cylinder) to verify second-order geometric convergence $\mathcal{O}(h^2)$ of surface area error.

---

## 6. Test Suite Architecture & File Layout

```text
tests/
├── __init__.py
├── conftest.py                  # Pytest fixtures, test sample paths, synthetic mesh generators
├── test_cad_e2e.py              # Tiers 1-4: Feature coverage, corner cases, cross-feature, CAD samples
├── test_surface_check.py        # Programmatic OpenFOAM surfaceCheck (all 10 checks)
├── test_asme_vv.py              # ASME V&V 10 / 20 code verification & affine invariants
└── run_e2e_tests.py             # Standalone & CLI test runner with JSON / JUnit reporting
```

---

## 7. Test Execution Commands

### Execution via Virtual Environment Pytest:
```powershell
# Full test suite execution with verbose output
.venv\Scripts\pytest tests/ -v

# Run specific test tiers / modules
.venv\Scripts\pytest tests/test_cad_e2e.py -v
.venv\Scripts\pytest tests/test_surface_check.py -v
.venv\Scripts\pytest tests/test_asme_vv.py -v

# Run with test keyword filtering
.venv\Scripts\pytest tests/ -k "real_cad_samples" -v
.venv\Scripts\pytest tests/ -k "surface_check" -v
```

### Standalone Test Runner:
```powershell
# Run standalone test runner (supports both pytest and direct Python execution)
.venv\Scripts\python tests/run_e2e_tests.py

# Standalone with JSON report output
.venv\Scripts\python tests/run_e2e_tests.py --json-report tests/report.json
```
