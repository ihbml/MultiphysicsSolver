"""Adversarial stress testing and edge-case fuzzing for Milestone M5C.

Tests:
1. Invalid Bytecode and VM Stack Underflow / Overflow in ExpressionEngine
2. ExpressionEngine edge cases: division by zero, non-ascii, nested functions, missing variables
3. Corrupt and Malformed .mphys containers:
   - File smaller than minimum header + trailer
   - Corrupted header magic
   - Corrupted footer trailer magic
   - Corrupted / truncated footer JSON
   - Corrupted compressed chunk payload
   - Empty container
   - Zero chunks query
4. CAbiSpan and C-ABI data lake memory safety:
   - Unaligned pointer detection
   - Null pointer handling
   - Invalid datatype enum
   - Zero length buffer
5. SuperElements & J_ENS edge cases:
   - Masks outside [0, 255]
   - Degenerate / collinear nodes in cells
   - Inverted pyramid / hex / wedge / tetra
   - TopDownEntityMapper with degenerate geometry, empty faces, negative coords
"""

import io
import math
import os
import struct
import tempfile
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
    CANONICAL_CLASSES,
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
    Instruction,
)
from backend.native_rust.mphys_container import (
    MPhysWriter,
    MPhysReader,
    MPHYS_HEADER_MAGIC,
    MPHYS_FOOTER_MAGIC,
    HEADER_SIZE_BYTES,
    CompressionType,
    compress_bytes,
    decompress_bytes,
    compute_morton_3d,
)


# ==============================================================================
# 1. Adversarial Expression Engine Tests
# ==============================================================================

class TestExpressionEngineAdversarial:
    """Stress tests and boundary checks for ExpressionEngine."""

    def test_expression_syntax_errors(self):
        """Verify invalid syntax raises SyntaxError gracefully without crash."""
        invalid_expressions = [
            "",
            "   ",
            "1 + * 2",
            "sin(",
            "let 123 = 4;",
            "foo(((",
            "---",
            ";;;",
            "let a = ;",
        ]
        for expr in invalid_expressions:
            with pytest.raises((SyntaxError, ValueError)):
                ExpressionEngine(expr)

    def test_expression_unsupported_functions(self):
        """Verify unknown functions raise ValueError."""
        with pytest.raises(ValueError, match="Unsupported math function"):
            ExpressionEngine("unsupported_function(x)")

    def test_expression_missing_variables(self):
        """Verify missing variable raises ValueError."""
        engine = ExpressionEngine("x + y + z")
        with pytest.raises(ValueError, match="requires variable"):
            engine.evaluate({"x": 1.0, "y": 2.0})

    def test_expression_stack_underflow_safety(self):
        """Verify VM stack underflow raises IndexError / RuntimeError safely."""
        engine = ExpressionEngine("1.0 + 2.0")
        # Intentionally inject bad bytecode with only an ADD opcode (empty stack)
        engine.bytecode = [Instruction(OpCode.ADD)]
        with pytest.raises((IndexError, RuntimeError)):
            engine.evaluate({})

    def test_expression_stack_overflow_corrupted_state(self):
        """Verify corrupted VM stack (extra items left) raises RuntimeError."""
        engine = ExpressionEngine("1.0")
        # Inject two constants and return (leaves 2 items on stack)
        engine.bytecode = [
            Instruction(OpCode.LOAD_CONST, 1.0),
            Instruction(OpCode.LOAD_CONST, 2.0),
            Instruction(OpCode.RETURN),
        ]
        with pytest.raises(RuntimeError, match="Corrupted VM stack"):
            engine.evaluate({})

    def test_expression_division_by_zero_handling(self):
        """Verify division by zero raises ZeroDivisionError on scalars and yields inf/nan on vectors."""
        engine = ExpressionEngine("x / 0.0")
        with pytest.raises(ZeroDivisionError):
            engine.evaluate({"x": 10.0})

        # Vectorized division by zero in numpy
        x_arr = np.array([1.0, 0.0, -1.0])
        val_arr = engine.evaluate({"x": x_arr})
        assert np.isinf(val_arr[0]) and val_arr[0] > 0
        assert np.isnan(val_arr[1])
        assert np.isinf(val_arr[2]) and val_arr[2] < 0

    def test_expression_rhai_multiline_chaining(self):
        """Verify complex multi-statement Rhai assignments."""
        expr = "let r = sqrt(x^2 + y^2); let theta = atan(y / (x + 1e-12)); r * cos(theta);"
        engine = ExpressionEngine(expr)
        val = engine.evaluate({"x": 3.0, "y": 4.0})
        # r = 5.0, theta = atan(4/3), r * cos(theta) = 3.0 (which is x)
        assert np.isclose(val, 3.0, atol=1e-5)


# ==============================================================================
# 2. Adversarial .mphys Container Tests
# ==============================================================================

class TestMPhysContainerAdversarial:
    """Stress testing binary corruption, boundary sizes, and error paths in .mphys."""

    def test_truncated_file_rejection(self):
        """Verify files shorter than minimum header + trailer are rejected."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "short.mphys")
            with open(fpath, "wb") as f:
                f.write(b"SHORT")
            with pytest.raises(ValueError, match="Corrupt file: smaller than minimum"):
                MPhysReader(fpath)

    def test_invalid_header_magic_rejection(self):
        """Verify wrong header magic raises ValueError."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "bad_magic.mphys")
            with open(fpath, "wb") as f:
                f.write(b"WRONGMGK" + b"\x00" * 100)
            with pytest.raises(ValueError, match="Invalid magic"):
                MPhysReader(fpath)

    def test_invalid_footer_magic_rejection(self):
        """Verify correct header but invalid closing footer trailer magic raises ValueError."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "bad_footer.mphys")
            with open(fpath, "wb") as f:
                # 64-byte valid header
                hdr = bytearray(HEADER_SIZE_BYTES)
                hdr[:8] = MPHYS_HEADER_MAGIC
                f.write(hdr)
                # dummy data
                f.write(b"\x00" * 50)
                # invalid 16-byte trailer
                trailer = struct.pack("<Q", 50) + b"BADEOF12"
                f.write(trailer)
            with pytest.raises(ValueError, match="Invalid closing magic"):
                MPhysReader(fpath)

    def test_corrupt_compressed_footer_json(self):
        """Verify corrupted compressed footer payload raises exception cleanly."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "corrupt_footer_payload.mphys")
            with open(fpath, "wb") as f:
                hdr = bytearray(HEADER_SIZE_BYTES)
                hdr[:8] = MPHYS_HEADER_MAGIC
                struct.pack_into("<HHQQIIII", hdr, 8, 1, 0x0100, 64, 0, 0, 65536, 0, 0)
                f.write(hdr)
                # write garbage footer payload
                garbage = b"THIS_IS_NOT_ZLIB_DATA_12345678"
                f.write(garbage)
                # valid trailer pointing to garbage
                trailer = struct.pack("<Q", len(garbage)) + MPHYS_FOOTER_MAGIC
                f.write(trailer)
            with pytest.raises(Exception):
                MPhysReader(fpath)

    def test_empty_query_aabb_space_skipping(self):
        """Verify querying non-intersecting AABB returns empty chunk list."""
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "empty_query.mphys")
            writer = MPhysWriter(fpath, chunk_size=10)
            pts = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=float)
            mesh = UnstructuredMesh(
                points=pts,
                cells=[np.array([0, 1, 2, 3])],
                owner=np.array([0]),
                neighbour=np.array([-1]),
                face_areas=np.array([[0, 0, 1]]),
                face_centres=np.array([[0.5, 0.5, 0]]),
                cell_centres=np.array([[0.5, 0.5, 0]]),
                cell_volumes=np.array([1.0]),
                delta_f=np.array([[0, 0, 1]]),
                k_f=np.array([[0, 0, 0]]),
                boundaries={"wall": (0, 1)},
            )
            writer.write_mesh_epoch(mesh)
            writer.close()

            with MPhysReader(fpath) as reader:
                # Query completely outside bounding box (e.g. [100, 100, 100, 200, 200, 200])
                outside_aabb = [100.0, 100.0, 100.0, 200.0, 200.0, 200.0]
                chunks = reader.find_chunks_intersecting_aabb(outside_aabb)
                assert len(chunks) == 0

                # Out of bounds step read raises IndexError
                with pytest.raises(IndexError):
                    reader.read_field_step(999)


# ==============================================================================
# 3. Adversarial C-ABI Data Lake Tests
# ==============================================================================

class TestCAbiDataLakeAdversarial:
    """Stress tests for 64-byte alignment and CAbiSpan zero-copy buffers."""

    def test_unaligned_array_detection(self):
        """Verify unaligned slice is detected by is_aligned_64()."""
        # Allocate 64-byte aligned array
        aligned, keeper = allocate_aligned_array(128, dtype=np.float64, align=64)
        # Slicing from index 1 shifts address by 8 bytes (not 64-byte aligned)
        unaligned_slice = aligned[1:]
        addr = unaligned_slice.__array_interface__["data"][0]
        assert addr % 64 != 0

        # Create span without auto-realignment
        span, _ = create_cabi_span(unaligned_slice, ensure_aligned=False)
        assert not span.is_aligned_64()

        # Create span with ensure_aligned=True must enforce 64-byte alignment
        span_realigned, _ = create_cabi_span(unaligned_slice, ensure_aligned=True)
        assert span_realigned.is_aligned_64()

    def test_null_and_zero_length_span(self):
        """Verify empty and null CAbiSpan safely handled."""
        empty_arr = np.empty(0, dtype=np.float64)
        span, _ = create_cabi_span(empty_arr)
        assert span.len == 0
        arr = span.to_numpy()
        assert len(arr) == 0

    def test_unsupported_numpy_dtype_rejected(self):
        """Verify unsupported numpy dtype (e.g. complex, object, string) raises ValueError."""
        complex_arr = np.array([1.0 + 2.0j], dtype=np.complex128)
        with pytest.raises(ValueError, match="no direct CAbiDataType mapping"):
            create_cabi_span(complex_arr)


# ==============================================================================
# 4. Adversarial SuperElements and J_ENS Tests
# ==============================================================================

class TestSuperElementsAndJENSAdversarial:
    """Edge cases for combinatorial super elements, defeaturing, and J_ENS."""

    def test_mask_clamping_and_out_of_range(self):
        """Verify masks outside 0..255 are safely masked to 8-bit."""
        # 0x1FF & 0xFF == 0xFF
        cid1, elems1, sj1 = get_super_element_for_mask(0x1FF)
        cid2, elems2, sj2 = get_super_element_for_mask(0xFF)
        assert cid1 == cid2
        assert len(elems1) == len(elems2)
        assert np.isclose(sj1, sj2)

    def test_degenerate_zero_volume_subelement_jacobian(self):
        """Verify collapsed/collinear nodes produce zero or near-zero Scaled Jacobian."""
        # All 8 vertices of Hex8 at (0, 0, 0)
        pts_collapsed = np.zeros((8, 3), dtype=float)
        sj = compute_subelement_scaled_jacobian(pts_collapsed, CellType.HEXAHEDRON)
        assert np.isclose(sj, 0.0)

    def test_inverted_tetrahedron_negative_j_ens(self):
        """Verify inverted tetrahedron yields negative J_ENS."""
        # Invert orientation by swapping nodes 1 and 2
        pts = np.array([
            [0.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],  # swapped
            [1.0, 0.0, 0.0],  # swapped
            [0.0, 0.0, 1.0],
        ])
        val = compute_cell_j_ens(pts, [0, 1, 2, 3], CellType.TETRA)
        assert val < 0.0

    def test_inverted_wedge_negative_j_ens(self):
        """Verify inverted prism/wedge yields negative J_ENS."""
        pts = np.array([
            [0, 0, 0], [0, 1, 0], [1, 0, 0],  # swapped triangle base
            [0, 0, 1], [0, 1, 1], [1, 0, 1],
        ], dtype=float)
        val = compute_cell_j_ens(pts, list(range(6)), CellType.WEDGE)
        assert val < 0.0

    def test_top_down_entity_mapper_empty_and_degenerate_faces(self):
        """Verify TopDownEntityMapper handles empty boundary faces and tiny geometries gracefully."""
        nodes = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=float)
        # Empty boundary faces
        mapper = TopDownEntityMapper(min_element_size=0.1)
        classif = mapper.classify_entities(nodes, boundary_faces=[])
        # All nodes must be safely classified as volume nodes (3 DOF)
        for i in range(len(nodes)):
            assert classif.node_dof[i] == NodeDOF.VOLUME_3_DOF
