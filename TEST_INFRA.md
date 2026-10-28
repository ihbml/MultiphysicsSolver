# Test Infrastructure & Verification Architecture: In-Memory Multiphysics Mesher Backbone

**Author:** E2E Testing Architect (`test_writer_e2e_1`)  
**Target Module:** `backend/meshing` (Rust & Modern C++ Core with Python/NumPy Transparent Fallback)  
**Test Suite Root:** `tests/`  
**Master Test Suite:** `tests/test_mesher.py`  
**Status:** Authoritative Test Infrastructure Baseline  

---

## 1. Test Philosophy & Design Principles

The verification framework for the CFDSolver In-Memory Multiphysics Mesher Backbone adheres to the rigorous standards of **ASME V&V 10** (*Verification and Validation in Computational Solid Mechanics*) and **ASME V&V 20** (*Verification and Validation in Computational Fluid Dynamics and Heat Transfer*), combined with the mathematical invariants established in contemporary peer-reviewed CFD literature:
- **DiFVM:** Du, Li, Xu & Wang (2026): *Vectorized Graph-Based Finite Volume Solver for Differentiable CFD*, arXiv:2603.15920.
- **Eulerian SPH-FVM:** Wang, Haidn & Hu (2025): *Boundary Condition from FVM into Eulerian SPH*, Comput. Phys. Commun. 307, 109429.
- **OPMIS:** Zhang, Ming, Wang & Liang (2025): *Adaptive SPH-FVM Method with Optimized Particle-Mesh Interpolation*, Int. J. Multiphase Flow 189, 105256.
- **DistMesh:** Persson & Strang (2004): *A Simple Mesh Generator in MATLAB*, SIAM Review 46(2), 329–345.

```
+---------------------------------------------------------------------------------------------------+
|                                 4-Tier Verification Hierarchy                                     |
|                                                                                                   |
|  [Tier 1: Unit & Feature Invariants]                                                              |
|   └── Positive cell volumes (V_i > 0), closed-cell Gauss divergence (< 10^-11),                   |
|       orthogonality consistency (delta_f + k_f == S_f), RCM bandwidth reduction.                  |
|                                                                                                   |
|  [Tier 2: Boundary, Adversarial & Robustness Cases]                                              |
|   └── Near-90 deg non-orthogonality traps, SPH truncated support detection (div r < 0.75 d),      |
|       CAD sketch wireframe & 2D curve filtering (GEOMETRIC_CURVE_SET, DRAUGHTING_MODEL).          |
|                                                                                                   |
|  [Tier 3: Multiphysics & Dynamic Cross-Feature Workflows]                                         |
|   └── SPH 0th-order wall consistency (A_iS + sum V_j grad W_ij = 0, < 10^-10),                   |
|       OPMIS linear field moment reproduction (|f_interp - f| < 10^-11),                           |
|       Moving mesh ALE variable-stiffness Laplacian smoothing & degenerate cell detection.        |
|                                                                                                   |
|  [Tier 4: Real-World CAD Reference & Complex Aerodynamics E2E]                                    |
|   └── Rotated 200 mm cube (TestCAD.*) analytical BBox & volume verification,                      |
|       Complex turbine blade (BladeFullRev1CFD / BladeRev1CFD.obj) watertight 2-manifold meshing.  |
|                                                                                                   |
|  [Regression Protection Baseline]                                                                 |
|   └── 100% PASS across all existing 277 repository regression tests.                              |
+---------------------------------------------------------------------------------------------------+
```

### Core Tenets:
1. **Independent Validation**: The test suite is authored and maintained independently from the implementation code (`backend/meshing/`). Tests are derived strictly from physical and mathematical invariants, never tailored to accommodate implementation shortcuts.
2. **Authoritative Ground Truth Derivation**:
   - Geometric conservation: $\max_i \|\sum_{f \in \text{faces}(i)} \mathbf{S}_f\| < 10^{-11}$.
   - Orthogonality decomposition: $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ to machine precision ($< 10^{-14}$).
   - Boundary wall SPH closure: $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$ to $< 10^{-10}$.
   - OPMIS linear field moment reproduction: $L_\infty < 10^{-11}$ for arbitrary linear fields $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$.
   - Real-world CAD ground truth: $200.0\text{ mm}$ rotated cube ($V = 0.008\text{ m}^3$, $A = 0.24\text{ m}^2$, $\text{BBox} = [-0.115846, 0.115846]^2 \times [0.0, 0.2]\text{ m}$) and aerodynamic blade ($V = 1.883373 \times 10^5\text{ mm}^3$, $\chi = 2$, 0 open edges).
3. **No Facade Testing**: Every test exercises real geometry parsing, signed distance evaluations, Delaunay triangulation, graph topology construction, and numerical solves. No tests pass trivially without exercising real logic.
4. **Adversarial & Fault-Tolerant Verification**: Explicit tests for 2D CAD sketches embedded in 3D STEP files, extreme mesh motions inducing degenerate cells, singular face-to-center alignments, and boundary particle truncations.

---

## 2. Feature Inventory & Verification Matrix

The test suite systematically covers all 20 features defined in `PROJECT.md` across 4 testing tiers:

| # | Feature | Test Module | Function Name | Invariant / Acceptance Criterion | Tier Coverage |
|---|---|---|---|---|---|
| 1 | Continuous SDF Engine (2D & 3D) | `test_mesher.py` | `test_strict_positive_volumes` | Signed distance $\phi < 0$ inside, $\|\nabla \phi\| \approx 1$ | Tier 1, 4 |
| 2 | SurfaceMesh to SDF Bridge | `test_mesher.py` | `test_e2e_meshing_testcad_cube` | Fast cKDTree query, exact distance to triangular facets | Tier 1, 4 |
| 3 | CAD Auxiliary Wireframe Filter | `test_mesher.py` | `test_cad_sketch_line_filtering` | Discards `GEOMETRIC_CURVE_SET`, `DRAUGHTING_MODEL` | Tier 2, 4 |
| 4 | Curvature & Proximity Refinement | `test_mesher.py` | `test_inflation_boundary_layers` | Sizing function $h(\mathbf{x})$ adapts to surface curvature | Tier 1, 2 |
| 5 | DistMesh Unstructured Mesher Kernel | `test_mesher.py` | `test_strict_positive_volumes` | Spring equilibrium relaxation, valid Delaunay triangulation | Tier 1, 4 |
| 6 | Boundary Inflation & Buffer Layers | `test_mesher.py` | `test_inflation_boundary_layers` | Prismatic/quad wall layers with geometric growth ratio $r$ | Tier 1, 2 |
| 7 | Reverse Cuthill-McKee (RCM) | `test_mesher.py` | `test_rcm_bandwidth_reduction` | BFS reordering reduces matrix bandwidth $\beta_1 < \beta_0$ | Tier 1, 3 |
| 8 | DiFVM Graph-Topology & SoA | `test_mesher.py` | `test_gauss_divergence_theorem_closure` | Flat SoA arrays: `owner`, `neighbour`, $\mathbf{S}_f$, $\mathbf{c}_f$, $V_i$ | Tier 1, 4 |
| 9 | Non-Orthogonal Decomposition | `test_mesher.py` | `test_non_orthogonality_consistency` | Vectorized decomposition: $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ | Tier 1, 2 |
| 10 | Error-Map & Stabilization Mask | `test_mesher.py` | `test_mesh_error_map_completeness` | $\theta_f$, $\text{skew}_f$, $\text{AR}_c$, $\text{mask} \in \{0, 1, 2, 3\}$ | Tier 1, 2 |
| 11 | FoamMesh & C-ABI Compatibility | `test_mesher.py` | `test_gauss_divergence_theorem_closure` | Bit-for-bit parity with FoamMesh data layout | Tier 1, 4 |
| 12 | SPH Incomplete Support Detection | `test_mesher.py` | `test_sph_boundary_detection_div_r` | Truncated boundary particles identified by $\text{div }\mathbf{r} < 0.75 d$ | Tier 2, 3 |
| 13 | Virtual Wall Boundary Vector ($\mathbf{A}_{iS}$) | `test_mesher.py` | `test_sph_wall_consistency_A_iS` | $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0} \quad (< 10^{-10})$ | Tier 3 |
| 14 | OPMIS Taylor Interpolation (P2M/M2P) | `test_mesher.py` | `test_opmis_linear_moment_reproduction` | Exact reproduction of linear fields $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$ ($L_\infty < 10^{-11}$) | Tier 3 |
| 15 | Moving Mesh ALE & Laplacian Smoothing | `test_mesher.py` | `test_moving_mesh_ale_laplacian_smoothing` | Variable-stiffness Laplacian smoothing preserves boundary layers | Tier 3 |
| 16 | Degenerate Cell Detection | `test_mesher.py` | `test_moving_mesh_ale_laplacian_smoothing` | Identifies $V_c \le 0$ and extreme distortion under motion | Tier 2, 3 |
| 17 | Pure-Python/NumPy Fallback Engine | `test_mesher.py` | `test_strict_positive_volumes` | Standalone execution via NumPy/SciPy without compiler | Tier 1, 4 |
| 18 | Native C++20 / Rust Kernels | `test_mesher.py` | `test_strict_positive_volumes` | Zero-copy C-ABI bridge when native libraries are compiled | Tier 1, 4 |
| 19 | Complex CAD Meshing (TestCAD & Blade) | `test_mesher.py` | `test_e2e_meshing_testcad_cube`, `test_e2e_meshing_blade_full_rev1` | Watertight meshing of 200 mm cube and turbine blade | Tier 4 |
| 20 | Comprehensive Verification Suite | `test_mesher.py` | All 13 tests in suite | 100% pass on mesher suite + 277 regression tests | Tier 1–4 |

---

## 3. Mathematical Invariant Formulations & Tolerances

### 3.1 Strict Positive Cell Volumes ($V_i > 0$)
- **Physical Rationale**: Negative or zero cell volumes cause unphysical negative densities/pressures and solver divergence.
- **Formulation**:
  - 2D (Triangles/Quads): Signed polygon area via cross product:
    $$V_i = \frac{1}{2} \sum_{k=0}^{m-1} (x_k y_{k+1} - x_{k+1} y_k) > 0$$
  - 3D (Polyhedra): Divergence theorem decomposition:
    $$V_i = \frac{1}{3} \sum_{f \in \text{faces}(i)} \sigma_{if} (\mathbf{c}_f \cdot \mathbf{S}_f) > 0$$
- **Verification Threshold**: $\min_i V_i > 10^{-15}\text{ m}^3$, finite and non-NaN.

### 3.2 Exact Gauss Divergence Closed Cell Sum ($\max_i \|\sum \mathbf{S}_f\| < 10^{-11}$)
- **Physical Rationale**: Gauss's divergence theorem applied to a uniform vector field requires that the closed surface integral of outward area normals vanishes identically:
  $$\oint_{\partial \Omega_i} d\mathbf{S} = \sum_{f \in \text{faces}(i)} \sigma_{if} \mathbf{S}_f = \mathbf{0}$$
- **Evaluation**:
  $$\mathbf{R}_i = \sum_{f \in \text{owner}(i)} \mathbf{S}_f - \sum_{f \in \text{neighbour}(i)} \mathbf{S}_f$$
- **Verification Threshold**: $\max_{i=0 \dots N_c - 1} \|\mathbf{R}_i\| < 10^{-11}\text{ m}^2$.

### 3.3 Non-Orthogonality Decomposition Consistency ($\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$)
- **Physical Rationale**: Over-relaxed decomposition (DiFVM / OpenFOAM) splits face area $\mathbf{S}_f$ into orthogonal component $\boldsymbol{\Delta}_f$ parallel to cell-centre vector $\mathbf{d} = \mathbf{c}_N - \mathbf{c}_O$ and non-orthogonal correction $\mathbf{k}_f$:
  $$\boldsymbol{\Delta}_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}} \mathbf{d}, \quad \mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f$$
- **Verification Threshold**: $\max_{f \in \text{internal}} \|\boldsymbol{\Delta}_f + \mathbf{k}_f - \mathbf{S}_f\| < 10^{-14}$.

### 3.4 Eulerian SPH Wall Consistency ($\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$)
- **Physical Rationale** (Wang, Haidn & Hu, 2025): Near walls, incomplete particle support truncates the kernel. The virtual boundary closure vector $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ restores 0th-order consistency.
- **Support Incompleteness Detector**: $\text{div }\mathbf{r}_i = -\sum_j V_j (\mathbf{r}_i - \mathbf{r}_j) \cdot \nabla_i W_{ij} < 0.75 d$.
- **Verification Threshold**: $\|\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij}\| < 10^{-10}$ for all boundary particles.

### 3.5 OPMIS Linear Field Moment Reproduction
- **Physical Rationale** (Zhang, Ming, Wang & Liang, 2025): Hybrid particle-mesh transfers (P2M and M2P) must reproduce affine/linear fields $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$ without numerical diffusion.
- **Verification Threshold**:
  $$\max |f_{\text{P2M}}(\mathbf{x}_c) - f_{\text{exact}}(\mathbf{x}_c)| < 10^{-11}$$
  $$\max |f_{\text{M2P}}(\mathbf{x}_p) - f_{\text{exact}}(\mathbf{x}_p)| < 10^{-11}$$

### 3.6 RCM Bandwidth Reduction
- **Physical Rationale**: Minimizes cache misses in sparse matrix-vector multiplications during FVM linear solves.
- **Evaluation**: Bandwidth $\beta = \max_{(i, j) \in E} |i - j|$.
- **Verification Threshold**: $\beta_{\text{RCM}} < \beta_{\text{initial}}$ with $\ge 30\%$ bandwidth reduction on structured and unstructured grids.

---

## 4. CAD Reference Samples & Sketch Filtering Specification

### 4.1 Rotated 200 mm Cube (`TestCAD.*`)
- **Dimensions**: $200.0\text{ mm} \times 200.0\text{ mm} \times 200.0\text{ mm}$ ($0.2\text{ m} \times 0.2\text{ m} \times 0.2\text{ m}$).
- **Rotation**: $10.0^\circ$ counter-clockwise around Z-axis.
- **CAD Bounding Box**: $X, Y \in [-115.845593, 115.845593]\text{ mm}$, $Z \in [0.0, 200.0]\text{ mm}$.
- **CFD Bounding Box**: $X, Y \in [-0.115846, 0.115846]\text{ m}$, $Z \in [0.0, 0.200000]\text{ m}$.
- **Analytical Volume**: $V = 0.008\text{ m}^3$ ($8.0 \times 10^6\text{ mm}^3$).
- **Analytical Surface Area**: $A = 0.240\text{ m}^2$ ($2.4 \times 10^5\text{ mm}^2$).
- **Topology**: Watertight 2-manifold closed shell, $\chi = V - E + F = 2$, 0 open edges.

### 4.2 Auxiliary Sketch & Wireframe Filtering
- **Discovery**: `TestCADSTPAP214.stp` contains standalone sketch entities in a `GEOMETRIC_CURVE_SET` (`Line1`–`Line4`, entities `#121`–`#124`, origin point `#219`).
- **Discovery**: `BladeFullRev1CFD.stp` contains 9 `DRAUGHTING_MODEL` entities with `CAMERA_MODEL_D3`, `VIEW_VOLUME`, and `PLANAR_BOX` representations.
- **Filtering Requirement**: The mesher ingests only topological 2-manifold shells (`CLOSED_SHELL` inside `MANIFOLD_SOLID_BREP`), silently discarding 1D curves, wireframe sketches, drafting cameras, and non-face entities without crashing or generating degenerate elements.

### 4.3 Turbine Blade (`BladeFullRev1CFD.*` & `BladeRev1CFD.obj`)
- **Reference Geometry**: `BladeRev1CFD.obj` (30,832 vertices, 61,660 triangular faces, 92,490 edges).
- **Topology Ground Truth**: Watertight 2-manifold genus-0 surface ($\chi = 2$), 0 open edges, 0 multi-edges.
- **Signed Volume**: $V = 1.883373 \times 10^5\text{ mm}^3$ ($0.0001883373\text{ m}^3$).
- **Bounding Box (CAD mm)**:
  - $X \in [-77.364941, 4.631219]\text{ mm}$ (span $81.996\text{ mm}$)
  - $Y \in [-29.945928, 86.810913]\text{ mm}$ (span $116.757\text{ mm}$)
  - $Z \in [-100.000000, 200.000000]\text{ mm}$ (span $300.000\text{ mm}$)

---

## 5. Master Test Suite Architecture (`tests/test_mesher.py`)

The master test suite consists of 13 independent unit and end-to-end tests:

```text
tests/test_mesher.py
├── test_strict_positive_volumes
├── test_gauss_divergence_theorem_closure
├── test_non_orthogonality_consistency
├── test_sph_boundary_detection_div_r
├── test_sph_wall_consistency_A_iS
├── test_opmis_linear_moment_reproduction
├── test_mesh_error_map_completeness
├── test_inflation_boundary_layers
├── test_rcm_bandwidth_reduction
├── test_moving_mesh_ale_laplacian_smoothing
├── test_degenerate_cell_detection
├── test_cad_sketch_line_filtering
├── test_e2e_meshing_testcad_cube
└── test_e2e_meshing_blade_full_rev1
```

---

## 6. Test Execution Instructions

### Full Regression Suite Run:
```powershell
.venv\Scripts\pytest.exe tests/ backend/geometry/tests/ -v
```

### Dedicated Mesher Test Suite:
```powershell
.venv\Scripts\pytest.exe tests/test_mesher.py -v
```

### Keyword Filtered Invariant Runs:
```powershell
.venv\Scripts\pytest.exe tests/test_mesher.py -k "gauss or orthogonality or volume" -v
.venv\Scripts\pytest.exe tests/test_mesher.py -k "sph or opmis" -v
.venv\Scripts\pytest.exe tests/test_mesher.py -k "cad or blade" -v
```
