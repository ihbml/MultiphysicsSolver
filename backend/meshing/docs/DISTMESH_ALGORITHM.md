# DistMesh Core, Boundary Inflation & Feature 23 Pinching / Termination

## 1. DistMesh Force Equilibrium Algorithm

Based on Persson & Strang (2004) and modernized for 2D/3D CFD domains:
DistMesh models the edges of a Delaunay triangulation as a mechanical truss system where edge lengths are governed by non-linear repulsive spring forces.

### 1.1 Seeding & Rejection Sampling
1. **Background Equilateral Lattice**:
   Candidate nodes are placed on an equilateral triangular lattice:
   $$\Delta y = h_0 \frac{\sqrt{3}}{2}, \quad \Delta x = h_0, \quad x_{i,j} = x_0 + i h_0 + (j \bmod 2)\frac{h_0}{2}$$
2. **Rejection Thinning**:
   Nodes outside the domain are filtered: keep if signed distance $\phi(\mathbf{x}) \le 0.05 h(\mathbf{x})$.
   To match target local sizing $h(\mathbf{x})$, points are retained with probability:
   $$P(\text{keep}) = \frac{r(\mathbf{x})}{r_{\max}}, \quad r(\mathbf{x}) = \frac{1}{h(\mathbf{x})^2}$$

### 1.2 Truss Force Equilibrium
For every edge $e = (i, j)$ connecting nodes $\mathbf{x}_i$ and $\mathbf{x}_j$:
- Current length: $l_e = \|\mathbf{x}_i - \mathbf{x}_j\|$
- Target length: $h_e = h\left(\frac{\mathbf{x}_i + \mathbf{x}_j}{2}\right)$
- Global equilibrium scaling factor:
  $$s_0 = 1.2 \sqrt{\frac{\sum_e l_e^2}{\sum_e h_e^2}}$$
- Repulsive spring force:
  $$F_e = \max(s_0 h_e - l_e, 0)$$

### 1.3 Forward Euler Node Motion & Level-Set Projection
At each iteration:
$$\mathbf{F}_i = \sum_{j \in \text{adj}(i)} F_{ij} \frac{\mathbf{x}_i - \mathbf{x}_j}{l_{ij}}$$
$$\mathbf{x}_i \leftarrow \mathbf{x}_i + \Delta t \, \mathbf{F}_i, \quad \Delta t = 0.20$$

If an interior node crosses the domain boundary ($\phi(\mathbf{x}_i) > 0$), it is projected back onto the zero level set along the normalized gradient:
$$\mathbf{x}_i \leftarrow \mathbf{x}_i - \phi(\mathbf{x}_i) \frac{\nabla \phi(\mathbf{x}_i)}{\|\nabla \phi(\mathbf{x}_i)\|^2}$$

---

## 2. Boundary Layer Inflation & Feature 23 Pinching / Termination

### 2.1 Prismatic Boundary Layer Extrusion
To resolve viscous sublayers for turbulent boundary layers at $y^+ \sim 1$:
- First layer height $h_1$
- Growth expansion ratio $r \in [1.1, 1.3]$
- Layer thickness for layer $k$:
  $$h_k = h_1 r^{k-1}$$
- Cumulative normal distance to layer $k$ interface:
  $$\delta_k = \sum_{l=1}^k h_l = h_1 \frac{r^k - 1}{r - 1}, \quad k \in \{1, \dots, N_{\text{layers}}\}$$

### 2.2 Feature 23 Clearance-Based Pinching
In sharp corners, narrow gaps, and blade trailing edges, uniform normal extrusion produces intersecting inflation vectors, creating inverted elements with negative volume ($V_i \le 0$).

To prevent cell inversion, the inflation distance is modulated by a local pinching factor $\kappa_i \in [0.1, 1.0]$:
$$\kappa_i = \tanh\left(\frac{d_{\text{clearance}}(\mathbf{x}_i)}{2 \delta_N}\right) \cdot \left[ 1.0 - \left(\frac{\theta_{\text{corner}}}{\pi}\right)^2 \right]$$
$$\delta_{i, k}^{\text{pinched}} = \delta_k \cdot \kappa_i$$
where:
- $d_{\text{clearance}}(\mathbf{x}_i)$ is the ray-marched distance to the opposing boundary.
- $\theta_{\text{corner}}$ is the corner turn angle between sharing wall facets.

### 2.3 Feature 23 Layer Termination
When clearance is critically narrow ($d_{\text{clearance}} < 2 h_1$) or $\theta_{\text{corner}} > 140^\circ$:
- The boundary layer terminates smoothly ($N_{\text{layers}} \to 0$).
- Transition triangles/wedges connect the truncated quad layer to the unstructured interior.
- Strictly guarantees positive volumes $V_i > 10^{-15}$ across all elements.

---

## 3. Reverse Cuthill-McKee (RCM) Bandwidth Reduction

To optimize CPU L1/L2 cache hit rates and vector gather operations during DiFVM matrix-vector products:
1. **Adjacency Graph**: Nodes are cells $c \in [0, N_c - 1]$, edges are internal faces connecting owner and neighbour.
2. **Initial Bandwidth**:
   $$\beta_0 = \max_{f \in \text{internal}} |\text{owner}[f] - \text{neighbour}[f]|$$
3. **Traversal**:
   - Locate pseudo-peripheral node $s$ with minimal degree $\deg(s) = \min_c \text{deg}(c)$.
   - Perform BFS from $s$, sorting neighbors at each level in ascending order of degree.
   - Form Cuthill-McKee order $P_{\text{cm}}$.
4. **Reverse Permutation**:
   $$P_{\text{rcm}}(k) = P_{\text{cm}}(N_c - 1 - k)$$
5. **Bandwidth Reduction Target**:
   $$\frac{\beta_0 - \beta_{\text{rcm}}}{\beta_0} \ge 30\%$$
