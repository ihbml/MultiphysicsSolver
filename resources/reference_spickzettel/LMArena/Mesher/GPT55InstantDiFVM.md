For your project, I recommend **C++20/23 for the DiFVM Mesher**, with Rust handling the existing backend integration.

Since your mesher already exists and the backend is running, I would **not rebuild anything**. Instead, I would optimize the existing meshing pipeline and introduce a high-performance graph representation inspired by the DiFVM paper.

One important distinction: **DiFVM is primarily a graph-based finite-volume solver architecture, not a mesh-generation algorithm.** For your mesher, the useful parts are its static graph construction, geometric preprocessing, and efficient memory layout.

### 1. Recommended architecture

```text
backend/
│
├── meshing/                    # Existing mesher (KEEP)
│   ├── ...
│   └── ...
│
├── native_cpp/
│   └── difvm_mesh/
│       │
│       ├── include/
│       │   ├── mesh_graph.h
│       │   ├── mesh_geometry.h
│       │   ├── mesh_quality.h
│       │   ├── mesh_error_map.h
│       │   └── mesh_export.h
│       │
│       ├── src/
│       │   ├── mesh_graph.cpp
│       │   ├── mesh_geometry.cpp
│       │   ├── mesh_quality.cpp
│       │   ├── mesh_error_map.cpp
│       │   └── mesh_export.cpp
│       │
│       ├── tests/
│       │   ├── graph_tests.cpp
│       │   ├── geometry_tests.cpp
│       │   └── performance_tests.cpp
│       │
│       └── CMakeLists.txt
│
└── native_rust/
    └── ...                     # Existing backend integration
```

The existing mesher remains responsible for creating tetrahedral, hexahedral, polyhedral, and hybrid meshes.

The new C++ component converts those meshes into the static graph representation required by DiFVM.

### 2. Example: High-performance DiFVM graph structure

The most important optimization is storing mesh connectivity and geometric properties in contiguous arrays.

Instead of creating millions of individual C++ objects, use Structure of Arrays (SoA).

`mesh_graph.h`

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace difvm {

struct MeshGraph {
  // Mesh dimensions
  std::size_t num_cells = 0;
  std::size_t num_internal_faces = 0;
  std::size_t num_boundary_faces = 0;

  // Graph connectivity
  std::vector<std::int32_t> owner;
  std::vector<std::int32_t> neighbour;

  // Cell geometry
  std::vector<double> cell_x;
  std::vector<double> cell_y;
  std::vector<double> cell_z;
  std::vector<double> cell_volume;

  // Face geometry
  std::vector<double> face_x;
  std::vector<double> face_y;
  std::vector<double> face_z;

  // Oriented face area vectors
  std::vector<double> sf_x;
  std::vector<double> sf_y;
  std::vector<double> sf_z;

  // Precomputed orthogonal diffusion coefficient
  std::vector<double> diffusion_weight;

  // Non-orthogonal correction vectors
  std::vector<double> correction_x;
  std::vector<double> correction_y;
  std::vector<double> correction_z;
};

}  // namespace difvm
```

This is a simplified example. Production arrays should use a custom 64-byte-aligned allocator or aligned memory pool.

The graph can be constructed once and reused by the solver without rebuilding connectivity.

### 3. Example: Precomputing DiFVM face geometry

Following Section 2.3.2 of the paper, the mesher should calculate the orthogonal diffusion contribution and non-orthogonal correction.

`mesh_geometry.cpp`

```cpp
#include "mesh_graph.h"

#include <cmath>
#include <stdexcept>

namespace difvm {

void ComputeFaceGeometry(MeshGraph& mesh) {

  constexpr double kEpsilon = 1e-14;

  const std::size_t n = mesh.num_internal_faces;

  mesh.diffusion_weight.resize(n);

  mesh.correction_x.resize(n);
  mesh.correction_y.resize(n);
  mesh.correction_z.resize(n);

  #pragma omp parallel for schedule(static)
  for (std::int64_t f = 0;
       f < static_cast<std::int64_t>(n);
       ++f) {

    const auto i = mesh.owner[f];
    const auto j = mesh.neighbour[f];

    // Cell-center displacement
    const double dx =
        mesh.cell_x[j] - mesh.cell_x[i];

    const double dy =
        mesh.cell_y[j] - mesh.cell_y[i];

    const double dz =
        mesh.cell_z[j] - mesh.cell_z[i];

    const double distance =
        std::sqrt(dx * dx + dy * dy + dz * dz);

    // Face area vector
    const double sx = mesh.sf_x[f];
    const double sy = mesh.sf_y[f];
    const double sz = mesh.sf_z[f];

    const double area =
        std::sqrt(sx * sx + sy * sy + sz * sz);

    if (distance <= kEpsilon ||
        area <= kEpsilon) {
      // Invalid geometry must be rejected during
      // mesh validation, before parallel preprocessing.
      mesh.diffusion_weight[f] = 0.0;
      mesh.correction_x[f] = 0.0;
      mesh.correction_y[f] = 0.0;
      mesh.correction_z[f] = 0.0;
      continue;
    }

    // Unit vector between cell centers
    const double ex = dx / distance;
    const double ey = dy / distance;
    const double ez = dz / distance;

    // Projection of face area onto cell connection
    const double projection =
        sx * ex + sy * ey + sz * ez;

    // Requires consistently oriented internal faces.
    // Near-zero projections indicate invalid or
    // excessively non-orthogonal geometry.
    if (projection <= kEpsilon * area) {
      mesh.diffusion_weight[f] = 0.0;
      mesh.correction_x[f] = 0.0;
      mesh.correction_y[f] = 0.0;
      mesh.correction_z[f] = 0.0;
      continue;
    }

    // Over-relaxed orthogonal contribution
    const double delta_magnitude =
        area * area / projection;

    // Coefficient multiplying (phi_j - phi_i)
    mesh.diffusion_weight[f] =
        delta_magnitude / distance;

    // Non-orthogonal correction vector
    mesh.correction_x[f] =
        sx - delta_magnitude * ex;

    mesh.correction_y[f] =
        sy - delta_magnitude * ey;

    mesh.correction_z[f] =
        sz - delta_magnitude * ez;
  }
}

}  // namespace difvm
```

This implementation demonstrates the preprocessing pattern. A production implementation should validate every face before entering the parallel kernel and report invalid geometry rather than silently accepting zero coefficients.

**Performance benefit:** The solver no longer needs to repeatedly calculate face distances, normal projections, or correction vectors.

These values are calculated once when the mesh is generated or imported.

### 4. MeshErrorMap

I would also implement your proposed `MeshErrorMap`.

It can store:

- Face non-orthogonality angles.
- Face skewness vectors.
- Cell-volume ratios.
- Cut-cell quality indicators.
- Face-specific higher-order blending factors.
- Cell-specific numerical stabilization recommendations.

However, these should remain separate from the actual mesh geometry.

For example:

```cpp
struct MeshErrorMap {

  // Face-level properties
  std::vector<double> nonorthogonality;
  std::vector<double> skewness;

  std::vector<double> skew_x;
  std::vector<double> skew_y;
  std::vector<double> skew_z;

  // Higher-order reconstruction blending
  std::vector<double> beta;

  // Cell-level properties
  std::vector<double> omega;

  // Diagnostics
  std::vector<std::uint8_t> quality_flags;
};
```

The mesher calculates these quantities once.

The solver subsequently accesses them directly through contiguous arrays.

Importantly, numerical damping and stabilization should remain configurable and must be validated against conservation and convergence tests. Mesh quality indicators alone cannot guarantee solver stability.

---

## 5. Example prompt for your coding agent

Since your backend and mesher already exist, I would give your coding agent the following focused implementation prompt.

```text
TASK:
Optimize and extend the EXISTING DiFVM mesher backend.

IMPORTANT:
The mesher already exists.
The backend instance is already running.

DO NOT:
- Rebuild the mesher from scratch.
- Replace existing backend infrastructure.
- Create duplicate meshing services.
- Modify unrelated solver components.
- Implement the full CFD solver.
- Implement PISO, Rhie-Chow, or physics modules.
- Introduce unnecessary dependencies.

PRIMARY OBJECTIVE:
Transform the existing mesher into a high-performance
DiFVM-compatible graph mesh preprocessing pipeline.

REFERENCE:
resources/papers/Meshing_FVM_DiFVM_2603.15920v1.pdf

Focus specifically on:
- Section 2.3.1: Mesh data structure.
- Section 2.3.2: Geometric preprocessing.
- Section 2.4.1: Static graph construction.

LANGUAGE:
Use modern C++20/23 for computationally intensive
mesh processing.

Follow the Google C++ Style Guide.

Use the existing Rust/Python backend interfaces
where appropriate.

ARCHITECTURE:

1. EXISTING MESH INTEGRATION

First inspect the current repository.

Identify:
- Existing mesher implementation.
- Existing mesh data structures.
- Existing geometry processing.
- Existing backend API.
- Existing mesh export formats.
- Existing memory management.
- Existing build system.

Reuse existing implementations wherever possible.

Do not duplicate functionality.

2. STATIC GRAPH CONSTRUCTION

Convert existing meshes into a DiFVM-compatible
cell-centered graph representation.

Support:
- Tetrahedral cells.
- Hexahedral cells.
- Prismatic cells.
- Pyramidal cells.
- General polyhedral cells.
- Hybrid unstructured meshes.

Construct:

owner[]
neighbour[]

cell_centroid_x[]
cell_centroid_y[]
cell_centroid_z[]

cell_volume[]

face_centroid_x[]
face_centroid_y[]
face_centroid_z[]

face_area_vector_x[]
face_area_vector_y[]
face_area_vector_z[]

All internal faces must have exactly one owner
and one neighbour.

Boundary faces must preserve their owning cell
and boundary patch association.

Validate all connectivity indices.

3. HIGH-PERFORMANCE MEMORY LAYOUT

Use Structure of Arrays (SoA).

Requirements:
- Contiguous memory.
- 64-byte aligned allocations.
- Minimal heap allocations.
- Preallocated buffers.
- No per-cell dynamic allocations in hot loops.
- No virtual function calls in hot loops.
- No unnecessary data copies.
- CPU-native FP64 geometry processing.

Use 32-bit connectivity indices when safe.

Support 64-bit indices for meshes exceeding
32-bit indexing limits.

Avoid AoS-to-SoA conversions during repeated
solver evaluations.

4. GEOMETRIC PREPROCESSING

Precompute:

- Cell volumes.
- Cell centroids.
- Face centroids.
- Oriented face area vectors.
- Cell-center distances.
- Face interpolation weights.
- Orthogonal diffusion coefficients.
- Non-orthogonal correction vectors.
- Face skewness displacement vectors.

Implement the over-relaxed non-orthogonal
correction described in DiFVM Section 2.3.2.

Ensure correct face orientation.

Reject:
- Negative or zero cell volumes.
- Degenerate faces.
- Invalid connectivity.
- Non-manifold internal faces.
- Numerically singular geometric configurations.

5. MESH ERROR MAP

Implement a static MeshErrorMap.

Store:

Face-level:
- Non-orthogonality.
- Skewness.
- Cell-volume ratios.
- Quality flags.
- Optional reconstruction blending beta.

Cell-level:
- Volume quality.
- Cut-cell quality.
- Optional stabilization factor omega.

Keep raw geometric measurements separate
from configurable numerical stabilization.

Do not silently modify physical geometry
to improve numerical stability.

6. PARALLELIZATION

Use C++ parallel processing where beneficial.

Prefer:
- OpenMP parallel for.
- SIMD-compatible loops.
- Cache-friendly iteration.
- Preallocated thread-local buffers.
- Deterministic parallel algorithms.

Avoid:
- Global mutexes in hot loops.
- Unnecessary atomics.
- Repeated memory allocation.
- False sharing.

Benchmark serial and parallel implementations.

Do not assume parallel execution is always faster
for small meshes.

7. GRAPH ACCESS OPTIMIZATION

Construct optional CSR connectivity:

cell_face_offsets[]
cell_face_indices[]

Preserve face orientation information.

Support efficient:

- Cell-to-face traversal.
- Face-to-cell traversal.
- Owner/neighbour access.
- Boundary patch traversal.

Investigate optional Morton ordering.

Only enable reordering when benchmarks
demonstrate a measurable improvement.

Preserve original cell and face mappings
for visualization and external interfaces.

8. BACKEND INTEGRATION

Integrate with the EXISTING backend.

Do not introduce a second backend server.

Expose graph mesh data through a stable
C-compatible interface where necessary.

Avoid copying complete mesh arrays between
C++, Rust, and Python.

Use zero-copy views when ownership,
alignment, and lifetime guarantees permit.

Clearly document memory ownership.

9. VALIDATION

Implement automated tests for:

- Tetrahedral meshes.
- Hexahedral meshes.
- Hybrid meshes.
- Boundary connectivity.
- Face orientation.
- Cell volumes.
- Face area vectors.
- Non-orthogonal corrections.
- MeshErrorMap.
- CSR connectivity.
- Invalid geometry handling.

Verify geometric conservation identities.

For every closed cell:

sum(face_area_vectors) approximately equals zero.

Verify:

sum(cell_volumes) approximately equals
the expected domain volume.

Use scale-aware numerical tolerances.

10. PERFORMANCE BENCHMARKS

Benchmark:

10,000 cells
100,000 cells
1,000,000 cells
10,000,000 cells

Only run sizes supported by available RAM.

Measure:

- Graph construction time.
- Geometry preprocessing time.
- MeshErrorMap construction time.
- Peak memory consumption.
- Allocation count.
- Sequential throughput.
- Parallel throughput.
- Memory bandwidth utilization.

Compare against the current implementation.

Do not claim performance improvements
without measured results.

11. IMPLEMENTATION STRATEGY

Work incrementally.

Step 1:
Inspect the existing repository.

Step 2:
Document existing mesh architecture.

Step 3:
Identify actual bottlenecks.

Step 4:
Implement one optimization at a time.

Step 5:
Compile and test each modification.

Step 6:
Run correctness and performance benchmarks.

Step 7:
Integrate improvements into the existing backend.

Step 8:
Document modifications and benchmark results.

DELIVERABLE:

A production-oriented, CPU-optimized,
DiFVM-compatible mesh graph preprocessing system.

The existing mesher and backend must remain
functional throughout development.

Do not implement unrelated CFD features.

Prioritize:

1. Correctness.
2. Compatibility.
3. Memory efficiency.
4. Parallel performance.
5. Maintainability.
```

### 6. C++ vs. Rust vs. Fortran

For this particular task, my recommendation is:

| Component | Language | Reason |
|---|---|---|
| Mesh topology | C++20 | Efficient graph construction |
| Geometry calculations | C++20 | SIMD and parallel loops |
| Mesh quality analysis | C++20 | Efficient array processing |
| Static graph generation | C++20 | Direct memory management |
| Backend integration | Existing Rust | Safe ownership and API integration |
| File import/export | Existing implementation | Avoid duplicate functionality |
| Numerical solver | C++ / Fortran | Separate concern |

**Why not Fortran for the mesher?**

Fortran is excellent for dense numerical kernels, but unstructured mesh construction involves irregular topology, indexing, connectivity, and memory management. C++ is generally more convenient for these operations.

**Why not Rust for everything?**

Rust can achieve comparable performance to C++. There is no inherent reason C++ must be faster.

However, C++20 offers a mature ecosystem for computational geometry, SIMD, OpenMP, and HPC mesh processing.

Since your architecture already separates Rust backend services from C++ numerical components, C++ is a natural fit.

### 7. One important improvement over the original plan

I would separate mesh generation from DiFVM preprocessing:

```text
                    EXISTING MESHER
                          |
                          v
                 Unstructured Mesh
                          |
                          v
               Mesh Geometry Validation
                          |
                          v
                DiFVM Graph Builder
                          |
              +-----------+-----------+
              |                       |
              v                       v
       Static Graph SoA          MeshErrorMap
              |                       |
              +-----------+-----------+
                          |
                          v
                 Immutable MeshGraph
                          |
              +-----------+-----------+
              |                       |
              v                       v
         CFD Solver              Visualization
```

This separation is especially important for your optimization workflows.

For SIMP and RAMP topology optimization, the geometry and graph connectivity can remain unchanged across thousands of iterations. Only the design variables and physical fields need updating.

That means the expensive meshing and graph preprocessing stages can be performed once, while the solver reuses the resulting arrays.

**Final recommendation:** Keep your existing mesher, implement the DiFVM graph and geometric preprocessing layer in C++20, and integrate it into your running backend. Start with correct SoA/CSR construction and geometric validation, then optimize based on profiling. This provides a strong foundation for a high-performance, differentiable FVM solver without unnecessarily rebuilding your current infrastructure.