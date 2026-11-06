## 2026-10-30T22:46:39Z

Implement Phase 1 (WP1: Topology core, cell types, face connectivity, MeshErrorMap, polyMesh export; and WP2: exact Mesh-SDF, AABB tree, pseudonormals, Winding Numbers, VoroMesh) of the CFD meshing subsystem in Python and C++20 according to the Zero-Shortcut Policy, stopping for review before WP3–WP6.

Working directory: G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator\
Project root: G:\Datein\Projects\CFDSolver
Original request file: G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
Your identity: teamwork_orchestrator
Integrity mode: development

## Reference Material
- BACKEND_SUGGESTIONS.md / SUGGESTIONS_BACKEND.md (Abschnitt 4: Audit-Befunde K1–K8, Bug A–Bug F, H1–H11; Abschnitte 5 & 6: M2.5/M3+)
- resources/papers/literature_survey_2026.md (insb. §6 & §9.4 für WP1, §9.3 & §9.8 für WP2)
- ARCHITECTURE.md (Abschnitt 4.2 zur CMS-Geometrie-Fehlerkompensationskarte MeshErrorMap und Standalone In-Memory Graph-FVM SoA)
- resources/reference_spickzettel/INDEX.md (CAD/libigl/, Meshing/fTetWild/)
- Bestehender Code in backend/meshing/ (models.py, difvm_topology.py, mesh_engine.py, geometry_sdf.py, distmesh_core.py, native/include/, native/src/, native/CMakeLists.txt)

## Requirements

### R1. Topologie-Kern, Zelltyp-Eindeutigkeit, faces-Konnektivität, MeshErrorMap & polyMesh-Export (WP1)
- Erweitere UnstructuredMesh (models.py) und NativeMeshData (difvm_mesh.hpp / cfd_mesher_native.h / cfd_mesher_native.cpp) um explizite Face-zu-Knoten-Konnektivität mit konsistenter Rechts-Hand-Regel: interne Face-Normale S_f zeigt strikt von owner[f] nach neighbour[f] (owner[f] < neighbour[f]), Rand-Faces zeigen nach außen. In C++ werden face_offsets und face_nodes über C-ABI Getter exportiert.
- Implementiere backend/meshing/polymesh_io.py mit export_openfoam_polymesh (und mesh.to_openfoam_polymesh(case_dir)) für standardkonformes constant/polyMesh/ (points, faces, owner, neighbour nur für interne Faces, boundary mit korrekten Typen wall, patch, symmetry, empty, mappedWall, cyclic) sowie extrude_2d_to_3d_slab für 1-Zellen-dicke 3D-Prismen/Hexaeder mit frontAndBack (type empty;).
- Führe explizite Zelltypen CellType (kompatibel zu VTK: TRIANGLE=5, POLYGON=7, QUAD=9, TETRA=10, HEXAHEDRON=12, WEDGE=13, PYRAMID=14, POLYHEDRON=42) bzw. cell_types und topological_dim (2 oder 3) in compute_difvm_topology (Python & C++) ein. Koplanare 4-Knoten-Elemente sind QUAD, niemals TETRA (auch bei z != 0). Unterstütze allgemeine 3D-Polyeder (POLYHEDRON=42).
- Stelle Manifoldness sicher: Wenn >2 Zellen dasselbe Face teilen, wirf sofort ValueError. Erzwinge obere Dreiecksordnung (owner < neighbour).
- Repariere extract_cht_twin_faces: Entferne gekoppelte Faces sauber aus dem internen Block, re-indiziere und hänge sie als zusammenhängende Boundary-Patches (coupledWall_regionA_to_B, coupledWall_regionB_to_A) an.
- Reiche benannte Rand-Patches (face_patch_ids / patch_names) sauber durch compute_difvm_topology durch.
- Repariere rcm_reorder und ergänze hilbert_sfc_reorder (3D Morton/Hilbert SFC inkl. inner_cells vs. halo_adjacent_cells Metadaten) unter verlustfreier Erhaltung von boundaries, baffles, cyclic_pairs und metadata.
- Erweitere mesh.error_map (Python & C++) um die CMS-Kompensationsfelder (m_skew, k_f_comp mit alpha_max=1.5, beta_upwind, omega_cell, wsrd_alpha mit Berger-Giuliani WSRD-Gewichten). Klemme cos_theta auf [-1.0, 1.0]; bei S_f . d_f <= 0 eskaliere sofort auf stabilization_mask = 3.
- Vektorisierte, skaleninvariante UnstructuredMesh.validate(): np.add.at / np.subtract.at statt Python-Schleifen; Gauss-Closure relativ zur Zelloberfläche (tol_rel = 1e-11); echte Projektionsidentität (Kollinearität ||delta_f x d_f|| / (||delta_f|| ||d_f||) < 1e-12 und Überrelaxation delta_f . S_f = ||S_f||^2 für theta_f < 89 deg); owner < neighbour; zusammenhängende Blöcke. Vereinheitliche CurvatureSizingConfig und SizingParameters.
- Kapsle in cfd_mesher_native.cpp jede extern "C"-Funktion in try/catch (Returncode -2 bei Exception), prüfe alle Array-Grenzen, nutze in Python try ... finally: cfd_mesher_free(m_ptr), stelle CMakeLists.txt auf /fp:precise / -fno-fast-math -ffp-contract=off um und unterstütze .dll, .so, .dylib.

### R2. Exaktes Mesh-SDF mit AABB-Baum, Pseudonormalen, Winding Numbers & VoroMesh (WP2)
- Ersetze den Zentroid-KDTree in CADMeshSDF (geometry_sdf.py und C++ Pendant) durch einen BVH / AABB-Baum über Dreiecks-Bounding-Boxen mit prev_facet Spatial-Coherence-Hint-Caching.
- Berechne winkelgewichtete Pseudonormalen nach Bærentzen & Aanæs (2005): Flächennormale n_f im Dreiecksinneren, Kantennormale n_e auf Kanten, winkelgewichtete Eckpunktnormale n_v auf Vertices, für vorzeichenechte Distanzbestimmung ohne Vorzeichenflips an scharfen Kanten.
- Implementiere die Generalized Winding Number W(x) = 1/(4*pi) * sum_t Omega_t(x) nach Oosterom & Strackee / Jacobson et al. (2013) als robusten Modus für nicht-wasserdichte "Dirty-CAD"-Meshes.
- Implementiere den differenzierbaren VoroMesh-Bisector-Loss nach Maruani et al. (2023) über die k=16 nächsten Saatpunkte für wasserdichte, selbstschnittfreie Voronoi-Hüllen.

### R3. Zero-Shortcut Engineering & Dual-Engine Parity
- Keine Geometrie-Heuristiken (kein if len(verts) == 8 and len(faces) == 12), keine auf Testfälle getunten Magic-Thresholds, keine unskalierten mm-Konstanten und keine stillen Auflösungs-Clamps. Bei Überschreiten von Sicherheitslimits (max_cells) Exception oder warnings.warn werfen, niemals still h0 überschreiben.
- Keine stillen Fallbacks (kein nacktes except Exception: pass in mesh_engine.py oder difvm_topology.py); exakte Fehlerprotokollierung bzw. RuntimeError im strict_native=True-Modus.
- Jede C++20-Funktion in backend/meshing/native/src/ muss echte Geometrie/Topologie berechnen. C++20 und Pure-Python/NumPy müssen identische physikalische Invarianten liefern.
- Alle Implementierungen müssen mathematisch und physikalisch fundiert sowie deployable sein.

### R4. Phased Execution Gate: Review vor WP3–WP6
- Die Implementierung konzentriert sich auf Phase 1 (WP1 und WP2).
- Nach Abschluss und vollständiger Verifikation von Gate WP1 und Gate WP2 stoppt die Ausführung für ein Review der Architektur und Ergebnisse, bevor mit WP3 (2D-Mesher), WP4 (3D-Mosaic Poly-Hexcore), WP5 (Boundary-Layer-Inflation) und WP6 (ALE/Moving Mesh) fortgefahren wird.

## Acceptance Criteria

### Gate WP1: Topologie, faces-Konnektivität, MeshErrorMap & polyMesh
- [ ] UnstructuredMesh und NativeMeshData besitzen explizite Face-zu-Knoten-Konnektivität mit owner < neighbour auf allen internen Faces und nach außen zeigenden Randnormalen.
- [ ] export_openfoam_polymesh erzeugt ein standardkonformes constant/polyMesh/ (points, faces, owner, neighbour nur für interne Faces, boundary mit korrekten Typen) und extrude_2d_to_3d_slab erzeugt ein 1-Zellen-dickes 3D-Gitter mit frontAndBack (type empty;).
- [ ] Zelltypen unterscheiden QUAD (2D koplanar, auch bei z != 0) von TETRA (3D), und POLYHEDRON=42 wird unterstützt.
- [ ] Nicht-manifolde Eingaben (>2 Zellen teilen sich ein Face) werfen deterministisch ValueError.
- [ ] extract_cht_twin_faces hinterlässt keine neighbour = -1 im internen Face-Block, sondern lagert gekoppelte Faces als saubere Boundary-Patches aus.
- [ ] rcm_reorder und hilbert_sfc_reorder erhalten boundaries, baffles, cyclic_pairs und metadata verlustfrei.
- [ ] Invertierte Faces (S_f . d_f <= 0) werden nicht auf 90 deg geklemmt, sondern sofort erkannt und auf stabilization_mask = 3 eskaliert.
- [ ] mesh.error_map enthält m_skew, k_f_comp, beta_upwind, omega_cell und wsrd_alpha.
- [ ] UnstructuredMesh.validate() ist vektorisiert (np.add.at / np.subtract.at), prüft relative Gauss-Closure (tol_rel = 1e-11) und die echte Projektionsidentität.
- [ ] Unit-Tests mit L-förmigen 2D-Quad-Gittern (bei z = 5.0), skalierten 3D-Gittern (10^-6 m bis 10^3 m), nicht-manifolden Faces und CHT-Zweiregionen-Gittern sind grün.
- [ ] C-ABI in cfd_mesher_native.cpp kapselt alle extern "C"-Funktionen in try/catch (Returncode -2 bei Exception), prüft Array-Grenzen, und CMakeLists.txt kompiliert mit /fp:precise bzw. -fno-fast-math.

### Gate WP2: Exaktes Mesh-SDF, AABB-Baum & Pseudonormalen
- [ ] CADMeshSDF verwendet einen BVH/AABB-Baum mit winkelgewichteten Pseudonormalen (Flächen-, Kanten-, Eckpunktnormalen nach Bærentzen & Aanæs 2005).
- [ ] Generalized Winding Numbers W(x) nach Oosterom & Strackee / Jacobson et al. (2013) sind für nicht-wasserdichte Meshes implementiert und getestet.
- [ ] Differenzierbarer VoroMesh-Bisector-Loss über k=16 nächste Saatpunkte ist implementiert und liefert stetige Distanzwerte.
- [ ] Test an anisotropem Quader/Keil mit scharfen 30 deg- und 90 deg-Kanten sowie Mesh mit kleinem Spalt zeigt kein einziges Vorzeichen-Flippen entlang Kanten und Eckendiagonalen.

### Zero-Shortcut Policy, Regression & Review-Stopp
- [ ] Alle 540 bestehenden Tests in tests/ und backend/geometry/tests/ bleiben grün.
- [ ] Keine nackten except Exception: pass, keine Geometrie-Heuristiken (len(verts)==8), keine Magic-Thresholds und keine stillen Auflösungs-Clamps im Code.
- [ ] Nach erfolgreicher Verifikation von Gate WP1 und Gate WP2 stoppt die Ausführung für das Review vor Beginn von WP3–WP6.

## 2026-10-30T22:48:14Z

Zusätzliche Referenzressourcen vom Benutzer bereitgestellt:
In `G:\Datein\Projects\CFDSolver\resources\reference_spickzettel\LMArena` befinden sich mehrere vertiefende Ausarbeitungen und Implementationen zum DiFVM-Paper aus LM Arena (z. B. `Opus46DiFVM.md`, `Haiku55medium.md`, `FableDiFVM51low.md`, `GPT55highDiFVM.md`, sowie die Ordner `DiFVMAgentModeUnkown` und `DiFVMAgentModeUnkonwnbutHigh`).

Diese können vom Team als wertvolle mathematische und architektonische Referenz für die DiFVM SoA Graph-FVM-Strukturen, diskrete Operatoren und Vektorisierungen herangezogen werden.

Diese Information wurde auch in ORIGINAL_REQUEST.md hinterlegt.

## 2026-10-30T22:50:37Z

Wichtige Benutzer-Direktive zur Priorität & Referenzen:

Das CFDSolver-Projekt selbst ist und bleibt die "Single Source of Truth".
Die Spickzettel und LMArena-Dokumente dürfen NICHT 1:1 unkritisch übernommen werden.
Sie dienen als Inspiration und Referenz: Nur Teile, Blöcke oder Skripte, die nach sorgfältiger Prüfung für mathematisch/technisch gut und passend befunden wurden, dürfen selektiv übernommen werden, um die Entwicklung zu erleichtern oder zu beschleunigen.

Diese Direktive ist bindend und wurde auch in ORIGINAL_REQUEST.md protokolliert.

## 2026-10-30T22:58:02Z

Verbindliche Benutzer-Vorgabe für die Gate-Audits (Unabhängiges 3-Agenten-Audit-Gremium):

Für jeden Gate-Pass (Gate WP1, Gate WP2) MUSS ein unabhängiges 3-Agenten-Gremium eingerichtet und einberufen werden:
1. Agent 1 (Paper-Fidelity & Zero-Shortcut Challenger):
   - Prüft unabhängig, ob alle Gleichungen und Methoden aus den Papern sauber und vollständig im Code implementiert sind.
   - Sucht aggressiv nach Abkürzungen, Heuristiken, Magic Numbers, stillen Fallbacks oder Test-Cheats.
2. Agent 2 (Code & Software-Architektur Auditor):
   - Schaut sich unabhängig rein den Code an (C++20, Python, C-ABI, Thread-/Memory-Safety, Fehlerbehandlung, keine Stubs/Facades).
3. Agent 3 (Mathematik & Numerik Auditor):
   - Prüft streng die mathematische Korrektheit, Skaleninvarianz, physikalischen Invarianten (V_i > 0, relativer Gauss-Closure, Projektions-Kollinearität, Winding Numbers, Pseudonormalen, WSRD/CMS).

Strikte Regeln für das Gremium:
- Einstimmigkeits-Prinzip: Ein Gate-Pass wird NUR DANN erteilt, wenn das Gremium zu 100 % EINSTIMMIG zustimmt (alle 3 Voten müssen "PASS / APPROVE" sein; bei einem einzigen Veto muss nachgebessert werden).
- Audit-Only: Das Gremium läuft nur dann, wenn ein Gate-Audit ansteht.
- Blind / Kontext-freie Code-Prüfung: Das Gremium darf über die allgemeine Projektstruktur informiert werden, muss den Mesher-Code aber unabhängig und objektiv auf seine eigene Substanz hin auditieren, ohne sich auf vorangegangene Erfolgsmeldungen zu verlassen.

Bitte verbindlich in PROJECT.md, BRIEFING.md und den Gate-Verifikationsplänen verankern. Die Direktive wurde auch in ORIGINAL_REQUEST.md dokumentiert.

## 2026-10-30T23:10:33Z

Benutzer-Vorgabe zu Code-Stil und Formatierung:
Sowohl im C++20-Code als auch im Python-Code ist strikt der Google Style Guide einzuhalten:
1. C++20: Google C++ Style Guide (Namenskonventionen, keine unbedachten using namespace std; in Headern, saubere Include-Guards, 2-Space/Clang-Format-Konformität, nullptr statt NULL/0, explizite Typen).
2. Python: Google Python Style Guide (Typannotationen für alle öffentlichen Funktionen, Google-Style Docstrings mit Args/Returns/Raises, PEP8/Black-Konformität, keine Wildcard-Imports).

Bitte in den Richtlinien der Worker (insb. worker_phase1_wp1_1) und im Prüfprotokoll des Code-Auditors (Gremium Agent 2) verbindlich verankern. Diese Direktive wurde auch in ORIGINAL_REQUEST.md hinterlegt.

## 2026-10-30T23:58:44Z

Wiederaufnahme nach Server-Neustart — Benutzer-Direktive:
Der Benutzer hat bestätigt: "Es kann weiter gehen glaub ich".

Bitte nimm die Arbeit wie vorgegeben wieder auf:
1. Status prüfen (WP1-Akzeptanztests in tests/test_wp1_topology_polymesh.py waren bereits zu 100% grün).
2. Falls worker_phase1_wp1_1 durch den Neustart angehalten wurde, belebe ihn wieder oder überprüfe dessen Regressionslauf (540 Tests), um den formalen Handoff für Gate WP1 zu finalisieren.
3. Berufe das unabhängige 3-Agenten-Audit-Gremium für Gate WP1 ein:
   - Agent 1: Paper-Fidelity & Zero-Shortcut Challenger
   - Agent 2: Code & Software-Architektur Auditor (inkl. Google Style Guide)
   - Agent 3: Mathematik & Numerik Auditor
   Einstimmiges Votum (100% APPROVE) für die Freigabe von Gate WP1 einholen.
135: 4. Nach erfolgreichem Gate WP1 direkt mit WP2 (Exaktes Mesh-SDF mit AABB-Baum, Pseudonormalen nach Bærentzen & Aanæs, Generalized Winding Numbers nach Jacobson et al., differenzierbarer VoroMesh-Bisector-Loss nach Maruani et al.) fortfahren.

## 2026-10-31T02:24:25Z

VERBINDLICHE BENUTZER-DIREKTIVE (Token-Schonung / Phasen-Stopp):

Sobald das Gate WP2 vom 3-Agenten-Audit-Gremium einstimmig freigegeben wurde (100% APPROVE von Challenger, Code-Auditor und Math-Auditor nach Beseitigung der 3 Kritikpunkte), MUSS die Ausführung von Teamwork sofort vollständig pausieren / anhalten.

Es soll unter KEINEN UMSTÄNDEN mit WP3–WP6 begonnen werden! Der Benutzer möchte seine verbleibenden Token für das Reformer-Projekt aufsparen.
Erstelle nach dem erfolgreichen Gate-WP2-Pass einen sauberen, vollständigen Abschluss- und Checkpoint-Bericht für Phase 1 (WP1 & WP2) und beende den Lauf dann geordnet.

Die Direktive wurde in ORIGINAL_REQUEST.md dokumentiert.

## 2026-11-06T20:55:34Z

Resolve 6 critical mathematical and paper-fidelity audit defects and 4 static audit issues across the C++ and Python meshing backend, recompile the native DLL, commit the changes, build the automated checkMesh and simpleFoam acceptance validation pipeline in WSL, and pass the independent 3-agent audit committee with unanimous 3:0 approval under specified hardware and architecture guidelines.

Working directory: g:\Datein\Projects\CFDSolver
Integrity mode: development

## Reference Materials
- Literature Survey & Verification Papers: `resources/papers/literature_survey_2026.md` and `resources/papers/`
- Reference Implementations: `resources/reference_spickzettel/LMArena/`
- Benchmark CAD Geometries: `resources/CADTestSamples/` (including `TestCAD.stp`, `BladeFullRev1CFD.stp`) and OpenFOAM tutorial CAD cases in `OpenFOAM_base/openfoam/` (e.g., `snappyMultiRegionHeater`, `motorBike`)

## Requirements

### R1. Cut-Cell Snapping & AMIPS Metric Fix in Octree Hexcore
In `backend/meshing/native/src/octree_hexcore.cpp:645-648`, eliminate the collinear tetrahedron in `computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center)`. For each corner vertex `v` in adjacent octant `onode`, determine its corner index `c_idx in 0..7` and construct a positively oriented corner tetrahedron from `test_pos` and the 3 adjacent octant edge neighbor nodes, ensuring `det_j = dx * dy * dz > 0` and finite AMIPS energy. Verify that all cell sub-pyramids sharing vertex `v` retain positive volume after snapping. Add a test in `tests/test_wp4_multiphysics_hexcore.py` calling `generate_hexcore_native` on a sphere verifying wall nodes snap to `|phi| < 1e-3`.

### R2. Translation-Invariant Polygon Centroids, Face Normals, and Cell Volumes
In `backend/meshing/native/src/octree_hexcore.cpp:667-708`:
1. In Step 9, compute `mf.normal_owner` and `mf.centre` via triangle-fan integration (area-weighted sub-triangle centroids as in `computePolygonNormalAndCentroid` in `difvm_mesh.cpp:36-78`) to accurately handle non-planar snapped quad faces. Flag inverted sub-triangles (`mf.is_folded`) if oriented opposite to the face normal.
2. In Step 10, compute for each cell `c` a local reference point `c_approx` (mean of `mf.centre` of all faces of cell `c`) and integrate pyramid volumes and centroids as in `difvm_mesh.cpp:444-456`:
   `pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0`
   `pyr_c = mf.centre * 0.75 + c_approx * 0.25`
   `cell_vols[c] += pyr_vol`
   `cell_cents[c] += pyr_c * pyr_vol`
   and normalize at the end by `cell_vols[c]`.

### R3. Small-Cell Agglomeration, Cell Weighting, and WSRD Weights
1. In `octree_hexcore.cpp:742-847`, when merging a small cell `c` into `best_nb`, union all its vertices into `cell_nodes` of the target cell `best_nb` and set `cell_types[new_c] = POLYHEDRON (42)`. Merge and deduplicate internal faces sharing the same `(owner, neighbour)` pair and maintain `owner < neighbour`.
2. In `octree_hexcore.cpp:861, 1020`, compute `mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c])` across all adjacent faces of cell `c` as in `difvm_mesh.cpp:666-677`.
3. In `mesh_engine.py:750-768` and `difvm_topology.py:956-972`, compute `wsrd_weights` according to Berger & Giuliani (2024) Eq. (4.2) for `active_cfg.small_cell_ratio <= alpha_i < active_cfg.wsrd_target_alpha` using dynamic configuration attributes instead of hardcoded `0.20` and `0.50`, storing in `mesh.error_map["wsrd_weights"]` and `mesh.metadata["wsrd_weights"]`.

### R4. Solid-Only Domain Mode Dispatch Fix
In `backend/meshing/mesh_engine.py:126-130`, correct the `is_solid_only` dispatch branch so that `is_solid_only = (config is None) or (active_cfg.domain_mode == "solid_only")`. When `config is not None` and `active_cfg.domain_mode != "solid_only"`, call `self.mesh_hexcore(clean_mesh, scale=1.0, h0=effective_h0, config=active_cfg)`.

### R5. DistMesh Boundary Projection & Persson & Strang (2004) Consistency
1. In `backend/meshing/distmesh_core.py:734`, change point filtering to `p = p[d < -0.05 * h_vals]`.
2. In `distmesh_core.py:827-839`, execute up to 3 Newton projection steps for points with `d_nodes > 0.0`. In `backend/meshing/native/src/distmesh.cpp:734-739`, when an interior node is pushed outside (`!pointInPoly(new_pos, sub_poly)`), project `new_pos` orthogonally back onto the nearest polygon boundary segment.
3. In `distmesh.cpp:239-388`, filter generated cells against surface triangles using `MeshSDF` (`phi < 0` at cell centroid) or delegate to `OctreeHexcore` (`domain_mode = 2`).

### R6. GCI Dimensional Scaling, SDF Tolerances, and Fast Winding Number
1. In `backend/meshing/gci.py:113-114`, compute representative grid size with exponent `1.0 / max(1, mesh.topological_dim)`.
2. In `gci.py:148-156`, clamp `p` to `[0.5, 10.0]` only when outside range or when oscillatory convergence occurs (`eps_32 * eps_21 <= 0`), maintaining consistent exponent in `phi_extrapolated` and `gci_fine`.
3. In `backend/meshing/native/src/sdf_engine.cpp:479-484`, apply `1e-5 * diag` tolerance on outside bounding box check.
4. In `geometry_sdf.py:989-1049` and `sdf_engine.cpp:503-540`, accelerate `winding_number` evaluation via AABB Barnes-Hut far-field approximation for nodes where `dist > 2.0 * node_radius`.

### R7. Static Audit Detail Fixes, Boundary Types, and Strict Native Mode
1. In `difvm_topology.py:778-784`, fix attribute names: use `config.sizing.alpha` / `config.sizing.beta` and `config.inflation.pinching` / `config.inflation.pinching_threshold_angle`.
2. In `MeshEngine` (`mesh_engine.py:465`), introduce `strict_native: bool = False` and support `CFD_STRICT_NATIVE=1` environment variable so `mesh_hexcore` never falls back silently to Python when strict native is enabled.
3. In `polymesh_io.py:150-163`, support configurable `patch_types` exporting `top`, `sides`, and `bottom` (without ground plane) as `symmetry` or `patch` instead of `wall`.
4. In `inflation.py:104`, replace global `nominal_normal` flip with consistent outward normal orientation per wall face, and reverse node ordering `f_arr[::-1]` when normal is flipped.

### R8. Benchmark CAD Testing and OpenFOAM Tutorial Validation
Execute mesh generation and validation against real benchmark CAD models:
1. Samples in `resources/CADTestSamples/`, including `TestCAD.stp` and `BladeFullRev1CFD.stp`.
2. Tutorial CAD geometries in `OpenFOAM_base/openfoam/` tutorials (e.g., `snappyMultiRegionHeater`, `motorBike`).

### R9. Literature Survey Alignment and Reference Code Quality
1. Verify and align all mathematical formulas with papers catalogued in `resources/papers/` and `resources/papers/literature_survey_2026.md`.
2. For algorithms, code patterns, or ideas referenced from `resources/reference_spickzettel/LMArena/`, actively verify and patch known defects before adoption, adapting all implementations cleanly into the CFDSolver architecture without unvetted 1:1 copies.

### R10. Automated checkMesh and simpleFoam Acceptance Pipeline in WSL
Build an automated non-interactive acceptance pipeline running in WSL that exports the generated mesh via `export_polyMesh` and executes:
1. OpenFOAM `checkMesh` verifying face non-orthogonality, skewness, aspect ratios, and zero negative volume faces.
2. `simpleFoam` comparison against `snappyHexMesh` baseline reference cases.

### R11. Architecture, Code Quality, HPC Standards, and Fail-Fast Safety
- **Google C++ Style Guide:** Native C++ code must follow Google C++ Style guidelines using modern C++20, clean RAII patterns, explicit error handling, and no naked macros.
- **Fail-Fast Error Handling & Safety:** Explicitly throw typed C++ and Python exceptions before any potential crash, out-of-bounds access, or memory corruption. Never segfault, leak raw pointers, or silently corrupt data.
- **Open Software Architecture (OSA) Compliance:** Maintain generic metadata and tensor container interfaces for multiphysics coupling (CFD, structural FEM, CHT twin-faces, anisotropic stiffness tensors).
- **High Performance Core:** Performance-critical routines in C++20 / Rust / Modern Fortran must follow Structure-of-Arrays (SoA) layout, zero-copy pointer passing, high cache locality, memory efficiency, and SIMD/AVX2 friendliness.

### R12. Independent 3-Agent Audit Committee Gate Certification
Before completion, code and results must be submitted to an independent 3-agent audit committee:
1. **Agent 1 (Paper-Fidelity & Zero-Shortcut Challenger):** Audits exact formula fidelity against papers, enforces zero shortcuts, verifies absence of heuristics, magic numbers, or stub implementations.
2. **Agent 2 (Code & Software-Architecture Auditor):** Audits code cleanliness, C++20/Python style guidelines, thread and memory safety, and OSA compliance.
3. **Agent 3 (Mathematics & Numerics Auditor):** Audits numerical invariants, geometric conservatism, convergence, stability, and mesh topology correctness.
Final acceptance requires 100% unanimous approval (3:0 APPROVE) across all 3 auditors.

### R13. Operational Hardware and Concurrency Constraints
Protect host stability with Ansys Fluent active in background (~95% RAM):
- Set thread limits to 8 to 10 threads before executions: `OMP_NUM_THREADS=8` (or 10), `MKL_NUM_THREADS=8` (or 10), `OPENBLAS_NUM_THREADS=8` (or 10).
- Run CMake builds with `--parallel 8` (or 10), preferring core allocation/pinning to E-cores or idle P-cores.
- Run `pytest` strictly sequentially on individual test files with small memory footprints (no `pytest -n auto`, no xdist).
- Restrict initial test mesh sizes to <= 30,000 cells.

## Acceptance Criteria

### Mathematical & Meshing Core Validation
- [ ] `computeTetAMIPS` evaluates on non-collinear corner tetrahedra with `det_j > 0` and finite AMIPS energy.
- [ ] Test in `tests/test_wp4_multiphysics_hexcore.py` proves wall nodes snap to `|phi| < 1e-3` on a sphere surface using `generate_hexcore_native`.
- [ ] In `octree_hexcore.cpp`, unit cube `[0,1]^3` computes centroid `(0.5, 0.5, 0.5)` and shifted cube `[10,11]^3` computes centroid `(10.5, 10.5, 10.5)`.
- [ ] Agglomerated small cells in native hexcore have `cell_types = 42 (POLYHEDRON)`, unioned vertex sets, and sorted internal faces with `owner < neighbour`.
- [ ] `mesh.omega_cell` is populated with `std::max(0.1, min_beta_per_cell)` across adjacent faces.
- [ ] `MeshEngine.mesh_hexcore` and `difvm_topology.py` compute `wsrd_weights` using dynamic `config.small_cell_ratio` and `config.wsrd_target_alpha`.
- [ ] `is_solid_only` in `mesh_engine.py` evaluates to False when `domain_mode != "solid_only"`, dispatching to `mesh_hexcore`.
- [ ] DistMesh point filtering strictly uses `-0.05 * h_vals`, applies Newton boundary projection, and projects pushed-out points orthogonally to boundary segments.
- [ ] `compute_grid_representative_size` in `gci.py` scales with `1.0 / max(1, mesh.topological_dim)`.
- [ ] In `difvm_topology.py`, attribute lookups use `config.sizing.alpha`/`beta` and `config.inflation.pinching`/`pinching_threshold_angle` without AttributeError.
- [ ] Boundary faces in `polymesh_io.py` export with configurable patch types (`symmetry` / `patch` for top/sides/bottom).
- [ ] `inflation.py` maintains consistent outward normals without inverting opposing faces on closed bodies.

### CAD Benchmark & Reference Alignment
- [ ] Meshing succeeds on CAD models from `resources/CADTestSamples/` (`TestCAD.stp`, `BladeFullRev1CFD.stp`).
- [ ] Meshing succeeds on CAD samples from `OpenFOAM_base/openfoam/` tutorials (`snappyMultiRegionHeater`, `motorBike`).
- [ ] Formulas implemented match the reference publications in `resources/papers/` and `literature_survey_2026.md`.
- [ ] Snippets referenced from `resources/reference_spickzettel/LMArena/` are verified, defect-patched, and adapted to CFDSolver architecture.

### Compilation, Strict Mode & Safety
- [ ] MSVC 2022 C++20 Release DLL (`cfd_mesher_native.dll`) compiles cleanly via CMake with `--parallel 8` to `10`.
- [ ] With `CFD_STRICT_NATIVE=1`, native execution runs without silent Python fallback.
- [ ] Out-of-bounds inputs and invalid geometry states raise explicit C++ / Python exceptions rather than crashing or segfaulting.
- [ ] All 615 existing tests pass under sequential pytest execution with 8–10 threads.

### WSL OpenFOAM Acceptance Pipeline
- [ ] Automated script exports mesh to OpenFOAM `polyMesh` and runs `checkMesh` in WSL without fatal mesh errors.
- [ ] Test execution runs `simpleFoam` in WSL and compares mesh diagnostics against `snappyHexMesh`.

### Audit Committee Gate Certification & Git Commit
- [ ] Independent 3-agent audit committee (Paper-Fidelity Challenger, Code & Architecture Auditor, Numerics Auditor) achieves unanimous 3:0 APPROVE certification.
- [ ] Completed fixes, new tests, and pipeline scripts are committed to git.
