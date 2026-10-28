# DiFVM Graph-Topology & Polyhedral Structure of Arrays (SoA) Specification

## 1. Mathematical Foundations & Collocated Finite Volume Graph

In collocated Finite Volume Method (FVM) formulations for differentiable CFD (Du et al., 2026 DiFVM; Jasak, 1996), all primary flow variables (pressure $p$, velocity $\mathbf{u}$, temperature $T$) are stored at cell centroids $\mathbf{C}_i$. The underlying computational grid is represented as an unstructured directed graph stored in flat, vectorized Structure of Arrays (SoA) layout.

### 1.1 Fundamental Topological Arrays
For a mesh containing $N_c$ cells and $N_f$ unique faces:
- `owner`: Array of length $N_f$, storing the owner cell index $O = \text{owner}[f] \in [0, N_c - 1]$.
- `neighbour`: Array of length $N_f$, storing the neighbour cell index $N = \text{neighbour}[f]$.
  - Internal faces: $N \ge 0$, with $O < N$.
  - Boundary faces: $N = -1$.
- `face_areas` ($\mathbf{S}_f$): Array of shape $(N_f, 3)$, outward area normal vector pointing from owner to neighbour.
- `face_centres` ($\mathbf{c}_f$): Array of shape $(N_f, 3)$, geometric centroid of face $f$.
- `cell_centres` ($\mathbf{C}_i$): Array of shape $(N_c, 3)$, geometric centroid of cell $i$.
- `cell_volumes` ($V_i$): Array of shape $(N_c,)$, strictly positive volume $V_i > 0$.

### 1.2 Orientation Factor & Closed-Cell Divergence
For each cell $i$, the orientation factor for face $f \in \mathcal{F}(i)$ is:
$$\sigma_{if} = \begin{cases} +1 & \text{if } i = \text{owner}[f] \\ -1 & \text{if } i = \text{neighbour}[f] \end{cases}$$

By the Gauss divergence theorem applied to a constant vector field $\mathbf{u} = \mathbf{c}$, every closed polyhedral cell must satisfy the exact vector closure identity:
$$\sum_{f \in \mathcal{F}(i)} \sigma_{if} \mathbf{S}_f \equiv \mathbf{0}$$

**Acceptance Criterion Tolerance**:
$$\max_{i \in [0, N_c - 1]} \left\| \sum_{f \in \mathcal{F}(i)} \sigma_{if} \mathbf{S}_f \right\| < 10^{-11} \, \text{m}^2$$

---

## 2. Polyhedral Volume & Centroid Integration via Divergence Theorem

To compute the volume and centroid of an arbitrary polyhedral cell with $N_v$ vertices and non-planar polygon faces:

1. **Preliminary Centroid Approximation**:
   $$\mathbf{c}_{\text{approx}} = \frac{1}{N_v} \sum_{v \in \text{cell}} \mathbf{x}_v$$

2. **Face Centroid & Normal Vector via Triangle Fan**:
   For face $f = (v_0, v_1, \dots, v_{n-1})$:
   $$\mathbf{S}_f = \frac{1}{2} \sum_{k=1}^{n-2} (\mathbf{x}_{v_k} - \mathbf{x}_{v_0}) \times (\mathbf{x}_{v_{k+1}} - \mathbf{x}_{v_0})$$
   $$\mathbf{c}_f = \frac{1}{A_f} \sum_{k=1}^{n-2} A_{f,k} \, \frac{\mathbf{x}_{v_0} + \mathbf{x}_{v_k} + \mathbf{x}_{v_{k+1}}}{3}, \quad A_f = \|\mathbf{S}_f\|$$

3. **Pyramid Decomposition Volume**:
   Decompose the cell into pyramids whose bases are the cell faces and whose common apex is $\mathbf{c}_{\text{approx}}$:
   $$V_i = \frac{1}{3} \sum_{f \in \mathcal{F}(i)} \sigma_{if} \, \left[ (\mathbf{c}_f - \mathbf{c}_{\text{approx}}) \cdot \mathbf{S}_f \right] > 10^{-15}$$

4. **Exact Cell Centroid**:
   $$\mathbf{C}_i = \mathbf{c}_{\text{approx}} + \frac{1}{V_i} \sum_{f \in \mathcal{F}(i)} \sigma_{if} \, \frac{3}{4} (\mathbf{c}_f - \mathbf{c}_{\text{approx}}) \left[ \frac{1}{3} (\mathbf{c}_f - \mathbf{c}_{\text{approx}}) \cdot \mathbf{S}_f \right]$$

---

## 3. Over-Relaxed Non-Orthogonal Decomposition

In non-orthogonal unstructured grids, the vector connecting adjacent cell centroids $\mathbf{d}_f = \mathbf{C}_N - \mathbf{C}_O$ is not parallel to the face area vector $\mathbf{S}_f$. 

To discretize diffusion fluxes without matrix asymmetry, the DiFVM / OpenFOAM over-relaxed formulation decomposes $\mathbf{S}_f$ into:
$$\mathbf{S}_f = \boldsymbol{\Delta}_f + \mathbf{k}_f$$
where:
- $\boldsymbol{\Delta}_f$ is the orthogonal projection component directed along $\mathbf{d}_f$, treated implicitly in the Laplacian matrix.
- $\mathbf{k}_f$ is the non-orthogonal correction vector, evaluated explicitly via deferred correction loops.

### 3.1 Mathematical Derivation
The over-relaxed projection enforces:
$$\boldsymbol{\Delta}_f \cdot \mathbf{S}_f = \|\mathbf{S}_f\|^2$$
Since $\boldsymbol{\Delta}_f$ is collinear with $\mathbf{d}_f$, $\boldsymbol{\Delta}_f = \alpha \, \mathbf{d}_f$:
$$\alpha (\mathbf{d}_f \cdot \mathbf{S}_f) = \|\mathbf{S}_f\|^2 \implies \alpha = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}_f}$$
Therefore:
$$\boldsymbol{\Delta}_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{S}_f \cdot \mathbf{d}_f} \, \mathbf{d}_f$$
$$\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f$$

### 3.2 Boundary Face Formulation
On physical boundary faces where no neighbour cell exists:
$$\mathbf{d}_f = \mathbf{c}_f - \mathbf{C}_O$$
$$\boldsymbol{\Delta}_f = \mathbf{d}_f, \quad \mathbf{k}_f = \mathbf{0}$$

### 3.3 Exact Conservation Identity
$$\max_{f \in \text{internal}} \|\boldsymbol{\Delta}_f + \mathbf{k}_f - \mathbf{S}_f\| < 10^{-14}$$

---

## 4. Error-Map & 4-Tier Solver Stabilization Mask

To provide predictive quality monitoring and dynamic flux limiting for the CFD solver (PISO/SIMPLE):

| Metric | Mathematical Formula | Physical Significance | Acceptance Bounds |
|---|---|---|---|
| **Non-orthogonality angle** $\theta_f$ | $\arccos\left( \frac{\mathbf{S}_f \cdot \mathbf{d}_f}{\|\mathbf{S}_f\| \|\mathbf{d}_f\|} \right) \frac{180^\circ}{\pi}$ | Angle between centroid-to-centroid line and face normal | $\theta_f \in [0^\circ, 90^\circ]$, optimal $< 40^\circ$ |
| **Skewness** $Q_{\text{skew}, f}$ | $\frac{\|\mathbf{c}_f - (\mathbf{C}_O + t \mathbf{d}_f)\|}{\|\mathbf{d}_f\|}$ | Distance between face center and centroid line intersection | $Q_{\text{skew}} \ge 0$, optimal $< 0.4$ |
| **Aspect ratio** $\text{AR}_c$ | $\frac{\max_{e \in \text{edges}(c)} l_e}{\min_{e \in \text{edges}(c)} l_e}$ | Ratio of longest to shortest cell edge | $\text{AR}_c \ge 1.0$, optimal $< 40.0$ |

### 4.1 Stabilization Tiers (`error_map["stabilization_mask"]`)
Each face is tagged with an integer flag $M_f \in \{0, 1, 2, 3\}$:
- **Tier 0 (Optimal CD):** $\theta_f < 40^\circ$ and $Q_{\text{skew}} < 0.4$. Standard central differencing without flux limiting.
- **Tier 1 (Mild Over-Relaxed):** $40^\circ \le \theta_f < 65^\circ$ or $0.4 \le Q_{\text{skew}} < 0.7$. Deferred-correction non-orthogonal loops active.
- **Tier 2 (High Flux Limiting):** $65^\circ \le \theta_f < 75^\circ$ or $0.7 \le Q_{\text{skew}} < 1.0$. Blend 20% upwind dissipation into face flux.
- **Tier 3 (Critical Upwind Clamp):** $\theta_f \ge 75^\circ$ or $Q_{\text{skew}} \ge 1.0$. Full first-order upwind fallback to guarantee diagonal dominance and prevent matrix singularity.
