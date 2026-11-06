# WP4 Survey & Architecture Analysis: Domain Modes, Farfield, CAD Assets, Multiphysics CHT, GCI Hierarchy & Test Suite

**Author**: `explorer_survey_wp4_2` (teamwork_explorer)  
**Date**: 2026-11-04  
**Workspace**: `G:\Datein\Projects\CFDSolver`  
**Mission**: Deep investigation of Work Package 4 (WP4) requirements, codebase touchpoints, CAD benchmark assets, CHT multi-region coupling, deterministic GCI 3-grid hierarchy, and test suite architecture.

---

## Executive Summary

Work Package 4 (WP4) implements true 3D fluid and multiphysics domain meshing for CFDSolver. Current code (`backend/meshing/mesh_engine.py:118-228` and `backend/meshing/native/src/distmesh.cpp:259-320`) contains known defects identified in `SUGGESTIONS_BACKEND.md` (§4.1 Audit finding K4 and K5):
1. **Heuristic classification**: Cube detection relying on `len(verts) == 8 and len(faces) == 12` (`mesh_engine.py:120`).
2. **Inverted domain meshing**: In level-set mode (`mesh_engine.py:303`), cells with $\phi(\mathbf{x}) < \text{threshold}$ (the solid interior) were retained rather than meshing the external fluid volume $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$ ($\phi(\mathbf{x}) > 0$).
3. **C++ Stub**: `cfd_mesher_generate_from_surface` (`distmesh.cpp:259-399`) ignored surface triangles and merely filled the bounding box with up to $40^3$ Cartesian hexes with arbitrary resolution clamps (`std::clamp(nx, 2, 40)`).
4. **Patch Collapsing**: All boundary faces were lumped into a single `"wall"` patch (`difvm_topology.py:683, 904`), losing Farfield boundaries (`inlet`, `outlet`, `bottom`, `top`, `sides`) and CAD B-Rep surface patches.

This report establishes the complete architecture and verification plan to resolve these findings and deliver WP4 with zero shortcuts.

---

## 1. Domain Modes & Farfield Architecture

### 1.1 Declarative Configuration Schema in `MeshingConfig`

Currently, `MeshingConfig` (`backend/meshing/models.py:120-158`) defines only:
```python
@dataclass
class MeshingConfig:
    sizing: CurvatureSizingConfig = field(default_factory=CurvatureSizingConfig)
    inflation: Optional[InflationConfig] = None
    quality: QualityConfig = field(default_factory=QualityConfig)
    rcm_reorder: bool = True
    cht_multiregion: bool = False
    cyclic_symmetry: bool = False
    cyclic_angle_deg: float = 0.0
    distmesh_iters: int = 45
```

For WP4, `MeshingConfig` must be extended with explicit domain modes, padding parameters, and refinement zones:

```python
DomainMode = Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"]

@dataclass
class FarfieldPadding:
    """Relative farfield padding multipliers based on characteristic length L_ref."""
    upstream: float = 5.0      # -X direction (inlet): 5 * L_ref
    downstream: float = 10.0   # +X direction (outlet): 10 * L_ref
    lateral_y: float = 5.0     # ±Y direction (sides): 5 * L_ref
    lateral_z: float = 5.0     # ±Z direction (bottom/top): 5 * L_ref
    ground_plane: bool = False # If True, bottom snaps to body z_min (no padding below)

@dataclass
class MeshingConfig:
    # Existing fields
    sizing: CurvatureSizingConfig = field(default_factory=CurvatureSizingConfig)
    inflation: Optional[InflationConfig] = None
    quality: QualityConfig = field(default_factory=QualityConfig)
    rcm_reorder: bool = True
    cht_multiregion: bool = False
    cyclic_symmetry: bool = False
    cyclic_angle_deg: float = 0.0
    distmesh_iters: int = 45

    # WP4 Extensions
    domain_mode: DomainMode = "external_fluid"
    farfield_padding: Optional[Union[float, Dict[str, float], FarfieldPadding]] = None
    farfield_bbox: Optional[Union[BoundingBox, Tuple[float, float, float, float, float, float]]] = None
    refinement_zones: List[Dict[str, Any]] = field(default_factory=list)
```

### 1.2 Farfield Bounding Box Computation Logic

Given a scaled CAD surface mesh $\mathcal{S}$ with bounding box $[\mathbf{b}_{\min}, \mathbf{b}_{\max}]$ where $\mathbf{b}_{\min}, \mathbf{b}_{\max} \in \mathbb{R}^3$ and $\text{span} = \mathbf{b}_{\max} - \mathbf{b}_{\min}$:

1. **Characteristic Reference Length $L_{\text{ref}}$**:
   $$L_{\text{ref}} = \begin{cases} \text{span}[0] & \text{if } \text{span}[0] > 10^{-6} \text{ (chord along flow direction X)} \\ \max(\text{span}) & \text{otherwise} \end{cases}$$
2. **Padding Application**:
   If `farfield_padding` is a float $p$, it applies uniformly: upstream = $p$, downstream = $2p$, lateral = $p$.
   Default standard aerodynamic wind-tunnel padding:
   - Upstream ($-X$, Inlet): $5.0 \cdot L_{\text{ref}}$
   - Downstream ($+X$, Outlet): $10.0 \cdot L_{\text{ref}}$
   - Lateral $Y$ ($\pm Y$, Sides): $5.0 \cdot L_{\text{ref}}$
   - Lateral $Z$ ($\pm Z$, Top & Bottom): $5.0 \cdot L_{\text{ref}}$ (if `ground_plane=True`, bottom is $z_{\min} = b_{\min}[2]$)
3. **Bounding Box Extents**:
   $$\begin{aligned}
   x_{\min}^{\text{ff}} &= b_{\min}[0] - \text{padding.upstream} \cdot L_{\text{ref}} \\
   x_{\max}^{\text{ff}} &= b_{\max}[0] + \text{padding.downstream} \cdot L_{\text{ref}} \\
   y_{\min}^{\text{ff}} &= b_{\min}[1] - \text{padding.lateral\_y} \cdot L_{\text{ref}} \\
   y_{\max}^{\text{ff}} &= b_{\max}[1] + \text{padding.lateral\_y} \cdot L_{\text{ref}} \\
   z_{\min}^{\text{ff}} &= \begin{cases} b_{\min}[2] & \text{if ground\_plane} \\ b_{\min}[2] - \text{padding.lateral\_z} \cdot L_{\text{ref}} & \text{otherwise} \end{cases} \\
   z_{\max}^{\text{ff}} &= b_{\max}[2] + \text{padding.lateral\_z} \cdot L_{\text{ref}}
   \end{aligned}$$
   If `farfield_bbox` is explicitly specified by the user, it directly overrides the computed farfield bounding box.

### 1.3 Boundary Patch Classification & Naming Convention

All boundary faces ($\text{neighbour}[f] == -1$) are partitioned into contiguous slices $[start, start + count)$ in `boundaries: Dict[str, Tuple[int, int]]`.

| Face Location | Normal Vector $\mathbf{S}_f$ Orientation | Patch Name | OpenFOAM / polyMesh Type |
| :--- | :--- | :--- | :--- |
| Outer Bounding Box: $x \approx x_{\min}^{\text{ff}}$ | Outward from domain ($-X$) | `inlet` | `patch` |
| Outer Bounding Box: $x \approx x_{\max}^{\text{ff}}$ | Outward from domain ($+X$) | `outlet` | `patch` |
| Outer Bounding Box: $y \approx y_{\min}^{\text{ff}}$ | Outward from domain ($-Y$) | `sides` (or `sides_ymin`) | `patch` / `symmetry` |
| Outer Bounding Box: $y \approx y_{\max}^{\text{ff}}$ | Outward from domain ($+Y$) | `sides` (or `sides_ymax`) | `patch` / `symmetry` |
| Outer Bounding Box: $z \approx z_{\min}^{\text{ff}}$ | Outward from domain ($-Z$) | `bottom` | `wall` / `patch` |
| Outer Bounding Box: $z \approx z_{\max}^{\text{ff}}$ | Outward from domain ($+Z$) | `top` | `patch` / `symmetry` |
| Obstacle / Body Boundary: $\phi(\mathbf{x}) = 0$ | Outward from fluid into solid ($-\nabla\phi$) | Inherited from `SurfaceMesh.patches` (e.g. `suction_side`, `pressure_side`, `hub`, `shroud`), or default `wall` | `wall` |

---

## 2. CAD Benchmark Assets Analysis

The repository contains three primary benchmark CAD assets:

### 2.1 `resources/CADTestSamples/TestCAD.stp`
- **File Format**: ISO 10303-21 STEP (AP214 automotive design).
- **Geometry**: Rotated 200 mm cube, CCW rotation of $10^\circ$ around the Z-axis, base at $Z = 0.0$.
- **Analytical Metrics**:
  * Unscaled CAD volume: $V_{\text{CAD}} = 200^3\text{ mm}^3 = 8{,}000{,}000\text{ mm}^3 = 8 \times 10^{-3}\text{ m}^3$.
  * Analytical CFD volume at `scale=0.001`: $V_{\text{CFD}} = (0.200)^3 = 0.008\text{ m}^3$.
  * Bounding box half-span: $\frac{0.200}{2} (\cos 10^\circ + \sin 10^\circ) = 0.115846\text{ m}$.
  * Bounding box: $X \in [-0.115846, 0.115846]\text{ m}$, $Y \in [-0.115846, 0.115846]\text{ m}$, $Z \in [0.0, 0.200]\text{ m}$.
- **Entity Sanitation**: Contains entity `#35 = GEOMETRIC_CURVE_SET` with 4 2D sketch lines, successfully stripped by `CADFeatureFilter.sanitize`.

### 2.2 `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`
- **File Format**: ISO 10303-21 STEP (AP242 managed model-based 3D engineering).
- **Size**: 99,257 lines (~5.28 MB).
- **Geometry**: Complete 36-blade axial turbine rotor assembly.
- **Scale Factor**: Must be parsed with `scale=0.001` (millimeter to meter conversion).
- **Analytical Metrics**:
  * Solid metal volume ~ $5.56 \times 10^6\text{ mm}^3 = 0.00556\text{ m}^3$.
  * Contains 9 drafting models (`DRAUGHTING_MODEL`) and representation maps, filtered by `CADFeatureFilter`.
  * Also available as single aerodynamic blade: `BladeRev1CFD.obj` (watertight shell, 30,832 vertices, 61,660 faces, volume ~ $187{,}600.6\text{ mm}^3 = 0.0001876\text{ m}^3$).

### 2.3 `OpenFOAM_base/openfoam/tutorials/resources/geometry/snappyMultiRegionHeater`
- **Multi-region CHT Reference Geometry**:
  * `bottomAir.stl.gz` (Fluid domain: cooling air below)
  * `topAir.stl.gz` (Fluid domain: cooling air above)
  * `heater.stl.gz` (Solid domain: heating core element)
  * `leftSolid.stl.gz` (Solid domain: structural heat conductor)
  * `rightSolid.stl.gz` (Solid domain: structural heat conductor)
- **Use Case**: Authoritative reference for multi-region `domain_mode="cht_coupled"`, region tagging, and twin-face boundary patch generation (`extract_cht_twin_faces`).

### 2.4 STEP Ingestion & Tessellation Pipeline in CFDSolver
- Ingestion occurs through `CADGeometryEngine.load(path, step_engine="auto")` (`backend/geometry/pipeline.py:50-113`):
  * Primary engine: `STEPParser` (`backend/geometry/parsers/step_parser.py:28-96`).
  * If `engine == "auto"`: tries pure-Python B-Rep entity graph traversal (`_parse_with_brep`). For curved non-polyhedral surfaces, falls back to Gmsh OpenCASCADE kernel (`_parse_with_gmsh`).
  * Gmsh uses Frontal-Delaunay 2D surface triangulation (`Mesh.Algorithm = 6`) with curvature-adaptive element sizing (`Mesh.MeshSizeFromCurvature`) and chordal deflection control.
  * Extracted entities preserve per-surface patch tags (`patch_sTag` or named CAD patches in `SurfaceMesh.patches`).

---

## 3. Multiphysics CHT Coupling & DiFVMMesh Interface Architecture

### 3.1 Cell Tagging & Storage
- Every cell $c \in \{0, \dots, N_c - 1\}$ is assigned an integer region ID:
  $$\text{cell\_regions}[c] \in \{0, 1, 2, \dots\}$$
  Convention: `0` = Fluid ($\Omega_F$), `1` = Solid ($\Omega_S$), or arbitrary region IDs.
- Stored as `UnstructuredMesh.cell_regions` (`np.int32` array of shape $(N_c,)$) and in native C++ `NativeMeshData::cell_regions` (`std::vector<int32_t>`). Exported via C-ABI: `cfd_mesher_get_cell_regions_ptr`.

### 3.2 Coupled Interface Detection
The coupled interface is the set of internal faces whose owner and neighbour belong to different regions:
$$\Gamma_{FS} = \{ f \in [0, N_{\text{faces}}) \mid \text{neighbour}[f] \ge 0 \;\land\; \text{cell\_regions}[\text{owner}[f]] \ne \text{cell\_regions}[\text{neighbour}[f]] \}$$

### 3.3 Collocated Twin Boundary Patch Extraction (`extract_cht_twin_faces`)
The function `extract_cht_twin_faces(mesh)` (`backend/meshing/difvm_topology.py:1113-1280`) transforms $\Gamma_{FS}$ into standard OpenFOAM collocated twin boundary patches:
1. **Contiguous Face Reindexing**:
   * Block 1: Clean internal faces $[0, n_{\text{clean\_int}})$ where $\text{region}(\text{owner}) == \text{region}(\text{neighbour})$.
   * Block 2: Original boundary faces $[n_{\text{clean\_int}}, n_{\text{clean\_int}} + n_{\text{orig\_bnd}})$.
   * Block 3: Coupled twin patch A (Fluid / Region $R_0$) $[start_a, start_a + n_{\text{coupled}})$.
   * Block 4: Coupled twin patch B (Solid / Region $R_1$) $[start_b, start_b + n_{\text{coupled}})$.
2. **Physical Invariants on Twin Patches**:
   * Patch A: $\text{owner} = \text{owner}[f]$, $\text{neighbour} = -1$, normal $\mathbf{S}_{f, A} = +\mathbf{S}_f$ pointing into solid.
   * Patch B: $\text{owner} = \text{neighbour}[f]$, $\text{neighbour} = -1$, normal $\mathbf{S}_{f, B} = -\mathbf{S}_f$ pointing into fluid.
   * Node ordering: `faces_b` reverses vertex order (`face_nodes[::-1]`) ensuring that the right-hand rule outward normal is mathematically exact.
   * $\mathbf{S}_{f, A} + \mathbf{S}_{f, B} = \mathbf{0}$ enforces exact thermal flux conservation: $-k_F \nabla T_F \cdot \mathbf{n} = -k_S \nabla T_S \cdot \mathbf{n}$.
3. **Baffle Tracking**:
   Recorded in `mesh.baffles` with twin face pairs $(f_A, f_B)$ for solver gather-scatter operations.

---

## 4. 3D Mosaic Poly-Hexcore & Octree Transition Architecture

### 4.1 2:1 Balanced Octree Generation
- Construction of Cartesian octree over the fluid domain ($\phi(\mathbf{x}) > 0$).
- Strict 2:1 balance condition: Adjacent leaf cells cannot differ by more than 1 octree level ($\Delta L \le 1$).

### 4.2 Polyhedral Transition Interface without Hanging Nodes (Mosaic / PUMA Principle)
- When a refined cell (level $L+1$) borders an unrefined neighbor cell (level $L$), the shared interface quad is split into 4 coplanar sub-quads.
- In naive meshers, this creates hanging nodes / T-junctions, destroying FVM flux conservation.
- In Mosaic Poly-Hexcore:
  * The coarse cell replaces its single quad face with the **4 coplanar sub-quad faces**.
  * The coarse cell now has $5 + 4 = 9$ faces.
  * It is classified as an explicit **general polyhedron** (`CellType::POLYHEDRON = 42`).
  * In `UnstructuredMesh.validate()`:
    $$\sum_{f \in c} \mathbf{S}_f = \sum_{f \in \text{unsplit}} \mathbf{S}_f + \sum_{k=1}^4 \mathbf{S}_{f, k} = \mathbf{0}$$
    Gauss closure is satisfied to machine precision ($< 10^{-14}$).
  * Non-orthogonality arising from centroid offset $\mathbf{d}_f = \mathbf{C}_{\text{fine}} - \mathbf{C}_{\text{coarse}}$ is stored in $\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\delta}_f$ and corrected via `MeshErrorMap` (`k_f_comp`, `m_skew`).

---

## 5. Cut-Cell Snapping & Small-Cell Merging (Berger & Giuliani 2024)

### 5.1 Cut-Cell Volume Fraction $\alpha_i$
For background Cartesian cells cut by the zero level-set $\phi(\mathbf{x}) = 0$:
$$\alpha_i = \frac{V_i}{V_{\text{bg}, i}} \in (0, 1]$$
where $V_{\text{bg}, i} = h^3$.

### 5.2 Small-Cell Merging ($\alpha_i < 0.20$)
- Severe cutting causes $V_i \to 0$, which restricts explicit CFD time steps ($\Delta t \propto V_i \to 0$).
- All cut cells with $\alpha_i < 0.20$ are merged topologically with their largest internal fluid neighbor cell $j$ (in direction $-\nabla\phi$):
  * Shared face $f_{ij}$ is dissolved.
  * New merged volume: $V_j' = V_j + V_i$.
  * Volume-weighted centroid: $\mathbf{C}_j' = \frac{V_j \mathbf{C}_j + V_i \mathbf{C}_i}{V_j + V_i}$.
  * The merged cell becomes a valid closed polyhedron (`CellType::POLYHEDRON = 42`).

### 5.3 Berger-Giuliani WSRD Weights ($0.20 \le \alpha_i < 0.50$)
For non-merged cut cells, Weighted State Redistribution (WSRD) stabilizes hyperbolic fluxes up to $\text{CFL} = 1$:
$$w_{ij} = \frac{1}{N_i - 1} \left( 1 - \frac{\alpha_j}{\alpha_{\text{target}}} \right), \quad \alpha_{\text{target}} = 0.50$$
where $N_i$ is the number of face neighbors of cell $i$.
Weights satisfy the partition of unity $\sum_{j \in \mathcal{N}(i)} w_{ij} = 1.0$, stored in `mesh.error_map["wsrd_alpha"]` and `mesh.metadata["wsrd_weights"]`.

---

## 6. Deterministic 3-Grid GCI Hierarchy ($\text{GCI} < 1\%$)

### 6.1 Mathematical Formulation (ASME V&V 20 / Celik et al. 2008)
Grid Convergence Index (GCI) provides a standardized, rigorous measure of spatial discretization uncertainty:
1. **Three Grids**:
   - Fine grid (Grid 1): Characteristic element size $h_1 = h_0$
   - Medium grid (Grid 2): Characteristic element size $h_2 = r \cdot h_1$
   - Coarse grid (Grid 3): Characteristic element size $h_3 = r \cdot h_2 = r^2 \cdot h_1$
   - Refinement ratio: $r = \sqrt{2} \approx 1.414 \in [1.3, 1.5]$.
2. **Apparent Order of Convergence $p$**:
   Given key scalar functional $\phi$ (e.g. enclosed volume, boundary flux, total surface area) on all three grids:
   $$p = \frac{1}{\ln(r)} \left| \ln \left| \frac{\phi_3 - \phi_2}{\phi_2 - \phi_1} \right| \right|$$
3. **Extrapolated Value & Relative Errors**:
   $$\phi_{\text{ext}}^{21} = \frac{r^p \phi_1 - \phi_2}{r^p - 1}$$
   $$e_a^{21} = \left| \frac{\phi_1 - \phi_2}{\phi_1} \right|$$
4. **Fine-Grid GCI**:
   $$\text{GCI}_{\text{fine}}^{21} = \frac{F_s \cdot e_a^{21}}{r^p - 1}$$
   where $F_s = 1.25$ is the ASME safety factor for three-grid studies.
   **Target Acceptance Criterion**: $\text{GCI}_{\text{fine}}^{21} < 1.0\%$.

### 6.2 Topological Consistency Requirements
To guarantee valid GCI comparisons:
- Exact same domain bounding box across all 3 grids.
- Identical patch names in `boundaries.keys()`.
- Identical `cell_regions` partition.
- Monotonic cell count progression: $N_{\text{coarse}} < N_{\text{medium}} < N_{\text{fine}}$.

---

## 7. Verification & Test Suite Infrastructure

### 7.1 Resource Safety Directives
- **Thread Control**: `OMP_NUM_THREADS=16` must be enforced across all tests and subprocesses (protecting background Ansys Fluent run on the machine).
- **RAM Protection (~95% RAM in use)**:
  * Unit test mesh sizes must be moderate: $N \le 5{,}000$ cells per test mesh.
  * Avoid large temporary arrays in memory; run garbage collection / `cfd_mesher_free` explicitly.
  * High-resolution production meshes (e.g. $10^7$ cells) are deferred to dedicated benchmark runs when Ansys is complete.

### 7.2 Architecture of `tests/test_wp4_multiphysics_hexcore.py`

The test suite must comprise 6 dedicated test classes:

```
tests/test_wp4_multiphysics_hexcore.py
├── TestWP4DomainModesAndFarfield
│   ├── test_external_fluid_farfield_padding_and_bbox
│   ├── test_farfield_boundary_patch_naming_and_orientation
│   ├── test_internal_fluid_mode
│   └── test_solid_only_mode
├── TestWP4MosaicPolyHexcore
│   ├── test_octree_2to1_balance
│   ├── test_polyhedral_interface_without_hanging_nodes (CellType == 42)
│   ├── test_closed_cell_gauss_divergence_closure (< 1e-11)
│   └── test_strict_upper_triangular_owner_neighbour
├── TestWP4CutCellSmallCellMerging
│   ├── test_cut_cell_volume_fraction
│   ├── test_small_cell_merging_threshold_020
│   └── test_berger_giuliani_wsrd_weights
├── TestWP4MultiphysicsCHTCoupling
│   ├── test_multiregion_cell_tagging
│   ├── test_conformal_twin_boundary_patches
│   └── test_twin_face_normal_opposition_and_flux_conservation
├── TestWP4DeterministicGCIHierarchy
│   ├── test_3grid_hierarchy_generation (r = 1.414)
│   ├── test_topological_patch_consistency_across_grids
│   └── test_gci_metric_below_1_percent
└── TestWP4CADBenchmarks
    ├── test_cad_benchmark_rotated_cube_stp (TestCAD.stp, scale=0.001)
    ├── test_cad_benchmark_turbine_rotor_stp (BladeFullRev1CFD.stp, scale=0.001)
    └── test_openfoam_benchmark_multiregion_heater (snappyMultiRegionHeater)
```

---

## 8. Gap Analysis & Implementation Roadmap

| Component | Current State | Required WP4 Implementation |
| :--- | :--- | :--- |
| `MeshingConfig` | Missing `domain_mode`, `farfield_padding`, `farfield_bbox` | Add `DomainMode` literal, `FarfieldPadding`, `farfield_bbox`, `refinement_zones` |
| `mesh_engine.py` | `len(verts) == 8` heuristic, meshes solid interior in level set | Remove heuristic, implement Farfield bounding box & fluid volume meshing ($\phi > 0$) |
| Hexcore Octree | Equidistant Cartesian box grid in C++ (`distmesh.cpp:259`) | Adaptive 2:1 balanced octree with polyhedral transition (`CellType::POLYHEDRON = 42`) |
| Cut-Cell & Merging | Hardcoded threshold `0.11 * h_min` in `mesh_engine.py:300` | AMIPS / SDF snapping + Small-Cell Merging ($\alpha < 0.20$) + Berger-Giuliani WSRD |
| CHT Twin Faces | Reindexed in Python (`difvm_topology.py:1113`), stub in C++ | Expose full contiguous CHT extraction in C++ and Python with twin patches |
| GCI Hierarchy | No automated 3-grid generator | Implement `generate_gci_mesh_hierarchy(cad, h0, r=1.414)` and GCI calculation |
| Test Suite | 579 tests pass | Implement `tests/test_wp4_multiphysics_hexcore.py` under 16-thread and RAM safety limits |

---

*End of analysis.md*
