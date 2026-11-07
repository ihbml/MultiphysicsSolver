# Project: Industrial In-Memory Multiphysics Mesher Backbone — Audit Remediation & WSL Pipeline

## Architecture & Data Flow
This phase resolves 6 critical mathematical and paper-fidelity audit defects (R1–R6) and 4 static audit issues (R7) across C++ and Python meshing backend, recompiles the native DLL (R11/R13), validates against CAD benchmarks (R8), builds the automated WSL OpenFOAM `checkMesh` and `simpleFoam` acceptance pipeline (R10), and achieves 100% unanimous approval (3:0 APPROVE) by an independent 3-agent audit committee (R12).

### Architecture Highlights
1. **Octree Hexcore Snapping & Geometry (R1, R2, R3.1-2)**:
   - Positively oriented corner tetrahedron AMIPS evaluation eliminates collinear tet degeneracy ($\det J = dx \cdot dy \cdot dz > 0$, finite $E_{\text{AMIPS}}$).
   - Area-weighted triangle fan for polygon face normals & centroids (`mf.centre`, `mf.normal_owner`), detecting inverted sub-triangles (`mf.is_folded`).
   - Local reference point $c_{\text{approx}}$ and pyramid decomposition for cell volume and centroid integration, strictly translation-invariant: $[0, 1]^3 \to (0.5, 0.5, 0.5)$ and $[10, 11]^3 \to (10.5, 10.5, 10.5)$.
   - Small-cell agglomeration assigns $cell\_types = POLYHEDRON (42)$, unions vertex sets, merges and deduplicates internal faces sharing $(owner, neighbour)$, and enforces upper-triangular ordering $owner < neighbour$.
   - Cell weighting $\omega_{\text{cell}}[c] = \max(0.1, \min_{f \in c} \beta_f)$ across adjacent faces.
2. **Python Meshing Backend & Static Fixes (R3.3, R4, R5, R6, R7)**:
   - Dynamic Berger & Giuliani (2024) WSRD weights using `config.small_cell_ratio` and `config.wsrd_target_alpha`, stored in `mesh.error_map` and `mesh.metadata`.
   - Corrected `is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")` dispatch branch in `mesh_engine.py:126-130`.
   - DistMesh point filtering $p = p[d < -0.05 \cdot h]$, 3 Newton boundary projection steps, and orthogonal boundary segment projection in `distmesh.cpp`.
   - ASME V&V 20 GCI scaling with $1.0 / \max(1, \text{mesh.topological\_dim})$, apparent order $p$ clamped to $[0.5, 10.0]$ with oscillatory convergence guard.
   - Fast Winding Number Barnes-Hut AABB tree dipole approximation for nodes with $\text{dist} > 2.0 \cdot R_K$, reducing complexity to $O(N \log T)$.
   - Bounding box check tolerance $10^{-5} \cdot \text{diag}$ in `sdf_engine.cpp:479-484`.
   - Resolved attribute lookups in `difvm_topology.py:778-784`, `strict_native` parameter and `CFD_STRICT_NATIVE=1` in `mesh_engine.py`, configurable `patch_types` in `polymesh_io.py`, and closed-body outward normals and node ordering in `inflation.py`.
3. **WSL OpenFOAM Acceptance Pipeline (R8, R10)**:
   - Automated non-interactive script `scripts/run_wsl_checkmesh.sh` exporting mesh via `export_openfoam_polymesh` and invoking `checkMesh` in WSL.
   - Verification of face non-orthogonality, skewness, aspect ratios, and zero negative volume faces.
   - `simpleFoam` comparison against `snappyHexMesh` baseline cases.
4. **Independent 3-Agent Audit Committee (R12)**:
   - Agent 1: Paper-Fidelity & Zero-Shortcut Challenger (`teamwork_challenger`).
   - Agent 2: Code & Software-Architecture Auditor (`teamwork_auditor`).
   - Agent 3: Mathematics & Numerics Auditor (`teamwork_critic` / `teamwork_auditor`).
   - 100% unanimous approval (3:0 APPROVE) strictly required.

---

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---|---|---|---|
| F26 | R1 Cut-Cell Snapping & AMIPS Metric Fix | Corner tetrahedron construction from corner index $c\_idx \in 0..7$ and 3 adjacent edge neighbors ($\det J = dx \cdot dy \cdot dz > 0$, finite AMIPS); positive sub-pyramid volume check; sphere snapping test in `tests/test_wp4_multiphysics_hexcore.py`. | M1 | ORIGINAL_REQUEST R1 |
| F27 | R2 Translation-Invariant Centroids, Normals & Cell Volumes | Triangle-fan integration for polygon centroids & normals, `mf.is_folded` flag; local reference point $c_{\text{approx}}$ and pyramid decomposition for cell volume/centroid integration. | M1 | ORIGINAL_REQUEST R2 |
| F28 | R3.1-2 Small-Cell Agglomeration & Cell Weighting | `cell_types = POLYHEDRON (42)`, vertex union, deduplication of internal faces sharing $(owner, neighbour)$ with $owner < neighbour$; $\omega_{\text{cell}}[c] = \max(0.1, \min_{f \in c} \beta_f)$. | M1 | ORIGINAL_REQUEST R3.1-2 |
| F29 | R3.3 Dynamic Berger-Giuliani WSRD Weights | Dynamic `config.small_cell_ratio` and `config.wsrd_target_alpha` per Berger & Giuliani (2024) Eq. 4.2 in `mesh_engine.py` and `difvm_topology.py`, writing to `mesh.error_map["wsrd_weights"]` and `mesh.metadata["wsrd_weights"]`. | M2 | ORIGINAL_REQUEST R3.3 |
| F30 | R4 Solid-Only Domain Mode Dispatch Fix | `is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")` in `mesh_engine.py:126-130`, calling `mesh_hexcore` when `domain_mode != "solid_only"`. | M2 | ORIGINAL_REQUEST R4 |
| F31 | R5 DistMesh Boundary Filtering, Projection & Cell Filtering | `distmesh_core.py:734` filtering $p = p[d < -0.05 \cdot h]$; up to 3 Newton projection steps in `distmesh_core.py:827-839`; orthogonal segment projection in `distmesh.cpp:734-739`; 3D cell filtering vs surface triangles in `distmesh.cpp:239-388`. | M1, M2 | ORIGINAL_REQUEST R5 |
| F32 | R6 GCI Scaling, SDF BBox Tolerance & Barnes-Hut Winding Number | GCI scaling with $1.0 / \max(1, \text{mesh.topological\_dim})$, $p$ clamped to $[0.5, 10.0]$ with oscillatory guard; outside bbox tolerance $10^{-5} \cdot \text{diag}$ in `sdf_engine.cpp:479-484`; Barnes-Hut dipole approximation for $\text{dist} > 2.0 \cdot R_K$. | M2 | ORIGINAL_REQUEST R6 |
| F33 | R7 Static Audit Detail Fixes | `difvm_topology.py:778-784` attribute lookups; `MeshEngine` `strict_native` parameter and `CFD_STRICT_NATIVE=1` check; `polymesh_io.py:150-163` configurable `patch_types`; `inflation.py:104` consistent outward normals on closed bodies and node order reversal $f\_arr[::-1]$. | M2 | ORIGINAL_REQUEST R7 |
| F34 | R8 & R10 Benchmark CAD Testing & WSL Acceptance Pipeline | Meshing benchmark CAD models (`TestCAD.stp`, `BladeFullRev1CFD.stp`, `snappyMultiRegionHeater`, `motorBike`); automated WSL OpenFOAM `checkMesh` and `simpleFoam` comparison pipeline script `scripts/run_wsl_checkmesh.sh`. | M4 | ORIGINAL_REQUEST R8, R10 |
| F35 | R11 & R13 Native DLL Compilation, Safety & Regression Baseline | Clean CMake compilation of `cfd_mesher_native.dll` with `--parallel 8`; all 615 tests passing sequentially with `OMP_NUM_THREADS=8`. | M3 | ORIGINAL_REQUEST R11, R13 |
| F36 | R12 Independent 3-Agent Audit Committee Gate & Git Commit | Unanimous 3:0 APPROVE certification by Challenger, Code Auditor, and Math Auditor; git commit of completed fixes and pipeline. | M5 | ORIGINAL_REQUEST R12 |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|---|---|---|---|
| M1 | Native C++ Hexcore Math & Geometry Core | Features F26, F27, F28, F31 (native part) (`octree_hexcore.cpp`, `distmesh.cpp`, `tests/test_wp4_multiphysics_hexcore.py`) | none | DONE |
| M2 | Python Backend, Sizing, GCI, DistMesh & Static Fixes | Features F29, F30, F31 (Python part), F32, F33 (`mesh_engine.py`, `difvm_topology.py`, `distmesh_core.py`, `gci.py`, `polymesh_io.py`, `inflation.py`, `geometry_sdf.py`, `sdf_engine.cpp`) | none | DONE |
| M3 | Native DLL Recompilation, Strict Native & Regression Testing | Feature F35 (`CMakeLists.txt`, `cfd_mesher_native.dll`, 615 tests sequential run) | M1, M2 | PLANNED (PAUSED per user directive) |
| M4 | CAD Benchmark Validation & Automated WSL OpenFOAM Pipeline | Feature F34 (`scripts/run_wsl_checkmesh.sh`, CAD benchmark tests) | M3 | PLANNED (PAUSED per user directive) |
| M5 | Independent 3-Agent Audit Committee Gate Certification & Git Commit | Feature F36 (Challenger, Code Auditor, Math Auditor, git commit) | M4 | PLANNED (PAUSED per user directive) |

> **User Pause Directive (2026-11-07T02:14:54Z)**:
> Upon completing and verifying Milestone M2 (DistMesh, GCI, SDF-BBox, static audit/API fixes), pause execution and stop cleanly to conserve tokens. Do not advance to subsequent milestones (M3–M5) until directed. Commit all verified changes to git and report final status.

---

## Interface Contracts

### `backend/meshing/native/include/octree_hexcore.hpp`
```cpp
// Triangle-fan centroid and normal calculation
struct MosaicFace {
  std::vector<int64_t> nodes;
  int64_t owner{-1};
  int64_t neighbour{-1};
  Vec3 normal_owner{0, 0, 0};
  Vec3 centre{0, 0, 0};
  std::string patch_name{"internal"};
  bool is_folded{false};
};
```

### `backend/meshing/mesh_engine.py`
```cpp
class MeshEngine:
  def __init__(self, ..., strict_native: bool = False):
    ...
```

---

## Code Layout & Exclusive File Ownership
- **Milestone M1 Worker owns:**
  - `backend/meshing/native/src/octree_hexcore.cpp`
  - `backend/meshing/native/src/distmesh.cpp`
  - `backend/meshing/native/include/octree_hexcore.hpp`
  - `tests/test_wp4_multiphysics_hexcore.py` (sphere snapping test)
- **Milestone M2 Worker owns:**
  - `backend/meshing/mesh_engine.py`
  - `backend/meshing/difvm_topology.py`
  - `backend/meshing/distmesh_core.py`
  - `backend/meshing/gci.py`
  - `backend/meshing/polymesh_io.py`
  - `backend/meshing/inflation.py`
  - `backend/meshing/geometry_sdf.py`
  - `backend/meshing/native/src/sdf_engine.cpp`
- **Milestone M3 Worker owns:**
  - `backend/meshing/native/CMakeLists.txt`
  - `backend/meshing/cfd_mesher_native.dll`
- **Milestone M4 Worker owns:**
  - `scripts/run_wsl_checkmesh.sh`
  - `tests/test_wsl_openfoam_acceptance.py`

---

## Mandatory Gate & Audit Rules
1. **Unanimous 3-Agent Audit Committee**: 100% UNANIMOUS approval (3:0 APPROVE) across Challenger, Code Auditor, and Math Auditor.
2. **Zero-Shortcut Policy**: No magic heuristics, no silent clamps, no bare excepts.
3. **Operational Concurrency & Memory Safety**: `OMP_NUM_THREADS=8`, CMake `--parallel 8`, sequential pytest, initial test mesh size $\le 30{,}000$ cells.
4. **Physical Invariants**: Strict $V_i > 0$, relative Gauss closure $< 10^{-11}$, projection collinearity $< 10^{-12}$.
5. **Regression Baseline**: All 615 existing tests must remain 100% green.
