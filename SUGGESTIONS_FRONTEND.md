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
