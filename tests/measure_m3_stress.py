"""Empirical Stress Measurement and Invariant Auditor for Milestone M3.

Measures exact numerical tolerances, condition numbers, residuals, and statistics
across SPH-FVM coupling invariants.
"""

from __future__ import annotations

import math
from typing import Dict, Any, List
import numpy as np

from backend.meshing.coupling import SPHKernel, SPHFVMCoupler, OPMISTaylorInterpolator
from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.models import UnstructuredMesh


def run_all_stress_measurements() -> Dict[str, Any]:
    results = {}

    print("=" * 80)
    print("CHALLENGER M3 EMPIRICAL STRESS MEASUREMENTS & INVARIANT AUDIT")
    print("=" * 80)

    # -----------------------------------------------------------------------
    # Task 1: SPH Support Divergence Detection (div r_i < 0.75 d)
    # -----------------------------------------------------------------------
    print("\n--- Task 1: SPH Support Divergence Detection ---")
    coupler_w = SPHFVMCoupler(kernel="wendland", support_factor=2.85, boundary_threshold_ratio=0.75)
    coupler_c = SPHFVMCoupler(kernel="cubic", support_factor=2.0, boundary_threshold_ratio=0.75)

    # 1.1 2D Uniform Cartesian Grid
    nx, ny = 30, 30
    dx = 0.0333
    x = np.linspace(0.0, 1.0, nx)
    y = np.linspace(0.0, 1.0, ny)
    X, Y = np.meshgrid(x, y)
    pos_2d = np.column_stack([X.ravel(), Y.ravel()])
    vol_2d = np.full(len(pos_2d), dx * dx, dtype=np.float64)
    h_2d = dx * 1.5

    div_r_2d_w = coupler_w.compute_position_divergence(pos_2d, vol_2d, h_2d, dim=2)
    is_bnd_2d_w = coupler_w.detect_boundary_particles(pos_2d, vol_2d, h_2d, dim=2)

    div_r_2d_c = coupler_c.compute_position_divergence(pos_2d, vol_2d, h_2d, dim=2)
    is_bnd_2d_c = coupler_c.detect_boundary_particles(pos_2d, vol_2d, h_2d, dim=2)

    dist_to_wall = np.minimum.reduce([pos_2d[:, 0], 1.0 - pos_2d[:, 0], pos_2d[:, 1], 1.0 - pos_2d[:, 1]])
    deep_int = dist_to_wall >= (coupler_w.support_factor * h_2d)
    skin = dist_to_wall < 1e-6

    fp_w = np.sum(is_bnd_2d_w[deep_int])
    fn_w = np.sum(~is_bnd_2d_w[skin])
    mean_int_div_w = float(np.mean(div_r_2d_w[deep_int]))
    min_int_div_w = float(np.min(div_r_2d_w[deep_int]))
    max_skin_div_w = float(np.max(div_r_2d_w[skin]))

    print(f"2D Wendland: Deep Interior div r = {mean_int_div_w:.4f} (min={min_int_div_w:.4f}, expected ~ 2.0)")
    print(f"2D Wendland: Outer Wall Skin div r max = {max_skin_div_w:.4f} (threshold = {0.75 * 2:.2f})")
    print(f"2D Wendland: False Positives in Deep Interior: {fp_w}/{np.sum(deep_int)} (FPR = {fp_w/np.sum(deep_int):.1%})")
    print(f"2D Wendland: False Negatives on Wall Skin: {fn_w}/{np.sum(skin)} (FNR = {fn_w/np.sum(skin):.1%})")

    results["task1_2d_wendland"] = {
        "mean_int_div": mean_int_div_w,
        "min_int_div": min_int_div_w,
        "max_skin_div": max_skin_div_w,
        "fpr": float(fp_w / np.sum(deep_int)),
        "fnr": float(fn_w / np.sum(skin)),
    }

    # 1.2 3D Randomly Perturbed Lattice
    rng = np.random.default_rng(42)
    n3 = 12
    coords = np.linspace(0.0, 1.0, n3)
    X3, Y3, Z3 = np.meshgrid(coords, coords, coords)
    pos_3d = np.column_stack([X3.ravel(), Y3.ravel(), Z3.ravel()])
    dx3 = 1.0 / (n3 - 1)
    vol_3d = np.full(len(pos_3d), dx3 ** 3, dtype=np.float64)
    h_3d = dx3 * 1.5

    # Jitter interior
    d_wall_3d = np.minimum.reduce([
        pos_3d[:, 0], 1.0 - pos_3d[:, 0],
        pos_3d[:, 1], 1.0 - pos_3d[:, 1],
        pos_3d[:, 2], 1.0 - pos_3d[:, 2],
    ])
    int_3d_mask = d_wall_3d > (coupler_w.support_factor * h_3d)
    skin_3d_mask = d_wall_3d < 1e-6

    jitter_levels = [0.0, 0.05, 0.10, 0.20, 0.30]
    jitter_res = []
    for jit in jitter_levels:
        pos_jit = pos_3d.copy()
        if jit > 0:
            pos_jit[int_3d_mask] += rng.uniform(-jit * dx3, jit * dx3, size=(np.sum(int_3d_mask), 3))
        
        div_r_3d = coupler_w.compute_position_divergence(pos_jit, vol_3d, h_3d, dim=3)
        is_bnd_3d = coupler_w.detect_boundary_particles(pos_jit, vol_3d, h_3d, dim=3)

        fp_3d = np.sum(is_bnd_3d[int_3d_mask])
        fn_3d = np.sum(~is_bnd_3d[skin_3d_mask])
        min_int = float(np.min(div_r_3d[int_3d_mask]))
        max_skin = float(np.max(div_r_3d[skin_3d_mask]))
        mean_int = float(np.mean(div_r_3d[int_3d_mask]))

        print(f"3D Jitter {jit*100:4.1f}%: mean_int={mean_int:.3f}, min_int={min_int:.3f}, max_skin={max_skin:.3f}, FP={fp_3d}, FN={fn_3d}")
        jitter_res.append({
            "jitter": jit,
            "mean_int_div": mean_int,
            "min_int_div": min_int,
            "max_skin_div": max_skin,
            "fp": int(fp_3d),
            "fn": int(fn_3d),
        })
    results["task1_3d_jitter"] = jitter_res

    # -----------------------------------------------------------------------
    # Task 2: Virtual Wall Vector A_iS 0th-Order Closure Across Curvatures
    # -----------------------------------------------------------------------
    print("\n--- Task 2: Virtual Wall Vector A_iS 0th-Order Closure ---")
    
    # 2.1 Flat planar wall
    A_2d, sum_grad_2d = coupler_w.compute_virtual_wall_vector(pos_2d, vol_2d, h_2d, return_sum_grad=True, dim=2)
    res_flat = np.max(np.linalg.norm(A_2d[is_bnd_2d_w] + sum_grad_2d[is_bnd_2d_w], axis=1))
    print(f"Flat 2D Wall: Max ||A_iS + sum V_j grad W_ij|| = {res_flat:.2e}")

    # 2.2 Curved Cylinder 3D
    r_cyl = 0.5
    l_cyl = 1.0
    n_cyl_p = 500
    r_rand = np.sqrt(rng.uniform(0.01, r_cyl**2, size=n_cyl_p))
    th_rand = rng.uniform(0.0, 2 * np.pi, size=n_cyl_p)
    z_rand = rng.uniform(0.0, l_cyl, size=n_cyl_p)
    cyl_pos = np.column_stack([r_rand * np.cos(th_rand), r_rand * np.sin(th_rand), z_rand])
    cyl_vol = np.full(len(cyl_pos), (np.pi * r_cyl**2 * l_cyl) / len(cyl_pos))
    cyl_h = (np.mean(cyl_vol) ** (1.0 / 3.0)) * 1.6

    A_cyl, sum_grad_cyl = coupler_w.compute_virtual_wall_vector(cyl_pos, cyl_vol, cyl_h, return_sum_grad=True, dim=3)
    is_bnd_cyl = coupler_w.detect_boundary_particles(cyl_pos, cyl_vol, cyl_h, dim=3)
    res_cyl = np.max(np.linalg.norm(A_cyl[is_bnd_cyl] + sum_grad_cyl[is_bnd_cyl], axis=1))
    int_A_cyl = np.max(np.linalg.norm(A_cyl[~is_bnd_cyl], axis=1)) if np.any(~is_bnd_cyl) else 0.0
    print(f"Curved 3D Cylinder: Max ||A_iS + sum V_j grad W_ij|| = {res_cyl:.2e}")
    print(f"Curved 3D Cylinder: Max ||A_iS|| on Interior = {int_A_cyl:.2e}")

    # 2.3 Closed Domain Conservation of Wall Vectors: sum A_iS ~ 0
    # In a closed box, the sum of surface normal vectors should be 0: oint n dS = 0
    sum_A_closed_2d = np.sum(A_2d, axis=0)
    print(f"Closed 2D Box: Vector Sum of A_iS = {sum_A_closed_2d} (||sum A_iS|| = {np.linalg.norm(sum_A_closed_2d):.2e})")

    results["task2_closure"] = {
        "max_res_flat_2d": float(res_flat),
        "max_res_curved_cyl_3d": float(res_cyl),
        "interior_A_norm": float(int_A_cyl),
        "sum_A_closed_box_norm": float(np.linalg.norm(sum_A_closed_2d)),
    }

    # -----------------------------------------------------------------------
    # Task 3: OPMIS Taylor Linear Moment Reproduction
    # -----------------------------------------------------------------------
    print("\n--- Task 3: OPMIS Taylor Linear Moment Reproduction ---")
    
    # Setup hex mesh
    mesh_nx, mesh_ny, mesh_nz = 4, 4, 4
    x_m = np.linspace(0.0, 1.0, mesh_nx + 1)
    y_m = np.linspace(0.0, 1.0, mesh_ny + 1)
    z_m = np.linspace(0.0, 1.0, mesh_nz + 1)
    points = []
    p_idx = {}
    for k in range(mesh_nz + 1):
        for j in range(mesh_ny + 1):
            for i in range(mesh_nx + 1):
                p_idx[(i, j, k)] = len(points)
                points.append([x_m[i], y_m[j], z_m[k]])
    points_arr = np.array(points, dtype=np.float64)
    cells = []
    for k in range(mesh_nz):
        for j in range(mesh_ny):
            for i in range(mesh_nx):
                c = [
                    p_idx[(i, j, k)], p_idx[(i + 1, j, k)], p_idx[(i + 1, j + 1, k)], p_idx[(i, j + 1, k)],
                    p_idx[(i, j, k + 1)], p_idx[(i + 1, j, k + 1)], p_idx[(i + 1, j + 1, k + 1)], p_idx[(i, j + 1, k + 1)],
                ]
                cells.append(np.array(c, dtype=np.int64))
    mesh = compute_difvm_topology(points_arr, cells)

    interpolator = OPMISTaylorInterpolator(k_neighbors_p2m=20, k_neighbors_m2p=14)

    # Test on irregular, clustered particles
    pos_stress = rng.uniform(0.02, 0.98, size=(300, 3))
    vol_stress = np.full(len(pos_stress), 1.0 / len(pos_stress), dtype=np.float64)
    h_stress = 1.0 / (len(pos_stress) ** (1.0 / 3.0)) * 1.5

    test_fields = [
        ("Constant field f=1", np.array([0.0, 0.0, 0.0]), 1.0),
        ("Standard linear f=2x-3y+4z+5", np.array([2.0, -3.0, 4.0]), 5.0),
        ("Extreme slope f=10^6 x - 5*10^5 y + 2*10^6 z", np.array([1e6, -5e5, 2e6]), 10.0),
        ("Tiny slope f=10^-6 x + 10^-6 y", np.array([1e-6, 1e-6, 0.0]), 1.0),
        ("Negative offset f=-15x + 30y - 45z - 100", np.array([-15.0, 30.0, -45.0]), -100.0),
    ]

    p2m_errors = []
    m2p_errors = []

    for name, a_v, b_v in test_fields:
        f_p = np.dot(pos_stress, a_v) + b_v
        f_exact_mesh = np.dot(mesh.cell_centres, a_v) + b_v

        # P2M
        f_interp_mesh = interpolator.interpolate_p2m(pos_stress, f_p, vol_stress, h_stress, mesh)
        abs_err_p2m = np.max(np.abs(f_interp_mesh - f_exact_mesh))
        norm_scale = max(np.max(np.abs(f_exact_mesh)), 1.0)
        rel_err_p2m = abs_err_p2m / norm_scale

        # M2P
        query_pts = pos_stress[:50]
        f_exact_query = np.dot(query_pts, a_v) + b_v
        f_interp_query = interpolator.interpolate_m2p(mesh, f_exact_mesh, query_pts)
        abs_err_m2p = np.max(np.abs(f_interp_query - f_exact_query))
        rel_err_m2p = abs_err_m2p / norm_scale

        print(f"P2M [{name}]: abs_err = {abs_err_p2m:.2e}, rel_err = {rel_err_p2m:.2e}")
        print(f"M2P [{name}]: abs_err = {abs_err_m2p:.2e}, rel_err = {rel_err_m2p:.2e}")

        p2m_errors.append({"name": name, "abs_err": float(abs_err_p2m), "rel_err": float(rel_err_p2m)})
        m2p_errors.append({"name": name, "abs_err": float(abs_err_m2p), "rel_err": float(rel_err_m2p)})

    results["task3_p2m"] = p2m_errors
    results["task3_m2p"] = m2p_errors

    # -----------------------------------------------------------------------
    # Task 4: Conservation Stress Testing
    # -----------------------------------------------------------------------
    print("\n--- Task 4: Conservation Stress Testing ---")
    
    # 4.1 0th-order and 1st-order moment reproduction
    # 0th moment: f = 1.0
    p2m_const = interpolator.interpolate_p2m(pos_stress, np.ones(len(pos_stress)), vol_stress, h_stress, mesh)
    dev_0th = float(np.max(np.abs(p2m_const - 1.0)))
    print(f"0th Moment Reproduction (Partition of Unity): max deviation = {dev_0th:.2e}")

    # 1st moment: f = x
    p2m_x = interpolator.interpolate_p2m(pos_stress, pos_stress[:, 0], vol_stress, h_stress, mesh)
    dev_1st_x = float(np.max(np.abs(p2m_x - mesh.cell_centres[:, 0])))
    print(f"1st Moment Reproduction (Linear X-completeness): max error = {dev_1st_x:.2e}")

    # 4.2 Discrete Volume / Mass Integral Conservation
    # For a co-located domain with regular particles:
    n_cube = 10
    coords_c = np.linspace(0.05, 0.95, n_cube)
    Xc, Yc, Zc = np.meshgrid(coords_c, coords_c, coords_c)
    cube_pos = np.column_stack([Xc.ravel(), Yc.ravel(), Zc.ravel()])
    cube_vol = np.full(len(cube_pos), (1.0 / n_cube) ** 3, dtype=np.float64)
    cube_h = (1.0 / n_cube) * 1.5

    # Scalar field phi = 1.0 (mass = 1.0)
    cube_p2m_1 = interpolator.interpolate_p2m(cube_pos, np.ones(len(cube_pos)), cube_vol, cube_h, mesh)
    discrete_mass_mesh = float(np.sum(cube_p2m_1 * mesh.cell_volumes))
    discrete_mass_part = float(np.sum(cube_vol))
    diff_0th_mass = abs(discrete_mass_mesh - discrete_mass_part)
    print(f"Global Mass (0th moment): Particles = {discrete_mass_part:.6f}, Mesh = {discrete_mass_mesh:.6f}, Discrepancy = {diff_0th_mass:.2e}")

    # 1st moment integral (center of mass): sum(x * V)
    # Exact center of mass for [0,1]^3 is (0.5, 0.5, 0.5)
    cube_p2m_x = interpolator.interpolate_p2m(cube_pos, cube_pos[:, 0], cube_vol, cube_h, mesh)
    com_x_mesh = float(np.sum(cube_p2m_x * mesh.cell_volumes))
    com_x_part = float(np.sum(cube_pos[:, 0] * cube_vol))
    diff_1st_moment = abs(com_x_mesh - com_x_part)
    print(f"Global 1st Moment (X centroid integral): Particles = {com_x_part:.6f}, Mesh = {com_x_mesh:.6f}, Discrepancy = {diff_1st_moment:.2e}")

    results["task4_conservation"] = {
        "dev_0th_reproduction": dev_0th,
        "dev_1st_reproduction_x": dev_1st_x,
        "mass_discrepancy_0th": diff_0th_mass,
        "centroid_discrepancy_1st": diff_1st_moment,
    }

    print("\n" + "=" * 80)
    print("ALL EMPIRICAL STRESS MEASUREMENTS COMPLETED SUCCESSFULLY")
    print("=" * 80)
    return results


if __name__ == "__main__":
    run_all_stress_measurements()
