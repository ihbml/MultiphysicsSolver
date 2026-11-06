# FVM / SPH Mesher — native backend

Performance-first backend that converts an **SPH particle cloud into a conforming
finite-volume mesh**, solves the compressible Euler equations on the resulting cell
graph, and reports the diagnostics required by three research papers:

| Paper | What is implemented |
| --- | --- |
| Wang, Haidn & Hu — *incorporating the FVM boundary algorithm into Eulerian SPH* | Per-cell zero-order consistency deficiency `χ` from a Wendland C2 kernel with virtual wall particles, and a wall closure that extrapolates the least-squares boundary gradient across the mirror point scaled by `(1 + κχ)`. |
| Zhang & Ming — *adaptive SPH–FVM with optimised particle–mesh interpolation* | Support-normalised partition-of-unity P2M/M2P operator on a uniform support grid; conservative by construction, with sparsity / stencil / L2 / L∞ / conservation-error reporting. |
| Du et al. — *DiFVM differentiable graph FVM* | CSR cell→face adjacency, a face-contiguous flux array and a race-free gather/scatter residual kernel (OpenMP), plus goal-functional gradients `dJ/dMach`, `dJ/dκ`. |

## Layout

```
native/src/engine.hpp    shared types, Wendland kernel, API
native/src/mesh.cpp      seed generation, Voronoi half-plane clipping, Lloyd
                         relaxation, ghost-seed obstacle carving, CSR graph,
                         SPH consistency, P2M/M2P interpolation operator
native/src/solver.cpp    HLLC flux, limited reconstruction, SSP-RK2, boundary
                         closure, CFL control, forces, sensitivities, JSON
native/src/main.cpp      stdin protocol (`key=value` lines) → JSON stdout
src/lib/native.ts        native bridge: freshness check + on-demand g++ build
src/lib/params.ts        validation, presets, request → native config
src/db/schema.ts         Drizzle tables (meshes, simulations)
src/app/api/*            REST route handlers
```

The binary is compiled with `g++ -std=c++17 -O3 -fopenmp` (fallback without
OpenMP) and is rebuilt automatically when C++ sources are newer than the binary.

## Protocol

The native core is a stateless CLI: newline separated `key=value` tokens on stdin,
a single JSON document on stdout.

```
op=mesh|solve|probe|health
target=2400 jitter=0.12 relax=2 walllayer=1
obstacle=circle 0.35 0.5 0.12
ic=shock_obstacle mach=2 cfl=0.35 steps=320 order=1 snapshots=6
wallclosure=sph kappa=1 interpolation=1 sensitivity=1
```

## REST API

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/api/health` | native capabilities, OpenMP threads, PostgreSQL probe |
| GET | `/api/presets` | presets, limits, discretisation summary |
| POST | `/api/meshes` | build + persist a mesh (geometry, CSR graph, quality audit) |
| GET | `/api/meshes` / `/api/meshes/:id` | list / fetch cached meshes |
| POST | `/api/simulations` | mesh + FVM run (snapshots, forces, interpolation, dJ/dθ) |
| GET | `/api/simulations` / `/api/simulations/:id` | recent runs / full payload |
| POST | `/api/diagnostics` | uniform-state residual certification |

Requests are validated against cell-step budgets (`24M` cell-steps) and geometry
bounds before the native process is spawned. Meshes are deduplicated by a SHA-256
fingerprint of the mesh parameters.

## Measured on the sandbox (4 threads)

| cells | faces | mesh build | 40 FVM steps | throughput |
| --- | --- | --- | --- | --- |
| 1 981 | 5 999 | 19 ms | 15.5 ms | 31 M face-evals/s |
| 8 023 | 24 173 | 83 ms | 64 ms | 30 M face-evals/s |
| 32 043 | 96 338 | 312 ms | 188 ms | 41 M face-evals/s |

A uniform state produces a **machine-zero** discrete residual (validated by
`/api/diagnostics`), i.e. the boundary closure and the interior HLLC fluxes are
free-stream preserving. The Sod tube matches the analytic plateau/shock positions
at `t = 0.1` with < 1e-4 relative mass drift.
