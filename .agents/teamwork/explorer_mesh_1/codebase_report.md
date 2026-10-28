# Comprehensive Codebase Exploration Report: Meshing Backend Integration

**Author:** `explorer_mesh_1` (Teamwork Explorer)  
**Date:** 2026-10-28  
**Scope:** Architecture, data structures, integration pathways, and build toolchains for the In-Memory Multiphysics Meshing Backend in CFDSolver.  
**Target Module:** `G:\Datein\Projects\CFDSolver\backend\meshing`

---

## Executive Summary

This report presents the findings of a thorough investigation across the CFDSolver repository. The objective is to design the integration blueprint for the new in-memory multiphysics meshing backend (`backend/meshing/`), connecting it with the existing CAD/Geometry subsystem (`backend/geometry/`), the native high-speed OpenFOAM I/O subsystem (`backend/native_rust/foam_fast_io/`), and the hybrid FVM-SPH numerical formulation (DiFVM, Eulerian SPH-FVM, OPMIS).

### Core Findings Matrix

| Domain | Current Repository State | Integration & Target Blueprint |
| :--- | :--- | :--- |
| **`backend/meshing`** | Greenfield module; directory does not yet exist. Neither `backend/__init__.py` nor `backend/meshing/__init__.py` exists. | Create module directory structure, package `__init__.py`, pure Python/NumPy vectorized fallback (`mesh_engine.py`), and modular components. |
| **`backend/geometry`** | Fully implemented and certified (240 tests passing). Discretized `SurfaceMesh` (V, F, N, patches), `CADGeometryEngine`, `BoundingBox`, and `SurfaceAuditor`. | Interface with `SurfaceMesh` to construct continuous Signed Distance Fields (SDF) and normal gradients $\nabla \phi$; filter 2D/3D sketch elements in STEP/OBJ. |
| **`backend/native_rust` / `foam_fast_io`** | Rust `FoamMesh` struct in `mesh.rs`, C-ABI in `ffi.rs`, and Python/NumPy mirror in `foam_reader.py`. Flat SoA memory layout. | 100% data layout parity: DiFVM Graph-Topology in SoA produces exact arrays required by `FoamMesh` (`owner`, `neighbour`, `face_areas`, `face_centres`, `cell_centres`, `cell_volumes`, `delta_f`, `k_f`). |
| **Build & Toolchains** | Python 3.14.7 in `.venv` with `numpy 2.5.4`, `scipy 1.18.1`, `trimesh 5.1.1`, `gmsh 4.15.2`. MSVC C++20 (v19.40) & CMake 3.28 installed in VS 2022 Community. Rust toolchain not currently on host PATH. | Seamless dual-path architecture: Pure Python/NumPy engine (`mesh_engine.py`) provides 100% operational functionality out of the box; native Rust/C++ backends act as high-performance accelerators. |

---

## 1. Meshing Backend Status & Planned Structure

### 1.1 Current State of `backend/meshing`
- The directory `G:\Datein\Projects\CFDSolver\backend\meshing` does not exist in the repository.
- There are no existing meshing files or legacy dependencies in `backend/`.
- `backend/__init__.py` does not currently exist. Adding it alongside `backend/meshing/__init__.py` will establish explicit, robust packaging.

### 1.2 Target Directory Architecture
To support modular development, clear separation of concerns, and seamless testing, the meshing backend should be structured as follows:

```text
G:\Datein\Projects\CFDSolver\backend\meshing\
├── __init__.py                # Package facade exporting MeshEngine, MeshSpec, DiFVMTopology, etc.
├── mesh_engine.py             # Main entry point and orchestration engine (Python fallback + native dispatcher)
├── geometry_sdf.py            # Continuous SDF & normal gradient evaluator (analytical + CAD surface mesh)
├── distmesh_core.py           # Body-fitted Delaunay & force-equilibrium relaxation kernel
├── difvm_topology.py          # Vectorized Graph-Topology in Structure of Arrays (SoA)
├── inflation.py               # Boundary layer prism/quad inflation with geometric growth
├── quality_metrics.py         # Error-map: non-orthogonality, skewness, aspect ratio, Gauss closure
├── coupling.py                # Eulerian SPH-FVM & OPMIS interface (div r, boundary vector A_iS, P2M/M2P)
├── moving_mesh.py             # ALE dynamic mesh smoothing and boundary deformation adaptation
├── native_bridge.py           # ctypes / C-ABI bridge to compiled native C++ or Rust kernels
└── native/                    # Native C++20 / Rust sources and build configuration
    ├── CMakeLists.txt         # MSVC / Clang / GCC build definitions for C++20
    └── src/                   # C++20 meshing and coupling kernels
```

---

## 2. Geometry Subsystem Integration (`backend/geometry`)

### 2.1 Existing Models and Capabilities
The existing geometry module (`backend/geometry/`) provides a robust foundation:
1. **`SurfaceMesh` (`backend/geometry/models.py:66-122`)**:
   - `vertices`: `np.ndarray` of shape `(V, 3)`, `float64`.
   - `faces`: `np.ndarray` of shape `(F, 3)`, `int32` (indexed triangular facets).
   - `normals`: `np.ndarray` of shape `(F, 3)`, unit normal vectors computed via cross product:
     $$\mathbf{n}_f = \frac{(\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0)}{\|(\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0)\|}$$
   - `patches`: `Dict[str, np.ndarray]` mapping boundary patch names to face index slices.
   - `get_bounding_box()`: Computes an enclosing `BoundingBox`.
2. **`BoundingBox` (`backend/geometry/models.py:16-64`)**:
   - `min_pt`, `max_pt`: 3D Euclidean coordinates.
   - Properties: `span`, `center`, `diagonal`, `volume`.
   - Methods: `from_points(pts)`, `scale(factor)`.
3. **`CADGeometryEngine` (`backend/geometry/pipeline.py`)**:
   - Fluent pipeline: `.load(file_path)` $\to$ `.scale(factor)` $\to$ `.audit()` $\to$ `.export_openfoam()`.
   - Supports:
     - ISO STEP (`.stp`, `.step` AP203/214/242) via Gmsh OpenCASCADE or pure B-Rep entity traversal.
     - Stereolithography (`.stl`, binary and ASCII with spatial vertex welding).
     - Wavefront OBJ (`.obj` with polygon fan triangulation and group-to-patch mapping).
     - Siemens NX native part files (`.prt`) via `CADBridge` CLI wrapper.
4. **`SurfaceAuditor` (`backend/geometry/audit/surface_auditor.py`)**:
   - Manifold & watertightness verification ($|E_{\text{open}}| = 0, |E_{\text{multi}}| = 0$).
   - Euler-Poincaré invariant ($\chi = V - E + F = 2$).
   - Signed volume computation and automatic normal winding repair:
     $$V_{\text{signed}} = \frac{1}{6} \sum_{f} (\mathbf{v}_0 \times \mathbf{v}_1) \cdot \mathbf{v}_2$$
   - Equilateral quality metric $Q = \frac{4\sqrt{3} A}{a^2 + b^2 + c^2}$ and OpenFOAM `surfaceCheck` certification.

### 2.2 Querying Continuous SDF $\phi(\mathbf{x})$ and Normal Gradients $\nabla \phi$
The mesher requires continuous distance evaluations for DistMesh force equilibrium, wall-layer inflation, and SPH boundary detection:
1. **Analytical Primitives (2D & 3D)**:
   - Analytical closed-form level sets provide infinite precision, zero memory overhead, and machine-precision derivatives:
     - *Box / Cuboid*: $\phi_{\text{box}}(\mathbf{x}) = \max_k(|x_k - c_k| - b_k)$.
     - *Cylinder / Circle*: $\phi_{\text{cyl}}(\mathbf{x}) = \sqrt{(x - c_x)^2 + (y - c_y)^2} - R$.
     - *CSG Operations*:
       - Union: $\phi_{A \cup B} = \min(\phi_A, \phi_B)$
       - Intersection: $\phi_{A \cap B} = \max(\phi_A, \phi_B)$
       - Difference: $\phi_{A \setminus B} = \max(\phi_A, -\phi_B)$
   - Gradient computation:
     $$\nabla \phi(\mathbf{x}) = \left( \frac{\phi(\mathbf{x} + \epsilon \mathbf{e}_x) - \phi(\mathbf{x} - \epsilon \mathbf{e}_x)}{2\epsilon}, \frac{\phi(\mathbf{x} + \epsilon \mathbf{e}_y) - \phi(\mathbf{x} - \epsilon \mathbf{e}_y)}{2\epsilon}, \frac{\phi(\mathbf{x} + \epsilon \mathbf{e}_z) - \phi(\mathbf{x} - \epsilon \mathbf{e}_z)}{2\epsilon} \right)$$
2. **Complex CAD Surface Meshes (e.g. `BladeFullRev1CFD`)**:
   - For arbitrary triangulated boundary surfaces, distance queries evaluate:
     $$d(\mathbf{x}) = \min_{f \in \text{faces}} \text{dist}(\mathbf{x}, \Delta_f)$$
   - Implementation strategy:
     - Utilize `scipy.spatial.cKDTree` on facet centroids and vertices for fast candidate selection ($O(\log N)$).
     - Point-to-triangle orthogonal projection computes the exact closest surface point $\mathbf{p}^*$.
     - Sign determination: Determined via facet normal dot product $(\mathbf{x} - \mathbf{p}^*) \cdot \mathbf{n}_{f^*}$ or ray-intersection parity test (generalized winding number). In the fluid meshing convention: $\phi < 0$ inside fluid domain, $\phi = 0$ on walls, $\phi > 0$ outside/solid.
     - Gradient: The outward unit normal gradient is directly:
       $$\nabla \phi(\mathbf{x}) = \frac{\mathbf{x} - \mathbf{p}^*}{\|\mathbf{x} - \mathbf{p}^*\|}$$

### 2.3 Foreign Element Filtering (2D Sketches, Wireframe Lines, Annotations)
- **Problem Identified**: Inspection of `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.log` reveals that Siemens NX exports include non-solid entities:
  - `camera_model_d3` (8), `draughting_model` (9), `planar_box` (8), `line` (252), `b_spline_curve_with_knots` (201).
- **Filtering Mechanism**:
  - In `STEPParser`, Gmsh extraction must strictly query 2D topological entities (`dim == 2`) and discard `dim == 1` wireframes or unattached curves.
  - In `OBJParser`, lines (`l v1 v2`) or points (`p v1`) must be ignored so only 2D facets (`f ...`) are parsed into `SurfaceMesh`.
  - Prior to meshing, the mesh auditor filters unreferenced vertices and asserts 2-manifold topological integrity.

---

## 3. Native Rust Subsystem & FoamMesh Data Model Parity

### 3.1 FoamMesh Memory Layout (`backend/native_rust/foam_fast_io/src/mesh.rs`)
The `FoamMesh` struct in Rust and its corresponding Python representation in `backend/native_rust/foam_reader.py` use a flat Structure of Arrays (SoA) layout matching OpenFOAM polyMesh standards:

```rust
pub struct FoamMesh {
    // Topological arrays
    pub points: Vec<[f64; 3]>,        // Flattened 3D coordinates (3 * N_points floats)
    pub face_offsets: Vec<usize>,    // Face node CSR index (N_faces + 1)
    pub face_nodes: Vec<usize>,      // Concatenated node indices for all faces
    pub owner: Vec<usize>,           // Owner cell index for each face (N_faces)
    pub neighbour: Vec<usize>,       // Neighbour cell index for internal faces (N_internal)
    pub n_cells: usize,              // Total number of cells
    pub boundaries: Vec<FoamBoundaryPatch>, // List of boundary patches (start_face, n_faces)

    // Precomputed geometric quantities (SoA)
    pub face_centres: Vec<[f64; 3]>, // Face centroid vectors (3 * N_faces floats)
    pub face_areas: Vec<[f64; 3]>,   // S_f face area normal vectors (3 * N_faces floats)
    pub face_magnitudes: Vec<f64>,   // |S_f| scalar area values (N_faces floats)
    pub cell_centres: Vec<[f64; 3]>, // Cell centroid vectors (3 * N_cells floats)
    pub cell_volumes: Vec<f64>,      // Cell volume scalar values (N_cells floats)
    pub delta_f: Vec<[f64; 3]>,      // Orthogonal projection vector Delta_f (3 * N_faces floats)
    pub k_f: Vec<[f64; 3]>,          // Non-orthogonal correction vector k_f = S_f - Delta_f
}
```

### 3.2 C-ABI Specification (`backend/native_rust/foam_fast_io/src/ffi.rs`)
The native C-ABI exports raw pointers to contiguous memory buffers:

| Function Signature (`extern "C"`) | Return Type | Description |
| :--- | :--- | :--- |
| `foam_mesh_load(dir_path: *const c_char)` | `*mut FoamMesh` | Loads OpenFOAM polyMesh from directory |
| `foam_mesh_free(ptr: *mut FoamMesh)` | `void` | Frees allocated mesh instance |
| `foam_mesh_get_n_points(ptr: *const FoamMesh)` | `usize` | Point count $N_p$ |
| `foam_mesh_get_n_cells(ptr: *const FoamMesh)` | `usize` | Cell count $N_c$ |
| `foam_mesh_get_n_faces(ptr: *const FoamMesh)` | `usize` | Total face count $N_f$ |
| `foam_mesh_get_n_internal_faces(ptr: *const FoamMesh)` | `usize` | Internal face count $N_{f,\text{int}}$ |
| `foam_mesh_get_points_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_p \times 3)$ coordinates |
| `foam_mesh_get_owner_ptr(ptr: *const FoamMesh)` | `*const usize` | Contiguous array of $N_f$ owner indices |
| `foam_mesh_get_neighbour_ptr(ptr: *const FoamMesh)` | `*const usize` | Contiguous array of $N_{f,\text{int}}$ neighbour indices |
| `foam_mesh_get_cell_centres_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_c \times 3)$ cell centroids |
| `foam_mesh_get_cell_volumes_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $N_c$ cell volumes |
| `foam_mesh_get_face_areas_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_f \times 3)$ $\mathbf{S}_f$ vectors |
| `foam_mesh_get_face_centres_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_f \times 3)$ face centroids |
| `foam_mesh_get_delta_f_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_f \times 3)$ $\boldsymbol{\Delta}_f$ vectors |
| `foam_mesh_get_k_f_ptr(ptr: *const FoamMesh)` | `*const f64` | Contiguous array of $(N_f \times 3)$ $\mathbf{k}_f$ vectors |
| `foam_mesh_get_total_volume(ptr: *const FoamMesh)` | `f64` | Sum of all cell volumes $\sum V_C$ |

### 3.3 DiFVM Graph-Topology in SoA Parity
The mathematical formulation in DiFVM (Du et al., 2026) operates directly on the scatter-gather graph defined by flat arrays:
- **Topology Arrays**: `owner` and `neighbour` define the directed edge graph connecting cell $O$ to cell $N$.
- **Geometric Invariants**:
  - **Over-Relaxed Orthogonal Projection**:
    $$\boldsymbol{\Delta}_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}} \mathbf{d}, \quad \text{where } \mathbf{d} = \mathbf{x}_{C,N} - \mathbf{x}_{C,O}$$
  - **Over-Relaxed Non-Orthogonal Correction Vector**:
    $$\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f \quad \Longrightarrow \quad \boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$$
  - **Linear Interpolation Weight**:
    $$w_f = \frac{\|\mathbf{x}_{C,N} - \mathbf{x}_f\|}{\|\mathbf{x}_{C,N} - \mathbf{x}_f\| + \|\mathbf{x}_f - \mathbf{x}_{C,O}\|}$$
  - **Cell Closure (Gauss Divergence Theorem)**:
    $$\sum_{f \in \text{faces}(C)} \mathbf{S}_{f,C} = \mathbf{0} \quad \left(\max_C \left\| \sum \mathbf{S}_f \right\| < 10^{-11} \right)$$
- **Zero-Copy Bridge**:
  Because the DiFVM SoA arrays match the fields and data types of `FoamMesh` bit-for-bit, any mesh generated by `backend/meshing` can directly construct a `FoamMesh` instance (or be converted without copying), allowing immediate execution of OpenFOAM solvers, PISO/SIMPLE iterations, and C-ABI export.

---

## 4. Build Environments, Compilers, and Toolchain Verification

An exhaustive inventory of the host development environment was conducted:

### 4.1 Python Environment (`.venv`)
- **Executable**: `G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe`
- **Python Version**: `3.14.7` (64-bit Windows)
- **Key Installed Packages**:
  - `numpy 2.5.4`: Vectorized array processing and linear algebra.
  - `scipy 1.18.1`: Spatial data structures (`cKDTree`, Delaunay triangulation, sparse solvers).
  - `trimesh 5.1.1`: Surface ray casting, watertight checks, and geometric queries.
  - `gmsh 4.15.2`: OpenCASCADE B-Rep tessellation and CAD ingestion.
  - `pytest 9.1.1`: Test automation framework.
- **Regression Suite Verification**:
  - The repository's entire test suite was executed against the active environment:
    ```powershell
    & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/
    ```
  - **Result**: `240 passed in 25.19s` (100% pass rate).

### 4.2 C++ Toolchain
- **Visual Studio Installation**: Microsoft Visual Studio 2022 Community (`C:\Program Files\Microsoft Visual Studio\2022\Community`).
- **MSVC Compiler**: `cl.exe` Version `19.40.33811` for x64 (`MSVC 14.40.33807`).
  - Full C++20 language and standard library support (`/std:c++20`).
  - Supports OpenMP (`/openmp`), AVX2 vectorization (`/arch:AVX2`), and high-performance optimizations (`/O2`).
- **CMake**: Version `3.28.3-msvc11` installed at:
  `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

### 4.3 Rust Toolchain & Fallback Strategy
- **Rust Status**: `rustc` and `cargo` are currently not in the system user PATH.
- **Cargo Crate Present**: `backend/native_rust/foam_fast_io/Cargo.toml` is configured as a `cdylib` and `rlib` with dependencies on `memmap2`, `flate2`, `byteorder`, `rayon`, and optional `pyo3`/`numpy`.
- **Architectural Consequence (Critical Design Rule)**:
  - The meshing subsystem MUST feature a **100% pure Python/NumPy fallback engine** (`backend/meshing/mesh_engine.py`).
  - This ensures that:
    1. The entire test suite and all meshing capabilities function out of the box in the existing `.venv` without requiring external toolchain installations or pre-compiled binaries.
    2. Native C++20 and Rust kernels can be seamlessly compiled and loaded via `ctypes` or `pybind11` when available, acting as drop-in performance accelerations without changing the public API.

---

## 5. Architectural Recommendations for `backend/meshing`

Based on the synthesis of reference implementations in `resources/reference_spickzettel/LMArena/` (specifically `2/backend` in Rust and `1/native` in C++20), the following algorithmic components should be integrated into the meshing architecture:

### 5.1 DistMesh Force Equilibrium Core
1. **Lattice Seeding**: Equilateral triangular background node placement across the domain bounding box.
2. **Rejection Method**: Remove seeds where $\phi(\mathbf{x}) > -0.05 h_0$ or random rejection based on the local sizing function:
   $$r(\mathbf{x}) = \frac{1}{h(\mathbf{x})^2}, \quad P_{\text{keep}} = \frac{r(\mathbf{x})}{r_{\max}}$$
3. **Delaunay Retriangulation**: Periodic Delaunay triangulation (`scipy.spatial.Delaunay` in Python, Bowyer-Watson in C++/Rust).
4. **Spring Force Equilibrium**:
   - For every edge $(i, j)$ of length $L_{ij}$ and target length $L_{0,ij} = h(\frac{\mathbf{x}_i + \mathbf{x}_j}{2})$:
     $$\mathbf{F}_{ij} = \max(L_{0,ij} - L_{ij}, 0) \frac{\mathbf{x}_i - \mathbf{x}_j}{L_{ij}}$$
   - Move interior nodes: $\mathbf{x}_i \leftarrow \mathbf{x}_i + \Delta t \sum_j \mathbf{F}_{ij}$.
   - Project boundary nodes onto the zero level set $\phi = 0$:
     $$\mathbf{x}_i \leftarrow \mathbf{x}_i - \phi(\mathbf{x}_i) \frac{\nabla \phi(\mathbf{x}_i)}{\|\nabla \phi(\mathbf{x}_i)\|^2}$$
5. **RCM (Reverse Cuthill-McKee) Bandwidth Optimization**: Permute cell and node indices to minimize sparse matrix bandwidth and maximize CPU L1/L2 cache locality.

### 5.2 Inflation Layer Generator
- Detect physical wall boundaries where boundary rule `bc == "wall"`.
- Generate $N_{\text{layers}}$ prismatic (3D) or quad (2D) boundary layers extruded in the normal direction $-\nabla \phi / \|\nabla \phi\|$.
- Geometric progression of layer heights:
  $$h_1 = y^+ \text{ target}, \quad h_k = h_1 r^{k-1}, \quad \text{with growth factor } r \in [1.15, 1.30]$$
- Stitch the outer inflation boundary seamlessly into the interior unstructured Delaunay mesh.

### 5.3 Error-Map & Solver Stabilization
Compute cell- and face-localized quality metrics directly in SoA:
- **Non-Orthogonality Angle**:
  $$\theta_{\text{no}} = \arccos \left( \frac{\mathbf{S}_f \cdot \mathbf{d}}{\|\mathbf{S}_f\| \|\mathbf{d}\|} \right) \in [0^\circ, 90^\circ)$$
- **Skewness Vector & Metric**:
  $$\mathbf{m}_f = \mathbf{x}_{C,O} + \frac{(\mathbf{x}_f - \mathbf{x}_{C,O}) \cdot \mathbf{n}_f}{\mathbf{d} \cdot \mathbf{n}_f} \mathbf{d}, \quad \text{Skew} = \frac{\|\mathbf{x}_f - \mathbf{m}_f\|}{\|\mathbf{d}\|}$$
- **Aspect Ratio**: Maximum to minimum cell dimension ratio.
- **Flagging Mask**: Cells with $\theta_{\text{no}} > 70^\circ$ or $\text{Skew} > 2.0$ are flagged in `mesh.error_map` to activate deferred correction or under-relaxation in the PISO/SIMPLE linear solvers.

### 5.4 Hybrid Eulerian SPH-FVM Coupling (Wang et al. & Zhang et al.)
- Detektion of wall-adjacent particles with truncated kernel support:
  $$\text{div}\,\mathbf{r}_i = \sum_j V_j (\mathbf{r}_j - \mathbf{r}_i) \cdot \nabla_i W_{ij} < 0.75\,d$$
- Construction of virtual FVM boundary vector $\mathbf{A}_{iS}$ restoring exact zeroth-order consistency:
  $$\mathbf{A}_{iS} = -\sum_{j \in \text{fluid}} V_j \nabla_i W_{ij} \quad \Longrightarrow \quad \mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$$
- OPMIS Partitioning (Core SPH, Buffer/Halo overlap, FVM background) with moment-reproducing Taylor polynomial reconstruction.

---

## 6. Actionable Implementation Checklist

- [x] **Exploration & Toolchain Audit**: Completed and documented in this report.
- [ ] **Package Foundation**: Create `backend/__init__.py` and `backend/meshing/__init__.py`.
- [ ] **Python Meshing Engine**: Implement `backend/meshing/mesh_engine.py` with pure Python/NumPy DistMesh algorithm.
- [ ] **Continuous SDF Engine**: Implement `backend/meshing/geometry_sdf.py` connecting analytical geometries and `SurfaceMesh`.
- [ ] **DiFVM SoA Topology**: Implement `backend/meshing/difvm_topology.py` generating flat `owner`, `neighbour`, $\mathbf{S}_f$, $\boldsymbol{\Delta}_f$, $\mathbf{k}_f$.
- [ ] **Wall Inflation**: Implement `backend/meshing/inflation.py` for structured near-wall prism layers.
- [ ] **Error-Map Generation**: Implement `backend/meshing/quality_metrics.py` computing non-orthogonality, skewness, and closure residuals.
- [ ] **SPH-FVM Coupling**: Implement `backend/meshing/coupling.py` (div r, $\mathbf{A}_{iS}$, OPMIS interpolation).
- [ ] **Native C++ & Rust Bridges**: Implement `backend/meshing/native_bridge.py` and C++20 / Rust kernels for accelerated computing.
- [ ] **Comprehensive Test Suite**: Implement `tests/test_mesher.py` covering all geometric invariants, analytical benchmarks, and complex geometries (`BladeFullRev1CFD`).
