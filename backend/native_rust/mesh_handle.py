"""Zero-copy mmap chunked .mphys handle (Blueprint §1.4.2, MPHYS002).

Provides an uncompressed, 64-byte-aligned chunked binary mesh container
allowing O(1) sub-5 ms metadata open and zero-copy NumPy array views via
``mmap.mmap`` + ``np.frombuffer`` sliced by 65,536-cell chunk boundaries.
"""

from __future__ import annotations

import json
import math
import mmap
import os
from pathlib import Path
import struct
import time
from typing import Any, BinaryIO, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np

from backend.meshing.models import CSRSequence, UnstructuredMesh

MPHYS2_HEADER_MAGIC = b"MPHYS002"
MPHYS_FOOTER_MAGIC = b"MPHYSEOF"
HEADER_SIZE_BYTES = 64
ALIGNMENT_BYTES = 64
DEFAULT_CHUNK_SIZE = 65536


def _pad_to_alignment(fh: BinaryIO, alignment: int = ALIGNMENT_BYTES) -> int:
    """Pad output stream with zero bytes to the next ``alignment`` boundary."""
    pos = fh.tell()
    rem = pos % alignment
    if rem != 0:
        fh.write(b"\x00" * (alignment - rem))
    return fh.tell()


def _write_aligned_array(fh: BinaryIO, arr: np.ndarray) -> Dict[str, Any]:
    """Write a C-contiguous little-endian NumPy array at a 64-byte-aligned offset."""
    dtype_le = arr.dtype.newbyteorder("<") if arr.dtype.byteorder not in ("<", "|", "=") else arr.dtype
    contig = np.ascontiguousarray(arr, dtype=dtype_le)
    offset = _pad_to_alignment(fh)
    raw = contig.tobytes(order="C")
    if raw:
        fh.write(raw)
    return {
        "offset": int(offset),
        "shape": [int(s) for s in contig.shape],
        "dtype": contig.dtype.str,
        "nbytes": int(len(raw)),
    }


def _pack_csr_pair(
    seq: Optional[Union[List[np.ndarray], CSRSequence]],
    offsets: Optional[np.ndarray],
    nodes: Optional[np.ndarray],
) -> Tuple[Optional[np.ndarray], Optional[np.ndarray]]:
    """Return contiguous (offsets, nodes) int64 arrays from CSR or list-of-arrays."""
    if offsets is not None and nodes is not None:
        return np.asarray(offsets, dtype=np.int64), np.asarray(nodes, dtype=np.int64)
    if isinstance(seq, CSRSequence):
        return np.asarray(seq.offsets, dtype=np.int64), np.asarray(seq.data, dtype=np.int64)
    if seq is not None and len(seq) > 0:
        lengths = np.fromiter((len(item) for item in seq), dtype=np.int64, count=len(seq))
        offs = np.empty(len(seq) + 1, dtype=np.int64)
        offs[0] = 0
        np.cumsum(lengths, out=offs[1:])
        flat = np.concatenate([np.asarray(item, dtype=np.int64).ravel() for item in seq])
        return offs, flat
    return None, None


def write_mesh_chunked(
    mesh: UnstructuredMesh,
    path: Union[str, Path],
    chunk_size: int = DEFAULT_CHUNK_SIZE,
    mesh_epoch_id: int = 0,
) -> Path:
    """Write an ``UnstructuredMesh`` as an aligned, mmap-ready MPHYS002 container.

    Arrays are stored uncompressed and 64-byte-aligned so ``MeshHandle`` can
    expose direct zero-copy ``np.frombuffer`` views without heap copies.
    """
    if chunk_size <= 0:
        raise ValueError(f"chunk_size must be positive, got {chunk_size}")

    out_path = Path(path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    points = np.asarray(mesh.points, dtype=np.float64)
    owner = np.asarray(mesh.owner, dtype=np.int64)
    neighbour = np.asarray(mesh.neighbour, dtype=np.int64)
    face_areas = np.asarray(mesh.face_areas, dtype=np.float64)
    face_centres = np.asarray(mesh.face_centres, dtype=np.float64)
    cell_centres = np.asarray(mesh.cell_centres, dtype=np.float64)
    cell_volumes = np.asarray(mesh.cell_volumes, dtype=np.float64)
    delta_f = np.asarray(mesh.delta_f, dtype=np.float64)
    k_f = np.asarray(mesh.k_f, dtype=np.float64)

    n_cells = int(len(cell_volumes))
    n_faces = int(len(face_areas))
    n_points = int(len(points))

    face_offsets, face_nodes = _pack_csr_pair(mesh.faces, mesh.face_offsets, mesh.face_nodes)
    cell_offsets, cell_nodes = _pack_csr_pair(mesh.cells, mesh.cell_offsets, mesh.cell_nodes)

    num_chunks = int(math.ceil(n_cells / chunk_size)) if n_cells > 0 else 0

    with open(out_path, "wb") as fh:
        fh.write(b"\x00" * HEADER_SIZE_BYTES)

        arrays_meta: Dict[str, Dict[str, Any]] = {}
        arrays_meta["points"] = _write_aligned_array(fh, points)
        arrays_meta["owner"] = _write_aligned_array(fh, owner)
        arrays_meta["neighbour"] = _write_aligned_array(fh, neighbour)
        arrays_meta["face_areas"] = _write_aligned_array(fh, face_areas)
        arrays_meta["face_centres"] = _write_aligned_array(fh, face_centres)
        arrays_meta["cell_centres"] = _write_aligned_array(fh, cell_centres)
        arrays_meta["cell_volumes"] = _write_aligned_array(fh, cell_volumes)
        arrays_meta["delta_f"] = _write_aligned_array(fh, delta_f)
        arrays_meta["k_f"] = _write_aligned_array(fh, k_f)

        if mesh.cell_regions is not None:
            arrays_meta["cell_regions"] = _write_aligned_array(
                fh, np.asarray(mesh.cell_regions, dtype=np.int32)
            )
        if mesh.cell_types is not None:
            arrays_meta["cell_types"] = _write_aligned_array(
                fh, np.asarray(mesh.cell_types, dtype=np.int32)
            )
        if face_offsets is not None and face_nodes is not None:
            arrays_meta["face_offsets"] = _write_aligned_array(fh, face_offsets)
            arrays_meta["face_nodes"] = _write_aligned_array(fh, face_nodes)
        if cell_offsets is not None and cell_nodes is not None:
            arrays_meta["cell_offsets"] = _write_aligned_array(fh, cell_offsets)
            arrays_meta["cell_nodes"] = _write_aligned_array(fh, cell_nodes)

        vol_base_offset = arrays_meta["cell_volumes"]["offset"]
        vol_itemsize = np.dtype(np.float64).itemsize

        chunks_meta: List[Dict[str, Any]] = []
        chunk_table_offset = _pad_to_alignment(fh)
        for ch_idx in range(num_chunks):
            c_start = ch_idx * chunk_size
            c_count = min(chunk_size, n_cells - c_start)
            byte_off = vol_base_offset + c_start * vol_itemsize
            fh.write(struct.pack("<QQQ", c_start, c_count, byte_off))

            if c_count > 0 and len(cell_centres) >= c_start + c_count:
                sl = cell_centres[c_start : c_start + c_count]
                c_min = np.min(sl, axis=0)
                c_max = np.max(sl, axis=0)
                aabb = [
                    float(c_min[0]), float(c_min[1]), float(c_min[2]),
                    float(c_max[0]), float(c_max[1]), float(c_max[2]),
                ]
            else:
                aabb = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]

            chunks_meta.append(
                {
                    "chunk_id": ch_idx,
                    "cell_start": int(c_start),
                    "cell_count": int(c_count),
                    "byte_offset": int(byte_off),
                    "aabb": aabb,
                }
            )

        footer_offset = fh.tell()
        boundaries_json = {
            str(name): [int(pair[0]), int(pair[1])]
            for name, pair in mesh.boundaries.items()
        }
        footer_obj = {
            "format": "MPHYS002",
            "version": 2,
            "mesh_epoch_id": int(mesh_epoch_id),
            "n_cells": n_cells,
            "n_faces": n_faces,
            "n_points": n_points,
            "chunk_size": int(chunk_size),
            "num_chunks": num_chunks,
            "topological_dim": int(mesh.topological_dim),
            "boundaries": boundaries_json,
            "chunk_table_offset": int(chunk_table_offset),
            "arrays": arrays_meta,
            "chunks": chunks_meta,
        }
        footer_bytes = json.dumps(footer_obj, separators=(",", ":")).encode("utf-8")
        fh.write(footer_bytes)
        fh.write(struct.pack("<Q", len(footer_bytes)))
        fh.write(MPHYS_FOOTER_MAGIC)

        header = bytearray(HEADER_SIZE_BYTES)
        header[0:8] = MPHYS2_HEADER_MAGIC
        struct.pack_into(
            "<HHQQIII",
            header,
            8,
            2,
            0x0100,
            footer_offset,
            mesh_epoch_id,
            0,
            chunk_size,
            num_chunks,
        )
        fh.seek(0)
        fh.write(header)
        fh.flush()

    return out_path


class MeshHandle:
    """Zero-copy ``mmap`` reader handle for MPHYS002 chunked mesh containers."""

    def __init__(
        self,
        path: Path,
        file_obj: BinaryIO,
        mm: mmap.mmap,
        header: Dict[str, Any],
        footer: Dict[str, Any],
        open_time_ms: float,
    ) -> None:
        self.path = path
        self._file: Optional[BinaryIO] = file_obj
        self._mmap: Optional[mmap.mmap] = mm
        self.header = header
        self.footer = footer
        self.open_time_ms = float(open_time_ms)

        self.n_cells: int = int(footer["n_cells"])
        self.n_faces: int = int(footer["n_faces"])
        self.n_points: int = int(footer["n_points"])
        self.chunk_size: int = int(footer["chunk_size"])
        self.num_chunks: int = int(footer["num_chunks"])
        self.boundaries: Dict[str, Tuple[int, int]] = {
            str(k): (int(v[0]), int(v[1]))
            for k, v in footer.get("boundaries", {}).items()
        }
        self._arrays: Dict[str, Dict[str, Any]] = footer.get("arrays", {})
        self.chunks: List[Dict[str, Any]] = footer.get("chunks", [])
        self._closed = False

    @classmethod
    def open(cls, path: Union[str, Path]) -> "MeshHandle":
        """Open a chunked ``.mphys`` file in O(1) time by reading only header + footer."""
        t0 = time.perf_counter()
        fpath = Path(path)
        fh = open(fpath, "rb")
        mm: Optional[mmap.mmap] = None
        try:
            file_size = os.fstat(fh.fileno()).st_size
            if file_size < HEADER_SIZE_BYTES + 16:
                raise ValueError(f"Corrupt .mphys file (too small: {file_size} bytes)")
            mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
            if mm[0:8] != MPHYS2_HEADER_MAGIC:
                raise ValueError(
                    f"Unsupported or invalid magic {mm[0:8]!r}; expected {MPHYS2_HEADER_MAGIC!r}"
                )

            version, endian, footer_offset, mesh_epoch, flags, chunk_size, num_chunks = (
                struct.unpack_from("<HHQQIII", mm, 8)
            )
            trailer_pos = file_size - 16
            footer_size = struct.unpack_from("<Q", mm, trailer_pos)[0]
            closing_magic = mm[trailer_pos + 8 : file_size]
            if closing_magic != MPHYS_FOOTER_MAGIC:
                raise ValueError(
                    f"Invalid closing magic {closing_magic!r}; expected {MPHYS_FOOTER_MAGIC!r}"
                )
            if footer_offset + footer_size > trailer_pos:
                raise ValueError("Corrupt footer offset/size in .mphys container")

            footer = json.loads(
                mm[footer_offset : footer_offset + footer_size].decode("utf-8")
            )
            header = {
                "version": version,
                "endianness": endian,
                "footer_offset": footer_offset,
                "mesh_epoch_id": mesh_epoch,
                "flags": flags,
                "chunk_size": chunk_size,
                "num_chunks": num_chunks,
            }
            open_ms = (time.perf_counter() - t0) * 1000.0
            return cls(fpath, fh, mm, header, footer, open_ms)
        except Exception:
            if mm is not None:
                try:
                    mm.close()
                except BufferError:
                    pass
            fh.close()
            raise

    def _ensure_open(self) -> mmap.mmap:
        if self._closed or self._mmap is None:
            raise RuntimeError("MeshHandle is closed")
        return self._mmap

    def _array_view(self, name: str) -> np.ndarray:
        mm = self._ensure_open()
        if name not in self._arrays:
            raise KeyError(f"Array '{name}' is not present in {self.path}")
        meta = self._arrays[name]
        shape = tuple(int(s) for s in meta["shape"])
        dtype = np.dtype(meta["dtype"])
        count = 1
        for dim in shape:
            count *= dim
        if count == 0:
            return np.empty(shape, dtype=dtype)
        arr = np.frombuffer(mm, dtype=dtype, count=count, offset=int(meta["offset"]))
        return arr.reshape(shape)

    def has_array(self, name: str) -> bool:
        return name in self._arrays

    def points_view(self) -> np.ndarray:
        return self._array_view("points")

    def owner_view(self) -> np.ndarray:
        return self._array_view("owner")

    def neighbour_view(self) -> np.ndarray:
        return self._array_view("neighbour")

    def face_areas_view(self) -> np.ndarray:
        return self._array_view("face_areas")

    def face_centres_view(self) -> np.ndarray:
        return self._array_view("face_centres")

    def cell_centres_view(self) -> np.ndarray:
        return self._array_view("cell_centres")

    def cell_volumes_view(self) -> np.ndarray:
        return self._array_view("cell_volumes")

    def delta_f_view(self) -> np.ndarray:
        return self._array_view("delta_f")

    def k_f_view(self) -> np.ndarray:
        return self._array_view("k_f")

    def cell_regions_view(self) -> Optional[np.ndarray]:
        if "cell_regions" not in self._arrays:
            return None
        return self._array_view("cell_regions")

    def read_chunk(self, chunk_idx: int) -> Dict[str, Any]:
        """Return zero-copy NumPy views sliced to a single 65,536-cell chunk."""
        self._ensure_open()
        if chunk_idx < 0 or chunk_idx >= self.num_chunks:
            raise IndexError(f"Chunk index {chunk_idx} out of range [0, {self.num_chunks})")

        ch = self.chunks[chunk_idx]
        s = int(ch["cell_start"])
        c = int(ch["cell_count"])

        out: Dict[str, Any] = {
            "chunk_id": int(ch["chunk_id"]),
            "cell_start": s,
            "cell_count": c,
            "byte_offset": int(ch["byte_offset"]),
            "aabb": list(ch["aabb"]),
            "cell_volumes": self.cell_volumes_view()[s : s + c],
            "cell_centres": self.cell_centres_view()[s : s + c],
        }
        regions = self.cell_regions_view()
        if regions is not None:
            out["cell_regions"] = regions[s : s + c]
        return out

    def to_unstructured_mesh(self) -> UnstructuredMesh:
        """Materialize a full in-memory ``UnstructuredMesh`` (copied from mmap)."""
        self._ensure_open()

        face_offsets = self._array_view("face_offsets").copy() if self.has_array("face_offsets") else None
        face_nodes = self._array_view("face_nodes").copy() if self.has_array("face_nodes") else None
        cell_offsets = self._array_view("cell_offsets").copy() if self.has_array("cell_offsets") else None
        cell_nodes = self._array_view("cell_nodes").copy() if self.has_array("cell_nodes") else None

        cells: Union[List[np.ndarray], CSRSequence]
        if cell_offsets is not None and cell_nodes is not None:
            cells = CSRSequence(cell_offsets, cell_nodes)
        else:
            cells = []

        faces: Optional[CSRSequence] = None
        if face_offsets is not None and face_nodes is not None:
            faces = CSRSequence(face_offsets, face_nodes)

        cell_regions = self._array_view("cell_regions").copy() if self.has_array("cell_regions") else None
        cell_types = self._array_view("cell_types").copy() if self.has_array("cell_types") else None

        return UnstructuredMesh(
            points=self.points_view().copy(),
            cells=cells,
            owner=self.owner_view().copy(),
            neighbour=self.neighbour_view().copy(),
            face_areas=self.face_areas_view().copy(),
            face_centres=self.face_centres_view().copy(),
            cell_centres=self.cell_centres_view().copy(),
            cell_volumes=self.cell_volumes_view().copy(),
            delta_f=self.delta_f_view().copy(),
            k_f=self.k_f_view().copy(),
            boundaries=dict(self.boundaries),
            cell_regions=cell_regions,
            faces=faces,
            cell_types=cell_types,
            topological_dim=int(self.footer.get("topological_dim", 3)),
            face_offsets=face_offsets,
            face_nodes=face_nodes,
            cell_offsets=cell_offsets,
            cell_nodes=cell_nodes,
        )

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._mmap is not None:
            try:
                self._mmap.close()
            except BufferError:
                pass
            self._mmap = None
        if self._file is not None and not self._file.closed:
            self._file.close()
            self._file = None

    def __enter__(self) -> "MeshHandle":
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()
