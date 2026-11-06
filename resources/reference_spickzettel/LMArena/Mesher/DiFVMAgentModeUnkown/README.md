# DiFVM-style graph-FVM mesher (prototype)

A standalone C++20 cut-cell **mesher only**. It creates a static, owner/neighbour
finite-volume graph and precomputes geometry and mesh-quality fields. It is not
an implementation of the DiFVM solver, a CFD solver, or an official component
of the attached paper. The paper describes the graph-FVM data representation and
over-relaxed correction; the meshing algorithm and quality heuristics here are
independent choices, not claims made by that paper.

## Important scope and safety limitations

* This is **not the requested adaptive octree mesher**. It uses a uniform
  Cartesian grid, subdivides each cube into six tetrahedra, then clips each
  tetrahedron to the fluid side. Adaptive octree refinement and 2:1 balancing
  remain to be implemented. Uniform refinement has O(N³) time and memory costs;
  the CLI's upper bound is not a guarantee that the mesh fits in memory.
* It produces tetrahedra and clipped convex polyhedra with triangular faces,
  **not one merged polyhedral cell per cut Cartesian cube**. It does not merge
  slivers, guarantee suitable CFD mesh quality, implement curved boundaries,
  or offer adaptive feature resolution. Small features can disappear between
  grid samples. Do not treat the output as production-validated CFD input.
* Only the **exterior** of a sphere or STL is meshed inside a rectangular
  domain. STL should be a closed watertight non-self-intersecting 2-manifold.
  This is not verified. Ray-parity tests can be ambiguous at triangle edges or
  vertices; pathological STL geometry may classify incorrectly. SDF samples
  near zero are perturbed to avoid exact cut-node degeneracies.
* No OpenFOAM `polyMesh` exporter, adaptive refinement, Morton ordering,
  multi-threading, GPU kernel, or solver is included. CPU performance is not
  benchmarked or claimed to beat other languages. The implementation uses a
  BVH for STL queries and 64-byte aligned SoA output arrays, but prioritizes
  a verifiable baseline over production scalability. SoA is aligned in C++
  memory; raw file loads are not guaranteed to have 64-byte alignment.
* The beta and omega rules are explicit **heuristics**, not values mandated by
  the DiFVM paper. They do not themselves change any solver matrix or flux.
  Verify mesh and numerical stability independently before any serious use.

## Build and run

Requirements: a C++20 compiler, `make`, and a POSIX host with `posix_memalign`.
No C++ third-party dependencies are required.

```sh
make
./mesher --n 12 --radius 0.5 --out output/sphere
./mesher --n 24 --stl path/to/watertight.stl \
  --bounds -1 -1 -1 1 1 1 --out output/stl
./mesher --help
```

`--n` is the number of Cartesian cubes **per axis**. Default domain is
`[-1,1]^3`; default sphere radius is 0.5 at `(0,0,0)`. The six bounds values
are xmin ymin zmin xmax ymax zmax. STL coordinates and the domain must be in
the same units. Use `--no-vtk` to skip VTK. Output files in an existing output
directory are overwritten; use a clean directory to avoid stale files. The CLI
accepts N=1..256 but high N may exhaust RAM. Exact geometry crossings are
perturbed by up to 1e-5 times the smallest grid spacing for robustness.

For a localized STL, a coarse grid can miss it completely. Visualize the mesh
in ParaView (`mesh.vtk`) and run a grid refinement study. A warning-free run is
not proof of correctness or good mesh quality.

## Test

The tests use Python 3 and NumPy; neither is needed to compile or run the C++
executable. Run `make test`. Checks include volume conservation (discrete
divergence theorem), face closure and orientation, graph index ranges, array
sizes and value bounds, over-relaxed decomposition, sphere volume convergence,
ASCII and binary STL, and invalid argument handling. These are limited tests,
not a proof of correctness for arbitrary inputs.

## Output format: `difvm-mesher-soa-v1`

`manifest.json` contains counts, types, endian order, and patch codes. Each
named field is a standalone contiguous, headerless binary array, little-endian.
Cell fields have `cells` entries, face fields `faces` entries. Indices are
zero-based; `neighbour == -1` denotes a boundary. Faces are not guaranteed to
be grouped by patch or with internal faces first. Boundary patches are grouped
only by code (1 = outer box, 2 = solid wall); prescribe actual boundary
conditions separately. Index and patch fields are signed `int64` and `int32`
respectively; all other fields are IEEE `float64`.

| Scope | Files (each ends in `.bin`) | Meaning |
|---|---|---|
| face | owner, neighbour, patch | graph connectivity and patch IDs; patch 0 is internal |
| face | sx, sy, sz | owner-outward area vector **S** |
| face | fx, fy, fz | face centroid |
| face | dx, dy, dz | over-relaxed vector **Delta** |
| face | kx, ky, kz | non-orthogonal vector **k = S - Delta** |
| face | angle | non-orthogonality in degrees |
| face | skx, sky, skz | skew vector from center-line/face-plane intersection to face centroid |
| face | beta | upwind blending fraction, 0 = no upwind, 1 = pure upwind |
| cell | cx, cy, cz | centroid |
| cell | volume | positive polyhedron volume |
| cell | fraction | cell volume divided by its original tetrahedron volume |
| cell | omega | diagonal-shield recommendation in (0,1] |

For internal faces, `d = x_neighbour - x_owner`. For boundaries, the
convention used is `d = x_face - x_owner`; there is no explicit ghost cell.

```
Delta = d * dot(S,S) / dot(S,d)
k = S - Delta
angle = acos(dot(S,d)/(|S||d|)) in degrees
skew = x_face - (x_owner + d * dot(S,x_face-x_owner)/dot(S,d))
beta = 0.1                                         if angle <= 65 degrees
     = 0.1 + 0.9 * tanh((angle-65)/10)             otherwise
omega = 1 / (1 + max(0, max_cell_angle-45)/45
               + 4*max(0, 0.1-fraction)/0.1)
```

Larger beta means **more upwind**, so the high-order portion is damped. A
solver may use `(1-beta)*high_order + beta*upwind`. For a positive diagonal,
`a_P / omega` strengthens it; this mesher merely exports omega, and does not
modify any equations. Neither heuristic is a physical model or a guarantee of
boundedness/stability. Since all faces are triangles, face coordinates and
area vectors refer to each triangle, not an aggregated polygon face.

Example reading arrays:

```python
import json
import numpy as np
from pathlib import Path
p = Path('output/sphere')
meta = json.loads((p / 'manifest.json').read_text())
owner = np.fromfile(p / 'owner.bin', dtype='<i8')
neighbour = np.fromfile(p / 'neighbour.bin', dtype='<i8')
volume = np.fromfile(p / 'volume.bin', dtype='<f8')
sx = np.fromfile(p / 'sx.bin', dtype='<f8')
assert owner.size == meta['faces'] and volume.size == meta['cells']
```

## Why C++20?

C++ provides native performance, explicit allocation/alignment and good support
for the irregular octree/graph/BVH data structures needed by a future adaptive
mesher. Rust and Fortran can also be fast; the best choice depends on the
implementation and benchmarks, not the language alone. This implementation has
not been benchmarked against them.
