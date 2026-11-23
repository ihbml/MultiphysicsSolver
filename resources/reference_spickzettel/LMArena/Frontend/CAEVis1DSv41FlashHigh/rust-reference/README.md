# Native reference — AEROFORGE Cockpit Stage 2

This crate is architecture-aligned with the web workbench in `../src`. It is deliberately
split so each Stage 2 concern can be unit tested without a GPU.

## Modules

| Module | Responsibility | Tested |
| --- | --- | --- |
| `spatial_rule` | picked patch → normal cone / bbox / cyl radius → topology-independent predicate, BC overlap audit | ✓ |
| `si_units` | recursive-descent expression parser + 7-vector dimension propagation, superscript diagnostics, Rhai upload script | ✓ |
| `probe` | hex-grid trilinear interpolation, surface barycentric sampling, plane–wall chord slicer, wall law | ✓ |
| `solver_data` | `#[repr(C)]` headers and zero-copy mmap payload views for CFDGEO01 / CFDMSH02 / CFDFLD03 | ✓ |
| `compare` | `Δφ = φB − φA` CPU reference + WGSL kernel, drag-count KPI deltas, Pareto filter | ✓ |
| `viewport` | reverse-Z wgpu pipeline, R32Uint pick target, Oklab LUTs, gizmo builder, wipe/split/delta passes | needs adapter |
| `app` | egui workbench chrome, rule/domain/probe/compare/calculator panels, egui_plot dock | needs display |

## Spatial rule authoring

`SmartSpatialRuleSynth::synthesize(&PatchTopology)` returns both the typed predicate and a
serialisable Rhai expression. The dominant normal component selects the primary clause; the
secondary clause depends on patch character:

- cylindrical tags (leading edge / fillet / hole) → `cyl_radius_yz` band
- tip / hub / shroud → tight axis band + axial normal cone
- trailing edge → axial normal cone + `bbox.max.x`
- aerodynamic surfaces → dominant normal cone + span band

The pick payload is immediately discarded. `detect_overlaps` evaluates every active rule
against the tessellated faces and reports conflicts; the renderer switches those groups to
`#EF233C` while the selected group remains `#004AFD`.

## SI checker

`compile_field("p_total = p + 0.5 * rho * dot(U, U)")` returns a `CompiledField` with the
dimension vector, quantity name, unit and a complete `TraceEntry` list. Errors are raised
before codegen. `CompiledField::rhai_upload_script` produces the sandboxed upload call:

```rhai
let field = compile_checked("p + 0.5 * rho * dot(U, U)", "Pressure");
let handle = upload_cfdfld03(field, "p_total", "Pa");
visualize(handle, Colormap::OklabTurbo);
```

Bare numeric literals are dimensionless scale factors, with two deliberate exceptions:
`287.05` is inferred as the dry-air gas constant so `sqrt(1.4 * 287.05 * T)` remains
dimensionally meaningful, and vectors must go through `dot`, `cross`, `mag` or `mag_sq`.

## wgpu viewport

- `DEPTH_FORMAT = Depth32Float`, compare `Greater`, clear 0.0, projection
  `Mat4::perspective_infinite_reverse_rh` — a true reverse-Z pipeline.
- Picking renders `face_group_id` into `R32Uint`, copies with a 256-byte aligned row pitch
  and maps asynchronously. Swap `PickTarget::read_pick` for a 1×1 scissored resolve to keep
  hover picking cheap.
- `Colormap` generates a 256-entry LUT that is sampled with `textureLoad`; Moreland
  cool–warm is the differential default.
- The A/B wipe is a single camera + two scissored draws, split mode is two half-width
  viewports. The uniform slots for run A/B/Δ are updated between draws (see inline note).
- `GizmoBuilder` implements the DPM cone (θcone, axis), FWH distance sphere and the
  anisotropic porous principal-axis triad.

## Notes for integration

1. Wire `ViewportRenderer` to your existing `wgpu::Device`/`Queue`.
2. Replace `demo_patches()` and `demo_grid()` with the CFDGEO01/CFDMSH02 readers.
3. Use the async NDJSON bridge to stream `CFDFLD03` payloads into the three field slots.
4. Keep spatial predicates in the case file; keep face-group IDs only in the pick buffer.
