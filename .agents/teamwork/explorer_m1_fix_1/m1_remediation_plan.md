# Milestone M1 Remediation Plan & Exact Code Modification Guide

**Author:** `explorer_m1_fix_1`  
**Date:** 2026-10-28  
**Target File:** `backend/meshing/geometry_sdf.py`  
**Test Suites:** `tests/test_geometry_sdf.py`, `tests/test_adversarial_m1.py`  
**Scope:** Complete drop-in code remediation for the 7 issues identified in Gate 1 review.

---

## 1. Issue Matrix & Root Cause Summary

| # | Issue | Component | File & Lines | Root Cause | Exact Fix |
|---|---|---|---|---|---|
| **1** | External flow proximity failure | `ProximityRefinement` | `geometry_sdf.py:1060, 1081-1086` | Marching along $-\mathbf{n}_{\text{start}}$ and skipping $\phi_0 > 0$ assumes fluid is $\phi < 0$. External flow has fluid at $\phi > 0$. | Add `domain_is_negative` / `inside_is_negative` support + auto-detection. In external domain ($\phi > 0$), march along $+\mathbf{n}_{\text{start}}$, search for crossing $\phi_{\text{ray}} \le 0$, and check opposing wall normal alignment. |
| **2** | Interface contract violation | `SDFEngine` | `geometry_sdf.py:452-498` | Missing `.evaluate(points)` method specified in `PROJECT.md` line 97. | Implement `SDFEngine(BaseSDF)` with instance method `.evaluate(points)` and dual class-level convenience `.evaluate(geom, points)`. |
| **3** | SurfaceMesh patches discarded | `CADFeatureFilter` | `geometry_sdf.py:783-790, 853-859` | `filter_degenerate_faces` and `extract_dominant_manifold_shell` instantiated `SurfaceMesh` without `patches`, dropping boundary definitions. | Build face index remap array `face_map` and filter/re-index `mesh.patches` across face pruning. |
| **4** | Normal sign error at boundary | `CADMeshSDF` | `geometry_sdf.py:680` | At $\|p - q\| \le 10^{-12}$, returned `best_normal` directly without negating when `inside_is_negative == False`. | Set `boundary_normal = best_normal if self.inside_is_negative else -best_normal`. |
| **5** | Scale sensitivity on micro-elements | `CADMeshSDF` | `geometry_sdf.py:615, 624, 629, 637` | Absolute clamping `det_safe = np.where(\|det\| < 1e-15, 1e-15, det)` collapses barycentric coordinates when $L \le 0.1$ mm ($\text{det} \sim L^4 \le 10^{-16}$). | Replace with relative threshold `scale = a * c`, `tol = np.maximum(1e-20, 1e-12 * scale)`, `det_safe = np.where(\|det\| < tol, np.sign(det) * tol, det)`. |
| **6** | IndexError on extreme coordinates | `CADMeshSDF` | `geometry_sdf.py:592, 600` | Coordinates $\ge 10^{160}$ overflow $(p-c)^2$ to `inf`, causing `cKDTree` to return index `len(data)` (`IndexError`). | Clamp candidate indices: `cand_idx = np.clip(cand_idx, 0, len(self.tri_v0) - 1)`. |
| **7** | Non-manifold fin leak & split seams | `CADFeatureFilter` | `geometry_sdf.py:829-834` | (a) Union-find grouped faces across ANY edge with $>1$ face, keeping fins. (b) Duplicate vertices across split seams split shells. | (a) Only propagate adjacency across strict 2-manifold edges (`len(f_list) == 2`). (b) Add `weld_coincident_vertices(tol=1e-7)` using `cKDTree.query_pairs`. |

---

## 2. Detailed Fix Specifications

### Issue 1: External Flow Proximity Refinement
**Location:** `backend/meshing/geometry_sdf.py` -> `ProximityRefinement`
**Mechanism:**
1. In external CFD aerodynamics, fluid domain is $\phi > 0$ outside solid bodies ($\phi < 0$).
2. The surface normal gradient $\mathbf{n}_{\text{start}} = \nabla \phi / \|\nabla \phi\|$ points outward into fluid.
3. To find opposing bodies across a gap, rays must march along $+\mathbf{n}_{\text{start}}$ (outward into fluid).
4. Ray samples stay in fluid ($\phi > 0$) until hitting an opposing solid body where $\phi \le 0$.
5. Linear interpolation finds the zero level set $t^*$:
   $$\text{frac} = \frac{-\phi_{\text{prev}}}{\phi_{\text{curr}} - \phi_{\text{prev}}},\quad t^* = t_{\text{prev}} + \text{frac} \cdot (t_{\text{curr}} - t_{\text{prev}})$$
6. The opposing normal alignment condition is identical in both cases: $\mathbf{n}_{\text{start}} \cdot \mathbf{n}_{\text{hit}} \le \theta_{\text{opp\_threshold}}$.
7. Total gap distance is $d_{\text{gap}} = d_{\text{near}} + t^*$, where $d_{\text{near}} = |\phi_0|$.

### Issue 2: `SDFEngine.evaluate` Interface Contract
**Location:** `backend/meshing/geometry_sdf.py` -> `SDFEngine`
**Mechanism:**
1. Make `SDFEngine` inherit from `BaseSDF`.
2. Support initialization with `root: Optional[Union[BaseSDF, Dict[str, Any]]] = None`.
3. Support instance evaluation: `engine.evaluate(points) -> Tuple[np.ndarray, np.ndarray]`.
4. Support class-level static convenience: `SDFEngine.evaluate(geom, points)` and `SDFEngine.evaluate(points)` with default root.
5. Implement `distance`, `gradient`, and `get_bounding_box` forwarding to root geometry.

### Issue 3: SurfaceMesh Patches Preservation & Re-indexing
**Location:** `backend/meshing/geometry_sdf.py` -> `CADFeatureFilter`
**Mechanism:**
1. In `filter_degenerate_faces`:
   ```python
   face_map = np.full(len(mesh.faces), -1, dtype=np.int32)
   face_map[valid_mask] = np.arange(np.count_nonzero(valid_mask), dtype=np.int32)
   new_patches = {}
   for p_name, p_faces in mesh.patches.items():
       p_faces_arr = np.asarray(p_faces, dtype=np.int32)
       valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < len(mesh.faces))
       mapped = face_map[p_faces_arr[valid_face_mask]]
       surviving = mapped[mapped >= 0]
       if len(surviving) > 0:
           new_patches[p_name] = surviving
   ```
2. In `extract_dominant_manifold_shell`: Apply identical remapping using `dominant_mask`.
3. In `filter_orphan_vertices`: Keep `new_patches = {k: np.copy(v) for k, v in mesh.patches.items()}`.

### Issue 4: Surface Normal Sign on Boundary when `inside_is_negative == False`
**Location:** `backend/meshing/geometry_sdf.py` -> `CADMeshSDF.evaluate`
**Mechanism:**
1. When $\|p - q\| \le 10^{-12}$, query point lies directly on boundary surface.
2. If `inside_is_negative == True`: outward gradient is $+\text{best\_normal}$.
3. If `inside_is_negative == False`: outward gradient is $-\text{best\_normal}$.
4. Define: `boundary_normal = best_normal if self.inside_is_negative else -best_normal`.

### Issue 5: Scale Sensitivity in Barycentric Projection
**Location:** `backend/meshing/geometry_sdf.py` -> `CADMeshSDF.evaluate`
**Mechanism:**
1. For an element of length $L \le 0.1$ mm, $a \sim L^2, c \sim L^2$, so $ac \sim L^4 \le 10^{-16}$.
2. Hardcoded clamping `1e-15` inflates the denominator by $13.3\times$, collapsing coordinates.
3. Replace with scale-relative threshold:
   ```python
   scale = a_coeff * c_coeff
   tol = np.maximum(1e-20, 1e-12 * scale)
   det_safe = np.where(np.abs(det) < tol, np.where(det >= 0.0, tol, -tol), det)
   ```
4. In edge projections and normal computations, lower zero threshold to `1e-20`.

### Issue 6: cKDTree Out-of-Range Indexing Guard
**Location:** `backend/meshing/geometry_sdf.py` -> `CADMeshSDF.evaluate`
**Mechanism:**
1. `cKDTree.query()` returns index `len(self.centroids)` when distance squared overflows to `inf`.
2. Guard against this immediately after querying:
   ```python
   num_faces = len(self.tri_v0)
   cand_idx = np.clip(cand_idx, 0, num_faces - 1)
   ```

### Issue 7: Strict 2-Manifold Edge Propagation & Vertex Welding
**Location:** `backend/meshing/geometry_sdf.py` -> `CADFeatureFilter`
**Mechanism:**
1. In `extract_dominant_manifold_shell`, union-find must ONLY join faces along strict 2-manifold edges:
   ```python
   for f_list in edge_to_faces.values():
       if len(f_list) == 2:
           union(f_list[0], f_list[1])
   ```
2. Implement `CADFeatureFilter.weld_coincident_vertices(mesh: SurfaceMesh, tol: float = 1e-7) -> SurfaceMesh`:
   - Uses `cKDTree(mesh.vertices).query_pairs(r=tol)`.
   - Unions coincident vertex indices and remaps `mesh.faces` to unique canonical roots.
3. In `CADFeatureFilter.sanitize`, execute vertex welding as step 1.

---

## 3. Drop-In Code Modifications for `backend/meshing/geometry_sdf.py`

### Chunk 1: `SDFEngine` Replacement (around lines 452-498)

```python
<<<<
class SDFEngine:
    """Unified Facade for continuous SDF evaluation and JSON/dict geometry tree building."""

    @staticmethod
    def from_dict(spec: Dict[str, Any]) -> BaseSDF:
====
class SDFEngine(BaseSDF):
    """Unified Facade for continuous SDF evaluation and JSON/dict geometry tree building.
    
    Implements the PROJECT.md interface contract:
        geometry_sdf.SDFEngine.evaluate(points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]
    """

    _default_root: Optional[BaseSDF] = None

    def __init__(self, root: Optional[Union[BaseSDF, Dict[str, Any]]] = None) -> None:
        if root is None:
            self.root: Optional[BaseSDF] = None
        elif isinstance(root, dict):
            self.root = SDFEngine.from_dict(root)
        elif isinstance(root, BaseSDF):
            self.root = root
        else:
            raise TypeError(f"Unsupported root geometry type: {type(root)}")

    @classmethod
    def set_default_geometry(cls, geom: Union[BaseSDF, Dict[str, Any]]) -> None:
        """Register a default geometry for class-level evaluation."""
        if isinstance(geom, dict):
            cls._default_root = cls.from_dict(geom)
        else:
            cls._default_root = geom

    def distance(self, points: np.ndarray) -> np.ndarray:
        """Evaluate continuous signed distance phi(x)."""
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.distance(points)

    def gradient(self, points: np.ndarray, eps: float = 1e-6) -> np.ndarray:
        """Evaluate outward unit normal gradient."""
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.gradient(points, eps=eps)

    def evaluate(
        self,
        points_or_geom: Union[np.ndarray, BaseSDF, Dict[str, Any]],
        points: Optional[np.ndarray] = None,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluate continuous signed distance and unit normal gradient: (phi, grad_phi).

        Supports:
        - Instance call: engine.evaluate(points)
        - Class call with geom: SDFEngine.evaluate(geom, points)
        - Class call with default geom: SDFEngine.evaluate(points)
        """
        if points is not None:
            geom = points_or_geom
            if isinstance(geom, dict):
                geom = SDFEngine.from_dict(geom)
            if isinstance(geom, BaseSDF):
                return geom.evaluate(points)
            raise TypeError(f"Invalid geometry: {type(geom)}")

        pts = points_or_geom
        root = getattr(self, "root", None) or self._default_root
        if root is not None and isinstance(pts, (np.ndarray, list, tuple)):
            return root.evaluate(np.asarray(pts))
        elif isinstance(self, SDFEngine) and self.root is not None:
            return self.root.evaluate(np.asarray(pts))
        raise RuntimeError("SDFEngine has no root geometry assigned to evaluate.")

    def get_bounding_box(self) -> BoundingBox:
        root = self.root or self._default_root
        if root is None:
            raise RuntimeError("SDFEngine has no root geometry assigned.")
        return root.get_bounding_box()

    @staticmethod
    def from_dict(spec: Dict[str, Any]) -> BaseSDF:
>>>>
```

---

### Chunk 2: `CADMeshSDF` Scale Sensitivity, Index Guard & Normal Sign (around lines 550-685)

```python
<<<<
        # Precompute face unit normal vectors
        cross = np.cross(self.tri_v1 - self.tri_v0, self.tri_v2 - self.tri_v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        lengths[lengths < 1e-15] = 1.0
        self.normals = cross / lengths
====
        # Precompute face unit normal vectors
        cross = np.cross(self.tri_v1 - self.tri_v0, self.tri_v2 - self.tri_v0)
        lengths = np.linalg.norm(cross, axis=1, keepdims=True)
        lengths[lengths < 1e-20] = 1.0
        self.normals = cross / lengths
>>>>
```

```python
<<<<
        # 1. Query nearest candidate triangle centroids via cKDTree
        _, cand_idx = self.tree.query(p, k=self.k_candidates)
        if self.k_candidates == 1:
            cand_idx = cand_idx[:, None]

        # 2. Vectorized projection of (N * k) point-triangle pairs
====
        # 1. Query nearest candidate triangle centroids via cKDTree
        _, cand_idx = self.tree.query(p, k=self.k_candidates)
        if self.k_candidates == 1:
            cand_idx = cand_idx[:, None]

        # Guard against cKDTree overflow on extreme coordinates (>= 1e160 or Inf)
        num_faces = len(self.tri_v0)
        cand_idx = np.clip(cand_idx, 0, num_faces - 1)

        # 2. Vectorized projection of (N * k) point-triangle pairs
>>>>
```

```python
<<<<
        det = a_coeff * c_coeff - b_coeff * b_coeff
        det_safe = np.where(np.abs(det) < 1e-15, 1e-15, det)

        s_param = (c_coeff * d_coeff - b_coeff * e_coeff) / det_safe
        t_param = (a_coeff * e_coeff - b_coeff * d_coeff) / det_safe

        inside_mask = (s_param >= 0.0) & (t_param >= 0.0) & (s_param + t_param <= 1.0)
        q_inside = a_pts + s_param[:, None] * u + t_param[:, None] * v

        # Edge 1: AB (t = 0)
        s1 = np.clip(d_coeff / np.maximum(a_coeff, 1e-15), 0.0, 1.0)
        q1 = a_pts + s1[:, None] * u
        d1_sq = np.sum((p_flat - q1) ** 2, axis=-1)

        # Edge 2: AC (s = 0)
        t2 = np.clip(e_coeff / np.maximum(c_coeff, 1e-15), 0.0, 1.0)
        q2 = a_pts + t2[:, None] * v
        d2_sq = np.sum((p_flat - q2) ** 2, axis=-1)

        # Edge 3: BC
        w_b = p_flat - b_pts
        e_bc = c_pts - b_pts
        bc_len_sq = np.sum(e_bc * e_bc, axis=-1)
        t3 = np.clip(np.sum(w_b * e_bc, axis=-1) / np.maximum(bc_len_sq, 1e-15), 0.0, 1.0)
====
        det = a_coeff * c_coeff - b_coeff * b_coeff
        scale = a_coeff * c_coeff
        tol = np.maximum(1e-20, 1e-12 * scale)
        det_safe = np.where(np.abs(det) < tol, np.where(det >= 0.0, tol, -tol), det)

        s_param = (c_coeff * d_coeff - b_coeff * e_coeff) / det_safe
        t_param = (a_coeff * e_coeff - b_coeff * d_coeff) / det_safe

        inside_mask = (s_param >= 0.0) & (t_param >= 0.0) & (s_param + t_param <= 1.0)
        q_inside = a_pts + s_param[:, None] * u + t_param[:, None] * v

        # Edge 1: AB (t = 0)
        s1 = np.clip(d_coeff / np.maximum(a_coeff, 1e-20), 0.0, 1.0)
        q1 = a_pts + s1[:, None] * u
        d1_sq = np.sum((p_flat - q1) ** 2, axis=-1)

        # Edge 2: AC (s = 0)
        t2 = np.clip(e_coeff / np.maximum(c_coeff, 1e-20), 0.0, 1.0)
        q2 = a_pts + t2[:, None] * v
        d2_sq = np.sum((p_flat - q2) ** 2, axis=-1)

        # Edge 3: BC
        w_b = p_flat - b_pts
        e_bc = c_pts - b_pts
        bc_len_sq = np.sum(e_bc * e_bc, axis=-1)
        t3 = np.clip(np.sum(w_b * e_bc, axis=-1) / np.maximum(bc_len_sq, 1e-20), 0.0, 1.0)
>>>>
```

```python
<<<<
        # 5. Outward unit normal gradient
        diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
        grad_phi = np.where(
            diff_norm > 1e-12,
            sign[:, None] * (diff / np.maximum(diff_norm, 1e-15)),
            best_normal,
        )
====
        # 5. Outward unit normal gradient
        diff_norm = np.linalg.norm(diff, axis=1, keepdims=True)
        boundary_normal = best_normal if self.inside_is_negative else -best_normal
        grad_phi = np.where(
            diff_norm > 1e-12,
            sign[:, None] * (diff / np.maximum(diff_norm, 1e-15)),
            boundary_normal,
        )
>>>>
```

---

### Chunk 3: `CADFeatureFilter` Vertex Welding, Strict Manifold Adjacency & Patches Remapping (lines 763-873)

```python
<<<<
    @staticmethod
    def filter_degenerate_faces(
        mesh: SurfaceMesh, min_area: float = 1e-14
    ) -> SurfaceMesh:
        """Remove zero-area triangles and collapsed vertices (v0 == v1 or v1 == v2 or v2 == v0)."""
        if len(mesh.faces) == 0:
            return mesh

        f = mesh.faces
        non_collapsed = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 2] != f[:, 0])

        v0 = mesh.vertices[f[:, 0]]
        v1 = mesh.vertices[f[:, 1]]
        v2 = mesh.vertices[f[:, 2]]
        cross = np.cross(v1 - v0, v2 - v0)
        areas = 0.5 * np.linalg.norm(cross, axis=1)

        valid_mask = non_collapsed & (areas >= min_area)
        if np.all(valid_mask):
            return mesh

        new_faces = mesh.faces[valid_mask]
        filtered = SurfaceMesh(
            vertices=mesh.vertices,
            faces=new_faces,
            normals=None,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(filtered)

    @staticmethod
    def extract_dominant_manifold_shell(mesh: SurfaceMesh) -> SurfaceMesh:
        """Extract the dominant (largest connected area) 2-manifold surface shell,
        discarding disconnected auxiliary wireframes and stray sketch polygons.
        """
        if len(mesh.faces) <= 1:
            return mesh

        num_faces = len(mesh.faces)

        # Build adjacency graph between faces sharing undirected edges
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        for f_idx, face in enumerate(mesh.faces):
            e0 = (min(face[0], face[1]), max(face[0], face[1]))
            e1 = (min(face[1], face[2]), max(face[1], face[2]))
            e2 = (min(face[2], face[0]), max(face[2], face[0]))
            for e in (e0, e1, e2):
                edge_to_faces.setdefault(e, []).append(f_idx)

        # Disjoint set union (Union-Find) on faces
        parent = np.arange(num_faces, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        def union(i: int, j: int) -> None:
            root_i = find(i)
            root_j = find(j)
            if root_i != root_j:
                parent[root_i] = root_j

        for f_list in edge_to_faces.values():
            if len(f_list) > 1:
                first = f_list[0]
                for other in f_list[1:]:
                    union(first, other)

        # Compute face areas
        v0 = mesh.vertices[mesh.faces[:, 0]]
        v1 = mesh.vertices[mesh.faces[:, 1]]
        v2 = mesh.vertices[mesh.faces[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)

        # Sum areas per component
        comp_areas: Dict[int, float] = {}
        for f_idx in range(num_faces):
            r = find(f_idx)
            comp_areas[r] = comp_areas.get(r, 0.0) + areas[f_idx]

        dominant_root = max(comp_areas.keys(), key=lambda r: comp_areas[r])
        dominant_mask = np.array([find(f_idx) == dominant_root for f_idx in range(num_faces)], dtype=bool)

        if np.all(dominant_mask):
            return mesh

        dom_mesh = SurfaceMesh(
            vertices=mesh.vertices,
            faces=mesh.faces[dominant_mask],
            normals=None,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(dom_mesh)

    @staticmethod
    def sanitize(mesh: SurfaceMesh, min_area: float = 1e-14) -> SurfaceMesh:
        """Execute complete sanitization chain:
        1. Remove degenerate and zero-area facets.
        2. Extract dominant 2-manifold shell.
        3. Compact and purge orphan sketch vertices.
        """
        clean = CADFeatureFilter.filter_degenerate_faces(mesh, min_area=min_area)
        clean = CADFeatureFilter.extract_dominant_manifold_shell(clean)
        clean = CADFeatureFilter.filter_orphan_vertices(clean)
        return clean
====
    @staticmethod
    def weld_coincident_vertices(
        mesh: SurfaceMesh, tol: float = 1e-7
    ) -> SurfaceMesh:
        """Weld duplicate / coincident vertices within spatial tolerance using cKDTree."""
        if len(mesh.vertices) <= 1:
            return mesh

        tree = cKDTree(mesh.vertices)
        pairs = tree.query_pairs(r=tol)
        if not pairs:
            return mesh

        num_verts = len(mesh.vertices)
        parent = np.arange(num_verts, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        for i, j in pairs:
            root_i = find(i)
            root_j = find(j)
            if root_i < root_j:
                parent[root_j] = root_i
            elif root_j < root_i:
                parent[root_i] = root_j

        roots = np.array([find(i) for i in range(num_verts)], dtype=np.int32)
        unique_roots, inv = np.unique(roots, return_inverse=True)

        welded_vertices = mesh.vertices[unique_roots]
        welded_faces = inv[mesh.faces].astype(np.int32)
        new_patches = {k: np.copy(v) for k, v in mesh.patches.items()}

        return SurfaceMesh(
            vertices=welded_vertices,
            faces=welded_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )

    @staticmethod
    def filter_degenerate_faces(
        mesh: SurfaceMesh, min_area: float = 1e-14
    ) -> SurfaceMesh:
        """Remove zero-area triangles and collapsed vertices, re-indexing patches."""
        if len(mesh.faces) == 0:
            return mesh

        f = mesh.faces
        non_collapsed = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 2] != f[:, 0])

        v0 = mesh.vertices[f[:, 0]]
        v1 = mesh.vertices[f[:, 1]]
        v2 = mesh.vertices[f[:, 2]]
        cross = np.cross(v1 - v0, v2 - v0)
        areas = 0.5 * np.linalg.norm(cross, axis=1)

        valid_mask = non_collapsed & (areas >= min_area)
        if np.all(valid_mask):
            return mesh

        new_faces = mesh.faces[valid_mask]

        # Remap patch face indices
        face_map = np.full(len(mesh.faces), -1, dtype=np.int32)
        face_map[valid_mask] = np.arange(np.count_nonzero(valid_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < len(mesh.faces))
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        filtered = SurfaceMesh(
            vertices=mesh.vertices,
            faces=new_faces,
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(filtered)

    @staticmethod
    def extract_dominant_manifold_shell(
        mesh: SurfaceMesh, weld_tol: float = 1e-7
    ) -> SurfaceMesh:
        """Extract the dominant 2-manifold surface shell across strict 2-manifold edges
        (len == 2), discarding non-manifold fins (>2 faces) and re-indexing patches.
        """
        if len(mesh.faces) <= 1:
            return mesh

        # Weld coincident vertices first
        if weld_tol > 0.0 and len(mesh.vertices) > 1:
            mesh = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)

        num_faces = len(mesh.faces)

        # Build adjacency graph between faces sharing undirected edges
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        for f_idx, face in enumerate(mesh.faces):
            e0 = (min(face[0], face[1]), max(face[0], face[1]))
            e1 = (min(face[1], face[2]), max(face[1], face[2]))
            e2 = (min(face[2], face[0]), max(face[2], face[0]))
            for e in (e0, e1, e2):
                edge_to_faces.setdefault(e, []).append(f_idx)

        # Disjoint set union (Union-Find) on faces across strict 2-manifold edges
        parent = np.arange(num_faces, dtype=np.int32)

        def find(i: int) -> int:
            path = []
            while parent[i] != i:
                path.append(i)
                i = parent[i]
            for node in path:
                parent[node] = i
            return i

        def union(i: int, j: int) -> None:
            root_i = find(i)
            root_j = find(j)
            if root_i != root_j:
                parent[root_i] = root_j

        # Strict 2-manifold adjacency: only join faces sharing an edge with exactly 2 faces
        for f_list in edge_to_faces.values():
            if len(f_list) == 2:
                union(f_list[0], f_list[1])

        # Compute face areas
        v0 = mesh.vertices[mesh.faces[:, 0]]
        v1 = mesh.vertices[mesh.faces[:, 1]]
        v2 = mesh.vertices[mesh.faces[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)

        # Sum areas per component
        comp_areas: Dict[int, float] = {}
        for f_idx in range(num_faces):
            r = find(f_idx)
            comp_areas[r] = comp_areas.get(r, 0.0) + areas[f_idx]

        dominant_root = max(comp_areas.keys(), key=lambda r: comp_areas[r])
        dominant_mask = np.array([find(f_idx) == dominant_root for f_idx in range(num_faces)], dtype=bool)

        if np.all(dominant_mask):
            return mesh

        # Remap patch face indices
        face_map = np.full(num_faces, -1, dtype=np.int32)
        face_map[dominant_mask] = np.arange(np.count_nonzero(dominant_mask), dtype=np.int32)

        new_patches: Dict[str, np.ndarray] = {}
        for p_name, p_faces in mesh.patches.items():
            p_faces_arr = np.asarray(p_faces, dtype=np.int32)
            valid_face_mask = (p_faces_arr >= 0) & (p_faces_arr < num_faces)
            mapped = face_map[p_faces_arr[valid_face_mask]]
            surviving = mapped[mapped >= 0]
            if len(surviving) > 0:
                new_patches[p_name] = surviving

        dom_mesh = SurfaceMesh(
            vertices=mesh.vertices,
            faces=mesh.faces[dominant_mask],
            normals=None,
            patches=new_patches,
            metadata=dict(mesh.metadata),
        )
        return CADFeatureFilter.filter_orphan_vertices(dom_mesh)

    @staticmethod
    def sanitize(
        mesh: SurfaceMesh, min_area: float = 1e-14, weld_tol: float = 1e-7
    ) -> SurfaceMesh:
        """Execute complete sanitization chain:
        1. Weld coincident vertices within spatial tolerance.
        2. Remove degenerate and zero-area facets.
        3. Extract dominant 2-manifold shell.
        4. Compact and purge orphan sketch vertices.
        """
        clean = CADFeatureFilter.weld_coincident_vertices(mesh, tol=weld_tol)
        clean = CADFeatureFilter.filter_degenerate_faces(clean, min_area=min_area)
        clean = CADFeatureFilter.extract_dominant_manifold_shell(clean, weld_tol=0.0)
        clean = CADFeatureFilter.filter_orphan_vertices(clean)
        return clean
>>>>
```

---

### Chunk 4: `ProximityRefinement` External Flow Support (lines 1020-1120)

```python
<<<<
class ProximityRefinement:
    """Detects opposing walls and computes proximity-adapted size h_prox(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
    ) -> None:
        """Initialize proximity evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
        """
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim

    def evaluate_gap_distance(self, points: np.ndarray) -> np.ndarray:
        """Compute narrow gap distance d_gap(x) along the inward normal ray.

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            d_gap: Array of shape (N,), float64 gap distance. Clamped to max_gap_search
                   if no opposing wall exists within range.
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape

        phi_0_eval, grad_0 = self.sdf_evaluator(pts)
        phi_0 = np.atleast_1d(phi_0_eval)
        grad_norms = np.linalg.norm(grad_0, axis=1, keepdims=True)
        grad_norms[grad_norms < 1e-12] = 1.0
        n_start = grad_0 / grad_norms
        v_inward = -n_start  # Ray direction into domain

        r_max = float(self.params.max_gap_search or (self.params.h_max / self.params.beta))
        n_steps = max(24, self.params.ray_march_steps * 3)

        # Use non-linear (quadratic) distribution to densely sample near-wall region
        u_samples = np.linspace(1.0 / n_steps, 1.0, n_steps)  # shape (S,)
        t_samples = (u_samples**2) * r_max

        # Construct candidate ray evaluation points: shape (N, S, d)
        ray_pts = pts[:, None, :] + (t_samples[None, :, None]) * v_inward[:, None, :]
        flat_ray_pts = ray_pts.reshape(-1, d)

        # Batch evaluate all sample points
        phi_ray_flat, grad_ray_flat = self.sdf_evaluator(flat_ray_pts)
        phi_ray = np.atleast_1d(phi_ray_flat).reshape(n_pts, n_steps)
        grad_ray = grad_ray_flat.reshape(n_pts, n_steps, d)

        d_near = np.abs(phi_0)
        d_gap = np.full(n_pts, r_max, dtype=np.float64)

        for i in range(n_pts):
            if phi_0[i] > 0.0:
                # Outside domain; proximity inactive
                continue

            crossings = np.where(phi_ray[i] >= 0.0)[0]
            if len(crossings) == 0:
                continue

            first_cross = crossings[0]
            if first_cross == 0:
                t_prev = 0.0
                phi_prev = phi_0[i]
                t_curr = t_samples[0]
                phi_curr = phi_ray[i, 0]
                denom = phi_curr - phi_prev
                frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
                t_star = t_prev + frac * (t_curr - t_prev)
                hit_grad = grad_ray[i, 0]
            else:
                t_prev = t_samples[first_cross - 1]
                t_curr = t_samples[first_cross]
                phi_prev = phi_ray[i, first_cross - 1]
                phi_curr = phi_ray[i, first_cross]
                denom = phi_curr - phi_prev
                frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
                t_star = t_prev + frac * (t_curr - t_prev)
                hit_grad = grad_ray[i, first_cross]

            hit_norm = np.linalg.norm(hit_grad)
            if hit_norm > 1e-12:
                n_hit = hit_grad / hit_norm
                # Opposing normal check: the hit wall normal opposes the starting wall normal
                alignment = np.dot(n_start[i], n_hit)
                if alignment <= self.params.theta_opp_threshold:
                    d_gap[i] = d_near[i] + t_star

        return d_gap[0] if single_point else d_gap
====
class ProximityRefinement:
    """Detects opposing walls and computes proximity-adapted size h_prox(x).
    
    Supports:
    - External aerodynamic flow domains where fluid has phi > 0 (marching along +n_start).
    - Internal channel flows where fluid has phi < 0 (marching along -n_start).
    - Both inside_is_negative=True and inside_is_negative=False sign conventions.
    """

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        domain_is_negative: Optional[bool] = None,
        inside_is_negative: Optional[bool] = None,
    ) -> None:
        """Initialize proximity evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
            domain_is_negative: If True, fluid domain is phi < 0. If False, fluid is phi > 0.
                                If None, auto-detects per query point.
            inside_is_negative: Convenient alias; sets domain_is_negative = not inside_is_negative.
        """
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim
        if inside_is_negative is not None and domain_is_negative is None:
            self.domain_is_negative = not inside_is_negative
        else:
            self.domain_is_negative = domain_is_negative

    def evaluate_gap_distance(self, points: np.ndarray) -> np.ndarray:
        """Compute narrow gap distance d_gap(x) along the normal ray.

        Args:
            points: Array of shape (N, d) or (d,), float64.

        Returns:
            d_gap: Array of shape (N,), float64 gap distance. Clamped to max_gap_search
                   if no opposing wall exists within range.
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape

        phi_0_eval, grad_0 = self.sdf_evaluator(pts)
        phi_0 = np.atleast_1d(phi_0_eval)
        grad_norms = np.linalg.norm(grad_0, axis=1, keepdims=True)
        grad_norms[grad_norms < 1e-12] = 1.0
        n_start = grad_0 / grad_norms

        r_max = float(self.params.max_gap_search or (self.params.h_max / self.params.beta))
        n_steps = max(24, self.params.ray_march_steps * 3)

        # Use non-linear (quadratic) distribution to densely sample near-wall region
        u_samples = np.linspace(1.0 / n_steps, 1.0, n_steps)  # shape (S,)
        t_samples = (u_samples**2) * r_max

        # Determine ray marching direction per point based on domain sign:
        # External fluid (phi > 0): ray marches along +n_start (into fluid, away from solid body)
        # Internal channel (phi < 0): ray marches along -n_start (into channel cavity)
        is_external = np.zeros(n_pts, dtype=bool)
        for i in range(n_pts):
            if self.domain_is_negative is True:
                is_external[i] = False
            elif self.domain_is_negative is False:
                is_external[i] = True
            else:
                is_external[i] = (phi_0[i] >= 0.0)

        v_ray = np.where(is_external[:, None], n_start, -n_start)

        # Construct candidate ray evaluation points: shape (N, S, d)
        ray_pts = pts[:, None, :] + (t_samples[None, :, None]) * v_ray[:, None, :]
        flat_ray_pts = ray_pts.reshape(-1, d)

        # Batch evaluate all sample points
        phi_ray_flat, grad_ray_flat = self.sdf_evaluator(flat_ray_pts)
        phi_ray = np.atleast_1d(phi_ray_flat).reshape(n_pts, n_steps)
        grad_ray = grad_ray_flat.reshape(n_pts, n_steps, d)

        d_near = np.abs(phi_0)
        d_gap = np.full(n_pts, r_max, dtype=np.float64)

        for i in range(n_pts):
            if self.domain_is_negative is True and phi_0[i] > 0.0:
                continue
            if self.domain_is_negative is False and phi_0[i] < 0.0:
                continue

            if is_external[i]:
                # External: marching into fluid (phi > 0), look for solid crossing where phi <= 0
                crossings = np.where(phi_ray[i] <= 0.0)[0]
            else:
                # Internal: marching into channel (phi < 0), look for boundary crossing where phi >= 0
                crossings = np.where(phi_ray[i] >= 0.0)[0]

            if len(crossings) == 0:
                continue

            first_cross = crossings[0]
            if first_cross == 0:
                t_prev = 0.0
                phi_prev = phi_0[i]
                t_curr = t_samples[0]
                phi_curr = phi_ray[i, 0]
                hit_grad = grad_ray[i, 0]
            else:
                t_prev = t_samples[first_cross - 1]
                t_curr = t_samples[first_cross]
                phi_prev = phi_ray[i, first_cross - 1]
                phi_curr = phi_ray[i, first_cross]
                hit_grad = grad_ray[i, first_cross]

            denom = phi_curr - phi_prev
            frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
            frac = np.clip(frac, 0.0, 1.0)
            t_star = t_prev + frac * (t_curr - t_prev)

            hit_norm = np.linalg.norm(hit_grad)
            if hit_norm > 1e-12:
                n_hit = hit_grad / hit_norm
                alignment = np.dot(n_start[i], n_hit)
                if alignment <= self.params.theta_opp_threshold:
                    d_gap[i] = d_near[i] + t_star

        return d_gap[0] if single_point else d_gap
>>>>
```

---

## 4. Verification Test Cases to Add in `tests/test_geometry_sdf.py`

Append the following 7 new unit tests to `tests/test_geometry_sdf.py`:

```python
def test_proximity_refinement_external_flow_spheres():
    """Verify gap detection between two spheres in an external flow domain (phi > 0)."""
    s1 = SphereSDF(center=[-1.05, 0.0, 0.0], radius=1.0)
    s2 = SphereSDF(center=[1.05, 0.0, 0.0], radius=1.0)
    union = CSGUnion(s1, s2)
    params = SizingParameters(h_min=0.005, h_max=0.20, beta=0.25)
    prox = ProximityRefinement(sdf_evaluator=union.evaluate, params=params, dim=3)

    gap = prox.evaluate_gap_distance(np.array([[0.0, 0.0, 0.0]]))
    h_prox = prox.evaluate_h_prox(np.array([[0.0, 0.0, 0.0]]))
    assert np.isclose(gap[0], 0.10, atol=0.01)
    assert np.isclose(h_prox[0], 0.25 * 0.10, atol=0.005)


def test_sdf_engine_evaluate_interface_contract():
    """Verify SDFEngine.evaluate interface contract per PROJECT.md on instance & class level."""
    s = SphereSDF(center=[0.0, 0.0, 0.0], radius=1.0)
    engine = SDFEngine(s)
    pts = np.array([[2.0, 0.0, 0.0], [0.0, 2.0, 0.0]])

    phi, grad = engine.evaluate(pts)
    assert np.allclose(phi, [1.0, 1.0])
    assert np.allclose(grad[0], [1.0, 0.0, 0.0])
    assert np.allclose(grad[1], [0.0, 1.0, 0.0])

    # Class-level convenience
    phi_c, grad_c = SDFEngine.evaluate(s, pts)
    assert np.allclose(phi, phi_c)
    assert np.allclose(grad, grad_c)


def test_cad_feature_filter_patches_preservation_and_reindexing():
    """Verify SurfaceMesh patches are preserved and re-indexed across feature filtering."""
    verts = np.array([
        [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
        [99.0, 99.0, 99.0],  # orphan vertex
    ])
    faces = np.array([
        [0, 1, 2],  # face 0 (valid)
        [0, 2, 3],  # face 1 (valid)
        [0, 0, 1],  # face 2 (degenerate)
    ])
    patches = {
        "inlet": np.array([0]),
        "outlet": np.array([1, 2]),
    }
    mesh = SurfaceMesh(vertices=verts, faces=faces, patches=patches)
    clean = CADFeatureFilter.filter_degenerate_faces(mesh)

    assert len(clean.faces) == 2
    assert "inlet" in clean.patches
    assert "outlet" in clean.patches
    assert np.array_equal(clean.patches["inlet"], [0])
    assert np.array_equal(clean.patches["outlet"], [1])  # face 2 pruned, face 1 preserved


def test_cad_mesh_sdf_inside_is_negative_false_boundary_normal():
    """Verify outward gradient orientation when inside_is_negative == False on boundary."""
    mesh = SurfaceMesh(
        vertices=np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]),
        faces=np.array([[0, 1, 2]]),
    )
    sdf_true = CADMeshSDF(mesh, inside_is_negative=True)
    sdf_false = CADMeshSDF(mesh, inside_is_negative=False)

    pt = np.array([0.2, 0.2, 0.0])
    phi_t, grad_t = sdf_true.evaluate(pt)
    phi_f, grad_f = sdf_false.evaluate(pt)

    assert abs(phi_t) < 1e-12
    assert abs(phi_f) < 1e-12
    assert np.allclose(grad_t, [0.0, 0.0, 1.0])
    assert np.allclose(grad_f, [0.0, 0.0, -1.0])


def test_cad_mesh_sdf_scale_sensitivity_micro_elements_fixed():
    """Verify sub-millimeter elements (L <= 0.1 mm) project without distortion."""
    L = 1e-4
    v0 = np.array([0.0, 0.0, 0.0])
    v1 = np.array([L, 0.0, 0.0])
    v2 = np.array([L / 2.0, L * np.sqrt(3.0) / 2.0, 0.0])
    mesh = SurfaceMesh(vertices=np.array([v0, v1, v2]), faces=np.array([[0, 1, 2]]))
    sdf = CADMeshSDF(mesh, k_candidates=1)

    centroid = (v0 + v1 + v2) / 3.0
    q = np.array([centroid[0], centroid[1], 0.1 * L])
    phi_q, grad_q = sdf.evaluate(q)

    assert abs(phi_q - 0.1 * L) / (0.1 * L) < 1e-5
    assert np.isclose(grad_q[2], 1.0, atol=1e-5)


def test_cad_mesh_sdf_extreme_coords_overflow_guarded():
    """Verify extreme coordinates (>= 1e160) do not crash with IndexError."""
    mesh = SurfaceMesh(
        vertices=np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]]),
        faces=np.array([[0, 1, 2]]),
    )
    sdf = CADMeshSDF(mesh, k_candidates=1)
    phi, grad = sdf.evaluate(np.array([1e160, 0.0, 0.0]))
    assert np.isfinite(phi)
    assert np.isclose(np.linalg.norm(grad), 1.0, atol=1e-10)


def test_cad_feature_filter_strict_manifold_and_vertex_welding():
    """Verify non-manifold fin is pruned and split seams are welded."""
    # Cube with duplicate vertices and an attached fin
    cube = SurfaceMesh(
        vertices=np.array([
            [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
            [0.5, -0.5, 0.5],  # fin vertex 8
        ]),
        faces=np.array([
            [0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
            [0, 1, 5], [0, 5, 4], [2, 3, 7], [2, 7, 6],
            [0, 4, 7], [0, 7, 3], [1, 2, 6], [1, 6, 5],
            [0, 1, 8],  # non-manifold fin sharing edge (0, 1)
        ]),
    )
    clean = CADFeatureFilter.sanitize(cube)
    assert len(clean.faces) == 12  # fin pruned
    assert len(clean.vertices) == 8
```

---

## 5. Updates for `tests/test_adversarial_m1.py`

In `tests/test_adversarial_m1.py`, 4 tests originally verified the presence of the vulnerabilities. Update their assertions to verify the fixed behavior:

1. In `test_cad_feature_filter_non_manifold_edges_vulnerability`:
   Change lines 138-140 to:
   ```python
   assert len(clean_mesh.faces) == 12
   assert clean_report.is_manifold is True
   assert clean_report.multi_edges_count == 0
   assert clean_report.is_watertight is True
   assert clean_report.euler_characteristic == 2
   ```

2. In `test_cad_feature_filter_duplicate_vertices_vulnerability`:
   Change lines 163-166 to:
   ```python
   assert len(clean_mesh.faces) == 12
   assert clean_report.is_watertight is True
   assert clean_report.open_edges_count == 0
   assert clean_report.euler_characteristic == 2
   ```

3. In `test_cad_mesh_sdf_scale_sensitivity_micro_elements`:
   Change lines 244-246 to:
   ```python
   assert rel_err < 1e-5, f"Expected micro scale accuracy: rel_err={rel_err}"
   assert pytest.approx(grad_q[2], abs=1e-5) == 1.0
   ```

4. In `test_cad_mesh_sdf_extreme_large_coords_overflow`:
   Change lines 273-275 to:
   ```python
   phi_ext, grad_ext = sdf.evaluate(np.array([1e160, 0.0, 0.0]))
   assert np.isfinite(phi_ext)
   assert pytest.approx(np.linalg.norm(grad_ext), abs=1e-10) == 1.0
   ```
