# Scope: CFDSolver Multiphysics Generalization, Paper Integration & Backend Interface Contracts

## Architecture & System Overview
This scope executes the production-grade generalization of the CFD and multiphysics solver by completely excising specific SMR-Xu-Froment kinetics across documentation, contracts, and codebase (R1); directly integrating applicable methods from 8 surveyed 2026/recent papers (R2); implementing backend architecture synthesis and combinatorial super-elements from `SUGGESTIONS_BACKEND.md` (§10 & §11.2) (R3); delivering backend Fast IO extensions, JSON-RPC 2.0 / NDJSON IPC contracts, and 7-element SI dimensions validation (R4); and completing open Phase 5 audits (H5, H6, M10, H9) under strict hardware limits (`OMP_NUM_THREADS=2`, small grids $\le 30{,}000$ cells, sequential pytest) with a mandatory 3-agent 3:0 PASS review panel (Anti-Cheat & Forensics, Mathematics & Paper Fidelity, Code Quality & Stability).

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
| F51 | R1 SMR Excision & Generic Multiphysics Chemistry | Completely excise SMR-Xu-Froment across all 13 mapped files; replace `smrKineticsSource.C` with generic data-driven `genericReactionSource.C`; parameterize `SpeciesChemistryModule` in `registry.py` for arbitrary species; update handover log in `COMMUNICATION_BACKEND_FRONTEND.md`. | M5A | ORIGINAL_REQUEST R1 |
| F52 | Audit H5 Multi-Region OpenFOAM Export | In `polymesh_io.py`, implement multi-region sub-directory export for `chtMultiRegionFoam` (`constant/<region>/polyMesh`), generate `constant/regionProperties`, and support `mappedWall` and `cyclic` patch attributes. | M5A | Audit H5 |
| F53 | Audit H6 Runnable Case Generation & Dynamic Decomposition | In `case_generator.py` and `registry.py`, generate runnable initial `0/` fields, dynamic `processorWeights` for arbitrary rank counts ($N \ne 16$), and complete `SIMPLE` and `PIMPLE` solver dictionaries with relaxation factors. | M5A | Audit H6 |
| F54 | Audit M10 Hardened WSL Runner | In `backend/runner/wsl_runner.py`, apply `shlex.quote` to command arguments, verify process return codes, handle drive letters safely, and implement streaming residual/Courant number parsing. | M5A | Audit M10 |
| F55 | Paper 6 & Audit H9 Exact Swept Volume & BDF2-DGCL | In `moving_mesh.py`, implement exact 3D swept volume via Simpson's rule $\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n)\cdot(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ and BDF2-DGCL $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$; eliminate $0.85 \cdot d_{\max}$ boundary heuristic. | M5B | Paper 6, Audit H9 |
| F56 | Paper 7 CC-FVM Symmetry Boundary & Householder Tensor | In `polymesh_io.py`, `difvm_topology.py`, and `case_generator.py`, implement orthogonal symmetry plane mirror center $\boldsymbol{\Delta} \parallel \mathbf{s}$ with zero cross-diffusion ($\mathbf{k}_f \equiv \mathbf{0}$) and Householder reflection tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ for vectors and rank-2 tensors. | M5B | Paper 7 |
| F57 | Paper 1 Watertight B-Rep CAD Meshing | In `step_parser.py`, implement topology-first B-Rep meshing, Jordan curve loop separation (disk vs annulus), Dijkstra 3D loop embedding, and 1D parameter boundary curve stitching. | M5B | Paper 1 |
| F58 | Paper 2 Differentiable 3D CAD DAG & Chain Rule | In `differentiable_cad.py`, compile parametric CAD DAG $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$, geometric safety inequalities $g_m(\boldsymbol{\theta}) \ge 0$, and adjoint vector-Jacobian chain rule interfaces. | M5B | Paper 2 |
| F59 | Paper 3 Continuous Mesh $L^p$-Metric Sizing | In `mesh_engine.py` and `octree_hexcore.cpp`, implement optimal $L^p$-metric tensor sizing $\mathcal{M}_{L^p} = D_{L^p} \det(|H_u|)^{-\frac{1}{2p+3}} |H_u|$ ($p=1, 2$) and noise-free $P_2$ edge midpoint reconstruction without heuristic clamps. | M5B | Paper 3 |
| F60 | Papers 4 & 5 Dual GPU Voronoi & Analytical Derivatives | Implement pure dual convex 3D polyhedron representation (`float4 P[64]`, `int3 T[96]`), radius-of-security termination, and analytical vertex sensitivity $A\mathbf{x}_c = b$ ($C^1$ volumes across topology flips) in `difvm_topology.py` and `voronoi_derivatives.py`. | M5B | Papers 4 & 5 |
| F61 | Paper 8 Monolithic Eulerian 3D FSI | In `fsi_monolithic.py`, implement one-continuum mixture stress $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$, Neo-Hookean solid elasticity, 3D geometric PLIC, Oldroyd-B transport, and fluid reset $\mathbf{B} = \mathbf{I}$ for $\phi < 10^{-8}$. | M5B | Paper 8 |
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
| M5B | Paper Integration (Meshing, DGCL H9, Symmetry, FSI) | Features F55, F56, F57, F58, F59, F60, F61 | M4_PHASE4, M5A | IN_PROGRESS |
| M5C | Architecture Synthesis & Super-Elements Integration | Features F62, F63 | M5B | PLANNED |
| M5D | Backend Fast IO & JSON-RPC/NDJSON IPC Contracts | Features F64, F65 | M5A, M5C | PLANNED |
| M6_FINAL | Final System Acceptance & 3:0 Gate Certification | Feature F66 | M1–M5D | PLANNED |

---

## Interface Contracts

### 1. Generic Multiphysics Chemistry (`genericReactionSource.C` / `.H`)
```cpp
class genericReactionSource : public cellSetOption {
    wordList speciesNames_;
    List<scalarList> stoichiometricMatrix_; // [nReactions][nSpecies]
    List<scalar> arrheniusA_;
    List<scalar> arrheniusEa_;
    List<scalar> reactionDeltaH_; // [nReactions] (J/mol)
public:
    TypeName("genericReactionSource");
    virtual void addSup(fvMatrix<scalar>& eqn, const label fieldi);
    virtual void addSup(const volScalarField& rho, fvMatrix<scalar>& eqn, const label fieldi);
};
```

### 2. Multi-Region OpenFOAM Export (`polymesh_io.py`)
```python
def export_openfoam_multiregion_polymesh(
    case_path: Path,
    regions: Dict[str, Tuple[UnstructuredMesh, Dict[str, Any]]],
    region_properties: Optional[Dict[str, List[str]]] = None
) -> None:
    """Exports each region into constant/<region>/polyMesh and writes constant/regionProperties."""
```

### 3. DGCL Exact Swept Volume & BDF2 (`moving_mesh.py`)
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

### 4. Householder Symmetry Connection (`difvm_topology.py` / `polymesh_io.py`)
```python
def compute_symmetry_mirror_point(c_p: np.ndarray, c_f: np.ndarray, s_f: np.ndarray) -> np.ndarray:
    """Evaluates Delta = c_f + ((c_f - c_p) . s_f / (s_f . s_f)) * s_f ensuring Delta - c_p is collinear with s_f."""

def householder_reflection_tensor(n_f: np.ndarray) -> np.ndarray:
    """Evaluates Householder reflection tensor R = I - 2 * outer(n_f, n_f)."""
```

### 5. Fast IO Boundary Fields & Directory Listing (`foam_fast_io`)
```rust
#[derive(Debug, Clone)]
pub enum PatchFieldSource {
    ExplicitBoundaryField,
    OwnerCellFallback,
}

#[derive(Debug, Clone)]
pub struct PatchFieldValues {
    pub patch_type: String,
    pub source: PatchFieldSource,
    pub data: FieldData,
}

pub struct TimeStepInfo {
    pub time_value: f64,
    pub dir_name: String,
    pub is_reconstructed: bool,
    pub is_processor: bool,
    pub fields: Vec<String>,
}
```

---

## Code Layout & File Boundaries
- **Milestone M5A Worker owns:**
  - `backend/native_cpp/smr_catalysis/` (Excise `smrKineticsSource.C`, `.H`; create `genericReactionSource.C`, `.H`, update `Make/files`)
  - `backend/physics_modules/registry.py` (Parameterize `SpeciesChemistryModule`)
  - `backend/meshing/polymesh_io.py` (Multi-region `export_openfoam_multiregion_polymesh`, `regionProperties`, `mappedWall`, `cyclic`)
  - `backend/core/case_generator.py` (Generate runnable `0/` fields, dynamic `processorWeights`, `SIMPLE`/`PIMPLE` solver blocks)
  - `backend/runner/wsl_runner.py` (`shlex.quote`, returncode check, safe drive pathing, residual parsing)
  - Documentation updates: `COMMUNICATION_BACKEND_FRONTEND.md` (handover log), `FRONTEND_REQUIREMENTS_ON_BACKEND.md`, `SUGGESTIONS_BACKEND.md`, `backend/OPENFOAM_BACKEND_ATLAS.md`
  - Tests: `tests/test_generic_reaction_and_openfoam_audits.py`
- **Milestone M5B Worker owns:**
  - `backend/meshing/moving_mesh.py` (Simpson swept volume, BDF2-DGCL, eliminate `0.85 * max_d` heuristic)
  - `backend/meshing/difvm_topology.py` (Householder reflection tensor, collinear symmetry connection, Voronoi dual)
  - `backend/geometry/parsers/step_parser.py` (B-Rep watertight meshing, Jordan curve separation, 1D stitching)
  - `backend/geometry/differentiable_cad.py` (Compiled CAD DAG, safety inequalities, adjoint chain rule)
  - `backend/meshing/mesh_engine.py` & `backend/meshing/native/src/octree_hexcore.cpp` ($L^p$-metric sizing)
  - `backend/physics_modules/fsi_monolithic.py` (Monolithic Eulerian FSI, PLIC, Oldroyd-B, fluid reset)
- **Milestone M5C Worker owns:**
  - `backend/meshing/native/src/octree_hexcore.cpp`, `backend/models.py`, `backend/meshing/native/src/difvm_mesh.cpp` (23-class super-elements, asymmetric voting, $J_{\text{ENS}}$)
  - `backend/native_rust/` (C-ABI SoA `CAbiSpan`, Rhai/SIMD bytecode engine, `.mphys` container)
- **Milestone M5D Worker owns:**
  - `backend/native_rust/foam_fast_io/src/fields.rs`, `parser.rs`, `lib.rs` (boundaryField, list_time_dirs, binary format)
  - `backend/ipc/` (`json_rpc_server.py`, `ndjson_telemetry.py`, `binary_buffers.py`, `si_units.py`)
  - Integration tests for Fast IO & IPC
