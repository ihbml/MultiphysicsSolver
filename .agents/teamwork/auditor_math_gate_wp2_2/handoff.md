# Forensic & Mathematics Re-Audit Report — Gate WP2 (CFD Meshing Subsystem)

**Auditor Role**: Gate WP2 Mathematics & Numerics Auditor (Iteration 2 Re-Audit)  
**Working Directory**: `G:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_math_gate_wp2_2\`  
**Target**: Work Package 2 (Features F11–F14 in `backend/meshing/geometry_sdf.py` and `backend/meshing/native/`)  
**Profile**: General Project (Development Mode)  
**Integrity Verdict**: **CLEAN** (Zero hardcoded test results, zero facades, zero heuristics, zero fabricated outputs)  
**Gate WP2 Final Verdict**: **APPROVE** (100% remediation of all 3 previous audit findings, rigorous mathematical invariants verified, dual-engine parity within machine precision, 100% green across all test suites)

---

## 1. Observation

### 1.1 Remediation Verification of Previous Audit Findings

#### Fix 1: Replacement of Magic Constants and Elimination of Out-of-Bounds Under-Read in `sdf_engine.cpp`
- **Location**: `backend/meshing/native/src/sdf_engine.cpp:87-88, 109-110, 231, 236-237, 436-447, 455-465, 473-478, 588` and `backend/meshing/native/include/sdf_engine.hpp:35-42`.
- **Observations in Code**:
  - In `TriangleAABBTree::build_recursive` (lines 87–88, 109–110): All instances of `1e30` and `-1e30` replaced with `constexpr double kInf = std::numeric_limits<double>::infinity();` and `-kInf`.
  - In `TriangleAABBTree::closest_point` (lines 231, 236–237): Initial `best_dist` and `best_dist_sq` initialized strictly to `std::numeric_limits<double>::infinity()`.
  - In `MeshSDF::voromesh_loss` (line 588): `min_h_abs` initialized to `std::numeric_limits<double>::infinity()`.
  - In `TriangleAABBTree` (`sdf_engine.hpp:35-42`): Added accessors `root_bmin()` and `root_bmax()`.
  - In `MeshSDF::evaluate` (`sdf_engine.cpp`):
    - Extreme coordinate handler (lines 436–447): Coordinates with $p_{\max} \ge 10^{30}$ evaluated analytically with Euclidean norm $\|p\|$ and outward unit gradient $p / \|p\|$, preventing IEEE 754 squaring overflow ($> 1.8 \times 10^{308}$).
    - Explicit facet bounds check (lines 455–465):
      ```cpp
      if (facet < 0 || facet >= static_cast<int32_t>(tree_.faces().size())) {
        double p_norm = p.norm();
        Vec3 unit_p = p_norm > 1e-15 ? p / p_norm : Vec3(0, 0, 1);
        out_phi[i] = p_norm;
        if (out_grad) {
          out_grad[3 * i + 0] = unit_p.x;
          out_grad[3 * i + 1] = unit_p.y;
          out_grad[3 * i + 2] = unit_p.z;
        }
        continue;
      }
      ```
    - Bounding-box sign protection (lines 473–478): Ensures points strictly outside root AABB cannot evaluate to negative distance due to floating-point truncation ties.
- **Empirical Reproduction Verification**:
  Executed the reproduction script on point $p = (0, 0, 10^{50})$:
  - Command output (Native C-ABI DLL):
    `Point (0, 0, 1e50) -> phi: 1e+50 grad: [0. 0. 1.]`
  - Command output (Python `CADMeshSDF`):
    `Python CADMeshSDF point (0, 0, 1e50) -> phi: 1e+50 grad: [0. 0. 1.]`
  - Tested 7 octant extreme points ($\pm 10^{40}$ along all Cartesian axes and diagonal $[10^{40}, 10^{40}, 10^{40}]$): All evaluate to positive distance with $\|\nabla \phi\| = 1.0$ exact unit outward gradient.
  - Result: **PASS**. The out-of-bounds memory read (`face_edge_map_[-1]`) and distance clamping to $-10^{30}$ are completely eliminated.

#### Fix 2: Deletion of Ad-Hoc Quad Recovery Heuristic & Test Fixture Correction
- **Location**: `backend/meshing/geometry_sdf.py:1247-1265` and `tests/test_wp2_mesh_sdf.py:201`.
- **Observations in Code**:
  - `geometry_sdf.py`: The `unref_v` loop that synthesized parallelogram triangles from unreferenced vertices has been completely deleted.
  - Repository-wide search: Grep for `unref_v` and `recovered_faces` returned 0 matches across the entire codebase.
  - Test fixture in `tests/test_wp2_mesh_sdf.py:201` (`_build_dirty_cad_mesh_with_gap`): Triangle `[4, 8, 9]` is explicitly defined in `faces`:
    ```python
    faces = np.array([
        [0, 2, 1], [0, 3, 2],
        [0, 1, 5], [0, 5, 4],
        [2, 3, 7], [2, 7, 6],
        [0, 4, 7], [0, 7, 3],
        [1, 2, 6], [1, 6, 5],
        [4, 5, 8], [4, 8, 9],  # Fully defines the closed strip, leaving slit [1-gap, 1] open
    ], dtype=np.int32)
    ```
  - Result: **PASS**. Zero-Shortcut Policy R3 fully satisfied. Production geometry solver no longer alters user mesh topology.

#### Fix 3: Harmonization of Default `sign_method` in `CADMeshSDF`
- **Location**: `backend/meshing/geometry_sdf.py:1264-1269`.
- **Observations in Code**:
  ```python
  if sign_method is not None:
      self.sign_method = sign_method
  elif k_candidates is not None:
      self.sign_method = "legacy"
  else:
      self.sign_method = "pseudonormal"
  ```
- **Empirical Verification**:
  - `CADMeshSDF(mesh).sign_method` evaluates to `'pseudonormal'`.
  - `CADMeshSDF(mesh, k_candidates=8).sign_method` evaluates to `'legacy'`.
  - `CADMeshSDF(mesh, sign_method='winding_number').sign_method` evaluates to `'winding_number'`.
  - `CADMeshSDF(mesh, k_candidates=8, sign_method='pseudonormal').sign_method` evaluates to `'pseudonormal'`.
  - Result: **PASS**. Default architecture now uses the Triangle AABB Tree and Bærentzen & Aanæs angle-weighted pseudonormals per R2.

---

### 1.2 Mathematical Invariants Verification

1. **Eberly / Ericson 7-Region Point-to-Triangle Projection**:
   - Evaluated analytical test points across all 7 Voronoi regions:
     - Region 0 (Vertex A): $q = [0, 0, 0]$, barycentric $[1, 0, 0]$, dist $= 1.5$.
     - Region 1 (Vertex B): $q = [1, 0, 0]$, barycentric $[0, 1, 0]$, dist $= \sqrt{1.5} \approx 1.22474487$.
     - Region 2 (Vertex C): $q = [0, 1, 0]$, barycentric $[0, 0, 1]$, dist $= \sqrt{1.5} \approx 1.22474487$.
     - Region 3 (Edge AB): $q = [0.5, 0, 0]$, barycentric $[0.5, 0.5, 0]$, dist $= \sqrt{0.5} \approx 0.70710678$.
     - Region 4 (Edge BC): $q = [0.5, 0.5, 0]$, barycentric $[0, 0.5, 0.5]$, dist $= \sqrt{0.75} \approx 0.86602540$.
     - Region 5 (Edge CA): $q = [0, 0.5, 0]$, barycentric $[0.5, 0, 0.5]$, dist $= \sqrt{0.5} \approx 0.70710678$.
     - Region 6 (Interior): $q = [0.2, 0.2, 0]$, barycentric $[0.6, 0.2, 0.2]$, dist $= 1.0$.
   - Verified that C++ `TriangleAABBTree::point_to_triangle` and Python `_point_to_triangle_distance_exact` match the analytical values within $10^{-15}$.

2. **Bærentzen & Aanæs (2005) Angle-Weighted Pseudonormals**:
   - Sharp 30° Acute Wedge (`_build_acute_wedge_30deg`):
     - Flank face normals meet at $30^\circ$ apex angle.
     - Apex ridge edge pseudonormal evaluates to $[0.0, 0.0, 1.0]$ exactly.
     - Query points above apex $(1.0, 0.0, 0.1)$ evaluate to $\phi > 0$ with outward unit normal $[0, 0, 1]$.
     - Query points below apex $(1.0, 0.0, -0.5)$ evaluate to $\phi < 0$.
   - Anisotropic 90° Sharp Box (`_build_anisotropic_box`):
     - Tested 20 query points along corner diagonals across exterior and interior.
     - Zero sign flips: Exterior $\phi > 0$, interior $\phi < 0$.
     - Outward gradient norms $\|\nabla \phi\| = 1.0000000000000000$ everywhere.

3. **Generalized Winding Numbers (Oosterom & Strackee / Jacobson et al.)**:
   - Watertight Box:
     - Interior points: $W(x) = 1.0000000000000000$ (max error $< 10^{-16}$).
     - Exterior points: $W(x) = 0.0000000000000000$ (max error $< 3.5 \times 10^{-17}$).
     - C++ vs Python Parity: Maximum absolute difference across all query points is $1.77 \times 10^{-17}$.
   - Dirty CAD Mesh with Slit Opening (width $0.05$ m):
     - Traversing through the slit ($z = 0.8 \to 1.2$):
       $W(z=0.8) = 0.96326 \to W(z=1.0) = 0.49987 \to W(z=1.2) = 0.03674$.
     - Monotonic decay: $\frac{dW}{dz} < 0$ strictly holds across all query intervals without non-monotonic jumps or negative values.

4. **VoroMesh Differentiable Bisector Loss (Maruani et al. 2023)**:
   - Evaluated 100 random query points against 10 seed generators with $k = 5$.
   - Analytical gradient norm:
     - Python: $\min \|\nabla d\| = 0.9999999999999998$, $\max \|\nabla d\| = 1.0000000000000002$.
     - C++: $\min \|\nabla d\| = 0.9999999999999998$, $\max \|\nabla d\| = 1.0000000000000002$.
   - Dual-engine parity:
     - Loss: Python $= 0.01470598842191841$, C++ $= 0.014705988421918406$ (diff $< 10^{-16}$).
     - Distances and gradients match within $10^{-15}$.

---

### 1.3 Test Suite Execution Verification

| Test Suite | Command | Result | Duration |
|---|---|:---:|:---:|
| **WP2 Acceptance** | `.\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py -v` | **4/4 PASSED** | 0.66s |
| **WP1 Regression** | `.\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v` | **11/11 PASSED** | 0.59s |
| **Adversarial WP2 Gate** | `.\.venv\Scripts\python.exe -m pytest tests/test_adversarial_wp2_gate.py -v` | **12/12 PASSED** | 0.61s |
| **Baseline Regression (540)** | `.\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests ... -q` | **540/540 PASSED** | 63.63s |

---

## 2. Logic Chain

1. **Premise 1 (Zero-Shortcut Policy & Remediation Criteria)**:
   The previous audit vetoed Gate WP2 due to:
   - Magic constants `1e30`/`1e60` causing tree pruning, returning facet $-1$, and inducing buffer under-reads (`face_edge_map_[-1]`) with corrupted negative distances for coordinates $> 10^{30}$.
   - An ad-hoc quad recovery heuristic synthesizing faces to mask a defect in the test fixture.
   - Non-default selection of the pseudonormal algorithm when `k_candidates` was not supplied.
2. **Inference 1 (Remediation Efficacy)**:
   - Replacing `1e30`/`1e60` with `std::numeric_limits<double>::infinity()`, adding explicit bounds check `if (facet < 0 || facet >= faces.size())`, and handling extreme points analytically completely resolves memory safety and scale invariance. Points at $(0, 0, 10^{50})$ and across all octants now correctly evaluate with positive distances and unit outward normal gradients.
   - Removing the quad recovery heuristic restores topological integrity to `geometry_sdf.py`, and correcting `_build_dirty_cad_mesh_with_gap` in `tests/test_wp2_mesh_sdf.py` directly fixes the test fixture without solver heuristics.
   - Harmonizing `CADMeshSDF` to default to `sign_method="pseudonormal"` fulfills R2.
3. **Premise 2 (Mathematical Rigor & Dual-Engine Parity)**:
   All four core algorithms (Eberly/Ericson 7 regions, Bærentzen & Aanæs angle weighting, Oosterom & Strackee solid angles, Maruani et al. VoroMesh loss) satisfy their theoretical invariants:
   - Barycentric coordinates sum to 1.0 and match exact analytical projections.
   - Pseudonormals eliminate sign flips across $30^\circ$ apexes and $90^\circ$ box edges.
   - Winding numbers equal 1.0 and 0.0 on closed watertight meshes, and degrade monotonically across slit openings on dirty CAD meshes.
   - VoroMesh bisector gradients possess unit Euclidean norm $\|\nabla d\| = 1.0$ almost everywhere.
   - Parity between Python and compiled C++20 kernels is verified within machine precision ($10^{-14}$ to $10^{-17}$).
4. **Premise 3 (Non-Regression)**:
   All 11 WP1 acceptance tests, all 12 adversarial stress tests, and all 540 baseline regression tests pass with 0 failures and 0 warnings.
5. **Conclusion**:
   Gate WP2 has satisfied all technical, numerical, mathematical, and Zero-Shortcut requirements. The previous veto conditions have been cleanly remediated. Gate WP2 is approved.

---

## 3. Caveats

- **No Caveats**: All 3 previous findings are remediated and verified through independent empirical tests. No new failure modes, numerical instabilities, or memory safety issues were identified.

---

## 4. Conclusion & Gate Verdict

- **Integrity Profile Verdict**: **CLEAN**
- **Gate WP2 Re-Audit Verdict**: **APPROVE**

---

## 5. Verification Method

To independently verify the audit conclusions:

1. **Verify Extreme Point (0, 0, 1e50) Distance & Gradient in C++ & Python**:
   ```powershell
   .\.venv\Scripts\python.exe -c "
   import numpy as np, ctypes
   from backend.meshing.geometry_sdf import CADMeshSDF, _get_sdf_dll
   from tests.test_wp2_mesh_sdf import _build_anisotropic_box

   mesh = _build_anisotropic_box()
   dll = _get_sdf_dll()
   c_v = mesh.vertices.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
   c_f = mesh.faces.ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
   eng = dll.cfd_sdf_create(c_v, len(mesh.vertices), c_f, len(mesh.faces))

   p = np.array([[0.0, 0.0, 1e50]], dtype=np.float64)
   phi = np.empty(1, dtype=np.float64)
   grad = np.empty((1, 3), dtype=np.float64)
   dll.cfd_sdf_evaluate(eng, p.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), 1,
                        phi.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                        grad.ctypes.data_as(ctypes.POINTER(ctypes.c_double)))
   print('C++ point (0, 0, 1e50) -> phi:', phi[0], 'grad:', grad[0])
   assert phi[0] > 0.0 and np.allclose(grad[0], [0.0, 0.0, 1.0])
   dll.cfd_sdf_free(eng)

   sdf = CADMeshSDF(mesh)
   phi_py, grad_py = sdf.evaluate(p[0])
   print('Py  point (0, 0, 1e50) -> phi:', phi_py, 'grad:', grad_py)
   assert phi_py > 0.0 and np.allclose(grad_py, [0.0, 0.0, 1.0])
   print('PASS: Verified.')
   "
   ```

2. **Verify Acceptance, WP1 Regression, and Adversarial Test Suites**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py -v
   .\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v
   .\.venv\Scripts\python.exe -m pytest tests/test_adversarial_wp2_gate.py -v
   ```

3. **Verify Full Baseline Regression Suite**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests --ignore=tests/test_wp1_topology_polymesh.py --ignore=tests/test_wp2_mesh_sdf.py --ignore=tests/test_adversarial_wp2_gate.py -q
   ```
   *Expected Output*: `540 passed in ~64s`.
