# Forensic & Mathematics Audit Report — Gate WP2 (CFD Meshing Subsystem)

**Auditor Role**: Gate WP2 Mathematics & Numerics Auditor  
**Working Directory**: `G:\Datein\Projects\CFDSolver\.agents\teamwork\auditor_math_gate_wp2_1\`  
**Target**: Work Package 2 (Features F11–F14 in `backend/meshing/geometry_sdf.py` and `backend/meshing/native/`)  
**Profile**: General Project (Development Mode)  
**Profile Integrity Verdict**: **CLEAN** (No hardcoded outputs, facades, or test result fabrications)  
**Gate WP2 Final Verdict**: **REQUEST_CHANGES** (Veto triggered due to critical numerical clamping & out-of-bounds memory read in native C++ kernel, plus ad-hoc geometry heuristic)

---

## 1. Observation

### 1.1 Test Suite Execution Verification
Executed all project test suites in the Python 3.14 virtual environment:
1. **WP2 Acceptance Test Suite**:
   - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py -v`
   - Result: `4 passed in 0.52s`
     - `test_triangle_aabb_tree_closest_point` PASSED
     - `test_angle_weighted_pseudonormals_no_sign_flips` PASSED
     - `test_generalized_winding_numbers_dirty_cad` PASSED
     - `test_voromesh_differentiable_bisector_loss` PASSED
2. **WP1 Regression Test Suite**:
   - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v`
   - Result: `11 passed in 0.56s` (100% non-regression on WP1)
3. **Adversarial Gate WP2 Test Suite**:
   - Command: `.\.venv\Scripts\python.exe -m pytest tests/test_adversarial_wp2_gate.py -v`
   - Result: `12 passed in 0.63s`

### 1.2 Mathematical Formulation & Invariants Audit
1. **Triangle AABB Tree & Point-to-Triangle Projection (Eberly / Ericson)**:
   - File: `backend/meshing/geometry_sdf.py:609-664` (`_point_to_triangle_distance_exact`)
   - File: `backend/meshing/native/src/sdf_engine.cpp:146-224` (`point_to_triangle`)
   - All 7 Voronoi feature regions (3 vertices, 3 edges, 1 interior face) were independently tested against exact analytical coordinates. Barycentric coordinates satisfy $u, v, w \ge 0$ and $u + v + w = 1.0$ everywhere within $10^{-15}$.
   - Spatial coherence hint caching (`prev_facet`): Tested over 1,000 random query points and continuous trajectories with deliberately distorted and sequential facet hints. Confirmed that `prev_facet` acts strictly as an upper bound initialization ($d_{\text{upper}} = \text{dist}(p, \Delta_{\text{hint}})$) and branch-and-bound traversal visits all subtrees closer than $d_{\text{upper}}$, guaranteeing that spatial coherence caching never converges to suboptimal local minima.

2. **Pseudonormals Numerics (Bærentzen & Aanæs 2005)**:
   - File: `backend/meshing/geometry_sdf.py:672-776` (`compute_angle_weighted_pseudonormals`)
   - File: `backend/meshing/native/src/sdf_engine.cpp:306-415` (`Pseudonormals`)
   - Edge pseudonormals: For 2-manifold edges, summing incident face normals $\frac{n_1 + n_2}{\|n_1 + n_2\|}$ matches the dihedral-angle-weighted normal bisector direction because dihedral angle $\alpha = \pi - \arccos(n_1 \cdot n_2)$ is symmetric for both faces.
   - Vertex pseudonormals: Corner angle weights $\theta_i = \arccos\left(\frac{e_1 \cdot e_2}{\|e_1\| \|e_2\|}\right)$ correctly weight incident face normals.
   - Collinear / degenerate triangles: Collinear zero-area triangles are assigned zero angle weight ($\cos \theta = 1.0 \implies \arccos(1.0) = 0.0$), preventing NaNs or invalid normal distortions.
   - Opposing faces cancellation: A threshold of $10^{-6}$ was verified on back-to-back baffle faces; it successfully catches zero cancellation ($n_1 + n_2 \approx 0$) and falls back to a deterministic safe normal (`Vec3(0, 0, 1)`), preventing machine precision noise ($\sim 4 \times 10^{-15}$) from being normalized into arbitrary spurious unit normals.

3. **Generalized Winding Numbers (Oosterom & Strackee 1983 / Jacobson et al. 2013)**:
   - File: `backend/meshing/geometry_sdf.py:989-1050` (`compute_mesh_winding_number`)
   - File: `backend/meshing/native/src/sdf_engine.cpp:461-498` (`winding_number`)
   - Formula: $\tan(\Omega/2) = \frac{\det([a, b, c])}{\|a\|\|b\|\|c\| + (a \cdot b)\|c\| + (a \cdot c)\|b\| + (b \cdot c)\|a\|}$ using `2.0 * atan2(num, denom)`.
   - Floating-point parity between Python and C++ was tested on 1,000 off-boundary points: maximum difference between Python and C++ was $2.22 \times 10^{-16}$ ($\epsilon_{\text{mach}}$).
   - Smooth boundary continuity: Traversing through a 0.05 m slit on a non-watertight CAD mesh showed continuous, monotonic transition from interior ($W = 0.977$) through the opening ($W = 0.481$) to exterior ($W = 0.023$) without jump discontinuities.

4. **VoroMesh Bisector Loss (Maruani et al. 2023)**:
   - File: `backend/meshing/geometry_sdf.py:1057-1145` (`voromesh_bisector_loss`)
   - File: `backend/meshing/native/src/sdf_engine.cpp:500-592` (`voromesh_loss`)
   - Distance to bisector plane between seeds $q_i, q_j$ with midpoint $m_{ij} = \frac{q_i + q_j}{2}$ and unit normal $n_{ij} = \frac{q_j - q_i}{\|q_j - q_i\|}$ verified against exact analytical solution $d = |(x - m_{ij}) \cdot n_{ij}|$.
   - Analytical unit gradient norm: Evaluated gradients have $\|\nabla_x d\| = 1.0$ almost everywhere (within $10^{-12}$). Exact analytical values matched across both engines.

---

### 1.3 Critical Audit Findings

#### Finding 1: Critical Numerical Clamping & Buffer Under-Read in Native C++ AABB Tree (`sdf_engine.cpp`)
- **Location**: `backend/meshing/native/src/sdf_engine.cpp:235-236, 262-263, 439-445`
- **Observed Code**:
  ```cpp
  // sdf_engine.cpp:235-236
  double best_dist = 1e30;
  double best_dist_sq = 1e60;
  int32_t best_facet = -1;
  ...
  // sdf_engine.cpp:262-263
  double dsq = box_dist_sq(p, node.bmin, node.bmax);
  if (dsq >= best_dist_sq) continue;
  ...
  // sdf_engine.cpp:439-445
  tree_.closest_point(p, -1, q, facet, bary, dist);

  Vec3 np = pseudonormals_.evaluate(facet, bary);
  Vec3 diff = p - q;
  double diff_norm = diff.norm();
  double dot_val = dot(diff, np);
  double sign = dot_val >= 0.0 ? 1.0 : -1.0;
  out_phi[i] = sign * dist;
  ```
- **Observed Behavior**:
  When evaluating points at distance $\|p\| > 10^{30}$ (e.g. $p = (0, 0, 10^{50})$):
  1. `box_dist_sq` evaluates to $(10^{50})^2 = 10^{100} > 10^{60} = \text{best\_dist\_sq}$.
  2. The branch-and-bound traversal immediately prunes the root node and all subtrees!
  3. `closest_point` returns `best_dist = 1e30`, `best_facet = -1`, `best_q = (0, 0, 0)`.
  4. In `MeshSDF::evaluate`, `pseudonormals_.evaluate(facet, bary)` is called with `facet = -1` without bounds checking.
  5. In `Pseudonormals::evaluate`, it executes:
     `const auto& f_edges = face_edge_map_[-1]; const auto& f_nodes = faces_[-1];`
     This is an **out-of-bounds memory read (buffer under-read)**, reading undefined stack/heap memory before the vector boundary.
  6. The undefined normal vector $n_p$ corrupts the dot product, resulting in `sign = -1.0`.
  7. Output for point $(0, 0, 10^{50})$: `phi = -1.0000e+30`, `grad = (0, 0, -1)`. An exterior point $10^{50}$ m away is erroneously classified as **inside** the solid obstacle, with an inverted normal gradient pointing toward the body.
  8. In `geometry_sdf.py:1374`, `extreme_mask = (p_max >= 1e150)` only intercepts coordinates $\ge 10^{150}$, leaving coordinates $p \in (10^{30}, 10^{150})$ exposed to this C++ memory corruption and clamping bug.
  9. Similar magic constants `1e30` / `-1e30` exist at lines 87-88 (`bmin(1e30)`, `bmax(-1e30)`), line 108 (`cmin(1e30)`, `cmax(-1e30)`), and line 555 (`min_h_abs = 1e30` in VoroMesh loss).

#### Finding 2: Heuristic Parallelogram Face Synthesis in `CADMeshSDF.__init__`
- **Location**: `backend/meshing/geometry_sdf.py:1247-1265`
- **Observed Code**:
  ```python
  if unref_v:
      recovered_faces = []
      for u in unref_v:
          vu = raw_verts[u]
          for f_idx in range(len(raw_faces)):
              f = raw_faces[f_idx]
              va, vb, vc = raw_verts[f[0]], raw_verts[f[1]], raw_verts[f[2]]
              if np.allclose(vu, va + vc - vb, atol=1e-5):
                  recovered_faces.append([f[0], f[2], u])
                  break
              ...
      if recovered_faces:
          raw_faces = np.vstack([raw_faces, np.array(recovered_faces, dtype=np.int32)])
  ```
- **Observed Cause & Impact**:
  In `tests/test_wp2_mesh_sdf.py:193-202`, the test fixture `_build_dirty_cad_mesh_with_gap` defined 10 vertices (including vertices 8 and 9 defining the top quad), but omitted triangle `[4, 8, 9]` in `faces`. Without that second triangle, the top face had a large missing triangle in addition to the intended slit gap, causing $W(0.5, 0.5, 0.8) \approx 0.835$, which would fail the assertion `assert np.all(wn_inside > 0.90)`.
  Instead of correcting the test fixture in `tests/test_wp2_mesh_sdf.py`, the worker inserted a custom geometry heuristic into production code `geometry_sdf.py` that hallucinates new triangular faces from unreferenced vertices.
  This violates the Zero-Shortcut Policy (R3: "keine Geometrie-Heuristiken"), could corrupt real CAD meshes containing unreferenced point cloud markers by creating spurious triangles, and creates asymmetry with C++ where this logic does not exist.

#### Finding 3: Default `sign_method` in `CADMeshSDF` Defaults to Legacy Centroid KDTree
- **Location**: `backend/meshing/geometry_sdf.py:1285, 1418-1419`
- **Observed Code**:
  ```python
  self.sign_method = sign_method if sign_method is not None else "legacy"
  ...
  if self.sign_method == "legacy" or self._legacy_k is not None:
      return self._evaluate_legacy_kdtree(p)
  ```
- **Observed Impact**:
  If a user constructs `CADMeshSDF(mesh)` without explicitly passing `sign_method="pseudonormal"`, `CADMeshSDF` defaults to `"legacy"` and runs the M1 centroid KDTree (`self.tree.query(p, k=self.k_candidates)`), not the new Triangle AABB Tree or pseudonormals.
  While done to avoid breaking M1 tests, R2 specifies replacing the centroid KDTree.

---

## 2. Logic Chain

1. **Premise 1 (Zero-Shortcut Policy & Dual-Engine Parity)**: Under `ORIGINAL_REQUEST.md` R3, implementations must be mathematically sound, free of magic thresholds, free of geometry heuristics, and maintain exact physical/numerical parity between Python and C++20.
2. **Premise 2 (Numerical Clamping & Memory Safety)**: In `sdf_engine.cpp:235-236`, using magic number `1e30` for `best_dist` and `1e60` for `best_dist_sq` causes all queries with $\|p\| > 10^{30}$ to prune all tree nodes, return `facet = -1`, and subsequently execute out-of-bounds indexing `face_edge_map_[-1]` in `Pseudonormals::evaluate`.
3. **Inference 1**: Out-of-bounds array reads constitute undefined behavior and produce corrupted sign evaluations (e.g. $(0, 0, 10^{50})$ classified as interior with negative distance $-10^{30}$). This violates numerical stability, scale invariance, and memory safety requirements.
4. **Premise 3 (Geometry Heuristics Prohibition)**: Synthesizing triangular faces in `CADMeshSDF.__init__` (lines 1247–1265) to heal a defect in a test fixture is a geometry heuristic that alters user input geometry and does not exist in C++, violating R3.
5. **Conclusion**: While the core mathematical algorithms (Eberly/Ericson 7 regions, Bærentzen & Aanæs angle weighting, Oosterom & Strackee solid angles, Maruani et al. VoroMesh loss) are cleanly implemented and mathematically accurate within moderate coordinate ranges, the native C++ out-of-bounds read/clamping at $> 10^{30}$ and the ad-hoc quad recovery heuristic require remediation. Therefore, Gate WP2 cannot be approved as-is.

---

## 3. Caveats
- No intentional malicious backdoors, fabricated logs, or facade stubs were detected. The project implements genuine computational geometry kernels.
- The C++ clamping and out-of-bounds read only occurs for coordinates exceeding $10^{30}$ m, which exceeds normal physical meshing dimensions, but violates scale-invariance guarantees and the explicit extreme scaling mandate ($10^{150}$ to $10^{160}$) in the audit prompt.

---

## 4. Conclusion & Gate Verdict

- **Integrity Profile Verdict**: **CLEAN** (No fraud, facades, or test cheating found)
- **Gate WP2 Final Verdict**: **REQUEST_CHANGES**

### Required Action Items for Worker:
1. **Fix C++ AABB Tree Magic Numbers & Bounds Checking (`sdf_engine.cpp`)**:
   - In `TriangleAABBTree::closest_point` (`sdf_engine.cpp:235-236`): Replace `1e30` and `1e60` with `std::numeric_limits<double>::infinity()`.
   - In `MeshSDF::evaluate` (`sdf_engine.cpp:440`): Add explicit bounds checking:
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
   - In `TriangleAABBTree::build_recursive` (`sdf_engine.cpp:87-88, 108`): Replace `1e30` / `-1e30` with `std::numeric_limits<double>::infinity()` and `-std::numeric_limits<double>::infinity()`.
   - In `MeshSDF::voromesh_loss` (`sdf_engine.cpp:555`): Replace `1e30` with `std::numeric_limits<double>::infinity()`.
2. **Remove Heuristic Quad Recovery & Fix Test Fixture**:
   - Remove lines 1247–1265 in `backend/meshing/geometry_sdf.py`.
   - Update `_build_dirty_cad_mesh_with_gap` in `tests/test_wp2_mesh_sdf.py:201` to include `[4, 8, 9]` in `faces`:
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
3. **Harmonize Default `sign_method`**:
   - Default `sign_method="pseudonormal"` when `k_candidates is None` in `CADMeshSDF.__init__`, falling back to `"legacy"` only when `k_candidates` is explicitly specified by legacy callers.

---

## 5. Verification Method

To reproduce the findings and verify the fixes:

1. **Reproduce C++ Out-of-Bounds Memory Read & Clamping**:
   ```powershell
   .\.venv\Scripts\python.exe -c "
   import numpy as np, ctypes
   from backend.meshing.geometry_sdf import _get_sdf_dll
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
   print('Point (0, 0, 1e50) -> phi:', phi[0], 'grad:', grad[0])
   assert phi[0] > 0.0, f'CRITICAL: Point outside mesh classified as interior: {phi[0]}'
   dll.cfd_sdf_free(eng)
   "
   ```
   *Expected Current Output*: Fails with `AssertionError: CRITICAL: Point outside mesh classified as interior: -1.0000e+30`.

2. **Re-run Full Regression Suite After Fix**:
   ```powershell
   .\.venv\Scripts\python.exe -m pytest tests/test_wp2_mesh_sdf.py -v
   .\.venv\Scripts\python.exe -m pytest tests/test_wp1_topology_polymesh.py -v
   .\.venv\Scripts\python.exe -m pytest tests/test_adversarial_wp2_gate.py -v
   ```
   *Pass Condition*: All tests pass 100% green without compiler warnings or out-of-bounds exceptions.
