# Original User Request

## Initial Request — 2026-10-19T13:46:17Z

Entwicklung einer industrietauglichen CAD- & Geometrie-Ingestion-Engine für das CFDSolver-Backend (G:\Datein\Projects\CFDSolver\backend\geometry). Das Modul liest ISO-Standards (STEP AP203/214/242, STL, OBJ) ein, prüft Geometrien auf Dichtheit (Watertight/Manifold), skaliert korrekt (CAD-mm -> CFD-m) und exportiert optimierte Oberflächen für OpenFOAM snappyHexMesh. Zudem bindet eine optionale Bridge die lokal vorhandenen CAD-Installationen (Siemens NX 1926) an, um native Formate (.prt) automatisiert zu konvertieren.

Working directory: G:\Datein\Projects\CFDSolver\backend\geometry
Integrity mode: development
Requested team: Implementer (Developer) + Unabhängiger Validator / Kritiker

## Verification Resources & Local Environments

### 1. Reale Referenz-Dateien in G:\Datein\Projects\CFDSolver\resources\CADTestSamples\
- TestCAD.obj (Wavefront OBJ, 8 Vertices, 12 Faces)
- TestCAD.stl (Binary STL, 12 Facetten)
- TestCADSTPAP203.stp (STEP AP203)
- TestCADSTPAP214.stp (STEP AP214 aus Siemens NX 1938)
- TestCADSTPAP242.stp (STEP AP242)
- TestCAD.prt (Siemens NX 1938 Part)

### 2. Verfügbare lokale CAD-Installationen (Read-Access)
- Siemens NX 1926: C:\Program Files\Siemens\NX1926
  - Native Translatoren: NXBIN\step203ug.exe, step214ug.exe, step242.exe
- Autodesk Inventor 2026: C:\Program Files\Autodesk\Inventor 2026
- Ansys v252: C:\Program Files\ANSYS Inc\v252

## Requirements

### R1. CAD-Ingestion & Format-Unterstützung (Open Standards)
- Einlesen von ISO STEP-Dateien (.step, .stp AP203/214/242), STL (.stl ASCII/Binary) und OBJ (.obj).
- Tesselierung von B-Rep-Flächen mit einstellbarer Chordal Deflection und Winkelauflösung.
- Performante Architektur (C++/Rust-Kern oder performante Bibliotheken wie OpenCASCADE / Trimesh / Numpy).

### R2. Native CAD Bridge (Siemens NX & Co.)
- Erkennung lokaler CAD-Installationen (Siemens NX 1926).
- CLI-Wrapper zur automatischen Konvertierung nativer Formate (.prt) in STEP/STL über die NX-Translatoren (step214ug.exe etc.).

### R3. Geometrie-Audit & OpenFOAM-Vorbereitung
- Manifold- und Dichtheitsprüfung (Watertight: geschlossene Hülle, Euler-Charakteristik V - E + F = 2 für einfache Körper, 0 offene Kanten).
- Normalen-Prüfung und konsistente Ausrichtung (nach außen bzw. in die Strömungsdomäne).
- Robuste Einheiten-Transformation (200 mm -> 0.2 m) mit Bounding-Box-Verifikation.
- Export in OpenFOAM-konforme constant/triSurface/*.stl und *.obj mit sauber definierten Patch-Namen für snappyHexMeshDict.

### R4. Unabhängige Test-Suite & ASME V&V Verifikation
- Implementierung einer umfassenden Test-Suite durch einen unabhängigen Test-/Validator-Agenten.
- Verifikation gegen die realen Test-Samples in resources/CADTestSamples/.
- Programmatische Prüfung gegen OpenFOAM surfaceCheck-Kriterien (0 nicht-geschlossene Kanten, 0 degenerierte Flächen).

## Acceptance Criteria

### Funktionalität & Schnittstellen
- [ ] Alle Test-Samples (TestCAD.obj, TestCAD.stl, TestCADSTPAP203.stp, TestCADSTPAP214.stp, TestCADSTPAP242.stp) werden erfolgreich eingelesen und geometrisch analysiert.
- [ ] Die Bounding Box des gedrehten Würfels wird vor und nach der Skalierung (200 mm -> 0.2 m) exakt berechnet.
- [ ] Exportierte Oberflächennetze landen in OpenFOAM-konformer Struktur (constant/triSurface/).

### Geometrische Qualität & CFD-Tauglichkeit
- [ ] Watertight/Manifold-Check bestätigt: Geometrie ist vollständig geschlossen (keine Löcher, keine T-Junctions).
- [ ] Normalenvektoren zeigen konsistent nach außen.

### Verifikation & Testabdeckung
- [ ] Unabhängige Test-Suite durch den Validator-Agenten verifiziert die Pipeline automatisiert.
- [ ] Fehlerhafte / offene Geometrien werden erkannt und mit klaren Fehlermeldungen abgefangen.

## Follow-up — 2026-10-28T00:22:07Z

# Teamwork Project Prompt — Final

> Status: Launched — delegating to teamwork_preview  
> Goal: Industrial In-Memory Multiphysics Mesher Backbone (Rust & C++ with Python Bridge)  
> Requested team: Full teamwork pipeline with dedicated Critic / Adversarial Validator  

Entwicklung eines extrem performanten, sicheren und speichereffizienten In-Memory Multiphysics Meshers in Rust und modernem C++ (mit nahtloser Python/NumPy-Bridge) für 2D- und 3D-Geometrien, Moving Mesh, nicht-konforme Übergänge und hybride FVM-SPH-Kopplung. Der Mesher implementiert die mathematischen Grundlagen aus DiFVM (Du et al., 2026), Eulerian SPH-FVM (Wang, Haidn & Hu, 2025), adaptiver OPMIS-Kopplung (Zhang et al., 2025) und DistMesh (Persson & Strang, 2004).

Working directory: `G:\Datein\Projects\CFDSolver\backend\meshing`
Integrity mode: development

---

## Wissenschaftliche Referenzen & Vorlagen
- **DiFVM:** Du, Li, Xu & Wang (2026): *Vectorized Graph-Based Finite Volume Solver for Differentiable CFD*, arXiv:2603.15920.
- **Eulerian SPH-FVM:** Wang, Haidn & Hu (2025): *Boundary Condition from FVM into Eulerian SPH*, Comput. Phys. Commun. 307, 109429.
- **OPMIS:** Zhang, Ming, Wang & Liang (2025): *Adaptive SPH-FVM Method with Optimized Particle-Mesh Interpolation*, Int. J. Multiphase Flow 189, 105256.
- **DistMesh:** Persson & Strang (2004): *A Simple Mesh Generator in MATLAB*, SIAM Review 46(2), 329–345.
- **Spickzettel & Vorlagen:** `resources/reference_spickzettel/LMArena/` (insb. `2/backend` Rust-Kern & `1/native` C++20-Kern).
- **Referenzgeometrien:**
  - Standard-Samples: `resources/CADTestSamples/TestCAD.*` (Einheitswürfel gedreht)
  - Komplexe 3D-Geometrie mit Skizzen/Linien: `resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.*` (Turbinenschaufel)

---

## Requirements

### R1. Geometrie-, SDF- & Feature-Engine (2D & 3D)
- Schnittstelle zur bestehenden CAD-Pipeline (`backend/geometry/`), die aus analytischen Primitiven und komplexen CAD-Netzen (STL/STEP/OBJ, insb. `BladeFullRev1CFD`) kontinuierliche Signed Distance Fields (SDF) und Normalengradienten $\nabla \phi$ bereitstellt.
- Robuste Filterung von Fremd-Elementen (z. B. Hilfslinien und 2D-Skizzen in CAD-Dateien).
- Krümmungs- und Abstands-Erkennung (Curvature & Proximity Refinement) zur automatischen lokalen Netzverfeinerung an Kanten, Radien und engen Spalten.

### R2. In-Memory Unstructured Meshing Core & Randschichten (Rust & C++)
- Körperangepasste Vernetzung (DistMesh Kraftgleichgewicht auf Delaunay-Triangulierung oder inkrementelles Bowyer-Watson).
- **Inflation Layers & Buffer Layers:** Strukturierte prismatische Randschichten an Wänden mit geometrischem Wachstumsfaktor für $y^+ \sim 1$ Auflösung.
- RCM (Reverse Cuthill-McKee) Bandbreiten-Reduktion zur Minimierung von Cache-Misses.

### R3. DiFVM Graph-Topology & Structure of Arrays (SoA)
- Direkte Erzeugung der unstrukturierten FVM-Graphentopologie in flachen Arrays (SoA):
  `owner`, `neighbour`, `face_areas` ($\mathbf{S}_f$), `face_centres`, `cell_centres`, `cell_volumes`, orthogonale Projektion $\boldsymbol{\Delta}_f$ und Over-Relaxed Korrekturvektor $\mathbf{k}_f$.
- **Error-Map & Solver-Stabilisierung:** Berechnung und Speicherung zell- und flächenlokaler Qualitätsmetriken (Nicht-Orthogonalitätswinkel, Skewness, Aspekt-Ratio) direkt im SoA-Graphen als Flagging-Maske zur Stabilisierung des PISO/SIMPLE-Lösers.
- Vollständige Kompatibilität zum bestehenden `FoamMesh`-Datenmodell (`backend/native_rust/foam_fast_io/`) und C-ABI (`extern "C"`).

### R4. Hybride Eulerian SPH-FVM Kopplung (Wang et al. & Zhang et al.)
- Detektion wandnaher Partikel mit unvollständigem Träger mittels Positionsdivergenz: $\text{div}\,\mathbf{r} < 0.75\,d$.
- Konstruktion des virtuellen FVM-Randabschlussvektors: $\mathbf{A}_{iS} = -\sum_j V_j \nabla_i W_{ij}$ (exakte Nullte-Ordnung-Konsistenz).
- OPMIS-Partitionierung (Core-Partikel, Halo/Puffer-Partikel, FVM-Gitter) und momentreproduzierende Taylor-Interpolation (P2M / M2P).

### R5. Moving Mesh & Dynamische Re-Adaption
- Unterstützung für Gitterdeformation (ALE-Konzepte: Verschiebung von Randknoten mit Laplacian-Smoothing der Innenknoten).
- Schnittstelle zur Erkennung degenerierter Zellen bei Gitterbewegung und Trigger für lokale Neuvernetzung.

### R6. Python-Bridge & Fallback
- Reines Python/NumPy-Modul (`backend/meshing/mesh_engine.py`) als transparenter Fallback für Tests und Frontends, falls native Bibliotheken noch nicht kompiliert sind.

---

## Acceptance Criteria

### Geometrische & Physikalische Invarianten
- [ ] Alle generierten Zellvolumina sind strikt positiv: $V_i > 0\,\forall i$.
- [ ] Exakter Gaußscher Divergenzsatz (geschlossene Zellen): $\max_i \|\sum_{f \in \text{faces}(i)} \mathbf{S}_f\| < 10^{-11}$.
- [ ] Nicht-Orthogonalitäts-Konsistenz: $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ auf allen internen Flächen.
- [ ] Nullte-Ordnung-Randkonsistenz in SPH: $\mathbf{A}_{iS} + \sum_j V_j \nabla_i W_{ij} = \mathbf{0}$ an physikalischen Wänden ($< 10^{-10}$).
- [ ] Moment-Reproduzierbarkeit der OPMIS-Interpolation: Exakte Rekonstruktion linearer Felder $f(\mathbf{x}) = \mathbf{a} \cdot \mathbf{x} + b$.

### Inflation & Error-Map
- [ ] Randschicht-Inflation erzeugt orthogonale Prismen/Quads an Wänden mit stetigem Übergang ins unstrukturierte Netz.
- [ ] Vollständige Error-Map (Skewness, Non-Orthogonality, Aspect Ratio) ist in `mesh.error_map` abrufbar.

### Robustheit & Komplexe Geometrien
- [ ] Erfolgreiche Vernetzung und Auditierung sowohl des 200-mm-Würfels als auch der komplexen Turbinenschaufel `BladeFullRev1CFD`.
- [ ] Keine Abstürze oder Endlosschleifen durch CAD-Skizzen/Hilfslinien.

### Verifikation & Kritiker-Schleife
- [ ] 100 % bestandene Tests in `tests/test_mesher.py` (mindestens 10 eigenständige Unit- und E2E-Tests).
- [ ] Bestehen der gesamten Regressionstestsuite des Repositoriums ($> 240$ Tests).
- [ ] Abnahme und Zertifizierung durch den unabhängigen Adversarial Validator / Kritiker.

## Follow-up — 2026-10-28T00:56:23Z

DIRECTIVE FROM USER & ARCHITECTURAL MANDATE:

The user reviewed the current code generation (noticing large pure Python files like `geometry_sdf.py` +1238 lines) and requested:
"Gibt es ein Grund warum er das in Python macht? Wenn nicht dann gerne die performantere Version benutzen"

Mandate for the Orchestrator and Workers:
1. Python is ONLY for the top-level test runner, CLI wrapper, and transparent fallback bridge.
2. The primary compute-heavy meshing engine MUST be implemented natively in **Rust** (or C++20) for maximum performance:
   - High-performance SDF / Level-Set queries over analytical bodies and 3D CAD meshes (AABB tree, Rayon multi-threading).
   - DistMesh iterative force-equilibrium and Bowyer-Watson / Delaunay triangulation (refer directly to `resources/reference_spickzettel/LMArena/2/backend/src/mesh.rs` and `LMArena/1/native/src/mesh.cpp`).
   - SPH neighbor search, virtual boundary closure vectors A_iS, and OPMIS matrix inverses.
3. Expose the native Rust/C++ kernel via standard C-ABI (`extern "C"`) so `backend/meshing` Python bridge loads the native shared library for zero-overhead execution while keeping tests passing.

## Follow-up — 2026-10-28T00:57:57Z

ADDITIONAL USER DIRECTIVE ON PERFORMANCE TOLERANCE:

The user explicitly stated:
"Also C++ können sie auch benutzen oder auch Python aber nur, wenn die Rechenzeit nur um 0,1% länger dauert"

Implication:
- Python is strictly prohibited for any loop or numerical calculation where its overhead exceeds 0.1% relative to compiled C++ / Rust (which applies to virtually all geometry, SDF queries, triangulation, relaxation, SPH, and linear algebra operations).
- C++20 and Rust are both explicitly approved for high-performance implementation.
- Python is only permitted for top-level configuration, CLI routing, and high-level test harness coordination.
- Orchestrator and all workers must enforce this exact <= 0.1% rule.

## Follow-up — 2026-10-28T01:01:36Z

HOST TOOLCHAIN INFORMATION:

The user has confirmed full access to Microsoft Visual Studio 2022 Community:
- MSVC C++20/C++23 Compiler (`cl.exe`): `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.40.33807\bin\Hostx64\x64\cl.exe`
- MSVC x64 Environment Setup: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`
- CMake: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

The native C++20 meshing core (`backend/meshing/native/`) can be compiled directly into a shared library (.dll) using these tools and loaded via ctypes in the Python bridge. Rust is not installed on the host, so C++20 is the primary native target for maximum performance.

## Follow-up — 2026-10-28T01:29:34Z

NEW USER REQUIREMENT: CONJUGATE HEAT TRANSFER (CHT) & COUPLED MULTI-REGION GEOMETRY

The user explicitly requested:
"Ah und es sollte möglich sein CHT (Hea Transfer) über coupled geometry zu verdwenden, das kann im Mesher sein oder in dem dann CAD Programm"

Architectural additions to incorporate into the Mesher:
1. Multi-Region Support:
   - Ability to partition and mesh multiple adjacent domains (e.g. Fluid zone $\Omega_F$ and Solid zone $\Omega_S$, like blade cooling, heat exchangers).
   - Cell tagging: `region_id` / `zone_name` stored in the SoA topology.
2. Coupled Interface / Baffle Identification:
   - Detection and extraction of the shared fluid-solid boundary $\Gamma_{FS} = \partial \Omega_F \cap \partial \Omega_S$.
   - Boundary patch classification as `coupledWall` / `mappedWall` (OpenFOAM CHT convention).
   - Support for both conformal twin-faces (direct face-to-face flux transfer) and non-conformal interfaces (AMI/GGI area-weighted conservative interpolation) to guarantee temperature and heat flux continuity ($T_F = T_S, -k_F \nabla T_F \cdot n = -k_S \nabla T_S \cdot n$).

## Follow-up — 2026-10-28T01:33:49Z

PRIORITY MANDATE: CHECKPOINTING, DOCUMENTATION TRACEABILITY & NEW FEATURES

1. Token Budget Awareness:
   - The user's 5h budget has 29% remaining (resets in 2h 51m).
   - The team must ensure strict atomic checkpointing: each milestone must commit clean, compilable, and self-contained code so state is perfectly preserved.

2. Documentation & Traceability Integration:
   - All internal design documents, specifications (`spec_report.md`, `m1_remediation_plan.md`), architecture decision records (ADRs), and critic audit reports generated inside `.agents/teamwork/` MUST be permanently consolidated and mirrored into the project repository (under `backend/meshing/docs/` or `docs/mesher/`).
   - Every mathematical decision, sign convention, and derivation must remain completely traceable in the project repository.

3. Features 22 & 23 Approved:
   - Feature 22: Rotational/Cyclic Periodic Symmetry for Turbomachinery (`BladeFullRev1CFD`) with rotation matrix mapping $\mathbf{x}_{\text{target}} = \mathbf{R}_z(\theta) \mathbf{x}_{\text{source}}$.
   - Feature 23: Boundary Layer Pinching & Layer Termination at sharp trailing edges to eliminate negative cell volumes.

## Follow-up — 2026-10-28T01:35:05Z

USER CLARIFICATION ON TOKEN USAGE & RUN CONTINUITY:

The user explicitly stated:
"Also er kann das schon ausschöpfen, nur das der Lauf nicht abbricht"

Guidance for Orchestrator:
- The team does NOT need to throttle or artificially hold back: work at full capacity to deliver maximum progress across M1, M2, and M3.
- Primary requirement: Guard against sudden aborts. Maintain continuous checkpointing and ensure every milestone commit is clean and self-contained so that if the 5h limit is reached, the workspace is left in a 100% stable, passing, and easily resumable state.

## Follow-up — 2026-10-28T01:59:51Z

FRONTEND & CONFIGURABILITY DIRECTIVE: DECLARATIVE MESHING CONFIGURATION

The user explicitly requested:
"Kann das einstellbar gemacht werden, also das ich einstellen kann, wie groß die Auflösung sein soll und die Wachstumsrate und andere Input Parameter? Das wird es dann auch für das Frontend brauchen"

Mandate for Models and Native Engine:
1. Provide a comprehensive, fully configurable declarative configuration schema in `backend/meshing/models.py` (`MeshingConfig`, `InflationConfig`, `CurvatureSizingConfig`) with JSON/dict serialization for the frontend:
   - Sizing: `min_size` (h_min), `max_size` (h_max), `growth_rate` (default 1.2)
   - Curvature/Proximity: `curvature_normal_angle` (e.g. 6.0 deg), `cells_per_gap` (default 1-2)
   - Boundary Layers: `n_layers` (default 10), `first_layer_height`, `layer_growth_rate` (default 1.05), `transition_ratio` (default 0.1), `pinching` (bool)
   - Quality gates: `min_orthogonal_quality` (default 0.2), `max_skewness` (default 0.7)
2. In C++20, provide a corresponding `struct MeshingConfig` and C-ABI export struct (`MeshingConfigC`) so the Python bridge and frontend can pass arbitrary user parameters directly into the native mesher without re-compilation.
