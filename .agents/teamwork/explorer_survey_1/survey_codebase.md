# Codebase & Environment Survey: CAD & Geometry Ingestion Engine

**Date:** 2026-10-19  
**Target Module:** `backend/geometry` in `G:\Datein\Projects\CFDSolver`  
**Explorer:** Codebase & Environment Explorer Subagent (`explorer_survey_1`)  
**Context / Reference:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`

---

## 1. Executive Summary & Purpose

The objective of this investigation is to provide a comprehensive architectural foundation and environmental survey for the development of an industrial-grade **CAD & Geometry Ingestion Engine** within `backend/geometry`. 

The engine must fulfill four mission-critical capabilities:
1. **Multi-Format Ingestion**: Ingest ISO STEP (AP203 / AP214 / AP242), binary and ASCII STL, and Wavefront OBJ.
2. **Native CAD Bridge**: Automatically detect and drive local CAD installations (specifically Siemens NX 1926) to translate native part files (`.prt`) to neutral exchange formats.
3. **Geometry Audit & Unit Scaling**: Verify watertightness (0 open edges, manifoldness, Euler characteristic $V - E + F = 2$), enforce outward-pointing surface normal consistency, and perform unit transformation ($200\text{ mm} \to 0.2\text{ m}$) with bounding-box verification.
4. **OpenFOAM snappyHexMesh Export**: Generate clean, compliant surfaces in `<case_dir>/constant/triSurface/` accompanied by configured dictionary snippets for `snappyHexMeshDict`.

Our empirical probe verified that the active Windows execution environment is running **Python 3.14.7 (64-bit)** alongside **WSL2 (Ubuntu 26.04)** hosting OpenFOAM 2606. Pre-built binary wheels for Python 3.14 are readily available on PyPI for `numpy`, `scipy`, `trimesh`, `gmsh`, `shapely`, `build123d`, and `pytest`. Furthermore, the Siemens NX 1926 installation was verified on disk, and a live CLI translation test of `TestCAD.prt` using `step214ug.cmd` was successfully executed in under 1 second.

---

## 2. Codebase Anatomy & Architecture

### 2.1 Workspace Overview

The project root is `G:\Datein\Projects\CFDSolver` with the following structural layout:

```text
CFDSolver/
├── backend/
│   ├── core/
│   │   └── case_generator.py          # Generates OpenFOAM cases (controlDict, decomposeParDict, etc.)
│   ├── runner/
│   │   └── wsl_runner.py              # WSL2 headless execution (blockMesh, mpirun, decomposePar)
│   ├── native_cpp/
│   │   └── smr_catalysis/             # OpenFOAM C++ fvOptions source (wmake)
│   ├── native_rust/
│   │   └── foam_fast_io/              # Placeholder for high-speed field reader (PyO3)
│   ├── physics_modules/               # 12 modular physics plugins
│   │   ├── base_module.py             # BasePhysicsModule abstraction
│   │   └── registry.py                # Central ModuleRegistry
│   ├── OPENFOAM_BACKEND_ATLAS.md      # Detailed OpenFOAM 2606 mapping atlas
│   └── README.md
├── frontend/                          # Trame + PyVista 3D Web UI
├── cases/                             # OpenFOAM runtime execution cases
├── OpenFOAM_base/openfoam/            # Upstream OpenCFD OpenFOAM v2606 tree
├── resources/
│   └── CADTestSamples/                # Real-world reference CAD parts
├── COMMUNICATION_BACKEND_FRONTEND.md  # Inter-agent & module communication contract
└── FRONTEND_REQUIREMENTS_ON_BACKEND.md# Frontend parameter schemas & mesh info requirements
```

### 2.2 Status of `backend/geometry`
Currently, `backend/geometry` is **greenfield** (does not exist yet). This allows the Implementer agent to establish a clean, modular, and unconstrained modern architecture adhering to the project's coding standards.

### 2.3 Integration Points with Existing Code
- **`backend/core/case_generator.py`**: Currently creates `system/`, `constant/`, and `0/` directories. The geometry module must feed surface mesh artifacts into `constant/triSurface/` and provide geometry blocks for `system/snappyHexMeshDict`.
- **`backend/runner/wsl_runner.py`**: Executes meshing via WSL (`blockMesh`, `snappyHexMesh`). The generated surfaces must be valid OpenFOAM `triSurfaceMesh` assets.
- **`FRONTEND_REQUIREMENTS_ON_BACKEND.md` (Line 93)**: Explicitly specifies `get_mesh_info()` returning cell/face count, boundary patches, and bounding box ($X_{\min}, X_{\max}$, etc.).

---

## 3. Execution Environment & Toolchain Deep Dive

### 3.1 Host Operating System & Dual-World Mapping
- **Host OS**: Microsoft Windows (amd64).
- **Dual-World Mapping**:
  - Windows Path: `G:\Datein\Projects\CFDSolver\`
  - WSL2 Linux Path: `/mnt/g/Datein/Projects/CFDSolver/`
- **WSL Status**: `Ubuntu-26.04` running under WSL Version 2.
- **OpenFOAM Installation**: Located at `/mnt/g/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam` with configuration script at `OpenFOAM_base/openfoam/etc/bashrc`.

### 3.2 Python Environment & Version
- **Active System Python**: Python 3.14.7 64-bit.
- **Executable**: `C:\Users\Ben Ludwig\AppData\Local\Python\pythoncore-3.14-64\python.exe`.
- **Virtual Environment Status**: No local virtual environment (`.venv`) exists in the project root yet.
- **Standard Library `venv`**: `python -m venv` is functional.
- **Package Management Rule**: Per the project's dependency guidelines, package installations must occur strictly inside a virtual environment (`.venv`), never into the global pythoncore directory.

### 3.3 Native Compilers & Build Tools
- **Windows Host PATH**: `git.exe` is present in `C:\Program Files\Git\cmd\git.exe`.
- Native C++ compilers (`cl.exe`, `gcc`, `clang`) and Rust tools (`rustc`, `cargo`, `cmake`, `ninja`) are **not** present in the default Windows user PATH.
- **Architectural Implication**: The geometry engine should **not** rely on compiling C++ or Rust extensions on the Windows host during installation. Instead, it must utilize pure-Python modules or pre-compiled C++ wheels with bundled DLLs (e.g. `gmsh`, `numpy`, `scipy`).

---

## 4. Package Ecosystem Audit (Python 3.14 Compatibility)

A dry-run resolution probe against PyPI for Python 3.14 (win_amd64) yielded the following availability matrix:

| Package | Status | Version Verified | Delivery Mechanism / Notes |
| :--- | :---: | :---: | :--- |
| **`numpy`** | Available | 2.5.4 | Pre-built `cp314-cp314-win_amd64.whl`. High-speed array & vector math. |
| **`trimesh`** | Available | 5.1.1 | `py3-none-any.whl`. Mesh topology, watertightness, ray/winding audit. |
| **`scipy`** | Available | 1.18.1 | Pre-built `cp314-cp314-win_amd64.whl`. Spatial KDTree, convex hull, sparse matrices. |
| **`gmsh`** | Available | 4.15.2 | Pre-built `py2.py3-none-win_amd64.whl`. **Bundles full OpenCASCADE (OCC) kernel** and B-Rep tessellator. |
| **`shapely`** | Available | 2.2.0 | Pre-built `cp314-cp314-win_amd64.whl` with GEOS bundled. Planar polygon analysis. |
| **`meshio`** | Available | 5.3.5 | `py3-none-any.whl`. Mesh I/O for OpenFOAM, VTK, STL, OBJ. |
| **`build123d`** | Available | 0.13.0 | Uses `cadquery-ocp-novtk-8.0.1.1.0`. Full OpenCASCADE wrapper. |
| **`cadquery`** | Available | 2.8.0 | Uses `cadquery-ocp-7.9.3.1.1`. Full OpenCASCADE wrapper. |
| **`pytest`** | Available | 9.1.1 | `py3-none-any.whl`. Test runner for independent verification. |
| **`pythonocc-core`** | **Unavailable** | None | Not distributed as PyPI wheels for Python 3.14 (Conda-only). **Avoid.** |

### Key Architectural Finding: OpenCASCADE via Gmsh
While `pythonocc-core` is unavailable on PyPI for cp314, `gmsh` (4.15.2) and `cadquery-ocp` (8.0.1) are both fully available as pre-built wheels. `gmsh` in particular is a 35 MB self-contained binary distribution that includes full OpenCASCADE STEP AP203/214/242 B-Rep importing, deflection-controlled tessellation, and surface meshing without requiring external compilation.

---

## 5. CAD Installation & Native Translators Verification

### 5.1 Discovered Local CAD Installations
1. **Siemens NX 1926**:
   - Location: `C:\Program Files\Siemens\NX1926`
   - Native Executables: `NXBIN\step203ug.exe`, `NXBIN\step214ug.exe`, `NXBIN\step242.exe`
   - Wrapper Scripts: `STEP214UG\step214ug.cmd`, `STEP203UG\step203ug.cmd`
2. **Autodesk Inventor 2026**:
   - Location: `C:\Program Files\Autodesk\Inventor 2026`
3. **Ansys v252**:
   - Location: `C:\Program Files\ANSYS Inc\v252`

### 5.2 Siemens NX 1926 CLI Bridge Validation
Direct execution of `step214ug.exe` without environment initialization results in an immediate crash:
```text
Error - Unable to retrieve environment variable STEP214UG_DIR
```

However, execution via the official wrapper `C:\Program Files\Siemens\NX1926\STEP214UG\step214ug.cmd` with required environment variables succeeds completely.

**Live Translation Test Result:**
- Target: `G:\Datein\Projects\CFDSolver\resources\CADTestSamples\TestCAD.prt`
- Command Executed:
  ```powershell
  $env:UGII_BASE_DIR = 'C:\Program Files\Siemens\NX1926'
  $env:STEP214UG_DIR = 'C:\Program Files\Siemens\NX1926\STEP214UG'
  $env:ROSE_DB = 'C:\Program Files\Siemens\NX1926\STEP214UG\'
  $env:ROSE = 'C:\Program Files\Siemens\NX1926\STEP214UG\'
  $env:PATH = 'C:\Program Files\Siemens\NX1926\NXBIN;' + $env:PATH
  & 'C:\Program Files\Siemens\NX1926\STEP214UG\step214ug.cmd' TestCAD.prt o TestCAD_converted.stp
  ```
- Output log:
  ```text
  STATUS-   Converting Part: TestCAD.prt
  STATUS-   Converting to Output Form (Phase 2 of 3)
  STATUS-   100% Completed
  SUMMARY-  Entities Processed: Body: 1, advanced_face: 6, closed_shell: 1, manifold_solid_brep: 1
  ```
- Generated valid 9,161-byte STEP file in `< 1.0` second.

---

## 6. Real Test Data Analysis (`resources/CADTestSamples/`)

All files in `resources/CADTestSamples/` were examined. They represent the exact same benchmark geometry:

| File Name | Format | Size | Topological / Geometric Attributes |
| :--- | :--- | :---: | :--- |
| `TestCAD.obj` | Wavefront OBJ | 504 B | 8 Vertices, 12 Triangular Faces, 1 Solid Group (`g TestCAD`) |
| `TestCAD.stl` | Binary STL | 684 B | 80-byte header, 12 Facets, IEEE 754 32-bit floats |
| `TestCADSTPAP203.stp` | STEP AP203 | 11,409 B | ISO-10303-21, 8 `VERTEX_POINT`, 12 `EDGE_CURVE`, 6 `ADVANCED_FACE` |
| `TestCADSTPAP214.stp` | STEP AP214 | 12,814 B | ISO-10303-21, 8 `VERTEX_POINT`, 12 `EDGE_CURVE`, 6 `ADVANCED_FACE` |
| `TestCADSTPAP242.stp` | STEP AP242 | 34,611 B | ISO-10303-21, same B-Rep + PMI & Draughting Model metadata |
| `TestCAD.prt` | Siemens NX 1938 Part | 108,806 B | Native Parasolid B-Rep container |
| `TestCAD.igs` | IGES 5.3 | 36,818 B | Unit flag `2HMM` (Millimeter), exported from NX 1926 |

### Ground-Truth Analytical Metrics for Verification:
- **Geometry**: A cube with side length $L = 200\text{ mm} = 0.2\text{ m}$.
- **Rotation**: Rotated around the Z-axis by $\theta = \arctan(81.115958 / 115.845593) = 35.0^\circ$ (or $55.0^\circ$ depending on quadrant).
- **Z-Span**: $Z \in [0.0, 200.0]\text{ mm} = [0.0, 0.2]\text{ m}$.
- **XY Extents (Exact)**:
  - $X_{\max} = \sqrt{20000} \cdot \cos(45^\circ - 35^\circ) = 115.845593\text{ mm} = 0.115845593\text{ m}$.
  - $X_{\min} = -115.845593\text{ mm} = -0.115845593\text{ m}$.
  - $Y_{\max} = 115.845593\text{ mm} = 0.115845593\text{ m}$.
  - $Y_{\min} = -115.845593\text{ mm} = -0.115845593\text{ m}$.
- **Analytical Volume**:
  - CAD Units: $V = 200^3 = 8\,000\,000\text{ mm}^3 = 8.0 \times 10^{-3}\text{ m}^3 = 0.008\text{ m}^3$.
  - CFD Units: $V = 0.2^3 = 0.008\text{ m}^3$.
- **Analytical Total Surface Area**:
  - CAD Units: $A = 6 \times 200^2 = 240\,000\text{ mm}^2 = 0.24\text{ m}^2$.
  - CFD Units: $A = 6 \times 0.2^2 = 0.24\text{ m}^2$.
- **Euler-Poincaré Characteristic**:
  - Triangulated mesh: $V = 8, E = 18, F = 12 \implies \chi = 8 - 18 + 12 = 2$.
  - B-Rep faces: $V = 8, E = 12, F = 6 \implies \chi = 8 - 12 + 6 = 2$.

---

## 7. OpenFOAM & snappyHexMesh Integration Specifications

Inspection of OpenFOAM v2606 source code (`applications/utilities/surface/surfaceCheck/surfaceCheck.C`) reveals the strict criteria that snappyHexMesh requires for input surface meshes:

1. **Closed & Watertight (`openEdges == 0`)**:
   Every edge must be shared by **exactly two** triangular faces. Surfaces with edges connected to only 1 face have open holes.
2. **Manifold Topology (`multipleEdges == 0`)**:
   No edge may be shared by more than 2 faces (no T-junctions, internal fins, or non-manifold junctions).
3. **Valid Non-Degenerate Triangles (`illegalFaces == 0`)**:
   No collapsed triangles where two or three vertex indices coincide ($f_0 = f_1$, $f_0 = f_2$, or $f_1 = f_2$), and no collinear zero-area triangles.
4. **Consistent Outward Normal Orientation**:
   All surface face normals must point into the fluid domain (consistently oriented outward for an internal obstacle/body). The signed volume must be positive:
   $$V_{\text{signed}} = \frac{1}{6} \sum_{i} \mathbf{v}_{0,i} \cdot (\mathbf{v}_{1,i} \times \mathbf{v}_{2,i}) > 0$$
5. **Standard Export File Hierarchy**:
   Surfaces must reside in:
   ```text
   <case_dir>/constant/triSurface/<surface_name>.stl
   <case_dir>/constant/triSurface/<surface_name>.obj
   ```
6. **Patch Definition**:
   ASCII STL files should begin with `solid <patch_name>` and conclude with `endsolid <patch_name>` to define boundary patch names recognized by `snappyHexMeshDict`.

---

## 8. Optimal Technology Stack & Architecture for `backend/geometry`

### 8.1 Recommended Technology Stack

1. **Primary Geometry & Mesh Engine**: `trimesh` + `numpy` (with `scipy` for spatial KDTree).
   - *Rationale*: Pre-built wheels exist for Python 3.14. Highly optimized C-extensions for vectorized triangle math, normal calculation, winding repair, and STL/OBJ I/O.
2. **STEP B-Rep Ingestion & Tessellation Engine**:
   - **Tier 1 (Industrial)**: `gmsh` Python API (v4.15.2). Built-in OpenCASCADE kernel allows direct reading of STEP AP203/214/242 and produces high-quality Delaunay/Frontal surface meshes with customizable chordal deflection (`Mesh.MeshSizeFromCurvature`) and angular deviation.
   - **Tier 2 (Fallback / Pure Python)**: An autonomous lightweight ISO-10303-21 STEP parser for polyhedral planar B-Rep faces. If external heavy CAD libraries are missing, the fallback extracts `CARTESIAN_POINT`, `EDGE_CURVE`, and `ADVANCED_FACE` / `FACE_OUTER_BOUND` to construct the triangular faces.
   - **Tier 3 (CAD Bridge)**: `Siemens NX 1926` Bridge (`nx_bridge.py`) converting `.prt` $\to$ `.stp` / `.stl`.
3. **Audit & Verification Engine**: Custom vectorized Python auditor implementing the 6 standard OpenFOAM checks ($V-E+F$, watertightness, manifold edges, degenerate area, outward normals, aspect ratio).

### 8.2 Proposed Module Decomposition

```text
backend/geometry/
├── __init__.py                  # Public API exports
├── models.py                    # Unified CadMesh dataclass, BoundingBox, AuditReport, FaceQuality
├── engine.py                    # GeometryEngine high-level fluent pipeline coordinator
├── io/
│   ├── __init__.py
│   ├── base_importer.py         # Abstract BaseImporter interface
│   ├── stl_importer.py          # Fast binary & ASCII STL reader
│   ├── obj_importer.py          # Wavefront OBJ parser
│   ├── step_importer.py         # Dual-engine STEP importer (Gmsh OCC + Pure-Python B-Rep fallback)
│   └── openfoam_exporter.py     # Exports constant/triSurface/*.stl and snappyHexMeshDict snippets
├── audit/
│   ├── __init__.py
│   ├── auditor.py               # Comprehensive mesh quality & topology auditor
│   ├── normals.py               # Normal computation & winding orientation auto-repair
│   └── scaler.py                # Unit scaling (mm -> m) with BoundingBox verification
└── bridges/
    ├── __init__.py
    └── nx_bridge.py             # Siemens NX 1926 environment detection & CLI translator wrapper
```

### 8.3 Core Data Model Specification (`models.py`)

```python
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple
import numpy as np

@dataclass
class BoundingBox:
    min_pt: np.ndarray  # shape (3,), float64
    max_pt: np.ndarray  # shape (3,), float64
    
    @property
    def span(self) -> np.ndarray:
        return self.max_pt - self.min_pt
    
    @property
    def center(self) -> np.ndarray:
        return (self.min_pt + self.max_pt) / 2.0

@dataclass
class AuditReport:
    is_watertight: bool
    is_manifold: bool
    euler_characteristic: int
    num_open_edges: int
    num_multiply_connected_edges: int
    num_degenerate_faces: int
    normals_consistent: bool
    normals_pointing_outward: bool
    volume: float
    surface_area: float
    bounding_box: BoundingBox
    issues: List[str] = field(default_factory=list)

@dataclass
class CadMesh:
    vertices: np.ndarray          # shape (V, 3), float64
    faces: np.ndarray             # shape (F, 3), int32
    normals: Optional[np.ndarray] = None # shape (F, 3), float64
    name: str = "geometry"
    units: str = "m"
    metadata: Dict[str, Any] = field(default_factory=dict)
```

---

## 9. Test & ASME V&V Verification Strategy

The test suite must be co-located under `tests/geometry/` and run under `pytest`.

### Test Architecture

```text
tests/geometry/
├── __init__.py
├── conftest.py                  # Pytest fixtures loading files from resources/CADTestSamples/
├── test_stl_importer.py         # Tests TestCAD.stl (Binary & ASCII), checks 12 facets
├── test_obj_importer.py         # Tests TestCAD.obj, checks 8 vertices, 12 faces
├── test_step_importer.py        # Tests AP203, AP214, AP242 step files
├── test_nx_bridge.py            # Tests NX installation detection & PRT translation
├── test_auditor.py              # Tests watertightness, manifoldness, Euler characteristic
├── test_scaler.py               # Tests 200 mm -> 0.2 m scaling and bounding box
├── test_openfoam_export.py      # Verifies constant/triSurface export and snappyHexMeshDict syntax
└── test_asme_vv.py              # ASME V&V 20 benchmark: compares against analytical metrics
```

### Verification Criteria (Pass/Fail Gate):
1. **ASME V&V Analytical Cube Gate**:
   - Calculated Volume: $|V - 0.008\text{ m}^3| / 0.008 < 10^{-5}$
   - Calculated Surface Area: $|A - 0.24\text{ m}^2| / 0.24 < 10^{-5}$
   - Bounding Box X/Y/Z spans: $|X_{\text{span}} - 0.2316912| < 10^{-5}\text{ m}$, $|Z_{\text{span}} - 0.200000| < 10^{-5}\text{ m}$.
   - Euler Characteristic: $\chi = 2$.
   - Open Edges: $0$.
   - Degenerate Triangles: $0$.
2. **Format Compatibility Gate**:
   - `TestCAD.obj`, `TestCAD.stl`, `TestCADSTPAP203.stp`, `TestCADSTPAP214.stp`, `TestCADSTPAP242.stp`, `TestCAD.prt` all ingest into functionally identical `CadMesh` representations.

---

## 10. Actionable Implementation Roadmap for Implementer Agent

1. **Environment Initialization**:
   - Create local `.venv`: `python -m venv .venv`.
   - Install required packages: `pip install numpy trimesh scipy gmsh pytest`.
2. **Core Data Models (`backend/geometry/models.py`)**:
   - Define `BoundingBox`, `AuditReport`, and `CadMesh` with array validation.
3. **Format Importers (`backend/geometry/io/`)**:
   - Implement `stl_importer.py` (both binary and ASCII with byte-level parsing).
   - Implement `obj_importer.py` (handling vertices, face indices, and materials).
   - Implement `step_importer.py` with multi-tier engine: Gmsh OpenCASCADE kernel primary + native pure-Python B-Rep parser fallback.
4. **NX CAD Bridge (`backend/geometry/bridges/nx_bridge.py`)**:
   - Implement environment auto-discovery (`UGII_BASE_DIR`, `STEP214UG_DIR`).
   - Implement robust CLI execution of `step214ug.cmd` with clean temporary directory isolation.
5. **Audit & Scaling Pipeline (`backend/geometry/audit/`)**:
   - Implement vectorized edge-sharing hash table for watertightness & manifold verification.
   - Implement signed volume divergence check and normal auto-reversal.
   - Implement unit conversion ($mm \to m$) with bounding box preservation.
6. **OpenFOAM Exporter (`backend/geometry/io/openfoam_exporter.py`)**:
   - Export named ASCII/Binary STL to `<case_dir>/constant/triSurface/`.
   - Generate template snippets for `snappyHexMeshDict`.
7. **Unified Facade (`backend/geometry/engine.py`)**:
   - Provide a clean, fluent interface: `GeometryEngine().load(path).audit().scale().export_openfoam(case_dir)`.
8. **Validation Test Suite (`tests/geometry/`)**:
   - Implement all verification tests and execute `pytest tests/geometry/` to complete acceptance criteria.
