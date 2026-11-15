# Changes: Worker M2_PHASE2 (Build & Plattform - OpenMP & Native Loader)

## Summary of Changes
Resolved audit finding H1 across native CMake build system and Python native library loaders:
1. Configured CMake to find and link OpenMP explicitly using standard CMake targets.
2. Hardened native library loading and error capture across `difvm_topology.py` and `mesh_engine.py`, ensuring fail-fast behavior with explicit `RuntimeError` whenever `CFD_STRICT_NATIVE=1` or `strict_native=True`.
3. Added comprehensive automated test suite `tests/test_native_loader.py`.

---

## Detailed File Modifications

### 1. `backend/meshing/native/CMakeLists.txt`
- **Added:** `find_package(OpenMP REQUIRED)` to locate compiler and linker OpenMP flags portably across MSVC, GCC, and Clang.
- **Added:** `target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)` ensuring `vcomp.lib` (MSVC) or `-lgomp` (Linux GCC) is linked to the shared library target.
- **Result:** Resolves Linux runtime error `undefined symbol: GOMP_loop_nonmonotonic_dynamic_next` and guarantees OpenMP runtime symbols are fully resolved. Compiled Release `cfd_mesher_native.dll` cleanly via CMake/MSVC.

### 2. `backend/meshing/difvm_topology.py`
- **Error Tracking:** Introduced `_NATIVE_LOAD_ERROR: Optional[str] = None` and public accessor `get_native_load_error() -> Optional[str]`.
- **Cache Invalidation:** Added `_reset_native_dll_cache()` to reset load attempts and cached errors for unit testing and dynamic reloading.
- **Path Override:** Extended `_find_native_library()` to respect `CFD_NATIVE_LIB_PATH` environment variable if specified.
- **Loader Hardening:**
  - Updated `get_native_dll(strict_native: bool = False)`:
    - Captures detailed exception class and message (`f"{type(e).__name__}: {e}"`) upon load failure into `_NATIVE_LOAD_ERROR`.
    - Logs errors via `logger.error` with stack trace.
    - If `strict_native=True` or `CFD_STRICT_NATIVE=1`, raises `RuntimeError` with comprehensive diagnostic message instead of returning `None`.
  - Added `require_native_dll() -> ctypes.CDLL` helper that always invokes `get_native_dll(strict_native=True)`.
  - Updated `compute_difvm_topology_native` and `generate_hexcore_native` to invoke `get_native_dll(strict_native=True)` and include `_NATIVE_LOAD_ERROR` in any `RuntimeError`.
  - Updated `compute_difvm_topology`:
    - Checks `is_strict = strict_native or (os.environ.get("CFD_STRICT_NATIVE", "0").strip() == "1")`.
    - Disallows `strict_native=True` with `use_native=False` (raises `RuntimeError`).
    - Passes `strict_native=is_strict` to `get_native_dll`.
    - Catches exceptions from native core in strict mode and re-raises `RuntimeError(f"Native mesher execution failed in strict mode: {e}")` instead of quietly falling back to Python.
    - If boundary patches or face IDs prevent native execution in strict mode, raises `RuntimeError` explaining the limitation.

### 3. `backend/meshing/mesh_engine.py`
- **Import:** Imported `get_native_load_error` from `backend.meshing.difvm_topology`.
- **Constructor:**
  - Validates `strict_native=True` requires `use_native=True`.
  - Calls `get_native_dll(strict_native=self.strict_native)`.
  - Raises `RuntimeError` immediately upon `MeshEngine` instantiation if `strict_native` is active and the DLL fails to load.
- **Execution Guardrails:**
  - In `mesh_hexcore` and `_mesh_external_fluid`:
    - Promoted fallback warnings to `logger.error`.
    - Raised explicit `RuntimeError` including `get_native_load_error()` if native mesher fails or is unavailable in strict mode.
  - In `mesh_2d_polygon`:
    - Evaluated `is_strict`.
    - Handled non-zero error codes from `cfd_mesher_generate_2d_polygon` with `RuntimeError` under strict mode.
    - Caught execution exceptions and raised `RuntimeError` in strict mode instead of silently falling back to pure Python ear-clipping.

### 4. `tests/test_native_loader.py`
- Added 5 new automated tests:
  1. `test_cmake_openmp_linkage`: Asserts `find_package(OpenMP REQUIRED)` and `target_link_libraries(... OpenMP::OpenMP_CXX)` are present in `CMakeLists.txt`.
  2. `test_native_dll_loads_successfully`: Verifies successful DLL loading and `require_native_dll()`.
  3. `test_strict_native_mode_bad_path_raises_runtime_error`: Verifies that invalid DLL paths are captured in `get_native_load_error()` in non-strict mode, and raise `RuntimeError` under `strict_native=True` and `CFD_STRICT_NATIVE=1`.
  4. `test_mesh_engine_strict_native_validation`: Verifies `MeshEngine` strict mode constraints and fail-fast initialization.
  5. `test_compute_difvm_topology_strict_native`: Verifies topology generation in strict mode, failure on `use_native=False`, and fail-fast exceptions when native library is unavailable.
