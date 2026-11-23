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
from backend.meshing.difvm_topology import (
    compute_symmetry_mirror_point,
    compute_householder_reflection,
    compute_symmetry_vector_diffusion_split,
    compute_symmetry_tensor_diffusion_split,
)


def _foam_file_header(class_name: str, object_name: str, write_binary: bool = False) -> str:
    """Generate standard OpenFOAM FoamFile header banner.

    Args:
        class_name: OpenFOAM class name (e.g. vectorField, faceList, labelList, polyBoundaryMesh).
        object_name: OpenFOAM dictionary/object name (e.g. points, faces, owner, neighbour, boundary).
        write_binary: If True, writes format binary; else format ascii;.

    Returns:
        Formatted FoamFile header block string.
    """
    fmt = "binary" if write_binary else "ascii"
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
        f"    format      {fmt};\n"
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
    patch_types: Optional[Dict[str, str]] = None,
    patch_metadata: Optional[Dict[str, Dict[str, Any]]] = None,
    region_name: Optional[str] = None,
) -> Path:
    """Export an in-memory UnstructuredMesh to standard OpenFOAM constant/polyMesh.

    Writes points, faces, owner, neighbour (internal faces only!), and boundary.

    Args:
        mesh: In-memory UnstructuredMesh instance.
        case_dir: Base directory of the OpenFOAM case.
        write_binary: If True, binary format is requested (defaults to ASCII).
        patch_types: Optional explicit mapping of patch names to OpenFOAM patch types.
        patch_metadata: Optional dictionary mapping patch names to extra dictionary
            attributes (e.g. sampleMode, sampleRegion, samplePatch for mappedWall;
            neighbourPatch, transform for cyclic).
        region_name: Optional multi-region name (exports to constant/<region_name>/polyMesh).

    Returns:
        Path to the generated constant/[<region_name>/]polyMesh directory.

    Raises:
        ValueError: If mesh invariants are violated or required face arrays are missing.
    """
    case_path = Path(case_dir).resolve()
    if region_name:
        poly_mesh_dir = case_path / "constant" / region_name / "polyMesh"
    else:
        poly_mesh_dir = case_path / "constant" / "polyMesh"
    poly_mesh_dir.mkdir(parents=True, exist_ok=True)

    n_points = mesh.num_points
    n_faces = mesh.num_faces
    n_internal = mesh.num_internal_faces

    # 1. Export points
    points_path = poly_mesh_dir / "points"
    if write_binary:
        pts = np.ascontiguousarray(mesh.points, dtype=np.float64)
        header = _foam_file_header("vectorField", "points", write_binary=True).encode("utf-8")
        with open(points_path, "wb") as f:
            f.write(header)
            f.write(f"{n_points}\n(\n".encode("utf-8"))
            f.write(pts.tobytes())
            f.write(b"\n)\n\n// ************************************************************************* //\n")
    else:
        with open(points_path, "w", encoding="utf-8") as f:
            f.write(_foam_file_header("vectorField", "points", write_binary=False))
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
        f.write(_foam_file_header("faceList", "faces", write_binary=False))
        f.write(f"{n_faces}\n(\n")
        for fnodes in faces_list:
            nodes_str = " ".join(str(int(v)) for v in fnodes)
            f.write(f"{len(fnodes)}({nodes_str})\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    # 3. Export owner
    owner_path = poly_mesh_dir / "owner"
    if write_binary:
        ow = np.ascontiguousarray(mesh.owner, dtype=np.int64)
        header = _foam_file_header("labelList", "owner", write_binary=True).encode("utf-8")
        with open(owner_path, "wb") as f:
            f.write(header)
            f.write(f"{n_faces}\n(\n".encode("utf-8"))
            f.write(ow.tobytes())
            f.write(b"\n)\n\n// ************************************************************************* //\n")
    else:
        with open(owner_path, "w", encoding="utf-8") as f:
            f.write(_foam_file_header("labelList", "owner", write_binary=False))
            f.write(f"{n_faces}\n(\n")
            for o in mesh.owner:
                f.write(f"{int(o)}\n")
            f.write(")\n\n")
            f.write("// ************************************************************************* //\n")

    # 4. Export neighbour (INTERNAL FACES ONLY)
    neighbour_path = poly_mesh_dir / "neighbour"
    if write_binary:
        nb = np.ascontiguousarray(mesh.neighbour[:n_internal], dtype=np.int64)
        header = _foam_file_header("labelList", "neighbour", write_binary=True).encode("utf-8")
        with open(neighbour_path, "wb") as f:
            f.write(header)
            f.write(f"{n_internal}\n(\n".encode("utf-8"))
            f.write(nb.tobytes())
            f.write(b"\n)\n\n// ************************************************************************* //\n")
    else:
        with open(neighbour_path, "w", encoding="utf-8") as f:
            f.write(_foam_file_header("labelList", "neighbour", write_binary=False))
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
    patch_types_meta = dict(mesh.metadata.get("patch_types", {}))
    if patch_types is not None:
        patch_types_meta.update(patch_types)

    combined_patch_meta: Dict[str, Dict[str, Any]] = {}
    if "patch_metadata" in mesh.metadata and isinstance(mesh.metadata["patch_metadata"], dict):
        for k, v in mesh.metadata["patch_metadata"].items():
            combined_patch_meta[k] = dict(v) if isinstance(v, dict) else {"type": str(v)}
    if patch_metadata is not None:
        for k, v in patch_metadata.items():
            if k in combined_patch_meta:
                combined_patch_meta[k].update(v)
            else:
                combined_patch_meta[k] = dict(v) if isinstance(v, dict) else {"type": str(v)}

    boundaries = mesh.boundaries
    if not boundaries and (n_faces - n_internal) > 0:
        boundaries = {"wall": (n_internal, n_faces - n_internal)}

    boundary_path = poly_mesh_dir / "boundary"
    with open(boundary_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("polyBoundaryMesh", "boundary"))
        f.write(f"{len(boundaries)}\n(\n")
        for patch_name, (start_f, count_f) in boundaries.items():
            # Infer standard OpenFOAM patch type
            p_lower = patch_name.lower()
            pmeta = combined_patch_meta.get(patch_name, {})
            if patch_name in patch_types_meta:
                ptype = patch_types_meta[patch_name]
            elif "type" in pmeta:
                ptype = str(pmeta["type"])
            elif p_lower in ("top", "sides", "bottom"):
                ptype = patch_types_meta.get("farfield", "symmetry")
            elif "empty" in p_lower or patch_name == "frontAndBack":
                ptype = "empty"
            elif "symmetry" in p_lower or "sym" in p_lower:
                ptype = "symmetry"
            elif "cyclic" in p_lower:
                ptype = "cyclic"
            elif "mapped" in p_lower or "coupled" in p_lower:
                ptype = "mappedWall"
            elif "inlet" in p_lower or "outlet" in p_lower or "patch" in p_lower:
                ptype = "patch"
            else:
                ptype = "wall"

            f.write(f"    {patch_name}\n")
            f.write("    {\n")
            f.write(f"        type            {ptype};\n")
            f.write(f"        nFaces          {int(count_f)};\n")
            f.write(f"        startFace       {int(start_f)};\n")

            # Mapped wall / coupled patch attributes
            if ptype in ("mappedWall", "mappedPatch", "mappedField") or "sampleMode" in pmeta or "samplePatch" in pmeta:
                sample_mode = pmeta.get("sampleMode", "nearestPatchFace")
                f.write(f"        sampleMode      {sample_mode};\n")
                if "sampleRegion" in pmeta:
                    f.write(f"        sampleRegion    {pmeta['sampleRegion']};\n")
                if "samplePatch" in pmeta:
                    f.write(f"        samplePatch     {pmeta['samplePatch']};\n")
                if "offset" in pmeta:
                    f.write(f"        offset          {pmeta['offset']};\n")

            # Cyclic patch attributes
            if ptype in ("cyclic", "cyclicAMI") or "neighbourPatch" in pmeta:
                if "neighbourPatch" in pmeta:
                    f.write(f"        neighbourPatch  {pmeta['neighbourPatch']};\n")
                transform = pmeta.get("transform", "noOrdering")
                f.write(f"        transform       {transform};\n")

            # Write other custom keys if specified
            for k, v in pmeta.items():
                if k not in ("type", "sampleMode", "sampleRegion", "samplePatch", "offset", "neighbourPatch", "transform"):
                    f.write(f"        {k}            {v};\n")

            f.write("    }\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")

    return poly_mesh_dir


def export_openfoam_multiregion_polymesh(
    case_path: Union[str, Path],
    regions: Union[
        Dict[str, Tuple[UnstructuredMesh, Dict[str, Any]]],
        Dict[str, UnstructuredMesh]
    ],
    region_properties: Optional[Dict[str, List[str]]] = None
) -> Dict[str, Path]:
    """Export multiple in-memory UnstructuredMesh regions into constant/<region>/polyMesh.

    Also generates constant/regionProperties for multi-region solvers such as chtMultiRegionFoam.

    Args:
        case_path: Base directory of the OpenFOAM case.
        regions: Dict mapping region name to either:
            - UnstructuredMesh
            - Tuple[UnstructuredMesh, Dict[str, Any]] where the dict can contain:
                - "patch_types": Dict[str, str]
                - "patch_metadata": Dict[str, Dict[str, Any]]
        region_properties: Optional dict mapping region category ("fluid", "solid")
            to a list of region names. If omitted, inferred automatically:
            regions with "solid" in name -> solid, otherwise -> fluid.

    Returns:
        Dict mapping region name to the Path of its generated polyMesh directory.

    Raises:
        ValueError: If regions is empty, or if region_properties references unknown regions.
    """
    if not regions:
        raise ValueError("regions dictionary cannot be empty; must contain at least one mesh region")

    case_dir = Path(case_path).resolve()
    poly_mesh_dirs: Dict[str, Path] = {}

    for region_name, region_val in regions.items():
        if isinstance(region_val, tuple):
            if len(region_val) != 2:
                raise ValueError(
                    f"Region '{region_name}' tuple must be (UnstructuredMesh, metadata_dict), got length {len(region_val)}"
                )
            mesh, meta = region_val
            if not isinstance(meta, dict):
                raise ValueError(f"Region '{region_name}' metadata must be a dict, got {type(meta)}")
        else:
            mesh = region_val
            meta = {}

        p_types = meta.get("patch_types", None)
        p_meta = meta.get("patch_metadata", None)

        p_dir = export_openfoam_polymesh(
            mesh=mesh,
            case_dir=case_dir,
            patch_types=p_types,
            patch_metadata=p_meta,
            region_name=region_name,
        )
        poly_mesh_dirs[region_name] = p_dir

    # Write constant/regionProperties
    constant_dir = case_dir / "constant"
    constant_dir.mkdir(parents=True, exist_ok=True)
    region_props_file = constant_dir / "regionProperties"

    if region_properties is None:
        fluid_list = []
        solid_list = []
        for r_name in regions.keys():
            if "solid" in r_name.lower():
                solid_list.append(r_name)
            else:
                fluid_list.append(r_name)
        region_properties = {"fluid": fluid_list, "solid": solid_list}
    else:
        # Validate that all referenced regions exist
        for cat, r_list in region_properties.items():
            if not isinstance(r_list, (list, tuple)):
                raise ValueError(f"regionProperties['{cat}'] must be a list/tuple of region names")
            for r in r_list:
                if r not in regions:
                    raise ValueError(
                        f"regionProperties references region '{r}' in category '{cat}', but '{r}' is not in regions"
                    )

    fluid_str = " ".join(region_properties.get("fluid", []))
    solid_str = " ".join(region_properties.get("solid", []))

    content = f"""/*--------------------------------*- C++ -*----------------------------------*\\
| =========                 |                                                 |
| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\\\    /   O peration     | Datei: constant/regionProperties                |
|   \\\\  /    A nd           | Mehrregionen-Topologie (chtMultiRegionFoam)     |
|    \\\\/     M anipulation  |                                                 |
\\*---------------------------------------------------------------------------*/
FoamFile
{{
    version     2.0;
    format      ascii;
    class       dictionary;
    location    "constant";
    object      regionProperties;
}}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

regions
(
    fluid ( {fluid_str} )
    solid ( {solid_str} )
);

// ************************************************************************* //
"""
    with open(region_props_file, "w", encoding="utf-8") as f:
        f.write(content)

    return poly_mesh_dirs


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
