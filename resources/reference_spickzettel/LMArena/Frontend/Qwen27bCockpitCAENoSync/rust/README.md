# HydroCore Cockpit — Rust reference (wgpu + egui)

Desktop reference implementation of the HydroCore multiphysics CFD cockpit.
The web prototype in `src/` mirrors this architecture 1:1 (WebGL2 ↔ wgpu,
in-process NDJSON ↔ stdio NDJSON).

## Build

```bash
cd rust
cargo run --release
# backend binary path overridable:
HYDROCORE_BACKEND=./target/release/foam-core-backend cargo run --release
```

## Architecture

```
┌────────────────────────── egui app (ui/app.rs) ──────────────────────────┐
│  TopBar (5 workspaces) · BlockStack (schema-driven) · Inspector          │
│  CentralPanel → wgpu::SurfaceTexture (render/pipeline.rs)                │
│  BottomPanel → convergence monitors (log residuals, Co, Cd/Cl/Tmax)      │
└──────────────┬───────────────────────────────────────┬───────────────────┘
               │ GPU readback (ID texture → staging)   │ UiState (revision tracking)
┌──────────────▼──────────────────┐     ┌──────────────▼──────────────────┐
│ render/pipeline.rs              │     │ ipc/client.rs                   │
│ phase 1: [albedo|normal|id] MRT │     │ tokio::process Child            │
│         + Depth32Float (rev-Z)  │     │ stdin  BufWriter  NDJSON reqs   │
│ phase 2: MatCap + EDL compositor│     │ stdout Lines  → pending map     │
│ f64 → f32 hi/lo camera-relative │     │        + telemetry bus          │
│ GPU ray-pick: readPixels(id)    │     │ stderr  → 4096-line ring buffer │
└─────────────────────────────────┘     └─────────────────────────────────┘
        shaders.wgsl                          Channel B: JSON-RPC 2.0
                                              16 endpoints + iter/timestep
                                              streams; foam_fast_io maps
                                              (Channel A) imported behind
                                              geometry.import / results.load
```

## Key invariants

* **Reverse-Z infinite projection** — `Depth32Float`, `CompareFunction::GreaterEqual`,
  depth 1.0 @ near → 0.5 @ ∞. A `Δy₁ = 1e-6 m` boundary layer on a 10 m domain
  keeps ~17 bits of depth separation; no z-fighting on thin walls.
* **Camera-relative f64→f32** — vertices are `f64` on the CPU. The vertex shader
  subtracts a hi/lo split `f32` center (`uCamRel`), keeping coordinates small
  before the f32 stage. See `uCamRel` in `shaders.wgsl`.
* **Flat-faceting invariant** — `location:"cell"` quality metrics are drawn as
  independent triangles with per-triangle-constant color (zero interpolation).
  Defective / NaN cells can never be smoothed away.
* **Unlit post mode** — zero tone-mapping; Oklab LUTs (Viridis / Moreland /
  Fire / Dark Ember / Traffic Light) uploaded as 256×1 `Rgba8Unorm`, optional
  N-band quantization with isoline darkening.
* **Revision tracking** — `geometry_revision` / `mesh_revision` from backend
  results; BC bindings carry the `bound_to` revision and are flagged STALE on
  remesh until re-applied via `geometry.patches.update`.
* **Cooperative cancel** — `job.cancel` is a normal RPC; the backend honors it
  between PISO iterations (checkpoint flush on `stop_graceful`, none on `abort`).
* **NaN emergency** — `iter` frames carry `n_nan_cells`; the UI raises a red
  alert frame and the mesh pass pulses the flagged cells (val < 0 encoding).

## Unit-aware expressions

`ui/app.rs::ExprField` runs the same 7-element SI dimension parser as the web
build (`units/` — shared crate in production; mirrored here for brevity).
`12.5 mm → 0.0125 m`, `3.2 bar → 3.2e5 Pa`, `1500 rpm → 157.08 s⁻¹`,
`850 C → 1123.15 K` (offset units), with dimension-vector validation before
any RPC payload is written.
