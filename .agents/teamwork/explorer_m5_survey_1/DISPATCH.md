## 2026-11-12T01:30:53Z
You are explorer_m5_survey_1.
Working directory: g:\Datein\Projects\CFDSolver
State folder: g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m5_survey_1
Authoritative request: g:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Dispatch file: g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m5_survey_1\DISPATCH.md

You MUST read ORIGINAL_REQUEST.md before starting work. Do NOT summarize or filter it.

Your mission:
1. SMR Removal & Generalization (R1):
   - Exhaustively find every reference to SMR-Xu-Froment-Kinetics (K8) and Steam Methane Reforming across requirements, suggestions, frontend docs, and codebase:
     - backend/native_cpp/smr_catalysis/smrKineticsSource.C
     - Phase 5 K8 audit in SCOPE.md, PROJECT.md, MultiphysicsSolver_Code_Review.md
     - Section 9.4.1 in SUGGESTIONS_BACKEND.md
     - FRONTEND_REQUIREMENTS_ON_BACKEND.md (e.g. line 164 species bias)
     - COMMUNICATION_BACKEND_FRONTEND.md
     - backend/OPENFOAM_BACKEND_ATLAS.md
     - Any other files.
   - Investigate existing multiphysics/chemistry interfaces in the codebase.
   - Design replacement generic, data-driven multiphysics and configurable species chemistry interfaces.
   - Specify the exact updates for COMMUNICATION_BACKEND_FRONTEND.md handover log.
2. Phase 5 Audits (H5, H6, M10, H9):
   - Investigate Audit H5 in backend/meshing/polymesh_io.py: multi-region export for chtMultiRegionFoam, regionProperties, mappedWall, cyclic.
   - Investigate Audit H6 in backend/meshing/case_generator.py and backend/core/registry.py: runnable 0/ and fvSolution files for general solvers.
   - Investigate Audit M10 in backend/runner/wsl_runner.py: shlex.quote, exit code check, path safety, residual parsing.
   - Investigate Audit H9 in backend/meshing/moving_mesh.py: current swept volume implementation, check discrete geometric conservation law (DGCL) Simpson's rule and BDF2 time stepping.

Maintain progress in progress.md in your state folder.
Produce a comprehensive report at g:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m5_survey_1\report.md and handoff.md.
When finished, send a message to the caller with a summary of findings and report path.
