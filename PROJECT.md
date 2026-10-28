# Project: Industrial In-Memory Multiphysics Mesher Backbone

## Architecture
The In-Memory Multiphysics Mesher Backbone for CFDSolver (`backend/meshing`) provides high-performance, in-memory unstructured mesh generation in 2D and 3D with boundary inflation layers, DiFVM graph-topology in Structure of Arrays (SoA), hybrid Eulerian SPH-FVM coupling (Wang et al., 2025; Zhang et al., 2025 OPMIS), and ALE moving mesh dynamics. 

Per user directive, the compute-heavy meshing engine is implemented natively in high-performance **Rust / C++20** (using SIMD, multi-threading, spatial trees, and zero-allocation memory pools) and exposed via standard C-ABI (`extern "C"`). 

**STRICT PERFORMANCE TOLERANCE (0.1% RULE):**
Python is strictly prohibited for any computational loop or numerical evaluation where its execution time exceeds native compiled C++/Rust by more than 0.1%. All core compute algorithms (continuous SDF evaluation, triangle projections, DistMesh Delaunay triangulation and spring force relaxation, boundary layer extrusion, RCM bandwidth reduction, SPH neighbor search and divergence, virtual wall vector A_iS, OPMIS Taylor matrix solving, and ALE Laplacian smoothing) MUST be compiled natively in C++20 or Rust. Python is reserved strictly for high-level CLI routing, configuration parsing, test orchestration, and C-ABI/ctypes bindings to the native shared library.

### Data Flow
```
[CAD Input / Analytical Geometry]
              │
              ▼
[SDF Engine & CAD Feature Filter (M1)] ──> Continuous SDF phi(x), grad phi, h(x)
              │
              ▼
[Inflation & Unstructured Meshing (M2)] ──> Prismatic Boundary Layers + DistMesh Delaunay
              │
              ▼
[RCM Bandwidth Reduction (M2)] ──> Cache-Optimized Node & Cell Numbering
              │
              ▼
[DiFVM Graph-Topology & SoA (M2)] ──> owner, neighbour, S_f, delta_f, k_f, error_map
              │
      ┌───────┴────────────────────────┐
      ▼                                ▼
[SPH-FVM Coupling (M3)]     [ALE Moving Mesh (M3)]
- div r < 0.75 d             - Boundary Node Motion
- A_iS boundary vector       - Laplacian Smoothing
- OPMIS Taylor P2M / M2P     - Degenerate Cell Audit
      │                                │
      └───────┬────────────────────────┘
              ▼
[FoamMesh C-ABI / Python Bridge / E2E Verification (M4)]
```

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---|---|---|---|
| 1 | Continuous SDF Engine (2D & 3D) | Evaluates distance phi(x) and normal gradient grad phi from analytical primitives (box, sphere, cylinder, NACA) | M1 | spec_miner_mesh_1 |
| 2 | SurfaceMesh to SDF Bridge | Queries phi(x) and grad phi from arbitrary triangulated SurfaceMesh via cKDTree and triangle projections | M1 | explorer_mesh_1 |
| 3 | CAD Auxiliary Wireframe & Sketch Line Filter | Discards 1D curves, GEOMETRIC_CURVE_SET, DRAUGHTING_MODEL, and non-face entities to isolate 2-manifold shells | M1 | explorer_mesh_2 |
| 4 | Curvature & Proximity Refinement | Computes local element size h(x) based on boundary curvature and proximity/narrow gap detection | M1 | spec_miner_mesh_1 |
| 5 | DistMesh Unstructured Mesher Kernel | Iterative spring force equilibrium on Delaunay / Bowyer-Watson triangulation with boundary projection | M2 | spec_miner_mesh_1 |
| 6 | Boundary Inflation & Buffer Layers | Extrudes structured orthogonal quads/prisms at walls with geometric growth ratio r for y+ ~ 1 resolution | M2 | spec_miner_mesh_1 |
| 7 | Reverse Cuthill-McKee (RCM) Reordering | BFS graph reordering reducing matrix bandwidth and cache misses | M2 | spec_miner_mesh_1 |
| 8 | DiFVM Graph-Topology & SoA Data Layout | Flat arrays: owner, neighbour, face_areas (S_f), face_centres, cell_centres, cell_volumes | M2 | explorer_mesh_1 |
| 9 | Non-Orthogonal Decomposition (delta_f, k_f) | Computes delta_f = (||S_f||^2 / (S_f . d)) d and k_f = S_f - delta_f satisfying delta_f + k_f == S_f | M2 | spec_miner_mesh_1 |
| 10 | Error-Map & Solver Stabilization Mask | Evaluates face non-orthogonality angle, skewness, aspect ratio; generates error_map mask | M2 | spec_miner_mesh_1 |
| 11 | FoamMesh & C-ABI Compatibility | Bit-for-bit parity with FoamMesh data structures and extern "C" ABI functions | M2 | explorer_mesh_1 |
| 12 | SPH Incomplete Support Detection | Evaluates div r = -sum V_j (r_i - r_j) . grad W_ij; detects truncated particles where div r < 0.75 d | M3 | spec_miner_mesh_1 |
| 13 | Virtual FVM Wall Boundary Vector (A_iS) | Computes A_iS = -sum V_j grad W_ij achieving 0th-order consistency A_iS + sum V_j grad W_ij = 0 (< 10^-10) | M3 | spec_miner_mesh_1 |
| 14 | OPMIS Partitioning & Taylor Interpolation | Core / Halo / Mesh partitioning and moment-reproducing Taylor P2M / M2P interpolation for linear fields | M3 | spec_miner_mesh_1 |
| 15 | Moving Mesh ALE & Laplacian Smoothing | Displaces boundary nodes and solves interior Laplacian smoothing preserving boundary layers | M3 | spec_miner_mesh_1 |
| 16 | Degenerate Cell Detection & Remeshing | Detects negative volumes (V_i <= 0) and high skewness under mesh motion to trigger remeshing | M3 | spec_miner_mesh_1 |
| 17 | Pure-Python/NumPy Fallback Engine | Complete transparent fallback backend (mesh_engine.py) requiring no native compilation | M2 | explorer_mesh_1 |
| 18 | Native C++20 Acceleration Kernels (MSVC/CMake) | Modern C++20 (/std:c++20) compiled DLL (cfd_mesher_native.dll) exposing C-ABI for zero-overhead execution | M2 | explorer_mesh_1 |
| 19 | Complex CAD Meshing (TestCAD & Blade) | Ingests, filters, meshes, and audits 200 mm cube and BladeFullRev1CFD turbine blade | M4 | explorer_mesh_2 |
| 20 | Comprehensive Verification Suite | Authored tests/test_mesher.py (>= 10 tests) covering all invariants + 277 repository regression tests | M4 | explorer_mesh_2 |
| 21 | Multi-Region Meshing & CHT Coupled Interfaces | Partition adjacent fluid/solid zones (cell_regions), identify coupledWall/mappedWall interfaces, conformal twin-faces and non-conformal AMI | M2 | user_directive |
| 22 | Rotational/Cyclic Periodic Symmetry | Support cyclic periodic face pairs for turbomachinery (BladeFullRev1CFD) with rotation matrix R_z(theta) and area vector/flux transformation | M2 | user_directive |
| 23 | Boundary Layer Pinching & Layer Termination | Detect sharp corners, trailing edges, and narrow gaps to smoothly taper and terminate prismatic inflation layers, preventing inverted cells | M2 | user_directive |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|---|---|---|---|
| M1 | Geometry, SDF Engine & CAD Feature Filtering | Features 1-4 (analytical primitives, mesh SDF, sketch/wireframe filtering, curvature/proximity) | none | DONE |
| M2 | In-Memory Mesher Core, Boundary Layers & Graph Topology | Features 5-11, 17, 18, 21-23 (DistMesh, Bowyer-Watson, inflation, pinching/termination, RCM, SoA DiFVM, native C++20 core, CHT, cyclic symmetry) | M1 | DONE |
| M3 | Multiphysics SPH-FVM Coupling & Dynamic Moving Mesh | Features 12-16 (SPH support detection, A_iS wall vector, OPMIS Taylor interpolation, ALE Laplacian smoothing, degenerate audit) | M2 | DONE |
| M4 | Final Milestone: E2E Validation, CAD Robustness & Hardening | Features 19-20 (100% pass on tests/test_mesher.py, 540 regression tests, BladeFullRev1CFD, adversarial hardening, victory audit) | M1, M2, M3, TEST_READY | DONE |

## Interface Contracts

### DiFVM Graph Mesh (`backend/meshing/mesh_engine.py` & `backend/meshing/models.py`)
```python
from dataclasses import dataclass, field
import numpy as np
from typing import Dict, List, Optional, Tuple, Any

@dataclass
class UnstructuredMesh:
    points: np.ndarray             # shape (N_p, 3), float64
    cells: List[np.ndarray]        # list of vertex index arrays per cell (or cell_nodes, cell_offsets)
    owner: np.ndarray              # shape (N_f,), int32/int64
    neighbour: np.ndarray          # shape (N_f,), int32/int64 (boundary faces: -1 or -patch_id - 1)
    face_areas: np.ndarray         # shape (N_f, 3), float64: S_f vector pointing owner -> neighbour
    face_centres: np.ndarray       # shape (N_f, 3), float64: c_f
    cell_centres: np.ndarray       # shape (N_c, 3), float64: C_i
    cell_volumes: np.ndarray       # shape (N_c,), float64: V_i > 0
    delta_f: np.ndarray            # shape (N_f, 3), float64: orthogonal projection vector
    k_f: np.ndarray                # shape (N_f, 3), float64: non-orthogonal correction, delta_f + k_f == S_f
    boundaries: Dict[str, Tuple[int, int]] # patch_name -> (start_face, num_faces)
    error_map: Dict[str, np.ndarray] = field(default_factory=dict) # non_ortho, skewness, aspect_ratio, mask
    metadata: Dict[str, Any] = field(default_factory=dict)
```

### Module Contracts
- `geometry_sdf.SDFEngine.evaluate(points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]`
  Returns `(phi, grad_phi)` where `phi` is signed distance (< 0 inside) and `grad_phi` is normalized gradient.
- `geometry_sdf.CADMeshSDF.from_surface_mesh(mesh: SurfaceMesh) -> CADMeshSDF`
  Computes continuous signed distance field and normal gradients from arbitrary 2-manifold CAD surface meshes.
- `mesh_engine.MeshEngine.generate_mesh(...) -> UnstructuredMesh`
  Generates 2D/3D body-fitted unstructured mesh with optional boundary inflation layers and RCM reordering.
- `coupling.SPHFVMCoupler.detect_boundary_particles(particles: np.ndarray, volumes: np.ndarray, h: float) -> np.ndarray`
  Returns boolean mask of truncated particles where `div r < 0.75 d`.
- `coupling.SPHFVMCoupler.compute_virtual_wall_vector(particles: np.ndarray, volumes: np.ndarray, h: float) -> np.ndarray`
  Computes `A_iS = -sum_j V_j grad W_ij` satisfying `||A_iS + sum_j V_j grad W_ij|| < 10^-10`.
- `coupling.OPMISTaylorInterpolator.interpolate_p2m(particles: np.ndarray, values: np.ndarray, mesh: UnstructuredMesh) -> np.ndarray`
  Moment-reproducing Taylor interpolation guaranteeing exact linear field reproduction $L_\infty < 10^{-11}$.
- `moving_mesh.ALEMeshMover.deform_mesh(mesh: UnstructuredMesh, boundary_displacement: np.ndarray) -> UnstructuredMesh`
  Solves interior Laplacian smoothing for displaced boundary nodes and validates positive cell volumes.

## Code Layout
```text
backend/meshing/
├── __init__.py                 # Package root
├── models.py                   # UnstructuredMesh, SPHParticleSet, MeshQualityReport
├── geometry_sdf.py             # Analytical primitives, SurfaceMesh SDF via cKDTree, feature filtering
├── distmesh_core.py            # DistMesh force equilibrium, Bowyer-Watson Delaunay, RCM
├── inflation.py                # Prismatic/quad wall boundary layer extrusion & buffer layers
├── difvm_topology.py           # SoA graph layout, owner/neighbour, face_areas, delta_f, k_f, error-map
├── coupling.py                 # Hybrid Eulerian SPH-FVM coupling, A_iS vector, OPMIS Taylor P2M/M2P
├── moving_mesh.py              # ALE mesh motion, Laplacian smoothing, degenerate cell detection
├── mesh_engine.py              # Unified high-level engine facade & pure Python fallback
└── native/                     # Compiled C++20 core (MSVC / CMake -> cfd_mesher_native.dll)
    ├── CMakeLists.txt
    ├── include/
    │   ├── cfd_mesher_native.h # C-ABI extern "C" headers
    │   ├── difvm_mesh.hpp      # Polyhedral mesh, SoA, RCM reordering
    │   ├── distmesh.hpp        # Fast Delaunay & force equilibrium
    │   ├── inflation.hpp       # Prismatic boundary layer extrusion
    │   ├── sdf_engine.hpp      # Fast BVH / spatial tree SDF queries
    │   └── coupling.hpp        # SPH neighbor search, A_iS, OPMIS Taylor solver
    └── src/
        ├── cfd_mesher_native.cpp
        ├── difvm_mesh.cpp
        ├── distmesh.cpp
        ├── inflation.cpp
        ├── sdf_engine.cpp
        └── coupling.cpp
```
