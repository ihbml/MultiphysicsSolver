# AEROFORGE — CFD Cockpit · Stage 2

A dual deliverable: a **live interactive workbench** (React + TypeScript + WebGL2) and a
**modular native reference implementation** (`rust-reference/`, wgpu + egui + egui_plot + rhai).

```
src/                         web workbench (the live preview)
  lib/units.ts               7-vector SI dimension checker + AST parser
  lib/sim.ts                 procedural blade, fields, rule synthesis, plot data
  lib/colormaps.ts           Oklab-interpolated 1D LUTs (turbo / Moreland / inferno)
  lib/math3d.ts              camera, matrices, inverse projection
  components/Viewport3D.tsx  WebGL2 viewport, R32Uint pick FBO, gizmos, A/B wipe
  components/Charts.tsx      publication SVG profiles, walls-law overlay, Pareto
  components/*Panels.tsx     rule synthesizer, domain/actors, probes, compare, calculator
rust-reference/              native reference implementation
  src/spatial_rule.rs        SmartSpatialRuleSynth + overlap audit
  src/si_units.rs            SI parser/checker, Rhai upload script generation
  src/probe.rs               trilinear / barycentric probes, chord slicer, wall law
  src/solver_data.rs         CFDGEO01 / CFDMSH02 / CFDFLD03 zero-copy readers
  src/viewport.rs            wgpu reverse-Z, pick target, LUTs, gizmo builder, A/B modes
  src/compare.rs             GPU delta kernel, KPI deltas, Pareto front
  src/shaders/*.wgsl         surface / lines / R32Uint pick shaders
  src/app.rs                 egui workbench and egui_plot dock
```

## Run the web workbench

```bash
npm install
npm run dev
npm run build      # single-file dist/index.html
```

## Run the native reference

```bash
cd rust-reference
cargo test          # SI checker, rule synthesis, overlap, trilinear probes, Pareto
cargo run --release
```

The native entry point requests the wgpu backend from eframe and presents the cockpit at
1600 × 980. The viewport renderer is exposed as an independent module so it can be embedded
in an existing `wgpu` device without eframe.

## Stage 2 features

### 1 · Click-to-author spatial boundary rules
- The WebGL2 pass renders `face_group_id` into an **R32Uint** offscreen texture and reads it
  back at the cursor. The integer is **never stored as a rule**.
- The synthesizer inspects the picked patch (area-weighted normal cone, centroid, AABB,
  cylindrical radius, STEP tag) and emits a topology-independent predicate such as
  `normal.z > 0.905 and abs(y - 0.360) < 1e-3`.
- Matched faces render in **#004AFD Electric Blue**. Any face group claimed by two active
  BCs is flagged and drawn in **#EF233C**. A red audit card reports the conflict.

### 2 · Domain sizing and physics actors
- Farfield cage normalised by chord: **5L upstream / 15L downstream / 5L radial**.
- Cylindrical MRF rotor zone with live RPM, tip radius, tip speed and tip Mach.
- Drag-and-drop actor palette: **DPM spray injector** (axis, cone θ, Rosin–Rammler d₃₂),
  **FWH microphone** (x_mic + distance sphere), **anisotropic porous zone** (e₁ e₂ e₃ + κ).

### 3 · Interactive probe / scientific plots
- Line rake `pA ↔ pB` with draggable endpoints, or a plane–wall chord slice.
- Trilinear cell sampling for volume fields and barycentric wall sampling for surface fields.
- Synchronised plots: **Cp(x/c)** with optional inverted y, **Tw(x/c)**, **y⁺(x/c)** with
  y⁺ = 1 and y⁺ = 30 target bands, and **u⁺(y⁺)** against both viscous and logarithmic
  law-of-the-wall curves. Hover readouts, deltas and export actions included.

### 4 · Multi-run A/B difference
- Wipe, split and full-frame delta modes share one camera; the split layout uses two
  half-width viewports with an identical projection.
- `Δφ = φB − φA` is computed through a **Moreland cool–warm Oklab LUT centred at zero**.
- KPI deltas: **ΔCd in drag counts, Δp₀ loss %, ΔTmax**, efficiency, mass flow and y⁺ plus a
  non-dominated **Pareto** scatter.

### 5 · SI-checked field calculator
Every AST node is annotated with `[kg, m, s, K, mol, A, cd]` before execution:

- `+ / −` require identical dimension vectors
- `× / ÷` add / subtract exponents
- `dot` adds operand dimensions, `grad` subtracts one metre, `div`/`curl` behave likewise
- `sqrt` halves every exponent; `mag`/`mag_sq` preserve (or double) dimensions
- vector results are rejected for scalar-field upload

The canonical illegal expression

```
p + 0.5 * dot(U, U)
```

is rejected at parse time with

```
Cannot add Pressure [kg·m⁻¹·s⁻²] and Specific kinetic energy [m²·s⁻²]
— multiply by Density rho [kg·m⁻³]
```

Valid expressions upload their scalar payload to a **CFDFLD03** descriptor and are immediately
colour-mapped in the viewport. The Rhai console surfaces the same type-check pipeline.

## Design language
Obsidian `#111217` / carbon `#18181C` surfaces, hairline `#24252c` rules, Electric Blue
`#004AFD` interaction colour, warning red `#EF233C`, actor amber `#FFB020`, with tabular
JetBrains Mono numerics and Inter UI type. Camera/lighting is intentionally neutral so the
scientific colour field stays legible.
