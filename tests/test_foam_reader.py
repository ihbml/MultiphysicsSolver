"""test_foam_reader.py
===================
Unit & Verification test suite for FoamReader (polyMesh & fields).
Tests:
1. Synthetic single-cell hexahedral mesh.
2. Real OpenFOAM verification tutorial mesh: skewnessCavity (2500 cells, 8812 faces).
3. Real OpenFOAM aerodynamic mesh: naca0012.
4. Field parser tests (uniform & nonuniform volScalarField and volVectorField).
5. Geometric & physical invariants (positive cell volumes, bounding box, Gauss divergence sum = 0).
"""

import gzip
from pathlib import Path
import numpy as np
import pytest

from backend.native_rust.foam_reader import FoamReader, FoamMesh, FoamBoundaryPatch


def test_synthetic_unit_cube(tmp_path):
    """Verifies geometric calculations on a synthetic 1x1x1 hexahedron."""
    poly_dir = tmp_path / "constant" / "polyMesh"
    poly_dir.mkdir(parents=True)

    # 8 points of unit cube
    points_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class vectorField; location "constant/polyMesh"; object points; }
8
(
(0 0 0)
(1 0 0)
(1 1 0)
(0 1 0)
(0 0 1)
(1 0 1)
(1 1 1)
(0 1 1)
)
"""
    # 6 quad faces
    faces_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class faceList; location "constant/polyMesh"; object faces; }
6
(
4(0 4 7 3) // x = 0 (left)
4(1 2 6 5) // x = 1 (right)
4(0 1 5 4) // y = 0 (bottom)
4(3 7 6 2) // y = 1 (top)
4(0 3 2 1) // z = 0 (back)
4(4 5 6 7) // z = 1 (front)
)
"""
    owner_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class labelList; location "constant/polyMesh"; object owner; }
6
(
0
0
0
0
0
0
)
"""
    neighbour_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class labelList; location "constant/polyMesh"; object neighbour; }
0
(
)
"""
    boundary_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class polyBoundaryMesh; location "constant/polyMesh"; object boundary; }
1
(
    walls
    {
        type patch;
        nFaces 6;
        startFace 0;
    }
)
"""
    (poly_dir / "points").write_text(points_content, encoding="utf-8")
    (poly_dir / "faces").write_text(faces_content, encoding="utf-8")
    (poly_dir / "owner").write_text(owner_content, encoding="utf-8")
    (poly_dir / "neighbour").write_text(neighbour_content, encoding="utf-8")
    (poly_dir / "boundary").write_text(boundary_content, encoding="utf-8")

    mesh = FoamReader.load_mesh(poly_dir)

    assert mesh.n_cells == 1
    assert mesh.n_points == 8
    assert mesh.n_faces == 6
    assert mesh.n_internal_faces == 0
    assert mesh.n_boundary_faces == 6

    # Invariants
    assert np.isclose(mesh.total_volume(), 1.0, atol=1e-12)
    assert np.allclose(mesh.cell_centres[0], [0.5, 0.5, 0.5], atol=1e-12)

    bbox_min, bbox_max = mesh.bounding_box()
    assert np.allclose(bbox_min, [0, 0, 0])
    assert np.allclose(bbox_max, [1, 1, 1])

    # Closed cell Gauss theorem: sum of outward face normal areas must be zero
    sum_sf = np.sum(mesh.face_areas, axis=0)
    assert np.allclose(sum_sf, [0, 0, 0], atol=1e-12)


def test_real_openfoam_skewness_cavity():
    """Loads and verifies the real OpenFOAM V&V skewnessCavity polyMesh (compressed .gz)."""
    poly_dir = Path("OpenFOAM_base/openfoam/tutorials/verificationAndValidation/schemes/skewnessCavity/setups.orig/common/constant/polyMesh")
    if not poly_dir.exists():
        pytest.skip("OpenFOAM_base skewnessCavity polyMesh directory not available")

    mesh = FoamReader.load_mesh(poly_dir)

    # Values from OpenFOAM header note: nPoints:2626 nCells:2500 nFaces:8812 nInternalFaces:3688
    assert mesh.n_points == 2626
    assert mesh.n_cells == 2500
    assert mesh.n_faces == 8812
    assert mesh.n_internal_faces == 3688
    assert mesh.n_boundary_faces == 8812 - 3688

    # All cell volumes must be positive
    assert np.all(mesh.cell_volumes > 0.0)

    # Total cavity volume should be close to 1x1x0.1 = 0.1 m3
    total_vol = mesh.total_volume()
    assert 0.05 < total_vol < 0.2

    # Verify boundary patches
    patch_names = [p.name for p in mesh.boundaries]
    assert "auto2" in patch_names
    assert "auto3" in patch_names
    assert "auto4" in patch_names
    assert "auto5" in patch_names
    assert "frontAndBack" in patch_names

    # Invariant: Delta_f + k_f == S_f on all internal faces
    n_internal = mesh.n_internal_faces
    assert np.allclose(mesh.delta_f[:n_internal] + mesh.k_f[:n_internal], mesh.face_areas[:n_internal], atol=1e-12)

    # Invariant: Gauss divergence theorem on closed interior cells
    # sum of outward face normal areas for any interior cell must be 0
    cell_flux_balance = np.zeros((mesh.n_cells, 3), dtype=np.float64)
    for f in range(mesh.n_faces):
        o = mesh.owner[f]
        cell_flux_balance[o] += mesh.face_areas[f]
        if f < n_internal:
            n = mesh.neighbour[f]
            cell_flux_balance[n] -= mesh.face_areas[f]

    # For all 2500 cells in the cavity, sum of outward face areas must sum to zero
    assert np.allclose(cell_flux_balance, 0.0, atol=1e-11)


def test_real_openfoam_naca0012():
    """Loads and verifies real OpenFOAM NACA0012 mesh from adjointOptimisationFoam."""
    poly_dir = Path("OpenFOAM_base/openfoam/tutorials/incompressible/adjointOptimisationFoam/resources/meshes/naca0012/polyMesh")
    if not poly_dir.exists():
        pytest.skip("OpenFOAM_base naca0012 polyMesh directory not available")

    mesh = FoamReader.load_mesh(poly_dir)

    assert mesh.n_points > 1000
    assert mesh.n_cells > 1000
    assert mesh.n_faces > 2000
    assert mesh.n_internal_faces > 0

    assert np.all(mesh.cell_volumes > 0.0)
    assert mesh.total_volume() > 0.0


def test_field_parsing_uniform(tmp_path):
    """Tests parsing uniform volScalarField and volVectorField."""
    p_file = tmp_path / "p"
    p_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class volScalarField; location "0"; object p; }
dimensions [0 2 -2 0 0 0 0];
internalField uniform 101325;
boundaryField { walls { type zeroGradient; } }
"""
    p_file.write_text(p_content, encoding="utf-8")

    field = FoamReader.load_field(p_file, n_cells=10)
    assert field["name"] == "p"
    assert field["dimensions"] == [0, 2, -2, 0, 0, 0, 0]
    assert not field["is_vector"]
    assert len(field["internal_field"]) == 10
    assert np.allclose(field["internal_field"], 101325)

    u_file = tmp_path / "U"
    u_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class volVectorField; location "0"; object U; }
dimensions [0 1 -1 0 0 0 0];
internalField uniform (15.5 0 2.5);
boundaryField { inlet { type fixedValue; value uniform (15.5 0 2.5); } }
"""
    u_file.write_text(u_content, encoding="utf-8")

    field_u = FoamReader.load_field(u_file, n_cells=5)
    assert field_u["name"] == "U"
    assert field_u["is_vector"]
    assert field_u["internal_field"].shape == (5, 3)
    assert np.allclose(field_u["internal_field"][0], [15.5, 0, 2.5])


def test_field_parsing_nonuniform(tmp_path):
    """Tests parsing nonuniform scalar field."""
    t_file = tmp_path / "T"
    t_content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile { version 2.0; format ascii; class volScalarField; location "0"; object T; }
dimensions [0 0 0 1 0 0 0];
internalField nonuniform List<scalar> 3 ( 300.0 350.5 400.0 );
boundaryField {}
"""
    t_file.write_text(t_content, encoding="utf-8")

    field_t = FoamReader.load_field(t_file)
    assert field_t["name"] == "T"
    assert not field_t["is_vector"]
    assert len(field_t["internal_field"]) == 3
    assert np.allclose(field_t["internal_field"], [300.0, 350.5, 400.0])
