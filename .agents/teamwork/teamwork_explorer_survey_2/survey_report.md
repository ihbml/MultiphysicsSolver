# Phase 3 & Phase 4 Survey Report: Heuristics Removal & Numerics/Topology Remediation

**Date:** 2026-11-09T23:55:00Z  
**Author:** Survey Explorer 2 (`teamwork_explorer`)  
**Scope:** Phase 3 (K3, K4, K9, M6, H10) & Phase 4 (H3, H4, H2, H7, H8, M1, M2)  
**Status:** Complete Read-Only Architectural Survey & Implementation Blueprint  

---

## Executive Summary

This report delivers a rigorous mathematical and code-level investigation of the remaining uncorrected audit items for Phase 3 and Phase 4 in the `CFDSolver` meshing and numerics backend. Every item has been traced to its exact source line in Python and C++20, the mathematical defect analyzed, the proposed drop-in replacement formulated, and the verification method established.

Key findings:
1. **Phase 3 Shortcuts:** Bounding-box polycube routing (`_detect_oriented_box_axes`) causes concave axis-aligned shapes (L- and U-profiles) to mesh as solid boxes (volume 4.0 instead of 3.0). The "backward compatibility shield" silently coerces `external_fluid` to `solid_only` whenever config parameters are omitted. STEP import default (`auto`) flattens curved surfaces (e.g., losing 23% surface area on `BladeFullRev1CFD.stp`). Magic numbers (500k RAM cap, `0.11 * h_min`, `h0 / 3`, `max_level <= 4`, $10^{-15}$ background volume overwrite) silently distort grid hierarchies.
2. **Phase 4 Numerics & Topology:** 2D cell centroid integration mistakenly applies 3D pyramid weights ($0.75 / 0.25$) rather than 2D triangle weights ($2/3 / 1/3$), distorting quadrilateral centroids (e.g. yielding $(1.41875, 0.3875)$ instead of $(1.4, 0.4)$). Element topology guessing (`len(verts) == 8`) treats flat octagons as 3D hexes and destroys polyhedra during ALE mesh deformation. GCI fails to detect divergence ($R \ge 1$) and assumes uniform grid spacing. AMI uses inverse-distance weights rather than area-conservative polygon clipping. SPH-FVM coupling uses tautological boundary area vectors ($A_{iS} = -\sum \nabla W$) and non-conservative P2M interpolation. Aspect ratio evaluates vertex pairs rather than edges or bounding boxes (reporting 1.732 for a unit cube instead of 1.0), and `wsrd_alpha` normalizes against the global median volume rather than the background octree cell volume.

---

## 1. Phase 3: Abkürzungen & Heuristiken entfernen

### 1.1. K3: Polycube Bounding-Box Shortcut (`mesh_engine.py:139-143`, `238-291`)

#### Exact Code Location
- `backend/meshing/mesh_engine.py:139-143` (Invocation in `mesh_cad_file`):
```python
139:         # 2. Case A: Check if geometry is a rectangular cuboid / box via rigorous normal-axis auditing
140:         R = self._detect_oriented_box_axes(verts, faces)
141:         if R is not None:
142:             return self._mesh_oriented_box(verts, R, h0=h0, active_cfg=active_cfg)
```
- `backend/meshing/mesh_engine.py:237-291` (`_detect_oriented_box_axes` definition):
```python
237:     @staticmethod
238:     def _detect_oriented_box_axes(
239:         verts: np.ndarray, faces: np.ndarray
240:     ) -> Optional[np.ndarray]:
...
263:         axes: List[np.ndarray] = []
264:         for n in normals:
265:             matched = False
266:             for a in axes:
267:                 if abs(float(np.dot(n, a))) > 0.999:
268:                     matched = True
269:                     break
270:             if not matched:
271:                 axes.append(n)
272:                 if len(axes) > 3:
273:                     return None
274: 
275:         if len(axes) != 3:
276:             return None
...
287:         R = np.column_stack([u0, u1, u2])
288:         if np.linalg.det(R) < 0:
289:             u2 = -u2
290:             R = np.column_stack([u0, u1, u2])
291:         return R
```

#### Defect Analysis
`_detect_oriented_box_axes` classifies any geometry as a "box" as long as all face normals cluster into 3 mutually orthogonal directions. 
However, **all orthogonal polyhedra** (L-profiles, U-profiles, T-sections, cruciforms, and blocks with rectangular cutouts) also have face normals parallel to $\pm x, \pm y, \pm z$!
Consequently, `_detect_oriented_box_axes` returns an orthonormal rotation matrix $R$ for an L-profile. `_mesh_oriented_box` then takes the global vertex span $L = \max(P) - \min(P)$ and fills the entire bounding box with a structured Cartesian hexahedral grid.
- **Consequence:** For an L-profile with true solid volume $3.0$, the mesher generates a solid block of volume $4.0$. The cutout geometry is obliterated.

#### Remediation Specification
To preserve the fast structured mesher for genuine solid rectangular cuboids while ensuring non-box shapes fall through to the level-set / octree hexcore pipeline:
1. **Geometric Invariant Check:** A genuine rectangular cuboid has:
   - Exactly 6 planar boundary faces (12 triangles for a standard triangulated STL/STEP mesh).
   - Enclosed geometric volume $V_{\text{surf}} = \frac{1}{3} \sum_{f} (\mathbf{c}_f \cdot \mathbf{S}_f)$ matching the bounding box volume $V_{\text{bbox}} = L_0 \cdot L_1 \cdot L_2$ to within tight numerical tolerance ($|V_{\text{surf}} - V_{\text{bbox}}| / V_{\text{bbox}} < 10^{-4}$).
2. **Implementation in `_detect_oriented_box_axes`:**
```python
        # Check volume consistency: surface divergence volume must match oriented bounding box volume
        P = verts @ R
        p_min = np.min(P, axis=0)
        p_max = np.max(P, axis=0)
        L = p_max - p_min
        bbox_vol = float(L[0] * L[1] * L[2])
        if bbox_vol <= 1e-15:
            return None

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
```
3. When `_detect_oriented_box_axes` returns `None`, non-box geometries route to `_mesh_complex_surface` (or `mesh_hexcore(..., domain_mode="solid_only")`), which cuts the geometry along the true zero-level set of the signed distance field.

---

### 1.2. K4: Backward Compatibility Shield (`mesh_engine.py:124-137`)

#### Exact Code Location
- `backend/meshing/mesh_engine.py:124-137`:
```python
124:         # Backward Compatibility Shield:
125:         # When both config is None and self.config is None, or active_cfg.domain_mode == "solid_only",
126:         # mesh_cad_file preserves solid body interior meshing so that existing tests pass without regression.
127:         is_solid_only = (config is None and self.config is None) or (active_cfg.domain_mode == "solid_only")
128: 
129:         if not is_solid_only:
130:             clean_mesh = SurfaceMesh(
131:                 vertices=verts,
132:                 faces=faces.copy(),
133:                 normals=sm.normals.copy() if sm.normals is not None else None,
134:                 patches=dict(sm.patches) if sm.patches else {},
135:             )
136:             effective_h0 = float(h0)
137:             return self.mesh_hexcore(clean_mesh, scale=1.0, h0=effective_h0, config=active_cfg)
```

#### Defect Analysis
`MeshingConfig.domain_mode` defaults to `"external_fluid"` (`models.py:143`).
However, line 127 checks `(config is None and self.config is None)`. If a user or script calls `engine.mesh_cad_file("sphere.step")` with default parameters, `is_solid_only` evaluates to `True`, forcing solid interior meshing rather than the declarative default `"external_fluid"`!
Furthermore, even when `config` is passed with `domain_mode="external_fluid"`, if `farfield_padding` and `farfield_bbox` are `None`, the mesher lacked an explicit `FarfieldPadding()` fallback.

#### Remediation Specification
1. In `mesh_engine.py:127`, eliminate the shield condition:
```python
        # Domain mode evaluation: honor declarative config domain_mode directly
        is_solid_only = (active_cfg.domain_mode == "solid_only")
```
2. In `mesh_engine.py:200-211` (`mesh_hexcore`), default to `FarfieldPadding()` when `domain_mode == "external_fluid"` and no farfield bounding box is specified:
```python
        if effective_domain_mode == "external_fluid" and farfield_bbox is None:
            padding = active_cfg.farfield_padding if active_cfg.farfield_padding is not None else FarfieldPadding()
            farfield_bbox = padding.compute_bbox(scaled_surf.vertices)
```
3. Update tests that explicitly intended to mesh solid bodies without passing a config:
   - `tests/test_challenger_m2_audit_2.py:246`: change `engine.mesh_cad_file(cube_stl, h0=0.5, config=None)` to pass `config=MeshingConfig(domain_mode="solid_only")`.
   - `tests/test_challenger_m2_audit_2_deep_stress.py:203`: explicitly test `config=MeshingConfig(domain_mode="solid_only")` for solid permutation.

---

### 1.3. K9: STEP Engine Default (`step_parser.py:84-95`)

#### Exact Code Location
- `backend/geometry/parsers/step_parser.py:84-95`:
```python
84:         # engine == "auto":
85:         # For polyhedral B-Rep models, pure B-Rep gives the exact topological representation.
86:         # For curved or complex models, Gmsh provides full OpenCASCADE B-Rep tessellation.
87:         try:
88:             return self._parse_with_brep(path, detected_units)
89:         except Exception as ex:
90:             logger.info("Direct B-Rep parsing fell back to Gmsh tessellation: %s", ex)
91:             if HAS_GMSH:
92:                 return self._parse_with_gmsh(
93:                     path, deflection, angular_tol, min_edge, max_edge, detected_units
94:                 )
95:             raise
```

#### Defect Analysis
When `engine == "auto"` (the default), `step_parser.py` attempts `_parse_with_brep` first.
`_parse_with_brep` traverses `ADVANCED_FACE` entity loops and triangulates the perimeter points using a flat triangle fan (`[v0, v_i, v_{i+1}]`, lines 347-353).
- **Consequence:** Curved B-Spline surfaces (cylinders, spheres, aerofoils, turbine blades such as `BladeFullRev1CFD.stp`) are NOT tessellated across their curved interior; they are flattened into 2D polygon fans across boundary vertices.
- **Audit Evidence:** On `BladeFullRev1CFD.stp`, `auto` generated 599 triangles with surface area $240,239\text{ mm}^2$, whereas Gmsh/OpenCASCADE produced 464,820 triangles with surface area $312,557\text{ mm}^2$ — **23% of the surface area was lost without error or warning**.

#### Remediation Specification
Make Gmsh / OpenCASCADE the primary engine when available:
```python
        # engine == "auto":
        # Gmsh/OpenCASCADE is the primary robust kernel for 3D CAD B-Rep tessellation.
        # Fall back to pure-Python B-Rep only if Gmsh is unavailable AND model has only planar faces.
        if HAS_GMSH:
            try:
                return self._parse_with_gmsh(
                    path, deflection, angular_tol, min_edge, max_edge, detected_units
                )
            except Exception as ex:
                logger.warning("Gmsh tessellation failed, attempting pure-Python B-Rep fallback: %s", ex)

        # Inspect if model has curved entities before permitting B-Rep fallback
        if self._has_curved_entities(path):
            raise RuntimeError(
                f"STEP file {path} contains curved B-Rep entities (B-spline/cylinder/sphere) "
                "which require Gmsh/OpenCASCADE for faithful tessellation. Install gmsh or specify engine='gmsh'."
            )
        return self._parse_with_brep(path, detected_units)
```

---

### 1.4. M6 & H10: Hard Clamps, Silent Overwrites & Magic Numbers

#### 1. `octree_hexcore.cpp:362` — Max Depth Clamp
- **Current Line:**
```cpp
362:   int32_t max_depth = std::clamp(options.max_depth, 0, 4);
```
- **Defect:** Clamps refinement depth to 4 levels regardless of user settings or geometry dimensions.
- **Remediation:** Remove the hard clamp of 4:
```cpp
  int32_t max_depth = std::clamp(options.max_depth, 0, 16); // 16 allows fine resolution while preventing 32-bit index overflow
```

#### 2. `octree_hexcore.cpp:947-954` — Silent $10^{-15}$ Background Volume Overwrite
- **Current Lines:**
```cpp
947:     cell_vols[c] = vol_sum;
948:     if (vol_sum > 1e-15) {
949:       cell_cents[c] = cent_sum / vol_sum;
950:     } else {
951:       cell_cents[c] = c_approx;
952:       cell_vols[c] = nodes[active_leaves[c]].v_bg;
953:     }
```
- **Defect:** If a snapped cut cell collapses, inverts, or has volume $\le 10^{-15}$, line 952 silently overwrites `cell_vols[c]` with the FULL uncut background cell volume `nodes[active_leaves[c]].v_bg`! This hides geometric inversion and distorts the total domain volume.
- **Remediation:** Retain the true signed volume and flag inverted / degenerate cells:
```cpp
    cell_vols[c] = vol_sum;
    if (std::abs(vol_sum) > 1e-15) {
      cell_cents[c] = cent_sum / vol_sum;
    } else {
      cell_cents[c] = c_approx;
      cell_vols[c] = 0.0; // Strictly mark degenerate cell with 0.0 volume
    }
```

#### 3. `mesh_engine.py:316-333` — 500k RAM Cap
- **Current Lines:**
```python
319:         max_cells_limit = 500_000
320:         total_cells = nx * ny * nz
321:         if total_cells > max_cells_limit:
322:             scale_fac = (max_cells_limit / total_cells) ** (1.0 / 3.0)
323:             new_nx = max(2, int(nx * scale_fac))
324:             new_ny = max(2, int(ny * scale_fac))
325:             new_nz = max(2, int(nz * scale_fac))
326:             warnings.warn(...)
327:             nx, ny, nz = new_nx, new_ny, new_nz
```
- **Defect:** Silently downscales grid resolution when $nx \cdot ny \cdot nz > 500,000$. In a GCI refinement hierarchy, fine and medium grids can become artificially identical!
- **Remediation:** Remove silent scaling. Support configurable limit and throw explicit exception:
```python
        max_cells_limit = int(getattr(active_cfg, "max_cells", 2_000_000))
        total_cells = nx * ny * nz
        if total_cells > max_cells_limit:
            raise ValueError(
                f"Requested resolution ({nx}x{ny}x{nz} = {total_cells} cells) exceeds "
                f"configured limit ({max_cells_limit}). Increase h0 or adjust active_cfg.max_cells."
            )
```

#### 4. `mesh_engine.py:937-940` and `981-983` — Magic Tuning Factors
- **Current Lines:**
```python
937:         aspect = span[2] / max(span[0], span[1])
938:         dx = h0 / 3.0 if aspect > 2.0 else h0
939:         dy = h0 / 3.0 if aspect > 2.0 else h0
940:         dz = h0
...
981:         h_min = min(dx, dy, dz)
982:         threshold = 0.11 * h_min  # Physical level-set interface narrow-band fraction (0.39% error)
```
- **Defect:** `h0 / 3.0` and `0.11 * h_min` are magic numbers tuned to force `BladeRev1CFD` test volume errors below 0.5%.
- **Remediation:** Use isotropic discretization derived directly from $h_0$, and exact level-set interface criterion `phi_c <= 0.0`:
```python
        dx = h0
        dy = h0
        dz = h0
...
        # Solid body level-set criterion: cell centroid lies within solid boundary (phi <= 0)
        active_cells = []
        for idx, (i, j, k) in enumerate(ijk_list):
            if phi_c[idx] <= 0.0:
                c_nodes = np.array([...], dtype=np.int64)
                active_cells.append(c_nodes)
```

---

## 2. Phase 4: Mathematik & Topologie korrigieren

### 2.1. H3: 2D-Schwerpunkte (`difvm_topology.py:1244`, `difvm_mesh.cpp:454`)

#### Exact Code Locations
- `backend/meshing/difvm_topology.py:1244`:
```python
1242:             pyr_vol = float(np.dot(tf["centre"] - c_approx, sf_out)) / div_factor
1243:             vol_sum += pyr_vol
1244:             pyr_c = tf["centre"] * 0.75 + c_approx * 0.25
1245:             centroid_weighted += pyr_c * pyr_vol
```
- `backend/meshing/native/src/difvm_mesh.cpp:454`:
```cpp
452:             double pyr_vol = dot(tf.centre - c_approx, sf_out) / div_factor;
453:             vol_sum += pyr_vol;
454:             Vec3 pyr_c = tf.centre * 0.75 + c_approx * 0.25;
455:             centroid_weighted += pyr_c * pyr_vol;
```

#### Mathematical Analysis
- In 3D, a polyhedron is decomposed into pyramids with base on face $f$ (centroid $\mathbf{c}_f$) and apex at $\mathbf{c}_{\text{approx}}$. The centroid of a pyramid is located $\frac{3}{4}$ along the segment from apex to base centroid:
  $$\mathbf{c}_{\text{pyr}} = \frac{3}{4} \mathbf{c}_f + \frac{1}{4} \mathbf{c}_{\text{approx}} = 0.75 \mathbf{c}_f + 0.25 \mathbf{c}_{\text{approx}}$$
- In 2D, Green's theorem decomposes a polygon into triangles with base along edge $f$ (midpoint $\mathbf{c}_f = \frac{\mathbf{v}_1 + \mathbf{v}_2}{2}$) and third vertex at $\mathbf{c}_{\text{approx}}$. The centroid of a triangle is the arithmetic mean of its three vertices:
  $$\mathbf{c}_{\text{tri}} = \frac{\mathbf{v}_1 + \mathbf{v}_2 + \mathbf{c}_{\text{approx}}}{3} = \frac{2}{3} \left(\frac{\mathbf{v}_1 + \mathbf{v}_2}{2}\right) + \frac{1}{3} \mathbf{c}_{\text{approx}} = \frac{2}{3} \mathbf{c}_f + \frac{1}{3} \mathbf{c}_{\text{approx}}$$
- Using $0.75 / 0.25$ in 2D shifts the sub-triangle centroid too far toward the edge ($75\%$ vs $66.7\%$), creating a systematic error for all asymmetric 2D elements.

#### Test Quadrilateral Verification: $(0,0)-(4,0)-(1,1)-(0,1)$
- **Analytical Solution:**
  Splitting into square $[0,1]^2$ ($A_1 = 1.0, C_1 = (0.5, 0.5)$) and triangle $(1,0)-(4,0)-(1,1)$ ($A_2 = 1.5, C_2 = (2.0, 1/3)$):
  $$C_x = \frac{1.0 \times 0.5 + 1.5 \times 2.0}{2.5} = \frac{3.5}{2.5} = 1.40$$
  $$C_y = \frac{1.0 \times 0.5 + 1.5 \times (1/3)}{2.5} = \frac{1.0}{2.5} = 0.40$$
  Exact centroid is **$(1.4, 0.4)$**.
- **Erroneous Code Output (using $0.75 / 0.25$):** $(1.41875, 0.3875)$.
- **Remediated Code Output (using $\frac{2}{3} / \frac{1}{3}$):** Exactly **$(1.4, 0.4)$**.

#### Remediation Specification
- In `difvm_topology.py`:
```python
            if is_cell_2d:
                pyr_c = tf["centre"] * (2.0 / 3.0) + c_approx * (1.0 / 3.0)
            else:
                pyr_c = tf["centre"] * 0.75 + c_approx * 0.25
```
- In `difvm_mesh.cpp`:
```cpp
            Vec3 pyr_c = is_2d_cell ? (tf.centre * (2.0 / 3.0) + c_approx * (1.0 / 3.0))
                                    : (tf.centre * 0.75 + c_approx * 0.25);
```

---

### 2.2. H4: Polyhedral Topology & Elimination of `len(verts) == 8` Heuristic

#### Exact Code Locations
- `backend/meshing/difvm_topology.py:288-317` (`_classify_cell_type`)
- `backend/meshing/difvm_topology.py:333-345` (`_extract_cell_faces_py`)
- `backend/meshing/native/src/difvm_mesh.cpp:105-117` (`extractCellFacesC`)
- `backend/meshing/moving_mesh.py:62-114` (`_build_node_adjacency`)
- `backend/meshing/moving_mesh.py:279-283` (`deform_mesh`)

#### Defect Analysis
1. `_classify_cell_type` and `extractCellFacesC` assume that any element with 8 vertices is a 3D `HEXAHEDRON`. A coplanar 8-node polygon in 2D is classified as `HEXAHEDRON` with volume $0.0$!
2. Polyhedral cut cells in Octree hexcore meshes (with 13, 17, 20 vertices) are defined by explicit face loops. Passing `cells` (a flat list of vertex indices) into `compute_difvm_topology` discards internal face connectivity and reconstructs topology via heuristics, cutting octree mesh volume in half ($63.5 \to 31.2$).
3. In `moving_mesh.py:279-283`, `deform_mesh` discards the original mesh's face lists, owner/neighbour arrays, and boundary patches, calling `compute_difvm_topology` on `new_points` and `mesh.cells`. This invalidates cut polyhedra.

#### Remediation Specification
1. **Explicit Face-Based Adjacency in `moving_mesh.py`:**
Instead of guessing cell geometry from vertex counts, construct node adjacency directly from existing face loops:
```python
    def _build_node_adjacency(self, mesh: UnstructuredMesh) -> List[Set[int]]:
        """Construct exact node adjacency from explicit mesh faces for all element types."""
        n_pts = len(mesh.points)
        adj: List[Set[int]] = [set() for _ in range(n_pts)]
        if mesh.faces is not None and len(mesh.faces) > 0:
            for fn in mesh.faces:
                nv = len(fn)
                for i in range(nv):
                    u = int(fn[i])
                    v = int(fn[(i + 1) % nv])
                    adj[u].add(v)
                    adj[v].add(u)
            return adj
        # Fallback for simple grids without explicit faces...
```
2. **Topology Preservation in `deform_mesh`:**
During ALE mesh deformation, the topological connectivity (faces, cell types, owner, neighbour, boundaries) does not change! Only coordinates change.
```python
        # Update geometric fields while preserving explicit face and cell topology
        new_mesh = UnstructuredMesh(
            points=new_points,
            cells=mesh.cells,
            owner=mesh.owner.copy(),
            neighbour=mesh.neighbour.copy(),
            face_areas=np.zeros_like(mesh.face_areas),
            face_centres=np.zeros_like(mesh.face_centres),
            cell_centres=np.zeros_like(mesh.cell_centres),
            cell_volumes=np.zeros_like(mesh.cell_volumes),
            delta_f=np.zeros_like(mesh.delta_f),
            k_f=np.zeros_like(mesh.k_f),
            boundaries=dict(mesh.boundaries),
            cell_regions=mesh.cell_regions.copy() if mesh.cell_regions is not None else None,
            faces=[f.copy() for f in mesh.faces] if mesh.faces is not None else None,
            cell_types=mesh.cell_types.copy() if mesh.cell_types is not None else None,
            topological_dim=mesh.topological_dim,
            metadata=dict(mesh.metadata),
        )
        new_mesh.recompute_geometry()
```
3. In `difvm_topology.py` and `difvm_mesh.cpp`, when `cell_types` is explicitly passed, respect it without running `len(verts)` heuristics. When `nv == 8`, verify non-coplanarity before assigning `HEXAHEDRON`.

---

### 2.3. H2: Celik et al. (2008) Grid Convergence Index (`backend/meshing/gci.py`)

#### Exact Code Location
- `backend/meshing/gci.py:120-187` (`compute_asme_vv20_gci`)
- `backend/meshing/gci.py:225-231` (`generate_3grid_gci_hierarchy`)

#### Defect Analysis
1. **Divergence Detection Missing:**
   Celik et al. (2008) define the convergence ratio:
   $$R = \frac{\epsilon_{21}}{\epsilon_{32}} = \frac{\phi_2 - \phi_1}{\phi_3 - \phi_2}$$
   - Monotonic convergence: $0 < R < 1$.
   - Oscillatory convergence: $-1 < R < 0$.
   - **Divergence:** $R \ge 1$ or $R \le -1$.
   When $|R| \ge 1$, Richardson extrapolation is invalid. Currently, `gci.py` inverts the ratio and returns a seemingly converged result with positive apparent order (e.g., sequence $(1.0, 1.1, 1.105)$ gives $p = 8.64, \text{GCI} = 1.8\%$).
2. **Uniform Refinement Assumption ($r_{21} = r_{32}$):**
   `compute_asme_vv20_gci` accepts only a single scalar `r`. In unstructured meshing, grid refinement is rarely uniform ($r_{21} \ne r_{32}$).
3. **Apparent Order Iteration Missing:**
   When $r_{21} \ne r_{32}$, Celik et al. Eq. (10) requires fixed-point iteration:
   $$p_{k+1} = \frac{1}{\ln(r_{21})} \left| \ln\left|\frac{\epsilon_{32}}{\epsilon_{21}}\right| + q(p_k) \right|, \quad q(p) = \ln\left(\frac{r_{21}^p - s}{r_{32}^p - s}\right), \quad s = \text{sign}\left(\frac{\epsilon_{32}}{\epsilon_{21}}\right)$$

#### Remediation Specification
1. Update `compute_asme_vv20_gci` signature:
```python
def compute_asme_vv20_gci(
    phi_1: float,
    phi_2: float,
    phi_3: float,
    r21: float,
    r32: Optional[float] = None,
    f_s: float = 1.25,
) -> GCIResult:
```
2. Implement divergence detection and transcendental apparent order solver:
```python
    eps_32 = phi_3 - phi_2
    eps_21 = phi_2 - phi_1

    # Check for zero differences
    if abs(eps_21) < 1e-15 or abs(eps_32) < 1e-15:
        return GCIResult(apparent_order_p=0.0, phi_extrapolated=phi_1, ...)

    R = eps_21 / eps_32
    if abs(R) >= 1.0:
        # Divergent sequence: Richardson extrapolation is mathematically invalid
        return GCIResult(
            apparent_order_p=float("nan"),
            phi_extrapolated=float("nan"),
            approx_rel_error_fine=abs(eps_21 / (phi_1 + 1e-30)),
            gci_fine=float("inf"),
            gci_medium=float("inf"),
            asymptotic_ratio=float("nan"),
            convergence_type="divergence",
        )

    # Iterative apparent order for non-uniform refinement
    if r32 is None or abs(r21 - r32) < 1e-6:
        p = float(np.log(abs(eps_32 / eps_21)) / np.log(r21))
    else:
        s = 1.0 if (eps_32 / eps_21) > 0 else -1.0
        p = abs(float(np.log(abs(eps_32 / eps_21)) / np.log(r21)))
        for _ in range(50):
            q = np.log((r21**p - s) / (r32**p - s))
            p_next = (1.0 / np.log(r21)) * abs(np.log(abs(eps_32 / eps_21)) + q)
            if abs(p_next - p) < 1e-5:
                p = p_next
                break
            p = p_next
```
3. In `generate_3grid_gci_hierarchy`, compute effective refinement ratios from actual mesh cell counts:
   $$r_{21} = \left(\frac{N_1}{N_2}\right)^{1/\text{dim}}, \quad r_{32} = \left(\frac{N_2}{N_3}\right)^{1/\text{dim}}$$

---

### 2.4. H7: Supermesh-AMI Area-Conservative Weights (`difvm_topology.py:1788-1815`)

#### Exact Code Location
- `backend/meshing/difvm_topology.py:1788-1815` (`compute_ami_weights`):
```python
1788: def compute_ami_weights(
1789:     source_centres: np.ndarray,
1790:     source_areas: np.ndarray,
1791:     target_centres: np.ndarray,
1792:     target_areas: np.ndarray,
1793:     radius: float = 0.05,
1794: ) -> np.ndarray:
...
1804:         diffs = source_centres - target_centres[i]
1805:         dists = np.linalg.norm(diffs, axis=1)
1806:         in_range = dists < radius
1807:         if np.any(in_range):
1808:             inv_d = 1.0 / (dists[in_range] + 1e-12)
1809:             w = inv_d / np.sum(inv_d)
1810:             weights[i, in_range] = w
```

#### Defect Analysis
1. `source_areas` and `target_areas` are completely unused arguments!
2. The function implements heuristic **Inverse Distance Weighting (IDW)** within a hardcoded radius of $0.05\text{ m}$.
3. **Conservation Violation:** If two source faces have identical centroid distance from a target face, but one has area $1.0\text{ m}^2$ and the other has area $10^{-6}\text{ m}^2$, both receive weight $0.5$. The flux through the interface violates conservation: $\sum_j w_{ij} A_j \ne A_i$.

#### Remediation Specification
Replace IDW with geometric **Supermesh polygon intersection**:
1. For each target face polygon $P_i$ and candidate source face polygon $P_j$:
   Compute intersection polygon $P_{ij} = P_i \cap P_j$ (via Sutherland-Hodgman clipping projected onto the interface plane).
2. Compute overlap area $A_{ij} = \text{Area}(P_{ij})$.
3. Set weight:
   $$w_{ij} = \frac{A_{ij}}{A_i}$$
4. This ensures exact mathematical conservation:
   $$\sum_{j} w_{ij} = \frac{\sum_j A_{ij}}{A_i} = 1.0, \quad \sum_{j} w_{ij} A_i = A_i, \quad \sum_{i} w_{ij} A_i = A_j$$

---

### 2.5. H8: SPH-FVM Coupling & Mass-Conservative P2M (`backend/meshing/coupling.py`)

#### Exact Code Locations
- `backend/meshing/coupling.py:368-370` (`compute_boundary_area_vectors`)
- `backend/meshing/coupling.py:530-538` (`OPMISTaylorInterpolator.interpolate_p2m`)

#### Defect Analysis
1. **Tautological Invariant:** Lines 368-370 literally set $A_{iS} = -\sum V_j \nabla W_{ij}$ on boundary particles. The corresponding test asserts $\|A_{iS} + \sum V_j \nabla W_{ij}\| < 10^{-10}$, which is a tautology (identical to $x - x == 0$) and does not verify physical wall geometry.
2. **Non-Conservative P2M:** Lines 530-538 compute an unweighted arithmetic average of the nearest 8 particles (`np.mean(reconstructions)`). Particle volumes $V_p$ and cell volumes $V_c$ are ignored. Total mass and energy are not conserved.

#### Remediation Specification
1. **Geometric Boundary Area Vectors:**
   Compute $A_{iS}$ by integrating the kernel over actual FVM boundary wall faces:
   $$A_{iS} = \sum_{f \in \text{wall}} \mathbf{S}_f \, W(\|x_i - \mathbf{c}_f\|, h)$$
   where $\mathbf{S}_f$ is the outward normal area vector of face $f$ and $\mathbf{c}_f$ is its centroid.
2. **Conservative P2M Interpolation:**
   Weight Taylor reconstructions by particle volume and kernel partition of unity:
   $$\phi(x_c) = \frac{\sum_{p} V_p \, W(\|x_c - x_p\|, h) \left[ \phi_p + \nabla \phi_p \cdot (x_c - x_p) \right]}{\sum_{p} V_p \, W(\|x_c - x_p\|, h)}$$
   This guarantees $\sum_c V_c \phi_c = \sum_p V_p \phi_p$ to numerical precision.

---

### 2.6. M1 & M2: Aspect Ratio & Background-Relative `wsrd_alpha`

#### Exact Code Locations
- `backend/meshing/difvm_topology.py:1471-1481` (Aspect Ratio)
- `backend/meshing/difvm_topology.py:1491-1496` (`wsrd_alpha`)
- `backend/meshing/native/src/difvm_mesh.cpp:679-719` (C++ Aspect Ratio & `wsrd_alpha`)
- `backend/meshing/native/src/octree_hexcore.cpp:1380-1406` (Octree Aspect Ratio)

#### Defect Analysis
1. **M1 (Aspect Ratio):**
   The current code evaluates all pairs of vertices in `cell_nodes` ($i < j$) and calculates $\max(d) / \min(d)$.
   - For a unit cube $[0, 1]^3$: the minimum distance is an edge ($1.0$), and the maximum distance is the 3D space diagonal ($\sqrt{3} \approx 1.732$).
   - The reported aspect ratio is **1.732** for a perfect unit cube, instead of **1.0**!
2. **M2 (`wsrd_alpha`):**
   In `difvm_topology.py` and `difvm_mesh.cpp`, `wsrd_alpha` divides cell volume by `np.median(pos_vols)`.
   - In a graded mesh with 3 refinement levels, a full, uncut hexahedral cell at level 3 has volume $1/64$ of level 0. Since its volume is far below the global median volume, it is falsely marked as a small cut-cell ($\alpha_i < 0.20$), triggering unneeded WSRD stabilization!
   - In Berger & Giuliani (2024), $\alpha_i$ is strictly the cut volume relative to its own **uncut background Cartesian/octree cell** ($V_i / V_i^{\text{bg}}$).

#### Remediation Specification
1. **Aspect Ratio Correction (Edge-Based / OpenFOAM Definition):**
   For any cell, aspect ratio is the ratio of maximum to minimum bounding span along principal axes or edge lengths:
   $$AR = \frac{\max(L_x, L_y, L_z)}{\min(L_x, L_y, L_z)}$$
   - For a unit cube: $L_x = L_y = L_z = 1.0 \implies \mathbf{AR = 1.00}$.
2. **`wsrd_alpha` Correction:**
   In `difvm_topology.py` and `difvm_mesh.cpp`, use background cell volumes $V^{\text{bg}}$:
```python
    if v_bg is not None:
        wsrd_alpha = np.clip(cell_volumes / (v_bg + 1e-30), 0.0, 1.0)
    else:
        # For structured Cartesian fallback, v_bg is nominal h0^dim
        v_nominal = float(h0 ** topo_dim) if h0 > 0 else float(np.max(cell_volumes))
        wsrd_alpha = np.clip(cell_volumes / (v_nominal + 1e-30), 0.0, 1.0)
```
   *(Note: `octree_hexcore.cpp:1412-1415` already computed `alpha = cell_vols[old_c] / nodes[active_leaves[old_c]].v_bg`, so native octree hexcore already had the correct formulation).*

---

## 3. Implementation Priority & Verification Matrix

| ID | Module / File | Change Summary | Verification Test |
|---|---|---|---|
| **K3** | `mesh_engine.py` | Add volume-consistency check to `_detect_oriented_box_axes`; fall through to level-set mesher for non-boxes | Meshing L-profile yields volume $\approx 3.0$ (not 4.0) |
| **K4** | `mesh_engine.py` | Remove shield; default to `FarfieldPadding()` for `external_fluid` | Meshing without config creates fluid farfield; solid tests pass with `domain_mode="solid_only"` |
| **K9** | `step_parser.py` | Set Gmsh/OpenCASCADE as default; prevent B-Rep flattening of curved entities | `BladeFullRev1CFD.stp` area $\ge 310,000\text{ mm}^2$ |
| **M6/H10** | `octree_hexcore.cpp`, `mesh_engine.py` | Remove depth $\le 4$ clamp, $10^{-15}$ background overwrite, 500k cell cap, and `0.11 * h_min` tuning | Depth 6 octree generates; inverted cells flag zero/negative volume; no silent downscaling |
| **H3** | `difvm_topology.py`, `difvm_mesh.cpp` | Use $(2/3) c_f + (1/3) c_{\text{approx}}$ for 2D cell centroid integration | Quadrilateral $(0,0)-(4,0)-(1,1)-(0,1)$ yields exactly $(1.4, 0.4)$ |
| **H4** | `moving_mesh.py`, `difvm_mesh.cpp` | Build node adjacency from explicit faces; preserve topology during ALE deformation | Octree polyhedra retain volume and face count through mesh motion |
| **H2** | `gci.py` | Full Celik formulation: divergence detection ($R \ge 1$), iterative apparent order $p$, effective $r$ from cell counts | Sequence $(1.0, 1.1, 1.105)$ flags divergence; non-uniform $r_{21} \ne r_{32}$ converges |
| **H7** | `difvm_topology.py` | Replace IDW with polygon overlap (Supermesh-AMI) | Weights satisfy $\sum w \cdot A = A$ for unequal source faces |
| **H8** | `coupling.py` | Boundary $A_{iS}$ from FVM wall faces; volume-weighted conservative P2M | Total mass $\sum V_c \phi_c == \sum V_p \phi_p$ within $10^{-10}$ |
| **M1/M2**| `difvm_topology.py`, `difvm_mesh.cpp` | Aspect ratio from bounding box / edges ($1.0$ for unit cube); $V_i / V_i^{\text{bg}}$ for `wsrd_alpha` | Unit cube has $AR = 1.0$; uncut fine octree cells retain $\alpha = 1.0$ |
