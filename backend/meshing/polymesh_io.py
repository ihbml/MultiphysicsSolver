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
    arch_line = '    arch        "LSB;label=64;scalar=64";\n' if write_binary else ""
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
        f"{arch_line}"
        f"    class       {class_name};\n"
        '    location    "constant/polyMesh";\n'
        f"    object      {object_name};\n"
        "}\n"
        "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n"
    )


def _extract_face_csr_arrays(mesh: UnstructuredMesh, n_faces: int) -> Tuple[np.ndarray, np.ndarray]:
    """Return (face_offsets, face_nodes) int64 CSR arrays from an UnstructuredMesh."""
    fo_attr = getattr(mesh, "face_offsets", None)
    fn_attr = getattr(mesh, "face_nodes", None)
    if fo_attr is not None and fn_attr is not None:
        fo = np.asarray(fo_attr, dtype=np.int64)
        fn = np.asarray(fn_attr, dtype=np.int64)
        if len(fo) == n_faces + 1:
            return fo, fn

    faces_list = mesh.faces
    if faces_list is None or len(faces_list) != n_faces:
        raise ValueError(
            f"UnstructuredMesh lacks explicit face-to-node connectivity: "
            f"faces count is {len(faces_list) if faces_list is not None else 0}, expected {n_faces}"
        )
    lengths = np.fromiter((len(f) for f in faces_list), dtype=np.int64, count=n_faces)
    fo = np.zeros(n_faces + 1, dtype=np.int64)
    np.cumsum(lengths, out=fo[1:])
    fn = (
        np.concatenate([np.asarray(f, dtype=np.int64) for f in faces_list])
        if fo[-1] > 0
        else np.empty(0, dtype=np.int64)
    )
    return fo, fn


def _write_faces_ascii(faces_path: Path, mesh: UnstructuredMesh, n_faces: int) -> None:
    """Write OpenFOAM ASCII faceList streaming directly from CSR offsets/nodes when available."""
    fo_attr = getattr(mesh, "face_offsets", None)
    fn_attr = getattr(mesh, "face_nodes", None)
    use_csr = (
        fo_attr is not None
        and fn_attr is not None
        and len(fo_attr) == n_faces + 1
    )
    if use_csr:
        fo_list = np.asarray(fo_attr, dtype=np.int64).tolist()
        fn_list = np.asarray(fn_attr, dtype=np.int64).tolist()
    else:
        faces_list = mesh.faces
        if faces_list is None or len(faces_list) != n_faces:
            raise ValueError(
                f"UnstructuredMesh lacks explicit face-to-node connectivity: "
                f"faces count is {len(faces_list) if faces_list is not None else 0}, expected {n_faces}"
            )

    chunk_size = 16384
    with open(faces_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("faceList", "faces", write_binary=False))
        f.write(f"{n_faces}\n(\n")
        if use_csr:
            for start in range(0, n_faces, chunk_size):
                end = min(start + chunk_size, n_faces)
                lines = [
                    f"{fo_list[i + 1] - fo_list[i]}({' '.join(map(str, fn_list[fo_list[i]:fo_list[i + 1]]))})\n"
                    for i in range(start, end)
                ]
                f.write("".join(lines))
        else:
            for start in range(0, n_faces, chunk_size):
                end = min(start + chunk_size, n_faces)
                lines = [
                    f"{len(fnodes)}({' '.join(map(str, fnodes.tolist() if isinstance(fnodes, np.ndarray) else fnodes))})\n"
                    for fnodes in faces_list[start:end]
                ]
                f.write("".join(lines))
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")


def _write_faces_binary(faces_path: Path, mesh: UnstructuredMesh, n_faces: int) -> None:
    """Write OpenFOAM binary faceCompactList streaming directly from CSR offsets/nodes."""
    fo, fn = _extract_face_csr_arrays(mesh, n_faces)
    fo_c = np.ascontiguousarray(fo, dtype=np.int64)
    fn_c = np.ascontiguousarray(fn, dtype=np.int64)
    header = _foam_file_header("faceCompactList", "faces", write_binary=True).encode("utf-8")
    with open(faces_path, "wb") as f:
        f.write(header)
        f.write(f"{len(fo_c)}\n(".encode("utf-8"))
        f.write(fo_c.tobytes())
        f.write(f")\n{len(fn_c)}\n(".encode("utf-8"))
        f.write(fn_c.tobytes())
        f.write(b")\n\n// ************************************************************************* //\n")


def _write_boundary(
    poly_mesh_dir: Path,
    mesh: UnstructuredMesh,
    patch_types: Optional[Dict[str, str]],
    patch_metadata: Optional[Dict[str, Dict[str, Any]]],
    n_faces: int,
    n_internal: int,
) -> None:
    """Write OpenFOAM polyBoundaryMesh dictionary with rich patch_specs/metadata support."""
    meta = mesh.metadata or {}
    patch_types_meta = dict(meta.get("patch_types", {}))
    if patch_types is not None:
        patch_types_meta.update(patch_types)

    combined_patch_meta: Dict[str, Dict[str, Any]] = {}
    for meta_key in ("patch_specs", "patch_metadata"):
        src = meta.get(meta_key)
        if isinstance(src, dict):
            for k, v in src.items():
                entry = dict(v) if isinstance(v, dict) else {"type": str(v)}
                if k in combined_patch_meta:
                    combined_patch_meta[k].update(entry)
                else:
                    combined_patch_meta[k] = entry

    if patch_metadata is not None:
        for k, v in patch_metadata.items():
            entry = dict(v) if isinstance(v, dict) else {"type": str(v)}
            if k in combined_patch_meta:
                combined_patch_meta[k].update(entry)
            else:
                combined_patch_meta[k] = entry

    boundaries = mesh.boundaries
    if not boundaries and (n_faces - n_internal) > 0:
        boundaries = {"wall": (n_internal, n_faces - n_internal)}

    def _fmt_kv(key: str, val: Any) -> str:
        if key == "inGroups" and isinstance(val, (list, tuple)):
            val_str = f"{len(val)}({' '.join(str(x) for x in val)})"
        else:
            val_str = str(val)
        pad = " " * max(1, 16 - len(key))
        return f"        {key}{pad}{val_str};\n"

    boundary_path = poly_mesh_dir / "boundary"
    with open(boundary_path, "w", encoding="utf-8") as f:
        f.write(_foam_file_header("polyBoundaryMesh", "boundary"))
        f.write(f"{len(boundaries)}\n(\n")
        for patch_name, (start_f, count_f) in boundaries.items():
            p_lower = patch_name.lower()
            pmeta = combined_patch_meta.get(patch_name, {})
            if patch_types is not None and patch_name in patch_types:
                ptype = patch_types[patch_name]
            elif patch_name in patch_types_meta:
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
            f.write(_fmt_kv("type", ptype))
            if "inGroups" in pmeta:
                f.write(_fmt_kv("inGroups", pmeta["inGroups"]))
            f.write(_fmt_kv("nFaces", int(count_f)))
            f.write(_fmt_kv("startFace", int(start_f)))

            emitted = {"type", "inGroups", "nFaces", "startFace"}

            # Mapped wall / coupled patch attributes
            if ptype in ("mappedWall", "mappedPatch", "mappedField") or "sampleMode" in pmeta or "samplePatch" in pmeta:
                sample_mode = pmeta.get("sampleMode", "nearestPatchFace")
                f.write(_fmt_kv("sampleMode", sample_mode))
                emitted.add("sampleMode")
                for key in ("sampleRegion", "samplePatch", "offset"):
                    if key in pmeta:
                        f.write(_fmt_kv(key, pmeta[key]))
                        emitted.add(key)

            # Cyclic patch attributes
            if ptype in ("cyclic", "cyclicAMI") or "neighbourPatch" in pmeta:
                if "neighbourPatch" in pmeta:
                    f.write(_fmt_kv("neighbourPatch", pmeta["neighbourPatch"]))
                    emitted.add("neighbourPatch")
                transform = pmeta.get("transform", "noOrdering")
                f.write(_fmt_kv("transform", transform))
                emitted.add("transform")

            # Write other custom keys if specified
            for k, v in pmeta.items():
                if k not in emitted:
                    f.write(_fmt_kv(k, v))

            f.write("    }\n")
        f.write(")\n\n")
        f.write("// ************************************************************************* //\n")


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
            f.write(f"{n_points}\n(".encode("utf-8"))
            f.write(pts.tobytes())
            f.write(b")\n\n// ************************************************************************* //\n")
    else:
        with open(points_path, "w", encoding="utf-8") as f:
            f.write(_foam_file_header("vectorField", "points", write_binary=False))
            f.write(f"{n_points}\n(\n")
            pts = mesh.points
            for p in pts:
                f.write(f"({p[0]:.15g} {p[1]:.15g} {p[2]:.15g})\n")
            f.write(")\n\n")
            f.write("// ************************************************************************* //\n")

    # 2. Export faces (ASCII faceList for cross-reader compatibility per CONTRACTS.md §4)
    _write_faces_ascii(poly_mesh_dir / "faces", mesh, n_faces)

    # 3. Export owner
    owner_path = poly_mesh_dir / "owner"
    if write_binary:
        ow = np.ascontiguousarray(mesh.owner, dtype=np.int64)
        header = _foam_file_header("labelList", "owner", write_binary=True).encode("utf-8")
        with open(owner_path, "wb") as f:
            f.write(header)
            f.write(f"{n_faces}\n(".encode("utf-8"))
            f.write(ow.tobytes())
            f.write(b")\n\n// ************************************************************************* //\n")
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
            f.write(f"{n_internal}\n(".encode("utf-8"))
            f.write(nb.tobytes())
            f.write(b")\n\n// ************************************************************************* //\n")
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
    _write_boundary(
        poly_mesh_dir=poly_mesh_dir,
        mesh=mesh,
        patch_types=patch_types,
        patch_metadata=patch_metadata,
        n_faces=n_faces,
        n_internal=n_internal,
    )

    return poly_mesh_dir


def _write_region_properties(constant_dir: Path, region_properties: Dict[str, List[str]]) -> Path:
    """Write constant/regionProperties classifying fluid and solid regions."""
    constant_dir.mkdir(parents=True, exist_ok=True)
    region_props_file = constant_dir / "regionProperties"
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
    return region_props_file


def _extract_cell_csr_arrays(mesh: UnstructuredMesh, n_cells: int) -> Tuple[np.ndarray, np.ndarray]:
    """Return (cell_offsets, cell_nodes) int64 CSR arrays from an UnstructuredMesh."""
    co_attr = getattr(mesh, "cell_offsets", None)
    cn_attr = getattr(mesh, "cell_nodes", None)
    if co_attr is not None and cn_attr is not None:
        co = np.asarray(co_attr, dtype=np.int64)
        cn = np.asarray(cn_attr, dtype=np.int64)
        if len(co) == n_cells + 1:
            return co, cn

    cells_list = mesh.cells
    if cells_list is None or len(cells_list) != n_cells:
        return np.zeros(n_cells + 1, dtype=np.int64), np.empty(0, dtype=np.int64)
    lengths = np.fromiter((len(c) for c in cells_list), dtype=np.int64, count=n_cells)
    co = np.zeros(n_cells + 1, dtype=np.int64)
    np.cumsum(lengths, out=co[1:])
    cn = (
        np.concatenate([np.asarray(c, dtype=np.int64) for c in cells_list])
        if co[-1] > 0
        else np.empty(0, dtype=np.int64)
    )
    return co, cn


def split_regions(
    mesh: UnstructuredMesh,
    region_names: Optional[Dict[int, str]] = None,
) -> Dict[str, UnstructuredMesh]:
    """Split a multi-region UnstructuredMesh into self-contained per-region submeshes.

    Inter-region coupled faces (whether still internal with differing cell_regions or
    already extracted into coupledWall twin patches) are converted into outward-oriented
    ``mappedWall`` boundary patches named ``<region>_to_<other_region>`` with full
    ``patch_specs`` metadata. Vertex coordinates are compacted per region.
    """
    if mesh.cell_regions is None or mesh.num_cells == 0:
        default_name = region_names.get(0, "region0") if region_names else "region0"
        return {default_name: mesh}

    cell_regions = np.asarray(mesh.cell_regions, dtype=np.int32)
    unique_regions = [int(r) for r in np.unique(cell_regions)]

    def _resolve_name(rid: int) -> str:
        if region_names is not None:
            return region_names.get(rid, f"region_{rid}")
        return {0: "fluid", 1: "solid"}.get(rid, f"region_{rid}")

    if len(unique_regions) <= 1:
        rid = unique_regions[0] if unique_regions else 0
        return {_resolve_name(rid): mesh}

    rid_to_name = {rid: _resolve_name(rid) for rid in unique_regions}
    n_orig_faces = mesh.num_faces
    n_orig_cells = mesh.num_cells
    owner = np.asarray(mesh.owner, dtype=np.int64)
    neighbour = np.asarray(mesh.neighbour, dtype=np.int64)
    orig_fo, orig_fn = _extract_face_csr_arrays(mesh, n_orig_faces)
    orig_co, orig_cn = _extract_cell_csr_arrays(mesh, n_orig_cells)

    # Identify already-extracted coupledWall twin faces -> map face_idx -> other_rid
    coupled_face_other_region: Dict[int, int] = {}
    for baffle in (mesh.baffles or []):
        twin_pairs = baffle.get("twin_pairs")
        if twin_pairs:
            for fa, fb in twin_pairs:
                fa_i, fb_i = int(fa), int(fb)
                if 0 <= fa_i < n_orig_faces and 0 <= fb_i < n_orig_faces:
                    ra = int(cell_regions[owner[fa_i]])
                    rb = int(cell_regions[owner[fb_i]])
                    if ra != rb:
                        coupled_face_other_region[fa_i] = rb
                        coupled_face_other_region[fb_i] = ra

    meta_twins = (mesh.metadata or {}).get("coupled_twin_pairs")
    if meta_twins:
        for fa, fb in meta_twins:
            fa_i, fb_i = int(fa), int(fb)
            if 0 <= fa_i < n_orig_faces and 0 <= fb_i < n_orig_faces:
                ra = int(cell_regions[owner[fa_i]])
                rb = int(cell_regions[owner[fb_i]])
                if ra != rb:
                    coupled_face_other_region[fa_i] = rb
                    coupled_face_other_region[fb_i] = ra

    # Fallback: match coupledWall boundary patches lacking explicit twin_pairs by sorted node key
    coupled_patch_names = {
        b["name"] for b in (mesh.baffles or []) if b.get("type") == "coupledWall" and "name" in b
    }
    for pname in (mesh.boundaries or {}):
        if "coupled" in pname.lower():
            coupled_patch_names.add(pname)

    unmatched_coupled_by_key: Dict[Tuple[int, ...], List[int]] = {}
    for pname in coupled_patch_names:
        if pname in (mesh.boundaries or {}):
            s_f, c_f = mesh.boundaries[pname]
            for f_idx in range(int(s_f), int(s_f) + int(c_f)):
                if f_idx not in coupled_face_other_region and neighbour[f_idx] < 0:
                    key = tuple(sorted(int(v) for v in orig_fn[orig_fo[f_idx]:orig_fo[f_idx + 1]]))
                    unmatched_coupled_by_key.setdefault(key, []).append(f_idx)
    for f_pair in unmatched_coupled_by_key.values():
        if len(f_pair) == 2:
            fa_i, fb_i = f_pair[0], f_pair[1]
            ra = int(cell_regions[owner[fa_i]])
            rb = int(cell_regions[owner[fb_i]])
            if ra != rb:
                coupled_face_other_region[fa_i] = rb
                coupled_face_other_region[fb_i] = ra

    # Map original boundary faces (not coupled) to their patch names in order
    orig_bnd_order: List[str] = list((mesh.boundaries or {}).keys())
    face_to_ext_patch: Dict[int, str] = {}
    for pname, (s_f, c_f) in (mesh.boundaries or {}).items():
        for f_idx in range(int(s_f), int(s_f) + int(c_f)):
            if f_idx not in coupled_face_other_region:
                face_to_ext_patch[f_idx] = pname

    orig_patch_specs = dict((mesh.metadata or {}).get("patch_specs", {}))
    orig_patch_meta = dict((mesh.metadata or {}).get("patch_metadata", {}))
    orig_patch_types = dict((mesh.metadata or {}).get("patch_types", {}))
    has_face_offsets_field = "face_offsets" in getattr(UnstructuredMesh, "__dataclass_fields__", {})
    has_cell_offsets_field = "cell_offsets" in getattr(UnstructuredMesh, "__dataclass_fields__", {})

    result: Dict[str, UnstructuredMesh] = {}

    for rid in sorted(unique_regions):
        rname = rid_to_name[rid]
        reg_cells = np.where(cell_regions == rid)[0]
        n_reg_cells = len(reg_cells)
        global_to_local = np.full(n_orig_cells, -1, dtype=np.int64)
        global_to_local[reg_cells] = np.arange(n_reg_cells, dtype=np.int64)

        # (local_owner, local_neighbour, orig_face_idx, flipped)
        internal_entries: List[Tuple[int, int, int, bool]] = []
        # other_rid -> List[(local_owner, orig_face_idx, flipped)]
        coupled_groups: Dict[int, List[Tuple[int, int, bool]]] = {}
        # patch_name -> List[(local_owner, orig_face_idx)]
        ext_bnd_groups: Dict[str, List[Tuple[int, int]]] = {}

        for f_idx in range(n_orig_faces):
            o_glob = int(owner[f_idx])
            nb_glob = int(neighbour[f_idx])
            if nb_glob >= 0:
                ro = int(cell_regions[o_glob])
                rnb = int(cell_regions[nb_glob])
                if ro == rid and rnb == rid:
                    lo = int(global_to_local[o_glob])
                    lnb = int(global_to_local[nb_glob])
                    if lo < lnb:
                        internal_entries.append((lo, lnb, f_idx, False))
                    else:
                        internal_entries.append((lnb, lo, f_idx, True))
                elif ro == rid:
                    coupled_groups.setdefault(rnb, []).append((int(global_to_local[o_glob]), f_idx, False))
                elif rnb == rid:
                    coupled_groups.setdefault(ro, []).append((int(global_to_local[nb_glob]), f_idx, True))
            else:
                if int(cell_regions[o_glob]) != rid:
                    continue
                lo = int(global_to_local[o_glob])
                if f_idx in coupled_face_other_region:
                    other_rid = coupled_face_other_region[f_idx]
                    coupled_groups.setdefault(other_rid, []).append((lo, f_idx, False))
                else:
                    pname = face_to_ext_patch.get(f_idx, "wall")
                    ext_bnd_groups.setdefault(pname, []).append((lo, f_idx))

        internal_entries.sort(key=lambda t: (t[0], t[1]))
        n_int_reg = len(internal_entries)

        # Assemble ordered face records: (local_owner, local_neighbour, orig_f, flipped)
        ordered_records: List[Tuple[int, int, int, bool]] = list(internal_entries)
        local_boundaries: Dict[str, Tuple[int, int]] = {}
        patch_specs: Dict[str, Dict[str, Any]] = {}
        cursor = n_int_reg

        for other_rid in sorted(coupled_groups.keys()):
            other_name = rid_to_name[other_rid]
            patch_name = f"{rname}_to_{other_name}"
            grp = coupled_groups[other_rid]
            local_boundaries[patch_name] = (cursor, len(grp))
            patch_specs[patch_name] = {
                "type": "mappedWall",
                "inGroups": "1(wall)",
                "sampleMode": "nearestPatchFace",
                "sampleRegion": other_name,
                "samplePatch": f"{other_name}_to_{rname}",
            }
            for lo, f_idx, flipped in grp:
                ordered_records.append((lo, -1, f_idx, flipped))
            cursor += len(grp)

        # Deterministic external boundary patch order: original order first, then any fallback ("wall")
        ext_patch_order = [p for p in orig_bnd_order if p in ext_bnd_groups]
        for p in sorted(ext_bnd_groups.keys()):
            if p not in ext_patch_order:
                ext_patch_order.append(p)

        for pname in ext_patch_order:
            grp = ext_bnd_groups[pname]
            local_boundaries[pname] = (cursor, len(grp))
            if pname in orig_patch_specs:
                patch_specs[pname] = dict(orig_patch_specs[pname])
            elif pname in orig_patch_meta:
                patch_specs[pname] = dict(orig_patch_meta[pname])
            elif pname in orig_patch_types:
                patch_specs[pname] = {"type": orig_patch_types[pname]}
            for lo, f_idx in grp:
                ordered_records.append((lo, -1, f_idx, False))
            cursor += len(grp)

        n_reg_faces = len(ordered_records)

        # Collect and compact vertex indices referenced by region faces and cells
        used_node_chunks: List[np.ndarray] = []
        for _, _, f_idx, _ in ordered_records:
            used_node_chunks.append(orig_fn[orig_fo[f_idx]:orig_fo[f_idx + 1]])
        if len(orig_cn) > 0:
            for c_glob in reg_cells:
                used_node_chunks.append(orig_cn[orig_co[c_glob]:orig_co[c_glob + 1]])
        used_pts = (
            np.unique(np.concatenate(used_node_chunks))
            if used_node_chunks
            else np.empty(0, dtype=np.int64)
        )
        pt_remap = np.full(mesh.num_points, -1, dtype=np.int64)
        pt_remap[used_pts] = np.arange(len(used_pts), dtype=np.int64)
        local_points = np.ascontiguousarray(mesh.points[used_pts], dtype=np.float64)

        local_cell_centres = np.ascontiguousarray(mesh.cell_centres[reg_cells], dtype=np.float64)
        local_cell_volumes = np.ascontiguousarray(mesh.cell_volumes[reg_cells], dtype=np.float64)
        local_cell_types = (
            np.ascontiguousarray(mesh.cell_types[reg_cells], dtype=np.int32)
            if mesh.cell_types is not None
            else None
        )
        local_cell_regions = np.full(n_reg_cells, rid, dtype=np.int32)

        local_owner = np.empty(n_reg_faces, dtype=np.int64)
        local_neighbour = np.empty(n_reg_faces, dtype=np.int64)
        local_face_areas = np.empty((n_reg_faces, 3), dtype=np.float64)
        local_face_centres = np.empty((n_reg_faces, 3), dtype=np.float64)
        local_delta_f = np.empty((n_reg_faces, 3), dtype=np.float64)
        local_k_f = np.empty((n_reg_faces, 3), dtype=np.float64)

        local_faces_list: List[np.ndarray] = []
        face_lengths = np.empty(n_reg_faces, dtype=np.int64)
        orig_face_indices = np.empty(n_reg_faces, dtype=np.int64)
        flipped_mask = np.zeros(n_reg_faces, dtype=bool)

        for idx, (lo, lnb, f_idx, flipped) in enumerate(ordered_records):
            local_owner[idx] = lo
            local_neighbour[idx] = lnb
            orig_face_indices[idx] = f_idx
            flipped_mask[idx] = flipped

            fn_slice = orig_fn[orig_fo[f_idx]:orig_fo[f_idx + 1]]
            remapped = pt_remap[fn_slice[::-1]] if flipped else pt_remap[fn_slice]
            remapped = np.ascontiguousarray(remapped, dtype=np.int64)
            local_faces_list.append(remapped)
            face_lengths[idx] = len(remapped)

            sf = mesh.face_areas[f_idx]
            cf = mesh.face_centres[f_idx]
            local_face_areas[idx] = -sf if flipped else sf
            local_face_centres[idx] = cf

            if lnb >= 0:
                local_delta_f[idx] = -mesh.delta_f[f_idx] if flipped else mesh.delta_f[f_idx]
                local_k_f[idx] = -mesh.k_f[f_idx] if flipped else mesh.k_f[f_idx]
            else:
                local_delta_f[idx] = cf - local_cell_centres[lo]
                local_k_f[idx] = 0.0

        local_fo = np.zeros(n_reg_faces + 1, dtype=np.int64)
        np.cumsum(face_lengths, out=local_fo[1:])
        local_fn = (
            np.concatenate(local_faces_list)
            if n_reg_faces > 0 and local_fo[-1] > 0
            else np.empty(0, dtype=np.int64)
        )

        local_cells_list: List[np.ndarray] = []
        cell_lengths = np.zeros(n_reg_cells, dtype=np.int64)
        if len(orig_cn) > 0:
            for i_c, c_glob in enumerate(reg_cells):
                cn_slice = orig_cn[orig_co[c_glob]:orig_co[c_glob + 1]]
                remapped_c = np.ascontiguousarray(pt_remap[cn_slice], dtype=np.int64)
                local_cells_list.append(remapped_c)
                cell_lengths[i_c] = len(remapped_c)
        local_co = np.zeros(n_reg_cells + 1, dtype=np.int64)
        np.cumsum(cell_lengths, out=local_co[1:])
        local_cn = (
            np.concatenate(local_cells_list)
            if local_cells_list and local_co[-1] > 0
            else np.empty(0, dtype=np.int64)
        )

        # Slice error_map cleanly
        local_error_map: Dict[str, np.ndarray] = {}
        for k, v in (mesh.error_map or {}).items():
            if isinstance(v, np.ndarray):
                if len(v) == n_orig_faces:
                    sliced = v[orig_face_indices].copy()
                    if v.ndim == 2 and k in ("m_skew", "k_f_comp") and np.any(flipped_mask):
                        sliced[flipped_mask] = -sliced[flipped_mask]
                    local_error_map[k] = sliced
                elif len(v) == n_orig_cells:
                    local_error_map[k] = v[reg_cells].copy()

        mesh_kwargs: Dict[str, Any] = {
            "points": local_points,
            "cells": local_cells_list,
            "owner": local_owner,
            "neighbour": local_neighbour,
            "face_areas": local_face_areas,
            "face_centres": local_face_centres,
            "cell_centres": local_cell_centres,
            "cell_volumes": local_cell_volumes,
            "delta_f": local_delta_f,
            "k_f": local_k_f,
            "boundaries": local_boundaries,
            "cell_regions": local_cell_regions,
            "baffles": [],
            "cyclic_pairs": [],
            "error_map": local_error_map,
            "faces": local_faces_list,
            "cell_types": local_cell_types,
            "topological_dim": mesh.topological_dim,
            "metadata": {
                "region_id": int(rid),
                "region_name": rname,
                "patch_specs": patch_specs,
            },
        }
        if has_face_offsets_field:
            mesh_kwargs["face_offsets"] = local_fo
            mesh_kwargs["face_nodes"] = local_fn
        if has_cell_offsets_field:
            mesh_kwargs["cell_offsets"] = local_co
            mesh_kwargs["cell_nodes"] = local_cn

        result[rname] = UnstructuredMesh(**mesh_kwargs)

    return result


def export_multiregion_openfoam_case(
    mesh: UnstructuredMesh,
    case_dir: Union[str, Path],
    region_names: Optional[Dict[int, str]] = None,
    binary: bool = False,
) -> Dict[str, Path]:
    """Split a multi-region UnstructuredMesh and export OpenFOAM constant/<region>/polyMesh + regionProperties."""
    case_path = Path(case_dir).resolve()
    region_meshes = split_regions(mesh, region_names=region_names)

    poly_mesh_dirs: Dict[str, Path] = {}
    fluid_list: List[str] = []
    solid_list: List[str] = []

    for rname, rmesh in region_meshes.items():
        poly_mesh_dirs[rname] = export_openfoam_polymesh(
            mesh=rmesh,
            case_dir=case_path,
            write_binary=binary,
            region_name=rname,
        )
        if "solid" in rname.lower():
            solid_list.append(rname)
        else:
            fluid_list.append(rname)

    _write_region_properties(case_path / "constant", {"fluid": fluid_list, "solid": solid_list})
    return poly_mesh_dirs


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

    _write_region_properties(case_dir / "constant", region_properties)

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

    # Build 3D topology via pure-Python constructor (strict_native-safe slab extrusion)
    from backend.meshing.difvm_topology import compute_difvm_topology_python

    mesh_3d = compute_difvm_topology_python(
        points=pts_3d,
        cells=cells_3d,
        cell_types=np.array(cell_types_3d, dtype=np.int32),
        topological_dim=3,
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
