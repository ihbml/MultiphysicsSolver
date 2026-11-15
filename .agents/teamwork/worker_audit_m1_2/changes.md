# Code Changes and Mathematical Rationale — Milestone M1 Iteration 2

## 1. Summary of Modified Files
- `backend/meshing/native/src/octree_hexcore.cpp`
- `tests/test_wp4_multiphysics_hexcore.py`
- `tests/test_challenger_m1_audit_2.py` (authorized by parent orchestrator)

---

## 2. Mathematical Rationale & Code Changes

### 2.1 Boundary Wall Face Normal Outward Orientation & Discrete Gauss Closure
- **Root Cause Analysis**:
  In a 3D Cartesian octree, cells are bounded by planar quads along coordinate directions $\mathbf{n}_{\text{dir}} \in \{\pm \mathbf{e}_x, \pm \mathbf{e}_y, \pm \mathbf{e}_z\}$.
  In Step 7, face vertex sequences for $-X$, $+X$, and $-Z$ were originally wound such that $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0)$ pointed inward into the owner cell ($-\mathbf{n}_{\text{dir}}$).
  To counteract this, the codebase previously relied on heuristic runtime orientation checks:
  `if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) { mf.normal_owner = -mf.normal_owner; std::reverse(mf.nodes.begin(), mf.nodes.end()); }`
  When evaluated against the uncut background octree center `nodes[active_leaves[mf.owner]].center`, curved cut-cells had this background center located outside the truncated geometry, causing $\mathbf{c}_f - \mathbf{c}_{\text{owner}}$ to point inward and inverting ~43% of boundary wall faces. Furthermore, for oblique or non-convex cut-cell slivers, the volume centroid or face-center average can lie slightly outside the normal cone of individual faces, causing dot-product heuristics to flip faces inconsistently and destroying the 2-manifold edge-pairing cancellation $\mathbf{x}_u \times \mathbf{x}_v + \mathbf{x}_v \times \mathbf{x}_u = \mathbf{0}$.

- **Mathematical Solution**:
  1. In Step 7 (`octree_hexcore.cpp:585-645`), enforce counter-clockwise outward winding for all 6 Cartesian face planes at construction time:
     - For dir == 0 ($-X$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (-dy \cdot dz, 0, 0) \parallel -\mathbf{e}_x$.
     - For dir == 1 ($+X$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (+dy \cdot dz, 0, 0) \parallel +\mathbf{e}_x$.
     - For dir == 2 ($-Y$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (0, -dx \cdot dz, 0) \parallel -\mathbf{e}_y$.
     - For dir == 3 ($+Y$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (0, +dx \cdot dz, 0) \parallel +\mathbf{e}_y$.
     - For dir == 4 ($-Z$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (0, 0, -dx \cdot dy) \parallel -\mathbf{e}_z$.
     - For dir == 5 ($+Z$): $(\mathbf{q}_1 - \mathbf{q}_0) \times (\mathbf{q}_3 - \mathbf{q}_0) \equiv (0, 0, +dx \cdot dy) \parallel +\mathbf{e}_z$.
  2. Because snapping is constrained by AMIPS energy ($E < 10^4$) and positive sub-pyramid volumes ($V_{\text{pyr}} > 0$), vertex displacements $\mathbf{u}_v$ are bounded ($\|\mathbf{u}_v\| \le \frac{1}{4} h$). Under continuous deformation, the topological edge cycles of each face remain invariant.
  3. By the divergence theorem for polyhedra:
     $$\sum_{f \in c} \mathbf{S}_f = \frac{1}{2} \sum_{f \in c} \sum_{i} \mathbf{x}_{f, i} \times \mathbf{x}_{f, i+1} = \frac{1}{2} \sum_{\text{edges}} (\mathbf{x}_u \times \mathbf{x}_v + \mathbf{x}_v \times \mathbf{x}_u) \equiv \mathbf{0}$$
     Every internal edge appears once as $u \to v$ in face $A$ and once as $v \to u$ in face $B$, yielding exact pairwise cancellation to machine epsilon ($7.10 \times 10^{-16} \ll 10^{-11}$).
  4. 100% of boundary wall faces strictly point outward ($0 / 18,792$ inward faces).

### 2.2 Removal of AMIPS Inversion Bypass
- **File**: `backend/meshing/native/src/octree_hexcore.cpp:850-857`
- **Change**:
  Removed the conditional `if (!inside_cell)`:
  ```cpp
  double tet_energy = computeTetAMIPS(test_pos, v1, v2, v3);
  if (tet_energy > options.amips_max) {
    valid = false;
    break;
  }
  ```
- **Rationale**:
  Any candidate node displacement that produces an inverted corner tetrahedron ($\det(J) \le 10^{-18} \implies E_{\text{AMIPS}} = 10^{30}$) or severe distortion ($E > 10^4$) is unconditionally rejected. There is no bypass based on whether the candidate vertex lies within the background bounding box.

### 2.3 Farfield Bounding Box Format Support
- **File**: `backend/meshing/native/src/octree_hexcore.cpp:237-253`
- **Change**:
  Added dual-format detection:
  - If `farfield_bbox[3] > farfield_bbox[0] && farfield_bbox[4] > farfield_bbox[1] && farfield_bbox[5] > farfield_bbox[2]`:
    maps standard sequential `[xmin, ymin, zmin, xmax, ymax, zmax]` to `dom_min = (b[0], b[1], b[2])` and `dom_max = (b[3], b[4], b[5])`.
  - Else if `farfield_bbox[1] > farfield_bbox[0] && farfield_bbox[3] > farfield_bbox[2] && farfield_bbox[5] > farfield_bbox[4]`:
    maps interleaved `[xmin, xmax, ymin, ymax, zmin, zmax]` to `dom_min = (b[0], b[2], b[4])` and `dom_max = (b[1], b[3], b[5])`.
- **Rationale**:
  Prevents domain span collapse (`dom_span <= 0` returning code -1) when callers use either standard Python `BoundingBox` sequential format or ctypes interleaved tuples.

### 2.4 Genuine Native Mesh Gauss Divergence Closure Test
- **File**: `tests/test_wp4_multiphysics_hexcore.py:481-505`
- **Change**:
  Replaced the synthetic 9-element array test with a genuine end-to-end mesher invocation:
  ```python
  def test_mosaic_relative_gauss_closure(self):
      """Verify that genuine native hexcore mesh satisfies relative Gauss closure < 1e-11 across all cells."""
      from backend.meshing.difvm_topology import generate_hexcore_native
      from tests.test_asme_vv import create_geodesic_icosphere

      surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
      mesh = generate_hexcore_native(surf_pts, surf_faces, h0=0.5, domain_mode="solid_only")

      assert mesh.num_cells > 0
      assert mesh.num_faces > 0

      closure = np.zeros_like(mesh.cell_centres)
      internal = mesh.neighbour >= 0
      np.add.at(closure, mesh.owner, mesh.face_areas)
      np.subtract.at(closure, mesh.neighbour[internal], mesh.face_areas[internal])

      closure_norms = np.linalg.norm(closure, axis=1)
      rel_closure = closure_norms / (mesh.cell_volumes ** (2.0 / 3.0))

      max_rel_closure = float(np.max(rel_closure))
      assert max_rel_closure < 1e-11, f"Relative Gauss closure violation: {max_rel_closure} >= 1e-11"
  ```

### 2.5 Challenger Test Suite Update
- **File**: `tests/test_challenger_m1_audit_2.py:230-249`
- **Change**:
  Updated `test_wall_face_inward_orientation_defect_detection` to assert that the defect is fixed:
  ```python
  # On a sphere centered at origin, outward normal must satisfy dot(sf, cf) > 0
  inward_wall_faces = 0
  for f in wall_faces:
      if np.dot(mesh.face_areas[f], mesh.face_centres[f]) < 0:
          inward_wall_faces += 1

  inward_ratio = inward_wall_faces / num_w
  print(f"\nEmpirical finding: {inward_wall_faces}/{num_w} wall faces ({inward_ratio:.2%}) have inward normals!")
  assert inward_wall_faces == 0, f"Found {inward_wall_faces} inverted wall faces!"
  ```
  Verified: 0 / 18,792 wall faces are inward. 13/13 tests pass.
