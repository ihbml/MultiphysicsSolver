# HexaLab

A React/TypeScript mesh inspector backed by a real C++20 meshing engine. The browser rebuilds and verifies the same implementation as the native executable in a dedicated WebAssembly worker. Initial views use the native engine's generated coordinates and connectivity, not a stock model or fabricated metrics.

## Run the workspace

```sh
npm install
npm run dev
```

`npm run build` creates a static deployment in `dist/`. Serve this directory over HTTP, including `engine/`, `fonts/`, and `meshes/`. All computational work stays local. Browser features include orbit, pan, zoom, cell picking, true edge/plane sections, element visibility, wireframe and scalar coloring, adjustable extrusion parameters, independent verification, wall extraction, and VTK/JSON exports.

## Compile the engine

```sh
g++ -std=c++20 -O2 octree_hexcore.cpp -o hexalab
./hexalab --self-test
./hexalab --geometry l-channel --out mesh.json
./hexalab --geometry twin-plate --layers 6 --first-height 0.18 --growth 1.22 \
  --out gap.json --fa-out walls.json --patch -1
```

Lengths are in millimeters. Supported patch indices are `-1` (all), `0` (floor), `1` (ceiling), `2` (front), `3` (outward +X), `4` (back), and `5` (outward -X). The higher ceiling and lower terrace share the ceiling patch. Invalid parameters or a failed acceptance check produce a nonzero exit status. `--help` describes the available parameters.

## Implemented methods

- Angle-weighted pseudonormals, visibility-cone projection, and visibility-constrained Laplacian smoothing. A compatible seed is not assumed at reentrant trihedral corners.
- Opposing-wall clearance from five narrow-cone rays through a median-split triangle BVH, excluding incident triangles.
- Graded prism extrusion with shared vertex rings. Total local height is limited by 40% of estimated clearance and a local grid-size cap. Problem vertices are retracted together, with complete regeneration and rejection after bounded retries. The implementation uses squeezing, not arbitrary independent layer deletion.
- A full-domain wedge determinant bound: the determinant is affine in the triangular coordinates and quadratic in the extrusion coordinate. Its minimum is found at the three triangle vertices, checking both height endpoints and every internal quadratic minimum. The gate is a **dimensionless normalized bound** of 0.15. A raw dimensional determinant threshold of 0.15 would change meaning under a unit conversion.
- The actual 23 canonical maximum-scaled-Jacobian super-element tables, expanded by cube symmetries to all 256 corner masks. Canonical cells are oriented once on the reference cube; negative generated cells are never repaired by taking an absolute volume.
- Asymmetric residual voting over 125 integration points, with the requested missing-count-squared plus excess-count cost. Shared precursor corners receive consistent retain/remove votes. The aligned fixtures require only the full mask for occupied macro cells.
- Top-down assignment to analytic corner, edge, surface, and volume entities, followed by a bounded transition-band relaxation. The interior Cartesian core stays fixed. A local apex fan stitches triangular prism-front faces to quadrilateral core faces with tetrahedra and pyramids.
- Per-corner element-normalized scaled Jacobians for Hex8, Prism6, Tet4, and Pyr5. The optimal scales are 1, sqrt(3)/2, sqrt(2)/2, and sqrt(2)/2, respectively. All four apex bases are tested for pyramids. A fictitious hex extruded from the quad base checks its projected convexity, including bow-tie and concave-base regressions.
- Finite-area extraction with face areas, oriented tangent-plane edge conormals, unfolded adjacent-face geodesic distances, edge-dihedral curvature tensors, and direct zero-copy owner-field indexing. Boundary-edge distances are zero sentinels because no adjacent face exists.

## Verification scope

This is a **verified analytic-fixture engine**, not a universal industrial CAD mesher. The 80 x 32 x 40 mm stepped L-channel and 80 x 32 x 5 mm twin-plate gap are aligned to their precursor grids. The L-channel uses two Cartesian refinement levels, coarsening 76 complete sibling octants in the interior. Coarse/fine faces are partitioned into four matching quads; shared edge midpoints propagate onto adjacent coarse faces via canonical face-center fans. The thin gap remains uniform. Arbitrary-depth octree management and general BREP projection are not implemented. Stromberg's cut-mask templates are not, by themselves, hanging-node templates for adaptive octrees. The prism/core interface uses an explicit conformal fan, not an unsupported claim that the published templates solve every boundary-layer interface.

Every accepted fixture has positive signed cell volumes and JENS > 0.25. Shared face rings are checked for exactly opposite orientation. Every singly owned face must be an expected exterior triangle; accidental open internal interfaces, hanging-face subdivisions, and nonmanifold faces fail acceptance. A spatial-bin audit checks every geometric edge for embedded hanging vertices. The outer triangulation is checked for closed edge incidence and connected vertex links. Every canonical cell-center/face-triangle simplex must be positive; their sum agrees with the known analytic domain volume. These are numerical checks of the emitted piecewise-linear complex, not exact-arithmetic proofs for arbitrary geometry or high-order elements.

The discrete area vector is accumulated from oriented face triangles. Canonical smallest-ID diagonals ensure both owners integrate the same warped quadrilateral. Signed volume is a translation-invariant sum of oriented tetrahedra relative to each cell center, never an absolute-value repair. All cells must satisfy `norm(sum(Sf)) / V^(2/3) < 1e-14` in binary64. A positive corner Jacobian or a handful of clearance rays is **not** treated as a universal collision certificate.

A selected open patch is a manifold **with boundary**, not a watertight closed surface. The complete exterior is closed. Each exterior prism owns exactly one outer triangle, so the face/owner map is bijective onto the selected owners for these fixtures. That property is tested and is not presumed for arbitrary polyhedra.

## Measured native results

| Fixture | Cells | Minimum signed volume (mm^3) | Minimum JENS | Maximum closure | Negative volumes | Unmatched interior faces |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stepped L-channel | 59,694 | 0.0839904121 | 0.2506287000 | 3.6235538e-15 | 0 | 0 |
| Twin-plate gap | 32,504 | 0.0347527797 | 0.2731967864 | 3.8754667e-15 | 0 | 0 |

These values use 6 requested layers, first height 0.18 mm and growth 1.22 with GCC 12.2 on x86-64. Floating-point residuals and timings may differ across compilers. Mesh exports include full precision coordinates; all dashboard results are read from the engine output.

An independent audit, `python3 tests/verify_mesh.py`, recomputes all signed simplex volumes, closure residuals, oriented face pairs, closed exterior incidence, and owner uniqueness directly from the exported coordinates. `node tests/workspace.mjs` exercises both geometries, rebuilding, picking, clipping, exports, wall operators, and responsive layouts against a running dev server. Set `BASE_URL` to test a production deployment instead.

`python3 tests/certify_dyadic.py` additionally certifies both shipped meshes with **exact arbitrary-precision integer predicates**. Every exported binary64 coordinate is represented by its exact dyadic rational. All 676,968 cell-fan simplices have strictly positive signed volume, all 92,198 cells satisfy JENS > 1/4, and all 55,488 prism maps satisfy the continuous normalized determinant bound of 3/20. Every exact area-vector sum is zero. Exact summed volumes are 62,464 and 12,800 mm^3. The derivations are in `engine/VERIFICATION.md`; `public/meshes/certificates.json` binds the results to the SHA-256 digest of each exported file. Interactive rebuilds use the native floating-point acceptance gates, not the offline integer checker.

## Reference data

`engine/superelements.hpp` reproduces the numerical data in Tables B.1 and C.2 of Henrik Stromberg, *Combinatorial Methods in Grid Based Meshing* (2023), distributed in the [authors' MIT-licensed repository](https://github.com/HenrikJStromberg/combinatorial_meshing), commit `461df5a393736418c94542ae1c24b86a5bf80454`. The repository's short TeX table enumerates only masks 0 through 22, not all canonical symmetry classes. The complete canonical cases in the dissertation appendix were used instead. The original license is retained in `engine/reference/LICENSE`.

See also Stromberg, Mayer-Eichberger, and Lohrengel, [Combinatorial Methods in Grid based Meshing](https://arxiv.org/abs/2307.08825). The extrusion implementation is a fixture-oriented implementation of the requested constraints, not a reproduction of a full published medial-object solver.

## Source layout

- `octree_hexcore.cpp`: geometry assembly, extrusion, acceptance checks, CLI and WASM entry points.
- `engine/geometry.hpp`: geometric integration, mixed quality metrics, wedge bounds, and BVH.
- `engine/templates.hpp`, `engine/superelements.hpp`: reference tables, symmetry expansion, and voting.
- `engine/fa_mesh.hpp`: two-dimensional finite-area operators and owner mapping.
- `src/mesh.ts`, `src/MeshViewport.tsx`: actual cell/plane intersection and interactive 3D projection.
- `src/Diagnostics.tsx`: measured statistics, cell inspection, finite-area extraction, and export.
- `public/engine/worker.js`: dedicated-worker bridge to the compiled C++ module.

## Rebuild the browser module

With Emscripten 3.1.74 or newer on PATH, run `sh scripts/build-engine.sh`. The precompiled WASM module is included, so ordinary frontend development does not require Emscripten.