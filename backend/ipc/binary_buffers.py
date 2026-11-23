"""binary_buffers.py
=================
Flat binary buffer export contracts for:
- Paket 1: Binary Geometry Buffer (vertices, normals, triangles, face groups, audit JSON).
- Paket 2: Binary Mesh Quality Fields (skewness, non-orthogonality, aspect ratio, volume, wsrd weights, amips energy, folded faces).
"""

from __future__ import annotations

import json
import struct
import zlib
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union
import numpy as np

# Header magic constants
MAGIC_PAKET1_GEOMETRY = b"CFDGEO01"      # 8 bytes
MAGIC_PAKET2_MESH_QUALITY = b"CFDMSH02"  # 8 bytes
MAGIC_PAKET3_FIELD = b"CFDFLD03"         # 8 bytes
HEADER_SIZE = 32                         # 32-byte alignment
FIELD_HEADER_SIZE = 64                   # 64-byte Paket 3 header
CHUNK_ENTRY_SIZE = 32                    # 32-byte Hilbert chunk entry


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

    @property
    def vertex_count(self) -> int:
        return self.n_vertices

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

    @property
    def vertex_count(self) -> int:
        return self.n_vertices

    @property
    def cell_count(self) -> int:
        return self.n_cells

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


_FIELD_HEADER_FMT = "<8sIIIIId7bBBBII10s"
_CHUNK_ENTRY_FMT = "<QIIIIff"


@dataclass
class FieldBufferDescriptor:
    """Zero-copy buffer layout metadata for Paket 3 field streaming (``CFDFLD03``)."""

    field_name: str
    mesh_revision: int
    step_index: int
    sim_time: float
    n_values: int
    n_chunks: int
    dimensions: List[int]
    chunk_size: int = 65536
    location: int = 0
    value_source: int = 0
    compression: int = 0
    offsets: Dict[str, int] = field(default_factory=dict)
    sizes: Dict[str, int] = field(default_factory=dict)
    shape: Tuple[int, ...] = ()

    @property
    def cell_count(self) -> int:
        return self.n_values

    def to_dict(self) -> Dict[str, Any]:
        return {
            "magic": MAGIC_PAKET3_FIELD.decode("ascii"),
            "field_name": self.field_name,
            "mesh_revision": self.mesh_revision,
            "step_index": self.step_index,
            "sim_time": self.sim_time,
            "n_values": self.n_values,
            "n_chunks": self.n_chunks,
            "dimensions": list(self.dimensions),
            "chunk_size": self.chunk_size,
            "location": self.location,
            "value_source": self.value_source,
            "compression": self.compression,
            "offsets": dict(self.offsets),
            "sizes": dict(self.sizes),
            "shape": list(self.shape),
        }


def pack_field_buffer(
    values: np.ndarray,
    *,
    mesh_revision: int = 1,
    step_index: int = 0,
    sim_time: float = 0.0,
    dimensions: Optional[Sequence[int]] = None,
    field_name: str = "phi",
    chunk_size: int = 65536,
    location: int = 0,
    value_source: int = 0,
    compression: int = 0,
    metadata: Optional[Dict[str, Any]] = None,
) -> Tuple[bytes, FieldBufferDescriptor]:
    """Pack a simulation field array into a Paket 3 ``CFDFLD03`` binary buffer.

    Header layout (64 bytes, little-endian):
    - ``magic`` [8B]: ``b"CFDFLD03"``
    - ``version`` [u32] = 1, ``mesh_revision`` [u32], ``step_index`` [u32],
      ``n_values`` [u32], ``n_chunks`` [u32]
    - ``sim_time`` [f64]
    - ``si_dims`` [7 x i8]
    - ``location`` [u8], ``value_source`` [u8], ``compression`` [u8]
    - ``payload_crc32`` [u32], ``meta_len`` [u32]
    - ``reserved`` [10B]
    """
    from backend.ipc.dimensions import validate_si_dimensions

    arr = np.ascontiguousarray(values, dtype=np.float32)
    orig_shape = tuple(int(s) for s in arr.shape)
    flat = arr.ravel()
    n_values = int(flat.size)
    if chunk_size <= 0:
        raise ValueError(f"chunk_size must be positive, got {chunk_size}")

    dims = validate_si_dimensions(list(dimensions) if dimensions is not None else [0, 0, 0, 0, 0, 0, 0])
    if any(d < -128 or d > 127 for d in dims):
        raise ValueError(f"SI dimensions must fit in int8 range [-128, 127], got {dims}")

    n_chunks = max(1, (n_values + chunk_size - 1) // chunk_size) if n_values > 0 else 0
    payload_bytes = flat.tobytes()
    payload_crc = zlib.crc32(payload_bytes) & 0xFFFFFFFF

    meta_dict = dict(metadata) if metadata is not None else {}
    meta_dict.setdefault("field_name", field_name)
    meta_dict.setdefault("shape", list(orig_shape))
    meta_bytes = json.dumps(meta_dict).encode("utf-8")
    meta_len = len(meta_bytes)

    chunk_table_offset = FIELD_HEADER_SIZE
    chunk_table_size = n_chunks * CHUNK_ENTRY_SIZE
    payload_offset = chunk_table_offset + chunk_table_size
    meta_offset = payload_offset + len(payload_bytes)

    chunk_entries = bytearray()
    for c_idx in range(n_chunks):
        start_el = c_idx * chunk_size
        end_el = min((c_idx + 1) * chunk_size, n_values)
        slice_arr = flat[start_el:end_el]
        min_v = float(np.min(slice_arr)) if slice_arr.size > 0 else 0.0
        max_v = float(np.max(slice_arr)) if slice_arr.size > 0 else 0.0
        byte_off = start_el * 4
        byte_len = (end_el - start_el) * 4
        chunk_entries.extend(
            struct.pack(
                _CHUNK_ENTRY_FMT,
                c_idx * chunk_size,
                start_el,
                end_el - start_el,
                byte_off,
                byte_len,
                min_v,
                max_v,
            )
        )

    header = struct.pack(
        _FIELD_HEADER_FMT,
        MAGIC_PAKET3_FIELD,
        1,
        int(mesh_revision),
        int(step_index),
        n_values,
        n_chunks,
        float(sim_time),
        dims[0], dims[1], dims[2], dims[3], dims[4], dims[5], dims[6],
        int(location) & 0xFF,
        int(value_source) & 0xFF,
        int(compression) & 0xFF,
        payload_crc,
        meta_len,
        b"\x00" * 10,
    )

    buffer = header + bytes(chunk_entries) + payload_bytes + meta_bytes
    descriptor = FieldBufferDescriptor(
        field_name=field_name,
        mesh_revision=int(mesh_revision),
        step_index=int(step_index),
        sim_time=float(sim_time),
        n_values=n_values,
        n_chunks=n_chunks,
        dimensions=dims,
        chunk_size=int(chunk_size),
        location=int(location),
        value_source=int(value_source),
        compression=int(compression),
        offsets={
            "chunk_table": chunk_table_offset,
            "payload": payload_offset,
            "metadata_json": meta_offset,
        },
        sizes={
            "chunk_table": chunk_table_size,
            "payload": len(payload_bytes),
            "metadata_json": meta_len,
        },
        shape=orig_shape,
    )
    return buffer, descriptor


def unpack_field_buffer(buffer: bytes) -> Dict[str, Any]:
    """Unpack a Paket 3 ``CFDFLD03`` binary buffer with CRC32 verification."""
    if len(buffer) < FIELD_HEADER_SIZE:
        raise ValueError(f"Buffer too short for CFDFLD03 header: {len(buffer)} bytes")

    unpacked = struct.unpack(_FIELD_HEADER_FMT, buffer[:FIELD_HEADER_SIZE])
    magic = unpacked[0]
    if magic != MAGIC_PAKET3_FIELD:
        raise ValueError(f"Invalid Paket 3 magic: {magic!r}, expected {MAGIC_PAKET3_FIELD!r}")

    version, mesh_revision, step_index, n_values, n_chunks = unpacked[1:6]
    sim_time = float(unpacked[6])
    dims = list(unpacked[7:14])
    location, value_source, compression = int(unpacked[14]), int(unpacked[15]), int(unpacked[16])
    expected_crc, meta_len = int(unpacked[17]), int(unpacked[18])

    offset = FIELD_HEADER_SIZE
    chunks: List[Dict[str, Any]] = []
    chunk_table_bytes = n_chunks * CHUNK_ENTRY_SIZE
    if len(buffer) < offset + chunk_table_bytes + n_values * 4 + meta_len:
        raise ValueError("Truncated CFDFLD03 payload or chunk table.")

    for _ in range(n_chunks):
        h_start, c_off, c_cnt, b_off, b_len, min_v, max_v = struct.unpack(
            _CHUNK_ENTRY_FMT, buffer[offset : offset + CHUNK_ENTRY_SIZE]
        )
        chunks.append(
            {
                "hilbert_start": int(h_start),
                "cell_offset": int(c_off),
                "cell_count": int(c_cnt),
                "byte_offset": int(b_off),
                "byte_length": int(b_len),
                "min_val": float(min_v),
                "max_val": float(max_v),
            }
        )
        offset += CHUNK_ENTRY_SIZE

    payload_bytes_len = n_values * 4
    payload_slice = buffer[offset : offset + payload_bytes_len]
    actual_crc = zlib.crc32(payload_slice) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise ValueError(
            f"CFDFLD03 CRC32 mismatch: expected 0x{expected_crc:08x}, got 0x{actual_crc:08x}"
        )

    values = np.frombuffer(payload_slice, dtype=np.float32, count=n_values).copy()
    offset += payload_bytes_len
    meta: Dict[str, Any] = {}
    if meta_len > 0:
        meta = json.loads(buffer[offset : offset + meta_len].decode("utf-8"))
        shape = meta.get("shape")
        if isinstance(shape, list) and shape:
            values = values.reshape(tuple(int(s) for s in shape))

    return {
        "version": int(version),
        "mesh_revision": int(mesh_revision),
        "step_index": int(step_index),
        "sim_time": sim_time,
        "dimensions": dims,
        "n_values": int(n_values),
        "n_chunks": int(n_chunks),
        "location": location,
        "value_source": value_source,
        "compression": compression,
        "chunks": chunks,
        "values": values,
        "metadata": meta,
    }


# ==============================================================================
# §13.3 The 5 In-Situ Post-Processing Kernels
# ==============================================================================


def slice_plane_polyhedral(
    points: np.ndarray,
    faces: Sequence[Sequence[int]],
    plane_origin: Sequence[float],
    plane_normal: Sequence[float],
    cell_values: Optional[np.ndarray] = None,
    chunk_aabbs: Optional[Sequence[Tuple[Sequence[float], Sequence[float]]]] = None,
) -> Dict[str, Any]:
    """Kernel 1: Exact polyhedral plane slicer with optional Hilbert chunk AABB culling."""
    pts = np.asarray(points, dtype=np.float64)
    origin = np.asarray(plane_origin, dtype=np.float64)
    normal = np.asarray(plane_normal, dtype=np.float64)
    n_norm = float(np.linalg.norm(normal))
    if n_norm <= 1e-15:
        raise ValueError("plane_normal must be non-zero")
    normal = normal / n_norm

    culled_chunks = 0
    if chunk_aabbs is not None:
        for bmin, bmax in chunk_aabbs:
            bmin_a = np.asarray(bmin, dtype=np.float64)
            bmax_a = np.asarray(bmax, dtype=np.float64)
            center = 0.5 * (bmin_a + bmax_a)
            half = 0.5 * (bmax_a - bmin_a)
            r_eff = float(np.sum(half * np.abs(normal)))
            if abs(float(np.dot(center - origin, normal))) > r_eff:
                culled_chunks += 1

    dist = (pts - origin[None, :]) @ normal
    slice_points: List[np.ndarray] = []
    segments: List[Tuple[int, int]] = []
    edge_cache: Dict[Tuple[int, int], int] = {}

    for face in faces:
        nv = len(face)
        if nv < 3:
            continue
        crossings: List[int] = []
        for k in range(nv):
            ia = int(face[k])
            ib = int(face[(k + 1) % nv])
            da = float(dist[ia])
            db = float(dist[ib])
            if (da < 0.0 <= db) or (db < 0.0 <= da):
                key = (min(ia, ib), max(ia, ib))
                pt_idx = edge_cache.get(key)
                if pt_idx is None:
                    denom = da - db
                    alpha = da / denom if abs(denom) > 1e-30 else 0.5
                    inter = pts[ia] + alpha * (pts[ib] - pts[ia])
                    pt_idx = len(slice_points)
                    slice_points.append(inter)
                    edge_cache[key] = pt_idx
                if pt_idx not in crossings:
                    crossings.append(pt_idx)
        for j in range(0, len(crossings) - 1, 2):
            segments.append((crossings[j], crossings[j + 1]))

    pts_out = np.asarray(slice_points, dtype=np.float64) if slice_points else np.zeros((0, 3), dtype=np.float64)
    return {
        "points": pts_out,
        "segments": np.asarray(segments, dtype=np.int32) if segments else np.zeros((0, 2), dtype=np.int32),
        "culled_chunks": culled_chunks,
        "cell_values": np.asarray(cell_values) if cell_values is not None else None,
    }


def compute_vortex_metrics(grad_u: np.ndarray) -> Dict[str, np.ndarray]:
    """Kernel 2: Compute Q-criterion and analytical lambda2 vortex identification metrics."""
    gu = np.asarray(grad_u, dtype=np.float64)
    s_tensor = 0.5 * (gu + np.swapaxes(gu, -1, -2))
    omega_tensor = 0.5 * (gu - np.swapaxes(gu, -1, -2))
    q_crit = 0.5 * (
        np.sum(omega_tensor * omega_tensor, axis=(-2, -1))
        - np.sum(s_tensor * s_tensor, axis=(-2, -1))
    )
    mat = (s_tensor @ s_tensor) + (omega_tensor @ omega_tensor)
    mat_sym = 0.5 * (mat + np.swapaxes(mat, -1, -2))
    eigvals = np.linalg.eigvalsh(mat_sym)
    lambda2 = eigvals[..., 1]
    return {"Q": np.asarray(q_crit, dtype=np.float64), "lambda2": np.asarray(lambda2, dtype=np.float64)}


def walk_segment_across_cells(
    mesh: Any,
    velocity: np.ndarray,
    seed_point: Sequence[float],
    dt: float = 0.05,
    max_steps: int = 200,
) -> np.ndarray:
    """Kernel 3: Topological O(1) face-exit streamline integrator across cells."""
    vel = np.asarray(velocity, dtype=np.float64)
    pos = np.asarray(seed_point, dtype=np.float64).copy()
    if mesh.num_cells == 0:
        return pos.reshape(1, 3)

    dists_init = np.linalg.norm(mesh.cell_centres - pos[None, :], axis=1)
    cell = int(np.argmin(dists_init))

    cell_faces: List[List[int]] = [[] for _ in range(mesh.num_cells)]
    for f_id in range(mesh.num_faces):
        cell_faces[int(mesh.owner[f_id])].append(f_id)
        nbr = int(mesh.neighbour[f_id])
        if nbr >= 0:
            cell_faces[nbr].append(f_id)

    trail: List[np.ndarray] = [pos.copy()]
    for _ in range(max_steps):
        u_c = vel[cell]
        if float(np.linalg.norm(u_c)) <= 1e-14:
            break
        t_exit = float(dt)
        next_cell = cell
        hit_boundary = False
        for f_id in cell_faces[cell]:
            own = int(mesh.owner[f_id])
            nbr = int(mesh.neighbour[f_id])
            sf = mesh.face_areas[f_id] if own == cell else -mesh.face_areas[f_id]
            flux = float(np.dot(u_c, sf))
            if flux > 1e-14:
                t_f = float(np.dot(mesh.face_centres[f_id] - pos, sf)) / flux
                if 0.0 <= t_f < t_exit:
                    t_exit = t_f
                    if nbr < 0:
                        hit_boundary = True
                    else:
                        next_cell = nbr if own == cell else own
        pos = pos + u_c * max(t_exit, 0.0)
        trail.append(pos.copy())
        if hit_boundary:
            break
        cell = next_cell

    return np.asarray(trail, dtype=np.float64)


def compute_wall_shear_3ring(
    mesh: Any,
    velocity: np.ndarray,
    nu: float,
    temperature: Optional[np.ndarray] = None,
    kappa_eff: Optional[float] = None,
    wall_patch: Optional[str] = None,
) -> Dict[str, np.ndarray]:
    """Kernel 4: Distance-weighted least-squares 3-ring wall shear stress, u_tau, y+ & q_w."""
    vel = np.asarray(velocity, dtype=np.float64)
    n_cells = mesh.num_cells
    cell_nbrs: List[List[int]] = [[] for _ in range(n_cells)]
    internal = np.where(mesh.neighbour >= 0)[0]
    for f_id in internal:
        o = int(mesh.owner[f_id])
        nb = int(mesh.neighbour[f_id])
        cell_nbrs[o].append(nb)
        cell_nbrs[nb].append(o)

    if wall_patch is not None and wall_patch in mesh.boundaries:
        start_f, count_f = mesh.boundaries[wall_patch]
        wall_faces = np.arange(start_f, start_f + count_f, dtype=np.int64)
    else:
        wall_faces = np.where(mesh.neighbour < 0)[0]

    n_w = len(wall_faces)
    tau_w = np.zeros((n_w, 3), dtype=np.float64)
    u_tau = np.zeros(n_w, dtype=np.float64)
    y_plus = np.zeros(n_w, dtype=np.float64)
    q_w = np.zeros(n_w, dtype=np.float64)

    for idx, f_id in enumerate(wall_faces):
        sf = mesh.face_areas[f_id]
        area = float(np.linalg.norm(sf))
        if area <= 1e-30:
            continue
        n_in = -sf / area
        own = int(mesh.owner[f_id])

        # Collect 3-ring neighborhood
        visited = {own}
        frontier = [own]
        for _ in range(3):
            nxt_frontier: List[int] = []
            for c in frontier:
                for nb in cell_nbrs[c]:
                    if nb not in visited:
                        visited.add(nb)
                        nxt_frontier.append(nb)
            frontier = nxt_frontier
            if not frontier:
                break

        ring_cells = list(visited)
        cf = mesh.face_centres[f_id]
        disp = mesh.cell_centres[ring_cells] - cf[None, :]
        y_n = np.maximum(disp @ n_in, 1e-12)
        weights = 1.0 / np.maximum(np.sum(disp * disp, axis=1), 1e-30)

        u_ring = vel[ring_cells]
        u_tang = u_ring - (u_ring @ n_in)[:, None] * n_in[None, :]
        denom_w = float(np.sum(weights * y_n * y_n))
        du_dn = (
            np.sum((weights * y_n)[:, None] * u_tang, axis=0) / denom_w
            if denom_w > 1e-30
            else np.zeros(3, dtype=np.float64)
        )
        tau = float(nu) * du_dn
        tau_w[idx] = tau
        ut = float(np.sqrt(max(float(np.linalg.norm(tau)), 0.0)))
        u_tau[idx] = ut
        y_own = max(float(np.dot(mesh.cell_centres[own] - cf, n_in)), 1e-12)
        y_plus[idx] = (y_own * ut / float(nu)) if float(nu) > 1e-30 else 0.0

        if temperature is not None and kappa_eff is not None and denom_w > 1e-30:
            t_arr = np.asarray(temperature, dtype=np.float64)
            dt_dn = float(np.sum(weights * y_n * (t_arr[ring_cells] - t_arr[own]))) / denom_w
            q_w[idx] = -float(kappa_eff) * dt_dn

    return {"tau_w": tau_w, "u_tau": u_tau, "y_plus": y_plus, "q_w": q_w}


def integrate_patch_set(
    mesh: Any,
    pressure: np.ndarray,
    wall_shear: Optional[np.ndarray] = None,
    mass_flux: Optional[np.ndarray] = None,
    patch_names: Optional[Sequence[str]] = None,
    q_inf: float = 1.0,
    area_ref: float = 1.0,
    moment_origin: Sequence[float] = (0.0, 0.0, 0.0),
    length_ref: float = 1.0,
) -> Dict[str, Any]:
    """Kernel 5: Real-time boundary patch integral (forces, moments, mass balance error)."""
    p_arr = np.asarray(pressure, dtype=np.float64)
    origin = np.asarray(moment_origin, dtype=np.float64)

    face_indices: List[int] = []
    if patch_names:
        for pname in patch_names:
            if pname in mesh.boundaries:
                sf_i, cnt_i = mesh.boundaries[pname]
                face_indices.extend(range(int(sf_i), int(sf_i) + int(cnt_i)))
    else:
        face_indices = np.where(mesh.neighbour < 0)[0].tolist()

    idx_arr = np.asarray(face_indices, dtype=np.int64)
    if len(idx_arr) == 0:
        zero3 = np.zeros(3, dtype=np.float64)
        return {
            "F_p": zero3,
            "F_v": zero3,
            "F_total": zero3,
            "Cd": 0.0,
            "Cl": 0.0,
            "Cs": 0.0,
            "Cm": 0.0,
            "mass_balance_error": 0.0,
        }

    sf = mesh.face_areas[idx_arr]
    cf = mesh.face_centres[idx_arr]
    area_mag = np.linalg.norm(sf, axis=1)
    p_f = p_arr[idx_arr] if len(p_arr) == mesh.num_faces else p_arr[mesh.owner[idx_arr]]
    f_p = np.sum(p_f[:, None] * sf, axis=0)

    if wall_shear is not None:
        ws = np.asarray(wall_shear, dtype=np.float64)
        ws_f = ws[idx_arr] if len(ws) == mesh.num_faces else ws
        f_v = np.sum(-ws_f * area_mag[:, None], axis=0)
    else:
        f_v = np.zeros(3, dtype=np.float64)

    f_tot = f_p + f_v
    denom = max(float(q_inf) * float(area_ref), 1e-30)
    moments = np.sum(np.cross(cf - origin[None, :], p_f[:, None] * sf), axis=0)
    m_err = float(np.sum(np.asarray(mass_flux, dtype=np.float64)[idx_arr])) if mass_flux is not None else 0.0

    return {
        "F_p": f_p,
        "F_v": f_v,
        "F_total": f_tot,
        "Cd": float(f_tot[0] / denom),
        "Cl": float(f_tot[2] / denom),
        "Cs": float(f_tot[1] / denom),
        "Cm": float(moments[1] / (denom * max(float(length_ref), 1e-30))),
        "mass_balance_error": m_err,
    }


__all__ = [
    "MAGIC_PAKET1_GEOMETRY",
    "MAGIC_PAKET2_MESH_QUALITY",
    "MAGIC_PAKET3_FIELD",
    "HEADER_SIZE",
    "FIELD_HEADER_SIZE",
    "CHUNK_ENTRY_SIZE",
    "GeometryBufferDescriptor",
    "MeshQualityBufferDescriptor",
    "FieldBufferDescriptor",
    "pack_geometry_buffer",
    "unpack_geometry_buffer",
    "pack_mesh_quality_buffer",
    "unpack_mesh_quality_buffer",
    "pack_field_buffer",
    "unpack_field_buffer",
    "slice_plane_polyhedral",
    "compute_vortex_metrics",
    "walk_segment_across_cells",
    "compute_wall_shear_3ring",
    "integrate_patch_set",
]
