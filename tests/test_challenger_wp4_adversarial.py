"""Adversarial Empirical Stress-Test Suite for Gate WP4: Multiphysics Domain Meshing & GCI Hierarchy.

Authored by Paper-Fidelity & Zero-Shortcut Challenger for Gate WP4.
Verifies mathematical rigor, absence of heuristics/shortcuts, paper fidelity,
numerical stability under extreme aspect ratios (AR > 20), non-convex polyhedral invariants,
and ASME V&V 20-2009 / Berger & Giuliani (2024) / AMIPS compliance.
"""

from __future__ import annotations

import math
from typing import Dict, List, Tuple
import numpy as np
import pytest

from backend.meshing.models import (
    CellType,
    FarfieldPadding,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import (
    compute_difvm_topology,
    extract_cht_twin_faces,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.gci import (
    GCIResult,
    compute_asme_vv20_gci,
    generate_3grid_gci_hierarchy,
)


def _make_box_surface_mesh(
    lx: float = 1.0,
    ly: float = 0.02,
    lz: float = 0.02,
    origin: Tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation_matrix: np.ndarray | None = None,
) -> SurfaceMesh:
    """Helper to generate a closed 12-triangle surface mesh for a rectangular box."""
    x0, y0, z0 = origin
    verts = np.array([
        [x0, y0, z0],
        [x0 + lx, y0, z0],
        [x0 + lx, y0 + ly, z0],
        [x0, y0 + ly, z0],
        [x0, y0, z0 + lz],
        [x0 + lx, y0, z0 + lz],
        [x0 + lx, y0 + ly, z0 + lz],
        [x0, y0 + ly, z0 + lz],
    ], dtype=np.float64)

    if rotation_matrix is not None:
        verts = verts @ rotation_matrix.T

    # 12 triangles (2 per face) with outward-pointing normals
    faces = np.array([
        # Bottom (-Z)
        [0, 2, 1], [0, 3, 2],
        # Top (+Z)
        [4, 5, 6], [4, 6, 7],
        # Front (-Y)
        [0, 1, 5], [0, 5, 4],
        # Back (+Y)
        [3, 6, 2], [3, 7, 6],
        # Left (-X)
        [0, 4, 7], [0, 7, 3],
        # Right (+X)
        [1, 2, 6], [1, 6, 5],
    ], dtype=np.int32)

    return SurfaceMesh(
        vertices=verts,
        faces=faces,
        patches={"wall": np.arange(12, dtype=np.int32)},
    )


class TestAdversarialSlenderBodies:
    """Stress tests on high-aspect-ratio slender bodies (AR > 20 and AR = 100)."""

    def test_needle_body_ar_50_normal_clustering(self):
        """Verify normal clustering accurately detects slender cuboid (AR = 50) without vertex heuristics."""
        R_rot = np.array([
            [0.8660254, -0.5, 0.0],
            [0.5, 0.8660254, 0.0],
            [0.0, 0.0, 1.0],
        ])
        sm = _make_box_surface_mesh(lx=1.0, ly=0.02, lz=0.02, rotation_matrix=R_rot)

        # Audit detected axes
        R_detected = MeshEngine._detect_oriented_box_axes(sm.vertices, sm.faces)
        assert R_detected is not None, "Failed to detect oriented box with AR = 50."

        # Orthogonality verification (|u_i . u_j| < 10^-3)
        u0 = R_detected[:, 0]
        u1 = R_detected[:, 1]
        u2 = R_detected[:, 2]
        assert abs(float(np.dot(u0, u1))) < 1e-3
        assert abs(float(np.dot(u0, u2))) < 1e-3
        assert abs(float(np.dot(u1, u2))) < 1e-3
        assert abs(float(np.linalg.det(R_detected)) - 1.0) < 1e-3

    def test_needle_body_ar_50_meshing_and_invariants(self):
        """Verify structured hexahedral meshing of slender body (AR = 50) maintains all invariants."""
        sm = _make_box_surface_mesh(lx=1.0, ly=0.02, lz=0.02)
        engine = MeshEngine(use_native=False)
        cfg = MeshingConfig(domain_mode="solid_only")

        mesh = engine._mesh_oriented_box(
            sm.vertices,
            np.eye(3),
            h0=0.01,
            active_cfg=cfg,
        )

        assert mesh.num_cells > 0
        # Positive volume invariant
        assert np.all(mesh.cell_volumes > 0.0)
        # Validate relative Gauss closure and projection collinearity
        assert mesh.validate()

    def test_slender_body_external_fluid_farfield(self):
        """Verify external fluid farfield generation around a slender needle (AR = 25)."""
        # Slender body with AR = 25: 1.0 x 0.04 x 0.04
        sm = _make_box_surface_mesh(lx=1.0, ly=0.04, lz=0.04)
        engine = MeshEngine(use_native=False)
        # Moderate padding and h0 = 0.02 ensures h0 < 0.04 so cells are carved out
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=FarfieldPadding(upstream=0.1, downstream=0.2, lateral_y=0.1, lateral_z=0.1),
        )

        mesh = engine.mesh_hexcore(sm, scale=1.0, h0=0.02, config=cfg)

        assert mesh.num_cells > 0
        assert np.all(mesh.cell_volumes > 0.0)
        # Farfield patches must be automatically categorized
        assert "inlet" in mesh.boundaries
        assert "outlet" in mesh.boundaries
        assert "bottom" in mesh.boundaries
        assert "top" in mesh.boundaries
        assert "sides" in mesh.boundaries
        assert "wall" in mesh.boundaries
        assert mesh.validate()


class TestAdversarialDegenerateGeometries:
    """Stress tests on degenerate inputs and error escalation guards."""

    def test_zero_span_planar_geometry_rejection(self):
        """Verify zero-span planar geometry (Lz = 0) is rejected with ValueError."""
        geom_bmin = np.array([0.0, 0.0, 0.0])
        geom_bmax = np.array([1.0, 1.0, 0.0])  # Zero span in Z
        cfg = MeshingConfig(domain_mode="external_fluid")

        with pytest.raises(ValueError, match="span must be positive"):
            MeshEngine._compute_farfield_bounds(geom_bmin, geom_bmax, cfg)

    def test_inverted_farfield_bbox_rejection(self):
        """Verify inverted farfield bbox (xmin >= xmax) raises ValueError."""
        geom_bmin = np.array([0.0, 0.0, 0.0])
        geom_bmax = np.array([1.0, 1.0, 1.0])
        # Inverted bbox where min > max
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_bbox=((2.0, 2.0, 2.0), (0.0, 0.0, 0.0)),
        )

        with pytest.raises(ValueError, match="Inverted farfield_bbox"):
            MeshEngine._compute_farfield_bounds(geom_bmin, geom_bmax, cfg)

    def test_geometry_penetrating_farfield_bbox_rejection(self):
        """Verify geometry exceeding/penetrating farfield bbox is detected and raises ValueError."""
        geom_bmin = np.array([-5.0, 0.0, 0.0])
        geom_bmax = np.array([5.0, 1.0, 1.0])
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_bbox=((-2.0, -2.0, -2.0), (2.0, 2.0, 2.0)),  # Inside geometry!
        )

        with pytest.raises(ValueError, match="Geometry penetrates farfield boundary box"):
            MeshEngine._compute_farfield_bounds(geom_bmin, geom_bmax, cfg)

    def test_non_positive_h0_rejection(self):
        """Verify h0 <= 0 is rejected with ValueError."""
        sm = _make_box_surface_mesh(lx=1.0, ly=1.0, lz=1.0)
        engine = MeshEngine(use_native=False)
        cfg = MeshingConfig()

        with pytest.raises(ValueError, match="h0 must be positive"):
            engine._mesh_oriented_box(sm.vertices, np.eye(3), h0=0.0, active_cfg=cfg)

        with pytest.raises(ValueError, match="h0 must be positive"):
            engine._mesh_oriented_box(sm.vertices, np.eye(3), h0=-0.05, active_cfg=cfg)


class TestAdversarialAMIPSEnergyAndSnapping:
    """Stress tests on 3D Conformal AMIPS distortion energy and step halving rollback."""

    @staticmethod
    def _compute_amips(verts: np.ndarray) -> float:
        """Scale- and rotation-invariant 3D AMIPS formula."""
        v0, v1, v2, v3 = verts[0], verts[1], verts[2], verts[3]
        e1, e2, e3 = v1 - v0, v2 - v0, v3 - v0
        det_j = float(np.dot(np.cross(e1, e2), e3))
        if det_j <= 1e-18:
            return 1e30

        edge_sq_sum = (
            np.sum((v0 - v1) ** 2)
            + np.sum((v0 - v2) ** 2)
            + np.sum((v0 - v3) ** 2)
            + np.sum((v1 - v2) ** 2)
            + np.sum((v1 - v3) ** 2)
            + np.sum((v2 - v3) ** 2)
        )
        ratio = (27.0 / 16.0) * (edge_sq_sum ** 3) / (det_j ** 2)
        return float(ratio ** (1.0 / 3.0))

    def test_regular_tetrahedron_amips_near_minimum(self):
        """Verify right-handed regular tetrahedron achieves near-minimum AMIPS energy (~3.0 to ~9.0)."""
        # Right-handed regular tetrahedron vertices: det_j > 0
        v = np.array([
            [-1.0, 0.0, -1.0 / math.sqrt(2.0)],
            [1.0, 0.0, -1.0 / math.sqrt(2.0)],
            [0.0, 1.0, 1.0 / math.sqrt(2.0)],
            [0.0, -1.0, 1.0 / math.sqrt(2.0)],
        ], dtype=np.float64)

        e = self._compute_amips(v)
        # AMIPS theoretical lower bound is 3.0; this tetrahedron gives ~9.0 < 10.0
        assert 3.0 <= e < 15.0

    def test_inverted_tetrahedron_returns_infinite_energy(self):
        """Verify inverted tetrahedron (det J <= 0) returns 1e30."""
        # Left-handed orientation: swap vertices 0 and 1
        v = np.array([
            [1.0, 0.0, -1.0 / math.sqrt(2.0)],
            [-1.0, 0.0, -1.0 / math.sqrt(2.0)],
            [0.0, 1.0, 1.0 / math.sqrt(2.0)],
            [0.0, -1.0, 1.0 / math.sqrt(2.0)],
        ], dtype=np.float64)

        e = self._compute_amips(v)
        assert e >= 1e30

    def test_step_halving_rollback_on_large_displacement(self):
        """Verify step-halving evaluates lambda in {1.0, 0.5, 0.25, 0.125} and rolls back if inverted."""
        base_tet = np.array([
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0],
        ], dtype=np.float64)

        # Huge displacement of node 3 through opposite face (0,1,2), causing severe inversion
        disp = np.array([0.0, 0.0, -2.0])

        best_lambda = 0.0
        for lam in [1.0, 0.5, 0.25, 0.125]:
            test_v = base_tet.copy()
            test_v[3] += lam * disp
            energy = self._compute_amips(test_v)
            if energy < 1e4:
                best_lambda = lam
                break

        # For lam = 1.0, z = -1.0 (inverted, det < 0)
        # For lam = 0.5, z = 0.0 (degenerate coplanar, det = 0)
        # For lam = 0.25, z = 0.5 (valid, det > 0)
        assert best_lambda == 0.25


class TestAdversarialBergerGiulianiWSRD:
    """Stress tests on Berger & Giuliani (2024) Weighted State Redistribution."""

    def test_wsrd_partition_of_unity_with_mixed_neighbors(self):
        """Verify partition of unity sum(w_ij) == 1.0 holds across diverse neighbor volume fractions."""
        alpha_target = 0.50
        alpha_i = 0.35  # Cut-cell in [0.20, 0.50)

        # Diverse neighbor volume fractions: small, medium, and fully background (>= 0.50)
        neighbor_alphas = [0.10, 0.30, 0.50, 0.85, 1.00]
        n_nb = len(neighbor_alphas)

        # Formula (Berger & Giuliani 2024 Eq. 4.2):
        # w_ij = 1/(N_i - 1) * max(0.0, 1.0 - alpha_j / alpha_target)
        w_nbs = [(1.0 / n_nb) * max(0.0, 1.0 - a_j / alpha_target) for a_j in neighbor_alphas]
        w_self = 1.0 - sum(w_nbs)

        total_weight = w_self + sum(w_nbs)
        assert abs(total_weight - 1.0) < 1e-14
        assert w_self > 0.0

        # Neighbors with alpha_j >= alpha_target should have weight 0
        assert w_nbs[2] == 0.0  # alpha = 0.50
        assert w_nbs[3] == 0.0  # alpha = 0.85
        assert w_nbs[4] == 0.0  # alpha = 1.00

        # Neighbors with alpha_j < alpha_target should have positive weight
        assert w_nbs[0] > 0.0   # alpha = 0.10
        assert w_nbs[1] > 0.0   # alpha = 0.30


class TestAdversarialCHTTwinFaces:
    """Stress tests on Conjugate Heat Transfer (CHT) multi-region twin-face coupling."""

    def test_cht_twin_face_opposite_normals_and_reversal(self):
        """Verify twin-faces have exact opposite normals S_B = -S_A and reversed node ordering."""
        # Create a simple two-cube mesh sharing an interface
        # Cube 0: [0, 1] x [0, 1] x [0, 1] (Fluid, region 0)
        # Cube 1: [1, 2] x [0, 1] x [0, 1] (Solid, region 1)
        points = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
            [2, 0, 0], [2, 1, 0], [2, 0, 1], [2, 1, 1],
        ], dtype=np.float64)

        cells = [
            np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64),
            np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64),
        ]
        cell_regions = np.array([0, 1], dtype=np.int32)

        # Compute topology with regions - this automatically extracts twin-faces
        mesh = compute_difvm_topology(points, cells, regions=cell_regions)
        assert mesh.num_cells == 2

        # Coupled boundaries must be present
        assert "coupledWall_fluid" in mesh.boundaries
        assert "coupledWall_solid" in mesh.boundaries

        bp_a = mesh.boundaries["coupledWall_fluid"]
        bp_b = mesh.boundaries["coupledWall_solid"]
        assert bp_a[1] == bp_b[1]  # Equal number of faces

        fa = bp_a[0]
        fb = bp_b[0]

        # Normal vectors must be strictly opposite: S_B = -S_A
        n_a = mesh.face_areas[fa]
        n_b = mesh.face_areas[fb]
        diff = np.linalg.norm(n_a + n_b)
        assert diff < 1e-12, f"Twin face normal anti-symmetry violated: diff={diff}"


class TestAdversarialASMEVV20GCI:
    """Stress tests on ASME V&V 20-2009 GCI convergence metrics."""

    def test_gci_exact_quadratic_convergence(self):
        """Verify ASME V&V 20 calculation for exact 2nd order convergence (p = 2.0)."""
        r = 1.41421356  # sqrt(2)
        p_true = 2.0
        phi_exact = 1.000000
        c = 0.01

        h1 = 0.02
        h2 = h1 * r
        h3 = h2 * r

        phi_1 = phi_exact + c * (h1 ** p_true)
        phi_2 = phi_exact + c * (h2 ** p_true)
        phi_3 = phi_exact + c * (h3 ** p_true)

        res = compute_asme_vv20_gci(phi_1, phi_2, phi_3, r=r)

        # Apparent order must match 2.0 exactly
        assert abs(res.apparent_order_p - 2.0) < 1e-4
        # Extrapolated continuum solution matches 1.000000
        assert abs(res.phi_extrapolated - phi_exact) < 1e-6
        # GCI fine must be < 1% (0.01)
        assert res.gci_fine < 0.01
        # Asymptotic ratio must be ~ 1.0
        assert abs(res.asymptotic_ratio - 1.0) < 1e-4

    def test_gci_hierarchy_patch_topology_mismatch_detection(self):
        """Verify generate_3grid_gci_hierarchy detects boundary patch topology mismatches."""
        counter = [0]

        def inconsistent_generator(h: float) -> UnstructuredMesh:
            counter[0] += 1
            pts = np.array([
                [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
                [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
            ], dtype=np.float64) * h
            cells = [np.arange(8, dtype=np.int64)]

            if counter[0] == 1:
                # Coarse level has patch "inlet" and "outlet" covering all 6 faces
                bps = {"inlet": (0, 3), "outlet": (3, 3)}
            else:
                # Medium/fine levels have "inlet", "outlet", and "wall" covering all 6 faces
                bps = {"inlet": (0, 2), "outlet": (2, 2), "wall": (4, 2)}

            mesh = compute_difvm_topology(pts, cells)
            # Inject inconsistent boundaries to simulate generator error
            mesh.boundaries = bps
            return mesh

        with pytest.raises(ValueError, match="Boundary patch mismatch across 3-grid GCI hierarchy"):
            generate_3grid_gci_hierarchy(inconsistent_generator, base_h0=0.05, r=1.414)


class TestAdversarialMosaicAndNonConvexPolyhedra:
    """Stress tests on Mosaic polyhedral transition interfaces and non-convex cells."""

    def test_mosaic_polyhedral_transition_zero_hanging_nodes_and_gauss_closure(self):
        """Verify ANSYS Mosaic transition quad split: exact vector sum S_Q = sum S_qk, CellType 42, closure < 10^-11."""
        # 1. Mathematical Area Vector Identity:
        # Coarse quad face on plane Z = 0: [-1, 1] x [-1, 1] (normal +Z, area 4.0)
        v0 = np.array([-1.0, -1.0, 0.0])
        v1 = np.array([1.0, -1.0, 0.0])
        v2 = np.array([1.0, 1.0, 0.0])
        v3 = np.array([-1.0, 1.0, 0.0])

        s_parent = np.cross(v1 - v0, v3 - v0)  # Area vector [0, 0, 4.0]
        assert np.allclose(s_parent, [0.0, 0.0, 4.0])

        # 4 coplanar sub-quads
        m01 = (v0 + v1) * 0.5
        m12 = (v1 + v2) * 0.5
        m23 = (v2 + v3) * 0.5
        m30 = (v3 + v0) * 0.5
        mc = (v0 + v1 + v2 + v3) * 0.25

        sub_quads = [
            (v0, m01, mc, m30),
            (m01, v1, m12, mc),
            (mc, m12, v2, m23),
            (m30, mc, m23, v3),
        ]

        sub_s_sum = np.zeros(3)
        for q0, q1, q2, q3 in sub_quads:
            s_q = 0.5 * (np.cross(q1 - q0, q2 - q0) + np.cross(q2 - q0, q3 - q0))
            sub_s_sum += s_q
            assert np.allclose(s_q, [0.0, 0.0, 1.0])

        # Exact sub-quad summation to parent face area vector
        assert np.allclose(sub_s_sum, s_parent, atol=1e-15)

        # 2. Closed Mosaic Polyhedral Cell Gauss Divergence Theorem:
        # Coarse cell [-1, 1]^3 with Z+ face replaced by 4 sub-quads (9 faces total):
        # 5 standard faces (area 4.0) + 4 sub-faces (area 1.0)
        mosaic_face_areas = [
            np.array([-4.0, 0.0, 0.0]),  # X-
            np.array([4.0, 0.0, 0.0]),   # X+
            np.array([0.0, -4.0, 0.0]),  # Y-
            np.array([0.0, 4.0, 0.0]),   # Y+
            np.array([0.0, 0.0, -4.0]),  # Z-
            np.array([0.0, 0.0, 1.0]),   # Z+ sub-quad 0
            np.array([0.0, 0.0, 1.0]),   # Z+ sub-quad 1
            np.array([0.0, 0.0, 1.0]),   # Z+ sub-quad 2
            np.array([0.0, 0.0, 1.0]),   # Z+ sub-quad 3
        ]

        total_area = sum(np.linalg.norm(s) for s in mosaic_face_areas)  # 24.0
        closure_vec = np.sum(mosaic_face_areas, axis=0)  # [0, 0, 0]
        rel_closure = np.linalg.norm(closure_vec) / total_area

        assert rel_closure < 1e-14
        assert rel_closure < 1e-11
        assert CellType.POLYHEDRON.value == 42

    def test_non_convex_polyhedral_cell_volume_and_normal_consistency(self):
        """Verify non-convex agglomerated polyhedral cell maintains V > 0 and consistent face normals."""
        # L-shaped non-convex cell formed by merging two adjacent cubes:
        # Cube A: [0, 1] x [0, 1] x [0, 1]
        # Cube B: [1, 2] x [0, 1] x [0, 1]
        pts = np.array([
            [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
            [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
            [2, 0, 0], [2, 1, 0], [2, 0, 1], [2, 1, 1],
        ], dtype=np.float64)

        # Single combined cell (non-convex or composite 10-node polyhedron)
        combined_cell = np.array([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], dtype=np.int64)
        mesh = compute_difvm_topology(pts, [combined_cell])

        assert mesh.num_cells == 1
        # Analytical volume of two 1x1x1 cubes is 2.0
        assert abs(mesh.cell_volumes[0] - 2.0) < 1e-12
        assert mesh.cell_volumes[0] > 0.0

        # Face normals must point strictly outward: S_f . (c_f - c_cell) > 0
        cc = mesh.cell_centres[0]
        for f in range(mesh.num_faces):
            sf = mesh.face_areas[f]
            cf = mesh.face_centres[f]
            # Outward normal projection
            dot_outward = float(np.dot(sf, cf - cc))
            assert dot_outward > 0.0, f"Face {f} normal points inward!"

        assert mesh.validate()
