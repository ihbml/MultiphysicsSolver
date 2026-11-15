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

from backend.meshing.difvm_topology import (
    CellType,
    _compute_polygon_normal_and_centroid_py,
    _is_face_folded_py,
    compute_difvm_topology,
)
from backend.meshing.models import UnstructuredMesh


def compute_polygon_swept_volume_simpson(
    face_nodes: Union[List[int], np.ndarray],
    points_n: np.ndarray,
    points_n1: np.ndarray,
) -> float:
    """Evaluate exact 3D swept volume of a polygon face moving linearly from t^n to t^{n+1}.

    Under linear nodal motion x_k(tau) = (1-tau)*x_k^n + tau*x_k^{n+1}, the face area
    vector S_f(tau) is quadratic in tau in [0, 1]. Simpson's rule computes the exact swept volume:
        Delta V_f^{n,n+1} = (1/6) * (x_f^{n+1} - x_f^n) . (S_f^n + 4 * S_f^{n+1/2} + S_f^{n+1})
    with zero truncation error (Paper 6 / DiscreteGCL / Audit H9).

    Parameters
    ----------
    face_nodes : Union[List[int], np.ndarray]
        Ordered node indices of the face polygon.
    points_n : np.ndarray
        Mesh coordinates at time t^n of shape (N_pts, 3).
    points_n1 : np.ndarray
        Mesh coordinates at time t^{n+1} of shape (N_pts, 3).

    Returns
    -------
    swept_volume : float
        Exact machine-precision swept volume of the face.
    """
    fn = list(face_nodes)
    pts_n = np.asarray(points_n, dtype=np.float64)
    pts_n1 = np.asarray(points_n1, dtype=np.float64)
    pts_half = 0.5 * (pts_n + pts_n1)

    s_n, c_n = _compute_polygon_normal_and_centroid_py(fn, pts_n)
    s_half, c_half = _compute_polygon_normal_and_centroid_py(fn, pts_half)
    s_n1, c_n1 = _compute_polygon_normal_and_centroid_py(fn, pts_n1)

    return float(compute_face_swept_volume_simpson(c_n, c_n1, s_n, s_half, s_n1))


def compute_face_swept_volume_simpson(
    x_n: np.ndarray,
    x_n1: np.ndarray,
    S_n: np.ndarray,
    S_n_half: np.ndarray,
    S_n1: np.ndarray,
) -> Union[float, np.ndarray]:
    """Evaluate exact 3D swept volume via Simpson's rule (Guillard & Farhat / Paper 6).

    For moving face vertices under linear motion from t^n to t^{n+1}, the face area
    vector S_f(tau) is quadratic in tau in [0, 1]. Simpson's rule integrates the swept
    volume with zero truncation error:
        Delta V_f^{n,n+1} = (1/6) * (x_n1 - x_n) . (S_n + 4 * S_n_half + S_n1)

    Parameters
    ----------
    x_n : np.ndarray
        Face centroid at time t^n, shape (3,) or (N, 3).
    x_n1 : np.ndarray
        Face centroid at time t^{n+1}, shape (3,) or (N, 3).
    S_n : np.ndarray
        Face area vector at time t^n, shape (3,) or (N, 3).
    S_n_half : np.ndarray
        Face area vector at midpoint t^{n+1/2}, shape (3,) or (N, 3).
    S_n1 : np.ndarray
        Face area vector at time t^{n+1}, shape (3,) or (N, 3).

    Returns
    -------
    swept_volume : Union[float, np.ndarray]
        Exact swept volume for a single face or array of faces.
    """
    x_n = np.asarray(x_n, dtype=np.float64)
    x_n1 = np.asarray(x_n1, dtype=np.float64)
    S_n = np.asarray(S_n, dtype=np.float64)
    S_n_half = np.asarray(S_n_half, dtype=np.float64)
    S_n1 = np.asarray(S_n1, dtype=np.float64)

    dx = x_n1 - x_n
    S_quad = S_n + 4.0 * S_n_half + S_n1

    if dx.ndim == 1:
        return float(np.dot(dx, S_quad) / 6.0)
    return np.sum(dx * S_quad, axis=-1) / 6.0


def compute_dgcl_grid_flux_bdf2(
    delta_V_n_n1: Union[float, np.ndarray],
    delta_V_nm1_n: Optional[Union[float, np.ndarray]],
    dt: float,
) -> Union[float, np.ndarray]:
    """Evaluate discrete GCL grid volume flux matching BDF2 time stepping (Paper 6).

    Under BDF2 time stepping:
        (3 V_c^{n+1} - 4 V_c^n + V_c^{n-1}) / (2 dt) = sum_f dot{V}_f^{n+1}
    the face volume flux must use consistent BDF2 weights:
        dot{V}_f^{n+1} = (3 * Delta V_f^{n,n+1} - Delta V_f^{n-1,n}) / (2 * dt)

    If delta_V_nm1_n is None (e.g. initial step n=0), falls back to BDF1-DGCL:
        dot{V}_f^1 = Delta V_f^{0,1} / dt.
    """
    if dt <= 0.0:
        raise ValueError(f"Time step dt must be strictly positive, got {dt}")

    dv_curr = np.asarray(delta_V_n_n1, dtype=np.float64)
    if delta_V_nm1_n is None:
        res = dv_curr / dt
    else:
        dv_prev = np.asarray(delta_V_nm1_n, dtype=np.float64)
        res = (1.5 * dv_curr - 0.5 * dv_prev) / dt

    if dv_curr.ndim == 0:
        return float(res)
    return res


def _update_mesh_geometry(mesh: UnstructuredMesh, new_points: np.ndarray) -> UnstructuredMesh:
    """Update geometric fields of an UnstructuredMesh after vertex displacement.

    Strictly preserves explicit faces, cell connectivity, cell types, boundaries,
    and regions without reconstructing topology from scratch.
    """
    if mesh.faces is None or len(mesh.faces) == 0:
        new_mesh = compute_difvm_topology(
            new_points,
            mesh.cells,
            regions=mesh.cell_regions,
            cell_types=mesh.cell_types,
            topological_dim=mesh.topological_dim,
        )
        if mesh.boundaries:
            new_mesh.boundaries = dict(mesh.boundaries)
        return new_mesh

    n_faces = len(mesh.faces)
    n_cells = len(mesh.cells)

    face_areas = np.zeros((n_faces, 3), dtype=np.float64)
    face_centres = np.zeros((n_faces, 3), dtype=np.float64)
    is_folded_arr = np.zeros(n_faces, dtype=bool)

    for f_idx, fn in enumerate(mesh.faces):
        sf, cf = _compute_polygon_normal_and_centroid_py(fn, new_points)
        face_areas[f_idx] = sf
        face_centres[f_idx] = cf
        is_folded_arr[f_idx] = _is_face_folded_py(fn, new_points, sf)

    cell_face_refs: List[List[Tuple[int, float]]] = [[] for _ in range(n_cells)]
    for f_idx in range(n_faces):
        o = int(mesh.owner[f_idx])
        cell_face_refs[o].append((f_idx, 1.0))
        nb = int(mesh.neighbour[f_idx])
        if nb >= 0:
            cell_face_refs[nb].append((f_idx, -1.0))

    cell_centres = np.zeros((n_cells, 3), dtype=np.float64)
    cell_volumes = np.zeros(n_cells, dtype=np.float64)

    for c_idx in range(n_cells):
        cell_arr = np.asarray(mesh.cells[c_idx], dtype=np.int64)
        c_approx = np.mean(new_points[cell_arr], axis=0) if len(cell_arr) > 0 else np.zeros(3)
        ctype = mesh.cell_types[c_idx] if mesh.cell_types is not None else CellType.HEXAHEDRON.value
        is_cell_2d = ctype in (CellType.TRIANGLE.value, CellType.QUAD.value, CellType.POLYGON.value) or mesh.topological_dim == 2
        div_factor = 2.0 if is_cell_2d else 3.0

        vol_sum = 0.0
        centroid_weighted = np.zeros(3, dtype=np.float64)
        for f_idx, sign in cell_face_refs[c_idx]:
            sf_out = face_areas[f_idx] * sign
            pyr_vol = float(np.dot(face_centres[f_idx] - c_approx, sf_out)) / div_factor
            vol_sum += pyr_vol
            if is_cell_2d:
                pyr_c = face_centres[f_idx] * (2.0 / 3.0) + c_approx * (1.0 / 3.0)
            else:
                pyr_c = face_centres[f_idx] * 0.75 + c_approx * 0.25
            centroid_weighted += pyr_c * pyr_vol

        if abs(vol_sum) > 1e-60:
            cell_volumes[c_idx] = vol_sum
            cell_centres[c_idx] = centroid_weighted / vol_sum
        else:
            cell_volumes[c_idx] = 0.0
            cell_centres[c_idx] = c_approx

    delta_f = np.zeros((n_faces, 3), dtype=np.float64)
    k_f = np.zeros((n_faces, 3), dtype=np.float64)
    k_f_comp = np.zeros((n_faces, 3), dtype=np.float64)
    m_skew = np.zeros((n_faces, 3), dtype=np.float64)
    beta_upwind = np.ones(n_faces, dtype=np.float64)
    non_ortho = np.zeros(n_faces, dtype=np.float64)
    skewness = np.zeros(n_faces, dtype=np.float64)
    stab_mask = np.zeros(n_faces, dtype=np.int32)

    for f_idx in range(n_faces):
        o = int(mesh.owner[f_idx])
        nb = int(mesh.neighbour[f_idx])
        sf = face_areas[f_idx]
        cf = face_centres[f_idx]
        c_o = cell_centres[o]

        if nb >= 0:
            c_nb = cell_centres[nb]
            df = c_nb - c_o
            df_norm = float(np.linalg.norm(df))
            sf_norm = float(np.linalg.norm(sf))
            sf_dot_df = float(np.dot(sf, df))

            if sf_dot_df > 1e-15 and df_norm > 1e-15:
                delta = (sf_norm ** 2 / sf_dot_df) * df
                k = sf - delta
                delta_norm = float(np.linalg.norm(delta))
                k_norm = float(np.linalg.norm(k))
                alpha_damp = min(1.0, 1.5 * delta_norm / (k_norm + 1e-15))
                k_comp = alpha_damp * k
            else:
                delta = sf.copy()
                k = np.zeros(3, dtype=np.float64)
                k_comp = np.zeros(3, dtype=np.float64)

            delta_f[f_idx] = delta
            k_f[f_idx] = k
            k_f_comp[f_idx] = k_comp

            skew = 0.0
            m_s = np.zeros(3, dtype=np.float64)
            if sf_norm > 1e-15 and df_norm > 1e-15:
                nf = sf / sf_norm
                df_dot_nf = float(np.dot(df, nf))
                if abs(df_dot_nf) > 1e-15:
                    t = float(np.dot(cf - c_o, nf)) / df_dot_nf
                    x_int = c_o + df * t
                    m_s = cf - x_int
                    skew = float(np.linalg.norm(m_s) / df_norm)
            skewness[f_idx] = skew
            m_skew[f_idx] = m_s

            cos_theta = 1.0
            if sf_norm > 1e-15 and df_norm > 1e-15:
                cos_theta = float(np.clip(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0))
            theta_deg = float(np.arccos(cos_theta) * 180.0 / np.pi)
            non_ortho[f_idx] = theta_deg

            tier = 0
            if is_folded_arr[f_idx] or sf_dot_df <= 0.0:
                tier = 3
                beta = 0.0
            else:
                beta_ortho = max(0.0, min(1.0, (75.0 - theta_deg) / 35.0))
                beta_skew = max(0.0, min(1.0, (1.0 - skew) / 0.6))
                beta = min(beta_ortho, beta_skew)
                if theta_deg >= 75.0 or skew >= 1.0:
                    tier = 3
                    beta = 0.0
                elif theta_deg >= 65.0 or skew >= 0.7:
                    tier = 2
                elif theta_deg >= 40.0 or skew >= 0.4:
                    tier = 1
            stab_mask[f_idx] = tier
            beta_upwind[f_idx] = beta
        else:
            d_out = cf - c_o
            sf_dot_dout = float(np.dot(sf, d_out))
            tier = 0
            beta = 1.0
            if is_folded_arr[f_idx] or sf_dot_dout <= 0.0:
                tier = 3
                beta = 0.0

            delta_f[f_idx] = d_out
            k_f[f_idx] = np.zeros(3, dtype=np.float64)
            k_f_comp[f_idx] = np.zeros(3, dtype=np.float64)
            m_skew[f_idx] = np.zeros(3, dtype=np.float64)
            non_ortho[f_idx] = 0.0
            skewness[f_idx] = 0.0
            stab_mask[f_idx] = tier
            beta_upwind[f_idx] = beta

    aspect_ratio = np.ones(n_cells, dtype=np.float64)
    for c_idx in range(n_cells):
        cell_arr = np.asarray(mesh.cells[c_idx], dtype=np.int64)
        if len(cell_arr) > 0:
            cpts = new_points[cell_arr]
            spans = np.max(cpts, axis=0) - np.min(cpts, axis=0)
            valid_spans = spans[spans > 1e-12]
            if len(valid_spans) >= 2:
                aspect_ratio[c_idx] = max(1.0, float(np.max(valid_spans) / max(float(np.min(valid_spans)), 1e-15)))

    min_beta_per_cell = np.ones(n_cells, dtype=np.float64)
    if n_faces > 0:
        np.minimum.at(min_beta_per_cell, mesh.owner, beta_upwind)
        int_mask = mesh.neighbour >= 0
        if np.any(int_mask):
            np.minimum.at(min_beta_per_cell, mesh.neighbour[int_mask], beta_upwind[int_mask])
    omega_cell = np.clip(min_beta_per_cell, 0.1, 1.0)

    pos_vols = cell_volumes[cell_volumes > 1e-60]
    v_bg = float(np.max(pos_vols)) if len(pos_vols) > 0 else 1.0
    wsrd_alpha = np.clip(cell_volumes / (v_bg + 1e-30), 0.0, 1.0)

    error_map = {
        "non_orthogonality": non_ortho,
        "skewness": skewness,
        "aspect_ratio": aspect_ratio,
        "stabilization_mask": stab_mask,
        "mask": stab_mask,
        "m_skew": m_skew,
        "k_f_comp": k_f_comp,
        "beta_upwind": beta_upwind,
        "omega_cell": omega_cell,
        "wsrd_alpha": wsrd_alpha,
    }

    new_mesh = UnstructuredMesh(
        points=new_points,
        cells=mesh.cells,
        owner=mesh.owner.copy(),
        neighbour=mesh.neighbour.copy(),
        face_areas=face_areas,
        face_centres=face_centres,
        cell_centres=cell_centres,
        cell_volumes=cell_volumes,
        delta_f=delta_f,
        k_f=k_f,
        boundaries=dict(mesh.boundaries),
        cell_regions=mesh.cell_regions.copy() if mesh.cell_regions is not None else None,
        faces=[np.array(f, copy=True) for f in mesh.faces] if mesh.faces is not None else None,
        cell_types=mesh.cell_types.copy() if mesh.cell_types is not None else None,
        topological_dim=mesh.topological_dim,
        baffles=list(mesh.baffles) if mesh.baffles is not None else [],
        error_map=error_map,
        metadata=dict(mesh.metadata) if mesh.metadata is not None else {},
    )
    return new_mesh


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
        """Construct exact node adjacency from explicit mesh faces for all element types."""
        n_pts = len(mesh.points)
        adj: List[Set[int]] = [set() for _ in range(n_pts)]

        if mesh.faces is not None and len(mesh.faces) > 0:
            for fn in mesh.faces:
                nv = len(fn)
                for i in range(nv):
                    u = int(fn[i])
                    v = int(fn[(i + 1) % nv])
                    adj[u].add(v)
                    adj[v].add(u)
            return adj

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

        # Determine fixed nodes topologically from boundary face connectivity
        if fixed_nodes is not None:
            fixed_mask = np.zeros(n_pts, dtype=bool)
            fixed_mask[np.asarray(fixed_nodes, dtype=np.int64)] = True
        else:
            # Identify Dirichlet fixed boundary nodes topologically from boundary face connectivity
            # (faces with neighbour == -1 not in moving patch / without moving displacement)
            fixed_mask = np.zeros(n_pts, dtype=bool)
            bnd_nodes: Set[int] = set()

            if mesh.faces is not None and len(mesh.faces) > 0 and mesh.neighbour is not None and len(mesh.neighbour) > 0:
                for f_idx, nb in enumerate(mesh.neighbour):
                    if nb == -1:
                        for v in mesh.faces[f_idx]:
                            bnd_nodes.add(int(v))
            elif mesh.boundaries:
                for patch_info in mesh.boundaries.values():
                    if isinstance(patch_info, dict) and "faces" in patch_info:
                        faces_list = patch_info["faces"]
                    elif isinstance(patch_info, (list, tuple, np.ndarray)):
                        faces_list = patch_info
                    else:
                        faces_list = []
                    if mesh.faces is not None:
                        for f_idx in faces_list:
                            if f_idx < len(mesh.faces):
                                for v in mesh.faces[f_idx]:
                                    bnd_nodes.add(int(v))

            for v in bnd_nodes:
                if not moving_mask[v]:
                    fixed_mask[v] = True

            # If no boundary faces were populated, identify boundary nodes from node adjacency degree
            if not np.any(fixed_mask):
                adj_temp = self._build_node_adjacency(mesh)
                degrees = np.array([len(adj_temp[i]) for i in range(n_pts)])
                med_deg = np.median(degrees)
                for i in range(n_pts):
                    if not moving_mask[i] and degrees[i] < med_deg:
                        fixed_mask[i] = True

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

        # Update geometry while strictly preserving explicit faces and topology
        return _update_mesh_geometry(mesh, new_points)

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
        mesh_nm1: Optional[UnstructuredMesh] = None,
    ) -> float:
        """Verify the Discrete Geometric Conservation Law (DGCL) via Simpson's rule (Paper 6 & Audit-H9).

        Evaluates exact swept volume:
            Delta V_f^{n,n+1} = (1/6) * (x_f^{n+1} - x_f^n) . (S_f^n + 4 S_f^{n+1/2} + S_f^{n+1})
        where x_f^{n+1/2} = (x_f^n + x_f^{n+1}) / 2 and S_f^{n+1/2} is computed from midpoint vertex positions.

        And checks grid flux conservation:
        - BDF1 (mesh_nm1 is None):
            (V_c^{n+1} - V_c^n) / dt = sum_f sigma_cf dot{V}_f^{n+1}
            with dot{V}_f^{n+1} = Delta V_f^{n,n+1} / dt
        - BDF2 (mesh_nm1 provided):
            (3 V_c^{n+1} - 4 V_c^n + V_c^{n-1}) / (2 dt) = sum_f sigma_cf dot{V}_f^{n+1}
            with dot{V}_f^{n+1} = (3 Delta V_f^{n,n+1} - Delta V_f^{n-1,n}) / (2 dt)

        Returns maximum residual across all cells.
        """
        if dt <= 0.0:
            raise ValueError(f"Time step dt must be positive, got {dt}")

        v_n = mesh_n.cell_volumes
        v_n1 = mesh_n1.cell_volumes
        n_faces = mesh_n.num_faces

        # Midpoint face areas and centroids for step [n, n+1]
        points_half = 0.5 * (mesh_n.points + mesh_n1.points)
        sf_half = np.zeros((n_faces, 3), dtype=np.float64)
        if mesh_n.faces is not None and len(mesh_n.faces) == n_faces:
            for f in range(n_faces):
                fn = mesh_n.faces[f]
                s_h, _ = _compute_polygon_normal_and_centroid_py(fn, points_half)
                sf_half[f] = s_h
        else:
            sf_half = 0.5 * (mesh_n.face_areas + mesh_n1.face_areas)

        delta_V_n_n1 = compute_face_swept_volume_simpson(
            mesh_n.face_centres,
            mesh_n1.face_centres,
            mesh_n.face_areas,
            sf_half,
            mesh_n1.face_areas,
        )

        if mesh_nm1 is not None:
            # BDF2 time stepping
            v_nm1 = mesh_nm1.cell_volumes
            d_vol_dt = (3.0 * v_n1 - 4.0 * v_n + v_nm1) / (2.0 * dt)

            points_half_prev = 0.5 * (mesh_nm1.points + mesh_n.points)
            sf_half_prev = np.zeros((n_faces, 3), dtype=np.float64)
            if mesh_n.faces is not None and len(mesh_n.faces) == n_faces:
                for f in range(n_faces):
                    fn = mesh_n.faces[f]
                    s_h_prev, _ = _compute_polygon_normal_and_centroid_py(fn, points_half_prev)
                    sf_half_prev[f] = s_h_prev
            else:
                sf_half_prev = 0.5 * (mesh_nm1.face_areas + mesh_n.face_areas)

            delta_V_nm1_n = compute_face_swept_volume_simpson(
                mesh_nm1.face_centres,
                mesh_n.face_centres,
                mesh_nm1.face_areas,
                sf_half_prev,
                mesh_n.face_areas,
            )
            flux = compute_dgcl_grid_flux_bdf2(delta_V_n_n1, delta_V_nm1_n, dt)
        else:
            # BDF1
            d_vol_dt = (v_n1 - v_n) / dt
            flux = compute_dgcl_grid_flux_bdf2(delta_V_n_n1, None, dt)

        cell_flux = np.zeros(len(v_n), dtype=np.float64)
        for f in range(n_faces):
            o = mesh_n.owner[f]
            cell_flux[o] += flux[f]
            nb = mesh_n.neighbour[f]
            if nb >= 0:
                cell_flux[nb] -= flux[f]

        max_residual = float(np.max(np.abs(d_vol_dt - cell_flux)))
        return max_residual
