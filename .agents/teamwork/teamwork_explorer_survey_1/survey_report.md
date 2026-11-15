# Survey Report: Phase 1 & Phase 2 Codebase Analysis

**Author:** Survey Explorer 1 (teamwork_explorer)  
**Date:** 2026-11-10  
**Scope:** Phase 1 (K7, K6, L4, L5, M9) and Phase 2 (H1)  
**Integrity Mode:** Development (Zero-Shortcut, Zero-Cheat Policy)

---

## 1. Executive Summary

This report provides the exact code locations, line numbers, mathematical proofs, diff specifications, and verification strategies for Phase 1 (Honesty & Test Integrity) and Phase 2 (Build & Platform OpenMP).

Key discoveries:
1. **K7 & L4 & L5:** `requirements.txt` is encoded in **UTF-16LE with BOM** (`\xff\xfe`), breaking non-pip tools. `README.md` and `ROADMAP.md` contain 7 absolute `file:///G:/...` links and advertise nonexistent in-memory FVM solvers, Fortran kernels (`backend/native_fortran` is empty), optimization algorithms (`backend/optimization` is empty), and material databases (`resources/materials_db` is empty).
2. **Doc Bundling & Repo Hygiene:** User strictly mandated that `SUGGESTIONS_BACKEND.md`, `SUGGESTIONS_FRONTEND.md`, `COMMUNICATION_BACKEND_FRONTEND.md`, and `FRONTEND_REQUIREMENTS_ON_BACKEND.md` remain untouched in root. Historical `.agents/teamwork/` metadata and `TEST_READY.md` will be consolidated into `docs/TEAMWORK_DEVELOPMENT_HISTORY.md`.
3. **K6:** `tests/test_wp4_multiphysics_hexcore.py` contains 4 local copy helper functions (lines 96–234) and multiple tautological test cases (lines 358–442, 443–480, 511–524, 558–575, 576–593) that test synthetic Python math instead of backend code. Canonical counterparts exist in `backend/meshing/gci.py` and `backend/meshing/difvm_topology.py`, while AMIPS needs a canonical export from `difvm_topology.py`.
4. **M9:** `UnstructuredMesh.validate()` in `backend/meshing/models.py:414-560` currently checks volume positivity, contiguous ordering, patch coverage, owner < neighbour, Gauss closure, and non-orthogonal decomposition, but does NOT check cell centroid containment or global centroid balance against the boundary integral. Consequently, the K1 defect (cell centroids multiplied by 4/3) passed undetected.
5. **H1:** `backend/meshing/native/CMakeLists.txt` only specifies compile flag `-fopenmp` on non-MSVC but never calls `find_package(OpenMP REQUIRED)` nor links `OpenMP::OpenMP_CXX`. Furthermore, `get_native_dll()` in `difvm_topology.py:282` catches `Exception` and silently returns `None`, causing `mesh_engine.py` to quietly fall back to Python Cartesian meshing unless `CFD_STRICT_NATIVE=1`.

---

## 2. Phase 1: Ehrlichkeit & Doku-Integrität (K7, L4, L5)

### 2.1 L5: `requirements.txt` UTF-16LE Encoding
- **File:** `requirements.txt`
- **Observed:**
  - First bytes: `b'\xff\xfec\x00o\x00l\x00o\x00r\x00a\x00m\x00'` (UTF-16LE BOM followed by null-byte interleaved ASCII).
  - Total packages: 10 (`colorama==0.4.6`, `gmsh==4.15.2`, `iniconfig==2.3.1`, `numpy==2.5.4`, `packaging==26.3`, `pluggy==1.7.0`, `Pygments==2.21.0`, `pytest==9.1.1`, `scipy==1.18.1`, `trimesh==5.1.1`).
- **Remediation Plan:**
  - Read as UTF-16, re-encode and write as clean UTF-8 without BOM.
  - Verification: `python -c "open('requirements.txt', 'r', encoding='utf-8').read()"`.

### 2.2 L4: Absolute `file:///G:/` Links
- **Observed Links:**
  - `README.md:24`: `[ARCHITECTURE.md](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md)`
  - `ROADMAP.md:3`: `[ARCHITECTURE.md](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md)`
  - `ROADMAP.md:3`: `[literature_survey_2026.md](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md)`
  - `ROADMAP.md:37`: `[SUGGESTIONS_BACKEND.md](file:///G:/Datein/Projects/CFDSolver/SUGGESTIONS_BACKEND.md)`
  - `ROADMAP.md:40`: `[Meshing_FVM_DiFVM_2603.15920v1.pdf](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_FVM_DiFVM_2603.15920v1.pdf)`
  - `ROADMAP.md:44`: `[MeshingFVM_SPH_2312.03542v1.pdf](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf)`
  - `ROADMAP.md:45`: `[MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf)`
  - `ROADMAP.md:62`: `[literature_survey_2026.md](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md)`
- **Remediation Plan:**
  - Convert all `file:///G:/Datein/Projects/CFDSolver/<relpath>` instances to clean relative markdown paths `<relpath>` or `./<relpath>`.

### 2.3 K7: README.md & ROADMAP.md Status Alignment
- **Observed Ghost Directories:**
  - `backend/native_fortran/`: Directory exists, but contains 0 files (100% empty).
  - `backend/optimization/`: Directory exists, but contains 0 files (100% empty).
  - `frontend/components/`: Directory exists, but contains 0 files (100% empty).
  - `resources/materials_db/`: Directory exists, but contains 0 files (100% empty).
  - `resources/validation_benchmarks/`: Directory exists, but contains 0 files (100% empty).
  - `backend/physics_modules/`: Contains 12 files that generate OpenFOAM dictionary strings, not standalone C++/Fortran physics solvers.
- **Remediation Plan for `README.md`:**
  1. Subtitle: Update from "Eigenständige... Plattform angetrieben durch Google-Style C++, Modern Fortran..." to:
     `> **Modulare CAD-Ingestion, Unstrukturierte Vernetzungs- & OpenFOAM-Case-Export-Plattform (Prototyp)**`
  2. Section 1 (Vision): Add explicit disclaimer:
     `> **Aktueller Entwicklungsstand:** Funktionaler Prototyp für CAD-Import (STEP, STL, OBJ), unstrukturierte Vernetzung (Cut-Cell / Octree Hexcore) und OpenFOAM-Case-/Gitter-Generierung. Standalone In-Memory FVM-Gleichungslöser, Fortran-Rechenkerne und Optimierungsmodule (SIMP / Adjoint / PSO) stellen die geplante Zielarchitektur dar.`
  3. Section 2 (Tree): Mark ghost directories explicitly:
     - `backend/native_fortran/   # Modern Fortran Numerik- & Physik-Kerne (geplant / in Entwicklung)`
     - `backend/optimization/     # SIMP, Gradient Descent, PSO, Monte Carlo (geplant / in Entwicklung)`
     - `frontend/components/       # Reaktive Vuetify-Widgets (geplant)`
     - `resources/materials_db/   # Stoffwert-Datenbank (geplant)`
     - `resources/validation_benchmarks/ # ASME V&V 20 Referenzfälle (geplant)`
     - `backend/physics_modules/  # 12 gekapselte OpenFOAM Dictionary- & Case-Vorlagen-Generatoren`
  4. Section 3: Clearly mark optimization methods as planned target architecture.
- **Remediation Plan for `ROADMAP.md`:**
  - In Phase 1: Clarify that the "272/272 Tests bestanden" referred solely to CAD geometry import & topology unit tests.

### 2.4 Repository Hygiene & Document Consolidation
- **Preserved in Root (UNTOUCHED as per user mandate):**
  - `README.md`
  - `ARCHITECTURE.md`
  - `ROADMAP.md`
  - `SUGGESTIONS_BACKEND.md`
  - `SUGGESTIONS_FRONTEND.md`
  - `COMMUNICATION_BACKEND_FRONTEND.md`
  - `FRONTEND_REQUIREMENTS_ON_BACKEND.md`
- **Consolidated into `docs/`:**
  - Create directory `docs/`.
  - Compile `docs/TEAMWORK_DEVELOPMENT_HISTORY.md` summarizing and indexing:
    - `TEST_READY.md` (claims vs review findings)
    - `TEST_INFRA.md`
    - `DEAD_ENDS.md`
    - Chronological index of agent handoffs and milestones from `.agents/teamwork/`.

---

## 3. Phase 1: Tautologische Tests (K6)

### 3.1 Local Helper Functions in `tests/test_wp4_multiphysics_hexcore.py`

| Local Function in Test | Lines in `test_wp4_multiphysics_hexcore.py` | Canonical Location in `backend/` | Action Required |
|---|---|---|---|
| `compute_amips_energy(vertices, reference_scale)` | Lines 96–125 | C++: `computeTetAMIPS` in `backend/meshing/native/src/octree_hexcore.cpp:58`. Python: Expose `compute_amips_energy` in `backend/meshing/difvm_topology.py` | Export canonical function in `difvm_topology.py` and import in test. |
| `compute_wsrd_weights(cut_alpha, neighbor_alphas, alpha_target)` | Lines 127–154 | `compute_wsrd_weights_array` & `compute_wsrd_redistribution_map` in `backend/meshing/difvm_topology.py:480-565` | Replace local scalar formula with canonical backend functions or import canonical wrapper from `difvm_topology.py`. |
| `compute_asme_vv20_gci(phi_1, phi_2, phi_3, r, f_s)` | Lines 157–198 | `compute_asme_vv20_gci` in `backend/meshing/gci.py:127-187` | Delete local copy; import `from backend.meshing.gci import compute_asme_vv20_gci`. `GCIResult` supports dict indexing, 100% compatible. |
| `compute_farfield_bounding_box(...)` | Lines 200–234 | `MeshEngine._compute_farfield_bounds` in `backend/meshing/mesh_engine.py:372-430` | Expose as public `compute_farfield_bounds` or `compute_farfield_bounding_box` in `backend/meshing/mesh_engine.py` and import directly. |

### 3.2 Tautological Test Cases in `tests/test_wp4_multiphysics_hexcore.py`

1. **`test_octree_2to1_balancing_condition` (lines 358–442):**
   - *Problem:* Constructs a local `OctreeTestNode` class in Python, subdivides it in Python, and checks its own Python subdivision algorithm. It tests ZERO lines of `backend/meshing/native/src/octree_hexcore.cpp`.
   - *Remediation:* Call `generate_hexcore_native` (or `mesh_hexcore`) with sizing gradient that enforces 2:1 balancing on a sphere or step geometry, then verify that adjacent leaves across the returned mesh differ by at most one refinement level (cell volume ratio $\le 8$ in 3D).
2. **`test_mosaic_polyhedral_quad_subface_splitting` (lines 443–480):**
   - *Problem:* Subdivides a quad in Python and asserts `CellType.POLYHEDRON == 42`.
   - *Remediation:* Verify that an agglomerated polyhedral cell generated by `generate_hexcore_native` has `cell_types[c] == 42` and that its faces match the sub-face topology.
3. **`test_snapping_along_grad_phi` (lines 511–524):**
   - *Problem:* Computes `target_node = node - phi_val * (grad_phi / np.sum(grad_phi**2))` inline on a synthetic point and verifies the point is on a sphere.
   - *Remediation:* Test `generate_hexcore_native` on a sphere CAD/SDF and verify that actual mesh wall boundary nodes snap to $|\phi| < 10^{-3} \cdot h_0$.
4. **`test_small_cell_agglomeration_vcut_below_02` (lines 558–575):**
   - *Problem:* Manually adds `0.08 + 0.92 = 1.0` and asserts `v_merged == 1.0`.
   - *Remediation:* Call `generate_hexcore_native` on a geometry that generates cut cells; verify that all cells in the resulting mesh satisfy $V_{\text{cell}} \ge \text{small\_cell\_ratio} \cdot V_{\text{background}}$.
5. **`test_berger_giuliani_wsrd_weights` (lines 576–593):**
   - *Problem:* Uses the local copy `compute_wsrd_weights`.
   - *Remediation:* Call `compute_wsrd_weights_array` and `compute_wsrd_redistribution_map` from `backend.meshing.difvm_topology`.
6. **`_create_synthetic_cht_mesh` (lines 765–800):**
   - *Problem:* Hardcodes 11 synthetic face arrays.
   - *Remediation:* Replace or supplement with multi-region meshing of two abutting bodies or ensure `extract_cht_twin_faces` is tested on a mesh built via `compute_difvm_topology`.

---

## 4. Phase 1: Mesh-Validierung & Fixtures (M9)

### 4.1 `UnstructuredMesh.validate()` Extensions in `backend/meshing/models.py`

#### A. Mathematical Formulation
1. **Centroid Containment in Cell Bounding Box:**
   For every cell $i \in \{0, \dots, N_c - 1\}$:
   Let $V_i = \text{cells}[i]$ be the vertex indices of cell $i$.
   $$\mathbf{b}_{\min, i} = \min_{v \in V_i} \mathbf{x}_v, \quad \mathbf{b}_{\max, i} = \max_{v \in V_i} \mathbf{x}_v$$
   The cell centroid $\mathbf{C}_i = \text{cell\_centres}[i]$ must satisfy:
   $$\mathbf{b}_{\min, i} - \epsilon_i \le \mathbf{C}_i \le \mathbf{b}_{\max, i} + \epsilon_i$$
   where $\epsilon_i = 10^{-6} \cdot \max(\|\mathbf{b}_{\max, i} - \mathbf{b}_{\min, i}\|, 10^{-12})$.
   *Detection:* In K1, $\mathbf{C}_i$ was multiplied by $4/3$. For any element offset from the origin (e.g. $[10, 11]^3$), the centroid was calculated as $14.0$ instead of $10.5$, which is well outside $[10, 11]$. This check catches K1 immediately.

2. **Global Centroid vs Boundary Surface Integral:**
   By the Divergence Theorem, for any bounded domain $\Omega \subset \mathbb{R}^d$:
   $$\int_\Omega \mathbf{x} \, dV = \frac{1}{d + 1} \int_{\partial \Omega} \mathbf{x} (\mathbf{x} \cdot \mathbf{n}) \, dS$$
   For a polyhedral mesh with planar boundary faces:
   $$\sum_{i=1}^{N_c} V_i \mathbf{C}_i \approx \frac{1}{d + 1} \sum_{f \in \text{boundary}} \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f)$$
   where $d = \text{topological\_dim}$ ($d=3$ for 3D, factor $1/4$; $d=2$ for 2D, factor $1/3$), and $\mathbf{S}_f$ is the outward-pointing boundary face area vector.
   The check requires:
   $$\left\| \sum_{i=1}^{N_c} V_i \mathbf{C}_i - \frac{1}{d + 1} \sum_{f \in \text{boundary}} \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f) \right\| < \text{tol}_{\text{centroid}} \cdot V_{\text{total}} \cdot L_{\text{char}}$$
   where $L_{\text{char}}$ is the bounding box diagonal of the domain.

3. **Cell Elongation / Aspect Ratio Check:**
   $$\text{elongation}_i = \frac{\max(\Delta x_i, \Delta y_i, \Delta z_i)}{\min(\Delta x_i, \Delta y_i, \Delta z_i)}$$
   Verify that for regular interior cells (outside boundary layer inflation), $\text{elongation}_i \le \text{max\_aspect\_ratio}$ (default 1000.0 to catch collapsed/degenerate flat cells).

#### B. Implementation Location in `models.py`
Add directly to `UnstructuredMesh.validate()` after invariant 5 (lines 559–560), accepting optional parameters `tol_centroid: float = 1e-4, check_containment: bool = True`.

### 4.2 `pytest.ini` & Missing CAD/NX Fixtures
- **Current `pytest.ini`:**
  ```ini
  [pytest]
  pythonpath = .
  testpaths = backend/geometry/tests
  ```
- **Target `pytest.ini`:**
  ```ini
  [pytest]
  pythonpath = .
  testpaths = tests backend/geometry/tests
  ```
- **Fixture Guards Needed:**
  1. `backend/geometry/tests/test_bridge.py:16-24`:
     - `test_cad_bridge_detection`: Hardcodes `assert inst["Siemens_NX"] is not None`.
     - *Fix:* Replace hard assertion with `@pytest.mark.skipif(not CADBridge().nx_translator.is_available, reason="Siemens NX not installed on host")`.
  2. `tests/test_cad_e2e.py:344`:
     - Assertions on Siemens NX installation.
     - *Fix:* Check `bridge.nx_translator.is_available` or skip.
  3. `tests/test_adversarial_challenger_2.py:382`:
     - `test_real_cad_sample_nx_prt`: Skip if NX is unavailable and no fallback file is present.
  4. `tests/test_wp4_multiphysics_hexcore.py:976`:
     - `test_cad_benchmark_snappy_multi_region_heater`: Add:
       `if not heater_gz.exists() or not bottom_air_gz.exists(): pytest.skip("OpenFOAM tutorial assets not found.")`.

---

## 5. Phase 2: Build & Plattform (H1)

### 5.1 CMakeLists.txt OpenMP Linkage
- **File:** `backend/meshing/native/CMakeLists.txt`
- **Observed:**
  - Lines 8–24:
    ```cmake
    if(MSVC)
        add_compile_options(... /openmp ...)
    else()
        add_compile_options(-O3 -fno-fast-math -ffp-contract=off -fopenmp -Wall -Wextra)
    endif()
    ```
  - `find_package(OpenMP REQUIRED)` is completely missing!
  - `target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)` is completely missing!
- **Consequence on Linux:**
  - Shared library compiles with OpenMP pragmas, but `libgomp` is never linked.
  - Runtime loading via `ctypes.CDLL` throws:
    `undefined symbol: GOMP_loop_nonmonotonic_dynamic_next`.
- **Exact Diff Specification:**
  ```cmake
  find_package(OpenMP REQUIRED)

  add_library(cfd_mesher_native SHARED
      src/cfd_mesher_native.cpp
      ...
  )

  target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)
  ```
  This is the official CMake standard; it automatically sets compiler flags (`/openmp` or `-fopenmp`) AND linker flags (`vcomp.lib` or `-lgomp`) for any platform.

### 5.2 Native Library Loaders (Fail-Fast vs Silent Fallback)
- **`backend/meshing/difvm_topology.py`:**
  - In `get_native_dll()`:
    Add global `_NATIVE_LOAD_ERROR: Optional[str] = None`.
    In the `except Exception as e` block:
    ```python
    _NATIVE_LOAD_ERROR = f"{type(e).__name__}: {e}"
    logger.error(f"Failed to load native mesher library from {dll_path}: {_NATIVE_LOAD_ERROR}")
    ```
    Expose a helper `require_native_dll() -> ctypes.CDLL` that raises `RuntimeError(f"Native library failed to load: {_NATIVE_LOAD_ERROR}")` if `_NATIVE_DLL is None`.
  - In `compute_difvm_topology()`:
    When `use_native=True` and `strict_native=True`:
    Raise `RuntimeError(f"Strict native execution requested but native library failed to load: {_NATIVE_LOAD_ERROR}")`.
- **`backend/meshing/mesh_engine.py`:**
  - In `mesh_hexcore()` (lines 226–231) and `_mesh_external_fluid()` (lines 473–478):
    When `is_strict=True` or `CFD_STRICT_NATIVE=1`, never swallow exceptions.
    Log error prominently with `logger.error` rather than `logger.warning`.

---

## 6. Verification & Test Plan

1. **Phase 1 Verification:**
   - `python -c "open('requirements.txt', 'r', encoding='utf-8').read()"` succeeds.
   - Grep for `file:///` across `README.md` and `ROADMAP.md` returns 0 matches.
   - `UnstructuredMesh.validate()` on a shifted cube with 4/3 centroid error raises `ValueError` with centroid mismatch.
   - `UnstructuredMesh.validate()` on a correct unit cube and sphere passes.
   - `pytest tests/test_wp4_multiphysics_hexcore.py` passes without local copies of `compute_asme_vv20_gci`, `compute_wsrd_weights`, `compute_farfield_bounding_box`.
   - `pytest` (without arguments) runs across `tests` and `backend/geometry/tests` and executes without crashes.
2. **Phase 2 Verification:**
   - On Windows: CMake configures and builds `cfd_mesher_native.dll` with OpenMP.
   - `python -c "from backend.meshing.difvm_topology import get_native_dll; assert get_native_dll() is not None"` passes.
   - With `CFD_STRICT_NATIVE=1`, native hexcore tests execute without falling back to Python.
