# AGENTS.md — Normative Guide for Coding Agents

## 1. Repository Map
- `backend/geometry/` — CAD ingestion (`STEPParser`, `STLParser`, `OBJParser`), unit conversion to SI (`m`), surface auditing, and `geometry_sdf.py`.
- `backend/meshing/` — Python meshing orchestration (`mesh_engine.py`, `difvm_topology.py`, `models.py`, `polymesh_io.py`, `invariants.py`).
- `backend/meshing/native/` — C++20/OpenMP meshing & SDF core (`octree_hexcore.cpp`, `difvm_mesh.cpp`, `sdf_engine.cpp`, `cfd_mesher_native.cpp`).
- `backend/native_rust/foam_fast_io/` — Rust OpenFOAM / `.mphys` I/O crate (`ffi.rs`, `foam_reader.py`).
- `backend/physics_modules/` & `backend/solver/` — Multiphysics module specs, OpenFOAM case generation, and solver drivers.
- `backend/ipc/` — JSON-RPC / NDJSON telemetry and async job service for the frontend.
- `abi/cfd_abi.toml` & `tools/abi_gen.py` — Single-source C-ABI schema & layout verifier.
- `docs/` — Architectural & audit synthesis (`docs/BLUEPRINT_AND_AUDIT_SUMMARY.md`) and development history.

## 2. Core Developer & Agent Commands (Windows PowerShell)
Always use the workspace virtual environment `.\.venv\Scripts\python.exe`:

```powershell
.\.venv\Scripts\python.exe tools/dev.py build            # Build C++20 DLL + verify ABI layout
.\.venv\Scripts\python.exe tools/dev.py verify           # Build + fast L0 invariant & ABI tests (< 60s)
.\.venv\Scripts\python.exe tools/dev.py verify --full    # Run L0 + E2E mesher suite (excluding missing external assets)
.\.venv\Scripts\python.exe tools/abi_gen.py --check      # Check C-ABI struct sizes & field offsets against native DLL
```

## 3. Non-Negotiable Rules
1. **Read Contracts First:** Check [`INVARIANTS.md`](INVARIANTS.md) and [`CONTRACTS.md`](CONTRACTS.md) before changing meshing, C-ABI, or I/O code.
2. **Never Overwrite Ground Truth in Tests:** Never manually assign `mesh.boundaries` or `mesh.cell_regions` in end-to-end tests to mask mesher output; verify with `backend.meshing.invariants.assert_fvm_invariants`.
3. **Never Commit Compiled Binaries:** `cfd_mesher_native.dll` / `.so` / `.dylib` are build artifacts verified at load time via `cfd_abi_layout_json()`.
4. **Strict SI & Explicit Layouts:** Internal coordinates are SI metres (`m`). In C-ABI, `farfield_bbox` is strictly `double[6] = {xmin, ymin, zmin, xmax, ymax, zmax}`.
5. **Memory & OpenMP Safety:** No `std::string` allocations per face in hot OpenMP loops; every `#pragma omp parallel` block that allocates must catch exceptions locally and set an atomic failure flag instead of terminating the host process.
6. **File Size Ratchet:** Keep new modules focused (< 800 lines); decompose monoliths along pass boundaries (`HexcoreState` / `MeshPass`) while preserving `mesh_fingerprint`.
