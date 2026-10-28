# Milestone M4: End-to-End CAD Ingestion, Filtering, and Meshing Validation Report

**Subsystem**: `backend/meshing`  
**Reference Geometries Tested**:
1. `TestCAD.obj` & `TestCADSTPAP214.stp` (200 mm rotated cube, analytical CFD volume $V = 0.008\text{ m}^3$)
2. `BladeRev1CFD.obj` (single aerodynamic turbine blade, target volume $V \approx 187,600.6\text{ mm}^3$)
3. `BladeFullRev1CFD.stp` (full turbine rotor assembly, target volume $V \approx 5.56 \times 10^6\text{ mm}^3$)

---

## 1. Executive Summary & Verification Matrix

| Fixture | Format | Nodes | Cells | Measured Volume | Analytical / Ref Volume | Relative Error | Runtime | Physical Invariants |
|---|---|---|---|---|---|---|---|---|
| **TestCAD.obj** | Wavefront OBJ | 512 | 343 hex | $0.00800000002\text{ m}^3$ | $0.008\text{ m}^3$ | $2.99 \times 10^{-9}$ | 0.006 s | $V_i > 0$, $\max\|\sum \mathbf{S}_f\| = 1.96 \times 10^{-19}$ |
| **TestCADSTPAP214.stp** | STEP AP214 | 512 | 343 hex | $0.00800000002\text{ m}^3$ | $0.008\text{ m}^3$ | $2.99 \times 10^{-9}$ | 0.006 s | $V_i > 0$, sketch lines purged, $\chi = 2$ |
| **BladeRev1CFD.obj** | Wavefront OBJ | 9,450 | 533 poly | $187,600.57\text{ mm}^3$ | $187,600.6\text{ mm}^3$ | $< 0.0001\%$ | 0.221 s | $V_i > 0$, $\max\|\sum \mathbf{S}_f\| = 0.0$, 2-manifold |
| **BladeFullRev1CFD.stp** | STEP AP214 | 19,220 | 3,590 poly | $5,557,889.29\text{ mm}^3$ | $5,560,000.0\text{ mm}^3$ | $0.038\%$ | 0.270 s | $V_i > 0$, 9 drafting models filtered out |

---

## 2. Ingestion & Filtering Mechanics

### 2.1 Non-Face Entity & Auxiliary Curve Filtering
- In Siemens NX STEP export `TestCADSTPAP214.stp`, entity `#35=GEOMETRIC_CURVE_SET('None',(#219,#121,#122,#123,#124))` contains 4 auxiliary 2D sketch lines and 1 origin point.
- `CADFeatureFilter.sanitize(sm)` and `STEPParser` parse only 2-manifold B-Rep shells (`ADVANCED_FACE`), discarding all 1D curve sets and auxiliary drafting entities.
- Topological audit on sanitized mesh confirms: $V=8$, $F=12$, $E=18$, Euler characteristic $\chi = V - E + F = 8 - 18 + 12 = 2$, 0 open edges, 0 multi-edges.

### 2.2 Draughting Model Immunity
- `BladeFullRev1CFD.stp` contains 9 `DRAUGHTING_MODEL` entities (`#19`–`#27`: `Top`, `Front`, `Right`, `Back`, `Bottom`, `Left`, `Isometric`, `Trimetric`) with associated camera models (`CAMERA_MODEL_D3`, `VIEW_VOLUME`, `PLANAR_BOX`).
- Topological traversal strictly follows `ADVANCED_FACE -> FACE_OUTER_BOUND -> EDGE_LOOP -> EDGE_CURVE -> CARTESIAN_POINT`, naturally ignoring all viewport and drafting metadata without parsing exceptions or performance degradation.

---

## 3. Continuous Signed Distance Field (`CADMeshSDF`)

Continuous SDF evaluation was validated across all 3 CAD parts:
- For `TestCAD.obj`: Centroid $\mathbf{c} = [0, 0, 100]\text{ mm}$ evaluated to $\phi = -100.0000\text{ mm}$ (exact half-width), $\|\nabla \phi\| = 1.0000$. Exterior query point $([100, 100, 300] + 0.5 \cdot \text{span})$ evaluated to $\phi = 214.6930\text{ mm}$, $\|\nabla \phi\| = 1.0000$.
- For `BladeRev1CFD.obj` and `BladeFullRev1CFD.stp`: Continuous spatial queries reliably determine distance to geometry boundary and surface unit normals with machine precision.

---

## 4. Boundary Layer Inflation (`BoundaryLayerGenerator`)

- Prismatic boundary layer inflation was verified on physical wall patches using `MeshEngine.inflate_boundary_layers`:
  - Configuration: `num_layers = 3`, `first_layer_height = 1.0`, `growth_rate = 1.2`, `pinching = True`.
  - Layer thicknesses verified: $h_0 = 1.000$, $h_1 = 1.200$, $h_2 = 1.440$ ($h_k = h_1 \cdot r^k$, error $< 10^{-7}$).
  - Cell volumes strictly positive ($V_i = 20,000.0 > 0$).
  - Full error map generated with aspect ratio and stabilization mask fields.

---

## 5. Verification Commands

To reproduce the CAD meshing validation tests:
```powershell
G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe -m pytest tests/test_cad_meshing_validation.py -v
```
Output: **8 passed in 1.30s**.
