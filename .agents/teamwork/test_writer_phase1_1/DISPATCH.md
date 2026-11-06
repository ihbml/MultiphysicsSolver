## 2026-10-30T23:03:38Z
You are test_writer_phase1_1, a teamwork_test_writer subagent.

## Working Directory
Your working directory is:
G:\Datein\Projects\CFDSolver\.agents\teamwork\test_writer_phase1_1
Write all your agent metadata (BRIEFING.md, progress.md, handoff.md) into this directory.

## File Ownership
You exclusively own and write:
- `tests/test_wp1_topology_polymesh.py`
- `tests/test_wp2_mesh_sdf.py`
- `TEST_READY.md` at project root
Do NOT modify any files in `backend/meshing/`.

## Authoritative Reference Documents
Read these before starting:
1. `G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md`
2. `G:\Datein\Projects\CFDSolver\PROJECT.md`
3. `G:\Datein\Projects\CFDSolver\TEST_INFRA.md`
4. Survey reports:
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_1\survey_wp1.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_2\survey_wp2.md`
   - `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_phase1_survey_3\survey_baseline_verification.md`

## Mission: Author Phase 1 Test Suites
Author standard-conforming pytest test suites for Phase 1:

1. `tests/test_wp1_topology_polymesh.py`:
   - Feature coverage & boundary/edge cases for WP1:
     - `test_face_connectivity_and_right_hand_rule`: Internal face normal $S_f$ points from owner to neighbour ($owner < neighbour$), boundary faces point outward. `faces` property is present and matches `face_areas` and `face_centres`.
     - `test_l_shaped_2d_quad_at_z5`: L-shaped 2D quad mesh at $z = 5.0$. Verifies elements are classified as `CellType.QUAD` ($9$), NOT `CellType.TETRA` ($10$). Validates positive cell volumes, correct face normals, and valid Gauss-closure.
     - `test_scale_invariance_validate`: Micro-mesh ($10^{-6}\text{ m}$) and macro-mesh ($10^3\text{ m}$) pass `mesh.validate()` with relative Gauss-closure ($\text{tol}_{\text{rel}} = 10^{-11}$) and collinearity/over-relaxation projection identities.
     - `test_strict_non_manifold_rejection`: Mesh where 3 cells share the same face deterministically raises `ValueError`.
     - `test_cht_twin_faces_contiguous`: `extract_cht_twin_faces` produces clean internal faces ($0 \dots n_{\text{int}}-1$) with no `neighbour == -1` in the internal block, and appends contiguous coupled boundary patches (`coupledWall_regionA_to_B`, `coupledWall_regionB_to_A`).
     - `test_named_boundary_patches_passthrough`: Boundary patch names (`inlet`, `outlet`, `wall`, `symmetry`) survive `compute_difvm_topology`.
     - `test_rcm_and_hilbert_sfc_reordering`: `rcm_reorder` and `hilbert_sfc_reorder` preserve boundaries, baffles, cyclic pairs, and metadata; verify `inner_cells` and `halo_adjacent_cells`.
     - `test_cms_mesh_error_map_fields`: Verifies $m_f^{\text{skew}}$, $k_f^{\text{comp}}$ ($\alpha_{\max}=1.5$), $\beta_f^{\text{upwind}}$, $\omega_i^{\text{cell}}$, and $\alpha_i^{\text{wsrd}}$ in `mesh.error_map`. Inverted face ($S_f \cdot d_f \le 0$) escalates to `stabilization_mask = 3` and $\beta_f = 0.0$.
     - `test_openfoam_polymesh_export_syntax`: Exports mesh via `export_openfoam_polymesh(mesh, tmp_path)` and verifies `points`, `faces`, `owner`, `neighbour` (length matches internal faces exactly!), and `boundary` FoamFile headers and dictionary syntax.
     - `test_extrude_2d_to_3d_slab`: Extrudes 2D quad mesh into 1-cell thick 3D slab with `frontAndBack` patch (`type empty;`), valid 3D topology, and positive cell volumes.
     - `test_c_abi_exception_safety_and_bounds`: C-ABI functions return -1/-2 on invalid handles or out-of-bounds offsets instead of crashing.

2. `tests/test_wp2_mesh_sdf.py`:
   - Feature coverage & edge cases for WP2:
     - `test_triangle_aabb_tree_closest_point`: `TriangleAABBTree` on complex surface mesh, verifies exact closest point, closest triangle ID, barycentric coordinates, and speedup from `prev_facet` spatial coherence caching.
     - `test_angle_weighted_pseudonormals_no_sign_flips`: Tests anisotropic box/wedge with sharp $30^\circ$ and $90^\circ$ edges and corner diagonals. Evaluates points along normal offsets from edges and corners: verifies zero sign flips ($\operatorname{sign}(s) \equiv \operatorname{sign}((p - q) \cdot n_{\text{pseudo}})$).
     - `test_generalized_winding_numbers_dirty_cad`: Tests non-watertight CAD mesh with small gaps ($10^{-4}\text{ m}$). Verifies $W(x) \ge 0.5$ inside and $W(x) < 0.5$ outside without numerical overflow or division by zero.
     - `test_voromesh_differentiable_bisector_loss`: Computes bisector loss over $k=16$ nearest seeds according to Maruani et al. (2023). Verifies continuous distance values, unit gradient norm, and numerical stability.

3. Publish `TEST_READY.md` at project root with runner commands, test counts, and coverage summary.

## Deliverables
- `tests/test_wp1_topology_polymesh.py`
- `tests/test_wp2_mesh_sdf.py`
- `TEST_READY.md`
- `handoff.md` in your working directory.
Send message to orchestrator upon completion.


## 2026-10-30T23:11:15Z
**Context**: Test Suite Standards & Code Style
**Content**: Verbindliche Benutzer-Vorgabe zu Code-Stil und Formatierung:
Im Python-Code ist strikt der Google Python Style Guide einzuhalten:
Typannotationen für alle öffentlichen Hilfsfunktionen/Fixtures, Google-Style Docstrings mit Args/Returns/Raises, PEP8/Black-Konformität, keine Wildcard-Imports.
Das unabhängige Audit-Gremium wird dies strikt prüfen.
**Action**: Berücksichtige dies bei der Erstellung der Test-Dateien.
