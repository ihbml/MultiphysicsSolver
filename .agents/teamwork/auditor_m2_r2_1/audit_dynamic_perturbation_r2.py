"""Forensic Re-Audit Script for Milestone M2.

Auditor: auditor_m2_r2_1
Gate: Gate 2 Forensic Integrity Re-Audit
"""

import os
import sys
import ctypes
import numpy as np

# Ensure workspace root is in sys.path
workspace_root = r"G:\Datein\Projects\CFDSolver"
if workspace_root not in sys.path:
    sys.path.insert(0, workspace_root)

from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import MeshingConfig, CurvatureSizingConfig, UnstructuredMesh
from backend.meshing.difvm_topology import get_native_dll, MeshingConfigC


def test_static_forensics():
    print("=== PHASE 1: STATIC CODE FORENSICS ===")
    mesh_engine_path = os.path.join(workspace_root, "backend", "meshing", "mesh_engine.py")
    distmesh_cpp_path = os.path.join(workspace_root, "backend", "meshing", "native", "src", "distmesh.cpp")

    with open(mesh_engine_path, "r", encoding="utf-8") as f:
        me_content = f.read()

    # 1. Check for dummy_cells
    dummy_cells_count = me_content.count("dummy_cells")
    print(f"Occurrences of 'dummy_cells' in mesh_engine.py: {dummy_cells_count}")
    assert dummy_cells_count == 0, f"Found {dummy_cells_count} occurrences of 'dummy_cells' in mesh_engine.py!"

    # 2. Check for 0.56 volume tuning heuristic
    tuning_056_count = me_content.count("0.56")
    print(f"Occurrences of '0.56' in mesh_engine.py: {tuning_056_count}")
    assert tuning_056_count == 0, f"Found {tuning_056_count} occurrences of '0.56' in mesh_engine.py!"

    # 3. Check for physical narrow-band level-set formulation
    has_narrow_band = "threshold = 0.11 * h_min" in me_content or "narrow" in me_content
    print(f"Physical narrow-band formulation in mesh_engine.py: {has_narrow_band}")
    assert has_narrow_band, "Physical narrow-band formulation missing in mesh_engine.py!"

    # 4. Check for CSR buffer extraction in mesh_engine.py
    has_csr_offsets = "cfd_mesher_get_cell_offsets_ptr" in me_content
    has_csr_nodes = "cfd_mesher_get_cell_nodes_ptr" in me_content
    print(f"CSR cell_offsets read in mesh_engine.py: {has_csr_offsets}")
    print(f"CSR cell_nodes read in mesh_engine.py: {has_csr_nodes}")
    assert has_csr_offsets and has_csr_nodes, "Native CSR buffer connectivity reading missing in mesh_engine.py!"

    with open(distmesh_cpp_path, "r", encoding="utf-8") as f:
        dm_content = f.read()

    # 5. Check BowyerWatsonDelaunay in distmesh.cpp
    has_bw = "struct BowyerWatsonDelaunay" in dm_content
    has_circum = "circumContains" in dm_content
    has_emap = "emap" in dm_content
    print(f"BowyerWatsonDelaunay in distmesh.cpp: {has_bw}")
    print(f"circumContains in distmesh.cpp: {has_circum}")
    print(f"Directed edge map (emap) in distmesh.cpp: {has_emap}")
    assert has_bw and has_circum and has_emap, "Bowyer-Watson Delaunay missing in distmesh.cpp!"

    # 6. Check elimination of hex centroid repulsion facade
    has_old_facade = "d.normalized() * (dist * 0.05)" in dm_content
    print(f"Old hex centroid repulsion facade in distmesh.cpp: {has_old_facade}")
    assert not has_old_facade, "Old hex centroid repulsion facade still present in distmesh.cpp!"

    # 7. Check genuine Persson & Strang spring force formulation F = max(L0 - le, 0)
    has_spring_formula = "std::max(L0 - le, 0.0)" in dm_content
    print(f"Persson & Strang spring formula in distmesh.cpp: {has_spring_formula}")
    assert has_spring_formula, "Persson & Strang spring force formula missing in distmesh.cpp!"

    print(">>> Phase 1 Static Forensics: ALL CHECKS PASSED <<<\n")


def run_native_2d_raw(poly: np.ndarray, h0: float, iters: int):
    """Direct C-ABI call to cfd_mesher_generate_2d_polygon."""
    dll = get_native_dll()
    assert dll is not None

    cfg_c = MeshingConfigC()
    cfg_c.h0 = h0
    cfg_c.distmesh_iters = iters
    cfg_c.enable_rcm = 0

    m_ptr = dll.cfd_mesher_create()
    dll.cfd_mesher_generate_2d_polygon.restype = ctypes.c_int32
    dll.cfd_mesher_generate_2d_polygon.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int64,
        ctypes.POINTER(MeshingConfigC),
    ]
    res = dll.cfd_mesher_generate_2d_polygon(
        m_ptr,
        poly.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
        len(poly),
        ctypes.byref(cfg_c),
    )
    assert res == 0, f"Native 2D mesher failed with code {res}"

    n_points = dll.cfd_mesher_get_n_points(m_ptr)
    n_cells = dll.cfd_mesher_get_n_cells(m_ptr)
    n_faces = dll.cfd_mesher_get_n_faces(m_ptr)
    n_internal = dll.cfd_mesher_get_n_internal_faces(m_ptr)

    pts_ptr = dll.cfd_mesher_get_points_ptr(m_ptr)
    pts = np.ctypeslib.as_array(pts_ptr, shape=(n_points, 3)).copy()

    vols_ptr = dll.cfd_mesher_get_cell_volumes_ptr(m_ptr)
    vols = np.ctypeslib.as_array(vols_ptr, shape=(n_cells,)).copy()

    offsets_ptr = dll.cfd_mesher_get_cell_offsets_ptr(m_ptr)
    nodes_ptr = dll.cfd_mesher_get_cell_nodes_ptr(m_ptr)
    offsets = np.ctypeslib.as_array(offsets_ptr, shape=(n_cells + 1,))
    nodes = np.ctypeslib.as_array(nodes_ptr, shape=(int(offsets[-1]),))
    cells = [nodes[offsets[i]:offsets[i + 1]].copy() for i in range(n_cells)]

    owner_ptr = dll.cfd_mesher_get_owner_ptr(m_ptr)
    neighbour_ptr = dll.cfd_mesher_get_neighbour_ptr(m_ptr)
    face_areas_ptr = dll.cfd_mesher_get_face_areas_ptr(m_ptr)
    face_centres_ptr = dll.cfd_mesher_get_face_centres_ptr(m_ptr)
    cell_centres_ptr = dll.cfd_mesher_get_cell_centres_ptr(m_ptr)
    delta_f_ptr = dll.cfd_mesher_get_delta_f_ptr(m_ptr)
    k_f_ptr = dll.cfd_mesher_get_k_f_ptr(m_ptr)

    owner = np.ctypeslib.as_array(owner_ptr, shape=(n_faces,)).copy()
    neighbour = np.ctypeslib.as_array(neighbour_ptr, shape=(n_faces,)).copy()
    face_areas = np.ctypeslib.as_array(face_areas_ptr, shape=(n_faces, 3)).copy()
    face_centres = np.ctypeslib.as_array(face_centres_ptr, shape=(n_faces, 3)).copy()
    cell_centres = np.ctypeslib.as_array(cell_centres_ptr, shape=(n_cells, 3)).copy()
    delta_f = np.ctypeslib.as_array(delta_f_ptr, shape=(n_faces, 3)).copy()
    k_f = np.ctypeslib.as_array(k_f_ptr, shape=(n_faces, 3)).copy()

    dll.cfd_mesher_free(m_ptr)

    return {
        "points": pts,
        "cells": cells,
        "cell_volumes": vols,
        "cell_centres": cell_centres,
        "owner": owner,
        "neighbour": neighbour,
        "face_areas": face_areas,
        "face_centres": face_centres,
        "delta_f": delta_f,
        "k_f": k_f,
        "n_internal": n_internal,
    }


def test_native_bowyer_watson_and_relaxation():
    print("=== PHASE 2: DYNAMIC NATIVE BOWYER-WATSON & SPRING RELAXATION ===")
    engine = MeshEngine()
    assert engine.has_native_core, "Native C++20 DLL not loaded!"

    # Test 1: High-level MeshEngine.mesh_2d_polygon on simple quad
    poly_quad = np.array([
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]
    ], dtype=np.float64)
    mesh_quad = engine.mesh_2d_polygon(poly_quad)
    assert mesh_quad is not None
    assert mesh_quad.metadata.get("engine") == "native_cpp20_2d"
    assert len(mesh_quad.cell_volumes) == 4
    assert abs(np.sum(mesh_quad.cell_volumes) - 1.0) < 1e-12
    # Check that cells are genuine arrays from CSR, not dummy [0, 1, 2]
    cell_tuples = [tuple(c) for c in mesh_quad.cells]
    print(f"Quad cells from native CSR: {cell_tuples}")
    assert len(set(cell_tuples)) == 4, "Cells should all have distinct node connectivity"
    assert not any(np.array_equal(c, [0, 1, 2]) for c in mesh_quad.cells), "Found placeholder dummy cells!"

    # Test 2: Fine 2D Bowyer-Watson Delaunay on Perturbed Domain with Internal Nodes
    np.random.seed(42)
    n_poly = 16
    theta = np.linspace(0, 2 * np.pi, n_poly, endpoint=False)
    r = 1.0 + 0.15 * np.sin(3 * theta) + 0.05 * np.random.randn(n_poly)
    poly = np.column_stack([r * np.cos(theta), r * np.sin(theta)])

    res_fine = run_native_2d_raw(poly, h0=0.15, iters=15)
    pts = res_fine["points"]
    cells = res_fine["cells"]
    vols = res_fine["cell_volumes"]
    owner = res_fine["owner"]
    neighbour = res_fine["neighbour"]
    fa = res_fine["face_areas"]
    df = res_fine["delta_f"]
    kf = res_fine["k_f"]

    print(f"Bowyer-Watson Delaunay generated: {len(pts)} points, {len(cells)} cells, {len(owner)} faces")
    assert len(cells) > 30, f"Expected > 30 cells for h0=0.15, got {len(cells)}"

    # Check cell uniqueness
    unique_cells = set(tuple(sorted(c)) for c in cells)
    assert len(unique_cells) == len(cells), "Duplicate triangles generated!"

    # Check strictly positive volumes
    min_vol = np.min(vols)
    max_vol = np.max(vols)
    total_vol = np.sum(vols)
    print(f"Cell volumes: min={min_vol:.6e}, max={max_vol:.6e}, total={total_vol:.6f}")
    assert min_vol > 0.0, f"Non-positive cell volume detected: {min_vol}"

    # Check Gauss divergence closure
    face_fluxes = np.zeros((len(cells), 3))
    for f in range(len(owner)):
        own = owner[f]
        face_fluxes[own] += fa[f]
        nb = neighbour[f]
        if nb >= 0:
            face_fluxes[nb] -= fa[f]
    max_gauss_err = np.max(np.linalg.norm(face_fluxes, axis=1))
    print(f"Max Gauss divergence closure error: {max_gauss_err:.6e}")
    assert max_gauss_err < 1e-12, f"Gauss divergence theorem violated: {max_gauss_err}"

    # Check Over-relaxed decomposition identity on internal faces
    internal = neighbour >= 0
    decomp_err = np.max(np.linalg.norm(df[internal] + kf[internal] - fa[internal], axis=1))
    print(f"Max over-relaxed decomposition error: {decomp_err:.6e}")
    assert decomp_err < 1e-12, f"Over-relaxed decomposition violated: {decomp_err}"

    # Test 3: Resolution Scaling Response (h0=0.20 vs h0=0.10)
    res_coarse = run_native_2d_raw(poly, h0=0.20, iters=10)
    res_ultra_fine = run_native_2d_raw(poly, h0=0.10, iters=10)
    ratio = len(res_ultra_fine["cells"]) / len(res_coarse["cells"])
    print(f"Resolution scaling: Coarse cells={len(res_coarse['cells'])}, Ultra-fine cells={len(res_ultra_fine['cells'])}, Ratio={ratio:.2f}")
    assert ratio >= 2.0, f"Expected cell count scaling ratio >= 2.0x, got {ratio:.2f}"

    # Test 4: Spring Relaxation Node Movement
    res_iter0 = run_native_2d_raw(poly, h0=0.15, iters=0)
    res_iter20 = run_native_2d_raw(poly, h0=0.15, iters=20)
    assert len(res_iter0["points"]) == len(res_iter20["points"]), "Node count mismatch for same seeding"
    disp = np.linalg.norm(res_iter20["points"][n_poly:, :2] - res_iter0["points"][n_poly:, :2], axis=1)
    max_disp = np.max(disp)
    mean_disp = np.mean(disp)
    print(f"Spring force displacement (iters=0 vs 20): max={max_disp:.6e}, mean={mean_disp:.6e}")
    assert max_disp > 1e-3, f"Spring force relaxation did not move nodes! max_disp={max_disp}"

    print(">>> Phase 2 Native Bowyer-Watson & Spring Relaxation: ALL CHECKS PASSED <<<\n")


def test_dynamic_coordinate_perturbation():
    print("=== PHASE 3: DYNAMIC PERTURBATION & REPRODUCIBILITY ===")
    engine = MeshEngine()

    base_poly = np.array([
        [0.0, 0.0], [1.0, 0.0], [1.2, 0.5], [1.0, 1.0], [0.0, 1.0], [-0.2, 0.5]
    ], dtype=np.float64)

    m_base = engine.mesh_2d_polygon(base_poly)

    perturbed_poly = base_poly + np.array([
        [0.01, -0.02], [-0.01, 0.03], [0.02, 0.01], [-0.03, -0.01], [0.01, 0.02], [-0.02, 0.0]
    ])
    m_pert = engine.mesh_2d_polygon(perturbed_poly)

    vol_base = np.sum(m_base.cell_volumes)
    vol_pert = np.sum(m_pert.cell_volumes)
    print(f"Base volume: {vol_base:.6f}, Perturbed volume: {vol_pert:.6f}")
    assert abs(vol_base - vol_pert) > 1e-4, "Perturbed mesh volume did not respond dynamically!"

    # Invariants on perturbed mesh
    face_fluxes = np.zeros_like(m_pert.cell_centres)
    for f in range(len(m_pert.owner)):
        own = m_pert.owner[f]
        face_fluxes[own] += m_pert.face_areas[f]
        nb = m_pert.neighbour[f]
        if nb >= 0:
            face_fluxes[nb] -= m_pert.face_areas[f]
    gauss_err = np.max(np.linalg.norm(face_fluxes, axis=1))
    print(f"Perturbed mesh Gauss error: {gauss_err:.6e}")
    assert gauss_err < 1e-12, f"Perturbed mesh Gauss error too large: {gauss_err}"

    # Parity check with pure-Python fallback
    engine_py = MeshEngine(use_native=False)
    m_py = engine_py.mesh_2d_polygon(base_poly)
    print(f"Python fallback cells: {len(m_py.cells)}, Native cells: {len(m_base.cells)}")
    assert len(m_py.cells) == len(m_base.cells)
    vol_py = np.sum(m_py.cell_volumes)
    assert abs(vol_py - vol_base) < 1e-12, f"Native/Python volume discrepancy: {abs(vol_py - vol_base)}"

    print(">>> Phase 3 Dynamic Perturbation: ALL CHECKS PASSED <<<\n")


def main():
    print("=====================================================================")
    print("      GATE 2 FORENSIC INTEGRITY RE-AUDIT: MILESTONE M2 (R2)        ")
    print("=====================================================================")

    test_static_forensics()
    test_native_bowyer_watson_and_relaxation()
    test_dynamic_coordinate_perturbation()

    print("=====================================================================")
    print("             ALL GATE 2 FORENSIC INTEGRITY CHECKS PASSED!            ")
    print("=====================================================================")


if __name__ == "__main__":
    main()
