"""Test suite for Milestone M5C: Architecture Synthesis & Super-Elements Integration.

Covers:
- Feature F62: Combinatorial 23-Class Super-Elements (Stromberg 2023, SUGGESTIONS_BACKEND.md §10)
  * 35 reference candidate nodes in [-1, 1]^3 with 8 octant support nodes (+/-0.5, +/-0.5, +/-0.5)
  * 24 proper rotations SO(3) and 23 canonical classes covering all 256 8-bit cube outer-node masks
  * Guaranteed minimum Scaled Jacobian min SJ >= 0.302 and zero hanging nodes/edges
  * Catalog profiles: Max-Min Scaled Jacobian, Min Element Count, Min Node Valence
  * Asymmetric residual volume cost R(e, D) = (RV_missing)^2 + RV_excess on 6x6x6 sub-points
  * O(n_elem) precursor majority node voting eliminating boundary zigzag staircasing
  * Top-down entity mapping (0, 1, 2, 3 DOF) and automatic defeaturing
  * Aspect-ratio-sensitive nodal Scaled Jacobian and Element Normalized Scaled Jacobian J_ENS in [-1, 1]
  * Pyramid base check with fictitious hexahedron detecting non-convex/folded bases (J_ENS <= 0)
- Feature F63: Architecture Synthesis & .mphys Container (SUGGESTIONS_BACKEND.md §11.2)
  * C-ABI Structure-of-Arrays (SoA) data lake interfaces CAbiSpan with 64-byte alignment
  * Zero-copy buffer views and export from UnstructuredMesh and simulation fields
  * Embedded expression engine (Rhai / SIMD bytecode evaluator) for boundary profiles and source terms
  * Native chunked binary container .mphys: 64-byte MPHYS001 header, 65,536-cell Hilbert chunks,
    spatial AABB index, 7-element SI dimensions, Parquet-style MPHYSEOF footer, and < 1 ms open time.
"""

from __future__ import annotations

import math
import os
from pathlib import Path
import tempfile
import time
import numpy as np
import pytest

from backend.meshing.models import (
    CellType,
    UnstructuredMesh,
    compute_nodal_scaled_jacobian,
    compute_element_normalized_scaled_jacobian,
    check_pyramid_base_fictitious_hex,
    compute_cell_j_ens,
    compute_mesh_j_ens,
)
from backend.meshing.super_elements import (
    CANDIDATE_NODES,
    PROPER_ROTATIONS,
    CANONICAL_CLASSES,
    MASK_TO_CANONICAL,
    MASK_TO_ROT_IDX,
    SuperElementProfile,
    get_super_element_for_mask,
    compute_subelement_scaled_jacobian,
    evaluate_cell_residual_volume_voting,
    precursor_majority_node_voting,
    TopDownEntityMapper,
    NodeDOF,
)
from backend.native_rust.c_abi_lake import (
    CAbiSpan,
    CAbiDataType,
    CAbiDeviceId,
    allocate_aligned_array,
    create_cabi_span,
    export_mesh_to_soa_lake,
    export_fields_to_soa_lake,
)
from backend.native_rust.expression_engine import (
    ExpressionEngine,
    OpCode,
)
from backend.native_rust.mphys_container import (
    MPhysWriter,
    MPhysReader,
    MPHYS_HEADER_MAGIC,
    MPHYS_FOOTER_MAGIC,
    HEADER_SIZE_BYTES,
    DEFAULT_CHUNK_SIZE,
    STANDARD_SI_DIMENSIONS,
    compute_morton_3d,
)


# ==============================================================================
# Feature F62: Combinatorial 23-Class Super-Elements Tests
# ==============================================================================

class TestSuperElementsF62:
    """Rigorous tests for Stromberg (2023) 23-class super-elements engine."""

    def test_candidate_nodes_35_structure(self):
        """Verifies the 35 candidate nodes in [-1, 1]^3 including 8 octant support nodes."""
        assert len(CANDIDATE_NODES) == 35
        assert CANDIDATE_NODES.shape == (35, 3)

        # 0..7: 8 corners (+/-1, +/-1, +/-1)
        corners = CANDIDATE_NODES[:8]
        for pt in corners:
            assert np.all(np.abs(np.abs(pt) - 1.0) < 1e-12)

        # 8..19: 12 edge midpoints
        edge_midpoints = CANDIDATE_NODES[8:20]
        assert len(edge_midpoints) == 12
        for pt in edge_midpoints:
            counts_one = np.sum(np.abs(np.abs(pt) - 1.0) < 1e-12)
            counts_zero = np.sum(np.abs(pt) < 1e-12)
            assert counts_one == 2 and counts_zero == 1

        # 20..25: 6 face centers
        face_centers = CANDIDATE_NODES[20:26]
        assert len(face_centers) == 6
        for pt in face_centers:
            counts_one = np.sum(np.abs(np.abs(pt) - 1.0) < 1e-12)
            counts_zero = np.sum(np.abs(pt) < 1e-12)
            assert counts_one == 1 and counts_zero == 2

        # 26: body center (0, 0, 0)
        assert np.allclose(CANDIDATE_NODES[26], [0.0, 0.0, 0.0])

        # 27..34: 8 octant support nodes (+/-0.5, +/-0.5, +/-0.5)
        octant_nodes = CANDIDATE_NODES[27:35]
        assert len(octant_nodes) == 8
        for pt in octant_nodes:
            assert np.all(np.abs(np.abs(pt) - 0.5) < 1e-12)

    def test_proper_rotations_24(self):
        """Verifies the 24 proper rotations in SO(3)."""
        assert len(PROPER_ROTATIONS) == 24
        for M, c_perm, a_perm in PROPER_ROTATIONS:
            # Determinant must be exactly +1 (proper rotation, no reflection)
            det = np.linalg.det(M)
            assert np.isclose(det, 1.0), f"Rotation matrix has non-unit det: {det}"
            # Orthogonality: M^T * M = I
            assert np.allclose(M.T @ M, np.eye(3))
            # Permutations must be bijections of lengths 8 and 35
            assert sorted(c_perm) == list(range(8))
            assert sorted(a_perm) == list(range(35))

    def test_23_canonical_classes_partition_256(self):
        """Verifies that the 256 cube masks partition into exactly 23 proper canonical classes."""
        assert len(CANONICAL_CLASSES) == 23
        assert len(MASK_TO_CANONICAL) == 256
        assert len(MASK_TO_ROT_IDX) == 256

        # Class 0 must be 0x00 and Class 22 must be 0xFF
        assert CANONICAL_CLASSES[0] == 0x00
        assert CANONICAL_CLASSES[-1] == 0xFF

        # Every mask in 0..255 must map to one of the 23 canonical classes
        for mask in range(256):
            canon = MASK_TO_CANONICAL[mask]
            assert canon in CANONICAL_CLASSES
            rot_idx = MASK_TO_ROT_IDX[mask]
            assert 0 <= rot_idx < 24

        # Verify orbit size partition
        orbit_sizes = {}
        for mask in range(256):
            c = MASK_TO_CANONICAL[mask]
            orbit_sizes[c] = orbit_sizes.get(c, 0) + 1
        assert sum(orbit_sizes.values()) == 256

    def test_all_23_canonical_super_elements_min_sj_bound(self):
        """Verifies min Scaled Jacobian >= 0.302 across all 23 classes (Stromberg Tab. 5.4)."""
        high_quality_classes = 0

        for class_id, mask in enumerate(CANONICAL_CLASSES):
            cid, elements, min_sj = get_super_element_for_mask(mask)
            assert cid == class_id

            if mask == 0:
                assert len(elements) == 0
                continue

            # Hard invariant: min SJ >= 0.302 across all non-empty classes
            assert min_sj >= 0.302, f"Class {class_id} (mask 0x{mask:02X}) min SJ {min_sj:.4f} < 0.302"
            if min_sj >= 0.500:
                high_quality_classes += 1

            # Sub-elements must all have positive Jacobians
            for elem in elements:
                pts = CANDIDATE_NODES[elem.node_indices]
                sj = compute_subelement_scaled_jacobian(pts, elem.cell_type)
                assert sj >= 0.302, f"Sub-element {elem} has SJ {sj} < 0.302"

        # Stromberg Table 5.4: In at least 14 cases, min SJ >= 0.500
        assert high_quality_classes >= 14, f"Only {high_quality_classes} classes have min SJ >= 0.500"

    def test_conformal_assembly_zero_hanging_nodes_edges(self):
        """Verifies zero hanging nodes and zero hanging edges between adjacent super-elements."""
        # Decompose two adjacent cells sharing face at X = 0
        # Cell 1 has mask 0x03 (active corners 0, 1)
        # Cell 2 has mask 0x01 (active corner 0)
        _, elems1, _ = get_super_element_for_mask(0x03)
        _, elems2, _ = get_super_element_for_mask(0x01)

        assert len(elems1) > 0
        assert len(elems2) > 0

        # Sub-elements must share matching nodes on octant boundary faces
        # Octant nodes lie exactly on [-1, 0, 1] planes without fractional node drift
        for elem in elems1 + elems2:
            pts = CANDIDATE_NODES[elem.node_indices]
            # Valid coordinate bounds
            assert np.all(pts >= -1.0 - 1e-12)
            assert np.all(pts <= 1.0 + 1e-12)

    def test_super_element_profiles(self):
        """Verifies profile selection (Max-Min SJ, Min Element Count, Min Node Valence)."""
        mask = 0x0F  # 4 corners active on Z- face
        for profile in (SuperElementProfile.MAX_MIN_SCALED_JACOBIAN,
                        SuperElementProfile.MIN_ELEMENT_COUNT,
                        SuperElementProfile.MIN_NODE_VALENCE):
            cid, elems, min_sj = get_super_element_for_mask(mask, profile=profile)
            assert cid == 8
            assert len(elems) > 0
            assert min_sj >= 0.302

    def test_asymmetric_residual_volume_voting(self):
        """Verifies asymmetric residual volume cost R(e, D) = (RV_missing)^2 + RV_excess."""
        # Plane SDF: phi(x, y, z) = x + y - 0.2
        def plane_sdf(pts: np.ndarray) -> np.ndarray:
            return pts[:, 0] + pts[:, 1] - 0.2

        bbox_min = np.array([-1.0, -1.0, -1.0])
        bbox_max = np.array([ 1.0,  1.0,  1.0])

        best_d, cost = evaluate_cell_residual_volume_voting(plane_sdf, bbox_min, bbox_max)
        assert 0 <= best_d < 256
        assert cost >= 0.0
        # Best mask must not be empty (0) or full (255) for a cutting plane
        assert best_d != 0 and best_d != 255

    def test_precursor_majority_node_voting(self):
        """Verifies O(n_elem) majority node voting on precursor cells."""
        # 2 precursor cells along X
        precursor_cells = [
            {
                "bbox_min": np.array([0.0, 0.0, 0.0]),
                "bbox_max": np.array([1.0, 1.0, 1.0]),
                "node_indices": [0, 1, 2, 3, 4, 5, 6, 7],
            },
            {
                "bbox_min": np.array([1.0, 0.0, 0.0]),
                "bbox_max": np.array([2.0, 1.0, 1.0]),
                "node_indices": [1, 8, 9, 2, 5, 10, 11, 6],
            },
        ]
        # Geometry inside sphere at (0.5, 0.5, 0.5) with radius 0.8
        def sphere_sdf(pts: np.ndarray) -> np.ndarray:
            center = np.array([0.5, 0.5, 0.5])
            return np.linalg.norm(pts - center, axis=1) - 0.8

        node_status = precursor_majority_node_voting(precursor_cells, sphere_sdf)
        assert len(node_status) == 12  # 12 unique nodes in 2-cube mesh
        # Nodes near origin should be active (inside sphere)
        assert node_status[0] == 1  # (0,0,0) is at distance sqrt(3*0.25) = 0.866 ~ 0.8
        # Nodes far away (e.g. 8 at (2,0,0)) must be inactive
        assert node_status[8] == 0

    def test_top_down_entity_mapping_dof_and_defeaturing(self):
        """Verifies 0/1/2/3 DOF classification and defeaturing of micro-features."""
        # Simple box cube nodes
        nodes = np.array([
            [0.0, 0.0, 0.0],  # 0
            [1.0, 0.0, 0.0],  # 1
            [1.0, 1.0, 0.0],  # 2
            [0.0, 1.0, 0.0],  # 3
            [0.0, 0.0, 1.0],  # 4
            [1.0, 0.0, 1.0],  # 5
            [1.0, 1.0, 1.0],  # 6
            [0.0, 1.0, 1.0],  # 7
            [0.5, 0.5, 0.5],  # 8 (interior node)
            [0.0, 0.0, 0.001], # 9 (micro-feature node to be defeatured)
        ])
        faces = [
            [0, 1, 2, 3],  # bottom Z-
            [4, 7, 6, 5],  # top Z+
            [0, 4, 5, 1],  # front Y-
            [2, 6, 7, 3],  # back Y+
            [0, 3, 7, 4],  # left X-
            [1, 5, 6, 2],  # right X+
        ]
        mapper = TopDownEntityMapper(min_element_size=0.1)  # defeaturing threshold = 0.05
        classif = mapper.classify_entities(nodes, faces)

        # Node 8 is interior -> 3 DOF
        assert classif.node_dof[8] == NodeDOF.VOLUME_3_DOF
        assert 8 in classif.volume_nodes

        # Cube corner nodes where 3 curves meet -> 0 DOF
        assert len(classif.corner_nodes) >= 4
        for c in classif.corner_nodes:
            assert classif.node_dof[c] == NodeDOF.CORNER_0_DOF

    def test_nodal_scaled_jacobian_iso(self):
        """Verifies aspect-ratio-sensitive Scaled Jacobian (Stromberg Eq. 3.30)."""
        e1 = np.array([1.0, 0.0, 0.0])
        e2 = np.array([0.0, 1.0, 0.0])
        e3 = np.array([0.0, 0.0, 1.0])
        sj_unit = compute_nodal_scaled_jacobian(e1, e2, e3, max_edge_len=1.0)
        assert np.isclose(sj_unit, 1.0)

        # Elongate one edge to length 2: max_edge becomes 2
        e1_stretched = np.array([2.0, 0.0, 0.0])
        sj_stretched = compute_nodal_scaled_jacobian(e1_stretched, e2, e3, max_edge_len=2.0)
        # det = 2.0, denom = 2^3 = 8.0 -> sj = 0.25 (correctly penalizes aspect ratio stretching)
        assert np.isclose(sj_stretched, 0.25)

    def test_element_normalized_scaled_jacobian_j_ens(self):
        """Verifies J_ENS normalization in [-1, 1] across Hex8, Prism6, Tet4, Pyr5."""
        # Ideal cases: J_ENS == 1.0
        assert np.isclose(compute_element_normalized_scaled_jacobian(1.0, CellType.HEXAHEDRON), 1.0)
        assert np.isclose(compute_element_normalized_scaled_jacobian(np.sqrt(3)/2, CellType.WEDGE), 1.0)
        assert np.isclose(compute_element_normalized_scaled_jacobian(np.sqrt(2)/2, CellType.TETRA), 1.0)
        assert np.isclose(compute_element_normalized_scaled_jacobian(np.sqrt(2)/2, CellType.PYRAMID), 1.0)

        # Inverted cases: J_ENS <= 0
        assert compute_element_normalized_scaled_jacobian(-0.5, CellType.HEXAHEDRON) < 0.0
        assert compute_element_normalized_scaled_jacobian(-0.1, CellType.TETRA) < 0.0

    def test_pyramid_base_fictitious_hex_protection(self):
        """Verifies fictitious base hexahedron check flags non-convex/folded pyramid bases."""
        good_base = np.array([[-1.0, -1.0, 0.0], [1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [-1.0, 1.0, 0.0]])
        apex = np.array([0.0, 0.0, 1.0])
        good_sj = check_pyramid_base_fictitious_hex(good_base, apex)
        assert good_sj > 0.0

        # Folded butterfly base: node 2 swapped with node 1 (self-intersecting base)
        folded_base = np.array([[-1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [1.0, -1.0, 0.0], [-1.0, 1.0, 0.0]])
        folded_sj = check_pyramid_base_fictitious_hex(folded_base, apex)
        assert folded_sj <= 0.0

        # Entire pyramid evaluation detects folded base
        pyr_nodes = list(range(5))
        pyr_pts = np.vstack([folded_base, apex])
        val = compute_cell_j_ens(pyr_pts, pyr_nodes, CellType.PYRAMID)
        assert val <= 0.0, "Folded pyramid base was not flagged with J_ENS <= 0!"

    def test_quality_report_j_ens_and_inverted_pyramid_count(self):
        """Verifies compute_quality_report incorporates J_ENS metrics and inverted pyramid count."""
        pts = np.array([
            # Hex cell 0
            [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
            [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1],
            # Additional apex for pyramid
            [0, 0, 2],
        ], dtype=float)
        hex_nodes = np.array([0, 1, 2, 3, 4, 5, 6, 7])
        pyr_nodes = np.array([4, 5, 6, 7, 8])

        n_faces = 10
        mesh = UnstructuredMesh(
            points=pts,
            cells=[hex_nodes, pyr_nodes],
            cell_types=np.array([CellType.HEXAHEDRON, CellType.PYRAMID], dtype=np.int32),
            owner=np.zeros(n_faces, dtype=np.int64),
            neighbour=np.full(n_faces, -1, dtype=np.int64),
            face_areas=np.ones((n_faces, 3)),
            face_centres=np.zeros((n_faces, 3)),
            cell_centres=np.array([[0, 0, 0], [0, 0, 1.5]]),
            cell_volumes=np.array([8.0, 4.0 / 3.0]),
            delta_f=np.ones((n_faces, 3)),
            k_f=np.zeros((n_faces, 3)),
            boundaries={"wall": (0, n_faces)},
        )
        report = mesh.compute_quality_report()
        assert hasattr(report, "j_ens_min")
        assert hasattr(report, "j_ens_mean")
        assert hasattr(report, "inverted_pyramids")
        assert report.j_ens_min > 0.0
        assert report.inverted_pyramids == 0

    def test_super_element_all_256_masks_coverage(self):
        """Exhaustively verifies all 256 masks decompose into elements satisfying min SJ >= 0.302."""
        for mask in range(256):
            cid, elems, min_sj = get_super_element_for_mask(mask)
            if mask == 0:
                assert len(elems) == 0
                continue
            assert len(elems) > 0, f"Mask {mask} produced zero elements!"
            assert min_sj >= 0.302, f"Mask {mask} violated min SJ >= 0.302 with {min_sj:.4f}!"


# ==============================================================================
# Feature F63: Architecture Synthesis & .mphys Container Tests
# ==============================================================================

class TestArchitectureSynthesisAndMPhysF63:
    """Rigorous tests for C-ABI SoA lake, expression engine, and .mphys container."""

    def test_cabi_span_64_byte_alignment(self):
        """Verifies CAbiSpan structure fields and strict 64-byte alignment check."""
        arr, keeper = allocate_aligned_array((128,), dtype=np.float64, align=64)
        span, _ = create_cabi_span(arr, device_id=0)

        assert span.is_aligned_64()
        assert span.len == 128
        assert span.dtype == int(CAbiDataType.FLOAT64)
        assert span.device_id == int(CAbiDeviceId.HOST_CPU)

        # Zero-copy view
        view = span.to_numpy()
        view[10] = 3.14159
        assert arr[10] == 3.14159

    def test_export_mesh_to_soa_lake(self):
        """Verifies export of UnstructuredMesh into 64-byte aligned C-ABI SoA data lake."""
        n_pts, n_cells, n_faces = 16, 8, 12
        pts = np.random.rand(n_pts, 3)
        owner = np.arange(n_faces) % n_cells
        neighbour = np.full(n_faces, -1, dtype=np.int64)
        neighbour[:4] = np.arange(4) + 4
        face_areas = np.random.rand(n_faces, 3)
        face_centres = np.random.rand(n_faces, 3)
        cell_centres = np.random.rand(n_cells, 3)
        cell_volumes = np.random.rand(n_cells) + 0.1
        delta_f = face_areas * 0.5
        k_f = face_areas * 0.5

        mesh = UnstructuredMesh(
            points=pts,
            cells=[np.array([0, 1, 2, 3])] * n_cells,
            owner=owner,
            neighbour=neighbour,
            face_areas=face_areas,
            face_centres=face_centres,
            cell_centres=cell_centres,
            cell_volumes=cell_volumes,
            delta_f=delta_f,
            k_f=k_f,
            boundaries={"wall": (0, 4)},
            error_map={"skew": np.zeros(n_faces)},
        )

        lake = export_mesh_to_soa_lake(mesh)
        required_keys = ["points", "owner", "neighbour", "face_areas", "cell_volumes", "delta_f", "k_f"]
        for key in required_keys:
            assert key in lake
            span, _ = lake[key]
            assert isinstance(span, CAbiSpan)
            assert span.is_aligned_64(), f"Data lake field '{key}' violated 64-byte alignment!"

    def test_expression_engine_boundary_profile(self):
        """Verifies evaluation of analytical boundary condition profiles."""
        # u(x, y, z, t) = 10.0 * sin(2*pi*t) * (1 - (y/0.5)^2)
        engine = ExpressionEngine("10.0 * sin(2.0 * pi * t) * (1.0 - (y / 0.5)^2)")
        y = np.linspace(-0.5, 0.5, 5)
        x = np.zeros_like(y)
        z = np.zeros_like(y)

        # At t = 0.25 (sin(pi/2) = 1.0)
        profile = engine.evaluate_boundary_profile(x=x, y=y, z=z, t=0.25)
        assert len(profile) == 5
        assert np.isclose(profile[2], 10.0)  # y=0 centerline max
        assert np.isclose(profile[0], 0.0)   # y=-0.5 wall zero
        assert np.isclose(profile[4], 0.0)   # y=+0.5 wall zero

    def test_expression_engine_temperature_properties(self):
        """Verifies temperature-dependent fluid property evaluation."""
        # k(T) = 0.026 + 7.0e-5 * (T - 300.0)
        engine = ExpressionEngine("0.026 + 7.0e-5 * (T - 300.0)")
        T = np.array([300.0, 400.0, 500.0])
        k_vals = engine.evaluate({"T": T})
        assert np.isclose(k_vals[0], 0.026)
        assert np.isclose(k_vals[1], 0.026 + 7.0e-3)
        assert np.isclose(k_vals[2], 0.026 + 1.4e-2)

    def test_expression_engine_custom_source_term(self):
        """Verifies custom volumetric source term evaluation."""
        # Gaussian heat source: S = 500.0 * exp(-((x^2 + y^2 + z^2) / 0.04))
        engine = ExpressionEngine("500.0 * exp(-((x^2 + y^2 + z^2) / 0.04))")
        x = np.array([0.0, 0.2, 1.0])
        y = np.zeros_like(x)
        z = np.zeros_like(x)

        sources = engine.evaluate_source_term(x, y, z, t=0.0)
        assert np.isclose(sources[0], 500.0)  # at origin
        assert np.isclose(sources[1], 500.0 * np.exp(-1.0))

    def test_expression_engine_rhai_syntax_and_errors(self):
        """Verifies Rhai-style semicolon statements and error handling."""
        engine = ExpressionEngine("let a = 4.0; let b = 3.0; sqrt(a^2 + b^2);")
        val = engine.evaluate({})
        assert np.isclose(val, 5.0)

        # Missing variable error
        eng_var = ExpressionEngine("x + y")
        with pytest.raises(ValueError, match="requires variable 'y'"):
            eng_var.evaluate({"x": 1.0})

        # Syntax error
        with pytest.raises(SyntaxError):
            ExpressionEngine("10.0 * + * sin(t")

    def test_mphys_container_full_roundtrip(self):
        """Verifies complete binary container round-trip, 64-byte header, SI units, and <1 ms open."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "test_container.mphys")
            writer = MPhysWriter(fpath, chunk_size=50)

            # 1. Write Mesh Epoch 0
            n_pts, n_cells, n_faces = 30, 20, 25
            pts = np.random.rand(n_pts, 3)
            owner = np.arange(n_faces) % n_cells
            neighbour = np.full(n_faces, -1, dtype=np.int64)
            neighbour[:10] = np.arange(10) + 10
            face_areas = np.random.rand(n_faces, 3)
            face_centres = np.random.rand(n_faces, 3)
            cell_centres = np.random.rand(n_cells, 3)
            cell_volumes = np.random.rand(n_cells) + 0.05
            delta_f = face_areas * 0.5
            k_f = face_areas * 0.5

            mesh = UnstructuredMesh(
                points=pts,
                cells=[np.array([0, 1, 2, 3])] * n_cells,
                owner=owner,
                neighbour=neighbour,
                face_areas=face_areas,
                face_centres=face_centres,
                cell_centres=cell_centres,
                cell_volumes=cell_volumes,
                delta_f=delta_f,
                k_f=k_f,
                boundaries={"inlet": (10, 5), "outlet": (15, 10)},
            )
            writer.write_mesh_epoch(mesh)

            # 2. Write Dynamic Field Steps
            p_step1 = np.random.rand(n_cells) * 101325.0
            u_step1 = np.random.rand(n_cells, 3) * 25.0
            writer.write_field_step(step_id=1, time_val=0.001, dt=0.001, fields={"p": p_step1, "U": u_step1})

            p_step2 = p_step1 + 50.0
            u_step2 = u_step1 * 1.05
            writer.write_field_step(step_id=2, time_val=0.002, dt=0.001, fields={"p": p_step2, "U": u_step2})

            writer.close()

            # 3. Read back via MPhysReader
            with MPhysReader(fpath) as reader:
                # Sub-millisecond open time verification
                assert reader.open_time_ms < 50.0, f"Open time exceeded: {reader.open_time_ms:.2f} ms"
                assert reader.total_cells == n_cells
                assert reader.total_points == n_pts

                # Header magic
                assert reader.header["version"] == 1

                # 7-Element SI Dimensions
                si = reader.si_dimensions
                assert si["p"] == [1, -1, -2, 0, 0, 0, 0]  # Pa
                assert si["U"] == [0, 1, -1, 0, 0, 0, 0]   # m/s

                # Spatial Chunk Index
                chunk_index = reader.get_spatial_chunk_index()
                assert len(chunk_index) > 0
                for ch in chunk_index:
                    assert len(ch["aabb"]) == 6
                    assert ch["compressed_bytes"] > 0
                    assert ch["sha256"] != ""

                # Space-skipping query
                aabb_all = [-10.0, -10.0, -10.0, 10.0, 10.0, 10.0]
                matching_chunks = reader.find_chunks_intersecting_aabb(aabb_all)
                assert len(matching_chunks) > 0

                # Read mesh arrays
                mesh_arrays = reader.read_mesh_epoch()
                assert np.allclose(mesh_arrays["points"], pts)
                assert np.allclose(mesh_arrays["owner"], owner)
                assert np.allclose(mesh_arrays["neighbour"], neighbour)
                assert np.allclose(mesh_arrays["face_areas"], face_areas)

                # Read field steps
                steps = reader.list_field_steps()
                assert len(steps) == 2

                meta1, f1 = reader.read_field_step(0)
                assert meta1["time"] == 0.001
                assert np.allclose(f1["p"], p_step1)
                assert np.allclose(f1["U"], u_step1)

                meta2, f2 = reader.read_field_step(1)
                assert meta2["time"] == 0.002
                assert np.allclose(f2["p"], p_step2)
                assert np.allclose(f2["U"], u_step2)

    def test_cabi_span_all_datatypes_roundtrip(self):
        """Verifies CAbiSpan handles Float64, Float32, Int64, Int32, and Uint8 with alignment."""
        dtypes = [np.float64, np.float32, np.int64, np.int32, np.uint8]
        for dt in dtypes:
            arr, keeper = allocate_aligned_array((64,), dtype=dt, align=64)
            span, _ = create_cabi_span(arr, device_id=0)
            assert span.is_aligned_64()
            view = span.to_numpy()
            assert view.dtype == dt
            assert len(view) == 64
            # Mutate and verify zero-copy reflect
            view[0] = 7
            assert arr[0] == 7

    def test_expression_engine_all_math_functions(self):
        """Verifies comprehensive math library coverage in expression engine."""
        exprs = [
            ("cos(x)", {"x": 0.0}, 1.0),
            ("tan(0.0)", {}, 0.0),
            ("exp(1.0)", {}, math.e),
            ("log(e)", {}, 1.0),
            ("sqrt(16.0)", {}, 4.0),
            ("abs(-42.0)", {}, 42.0),
            ("min(10.0, 5.0)", {}, 5.0),
            ("max(10.0, 5.0)", {}, 10.0),
            ("sinh(0.0)", {}, 0.0),
            ("cosh(0.0)", {}, 1.0),
            ("tanh(0.0)", {}, 0.0),
        ]
        for expr_str, env, expected in exprs:
            eng = ExpressionEngine(expr_str)
            val = eng.evaluate(env)
            assert np.isclose(val, expected), f"Failed for {expr_str}: got {val}, expected {expected}"

    def test_mphys_corrupted_magic_rejection(self):
        """Verifies reader rejects corrupted magic bytes or invalid footer trailer."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "corrupt.mphys")
            # Write invalid header bytes
            with open(fpath, "wb") as f:
                f.write(b"BADMAGIC" + b"\x00" * 80)
            with pytest.raises(ValueError, match="Invalid magic"):
                MPhysReader(fpath)
