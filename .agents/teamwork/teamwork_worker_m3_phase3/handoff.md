# Phase 3 Remediation Handoff Report: Removal of Heuristics & Mathematical Shortcuts (K3, K4, K9, M6, H10)

## 1. Observation
1. **K3 (Polycube Bounding-Box Bypass)**:
   - **File**: `backend/meshing/mesh_engine.py:346-378`
   - **Observed Behavior**: `_detect_oriented_box_axes` clustered surface face normals into Cartesian eigenvectors $\mathbf{u}_0, \mathbf{u}_1, \mathbf{u}_2$. For orthogonal polyhedra with re-entrant concave corners (e.g., L-profile or U-profile), all outward face normals align strictly with $\pm \hat{\mathbf{x}}, \pm \hat{\mathbf{y}}, \pm \hat{\mathbf{z}}$. Consequently, `_detect_oriented_box_axes` returned rotation matrix $R$, routing non-box geometries to `_mesh_oriented_box`. This meshed the entire convex bounding box, returning volume $V = 4.0$ for an L-profile of true geometric volume $V = 3.0$ (a 33.3% volume inflation).
2. **K4 (Compatibility Shield Silencing External Fluid)**:
   - **File**: `backend/meshing/mesh_engine.py:167-171`
   - **Observed Code**:
     ```python
     is_solid_only = (
         (config is None and self.config is None)
         or (active_cfg.domain_mode == "solid_only")
         or (
             active_cfg.domain_mode == "external_fluid"
             and active_cfg.farfield_padding is None
             and active_cfg.farfield_bbox is None
         )
     )
     ```
   - **Observed Behavior**: The schema default is `domain_mode = "external_fluid"`. However, when callers invoked `mesh_cad_file(cad_path)` without explicit `farfield_padding` or `farfield_bbox`, the shield silently bypassed external fluid meshing and produced a solid interior mesh, masking regressions and bypassing wind-tunnel generation.
3. **K9 (Curved B-Rep Flattening in STEP Parser)**:
   - **File**: `backend/geometry/parsers/step_parser.py:84-95`
   - **Observed Behavior**: In `STEPParser.parse(engine="auto")`, pure-Python `_parse_with_brep` was attempted first. `_parse_with_brep` connects boundary vertex loops with planar triangle fans without querying surface curvature. On `BladeFullRev1CFD.stp`, `auto` produced 599 triangles with surface area $240,239\text{ mm}^2$, whereas OpenCASCADE/Gmsh produced 464,820 triangles with surface area $312,557\text{ mm}^2$ — losing $23.1\%$ of the physical surface area silently.
4. **M6 & H10 (Hard Clamps, Silent Overwrites, and Magic Tuning Factors)**:
   - **File**: `backend/meshing/native/src/octree_hexcore.cpp:362`
     - Clamped octree refinement depth to 4: `int32_t max_depth = std::clamp(options.max_depth, 0, 4);`.
   - **File**: `backend/meshing/native/src/octree_hexcore.cpp:947-954`
     - Overwrote collapsed/degenerate cut cells with full uncut cell volume:
       `if (vol_sum > 1e-15) { ... } else { cell_vols[c] = nodes[active_leaves[c]].v_bg; }`.
   - **File**: `backend/meshing/mesh_engine.py:406-414`
     - Silently coarsened resolution when $nx \cdot ny \cdot nz > 500,000$, destroying GCI refinement hierarchies.
   - **File**: `backend/meshing/mesh_engine.py:1019-1068`
     - Arbitrary heuristic multipliers `threshold = 0.11 * h_min` (tuned to blade 0.39% volume error) and `aspect > 2.0 -> h0 / 3.0`.
5. **Compilation and Test Execution**:
   - Native library built cleanly: `backend/meshing/cfd_mesher_native.dll` (MSVC CMake 3.28.3, C++20 Release mode, exit code 0).
   - Test suites:
     - `pytest tests/test_mesh_engine.py`: 17 passed in 0.81s.
     - `pytest tests/test_wp4_multiphysics_hexcore.py`: 24 passed in 201.58s.

---

## 2. Logic Chain
1. **K3**:
   - *Premise*: A true rectangular cuboid has exactly 6 planar faces aligned pairwise in opposite directions, and its Gauss divergence surface integral closed volume $V_{\text{surf}} = \frac{1}{3}\sum (\mathbf{c}_f \cdot \mathbf{S}_f)$ equals its oriented bounding box volume $V_{\text{bbox}} = L_0 \cdot L_1 \cdot L_2$.
   - *Inference*: Re-entrant polyhedra (L-profile, U-profile, stepped blocks) have face normal clusters that align with coordinate axes, but their closed surface volume is strictly smaller than $V_{\text{bbox}}$, and they have more than 6 directional face clusters.
   - *Action*: In `_detect_oriented_box_axes`, compute $V_{\text{surf}}$ and $V_{\text{bbox}}$. Check $|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$ and require `dir_clusters == 6`. If not satisfied, reject the box shortcut ($R = \text{None}$) and route through full level-set unstructured meshing.
   - *Verification*: L-profile ($V = 3.0$) meshes with volume $3.008$ (0.28% discretization error at $h=0.10$), never inflating to bounding box volume $4.0$.
2. **K4**:
   - *Premise*: Declarative configuration contracts must be respected. When `domain_mode="external_fluid"` is selected, the solver must mesh the fluid volume around the obstacle, not silently convert to solid.
   - *Inference*: The compatibility shield masked whether tests actually verified external fluid or solid meshing.
   - *Action*: Eliminate the shield condition. Default to `FarfieldPadding()` to automatically construct the 16L wind tunnel box when parameters are omitted in `external_fluid` mode. Add explicit `domain_mode="solid_only"` to legacy benchmark tests that explicitly test solid body properties.
3. **K9**:
   - *Premise*: STEP files contain mathematical definitions of surfaces. Planar faces (`PLANE`) are faithfully represented by polygon loops. Curved surfaces (`B_SPLINE_SURFACE`, `CYLINDRICAL_SURFACE`, `SPHERICAL_SURFACE`, etc.) cannot be represented by flat polygon fans without losing geometric curvature.
   - *Inference*: Using pure-Python B-Rep on curved geometry produces unphysical flat polygons.
   - *Action*: Implement `_has_curved_entities(path: Path) -> bool`. When curved entities exist, require OpenCASCADE/Gmsh (`_parse_with_gmsh`). If Gmsh is unavailable on curved models, raise `RuntimeError` rather than silently distorting geometry. For planar models (cuboids), permit exact B-Rep boundary representation.
4. **M6 & H10**:
   - *Premise*: A scientific CFD mesher must not cap refinement depth to 4, must not silently overwrite degenerate cut cells with uncut background volumes, must not silently coarsen grids above 500k cells, and must not employ magic numbers.
   - *Action*:
     - In `octree_hexcore.cpp:362`, increase max depth clamp to 16.
     - In `octree_hexcore.cpp:947-954`, assign `0.0` volume to degenerate/collapsed cells ($|\text{vol\_sum}| \le 10^{-15}$) while preserving exact signed volumes and centroids.
     - In `mesh_engine.py`, replace 500k silent scaling with configurable `max_cells` (default 2,000,000) that raises explicit `ValueError`.
     - In `_mesh_complex_surface`, remove `0.11 * h_min` and `h0 / 3.0` scaling, utilizing isotropic cell spacing $dx = dy = dz = h_0$ and physical levelset threshold $\phi \le 0$.

---

## 3. Caveats
- **Gmsh Requirement for Curved STEPs**: Faithful ingestion of curved STEP CAD models (e.g. `BladeFullRev1CFD.stp`) requires Gmsh/OpenCASCADE installed in the Python environment (`python -m pip install gmsh`). If Gmsh is not installed and a user attempts to load a curved STEP model, the parser now raises an explicit `RuntimeError` rather than returning a flattened facet approximation.
- **Ansys Background Throttling**: Execution on host machine must maintain `OMP_NUM_THREADS=2` and `MKL_NUM_THREADS=2` to avoid CPU starvation with background Ansys simulations.

---

## 4. Conclusion
All Phase 3 heuristics, mathematical shortcuts, and silent error shields have been eliminated:
1. **K3**: Polycube bounding-box shortcut eliminated for non-box geometries (L-profile volume verified $\approx 3.0$).
2. **K4**: Compatibility shield removed; `mesh_hexcore` defaults to `FarfieldPadding()` for `external_fluid`.
3. **K9**: OpenCASCADE/Gmsh is enforced for curved STEP models, preventing 23% surface area loss on turbine blades.
4. **M6 & H10**: Octree refinement depth clamp expanded to 16, degenerate volume overwriting replaced with strict 0.0 volume, 500k RAM cap replaced with explicit exception, and magic numbers eliminated.
5. All native C++20 and Python tests pass cleanly (17/17 in `test_mesh_engine.py`, 24/24 in `test_wp4_multiphysics_hexcore.py`).

---

## 5. Verification Method
To independently verify:
```powershell
# Set CPU throttling for host background process
$env:OMP_NUM_THREADS="2"
$env:MKL_NUM_THREADS="2"

# 1. Run MeshEngine unit and audit test suite
.\.venv\Scripts\python.exe -m pytest tests/test_mesh_engine.py

# 2. Run WP4 Multiphysics Hexcore end-to-end suite
.\.venv\Scripts\python.exe -m pytest tests/test_wp4_multiphysics_hexcore.py
```
- **Files to inspect**:
  - `backend/meshing/mesh_engine.py`: Lines 150-185 (K4), 346-385 (K3), 406-418 (M6), 1019-1068 (M6/H10).
  - `backend/geometry/parsers/step_parser.py`: Lines 84-140 (K9).
  - `backend/meshing/native/src/octree_hexcore.cpp`: Line 362 (depth clamp), Lines 947-954 (silent volume overwrite).
- **Invalidation Condition**:
  - If meshing an L-profile returns volume $> 3.5$ or equals 4.0, K3 has regressed.
  - If `STEPParser` flattens `BladeFullRev1CFD.stp` to $< 300{,}000\text{ mm}^2$ surface area, K9 has regressed.
