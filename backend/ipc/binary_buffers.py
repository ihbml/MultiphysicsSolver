"""binary_buffers.py
=================
Flat binary buffer export contracts for:
- Paket 1: Binary Geometry Buffer (vertices, normals, triangles, face groups, audit JSON).
- Paket 2: Binary Mesh Quality Fields (skewness, non-orthogonality, aspect ratio, volume, wsrd weights, amips energy, folded faces).
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

# Header magic constants
MAGIC_PAKET1_GEOMETRY = b"CFDGEO01"      # 8 bytes
MAGIC_PAKET2_MESH_QUALITY = b"CFDMSH02"  # 8 bytes
HEADER_SIZE = 32                         # 32-byte alignment


@dataclass
class GeometryBufferDescriptor:
    """Zero-copy buffer layout metadata for Paket 1 CAD/Surface Geometry."""
    dataset_id: str
    geometry_revision: int
    n_vertices: int
    n_triangles: int
    coordinate_system: str = "right_handed_z_up"
    length_unit: str = "m"
    endianness: str = "little_endian"
    offsets: Dict[str, int] = field(default_factory=dict)
    sizes: Dict[str, int] = field(default_factory=dict)
    dtypes: Dict[str, str] = field(default_factory=lambda: {
        "vertices": "float64",
        "normals": "float32",
        "triangles": "uint32",
        "face_group_id": "uint32",
    })

    def to_dict(self) -> Dict[str, Any]:
        return {
            "dataset_id": self.dataset_id,
            "geometry_revision": self.geometry_revision,
            "n_vertices": self.n_vertices,
            "n_triangles": self.n_triangles,
            "coordinate_system": self.coordinate_system,
            "length_unit": self.length_unit,
            "endianness": self.endianness,
            "offsets": self.offsets,
            "sizes": self.sizes,
            "dtypes": self.dtypes,
        }


def pack_geometry_buffer(
    vertices: np.ndarray,
    normals: np.ndarray,
    triangles: np.ndarray,
    face_group_id: Optional[np.ndarray] = None,
    audit_report: Optional[Dict[str, Any]] = None,
    dataset_id: str = "geo_001",
    geometry_revision: int = 1,
) -> Tuple[bytes, GeometryBufferDescriptor]:
    """Packs triangulated CAD surface geometry into contiguous flat binary buffer.

    Layout:
    1. 32-byte header:
       - magic: b"CFDGEO01" (8B)
       - version: uint32 (4B)
       - n_vertices: uint32 (4B)
       - n_triangles: uint32 (4B)
       - audit_json_len: uint32 (4B)
       - reserved: 8 bytes zero padding
    2. vertices: (N_v, 3) float64 (little-endian) -> 24 bytes per vertex
    3. normals: (N_v, 3) float32 (little-endian) -> 12 bytes per vertex
    4. triangles: (N_t, 3) uint32 (little-endian) -> 12 bytes per triangle
    5. face_group_id: (N_t,) uint32 (little-endian) -> 4 bytes per triangle
    6. audit_json: UTF-8 encoded audit JSON metadata
    """
    v_arr = np.ascontiguousarray(vertices, dtype=np.float64)
    if v_arr.ndim != 2 or v_arr.shape[1] != 3:
        raise ValueError(f"Vertices must have shape (N, 3), got {v_arr.shape}")
    n_v = len(v_arr)

    # Normals: ensure (N_v, 3) float32 (vertex normals)
    n_arr = np.ascontiguousarray(normals, dtype=np.float32)
    if n_arr.shape != (n_v, 3):
        if len(n_arr) == len(triangles):
            # Face normals provided -> compute per-vertex normals via accumulator
            t_idx = np.ascontiguousarray(triangles, dtype=np.uint32)
            v_norm = np.zeros((n_v, 3), dtype=np.float32)
            for f in range(len(t_idx)):
                fn = n_arr[f]
                for node in t_idx[f]:
                    v_norm[node] += fn
            lens = np.linalg.norm(v_norm, axis=1, keepdims=True)
            lens[lens < 1e-12] = 1.0
            n_arr = np.ascontiguousarray(v_norm / lens, dtype=np.float32)
        else:
            raise ValueError(f"Normals shape {n_arr.shape} does not match vertex count {n_v}")

    t_arr = np.ascontiguousarray(triangles, dtype=np.uint32)
    if t_arr.ndim != 2 or t_arr.shape[1] != 3:
        raise ValueError(f"Triangles must have shape (N, 3), got {t_arr.shape}")
    n_t = len(t_arr)

    if face_group_id is not None:
        fg_arr = np.ascontiguousarray(face_group_id, dtype=np.uint32)
        if len(fg_arr) != n_t:
            raise ValueError(f"face_group_id count {len(fg_arr)} != triangles count {n_t}")
    else:
        fg_arr = np.zeros(n_t, dtype=np.uint32)

    audit_dict = dict(audit_report) if audit_report is not None else {}
    audit_bytes = json.dumps(audit_dict).encode("utf-8")
    audit_len = len(audit_bytes)

    # Compute offsets
    offset_v = HEADER_SIZE
    size_v = n_v * 3 * 8
    offset_n = offset_v + size_v
    size_n = n_v * 3 * 4
    offset_t = offset_n + size_n
    size_t = n_t * 3 * 4
    offset_fg = offset_t + size_t
    size_fg = n_t * 4
    offset_audit = offset_fg + size_fg
    size_audit = audit_len

    # Pack 32-byte header: 8s, I, I, I, I, 8s
    header = struct.pack(
        "<8sIIII8s",
        MAGIC_PAKET1_GEOMETRY,
        1,          # version
        n_v,
        n_t,
        audit_len,
        b"\x00" * 8
    )

    buffer = (
        header +
        v_arr.tobytes() +
        n_arr.tobytes() +
        t_arr.tobytes() +
        fg_arr.tobytes() +
        audit_bytes
    )

    descriptor = GeometryBufferDescriptor(
        dataset_id=dataset_id,
        geometry_revision=geometry_revision,
        n_vertices=n_v,
        n_triangles=n_t,
        offsets={
            "vertices": offset_v,
            "normals": offset_n,
            "triangles": offset_t,
            "face_group_id": offset_fg,
            "audit_json": offset_audit,
        },
        sizes={
            "vertices": size_v,
            "normals": size_n,
            "triangles": size_t,
            "face_group_id": size_fg,
            "audit_json": size_audit,
        },
    )

    return buffer, descriptor


def unpack_geometry_buffer(buffer: bytes) -> Dict[str, Any]:
    """Unpacks a Paket 1 binary geometry buffer into NumPy arrays and audit dict."""
    if len(buffer) < HEADER_SIZE:
        raise ValueError(f"Buffer too short for geometry header: {len(buffer)} bytes")

    magic, version, n_v, n_t, audit_len, _ = struct.unpack("<8sIIII8s", buffer[:HEADER_SIZE])
    if magic != MAGIC_PAKET1_GEOMETRY:
        raise ValueError(f"Invalid geometry buffer magic: {magic!r}, expected {MAGIC_PAKET1_GEOMETRY!r}")

    offset = HEADER_SIZE
    # Vertices (n_v, 3) float64
    v_bytes = n_v * 3 * 8
    vertices = np.frombuffer(buffer[offset : offset + v_bytes], dtype=np.float64, count=n_v * 3).reshape(n_v, 3).copy()
    offset += v_bytes

    # Normals (n_v, 3) float32
    n_bytes = n_v * 3 * 4
    normals = np.frombuffer(buffer[offset : offset + n_bytes], dtype=np.float32, count=n_v * 3).reshape(n_v, 3).copy()
    offset += n_bytes

    # Triangles (n_t, 3) uint32
    t_bytes = n_t * 3 * 4
    triangles = np.frombuffer(buffer[offset : offset + t_bytes], dtype=np.uint32, count=n_t * 3).reshape(n_t, 3).copy()
    offset += t_bytes

    # face_group_id (n_t,) uint32
    fg_bytes = n_t * 4
    face_group_id = np.frombuffer(buffer[offset : offset + fg_bytes], dtype=np.uint32, count=n_t).copy()
    offset += fg_bytes

    # Audit JSON
    audit_json = {}
    if audit_len > 0:
        json_str = buffer[offset : offset + audit_len].decode("utf-8")
        audit_json = json.loads(json_str)

    return {
        "version": version,
        "n_vertices": n_v,
        "n_triangles": n_t,
        "vertices": vertices,
        "normals": normals,
        "triangles": triangles,
        "face_group_id": face_group_id,
        "audit_report": audit_json,
    }


@dataclass
class MeshQualityBufferDescriptor:
    """Zero-copy buffer layout metadata for Paket 2 Mesh Quality Fields."""
    mesh_id: str
    mesh_revision: int
    n_cells: int
    n_folded: int
    location: str = "cell"
    endianness: str = "little_endian"
    field_names: List[str] = field(default_factory=lambda: [
        "skewness",
        "non_orthogonality",
        "aspect_ratio",
        "volume",
        "wsrd_weights",
        "amips_energy",
    ])
    offsets: Dict[str, int] = field(default_factory=dict)
    sizes: Dict[str, int] = field(default_factory=dict)
    dtypes: Dict[str, str] = field(default_factory=lambda: {
        "fields": "float32",
        "folded_faces": "uint32",
    })

    def to_dict(self) -> Dict[str, Any]:
        return {
            "mesh_id": self.mesh_id,
            "mesh_revision": self.mesh_revision,
            "n_cells": self.n_cells,
            "n_folded": self.n_folded,
            "location": self.location,
            "endianness": self.endianness,
            "field_names": list(self.field_names),
            "offsets": self.offsets,
            "sizes": self.sizes,
            "dtypes": self.dtypes,
        }


def pack_mesh_quality_buffer(
    skewness: np.ndarray,
    non_orthogonality: np.ndarray,
    aspect_ratio: np.ndarray,
    volume: np.ndarray,
    wsrd_weights: Optional[np.ndarray] = None,
    amips_energy: Optional[np.ndarray] = None,
    folded_face_indices: Optional[np.ndarray] = None,
    summary: Optional[Dict[str, Any]] = None,
    mesh_id: str = "mesh_001",
    mesh_revision: int = 1,
) -> Tuple[bytes, MeshQualityBufferDescriptor]:
    """Packs cell-centered quality metrics and folded face indices into flat binary buffer.

    Metrics (6 fields, each [f32; N_cells]):
    1. skewness
    2. non_orthogonality [deg]
    3. aspect_ratio
    4. volume [m^3]
    5. wsrd_weights
    6. amips_energy
    Followed by folded_face_indices [u32; N_folded] and summary JSON.
    """
    s_arr = np.ascontiguousarray(skewness, dtype=np.float32)
    n_cells = len(s_arr)

    no_arr = np.ascontiguousarray(non_orthogonality, dtype=np.float32)
    ar_arr = np.ascontiguousarray(aspect_ratio, dtype=np.float32)
    vol_arr = np.ascontiguousarray(volume, dtype=np.float32)

    for name, arr in [("non_orthogonality", no_arr), ("aspect_ratio", ar_arr), ("volume", vol_arr)]:
        if len(arr) != n_cells:
            raise ValueError(f"Field '{name}' length {len(arr)} != n_cells {n_cells}")

    wsrd_arr = np.ascontiguousarray(wsrd_weights, dtype=np.float32) if wsrd_weights is not None else np.ones(n_cells, dtype=np.float32)
    amips_arr = np.ascontiguousarray(amips_energy, dtype=np.float32) if amips_energy is not None else np.ones(n_cells, dtype=np.float32)

    if folded_face_indices is not None:
        ff_arr = np.ascontiguousarray(folded_face_indices, dtype=np.uint32)
    else:
        ff_arr = np.empty(0, dtype=np.uint32)
    n_folded = len(ff_arr)

    summary_dict = dict(summary) if summary is not None else {
        "n_cells": n_cells,
        "max_skewness": float(np.max(s_arr)) if n_cells > 0 else 0.0,
        "max_non_ortho": float(np.max(no_arr)) if n_cells > 0 else 0.0,
        "min_volume": float(np.min(vol_arr)) if n_cells > 0 else 0.0,
    }
    sum_bytes = json.dumps(summary_dict).encode("utf-8")
    sum_len = len(sum_bytes)

    # Offsets
    field_size = n_cells * 4
    offset_skew = HEADER_SIZE
    offset_no = offset_skew + field_size
    offset_ar = offset_no + field_size
    offset_vol = offset_ar + field_size
    offset_wsrd = offset_vol + field_size
    offset_amips = offset_wsrd + field_size
    offset_ff = offset_amips + field_size
    size_ff = n_folded * 4
    offset_sum = offset_ff + size_ff
    size_sum = sum_len

    header = struct.pack(
        "<8sIIII8s",
        MAGIC_PAKET2_MESH_QUALITY,
        1,          # version
        n_cells,
        n_folded,
        sum_len,
        b"\x00" * 8
    )

    buffer = (
        header +
        s_arr.tobytes() +
        no_arr.tobytes() +
        ar_arr.tobytes() +
        vol_arr.tobytes() +
        wsrd_arr.tobytes() +
        amips_arr.tobytes() +
        ff_arr.tobytes() +
        sum_bytes
    )

    descriptor = MeshQualityBufferDescriptor(
        mesh_id=mesh_id,
        mesh_revision=mesh_revision,
        n_cells=n_cells,
        n_folded=n_folded,
        offsets={
            "skewness": offset_skew,
            "non_orthogonality": offset_no,
            "aspect_ratio": offset_ar,
            "volume": offset_vol,
            "wsrd_weights": offset_wsrd,
            "amips_energy": offset_amips,
            "folded_faces": offset_ff,
            "summary_json": offset_sum,
        },
        sizes={
            "skewness": field_size,
            "non_orthogonality": field_size,
            "aspect_ratio": field_size,
            "volume": field_size,
            "wsrd_weights": field_size,
            "amips_energy": field_size,
            "folded_faces": size_ff,
            "summary_json": size_sum,
        },
    )

    return buffer, descriptor


def unpack_mesh_quality_buffer(buffer: bytes) -> Dict[str, Any]:
    """Unpacks a Paket 2 binary mesh quality buffer into metric arrays and summary dict."""
    if len(buffer) < HEADER_SIZE:
        raise ValueError(f"Buffer too short for mesh quality header: {len(buffer)} bytes")

    magic, version, n_cells, n_folded, sum_len, _ = struct.unpack("<8sIIII8s", buffer[:HEADER_SIZE])
    if magic != MAGIC_PAKET2_MESH_QUALITY:
        raise ValueError(f"Invalid mesh quality buffer magic: {magic!r}, expected {MAGIC_PAKET2_MESH_QUALITY!r}")

    offset = HEADER_SIZE
    f_bytes = n_cells * 4

    skewness = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    non_ortho = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    aspect_ratio = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    volume = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    wsrd = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    amips = np.frombuffer(buffer[offset : offset + f_bytes], dtype=np.float32, count=n_cells).copy()
    offset += f_bytes

    ff_bytes = n_folded * 4
    folded_faces = np.frombuffer(buffer[offset : offset + ff_bytes], dtype=np.uint32, count=n_folded).copy()
    offset += ff_bytes

    summary = {}
    if sum_len > 0:
        sum_str = buffer[offset : offset + sum_len].decode("utf-8")
        summary = json.loads(sum_str)

    return {
        "version": version,
        "n_cells": n_cells,
        "n_folded": n_folded,
        "skewness": skewness,
        "non_orthogonality": non_ortho,
        "aspect_ratio": aspect_ratio,
        "volume": volume,
        "wsrd_weights": wsrd,
        "amips_energy": amips,
        "folded_face_indices": folded_faces,
        "summary": summary,
    }
