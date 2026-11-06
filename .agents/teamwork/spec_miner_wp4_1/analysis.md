# Technical & Mathematical Specification Report: Work Package 4
## 3D Poly-Hexcore, ANSYS Mosaic / PUMA Polyhedral Interface, Cut-Cell Snapping, Small-Cell Merging, Multiphysics CHT & GCI Hierarchy

**Author:** `spec_miner_wp4_1` (Teamwork Specification Miner)  
**Date:** 2026-11-04  
**Working Directory:** `G:\Datein\Projects\CFDSolver`  
**Milestone:** Work Package 4 (WP4)

---

## 1. Executive Summary & Authoritative Specification Sources

This document provides the exhaustive mathematical formulations, algorithmic steps, data structures, and architectural specifications for **Work Package 4 (Echte 3D-Fluidgebiets- & Mehrbereichs-Vernetzung / Multiphysics Domain Meshing & GCI-Hierarchie)** of the `CFDSolver` in-memory meshing subsystem.

### Authoritative Specification Sources
1. **`ORIGINAL_REQUEST.md`** (Approved Task Prompt 2026-11-03T23:55:15Z)
2. **`SUGGESTIONS_BACKEND.md`** (Sections 4.0, 4.1 K1–K8, 4.2 Bug A–Bug F, 5.1 M2.5, 5.2 M3+, Section 6)
3. **`resources/papers/literature_survey_2026.md`** (Sections 9.1 VoroCrust, 9.2 ANSYS Mosaic/PUMA, 9.3 fTetWild/AMIPS, 9.4 Berger & Giuliani 2024 WSRD, 9.7 Loseille & Löhner 2013)
4. **ASME V&V 20-2009 / Celik et al. (2008)** (Standard for Verification and Validation in Computational Fluid Dynamics and Heat Transfer: Grid Convergence Index)
5. **Existing Codebase & Reference Implementations**:
   - `backend/meshing/models.py`, `backend/meshing/difvm_topology.py`, `backend/meshing/mesh_engine.py`
   - `backend/meshing/native/src/difvm_mesh.cpp`, `cht_cyclic.cpp`, `sdf_engine.cpp`
   - `resources/reference_spickzettel/LMArena/Mesher/DiFVMAgentModeUnkonwnbutHigh/difvm_mesh/src/generate.cpp` (cap2-bug resolution and convex cut-cell clipping)

---

## 2. Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Domain Architecture | Multiphysics Domain Modes | Explicit domain meshing selector supporting external aerodynamic flows, internal piped flows, structural/thermal solids, and coupled conjugate heat transfer | `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"]` | Tagged mesh domain with corresponding cell regions and boundary patches | Invalid mode raises `ValueError` | `SUGGESTIONS_BACKEND.md` §4.1 K4, `ORIGINAL_REQUEST.md` R1 |
| 2 | Farfield Generation | Configurable Farfield Geometry | Automated generation of outer bounding domain based on reference length $L_{\text{ref}}$ padding or explicit axis-aligned bounding box (`farfield_bbox`) | `farfield_padding: Optional[Tuple[float,...]]`, `farfield_bbox: Optional[Tuple[float,...]]` | Outer domain boundary planes $\Omega_{\text{farfield}}$ | Inverted bbox ($x_{\min} \ge x_{\max}$) raises `ValueError` | `SUGGESTIONS_BACKEND.md` §5.1, `ORIGINAL_REQUEST.md` R1 |
| 3 | Patch Assignment | Automated Farfield Patch Naming | Automatic classification and naming of boundary faces on farfield box: `inlet` ($x_{\min}$), `outlet` ($x_{\max}$), `bottom` ($z_{\min}$), `top` ($z_{\max}$), `sides` ($y_{\min}, y_{\max}$) | Farfield face coordinates and normals | Named entries in `boundaries: Dict[str, Tuple[int, int]]` | Non-boundary face in patch raises `ValueError` in `validate()` | `SUGGESTIONS_BACKEND.md` §4.1 K7, `ORIGINAL_REQUEST.md` R1 |
| 4 | Patch Assignment | CAD Body Patch Inheritance | Preserves and inherits CAD surface patch names (`SurfaceMesh.patches`) from STEP/OBJ/STL onto interior body boundaries | `SurfaceMesh.patches` | Contiguous boundary patch blocks in `boundaries` | Overlapping or missing boundary faces raises `ValueError` in `validate()` | `SUGGESTIONS_BACKEND.md` §5.1 |
| 5 | Background Grid | 2:1 Balanced Octree Hexcore | Adaptive Cartesian octree construction in the fluid domain ($\phi(\mathbf{x}) > 0$) with strict 2:1 balancing between neighboring cells | CAD geometry SDF $\phi(\mathbf{x})$, nominal size $h_0$, local sizing $h(\mathbf{x})$ | Octree leaf cells with size ratio $\le 2:1$ across all interfaces | Depth overflow or empty domain raises `RuntimeError` | `SUGGESTIONS_BACKEND.md` §5.2.1, `literature_survey_2026.md` §9.2 |
| 6 | Transition Interface | Mosaic Polyhedral Transition (No Hanging Nodes) | Conformal interface replacement: when an unrefined cell borders a 2:1 split neighbor, the shared quad face is replaced by 4 coplanar sub-faces on the unrefined cell | Octree leaf cells with level difference $\Delta L = 1$ | Conformal 9-faced polyhedron (`CellType.POLYHEDRON = 42`) in DiFVM graph | Missing subface linkage raises non-manifold error | `SUGGESTIONS_BACKEND.md` §5.2.1, Aliaga et al. (2026) |
| 7 | Boundary Conformity | Cut-Cell Surface Snapping | Projects near-boundary octree nodes onto CAD zero-isolevel $\phi(\mathbf{x}) = 0$ along $\nabla \phi$ | Nodes with $|\phi(\mathbf{x})| < \epsilon_{\text{snap}}$, SDF $\phi, \nabla \phi$ | Snapped boundary vertices with $|\phi| < 10^{-12}$ | Inversion rollback if $\det \mathbf{J} \le 0$ or $V_i \le 0$ | `SUGGESTIONS_BACKEND.md` §5.2.2, Hu et al. (2020) |
| 8 | Boundary Conformity | Conformal AMIPS Energy Guard | Scale- and rotation-invariant AMIPS distortion energy monitoring during node movement to prevent inverted/sliver cells | Element Jacobian $\mathbf{J}$ w.r.t. regular reference element | $E_{\text{AMIPS}}(T) = \frac{\operatorname{tr}(\mathbf{J}^T \mathbf{J})}{(\det \mathbf{J})^{2/3}} \ge 3$ | Rejection / line-search backtracking if $E_{\text{AMIPS}} > 10^4$ | `literature_survey_2026.md` §9.3 (`fTetWild`) |
| 9 | Boundary Conformity | VoroCrust Mirrored Seed Pairing | Computes exact primal-dual orthogonal seed pairs $(g^\uparrow, g^\downarrow)$ from surface sphere triplets for zero non-orthogonality ($\mathbf{k}_f \equiv \mathbf{0}$) | Boundary triangle vertices and sphere radii | Mirrored dual generators across CAD facets | Overlapping sphere check eliminates sliver tetrahedra | `literature_survey_2026.md` §9.1 (`VoroCrust`) |
| 10 | Cut-Cell Stabilization | Small-Cell Merging ($\alpha < 0.20$) | Topologically agglomerates cut-cells with tiny volume fraction $V_{\text{cut}} < 0.20 V_{\text{bg}}$ into the adjacent fluid cell along $\nabla \phi$ | Cut-cells with $\alpha_i < 0.20$, neighbor graph | Merged closed polyhedron (`CellType.POLYHEDRON = 42`) with $V_{\text{merged}} \ge 0.20 V_{\text{bg}}$ | No fluid neighbor triggers fallback to largest face neighbor | `SUGGESTIONS_BACKEND.md` §5.2.3, Berger & Giuliani (2024) |
| 11 | Numerical Stability | Berger-Giuliani (2024) WSRD Weights | Computes provably monotone Weighted State Redistribution weights $w_{ij}$ for cut-cells with $0.20 \le \alpha_i < 0.50$ | Cut-cells with $0.20 \le \alpha_i < 0.50$ | Conservative weights $w_{ij} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ | Normalization check $\sum_j w_{ij} = 1.0$ | `literature_survey_2026.md` §9.4, Berger & Giuliani (2024) |
| 12 | Multiphysics Coupling | CHT Conformal Twin-Faces | Splits fluid-solid interface faces into collocated twin patches `coupledWall_fluid` and `coupledWall_solid` with opposite face normals | Multi-region mesh with `cell_regions` | Reindexed mesh with twin boundary patches and `baffles` metadata | Non-matching twin face count raises `ValueError` | `SUGGESTIONS_BACKEND.md` §4.3 H4, `difvm_topology.py` |
| 13 | Verification & Validation | Automated 3-Grid GCI Hierarchy | Generates 3 deterministically refined meshes (coarse, medium, fine with $r \in [1.3, 1.5]$) preserving identical patch topology for ASME V&V 20 GCI studies | Target geometry, refinement ratio $r \in [1.3, 1.5]$, base config | 3 `UnstructuredMesh` objects (`mesh_fine`, `mesh_medium`, `mesh_coarse`) | Non-matching patch set across levels raises `ValueError` | `ORIGINAL_REQUEST.md` R4, `ROADMAP.md` §Phase 4 |
| 14 | Quality Assurance | Vectorized Scale-Invariant Validation | Checks $V_i > 0$, relative Gauss closure $< 10^{-11}$, projection collinearity $< 10^{-12}$, over-relaxation, and upper-triangular ordering | `UnstructuredMesh` | Boolean `True` or detailed `ValueError` | Fails fast on inverted cells or open face closures | `models.py:349-495`, `ORIGINAL_REQUEST.md` R1 |
| 15 | Code Cleanliness | Zero-Shortcut & De-Clamping | Removal of all `len(verts) == 8` box heuristics, millimeter-assumed constants (`3.0`, `5.0`), and silent resolution clamps ($n \le 30$) | Mesher input coordinates | Scalable, scale-invariant element generation | Limit violations raise explicit `UserWarning` / `ValueError` | `SUGGESTIONS_BACKEND.md` §4.1 K3, K4 |
| 16 | HPC Execution | 16-Thread & RAM-Safety Guard | Enforces $OMP\_NUM\_THREADS \le 16$ and streaming SoA allocation to prevent crashing concurrent ANSYS Fluent runs | System thread pool and mesher memory allocator | High-performance execution under $\le 16$ cores and bounded RAM | Exceeding 16 cores blocked at process launch | `ORIGINAL_REQUEST.md` Follow-up 2026-11-03T00:42:09Z |

---

## 3. Edge Cases & Observed / Required Behaviors

| # | Feature | Input | Observed / Required Behavior |
|---|---------|-------|------------------------------|
| 1 | Farfield Generator | Target geometry bbox with zero span in one dimension ($L_z = 0$) | Reject with `ValueError("Planar geometry provided to 3D farfield mesher; use 2D mesher instead")`. |
| 2 | Farfield Generator | Downstream wake domain specification | Ensure downstream extension is asymmetric ($+10 L_{\text{ref}}$ in $+x$, $+5 L_{\text{ref}}$ elsewhere) to fully capture aerodynamic wake without wasted upstream resolution. |
| 3 | Patch Assignment | Face exactly on the diagonal between `inlet` ($x_{\min}$) and `bottom` ($z_{\min}$) | Dominant normal projection determines patch: if $|n_x| \ge |n_z|$, assign `inlet`, else `bottom`. No face may belong to multiple patches. |
| 4 | Patch Assignment | Body geometry touching or piercing the outer farfield boundary | Raise `ValueError("Geometry penetrates farfield boundary box; increase farfield_padding")`. |
| 5 | Mosaic Transition | 4:1 refinement jump between adjacent octree cells | Octree 2:1 balancing pass MUST split intermediate cells recursively until all adjacent leaf cells differ by at most 1 level ($\Delta L \le 1$). |
| 6 | Mosaic Transition | Polyhedron face listing in DiFVM graph | The 4 sub-faces must be oriented consistently with the unrefined cell centroid (outward normal $\mathbf{S}_f \cdot (\mathbf{c}_f - \mathbf{C}_i) > 0$). |
| 7 | Cut-Cell Snapping | Node equidistant to two opposing CAD walls | Ray-marching along $\nabla \phi$ detects opposing wall collision; snap node to the nearest surface point only if distance does not exceed $0.4 d_{\text{opp}}$. |
| 8 | Cut-Cell Snapping | Snapping causes negative cell volume ($V_i \le 0$) | Backtrack along line search $\mathbf{x} = \mathbf{x}_{\text{orig}} + \lambda (\mathbf{x}_{\text{snap}} - \mathbf{x}_{\text{orig}})$ with $\lambda \in \{0.5, 0.25, 0.125, 0.0\}$ until $V_i > 0$ and $E_{\text{AMIPS}} < 10^4$. |
| 9 | Small-Cell Merging | Cut-cell with volume $V_{\text{cut}} = 10^{-16} V_{\text{bg}}$ (extreme sliver) | Agglomerate into neighboring fluid cell along $\nabla \phi$. Volume is summed: $V_{\text{new}} = V_{\text{cut}} + V_{\text{nb}} > 0$. Sliver face removed. |
| 10 | Small-Cell Merging | Cut-cell with no available fluid neighbor (island cell or narrow channel deadlock) | Merge into neighbor with largest shared face area; if completely isolated, discard with audit log warning. |
| 11 | Berger-Giuliani WSRD | Target cell with $\alpha_j \ge \alpha_{\text{target}} = 0.50$ | Weight $w_{ij}$ smoothly drops to zero: $w_{ij} = \frac{1}{N_i - 1}(1 - 1) = 0$, ensuring standard unperturbed FVM scheme on regular cells. |
| 12 | Berger-Giuliani WSRD | Cut-cell with $\alpha_i = 0.20$ exactly on merging/WSRD threshold | Handled by WSRD as valid distinct cell with $w_{ii} = 1 - \sum w_{ij} > 0$. |
| 13 | CHT Twin-Faces | Multi-region mesh with 3 or more contiguous regions (e.g. `snappyMultiRegionHeater`: fluid, heater, solid left, solid right) | Generalized twin naming: `coupledWall_region{A}_to_{B}` and `coupledWall_region{B}_to_{A}` with reverse node ordering for region B. |
| 14 | CHT Twin-Faces | Internal face sharing cells of the same region | Remain in clean internal face block $[0, n_{\text{clean\_int}})$; MUST NOT be extracted into twin boundary patches. |
| 15 | GCI Hierarchy | Refinement ratio $r < 1.3$ or $r > 1.5$ | Warn user that ASME V&V 20 recommends $r \in [1.3, 1.5]$ for reliable numerical error estimates; clamp if outside $[1.1, 2.0]$. |
| 16 | GCI Hierarchy | Grid generation at fine level exceeds RAM threshold (> 16 GB) | Downscale background resolution or raise $h_0$ to avoid provoking OS paging while ANSYS Fluent runs in background. |
| 17 | Cut-Cell Clipping | `cap2` orientation flip bug (from `LMArena/generate.cpp:1146-1150`) | Do NOT negate `cap_S2` prior to checking `S2` in individual triangles. Maintain separate boolean flag `need_flip = (dot(cap_S2, outward) < 0.0)` and apply consistently to all cap triangles. |

---

## 4. Deep-Dive Mathematical Formulations & Algorithmic Steps

### 4.1 3D Poly-Hexcore & ANSYS Mosaic / PUMA Conformal Transition

#### 4.1.1 Octree Construction & 2:1 Balancing
Let $\Omega_{\text{domain}} = [x_{\min}, x_{\max}] \times [y_{\min}, y_{\max}] \times [z_{\min}, z_{\max}]$ be the bounding box enclosing the computational domain.
1. The root octant has level $L = 0$ with side lengths $(H_x, H_y, H_z)$.
2. An octant $C$ at level $L$ is recursively subdivided into 8 children ($L+1$) if:
   $$\text{size}(C) > h(\mathbf{C}_{\text{centroid}}) \quad \text{or} \quad \text{intersects\_narrow\_band}(\phi(\mathbf{x}), C)$$
3. **2:1 Balancing Rule**:
   For any two leaf octants $C_A, C_B$ sharing a face, edge, or vertex:
   $$|L(C_A) - L(C_B)| \le 1$$
   If $L(C_A) - L(C_B) > 1$, cell $C_B$ is split into 8 children. This propagation continues until the entire octree satisfies 2:1 balance.

#### 4.1.2 Mosaic Polyhedral Interface without Hanging Nodes
Consider an interface between coarse cell $C_C$ (level $L$) and 4 fine child cells $C_{F,1}, C_{F,2}, C_{F,3}, C_{F,4}$ (level $L+1$).
- Coarse cell face $F_{\text{orig}}$ has 4 vertices: $(v_0, v_1, v_2, v_3)$.
- Edge midpoints are $m_{01}, m_{12}, m_{23}, m_{30}$, and face center is $m_c$.
- The face $F_{\text{orig}}$ is replaced by 4 sub-faces:
  - $f_0 = (v_0, m_{01}, m_c, m_{30})$
  - $f_1 = (m_{01}, v_1, m_{12}, m_c)$
  - $f_2 = (m_c, m_{12}, v_2, m_{23})$
  - $f_3 = (m_{30}, m_c, m_{23}, v_3)$
- **Conformal Assignment**:
  - For each sub-face $f_k$, fine cell $C_{F,k}$ has $f_k$ as its touching boundary face.
  - Coarse cell $C_C$ incorporates all 4 sub-faces $f_0, f_1, f_2, f_3$ into its face list, along with its remaining 5 unmodified faces.
  - Cell $C_C$ now has 9 faces: it is classified as `CellType.POLYHEDRON = 42`.
  - In the DiFVM SoA graph:
    $$\text{owner}[f_k] = \min(C_C, C_{F,k}), \quad \text{neighbour}[f_k] = \max(C_C, C_{F,k})$$
    $$\mathbf{S}_{f_k} = \mathbf{n}_{\text{out}} A_{f_k} \quad (\text{directed from owner to neighbour})$$
  - Result: **0 hanging nodes**, **0 non-conforming vertices**, bit-for-bit compatible with OpenFOAM `polyMesh`.

---

### 4.2 Boundary Conformity: Cut-Cell Snapping & AMIPS Optimization

#### 4.2.1 Level-Set Classification
Let $\phi(\mathbf{x})$ be the signed distance function to the CAD body:
$$\phi(\mathbf{x}) > 0 \implies \mathbf{x} \in \Omega_{\text{fluid}}, \quad \phi(\mathbf{x}) < 0 \implies \mathbf{x} \in \Omega_{\text{solid}}, \quad \phi(\mathbf{x}) = 0 \implies \mathbf{x} \in \Gamma_{\text{wall}}$$
The outward unit normal into the fluid is:
$$\mathbf{n}(\mathbf{x}) = \frac{\nabla \phi(\mathbf{x})}{\|\nabla \phi(\mathbf{x})\|_2}$$

#### 4.2.2 Node Snapping Formula
For any background grid node $\mathbf{x}_k$ located within snapping distance $\epsilon_{\text{snap}} = \beta_{\text{snap}} h_{\text{loc}}$ (with $\beta_{\text{snap}} \in [0.10, 0.25]$):
$$\mathbf{x}_k^{\text{target}} = \mathbf{x}_k - \phi(\mathbf{x}_k) \frac{\nabla \phi(\mathbf{x}_k)}{\|\nabla \phi(\mathbf{x}_k)\|_2^2}$$

#### 4.2.3 Conformal AMIPS Energy Formulation (Hu et al. 2020 / fTetWild)
To ensure snapping does not invert or severely distort elements:
Let $\mathbf{D}_0$ be the reference coordinate matrix for an ideal element, and $\mathbf{D}_s = [\mathbf{v}_1 - \mathbf{v}_0, \mathbf{v}_2 - \mathbf{v}_0, \mathbf{v}_3 - \mathbf{v}_0]$ the current element matrix.
The deformation Jacobian is $\mathbf{J} = \mathbf{D}_s \mathbf{D}_0^{-1}$.
The scale- and rotation-invariant 3D AMIPS energy is:
$$E_{\text{AMIPS}}(T) = \frac{\operatorname{tr}(\mathbf{J}^T \mathbf{J})}{(\det \mathbf{J})^{2/3}} = \left[ \frac{27}{16} \frac{\left( \sum_{0 \le i < j \le 3} \|\mathbf{v}_i - \mathbf{v}_j\|_2^2 \right)^3}{\left(\det[\mathbf{v}_1 - \mathbf{v}_0, \mathbf{v}_2 - \mathbf{v}_0, \mathbf{v}_3 - \mathbf{v}_0]\right)^2} \right]^{1/3} \ge 3$$
- As $\det \mathbf{J} \to 0^+$, $E_{\text{AMIPS}} \to +\infty$.
- **Snapping Acceptance Rule**:
  Snapping displacement $\Delta \mathbf{x}_k = \mathbf{x}_k^{\text{target}} - \mathbf{x}_k$ is accepted with step length $\lambda \in [0, 1]$:
  $$\mathbf{x}_k(\lambda) = \mathbf{x}_k + \lambda \Delta \mathbf{x}_k$$
  Accept largest $\lambda \in \{1.0, 0.5, 0.25, 0.125, 0.0\}$ such that:
  $$\forall T \in \text{incident\_cells}(k): \quad \det \mathbf{J}(T) > 0 \quad \text{and} \quad E_{\text{AMIPS}}(T) < 10^4$$

#### 4.2.4 VoroCrust Mirrored Seed Pairing (Abdelkader et al. 2020)
For Voronoi-based boundary conformity, for each CAD triangle $(p_1, p_2, p_3)$ with bounding sphere radii $(r_1, r_2, r_3)$:
The dual mirrored seeds $(g^\uparrow, g^\downarrow)$ satisfy:
$$\|g^{\uparrow,\downarrow} - p_1\|^2 = r_1^2, \quad \|g^{\uparrow,\downarrow} - p_2\|^2 = r_2^2, \quad \|g^{\uparrow,\downarrow} - p_3\|^2 = r_3^2$$
Solving in the local orthonormal frame $\mathbf{e}_1 = \frac{p_2 - p_1}{\|p_2 - p_1\|}, \mathbf{e}_2 = \frac{(p_3 - p_1) - ((p_3 - p_1)\cdot \mathbf{e}_1)\mathbf{e}_1}{\|\dots\|}, \mathbf{e}_3 = \mathbf{e}_1 \times \mathbf{e}_2$:
$$x = \frac{r_1^2 - r_2^2 + d^2}{2d}, \quad y = \frac{r_1^2 - r_3^2 + i^2 + j^2 - 2ix}{2j}, \quad z = \sqrt{\max(0, r_1^2 - x^2 - y^2)}$$
$$g^{\uparrow,\downarrow} = p_1 + x\mathbf{e}_1 + y\mathbf{e}_2 \pm z\mathbf{e}_3$$
The shared Voronoi facet between $g^\uparrow$ and $g^\downarrow$ lies exactly on $(p_1, p_2, p_3)$ and has non-orthogonality vector $\mathbf{k}_f \equiv \mathbf{0}$.

---

### 4.3 Small-Cell Merging & Berger & Giuliani (2024) WSRD

#### 4.3.1 Cut-Cell Volume Fraction Definition
For any cut-cell $i$ resulting from intersection with $\phi = 0$:
$$\alpha_i = \frac{V_i}{V_{\text{bg}, i}} \in (0, 1]$$
where $V_{\text{bg}, i} = \Delta x_i \Delta y_i \Delta z_i$ is the unclipped Cartesian background cell volume.

#### 4.3.2 Small-Cell Merging Algorithm ($\alpha_i < 0.20$)
Cells with $\alpha_i < 0.20$ cannot stably support hyperbolic fluxes without crippling the global CFL condition.
1. **Selection of Merge Donor**: Cell $i$ with $\alpha_i < 0.20$.
2. **Selection of Merge Receiver $j^*$**:
   Search among all adjacent fluid cells $j \in \mathcal{N}(i)$ sharing an internal face $f_{ij}$:
   $$j^* = \arg\max_{j \in \mathcal{N}(i)} \left( \frac{\mathbf{c}_{f_{ij}} - \mathbf{C}_i}{\|\mathbf{c}_{f_{ij}} - \mathbf{C}_i\|_2} \cdot \frac{\nabla \phi(\mathbf{C}_i)}{\|\nabla \phi(\mathbf{C}_i)\|_2} \right)$$
   (i.e., the fluid neighbor most aligned with the outward gradient $\nabla \phi$).
   Ties are broken by selecting the neighbor with the largest shared face area $\|\mathbf{S}_{f_{ij}}\|_2$.
3. **Topological Agglomeration**:
   - Internal face $f_{ij^*}$ is deleted from the face graph.
   - Cells $i$ and $j^*$ are merged into a unified cell $M = i \cup j^*$:
     $$V_M = V_i + V_{j^*} \ge 0.20 V_{\text{bg}, i} + V_{j^*} > 0$$
     $$\mathbf{C}_M = \frac{V_i \mathbf{C}_i + V_{j^*} \mathbf{C}_{j^*}}{V_i + V_{j^*}}$$
   - Faces of $M$ are:
     $$\mathcal{F}_M = (\mathcal{F}_i \cup \mathcal{F}_{j^*}) \setminus \{f_{ij^*}\}$$
   - Any external face previously pointing to $i$ or $j^*$ now points to $M$ as owner or neighbour.
   - Cell $M$ is marked as `CellType.POLYHEDRON = 42`.

#### 4.3.3 Berger & Giuliani (2024) Weighted State Redistribution (WSRD)
For cells with $0.20 \le \alpha_i < \alpha_{\text{target}}$ (where $\alpha_{\text{target}} = 0.50$):
These cells are kept distinct in the mesh topology, but require WSRD weights stored in `mesh.error_map["wsrd_weights"]` for conservative, monotone solver time-stepping up to $\text{CFL} = 1$.

1. **Neighborhood Definitions**:
   - Let $M_k$ be the merging neighborhood for cell $k$.
   - Let $W_i = \{j \mid i \in M_j\}$ be the overlap set of cells receiving state contributions from cell $i$.
   - Let $N_i = |W_i|$ be the overlap count ($N_i \ge 2$).
2. **Weight Formulas (Berger & Giuliani 2024, Eq. 4.2)**:
   For $j \in W_i \setminus \{i\}$:
   $$w_{i,j} = \frac{1}{N_i - 1} \left(1 - \frac{\alpha_j}{\alpha_{\text{target}}}\right)$$
   For the cell itself ($j = i$):
   $$w_{i,i} = 1 - \sum_{j \in W_i \setminus \{i\}} w_{i,j}$$
3. **Conservation & Monotonicity Properties**:
   - Strict conservation:
     $$\sum_{j \in W_i} w_{i,j} = 1.0$$
   - Smooth shutoff: As $\alpha_j \to \alpha_{\text{target}} = 0.50$, $w_{i,j} \to 0$ and $w_{i,i} \to 1.0$ smoothly without discontinuous jumping.
   - For canonical 2-cell normal merging ($N_1 = 2, \alpha_{\text{target}} = 0.5$):
     $$w_{0,0} = 1, \quad w_{1,0} = 1 - 2\alpha_0 \ge \frac{1}{2} - \alpha_0, \quad w_{1,1} = 2\alpha_0$$
4. **State Redistribution Algorithm**:
   - Intermediate state after standard FVM step: $\widehat{U}_i^{n-1}$.
   - Neighborhood average:
     $$\widehat{V}_j = \sum_{i \in M_j} w_{i,j} V_i, \qquad \widehat{Q}_j^{n-1} = \frac{1}{\widehat{V}_j} \sum_{i \in M_j} w_{i,j} V_i \widehat{U}_i^{n-1}$$
   - Final state update:
     $$U_i^n = \sum_{j \in W_i} w_{i,j} \widehat{Q}_j^{n-1}$$
   - Initial condition pre-merging (Eq. 4.1):
     $$U^0 = \operatorname{SRD}(U^{\text{init}})$$

---

### 4.4 Automated 3-Grid GCI (Grid Convergence Index) Hierarchy

According to **ASME V&V 20-2009** and **Celik et al. (2008)**, numerical verification requires a systematic 3-grid convergence study:
Let Grid 1 be Fine, Grid 2 be Medium, and Grid 3 be Coarse.

#### 4.4.1 Representative Grid Size
For each 3D unstructured mesh $k \in \{1, 2, 3\}$ with $N_k$ cells and total volume $V_{\text{tot}}$:
$$h_k = \left( \frac{1}{N_k} \sum_{i=1}^{N_k} V_{i,k} \right)^{1/3}$$

#### 4.4.2 Grid Refinement Ratios
$$r_{21} = \frac{h_2}{h_1}, \qquad r_{32} = \frac{h_3}{h_2}$$
Requirement: $r_{21}, r_{32} \in [1.3, 1.5]$ (nominally $r = 1.414 \approx \sqrt{2}$).

#### 4.4.3 Apparent Order of Convergence $p$
Let $\phi_1, \phi_2, \phi_3$ be the solution of a key integral quantity (e.g. drag coefficient $C_D$, lift $C_L$, pressure drop $\Delta p$).
Differences:
$$\epsilon_{32} = \phi_3 - \phi_2, \qquad \epsilon_{21} = \phi_2 - \phi_1$$
The apparent order $p$ is calculated from:
$$p = \frac{1}{\ln(r_{21})} |\ln|\epsilon_{32}/\epsilon_{21}| + q(p)|$$
$$q(p) = \ln\left( \frac{r_{21}^p - s}{r_{32}^p - s} \right), \qquad s = 1 \cdot \operatorname{sign}(\epsilon_{32}/\epsilon_{21})$$
For uniform refinement ratio $r_{21} = r_{32} = r$:
$$q(p) = 0 \implies p = \frac{\ln|\epsilon_{32}/\epsilon_{21}|}{\ln(r)}$$

#### 4.4.4 Extrapolated Values & Error Estimates
- Richardson Extrapolated value:
  $$\phi_{\text{ext}}^{21} = \frac{r_{21}^p \phi_1 - \phi_2}{r_{21}^p - 1}$$
- Approximate relative error:
  $$e_a^{21} = \left| \frac{\phi_1 - \phi_2}{\phi_1} \right|$$
- Extrapolated relative error:
  $$e_{\text{ext}}^{21} = \left| \frac{\phi_{\text{ext}}^{21} - \phi_1}{\phi_{\text{ext}}^{21}} \right|$$
- **Fine Grid Convergence Index ($\text{GCI}_{21}$)**:
  $$\text{GCI}_{21} = \frac{1.25\, e_a^{21}}{r_{21}^p - 1}$$
- **Medium Grid Convergence Index ($\text{GCI}_{32}$)**:
  $$\text{GCI}_{32} = \frac{1.25\, e_a^{32} r_{21}^p}{r_{32}^p - 1}, \quad \text{where } e_a^{32} = \left|\frac{\phi_2 - \phi_3}{\phi_2}\right|$$
- **Asymptotic Range Check**:
  $$\text{Ratio} = \frac{\text{GCI}_{32}}{r_{21}^p \, \text{GCI}_{21}} \approx 1.0$$
  If $\text{Ratio} \in [0.95, 1.05]$, the solutions are confirmed to be in the asymptotic range of convergence.
  Target: $\text{GCI}_{21} < 1.0\%$.

#### 4.4.5 Topologically Conforming Hierarchy Guarantee
The mesher generates all 3 levels using deterministic octree division rules ($h_1 = h_0 / r, h_2 = h_0, h_3 = h_0 \cdot r$).
All 3 meshes preserve:
1. Exact identical patch names and boundary face types (`inlet`, `outlet`, `bottom`, `top`, `sides`, `wall`, `coupledWall_*`).
2. Identical coordinate frames and boundary alignment.
3. Identical cell type enums and SoA array layout.

---

### 4.5 Multiphysics CHT Coupled Interface Architecture

#### 4.5.1 Multi-Region Representation
- Every cell $c$ has an integer region tag `cell_regions[c]`:
  - $0$: Fluid domain $\Omega_F$
  - $1$: Solid domain $\Omega_S$ (or additional regions $2, 3, \dots$ for multi-solid heaters)
- Stored as `cell_regions: np.ndarray` (int32) in Python and `mesh.cell_regions` (`std::vector<int32_t>`) in C++20.

#### 4.5.2 Twin-Face Extraction Algorithm (`extract_cht_twin_faces`)
1. **Partitioning Faces**:
   - Clean internal faces: $\text{neighbour}[f] \ge 0 \land \text{cell\_regions}[\text{owner}[f]] == \text{cell\_regions}[\text{neighbour}[f]]$.
   - Coupled interface faces: $\text{neighbour}[f] \ge 0 \land \text{cell\_regions}[\text{owner}[f]] \ne \text{cell\_regions}[\text{neighbour}[f]]$.
   - Original boundary faces: $\text{neighbour}[f] == -1$.
2. **Reindexing into Contiguous Blocks**:
   - Block 1 $[0, n_{\text{clean\_int}})$: Clean internal faces (`neighbour >= 0`, `owner < neighbour`).
   - Block 2 $[n_{\text{clean\_int}}, n_{\text{clean\_int}} + n_{\text{orig\_bnd}})$: Original boundary patches (shifted by $\Delta = n_{\text{clean\_int}} - n_{\text{orig\_int}}$).
   - Block 3 $[n_{\text{clean\_int}} + n_{\text{orig\_bnd}}, n_{\text{clean\_int}} + n_{\text{orig\_bnd}} + n_{\text{coupled}})$: Twin patch `coupledWall_fluid` (or `coupledWall_regionA_to_B`):
     - `owner` = original owner (in fluid).
     - `neighbour` = -1.
     - $\mathbf{S}_f = +\mathbf{S}_{f, \text{orig}}$ (pointing into solid).
   - Block 4 $[n_{\text{clean\_int}} + n_{\text{orig\_bnd}} + n_{\text{coupled}}, \text{total})$: Twin patch `coupledWall_solid` (or `coupledWall_regionB_to_A`):
     - `owner` = original neighbour (in solid).
     - `neighbour` = -1.
     - $\mathbf{S}_f = -\mathbf{S}_{f, \text{orig}}$ (pointing into fluid).
     - Vertices reversed: $\text{faces}[f_{\text{solid}}] = \text{faces}[f_{\text{fluid}}][::-1]$.
3. **Baffle Pairing Metadata**:
   `baffles` list records pairs: `{"patch_a": "coupledWall_fluid", "patch_b": "coupledWall_solid", "face_pairs": [(fa, fb), ...]}`.

---

### 4.6 Invariant Audit & Verification Thresholds

| Invariant | Formulation / Mathematical Check | Tolerance / Threshold |
|---|---|---|
| Strictly Positive Volume | $\min_i V_i > 0$ for all $i \in \{0, \dots, N_c - 1\}$ | $\min V_i > 0$ (Zero tolerance for $V_i \le 0$) |
| Scale-Invariant Gauss Closure | $\max_i \frac{\|\sum_{f \in i} \pm \mathbf{S}_f\|_2}{\sum_{f \in i} \|\mathbf{S}_f\|_2} < \text{tol}_{\text{rel}}$ | $\text{tol}_{\text{rel}} < 10^{-11}$ |
| Non-Orthogonal Decomposition | $\max_{f} \|\boldsymbol{\delta}_f + \mathbf{k}_f - \mathbf{S}_f\|_2 < \text{tol}_{\text{decomp}}$ | $\text{tol}_{\text{decomp}} < 10^{-14}$ |
| Projection Collinearity | $\max_{f \in \text{int}} \frac{\|\boldsymbol{\delta}_f \times \mathbf{d}_f\|_2}{\|\boldsymbol{\delta}_f\|_2 \|\mathbf{d}_f\|_2} < \text{tol}_{\text{proj}}$ | $\text{tol}_{\text{proj}} < 10^{-12}$ |
| Over-Relaxation Identity | $\max_{f \in \text{int}, \theta < 89^\circ} \frac{|\boldsymbol{\delta}_f \cdot \mathbf{S}_f - \|\mathbf{S}_f\|^2|}{\|\mathbf{S}_f\|^2} < \text{tol}_{\text{relax}}$ | $\text{tol}_{\text{relax}} < 10^{-10}$ |
| Face Ordering | $\text{owner}[f] < \text{neighbour}[f]$ on all internal faces | Strict inequality ($100\%$) |
| Block Contiguity | All internal faces precede all boundary faces | Strict boundary cutoff ($100\%$) |
| Patch Coverage | $\sum_{(p, (s, c))} c == n_{\text{bnd}}$ with no overlaps | Exact match ($100\%$) |
| Stabilization Escalation | Faces with $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ | Immediate escalation to `stabilization_mask = 3` |

---

## 5. Architectural Specifications & Interface Contracts

### 5.1 Python Dataclass Extensions (`backend/meshing/models.py`)

```python
from typing import Literal, Optional, Tuple, List, Dict, Any
from dataclasses import dataclass, field
import numpy as np

@dataclass
class FarfieldConfig:
    """Configuration for outer domain bounding box generation."""
    padding_upstream: float = 5.0    # 5 * L_ref upstream (-x)
    padding_downstream: float = 10.0 # 10 * L_ref downstream (+x)
    padding_lateral: float = 5.0    # 5 * L_ref sides (+/- y)
    padding_vertical: float = 5.0   # 5 * L_ref top/bottom (+/- z)
    explicit_bbox: Optional[Tuple[float, float, float, float, float, float]] = None

@dataclass
class CutCellConfig:
    """Configuration for Cut-Cell Snapping and Small-Cell Merging."""
    snap_tolerance_factor: float = 0.20 # Snap nodes within 0.20 * h_loc
    small_cell_volume_ratio: float = 0.20 # Merge cells with V_cut < 0.20 * V_bg
    wsrd_target_alpha: float = 0.50 # WSRD redistribution threshold (0.20 <= alpha < 0.50)
    amips_energy_max: float = 1e4 # Maximum acceptable AMIPS distortion energy

@dataclass
class GCIHierarchyConfig:
    """Configuration for automated 3-grid GCI convergence studies."""
    refinement_ratio: float = 1.414 # Refinement factor r in [1.3, 1.5]
    levels: int = 3 # Coarse, Medium, Fine

@dataclass
class MeshingConfig:
    """Declarative unified configuration schema for native mesher and frontend."""
    domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"
    farfield: FarfieldConfig = field(default_factory=FarfieldConfig)
    cutcell: CutCellConfig = field(default_factory=CutCellConfig)
    gci: GCIHierarchyConfig = field(default_factory=GCIHierarchyConfig)
    sizing: CurvatureSizingConfig = field(default_factory=CurvatureSizingConfig)
    inflation: Optional[InflationConfig] = None
    quality: QualityConfig = field(default_factory=QualityConfig)
    rcm_reorder: bool = True
    cht_multiregion: bool = False
    cyclic_symmetry: bool = False
    cyclic_angle_deg: float = 0.0
    distmesh_iters: int = 45
```

### 5.2 C++20 Interface Contracts (`backend/meshing/native/include/`)

#### 5.2.1 `difvm_mesh.hpp` Extensions
```cpp
// Explicit Cell Types (VTK & OpenFOAM polyhedral compatibility)
enum class CellType : int32_t {
    TRIANGLE = 5,
    POLYGON = 7,
    QUAD = 9,
    TETRA = 10,
    HEXAHEDRON = 12,
    WEDGE = 13,
    PYRAMID = 14,
    POLYHEDRON = 42
};

struct PolyHexcoreOptions {
    double h0 = 0.05;
    double min_cell_volume_fraction = 0.20; // Small-cell merge threshold
    double wsrd_alpha_target = 0.50;        // Berger-Giuliani threshold
    double snap_tol_factor = 0.20;
    int32_t max_octree_depth = 8;
    int32_t domain_mode = 0; // 0=external_fluid, 1=internal_fluid, 2=solid_only, 3=cht_coupled
};
```

#### 5.2.2 C-ABI Functions in `cfd_mesher_native.h`
```cpp
// Poly-Hexcore generator
CFD_MESHER_API int32_t cfd_mesher_generate_poly_hexcore(
    void* handle,
    const double* surf_points,
    int64_t n_points,
    const int64_t* surf_faces,
    int64_t n_faces,
    const PolyHexcoreOptions* opts
);

// Berger-Giuliani WSRD weight accessor
CFD_MESHER_API const double* cfd_mesher_get_wsrd_weights_ptr(void* handle);
CFD_MESHER_API const double* cfd_mesher_get_cell_volumes_bg_ptr(void* handle);

// Multi-region CHT tagging
CFD_MESHER_API const int32_t* cfd_mesher_get_cell_regions_ptr(void* handle);
```

---

## 6. Implementation & Verification Plan for Work Package 4

### 6.1 Step-by-Step Implementation Sequence
1. **Model & Configuration Updates (`backend/meshing/models.py`)**:
   - Add `FarfieldConfig`, `CutCellConfig`, `GCIHierarchyConfig`.
   - Update `MeshingConfig` with `domain_mode`, `farfield`, `cutcell`, and `gci`.
   - Ensure complete serialization (`to_dict`, `from_dict`).
2. **Farfield & Patch Assignment Module (`backend/meshing/mesh_engine.py`)**:
   - Implement `compute_farfield_bounding_box(geom_bbox, farfield_cfg)`.
   - Implement `assign_farfield_patches(faces, points, farfield_bbox)`.
   - Completely eliminate all `len(verts) == 8` heuristics and millimeter-assumed constants (`3.0`, `5.0`).
3. **Octree & Mosaic Conformal Transition (`backend/meshing/hexcore.py` and C++ `difvm_hexcore.cpp`)**:
   - Implement 2:1 balanced octree grid construction.
   - Implement Mosaic polyhedral replacement for 2:1 transition faces (generating `CellType.POLYHEDRON = 42` without hanging nodes).
4. **Cut-Cell Snapping & Small-Cell Merging (`backend/meshing/cutcell.py`)**:
   - Implement AMIPS-guarded node snapping on $\phi = 0$.
   - Implement topological small-cell merging for $\alpha_i < 0.20$ along $\nabla \phi$.
   - Avoid the `cap2` orientation flip bug identified in `LMArena/generate.cpp`.
5. **Berger-Giuliani WSRD Weights**:
   - Compute $w_{ij} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ for $0.20 \le \alpha_i < 0.50$.
   - Populate `mesh.error_map["wsrd_weights"]` and `mesh.error_map["wsrd_alpha"]`.
6. **Automated 3-Grid GCI Hierarchy (`backend/meshing/gci.py`)**:
   - Implement `generate_gci_hierarchy(cad_file, base_config, r=1.414) -> Tuple[UnstructuredMesh, UnstructuredMesh, UnstructuredMesh]`.
   - Implement `compute_gci_metrics(phi_fine, phi_med, phi_coarse, r) -> GCIMetrics`.
7. **Comprehensive Unit- & Benchmark Test Suite (`tests/test_wp4_multiphysics_hexcore.py`)**:
   - Test external fluid domain meshing with Farfield patches.
   - Test Mosaic transition without hanging nodes (verify closed Gauss divergence $< 10^{-11}$).
   - Test small-cell merging eliminating all cells with $\alpha < 0.20$.
   - Test Berger-Giuliani WSRD weight conservation ($\sum w_{ij} = 1.0$) and monotonicity.
   - Test CHT multi-region coupling with `snappyMultiRegionHeater`.
   - Test GCI 3-grid hierarchy generation and Richardson extrapolation.
   - Test CAD benchmarks (`TestCAD.stp`, `BladeFullRev1CFD.stp` with `scale=0.001`) with moderate grid sizes under 16-thread & RAM limits.
8. **Independent 3-Agent Audit Committee**:
   - Paper-Fidelity Challenger audit.
   - Code- & Software-Architecture Auditor (Google C++ Style & PEP 8).
   - Mathematics- & Numerics-Auditor ($V_i > 0$, relative Gauss closure $< 10^{-11}$).
