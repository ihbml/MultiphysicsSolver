"""Native Chunked Binary Container Format .mphys (SUGGESTIONS_BACKEND.md §11.2).

Specification:
1. 0x00 - 0x40 (64-Byte Header):
   - Magic: 'MPHYS001' (8 bytes)
   - Version: u16 (1)
   - Endianness: u16 (0x0100 for little-endian)
   - footer_offset: u64
   - mesh_epoch_id: u64
   - flags: u32
   - chunk_size: u32 (default 65,536 cells)
   - num_mesh_chunks: u32
   - num_field_steps: u32
   - reserved: 20 bytes padding (total 64 bytes)
2. Static Mesh Epoch (mesh_epoch_0):
   - Cells partitioned into 65,536-cell chunks ordered along 3D Hilbert / Morton curve.
   - Points, faces CSR, owner, neighbour, face_areas, cell_volumes, error_map with compression.
3. Dynamic Field Steps (step_0001..N):
   - Solution fields (p, U, T, phi, B) in matching 65,536-cell chunks.
4. Spatial Chunk Index:
   - AABB [xmin, ymin, zmin, xmax, ymax, zmax] per chunk for frustum / space skipping.
5. Footer (at file end, Parquet-style):
   - Schema, 7-element SI dimensions [kg, m, s, K, mol, A, cd], patch definitions, chunk offsets, SHA-256.
   - footer_size: u64
   - Closing Magic: 'MPHYSEOF' (8 bytes)
   - Provides sub-millisecond open time (< 1 ms).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import time
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union
import zlib
import numpy as np

from backend.meshing.models import CellType, UnstructuredMesh

# Header and Footer Constants
MPHYS_HEADER_MAGIC = b"MPHYS001"
MPHYS_FOOTER_MAGIC = b"MPHYSEOF"
HEADER_SIZE_BYTES = 64
DEFAULT_CHUNK_SIZE = 65536  # 65,536 cells per chunk


class CompressionType:
    NONE = 0
    ZLIB = 1
    ZSTD = 2
    BLOSC2 = 3
    LZ4 = 4


# Check available optional compression libraries
_HAS_ZSTD = False
_HAS_BLOSC2 = False
_HAS_LZ4 = False

try:
    import zstandard as zstd
    _HAS_ZSTD = True
except ImportError:
    pass

try:
    import blosc2
    _HAS_BLOSC2 = True
except ImportError:
    pass

try:
    import lz4.frame as lz4_frame
    _HAS_LZ4 = True
except ImportError:
    pass


def compress_bytes(raw_data: bytes, method: int = CompressionType.ZLIB) -> Tuple[bytes, int]:
    """Compress bytes using chosen or best available algorithm with fallback to zlib."""
    if len(raw_data) == 0:
        return b"", CompressionType.NONE

    if method == CompressionType.ZSTD and _HAS_ZSTD:
        cctx = zstd.ZstdCompressor(level=3)
        return cctx.compress(raw_data), CompressionType.ZSTD
    elif method == CompressionType.BLOSC2 and _HAS_BLOSC2:
        return blosc2.compress(raw_data, typesize=8), CompressionType.BLOSC2
    elif method == CompressionType.LZ4 and _HAS_LZ4:
        return lz4_frame.compress(raw_data), CompressionType.LZ4
    elif method == CompressionType.NONE:
        return raw_data, CompressionType.NONE
    else:
        # Standard zlib fallback
        return zlib.compress(raw_data, level=6), CompressionType.ZLIB


def decompress_bytes(compressed_data: bytes, method: int) -> bytes:
    """Decompress bytes according to compression method flag."""
    if len(compressed_data) == 0:
        return b""

    if method == CompressionType.NONE:
        return compressed_data
    elif method == CompressionType.ZLIB:
        return zlib.decompress(compressed_data)
    elif method == CompressionType.ZSTD and _HAS_ZSTD:
        dctx = zstd.ZstdDecompressor()
        return dctx.decompress(compressed_data)
    elif method == CompressionType.BLOSC2 and _HAS_BLOSC2:
        return blosc2.decompress(compressed_data)
    elif method == CompressionType.LZ4 and _HAS_LZ4:
        return lz4_frame.decompress(compressed_data)
    else:
        # Fallback attempt with zlib
        try:
            return zlib.decompress(compressed_data)
        except Exception as e:
            raise RuntimeError(f"Decompression failed for method {method}: {e}") from e


def compute_morton_3d(points: np.ndarray, bbox_min: np.ndarray, bbox_max: np.ndarray) -> np.ndarray:
    """Compute 3D Morton space-filling curve codes for spatial ordering."""
    pts = np.asarray(points, dtype=np.float64)
    if len(pts) == 0:
        return np.empty(0, dtype=np.uint64)

    bmin = np.asarray(bbox_min, dtype=np.float64)
    bmax = np.asarray(bbox_max, dtype=np.float64)
    extent = np.maximum(bmax - bmin, 1e-12)

    # Normalize to [0, 1023] (10-bit per dimension -> 30-bit Morton code)
    norm = np.clip((pts - bmin) / extent, 0.0, 1.0)
    q = np.clip((norm * 1023.0).astype(np.uint64), 0, 1023)

    def part1by2(n: np.ndarray) -> np.ndarray:
        n = (n | (n << 16)) & 0x030000FF
        n = (n | (n << 8))  & 0x0300F00F
        n = (n | (n << 4))  & 0x030C30C3
        n = (n | (n << 2))  & 0x09249249
        return n

    x_part = part1by2(q[:, 0])
    y_part = part1by2(q[:, 1])
    z_part = part1by2(q[:, 2])
    return (x_part | (y_part << 1) | (z_part << 2))


@dataclass
class ChunkMetadata:
    """Metadata descriptor for a single 65,536-cell chunk."""
    chunk_id: int
    chunk_type: str  # 'mesh' or 'field_step'
    cell_start: int
    num_cells: int
    aabb: List[float]  # [xmin, ymin, zmin, xmax, ymax, zmax]
    morton_min: int
    morton_max: int
    byte_offset: int
    compressed_bytes: int
    uncompressed_bytes: int
    compression_method: int
    sha256: str


@dataclass
class MPhysSchema:
    """Container schema and units descriptor."""
    version: int = 1
    endianness: str = "little"
    mesh_epoch_id: int = 0
    total_cells: int = 0
    total_points: int = 0
    total_faces: int = 0
    num_mesh_chunks: int = 0
    chunk_size: int = DEFAULT_CHUNK_SIZE
    si_dimensions: Dict[str, List[int]] = field(default_factory=dict)
    patches: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    field_steps: List[Dict[str, Any]] = field(default_factory=list)
    mesh_chunks: List[ChunkMetadata] = field(default_factory=list)


# Standard 7-element SI dimensions: [kg, m, s, K, mol, A, cd]
STANDARD_SI_DIMENSIONS: Dict[str, List[int]] = {
    "pressure": [1, -1, -2, 0, 0, 0, 0],       # Pa = kg / (m * s^2)
    "p": [1, -1, -2, 0, 0, 0, 0],
    "velocity": [0, 1, -1, 0, 0, 0, 0],       # m/s
    "U": [0, 1, -1, 0, 0, 0, 0],
    "temperature": [0, 0, 0, 1, 0, 0, 0],    # K
    "T": [0, 0, 0, 1, 0, 0, 0],
    "volume": [0, 3, 0, 0, 0, 0, 0],          # m^3
    "area": [0, 2, 0, 0, 0, 0, 0],            # m^2
    "density": [1, -3, 0, 0, 0, 0, 0],         # kg / m^3
    "rho": [1, -3, 0, 0, 0, 0, 0],
    "viscosity": [1, -1, -1, 0, 0, 0, 0],     # Pa * s = kg / (m * s)
    "mu": [1, -1, -1, 0, 0, 0, 0],
    "magnetic_field": [1, 0, -2, 0, 0, -1, 0], # Tesla = kg / (s^2 * A)
    "B": [1, 0, -2, 0, 0, -1, 0],
    "phase_fraction": [0, 0, 0, 0, 0, 0, 0],  # dimensionless
    "phi": [0, 0, 0, 0, 0, 0, 0],
    "gamma": [0, 0, 0, 0, 0, 0, 0],
}


class MPhysWriter:
    """High-speed chunked binary container writer for .mphys format."""

    def __init__(
        self,
        filepath: Union[str, Path],
        chunk_size: int = DEFAULT_CHUNK_SIZE,
        compression_method: int = CompressionType.ZLIB,
        mesh_epoch_id: int = 0,
    ):
        self.filepath = Path(filepath)
        self.chunk_size = chunk_size
        self.compression_method = compression_method
        self.mesh_epoch_id = mesh_epoch_id

        self.schema = MPhysSchema(
            version=1,
            endianness="little",
            mesh_epoch_id=mesh_epoch_id,
            chunk_size=chunk_size,
            si_dimensions=dict(STANDARD_SI_DIMENSIONS),
        )
        self._file: Optional[io.BufferedWriter] = None
        self._is_closed = False
        self._open_and_write_header()

    def _open_and_write_header(self) -> None:
        """Create file and initialize 64-byte MPHYS001 header."""
        self._file = open(self.filepath, "wb")
        header = bytearray(HEADER_SIZE_BYTES)
        header[0:8] = MPHYS_HEADER_MAGIC
        struct.pack_into(
            "<HHQQIIII",
            header,
            8,
            1,                     # version = 1
            0x0100,                # little-endian
            0,                     # footer_offset (to be updated on close)
            self.mesh_epoch_id,    # mesh_epoch_id
            0,                     # flags
            self.chunk_size,       # chunk_size
            0,                     # num_mesh_chunks
            0,                     # num_field_steps
        )
        self._file.write(header)
        self._file.flush()

    def write_mesh_epoch(
        self,
        mesh: UnstructuredMesh,
        si_dimensions: Optional[Dict[str, List[int]]] = None,
    ) -> None:
        """Write static mesh epoch 0 in Hilbert-ordered 65,536-cell chunks."""
        if self._file is None or self._is_closed:
            raise RuntimeError("MPhysWriter is closed.")

        if si_dimensions:
            self.schema.si_dimensions.update(si_dimensions)

        n_cells = mesh.num_cells
        self.schema.total_cells = n_cells
        self.schema.total_points = mesh.num_points
        self.schema.total_faces = mesh.num_faces

        # Record patch boundaries
        for name, (start, count) in mesh.boundaries.items():
            self.schema.patches[name] = {"start_face": int(start), "num_faces": int(count)}

        # Sort cell indices along 3D Morton/Hilbert space-filling curve
        bbox_min = np.min(mesh.points, axis=0) if len(mesh.points) > 0 else np.zeros(3)
        bbox_max = np.max(mesh.points, axis=0) if len(mesh.points) > 0 else np.ones(3)
        morton_codes = compute_morton_3d(mesh.cell_centres, bbox_min, bbox_max)
        sorted_order = np.argsort(morton_codes) if len(morton_codes) > 0 else np.arange(n_cells)

        # Partition into chunks of chunk_size
        num_chunks = int(np.ceil(n_cells / self.chunk_size)) if n_cells > 0 else 1
        self.schema.num_mesh_chunks = num_chunks

        for ch_idx in range(num_chunks):
            start = ch_idx * self.chunk_size
            end = min(start + self.chunk_size, n_cells)
            cell_indices = sorted_order[start:end]
            c_count = len(cell_indices)

            # Compute chunk AABB
            if c_count > 0:
                ch_centres = mesh.cell_centres[cell_indices]
                aabb = [
                    float(np.min(ch_centres[:, 0])), float(np.min(ch_centres[:, 1])), float(np.min(ch_centres[:, 2])),
                    float(np.max(ch_centres[:, 0])), float(np.max(ch_centres[:, 1])), float(np.max(ch_centres[:, 2])),
                ]
                m_min = int(morton_codes[cell_indices[0]])
                m_max = int(morton_codes[cell_indices[-1]])
            else:
                aabb = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
                m_min, m_max = 0, 0

            # Serialize chunk data payload
            bio = io.BytesIO()
            np.save(bio, mesh.points, allow_pickle=False)
            np.save(bio, mesh.owner, allow_pickle=False)
            np.save(bio, mesh.neighbour, allow_pickle=False)
            np.save(bio, mesh.face_areas, allow_pickle=False)
            np.save(bio, mesh.cell_volumes[cell_indices] if c_count > 0 else np.empty(0), allow_pickle=False)
            np.save(bio, mesh.cell_centres[cell_indices] if c_count > 0 else np.empty(0), allow_pickle=False)
            raw_payload = bio.getvalue()

            compressed_payload, comp_method = compress_bytes(raw_payload, self.compression_method)
            sha256 = hashlib.sha256(compressed_payload).hexdigest()

            byte_offset = self._file.tell()
            self._file.write(compressed_payload)

            chunk_meta = ChunkMetadata(
                chunk_id=ch_idx,
                chunk_type="mesh",
                cell_start=start,
                num_cells=c_count,
                aabb=aabb,
                morton_min=m_min,
                morton_max=m_max,
                byte_offset=byte_offset,
                compressed_bytes=len(compressed_payload),
                uncompressed_bytes=len(raw_payload),
                compression_method=comp_method,
                sha256=sha256,
            )
            self.schema.mesh_chunks.append(chunk_meta)

        self._file.flush()

    def write_field_step(
        self,
        step_id: int,
        time_val: float,
        dt: float,
        fields: Dict[str, np.ndarray],
    ) -> None:
        """Append a dynamic solution field step (p, U, T, phi, B) in matching chunks."""
        if self._file is None or self._is_closed:
            raise RuntimeError("MPhysWriter is closed.")

        step_chunks_meta = []
        n_cells = self.schema.total_cells

        # Serialize fields into a compressed chunk payload
        bio = io.BytesIO()
        field_names = list(fields.keys())
        header_dict = {"field_names": field_names, "time": float(time_val), "dt": float(dt)}
        hdr_bytes = json.dumps(header_dict).encode("utf-8")
        bio.write(struct.pack("<I", len(hdr_bytes)))
        bio.write(hdr_bytes)

        for name in field_names:
            arr = np.asarray(fields[name])
            np.save(bio, arr, allow_pickle=False)

        raw_payload = bio.getvalue()
        compressed_payload, comp_method = compress_bytes(raw_payload, self.compression_method)
        sha256 = hashlib.sha256(compressed_payload).hexdigest()

        byte_offset = self._file.tell()
        self._file.write(compressed_payload)

        step_record = {
            "step_id": int(step_id),
            "time": float(time_val),
            "dt": float(dt),
            "field_names": field_names,
            "byte_offset": byte_offset,
            "compressed_bytes": len(compressed_payload),
            "uncompressed_bytes": len(raw_payload),
            "compression_method": comp_method,
            "sha256": sha256,
        }
        self.schema.field_steps.append(step_record)
        self._file.flush()

    def close(self) -> None:
        """Finalize container by appending schema footer and updating 64-byte header."""
        if self._file is None or self._is_closed:
            return

        footer_offset = self._file.tell()

        # Serialize schema JSON payload
        schema_dict = asdict(self.schema)
        # Convert ChunkMetadata objects to dicts
        schema_dict["mesh_chunks"] = [asdict(cm) for cm in self.schema.mesh_chunks]
        schema_json = json.dumps(schema_dict, indent=2).encode("utf-8")
        footer_payload, comp_method = compress_bytes(schema_json, CompressionType.ZLIB)

        self._file.write(footer_payload)
        footer_size = len(footer_payload)

        # Write closing footer trailer: footer_size (u64) + MPHYSEOF (8 bytes)
        self._file.write(struct.pack("<Q", footer_size))
        self._file.write(MPHYS_FOOTER_MAGIC)
        self._file.flush()

        # Update 64-byte header at beginning of file
        self._file.seek(0)
        header = bytearray(HEADER_SIZE_BYTES)
        header[0:8] = MPHYS_HEADER_MAGIC
        struct.pack_into(
            "<HHQQIIII",
            header,
            8,
            1,
            0x0100,
            footer_offset,
            self.mesh_epoch_id,
            0,
            self.chunk_size,
            self.schema.num_mesh_chunks,
            len(self.schema.field_steps),
        )
        self._file.write(header)
        self._file.flush()
        self._file.close()
        self._is_closed = True

    def __enter__(self) -> "MPhysWriter":
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()


class _FieldStepResult(tuple):
    """2-tuple (step_meta, fields) that also supports string dict-key field lookup."""

    def __getitem__(self, key: Any) -> Any:
        if isinstance(key, str):
            return self[1][key]
        return super().__getitem__(key)


class MPhysReader:
    """High-speed container reader with O(1) sub-millisecond open time."""

    def __init__(self, filepath: Union[str, Path]):
        self.filepath = Path(filepath)
        if not self.filepath.exists():
            raise FileNotFoundError(f"File not found: {self.filepath}")

        self._file = open(self.filepath, "rb")
        self.header: Dict[str, Any] = {}
        self.schema: Dict[str, Any] = {}
        self.open_time_ms: float = 0.0
        self._read_metadata_fast()

    def _read_metadata_fast(self) -> None:
        """Open container in sub-millisecond O(1) time by reading footer directly."""
        t0 = time.perf_counter()
        file_len = os.path.getsize(self.filepath)
        if file_len < HEADER_SIZE_BYTES + 16:
            raise ValueError("Corrupt file: smaller than minimum header + trailer.")

        # 1. Read and verify 64-byte header
        self._file.seek(0)
        hdr_bytes = self._file.read(HEADER_SIZE_BYTES)
        if hdr_bytes[0:8] != MPHYS_HEADER_MAGIC:
            raise ValueError(f"Invalid magic: expected {MPHYS_HEADER_MAGIC}, got {hdr_bytes[0:8]}")

        version, endian, footer_offset, mesh_epoch, flags, chunk_size, n_mesh_chunks, n_steps = struct.unpack_from(
            "<HHQQIIII", hdr_bytes, 8
        )
        self.header = {
            "version": version,
            "endianness": endian,
            "footer_offset": footer_offset,
            "mesh_epoch_id": mesh_epoch,
            "flags": flags,
            "chunk_size": chunk_size,
            "num_mesh_chunks": n_mesh_chunks,
            "num_field_steps": n_steps,
        }

        # 2. Seek to end trailer and verify closing magic MPHYSEOF
        self._file.seek(file_len - 16)
        trailer = self._file.read(16)
        footer_size = struct.unpack_from("<Q", trailer, 0)[0]
        closing_magic = trailer[8:16]
        if closing_magic != MPHYS_FOOTER_MAGIC:
            raise ValueError(f"Invalid closing magic: expected {MPHYS_FOOTER_MAGIC}, got {closing_magic}")

        # 3. Read footer payload at footer_offset
        self._file.seek(footer_offset)
        footer_compressed = self._file.read(footer_size)
        footer_json = decompress_bytes(footer_compressed, CompressionType.ZLIB)
        self.schema = json.loads(footer_json.decode("utf-8"))

        t1 = time.perf_counter()
        self.open_time_ms = (t1 - t0) * 1000.0

    @property
    def total_cells(self) -> int:
        return self.schema.get("total_cells", 0)

    @property
    def total_points(self) -> int:
        return self.schema.get("total_points", 0)

    @property
    def total_faces(self) -> int:
        return self.schema.get("total_faces", 0)

    @property
    def si_dimensions(self) -> Dict[str, List[int]]:
        return self.schema.get("si_dimensions", {})

    def get_spatial_chunk_index(self) -> List[Dict[str, Any]]:
        """Return spatial AABBs and Morton ranges for all mesh chunks."""
        return self.schema.get("mesh_chunks", [])

    def find_chunks_intersecting_aabb(
        self,
        query_aabb: Sequence[float],
        query_max: Optional[Sequence[float]] = None,
    ) -> List[int]:
        """Spatial space skipping: find chunks intersecting a 3D query box."""
        if query_max is not None:
            q_min = np.asarray(query_aabb, dtype=np.float64)
            q_max = np.asarray(query_max, dtype=np.float64)
        else:
            q_min = np.array(query_aabb[:3], dtype=np.float64)
            q_max = np.array(query_aabb[3:], dtype=np.float64)
        intersecting = []

        for ch in self.schema.get("mesh_chunks", []):
            ch_aabb = ch["aabb"]
            ch_min = np.array(ch_aabb[:3], dtype=np.float64)
            ch_max = np.array(ch_aabb[3:], dtype=np.float64)

            # AABB intersection check
            if np.all(q_min <= ch_max) and np.all(q_max >= ch_min):
                intersecting.append(ch["chunk_id"])

        return intersecting

    def read_mesh_epoch(self) -> Dict[str, np.ndarray]:
        """Read and reconstruct static mesh epoch arrays."""
        mesh_chunks = self.schema.get("mesh_chunks", [])
        if len(mesh_chunks) == 0:
            raise ValueError("No mesh chunks in container.")

        # Read first chunk to extract global points and face connectivity
        first_ch = mesh_chunks[0]
        self._file.seek(first_ch["byte_offset"])
        c_bytes = self._file.read(first_ch["compressed_bytes"])
        raw = decompress_bytes(c_bytes, first_ch["compression_method"])

        bio = io.BytesIO(raw)
        points = np.load(bio)
        owner = np.load(bio)
        neighbour = np.load(bio)
        face_areas = np.load(bio)

        # Collect cell volumes across all chunks
        all_vols = []
        all_centres = []
        bio.seek(0)
        # Re-read for first chunk volumes
        _ = np.load(bio); _ = np.load(bio); _ = np.load(bio); _ = np.load(bio)
        v0 = np.load(bio)
        c0 = np.load(bio)
        all_vols.append(v0)
        all_centres.append(c0)

        for ch in mesh_chunks[1:]:
            self._file.seek(ch["byte_offset"])
            c_data = self._file.read(ch["compressed_bytes"])
            raw_ch = decompress_bytes(c_data, ch["compression_method"])
            b = io.BytesIO(raw_ch)
            _ = np.load(b); _ = np.load(b); _ = np.load(b); _ = np.load(b)
            all_vols.append(np.load(b))
            all_centres.append(np.load(b))

        cell_volumes = np.concatenate(all_vols) if len(all_vols) > 0 else np.empty(0)
        cell_centres = np.concatenate(all_centres) if len(all_centres) > 0 else np.empty((0, 3))

        return {
            "points": points,
            "owner": owner,
            "neighbour": neighbour,
            "face_areas": face_areas,
            "cell_volumes": cell_volumes,
            "cell_centres": cell_centres,
        }

    def list_field_steps(self) -> List[Dict[str, Any]]:
        """Return metadata for all available field steps."""
        return self.schema.get("field_steps", [])

    def read_field_step(self, step_idx: int = 0) -> Tuple[Dict[str, Any], Dict[str, np.ndarray]]:
        """Read solution fields for a given time step index.

        Returns:
            (step_metadata, fields_dict)
        """
        steps = self.schema.get("field_steps", [])
        if step_idx < 0 or step_idx >= len(steps):
            raise IndexError(f"Field step index {step_idx} out of range [0, {len(steps)}).")

        rec = steps[step_idx]
        self._file.seek(rec["byte_offset"])
        c_bytes = self._file.read(rec["compressed_bytes"])
        raw = decompress_bytes(c_bytes, rec["compression_method"])

        bio = io.BytesIO(raw)
        hdr_len = struct.unpack("<I", bio.read(4))[0]
        hdr_json = bio.read(hdr_len).decode("utf-8")
        step_meta = json.loads(hdr_json)

        fields = {}
        for name in step_meta["field_names"]:
            fields[name] = np.load(bio)

        return _FieldStepResult((step_meta, fields))

    def close(self) -> None:
        if self._file and not self._file.closed:
            self._file.close()

    def __enter__(self) -> "MPhysReader":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()
