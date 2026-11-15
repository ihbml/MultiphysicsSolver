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


---

## 9. Ergänzungen aus dem externen Repo-weiten Red-Team-Audit (Commit `69b33c1`, Stand: 2026-11-07)

> **Kontext:** Ein unabhängiges externes Code-Review des gesamten Repositories (unter Linux/GCC kompiliert und gegen reale Gegenbeispiele nachgerechnet) hat nicht nur unsere Mesher-Befunde aus Abschnitt 7 unabhängig bestätigt, sondern darüber hinaus schwerwiegende **Test-Tautologien („Reward Hacking“)**, **CAD-Import-Verluste**, **Geometrie-Abkürzungen** und **Physik-/Build-Fehler** in anderen Modulen aufgedeckt. Alle folgenden Punkte sind verbindlich abzuarbeiten.

### 9.1 🔴 Anti-Reward-Hacking & Test-Integrität (KRITISCH — K6 & M9)

1. **Verbot lokaler Test-Nachbauten in [`tests/test_wp4_multiphysics_hexcore.py:96-185`](file:///G:/Datein/Projects/CFDSolver/tests/test_wp4_multiphysics_hexcore.py#L96-L185) (Audit-K6):**
   * **Befund:** `test_wp4_multiphysics_hexcore.py` definiert ab Zeile 96 eigene lokale Hilfsfunktionen (`compute_amips_energy`, `compute_wsrd_weights`, `compute_asme_vv20_gci`, `compute_farfield_bounding_box`) und prüft in den zentralen WP4-Tests lediglich diese lokalen Test-Kopien, handgeschriebene Vektoren oder triviale Identitäten (`0.08 + 0.92 == 1.0`, `CellType.POLYHEDRON == 42`, synthetische Werte $\phi = \phi_{\text{exakt}} + c h^p$), **ohne den produktiven C++-/Backend-Code aufzurufen**!
   * **Verbindliche Regel für alle Agenten-Instanzen:**
     * Alle lokalen Physik-/Geometrie-Nachbauten in `tests/` **restlos löschen** und ausschließlich die echten Produktionsfunktionen aus `backend/` (mit `strict_native=True`) importieren!
     * Jeder Feature-Test muss ein **echtes End-to-End-Netz** auf einer analytisch bekannten Geometrie **außerhalb des Koordinatenursprungs** (z. B. Kugel bei $(3, 2, 1)$, L-Profil, geneigtes Trapez, Zylinder) erzeugen und harte geometrische Wahrheiten prüfen:
       * $|\sum_i V_i - V_{\text{analytisch}}| / V_{\text{analytisch}} < \text{Tol}$,
       * $\max_{v \in \Gamma_{\text{wall}}} |\phi(\mathbf{x}_v)| < \text{Tol}$ (beweist echtes Oberfächen-Snapping!),
       * $\mathbf{C}_i \in [\mathbf{x}_{\min}^{(i)}, \mathbf{x}_{\max}^{(i)}]$ für **jede** Zelle $i$ (Zellschwerpunkt liegt innerhalb der Zell-Bounding-Box!).
2. **Streckungs-Check in [`UnstructuredMesh.validate()` (`models.py`)](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py) ergänzen:**
   * Eine zentrische Streckung aller Zellschwerpunkte um $(0,0,0)$ (wie der Faktor $4/3$ in `octree_hexcore.cpp:704`) verletzt weder den Gauß-Schluss noch $\boldsymbol{\delta}_f \parallel \mathbf{d}_f$ und schlüpft deshalb durch `validate()`!
   * **Fix:** In `validate()` zwingend prüfen, dass jeder Zellschwerpunkt `cell_centres[c]` innerhalb der AABB seiner Zell-/Flächenknoten liegt (für konvexe Zellen Pflicht) und dass der volumengewichtete Gesamtschwerpunkt $\frac{1}{\sum V_i}\sum_i V_i \mathbf{C}_i$ konsistent zur Rand-Oberflächenintegration $\frac{1}{4 V_{\text{ges}}}\sum_{f \in \partial\Omega} (\mathbf{c}_f \cdot \mathbf{S}_f)\mathbf{c}_f$ ist.
3. **Vollständige Test-Sammlung & Reproduzierbarkeit ([`pytest.ini`](file:///G:/Datein/Projects/CFDSolver/pytest.ini), Audit-M9):**
   * `pytest.ini` sammelt aktuell nur `backend/geometry/tests` (37 Tests) ein, sodass `pytest` ohne Argumente über 570 Tests in `tests/` ignoriert $\to$ `testpaths = tests backend/geometry/tests` setzen.
   * Tests, die nicht versionierte lokale Dateien (`TestCAD.obj`, `BladeRev1CFD.obj`, `OpenFOAM_base/`, Siemens NX Lizenz) benötigen, entweder mit kleinen eingecheckten Ersatz-Fixtures versorgen oder sauber mit `@pytest.mark.skipif(not fixture.exists(), reason=...)` dekorieren, statt mit `ERROR`/`FAILED` abzustürzen.

---

### 9.2 🔴 CAD-Import, Geometrie-Abkürzungen & Linux-Build (Audit-K3, K9, H1, H10)

1. **STEP-Import plättet gekrümmte Flächen im `auto`-Modus weg ([`step_parser.py:84-95`](file:///G:/Datein/Projects/CFDSolver/backend/geometry/parsers/step_parser.py#L84-L95), Audit-K9):**
   * **Befund:** `engine="auto"` ruft zuerst den simplen Python-Parser `_parse_with_brep` auf, der jede `ADVANCED_FACE` nur über ihre Eckpunkte als ebenes Fächer-Polygon trianguliert, und fällt nur bei einer Python-Exception auf Gmsh/OpenCASCADE zurück. Bei `BladeFullRev1CFD.stp` wirft er keine Exception, sondern bügelt alle Freiformflächen platt (**599 statt 464 820 Dreiecke — 23 % Oberfläche fehlen lautlos!**).
   * **Fix:** In `engine="auto"` **Gmsh/OpenCASCADE (`_parse_with_gmsh`) zum primären Standardpfad machen**! `_parse_with_brep` darf nur noch als Fallback dienen, wenn `HAS_GMSH == False` **und** alle Flächen nachweislich ebene, konvexe `PLANE`-Flächen ohne Innenlöcher (`FACE_BOUND`) sind (sonst `RuntimeError` mit klarem Hinweis auf fehlendes `gmsh`).
2. **„Polycube“-Falle vernetzt L-/U-Profile als vollen Quader ([`mesh_engine.py:139, 231-284`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L231-L284), Audit-K3):**
   * **Befund:** `_detect_oriented_box_axes` klassifiziert jedes Bauteil als Quader, sobald alle Dreiecksnormalen in 3 orthogonale Achsen fallen. Damit werden L-Profile, U-Profile, T-Stücke und Quader mit Rechteckbohrung fälschlich als massive volle Bounding-Box vernetzt (Volumen $4{,}0$ statt $3{,}0$!).
   * **Fix:** Den `_mesh_oriented_box`-Sonderpfad für allgemeine CAD-Dateien entweder komplett entfernen oder zwingend absichern über $|V_{\text{surface}} - V_{\text{OBB}}| / V_{\text{OBB}} < 10^{-4}$ **und** exakt 6 zusammenhängende koplanare Außenflächen.
3. **Fehlender OpenMP-Linker-Eintrag unter Linux/WSL ([`CMakeLists.txt:21-36`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/CMakeLists.txt#L21-L36), Audit-H1):**
   * **Befund:** Unter GCC/Clang wird `-fopenmp` nur beim Kompilieren übergeben, aber nicht gelinkt (`undefined symbol: GOMP_loop_nonmonotonic_dynamic_next`). `get_native_dll()` schluckt den Ladefehler und fällt still auf Python zurück.
   * **Fix in [`backend/meshing/native/CMakeLists.txt`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/CMakeLists.txt):**
     ```cmake
     find_package(OpenMP REQUIRED)
     target_link_libraries(cfd_mesher_native PRIVATE OpenMP::OpenMP_CXX)
     ```
4. **Harte Octree-Klemmen & ignorierte Config-Parameter in C++ ([`octree_hexcore.cpp:248, 272, 323, 704-708`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp#L248), [`cfd_mesher_native.cpp:122-129`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/cfd_mesher_native.cpp#L122-L129), Audit-H10 & M6):**
   * Verfeinerungstiefe im Octree nicht hart auf `max_level <= 4` klemmen, sondern konfigurierbar über `h_min` / `h_max` sowie Krümmungs- und Spaltweiten-Sizing steuern; 2:1-Balancierung bis zur echten Konvergenz iterieren (Fehler werfen falls nicht konvergiert).
   * Alle Python-Config-Felder (`small_cell_ratio`, `wsrd_target_alpha`, `snap_*`) vollständig über das C-Struct `NativeMeshingConfig` bis in `octree_hexcore.cpp` durchreichen.
   * Die zu großzügige Fluid-Zell-Aktivierung $\phi \ge -0.15 \cdot \text{diag}$ (wodurch 5,6 % Festkörpervolumen fälschlich zu Fluid wird) auf die tatsächliche lokale Halbzell-Diagonale $\frac{\sqrt{3}}{2} h_{\text{cell}}$ begrenzen und durch echtes Cut-Cell-Clipping/Snapping korrigieren.
   * Zellen mit $V_i \le 10^{-15}\,\text{m}^3$ (inkl. invertierter Zellen!) **niemals** in `octree_hexcore.cpp:704-708` still durch das volle Hintergrundvolumen ersetzen, sondern als Fehler melden!
   * In [`mesh_engine.py:312, 926-970`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/mesh_engine.py#L926-L970) auf Testfälle getunte magische Zahlen (`threshold = 0.11 * h_min`, `aspect > 2.0 -> h0 / 3`) und das stille Herunterskalieren ab 500 000 Zellen entfernen (bei Überschreitung eines expliziten `max_cells`-Limits saubere Exception werfen, damit GCI-Studien nicht still zwei identische Netze vergleichen!).

---

### 9.3 🟠 Weitere Mathematische & Topologische Korrekturen (Audit-H2, H3, H4, H7, H8, M1–M8, M12, L1, L3)

1. **2D-Zellschwerpunkte nutzen fälschlich 3D-Pyramidengewichte ([`difvm_topology.py:1244`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L1244), [`difvm_mesh.cpp:454`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/difvm_mesh.cpp#L454), Audit-H3):**
   * **Befund:** In 2D (`div_factor = 2.0`) ist das Teil-Element ein Dreieck mit Schwerpunkt $\frac{2}{3}\mathbf{c}_f + \frac{1}{3}\mathbf{c}_{\text{approx}}$, der Code rechnet aber mit `0.75 * cf + 0.25 * c_approx` (3D-Pyramide). Auf dem Viereck $(0,0)\text{–}(4,0)\text{–}(1,1)\text{–}(0,1)$ entsteht $(1.41875, 0.3875)$ statt exakt $(1.4, 0.4)$.
   * **Fix:** Gewicht direkt an die Dimension koppeln: `w_f = 2.0 / 3.0 if is_cell_2d else 0.75; pyr_c = w_f * cf + (1.0 - w_f) * c_approx` (analog in `difvm_mesh.cpp`).
2. **Polyeder-Zerstörung beim Topologie-Neuaufbau aus `cells` ([`difvm_topology.py:288-419`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L288-L419), [`moving_mesh.py:65`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/moving_mesh.py#L65), Audit-H4):**
   * **Befund:** Polyeder (z. B. 13/17/20-Knoten Mosaic-Zellen aus dem Octree) sind durch eine bloße Knotenliste `cells[c]` topologisch nicht eindeutig bestimmt. Ruft z. B. `ALEMeshMover.deform_mesh` erneut `compute_difvm_topology(new_points, mesh.cells)` auf, halbiert sich das Netzvolumen ($63{,}5 \to 31{,}2$)! Ebenso wird ein ebenes 2D-Achteck (`len == 8`) als 3D-`HEXAHEDRON` mit Volumen $0{,}0$ fehlklassifiziert.
   * **Fix:** Sobald `mesh.faces`, `owner` und `neighbour` existieren (OpenFOAM-Darstellung), müssen Netzbewegungen (`moving_mesh.py`) und Reorderings (`rcm_reorder`, `morton_sfc_reorder`) die bestehenden `faces` beibehalten und lediglich Flächenvektoren, Schwerpunkte und Volumina über `faces` + `owner`/`neighbour` neu auswerten! `cell_types` immer explizit vom Generator übergeben.
3. **Vollständige GCI-Implementierung nach Celik et al. (2008) ([`gci.py:143-232`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/gci.py#L143-L232), Audit-H2):**
   * Konvergenzverhältnis $R = \varepsilon_{21} / \varepsilon_{32}$ klassifizieren: $R \ge 1$ oder $|\varepsilon_{21}| \ge |\varepsilon_{32}| \implies$ **`divergent`** (GCI ungültig / Flag setzen!), $R < 0 \implies$ `oscillatory`, $0 < R < 1 \implies$ `monotonic`.
   * Tatsächliche Verfeinerungsfaktoren $r_{21} = h_2/h_1$ und $r_{32} = h_3/h_2$ aus den realen Zellzahlen $h_k = (V_{\text{ges}} / N_k)^{1/d_{\text{topo}}}$ berechnen und für $r_{21} \neq r_{32}$ die transzendente Celik-Gleichung $p = \frac{1}{\ln r_{21}}\big|\ln|\varepsilon_{32}/\varepsilon_{21}| + q(p)\big|$ mit $q(p) = \ln\!\left(\frac{r_{21}^p - s}{r_{32}^p - s}\right)$, $s = \operatorname{sign}(\varepsilon_{32}/\varepsilon_{21})$ iterativ lösen. Künstliche Obergrenze $r \le 1{,}5$ entfernen.
4. **Flächenkonservative AMI-Gewichte & echte SPH-FVM-Konsistenz ([`difvm_topology.py:1655-1682`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L1655-L1682), [`coupling.py:362-541`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/coupling.py#L362-L541), Audit-H7 & H8):**
   * **AMI (`difvm_topology.py`):** Statt nicht-konservativer Inverse-Distance-Gewichte in festem Radius `0.05` echte 2D-Polygon-Schnittflächen-Projektion (Sutherland-Hodgman Clipping im lokalen Tangentialsystem, $w_{ij} = A_{i \cap j} / A_i$, Partition of Unity $\sum_j w_{ij} = 1$) implementieren.
   * **SPH-FVM (`coupling.py`):** Die Tautologie `A_iS[is_boundary] = -sum_grad_W[is_boundary]` entfernen; $\mathbf{A}_{iS} = \sum_{f \in \Gamma_S} \mathbf{S}_f \bar{W}_{if}$ aus den tatsächlichen FVM-Wandflächenvektoren berechnen, P2M konservativ volumengewichtet formulieren und singuläre Korrekturmatrizen $\mathbf{M}_i$ explizit melden/regularisieren statt still durch $\mathbf{I}$ zu ersetzen.
5. **Korrekte Definition von `aspect_ratio` und `wsrd_alpha` (Audit-M1 & M2):**
   * **`aspect_ratio` ([`difvm_topology.py:1472-1480`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L1472-L1480), [`difvm_mesh.cpp:694-718`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/difvm_mesh.cpp#L694-L718)):** Nicht alle Paar-Abstände inkl. Raumdiagonale vergleichen (liefert beim perfekten Würfel $\sqrt{3} \approx 1.732$ statt $1.0$!), sondern entweder das Verhältnis längster zu kürzester **Zellkante** oder die OpenFOAM-Zell-Aspect-Ratio $AR_c = \frac{1}{6}\frac{\max(\sum |S_{f,x}|, \sum |S_{f,y}|, \sum |S_{f,z}|) \cdot \Delta_{\max}}{V_c}$ verwenden ($AR_{\text{Würfel}} \equiv 1.0$).
   * **`wsrd_alpha` ([`difvm_topology.py:1491-1495`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/difvm_topology.py#L1491-L1495), [`difvm_mesh.cpp:679-691`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/difvm_mesh.cpp#L679-L691)):** Nicht durch `median(cell_volumes)` teilen (markiert in gradierten Octree-Netzen jede ungeschnittene feine Zelle fälschlich als Cut-Cell!), sondern durch das ungeschnittene Referenzvolumen $V_{\text{bg}}(\ell_c) = h_c^3$ des jeweiligen Octree-Levels $\ell_c$.
6. **Weitere Robustheits-Fixes (Audit-M3, M4, M7, M8, M12, L1, L3):**
   * **M3:** Polygon-Normalen und -Schwerpunkte ab dem arithmetischen Knotenmittel $\bar{\mathbf{x}}_f$ mit **vorzeichenbehafteten** Teilflächen berechnen (korrekt auch für konkave Polygone); Flächenorientierung rein topologisch statt gegen den Zellknoten-Mittelwert bestimmen.
   * **M4:** Alle absoluten Toleranzen (`-1e-8` in `geometry_sdf.py:1287`, `1e-15`, `0.05`, `1e-4` bei Cyclic) relativ zur Bounding-Box-Diagonale bzw. lokalen Zellgröße skalieren.
   * **M7:** Beim 2D-Ear-Clipping mit Innenlöchern (`distmesh_core.py:473-506`) Eberlys horizontalen Strahl- & Sichtbarkeitstest verwenden, damit Loch-Brücken keine anderen Kanten schneiden.
   * **M8:** `cyclic`-Paarung (`difvm_topology.py:1685-1711`) via `cKDTree`, beliebiger Rotationsachse/Translationsvektor und bijektivem 1:1-Matching + Flächenvergleich umsetzen.
   * **M12:** Winding-Number-Auswertung (`geometry_sdf.py:989-1052`) über die Abfragepunkte chunken (z. B. `4096` Punkte pro Batch), um Multi-GB-Zwischenarrays bei $>10^5$ Punkten zu vermeiden.
   * **L1 & L3:** `hilbert_sfc_reorder` ehrlich in `morton_sfc_reorder` umbenennen (oder echten 3D-Hilbert-Index implementieren); im Rust-Reader (`backend/native_rust/`) ebenfalls einen zelllokalen Referenzpunkt $\mathbf{x}_{\text{ref}}$ für Schwerpunkte nutzen, um Auslöschung fernab des Ursprungs zu verhindern.

---

### 9.4 🟠 Physik-Quellen, OpenFOAM-Schnittstelle & Dokumentations-Ehrlichkeit (Audit-K7, K8, H5, H6, M10, L4, L5)

1. **Generische Reaktionskinetik & Quellterm-Kopplung datengetrieben implementieren (`genericReactionSource.C`, Audit-K8, R1):**
   * **Befund:** Bisherige Kinetikquellen waren hardcodiert, verletzten Massen-/Energiebilanzen durch unvollständige Spezieskopplung und ignorierten `fieldi` in `addSup`.
   * **Fix:** Vollständig generische, datengesteuerte Reaktionsquellterme (`genericReactionSource`) mit dynamischer Stöchiometriematrix, Arrhenius-Parametrisierung, Reaktionsenthalpien und analytischer Jacobi-Hauptdiagonale für beliebige Spezies- und Enthalpiegleichungen implementieren.
2. **Vollständiger OpenFOAM-Export, lauffähiger Case-Generator & gehärteter WSL-Runner (Audit-H5, H6, M10):**
   * **H5 (`polymesh_io.py`):** CHT-Netze für `chtMultiRegionFoam` in getrennte Sub-Regionen (`constant/<region>/polyMesh` + `constant/regionProperties`) mit vollständigen `mappedWall`-Einträgen (`sampleMode nearestPatchFace`, `sampleRegion`, `samplePatch`) und `cyclic`-Einträgen (`neighbourPatch`, `transform`) schreiben.
   * **H6 (`case_generator.py`, `registry.py`):** Vollständige `0/`-Startfelder, gültige `FoamFile`-Header, `fvSolution`-`SIMPLE`/`PIMPLE`-Blöcke und dynamische `processorWeights` passend zu `mpi_ranks` erzeugen; nicht in Standard-OpenFOAM existierende Custom-Dictionaries klar als Platzhalter für eigene Solver-Erweiterungen kennzeichnen.
   * **M10 (`wsl_runner.py`):** Alle Pfade und Solver-Argumente mit `shlex.quote(...)` absichern, Exit-Codes (`proc.returncode != 0`) hart prüfen und das angekündigte Live-Residuen-/Divergenz-Parsing implementieren.
3. **Dokumentations-Ehrlichkeit & Repo-Hygiene (Audit-K7, L4, L5, N1):**
   * **K7:** [`README.md`](file:///G:/Datein/Projects/CFDSolver/README.md), [`ROADMAP.md`](file:///G:/Datein/Projects/CFDSolver/ROADMAP.md), [`PROJECT.md`](file:///G:/Datein/Projects/CFDSolver/PROJECT.md) und `TEST_READY.md` ehrlich auf den tatsächlichen Code-Stand abgleichen: Klar trennen zwischen **„Bereits im Repo implementiert“** und **„Geplante Ziel-Architektur (Phase 3+)“**.
   * **L4 & L5:** Lokale `file:///G:/Datein/...`-Links in `README.md`/`ROADMAP.md` durch relative GitHub-kompatible Pfade ersetzen; [`requirements.txt`](file:///G:/Datein/Projects/CFDSolver/requirements.txt) von **UTF-16** nach **UTF-8** konvertieren.


---

## 10. Integration der Super-Elements-Dissertation ([`SuperElements_Db_115554.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/SuperElements_Db_115554.pdf), Stromberg 2023) in Mesher, FEM/CHT & TopOpt

> **Quelle:** H. Stromberg (2023), *„Combinatorial Meshing for Mechanical FEM“*, Diss. D 104, TU Clausthal (Detailformeln in **[`literature_survey_2026.md` §9.9](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md)** und Code-Vorlage in **[`INDEX.md` Snippet 8](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/INDEX.md)**).  
> **Kern-Mehrwert:** Löst das in **Audit-K2** (Abschnitt 9) kritisierte Problem („echte Cut-Cells vs. bloßes Treppenstufen-Octree“) auf extrem elegante Weise, **ohne** dass zur Laufzeit instabile geometrische Polyeder-Schnitte (Splitter-Facetten, $\alpha_i \to 0$) oder ein externer Solver nötig sind!

### 10.1 `constexpr` Lookup-Tabelle der 23 ASP-optimierten Hexaeder-Super-Elemente in [`octree_hexcore.cpp`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/octree_hexcore.cpp)
* **Warum das ein Gamechanger für `octree_hexcore.cpp` ist:**
  * Statt eine angeschnittene Rand-Hintergrundzelle willkürlich zu clippen (erzeugt degenerierte Splitterzellen) oder nur ihre 8 Ecken zu verschieben (scheitert an konkaven/steilen Schnitten), wird jede geschnittene Hexaeder-Zelle anhand der 8-Bit-In/Out-Maske ihrer 8 *Outer Nodes* ($2^8 = 256$ Zustände $\to$ **23 symmetriereduzierte kanonische Fälle**) durch das **mit Answer Set Programming (Clingo) global optimal gelöste Super-Element** (aus **Anhang B & C, Tab. B.1 & Tab. C.2–C.4, S. 121–155**) ersetzt!
  * Jedes Super-Element nutzt Teilmengen von **35 festen lokalen Kandidatenknoten in $[-1,1]^3$** (inkl. der **8 Oktanten-Stützknoten $(\pm 0.5, \pm 0.5, \pm 0.5)$**) und zerlegt die angeschnittene Zelle in wohlgeformte `Hex8`-, `Prism6`-, `Pyr5`- und `Tet4`-Elemente.
  * **Vier harte mathematische Garantien zum Nulltarif ($\mathcal{O}(1)$ Tabellen-Lookup pro Zelle):**
    1. **Beweisbare Mindestqualität $\min SJ \ge 0.302$** über alle 23 Schnittfälle hinweg (in 14 von 23 Fällen sogar $\min SJ \ge 0.500$) bereits *vor* dem Glätten!
    2. **100 % frei von Hanging Nodes UND Hanging Edges** (Abb. 4.1, 5.1, 5.2): Da die Flächentriangulierung/-quadrangulierung jeder der 6 Würfelseiten eindeutig durch die aktiven Eckknoten dieser Seite festgelegt ist, passen Nachbarzellen automatisch konform zusammen – selbst für quadratische FEM-Ansätze (20-Knoten-Hex / 10-Knoten-Tet in Thermo-Mechanik/FSI).
    3. **3 wählbare Katalog-Profile direkt aus Anhang C:**
       * `Tab. C.2` (*Max-Min Scaled Jacobian* — höchste numerische Stabilität für steife CFD/Struktur-Kerbspannungen),
       * `Tab. C.3` (*Min Element Count* — minimaler RAM- und Rechenzeitbedarf),
       * `Tab. C.4` (*Min Node Valence* — minimale Bandbreite der dünnbesetzten CSR-Systemmatrix).
    4. **Komplementär-Templates für konkave Innenkanten & 2:1-Octree-Übergänge (Abb. 5.10, S. 76):** Konkave Innenkanten (mit $k$ aktiven Knoten) werden als Komplement der $(8-k)$-Fälle im $2\times 2\times 2$-Block abgebildet; der 2:1-Octree-Stufensprung nutzt dasselbe Pyramiden-/Prismen-Stützschema.

### 10.2 Asymmetrisches Residual Volume ($RV_{\text{missing}}^2 + RV_{\text{excess}}$) & $\mathcal{O}(n_{\text{elem}})$ Voting-Heuristik
* **Vermeidung des Zigzag-Fehlers an fast gitterparallelen Wänden (Kap. 6.2 & 6.4, S. 78–84):**
  * Pro Rand-Precursor-Zelle $e$ wird auf $6\times 6\times 6 = 216$ Sub-Abtastpunkten (deren Zugehörigkeit zu den 256 Super-Element-Masken **einmalig offline** als `uint64_t`-Bitset vorberechnet ist!) geprüft, ob $\phi(\mathbf{x}_s) \le 0$ gilt.
  * Die Kostenfunktion bestraft fehlendes Bauteil-/Fluidvolumen **quadratisch** und überstehendes Volumen **linear**:
    $$R(e, D) = \left(RV_{\text{missing}}(e, D)\right)^2 + RV_{\text{excess}}(e, D), \qquad D^*(e) = \arg\min_{D \in \{0,\dots,255\}} R(e, D)$$
  * Anschließend stimmt jede Zelle $e$ mit $v(e, N) \in \{+1, -1\}$ über ihre 8 Eckknoten $N$ ab. Knoten mit $\sum_{e \ni N} v(e, N) > 0$ bleiben aktiv; daraus folgt in **strikt linearer Zeit $\mathcal{O}(n_{\text{elem}})$** (wenige Millisekunden in C++20) die global konsistente Super-Element-Topologie.

### 10.3 Top-Down Entity Mapping & Automatisches Defeaturing in $\mathcal{O}(n \log n)$ (Kap. 7, S. 87–91)
* **Scharfe Kanten & Ecken ohne manuelle Feature-Listen schützen:**
  1. **Feine Hilfs-Triangulierung ($l_{\text{tri}} = \frac{1}{2} l_{\text{min,mesh}}$):** Verhindert Newton-Raphson-Divergenz auf gekrümmten NURBS-Flächen und ignoriert automatisch Mikro-Flächen $< \frac{1}{2} l_{\text{min,mesh}}$ (*eingebautes Defeaturing*!).
  2. **3-Stufen-Deduktion:**
     * *Rand-Faces $\to$ CAD-Surfaces* via BVH/Octree-Raycasting vom Flächenschwerpunkt in $\mathcal{O}(n \log n)$.
     * *Rand-Kanten $\to$ CAD-Curves*, sobald ihre zwei Nachbarflächen auf unterschiedliche CAD-Surfaces $(S_1 \neq S_2)$ zeigen und die Kantenkette zusammenhängend ist.
     * *Rand-Knoten $\to$ CAD-Corners*, sobald dort $\ge 3$ CAD-Kurven zusammentreffen.
  3. **Exakte Freiheitsgrad-Hierarchie für unser Oberfächen-Snapping & Smoothing (`octree_hexcore.cpp` Step 8):**
     * **Corner-Nodes (0 DOF):** Exakt auf die CAD-Ecke gesetzt und eingefroren.
     * **Curve-Nodes (1 DOF):** Auf die 1D-Schnittkurve projiziert und nur tangential entlang der Kante beweglich.
     * **Surface-Nodes (2 DOF):** Auf die CAD-Surface projiziert und tangential auf der Fläche glättbar.
     * **Volume-Nodes (3 DOF):** Im Raum frei glättbar unter Maximierung von $J_{\text{ENS}}$.

### 10.4 Vereinheitlichte Qualitätsmetrik $J_{\text{ENS}}$ + Pyramiden-Basis-Check in [`models.py`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/models.py) & [`difvm_mesh.cpp`](file:///G:/Datein/Projects/CFDSolver/backend/meshing/native/src/difvm_mesh.cpp)
* **Ergänzung in `compute_quality_report()` und `validate()`:**
  * Implementierung des aspektverhältnis-sensitiven Knoten-Scaled-Jacobian $J_{s,i} = \det(\mathbf{J}_i) / (\max_k l_k)^3$ (Gl. 3.30, löst gleichzeitig **Audit-M1** für gestreckte/verzerrte Zellen!) und des auf $[-1, 1]$ normierten **Element Normalized Scaled Jacobian $J_{\text{ENS}}$** (Gl. 3.32) über Hex8 ($J_{S,\text{opt}}=1$), Prism6 ($\sqrt{3}/2$), Tet4 ($\sqrt{2}/2$) und Pyr5 ($\sqrt{2}/2$).
  * **Spezialschutz für Pyramiden (Stromberg S. 38, Abb. 3.19):** Errichtung eines fiktiven Hexaeders der Höhe $\bar{l}_{\text{base}}$ über der 4-Knoten-Pyramidenbasis während des Qualitäts-Checks, damit Pyramiden mit gefalteter/nicht-konvexer Vierecksbasis zuverlässig mit $J_{\text{ENS}} \le 0$ erkannt werden.

### 10.5 Rohteil-Precursor-Meshes (Turbomaschinen/Wellen) & SIMP-zu-CAD Body-Fitted Remeshing
1. **Zylindrische / Extrudierte Precursor-Netze gegen Orientierungssensitivität (Kap. 4.2 & MAMBO B42, S. 54–56, 98):**
   * Für rotierende Bauteile (Turbinenläufer, Wellen, Zahnräder mit $Z$ identischen Zähnen) optional ein **rotationssymmetrisches Zylinder-Sweep-Netz als Precursor-Mesh** statt einer kartesischen Box nutzen $\implies$ jeder Zahn erhält bit-identische Netzqualität und Spannungs-/Strömungsverteilung!
2. **Millisekunden-Konverter vom SIMP-Topologieoptimierungsfeld $\gamma_i \in [0, 1]$ zum Body-Fitted Verifikationsnetz:**
   * Nach Abschluss einer SIMP/Brinkman-Topologieoptimierung auf dem Hex-Gitter wird die Isofläche $\gamma(\mathbf{x}) = 0.5$ direkt über die asymmetrische Residual-Volume-Voting-Heuristik + die 23 Super-Elemente in ein **glattes, zig-zag-freies, 100 % konformes Mischgitter ($\min SJ \ge 0.302$)** übersetzt – bereit für die finale CHT- und Kerbspannungs-Verifikation ohne manuelles CAD-Reverse-Engineering!


---

## 11. Auswertung der 8 neuen Paper & Carbon-Architektur-Synthese (Stand: 2026-11-11)

> **Kontext:** Die 8 am 2026-11-11 in [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers) ergänzten Publikationen (vollständige mathematische Herleitungen in **[`literature_survey_2026.md` §9.10–§9.17](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md)** und Code-Blöcke in **[`INDEX.md` Snippets 9–11](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/INDEX.md)**) sowie das Architektur-Review von Carbon schließen die letzten Lücken zwischen **CAD-Import**, **GPU-Mesher**, **monolithischer Fluid-Struktur-Wärme-Kopplung (FSI/CHT)**, **Adjoint-Formoptimierung** und **binärem High-Speed-Dateiformat (`.mphys`)**.

---

### 11.1 Konkreter Nutzen der 8 neuen Paper für CAD, Mesher, Solver & Optimizer

| Neues Paper in `resources/papers/` | Schlüssel-Formel / Algorithmus | Konkrete Integration in unseren Stack |
| :--- | :--- | :--- |
| **1. [`CADWatertightFineMeshing_2604.02141v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CADWatertightFineMeshing_2604.02141v1.pdf)**<br>*(Zhou, Zint, Panozzo, Schneider – April 2026: Topology-First B-Rep Meshing)* | Verwirft fehlerhafte 2D-$(u,v)$-Trimmkurven (`PCURVE`, die in 14 % aller STEP-Dateien selbstschneiden!), vernetzt jedes NURBS-Patch ungetrimmt auf $[0,1]^2$, bettet 3D-Randkurven per **Closest-Point + Dijkstra-Kürzeste-Pfade** (`Alg. 1 & 2`) ein, trennt Innen/Außen rein topologisch nach dem Jordan-Kurvensatz (`KeepDisk`/`KeepComplement`), glättet harmonisch ($\Delta u = 0$) und vernäht Nachbarflächen über die **1D-Kurvenparameter-Vereinigung $\{t_k\} \subset [0,1]$**. | **Ultimativer Fix für `step_parser.py` (Audit-K9):** Garantiert **100 % wasserdichte, rissfreie Oberflächengitter** selbst bei kaputten Industrie-STEP-Dateien ohne manuelles Geometry-Cleanup (SpaceClaim-Ersatz) und erhält alle `ADVANCED_FACE`-Patch-IDs für Randbedingungen. |
| **2. [`Differtialble3DCAD_2110.01182v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Differtialble3DCAD_2110.01182v1.pdf)**<br>*(Cascaval, Schulz et al. – SIGGRAPH Asia 2021: Differentiable 3D CAD Programs)* | Baut beim einmaligen Ausführen von CAD-Operationen (`Box`, `Chamfer`, `Extrude`, `Revolve`) einen **kompilierten Berechnungsgraphen (DAG) $G$** für $\mathbf{x}_{\text{surf}} = \mathcal{F}(\boldsymbol{\theta}_{\text{CAD}})$ samt **automatischen geometrischen Sicherheits-Ungleichungen** $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$ (z. B. Fase $d \le \|V_4 - V_3\|_2$) und biharmonischer/ARAP-Regularisierung $\operatorname{tr}(\mathbf{D}^T\mathbf{Q}\mathbf{D})$ auf. | **Brücke vom Adjoint-Solver ins parametrische CAD:** Liefert ohne Neuvernetzung und ohne CAD-Kernel-Aufruf in der Schleife die exakte Kettenregel $\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}_{\text{CAD}}} = (\frac{\partial J}{\partial \mathbf{x}_{\text{surf}}})^T \frac{\partial \mathbf{x}_{\text{surf}}}{\partial \boldsymbol{\theta}_{\text{CAD}}}$ unter Einhaltung von $g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$ (via SLSQP/MMA). |
| **3. [`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Continuous_Mesh_Framework_Part_II_Validations_and_.pdf)**<br>*(Loseille & Alauzet – SINUM 2011: Continuous Mesh Part II)* | Beweist die global optimale $L^p$-Metrik $\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p}\det(|H_u|)^{-\frac{1}{2p+3}}|H_u|$ (**Gl. 2.10**) mit Fehlerschranke $\mathcal{O}(N^{-2/3})$ (**Gl. 2.11**) und rauschfreier $P_2$-Kantenmittelpunkts-Rekonstruktion $u_h(\mathbf{e}) = \frac{u_h(\mathbf{p})+u_h(\mathbf{q})}{2} + \frac{\nabla_R u_h(\mathbf{p})-\nabla_R u_h(\mathbf{q})}{8}\cdot\mathbf{p}\mathbf{q}$ (**§3.2**). | **Multiskalen-Sizing in `octree_hexcore.cpp` (löst Audit-H10):** Während $L^\infty$ alle Zellen im stärksten Stoß konzentriert, gewichtet $p=1,2$ über $\det(|H_u|)^{-1/(2p+3)}$ auch schwache Nachlaufwirbel und Akustikwellen optimal für eine exakt vorgegebene Ziel-Zellzahl $N$. |
| **4. [`MeshlessVoronoiOnGPU_3272127.3275092.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshlessVoronoiOnGPU_3272127.3275092.pdf)**<br>*(Ray, Sokolov, Lefebvre, Lévy – SIGGRAPH Asia 2018: Meshless Voronoi on GPU)* | Speichert jede konvexe 3D-Polyederzelle rein dual als Ebenen-Array `float4 P[64]` und duales Dreiecks-Array `int3 T[96]` ($\equiv$ primale Ecken der Valenz 3) lokal im **GPU Shared Memory** (`clipByPlane` Alg. 3 + *Radius of Security* $\|\mathbf{x}_p - \mathbf{x}_i\| > 2 R_{\max}$ + Volumen aus 6 orientierten Projektions-Tetraedern pro Ecke). | **10 Mio. 3D-Polyederzellen in 800 ms direkt im VRAM:** Baut die FVM-Nachbarschafts-, Flächen- und Volumen-Arrays (`owner`, `neighbour`, $\mathbf{S}_f$, $V_i$, $\mathbf{C}_i$) komplett ohne globalen Delaunay-Graphen und ohne Locks auf der GPU auf; ersetzt die 6 Start-Ebenen der BBox durch Octree-Hexaeder oder CAD-Dreiecke für GPU-Cut-Cells! |
| **5. [`VoronoiDiagrams_2404.18629v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoronoiDiagrams_2404.18629v1.pdf)**<br>*(Numerow, Li, Coros, Thomaszewski – SIGGRAPH 2024: Differentiable Voronoi Diagrams)* | Jeder der 4 Typen von 3D-Restricted-Power-Diagramm-Ecken $\mathbf{x}_c$ löst das lokale $3\times 3$-System $\mathbf{A}\mathbf{x}_c = \mathbf{b}$ (**Gl. 4**), woraus geschlossen $\frac{\partial \mathbf{x}_c}{\partial q} = \mathbf{A}^{-1}(\frac{\partial \mathbf{b}}{\partial q} - \frac{\partial \mathbf{A}}{\partial q}\mathbf{x}_c)$ und $\frac{\partial^2 \mathbf{x}_c}{\partial q\partial r}$ folgen (**Gl. 6–9, 29**). Zellvolumina $V_i(\mathbf{c})$ sind über Topologie-Flips hinweg **$C^1$-stetig**! | **Exakte analytische Gitter-Sensitivitäten für DiFVM & Newton-CVT-Glättung:** Da Ray et al. (Paper 4) für jede Ecke bereits das Ebenentripel $(u,v,w)$ in `int3 T[96]` speichert, fallen die exakten Ableitungen $\frac{\partial V_i}{\partial \mathbf{x}_k}, \frac{\partial \mathbf{S}_f}{\partial \mathbf{x}_k}$ für Adjoint-Shape-Optimization und BDF2-Moving-Mesh zum Nulltarif ab! |
| **6. [`DiscreteGCL_1709.04788v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/DiscreteGCL_1709.04788v4.pdf)**<br>*(Skopenkov 2023: Discrete Field Theory + Guillard/Farhat ALE-DGCL)* | 1. *Skopenkov:* Exakter diskreter Energie-Impuls-Tensor $\partial T[\phi] = 0$ (**Gl. 10**) und diskrete Stokes-Formel $\langle T, \partial g\rangle_k = \langle \partial T, g\rangle_k$ (**Prop. 4.2.4**) über $\delta\delta=0, \partial\partial=0$.<br>2. *3D-ALE-DGCL:* Überstrichenes Facettenvolumen via **Simpson-Regel** $\Delta V_f^{n,n+1} = \frac{1}{6}\Delta\mathbf{x}_f \cdot (\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1})$ und BDF2-Fluss $\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\Delta t}$. | **Behebt Audit-H9 in `moving_mesh.py`:** Bisher nutzte `moving_mesh.py` nur $\mathbf{w}_f \cdot \bar{\mathbf{S}}_f$ (in 3D nicht exakt!). Die Simpson-Swept-Volume-Formel + BDF2/RK-DGCL-Gewichtung verhindert künstliche Massen-/Druckquellen auf bewegten Gittern bis auf Maschinengenauigkeit. |
| **7. [`Demirdzic-2022-...pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf)**<br>*(Demirdžić & Cardiff 2022: CC-FVM Continuum Mechanics & Symmetry BCs)* | Beweist, dass an jeder Symmetrieebene wegen $\boldsymbol{\Delta} \parallel \mathbf{s}$ die **nicht-orthogonale Cross-Diffusion exakt Null** ist (**Gl. 3–4**), und leitet über den Householder-Tensor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ (**Gl. 8**) den diagonaldominanten Patankar-Split für Vektoren $\mathbf{v}$ (**Gl. 15–16**) und Tensoren 2. Stufe $\mathbf{T}$ (**Gl. 21–22**) samt Schiefheitskorrektur $\mathbf{t}\cdot\mathbf{G}_P$ (**Gl. 34**) her. | **Monolithische Fluid- & Festkörper-FVM im selben Graphen:** Erlaubt es, lineare/hyperelastische Festkörpermechanik (Spannungen, Verschiebungen $\mathbf{u}_s$) und Fluidströmung auf demselben Polyedergitter mit identischer FVM-Datenstruktur und exakten Symmetrie-Randbedingungen zu lösen. |
| **8. [`FSI_Monolithic_2505.22920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FSI_Monolithic_2505.22920v1.pdf)**<br>*(Prajapati, Fakhreddine, Mahesh – Mai 2025: Monolithic Eulerian VOF/PLIC FSI)* | Ein-Feld-Formulierung $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$ (**Gl. 10**) mit hyperelastischem Cauchy-Green-Tensor $\mathbf{B} = \mathbf{F}\mathbf{F}^T$, Oldroyd-Transport (**Gl. 16**, WENO-Z), geometrischer PLIC-Advektion (**Gl. 29**) und hartem **Fluid-Reset $\mathbf{B} = \mathbf{I}$ für $\phi < 10^{-8}$**. | **Große FSI-Verformungen (Ventile, Membranen, Kontakt) ohne Remeshing:** Erreicht dank geometrischem PLIC + $\mathbf{B}$-Reset auf einem $128^3$-Gitter dieselbe Grenzflächenschärfe wie ältere Euler-FSI-Solver auf $1024^3$ ($512\times$ weniger Zellen!). |

---

### 11.2 Kritische Bewertung & Übernahme von Carbons Architektur-Vorschlägen

Carbons Analyse trifft unsere Ziel-Architektur in fast allen Punkten ins Schwarze. Hier ist die verbindliche Einordnung für unsere Umsetzung:

#### 1. Sprach-Architektur: Rust + C++20/23 + LLVM Enzyme AD (und wo Fortran sinnvoll abgegrenzt wird)
* **Vollständige Zustimmung zu Rust (I/O, Orchestrierung, Networking) + C++20/23 (Mesher & FVM/FEM-Solver):**
  * Dass Mesher und Solver **denselben C++20/23-Speicherkern** teilen, ist entscheidend: Unsere `octree_hexcore.cpp`-, GPU-Voronoi- und FVM-Operatoren arbeiten Zero-Copy auf denselben 64-Byte-aligned SoA-Spans (`std::span` / `std::mdspan`).
  * **Carbons Einwand zu Fortran ist für den unstrukturierten Graph-FVM-Kern und Adjoint-Optimizer absolut berechtigt:** Auf unstrukturierten Polyedergittern bringt Fortran gegenüber C++20 mit `__restrict__` keinen Geschwindigkeitsvorteil, erschwert aber durch Row-Major/Column-Major-Unterschiede und separate Compiler-Pipelines die automatische Differenzierung.
  * **Konsequenz für unseren Stack:**
    1. Der gesamte **unstrukturierte FVM/FEM-Feldlöser, Mesher und Adjoint-Kern** wird einheitlich in **C++20/23** geschrieben und direkt über **LLVM Enzyme AD** ([`resources/reference_spickzettel/Optimization_AD/Enzyme`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme)) auf LLVM-IR-Ebene rückwärts differenziert (kombiniert mit dem impliziten Funktionentheorem und Binomial-Checkpointing nach *DiFVM* / *Optimistix*).
    2. Optionale `pure bind(C)` Modern-Fortran-Kerne bleiben (falls überhaupt genutzt) **ausschließlich auf rein zelllokale 0D-Punkt-Routinen** (wie steife Arrhenius-Chemie/NASA-Polynome pro Zelle, wo es keine Gitter-Nachbarschaftsindizes gibt) beschränkt und werden über `flang-new` in denselben LLVM-Bitcode übersetzt.

#### 2. Open Software Architecture: Strikte C-ABI Data-Lake-Architektur & Deklarativer DAG
Damit jedes Modul (Mesher, FVM-Solver, Solid-Solver, Optimizer, Visualizer) einzeln austauschbar bleibt:
* **Zentraler SoA Data-Lake über C-ABI (`extern "C"`):**
  Keine geschachtelten C++-Klassen über Modulgrenzen hinweg! Alle Gitter- und Feldgrößen werden als flache, 64-Byte-aligned Arrays (`owner`, `neighbour`, `face_areas`, `cell_volumes`, `pressure`, `velocity`, `gamma_simp`, `B_tensor`) in einem zentralen Memory-Pool verwaltet und als schlanke C-Structs `(void* ptr, size_t len, uint32_t dtype, uint32_t device_id)` zwischen Rust, C++ und GPU übergeben.
* **Eingebettete Ausdrucks-Engine für Randbedingungen & Quellterme:**
  Für benutzerdefinierte Einströmprofile $u_{\text{in}}(x,y,z,t)$, temperaturabhängige Stoffwerte oder Custom-Quellterme wird eine leichtgewichtige Expression-Engine (z. B. **Rhai** in Rust oder ein kleiner SIMD-Bytecode-Evaluator in C++) angebunden, damit Nutzer Formeln ohne Neukompilierung übergeben können.

#### 3. Spezifikation des nativen Container-Dateiformats (`.mphys` / `.sim`)
Um das langsame OpenFOAM-Einzeldatei-Chaos (tausende ASCII-Dateien) abzulösen, wird für den Rust-Fast-I/O-Layer (`backend/native_rust/`) folgendes **Chunked Binary Container-Format (`.mphys`)** festgelegt:

| Offset / Sektion | Inhalt | Format & Alignment | Technischer Vorteil |
| :--- | :--- | :--- | :--- |
| **`0x00 – 0x40` (64 Byte Header)** | Magic Bytes `MPHYS001`, Format-Version, Endianness-Flag, `uint64_t footer_offset`, `uint64_t mesh_epoch_id` | Raw Binary (64 Byte) | Sofortige Dateierkennung und $\mathcal{O}(1)$-Sprung zum FlatBuffers-Footer am Dateiende. |
| **Static Mesh Epoch (`mesh_epoch_0`)** | `points`, `faces` (CSR), `owner`, `neighbour`, $\mathbf{S}_f, \mathbf{c}_f, \mathbf{C}_i, V_i$, `MeshErrorMap`, sortiert entlang der **3D-Hilbert-/Morton-Kurve** in festen Blöcken à **65.536 Zellen** | 64-Byte-aligned Little-Endian Arrays, komprimiert mit **Blosc2 / Zstd (oder LZ4)** | Statisches Gitter wird nur **einmal** geschrieben; alle Zeitschritte referenzieren nur `mesh_epoch_0` (erst bei adaptivem Remeshing folgt `mesh_epoch_1`). |
| **Dynamic Field Steps (`step_0001..N`)** | Pro Zeitschritt/Iteration nur die veränderten SoA-Felder ($p, \mathbf{u}, T, \phi_{\text{VOF}}, \mathbf{B}, \gamma_i$) in denselben 65.536er Hilbert-Chunks | 64-Byte-aligned Blosc2/LZ4-Chunks | **Lock-freies Multi-Node/MPI-Schreiben** (jeder Rechenknoten schreibt seine disjunkten Chunks parallel) und direkter DMA-Transfer via `mmap` in RAM/VRAM. |
| **Spatial Chunk Index** | Achsenparallele Bounding-Box (AABB $[x_{\min}, x_{\max}]^3$) + `[min_val, max_val]` pro 65.536-Zellen-Chunk | Kompaktes R-Tree- / Morton-Intervall-Array | **Out-of-Core Visualisierung:** Bei 100+ Mio. Zellen lädt der Visualizer per `mmap` nur diejenigen Chunks in den GPU-VRAM, die im Kamera-Frustum liegen oder die gesuchte Isofläche $[\phi_{\text{iso}}]$ enthalten! |
| **Footer (am Dateiende, Parquet-Prinzip)** | Vollständiges Schema, SI-Einheiten-Hashes, Patch-Definitionen (`name`, `type`, `start_face`, `n_faces`), Chunk-Offsets, Kompressions-Flags und SHA-256-Prüfsummen | **FlatBuffers** (oder CBOR) + abschließendes `uint64_t footer_size` + Magic `MPHYSEOF` | **Append-Only Streaming & Crash-Sicherheit:** Neue Zeitschritte werden während des Laufs einfach hinten angehängt und ein neuer Footer geschrieben; eine 50-GB-Datei öffnet in **$< 1\,\text{ms}$** ohne Parsing der Nutzdaten! |

#### 4. Zero-Copy In-Situ Visualisierung (Hardware RT-Cores für unstrukturierte Polyedergitter)
* Für die eigenständige Desktop-Applikation (Rust `egui`/`Slint` + `wgpu`/Vulkan bzw. C++ Vulkan/OptiX-Viewport) werden die Lösungsfelder auf der GPU **nicht** über den PCIe-Bus zurück zur CPU kopiert, sondern per Vulkan/CUDA External Memory Interop direkt im VRAM gerendert.
* Nach dem Prinzip von **Morrical, Usher, Wald & Pascucci (2019–2021)** (*Efficient Space Skipping and Adaptive Sampling of Unstructured Volumes*) werden die Hardware-Raytracing-Einheiten (**RT-Cores**) moderner GPUs genutzt, um über das grobe Hilbert-Chunk-/Octree-BVH leere Bereiche zu überspringen (*Space Skipping*) und Punkt-in-Polyeder-Abfragen sowie Volumen-Raymarching direkt in Echtzeit (60–120 FPS) während der laufenden Solver-Iteration auszuführen.


---

## 12. Auswertung der neuen LMArena Solver- & Mesher-Spickzettel (`Fable 5.2`, `Sol 6.1 Max`, `Gemini 4 Argon High CUDA` & `Step 5 Preview` — Stand: 2026-11-13)

> **Kontext:** Vollständige technische Bewertung und Synthese der 7 neu hinzugefügten Spickzettel in [`resources/reference_spickzettel/LMArena/Solver/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver) und [`resources/reference_spickzettel/LMArena/Mesher/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher) (direkte Code-Lesezeichen in **[`INDEX.md` Kategorie 7b & Snippets 12–14](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/INDEX.md)**).

---

### 12.1 Gesamtbewertung der 7 neuen Spickzettel im Überblick

| # | Spickzettel (Direkt-Link) | Bewertung | Modell / Typ | Wichtigster Mehrwert für unseren Stack |
| :--- | :--- | :---: | :--- | :--- |
| **1** | **[`MesherAndSolverSol61Max/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max)**<br>*(„continuum — Solver Engineering Notebook“)* | ⭐⭐⭐⭐⭐ **(5.0 / 5)** | **GPT-6 Sol / Sol 6.1 Max** (`g++ -std=c++20 -O2` getestet + 14 Playwright-Tests + 9 C++20/CUDA/MPI-Snippets) | **Numerischer & HPC-Goldstandard:** Deckt **3 kritische Bugs** in naiven 2-Klassen-GPU-Solvern auf (`base-all`-Vertrag auf allen 4 Segmenten, 2-Stufen-Halo `halo_phi` + `halo_grad` für Nachbar-Gradienten $(\nabla\phi)_N$, atomikfreier `CellFaceCSR`-Gather ohne Ghost-Row-Korruption) und beweist in [`reference_test.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/reference_test.cpp) exakte Reproduktion linearer 3D-Felder auf schiefen Flächen. |
| **2** | **[`SolverGemini4ArgonHighCUDA.md`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md)** | ⭐⭐⭐⭐⭐ **(5.0 / 5)** | **Gemini 4 Argon High CUDA** (Kompilierbarer CUDA/C++-Code + 6 tiefgehende GPU/AD-Analysen) | **Direkte CUDA-Vorlage für unseren 4-Quadranten-Dispatcher:** Kompakte **6%-`MeshCorrectionSoA`** (spart 94 % Korrekturspeicher über `seg3_corr_offset`), **gradientenfreie konvektive Upwind-Deferred-Correction in Klasse 0**, Continuity-Diagonalkorrektur, Schutz vor der 24-Byte-`double3`-Alignment-Falle und Lösung des **Hilbert-SFC Warp-Atomic-Staus in LLVM Enzyme Reverse-Mode AD**. |
| **3** | **[`Fable52HighChemSolver/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver)**<br>*(„Å→m Multiphysics Chemistry Solver Guide“)* | ⭐⭐⭐⭐½ **(4.5 / 5)** | **Fable 5.2 High** (React 19 / TS Referenz-App: [`solvers.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/data/solvers.ts), [`stacks.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/data/stacks.ts), [`scale.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/lib/scale.ts), [`Speed.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/components/Speed.tsx), [`Language.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/components/Language.tsx)) | **Herausragende Architektur- & Multiskalen-Chemie-Blaupause:** 13-Dekaden-Skalenmodell ($10^{-10}\,\text{m}$ bis $10^{3}\,\text{m}$), saubere Trennung in *Solver*, *Kinetik-Kerne* (`Cantera`, `TChem`, `PelePhysics` AoT-Codegen, `Reaktoro`) und *Coupler* (`preCICE`), die **6 Performance-Hebel reaktiver Strömungen** sowie die **C++ Solver + Rust `cxx`/`diffsol`/`faer` FFI-Brücke**. |
| **4** | **[`MesherAndSolverStep5Preview/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview)**<br>*(„MESHFORGE — Architecture Hub“)* | ⭐⭐⭐⭐☆ **(4.1 / 5)** | **Step 5 Preview** (Interaktives 8-Sektionen-Dashboard + CUDA-Skizzen in [`content.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/data/content.ts)) | **Starkes Ideen- & Cockpit-UI-Vorbild:** Interaktiver 4-Segment-`MeshErrorMap`-Explorer mit VRAM-Formel $\Delta_{\text{VRAM}} = \frac{96\,N_0}{140\,N}$, 3-Stream CUDA/MPI Timeline-Scrubber, Hilbert-SFC Strong-Scaling-Rechner ($s$-step Krylov + ZFP), $128^3$ Pilot-LBM Sizing (`pilot_metric_kernel`) und tangential projiziertes Enzyme Seed-Morphing (*Achtung: die 3 Kernel-Bugs in `content.ts` werden durch `Sol61Max` korrigiert!*). |
| **5** | **[`BlackglasIChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/BlackglasIChemSugg.txt)** | ⭐⭐⭐⭐ **(4.0 / 5)** | **Blackglas I** (321 Zeilen Literatur- & Benchmark-Studie mit 51 Quellen) | **Beste Literatur- & Benchmark-Fundgrube für reaktive CFD:** Dokumentiert **DLBFoam** (~10× Speedup durch dynamisches Lastbalancing steifer Chemie-ODEs), **Analytische Sparse-Jacobians** (~100× Speedup), **DeepFlame** (100× Speedup bei $\text{H}_2$-Zündung), **EBIdnsFoam**, **MUI** und die Standard-Validierungsfälle **Sandia Flame D** & **ECN Spray A**. |
| **6** | **[`ChatGPTSol61SearchMaxChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/ChatGPTSol61SearchMaxChemSugg.txt)** | ⭐⭐⭐ **(3.0 / 5)** | **ChatGPT Sol 6.1 Search Max** (Kompakte Entscheidungsnotiz, 33 Zeilen) | Präzise Einordnung von **dünnbesetzten vorkonditionierten Jacobi-Matrizen** in `Cantera`/`SUNDIALS` sowie Rusts **`diffsol`**-Crate (BDF/SDIRK mit Sensitivitätsanalyse) rund um einen C++-Solverkern. |
| **7** | **[`Mistral4LargeChemSolverSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeChemSolverSugg.txt)** | ⭐⭐⭐ **(3.0 / 5)** | **Mistral 4 Large** (Entscheidungsmatrix, 70 Zeilen) | Übersichtliche 6-Zeilen-Entscheidungsmatrix (`MOOSE`, `OpenFOAM + Cantera`, `AMReX/PeleC`, `MFEM`, `deal.II + SUNDIALS`, `LAMMPS + preCICE`) und Hybrid-Rust/C++-FFI-Empfehlung. |

---

### 12.2 Sechs verbindliche Numerik- & HPC-Verträge für unseren 4-Quadranten FVM-Solver (Synthese aus `Sol61Max`, `Gemini4ArgonHighCUDA` & `Step5Preview`)

Die Kombination aus [`MesherAndSolverSol61Max`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max), [`SolverGemini4ArgonHighCUDA.md`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md) und [`MesherAndSolverStep5Preview`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview) liefert die exakte Produktions-Spezifikation für das Zusammenspiel von Mesher (`MeshErrorMap`) und GPU/CPU-Solver (erweitert **Abschnitt 8.1**):

#### 1. `base-all`-Vertrag & Kompakte 6%-`MeshCorrectionSoA` (`face_layout.hpp` + `SolverGemini4ArgonHighCUDA.md`)
* **Kritischer Bug in naiven Skizzen (`Step5Preview` `content.ts:378-391`):** Wenn `launch_ortho_fast` nur auf `Seg 0` und `Seg 2` aufgerufen wird, während `launch_skew_correction` (das nur explizit in `rhs` schreibt!) auf `Seg 1` und `Seg 3` läuft, **fehlt auf allen schiefen/nicht-orthogonalen Flächen der implizite 2-Punkt-Basisoperator in der Matrix $\mathbf{A}$ komplett!**
* **Verbindliche Lösung:**
  * Alle internen Flächen werden entlang der 3D-Hilbert-Kurve in **4 Quadranten** sortiert:
    * **`Seg 0` (`inner_fast`, ~85–90 %):** Lokal, $\theta_f < 1^\circ$, keine Schiefheit.
    * **`Seg 1` (`inner_corrected`, ~5–10 %):** Lokal (inkl. vollständiger Gradienten-Stencils!), benötigt Korrektur.
    * **`Seg 2` (`halo_fast`, ~3–4 %):** MPI-Halo-Fläche, orthogonal.
    * **`Seg 3` (`halo_corrected`, ~1 %):** MPI-Halo-Fläche (oder Fläche mit Ghost-Zellen im Gradienten-Stencil), benötigt Korrektur.
  * Entweder führt `assemble_class1_kernel` den impliziten 2-Punkt-Basisanteil direkt in *Part A* mit aus ([`SolverGemini4ArgonHighCUDA.md#L101-L129`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L101-L129)), oder `base_coefficients` läuft über **alle 4 Segmente**, während `correction_flux` nur über die **kompakte `CorrectionSoA`** (`correction_inner` + `correction_halo`, ~6 % der Flächen) iteriert ([`face_layout.hpp#L11-L35`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/face_layout.hpp#L11-L35)).
  * **94 % Speicherersparnis bei den Korrekturvektoren:** `MeshCorrectionSoA` (`m_skew_x/y/z`, `k_comp_x/y/z`, `omega`) wird **nur** für `Seg 1` und `Seg 3` allokiert. Mit dem Offset-Trick `seg1_corr_offset = b.seg0_end` und `seg3_corr_offset = b.seg2_end - (b.seg1_end - b.seg0_end)` liegen beide Segmente lückenlos im kompakten 6%-Array!
  * **Verbot von 24-Byte-`double3`-Structs auf der GPU:** Alle 3D-Vektoren (`m_skew`, `k_comp`, `grad_phi`) werden strikt als getrennte `_x`, `_y`, `_z` `double* __restrict__`-Arrays gespeichert, um unaligned 24-Byte-L1-Cache-Splits zu verhindern.

#### 2. Linear-Exakter Ebenen-Schnitt $\beta_f$ & Explizite `FaceClass::invalid`-Abweisung ([`face_geometry.hpp#L25-L53`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/face_geometry.hpp#L25-L53))
* Statt der orthogonalen Linienprojektion wird das Interpolationsgewicht $\beta_f$ als **Schnittpunkt der Verbindungsgeraden $\overrightarrow{PN}$ mit der Flächenebene** definiert:
  $$\beta_f = \frac{(\mathbf{x}_f - \mathbf{x}_P)\cdot\mathbf{S}_f}{\mathbf{d}_f\cdot\mathbf{S}_f}, \qquad \alpha_f = \frac{\|\mathbf{S}_f\|^2}{\mathbf{d}_f\cdot\mathbf{S}_f}, \qquad \mathbf{m}_f^{\text{skew}} = \mathbf{x}_f - (\mathbf{x}_P + \beta_f\mathbf{d}_f), \qquad \mathbf{k}_f^{\text{corr}} = \mathbf{S}_f - \alpha_f\mathbf{d}_f$$
* **Mathematischer Vorteil:** Dadurch liegt $\mathbf{m}_f^{\text{skew}}$ exakt in der Flächenebene ($\mathbf{m}_f^{\text{skew}}\cdot\mathbf{S}_f = 0$), und für **jedes beliebige lineare Feld $\phi(\mathbf{x}) = \phi_0 + \mathbf{g}\cdot\mathbf{x}$** gilt auf beliebig schiefen und nicht-orthogonalen 3D-Flächen exakt bis auf Maschinengenauigkeit (`reference_test.cpp:20-26`):
  $$\underbrace{\dot{m}_f\big((1-\beta_f)\phi_P + \beta_f\phi_N\big) - \Gamma_f\alpha_f(\phi_N - \phi_P)}_{\text{Klasse 0 (Impliziter 2-Punkt-Basisfluss)}} + \underbrace{\dot{m}_f\,\mathbf{g}_f\cdot\mathbf{m}_f^{\text{skew}} - \Gamma_f\,\mathbf{g}_f\cdot\mathbf{k}_f^{\text{corr}}}_{\text{Klasse 1 (Explizite Korrektur)}} \equiv \dot{m}_f\phi(\mathbf{x}_f) - \Gamma_f\,\mathbf{g}\cdot\mathbf{S}_f$$
* **Keine stillen Clamps bei invertierten Flächen:** Ist $\cos\theta_f = \frac{\mathbf{d}_f\cdot\mathbf{S}_f}{\|\mathbf{d}_f\|\|\mathbf{S}_f\|} \le 10^{-8}$ oder nicht-finit, gibt `geometry()` explizit `FaceClass::invalid` zurück, damit der Mesher/Repair-Pass (z. B. WSRD Cut-Cell-Merging oder Stromberg-Super-Elemente) greift, statt einen durch `1e-15` geteilten Müll-Leitwert in die Matrix zu schreiben.

#### 3. Gradientenfreie Konvektive Deferred Correction in Klasse 0, Pipeline-Overlap & Diagonal-Only CSR-Reset ([`SolverGemini4ArgonHighCUDA.md#L204-L244`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L204-L244))
* **Unbedingte M-Matrix-Dominanz ohne Laden von $\nabla\phi$:** In Klasse 0 wird der Konvektionsfluss in einen **impliziten 1.-Ordnung-Upwind-Teil** (garantiert $a_{PN} = \min(\dot{m}_f, 0) - \Gamma_f\alpha_f \le 0$ und $a_{NP} = -\max(\dot{m}_f, 0) - \Gamma_f\alpha_f \le 0$ für beliebige Zell-Péclet-Zahlen!) und eine **explizite lineare Deferred Correction** auf der RHS zerlegt:
  $$F_{f,\text{conv}} = \underbrace{\max(\dot{m}_f, 0)\phi_P + \min(\dot{m}_f, 0)\phi_N}_{\text{Implizit in CSR-Matrix}} + \underbrace{\dot{m}_f\big(\beta_f\phi_N + (1-\beta_f)\phi_P\big) - \big[\max(\dot{m}_f, 0)\phi_P + \min(\dot{m}_f, 0)\phi_N\big]}_{\text{Explizit auf RHS (benötigt NUR }\phi_P, \phi_N\text{ — KEIN }\nabla\phi\text{!)}}$$
* **Continuity-Diagonalkorrektur in SIMPLE/PISO:** Durch Abzug von $(\sum_{f\in P}\dot{m}_f)\phi_P$ (`fvm::Sp(fvc::div(phi), phi)`) gilt für den impliziten Diagonalbeitrag jeder Fläche exakt $a_{P,\text{diag}} = -a_{PN} = \Gamma_f\alpha_f - \min(\dot{m}_f, 0) \ge 0$, sodass auch bei $\nabla\cdot\mathbf{u} \neq 0$ in Zwischeniterationen strikt $a_{PP} = \sum_N |a_{PN}|$ erhalten bleibt.
* **Green-Gauss Gradient Pipeline-Overlap & 85 % schnellerer CSR-Reset:**
  * Da `Segment 0` (~90 % aller Flächen) dank obiger Formel **keinerlei Zellgradienten $\nabla\phi$ liest**, läuft `assemble_class0_kernel` auf `stream_internal` **gleichzeitig (überlappend) mit dem Green-Gauss-Gradienten-Kernel**! Die Gradienten müssen erst fertig sein, wenn `Segment 1` (~5 % der Flächen) startet.
  * Da die Nebendiagonalen `csr_off_PN[f]` und `csr_off_NP[f]` per Direktzuweisung (`=`) überschrieben werden, darf vor der Assemblierung **kein `cudaMemset` über das gesamte CSR-Matrix-Array** laufen – es werden nur die $N_{\text{cells}}$ Hauptdiagonaleinträge und `rhs` genullt (~85 % weniger Speicherbandbreite beim Reset).

#### 4. Atomik-freier `CellFaceCSR`-Gather gegen Ghost-Row-Korruption & LLVM Enzyme Reverse-Mode Warp-Stau ([`gather_residual.cu`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/gather_residual.cu) & [`SolverGemini4ArgonHighCUDA.md#L226-L234`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L226-L234))
* **Zwei gravierende Probleme von `atomicAdd` auf Zell-Diagonalen/RHS:**
  1. *Ghost-Row-Korruption (`Sol61Max` `ghost-writes`):* Auf MPI-Halo-Flächen (`Seg 2` & `Seg 3`) ist die Nachbarzelle $N \ge N_{\text{owned}}$ eine reine Lese-Ghost-Zelle ohne eigene lokale CSR-Matrixzeile; ein blindes `atomicAdd(&sys.rhs[N], ...)` schreibt Out-of-Bounds!
  2. *Warp-Atomic-Stau in Vorwärts- & LLVM-Enzyme-Adjoint-Läufen (`Gemini4ArgonHighCUDA` §2.D):* Weil die 3D-Hilbert-Kurve alle Flächen derselben Besitzerzelle $P$ direkt hintereinander anordnet ($f, f+1, f+2, \dots$), treffen benachbarte Threads desselben GPU-Warps bei `atomicAdd(&sys.val[csr_diag_P[f]], ...)` exakt dieselbe Speicheradresse. Wenn **LLVM Enzyme (`__enzyme_autodiff`)** diesen Kernel rückwärts differenziert, erzeugt es atomare Additionen auf die adjungierten Variablen (`d_phi`, `d_grad`), was den Rückwärtspass massiv serialisiert!
* **Verbindliche Lösung:**
  * Flächen-Kerne schreiben ihre Flüsse/Koeffizienten **100 % atomikfrei** pro Fläche (`csr_off_PN[f]`, `csr_off_NP[f]`, `face_flux[f]`).
  * Anschließend summiert ein leichtgewichtiger **`CellFaceCSR`-Gather-Kernel** (`gather_residual.cu`, 1 Thread pro eigener Zelle $0 \le c < N_{\text{owned}}$) über die 6–14 inzidenten Flächen mit Vorzeichen `orientation[k]` $\in \{-1, +1\}$ die Hauptdiagonale $a_{PP}$ und das Residuum `rhs[c]` **komplett ohne Atomics, ohne Ghost-Row-Zugriff, bitweise reproduzierbar und 100 % Enzyme-AD-freundlich** auf!

#### 5. Voll-Stencil Inner/Halo-Klassifikation & 2-Stufen MPI/CUDA Halo-Schedule ([`halo_schedule.cpp#L5-L39`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/halo_schedule.cpp#L5-L39))
* **Voll-Stencil-Regel:** Eine korrigierte Fläche gehört nur dann in `Seg 1` (`inner_corrected`), wenn sowohl $P$ als auch $N$ lokal sind **und** die Rekonstruktions-Stencils für $(\nabla\phi)_P$ und $(\nabla\phi)_N$ **keine einzige Ghost-Zelle lesen**. Andernfalls gehört die Fläche in `Seg 3` (`halo_corrected`).
* **Zweistufiger Halo-Austausch (`halo_phi` + `halo_grad`):** Ein einfacher Austausch des Skalarfelds $\phi$ (`halo_phi`) reicht für `Seg 2` (`halo_fast`) und für die Berechnung der eigenen Randgradienten $(\nabla\phi)_P$, liefert aber **nicht** den Gradienten $(\nabla\phi)_N$ der entfernten Ghost-Zelle für `Seg 3` (`halo_corrected`)! Deshalb folgt auf die Randgradienten-Berechnung zwingend ein zweiter, kompakter Halo-Austausch `halo_grad` (mit portabler Host-Synchronisation via `cudaEventSynchronize(phi_packed)` vor `MPI_Isend`).

#### 6. Drei-Karten-Trennung (`GeometryMap`, `SolutionIndicator`, `ErrorEstimator`), SPD-Cholesky-Metrik & $128^3$ Pilot-LBM ([`notebook.ts#L64-L125`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/data/notebook.ts#L64-L125) & [`content.ts#L335-L358`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/data/content.ts#L335-L358))
* Statt Geometriefehler und Diskretisierungsfehler in einer einzigen Zahl zu vermischen, trennt die `MeshErrorMap` sauber in:
  1. **`GeometryMap`** (dimensionslose Schiefheit $\|\mathbf{m}_f^{\text{skew}}\|/h_{\text{ref}}$, Nicht-Orthogonalität $\|\mathbf{k}_f^{\text{corr}}\|/\|\mathbf{S}_f\|$, $J_{\text{ENS}}$) $\implies$ steuert die statische 4-Quadranten-Klassifikation.
  2. **`SolutionIndicator`** (Wirbelstärke $\|\nabla\times\mathbf{u}_{\text{pilot}}\|$ aus dem 200-ms-$128^3$-Pilot-LBM via `pilot_metric_kernel`, Wandabstand $\text{SDF}$, Hesse-Matrix $|H_u|$) $\implies$ steuert das initiale und adaptive Sizing $h(\mathbf{x}) = \min(h_{\text{CAD}}, \frac{c_\omega}{\|\nabla\times\mathbf{u}\|+\varepsilon}, 0.05\,d_{\text{wall}})$.
  3. **`ErrorEstimator`** (Residuum, Entropie-Adjoint / Dual-Weighted Residual DWR) $\implies$ steuert zielorientierte Adaption und das tangential projizierte + per 1-Schritt-Newton rückprojizierte Voronoi-Seed-Morphing (`Snippet 14`).
* **SPD-Garantie für gelernte/anisotrope Metriken:** Jeder neuronale oder datengetriebene Metrik-Prädiktor gibt niemals rohe $3\times 3$-Einträge aus, sondern einen unteren Dreiecks-Cholesky-Faktor $\mathbf{L}(\mathbf{x})$ mit Eigenwert-Clamping $h_{\min} \le h_i \le h_{\max}$:
  $$\mathcal{M}(\mathbf{x}) = \mathbf{L}(\mathbf{x})\mathbf{L}(\mathbf{x})^T + \epsilon\mathbf{I} \succ 0$$

---

### 12.3 Reaktive Strömungen, Multiskalen-Chemie (`Å → m`) & C++/Rust-Brücke (`Fable 5.2`, `Blackglas I` & `Sol 6.1 Search Max`)

Aus [`Fable52HighChemSolver`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver), [`BlackglasIChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/BlackglasIChemSugg.txt) und [`ChatGPTSol61SearchMaxChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/ChatGPTSol61SearchMaxChemSugg.txt) übernehmen wir folgende Architektur-Prinzipien für unser `species_chem`- und `energy_heat`-Modul (wie u. a. in `genericReactionSource.C` begonnen):

1. **Operator Splitting + Dynamisches Lastbalancing steifer Chemie-Zellen (**DLBFoam**-Prinzip, ~10× Speedup):**
   * Durch Operator-Splitting (Strang/Lie) wird die steife Reaktionskinetik vom FVM-Transport entkoppelt, sodass jede Gitterzelle ein unabhängiges 0D-ODE-System $\frac{\mathrm{d}\mathbf{Y}_i}{\mathrm{d}t} = \frac{\dot{\boldsymbol{\omega}}(\mathbf{Y}_i, T_i, p_i)}{\rho_i}$ bildet (`Speed.tsx` Lever 2).
   * Da Reaktions- und Flammenfronten oft nur 5–15 % der Zellen einnehmen, während 85 % der Zellen chemisch inert oder im Gleichgewicht sind, werden die **aktiven Chemie-Zellen ($\|\dot{\boldsymbol{\omega}}_i\| > \varepsilon_{\text{react}}$)** vor dem ODE-Schritt dynamisch in eine kompakte Worklist gepackt und gleichmäßig über alle CPU-Threads (`rayon` / OpenMP) bzw. GPU-Warps verteilt (**DLBFoam-Pattern**, [`BlackglasIChemSugg.txt#L64-L66`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/BlackglasIChemSugg.txt#L64-L66) $\implies$ **~10× End-to-End-Speedup**).
2. **Ahead-of-Time (AoT) Mechanism Code Generation (**PelePhysics**-Prinzip) + Analytische Sparse-Jacobians (~100× Speedup):**
   * Allgemeine Laufzeit-Kinetik-Interpreter (wie unbatchtes `Cantera` in einer 3D-Zellschleife) bremsen GPUs und SIMD-Pipelines aus (`Fable52HighChemSolver/src/data/solvers.ts:249-256`).
   * **Lösung (`Speed.tsx` Lever 3 & `BlackglasIChemSugg.txt:67-70`):** Ein kleiner Code-Generator übersetzt Reaktionsmechanismen (YAML / Cantera-CTI) **Ahead-of-Time** in verzweigungsfreien C++20/CUDA/Rust-Code für die Produktionsraten $\dot{\omega}_k$ **und die exakte analytische, dünnbesetzte Jacobi-Matrix** $J_{kj} = \frac{\partial \dot{\omega}_k}{\partial Y_j}$. Zusammen mit vorkonditionierten Sparse-BDF/SDIRK-Lösern (`SUNDIALS CVODE` / `TChem` in C++ bzw. `diffsol` + `faer` in Rust via `cxx`-FFI, [`Language.tsx#L28-L61`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/components/Language.tsx#L28-L61)) bringt dies **bis zu 100× (zwei Größenordnungen) Beschleunigung** gegenüber Differenzenquotienten-Jacobians!
3. **13-Dekaden Multiskalen-Kopplungs-Vertrag (`10^-10 m` bis `10^3 m`, [`scale.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/lib/scale.ts) & [`stacks.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/data/stacks.ts)):**
   * **Offline-Parameter-Handoff vor Laufzeit-Kopplung (`Speed.tsx` Lever 6):** Wo sich Zeitskalen sauber trennen (z. B. Atomistik/MD $\to$ Arrhenius-Parameter & Transportkoeffizienten, oder Poren-LBM auf CT-Scans $\to$ Permeabilitätstensor $\mathbf{K}$ & effektive Diffusivität $D_{\text{eff}}$), werden Parameter als komprimierte Tabellen/Dateien übergeben.
   * **Explizite Erhaltungs-Verträge bei Laufzeit-Kopplung (`Sol61Max` `notebook.ts:130-137`):** Bei bidirektionaler Kopplung (z. B. über `preCICE` oder unseren C-ABI Data-Lake) müssen intensive Größen (Temperatur $T$, Konzentration $c$, Geschwindigkeit $\mathbf{u}$) als **`consistent`** und extensive Größen (integrierte Wärmeströme $\dot{Q}_f$, Knotenkräfte $\mathbf{F}_f$, Stoffströme $\dot{m}_{k,f}$) zwingend als **`conservative`** deklariert werden; bei impliziten Kopplungs-Subiterationen müssen **alle** Teilzustände (Fluid + Chemie + Partikel + Gitterposition) im Checkpoint gesichert werden.
4. **Übernahme der besten UI-/Cockpit-Komponenten für unser Frontend:**
   * Aus [`Fable52HighChemSolver/src/lib/scale.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/lib/scale.ts#L25-L45): Der `gradientWindow(lo, hi)`-CSS-Trick für skalenkonsistente logarithmische Farbkodierung über 13 Größenordnungen.
   * Aus [`MesherAndSolverStep5Preview/src/components/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/components): Der interaktive **4-Segment-`MeshErrorMap`-Regler** mit Live-VRAM-Bandbreitenanzeige ($\Delta_{\text{VRAM}} = \frac{96\,N_0}{140\,N}$), der **3-Stream CUDA/MPI Timeline-Scrubber** (`Takt 0..2`) und der **Hilbert-SFC Strong-Scaling-Explorer** ($\eta(K) = \frac{1}{1 + 6 f_{\text{halo}}(K) + (\gamma \log_2 K)/s}$).

