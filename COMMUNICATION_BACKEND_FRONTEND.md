# Kommunikationsprotokoll: Backend <-> Frontend
> **Zweck dieser Datei:**  
> Dies ist das lebendige Übergabedokument zwischen der **Backend-Instanz** und der **Frontend-Instanz**.  
> Immer wenn eine Instanz Änderungen an Schnittstellen, Datenformaten, Pfaden oder Feldern vornimmt, wird hier ein Eintrag angelegt, damit die andere Seite sofort weiß, was verfügbar ist und wie darauf zugegriffen werden kann.

---

## 1. Aktueller Status der Schnittstelle (v0.2.0 — Architekturwechsel Frontend auf Option C)

| Eigenschaft | Spezifikation |
| :--- | :--- |
| **Frontend-Architektur (NEU)** | **Option C: Natives Rust Desktop-Cockpit (`wgpu` DirectX 12 / Vulkan + `egui`)** im **nTop / Blender**-Design (Event-Driven 0 % Idle-GPU, Camera-Relative `f64`->`f32`, Reverse-Z) |
| **Design- & Colormap-Referenz** | `frontend/design_reference/DESIGN_REFERENCE.md` & `frontend/design_reference/palettes.json` |
| **Zero-Copy 3D-Datenpfad** | Direktes Rust-Linking gegen `backend/native_rust/foam_fast_io` (`rlib`: `FoamMesh`, `FoamField`) + Binär-Triangulations-Puffer für CAD (`backend/geometry/`) |
| **Steuer- & Orchestrierungs-Kanal** | Sprachunabhängiger JSON-Schema-/IPC-Kontrakt (NDJSON Stream / Local RPC) zu Python (`ModuleRegistry`, `CADGeometryEngine`, `MeshEngine`, `CaseGenerator`, `WslSimulationRunner`) |
| **Legacy-Prototyp** | `frontend/cockpit/app.py` (Trame/WebGL — abgelöst durch natives Rust-Cockpit wegen Browser-RAM-Limits & Serialisierungs-Overhead) |

---

## 2. Verfügbare Datenobjekte & Pfade

### A. Physik-Module (Backend -> Frontend)
Das Frontend importiert die Registry direkt über:
```python
from backend.physics_modules.registry import ModuleRegistry

registry = ModuleRegistry()
modules = registry.get_all_modules()  # Gibt Dictionary aller verfügbaren Module zurück
```

Jedes Modul liefert über `mod.to_dict()` folgende Struktur:
```json
{
  "name": "Energy",
  "category": "Thermal",
  "description": "Löst die thermische Energiegleichung...",
  "enabled": true,
  "model_variant": "sensibleEnthalpy",
  "parameters": {},
  "required_fields": ["T", "h", "thermo:Cp", "thermo:rho"],
  "supported_solvers": ["chtMultiRegionFoam", "buoyantPimpleFoam"]
}
```

### B. Verfügbare 3D-Skalar- und Vektorfelder für den Viewport
Sobald ein OpenFOAM-Case berechnet ist oder geladen wird, stellt das Backend folgende Standard-Felder bereit:

| Feldname | Symbol | Einheit | Physikalisches Modul | Typ |
| :--- | :--- | :--- | :--- | :--- |
| `T` | Temperatur | $\text{K}$ | Energy / Radiation / Battery | Skalar |
| `p` / `p_rgh` | Druck | $\text{Pa}$ | Flow / Multiphase | Skalar |
| `U` | Geschwindigkeit | $\text{m/s}$ | Flow / Viscous | Vektor $(U_x, U_y, U_z)$ |
| `k` | Turbulente kinetische Energie | $\text{m}^2/\text{s}^2$ | Viscous & Turbulence | Skalar |
| `omega` / `epsilon`| Dissipationsrate | $1/\text{s}$ | Viscous & Turbulence | Skalar |
| `alpha.water` | Phasenanteil | $-$ | Multiphase (VoF) | Skalar $[0, 1]$ |
| `Y_<species>` (z. B. `Y_O2`, `Y_H2`) | Spezies-Massenanteile | $\text{kg/kg}$ | Species & Chemistry | Skalar $[0, 1]$ |
| `filmHeight` | Wandfilmdicke | $\text{m}$ | Eulerian Wall Film (EWF) | Skalar |
| `B` | Magnetische Flussdichte | $\text{T}$ | Magnetohydrodynamics (MHD)| Vektor |
| `soundPressureLevel`| Schalldruckpegel | $\text{dB}$ | Acoustics | Skalar |

---

## 3. Changelog & Übergabe-Historie

### [2026-10-19] — Initiale Übergabe (Backend -> Frontend)
* **Autor:** Master / Backend Initialisierung
* **Was wurde implementiert?**
  1. `backend/physics_modules/registry.py`: Registry mit 12 Modulen (Energy, Viscous, Multiphase, Species, DPM, EWF, Radiation, Acoustics, Ablation, Battery, MHD, Porous).
  2. `backend/core/case_generator.py`: Automatische Erstellung von `controlDict`, `fvSchemes`, `fvSolution` und `decomposeParDict` mit P-Core/E-Core Lastgewichtung.
  3. `backend/native_cpp/smr_catalysis/`: C++ `fvOption` für generische Reaktionsquellterme (`genericReactionSource`) mit Jacobi-stabilisierter Hauptdiagonale (`eqn.diag()`).
  4. `frontend/cockpit/app.py`: Trame + PyVista Basis-Applikation mit Modul-Switches in der Sidebar und VTK-Viewport.
* **Wo zu finden?**
  * `G:\Datein\Projects\CFDSolver\backend\`
  * `G:\Datein\Projects\CFDSolver\frontend\cockpit\app.py`
* **Hinweis an das Frontend:**  
  Das Frontend kann Module jetzt per `registry.enable_module(name, variant)` und `registry.disable_module(name)` dynamisch umschalten. Validierungswarnungen werden über `registry.validate_configuration()` als Liste von Strings geliefert.

### [2026-10-19] — CAD- & Geometrie-Ingestion Engine bereitgestellt (Backend -> Frontend)
* **Autor:** Backend / Teamwork Sentinel
* **Was wurde implementiert?**
  1. `backend/geometry/pipeline.py`: `CADGeometryEngine`-Fassade für automatisches Laden, Auditieren, Skalieren ($mm \to m$) und Exportieren für OpenFOAM.
  2. Multi-Format Support: ISO STEP (AP203, AP214, AP242), Binary & ASCII STL (mit `cKDTree` Vertex Welding), Wavefront OBJ.
  3. Siemens NX 1926 Native Bridge (`backend/geometry/bridge/`): Automatische Konvertierung von nativer `.prt` zu STEP AP214 in $< 2\,\text{s}$.
  4. OpenFOAM Exporter (`backend/geometry/exporter/`): Schreibt direkt nach `constant/triSurface/*.stl` und generiert passende Geometrieblöcke für `system/snappyHexMeshDict`.
  5. Vollständig validiert mit 272/272 bestandenen Tests (10/10 OpenFOAM `surfaceCheck`-Kriterien, ASME V&V 10/20).
* **Wo zu finden?**
  * `G:\Datein\Projects\CFDSolver\backend\geometry\`
* **Hinweis an das Frontend:**
  Das Cockpit kann über `CADGeometryEngine.load(path).audit()` direkt BoundingBox, Dichtheitsstatus (`is_watertight`), Flächenanzahl und Dreiecke für den PyVista 3D-Viewport abfragen und Geometrien mit einem Klick in das Case-Verzeichnis exportieren.

### [2026-11-07] — Architektur-Entscheidung Frontend: Wechsel auf Option C (Rust + `wgpu` + `egui`) & Design-Referenz (Frontend -> Backend)
* **Autor:** Frontend-Instanz
* **Was wurde festgelegt & angelegt?**
  1. **Technologiewechsel auf Option C:** Das Frontend wechselt von HTML/Trame auf eine **native Rust GPU-Applikation (`wgpu` mit DirectX 12 / Vulkan + `egui`)**, orientiert an **nTop** (farbcodierter Physik-Block-Stack, einheitenbewusste Inputs, MatCap/Cavity CAD-Shading) und **Blender** (Workspace-Tabs, Outliner + Property-Inspector, Zeitleiste, Event-Driven Rendering mit 0 % GPU-Last im Leerlauf).
  2. **Design- & Colormap-Spezifikation:** Hinterlegt unter `frontend/design_reference/DESIGN_REFERENCE.md` und maschinenlesbar unter `frontend/design_reference/palettes.json` inkl. Referenz-Screenshots (`assets/`), 6 UI-Farbtriaden (`#111217` Obsidian & `#18181C` Carbon als neutrale Dark-Basis, `#004AFD` Electric Blue, `#B9F500` Acid Lime, `#FEC40B` Amber, `#FC75B4` Pink, `#723BFA` Violet für Block-Kategorien) sowie wissenschaftlichen 1D-LUT-Colormaps (*Inferno*, *Ampel*, *Aero*, *Grün-Weiß*, *Dark Ember*, *Fire*, *Moreland Cool-Warm*, *Ansys CFD-Post Maps*).
  3. **Aktualisierte Backend-Anforderungen:** Vollständige Spezifikation aller UI->Backend-Inputs, JSON-RPC/IPC-Endpunkte und `foam_fast_io`-Erweiterungen (`boundaryField`-Parsing, Zeitschritt-Discovery, Binär-Mesh/CAD-Übergabe) in `FRONTEND_REQUIREMENTS_ON_BACKEND.md` (v0.2.0) eingetragen.
* **Wichtiger Hinweis an das Backend:**
  * Da das Cockpit nun als **eigenständige kompilierte Rust-Binary** läuft, bindet es `backend/native_rust/foam_fast_io` direkt als native Rust-Dependency (`rlib`, ohne Python-GIL!) für Zero-Copy Mesh- und Feld-Streaming ein.
  * Für die Python-Subsysteme (`CADGeometryEngine`, `MeshEngine`, `ModuleRegistry`, `CaseGenerator`, `WslSimulationRunner`) benötigt das Frontend eine schlanke **JSON-Schema- & IPC-Brücke** (siehe Details in `FRONTEND_REQUIREMENTS_ON_BACKEND.md`).

### [2026-11-12] — Generalisierung Physik-Module & SMR-Exzision (Backend -> Frontend)
* **Autor:** Backend Teamwork
* **Was wurde implementiert?**
  1. **SMR-Exzision & Generalisierung (Audit-K8, R1):** Alle spezifischen SMR-Xu-Froment-Abhängigkeiten wurden restlos aus dem Backend und den Schnittstellen entfernt.
  2. **Konfigurierbare Spezies-Kinetik:** `SpeciesChemistryModule` in `backend/physics_modules/registry.py` unterstützt nun beliebige Spezieslisten (`species_names`), Mechanismen-Dateien (Chemkin / OpenFOAM `reactions`) und Reaktionsmodelle (Laminar Arrhenius, EDC, PaSR, Heterogene Oberflächenkatalyse).
  3. **Generische C++ Reaktions-fvOption:** Ersatz von `smrKineticsSource` durch datengesteuerte Reaktionsquellterme (`genericReactionSource`) mit analytischer Jacobi-Hauptdiagonale für beliebige Reaktionen.
  4. **Multi-Region CHT-Export (Audit-H5):** `backend/meshing/polymesh_io.py` unterstützt nun getrennte Regionen (`constant/<region>/polyMesh`), `constant/regionProperties` sowie vollständige `mappedWall`- und `cyclic`-Patch-Attribute.
  5. **Lauffähige OpenFOAM-Cases (Audit-H6):** `CaseGenerator` erzeugt vollständige `0/`-Startfelder, dynamische `processorWeights` passend zu beliebigen `mpi_ranks` und vollständige `fvSolution`-Blöcke für `SIMPLE` (stationär) und `PIMPLE` (transient).
  6. **Gehärteter WSL-Runner (Audit-M10):** `WslSimulationRunner` nutzt `shlex.quote`, prüft Return-Codes strikt und liefert Live-Residuen-/Divergenz-Streaming.
  7. **DGCL-treue Netzbewegung (Audit-H9):** `moving_mesh.py` implementiert Simpson-Swept-Volume und BDF2-DGCL für bewegte Gitter ohne künstliche Massenquellen.
* **Hinweis an das Frontend:**
  * Im Viewport-Feldkatalog wird `CH4, H2, CO` durch dynamische Speziesfelder `Y_<species>` ersetzt.
  * Das Cockpit kann über `module.config.parameters["species"]` beliebige Spezieslisten an das Backend übergeben.

### [2026-11-14] — Fast IO & JSON-RPC 2.0 / NDJSON IPC Contracts bereitgestellt (Backend -> Frontend)
* **Autor:** Backend Teamwork (Milestone M5D / Features F64 & F65)
* **Was wurde implementiert?**
  1. **Fast IO Extensions (F64):**
     - `boundaryField`-Parsing (`pub boundary_fields: HashMap<String, PatchFieldValues>` in `fields.rs` und `foam_reader.py`) mit vollständiger Unterstützung von `uniform` und `nonuniform List<scalar|vector>` sowie automatischem Fallback auf `owner`-Zellwerte bei `zeroGradient` / `calculated`.
     - `list_time_dirs(case_path)`: Discovery numerischer Zeitordner in aufsteigender numerischer Sortierung mit Kennzeichnung von rekonstruierten vs. `processor0..N`-Zuständen und verfügbaren Feldern.
     - `format binary;`-Header-Support in `FoamStream` und `FoamReader` für binäre Skalar-/Vektor-Listen, `points`, `faces`, `owner` und `neighbour`.
  2. **JSON-RPC 2.0 / NDJSON IPC Service (F65):**
     - Vollständige Implementierung aller 16 IPC-Endpunkte in `backend/ipc/`: `system.hello`, `schema.get`, `geometry.import`, `geometry.patches.update`, `mesh.generate`, `job.cancel`, `materials.list`, `case.validate`, `case.generate`, `solver.start`, `solver.write_now`, `solver.stop_graceful`, `solver.abort`, `results.list_times`, `results.list_fields`, `results.load`.
     - Strikte Diagnose-Trennung: Logs ausschließlich auf `stderr`, `stdout` bleibt rein für JSON-RPC / NDJSON.
     - Asynchrones Job-Management mit UUIDs und atomarer Abbruch-Sicherheit via `job.cancel` (keine Veröffentlichung von unvollständigen Artefakten).
  3. **Strikte 7-Element SI-Dimensionen:**
     - Validierung aller Felddimensionen gegen `[kg, m, s, K, mol, A, cd]`. Ablehnung ungültiger Längen, Fließkommazahlen oder fehlender Einheiten.
  4. **Binäre Buffer-Verträge:**
     - **Paket 1 (Geometrie):** Flat Binary Buffer (`CFDGEO01`) für `vertices` (f64), `normals` (f32), `triangles` (u32), `face_group_id` (u32) und strukturierten Audit-JSON.
     - **Paket 2 (Gitterqualität):** Flat Binary Buffer (`CFDMSH02`) für Zellmetriken (`skewness`, `non_orthogonality`, `aspect_ratio`, `cell_volume`, `wsrd_weights`, `amips_energy` in f32), invertierte Flächen (`folded_face_indices` in u32) und Summary-JSON.
  5. **Paket 4 Live-Telemetrie-Stream:**
     - Streaming von `iter`-Events mit Zeitschritten, Residuen, Courant-Zahlen und `n_nan_cells`-Monitor sowie `timestep_written`-Events.
  6. **Deklaratives UI-Schema & Materialdatenbank:**
     - `ModuleRegistry.export_ui_schema()` liefert nTop-/Egui-kompatible Schemata aller 13 Physik-Module.
     - `MaterialDatabase` liefert thermodynamische Stoffdaten aus eingebauten Datensätzen und `resources/materials_db/*.json`.
* **Hinweis an das Frontend:**
  - Das Rust-Cockpit kann nun über `backend.ipc.IPCService` sämtliche 16 Methoden direkt ansteuern oder `python -m backend.ipc.service` als Subprozess über stdio einbinden.



