# Phase 1 Survey: Baseline Verification, Build System & Zero-Shortcut Integrity Audit

**Author:** explorer_phase1_survey_3  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_3`  
**Date:** 2026-10-30  
**Project:** CFDSolver In-Memory Multiphysics Mesher Backbone  

---

## Executive Summary

This report delivers a comprehensive investigation of the CFDSolver baseline test suites, native C++20 CMake build configuration, ctypes C-ABI bridge, Zero-Shortcut policy compliance across `backend/meshing/`, and acceptance test designs for Gates WP1 and WP2.

### Key Audit Findings:
1. **Baseline Test Status:** Exactly **540/540 tests** are currently green (passing in 35.21s) when executed via `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests`.
2. **Build System Invariance:** `backend/meshing/native/CMakeLists.txt` uses `/fp:fast` (MSVC) and `-ffast-math` (GCC/Clang), which violates IEEE-754 determinism, breaks NaN/Inf handling, and causes sign flips in geometric projections. Visual Studio 2022 Community (`cl.exe`, `vcvars64.bat`) and CMake are verified functional on the host.
3. **C-ABI Exception Vulnerability:** Functions in `backend/meshing/native/src/cfd_mesher_native.cpp` lack `try/catch` blocks. Unhandled C++ exceptions unwind across the C-ABI boundary into Python, causing hard process crashes. `mesh_engine.py` allocates native handles without `try ... finally: cfd_mesher_free(m_ptr)`.
4. **Zero-Shortcut Violations Discovered:**
   - Bare `except Exception: pass` silently swallowing meshing failures in `mesh_engine.py:460`.
   - Hardcoded geometry heuristic `if len(verts) == 8 and len(faces) == 12:` in `mesh_engine.py:99`.
   - Dead code and 2D quad discrimination bug: in `difvm_mesh.cpp:94` and `difvm_topology.py:204`, any 4-vertex cell is unconditionally parsed as a 3D tetrahedron, breaking 2D quad meshes.
   - Unscaled millimeter constants `dx = max(3.0, h0 / 3.0)` in `mesh_engine.py:221-223`.
   - Inverted face masking: `cos_theta = std::clamp(..., 0.0, 1.0)` clamps inverted faces ($S_f \cdot d_f \le 0$) to $90^\circ$ instead of escalating to stabilization mask 3.
   - Contiguous block corruption in `extract_cht_twin_faces`: sets `neighbour[f] = -1` in-place inside the internal face block.

---

## 1. Test Suite Baseline & Test Runner Specification

### 1.1 Test Suite Inventory & Structure
The CFDSolver test suite is organized into two primary trees:
- `tests/`: 22 test files covering adversarial geometry audits, mesh models, DiFVM topology, ALE moving mesh, SPH/OPMIS coupling, ASME V&V 20, and CAD sample pipelines.
- `backend/geometry/tests/`: 7 test files covering CAD parsers (STL, OBJ, STEP), surface auditor, quality metrics, scaler, and CAD bridge.

### 1.2 Exact Test Runner Commands

| Target Scope | Test Runner Command | Collected Tests | Current Status |
| :--- | :--- | :--- | :--- |
| **All Tests (Baseline)** | `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests` | **540** | **540 Passed (100% Green)** in 35.21s |
| Geometry Only | `.\.venv\Scripts\python.exe -m pytest backend/geometry/tests` | 37 | 37 Passed |
| Mesher Only | `.\.venv\Scripts\python.exe -m pytest tests/test_mesher.py` | 14 | 14 Passed |
| Fast Mode | `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests -q` | 540 | 540 Passed |

*Note on `pytest.ini`:* `pytest.ini` currently sets `testpaths = backend/geometry/tests`. Running bare `pytest` only executes 37 tests. Workers and CI must specify `tests backend/geometry/tests` or update `pytest.ini` to `testpaths = tests backend/geometry/tests`.

### 1.3 Baseline Warnings
During the 540-test baseline run, 8 `RuntimeWarning: overflow encountered in square` warnings occur in `backend/meshing/geometry_sdf.py:714, 719, 727, 737` during extreme coordinate stress tests ($>10^{160}$). These warnings highlight the numerical sensitivity of the current centroid-KDTree SDF and confirm the necessity of the WP2 BVH/AABB-Tree refactoring.

---

## 2. Native CMake Build System & C-ABI Bridge Architecture

### 2.1 Compiler Flags & IEEE-754 Compliance Audit
In `backend/meshing/native/CMakeLists.txt`:
```cmake
# Current (Violating Zero-Shortcut & IEEE-754 Determinism):
if(MSVC)
    add_compile_options(/std:c++20 /O2 /Oi /Ot /fp:fast /openmp /W4 /MP)
else()
    add_compile_options(-O3 -ffast-math -fopenmp -Wall -Wextra)
endif()
```

**Required Remediation:**
```cmake
# Required (IEEE-754 Precise Floating Point):
if(MSVC)
    add_compile_options(/std:c++20 /O2 /Oi /Ot /fp:precise /openmp /W4 /MP)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DCFD_MESHER_EXPORTS)
else()
    add_compile_options(-O3 -fno-fast-math -ffp-contract=off -fopenmp -Wall -Wextra)
    add_definitions(-DCFD_MESHER_EXPORTS)
endif()
```
**Rationale:** `/fp:fast` and `-ffast-math` permit associative reordering of additions, assume non-NaN/non-Inf arithmetic, and ignore signed zeros. In CFD meshing, this corrupts projection collinearity checks ($\|\delta_f \times d_f\| / (\|\delta_f\| \|d_f\|) < 10^{-12}$), causes inconsistent cell Gauss closure sums between debug and release builds, and masks degeneracies.

### 2.2 Cross-Platform Library Discovery
In `backend/meshing/difvm_topology.py`:
- **Current Defect:** Lines 26–27 hardcode:
  ```python
  DLL_DIR = Path(__file__).resolve().parent
  NATIVE_DLL_PATH = DLL_DIR / "cfd_mesher_native.dll"
  ```
  This immediately fails on Linux (`.so`) and macOS (`.dylib`).
- **Required Remediation:**
  Multi-platform loader scanning candidate filenames:
  ```python
  def _find_native_library() -> Optional[Path]:
      dll_dir = Path(__file__).resolve().parent
      candidates = [
          dll_dir / "cfd_mesher_native.dll",         # Windows
          dll_dir / "libcfd_mesher_native.so",        # Linux
          dll_dir / "cfd_mesher_native.so",           # Linux alt
          dll_dir / "libcfd_mesher_native.dylib",     # macOS
          dll_dir / "cfd_mesher_native.dylib",        # macOS alt
      ]
      for p in candidates:
          if p.exists():
              return p
      return None
  ```

### 2.3 C-ABI Exception Safety & Memory Management
1. **Extern "C" Exception Guards:**  
   Every function in `cfd_mesher_native.cpp` must be wrapped in `try { ... } catch (const std::exception& e) { return -2; } catch (...) { return -2; }`.
2. **Buffer Bounds Validation:**  
   In `cfd_mesher_build_from_cells`:
   - Validate `cell_offsets[c + 1] >= cell_offsets[c]`.
   - Validate `cell_nodes[k] >= 0 && cell_nodes[k] < n_points`.
   - On boundary violation, return `-1` (invalid argument).
3. **Python Handle Free Guarantee:**  
   In `backend/meshing/mesh_engine.py` (lines 360–433):
   Wrap `m_ptr = self._native_dll.cfd_mesher_create()` strictly in:
   ```python
   m_ptr = self._native_dll.cfd_mesher_create()
   try:
       res = self._native_dll.cfd_mesher_generate_2d_polygon(...)
       if res != 0:
           raise RuntimeError(f"Native mesher returned error code {res}")
       ...
   finally:
       self._native_dll.cfd_mesher_free(m_ptr)
   ```

### 2.4 Host Toolchain Verification
The Windows development environment possesses the required toolchain:
- **Visual Studio 2022 Community:** `C:\Program Files\Microsoft Visual Studio\2022\Community`
- **MSVC Compiler:** `vcvars64.bat` located at `VC\Auxiliary\Build\vcvars64.bat`
- **CMake:** `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` (CMake 3.20+)
- Verified configuration command:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S backend/meshing/native -B backend/meshing/native/build -G "Visual Studio 17 2022" -A x64`

---

## 3. Comprehensive Zero-Shortcut Audit (`backend/meshing/`)

| # | Category | Location | Verbatim Defect Code / Mechanism | Required Zero-Shortcut Fix |
| :- | :--- | :--- | :--- | :--- |
| **Z1** | Bare `except: pass` | `mesh_engine.py:460-461` | `except Exception: pass` silently masks native C++ meshing exceptions and falls back to trivial centroid fan. | Remove silent pass; log exact exception with `logger.error` or propagate in `strict_native=True`. |
| **Z2** | Silent DLL fallback | `difvm_topology.py:832-836` | `except Exception: mesh = None` silently ignores native failures without logging or strict mode raise. | Add `strict_native: bool = False` argument. If `strict_native=True`, raise `RuntimeError("Native mesher execution failed: ...")`. |
| **Z3** | Geometry Heuristic | `mesh_engine.py:99` | `if len(verts) == 8 and len(faces) == 12:` triggers dedicated hardcoded `_mesh_rotated_cube`. | Delete heuristic. General polyhedral mesher must handle arbitrary polyhedra without vertex/face count checks. |
| **Z4** | Dead Code & Quad Bug | `difvm_mesh.cpp:94, 119` and `difvm_topology.py:204` | `else if (nv == 4)` tetrahedron check precedes 2D quad check on line 119, making 2D quad unreachable. | Check planarity ($V_{\text{tet}} / L^3 < 10^{-6}$): coplanar 4-node cells are `QUAD=9`, non-coplanar are `TETRA=10`. |
| **Z5** | Unscaled mm Constants | `mesh_engine.py:221-223` | `dx = max(3.0, h0 / 3.0)` and `dz = max(5.0, h0)` hardcodes millimeters into meters. | Use scale-free relative discretization `h0` without hardcoded constants `3.0` or `5.0`. |
| **Z6** | Silent Resolution Clamp | `mesh_engine.py:158-160, 229-231` | `nx = min(nx, 25); ny = min(ny, 25); nz = min(nz, 25)` silently caps resolution. | Remove silent min-clamping. If cell count exceeds `max_cells`, raise `ValueError` or emit warning, never clamp. |
| **Z7** | Inverted Face Masking | `difvm_mesh.cpp:358` and `difvm_topology.py:578` | `cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), 0.0, 1.0)` clamps negative dot products to 0 ($90^\circ$). | Clamp `cos_theta` to `[-1.0, 1.0]`. If $S_f \cdot d_f \le 0$, unconditionally escalate `stabilization_mask = 3`. |
| **Z8** | Contiguous Block Violation | `difvm_topology.py:705` | `extract_cht_twin_faces` sets `neighbour[f] = -1` in-place, creating boundary holes inside internal face block. | Partition faces: internal faces ($nb \ge 0$) in `[0, n_internal)`, coupled twin faces removed and appended to boundary block. |
| **Z9** | Metadata Loss in RCM | `distmesh_core.py:105` | `mesh_rcm = compute_difvm_topology(points, new_cells)` drops `boundaries`, `baffles`, `cyclic_pairs`, `metadata`. | Re-index boundary and cyclic face references under cell permutation, preserving all mesh metadata. |
| **Z10** | Non-Manifold Silently Overwritten | `difvm_mesh.cpp:213` and `difvm_topology.py:487` | If $>2$ cells share a face, `temp_faces[f_idx].neighbour = c` overwrites neighbour without error. | If `temp_faces[f_idx].neighbour != -1`, immediately raise `ValueError("Non-manifold mesh: >2 cells share face")`. |
| **Z11** | Missing Face Connectivity | `models.py:204-220` and `difvm_mesh.hpp:53-77` | `UnstructuredMesh` and `NativeMeshData` lack face-to-node connectivity arrays (`faces` / `face_offsets`, `face_nodes`). | Add `faces: List[np.ndarray]` to `UnstructuredMesh`, `face_offsets`/`face_nodes` to `NativeMeshData` with C-ABI getters. |
| **Z12** | Missing CMS Error Map Fields | `models.py:218` and `difvm_topology.py:640` | `error_map` only has `non_ortho`, `skew`, `ar`, `mask`. Missing `m_skew`, `k_f_comp`, `beta_upwind`, `omega_cell`, `wsrd_alpha`. | Compute and populate all 5 CMS error compensation fields in both C++ and Python. |
| **Z13** | Redundant Sizing Configs | `models.py:46, 120` | `CurvatureSizingConfig` (`alpha`, `beta`, `growth_rate`) vs `SizingParameters` (`alpha`, `beta`, `grading`). | Unify configuration classes into a single canonical `SizingParameters` schema. |

---

## 4. Verification Matrices & Acceptance Criteria Test Design

### 4.1 Gate WP1 Verification Matrix

| Test ID | Test Target | Verification Mechanism | Acceptance Criterion |
| :--- | :--- | :--- | :--- |
| **WP1-T1** | 2D Quad planar mesh at $z = 5.0$ | Build an L-shaped planar mesh at $z = 5.0$ with 4-node cells. Call `compute_difvm_topology`. | `cell_types == [CellType.QUAD]*N` (value 9, NOT 10). `topological_dim == 2`. Face areas $S_f$ are 1D edge normals in $xy$-plane with $S_{f,z} = 0$. |
| **WP1-T2** | Scale Invariance ($10^{-6}\text{ m}$ to $10^{3}\text{ m}$) | Construct unit cube mesh, scale points by $s \in \{10^{-6}, 10^{-3}, 1.0, 10^{3}\}$. Run `mesh.validate()`. | Passes `validate()` without numerical overflow or spurious tolerance failures. Rel closure $< 10^{-11}$. |
| **WP1-T3** | Non-Manifold Detection | Create 3 hexahedra or 3 quads sharing a single face. Run `compute_difvm_topology`. | Deterministically raises `ValueError("Non-manifold mesh: face shared by more than 2 cells")`. In C++, returns `-2`. |
| **WP1-T4** | CHT Two-Region Mesh Partitioning | 2 adjoining cubes with `regions = [0, 1]`. Call `extract_cht_twin_faces`. | Zero `neighbour == -1` in $[0, n_{\text{internal}})$. Two boundary patches `coupledWall_region0_to_1` and `coupledWall_region1_to_0` generated with opposing normals $S_{f1} = -S_{f2}$. |
| **WP1-T5** | Inverted Face Escalation | Construct 2 cells with reversed face normal ($S_f \cdot d_f < 0$). Compute topology. | `stabilization_mask == 3`. `cos_theta` evaluates $< 0$ (not clamped to 0 / $90^\circ$). |
| **WP1-T6** | Lossless RCM & Hilbert SFC | Apply `rcm_reorder` and `hilbert_sfc_reorder` to mesh with `boundaries`, `baffles`, `cyclic_pairs`. | All patches, baffles, cyclic pairs, and metadata preserved identically; bandwidth reduced or SFC locality verified. |
| **WP1-T7** | CMS MeshErrorMap Fields | Inspect `mesh.error_map` on skewed/non-orthogonal mesh. | Contains `m_skew` ($\|m_f\| / \|d_f\|$), `k_f_comp` ($\alpha_{\max} = 1.5$), `beta_upwind`, `omega_cell`, `wsrd_alpha`. |
| **WP1-T8** | OpenFOAM polyMesh Export | Call `export_openfoam_polymesh(mesh, path)` on 3D hex mesh and 2D quad slab (`extrude_2d_to_3d_slab`). | Standard OpenFOAM files (`points`, `faces`, `owner`, `neighbour`, `boundary`) created. `neighbour` count equals $n_{\text{internal}}$. 2D slab has `type empty;` for `frontAndBack`. |

### 4.2 Gate WP2 Verification Matrix

| Test ID | Test Target | Verification Mechanism | Acceptance Criterion |
| :--- | :--- | :--- | :--- |
| **WP2-T1** | AABB-Tree Distance Query | Compare AABB-Tree triangle query against brute-force point-triangle projection on 5,000 random points. | Unsigned distance matches brute-force within $\|d_{\text{tree}} - d_{\text{exact}}\| < 10^{-14}$. Traversal speedup $> 10\times$. |
| **WP2-T2** | Angle-Weighted Pseudonormals on 30° & 90° Sharp Edges | Query SDF along lines crossing 30° wedge edge and 90° box corner, exactly through edge midpoints and vertices. | Zero sign flips! Distance function is strictly monotonic across boundary. Sign outside is positive, inside is negative. |
| **WP2-T3** | Generalized Winding Numbers on Dirty CAD | Non-watertight CAD mesh with 0.1 mm crack/gap. Query points inside solid cavity and outside. | $W(x) \ge 0.5$ for all interior points, $W(x) < 0.5$ for all exterior points. Classical single-normal SDF flips sign through crack; GWN remains continuous and robust. |
| **WP2-T4** | Differentiable VoroMesh Bisector Loss | Evaluate VoroMesh bisector loss on $k=16$ nearest Voronoi seeds across a uniform grid. | Continuous distance field without jump discontinuities. Gradient $\nabla \phi$ has unit norm $\|\nabla \phi\| \approx 1.0$ almost everywhere. |

---

## 5. Architectural Guidance for Implementation Workers

1. **Topological Invariants & Face Orientation Convention:**
   - Every internal face $f$ must satisfy `owner[f] < neighbour[f]`.
   - The face normal vector $S_f$ must point strictly from `owner[f]` to `neighbour[f]`.
   - Boundary faces must have `neighbour[f] == -1` and normal $S_f$ pointing outward into the exterior.
   - All internal faces must occupy indices $[0, n_{\text{internal}})$; all boundary faces must occupy $[n_{\text{internal}}, n_{\text{faces}})$.
2. **2D vs 3D Planarity Check:**
   - For any 4-node cell with points $p_0, p_1, p_2, p_3$:
     Let $v_1 = p_1 - p_0$, $v_2 = p_2 - p_0$, $v_3 = p_3 - p_0$.
     Tetrahedral volume $V = \frac{1}{6} |v_1 \cdot (v_2 \times v_3)|$.
     Edge scale $L = \frac{1}{4}(\|p_1 - p_0\| + \|p_2 - p_1\| + \|p_3 - p_2\| + \|p_0 - p_3\|)$.
     If $V / L^3 < 10^{-6}$: classify as `CellType.QUAD` ($9$), `topological_dim = 2`.
     Else: classify as `CellType.TETRA` ($10$), `topological_dim = 3`.
3. **No Short-Circuiting of Fallbacks:**
   - When native execution is requested (`use_native=True`), if the C-ABI function returns an error or raises, the system must NOT silently substitute a dummy shape or fall back to a trivial centroid fan without explicit logging. In `strict_native=True` mode, it must raise `RuntimeError`.
