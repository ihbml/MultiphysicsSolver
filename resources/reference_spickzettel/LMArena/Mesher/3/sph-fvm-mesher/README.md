# HybridMesh — FVM / SPH meshing and coupling backend

A dependency-free **C++20 library and command-line tool** that prepares 2D/3D finite-volume cells, an adaptive SPH particle region, physical-boundary metadata, and particle–mesh transfer data. A small **browser frontend** lets you run the same executable and inspect its results. This is **geometry/discretization, not a CFD time-stepper**. It can load an existing unstructured mesh or create a rectangular test mesh; it does **not** mesh a CAD/STEP file by itself.

## What is implemented from the papers

| Reference | Implemented in this backend | Deliberate boundary of scope |
| --- | --- | --- |
| Wang, Haidn & Hu, *Incorporating the algorithm for the boundary condition from FVM into the framework of Eulerian SPH* (2023), [arXiv:2312.03542](https://arxiv.org/html/2312.03542v1) | Detect deficient support using their position-divergence criterion `div r < 0.75 × dimension` (Eq. 25); create a **virtual boundary area vector** `A_iS = −Σ_j V_j ∇ᵢW_ij` (Eq. 23) for *core particles near a physical mesh boundary*. Keep closest physical patch and wall distance. | No boundary-condition **state/flux**, Riemann solver, pressure-driven particle relaxation, or proof of interior zero-order consistency. Uses Wendland C2, not their truncated Laguerre–Gauss kernel. Do not apply the physical-wall closure to the artificial overlap halo. |
| Zhang, Ming, Wang & Liang, *An adaptive SPH-FVM method with optimized particle-mesh interpolation scheme for strongly compressible multiphase flows* (2025), [accepted version](https://www.repository.cam.ac.uk/items/6f187199-5a58-4b59-9a2d-e02d9f602578) | Common/transition/interpolation **particle** roles and common/interpolation/sleeping **mesh** roles (Fig. 1); a movable box-core threshold with a dead band; SPH → mesh corrected-gradient/Taylor averaging (their OPMIS Eqs. 2–4); mesh → halo field interpolation and transition blending (Eq. 6). | No shock/bubble solver, multiphase physics, conservative remapping on region changes, or exact reproduction of their Cartesian `2^dim` mesh → particle Lagrange weights (Eq. 5). The alternative here is affine-exact weighted least squares on *unstructured* cell centers. |
| Du, Li, Xu & Wang, *DiFVM: A Vectorized Graph-Based Finite Volume Solver for Differentiable CFD on Unstructured Meshes* (2026), [arXiv:2603.15920](https://arxiv.org/html/2603.15920v1) | Static cell/face geometry, owner–neighbor connectivity, CSR adjacency, boundary patches, oriented face-area vectors and signed face-flux scatter/gather. | No Navier–Stokes/PISO solver, OpenFOAM **case** configuration/fields, GPU kernels, autodifferentiation, or Windkessel conditions. This is graph-ready *data*, not DiFVM itself. |

**Terminology:** `closure_area` is the SPH virtual area/gradient contribution of Eq. 23 (dimensions of inverse length); it is **not** the physical FVM face area. Only `Face::area_vector` is a physical length (2D) or area (3D). A virtual closure at a corner has one effective normal and a nearest patch; imposing distinct corner boundary conditions requires a subsequent solver-specific treatment.

## Browser tester (frontend)

Requires a C++20 compiler, `make`, and Python 3.10+; **no npm, pip packages, database, or JavaScript framework**. Python's standard-library HTTP server is only an adapter that validates inputs and invokes the original C++ mesher.

```sh
make -j4
make test-web                      # HTTP integration tests against the real C++ CLI
make web                           # open http://localhost:8000
# To change port: PORT=8080 make web
```

The page runs a small 2D box example on load. Try 2D/3D generated boxes, included unstructured Gmsh/OpenFOAM examples, or upload an ASCII Gmsh MSH 2.2 file / the five ASCII OpenFOAM polyMesh files (`points`, `faces`, `owner`, `neighbour`, `boundary`). Select a box, disk/sphere, or whole-domain SPH core; adjust spacing, kernel support and mesh band; optionally supply a CSV of interface points for a one-step box adaptation. Results include entity counts, coupling diagnostics, a 2D map or 3D slice preview, individual CSV/VTK downloads, the complete output ZIP, and the unmodified CLI log. The canvas may sample large outputs **for display only**; exported files are complete.

Web-only safety limits are 30,000 generated box cells, ~150,000 estimated lattice sites, 2 MB per uploaded file, a 7 MB request, and a 60-second run. The C++ library/CLI remains independent of these frontend limits. Web runs are kept temporarily in `build/web-runs` (at most eight); download a ZIP to keep your results. This UI is intended as a local/test interface, **not** a multi-user public deployment or a CFD solver.

## Build and run

The C++ backend itself requires only a C++20 compiler and `make`. No runtime or third-party library dependencies.

```sh
make -j4
make test                 # >8,000 geometry, closure, format and affine-transfer checks
make bench                # library-only 2D and 3D timing; excludes output I/O
```

Optional OpenMP: `make clean && make -j4 OMP=1 && OMP_NUM_THREADS=4 make OMP=1 test`. Use `make clean` when switching compiler options, because the Makefile does not track changed flags. Alternatively, use CMake (`-DHYBRIDMESH_OPENMP=ON` to enable OpenMP); its test target works with CTest.

```sh
# Generated 2D quad mesh, SPH in a central box, plus transition/halo layers.
./build/hybridmesh --box2 80 80 1 1 --spacing 0.0125 \
  --core-box 0.25 0.25 0.75 0.75 --output run2d --no-links

# Read an unstructured 2D/3D ASCII Gmsh 2.2 mesh and place a core ball.
./build/hybridmesh --gmsh tests/fixtures/jittered_triangles.msh \
  --spacing 0.055 --core-ball 0.5 0.5 0.22 --output run_gmsh

# Read OpenFOAM's mesh geometry / patch table (not its flow fields).
./build/hybridmesh --openfoam tests/fixtures/foam --spacing 0.4 \
  --core-all --output run_foam --no-links

# 3D generated hex mesh. In 2D --core-ball is a disk.
./build/hybridmesh --box3 24 24 24 1 1 1 --spacing 0.0416666667 \
  --core-ball 0.5 0.5 0.5 0.23 --output run3d --no-links

# Optional one-step adaptive box threshold adjustment around a point cloud.
./build/hybridmesh --box2 50 50 1 1 --spacing 0.02 \
  --core-box 0.3 0.3 0.7 0.7 --adapt-points examples/interface2d.csv \
  --margin 0.1 --step 0.05 --output adapted --no-links
```

`--core-box` takes all *minimum coordinates*, then all *maximum coordinates*: 2D `xmin ymin xmax ymax`, 3D `xmin ymin zmin xmax ymax zmax`. `--support` changes the SPH cutoff/spacing ratio (default **2.6**); `--band` changes the interpolation-mesh band in particle spacings (default **2**). The two successive outer particle shells each have a width of one kernel cutoff. `--core-all` is useful for testing physical-boundary closure; it normally has no surrounding FVM common-cell region.

### Outputs

| File | Contents |
| --- | --- |
| `mesh_points.csv`, `cells.csv`, `faces.csv`, `patches.csv` | Cell volumes/areas, centroids, roles, node indices; outward-from-owner face vectors and boundary patch IDs. |
| `particles.csv` | Positions, cell ownership, role, quadrature volume, support divergence, physical closure vector/normal, nearest physical wall/patch. |
| `sph_neighbors.csv`, `fvm_graph.csv` | Directed particle CSR edges and one owner → neighbor row per internal FVM face. Omitted with `--no-links`. |
| `particle_to_mesh.csv`, `mesh_to_particle.csv` | SPH → interpolation-cell sample associations and FVM → halo particle **weighted** stencils. Omitted with `--no-links`. SPH → mesh Taylor gradients are applied at runtime, so these associations are *not* complete fixed scalar weights. |
| `particles.vtk`, `mesh.vtk` | ParaView-compatible particle cloud and ordinary first-order element geometry/roles. `mesh.vtk` is omitted for arbitrary OpenFOAM polyhedra (the CSV face loops are still exported). |
| `summary.txt` | Entity/role counts, fallback counters and singular-gradient diagnostics. |

All IDs are **zero-based**; CSV `-1` denotes a missing neighbor/patch. Patches are **labels**, not applied flow boundary conditions. The output directory is created automatically; existing generated files are overwritten or removed when inapplicable.

### Library API

The public header is [`include/hybridmesh/hybridmesh.hpp`](include/hybridmesh/hybridmesh.hpp). Link the static library at `build/libhybridmesh.a`:

```cpp
#include "hybridmesh/hybridmesh.hpp"
using namespace hybridmesh;

Mesh mesh = read_gmsh22("domain.msh");       // or read_openfoam(...)
Graph graph = make_graph(mesh);
Region core = Region::sphere({0.5, 0.5, 0}, 0.2, mesh.dimension);
ParticleSet particles = discretize(mesh, core, {.spacing = 0.01});
TransferPlan transfer = make_transfer_plan(mesh, particles);

// Supply one finite scalar per particle; receive mesh-size output with NaN
// outside interpolation cells. Repeat per scalar/vector component.
std::vector<double> particle_scalar(particles.particles.size(), 1.0);
auto interpolated_cells = particle_to_mesh_opmis(mesh, particles,
                                                  transfer, particle_scalar);

// The opposite transfer takes a scalar per mesh cell. Non-halo particle
// outputs are NaN. Common FVM cells used by the stencils must be finite.
std::vector<double> mesh_scalar(mesh.cells.size(), 1.0);
auto halo_scalar = mesh_to_halo_particles(mesh, particles, transfer, mesh_scalar);

// An external FV solver can form one signed flux per face and scatter it.
std::vector<double> face_flux(mesh.faces.size(), 0.0);
auto cell_residual = scatter_face_flux(mesh, face_flux);
```

Changing the interface points? Call `adapt_box_to_interface(core, points, mesh, margin, step)`, then **re-run** `discretize` and `make_transfer_plan`. This updates the discretization; it does **not** transport density, energy, or particle identities between old and new regions.

## Numerical/geometry notes

- Supports generated 2D quad / 3D hex boxes; **ASCII Gmsh MSH 2.2 first-order** triangles, quads, tets, hexes, prisms and pyramids (physical boundary labels); and **ASCII OpenFOAM `constant/polyMesh`** (face-based convex polyhedra, including nonstandard polygonal faces). Gmsh v4/binary, OpenFOAM binary/dictionaries/fields, nonplanar faces, concave cells and curved/high-order elements are intentionally rejected or not supported. 2D geometry lies in a plane of constant `z` and is computed in XY coordinates.
- A global lattice at `(i+1/2) × spacing` is clipped against the half-spaces of *active* cells. Tiny active cells without a lattice site get one **centroid fallback** sample. The particle volume is `cell_volume / lattice_sites_owned_by_cell`, or the full cell volume for a centroid fallback. For a partially covered cell, the active particle volumes sum to only the sampled fraction; **this is not conservative mesh-to-particle state remapping**.
- SPH neighbors use a spatial hash; physical boundary proximity uses a face BVH; interpolation neighbors use a KD-tree. The SPH kernel is **Wendland C2** with support `R = support_ratio × spacing`. CSR neighbor lists are deterministically sorted. Independent per-particle loops can be compiled with OpenMP.
- For SPH → mesh transfer, the corrected linear gradient is computed from the moment `Mᵢ = Σⱼ Vⱼ wᵢⱼ Δxᵢⱼ Δxᵢⱼᵀ` and `∇φᵢ = Mᵢ⁻¹ Σⱼ Vⱼ wᵢⱼ (φⱼ−φᵢ) Δxᵢⱼ`, with `w=(1−r/R)³`. Reconstruction in an interpolation cell is the **mean** of `φᵢ + ∇φᵢ·(x_cell−xᵢ)` over common particles in that cell. If a cell has no such sample, it uses nearby common particles. Singular moments fall back to a zero gradient and are reported in `summary.txt`; do not use such a case for accurate shock reconstruction.
- For mesh → halo transfer, inverse-distance weighted **affine moving least squares** uses up to 8 (2D) or 16 (3D) closest *common* mesh centroids. It reproduces constant and linear fields if the local moment is full rank; otherwise it switches to normalized inverse-distance weights and reports a fallback. High-gradient fields still need a limiter/shock-aware solver and validation.
- The SPH closure is assembled only for **common SPH particles** meeting the paper's divergence test and lying within one support radius of a **physical** face. Artificial SPH/FVM overlap support is **not** mistaken for a wall. Interior particles are not relaxed: their residual `Σⱼ Vⱼ ∇Wᵢⱼ` may be nonzero on irregular distributions. An attached solver must provide boundary states and examine patch/corner treatment.

**Performance example (this sandbox, single-threaded `-O3`, excluding file export):** `make bench` produced ~25,600 2D cells / 4,344 active particles in approximately 0.041 s mesh build + 0.014 s sample/neighbors + 0.013 s coupling plan; ~32,768 3D cells / 7,088 particles in 0.131 s + 0.085 s + 0.053 s respectively. These are illustrative measurements, not cross-machine guarantees; run the included benchmark on your target hardware. Tests also pass with address/undefined-behavior sanitizers and with OpenMP enabled.

## Project layout

```text
include/hybridmesh/hybridmesh.hpp  public C++ API
src/mesh.cpp                     convex cell/face geometry, graph, adaptive box
src/io.cpp                       mesh readers + CSV/VTK exports
src/sph.cpp                      clipping, SPH CSR, boundary BVH, closure
src/transfer.cpp                 geometric stencils and scalar transfers
src/cli.cpp                      C++ command-line frontend
web/index.html, style.css, app.js browser UI (plain HTML/CSS/JS)
web/server.py                   stdlib HTTP adapter to C++ CLI
web/test_web.py                 end-to-end HTTP integration tests
examples/bench.cpp              no-I/O performance benchmark
tests/                         formats, conservation, boundary and affine tests
```

If the eventual goal is the **full compressible multiphase solver**, the next components are field initialization/remapping at wake/sleep transitions, phase/interface tracking, conservative Riemann-SPH and shock-capturing FVM fluxes, stable time integration, solver-specific physical BC states, and benchmark comparisons with the papers. DiFVM-like **autodifferentiability** would additionally require a differentiable numeric backend rather than this minimal-dependency C++ geometry layer.
