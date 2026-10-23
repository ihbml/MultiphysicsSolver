# Adversarial Challenge Report: Geometry Audit & OpenFOAM surfaceCheck Compliance

**Reviewer**: Challenger 1 (Geometry Audit & OpenFOAM surfaceCheck)  
**Target Module**: `backend/geometry/audit/surface_auditor.py`, `backend/geometry/audit/quality_metrics.py`  
**Authoritative Contracts**: `PROJECT.md`, `TEST_INFRA.md`, OpenFOAM `surfaceCheck.C`  
**Test Suite**: `tests/test_adversarial_audit.py` (30 independent empirical stress tests)  
**Execution Status**: 30 / 30 Passed  
**Overall Risk Assessment**: **CRITICAL**

---

## 1. Executive Summary & Verdict

Based on rigorous adversarial stress-testing with synthetic meshes probing numerical boundaries, degenerate topologies, and edge-case geometries, the current implementation of the Surface Auditor (`surface_auditor.py`) and Quality Metrics (`quality_metrics.py`) exhibits **two critical algorithmic defects**, **three high-severity specification deviations**, and **two medium-severity topological limitations**.

### Formal Verdict: **REJECT**
The implementation cannot be released to production until the critical numerical division-by-zero bug in the Moeller intersection test and the triangle quality bypass in `passed_openfoam_surfacecheck` are corrected.

---

## 2. Challenge Inventory & Empirical Proof

### [Critical] Challenge 1: Moeller Algorithm 0/0 NaN Division Silently Misses Piercing Intersections
- **Location**: `backend/geometry/audit/surface_auditor.py`, lines 335–351 (`_triangles_intersect_moeller`)
- **Assumption Challenged**: Assumed that the scalar projection parameter $t_a = -dists[i_0] / (dists[i_1] - dists[i_0])$ always has a non-zero denominator when faces are non-coplanar.
- **Attack Scenario**: Construct two orthogonal intersecting triangles where an edge of Triangle 1 lies directly in the plane of Triangle 2 (e.g. Triangle 1 on $Z=0$ with edge along the X-axis, Triangle 2 on $Y=0$ piercing Triangle 1 through $X \in [-0.25, 0.25]$).
- **Observed Behavior**:
  - $dists$ evaluated to `[0.0, 0.0, -1.0]`.
  - In `get_interval`: `dists[0] * dists[1] == 0`, falling into the `else` branch with `i0=0, i1=1`.
  - Expression `dists[i1] - dists[i0]` equals `0.0 - 0.0 = 0.0`.
  - Division `-0.0 / 0.0` yields `NaN` and triggers `RuntimeWarning: invalid value encountered in scalar divide`.
  - All subsequent interval comparisons with `NaN` evaluate to `False`.
  - `_triangles_intersect_moeller` returns `False`, completely missing a true physical intersection!
- **Empirical Test**: `test_defect_moeller_division_by_zero_and_missed_intersection_when_edge_in_plane` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: Any CAD model with axis-aligned facets, stepped bodies, or T-junctions where an edge lies in an adjacent plane will silently miss self-intersections, allowing corrupt geometries into OpenFOAM `snappyHexMesh` and causing mesher divergence.
- **Mitigation**:
  1. Detect when two vertices of a triangle lie on the intersection line ($dists[i] == 0$ and $dists[j] == 0$). In this case, the interval on the intersection line is directly given by the projections of those two vertices, avoiding any division by zero.
  2. Guard the denominator with `safe_denom = np.where(np.abs(denom) < 1e-12, 1e-12, denom)` and handle collinear line intersections cleanly.

---

### [High] Challenge 2: OpenFOAM `passed_openfoam_surfacecheck` Completely Bypasses Triangle Quality Checks
- **Location**: `backend/geometry/audit/surface_auditor.py`, lines 118–127
- **Assumption Challenged**: Assumed that the Boolean verdict `passed_openfoam_surfacecheck` enforces all 10 OpenFOAM `surfaceCheck` criteria specified in `TEST_INFRA.md`.
- **Attack Scenario**: Construct a watertight, 2-manifold genus-0 tetrahedron where vertices are perturbed to create severe sliver triangles ($Q = 2.3 \times 10^{-12} \ll 10^{-5}$), resulting in `bad_faces_count == 4`.
- **Observed Behavior**:
  ```python
  passed_openfoam = (
      len(illegal_faces) == 0
      and len(open_edges) == 0
      and len(multi_edges) == 0
      and normal_zones_count == 1
      and is_outward
      and (euler_chi == 2)
      and (self_intersections_count == 0)
      and (len(close_points) == 0)
  )
  ```
  `passed_openfoam_surfacecheck` returns `True` despite all 4 faces having equilateral quality $Q \approx 10^{-12}$!
- **Empirical Test**: `test_defect_sliver_mesh_passes_surfacecheck_unflagged` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: Severely distorted sliver triangles produce catastrophic skewness and unorthogonality in OpenFOAM boundary-layer snapping, causing mesh generation failure or non-converging CFD simulations.
- **Mitigation**: Include `len(bad_faces) == 0` (or `quality_min >= 1e-5`) in the `passed_openfoam` boolean predicate.

---

### [High] Challenge 3: Inconsistent Quality Threshold ($10^{-11}$ vs $10^{-5}$) in `quality_metrics.py`
- **Location**: `backend/geometry/audit/quality_metrics.py`, line 87
- **Assumption Challenged**: Conformance with the authoritative specification in `PROJECT.md` Feature 18, `TEST_INFRA.md` Section 4 Check 2, and `TEST_READY.md` Check 2.
- **Observed Behavior**:
  ```python
  # Line 87 in quality_metrics.py:
  bad_faces = np.where(q < 1e-11)[0].tolist()
  ```
  The code sets the threshold for `bad_faces` at $10^{-11}$. However, `PROJECT.md`, `TEST_INFRA.md`, and the prompt instructions explicitly define the threshold as $Q < 10^{-5}$.
- **Blast Radius**: Triangles with $Q \in [10^{-11}, 10^{-5}]$ are considered acceptable by `quality_metrics.py`, even though they violate the CFD mesh quality criteria for snappyHexMesh.
- **Mitigation**: Allow a configurable threshold defaulting to $10^{-5}$:
  ```python
  def compute_equilateral_quality(mesh: SurfaceMesh, min_q_tol: float = 1e-5) -> ...
      bad_faces = np.where(q < min_q_tol)[0].tolist()
  ```

---

### [High] Challenge 4: Global Normal Auto-Repair Corrupts Valid Components in Multi-Body Meshes
- **Location**: `backend/geometry/audit/surface_auditor.py`, lines 84–95
- **Assumption Challenged**: Assumed that inverting the global face array `mesh.faces[:, [1, 2]] = mesh.faces[:, [2, 1]]` is a valid repair mechanism for any mesh with negative net volume.
- **Attack Scenario**: Construct a multi-component mesh containing Component 1 (small outward cube, $V_1 = +1.0$) and Component 2 (large inverted cube, $V_2 = -8.0$). Net signed volume is $-7.0 < 0.0$.
- **Observed Behavior**:
  `SurfaceAuditor(auto_repair_normals=True)` detects net negative volume and flips the entire face array. After repair:
  - Component 2 becomes outward ($V_2 = +8.0$).
  - Component 1 is corrupted from outward to inverted ($V_1 = -1.0$)!
- **Empirical Test**: `test_multi_component_global_auto_repair_inversion_hazard` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: In assemblies with multiple solids or fluid/solid conjugated heat transfer domains, global winding reversal corrupts valid solids, inverting flow domain boundaries.
- **Mitigation**: Normal orientation repair must partition faces by connected component and invert winding only on components whose individual signed volume is negative.

---

### [Medium] Challenge 5: Artificial `< 4` Face Count Bypass for Self-Intersections
- **Location**: `backend/geometry/audit/surface_auditor.py`, line 258
- **Assumption Challenged**: Assumed that any mesh with $N < 4$ faces can never have self-intersections.
- **Attack Scenario**: Input a surface mesh with 2 non-adjacent intersecting triangles.
- **Observed Behavior**:
  ```python
  if n_faces < 4:
      return 0
  ```
  The broad-phase check immediately returns 0 without checking bounding boxes or invoking the Moeller algorithm.
- **Empirical Test**: `test_defect_n_faces_under_4_silently_ignores_intersections` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: Small surface patches or CAD features with low triangle counts bypass self-intersection auditing.
- **Mitigation**: Change condition to `if n_faces < 2: return 0`.

---

### [Medium] Challenge 6: Incomplete 2-Manifold Definition Omits Vertex Bow-Tie Verification
- **Location**: `backend/geometry/audit/surface_auditor.py`, line 67
- **Assumption Challenged**: Assumed that an edge-manifold surface ($|F(e)| \le 2$) is guaranteed to be 2-manifold.
- **Attack Scenario**: Two tetrahedra meeting at a single pinch vertex (bow-tie vertex).
- **Observed Behavior**:
  - `open_edges_count == 0`
  - `multi_edges_count == 0`
  - `report.is_manifold == True`
  The report claims the mesh is 2-manifold even though the neighborhood of the shared vertex is homeomorphic to two cones touching at the apex, violating the 2-manifold definition.
- **Empirical Test**: `test_limitation_is_manifold_only_checks_edges_not_vertices` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: If a non-manifold pinch vertex occurs on a geometry with non-zero genus where the Euler characteristic happens to evaluate to 2, `passed_openfoam_surfacecheck` will falsely pass the mesh.
- **Mitigation**: Verify vertex manifoldness: for every vertex $v$, the link of adjacent triangles must form a single connected cycle.

---

### [Medium] Challenge 7: Coplanar Overlapping Triangles and Vertex-Sharing Penetrations Ignored
- **Location**: `backend/geometry/audit/surface_auditor.py`, lines 279, 308, 327
- **Observed Behavior**:
  - Coplanar triangles trigger `if np.all(dist1 == 0.0): return False` and are never checked for 2D polygon intersection.
  - Triangles sharing 1 vertex trigger `if len(set(f_i).intersection(f_j)) > 0: continue` and are skipped even if one penetrates through the interior of the other.
- **Empirical Tests**: `test_defect_coplanar_overlapping_triangles_missed` and `test_defect_vertex_sharing_penetrating_triangles_missed` in `tests/test_adversarial_audit.py`.
- **Blast Radius**: Coincident or overlapping surface patches from adjacent CAD bodies are not flagged as self-intersections.
- **Mitigation**: Implement 2D projection overlap testing for coplanar triangles, and allow intersection testing when triangles share only 1 vertex (skipping only edge-sharing triangles).

---

## 3. Stress Test Results Summary

| # | Stress Test Scenario | Expected Outcome | Actual Auditor Output | Test Result |
|---|---|---|---|:---:|
| 1 | Regular Tetrahedron | $\chi=2$, open=0, multi=0, $V>0$, PASS | $\chi=2$, open=0, multi=0, $V>0$, PASS | **PASS** |
| 2 | Regular Octahedron | $\chi=2$, open=0, multi=0, $V>0$, PASS | $\chi=2$, open=0, multi=0, $V>0$, PASS | **PASS** |
| 3 | Genus-1 Torus | $\chi=0$, open=0, multi=0, FAIL (genus $\ne 0$) | $\chi=0$, open=0, multi=0, FAIL | **PASS** |
| 4 | 3-Face Y-Junction Edge | `multi_edges=1`, `is_manifold=False` | `multi_edges=1`, `is_manifold=False` | **PASS** |
| 5 | 4-Face X-Junction Edge | `multi_edges>=1`, `is_manifold=False` | `multi_edges>=1`, `is_manifold=False` | **PASS** |
| 6 | Bow-Tie Vertex (Two Tets) | $\chi=3 \ne 2$, FAIL surfaceCheck | $\chi=3$, FAIL surfaceCheck | **PASS** |
| 7 | Bow-Tie `is_manifold` | `is_manifold=False` (2-manifold violation) | `is_manifold=True` (**DEFECT**) | **CONFIRMED** |
| 8 | Unwelded T-Junction Midpoint | `open_edges>=3`, `is_watertight=False` | `open_edges=3`, `is_watertight=False` | **PASS** |
| 9 | 1-Face Hole in Unit Cube | `open_edges=3`, $\chi=1$, FAIL | `open_edges=3`, $\chi=1$, FAIL | **PASS** |
| 10 | 2-Adjacent-Face Hole | `open_edges=4`, $\chi=1$, FAIL | `open_edges=4`, $\chi=1$, FAIL | **PASS** |
| 11 | Two Disjoint Closed Cubes | `components=2`, `normal_zones=2`, $\chi=4$ | `components=2`, `normal_zones=2`, $\chi=4$ | **PASS** |
| 12 | Nested Cubes (Mixed Winding) | `normal_zones=2`, FAIL | `normal_zones=2`, FAIL | **PASS** |
| 13 | Multi-Component Auto-Repair | Invert only negative component | Globally inverts both (**DEFECT**) | **CONFIRMED** |
| 14 | Inverted Cube (No Auto-Repair) | `is_outward=False`, $V < 0$, FAIL | `is_outward=False`, $V < 0$, FAIL | **PASS** |
| 15 | Inverted Cube (With Auto-Repair)| `is_outward=True`, $V > 0$, PASS | `is_outward=True`, $V > 0$, PASS | **PASS** |
| 16 | 1 Face Inverted in Cube | `inconsistent_edges=3`, `zones=2`, FAIL | `inconsistent_edges=3`, `zones=2`, FAIL | **PASS** |
| 17 | Duplicate Vertex Indices `[0,0,1]` | `illegal_faces` flagged | `illegal_faces` flagged | **PASS** |
| 18 | Collinear 3 Vertices (Area = 0) | `illegal_faces` flagged | `illegal_faces` flagged | **PASS** |
| 19 | Microscopic Area ($10^{-16} < 10^{-14}$)| `illegal_faces` flagged | `illegal_faces` flagged | **PASS** |
| 20 | Coincident Vertex Coordinates | `illegal_faces` flagged | `illegal_faces` flagged | **PASS** |
| 21 | Needle Sliver ($Q < 10^{-5}$) | $Q < 10^{-5}$, flagged in `bad_faces` | $Q < 10^{-5}$, `bad_faces=0` (**DEFECT**) | **CONFIRMED** |
| 22 | Cap Sliver (Obtuse Angle) | $Q < 10^{-5}$ | $Q < 10^{-5}$ | **PASS** |
| 23 | Equilateral Triangle ($Q = 1.0$) | $Q = 1.0$, hist bin 19 | $Q = 1.0$, hist bin 19 | **PASS** |
| 24 | Quality Histogram Bins | Exactly 20 bins, sum = $|F|$ | Exactly 20 bins, sum = $|F|$ | **PASS** |
| 25 | Watertight Sliver Mesh Verdict | FAIL OpenFOAM surfaceCheck | PASS OpenFOAM (**DEFECT**) | **CONFIRMED** |
| 26 | Piercing Triangles ($N \ge 4$) | `self_intersections=1`, FAIL | `self_intersections=1`, FAIL | **PASS** |
| 27 | Piercing Triangles ($N < 4$) | `self_intersections=1`, FAIL | `self_intersections=0` (**DEFECT**) | **CONFIRMED** |
| 28 | Interpenetrating Cubes | `self_intersections > 0`, FAIL | `self_intersections > 0`, FAIL | **PASS** |
| 29 | Edge in Adjacent Plane | `self_intersections=1`, No NaN | NaN / 0/0 divide, `intersections=0` (**CRITICAL DEFECT**) | **CONFIRMED** |
| 30 | Coplanar Overlapping Triangles | `self_intersections=1` | Returns `False` (**DEFECT**) | **CONFIRMED** |

---

## 4. Unchallenged Areas
- **Extreme Scale Tessellations ($N_f > 100,000$)**: Memory consumption and $O(N^2)$ worst-case broad-phase self-intersection scaling were not benchmarked due to CPU time constraints.
- **Non-Standard STEP Assemblies with Mixed Units**: Testing focused on single solid B-Rep cubes in standard mm units.

---

## 5. Required Remediations for Approval

1. **Fix Moeller Intersection 0/0 Division**: Handle edge-in-plane cases where $dists[i1] - dists[i0] == 0$ without division by zero.
2. **Add Quality Check to `passed_openfoam_surfacecheck`**: Ensure `quality_min >= 1e-5` (or `len(bad_faces) == 0`) is required to pass OpenFOAM surfaceCheck.
3. **Align `bad_faces` Threshold**: Change $10^{-11}$ to $10^{-5}$ in `QualityMetrics.compute_equilateral_quality` as mandated by `PROJECT.md` and `TEST_INFRA.md`.
4. **Per-Component Normal Auto-Repair**: Compute signed volume and invert winding independently for each connected component.
5. **Remove $N < 4$ Bypass**: Allow self-intersection broad phase for all meshes with $N \ge 2$.
