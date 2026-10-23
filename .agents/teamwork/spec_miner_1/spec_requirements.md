# Technical Specification & Acceptance Criteria: CAD & Geometry Ingestion Engine

**Author:** Specification & Criteria Miner  
**Date:** 2026-10-19  
**Target Module:** `CFDSolver-Backend` (`G:\Datein\Projects\CFDSolver\backend\geometry`)  
**Status:** Authoritative Specification (Phase 0 Baseline)  

---

## 1. Executive Summary & Architectural Overview

The **CAD & Geometry Ingestion Engine** is a core subsystem of the CFDSolver platform responsible for ingesting, auditing, repairing, transforming, and exporting 3D CAD geometries for computational fluid dynamics (CFD) simulations powered by OpenFOAM (`snappyHexMesh`).

```text
+-----------------------------------------------------------------------------------+
|                            CAD Ingestion Subsystem                                |
|                                                                                   |
|  [Native CAD: .prt]          [Open Standards: .step, .stp, .stl, .obj]             |
|          |                                       |                                |
|          v                                       |                                |
|  +--------------------+                          |                                |
|  | Native CAD Bridge  | (Siemens NX 1926 CLI)    |                                |
|  | (step214ug.cmd)    |                          |                                |
|  +--------------------+                          |                                |
|          | (AP214 STEP)                          |                                |
|          +-----------------------+---------------+                                |
|                                  |                                                |
|                                  v                                                |
|                   +-------------------------------+                               |
|                   |  Format Parser & Tessellator  |                               |
|                   |  (B-Rep -> Triangular Mesh)   |                               |
|                   +-------------------------------+                               |
|                                  |                                                |
|                                  v                                                |
|                   +-------------------------------+                               |
|                   |   Geometry Quality Auditor    |                               |
|                   |   (Watertight, Manifold,      |                               |
|                   |    Euler, Normals, Quality)   |                               |
|                   +-------------------------------+                               |
|                                  |                                                |
|                                  v                                                |
|                   +-------------------------------+                               |
|                   |   Scaling & Transformation    |                               |
|                   |   (CAD mm -> CFD m [0.001])   |                               |
|                   +-------------------------------+                               |
|                                  |                                                |
|                                  v                                                |
|                   +-------------------------------+                               |
|                   |       OpenFOAM Exporter       |                               |
|                   |  (constant/triSurface/*.stl)  |                               |
|                   |  (snappyHexMeshDict patches)  |                               |
|                   +-------------------------------+                               |
+-----------------------------------------------------------------------------------+
```

---

## 2. Requirements Specification

### R1. CAD Ingestion & Open Standards

#### R1.1. Supported Formats
The engine must provide native parsing and processing capabilities for three primary standard formats:
1. **STEP (Standard for the Exchange of Product Model Data - ISO 10303-21)**:
   - Extensions: `.stp`, `.step`.
   - Application Protocols (AP):
     - **AP203** (*Configuration controlled 3D designs of mechanical parts and assemblies*): Focuses on B-Rep topology (`MANIFOLD_SOLID_BREP`, `CLOSED_SHELL`, `ADVANCED_FACE`).
     - **AP214** (*Core data for automotive mechanical design processes*): Extended with colors, layers, and assemblies (`step_merged_cad_schema`).
     - **AP242** (*Managed model-based 3D engineering*): Combines AP203 & AP214; includes semantic PMI and native tessellated geometries (`TESSELLATED_SHAPE_REPRESENTATION`, `TRIANGULATED_FACE`).
   - Unit Resolution: The parser must inspect `#...=GLOBAL_UNIT_ASSIGNED_CONTEXT(...)` and detect length units (`SI_UNIT(.MILLI., .METRE.)` vs. `SI_UNIT($, .METRE.)`, or conversion factors) to establish the base physical scale.

2. **STL (Stereolithography - 3D Systems)**:
   - Extensions: `.stl`.
   - Modalities:
     - **Binary STL**: Exactly 80-byte header, uint32 little-endian facet count $N$, followed by $N \times 50$-byte facet structs $(3 \times \text{float32 normal}, 9 \times \text{float32 vertices}, \text{uint16 attribute})$. Expected file size: $S = 84 + 50 N$ bytes.
     - **ASCII STL**: Case-insensitive keyword parsing (`solid <name>`, `facet normal ni nj nk`, `outer loop`, `vertex vx vy vz`, `endloop`, `endfacet`, `endsolid`).
   - Vertex Deduplication / Welding: STL stores unindexed, disconnected triangles. The parser must perform topological vertex welding using spatial hashing with tolerance $\epsilon_{\text{weld}} = 10^{-7}\text{ m}$ (or relative $\epsilon_{\text{rel}} = 10^{-6} \cdot \text{diag}(\text{BBox})$) to reconstruct edge-to-face and vertex-to-face adjacency graphs.

3. **OBJ (Wavefront OBJ)**:
   - Extensions: `.obj`.
   - Geometry Tokens: Geometric vertices `v x y z`, vertex normals `vn i j k`, texture coords `vt u v`.
   - Patch & Region Tokens: Group names `g <name>`, object names `o <name>`, material names `usemtl <name>`. These tokens must be mapped to distinct CFD boundary patches.
   - Polygon Tessellation: Triangles (`f v1 v2 v3`), quads (`f v1 v2 v3 v4`), and general n-gons. Any polygon with $>3$ vertices must be automatically triangulated (fan triangulation or ear clipping) into planar triangles.
   - Index Handling: 1-based positive indexing and negative relative indexing (`f -1 -2 -3`).

#### R1.2. B-Rep Tessellation Parameters
When converting continuous B-Rep surfaces (NURBS, analytic planes, cylinders, tori) to discrete triangular meshes, the tessellation module must support configurable discretization controls:

| Parameter | Symbol | Data Type | Default Value | Allowed Range | Unit | Description |
|---|---|---|---|---|---|---|
| Linear Deflection (Chordal Error) | $\delta_{\text{lin}}$ | `float` | `0.0001` (0.1 mm) | $[10^{-6}, 10^{-1}]$ | m | Maximum perpendicular distance between continuous curved surface $S(u, v)$ and approximating planar triangle facet $T$. |
| Angular Tolerance | $\alpha_{\text{ang}}$ | `float` | `0.1745` (10°) | $[0.0175, 0.5236]$ (1° - 30°) | rad | Maximum allowable angle between adjacent surface normals along a curved surface facet. |
| Minimum Edge Length | $h_{\min}$ | `float` | `0.00005` (0.05 mm) | $[10^{-7}, 10^{-2}]$ | m | Lower bound on triangle edge length to prevent degenerate sliver elements. |
| Maximum Edge Length | $h_{\max}$ | `float` | `0.05` (50 mm) | $[10^{-4}, 10.0]$ | m | Upper bound on triangle edge length to enforce minimum mesh density across flat planar walls. |

---

### R2. Native CAD Bridge (Siemens NX 1926)

#### R2.1. Environment & Binary Detection Logic
The bridge must autonomously detect local Siemens NX installations without requiring hardcoded user configuration:
1. **Search Precedence**:
   - Step 1: Environment variable `UGII_BASE_DIR` (e.g., `C:\Program Files\Siemens\NX1926`).
   - Step 2: System registry query:
     - `HKLM:\SOFTWARE\Siemens\NX`
     - `HKLM:\SOFTWARE\Unigraphics Solutions\NX`
   - Step 3: Filesystem probing on standard Windows paths:
     - `C:\Program Files\Siemens\NX1926`
     - `C:\Program Files\Siemens\NX*`
2. **Binary Validation**:
   - Primary executable: `<UGII_BASE_DIR>\NXBIN\step214ug.exe` (and `step203ug.exe`, `step242.exe`).
   - Command wrapper: `<UGII_BASE_DIR>\STEP214UG\step214ug.cmd` (and `step203ug.cmd`).
   - Batch script: `<UGII_BASE_DIR>\STEP214UG\step214ug.bat`.
   - Export definition template: `<UGII_BASE_DIR>\STEP214UG\ugstep214.def`.

#### R2.2. Mandatory Execution Environment Variables
Direct invocation of `step214ug.exe` without NX environment setup triggers fatal memory segmentation (`signal 11 / COLLAPSE in error.cxx`). The bridge process MUST prepare the subprocess environment dictionary with:
- `UGII_BASE_DIR = <NX_INSTALL_DIR>`
- `UGII_ROOT_DIR = <NX_INSTALL_DIR>\UGII`
- `STEP214UG_DIR = <NX_INSTALL_DIR>\STEP214UG\` *(Note: MUST include trailing backslash)*
- `STEP203UG_DIR = <NX_INSTALL_DIR>\STEP203UG\` *(Note: MUST include trailing backslash)*
- `ROSE_DB = <NX_INSTALL_DIR>\STEP214UG\` *(Note: MUST include trailing backslash)*
- `ROSE = <NX_INSTALL_DIR>\STEP214UG\` *(Note: MUST include trailing backslash)*
- `PATH = <NX_INSTALL_DIR>\NXBIN;<NX_INSTALL_DIR>\UGII;<SystemPath>`

#### R2.3. CLI Invocation Syntax
The bridge wraps the command-line translator via:
```cmd
cmd.exe /c "<UGII_BASE_DIR>\STEP214UG\step214ug.cmd" "<input_prt>" o "<output_stp>" l "<log_file>" d "<def_file>"
```
Or direct binary invocation:
```cmd
"<UGII_BASE_DIR>\NXBIN\step214ug.exe" "<input_prt>" o="<output_stp>" l="<log_file>" d="<def_file>"
```
- Parameters:
  - `<input_prt>`: Absolute path to native `.prt` file.
  - `o <output_stp>`: Output STEP file path.
  - `l <log_file>`: Path to translator transaction log.
  - `d <def_file>`: Path to definition file specifying `CHOOSE_DIRECTION = UG to STEP`, `MODULES_MASK = Solids`, `APPLICATION_PROTOCOL = AP214`.

#### R2.4. Process Controls, Timeout & Log Auditing
- **Timeout**: Hard timeout of `120 seconds` (configurable).
- **Exit Codes**:
  - `0`: Process exited normally.
  - Non-zero (`1` etc.): Execution failure, missing license, or unhandled exception.
- **Log Verification**:
  Regardless of exit code, the bridge must inspect the generated `.log` file for authoritative confirmation:
  - Success indicator: `Number of solids processed without problems : [>= 1]`.
  - Partial indicator: `Number of solids with problems output as sheets : [> 0]` (triggers warning).
  - Failure indicator: `Number of solids not processed : [> 0]` or `License Error` or `Cannot connect to license server`.

#### R2.5. Graceful Fallback Contract
If Siemens NX is not installed, the license server is unreachable, or conversion fails:
- The bridge must raise a specialized, structured exception (`NativeCADBridgeUnavailableError`).
- It must log a clear diagnostic warning without crashing the CFD pipeline.
- It must search for an existing converted open standard file (`<name>.stp`, `<name>.stl`, `<name>.obj`) in the input folder.

---

### R3. Geometry Audit & OpenFOAM Export

#### R3.1. Watertightness & 2-Manifold Criteria
A surface mesh $\mathcal{M} = (V, E, F)$ represents a valid solid boundary for OpenFOAM computational volume generation if and only if it satisfies the following topological invariants:

1. **2-Manifold Edge Adjacency**:
   Every edge $e \in E$ must be shared by exactly two triangular faces:
   $$\forall e \in E: \quad |F(e)| = 2$$
   - **Boundary / Open Edges**: $E_{\text{open}} = \{e \in E : |F(e)| = 1\}$. Criterion: $|E_{\text{open}}| = 0$ (no holes/gaps).
   - **Non-Manifold / Multiply-Connected Edges**: $E_{\text{multi}} = \{e \in E : |F(e)| > 2\}$. Criterion: $|E_{\text{multi}}| = 0$ (no internal t-junctions or intersecting sheets).

2. **2-Manifold Vertex Condition**:
   For every vertex $v \in V$, the star of incident triangles $\text{Star}(v)$ must form a single topological disk (fan). Vertices must not join two distinct closed cones (no "pinched / bow-tie" vertices).

3. **No Self-Intersections**:
   No two non-adjacent triangles intersect in 3D Euclidean space:
   $$\forall T_i, T_j \in F \quad (T_i \cap T_j = \emptyset \quad \forall i, j \text{ not sharing edge or vertex})$$

#### R3.2. Euler-Poincaré Characteristic ($\chi$)
For a closed, orientable polyhedral 2-manifold surface of genus $g$:
$$\chi = V - E + F = 2 - 2g$$
- For simply connected, single solid bodies (genus-0, homeomorphic to a sphere $S^2$, including cubes, cylinders, airfoils, vehicle bodies):
  $$\chi = V - E + F = 2$$
- The audit engine must evaluate:
  $$\Delta \chi = |(V - E + F) - (2 - 2g)| = 0$$
- For triangulated genus-0 surfaces, since each face has 3 edges and each edge is shared by 2 faces ($3F = 2E \implies E = 1.5 F$), the invariant simplifies to:
  $$V - \frac{1}{2} F = 2 \iff F = 2V - 4$$
  *(Empirical verification on `TestCAD`: $V = 8 \implies F = 2(8) - 4 = 12$, $E = 18 \implies 8 - 18 + 12 = 2$).*

#### R3.3. Normal Consistency & Outward Orientation
1. **Adjacency Compatibility**:
   For every pair of triangles $T_A = (v_1, v_2, v_3)$ and $T_B$ sharing an edge between vertices $v_i$ and $v_j$, the traversal order along the edge in $T_A$ must be opposite to the order in $T_B$:
   $$\vec{e}_{A} = (v_i \to v_j) \implies \vec{e}_{B} = (v_j \to v_i)$$
   The number of orientation zones with consistent normals must be exactly $1$.
2. **Outward Orientation via Signed Volume**:
   Using Gauss's Divergence Theorem, the signed enclosed volume of the surface is computed by summing tetrahedron determinants:
   $$V_{\text{signed}} = \frac{1}{6} \sum_{i=1}^{F} \mathbf{v}_{i, 1} \cdot \left( \mathbf{v}_{i, 2} \times \mathbf{v}_{i, 3} \right)$$
   - If $V_{\text{signed}} > 0$: Triangle normals consistently point **OUTWARDS**.
   - If $V_{\text{signed}} < 0$: Triangle normals point **INWARDS**. The audit engine must automatically invert the vertex winding order:
     $$(v_1, v_2, v_3) \leftarrow (v_1, v_3, v_2)$$
     to guarantee positive outward-pointing normals.
   - If $|V_{\text{signed}}| < 10^{-12}$: The surface is non-closed or degenerate.

#### R3.4. Scaling Transformation (CAD mm $\to$ CFD m)
CAD models authored in millimeters must be transformed to SI meters before mesh generation:
$$\mathbf{x}_{\text{CFD}} = \mathbf{S} \cdot \mathbf{x}_{\text{CAD}} = 0.001 \cdot \mathbf{x}_{\text{CAD}}$$
- Bounding Box Invariant Check:
  - $\text{Span}_{i, \text{CFD}} = 0.001 \times \text{Span}_{i, \text{CAD}} \quad \forall i \in \{X, Y, Z\}$
  - $V_{\text{CFD}} = 10^{-9} \times V_{\text{CAD}}$
- Verification on `TestCAD`:
  - CAD BBox: $X \in [-115.845593, 115.845593]$ mm, $Y \in [-115.845593, 115.845593]$ mm, $Z \in [0, 200]$ mm.
  - CFD BBox: $X \in [-0.115846, 0.115846]$ m, $Y \in [-0.115846, 0.115846]$ m, $Z \in [0, 0.200000]$ m.

#### R3.5. OpenFOAM Export Structure & Patch Specification
- Target Directory:
  ```text
  <case_dir>/
  └── constant/
      └── triSurface/
          ├── <geometry_name>.stl        # Scaled, watertight surface mesh
          └── <geometry_name>_feature.eMesh  # Extracted feature edges (optional)
  ```
- Naming & `snappyHexMeshDict` Alignment:
  - Patch identifiers in multi-patch geometries (e.g., `inlet`, `outlet`, `wall`, `symmetry`) must be preserved as named regions in the exported surface mesh.
  - Format compatibility: The exported STL must use standard ASCII multi-solid blocks:
    ```stl
    solid wall
      facet normal ...
      ...
    endsolid wall
    solid inlet
      ...
    endsolid inlet
    ```
    or binary format with separate patch files, allowing OpenFOAM's `triSurfaceMesh` to assign distinct boundary types.

---

### R4. Independent Test Suite & ASME V&V Verification

#### R4.1. ASME V&V Principles for CFD Geometry Ingestion
In accordance with **ASME V&V 10** (*Verification and Validation in Computational Solid Mechanics*) and **ASME V&V 20** (*Verification and Validation in Computational Fluid Dynamics and Heat Transfer*):
1. **Software Quality Assurance (SQA)**:
   - Independent test harness executed in isolation by a dedicated validator agent.
   - Comprehensive test assertions on numerical precision, affine invariance, and error handling.
2. **Code Verification (Geometric Invariants)**:
   - Analytical benchmark geometries (e.g. unit sphere, rotated cube with analytical bounding box and volume).
   - Verification that scaling preserves angles ($(\mathbf{u} \cdot \mathbf{v})/(\|\mathbf{u}\|\|\mathbf{v}\|) = \text{const}$) and scales area by $s^2$ and volume by $s^3$ within machine precision ($\epsilon \le 10^{-14}$).
3. **Discretization Error Control**:
   - Verification that tessellation errors do not introduce non-physical boundary perturbations exceeding solver tolerances.

#### R4.2. Exact OpenFOAM `surfaceCheck` Algorithmic Mapping
The independent test suite must programmatically implement and verify the exact checks performed by OpenFOAM's `surfaceCheck` utility (`applications/utilities/surface/surfaceCheck/surfaceCheck.C`):

| Check # | OpenFOAM `surfaceCheck` Mechanism | Algorithmic Definition | Acceptance Criterion |
|---|---|---|---|
| **1** | `illegalFaces` / `validTri` | Duplicate vertices in triangle ($v_0 == v_1 \lor v_0 == v_2 \lor v_1 == v_2$) or collinear vertices ($\text{Area} < 10^{-14}\text{ m}^2$). | **0 illegal triangles** |
| **2** | `triQ` / `triangle::quality` | Equilateral triangle quality metric: $Q = 4\sqrt{3} \frac{\text{Area}}{a^2 + b^2 + c^2} \in [0, 1]$. Values $< 10^{-11}$ flagged as bad faces. | **$Q_{\min} > 10^{-5}$** (no collapsed triangles) |
| **3** | `edgeMag` | Min and max Euclidean length of mesh edges: $L_e = \|\mathbf{v}_{e, 1} - \mathbf{v}_{e, 0}\|$. | $L_{\min} > 10^{-6} \cdot \text{diag}(\text{BBox})$ |
| **4** | `closePoints` | Points closer than $10^{-6} \cdot \text{diag}(\text{BBox})$ without being connected by a mesh edge. | **0 close unconnected points** |
| **5** | `openEdges` | Edges incident to exactly one face: $|F(e)| == 1$. | **0 open edges** |
| **6** | `multipleEdges` | Edges incident to $>2$ faces: $|F(e)| > 2$. | **0 non-manifold edges** |
| **7** | `markZones` | Connected components via face adjacency across non-manifold edges. | **1 connected part** (for single body) |
| **8** | `checkOrientation` | Normals orientation consistency check across shared edges. | **1 single normal zone** |
| **9** | `outwardOrientation` | Enclosed signed volume calculation via Divergence Theorem. | **$V_{\text{signed}} > 0$** |
| **10** | `checkSelfIntersection` | Spatial octree/BVH ray-triangle intersection testing between non-adjacent elements. | **0 self-intersections** |

---

## 3. Authoritative Specification Tables

### Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|---|---|---|---|---|---|---|
| 1 | Ingestion | STEP AP203 Ingestion | Reads B-Rep solids and topology from ISO 10303-21 AP203 files. | File path (`.stp`, `.step`) | Mesh topology (V, E, F), units | Raises `InvalidSTEPFileError` on malformed schema/syntax. | Reference sample `TestCADSTPAP203.stp` |
| 2 | Ingestion | STEP AP214 Ingestion | Reads B-Rep solids with layer/color/assembly attributes from AP214 files. | File path (`.stp`, `.step`) | Mesh topology (V, E, F), patch groups | Raises `InvalidSTEPFileError` on corrupted file. | Reference sample `TestCADSTPAP214.stp` |
| 3 | Ingestion | STEP AP242 Ingestion | Reads advanced model-based 3D geometries, PMI, and tessellated shapes from AP242. | File path (`.stp`, `.step`) | Mesh topology (V, E, F), annotations | Raises `UnsupportedAPSchemaError` on unhandled entities. | Reference sample `TestCADSTPAP242.stp` |
| 4 | Ingestion | Binary STL Ingestion | Parses 80-byte header, uint32 face count, and 50-byte IEEE 754 float32 triangle records. | File path (`.stl`) | Triangles array $(N, 3, 3)$, face normals | Raises `CorruptedSTLError` if file size $\neq 84 + 50N$. | Reference sample `TestCAD.stl` |
| 5 | Ingestion | ASCII STL Ingestion | Text parser for `solid` ... `facet normal` ... `vertex` ... `endfacet` ... `endsolid`. | File path (`.stl`) | Triangles array $(N, 3, 3)$ | Raises `MalformedSTLTextError` on unexpected tokens. | Open standard specification |
| 6 | Ingestion | Wavefront OBJ Ingestion | Parses `v`, `vn`, `vt`, `f`, `g`, `o`, `usemtl`. | File path (`.obj`) | Vertices, faces, patch mappings | Raises `InvalidOBJError` on out-of-range index or parse error. | Reference sample `TestCAD.obj` |
| 7 | Ingestion | Polygon Triangulation | Converts quads and n-gons ($N \ge 4$) in OBJ to triangles. | Face vertex index array $(N \ge 4)$ | Triangulated sub-faces $(N-2, 3)$ | Raises `NonPlanarPolygonError` if polygon cannot be planar triangulated. | Wavefront OBJ spec |
| 8 | Ingestion | Topological Vertex Welding | Deduplicates coincident vertices in unindexed meshes (STL) into topological shared graph. | Unindexed triangle list, spatial tolerance $\epsilon$ | Vertex array $(V, 3)$, Face index array $(F, 3)$, Edge map | Emits warning if $\epsilon$ collapses valid geometry. | Geometry processing theory |
| 9 | Ingestion | B-Rep Tessellation | Converts continuous B-Rep surfaces to planar triangular meshes with chordal and angular deflection. | B-Rep shape, $\delta_{\text{lin}}$, $\alpha_{\text{ang}}$, $h_{\min}$, $h_{\max}$ | Triangular surface mesh $(V, F)$ | Raises `TessellationFailedError` if surface parameterization fails. | OpenCASCADE / Gmsh spec |
| 10 | CAD Bridge | Siemens NX Auto-Detection | Probes `UGII_BASE_DIR`, system registry, and standard paths for Siemens NX 1926. | Filesystem & registry | `NXInstallationInfo` (version, root path, binary paths) | Returns `None` or raises `NXNotInstalledError` if not found. | Probing `C:\Program Files\Siemens\NX1926` |
| 11 | CAD Bridge | NX Environment Configurator | Constructs required environment variables (`STEP214UG_DIR`, `ROSE`, `PATH`) with trailing slashes. | NX base directory | Subprocess environment dict | Raises `ConfigurationError` if directory structures missing. | Empirical CLI probe (`step214ug.cmd`) |
| 12 | CAD Bridge | Native `.prt` CLI Translation | Invokes `step214ug.cmd` / `step214ug.exe` with `o=`, `l=`, `d=` parameters to convert `.prt` to STEP. | Input `.prt` path, output `.stp` path, timeout | Generated STEP file, translation log | Raises `NXTranslationError` on non-zero exit code or timeout. | Empirical CLI probe (`step214ug.cmd`) |
| 13 | CAD Bridge | NX Log Audit Verification | Parses NX translator log to verify that solids processed count $\ge 1$ and no license errors occurred. | Log file path | Status (`Success`, `Partial`, `Failed`) | Raises `NXLicenseError` or `NXConversionFailedError`. | Log inspection (`TestCADSTPAP214.log`) |
| 14 | CAD Bridge | Graceful Fallback Handler | Catches native CAD unavailability and falls back to pre-converted STEP/STL/OBJ files. | Target file path, available files | Fallback file path, warning message | Raises `FileNotFoundError` only if no valid fallback exists. | User requirements R2 |
| 15 | Audit | 2-Manifold Edge Adjacency Check | Verifies that every edge is shared by exactly 2 faces. Counts open ($=1$) and non-manifold ($>2$) edges. | Mesh $(V, F)$ | Edge-to-face count, `open_edges`, `multi_edges` lists | Reports non-manifold violations; fails watertight check. | OpenFOAM `surfaceCheck.C:763-839` |
| 16 | Audit | Euler Characteristic Check | Computes $\chi = V - E + F$; compares against $2 - 2g$ (default $\chi = 2$ for genus-0). | Mesh $(V, E, F)$, genus $g$ | Integer $\chi$, boolean `is_euler_valid` | Fails audit if $\chi \neq 2 - 2g$. | Geometric topology / `surfaceCheck` |
| 17 | Audit | Degenerate Triangle Check | Detects collapsed vertices ($v_0 == v_1 \lor v_0 == v_2 \lor v_1 == v_2$) and zero-area triangles. | Mesh $(V, F)$, $\epsilon_{\text{area}}$ | List of illegal face indices | Fails audit if illegal triangle count $>0$. | OpenFOAM `surfaceCheck.C:448-526` |
| 18 | Audit | Triangle Quality Metric | Evaluates equilateral quality $Q = 4\sqrt{3} A / (a^2 + b^2 + c^2)$ across 20 histogram bins. | Mesh $(V, F)$ | $Q_{\min}, Q_{\max}$, histogram bins | Flags faces with $Q < 10^{-11}$ as `badFaces`. | OpenFOAM `surfaceCheck.C:530-641` |
| 19 | Audit | Close Unconnected Points Check | Identifies point pairs with distance $< 10^{-6} \cdot \text{diag}(\text{BBox})$ not joined by an edge. | Mesh $(V, E)$, bounding box | Count and list of close point pairs | Emits warning / fails check if close points detected. | OpenFOAM `surfaceCheck.C:679-760` |
| 20 | Audit | Connected Components Analysis | Segments mesh into topologically connected zones using breadth-first traversal. | Mesh $(V, E, F)$ | Number of connected parts, part face assignments | Flags multiple parts if single closed body expected. | OpenFOAM `surfaceCheck.C:841-896` |
| 21 | Audit | Normal Consistency Check | Checks that adjacent faces share edges in opposite half-edge traversal directions. | Mesh $(V, F)$ | Number of normal orientation zones, flipped face list | Fails audit if normal zones $>1$. | OpenFOAM `surfaceCheck.C:900-964` |
| 22 | Audit | Outward Orientation Verification | Computes signed volume via Divergence Theorem to verify outward normal direction ($V_{\text{signed}} > 0$). | Mesh $(V, F)$ | $V_{\text{signed}}$, boolean `is_outward` | If $V < 0$, automatically inverts face winding. | Computational geometry theory |
| 23 | Audit | Self-Intersection Detection | Performs BVH-accelerated triangle-triangle and edge-triangle intersection queries. | Mesh $(V, E, F)$ | Number of self-intersections, intersection points | Fails audit if self-intersection count $>0$. | OpenFOAM `surfaceCheck.C:967-1077` |
| 24 | Transformation | Unit Scaling (CAD mm $\to$ CFD m) | Multiplies all vertex coordinates by scale factor $s = 0.001$. | Mesh $(V, F)$, scale factor $s$ | Scaled mesh $(V_{\text{scaled}}, F)$ | Raises `ValueError` on invalid or negative scale factor. | User requirements R3 |
| 25 | Transformation | Bounding Box Verifier | Computes analytical axis-aligned bounding box $[\mathbf{x}_{\min}, \mathbf{x}_{\max}]$ and span vectors. | Mesh $(V, F)$ | $\mathbf{x}_{\min}, \mathbf{x}_{\max}$, $\mathbf{span}$, center | Validates exact bounding box match before and after scaling. | User acceptance criteria |
| 26 | Export | OpenFOAM `constant/triSurface` Exporter | Writes clean, watertight surface mesh to `<case_dir>/constant/triSurface/<name>.stl`. | Case directory, mesh object, format | Path to exported STL/OBJ file | Creates parent directories if missing; validates written file size. | OpenFOAM directory layout |
| 27 | Export | `snappyHexMeshDict` Patch Configurator | Formulates dictionary block for `geometry { <name>.stl { type triSurfaceMesh; name <patch>; } }`. | Geometry metadata, patch names | Formatted dictionary string | Validates syntax compliance with OpenFOAM dictionary rules. | OpenFOAM case convention |

---

### Edge Cases

| # | Feature | Input | Observed Behavior | Handling / Specification Requirement |
|---|---|---|---|---|
| 1 | Binary STL Parser | Binary file starting with ASCII characters `"solid"` | Ambiguous format detection: naive parsers assume ASCII and crash. | Check file size against $84 + 50N$; if $S == 84 + 50N$, treat strictly as binary regardless of header characters. |
| 2 | Binary STL Parser | File with truncated byte count (e.g. $S < 84 + 50N$) | Incomplete read or premature EOF. | Raise `CorruptedSTLError` with exact expected vs actual byte length. |
| 3 | ASCII STL Parser | Facet normals written as `(0, 0, 0)` | Normal vector undefined / degenerate in input file. | Automatically recompute face normal using cross product of vertices: $\mathbf{n} = (\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0) / \|\dots\|$. |
| 4 | OBJ Parser | Non-triangular faces (quads, pentagons, arbitrary n-gons) | Downstream CFD mesher (`snappyHexMesh`) expects purely triangular facets. | Fan triangulate: face $(v_0, v_1, \dots, v_{k-1}) \to \{(v_0, v_1, v_2), (v_0, v_2, v_3), \dots, (v_0, v_{k-2}, v_{k-1})\}$. |
| 5 | OBJ Parser | Negative vertex indices (e.g. `f -1 -2 -3`) | Relative indexing pointing backward from current vertex list size. | Resolve relative index: $idx_{\text{abs}} = len(V) + idx_{\text{rel}}$. |
| 6 | Vertex Welding | Two vertices separated by less than numerical tolerance $\epsilon$ | Unwelded vertices lead to spurious boundary open edges ($|F(e)| = 1$). | Spatial hash grid welding with tolerance $\epsilon = 10^{-7}\text{ m}$. Verify that vertex merge does not flip face normal. |
| 7 | STEP Ingestion | AP242 file containing embedded tessellated shape representations | Hybrid B-Rep and tessellated data. | Prefer native tessellated data if valid and manifold; fallback to B-Rep tessellation if missing or incomplete. |
| 8 | Native CAD Bridge | Siemens NX invoked without trailing backslash in `STEP214UG_DIR` | Binary attempts to load `STEP214UGstep214ug.bin` and crashes with fatal `signal 11`. | Bridge must enforce trailing backslash on all NX directory environment variables (`STEP214UG_DIR`, `ROSE`, etc.). |
| 9 | Native CAD Bridge | NX license server unavailable or network license checkout timeout | Process hangs or exits with code 1 and error in syslog. | Subprocess execution with explicit 120s timeout and log scanner for `License Error`; fallback to open standard files. |
| 10 | Watertight Audit | Surface with 1 missing face (hole) | Exactly 3 open boundary edges ($|F(e)| = 1$), Euler characteristic $\chi = 1$. | Flag as non-watertight; report open edge coordinates; prevent export to `snappyHexMesh`. |
| 11 | Manifold Audit | Two solid cubes touching along an edge (T-junction edge) | Edge incident to 4 faces ($|F(e)| = 4$). | Flag as non-manifold edge violation; reject surface. |
| 12 | Manifold Audit | Two solid cones touching at a single vertex (pinch / bow-tie) | Vertex star consists of two disconnected triangle fans. | Flag as non-manifold vertex violation; reject surface. |
| 13 | Normal Audit | Surface with clockwise vertex winding (normals point inward) | Enclosed signed volume $V_{\text{signed}} = -0.008\text{ m}^3 < 0$. | Automatically detect $V_{\text{signed}} < 0$ and invert face winding order $(v_0, v_1, v_2) \to (v_0, v_2, v_1)$. |
| 14 | Quality Audit | Sliver triangle with high aspect ratio (e.g. edge ratio $1000:1$) | Triangle quality metric $Q < 10^{-5}$ (approaching $0$). | Flag in triangle quality histogram; issue warning for potential `snappyHexMesh` snapping failure. |
| 15 | Scaling | Model with mixed or unscaled units (e.g. already in meters or in inches) | Double-scaling or incorrect scaling. | Inspect file header units (`SI_UNIT(.MILLI., .METRE.)` vs `.INCH.`). Allow explicit user override parameter `input_unit="mm" \| "m" \| "in"`. |

---

## 4. Algorithmic Specifications & Mathematical Contracts

### 4.1. Topological Vertex Welding Algorithm
Given an unindexed array of $F$ triangles with $3F$ vertices $\mathbf{P} \in \mathbb{R}^{3F \times 3}$ and spatial tolerance $\epsilon$:

```python
def weld_vertices(P: np.ndarray, eps: float = 1e-7) -> Tuple[np.ndarray, np.ndarray]:
    """
    Welds coincident vertices using spatial voxel hashing.
    Time Complexity: O(N) average.
    """
    # Quantize coordinates to discrete integer grid
    inv_eps = 1.0 / eps
    grid_coords = np.round(P * inv_eps).astype(np.int64)
    
    unique_vertices = []
    faces = np.empty(len(P) // 3, dtype=(np.int32, 3))
    voxel_map = {} # hash(ix, iy, iz) -> vertex_index
    
    for i, pt in enumerate(P):
        key = (grid_coords[i, 0], grid_coords[i, 1], grid_coords[i, 2])
        if key not in voxel_map:
            v_idx = len(unique_vertices)
            voxel_map[key] = v_idx
            unique_vertices.append(pt)
        else:
            v_idx = voxel_map[key]
        
        tri_idx = i // 3
        vert_pos = i % 3
        faces[tri_idx, vert_pos] = v_idx
        
    return np.array(unique_vertices, dtype=np.float64), faces
```

### 4.2. Manifold & Boundary Audit Algorithm
```python
def audit_manifold_topology(faces: np.ndarray) -> Dict[str, Any]:
    """
    Audits edge-face connectivity for 2-manifold closedness.
    """
    edge_face_map = {}
    for fi, f in enumerate(faces):
        for i in range(3):
            u, v = f[i], f[(i + 1) % 3]
            edge = (min(u, v), max(u, v))
            edge_face_map.setdefault(edge, []).append(fi)
            
    open_edges = [e for e, fl in edge_face_map.items() if len(fl) == 1]
    multi_edges = [e for e, fl in edge_face_map.items() if len(fl) > 2]
    
    is_watertight = (len(open_edges) == 0 and len(multi_edges) == 0)
    
    return {
        "is_watertight": is_watertight,
        "num_edges": len(edge_face_map),
        "open_edges_count": len(open_edges),
        "multi_edges_count": len(multi_edges),
        "open_edges": open_edges,
        "multi_edges": multi_edges
    }
```

### 4.3. Signed Volume & Outward Orientation Algorithm
```python
def compute_signed_volume_and_orient(V: np.ndarray, F: np.ndarray) -> Tuple[float, np.ndarray]:
    """
    Computes signed volume via Divergence Theorem.
    Ensures outward pointing normals by reversing winding if V < 0.
    """
    v0 = V[F[:, 0]]
    v1 = V[F[:, 1]]
    v2 = V[F[:, 2]]
    
    # Determinant: (v0 x v1) . v2
    cross_prod = np.cross(v0, v1)
    signed_vol = np.sum(cross_prod * v2) / 6.0
    
    oriented_F = F.copy()
    if signed_vol < 0.0:
        # Invert winding order to orient outward
        oriented_F[:, [1, 2]] = oriented_F[:, [2, 1]]
        signed_vol = -signed_vol
        
    return signed_vol, oriented_F
```

---

## 5. Module Architecture & Code Layout

The geometry subsystem will reside in `backend/geometry/`:

```text
backend/geometry/
├── __init__.py
├── parsers/
│   ├── __init__.py
│   ├── base_parser.py          # Abstract BaseParser class
│   ├── stl_parser.py           # High-speed ASCII & Binary STL parser
│   ├── obj_parser.py           # Wavefront OBJ parser with n-gon triangulation
│   └── step_parser.py          # ISO STEP (AP203/214/242) parser & B-Rep tessellator
├── bridge/
│   ├── __init__.py
│   ├── cad_bridge.py           # Native CAD Bridge manager & detector
│   └── nx_translator.py        # Siemens NX CLI wrapper (step214ug, step203ug, step242)
├── audit/
│   ├── __init__.py
│   ├── surface_auditor.py      # Watertight, Euler, Manifold, Normal consistency
│   └── quality_metrics.py      # Triangle quality, aspect ratio, close points
├── transformation/
│   ├── __init__.py
│   └── scaler.py               # Unit conversion (mm -> m) & BBox validation
├── exporter/
│   ├── __init__.py
│   └── openfoam_exporter.py    # constant/triSurface export & snappyHexMeshDict generation
└── pipeline.py                 # Unified CAD ingestion facade
```

---

## 6. Acceptance Criteria Checklist (Verifiable Gates)

- [ ] **G1 (Format Ingestion)**: Engine parses all 5 sample files in `resources/CADTestSamples/` (`TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203.stp`, `TestCADSTPAP214.stp`, `TestCADSTPAP242.stp`) without error.
- [ ] **G2 (Topology Verification)**: For all valid samples, `open_edges == 0`, `multi_edges == 0`, and Euler characteristic satisfies $\chi = V - E + F = 2$.
- [ ] **G3 (Normal Orientation)**: Signed volume computation confirms positive volume $V > 0$ and consistent outward-pointing facet normals.
- [ ] **G4 (Scale Invariant)**: Scaling 200 mm cube with factor 0.001 yields exact bounding box $X, Y \in [-0.115846, 0.115846]\text{ m}$ and $Z \in [0, 0.200000]\text{ m}$ with volume $0.008\text{ m}^3 \pm 10^{-7}$.
- [ ] **G5 (NX Bridge Execution)**: NX bridge properly detects Siemens NX 1926 at `C:\Program Files\Siemens\NX1926`, builds environment variables with trailing slashes, invokes `step214ug.cmd`, and verifies output log.
- [ ] **G6 (Fallback Handling)**: Disabling NX bridge gracefully triggers fallback to existing STEP/STL/OBJ files without uncaught exceptions.
- [ ] **G7 (OpenFOAM Export)**: Generates valid STL in `<case_dir>/constant/triSurface/` and outputs corresponding `snappyHexMeshDict` geometry block.
- [ ] **G8 (SurfaceCheck Compliance)**: Passes all 10 OpenFOAM `surfaceCheck` algorithmic criteria with 0 illegal triangles, 0 open edges, and 0 self-intersections.
