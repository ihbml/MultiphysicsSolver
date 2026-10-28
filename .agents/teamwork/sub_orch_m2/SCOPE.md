# Scope: Milestone M2 — In-Memory Mesher Core, Boundary Layers & Graph Topology

## Mission
Deliver the high-performance In-Memory Unstructured Meshing Core in compiled Modern C++20 (MSVC / CMake) and Python bridge (`backend/meshing`), covering DistMesh spring force equilibrium, Bowyer-Watson Delaunay triangulation, prismatic boundary layer inflation for $y^+ \sim 1$, layer pinching/termination, RCM bandwidth reduction, DiFVM Structure of Arrays (SoA) graph topology with error-map stabilization mask, Conjugate Heat Transfer (CHT) multi-region coupled interfaces, cyclic periodic symmetry, and declarative frontend configuration.

## Features Owned
- **Feature 5:** DistMesh Unstructured Mesher Kernel (iterative force equilibrium on Delaunay / Bowyer-Watson triangulation in 2D and 3D with boundary node projection).
- **Feature 6:** Boundary Inflation & Buffer Layers (prismatic/quad orthogonal extrusion at walls with geometric growth ratio $r = 1.1 - 1.3$ for $y^+ \sim 1$ resolution, smooth buffer transition to interior).
- **Feature 7:** Reverse Cuthill-McKee (RCM) Reordering (graph BFS reordering reducing matrix bandwidth and cache misses).
- **Feature 8:** DiFVM Graph-Topology & SoA Data Layout (`owner`, `neighbour`, `face_areas`, `face_centres`, `cell_centres`, `cell_volumes`).
- **Feature 9:** Non-Orthogonal Decomposition ($\boldsymbol{\Delta}_f, \mathbf{k}_f$) satisfying exact algebraic identity $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$.
- **Feature 10:** Error-Map & Solver Stabilization Mask (`mesh.error_map`: non-orthogonality angle, skewness, aspect ratio, solver stabilization mask).
- **Feature 11:** FoamMesh & C-ABI Compatibility (bit-for-bit parity with `backend/native_rust/foam_fast_io` and OpenFOAM format).
- **Feature 17:** Pure-Python/NumPy Transparent Fallback (`mesh_engine.py`).
- **Feature 18:** Native C++20 Core (`backend/meshing/native/` compiled with MSVC `cl.exe /std:c++20` and CMake into `cfd_mesher_native.dll` exposing `extern "C"` ABI).
- **Feature 21:** Multi-Region Meshing & CHT Coupled Interfaces (`cell_regions`, `coupledWall` / `mappedWall` interfaces, conformal twin-faces, non-conformal AMI area-weighted flux transfer).
- **Feature 22:** Rotational / Cyclic Periodic Symmetry (cyclic boundary face pairs for turbomachinery with rotation matrix $\mathbf{R}_z(\theta)$ and area vector/flux transformation).
- **Feature 23:** Boundary Layer Pinching & Layer Termination (tapering and terminating layer growth in narrow gaps and sharp trailing edges to prevent inverted cells $V_c \le 0$).

## User Mandates & Constraints
1. **0.1% Strict Performance Rule:** All core compute operations (DistMesh force equilibrium, Delaunay triangulation, inflation extrusion, RCM reordering) MUST execute in compiled native C++20 (`cfd_mesher_native.dll`). Python is reserved for CLI routing, configuration parsing, test orchestration, and C-ABI/ctypes bindings.
2. **Declarative Configuration Schema:** Implement `MeshingConfig`, `InflationConfig`, `CurvatureSizingConfig`, `QualityConfig` dataclasses with `.to_dict()` and `.from_dict()`, and matching C++20 `MeshingConfigC` C-ABI struct.
3. **Permanent Documentation Traceability:** Consolidate and mirror all mathematical derivations, specifications, and architecture records into `backend/meshing/docs/`.
4. **Physical Invariants:**
   - Strict positive cell volumes $V_i > 0$ for all cells.
   - Exact closed-cell Gauss divergence sum $\max_i \|\sum \mathbf{S}_f\| < 10^{-11}$.
   - Over-relaxed orthogonality identity $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ across all internal faces.
   - 100% test pass on existing and new suites with zero regressions.

## Code Layout for Milestone M2
```text
backend/meshing/
├── __init__.py                 # Exports all models, configs, engines
├── models.py                   # UnstructuredMesh, MeshingConfig, InflationConfig, QualityConfig
├── distmesh_core.py            # DistMesh Python bridge & fallback
├── inflation.py                # Boundary inflation, pinching & layer termination
├── difvm_topology.py           # SoA graph layout, owner/neighbour, S_f, delta_f, k_f, error-map, CHT, cyclic
├── mesh_engine.py              # Unified engine facade (loads native C++20 DLL via ctypes)
├── native/                     # Compiled C++20 core
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── cfd_mesher_native.h # C-ABI extern "C" headers
│   │   ├── difvm_mesh.hpp
│   │   ├── distmesh.hpp
│   │   └── inflation.hpp
│   └── src/
│       ├── cfd_mesher_native.cpp
│       ├── difvm_mesh.cpp
│       ├── distmesh.cpp
│       └── inflation.cpp
└── docs/                       # Mirrored documentation and mathematical specifications
    ├── SPECIFICATION.md
    ├── DIFVM_TOPOLOGY.md
    ├── DISTMESH_ALGORITHM.md
    └── CHT_COUPLED_INTERFACES.md
```
