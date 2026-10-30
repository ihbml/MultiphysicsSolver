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

### 5.2 High-End „Optimization-First“ Additionen (Meilenstein M3+)
1. **Mosaic-artiges *Poly-Hexcore* & ungeclipptes *VoroCrust*-Meshing ([Abdelkader et al. 2020/2023](https://arxiv.org/abs/1902.08767), [Aliaga et al. 2026](https://doi.org/10.2514/1.c038661)):**
   * Kombination aus achsenparallelem **Octree-Hexaeder-Kern** ($\theta_f = 0^\circ$, null Skewness), **Poly-Prismen-Grenzschicht** ($y^+ \approx 1$) und **einer Schicht konvexer Polyeder / Voronoi-Zellen** als nahtlosem Kleber. Da unser DiFVM-Graph-Solver beliebige Polyeder nativ verarbeitet, sinkt die Zellanzahl bei höherer Genauigkeit um 30–50 %.
2. **„Dirty-CAD“-Immunität via Generalized Winding Numbers & Envelope-Meshing ([Hu et al. `fTetWild` 2020](https://arxiv.org/abs/1908.03581), [Jacobson et al. 2013](https://doi.org/10.1145/2461912.2461916)):**
   * Ersatz fragiler Einzelnormalen-Vorzeichen durch das harmonische Raumwinkelfeld $w(\mathbf{x}) \in [0, 1]$ (Spickzettel: [`libigl/include/igl/fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp) & [`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild)), womit selbst nicht-wasserdichte STL/STEP-Daten mit Spalten und Selbstschnitten inversionsfrei vernetzt werden.
3. **CFL-stabile Cut-Cells via Small-Cell Merging & *Weighted State Redistribution (WSRD)* ([Berger & Giuliani 2024](https://arxiv.org/abs/2308.16332)):**
   * Verhindert den Zeitschritt-Kollaps ($\Delta t \to 0$) angeschnittener Kartesischer Randzellen entweder geometrisch durch Verschmelzen kleiner Cut-Cells ($V_{\text{cut}} < 0.2\,V_{\text{bg}}$) mit ihrer Nachbarzelle zu einem konvexen Polyeder oder im Solver durch konservative WSRD-Umverteilung.
4. **Goal-Oriented Adjoint- & Riemannsche Metrik-Adaption $\mathcal{M}(\mathbf{x})$ ([Balan et al. 2022](https://doi.org/10.1016/j.compfluid.2021.105259), Spickzettel [`mmg`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg)):**
   * Kopplung mit unserem Adjoint-Solver: Aus dem lokalen Zielgrößenfehler $\eta_i = |\boldsymbol{\psi}_i^T \mathbf{R}_i(\mathbf{u})|$ wird ein anisotroper Metriktensor $\mathcal{M}(\mathbf{x})$ berechnet, der das Netz gezielt entlang von Stoßwellen, Scherschichten und Nachläufen streckt.
5. **Inversionssicheres, differenzierbares ALE via Polykonvexer Neo-Hooke-Hyperelastizität ([Shamanskiy & Simeon 2021](https://arxiv.org/abs/2006.14051), [`JamTet` 2026](https://arxiv.org/abs/2610.08880), [`VoroMesh` 2023](https://arxiv.org/abs/2308.14616)):**
   * Ablösung des einfachen Laplace-Smoothings in `moving_mesh.py` durch ein Jacobi-versteiftes / polykonvexes Neo-Hooke-Gleichgewicht ($W_{\text{NH}} \to +\infty$ für $\det\mathbf{F} \to 0^+$), das Grenzschicht-Inversionen bei großen Formänderungen mathematisch ausschließt und exakte Sensitivitäten $\partial \mathbf{x}_{\text{vol}} / \partial \mathbf{x}_{\text{surf}}$ für Shape Optimization liefert.
6. **Medial-Axis & Multi-Normal Grenzschicht-Extrusion ([Loseille & Löhner 2013](https://doi.org/10.1007/978-3-642-33573-0_29), [Marcon et al. 2019](https://arxiv.org/abs/1901.00992)):**
   * Vorab-Berechnung des lokalen Medial-Axis-Radius aus dem Eikonal-/SDF-Feld als strikte Kollisionsschranke für gegenüberliegende Wände, kombiniert mit Multi-Normal-Fächern an scharfen Hinterkanten und sauberem Layer-Step-Down (Pyramiden/Wedges), ohne die erste $y^+$-Schicht zu stauchen.
7. **Gekoppelter 2D-Manifold-Submesh-Extraktor (`faMesh`) für Wandfilme & Katalyse:**
   * Automatische Extraktion des gekoppelten 2D-Finite-Area-Oberflächengitters auf gekrümmten Wand-Patches (mit 1:1 Face-Mapping ins 3D-Volumengitter) für das Eulerian-Wall-Film- (EWF) und Washcoat-Katalyse-Modul.
