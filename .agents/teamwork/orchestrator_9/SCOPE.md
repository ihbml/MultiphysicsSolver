# Scope: CFDSolver Multiphysics Generalization, Paper Integration & Backend Interface Contracts

## Architecture & System Overview
This scope executes the production-grade generalization of the CFD and multiphysics solver by completely excising specific SMR-Xu-Froment kinetics across documentation, contracts, and codebase (R1); directly integrating applicable methods from 8 surveyed 2026/recent papers (R2); implementing backend architecture synthesis and combinatorial super-elements from `SUGGESTIONS_BACKEND.md` (§10, §11.2, & §12) (R3); delivering backend Fast IO extensions, JSON-RPC 2.0 / NDJSON IPC contracts, and 7-element SI dimensions validation (R4); and completing open Phase 5 audits (H5, H6, M10, H9) under strict hardware limits (`OMP_NUM_THREADS=2`, small grids $\le 30{,}000$ cells, sequential pytest) with a mandatory 3-agent 3:0 PASS review panel (Anti-Cheat & Forensics, Mathematics & Paper Fidelity, Code Quality & Stability).

All frontend UI (`egui`), GPU renderers (`wgpu`), shaders, colormaps, viewports, and visual presentation logic are strictly out of scope and remain entirely owned by the frontend.

---

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F37 | K7, L4, L5 & Doku-Archiv | Reset README.md and ROADMAP.md to prototype status; relative doc links; UTF-8 requirements.txt; archive teamwork history. | M1_PHASE1 | Review K7, L4, L5, N1 |
| F38 | K6 Anti-Tautological Tests | Eliminate tautological test helper copies in `test_wp4_multiphysics_hexcore.py`. | M1_PHASE1 | Review K6 |
| F39 | M9 Mesh Validation & Fixture Guards | Bounding box containment, divergence theorem centroid balance, `@pytest.mark.skipif` guards. | M1_PHASE1 | Review M9 |
| F40 | H1 OpenMP Build & Native Loaders | `find_package(OpenMP REQUIRED)` in CMake; fail-fast native loaders with diagnostic logging. | M2_PHASE2 | Review H1 |
| F41 | K3 Polycube Shortcut Removal | Level-set meshing for non-box CAD shapes with true volume. | M3_PHASE3 | Review K3 |
| F42 | K4 Compatibility Shield Removal | Default `FarfieldPadding()` for external fluid. | M3_PHASE3 | Review K4 |
| F43 | K9 STEP Engine Default | Gmsh/OpenCASCADE primary engine for curved surface fidelity. | M3_PHASE3 | Review K9 |
| F44 | M6 & H10 Clamps and Overwrites Removal | Eliminate octree clamps ($10^{-15}$ volume overwrite, 500k-cell RAM downscaling, tuning magic numbers). | M3_PHASE3 | Review M6, H10 |
| F45 | H3 2D Centroid Formula Correction | 2D triangle centroids via $(2/3)c_f + (1/3)c_{\text{approx}}$ yielding exact $(1.4, 0.4)$ for quad $(0,0)-(4,0)-(1,1)-(0,1)$; planar 2D $z_0 \ne 0$ support. | M4_PHASE4 | Review H3 |
| F46 | H4 Explicit Polyhedron Topology Preservation | Eliminate `len(verts) == 8` guesswork; preserve explicit faces `mesh.faces` and owner/neighbour connectivity. | M4_PHASE4 | Review H4 |
| F47 | H2 Full Celik-GCI Implementation | Full Celik et al. (2008) formulation: convergence ratio $R$, divergence flag $R \ge 1$, apparent order $p$, effective ratio $r=(N_1/N_2)^{1/\text{dim}}$. | M4_PHASE4 | Review H2 |
| F48 | H7 Area-Conservative Supermesh-AMI | Sutherland-Hodgman polygon clipping overlap weights satisfying $\sum w \cdot A = A$. | M4_PHASE4 | Review H7 |
| F49 | H8 Physical SPH-FVM Coupling & Conservative P2M | True boundary area vectors $A_{iS}$; volume-weighted conservative P2M interpolation ($\sum V_c \phi_c = \sum V_p \phi_p$). | M4_PHASE4 | Review H8 |
| F50 | M1 & M2 Aspect Ratio & Relative wsrd_alpha | True aspect ratio (1.0 for unit cube); `wsrd_alpha` relative to background octree cell volume. | M4_PHASE4 | Review M1, M2 |
| F51 | R1 SMR Excision & Generic Multiphysics Chemistry | Completely excise SMR-Xu-Froment across all mapped files; replace `smrKineticsSource.C` with generic data-driven `genericReactionSource.C`; parameterize `SpeciesChemistryModule` in `registry.py` for arbitrary species; clean up remaining mention in `SUGGESTIONS_BACKEND.md:630`; update handover log in `COMMUNICATION_BACKEND_FRONTEND.md`. | M5A | ORIGINAL_REQUEST R1 |
| F52 | Audit H5 Multi-Region OpenFOAM Export | In `polymesh_io.py`, implement multi-region sub-directory export for `chtMultiRegionFoam` (`constant/<region>/polyMesh`), generate `constant/regionProperties`, and support `mappedWall` and `cyclic` patch attributes. | M5A | Audit H5 |
| F53 | Audit H6 Runnable Case Generation & Dynamic Decomposition | In `case_generator.py` and `registry.py`, generate runnable initial `0/` fields, dynamic `processorWeights` for arbitrary rank counts ($N \ne 16$), and complete `SIMPLE` and `PIMPLE` solver dictionaries with relaxation factors. | M5A | Audit H6 |
| F54 | Audit M10 Hardened WSL Runner | In `backend/runner/wsl_runner.py`, apply `shlex.quote` to command arguments, verify process return codes, handle drive letters safely, and implement streaming residual/Courant number parsing. | M5A | Audit M10 |
| F55 | Paper 6 & Audit H9 Exact Swept Volume & BDF2-DGCL | In `backend/meshing/moving_mesh.py`, implement exact 3D swept volume via Simpson's rule $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ and BDF2-DGCL $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$; eliminate $0.85 \cdot d_{\max}$ boundary heuristic. | M5B | Paper 6, Audit H9 |
| F56 | Paper 7 CC-FVM Symmetry Boundary & Householder Tensor | In `backend/meshing/polymesh_io.py`, `backend/meshing/difvm_topology.py`, and `backend/core/case_generator.py`, implement orthogonal symmetry plane mirror center $\boldsymbol{\Delta} \parallel \mathbf{s}$ with zero cross-diffusion ($\mathbf{k}_f \equiv \mathbf{0}$) and Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vectors and rank-2 tensors. | M5B | Paper 7 |
| F57 | Paper 1 Watertight B-Rep CAD Meshing | In `backend/geometry/parsers/step_parser.py`, implement topology-first B-Rep meshing, Jordan curve loop separation (disk vs annulus), Dijkstra 3D loop embedding, and 1D parameter boundary curve stitching. | M5B | Paper 1 |
| F58 | Paper 2 Differentiable 3D CAD DAG & Chain Rule | In `backend/geometry/differentiable_cad.py`, compile parametric CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$, geometric safety inequalities $g_m(\boldsymbol{\theta}) \ge 0$, and adjoint vector-Jacobian chain rule interfaces. | M5B | Paper 2 |
| F59 | Paper 3 Continuous Mesh $L^p$-Metric Sizing | In `backend/meshing/mesh_engine.py` and `backend/meshing/native/src/octree_hexcore.cpp`, implement optimal $L^p$-metric tensor sizing $\mathcal{M}_{L^p} = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ ($p=1, 2$) and noise-free $P_2$ edge midpoint reconstruction without heuristic clamps. | M5B | Paper 3 |
| F60 | Papers 4 & 5 Dual GPU Voronoi & Analytical Derivatives | Implement pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`), radius-of-security termination, and analytical vertex sensitivity $A\mathbf{x}_c = b$ ($C^1$ volumes across topology flips) in `backend/meshing/gpu_voronoi.py` / `backend/meshing/difvm_topology.py`. | M5B | Papers 4 & 5 |
| F61 | Paper 8 Monolithic Eulerian 3D FSI | In `backend/physics_modules/monolithic_fsi.py`, implement one-continuum mixture stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$, Neo-Hookean solid elasticity, 3D geometric PLIC, Oldroyd-B transport, and fluid reset $\mathbf{B} = \mathbf{I}$ for $\phi < 10^{-8}$. | M5B | Paper 8 |
| F61b | §12.2 FVM/GPU Numerics & HPC Contracts | In meshing/FVM modules: linear-exact plane intersection $\beta_f = \frac{(\mathbf{x}_f - \mathbf{x}_P)\cdot\mathbf{S}_f}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\alpha_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{d}_f\cdot\mathbf{S}_f}$, $\mathbf{m}_f^{\text{skew}}$, $\mathbf{k}_f^{\text{corr}}$, explicit `FaceClass::invalid` rejection for $\cos\theta_f \le 10^{-8}$, M-matrix deferred correction, atomic-free `CellFaceCSR` gather, compact 6% `MeshCorrectionSoA`, 3-Map `MeshErrorMap` & SPD Cholesky factorization $\mathcal{M} = \mathbf{L}\mathbf{L}^T + \epsilon\mathbf{I} \succ 0$, tangential CAD projection + 1-step Newton re-projection. Clean up `smrKineticsSource.C` in `SUGGESTIONS_BACKEND.md:630`. | M5B | Suggestions §12.2, Snippets 12–14 |
| F62 | Section 10 Combinatorial 23-Class Super-Elements | In `octree_hexcore.cpp`, `models.py`, `difvm_mesh.cpp`, implement `constexpr` 23-class ASP-optimized super-elements ($\min SJ \ge 0.302$), asymmetric residual volume voting ($RV_{\text{missing}}^2 + RV_{\text{excess}}$), top-down DOF hierarchy, and $J_{\text{ENS}}$ / pyramid base check. | M5C | Suggestions §10 |
| F63 | Section 11.2 Architecture Synthesis & .mphys Container | In `backend/native_rust/`, establish C-ABI Structure-of-Arrays (SoA) data lake interfaces `CAbiSpan`, embedded Rhai/SIMD bytecode expression engine, native chunked binary container `.mphys` (64-byte `MPHYS001` header, 65,536-cell Hilbert chunks with Blosc2/Zstd/LZ4, FlatBuffers footer `MPHYSEOF`), and zero-copy buffer export. | M5C | Suggestions §11.2 |
| F64 | Fast IO Extensions (boundaryField, list_time_dirs, binary) | In `backend/native_rust/foam_fast_io`, implement `boundaryField` parsing in `src/fields.rs` (`pub boundary_fields: HashMap<String, PatchFieldValues>`) with uniform, nonuniform, and owner-cell fallback for `zeroGradient`/`calculated`; implement `list_time_dirs` with sorted numeric time directories and reconstructed vs processor status; implement `format binary;` header support in `FoamStream`. | M5D | ORIGINAL_REQUEST R4 |
| F65 | Backend JSON-RPC 2.0 / NDJSON IPC Contracts & Telemetry | Implement JSON-RPC 2.0 / NDJSON IPC backend service covering all 16 endpoints (`system.hello`, `schema.get`, `geometry.import`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`/`case.generate`, `solver.*`, `results.*`); strict 7-element SI dimensions validation `[kg, m, s, K, mol, A, cd]`; Paket 1 binary geometry buffers; Paket 2 binary mesh quality fields; Paket 4 live NDJSON telemetry streaming (`iter` events with Courant numbers and NaN monitors); verify zero frontend UI/rendering code. | M5D | ORIGINAL_REQUEST R4 |
| F66 | Full System Acceptance & Final Certification | Sequential pytest regression execution under `OMP_NUM_THREADS=2`, grids $\le 30{,}000$ cells, and unanimous 3:0 PASS gate sign-off across Anti-Cheat, Math Critic, and Code Quality. | M6_FINAL | Review Panel Governance |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1_PHASE1 | Ehrlichkeit, Test-Integrität & Doku-Archiv | Features F37, F38, F39 | none | DONE (3:0 UNANIMOUS PASS) |
| M2_PHASE2 | Build & Plattform (OpenMP) | Feature F40 | none | DONE (3:0 UNANIMOUS PASS) |
| M3_PHASE3 | Abkürzungen & Heuristiken entfernen | Features F41, F42, F43, F44 | M1_PHASE1, M2_PHASE2 | DONE (3:0 UNANIMOUS PASS) |
| M4_PHASE4 | Mathematik & Topologie korrigieren | Features F45, F46, F47, F48, F49, F50 | M2_PHASE2, M3_PHASE3 | DONE (3:0 UNANIMOUS PASS) |
| M5A | SMR Excision, Multiphysics Chemistry & OpenFOAM Audits (H5, H6, M10) | Features F51, F52, F53, F54 | M1_PHASE1, M4_PHASE4 | DONE (3:0 UNANIMOUS PASS) |
| M5B | Paper Integration (Meshing, DGCL H9, Symmetry, FSI) | Features F55, F56, F57, F58, F59, F60, F61, F61b | M4_PHASE4, M5A | DONE (3:0 UNANIMOUS PASS) |
| M5C | Architecture Synthesis & Super-Elements Integration | Features F62, F63 | M5B | DONE (3:0 UNANIMOUS PASS) |
| M5D | Backend Fast IO & JSON-RPC/NDJSON IPC Contracts | Features F64, F65 | M5A, M5C | PLANNED |
| M6_FINAL | Full System Acceptance & Final Certification | Feature F66 | M1–M5D | PLANNED (PAUSE POINT: Wait for User Commit/Push after M5D) |

---

## Interface Contracts

### 1. DGCL Exact Swept Volume & BDF2 (`moving_mesh.py`)
```python
def compute_face_swept_volume_simpson(
    x_n: np.ndarray, x_n1: np.ndarray,
    S_n: np.ndarray, S_n_half: np.ndarray, S_n1: np.ndarray
) -> np.ndarray:
    """Evaluates exact 3D swept volume via Simpson's rule: Delta V_f = (1/6) (x_n1 - x_n) . (S_n + 4 S_n_half + S_n1)."""

def compute_dgcl_grid_flux_bdf2(
    delta_V_n_n1: np.ndarray, delta_V_nm1_n: np.ndarray, dt: float
) -> np.ndarray:
    """Evaluates BDF2-DGCL grid flux: dot{V}_f^{n+1} = (3 delta_V_n_n1 - delta_V_nm1_n) / (2 dt)."""
```

### 2. Householder Symmetry Connection (`difvm_topology.py` / `polymesh_io.py`)
```python
def compute_symmetry_mirror_point(c_p: np.ndarray, c_f: np.ndarray, s_f: np.ndarray) -> np.ndarray:
    """Evaluates Delta = c_f + ((c_f - c_p) . s_f / (s_f . s_f)) * s_f ensuring Delta - c_p is collinear with s_f."""

def householder_reflection_tensor(n_f: np.ndarray) -> np.ndarray:
    """Evaluates Householder reflection tensor R = I - 2 * outer(n_f, n_f)."""
```

### 3. Linear-Exact Face Geometry & Invalid Rejection (`face_geometry` / `difvm_topology.py`)
```python
def compute_linear_exact_face_geometry(
    xp: np.ndarray, xn: np.ndarray, xf: np.ndarray, area: np.ndarray, h_ref: float
) -> Dict[str, Any]:
    """Computes beta_f = (xf - xp).area / (d.area), alpha_f = ||area||^2 / (d.area),
    skew = xf - (xp + beta_f * d), correction = area - alpha_f * d.
    Rejects with FaceClass.INVALID if cosine <= 1e-8 or not finite."""
```

### 4. Differentiable CAD DAG & Safety Inequalities (`differentiable_cad.py`)
```python
class DifferentiableCADDAG:
    def forward(self, theta: np.ndarray) -> np.ndarray: ...
    def vector_jacobian_product(self, v: np.ndarray) -> np.ndarray: ...
    def evaluate_safety_inequalities(self, theta: np.ndarray) -> np.ndarray: ...
```

### 5. Monolithic Eulerian FSI (`monolithic_fsi.py`)
```python
def compute_mixture_stress(phi: np.ndarray, sigma_s: np.ndarray, sigma_f: np.ndarray) -> np.ndarray:
    """sigma_ij = phi * sigma_s + (1 - phi) * sigma_f."""

def reset_fluid_left_cauchy_green(B: np.ndarray, phi: np.ndarray, threshold: float = 1e-8) -> np.ndarray:
    """Resets B = I where solid volume fraction phi < threshold."""
```

---

## Code Layout & File Boundaries for Milestone M5B
- Worker owns exclusively:
  - `backend/meshing/moving_mesh.py`
  - `backend/meshing/difvm_topology.py`
  - `backend/meshing/polymesh_io.py`
  - `backend/core/case_generator.py`
  - `backend/geometry/parsers/step_parser.py`
  - `backend/geometry/differentiable_cad.py`
  - `backend/meshing/mesh_engine.py`
  - `backend/meshing/gpu_voronoi.py` (or `backend/meshing/voronoi_derivatives.py`)
  - `backend/physics_modules/monolithic_fsi.py`
  - `SUGGESTIONS_BACKEND.md` (cleaning line 630 mention of `smrKineticsSource.C`)
  - `tests/test_paper_integration_m5b.py` (comprehensive verification test suite)
