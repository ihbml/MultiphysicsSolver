# Original User Request

## Initial Request — 2026-11-06T20:54:00Z

Resolve 6 critical mathematical and paper-fidelity audit defects and 4 static audit issues across the C++ and Python meshing backend, recompile the native DLL, commit the changes, build the automated checkMesh and simpleFoam acceptance validation pipeline in WSL, and pass the independent 3-agent audit committee with unanimous 3:0 approval under specified hardware and architecture guidelines.

Working directory: g:\Datein\Projects\CFDSolver
Integrity mode: development

## Reference Materials
- Literature Survey & Verification Papers: `resources/papers/literature_survey_2026.md` and `resources/papers/`
- Reference Implementations: `resources/reference_spickzettel/LMArena/`
- Benchmark CAD Geometries: `resources/CADTestSamples/` (including `TestCAD.stp`, `BladeFullRev1CFD.stp`) and OpenFOAM tutorial CAD cases in `OpenFOAM_base/openfoam/` (e.g., `snappyMultiRegionHeater`, `motorBike`)

## Requirements

### R1. Cut-Cell Snapping & AMIPS Metric Fix in Octree Hexcore
In `backend/meshing/native/src/octree_hexcore.cpp:645-648`, eliminate the collinear tetrahedron in `computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center)`. For each corner vertex `v` in adjacent octant `onode`, determine its corner index `c_idx in 0..7` and construct a positively oriented corner tetrahedron from `test_pos` and the 3 adjacent octant edge neighbor nodes, ensuring `det_j = dx * dy * dz > 0` and finite AMIPS energy. Verify that all cell sub-pyramids sharing vertex `v` retain positive volume after snapping. Add a test in `tests/test_wp4_multiphysics_hexcore.py` calling `generate_hexcore_native` on a sphere verifying wall nodes snap to `|phi| < 1e-3`.

### R2. Translation-Invariant Polygon Centroids, Face Normals, and Cell Volumes
In `backend/meshing/native/src/octree_hexcore.cpp:667-708`:
1. In Step 9, compute `mf.normal_owner` and `mf.centre` via triangle-fan integration (area-weighted sub-triangle centroids as in `computePolygonNormalAndCentroid` in `difvm_mesh.cpp:36-78`) to accurately handle non-planar snapped quad faces. Flag inverted sub-triangles (`mf.is_folded`) if oriented opposite to the face normal.
2. In Step 10, compute for each cell `c` a local reference point `c_approx` (mean of `mf.centre` of all faces of cell `c`) and integrate pyramid volumes and centroids as in `difvm_mesh.cpp:444-456`:
   `pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0`
   `pyr_c = mf.centre * 0.75 + c_approx * 0.25`
   `cell_vols[c] += pyr_vol`
   `cell_cents[c] += pyr_c * pyr_vol`
   and normalize at the end by `cell_vols[c]`.

### R3. Small-Cell Agglomeration, Cell Weighting, and WSRD Weights
1. In `octree_hexcore.cpp:742-847`, when merging a small cell `c` into `best_nb`, union all its vertices into `cell_nodes` of the target cell `best_nb` and set `cell_types[new_c] = POLYHEDRON (42)`. Merge and deduplicate internal faces sharing the same `(owner, neighbour)` pair and maintain `owner < neighbour`.
2. In `octree_hexcore.cpp:861, 1020`, compute `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c])` across all adjacent faces of cell `c` as in `difvm_mesh.cpp:666-677`.
3. In `mesh_engine.py:750-768` and `difvm_topology.py:956-972`, compute `wsrd_weights` according to Berger & Giuliani (2024) Eq. (4.2) for `active_cfg.small_cell_ratio <= alpha_i < active_cfg.wsrd_target_alpha` using dynamic configuration attributes instead of hardcoded `0.20` and `0.50`, storing in `mesh.error_map["wsrd_weights"]` and `mesh.metadata["wsrd_weights"]`.

### R4. Solid-Only Domain Mode Dispatch Fix
In `backend/meshing/mesh_engine.py:126-130`, correct the `is_solid_only` dispatch branch so that `is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")`. When `config is not None` and `active_cfg.domain_mode != "solid_only"`, call `self.mesh_hexcore(clean_mesh, scale=1.0, h0=effective_h0, config=active_cfg)`.

### R5. DistMesh Boundary Projection & Persson & Strang (2004) Consistency
1. In `backend/meshing/distmesh_core.py:734`, change point filtering to `p = p[d < -0.05 * h_vals]`.
2. In `distmesh_core.py:827-839`, execute up to 3 Newton projection steps for points with `d_nodes > 0.0`. In `backend/meshing/native/src/distmesh.cpp:734-739`, when an interior node is pushed outside (`!pointInPoly(new_pos, sub_poly)`), project `new_pos` orthogonally back onto the nearest polygon boundary segment.
3. In `distmesh.cpp:239-388`, filter generated cells against surface triangles using `MeshSDF` (`phi < 0` at cell centroid) or delegate to `OctreeHexcore` (`domain_mode = 2`).

### R6. GCI Dimensional Scaling, SDF Tolerances, and Fast Winding Number
1. In `backend/meshing/gci.py:113-114`, compute representative grid size with exponent `1.0 / max(1, mesh.topological_dim)`.
2. In `gci.py:148-156`, clamp `p` to `[0.5, 10.0]` only when outside range or when oscillatory convergence occurs (`eps_32 * eps_21 <= 0`), maintaining consistent exponent in `phi_extrapolated` and `gci_fine`.
3. In `backend/meshing/native/src/sdf_engine.cpp:479-484`, apply `1e-5 * diag` tolerance on outside bounding box check.
4. In `geometry_sdf.py:989-1049` and `sdf_engine.cpp:503-540`, accelerate `winding_number` evaluation via AABB Barnes-Hut far-field approximation for nodes where `dist > 2.0 * node_radius`.

### R7. Static Audit Detail Fixes, Boundary Types, and Strict Native Mode
1. In `difvm_topology.py:778-784`, fix attribute names: use `config.sizing.alpha` / `config.sizing.beta` and `config.inflation.pinching` / `config.inflation.pinching_threshold_angle`.
2. In `MeshEngine` (`mesh_engine.py:465`), introduce `strict_native: bool = False` and support `CFD_STRICT_NATIVE=1` environment variable so `mesh_hexcore` never falls back silently to Python when strict native is enabled.
3. In `polymesh_io.py:150-163`, support configurable `patch_types` exporting `top`, `sides`, and `bottom` (without ground plane) as `symmetry` or `patch` instead of `wall`.
4. In `inflation.py:104`, replace global `nominal_normal` flip with consistent outward normal orientation per wall face, and reverse node ordering `f_arr[::-1]` when normal is flipped.

### R8. Benchmark CAD Testing and OpenFOAM Tutorial Validation
Execute mesh generation and validation against real benchmark CAD models:
1. Samples in `resources/CADTestSamples/`, including `TestCAD.stp` and `BladeFullRev1CFD.stp`.
2. Tutorial CAD geometries in `OpenFOAM_base/openfoam/` tutorials (e.g., `snappyMultiRegionHeater` and `motorBike`).

### R9. Literature Survey Alignment and Reference Code Quality
1. Verify and align all mathematical formulas with papers catalogued in `resources/papers/` and `resources/papers/literature_survey_2026.md`.
2. For algorithms, code patterns, or ideas referenced from `resources/reference_spickzettel/LMArena/`, actively verify and patch known defects before adoption, adapting all implementations cleanly into the CFDSolver architecture without unvetted 1:1 copies.

### R10. Automated checkMesh and simpleFoam Acceptance Pipeline in WSL
Build an automated non-interactive acceptance pipeline running in WSL that exports the generated mesh via `export_polyMesh` and executes:
1. OpenFOAM `checkMesh` verifying face non-orthogonality, skewness, aspect ratios, and zero negative volume faces.
2. `simpleFoam` comparison against `snappyHexMesh` baseline reference cases.

### R11. Architecture, Code Quality, HPC Standards, and Fail-Fast Safety
- **Google C++ Style Guide:** Native C++ code must follow Google C++ Style guidelines using modern C++20, clean RAII patterns, explicit error handling, and no naked macros.
- **Fail-Fast Error Handling & Safety:** Explicitly throw typed C++ and Python exceptions before any potential crash, out-of-bounds access, or memory corruption. Never segfault, leak raw pointers, or silently corrupt data.
- **Open Software Architecture (OSA) Compliance:** Maintain generic metadata and tensor container interfaces for multiphysics coupling (CFD, structural FEM, CHT twin-faces, anisotropic stiffness tensors).
- **High Performance Core:** Performance-critical routines in C++20 / Rust / Modern Fortran must follow Structure-of-Arrays (SoA) layout, zero-copy pointer passing, high cache locality, memory efficiency, and SIMD/AVX2 friendliness.

### R12. Independent 3-Agent Audit Committee Gate Certification
Before completion, code and results must be submitted to an independent 3-agent audit committee:
1. **Agent 1 (Paper-Fidelity & Zero-Shortcut Challenger):** Audits exact formula fidelity against papers, enforces zero shortcuts, verifies absence of heuristics, magic numbers, or stub implementations.
2. **Agent 2 (Code & Software-Architecture Auditor):** Audits code cleanliness, C++20/Python style guidelines, thread and memory safety, and OSA compliance.
3. **Agent 3 (Mathematics & Numerics Auditor):** Audits numerical invariants, geometric conservatism, convergence, stability, and mesh topology correctness.
Final acceptance requires 100% unanimous approval (3:0 APPROVE) across all 3 auditors.

### R13. Operational Hardware and Concurrency Constraints
Protect host stability with Ansys Fluent active in background (~95% RAM):
- Set thread limits to 8 to 10 threads before executions: `OMP_NUM_THREADS=8` (or 10), `MKL_NUM_THREADS=8` (or 10), `OPENBLAS_NUM_THREADS=8` (or 10).
- Run CMake builds with `--parallel 8` (or 10), preferring core allocation/pinning to E-cores or idle P-cores.
- Run `pytest` strictly sequentially on individual test files with small memory footprints (no `pytest -n auto`, no xdist).
- Restrict initial test mesh sizes to <= 30,000 cells.

## Acceptance Criteria

### Mathematical & Meshing Core Validation
- [ ] `computeTetAMIPS` evaluates on non-collinear corner tetrahedra with `det_j > 0` and finite AMIPS energy.
- [ ] Test in `tests/test_wp4_multiphysics_hexcore.py` proves wall nodes snap to `|phi| < 1e-3` on a sphere surface using `generate_hexcore_native`.
- [ ] In `octree_hexcore.cpp`, unit cube `[0,1]^3` computes centroid `(0.5, 0.5, 0.5)` and shifted cube `[10,11]^3` computes centroid `(10.5, 10.5, 10.5)`.
- [ ] Agglomerated small cells in native hexcore have `cell_types = 42 (POLYHEDRON)`, unioned vertex sets, and sorted internal faces with `owner < neighbour`.
- [ ] `mesh.omega_cell` is populated with `std::max(0.1, min_beta_per_cell)` across adjacent faces.
- [ ] `MeshEngine.mesh_hexcore` and `difvm_topology.py` compute `wsrd_weights` using dynamic `config.small_cell_ratio` and `config.wsrd_target_alpha`.
- [ ] `is_solid_only` in `mesh_engine.py` evaluates to False when `domain_mode != "solid_only"`, dispatching to `mesh_hexcore`.
- [ ] DistMesh point filtering strictly uses `-0.05 * h_vals`, applies Newton boundary projection, and projects pushed-out points orthogonally to boundary segments.
- [ ] `compute_grid_representative_size` in `gci.py` scales with `1.0 / max(1, mesh.topological_dim)`.
- [ ] In `difvm_topology.py`, attribute lookups use `config.sizing.alpha`/`beta` and `config.inflation.pinching`/`pinching_threshold_angle` without AttributeError.
- [ ] Boundary faces in `polymesh_io.py` export with configurable patch types (`symmetry` / `patch` for top/sides/bottom).
- [ ] `inflation.py` maintains consistent outward normals without inverting opposing faces on closed bodies.

### CAD Benchmark & Reference Alignment
- [ ] Meshing succeeds on CAD models from `resources/CADTestSamples/` (`TestCAD.stp`, `BladeFullRev1CFD.stp`).
- [ ] Meshing succeeds on CAD samples from `OpenFOAM_base/openfoam/` tutorials (`snappyMultiRegionHeater`, `motorBike`).
- [ ] Formulas implemented match the reference publications in `resources/papers/` and `literature_survey_2026.md`.
- [ ] Snippets referenced from `resources/reference_spickzettel/LMArena/` are verified, defect-patched, and adapted to CFDSolver architecture.

### Compilation, Strict Mode & Safety
- [ ] MSVC 2022 C++20 Release DLL (`cfd_mesher_native.dll`) compiles cleanly via CMake with `--parallel 8` to `10`.
- [ ] With `CFD_STRICT_NATIVE=1`, native execution runs without silent Python fallback.
- [ ] Out-of-bounds inputs and invalid geometry states raise explicit C++ / Python exceptions rather than crashing or segfaulting.
- [ ] All 615 existing tests pass under sequential pytest execution with 8–10 threads.

### WSL OpenFOAM Acceptance Pipeline
- [ ] Automated script exports mesh to OpenFOAM `polyMesh` and runs `checkMesh` in WSL without fatal mesh errors.
- [ ] Test execution runs `simpleFoam` in WSL and compares mesh diagnostics against `snappyHexMesh`.

### Audit Committee Gate Certification & Git Commit
- [ ] Independent 3-agent audit committee (Paper-Fidelity Challenger, Code & Architecture Auditor, Numerics Auditor) achieves unanimous 3:0 APPROVE certification.
- [ ] Completed fixes, new tests, and pipeline scripts are committed to git.
