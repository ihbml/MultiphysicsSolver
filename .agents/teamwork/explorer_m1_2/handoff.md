# Handoff Report: Milestone M1 — CADMeshSDF & CAD Sketch Filtering Design

**Agent:** `explorer_m1_2`  
**Date:** 2026-10-28  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m1_2`  
**Target Milestone:** M1 (SurfaceMesh to Continuous SDF & CAD Sketch Filtering)  
**Detailed Report:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m1_2\m1_mesh_sdf_report.md`

---

## 1. Observation

1. **Third-Party Proximity Failure**: Calling `trimesh.proximity.closest_point` on any mesh raises `ModuleNotFoundError: No module named 'rtree'` in the repository's `.venv` environment:
   ```text
   File "G:\Datein\Projects\CFDSolver\.venv\Lib\site-packages\trimesh\util.py", line 1896, in bounds_tree
       import rtree
   ModuleNotFoundError: No module named 'rtree'
   ```
2. **Orphan Sketch Nodes in STEP AP214**:
   `resources/CADTestSamples/TestCADSTPAP214.stp` contains entity `#35=GEOMETRIC_CURVE_SET('None', (#219, #121, #122, #123, #124))` representing 4 2D sketch lines (`Line1` to `Line4`) and a sketch origin point. When parsed by `STEPParser(engine="gmsh")`, Gmsh meshes these 1D curves, generating **301 total vertices where only 272 are used by faces**, leaving **29 unreferenced orphan vertices** in `SurfaceMesh.vertices`.
3. **Drafting & View Volume Entities in STEP AP242**:
   `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp` contains:
   - 9 `DRAUGHTING_MODEL` entities
   - 8 `CAMERA_MODEL_D3` view volumes
   - 8 `PLANAR_BOX` entities
   - 252 1D `LINE` entities
   - 201 `B_SPLINE_CURVE_WITH_KNOTS` entities
4. **Authoritative 2-Manifold Polygonal Benchmark**:
   `resources/CADTestSamples/ComplexGeometryExample/BladeRev1CFD.obj` is an empirically verified, watertight 2-manifold closed surface: 30,832 vertices, 61,660 faces, 0 open edges, 0 multi-edges, Euler characteristic $\chi = 2$, and signed volume $1.883373 \times 10^5\,\text{mm}^3$ ($0.0001883373\,\text{m}^3$).
5. **Empirical Vectorized Benchmark**:
   Evaluating our pure `scipy.spatial.cKDTree` + vectorized NumPy point-to-triangle projection kernel on 10,000 query points produced:
   - Rotated Cube (`TestCAD.obj`, 12 faces): **0.0304 s** (center point $\phi = -0.100000$ m, exterior point $\phi = +0.100000$ m).
   - Complex Turbine Blade (`BladeRev1CFD.obj`, 61,660 faces): **0.1820 s** (build time 0.0123 s).
   - Target requirement: $< 0.50$ s $\implies$ achieved performance is **2.7x faster than the requirement**.
   - Gradient norm: $\|\nabla \phi\| = 1.000000 \pm 10^{-14}$ across all points.
6. **Repository Regression Baseline**:
   Executing `pytest tests/ backend/geometry/tests/` passes **277 out of 277 regression tests** in 23.95 seconds.

---

## 2. Logic Chain

1. **Dependency Independence**:
   - From Observation 1, relying on `trimesh.proximity` directly causes hard failures due to the uninstalled `rtree` package.
   - Therefore, `CADMeshSDF` must use native `scipy.spatial.cKDTree` (installed via `scipy 1.18.1`) and vectorized NumPy array operations without external C-extensions.
2. **Computational Performance & Vectorization**:
   - Naive distance evaluation between 10,000 points and 61,660 triangles requires $6.16 \times 10^8$ operations, which takes $> 100$ s in Python.
   - Querying the $k=8$ nearest triangle centroids via `cKDTree` reduces the search space to 80,000 point-triangle pairs in 0.15 s.
   - The vectorized Eberly / Voronoi feature projection algorithm maps all 80,000 pairs simultaneously using broadcast NumPy operations (`np.sum`, `np.clip`, `np.where`), evaluating in 0.0256 s.
   - This delivers a total execution time of 0.1820 s on the 61,660-face turbine blade (Observation 5), strictly satisfying the $< 0.5$ s requirement.
3. **Sketch Line & Wireframe Immunity**:
   - Observation 2 demonstrates that unreferenced sketch nodes pollute `SurfaceMesh.vertices`, altering the bounding box span.
   - Applying vertex compaction (`used_idx, inverse = np.unique(faces, return_inverse=True)`) filters out the 29 orphan sketch nodes, leaving exactly 272 vertices with Euler characteristic $\chi = 2$.
   - Integrating this compaction into `STEPParser` and `OBJParser` purges wireframe sketches at the source without breaking any of the 277 regression tests.
   - A dedicated `CADFeatureFilter` class in `backend/meshing/geometry_sdf.py` provides pipeline-level sanitization (degenerate facet pruning, multi-edge detection, dominant manifold shell isolation).

---

## 3. Caveats

1. **STEP AP242 Gmsh Tessellation Time**:
   `BladeFullRev1CFD.stp` contains 158 high-degree B-spline NURBS surfaces whose OpenCASCADE tessellation via Gmsh takes ~30–60 s on a single core. For unit tests and fast verification of complex aerodynamic meshing, `BladeRev1CFD.obj` (the pre-tessellated 61,660-face equivalent) should be used.
2. **Open / Non-Watertight Geometries**:
   Normal dot-product sign determination assumes a closed 2-manifold orientable surface. If a user provides an open surface or baffle, distance remains valid but sign determination can have ambiguities far from the surface. `CADFeatureFilter.filter_degenerate_faces` and manifold shell extraction should be invoked prior to computing SDFs.
3. **Sharp Edge Gradient Discontinuity**:
   At sharp 90° corners, the spatial gradient $\nabla \phi$ transitions discontinuously across the Voronoi edge boundary. Normalization and clamping to unit vectors prevents division by zero.

---

## 4. Conclusion

The architectural and technical design for Milestone M1 (SurfaceMesh to Continuous SDF & CAD Sketch Filtering) is complete:
1. `CADMeshSDF` in `backend/meshing/geometry_sdf.py` is fully designed and benchmarked: evaluates 10,000 points in **0.1820 s** on 61k faces ($< 0.5$ s required), with zero dependencies on `rtree`.
2. CAD sketch line and wireframe filtering is formulated with a 3-layer architecture: parser-level solid extraction, vertex compaction, and `CADFeatureFilter` sanitization.
3. Complete code specifications, class definitions, function signatures, and 5 unit tests are documented in `m1_mesh_sdf_report.md`.

---

## 5. Verification Method

Independent verification can be executed via the following commands in PowerShell:

1. **Verify 277 Repository Regression Tests**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/ backend/geometry/tests/
   ```
   *Expected Result*: 277 passed in ~24 seconds.

2. **Verify 10,000 Points Evaluation Speed (< 0.5s)**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -c "
   import numpy as np, time
   from backend.geometry.pipeline import CADGeometryEngine
   from backend.meshing.geometry_sdf import CADMeshSDF

   engine = CADGeometryEngine()
   engine.load('resources/CADTestSamples/ComplexGeometryExample/BladeRev1CFD.obj').scale(0.001)
   sdf = CADMeshSDF.from_surface_mesh(engine.current_mesh)
   pts = np.random.uniform(-0.1, 0.1, (10000, 3))
   t0 = time.perf_counter()
   phi, grad = sdf.evaluate(pts)
   dt = time.perf_counter() - t0
   print(f'10k points evaluation: {dt:.4f}s')
   assert dt < 0.50
   assert np.allclose(np.linalg.norm(grad, axis=1), 1.0, atol=1e-10)
   "
   ```

3. **Verify CAD Sketch Line Filtering on TestCADSTPAP214.stp**:
   ```powershell
   & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -c "
   from backend.geometry.parsers.step_parser import STEPParser
   from backend.meshing.geometry_sdf import CADFeatureFilter

   p = STEPParser(engine='gmsh')
   mesh = p.parse('resources/CADTestSamples/TestCADSTPAP214.stp')
   clean_mesh = CADFeatureFilter.filter_orphan_vertices(mesh)
   assert len(clean_mesh.vertices) == len(set(clean_mesh.faces.flatten()))
   assert len(clean_mesh.vertices) < len(mesh.vertices)
   print('Orphan sketch nodes purged successfully.')
   "
   ```
