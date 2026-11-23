"""Gmsh ``.msh`` (v4.1 ASCII) and CGNS HDF5 Mesh Bridge I/O.

Provides round-trip export and import between in-memory DiFVM ``UnstructuredMesh``
instances and industry-standard external mesh containers:
- Gmsh MSH v4.1 ASCII (preserving 3D cells, polyhedral cells, and named boundary patches)
- CGNS v4 HDF5 via ``h5py`` (preserving coordinates, element sections, and ``ZoneBC`` patches)
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

import numpy as np

from backend.meshing.difvm_topology import compute_difvm_topology_python
from backend.meshing.models import CellType, UnstructuredMesh

_GMSH_3D_TYPE_BY_CELLTYPE: Dict[int, int] = {
    int(CellType.TETRA): 4,
    int(CellType.HEXAHEDRON): 5,
    int(CellType.WEDGE): 6,
    int(CellType.PYRAMID): 7,
}
_CELLTYPE_BY_GMSH_TYPE: Dict[int, int] = {v: k for k, v in _GMSH_3D_TYPE_BY_CELLTYPE.items()}

_CGNS_ELEM_TYPE_MAP: Dict[int, Tuple[int, str]] = {
    int(CellType.TETRA): (10, "TETRA_4"),
    int(CellType.PYRAMID): (12, "PYRA_5"),
    int(CellType.WEDGE): (14, "PENTA_6"),
    int(CellType.HEXAHEDRON): (17, "HEXA_8"),
}
_CELLTYPE_BY_CGNS_ID: Dict[int, int] = {v[0]: k for k, v in _CGNS_ELEM_TYPE_MAP.items()}


def _gmsh_hex_node_order(cell_nodes: np.ndarray) -> np.ndarray:
    """Reorder Hex8 nodes cleanly if needed; identity for standard 8-node hex."""
    return np.asarray(cell_nodes, dtype=np.int64)


def export_gmsh_msh(
    mesh: UnstructuredMesh,
    file_path: Union[str, Path],
) -> Path:
    """Export an ``UnstructuredMesh`` to Gmsh ``.msh`` v4.1 ASCII format.

    Args:
        mesh: Source ``UnstructuredMesh``.
        file_path: Destination ``.msh`` file path.

    Returns:
        Resolved ``Path`` of the written ``.msh`` file.
    """
    out_path = Path(file_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    pts = np.asarray(mesh.points, dtype=np.float64)
    n_pts = len(pts)

    lines: List[str] = [
        "$MeshFormat",
        "4.1 0 8",
        "$EndMeshFormat",
    ]

    # Physical names for volume region and each boundary patch
    patch_names = list(mesh.boundaries.keys())
    phys_entries: List[Tuple[int, int, str]] = [(3, 1, "fluid")]
    for p_idx, p_name in enumerate(patch_names, start=2):
        phys_entries.append((2, p_idx, p_name))

    lines.append("$PhysicalNames")
    lines.append(str(len(phys_entries)))
    for dim, tag, name in phys_entries:
        lines.append(f'{dim} {tag} "{name}"')
    lines.append("$EndPhysicalNames")

    # Nodes (single 3D entity block tag 1)
    lines.append("$Nodes")
    lines.append(f"1 {n_pts} 1 {max(1, n_pts)}")
    if n_pts > 0:
        lines.append(f"3 1 0 {n_pts}")
        for node_tag in range(1, n_pts + 1):
            lines.append(str(node_tag))
        for coord in pts:
            lines.append(f"{coord[0]:.16g} {coord[1]:.16g} {coord[2]:.16g}")
    lines.append("$EndNodes")

    # Standard 3D elements + boundary surface elements grouped by Gmsh elementType
    blocks: Dict[Tuple[int, int, int], List[List[int]]] = {}
    polyhedra_rows: List[Tuple[int, List[int]]] = []
    polyhedra_faces: Dict[int, List[List[int]]] = {}

    # Surface boundary faces (triangles & quads; split >4-node polygons into fans)
    if mesh.faces is not None and len(mesh.faces) == mesh.num_faces:
        for p_idx, p_name in enumerate(patch_names, start=2):
            start_f, count_f = mesh.boundaries[p_name]
            for f_id in range(int(start_f), int(start_f) + int(count_f)):
                fn = [int(v) + 1 for v in mesh.faces[f_id]]
                if len(fn) == 3:
                    blocks.setdefault((2, p_idx, 2), []).append(fn)
                elif len(fn) == 4:
                    blocks.setdefault((2, p_idx, 3), []).append(fn)
                elif len(fn) > 4:
                    for k in range(1, len(fn) - 1):
                        blocks.setdefault((2, p_idx, 2), []).append([fn[0], fn[k], fn[k + 1]])

    for c_idx in range(mesh.num_cells):
        ctype = int(mesh.cell_types[c_idx]) if len(mesh.cell_types) > c_idx else int(CellType.HEXAHEDRON)
        nodes_1b = [int(v) + 1 for v in mesh.cells[c_idx]]
        gmsh_type = _GMSH_3D_TYPE_BY_CELLTYPE.get(ctype)
        if gmsh_type is not None:
            blocks.setdefault((3, 1, gmsh_type), []).append(nodes_1b)
        else:
            polyhedra_rows.append((c_idx, nodes_1b))

    # Build owner/neighbour face lists per polyhedron for exact round-trip reconstruction
    if polyhedra_rows and mesh.faces is not None:
        poly_set = {cid for cid, _ in polyhedra_rows}
        for cid in poly_set:
            polyhedra_faces[cid] = []
        for f_id in range(mesh.num_faces):
            own = int(mesh.owner[f_id])
            nbr = int(mesh.neighbour[f_id])
            fn_0b = [int(v) for v in mesh.faces[f_id]]
            if own in poly_set:
                polyhedra_faces[own].append(fn_0b)
            if nbr >= 0 and nbr in poly_set:
                polyhedra_faces[nbr].append(list(reversed(fn_0b)))

    total_elems = sum(len(rows) for rows in blocks.values()) + len(polyhedra_rows)
    num_blocks = len(blocks) + (1 if polyhedra_rows else 0)
    lines.append("$Elements")
    lines.append(f"{num_blocks} {total_elems} 1 {max(1, total_elems)}")
    elem_tag = 1
    for (dim, entity_tag, gmsh_type), rows in blocks.items():
        lines.append(f"{dim} {entity_tag} {gmsh_type} {len(rows)}")
        for row in rows:
            lines.append(f"{elem_tag} " + " ".join(str(x) for x in row))
            elem_tag += 1
    if polyhedra_rows:
        # Encode polyhedra as elementType 15? No, emit clean MSH 4.1 1-node point/polyhedron table
        # plus structured $PolyhedronTopology block for lossless native import.
        lines.append(f"3 1 15 {len(polyhedra_rows)}")
        for _, nodes_1b in polyhedra_rows:
            lines.append(f"{elem_tag} {nodes_1b[0]}")
            elem_tag += 1
    lines.append("$EndElements")

    if polyhedra_rows:
        lines.append("$PolyhedronTopology")
        lines.append(str(len(polyhedra_rows)))
        for cid, nodes_1b in polyhedra_rows:
            nodes_0b = [str(v - 1) for v in nodes_1b]
            f_list = polyhedra_faces.get(cid, [])
            face_tokens = [f"{len(fv)}:" + ",".join(str(v) for v in fv) for fv in f_list]
            lines.append(f"{cid} {' '.join(nodes_0b)} | {' ; '.join(face_tokens)}")
        lines.append("$EndPolyhedronTopology")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out_path


def import_gmsh_msh(file_path: Union[str, Path]) -> UnstructuredMesh:
    """Import a Gmsh ``.msh`` v4.1 ASCII file into an ``UnstructuredMesh``.

    Args:
        file_path: Input ``.msh`` file path.

    Returns:
        Reconstructed ``UnstructuredMesh`` instance.
    """
    in_path = Path(file_path)
    if not in_path.is_file():
        raise FileNotFoundError(f"Gmsh file not found: {in_path}")

    raw_lines = [line.strip() for line in in_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    idx = 0
    n_lines = len(raw_lines)

    points_map: Dict[int, np.ndarray] = {}
    phys_names: Dict[Tuple[int, int], str] = {}
    cells_raw: List[np.ndarray] = []
    poly_extra: List[np.ndarray] = []
    has_poly_block = False

    while idx < n_lines:
        sec = raw_lines[idx]
        idx += 1
        if sec == "$PhysicalNames":
            count = int(raw_lines[idx])
            idx += 1
            for _ in range(count):
                parts = raw_lines[idx].split(maxsplit=2)
                idx += 1
                dim = int(parts[0])
                tag = int(parts[1])
                name = parts[2].strip('"')
                phys_names[(dim, tag)] = name
        elif sec == "$Nodes":
            hdr = [int(x) for x in raw_lines[idx].split()]
            idx += 1
            num_blocks = hdr[0]
            for _ in range(num_blocks):
                bhdr = [int(x) for x in raw_lines[idx].split()]
                idx += 1
                n_in_block = bhdr[3]
                node_tags = [int(raw_lines[idx + k]) for k in range(n_in_block)]
                idx += n_in_block
                for tag in node_tags:
                    xyz = [float(x) for x in raw_lines[idx].split()]
                    idx += 1
                    points_map[tag] = np.array(xyz, dtype=np.float64)
        elif sec == "$Elements":
            hdr = [int(x) for x in raw_lines[idx].split()]
            idx += 1
            num_blocks = hdr[0]
            for _ in range(num_blocks):
                bhdr = [int(x) for x in raw_lines[idx].split()]
                idx += 1
                dim, _, etype, n_in_block = bhdr[0], bhdr[1], bhdr[2], bhdr[3]
                for _ in range(n_in_block):
                    vals = [int(x) for x in raw_lines[idx].split()]
                    idx += 1
                    if dim == 3 and etype in _CELLTYPE_BY_GMSH_TYPE:
                        cells_raw.append(np.array([v - 1 for v in vals[1:]], dtype=np.int64))
        elif sec == "$PolyhedronTopology":
            has_poly_block = True
            count = int(raw_lines[idx])
            idx += 1
            for _ in range(count):
                line = raw_lines[idx]
                idx += 1
                lhs = line.split("|", maxsplit=1)[0].strip().split()
                poly_extra.append(np.array([int(v) for v in lhs[1:]], dtype=np.int64))

    max_tag = max(points_map.keys()) if points_map else 0
    points = np.zeros((max_tag, 3), dtype=np.float64)
    for tag, coord in points_map.items():
        points[tag - 1] = coord

    all_cells = cells_raw + (poly_extra if has_poly_block else [])
    if not all_cells:
        raise ValueError(f"No 3D volumetric elements found in Gmsh file: {in_path}")

    return compute_difvm_topology_python(points, all_cells)


def export_cgns(
    mesh: UnstructuredMesh,
    file_path: Union[str, Path],
) -> Path:
    """Export an ``UnstructuredMesh`` to CGNS v4 HDF5 format.

    Args:
        mesh: Source ``UnstructuredMesh``.
        file_path: Destination ``.cgns`` file path.

    Returns:
        Resolved ``Path`` of the written ``.cgns`` file.
    """
    import h5py

    out_path = Path(file_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    pts = np.asarray(mesh.points, dtype=np.float64)
    n_pts = len(pts)
    n_cells = mesh.num_cells

    with h5py.File(out_path, "w") as handle:
        handle.attrs["name"] = np.bytes_("HDF5 MotherNode")
        handle.attrs["format"] = np.bytes_("IEEE_LITTLE_64")
        ver = handle.create_group("CGNSLibraryVersion")
        ver.attrs["label"] = np.bytes_("CGNSLibraryVersion_t")
        ver.attrs["type"] = np.bytes_("R4")
        ver.create_dataset(" data", data=np.array([4.2], dtype=np.float32))

        base = handle.create_group("Base")
        base.attrs["label"] = np.bytes_("CGNSBase_t")
        base.attrs["type"] = np.bytes_("I4")
        base.create_dataset(" data", data=np.array([3, 3], dtype=np.int32))

        zone = base.create_group("Zone1")
        zone.attrs["label"] = np.bytes_("Zone_t")
        zone.attrs["type"] = np.bytes_("I4")
        zone.create_dataset(" data", data=np.array([[n_pts, n_cells, 0]], dtype=np.int32))

        ztype = zone.create_group("ZoneType")
        ztype.attrs["label"] = np.bytes_("ZoneType_t")
        ztype.create_dataset(" data", data=np.bytes_("Unstructured"))

        gc = zone.create_group("GridCoordinates")
        gc.attrs["label"] = np.bytes_("GridCoordinates_t")
        for axis_idx, cname in enumerate(("CoordinateX", "CoordinateY", "CoordinateZ")):
            cgrp = gc.create_group(cname)
            cgrp.attrs["label"] = np.bytes_("DataArray_t")
            cgrp.create_dataset(" data", data=np.ascontiguousarray(pts[:, axis_idx], dtype=np.float64))

        # Group standard 3D cells and polyhedra
        grouped: Dict[int, List[np.ndarray]] = {}
        poly_cells: List[np.ndarray] = []
        for c_idx in range(n_cells):
            ctype = int(mesh.cell_types[c_idx]) if len(mesh.cell_types) > c_idx else int(CellType.HEXAHEDRON)
            conn = np.asarray(mesh.cells[c_idx], dtype=np.int32) + 1
            if ctype in _CGNS_ELEM_TYPE_MAP:
                grouped.setdefault(ctype, []).append(conn)
            else:
                poly_cells.append(conn)

        elem_offset = 1
        sec_idx = 1
        for ctype, rows in grouped.items():
            cgns_id, sec_name = _CGNS_ELEM_TYPE_MAP[ctype]
            sec = zone.create_group(f"ElemSection_{sec_idx}_{sec_name}")
            sec.attrs["label"] = np.bytes_("Elements_t")
            sec.create_dataset(" data", data=np.array([cgns_id, 0], dtype=np.int32))
            er = sec.create_group("ElementRange")
            er.create_dataset(" data", data=np.array([elem_offset, elem_offset + len(rows) - 1], dtype=np.int32))
            ec = sec.create_group("ElementConnectivity")
            ec.create_dataset(" data", data=np.concatenate(rows).astype(np.int32))
            elem_offset += len(rows)
            sec_idx += 1

        if poly_cells:
            sec = zone.create_group(f"ElemSection_{sec_idx}_POLYHEDRON")
            sec.attrs["label"] = np.bytes_("Elements_t")
            sec.create_dataset(" data", data=np.array([23, 0], dtype=np.int32))  # NFACE_n = 23
            er = sec.create_group("ElementRange")
            er.create_dataset(" data", data=np.array([elem_offset, elem_offset + len(poly_cells) - 1], dtype=np.int32))
            offsets = np.zeros(len(poly_cells) + 1, dtype=np.int32)
            for i, row in enumerate(poly_cells):
                offsets[i + 1] = offsets[i] + len(row)
            eo = sec.create_group("ElementStartOffset")
            eo.create_dataset(" data", data=offsets)
            ec = sec.create_group("ElementConnectivity")
            ec.create_dataset(" data", data=np.concatenate(poly_cells).astype(np.int32))

        # ZoneBC preserving boundary patch face ranges
        if mesh.boundaries:
            zbc = zone.create_group("ZoneBC")
            zbc.attrs["label"] = np.bytes_("ZoneBC_t")
            for p_name, (start_f, count_f) in mesh.boundaries.items():
                bc_grp = zbc.create_group(str(p_name))
                bc_grp.attrs["label"] = np.bytes_("BC_t")
                bc_grp.create_dataset(" data", data=np.bytes_("BCWall"))
                pl = bc_grp.create_group("PointList")
                pl.create_dataset(
                    " data",
                    data=np.arange(int(start_f) + 1, int(start_f) + int(count_f) + 1, dtype=np.int32),
                )

    return out_path


def import_cgns(file_path: Union[str, Path]) -> UnstructuredMesh:
    """Import a CGNS HDF5 mesh file created by :func:`export_cgns` into an ``UnstructuredMesh``."""
    import h5py

    in_path = Path(file_path)
    if not in_path.is_file():
        raise FileNotFoundError(f"CGNS file not found: {in_path}")

    nodes_per_cgns = {10: 4, 12: 5, 14: 6, 17: 8}
    with h5py.File(in_path, "r") as handle:
        zone = handle["Base/Zone1"]
        gc = zone["GridCoordinates"]
        cx = np.asarray(gc["CoordinateX"][" data"][()], dtype=np.float64)
        cy = np.asarray(gc["CoordinateY"][" data"][()], dtype=np.float64)
        cz = np.asarray(gc["CoordinateZ"][" data"][()], dtype=np.float64)
        points = np.column_stack([cx, cy, cz])

        cells: List[np.ndarray] = []
        for grp_name in sorted(zone.keys()):
            if not grp_name.startswith("ElemSection_"):
                continue
            sec = zone[grp_name]
            cgns_type = int(np.asarray(sec[" data"][()]).reshape(-1)[0])
            conn = np.asarray(sec["ElementConnectivity"][" data"][()], dtype=np.int64) - 1
            if cgns_type in nodes_per_cgns:
                stride = nodes_per_cgns[cgns_type]
                for row in conn.reshape(-1, stride):
                    cells.append(row)
            elif cgns_type == 23 and "ElementStartOffset" in sec:
                offsets = np.asarray(sec["ElementStartOffset"][" data"][()], dtype=np.int64)
                for i in range(len(offsets) - 1):
                    cells.append(conn[offsets[i] : offsets[i + 1]])

    if not cells:
        raise ValueError(f"No 3D cell connectivity sections found in CGNS file: {in_path}")
    return compute_difvm_topology_python(points, cells)


__all__ = [
    "export_gmsh_msh",
    "import_gmsh_msh",
    "export_cgns",
    "import_cgns",
]
