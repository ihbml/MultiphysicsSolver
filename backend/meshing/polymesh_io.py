"""OpenFOAM polyMesh I/O and 2D-to-3D Slab Extrusion Engine.

Implements standard OpenFOAM constant/polyMesh directory export:
- points: 3D coordinates in FoamFile vectorField format.
- faces: Face-to-node connectivity in FoamFile faceList format.
- owner: Cell owner index for all faces in FoamFile labelList format.
- neighbour: Cell neighbour index for internal faces ONLY in FoamFile labelList format.
- boundary: polyBoundaryMesh dictionary defining contiguous boundary patches with valid types:
  wall, patch, symmetry, empty, mappedWall, cyclic.

Also implements extrude_2d_to_3d_slab to convert 2D planar meshes into
standard 1-cell thick 3D slabs with frontAndBack empty boundary patches.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.meshing.models import CellType, UnstructuredMesh


def _foam_file_header(class_name: str, object_name: str) -> str:
    """Generate standard OpenFOAM ASCII FoamFile header banner.

    Args:
        class_name: OpenFOAM class name (e.g. vectorField, faceList, labelList, polyBoundaryMesh).
        object_name: OpenFOAM dictionary/object name (e.g. points, faces, owner, neighbour, boundary).

    Returns:
        Formatted FoamFile header block string.
    """
    return (
        "/*--------------------------------*- C++ -*----------------------------------*\\\n"
        "| =========                 |                                                 |\n"
        "| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |\n"
        "|  \\\\    /   O peration     | Version:  2406                                  |\n"
        "|   \\\\  /    A nd           | Website:  www.openfoam.com                      |\n"
        "|    \\\\/     M anipulation  |                                                 |\n"
        "\\*---------------------------------------------------------------------------*/\n"
        "FoamFile\n"
        "{\n"
        "    version     2.0;\n"
        "    format      ascii;\n"
        f"    class       {class_name};\n"
        '    location    "constant/polyMesh";\n'
        f"    object      {object_name};\n"
        "}\n"
        "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n"
    )


def export_openfoam_polymesh(
    mesh: UnstructuredMesh,
    case_dir: Union[str, Path],
    write_binary: bool = False,
) -> Path:
    """Export an in-memory UnstructuredMesh to standard OpenFOAM constant/polyMesh.

    Writes points, faces, owner, neighbour (internal faces only!), and boundary.

    Args:
        mesh: In-memory UnstructuredMesh instance.
        case_dir: Base directory of the OpenFOAM case.
        write_binary: If True, binary format is requested (defaults to ASCII).

    Returns:
        Path to the generated constant/polyMesh directory.

    Raises:
        ValueError: If mesh invariants are violated or required face arrays are missing.
    """
    case_path = Path(case_dir).resolve()
    poly_mesh_dir = case_path / "constant" / "polyMesh"
    poly_mesh_dir.mkdir(parents=True, exist_ok=True)

    n_points = mesh.num_points
    n_faces = mesh.num_faces
    n_internal = mesh.num_internal_faces

    # 1. Export points
    points_path = poly_mesh_dir / "points"
    with open(points_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("vectorField", "points"))
        f.write(f"{n_points}\n(\n")
        pts = mesh.points
        for p in pts:
            f.write(f"({p[0]:.15g} {p[1]:.15g} {p[2]:.15g})\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    # 2. Export faces
    faces_list = mesh.faces
    if faces_list is None or len(faces_list) != n_faces:
        # Fallback if faces array is not explicitly populated: construct from cells
        raise ValueError(
            f"UnstructuredMesh lacks explicit face-to-node connectivity: "
            f"faces count is {len(faces_list) if faces_list is not None else 0}, expected {n_faces}"
        )

    faces_path = poly_mesh_dir / "faces"
    with open(faces_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("faceList", "faces"))
        f.write(f"{n_faces}\n(\n")
        for fnodes in faces_list:
            nodes_str = " ".join(str(int(v)) for v in fnodes)
            f.write(f"{len(fnodes)}({nodes_str})\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    # 3. Export owner
    owner_path = poly_mesh_dir / "owner"
    with open(owner_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("labelList", "owner"))
        f.write(f"{n_faces}\n(\n")
        for o in mesh.owner:
            f.write(f"{int(o)}\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    # 4. Export neighbour (INTERNAL FACES ONLY)
    neighbour_path = poly_mesh_dir / "neighbour"
    with open(neighbour_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("labelList", "neighbour"))
        f.write(f"{n_internal}\n(\n")
        for f_idx in range(n_internal):
            nb = mesh.neighbour[f_idx]
            if nb < 0:
                raise ValueError(
                    f"Contiguous internal face block violated: face {f_idx} has neighbour {nb} < 0"
                )
            f.write(f"{int(nb)}\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    # 5. Export boundary dictionary
    patch_types_meta = mesh.metadata.get("patch_types", {})
    boundaries = mesh.boundaries
    if not boundaries and (n_faces - n_internal) > 0:
        boundaries = {"wall": (n_internal, n_faces - n_internal)}

    boundary_path = poly_mesh_dir / "boundary"
    with open(boundary_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("polyBoundaryMesh", "boundary"))
        f.write(f"{len(boundaries)}\n(\n")
        for patch_name, (start_f, count_f) in boundaries.items():
            # Infer standard OpenFOAM patch type
            if patch_name in patch_types_meta:
                ptype = patch_types_meta[patch_name]
            elif "empty" in patch_name.lower() or patch_name == "frontAndBack":
                ptype = "empty"
            elif "symmetry" in patch_name.lower() or "sym" in patch_name.lower():
                ptype = "symmetry"
            elif "cyclic" in patch_name.lower():
                ptype = "cyclic"
            elif "mapped" in patch_name.lower() or "coupled" in patch_name.lower():
                ptype = "mappedWall"
            elif "inlet" in patch_name.lower() or "outlet" in patch_name.lower() or "patch" in patch_name.lower():
                ptype = "patch"
            else:
                ptype = "wall"

            f.write(f"    {patch_name}\n")
            f.write("    {\n")
            f.write(f"        type            {ptype};\n")
            f.write(f"        nFaces          {int(count_f)};\n")
            f.write(f"        startFace       {int(start_f)};\n")
            f.write("    }\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    return poly_mesh_dir


def extrude_2d_to_3d_slab(
    mesh_2d: UnstructuredMesh,
    thickness: float = 1.0,
) -> UnstructuredMesh:
    """Extrude a 2D planar mesh into a 1-cell thick 3D mesh for OpenFOAM simulations.

    Triangles (3-node) are extruded to wedges/prisms (6-node).
    Quads (4-node) are extruded to hexahedra (8-node).
    The top and bottom faces are gathered into an OpenFOAM boundary patch
    named 'frontAndBack' with 'type empty;'.

    Args:
        mesh_2d: Planar 2D mesh (CellType.TRIANGLE or CellType.QUAD).
        thickness: Extrusion thickness in meters along the normal direction.

    Returns:
        1-cell thick volumetric 3D UnstructuredMesh.

    Raises:
        ValueError: If thickness is non-positive or mesh_2d has unsupported elements.
    """
    if thickness <= 0.0:
        raise ValueError(f"Extrusion thickness must be positive, got {thickness}")

    n_pts_2d = mesh_2d.num_points
    pts_2d = mesh_2d.points.copy()

    # Create extruded points: layer 0 (bottom at z - thickness/2) and layer 1 (top at z + thickness/2)
    pts_bottom = pts_2d.copy()
    pts_bottom[:, 2] -= 0.5 * thickness
    pts_top = pts_2d.copy()
    pts_top[:, 2] += 0.5 * thickness
    pts_3d = np.vstack([pts_bottom, pts_top])

    cells_3d: List[np.ndarray] = []
    cell_types_3d: List[int] = []

    for c in mesh_2d.cells:
        nv = len(c)
        if nv == 4:
            # Quad -> Hexahedron (VTK 12)
            # Bottom vertices 0, 1, 2, 3; Top vertices 0+N, 1+N, 2+N, 3+N
            hex_nodes = np.array([
                c[0], c[1], c[2], c[3],
                c[0] + n_pts_2d, c[1] + n_pts_2d, c[2] + n_pts_2d, c[3] + n_pts_2d,
            ], dtype=np.int64)
            cells_3d.append(hex_nodes)
            cell_types_3d.append(int(CellType.HEXAHEDRON))
        elif nv == 3:
            # Triangle -> Wedge / Triangular prism (VTK 13)
            # Bottom 0, 1, 2; Top 0+N, 1+N, 2+N
            wedge_nodes = np.array([
                c[0], c[1], c[2],
                c[0] + n_pts_2d, c[1] + n_pts_2d, c[2] + n_pts_2d,
            ], dtype=np.int64)
            cells_3d.append(wedge_nodes)
            cell_types_3d.append(int(CellType.WEDGE))
        else:
            raise ValueError(f"Unsupported 2D cell with {nv} vertices for 3D slab extrusion")

    # Build 3D topology
    from backend.meshing.difvm_topology import compute_difvm_topology

    mesh_3d = compute_difvm_topology(
        points=pts_3d,
        cells=cells_3d,
        cell_types=np.array(cell_types_3d, dtype=np.int32),
        topological_dim=3,
        use_native=False,  # Use pure Python for slab extrusion to ensure exact patch tagging
    )

    # Separate boundaries into:
    # 1. 'frontAndBack' empty patch (normal pointing along -z or +z)
    # 2. Side boundary patches inherited from 2D
    n_faces = mesh_3d.num_faces
    internal_mask = mesh_3d.neighbour >= 0
    bnd_indices = np.where(~internal_mask)[0]

    front_back_faces: List[int] = []
    side_faces: List[int] = []

    for f_idx in bnd_indices:
        sf = mesh_3d.face_areas[f_idx]
        sf_norm = np.linalg.norm(sf)
        if sf_norm > 1e-15:
            unit_sf = sf / sf_norm
            # Check if normal is strictly in z-direction (top or bottom frontAndBack face)
            if abs(abs(unit_sf[2]) - 1.0) < 1e-3:
                front_back_faces.append(int(f_idx))
            else:
                side_faces.append(int(f_idx))
        else:
            side_faces.append(int(f_idx))

    # Reorder boundary faces so side faces come first, followed by frontAndBack
    n_int = mesh_3d.num_internal_faces
    new_face_order = list(range(n_int)) + side_faces + front_back_faces

    # Reindex face arrays
    reorder_idx = np.array(new_face_order, dtype=np.int64)
    inv_reorder = np.empty_like(reorder_idx)
    inv_reorder[reorder_idx] = np.arange(len(reorder_idx))

    new_owner = mesh_3d.owner[reorder_idx]
    new_neighbour = mesh_3d.neighbour[reorder_idx]
    new_face_areas = mesh_3d.face_areas[reorder_idx]
    new_face_centres = mesh_3d.face_centres[reorder_idx]
    new_delta_f = mesh_3d.delta_f[reorder_idx]
    new_k_f = mesh_3d.k_f[reorder_idx]
    new_faces = [mesh_3d.faces[i] for i in reorder_idx] if mesh_3d.faces is not None else None

    new_error_map = {}
    for k, v in mesh_3d.error_map.items():
        if isinstance(v, np.ndarray) and len(v) == n_faces:
            new_error_map[k] = v[reorder_idx]
        else:
            new_error_map[k] = v

    new_boundaries: Dict[str, Tuple[int, int]] = {}
    n_side = len(side_faces)
    n_fb = len(front_back_faces)

    if n_side > 0:
        new_boundaries["wall"] = (n_int, n_side)
    if n_fb > 0:
        new_boundaries["frontAndBack"] = (n_int + n_side, n_fb)

    metadata = dict(mesh_3d.metadata)
    metadata["patch_types"] = {
        "wall": "wall",
        "frontAndBack": "empty",
    }
    metadata["extrusion_thickness"] = thickness

    return UnstructuredMesh(
        points=mesh_3d.points,
        cells=cells_3d,
        owner=new_owner,
        neighbour=new_neighbour,
        face_areas=new_face_areas,
        face_centres=new_face_centres,
        cell_centres=mesh_3d.cell_centres,
        cell_volumes=mesh_3d.cell_volumes,
        delta_f=new_delta_f,
        k_f=new_k_f,
        boundaries=new_boundaries,
        cell_regions=mesh_3d.cell_regions,
        baffles=mesh_3d.baffles,
        cyclic_pairs=mesh_3d.cyclic_pairs,
        error_map=new_error_map,
        metadata=metadata,
        faces=new_faces,
        cell_types=np.array(cell_types_3d, dtype=np.int32),
        topological_dim=3,
    )
