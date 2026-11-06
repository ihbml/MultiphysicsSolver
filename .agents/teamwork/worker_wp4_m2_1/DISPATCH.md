## 2026-11-04T00:42:22Z
You are worker_wp4_m2_1, a teamwork_worker subagent.
Your working directory is G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_wp4_m2_1\.

CRITICAL: First, read the authoritative user request at:
G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Also read:
- G:\Datein\Projects\CFDSolver\PROJECT.md
- G:\Datein\Projects\CFDSolver\TEST_INFRA.md
- G:\Datein\Projects\CFDSolver\.agents\teamwork\spec_miner_wp4_1\analysis.md
- G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_survey_wp4_1\analysis.md
- G:\Datein\Projects\CFDSolver\.agents\teamwork\worker_wp4_m1_1\handoff.md

Your exclusive file ownership:
- G:\Datein\Projects\CFDSolver\backend\meshing\native\include\octree_hexcore.hpp
- G:\Datein\Projects\CFDSolver\backend\meshing\native\src\octree_hexcore.cpp
- G:\Datein\Projects\CFDSolver\backend\meshing\native\include\cfd_mesher_native.h
- G:\Datein\Projects\CFDSolver\backend\meshing\native\src\cfd_mesher_native.cpp
- G:\Datein\Projects\CFDSolver\backend\meshing\native\src\difvm_mesh.cpp
- G:\Datein\Projects\CFDSolver\backend\meshing\native\src\distmesh.cpp
- G:\Datein\Projects\CFDSolver\backend\meshing\native\CMakeLists.txt
- G:\Datein\Projects\CFDSolver\backend\meshing\mesh_engine.py (hooking into native hexcore)

Your mission:
Implement Milestone M4.2 (R2: 3D Mosaic Poly-Hexcore & Octree-Kern, R3: Cut-Cell Snapping & Small-Cell Merging):
1. Native C++20 Octree & Mosaic Poly-Hexcore Engine (`octree_hexcore.hpp`, `octree_hexcore.cpp`):
   - 2:1 balanced adaptive Cartesian Octree in fluid domain (level-set phi(x) > 0). Adjacent leaf cells must satisfy Delta L <= 1 across all 26 directions.
   - ANSYS Mosaic / PUMA Polyhedral transition interface: When a refined cell borders an unrefined neighbor, replace the interface quad face on the unrefined neighbor with 4 coplanar sub-faces.
   - CellType assignment: Coarse transition cells become CellType::POLYHEDRON = 42. No hanging nodes in the DiFVM face-graph.
   - Relative Gauss closure ||sum S_f|| / sum ||S_f|| < 10^-11 on all cells.
2. Cut-Cell Snapping & Small-Cell Merging:
   - Snapping: boundary nodes snapped along grad(phi) onto CAD surface under AMIPS energy guard E_AMIPS < 10^4 and strict positive cell volume V_i > 0.
   - Small-cell merging: cut-cells with V_cut < 0.20 * V_bg topologically merged with their fluid neighbor along grad(phi) into a single closed polyhedron.
   - Berger & Giuliani (2024) WSRD weights: compute w_ij = 1/(N_i - 1) * (1 - alpha_j / alpha_target) for 0.20 <= alpha_i < 0.50, satisfying sum w_ij = 1.0. Store in NativeMeshData.wsrd_alpha and Python mesh.error_map["wsrd_weights"].
3. DiFVM Integration & C-ABI:
   - In `difvm_mesh.cpp`: Ensure `extractCellFaces` correctly preserves `CellType::POLYHEDRON = 42` for polyhedra with arbitrary polygon faces, without falling back to 2D polygons.
   - In `cfd_mesher_native.h` / `cfd_mesher_native.cpp`: Expose `cfd_mesher_generate_hexcore(...)` and ensure bounds checks and try/catch exception safety.
   - In `CMakeLists.txt`: Add `src/octree_hexcore.cpp` to library sources.
   - In `distmesh.cpp`: Clean up the K5 stub (unused `surf_triangles` and `std::clamp(nx, 2, 40)`).
4. Compilation & Verification:
   - Recompile native C++20 Release DLL using:
     & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build backend/meshing/native/build --config Release
     (Enforce OMP_NUM_THREADS=16).
   - In `mesh_engine.py`: Call native hexcore generator when available, providing seamless Python-native parity.
   - Run tests:
     $env:OMP_NUM_THREADS="16"; .\.venv\Scripts\python.exe -m pytest tests/test_wp4_multiphysics_hexcore.py tests/test_mesh_engine.py tests/test_cad_meshing_validation.py -v
   - Ensure all tests pass!
5. Write `analysis.md` and `handoff.md` in your working directory.
Send a message back when complete.
