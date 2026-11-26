"""WSRD cut-cell weights, AMIPS energy, Paper 7 symmetry operators, and §12.2 numerics."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple
import numpy as np

from backend.meshing.models import UnstructuredMesh


def compute_wsrd_weights_array(
    wsrd_alpha: np.ndarray,
    small_cell_ratio: float = 0.20,
    wsrd_target_alpha: float = 0.50,
) -> np.ndarray:
    """Compute Berger & Giuliani (2024) dynamic WSRD cell weights according to Eq. (4.2).

    w_i = sin^2(pi/2 * (alpha_i - alpha_small) / (alpha_target - alpha_small))
    for alpha_small <= alpha_i < alpha_target.
    """
    alpha = np.asarray(wsrd_alpha, dtype=np.float64)
    w = np.zeros_like(alpha)
    a_small = float(small_cell_ratio)
    a_target = float(wsrd_target_alpha)
    delta_a = a_target - a_small
    if delta_a > 1e-12:
        mid_mask = (alpha >= a_small) & (alpha < a_target)
        scaled = (alpha[mid_mask] - a_small) / delta_a
        w[mid_mask] = np.sin(0.5 * np.pi * scaled) ** 2
        w[alpha >= a_target] = 1.0
        w[alpha < a_small] = 0.0
    else:
        w[alpha >= a_target] = 1.0
    return w


def compute_wsrd_weights(
    cut_alpha: float,
    neighbor_alphas: List[float],
    alpha_target: float = 0.50,
) -> Dict[str, Any]:
    """Compute Berger & Giuliani (2024) Weighted State Redistribution weights for a single cut-cell."""
    n_neighbors = len(neighbor_alphas)
    if n_neighbors == 0:
        return {"self_weight": 1.0, "neighbor_weights": [], "partition_of_unity": 0.0}

    n_i = n_neighbors + 1
    weights_nb = []
    for a_j in neighbor_alphas:
        w_ij = (1.0 / (n_i - 1)) * max(0.0, 1.0 - a_j / alpha_target)
        weights_nb.append(w_ij)

    sum_nb = sum(weights_nb)
    w_ii = 1.0 - sum_nb
    return {
        "self_weight": w_ii,
        "neighbor_weights": weights_nb,
        "partition_of_unity": abs((w_ii + sum_nb) - 1.0),
    }


def compute_amips_energy(
    vertices: np.ndarray,
    reference_scale: float = 1.0,
) -> float:
    """Compute scale- and rotation-invariant 3D AMIPS distortion energy for a tetrahedron."""
    verts = np.asarray(vertices, dtype=np.float64)
    if len(verts) < 4:
        raise ValueError("AMIPS energy requires at least 4 vertices (tetrahedron).")
    v0, v1, v2, v3 = verts[0], verts[1], verts[2], verts[3]
    d_s = np.column_stack([v1 - v0, v2 - v0, v3 - v0])
    det_j = float(np.linalg.det(d_s))
    if det_j <= 1e-18:
        return float("inf")

    edge_sum_sq = 0.0
    for i in range(4):
        for j in range(i + 1, 4):
            edge_sum_sq += float(np.sum((verts[i] - verts[j]) ** 2))

    ratio = (27.0 / 16.0) * (edge_sum_sq ** 3) / (det_j ** 2)
    return float(ratio ** (1.0 / 3.0))


def compute_wsrd_redistribution_map(
    wsrd_alpha: np.ndarray,
    owner: np.ndarray,
    neighbour: np.ndarray,
    n_cells: int,
    n_faces: int,
    small_cell_ratio: float = 0.20,
    wsrd_target_alpha: float = 0.50,
) -> Dict[int, Dict[str, Any]]:
    """Compute neighbor redistribution mapping for cut-cells in [small_cell_ratio, wsrd_target_alpha)."""
    redist_map: Dict[int, Dict[str, Any]] = {}
    a_small = float(small_cell_ratio)
    a_target = float(wsrd_target_alpha)
    if n_cells <= 0 or n_faces <= 0:
        return redist_map

    cut_mask = (wsrd_alpha >= a_small) & (wsrd_alpha < a_target)
    if not np.any(cut_mask):
        return redist_map

    cut_cells = np.where(cut_mask)[0]
    safe_nb = np.maximum(neighbour, 0)
    rel_mask = (neighbour >= 0) & (cut_mask[owner] | cut_mask[safe_nb])
    rel_faces = np.where(rel_mask)[0]

    adj: Dict[int, List[int]] = {int(c): [] for c in cut_cells}
    for f_idx in rel_faces:
        o = int(owner[f_idx])
        nb = int(neighbour[f_idx])
        if cut_mask[o]:
            adj[o].append(nb)
        if cut_mask[nb]:
            adj[nb].append(o)

    for c_arr in cut_cells:
        c = int(c_arr)
        a_c = float(wsrd_alpha[c])
        nb_indices = adj[c]
        if not nb_indices:
            continue

        unique_nbs = list(dict.fromkeys(nb_indices))
        if a_target > a_small:
            scaled = (a_c - a_small) / (a_target - a_small)
            w_self = float(np.sin(0.5 * np.pi * scaled) ** 2)
        else:
            w_self = 0.0
        w_donate = float(1.0 - w_self)

        regular_larger = [nb for nb in unique_nbs if float(wsrd_alpha[nb]) >= a_target]
        larger_nbs = [nb for nb in unique_nbs if float(wsrd_alpha[nb]) > a_c]

        if regular_larger:
            receivers = regular_larger
            v = {nb: max(1e-15, float(wsrd_alpha[nb]) - a_c) for nb in receivers}
        elif larger_nbs:
            receivers = larger_nbs
            v = {nb: max(1e-15, float(wsrd_alpha[nb]) - a_c) for nb in receivers}
        else:
            receivers = unique_nbs
            v = {nb: 1.0 for nb in receivers}

        sum_v = sum(v.values())
        w_nbs = {nb: 0.0 for nb in unique_nbs}
        if sum_v > 0.0 and len(receivers) > 0:
            allocated = 0.0
            for i, r_idx in enumerate(receivers):
                if i == len(receivers) - 1:
                    weight = float(w_donate - allocated)
                else:
                    weight = float(w_donate * (v[r_idx] / sum_v))
                    allocated += weight
                w_nbs[r_idx] = weight

        redist_map[c] = {"self": w_self, "neighbors": w_nbs}
    return redist_map


def compute_symmetry_mirror_point(
    c_P: np.ndarray,
    c_f: np.ndarray,
    s_f: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Construct Demirdzic (2022) orthogonal symmetry mirror connection (Paper 7)."""
    c_P = np.asarray(c_P, dtype=np.float64)
    c_f = np.asarray(c_f, dtype=np.float64)
    s_f = np.asarray(s_f, dtype=np.float64)

    is_1d = (c_P.ndim == 1)
    if is_1d:
        c_P = c_P.reshape(1, 3)
        c_f = c_f.reshape(1, 3)
        s_f = s_f.reshape(1, 3)

    d = c_f - c_P
    s_dot_s = np.sum(s_f * s_f, axis=-1, keepdims=True)
    d_dot_s = np.sum(d * s_f, axis=-1, keepdims=True)

    if np.any(s_dot_s <= 0.0):
        raise ValueError("Degenerate boundary face with zero or negative area vector magnitude.")

    Delta_vec = 2.0 * (d_dot_s / s_dot_s) * s_f
    c_M = c_P + Delta_vec
    k_f = np.zeros_like(s_f)

    if is_1d:
        return c_M[0], Delta_vec[0], k_f[0]
    return c_M, Delta_vec, k_f


def compute_householder_reflection(s_f: np.ndarray) -> np.ndarray:
    """Construct Householder reflection tensor R = I - 2 * n (x) n (Paper 7)."""
    s_f = np.asarray(s_f, dtype=np.float64)
    is_1d = (s_f.ndim == 1)
    if is_1d:
        s_f = s_f.reshape(1, 3)

    N = len(s_f)
    R = np.zeros((N, 3, 3), dtype=np.float64)
    I3 = np.eye(3, dtype=np.float64)

    for idx in range(N):
        sf = s_f[idx]
        norm_sq = float(np.dot(sf, sf))
        if norm_sq <= 1e-30:
            raise ValueError(f"Face normal at index {idx} has near-zero magnitude: {norm_sq}")
        n = sf / np.sqrt(norm_sq)
        R[idx] = I3 - 2.0 * np.outer(n, n)

    if is_1d:
        return R[0]
    return R


def compute_symmetry_vector_diffusion_split(
    gamma_v: float,
    s_f: np.ndarray,
    d_vec: np.ndarray,
    v_P: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray]:
    """Patankar diagonally-dominant split for vector diffusion flux on symmetry plane (Paper 7 Eq. 15-16)."""
    if gamma_v < 0.0:
        raise ValueError(f"Diffusivity gamma_v must be non-negative, got {gamma_v}")
    s_f = np.asarray(s_f, dtype=np.float64)
    d_vec = np.asarray(d_vec, dtype=np.float64)
    v_P = np.asarray(v_P, dtype=np.float64)

    ds = float(np.dot(d_vec, s_f))
    if ds <= 1e-15:
        raise ValueError(f"Non-positive cell-to-face dot product d . s_f = {ds}; inverted boundary cell.")

    s_dot_v = float(np.dot(s_f, v_P))
    D_im = gamma_v * (s_f ** 2) / ds
    D_ex = -gamma_v * ((s_dot_v - s_f * v_P) * s_f) / ds
    return D_im, D_ex


def compute_symmetry_tensor_diffusion_split(
    gamma_T: float,
    s_f: np.ndarray,
    d_vec: np.ndarray,
    T_P: np.ndarray,
) -> Tuple[np.ndarray, np.ndarray]:
    """Patankar diagonally-dominant split for symmetric rank-2 tensor diffusion flux on symmetry plane."""
    if gamma_T < 0.0:
        raise ValueError(f"Diffusivity gamma_T must be non-negative, got {gamma_T}")
    s_f = np.asarray(s_f, dtype=np.float64)
    d_vec = np.asarray(d_vec, dtype=np.float64)
    T_P = np.asarray(T_P, dtype=np.float64)

    ds = float(np.dot(d_vec, s_f))
    s2 = float(np.dot(s_f, s_f))
    if ds <= 1e-15 or s2 <= 1e-30:
        raise ValueError("Degenerate boundary face or inverted cell.")

    s_T = np.dot(s_f, T_P)
    s_T_s = float(np.dot(s_T, s_f))

    D_im = np.zeros((3, 3), dtype=np.float64)
    D_ex = np.zeros((3, 3), dtype=np.float64)

    for i in range(3):
        for j in range(3):
            diag_weight = (s_f[i] ** 2 + s_f[j] ** 2)
            D_im[i, j] = gamma_T * diag_weight / ds
            ex_term = (
                s_f[i] * s_T[j]
                + s_f[j] * s_T[i]
                - 2.0 * s_T_s * s_f[i] * s_f[j] / s2
                - diag_weight * T_P[i, j]
            )
            D_ex[i, j] = -gamma_T * ex_term / ds

    return D_im, D_ex


class FaceClass:
    """Classification of internal and boundary faces for FVM assembly."""
    INVALID = -1
    FAST = 0        # Class 0: Near-orthogonal, low-skew (M-matrix upwind)
    CORRECTED = 1   # Class 1: Non-orthogonal / skewed requiring deferred correction


def compute_linear_exact_face_geometry(
    x_P: np.ndarray,
    x_N: np.ndarray,
    x_f: np.ndarray,
    area_f: np.ndarray,
    h_ref: float,
    skew_tol: float = 1e-12,
    non_ortho_tol: float = 1e-12,
) -> Dict[str, Any]:
    """Compute linear-exact plane intersection geometry and FaceClass (Snippet 12 / §12.2)."""
    x_P = np.asarray(x_P, dtype=np.float64)
    x_N = np.asarray(x_N, dtype=np.float64)
    x_f = np.asarray(x_f, dtype=np.float64)
    area_f = np.asarray(area_f, dtype=np.float64)

    d = x_N - x_P
    ds = float(np.dot(d, area_f))
    d2 = float(np.dot(d, d))
    s2 = float(np.dot(area_f, area_f))

    invalid_res = {
        "kind": FaceClass.INVALID,
        "beta": 0.0,
        "alpha": 0.0,
        "skew": np.zeros(3, dtype=np.float64),
        "correction": np.zeros(3, dtype=np.float64),
        "relative_skew": float("inf"),
        "non_ortho": float("inf"),
    }

    if not (d2 > 0.0 and s2 > 0.0 and h_ref > 0.0):
        return invalid_res

    denom = np.sqrt(d2 * s2)
    cosine = ds / denom
    if not np.isfinite(cosine) or cosine <= 1e-8:
        return invalid_res

    beta = float(np.dot(x_f - x_P, area_f) / ds)
    alpha = float(s2 / ds)
    skew = x_f - (x_P + d * beta)
    correction = area_f - d * alpha
    rel_skew = float(np.linalg.norm(skew) / h_ref)
    non_ortho = float(np.linalg.norm(correction) / np.sqrt(s2))

    kind = FaceClass.FAST if (rel_skew <= skew_tol and non_ortho <= non_ortho_tol) else FaceClass.CORRECTED

    return {
        "kind": kind,
        "beta": beta,
        "alpha": alpha,
        "skew": skew,
        "correction": correction,
        "relative_skew": rel_skew,
        "non_ortho": non_ortho,
    }


def gather_residual_cell_face_csr(
    owned_cells: int,
    offsets: np.ndarray,
    faces: np.ndarray,
    orientation: np.ndarray,
    face_flux: np.ndarray,
    residual: Optional[np.ndarray] = None,
) -> np.ndarray:
    """Deterministic, atomic-free cell residual gather via CellFaceCSR adjacency (Snippet 12)."""
    if residual is None:
        residual = np.zeros(owned_cells, dtype=np.float64)
    else:
        residual = np.asarray(residual, dtype=np.float64)

    for cell in range(owned_cells):
        start = offsets[cell]
        end = offsets[cell + 1]
        sum_flux = 0.0
        for k in range(start, end):
            f_idx = faces[k]
            sum_flux += float(orientation[k]) * float(face_flux[f_idx])
        residual[cell] += sum_flux

    return residual


def factorize_spd_metric_cholesky(
    M: np.ndarray,
    eps: float = 1e-12,
    h_min: Optional[float] = None,
    h_max: Optional[float] = None,
) -> Tuple[np.ndarray, np.ndarray]:
    """SPD Cholesky factorization of Riemannian metric tensor M = L L^T + eps I (§12.2)."""
    M = np.asarray(M, dtype=np.float64)
    M_sym = 0.5 * (M + M.T)

    eigvals, eigvecs = np.linalg.eigh(M_sym)
    min_eig = eps
    max_eig = 1e12

    if h_max is not None and h_max > 0.0:
        min_eig = max(eps, 1.0 / (h_max ** 2))
    if h_min is not None and h_min > 0.0:
        max_eig = 1.0 / (h_min ** 2)

    clamped_eigvals = np.clip(eigvals, min_eig, max_eig)
    M_spd = eigvecs @ np.diag(clamped_eigvals) @ eigvecs.T
    L = np.linalg.cholesky(M_spd)
    return M_spd, L


def project_boundary_points_tangential_newton(
    points: np.ndarray,
    sdf_eval_fn: Any,
    sdf_grad_fn: Any,
    adjoint_grad: np.ndarray,
    step_size: float = 1.0,
) -> np.ndarray:
    """Tangential CAD projection + 1-step Newton re-projection onto psi(x) = 0 (Snippet 14)."""
    pts = np.asarray(points, dtype=np.float64)
    grads = np.asarray(adjoint_grad, dtype=np.float64)
    result = np.zeros_like(pts)

    for i in range(len(pts)):
        p = pts[i]
        g = grads[i]
        n = sdf_grad_fn(p)
        n = n / (np.linalg.norm(n) + 1e-15)

        g_tan = g - n * float(np.dot(g, n))
        x_trial = p - step_size * g_tan

        psi_val = float(sdf_eval_fn(x_trial))
        n_trial = sdf_grad_fn(x_trial)
        n_trial = n_trial / (np.linalg.norm(n_trial) + 1e-15)
        result[i] = x_trial - psi_val * n_trial

    return result


def compute_gradient_free_deferred_correction(
    m_dot: float,
    gamma: float,
    alpha_f: float,
    beta_f: float,
    phi_P: float,
    phi_N: float,
) -> Tuple[float, float, float, float, float]:
    """Class 0 gradient-free convective upwind deferred correction (Snippet 13 / §12.2)."""
    diff_flux = gamma * alpha_f
    m_pos = max(m_dot, 0.0)
    m_neg = min(m_dot, 0.0)

    a_PN = m_neg - diff_flux
    a_NP = -m_pos - diff_flux

    a_P_diag = -a_PN
    a_N_diag = -a_NP

    flux_ho = m_dot * (beta_f * phi_N + (1.0 - beta_f) * phi_P)
    flux_upw = m_pos * phi_P + m_neg * phi_N
    flux_deferred = flux_ho - flux_upw

    return a_PN, a_NP, a_P_diag, a_N_diag, flux_deferred


def compute_three_map_mesh_error_map(
    mesh: UnstructuredMesh,
    u_pilot: Optional[np.ndarray] = None,
    wall_distance: Optional[np.ndarray] = None,
    H_u: Optional[np.ndarray] = None,
    residual: Optional[np.ndarray] = None,
) -> Dict[str, Dict[str, Any]]:
    """Separate 3-Map MeshErrorMap architecture (§12.2)."""
    n_faces = mesh.num_faces
    n_cells = mesh.num_cells

    geom_map: Dict[str, Any] = {
        "m_skew_relative": mesh.error_map.get("skewness", np.zeros(n_faces)),
        "non_orthogonality": mesh.error_map.get("non_orthogonality", np.zeros(n_faces)),
        "aspect_ratio": mesh.error_map.get("aspect_ratio", np.ones(n_cells)),
        "stabilization_mask": mesh.error_map.get("stabilization_mask", np.zeros(n_faces, dtype=np.int32)),
    }

    sol_indicator: Dict[str, Any] = {}
    if u_pilot is not None:
        sol_indicator["pilot_velocity_magnitude"] = np.linalg.norm(u_pilot, axis=-1)
    if wall_distance is not None:
        sol_indicator["wall_distance_sdf"] = np.asarray(wall_distance, dtype=np.float64)
    if H_u is not None:
        sol_indicator["hessian_frobenius_norm"] = np.linalg.norm(H_u, axis=(-2, -1))

    err_estimator: Dict[str, Any] = {}
    if residual is not None:
        err_estimator["residual_norm"] = np.abs(np.asarray(residual, dtype=np.float64))
    else:
        err_estimator["residual_norm"] = np.zeros(n_cells, dtype=np.float64)

    return {
        "GeometryMap": geom_map,
        "SolutionIndicator": sol_indicator,
        "ErrorEstimator": err_estimator,
    }
