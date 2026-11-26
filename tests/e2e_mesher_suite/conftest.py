"""Pytest configuration, asset registry, CAD fixtures, and isolation helpers for E2E mesher tests."""

from __future__ import annotations

import ctypes
import hashlib
import json
import logging
import os
from pathlib import Path
import subprocess
from typing import Any, Dict, Iterator, Mapping, Optional

import numpy as np
import pytest

from backend.geometry.models import SurfaceMesh
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing.difvm_topology import (
    _reset_native_dll_cache,
    get_native_dll,
    get_native_load_error,
)

logger = logging.getLogger(__name__)

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
_CAD_SAMPLES_DIR: Path = REPO_ROOT / "resources" / "CADTestSamples"
_CAD_COMPLEX_DIR: Path = _CAD_SAMPLES_DIR / "ComplexGeometryExample"
_OPENFOAM_GEO_DIR: Path = (
    REPO_ROOT / "OpenFOAM_base" / "openfoam" / "tutorials" / "resources" / "geometry"
)

_EXT_ASSETS_ENV = os.environ.get("CFD_EXTERNAL_ASSETS", "").strip()
_EXT_ASSETS_DIR = (
    Path(_EXT_ASSETS_ENV)
    if _EXT_ASSETS_ENV
    else Path(r"g:\Datein\Projects\NaturalgasTurbineReformer\CFD\Rev1Basis")
)

def _resolve_fluent_path(filename: str, fallback_rel: str) -> Path:
    if _EXT_ASSETS_ENV:
        return _EXT_ASSETS_DIR / filename
    return _EXT_ASSETS_DIR / fallback_rel

ASSET_PATHS: Dict[str, Path] = {
    # Canonical 11 Tier 4 targets
    "NACA0012": _OPENFOAM_GEO_DIR / "NACA0012.obj.gz",
    "motorBike": _OPENFOAM_GEO_DIR / "motorBike.obj.gz",
    "DTC-scaled": _OPENFOAM_GEO_DIR / "DTC-scaled.stl.gz",
    "cyclone": _OPENFOAM_GEO_DIR / "cyclone.stl.gz",
    "BladeRev1CFD": _CAD_COMPLEX_DIR / "BladeRev1CFD.obj",
    "BladeFullRev1CFD": _CAD_COMPLEX_DIR / "BladeFullRev1CFD.stp",
    "snappyMultiRegionHeater": _OPENFOAM_GEO_DIR / "snappyMultiRegionHeater",
    "cpuCabinet": _OPENFOAM_GEO_DIR / "cpuCabinet",
    "opposite_walls": _OPENFOAM_GEO_DIR / "opposite_walls",
    "FLTG": _resolve_fluent_path(
        "FLTG.msh.h5",
        r"FluentSimOfv1Reformer_files\dp0\FLTG\Fluent\FLTG.msh.h5",
    ),
    "FLTG_1_1": _resolve_fluent_path(
        "FLTG 1.1.msh.h5",
        r"FluentSimOfv1Reformer_files\dp0\FLTG-1\Fluent\FLTG 1.1.msh.h5",
    ),
    # Helpful additional workspace geometry aliases
    "flange": _OPENFOAM_GEO_DIR / "flange.stl.gz",
    "building_wtc2": _OPENFOAM_GEO_DIR / "building_wtc2.obj",
    "fsi-bridge1": _OPENFOAM_GEO_DIR / "fsi-bridge1.obj",
    "TestCAD_obj": _CAD_SAMPLES_DIR / "TestCAD.obj",
    "TestCAD_stp": _CAD_SAMPLES_DIR / "TestCADSTPAP214.stp",
}

_CAD_CACHE_DIR = Path(
    os.environ.get("CFD_CAD_CACHE_DIR", str(REPO_ROOT / ".cache" / "cad"))
)
_CAD_CACHE_ENABLED = os.environ.get("CFD_CAD_CACHE", "1") != "0"

_SURFACE_CACHE: Dict[str, SurfaceMesh] = {}
_DIR_SURFACE_CACHE: Dict[str, Dict[str, SurfaceMesh]] = {}
_NATIVE_ENV_KEYS = (
    "CFD_STRICT_NATIVE",
    "CFD_NATIVE_LIB_PATH",
    "OMP_NUM_THREADS",
)


def pytest_configure(config: pytest.Config) -> None:
    """Register all E2E mesher suite markers with pytest."""
    markers = (
        "e2e: End-to-end CFDSolver mesher verification suite.",
        "tier1: Tier 1 isolated happy-path feature tests.",
        "tier2: Tier 2 boundary, corner, and negative-path tests.",
        "tier3: Tier 3 pairwise cross-feature combination tests.",
        "tier4: Tier 4 real-world CAD, OpenFOAM, and Fluent parity scenarios.",
        "slow: Long-running test (>5s).",
        "scale_5m: Large 5M-12M cell scaling and memory stability tests.",
        "wsl_checkmesh: Requires WSL2 OpenFOAM checkMesh toolchain.",
        "fluent_ref: Compares against Ansys Fluent .msh.h5 reference files.",
        "s13_followup: Section 13 follow-up specification contract tests.",
    )
    for marker_spec in markers:
        config.addinivalue_line("markers", marker_spec)


def _surface_stem(path: Path) -> str:
    """Return canonical stem for plain or gzipped CAD surface files."""
    name = path.name
    if name.lower().endswith(".gz"):
        name = name[:-3]
    return Path(name).stem


def _cad_cache_path(asset_path: Path, deflection: float, min_edge: float) -> Path:
    """Compute content/stat-keyed disk cache path for a tessellated SurfaceMesh."""
    st = asset_path.stat()
    payload = json.dumps(
        ["v1", str(asset_path.resolve()), int(st.st_size), int(st.st_mtime_ns), float(deflection), float(min_edge)],
        separators=(",", ":"),
    ).encode("utf-8")
    digest = hashlib.sha256(payload).hexdigest()[:16]
    return _CAD_CACHE_DIR / f"{_surface_stem(asset_path)}_{digest}.npz"


def _save_surface_npz(path: Path, surf: SurfaceMesh) -> None:
    """Atomically persist a SurfaceMesh losslessly to compressed .npz format."""
    path.parent.mkdir(parents=True, exist_ok=True)
    patch_names = list(surf.patches.keys())
    arrays: Dict[str, Any] = {
        "vertices": np.asarray(surf.vertices, dtype=np.float64),
        "faces": np.asarray(surf.faces, dtype=np.int32),
        "normals": (
            np.asarray(surf.normals, dtype=np.float64)
            if surf.normals is not None
            else np.empty((0, 3), dtype=np.float64)
        ),
        "has_normals": np.array([1 if surf.normals is not None else 0], dtype=np.uint8),
        "patch_names_json": np.array([json.dumps(patch_names)], dtype=object),
        "metadata_json": np.array([json.dumps(surf.metadata or {}, default=str)], dtype=object),
    }
    for idx, pname in enumerate(patch_names):
        arrays[f"patch_{idx}"] = np.asarray(surf.patches[pname], dtype=np.int32)

    tmp_path = path.with_name(f"{path.name}.{os.getpid()}.tmp.npz")
    try:
        with open(tmp_path, "wb") as handle:
            np.savez(handle, **arrays)
        os.replace(tmp_path, path)
    finally:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass


def _load_surface_npz(path: Path) -> Optional[SurfaceMesh]:
    """Load a cached SurfaceMesh from .npz; return None if missing or corrupt."""
    if not path.is_file():
        return None
    try:
        with np.load(path, allow_pickle=True) as data:
            vertices = np.array(data["vertices"], dtype=np.float64, copy=True)
            faces = np.array(data["faces"], dtype=np.int32, copy=True)
            has_normals = bool(int(data["has_normals"][0])) if "has_normals" in data else True
            normals = (
                np.array(data["normals"], dtype=np.float64, copy=True)
                if has_normals and len(data["normals"]) == len(faces)
                else None
            )
            patch_names = json.loads(str(data["patch_names_json"][0]))
            patches: Dict[str, np.ndarray] = {}
            for idx, pname in enumerate(patch_names):
                patches[str(pname)] = np.array(data[f"patch_{idx}"], dtype=np.int32, copy=True)
            metadata = json.loads(str(data["metadata_json"][0]))
        return SurfaceMesh(
            vertices=vertices,
            faces=faces,
            normals=normals,
            patches=patches,
            metadata=metadata if isinstance(metadata, dict) else {},
        )
    except Exception as exc:
        logger.warning("Ignoring corrupt CAD cache %s: %s", path, exc)
        return None


def load_cached_surface(
    key: str,
    *,
    deflection: float = 0.0001,
    min_edge: float = 0.00005,
) -> SurfaceMesh:
    """Load and session/disk-cache a single-file CAD surface, returning a deep copy.

    Never calls ``gmsh.finalize()``. Calls ``pytest.skip`` if an external asset
    file is absent on disk; calls ``pytest.fail`` if the key is unknown or parsing fails.
    """
    if key not in ASSET_PATHS:
        pytest.fail(f"Unknown CAD asset key '{key}'. Known keys: {sorted(ASSET_PATHS)}")

    asset_path = ASSET_PATHS[key]
    if not asset_path.is_file():
        pytest.skip(
            f"External CAD asset '{key}' not present at {asset_path} "
            "(set CFD_EXTERNAL_ASSETS or populate OpenFOAM_base)"
        )

    cache_key = f"{key}|defl={deflection}|min_edge={min_edge}"
    cached = _SURFACE_CACHE.get(cache_key)
    if cached is None and _CAD_CACHE_ENABLED:
        disk_path = _cad_cache_path(asset_path, deflection, min_edge)
        cached = _load_surface_npz(disk_path)
        if cached is not None:
            _SURFACE_CACHE[cache_key] = cached

    if cached is None:
        logger.info("Loading and caching CAD surface '%s' from %s", key, asset_path)
        try:
            engine = CADGeometryEngine()
            engine.load(
                asset_path,
                deflection=deflection,
                min_edge=min_edge,
            )
            if engine.current_mesh is None:
                pytest.fail(
                    f"CADGeometryEngine.load('{asset_path}') produced current_mesh=None"
                )
            cached = engine.current_mesh
            _SURFACE_CACHE[cache_key] = cached
            if _CAD_CACHE_ENABLED:
                try:
                    _save_surface_npz(_cad_cache_path(asset_path, deflection, min_edge), cached)
                except Exception as exc:
                    logger.warning("Failed to write CAD cache for '%s': %s", key, exc)
        except Exception as exc:
            pytest.fail(
                f"Failed to load CAD asset '{key}' from {asset_path}: "
                f"{type(exc).__name__}: {exc}"
            )
    return cached.copy()


def load_cached_directory_surfaces(key: str) -> Dict[str, SurfaceMesh]:
    """Load and session/disk-cache all surface files inside a multi-part directory asset."""
    if key not in ASSET_PATHS:
        pytest.fail(f"Unknown CAD directory asset key '{key}'.")

    asset_dir = ASSET_PATHS[key]
    if not asset_dir.is_dir():
        pytest.skip(
            f"External multi-part CAD directory '{key}' not present at {asset_dir} "
            "(set CFD_EXTERNAL_ASSETS or populate OpenFOAM_base)"
        )

    cached_map = _DIR_SURFACE_CACHE.get(key)
    if cached_map is None:
        supported_suffixes = (".stl", ".stl.gz", ".obj", ".obj.gz")
        files = sorted(
            p
            for p in asset_dir.iterdir()
            if p.is_file() and p.name.lower().endswith(supported_suffixes)
        )
        if not files:
            pytest.fail(
                f"No supported CAD surface files found in directory '{key}': {asset_dir}"
            )
        loaded: Dict[str, SurfaceMesh] = {}
        for file_path in files:
            stem = _surface_stem(file_path)
            disk_surf: Optional[SurfaceMesh] = None
            disk_path: Optional[Path] = None
            if _CAD_CACHE_ENABLED:
                disk_path = _cad_cache_path(file_path, 0.0, 0.0)
                disk_surf = _load_surface_npz(disk_path)
            if disk_surf is not None:
                loaded[stem] = disk_surf
                continue
            try:
                engine = CADGeometryEngine()
                engine.load(file_path)
                if engine.current_mesh is None:
                    pytest.fail(
                        f"CADGeometryEngine.load('{file_path}') produced current_mesh=None"
                    )
                loaded[stem] = engine.current_mesh
                if disk_path is not None:
                    try:
                        _save_surface_npz(disk_path, engine.current_mesh)
                    except Exception as exc:
                        logger.warning("Failed to write CAD cache for '%s': %s", file_path, exc)
            except Exception as exc:
                pytest.fail(
                    f"Failed to load surface file '{file_path}' in '{key}': "
                    f"{type(exc).__name__}: {exc}"
                )
        _DIR_SURFACE_CACHE[key] = loaded
        cached_map = loaded

    return {stem: mesh.copy() for stem, mesh in cached_map.items()}


@pytest.fixture(scope="session")
def asset_paths() -> Mapping[str, Path]:
    """Return read-only mapping of canonical Tier 4 asset keys to filesystem paths."""
    return dict(ASSET_PATHS)


@pytest.fixture(scope="session")
def _session_naca0012_base() -> SurfaceMesh:
    return load_cached_surface("NACA0012")


@pytest.fixture
def naca0012_surface(_session_naca0012_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of NACA0012.obj.gz SurfaceMesh."""
    return _session_naca0012_base.copy()


@pytest.fixture(scope="session")
def _session_motorbike_base() -> SurfaceMesh:
    return load_cached_surface("motorBike")


@pytest.fixture
def motorbike_surface(_session_motorbike_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of motorBike.obj.gz SurfaceMesh."""
    return _session_motorbike_base.copy()


@pytest.fixture(scope="session")
def _session_dtc_scaled_base() -> SurfaceMesh:
    return load_cached_surface("DTC-scaled")


@pytest.fixture
def dtc_scaled_surface(_session_dtc_scaled_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of DTC-scaled.stl.gz SurfaceMesh."""
    return _session_dtc_scaled_base.copy()


@pytest.fixture(scope="session")
def _session_blade_rev1_base() -> SurfaceMesh:
    return load_cached_surface("BladeRev1CFD")


@pytest.fixture
def blade_rev1_surface(_session_blade_rev1_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of BladeRev1CFD.obj SurfaceMesh."""
    return _session_blade_rev1_base.copy()


@pytest.fixture(scope="session")
def _session_blade_full_base() -> SurfaceMesh:
    return load_cached_surface(
        "BladeFullRev1CFD",
        deflection=0.002,
        min_edge=0.001,
    )


@pytest.fixture
def blade_full_surface(_session_blade_full_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of BladeFullRev1CFD.stp SurfaceMesh."""
    return _session_blade_full_base.copy()


@pytest.fixture(scope="session")
def _session_cyclone_base() -> SurfaceMesh:
    return load_cached_surface("cyclone")


@pytest.fixture
def cyclone_surface(_session_cyclone_base: SurfaceMesh) -> SurfaceMesh:
    """Return a fresh deep copy of cyclone.stl.gz SurfaceMesh."""
    return _session_cyclone_base.copy()


@pytest.fixture(scope="session")
def _session_snappy_heater_base() -> Dict[str, SurfaceMesh]:
    return load_cached_directory_surfaces("snappyMultiRegionHeater")


@pytest.fixture
def snappy_heater_surfaces(
    _session_snappy_heater_base: Dict[str, SurfaceMesh],
) -> Dict[str, SurfaceMesh]:
    """Return fresh deep copies of snappyMultiRegionHeater STL surfaces."""
    return {k: v.copy() for k, v in _session_snappy_heater_base.items()}


@pytest.fixture(scope="session")
def _session_cpu_cabinet_base() -> Dict[str, SurfaceMesh]:
    return load_cached_directory_surfaces("cpuCabinet")


@pytest.fixture
def cpu_cabinet_surfaces(
    _session_cpu_cabinet_base: Dict[str, SurfaceMesh],
) -> Dict[str, SurfaceMesh]:
    """Return fresh deep copies of cpuCabinet OBJ surfaces."""
    return {k: v.copy() for k, v in _session_cpu_cabinet_base.items()}


@pytest.fixture(scope="session")
def _session_opposite_walls_base() -> Dict[str, SurfaceMesh]:
    return load_cached_directory_surfaces("opposite_walls")


@pytest.fixture
def opposite_walls_surfaces(
    _session_opposite_walls_base: Dict[str, SurfaceMesh],
) -> Dict[str, SurfaceMesh]:
    """Return fresh deep copies of opposite_walls STL surfaces."""
    return {k: v.copy() for k, v in _session_opposite_walls_base.items()}


@pytest.fixture
def restore_native_env() -> Iterator[None]:
    """Snapshot and restore native DLL environment variables and cache around a test."""
    snapshot: Dict[str, Optional[str]] = {
        key: os.environ.get(key) for key in _NATIVE_ENV_KEYS
    }
    _reset_native_dll_cache()
    try:
        yield
    finally:
        for key, prev_val in snapshot.items():
            if prev_val is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = prev_val
        _reset_native_dll_cache()


@pytest.fixture
def require_native_dll() -> ctypes.CDLL:
    """Load cfd_mesher_native in strict mode or fail explicitly with diagnostics."""
    try:
        dll = get_native_dll(strict_native=True)
    except Exception as exc:
        pytest.fail(
            f"Native mesher DLL unavailable under strict_native=True: "
            f"{type(exc).__name__}: {exc}"
        )
    if dll is None:
        pytest.fail(
            f"Native mesher DLL returned None (load error: {get_native_load_error()})"
        )
    return dll


@pytest.fixture
def require_wsl_checkmesh() -> Dict[str, Any]:
    """Verify WSL2 OpenFOAM checkMesh or scripts/run_wsl_checkmesh.sh is available.

    Fails explicitly via ``pytest.fail`` if neither the wrapper script nor a
    working ``checkMesh`` executable inside WSL2 (``Ubuntu-26.04``) is found.
    """
    wrapper_script = REPO_ROOT / "scripts" / "run_wsl_checkmesh.sh"
    if wrapper_script.is_file():
        return {"mode": "wrapper_script", "script_path": wrapper_script}

    probe_cmd = [
        "wsl",
        "-d",
        "Ubuntu-26.04",
        "--",
        "bash",
        "-lc",
        (
            "for rc in /usr/lib/openfoam/openfoam*/etc/bashrc /opt/openfoam*/etc/bashrc; do "
            "  if [ -f \"$rc\" ]; then source \"$rc\" >/dev/null 2>&1 || true; fi; "
            "done; "
            "command -v checkMesh"
        ),
    ]
    try:
        proc = subprocess.run(
            probe_cmd,
            capture_output=True,
            text=True,
            stdin=subprocess.DEVNULL,
            timeout=15.0,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        pytest.skip(
            f"WSL2 Ubuntu-26.04 checkMesh probe timed out after 15s and "
            f"{wrapper_script} does not exist: {exc}"
        )
    except OSError as exc:
        pytest.skip(
            f"WSL2 checkMesh probe unavailable and {wrapper_script} does not exist: "
            f"{type(exc).__name__}: {exc}"
        )

    if proc.returncode != 0 or not proc.stdout.strip():
        pytest.skip(
            "WSL2 OpenFOAM checkMesh toolchain is unavailable: "
            f"script={wrapper_script} (exists={wrapper_script.is_file()}), "
            f"wsl probe exit={proc.returncode}, stdout={proc.stdout.strip()!r}, "
            f"stderr={proc.stderr.strip()!r}"
        )
    return {"mode": "wsl_direct", "checkmesh_path": proc.stdout.strip()}


@pytest.fixture
def require_fluent_h5() -> Dict[str, Path]:
    """Verify h5py and both Ansys Fluent .msh.h5 reference files are readable."""
    h5py = pytest.importorskip(
        "h5py", reason="h5py not installed; skipping Fluent .msh.h5 parity tests"
    )

    paths: Dict[str, Path] = {}
    for key in ("FLTG", "FLTG_1_1"):
        p = ASSET_PATHS[key]
        if not p.is_file():
            pytest.skip(
                f"External Fluent reference '{key}' not present at {p} "
                "(set CFD_EXTERNAL_ASSETS)"
            )
        try:
            with h5py.File(p, "r") as handle:
                if "meshes" not in handle:
                    pytest.fail(
                        f"Fluent HDF5 file '{key}' at {p} lacks top-level 'meshes' group."
                    )
        except Exception as exc:
            pytest.fail(
                f"Failed to open Fluent reference HDF5 '{key}' at {p}: "
                f"{type(exc).__name__}: {exc}"
            )
        paths[key] = p
    return paths
