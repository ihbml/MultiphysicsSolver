# Arbitrary Lagrangian-Eulerian (ALE) Moving Mesh & Degenerate Cell Detection

## 1. Theoretical Background & Scientific Foundations

In multiphysics aeroelastic and moving boundary problems (e.g. oscillating turbine blades, moving pistons, fluid-structure interaction):
- Boundaries deform dynamically: $\mathbf{x}_{\text{bnd}}(t) = \mathbf{x}_{\text{bnd}}(0) + \mathbf{d}(t)$.
- Mesh interior nodes must move with grid velocity $\mathbf{w} = \frac{\partial \mathbf{x}}{\partial t}$ smoothly without inverting cells or destroying near-wall prism orthogonality.
- Geometric conservation laws must be satisfied to prevent artificial mass and momentum sources.

This module implements the ALE moving mesh formulation and degenerate cell detection specified in `PROJECT.md` Features 15 and 16.

---

## 2. Variable-Stiffness Laplacian Smoothing (Feature 15)

### Governing PDE
The interior node displacement field $\mathbf{u}(\mathbf{x}) = \mathbf{x}^{n+1} - \mathbf{x}^n$ is governed by a variable-stiffness elliptic PDE:
$$\nabla \cdot (\gamma(\mathbf{x}) \nabla \mathbf{u}) = \mathbf{0}$$
with boundary Dirichlet conditions:
$$\mathbf{u}|_{\Gamma_{\text{moving}}} = \mathbf{d}_{\text{bnd}}(t), \quad \mathbf{u}|_{\Gamma_{\text{fixed}}} = \mathbf{0}$$

### Variable-Stiffness Field
Near physical walls, boundary layer prisms must maintain their orthogonality and thin layer spacing ($y^+ \sim 1$). In contrast, large interior unstructured cells can comfortably absorb significant deformations.
The stiffness $\gamma(\mathbf{x})$ is defined as an inverse distance/volume power law:
$$\gamma(\mathbf{x}) = \frac{1}{(d_{\text{wall}}(\mathbf{x}) + \varepsilon)^\alpha} \quad (\alpha \ge 1)$$
where $d_{\text{wall}}$ is the distance to the nearest moving or fixed wall, and $\alpha \in [1, 2]$ is the user-configurable stiffness exponent.

### Discrete Graph Formulation
On the unstructured polyhedral/hexahedral node adjacency graph $(V, E)$:
$$\sum_{j \in \mathcal{N}(i)} \gamma_{ij} (\mathbf{u}_j - \mathbf{u}_i) = \mathbf{0} \quad \forall i \in V_{\text{interior}}$$
where edge stiffness $\gamma_{ij} = \frac{1}{2}(\gamma_i + \gamma_j)$.

The resulting sparse symmetric positive-definite (SPD) system is solved via `scipy.sparse.linalg.spsolve` component by component ($x, y, z$).

### Post-Deformation Topological Update
After node coordinates are updated ($\mathbf{x}^{n+1} = \mathbf{x}^n + \mathbf{u}$):
- Full DiFVM SoA graph arrays (`owner`, `neighbour`, `face_areas`, `face_centres`, `cell_centres`, `cell_volumes`, `delta_f`, `k_f`, `error_map`) are recomputed via `compute_difvm_topology`.
- Cell volume positivity is strictly validated: $V_i > 10^{-15} \quad \forall i$.
- Fixed boundary stationary condition is verified: $\|\mathbf{u}|_{\Gamma_{\text{fixed}}}\| < 10^{-12}$.

In `ALEMeshMover`:
```python
mover = ALEMeshMover(stiffness_exponent=1.0)
moved_mesh = mover.deform_mesh(mesh, boundary_displacement)
```

---

## 3. Degenerate Cell Detection & Remeshing Trigger (Feature 16)

During large boundary motions or high-frequency vibrations, cells can invert, collapse, or suffer severe skewness. The `detect_degenerate_cells` engine performs comprehensive audits against 6 failure criteria:

1. **Inverted / Collapsed Cell Volume**:
   $$V_c \le 10^{-15} \quad \text{or non-finite } (\text{NaN} / \text{Inf})$$
2. **Severe Face Non-Orthogonality**:
   $$\theta_f > 85^\circ$$
   where $\cos \theta_f = \frac{\mathbf{S}_f \cdot \mathbf{d}}{\|\mathbf{S}_f\| \|\mathbf{d}\|}$.
3. **Severe Face Skewness**:
   $$\text{skew}_f > 0.85$$
   where $\text{skew}_f = \frac{\|\mathbf{x}_f - \mathbf{x}'\|}{\|\mathbf{d}\|}$.
4. **Critical Solver Stabilization Mask**:
   $$\text{mask}_f == 3$$
5. **High Cell Aspect Ratio**:
   $$\text{AR}_c > 40.0$$
6. **Non-Finite Centroid Coordinates**:
   $$\mathbf{x}_c \text{ contains NaN or Inf}$$

### Usage
```python
mover = ALEMeshMover()
bad_cells = mover.detect_degenerate_cells(mesh)
if len(bad_cells) > 0:
    # Trigger local cavity remeshing or global DistMesh re-generation
    print(f"Triggering remesh for {len(bad_cells)} degenerate cells: {bad_cells}")
```

---

## 4. Discrete Geometric Conservation Law (DGCL)

When moving mesh boundaries, the volume change must strictly match the swept face volume:
$$\frac{V_c^{n+1} - V_c^n}{\Delta t} = \sum_{f \in \text{faces}(c)} \sigma_{cf} \mathbf{w}_f \cdot \mathbf{S}_f^{n+1/2}$$
where $\mathbf{w}_f = \frac{\mathbf{x}_f^{n+1} - \mathbf{x}_f^n}{\Delta t}$ and $\mathbf{S}_f^{n+1/2} = \frac{1}{2}(\mathbf{S}_f^n + \mathbf{S}_f^{n+1})$.

The method `verify_dgcl(mesh_n, mesh_n1, dt)` verifies this identity to machine precision ($< 10^{-12}$).
