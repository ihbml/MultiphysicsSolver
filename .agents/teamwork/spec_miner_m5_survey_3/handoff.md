# Milestone 5 Survey Handoff Report: Fast IO, IPC & Binary Buffer Contracts, Backend Boundaries

## 1. Observation
1. **Authoritative Specification:**
   - In `ORIGINAL_REQUEST.md` (lines 31–34), Requirement R4 specifies:
     > "Fast IO Extensions (`backend/native_rust/foam_fast_io`): Implement `boundaryField` parsing in `src/fields.rs` (`pub boundary_fields: HashMap<String, PatchFieldValues>`) supporting `uniform` and `nonuniform List<scalar|vector>` with fallback to patch `owner` cell values for `zeroGradient`/`calculated`, implement `list_time_dirs(case_path: &Path) -> Vec<TimeStepInfo>` with reconstructed vs processor status, and add `format binary;` header support in `FoamStream` (binary scalar/vector lists and `points`/`faces` via slice / mmap)."
     > "IPC & Binary Buffer Contracts: Implement JSON-RPC 2.0 / NDJSON IPC (stdin/stdout or named pipe, diagnostics strictly on stderr, async `job_id`, clean cancellation) covering `system.hello`, `schema.get` (`ModuleRegistry.export_ui_schema()`), `geometry.import`/`geometry.patches.update`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`/`case.generate`, `solver.start`/`write_now`/`stop_graceful`/`abort`, and `results.list_times`/`list_fields`/`load`. Adhere to 7-element SI dimensions `[kg, m, s, K, mol, A, cd]`, binary geometry buffers (Paket 1: vertices, normals, triangles, face groups, audit JSON), binary mesh quality fields (Paket 2: skewness, non-orthogonality, aspect ratio, volume, wsrd weights, amips energy, folded faces), and live NDJSON telemetry streaming (Paket 4: `iter` events with residuals, Courant numbers, and `n_nan_cells`)."
     > "All frontend UI (`egui`), GPU renderers (`wgpu`), shaders, colormaps, viewports, and visual presentation logic are strictly out of scope and remain entirely owned by the frontend."

2. **Existing Fast IO Implementation State (`backend/native_rust/foam_fast_io`):**
   - In `backend/native_rust/foam_fast_io/src/fields.rs` lines 120–123:
     ```rust
     "boundaryField" => {
         // We successfully parsed internalField, stop here for fast extraction
         break;
     }
     ```
     `boundaryField` is explicitly broken off and unparsed; `FoamField` lacks `boundary_fields`.
   - In `backend/native_rust/foam_fast_io/src/parser.rs`, `load_poly_mesh` loads ASCII/gzipped `points`, `faces`, `owner`, `neighbour`, `boundary`. `FoamStream` does not inspect `format binary;` in `skip_foam_header` and does not provide raw binary slice extraction methods.
   - There is no implementation of `list_time_dirs` in `foam_fast_io`.

3. **OpenFOAM Binary and Boundary Formats (`OpenFOAM_base`):**
   - In `OpenFOAM_base/openfoam/tutorials/basic/potentialFoam/pitzDaily/0.orig/U` lines 21–48: boundaryField patches specify `type`, optional `value uniform (x y z)` or `nonuniform List<vector>`. Patches with `zeroGradient` or `noSlip` omit explicit value lists.
   - In `OpenFOAM_base/openfoam/src/OpenFOAM/containers/Lists/List/UListIO.C` lines 123–134 and 270–288, OpenFOAM binary contiguous lists are written as: size label token, newline, `(`, raw binary block of $N \times \text{sizeof}(T)$ bytes, newline, `)`.
   - In `OpenFOAM_base/openfoam/src/OpenFOAM/db/IOstreams/Sstreams/ISstream.C` lines 1125–1173, binary stream reading invokes `beginRawRead()`, `readRaw(data, count)`, and `endRawRead()`.

4. **Frontend Architecture & Contracts (`FRONTEND_REQUIREMENTS_ON_BACKEND.md`):**
   - Lines 9–23: Two-channel architecture:
     - Channel A: Direct Rust crate linking (`rlib: foam_fast_io`) and flat binary buffers.
     - Channel B: JSON-RPC 2.0 / NDJSON IPC over `stdin`/`stdout` or named pipe; diagnostics strictly on `stderr`.
   - Lines 37–42: 7-element SI dimensions `[kg, m, s, K, mol, A, cd]` mandatory. Pressure must distinguish `pressure_abs` vs `pressure_rel`.
   - Lines 76–85: Paket 1 binary buffers: `vertices: [f64; 3 * N_verts]`, `normals: [f32; 3 * N_verts]`, `triangles: [u32; 3 * N_tris]`, `face_group_id: [u32; N_tris]`, and structured audit JSON.
   - Lines 117–120: Paket 2 binary mesh quality fields: `skewness`, `nonOrthogonality`, `aspectRatio`, `cellVolume`, `wsrdWeights`, `amipsEnergy`, `folded_face_indices`.
   - Lines 189–196: Paket 4 live telemetry: NDJSON `iter` and `timestep_written` events.

5. **Backend Boundary Verification:**
   - PowerShell search `Get-ChildItem -Path "backend" -Recurse -File | Select-String -Pattern "wgpu|egui|wgsl"` returned **0 occurrences**.
   - Frontend UI (`egui`), renderers (`wgpu`), shaders, and colormaps are isolated in `resources/reference_spickzettel/LMArena/Frontend/AgentModeModelUnknownRender` and `frontend/design_reference/`.

---

## 2. Logic Chain
1. *From Observation 2 & 3:* In OpenFOAM, wall quantities on boundary patches (such as wall temperature $T_w$, $y^+$, skin friction $\tau_w$) reside in `boundaryField`. Because `src/fields.rs` breaks when reaching `boundaryField`, these values are currently dropped. Adding `pub boundary_fields: HashMap<String, PatchFieldValues>` to `FoamField` allows storing these values.
2. *From Observation 1, 2 & 4:* When boundary patches use `type zeroGradient;` or `type calculated;`, they do not store values explicitly. In Finite Volume numerics, zero normal gradient implies the face value equals the adjacent owner cell value ($\phi_{\text{face}} = \phi_{\text{cell}}(\text{owner}[f])$). Providing a fallback that inspects `mesh.owner` for boundary faces ($f \in [\text{start\_face} .. \text{start\_face} + n\_faces]$) accurately populates patch values with data source marked as `PatchFieldSource::OwnerCellFallback`.
3. *From Observation 3:* OpenFOAM binary files contain an ASCII `FoamFile { format binary; ... }` header followed by an ASCII count, a `(` delimiter, dense little-endian byte arrays, and a closing `)` delimiter. Enhancing `FoamStream` to record `is_binary` and slice raw bytes via memory mapping allows zero-copy reading without ASCII string parsing overhead.
4. *From Observation 1 & 4:* The frontend cockpit communicates via JSON-RPC 2.0 / NDJSON. Long-running jobs (`geometry.import`, `mesh.generate`, `solver.start`) must return an immediate `job_id`, emit async progress events, and support `job.cancel` without producing partial corrupted artifacts. Diagnostics on `stderr` prevent stream corruption.
5. *From Observation 4:* For GPU rendering, the frontend requires flat, contiguous, camera-relative `f64` vertices, `f32` normals, `u32` triangle indices, and `u32` face group IDs (Paket 1), plus flat `f32` cell quality fields (Paket 2). The backend is solely responsible for generating these raw buffers.
6. *From Observation 5:* The backend has zero occurrences of `wgpu`, `egui`, or shaders. Keeping rendering, colormaps, and UI strictly in the frontend preserves architectural separation and satisfies the Zero-Shortcut anti-cheat review criteria.

---

## 3. Caveats
1. **Label Bitwidth:** OpenFOAM can be compiled with 32-bit labels (`label = int32_t`) or 64-bit labels (`label = int64_t`). The binary parser should default to 32-bit labels for standard OpenFOAM builds, or detect `arch` / label size if present in header.
2. **Mesh Association for Fields:** In `FoamField::load`, if no `FoamMesh` is provided, owner cell fallback cannot resolve cell indices immediately. The implementation must support binding a mesh (e.g. `load_with_mesh` or `field.apply_mesh_fallback(&mesh)`) to resolve owner cells.
3. **Decomposed Case Writing Inconsistencies:** During an active simulation, `processor` directories may temporarily have incomplete writes across MPI ranks until all ranks flush. `list_time_dirs` must track `processor_count` and flag partial writes gracefully.

---

## 4. Conclusion
The requirements, schemas, binary buffer layouts, and endpoint definitions for Milestone 5 are fully surveyed and specified:
- **Fast IO Extensions:** `FoamField` must parse `boundaryField` into `HashMap<String, PatchFieldValues>` with `uniform`, `nonuniform`, `noSlip`, and owner-cell fallback for `zeroGradient`/`calculated`. `list_time_dirs` must discover sorted numeric times across case root and `processor*`. `FoamStream` must support `format binary;` headers and raw slice extraction.
- **IPC & Binary Buffer Contracts:** 16 JSON-RPC 2.0 endpoints specified with input/output schemas and async cancellation semantics. Strict 7-element SI dimensions vector `[kg, m, s, K, mol, A, cd]` enforced. Packed binary geometry buffers (Paket 1), binary mesh quality fields (Paket 2), and live NDJSON telemetry streaming (Paket 4) fully detailed.
- **Strict Backend Boundary:** Verified that all frontend UI (`egui`), GPU renderers (`wgpu`), shaders, and colormaps remain 100% owned by the frontend.

---

## 5. Verification Method

### 5.1 Verification Commands
1. **Inspect Survey Report:**
   - Verify report file exists and is complete:
     `Get-Content -Path ".agents/teamwork/spec_miner_m5_survey_3/report.md" -TotalCount 50`
2. **Codebase Boundary Audit:**
   - Verify zero occurrences of frontend UI/GPU code in backend:
     `Get-ChildItem -Path "backend" -Recurse -File | Select-String -Pattern "wgpu|egui|wgsl" | Measure-Object` (Must return Count = 0).
3. **Inspect Crate Layout:**
   - Verify `foam_fast_io` crate files:
     `Get-ChildItem -Path "backend/native_rust/foam_fast_io/src" | Select-Object Name`
4. **Existing Test Suite Stability:**
   - Sequential pytest execution under throttled thread settings:
     `$env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; $env:OPENBLAS_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest tests/test_foam_reader.py`

### 5.2 Invalidation Conditions
This specification survey is invalidated if:
- `boundaryField` parsing drops or modifies patch data without recording the exact data provenance (`source: "boundary_field"` vs `"owner_cell_fallback"`).
- Non-numeric directories in OpenFOAM cases (e.g. `system`, `constant`, `VTK`) are parsed as time steps in `list_time_dirs`.
- JSON-RPC stdout output contains unformatted log strings or non-NDJSON text.
- Any `wgpu`, `egui`, or shader code is introduced into `backend/`.
