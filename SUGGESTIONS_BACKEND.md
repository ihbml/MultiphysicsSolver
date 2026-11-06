# Suggestions & Ideenpool: Backend, HPC Core & Optimization
> **Verbindliche Architektur-Vorgabe:** Siehe **[ARCHITECTURE.md](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md)**  
> **Fokus:** Eigenständiger In-Memory Solver (Google-Style C++ + Modern Fortran + Rust), Differenzierbare Physik (SIMP / Adjoint) & Schwarm-Optimierung (PSO / Monte Carlo).

---

## 1. Tri-Language HPC Core (`native_cpp` + `native_fortran` + `native_rust`)
* **Google-Style C++ FVM Backbone (`backend/native_cpp/`):**
  * Strikte *Structure of Arrays (SoA)* Datenhaltung statt OpenFOAM-Makro-Objekten.
  * Alle Gitterfelder ($p, \mathbf{u}, T, Y_k$) sowie das Topologie-Designfeld $\gamma_i \in [0, 1]$ liegen als flache, 64-Byte-aligned Speicherblöcke im RAM.
* **Modern Fortran Rechenkerne (`backend/native_fortran/` via `iso_c_binding`):**
  * Zustandsfreie (`pure`) Fortran-2018/2023-Subroutinen mit `bind(C)` und `contiguous`-Arrays für maximale AVX2/AVX-512 SIMD-Vektorisierung ohne Pointer-Aliasing.
  * Analytische Jacobi-Hauptdiagonale ($\frac{\partial S}{\partial \phi} < 0$) direkt in den Fortran-Kinetik- und Strahlungskernen.
* **Rust Fast-I/O, Memory Pool & Streaming (`backend/native_rust/`):**
  * Memory-Mapped Parsing (`memmap2` + `fast-float` + `rayon`) für Gitter- und Felddaten (10x–30x schneller als C++ `IOstream` oder Python).
  * Zero-Copy Übergabe der Speicherzeiger an C++/Fortran und Live-WebSocket-Streaming ans Trame-Cockpit.

---

## 2. Optimization-First Engine (`backend/optimization/`)
* **SIMP / Brinkman Topologieoptimierung ohne Remeshing:**
  * Jede Gitterzelle trägt die kontinuierliche Designvariable $\gamma_i \in [0, 1]$ ($1 = \text{Fluid}$, $0 = \text{Solid}$).
  * Brinkman-Bremsterm $-\alpha(\gamma_i)\mathbf{u}$ im Impulssatz und RAMP/SIMP-Interpolation der Wärmeleitfähigkeit $\lambda(\gamma_i)$ in der Energiegleichung.
* **Gradient Descent, L-BFGS & MMA via Adjoint / Auto-Diff:**
  * Da die Fortran- und C++-Kerne frei von Makros und globalem Zustand sind, können **LLVM Enzyme** oder **Tapenade** direkt den adjungierten Gradienten $\frac{dJ}{d\gamma_i}$ für Millionen Zellen berechnen.
* **In-Memory Warm-Start für PSO & Monte Carlo:**
  * Bei Parameterstudien (PSO / Monte Carlo) bleibt das Gitter im RAM; das konvergierte Feld des vorherigen Partikels dient als Startwert (*Warm-Start*), was 60–80 % der Iterationen einspart.

---

## 3. OpenFOAM als Blaupause & ASME V&V 20 Referenz
* `OpenFOAM_base/openfoam` wird genutzt, um Diskretisierungsformeln der 12 Physik-Module nachzuschlagen und unseren eigenständigen Solver im Kreuzvergleich (Code-to-Code Audit nach ASME V&V 20) zu validieren.

---

## 4. Verbindlicher Mesher-Sanierungsplan (`backend/meshing/` Audit vom 2026-10-29)

> **Hintergrund & Status:** Die Mesher-Unit-Tests (`test_mesher.py`, `test_mesh_engine.py`, `test_cad_meshing_validation.py`) laufen aktuell zwar grün, prüfen aber überwiegend auf Testfälle getunte Spezialpfade („läuft ohne Exception“), während allgemeine Eingaben stille Fehlresultate oder invertierte Zellen liefern.  
> Die folgenden Befunde aus dem Code-Review und den empirischen Probes der **Validierungs- und Testing-Instanz** wurden Zeile für Zeile im Quellcode verifiziert und sind durch die **Backend-Instanz** prioritär abzuarbeiten.

### 4.0 Architektonische Leitplanke für die Backend-Instanz (WICHTIG!)
1. **Kein Rückfall auf reines `snappyHexMesh`-Wrapping:** Entgegen der Annahme im Review, die Plattform sei ein reiner OpenFOAM-Wrapper, bleibt unser Kern gemäß [`ARCHITECTURE.md`](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md) ein **eigenständiger In-Memory Graph-FVM- & Topologie-Optimierungs-Solver** (C++20 / Modern Fortran / Rust). Unser Mesher muss also primär die flachen SoA-Solver-Arrays (`owner`, `neighbour`, $\mathbf{S}_f$, $\mathbf{c}_f$, $\mathbf{C}_i$, $V_i$, $\boldsymbol{\delta}_f$, $\mathbf{k}_f$ sowie die `MeshErrorMap`) im RAM liefern.
2. **Warum `faces` und `constant/polyMesh`-Export trotzdem Pflicht sind (K1):**
   * Ohne explizite `faces: List[np.ndarray]` (geordnete Knotenindizes pro Face) in [`UnstructuredMesh`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L198-L220) und ohne einen `export_openfoam_polymesh(path)`-Writer können wir unsere Netze **nicht gegen `OpenFOAM_base` (`checkMesh`, `simpleFoam`) nach ASME V&V 20 kreuzvalidieren** und im PyVista/Trame-Cockpit keine exakten Polyeder-Faces rendern.

---

### 4.1 🔴 KRITISCHE DEFEKTE (K1–K8) — Blockieren produktiven CFD-Einsatz

| ID | Befund & Code-Stelle | Ursache im aktuellen Code | Verbindliche Vorgabe für die Backend-Instanz (Fix) |
| :--- | :--- | :--- | :--- |
| **K1** | **Fehlende `faces`-Konnektivität & fehlender `constant/polyMesh`-Writer**<br>[`models.py:198-220`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L198-L220), [`difvm_topology.py:475-555`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L475-L555) | `temp_faces[f]["nodes"]` wird beim Aufbau zwar erzeugt, aber beim Erstellen von `UnstructuredMesh` verworfen. Im C++-Kern (`NativeMeshData`) fehlen `face_offsets` und `face_nodes` ebenfalls. | 1. `faces: List[np.ndarray]` (bzw. CSR `face_offsets` + `face_nodes` in C++ und Rust) in `UnstructuredMesh` speichern (Rechts-Hand-Regel: Normale zeigt von `owner` zu `neighbour` bzw. nach außen).<br>2. Writer `export_polyMesh(dir_path)` implementieren, der `points`, `faces`, `owner`, `neighbour` (Länge `n_internal_faces`) und `boundary` (OpenFOAM ASCII/Binary Format) für `checkMesh`-Audits schreibt. |
| **K2** | **2D-Polygon-Mesher erzeugt bei nicht-konvexen oder CW-Polygonen invertierte/überlappende Zellen**<br>[`mesh_engine.py:356-359`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L356-L359), [`distmesh.cpp:433-491`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/distmesh.cpp#L433-L491) | Python setzt `cfg_c.h0` nie (`h0 = 0.0`). Nativ wird dadurch `if (h0 > 0.0)` übersprungen; es werden **keine inneren Punkte** erzeugt und immer ein blinder **Zentroid-Fan** (`{c_idx, i, (i+1)%n}`) gebaut. Bei L-Polygonen liegt der Schwerpunkt außerhalb ($\min V = -0.48$, $\sum \|V\| = 2.68$ statt $0.76$); bei CW-Polygonen sind alle Volumina negativ. | 1. Vorzeichen der Polygonfläche per Shoelace-Formel prüfen: Ist $A_{\text{signed}} < 0$ (CW), Knotenreihenfolge automatisch auf CCW drehen.<br>2. `cfg_c.h0` in Python sauber aus `active_cfg.sizing` (bzw. Bounding-Box-Heuristik falls nicht gesetzt) übergeben.<br>3. Den blinden Zentroid-Fan-Fallback komplett entfernen und durch Constrained Delaunay / Ear-Clipping für kleine Punktmengen ersetzen. |
| **K3** | **Hardcodierte Millimeter-Zellgrößen & stille Auflösungs-Clamps ($n \le 30$)**<br>[`mesh_engine.py:221-231`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L221-L231) | `dx = max(3.0, h0 / 3.0)` und `dz = max(5.0, h0)` setzen unskalierte Millimeter voraus. Mit `scale=0.001` (mm $\to$ m) wird $\Delta x \ge 3\,\text{m}$ und eine Turbinenschaufel kollabiert in ~1 Zelle. Zudem kappt `nx = min(nx, 30)` jede GCI-Verfeinerungsstudie, und `threshold = 0.11 * h_min` (Zeile 269) ist hart auf einen Testfall getuned. | 1. Alle absoluten Konstanten (`3.0`, `5.0`, `0.11`) und stillen Clamps (`min(nx, 25/30/40)`) restlos entfernen.<br>2. Zellgröße strikt relativ zu `h0` (in skalierten Ziel-Einheiten) bzw. über das adaptive Sizing-Feld $h(\mathbf{x})$ bestimmen.<br>3. Optionales `max_cells`-Sicherheitslimit mit expliziter `UserWarning` statt stillem Überschreiben von `h0`. |
| **K4** | **„Body-fitted“ 3D-Mesher vernetzt den Festkörper ($\phi < 0$) als Treppenstufen-Voxel statt das Fluidgebiet**<br>[`mesh_engine.py:99-103, 271-284`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L271-L284) | 1. Zeile 99 erkennt „Würfel“ heuristisch über `len(verts) == 8 and len(faces) == 12` (klassifiziert auch Keile/Wedges falsch).<br>2. Zeile 273 behält Zellen mit `phi_c[idx] < threshold` (also das **Innere des Festkörpers**!) ohne äußere Fluid-Bounding-Box und ohne Cut-Cell-/Randprojektion. | 1. Expliziten Modus `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"]` + Farfield-Box-Konfiguration einführen.<br>2. Für `external_fluid` wird der Bereich $\phi(\mathbf{x}) > 0$ innerhalb der Farfield-Box vernetzt (`CSGDifference(FarfieldBox, BodySDF)`).<br>3. Randknoten per Level-Set-Projektion (`project_to_boundary`) bzw. Cut-Cell/Tet-Vernetzung (Spickzettel: [`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/CutMesh.cpp), [`mmg`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg)) auf $\phi = 0$ legen. |
| **K5** | **`cfd_mesher_generate_from_surface` (C++) ist ein Stub**<br>[`distmesh.cpp:259-399`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/distmesh.cpp#L259-L399) | Der Parameter `surf_triangles` wird in `generateFromSurface` **komplett ignoriert**. Es wird lediglich die Achsen-Bounding-Box von `surf_points` mit bis zu $40^3$ Hexaedern gefüllt; die anschließende Feder-Relaxation startet auf einem äquidistanten Gitter im Kräftegleichgewicht und bewirkt nichts. | `surf_triangles` in C++ tatsächlich über einen BVH/AABB-Baum (Spickzettel: [`libigl/include/igl/AABB.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/AABB.h)) auswerten, Zellen nach $\phi(\mathbf{x})$ filtern und Randknoten auf die Oberfläche projizieren. |
| **K6** | **Inflation an geschlossenen 3D-Körpern erzeugt negative Zellvolumina ($V_i < 0$)**<br>[`inflation.py:104-105, 172-199`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py#L104-L105) | Zeile 104 (`if np.dot(fn, nominal_normal) < 0.0: fn = -fn`) klappt alle Face-Normalen auf eine einzige globale `nominal_normal` um. Auf der Rückseite eines 3D-Körpers zeigen die Normalen dadurch **nach innen**, während die Knotenreihenfolge `f_arr` beim Extrudieren unverändert bleibt ($\to$ 4 von 24 Zellen beim Würfel haben $V_i < 0$). | 1. Globale `nominal_normal`-Umklappung für 3D-Flächen entfernen! Stdessen konsistente Auswärts-Normalen aus der geschlossenen 2-Manifold-Topologie (oder via $\nabla\phi(\mathbf{x})$ des SDF) verwenden.<br>2. Falls eine einzelne Face-Normale gedreht werden muss, **muss** gleichzeitig die Knotenreihenfolge `f_arr[::-1]` umgekehrt werden, damit $\det(J) > 0$ und $V_i > 0$ für jedes Prisma/Hexaeder garantiert ist. |
| **K7** | **Alle Randflächen landen in einem einzigen Patch `"wall"`**<br>[`difvm_topology.py:398, 625`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L398), [`mesh_engine.py:448`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L448) | Alle Boundary-Faces (`neighbour == -1`) werden pauschal unter `boundaries["wall"] = (n_int, n_bnd)` zusammengefasst. `SurfaceMesh.patches` aus dem CAD-Import sowie Farfield-Seiten (`inlet`, `outlet`, `symmetry`, `empty`) gehen verloren. | Boundary-Faces nach ihrer zugehörigen CAD-Patch-ID bzw. Farfield-Box-Ebene (`inlet`, `outlet`, `top`, `bottom`, `frontAndBack`, `wall`) gruppieren, zusammenhängend sortieren und als getrennte Einträge in `boundaries: Dict[str, Tuple[int, int]]` ablegen. |
| **K8** | **Fehlende Git-Versionierung des Meshers**<br>`git status` | `backend/meshing/`, `backend/native_rust/` und `tests/` waren untracked. | *(Bereits durch Sofort-Commit erledigt — künftig nach jedem abgeschlossenen Teil-Meilenstein committen!)* |

---

### 4.2 🔴 ZUSÄTZLICHE DEEP-DIVE-BUGS AUS DER GEDANKEN-ANALYSE DER VALIDIERUNGS-INSTANZ (A–F)

Diese 6 subtilen, aber schwerwiegenden Architektur- und Logikfehler wurden von der Validierungs-Instanz kurz vor ihrem Token-Limit im Code identifiziert und sind vollständig bestätigt:

1. **Bug A — Katastrophale Zelltyp-Kollision in [`_extract_cell_faces_py` (`difvm_topology.py:190-240`)](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L190-L240) (2D-Quad vs. 3D-Tetraeder):**
   * **Problem:** `_extract_cell_faces_py` entscheidet **nur** anhand der Knotenanzahl `nv = len(cell_nodes)`:
     * `nv == 4` wird **immer** als **3D-Tetraeder** (4 Dreiecks-Faces) zerlegt!
     * `nv == 5` als 3D-Pyramide, `nv == 6` als 3D-Wedge, `nv == 8` als 3D-Hexaeder.
   * **Folge:** Übergibt man ein 2D-Vierecksgitter (4-Knoten-Quads in der $xy$-Ebene, z. B. aus 2D-Inflation oder strukturiertem 2D-Gitter) oder 2D-Polygone mit 5, 6 oder 8 Ecken, zerlegt `_extract_cell_faces_py` jedes 2D-Quad in 4 koplanare 3D-Dreiecke in der $xy$-Ebene ($\to$ **Volumen $V_i = 0$**!).
   * **Fix:** Entweder explizites `cell_types: Optional[np.ndarray]` (analog VTK Cell Types: `VTK_TRIANGLE=5`, `VTK_QUAD=9`, `VTK_POLYGON=7`, `VTK_TETRA=10`, `VTK_HEXAHEDRON=12`, `VTK_WEDGE=13`, `VTK_PYRAMID=14`) übergeben oder in `_extract_cell_faces_py` vorab prüfen, ob die Zelle 2D-planar in $z=z_0$ ist (`is_2d`), sodass in 2D jede Zelle mit $N_v$ Knoten als 2D-Polygon mit $N_v$ Kanten-Faces extrahiert wird!

2. **Bug B — `generate_mesh(geometry_or_sdf)` akzeptiert gar kein `BaseSDF` & `distmesh_2d` ist Dead Code ([`mesh_engine.py:28-29, 463-492`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L463-L492)):**
   * **Problem:** `distmesh_2d` und `BaseSDF` werden in `mesh_engine.py` zwar importiert, aber **nirgends aufgerufen**. Übergibt man eine `BaseSDF`-Instanz an `generate_mesh(geometry_or_sdf)`, fliegt in Zeile 492 `TypeError: Unsupported geometry type`. Zudem nutzt der Python-Fallback in `mesh_2d_polygon` (Zeile 463–476) wieder nur den fehlerhaften Zentroid-Fan statt `distmesh_2d`, und Zeile 430 enthält den Index-Bug `(i + 1) % n_cells` statt `(i + 1) % len(poly)`.
   * **Fix:** `isinstance(geometry_or_sdf, BaseSDF)` in `generate_mesh` verdrahten und in `mesh_2d_polygon` (sowie für 2D-SDFs) `distmesh_2d` mit `PolygonSDF` und `AdaptiveSizeFunction` aufrufen.

3. **Bug C — Vorzeichen-Invertierung & verschobene Fixpunkte (`pfix`) in [`distmesh_core.py:164-259`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/distmesh_core.py#L164-L259):**
   * **Problem 1 (Zeile 164–167):** Der Kommentar lautet `# Filter points outside domain: keep if phi < -0.05 * h`, aber der Code lautet `p = p[d < 0.05 * h_vals]` (Vorzeichenfehler `+0.05` statt `-0.05`, behält Punkte außerhalb des Rands!).
   * **Problem 2 (Zeile 249–258):** Bei der Projektion `outside = d_nodes > 0.0; p[outside] -= ...` wird vergessen, `outside[:n_fix] = False` zu setzen. Dadurch werden feste Eckpunkte (`pfix`) verschoben!
   * **Problem 3 (Zeile 181):** `diffs = p[:, None, :] - pfix[None, :, :]` allokiert eine dichte $(N \times N_{\text{fix}} \times 2)$-Matrix statt `scipy.spatial.cKDTree(pfix).query(p)` zu nutzen.
   * **Problem 4 (Zeile 267–270):** Nach der finalen Delaunay-Triangulierung fehlt die Orientierungsprüfung (`orient2d > 0` für CCW) sowie das Filtern entarteter Splitter-Dreiecke (Slivers am konkaven Rand).

4. **Bug D — Inflation ([`inflation.py:150-199`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py#L150-L199)): Quetschung der ersten $y^+$-Schicht an Ecken, $V_i = 0$ bei Clearance $0$ & verschluckte Polygone:**
   * **Problem 1 (Zeile 151 & 163):** Bei `clearances[i] == 0.0` ist `np.tanh(0.0) == 0.0`, also `pinch_factors[i] = 0.0`. Da Zeile 163 `effective_dist = dist_k * pinch_factors[i]` für **alle** Schichten $k$ (auch die erste Schicht $k=0$) anwendet, kollabieren alle Schichtknoten auf den Wandpunkt $\to$ **Zellvolumen $V_i = 0$**! Zudem darf Corner-Pinching die erste Wandschicht ($h_1$ für $y^+ \approx 1$) nicht pauschal mitstauchen, sondern sollte primär die Wachstumsrate $r$ der äußeren Schichten dämpfen.
   * **Problem 2 (Zeile 116–133):** Eckennormalen werden ungewichtet gemittelt (`vert_normals[v] += fn`), ohne Winkelgewichtung und ohne die 1/$\cos(\theta/2)$-Längenkorrektur (wodurch Schichten an $90^\circ$-Ecken um den Faktor $\cos(45^\circ) \approx 0.707$ zu dünn werden).
   * **Problem 3 (Zeile 174–199):** Es werden nur Faces mit `len(f_arr) == 4` und `len(f_arr) == 3` extrudiert; Polygone mit $\ge 5$ Knoten fallen ohne `else:`-Zweig lautlos unter den Tisch.

5. **Bug E — [`rcm_reorder` (`distmesh_core.py:98-106`)](file:///G:/Datein/Projects/CFDSolver/backend/meshing/distmesh_core.py#L98-L106) vernichtet Boundary-Namen, Baffles, Cyclic-Pairs und Metadaten:**
   * **Problem:** `rcm_reorder` baut am Ende über `compute_difvm_topology(mesh.points, new_cells, regions=new_regions)` ein komplett neues `UnstructuredMesh` auf. Dabei gehen alle zuvor vorhandenen `boundaries`, `baffles`, `cyclic_pairs` und `metadata` verloren.
   * **Fix:** In `rcm_reorder` die bestehenden Arrays (`cell_centres`, `cell_volumes`, `owner`, `neighbour`) direkt über die inverse Zell-Permutation `inv_perm` umindizieren und die internen Faces neu nach `(owner, neighbour)` sortieren, sodass Boundary-Faces und ihre Indizes erhalten bleiben.

6. **Bug F — Tautologische & skalenabhängige Prüfungen in [`UnstructuredMesh.validate()` (`models.py:268-309`)](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L268-L309):**
   * **Problem 1 (Tautologie):** Da `difvm_topology.py` `k_f = S_f - delta_f` setzt, prüft `(delta_f + k_f) - S_f` nur $( \boldsymbol{\delta}_f + (\mathbf{S}_f - \boldsymbol{\delta}_f) ) - \mathbf{S}_f = 0$. Stattdessen muss geprüft werden, dass $\boldsymbol{\delta}_f$ kollinear zu $\mathbf{d}_f = \mathbf{C}_{\text{nb}} - \mathbf{C}_{\text{owner}}$ ist und die Überrelaxations-Bedingung $\boldsymbol{\delta}_f \cdot \mathbf{d}_f = \mathbf{S}_f \cdot \mathbf{S}_f \cdot \frac{\|\mathbf{d}_f\|}{\mathbf{S}_f \cdot \mathbf{d}_f}$ erfüllt!
   * **Problem 2 (Skalenabhängige Toleranz):** `tol_closure = 1e-11` ist absolut. Bei großen Millimeter-Netzen ($A_f \sim 10^4$) schlägt der Check wegen normalem FP64-Rundungsrauschen fehl, während bei Mikrometer-Zellen ($A_f \sim 10^{-12}$) selbst komplett offene Zellen bestehen! `closure` muss relativ zur gesamten Zelloberfläche $\sum_{f \in c} \|\mathbf{S}_f\|$ normiert werden.
   * **Problem 3 (Fehlende Topologie-Checks & langsame Schleife):** Die Python-Schleife `for f in range(self.num_faces):` in `validate()` und `compute_quality_report()` durch vektorisiertes `np.add.at` ersetzen und echte Invarianten prüfen: `owner < neighbour` auf internen Faces, obere Dreiecksordnung, interne Faces vor Rand-Faces, lückenlose und überlappungsfreie Abdeckung aller Rand-Faces durch `boundaries`.

---

### 4.3 🟠 HOHE & 🟡 MITTLERE PRIORITÄT (H1–H11, M1–M11)

* **H1 (Stille Fallbacks entfernen):** Keine nackten `except Exception: pass` mehr in [`mesh_engine.py:460`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L460) und [`difvm_topology.py:185, 832`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L832). Wenn `use_native=True` explizit gefordert ist oder ein Laufzeitfehler in C++ auftritt, muss zumindest ein strukturiertes `logging.warning` (bzw. im Strict-Mode eine Exception) ausgelöst werden.
* **H2 (C-ABI Härtung in [`cfd_mesher_native.cpp`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/cfd_mesher_native.cpp)):**
  * Jede `extern "C"`-Funktion in `try { ... } catch (...) { return -2; }` kapseln.
  * Vor dem Aufbau in `cfd_mesher_build_from_cells` prüfen, dass `cell_offsets[0] == 0`, `cell_offsets[c] <= cell_offsets[c+1]` und für alle Knoten `0 <= cell_nodes[k] < n_points` gilt (verhindert Out-of-Bounds-Segfaults).
* **H3 (Manifoldness-Prüfung in [`difvm_topology.py:485-489`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L485-L489)):**
  * Wenn `canonical_key` bereits in `face_map` existiert und `temp_faces[f_idx]["neighbour"] != -1` ist (d. h. eine **dritte** Zelle teilt sich dasselbe Face!), darf `neighbour` nicht still überschrieben werden, sondern es muss ein `ValueError("Non-manifold interior face shared by >2 cells")` geworfen werden. Ebenso prüfen, dass `owner` und `neighbour` die gemeinsame Fläche mit entgegengesetzter Umlaufrichtung durchlaufen.
* **H4 (CHT Twin-Face-Layout in [`extract_cht_twin_faces` (`difvm_topology.py:671-755`)](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L671-L755)):**
  * Aktuell wird bei gekoppelten internen Faces einfach `neighbour[f] = -1` mitten im internen Face-Block gesetzt (`owner`/`neighbour`-Sortierung geht kaputt!) und `boundaries["coupledWall_fluid"] = (coupled_internal[0], len(coupled_internal))` nimmt fälschlich an, dass alle gekoppelten Faces lückenlos hintereinander liegen.
  * **Fix:** Gekoppelte Faces sauber aus dem internen Block herauslösen und als **zwei zusammenhängende Boundary-Blöcke** am Ende der Face-Liste anfügen (sortiert nach Fluid-Region als Owner im ersten Patch und Solid-Region als Owner im zweiten Patch).
* **H5 (2D-Erkennung & Extrusion in [`difvm_topology.py:497`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L497)):**
  * 2D nicht über `np.all(np.abs(pts[cell_arr, 2]) < 1e-6)` erraten (versagt für 2D-Schnitte bei $z \neq 0$), sondern explizit über Zell-Topologie / Koplanarität bestimmen, und eine Hilfsmethode `extrude_2d_to_3d_slab(dz=1.0)` bereitstellen, die für OpenFOAM-Kreuzvalidierungen ein 1-Zellen-dickes 3D-Prisma/Hex-Netz mit `frontAndBack` (`empty`)-Patches erzeugt.
* **H6 (Invertierte Faces nicht als 90° maskieren in [`difvm_topology.py:578`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L578) & `difvm_mesh.cpp`):**
  * `np.clip(sf_dot_df / (sf_norm * df_norm), 0.0, 1.0)` durch `np.clip(..., -1.0, 1.0)` ersetzen! Wenn $\mathbf{S}_f \cdot \mathbf{d}_f \le 0$ ($\theta \ge 90^\circ$), liegt ein topologischer Orientierungsfehler oder stark konkaves/invertiertes Zellpaar vor $\to$ sofort in `stabilization_mask = 3` (bzw. Validierungsfehler) eskalieren!
* **H7 (Exaktes Mesh-SDF in [`geometry_sdf.py:587-793`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/geometry_sdf.py#L587-L793)):**
  * `CADMeshSDF` sucht aktuell nur unter den $k=8$ nächsten Dreiecks-Schwerpunkten (`cKDTree(self.centroids)`) und bestimmt das Vorzeichen aus der ungewichteten Normalen eines einzigen Dreiecks (`dot = np.sum(diff * best_normal)`). An scharfen Kanten/Ecken kippt das Vorzeichen, und große Dreiecke mit weit entferntem Schwerpunkt werden verfehlt.
  * **Fix nach Spickzettel ([`libigl/include/igl/signed_distance.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/signed_distance.cpp)):**
    1. **Winkelgewichtete Pseudonormalen (Bærentzen & Aanæs 2005)** auf allen Dreiecksflächen, Kanten (Mittelwert der zwei adjazenten Flächennormalen) und Eckpunkten (gewichtete Summe mit Innenwinkel $\alpha_i$) vorberechnen; je nachdem, ob der nächste Punkt `best_q` im Dreiecksinneren, auf einer Kante oder auf einem Eckpunkt liegt, die zugehörige Pseudonormale für das Vorzeichen nutzen (oder Generalized Winding Numbers nach [`fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp)).
    2. Für die Abstandssuche einen **AABB-Baum über die Dreiecks-Bounding-Boxen** statt nur über die Schwerpunkte verwenden.
* **H8 (Native DistMesh Randprojektion in [`distmesh.cpp:493-560`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/distmesh.cpp#L493-L560)):**
  * Im C++-`generate2DPolygon`-Loop werden Knoten nach `all_pts[i] += forces[i] * delta_t` **nie** auf den Polygonrand zurückprojiziert, wenn sie das Polygon verlassen! Dadurch driften Punkte nach außen, ihre Dreiecke werden durch `if (pointInPoly(c))` gelöscht und am Rand entstehen Löcher. Nach jedem Schritt müssen Punkte mit `pointInPoly(p) == false` auf das nächste Polygon-Kantensegment zurückprojiziert werden.
* **H9 (ALE Moving Mesh Randschutz & Inversionscheck in [`moving_mesh.py:279-287`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/moving_mesh.py#L279-L287)):**
  * Nicht-bewegte Randknoten nicht per Bounding-Box-Heuristik („äußerste 15 %“) raten, sondern **alle Knoten auf Boundary-Faces (`neighbour == -1`)** standardmäßig als Dirichlet-Rand (entweder vorgeschriebene Verschiebung $\mathbf{u}_{\text{wall}}$ oder $\mathbf{u} = \mathbf{0}$ am festen Außenrand) behandeln, und nach dem Update `mesh_new.validate()` aufrufen.
* **H10 (`try ... finally` in [`mesh_engine.py:360-432`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L360-L432)):**
  * `m_ptr = self._native_dll.cfd_mesher_create()` zwingend in `try: ... finally: self._native_dll.cfd_mesher_free(m_ptr)` einschließen.
* **H11 (IEEE-754 Determinismus in [`CMakeLists.txt:15, 22`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/CMakeLists.txt#L15)):**
  * `/fp:fast` (MSVC) und `-ffast-math` (GCC/Clang) durch `/fp:precise` bzw. `-fno-fast-math -ffp-contract=off` ersetzen, damit `std::isfinite` funktioniert und Reduktionen reproduzierbar bleiben. Cross-Platform-Loader in `difvm_topology.py` ergänzen (`.dll` auf Windows, `libcfd_mesher_native.so` auf Linux/WSL, `.dylib` auf macOS).
* **M1 & M6 (Konfigurations-Konsistenz & `MeshErrorMap` nach CMS-Prinzip):**
  * Redundanz zwischen `CurvatureSizingConfig` und `SizingParameters` in [`models.py`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py) auflösen.
  * Die aktuelle skalare `error_map` um die in [`ARCHITECTURE.md` (Abschnitt 4.2)](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md) spezifizierte **CMS-Geometrie-Fehlerkompensationskarte (`MeshErrorMap`)** erweitern:
    1. **Gerichteter 3D-Skewness-Offset-Vektor** $\mathbf{m}_f^{\text{skew}} = \mathbf{c}_f - \mathbf{x}_{\text{int}} \in \mathbb{R}^3$ für die explizite Gradienten-Korrektur $\phi_f = \phi_{\text{int}} + (\nabla\phi)_f \cdot \mathbf{m}_f^{\text{skew}}$,
    2. **Gedämpfter nicht-orthogonaler Korrekturvektor** $\mathbf{k}_f^{\text{comp}} = \min\!\left(1, \frac{\alpha_{\max} \|\boldsymbol{\delta}_f\|}{\|\mathbf{k}_f\| + \epsilon}\right) \mathbf{k}_f$,
    3. **Stetiger Upwind-Blending-Faktor** $\beta_f \in [0, 1]$ und **zellweiser Diagonal-Schutzfaktor** $\omega_i \in (0, 1]$ (branchless in den FVM-Gather-Scatter-Loops).

---

## 5. Strukturelle Pflicht-Erweiterungen (M2.5) & Next-Gen Mesher-Ausbau (M3+)
> **Wissenschaftliche Referenzen:** Siehe **[`resources/papers/literature_survey_2026.md` (Abschnitt 9)](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md)** und unsere lokalen C++-Referenz-Repos in **[`resources/reference_spickzettel/INDEX.md`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/INDEX.md)**.

### 5.1 Strukturelle Pflicht-Bausteine für reale Industrie-Bauteile (Meilenstein M2.5)
1. **3D-Fluid-Volumen-Extraktion (Farfield-Generator & Wetted-Surface-Floodfill):**
   * *Außenströmung (`external_fluid`):* Konfigurierbare Windkanal-/Farfield-Box (bzw. Zylinder/Rotationssektor für Turbomaschinen) mit automatischer Benennung der Außenpatches (`inlet`, `outlet`, `top`, `bottom`, `sides` / `symmetry`) und Vernetzung von $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$ ($\phi_{\text{solid}}(\mathbf{x}) > 0$).
   * *Innenströmung (`internal_fluid`):* Floodfill ab einem Innen-Saatpunkt $\mathbf{x}_{\text{seed}}$ innerhalb geschlossener Gehäuse/Rohre mit Cap-Patches (`inlet`, `outlet`).
2. **Konformes Vernähen („Stitching“) von Grenzschicht (Inflation) und Kernnetz:**
   * Die äußeren Deckflächen („Cap Faces“) der aufgeblasenen Prismen-/Hexaeder-Grenzschicht dürfen nicht lose in der Luft hängen, sondern müssen wasserdicht über Pyramiden (Quad $\leftrightarrow$ Tet) oder allgemeine konvexe Polyeder-Klebezellen (Mosaic Poly-Hexcore) an das äußere Volumengitter angebunden werden.
3. **CAD B-Rep Patch-Vererbung & Feature-Edge-Schutz:**
   * Durchreichen von `SurfaceMesh.patches` (aus STEP/OBJ/STL) bis in `UnstructuredMesh.boundaries`, inklusive Schutz scharfer Geometrie-Kanten (Knickwinkel $> 30^\circ$, z. B. Schaufel-Hinterkanten) als fixierte 1D-Feature-Kanten.
4. **Lokale Verfeinerungszonen („Bodies of Influence“ / Wake-Refinement) & Zero-Thickness Baffles:**
   * Analytische Verfeinerungsprimitive (`BoxRefinement`, `CylinderRefinement`, `ConeWakeRefinement`), die lokal $h(\mathbf{x}) = \min(h(\mathbf{x}), h_{\text{zone}})$ setzen, sowie dünne Leitbleche (interne Doppelflächen-Baffles).
5. **Space-Filling-Curve Partitionierung (Hilbert / Morton) + Halo-Layer:**
   * Direkte Zell-Umnummerierung entlang einer 3D-Hilbert-/Morton-Kurve (schneller als RCM, optimal für CPU-SIMD-Caches und Voraussetzung für das Zero-Copy Load-Balancing `DynamicRedistributor` in [`ARCHITECTURE.md`](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md)).

### 5.2 High-End „Optimization-First“ Additionen (Meilenstein M3+ — Detailformeln in [`literature_survey_2026.md` §9](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L175))
1. **Mosaic-artiges *Poly-Hexcore*, konforme *PUMA*-Subdivision & ungeclipptes *VoroCrust*-Meshing ([`VoroCrust_1902.08767v3.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroCrust_1902.08767v3.pdf), [`NotDownloadable/1 Ansys ICEM CFD Meshing Adaptation/`](file:///G:/Datein/Projects/CFDSolver/resources/papers/NotDownloadable/1%20Ansys%20ICEM%20CFD%20Meshing%20Adaptation)):**
   * Kombination aus achsenparallelem **Octree-Hexaeder-Kern** ($\theta_f = 0^\circ$), **Poly-Prismen-Grenzschicht** ($y^+ \le 1$) und **konvexen Polyeder-/Voronoi-Übergangszellen** (ohne Hanging Nodes: ein 2:1 geteiltes Quad-Face wird auf der ungeteilten Nachbarzelle als 4 koplanare Teil-Faces eines 9-flächigen Polyeders geführt).
   * **PUMA-Adaption mit Entropie-Hesse-Sensor (Adapt-II):** Kombinierter Multiskalen-Hesse-Indikator $\|u - u_h\|_{L^p(\Omega_k)}^p = \int_{\Omega_k} \operatorname{tr}(\mathbf{M}^{-1/2}|\mathbf{H}|\mathbf{M}^{-1/2})^p\,\mathrm{d}\mathbf{x}$ über **Druck $p$, Machzahl $M$ und Entropie $s = c_v \ln(p/\rho^\gamma)$** (25 % kleinere Netze, turbulenzmodell-unabhängig).
   * **VoroCrust Sliver-Elimination:** Gespiegelte Saatpunktpaare $(g^\uparrow, g^\downarrow)$ aus überlappenden Oberflächenkugel-Tripeln $(b_p, b_q, b_w)$ mit $L$-Lipschitz-Schranke ($L=0.25$) und Schrumpfung $r_q^{\text{new}} = \|q - g^\downarrow\|$ bei Halbüberdeckung ($\implies$ exakte Primal-Dual-Orthogonalität $\mathbf{k}_f \equiv \mathbf{0}$).
2. **„Dirty-CAD“-Immunität via Generalized Winding Numbers, $\epsilon$-Envelope & Conformal AMIPS ([`FastTetrahedralMeshing_1908.03581v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FastTetrahedralMeshing_1908.03581v2.pdf), Spickzettel [`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild)):**
   * Ersatz fragiler Einzelnormalen-Vorzeichen durch die schnelle harmonische Raumwinkelformel $W(\mathbf{c}_k) = \frac{1}{4\pi}\sum_f \Omega_f(\mathbf{c}_k)$ (inkl. Auto-Flip falls $n_{\text{tets}} == 0$ und direkten CSG-Booleans `Difference` für $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}}$).
   * Lokale Netzoptimierung (Edge Split $> \frac{4}{3}l_{\text{loc}}$, Edge Collapse $< \frac{4}{5}l_{\text{loc}}$, 3-2/4-4/5-6 Edge Swapping, analytisches Newton-Smoothing) unter Minimierung der skaleninvarianten **Conformal AMIPS Energie** $E_{\text{AMIPS}}(T) = \operatorname{tr}(\mathbf{J}^T\mathbf{J}) / (\det\mathbf{J})^{2/3} \ge 3$ innerhalb der konservativen $\epsilon$-Envelope $\epsilon_{\text{usable}} = \epsilon_{\text{input}} - d_d/\sqrt{3}$.
3. **CFL-stabile Cut-Cells via *Weighted State Redistribution (WSRD)* & Traktions-kompatibler Point-Cloud WSS ([`CutCell_Meshing2308.16332v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CutCell_Meshing2308.16332v2.pdf), [`WallShearStress_pointCloud_Meshing_2609.24069v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/WallShearStress_pointCloud_Meshing_2609.24069v1.pdf)):**
   * Verhindert den Zeitschritt-Kollaps angeschnittener Randzellen ($\alpha_i = V_i/V_{\text{ref}} < 0.5$) geometrisch durch Small-Cell Merging ($\alpha_i < 0.2$) und im Solver durch konservatives, bis $\text{CFL}=1$ monotoniebewahrendes WSRD mit Gewichten $w_{i,j} = \frac{1}{N_i - 1}(1 - \alpha_j/\alpha_{\text{target}})$ (inkl. Pre-Merging $U^0 = \operatorname{SRD}(U^{\text{init}})$).
   * Für Cut-Cells/Punktwolken ohne Prismenschicht: Unsymmetrische Nitsche-Wandbedingung ($\gamma = -1$) gekoppelt mit **Spaldings universellem Wandgesetz** und einer traktionskompatiblen $24\times 24$ Least-Squares-Spannungsrekonstruktion über 3-Ring-Cut-Cell-Patches.
4. **Continuous Mesh Framework, Goal-Oriented Adjoint-Metrik $\mathcal{M}(\mathbf{x})$, Fidkowski Entropie-Adjoint & 3.-Ordnung DWR ([`Meshing_ErrorCorrection_contmesh.part1.final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_ErrorCorrection_contmesh.part1.final.pdf), [`Error_Estimators1-s2.0-S0045793021003601-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Error_Estimators1-s2.0-S0045793021003601-main.pdf), [`ErrorCorrection_Meshing1712.00693v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ErrorCorrection_Meshing1712.00693v1.pdf)):**
   * Kontinuierliche Metrik-Dualität $\mathcal{M} = d^{2/3}\mathcal{R}\operatorname{diag}(r_i^{-2/3})\mathcal{R}^T$ mit Quasi-Unit-Qualität $Q_{\mathcal{M}}(K) \ge 0.8$; wahlweise Multiskalen-$L^q$-Metrik ($q=2,4$), adjungierte Zielgrößen-Metrik $\mathcal{M}_{\text{go}}$ oder **Fidkowskis Entropie-Variablen** $\mathbf{V}$ als lösungsfreier „Gratis-Adjoint“, plus Dual-Weighted-Residual Zielgrößenkorrektur $J_{\text{corr}} = J_H(\mathbf{U}_H) - \mathbf{\Psi}_h^T \mathbf{R}_h(\mathbf{U}_h^H)$.
5. **Inversionssicheres, differenzierbares ALE (`TINE`), `JamTet` Sphere-Packing Morphing, `UM2N` & `VoroMesh` ([`Moving_Mesh_2006.14051v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2006.14051v1.pdf), [`MovingMesh_Morphing_2610.08880v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MovingMesh_Morphing_2610.08880v1.pdf), [`Moving_Mesh_2407.00382v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2407.00382v4.pdf), [`VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf)):**
   * **TINE auf $\Omega_f^0$:** Logarithmische Neo-Hooke-Elastizität ($\mathbf{S}_a = \lambda_a \ln J_a \mathbf{C}_a^{-1} + \mu_a(\mathbf{I} - \mathbf{C}_a^{-1})$) mit Jacobi-Versteifung $E_{\text{eff}}(e) = E_0 (V_e^0)^{-\chi}$ ($\chi = 2.5$) auf dem Referenzgitter $\Omega_f^0$ verhindert *Accumulated Distortion* über beliebig viele FSI-Zyklen.
   * **JamTet & UM2N:** 2-Stufen Kugelpackung (*Water Stage* Gauß-Abstoßung + *Jam Stage* harmonischer Kontakt mit SDF-Offset-Projektion $\phi(\mathbf{p}) \le -s$) für 100 % Knoten-Identität bei großen Formänderungen sowie Monge-Ampère 1-Hop Graph-Attention $r$-Adaption.
   * **VoroMesh Theorem 1 (`VoroLoss`):** Optimiert 3D-Voronoi-Saatpunkte $\mathbf{Q}$ analytisch über den Abstand zur nächsten Mittelsenkrechten-Ebene $H_{i_x, j}$ ($k$-NN mit $k=16$, ohne Voronoi-Diagramm-Konstruktion in der Schleife) für garantierte Wasserdichtigkeit.
6. **Sichtbarkeitskegel-, Multi-Normal- & Medial-Axis-Grenzschichtextrusion ([Loseille & Löhner 2013](https://doi.org/10.1007/978-3-642-33573-0_27), [`Boundarylayer_Collision_1901.00992v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Boundarylayer_Collision_1901.00992v1.pdf)):**
   * **Visibility Cone & Multi-Normals:** Extrusionsvektor $\mathbf{n}_i$ muss $\langle \mathbf{n}_i, \mathbf{n}_{T_j}\rangle \ge \cos\theta_{\text{vis}} > 0$ für alle inzidenten Wanddreiecke erfüllen; an scharfen Kanten ($\theta > \theta_{\text{ridge}} \approx 60^\circ$) Aufspaltung in Multi-Normal-Cluster mit Winkelhalbierenden.
   * **Medial-Axis-Schranke & Isoparametrisches Macro-Splitting:** Schutz gegen Front-Kollisionen über $\delta_{i,k} \le \alpha_{\text{coll}} d_{\text{opp}}$ ($\alpha_{\text{coll}} \approx 0.4$) und Krümmungsschranke $\delta_{i,k} < (1-\varepsilon_\kappa)/\kappa_i$ mit **Layer Step-Down** (keine Quetschung von $h_1$ auf $0$!) sowie optionalem isoparametrischem Splitting einer gültigen Makro-Schale $\chi_k = \chi \circ f_k$ ($\det J_{\chi_k} > 0$ garantiert).
7. **Gekoppelter 2D-Manifold-Submesh-Extraktor (`faMesh`) für Wandfilme & Katalyse:**
   * Automatische Extraktion des gekoppelten 2D-Finite-Area-Oberflächengitters auf gekrümmten Wand-Patches (mit 1:1 Face-Mapping ins 3D-Volumengitter) für das Eulerian-Wall-Film- (EWF) und Washcoat-Katalyse-Modul.

---

## 6. Strategische Architektur-Erweiterungen (Physics-Guided Meshing, RT-Cores, EMF/DEC & Cloud Scaling)

1. **Physics-Guided Pilot-Meshing (0,2 s GPU-LBM / OpenVDB Vorab-Simulation als Initial Guess):**
   * Vor dem eigentlichen Vernetzen läuft ein ultraschneller grober Pilot-Lauf ($128^3$ Kartesisches Voxel-/OpenVDB-Gitter, Semi-Lagrangian oder GPU-LBM in `FP16`/`FP32`), dessen Wirbelstärke $\boldsymbol{\omega} = \nabla \times \mathbf{u}_{\text{pilot}}$ und Scherrate direkt das Sizing-Feld $h(\mathbf{x})$ im Nachlauf (*Wake*) steuern und gleichzeitig als **Warm-Start-Initialisierung** ($\mathbf{u}^{(0)}, p^{(0)}$) auf das finale unstrukturierte Gitter interpoliert werden.
2. **Full Multigrid (FMG) direkt aus der Octree-/PUMA-Mesher-Hierarchie:**
   * Die groben Zwischenstufen ($L_0 \to L_1 \to L_2$) des Octree-/PUMA-Meshers werden nicht verworfen, sondern als echte geometrische Multigrid-Hierarchie im RAM behalten (5x–10x schnellere Konvergenz des Solvers analog Aliaga et al. 2026).
3. **Erweiterte Nutzung der CMS-`MeshErrorMap` über reine Solver-Stabilität hinaus:**
   * **Gitterqualitäts-gesteuerte lokale Vorkonditionierung:** Mehr Glättungs-Sweeps (ILU/Chebyshev) gezielt in Zellen mit $\omega_i < 0.8$ bei minimalem Aufwand (Jacobi) im orthogonalen Hexcore ($\theta_f = 0^\circ$).
   * **Adjoint- & Topologie-Vertrauensgewichtung:** Dämpfung unphysikalischer Sensitivitätsspitzen $\tilde{g}_i = \omega_i \frac{\mathrm{d}J}{\mathrm{d}\gamma_i}$ an stark verzerrten Cut-Cells.
   * **Lokaler CFL-/Zeitschritt-Schutz & DES/LES-Filterweite $\Delta_{\text{eff}}$:** Automatische Zeitschrittbegrenzung und richtungsabhängige Wirbel-Filterweite aus den Metrik-Eigenwerten.
4. **Physik-spezifische Gitter (EMF Skin-Depth & Primal-Dual Voronoi DEC) & Hardware Raytracing (`RT-Cores`):**
   * **Elektromagnetik (EMF / Induktion / Plasma):** Skin-Depth-Grenzschicht im Leiter mit $\delta_s = 1/\sqrt{\pi f \mu_{\text{mag}} \sigma_{\text{el}}}$ sowie orthogonal-duale VoroCrust/Delaunay-Gitter für *Discrete Exterior Calculus (DEC)* ($\nabla \cdot \mathbf{B} = 0$ exakt auf Maschinengenauigkeit).
   * **RT-Core Beschleunigung (OptiX / Vulkan Ray Query / Embree BVH):** Hardware-Raytracing für Wärmestrahlung (`fvDOM` / Monte-Carlo View Factors), Hochfrequenz-EMF (SBR), sofortige Sichtbarkeits-/Spaltweitenmessung $d_{\text{opp}}$ im Mesher sowie Lagrange-Partikel-Wand-Kollisionen (`DPM`).
5. **Multi-Node Cloud Scaling via 2-Stufen Hilbert-SFC & Inner/Halo Compute-Communication Overlap:**
   * Jede MPI-/Cloud-Node erhält ein zusammenhängendes Segment der 3D-Hilbert-Kurve. Innerhalb jeder Node werden Zellen strikt in **Inner Cells** (Index $0 \dots N_{\text{inner}}-1$, kein Netzwerkbedarf) und **Boundary-Adjacent Cells + Halo Cells** sortiert, sodass die Berechnung der Inner Cells zu 100 % die Netzwerklatenz des asynchronen Halo-Austauschs (`MPI_Isend`/`Irecv` bzw. gRPC-Stream) überdeckt.


---

## 7. WP 1–WP 4 Post-Audit Remediation & Empirische Probe-Befunde (Stand: 2026-11-06)

> **Kontext:** Die Kombination aus dem statischen C++20/Python-Code-Audit (WP 1–WP 4) und dem empirischen Testlauf (`probe_mesher_v2.py`) zeigt: WP 1–3 sowie große Teile von WP 4 (`faces`-Speicherung, `to_openfoam_polymesh`, CCW-Polygon-Fix, Einheiten-Invarianz, GCI-Familie mit $r \approx 1.42 \approx \sqrt{2}$, Farfield-Box) funktionieren.  
> Allerdings verhindern aktuell **ein Python-Attributfehler (der den gesamten C++-Hexcore-Mesher still auf eine unrefinierte Python-Schleife zurückfallen lässt)** sowie **zwei mathematische Bugs in [`octree_hexcore.cpp`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp)** und der **noch offene K6-Normalen-Flip in [`inflation.py`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py)** die finale Abnahme von WP 4.

### 7.0 ⚠️ WICHTIG: Ressourcen- & Hardware-Leitplanke (ANSYS läuft im Hintergrund bei ~95 % RAM!)
Auf der Workstation läuft parallel eine große ANSYS-Simulation mit **~95 % RAM-Auslastung**. Für alle Builds, Tests und Validierungsläufe gelten ab sofort zwingend folgende Limits:
1. **Keine Multi-Core-Kompilierung / kein paralleles Pytest:**
   * CMake-Build maximal mit 2 Threads starten: `cmake --build backend/meshing/native/build --config Release --parallel 2`
   * OpenMP in allen Terminal-Sessions drosseln: `$env:OMP_NUM_THREADS="2"`
   * `pytest` **niemals** mit `-n auto` (`pytest-xdist`) aufrufen, sondern strikt sequenziell pro Testdatei ausführen.
2. **Maximale Gittergröße bei lokalen Validierungs-Probes:**
   * Solange der C++-Pfad nicht aktiv ist, keine Gitter mit $> 30\,000$ Zellen in Python erzeugen. Selbst mit aktivem C++-Hexcore bei lokalen Tests $h_0$ so wählen, dass $< 150\,000$ Zellen entstehen (`gc.collect()` nach jedem GCI-Tupel aufrufen).

---

### 7.1 Verbindliche Bugfix-Checkliste für die WP 4 Abnahme (Priorisiert)

#### 🔴 Prio 1 (Sofort, ~15 Min.): 4 `AttributeError`-Abstürze in [`difvm_topology.py:778-784`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L778-L784) & Strict-Mode gegen stille Fallbacks
* **Befund:** `generate_hexcore_native` in [`difvm_topology.py:778-784`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L778-L784) greift auf vier veraltete Attributnamen zu, die in [`CurvatureSizingConfig`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L78-L88) und [`InflationConfig`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L54-L66) anders heißen. Dadurch wirft **jeder** Aufruf von `generate_hexcore_native` sofort einen `AttributeError`, den [`mesh_engine.py:465`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L465) abfängt und still auf den uniformen Python-Fallback (ohne Octree-Verfeinerung, ohne Mosaic-Polyeder, ~400 s für 375k Zellen!) umleitet.
* **Exakter Fix in [`difvm_topology.py:778-785`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L778-L785):**
  ```python
  cfg_c.alpha_curv = float(config.sizing.alpha)
  cfg_c.beta_prox = float(config.sizing.beta)
  cfg_c.num_layers = int(config.inflation.num_layers if config.inflation.enabled else 0)
  cfg_c.first_layer_height = float(config.inflation.first_layer_thickness)
  cfg_c.growth_rate = float(config.inflation.growth_rate)
  cfg_c.enable_pinching = int(1 if config.inflation.pinching else 0)
  cfg_c.pinching_angle_deg = float(config.inflation.pinching_threshold_angle)
  ```
* **Pflicht-Ergänzung (`strict_native`):** In [`MeshingConfig`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py#L90-L115) und [`MeshEngine.__init__`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L45-L60) ein Flag `strict_native: bool = False` (sowie Unterstützung für die Umgebungsvariable `CFD_STRICT_NATIVE=1`) einführen. Ist `strict_native=True` (Standard in allen Unit- und Gate-Tests!), darf [`mesh_engine.py:465`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L465) bei einem Fehler im C++-Mesher **nicht** auf Python zurückfallen, sondern muss die Exception direkt weiterwerfen (`raise`).

#### 🔴 Prio 2 (Kritisch C++): Kollinearitäts-Bug in [`octree_hexcore.cpp:645-648`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L645-L648) deaktiviert 100 % des Cut-Cell-Snappings
* **Befund:** In Step 8 (Constrained AMIPS Cut-Cell Snapping) prüft Zeile 646 die Qualität über:
  ```cpp
  double amips = computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center);
  ```
  Da `onode.center = 0.5 * (onode.bmin + onode.bmax)` exakt der Mittelpunkt der Raumdiagonale zwischen `bmin` und `bmax` ist, sind die drei Punkte `bmin`, `bmax` und `center` **immer kollinear**! Für jeden beliebigen Punkt `test_pos` gilt:
  $$\mathbf{e}_3 = \frac{1}{2}\mathbf{e}_1 + \frac{1}{2}\mathbf{e}_2 \implies \det \mathbf{D}_s = (\mathbf{e}_1 \times \mathbf{e}_2) \cdot \mathbf{e}_3 \equiv 0$$
  Folglich gibt `computeTetAMIPS` **ausnahmslos `1e30` zurück**, `valid` wird immer `false`, `best_lambda` bleibt `0.0` und **im gesamten C++-Mesher wird kein einziger Knoten auf die CAD-Oberfläche gesnappt** (Körper bleibt eine reine Treppenstufen-Voxelfläche!).
* **Exakter Fix in [`octree_hexcore.cpp:635-655`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L635-L655):**
  Für die jeweilige Ecke $c \in \{0,\dots,7\}$ des Oktanten mit Binärkoordinaten $(i_x, i_y, i_z) \in \{0,1\}^3$ das rechtshändige Ecken-Tetraeder aus `test_pos` und den drei adjazenten Kanten-Nachbarn des Oktanten bilden (mit Vorzeichentausch von zwei Basisvektoren falls $(i_x \oplus i_y \oplus i_z) == 1$, damit im unverschobenen Würfel $\det \mathbf{D}_s = +\Delta x \Delta y \Delta z > 0$ gilt). Zusätzlich prüfen, dass für alle an Knoten $v$ angrenzenden Flächen $f$ das Teil-Pyramidenvolumen $(\mathbf{c}_f^{\text{new}} - \mathbf{c}_{\text{cell}}) \cdot (\sigma_{cf} \mathbf{S}_f^{\text{new}}) > 0$ bleibt.

#### 🔴 Prio 3 (Kritisch C++): Nicht-translationsinvariante Zellvolumina & verschobene Zellschwerpunkte in [`octree_hexcore.cpp:667-708`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L667-L708)
* **Befund:**
  1. In Step 9 (`octree_hexcore.cpp:667-680`) wird `mf.centre` als ungewichtetes Knotenmittel berechnet und `mf.is_folded` nie gesetzt.
  2. In Step 10 (`octree_hexcore.cpp:682-708`) wird das Volumen um den globalen Ursprung $(0,0,0)$ summiert (`v_o = dot(mf.centre, mf.normal_owner) / 3.0`) und für den Zellschwerpunkt gerechnet:
     ```cpp
     cell_cents[o] = cell_cents[o] + mf.centre * (v_o * 3.0);
     // ...
     cell_cents[c] = cell_cents[c] * (1.0 / (cell_vols[c] * 3.0));
     ```
     **Mathematischer Fehler:** Erstens ist der Schwerpunkt einer Pyramide mit Spitze $\mathbf{c}_{\text{ref}}$ und Basis-Schwerpunkt $\mathbf{c}_f$ gleich $\frac{3}{4}\mathbf{c}_f + \frac{1}{4}\mathbf{c}_{\text{ref}}$ (es fehlt der Faktor $\frac{3}{4}$!). Zweitens ist die Formel um $(0,0,0)$ **nicht translationsinvariant**: Auf $[0,1]^3$ liefert sie $\mathbf{C} = (\frac{2}{3}, \frac{2}{3}, \frac{2}{3})$ statt $(0.5, 0.5, 0.5)$; auf einem verschobenen Würfel $[10,11]^3$ liefert sie $\mathbf{C} = (7, 7, 7)$ **weit außerhalb der Zelle**! Das zerstört $\mathbf{d}_f = \mathbf{C}_N - \mathbf{C}_P$ und lässt den nachgeschalteten Check in [`difvm_topology.py:798`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L798) (`native_min_vol <= 0`) bei verschobenen Geometrien oder kleinen Cut-Cells fehlschlagen.
* **Exakter Fix in [`octree_hexcore.cpp:667-708`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L667-L708):**
  1. In Step 9 `mf.normal_owner` und `mf.centre` per flächengewichteter Triangle-Fan-Zerlegung berechnen (exakt wie `computePolygonNormalAndCentroid` in [`difvm_mesh.cpp:36-78`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/difvm_mesh.cpp#L36-L78)) und `mf.is_folded = true` setzen, falls ein Teil-Dreieck $\mathbf{n}_{\text{tri}} \cdot \mathbf{S}_f < 0$ aufweist.
  2. In Step 10 zuerst pro Zelle das arithmetische Mittel ihrer Flächenschwerpunkte als lokalen Referenzpunkt $\mathbf{c}_{\text{approx}, c} = \frac{1}{|F_c|}\sum_{f \in F_c} \mathbf{c}_f$ bestimmen und danach lokal und translationsinvariant aufsummieren:
     $$V_{\text{pyr}, f} = \frac{1}{3}(\mathbf{c}_f - \mathbf{c}_{\text{approx}, c}) \cdot (\sigma_{cf} \mathbf{S}_f), \quad \mathbf{c}_{\text{pyr}, f} = \frac{3}{4}\mathbf{c}_f + \frac{1}{4}\mathbf{c}_{\text{approx}, c}, \quad \mathbf{C}_c = \frac{1}{V_c}\sum_{f \in F_c} V_{\text{pyr}, f}\,\mathbf{c}_{\text{pyr}, f}$$

#### 🔴 Prio 4 (Kritisch Python): K6 Grenzschicht-Invertierung an geschlossenen 3D-Körpern ([`inflation.py:74-106`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py#L74-L106))
* **Befund:** In `extrude_boundary_layers` ([`inflation.py:74-106`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py#L74-L106)) wird bei `nominal_normal=None` die erste Flächennormale als globale `nominal_normal` gespeichert und in Zeile 104 jede weitere Wandfläche mit `if np.dot(fn, nominal_normal) < 0.0: fn = -fn` in denselben Halbraum gezwungen. Bei einem geschlossenen 3D-Körper (z. B. Würfel oder Kugel) zeigen dadurch die Normalen auf der abgewandten Körperseite **in den Körper hinein** bzw. entgegen der Knoten-Umlaufrichtung `f_arr`, was reproduzierbar 4 von 24 Prismen/Hexaedern mit **negativem Volumen ($V_i < 0$)** erzeugt.
* **Exakter Fix in [`inflation.py:74-106`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/inflation.py#L74-L106):**
  * Den globalen `nominal_normal`-Flip **nur** ausführen, wenn der Aufrufer explizit einen Vektor `nominal_normal is not None` (für eine einzelne ebene Platte) übergeben hat.
  * Ist `nominal_normal is None` (Standard für allgemeine/geschlossene 3D-Körper), muss jede Wandfläche $f$ lokal nach außen ins Fluid zeigen: Entweder über den SDF-Gradienten $\nabla\phi(\mathbf{c}_f)$ oder relativ zum Schwerpunkt $\mathbf{c}_{\text{body}}$ der Wand-Punktwolke: Falls $\mathbf{n}_f \cdot (\mathbf{c}_f - \mathbf{c}_{\text{body}}) < 0$, wird **sowohl** $\mathbf{n}_f \leftarrow -\mathbf{n}_f$ gedreht **als auch** die Knotenreihenfolge des Wand-Faces `f_arr = f_arr[::-1]` umgekehrt, damit $\det \mathbf{J} > 0$ für jede Schicht garantiert ist!

#### 🟠 Prio 5 (Hoch C++/Python): Small-Cell Merging (`cell_nodes`), `omega_cell` & WSRD-Gewichte
1. **Zellknoten & parallele Faces beim Small-Cell Merging ([`octree_hexcore.cpp:742-861`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L742-L861)):**
   * Wenn die kleine Zelle `c` in `best_nb` verschmolzen wird, müssen ihre Knoten in `cell_nodes` von `best_nb` vereinigt und `cell_types[best_nb] = static_cast<int32_t>(CellType::POLYHEDRON)` (`42`) gesetzt werden.
   * Außerdem müssen nach dem Umbiegen von `mf.owner` / `mf.neighbour` auf `best_nb` doppelt auftretende interne Faces zwischen demselben Zellpaar `(u, v)` verschmolzen (oder ihre gemeinsame Schnittfläche entfernt) werden, damit `checkMesh` keine *duplicate internal faces* meldet.
2. **Zellweiser Unterrelaxations-Schutzfaktor `omega_cell` ([`octree_hexcore.cpp:1020`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L1020)):**
   * In Step 13 (`octree_hexcore.cpp:1058`) aus dem minimalen $\beta_f$ aller an Zelle $c$ angrenzenden Faces `mesh.omega_cell[c] = std::max(0.1, min_beta_c)` belegen (aktuell bleibt `omega_cell` hart auf `1.0`).
3. **WSRD-Gewichte ([`difvm_topology.py:820-831`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L820-L831) & [`mesh_engine.py:750-768`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L750-L768)):**
   * Nach Berger & Giuliani (2024, Gl. 4.2) für alle Cut-Cells mit $\alpha_j = V_j / V_{\text{ref}} < \alpha_{\text{target}}$ die konservativen Redistribution-Gewichte $w_{i,j} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ berechnen und in `mesh.wsrd_weights` ablegen.

#### 🟠 Prio 6 (Hoch Python): OpenFOAM-Randbedingungstypen ([`polymesh_io.py:150-163`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/polymesh_io.py#L150-L163)) & Default-Domain-Mode ([`mesh_engine.py:126-130`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L126-L130))
1. **Farfield-Patches nicht als `wall` exportieren ([`polymesh_io.py:150-163`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/polymesh_io.py#L150-L163)):**
   * Aktuell setzt der `else:`-Zweig in Zeile 163 `top`, `bottom` und `sides` der Fernfeld-Box auf `type wall;`, was im Windkanal künstliche Reibungsgrenzschichten an den Außenwänden erzeugt.
   * **Fix:** `sides` und `top` (sowie `bottom`, sofern nicht explizit als Bodenwand `ground_wall` konfiguriert) standardmäßig als `symmetry` oder `patch` exportieren. Zusätzlich ein optionales Mapping `patch_types: Dict[str, str]` in `MeshingConfig` und `mesh.metadata["patch_types"]` unterstützen.
2. **Legacy-Sperre in [`mesh_engine.py:126-130`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L126-L130) entschärfen:**
   * Aktuell erzwingt `config is None or (active_cfg.domain_mode == "solid_only") or (active_cfg.farfield_padding is None and not active_cfg.inflation.enabled)` selbst dann eine reine Festkörper-Vernetzung (`solid_only`), wenn der Nutzer `MeshingConfig(domain_mode="external_fluid")` (mit Standard-Padding `None`) übergibt!
   * **Fix:** Sobald `config is not None` und `active_cfg.domain_mode != "solid_only"` ist, **immer** `self.mesh_hexcore(...)` mit dem automatischen 5L/10L/5L-Farfield-Padding aufrufen.

#### 🟡 Prio 7 (Mittel WP 3): Restbefunde in DistMesh, GCI & SDF-BBox
* **[`distmesh_core.py:734`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/distmesh_core.py#L734):** `p = p[d < -0.05 * h_vals]` statt `p = p[d < 0.05 * h_vals]` (Vorzeichenfehler wie in Zeile 281 beheben).
* **[`distmesh.cpp:734-739`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/distmesh.cpp#L734-L739):** Aus dem Polygon gedrückte Knoten per Orthogonalprojektion auf das nächste Polygon-Kantensegment zurückprojizieren, statt den Schritt nur einzufrieren.
* **[`gci.py:113-114`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/gci.py#L113-L114):** In `compute_grid_representative_size` den Exponenten `1.0 / max(1, mesh.topological_dim)` statt `1.0 / mesh.geometric_dim` verwenden, damit 2D-Netze mit `points.shape[1] == 3` korrekt mit $(V/N)^{1/2}$ skaliert werden.
* **[`sdf_engine.cpp:479-484`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/sdf_engine.cpp#L479-L484):** In `outside_bbox` eine Toleranz $\varepsilon = 10^{-5} \|\mathbf{b}_{\max} - \mathbf{b}_{\min}\|$ addieren, damit Punkte direkt auf achsenparallelen CAD-Wänden nicht pauschal als positiv überschrieben werden.

---

### 7.2 Objektive, automatisierte Abnahmekette (Ersatz für das 3-Agenten-KI-Debattier-Gate)

Statt für jedes Gate drei KI-Auditoren über tausende Codezeilen diskutieren zu lassen (hoher Tokenverbrauch, übersieht Laufzeit-Fallbacks), wird die Abnahme von **WP 4 (Gate M4.5)** durch eine **deterministische, reproduzierbare 4-Stufen-Validierungskette** (Skript `scripts/validate_mesher_gate.py`) definiert:

1. **Stufe 1 — Native-Strict Generation & Invarianten (`strict_native=True`):**
   * Erzeugung des Hexcore-Gitters um einen 200-mm-Benchmark-Würfel (bzw. Zylinder/Kugel) mit `strict_native=True` (kein Python-Fallback erlaubt!).
   * Prüfung: `min(cell_volumes) > 0`, `min_cell_volume < 0.25 * h0^3` (beweist aktive Octree-Verfeinerung am Körper!), `mesh.validate() == True`.
2. **Stufe 2 — OpenFOAM `checkMesh` in WSL:**
   * Export via `mesh.to_openfoam_polymesh(case_dir / "constant" / "polyMesh")` und Aufruf von `wsl bash -lc "cd ... && checkMesh"` (mit `OMP_NUM_THREADS=2`).
   * **Abnahmekriterium:** `Mesh OK.` bzw. `0 failed mesh checks` (keine negativen Volumina, keine offenen Zellen, keine nicht-konformen internen Flächen).
3. **Stufe 3 — Kurzer `simpleFoam`-Smoke-Test (50–100 Iterationen in WSL):**
   * Stationärer inkompressibler Lauf ($Re \approx 10^4$) auf dem exportierten Gitter für 50–100 Iterationen.
   * **Abnahmekriterium:** Residuen ($U_x, U_y, U_z, p$) fallen monoton ohne Divergenz (`FOAM FATAL ERROR` / `floating point exception`), und der berechnete Widerstandsbeiwert $c_D$ des Würfels liegt im physikalisch plausiblen Korridor ($c_D \in [0.8, 1.3]$).
4. **Stufe 4 — Kreuzvergleich gegen `snappyHexMesh` (Dual-Mesher-Strategie):**
   * Da der Export nach `snappyHexMesh` bereits vorbereitet ist, wird dieselbe Geometrie bei gleicher Ziel-Zellgröße einmal durch unseren nativen Hexcore-Mesher und einmal durch `snappyHexMesh` geschickt.
   * Das liefert erstens sofort einen produktionsreifen Backup-Mesher für komplexe CAD-Fälle und zweitens einen direkten, quantitativen Benchmark (Laufzeit, `checkMesh`-Metriken, $c_D$-Abweichung), bis unser nativer C++-Hexcore-Mesher in allen Metriken gleichzieht.

---

## 8. High-Leverage Praxis-Tipps für das Zusammenspiel Mesher $\leftrightarrow$ Solver

Die folgenden drei Architektur-Kniffe verbinden die Datenstrukturen des neuen WP-4-Meshers (`octree_hexcore.cpp` + `MeshErrorMap`) direkt mit maximaler Performance und Stabilität im C++20/Fortran/CUDA-Solver:

### 8.1 Tipp 1: Two-Class Branchless Face-Partitioning (`Ortho-Fast` vs. `Skew-Corrected`) + Precomputed CSR Matrix-Indizes
Da ein Octree-Hexcore-Netz im Inneren aus exakt achsenparallelen Würfeln besteht ($\theta_f = 0^\circ$, $\mathbf{k}_f = \mathbf{0}$, $\mathbf{m}_f^{\text{skew}} = \mathbf{0}$), sind **85–92 % aller internen Faces perfekt orthogonal**, während nur **8–15 %** der Faces (an Cut-Cells, Prismen-Übergängen und 2:1-Polyedern) eine Nicht-Orthogonalitäts- und Skewness-Korrektur benötigen.

* **Zweigfreie 2-Klassen-Sortierung im Mesher:**
  Beim Aufbau des Gitters sortieren wir die internen Faces nicht nur nach `(owner, neighbour)`, sondern partitionieren ihre Indizes in zwei disjunkte, kontingente Listen:
  1. **`ortho_faces` ($85\text{–}92\,\%$):** $\theta_f < 1^\circ$ und $\|\mathbf{m}_f^{\text{skew}}\| < 10^{-3} \|\mathbf{d}_f\|$.
     * Braucht im Face-Loop **weder** $\mathbf{k}_f^{\text{comp}}$ **noch** $\mathbf{m}_f^{\text{skew}}$ **noch** die $3\times 3$-Gradienten $(\nabla\phi)_P, (\nabla\phi)_N$ aus dem RAM zu laden!
     * **Halbiert den Speicherbandbreiten-Bedarf pro Face** von ~144 Bytes auf ~64 Bytes.
  2. **`skew_faces` ($8\text{–}15\,\%$):** Alle übrigen internen Faces (Cut-Cells, Mosaic-Übergänge).
     * Läuft in einem eigenen, kompakten zweiten Kernel ohne jede `if (theta > tol)`-Verzweigung (100 % SIMD-/SIMT-Effizienz ohne Warp-Divergenz).
* **Vorab-Mapping der CSR-Matrix-Indizes (`csr_LP_idx`, `csr_UP_idx`, `csr_UN_idx`, `csr_LN_idx`):**
  * Statt während der Matrix-Assemblierung im Solver pro Face eine binäre Suche im CSR-Spaltenvektor `col_idx` durchzuführen, speichert der Mesher beim einmaligen Aufbau des CSR-Graphen für jedes interne Face $f$ direkt die 4 Ziel-Offsets im `values`-Array (`diag_idx[P]`, `diag_idx[N]`, `upper_idx[f]`, `lower_idx[f]`).
* **⚠️ Mathematisch exakte Formeln für die beiden Face-Kernels (WICHTIG gegen Vorzeichen- & Einheiten-Bugs!):**
  1. **Exaktes überrelaxiertes Diffusionsgewicht $\alpha_f^{\text{ortho}}$ (Einheit: $\text{m}$):**
     Aus $\boldsymbol{\delta}_f = \frac{\mathbf{S}_f \cdot \mathbf{S}_f}{\mathbf{d}_f \cdot \mathbf{S}_f}\,\mathbf{d}_f$ folgt für den impliziten orthogonalen Diffusionsleitwert $D_f = \Gamma_f \frac{\boldsymbol{\delta}_f \cdot \mathbf{S}_f}{\|\mathbf{d}_f\|^2} = \Gamma_f \alpha_f^{\text{ortho}}$:
     $$\alpha_f^{\text{ortho}} = \frac{\|\boldsymbol{\delta}_f\|}{\|\mathbf{d}_f\|} = \frac{\mathbf{S}_f \cdot \mathbf{S}_f}{\mathbf{d}_f \cdot \mathbf{S}_f} \quad [\text{m}]$$
     *(Achtung: $\mathbf{S}_f \cdot \mathbf{S}_f$ steht im **Zähler**, $\mathbf{d}_f \cdot \mathbf{S}_f$ im **Nenner** — niemals umdrehen!)*
  2. **Strikte Trennung zwischen geometrischem Interpolationsgewicht $w_f$ und CMS-Upwind-Faktor $\beta_f^{\text{CMS}}$:**
     * Geometrisches Linear-Interpolationsgewicht: $w_f = \frac{\|\mathbf{x}_N - \mathbf{x}_{\text{int}}\|}{\|\mathbf{d}_f\|} \in (0, 1)$, sodass $\phi_{\text{int}} = w_f \phi_P + (1 - w_f)\phi_N$.
     * CMS-Qualitätsfaktor aus `MeshErrorMap`: $\beta_f^{\text{CMS}} \in [0, 1]$ ($1 = \text{Zentraldifferenz/LUST}$, $0 = \text{reines Upwind an stark verzerrten Zellen}$).
  3. **Unbedingt M-Matrix-stabile Konvektions-Assemblierung (1. Ordnung Upwind implizit + Deferred Correction auf dem RHS):**
     Damit die Systemmatrix $\mathbf{A}$ auch bei hohen lokalen Zell-Péclet-Zahlen ($Pe_f = \frac{|\dot{m}_f|}{\Gamma_f \alpha_f^{\text{ortho}}} > 2$) garantiert diagonaldominant (M-Matrix) bleibt, wird in die Matrix $\mathbf{A}$ **immer** 1st-Order Upwind eingetragen und der Anteil höherer Ordnung ($\beta_f^{\text{CMS}}$ + Skewness-Korrektur) als **Deferred Correction** auf die rechte Seite $b$ gebucht:
     $$F_{f,\text{conv}}^{\text{impl}} = \max(\dot{m}_f, 0)\,\phi_P + \min(\dot{m}_f, 0)\,\phi_N$$
     $$F_{f,\text{conv}}^{\text{corr}} = \dot{m}_f \cdot \beta_f^{\text{CMS}} \Big( \underbrace{w_f \phi_P + (1-w_f)\phi_N - \phi_{\text{upwind}}}_{\text{High-Order Blending}} + (\nabla\phi)_f \cdot \mathbf{m}_f^{\text{skew}} \Big)$$
     Damit sind für jedes interne Face $f$ (von $P \to N$) die Matrixbeiträge garantiert vorzeichenrichtig:
     * $A_{PP} \mathrel{+}= D_f + \max(\dot{m}_f, 0) > 0$
     * $A_{PN} \mathrel{+}= -D_f + \min(\dot{m}_f, 0) \le 0$
     * $A_{NN} \mathrel{+}= D_f - \min(\dot{m}_f, 0) > 0$
     * $A_{NP} \mathrel{+}= -D_f - \max(\dot{m}_f, 0) \le 0$
     * Rechte Seite: $b_P \mathrel{+}= \Gamma_f (\nabla\phi)_f \cdot \mathbf{k}_f^{\text{comp}} - F_{f,\text{conv}}^{\text{corr}}$, $\quad b_N \mathrel{-}= \Gamma_f (\nabla\phi)_f \cdot \mathbf{k}_f^{\text{comp}} - F_{f,\text{conv}}^{\text{corr}}$.

### 8.2 Tipp 2: „Gratis“ Full-Multigrid (FMG) & Nachlauf-Adaption direkt auf Octree-Level 0/1
* Da unser `octree_hexcore.cpp`-Mesher ohnehin zuerst das grobe Hintergrundgitter (Level 0/1, z. B. $25 \times 12 \times 12 \approx 3\,600$ Zellen) aufbaut, braucht es für das **Physics-Guided Pilot-Meshing** keinen separaten LBM-Löser:
  1. Unser eigener FVM-Solver rechnet auf dem groben Level-1-Hexcore-Netz in **$< 0.1\,\text{s}$** ~50 schnelle Pilot-Iterationen.
  2. Die daraus gewonnene Wirbelstärke $\|\nabla \times \mathbf{u}_{\text{pilot}}\|$ und der Totaldruckverlust markieren automatisch das Nachlaufgebiet (*Wake*) hinter dem Körper, ohne dass der Nutzer manuell eine Wake-Refinement-Box definieren muss!
  3. Nach der finalen Octree-Verfeinerung wird $(\mathbf{u}_{\text{pilot}}, p_{\text{pilot}})$ per direkter Eltern-Kind-Injektion (0 Suchkosten, da der Octree-Baum im RAM liegt!) als Startfeld auf das feine Netz übertragen (spart ~60 % der äußeren SIMPLE/PISO-Iterationen).

### 8.3 Tipp 3: Gezielte Nachglättung & Adjoint-Schutz über `MeshErrorMap` (`stab_mask` & $\omega_i$)
* **Lokales Qualitäts-Budget im Mesher:** Statt teures AMIPS-Smoothing über alle 500 000 Knoten laufen zu lassen, iteriert der Post-Smoothing-Pass **ausschließlich** über das 1-Ring-Knoten-Stencil derjenigen Faces, bei denen `stab_mask >= 2` ($\theta_f > 70^\circ$) gilt (typischerweise $< 0.5\,\%$ aller Knoten $\implies$ $> 100\times$ schneller).
* **Automatischer lokaler Unterrelaxations- & TopOpt-Schutz:**
  * Im impliziten Löser erhält jede Zelle $i$ den effektiven Unterrelaxationsfaktor $\alpha_{\text{eff}, i} = \alpha_{\text{global}} \cdot \omega_i$. Dadurch kann das globale $\alpha_U$ auf $0.8\text{–}0.9$ angehoben werden, während einzelne kritische Cut-Cells ($\omega_i \approx 0.2$) automatisch stärker gedämpft werden.
  * In der SIMP/Brinkman-Topologieoptimierung wird der adjungierte Sensitivitätsgradient vor dem Helmholtz-Filter mit $\omega_i$ gewichtet ($\tilde{s}_i = \omega_i \frac{\partial J}{\partial \gamma_i}$), sodass verzerrte Cut-Cells keine künstlichen Material-Artefakte auslösen.
