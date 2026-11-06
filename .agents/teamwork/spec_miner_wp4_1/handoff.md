# Handoff Report: Specification Mining for Work Package 4 (WP4)

**From:** `spec_miner_wp4_1` (Teamwork Specification Miner)  
**To:** Orchestrator (`f399b5d5-15ff-43c9-8f07-1e5bfc115e13`)  
**Date:** 2026-11-04  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_wp4_1\`  
**Status:** Task Complete (Hard Handoff)

---

## 1. Observation

1. **Current Codebase Deficiencies in `backend/meshing/mesh_engine.py`**:
   - `mesh_engine.py:111-131`: `_is_box_geometry` hardcodes `len(verts) != 8` and classifies geometries via heuristic vertex counts:
     ```python
     if len(verts) != 8:
         return False
     ```
   - `mesh_engine.py:251-262`: Heuristic cell count limits `max_res = 100` and hardcoded stepping fractions:
     ```python
     dx = max(h0 / 3.0, span[0] / 100.0, 1e-6)
     ```
   - `mesh_engine.py:300-304`: Hardcoded threshold tuned to specific narrow bands:
     ```python
     threshold = 0.11 * h_min  # Physical level-set interface narrow-band fraction (0.39% error)
     ...
     if phi_c[idx] < threshold:
     ```
   - `mesh_engine.py` currently meshes the solid interior ($\phi < 0$) in `_mesh_complex_surface` rather than the exterior fluid domain with a farfield box.

2. **Configuration Gaps in `backend/meshing/models.py`**:
   - `MeshingConfig` (lines 121–132) lacks the explicit `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"]` required by `ORIGINAL_REQUEST.md` R1 and `SUGGESTIONS_BACKEND.md` §4.1 K4.
   - It also lacks `farfield` bounding parameters (`farfield_padding`, `farfield_bbox`), `cutcell` parameters (snapping factor, small cell ratio $0.20$, WSRD target $\alpha_{\text{target}} = 0.50$), and `gci` hierarchy configuration ($r \in [1.3, 1.5]$).

3. **Multi-Region & Twin-Faces in `backend/meshing/difvm_topology.py`**:
   - Lines 1113–1270: `extract_cht_twin_faces` is already cleanly implemented in Python, re-indexing clean internal faces into $[0, n_{\text{clean\_int}})$, original boundaries into $[n_{\text{clean\_int}}, n_{\text{clean\_int}} + n_{\text{orig\_bnd}})$, and appending `coupledWall_fluid` and `coupledWall_solid` (with $\mathbf{S}_{f, \text{solid}} = -\mathbf{S}_{f, \text{fluid}}$ and reversed node ordering).
   - In C++ (`backend/meshing/native/src/cht_cyclic.cpp:17-47`), `extractCoupledInterfaces` currently appends patch metadata but does not yet perform full face reindexing in `NativeMeshData`.

4. **Reference Formulations in `literature_survey_2026.md` & Literature**:
   - **Berger & Giuliani (2024)** (§9.4):
     $$w_{i,j} = \frac{1}{N_i - 1}\left(1 - \frac{\alpha_j}{\alpha_{\text{target}}}\right) \quad \forall j \in W_i \setminus \{i\}, \qquad w_{i,i} = 1 - \sum_{j \in W_i \setminus \{i\}} w_{i,j}$$
     for cut-cells with $0.20 \le \alpha_i < \alpha_{\text{target}} = 0.50$, guaranteeing strict conservation ($\sum w_{ij} = 1$) and smooth shutoff.
   - **Small-cell merging threshold**:
     $$\alpha_i = \frac{V_{\text{cut}, i}}{V_{\text{bg}, i}} < 0.20$$
     merged in the direction of the outward fluid normal $\nabla \phi$.
   - **ANSYS Mosaic / PUMA** (§9.2, Aliaga et al. 2026): Conformal polyhedral replacement of 2:1 balanced octree transition faces without hanging nodes (`CellType.POLYHEDRON = 42`).
   - **AMIPS Energy** (§9.3, Hu et al. 2020 / `fTetWild`):
     $$E_{\text{AMIPS}}(T) = \frac{\operatorname{tr}(\mathbf{J}^T \mathbf{J})}{(\det \mathbf{J})^{2/3}} \ge 3$$
   - **Known Reference Bug**: In `resources/reference_spickzettel/LMArena/Mesher/DiFVMAgentModeUnkonwnbutHigh/difvm_mesh/src/generate.cpp:1146-1150`, line 1146 negates `cap_S2`, causing line 1150 `if (dot(cap_S2, outward2) < 0.0)` to fail and leave individual cap triangle normals un-flipped (`cap2` bug).

5. **Operational Directives**:
   - Ansys Fluent is running concurrently on the user's host machine.
   - Strict limit: $OMP\_NUM\_THREADS \le 16$, maximum 16 pytest workers, RAM-saving mode (streaming SoA, no memory hoarding).

---

## 2. Logic Chain

1. **Defect Elimination**:
   - From Observation 1, the current `mesh_engine.py` cannot generate true 3D external fluid meshes for arbitrary CAD bodies because it relies on `len(verts) == 8` and keeps $\phi < \text{threshold}$ (the solid).
   - Therefore, implementing WP4 requires removing `_is_box_geometry` heuristics and implementing true domain subtraction $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$ ($\phi(\mathbf{x}) > 0$).

2. **Interface Definition**:
   - From Observation 2, `MeshingConfig` is the single source of truth for the frontend and Python/C++ solvers.
   - Adding `domain_mode`, `farfield`, `cutcell`, and `gci` directly to `MeshingConfig` enables deterministic dispatch across all domain types.

3. **Algorithm Coupling**:
   - Background Octree generation must strictly enforce 2:1 balancing so that face jumps are at most 2:1.
   - Mosaic / PUMA replacement replaces the single coarse face by 4 conforming sub-faces on the unrefined cell, categorizing it as `CellType.POLYHEDRON = 42` and completely eliminating hanging nodes.
   - Cut-cells must be split into two regimes:
     * Severely truncated cells ($\alpha_i < 0.20$) are topologically agglomerated into fluid neighbors along $\nabla \phi$.
     * Moderately truncated cells ($0.20 \le \alpha_i < 0.50$) receive Berger-Giuliani WSRD weights $w_{ij}$ in `mesh.error_map["wsrd_weights"]`.
   - Node snapping onto $\phi = 0$ must be guarded by AMIPS energy $E_{\text{AMIPS}} < 10^4$ and positive volume checks to guarantee $V_i > 0$.

4. **Hierarchical Verification (GCI)**:
   - For ASME V&V 20 compliance, generating 3 meshes with $r \in [1.3, 1.5]$ with preserved patch topologies allows automated computation of the apparent order $p$, Richardson extrapolation $\phi_{\text{ext}}$, and $\text{GCI}_{21} < 1\%$.

---

## 3. Caveats

1. **Boundary-Layer Inflation Interaction (WP5 Boundary)**:
   - Full prismatic boundary layer inflation with visibility cones and medial-axis step-down is the exclusive scope of **WP5**. In WP4, the Poly-Hexcore interface terminates at the snapped/cut CAD wall facets.
2. **Moving Mesh / ALE Interaction (WP6 Boundary)**:
   - Dynamic mesh morphing (`TINE`, `JamTet`, `UM2N`) and anisotropic metric adaptation are part of **WP6**. WP4 delivers the static 3D multi-domain grid generator and GCI hierarchy.
3. **Hardware Load**:
   - Because ANSYS Fluent is consuming ~95% RAM and heavy CPU on the user system, tests in `tests/test_wp4_multiphysics_hexcore.py` must use moderate background grid resolutions (e.g. $h_0 = 0.05$ to $0.02$) to maintain $\le 16$ threads and $\le 2$ GB RAM overhead.

---

## 4. Conclusion

The specification mining for Work Package 4 is complete and fully documented in `G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_wp4_1\analysis.md`. All mathematical equations, algorithmic steps, data structures, and edge cases have been mined directly from authoritative project sources and reference literature.

The path forward for the implementation team is clear:
1. Extend `MeshingConfig` in `models.py`.
2. Implement 2:1 balanced Octree and Mosaic Poly-Hexcore transition interface.
3. Implement AMIPS-guarded snapping and Berger-Giuliani small-cell merging + WSRD.
4. Implement automated Farfield generator and 3-grid GCI hierarchy.
5. Create comprehensive test suite `tests/test_wp4_multiphysics_hexcore.py`.
6. Submit to the independent 3-agent audit committee for Gate WP4 signoff.

---

## 5. Verification Method

To independently verify the findings in this report:

1. **Inspect Analysis Report**:
   - Check `G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_wp4_1\analysis.md` for complete feature and edge case tables and mathematical derivations.

2. **Verify Equations & Reference Citations**:
   - Check Berger & Giuliani (2024) Eq. 4.2 in `resources/papers/literature_survey_2026.md:273-283`.
   - Check Mosaic / PUMA polyhedral subdivision in `literature_survey_2026.md:232-242`.
   - Check AMIPS energy in `literature_survey_2026.md:248-251`.
   - Check existing CHT twin-face extraction in `backend/meshing/difvm_topology.py:1113-1270`.
   - Check existing validation invariants in `backend/meshing/models.py:349-495`.

3. **Verify Baseline Test Suite**:
   ```powershell
   pytest tests/test_wp1_topology_polymesh.py tests/test_wp2_mesh_sdf.py tests/test_wp3_robust_2d_mesher.py
   ```
   Ensures all existing tests pass without regressions before WP4 development starts.
