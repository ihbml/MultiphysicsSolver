"""Milestone M2 Adversarial Challenge Suite 2.

Author: challenger_m2_2 (teamwork_challenger)
Target: Milestone M2 Audit Remediation

Covers:
1. R3.3 Berger & Giuliani (2024) WSRD Dynamic Weights:
   - Custom configurations (small_cell_ratio=0.15, wsrd_target_alpha=0.45, degenerate/inverse).
   - Cell pairing map verification: shared internal faces, donor volume vs receiver volume.
2. R4 Solid-Only Domain Mode Dispatch:
   - Default config vs explicit domain_mode="solid_only" vs domain_mode="fluid_only" in MeshEngine.
3. R5.1 DistMesh Point Filtering & Newton Projection:
   - Multi-step Newton projection with non-convex distance functions and boundary-hugging geometries.
   - Points stay inside or on boundary; no blow up or oscillation.
4. R6.1-2 GCI Scaling & Order Clamping:
   - Asymptotic limits (r -> 1.0, r >= 2.0, monotonic vs oscillatory).
   - Topological dimensions D=1, D=2, D=3, and fallback for missing topological_dim.
5. R6.3-4 SDF Bounding Box Tolerance & Barnes-Hut Dipole Acceleration:
   - C++ engine vs Python engine winding number consistency on complex 3D meshes (sphere, torus).
   - Far-field dipole cutoff smoothness at dist = 2.0 * r_node.
6. R7 Static Audit Detail Fixes:
   - polymesh_io.py custom patch_types mapping and default fallback.
   - Inflation layer generation with non-planar boundary faces and inverted normal reversal (f_arr[::-1]).
"""

import os
import shutil
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import (
    compute_difvm_topology_python,
    compute_wsrd_redistribution_map,
    compute_wsrd_weights_array,
    get_native_dll,
)
from backend.meshing.distmesh_core import distmesh_2d, polygon_signed_distance
from backend.meshing.gci import (
    GCIResult,
    compute_asme_vv20_gci,
    compute_grid_representative_size,
)
from backend.meshing.geometry_sdf import (
    CADMeshSDF,
    compute_mesh_winding_number,
)
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import (
    InflationConfig,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.meshing.polymesh_io import export_openfoam_polymesh


# ==============================================================================
# Helper functions for geometric models
# ==============================================================================


def create_uv_sphere(radius: float = 1.0, n_lat: int = 16, n_lon: int = 32) -> Tuple[np.ndarray, np.ndarray]:
    """Generate UV sphere mesh."""
    verts = []
    # North pole
    verts.append([0.0, 0.0, radius])
    for i in range(1, n_lat):
        theta = np.pi * i / n_lat
        z = radius * np.cos(theta)
        r_xy = radius * np.sin(theta)
        for j in range(n_lon):
            phi = 2.0 * np.pi * j / n_lon
            x = r_xy * np.cos(phi)
            y = r_xy * np.sin(phi)
            verts.append([x, y, z])
    # South pole
    verts.append([0.0, 0.0, -radius])
    verts = np.array(verts, dtype=np.float64)

    faces = []
    # Top cap
    for j in range(n_lon):
        j_next = (j + 1) % n_lon
        faces.append([0, 1 + j, 1 + j_next])
    # Body
    for i in range(n_lat - 2):
        row_start = 1 + i * n_lon
        next_row_start = 1 + (i + 1) * n_lon
        for j in range(n_lon):
            j_next = (j + 1) % n_lon
            v0 = row_start + j
            v1 = next_row_start + j
            v2 = next_row_start + j_next
            v3 = row_start + j_next
            faces.append([v0, v1, v2])
            faces.append([v0, v2, v3])
    # Bottom cap
    south_idx = len(verts) - 1
    last_row_start = 1 + (n_lat - 2) * n_lon
    for j in range(n_lon):
        j_next = (j + 1) % n_lon
        faces.append([south_idx, last_row_start + j_next, last_row_start + j])
    return verts, np.array(faces, dtype=np.int32)


# ==============================================================================
# 1. R3.3 Berger & Giuliani (2024) WSRD Dynamic Weights
# ==============================================================================


class TestAdversarialWSRD:
    """Stress-test Berger & Giuliani WSRD dynamic weights and redistribution mapping."""

    def test_custom_configurations_weights(self):
        """Test continuous sin^2 weighting function with custom and degenerate thresholds."""
        alphas = np.array([0.05, 0.15, 0.30, 0.45, 0.75, 1.0])
        weights = compute_wsrd_weights_array(alphas, small_cell_ratio=0.15, wsrd_target_alpha=0.45)

        # alpha < 0.15 -> 0.0
        assert weights[0] == pytest.approx(0.0, abs=1e-12)
        assert weights[1] == pytest.approx(0.0, abs=1e-12)
        # alpha = 0.30 is exactly halfway -> scaled = 0.5 -> sin^2(pi/4) = 0.5
        assert weights[2] == pytest.approx(0.5, abs=1e-12)
        # alpha >= 0.45 -> 1.0
        assert weights[3] == pytest.approx(1.0, abs=1e-12)
        assert weights[4] == pytest.approx(1.0, abs=1e-12)
        assert weights[5] == pytest.approx(1.0, abs=1e-12)

        # Degenerate / inverted threshold: small_cell_ratio >= wsrd_target_alpha
        weights_deg = compute_wsrd_weights_array(alphas, small_cell_ratio=0.50, wsrd_target_alpha=0.20)
        assert weights_deg[0] == 0.0
        assert weights_deg[1] == 0.0
        assert weights_deg[2] == 1.0  # alpha >= 0.20
        assert weights_deg[3] == 1.0

        # Equal thresholds (step function)
        weights_eq = compute_wsrd_weights_array(alphas, small_cell_ratio=0.40, wsrd_target_alpha=0.40)
        assert weights_eq[2] == 0.0
        assert weights_eq[3] == 1.0

    def test_wsrd_pairing_map_invariants(self):
        """Verify cell pairing map: donor donation across shared internal faces and receiver properties."""
        # Cell 0: cut-cell alpha = 0.25 (small donor)
        # Cell 1: cut-cell alpha = 0.35 (small cell)
        # Cell 2: full cell alpha = 1.00 (large regular cell)
        # Cell 3: full cell alpha = 1.00 (boundary cell)
        wsrd_alpha = np.array([0.25, 0.35, 1.00, 1.00], dtype=np.float64)
        owner = np.array([0, 0, 1, 0, 2], dtype=np.int64)
        neighbour = np.array([1, 2, 2, -1, 3], dtype=np.int64)
        n_cells = 4
        n_faces = 5

        redist_map = compute_wsrd_redistribution_map(
            wsrd_alpha, owner, neighbour, n_cells, n_faces,
            small_cell_ratio=0.20, wsrd_target_alpha=0.50,
        )

        assert 0 in redist_map
        assert 1 in redist_map
        assert 2 not in redist_map
        assert 3 not in redist_map

        for donor_c, entry in redist_map.items():
            assert "self" in entry
            assert "neighbors" in entry
            nb_dict = entry["neighbors"]
            assert -1 not in nb_dict
            total_w = entry["self"] + sum(nb_dict.values())
            assert total_w == pytest.approx(1.0, abs=1e-14)

            # Check that donor only communicates with shared internal face neighbors
            shared_nbs = set()
            for f_idx in range(n_faces):
                if neighbour[f_idx] >= 0:
                    if owner[f_idx] == donor_c:
                        shared_nbs.add(int(neighbour[f_idx]))
                    elif neighbour[f_idx] == donor_c:
                        shared_nbs.add(int(owner[f_idx]))
            for nb_id in nb_dict.keys():
                assert nb_id in shared_nbs

    def test_wsrd_donor_receiver_volume_ordering(self):
        """Stress-test whether donor cells donate to larger receivers or smaller receivers."""
        # Cut cell 0 has volume fraction 0.40 (small donor)
        # Neighbor 1 has volume fraction 0.25 (smaller than donor!)
        # Neighbor 2 has volume fraction 0.90 (large regular cell)
        wsrd_alpha = np.array([0.40, 0.25, 0.90], dtype=np.float64)
        owner = np.array([0, 0], dtype=np.int64)
        neighbour = np.array([1, 2], dtype=np.int64)
        n_cells = 3
        n_faces = 2

        redist_map = compute_wsrd_redistribution_map(
            wsrd_alpha, owner, neighbour, n_cells, n_faces,
            small_cell_ratio=0.20, wsrd_target_alpha=0.50,
        )

        # In Berger & Giuliani WSRD, donor cells donate to larger receivers (preferring regular fluid cells)
        weights_0 = redist_map[0]["neighbors"]
        assert weights_0[2] > 0.0, "Large regular neighbor received positive donation weight"
        assert weights_0[1] == 0.0, "Smaller neighbor received zero donation weight"
        assert redist_map[0]["self"] + sum(weights_0.values()) == pytest.approx(1.0, abs=1e-14)


# ==============================================================================
# 2. R4 Solid-Only Domain Mode Dispatch
# ==============================================================================


class TestAdversarialDomainModeDispatch:
    """Stress-test domain mode routing in MeshEngine."""

    def test_domain_mode_dispatch_cad(self, tmp_path):
        """Test default config vs explicit solid_only vs fluid_only/external_fluid."""
        cube_stl = tmp_path / "test_cube.stl"
        verts = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
        ], dtype=np.float64)
        faces = np.array([
            [0, 2, 1], [0, 3, 2],  # bottom
            [4, 5, 6], [4, 6, 7],  # top
            [0, 1, 5], [0, 5, 4],  # front
            [2, 3, 7], [2, 7, 6],  # back
            [0, 4, 7], [0, 7, 3],  # left
            [1, 2, 6], [1, 6, 5],  # right
        ], dtype=np.int32)

        with open(cube_stl, "w") as f:
            f.write("solid cube\n")
            for tri in faces:
                f.write("  facet normal 0 0 0\n    outer loop\n")
                for vid in tri:
                    f.write(f"      vertex {verts[vid, 0]} {verts[vid, 1]} {verts[vid, 2]}\n")
                f.write("    endloop\n  endfacet\n")
            f.write("endsolid cube\n")

        engine = MeshEngine(use_native=False)

        # Case 1: config=None -> defaults to solid_only
        mesh_default = engine.mesh_cad_file(cube_stl, h0=0.5, config=None)
        assert isinstance(mesh_default, UnstructuredMesh)
        assert mesh_default.num_cells > 0

        # Case 2: explicit config with domain_mode="solid_only"
        cfg_solid = MeshingConfig(domain_mode="solid_only")
        mesh_solid = engine.mesh_cad_file(cube_stl, h0=0.5, config=cfg_solid)
        assert isinstance(mesh_solid, UnstructuredMesh)
        assert mesh_solid.num_cells > 0

        # Case 3: explicit config with domain_mode="external_fluid"
        cfg_fluid = MeshingConfig(domain_mode="external_fluid")
        mesh_fluid = engine.mesh_cad_file(cube_stl, h0=0.5, config=cfg_fluid)
        assert isinstance(mesh_fluid, UnstructuredMesh)
        assert mesh_fluid.num_cells > 0

        # Case 4: engine configured with self.config (domain_mode="external_fluid"), called with config=None
        engine_fluid = MeshEngine(use_native=False, config=MeshingConfig(domain_mode="external_fluid"))
        mesh_fluid_inherited = engine_fluid.mesh_cad_file(cube_stl, h0=0.5, config=None)
        assert isinstance(mesh_fluid_inherited, UnstructuredMesh)
        assert mesh_fluid_inherited.num_cells > 0


# ==============================================================================
# 3. R5.1 DistMesh Point Filtering & Newton Projection
# ==============================================================================


class TestAdversarialDistMesh:
    """Stress-test DistMesh 2D with non-convex domains and Newton projection."""

    def test_distmesh_nonconvex_l_shape(self):
        """Test multi-step Newton projection on an L-shaped domain with 270 deg re-entrant corner."""
        l_poly = np.array([
            [0.0, 0.0],
            [2.0, 0.0],
            [2.0, 1.0],
            [1.0, 1.0],
            [1.0, 2.0],
            [0.0, 2.0],
        ], dtype=np.float64)

        def fd(p: np.ndarray) -> np.ndarray:
            return polygon_signed_distance(p, l_poly)

        def fh(p: np.ndarray) -> np.ndarray:
            return np.full(len(p), 0.25)

        bbox = (0.0, 0.0, 2.0, 2.0)
        pts, tris = distmesh_2d(fd, fh, h0=0.25, bbox=bbox, pfix=l_poly, max_iters=30)

        assert len(pts) > 0
        assert len(tris) > 0

        # Verify no NaN or Inf (no blowup during Newton projection)
        assert not np.any(np.isnan(pts))
        assert not np.any(np.isinf(pts))

        # Verify all points stay inside or on the boundary (fd <= 1e-3)
        d_pts = fd(pts)
        assert np.all(d_pts <= 1e-3), f"Max distance outside L-shape: {np.max(d_pts)}"

    def test_distmesh_narrow_annulus(self):
        """Test boundary-hugging geometry: narrow annulus r in [0.75, 1.0]."""
        r_in = 0.75
        r_out = 1.00

        def fd(p: np.ndarray) -> np.ndarray:
            d_center = np.linalg.norm(p, axis=1)
            d1 = d_center - r_out
            d2 = r_in - d_center
            return np.maximum(d1, d2)

        def fh(p: np.ndarray) -> np.ndarray:
            return np.full(len(p), 0.12)

        bbox = (-1.1, -1.1, 1.1, 1.1)
        pts, tris = distmesh_2d(fd, fh, h0=0.12, bbox=bbox, max_iters=25)

        assert len(pts) > 0
        assert len(tris) > 0
        assert not np.any(np.isnan(pts))

        d_pts = fd(pts)
        assert np.all(d_pts <= 1e-3), f"Max distance outside annulus: {np.max(d_pts)}"


# ==============================================================================
# 4. R6.1-2 GCI Scaling & Order Clamping
# ==============================================================================


class TestAdversarialGCI:
    """Stress-test ASME V&V 20-2009 GCI scaling and apparent order clamping."""

    def test_gci_topological_dimensions(self):
        """Verify representative grid size scaling across 1D, 2D, 3D."""
        cells = [np.array([0, 1, 2, 3], dtype=np.int64)]
        pts = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=np.float64)
        vols = np.array([8.0], dtype=np.float64)

        # Case 1: 3D mesh (V = 8.0 -> h = 8^(1/3) = 2.0)
        mesh_3d = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
            topological_dim=3,
        )
        h_3d = compute_grid_representative_size(mesh_3d)
        assert h_3d == pytest.approx(2.0, rel=1e-6)

        # Case 2: 2D mesh (A = 8.0 -> h = 8^(1/2) = 2.828427)
        mesh_2d = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
            topological_dim=2,
        )
        h_2d = compute_grid_representative_size(mesh_2d)
        assert h_2d == pytest.approx(np.sqrt(8.0), rel=1e-6)

        # Case 3: 1D mesh (L = 8.0 -> h = 8^1 = 8.0)
        mesh_1d = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
            topological_dim=1,
        )
        h_1d = compute_grid_representative_size(mesh_1d)
        assert h_1d == pytest.approx(8.0, rel=1e-6)

    def test_gci_topological_dim_none_fallback_defect(self):
        """Demonstrate TypeError bug when topological_dim is None."""
        cells = [np.array([0, 1, 2, 3], dtype=np.int64)]
        pts = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=np.float64)
        vols = np.array([8.0], dtype=np.float64)

        mesh_none = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
            topological_dim=None,
        )
        # Graceful fallback to dim=3 when topological_dim is None
        h = compute_grid_representative_size(mesh_none)
        assert h == pytest.approx(2.0, rel=1e-6)

    def test_gci_asymptotic_and_clamping(self):
        """Test asymptotic refinement limits (r -> 1.0, r >= 2.0) and monotonic vs oscillatory clamping."""
        with pytest.raises(ValueError):
            compute_asme_vv20_gci(1.0, 1.1, 1.2, r=1.0)
        with pytest.raises(ValueError):
            compute_asme_vv20_gci(1.0, 1.1, 1.2, r=0.9)

        # Asymptotic limit r -> 1.0+ (e.g. r = 1.0001)
        res_r_near_1 = compute_asme_vv20_gci(1.0, 1.01, 1.0201, r=1.0001)
        assert np.isfinite(res_r_near_1.apparent_order_p)
        assert np.isfinite(res_r_near_1.phi_extrapolated)
        assert np.isfinite(res_r_near_1.gci_fine)

        # High refinement ratio r = 3.0
        res_r_high = compute_asme_vv20_gci(1.0, 1.04, 1.16, r=3.0)
        assert 0.5 <= res_r_high.apparent_order_p <= 10.0

        # Oscillatory convergence: eps_32 * eps_21 <= 0
        res_osc = compute_asme_vv20_gci(1.0, 1.2, 1.1, r=1.5)
        assert res_osc.apparent_order_p == pytest.approx(1.0, abs=1e-12)

        # Out-of-bounds orders clamped to [0.5, 10.0]
        res_super = compute_asme_vv20_gci(1.0, 1.0000000001, 2.0, r=2.0)
        assert res_super.apparent_order_p == pytest.approx(10.0, abs=1e-12)

        res_slow = compute_asme_vv20_gci(1.0, 1.0, 1.000001, r=2.0)
        assert 0.5 <= res_slow.apparent_order_p <= 10.0


# ==============================================================================
# 5. R6.3-4 SDF Bounding Box Tolerance & Barnes-Hut Dipole Acceleration
# ==============================================================================


class TestAdversarialSDFAndBarnesHut:
    """Stress-test generalized winding numbers and dipole far-field smoothness."""

    def test_cpp_vs_python_winding_number_consistency(self):
        """Verify C++ engine vs Python engine winding number consistency on complex 3D meshes."""
        verts, faces = create_uv_sphere(radius=1.0, n_lat=16, n_lon=24)
        tri_v0 = verts[faces[:, 0]]
        tri_v1 = verts[faces[:, 1]]
        tri_v2 = verts[faces[:, 2]]

        query_pts = np.array([
            [0.0, 0.0, 0.0],       # deep inside -> W ~ 1.0
            [0.5, 0.0, 0.0],       # inside -> W ~ 1.0
            [2.5, 0.0, 0.0],       # far outside -> W ~ 0.0
            [0.0, 3.0, 0.0],       # far outside -> W ~ 0.0
        ], dtype=np.float64)

        wn_py = compute_mesh_winding_number(query_pts, tri_v0, tri_v1, tri_v2, chunk_size=16)

        assert wn_py[0] == pytest.approx(1.0, abs=0.05)
        assert wn_py[1] == pytest.approx(1.0, abs=0.05)
        assert wn_py[2] == pytest.approx(0.0, abs=0.05)
        assert wn_py[3] == pytest.approx(0.0, abs=0.05)

        dll = get_native_dll()
        if dll is not None and hasattr(dll, "cfd_sdf_winding_number"):
            sm = SurfaceMesh(vertices=verts, faces=faces)
            sdf_native = CADMeshSDF(sm, use_native=True)
            wn_cpp = sdf_native.winding_number(query_pts)
            np.testing.assert_allclose(wn_cpp, wn_py, atol=0.05)

    def test_dipole_cutoff_smoothness(self):
        """Verify far-field dipole cutoff smoothness near dist = 2.0 * node_radius."""
        grid_x = np.linspace(-0.2, 0.2, 5)
        grid_y = np.linspace(-0.2, 0.2, 5)
        xx, yy = np.meshgrid(grid_x, grid_y)
        v = np.column_stack([xx.ravel(), yy.ravel(), np.zeros(25)])
        f = []
        for i in range(4):
            for j in range(4):
                idx = i * 5 + j
                f.append([idx, idx + 1, idx + 6])
                f.append([idx, idx + 6, idx + 5])
        faces = np.array(f, dtype=np.int32)
        tri_v0 = v[faces[:, 0]]
        tri_v1 = v[faces[:, 1]]
        tri_v2 = v[faces[:, 2]]

        c_bmin = np.min(v, axis=0)
        c_bmax = np.max(v, axis=0)
        node_radius = 0.5 * float(np.linalg.norm(c_bmax - c_bmin))

        r_thresh = 2.0 * node_radius
        d_below = r_thresh * 0.999
        d_above = r_thresh * 1.001

        p_below = np.array([[0.0, 0.0, d_below]])
        p_above = np.array([[0.0, 0.0, d_above]])

        wn_below = compute_mesh_winding_number(p_below, tri_v0, tri_v1, tri_v2, chunk_size=32)
        wn_above = compute_mesh_winding_number(p_above, tri_v0, tri_v1, tri_v2, chunk_size=32)

        val_below = float(np.ravel(wn_below)[0])
        val_above = float(np.ravel(wn_above)[0])
        diff = abs(val_below - val_above)
        # Verify continuity across the cutoff threshold (difference < 0.05)
        assert diff < 0.05, f"Discontinuous jump at dipole cutoff: {diff}"


# ==============================================================================
# 6. R7 Static Audit Detail Fixes
# ==============================================================================


class TestAdversarialStaticAuditFixes:
    """Stress-test polymesh_io patch_types and inflation layer normal reversal."""

    def test_polymesh_io_patch_types_mapping(self, tmp_path):
        """Test custom patch_types mapping and default fallback for farfield."""
        pts = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
        ], dtype=np.float64)
        cells = [np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)]
        # Define 6 quad faces
        faces = [
            np.array([0, 3, 2, 1], dtype=np.int64),  # bottom
            np.array([4, 5, 6, 7], dtype=np.int64),  # top
            np.array([0, 1, 5, 4], dtype=np.int64),  # front
            np.array([2, 3, 7, 6], dtype=np.int64),  # back
            np.array([0, 4, 7, 3], dtype=np.int64),  # left
            np.array([1, 2, 6, 5], dtype=np.int64),  # right
        ]
        boundaries = {
            "inlet": (0, 1),
            "outlet": (1, 1),
            "top": (2, 1),
            "sides": (3, 1),
            "wall": (4, 2),
        }
        mesh = UnstructuredMesh(
            points=pts, cells=cells, owner=np.array([0, 0, 0, 0, 0, 0], dtype=np.int64),
            neighbour=np.array([-1, -1, -1, -1, -1, -1], dtype=np.int64),
            face_areas=np.ones(6), face_centres=np.zeros((6, 3)),
            cell_centres=np.array([[0.5, 0.5, 0.5]]), cell_volumes=np.array([1.0]),
            delta_f=np.ones(6), k_f=np.zeros((6, 3)), boundaries=boundaries,
            cell_regions=np.array([0]), baffles=[], error_map={}, faces=faces,
        )

        out_dir_default = tmp_path / "polymesh_default"
        export_openfoam_polymesh(mesh, out_dir_default)
        bnd_content_default = (out_dir_default / "constant" / "polyMesh" / "boundary").read_text()

        # In default fallback: top & sides map to symmetry
        assert "type            symmetry;" in bnd_content_default

        # Custom patch_types mapping
        custom_types = {"top": "patch", "sides": "patch"}
        out_dir_custom = tmp_path / "polymesh_custom"
        export_openfoam_polymesh(mesh, out_dir_custom, patch_types=custom_types)
        bnd_content_custom = (out_dir_custom / "constant" / "polyMesh" / "boundary").read_text()

        # In custom mapping: top should be patch
        assert "top\n    {\n        type            patch;" in bnd_content_custom

    def test_inflation_nonplanar_faces_and_normal_reversal(self):
        """Test inflation layer with non-planar boundary faces and inverted normal repair."""
        pts = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [1.0, 1.0, 0.0],
            [0.0, 1.0, 0.05],  # slightly non-planar
        ], dtype=np.float64)
        wall_faces = [[0, 1, 2, 3]]

        # Case 1: nominal_normal is outward [+z]
        cfg = InflationConfig(num_layers=2, first_layer_height=0.01, growth_rate=1.2, pinching=True)
        gen = BoundaryLayerGenerator(cfg)
        mesh_out = gen.inflate_layers(pts, wall_faces, nominal_normal=np.array([0.0, 0.0, 1.0]))

        assert mesh_out.num_cells == 2  # 2 layers of hexes
        assert np.all(mesh_out.cell_volumes > 0.0), "All extruded cell volumes must be positive"

        # Case 2: nominal_normal is opposing [-z], triggering normal flip and f_arr[::-1] reversal
        mesh_flip = gen.inflate_layers(pts, wall_faces, nominal_normal=np.array([0.0, 0.0, -1.0]))

        assert mesh_flip.num_cells == 2
        assert np.all(mesh_flip.cell_volumes > 0.0), "Inverted face normal repair must preserve positive volume"
        hex0 = mesh_flip.cells[0]
        # Bottom quad in flipped orientation should be reversed: [3, 2, 1, 0] instead of [0, 1, 2, 3]
        assert list(hex0[:4]) == [3, 2, 1, 0]
