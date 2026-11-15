# Comprehensive Specification Survey Report: Milestone 5
**Subagent:** `spec_miner_m5_survey_3`  
**Working Directory:** `G:\Datein\Projects\CFDSolver`  
**Target Milestone:** Milestone 5 (Fast IO Extensions, IPC & Binary Buffer Contracts, Backend Boundary Verification)  
**Date:** 2026-11-12  

---

## 1. Executive Summary

This report delivers the authoritative specification mining for **Milestone 5**, encompassing:
1. **Fast IO Extensions** in `backend/native_rust/foam_fast_io`:
   - `boundaryField` parsing in `src/fields.rs` (`pub boundary_fields: HashMap<String, PatchFieldValues>`) supporting `uniform` and `nonuniform List<scalar|vector>` with automatic fallback to patch owner cell values for `zeroGradient` and `calculated` boundary conditions.
   - `list_time_dirs(case_path: &Path) -> Vec<TimeStepInfo>` for ascending numerical time discovery with reconstructed vs. decomposed (`processor0..N`) status and field discovery.
   - `format binary;` header support in `FoamStream` enabling zero-copy slice and memory-mapped parsing of binary OpenFOAM streams for points, faces, labels, and field arrays.
2. **IPC & Binary Buffer Contracts** (`FRONTEND_REQUIREMENTS_ON_BACKEND.md` and `COMMUNICATION_BACKEND_FRONTEND.md`):
   - JSON-RPC 2.0 / NDJSON IPC protocol over `stdin`/`stdout` or named pipes, keeping stderr strictly reserved for diagnostics.
   - Complete endpoint catalog (`system.hello`, `schema.get`, `geometry.import`, `geometry.patches.update`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`, `case.generate`, `solver.start`, `solver.write_now`, `solver.stop_graceful`, `solver.abort`, `results.list_times`, `results.list_fields`, `results.load`).
   - Strict 7-element SI dimensions validation vector: `[kg, m, s, K, mol, A, cd]`.
   - Paket 1: Binary geometry buffers (`vertices: [f64]`, `normals: [f32]`, `triangles: [u32]`, `face_group_id: [u32]`, structured audit JSON).
   - Paket 2: Binary mesh quality fields (`skewness`, `nonOrthogonality`, `aspectRatio`, `cellVolume`, `wsrdWeights`, `amipsEnergy`, `folded_face_indices`).
   - Paket 4: Live NDJSON telemetry streaming (`iter` events with Courant numbers and NaN cell detection; `timestep_written` events).
3. **Strict Backend Boundary Verification**:
   - Verification that all UI logic (`egui`), GPU renderers (`wgpu`), shaders (WGSL/GLSL), colormaps/palettes, viewports, and camera projections remain 100% owned by the frontend, with the backend providing only raw typed buffers and metadata.

---

## 2. Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Fast IO | `FoamField.boundary_fields` parsing | Parses OpenFOAM `boundaryField { ... }` dictionaries for all boundary patches into `HashMap<String, PatchFieldValues>` | Field file path (`&Path`), optional mesh context (`Option<&FoamMesh>`) | Populated `boundary_fields` map on `FoamField` | Returns `Err(String)` on syntax errors, malformed tokens, or missing brackets | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §3.1, `src/fields.rs` |
| 2 | Fast IO | Uniform & Nonuniform Patch Values | Extracts scalar and vector values defined as `uniform <val>` or `nonuniform List<scalar\|vector> N ( ... )` | Patch token stream in OpenFOAM dictionary | `FoamFieldData::Scalar` or `FoamFieldData::Vector` sized to patch face count | Returns `Err` on dimension mismatch or invalid float literals | OpenFOAM `UListIO.C`, `fields.rs` |
| 3 | Fast IO | Patch Owner Cell Fallback | Evaluates `zeroGradient` and `calculated` patch values by copying adjacent owner cell internal field values ($\phi_f = \phi_{\text{owner}[f]}$) | Patch face range `[start_face .. start_face + n_faces]`, `mesh.owner`, `internal_field` | Dense boundary face array with `PatchFieldSource::OwnerCellFallback` | If mesh is absent when fallback required, returns `Err` or flags unpopulated fallback | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §1.B, line 52 & 203 |
| 4 | Fast IO | `list_time_dirs` | Scans case root and `processor*` subdirectories, extracting numeric times in sorted order | `case_path: &Path` | `Vec<TimeStepInfo>` sorted by numeric time ascending | Ignores non-numeric dirs (`constant`, `system`); returns empty Vec if no times found | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §3.2 |
| 5 | Fast IO | Reconstructed vs Decomposed Detection | Determines whether each time step exists reconstructed in root or partitioned in `processor0..N` | Path scan of root and `processor0..N/<time>` | Sets `reconstructed: bool`, `is_processor: bool`, `processor_dirs: Vec<PathBuf>` | If processor dirs are missing or corrupt, marks `is_processor = false` | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §3.2 |
| 6 | Fast IO | Time Step Field Discovery | Enumerate all OpenFOAM field files present in a time directory (excluding `uniform/` dir) | Time directory path | `available_fields: Vec<String>` | Skips unreadable files or directories; handles `.gz` files transparently | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §3.2, line 194 |
| 7 | Fast IO | `format binary;` Header Detection | Inspects `FoamFile { ... }` header to identify `format binary;` vs `format ascii;` | File byte slice | `is_binary: bool` flag stored in `FoamStream` | Returns `Err` if `FoamFile` header is missing or unclosed | OpenFOAM `Istream.H`, `parser.rs` |
| 8 | Fast IO | Binary Points Zero-Copy Slice | Reads `points` binary block (`count` followed by `\n(\n` then raw IEEE-754 doubles) via memory slice | Binary byte buffer, `n_points` | `Vec<[f64; 3]>` or slice referencing mmap buffer | Fails if byte buffer length < $24 \times N_{\text{points}}$ | OpenFOAM `UListIO.C`, `Istream.C` |
| 9 | Fast IO | Binary Faces Zero-Copy Slice | Reads binary `faces` list via contiguous block or compact offset/node layout | Binary byte buffer, `n_faces` | `(face_offsets: Vec<usize>, face_nodes: Vec<usize>)` | Returns `Err` on truncated face buffers or negative label indices | OpenFOAM `UListIO.C` |
| 10 | Fast IO | Binary Scalar/Vector Field Slice | Reads `nonuniform List<scalar\|vector>` directly from contiguous binary chunk | Binary byte buffer, count $N$ | `FoamFieldData::Scalar(Vec<f64>)` ($8N$ bytes) or `Vector(Vec<[f64; 3]>)` ($24N$ bytes) | Returns `Err` on byte buffer underflow or missing trailing `)` delimiter | OpenFOAM `ListIO.C`, `fields.rs` |
| 11 | IPC | JSON-RPC 2.0 Transport Engine | Newline Delimited JSON (NDJSON) framing over `stdin`/`stdout` or named pipe; diagnostics strictly on `stderr` | NDJSON request strings on `stdin` | NDJSON response and notification strings on `stdout` | Emits JSON-RPC `-32700` (Parse error) or `-32600` (Invalid Request) on stdout; stderr for debug | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §1 (Kanal B) |
| 12 | IPC | `system.hello` | Handshake verifying backend version, protocol compatibility, capabilities, and formats | `{"protocol_version": "0.2.0"}` | `{"backend_version": "0.2.0", "capabilities": [...], "supported_formats": [...]}` | Emits error if protocol version is fundamentally incompatible | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` table line 27 |
| 13 | IPC | `schema.get` | Exports declarative parameter schemas for physics modules with SI units and dimensions | Optional `module_id` string | JSON schema with types, defaults, bounds, 7-element SI dimensions, `visible_if` | Returns `-32602` (Invalid params) if `module_id` is unknown | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 3 |
| 14 | IPC | `geometry.import` | Asynchronously imports, repairs, audits, and tessellates CAD files | `file_path`, `source_unit`, `chordal_tolerance_m`, `angular_deflection_deg`, `domain_mode` | Immediate `job_id`, followed by completion payload with bounds, audit, and binary descriptor | Emits `job.failed` event if file not found, unparseable, or non-manifold unrepairable | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 1 |
| 15 | IPC | `geometry.patches.update` | Assigns surface face groups to OpenFOAM boundary patches | `geometry_id`, `geometry_revision`, `surface_group_ids`, `patch_name`, `patch_type` | Updated patch list, incremented `geometry_revision` | Fails if `geometry_revision` is stale or surface group IDs are invalid | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` table line 30 |
| 16 | IPC | `mesh.generate` | Triggers background unstructured polyhedral/hexcore mesh generation | `geometry_id`, `strategy`, `base_cell_size_m`, `refinement_zones`, `boundary_layers`, `cut_cell_settings` | Immediate `job_id`, followed by `mesh_revision`, `polyMesh_path`, quality buffers | Emits failure if geometry is non-watertight or sizing results in inverted cells | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 2 |
| 17 | IPC | `job.cancel` | Cancels in-flight async job and purges incomplete scratch artifacts | `job_id` | `{"job_id": "...", "status": "cancelled", "cleaned_artifacts": [...]}` | Never emits partial or invalid result artifacts after cancellation | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 22, line 32 |
| 18 | IPC | `materials.list` | Returns verified thermodynamic and transport property database from `resources/materials_db/` | Optional `category` filter | List of material definitions with NASA polynomials, $\rho$, $C_p$, $\mu$, $k$ | Returns empty list or error if database file is corrupt | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 174 |
| 19 | IPC | `case.validate` | Dry-run validation of physics configuration, boundary conditions, and numerical schemes | Case setup dictionary | `{"is_valid": bool, "auto_selected_solver": str, "errors": [...], "warnings": [...]}` | Structured errors with `parameter_path` and `code` | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 175 |
| 20 | IPC | `case.generate` | Generates full OpenFOAM case directory (`controlDict`, `fvSchemes`, `fvSolution`, `0/`, `constant/`) | Case setup dictionary, target path | `{"case_path": str, "generated_files": [...], "solver": str}` | Raises fail-fast validation error on incompatible physics combinations | `case_generator.py`, `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 33 |
| 21 | IPC | `solver.start` | Launches OpenFOAM solver run under WSL2 with MPI decomposition | `case_id`, `mpi_ranks`, `solver_mode`, `end_time`, `delta_t`, `under_relaxation` | Immediate `job_id`, initial run status (`started`) | Fails if WSL2 unavailable, MPI exceeds core limit, or case files missing | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 4 |
| 22 | IPC | `solver.write_now` | Triggers OpenFOAM `writeNow` runtime instruction without stopping solver | `case_id` / `run_id` | Confirmation that `controlDict` or signal was dispatched | Returns error if simulation is not currently running | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 185 |
| 23 | IPC | `solver.stop_graceful` | Sets `stopAt writeNow` in OpenFOAM, writes final timestep cleanly, and exits | `case_id` / `run_id` | Status transition to `stopping` | Returns error if simulation process does not exist | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 186 |
| 24 | IPC | `solver.abort` | Sends SIGTERM followed by SIGKILL to WSL2 MPI solver process tree | `case_id` / `run_id` | Status transition to `aborted` | Idempotent: succeeds even if process has already terminated | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 187 |
| 25 | IPC | `results.list_times` | Returns available numeric time steps for a case (reconstructed and decomposed) | `case_id` / case path | `Vec<TimeStepInfo>` | Returns error if case path does not exist | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 35 |
| 26 | IPC | `results.list_fields` | Enumerates fields available at a given time step with dimensions and localization | `case_id`, `time_dir` | Field metadata list (`name`, `dimensions`, `location`, `class_name`) | Returns error if time directory does not exist | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 35 |
| 27 | IPC | `results.load` | Exports field data as flat binary buffer descriptor for GPU upload | `case_id`, `time_dir`, `field_name`, optional `patch_name` | Buffer descriptor (`dtype`, `shape`, `location`, `source`, `buffer_path`) | Returns error if field file is missing or unreadable | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` line 35, line 47 |
| 28 | Contracts | 7-Element SI Dimensions | Enforces strict validation of unit dimensions `[kg, m, s, K, mol, A, cd]` | 7-element integer array | Validated unit dimension or normalization error | Throws error if array length $\ne 7$ or non-integer exponents passed | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §1 (Strikte Einheitenregeln) |
| 29 | Contracts | Paket 1: Binary Geometry Buffers | Packed flat buffers for CAD preview: `vertices [f64]`, `normals [f32]`, `triangles [u32]`, `face_group_id [u32]` | Tessellated surface mesh | Flat little-endian binary buffers + Audit JSON | Bounds check violation on buffer length triggers fail-fast exception | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 1 |
| 30 | Contracts | Paket 2: Binary Mesh Quality Fields | Packed flat `[f32; N_cells]` buffers for `skewness`, `nonOrtho`, `aspectRatio`, `cellVolume`, `wsrdWeights`, `amipsEnergy` | Unstructured mesh | 6 flat binary arrays + `folded_face_indices [u32]` | Inverted cell ($V_C \le 0$) flags error in mesh summary | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 2 |
| 31 | Contracts | Paket 4: Live Telemetry Stream | Real-time NDJSON stream of solver iteration metrics, Courant numbers, residuals, and NaN count | Solver stdout stream from WSL2 | Emitted NDJSON `type: "iter"` and `type: "timestep_written"` | If `n_nan_cells > 0`, flags imminent divergence warning in telemetry stream | `FRONTEND_REQUIREMENTS_ON_BACKEND.md` §Paket 4 |
| 32 | Governance | Strict Backend Boundary | Restricts backend to data provisioning; UI, shaders, colormaps, and wgpu renderers remain in frontend | Architecture audit | Clean backend codebase with zero UI/render dependencies | Anti-cheat review rejects any wgpu/egui code introduced in backend | `ORIGINAL_REQUEST.md` R4 & Review Criteria |

---

## 3. Edge Cases

| # | Feature | Input | Observed / Expected Behavior |
|---|---------|-------|------------------------------|
| 1 | `boundaryField` parsing | Missing `boundaryField` block in scalar/vector field file | Parser succeeds in reading `internalField` and leaves `boundary_fields` empty (valid for initial fields or truncated files). |
| 2 | `boundaryField` parsing | Patch defined with `type zeroGradient;` without `FoamMesh` provided | Cannot compute owner cell indices; records `PatchFieldValues` with `source: OwnerCellFallback` but empty data array until mesh is bound via `apply_mesh_fallback(&FoamMesh)`. |
| 3 | `boundaryField` parsing | Nonuniform list with count mismatching patch face count ($N \ne \text{patch.n\_faces}$) | Throws descriptive error `Err("Patch 'inlet' count mismatch: expected 42 faces, got 30")` without memory overflow. |
| 4 | `boundaryField` parsing | Uniform vector specified with missing parenthesis: `uniform 10 0 0;` | Tokenizer detects missing `(` delimiter and returns clean syntax error instead of panicking. |
| 5 | `boundaryField` parsing | Patch type `noSlip` for velocity field `U` without explicit `value` entry | Automatically synthesizes uniform zero vector `[0.0, 0.0, 0.0]` for all $N_{\text{faces}}$ of the patch with `source: BoundaryField`. |
| 6 | `list_time_dirs` | Directory with scientific notation (`1e-05`, `2.5e-3`) alongside integers (`0`, `1`) | Parses string with `f64::from_str` and sorts correctly by floating point magnitude: `0.0 < 1e-05 < 2.5e-3 < 1.0`. |
| 7 | `list_time_dirs` | Case contains non-time directories with numbers: `postProcessing`, `processor0`, `VTK` | Strict filter ignores known system dirs and requires name to parse as pure float without alphabetical prefixes. |
| 8 | `list_time_dirs` | Decomposed case where `processor0` has times `[0.01, 0.02]` but `processor3` only has `[0.01]` | Flags `time = 0.02` as `is_processor: true`, `reconstructed: false`, `processor_count: 1` indicating incomplete write across MPI ranks. |
| 9 | `format binary;` parsing | Stream contains mixed ASCII tokens followed by raw binary block | `FoamStream` parses tokens up to `(`, switches to raw byte slicing for $N \times \text{sizeof}(T)$, then skips trailing `\n)\n` delimiter. |
| 10 | `format binary;` parsing | Binary file truncated prematurely (EOF reached before $N \times \text{sizeof}(T)$ bytes read) | Bounds check fails gracefully: returns `Err("Unexpected EOF reading binary stream: expected X bytes, found Y")`. |
| 11 | `format binary;` parsing | Endianness mismatch on binary data | OpenFOAM writes native little-endian on x86_64. Parser verifies system little-endian byte ordering or uses `byteorder::LE`. |
| 12 | JSON-RPC IPC | Client sends invalid JSON string on `stdin` | IPC server writes single-line standard JSON-RPC error response `{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}}` on `stdout`. |
| 13 | JSON-RPC IPC | Command invoked while another long-running command is running | Async commands return `job_id` immediately; thread pool processes job while main IPC thread continues accepting commands. |
| 14 | JSON-RPC IPC | `job.cancel` invoked for job that has already completed | Returns `{"job_id": "...", "status": "already_completed"}` without crashing or removing completed artifacts. |
| 15 | JSON-RPC IPC | `job.cancel` invoked during mesh generation | Cancels worker thread / subprocess, deletes scratch files in case folder, and ensures no partial `polyMesh` is published to frontend. |
| 16 | SI Dimensions | 7-element vector passed with fractional exponent: `[0.5, 1, -1, 0, 0, 0, 0]` | Rejected by schema validator; OpenFOAM dimensions require strict integer exponents `[i32; 7]`. |
| 17 | SI Dimensions | Incompressible vs compressible pressure confusion | Validator checks solver context: incompressible `simpleFoam` expects kinematic pressure $[0, 2, -2, 0, 0, 0, 0]$ ($\text{m}^2/\text{s}^2$); compressible `chtMultiRegionFoam` expects static pressure $[1, -1, -2, 0, 0, 0, 0]$ ($\text{Pa}$). |
| 18 | Binary Geometry Buffers | Mesh has degenerate triangles (zero area) | `SurfaceMesh.compute_face_normals()` clamps zero cross-product magnitude to avoid `NaN` division, producing unit normal $[0, 0, 1]$. |
| 19 | Binary Geometry Buffers | Non-manifold edge shared by 3 or more triangles | Surface auditor identifies multi-edges and adds coordinates to `non_manifold_edges` list for frontend highlighting without crashing. |
| 20 | Binary Mesh Quality | Cell has negative volume (inverted/folded cell) | Evaluator flags $V_C \le 0$ as invalid, records cell index in `folded_face_indices`, and sets `MeshQualityReport.is_valid = false`. |
| 21 | Live Telemetry Streaming | OpenFOAM solver emits `Floating point exception` or `NaN` in residuals | Telemetry parser detects `NaN` string, increments `n_nan_cells`, and emits telemetry record with warning flag before solver exits. |
| 22 | WSL Simulation Runner | WSL2 command line contains special characters in file paths | Uses `shlex.quote` on all arguments to prevent shell injection and path parsing failures across Windows/WSL mount boundaries. |

---

## 4. In-Depth Technical Specification

### 4.1 Fast IO Extensions (`backend/native_rust/foam_fast_io`)

#### 4.1.1 `boundaryField` Parsing in `src/fields.rs`

##### Current State
In `backend/native_rust/foam_fast_io/src/fields.rs`, `FoamField::load` only parses `internalField` and terminates when hitting `"boundaryField"` (lines 120–123):
```rust
"boundaryField" => {
    // We successfully parsed internalField, stop here for fast extraction
    break;
}
```
This leaves the frontend unable to visualize boundary wall quantities such as wall temperatures ($T_w$), heat flux, $y^+$, skin friction ($\tau_w$), or surface pressures directly from boundary faces.

##### Required Data Structures
```rust
use std::collections::HashMap;

/// Source indicator for boundary patch values, ensuring strict data provenance.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PatchFieldSource {
    /// True physical boundary condition values parsed from boundaryField.
    BoundaryField,
    /// Synthetic values populated from adjacent owner cells (zeroGradient/calculated).
    OwnerCellFallback,
}

/// Typed container for boundary patch field values.
#[derive(Debug, Clone, PartialEq)]
pub struct PatchFieldValues {
    pub patch_name: String,
    pub patch_type: String, // e.g. "fixedValue", "zeroGradient", "calculated", "noSlip", "empty", "symmetry"
    pub source: PatchFieldSource,
    pub data: FoamFieldData, // Scalar(Vec<f64>) or Vector(Vec<[f64; 3]>)
}

impl PatchFieldValues {
    #[inline]
    pub fn len(&self) -> usize {
        match &self.data {
            FoamFieldData::Scalar(v) => v.len(),
            FoamFieldData::Vector(v) => v.len(),
        }
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    #[inline]
    pub fn as_slice_f64(&self) -> Option<&[f64]> {
        match &self.data {
            FoamFieldData::Scalar(v) => Some(v.as_slice()),
            _ => None,
        }
    }

    #[inline]
    pub fn as_slice_vec3(&self) -> Option<&[[f64; 3]]> {
        match &self.data {
            FoamFieldData::Vector(v) => Some(v.as_slice()),
            _ => None,
        }
    }
}

/// Extended FoamField containing both internalField and boundaryField map.
#[derive(Debug, Clone)]
pub struct FoamField {
    pub name: String,
    pub class_name: String,
    pub dimensions: [i32; 7],
    pub internal_field: FoamFieldData,
    pub boundary_fields: HashMap<String, PatchFieldValues>,
}
```

##### Parsing Algorithm for `boundaryField`
1. When token `"boundaryField"` is encountered, enter the boundary dictionary block expecting `{`.
2. While inside the `boundaryField` block (tracking nesting depth):
   - Read `patch_name` (e.g. `"inlet"`, `"outlet"`, `"blade_wall"`).
   - Expect `{` for patch sub-dictionary.
   - Parse key-value pairs:
     - `type <word>;` $\rightarrow$ records `patch_type`.
     - `value uniform <val>;` $\rightarrow$ parses uniform scalar or vector.
     - `value nonuniform List<scalar|vector> N ( ... );` $\rightarrow$ parses nonuniform array of length $N$.
     - `value $internalField;` $\rightarrow$ copies internal field if applicable.
   - Close patch sub-dictionary with `}`.
3. Fallback Resolution with `FoamMesh`:
   - If a patch has `type zeroGradient;` or `type calculated;` and no explicit `value` entry:
     - Locate the patch in `mesh.boundaries` to retrieve `start_face` and `n_faces`.
     - For each face $f \in [\text{start\_face} .. \text{start\_face} + n\_faces]$:
       - Look up owner cell: $c = \text{mesh.owner}[f]$.
       - Extract value from `self.internal_field[c]`.
     - Store resulting values in `PatchFieldValues` with `source = PatchFieldSource::OwnerCellFallback`.
   - If patch has `type noSlip;` and field is vector (`volVectorField`):
     - Populate $N_{\text{faces}}$ with `[0.0, 0.0, 0.0]` and `source = PatchFieldSource::BoundaryField`.
   - If patch is `empty` or `symmetry`:
     - If mesh is present, copy owner values or record length-0 slice depending on patch semantics.

---

#### 4.1.2 Time Directory & Decomposed Case Discovery (`list_time_dirs`)

##### Function Signature & Struct Layout
```rust
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq)]
pub struct TimeStepInfo {
    /// Numerical time value for sorting and interpolation (e.g. 0.0, 0.005, 0.02)
    pub time: f64,
    /// Verbatim directory name string as formatted by OpenFOAM (e.g. "0", "0.020", "1e-05")
    pub time_dir: String,
    /// Absolute filesystem path to the root time directory (if reconstructed) or first processor dir
    pub path: PathBuf,
    /// True if the time directory is reconstructed and present in the case root
    pub reconstructed: bool,
    /// True if data for this time step is located in processor0..N subdirectories
    pub is_processor: bool,
    /// Paths to processor time directories (e.g. processor0/0.02, processor1/0.02)
    pub processor_dirs: Vec<PathBuf>,
    /// Sorted list of field names present at this time step (e.g. ["U", "p", "T", "k", "omega", "yPlus"])
    pub available_fields: Vec<String>,
}

/// Scans case_path and returns all discovered time steps in strictly ascending numerical order.
pub fn list_time_dirs(case_path: &Path) -> Result<Vec<TimeStepInfo>, String>;
```

##### Discovery Algorithm
1. Verify `case_path` exists and is a directory.
2. Initialize `time_map: HashMap<String, TimeStepInfo>`.
3. Root Scan:
   - Read entries of `case_path`.
   - Skip non-directories and system directories: `constant`, `system`, `postProcessing`, `dynamicCode`, `VTK`, `processor*`.
   - Attempt to parse directory name as `f64`. If successful:
     - List files in the time directory. Filter out subdirectories (such as `uniform/`). Strip `.gz` suffix if present to get clean field names.
     - Insert `TimeStepInfo` with `reconstructed: true, is_processor: false`.
4. Processor Scan:
   - Scan entries matching regex `^processor(\d+)$`.
   - For each processor directory, scan its subdirectories.
   - If a subdirectory name parses as `f64`:
     - If already present in `time_map`: set `is_processor = true`, append processor path to `processor_dirs`, merge new field names into `available_fields`.
     - If not present in `time_map`: insert with `reconstructed: false, is_processor: true`, add to `processor_dirs`.
5. Convert `time_map.values()` to `Vec<TimeStepInfo>` and sort ascending by `time` using `f64::total_cmp`.

---

#### 4.1.3 `format binary;` Header Support in `FoamStream`

##### OpenFOAM Binary Stream Structure
When an OpenFOAM dictionary or field is written with `writeFormat binary;`:
1. The `FoamFile` header is in ASCII:
   ```
   /*--------------------------------*- C++ -*----------------------------------*\
   FoamFile
   {
       version     2.0;
       format      binary;
       class       volScalarField;
       location    "0.02";
       object      T;
   }
   \*---------------------------------------------------------------------------*/
   ```
2. Following header, metadata entries (`dimensions`, `internalField nonuniform List<scalar>`) remain ASCII tokens until the list delimiter.
3. For contiguous lists (`points`, `faces`, labels, scalar/vector fields):
   - The size is written as an ASCII integer token (e.g. `2500`).
   - Followed by newline and opening delimiter `(`: `\n(\n`.
   - Immediately followed by raw binary bytes:
     - For `points`: $N \times 3 \times 8$ bytes (dense IEEE-754 double precision coordinates).
     - For `owner` / `neighbour`: $N \times 4$ bytes (or 8 bytes if `label` is 64-bit).
     - For `volScalarField`: $N \times 8$ bytes (`f64`).
     - For `volVectorField`: $N \times 3 \times 8$ bytes (`[f64; 3]`).
   - Followed by newline and closing delimiter `)`: `\n)\n`.

##### Extensions to `FoamStream`
```rust
pub struct FoamStream<'a> {
    bytes: &'a [u8],
    pos: usize,
    is_binary: bool,
}

impl<'a> FoamStream<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, pos: 0, is_binary: false }
    }

    #[inline]
    pub fn is_binary(&self) -> bool {
        self.is_binary
    }

    /// Skips FoamFile header and detects whether format is binary or ascii.
    pub fn skip_foam_header(&mut self) -> Result<(), String> {
        // Parse FoamFile { ... } tokens
        // If token "format" is followed by "binary;" -> self.is_binary = true;
        // If token "format" is followed by "ascii;" -> self.is_binary = false;
        ...
    }

    /// Reads raw binary bytes directly from the slice without allocation.
    pub fn read_binary_slice(&mut self, byte_count: usize) -> Result<&'a [u8], String> {
        if self.pos + byte_count > self.bytes.len() {
            return Err(format!(
                "Unexpected EOF reading binary block: requested {} bytes, only {} remaining",
                byte_count,
                self.bytes.len() - self.pos
            ));
        }
        let slice = &self.bytes[self.pos .. self.pos + byte_count];
        self.pos += byte_count;
        Ok(slice)
    }
}
```

---

### 4.2 IPC & Binary Buffer Contracts

#### 4.2.1 Protocol Framing & Architecture
- **Protocol:** JSON-RPC 2.0.
- **Framing:** Newline Delimited JSON (NDJSON) over standard I/O (`stdin`/`stdout`) or local Named Pipe (`\\.\pipe\cfdsolver_ipc`).
- **Stream Discipline:**
  - `stdin`: Inbound client JSON-RPC requests.
  - `stdout`: Outbound JSON-RPC responses, async job notifications, and live telemetry stream. **STRICTLY NDJSON ONLY.**
  - `stderr`: Unstructured log output, WSL process diagnostics, debug messages. Any non-JSON text on stdout corrupts the IPC channel!
- **Asynchronous Execution Model:**
  - Long-running operations (`geometry.import`, `mesh.generate`, `solver.start`) return an immediate `job_id`:
    ```json
    {"jsonrpc": "2.0", "id": 1, "result": {"job_id": "job-a489f", "status": "running"}}
    ```
  - Notifications are emitted during execution:
    ```json
    {"jsonrpc": "2.0", "method": "job.progress", "params": {"job_id": "job-a489f", "fraction": 0.65, "step": "surface_snapping"}}
    ```
  - On completion:
    ```json
    {"jsonrpc": "2.0", "method": "job.completed", "params": {"job_id": "job-a489f", "result": { ... }}}
    ```

#### 4.2.2 Complete Method Endpoint Catalog

##### 1. `system.hello`
- **Request:**
  ```json
  {"jsonrpc":"2.0","id":1,"method":"system.hello","params":{"protocol_version":"0.2.0"}}
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "backend_version": "0.2.0",
      "protocol_version": "0.2.0",
      "capabilities": [
        "cad_import_brep", "cad_watertight_audit", "octree_hexcore",
        "boundary_layer_inflation", "physics_registry", "foam_fast_io",
        "binary_buffers", "live_telemetry_ndjson", "wsl_mpi_solver"
      ],
      "supported_cad_formats": ["step", "stp", "stl", "obj", "prt"],
      "supported_solvers": [
        "simpleFoam", "pimpleFoam", "buoyantPimpleFoam",
        "chtMultiRegionFoam", "reactingFoam", "interFoam"
      ]
    }
  }
  ```

##### 2. `schema.get`
- **Request:**
  ```json
  {"jsonrpc":"2.0","id":2,"method":"schema.get","params":{"module_id":"Viscous"}}
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "module_id": "Viscous",
      "display_name": "Viscous & Turbulence",
      "ui_category": "flow_turbulence",
      "description": "RANS / LES turbulence modeling and wall treatment",
      "variants": ["Inviscid", "Laminar", "Spalart-Allmaras", "k-epsilon", "k-omega SST", "LES WALE"],
      "default_variant": "k-omega SST",
      "requires_modules": [],
      "parameters": {
        "turbulence_intensity": {
          "type": "float",
          "label": "Turbulence Intensity I",
          "description": "Freestream turbulence intensity at inlet",
          "default": 0.05,
          "min": 0.001,
          "max": 0.50,
          "unit_si": "-",
          "display_unit_default": "%",
          "dimensions": [0, 0, 0, 0, 0, 0, 0],
          "visible_if": {"variant_not_in": ["Inviscid", "Laminar"]}
        },
        "wall_treatment": {
          "type": "select",
          "label": "Wall Treatment",
          "options": ["lowRe_yPlus1", "nutkWallFunction"],
          "default": "lowRe_yPlus1",
          "visible_if": {"variant_in": ["k-omega SST", "Spalart-Allmaras"]}
        }
      }
    }
  }
  ```

##### 3. `geometry.import`
- **Request:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 3,
    "method": "geometry.import",
    "params": {
      "file_path": "resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp",
      "source_unit": "mm",
      "chordal_tolerance_m": 0.0001,
      "angular_deflection_deg": 10.0,
      "domain_mode": "cht_conjugate"
    }
  }
  ```
- **Response (Async Job Initialized):**
  ```json
  {"jsonrpc":"2.0","id":3,"result":{"job_id":"job-geom-001","status":"running"}}
  ```
- **Completion Event Payload:**
  ```json
  {
    "jsonrpc": "2.0",
    "method": "job.completed",
    "params": {
      "job_id": "job-geom-001",
      "result": {
        "geometry_id": "geom-blade-rev1",
        "geometry_revision": 1,
        "bounding_box": {
          "min": [-0.05, -0.02, 0.0],
          "max": [0.05, 0.02, 0.15],
          "span": [0.10, 0.04, 0.15]
        },
        "is_watertight": true,
        "surface_area_m2": 0.0125,
        "volume_m3": 0.000185,
        "n_vertices": 14520,
        "n_triangles": 29036,
        "face_groups": [
          {"id": 1, "name": "blade_suction_side", "type": "wall", "triangle_count": 12400},
          {"id": 2, "name": "blade_pressure_side", "type": "wall", "triangle_count": 12200},
          {"id": 3, "name": "blade_hub", "type": "wall", "triangle_count": 2436},
          {"id": 4, "name": "blade_shroud", "type": "wall", "triangle_count": 2000}
        ],
        "open_edge_segments": [],
        "non_manifold_edges": [],
        "binary_descriptor": {
          "buffer_path": "cases/scratch/geom-blade-rev1.bin",
          "vertices_offset_bytes": 0,
          "vertices_length_bytes": 348480,
          "normals_offset_bytes": 348480,
          "normals_length_bytes": 174240,
          "triangles_offset_bytes": 522720,
          "triangles_length_bytes": 348432,
          "face_groups_offset_bytes": 871152,
          "face_groups_length_bytes": 116144
        }
      }
    }
  }
  ```

##### 4. `geometry.patches.update`
- **Request:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 4,
    "method": "geometry.patches.update",
    "params": {
      "geometry_id": "geom-blade-rev1",
      "geometry_revision": 1,
      "surface_group_ids": [1, 2],
      "patch_name": "blade_wall",
      "patch_type": "wall"
    }
  }
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 4,
    "result": {
      "geometry_id": "geom-blade-rev1",
      "geometry_revision": 2,
      "patches": [
        {"name": "blade_wall", "type": "wall", "surface_group_ids": [1, 2]},
        {"name": "blade_hub", "type": "wall", "surface_group_ids": [3]},
        {"name": "blade_shroud", "type": "wall", "surface_group_ids": [4]}
      ],
      "validation_errors": []
    }
  }
  ```

##### 5. `mesh.generate`
- **Request:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 5,
    "method": "mesh.generate",
    "params": {
      "geometry_id": "geom-blade-rev1",
      "case_dir": "cases/turbine_blade_run01",
      "strategy": "octree_hexcore",
      "domain_mode": "fluid_external",
      "base_cell_size_m": 0.005,
      "refinement_zones": [
        {"target_patch": "blade_wall", "level": 3, "radius_m": 0.01}
      ],
      "boundary_layers": {
        "enabled": true,
        "patches": ["blade_wall"],
        "n_layers": 15,
        "first_layer_thickness_m": 1.2e-5,
        "expansion_ratio": 1.2
      },
      "cut_cell_settings": {
        "small_cell_ratio": 0.15,
        "wsrd_target_alpha": 0.5
      }
    }
  }
  ```
- **Response (Async Job Initialized):**
  ```json
  {"jsonrpc":"2.0","id":5,"result":{"job_id":"job-mesh-002","status":"running"}}
  ```
- **Completion Event Payload:**
  ```json
  {
    "jsonrpc": "2.0",
    "method": "job.completed",
    "params": {
      "job_id": "job-mesh-002",
      "result": {
        "mesh_revision": 1,
        "polyMesh_path": "cases/turbine_blade_run01/constant/polyMesh",
        "summary_report": {
          "n_cells": 28450,
          "n_points": 31200,
          "n_faces": 86120,
          "n_internal_faces": 78200,
          "cell_breakdown": {"hex": 25100, "prism": 2800, "polyhedron": 550},
          "max_skewness": 1.15,
          "max_non_ortho": 58.2,
          "min_volume": 1.45e-9
        },
        "quality_binary_descriptor": {
          "buffer_path": "cases/turbine_blade_run01/constant/polyMesh/mesh_quality.bin",
          "n_cells": 28450,
          "fields": [
            "skewness", "nonOrthogonality", "aspectRatio",
            "cellVolume", "wsrdWeights", "amipsEnergy"
          ]
        },
        "folded_face_indices": []
      }
    }
  }
  ```

##### 6. `job.cancel`
- **Request:**
  ```json
  {"jsonrpc":"2.0","id":6,"method":"job.cancel","params":{"job_id":"job-mesh-002"}}
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 6,
    "result": {
      "job_id": "job-mesh-002",
      "status": "cancelled",
      "cleaned_artifacts": ["cases/turbine_blade_run01/constant/polyMesh"]
    }
  }
  ```

##### 7. `materials.list`
- **Request:**
  ```json
  {"jsonrpc":"2.0","id":7,"method":"materials.list","params":{"category":"fluids"}}
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 7,
    "result": {
      "materials": [
        {
          "name": "air",
          "phase": "gas",
          "rho": {"type": "perfectGas", "mol_weight": 28.96},
          "Cp": {"type": "janaf", "coeffs_low": [3.25, 1.3e-3, -3.8e-7, 0, 0, 0, 0]},
          "mu": {"type": "sutherland", "mu0": 1.716e-5, "T0": 273.15, "S": 110.4},
          "Pr": 0.707
        },
        {
          "name": "water",
          "phase": "liquid",
          "rho": {"type": "constant", "value": 998.2},
          "Cp": {"type": "constant", "value": 4182.0},
          "mu": {"type": "constant", "value": 1.002e-3},
          "k_thermal": {"type": "constant", "value": 0.6}
        }
      ]
    }
  }
  ```

##### 8. `case.validate` / `case.generate`
- **Request (`case.validate`):**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 8,
    "method": "case.validate",
    "params": {
      "active_modules": [
        {"name": "Viscous", "variant": "k-omega SST"},
        {"name": "Energy", "variant": "sensibleEnthalpy"}
      ],
      "boundary_conditions": {
        "blade_wall": {"preset": "no_slip_wall_fixed_T", "T_K": 1150.0},
        "inlet": {"preset": "velocity_inlet", "U_m_s": [120.0, 0.0, 0.0], "T_K": 1400.0, "turb_intensity": 0.05},
        "outlet": {"preset": "pressure_outlet", "p_rel_Pa": 0.0}
      }
    }
  }
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 8,
    "result": {
      "is_valid": true,
      "auto_selected_solver": "chtMultiRegionFoam",
      "errors": [],
      "warnings": []
    }
  }
  ```

##### 9. `solver.start` / `write_now` / `stop_graceful` / `abort`
- **Request (`solver.start`):**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 9,
    "method": "solver.start",
    "params": {
      "case_id": "cases/turbine_blade_run01",
      "solver_mode": "transient",
      "end_time_or_iters": 0.05,
      "delta_t": 1e-5,
      "adjust_time_step": true,
      "max_co": 0.8,
      "write_interval": 0.005,
      "mpi_ranks": 16,
      "p_core_weight": 1.5,
      "e_core_weight": 1.0
    }
  }
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 9,
    "result": {
      "job_id": "job-sim-003",
      "run_id": "run-001",
      "status": "started"
    }
  }
  ```
- **Runtime Control Calls:**
  - `solver.write_now`: writes `stopAt writeNow` to `controlDict` or triggers runtime write without terminating.
  - `solver.stop_graceful`: sets `stopAt writeNow`, awaits clean flush of fields, terminates cleanly.
  - `solver.abort`: dispatches `pkill -9 -P <pid>` to WSL2 process group.

##### 10. `results.list_times` / `list_fields` / `load`
- **Request (`results.list_times`):**
  ```json
  {"jsonrpc":"2.0","id":10,"method":"results.list_times","params":{"case_id":"cases/turbine_blade_run01"}}
  ```
- **Response:**
  ```json
  {
    "jsonrpc": "2.0",
    "id": 10,
    "result": {
      "times": [
        {"time": 0.0, "time_dir": "0", "reconstructed": true, "is_processor": false, "available_fields": ["U", "p", "T"]},
        {"time": 0.005, "time_dir": "0.005", "reconstructed": true, "is_processor": true, "available_fields": ["U", "p", "T", "k", "omega", "yPlus"]},
        {"time": 0.010, "time_dir": "0.010", "reconstructed": true, "is_processor": true, "available_fields": ["U", "p", "T", "k", "omega", "yPlus"]}
      ]
    }
  }
  ```

---

#### 4.2.3 7-Element SI Dimensions Vector

Every physical quantity exchanged across the IPC boundary must be accompanied or validated against the 7-element SI base dimension vector:

$$[\text{kg},\, \text{m},\, \text{s},\, \text{K},\, \text{mol},\, \text{A},\, \text{cd}]$$

| Physical Quantity | Symbol | SI Unit | Dimension Vector `[kg, m, s, K, mol, A, cd]` |
|:---|:---|:---|:---|
| Kinematic Pressure (Incompressible) | $p/\rho$ | $\text{m}^2/\text{s}^2$ | `[0, 2, -2, 0, 0, 0, 0]` |
| Static / Absolute Pressure | $p$ | $\text{Pa} = \text{kg}/(\text{m}\cdot\text{s}^2)$ | `[1, -1, -2, 0, 0, 0, 0]` |
| Velocity | $\mathbf{U}$ | $\text{m/s}$ | `[0, 1, -1, 0, 0, 0, 0]` |
| Temperature | $T$ | $\text{K}$ | `[0, 0, 0, 1, 0, 0, 0]` |
| Density | $\rho$ | $\text{kg/m}^3$ | `[1, -3, 0, 0, 0, 0, 0]` |
| Dynamic Viscosity | $\mu$ | $\text{Pa}\cdot\text{s} = \text{kg}/(\text{m}\cdot\text{s})$ | `[1, -1, -1, 0, 0, 0, 0]` |
| Kinematic Viscosity | $\nu$ | $\text{m}^2/\text{s}$ | `[0, 2, -1, 0, 0, 0, 0]` |
| Turbulent Kinetic Energy | $k$ | $\text{m}^2/\text{s}^2$ | `[0, 2, -2, 0, 0, 0, 0]` |
| Specific Dissipation Rate | $\omega$ | $1/\text{s}$ | `[0, 0, -1, 0, 0, 0, 0]` |
| Dissipation Rate | $\epsilon$ | $\text{m}^2/\text{s}^3$ | `[0, 2, -3, 0, 0, 0, 0]` |
| Specific Heat Capacity | $C_p$ | $\text{J}/(\text{kg}\cdot\text{K}) = \text{m}^2/(\text{s}^2\cdot\text{K})$ | `[0, 2, -2, -1, 0, 0, 0]` |
| Thermal Conductivity | $\kappa$ | $\text{W}/(\text{m}\cdot\text{K}) = \text{kg}\cdot\text{m}/(\text{s}^3\cdot\text{K})$ | `[1, 1, -3, -1, 0, 0, 0]` |
| Surface Tension | $\sigma$ | $\text{N/m} = \text{kg}/\text{s}^2$ | `[1, 0, -2, 0, 0, 0, 0]` |
| Magnetic Flux Density | $\mathbf{B}$ | $\text{T} = \text{kg}/(\text{s}^2\cdot\text{A})$ | `[1, 0, -2, 0, 0, -1, 0]` |
| Mass Fraction | $Y_i$ | $\text{kg/kg} = -$ | `[0, 0, 0, 0, 0, 0, 0]` |

##### Absolute vs. Relative Pressure Rules
- `pressure_abs`: Absolute thermodynamic pressure $p$. Must be strictly positive ($p > 0$).
- `pressure_rel`: Gauge or hydrostatic pressure $p_{\text{rel}} = p - p_\infty$ or $p_{\text{rgh}} = p - \rho \mathbf{g}\cdot\mathbf{h}$. May take positive or negative values.
- Never guess units from field names! The 7-element dimension vector is authoritative.

---

#### 4.2.4 Binary Buffer Layouts

##### Paket 1: Binary Geometry Buffers (`geometry.import`)
The backend provides a tightly packed, flat binary buffer descriptor for GPU upload:
- **`vertices`**: Array of IEEE-754 64-bit floats (`f64`). Shape: `[N_verts, 3]`. Byte size: $24 \times N_{\text{verts}}$. Endianness: Little-endian.
- **`normals`**: Array of IEEE-754 32-bit floats (`f32`). Shape: `[N_verts, 3]`. Byte size: $12 \times N_{\text{verts}}$. Endianness: Little-endian.
- **`triangles`**: Array of unsigned 32-bit integers (`u32`). Shape: `[N_tris, 3]`. Byte size: $12 \times N_{\text{tris}}$. Endianness: Little-endian.
- **`face_group_id`**: Array of unsigned 32-bit integers (`u32`). Shape: `[N_tris]`. Byte size: $4 \times N_{\text{tris}}$. Identifies the CAD surface index for raycast picking.

Accompanying Audit JSON:
```json
{
  "bounding_box": {"min": [x_min, y_min, z_min], "max": [x_max, y_max, z_max]},
  "is_watertight": true,
  "surface_area_m2": 0.0125,
  "volume_m3": 0.000185,
  "open_edge_segments": [
    [[x1, y1, z1], [x2, y2, z2]]
  ],
  "non_manifold_edges": []
}
```

##### Paket 2: Binary Mesh Quality Fields (`mesh.generate`)
Quality metrics evaluated per cell, stored as flat little-endian binary arrays of `f32`:
- `skewness`: `[f32; N_cells]`, $4 \times N_{\text{cells}}$ bytes.
- `nonOrthogonality`: `[f32; N_cells]` in degrees, $4 \times N_{\text{cells}}$ bytes.
- `aspectRatio`: `[f32; N_cells]`, $4 \times N_{\text{cells}}$ bytes.
- `cellVolume`: `[f32; N_cells]` in $\text{m}^3$, $4 \times N_{\text{cells}}$ bytes.
- `wsrdWeights`: `[f32; N_cells]`, $4 \times N_{\text{cells}}$ bytes.
- `amipsEnergy`: `[f32; N_cells]`, $4 \times N_{\text{cells}}$ bytes.
- `folded_face_indices`: `[u32; N_folded]`, $4 \times N_{\text{folded}}$ bytes.

Drawing Rule:
All quality fields are bound to `location: "cell"`. They must be rendered flat (faceted per polygon) without nodal interpolation, preventing bad cells from blending into healthy neighbors.

##### Paket 4: Live NDJSON Telemetry Streaming
Streamed over `stdout` at each solver iteration:
```json
{"type":"iter","step":240,"time":0.024,"dt":1e-4,"co_mean":0.18,"co_max":0.79,"wall_s":14.2,"residuals":{"Ux":8.1e-5,"Uy":6.2e-5,"Uz":1.1e-5,"p":1.9e-3,"h":3.4e-6,"k":1.2e-4,"omega":7.5e-5},"monitors":{"Cd":0.042,"Cl":0.891,"T_max":1184.5,"n_nan_cells":0}}
```
And upon writing a time step:
```json
{"type":"timestep_written","time_dir":"0.020","reconstructed":true,"available_fields":["U","p","T","k","omega","yPlus"]}
```

---

## 5. Strict Backend Boundary Verification

### 5.1 Affirmative Responsibility Matrix

| Component / Subsystem | Backend Owns | Frontend Owns | Verification Evidence |
|:---|:---:|:---:|:---|
| CAD Parsing & Repair | YES | NO | `backend/geometry/` (OpenCASCADE / B-Rep) |
| Flat Triangulation & Vertex Buffer Generation | YES | NO | Outputs `[f64]` and `[f32]` binary buffers |
| GPU Device, Queue, RenderPass creation | NO | YES | `AgentModeModelUnknownRender/crates/cad_renderer` (`wgpu`) |
| WGSL / GLSL Shader compilation & dispatch | NO | YES | MatCap, Cavity, PBR shaders in frontend |
| Colormap LUT evaluation (Inferno, Ampel, etc.) | NO | YES | `frontend/design_reference/palettes.json` |
| egui Widgets, Outliner, Block Stack, Tabs | NO | YES | Rust desktop cockpit (`egui`) |
| Camera Matrices, Orbit, Pan, Zoom, Projection | NO | YES | Frontend camera-relative `f64` $\rightarrow$ `f32` |
| Mouse Raycasting & Triangle Selection | NO | YES | Frontend raycasting against `triangles` and `face_group_id` |
| Unstructured Hexcore / PolyMesh Meshing | YES | NO | `backend/meshing/` |
| Mesh Quality Metric Computation | YES | NO | `backend/meshing/mesh_engine.py` |
| Physics Schema Export & SI Dimensions | YES | NO | `backend/physics_modules/registry.py` |
| OpenFOAM Case Directory & Dictionaries | YES | NO | `backend/core/case_generator.py` |
| Fast IO polyMesh & Field Readers | YES | NO | `backend/native_rust/foam_fast_io` |
| WSL2 Solver Execution & MPI Signaling | YES | NO | `backend/runner/wsl_runner.py` |
| JSON-RPC 2.0 / NDJSON IPC Dispatcher | YES | NO | Backend IPC Server |

### 5.2 Verification Check
- A recursive search of `backend/` confirmed **0 occurrences** of `wgpu`, `egui`, `wgsl`, or graphics pipeline constructs.
- All renderer crates reside strictly within frontend resources (`resources/reference_spickzettel/LMArena/Frontend/AgentModeModelUnknownRender`).
- The backend remains purely a high-speed data compute and provisioning engine, adhering 100% to the boundary contract.

---

## 6. Summary of Actionable Implementation Deliverables for Milestone 5

1. **`backend/native_rust/foam_fast_io/src/fields.rs`**:
   - Add `PatchFieldSource` enum (`BoundaryField`, `OwnerCellFallback`).
   - Add `PatchFieldValues` struct with typed data accessor methods.
   - Extend `FoamField` with `pub boundary_fields: HashMap<String, PatchFieldValues>`.
   - Update `FoamField::load` (and/or provide `load_with_mesh` / `apply_mesh_fallback`) to parse `boundaryField` blocks, including uniform, nonuniform, `noSlip`, and owner-cell fallback for `zeroGradient` and `calculated`.
2. **`backend/native_rust/foam_fast_io/src/parser.rs`**:
   - Implement `list_time_dirs(case_path: &Path) -> Result<Vec<TimeStepInfo>, String>`.
   - Support `format binary;` detection in `FoamStream::skip_foam_header`.
   - Add `read_binary_slice` to `FoamStream` and implement binary list loading for `points`, `faces`, and fields.
3. **Backend IPC Server**:
   - Implement JSON-RPC 2.0 / NDJSON engine over `stdin`/`stdout` or named pipe.
   - Implement all 16 endpoints listed in Section 4.2.2.
   - Implement `export_ui_schema()` in `ModuleRegistry` with 7-element SI dimensions validation.
   - Implement binary geometry and mesh quality buffer writers for Paket 1 and Paket 2.
   - Connect live telemetry streaming from `WslSimulationRunner` to emit NDJSON `iter` and `timestep_written` events.
