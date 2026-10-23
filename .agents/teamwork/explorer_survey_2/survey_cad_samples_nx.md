# CAD Samples & Siemens NX Ingestion Survey Report

**Author**: Explorer Agent (`explorer_survey_2`)  
**Date**: 2026-10-19  
**Target Module**: `backend/geometry` (CFDSolver Platform)  
**Reference Samples**: `resources/CADTestSamples/`  

---

## 1. Executive Summary

This investigation delivers a comprehensive, empirical audit of the reference CAD test files in `resources/CADTestSamples/` and evaluates the local CAD environment (Siemens NX 1926, Autodesk Inventor 2026, Ansys v252).

### Key Findings
1. **Geometry Ground Truth**:
   All reference models (`TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203.stp`, `TestCADSTPAP214.stp`, `TestCADSTPAP242.stp`, `TestCAD.prt`) represent the **exact same geometry**:
   - A **$200.0\text{ mm}$ cube** centered in the XY-plane at $(0.0, 0.0)$, with base sitting at $Z = 0.0\text{ mm}$ and top at $Z = 200.0\text{ mm}$ (cube center: $(0, 0, 100)\text{ mm}$).
   - The cube is **rotated by exactly $10.0^\circ$ counter-clockwise around the Z-axis** ($\theta = 10.0^\circ \approx 0.174533\text{ rad}$).
   - Bounding Box in CAD units ($\text{mm}$):
     $$X \in [-115.845593, 115.845593]\text{ mm} \quad (\Delta X = 231.691186\text{ mm})$$
     $$Y \in [-115.845593, 115.845593]\text{ mm} \quad (\Delta Y = 231.691186\text{ mm})$$
     $$Z \in [0.0, 200.0]\text{ mm} \quad (\Delta Z = 200.0\text{ mm})$$
   - Bounding Box after standard CFD scaling ($1\text{ mm} \to 0.001\text{ m}$):
     $$X \in [-0.115846, 0.115846]\text{ m} \quad (\Delta X = 0.231691\text{ m})$$
     $$Y \in [-0.115846, 0.115846]\text{ m} \quad (\Delta Y = 0.231691\text{ m})$$
     $$Z \in [0.0, 0.200]\text{ m} \quad (\Delta Z = 0.200\text{ m})$$
     Volume $V = 0.008\text{ m}^3$ ($8 \times 10^6\text{ mm}^3$), Surface Area $A = 0.24\text{ m}^2$ ($2.4 \times 10^5\text{ mm}^2$).
   - Topology: Watertight, 2-manifold closed shell with Euler characteristic $\chi = V - E + F = 8 - 12 + 6 = 2$ (for B-Rep) and $\chi = 8 - 18 + 12 = 2$ (for triangulated STL/OBJ). All facet normals point consistently outwards into the fluid domain.

2. **Siemens NX 1926 CLI Automation**:
   - Siemens NX 1926 is installed at `C:\Program Files\Siemens\NX1926`.
   - Licensing is fully operational via network license server: `SPLM_LICENSE_SERVER = 50072@license3.rz.rwth-aachen.de`.
   - Native command-line translators for STEP AP214 (`step214ug.cmd`), STEP AP203 (`step203ug.cmd`), STEP AP242 (`TRANSLATORS\step242\step242.exe`), and Wavefront OBJ (`TRANSLATORS\wavefrontobj\wavefrontobj.exe`) are present.
   - **Empirical Execution Validated**: Live translations of `TestCAD.prt` using `step214ug.cmd`, `step203ug.cmd`, and `step242.exe` executed synchronously in ~2 seconds, producing valid STEP files with exit code 0.
   - Tessellation control parameters (chordal deflection and angular resolution) are directly configurable in the translator configuration templates (`nxobj.def`, `ugstep242.def`).

3. **Other CAD Installations**:
   - **Autodesk Inventor 2026** is installed at `C:\Program Files\Autodesk\Inventor 2026`. Its architecture uses COM-based translation add-ins (`Inventor.exe`, `Bin\Addins`), suited for Windows COM automation.
   - **Ansys v252** is installed at `C:\Program Files\ANSYS Inc\v252` with active license server `50198@license3.rz.rwth-aachen.de`.

---

## 2. In-Depth CAD Reference Sample Audit

The directory `G:\Datein\Projects\CFDSolver\resources\CADTestSamples\` contains 13 files. Below is the detailed breakdown:

### 2.1 File Catalog & Geometry Summary

| Filename | File Size | Format / Schema | Entities / Counts | Bounding Box [Xmin, Xmax] x [Ymin, Ymax] x [Zmin, Zmax] (mm) | Unit Tag |
|---|---|---|---|---|---|
| `TestCAD.obj` | 504 B | Wavefront OBJ | 8 vertices, 12 triangular faces | $[-115.845593, 115.845593] \times [-115.845593, 115.845593] \times [0.0, 200.0]$ | Implicit mm (referenced in `testcad.mtl`) |
| `TestCAD.stl` | 684 B | Binary STL | 12 facets, 8 unique vertices | $[-115.845596, 115.845596] \times [-115.845596, 115.845596] \times [0.0, 200.0]$ | Implicit mm |
| `TestCADSTPAP203.stp` | 11,409 B | STEP AP203 (`MIM_LF`) | 1 `manifold_solid_brep`, 1 `closed_shell`, 6 `advanced_face`, 12 `edge_curve`, 8 `vertex_point` | Same exact 8 vertices: $[-115.845593, 115.845593] \times [-115.845593, 115.845593] \times [0.0, 200.0]$ | Explicit: `SI_UNIT(.MILLI.,.METRE.)` (`#248`) |
| `TestCADSTPAP214.stp` | 12,814 B | STEP AP214 (`AUTOMOTIVE_DESIGN`) | 1 `manifold_solid_brep`, 1 `closed_shell`, 6 `advanced_face`, 12 `edge_curve`, 8 `vertex_point` | Same exact 8 vertices: $[-115.845593, 115.845593] \times [-115.845593, 115.845593] \times [0.0, 200.0]$ | Explicit: `SI_UNIT(.MILLI.,.METRE.)` (`#259`) |
| `TestCADSTPAP242.stp` | 34,611 B | STEP AP242 (`AP242_MIM_LF`) | 1 `manifold_solid_brep`, 6 `advanced_face`, 8 `vertex_point`, 8 MBD/PMI Draughting Views | Same exact 8 vertices: $[-115.845593, 115.845593] \times [-115.845593, 115.845593] \times [0.0, 200.0]$ | Explicit: `SI_UNIT(.MILLI.,.METRE.)` (`#548`) |
| `TestCAD.prt` | 108,806 B | Siemens NX Part (`SPLMSSTR`) | 1 Solid Body, 6 Faces, 12 Edges, 8 Vertices | Identical rotated 200 mm cube in NX model space | Native NX (mm) |
| `TestCAD.catpart` | 58,140 B | CATIA V5 Part | Native CATIA Part | Identical cube | CATIA mm |
| `TestCAD.igs` | 36,818 B | IGES 5.3 | 6 Trimmed B-Spline surfaces, 12 line curves | Identical cube | IGES mm |
| `testcad.mtl` | 219 B | MTL Material | `newmtl TestCADMaterial0`, `Kd 0.8 0.8 0.8` | N/A | N/A |
| `TestCAD.log` | 3,112 B | Text Log | NX 1926 CATIA V5 & IGES export logs | N/A | N/A |
| `TestCADSTPAP203.log` | 6,182 B | Text Log | NX 1926 AP203 export log | N/A | N/A |
| `TestCADSTPAP214.log` | 5,380 B | Text Log | NX 1926 AP214 export log | N/A | N/A |
| `TestCADSTPAP242.log` | 5,896 B | Text Log | NX 1926 AP242 export log | N/A | N/A |

---

### 2.2 Mathematical Proof of Geometry & Rotation

Let the cube have edge length $L = 200.0\text{ mm}$ and center $(X_c, Y_c) = (0.0, 0.0)$.
The unrotated square corners in the XY plane before rotation are:
$$P_0 = (\pm 100, \pm 100)$$

Applying rotation matrix $R_z(\theta)$ with $\theta = 10^\circ$:
$$\cos(10^\circ) = 0.984807753012208$$
$$\sin(10^\circ) = 0.173648177666930$$

The four rotated $(X, Y)$ vertices are computed as:
$$X_1 = -100 \cos(10^\circ) + 100 \sin(10^\circ) = -98.480775 + 17.364818 = -81.1159575\text{ mm}$$
$$Y_1 = 100 \sin(10^\circ) + 100 \cos(10^\circ) = 17.364818 + 98.480775 = 115.8455931\text{ mm}$$

$$X_2 = -100 \cos(10^\circ) - 100 \sin(10^\circ) = -98.480775 - 17.364818 = -115.8455931\text{ mm}$$
$$Y_2 = -100 \cos(10^\circ) + 100 \sin(10^\circ) = -98.480775 + 17.364818 = -81.1159575\text{ mm}$$

$$X_3 = 100 \cos(10^\circ) - 100 \sin(10^\circ) = 98.480775 - 17.364818 = 81.1159575\text{ mm}$$
$$Y_3 = -100 \sin(10^\circ) - 100 \cos(10^\circ) = -17.364818 - 98.480775 = -115.8455931\text{ mm}$$

$$X_4 = 100 \cos(10^\circ) + 100 \sin(10^\circ) = 98.480775 + 17.364818 = 115.8455931\text{ mm}$$
$$Y_4 = -100 \sin(10^\circ) + 100 \cos(10^\circ) = -17.364818 + 98.480775 = 81.1159575\text{ mm}$$

The $Z$ coordinates are:
- Bottom face: $Z = 0.000000\text{ mm}$
- Top face: $Z = 200.000000\text{ mm}$

This matches the numbers in `TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203.stp`, `TestCADSTPAP214.stp`, and `TestCADSTPAP242.stp` to machine precision.

---

### 2.3 Detailed File Examination

#### A. `TestCAD.obj`
- **Header**: `# Created with Kaydara FBX`, `mtllib testcad.mtl`, `g TestCAD`, `usemtl TestCADMaterial0`
- **Vertex definitions**: 8 vertices (`v -81.115958 115.845593 200.000000` to `v 115.845593 81.115958 200.000000`).
- **Face definitions**: 12 triangular faces (`f 1 2 3`, `f 3 4 1`, ..., `f 7 5 3`), 1-based indexing, pure triangles, manifold.

#### B. `TestCAD.stl`
- **File size**: Exactly $684\text{ bytes} = 80\text{ (header)} + 4\text{ (triangle count)} + 12 \times 50\text{ (facet records)}$.
- **Header**: 80 ASCII space characters ($0\text{x}20$).
- **Facet records**: 12 triangles.
- **Normal vectors & winding check**:
  - Tri 1 & 2: normal $(-0.9848, 0.1736, 0.0)$ (pointing outward along negative X')
  - Tri 3 & 4: normal $(-0.1736, -0.9848, 0.0)$ (pointing outward along negative Y')
  - Tri 5 & 6: normal $(0.9848, -0.1736, 0.0)$ (pointing outward along positive X')
  - Tri 7 & 8: normal $(0.1736, 0.9848, 0.0)$ (pointing outward along positive Y')
  - Tri 9 & 10: normal $(0.0, 0.0, 1.0)$ (pointing outward along +Z at $Z=200\text{ mm}$)
  - Tri 11 & 12: normal $(0.0, 0.0, -1.0)$ (pointing outward along -Z at $Z=0\text{ mm}$)
- All facet vertices follow the counter-clockwise right-hand rule $\vec{n} = (\vec{v}_2 - \vec{v}_1) \times (\vec{v}_3 - \vec{v}_1)$.
- All edges appear in exactly 2 triangles in opposing directions $\to$ **Watertight Manifold**.

#### C. `TestCADSTPAP203.stp` & `TestCADSTPAP214.stp`
- Generated by: `SIEMENS PLM Software NX 1938` via `ST-DEVELOPER v16.7`.
- Schema:
  - AP203: `AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF { 1 0 10303 403 2 1 2}`
  - AP214: `AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 1 }`
- Explicit Unit Block:
  ```step
  #248=(
  LENGTH_UNIT()
  NAMED_UNIT(*)
  SI_UNIT(.MILLI.,.METRE.)
  );
  ```
  Uncertainty distance accuracy value: `2.E-5 mm`.
  Plane angle unit: `CONVERSION_BASED_UNIT('DEGREE', #246)` (`0.0174532925 RADIAN`).
  Solid angle unit: `SI_UNIT($, .STERADIAN.)`.
- B-Rep Entities:
  - 1 `MANIFOLD_SOLID_BREP`
  - 1 `CLOSED_SHELL`
  - 6 `ADVANCED_FACE` (each bounded by 1 `FACE_OUTER_BOUND` with 1 `EDGE_LOOP`)
  - 12 `EDGE_CURVE` (each referencing 1 `LINE` geometry)
  - 24 `ORIENTED_EDGE` (each edge traversed once forward, once reversed)
  - 8 `VERTEX_POINT` (each referencing 1 `CARTESIAN_POINT`)

#### D. `TestCADSTPAP242.stp`
- Schema: `AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF { 1 0 10303 442 1 1 4 }`
- Includes Model-Based Definition (MBD) and PMI annotations:
  - 8 `DRAUGHTING_MODEL` items for standard views: `Top`, `Front`, `Right`, `Back`, `Bottom`, `Left`, `Isometric`, `Trimetric`.
  - 208 `CARTESIAN_POINT` entities (including framing boxes and camera positions).
  - Explicit Unit: `#548 = (LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.))`.
  - The B-Rep entity tree contains the identical 8 `VERTEX_POINT` items as AP203/AP214.

#### E. `TestCAD.prt`
- Native Siemens NX part binary file.
- Container header: `SPLMSSTR` (Siemens PLM Structured Storage) containing directories `/Root/UG_PART/`, `/Root/FastLoad/`, `/Root/part/`, `/Root/images/`.
- Created by NX 1938; can be opened and translated directly by NX 1926 backward/forward compatibility tools.

---

## 3. Local CAD Environment Investigation

### 3.1 Siemens NX 1926

#### Installation Paths & Environment
- **Install Root (`UGII_BASE_DIR`)**: `C:\Program Files\Siemens\NX1926`
- **Executables Directory (`NXBIN`)**: `C:\Program Files\Siemens\NX1926\NXBIN`
- **License Server (`SPLM_LICENSE_SERVER`)**: `50072@license3.rz.rwth-aachen.de`
- **Language (`UGII_LANG`)**: `german`
- **Ansys Integration Plugin**: `C:\Users\Ben Ludwig\AppData\Roaming\Ansys\v252\Unigraphics\custom_dirs.dat`

#### Available Translators & Verified CLI Commands

| Translator Target | Command-Line Wrapper | Direct Executable | Tested & Verified Command |
|---|---|---|---|
| **STEP AP214** | `C:\Program Files\Siemens\NX1926\STEP214UG\step214ug.cmd` | `NXBIN\step214ug.exe` | `cmd.exe /c ""C:\Program Files\Siemens\NX1926\STEP214UG\step214ug.cmd" "<in.prt>" o "<out.stp>" l "<out.log>""` |
| **STEP AP203** | `C:\Program Files\Siemens\NX1926\STEP203UG\step203ug.cmd` | `NXBIN\step203ug.exe` | `cmd.exe /c ""C:\Program Files\Siemens\NX1926\STEP203UG\step203ug.cmd" "<in.prt>" o "<out.stp>" l "<out.log>""` |
| **STEP AP242** | Custom execution | `TRANSLATORS\step242\step242.exe` | `$env:STEP242_DIR="C:\Program Files\Siemens\NX1926\TRANSLATORS\step242\"; $env:PATH="C:\Program Files\Siemens\NX1926\NXBIN;"+$env:PATH; step242.exe "<in.prt>" "o=<out.stp>" "l=<out.log>" "d=C:\Program Files\Siemens\NX1926\TRANSLATORS\step242\ugstep242.def"` |
| **Wavefront OBJ** | `TRANSLATORS\wavefrontobj\nxobj.bat` | `TRANSLATORS\wavefrontobj\wavefrontobj.exe` | Configured via `nxobj.def` (supports chordal and angular tolerance) |
| **IGES 5.3** | `IGES\iges.bat` | `NXBIN\iges.exe` | Invoked via `iges.cmd` |
| **CATIA V5** | `CATIAV5\catiav5.bat` | `NXBIN\catiav5.exe` | Translates NX parts to/from `.catpart` |

#### Key Technical Discoveries & Gotchas for the NX Bridge
1. **Calling Executables Directly vs Batch Wrappers**:
   Executing `NXBIN\step214ug.exe` without setting environment variables results in a fatal crash:
   `Error - Unable to retrieve environment variable STEP214UG_DIR`.
   Executing `step242.exe` without `STEP242_DIR` similarly crashes.
   **Solution**: Always invoke via `step214ug.cmd` / `step203ug.cmd` or ensure `UGII_BASE_DIR`, `STEP214UG_DIR`, `ROSE_DB`, `ROSE`, and `PATH` are populated in the subprocess environment dictionary.
2. **License Checkout**:
   Execution requires a live connection to `50072@license3.rz.rwth-aachen.de`. If the user is off-network or disconnected from the RWTH VPN / license server, the translator will throw a license error. The `NXBridge` must catch this gracefully and provide a clear message: `"Siemens NX license checkout failed. Please check connection to SPLM_LICENSE_SERVER (50072@license3.rz.rwth-aachen.de)."`.
3. **Execution Speed**:
   Translation of `TestCAD.prt` took **$1.8\text{ seconds}$** in our live test.

---

### 3.2 Autodesk Inventor 2026

- **Installation Directory**: `C:\Program Files\Autodesk\Inventor 2026`
- **Main Binary**: `C:\Program Files\Autodesk\Inventor 2026\Bin\Inventor.exe` ($1.58\text{ MB}$)
- **Architecture**:
  - Unlike Siemens NX, Inventor does not ship standalone command-line `.exe` wrappers for individual translators.
  - Translators are implemented as COM In-Process AddIns (`Bin\Addins`).
  - Headless automation is feasible on Windows via COM Automation (`win32com.client.Dispatch("Inventor.Application")` or `ApprenticeServer`), but requires Inventor background initialization and licensing.
  - Recommended status for the CFDSolver backend: **Secondary/Optional extension**. Priority 1 is Siemens NX.

---

### 3.3 Ansys v252

- **Installation Directory**: `C:\Program Files\ANSYS Inc\v252`
- **Environment**:
  - `ANSYS252_DIR = C:\Program Files\ANSYS Inc\v252\ANSYS`
  - `ANSYSLI_SERVERS = 50198@license3.rz.rwth-aachen.de`
  - `ANSYSLMD_LICENSE_FILE = 50199@license3.rz.rwth-aachen.de`
- **Available Geometry Tools**:
  - `scdm` (SpaceClaim Direct Modeler)
  - `Discovery`
  - `meshing`, `icemcfd`, `fluent`
  - Ansys registers an active NX CAD reader extension in `C:\Users\Ben Ludwig\AppData\Roaming\Ansys\v252\Unigraphics\custom_dirs.dat`.

---

## 4. Architectural Blueprint for `backend/geometry`

To satisfy all requirements from `ORIGINAL_REQUEST.md` (R1 to R4) and prepare for OpenFOAM snappyHexMesh, the `backend/geometry` module should be organized as follows:

```text
backend/geometry/
├── __init__.py                 # Package exports: CADIngestionEngine, GeometryAudit, NXBridge
├── formats/
│   ├── __init__.py
│   ├── base.py                 # Abstract BaseMesh / BaseGeometry data structure
│   ├── stl_reader.py           # Robust Binary & ASCII STL parser
│   ├── obj_reader.py           # Wavefront OBJ parser (vertices, normals, faces, groups)
│   └── step_reader.py          # ISO 10303 STEP B-Rep parser (AP203, AP214, AP242)
├── bridge/
│   ├── __init__.py
│   └── nx_bridge.py            # Siemens NX CLI bridge (detection, license check, step214ug invocation)
├── audit/
│   ├── __init__.py
│   └── quality_checker.py      # Manifoldness, Euler characteristic, normals, watertightness, scaling
└── export/
    ├── __init__.py
    └── openfoam_exporter.py    # Export to constant/triSurface/*.stl and *.obj with named patches
```

### 4.1 Data Structures & Algorithms

1. **Triangular Surface Representation (`SurfaceMesh`)**:
   - `vertices`: array of shape $(N, 3)$, float64
   - `faces`: array of shape $(M, 3)$, int32
   - `normals`: array of shape $(M, 3)$, float64
   - `patches`: mapping of name $\to$ face indices for snappyHexMesh boundary regions.

2. **ASME V&V Geometric Audit Checks**:
   - **Watertight Check**: Build an edge map where each undirected edge $(u, v)$ with $u < v$ records its adjacent face indices and directed orientation. For a closed, watertight manifold 2-surface:
     - Number of open / boundary edges (edges with 1 incident face) must be **strictly 0**.
     - Number of non-manifold edges (edges with $>2$ incident faces) must be **strictly 0**.
     - Number of non-manifold vertices must be **strictly 0**.
     - Every directed edge $(u \to v)$ must have an opposing twin $(v \to u)$ in another face.
   - **Euler Characteristic**:
     $$\chi = V - E + F = 2$$
     For our reference cube: $V = 8$, $E = 18$ (12 cube edges + 6 face diagonals), $F = 12$ triangles:
     $$\chi = 8 - 18 + 12 = 2 \quad \text{(Confirmed)}$$
   - **Normal Orientation**:
     Compute the signed volume of the closed surface:
     $$V_{\text{signed}} = \frac{1}{6} \sum_{i=1}^M \vec{v}_{i,1} \cdot (\vec{v}_{i,2} \times \vec{v}_{i,3})$$
     If $V_{\text{signed}} > 0$, face windings are counter-clockwise from outside and normals point outward. If $V_{\text{signed}} < 0$, windings are inverted and must be automatically corrected by swapping $v_2 \leftrightarrow v_3$.
   - **Degenerate Face Check**:
     Triangle area $A_i = \frac{1}{2} \|(\vec{v}_{i,2} - \vec{v}_{i,1}) \times (\vec{v}_{i,3} - \vec{v}_{i,1})\|$. Faces with $A_i < 10^{-14}\text{ m}^2$ are flagged as degenerate.

3. **Scaling & Unit System**:
   - CAD files in standard mechanical engineering are defined in millimeters ($\text{mm}$).
   - OpenFOAM solvers strictly require SI meters ($\text{m}$).
   - Scale factor: $s = 0.001$.
   - The engine must compute the bounding box before and after scaling:
     - Pre-scaling: $\Delta X = 231.691\text{ mm}$, $\Delta Y = 231.691\text{ mm}$, $\Delta Z = 200.0\text{ mm}$.
     - Post-scaling: $\Delta X = 0.231691\text{ m}$, $\Delta Y = 0.231691\text{ m}$, $\Delta Z = 0.200\text{ m}$.
     - Volume: $V = 0.008\text{ m}^3$.

4. **OpenFOAM `constant/triSurface/` Export**:
   - File format: ASCII/Binary STL or OBJ.
   - For snappyHexMesh, multi-region or named patch STL export is crucial:
     ```stl
     solid wall_bottom
       facet normal 0 0 -1
         outer loop
           vertex ...
         endloop
       endfacet
     endsolid wall_bottom
     solid wall_top
       ...
     endsolid wall_top
     ```
   - Patch naming options: by face normal grouping (e.g. `inlet`, `outlet`, `walls`), by CAD layer/group name, or as a single solid body (`cube`).

---

## 5. Actionable Guidance for Implementer & Validator

### For the Implementer:
1. **Dependency Independence**:
   Ensure core parsers (Binary STL, ASCII STL, Wavefront OBJ) and the audit algorithms run with zero external heavy dependencies (or using lightweight pure Python math / standard library). This guarantees immediate usability in Python 3.14 without requiring binary wheels for `pythonocc` or compilation of heavy C-extensions.
2. **STEP Parsing Strategy**:
   - Provide direct B-Rep vertex and facet extraction for STEP AP203/214/242 by parsing `CARTESIAN_POINT`, `ADVANCED_FACE`, `PLANE`, `SI_UNIT`.
   - Provide an automated bridge option: when `.prt` or `.stp` is supplied, `NXBridge` can convert it using Siemens NX 1926 into clean STL/OBJ if requested.
3. **NX Bridge Robustness**:
   - Detect `UGII_BASE_DIR` automatically from environment or registry.
   - Validate `SPLM_LICENSE_SERVER` availability.
   - Use `step214ug.cmd` as the primary translation tool for `.prt` files.
   - Include timeouts and stderr capture in `subprocess.run`.

### For the Independent Validator:
1. **Verification Test Matrix**:
   - Test each of the 5 reference files (`TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203.stp`, `TestCADSTPAP214.stp`, `TestCADSTPAP242.stp`) through the ingestion pipeline.
   - Assert bounding boxes match within $10^{-5}\text{ m}$:
     $$X_{\text{span}} = 0.231691\text{ m}, \quad Y_{\text{span}} = 0.231691\text{ m}, \quad Z_{\text{span}} = 0.200000\text{ m}$$
   - Assert Watertightness: 0 open edges, 0 non-manifold edges.
   - Assert Euler characteristic: $\chi = 2$.
   - Assert Normal orientation: $V > 0$, normals point outward.
   - Run NX Bridge test on `TestCAD.prt` if NX is installed and licensed.
   - Verify exported OpenFOAM files in `constant/triSurface/` are syntactically valid and compatible with `surfaceCheck`.
