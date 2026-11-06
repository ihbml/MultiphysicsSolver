# Test Infrastructure & Verification Architecture: CFDSolver Meshing Subsystem

## 1. Overview & Test Philosophy

The test infrastructure of CFDSolver enforces a rigorous, requirement-driven, opaque-box testing methodology adhering strictly to:
- **Zero-Shortcut Policy**: No dummy facades, no heuristics (`len(verts) == 8`), no magic numbers, no silent clamps, and no test-specific bypasses.
- **Dual-Engine Parity**: Python and native C++20 kernels must produce mathematically and physically identical invariants.
- **ASME V&V 20-2009 Standard**: Verification and Validation in Computational Fluid Dynamics and Heat Transfer (especially Grid Convergence Index GCI < 1%).
- **HPC Safety & RAM Protection**: Strict limit `OMP_NUM_THREADS <= 16`, moderate cell counts for unit tests ($N \le 5{,}000$), streaming SoA buffers, and immediate cleanup via RAII / `try-finally` to ensure concurrent stability alongside industrial CFD solvers.

---

## 2. 4-Tier Testing Methodology

The verification hierarchy is organized into four distinct tiers:

```
┌─────────────────────────────────────────────────────────────┐
│ Tier 4: Real-World CAD Benchmarks                           │
│ (TestCAD.stp, BladeFullRev1CFD.stp, snappyMultiRegionHeater) │
├─────────────────────────────────────────────────────────────┤
│ Tier 3: Cross-Feature Combinations                          │
│ (External Fluid + Mosaic + Merging, CHT + 3-Grid GCI)       │
├─────────────────────────────────────────────────────────────┤
│ Tier 2: Boundary & Corner Cases                             │
│ (Empty Farfield BBox, Non-Convex Bodies, α < 0.05 Slivers)  │
├─────────────────────────────────────────────────────────────┤
│ Tier 1: Feature Coverage                                    │
│ (Domain Modes, Farfield Patches, Octree, Mosaic, CHT, GCI)  │
└─────────────────────────────────────────────────────────────┘
```

### Tier 1: Feature Coverage (Core Functionality)
Covers the fundamental capabilities specified in `PROJECT.md` and `ORIGINAL_REQUEST.md`:
1. **Domain Modes (F16)**:
   - `external_fluid`: External aerodynamics bounding box $\Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$ with automatic patch assignment.
   - `internal_fluid`: Pipe/manifold flow within interior boundary walls.
   - `solid_only`: Structural mechanics / thermal conduction inside solid bodies ($\phi \le 0$).
   - `cht_coupled`: Multi-region coupled fluid-solid conjugate heat transfer domain with collocated interfaces.
2. **Farfield Patch Generation (F17)**:
   - Automated boundary face classification into `inlet` ($x_{\min}$), `outlet` ($x_{\max}$), `bottom` ($z_{\min}$), `top` ($z_{\max}$), and `sides` ($y_{\min}, y_{\max}$).
   - CAD surface patch preservation (`SurfaceMesh.patches` $\to$ body `wall` patches).
3. **3D 2:1 Balanced Octree Hexcore (F18)**:
   - Recursive adaptive spatial subdivision based on local metric $h(\mathbf{x})$ and narrow-band SDF $\phi(\mathbf{x})$.
   - Strict 2:1 balancing in all 26 spatial directions ($\Delta L \le 1$ across faces, edges, vertices).
4. **ANSYS Mosaic / PUMA Polyhedral Interface (F19)**:
   - Replacement of unrefined interface quad face with 4 coplanar sub-faces.
   - Zero hanging nodes in FVM graph; transition cells tagged as `CellType::POLYHEDRON = 42`.
   - Relative Gauss closure $\|\sum \mathbf{S}_f\| / \sum \|\mathbf{S}_f\| < 10^{-11}$.
5. **Cut-Cell Snapping along $\nabla\phi$ (F20)**:
   - Surface conformity projection: $\mathbf{x}_k^{\text{target}} = \mathbf{x}_k - \phi(\mathbf{x}_k) \frac{\nabla \phi}{\|\nabla \phi\|^2}$.
   - Scale-invariant AMIPS distortion energy guard: $E_{\text{AMIPS}}(T) < 10^4$ and strictly $V_i > 0$.
6. **Small-Cell Merging & Berger-Giuliani WSRD (F21)**:
   - Topological agglomeration for sliver cut-cells with $V_{\text{cut}} < 0.20 V_{\text{bg}}$ into neighbor cell along $\nabla\phi$.
   - Berger & Giuliani (2024) Weighted State Redistribution weights $w_{ij} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ for $0.20 \le \alpha_i < 0.50$ satisfying partition of unity $\sum_j w_{ij} = 1.0$.
7. **Multiphysics CHT Multi-Region Coupling (F22)**:
   - Multi-region tagging via `cell_regions` ($0 = \text{fluid}, 1 = \text{solid}$).
   - Conformal collocated twin-face extraction (`extract_cht_twin_faces`) into `coupledWall_fluid` and `coupledWall_solid` (or `coupledWall_regionA_to_B` / `coupledWall_regionB_to_A`) with opposite normals $\mathbf{S}_{f, \text{solid}} = -\mathbf{S}_{f, \text{fluid}}$ and contiguous boundary ordering.
8. **Automated 3-Grid GCI Hierarchy (F23)**:
   - Deterministic 3-grid generation (coarse $h_3$, medium $h_2$, fine $h_1$) with refinement ratio $r \in [1.3, 1.5]$ ($r \approx \sqrt{2}$).
   - Identical patch topology preservation across all 3 grid levels.
   - ASME V&V 20 GCI calculation ensuring $\text{GCI}_{\text{fine}} < 1.0\%$.

### Tier 2: Boundary & Corner Cases (Robustness & Edge Conditions)
Verifies error handling, geometric singularities, and extreme aspect ratios:
1. **Invalid / Degenerate Farfield Bounding Boxes**:
   - Inverted bounding box ($x_{\min} \ge x_{\max}$) raises `ValueError`.
   - Zero-span geometry ($L_z = 0$) rejected with descriptive error.
   - Body geometry penetrating or exceeding farfield bbox detected and escalated.
2. **Non-Convex & Re-entrant Geometries**:
   - Re-entrant cavities, thin fins, and sharp trailing edges tested without centroid fan distortion.
   - Opposing wall collision detection preventing nodes from snapping across narrow gaps ($d < 0.4 d_{\text{opp}}$).
3. **Extreme Sliver Cut-Cells ($\alpha < 0.05$)**:
   - Tiny cut-cells ($V_i = 10^{-15} V_{\text{bg}}$) successfully absorbed by topological agglomeration.
   - Zero negative cell volumes, zero orphan faces, and no dangling vertices.
4. **Thin Boundaries & High Aspect Ratio**:
   - Slender elements with aspect ratio $\text{AR} > 100$ maintain positive volume and valid over-relaxation.
5. **Orientation & Cap-Bug Hardening**:
   - Cap2 orientation flip bug (from `LMArena/generate.cpp`) guarded: consistent normal orientation $\mathbf{S}_f \cdot (\mathbf{c}_f - \mathbf{C}_i) > 0$.

### Tier 3: Cross-Feature Combinations (Subsystem Interaction)
Tests complex multi-feature interactions:
1. **External Fluid + Mosaic Octree + Small-Cell Merging**:
   - Full pipeline execution from STEP surface mesh through 2:1 octree, Mosaic polyhedral splitting, cut-cell snapping, and agglomeration.
   - Verifies that merged cells adjacent to Mosaic transition cells remain valid polyhedra with exact Gauss closure $< 10^{-11}$.
2. **Conjugate Heat Transfer + 3-Grid GCI Hierarchy**:
   - Multi-region domain meshed at 3 successive refinement levels ($r = \sqrt{2}$).
   - Confirms that both fluid and solid regions, as well as twin-face coupled patches, scale uniformly and maintain consistent GCI convergence.

### Tier 4: Real-World CAD Benchmarks (Industrial Validation)
End-to-end execution on real-world reference engineering models:
1. **`TestCAD.stp` (Rotated 200 mm Cube)**:
   - ISO 10303-21 STEP AP214 format, $10^\circ$ rotation around Z-axis.
   - Analytical CFD volume at `scale=0.001`: $V = 0.008\text{ m}^3$ (exact).
   - Sanitization of `#35 = GEOMETRIC_CURVE_SET` sketch entities.
   - Invariant validation: $V_i > 0$, relative Gauss closure $< 10^{-11}$.
2. **`BladeFullRev1CFD.stp` (36-Blade Axial Turbine Rotor)**:
   - ISO 10303-21 STEP AP242, 99,257 lines, 5.28 MB.
   - Complex freeform aerofoil curvature, multi-blade passages.
   - Ingestion at `scale=0.001`, non-face entity filtration (9 `DRAUGHTING_MODEL` items).
   - Hexcore domain meshing with moderate $h_0$ under RAM-safe protocol.
3. **`snappyMultiRegionHeater` (Multi-Region OpenFOAM CHT Reference)**:
   - 5-region conjugate heat transfer geometry (`bottomAir`, `topAir`, `heater`, `leftSolid`, `rightSolid`).
   - Conformal multi-region coupling, twin-face patch generation, and thermal flux conservation.

---

## 3. Test Architecture & Execution Protocol

### 3.1 HPC & Thread-Safety Policy
- **Thread Limit**: All test executions must explicitly enforce:
  ```powershell
  $env:OMP_NUM_THREADS = "16"
  ```
- **RAM Protection**: Unit tests must maintain moderate cell counts ($N \le 5{,}000$). Real-world CAD benchmarks use scale-invariant $h_0$ to prevent exceeding memory bounds during concurrent execution with ANSYS Fluent.

### 3.2 Invariant Verification Thresholds
Every generated mesh must satisfy the following mathematical invariants:
1. **Positive Volume**:
   $$\forall i \in [0, N_c): \quad V_i > 0$$
2. **Relative Gauss Divergence Closure**:
   $$\max_{i \in [0, N_c)} \frac{\|\sum_{f \in c_i} \sigma_{if} \mathbf{S}_f\|_2}{\sum_{f \in c_i} \|\mathbf{S}_f\|_2} < 10^{-11}$$
3. **Projection Collinearity**:
   $$\frac{\|\boldsymbol{\delta}_f \times \mathbf{d}_f\|_2}{\|\boldsymbol{\delta}_f\|_2 \|\mathbf{d}_f\|_2} < 10^{-12}$$
4. **Over-Relaxation Identity**:
   $$\frac{|\boldsymbol{\delta}_f \cdot \mathbf{S}_f - \|\mathbf{S}_f\|_2^2|}{\|\mathbf{S}_f\|_2^2} < 10^{-11} \quad \text{for } \theta_f < 89^\circ$$
5. **Upper-Triangular Ordering**:
   $$\forall f \in [0, n_{\text{int}}): \quad \text{owner}[f] < \text{neighbour}[f]$$

### 3.3 Test Runner Command
```powershell
$env:OMP_NUM_THREADS = "16"
.\.venv\Scripts\python.exe -m pytest tests/test_wp4_multiphysics_hexcore.py -v
```

---

## 4. Test Suite Inventory

| Test Suite File | Work Package | Primary Scope | Test Classes |
| :--- | :---: | :--- | :--- |
| `tests/test_wp1_topology_polymesh.py` | WP1 | Topology core, cell types, face connectivity, polyMesh export | 10 tests |
| `tests/test_wp2_mesh_sdf.py` | WP2 | Mesh-SDF, BVH/AABB tree, pseudonormals, winding numbers, VoroMesh | 5 tests |
| `tests/test_wp3_robust_2d_mesher.py` | WP3 | 2D Delaunay/DistMesh, Shoelace CCW, ear-clipping, area conservation | 12 tests |
| `tests/test_wp4_multiphysics_hexcore.py` | WP4 | 3D Multiphysics Domain Meshing & GCI Hierarchy | 6 test classes (detailed below) |

### Test Classes in `tests/test_wp4_multiphysics_hexcore.py`:
1. `TestDomainModesAndFarfield`:
   - Validates `external_fluid`, `internal_fluid`, `solid_only`, and `cht_coupled` mode configurations.
   - Validates automated farfield patch assignment (`inlet`, `outlet`, `bottom`, `top`, `sides`) and body patch inheritance.
   - Tests boundary conditions: empty bbox, invalid aspect ratio, geometry outside farfield.
2. `TestMosaicPolyHexcore`:
   - Validates 2:1 octree balancing across adjacent cells ($\Delta L \le 1$).
   - Validates conformal quad face split into 4 coplanar sub-faces on unrefined cells.
   - Validates zero hanging nodes in DiFVM graph and `CellType::POLYHEDRON = 42`.
   - Validates relative Gauss closure $< 10^{-11}$.
3. `TestCutCellSnappingAndMerging`:
   - Validates surface node projection along $\nabla\phi$.
   - Validates AMIPS distortion energy monitoring and rollback on inversion.
   - Validates small-cell agglomeration for cut-cells with $V_{\text{cut}} < 0.20 V_{\text{bg}}$.
   - Validates Berger & Giuliani (2024) WSRD weights $w_{ij}$ for $0.20 \le \alpha_i < 0.50$ and partition of unity $\sum_j w_{ij} = 1.0$.
4. `TestMultiphysicsCHTCoupling`:
   - Validates multi-region integer tagging in `cell_regions`.
   - Validates twin-face boundary patch extraction (`coupledWall_fluid`, `coupledWall_solid` / `coupledWall_regionA_to_B`).
   - Validates exact opposite normals $\mathbf{S}_{f, B} = -\mathbf{S}_{f, A}$, reversed vertex ordering, and contiguous boundary slices.
5. `TestGCIHierarchy`:
   - Validates deterministic 3-grid generation (coarse, medium, fine with $r \in [1.3, 1.5]$).
   - Validates strict boundary patch and topological consistency across all 3 levels.
   - Validates ASME V&V 20 GCI calculation with $\text{GCI}_{\text{fine}} < 1.0\%$.
6. `TestCADBenchmarks`:
   - Real-world benchmark: `TestCAD.stp` (analytical CFD volume $V = 0.008\text{ m}^3$ at `scale=0.001`).
   - Real-world benchmark: `BladeFullRev1CFD.stp` (36-blade turbine rotor at `scale=0.001`).
   - Real-world benchmark: `snappyMultiRegionHeater` (multi-region CHT geometry).
   - Invariant verification: strictly positive volumes $V_i > 0$ and relative Gauss closure $< 10^{-11}$.

---

## 5. Gate Pass Criteria for Gate WP4
- **100% Pass** on all tests in `tests/test_wp4_multiphysics_hexcore.py`.
- **Zero Regressions**: All 579 existing baseline tests remain 100% green.
- **Unanimous 3:0 Approval**: The independent 3-agent audit committee (Paper-Fidelity Challenger, Code Auditor, Mathematics Auditor) unanimously approves Gate WP4.
