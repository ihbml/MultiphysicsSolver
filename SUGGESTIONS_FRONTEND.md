# Suggestions & Ideenpool: Frontend & 3D-Cockpit (Option C)
> **Architektur-Fokus:** Natives Rust Desktop-Cockpit (`wgpu` DirectX 12 / Vulkan + `egui`, optional `OpenXR`), Design-Orientierung an **nTop** & **Blender**.  
> **Design- & Farb-Referenz:** Siehe `frontend/design_reference/DESIGN_REFERENCE.md` und `frontend/design_reference/palettes.json`.

---

## 1. 3D-Visualisierung & `wgpu` GPU-Pipeline
* **Zwei-Phasen-Shading (CAD MatCap vs. Scientific Unlit):**
  * **CAD- & Vernetzungs-Modus:** Plastisches *MatCap-Shading* kombiniert mit *Screen-Space Cavity / Eye-Dome Lighting (EDL)* (wie in nTop & Blender) für maximale Tiefen- und Kantenlesbarkeit ohne manuelles Licht-Setup.
  * **CFD-Feld-Modus:** Streng unverfälschtes *Unlit Scientific Rendering* ohne Tone-Mapping (exakte Übereinstimmung mit der Legende) über vorberechnete 1D-LUT-Texturen im Oklab/CIELAB-Raum (stufenlos oder mit Ansys-artigen Iso-Bändern umschaltbar).
* **GPU Compute Slicing & Iso-Flächen:**
  * Echtzeit-Schnittebenen (X, Y, Z sowie frei drehbares 3D-Plane-Gizmo) direkt als WGSL-Compute-Shader über `FoamMesh` (`backend/native_rust/foam_fast_io`).
* **3D-Partikel-, Stromlinien- & LIC-Darstellung:**
  * GPU-instanzierte Strömungspartikel und Bänder entlang des Geschwindigkeitsvektors $\mathbf{U}$ sowie *Surface LIC* (Line Integral Convolution) für Wandschubspannungslinien und Ablösegebiete auf Schaufeloberflächen.
* **AR / XR-Ausbaustufe (`OpenXR`):**
  * Optionale Anbindung eines `OpenXR`-Render-Targets an denselben `wgpu`/Vulkan-Graphen zur maßstabsgetreuen 1:1-Inspektion von CAD-Baugruppen, Kühlkanälen und 3D-Wirbelstrukturen.

---

## 2. nTop- & Blender-orientierte UI/UX
* **Workspace-Tabs in der Top-Bar (Blender-Stil):**
  * Schneller Layout-Wechsel: `1. CAD & Domain` | `2. Meshing` | `3. Physics Setup` | `4. Solve & Monitors` | `5. Post-Processing`.
* **Farbcodierter Notebook-/Block-Stack (nTop-Stil, linkes Panel):**
  * Jedes Modul (Geometrie, Verfeinerung, Grenzschicht, Turbulenz, Katalyse, Strahlung, etc.) ist ein aufklappbarer Block mit semantischem Farbstreifen aus unseren definierten UI-Triaden (`#004AFD` Electric Blue, `#B9F500` Acid Lime, `#FEC40B` Amber, `#FC75B4` Pink, `#723BFA` Violet).
* **Outliner + Property-Inspector (rechtes Split-Panel):**
  * Oben: Hierarchischer Baum aller CAD-Körper, Zellzonen (`cellZones`) und Rand-Patches (`boundaries`) mit Sichtbarkeits-Auge und Drahtgitter-Toggle.
  * Unten: Kontextsensitive Detailparameter und schnelle Einheiten-Umschaltung (`K` $\leftrightarrow$ `°C`, `Pa` $\leftrightarrow$ `bar`, `m` $\leftrightarrow$ `mm`).
* **Multi-View Splitscreen & Live-Diagnose:**
  * Koppelbare Quadranten für 3D-Ansicht, logarithmische Echtzeit-Residuenkurven (`egui_plot`), 1D-Profildiagramme (z. B. $c_p$ oder $T_w$ über $x/c$) und automatischen `NaN`/Defektzellen-Detektor (`#B9F500` Acid Lime).

---

## 3. Selektiv adaptierte Konzepte aus `Qwen38maxCADRender1/2` (Ohne 1:1-Übernahme!)
* **6-Pass `wgpu` CAD- & Diagnose-Pipeline (aus `Qwen38maxCADRender1`, bereinigt um Qwen-Tessellierungsfehler):**
  * **Pass 1 (`cad_bg`):** Ruhiger vertikaler Studio-Gradient passend zur gewählten UI-Triade.
  * **Pass 2 (`cad_grid`):** Endloses Reverse-Z-Konstruktionsgitter per Fullscreen-Ray-Plane-Intersection (`fwidth()`-gefiltert, Horizont-Fade).
  * **Pass 3 (`cad_surface`):** Umschaltbar zwischen *MatCap/Curvature*, *Studio PBR*, *Gooch (Technical Illustration)*, *Zebra-Continuity* (Krümmungsanalyse) und unserem *Unlit Scientific CFD-LUT-Modus* (ohne ACES-Tonemapping!).
  * **Pass 4 (`cad_edges`):** Reverse-Z-gebiasetes Kanten-Overlay, das scharfe B-Rep-/Crease-Kanten von **offenen/nicht-wasserdichten Randkanten (`Boundary`)** unterscheidet und Lecks sofort farblich als *CAD-Doctor-Warnung* markiert.
  * **Pass 5 (`cad_pick`):** `R32Uint`-Offscreen-ID-Buffer für exaktes 1-Pixel-Patch- und Bauteil-Picking per Mausklick.
  * **Pass 6 (`cad_overlay`):** 3D-PMI-Bemaßungs-Callouts (Durchmesser `Ø`, Abstände, Flächenwinkel mit Knick-Führungslinien wie in `Qwen38maxCADRender2`) und geschlossene Schnittebenen-Kappen (*Capped Section Cuts* mit Schraffur statt hohler Schnitte).
* **Zweistufige Erweiterbarkeit (`bevy_ecs` + `Rhai`-Live-Scripting):**
  * **Kompilierte Rust-ECS-Plugins (`CadAddon`):** Fügen eigene Komponenten, Systeme (`PreUpdate`, `Update`, `PostUpdate`, `PreRender`) und `egui`-Panels hinzu, ohne den Core-Renderer anzufassen (*„Addons never touch the renderer“*).
  * **Laufzeit-Skripte (`Rhai`):** Erlauben dem Nutzer im laufenden Betrieb kleine parametrische Helfer (z. B. Lochkreis-Generator, Spaltmaß-/Clearance-Checker, automatisierte Randbedingungs-Zuweisung) inklusive eigener Mini-UI-Slider zu skripten.

---

## 4. Synthese der neuen Frontend-, Post-Processing- & WGSL-Shader-Spickzettel (Stand: 2026-11-18)

> **Kontext:** Auswertung der am 2026-11-15 ergänzten Spickzettel in [`resources/reference_spickzettel/LMArena/Frontend/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend) (vollständiger Index in **[`INDEX.md` Kategorie 7c & Snippets 18–20](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/INDEX.md)**).

| Spickzettel | Bewertung | Was wir konkret für das Rust-Cockpit (`wgpu` + `egui`) übernehmen |
| :--- | :---: | :--- |
| **[`ChatGPT6LunaMaxCAENoSync`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync)**<br>*(ChatGPT 6 Luna Max: „FLUX / CFD“)* | ⭐⭐⭐⭐⭐ **(5.0 / 5)** | • **Zero-Copy `bytemuck`-Parser ([`buffers.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/buffers.rs)):** Overflow-sichere (`checked_mul`/`checked_add`) Slice-Views für `CFDGEO01`, `CFDMSH02` und `CFDFLD03` ohne Kopieren.<br/>• **Camera-Relative `f64 -> (f32_hi, f32_lo)` Split + Reverse-Z ([`viewport.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/viewport.rs)):** Verhindert Z-Fighting auf $\Delta y_1 = 10^{-6}\,\text{m}$ Grenzschichten (`Depth32Float`, `CompareFunction::GreaterEqual`, Clear `0.0`) + asynchrones 1×1 Pixel `R32Uint` GPU-Picking (`read_id_pixel`, 256-B Row-Alignment).<br/>• **Flat-Faceted Mesh Quality & WGSL-Shader ([`mesh_quality.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/mesh_quality.rs), [`mesh_inspect.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/mesh_inspect.wgsl), [`surface_lic.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/surface_lic.wgsl), [`streamline.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/streamline.wgsl)):** `@interpolate(flat)` verhindert jedes Weichzeichnen von `NaN`-/Defektzellen; `@compute @workgroup_size(8,8,1)` berechnet 2D Surface-LIC für Wandschubspannungen $\boldsymbol{\tau}_w$; `vs_ribbon` zeichnet kamerazugewandte Geschwindigkeitsbänder.<br/>• **Lockfreie `RevisionClock` & Domänen-Typen ([`ipc.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/ipc.rs), [`physics.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/physics.rs)):** Invalidiert Patch-Zuweisungen automatisch bei neuem `mesh_revision` und trennt typsicher `AbsoluteP` vs. `ReducedPRgh` sowie `FirstLayerHeight` vs. `TargetYPlus`. |
| **[`Qwen27bCockpitCAENoSync`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync)**<br>*(Qwen 27B: „HydroCore“)* | ⭐⭐⭐⭐½ **(4.6 / 5)** | • **Perzeptueller $256\times 1$ Oklab-LUT-Generator ([`colormaps.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/fields/colormaps.ts)):** Interpoliert Stützstellen für `viridis`, `moreland`, `fire`, `darkEmber` und `traffic` in Ottossons Oklab-Raum $(L,a,b)$ inkl. optionaler Ansys-Band-Quantisierung und Isolinien-Abdunklung (`fract(t * bands) < 0.035`).<br/>• **2-Phasen MRT-Compositor & 3D Slice-Plane-Gizmo ([`shaders.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/rust/src/render/shaders.wgsl), [`engine.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/gl/engine.ts)):** Rendert `[albedo, normal, id]` in Phase 1 und verknüpft in Phase 2 MatCap + 4-Tap Normal-Luminanz-EDL (bzw. Unlit Field) mit Hover-/Selektions-Rändern sowie einem direkt im 3D-Raum ziehbaren Schnittebenen-Gizmo.<br/>• **4-Quadranten Quality-Splitscreen & Unicode-SI-Parser ([`parser.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/units/parser.ts)):** Formatiert den 7-Elemente-SI-Vektor als lesbare Hochzahlen (`kg·m⁻¹·s⁻²`) bei Einheitenfehlern (*Hinweis: die 2 kleinen Tippfehler in `rust/src/render/pipeline.rs:23,222` werden durch die korrekten Funktionen aus `ChatGPT6LunaMaxCAENoSync/viewport.rs` ersetzt*). |
| **[`CAEVisualisation1Unknown.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation1Unknown.txt)**, **[`CAEVisualisation2Unknown.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt)** & **[`MuseEmberMax`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/MuseEmberMax)** | ⭐⭐⭐⭐⭐ **(4.9 / 5)** / ⭐⭐⭐⭐☆ | • **Lock-Free SPMC Shared-Memory Consumer ([`CAEVisualisation2Unknown.txt#L537-L667`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt#L537-L667)):** Liest `Paket 3 (CFDFLD03)` im Rust-Cockpit per Seqlock + Refcount-Lease direkt aus dem OS-Shared-Memory (`CreateFileMappingA` / `shm_open`) mit 60–120 FPS ohne Kopie und ohne den Solver zu bremsen.<br/>• **64-Byte `CFDFLD03` Hex- & SPMC-Slot-Diagnose-Ansicht ([`MuseEmberMax/src/App.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/MuseEmberMax/src/App.tsx)):** Kompaktes Diagnose-Widget für das Cockpit-Footer-Dock zur Überwachung von Ringbuffer-Slot-Sequenzen, CRC32C-Prüfsummen und Consumer-Lag. |

