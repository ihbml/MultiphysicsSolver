"""Precise Numerical Measurement Harness for Milestone M2 Invariants.

Collects exact maximum residuals, minimum volumes, and oracle differences for handoff report.
"""

import math
import numpy as np
from backend.meshing.difvm_topology import (
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    get_native_dll,
)
from backend.meshing.mesh_engine import MeshEngine
from tests.test_empirical_challenger_m2_1 import (
    generate_perturbed_hex_mesh,
    generate_sheared_hex_mesh,
    generate_angled_face_mesh,
    generate_non_convex_hex,
    generate_hybrid_mesh,
    TESTCAD_OBJ,
    BLADE_OBJ,
)

def run_measurements():
    results = {}

    # 1. Volume Positivity Measurements
    vol_tests = {}
    # Perturbed mesh (nx=6, ny=6, nz=6, pert=0.20)
    pts_p, cells_p = generate_perturbed_hex_mesh(6, 6, 6, perturbation=0.20, seed=999)
    m_nat = compute_difvm_topology_native(pts_p, cells_p)
    m_py = compute_difvm_topology_python(pts_p, cells_p)
    vol_tests["perturbed_hex_native_min_v"] = float(np.min(m_nat.cell_volumes))
    vol_tests["perturbed_hex_py_min_v"] = float(np.min(m_py.cell_volumes))
    vol_tests["perturbed_hex_v_diff"] = float(np.max(np.abs(m_nat.cell_volumes - m_py.cell_volumes)))

    # Micro scale (L = 1e-4)
    L = 1e-4
    pts_m = np.array([
        [0, 0, 0], [L, 0, 0], [L, L, 0], [0, L, 0],
        [0, 0, L], [L, 0, L], [L, L, L], [0, L, L],
    ], dtype=np.float64)
    cells_m = [np.arange(8, dtype=np.int64)]
    m_micro_nat = compute_difvm_topology_native(pts_m, cells_m)
    vol_tests["micro_scale_vol"] = float(m_micro_nat.cell_volumes[0])

    # Extreme aspect ratio (1000:1)
    pts_ar = np.array([
        [0, 0, 0], [1000, 0, 0], [1000, 1, 0], [0, 1, 0],
        [0, 0, 1], [1000, 0, 1], [1000, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    cells_ar = [np.arange(8, dtype=np.int64)]
    m_ar_nat = compute_difvm_topology_native(pts_ar, cells_ar)
    vol_tests["aspect_ratio_1000_vol"] = float(m_ar_nat.cell_volumes[0])

    # Non-convex hex
    pts_nc, cells_nc = generate_non_convex_hex()
    m_nc_nat = compute_difvm_topology_native(pts_nc, cells_nc)
    vol_tests["non_convex_vol"] = float(m_nc_nat.cell_volumes[0])

    results["volume_positivity"] = vol_tests

    # 2. Gauss Divergence Closure Measurements
    closure_tests = {}
    cases = [
        ("cartesian_4x4x4", generate_sheared_hex_mesh(4, 4, 4, shear_xy=0.0, shear_xz=0.0)),
        ("sheared_shear_0.5", generate_sheared_hex_mesh(4, 4, 4, shear_xy=0.5, shear_xz=0.25)),
        ("sheared_shear_1.0", generate_sheared_hex_mesh(4, 4, 4, shear_xy=1.0, shear_xz=0.5)),
        ("sheared_shear_2.0", generate_sheared_hex_mesh(4, 4, 4, shear_xy=2.0, shear_xz=1.0)),
        ("sheared_shear_3.5", generate_sheared_hex_mesh(4, 4, 4, shear_xy=3.5, shear_xz=1.75)),
        ("perturbed_warped_6x6x6", generate_perturbed_hex_mesh(6, 6, 6, perturbation=0.20, seed=123)),
        ("hybrid_hex_tet_wedge_pyr", generate_hybrid_mesh()),
    ]
    for name, (pts, cells) in cases:
        mn = compute_difvm_topology_native(pts, cells)
        mp = compute_difvm_topology_python(pts, cells)

        for tag, mesh in [("native", mn), ("python", mp)]:
            c = np.zeros((mesh.num_cells, 3), dtype=np.float64)
            np.add.at(c, mesh.owner, mesh.face_areas)
            intern = mesh.neighbour >= 0
            np.subtract.at(c, mesh.neighbour[intern], mesh.face_areas[intern])
            max_c = float(np.max(np.linalg.norm(c, axis=1)))
            closure_tests[f"{name}_{tag}"] = max_c

    results["gauss_closure"] = closure_tests

    # 3. Over-Relaxed Decomposition Measurements across angles
    decomp_tests = {}
    test_angles = [0.0, 30.0, 45.0, 60.0, 70.0, 75.0, 80.0, 85.0, 88.0, 89.0, 89.5]
    for ang in test_angles:
        pts, cells = generate_angled_face_mesh(ang)
        mn = compute_difvm_topology_native(pts, cells)
        mp = compute_difvm_topology_python(pts, cells)

        for tag, mesh in [("native", mn), ("python", mp)]:
            intern = mesh.neighbour >= 0
            diff = (mesh.delta_f[intern] + mesh.k_f[intern]) - mesh.face_areas[intern]
            max_d = float(np.max(np.linalg.norm(diff, axis=1)))
            actual_ang = float(mesh.error_map["non_orthogonality"][intern][0])
            decomp_tests[f"angle_{ang}_{tag}"] = {
                "actual_angle_deg": actual_ang,
                "max_decomp_residual": max_d,
                "delta_f_norm": float(np.linalg.norm(mesh.delta_f[intern][0])),
                "k_f_norm": float(np.linalg.norm(mesh.k_f[intern][0])),
                "sf_norm": float(np.linalg.norm(mesh.face_areas[intern][0])),
            }

    results["decomposition"] = decomp_tests

    # 4. Error Map vs Oracle Measurements
    pts_s, cells_s = generate_sheared_hex_mesh(3, 3, 3, shear_xy=0.6, shear_xz=0.3)
    mn = compute_difvm_topology_native(pts_s, cells_s)
    mp = compute_difvm_topology_python(pts_s, cells_s)

    em_diffs = {}
    for tag, mesh in [("native", mn), ("python", mp)]:
        # Oracle angle
        max_th_diff = 0.0
        max_sk_diff = 0.0
        for f in range(mesh.num_faces):
            o = mesh.owner[f]
            nb = mesh.neighbour[f]
            sf = mesh.face_areas[f]
            cf = mesh.face_centres[f]
            co = mesh.cell_centres[o]
            if nb >= 0:
                df = mesh.cell_centres[nb] - co
                sf_norm = np.linalg.norm(sf)
                df_norm = np.linalg.norm(df)
                cos_th = np.clip(np.dot(sf, df) / (sf_norm * df_norm), 0.0, 1.0)
                oracle_th = float(np.arccos(cos_th) * 180.0 / np.pi)
                max_th_diff = max(max_th_diff, abs(mesh.error_map["non_orthogonality"][f] - oracle_th))

                nf = sf / sf_norm
                df_dot_nf = float(np.dot(df, nf))
                oracle_skew = 0.0
                if abs(df_dot_nf) > 1e-15:
                    t = float(np.dot(cf - co, nf)) / df_dot_nf
                    x_int = co + df * t
                    oracle_skew = float(np.linalg.norm(cf - x_int) / df_norm)
                max_sk_diff = max(max_sk_diff, abs(mesh.error_map["skewness"][f] - oracle_skew))

        # Oracle aspect ratio
        max_ar_diff = 0.0
        for c in range(mesh.num_cells):
            cell_pts = pts_s[cells_s[c]]
            diffs = cell_pts[:, None, :] - cell_pts[None, :, :]
            dists = np.linalg.norm(diffs, axis=-1)
            nonzero = dists[dists > 1e-12]
            oracle_ar = float(np.max(nonzero) / np.min(nonzero)) if len(nonzero) > 0 else 1.0
            max_ar_diff = max(max_ar_diff, abs(mesh.error_map["aspect_ratio"][c] - oracle_ar))

        em_diffs[f"{tag}_max_angle_err_vs_oracle"] = max_th_diff
        em_diffs[f"{tag}_max_skewness_err_vs_oracle"] = max_sk_diff
        em_diffs[f"{tag}_max_ar_err_vs_oracle"] = max_ar_diff

    results["error_map_oracle"] = em_diffs

    # 5. Real CAD models
    engine = MeshEngine()
    cad_results = {}
    if TESTCAD_OBJ.exists():
        m_cube = engine.mesh_cad_file(TESTCAD_OBJ, h0=0.03, scale=0.001)
        c = np.zeros((m_cube.num_cells, 3), dtype=np.float64)
        np.add.at(c, m_cube.owner, m_cube.face_areas)
        intern = m_cube.neighbour >= 0
        np.subtract.at(c, m_cube.neighbour[intern], m_cube.face_areas[intern])
        max_c = float(np.max(np.linalg.norm(c, axis=1)))
        diff = (m_cube.delta_f[intern] + m_cube.k_f[intern]) - m_cube.face_areas[intern]
        max_d = float(np.max(np.linalg.norm(diff, axis=1)))

        cad_results["testcad_cube"] = {
            "num_cells": m_cube.num_cells,
            "min_volume": float(np.min(m_cube.cell_volumes)),
            "total_volume": float(np.sum(m_cube.cell_volumes)),
            "max_closure": max_c,
            "max_decomp": max_d,
            "max_non_ortho": float(np.max(m_cube.error_map["non_orthogonality"])),
            "max_skewness": float(np.max(m_cube.error_map["skewness"])),
        }

    if BLADE_OBJ.exists():
        m_blade = engine.mesh_cad_file(BLADE_OBJ, h0=15.0, scale=1.0)
        c = np.zeros((m_blade.num_cells, 3), dtype=np.float64)
        np.add.at(c, m_blade.owner, m_blade.face_areas)
        intern = m_blade.neighbour >= 0
        np.subtract.at(c, m_blade.neighbour[intern], m_blade.face_areas[intern])
        max_c = float(np.max(np.linalg.norm(c, axis=1)))
        diff = (m_blade.delta_f[intern] + m_blade.k_f[intern]) - m_blade.face_areas[intern]
        max_d = float(np.max(np.linalg.norm(diff, axis=1)))

        cad_results["blade_rev1"] = {
            "num_cells": m_blade.num_cells,
            "min_volume": float(np.min(m_blade.cell_volumes)),
            "total_volume": float(np.sum(m_blade.cell_volumes)),
            "max_closure": max_c,
            "max_decomp": max_d,
            "max_non_ortho": float(np.max(m_blade.error_map["non_orthogonality"])),
            "max_skewness": float(np.max(m_blade.error_map["skewness"])),
        }

    results["cad_models"] = cad_results

    return results

if __name__ == "__main__":
    import pprint
    res = run_measurements()
    pprint.pprint(res, width=120)
