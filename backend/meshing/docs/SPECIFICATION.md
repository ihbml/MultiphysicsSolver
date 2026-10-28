# Specification Report: In-Memory Multiphysics Mesher Backbone

**Target Module**: `backend/meshing` (Rust & Modern C++ Core with Python/NumPy Bridge)  
**Author**: `spec_miner_mesh_1`  
**Date**: 2026-10-28  
**Authoritative Sources**:
1. `ORIGINAL_REQUEST.md` (specifically follow-up prompt `2026-10-28T00:22:07Z`)
2. Reference implementations in `resources/reference_spickzettel/LMArena/`:
   - `1/native/src/` (`core.hpp`, `mesh.cpp`, `sph.cpp`, `coupling.cpp`)
   - `2/backend/src/` (`mesh.rs`, `sph.rs`, `couple.rs`, `geometry.rs`, `lib.rs`)
   - `3/sph-fvm-mesher/` (`hybridmesh.hpp`, `transfer.cpp`, `sph.cpp`, `mesh.cpp`)
   - `4/native/src/` (`engine.hpp`, `mesh.cpp`, `solver.cpp`)
3. Existing repository data models:
   - `backend/native_rust/foam_fast_io/src/mesh.rs` & `ffi.rs`
   - `backend/native_rust/foam_reader.py`
   - `backend/geometry/models.py` & `pipeline.py`
   - `resources/papers/literature_survey_2026.md` (DiFVM, Wang et al., Zhang et al., Persson & Strang)

---

## Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | DiFVM SoA | Polyhedral Graph Topology | Collocated cell-centred FVM graph stored in flat contiguous arrays (Structure of Arrays) | Points, face node connectivity, owner list, boundary patches | `owner`, `neighbour`, `face_centres`, `face_areas`, `cell_centres`, `cell_volumes` | Reject empty or unclosed polygons/polyhedra; assert contiguous index partition | `2/backend/src/mesh.rs`, `foam_fast_io/src/mesh.rs`, DiFVM (2026) |
| 2 | DiFVM SoA | Non-Orthogonal Decomposition ($\boldsymbol{\Delta}_f, \mathbf{k}_f$) | Vectorized over-relaxed decomposition of face normal area vector $\mathbf{S}_f$ into orthogonal component along cell-centre vector $\mathbf{d}$ and non-orthogonal correction $\mathbf{k}_f$ | $\mathbf{S}_f$, cell centres $\mathbf{x}_O, \mathbf{x}_N$, $|\mathbf{S}_f|$ | $\boldsymbol{\Delta}_f, \mathbf{k}_f$ satisfying $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ | If $\mathbf{S}_f \cdot \mathbf{d} \le 10^{-15}$, fallback to $\boldsymbol{\Delta}_f = \mathbf{S}_f, \mathbf{k}_f = \mathbf{0}$ | `foam_fast_io/src/mesh.rs:200-239`, `1/native/src/mesh.cpp:906-914` |
| 3 | DiFVM SoA | Quality Metrics & Error-Map | Precomputes non-orthogonality angle $\theta_f$, skewness distance $\mathbf{e}_s$, and cell aspect ratio $\text{AR}_c$ stored directly in graph | Face and cell geometry | `non_ortho` (deg), `skew` (dimensionless), `aspect_ratio`, `mask` | Clamp dot-products to $[-1, 1]$; flag high non-orthogonality ($> 70^\circ$) and skewness ($> 0.85$) | `2/backend/src/mesh.rs:412-436`, `1/native/src/mesh.cpp:1000-1040` |
| 4 | DiFVM SoA | Solver Stabilization Mask | Multi-tier integer mask $(0, 1, 2, 3)$ on faces for dynamic under-relaxation and flux limiting in PISO/SIMPLE | Face quality metrics | Cell and face stabilization flags in `mesh.error_map` | High-skew/high-nonortho faces clamp explicit deferred corrections to avoid solver blowup | DiFVM §4, `1/native/src/mesh.cpp:1059-1064` |
| 5 | DistMesh | Force Equilibrium Relaxation | Node redistribution via spring/truss force equilibrium driven by signed distance field (SDF) and target length function | Initial seed points, continuous SDF $\phi(\mathbf{x})$, sizing field $h(\mathbf{x})$ | Relaxed node coordinates $\mathbf{x}_i$, edge connectivity | Limit maximum displacement per step ($\le 0.15 h_0$); project boundary nodes to $\phi = 0$ | Persson & Strang (2004), `2/backend/src/mesh.rs:172-254` |
| 6 | DistMesh | SDF Boundary Projection | Projects points that cross boundary back onto the zero level set using continuous normalized SDF gradient | Node position $\mathbf{x}$, SDF $\phi$, gradient $\nabla \phi$ | Projected position $\mathbf{x} - \phi \frac{\nabla \phi}{\|\nabla \phi\|^2}$ | Guard against $\|\nabla \phi\| < 10^{-12}$; retain position if singular | `2/backend/src/mesh.rs:245-251`, `1/native/src/core.hpp:107-113` |
| 7 | Bowyer-Watson | Incremental Delaunay Kernel | Incremental triangulation inserting points into a super-simplex and retriangulating the circum-containment cavity | Node set, bounding box | Triangle/tetrahedron cell list and adjacency pointers | Robust orientation test `orient2d` / `orient3d`; fallback brute-force cavity search | `1/native/src/mesh.cpp:40-296` |
| 8 | Meshing | Boundary Layer Inflation | Generates structured prismatic (3D) or quadrilateral (2D) boundary layers at physical wall patches with geometric growth | Wall contour/surface nodes, first layer height $h_1$, expansion ratio $r$, layer count $N$ | Boundary layer nodes and quad/prism cells | Restrict extrusion if node exits domain ($\phi > 0$); smooth normal vectors at corners | `1/native/src/mesh.cpp:485-517` |
| 9 | Optimization | Reverse Cuthill-McKee (RCM) | Bandwidth reduction renumbering on the cell adjacency graph to optimize CPU cache locality | Cell count $N_c$, `owner`, `neighbour` | Permutation vector `perm`, inverse mapping, bandwidths $\beta_0, \beta_1$ | Disconnected graph components handled via multi-start BFS | `2/backend/src/mesh.rs:548-615` |
| 10 | SPH-FVM | Position Divergence Boundary Detector | Evaluates $\text{div}\,\mathbf{r} = -\sum_j V_j r_{ij} \frac{\partial W}{\partial r}$ to identify particles with truncated kernel support near physical walls | Particle positions, volumes, kernel $W$ | `div_pos`, boolean boundary flag `is_bnd = (div_pos < 0.75 * d)` | Particles with $\text{div}\,\mathbf{r} < 0.75 d$ tagged as boundary particles needing wall closure | Wang et al. (2025), `2/backend/src/sph.rs:380-405`, `1/native/src/sph.cpp:430-465` |
| 11 | SPH-FVM | Virtual Wall Boundary Vector ($\mathbf{A}_{iS}$) | Constructs virtual boundary area vector $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ guaranteeing 0th-order consistency on physical walls | Particle positions, neighbor volumes, kernel gradient | Vector $\mathbf{A}_{iS}$, magnitude $|\mathbf{A}_{iS}|$, unit normal $\mathbf{n}_{iS}$ | Invariant $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$ verified to $< 10^{-10}$ | Wang et al. (2025), `2/backend/src/sph.rs:408-418`, `1/native/src/sph.cpp:463-467` |
| 12 | SPH-FVM | Kernel-Gradient Renormalization ($\mathbf{B}_i$) | Corrects kernel gradients via matrix $\mathbf{B}_i = \left[\sum_j V_j (\mathbf{x}_j - \mathbf{x}_i) \otimes \nabla_i W_{ij}\right]^{-1}$ for 1st-order consistency | Particle positions, neighbor volumes, kernel gradients | $d \times d$ correction matrix $\mathbf{B}_i$ per particle | If matrix determinant $|\det \mathbf{M}| < 10^{-14}$, revert to identity $\mathbf{I}$ | Wang et al. (2025), `1/native/src/sph.cpp:468-475` |
| 13 | SPH-FVM | OPMIS Domain Partitioning | Partitions simulation domain into Core particles, Buffer/Halo particles, and FVM cells based on core distance function | Mesh cell centroids, particle positions, core geometry, buffer width | Region tags: $M_{\text{common}}, M_{\text{interp}}, M_{\text{sleep}}$ and $P_{\text{core}}, P_{\text{buffer}}, P_{\text{ghost}}$ | Warn if coupling buffer has zero overlap; prune sleeping particles | Zhang et al. (2025), `2/backend/src/couple.rs:54-82`, `1/native/src/coupling.cpp:378-468` |
| 14 | SPH-FVM | OPMIS Taylor P2M Transfer | High-fidelity particle-to-mesh interpolation using Taylor expansion $\phi_c = \frac{1}{N} \sum_i [\phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i)]$ | Particle field values, moment-corrected particle gradients, cell centroids | Interpolated field values on mesh cells (or CSR matrix) | Exact for all linear fields $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$ (error $< 10^{-12}$) | Zhang et al. (2025), `3/sph-fvm-mesher/src/transfer.cpp:169-220` |
| 15 | SPH-FVM | OPMIS Affine M2P Transfer | Mesh-to-particle interpolation using affine-reproducing weighted least squares | Mesh cell field values, cell centroids, particle positions | Interpolated field values on halo/buffer particles (CSR matrix) | Fallback to Shepard weights if moment matrix is ill-conditioned ($\text{cond} > 10^6$) | Zhang et al. (2025), `2/backend/src/couple.rs:146-182`, `3/sph-fvm-mesher/src/transfer.cpp:37-75` |
| 16 | Moving Mesh | ALE Node Displacement PDE | Computes interior mesh velocity/displacement by solving variable-stiffness Laplacian PDE | Prescribed boundary displacements $\Delta \mathbf{x}_{\text{bnd}}$, cell volumes/wall distances | Interior node displacement field $\Delta \mathbf{x}$ | Near-wall cells preserve layer thickness; interior absorbs bulk motion | `1/native/src/mesh.cpp:727-774`, `resources/papers/literature_survey_2026.md` |
| 17 | Moving Mesh | Degenerate Cell Audit | Detects inverted, sliver, or crushed cells during mesh motion and flags remeshing | Updated mesh node coordinates | List of degenerate cell/face IDs, boolean remesh trigger | Trigger remeshing when $V_c \le 0$, $\theta_f > 75^\circ$, or $\text{AR}_c > 25$ | `1/native/src/mesh.cpp:684-711` |
| 18 | Moving Mesh | Local Cavity Remeshing | Removes bad cells and performs local Bowyer-Watson retriangulation with field interpolation | Degenerate cell IDs, existing mesh, field values | Locally updated mesh topology and interpolated solution | Retain boundary conformity; preserve volume conservation via GCL | `1/native/src/mesh.cpp:245-296` |
| 19 | Python Bridge | Pure-Python Mesh Engine | 100% standalone NumPy/SciPy fallback module (`backend/meshing/mesh_engine.py`) | Meshing parameters, geometry definitions | `FoamMesh`-compatible Python objects, error-map, OPMIS operators | Runs out-of-the-box without requiring native compilation | `backend/native_rust/foam_reader.py`, `ORIGINAL_REQUEST.md` R6 |
| 20 | Python Bridge | Native Binary Dispatch | Transparent ctypes / C-ABI bridge detecting compiled `.dll` / `.so` / `.pyd` libraries | Directory path or parameters | Direct zero-copy numpy buffer wrapping over native Rust/C++ pointers | Gracefully falls back to pure Python if native library is missing or fails load | `backend/native_rust/foam_fast_io/src/ffi.rs`, `foam_reader.py:40-75` |

---

## Edge Cases

| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | Non-Orthogonal Decomposition | Orthogonal face where $\mathbf{d}$ is exactly parallel to $\mathbf{S}_f$ ($\theta_f = 0^\circ$) | $\mathbf{S}_f \cdot \mathbf{d} = \|\mathbf{S}_f\| \|\mathbf{d}\|$, factor $= \|\mathbf{S}_f\| / \|\mathbf{d}\|$, $\boldsymbol{\Delta}_f = \mathbf{S}_f$, $\mathbf{k}_f = \mathbf{0}$. Invariant $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ holds exactly. |
| 2 | Non-Orthogonal Decomposition | Face perpendicular to cell-centre vector: $\mathbf{S}_f \cdot \mathbf{d} \to 0$ ($\theta_f \to 90^\circ$) | Denominator $|\mathbf{S}_f \cdot \mathbf{d}| \le 10^{-15}$. Code traps singularity, sets $\boldsymbol{\Delta}_f = \mathbf{S}_f$ and $\mathbf{k}_f = \mathbf{0}$, and tags face in `error_map` with critical mask (`mask = 3`). |
| 3 | Closed Cell Divergence | Highly distorted polyhedral/triangular cell with skewed faces | Sum of outward area vectors $\sum_{f \in \text{faces}(i)} \sigma_{if} \mathbf{S}_f$ evaluated. Must satisfy $\max_i \|\sum \sigma_{if} \mathbf{S}_f\| < 10^{-11}$. Verified via divergence theorem. |
| 4 | Cell Volume Calculation | Arbitrary polyhedral cell with non-planar faces | Pyramid decomposition from face approximations to cell centroid: $V_c = \frac{1}{3} \sum_f \sigma_{if} (\mathbf{x}_f \cdot \mathbf{S}_f)$. Yields strictly positive volume $V_c > 10^{-15}$ for valid meshes. |
| 5 | DistMesh Boundary Seeding | Node placed outside boundary ($\phi(\mathbf{x}) > 0$) | Initial seeding rejects points with $\phi > -0.05 h_0$. During relaxation, points crossing $\phi > 0$ are projected back along normalized gradient $\mathbf{x} \leftarrow \mathbf{x} - \phi \frac{\nabla \phi}{\|\nabla \phi\|^2}$. |
| 6 | DistMesh Sharp Corners | Point placed at sharp 90° or acute corner | Multiple boundary tangents create gradient discontinuity. SDF projection uses analytical or robust finite-difference gradient with radius $\varepsilon = 10^{-5} h_0$; corner points marked as fixed. |
| 7 | Bowyer-Watson Delaunay | 4 or more co-circular / co-spherical nodes | Determinant of circumcircle test $\approx 0$. Determinant threshold $\det > 10^{-18}$ prevents infinite cavity loop or ambiguous edge swaps. |
| 8 | Boundary Layer Extrusion | Concave corner with high curvature ($R_{\text{curv}} < \delta_{\text{bl}}$) | Extrusion normals intersect (coordinate cross-over). Inward normals must be smoothed via Laplacian averaging or truncated when distance between adjacent extruded nodes drops below $0.5 h_1$. |
| 9 | Boundary Layer Termination | Outer boundary layer face connecting to interior unstructured mesh | Transition ratio checked: layer thickness $h_{N_{\text{bl}}} = h_1 r^{N_{\text{bl}}-1}$ matches interior element size $h_0$ within 20% to prevent artificial numerical dissipation jumps. |
| 10 | SPH Position Divergence | Interior particle with full spherical/circular kernel support | Divergence of position $\text{div}\,\mathbf{r} = -\sum_j V_j r_{ij} \frac{dW}{dr} \approx d$ ($2.0 \pm 0.05$ in 2D, $3.0 \pm 0.05$ in 3D). Correctly tagged as `PCAT_INTERIOR`. |
| 11 | SPH Boundary Particle | Wall particle with hemispherical truncated support | $\text{div}\,\mathbf{r} \approx 0.5 \cdot d < 0.75 d$. Tagged as `PCAT_BOUNDARY`. Virtual boundary vector $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ non-zero. |
| 12 | SPH 0th-Order Wall Consistency | Virtual wall vector closure at physical wall | Condition $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} \equiv \mathbf{0}$ verified to machine precision ($< 10^{-14}$). Area sum $\sum_{i \in \text{bnd}} \|\mathbf{A}_{iS}\|$ matches physical wall perimeter/surface within $0.5\%$. |
| 13 | OPMIS Moment Matrix Singular | Collinear or co-planar particle neighborhood in buffer layer | Determinant of moment matrix $\det(\mathbf{M}_i) < 10^{-14}$. Inversion fails; OPMIS falls back to 0th-order Shepard weighting with warning flag `valid_moments[i] = 0`. |
| 14 | OPMIS Linear Patch Test | Linear field $f(x, y, z) = a x + b y + c z + d$ across SPH-FVM interface | P2M Taylor reconstruction and M2P affine least squares both reproduce $f(\mathbf{x})$ exactly with maximum residual $L_\infty < 10^{-12}$. Zero spurious pressure jump across interface. |
| 15 | ALE Laplacian Smoothing | Wall moves with large displacement $\Delta \mathbf{x}_w \approx h_0$ | Uniform stiffness causes boundary cells to distort. Distance-weighted stiffness $\gamma = 1 / d_w^2$ preserves boundary layer thickness while far-field cells absorb deformation smoothly. |
| 16 | ALE Inverted Cell | Sudden large boundary acceleration or vortex impingement | Cell volume becomes negative: $V_c \le 0$ or orient2d $< 0$. Audit catches condition immediately, triggers local cavity remeshing before linear solver executes. |
| 17 | CAD Input With Sketches/Lines | STEP or IGES file containing 2D construction curves or orphan vertices | Parser filters geometric entities with topological dimension $< 2$, extracts only manifold closed solid shells ($V - E + F = 2$), discarding wireframe sketches. |
| 18 | RCM Graph Disconnected | Domain with multi-component disjoint fluid regions | Standard BFS would visit only one component. RCM iterates over all unvisited nodes $0 \dots N_c-1$, sorting each connected component independently. |
| 19 | Python Fallback Performance | 100,000 cells evaluated in pure Python without native binary | Vectorized NumPy operations (NumPy arrays, SciPy spatial Delaunay, KDTree, and CSR matrices) prevent Python `for`-loop bottlenecks, executing full mesh audit in $< 1.5$ seconds. |
| 20 | C-ABI Pointer Null Check | Python caller passes null pointer or corrupted mesh handle to `foam_fast_io` | FFI functions in `ffi.rs` check `if ptr.is_null() { return 0; }` preventing segfaults; Python ctypes wrapper catches null pointer and raises `ValueError`. |

---

## Detailed Architectural & Mathematical Specifications

### 1. DiFVM (Du et al., 2026) Graph-Topology & Structure of Arrays (SoA)

#### 1.1 Fundamental Topological Arrays
An unstructured polyhedral finite-volume mesh with $N_p$ vertices, $N_c$ cells, $N_f$ total faces, and $N_{\text{int}}$ internal faces is represented as flat, cache-aligned Structure of Arrays (SoA):

- `points`: Float64 array of shape $(N_p, 3)$, vertex coordinates $\mathbf{x}_p = (x, y, z)$.
- `face_offsets`: Int64 array of shape $(N_f + 1,)$, prefix sum of face vertex counts.
- `face_nodes`: Int64 array of shape $(\sum_{f} n_{v, f},)$, ordered indices of vertices defining face polygons.
- `owner`: Int64 array of shape $(N_f,)$, cell index $O(f) \in [0, N_c - 1]$ owning face $f$.
- `neighbour`: Int64 array of shape $(N_{\text{int}},)$, neighbour cell index $N(f) \in [0, N_c - 1]$ sharing face $f$. For boundary faces $f \ge N_{\text{int}}$, neighbour is omitted (or $-1$).
- `boundaries`: List of patch definitions, each having contiguous face range $[\text{start\_face}, \text{start\_face} + n_{\text{faces}})$.

#### 1.2 Geometric Quantities & Divergence Theorem Decomposition

##### Face Centroid ($\mathbf{x}_f$) and Area Normal Vector ($\mathbf{S}_f$)
For an arbitrary polygon with ordered vertices $\mathbf{p}_0, \mathbf{p}_1, \dots, \mathbf{p}_{m-1}$:
1. Preliminary centroid estimate:
   $$\mathbf{c}_{\text{approx}} = \frac{1}{m} \sum_{k=0}^{m-1} \mathbf{p}_k$$
2. Decompose into $m$ triangles formed by $(\mathbf{c}_{\text{approx}}, \mathbf{p}_k, \mathbf{p}_{k+1})$ (with $\mathbf{p}_m \equiv \mathbf{p}_0$):
   $$\mathbf{S}_{f, k} = \frac{1}{2} (\mathbf{p}_k - \mathbf{c}_{\text{approx}}) \times (\mathbf{p}_{k+1} - \mathbf{c}_{\text{approx}})$$
   $$A_k = \|\mathbf{S}_{f, k}\|, \quad \mathbf{c}_k = \frac{\mathbf{c}_{\text{approx}} + \mathbf{p}_k + \mathbf{p}_{k+1}}{3}$$
3. Integrated face normal area vector $\mathbf{S}_f$ and centroid $\mathbf{x}_f$:
   $$\mathbf{S}_f = \sum_{k=0}^{m-1} \mathbf{S}_{f, k}, \quad |\mathbf{S}_f| = \|\mathbf{S}_f\|$$
   $$\mathbf{x}_f = \frac{\sum_{k=0}^{m-1} \mathbf{c}_k A_k}{\sum_{k=0}^{m-1} A_k} \quad (\text{if } \sum A_k > 10^{-15}, \text{ else } \mathbf{c}_{\text{approx}})$$
   *Convention*: $\mathbf{S}_f$ points outward from owner $O(f)$ into neighbour $N(f)$.

##### Cell Volume ($V_c$) and Centroid ($\mathbf{x}_c$)
Via Gauss's divergence theorem, volume and centroid are evaluated by decomposing the polyhedron into pyramids rooted at the origin (or cell reference point):
$$V_c = \frac{1}{3} \sum_{f \in \text{faces}(c)} \sigma_{cf} (\mathbf{x}_f \cdot \mathbf{S}_f)$$
$$\mathbf{x}_c = \frac{1}{4 V_c} \sum_{f \in \text{faces}(c)} \sigma_{cf} (\mathbf{x}_f \cdot \mathbf{S}_f) \mathbf{x}_f$$
where $\sigma_{cf} = +1$ if $c = \text{owner}(f)$ and $\sigma_{cf} = -1$ if $c = \text{neighbour}(f)$.

##### Geometric Invariant: Closed-Cell Condition
$$\max_{c \in [0, N_c - 1]} \left\| \sum_{f \in \text{faces}(c)} \sigma_{cf} \mathbf{S}_f \right\| < 10^{-11}$$

#### 1.3 Vectorized Over-Relaxed Non-Orthogonal Decomposition
Let $\mathbf{d} = \mathbf{x}_{N(f)} - \mathbf{x}_{O(f)}$ be the vector connecting the cell centres of neighbour and owner.
The diffusion flux across face $f$ is:
$$\int_f \Gamma \nabla \phi \cdot d\mathbf{S} \approx \Gamma_f (\nabla \phi)_f \cdot \mathbf{S}_f$$
In the DiFVM over-relaxed formulation (Jasic & Gosman, 1995; Du et al., 2026), $\mathbf{S}_f$ is decomposed into an orthogonal component $\boldsymbol{\Delta}_f$ parallel to $\mathbf{d}$ and a non-orthogonal correction vector $\mathbf{k}_f$:
$$\boldsymbol{\Delta}_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}} \mathbf{d}$$
$$\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f$$
Notice that by construction:
$$\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$$
The normal gradient is decomposed into an implicit orthogonal part and an explicit deferred correction:
$$(\nabla \phi)_f \cdot \mathbf{S}_f = \underbrace{\|\boldsymbol{\Delta}_f\| \frac{\phi_N - \phi_O}{\|\mathbf{d}\|}}_{\text{Implicit matrix term}} + \underbrace{\mathbf{k}_f \cdot (\overline{\nabla \phi})_f}_{\text{Explicit deferred correction}}$$
For boundary faces: $\mathbf{d} = \mathbf{x}_f - \mathbf{x}_{O(f)}$, $\boldsymbol{\Delta}_f = \mathbf{d}$, $\mathbf{k}_f = \mathbf{0}$.

#### 1.4 Quality Metrics & Error-Map Formulation
For each internal face $f$:
1. **Non-orthogonality angle** $\theta_f$:
   $$\cos \theta_f = \frac{\mathbf{S}_f \cdot \mathbf{d}}{\|\mathbf{S}_f\| \|\mathbf{d}\|}$$
   $$\theta_f = \arccos\left(\text{clamp}(\cos \theta_f, -1.0, 1.0)\right) \times \frac{180^\circ}{\pi}$$
2. **Face Skewness** $\text{skew}_f$:
   Intersection of line of centres with face plane:
   $$\mathbf{x}' = \mathbf{x}_O + \frac{(\mathbf{x}_f - \mathbf{x}_O) \cdot \mathbf{S}_f}{\mathbf{d} \cdot \mathbf{S}_f} \mathbf{d}$$
   $$\text{skew}_f = \frac{\|\mathbf{x}_f - \mathbf{x}'\|}{\|\mathbf{d}\|}$$
3. **Linear interpolation weight** $w_f$:
   $$w_f = \frac{\|\mathbf{x}_N - \mathbf{x}_f\|}{\|\mathbf{x}_N - \mathbf{x}_f\| + \|\mathbf{x}_O - \mathbf{x}_f\|}$$
4. **Aspect Ratio** $\text{AR}_c$:
   $$\text{AR}_c = \frac{\max_{e \in c} L_e}{\min_{e \in c} L_e} \quad \text{or} \quad \frac{R_{\text{circ}}}{2 r_{\text{in}}}$$

##### Solver Stabilization Mask (`mesh.error_map['mask']`)
$$\text{mask}_f = \begin{cases}
0 & (\text{Normal}) & \theta_f \le 65^\circ \text{ and } \text{skew}_f \le 0.50 \\
1 & (\text{Over-Relaxed Corrected}) & 65^\circ < \theta_f \le 75^\circ \text{ or } 0.50 < \text{skew}_f \le 0.85 \\
2 & (\text{Limited Under-Relaxation}) & 75^\circ < \theta_f \le 85^\circ \text{ or } 0.85 < \text{skew}_f \le 1.20 \\
3 & (\text{Critical Stabilized}) & \theta_f > 85^\circ \text{ or } \text{skew}_f > 1.20
\end{cases}$$
When $\text{mask}_f \ge 2$: PISO/SIMPLE limits explicit non-orthogonal gradient updates $\mathbf{k}_f \cdot \nabla \phi$ via a Minmod limiter and applies local under-relaxation $\alpha_p \leftarrow 0.5 \alpha_p$.

---

### 2. In-Memory Unstructured Meshing & Boundary Layers

#### 2.1 DistMesh Force Equilibrium (Persson & Strang, 2004)
The mesh nodes $\mathbf{X} = \{\mathbf{x}_i\}_{i=0}^{N-1}$ are treated as a mechanical truss structure governed by fictitious bar forces:
1. Sizing function $h(\mathbf{x})$:
   $$h(\mathbf{x}) = \min\left(h_0 \left(1 + g \frac{|\phi(\mathbf{x})|}{h_0}\right), h_0 \cdot r_{\max}\right)$$
   where $g \approx 0.45$ (grading) and $r_{\max} \approx 3.0$.
2. Delaunay Triangulation: Generate simplex topology $T = \text{Delaunay}(\mathbf{X})$.
3. Filter exterior simplices: Remove simplex if centroid $\mathbf{x}_{\text{cen}}$ has SDF $\phi(\mathbf{x}_{\text{cen}}) \ge -10^{-3} h_0$.
4. Bar Forces on edge $(i, j)$:
   $$L_{ij} = \|\mathbf{x}_i - \mathbf{x}_j\|$$
   $$h_{ij} = h\left(\frac{\mathbf{x}_i + \mathbf{x}_j}{2}\right)$$
   $$s = 1.2 \sqrt{\frac{\sum_{(i,j)} L_{ij}^2}{\sum_{(i,j)} h_{ij}^2}}$$
   $$f(L_{ij}, h_{ij}) = \max\left(s \cdot h_{ij} - L_{ij}, 0\right)$$
   Force on node $i$:
   $$\mathbf{F}_i = \sum_{j \in \mathcal{N}(i)} f(L_{ij}, h_{ij}) \frac{\mathbf{x}_i - \mathbf{x}_j}{L_{ij}}$$
5. Forward Euler Position Update:
   $$\mathbf{x}_i \leftarrow \mathbf{x}_i + \Delta t \, \mathbf{F}_i \quad (\Delta t = 0.22)$$
6. SDF Boundary Projection:
   For all mobile nodes $i \ge N_{\text{fixed}}$ where $\phi(\mathbf{x}_i) > 0$:
   $$\mathbf{x}_i \leftarrow \mathbf{x}_i - \phi(\mathbf{x}_i) \frac{\nabla \phi(\mathbf{x}_i)}{\|\nabla \phi(\mathbf{x}_i)\|^2}$$
7. Retriangulate when maximum displacement exceeds threshold: $\max_i \|\Delta \mathbf{x}_i\| > 0.2 h_0$.

#### 2.2 Incremental Bowyer-Watson Kernel
To insert point $\mathbf{p}$:
1. Centroid-grid bucket hash or adjacency walking finds triangle containing $\mathbf{p}$.
2. Cavity search: breadth-first search marks dead triangles whose circumcircle/circumsphere contains $\mathbf{p}$:
   $$\det \begin{bmatrix}
   x_a - p_x & y_a - p_y & \|\mathbf{x}_a - \mathbf{p}\|^2 \\
   x_b - p_x & y_b - p_y & \|\mathbf{x}_b - \mathbf{p}\|^2 \\
   x_c - p_x & y_c - p_y & \|\mathbf{x}_c - \mathbf{p}\|^2
   \end{bmatrix} > 10^{-18}$$
3. Cavity boundary: Collect directed edges of dead triangles whose neighbor is alive or non-existent.
4. Remove dead triangles and link $\mathbf{p}$ to cavity boundary edges.
5. Constraint recovery: If required boundary edge $(a, b)$ is missing, apply quadrilateral diagonal flip if convex; else bisect constraint at midpoint.

#### 2.3 Boundary Layer Inflation (Quads / Prisms)
For high-Reynolds wall boundary layers requiring $y^+ \sim 1$:
1. First cell height: $h_1 = y^+ \frac{\nu}{u_\tau}$, with growth ratio $r \in [1.15, 1.25]$.
2. Wall normal vectors: For each wall node $\mathbf{x}_{w, i}$, compute inward unit normal:
   $$\mathbf{n}_{\text{in}, i} = -\frac{\nabla \phi(\mathbf{x}_{w, i})}{\|\nabla \phi(\mathbf{x}_{w, i})\|}$$
3. Extruded layer positions:
   $$\mathbf{x}_{k, i} = \mathbf{x}_{w, i} + \delta_k \mathbf{n}_{\text{in}, i}, \quad \delta_k = h_1 \frac{r^k - 1}{r - 1} \quad (k = 1, \dots, N_{\text{bl}})$$
4. Corner smoothing: At convex/concave corners, normals are angle-weighted and checked for intersection:
   $$\|\mathbf{x}_{k, i} - \mathbf{x}_{k, j}\| \ge 0.5 \|\mathbf{x}_{w, i} - \mathbf{x}_{w, j}\|$$
5. Quad/Prism connectivity: Quad between $(\mathbf{x}_{k-1, i}, \mathbf{x}_{k-1, i+1}, \mathbf{x}_{k, i+1}, \mathbf{x}_{k, i})$ can either be retained as quad or split into two triangles along the shortest diagonal.
6. Smooth transition: Inflation terminates when $h_k \approx h_{\text{unstructured}}$, yielding smooth element size transition.

#### 2.4 Reverse Cuthill-McKee (RCM) Algorithm
Minimizes CSR matrix bandwidth $\beta = \max_{(i, j) \in E} |i - j|$:
1. Construct undirected cell adjacency graph $G = (V, E)$ from internal faces: edge between $O(f)$ and $N(f)$.
2. Compute node degrees $\text{deg}(c) = |\mathcal{N}(c)|$.
3. Find pseudo-peripheral start node $s$ using George-Liu algorithm:
   - Run BFS from arbitrary node $u$ to find deepest level set. Pick node $v$ of minimum degree in deepest level.
   - Run BFS from $v$; repeat until diameter no longer increases. Set $s = v$.
4. Standard Cuthill-McKee BFS:
   - Queue $Q = [s]$, visited set $V = \{s\}$, result array $P = []$.
   - While $Q$ is not empty:
     - Pop $u = Q.\text{pop\_front}()$, append $u$ to $P$.
     - Collect unvisited neighbours $\mathcal{N}(u) \setminus V$.
     - Sort collected neighbours in ascending order of degree: $\text{deg}(v_1) \le \text{deg}(v_2) \le \dots$.
     - Mark neighbours as visited and append to $Q$.
5. Reverse permutation:
   $$\pi_{\text{RCM}}[i] = P[N_c - 1 - i] \quad \forall i \in [0, N_c - 1]$$
6. Inverse mapping $\text{idx}[c] = i$ such that $\pi_{\text{RCM}}[i] = c$.
7. Relabel cells: $c_{\text{new}} = \text{idx}[c_{\text{old}}]$.

---

### 3. Hybrid Eulerian SPH-FVM Coupling (Wang et al., 2025 & Zhang et al., 2025 OPMIS)

#### 3.1 Incomplete Support Detection via Divergence of Position
In continuous Eulerian SPH, the identity $\nabla \cdot \mathbf{r} = d$ holds for dimension $d$. In discrete SPH:
$$\text{div}\,\mathbf{r}_i = -\sum_{j \in \mathcal{N}(i)} V_j \, r_{ij} \frac{\partial W}{\partial r}(r_{ij}, h_{ij})$$
- Full support in bulk fluid: $\text{div}\,\mathbf{r}_i \approx d$ ($2.0$ in 2D, $3.0$ in 3D).
- Incomplete support detector (Wang, Haidn & Hu, 2025):
  $$\text{div}\,\mathbf{r}_i < 0.75 \, d \implies \text{Particle } i \text{ is on physical boundary}$$

#### 3.2 Virtual FVM Wall Boundary Vector ($\mathbf{A}_{iS}$)
For an interior particle with full support, 0th-order consistency holds:
$$\mathbf{A}_i = \sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij} \to \mathbf{0}$$
For a truncated boundary particle, $\mathbf{A}_i \neq \mathbf{0}$. The virtual boundary closing vector is defined as:
$$\mathbf{A}_{iS} = -\sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij}$$
This guarantees exact 0th-order consistency:
$$\mathbf{A}_{iS} + \sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij} \equiv \mathbf{0} \quad (< 10^{-10})$$
Physical equivalence: $\mathbf{A}_{iS}$ acts as the virtual FVM wall face:
- Equivalent wall face area: $S_{i, \text{wall}} = \|\mathbf{A}_{iS}\|$
- Outward wall normal vector: $\mathbf{n}_{iS} = \frac{\mathbf{A}_{iS}}{\|\mathbf{A}_{iS}\|}$
- Wall perimeter/area sum invariant: $\sum_{i \in \text{bnd}} \|\mathbf{A}_{iS}\| \approx A_{\text{wall, exact}}$

#### 3.3 Kernel-Gradient Renormalization Tensor ($\mathbf{B}_i$)
To restore 1st-order consistency $\sum_j V_j (\mathbf{x}_j - \mathbf{x}_i) \otimes \widetilde{\nabla}_i W_{ij} = \mathbf{I}$:
$$\mathbf{M}_i = \sum_{j \in \mathcal{N}(i)} V_j (\mathbf{x}_j - \mathbf{x}_i) \otimes \nabla_i W_{ij}$$
$$\mathbf{B}_i = \mathbf{M}_i^{-1}$$
$$\widetilde{\nabla}_i W_{ij} = \mathbf{B}_i \nabla_i W_{ij}$$
If $\det \mathbf{M}_i \le 10^{-14}$, revert to uncorrected gradient $\mathbf{B}_i = \mathbf{I}$.

#### 3.4 OPMIS Adaptive Domain Partitioning
Domain is decomposed into three overlapping zones:
1. Core Region ($d_{\text{core}}(\mathbf{x}) < 0$): Handled by SPH particles ($P_{\text{core}}$). FVM cells here are sleeping ($M_{\text{sleep}}$).
2. Buffer / Overlap Region ($0 \le d_{\text{core}}(\mathbf{x}) < \delta_{\text{buf}}$ with $\delta_{\text{buf}} \approx 2 \sim 3 h_p$): Both SPH particles ($P_{\text{buffer}}$) and FVM cells ($M_{\text{interp}}$) are active.
3. Mesh Region ($d_{\text{core}}(\mathbf{x}) \ge \delta_{\text{buf}}$): Pure FVM domain ($M_{\text{common}}$). SPH particles are pruned ($P_{\text{ghost}}$).

#### 3.5 Moment-Reproducing Taylor Interpolation

##### Particle-to-Mesh (P2M) Taylor Interpolation
For each interpolation cell $c \in M_{\text{interp}}$ with centroid $\mathbf{x}_c$:
$$\phi(\mathbf{x}_c) = \frac{1}{|\mathcal{P}_c|} \sum_{i \in \mathcal{P}_c} \left[ \phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i) \right]$$
where $\mathcal{P}_c$ is the set of particles located within cell $c$ (or nearest $k$ particles), and particle gradient $\nabla \phi_i$ is evaluated using the moment matrix:
$$\mathbf{M}_{p, i} = \sum_{j \in \mathcal{N}(i)} w_{ij} (\mathbf{x}_j - \mathbf{x}_i) \otimes (\mathbf{x}_j - \mathbf{x}_i)$$
$$\nabla \phi_i = \mathbf{M}_{p, i}^{-1} \sum_{j \in \mathcal{N}(i)} w_{ij} (\mathbf{x}_j - \mathbf{x}_i) (\phi_j - \phi_i)$$
*Mathematical Proof of Exact Linear Reproduction*:
Let $\phi(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$.
Then $\phi_j - \phi_i = \mathbf{a} \cdot (\mathbf{x}_j - \mathbf{x}_i)$.
$$\sum_j w_{ij} (\mathbf{x}_j - \mathbf{x}_i) (\phi_j - \phi_i) = \left[ \sum_j w_{ij} (\mathbf{x}_j - \mathbf{x}_i) \otimes (\mathbf{x}_j - \mathbf{x}_i) \right] \mathbf{a} = \mathbf{M}_{p, i} \mathbf{a}$$
$$\nabla \phi_i = \mathbf{M}_{p, i}^{-1} \mathbf{M}_{p, i} \mathbf{a} = \mathbf{a}$$
Substituting into the Taylor reconstruction:
$$\phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i) = (\mathbf{a} \cdot \mathbf{x}_i + b) + \mathbf{a} \cdot (\mathbf{x}_c - \mathbf{x}_i) = \mathbf{a} \cdot \mathbf{x}_c + b = \phi(\mathbf{x}_c)$$
Average over $|\mathcal{P}_c|$ is identically $\phi(\mathbf{x}_c)$ to machine precision.

##### Mesh-to-Particle (M2P) Affine Weighted Least-Squares
For halo particle $p \in P_{\text{buffer}}$ at position $\mathbf{x}_p$:
$$\phi(\mathbf{x}_p) = \sum_{k \in \mathcal{M}_p} w_{pk} \phi(\mathbf{x}_k)$$
where weights $w_{pk}$ satisfy the affine reproduction constraints:
$$\sum_{k \in \mathcal{M}_p} w_{pk} = 1, \quad \sum_{k \in \mathcal{M}_p} w_{pk} (\mathbf{x}_k - \mathbf{x}_p) = \mathbf{0}$$
Obtained by minimizing $\sum_k \frac{(w_{pk} - w_k^0)^2}{w_k^0}$ where $w_k^0 = \frac{V_k}{0.04 + \|\mathbf{x}_k - \mathbf{x}_p\|^2}$.

---

### 4. Moving Mesh & Dynamic Re-Adaption

#### 4.1 Arbitrary Lagrangian-Eulerian (ALE) Formulation
When mesh boundaries move (e.g. oscillating airfoil or piston), mesh nodes move with grid velocity:
$$\mathbf{w}_i = \frac{\mathbf{x}_i^{n+1} - \mathbf{x}_i^n}{\Delta t}$$
$$\mathbf{w}_f = \frac{\mathbf{x}_f^{n+1} - \mathbf{x}_f^n}{\Delta t}$$

##### Discrete Geometric Conservation Law (DGCL)
The volume change must strictly equal the swept face volume:
$$\frac{V_c^{n+1} - V_c^n}{\Delta t} = \sum_{f \in \text{faces}(c)} \sigma_{cf} \mathbf{w}_f \cdot \mathbf{S}_f^{n+1/2}$$
##### ALE Convection Flux
$$\dot{m}_f = \rho_f (\mathbf{u}_f - \mathbf{w}_f) \cdot \mathbf{S}_f$$

#### 4.2 Variable-Stiffness Laplacian Smoothing
Interior node displacements $\Delta \mathbf{x}$ are determined by solving a variable-diffusion Laplace equation:
$$\nabla \cdot (\gamma(\mathbf{x}) \nabla (\Delta \mathbf{x})) = \mathbf{0}$$
$$\text{Boundary conditions: } \Delta \mathbf{x}|_{\text{moving}} = \mathbf{d}_{\text{bnd}}(t), \quad \Delta \mathbf{x}|_{\text{fixed}} = \mathbf{0}$$
Stiffness field:
$$\gamma(\mathbf{x}) = \frac{1}{(d_{\text{wall}}(\mathbf{x}) + \varepsilon)^\alpha} \quad \text{or} \quad \gamma_c = \frac{1}{V_c^\alpha} \quad (\alpha \in [1.0, 2.0])$$
This ensures near-wall cells have very high stiffness, preserving orthogonal boundary layer prism heights, while larger interior cells absorb the mesh deformation.

#### 4.3 Degenerate Cell Detection & Remeshing Triggers
At each time step after mesh displacement, audit:
1. $V_c \le 0$ or $V_c / V_{c, 0} < 0.05$ (Inverted / collapsed cell)
2. $\theta_f > 75^\circ$ (Severe non-orthogonality)
3. $\text{skew}_f > 0.85$ (Severe face skewness)
4. $\text{AR}_c > 25.0$ (Sliver/needle element)
5. Normal inversion: $(\mathbf{x}_f - \mathbf{x}_c) \cdot \mathbf{S}_f < 0$

##### Remeshing Trigger
- If number of bad cells $N_{\text{bad}} = 0$: continue simulation.
- If $N_{\text{bad}} \le 20$ and isolated: apply local cavity remeshing (delete bad cells, re-triangulate cavity via Bowyer-Watson, interpolate fields via OPMIS P2M).
- If $N_{\text{bad}} > 20$ or near-wall layer collapses: trigger global remeshing via DistMesh / Bowyer-Watson.

---

### 5. Python-Bridge & Pure-Python Fallback

#### 5.1 Architecture of `backend/meshing/mesh_engine.py`
The module must provide transparent execution:
```
           +-----------------------------------------------+
           |           MeshEngine Facade Class             |
           +-----------------------------------------------+
                                  |
              Is native library compiled & available?
                     /                         \
                   YES                          NO
                   /                             \
     +--------------------------+    +--------------------------+
     | Native Rust/C++ Bridge   |    | Pure Python / NumPy Core |
     | (ctypes / foam_fast_io)  |    | (scipy.spatial.Delaunay, |
     | Zero-copy pointer arrays |    | csgraph.rcm, vectorized) |
     +--------------------------+    +--------------------------+
                   \                             /
                    +---------------------------+
                    |  Unified FoamMesh Output  |
                    |  (identical data arrays)  |
                    +---------------------------+
```

#### 5.2 Pure-Python / NumPy Algorithmic Implementation Requirements
The pure-Python fallback uses:
- `scipy.spatial.Delaunay`: Triangulation kernel.
- `scipy.spatial.cKDTree`: Nearest-neighbor particle queries and background grid acceleration.
- `scipy.sparse.csgraph.reverse_cuthill_mckee`: Bandwidth reduction permutation.
- `scipy.sparse.linalg.spsolve`: Laplacian mesh deformation system.
- Vectorized NumPy arrays matching exact C-ABI types: `float64` for coordinates/vectors, `int64` for indices.

---

## Acceptance Criteria & Numerical Invariants

| # | Invariant / Acceptance Criterion | Mathematical Definition | Strict Tolerance |
|---|----------------------------------|-------------------------|------------------|
| 1 | Positive Cell Volumes | $V_c > 0 \quad \forall c \in [0, N_c - 1]$ | $V_c > 10^{-15}$ |
| 2 | Closed-Cell Gauss Divergence | $\max_c \|\sum_{f \in \text{faces}(c)} \sigma_{cf} \mathbf{S}_f\|$ | $< 10^{-11}$ |
| 3 | Non-Orthogonal Decomposition Identity | $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f \quad \forall f \in [0, N_f - 1]$ | $< 10^{-14}$ |
| 4 | SPH 0th-Order Wall Consistency | $\|\mathbf{A}_{iS} + \sum_{j} V_j \nabla_i W_{ij}\|$ | $< 10^{-10}$ |
| 5 | OPMIS Linear Moment Reproduction | $\|f(\mathbf{x}_c) - \text{P2M}(f)\|$ for $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$ | $< 10^{-12}$ |
| 6 | Total Domain Enclosed Volume | $|\sum_c V_c - V_{\text{analytical}}|$ for unit cube / box | $< 10^{-10}$ |
| 7 | Discrete Geometric Conservation Law (DGCL) | $|\frac{V_c^{n+1} - V_c^n}{\Delta t} - \sum_f \sigma_{cf} \mathbf{w}_f \cdot \mathbf{S}_f^{n+1/2}|$ | $< 10^{-12}$ |
| 8 | RCM Bandwidth Reduction | $\beta_{\text{after}} < \beta_{\text{before}}$ | $\ge 50\%$ reduction |
| 9 | Boundary Layer Orthogonality | $\theta_f$ on first inflation layer wall faces | $\le 15^\circ$ |
| 10 | Error Map Coverage | `error_map` has entries for every face and cell | 100% complete |
