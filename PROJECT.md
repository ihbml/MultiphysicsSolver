# Project: CFDSolver CAD & Geometry Ingestion Engine

## Architecture
The CAD & Geometry Ingestion Engine for CFDSolver-Backend (`backend/geometry`) processes multi-format CAD models (ISO STEP AP203/214/242, STL binary/ASCII, Wavefront OBJ, native Siemens NX `.prt`), performs rigorous topological and geometric auditing, transforms spatial dimensions to SI units (mm to m), and exports watertight triangulated surface meshes for OpenFOAM computational volume meshing (`snappyHexMesh`).

### Data Flow
```
[CAD Input: .stp, .stl, .obj, .prt]
             │
             ▼
[Native CAD Bridge (Siemens NX 1926)] ──(if .prt)──> [STEP AP214]
             │
             ▼
[Format Parsers (STEP / STL / OBJ)] ──> [B-Rep Tessellation / Vertex Welding]
             │
             ▼
[Raw SurfaceMesh (Vertices V, Faces F, Edges E, Patches)]
             │
             ▼
[Unit Scaling (200 mm -> 0.2 m, BBox Invariant Check)]
             │
             ▼
[Geometry & Topology Auditor (2-Manifold, Euler chi=2, Normals Outward, SurfaceCheck)]
             │
             ▼
[OpenFOAM Exporter (<case_dir>/constant/triSurface/*.stl, snappyHexMeshDict block)]
```

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---|---|---|---|
| 1 | STEP AP203 Ingestion | Reads B-Rep solids and topology from ISO 10303-21 AP203 files | M1 | spec_miner_1 |
| 2 | STEP AP214 Ingestion | Reads B-Rep solids with layer/color/assembly attributes from AP214 files | M1 | spec_miner_1 |
| 3 | STEP AP242 Ingestion | Reads advanced model-based 3D geometries, PMI, and tessellated shapes from AP242 | M1 | spec_miner_1 |
| 4 | Binary STL Ingestion | Parses 80-byte header, uint32 face count, 50-byte IEEE 754 float32 triangle records | M1 | spec_miner_1 |
| 5 | ASCII STL Ingestion | Text parser for solid/facet normal/vertex/endfacet/endsolid | M1 | spec_miner_1 |
| 6 | Wavefront OBJ Ingestion | Parses v, vn, vt, f, g, o, usemtl tokens | M1 | spec_miner_1 |
| 7 | Polygon Triangulation | Converts quads and n-gons (N >= 4) in OBJ to planar triangles | M1 | spec_miner_1 |
| 8 | Topological Vertex Welding | Spatial hashing vertex deduplication with configurable tolerance | M1 | spec_miner_1 |
| 9 | B-Rep Tessellation | Converts continuous B-Rep surfaces to triangular meshes with chordal/angular controls | M1 | spec_miner_1 |
| 10 | Siemens NX Auto-Detection | Probes UGII_BASE_DIR, registry, and filesystem for NX 1926 | M2 | spec_miner_1 |
| 11 | NX Environment Configurator | Constructs required environment variables (STEP214UG_DIR with trailing slash, etc.) | M2 | spec_miner_1 |
| 12 | Native .prt CLI Translation | Invokes step214ug.cmd/step214ug.exe to translate .prt to STEP | M2 | spec_miner_1 |
| 13 | NX Log Audit Verification | Parses NX translator log to verify solids processed >= 1 and check license status | M2 | spec_miner_1 |
| 14 | Graceful Fallback Handler | Catches native CAD unavailability and falls back to open standards | M2 | spec_miner_1 |
| 15 | 2-Manifold Edge Adjacency Check | Verifies every edge shared by exactly 2 faces; detects open/multi edges | M3 | spec_miner_1 |
| 16 | Euler Characteristic Check | Computes chi = V - E + F and verifies chi == 2 - 2g (chi == 2 for genus-0) | M3 | spec_miner_1 |
| 17 | Degenerate Triangle Check | Detects duplicate vertex indices and near-zero area triangles | M3 | spec_miner_1 |
| 18 | Triangle Quality Metric | Evaluates equilateral quality Q = 4*sqrt(3)*A/(a^2+b^2+c^2) across 20 histogram bins | M3 | spec_miner_1 |
| 19 | Close Unconnected Points Check | Detects points closer than 1e-6 * span(diag) not joined by mesh edges | M3 | spec_miner_1 |
| 20 | Connected Components Analysis | Segments mesh into topologically connected zones | M3 | spec_miner_1 |
| 21 | Normal Consistency Check | Checks opposite half-edge traversal and identifies normal orientation zones | M3 | spec_miner_1 |
| 22 | Outward Orientation Verification | Computes signed volume via Divergence Theorem; auto-inverts winding if V < 0 | M3 | spec_miner_1 |
| 23 | Self-Intersection Detection | Performs BVH-accelerated triangle-triangle intersection testing | M3 | spec_miner_1 |
| 24 | Unit Scaling (CAD mm -> CFD m) | Scales vertex coordinates by factor (e.g. 0.001) with bounding box checks | M3 | spec_miner_1 |
| 25 | Bounding Box Verifier | Computes exact min/max bounds and span vectors before and after scaling | M3 | spec_miner_1 |
| 26 | OpenFOAM constant/triSurface Exporter | Writes clean, watertight surface mesh to <case_dir>/constant/triSurface/<name>.stl | M3 | spec_miner_1 |
| 27 | snappyHexMeshDict Patch Configurator | Formulates geometry dictionary block with named boundary patches | M3 | spec_miner_1 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|---|---|---|---|
| M1 | Core CAD Ingestion & B-Rep Tessellation | Features 1-9 (STEP AP203/214/242, STL binary/ASCII, OBJ, triangulation, welding, tessellation) | none | DONE |
| M2 | Native CAD Bridge (Siemens NX 1926) | Features 10-14 (NX auto-detection, environment setup, CLI translation, log verification, graceful fallback) | none | DONE |
| M3 | Geometry Audit, Transformation & OpenFOAM Export | Features 15-27 (2-manifold, Euler chi=2, normals, surfaceCheck rules, scaling mm->m, triSurface exporter, snappyHexMeshDict, pipeline facade) | M1 | DONE |
| M4 | Final Milestone: E2E Validation & ASME V&V Hardening | 100% E2E test pass (Tiers 1-4) on real test samples, adversarial hardening (Tier 5), 272/272 tests passed | M1, M2, M3, TEST_READY | DONE |

## Interface Contracts

### Data Structures (`backend/geometry/models.py`)
```python
from dataclasses import dataclass, field
import numpy as np
from typing import Dict, List, Optional, Tuple, Any

@dataclass
class SurfaceMesh:
    vertices: np.ndarray          # shape (V, 3), float64
    faces: np.ndarray             # shape (F, 3), int32
    normals: Optional[np.ndarray] = None  # shape (F, 3), float64
    patches: Dict[str, np.ndarray] = field(default_factory=dict) # patch_name -> face_indices
    metadata: Dict[str, Any] = field(default_factory=dict)

@dataclass
class AuditReport:
    is_watertight: bool
    is_manifold: bool
    euler_characteristic: int
    open_edges_count: int
    multi_edges_count: int
    degenerate_faces_count: int
    normal_zones_count: int
    signed_volume: float
    is_outward_oriented: bool
    quality_min: float
    quality_histogram: List[int]
    self_intersections_count: int
    bounding_box_min: np.ndarray
    bounding_box_max: np.ndarray
    passed_openfoam_surfacecheck: bool
    details: Dict[str, Any] = field(default_factory=dict)
```

### Module Contracts
- `parsers.base_parser.BaseParser.parse(file_path: Path) -> SurfaceMesh`
- `bridge.cad_bridge.CADBridge.translate(input_file: Path, output_format: str) -> Path`
- `audit.surface_auditor.SurfaceAuditor.audit(mesh: SurfaceMesh) -> AuditReport`
- `transformation.scaler.GeometryScaler.scale(mesh: SurfaceMesh, scale_factor: float = 0.001) -> SurfaceMesh`
- `exporter.openfoam_exporter.OpenFOAMExporter.export(mesh: SurfaceMesh, case_dir: Path, filename: str) -> Path`
- `pipeline.CADGeometryEngine.ingest_and_prepare(input_file: Path, case_dir: Path, ...) -> Tuple[Path, AuditReport]`

## Code Layout
```text
backend/geometry/
├── __init__.py
├── models.py                   # SurfaceMesh, AuditReport, BoundingBox
├── parsers/
│   ├── __init__.py
│   ├── base_parser.py          # Abstract BaseParser class
│   ├── stl_parser.py           # Fast ASCII & Binary STL parser with welding
│   ├── obj_parser.py           # Wavefront OBJ parser with n-gon triangulation
│   └── step_parser.py          # STEP AP203/214/242 parser & B-Rep tessellator (gmsh/OCCT)
├── bridge/
│   ├── __init__.py
│   ├── cad_bridge.py           # Native CAD bridge detector & manager
│   └── nx_translator.py        # Siemens NX 1926 CLI translator (step214ug, step203ug)
├── audit/
│   ├── __init__.py
│   ├── surface_auditor.py      # Watertight, Euler, Manifold, Normal consistency
│   └── quality_metrics.py      # Equilateral quality Q, edgeMag, closePoints
├── transformation/
│   ├── __init__.py
│   └── scaler.py               # Unit scaling (CAD mm -> CFD m) & BBox validation
├── exporter/
│   ├── __init__.py
│   └── openfoam_exporter.py    # constant/triSurface export & snappyHexMeshDict block
├── pipeline.py                 # CADGeometryEngine unified facade
└── tests/                      # Internal unit tests for geometry components
```
