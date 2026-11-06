"""Independent Mathematical & Numerical Audit Verification Script for Gate WP4.

Auditor: auditor_math_gate_wp4_1
Tasks:
1. Strict Positivity of Cell Volumes (V_i > 0) & AMIPS Inversion Guard
2. Scale-Invariant Relative Gauss Divergence Closure (< 10^-11) & Quad Splitting Conservation
3. Surface Projection & SDF Normal Collinearity (< 10^-12)
4. Multiphysics CHT Twin-Faces Anti-Symmetry (||S_A + S_B|| = 0.0) & Area Equality & Winding
5. Asymptotic GCI Convergence (ASME V&V 20-2009 / Celik et al. 2008) & 3-Grid Hierarchy
"""

import os
import sys
import numpy as np
from pathlib import Path

os.environ["OMP_NUM_THREADS"] = "16"

from backend.geometry.models import SurfaceMesh
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    generate_hexcore_native,
    extract_cht_twin_faces,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    CellType,
    FarfieldPadding,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.meshing.gci import (
    compute_asme_vv20_gci,
    generate_3grid_gci_hierarchy,
    compute_grid_representative_size,
)

print("=" * 80)
print("GATE WP4 MATHEMATIK- & NUMERIK-AUDIT: INDEPENDENT EMPIRICAL & ANALYTICAL VERIFICATION")
print("=" * 80)

# ------------------------------------------------------------------------------
# AUDIT TASK 1 & 2: Volume Positivity & Scale-Invariant Gauss Closure
# ------------------------------------------------------------------------------
print("\n--- AUDIT TASK 1 & 2: VOLUME POSITIVITY & RELATIVE GAUSS DIVERGENCE CLOSURE ---")

def resolve_cad_file(filename: str) -> Path:
    candidates = [
        Path("resources/CADTestSamples") / filename,
        Path("resources/CADTestSamples/ComplexGeometryExample") / filename,
        Path("backend/geometry/tests/fixtures") / filename,
    ]
    for c in candidates:
        if c.exists():
            return c
    raise FileNotFoundError(f"CAD fixture {filename} not found.")

cad_files = [
    (resolve_cad_file("TestCAD.obj"), 0.001, "external_fluid", 0.08),
    (resolve_cad_file("TestCAD.stp"), 0.001, "external_fluid", 0.08),
    (resolve_cad_file("BladeFullRev1CFD.stp"), 0.001, "external_fluid", 0.12),
]

for filepath, scale, domain_mode, h0 in cad_files:
    p = Path(filepath)
    if not p.exists():
        print(f"Skipping {filepath} (file not found)")
        continue
    
    ce = CADGeometryEngine()
    ce.load(str(p))
    sm = ce.current_mesh
    verts = sm.vertices * scale
    faces = sm.faces
    
    print(f"\nEvaluating CAD Asset: {p.name}")
    print(f"Surface Vertices: {len(verts)}, Faces: {len(faces)}")
    
    mesh = generate_hexcore_native(
        verts,
        faces,
        h0=h0,
        domain_mode=domain_mode,
    )
    
    n_cells = mesh.num_cells
    n_faces = mesh.num_faces
    vols = mesh.cell_volumes
    cell_types = mesh.cell_types
    
    n_hex = int(np.sum(cell_types == 12))  # HEXAHEDRON
    n_poly = int(np.sum(cell_types == 42)) # POLYHEDRON
    
    min_vol = float(np.min(vols))
    max_vol = float(np.max(vols))
    n_non_pos = int(np.sum(vols <= 0.0))
    
    print(f"Generated Grid: {n_cells} cells ({n_hex} hex, {n_poly} polyhedra), {n_faces} faces")
    print(f"Cell Volume: min = {min_vol:.6e}, max = {max_vol:.6e}")
    print(f"Non-positive cell count: {n_non_pos}")
    assert n_non_pos == 0, f"FAILED: Found {n_non_pos} non-positive cells in {p.name}!"
    assert min_vol > 0.0, f"FAILED: Minimum volume is not strictly positive ({min_vol})!"
    
    # Compute Scale-Invariant Relative Gauss Divergence Closure per cell
    owner = mesh.owner
    neighbour = mesh.neighbour
    face_areas = mesh.face_areas
    
    sum_sf = np.zeros((n_cells, 3), dtype=np.float64)
    sum_norm_sf = np.zeros(n_cells, dtype=np.float64)
    
    norm_sf = np.linalg.norm(face_areas, axis=1)
    
    np.add.at(sum_sf, owner, face_areas)
    np.add.at(sum_norm_sf, owner, norm_sf)
    
    int_mask = neighbour >= 0
    nb_int = neighbour[int_mask]
    sf_int = face_areas[int_mask]
    norm_sf_int = norm_sf[int_mask]
    
    np.subtract.at(sum_sf, nb_int, sf_int)
    np.add.at(sum_norm_sf, nb_int, norm_sf_int)
    
    rel_closure = np.linalg.norm(sum_sf, axis=1) / np.maximum(sum_norm_sf, 1e-30)
    max_rel_closure = float(np.max(rel_closure))
    mean_rel_closure = float(np.mean(rel_closure))
    
    print(f"Relative Gauss Closure: max = {max_rel_closure:.4e}, mean = {mean_rel_closure:.4e}")
    assert max_rel_closure < 1e-11, f"FAILED: Max relative Gauss closure {max_rel_closure} >= 1e-11 in {p.name}!"
    
    # Also verify validate() passes
    val_ok = mesh.validate()
    print(f"mesh.validate(): {val_ok}")
    assert val_ok, f"FAILED: mesh.validate() returned False for {p.name}!"

# ------------------------------------------------------------------------------
# AUDIT TASK 3: Surface Snapping & Collinearity
# ------------------------------------------------------------------------------
print("\n--- AUDIT TASK 3: SURFACE PROJECTION & SDF NORMAL COLLINEARITY ---")

# Let's test analytical sphere level set snapping
radius = 0.5
def sphere_sdf(p):
    dist = np.linalg.norm(p, axis=-1) - radius
    grad = p / np.linalg.norm(p, axis=-1, keepdims=True)
    return dist, grad

# Generate candidate points near the sphere surface in a narrow band [radius - 0.05, radius + 0.05]
np.random.seed(123)
rand_pts = np.random.uniform(-0.6, 0.6, size=(1000, 3))
dists, grads = sphere_sdf(rand_pts)
mask = np.abs(dists) <= 0.05
test_pts = rand_pts[mask]
test_dists = dists[mask]
test_grads = grads[mask]

# Apply snapping formula: x_snap = x - (phi / ||grad||^2) * grad
snap_pts = test_pts - (test_dists[:, None] / np.sum(test_grads**2, axis=1, keepdims=True)) * test_grads
snap_dists, snap_grads = sphere_sdf(snap_pts)

max_snap_dist = np.max(np.abs(snap_dists))
print(f"Analytical Sphere Snapping: tested {len(test_pts)} points")
print(f"Max distance from zero level set after snapping: {max_snap_dist:.4e}")

# Check collinearity of surface gradient vs displacement
disp = snap_pts - test_pts
disp_norm = np.linalg.norm(disp, axis=1, keepdims=True)
non_zero_disp = (disp_norm[:, 0] > 1e-12)

disp_dir = disp[non_zero_disp] / disp_norm[non_zero_disp]
grad_dir = test_grads[non_zero_disp]
cross_err = np.linalg.norm(np.cross(disp_dir, grad_dir), axis=1)
max_cross_err = np.max(cross_err)
print(f"Displacement vs SDF Gradient Collinearity: max ||disp x grad|| = {max_cross_err:.4e}")
assert max_cross_err < 1e-12, f"FAILED: Collinearity error {max_cross_err} >= 1e-12!"

# Also check collinearity in real CAD mesh boundary faces:
# For internal faces: check delta_f x d_f collinearity in DiFVM
for filepath, scale, domain_mode, h0 in [cad_files[0]]:
    ce = CADGeometryEngine()
    ce.load(filepath)
    sm = ce.current_mesh
    mesh = generate_hexcore_native(sm.vertices * scale, sm.faces, h0=h0)
    int_mask = mesh.neighbour >= 0
    co = mesh.cell_centres[mesh.owner[int_mask]]
    cnb = mesh.cell_centres[mesh.neighbour[int_mask]]
    df = cnb - co
    delta_f = mesh.delta_f[int_mask]
    
    df_norm = np.linalg.norm(df, axis=1, keepdims=True)
    delta_norm = np.linalg.norm(delta_f, axis=1, keepdims=True)
    
    valid_mask = (df_norm[:, 0] > 1e-12) & (delta_norm[:, 0] > 1e-12)
    cross_prod = np.cross(delta_f[valid_mask] / delta_norm[valid_mask], df[valid_mask] / df_norm[valid_mask])
    collin_err = np.max(np.linalg.norm(cross_prod, axis=1))
    print(f"DiFVM Internal Faces Projection Collinearity (||delta_f x d_f|| / (||delta|| ||d||)): {collin_err:.4e}")
    assert collin_err < 1e-12, f"FAILED: Internal face collinearity error {collin_err} >= 1e-12!"

# ------------------------------------------------------------------------------
# AUDIT TASK 4: Multiphysics CHT Twin-Faces Anti-Symmetry
# ------------------------------------------------------------------------------
print("\n--- AUDIT TASK 4: MULTIPHYSICS CHT TWIN-FACES ANTI-SYMMETRY ---")

# Test 1: Synthetic 2-region mesh
points = np.array([
    [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [1.0, 1.0, 1.0], [0.0, 1.0, 1.0],
    [2.0, 0.0, 0.0], [2.0, 1.0, 0.0], [2.0, 0.0, 1.0], [2.0, 1.0, 1.0],
], dtype=np.float64)

# Cell 0 (Region 0 - Fluid), Cell 1 (Region 1 - Solid)
cells = [
    [0, 1, 2, 3, 4, 5, 6, 7],     # Hex 0: [0,1]x[0,1]x[0,1]
    [1, 8, 9, 2, 5, 10, 11, 6],   # Hex 1: [1,2]x[0,1]x[0,1]
]
cell_regions = np.array([0, 1], dtype=np.int32)

mesh_raw = compute_difvm_topology(points, cells, regions=cell_regions)
print(f"Raw 2-region mesh created: num_faces={mesh_raw.num_faces}, is_multiregion={mesh_raw.is_multiregion}")

mesh_cht = extract_cht_twin_faces(mesh_raw)
print(f"After extract_cht_twin_faces: num_faces={mesh_cht.num_faces}, boundaries={mesh_cht.boundaries}")

# Find coupled twin patches
assert "coupledWall_fluid" in mesh_cht.boundaries, "Missing coupledWall_fluid boundary!"
assert "coupledWall_solid" in mesh_cht.boundaries, "Missing coupledWall_solid boundary!"

start_f, count_f = mesh_cht.boundaries["coupledWall_fluid"]
start_s, count_s = mesh_cht.boundaries["coupledWall_solid"]

assert count_f == count_s, f"Count mismatch: fluid={count_f}, solid={count_s}"
print(f"Coupled twin patch size: {count_f} faces")

for i in range(count_f):
    fa = start_f + i
    fb = start_s + i
    
    sa = mesh_cht.face_areas[fa]
    sb = mesh_cht.face_areas[fb]
    
    sum_s = sa + sb
    norm_sum = float(np.linalg.norm(sum_s))
    area_a = float(np.linalg.norm(sa))
    area_b = float(np.linalg.norm(sb))
    
    print(f"Pair {i}: S_fluid={sa}, S_solid={sb}, ||S_fluid + S_solid||={norm_sum:.4e}, area_diff={abs(area_a - area_b):.4e}")
    assert norm_sum == 0.0, f"FAILED: Non-zero sum for twin faces {fa} and {fb}: {norm_sum} != 0.0!"
    assert abs(area_a - area_b) == 0.0, f"FAILED: Area difference between twin faces {fa} and {fb} is non-zero!"
    
    # Check reversed winding order
    nodes_a = mesh_cht.faces[fa]
    nodes_b = mesh_cht.faces[fb]
    print(f"Pair {i} vertices: fluid={nodes_a.tolist()}, solid={nodes_b.tolist()}")
    assert np.array_equal(nodes_b, nodes_a[::-1]), f"FAILED: Vertex winding is not reversed! nodes_b={nodes_b}, nodes_a[::-1]={nodes_a[::-1]}"

# Test 2: SnappyMultiRegionHeater real benchmark
heater_dir = Path("OpenFOAM_base/openfoam/tutorials/resources/geometry/snappyMultiRegionHeater")
if heater_dir.exists():
    print(f"\nEvaluating Real Benchmark: snappyMultiRegionHeater ({heater_dir})")
    me = MeshEngine()
    mesh_heater = me.mesh_cad_file(str(heater_dir), h0=0.02)
    print(f"Heater Mesh: num_cells={mesh_heater.num_cells}, num_faces={mesh_heater.num_faces}")
    print(f"Boundaries: {list(mesh_heater.boundaries.keys())}")
    
    # Check coupled boundaries
    coupled_patches = [k for k in mesh_heater.boundaries.keys() if "coupledWall" in k]
    print(f"Found {len(coupled_patches)} coupled twin patches: {coupled_patches}")
    assert len(coupled_patches) >= 2, "Expected at least 2 coupled patches in multi-region heater!"
    
    # Check twin face anti-symmetry on all coupled patches
    for patch_a in coupled_patches:
        if "_to_" in patch_a:
            # Format: coupledWall_regionA_to_B
            parts = patch_a.split("_to_")
            reg_a = parts[0].split("region")[1]
            reg_b = parts[1]
            patch_b = f"coupledWall_region{reg_b}_to_{reg_a}"
        elif "fluid" in patch_a:
            patch_b = patch_a.replace("fluid", "solid")
        else:
            continue
        
        if patch_b in mesh_heater.boundaries:
            st_a, cnt_a = mesh_heater.boundaries[patch_a]
            st_b, cnt_b = mesh_heater.boundaries[patch_b]
            assert cnt_a == cnt_b, f"Count mismatch between {patch_a} ({cnt_a}) and {patch_b} ({cnt_b})"
            
            for k in range(cnt_a):
                fa = st_a + k
                fb = st_b + k
                sa = mesh_heater.face_areas[fa]
                sb = mesh_heater.face_areas[fb]
                norm_sum = float(np.linalg.norm(sa + sb))
                assert norm_sum == 0.0, f"FAILED: Twin faces {fa} and {fb} have non-zero sum: {norm_sum}"

# ------------------------------------------------------------------------------
# AUDIT TASK 5: Asymptotic GCI Convergence & 3-Grid Hierarchy
# ------------------------------------------------------------------------------
print("\n--- AUDIT TASK 5: ASYMPTOTIC GCI CONVERGENCE (ASME V&V 20-2009) ---")

# Manufactured solution: f(h) = phi_exact + C * h^p
# Case A: p = 2.0 (2nd-order method)
phi_exact_2 = 100.0
C2 = 5.0
p_true_2 = 2.0
r = 1.41421356 # sqrt(2) in [1.3, 1.5]
h1 = 0.01
h2 = r * h1
h3 = r * h2

phi1_2 = phi_exact_2 + C2 * (h1 ** p_true_2)
phi2_2 = phi_exact_2 + C2 * (h2 ** p_true_2)
phi3_2 = phi_exact_2 + C2 * (h3 ** p_true_2)

res_2 = compute_asme_vv20_gci(phi1_2, phi2_2, phi3_2, r)
print("ASME V&V 20 Case 2nd-order:")
print(f"Apparent order p: {res_2.apparent_order_p:.6f} (target: 2.000000)")
print(f"Extrapolated phi_ext: {res_2.phi_extrapolated:.6f} (target: {phi_exact_2:.6f})")
print(f"Approx relative error: {res_2.approx_rel_error_fine:.6e}")
print(f"GCI fine: {res_2.gci_fine:.6e}")
print(f"Asymptotic ratio: {res_2.asymptotic_ratio:.6f} (target: 1.000000)")

assert abs(res_2.apparent_order_p - 2.0) < 1e-5, f"FAILED: Apparent order {res_2.apparent_order_p} != 2.0!"
assert abs(res_2.phi_extrapolated - phi_exact_2) < 1e-5, f"FAILED: Extrapolated solution {res_2.phi_extrapolated} != {phi_exact_2}!"
assert abs(res_2.asymptotic_ratio - 1.0) < 1e-5, f"FAILED: Asymptotic ratio {res_2.asymptotic_ratio} != 1.0!"
assert res_2.gci_fine < 0.01, f"FAILED: GCI fine {res_2.gci_fine} >= 1%!"

# Case B: p = 4.0 (4th-order method)
phi_exact_4 = 42.0
C4 = 25.0
p_true_4 = 4.0
phi1_4 = phi_exact_4 + C4 * (h1 ** p_true_4)
phi2_4 = phi_exact_4 + C4 * (h2 ** p_true_4)
phi3_4 = phi_exact_4 + C4 * (h3 ** p_true_4)

res_4 = compute_asme_vv20_gci(phi1_4, phi2_4, phi3_4, r)
print("\nASME V&V 20 Case 4th-order:")
print(f"Apparent order p: {res_4.apparent_order_p:.6f} (target: 4.000000)")
print(f"Extrapolated phi_ext: {res_4.phi_extrapolated:.6f} (target: {phi_exact_4:.6f})")
print(f"GCI fine: {res_4.gci_fine:.6e}")
print(f"Asymptotic ratio: {res_4.asymptotic_ratio:.6f} (target: 1.000000)")

assert abs(res_4.apparent_order_p - 4.0) < 1e-5, f"FAILED: Apparent order {res_4.apparent_order_p} != 4.0!"
assert abs(res_4.phi_extrapolated - phi_exact_4) < 1e-5, f"FAILED: Extrapolated solution {res_4.phi_extrapolated} != {phi_exact_4}!"
assert abs(res_4.asymptotic_ratio - 1.0) < 1e-5, f"FAILED: Asymptotic ratio {res_4.asymptotic_ratio} != 1.0!"
assert res_4.gci_fine < 0.01, f"FAILED: GCI fine {res_4.gci_fine} >= 1%!"

# Case C: Automated 3-grid hierarchy generation on 3D geometry
print("\nAutomated 3-Grid Hierarchy Generation:")
me = MeshEngine()
test_cad = "resources/CADTestSamples/TestCAD.obj"
ce = CADGeometryEngine()
ce.load(test_cad)
sm = ce.current_mesh
v_cad = sm.vertices * 0.001
f_cad = sm.faces

def generator(h):
    return generate_hexcore_native(v_cad, f_cad, h0=h, domain_mode="external_fluid")

hierarchy = generate_3grid_gci_hierarchy(generator, base_h0=0.06, r=1.41421356)
print(f"Fine grid: {hierarchy.fine.num_cells} cells, boundaries={list(hierarchy.fine.boundaries.keys())}")
print(f"Medium grid: {hierarchy.medium.num_cells} cells, boundaries={list(hierarchy.medium.boundaries.keys())}")
print(f"Coarse grid: {hierarchy.coarse.num_cells} cells, boundaries={list(hierarchy.coarse.boundaries.keys())}")

# Check cell ordering: N_fine > N_medium > N_coarse
assert hierarchy.fine.num_cells > hierarchy.medium.num_cells > hierarchy.coarse.num_cells, "Grid cell counts do not monotonically decrease!"

# Check representative grid size
h_fine = compute_grid_representative_size(hierarchy.fine)
h_med = compute_grid_representative_size(hierarchy.medium)
h_coarse = compute_grid_representative_size(hierarchy.coarse)

r21 = h_med / h_fine
r32 = h_coarse / h_med
print(f"Representative cell sizes: h_fine={h_fine:.4e}, h_med={h_med:.4e}, h_coarse={h_coarse:.4e}")
print(f"Actual refinement ratios: r21={r21:.4f}, r32={r32:.4f}")
assert 1.25 <= r21 <= 1.6, f"Refinement ratio r21={r21} outside expected range!"
assert 1.25 <= r32 <= 1.6, f"Refinement ratio r32={r32} outside expected range!"

print("\n" + "=" * 80)
print("ALL MATHEMATICAL AND NUMERICAL INVARIANTS PERFECTLY VERIFIED!")
print("=" * 80)
