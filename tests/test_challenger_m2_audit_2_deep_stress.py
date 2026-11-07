"""Adversarial Deep Stress Harness for Milestone M2 Iteration 2 Certification.

Author: challenger_m2_r2_2 (teamwork_challenger)
Target: Empirical verification of 3 remediated defects:
1. gci.py:115 - topological_dim handling (None, missing, varying types, boundary values)
2. difvm_topology.py:508-575 - WSRD donor-receiver pairing and partition of unity stress
3. mesh_engine.py:129 - domain_mode inheritance & dispatch permutations
"""

import numpy as np
import pytest
from backend.geometry.models import SurfaceMesh
from backend.meshing.difvm_topology import compute_wsrd_redistribution_map
from backend.meshing.gci import compute_grid_representative_size
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import MeshingConfig, UnstructuredMesh


class TestDeepStressGCI:
    """Stress-test compute_grid_representative_size edge cases."""

    def test_topological_dim_variations(self):
        pts = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=np.float64)
        cells = [np.array([0, 1, 2, 3], dtype=np.int64)]
        vols = np.array([8.0], dtype=np.float64)

        base_kwargs = dict(
            points=pts, cells=cells, owner=np.array([], dtype=np.int64),
            neighbour=np.array([], dtype=np.int64), face_areas=np.array([], dtype=np.float64),
            face_centres=np.empty((0, 3)), cell_centres=np.array([[0.5, 0.5, 0.5]]),
            cell_volumes=vols, delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([0]), baffles=[], error_map={},
        )

        # 1. Explicit None
        m_none = UnstructuredMesh(**base_kwargs, topological_dim=None)
        assert compute_grid_representative_size(m_none) == pytest.approx(2.0, rel=1e-12)

        # 2. Default/omitted attribute
        m_def = UnstructuredMesh(**base_kwargs)
        assert compute_grid_representative_size(m_def) == pytest.approx(2.0, rel=1e-12)

        # 3. Explicit 3D, 2D, 1D
        m_3d = UnstructuredMesh(**base_kwargs, topological_dim=3)
        assert compute_grid_representative_size(m_3d) == pytest.approx(2.0, rel=1e-12)

        m_2d = UnstructuredMesh(**base_kwargs, topological_dim=2)
        assert compute_grid_representative_size(m_2d) == pytest.approx(np.sqrt(8.0), rel=1e-12)

        m_1d = UnstructuredMesh(**base_kwargs, topological_dim=1)
        assert compute_grid_representative_size(m_1d) == pytest.approx(8.0, rel=1e-12)

        # 4. Float / string convertible
        m_float = UnstructuredMesh(**base_kwargs, topological_dim=3.0)
        assert compute_grid_representative_size(m_float) == pytest.approx(2.0, rel=1e-12)

        m_str = UnstructuredMesh(**base_kwargs, topological_dim="2")
        assert compute_grid_representative_size(m_str) == pytest.approx(np.sqrt(8.0), rel=1e-12)

        # 5. Zero / negative dim clamped to min 1
        m_zero = UnstructuredMesh(**base_kwargs, topological_dim=0)
        assert compute_grid_representative_size(m_zero) == pytest.approx(8.0, rel=1e-12)

        m_neg = UnstructuredMesh(**base_kwargs, topological_dim=-2)
        assert compute_grid_representative_size(m_neg) == pytest.approx(8.0, rel=1e-12)

    def test_gci_error_paths(self):
        base_kwargs = dict(
            points=np.zeros((1, 3)), cells=[], owner=np.array([]),
            neighbour=np.array([]), face_areas=np.array([]),
            face_centres=np.empty((0, 3)), cell_centres=np.empty((0, 3)),
            cell_volumes=np.array([]), delta_f=np.array([]), k_f=np.array([]),
            boundaries={}, cell_regions=np.array([]), baffles=[], error_map={},
        )
        # Empty mesh
        m_empty = UnstructuredMesh(**base_kwargs)
        with pytest.raises(ValueError, match="empty mesh"):
            compute_grid_representative_size(m_empty)

        # Zero or negative mean volume
        base_kwargs["cells"] = [np.array([0])]
        base_kwargs["cell_volumes"] = np.array([0.0])
        base_kwargs["cell_centres"] = np.zeros((1, 3))
        m_zero_vol = UnstructuredMesh(**base_kwargs)
        with pytest.raises(ValueError, match="non-positive"):
            compute_grid_representative_size(m_zero_vol)


class TestDeepStressWSRD:
    """Exhaustive stress-testing of Berger & Giuliani (2024) donor-receiver pairing & partition of unity."""

    def test_randomized_wsrd_invariants_and_unity(self):
        """1000 randomized Monte Carlo configurations testing partition of unity and donor-receiver pairing."""
        rng = np.random.default_rng(42)

        for trial in range(1000):
            n_cells = rng.integers(5, 30)
            a_small = rng.uniform(0.10, 0.25)
            a_target = rng.uniform(a_small + 0.10, 0.70)

            # Assign random volume fractions in [0.05, 1.0]
            wsrd_alpha = rng.uniform(0.05, 1.0, size=n_cells)

            # Build random faces
            n_faces = rng.integers(15, 60)
            owner = rng.integers(0, n_cells, size=n_faces)
            # neighbour can be valid cell or -1 (boundary)
            neighbour = rng.integers(-1, n_cells, size=n_faces)
            # Ensure owner != neighbour
            for f in range(n_faces):
                if neighbour[f] == owner[f]:
                    neighbour[f] = -1

            redist_map = compute_wsrd_redistribution_map(
                wsrd_alpha, owner, neighbour, n_cells, n_faces,
                small_cell_ratio=a_small, wsrd_target_alpha=a_target,
            )

            for donor_c, entry in redist_map.items():
                a_c = float(wsrd_alpha[donor_c])
                assert a_small <= a_c < a_target

                w_self = entry["self"]
                w_nbs = entry["neighbors"]

                assert w_self >= 0.0, f"Negative self weight {w_self}"
                for nb_id, w in w_nbs.items():
                    assert w >= -1e-15, f"Negative neighbor weight {w} for nb {nb_id}"

                # Partition of unity: sum(w) == 1.0 to machine precision (< 1e-14)
                total_w = w_self + sum(w_nbs.values())
                assert abs(total_w - 1.0) < 1e-14, f"Partition of unity violated: {total_w}"

                # Donor-receiver pairing validation:
                # Find all valid internal neighbors
                valid_nbs = set()
                for f in range(n_faces):
                    if neighbour[f] >= 0:
                        if owner[f] == donor_c:
                            valid_nbs.add(int(neighbour[f]))
                        elif neighbour[f] == donor_c:
                            valid_nbs.add(int(owner[f]))

                # Check boundary faces not present
                for nb_id in w_nbs.keys():
                    assert nb_id in valid_nbs
                    assert nb_id >= 0

                # Check donor-receiver preference hierarchy
                regular_larger = [nb for nb in valid_nbs if float(wsrd_alpha[nb]) >= a_target]
                larger_nbs = [nb for nb in valid_nbs if float(wsrd_alpha[nb]) > a_c]

                if regular_larger:
                    # Regular larger neighbors must receive ALL donated weight
                    for nb in valid_nbs:
                        if nb in regular_larger:
                            assert w_nbs[nb] > 0.0, f"Regular larger nb {nb} received 0 weight"
                        else:
                            assert w_nbs[nb] == 0.0, f"Non-regular nb {nb} received non-zero weight {w_nbs[nb]}"
                elif larger_nbs:
                    # Larger cut cells must receive ALL donated weight
                    for nb in valid_nbs:
                        if nb in larger_nbs:
                            assert w_nbs[nb] > 0.0, f"Larger nb {nb} received 0 weight"
                        else:
                            assert w_nbs[nb] == 0.0, f"Smaller nb {nb} received non-zero weight {w_nbs[nb]}"
                else:
                    # No larger neighbors: distributed equally among all valid internal neighbors
                    if valid_nbs:
                        for nb in valid_nbs:
                            assert w_nbs[nb] >= 0.0


class TestDeepStressDomainModeDispatch:
    """Stress-test domain mode routing in MeshEngine across all configuration permutations."""

    def test_cad_dispatch_permutations(self, tmp_path):
        cube_stl = tmp_path / "dispatch_cube.stl"
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

        # Permutation 1: engine config=None, call config=None -> solid_only (legacy backward compat)
        eng1 = MeshEngine(use_native=False, config=None)
        m1 = eng1.mesh_cad_file(cube_stl, h0=0.5, config=None)
        assert isinstance(m1, UnstructuredMesh)
        assert m1.num_cells > 0

        # Permutation 2: engine config=external_fluid, call config=None -> external_fluid (honors instance config)
        eng2 = MeshEngine(use_native=False, config=MeshingConfig(domain_mode="external_fluid"))
        m2 = eng2.mesh_cad_file(cube_stl, h0=0.5, config=None)
        assert isinstance(m2, UnstructuredMesh)
        assert m2.num_cells > 0

        # Permutation 3: engine config=solid_only, call config=None -> solid_only
        eng3 = MeshEngine(use_native=False, config=MeshingConfig(domain_mode="solid_only"))
        m3 = eng3.mesh_cad_file(cube_stl, h0=0.5, config=None)
        assert isinstance(m3, UnstructuredMesh)
        assert m3.num_cells > 0

        # Permutation 4: engine config=external_fluid, call config=solid_only -> argument overrides to solid_only
        m4 = eng2.mesh_cad_file(cube_stl, h0=0.5, config=MeshingConfig(domain_mode="solid_only"))
        assert isinstance(m4, UnstructuredMesh)
        assert m4.num_cells > 0

        # Permutation 5: engine config=solid_only, call config=external_fluid -> argument overrides to external_fluid
        m5 = eng3.mesh_cad_file(cube_stl, h0=0.5, config=MeshingConfig(domain_mode="external_fluid"))
        assert isinstance(m5, UnstructuredMesh)
        assert m5.num_cells > 0
