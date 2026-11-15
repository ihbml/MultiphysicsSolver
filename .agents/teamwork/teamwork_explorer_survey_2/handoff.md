# Handoff Report: Survey Explorer 2 (Phase 3 & Phase 4)

**Handoff Type:** Hard (Survey and Specification Complete)  
**Agent:** Survey Explorer 2 (`teamwork_explorer`)  
**Working Directory:** `g:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_2\`  
**Target Recipient:** Orchestrator (`e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`)  
**Reference Document:** `survey_report.md`  

---

## 1. Observation

Direct observations from source inspection and audit review:

1. **K3 (Polycube Bounding-Box Shortcut):**
   - In `backend/meshing/mesh_engine.py:263-276`, `_detect_oriented_box_axes` checks only that face normals cluster into 3 orthogonal axes. Any orthogonal concave geometry (L-profile, U-profile) satisfies this.
   - In `mesh_engine.py:141-142`, when $R \ne \text{None}$, `_mesh_oriented_box` fills the entire bounding box with a regular Cartesian grid.
   - Result: An L-profile of volume $3.0$ is meshed with volume $4.0$.

2. **K4 (Compatibility Shield):**
   - In `backend/meshing/mesh_engine.py:127`:
     `is_solid_only = (config is None and self.config is None) or (active_cfg.domain_mode == "solid_only")`
   - Even though `MeshingConfig.domain_mode` defaults to `"external_fluid"` (`models.py:143`), calling `mesh_cad_file` without arguments forces solid meshing.

3. **K9 (STEP Engine Default):**
   - In `backend/geometry/parsers/step_parser.py:84-95`, `engine="auto"` attempts `_parse_with_brep` first.
   - `_parse_with_brep` (lines 347-353) uses a perimeter triangle fan `[v0, v_i, v_{i+1}]`. Curved surfaces are flattened into flat polygon fans across boundary vertices without interior discretization.
   - Audit measurement: On `BladeFullRev1CFD.stp`, `auto` generated 599 triangles with area $240,239\text{ mm}^2$ versus Gmsh's 464,820 triangles with area $312,557\text{ mm}^2$ (23% surface area missing).

4. **M6 & H10 (Magic Numbers, Clamps, Overwrites):**
   - `backend/meshing/native/src/octree_hexcore.cpp:362`: `int32_t max_depth = std::clamp(options.max_depth, 0, 4);` (hardcoded 4-level cap).
   - `octree_hexcore.cpp:947-954`: When `vol_sum <= 1e-15`, line 952 sets `cell_vols[c] = nodes[active_leaves[c]].v_bg;` (silently overwriting collapsed/inverted cut-cells with full background cell volume).
   - `mesh_engine.py:319-333`: `max_cells_limit = 500_000` silently downscales grid dimensions $(nx, ny, nz)$ by $(500,000 / \text{total})^{1/3}$.
   - `mesh_engine.py:937-940`: `dx = h0 / 3.0 if aspect > 2.0 else h0`.
   - `mesh_engine.py:981-983`: `threshold = 0.11 * h_min # (0.39% error)`.

5. **H3 (2D-Schwerpunkte):**
   - `backend/meshing/difvm_topology.py:1244`: `pyr_c = tf["centre"] * 0.75 + c_approx * 0.25`.
   - `backend/meshing/native/src/difvm_mesh.cpp:454`: `Vec3 pyr_c = tf.centre * 0.75 + c_approx * 0.25;`.
   - For a 2D quadrilateral with vertices $(0,0)-(4,0)-(1,1)-(0,1)$, the analytical centroid is $(1.4, 0.4)$. The $0.75/0.25$ formula yields $(1.41875, 0.3875)$.

6. **H4 (Explizite Polyeder-Topologie):**
   - `backend/meshing/difvm_topology.py:307` and `difvm_mesh.cpp:107`: Unconditionally assigns `CellType::HEXAHEDRON` whenever vertex count is 8, misclassifying flat 2D octagons.
   - `backend/meshing/moving_mesh.py:65-113`: `_build_node_adjacency` relies on `n_nodes == 8` heuristics.
   - `moving_mesh.py:279-283`: `deform_mesh` discards original `mesh.faces`, `owner`, `neighbour`, and `cell_types`, calling `compute_difvm_topology(new_points, mesh.cells)`. For octree meshes with cut polyhedra, volume shrinks from $63.5$ to $31.2$.

7. **H2 (Celik-GCI):**
   - `backend/meshing/gci.py:148-158`: Computes apparent order without convergence ratio $R = \epsilon_{21}/\epsilon_{32}$. For divergent sequence $(1.0, 1.1, 1.105)$, reports apparent order $p = 8.64$ and $\text{GCI} = 1.8\%$, masking divergence.
   - Assumes uniform refinement $r_{21} = r_{32}$ and omits Celik et al. Eq. (10) transcendental iteration $q(p)$.

8. **H7 (Supermesh-AMI):**
   - `backend/meshing/difvm_topology.py:1788-1815`: `compute_ami_weights` takes `source_areas` and `target_areas` but never uses them. Weights are computed from inverse centroid distance within a hardcoded radius $0.05\text{ m}$. Unequal source faces ($1.0\text{ m}^2$ and $10^{-6}\text{ m}^2$) receive identical $0.5$ weights.

9. **H8 (SPH-FVM Coupling & P2M):**
   - `backend/meshing/coupling.py:368-370`: Defines $A_{iS} = -\sum V_j \nabla W_{ij}$ on boundary particles, making the balance check $\|A_{iS} + \sum V_j \nabla W_{ij}\| = 0$ tautological.
   - `coupling.py:530-538`: `interpolate_p2m` takes unweighted `np.mean` of 8 particles, ignoring particle volume $V_p$ and cell volume $V_c$.

10. **M1 & M2 (Aspect Ratio & wsrd_alpha):**
    - `difvm_topology.py:1471-1480`, `difvm_mesh.cpp:694-719`, and `octree_hexcore.cpp:1381-1406`: Computes aspect ratio from all vertex-pair distances ($\max(d)/\min(d)$). A unit cube $[0,1]^3$ yields $\sqrt{3} \approx 1.732$ instead of $1.0$.
    - `difvm_topology.py:1493` and `difvm_mesh.cpp:687`: Normalizes `wsrd_alpha` by `median(pos_vols)` rather than background octree cell volume $V^{\text{bg}}$, causing uncut fine-grid cells to trigger WSRD small-cell stabilization.

---

## 2. Logic Chain

1. **Box Detection & Shortcuts (K3, K4):**
   - From Obs. 1: Because axis-aligned orthogonal polyhedra have 3 normal directions, `_detect_oriented_box_axes` cannot distinguish an L-profile from a solid box. Adding a surface-to-bounding-box volume verification ($|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$) immediately filters out concave geometries without breaking genuine box detection.
   - From Obs. 2: The condition `(config is None and self.config is None)` overrides the documented default `domain_mode="external_fluid"`. Removing this shield and defaulting farfield parameters to `FarfieldPadding()` aligns behavior with declarative configuration.

2. **CAD Ingestion Fidelity (K9):**
   - From Obs. 3: Because pure B-Rep does not tessellate interior curved surfaces, `BladeFullRev1CFD.stp` loses 23% of its surface area. Routing `engine="auto"` to Gmsh / OpenCASCADE as primary, and falling back to B-Rep only when Gmsh is absent AND all entities are planar, guarantees geometric preservation.

3. **Zero-Shortcut & Fail-Fast Numerics (M6, H10):**
   - From Obs. 4: Clamping octree depth to 4 and silently overwriting degenerate cut-cells with $V_{\text{bg}}$ hides mesh flaws from the solver. Removing the clamp (allowing depth up to 16) and reporting true zero/negative volume ensures `validate()` detects inversions. Replacing the 500k silent scaling with an explicit exception guarantees deterministic GCI grid hierarchies.

4. **Topological & Mathematical Correctness (H3, H4, H2, H7, H8, M1, M2):**
   - From Obs. 5: In 2D, the sub-triangle centroid is $\frac{1}{3}(v_1 + v_2 + c_{\text{approx}}) = \frac{2}{3} c_f + \frac{1}{3} c_{\text{approx}}$. Replacing $0.75 / 0.25$ yields the exact analytical centroid $(1.4, 0.4)$ for the test quad $(0,0)-(4,0)-(1,1)-(0,1)$.
   - From Obs. 6: Preserving explicit face arrays `mesh.faces` and owner/neighbour connectivity during ALE deformation prevents the destruction of polyhedra.
   - From Obs. 7: Checking $R = \epsilon_{21}/\epsilon_{32} \ge 1.0$ detects divergence per Celik et al. (2008), preventing misleading convergence reports.
   - From Obs. 8: Calculating weights from geometric polygon overlap areas ($w_{ij} = A_{ij}/A_i$) guarantees strict area conservation $\sum w \cdot A = A$.
   - From Obs. 9: Integrating boundary area vectors $A_{iS}$ over actual FVM wall faces provides a genuine geometric check; volume-weighted P2M ensures mass conservation.
   - From Obs. 10: Defining aspect ratio via bounding box / edge lengths yields $1.0$ for a unit cube; normalizing $V_i$ by background cell volume $V^{\text{bg}}$ prevents false small-cell classification.

---

## 3. Caveats

- **External CAD dependencies:** Testing Gmsh on STEP files requires `gmsh` Python package and OpenCASCADE runtime.
- **Python environment:** Host environment has a permission restriction on `C:\Users\Ben Ludwig\AppData\Local\Python\pythoncore-3.14-64\python.exe`. Testing must use an available Python interpreter or rely on unit test execution during Phase 3 implementation.
- **Background Process:** Active background Ansys execution requires strict adherence to `OMP_NUM_THREADS=2` and grids $\le 30,000$ cells during verification.

---

## 4. Conclusion

All 12 items across Phase 3 and Phase 4 have been completely mapped, diagnosed, and mathematically resolved. Drop-in code modifications and diff blueprints have been authored in `survey_report.md`. The implementer can directly apply these changes without ambiguity.

---

## 5. Verification Method

1. **K3 & K4 Verification:**
   - Meshing an L-profile ($[0,2]\times[0,2]\times[0,1]$ minus $[1,2]\times[1,2]\times[1,2]$) in `solid_only` mode yields mesh volume $\approx 3.0$ (within 2%), not $4.0$.
   - Meshing a sphere CAD without config arguments creates an external fluid farfield box with patches `inlet`, `outlet`, `farfield_wall`, `wall`.
2. **K9 Verification:**
   - Ingesting `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp` with `engine="auto"` yields surface area $\ge 310,000\text{ mm}^2$ and $> 400,000$ triangles.
3. **H3 Verification:**
   - Construct single quadrilateral cell with points $(0,0), (4,0), (1,1), (0,1)$. Verify `cell_centres` equals `[1.4, 0.4, 0.0]` within $10^{-12}$.
4. **M1 & M2 Verification:**
   - Compute `aspect_ratio` on a unit cube $[0,1]^3$. Verify value equals exactly $1.0$.
5. **H2 Verification:**
   - Evaluate GCI on divergent sequence $(1.0, 1.1, 1.105)$. Verify `convergence_type == "divergence"`, `apparent_order_p` is NaN, and `gci_fine` is infinite.
6. **H7 Verification:**
   - Compute AMI weights between source face of area $1.0$ and source face of area $0.01$ overlapping target face of area $0.5$. Verify $\sum_j w_{ij} A_j == A_i$.
