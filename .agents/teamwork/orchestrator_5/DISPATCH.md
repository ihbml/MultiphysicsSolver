## 2026-11-03T00:34:29Z

Multiphysics production implementation of Phase 2 (WP3 to WP6) of the meshing subsystem for CFDSolver (CFD, FEM Mechanics, FEM Electronics/EMF, CHT) including remediation of the 5 audit findings from Phase 1, using an independent 3-agent audit committee (Paper-Fidelity Challenger, Code-Auditor, Math-Auditor with 100% unanimity requirement).

Working directory: G:\Datein\Projects\CFDSolver
Integrity mode: development
The team must NOT: take shortcuts (no dummy stubs, no magic numbers, no test-specific heuristics or mockups, no silent exception fallbacks, and no partial or incomplete paper implementations).

## Reference Material
- SUGGESTIONS_BACKEND.md (insbesondere Abschnitte 4, 5 und 6 als primäre Anforderungs- und Designspezifikation)
- OpenFOAM_base/openfoam (als Blaupause und ASME V&V 20 Referenz gemäß SUGGESTIONS_BACKEND.md Abschnitt 3)
- ARCHITECTURE.md (Abschnitt 4.2: CMS MeshErrorMap und In-Memory Graph-FVM SoA)
- resources/papers/literature_survey_2026.md (§6, §9.3, §9.4, §9.8) und lokale C++-Repos in resources/reference_spickzettel/
- LMArena/Mesher/ (als selektive Inspiration: normierter tanh_guard aus graph_mesh.cpp / FableDiFVM51low.md, STL-Topologie-Reparatur TriangleSoup::finalise(), VTK-Type-42-(POLYHEDRON)- & OpenFOAM-Lexer-I/O; bekannte Bugs beachten: cap2-Bug in make_cutcell_mesh und Reordering-Bug in reorder.cpp)
- Bestehender Code in backend/meshing/ (models.py, difvm_topology.py, mesh_engine.py, geometry_sdf.py, distmesh_core.py, native/)

## Verification Resources
- 567 bestehende Unit-, Integrations-, Adversarial- und Baseline-Tests in tests/ und backend/geometry/tests/
- tests/test_mesher_validation_gate.py (Gates P1 bis P6)
- Neue gatespezifische Tests für WP3, WP4, WP5 und WP6 inklusive Multiphysics-, GCI- und Open Software Architecture (OSA)-Validierung

## Requirements

### R1. Behebung der 5 Phase-1-Audit-Punkte & Re-Kompilierung (Vor WP3)
Vor Beginn der Arbeiten an WP3 müssen die 5 Audit-Punkte aus Phase 1 behoben und verifiziert werden:
1. Python/C++-Parität in MeshErrorMap (backend/meshing/difvm_topology.py, Zeilen 988–1038):
   - In compute_difvm_topology_python die Vertauschung von omega_cell und wsrd_alpha korrigieren:
     * omega_cell muss wie in C++ (difvm_mesh.cpp:533-544) das Minimum von beta_upwind über alle angrenzenden Faces jeder Zelle sein, geclampt auf [0.1, 1.0] (mittels np.minimum.at).
     * wsrd_alpha muss wie in C++ (difvm_mesh.cpp:546-558) np.clip(cell_volumes / (np.median(cell_volumes) + 1e-30), 0.0, 1.0) sein (statt np.ones).
     * beta_upwind muss in Python dieselbe Formel wie in C++ (difvm_mesh.cpp:495-497) verwenden: np.minimum(np.clip((75.0 - non_ortho) / 35.0, 0.0, 1.0), np.clip((1.0 - skewness) / 0.6, 0.0, 1.0)) (und 0.0 wo stab_mask == 3).
2. Kein langsamer Python-Vorab-Loop in compute_difvm_topology_native (backend/meshing/difvm_topology.py, Zeilen 465–502):
   - Entferne die Python-Schleife for c in cells: _extract_cell_faces_py(...) vor dem C++-Aufruf, da DiFVMMesh::build_from_cells in difvm_mesh.cpp:264 Nicht-Mannigfaltigkeit bereits nativ prüft und -2 zurückgibt.
   - Wenn dll.cfd_mesher_build_from_cells -2 zurückgibt, wirf direkt ValueError("Non-manifold mesh: a face is shared by more than 2 cells.").
3. Auswärts-Orientierung aller Zell-Faces in _extract_cell_faces_py (difvm_topology.py:295-351) und extract_cell_faces (difvm_mesh.cpp:146-214):
   - Prüfe bei allen 2D- und 3D-Zelltypen anhand des Zell-Schwerpunkts cell_c, ob die lokale Face-Normale nach außen zeigt (dot(fn, fc - cell_c) < 0 in 3D bzw. dot([dy, -dx, 0], fc - cell_c) < 0 in 2D). Falls sie nach innen zeigt, drehe die Knotenreihenfolge um (f_nodes[::-1] bzw. std::reverse), damit auch CW-geordnete 2D-Zellen oder invertierte Tets immer auswärts gerichtete owner-Normalen erzeugen.
4. Boundary-Patch-Abdeckung in UnstructuredMesh.validate() (backend/meshing/models.py) & Thread-Local prev_facet in MeshSDF::evaluate (sdf_engine.cpp:453):
   - Prüfe in validate(), dass (sofern self.boundaries nicht leer ist) alle (start, count)-Bereiche im Intervall [n_int, n_faces) liegen, sich nicht überlappen und ihre Summe exakt n_bnd ergibt.
   - Halte in MeshSDF::evaluate (sdf_engine.cpp:431-490) pro OpenMP-Thread int32_t prev_facet = -1; und übergib prev_facet an tree_.closest_point(p, prev_facet, q, facet, bary, dist); prev_facet = facet;.
5. Optionaler seed_signs-Filter in voromesh_bisector_loss (geometry_sdf.py / sdf_engine.cpp):
   - Ergänze einen optionalen Parameter seed_signs: Optional[np.ndarray] = None (Default None). Wenn seed_signs übergeben wird, berücksichtige für die VoroMesh-Rand-Bisektoren nach Maruani et al. (2023) nur Nachbar-Paare (i, j) mit unterschiedlichem Vorzeichen (seed_signs[i] * seed_signs[j] < 0).
- Rekompiliere cfd_mesher_native.dll neu und stelle sicher, dass alle 567 Tests zu 100% grün bleiben.

### R2. WP 3: Robuster 2D-Mesher
- Implementiere Constrained Delaunay / DistMesh mit adaptiver Größendefinition h(x) und Kantenunterteilung entlang des Randes gesteuert durch h(x).
- Shoelace CCW-Kanonisierung von Polygonen und Rändern (automatische Richtungsumkehr bei CW).
- Ear-Clipping / Constrained Delaunay Randtreue ohne blinden Zentroid-Fan; strikt positive Zellflächen V_i > 0 und exakte analytische Flächenerhaltung sum(V_i).

### R3. WP 4: Echte 3D-Fluidgebiets- & Mehrbereichs-Vernetzung (Multiphysics & GCI-Hierarchie)
- Implementiere Mosaic Poly-Hexcore / Cut-Cell / Small-Cell Merging & FMG-Hierarchie.
- Multiphysics-Architektur von Grund auf:
  * Unterstützung für reine Fluidgebiete (CFD), Festkörpergebiete (Struktur-FEM & Wärmeleitung), Elektronik/EMF-Leiter sowie konforme Mehrbereichs-Kopplung (CHT Twin-Faces und Fluid-Solid-Conductor Interfaces über cell_regions).
  * Unterstützung anisotroper Materialeigenschaften und knotenweiser Tensoren.
- Domänen-Modi: external_fluid, internal_fluid, solid_only, cht_coupled mit konfigurierbarem farfield_padding / farfield_bbox und analytischen Verfeinerungszonen (refinement_zones).
- Adaptiver 2:1 balancierter Octree ohne Hanging Nodes über ein polyedrisches Interface.
- Cut-Cell Snapping mit AMIPS / VoroCrust; Small-Cell Merging für V_cut < 0.2 V_bg mit Berger-Giuliani WSRD w_ij.
- Automatische Generierung und Benennung von Farfield-Patches (inlet, outlet, bottom, top, sides) und Körper-Patches.
- Automatisierte 3-Gitter-GCI-Hierarchie: Integrierte Generierung von 3 Verfeinerungsstufen (Grob, Mittel, Fein mit Verfeinerungsfaktor r ~ 1.3 bis 1.5) unter exakter Erhaltung der Topologie und Patch-Identitäten für automatisierte GCI-Studien (Grid Convergence Index GCI < 1%).

### R4. WP 5: Kollisionsfreie 3D-Grenzschicht-Inflation mit Grading (y+ <= 1)
- Loseille-Löhner (2013) Visibility-Cone-Bedingung <n_i, n_Tj> >= cos(80 deg) > 0.
- Multi-Normals an Kanten und Ecken mit 1/cos(theta/2) Dickenkorrektur.
- Boundary Layer Inflation Grading: Explizite geometrische Progression zur Wand mit Streckungsfaktor r_stretch in [1.15, 1.20] zur sicheren Auflösung der viskosen Unterschicht bis y+ <= 1 (z. B. an katalytischen Wänden oder Turbinenschaufeln).
- Medial-Axis-Abstand R_medial(x_i) und Schutz der ersten Zellhöhe h_1 via Layer Step-Down delta_{i,k} <= min(0.4 d_opp, 0.8 R_medial) bzw. Marcon Macro-Layer Splitting (keine Quetschung von h_1 auf 0!).
- Konformes Stitching der Inflationsschicht mit dem Kernnetz (wasserdichte Anbindung ohne Hanging Nodes).

### R5. WP 6: Inversionssicheres ALE, JamTet, UM2N, Anisotrope Metrik/PUMA & Open Software Architecture (OSA) Multiphysics
- Shamanskiy & Simeon TINE mit logarithmischer Neo-Hooke-Spannung & Jacobi-Versteifung E_eff(e) = E_0 (V_e^0)^-2.5 für null akkumulierte Verzerrung bei periodischer Bewegung.
- JamTet Sphere-Packing Morphing und UM2N Monitor.
- Anisotrope Metrik: L_q-Hesse-Sensor & Fidkowski Entropie-Adjoint, puma_subdivide_cells.
- 2D faMesh-Submesh: extract_wall_film_submesh, Spalding-Newton u* und 24x24 Patch-WSS nach Jaiswal & Hsu (2026).
- Open Software Architecture (OSA) für Multiphysics-Kopplung:
  * Extensible Architektur mit generischen zell- und punktweisen Metadaten-/Tensor-Containern (`material_properties`, `region_properties`, `constitutive_data`) in UnstructuredMesh für beliebige physikalische Parameter und Materialmodelle (isotrope/anisotrope Elastizität, Wärmeleittensoren, elektrische Leitfähigkeit, Reaktionsparameter).
  * Validierungs-Referenzimplementierung für thermomechanische Kopplung mit kubischem 6x6-Steifigkeitstensor (CMSX-4 Einkristall-Superlegierung: C11 = 192 GPa, C12 = 136 GPa, C44 = 96 GPa) sowie Datenabbildung auf von Mises Spannung und Schmid-Faktor auf oktaedrischen Gleitsystemen {111}<110>.

### R6. Audit-Gremium, Zero-Shortcut Policy, Google C++ Style & Performance/Safety
- Unabhängiges 3-Agenten-Audit-Gremium (Paper-Fidelity Challenger, Code-Auditor, Mathematik-Auditor) für jedes Gate (Gate WP3, Gate WP4, Gate WP5, Gate WP6).
- Strikte Einstimmigkeitsregel: Ein Gate gilt nur dann als bestanden, wenn alle 3 Voten einstimmig auf APPROVE stehen (3:0).
- Die Ausführung durchläuft die Gates autonom nacheinander, solange jedes Gate einstimmig freigegeben wird.
- Zero-Shortcut Policy: Keine Heuristiken, keine Magic Numbers, keine Dummy-Stubs, keine Test-spezifischen Attrappen, keine stillen Fallbacks (kein nacktes except: pass) sowie vollständige, mathematisch exakte Implementierung aller Formulierungen aus den Papern.
- Verbindliche Einhaltung des Google C++ Style Guides für den gesamten C++20-Code in native/ sowie PEP 8 in Python.
- Höchste Performance: Vektorisiertes C++20, OpenMP-Parallelisierung, SIMD-Vektorisierung, cache-freundliche Structure-of-Arrays (SoA) Datenhaltung, restlose Beseitigung langsamer Python-Loops und Zero-Copy Puffer-Views.
- Höchste Speichersicherheit & Robustheit: Strikte Array-Grenzen-Prüfungen auf allen cell/node offsets, lückenlose Ausnahmebehandlung (C-ABI try/catch mit Fehlercode -2), kein Pufferüberlauf, keine Speicherlecks (vollständige Freigabe via RAII / try-finally).
- Vollständige Abschlussabnahme mit tests/test_mesher_validation_gate.py (P1 bis P6).

## 2026-11-03T00:42:25Z

CRITICAL RESOURCE DIRECTIVE FROM USER (CPU & RAM):

1. CPU Thread Limit (Background Ansys simulation running!):
- Max 16 cores/threads for all HPC runs, OpenMP parallelizations, and tests (DO NOT use all 28 cores!).
- Always set OMP_NUM_THREADS=16 for C++20 runs, binaries, and subprocesses.
- Limit parallel test runners (pytest -n ...) to maximum 16 workers.

2. RAM Usage (~95% occupied by Ansys):
- Work extremely memory-efficiently (streaming, flat SoA arrays, avoid large temporary buffers in RAM).
- Release memory immediately (RAII, cfd_mesher_free, no memory hoarding).
- Use moderate cell counts for test meshes to avoid Out-Of-Memory (OOM).

Anchor these constraints bindingly in your BRIEFING.md, plan.md, and all worker dispatch instructions!

## 2026-11-03T01:51:02Z

Additional Benchmark CAD Resources Directive received from user/caller:

In `G:\Datein\Projects\CFDSolver\OpenFOAM_base` the following official OpenFOAM test CADs and geometries are available:
- `OpenFOAM_base/openfoam/tutorials/resources/geometry/`:
  * NACA0012.obj.gz, nacaAirfoil, airfoilWithLayers (2D/3D Aerodynamics & Inflation)
  * annularCombustorTurbine, rotorDisk, propeller, mixerVesselAMI (Turbomachinery & cyclic sectors)
  * snappyMultiRegionHeater, cpuCabinet, annularThermalMixer (Multiphysics CHT & Conformal Twin-Faces)
  * injectorPipe, squareBend, baffle3D.stl.gz (Internal flow & Baffles)
- Stress-Test CADs in `OpenFOAM_base/openfoam/applications/utilities/surface/`:
  * `surfaceSplitNonManifolds/test/twoFlanges_nonManifold.stl` (True non-manifold geometry for exception validation)
  * `surfaceClean/multiCollapse.stl` (Dirty CAD for Winding Numbers & AMIPS).

These official benchmark CADs are available to be drawn upon as high-fidelity reference geometries across Gates WP3, WP4, WP5, and WP6.

## 2026-11-03T02:10:08Z

User Directive on Focused Re-Audit Efficiency received and recorded to ORIGINAL_REQUEST.md:

For remediation re-audits following a veto or REQUEST_CHANGES (such as Gate WP3 Round 2):
The audit committee auditors do NOT need to re-verify the entire unchanged codebase from scratch.
They must focus specifically on:
1. The new / modified code and affected blocks (the remediation diff),
2. The explicitly challenged findings (e.g. centroid fan removal, empty lattice guards, sizing config resolution),
3. Clean pass of the complete test suite.

Please embed this instruction directly into the dispatch prompts for the re-audit committee to optimize token usage and execution velocity.

## 2026-11-03T02:22:19Z

MANDATORY USER DIRECTIVE (End-of-Day Execution Pause / Stop after Gate WP3):

As soon as Gate WP3 is certified and unanimously approved by the 3-agent audit committee (100% APPROVE by Challenger, Code-Auditor, and Math-Auditor upon re-audit):
YOU MUST IMMEDIATELY AND ORDERLY STOP AND PAUSE EXECUTION FOR TODAY!

1. DO NOT begin WP4!
2. Upon Gate WP3 passing, generate a clean, comprehensive Checkpoint and Handoff Report for Milestone WP3.
3. Orderly terminate/cancel all background timers and tasks, and deliver your completion report to the Sentinel.
