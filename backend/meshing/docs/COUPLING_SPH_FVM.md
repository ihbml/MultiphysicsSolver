# Eulerian SPH-FVM Interface Coupling & OPMIS Taylor Interpolation

## 1. Theoretical Background & Scientific Foundations

In multiphysics fluid simulations, hybrid coupling between Eulerian Smoothed Particle Hydrodynamics (SPH) and Finite Volume Method (FVM) leverages the strengths of both methods:
1. **Eulerian SPH** accurately captures violent free surfaces, large topological changes, droplet breakups, and phase interfaces without grid entanglement.
2. **FVM (DiFVM / OpenFOAM)** provides high computational efficiency and strict conservation in boundary layers and bulk flow regions.

This module implements the mathematical formulations established in:
- **Wang, Haidn & Hu (2025)**: *Incorporating the algorithm for the boundary condition from FVM into the framework of Eulerian SPH*, Computer Physics Communications 307, 109429.
- **Zhang, Ming, Wang & Liang (2025)**: *Adaptive SPH-FVM method with an optimized particle-mesh interpolation scheme (OPMIS)*, International Journal of Multiphase Flow 189, 105256.

---

## 2. Incomplete Kernel Support & Position Divergence Detector (Feature 12)

### Mathematical Formulation
In continuous continuum mechanics, the identity:
$$\nabla \cdot \mathbf{r} = d$$
holds identically for spatial dimension $d$ ($d = 2$ in 2D, $d = 3$ in 3D).

In discrete SPH with particle positions $\mathbf{r}_i$, volumes $V_j$, and smoothing length $h$, the discrete divergence of position is defined as:
$$\text{div }\mathbf{r}_i = -\sum_{j \in \mathcal{N}(i)} V_j (\mathbf{r}_i - \mathbf{r}_j) \cdot \nabla_i W_{ij}$$

### Continuum Limit
Using integration by parts over domain $\Omega$ with outward boundary normal $\mathbf{n}$:
$$\int_{\Omega} \nabla \cdot (\mathbf{x} W) dV = \int_{\Omega} (\nabla \cdot \mathbf{x}) W dV + \int_{\Omega} \mathbf{x} \cdot \nabla W dV$$
For an interior particle whose compact support $\text{supp}(W) \subset \Omega$, the boundary integral $\oint_{\partial \Omega} (\mathbf{x} \cdot \mathbf{n}) W dS = 0$, yielding:
$$-\int_{\Omega} \mathbf{x} \cdot \nabla W dV = d \int_{\Omega} W dV = d$$

### Physical Wall Detection Criterion
For a particle located directly on a flat physical wall, exactly half of the kernel support sphere is truncated outside the fluid domain:
$$\text{div }\mathbf{r}_{\text{wall}} \approx 0.5 \cdot d$$
Wang, Haidn & Hu (2025) derived the robust truncated support detector:
$$\text{div }\mathbf{r}_i < 0.75 \cdot d \implies \text{Particle } i \text{ is on a physical boundary}$$
- In 2D: $\text{div }\mathbf{r}_i < 1.50$
- In 3D: $\text{div }\mathbf{r}_i < 2.25$

In `SPHFVMCoupler`:
```python
coupler = SPHFVMCoupler(kernel="wendland", support_factor=2.85)
is_boundary = coupler.detect_boundary_particles(pos, vol, h)
```

---

## 3. Virtual FVM Wall Boundary Area Vector $\mathbf{A}_{iS}$ (Feature 13)

### 0th-Order Consistency Violation Near Walls
For interior particles with symmetric, unperturbed support:
$$\sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij} \approx \mathbf{0}$$
Near physical walls, truncated support creates a large non-zero residual $\sum_j V_j \nabla_i W_{ij} \neq \mathbf{0}$, which severely degrades zeroth-order consistency and generates unphysical wall repulsion.

### Virtual Wall Closure Formulation
Wang, Haidn & Hu (2025) established the virtual boundary closing vector:
$$\mathbf{A}_{iS} = -\sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij} \quad (\text{for } i \in \text{boundary})$$
$$\mathbf{A}_{iS} = \mathbf{0} \quad (\text{for } i \in \text{interior})$$

### Physical and Numerical Invariant
This vector acts exactly as the virtual FVM wall boundary face:
1. **Exact 0th-order consistency**:
   $$\|\mathbf{A}_{iS} + \sum_{j \in \mathcal{N}(i)} V_j \nabla_i W_{ij}\| < 10^{-10}$$
2. **Equivalent wall area**: $S_{i, \text{wall}} = \|\mathbf{A}_{iS}\|$
3. **Outward unit normal**: $\mathbf{n}_{iS} = \frac{\mathbf{A}_{iS}}{\|\mathbf{A}_{iS}\|}$
4. **Wall perimeter/surface sum**: $\sum_{i \in \text{bnd}} \|\mathbf{A}_{iS}\| \approx A_{\text{wall, exact}}$

In `SPHFVMCoupler`:
```python
A_iS, sum_grad_W = coupler.compute_virtual_wall_vector(pos, vol, h, return_sum_grad=True)
```

---

## 4. OPMIS Moment-Reproducing Taylor Interpolation (Feature 14)

### 4.1 Particle-to-Mesh (P2M) Transfer
For each particle $i$, the spatial gradient $\nabla \phi_i$ is evaluated using the local moment tensor:
$$\mathbf{M}_{p, i} = \sum_{j \in \mathcal{N}(i)} w_{ij} (\mathbf{x}_j - \mathbf{x}_i) \otimes (\mathbf{x}_j - \mathbf{x}_i)$$
$$\nabla \phi_i = \mathbf{M}_{p, i}^{-1} \sum_{j \in \mathcal{N}(i)} w_{ij} (\mathbf{x}_j - \mathbf{x}_i) (\phi_j - \phi_i)$$

At each FVM mesh cell centroid $\mathbf{x}_c$, the field is reconstructed via Taylor expansion from surrounding particles $\mathcal{P}_c$:
$$\phi(\mathbf{x}_c) = \frac{1}{|\mathcal{P}_c|} \sum_{i \in \mathcal{P}_c} \left[ \phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i) \right]$$

#### Proof of Exact Linear Reproduction
Let $\phi(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$.
Then $\phi_j - \phi_i = \mathbf{a} \cdot (\mathbf{x}_j - \mathbf{x}_i)$.
$$\sum_j w_{ij} (\mathbf{x}_j - \mathbf{x}_i) (\phi_j - \phi_i) = \left[ \sum_j w_{ij} (\mathbf{x}_j - \mathbf{x}_i) \otimes (\mathbf{x}_j - \mathbf{x}_i) \right] \mathbf{a} = \mathbf{M}_{p, i} \mathbf{a}$$
$$\nabla \phi_i = \mathbf{M}_{p, i}^{-1} \mathbf{M}_{p, i} \mathbf{a} = \mathbf{a}$$
Substituting into the Taylor expansion for each particle:
$$\phi_i + \nabla \phi_i \cdot (\mathbf{x}_c - \mathbf{x}_i) = (\mathbf{a} \cdot \mathbf{x}_i + b) + \mathbf{a} \cdot (\mathbf{x}_c - \mathbf{x}_i) = \mathbf{a} \cdot \mathbf{x}_c + b = \phi(\mathbf{x}_c)$$
Average over $|\mathcal{P}_c|$ is identically $\phi(\mathbf{x}_c)$ to machine precision ($L_\infty < 10^{-11}$).

### 4.2 Mesh-to-Particle (M2P) Transfer
For each query particle $\mathbf{x}_p$, an affine reproducing moving least-squares polynomial is fitted to neighboring mesh cell centroids $\mathbf{x}_k$:
$$\min_{c_0, \mathbf{g}} \sum_{k \in \mathcal{M}_p} w_k \left( c_0 + \mathbf{g} \cdot (\mathbf{x}_k - \mathbf{x}_p) - \phi(\mathbf{x}_k) \right)^2$$
The constant term $c_0$ is the exact interpolated value $\phi(\mathbf{x}_p)$ reproducing arbitrary linear fields with $L_\infty < 10^{-11}$.

In `OPMISTaylorInterpolator`:
```python
interpolator = OPMISTaylorInterpolator()
# P2M
field_mesh = interpolator.interpolate_p2m(pos, field_particles, vol, h, mesh)
# M2P
field_query = interpolator.interpolate_m2p(mesh, field_mesh, query_points)
```

---

## 5. Verification Invariants & Tolerances

| Invariant | Mathematical Formula | Strict Tolerance |
|---|---|---|
| SPH 0th-Order Wall Consistency | $\|\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij}\|$ | $< 10^{-10}$ |
| Interior Virtual Wall Zero | $\|\mathbf{A}_{iS}\|$ for $\text{div }\mathbf{r} \ge 0.75 d$ | $< 10^{-6}$ |
| P2M Linear Moment Reproduction | $\|f(\mathbf{x}_c) - \text{P2M}(f)\|_\infty$ | $< 10^{-11}$ |
| M2P Affine Linear Reproduction | $\|f(\mathbf{x}_p) - \text{M2P}(f)\|_\infty$ | $< 10^{-11}$ |
