"""C-ABI Schema & Runtime Layout Verifier (Blueprint §3.1).

Validates that `abi/cfd_abi.toml`, Python `ctypes.Structure` definitions in
`backend.meshing.difvm_topology`, and the compiled `cfd_mesher_native` shared
library agree byte-for-byte on `abi_version`, `sizeof`, and every field offset.
"""

from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path
import sys
import tomllib
from typing import Any, Dict

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


def load_abi_schema(schema_path: Path | None = None) -> Dict[str, Any]:
    path = schema_path or (REPO_ROOT / "abi" / "cfd_abi.toml")
    with open(path, "rb") as f:
        return tomllib.load(f)


def verify_abi_against_schema(require_dll: bool = True) -> Dict[str, Any]:
    from backend.meshing.difvm_topology import (
        BoundaryPatchC,
        MeshingConfigC,
        get_native_dll,
    )

    schema = load_abi_schema()
    expected_version = int(schema["abi_version"])
    py_structs = {
        "MeshingConfigC": MeshingConfigC,
        "BoundaryPatchC": BoundaryPatchC,
    }

    for sname, sdef in schema["struct"].items():
        cls = py_structs[sname]
        actual_sz = ctypes.sizeof(cls)
        if actual_sz != int(sdef["size"]):
            raise RuntimeError(
                f"Python ctypes sizeof({sname}) = {actual_sz} != schema {sdef['size']}"
            )
        for fname, _ftype, expected_off in sdef["fields"]:
            actual_off = getattr(cls, fname).offset
            if actual_off != int(expected_off):
                raise RuntimeError(
                    f"Python ctypes {sname}.{fname} offset = {actual_off} != schema {expected_off}"
                )

    dll = get_native_dll(strict_native=require_dll)
    if dll is None:
        return {"abi_version": expected_version, "native_checked": False}

    if not hasattr(dll, "cfd_abi_layout_json"):
        raise RuntimeError(
            "Loaded native library lacks cfd_abi_layout_json() (stale binary). "
            "Rebuild via: .\\.venv\\Scripts\\python.exe tools/dev.py build"
        )

    dll.cfd_abi_layout_json.restype = ctypes.c_char_p
    dll.cfd_abi_layout_json.argtypes = []
    raw_json = dll.cfd_abi_layout_json()
    if not raw_json:
        raise RuntimeError("cfd_abi_layout_json() returned NULL.")
    native_layout = json.loads(raw_json.decode("utf-8"))

    if int(native_layout["abi_version"]) != expected_version:
        raise RuntimeError(
            f"Native DLL abi_version={native_layout['abi_version']} != schema {expected_version}"
        )

    for sname, sdef in schema["struct"].items():
        n_struct = native_layout["structs"][sname]
        if int(n_struct["size"]) != int(sdef["size"]):
            raise RuntimeError(
                f"Native sizeof({sname})={n_struct['size']} != schema {sdef['size']}"
            )
        for fname, _ftype, expected_off in sdef["fields"]:
            n_off = int(n_struct["offsets"][fname])
            if n_off != int(expected_off):
                raise RuntimeError(
                    f"Native offsetof({sname}, {fname})={n_off} != schema {expected_off}"
                )

    return {"abi_version": expected_version, "native_checked": True, "layout": native_layout}


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify C-ABI struct sizes and offsets.")
    parser.add_argument("--check", action="store_true", help="Verify Python + Native DLL against abi/cfd_abi.toml")
    parser.add_argument("--no-dll", action="store_true", help="Verify Python ctypes against TOML only")
    args = parser.parse_args()

    res = verify_abi_against_schema(require_dll=not args.no_dll)
    print(
        f"[ABI OK] version={res['abi_version']} native_checked={res['native_checked']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
