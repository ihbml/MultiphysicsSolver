# Scope: CFDSolver Audit Findings Remediation (5 Phases)

## Architecture & System Overview
This project resolves and certifies all uncorrected audit findings (points 9.1 through 9.4) from `MultiphysicsSolver_Code_Review.md` across 5 sequential phases under strict low-resource constraints (`OMP_NUM_THREADS=2`, grids $\le 30{,}000$ cells, sequential pytest) with unanimous 3-agent gate approval (Anti-Cheat / Code Cleanliness, Mathematical Fidelity / Paper Conformance, Code Quality / Stability).

---

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F37 | K7, L4, L5 & Doc Bundling | Reset README.md and ROADMAP.md to actual prototype status; convert absolute file:///G:/ links to relative paths; convert requirements.txt from UTF-16LE to UTF-8 without BOM; bundle `.agents/teamwork/` history and `TEST_READY.md` into `docs/TEAMWORK_DEVELOPMENT_HISTORY.md` while leaving `SUGGESTIONS_*.md` and `COMMUNICATION_*.md` untouched in root. | M1_PHASE1 | Review K7, L4, L5, N1 |
| F38 | K6 Anti-Tautological Tests | Replace tautological helper copies in `tests/test_wp4_multiphysics_hexcore.py:96-234` (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`) with direct imports from canonical `backend/` modules. Replace tautological assertions with real backend mesh evaluations. | M1_PHASE1 | Review K6 |
| F39 | M9 Mesh Validation & Fixture Guards | In `models.py` (`UnstructuredMesh.validate()`), implement cell bounding box containment check ($C_i \in \text{bbox}_i$) and divergence theorem global centroid balance $\sum V_i \mathbf{C}_i \approx \frac{1}{d+1} \sum \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f)$. Update `pytest.ini` (`testpaths = tests backend/geometry/tests`) and add `@pytest.mark.skipif` guards for missing external NX/OpenFOAM tutorial fixtures. Whitelist `*.obj` in `resources/CADTestSamples/` in `.gitignore`. | M1_PHASE1 | Review M9 |
| F40 | H1 OpenMP Build & Native Loaders | Add `find_package(OpenMP REQUIRED)` and link `OpenMP::OpenMP_CXX` in `backend/meshing/native/CMakeLists.txt`. Ensure native library loader in Python (`difvm_topology.py`, `mesh_engine.py`) fails fast with clear diagnostic logging when strict native mode is enabled instead of silently falling back to Python. | M2_PHASE2 | Review H1 |
| F41 | K3 Polycube Shortcut Removal | Remove polycube bounding-box shortcut in `mesh_engine.py:231-284` (`_detect_oriented_box_axes`) or enforce surface volume vs bounding box volume check ($|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$) so non-box shapes like L- and U-profiles are meshed by general level-set pipeline with true volume (~3.0). | M3_PHASE3 | Review K3 |
| F42 | K4 Compatibility Shield Removal | Remove backward compatibility shield in `mesh_engine.py:121-131` that forces solid meshing when `external_fluid` is requested without farfield parameters; default to `FarfieldPadding()` and update affected legacy tests to `domain_mode="solid_only"`. | M3_PHASE3 | Review K4 |
| F43 | K9 STEP Engine Default | Set Gmsh/OpenCASCADE as primary engine in `backend/geometry/parsers/step_parser.py:84-95` (`engine="auto"`) so curved surfaces like `BladeFullRev1CFD.stp` preserve full surface area and curvature without flattening. | M3_PHASE3 | Review K9 |
| F44 | M6 & H10 Clamps and Overwrites Removal | Eliminate `max_level <= 4` clamp and $10^{-15}$ silent background volume overwrite in `octree_hexcore.cpp`. Eliminate 500k-cell RAM downscaling cap and `0.11 * h_min`, `h0/3` tuning magic numbers in `mesh_engine.py`. Raise explicit exceptions on resolution limits instead of silently coarsening. | M3_PHASE3 | Review M6, H10 |
| F45 | H3 2D Centroid Formula Correction | In `difvm_topology.py:1244` and `difvm_mesh.cpp:454`, change 2D triangle weights from 3D pyramid weights ($0.75/0.25$) to 2D triangle weights ($(2/3) c_f + (1/3) c_{\text{approx}}$), yielding exact $(1.4, 0.4)$ for quadrilateral $(0,0)-(4,0)-(1,1)-(0,1)$. | M4_PHASE4 | Review H3 |
| F46 | H4 Explicit Polyhedron Topology Preservation | In `moving_mesh.py`, `difvm_topology.py`, and `difvm_mesh.cpp`, eliminate `len(verts) == 8` guessing and preserve explicit face lists `mesh.faces` and owner/neighbour connectivity so polyhedra in octree meshes retain volume and topology during mesh motion. | M4_PHASE4 | Review H4 |
| F47 | H2 Full Celik-GCI Implementation | In `backend/meshing/gci.py`, implement full Celik et al. (2008) formulation: convergence ratio $R = \epsilon_{21}/\epsilon_{32}$, divergence flag for $R \ge 1$, iterative apparent order $p$ with transcendental $q(p)$, effective refinement ratio $r = (N_1/N_2)^{1/\text{dim}}$, and eliminate artificial clamps. | M4_PHASE4 | Review H2 |
| F48 | H7 Area-Conservative Supermesh-AMI | In `difvm_topology.py:1788-1815`, replace inverse-distance weighting with area-conservative polygon overlap weights (Supermesh AMI) satisfying $\sum w \cdot A = A$. | M4_PHASE4 | Review H7 |
| F49 | H8 Physical SPH-FVM Coupling & Conservative P2M | In `backend/meshing/coupling.py`, compute boundary area vectors $A_{iS}$ from actual FVM boundary geometry instead of tautological definition; implement mass-conservative P2M interpolation using cell volume weighting. | M4_PHASE4 | Review H8 |
| F50 | M1 & M2 Aspect Ratio & Relative wsrd_alpha | Correct aspect ratio definition in `difvm_topology.py`, `difvm_mesh.cpp`, and `octree_hexcore.cpp` to use edge lengths or OpenFOAM bounding box definition (yielding 1.0 for unit cube). Redefine `wsrd_alpha` as cut-cell volume relative to background octree cell volume rather than global median volume. | M4_PHASE4 | Review M1, M2 |
| F51 | K8 Reversible Xu-Froment SMR Kinetics | In `backend/native_cpp/smr_catalysis/smrKineticsSource.C`, implement stoichiometrically coupled reaction rates for species (CH4, H2O, H2, CO, CO2) and consistent enthalpy sink $q = -r \cdot \Delta H(T)$ with reversible Xu-Froment kinetics. Properly evaluate `fieldi` in `addSup` so enthalpy sinks are not erroneously added to multiple fields. | M5_PHASE5 | Review K8 |
| F52 | H5 & H6 Valid Multi-Region OpenFOAM Export | In `polymesh_io.py` and `case_generator.py`, generate valid multi-region exports for `chtMultiRegionFoam` including `regionProperties`, correct patch types (`patch` for farfield, `wall` for body, `mappedWall` with required keys), and complete `0/` and `fvSolution` files. | M5_PHASE5 | Review H5, H6 |
| F53 | M10 Hardened WSL Runner | In `backend/runner/wsl_runner.py`, apply `shlex.quote` to command arguments, verify process return codes, and handle paths without drive letters safely. | M5_PHASE5 | Review M10 |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1_PHASE1 | Ehrlichkeit, Test-Integrität & Doku-Archiv | Features F37, F38, F39 | none | DONE |
| M2_PHASE2 | Build & Plattform (OpenMP) | Feature F40 | none | DONE |
| M3_PHASE3 | Abkürzungen & Heuristiken entfernen | Features F41, F42, F43, F44 | M1_PHASE1, M2_PHASE2 | DONE |
| M4_PHASE4 | Mathematik & Topologie korrigieren | Features F45, F46, F47, F48, F49, F50 | M2_PHASE2, M3_PHASE3 | IN_PROGRESS |
| M5_PHASE5 | Physik & OpenFOAM-Schnittstelle | Features F51, F52, F53 | M1_PHASE1, M4_PHASE4 | PLANNED |
| M6_FINAL | Final System Acceptance & Gate Certification | Full regression & 3:0 Unanimous Gate Approval | M1–M5 | PLANNED |

---

## Code Layout & File Ownership per Milestone
- **Milestone M1_PHASE1 Worker owns:**
  - `README.md`, `ROADMAP.md`, `requirements.txt`, `pytest.ini`, `.gitignore`
  - `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`
  - `tests/test_wp4_multiphysics_hexcore.py` (canonical helper imports and anti-tautology tests)
  - `models.py` (`UnstructuredMesh.validate()` bounding box & global centroid checks)
  - `backend/geometry/tests/test_bridge.py` (skipif for NX)
- **Milestone M2_PHASE2 Worker owns:**
  - `backend/meshing/native/CMakeLists.txt`
  - Native loading checks in `backend/meshing/difvm_topology.py` and `backend/meshing/mesh_engine.py`
- **Milestone M3_PHASE3 Worker owns:**
  - `backend/meshing/mesh_engine.py` (box detection, farfield shield, 500k limit, tuning factors)
  - `backend/geometry/parsers/step_parser.py` (Gmsh default)
  - `backend/meshing/native/src/octree_hexcore.cpp` (max_level clamp, 10^-15 overwrite)
- **Milestone M4_PHASE4 Worker owns:**
  - `backend/meshing/difvm_topology.py` (2D centroids, aspect ratio, wsrd_alpha, Supermesh AMI)
  - `backend/meshing/native/src/difvm_mesh.cpp` (2D centroids, aspect ratio, wsrd_alpha)
  - `backend/meshing/moving_mesh.py` (explicit polyhedral faces)
  - `backend/meshing/gci.py` (full Celik GCI)
  - `backend/meshing/coupling.py` (boundary area vectors, conservative P2M)
- **Milestone M5_PHASE5 Worker owns:**
  - `backend/native_cpp/smr_catalysis/smrKineticsSource.C`
  - `backend/meshing/polymesh_io.py` (multi-region export, mappedWall keys)
  - `backend/core/case_generator.py` (chtMultiRegionFoam case files, 0/, fvSolution)
  - `backend/runner/wsl_runner.py` (shlex.quote, return codes, path fix)

---

## Resource & Execution Guardrails
1. **Low-Resource Throttling:** `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
2. **Small Meshes:** Cell counts strictly $\le 30{,}000$ cells during verification.
3. **Sequential Execution:** Run tests strictly sequentially (`.\\.venv\\Scripts\\python.exe -m pytest ...`), never with `-n` / xdist.
4. **Active Background Process Protection:** Zero starvation for background Ansys process.
5. **Zero-Shortcut & Fail-Fast:** No silent clamps, no swallowed exceptions, no fake test assertions.
6. **Mandatory Unanimous 3-Agent Gate:** 1x Anti-Cheat / Code Cleanliness, 1x Mathematical Fidelity / Paper Conformance, 1x Code Quality / Stability. 100% 3:0 APPROVE required to pass each milestone.
