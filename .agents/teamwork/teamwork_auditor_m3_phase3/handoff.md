# Forensic Anti-Cheat & Cleanliness Audit Report: Phase 3 Remediation (M3_PHASE3)

## Forensic Audit Report

**Work Product**: Phase 3 Remediation (`backend/meshing/mesh_engine.py`, `backend/geometry/parsers/step_parser.py`, `backend/meshing/native/src/octree_hexcore.cpp`)  
**Profile**: General Project  
**Integrity Mode**: Development  
**Verdict**: CLEAN  

### Phase Results
- **Anti-Cheat & Hardcoded Output Detection**: PASS — No hardcoded test strings, constant return stubs, or synthetic test fixtures found in modified codebase.
- **Facade Detection**: PASS — Algorithms are genuine, fully implemented mathematical routines (Gauss divergence surface integration, ISO 10303-21 entity graph parsing, C++20 Octree level-set discretization).
- **Pre-populated Artifact Detection**: PASS — Workspace clean; only standard NX CAD export log `TestCAD.log` present.
- **K3 (Polycube Shortcut Removal)**: PASS — `_detect_oriented_box_axes` strictly enforces Gauss divergence volume comparison $|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-3}$ and 6 directional normal clusters. L-profile and non-box shapes are rejected and meshed via general pipeline with true volume $\approx 3.0$ (never inflating to bounding box volume 4.0).
- **K4 (Compatibility Shield Removal)**: PASS — Backward compatibility shield that forced `domain_mode = "solid_only"` on omitted farfield parameters has been completely removed; defaults cleanly to `FarfieldPadding()` to construct the 16L wind tunnel fluid domain.
- **K9 (STEP Engine Default)**: PASS — `STEPParser(engine="auto")` scans for 16 ISO 10303-21 curved geometric entities; curved models (`BladeFullRev1CFD.stp`) enforce OpenCASCADE/Gmsh, raising explicit `RuntimeError` if Gmsh is absent instead of flattening curvature via planar triangle fans. Planar models (`TestCADSTPAP214.stp`) cleanly use exact B-Rep.
- **M6 & H10 (Hard Clamps & Silent Overwrites Removal)**: PASS — In `octree_hexcore.cpp`, `max_depth` clamp expanded from 4 to 16, and degenerate cut cells ($|\text{vol}| \le 10^{-15}$) are set to strict volume `0.0` rather than silently overwritten with uncut background cell volumes. In `mesh_engine.py`, the 500k-cell RAM downscaling cap is replaced with an explicit `ValueError`, and tuned magic factors ($0.11 \cdot h_{\min}$, $h_0 / 3$) are removed in favor of isotropic element sizing and physical level-set thresholds.
- **Empirical Test Execution**: PASS — 17/17 passed in `tests/test_mesh_engine.py` (20.38s) and 24/24 passed in `tests/test_wp4_multiphysics_hexcore.py` (210.87s) under low host load profile (`OMP_NUM_THREADS=2`).

---

## 1. Observation

### 1.1. Inspected Files & Verbatim Code Diffs

1. **`backend/meshing/mesh_engine.py`**:
   - **K4 Remediation (Lines 172–175)**:
     ```python
     # Domain mode evaluation: honor declarative config domain_mode directly
     is_solid_only = (active_cfg.domain_mode == "solid_only")
     ```
     The previous compatibility shield condition `(config is None and self.config is None) or ... or (active_cfg.domain_mode == "external_fluid" and active_cfg.farfield_padding is None and active_cfg.farfield_bbox is None)` was deleted.
   - **K4 Farfield Defaulting (Lines 258–267)**:
     ```python
     if effective_domain_mode == "external_fluid" and farfield_bbox is None:
         padding = active_cfg.farfield_padding if active_cfg.farfield_padding is not None else FarfieldPadding()
         bmin = np.min(scaled_surf.vertices, axis=0)
         bmax = np.max(scaled_surf.vertices, axis=0)
         ff_min, ff_max = self._compute_farfield_bounds(bmin, bmax, active_cfg)
         farfield_bbox = (
             float(ff_min[0]), float(ff_max[0]),
             float(ff_min[1]), float(ff_max[1]),
             float(ff_min[2]), float(ff_max[2]),
         )
     ```
   - **K3 Polycube Divergence Verification (Lines 351–381)**:
     ```python
     # Closed surface volume via divergence theorem
     v0 = verts[faces[:, 0]]
     v1 = verts[faces[:, 1]]
     v2 = verts[faces[:, 2]]
     c_f = (v0 + v1 + v2) / 3.0
     s_f = 0.5 * np.cross(v1 - v0, v2 - v0)
     surf_vol = abs(float(np.sum(np.sum(c_f * s_f, axis=1))) / 3.0)

     # Non-box geometries (e.g. L-profile: vol 3.0 vs bbox 4.0) deviate by > 0.1%
     if abs(surf_vol - bbox_vol) / bbox_vol > 1e-3:
         return None

     # Verify face count and directional clusters: a rectangular box has exactly 6 planar faces
     # (each axis must have both positive and negative normal faces, giving 6 distinct directions)
     dir_clusters = 0
     for axis in [u0, -u0, u1, -u1, u2, -u2]:
         if np.any(np.dot(normals, axis) > 0.99):
             dir_clusters += 1
     if dir_clusters != 6:
         return None
     ```
   - **M6 500k RAM Cap Elimination (Lines 406–414)**:
     ```python
     # Resolution limit check: fail fast on excessive resolution rather than silently coarsening
     max_cells_limit = int(getattr(active_cfg, "max_cells", 2_000_000))
     total_cells = nx * ny * nz
     if total_cells > max_cells_limit:
         raise ValueError(
             f"Requested resolution ({nx}x{ny}x{nz} = {total_cells} cells) exceeds "
             f"configured limit ({max_cells_limit}). Increase h0 or adjust active_cfg.max_cells."
         )
     ```
   - **M6 / H10 Tuning Factor Elimination (Lines 1019–1066)**:
     ```python
     # Isotropic element sizes derived directly from h0:
     dx = float(h0)
     dy = float(h0)
     dz = float(h0)
     ...
     # Solid body level-set criterion: cell centroid lies within solid boundary (phi <= 0)
     threshold = float(getattr(active_cfg, "levelset_threshold", 0.0))
     active_cells = []
     for idx, (i, j, k) in enumerate(ijk_list):
         if phi_c[idx] <= threshold:
     ```

2. **`backend/geometry/parsers/step_parser.py`**:
   - **K9 Curved STEP Model Enforcing OpenCASCADE/Gmsh (Lines 84–137)**:
     ```python
     # engine == "auto":
     # Curved B-Rep entities REQUIRE Gmsh/OpenCASCADE to avoid flattening and preserve true surface area (K9).
     if self._has_curved_entities(path):
         if not HAS_GMSH:
             raise RuntimeError(
                 f"STEP file {path} contains curved B-Rep entities (B-spline/cylinder/sphere) "
                 "which require Gmsh/OpenCASCADE for faithful tessellation. Install gmsh or specify engine='gmsh'."
             )
         return self._parse_with_gmsh(
             path, deflection, angular_tol, min_edge, max_edge, detected_units
         )

     # Planar models (e.g. cuboids, polyhedra) can be cleanly ingested via exact B-Rep topology
     try:
         return self._parse_with_brep(path, detected_units)
     ```
     `_has_curved_entities(path)` streams the STEP file in 1 MB chunks inspecting for `B_SPLINE_SURFACE`, `CYLINDRICAL_SURFACE`, `SPHERICAL_SURFACE`, `CONICAL_SURFACE`, `TOROIDAL_SURFACE`, `SURFACE_OF_REVOLUTION`, `CIRCLE`, `ELLIPSE`, etc.

3. **`backend/meshing/native/src/octree_hexcore.cpp`**:
   - **M6 Max Refinement Depth (Line 362)**:
     ```cpp
     int32_t max_depth = std::clamp(options.max_depth, 0, 16);
     ```
   - **H10 Degenerate Cell Handling (Lines 947–954)**:
     ```cpp
     cell_vols[c] = vol_sum;
     if (std::abs(vol_sum) > 1e-15) {
       cell_cents[c] = cent_sum / vol_sum;
     } else {
       cell_cents[c] = c_approx;
       cell_vols[c] = 0.0;
     }
     ```

---

## 2. Logic Chain

1. **K3 (Polycube Shortcut)**:
   - *Observation*: For an L-profile, face normals point exclusively in coordinate directions ($\pm x, \pm y, \pm z$), which previously led `_detect_oriented_box_axes` to declare it a box.
   - *Remediation Logic*: The Gauss divergence theorem provides an exact surface integral for closed volume: $V_{\text{surf}} = \frac{1}{3}\sum (\mathbf{c}_f \cdot \mathbf{S}_f)$. For an L-profile with dimensions $2 \times 2 \times 1$ and a $1 \times 1 \times 1$ cutout, $V_{\text{surf}} = 3.0$, while the oriented bounding box volume is $V_{\text{bbox}} = 2 \times 2 \times 1 = 4.0$. The relative error $|3.0 - 4.0| / 4.0 = 25\% \gg 10^{-3}$.
   - *Empirical Verification*: Running `MeshEngine._detect_oriented_box_axes` on the L-profile returned `None`. Meshing the L-profile yielded total cell volume $2.9999999999999996 \approx 3.0$ (never inflating to 4.0).

2. **K4 (Compatibility Shield)**:
   - *Observation*: Defaulting `domain_mode = "external_fluid"` without explicit farfield padding previously triggered an `is_solid_only` fallback that silently generated a solid mesh.
   - *Remediation Logic*: By removing the shield and setting `is_solid_only = (active_cfg.domain_mode == "solid_only")`, any invocation in `external_fluid` mode (including defaults) activates `FarfieldPadding()` (5L upstream, 10L downstream, 5L lateral) and meshes the fluid domain.
   - *Empirical Verification*: Meshing a unit cube with `config=None` generated 1,935 fluid cells with total volume $1,935.0 \text{ m}^3$ around the body. With `domain_mode="solid_only"`, it generated 8 cells with volume $1.0 \text{ m}^3$.

3. **K9 (STEP Engine Default)**:
   - *Observation*: Direct B-Rep parsing triangulated curved faces with flat triangle fans across boundary loops, causing 23.1% surface area loss on `BladeFullRev1CFD.stp`.
   - *Remediation Logic*: `_has_curved_entities` categorizes models before parsing. Curved models require OpenCASCADE/Gmsh (`_parse_with_gmsh`). If Gmsh is absent, an explicit `RuntimeError` is raised rather than silently degrading geometry. Planar models continue using exact B-Rep.
   - *Empirical Verification*:
     - `BladeFullRev1CFD.stp`: `_has_curved_entities -> True`. Mocking `HAS_GMSH=False` raised expected `RuntimeError`.
     - `TestCADSTPAP214.stp`: `_has_curved_entities -> False`. Parsed cleanly via exact B-Rep producing 8 vertices, 12 faces.

4. **M6 & H10 (Clamps, Overwrites, Magic Numbers)**:
   - *Observation*: Depth clamped to 4, degenerate cut cells overwritten with uncut background cell volumes, RAM downscaling at 500k, and magic numbers ($0.11 \cdot h_{\min}$, $h_0/3$).
   - *Remediation Logic*: Expanding max depth to 16 allows fine surface resolution. Setting degenerate volume to `0.0` prevents volume inflation and preserves honest geometry for downstream checks. Replacing 500k coarsening with `ValueError` ensures GCI grid hierarchy determinism. Removing tuning multipliers enforces physical discretization.
   - *Empirical Verification*:
     - Attempting to mesh 8,000 cells with `max_cells = 500` raised explicit `ValueError: Requested resolution (20x20x20 = 8000 cells) exceeds configured limit (500)`.
     - Native C++ hexcore engine generated 33,376 cells with all $V_i > 0$ and `mesh.validate() == True`.

---

## 3. Caveats

- **External CAD Prerequisite**: Complex curved STEP CAD models (e.g. `BladeFullRev1CFD.stp`) require `gmsh` in the Python environment for OpenCASCADE tessellation. If missing, the parser fails fast with `RuntimeError` as designed.
- **Resource Limits**: In accordance with user constraints (active Ansys process in background), tests must be executed with `OMP_NUM_THREADS=2` and sequential pytest execution.

---

## 4. Conclusion

The Phase 3 remediation has been audited under strict forensic standards:
1. **Zero Cheats / Zero Shortcuts**: All heuristic bypasses, synthetic shields, and magic tuning constants have been excised.
2. **K3, K4, K9, M6, and H10**: Fully and honestly resolved with mathematically rigorous formulations (Gauss divergence closed volume audit, declarative farfield defaulting, OpenCASCADE curved entity routing, 16-level depth ceiling, strict zero degenerate volume, fail-fast resolution exceptions).
3. **Verdict**: **CLEAN — APPROVE**.

---

## 5. Verification Method

To independently verify all claims:

```powershell
$env:OMP_NUM_THREADS="2"
$env:MKL_NUM_THREADS="2"

# 1. Verify Phase 3 mesh engine unit and regression tests
.\.venv\Scripts\python.exe -m pytest tests/test_mesh_engine.py -v

# 2. Verify WP4 multiphysics hexcore test suite
.\.venv\Scripts\python.exe -m pytest tests/test_wp4_multiphysics_hexcore.py -v

# 3. Verify K3 non-box rejection on L-profile
.\.venv\Scripts\python.exe -c "
import numpy as np
from backend.meshing.mesh_engine import MeshEngine
pts = np.array([[0,0,0],[1,0,0],[2,0,0],[2,1,0],[1,1,0],[1,2,0],[0,2,0],[0,0,1],[1,0,1],[2,0,1],[2,1,1],[1,1,1],[1,2,1],[0,2,1]], dtype=np.float64)
faces = np.array([[0,4,1],[0,6,4],[6,5,4],[1,3,2],[1,4,3],[7,8,11],[7,11,13],[13,11,12],[8,9,10],[8,10,11],[0,1,8],[0,8,7],[1,2,9],[1,9,8],[2,3,10],[2,10,9],[3,4,11],[3,11,10],[4,5,12],[4,12,11],[5,6,13],[5,13,12],[6,0,7],[6,7,13]], dtype=np.int32)
assert MeshEngine._detect_oriented_box_axes(pts, faces) is None
"
```

- **Invalidation Conditions**:
  - If `_detect_oriented_box_axes` returns an orientation matrix $R$ for an L-profile, K3 has regressed.
  - If `mesh_cad_file(cube_stl, config=None)` generates an internal solid mesh instead of an external fluid mesh, K4 has regressed.
  - If `step_parser.py` flattens `BladeFullRev1CFD.stp` using pure B-Rep without Gmsh, K9 has regressed.
  - If `octree_hexcore.cpp` clamps depth to 4 or assigns `nodes[active_leaves[c]].v_bg` to degenerate cut cells, M6/H10 has regressed.
