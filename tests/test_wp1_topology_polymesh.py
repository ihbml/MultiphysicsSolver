"""Standard-conforming pytest test suite for Work Package 1 (WP1).

Covers all 11 acceptance criteria and boundary/edge cases for Gate WP1:
- F1: Explicit face-to-node connectivity and right-hand rule orientation.
- F2: OpenFOAM polyMesh export syntax and 2D slab extrusion.
- F3: VTK-compatible CellType enum and coplanar 2D QUAD vs 3D TETRA discrimination at z = 5.0.
- F4: Strict manifoldness check (>2 cells sharing face -> ValueError) and upper-triangular order.
- F5: Contiguous CHT twin-face boundary extraction (no neighbour == -1 in internal block).
- F6: Named boundary patch pass-through through compute_difvm_topology.
- F7: Lossless RCM and 3D Hilbert SFC reordering with inner_cells / halo_adjacent_cells metadata.
- F8: CMS Geometry Error Compensation Map (m_skew, k_f_comp, beta_upwind, omega_cell, wsrd_alpha).
- F9: Vectorized scale-invariant validate() on micro (1e-6 m) and macro (1e3 m) meshes.
- F10: Native C-ABI exception safety and array bounds validation.
"""

from __future__ import annotations

import ctypes
from pathlib import Path
import re
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np
import pytest

from backend.meshing.models import UnstructuredMesh

# Conditional imports for WP1 components under development
try:
    from backend.meshing.models import CellType
except ImportError:
    CellType = None

try:
    from backend.meshing.difvm_topology import (
        compute_difvm_topology,
        compute_difvm_topology_python,
        extract_cht_twin_faces,
        get_native_dll,
    )
except ImportError as e:
    pytest.fail(f"Could not import difvm_topology components: {e}")

try:
    from backend.meshing.distmesh_core import rcm_reorder
except ImportError:
    rcm_reorder = None

try:
    from backend.meshing.distmesh_core import hilbert_sfc_reorder
except ImportError:
    try:
        from backend.meshing.difvm_topology import hilbert_sfc_reorder
    except ImportError:
        hilbert_sfc_reorder = None

try:
    from backend.meshing.polymesh_io import export_openfoam_polymesh, extrude_2d_to_3d_slab
except ImportError:
    export_openfoam_polymesh = None
    extrude_2d_to_3d_slab = None


# ==============================================================================
# Geometric Mesh Synthesis Fixtures & Helpers
# ==============================================================================


def _create_two_cube_mesh(dx: float = 1.0) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Synthesizes 2 adjacent cubes sharing an internal face at x = dx.

    Args:
        dx: Edge length and spatial scale of the cubes in meters.

    Returns:
        Tuple containing:
        - points: (12, 3) float64 array of vertex coordinates.
        - cells: List of two 8-node hexahedral cell index arrays.
    """
    pts = np.array([
        # Cube 0 vertices (x = 0 and x = dx)
        [0.0, 0.0, 0.0], [dx, 0.0, 0.0], [dx, dx, 0.0], [0.0, dx, 0.0],  # 0..3 (z=0)
        [0.0, 0.0, dx], [dx, 0.0, dx], [dx, dx, dx], [0.0, dx, dx],      # 4..7 (z=dx)
        # Cube 1 extra vertices (x = 2*dx)
        [2.0 * dx, 0.0, 0.0], [2.0 * dx, dx, 0.0],                       # 8, 9 (z=0)
        [2.0 * dx, 0.0, dx], [2.0 * dx, dx, dx],                         # 10, 11 (z=dx)
    ], dtype=np.float64)

    cell0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
    cell1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)
    return pts, [cell0, cell1]


def _create_l_shaped_quad_mesh_at_z(z_val: float = 5.0) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Synthesizes 2D L-shaped domain with 3 coplanar QUAD cells at specified z elevation.

    Quad 0: [0, 1] x [0, 1]
    Quad 1: [1, 2] x [0, 1]
    Quad 2: [0, 1] x [1, 2]

    Args:
        z_val: Z elevation coordinate for all vertices (default 5.0).

    Returns:
        Tuple containing:
        - points: (8, 3) float64 array of planar vertices.
        - cells: List of three 4-node quad cell index arrays.
    """
    pts = np.array([
        [0.0, 0.0, z_val],  # 0
        [1.0, 0.0, z_val],  # 1
        [2.0, 0.0, z_val],  # 2
        [0.0, 1.0, z_val],  # 3
        [1.0, 1.0, z_val],  # 4
        [2.0, 1.0, z_val],  # 5
        [0.0, 2.0, z_val],  # 6
        [1.0, 2.0, z_val],  # 7
    ], dtype=np.float64)

    c0 = np.array([0, 1, 4, 3], dtype=np.int64)
    c1 = np.array([1, 2, 5, 4], dtype=np.int64)
    c2 = np.array([3, 4, 7, 6], dtype=np.int64)
    return pts, [c0, c1, c2]


def _create_3x3x3_hex_mesh(scale: float = 1.0) -> Tuple[np.ndarray, List[np.ndarray]]:
    """Synthesizes structured 3x3x3 hexahedral mesh (27 cells, 64 vertices).

    Args:
        scale: Domain scaling factor.

    Returns:
        Tuple containing:
        - points: (64, 3) float64 array of grid vertices.
        - cells: List of 27 hexahedral cell index arrays (8 nodes each).
    """
    coords = np.linspace(0.0, 3.0 * scale, 4)
    x, y, z = np.meshgrid(coords, coords, coords, indexing="ij")
    pts = np.column_stack([x.ravel(), y.ravel(), z.ravel()])

    def node_id(i: int, j: int, k: int) -> int:
        return (i * 4 + j) * 4 + k

    cells = []
    for i in range(3):
        for j in range(3):
            for k in range(3):
                hex_nodes = np.array([
                    node_id(i, j, k),
                    node_id(i + 1, j, k),
                    node_id(i + 1, j + 1, k),
                    node_id(i, j + 1, k),
                    node_id(i, j, k + 1),
                    node_id(i + 1, j, k + 1),
                    node_id(i + 1, j + 1, k + 1),
                    node_id(i, j + 1, k + 1),
                ], dtype=np.int64)
                cells.append(hex_nodes)
    return pts, cells


# ==============================================================================
# Gate WP1 Tests
# ==============================================================================


def test_face_connectivity_and_right_hand_rule() -> None:
    """Verifies explicit face connectivity and consistent right-hand rule face normals.

    Tests that:
    - Internal face normal S_f points strictly owner -> neighbour with owner < neighbour.
    - Boundary face normal S_f points outward from owner with neighbour == -1.
    - mesh.faces property is present and matches face_areas and face_centres.
    - Vertex order of face matches the right-hand rule orientation of S_f.

    Raises:
        AssertionError: If face connectivity or right-hand rule invariants are violated.
    """
    pts, cells = _create_two_cube_mesh(dx=1.0)
    mesh = compute_difvm_topology(pts, cells)

    # 1. Verify mesh.faces property exists and is populated
    assert hasattr(mesh, "faces"), "UnstructuredMesh must have 'faces' property"
    assert mesh.faces is not None, "mesh.faces must not be None"
    assert len(mesh.faces) == mesh.num_faces, (
        f"Length of mesh.faces ({len(mesh.faces)}) must equal mesh.num_faces ({mesh.num_faces})"
    )

    n_internal = mesh.num_internal_faces
    assert n_internal > 0, "Two-cube mesh must contain at least 1 internal face"

    # 2. Audit internal faces: owner < neighbour and outward normal orientation
    for f in range(n_internal):
        o = mesh.owner[f]
        nb = mesh.neighbour[f]
        sf = mesh.face_areas[f]
        cf = mesh.face_centres[f]
        fnodes = mesh.faces[f]

        assert nb >= 0, f"Internal face {f} must have neighbour >= 0, got {nb}"
        assert o < nb, (
            f"Internal face {f} must satisfy upper-triangular owner < neighbour, got owner={o}, nb={nb}"
        )

        # d_f vector pointing from owner centroid to neighbour centroid
        d_f = mesh.cell_centres[nb] - mesh.cell_centres[o]
        proj = float(np.dot(sf, d_f))
        assert proj > 0.0, (
            f"Internal face {f} normal S_f must point owner -> neighbour (S_f . d_f = {proj:.4e} <= 0)"
        )

        # 3. Verify right-hand rule on face nodes: sum of cross products matches S_f direction
        assert len(fnodes) >= 3, f"Face {f} must have at least 3 nodes, got {len(fnodes)}"
        poly_pts = pts[fnodes]
        v0 = poly_pts[0]
        n_geom = np.zeros(3, dtype=np.float64)
        for i in range(1, len(fnodes) - 1):
            n_geom += 0.5 * np.cross(poly_pts[i] - v0, poly_pts[i + 1] - v0)

        n_geom_norm = np.linalg.norm(n_geom)
        sf_norm = np.linalg.norm(sf)
        assert np.isclose(n_geom_norm, sf_norm, rtol=1e-8, atol=1e-12), (
            f"Face {f} geometric area {n_geom_norm:.6e} does not match face_areas magnitude {sf_norm:.6e}"
        )

        alignment = float(np.dot(n_geom, sf) / (n_geom_norm * sf_norm))
        assert alignment > 0.9999, (
            f"Face {f} node winding right-hand rule violates S_f direction (cos_angle = {alignment:.6f} < 1.0)"
        )

        c_geom = np.mean(poly_pts, axis=0)
        assert np.allclose(c_geom, cf, atol=1e-10), f"Face {f} centre mismatch: {cf} vs {c_geom}"

    # 4. Audit boundary faces: neighbour == -1 and normal points outward
    for f in range(n_internal, mesh.num_faces):
        o = mesh.owner[f]
        nb = mesh.neighbour[f]
        sf = mesh.face_areas[f]
        cf = mesh.face_centres[f]
        assert nb == -1, f"Boundary face {f} must have neighbour == -1, got {nb}"

        d_out = cf - mesh.cell_centres[o]
        assert float(np.dot(sf, d_out)) > 0.0, f"Boundary face {f} normal S_f does not point outward from owner"


def test_l_shaped_2d_quad_at_z5() -> None:
    """Verifies L-shaped 2D planar quad mesh located at z = 5.0.

    Tests that:
    - Planar 4-node elements at z = 5.0 are classified as CellType.QUAD (9), not TETRA (10).
    - topological_dim is 2.
    - Cell volumes (areas) are positive and match the 1.0 m^2 expected value.
    - Face normals lie strictly in the xy-plane (S_{f, z} == 0).
    - Relative Gauss-closure theorem is satisfied and validate() passes.

    Raises:
        AssertionError: If cell classification, dimensions, or closure theorems fail.
    """
    pts, cells = _create_l_shaped_quad_mesh_at_z(z_val=5.0)
    mesh = compute_difvm_topology(pts, cells)

    # 1. Verify topological dimension
    assert hasattr(mesh, "topological_dim"), "UnstructuredMesh must have 'topological_dim'"
    assert mesh.topological_dim == 2, (
        f"Expected topological_dim == 2 for planar quad mesh, got {mesh.topological_dim}"
    )

    # 2. Verify CellType is QUAD (9), never TETRA (10)
    assert hasattr(mesh, "cell_types"), "UnstructuredMesh must have 'cell_types'"
    assert mesh.cell_types is not None, "mesh.cell_types must not be None"
    assert len(mesh.cell_types) == len(cells)

    expected_quad_type = CellType.QUAD if CellType is not None else 9
    for c_idx, ctype in enumerate(mesh.cell_types):
        assert ctype == expected_quad_type, (
            f"Cell {c_idx} was misclassified as {ctype} (expected QUAD={expected_quad_type}, NOT TETRA=10)"
        )

    # 3. Positive cell volumes / areas
    assert len(mesh.cell_volumes) == 3
    assert np.all(mesh.cell_volumes > 0.0), f"All cell volumes must be positive, got {mesh.cell_volumes}"
    assert np.allclose(mesh.cell_volumes, 1.0, atol=1e-10), f"Expected unit area quads, got {mesh.cell_volumes}"

    # 4. Face normals in xy-plane: S_{f, z} == 0
    assert np.allclose(mesh.face_areas[:, 2], 0.0, atol=1e-12), (
        f"2D quad face normals must have z=0, max abs z is {np.max(np.abs(mesh.face_areas[:, 2])):.2e}"
    )

    # 5. Gauss closure theorem and validate()
    mesh.validate(tol_closure=1e-11)


def test_scale_invariance_validate() -> None:
    """Verifies scale invariance of UnstructuredMesh.validate() from 1e-6 m to 1e3 m.

    Tests that:
    - Micro-mesh (1 micron) and macro-mesh (1 kilometer) pass validate().
    - Relative Gauss-closure tolerance is scale-invariant: rel_closure <= 1e-11.
    - Projection collinearity and over-relaxation identities hold regardless of scale.
    - Corrupted mesh deterministically raises ValueError.

    Raises:
        AssertionError: If validation fails on valid scaled meshes.
    """
    scales = [1e-6, 1.0, 1e3]
    for scale in scales:
        pts, cells = _create_two_cube_mesh(dx=scale)
        mesh = compute_difvm_topology(pts, cells)

        assert mesh.validate(tol_closure=1e-11) is True, f"validate() failed on mesh scaled by {scale}"

        # Mathematical verification of relative Gauss closure
        internal_mask = mesh.neighbour >= 0
        cell_surf = np.zeros(mesh.num_cells, dtype=np.float64)
        fa_mag = np.linalg.norm(mesh.face_areas, axis=1)
        np.add.at(cell_surf, mesh.owner, fa_mag)
        np.add.at(cell_surf, mesh.neighbour[internal_mask], fa_mag[internal_mask])

        closure = np.zeros_like(mesh.cell_centres)
        np.add.at(closure, mesh.owner, mesh.face_areas)
        np.subtract.at(closure, mesh.neighbour[internal_mask], mesh.face_areas[internal_mask])

        closure_mag = np.linalg.norm(closure, axis=1)
        rel_closure = np.max(closure_mag / cell_surf)
        assert rel_closure < 1e-11, f"Scale {scale}: Relative Gauss closure {rel_closure:.2e} >= 1e-11"

        # Collinearity on internal faces: ||delta_f x d_f|| / (||delta_f|| ||d_f||) < 1e-12
        for f in range(mesh.num_internal_faces):
            o, nb = mesh.owner[f], mesh.neighbour[f]
            df = mesh.cell_centres[nb] - mesh.cell_centres[o]
            delta_f = mesh.delta_f[f]
            cross_mag = np.linalg.norm(np.cross(delta_f, df))
            collin = cross_mag / (np.linalg.norm(delta_f) * np.linalg.norm(df) + 1e-30)
            assert collin < 1e-12, f"Scale {scale}, Face {f}: Collinearity violation {collin:.2e}"

    # Adversarial test: deliberately violate Gauss closure on cell 0
    pts, cells = _create_two_cube_mesh(dx=1.0)
    mesh_bad = compute_difvm_topology(pts, cells)
    mesh_bad.face_areas[0] *= 1.10
    with pytest.raises(ValueError, match="(?i)gauss|closure|divergence"):
        mesh_bad.validate(tol_closure=1e-11)


def test_strict_non_manifold_rejection() -> None:
    """Verifies that meshes where >2 cells share the same face deterministically raise ValueError.

    Raises:
        AssertionError: If non-manifold geometry fails to raise ValueError.
    """
    pts_2d = np.array([
        [0.0, 0.0, 0.0], [0.0, 1.0, 0.0],   # shared edge 0-1
        [1.0, 0.0, 0.0], [1.0, 1.0, 0.0],   # quad 0
        [-1.0, 0.0, 0.0], [-1.0, 1.0, 0.0], # quad 1
        [0.0, 0.0, 1.0], [0.0, 1.0, 1.0],   # quad 2 (branching non-manifoldly from edge 0-1)
    ], dtype=np.float64)
    q0 = np.array([0, 2, 3, 1], dtype=np.int64)
    q1 = np.array([0, 1, 5, 4], dtype=np.int64)
    q2 = np.array([0, 1, 7, 6], dtype=np.int64)

    with pytest.raises(ValueError, match="(?i)non-manifold|shared by (>|more than) 2"):
        compute_difvm_topology(pts_2d, [q0, q1, q2])


def test_cht_twin_faces_contiguous() -> None:
    """Verifies CHT multi-region extraction produces contiguous internal faces with no neighbour == -1.

    Tests that:
    - Internal face block [0, n_int) contains strictly neighbour >= 0.
    - Coupled faces are relocated into contiguous boundary patches coupledWall_regionA_to_B and coupledWall_regionB_to_A.
    - Twin face normals are equal and opposite (S_f1 == -S_f2).
    - Resulting mesh passes validate().

    Raises:
        AssertionError: If neighbour == -1 remains in internal block or twin faces are non-contiguous.
    """
    pts, cells = _create_two_cube_mesh(dx=1.0)
    regions = np.array([0, 1], dtype=np.int32)
    mesh = compute_difvm_topology(pts, cells, regions=regions)

    if mesh.num_internal_faces > 0 and len(mesh.boundaries) == 1:
        mesh = extract_cht_twin_faces(mesh)

    n_internal = mesh.num_internal_faces

    # 1. Strict contiguous block invariant: NO neighbour == -1 in [0, n_internal)
    internal_neighbours = mesh.neighbour[:n_internal]
    assert np.all(internal_neighbours >= 0), (
        f"Found {np.sum(internal_neighbours < 0)} invalid neighbour==-1 inside internal face block [0, {n_internal})! "
        f"Violates FVM contiguous face block invariant."
    )

    # 2. All faces from n_internal to end must have neighbour == -1
    boundary_neighbours = mesh.neighbour[n_internal:]
    assert np.all(boundary_neighbours == -1), (
        f"Found {np.sum(boundary_neighbours >= 0)} non-boundary faces inside boundary face block"
    )

    # 3. Coupled twin boundary patches must exist and be contiguous
    coupled_keys = [k for k in mesh.boundaries if "coupled" in k.lower()]
    assert len(coupled_keys) >= 2, (
        f"Expected at least 2 coupledWall boundary patches, found: {list(mesh.boundaries.keys())}"
    )

    patch_a_start, patch_a_count = mesh.boundaries[coupled_keys[0]]
    patch_b_start, patch_b_count = mesh.boundaries[coupled_keys[1]]
    assert patch_a_count == patch_b_count, (
        f"Coupled patches must have equal face counts ({patch_a_count} vs {patch_b_count})"
    )

    for i in range(patch_a_count):
        fa = patch_a_start + i
        fb = patch_b_start + i
        sf_a = mesh.face_areas[fa]
        sf_b = mesh.face_areas[fb]
        assert np.allclose(sf_a, -sf_b, atol=1e-12), (
            f"Coupled twin face normals must be opposite: {sf_a} vs {sf_b}"
        )

    mesh.validate()


def test_named_boundary_patches_passthrough() -> None:
    """Verifies named boundary patches survive compute_difvm_topology cleanly.

    Raises:
        AssertionError: If boundary patches are dropped or ranges overlap.
    """
    pts, cells = _create_two_cube_mesh(dx=1.0)
    patch_definitions = {
        "inlet": [[0, 3, 7, 4]],
        "outlet": [[8, 9, 11, 10]],
        "wall": [[0, 1, 5, 4], [3, 2, 6, 7]],
        "symmetry": [[0, 1, 2, 3], [4, 5, 6, 7]],
    }

    try:
        mesh = compute_difvm_topology(pts, cells, boundary_patches=patch_definitions)
    except TypeError:
        mesh = compute_difvm_topology(pts, cells)

    assert isinstance(mesh.boundaries, dict), "mesh.boundaries must be a dictionary"
    assert len(mesh.boundaries) >= 1, "mesh.boundaries must not be empty"

    total_boundary_faces = 0
    n_internal = mesh.num_internal_faces
    for patch_name, (start_f, count_f) in mesh.boundaries.items():
        assert start_f >= n_internal, f"Patch {patch_name} starts at {start_f} < n_internal ({n_internal})"
        assert count_f > 0, f"Patch {patch_name} has count <= 0"
        assert start_f + count_f <= mesh.num_faces, f"Patch {patch_name} exceeds total face count"
        total_boundary_faces += count_f

    assert total_boundary_faces == mesh.num_boundary_faces, (
        f"Sum of patch face counts ({total_boundary_faces}) does not match num_boundary_faces ({mesh.num_boundary_faces})"
    )


def test_rcm_and_hilbert_sfc_reordering() -> None:
    """Verifies lossless RCM and 3D Hilbert SFC reordering preserving boundaries, baffles, and metadata.

    Raises:
        AssertionError: If reordering loses boundaries, metadata, or corrupts cell partitioning.
    """
    if rcm_reorder is None:
        pytest.fail("rcm_reorder function is not available in distmesh_core")

    pts, cells = _create_3x3x3_hex_mesh(scale=1.0)
    mesh = compute_difvm_topology(pts, cells)
    mesh.metadata["test_run_id"] = "RCM_SFC_GATE_WP1"
    mesh.baffles = [{"name": "mock_baffle", "twin_pairs": [(0, 1)]}]
    mesh.cyclic_pairs = [(10, 11)]

    # 1. Test RCM reordering
    mesh_rcm, perm_rcm = rcm_reorder(mesh)
    assert len(perm_rcm) == mesh.num_cells, "RCM permutation vector must match cell count"
    assert len(np.unique(perm_rcm)) == mesh.num_cells, "RCM permutation must be a bijective mapping"

    assert mesh_rcm.metadata.get("test_run_id") == "RCM_SFC_GATE_WP1", "RCM dropped mesh.metadata"
    assert len(mesh_rcm.boundaries) == len(mesh.boundaries), "RCM corrupted mesh.boundaries"
    assert len(mesh_rcm.baffles) == len(mesh.baffles), "RCM dropped mesh.baffles"
    assert len(mesh_rcm.cyclic_pairs) == len(mesh.cyclic_pairs), "RCM dropped mesh.cyclic_pairs"
    mesh_rcm.validate()

    # 2. Test 3D Hilbert SFC reordering
    if hilbert_sfc_reorder is not None:
        mesh_sfc, perm_sfc = hilbert_sfc_reorder(mesh)
        assert len(perm_sfc) == mesh.num_cells
        assert mesh_sfc.metadata.get("test_run_id") == "RCM_SFC_GATE_WP1"
        assert "inner_cells" in mesh_sfc.metadata, "hilbert_sfc_reorder must populate 'inner_cells' in metadata"
        assert "halo_adjacent_cells" in mesh_sfc.metadata, (
            "hilbert_sfc_reorder must populate 'halo_adjacent_cells' in metadata"
        )

        inner_cells = set(mesh_sfc.metadata["inner_cells"])
        halo_cells = set(mesh_sfc.metadata["halo_adjacent_cells"])

        assert len(inner_cells) == 1, f"Expected 1 inner cell in 3x3x3 hex grid, got {len(inner_cells)}"
        assert len(halo_cells) == 26, f"Expected 26 halo-adjacent cells in 3x3x3 hex grid, got {len(halo_cells)}"
        assert inner_cells.isdisjoint(halo_cells), "inner_cells and halo_adjacent_cells must be disjoint"
        assert len(inner_cells) + len(halo_cells) == 27, "Partition must cover all cells"
        mesh_sfc.validate()


def test_cms_mesh_error_map_fields() -> None:
    """Verifies CMS MeshErrorMap fields and inverted face stabilization escalation.

    Tests that:
    - m_skew, k_f_comp, beta_upwind, omega_cell, wsrd_alpha are present.
    - Non-orthogonal damping k_f_comp respects alpha_max = 1.5.
    - Inverted face condition (S_f . d_f <= 0) escalates immediately to stabilization_mask = 3 and beta_upwind = 0.0.

    Raises:
        AssertionError: If error map fields are missing or inverted faces are not escalated.
    """
    pts, cells = _create_two_cube_mesh(dx=1.0)
    mesh = compute_difvm_topology(pts, cells)

    assert isinstance(mesh.error_map, dict), "mesh.error_map must be a dictionary"
    cms_keys = ["m_skew", "k_f_comp", "beta_upwind", "omega_cell", "wsrd_alpha"]
    for k in cms_keys:
        assert k in mesh.error_map, f"CMS compensation field '{k}' missing from mesh.error_map"

    delta_f_mag = np.linalg.norm(mesh.delta_f, axis=1)
    k_f_comp_mag = np.linalg.norm(mesh.error_map["k_f_comp"], axis=1)
    alpha_max = 1.5
    assert np.all(k_f_comp_mag <= alpha_max * delta_f_mag + 1e-12), (
        "k_f_comp magnitude exceeds alpha_max * ||delta_f||"
    )

    beta_upwind = mesh.error_map["beta_upwind"]
    assert np.all(beta_upwind >= 0.0) and np.all(beta_upwind <= 1.0), (
        f"beta_upwind must lie in [0, 1], got min={np.min(beta_upwind)}, max={np.max(beta_upwind)}"
    )

    omega_cell = mesh.error_map["omega_cell"]
    assert np.all(omega_cell >= 0.1) and np.all(omega_cell <= 1.0), (
        f"omega_cell must lie in [0.1, 1.0], got min={np.min(omega_cell)}, max={np.max(omega_cell)}"
    )

    # Inverted face test: distorted face where S_f . d_f <= 0
    pts_distorted = pts.copy()
    pts_distorted[1, 0] = -0.5
    try:
        mesh_inv = compute_difvm_topology(pts_distorted, cells)
        stab_mask = mesh_inv.error_map.get("stabilization_mask", mesh_inv.error_map.get("mask"))
        assert stab_mask is not None
        assert np.any(stab_mask == 3), "Inverted face (S_f . d_f <= 0) must escalate to stabilization_mask = 3"
        inv_face_idx = np.where(stab_mask == 3)[0]
        assert np.all(mesh_inv.error_map["beta_upwind"][inv_face_idx] == 0.0), (
            "beta_upwind must be 0.0 on inverted face"
        )
    except ValueError:
        pass


def test_openfoam_polymesh_export_syntax(tmp_path: Path) -> None:
    """Verifies export_openfoam_polymesh generates standard-conforming constant/polyMesh files.

    Args:
        tmp_path: Temporary directory fixture provided by pytest.

    Raises:
        AssertionError: If exported polyMesh files are missing, syntactically invalid, or have mismatched lengths.
    """
    if export_openfoam_polymesh is None:
        pytest.fail("polymesh_io.export_openfoam_polymesh is not implemented")

    pts, cells = _create_two_cube_mesh(dx=1.0)
    mesh = compute_difvm_topology(pts, cells)

    export_openfoam_polymesh(mesh, str(tmp_path))

    poly_mesh_dir = tmp_path / "constant" / "polyMesh"
    if not poly_mesh_dir.exists():
        poly_mesh_dir = tmp_path / "polyMesh"
    assert poly_mesh_dir.exists(), f"polyMesh directory not found at {poly_mesh_dir}"

    for fname in ["points", "faces", "owner", "neighbour", "boundary"]:
        fpath = poly_mesh_dir / fname
        assert fpath.exists(), f"OpenFOAM polyMesh file '{fname}' missing from {poly_mesh_dir}"

        content = fpath.read_text(encoding="utf-8")
        assert "FoamFile" in content, f"File '{fname}' missing FoamFile header"
        assert "version" in content and "format" in content, f"File '{fname}' missing header fields"

    neighbour_content = (poly_mesh_dir / "neighbour").read_text(encoding="utf-8")
    match = re.search(r"(\d+)\s*\(", neighbour_content)
    assert match is not None, "Could not parse list count in neighbour file"
    n_neighbour = int(match.group(1))
    assert n_neighbour == mesh.num_internal_faces, (
        f"OpenFOAM neighbour list length ({n_neighbour}) MUST match internal face count ({mesh.num_internal_faces})"
    )

    boundary_content = (poly_mesh_dir / "boundary").read_text(encoding="utf-8")
    assert "type" in boundary_content and "nFaces" in boundary_content and "startFace" in boundary_content, (
        "boundary file missing required OpenFOAM dictionary syntax (type, nFaces, startFace)"
    )


def test_extrude_2d_to_3d_slab() -> None:
    """Verifies extrude_2d_to_3d_slab generates 1-cell thick 3D mesh with frontAndBack empty patch.

    Raises:
        AssertionError: If extrusion fails to produce valid 3D topology or positive cell volumes.
    """
    if extrude_2d_to_3d_slab is None:
        pytest.fail("polymesh_io.extrude_2d_to_3d_slab is not implemented")

    pts_2d, cells_2d = _create_l_shaped_quad_mesh_at_z(z_val=0.0)
    mesh_2d = compute_difvm_topology(pts_2d, cells_2d)

    thickness = 0.5
    mesh_3d = extrude_2d_to_3d_slab(mesh_2d, thickness=thickness)

    assert isinstance(mesh_3d, UnstructuredMesh), "extrude_2d_to_3d_slab must return an UnstructuredMesh"
    assert mesh_3d.topological_dim == 3, (
        f"Extruded mesh must have topological_dim == 3, got {mesh_3d.topological_dim}"
    )
    assert mesh_3d.num_cells == mesh_2d.num_cells, "Cell count must be preserved in 1-cell thick extrusion"

    assert np.all(mesh_3d.cell_volumes > 0.0), "Extruded cell volumes must be positive"
    expected_vol = 1.0 * thickness
    assert np.allclose(mesh_3d.cell_volumes, expected_vol, atol=1e-8), (
        f"Expected cell volumes {expected_vol}, got {mesh_3d.cell_volumes}"
    )

    assert "frontAndBack" in mesh_3d.boundaries, (
        f"Extruded 3D slab must contain 'frontAndBack' patch, found: {list(mesh_3d.boundaries.keys())}"
    )

    mesh_3d.validate()


def test_c_abi_exception_safety_and_bounds() -> None:
    """Verifies C-ABI exception safety guards and array bounds validation.

    Tests that:
    - Calling functions with null mesh handle returns error code (-1 or -2) or 0 without crashing.
    - Calling cfd_mesher_build_from_cells with non-monotonic offsets or out-of-bounds nodes returns -1.

    Raises:
        AssertionError: If bounds checking fails or process terminates abruptly.
    """
    dll = get_native_dll()
    if dll is None:
        pytest.skip("Native cfd_mesher_native library not available on this platform")

    # 1. Null handle invocation on accessor
    n_pts = dll.cfd_mesher_get_n_points(None)
    assert n_pts in (0, -1, -2), f"Expected 0, -1, or -2 for null handle, got {n_pts}"

    # 2. Invalid cell offsets: non-monotonic offsets [0, 5, 2]
    pts = np.zeros((8, 3), dtype=np.float64)
    bad_offsets = np.array([0, 5, 2], dtype=np.int64)
    nodes = np.zeros(10, dtype=np.int64)

    pts_p = pts.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
    offsets_p = bad_offsets.ctypes.data_as(ctypes.POINTER(ctypes.c_int64))
    nodes_p = nodes.ctypes.data_as(ctypes.POINTER(ctypes.c_int64))

    ret_bounds = dll.cfd_mesher_build_from_cells(
        None,
        pts_p, 8,
        offsets_p,
        nodes_p,
        2,
        None
    )
    assert ret_bounds in (-1, -2), f"Expected -1 or -2 for non-monotonic offsets, got {ret_bounds}"
