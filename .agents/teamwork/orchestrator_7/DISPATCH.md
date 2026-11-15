## 2026-11-09T23:48:37Z

Resolve and verify all uncorrected audit findings (points 9.1 through 9.4) for CFDSolver following the structured 5-phase recommendations from `MultiphysicsSolver_Code_Review.md` using low-resource sequential tests, and then resume prior tasks.

Working directory: g:\Datein\Projects\CFDSolver
Integrity mode: development
The team must NOT: use cheats, shortcuts, heuristic mock values, or fake test results.

## Reference Material
- Code review and remediation guidance: `c:\Users\Ben Ludwig\Downloads\MultiphysicsSolver_Code_Review.md`

## Context & Constraints

### Hardware & Resource Limits (Active Background Process)
- Ansys is actively running in the background. Absolutely no HPC, full-load, or benchmark tests.
- Strictly throttle thread limits: `OMP_NUM_THREADS=2` (maximum 4), `MKL_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`.
- Run tests strictly sequentially and only on small test grids (<= 30,000 cells).
- Keep RAM and CPU footprint minimal to prevent starvation or crashing of the background Ansys process.
- Python environment: `.\\.venv\\Scripts\\python.exe`.

### Review Panel & Quality Policy
- 3-agent review panel required: 1x Code Cleanliness / Anti-Cheat, 1x Mathematical Fidelity / Paper Conformance, 1x Code Quality / Stability. Unanimous approval (3:0 PASS) required.
- Strict Zero-Shortcut Policy: throw exceptions before crashes, no raw pointer leaks, strict bounds checking, no heuristic clamps, and no swallowed exceptions.
- Existing Git state: Commit `23d1768f11e2ce175a1f9d58bdae9b3d1651cd2f` on `main` contains certified milestones M1 and M2.
- CAD test samples: `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `resources/CADTestSamples/TestCAD.obj`, `TestCADSTPAP214.stp`.

## Requirements (5-Phase Execution Plan)

### Phase 1: Ehrlichkeit & Test-Integrität (K7, K6, L4, L5, M9)
- **K7 & L4 & L5 (Doku & Config):** Reset `README.md` and `ROADMAP.md` to the actual implementation status (CAD ingestion + mesher prototype; physics modules are templates; remove or clearly mark nonexistent FVM solvers, Fortran modules, and optimization directories). Replace all absolute links (`file:///G:/...`) with relative paths. Convert `requirements.txt` to UTF-8.
- **K6 (Tautologische Tests):** In `tests/test_wp4_multiphysics_hexcore.py:96-185`, completely delete all local tautological helper copies (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`, etc.) and import the canonical functions directly from `backend/`.
- **M9 & UnstructuredMesh.validate():** In `models.py` (`UnstructuredMesh.validate()`), add an elongation and containment check ensuring each cell centroid $C_i$ lies within its cell bounding box and that $\sum V_i C_i$ matches the boundary surface integral. Update `pytest.ini` to `testpaths = tests backend/geometry/tests` and add `@pytest.mark.skipif` guards for missing external CAD/NX fixtures.

### Phase 2: Build & Plattform (H1)
- In `backend/meshing/native/CMakeLists.txt`, add `find_package(OpenMP REQUIRED)` and link OpenMP explicitly: `target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)`.
- Ensure native library loader does not silently swallow errors or fall back to Python without failing tests that expect native execution.

### Phase 3: Abkürzungen & Heuristiken entfernen (K3, K4, K9, M6, H10)
- **K3 (Polycube-Sonderfall):** Remove or safeguard `_detect_oriented_box_axes` in `backend/meshing/mesh_engine.py:231-284` so that non-box geometries like L- and U-profiles are meshed by the general pipeline rather than as solid bounding boxes.
- **K4 (Compatibility Shield):** Remove the backward compatibility shield in `mesh_engine.py:121-131` that silently forces solid meshes when `external_fluid` is requested. Default to `FarfieldPadding()` when parameters are omitted, and explicitly set affected tests to `domain_mode="solid_only"`.
- **K9 (STEP Engine Default):** In `backend/geometry/parsers/step_parser.py:84-95` (`engine="auto"`), make Gmsh/OpenCASCADE the primary path so curved surfaces (e.g. `BladeFullRev1CFD.stp`) are preserved rather than flattened by `_parse_with_brep`.
- **M6 & H10 (Klemmen & Skalierungen):** Remove the hard `max_level <= 4` clamp and the silent $10^{-15}$ volume overwrite in `octree_hexcore.cpp`. In `mesh_engine.py`, eliminate the 500k-cell RAM downscaling cap and the tuned magic numbers ($0.11 \cdot h_{\min}$, $h_0/3$). Raise explicit exceptions on resolution limits instead of silently coarsening.

### Phase 4: Mathematik & Topologie korrigieren (H3, H4, H2, H7, H8, M1, M2)
- **H3 (2D-Schwerpunkte):** In `difvm_topology.py:1126` and `difvm_mesh.cpp:454`, change 2D triangle weights to $(2/3) c_f + (1/3) c_{\text{approx}}$ (or use Green's theorem / Shoelace directly) instead of 3D pyramid weights ($0.75 / 0.25$).
- **H4 (Explizite Polyeder-Topologie):** In `moving_mesh.py`, `difvm_topology.py`, and `difvm_mesh.cpp`, eliminate the `len(verts) == 8` heuristic. Preserve explicit face lists and owner/neighbour connectivity so polyhedra in Octree meshes are not destroyed during mesh motion or topology reconstruction.
- **H2 (Celik-GCI):** In `backend/meshing/gci.py`, implement full Celik et al. (2008) formulation: detect divergence ($R \ge 1$), compute iterative apparent order $p$, determine effective grid refinement ratio $r = (N_1 / N_2)^{1/\text{dim}}$, and remove artificial clamps.
- **H7 (Supermesh-AMI):** In `difvm_topology.py:1655-1682`, replace inverse-distance weighting with area-conservative polygon overlap weights (Supermesh AMI) satisfying $\sum w \cdot A = A$.
- **H8 (SPH-FVM Kopplung & P2M):** In `backend/meshing/coupling.py`, compute boundary area vectors $A_{iS}$ from actual FVM boundary geometry rather than defining it as $- \sum V_j \nabla W_{ij}$. Implement mass-conservative P2M interpolation using cell volume weighting.
- **M1 & M2 (Aspect Ratio & wsrd_alpha):** Correct aspect ratio definition in `difvm_topology.py`, `difvm_mesh.cpp`, and `octree_hexcore.cpp` to use edge lengths or OpenFOAM definitions (yielding 1.0 for a unit cube). Redefine `wsrd_alpha` as cut-cell volume relative to its background octree cell rather than the global median volume.

### Phase 5: Physik & OpenFOAM-Schnittstelle (K8, H5, H6, M10)
- **K8 (SMR-Kinetik):** In `backend/native_cpp/smr_catalysis/smrKineticsSource.C`, implement stoichiometrically coupled reaction rates for species (CH₄, H₂O, H₂, CO, CO₂) and consistent enthalpy sink $q = -r \cdot \Delta H(T)$ with reversible Xu-Froment kinetics. Properly evaluate `fieldi` in `addSup` so enthalpy sinks are not erroneously added to multiple fields.
- **H5 & H6 (OpenFOAM Export & Cases):** In `polymesh_io.py` and `case_generator.py`, generate valid multi-region exports for `chtMultiRegionFoam` including `regionProperties`, correct patch types (`patch` for farfield, `wall` for body, `mappedWall` with required keys), and complete `0/` and `fvSolution` files.
- **M10 (WSL-Runner):** In the WSL execution runner, apply `shlex.quote` to command arguments, verify process return codes, and handle paths without drive letters safely.


## 2026-11-09T23:51:30Z

ZUSATZ-AUFTRAG VOM NUTZER ZU PHASE 1 (REPO-HYGIENE & DOKU):
Der Nutzer merkt an:
"Vielleicht die .md Dateien in eine bündeln, das war ja gedacht um nachzuvollziehen, was wann wo passiert ist"

Umsetzungsempfehlung für Phase 1:
Die zahlreichen losen Markdown-Dateien im Root-Verzeichnis (wie SUGGESTIONS_BACKEND.md, SUGGESTIONS_FRONTEND.md, COMMUNICATION_BACKEND_FRONTEND.md, FRONTEND_REQUIREMENTS_ON_BACKEND.md, TEST_READY.md, TEST_INFRA.md, DEAD_ENDS.md etc.), die während früherer Entwicklungsphasen als Nachvollziehbarkeits-Protokolle entstanden sind, sollen nicht verloren gehen, aber gebündelt / archiviert werden:
1. Zusammenfassung / Archivierung in einem zentralen Dokumentationsverzeichnis (z. B. `docs/DEVELOPMENT_HISTORY.md` oder `docs/archive/`).
2. Im Root verbleiben nur die Kern-Dokumente: ein ehrliches, aufgeräumtes `README.md`, `ARCHITECTURE.md` und `ROADMAP.md`.
3. Damit bleibt die vollständige Historie („was wann wo passiert ist“) zu 100% erhalten und durchsuchbar, während das Root-Verzeichnis sauber und übersichtlich wird (adressiert auch Audit-Befund N1).


## 2026-11-09T23:52:56Z

WICHTIGE PRÄZISIERUNG DES NUTZERS ZUR DOKU-BÜNDELUNG:

Die Frontend/Backend-Kommunikationsdokumente:
- SUGGESTIONS_BACKEND.md
- SUGGESTIONS_FRONTEND.md
- COMMUNICATION_BACKEND_FRONTEND.md
- FRONTEND_REQUIREMENTS_ON_BACKEND.md
MÜSSEN UNANGETASTET BLEIBEN und genau so im Root bestehen bleiben wie sie sind!

NUR die Dokumente von Teamwork (die vielen Agenten-Handoffs, Briefings, Dispatches und Progress-Dateien aus `.agents/teamwork/` sowie Teamwork-Testberichte wie TEST_READY.md) sollen in eine zentrale Datei gebündelt werden, damit die Hunderte verstreuten Einzeldateien zusammengefasst und chronologisch nachvollziehbar sind.
