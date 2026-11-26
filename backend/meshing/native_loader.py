"""Native C++20 shared library loader, C-ABI structs, and status error mapping."""

from __future__ import annotations

import ctypes
import json
import logging
import os
from pathlib import Path
import sys
from typing import Any, List, NoReturn, Optional

logger = logging.getLogger(__name__)

_NATIVE_DLL: Optional[ctypes.CDLL] = None
_DLL_LOAD_ATTEMPTED: bool = False
_NATIVE_LOAD_ERROR: Optional[str] = None


def _env_strict_native() -> bool:
    return os.environ.get("CFD_STRICT_NATIVE", "").strip().lower() in ("1", "true", "yes")


def get_native_load_error() -> Optional[str]:
    """Return the last error message encountered during native library loading, if any."""
    global _NATIVE_LOAD_ERROR
    return _NATIVE_LOAD_ERROR


def _reset_native_dll_cache() -> None:
    """Reset cached DLL and load state (primarily for testing error paths)."""
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED, _NATIVE_LOAD_ERROR
    _NATIVE_DLL = None
    _DLL_LOAD_ATTEMPTED = False
    _NATIVE_LOAD_ERROR = None


class MeshingConfigC(ctypes.Structure):
    _fields_ = [
        ("h_min", ctypes.c_double),
        ("h_max", ctypes.c_double),
        ("h0", ctypes.c_double),
        ("grading", ctypes.c_double),
        ("alpha_curv", ctypes.c_double),
        ("beta_prox", ctypes.c_double),
        ("num_bl_layers", ctypes.c_int32),
        ("bl_first_height", ctypes.c_double),
        ("bl_growth_rate", ctypes.c_double),
        ("bl_pinching_enabled", ctypes.c_int32),
        ("bl_pinching_angle_deg", ctypes.c_double),
        ("enable_rcm", ctypes.c_int32),
        ("enable_cht", ctypes.c_int32),
        ("enable_cyclic", ctypes.c_int32),
        ("cyclic_angle_rad", ctypes.c_double),
        ("distmesh_iters", ctypes.c_int32),
    ]


class BoundaryPatchC(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 64),
        ("patch_type", ctypes.c_char * 32),
        ("start_face", ctypes.c_int64),
        ("num_faces", ctypes.c_int64),
        ("region_id", ctypes.c_int32),
    ]


def _find_native_library() -> Optional[Path]:
    """Locate the native cfd_mesher_native shared library across platforms and build directories."""
    custom_env_path = os.environ.get("CFD_NATIVE_LIB_PATH")
    if custom_env_path:
        cand = Path(custom_env_path)
        if cand.is_file():
            return cand
        logger.error(f"CFD_NATIVE_LIB_PATH specified but file does not exist: {custom_env_path}")
        return None

    pkg_dir = Path(__file__).resolve().parent
    build_dir = pkg_dir / "native" / "build"

    names: List[str]
    if sys.platform.startswith("win"):
        names = ["cfd_mesher_native.dll", "libcfd_mesher_native.dll"]
    elif sys.platform == "darwin":
        names = ["libcfd_mesher_native.dylib", "cfd_mesher_native.dylib"]
    else:
        names = ["libcfd_mesher_native.so", "cfd_mesher_native.so"]

    search_dirs = [
        build_dir / "Release",
        build_dir / "RelWithDebInfo",
        build_dir,
        pkg_dir,
        build_dir / "Debug",
        pkg_dir / "native",
    ]

    # Pick newest existing candidate across build/Release and pkg_dir (AUD-037)
    candidates: List[Path] = []
    for s_dir in search_dirs:
        for name in names:
            cand = s_dir / name
            if cand.is_file():
                candidates.append(cand)

    if not candidates:
        return None
    candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return candidates[0]


def _verify_loaded_dll_abi(dll: ctypes.CDLL) -> None:
    """Verify ABI version, struct sizeof, and field offsets on load (Blueprint §3.1, AUD-037)."""
    if not hasattr(dll, "cfd_abi_layout_json"):
        return

    dll.cfd_abi_layout_json.restype = ctypes.c_char_p
    dll.cfd_abi_layout_json.argtypes = []
    raw = dll.cfd_abi_layout_json()
    if not raw:
        raise RuntimeError("cfd_abi_layout_json() returned NULL.")
    layout = json.loads(raw.decode("utf-8"))
    if int(layout.get("abi_version", -1)) != 2:
        raise RuntimeError(
            f"C-ABI version mismatch: native={layout.get('abi_version')}, python=2. "
            "Rebuild via: python tools/dev.py build"
        )
    py_structs = {
        "MeshingConfigC": MeshingConfigC,
        "BoundaryPatchC": BoundaryPatchC,
    }
    for sname, cls in py_structs.items():
        n_info = layout["structs"][sname]
        if ctypes.sizeof(cls) != int(n_info["size"]):
            raise RuntimeError(
                f"C-ABI sizeof({sname}) mismatch: python={ctypes.sizeof(cls)}, native={n_info['size']}"
            )
        for fname, n_off in n_info["offsets"].items():
            py_off = getattr(cls, fname).offset
            if py_off != int(n_off):
                raise RuntimeError(
                    f"C-ABI offsetof({sname}.{fname}) mismatch: python={py_off}, native={n_off}"
                )


def _configure_dll_signatures(dll: ctypes.CDLL) -> None:
    """Register ctypes argtypes and restype for all exported C-ABI symbols."""
    if hasattr(dll, "cfd_mesher_abi_version"):
        dll.cfd_mesher_abi_version.restype = ctypes.c_int32
        dll.cfd_mesher_abi_version.argtypes = []

    if hasattr(dll, "cfd_mesher_last_error"):
        dll.cfd_mesher_last_error.restype = ctypes.c_int32
        dll.cfd_mesher_last_error.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]

    _verify_loaded_dll_abi(dll)

    dll.cfd_mesher_create.restype = ctypes.c_void_p
    dll.cfd_mesher_create.argtypes = []

    dll.cfd_mesher_free.restype = None
    dll.cfd_mesher_free.argtypes = [ctypes.c_void_p]

    dll.cfd_mesher_build_from_cells.restype = ctypes.c_int32
    dll.cfd_mesher_build_from_cells.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int64,
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.c_int64,
        ctypes.POINTER(ctypes.c_int32),
    ]

    for fn in (
        "cfd_mesher_get_n_points",
        "cfd_mesher_get_n_cells",
        "cfd_mesher_get_n_faces",
        "cfd_mesher_get_n_internal_faces",
        "cfd_mesher_get_n_patches",
    ):
        getattr(dll, fn).restype = ctypes.c_int64
        getattr(dll, fn).argtypes = [ctypes.c_void_p]

    for fn in (
        "cfd_mesher_get_points_ptr",
        "cfd_mesher_get_face_areas_ptr",
        "cfd_mesher_get_face_centres_ptr",
        "cfd_mesher_get_cell_centres_ptr",
        "cfd_mesher_get_cell_volumes_ptr",
        "cfd_mesher_get_delta_f_ptr",
        "cfd_mesher_get_k_f_ptr",
        "cfd_mesher_get_non_orthogonality_ptr",
        "cfd_mesher_get_skewness_ptr",
        "cfd_mesher_get_aspect_ratio_ptr",
    ):
        getattr(dll, fn).restype = ctypes.POINTER(ctypes.c_double)
        getattr(dll, fn).argtypes = [ctypes.c_void_p]

    for fn in (
        "cfd_mesher_get_owner_ptr",
        "cfd_mesher_get_neighbour_ptr",
        "cfd_mesher_get_cell_offsets_ptr",
        "cfd_mesher_get_cell_nodes_ptr",
    ):
        getattr(dll, fn).restype = ctypes.POINTER(ctypes.c_int64)
        getattr(dll, fn).argtypes = [ctypes.c_void_p]

    for fn in ("cfd_mesher_get_face_offsets_ptr", "cfd_mesher_get_face_nodes_ptr"):
        if hasattr(dll, fn):
            getattr(dll, fn).restype = ctypes.POINTER(ctypes.c_int64)
            getattr(dll, fn).argtypes = [ctypes.c_void_p]

    if hasattr(dll, "cfd_mesher_get_n_face_nodes"):
        dll.cfd_mesher_get_n_face_nodes.restype = ctypes.c_int64
        dll.cfd_mesher_get_n_face_nodes.argtypes = [ctypes.c_void_p]

    if hasattr(dll, "cfd_mesher_get_cell_types_ptr"):
        dll.cfd_mesher_get_cell_types_ptr.restype = ctypes.POINTER(ctypes.c_int32)
        dll.cfd_mesher_get_cell_types_ptr.argtypes = [ctypes.c_void_p]

    if hasattr(dll, "cfd_mesher_get_topological_dim"):
        dll.cfd_mesher_get_topological_dim.restype = ctypes.c_int32
        dll.cfd_mesher_get_topological_dim.argtypes = [ctypes.c_void_p]

    for fn in ("cfd_mesher_get_stabilization_mask_ptr", "cfd_mesher_get_cell_regions_ptr"):
        getattr(dll, fn).restype = ctypes.POINTER(ctypes.c_int32)
        getattr(dll, fn).argtypes = [ctypes.c_void_p]

    for fn in (
        "cfd_mesher_get_m_skew_ptr",
        "cfd_mesher_get_k_f_comp_ptr",
        "cfd_mesher_get_beta_upwind_ptr",
        "cfd_mesher_get_omega_cell_ptr",
        "cfd_mesher_get_wsrd_alpha_ptr",
    ):
        if hasattr(dll, fn):
            getattr(dll, fn).restype = ctypes.POINTER(ctypes.c_double)
            getattr(dll, fn).argtypes = [ctypes.c_void_p]

    dll.cfd_mesher_get_boundary_patch.restype = ctypes.c_int32
    dll.cfd_mesher_get_boundary_patch.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int64,
        ctypes.POINTER(BoundaryPatchC),
    ]

    if hasattr(dll, "cfd_mesher_extract_cyclic_pairs"):
        dll.cfd_mesher_extract_cyclic_pairs.restype = ctypes.c_int64
        dll.cfd_mesher_extract_cyclic_pairs.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.POINTER(ctypes.c_int64),
            ctypes.POINTER(ctypes.c_int64),
            ctypes.c_int64,
        ]

    if hasattr(dll, "cfd_mesher_generate_hexcore"):
        dll.cfd_mesher_generate_hexcore.restype = ctypes.c_int32
        dll.cfd_mesher_generate_hexcore.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_int64,
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_int32,
            ctypes.c_double,
            ctypes.c_void_p,
        ]

    dll.cfd_mesher_compute_max_divergence_closure.restype = ctypes.c_double
    dll.cfd_mesher_compute_max_divergence_closure.argtypes = [ctypes.c_void_p]

    dll.cfd_mesher_compute_max_decomposition_error.restype = ctypes.c_double
    dll.cfd_mesher_compute_max_decomposition_error.argtypes = [ctypes.c_void_p]


def get_native_dll(strict_native: bool = False) -> Optional[ctypes.CDLL]:
    """Load and return the native cfd_mesher_native shared library if available."""
    global _NATIVE_DLL, _DLL_LOAD_ATTEMPTED, _NATIVE_LOAD_ERROR
    is_strict = strict_native or _env_strict_native()

    if _DLL_LOAD_ATTEMPTED:
        if _NATIVE_DLL is not None:
            return _NATIVE_DLL
        if is_strict:
            raise RuntimeError(
                f"Strict native mode enabled but native mesher library cfd_mesher_native is unavailable. "
                f"Previous load failure: {_NATIVE_LOAD_ERROR}"
            )
        return None

    _DLL_LOAD_ATTEMPTED = True
    dll_path = _find_native_library()
    if dll_path is None:
        _NATIVE_LOAD_ERROR = (
            "Native shared library file (cfd_mesher_native) could not be located in search paths. "
            "Ensure the library is built with CMake (backend/meshing/native/build)."
        )
        logger.error(f"Failed to find native mesher library: {_NATIVE_LOAD_ERROR}")
        if is_strict:
            raise RuntimeError(f"Strict native mode enabled but native library not found: {_NATIVE_LOAD_ERROR}")
        return None

    try:
        dll = ctypes.CDLL(str(dll_path))
        _configure_dll_signatures(dll)
        _NATIVE_DLL = dll
        _NATIVE_LOAD_ERROR = None
        return dll
    except Exception as e:
        _NATIVE_LOAD_ERROR = f"{type(e).__name__}: {e}"
        logger.error(f"Failed to load native mesher library from {dll_path}: {_NATIVE_LOAD_ERROR}", exc_info=True)
        _NATIVE_DLL = None
        if is_strict:
            raise RuntimeError(
                f"Strict native mode enabled but native mesher library failed to load from {dll_path}: {_NATIVE_LOAD_ERROR}"
            ) from e
        return None


def require_native_dll() -> ctypes.CDLL:
    """Load and return the native library, raising RuntimeError if unavailable."""
    dll = get_native_dll(strict_native=True)
    if dll is None:
        raise RuntimeError(f"Native mesher library is unavailable: {_NATIVE_LOAD_ERROR}")
    return dll


def get_last_native_error(dll: Any, mesh_handle: Any) -> str:
    """Retrieve diagnostic error string from cfd_mesher_last_error() if available."""
    if dll is None or not hasattr(dll, "cfd_mesher_last_error"):
        return ""
    try:
        err_buf = ctypes.create_string_buffer(512)
        dll.cfd_mesher_last_error(mesh_handle, err_buf, 512)
        return err_buf.value.decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def raise_native_status_error(
    dll: Any, mesh_handle: Any, func_name: str, status: int
) -> NoReturn:
    """Map non-zero C-ABI status codes to explicit Python exceptions with diagnostics."""
    detail = get_last_native_error(dll, mesh_handle)
    status_int = int(status)
    if status_int == -2:
        raise MemoryError(
            f"Native mesher out of memory (status=-2): {detail}"
            if detail
            else "Native mesher out of memory (status=-2)"
        )
    if status_int == -12:
        raise MemoryError(
            f"Native octree_hexcore exceeded memory/node budget (code={status_int}"
            + (f", {detail}" if detail else "")
            + "; set CFD_MAX_OCTREE_NODES to adjust). Check CAD unit scale or h0."
        )
    if status_int in (-13, -14):
        raise RuntimeError(
            f"Native mesher convergence failure (status={status_int}): {detail}"
            if detail
            else f"Native mesher convergence failure (status={status_int})"
        )
    raise RuntimeError(
        f"Native {func_name} failed with code {status_int}"
        + (f": {detail}" if detail else "")
    )
