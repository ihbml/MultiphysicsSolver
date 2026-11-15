# Comprehensive Specification Report: 8 Surveyed Papers & Backend Architecture Synthesis (Super-Elements & Carbon Architecture)

**Author**: Specification Miner Agent (`spec_miner_m5_survey_2`)  
**Date**: 2026-11-12  
**Mission**: Survey and extract exact mathematical formulations, algorithms, invariants, data structures, and code integration designs for the 8 surveyed papers (2024–2026) and `SUGGESTIONS_BACKEND.md` (§10 & §11.2), completing requirements R2 and R3 of `ORIGINAL_REQUEST.md`.

---

## 1. Executive Summary

This specification report provides complete, production-grade mathematical models, discrete algorithms, algebraic invariants, and concrete C-ABI / C++20 / Rust / Python interface definitions for:
1. **Paper 1 (`CADWatertightFineMeshing_2604.02141v1.pdf`)**: Topology-First B-Rep Meshing, eliminating 2D trim curve (`PCURVE`) failures via 3D Dijkstra loop embedding, Jordan curve separation, and 1D boundary stitching in `backend/geometry/parsers/step_parser.py`.
2. **Paper 2 (`Differtialble3DCAD_2110.01182v1.pdf`)**: Compiled parametric CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$, automated geometric safety inequalities $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$, and adjoint reverse-mode chain rule interfaces $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}} = (\frac{\partial J}{\partial \mathbf{x}})^T \frac{\partial \mathbf{x}}{\partial \boldsymbol{\theta}}$.
3. **Paper 3 (`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`)**: Optimal $L^p$-metric tensor sizing $\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ and noise-free $P_2$ edge midpoint Hessian reconstruction in `backend/meshing/mesh_engine.py` and `backend/meshing/native/src/octree_hexcore.cpp`.
4. **Paper 4 (`MeshlessVoronoiOnGPU_3272127.3275092.pdf`)**: Meshless pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`), radius-of-security clipping termination, and 6-tetrahedron projection volume integration for zero-lock GPU meshing.
5. **Paper 5 (`VoronoiDiagrams_2404.18629v1.pdf`)**: Analytical 1st and 2nd derivatives of Voronoi and power diagram vertices via local $3 \times 3$ system $\mathbf{A} \mathbf{x}_c = \mathbf{b}$, proving $C^1$-smooth cell volumes across topological flips for Newton-CVT and adjoint sensitivity.
6. **Paper 6 (`DiscreteGCL_1709.04788v4.pdf`)**: Exact 3D swept volume via Simpson's rule $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ and BDF2-DGCL $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$ in `backend/meshing/moving_mesh.py` (resolving Audit-H9).
7. **Paper 7 (`Demirdzic-2022-Symmetryplaneboundaryconditions...`)**: Orthogonal symmetry plane connection ($\boldsymbol{\Delta} \parallel \mathbf{s}$, identically zero cross-diffusion) and Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vectors and rank-2 tensors with diagonally dominant Patankar splitting in `polymesh_io.py`, `difvm_topology.py`, and `case_generator.py`.
8. **Paper 8 (`FSI_Monolithic_2505.22920v1.pdf`)**: Monolithic 3D Eulerian FSI with one-continuum stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$, Neo-Hookean solid elasticity, 3D geometric PLIC, 5th-order WENO-Z Oldroyd transport, and fluid reset $\mathbf{B} = \mathbf{I}$ for $\phi < 10^{-8}$.
9. **Super-Elements Synthesis (`SUGGESTIONS_BACKEND.md` §10)**: `constexpr` 23-class ASP-optimized hexahedral super-elements ($\min SJ \ge 0.302$, 0 hanging nodes/edges), asymmetric residual volume voting ($RV_{\text{missing}}^2 + RV_{\text{excess}}$), top-down entity mapping (0, 1, 2, 3 DOF), and unified $J_{\text{ENS}}$ quality metric with fictitious hex pyramid base check.
10. **Carbon Architecture Synthesis (`SUGGESTIONS_BACKEND.md` §11.2)**: Unified C++20/23 core with Rust orchestration; strict C-ABI Structure-of-Arrays (SoA) data lake interfaces (`(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)`); embedded expression engine (Rhai/SIMD bytecode) for generic boundary conditions and sources; and native binary container format `.mphys` (64-byte `MPHYS001` header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, FlatBuffers footer `MPHYSEOF`).

---

## 2. Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | CAD & Geometry | Discard 2D Trimming Curves (`PCURVE`) | Eliminates self-intersecting / gapped 2D parametric trim curves in STEP files; relies strictly on 3D B-Rep topology graph | STEP entity graph (`ADVANCED_FACE`, `EDGE_LOOP`) | Untrimmed parametric patches $p^2(u,v): [0,1]^2 \to \mathbb{R}^3$ | Raises `ValueError` if B-Rep graph has no faces | Paper 1 (§1, §2) |
| 2 | CAD & Geometry | Untrimmed Patch RGB Subdivision | Adaptive recursive subdivision of $[0,1]^2$ domain into 3D triangular mesh $M_k$ within tolerance $\epsilon$ | Surface patch, deflection $\epsilon$, $h_{\max}$ | Triangular surface mesh $M_k$ | Divergence if patch Jacobian singular | Paper 1 (§2.2) |
| 3 | CAD & Geometry | Dijkstra 3D Loop Embedding | Embeds 3D boundary curves onto $M_k$ edges via shortest paths, avoiding occupied edges $\mathcal{F}_{\text{occupied}}$ | Surface mesh $M_k$, sampled 3D boundary curves | 1D closed edge cycle $\Gamma \subset M_k$ | Inserts Steiner vertices if no path exists | Paper 1 (§3, Alg. 1) |
| 4 | CAD & Geometry | Jordan Curve Combinatorial Separation | Flood-fill partition of $M_k$ across $\Gamma$ into Disk ($D_k$) and Annulus without geometric ray tests | Mesh $M_k$, boundary cycle $\Gamma$, loop type (outer/inner) | Trimmed manifold surface patch $M_k'$ | Raises `TopologyError` if $\Gamma$ is not closed | Paper 1 (§3.2, Alg. 2) |
| 5 | CAD & Geometry | 1D Parameter Curve Stitching | Unifies parametric coordinates $\{t_k\} \subset [0,1]$ across shared b-edges, splitting adjacent edges at identical $\tau_m$ | Two adjacent patches $M_a, M_b$, shared curve $p^1(t)$ | 100% watertight stitched 2-manifold surface mesh | Raises `ValueError` if edge endpoint mismatch $> 10^{-6}$ | Paper 1 (§4, §5) |
| 6 | Differentiable CAD | Compiled CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta})$ | Generates symbolic computational graph of surface nodes from CAD operations (`Box`, `Extrude`, `Chamfer`, `Revolve`) | CAD design vector $\boldsymbol{\theta}_{\text{CAD}} \in \mathbb{R}^p$ | Surface coordinates $\mathbf{x}_{\text{surf}} \in \mathbb{R}^{N_v \times 3}$, Jacobian $\nabla_{\boldsymbol{\theta}} \mathcal{F}$ | Throws `DomainError` if parameter out of bounds | Paper 2 (§3.1, Fig. 4) |
| 7 | Differentiable CAD | Geometric Safety Inequalities $g_m(\boldsymbol{\theta}) \ge 0$ | Automated extraction of non-inversion inequalities (e.g. chamfer $d \le \|V_4 - V_3\|_2$) during DAG compilation | CAD operation parameters | Inequality constraint functions $g_m(\boldsymbol{\theta})$, gradients $\nabla g_m$ | SLSQP/MMA rejects step if $g_m(\boldsymbol{\theta}) < 0$ | Paper 2 (§3.2, Eq. 2) |
| 8 | Differentiable CAD | Adjoint Chain Rule Interface | Evaluates $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}} = (\frac{\partial J}{\partial \mathbf{x}})^T \frac{\partial \mathbf{x}}{\partial \boldsymbol{\theta}}$ via VJP without CAD re-meshing or kernel re-evaluation | Adjoint shape sensitivity $\frac{\partial J}{\partial \mathbf{x}_{\text{surf}}}$, DAG | Parameter gradient $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}_{\text{CAD}}} \in \mathbb{R}^p$ | Zero gradient if node uncoupled from objective | Paper 2 (§3.3, Eq. 1) |
| 9 | Differentiable CAD | Biharmonic & Volume Regularization | Regularizes shape optimization via biharmonic energy $E_{\text{bh}} = \operatorname{tr}(\mathbf{D}^T \mathbf{Q} \mathbf{D})$ and volume penalty $E_{\text{vol}} = (\operatorname{Vol} - \operatorname{Vol}_0)^2$ | Mesh displacement $\mathbf{D}$, target volume $\operatorname{Vol}_0$ | Regularization scalar and gradient $\nabla_{\boldsymbol{\theta}} E$ | None (convex quadratic penalties) | Paper 2 (§3.4, Eq. 7, 11) |
| 10 | Adaptive Meshing | Optimal Multiscale $L^p$-Metric Tensor | Computes Riemannian metric $\mathcal{M}_{L^p} = D_{L^p} (\det|H_u|)^{-\frac{1}{2p+3}} |H_u|$ for exact target cell count $N$ | Hessian field $H_u(\mathbf{x})$, target count $N$, exponent $p$ | SPD metric tensor field $\mathcal{M}(\mathbf{x}) \in \mathcal{S}_3^{++}$ | Reverts to isotropic background metric if $H_u \equiv 0$ | Paper 3 (Thm. 2.1, Eq. 2.10) |
| 11 | Adaptive Meshing | Noise-Free $P_2$ Edge Midpoint Reconstruction | Computes edge midpoint values $u_h(\mathbf{m}_e)$ from node values and volume-weighted gradients without $O(h^{-2})$ noise | Node coordinates $\mathbf{p}$, cell gradients $\nabla u$ | Quadratic $P_2$ field, constant element Hessian $H_u$ | Throws if element degenerate ($V \le 0$) | Paper 3 (§3.1, Eq. 3.1) |
| 12 | GPU Meshing | Dual Polyhedron Representation | Represents convex 3D Voronoi cells via half-space planes `float4 P[64]` and dual triangles `int3 T[96]` | Seed point $\mathbf{x}_i$, neighbor points | Dual polyhedron in local GPU shared memory (2176 B) | Throws overflow if $>64$ planes or $>96$ triangles | Paper 4 (§3.2, Fig. 8) |
| 13 | GPU Meshing | Radius-of-Security Early Exit | Terminates neighbor clipping when neighbor distance $\|\mathbf{x}_p - \mathbf{x}_i\| > 2 \max_{v \in T} \|v - \mathbf{x}_i\|_2$ | Sorted neighbor candidates, current cell bounds | Early termination flag `is_cell_finished` | None (mathematically exact termination) | Paper 4 (§3.1, Alg. 2) |
| 14 | GPU Meshing | Dual Plane Clipping (`clipByPlane`) | Clips dual cell by plane $\mathbf{n}\cdot\mathbf{x} + d = 0$, identifies removed cycle $\partial\mathcal{R}$, and seals with dual fan $(s,t,p)$ | Bisector plane $\mathcal{P}$, dual cell $(P, T)$ | Updated dual cell $(P', T')$ | Melquiond-Pion FP filter if $|\det A| < 10^{-12}$ | Paper 4 (§3.3, Alg. 3) |
| 15 | GPU Meshing | 6-Tetrahedron Projection Volume Integration | Evaluates exact cell volume and centroid by summing 6 oriented tetrahedra per dual triangle $(u,v,w) \in T$ | Dual cell $(P, T)$, seed $\mathbf{x}_i$ | Volume $V_i$, centroid $\mathbf{C}_i$, face areas $\mathbf{S}_f$ | Stokes cancellation removes exterior volume | Paper 4 (§3.5, Fig. 13) |
| 16 | Differentiable Meshing | Analytical Voronoi Vertex Derivatives | Evaluates $\frac{\partial \mathbf{x}_c}{\partial q} = \mathbf{A}^{-1}(\frac{\partial \mathbf{b}}{\partial q} - \frac{\partial \mathbf{A}}{\partial q}\mathbf{x}_c)$ for all 4 vertex classes via local $3\times 3$ system | 3 incident planes $(u, v, w)$, seed/weight variable $q$ | Vertex sensitivity $\frac{\partial \mathbf{x}_c}{\partial q} \in \mathbb{R}^3$ | Melquiond-Pion exact predicate if $\det\mathbf{A} \approx 0$ | Paper 5 (§3.2, Eq. 4-9) |
| 17 | Differentiable Meshing | $C^1$ Volume Continuity across Topology Flips | Proves $\nabla_{\mathbf{c}} V_i$ is continuous across Delaunay flips / Voronoi edge collapses, enabling Newton-CVT | Power diagram seeds $\mathbf{c}$, weights $\mathbf{w}$ | $C^1$-smooth cell volume $V_i(\mathbf{c}, \mathbf{w})$, Hessian $\nabla^2 V_i$ | Inversion guard prevents non-convex cells | Paper 5 (§3.4, Eq. 29) |
| 18 | Moving Mesh ALE | Exact 3D Simpson Swept Volume | Evaluates face swept volume $\Delta V_f = \frac{1}{6}\Delta\mathbf{x}_f \cdot (\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ exactly for quadratic $\mathbf{S}_f(\tau)$ | Vertex positions $\mathbf{x}^n, \mathbf{x}^{n+1}$, face indices | Swept volume $\Delta V_f^{n, n+1} \in \mathbb{R}$ | Residual $< 10^{-15}$ against $\sum \Delta V = \Delta V_c$ | Paper 6 (§4, Eq. 4); Audit-H9 |
| 19 | Moving Mesh ALE | Multistep BDF2-DGCL Face Flux | Emits grid volume flux $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$, matching BDF2 equation weights | Swept volumes $\Delta V_f^{n,n+1}, \Delta V_f^{n-1,n}$, $\Delta t$ | Conservative grid flux $\dot{V}_f^{n+1}$ | Zero artificial mass source to machine precision | Paper 6 (§4, Eq. 6); Audit-H9 |
| 20 | Symmetry BC | Zero Cross-Diffusion Symmetry Connection | Enforces $\boldsymbol{\Delta} = 2\frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s} \parallel \mathbf{s}$, proving non-orthogonal cross-diffusion $\mathbf{k}_f \equiv \mathbf{0}$ on symmetry planes | Boundary face normal $\mathbf{s}$, centroid distance $\mathbf{d}$ | Collinear delta vector $\boldsymbol{\Delta}$, cross vector $\mathbf{k}_f \equiv \mathbf{0}$ | Raises if $\mathbf{d}\cdot\mathbf{s} \le 0$ (inverted boundary cell) | Paper 7 (Eq. 3-7) |
| 21 | Symmetry BC | Householder Vector Diffusion Split | Splitting $\mathbf{v}_M = \mathbf{R}\mathbf{v}_P$ into positive implicit diagonal $D_{vi,\text{im}} = \Gamma_v \frac{s_i^2}{\mathbf{d}\cdot\mathbf{s}}$ and explicit RHS $D_{vi,\text{ex}}$ | Viscosity $\Gamma_v$, face normal $\mathbf{s}$, vector $\mathbf{v}_P$ | Diagonally dominant matrix entries $(a_P, b_P)$ | Preserves diagonal dominance unconditionally | Paper 7 (Eq. 15-16) |
| 22 | Symmetry BC | Householder Rank-2 Tensor Split | Reflects stress tensor $\mathbf{T}_M = \mathbf{R}\mathbf{T}_P\mathbf{R}^T$ with implicit diagonal $D_{Tij,\text{im}} = \Gamma_T \frac{s_i^2 + s_j^2}{\mathbf{d}\cdot\mathbf{s}}$ | Tensor diffusivity $\Gamma_T$, $\mathbf{s}$, $\mathbf{d}$, tensor $\mathbf{T}_P$ | Diagonally dominant tensor matrix split | Unconditionally positive main diagonal | Paper 7 (Eq. 21-22) |
| 23 | Monolithic FSI | One-Continuum Eulerian Stress Blend | Evaluates mixture momentum stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$ across fluid-solid interface | Solid fraction $\phi$, fluid stress $\boldsymbol{\sigma}^f$, solid stress $\boldsymbol{\sigma}^s$ | Unified stress tensor $\boldsymbol{\sigma}(\mathbf{x}) \in \mathbb{R}^{3 \times 3}$ | Clipped to pure fluid/solid outside interface band | Paper 8 (Eq. 7-10) |
| 24 | Monolithic FSI | Neo-Hookean Elastic Stress | Evaluates solid elastic stress $\sigma_{ij}^s = 2\mu^s D_{ij} + G^s (B_{ij} - \delta_{ij})$ via left Cauchy-Green tensor $\mathbf{B}$ | Strain rate $D_{ij}$, deformation tensor $B_{ij}$, shear modulus $G^s$ | Solid Cauchy stress $\boldsymbol{\sigma}^s$ | Fails if $\det\mathbf{B} \le 0$ (element inversion) | Paper 8 (Eq. 14) |
| 25 | Monolithic FSI | Oldroyd-B Transport with WENO-Z | Advects $\mathbf{B}$ via $\partial_t B_{ij} + u_k \partial_{x_k} B_{ij} = B_{ik} \partial_{x_k} u_j + B_{kj} \partial_{x_k} u_i$ using 5th-order WENO-Z | Velocity field $\mathbf{u}$, tensor field $\mathbf{B}$, $\Delta t$ | Updated tensor $\mathbf{B}^{n+1}$ | Stable for CFL $< 0.5$ under AB2 | Paper 8 (Eq. 16, 30-39) |
| 26 | Monolithic FSI | Pure Fluid Deformation Reset ($\mathbf{B} = \mathbf{I}$) | Resets $\mathbf{B} \leftarrow \mathbf{I}$ wherever solid volume fraction $\phi < 10^{-8}$, eliminating spurious elastic shear accumulation | Tensor field $\mathbf{B}$, volume fraction $\phi$ | Reset tensor field $\mathbf{B}$ with zero shear in fluid | Eliminates exponential strain divergence | Paper 8 (§3.2, Eq. 30) |
| 27 | Super-Elements | 23 Canonical ASP Super-Elements | Replaces cut Cartesian hex cells by ASP-optimized conforming sub-elements using 35 candidate nodes | 8-bit corner mask $D \in [0, 255]$ | Assembly of Hex8, Prism6, Pyr5, Tet4 elements | Look-up table guarantees $\min SJ \ge 0.302$ | SUGGESTIONS §10.1; Stromberg |
| 28 | Super-Elements | Asymmetric Residual Volume Voting | Selects optimal super-element mask by penalizing missing volume quadratically: $R = RV_{\text{missing}}^2 + RV_{\text{excess}}$ | 216 subdomain evaluations against SDF $\phi$ | Optimal 8-bit mask $D^*$, node votes $v(e, N)$ | Prevents zigzag staircasing at boundary | SUGGESTIONS §10.2; Stromberg |
| 29 | Super-Elements | Top-Down Entity Mapping & Defeaturing | Maps mesh faces $\to$ CAD surfaces, edges $\to$ CAD curves, nodes $\to$ CAD corners, assigning 0/1/2/3 DOF | Surface triangles, CAD B-Rep entities | Constrained node DOFs for projection and smoothing | Automatically defeatures features $< \frac{1}{2} l_{\min}$ | SUGGESTIONS §10.3; Stromberg |
| 30 | Super-Elements | Aspect-Sensitive $J_{\text{ENS}}$ & Pyramid Check | Computes normalized scaled Jacobian $J_{\text{ENS}} \in [-1, 1]$ and checks pyramid base via fictitious hexahedron | Element nodes, element type (Hex, Wedge, Pyr, Tet) | Metric $J_{\text{ENS}}$, flags inverted pyramid bases ($J_{\text{ENS}} \le 0$) | Throws on inverted element ($J_{\text{ENS}} \le 0$) | SUGGESTIONS §10.4; Stromberg |
| 31 | Architecture Synthesis | Strict C-ABI SoA Descriptor | Encapsulates contiguous memory arrays as flat `(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)` | Flat buffer pointer, length, datatype enum, device ID | Zero-copy C-ABI span compatible with C++, Rust, GPU | Validates alignment (64 bytes) and bounds | SUGGESTIONS §11.2 |
| 32 | Architecture Synthesis | Embedded Expression Engine (Rhai / SIMD) | Evaluates user-defined analytical boundary functions $u(x,y,z,t)$ and source terms without C++ recompilation | String expression (e.g. `"10.0 * sin(2.0*pi*t)"`), coordinates | Evaluated float array across boundary/cell points | Throws syntax/type error at setup time | SUGGESTIONS §11.2 |
| 33 | Architecture Synthesis | Chunked Binary `.mphys` Container | Writes/reads 64-byte `MPHYS001` header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, and FlatBuffers footer | Solver mesh epoch, dynamic time-step fields | Chunked binary `.mphys` file with $\mathcal{O}(1)$ open time | Validates magic bytes and footer SHA-256 | SUGGESTIONS §11.2 |

---

## 3. Edge Cases

| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | Dijkstra 3D Loop Embedding | Seam edge on periodic cylinder or closed torus where b-vertices appear twice in loop | Algorithm duplicates seam b-vertices/edges into distinct topological instances so the loop remains a simple Jordan curve on a disk. |
| 2 | Jordan Curve Loop Separation | Non-convex patch with multiple inner holes (concentric cutouts) | Outer loop splits off disk $D_0$; each inner loop splits complement into hole and annulus; combinatorial flood-fill reliably extracts multiconnected domain without ray-casting leaks. |
| 3 | 1D Boundary Stitching | Adjacent NURBS patches with high curvature discrepancy along shared boundary | Parameter sampling unifies $\mathcal{T} = \{t_a\} \cup \{t_b\}$; segment subdivisions match coordinates to $< 10^{-14}$ m; avoids crack formation without moving vertices off the true boundary curve. |
| 4 | Differentiable CAD DAG | Chamfer parameter $\theta_d$ exceeds incident edge length ($d > \|V_4 - V_3\|$) | Safety inequality $g_{\text{chamfer}}(\boldsymbol{\theta}) = \|V_4 - V_3\| - d$ violates feasibility ($g < 0$); optimizer constraint projection rejects trial step before CAD kernel failure. |
| 5 | Differentiable CAD DAG | Collinear extrusion or zero-length revolve axis | Jacobian determinant of transformation drops to zero ($\det J = 0$); DAG evaluator throws `SingularGeometryException` instead of generating collapsed degenerate faces. |
| 6 | $L^p$-Metric Tensor Sizing | Uniform velocity field ($u = \text{const}$, Hessian $H_u \equiv 0$) | Metric sizing detects $\det|H_u| = 0$; safely reverts to isotropic background metric $\mathcal{M} = h_{\max}^{-2} \mathbf{I}$, preventing division by zero or NaN metric tensors. |
| 7 | $P_2$ Midpoint Reconstruction | Element with aspect ratio $> 100$ (boundary layer prism/wedge) | Volume-weighted nodal gradient projection dampens transversal high-frequency noise; Hermite interpolation along edge preserves steep wall-normal gradient without oscillations. |
| 8 | Meshless Voronoi GPU | Seed points arranged on a perfect Cartesian lattice (multiple cospherical seeds) | Voronoi vertices have valence $>3$; Melquiond-Pion floating-point filter detects degeneracy ($|\det A| < 10^{-12}$) and resolves perturbations deterministically without infinite loops. |
| 9 | Meshless Voronoi GPU | Seed located extremely close to boundary bounding plane ($d_{\text{plane}} < 10^{-10}$) | `clipByPlane` culls outer vertices; remaining volume shrinks smoothly; Stokes-based projection tetrahedra yield exact volume without negative volume anomalies. |
| 10 | Analytical Voronoi Derivatives | Two seeds swap topology order during deformation (Voronoi topology flip) | Vertex $\mathbf{x}_c$ transitions through co-spherical state; volume derivative $\nabla_{\mathbf{c}} V_i$ is proven $C^1$-continuous; analytical Jacobian matches finite-difference gradient across transition. |
| 11 | Simpson Swept Volume | Purely translational 3D mesh motion ($\mathbf{x}_k^{n+1} = \mathbf{x}_k^n + \mathbf{v} \Delta t$) | Midpoint face area equals endpoint face areas ($\mathbf{S}_f^n = \mathbf{S}_f^{n+1/2} = \mathbf{S}_f^{n+1}$); Simpson formula reduces exactly to $\mathbf{w}_f \cdot \mathbf{S}_f \Delta t$. |
| 12 | Simpson Swept Volume | Skewed rotating face where 4 nodes deform non-coplanarly | Midpoint face area vector captures quadratic twisting ($\mathbf{S}_f(\tau) = a \tau^2 + b \tau + c$); Simpson rule integrates swept volume to machine precision ($< 10^{-16}$); trapezoidal rule exhibits $O(\Delta t^2)$ error. |
| 13 | BDF2-DGCL Face Flux | Initial time step ($n=0$) where step $n-1$ does not exist | Engine automatically switches to Euler backward (BDF1-DGCL) $\dot{V}_f^1 = \frac{\Delta V_f^{0,1}}{\Delta t}$ on the first step, preserving exact GCL before resuming BDF2 on step $n \ge 1$. |
| 14 | Householder Symmetry BC | Symmetry plane inclined at arbitrary non-Cartesian angle (e.g. $45^\circ$ wedge) | $\boldsymbol{\Delta}$ remains exactly parallel to outward normal $\mathbf{s}$; cross-diffusion vector $\mathbf{k}_f \equiv \mathbf{0}$; Patankar split maintains strict diagonal dominance with zero non-orthogonal iterations needed. |
| 15 | Householder Symmetry BC | Boundary face with severe centroid skewness ($\mathbf{d} \not\parallel \mathbf{s}$) | Centroid tangential offset $\mathbf{t} = \mathbf{d} - \frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$ accurately corrects face centroid value $\mathbf{v}_B$ using the tangential gradient $\mathbf{t}\cdot\mathbf{G}_P$ without altering flux orthogonality. |
| 16 | Monolithic Eulerian FSI | Pure fluid cell with high shear rate ($\dot{\gamma} = 10^5 \text{ s}^{-1}, \phi = 0$) | Fluid reset immediately overwrites $\mathbf{B} = \mathbf{I}$; stops unbounded exponential growth of Oldroyd elastic stress in fluid region without artificial numerical viscosity. |
| 17 | Monolithic Eulerian FSI | Cut cell with tiny solid volume fraction ($\phi = 10^{-9} < \epsilon_c = 10^{-8}$) | Engine clips $\phi \leftarrow 0$ and sets $\mathbf{B} \leftarrow \mathbf{I}$, preventing ill-conditioned linear systems from tiny solid slivers. |
| 18 | Super-Elements Meshing | Flat wall aligned with grid lines (corner mask matches canonical case 1 or 2) | Super-element look-up table emits pure hexahedra or standard prisms with $\min SJ \ge 0.500$ and zero pyramids, minimizing element count. |
| 19 | Super-Elements Meshing | Highly concave corner with narrow notch (case 22) | Answer Set Programming ensures conforming transition using internal octant support nodes $(\pm 0.5, \pm 0.5, \pm 0.5)$; achieves $\min SJ \ge 0.302$ without inverted elements. |
| 20 | Super-Elements Meshing | Pyramid with warped, non-planar quadrilateral base | Standard nodal SJ fails to detect folded base; fictitious hexahedron of height $\bar{l}_{\text{base}}$ evaluates negative Jacobian, correctly flagging $J_{\text{ENS}} \le 0$. |
| 21 | C-ABI SoA Data Lake | Data transfer between host CPU and CUDA/Vulkan GPU device | Struct `device_id` indicates memory locality; host functions verify `device_id == 0` or execute zero-copy unified memory / DMA staging without raw pointer corruption. |
| 22 | `.mphys` Container | Sudden simulation termination / crash during time-step writing | File format writes new steps append-only and commits by writing the 64-byte footer `MPHYSEOF` last; incomplete trailing bytes are ignored on reopen, preserving all previous time steps. |

---

## 4. Deep-Dive Mathematical Formulations & Algorithmic Designs

### 4.1 Paper 1: Topology-First B-Rep Meshing (`CADWatertightFineMeshing_2604.02141v1.pdf`)
*Target Module: `backend/geometry/parsers/step_parser.py`*

#### 4.1.1 Problem & Mathematical Definition
In real-world CAD files, 2D trimming curves (`PCURVE`) in $(u,v)$ parameter space frequently self-intersect or fail to meet tolerances, producing boundary cracks or mesher crashes in conventional tools (OpenCASCADE, Gmsh).  
Paper 1 establishes the combinatorial B-Rep topology:
$$\mathcal{P} = \left(\mathcal{V}^b, \mathcal{E}^b, \mathcal{L}^b, \mathcal{F}^b\right)$$
where each topological entity is treated as an inviolable constraint:
- b-vertices $v_i^b \in \mathcal{V}^b$ with 3D positions $\mathbf{p}_i^0 \in \mathbb{R}^3$ (`CARTESIAN_POINT`, `VERTEX_POINT`).
- b-edges $e_i^b \in \mathcal{E}^b$ with 3D curves $\mathbf{p}_i^1(t): [0,1] \to \mathbb{R}^3$ (`EDGE_CURVE`).
- b-loops $\ell_i^b \in \mathcal{L}^b$ containing ordered oriented edges (`EDGE_LOOP`, `ORIENTED_EDGE`).
- b-faces $f_i^b \in \mathcal{F}^b$ with parametric surfaces $\mathbf{p}_i^2(u,v): [0,1]^2 \to \mathbb{R}^3$ (`ADVANCED_FACE`).

#### 4.1.2 Algorithmic Execution Steps
1. **Discard 2D Curves:** Ignore all `PCURVE` entities.
2. **Untrimmed Surface Sampling:**
   Mesh the full $[0,1]^2$ domain of face $f_k^b$ into a regular triangle grid. Adaptively subdivide triangles using Red-Green-Blue (RGB) subdivision until:
   $$\max_{\mathbf{x} \in \Delta} \|\mathbf{x} - \mathbf{p}_k^2(u,v)\|_2 \le \epsilon_{\text{chord}}, \quad \max_{e \in \Delta} \|e\|_2 \le h_{\max}$$
3. **Dijkstra Loop Embedding (Algorithm 1):**
   - Sample 3D boundary curve points $\mathbf{q}_j = \mathbf{p}^1(t_j)$.
   - Project each $\mathbf{q}_j$ onto the closest surface vertex in $M_k$.
   - Connect consecutive projected points $\mathbf{q}_j, \mathbf{q}_{j+1}$ via shortest paths on the edge graph of $M_k$ using Dijkstra's algorithm.
   - Assign infinite edge weight to already occupied boundary edges $\mathcal{F}_{\text{occupied}}$ to prevent edge self-intersection.
4. **Jordan Curve Separation (Algorithm 2):**
   - The embedded edge loop forms a closed 1D cycle $\Gamma$.
   - Flood-fill traverse dual face adjacency.
   - For `FACE_OUTER_BOUND`: retain interior disk component $D_k$, discard outside.
   - For `FACE_BOUND` (holes): discard interior of hole component, retain annulus.
5. **Harmonic Snapping:**
   - Snap boundary vertices to true 3D curve coordinates $\mathbf{p}^1(t)$.
   - Relax interior vertices via the discrete Laplace-Beltrami equation:
     $$\sum_{j \in \mathcal{N}(i)} w_{ij} (\mathbf{x}_j - \mathbf{x}_i) = \mathbf{0}, \quad w_{ij} = \frac{1}{2}(\cot \alpha_{ij} + \cot \beta_{ij}), \quad \mathbf{x}|_{\partial M_k} = \mathbf{x}_{\text{boundary}}$$
6. **1D Parameter Stitching:**
   - For shared edge $e^b = f_a^b \cap f_b^b$, merge parameter sets: $\mathcal{T} = \{t_{a,k}\} \cup \{t_{b,l}\} = \{\tau_0, \tau_1, \dots, \tau_M\}$.
   - Split edge segments in both meshes at all $\tau_m$.
   - Weld coincident vertices; guarantees 100% watertight 2-manifold surface mesh without cracks.

---

### 4.2 Paper 2: Differentiable 3D CAD Programs (`Differtialble3DCAD_2110.01182v1.pdf`)
*Target Module: `backend/geometry/differentiable_cad.py` & Adjoint Optimizer Bridge*

#### 4.2.1 Mathematical Formulation
The parametric CAD model is compiled into an immutable Directed Acyclic Graph (DAG) $\mathcal{G}$:
$$\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}}), \quad \boldsymbol{\theta}_{\text{CAD}} \in \mathbb{R}^p$$
where $\boldsymbol{\theta}_{\text{CAD}}$ represents design parameters (dimensions, angles, fillet radii).

#### 4.2.2 Automated Safety Inequalities
During DAG trace construction, geometric validity constraints are generated automatically:
$$g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0, \quad m = 1, \dots, M$$
Examples:
- Non-inverting chamfer: $g_{\text{chamfer}}(\boldsymbol{\theta}) = \|\mathbf{x}_4 - \mathbf{x}_3\|_2 - d_{\text{chamfer}} \ge 0$
- Minimum shell thickness: $g_{\text{thick}}(\boldsymbol{\theta}) = \|\mathbf{x}_{\text{outer}} - \mathbf{x}_{\text{inner}}\|_2 - t_{\min} \ge 0$
- Non-negative length: $g_{\text{len}}(\boldsymbol{\theta}) = L - L_{\min} \ge 0$

#### 4.2.3 Adjoint Chain Rule Interface
Let $J(\mathbf{u}, p, \mathbf{x}_{\text{surf}})$ be the CFD cost functional (e.g. pressure drop, drag, temperature variance). The discrete adjoint solver computes surface sensitivity $\frac{\partial J}{\partial \mathbf{x}_{\text{surf}}} \in \mathbb{R}^{3 N_v}$.  
The total gradient with respect to CAD parameters is evaluated via the exact chain rule:
$$\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}_{\text{CAD}}} = \left(\frac{\partial J}{\partial \mathbf{x}_{\text{surf}}}\right)^T \frac{\partial \mathbf{x}_{\text{surf}}}{\partial \boldsymbol{\theta}_{\text{CAD}}} = \sum_{i=1}^{N_v} \left(\frac{\partial J}{\partial \mathbf{x}_i}\right)^T \frac{\partial \mathbf{x}_i}{\partial \boldsymbol{\theta}_{\text{CAD}}}$$
evaluated via Reverse-Mode Automatic Differentiation (Vector-Jacobian Product) through the DAG $\mathcal{F}$ in $\mathcal{O}(N_v)$ time.

#### 4.2.4 Regularization Terms
- Biharmonic deformation energy:
  $$E_{\text{bh}}(\boldsymbol{\theta}) = \operatorname{tr}\left(\mathbf{D}(\boldsymbol{\theta})^T \mathbf{Q} \mathbf{D}(\boldsymbol{\theta})\right), \quad \mathbf{D}(\boldsymbol{\theta}) = \mathbf{x}_{\text{surf}}(\boldsymbol{\theta}) - \mathbf{x}_{\text{surf}}(\boldsymbol{\theta}_0)$$
  where $\mathbf{Q} = \mathbf{M}^{-1} \mathbf{L}^T \mathbf{M}^{-1} \mathbf{L}$ is the squared cotangent Laplacian.
- Volume conservation constraint:
  $$E_{\text{vol}}(\boldsymbol{\theta}) = \left(\operatorname{Vol}(\boldsymbol{\theta}) - \operatorname{Vol}_0\right)^2, \quad \operatorname{Vol}(\boldsymbol{\theta}) = \frac{1}{6} \sum_{f=(a,b,c)} \mathbf{x}_a \cdot (\mathbf{x}_b \times \mathbf{x}_c)$$

---

### 4.3 Paper 3: Continuous Mesh Framework & $L^p$-Metric Tensor Sizing (`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`)
*Target Module: `backend/meshing/mesh_engine.py` & `backend/meshing/native/src/octree_hexcore.cpp`*

#### 4.3.1 Mathematical Metric Formulation
In a Riemannian continuous metric framework, the spatial step size tensor $\mathcal{M}(\mathbf{x}) \in \mathcal{S}_3^{++}$ is governed by the absolute Hessian matrix:
$$|H_u(\mathbf{x})| = \mathcal{R}(\mathbf{x}) \operatorname{diag}\left(|\lambda_1|, |\lambda_2|, |\lambda_3|\right) \mathcal{R}(\mathbf{x})^T$$
Under total target cell count $N = \mathcal{C}(\mathcal{M}) = \int_\Omega \sqrt{\det\mathcal{M}} \, \mathrm{d}\mathbf{x}$, the global $L^p$ interpolation error minimization problem yields the unique optimal metric tensor:
$$\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p} \left(\det|H_u(\mathbf{x})|\right)^{-\frac{1}{2p+3}} |H_u(\mathbf{x})|$$
with normalization constant $D_{L^p}$:
$$D_{L^p} = N^{\frac{2}{3}} \left(\int_\Omega \left(\det|H_u(\mathbf{x})|\right)^{\frac{p}{2p+3}} \mathrm{d}\mathbf{x}\right)^{-\frac{2}{3}}$$
- For $p=1$ (acoustics, wake vortices, entropy layers): exponent is $-\frac{1}{5} = -0.20$.
- For $p=2$ (viscous flows, Navier-Stokes shear): exponent is $-\frac{1}{7} \approx -0.1428$.
- For $p \to \infty$ ($L^\infty$ norm): exponent tends to $0$, concentrating resolution solely at maximum shock peaks.

#### 4.3.2 Noise-Free $P_2$ Edge Midpoint Reconstruction
To eliminate $O(h^{-2})$ noise amplification during discrete second differentiation:
1. Compute nodal volume-weighted gradient projection:
   $$\nabla_R u_h(\mathbf{p}) = \frac{\sum_{K_j \in \mathcal{S}(\mathbf{p})} |K_j| \nabla(u_h|_{K_j})}{\sum_{K_j \in \mathcal{S}(\mathbf{p})} |K_j|}$$
2. Reconstruct cubic Hermite scalar value at every edge midpoint $\mathbf{m}_e = \frac{\mathbf{p}+\mathbf{q}}{2}$:
   $$u_h(\mathbf{m}_e) = \frac{u_h(\mathbf{p}) + u_h(\mathbf{q})}{2} + \frac{\nabla_R u_h(\mathbf{p}) - \nabla_R u_h(\mathbf{q})}{8} \cdot (\mathbf{p} - \mathbf{q})$$
3. Compute element Hessian from the unique $P_2$ polynomial defined by nodal and midpoint values.

---

### 4.4 Paper 4: Meshless Voronoi on GPU (`MeshlessVoronoiOnGPU_3272127.3275092.pdf`)
*Target Module: `backend/meshing/gpu_voronoi.hpp` & CUDA/Vulkan Mesh Kernels*

#### 4.4.1 Dual Polyhedral Representation
Because every vertex of a generic 3D convex polyhedron is trivalent (intersection of 3 planes), each Voronoi cell is represented in GPU shared memory as:
```cpp
struct alignas(16) GpuVoronoiCell {
    float4 P[64];  // Half-space planes a*x + b*y + c*z + d <= 0
    int3   T[96];  // Dual triangles (u, v, w) defining primal vertex v_uvw = P_u cap P_v cap P_w
    int    num_planes;
    int    num_triangles;
};
```
Memory footprint is 2,176 bytes per cell, allowing 100% lock-free parallel execution within GPU warp shared memory.

#### 4.4.2 Radius-of-Security Criterion
When clipping cell $i$ with neighbor candidates $\mathbf{x}_j$ ordered by distance from seed $\mathbf{x}_i$:
$$R_{\max} = \max_{(u,v,w) \in T} \|\mathbf{v}_{uvw} - \mathbf{x}_i\|_2$$
If the next candidate $\mathbf{x}_p$ satisfies:
$$\|\mathbf{x}_p - \mathbf{x}_i\|_2 > 2 R_{\max}$$
clipping terminates immediately with mathematical certainty that no subsequent point can touch the cell.

#### 4.4.3 6-Tetrahedron Projection Volume Integration
For each dual triangle $(u, v, w) \in T$ with primal vertex $\mathbf{v} = \mathbf{v}_{uvw}$, project $\mathbf{v}$ onto planes $\mathbf{p}_u, \mathbf{p}_v, \mathbf{p}_w$ and edges $\mathbf{e}_{uv}, \mathbf{e}_{vw}, \mathbf{e}_{wu}$. Evaluate 6 oriented tetrahedra connecting seed $\mathbf{x}_i$ to the projections. Exterior volumes cancel identically by Stokes' theorem, giving machine-exact volume $V_i$, centroid $\mathbf{C}_i$, and face area vectors $\mathbf{S}_f$.

---

### 4.5 Paper 5: Differentiable Voronoi Diagrams (`VoronoiDiagrams_2404.18629v1.pdf`)
*Target Module: `backend/meshing/diff_voronoi.py` & Adjoint Mesh Sensitivity*

#### 4.5.1 Local Linear System for Cell Vertices
A 3D cell vertex $\mathbf{x}_c$ formed by 3 bisector/boundary planes satisfies:
$$\mathbf{A}(\mathbf{c}, \mathbf{v}) \mathbf{x}_c = \mathbf{b}(\mathbf{c}, \mathbf{v})$$
For bisector between seeds $\mathbf{x}_i$ and $\mathbf{x}_j$ with weights $w_i, w_j$:
$$(\mathbf{x}_j - \mathbf{x}_i) \cdot \mathbf{x}_c = \frac{1}{2}\left(\|\mathbf{x}_j\|^2 - \|\mathbf{x}_i\|^2 + w_i - w_j\right)$$

#### 4.5.2 Analytical Derivatives
Implicit differentiation yields:
$$\frac{\partial \mathbf{x}_c}{\partial q} = \mathbf{A}^{-1} \left(\frac{\partial \mathbf{b}}{\partial q} - \frac{\partial \mathbf{A}}{\partial q} \mathbf{x}_c\right)$$
For seed $\mathbf{x}_j$ on row $k$:
$$\frac{\partial \mathbf{x}_c}{\partial \mathbf{x}_j} = \mathbf{A}^{-1} \mathbf{e}_k \otimes (\mathbf{x}_j - \mathbf{x}_c)$$
Second derivatives (Hessian):
$$\frac{\partial^2 \mathbf{x}_c}{\partial q \, \partial r} = \mathbf{A}^{-1} \left(\frac{\partial^2 \mathbf{b}}{\partial q \partial r} - \frac{\partial^2 \mathbf{A}}{\partial q \partial r}\mathbf{x}_c - \frac{\partial \mathbf{A}}{\partial q}\frac{\partial \mathbf{x}_c}{\partial r} - \frac{\partial \mathbf{A}}{\partial r}\frac{\partial \mathbf{x}_c}{\partial q}\right)$$

#### 4.5.3 $C^1$ Volume Continuity
Cell volume is evaluated via boundary surface integration:
$$V_i = \frac{1}{3} \sum_{f \in \partial C_i} \mathbf{x}_{c,f} \cdot \mathbf{S}_f$$
During a Delaunay edge flip or Voronoi topological transition, the area of the vanishing facet $\|\mathbf{S}_f\| \to 0$, proving that $V_i(\mathbf{c}, \mathbf{w})$ is strictly $C^1$-continuous across all topology flips.

---

### 4.6 Paper 6: Discrete GCL on Moving Meshes (`DiscreteGCL_1709.04788v4.pdf`)
*Target Module: `backend/meshing/moving_mesh.py` (Audit-H9)*

#### 4.6.1 Exact 3D Swept Volume via Simpson's Rule
For a moving face whose nodes translate linearly from $t^n$ to $t^{n+1}$:
$$\mathbf{x}_k(\tau) = (1-\tau)\mathbf{x}_k^n + \tau \mathbf{x}_k^{n+1}, \quad \tau \in [0, 1]$$
the face area vector $\mathbf{S}_f(\tau) = \frac{1}{2} \sum \mathbf{x}_a(\tau) \times \mathbf{x}_b(\tau)$ is a quadratic polynomial in $\tau$.  
Simpson's 3/8 rule integrates the 3D swept volume **with zero truncation error**:
$$\Delta V_f^{n,n+1} = \int_0^1 (\mathbf{x}_f^{n+1} - \mathbf{x}_f^n) \cdot \mathbf{S}_f(\tau) \, \mathrm{d}\tau = \frac{1}{6} (\mathbf{x}_f^{n+1} - \mathbf{x}_f^n) \cdot \left(\mathbf{S}_f^n + 4 \mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1}\right)$$
where $\mathbf{S}_f^{n+1/2}$ is the face area vector evaluated at midpoint coordinates $\mathbf{x}_k^{n+1/2} = \frac{\mathbf{x}_k^n + \mathbf{x}_k^{n+1}}{2}$.

#### 4.6.2 Multistep BDF2-DGCL Conservation
To satisfy the discrete geometric conservation law under BDF2 time stepping:
$$\frac{3 V_i^{n+1} - 4 V_i^n + V_i^{n-1}}{2 \Delta t} = \sum_{f \in \partial\Omega_i} \sigma_{if} \dot{V}_f^{n+1}$$
the grid volume flux must use the exact BDF2 coefficients:
$$\dot{V}_f^{n+1} = \frac{3 \Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2 \Delta t}$$
This completely resolves Audit-H9 and prevents spurious mass/momentum sources on deforming 3D meshes.

---

### 4.7 Paper 7: CC-FVM Symmetry Plane Boundary Conditions (`Demirdzic-2022-...pdf`)
*Target Modules: `backend/meshing/polymesh_io.py`, `backend/meshing/difvm_topology.py`, `backend/core/case_generator.py`*

#### 4.7.1 Orthogonal Mirror Connection
Let $P$ be the cell center, $B$ the boundary face centroid, $\mathbf{s} = \mathbf{S}_f$ the outward face area normal vector, and $\mathbf{d} = \mathbf{x}_B - \mathbf{x}_P$.  
The mirror cell center $M$ is defined by:
$$\boldsymbol{\Delta} = \overrightarrow{PM} = 2 \frac{\mathbf{d} \cdot \mathbf{s}}{\mathbf{s} \cdot \mathbf{s}} \mathbf{s}$$
**Invariant:** $\boldsymbol{\Delta}$ is strictly collinear to $\mathbf{s}$ ($\boldsymbol{\Delta} \parallel \mathbf{s}$).  
Therefore, the non-orthogonal cross-diffusion vector is **identically zero**:
$$\mathbf{k}_f = \mathbf{s} - \frac{\mathbf{s}\cdot\mathbf{s}}{\boldsymbol{\Delta}\cdot\mathbf{s}}\boldsymbol{\Delta} \equiv \mathbf{0}$$
Face centroid skewness offset is:
$$\mathbf{t} = \mathbf{d} - \frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$$

#### 4.7.2 Householder Reflection Tensor $\mathbf{R}$
$$\mathbf{R} = \mathbf{I} - 2 \mathbf{n} \otimes \mathbf{n} = \mathbf{I} - 2 \frac{\mathbf{s} \otimes \mathbf{s}}{\|\mathbf{s}\|^2}$$
- **Vector Diffusion Flux Split (Velocity $\mathbf{v}$ / Displacement $\mathbf{u}_s$):**
  From $\mathbf{v}_M = \mathbf{R} \mathbf{v}_P$, the diffusion flux is:
  $$\mathbf{D}_v = -\Gamma_v \frac{\mathbf{s} \cdot \mathbf{v}_P}{\mathbf{d} \cdot \mathbf{s}} \mathbf{s}$$
  Unconditionally diagonally dominant Patankar split for component $i \in \{x, y, z\}$:
  $$D_{vi, \text{im}} = \Gamma_v \frac{s_i^2}{\mathbf{d} \cdot \mathbf{s}} > 0 \quad (\text{main diagonal } a_P), \qquad D_{vi, \text{ex}} = -\Gamma_v \frac{(\mathbf{s}\cdot\mathbf{v}_P - s_i v_{Pi}) s_i}{\mathbf{d}\cdot\mathbf{s}} \quad (\text{RHS } b_P)$$
- **Rank-2 Tensor Diffusion Flux Split (Stress $\mathbf{T}$ / Cauchy-Green $\mathbf{B}$):**
  From $\mathbf{T}_M = \mathbf{R} \mathbf{T}_P \mathbf{R}^T$, component-wise split for $T_{ij}$:
  $$D_{Tij, \text{im}} = \Gamma_T \frac{s_i^2 + s_j^2}{\mathbf{d} \cdot \mathbf{s}} > 0, \qquad D_{Tij, \text{ex}} = -\Gamma_T \frac{s_i(s_k T_{Pkj}) + s_j(s_k T_{Pik}) - \frac{2(s_p s_q T_{Ppq})s_i s_j}{\mathbf{s}\cdot\mathbf{s}} - (s_i^2 + s_j^2)T_{Pij}}{\mathbf{d}\cdot\mathbf{s}}$$

---

### 4.8 Paper 8: Monolithic Eulerian 3D FSI (`FSI_Monolithic_2505.22920v1.pdf`)
*Target Module: `backend/physics_modules/fsi_monolithic.py` & DiFVM Solvers*

#### 4.8.1 One-Continuum Formulation
The combined fluid-structure system is solved on a single fixed Eulerian grid:
$$\frac{\partial u_i}{\partial t} + \frac{\partial (u_i u_j)}{\partial x_j} = -\frac{1}{\rho}\frac{\partial p}{\partial x_i} + \frac{1}{\rho}\frac{\partial \sigma_{ij}}{\partial x_j} + g_i$$
with unified volume-averaged mixture variables ($\phi \in [0, 1]$ solid fraction):
$$\rho = \phi \rho^s + (1-\phi) \rho^f, \quad \sigma_{ij} = \phi \sigma_{ij}^s + (1-\phi) \sigma_{ij}^f$$
Solid Cauchy stress (Incompressible Neo-Hookean):
$$\sigma_{ij}^s = 2 \mu^s D_{ij} + G^s (B_{ij} - \delta_{ij}), \quad D_{ij} = \frac{1}{2}\left(\frac{\partial u_i}{\partial x_j} + \frac{\partial u_j}{\partial x_i}\right)$$

#### 4.8.2 Oldroyd-B Transport with WENO-Z
The left Cauchy-Green deformation tensor $\mathbf{B} = \mathbf{F} \mathbf{F}^T$ satisfies the upper-convected Oldroyd transport equation:
$$\frac{\partial B_{ij}}{\partial t} + u_k \frac{\partial B_{ij}}{\partial x_k} = B_{ik} \frac{\partial u_j}{\partial x_k} + B_{kj} \frac{\partial u_i}{\partial x_k}$$
Advection term $u_k \partial_{x_k} B_{ij}$ is discretized using 5th-order WENO-Z with Adams-Bashforth 2 (AB2) time integration.

#### 4.8.3 Geometric PLIC & Pure Fluid Deformation Reset
Interface plane in cut cells: $\mathbf{m} \cdot \mathbf{x} = \beta$ with $\mathbf{m} = \nabla \phi / \|\nabla \phi\|$.  
**Stability Reset:** In all fluid cells where solid fraction $\phi < \epsilon_c = 10^{-8}$:
$$\mathbf{B} \leftarrow \mathbf{I}_{3 \times 3}$$
This prevents spurious exponential growth of $\mathbf{B}$ in sheared fluid regions and preserves sharp interface boundaries on coarse grids ($128^3$).

---

### 4.9 Super-Elements Synthesis (`SUGGESTIONS_BACKEND.md` §10)
*Target Modules: `backend/meshing/native/src/octree_hexcore.cpp`, `backend/meshing/models.py`, `backend/meshing/native/src/difvm_mesh.cpp`*

#### 4.9.1 `constexpr` 23-Class Super-Elements
The $2^8 = 256$ outer node presence bitmasks of a cut hex cell reduce under octahedral rotation/reflection to 23 canonical classes. Each class is replaced by an ASP-optimized conforming sub-mesh built from 35 reference candidate nodes in $[-1, 1]^3$:
- 8 corners $(\pm 1, \pm 1, \pm 1)$
- 12 edge midpoints $(0, \pm 1, \pm 1), \dots$
- 6 face centers $(\pm 1, 0, 0), \dots$
- 1 body center $(0, 0, 0)$
- 8 octant support nodes $(\pm 0.5, \pm 0.5, \pm 0.5)$
Guarantees $\min SJ \ge 0.302$ and 0 hanging nodes/edges.

#### 4.9.2 Asymmetric Residual Volume Voting
To eliminate zigzag boundary staircasing:
$$R(e, D) = \left(RV_{\text{missing}}(e, D)\right)^2 + RV_{\text{excess}}(e, D), \quad D^*(e) = \arg\min_{D \in \{0,\dots,255\}} R(e, D)$$
evaluated over $6 \times 6 \times 6 = 216$ offline precomputed subdomains per cell. Each cell votes $v(e, N) \in \{+1, -1\}$ on its corner nodes; nodes with $\sum v > 0$ remain active, executing in linear time $\mathcal{O}(n_{\text{elem}})$.

#### 4.9.3 Top-Down Entity Mapping & Degrees of Freedom
1. Mesh-Faces $\to$ CAD-Surfaces (2 DOF: surface tangential smoothing)
2. Mesh-Edges $\to$ CAD-Curves (1 DOF: curve tangential sliding)
3. Mesh-Nodes $\to$ CAD-Corners (0 DOF: fixed in space)
4. Interior Nodes $\to$ Volume (3 DOF: unrestricted smoothing to maximize $J_{\text{ENS}}$)

#### 4.9.4 Unified Quality Metric $J_{\text{ENS}}$ & Fictitious Hex Pyramid Check
- Nodal Scaled Jacobian normalized by max edge cubed:
  $$J_{s,i} = \frac{\det([\mathbf{x}_1 - \mathbf{x}_i, \mathbf{x}_2 - \mathbf{x}_i, \mathbf{x}_3 - \mathbf{x}_i])}{(\max_k l_k)^3}$$
- Element Normalized Scaled Jacobian $J_{\text{ENS}} \in [-1, 1]$:
  $$J_{\text{ENS}} = \begin{cases} (1 + J_{S,\text{opt}}) - J_S & J_S > J_{S,\text{opt}} \\ J_S / J_{S,\text{opt}} & -J_{S,\text{opt}} \le J_S \le J_{S,\text{opt}} \\ -(1 + J_{S,\text{opt}}) - J_S & J_S < -J_{S,\text{opt}} \end{cases}$$
  where $J_{S,\text{opt}} = 1.0$ (Hex8), $\frac{\sqrt{3}}{2}$ (Prism6), $\frac{\sqrt{2}}{2}$ (Tet4, Pyr5).
- Fictitious hex pyramid check: Erect a fictitious hexahedron of height $\bar{l}_{\text{base}} = \frac{1}{4}\sum l_b$ over the 4-node pyramid base; include its 8 nodal SJs into the pyramid evaluation to detect warped/non-convex quadrilateral bases ($J_{\text{ENS}} \le 0$).

---

### 4.10 Carbon Architecture Synthesis & `.mphys` Container (`SUGGESTIONS_BACKEND.md` §11.2)
*Target Modules: `backend/native_rust/`, C-ABI Data Lake, `backend/core/`*

#### 4.10.1 C-ABI SoA Data Lake
Universal 64-byte-aligned C-ABI data lake descriptor:
```c
typedef struct {
    void*    ptr;        /* 64-byte aligned data pointer */
    size_t   len;        /* Element count */
    uint32_t dtype;      /* 0=f64, 1=f32, 2=i64, 3=i32, 4=u8 */
    uint32_t device_id;  /* 0=host_cpu, 1+=gpu_device */
} CAbiSpan;
```

#### 4.10.2 Chunked Binary Container `.mphys`
File Layout:
```
+-------------------------------------------------------------------------+
| Offset 0x00..0x40 (64 Bytes Header):                                    |
| Magic: 'MPHYS001' | Version (u16) | Endian (u16) | Footer Offset (u64)  |
| Mesh Epoch ID (u64) | Flags (u32) | Reserved (28 bytes)                 |
+-------------------------------------------------------------------------+
| Static Mesh Epoch 0:                                                    |
| Chunks of 65,536 cells ordered along 3D Hilbert / Morton curve          |
| Compressed with Blosc2 / Zstd (points, faces CSR, owner, neighbour, Sf) |
+-------------------------------------------------------------------------+
| Dynamic Field Step 0001..N:                                             |
| Chunks of 65,536 cells containing changed fields (p, U, T, B, phi)      |
| Compressed with Blosc2 / LZ4                                            |
+-------------------------------------------------------------------------+
| Spatial Chunk Index:                                                    |
| Morton interval + AABB [xmin, ymin, zmin, xmax, ymax, zmax] per chunk   |
+-------------------------------------------------------------------------+
| Footer (am Dateiende, Parquet-Prinzip):                                 |
| FlatBuffers schema, 7-element SI units [kg, m, s, K, mol, A, cd],       |
| Patch definitions, Chunk byte offsets, SHA-256 checksums,               |
| uint64_t footer_size, Closing Magic: 'MPHYSEOF'                         |
+-------------------------------------------------------------------------+
```
Provides $\mathcal{O}(1)$ open time ($< 1$ ms for 50 GB files) and crash-resilient append-only streaming.

---

## 5. Concrete Code Mapping & Test Specifications

| Paper / Architecture Section | Target Implementation File | Proposed New / Modified Classes & Functions | Targeted Tests to Write / Verify |
|---|---|---|---|
| **Paper 1 (Topology-First CAD)** | `backend/geometry/parsers/step_parser.py` | `StepTopologyParser`, `_dijkstra_loop_embedding()`, `_jordan_curve_separation()`, `_stitch_1d_boundary_curves()` | `tests/test_step_watertight_meshing.py`: Tests on `BladeFullRev1CFD.stp` and `TestCADSTPAP214.stp` confirming 0 unstitched edges, 0 self-intersections. |
| **Paper 2 (Differentiable CAD)** | `backend/geometry/differentiable_cad.py` | `CadComputationDAG`, `CadParametricNode`, `eval_cad_sensitivities()`, `eval_geometric_safety_constraints()` | `tests/test_differentiable_cad_dag.py`: Verifies analytical $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}}$ against finite differences and enforces $g_m \ge 0$. |
| **Paper 3 (Continuous $L^p$ Metric)** | `backend/meshing/mesh_engine.py`, `backend/meshing/native/src/octree_hexcore.cpp` | `compute_lp_metric_tensor()`, `reconstruct_p2_edge_midpoints()`, `OctreeHexcore::refineWithMetric()` | `tests/test_lp_metric_sizing.py`: Tests $p=1, 2$ sizing, verifying target cell count $N \pm 3\%$ without heuristic clamps. |
| **Paper 4 (Meshless Voronoi GPU)** | `backend/meshing/native/src/gpu_voronoi.cpp`, `gpu_voronoi.hpp` | `GpuVoronoiCell`, `clipByPlane()`, `compute_radius_of_security()`, `integrate_projection_tetrahedra()` | `tests/test_gpu_voronoi_meshless.py`: Unit test verifying Voronoi volumes and face areas against analytical polyhedra. |
| **Paper 5 (Differentiable Voronoi)** | `backend/meshing/diff_voronoi.py` | `DifferentiableVoronoi`, `compute_vertex_derivatives()`, `newton_cvt_smooth()` | `tests/test_differentiable_voronoi.py`: Validates $\frac{\partial \mathbf{x}_c}{\partial \mathbf{c}}$ and $C^1$ volume continuity across Delaunay flips. |
| **Paper 6 (Discrete GCL)** | `backend/meshing/moving_mesh.py` | `compute_simpson_swept_volume()`, `compute_bdf2_dgcl_face_flux()`, update `verify_dgcl()` | `tests/test_moving_mesh_dgcl.py`: Tests Simpson swept volume on rotating/deforming faces, confirming GCL residual $< 10^{-14}$. |
| **Paper 7 (Symmetry BC CC-FVM)** | `backend/meshing/polymesh_io.py`, `difvm_topology.py`, `case_generator.py` | `compute_householder_symmetry_split()`, `export_openfoam_polymesh()`, `generate_symmetry_plane_bc()` | `tests/test_symmetry_householder.py`: Verifies zero cross-diffusion ($\mathbf{k}_f \equiv \mathbf{0}$) and vector/tensor Patankar splits. |
| **Paper 8 (Monolithic Eulerian FSI)** | `backend/physics_modules/fsi_monolithic.py` | `EulerianMonolithicFSI`, `advect_cauchy_green_weno()`, `apply_fluid_reset()`, `plic_3d_flux()` | `tests/test_fsi_monolithic_eulerian.py`: Verifies $\mathbf{B} = \mathbf{I}$ reset for $\phi < 10^{-8}$ and Neo-Hookean solid stress. |
| **Section 10 (Super-Elements)** | `octree_hexcore.cpp`, `models.py`, `difvm_mesh.cpp` | `constexpr uint8_t SUPER_ELEMENT_TABLE[256]`, `asymmetric_residual_volume_voting()`, `compute_j_ens()` | `tests/test_super_elements_hexcore.py`: Checks all 23 cases satisfy $\min SJ \ge 0.302$, 0 hanging nodes, and pyramid base checks. |
| **Section 11.2 (Carbon Architecture)** | `backend/native_rust/`, C-ABI layer | `CAbiSpan`, `MPhysWriter`, `MPhysReader`, `RhaiExpressionEvaluator` | `tests/test_mphys_container.py`: Verifies 64-byte `MPHYS001` header, 65,536-cell chunk compression, and FlatBuffers footer round-trip. |

---

## 6. Mathematical Invariants & Review Governance Checkpoints

The 3-member review panel must verify these hard invariants:
1. **Discrete Geometric Conservation Law (DGCL):**
   $$\sum_{f \in \partial\Omega_i} \sigma_{if} \dot{V}_f^{n+1} = \frac{3 V_i^{n+1} - 4 V_i^n + V_i^{n-1}}{2 \Delta t} \pm 10^{-14} \text{ m}^3/\text{s}$$
2. **Symmetry Plane Cross-Diffusion Invariant:**
   $$\boldsymbol{\Delta} \times \mathbf{S}_f \equiv \mathbf{0} \implies \mathbf{k}_f \equiv \mathbf{0}$$
3. **Super-Elements Conformal Quality Bound:**
   $$\min_{e \in \text{SuperElements}} SJ(e) \ge 0.302, \quad \text{HangingNodes} \equiv 0, \quad \text{HangingEdges} \equiv 0$$
4. **Fluid Elastic Reset:**
   $$\forall \mathbf{x} \in \Omega \text{ with } \phi(\mathbf{x}) < 10^{-8} \implies \mathbf{B}(\mathbf{x}) \equiv \mathbf{I}_{3 \times 3}$$
5. **No Heuristic Clamps:**
   All sizing must proceed strictly from metric tensor sizing $\mathcal{M}_{L^p}$ or user configuration without arbitrary caps (`min(nx, 30)` strictly forbidden).
