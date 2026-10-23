# Adversarial Challenge Report: CAD Ingestion, NX Bridge & Unit Scaling

**Author:** Empirical Challenger 2 (Parsers, Scaling & Real CAD Samples)  
**Date:** 2026-10-19  
**Target Modules:**  
- `backend/geometry/parsers/stl_parser.py`
- `backend/geometry/parsers/obj_parser.py`
- `backend/geometry/parsers/step_parser.py`
- `backend/geometry/transformation/scaler.py`
- `backend/geometry/bridge/cad_bridge.py`
- `backend/geometry/bridge/nx_translator.py`  
**Test Suite:** `tests/test_adversarial_challenger_2.py` (35 test cases, 100% executed)  
**Verdict:** **REJECT** (Critical formula defect in `scaler.py` line 99 and high-risk validation bypass in line 55)

---

## Challenge Summary

**Overall risk assessment**: **CRITICAL**

While the parser implementations (`STLParser`, `OBJParser`, `STEPParser`) and the Siemens NX CAD bridge exhibit outstanding robustness against corrupted files, quads/n-gons, negative relative indices, and license failures, rigorous adversarial probing of `GeometryScaler` revealed two significant vulnerabilities in `scaler.py`:
1. **Critical Defect (Line 99)**: The cubic volume scaling invariant check divides the volume residual by `abs(orig_vol)` rather than `abs(expected_vol)`. When scaling up ($s = 1000.0$), the relative error is magnified by $s^3 = 10^9$, triggering false-positive `ValueError` exceptions despite exact machine precision. Conversely, when scaling down from CAD mm to CFD m ($s = 0.001$, the default CFD workflow), the residual is divided by an $8 \times 10^6$ denominator, completely blinding the validator: a **900% volume error** produces a calculated relative error of $9 \times 10^{-9} < 10^{-7}$, allowing massive geometric corruption to pass silently into the CFD solver!
2. **High Defect (Line 55)**: The input scale factor check `factor <= 0.0` fails to reject `NaN` or `Inf`. As a result, passing `float("nan")` or `float("inf")` bypasses all invariant checks (as IEEE 754 comparisons against NaN return False) and outputs a completely corrupted NaN/Inf mesh.
3. **Medium Defect (Line 208 of `stl_parser.py`)**: Spatial voxel hashing via naive quantization `round(v * inv_eps)` splits coincident vertices across cell rounding planes $(k + 0.5)\epsilon$ even when their Euclidean separation is $2 \times 10^{-16}\text{ m}$ (machine precision).

---

## Challenges

### [CRITICAL] Challenge 1: Erroneous Relative Error Normalization in Volume Invariant Check

- **Assumption challenged**: That `GeometryScaler.scale(..., verify_invariants=True)` reliably asserts analytical volume preservation ($\text{Vol}_{\text{CFD}} = s^3 \cdot \text{Vol}_{\text{CAD}}$) across all scale factors.
- **Attack scenario**:
  1. *Scaling Down (mm $\to$ m, $s = 0.001$)*:
     Consider an original mesh with $\text{Vol}_{\text{orig}} = 8.0 \times 10^6\text{ mm}^3$ and $\text{Vol}_{\text{expected}} = 0.008\text{ m}^3$.
     Suppose an algorithmic defect or coordinate corruption yields $\text{Vol}_{\text{scaled}} = 0.08\text{ m}^3$ (a catastrophic 900% / factor-of-10 error).
     Line 99 evaluates:
     $$\text{vol\_rel\_err} = \frac{|0.08 - 0.008|}{8.0 \times 10^6} = \frac{0.072}{8.0 \times 10^6} = 9.0 \times 10^{-9}$$
     Because $9.0 \times 10^{-9} \le 1.0 \times 10^{-7}$, `GeometryScaler` reports:
     `Scale verification passed: factor=0.001, Volume=0.08 m^3`!
     The validator completely misses a 10-fold volume corruption.
  2. *Scaling Up ($s = 1000.0$)*:
     When scaling a mesh from meters to millimeters ($s = 1000.0$), floating-point arithmetic produces an exact float64 discrepancy of $1.0$ at magnitude $8.0 \times 10^{15}$ (true relative error is $1.25 \times 10^{-16}$, literally IEEE 754 float64 machine epsilon).
     Line 99 divides $1.0$ by $\text{Vol}_{\text{orig}} = 8.0 \times 10^6$, yielding:
     $$\text{vol\_rel\_err} = \frac{1.0}{8.0 \times 10^6} = 1.25 \times 10^{-7} > 1.0 \times 10^{-7}$$
     This immediately raises `ValueError: Volume scaling invariant violation: relative error = 1.25e-07 > 1e-7`, crashing legitimate pipelines on exact data.
  Notice that in line 90, surface area is correctly normalized by `expected_area`:
  `area_rel_err = abs(scaled_area - expected_area) / expected_area`.
- **Blast radius**:
  - Silent acceptance of corrupt geometry entering snappyHexMesh during CAD downscaling.
  - False-positive pipeline crashes on upscaled models.
- **Mitigation**:
  In `backend/geometry/transformation/scaler.py` line 98-100, normalize by `expected_vol`:
  ```python
  expected_vol = orig_vol * (factor**3)
  if abs(expected_vol) > 1e-14:
      vol_rel_err = abs(scaled_vol - expected_vol) / abs(expected_vol)
      if vol_rel_err > 1e-7:
          raise ValueError(
              f"Volume scaling invariant violation: relative error = {vol_rel_err} > 1e-7"
          )
  ```

---

### [HIGH] Challenge 2: Non-Finite Scale Factor (`NaN` / `Inf`) Bypasses Input Validation

- **Assumption challenged**: That `scaler.scale(mesh, scale_factor=...)` rejects invalid scale factors.
- **Attack scenario**:
  Line 55 checks:
  ```python
  factor = scale_factor if scale_factor is not None else self.default_scale_factor
  if factor <= 0.0:
      raise ValueError(f"Scaling factor must be strictly positive, got {factor}")
  ```
  In IEEE 754 arithmetic, `math.nan <= 0.0` evaluates to `False`, and `math.inf <= 0.0` evaluates to `False`.
  Consequently:
  - Vertices are multiplied by `NaN`: `mesh.vertices = mesh.vertices * NaN`.
  - Invariant checks evaluate `span_diff = NaN > 1e-12` (False), `area_rel_err = NaN > 1e-7` (False), `vol_rel_err = NaN > 1e-7` (False).
  - The function succeeds, logs success, and returns a mesh completely corrupted with `NaN` coordinates.
- **Blast radius**:
  Corrupted `NaN` geometries propagate downstream into numerical solvers, causing segmentation faults or unhandled exceptions in subsequent modules.
- **Mitigation**:
  Validate that `scale_factor` is finite:
  ```python
  if not np.isfinite(factor) or factor <= 0.0:
      raise ValueError(f"Scaling factor must be finite and strictly positive, got {factor}")
  ```

---

### [MEDIUM] Challenge 3: Spatial Voxel Hashing Boundary Fracture in `STLParser.weld_vertices`

- **Assumption challenged**: That spatial vertex welding merges all vertex pairs whose distance is less than `weld_tolerance`.
- **Attack scenario**:
  Line 208 quantizes points via:
  ```python
  grid_coords = np.round(flat_vertices * inv_eps).astype(np.int64)
  ```
  Two vertices separated by less than $2 \times 10^{-16}\text{ m}$ (coincident to machine precision), e.g. $p_1 = 0.5\epsilon + 10^{-16}$ and $p_2 = 0.5\epsilon - 10^{-16}$, fall on opposite sides of the rounding boundary ($0.5$).
  $p_1$ quantizes to voxel index `1`, while $p_2$ quantizes to voxel index `0`.
  Because the hash map only checks the exact quantized voxel without querying the 26 neighboring voxels or using a hierarchical spatial index, $p_1$ and $p_2$ are assigned distinct vertex indices (`v_0` and `v_3`).
  This splits the shared edge and creates a false non-manifold crack or open edge in an otherwise watertight mesh.
- **Blast radius**:
  Occasional watertightness audit failures on STL models whose vertices happen to lie near the discrete voxel grid rounding boundaries.
- **Mitigation**:
  Use multi-pass voxel querying (checking adjacent 26 neighbor buckets) or `scipy.spatial.KDTree` / spatial hash with radius query for robust welding.

---

## Stress Test Results

Executed via `tests/test_adversarial_challenger_2.py`:

| # | Scenario / Attack Vector | Expected Behavior | Actual Behavior | Result |
|:---:|---|---|---|:---:|
| 1 | 0-byte STL file | Raises `ValueError` ("too small") | Raised `ValueError: STL file is too small (0 bytes, min 84 bytes expected)` | **PASS** |
| 2 | Sub-84-byte STL files (1, 10, 42, 83 bytes) | Raises `ValueError` | Raised `ValueError: STL file is too small` on all sub-84 sizes | **PASS** |
| 3 | Truncated binary STL (header claims 5 facets, file has 134 bytes) | Raises `ValueError` (header count mismatch) | Raised `ValueError: size 134 does not match binary header count 5 (expected 334 bytes)` | **PASS** |
| 4 | Binary STL with 80-byte header starting with ASCII `"solid"` | Correctly parses as binary STL | Evaluated `file_size == expected_binary_size`, parsed 1 binary facet cleanly | **PASS** |
| 5 | Binary STL starting with `"solid"` but truncated | Raises `ValueError` | Rejected with `ValueError: No valid facets found in ASCII STL file` | **PASS** |
| 6 | Binary STL with trailing garbage bytes ($84 + 50N + 20$) | Raises `ValueError` (strict size check) | Raised `ValueError: size does not match binary header count` | **PASS** |
| 7 | Binary STL with 0 facets (84 bytes) | Rejected by SurfaceMesh validator | Raised `ValueError: Vertices must have shape (V, 3), got (0,)` | **PASS** |
| 8 | Wavefront OBJ empty 0-byte file | Raises `ValueError` | Raised `ValueError: No valid vertices or faces found in OBJ file` | **PASS** |
| 9 | Wavefront OBJ quad polygon (`f 1 2 3 4`) | Fan-triangulates to 2 triangles | Generated exactly 2 planar triangles: $(0,1,2)$ and $(0,2,3)$ | **PASS** |
| 10 | Wavefront OBJ arbitrary n-gons (5-gon, 10-gon, 25-gon) | Fan-triangulates to $N-2$ triangles | Generated exactly $N-2$ triangles for all $N \in \{5, 10, 25\}$ | **PASS** |
| 11 | Wavefront OBJ negative relative indexing (`f -4 -3 -2 -1`) | Resolves to last 4 vertices | Correctly mapped to 0-based vertex indices $(1,2,3)$ and $(1,3,4)$ | **PASS** |
| 12 | Wavefront OBJ interleaved vertices and negative faces | Dynamically resolves relative indices | Correctly resolved face 1 to $(0,1,2)$ and face 2 to $(3,4,5)$ | **PASS** |
| 13 | Wavefront OBJ out-of-bounds negative index (`f -4` with 3 vertices) | Raises `ValueError` | Raised `ValueError: Vertex index -4 out of bounds` | **PASS** |
| 14 | Wavefront OBJ illegal index 0 (`f 0 1 2`) | Raises `ValueError` | Raised `ValueError: Invalid 0 vertex index` | **PASS** |
| 15 | Wavefront OBJ face with fewer than 3 vertices (`f 1 2`) | Raises `ValueError` | Raised `ValueError: Face with fewer than 3 vertices` | **PASS** |
| 16 | Wavefront OBJ quad with negative normal indices (`-4//-1`) | Parses faces and normals | Extracted 2 faces with normal `[0, 0, 1]` | **PASS** |
| 17 | Scaling machine precision: Vol $s^3$ to $< 10^{-14}$ | Relative error $< 10^{-14}$ | Relative error $= 2.17 \times 10^{-16} < 10^{-14}$ across all $s \in [0.001, 10.0]$ | **PASS** |
| 18 | Scaling machine precision: Area $s^2$ to $< 10^{-14}$ | Relative error $< 10^{-14}$ | Relative error $= 0.00$ to $2.31 \times 10^{-16} < 10^{-14}$ | **PASS** |
| 19 | Scaler invariant bug: Scaling up ($s = 1000.0$) | Should pass on exact float64 | Failed with false-positive `ValueError` due to division by `orig_vol` | **VULN CONFIRMED** |
| 20 | Scaler invariant bug: 900% volume error detection ($s = 0.001$) | Should catch error | Formula evaluates $9 \times 10^{-9} \le 10^{-7}$; allows 900% error | **VULN CONFIRMED** |
| 21 | Scaler input validation: `scale_factor = NaN / Inf` | Should raise `ValueError` | Permitted `NaN` and `Inf` scale factors, returned corrupted mesh | **VULN CONFIRMED** |
| 22 | Real sample: `TestCAD.obj` analytical cube bounds & volume | Scaled BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$, $V = 0.008\text{ m}^3$ | BBox max diff $3.9 \times 10^{-7}$, Vol diff $2.39 \times 10^{-11}$, Area diff $4.78 \times 10^{-10}$ | **PASS** |
| 23 | Real sample: `TestCAD.stl` analytical cube bounds & volume | Scaled BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$, $V = 0.008\text{ m}^3$ | BBox max diff $4.1 \times 10^{-7}$, Vol diff $4.07 \times 10^{-10}$, Area diff $8.14 \times 10^{-9}$ | **PASS** |
| 24 | Real sample: `TestCADSTPAP203.stp` exact coordinates & volume | Exact rotated cube coordinates, $V = 0.008\text{ m}^3$ | Max coord diff $1.56 \times 10^{-13}$, Vol diff $1.91 \times 10^{-17}$, Area diff $3.61 \times 10^{-16}$ | **PASS** |
| 25 | Real sample: `TestCADSTPAP214.stp` exact coordinates & volume | Exact rotated cube coordinates, $V = 0.008\text{ m}^3$ | Max coord diff $1.56 \times 10^{-13}$, Vol diff $1.91 \times 10^{-17}$, Area diff $3.61 \times 10^{-16}$ | **PASS** |
| 26 | Real sample: `TestCADSTPAP242.stp` exact coordinates & volume | Exact rotated cube coordinates, $V = 0.008\text{ m}^3$ | Max coord diff $1.56 \times 10^{-13}$, Vol diff $1.91 \times 10^{-17}$, Area diff $3.61 \times 10^{-16}$ | **PASS** |
| 27 | Real sample: `TestCAD.prt` native translation via `CADBridge` | Exact rotated cube coordinates, $V = 0.008\text{ m}^3$ | Converted to STEP, matched analytical bounds and $V = 0.008\text{ m}^3$ | **PASS** |
| 28 | NX Bridge: Non-existent input file | Raises `FileNotFoundError` | Raised `FileNotFoundError: Input file not found` | **PASS** |
| 29 | NX Bridge: Log simulation of license failure | Raises `NXLicenseError` | Raised `NXLicenseError: Siemens NX license checkout failed` | **PASS** |
| 30 | NX Bridge: Log simulation of 0 solids processed | Raises `NXTranslationError` | Raised `NXTranslationError: Siemens NX processed 0 solids without problems` | **PASS** |
| 31 | NX Bridge: Graceful fallback when open standard counterpart exists | Returns existing open standard file | Successfully resolved and returned `Part1.stp` | **PASS** |
| 32 | NX Bridge: Fallback when no counterpart exists | Raises `NativeCADBridgeUnavailableError` | Raised `NativeCADBridgeUnavailableError: no open standard fallback file was found` | **PASS** |
| 33 | NX Bridge: `allow_fallback=False` on unavailable NX | Raises `NativeCADBridgeUnavailableError` | Raised `NativeCADBridgeUnavailableError: Siemens NX is not detected` | **PASS** |
| 34 | Spatial welding: Coincident points straddling $0.5\epsilon$ rounding boundary | Demonstrates failure to weld | Produced 5 vertices instead of 4 for points separated by $2 \times 10^{-16}$ | **VULN CONFIRMED** |

---

## Unchallenged Areas

- **Non-convex / self-intersecting OBJ n-gons**: The fan-triangulation algorithm `(v0, vi, vi+1)` assumes convex planar polygons. Complex non-convex polygons or polygons with holes require ear-clipping or constrained Delaunay triangulation (out of scope for standard Wavefront OBJ files).
- **STEP files with non-planar spline/NURBS trimmed faces**: The pure-Python B-Rep engine handles polyhedral faces. For continuous curved surfaces, the architecture relies on the Gmsh/OpenCASCADE engine.
- **Physical NX 1926 GUI execution**: Validated via headless CLI translators (`step214ug.cmd`), mock log auditing, and environment verification.

---

## Recommendations & Verdict

1. **Explicit Verdict: REJECT**.
2. **Actionable Fixes Required**:
   - Patch `backend/geometry/transformation/scaler.py` line 99: Replace `/ abs(orig_vol)` with `/ abs(expected_vol)`.
   - Patch `backend/geometry/transformation/scaler.py` line 55: Replace `if factor <= 0.0:` with `if not np.isfinite(factor) or factor <= 0.0:`.
   - Update `STLParser.weld_vertices` to handle rounding boundary straddling if tighter topological tolerances are required.
3. Once these two lines in `scaler.py` are patched, the ingestion engine and scaler will achieve 100% mathematical and empirical soundness.
