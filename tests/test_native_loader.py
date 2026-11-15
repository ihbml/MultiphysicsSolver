import os
from pathlib import Path
import pytest
import numpy as np

from backend.meshing.difvm_topology import (
    get_native_dll,
    get_native_load_error,
    _reset_native_dll_cache,
    require_native_dll,
    compute_difvm_topology,
    compute_difvm_topology_native,
    generate_hexcore_native,
)
from backend.meshing.mesh_engine import MeshEngine


def test_cmake_openmp_linkage():
    """Verify OpenMP package discovery and target linkage in native CMakeLists.txt."""
    cmake_path = Path("backend/meshing/native/CMakeLists.txt")
    assert cmake_path.is_file(), "CMakeLists.txt not found"
    content = cmake_path.read_text(encoding="utf-8")
    assert "find_package(OpenMP REQUIRED)" in content, "find_package(OpenMP REQUIRED) missing in CMakeLists.txt"
    assert "target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)" in content, (
        "OpenMP::OpenMP_CXX link target missing in CMakeLists.txt"
    )


def test_native_dll_loads_successfully():
    """Verify native shared library loads properly and sets no error state."""
    _reset_native_dll_cache()
    dll = get_native_dll(strict_native=True)
    assert dll is not None, "Failed to load cfd_mesher_native shared library"
    assert get_native_load_error() is None
    assert require_native_dll() is not None


def test_strict_native_mode_bad_path_raises_runtime_error(monkeypatch):
    """Verify strict native mode captures load failures and raises RuntimeError without silent fallback."""
    bad_path = "non_existent_library_file.dll"
    monkeypatch.setenv("CFD_NATIVE_LIB_PATH", bad_path)
    _reset_native_dll_cache()

    try:
        # In non-strict mode, returns None but captures error diagnostics
        dll = get_native_dll(strict_native=False)
        assert dll is None
        err = get_native_load_error()
        assert err is not None
        assert "non_existent_library_file.dll" in err or "search paths" in err

        # In strict_native=True, must immediately raise RuntimeError with error detail
        _reset_native_dll_cache()
        with pytest.raises(RuntimeError, match="Strict native mode enabled"):
            get_native_dll(strict_native=True)

        # With CFD_STRICT_NATIVE=1, must also immediately raise RuntimeError
        monkeypatch.setenv("CFD_STRICT_NATIVE", "1")
        _reset_native_dll_cache()
        with pytest.raises(RuntimeError, match="Strict native mode enabled"):
            get_native_dll()

        # require_native_dll must also fail-fast
        with pytest.raises(RuntimeError):
            require_native_dll()

    finally:
        _reset_native_dll_cache()


def test_mesh_engine_strict_native_validation(monkeypatch):
    """Verify MeshEngine strictly validates native loader availability."""
    _reset_native_dll_cache()

    # With native library available, strict_native=True succeeds
    engine = MeshEngine(use_native=True, strict_native=True)
    assert engine.has_native_core is True

    # strict_native=True with use_native=False must raise RuntimeError
    with pytest.raises(RuntimeError, match="strict_native=True requires use_native=True"):
        MeshEngine(use_native=False, strict_native=True)

    # When native library fails to load under strict_native=True, MeshEngine must fail fast
    monkeypatch.setenv("CFD_NATIVE_LIB_PATH", "missing_dll_path.dll")
    _reset_native_dll_cache()
    with pytest.raises(RuntimeError, match="Strict native mode enabled"):
        MeshEngine(use_native=True, strict_native=True)

    _reset_native_dll_cache()


def test_compute_difvm_topology_strict_native(monkeypatch):
    """Verify compute_difvm_topology enforces strict native execution and raises on failure."""
    pts = np.array([
        [0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0],
        [1.0, 1.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [1.0, 0.0, 1.0],
        [1.0, 1.0, 1.0],
        [0.0, 1.0, 1.0],
    ], dtype=np.float64)
    cells = [np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)]

    _reset_native_dll_cache()
    # Native execution succeeds in strict mode
    mesh = compute_difvm_topology(pts, cells, use_native=True, strict_native=True)
    assert mesh is not None
    assert len(mesh.cells) == 1

    # In strict mode, use_native=False must raise RuntimeError
    with pytest.raises(RuntimeError, match="strict_native=True requires use_native=True"):
        compute_difvm_topology(pts, cells, use_native=False, strict_native=True)

    # When library is missing, strict mode must fail fast with RuntimeError
    monkeypatch.setenv("CFD_NATIVE_LIB_PATH", "missing_dll_path.dll")
    _reset_native_dll_cache()
    with pytest.raises(RuntimeError, match="Strict native"):
        compute_difvm_topology(pts, cells, use_native=True, strict_native=True)

    # Native direct helpers must also raise when DLL is unavailable
    with pytest.raises(RuntimeError):
        compute_difvm_topology_native(pts, cells)

    with pytest.raises(RuntimeError):
        generate_hexcore_native(pts, np.array([[0, 1, 2]], dtype=np.int32))

    _reset_native_dll_cache()
