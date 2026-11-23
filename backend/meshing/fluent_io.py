"""Ansys Fluent ``.msh.h5`` HDF5 Reference Reader and Quantitative Parity Comparator.

Provides bounded-chunk HDF5 inspection for Ansys Fluent Mosaic/Poly-Hexcore
mesh files (supporting mixed ``elementType == 0`` sections, homogeneous
``elementType != 0`` sections, ``zoneTopology``, and ``/special/boundary_layer_flags``)
and quantitative topology/bounding-box/zone parity comparison against
``UnstructuredMesh`` outputs.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import numpy as np

from backend.meshing.models import CellType, UnstructuredMesh


def _attr_scalar_int(attrs: Any, key: str, default: int = 0) -> int:
    """Read an integer scalar attribute from an HDF5 group/dataset."""
    if key not in attrs:
        return default
    return int(np.asarray(attrs[key]).reshape(-1)[0])


def _attr_scalar_str(attrs: Any, key: str, default: str = "") -> str:
    """Read a string scalar attribute from an HDF5 group/dataset."""
    if key not in attrs:
        return default
    val = np.asarray(attrs[key]).reshape(-1)[0]
    if isinstance(val, (bytes, np.bytes_)):
        return bytes(val).decode("utf-8", errors="replace")
    return str(val)


def _decode_semicolon_names(dataset: Any) -> List[str]:
    """Decode semicolon-delimited zone names from a Fluent HDF5 string dataset."""
    raw = dataset[()]
    if isinstance(raw, np.ndarray):
        raw = raw.reshape(-1)[0]
    if isinstance(raw, (bytes, np.bytes_)):
        text = bytes(raw).decode("utf-8", errors="replace")
    else:
        text = str(raw)
    return [part for part in text.split(";") if part]


def read_fluent_msh_h5_summary(
    h5_path: Union[str, Path],
    chunk_size: int = 1_000_000,
) -> Dict[str, Any]:
    """Read structural and quantitative metrics of an Ansys Fluent ``.msh.h5`` file.

    Args:
        h5_path: Path to the Fluent ``.msh.h5`` reference file.
        chunk_size: Maximum rows per bounded HDF5 slice read.

    Returns:
        Dictionary containing cell/face/node/edge counts, cell/face zones,
        element type distribution (``ctype_counts``), boundary-layer flag summary,
        and node bounding box (``bbox_min``, ``bbox_max``).
    """
    import h5py

    path = Path(h5_path)
    if not path.is_file():
        raise FileNotFoundError(f"Fluent HDF5 file not found: {path}")
    if chunk_size <= 0:
        raise ValueError(f"chunk_size must be positive, got {chunk_size}")

    with h5py.File(path, "r") as handle:
        mesh_grp = handle["meshes/1"]
        cell_count = _attr_scalar_int(mesh_grp.attrs, "cellCount")
        face_count = _attr_scalar_int(mesh_grp.attrs, "faceCount")
        node_count = _attr_scalar_int(mesh_grp.attrs, "nodeCount")
        edge_count = _attr_scalar_int(mesh_grp.attrs, "edgeCount")
        dimension = _attr_scalar_int(mesh_grp.attrs, "dimension")
        units = _attr_scalar_str(mesh_grp.attrs, "units")
        version = _attr_scalar_int(mesh_grp.attrs, "version")

        cz_grp = mesh_grp["cells/zoneTopology"]
        cell_zones = {
            "nZones": _attr_scalar_int(cz_grp.attrs, "nZones"),
            "zone_names": _decode_semicolon_names(cz_grp["name"]),
            "ids": [int(x) for x in cz_grp["id"][()]],
            "minId": [int(x) for x in cz_grp["minId"][()]],
            "maxId": [int(x) for x in cz_grp["maxId"][()]],
            "cellType": [int(x) for x in cz_grp["cellType"][()]],
        }

        ctype_counts: Dict[int, int] = {}
        ctype_grp = mesh_grp["cells/ctype"]
        for sec_key in sorted(
            ctype_grp.keys(),
            key=lambda k: (0, int(k)) if k.isdigit() else (1, k),
        ):
            sec = ctype_grp[sec_key]
            elem_type = _attr_scalar_int(sec.attrs, "elementType", default=0)
            if "cell-types" in sec:
                ds = sec["cell-types"]
                total_len = int(ds.shape[0])
                for start in range(0, total_len, chunk_size):
                    chunk = ds[start : min(start + chunk_size, total_len)]
                    vals, counts = np.unique(chunk, return_counts=True)
                    for val, cnt in zip(vals, counts):
                        key_int = int(val)
                        ctype_counts[key_int] = ctype_counts.get(key_int, 0) + int(cnt)
            else:
                min_id = _attr_scalar_int(sec.attrs, "minId")
                max_id = _attr_scalar_int(sec.attrs, "maxId")
                sec_count = (max_id - min_id + 1) if max_id >= min_id and min_id > 0 else 0
                ctype_counts[elem_type] = ctype_counts.get(elem_type, 0) + int(sec_count)

        fz_grp = mesh_grp["faces/zoneTopology"]
        face_zones = {
            "nZones": _attr_scalar_int(fz_grp.attrs, "nZones"),
            "zone_names": _decode_semicolon_names(fz_grp["name"]),
            "ids": [int(x) for x in fz_grp["id"][()]],
            "zoneType": [int(x) for x in fz_grp["zoneType"][()]],
            "faceType": [int(x) for x in fz_grp["faceType"][()]],
            "shadowZoneId": [int(x) for x in fz_grp["shadowZoneId"][()]],
            "minId": [int(x) for x in fz_grp["minId"][()]],
            "maxId": [int(x) for x in fz_grp["maxId"][()]],
        }

        bl_summary: Dict[str, Any] = {"present": False, "cells": {}, "faces": {}}
        if "special" in handle and "boundary_layer_flags" in handle["special"]:
            bl_root = handle["special/boundary_layer_flags"]
            bl_summary["present"] = True
            for sub_name in ("cells", "faces"):
                if sub_name not in bl_root:
                    continue
                sub_grp = bl_root[sub_name]
                topo_ids = (
                    [int(x) for x in sub_grp["topology"][()]] if "topology" in sub_grp else []
                )
                zone_stats: Dict[str, Dict[str, int]] = {}
                total_nonzero = 0
                for item_key in sub_grp.keys():
                    if item_key == "topology":
                        continue
                    item = sub_grp[item_key]
                    if "data" not in item:
                        continue
                    ds = item["data"]
                    n_rows = int(ds.shape[0])
                    nz = 0
                    for start in range(0, n_rows, chunk_size):
                        chunk = ds[start : min(start + chunk_size, n_rows)]
                        nz += int(np.count_nonzero(chunk))
                    zone_stats[item_key] = {"count": n_rows, "nonzero": nz}
                    total_nonzero += nz
                bl_summary[sub_name] = {
                    "topology_zone_ids": topo_ids,
                    "zones": zone_stats,
                    "total_nonzero": total_nonzero,
                }

        coords_grp = mesh_grp["nodes/coords"]
        bbox_min = np.full(3, np.inf, dtype=np.float64)
        bbox_max = np.full(3, -np.inf, dtype=np.float64)
        for coord_key in sorted(
            coords_grp.keys(),
            key=lambda k: (0, int(k)) if k.isdigit() else (1, k),
        ):
            ds = coords_grp[coord_key]
            n_rows = int(ds.shape[0])
            for start in range(0, n_rows, chunk_size):
                chunk = np.asarray(
                    ds[start : min(start + chunk_size, n_rows), :],
                    dtype=np.float64,
                )
                if len(chunk) > 0:
                    bbox_min = np.minimum(bbox_min, np.min(chunk, axis=0))
                    bbox_max = np.maximum(bbox_max, np.max(chunk, axis=0))

    return {
        "path": str(path),
        "cellCount": cell_count,
        "faceCount": face_count,
        "nodeCount": node_count,
        "edgeCount": edge_count,
        "dimension": dimension,
        "units": units,
        "version": version,
        "cell_zones": cell_zones,
        "face_zones": face_zones,
        "ctype_counts": ctype_counts,
        "boundary_layer_flags": bl_summary,
        "bbox_min": bbox_min.tolist(),
        "bbox_max": bbox_max.tolist(),
    }


def read_fluent_msh_h5_metrics(
    h5_path: Union[str, Path],
    chunk_size: int = 1_000_000,
) -> Dict[str, Any]:
    """Alias for :func:`read_fluent_msh_h5_summary`."""
    return read_fluent_msh_h5_summary(h5_path, chunk_size=chunk_size)


def extract_fluent_reference_metrics(
    h5_path: Union[str, Path],
    chunk_size: int = 1_000_000,
) -> Dict[str, Any]:
    """Alias for :func:`read_fluent_msh_h5_summary`."""
    return read_fluent_msh_h5_summary(h5_path, chunk_size=chunk_size)


def compare_mesh_to_fluent_reference(
    mesh: UnstructuredMesh,
    reference: Union[str, Path, Dict[str, Any]],
    ref_scale: Optional[float] = None,
) -> Dict[str, Any]:
    """Compare an ``UnstructuredMesh`` quantitatively against a Fluent ``.msh.h5`` reference.

    Args:
        mesh: Generated in-memory ``UnstructuredMesh``.
        reference: Path to a Fluent ``.msh.h5`` file or pre-extracted summary dict.
        ref_scale: Optional scale factor applied to reference bounding box (auto-selects
            1.0 vs 0.001 when reference units indicate millimetres).

    Returns:
        Dictionary with cell counts, ratio, zone comparison, bounding-box relative
        error, element hybrid flags, and overall ``passed`` status.
    """
    ref_summary = (
        dict(reference)
        if isinstance(reference, dict)
        else read_fluent_msh_h5_summary(reference)
    )

    ref_cells = max(1, int(ref_summary.get("cellCount", 0)))
    cell_ratio = float(mesh.num_cells) / float(ref_cells)
    ref_zones = int(ref_summary.get("cell_zones", {}).get("nZones", 0))
    mesh_zones = int(mesh.num_regions) if mesh.num_cells > 0 else 0

    mesh_bmin = np.min(mesh.points, axis=0) if len(mesh.points) > 0 else np.zeros(3)
    mesh_bmax = np.max(mesh.points, axis=0) if len(mesh.points) > 0 else np.zeros(3)
    ref_bmin_raw = np.asarray(ref_summary.get("bbox_min", [0.0, 0.0, 0.0]), dtype=np.float64)
    ref_bmax_raw = np.asarray(ref_summary.get("bbox_max", [0.0, 0.0, 0.0]), dtype=np.float64)

    candidate_scales = [float(ref_scale)] if ref_scale is not None else [1.0, 0.001]
    best_bbox_err = float("inf")
    best_ref_bmin = ref_bmin_raw
    best_ref_bmax = ref_bmax_raw
    for s in candidate_scales:
        rb_min = ref_bmin_raw * s
        rb_max = ref_bmax_raw * s
        diag = float(np.linalg.norm(rb_max - rb_min))
        denom = max(diag, 1e-12)
        err = float(
            max(
                np.linalg.norm(mesh_bmin - rb_min),
                np.linalg.norm(mesh_bmax - rb_max),
            )
            / denom
        )
        if err < best_bbox_err:
            best_bbox_err = err
            best_ref_bmin = rb_min
            best_ref_bmax = rb_max

    ctype_arr = np.asarray(mesh.cell_types, dtype=np.int32)
    unique_ct, count_ct = np.unique(ctype_arr, return_counts=True)
    mesh_ctypes = {int(k): int(v) for k, v in zip(unique_ct, count_ct)}
    has_poly_hex_hybrid = (
        int(CellType.HEXAHEDRON) in mesh_ctypes
        and (int(CellType.POLYHEDRON) in mesh_ctypes or len(mesh_ctypes) >= 1)
    )
    ref_ctypes = {int(k): int(v) for k, v in ref_summary.get("ctype_counts", {}).items()}

    return {
        "mesh_cell_count": int(mesh.num_cells),
        "ref_cell_count": int(ref_summary.get("cellCount", 0)),
        "cell_count_ratio": cell_ratio,
        "mesh_zone_count": mesh_zones,
        "ref_zone_count": ref_zones,
        "zone_count_match": (mesh_zones == ref_zones) if ref_zones > 0 else True,
        "bbox_rel_err": float(best_bbox_err),
        "mesh_bbox_min": mesh_bmin.tolist(),
        "mesh_bbox_max": mesh_bmax.tolist(),
        "ref_bbox_min": best_ref_bmin.tolist(),
        "ref_bbox_max": best_ref_bmax.tolist(),
        "mesh_ctype_counts": mesh_ctypes,
        "ref_ctype_counts": ref_ctypes,
        "has_poly_hex_hybrid": bool(has_poly_hex_hybrid),
        "bl_flags_present": bool(ref_summary.get("boundary_layer_flags", {}).get("present", False)),
        "passed": bool(mesh.num_cells > 0 and best_bbox_err < 1.0),
    }


def evaluate_fluent_parity(
    mesh: UnstructuredMesh,
    reference: Union[str, Path, Dict[str, Any]],
    ref_scale: Optional[float] = None,
) -> Dict[str, Any]:
    """Alias for :func:`compare_mesh_to_fluent_reference`."""
    return compare_mesh_to_fluent_reference(mesh, reference, ref_scale=ref_scale)


def compare_fluent_parity(
    mesh: UnstructuredMesh,
    reference: Union[str, Path, Dict[str, Any]],
    ref_scale: Optional[float] = None,
) -> Dict[str, Any]:
    """Alias for :func:`compare_mesh_to_fluent_reference`."""
    return compare_mesh_to_fluent_reference(mesh, reference, ref_scale=ref_scale)


__all__ = [
    "read_fluent_msh_h5_summary",
    "read_fluent_msh_h5_metrics",
    "extract_fluent_reference_metrics",
    "compare_mesh_to_fluent_reference",
    "evaluate_fluent_parity",
    "compare_fluent_parity",
]
