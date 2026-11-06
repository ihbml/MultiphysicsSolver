# Technical Survey & Audit Report: Native C++ Octree Hexcore & Mesh Generation Mechanics

**Author**: `explorer_audit_survey_1` (Teamwork Explorer)  
**Date**: 2026-11-06  
**Target Requirements**: R1, R2, R3.1-2, R5.2-3  
**Target Files Analyzed**:
- `backend/meshing/native/src/octree_hexcore.cpp`
- `backend/meshing/native/src/difvm_mesh.cpp`
- `backend/meshing/native/src/distmesh.cpp`
- `backend/meshing/native/src/cfd_mesher_native.cpp`
- `backend/meshing/native/include/octree_hexcore.hpp`, `distmesh.hpp`, `cfd_mesher_native.h`
- `backend/meshing/difvm_topology.py`
- `backend/meshing/mesh_engine.py`
- `backend/meshing/distmesh_core.py`
- `tests/test_wp4_multiphysics_hexcore.py`

---

## 1. Executive Summary

This survey provides a comprehensive mathematical, architectural, and algorithmic analysis of the native C++ meshing backend in CFDSolver. Four critical defects in `octree_hexcore.cpp` and `distmesh.cpp` were investigated and mathematically resolved:

1. **R1 (Paralyzed Cut-Cell Snapping)**: In `octree_hexcore.cpp:646`, `computeTetAMIPS` is called with arguments `(test_pos, onode.bmin, onode.bmax, onode.center)`. Because `center = 0.5*(bmin + bmax)`, the three reference vertices are strictly collinear, making the Jacobian determinant identically zero ($\det(J) \equiv 0$). This unconditionally triggers the degeneracy guard (`det_j <= 1e-18 -> 1e30`), causing every line-search step to fail and disabling cut-cell snapping entirely. We formulate the exact corner tetrahedron construction based on corner index $c\_idx \in [0, 7]$ and the 3 adjacent octant edge neighbors, ensuring $\det(J) = dx \cdot dy \cdot dz > 0$, finite AMIPS energy, and positive sub-pyramid volumes.
2. **R2 (Translation-Variant Centroids & Flawed Face Normals)**: In `octree_hexcore.cpp:667-708`, polygon face normals/centroids assume flat quads, while cell centroid integration evaluates $\frac{1}{3} \sum \mathbf{c}_f (\mathbf{c}_f \cdot \mathbf{S}_f)$. On a unit cube $[0, 1]^3$, this formula yields $(0.6667, 0.6667, 0.6667)$ instead of $(0.5, 0.5, 0.5)$; on a shifted cube $[10, 11]^3$, it yields $(14.0, 14.0, 14.0)$ instead of $(10.5, 10.5, 10.5)$ (a 350% error). We replace this with area-weighted triangle-fan integration and local reference point pyramid volume decomposition ($c_{approx}$), proving exact translation invariance.
3. **R3.1-2 (Illegal Duplicate Internal Faces & Missing Omega Cell Weighting)**: During small-cell agglomeration in Step 11, merging small cell $c$ into neighbor $best\_nb$ creates duplicate internal faces sharing the identical `(owner, neighbour)` pair when both cells bordered a common third cell. In OpenFOAM, this triggers fatal `checkMesh` errors. We formulate strict face deduplication/merging and upper-triangular ordering (`owner < neighbour`). Furthermore, `mesh.omega_cell` is left unpopulated at 1.0; we port the cell-wise minimum upwind factor $\omega_c = \max(0.1, \min_{f \in c} \beta_f)$ from `difvm_mesh.cpp:666-677`.
4. **R5.2-3 (DistMesh Interior Push-Out & Cell Filtering Stub)**: In `distmesh.cpp:734-739`, when interior nodes are pushed outside during spring relaxation, they are frozen instead of projected onto boundary segments. In `distmesh.cpp:239-388`, `surf_triangles` is completely cast to `(void)`. We formulate orthogonal projection onto the nearest polygon boundary segment and specify 3D cell filtering via `MeshSDF` or delegation to `OctreeHexcore(domain_mode = 2)`.

---

## 2. Requirement R1: Cut-Cell Snapping & AMIPS Metric Fix

### 2.1 Problem Analysis: Why `computeTetAMIPS` Always Produces Inversion

In `backend/meshing/native/src/octree_hexcore.cpp:644-651`:
```cpp
for (int64_t cell_idx : node_to_cells[v]) {
  const OctreeNode& onode = nodes[active_leaves[cell_idx]];
  double tet_energy = computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center);
  if (tet_energy > options.amips_max) {
    valid = false;
    break;
  }
}
```

The function `computeTetAMIPS(v0, v1, v2, v3)` evaluates the 3D AMIPS distortion energy (Hu et al. 2020, *fTetWild*):
```cpp
double computeTetAMIPS(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3) {
  Vec3 e1 = v1 - v0;
  Vec3 e2 = v2 - v0;
  Vec3 e3 = v3 - v0;
  double det_j = dot(cross(e1, e2), e3);
  if (det_j <= 1e-18) {
    return 1e30;  // Inverted or degenerate element
  }
  double edge_sum_sq = (v0 - v1).norm2() + (v0 - v2).norm2() + (v0 - v3).norm2() +
                       (v1 - v2).norm2() + (v1 - v3).norm2() + (v2 - v3).norm2();
  double ratio = (27.0 / 16.0) * (edge_sum_sq * edge_sum_sq * edge_sum_sq) / (det_j * det_j);
  return std::cbrt(ratio);
}
```

#### Mathematical Proof of Degeneracy:
Let $\mathbf{b}_{\min} = (x_{\min}, y_{\min}, z_{\min})$ and $\mathbf{b}_{\max} = (x_{\max}, y_{\max}, z_{\max})$.
The node center is defined as:
$$\mathbf{c} = \frac{1}{2}(\mathbf{b}_{\min} + \mathbf{b}_{\max})$$
Let $\mathbf{v}_0 = \mathbf{x}_{test}$, $\mathbf{v}_1 = \mathbf{b}_{\min}$, $\mathbf{v}_2 = \mathbf{b}_{\max}$, and $\mathbf{v}_3 = \mathbf{c}$.
The edge vectors from $\mathbf{v}_0$ are:
$$\mathbf{e}_1 = \mathbf{b}_{\min} - \mathbf{x}_{test}, \quad \mathbf{e}_2 = \mathbf{b}_{\max} - \mathbf{x}_{test}$$
$$\mathbf{e}_3 = \mathbf{c} - \mathbf{x}_{test} = \frac{1}{2}(\mathbf{b}_{\min} + \mathbf{b}_{\max}) - \mathbf{x}_{test} = \frac{1}{2}\mathbf{e}_1 + \frac{1}{2}\mathbf{e}_2$$
The scalar triple product is:
$$\det(\mathbf{J}) = (\mathbf{e}_1 \times \mathbf{e}_2) \cdot \mathbf{e}_3 = (\mathbf{e}_1 \times \mathbf{e}_2) \cdot \left(\frac{1}{2}\mathbf{e}_1 + \frac{1}{2}\mathbf{e}_2\right) = \frac{1}{2}(\mathbf{e}_1 \times \mathbf{e}_2) \cdot \mathbf{e}_1 + \frac{1}{2}(\mathbf{e}_1 \times \mathbf{e}_2) \cdot \mathbf{e}_2 \equiv 0$$
Because $(\mathbf{e}_1 \times \mathbf{e}_2)$ is orthogonal to both $\mathbf{e}_1$ and $\mathbf{e}_2$, $\det(\mathbf{J}) \equiv 0$ identically for **any** point $\mathbf{x}_{test} \in \mathbb{R}^3$.
As a direct consequence:
$$\det(\mathbf{J}) \le 10^{-18} \implies \text{computeTetAMIPS returns } 10^{30}$$
Since $10^{30} > \text{options.amips\_max} = 10^4$, `valid` is set to `false` for every test position, line-search never accepts $\lambda > 0$, and snapping is completely broken.

---

### 2.2 The Positively Oriented Corner Tetrahedron Construction

To evaluate the true distortion of the cell at corner vertex $v$, we must construct a non-degenerate, positively oriented tetrahedron using vertex $v$ and its 3 adjacent edge neighbors in the octree node `onode`.

#### Step 1: Corner Index Identification
For octree node `onode` with bounds $[\mathbf{b}_{\min}, \mathbf{b}_{\max}]$ and cell span $(dx, dy, dz) = \mathbf{b}_{\max} - \mathbf{b}_{\min}$:
Determine the corner index $c\_idx \in \{0, \dots, 7\}$ of vertex $\mathbf{p} = \text{node\_coords}[v]$:
$$cx = (\mathbf{p}.x \ge \text{onode.center}.x) \;?\; 1 : 0$$
$$cy = (\mathbf{p}.y \ge \text{onode.center}.y) \;?\; 1 : 0$$
$$cz = (\mathbf{p}.z \ge \text{onode.center}.z) \;?\; 1 : 0$$
$$c\_idx = cz \cdot 4 + cy \cdot 2 + cx$$

The corner vertex location in the box is:
$$\mathbf{v}_{corner} = \big( (cx == 0 ? \mathbf{b}_{\min}.x : \mathbf{b}_{\max}.x),\; (cy == 0 ? \mathbf{b}_{\min}.y : \mathbf{b}_{\max}.y),\; (cz == 0 ? \mathbf{b}_{\min}.z : \mathbf{b}_{\max}.z) \big)$$

#### Step 2: Identification of the 3 Adjacent Edge Neighbors
Within `onode`, the three vertices directly connected to $\mathbf{v}_{corner}$ along the coordinate axes are:
$$\mathbf{v}_x = \mathbf{v}_{corner} + (1 - 2\cdot cx) dx \cdot \hat{\mathbf{i}}$$
$$\mathbf{v}_y = \mathbf{v}_{corner} + (1 - 2\cdot cy) dy \cdot \hat{\mathbf{j}}$$
$$\mathbf{v}_z = \mathbf{v}_{corner} + (1 - 2\cdot cz) dz \cdot \hat{\mathbf{k}}$$

#### Step 3: Enforcing Right-Handed Orientation ($\det(\mathbf{J}) = dx \cdot dy \cdot dz > 0$)
The displacement vectors from $\mathbf{v}_{corner}$ are:
$$\boldsymbol{\delta}_x = (1 - 2\cdot cx) dx \cdot \hat{\mathbf{i}}, \quad \boldsymbol{\delta}_y = (1 - 2\cdot cy) dy \cdot \hat{\mathbf{j}}, \quad \boldsymbol{\delta}_z = (1 - 2\cdot cz) dz \cdot \hat{\mathbf{k}}$$
The triple product of these vectors is:
$$(\boldsymbol{\delta}_x \times \boldsymbol{\delta}_y) \cdot \boldsymbol{\delta}_z = (1 - 2\cdot cx)(1 - 2\cdot cy)(1 - 2\cdot cz) \cdot (dx \cdot dy \cdot dz)$$
Let sign factor $s = (1 - 2\cdot cx)(1 - 2\cdot cy)(1 - 2\cdot cz) \in \{-1, +1\}$.
To guarantee $\det(\mathbf{J}) > 0$ for tetrahedron $(\mathbf{v}_0, \mathbf{v}_1, \mathbf{v}_2, \mathbf{v}_3)$ where $\mathbf{v}_0 = \mathbf{x}_{test}$:
- If $s > 0$: set $\mathbf{v}_1 = \mathbf{v}_x$, $\mathbf{v}_2 = \mathbf{v}_y$, $\mathbf{v}_3 = \mathbf{v}_z$.
- If $s < 0$: set $\mathbf{v}_1 = \mathbf{v}_y$, $\mathbf{v}_2 = \mathbf{v}_x$, $\mathbf{v}_3 = \mathbf{v}_z$ (swap $\mathbf{v}_1$ and $\mathbf{v}_2$).

Before snapping ($\mathbf{x}_{test} = \mathbf{v}_{corner}$):
$$\det(\mathbf{J}) = dx \cdot dy \cdot dz > 0$$
The edge lengths are:
$$l_1 = dx, \quad l_2 = dy, \quad l_3 = dz, \quad l_{12} = \sqrt{dx^2+dy^2}, \quad l_{23} = \sqrt{dy^2+dz^2}, \quad l_{31} = \sqrt{dz^2+dx^2}$$
For a cube ($dx = dy = dz = h$):
$$\sum l^2 = 3 h^2 + 3(2 h^2) = 9 h^2, \quad \det(\mathbf{J}) = h^3$$
$$E_{\text{AMIPS}} = \left[ \frac{27}{16} \frac{(9 h^2)^3}{(h^3)^2} \right]^{1/3} = \left[ \frac{27 \times 729}{16} \right]^{1/3} = \frac{27}{2\sqrt[3]{2}} \approx 10.71 \ll 10^4$$
This yields a well-conditioned, finite AMIPS metric!

---

### 2.3 Verification of Cell Sub-Pyramids Sharing Vertex $v$

In addition to the corner tetrahedron, moving vertex $v$ to $\mathbf{x}_{test}$ must preserve positive volume for all cell sub-pyramids sharing $v$:
For each face $f$ of cell $c$ containing $v$:
1. Update face coordinates with $v \to \mathbf{x}_{test}$.
2. Compute new face normal $\mathbf{S}_f$ and centroid $\mathbf{c}_f$ via triangle fan.
3. Compute the sub-pyramid volume between face $f$ and cell center $\mathbf{c}_{cell}$:
   $$V_{pyr, f} = \frac{1}{3} (\mathbf{c}_f - \mathbf{c}_{cell}) \cdot \mathbf{S}_{f,out}$$
4. If $V_{pyr, f} \le 10^{-15} \cdot V_{bg}$, the element is degenerating or inverting $\implies$ reject $\lambda$.

---

### 2.4 Sphere Test Specification in `tests/test_wp4_multiphysics_hexcore.py`

Add test method `test_cut_cell_sphere_wall_snapping_native`:
```python
def test_cut_cell_sphere_wall_snapping_native(self):
    """Verify cut-cell wall nodes snap to |phi| < 1e-3 on a spherical CAD surface using generate_hexcore_native."""
    from tests.test_asme_vv import create_geodesic_icosphere
    from backend.meshing.difvm_topology import generate_hexcore_native

    # 1. Unit sphere CAD geometry (radius R = 1.0, center = origin)
    surf_pts, surf_faces = create_geodesic_icosphere(subdivisions=2)
    assert len(surf_pts) > 0 and len(surf_faces) > 0

    # 2. Call native hexcore mesher
    h0 = 0.15
    mesh = generate_hexcore_native(
        surf_points=surf_pts,
        surf_faces=surf_faces,
        h0=h0,
        domain_mode="solid_only",
    )

    assert mesh.num_cells > 0
    assert "wall" in mesh.boundaries

    # 3. Extract wall boundary nodes
    start_f, num_f = mesh.boundaries["wall"]
    wall_node_indices = set()
    for f_idx in range(start_f, start_f + num_f):
        for v in mesh.faces[f_idx]:
            wall_node_indices.add(v)

    wall_nodes = mesh.points[list(wall_node_indices)]
    assert len(wall_nodes) > 0

    # 4. Evaluate exact signed distance: phi(x) = ||x|| - 1.0
    radii = np.linalg.norm(wall_nodes, axis=1)
    phi_vals = np.abs(radii - 1.0)

    # 5. Verify snapping tolerance: max |phi| < 1e-3
    assert np.max(phi_vals) < 1e-3
    assert np.all(mesh.cell_volumes > 0.0)
```

---

## 3. Requirement R2: Translation-Invariant Polygon Centroids, Face Normals, and Cell Volumes

### 3.1 Flaw Analysis in Steps 9 & 10 (`octree_hexcore.cpp:667-708`)

Currently, Step 9 computes face normals via:
```cpp
const Vec3& p0 = node_coords[mf.nodes[0]];
const Vec3& p1 = node_coords[mf.nodes[1]];
const Vec3& p2 = node_coords[mf.nodes[2]];
const Vec3& p3 = node_coords[mf.nodes[3]];
mf.normal_owner = (cross(p1 - p0, p2 - p0) + cross(p2 - p0, p3 - p0)) * 0.5;
mf.centre = (p0 + p1 + p2 + p3) * 0.25;
```
And Step 10 computes cell centroids via:
```cpp
double v_o = dot(mf.centre, mf.normal_owner) / 3.0;
cell_vols[o] += v_o;
cell_cents[o] += mf.centre * (dot(mf.centre, mf.normal_owner));
...
cell_cents[c] /= (cell_vols[c] * 3.0);
```

#### Mathematical Proof of Centroid Error:
Consider a unit cube $[0, 1]^3$:
The 6 faces have:
- $x = 1$: center $(1, 0.5, 0.5)$, normal $(1, 0, 0)$. Term: $1 \cdot (1, 0.5, 0.5) = (1, 0.5, 0.5)$
- $x = 0$: center $(0, 0.5, 0.5)$, normal $(-1, 0, 0)$. Term: $0 \cdot (-1) \cdot (0, 0.5, 0.5) = (0, 0, 0)$
- $y = 1$: center $(0.5, 1, 0.5)$, normal $(0, 1, 0)$. Term: $(0.5, 1, 0.5)$
- $y = 0$: center $(0.5, 0, 0.5)$, normal $(0, -1, 0)$. Term: $(0, 0, 0)$
- $z = 1$: center $(0.5, 0.5, 1)$, normal $(0, 0, 1)$. Term: $(0.5, 0.5, 1)$
- $z = 0$: center $(0.5, 0.5, 0)$, normal $(0, 0, -1)$. Term: $(0, 0, 0)$

Summing across all faces:
$$\mathbf{C}_{\text{sum}} = (1 + 0.5 + 0.5,\; 0.5 + 1 + 0.5,\; 0.5 + 0.5 + 1) = (2.0, 2.0, 2.0)$$
Dividing by $3 \cdot V_c = 3.0$:
$$\mathbf{C}_{\text{computed}} = \left(\frac{2}{3}, \frac{2}{3}, \frac{2}{3}\right) \approx (0.6667, 0.6667, 0.6667) \ne (0.5, 0.5, 0.5)$$

Now, shift the cube to $[10, 11]^3$:
Face $x = 11$: center $(11, 10.5, 10.5)$, normal $(1, 0, 0)$. Term: $11 \cdot (11, 10.5, 10.5) = (121, 115.5, 115.5)$
Face $x = 10$: center $(10, 10.5, 10.5)$, normal $(-1, 0, 0)$. Term: $-10 \cdot (10, 10.5, 10.5) = (-100, -105, -105)$
Net $X$-face contribution: $(21, 10.5, 10.5)$.
By symmetry, total sum:
$$\mathbf{C}_{\text{sum}} = (21 + 10.5 + 10.5,\; 10.5 + 21 + 10.5,\; 10.5 + 10.5 + 21) = (42, 42, 42)$$
Dividing by $3 \cdot V_c = 3.0$:
$$\mathbf{C}_{\text{computed}} = (14.0, 14.0, 14.0) \ne (10.5, 10.5, 10.5)$$
The error is $3.5$ on a unit cell! The calculation is completely translation-dependent.

---

### 3.2 Exact Translation-Invariant Solution

#### Step 9: Triangle-Fan Normal and Area-Weighted Centroid
For each face $f$ with node indices $\mathbf{n}_f = [v_0, v_1, \dots, v_{k-1}]$:
$$\mathbf{p}_0 = \text{node\_coords}[v_0]$$
Initialize $\mathbf{S}_f = \mathbf{0}$, $\mathbf{c}_{\text{weighted}} = \mathbf{0}$, $A_{\text{total}} = 0$.
For $i = 1 \dots k - 2$:
$$\mathbf{p}_i = \text{node\_coords}[v_i], \quad \mathbf{p}_{i+1} = \text{node\_coords}[v_{i+1}]$$
$$\mathbf{n}_{tri} = (\mathbf{p}_i - \mathbf{p}_0) \times (\mathbf{p}_{i+1} - \mathbf{p}_0)$$
$$A_{tri} = \frac{1}{2} \|\mathbf{n}_{tri}\|$$
$$\mathbf{S}_f \mathrel{+}= \frac{1}{2}\mathbf{n}_{tri}$$
$$\mathbf{c}_{tri} = \frac{1}{3}(\mathbf{p}_0 + \mathbf{p}_i + \mathbf{p}_{i+1})$$
$$\mathbf{c}_{\text{weighted}} \mathrel{+}= A_{tri} \cdot \mathbf{c}_{tri}$$
$$A_{\text{total}} \mathrel{+}= A_{tri}$$

If $A_{\text{total}} > 10^{-15}$:
$$mf.centre = \frac{\mathbf{c}_{\text{weighted}}}{A_{\text{total}}}$$
$$mf.normal\_owner = \mathbf{S}_f$$

**Folded / Inverted Face Detection (`mf.is_folded`)**:
For each sub-triangle $i$:
If $A_{tri} > 10^{-15}$ and $\mathbf{n}_{tri} \cdot \mathbf{S}_f \le 0$:
$$mf.is\_folded = \text{true}$$

#### Step 10: Local Reference Point Pyramid Volume Integration
For each cell $c$:
1. Compute local reference point $\mathbf{c}_{approx}$ as the mean of face centroids of cell $c$:
   $$\mathbf{c}_{approx} = \frac{1}{|F_c|} \sum_{f \in F_c} mf.centre$$
2. For each face $f \in F_c$ with outward normal $\mathbf{S}_{f,out}$:
   $$V_{pyr} = \frac{1}{3} (\mathbf{c}_f - \mathbf{c}_{approx}) \cdot \mathbf{S}_{f,out}$$
   $$\mathbf{c}_{pyr} = 0.75 \cdot \mathbf{c}_f + 0.25 \cdot \mathbf{c}_{approx}$$
   $$cell\_vols[c] \mathrel{+}= V_{pyr}$$
   $$cell\_cents[c] \mathrel{+}= V_{pyr} \cdot \mathbf{c}_{pyr}$$
3. Final normalization:
   $$cell\_cents[c] = \frac{cell\_cents[c]}{cell\_vols[c]}$$

#### Mathematical Proof of Exact Translation Invariance:
Let coordinates undergo rigid translation $\mathbf{x} \to \mathbf{x} + \mathbf{T}$.
Then $\mathbf{c}_f \to \mathbf{c}_f + \mathbf{T}$ and $\mathbf{c}_{approx} \to \mathbf{c}_{approx} + \mathbf{T}$.
1. Pyramid volume:
   $$(\mathbf{c}_f' - \mathbf{c}_{approx}') \cdot \mathbf{S}_{f,out} = \big((\mathbf{c}_f + \mathbf{T}) - (\mathbf{c}_{approx} + \mathbf{T})\big) \cdot \mathbf{S}_{f,out} = (\mathbf{c}_f - \mathbf{c}_{approx}) \cdot \mathbf{S}_{f,out} = V_{pyr} \quad (\text{strictly invariant!})$$
2. Pyramid centroid:
   $$\mathbf{c}_{pyr}' = 0.75 (\mathbf{c}_f + \mathbf{T}) + 0.25 (\mathbf{c}_{approx} + \mathbf{T}) = \mathbf{c}_{pyr} + \mathbf{T}$$
3. Cell centroid:
   $$\mathbf{C}_c' = \frac{\sum V_{pyr} (\mathbf{c}_{pyr} + \mathbf{T})}{\sum V_{pyr}} = \frac{\sum V_{pyr} \mathbf{c}_{pyr}}{V_c} + \mathbf{T} = \mathbf{C}_c + \mathbf{T} \quad (\text{strictly equivariant!})$$

- For $[0, 1]^3$: $\mathbf{c}_{approx} = (0.5, 0.5, 0.5)$, $V_c = 1.0$, $\mathbf{C}_c = (0.5, 0.5, 0.5)$ exactly.
- For $[10, 11]^3$: $\mathbf{c}_{approx} = (10.5, 10.5, 10.5)$, $V_c = 1.0$, $\mathbf{C}_c = (10.5, 10.5, 10.5)$ exactly.

---

## 4. Requirement R3.1-2: Small-Cell Agglomeration and Cell Weighting

### 4.1 Small-Cell Agglomeration Mechanics (`octree_hexcore.cpp:742-847`)

When cell $c$ has volume fraction $\alpha = V_c / V_{bg} < \text{options.small\_cell\_ratio}$ (0.20):
It is agglomerated into neighbor $best\_nb$ along $\nabla \phi$.

#### Required Actions:
1. **Target Cell Type**: Set `cell_types[best_nb] = CellType::POLYHEDRON` (42).
2. **Vertex Set Union**: Union all vertices of cell $c$ into `cell_nodes` of $best\_nb$.
3. **Face Re-assignment & Shared Interface Removal**:
   - The face shared between $c$ and $best\_nb$ is neutralized: `mf.owner = -1; mf.neighbour = -1;`.
   - Any face bordering $c$ has its owner or neighbour updated to $best\_nb$.

#### Deduplication of Internal Faces:
If both $c$ and $best\_nb$ previously shared a boundary with a common neighbor $D$:
After merging, there will be two internal faces with owners/neighbours $\{best\_nb, D\}$.
In OpenFOAM `polyMesh`, duplicate faces between the same cell pair produce fatal checkMesh errors.
Step 12 must merge and deduplicate internal faces:
1. Ensure upper-triangular ordering: `if (mf.owner > mf.neighbour) { std::swap(mf.owner, mf.neighbour); mf.normal_owner = -mf.normal_owner; std::reverse(mf.nodes.begin(), mf.nodes.end()); }`.
2. Group faces by key `(mf.owner, mf.neighbour)`.
3. If multiple faces exist for key $(u, v)$:
   - Sum area normal vectors: $\mathbf{S}_{\text{merged}} = \sum_k \mathbf{S}_k$.
   - Area-weighted centroid: $\mathbf{c}_{\text{merged}} = \frac{\sum_k A_k \mathbf{c}_k}{\sum_k A_k}$.
   - Union boundary edges: cancel shared internal directed edges to extract the perimeter node cycle.

---

### 4.2 Cell Weighting: `mesh.omega_cell` (`octree_hexcore.cpp:861, 1020`)

In `octree_hexcore.cpp:861`:
`mesh.omega_cell.assign(final_n_cells, 1.0);`
And it is never computed.

In `difvm_mesh.cpp:666-677`:
```cpp
std::vector<double> min_beta_per_cell(n_cells, 1.0);
for (int64_t f = 0; f < n_faces; ++f) {
    int64_t o = mesh.owner[f];
    int64_t nb = mesh.neighbour[f];
    min_beta_per_cell[o] = std::min(min_beta_per_cell[o], mesh.beta_upwind[f]);
    if (nb >= 0) {
        min_beta_per_cell[nb] = std::min(min_beta_per_cell[nb], mesh.beta_upwind[f]);
    }
}
for (int64_t c = 0; c < n_cells; ++c) {
    mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c]);
}
```
This loop must be inserted in `octree_hexcore.cpp` immediately after `mesh.beta_upwind[f]` is populated for all faces (around line 974).

---

## 5. Requirement R5.2-3: Native DistMesh Boundary Projection & Cell Filtering

### 5.1 Interior Node Push-Out Projection (`distmesh.cpp:734-739`)

In `distmesh.cpp:734-739`:
```cpp
// Move only interior nodes
for (size_t i = n_fixed; i < all_pts.size(); ++i) {
  Vec2 new_pos = all_pts[i] + forces[i] * options.delta_t;
  if (pointInPoly(new_pos)) {
    all_pts[i] = new_pos;
  }
}
```
When `pointInPoly(new_pos)` is false, `all_pts[i]` is not updated.
Per Persson & Strang (2004), interior points pushed outside the boundary must be orthogonally projected back onto the nearest boundary polygon segment.

#### Orthogonal Projection Algorithm:
For point $\mathbf{p} = new\_pos$ and polygon segments $[p_j, p_i]$ ($j = i-1$):
$$\mathbf{ab} = p_i - p_j, \quad \mathbf{ap} = \mathbf{p} - p_j$$
$$t = \text{std::clamp}\left( \frac{\mathbf{ap} \cdot \mathbf{ab}}{\|\mathbf{ab}\|^2}, 0.0, 1.0 \right)$$
$$\mathbf{proj} = p_j + t \cdot \mathbf{ab}$$
Find segment with minimum Euclidean distance $\|\mathbf{p} - \mathbf{proj}\|$, and set:
$$all\_pts[i] = \mathbf{proj}$$

---

### 5.2 3D Cell Filtering vs. Surface Triangles in `distmesh.cpp:239-388`

`DistMeshEngine::generateFromSurface` currently casts `surf_triangles` to `(void)` and builds an unfiltered Cartesian grid over the bounding box planes.

#### Two Implementation Paths:
1. **Direct `MeshSDF` Cell Filtering**:
   - Construct `MeshSDF sdf(surf_points, n_points, surf_triangles, n_triangles);`.
   - For each generated hex cell $c$, compute centroid $\mathbf{c}_k = \frac{1}{8}\sum_{v} \mathbf{p}_v$.
   - Evaluate signed distance $\phi(\mathbf{c}_k)$.
   - Keep only cells where $\phi(\mathbf{c}_k) < 0$ (solid domain) or $\phi(\mathbf{c}_k) > 0$ (fluid domain).
   - Re-index vertices and invoke `DiFVMTraversal::buildTopology(mesh, active_nodes, active_cells)`.
2. **Delegation to `OctreeHexcore` (`domain_mode = 2`)**:
   - `OctreeHexcore` is already fully integrated with adaptive 2:1 balancing, cut-cell snapping, boundary patches, and polyhedral transitions.
   - Calling `OctreeHexcore::generate` with `domain_mode = 2` (`solid_only`) directly fulfills all physical and topological constraints.

---

## 6. Literature & Architectural References

1. **Hu, Schneider, Panozzo et al. (2020)**: *Fast Tetrahedral Meshing in the Wild* (fTetWild, ACM TOG 39(4)). Scale- and rotation-invariant 3D AMIPS energy metric and non-inversion line-search.
2. **Berger & Giuliani (2024)**: *A New Provably Stable Weighted State Redistribution Algorithm* (SIAM J. Sci. Comput., arXiv:2308.16332v2). Continuous conservative weighting for cut-cells.
3. **Persson & Strang (2004)**: *A Simple Mesh Generator in MATLAB* (SIAM Review 46(2)). Point filtering with $d < -0.05 h$, boundary Newton projection, and segment orthogonal projection.
4. **Du, Li, Wang et al. (2026)**: *DiFVM: A Vectorized Graph-Based Finite Volume Solver* (arXiv:2603.15920v1). Over-relaxed non-orthogonality correction and upwind blending.
5. **Aliaga, Zore et al. (2026)**: *ANSYS Mosaic Poly-Hexcore Mesh Adaptation* (AIAA Journal of Aircraft). Conformal Mosaic polyhedral transition layers without hanging nodes.

---

## 7. Concrete Code Diff Sketches for Implementers

### Patch Sketch 1: R1 Snapping & AMIPS in `octree_hexcore.cpp`
```cpp
// In octree_hexcore.cpp line 644
for (int64_t cell_idx : node_to_cells[v]) {
  const OctreeNode& onode = nodes[active_leaves[cell_idx]];
  int cx = (pos.x >= onode.center.x) ? 1 : 0;
  int cy = (pos.y >= onode.center.y) ? 1 : 0;
  int cz = (pos.z >= onode.center.z) ? 1 : 0;
  
  Vec3 v_corner(cx == 0 ? onode.bmin.x : onode.bmax.x,
                cy == 0 ? onode.bmin.y : onode.bmax.y,
                cz == 0 ? onode.bmin.z : onode.bmax.z);
  Vec3 vx = v_corner + Vec3((1 - 2 * cx) * onode.size.x, 0, 0);
  Vec3 vy = v_corner + Vec3(0, (1 - 2 * cy) * onode.size.y, 0);
  Vec3 vz = v_corner + Vec3(0, 0, (1 - 2 * cz) * onode.size.z);
  
  int s = (1 - 2 * cx) * (1 - 2 * cy) * (1 - 2 * cz);
  Vec3 v1 = (s > 0) ? vx : vy;
  Vec3 v2 = (s > 0) ? vy : vx;
  Vec3 v3 = vz;
  
  double tet_energy = computeTetAMIPS(test_pos, v1, v2, v3);
  if (tet_energy > options.amips_max) {
    valid = false;
    break;
  }
}
```

### Patch Sketch 2: R2 Pyramid Decomposition in `octree_hexcore.cpp`
```cpp
// Step 10: Compute Cell Volumes & Centroids via reference-point pyramid decomposition
for (int64_t c = 0; c < n_active_cells; ++c) {
  Vec3 c_approx(0, 0, 0);
  int count = 0;
  for (int64_t f_idx : cell_to_faces[c]) {
    c_approx += mosaic_faces[f_idx].centre;
    count++;
  }
  if (count > 0) c_approx /= static_cast<double>(count);

  double vol_sum = 0.0;
  Vec3 cent_sum(0, 0, 0);

  for (int64_t f_idx : cell_to_faces[c]) {
    const auto& mf = mosaic_faces[f_idx];
    double sign = (mf.owner == c) ? 1.0 : -1.0;
    Vec3 sf_out = mf.normal_owner * sign;
    double pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0;
    vol_sum += pyr_vol;
    Vec3 pyr_c = mf.centre * 0.75 + c_approx * 0.25;
    cent_sum += pyr_c * pyr_vol;
  }

  cell_vols[c] = vol_sum;
  if (vol_sum > 1e-15) {
    cell_cents[c] = cent_sum / vol_sum;
  } else {
    cell_cents[c] = c_approx;
  }
}
```

### Patch Sketch 3: R3.2 `omega_cell` in `octree_hexcore.cpp`
```cpp
// After face metrics loop (line 974):
std::vector<double> min_beta_per_cell(final_n_cells, 1.0);
for (int64_t f = 0; f < total_faces; ++f) {
  int64_t o = mesh.owner[f];
  int64_t nb = mesh.neighbour[f];
  min_beta_per_cell[o] = std::min(min_beta_per_cell[o], mesh.beta_upwind[f]);
  if (nb >= 0) {
    min_beta_per_cell[nb] = std::min(min_beta_per_cell[nb], mesh.beta_upwind[f]);
  }
}
for (int64_t c = 0; c < final_n_cells; ++c) {
  mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c]);
}
```

### Patch Sketch 4: R5.2 DistMesh Boundary Push-Out in `distmesh.cpp`
```cpp
// In distmesh.cpp line 734:
for (size_t i = n_fixed; i < all_pts.size(); ++i) {
  Vec2 new_pos = all_pts[i] + forces[i] * options.delta_t;
  if (pointInPoly(new_pos)) {
    all_pts[i] = new_pos;
  } else {
    // Project orthogonally onto nearest polygon segment
    double min_d2 = 1e30;
    Vec2 best_proj = all_pts[i];
    for (size_t j = 0, k = p_nodes.size() - 1; j < p_nodes.size(); k = j++) {
      Vec2 ab = p_nodes[j] - p_nodes[k];
      Vec2 ap = new_pos - p_nodes[k];
      double ab2 = ab.norm2();
      double t = (ab2 > 1e-15) ? std::clamp((ap.x * ab.x + ap.y * ab.y) / ab2, 0.0, 1.0) : 0.0;
      Vec2 proj = p_nodes[k] + ab * t;
      double d2 = (new_pos - proj).norm2();
      if (d2 < min_d2) {
        min_d2 = d2;
        best_proj = proj;
      }
    }
    all_pts[i] = best_proj;
  }
}
```
