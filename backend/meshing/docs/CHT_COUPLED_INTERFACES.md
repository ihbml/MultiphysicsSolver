# Conjugate Heat Transfer (CHT) Multi-Region & Rotational Cyclic Periodic Symmetry

## 1. Conjugate Heat Transfer (CHT) Multi-Region Architecture

Industrial CFD often involves adjacent fluid and solid domains (e.g. turbine blade metal $\Omega_S$ and cooling fluid $\Omega_F$).

### 1.1 Domain Partitioning & Cell Tagging
- Every cell $c$ is assigned an integer `region_id`:
  - `cell_regions[c] = 0` (Fluid $\Omega_F$)
  - `cell_regions[c] = 1` (Solid $\Omega_S$)
- Stored directly in `UnstructuredMesh.cell_regions` (int32 array) and native C-ABI pointer `cfd_mesher_get_cell_regions_ptr`.

### 1.2 Coupled Interface Detection ($\Gamma_{FS}$)
The coupled interface is the set of internal faces separating differing regions:
$$\Gamma_{FS} = \{ f \in \text{internal} \mid \text{region}(\text{owner}[f]) \ne \text{region}(\text{neighbour}[f]) \}$$

### 1.3 Conformal Twin-Faces (`coupledWall` / `mappedWall`)
In OpenFOAM polyMesh / CHT convention, coupled region boundaries are represented as twin patches:
- Fluid patch: `coupledWall_fluid` with face normal $\mathbf{S}_{f, F}$ pointing into solid.
- Solid patch: `coupledWall_solid` with face normal $\mathbf{S}_{f, S} = -\mathbf{S}_{f, F}$ pointing into fluid.
- Direct 1-to-1 face coupling enforces temperature and heat flux continuity:
  $$T_F = T_S, \quad -k_F \nabla T_F \cdot \mathbf{n} = -k_S \nabla T_S \cdot \mathbf{n}$$

### 1.4 Non-Conformal Arbitrary Mesh Interface (AMI)
For non-matching grids across $\Gamma_{FS}$, the mesher computes conservative polygon clipping intersection areas $A_{ij} = |\text{face}_i^F \cap \text{face}_j^S|$:
$$w_{ij} = \frac{A_{ij}}{A_i^F}, \quad \sum_j w_{ij} = 1.0$$
Guarantees exact global thermal conservation: $\sum_i Q_i = \sum_j Q_j$.

---

## 2. Rotational Cyclic Periodic Symmetry (Feature 22)

For axisymmetric turbomachinery cascades (such as `BladeFullRev1CFD`), meshing a single blade passage saves up to 95% of grid cells.

### 2.1 Sector Angle & Rotation Matrix
Given blade count $Z$ (e.g. $Z = 36$ blades), sector angle is $\theta = \frac{2\pi}{Z}$ (e.g. $10^\circ$).
Rotation matrix around the Z-axis:
$$\mathbf{R}_z(\theta) = \begin{bmatrix} \cos\theta & -\sin\theta & 0 \\ \sin\theta & \cos\theta & 0 \\ 0 & 0 & 1 \end{bmatrix}$$

### 2.2 Point, Area Normal & Flux Transformations
For any point $\mathbf{x}_{\text{source}}$ on the master cyclic patch:
$$\mathbf{x}_{\text{target}} = \mathbf{R}_z(\theta) \, \mathbf{x}_{\text{source}}$$
Area normal vector transformation:
$$\mathbf{S}_{f, \text{target}} = -\mathbf{R}_z(\theta) \, \mathbf{S}_{f, \text{source}}$$
Velocity vector transformation:
$$\mathbf{u}_{\text{target}} = \mathbf{R}_z(\theta) \, \mathbf{u}_{\text{source}}$$

### 2.3 Periodic Pairing Algorithm
- Detect boundary faces located on sector bounding planes $\theta_0$ and $\theta_0 + \frac{2\pi}{Z}$.
- Rotate master faces by $\mathbf{R}_z(\theta)$ and locate closest target face via spatial KDTree ($\|\mathbf{R}_z(\theta) \mathbf{c}_{f, \text{src}} - \mathbf{c}_{f, \text{tgt}}\| < 10^{-5}$).
- Store periodic face pairs in `UnstructuredMesh.cyclic_pairs`.
