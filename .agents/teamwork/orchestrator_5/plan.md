# Implementation & Audit Plan: Phase 2 (WP3 to WP6) & Audit Remediation

## Executive Overview
This plan implements Phase 2 of the CFDSolver Meshing Subsystem across 5 milestones with an independent 3-agent audit committee for each Work Package (WP3, WP4, WP5, WP6) plus baseline remediation (R1) and final acceptance (P1-P6).

## Resource Directives (Binding for all Agents & Workers)
- **CPU**: Max 16 threads/cores for all runs, OpenMP parallelizations (`OMP_NUM_THREADS=16`), and parallel test runs (`pytest -n 16` max). DO NOT use all 28 cores (Ansys running in background).
- **RAM**: ~95% RAM occupied by Ansys. Work strictly memory-efficiently (flat SoA arrays, no large memory buffers, immediate memory release via RAII/`cfd_mesher_free`, moderate cell counts for test meshes to prevent OOM).

## Milestone Breakdown

### Milestone R1: Phase 1 Audit Findings Remediation & DLL Recompilation
- **Scope**:
  1. MeshErrorMap parity in Python & C++ (`omega_cell` min over incident faces [0.1, 1.0], `wsrd_alpha` median normalized, `beta_upwind` matching formula).
  2. Remove slow Python pre-loop in `compute_difvm_topology_native`; rely on native -2 return code to raise `ValueError`.
  3. Outward face orientation in `_extract_cell_faces_py` and C++ `extract_cell_faces` based on cell centroid.
  4. Boundary patch coverage validation in `UnstructuredMesh.validate()` & thread-local `prev_facet` in OpenMP `MeshSDF::evaluate`.
  5. Optional `seed_signs` filter in `voromesh_bisector_loss`.
- **Target Files**:
  - `backend/meshing/difvm_topology.py`
  - `backend/meshing/models.py`
  - `backend/meshing/geometry_sdf.py`
  - `backend/meshing/native/src/difvm_mesh.cpp`
  - `backend/meshing/native/src/sdf_engine.cpp`
- **Verification**: MSVC 2022 C++20 build `cfd_mesher_native.dll` and run all 567 baseline tests.

### Milestone WP3: Robust 2D-Mesher
- **Scope**:
  - Constrained Delaunay / DistMesh with adaptive sizing $h(\mathbf{x})$ and boundary edge subdivision.
  - Shoelace CCW canonicalization of polygons and boundaries.
  - Ear-Clipping without centroid fan; strict cell volume positivity $V_i > 0$ and analytical area conservation $\sum V_i$.
- **Audit Gate WP3**: Independent 3-Agent Committee (Paper-Fidelity Challenger, Code-Auditor, Math-Auditor) 100% Unanimous APPROVE.

### Milestone WP4: Real 3D Fluid & Multiphysics Domain Meshing & GCI Hierarchy
- **Scope**:
  - 2:1 balanced octree without hanging nodes via polyhedral transition interface (Mosaic Poly-Hexcore).
  - Multiphysics domain modes (`external_fluid`, `internal_fluid`, `solid_only`, `cht_coupled`) with cell regions, twin-face interfaces, anisotropic properties / nodal tensors.
  - Cut-cell snapping (AMIPS / VoroCrust) and small-cell merging ($V_{\text{cut}} < 0.2 V_{\text{bg}}$ with Berger-Giuliani WSRD $w_{ij}$).
  - Farfield and body patch generation.
  - Automated 3-grid GCI hierarchy ($r \in [1.3, 1.5]$, $\text{GCI} < 1\%$).
- **Audit Gate WP4**: Independent 3-Agent Committee 100% Unanimous APPROVE.

### Milestone WP5: Collision-Free 3D Boundary Layer Inflation with Grading
- **Scope**:
  - Loseille-Löhner visibility cone condition $\langle \mathbf{n}_i, \mathbf{n}_{T_j} \rangle \ge \cos(80^\circ) > 0$.
  - Multi-normals at edges and corners with $1/\cos(\theta/2)$ thickness correction.
  - Inflation grading: geometric progression $r_{\text{stretch}} \in [1.15, 1.20]$ resolving down to $y^+ \le 1$.
  - Medial-axis step-down $\delta_{i,k} \le \min(0.4 d_{\text{opp}}, 0.8 R_{\text{medial}})$ / Marcon macro-layer splitting (no crushing $h_1$).
  - Conformal stitching with core mesh.
- **Audit Gate WP5**: Independent 3-Agent Committee 100% Unanimous APPROVE.

### Milestone WP6: Inversion-Safe ALE, JamTet, UM2N, Anisotropic Metric/PUMA & OSA Multiphysics
- **Scope**:
  - Shamanskiy & Simeon TINE with logarithmic Neo-Hookean energy & Jacobian stiffening $E_{\text{eff}}(e) = E_0 (V_e^0)^{-2.5}$.
  - JamTet sphere-packing morphing & UM2N monitor.
  - Anisotropic metric: $L_q$-Hessian sensor & Fidkowski entropy-adjoint, `puma_subdivide_cells`.
  - 2D `faMesh` submesh extraction, Spalding-Newton $u^*$ and 24x24 patch-WSS (Jaiswal & Hsu 2026).
  - Open Software Architecture (OSA) Multiphysics containers (`material_properties`, `region_properties`, `constitutive_data`).
  - Validation reference: CMSX-4 single-crystal superalloy ($C_{11}=192\text{ GPa}, C_{12}=136\text{ GPa}, C_{44}=96\text{ GPa}$), von Mises and Schmid factor on $\{111\}\langle 110 \rangle$.
- **Audit Gate WP6**: Independent 3-Agent Committee 100% Unanimous APPROVE.

### Final Acceptance: Gate P1-P6 Master Acceptance
- Complete verification with `tests/test_mesher_validation_gate.py` (P1 to P6) and repository regression tests.
