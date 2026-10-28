import ctypes
import numpy as np
import sys
from pathlib import Path

# Add repo root to path
repo_root = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(repo_root))

from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    compute_difvm_topology_native,
    compute_difvm_topology_python,
    get_native_dll,
)
from backend.meshing.distmesh_core import distmesh_2d, rcm_reorder, compute_bandwidth
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.models import InflationConfig, MeshingConfig
from backend.meshing.mesh_engine import MeshEngine
from tests.test_mesher import create_synthetic_hexahedral_grid


def audit_perturbations():
    print("=== STARTING DYNAMIC PERTURBATION AUDIT ===")
    rng = np.random.default_rng(999)

    # 1. Perturbation of Hex Grid
    base_pts, base_cells = create_synthetic_hexahedral_grid(nx=3, ny=3, nz=3, lx=1.0, ly=1.0, lz=1.0)
    mesh_base = compute_difvm_topology(base_pts, base_cells)
    base_vol = np.sum(mesh_base.cell_volumes)

    # Perturb points
    perturbed_pts = base_pts + rng.normal(scale=0.02, size=base_pts.shape)
    mesh_pert_native = compute_difvm_topology(perturbed_pts, base_cells, use_native=True)
    mesh_pert_py = compute_difvm_topology(perturbed_pts, base_cells, use_native=False)
    pert_vol = np.sum(mesh_pert_native.cell_volumes)

    print(f"Base volume: {base_vol:.6f}, Perturbed volume: {pert_vol:.6f}")
    assert abs(base_vol - pert_vol) > 1e-4, "Volumes must vary when vertices are perturbed!"

    # Parity between native and python on perturbed mesh
    vol_diff = np.max(np.abs(mesh_pert_native.cell_volumes - mesh_pert_py.cell_volumes))
    print(f"Native vs Python volume max diff on perturbed mesh: {vol_diff:.2e}")
    assert vol_diff < 1e-12

    cc_diff = np.max(np.abs(mesh_pert_native.cell_centres - mesh_pert_py.cell_centres))
    print(f"Native vs Python centroid max diff on perturbed mesh: {cc_diff:.2e}")
    assert cc_diff < 1e-12

    fa_diff = np.max(np.abs(mesh_pert_native.face_areas - mesh_pert_py.face_areas))
    print(f"Native vs Python face area max diff on perturbed mesh: {fa_diff:.2e}")
    assert fa_diff < 1e-12

    # Check Gauss closure on perturbed mesh
    closure = np.zeros_like(mesh_pert_native.cell_centres)
    np.add.at(closure, mesh_pert_native.owner, mesh_pert_native.face_areas)
    internal = mesh_pert_native.neighbour >= 0
    np.subtract.at(closure, mesh_pert_native.neighbour[internal], mesh_pert_native.face_areas[internal])
    max_closure = np.max(np.linalg.norm(closure, axis=1))
    print(f"Perturbed mesh Gauss closure error: {max_closure:.2e}")
    assert max_closure < 1e-11

    # Check over-relaxed decomposition on perturbed mesh
    decomp_diff = (mesh_pert_native.delta_f[internal] + mesh_pert_native.k_f[internal]) - mesh_pert_native.face_areas[internal]
    max_decomp = np.max(np.linalg.norm(decomp_diff, axis=1))
    print(f"Perturbed mesh over-relaxed decomposition error: {max_decomp:.2e}")
    assert max_decomp < 1e-14

    # 2. Dynamic testing of DistMesh 2D with ellipse
    a, b = 1.5, 0.8
    def fd_ellipse(p):
        return (p[:, 0] / a)**2 + (p[:, 1] / b)**2 - 1.0
    def fh_uniform(p):
        return np.full(len(p), 0.15)

    p_ell, t_ell = distmesh_2d(fd_ellipse, fh_uniform, h0=0.15, bbox=(-1.6, -0.9, 1.6, 0.9), max_iters=30)
    # Area of ellipse
    p0 = p_ell[t_ell[:, 0]]
    p1 = p_ell[t_ell[:, 1]]
    p2 = p_ell[t_ell[:, 2]]
    areas = 0.5 * np.abs((p1[:, 0] - p0[:, 0]) * (p2[:, 1] - p0[:, 1]) - (p1[:, 1] - p0[:, 1]) * (p2[:, 0] - p0[:, 0]))
    total_ellipse_area = np.sum(areas)
    analytical_area = np.pi * a * b
    rel_err = abs(total_ellipse_area - analytical_area) / analytical_area
    print(f"DistMesh 2D Ellipse Area: {total_ellipse_area:.4f} vs Analytical: {analytical_area:.4f} (rel err: {rel_err:.2%})")
    assert rel_err < 0.06

    # 3. Dynamic testing of inflation with variable growth rates
    gen_12 = BoundaryLayerGenerator(InflationConfig(num_layers=5, first_layer_height=0.001, growth_rate=1.2))
    gen_15 = BoundaryLayerGenerator(InflationConfig(num_layers=5, first_layer_height=0.001, growth_rate=1.5))
    thick_12 = gen_12.get_layer_thicknesses()
    thick_15 = gen_15.get_layer_thicknesses()
    print(f"Growth 1.2 total thickness: {sum(thick_12):.6f}, Growth 1.5 total thickness: {sum(thick_15):.6f}")
    assert sum(thick_15) > sum(thick_12)

    # 4. Native DLL cfd_mesher_generate_from_surface dynamic test
    dll = get_native_dll()
    if dll:
        # Create a box surface with 8 vertices and 12 triangles
        surf_pts = np.array([
            [0, 0, 0], [2, 0, 0], [2, 3, 0], [0, 3, 0],
            [0, 0, 4], [2, 0, 4], [2, 3, 4], [0, 3, 4],
        ], dtype=np.float64)
        surf_tris = np.array([
            [0, 2, 1], [0, 3, 2],
            [4, 5, 6], [4, 6, 7],
            [0, 1, 5], [0, 5, 4],
            [1, 2, 6], [1, 6, 5],
            [2, 3, 7], [2, 7, 6],
            [3, 0, 4], [3, 4, 7],
        ], dtype=np.int32)
        m_handle = dll.cfd_mesher_create()
        try:
            dll.cfd_mesher_generate_from_surface.restype = ctypes.c_int32
            dll.cfd_mesher_generate_from_surface.argtypes = [
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_double),
                ctypes.c_int64,
                ctypes.POINTER(ctypes.c_int32),
                ctypes.c_int64,
                ctypes.c_void_p,
            ]
            res = dll.cfd_mesher_generate_from_surface(
                m_handle,
                surf_pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                len(surf_pts),
                surf_tris.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
                len(surf_tris),
                None,
            )
            print(f"cfd_mesher_generate_from_surface return code: {res}")
            assert res == 0
            n_cells = dll.cfd_mesher_get_n_cells(m_handle)
            cv_ptr = dll.cfd_mesher_get_cell_volumes_ptr(m_handle)
            vols = np.ctypeslib.as_array(cv_ptr, shape=(n_cells,))
            tot_vol = np.sum(vols)
            print(f"Native mesher generated {n_cells} cells, total volume = {tot_vol:.4f} (expected: 2*3*4 = 24.0)")
            assert abs(tot_vol - 24.0) < 1e-6
        finally:
            dll.cfd_mesher_free(m_handle)

    print("=== DYNAMIC PERTURBATION AUDIT COMPLETED SUCCESSFULLY ===")


if __name__ == "__main__":
    audit_perturbations()
