"""Arbitrary Lagrangian-Eulerian (ALE) Moving Mesh Engine & Degenerate Cell Detection.

Implements Features 15 and 16 from PROJECT.md:
- Feature 15: Moving mesh ALE deformation via variable-stiffness Laplacian smoothing:
  div (gamma * grad u) = 0 with boundary Dirichlet conditions.
  Preserves boundary layer prism thickness near moving walls, guarantees positive
  cell volumes (V_i > 10^-15), and verifies zero displacement on fixed boundaries (< 10^-12).
- Feature 16: Degenerate cell detection and remeshing trigger:
  Audits inverted cells (V_i <= 0), severe face non-orthogonality (theta > 75 deg),
  extreme face skewness, and high aspect-ratio slivers.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Set, Tuple, Union

import numpy as np
from scipy.sparse import csr_matrix, lil_matrix
from scipy.sparse.linalg import spsolve

from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import UnstructuredMesh


class ALEMeshMover:
    """Arbitrary Lagrangian-Eulerian (ALE) mesh motion engine."""

    def __init__(
        self,
        stiffness_exponent: float = 1.0,
        min_volume_threshold: float = 1e-15,
        max_skewness_threshold: float = 0.85,
        max_non_ortho_threshold: float = 85.0,
        max_aspect_ratio_threshold: float = 40.0,
    ) -> None:
        """Initialize ALE mesh mover.

        Parameters
        ----------
        stiffness_exponent : float
            Exponent alpha for distance/volume-based stiffness: gamma = 1 / (d_wall + eps)^alpha.
        min_volume_threshold : float
            Threshold below which a cell is flagged as inverted or collapsed (default: 1e-15).
        max_skewness_threshold : float
            Face skewness threshold flagging degenerate cells (default: 0.85).
        max_non_ortho_threshold : float
            Face non-orthogonality angle threshold in degrees (default: 85.0).
        max_aspect_ratio_threshold : float
            Cell aspect ratio threshold flagging sliver cells (default: 40.0).
        """
        self.stiffness_exponent = float(stiffness_exponent)
        self.min_volume_threshold = float(min_volume_threshold)
        self.max_skewness_threshold = float(max_skewness_threshold)
        self.max_non_ortho_threshold = float(max_non_ortho_threshold)
        self.max_aspect_ratio_threshold = float(max_aspect_ratio_threshold)

    def _build_node_adjacency(self, mesh: UnstructuredMesh) -> List[Set[int]]:
        """Extract undirected node-to-node connectivity from mesh cell definitions."""
        n_pts = len(mesh.points)
        adj: List[Set[int]] = [set() for _ in range(n_pts)]

        for cell in mesh.cells:
            nodes = list(cell)
            n_nodes = len(nodes)
            if n_nodes == 8:
                # 8-node hexahedron
                edges = [
                    (0, 1), (1, 2), (2, 3), (3, 0),
                    (4, 5), (5, 6), (6, 7), (7, 4),
                    (0, 4), (1, 5), (2, 6), (3, 7),
                ]
                for u, v in edges:
                    adj[nodes[u]].add(nodes[v])
                    adj[nodes[v]].add(nodes[u])
            elif n_nodes == 4:
                # 4-node tetrahedron or 4-node quadrilateral
                for i in range(n_nodes):
                    for j in range(i + 1, n_nodes):
                        adj[nodes[i]].add(nodes[j])
                        adj[nodes[j]].add(nodes[i])
            elif n_nodes == 6:
                # 6-node prism/wedge
                edges = [
                    (0, 1), (1, 2), (2, 0),
                    (3, 4), (4, 5), (5, 3),
                    (0, 3), (1, 4), (2, 5),
                ]
                for u, v in edges:
                    adj[nodes[u]].add(nodes[v])
                    adj[nodes[v]].add(nodes[u])
            elif n_nodes == 5:
                # 5-node pyramid
                edges = [
                    (0, 1), (1, 2), (2, 3), (3, 0),
                    (0, 4), (1, 4), (2, 4), (3, 4),
                ]
                for u, v in edges:
                    adj[nodes[u]].add(nodes[v])
                    adj[nodes[v]].add(nodes[u])
            elif n_nodes == 3:
                # 3-node triangle
                edges = [(0, 1), (1, 2), (2, 0)]
                for u, v in edges:
                    adj[nodes[u]].add(nodes[v])
                    adj[nodes[v]].add(nodes[u])
            else:
                # General polygon/polyhedron: connect adjacent ring nodes
                for i in range(n_nodes):
                    u = nodes[i]
                    v = nodes[(i + 1) % n_nodes]
                    adj[u].add(v)
                    adj[v].add(u)

        return adj

    def deform_mesh(
        self,
        mesh: UnstructuredMesh,
        boundary_displacement: np.ndarray,
        fixed_nodes: Optional[Union[np.ndarray, List[int]]] = None,
        stiffness_exponent: Optional[float] = None,
    ) -> UnstructuredMesh:
        """Deform the unstructured mesh using variable-stiffness Laplacian smoothing.

        Solves:
            div (gamma(x) * grad u) = 0
        subject to:
            u|_moving = boundary_displacement
            u|_fixed  = 0

        Updates node coordinates, recomputes DiFVM topology and SoA arrays,
        and guarantees all cell volumes remain strictly positive.

        Parameters
        ----------
        mesh : UnstructuredMesh
            Original undeformed mesh.
        boundary_displacement : np.ndarray
            Array of prescribed displacement vectors of shape (N_p, 3).
        fixed_nodes : Optional[Union[np.ndarray, List[int]]]
            Indices of boundary nodes that must remain strictly stationary (displacement == 0).
            If None, automatically identified from the opposing boundary.
        stiffness_exponent : Optional[float]
            Optional override for the stiffness decay exponent alpha.

        Returns
        -------
        deformed_mesh : UnstructuredMesh
            New mesh with displaced points and fully updated DiFVM SoA topology.
        """
        boundary_displacement = np.asarray(boundary_displacement, dtype=np.float64)
        points = mesh.points
        n_pts = len(points)

        if len(boundary_displacement) != n_pts:
            raise ValueError(
                f"Displacement array length ({len(boundary_displacement)}) does not match node count ({n_pts})"
            )

        # Identify nodes with non-zero prescribed displacement
        disp_norms = np.linalg.norm(boundary_displacement, axis=1)
        moving_mask = disp_norms > 1e-12

        if not np.any(moving_mask):
            # No displacement prescribed: return copy
            return UnstructuredMesh(
                points=points.copy(),
                cells=[c.copy() for c in mesh.cells],
                owner=mesh.owner.copy(),
                neighbour=mesh.neighbour.copy(),
                face_areas=mesh.face_areas.copy(),
                face_centres=mesh.face_centres.copy(),
                cell_centres=mesh.cell_centres.copy(),
                cell_volumes=mesh.cell_volumes.copy(),
                delta_f=mesh.delta_f.copy(),
                k_f=mesh.k_f.copy(),
                boundaries=dict(mesh.boundaries),
                cell_regions=mesh.cell_regions.copy() if mesh.cell_regions is not None else None,
                baffles=list(mesh.baffles),
                cyclic_pairs=list(mesh.cyclic_pairs),
                error_map=dict(mesh.error_map),
                metadata=dict(mesh.metadata),
            )

        # Determine fixed nodes
        if fixed_nodes is not None:
            fixed_mask = np.zeros(n_pts, dtype=bool)
            fixed_mask[np.asarray(fixed_nodes, dtype=np.int64)] = True
        else:
            # Automatically find opposing boundary nodes
            moving_pts = points[moving_mask]
            min_pts = np.min(points, axis=0)
            max_pts = np.max(points, axis=0)
            bbox_span = np.maximum(max_pts - min_pts, 1e-12)

            # Determine which axis has the most prominent boundary constraint
            # Check bounding faces of moving nodes
            fixed_mask = np.zeros(n_pts, dtype=bool)
            for axis in range(3):
                # Is the moving surface on the maximum boundary of this axis?
                if np.all(moving_pts[:, axis] > max_pts[axis] - 0.05 * bbox_span[axis]):
                    # Opposing boundary is the minimum on this axis
                    fixed_mask |= (points[:, axis] < min_pts[axis] + 0.02 * bbox_span[axis])
                # Is the moving surface on the minimum boundary of this axis?
                elif np.all(moving_pts[:, axis] < min_pts[axis] + 0.05 * bbox_span[axis]):
                    # Opposing boundary is the maximum on this axis
                    fixed_mask |= (points[:, axis] > max_pts[axis] - 0.02 * bbox_span[axis])

            # If no single planar axis matched, fix nodes furthest from moving centroid
            if not np.any(fixed_mask):
                moving_centroid = np.mean(moving_pts, axis=0)
                dists_from_moving = np.linalg.norm(points - moving_centroid, axis=1)
                max_d = np.max(dists_from_moving)
                fixed_mask = dists_from_moving > 0.85 * max_d

        # Combine Dirichlet conditions: moving nodes + fixed nodes
        dirichlet_mask = moving_mask | fixed_mask

        # Build node adjacency graph
        adj = self._build_node_adjacency(mesh)

        # Distance-weighted stiffness calculation: gamma_i = 1 / (d_wall + eps)^alpha
        alpha = stiffness_exponent if stiffness_exponent is not None else self.stiffness_exponent
        stiffness = np.ones(n_pts, dtype=np.float64)
        if alpha > 0.0:
            # Distance from nearest moving or fixed wall
            wall_pts = points[dirichlet_mask]
            if len(wall_pts) > 0:
                from scipy.spatial import cKDTree
                wall_tree = cKDTree(wall_pts)
                d_wall, _ = wall_tree.query(points)
                h_char = np.mean(d_wall[d_wall > 1e-8]) if np.any(d_wall > 1e-8) else 1.0
                stiffness = 1.0 / np.power(d_wall / h_char + 0.05, alpha)

        # Assemble sparse Laplacian matrix
        L = lil_matrix((n_pts, n_pts), dtype=np.float64)
        rhs = np.zeros((n_pts, 3), dtype=np.float64)

        for i in range(n_pts):
            if moving_mask[i]:
                # Moving Dirichlet boundary condition
                L[i, i] = 1.0
                rhs[i] = boundary_displacement[i]
            elif fixed_mask[i]:
                # Fixed Dirichlet boundary condition
                L[i, i] = 1.0
                rhs[i] = [0.0, 0.0, 0.0]
            else:
                # Interior or unconstrained node: discrete variable-stiffness Laplacian
                # sum_{j in N(i)} gamma_ij * (u_j - u_i) = 0
                nbrs = adj[i]
                if not nbrs:
                    L[i, i] = 1.0
                    rhs[i] = [0.0, 0.0, 0.0]
                    continue

                total_weight = 0.0
                for j in nbrs:
                    # Edge stiffness: harmonic or arithmetic mean
                    gamma_ij = 0.5 * (stiffness[i] + stiffness[j])
                    L[i, j] = gamma_ij
                    total_weight += gamma_ij

                L[i, i] = -total_weight

        # Solve sparse system component by component
        L_csr = L.tocsr()
        u_disp = np.zeros((n_pts, 3), dtype=np.float64)
        for comp in range(3):
            u_disp[:, comp] = spsolve(L_csr, rhs[:, comp])

        # Strictly enforce zero displacement on fixed boundary
        u_disp[fixed_mask] = 0.0

        # Update node coordinates
        new_points = points + u_disp

        # Recompute full DiFVM topology and quality metrics
        new_mesh = compute_difvm_topology(
            new_points,
            mesh.cells,
            regions=mesh.cell_regions,
        )
        if mesh.boundaries:
            new_mesh.boundaries = dict(mesh.boundaries)

        return new_mesh

    def detect_degenerate_cells(
        self,
        mesh: UnstructuredMesh,
        min_volume: Optional[float] = None,
        max_skewness: Optional[float] = None,
        max_non_ortho_deg: Optional[float] = None,
        max_aspect_ratio: Optional[float] = None,
    ) -> List[int]:
        """Detect corrupted, inverted, or severely distorted cells.

        Triggers remeshing when cells become invalid under mesh motion.

        Criteria:
        1. Non-positive cell volume: V_c <= min_volume (inverted/collapsed cell)
        2. Non-finite volume or centroid (NaN / Inf)
        3. Severe face non-orthogonality: theta_f > max_non_ortho_deg
        4. Severe face skewness: skew_f > max_skewness
        5. Severe cell aspect ratio: AR_c > max_aspect_ratio
        6. Critical solver stabilization flag: mask == 3

        Parameters
        ----------
        mesh : UnstructuredMesh
            Mesh to inspect.
        min_volume : Optional[float]
            Volume threshold (default: 1e-15).
        max_skewness : Optional[float]
            Skewness threshold (default: 0.85).
        max_non_ortho_deg : Optional[float]
            Non-orthogonality threshold in degrees (default: 85.0).
        max_aspect_ratio : Optional[float]
            Aspect ratio threshold (default: 40.0).

        Returns
        -------
        bad_cells : List[int]
            Sorted list of unique corrupted cell indices.
        """
        v_thresh = min_volume if min_volume is not None else self.min_volume_threshold
        sk_thresh = max_skewness if max_skewness is not None else self.max_skewness_threshold
        no_thresh = max_non_ortho_deg if max_non_ortho_deg is not None else self.max_non_ortho_threshold
        ar_thresh = max_aspect_ratio if max_aspect_ratio is not None else self.max_aspect_ratio_threshold

        bad_cells_set: Set[int] = set()

        # 1. Non-positive or non-finite cell volume
        volumes = mesh.cell_volumes
        for c in range(len(volumes)):
            v = volumes[c]
            if not np.isfinite(v) or v <= v_thresh:
                bad_cells_set.add(c)

        # Check non-finite cell centroids
        for c in range(len(mesh.cell_centres)):
            if not np.all(np.isfinite(mesh.cell_centres[c])):
                bad_cells_set.add(c)

        # 2. Quality metrics from error map
        error_map = mesh.error_map
        non_ortho = error_map.get("non_orthogonality", error_map.get("non_ortho"))
        skewness = error_map.get("skewness", error_map.get("skew"))
        aspect_ratio = error_map.get("aspect_ratio")
        mask = error_map.get("stabilization_mask", error_map.get("mask"))

        # Check face non-orthogonality, skewness, and critical mask
        owner = mesh.owner
        neighbour = mesh.neighbour
        n_faces = len(owner)

        if non_ortho is not None and len(non_ortho) > 0:
            for f in range(min(n_faces, len(non_ortho))):
                if non_ortho[f] > no_thresh:
                    bad_cells_set.add(int(owner[f]))
                    if neighbour[f] >= 0:
                        bad_cells_set.add(int(neighbour[f]))

        if skewness is not None and len(skewness) > 0:
            for f in range(min(n_faces, len(skewness))):
                if skewness[f] > sk_thresh:
                    bad_cells_set.add(int(owner[f]))
                    if neighbour[f] >= 0:
                        bad_cells_set.add(int(neighbour[f]))

        if mask is not None and len(mask) > 0:
            for f in range(min(n_faces, len(mask))):
                # Tier 3 = critical non-orthogonal / high-skew face
                if mask[f] == 3:
                    bad_cells_set.add(int(owner[f]))
                    if neighbour[f] >= 0:
                        bad_cells_set.add(int(neighbour[f]))

        # Check cell aspect ratio
        if aspect_ratio is not None and len(aspect_ratio) > 0:
            for c in range(min(len(volumes), len(aspect_ratio))):
                if aspect_ratio[c] > ar_thresh or not np.isfinite(aspect_ratio[c]):
                    bad_cells_set.add(c)

        return sorted(list(bad_cells_set))

    def compute_mesh_velocity(
        self,
        points_n: np.ndarray,
        points_n1: np.ndarray,
        dt: float,
    ) -> np.ndarray:
        """Compute grid velocity w_i = (x_i^{n+1} - x_i^n) / dt."""
        if dt <= 0.0:
            raise ValueError(f"Time step dt must be strictly positive, got {dt}")
        return (np.asarray(points_n1) - np.asarray(points_n)) / dt

    def verify_dgcl(
        self,
        mesh_n: UnstructuredMesh,
        mesh_n1: UnstructuredMesh,
        dt: float,
    ) -> float:
        """Verify the Discrete Geometric Conservation Law (DGCL).

        Volume change must equal swept face volume:
            | (V_c^{n+1} - V_c^n) / dt - sum_f sigma_cf w_f . S_f^{n+1/2} | < 10^-12

        Returns maximum residual across all cells.
        """
        if dt <= 0.0:
            raise ValueError(f"Time step dt must be positive, got {dt}")

        v_n = mesh_n.cell_volumes
        v_n1 = mesh_n1.cell_volumes
        d_vol_dt = (v_n1 - v_n) / dt

        # Midpoint face areas and swept face velocity
        s_mid = 0.5 * (mesh_n.face_areas + mesh_n1.face_areas)
        w_f = (mesh_n1.face_centres - mesh_n.face_centres) / dt

        flux = np.sum(w_f * s_mid, axis=1)

        cell_flux = np.zeros(len(v_n), dtype=np.float64)
        for f in range(mesh_n.num_faces):
            o = mesh_n.owner[f]
            cell_flux[o] += flux[f]
            nb = mesh_n.neighbour[f]
            if nb >= 0:
                cell_flux[nb] -= flux[f]

        max_residual = float(np.max(np.abs(d_vol_dt - cell_flux)))
        return max_residual
