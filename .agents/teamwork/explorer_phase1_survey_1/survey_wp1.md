# Work Package 1 (WP1) Survey & Architectural Blueprint
**Mission:** Topologie-Kern, Zelltyp-Eindeutigkeit, faces-Konnektivität, MeshErrorMap & polyMesh-Export  
**Author:** explorer_phase1_survey_1  
**Target Milestone:** Phase 1 — WP1 Implementation  
**Integrity Mode:** Development (Zero-Shortcut Policy)

---

## 1. Executive Summary

This survey provides the authoritative gap analysis and implementation specification for Work Package 1 (WP1) of the CFD meshing subsystem.
Following the user's directive, the **CFDSolver codebase is the Single Source of Truth**. External spickzettel and LMArena notes have been critically audited and are referenced selectively where mathematically sound.

### Current State vs. Target State Summary:
| Area | Current State (Audit Findings) | Target State (WP1 Specification) |
| :--- | :--- | :--- |
| **Face Connectivity** | Discarded after topology extraction; `UnstructuredMesh` and `NativeMeshData` lack `faces` / `face_offsets` / `face_nodes`. | Explicit face-to-node connectivity in Python and C++ CSR; consistent right-hand rule (internal face normal $\mathbf{S}_f$ points strictly owner $\to$ neighbour with owner < neighbour; boundary faces point outward). |
| **polyMesh Export** | Non-existent (`backend/meshing/polymesh_io.py` missing). | Standard-conforming OpenFOAM `constant/polyMesh/` export (`points`, `faces`, `owner`, `neighbour` internal only, `boundary` with valid patch types: wall, patch, symmetry, empty, mappedWall, cyclic) and `extrude_2d_to_3d_slab` for 1-cell thick 2D representations with `frontAndBack` (`type empty;`). |
| **Cell Type Resolution** | Bug A: `nv == 4` always classified as 3D tetrahedron; in C++ lines 94-125 2D quad is dead code; $z \neq 0$ planarity detection fails. | Explicit `CellType` enum (VTK IDs: TRIANGLE=5, POLYGON=7, QUAD=9, TETRA=10, HEXAHEDRON=12, WEDGE=13, PYRAMID=14, POLYHEDRON=42), `cell_types` array, `topological_dim` (2 or 3). Coplanar 4-node elements are QUAD even if $z \neq 0$. General 3D polyhedra supported. |
| **Manifoldness Check** | `face_map` silently overwrites `neighbour` if a 3rd cell shares a face; no upper triangular enforcement. | Deterministic `ValueError` on non-manifold faces (>2 cells sharing face); strict upper triangular order (`owner < neighbour`). |
| **CHT Multi-Region Twin Faces** | `neighbour[f] = -1` set in-place inside internal block; breaks contiguous block invariant; patch range assumption broken. | Clean extraction of coupled faces from internal block; reindexing internal faces ($0 \dots n_{\text{int}}-1$ contiguous); appending coupled faces as contiguous boundary patches (`coupledWall_regionA_to_B`, `coupledWall_regionB_to_A`). |
| **Named Boundary Patches** | All boundary faces lumped into single patch `"wall"`. | Clean pass-through of `face_patch_ids` / `patch_names` through `compute_difvm_topology` (Python & C++). |
| **Reordering (RCM & SFC)** | Bug E: `rcm_reorder` rebuilds topology from scratch and discards boundaries, baffles, cyclic pairs, and metadata. | Lossless in-place reindexing for `rcm_reorder` and new 3D Morton/Hilbert `hilbert_sfc_reorder`; preservation of boundaries, baffles, cyclic pairs, metadata, and computation of `inner_cells` vs `halo_adjacent_cells`. |
| **CMS Error Map** | Scalar error map lacks CMS vector fields; `cos_theta` clamped to $[0.0, 1.0]$ masking inverted faces. | Complete CMS compensation map: $\mathbf{m}_f^{\text{skew}}$, $\mathbf{k}_f^{\text{comp}}$ ($\alpha_{\max}=1.5$), $\beta_f^{\text{upwind}}$, $\omega_i^{\text{cell}}$, $\alpha_i^{\text{wsrd}}$; `cos_theta` clamped to $[-1.0, 1.0]$; $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ escalates immediately to `stabilization_mask = 3`. |
| **Validation (`validate`)** | Bug F: slow Python loop; tautological check $(\boldsymbol{\delta}_f + \mathbf{k}_f) - \mathbf{S}_f == 0$; absolute `tol_closure = 1e-11` breaks micro/macro scaling. | Vectorized via `np.add.at` / `np.subtract.at`; relative Gauss-Closure $\text{rel\_closure} \le 10^{-11}$; exact projection identity (collinearity $< 10^{-12}$ and over-relaxation $\boldsymbol{\delta}_f \cdot \mathbf{S}_f = \|\mathbf{S}_f\|^2$ for $\theta_f < 89^\circ$); contiguous blocks check; unified sizing configs. |
| **C-ABI Robustness** | H2/H10/H11: no `try/catch` in extern "C"; no bounds check in `cfd_mesher_build_from_cells`; no `try...finally` in Python; `/fp:fast` breaks IEEE-754. | Every extern "C" wrapped in `try/catch` (return -2); array bounds validated (offsets monotonic, nodes in range); Python `try...finally: cfd_mesher_free`; CMakeLists.txt set to `/fp:precise` and `-fno-fast-math -ffp-contract=off`. |

---

## 2. Deep-Dive Gap Analysis by Requirement

### Requirement 1: Explicit Face-to-Node Connectivity & Orientation Invariants
- **Existing Files:**
  - `backend/meshing/models.py` (lines 198–235): `UnstructuredMesh` defines `points`, `cells`, `owner`, `neighbour`, `face_areas`, `face_centres`, `cell_centres`, `cell_volumes`, `delta_f`, `k_f`, `boundaries`, but lacks `faces`.
  - `backend/meshing/difvm_topology.py` (lines 477–483): `temp_faces` extracts `"nodes": fnodes`, but when constructing `UnstructuredMesh` at line 650, `faces` is discarded.
  - `backend/meshing/native/include/difvm_mesh.hpp` (lines 53–77): `NativeMeshData` stores `cell_offsets` and `cell_nodes`, but has no `face_offsets` or `face_nodes`.
  - `backend/meshing/native/include/cfd_mesher_native.h` (lines 87–107): C-ABI getters exist for cells, but none for faces.
- **Defects & Gaps:**
  1. Without ordered face-to-node connectivity, polyhedral faces cannot be rendered in PyVista/Trame and cannot be exported to OpenFOAM `faces`.
  2. Face normal direction must satisfy the right-hand rule:
     - For internal face $f$: $\mathbf{S}_f$ points strictly from `owner[f]` to `neighbour[f]`, and `owner[f] < neighbour[f]`.
     - For boundary face $f$: $\mathbf{S}_f$ points outward from `owner[f]`.
     - The vertex order $(v_0, v_1, \dots, v_{m-1})$ of face $f$ must follow counter-clockwise orientation when looking back towards the face against $\mathbf{S}_f$, so that $\frac{1}{2}\sum_{i=1}^{m-2} (\mathbf{p}_{v_i} - \mathbf{p}_{v_0}) \times (\mathbf{p}_{v_{i+1}} - \mathbf{p}_{v_0})$ is parallel to $\mathbf{S}_f$.
- **Required Changes:**
  - In `models.py`: Add `faces: List[np.ndarray]` to `UnstructuredMesh` (with default empty list/optional fallback for backwards compatibility).
  - In `difvm_mesh.hpp`: Add `std::vector<int64_t> face_offsets;` and `std::vector<int64_t> face_nodes;` to `NativeMeshData`.
  - In `cfd_mesher_native.h` & `cfd_mesher_native.cpp`: Add getters:
    - `cfd_mesher_get_face_offsets_ptr(const CfdNativeMesh* mesh)`
    - `cfd_mesher_get_face_nodes_ptr(const CfdNativeMesh* mesh)`
    - `cfd_mesher_get_n_face_nodes(const CfdNativeMesh* mesh)`
  - In `difvm_topology.py` & `difvm_mesh.cpp`: Populate `faces` in `compute_difvm_topology_python` and `buildTopology`.

---

### Requirement 2: OpenFOAM `polyMesh` Export & 2D-to-3D Slab Extrusion
- **Existing Files:**
  - File `backend/meshing/polymesh_io.py` does not exist.
- **Defects & Gaps:**
  1. No ability to export native in-memory meshes to standard OpenFOAM `constant/polyMesh/` for ASME V&V 20 code-to-code validation (`checkMesh`, `simpleFoam`).
  2. In OpenFOAM, 2D simulations are represented as 1-cell thick 3D slabs with front and back boundaries assigned `type empty;`. Currently 2D planar meshes cannot be directly run in OpenFOAM.
- **OpenFOAM polyMesh Specification Details:**
  - `points`: ASCII FoamFile vectorField with $N_p$ entries `(x y z)`.
  - `faces`: ASCII FoamFile faceList with $N_f$ entries `m(v0 v1 ... vm-1)`.
  - `owner`: ASCII FoamFile labelList with $N_f$ entries.
  - `neighbour`: ASCII FoamFile labelList with **only $N_{\text{internal}}$ entries**! (Crucial: OpenFOAM neighbour array terminates at internal faces).
  - `boundary`: ASCII FoamFile polyBoundaryMesh dictionary containing patch sections:
    ```
    patchName
    {
        type            wall; // patch, symmetry, empty, mappedWall, cyclic
        nFaces          count;
        startFace       startIndex;
    }
    ```
- **Required Changes:**
  - Create `backend/meshing/polymesh_io.py` implementing:
    - `export_openfoam_polymesh(mesh: UnstructuredMesh, case_dir: Union[str, Path], write_binary: bool = False) -> Path`
    - `extrude_2d_to_3d_slab(mesh_2d: UnstructuredMesh, dz: float = 1.0) -> UnstructuredMesh`
  - In `UnstructuredMesh`: Add `to_openfoam_polymesh(self, case_dir: Union[str, Path], write_binary: bool = False) -> Path` delegating to `polymesh_io`.

---

### Requirement 3: Explicit VTK-Compatible Cell Types & Coplanar 2D Quad vs. 3D Tetra
- **Existing Files:**
  - `backend/meshing/difvm_topology.py` (lines 190–240): `_extract_cell_faces_py` decides solely based on vertex count `nv = len(cell_nodes)`. `nv == 4` is unconditionally treated as a 3D Tetrahedron (lines 204–211).
  - `backend/meshing/native/src/difvm_mesh.cpp` (lines 81–132): `extractCellFaces` has `else if (nv == 4)` at line 94 for Tetrahedron, rendering line 119 `else if (nv == 4) // 2D Quad` completely unreachable (dead code).
  - `backend/meshing/difvm_topology.py` (lines 497–498) & `difvm_mesh.cpp` (lines 227–231): 2D planarity is guessed via `abs(z) < 1e-6`. Fails for 2D meshes located at $z \neq 0$ (e.g. $z = 5.0$) or tilted 2D cutting planes.
- **Mathematical Planarity & Coplanarity Check:**
  For 4 points $\mathbf{p}_0, \mathbf{p}_1, \mathbf{p}_2, \mathbf{p}_3$:
  Vectors: $\mathbf{e}_1 = \mathbf{p}_1 - \mathbf{p}_0$, $\mathbf{e}_2 = \mathbf{p}_2 - \mathbf{p}_0$, $\mathbf{e}_3 = \mathbf{p}_3 - \mathbf{p}_0$.
  Scalar triple product: $V_{\text{box}} = |(\mathbf{e}_1 \times \mathbf{e}_2) \cdot \mathbf{e}_3|$.
  Scale metric: $L = \max(\|\mathbf{e}_1\|, \|\mathbf{e}_2\|, \|\mathbf{e}_3\|)$.
  If $L > 10^{-15}$ and $V_{\text{box}} / L^3 < 10^{-6}$, the element is coplanar $\implies$ `CellType.QUAD` ($9$).
  If $V_{\text{box}} / L^3 \ge 10^{-4}$, the element is a 3D volume $\implies$ `CellType.TETRA` ($10$).
- **CellType Enum Specification:**
  ```python
  class CellType(IntEnum):
      TRIANGLE = 5       # VTK_TRIANGLE
      POLYGON = 7        # VTK_POLYGON
      QUAD = 9           # VTK_QUAD
      TETRA = 10         # VTK_TETRA
      HEXAHEDRON = 12    # VTK_HEXAHEDRON
      WEDGE = 13         # VTK_WEDGE
      PYRAMID = 14       # VTK_PYRAMID
      POLYHEDRON = 42    # VTK_POLYHEDRON
  ```
- **Required Changes:**
  - Define `CellType` in `backend/meshing/models.py`.
  - Pass `cell_types: Optional[np.ndarray]` and `topological_dim: Optional[int]` to `compute_difvm_topology` (Python & C++).
  - Update `_extract_cell_faces_py` and `extractCellFaces`: take `cell_type` or perform coplanarity check. If QUAD, extract 4 edge faces; if TETRA, extract 4 triangular faces.
  - Store `cell_types: np.ndarray` (int32) and `topological_dim: int` in `UnstructuredMesh` and `NativeMeshData`.

---

### Requirement 4: Deterministic Manifoldness Check & Upper Triangular Ordering
- **Existing Files:**
  - `backend/meshing/difvm_topology.py` (lines 485–489):
    ```python
    else:
        f_idx = face_map[canonical_key]
        temp_faces[f_idx]["neighbour"] = c_idx
    ```
    If a face key appears a 3rd time, `neighbour` is silently overwritten.
  - `backend/meshing/native/src/difvm_mesh.cpp` (lines 210–216): Same silent overwrite.
- **Defects & Gaps:**
  1. Non-manifold geometry (>2 cells sharing a face) creates unphysical solver graphs and crashes FVM balance equations.
  2. Owner/neighbour indices must strictly adhere to upper-triangular ordering: `owner < neighbour` for all internal faces.
  3. When owner and neighbour are identified, their face normal winding must be opposite.
- **Required Changes:**
  - In `difvm_topology.py` and `difvm_mesh.cpp`:
    When `canonical_key` is already in `face_map`:
    Check `if temp_faces[f_idx]["neighbour"] != -1:` $\implies$ immediately raise `ValueError(f"Non-manifold interior face {canonical_key} shared by >2 cells")` (Python) and throw `std::runtime_error` (C++).
    If `c_idx < temp_faces[f_idx]["owner"]`:
    Swap owner and neighbour: set new owner to `c_idx`, neighbour to old owner, negate $\mathbf{S}_f$, and reverse `face_nodes`.

---

### Requirement 5: CHT Multi-Region Twin-Face Interface Extraction
- **Existing Files:**
  - `backend/meshing/difvm_topology.py` (lines 671–755): `extract_cht_twin_faces`.
- **Defects & Gaps (H4):**
  1. Line 705: `neighbour[f] = -1` mutates an internal face into a boundary face directly at index $f < n_{\text{internal}}$. This corrupts the FVM layout requirement that all internal faces precede boundary faces.
  2. Line 717: `boundaries["coupledWall_fluid"] = (coupled_internal[0], len(coupled_internal))` falsely assumes coupled faces are contiguous in the internal array.
- **Required Clean CHT Reindexing Algorithm:**
  1. Identify coupled internal faces: $\mathcal{F}_{\text{coupled}} = \{f \mid \text{nb}[f] \ge 0 \land \text{region}[\text{owner}[f]] \neq \text{region}[\text{nb}[f]]\}$.
  2. True internal faces: $\mathcal{F}_{\text{true\_int}} = \{f \mid \text{nb}[f] \ge 0 \land \text{region}[\text{owner}[f]] == \text{region}[\text{nb}[f]]\}$.
  3. Original boundary faces: $\mathcal{F}_{\text{bnd}} = \{f \mid \text{nb}[f] < 0\}$.
  4. Coupled twin faces:
     - Patch A $\to$ B (`coupledWall_region0_to_region1`): owner in region 0, $\mathbf{S}_f$ pointing out.
     - Patch B $\to$ A (`coupledWall_region1_to_region0`): owner in region 1, $-\mathbf{S}_f$ pointing out.
  5. Concatenate ordered face list:
     $[\mathcal{F}_{\text{true\_int}}, \; \mathcal{F}_{\text{bnd}}, \; \mathcal{F}_{A \to B}, \; \mathcal{F}_{B \to A}]$.
  6. Rebuild all face-indexed arrays (`owner`, `neighbour`, `face_areas`, `face_centres`, `delta_f`, `k_f`, `faces`, and `error_map`) via the reindexing permutation.
  7. Register contiguous boundaries:
     `boundaries[orig_name] = (new_start, count)`
     `boundaries["coupledWall_region0_to_region1"] = (start_A, count)`
     `boundaries["coupledWall_region1_to_region0"] = (start_B, count)`
     Record twin pairs in `baffles`: `twin_pairs: [(idx_A_k, idx_B_k)]`.

---

### Requirement 6: Named Boundary Patches Pass-Through
- **Existing Files:**
  - `backend/meshing/difvm_topology.py` (lines 621–626): `boundaries["wall"] = (n_int, n_bnd)` hardcodes all boundary faces into `"wall"`.
  - `backend/meshing/mesh_engine.py` (lines 448): same single patch `"wall"`.
  - `backend/meshing/native/src/difvm_mesh.cpp` (lines 431–442): hardcodes single `"wall"` patch.
- **Defects & Gaps (K7):**
  CAD surface patches (`SurfaceMesh.patches`), Wind tunnel / Farfield boundary names (`inlet`, `outlet`, `symmetry`, `empty`), and user patches are lost during topology generation.
- **Required Changes:**
  - In `compute_difvm_topology` (Python & C++):
    Accept optional `boundary_patches: Optional[Dict[str, List[List[int]]]]` or `face_patch_ids: Optional[np.ndarray]`.
    When classifying boundary faces, match them against the patch definition. Group faces by patch name so each patch is a contiguous range $(start_face, num_faces)$.
    Ensure no unassigned boundary faces remain (fallback unassigned to `"wall"`).

---

### Requirement 7: Lossless RCM & 3D Morton/Hilbert Space-Filling Curve (SFC) Reordering
- **Existing Files:**
  - `backend/meshing/distmesh_core.py` (lines 98–106): `rcm_reorder` calls `compute_difvm_topology(mesh.points, new_cells, regions=new_regions)` $\implies$ completely discards `boundaries`, `baffles`, `cyclic_pairs`, and `metadata`.
- **Lossless In-Place Permutation Architecture:**
  Given cell permutation vector `perm` ($N_c$) where `new_cell[i] = old_cell[perm[i]]`, and inverse permutation `inv_perm[old_idx] = new_idx`:
  1. Permute cell-level arrays:
     - `cell_centres_new = cell_centres[perm]`
     - `cell_volumes_new = cell_volumes[perm]`
     - `cell_regions_new = cell_regions[perm]`
     - `cells_new = [cells[perm[i]] for i in range(N_c)]`
     - `cell_types_new = cell_types[perm]`
  2. Permute face connectivity:
     - For internal faces:
       `o_new = inv_perm[owner]`, `nb_new = inv_perm[neighbour]`.
       If `o_new > nb_new`: swap `o_new` and `nb_new`, negate $\mathbf{S}_f$, reverse face node order in `faces`.
       Sort internal faces by `(o_new, nb_new)` to maintain upper-triangular order.
     - For boundary faces:
       `o_new = inv_perm[owner]`, `neighbour` stays $-1$.
       Preserve boundary patch groupings and face order within patches.
  3. Remap `baffles` twin pairs and `cyclic_pairs` using the face index permutation.
  4. Preserve all `metadata`.
- **3D Morton / Hilbert SFC Reordering (`hilbert_sfc_reorder`):**
  - Compute cell centroids bounding box $[\mathbf{x}_{\min}, \mathbf{x}_{\max}]$.
  - Quantize cell centroid coordinates to integer grid $[0, 2^{21}-1]$.
  - Compute 3D Morton (bit-interleaving) or 3D Hilbert curve code.
  - Sort cell indices by SFC code to produce `perm`.
  - Reorder mesh losslessly.
  - Partition / label cells:
    - `is_halo = np.zeros(num_cells, bool)`
    - Mark all cells owning at least one boundary face: `is_halo[mesh.owner[mesh.neighbour < 0]] = True`
    - `inner_cells = np.where(~is_halo)[0]`
    - `halo_adjacent_cells = np.where(is_halo)[0]`
    - Store indices and counts in `mesh.metadata["inner_cells"]`, `mesh.metadata["halo_adjacent_cells"]`, `mesh.metadata["n_inner_cells"]`, `mesh.metadata["n_halo_adjacent_cells"]`.

---

### Requirement 8: CMS Error Map Extensions & Inverted Face Escalation
- **Existing Files:**
  - `backend/meshing/difvm_topology.py` (lines 575–601, 640–647):
    - `cos_theta` clamped via `np.clip(..., 0.0, 1.0)` $\implies$ masks negative dot products ($\mathbf{S}_f \cdot \mathbf{d}_f \le 0$).
    - Skewness displacement vector $\mathbf{m}_f^{\text{skew}} = \mathbf{c}_f - \mathbf{x}_{\text{int}}$ is computed as intermediate $cf - x\_int$, but NOT saved in `error_map`.
    - Damped correction $\mathbf{k}_f^{\text{comp}}$ is not calculated.
    - `beta_upwind`, `omega_cell`, `wsrd_alpha` are missing.
  - `backend/meshing/native/src/difvm_mesh.cpp` (lines 356–385): Same clamp `std::clamp(..., 0.0, 1.0)`.
- **Mathematical Specification of CMS Fields (ARCHITECTURE.md §4.2):**
  1. **Strict Cosine Clamping & Inversion Escalation:**
     $$\cos\theta_f = \operatorname{clip}\left(\frac{\mathbf{S}_f \cdot \mathbf{d}_f}{\|\mathbf{S}_f\| \|\mathbf{d}_f\|}, -1.0, 1.0\right)$$
     If $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ ($\theta_f \ge 90^\circ$):
     Immediately escalate to `stabilization_mask[f] = 3` and set $\beta_f^{\text{upwind}} = 0.0$.
  2. **Skewness Offset Vector $\mathbf{m}_f^{\text{skew}}$:**
     $$\mathbf{m}_f^{\text{skew}} = \mathbf{c}_f - \mathbf{x}_{\text{int}} \in \mathbb{R}^3 \quad \text{on internal faces, } \mathbf{0} \text{ on boundaries}$$
     where $\mathbf{x}_{\text{int}} = \mathbf{C}_{\text{owner}} + \frac{(\mathbf{c}_f - \mathbf{C}_{\text{owner}}) \cdot \mathbf{n}_f}{\mathbf{d}_f \cdot \mathbf{n}_f} \mathbf{d}_f$.
  3. **Damped Non-Orthogonal Correction Vector $\mathbf{k}_f^{\text{comp}}$:**
     $$\mathbf{k}_f^{\text{comp}} = \min\left(1.0, \frac{\alpha_{\max} \|\boldsymbol{\delta}_f\|}{\|\mathbf{k}_f\| + \epsilon}\right) \mathbf{k}_f \quad (\alpha_{\max} = 1.5, \epsilon = 10^{-15})$$
  4. **Surgical Upwind Blending Factor $\beta_f^{\text{upwind}} \in [0, 1]$:**
     $$\beta_{\text{ortho}} = \operatorname{clip}\left(\frac{75.0 - \theta_f}{35.0}, 0.0, 1.0\right), \quad \beta_{\text{skew}} = \operatorname{clip}\left(\frac{1.0 - \text{skew}_f}{0.6}, 0.0, 1.0\right)$$
     $$\beta_f = \begin{cases} 0.0 & \text{if } \mathbf{S}_f \cdot \mathbf{d}_f \le 0 \\ \min(\beta_{\text{ortho}}, \beta_{\text{skew}}) & \text{otherwise} \end{cases}$$
  5. **Cell-Wise Multiphysics Protection Factor $\omega_i^{\text{cell}} \in (0, 1]$:**
     $$\omega_i = \max\left(0.1, \min_{f \in \text{faces}(i)} \beta_f\right)$$
  6. **WSRD Cut-Cell Volume Fraction $\alpha_i^{\text{wsrd}}$:**
     $$\alpha_i^{\text{wsrd}} = \operatorname{clip}\left(\frac{V_i}{V_{\text{target}}}, 0.0, 1.0\right)$$
- **Required Changes:**
  - Populate these fields in `error_map` in both Python (`difvm_topology.py`) and C++ (`difvm_mesh.cpp` and `NativeMeshData`).
  - Export getters in C-ABI:
    `cfd_mesher_get_m_skew_ptr`, `cfd_mesher_get_k_f_comp_ptr`, `cfd_mesher_get_beta_upwind_ptr`, `cfd_mesher_get_omega_cell_ptr`, `cfd_mesher_get_wsrd_alpha_ptr`.

---

### Requirement 9: Vectorized, Scale-Invariant `UnstructuredMesh.validate()`
- **Existing Files:**
  - `backend/meshing/models.py` (lines 268–309): `UnstructuredMesh.validate()`
- **Defects & Gaps (Bug F):**
  1. Line 296–301 uses slow Python loop `for f in range(self.num_faces):` to accumulate closure.
  2. Line 284: tests `(self.delta_f + self.k_f) - self.face_areas`. Since `k_f` was computed as `S_f - delta_f`, this is a tautology $( \boldsymbol{\delta}_f + (\mathbf{S}_f - \boldsymbol{\delta}_f) ) - \mathbf{S}_f \equiv 0$ and does not check whether $\boldsymbol{\delta}_f$ is the true over-relaxed projection!
  3. Line 303: `max_closure > tol_closure` uses absolute tolerance $10^{-11}$. Fails for large meshes ($A_f \sim 10^4$) due to normal FP64 roundoff, and passes completely open cells for micro meshes ($A_f \sim 10^{-12}$).
- **Vectorized Invariant Checking Formulation:**
  1. **Vectorized Relative Gauss-Closure:**
     ```python
     cell_surface_area = np.zeros(self.num_cells, dtype=np.float64)
     face_area_mag = np.linalg.norm(self.face_areas, axis=1)
     np.add.at(cell_surface_area, self.owner, face_area_mag)
     internal_mask = self.neighbour >= 0
     np.add.at(cell_surface_area, self.neighbour[internal_mask], face_area_mag[internal_mask])

     closure = np.zeros_like(self.cell_centres)
     np.add.at(closure, self.owner, self.face_areas)
     np.subtract.at(closure, self.neighbour[internal_mask], self.face_areas[internal_mask])

     closure_norm = np.linalg.norm(closure, axis=1)
     rel_closure = np.where(cell_surface_area > 1e-30, closure_norm / cell_surface_area, closure_norm)
     max_rel_closure = float(np.max(rel_closure))
     if max_rel_closure > tol_rel:
         raise ValueError(f"Relative Gauss divergence closure theorem violated: max error = {max_rel_closure:.2e} > {tol_rel:.2e}")
     ```
  2. **Exact Projection & Over-Relaxation Identities:**
     On internal faces ($nb \ge 0$):
     - Collinearity of $\boldsymbol{\delta}_f$ with $\mathbf{d}_f = \mathbf{C}_{\text{nb}} - \mathbf{C}_{\text{owner}}$:
       $$\frac{\|\boldsymbol{\delta}_f \times \mathbf{d}_f\|}{\|\boldsymbol{\delta}_f\| \|\mathbf{d}_f\|} < 10^{-12}$$
     - Over-relaxation identity for $\theta_f < 89^\circ$:
       $$\frac{|\boldsymbol{\delta}_f \cdot \mathbf{S}_f - \|\mathbf{S}_f\|^2|}{\|\mathbf{S}_f\|^2} < 10^{-10}$$
     - Decomposition identity:
       $$\|(\boldsymbol{\delta}_f + \mathbf{k}_f) - \mathbf{S}_f\| < 10^{-12}$$
  3. **Topological Invariants:**
     - `self.owner[internal_mask] < self.neighbour[internal_mask]` strictly enforced.
     - Contiguous internal block: `np.all(self.neighbour[:n_int] >= 0)` and `np.all(self.neighbour[n_int:] == -1)`.
     - Boundary face coverage: $\sum_{\text{patch}} nFaces_{\text{patch}} == n_{\text{boundary}}$, no overlaps or gaps.
  4. **Unify Sizing Configurations:**
     Ensure `CurvatureSizingConfig` and `SizingParameters` in `models.py` have shared parameter mapping (`h_min`, `h_max`, `h0`, `alpha`, `beta`, `grading`, `growth_rate`).

---

### Requirement 10: Native C-ABI Hardening, Exception Safety & Platform Build
- **Existing Files:**
  - `backend/meshing/native/src/cfd_mesher_native.cpp`: functions lack `try/catch` and array bounds validation.
  - `backend/meshing/native/CMakeLists.txt`:
    - Line 15: `/fp:fast` (MSVC)
    - Line 22: `-ffast-math` (GCC/Clang)
  - `backend/meshing/difvm_topology.py`:
    - Hardcoded `.dll` loading, no `.so` or `.dylib` discovery.
- **Defects & Gaps (H2, H10, H11):**
  1. Any C++ exception in an `extern "C"` function crossing the ABI boundary terminates the process without a traceback.
  2. Malformed `cell_offsets` or out-of-range `cell_nodes` causes memory access violations.
  3. `/fp:fast` breaks `std::isfinite`, `std::isnan`, and associative floating-point reductions.
- **Required Changes:**
  - In `cfd_mesher_native.cpp`:
    Wrap all function bodies in `try { ... } catch (...) { return -2; }` (or `return nullptr` for pointer getters).
    In `cfd_mesher_build_from_cells`:
    Validate:
    - `cell_offsets[0] == 0`
    - `cell_offsets[c] <= cell_offsets[c+1]` for all $c \in [0, n_{\text{cells}}-1]$
    - $0 \le \text{cell\_nodes}[k] < n_{\text{points}}$ for all $k$
    If any check fails, return `-1` cleanly.
  - In `mesh_engine.py` & `difvm_topology.py`:
    Enforce `try ... finally: self._native_dll.cfd_mesher_free(m_ptr)`.
  - In `CMakeLists.txt`:
    Change MSVC flag to `/fp:precise`.
    Change GCC/Clang flag to `-fno-fast-math -ffp-contract=off`.
  - In `difvm_topology.py`:
    Cross-platform loader searching `cfd_mesher_native.dll`, `libcfd_mesher_native.so`, and `libcfd_mesher_native.dylib`.

---

## 3. Detailed File Modification Matrix

| File Path | Nature of Change | Components Added / Modified |
| :--- | :--- | :--- |
| `backend/meshing/models.py` | Modify | - Add `CellType(IntEnum)` (TRIANGLE=5, POLYGON=7, QUAD=9, TETRA=10, HEXAHEDRON=12, WEDGE=13, PYRAMID=14, POLYHEDRON=42)<br>- Add `faces: Optional[List[np.ndarray]] = None`, `cell_types: Optional[np.ndarray] = None`, `topological_dim: int = 3` to `UnstructuredMesh`<br>- Add `to_openfoam_polymesh(self, case_dir, write_binary)` method<br>- Vectorize `validate()` with `np.add.at`, relative Gauss closure (`tol_rel=1e-11`), collinearity & over-relaxation checks, upper-triangular check, contiguous blocks check<br>- Unify `CurvatureSizingConfig` and `SizingParameters` fields |
| `backend/meshing/polymesh_io.py` | **Create New** | - `export_openfoam_polymesh(mesh, case_dir, write_binary)`<br>- Standard OpenFOAM headers (`points`, `faces`, `owner`, `neighbour`, `boundary`)<br>- `extrude_2d_to_3d_slab(mesh_2d, dz)` generating 1-cell thick 3D mesh with `frontAndBack` patch (`type empty;`) |
| `backend/meshing/difvm_topology.py` | Modify | - Cross-platform DLL loader (.dll, .so, .dylib)<br>- Add `faces`, `face_offsets`, `face_nodes` C-ABI bindings<br>- Add CMS getters for `m_skew`, `k_f_comp`, `beta_upwind`, `omega_cell`, `wsrd_alpha`<br>- Fix `_extract_cell_faces_py`: QUAD coplanarity check ($V_{\text{box}} / L^3 < 10^{-6}$), never TETRA if coplanar<br>- Manifoldness check: `ValueError` if >2 cells share face<br>- Upper triangular ordering: `owner < neighbour`<br>- Fix CHT twin faces: non-destructive contiguous boundary patch relocation<br>- Pass through named boundary patches<br>- Clamp `cos_theta` to $[-1.0, 1.0]$; escalate $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ to `stabilization_mask = 3`<br>- Compute CMS fields: `m_skew`, `k_f_comp`, `beta_upwind`, `omega_cell`, `wsrd_alpha` |
| `backend/meshing/distmesh_core.py` | Modify | - Fix `rcm_reorder`: in-place lossless reindexing preserving boundaries, baffles, cyclic pairs, metadata<br>- Add `hilbert_sfc_reorder`: 3D Morton/Hilbert curve sorting with `inner_cells` and `halo_adjacent_cells` metadata |
| `backend/meshing/native/include/difvm_mesh.hpp` | Modify | - Add `std::vector<int64_t> face_offsets;`, `std::vector<int64_t> face_nodes;`<br>- Add `std::vector<int32_t> cell_types;`<br>- Add CMS fields: `std::vector<double> m_skew;`, `std::vector<double> k_f_comp;`, `std::vector<double> beta_upwind;`, `std::vector<double> omega_cell;`, `std::vector<double> wsrd_alpha;`<br>- Add `CellType` enum in `cfd` namespace |
| `backend/meshing/native/include/cfd_mesher_native.h` | Modify | - Add getters for `face_offsets`, `face_nodes`, `n_face_nodes`<br>- Add getter for `cell_types`<br>- Add getters for CMS fields: `m_skew`, `k_f_comp`, `beta_upwind`, `omega_cell`, `wsrd_alpha` |
| `backend/meshing/native/src/difvm_mesh.cpp` | Modify | - Fix `extractCellFaces`: remove dead code, distinguish QUAD and TETRA via coplanarity<br>- Fix manifoldness check: throw exception on >2 cells sharing face<br>- Enforce `owner < neighbour` and right-hand rule outward normal<br>- Populate `face_offsets` and `face_nodes`<br>- Clamp `cos_theta` in $[-1.0, 1.0]$; escalate $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ to `stab_mask = 3`<br>- Populate `m_skew`, `k_f_comp`, `beta_upwind`, `omega_cell`, `wsrd_alpha` |
| `backend/meshing/native/src/cfd_mesher_native.cpp` | Modify | - Wrap all extern "C" functions in `try { ... } catch (...) { return -2; }`<br>- Validate array bounds in `cfd_mesher_build_from_cells` (return -1 on invalid)<br>- Implement new getters for faces, cell_types, and CMS fields |
| `backend/meshing/native/CMakeLists.txt` | Modify | - Replace `/fp:fast` with `/fp:precise`<br>- Replace `-ffast-math` with `-fno-fast-math -ffp-contract=off` |
| `backend/meshing/mesh_engine.py` | Modify | - Wrap `cfd_mesher_create` in `try ... finally: cfd_mesher_free`<br>- Remove silent `except Exception: pass` (log warning or reraise in strict mode) |
| `tests/test_wp1_topology_polymesh.py` | **Create New** | - Comprehensive Gate WP1 unit tests covering all 10 acceptance criteria |

---

## 4. Test Scenarios for Gate WP1 Verification

To ensure all Gate WP1 requirements are strictly satisfied with zero regressions against existing tests, the following test suite must be implemented:

1. **Test L-Shaped 2D Quad Grid at $z = 5.0$:**
   - 4-node planar elements at $z = 5.0$.
   - Verified that `topological_dim == 2`, cell types are `CellType.QUAD`, volumes are positive, face normals lie in $xy$-plane, and `validate()` passes.
2. **Test Scaled 3D Meshes ($10^{-6}$ m to $10^3$ m):**
   - Micro-channel cube ($L = 10^{-6}$ m) and atmospheric domain ($L = 10^3$ m).
   - Verified that `validate()` passes with scale-invariant `tol_rel = 1e-11`.
3. **Test Non-Manifold Face Rejection:**
   - Three cubes sharing a single face.
   - Verified that `compute_difvm_topology` deterministically raises `ValueError("Non-manifold interior face ...")`.
4. **Test CHT Two-Region Twin Faces:**
   - Two abutting cubes with region 0 and region 1.
   - Verified: no `neighbour == -1` in internal faces; contiguous internal faces; two contiguous boundary patches `coupledWall_region0_to_region1` and `coupledWall_region1_to_region0`; `validate()` passes.
5. **Test Inverted Face Detection & Stabilization Mask:**
   - Distorted cell pair where $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$.
   - Verified: `cos_theta` clamped in $[-1.0, 1.0]$, `stabilization_mask = 3`, `beta_upwind = 0.0`.
6. **Test OpenFOAM polyMesh Export & 2D Slab Extrusion:**
   - Export standard grid; verify file syntax and lengths (`neighbour` length == `n_internal_faces`).
   - Extrude 2D L-grid to 3D slab; verify front and back patch is `"frontAndBack"` with `type empty;`.
7. **Test Lossless RCM & Hilbert SFC Reordering:**
   - Reorder mesh with named boundaries, baffles, cyclic pairs, and metadata.
   - Verify 100 % preservation of boundaries, twin pairs, and metadata; verify `inner_cells` and `halo_adjacent_cells` metadata.
8. **Test CMS Error Map Numerical Correctness:**
   - Verify $\mathbf{m}_f^{\text{skew}} = \mathbf{c}_f - \mathbf{x}_{\text{int}}$ and $\|\mathbf{k}_f^{\text{comp}}\| \le \alpha_{\max} \|\boldsymbol{\delta}_f\|$.
9. **Test C-ABI Robustness:**
   - Invalid cell offsets or node indices return -1 without crashing; try/finally frees handles.
10. **Regression Suite:**
    - All 540 existing tests in `tests/` and `backend/geometry/tests/` remain 100 % green.

---
*Report prepared for orchestrator review and worker implementation dispatch.*
