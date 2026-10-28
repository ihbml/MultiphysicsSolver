# FVM / SPH Mesher — Rust core

Rust implementation of the geometry, unstructured meshing, Eulerian-SPH particle
relaxation, FVM-equivalent boundary reconstruction, adaptive SPH–FVM coupling
operators and exporters used by the web UI.

The browser build ships a TypeScript engine with the identical algorithms and
JSON contract, so the demo is static. This crate is the performance path:

```
web client (React/canvas, no numerics in the hot path)
        │  JSON / (future) flat binary
        ▼
Rust core  ── axum job API ── rayon data-parallel kernels
        │
        └── mesh.vtk · particles.vtk · DiFVM graph.json · OpenFOAM polyMesh
```

## Run

```bash
cd backend
cargo run --release                 # http://0.0.0.0:8080
MESHER_ADDR=127.0.0.1:9000 cargo run --release
```

or:

```bash
docker build -t fvm-sph-mesher .
docker run --rm -p 8080:8080 fvm-sph-mesher
```

## API

Same field names as `src/lib/mesh.ts`, `src/lib/sph.ts` (camelCase).

| method | path | body / query |
| --- | --- | --- |
| GET | `/api/health` | — |
| POST | `/api/mesh` | `{ "geometry": "cylinder", "params": { "h0": 0.05, "grading": 0.45, "hmaxRatio": 3, "iters": 45 } }` |
| POST | `/api/sph` | `{ "geometry": "cylinder", "params": { "dp": 0.055, "hFactor": 1.3, "iters": 60, "kernel": "wendland", "refineRatio": 1, "grading": 0.6, "volumeReinit": true, "lattice": "square", "core": [1.1, 0.8, 0.62] } }` |
| POST | `/api/couple` | `{ "geometry": "cylinder", "dp": 0.055, "hFactor": 1.3, "kernel": "wendland", "core": { "x": 1.1, "y": 0.8, "r": 0.62, "buffer": 3 }, "scheme": "opmis" }` |
| GET | `/api/export/mesh.vtk?geometry=cylinder&h0=0.05` | — |
| GET | `/api/export/particles.vtk?geometry=cylinder` | — |
| GET | `/api/export/graph.json?geometry=cylinder` | — |

Geometries: `cylinder`, `step`, `airfoil`, `nozzle`, `cavity`, `bubble`.
Kernels: `wendland`, `cubic`, `quintic`.
Lattices: `square`, `hex`.

```bash
curl -s localhost:8080/api/mesh \
  -H 'content-type: application/json' \
  -d '{"geometry":"cylinder","params":{"h0":0.05,"grading":0.45,"hmaxRatio":3,"iters":45}}' \
  | jq '.stats'
```

## Where the time goes (and why Rust helps)

| stage | complexity | implementation |
| --- | --- | --- |
| level-set SDF sampling | O(N) | pure `f64`, inlined |
| force equilibrium | O(iters · E log E) | edge hash, rayon-ready |
| Delaunay triangulation | O(N log N) | `delaunator` (C++ port) |
| face topology | O(N) | `HashMap<(u32,u32)>`, flat arrays |
| SPH relaxation | O(n · k) | uniform grid, flat CSR-like lists |
| diagnostics / boundary surface | O(n · k) | rayon parallel map |
| OPMIS stencils | O(n · k · 9) | 3×3 inverse per target |

Rust matters for the O(n·k) parts and for meshes above ~10⁴–10⁶ cells: no boxed
per-particle objects, no GC, predictable `f64` kernels, and rayon across
particle loops. The TS engine is comfortably interactive at the demo sizes
(hundreds to a few thousand cells), so for the served demo numbers the network
hop can dominate — measure before assuming a win.

## Design notes

- **One geometry source.** `geometry::sdf` drives boundary projection, particle
  surface bounding and patch classification; no separate CAD/STL path.
- **Zero-order consistency is checked, not assumed.** `closure_residual` is
  `max_c |Σ_f S_f n_f| / L`; the mesh should reproduce it to ~1e-16 and
  `Σ A_bnd` should match the analytic perimeter.
- **Determinism.** Seeded lattice + fixed iteration counts. TS and Rust agree
  in algorithm and arithmetic order, but not necessarily bit-for-bit (libm
  `sqrt`/`acos` are IEEE, `powi` is not guaranteed identical); compare
  statistics with tolerances, not hashes.
- **Contract stability.** The JSON is a flat-array schema; switching to
  `bincode`/Arrow IPC is the next step for 10⁶-cell payloads.
- **WASM target.** Every module is free of I/O; `export.rs` only formats
  strings. `wasm-bindgen` wrappers can call `mesh::build`/`sph::run` directly
  for an in-browser Rust engine (best of both: no server, native-speed kernels).
