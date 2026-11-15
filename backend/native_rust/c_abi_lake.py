"""C-ABI Structure-of-Arrays (SoA) Data Lake Interface (SUGGESTIONS_BACKEND.md §11.2).

Provides universal 64-byte-aligned C-ABI data lake descriptors:
    typedef struct {
        void*    ptr;        /* 64-byte aligned data pointer */
        size_t   len;        /* Element count */
        uint32_t dtype;      /* 0=f64, 1=f32, 2=i64, 3=i32, 4=u8 */
        uint32_t device_id;  /* 0=host_cpu, 1+=gpu_device */
    } CAbiSpan;

Features:
- Strict 64-byte cache-line alignment enforcement for SIMD / AVX-512 / GPU coalesced access.
- Zero-copy buffer views between Python (NumPy), C++20, and Rust cores.
- Automated export from UnstructuredMesh and simulation solution fields.
"""

from __future__ import annotations

import ctypes
from enum import IntEnum
from typing import Any, Dict, Optional, Tuple, Union
import numpy as np

from backend.meshing.models import UnstructuredMesh


class CAbiDataType(IntEnum):
    """C-ABI Primitive Datatypes."""
    FLOAT64 = 0  # double / f64 (8 bytes)
    FLOAT32 = 1  # float / f32 (4 bytes)
    INT64 = 2    # int64_t / i64 (8 bytes)
    INT32 = 3    # int32_t / i32 (4 bytes)
    UINT8 = 4    # uint8_t / u8 (1 byte)


class CAbiDeviceId(IntEnum):
    """C-ABI Memory Locality Device IDs."""
    HOST_CPU = 0
    GPU_CUDA = 1
    GPU_VULKAN = 2


DTYPE_TO_CABI = {
    np.dtype(np.float64): CAbiDataType.FLOAT64,
    np.dtype(np.float32): CAbiDataType.FLOAT32,
    np.dtype(np.int64): CAbiDataType.INT64,
    np.dtype(np.int32): CAbiDataType.INT32,
    np.dtype(np.uint8): CAbiDataType.UINT8,
}

CABI_TO_NUMPY = {
    CAbiDataType.FLOAT64: np.float64,
    CAbiDataType.FLOAT32: np.float32,
    CAbiDataType.INT64: np.int64,
    CAbiDataType.INT32: np.int32,
    CAbiDataType.UINT8: np.uint8,
}


class CAbiSpan(ctypes.Structure):
    """C-compatible universal contiguous buffer descriptor."""

    _fields_ = [
        ("ptr", ctypes.c_void_p),
        ("len", ctypes.c_size_t),
        ("dtype", ctypes.c_uint32),
        ("device_id", ctypes.c_uint32),
    ]

    def __repr__(self) -> str:
        ptr_val = self.ptr if self.ptr is not None else 0
        aligned = (ptr_val % 64 == 0)
        dtype_name = CAbiDataType(self.dtype).name if self.dtype in CAbiDataType._value2member_map_ else str(self.dtype)
        return (
            f"CAbiSpan(ptr=0x{ptr_val:X}, len={self.len}, "
            f"dtype={dtype_name}, device_id={self.device_id}, aligned_64={aligned})"
        )

    def is_aligned_64(self) -> bool:
        """Verify whether pointer satisfies 64-byte cache-line alignment."""
        if self.ptr is None:
            return False
        return (self.ptr % 64) == 0

    def to_numpy(self, shape: Optional[Tuple[int, ...]] = None) -> np.ndarray:
        """Create a zero-copy NumPy array viewing the buffer memory."""
        if self.ptr is None or self.len == 0:
            np_type = CABI_TO_NUMPY.get(CAbiDataType(self.dtype), np.float64)
            return np.empty(0, dtype=np_type)

        np_type = CABI_TO_NUMPY.get(CAbiDataType(self.dtype))
        if np_type is None:
            raise ValueError(f"Unsupported CAbiDataType enum: {self.dtype}")

        ctype = np.ctypeslib.as_ctypes_type(np_type)
        c_array_type = ctype * self.len
        c_array = c_array_type.from_address(self.ptr)
        arr = np.ctypeslib.as_array(c_array)

        if shape is not None:
            return arr.reshape(shape)
        return arr


def allocate_aligned_array(
    shape: Union[int, Tuple[int, ...]],
    dtype: Any = np.float64,
    align: int = 64,
) -> Tuple[np.ndarray, np.ndarray]:
    """Allocate a NumPy array guaranteed to be aligned to `align` bytes (default 64).

    Returns:
        (aligned_view, raw_backing_buffer)
    """
    dt = np.dtype(dtype)
    shape_tuple = (shape,) if isinstance(shape, int) else tuple(shape)
    element_count = int(np.prod(shape_tuple)) if len(shape_tuple) > 0 else 1
    total_bytes = element_count * dt.itemsize

    # Allocate extra bytes for alignment padding
    raw_buffer = np.empty(total_bytes + align, dtype=np.uint8)
    address = raw_buffer.__array_interface__["data"][0]
    offset = (align - (address % align)) % align

    aligned_view = raw_buffer[offset : offset + total_bytes].view(dtype=dt).reshape(shape_tuple)
    assert aligned_view.__array_interface__["data"][0] % align == 0, "Alignment invariant failed."
    return aligned_view, raw_buffer


def create_cabi_span(
    arr: np.ndarray,
    device_id: int = 0,
    ensure_aligned: bool = True,
) -> Tuple[CAbiSpan, Optional[np.ndarray]]:
    """Create a CAbiSpan descriptor from a NumPy array.

    If `ensure_aligned` is True and the array is not 64-byte aligned,
    creates a 64-byte aligned copy.

    Returns:
        (span, backing_buffer_keeper)
    """
    arr_c = np.ascontiguousarray(arr)
    dt = arr_c.dtype
    cabi_dtype = DTYPE_TO_CABI.get(dt)
    if cabi_dtype is None:
        raise ValueError(f"NumPy dtype {dt} has no direct CAbiDataType mapping.")

    address = arr_c.__array_interface__["data"][0]
    backing_keeper: Optional[np.ndarray] = None

    if ensure_aligned and (address % 64 != 0):
        # Array is not 64-byte aligned, allocate aligned buffer and copy
        aligned_arr, raw_keeper = allocate_aligned_array(arr_c.shape, dtype=dt, align=64)
        np.copyto(aligned_arr, arr_c)
        arr_c = aligned_arr
        address = arr_c.__array_interface__["data"][0]
        backing_keeper = raw_keeper
    else:
        backing_keeper = arr_c

    total_len = arr_c.size
    span = CAbiSpan(
        ptr=address,
        len=total_len,
        dtype=int(cabi_dtype),
        device_id=device_id,
    )
    return span, backing_keeper


def export_mesh_to_soa_lake(
    mesh: UnstructuredMesh,
    device_id: int = 0,
) -> Dict[str, Tuple[CAbiSpan, Any]]:
    """Export UnstructuredMesh into a dictionary of 64-byte aligned CAbiSpan descriptors.

    All geometry and connectivity arrays are exported zero-copy or 64-byte aligned.
    """
    lake: Dict[str, Tuple[CAbiSpan, Any]] = {}

    lake["points"] = create_cabi_span(mesh.points, device_id=device_id)
    lake["owner"] = create_cabi_span(mesh.owner, device_id=device_id)
    lake["neighbour"] = create_cabi_span(mesh.neighbour, device_id=device_id)
    lake["face_areas"] = create_cabi_span(mesh.face_areas, device_id=device_id)
    lake["face_centres"] = create_cabi_span(mesh.face_centres, device_id=device_id)
    lake["cell_centres"] = create_cabi_span(mesh.cell_centres, device_id=device_id)
    lake["cell_volumes"] = create_cabi_span(mesh.cell_volumes, device_id=device_id)
    lake["delta_f"] = create_cabi_span(mesh.delta_f, device_id=device_id)
    lake["k_f"] = create_cabi_span(mesh.k_f, device_id=device_id)

    if mesh.cell_types is not None:
        lake["cell_types"] = create_cabi_span(mesh.cell_types, device_id=device_id)

    # Error map fields
    for k, v in mesh.error_map.items():
        if isinstance(v, np.ndarray):
            lake[f"error_map_{k}"] = create_cabi_span(v, device_id=device_id)

    return lake


def export_fields_to_soa_lake(
    fields: Dict[str, np.ndarray],
    device_id: int = 0,
) -> Dict[str, Tuple[CAbiSpan, Any]]:
    """Export physical solution fields (p, U, T, phi, B) into 64-byte aligned CAbiSpans."""
    lake: Dict[str, Tuple[CAbiSpan, Any]] = {}
    for name, arr in fields.items():
        lake[name] = create_cabi_span(arr, device_id=device_id)
    return lake
