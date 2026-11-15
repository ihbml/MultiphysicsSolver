# Changes: Phase 3 Remediation (K3, K4, K9, M6, H10)

## Overview
Worker M3_PHASE3 has eliminated mathematical shortcuts, polycube bounding-box bypasses, compatibility shields, magic numbers, and silent error overwrites across the C++20 meshing core and Python geometry pipeline.

---

## 1. K3: Polycube Shortcut Removal (`backend/meshing/mesh_engine.py`)
- **Location:** `backend/meshing/mesh_engine.py:346-378` (`_detect_oriented_box_axes`)
- **Defect:** Geometry normal clustering previously declared any orthogonal polyhedron (such as L-profiles and U-profiles) as a rectangular cuboid because all normals aligned with $\pm x, \pm y, \pm z$. The mesher then filled the entire bounding box with a Cartesian grid, assigning an L-profile with true volume 3.0 an erroneous volume of 4.0.
- **Remediation:**
  - Added Gauss divergence theorem closed surface volume calculation $V_{\text{surf}} = \frac{1}{3} \sum (\mathbf{c}_f \cdot \mathbf{S}_f)$ and compared against the oriented bounding box volume $V_{\text{bbox}} = L_0 \cdot L_1 \cdot L_2$.
  - Enforced $|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$ before declaring a body as a box.
  - Verified that face normals form exactly 6 directional clusters ($\pm u_0, \pm u_1, \pm u_2$), rejecting non-box polyhedra with re-entrant corners.
  - Non-box orthogonal geometries now cleanly fall through to the level-set meshing pipeline, correctly generating true geometric volume $\approx 3.0$ (verified in `tests/test_mesh_engine.py::test_k3_l_profile_true_geometric_volume`).

---

## 2. K4: Compatibility Shield Removal & Farfield Padding Default (`backend/meshing/mesh_engine.py`)
- **Location:** `backend/meshing/mesh_engine.py:122-175`, `254-266`
- **Defect:** Lines 167-171 contained a "Backward Compatibility Shield" that checked `is_solid_only = (config is None and self.config is None) or (active_cfg.domain_mode == "solid_only")`. This silently forced solid interior meshing whenever `config=None`, contradicting the declarative schema default `domain_mode = "external_fluid"`.
- **Remediation:**
  - Removed the compatibility shield condition: `is_solid_only = (active_cfg.domain_mode == "solid_only")`.
  - Added explicit `domain_mode: Optional[str] = None` argument to `mesh_cad_file` for ergonomic dispatch.
  - In `mesh_hexcore`, defaulted to `FarfieldPadding()` to automatically compute `farfield_bbox` using characteristic length $L_{\text{ref}}$ when `effective_domain_mode == "external_fluid"` and no bounding box is specified.
  - Updated legacy benchmark tests (`test_cad_benchmark_testcad_stp` and `test_cad_benchmark_blade_full_rev1` in `tests/test_wp4_multiphysics_hexcore.py`) to explicitly specify `domain_mode="solid_only"`.

---

## 3. K9: STEP Engine Default to OpenCASCADE/Gmsh (`backend/geometry/parsers/step_parser.py`)
- **Location:** `backend/geometry/parsers/step_parser.py:84-135`
- **Defect:** In `parse(engine="auto")`, pure-Python B-Rep entity traversal (`_parse_with_brep`) was attempted first. Because `_parse_with_brep` only connects boundary vertex loops with planar triangle fans, curved B-spline and cylindrical faces (such as in `BladeFullRev1CFD.stp`) were flattened, losing over 23% of surface area without warning.
- **Remediation:**
  - Implemented `STEPParser._has_curved_entities(path: Path) -> bool` to inspect STEP entities for non-planar surfaces and curves (`B_SPLINE_SURFACE`, `CYLINDRICAL_SURFACE`, `SPHERICAL_SURFACE`, `CONICAL_SURFACE`, `TOROIDAL_SURFACE`, `SURFACE_OF_REVOLUTION`, `CIRCLE`, `ELLIPSE`, etc.).
  - In `engine="auto"`: if the model has curved entities, OpenCASCADE/Gmsh (`_parse_with_gmsh`) is mandatory. If Gmsh is unavailable on a curved model, an explicit `RuntimeError` is raised rather than flattening curved geometry.
  - Planar geometries (where all surfaces are planar, e.g. cuboids) cleanly use exact B-Rep topology extraction (`_parse_with_brep`) or Gmsh if requested.

---

## 4. M6 & H10: Elimination of Hard Clamps, Silent Overwrites & Magic Numbers
### 4.1. C++ Native Octree Core (`backend/meshing/native/src/octree_hexcore.cpp`)
- **Max Depth Clamp (Line 362):** Removed hard `std::clamp(options.max_depth, 0, 4)` and expanded upper bound to 16 (`std::clamp(options.max_depth, 0, 16)`), allowing user-configured fine refinement without 32-bit index overflow.
- **Silent Volume Overwrite (Lines 947-954):** Removed silent replacement `cell_vols[c] = nodes[active_leaves[c]].v_bg` when `vol_sum <= 1e-15`. Instead, degenerate cells are strictly marked with volume `0.0`, and inverted/negative sub-volumes are retained so downstream mesh validation flags defects rather than masking them.

### 4.2. Python Mesh Engine (`backend/meshing/mesh_engine.py`)
- **500k RAM Downscaling Cap (Lines 406-414):** Removed heuristic that silently coarsened the grid when $nx \cdot ny \cdot nz > 500,000$. Replaced with configurable `active_cfg.max_cells` (default 2,000,000) that raises an explicit `ValueError` when resolution limits are exceeded, ensuring deterministic GCI grid hierarchies.
- **Tuned Magic Numbers in `_mesh_complex_surface` (Lines 1019-1068):**
  - Eliminated `aspect > 2.0 -> h0 / 3.0` spanwise tuning; enforced isotropic element sizing $dx = dy = dz = h_0$ derived directly from $h_0$.
  - Eliminated `threshold = 0.11 * h_min` (which was artificially tuned to 0.39% volume error on the single blade); adopted standard level-set interface criterion `phi_c <= 0.0` (with configurable `active_cfg.levelset_threshold`).

---

## 5. Build and Native Recompilation
- Recompiled `cfd_mesher_native.dll` via Visual Studio 2022 CMake in Release mode with `--parallel 2`:
  - Build output: `backend/meshing/cfd_mesher_native.dll`
  - Zero compilation errors, zero linker warnings.
