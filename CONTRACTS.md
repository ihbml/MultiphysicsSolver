# CONTRACTS.md — Normative Units, Domain Modes, I/O & C-ABI Contracts

## 1. Units & Coordinate System
- **Internal Solver & Mesher Unit:** SI metres (`m`), kilograms (`kg`), seconds (`s`), Kelvin (`K`).
- **STEP Ingestion (`STEPParser`):**
  - Reads `SI_UNIT` / `CONVERSION_BASED_UNIT` across the **entire** STEP file (whitespace-insensitive).
  - Sets Gmsh `Geometry.OCCTargetUnit` to match the detected file unit so `gmsh` and `brep` engines return identical model coordinates and `metadata["units"]` (`"mm"`, `"m"`, `"cm"`, `"in"`).
  - Enforces a bounding-box surface triangle budget prior to `gmsh.model.mesh.generate(2)` to prevent OOM kills on large-scale models.
  - `CADGeometryEngine.scale()` converts automatically to SI `m` using `metadata["units"]` when `scale_factor=None` (`1.0` for `"m"`, `0.001` for `"mm"`, `0.01` for `"cm"`, `0.0254` for `"in"`).
- **Translation Invariance:** `OctreeHexcore::generate()` shifts input geometry to its local bounding-box midpoint before octree construction, quantization, and polygon integration, and shifts output coordinates back at export (`AUD-031`).

## 2. Domain Modes & `farfield_bbox`
- **Valid `domain_mode` values (`Literal`):** `"external_fluid"` (`0`), `"internal_fluid"` (`1`), `"solid_only"` (`2`), `"cht_coupled"` (`3`). Unknown strings raise `ValueError` immediately (`AUD-008`).
- **Precedence:** An explicitly passed `domain_mode` argument to `MeshEngine.mesh_hexcore(..., domain_mode=...)` takes precedence over `MeshingConfig`'s default; `config.domain_mode` is used when `domain_mode is None`.
- **`farfield_bbox` C-ABI Layout (`AUD-003`):**
  - Across C++ (`cfd_mesher_generate_hexcore`), Python (`generate_hexcore_native`), and `MeshingConfig`, the 6-double flat array is **strictly** `[xmin, ymin, zmin, xmax, ymax, zmax]` (`min_pt[3]` followed by `max_pt[3]`), with `xmin < xmax`, `ymin < ymax`, `zmin < zmax`. Heuristics that guess interleaved `(xmin, xmax, ...)` are forbidden.
- **Boundary Patch Naming (`AUD-001`, `AUD-032`):**
  - Farfield patch names (`inlet`, `outlet`, `sides`, `top`, `bottom`) are emitted **only** in `external_fluid` (`domain_mode == 0`).

## 3. C-ABI Safety & Handshake (`abi_version = 2`)
- **Runtime Layout Handshake:** On load, Python calls `cfd_abi_layout_json()` and verifies `abi_version`, `sizeof(MeshingConfigC)`, `sizeof(BoundaryPatchC)`, and every field offset against `ctypes` (`AUD-037`).
- **Input Validation (`AUD-007`):** Every `extern "C"` entry point validates pointer non-nullness, finite coordinates (`std::isfinite`), triangle/cell vertex index bounds `[0, n_points)`, and cell minimum distinct vertex count before indexing memory.
- **Error Reporting (`AUD-039`):** Diagnostic invariant functions return `NaN` on null/invalid handles (never `0.0`), and `cfd_mesher_last_error()` provides human-readable pass & reason diagnostics.

## 4. OpenFOAM Binary Format (`AUD-015`)
- Binary `polyMesh` files (`points`, `owner`, `neighbour`) include `arch "LSB;label=64;scalar=64";` in `FoamFile` and write `<count>\n(` followed **immediately** by raw binary bytes and `)` (never `<count>\n(\n`, and the reader never strips whitespace bytes inside the binary payload).
