# Frontend-Anforderungen an das Backend (v0.2.0 — Option C: Rust + `wgpu` + `egui`)

> **Zweck dieser Datei:**  
> In dieser Spezifikation definiert die **Frontend-Instanz**, welche Schnittstellen, Datenstrukturen, Schemata und Rückmeldungen sie vom **Backend** benötigt und welche strukturierten **UI/UX-Inputs** das neue native Rust-Cockpit (**Option C: `wgpu` DX12/Vulkan + `egui` im nTop-/Blender-Stil**) an das Backend übergibt.  
> **Visuelle & farbliche Referenz:** Siehe `frontend/design_reference/DESIGN_REFERENCE.md` und `frontend/design_reference/palettes.json`.

---

## 1. Neue Zwei-Kanal-Schnittstellenarchitektur (Rust Cockpit <-> Backend)

Da das Frontend als eigenständige, kompilierte **Rust-Desktop-Applikation** läuft (0 % Idle-GPU, 120+ FPS, kein Browser-/WebSocket-RAM-Limit), kommuniziert es über zwei klar getrennte Kanäle mit dem Backend:

### Kanal A — High-Speed Zero-Copy Datenpfad (Natives Rust / Binärpuffer)
Für große 3D-Geometrien, Volumengitter (`polyMesh`) und Skalar-/Vektorfelder wird **niemals JSON** verwendet:
1. **Direktes Rust-Crate-Linking (`rlib`):**  
   Das Frontend bindet `backend/native_rust/foam_fast_io` direkt als native Cargo-Dependency ein (`FoamMesh`, `FoamBoundaryPatch`, `FoamField`, `FoamFieldData`).
2. **Flache Binärpuffer für CAD-Tessellierung & Mesh-Preview:**  
   Vor dem Schreiben eines vollständigen OpenFOAM-Cases liefert die `CADGeometryEngine` bzw. `MeshEngine` flache Binär-Arrays (oder eine `.bin`/Memory-Map-Datei im Case-Scratch-Ordner) für sofortiges GPU-Uploading.

### Kanal B — Steuerungs-, Schema- & Telemetrie-Kanal (JSON-RPC 2.0 / NDJSON IPC)
Für Konfiguration, CAD-Audit, Vernetzungsstart, Physik-Registry und Solver-Steuerung kommuniziert das Rust-Frontend über einen lokalen IPC-Kanal (**JSON-RPC 2.0, eine Nachricht pro Zeile als NDJSON über `stdin/stdout` oder Named Pipe**, Diagnose-Logs strikt getrennt auf `stderr`) mit dem Python-Orchestrator.
* **Asynchrone Jobs & Abbruch-Sicherheit:** Langläufer (`geometry.import`, `mesh.generate`, `solver.start`) liefern sofort eine `job_id` und senden danach Fortschritts-, Abschluss- oder Fehler-Events. Über `job.cancel` abgebrochene Jobs dürfen **niemals** ein scheinbar gültiges Teil-Ergebnis veröffentlichen.
* **Methoden-Übersicht (vom Backend mit Status *vorhanden / geplant / nicht unterstützt* zu bestätigen):**

| Methode | Wesentliche Inputs | Benötigte Antwort / Events | Backend-Status |
| :--- | :--- | :--- | :--- |
| `system.hello` | `protocol_version` | Backend-Version, Fähigkeiten (`capabilities`), unterstützte Formate | **vorhanden** (M5D / `backend/ipc/`) |
| `schema.get` | Modul- / Solver-Auswahl | Parameter-Definitionen, Typen, Defaults, Grenzen, SI-Dimensionen, `visible_if` | **vorhanden** (M5D / `ModuleRegistry.export_ui_schema()`) |
| `geometry.import` | Pfad, Format, Quell-Einheit, Toleranzen | `job_id` $\rightarrow$ `geometry_id`, `geometry_revision`, Bounds, Patch-Liste, Binär-Deskriptor, Leck-Diagnose | **vorhanden** (M5D / Paket 1 Binary Buffer) |
| `geometry.patches.update` | `geometry_id`, `geometry_revision`, Flächen-IDs, Patchname/-typ | Aktualisierte Patchliste, neue `geometry_revision`, Validierungsfehler | **vorhanden** (M5D / Revisionsinkrementierung) |
| `mesh.generate` | `geometry_id`, Domain, Größenfelder, Verfeinerung, Grenzschicht | `job_id` $\rightarrow$ `mesh_revision`, `polyMesh`-Pfad, Qualitätsfelder (`skewness`, `nonOrtho`, `amips`, `is_folded`) | **vorhanden** (M5D / Paket 2 Binary Buffer) |
| `job.cancel` | `job_id` | Bestätigung des sauberen Abbruchs & Bereinigung unvollständiger Artefakte | **vorhanden** (M5D / `AsyncJobManager.cancel_job()`) |
| `case.validate` / `case.generate` | Aktive Physikmodule, Materialien, BCs, Numerik | Strukturierte Fehler/Warnungen (`parameter_path`, `code`) bzw. Case-Pfad | **vorhanden** (M5D / `CaseGenerator`) |
| `solver.start` / `write_now` / `stop_graceful` / `abort` | `case_id` / `run_id` | Eindeutiger Laufzustand (`started`, `running`, `stopping`, `completed`, `failed`, `aborted`) | **vorhanden** (M5D / `WslSimulationRunner`) |
| `results.list_times` / `list_fields` / `load` | `case_id`, Zeitpunkt, Feldauswahl | Verfügbare Zeitpunkte, Feld-Metadaten (inkl. SI-Dimensionsvektor & Lokalisierung) und Buffer-Deskriptor | **vorhanden** (M5D / `FoamReader` + Owner-Fallback) |

> **Strikte Einheiten- & Dimensionsregeln:**  
> 1. **SI-Normierung vor Übergabe:** Das Frontend parst Eingaben wie `12 mm`, `850 C` oder `3 bar` und übergibt Zahlenwerte im Kontrakt ausschließlich in definierten SI-Basiseinheiten (`m`, `K`, `Pa`, `rad`, `s`). Ausnahmen (z. B. Winkel in Grad für OpenFOAM `nonOrthogonality`) müssen im Schema explizit als `unit_si: "deg"` deklariert sein.  
> 2. **Absolutdruck vs. Relativdruck:** Im Schema und in Feldern muss explizit zwischen Absolutdruck $p$ (`pressure_abs`) und Relativ-/hydrostatischem Druck $p - p_\infty$ bzw. `p_rgh` (`pressure_rel`) unterschieden werden.  
> 3. **Niemals Einheiten aus dem Feldnamen erraten:** In OpenFOAM hat `p` bei inkompressiblen Solvern (`simpleFoam`) die Dimension $[0, 2, -2, 0, 0, 0, 0]$ ($\text{m}^2/\text{s}^2$, kinematischer Druck $p/\rho$), bei kompressiblen Solvern dagegen $[1, -1, -2, 0, 0, 0, 0]$ ($\text{Pa}$). Maßgeblich ist ausschließlich der 7-teilige Dimensionsvektor `[kg, m, s, K, mol, A, cd]`.  
> 4. **Ziel-$y^+$ vs. erste Zellhöhe $\Delta y_1$:** Nicht stillschweigend austauschbar! Das Meshing-Backend erhält immer die geometrische Höhe `first_layer_thickness_m` ($\Delta y_1$ in $\text{m}$). Ein $y^+$-Schätzer im UI dient nur als Vorberechnungshilfe unter expliziter Angabe von $(U_\infty, \rho, \mu, L_{\text{ref}})$.

---

## 1.B Geometrie- und Feld-Buffer-Vertrag (Revisions- & Lokalisierungssicherheit)

Jeder zwischen Backend und Frontend ausgetauschte Geometrie- oder Felddatensatz (egal ob via `foam_fast_io` oder Binärpuffer) benötigt einen eindeutigen Deskriptor:
* **Identität & Revision:** `dataset_id` sowie `geometry_revision` bzw. `mesh_revision`. Patch-IDs (`face_group_id`) sind nur innerhalb derselben Revision stabil; nach einem Remeshing dürfen alte Zuordnungen nicht ungeprüft weiterverwendet werden.
* **Speicherlayout:** Koordinatensystem (rechtshändig, Achsenkonvention, Längeneinheit `m`), Datentyp (`f32` / `f64` / `u32`), Komponentenanzahl ($1$ Skalar, $3$ Vektor, $6$ symm. Tensor, $9$ Tensor), Elementanzahl, Endianness (`little_endian`), Stride sowie klare Ownership-/Lebensdauer-Regeln.
* **Zwingende Feld-Lokalisierung (`location: "cell" | "point" | "face" | "boundary_patch"`):**
  * **Zelldaten (`cell`):** Qualitätsmetriken (`nonOrthogonality`, `skewness`, `amips`) und `NaN`-Markierungen gehören zur Zelle und werden im 3D-Viewport **flach (faceted, ohne Knoten-Interpolation)** gezeichnet, damit einzelne defekte Zellen nicht zwischen intakten Nachbarn weichgezeichnet werden.
  * **Wand-/Randdaten (`boundary_patch`):** Größen wie Wandtemperatur $T_w$, `yPlus` oder Wandschubspannung dürfen **niemals stillschweigend durch interpolierte Zellzentrenwerte ersetzt** werden. Fehlen echte `boundaryField`-Werte oder liegt `zeroGradient` vor, muss die Herkunft im Deskriptor explizit als `source: "boundary_field" | "owner_cell_fallback"` gekennzeichnet sein.

---

## 2. Was das UI/UX an Inputs liefert -> Was das Backend bereitstellen muss

Die Benutzeroberfläche ist nach Blender-Vorbild in **Workspaces** oben und einen **nTop-Block-Stack** links gegliedert. Daraus ergeben sich folgende konkrete Schnittstellenpakete:

### Paket 1: CAD-Import, Audit & Patch-Zuweisung (`backend/geometry/`)

* **Inputs vom Frontend (UI -> Backend):**
  ```json
  {
    "command": "cad.import_and_audit",
    "file_path": "resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp",
    "source_unit": "mm",
    "chordal_tolerance_m": 0.0001,
    "angular_deflection_deg": 10.0,
    "domain_mode": "cht_conjugate"
  }
  ```
  * Sowie Patch-Umbenennung / Gruppierung durch Klick auf Flächen im 3D-Viewport: `cad.assign_patches` (`surface_group_ids` $\rightarrow$ `patch_name`: z. B. `inlet`, `outlet`, `blade_wall`, `cooling_channels`, `patch_type`: `wall` | `patch` | `symmetry` | `empty`).

* **Benötigter Output vom Backend (Backend -> Frontend):**
  1. **Tessellierter Binär-Geometriepuffer** zur Darstellung im MatCap/Cavity-CAD-Shader und für **Raycasting-Selektion einzelner CAD-Flächen (`#004AFD` Electric Blue)**:
     * `vertices`: `[f64; 3 * N_verts]` (für Camera-Relative `f64` $\rightarrow$ `f32` Berechnung)
     * `normals`: `[f32; 3 * N_verts]`
     * `triangles`: `[u32; 3 * N_tris]`
     * `face_group_id`: `[u32; N_tris]` (damit ein Klick auf ein Dreieck das gesamte CAD-Face / den Patch selektiert!)
  2. **Strukturierter Audit-Report (JSON):**
     * `bounding_box`: `{"min": [x, y, z], "max": [x, y, z]}`
     * `is_watertight`: `bool`, `surface_area_m2`: `float`, `volume_m3`: `float`
     * **Defekt-Koordinaten zum direkten 3D-Highlighting:** Liste offener Kanten (`open_edge_segments: [[p1, p2], ...]`) und Non-Manifold-Kanten, damit das Frontend Lecks sofort rot (`#EF233C`) / Acid Lime (`#B9F500`) am Bauteil anleuchten kann!

---

### Paket 2: Vernetzung & Gitterqualitäts-Inspektion (`backend/meshing/`)

* **Inputs vom Frontend (UI -> Backend):**
  ```json
  {
    "command": "mesh.generate",
    "case_dir": "cases/turbine_blade_run01",
    "strategy": "octree_hexcore",
    "domain_mode": "fluid_external",
    "base_cell_size_m": 0.005,
    "refinement_zones": [
      { "target_patch": "blade_wall", "level": 3, "radius_m": 0.01 }
    ],
    "boundary_layers": {
      "enabled": true,
      "patches": ["blade_wall"],
      "n_layers": 15,
      "first_layer_thickness_m": 1.2e-5,
      "expansion_ratio": 1.2
    },
    "cut_cell_settings": {
      "small_cell_ratio": 0.15,
      "wsrd_target_alpha": 0.5
    }
  }
  ```

* **Benötigter Output vom Backend (Backend -> Frontend):**
  1. Exportiertes `constant/polyMesh` im Case-Ordner (wird vom Frontend sofort per Zero-Copy über `foam_fast_io::load_poly_mesh` geladen).
  2. **Zellqualitäts-Felder für die Ampel-Colormap (`traffic_light`):**
     * Entweder direkt als OpenFOAM-Felder in `0/` (`skewness`, `nonOrthogonality`, `aspectRatio`, `cellVolume`, `wsrdWeights`, `amipsEnergy`) oder als binärer Qualitäts-Dump (`[f32; N_cells]` pro Metrik).
     * Liste kritischer / invertierter Flächen (`folded_face_indices` aus `mf.is_folded`), damit der User im *Mesh-Inspection-Workspace* per Slider alle Zellen mit z. B. $\text{Non-Orthogonality} > 65^\circ$ isolieren kann.
  3. **Kompakter Mesh-Summary-Report:** `n_cells`, `n_points`, `n_faces`, Zelltyp-Aufschlüsselung (`hex`, `prism`, `polyhedron`), `max_skewness`, `max_non_ortho`, `min_volume`.

---

### Paket 3: Deklaratives Physik-Schema & Randbedingungen (`ModuleRegistry` & `CaseGenerator`)

Damit das Rust-Frontend den **nTop-Block-Stack** auf der linken Seite dynamisch und typsicher aufbauen kann, benötigt es vom Backend einen Schema-Export (`registry.export_ui_schema()`):

#### A. Format-Vorgabe für das Modul- & Parameter-Schema (Backend -> Frontend)
```json
{
  "module_id": "Viscous",
  "display_name": "Viscous & Turbulence",
  "ui_category": "flow_turbulence",
  "description": "RANS / LES Turbulenzmodellierung und Wandfunktionen",
  "variants": ["Inviscid", "Laminar", "Spalart-Allmaras", "k-epsilon", "k-omega SST", "LES WALE"],
  "default_variant": "k-omega SST",
  "requires_modules": [],
  "parameters": {
    "turbulence_intensity": {
      "type": "float",
      "label": "Turbulenzintensität I",
      "description": "Freistrom-Turbulenzgrad am Einlass",
      "default": 0.05,
      "min": 0.001,
      "max": 0.50,
      "unit_si": "-",
      "display_unit_default": "%",
      "visible_if": { "variant_not_in": ["Inviscid", "Laminar"] }
    },
    "wall_treatment": {
      "type": "select",
      "label": "Wandbehandlung",
      "options": ["lowRe_yPlus1", "nutkWallFunction"],
      "default": "lowRe_yPlus1",
      "visible_if": { "variant_in": ["k-omega SST", "Spalart-Allmaras"] }
    }
  }
}
```

#### B. Fachliche Parameter-Anforderungen je Physik-Modul
1. **Viscous & Turbulence:** `model_variant` (`Inviscid`, `Laminar`, `Spalart-Allmaras`, `k-epsilon` [Standard/RNG/Realizable], `k-omega SST` [Standard/SAS], `LES WALE` / `Smagorinsky`), Turbulenzintensität $I$, turbulentes Längenmaß $L$ bzw. $\mu_t / \mu$, Schalter `Wall Functions` ($y^+ > 30$ vs. $y^+ \approx 1$).
2. **Energy & Heat:** `sensibleEnthalpy` vs. `sensibleInternalEnergy`, Referenztemperatur $T_{\text{ref}}$, CHT-Kopplungsmodus für Festkörperzonen.
3. **Species & Chemistry:** Reaktionsmodell (`Finite-Rate / Eddy-Dissipation`, `Surface Heterogeneous Kinetics / Generic Catalysis`, `Inert Species Transport`), Pfad zur Chemkin-/OpenFOAM-`reactions`-Datei, benutzerdefinierte Speziesliste (`species`), Auswahl der katalytischen `cellZone` / `patch`.
4. **Discrete Phase (DPM):** Injektions-Typ (`Surface`, `Point-Cone`, `Volume`), Startposition & Richtungsvektor (im 3D-Viewport per Gizmo platzierbar!), Durchmesserverteilung (`Monodispers`, `Rosin-Rammler`, `Normal`), Massenstrom $[\text{kg/s}]$, Partikeldichte $\rho_p$, Kopplung (`1-Way` vs. `2-Way`).
5. **Radiation:** Modell (`P1`, `fvDOM`), Winkeldiskretisierung ($n_\phi, n_\theta$), Absorptionskoeffizient $a$ $[1/\text{m}]$, Streukoeffizient $\sigma_s$ $[1/\text{m}]$.
6. **Battery Thermal:** `ECM 2-RC`, Nennkapazität $[\text{Ah}]$, $R_0 [\Omega]$, $R_1, C_1, R_2, C_2$, Entropiekoeffizient $dU/dT [\text{V/K}]$, Arrhenius-Thermal-Runaway-Trigger.
7. **Eulerian Wall Film (EWF):** Filmdichte $\rho_f$, Viskosität $\nu_f$, Verdampfungsmodell (`None`, `StandardEvaporation`), kritische Ablöseschubspannung $\tau_{\text{crit}}$.
8. **Multiphase / Porous / Acoustics / Ablation / MHD:** Analoge Parameterdefinitionen (Oberflächenspannung $\sigma$ für VoF; Darcy-Forchheimer-Tensoren $D, F$ + lokale Koordinatenachsen für Porous; FWH-Mikrofonkoordinaten für Acoustics; Magnetfeldvektor $\mathbf{B}_0$ für MHD).

#### C. Randbedingungs- & Material-Übergabe (UI -> Backend `CaseGenerator`)
Das Frontend sendet pro Patch aus `FoamMesh.boundaries` eine typisierte Zuordnung:
* `patch_name` $\rightarrow$ `bc_preset` (`velocity_inlet`, `mass_flow_inlet`, `total_pressure_inlet`, `pressure_outlet`, `no_slip_wall_adiabatic`, `no_slip_wall_fixed_T`, `no_slip_wall_heat_flux`, `cht_coupled_wall`, `symmetry`, `cyclic_ami`) + zugehörige SI-Werte.
* Material-Auswahl aus `resources/materials_db/` (inkl. Abfrage-Endpunkt `materials.list` für Stoffwerte, NASA-Polynome & Wärmeleitfähigkeit).
* Das Backend liefert vor dem Start über `case.validate_and_preview` eventuelle Inkonsistenzen (`errors`, `warnings`) sowie den automatisch gewählten OpenFOAM-Solvernamen zurück.

---

### Paket 4: Solver-Kontrolle & Echtzeit-Streaming (`WslSimulationRunner`)

* **Inputs vom Frontend (UI -> Backend):**
  * **Solver-Setup:** `solver_mode` (`steady` | `transient`), `end_time_or_iters`, `delta_t`, `adjust_time_step` (`bool`), `max_co`, `write_interval`, Unterrelaxationsfaktoren (`p`, `U`, `h`, `k`, `omega`), MPI-Parallelisierung (`n_ranks`, `p_core_weight`: 1.5, `e_core_weight`: 1.0).
  * **Laufzeit-Steuerung:**
    * `sim.start` — Startet Decompose + MPI-Run in WSL2.
    * `sim.write_now` — Triggert OpenFOAM `writeNow` im `controlDict` (ohne Abbruch!).
    * `sim.stop_graceful` — Setzt `stopAt writeNow`, schreibt aktuellen Zustand sauber raus und beendet MPI.
    * `sim.abort` — Sofortiger SIGTERM/SIGKILL auf verwaiste WSL2-MPI-Prozesse.

* **Benötigter Live-Telemetrie-Stream vom Backend (NDJSON pro Iteration):**
  ```json
  {"type":"iter","step":240,"time":0.024,"dt":1e-4,"co_mean":0.18,"co_max":0.79,"wall_s":14.2,"residuals":{"Ux":8.1e-5,"Uy":6.2e-5,"Uz":1.1e-5,"p":1.9e-3,"h":3.4e-6,"k":1.2e-4,"omega":7.5e-5},"monitors":{"Cd":0.042,"Cl":0.891,"T_max":1184.5,"n_nan_cells":0}}
  ```
  ```json
  {"type":"timestep_written","time_dir":"0.020","reconstructed":true,"available_fields":["U","p","T","k","omega","yPlus"]}
  ```

---

## 3. Konkrete Erweiterungs-Wünsche an `backend/native_rust/foam_fast_io`

Das bestehende Rust-Crate `foam_fast_io` ist bereits eine exzellente Basis (`FoamMesh` in SoA-Form, `FoamField`). Für das 3D-Rendering im neuen Rust-Cockpit benötigt das Frontend dort noch **3 gezielte Erweiterungen**:

1. **`boundaryField`-Parsing in `src/fields.rs` (Hohe Priorität!):**
   * Aktuell liest `FoamField::load` nur `internalField`. Für Wandkonturen auf Bauteilen (z. B. Schaufel-Oberflächentemperatur $T_w$, `yPlus`, Wandschubspannung, Wanddruck) stehen die exakten Randwerte im Block `boundaryField { <patchName> { value uniform ... | nonuniform List<scalar|vector> ... } }`.
   * *Wunsch:* `pub boundary_fields: HashMap<String, PatchFieldValues>` in `FoamField` ergänzen (mit automatischem Fallback auf die `owner`-Zellwerte des Patches bei `zeroGradient` / `calculated`).
2. **Zeitschritt- & Decomposed-Case-Discovery (`list_time_steps`):**
   * Hilfsfunktion `list_time_dirs(case_path: &Path) -> Vec<TimeStepInfo>` (sortierte numerische Zeiten `[0.0, 0.005, 0.01, ...]`, Liste der Feldnamen pro Zeitordner sowie Flag, ob die Daten rekonstruiert im Root oder verteilt in `processor0..N` liegen).
3. **Binäres OpenFOAM-Format (`format binary;` Header-Support):**
   * Sobald OpenFOAM-Läufe für maximale Schreibgeschwindigkeit auf `writeFormat binary;` umgestellt werden, sollte `FoamStream` neben ASCII/Gzip auch binäre Skalar-/Vektorlisten und `points`/`faces` direkt per Slice/Memory-Map lesen können.
