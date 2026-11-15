# Dispatch for worker_m5c

## Mission & Scope
Implement Milestone M5C: Architecture Synthesis & Super-Elements Integration (Features F62 & F63).

## Mandatory Integrity Warning
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Detailed Objectives

### 1. Feature F62: Combinatorial 23-Class Super-Elements (Stromberg 2023, SUGGESTIONS_BACKEND.md §10)
- **`constexpr` 23-Class Super-Element Table**:
  - Implement the 23 symmetry-reduced canonical ASP-optimized super-element cases derived from 256 8-bit cube outer-node configurations (Stromberg 2023, Appendices B & C).
  - Each super-element decomposes cut hexahedra into well-formed `Hex8`, `Prism6`, `Pyr5`, and `Tet4` elements using candidate points in $[-1, 1]^3$ (including the 8 octant support points $(\pm 0.5, \pm 0.5, \pm 0.5)$).
  - Guarantee $\min SJ \ge 0.302$ across all 23 cases, zero hanging nodes, and zero hanging edges.
  - Implement in `backend/meshing/super_elements.py` and/or C++ core `backend/meshing/native/src/octree_hexcore.cpp`.
- **Asymmetric Residual Volume Voting**:
  - Implement $R(e, D) = (RV_{\text{missing}}(e, D))^2 + RV_{\text{excess}}(e, D)$ for $D \in \{0, \dots, 255\}$.
  - Quadratic penalty for missing solid/fluid volume and linear penalty for excess volume on sub-sampling points.
  - Implement $\mathcal{O}(n_{\text{elem}})$ majority node voting $\sum_{e \ni N} v(e, N) > 0$ yielding global topology without zigzag artifacts.
- **Top-Down Entity Mapping (0, 1, 2, 3 DOF)**:
  - Corner-Nodes (0 DOF): locked to sharp CAD corners.
  - Curve-Nodes (1 DOF): projected to 1D curves, movable only tangentially.
  - Surface-Nodes (2 DOF): projected to CAD surfaces, smoothed tangentially.
  - Volume-Nodes (3 DOF): smoothed freely in 3D to maximize quality.
- **Quality Metric $J_{\text{ENS}}$ & Pyramid Protection**:
  - In `backend/models.py` / `compute_quality_report()` and `difvm_mesh.cpp`:
  - Aspect-ratio-sensitive nodal Scaled Jacobian: $J_{s,i} = \frac{\det(\mathbf{J}_i)}{(\max_k l_k)^3}$.
  - Element Normalized Scaled Jacobian $J_{\text{ENS}}$ normalized to $[-1, 1]$ across Hex8 ($J_{S,\text{opt}} = 1.0$), Prism6 ($\frac{\sqrt{3}}{2} \approx 0.866025$), Tet4 & Pyr5 ($\frac{\sqrt{2}}{2} \approx 0.707107$).
  - Special pyramid protection: fictitious base hexahedron check of height $\bar{l}_{\text{base}}$ detecting non-convex/folded 4-node pyramid bases ($J_{\text{ENS}} \le 0$).

### 2. Feature F63: Architecture Synthesis & `.mphys` Container (SUGGESTIONS_BACKEND.md §11.2)
- **C-ABI Structure-of-Arrays (SoA) Data Lake (`CAbiSpan`)**:
  - Implement `CAbiSpan` / C-ABI data lake interfaces in `backend/native_rust/` and Python ctypes wrapper:
    `(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)` with 64-byte alignment, zero-copy buffer export.
- **Embedded Expression Engine**:
  - Implement expression engine foundations (Rhai / SIMD bytecode evaluator in Rust/Python) for evaluation of generic boundary profiles $u_{\text{in}}(x, y, z, t)$, temperature-dependent properties, and custom source terms without recompilation.
- **Native Binary Container `.mphys`**:
  - Implement chunked binary container reader/writer (`backend/native_rust/` / Python wrapper `mphys_container.py`):
    - 64-byte Header: Magic `MPHYS001`, version, endianness, `uint64_t footer_offset`, `uint64_t mesh_epoch_id`.
    - Static Mesh Epoch (`mesh_epoch_0`): `points`, `faces` (CSR), `owner`, `neighbour`, `face_areas`, `cell_volumes`, `MeshErrorMap` sorted along 3D Hilbert curve in fixed 65,536-cell chunks (Blosc2 / Zstd / LZ4 compression).
    - Dynamic Field Steps: fields ($p, \mathbf{u}, T, \phi, \mathbf{B}$) in matching 65,536-cell chunks.
    - Spatial Chunk Index: AABB $[x_{\min}, x_{\max}]^3$ per chunk for frustum/space skipping.
    - Footer: Magic `MPHYSEOF`, schema, chunk offsets, SI dimensions hash, SHA-256 checksums. Sub-millisecond open time.

## Verification Requirements
- Create comprehensive test suite in `tests/test_super_elements_and_architecture_m5c.py`.
- Run sequential pytest under `OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`:
  `.\.venv\Scripts\python.exe -m pytest tests/test_super_elements_and_architecture_m5c.py -v`
- Also run regression suites (`tests/test_paper_integration_m5b.py`, `tests/test_generic_reaction_and_openfoam_audits.py`, `tests/test_wp4_multiphysics_hexcore.py`) to verify zero regressions.
- Write full self-contained `handoff.md` in `.agents/teamwork/worker_m5c/handoff.md`.


## 2026-11-14T00:10:05Z
[Message] timestamp=2026-11-14T00:10:05Z sender=ea8f7ac6-187f-461c-b8f2-aeda0693e3d8 priority=MESSAGE_PRIORITY_HIGH content=You are worker_m5c, a teamwork_worker responsible for implementing Milestone M5C: Architecture Synthesis & Super-Elements Integration (Features F62 & F63).
