# CFDSolver / MultiphysicsSolver Platform — Master Roadmap

> **Strategische Ausrichtung:** Aufbau eines **eigenständigen, hochskalierenden In-Memory Multiphysics-, Vernetzungs- und Optimierungs-Stacks** (**C++20 / CUDA** für Mesher, gekoppelte FVM- & SPH-FVM-Kerne, **Rust** für `.mphys`-Zero-Copy-I/O, SPMC-Streaming & `wgpu + egui`-Cockpit, **Python** für Orchestrierung, CAD-Pipeline & Plugin-Specs), bei dem OpenFOAM als mathematische Orientierung & Drop-in-Validierungsreferenz dient.  
> **Normative Dokumente:** **[`AGENTS.md`](AGENTS.md)** · **[`INVARIANTS.md`](INVARIANTS.md)** · **[`CONTRACTS.md`](CONTRACTS.md)** · **[`ARCHITECTURE.md`](ARCHITECTURE.md)** · **[`SUGGESTIONS_BACKEND.md`](SUGGESTIONS_BACKEND.md)** · **[`literature_survey_2026.md`](resources/papers/literature_survey_2026.md)** · **[`docs/BLUEPRINT_AND_AUDIT_SUMMARY.md`](docs/BLUEPRINT_AND_AUDIT_SUMMARY.md)**

---

## Phasen-Übersicht

```mermaid
flowchart LR
    P1["Phase 1: Fundament, CAD &<br/>Prototyp-Kern (Erledigt)"] --> P20["Phase 2.0: Architektur-Härtung,<br/>Pass-Pipeline & High-Scale Mesher<br/>(Sprints 1–3, Erledigt)"]
    P20 --> P25["Phase 2.5: Dual-Mode Coupled/PISO<br/>C++20/CUDA Solver & .mphys Streaming (AKTUELL)"]
    P25 --> P3["Phase 3: Die 13 Physik-Module &<br/>Natives Rust wgpu/egui Cockpit"]
    P3 --> P4["Phase 4: Hybrider Enzyme/NURBS-Adjoint,<br/>SIMP/FOSM TopOpt & ASME V&V 20"]
    P4 --> P5["Phase 5 (Future Horizon): MPI-Cluster<br/>(100M+ Zellen) & Quantum-Ready CFD"]
```

---

## Phase 1: Fundament, CAD-Ingestion & Literatur-/Spickzettel-Synthese (Abgeschlossen)

* [x] **WSL2 & OpenFOAM-Referenzbasis (`OpenFOAM_base/openfoam`):**
  * Lokale Referenzquellen für FVM-Operatoren, Randbedingungen und `checkMesh`-Kreuzvalidierung.
* [x] **CAD & Geometry Ingestion Engine (`backend/geometry/`):**
  * Unterstützung für ISO-10303 STEP (AP203/214/242 via Gmsh/OCCT & B-Rep-Parser), Binary/ASCII STL, Wavefront OBJ und Siemens NX `.prt`.
  * Vollständiger Oberflächen- & Topologie-Auditor (Wasserdichtheit, 2-Manifold, Euler-Charakteristik, Normalenorientierung) sowie Ganzdatei-Einheitenerkennung (`mm`, `cm`, `m`, `in`) mit `OCCTargetUnit`-Kopplung und Dreiecksbudget-Guard (`AUD-005`).
* [x] **Wissenschaftliches & Algorithmisches Fundament ([`literature_survey_2026.md`](resources/papers/literature_survey_2026.md) & [`INDEX.md`](resources/reference_spickzettel/INDEX.md)):**
  * Vollständige mathematische Auswertung von 23 Spezialkapiteln + struktur-erhaltenden Operatoren (*DiFVM*, *VoroCrust*, *Mosaic/PUMA*, *fTetWild*, *WSRD*, *Stromberg 23 Super-Elements*, *Mangani/Darwish $4\times 4$ MRF-Coupled*, *El Ouafa Two-Phase PCD*, *Klaij-Vuik Krylov/AL-SIMPLE*, *CPR*, *Yu/Müller STEP-NURBS-Adjoint*, *OPMIS SPH-FVM*, *Mimetischer Hodge-Stern* & *Port-Hamiltonsche Kopplung*) inkl. 30 kuratierter C++20/CUDA/Rust Gold-Snippets.
* [x] **Erster nativer C++20/OpenMP Octree-Hexcore & DiFVM-Prototyp (`backend/meshing/native/`):**
  * Grundlegende SoA-Topologie, Gauss-Schluss auf Maschinenpräzision ($\le 10^{-15}$), LDU-Ordnung, `MeshErrorMap`-Felder und `constant/polyMesh`-Export.

---

## Phase 2.0: Architektur-Härtung, Pass-Pipeline & High-Scale Mesher Blueprint (Sprints 1–3 — Fokus Jetzt)

*(Gemäß [`SUGGESTIONS_BACKEND.md` §16](SUGGESTIONS_BACKEND.md) und [`docs/BLUEPRINT_AND_AUDIT_SUMMARY.md`](docs/BLUEPRINT_AND_AUDIT_SUMMARY.md))*

### Sprint 1 — C-ABI-Vertrag, Agent-DX Guardrails & Kritische P0-Blocker
* [x] **Normatives Agent- & Verifikations-Kit:** [`AGENTS.md`](AGENTS.md), [`INVARIANTS.md`](INVARIANTS.md), [`CONTRACTS.md`](CONTRACTS.md), Suchfilter [`.ignore`](.ignore), Zeilenenden-/Binärregeln [`.gitattributes`](.gitattributes), unabhängiges FVM-Invarianten-Orakel + `mesh_fingerprint` ([`backend/meshing/invariants.py`](backend/meshing/invariants.py)) und Build/Verify-CLI ([`tools/dev.py`](tools/dev.py)).
* [x] **Single-Source C-ABI Schema & Laufzeit-Layout-Handshake (`Blueprint §3.1`, `AUD-037`):** [`abi/cfd_abi.toml`](abi/cfd_abi.toml) + [`tools/abi_gen.py`](tools/abi_gen.py) + `cfd_abi_layout_json()` / `cfd_mesher_last_error()` prüfen `abi_version`, `sizeof` und alle Feld-Offsets beim Laden der DLL.
* [x] **Kritische P0-Sofortfixes (`7/7` L0-Regressionstests grün in [`tests/test_sprint1_audit_and_invariants.py`](tests/test_sprint1_audit_and_invariants.py)):**
  * `AUD-001` & `AUD-032`: Use-after-move der Rand-Patchnamen behoben (`inlet`, `outlet`, `sides`, `top`, `bottom`, `wall` vollständig erhalten).
  * `AUD-003`: Eindeutiges `farfield_bbox`-Layout `[xmin, ymin, zmin, xmax, ymax, zmax]` ohne Rate-Heuristik.
  * `AUD-004`, `AUD-014` & `AUD-025`: CHT-Zonenverschachtelung und Zell-Regionszuordnung über **Generalized Winding Number** statt `cKDTree`; `coupledWall_solid` / `coupledWall_fluid` korrekt zugeordnet.
  * `AUD-005`, `AUD-007`, `AUD-008`, `AUD-015`, `AUD-039`: STEP-Einheiten > 64 kB + Gmsh-Dreiecksbudget, C-ABI Bounds-/NaN-Validierung gegen SEGV/OOB, expliziter `domain_mode`-Vorrang und OpenFOAM-kompatibles Binärformat (`arch` + `<N>\n(`).
* [x] **Test-Hygiene & Asset-Entkopplung (`AUD-026`, `Blueprint §3.2`):** Entfernen sämtlicher manueller `mesh.boundaries`-Überschreibungen in E2E-Tests, sauberes `pytest.skip` bei fehlenden externen Groß-Assets (`CFD_EXTERNAL_ASSETS`), CAD-Tessellierungs-Plattencache (`.cache/cad/*.npz`) und harte Zeit-/RAM-Budgets je Testebene (`L0`–`L4`).

### Sprint 2 — Pass-Zerlegung, $\mathcal{O}(h^2)$ Cut-Cell/Snapping & High-Scale-Fundament
* [x] **Bit-identische Monolith-Zerlegung von `octree_hexcore.cpp` + Python-Monolithen (`Sprint 2A` & `2C`, `Blueprint §2.1`):**
  * Aufteilen von `octree_hexcore.cpp` (2.432 Z.) in 9 Pass-Module (`backend/meshing/native/src/passes/*.cpp`, < 575 Z./Datei) sowie Zerlegung von `models.py`, `difvm_topology.py`, `mesh_engine.py` und `geometry_sdf.py` in `< 660`-Zeilen-Submodule mit Re-Export-Fassaden (`AGENTS.md` Regel 6).
* [x] **Wand-Snapping-Sanierung & QEF-Feature-Snap (`Sprint 2B` + `2C`, `AUD-002`, `Blueprint §1.2.3`):**
  * Strikte Vorzeichenaktivierung + simultanes Snapping mit gedämpfter lokaler Relaxation auf `wall_incident_cells` ($\mathcal{O}(n_{\text{wall}})$) + Polyeder-Promotion (Kugelflächenfehler `-1,1 %`, `100 %` gesnappt) + 3×3-Jacobi-QEF-Feature-Snap für scharfe Kanten/Ecken ($\theta > 35^\circ$).
* [x] **Speicher-, CSR- & Parallelisierungs-Umbau (`Sprint 2C`, `AUD-006`, `AUD-017`, `AUD-018`, `AUD-019`, `AUD-028`, `AUD-040`, `AUD-041`):**
  * `PatchId : uint8_t` (keine `std::string` je Fläche), flaches SoA/CSR-`FaceStore` & `cell_face_offsets`/`cell_face_ids`, parallele Sort+`lower_bound`-Vertex-Deduplizierung ohne `vmap`, sofortiges Freigeben transienter Puffer, `CSRSequence` im `UnstructuredMesh` ohne Python-Listen, Konvergenzcodes `-13`/`-14`, strikte Zyklus-Validierung (`AUD-028`) und kalibrierter RAM-Guard (`avail/1200`). *Gemessen auf `1,39M` Zellen (`5,98M` Flächen): **7,72 s** bei `OMP_NUM_THREADS=10`, **3,76×** End-to-End-Speedup (`1T -> 10T` auf 357k Zellen) und linear `~2,38 kB/Zelle` Peak-RSS-Delta (inkl. Python-SoA-Kopie; finaler Schritt auf $\le 600\text{ B/Zelle}$ folgt in Sprint 3 via 16-B Morton + `MeshHandle`).*
* [x] **Skalen-/Translationsinvarianz, Orientierung & OpenFOAM-Templates (`Sprint 2D`, `AUD-009`, `AUD-010`, `AUD-014`, `AUD-016`, `AUD-020`, `AUD-022`, `AUD-029`, `AUD-031`, `AUD-035`):**
  * Relative `h_min`/`h_max`-Skalierung (`1e-3..1e3 m`), automatische Normalisierung geschlossener/invertierter Schalen (`AUD-010`), `strict_native=True` als Standard (`AUD-016`), `split_regions` mit `mappedWall` (`nearestPatchFace`) + `export_multiregion_openfoam_case`, lauffähige `simpleFoam`/`chtMultiRegionFoam`-Templates (`CaseGenerator`, verifiziert via `FoamReader` & Strukturgates; lokale WSL2-Instanz ohne OpenFOAM-Binaries), lineare `STORE_VAR`/`LOAD_VAR`-`expression_engine` (`AUD-035`) und IPC-`geometry_id`-Prüfung (`AUD-020`). Verifiziert über [`test_sprint2_scale_and_hardening.py`](tests/test_sprint2_scale_and_hardening.py) (`L0 21 passed in 27.3s`, `L1 190 passed in 118.4s`).

### Sprint 3 — Konforme Strategie-C Grenzschicht, `FeatureGraph`, Linearer Morton-Octree & Plugin-System
* [x] **Skalenbewusster `FeatureGraph` & Weingarten-Krümmung (`Blueprint §1.2.1–1.2.2`, `AUD-033`):**
  * Gefensterter Normalensprung $\Theta_w(e)$ ($w = 2h_{\min}$), Canny-Hysterese, `smooth_patch`-Flood-Fill ([`feature_graph.py`](backend/meshing/feature_graph.py)), Cazals/Pouget Weingarten-Jet-Fit $\mathbf{W}=\mathbf{I}^{-1}\mathbf{II}$ innerhalb glatter Gebiete mit 20-B-CCBC-Export ([`weingarten.py`](backend/meshing/weingarten.py)) sowie `CurvatureMethod.WEINGARTEN` ([`_sdf_sizing.py`](backend/meshing/_sdf_sizing.py)).
* [x] **Konforme 3D-Grenzschicht nach Strategie C (`Blueprint §1.1`, `AUD-011`–`013`):**
  * `bl_prep_thickness_field` ($t_{\text{reach}}$, Lipschitz-geglättetes $\delta$), `solve_strand_growth_rates` mit `bl_scale`-Feld, `generate_conformal_bl_strategy_c` / `generate_offset_bl_mesh` ([`bl_strategy_c.py`](backend/meshing/bl_strategy_c.py)) mit **quadratischer Prismen-Jacobi-Schranke** (`compute_prism_jacobian_bound`, `Snippet 25`), konformem LDU-Stitching (`assert_fvm_invariants` bestanden) und Hinterkanten-Keilerkennung; dazu gekoppeltes **2D-`faMesh`** mit exakter Dreiecks-Abwicklungs-Geodäten ([`famesh_geodesic.py`](backend/meshing/famesh_geodesic.py)).
* [x] **Topologisches `ZoneDetect` mit Strahl-Blockierung & Baffles (`Blueprint §1.3`):**
  * Natives `cfd_sdf_segment_blocked` (`TriangleAABBTree::segment_intersects` in [`sdf_engine.cpp`](backend/meshing/native/src/sdf_engine.cpp)) + [`zone_detect.py`](backend/meshing/zone_detect.py) sperren Leckpfade durch dünne Wände ($< h_0$) zuverlässig und berechnen $R_{\text{th}} = \tau/\lambda$ inkl. optionalem `extract_cht_twin_faces`-Stitching.
* [x] **16-Byte Linearer Morton-Octree, `regular_faces`/`general_faces`-Split & Deklarative `ModuleSpec`-Plugins (`Blueprint §1.4.1–1.4.2`, `§2.2`, `AUD-021`, `AUD-036`, `AUD-038`):**
  * `Leaf` (`sizeof == 16`) & `RegularFace` (`sizeof == 12`) in [`linear_octree.hpp`](backend/meshing/native/include/linear_octree.hpp) / [`linear_octree.cpp`](backend/meshing/native/src/linear_octree.cpp) mit lock-freier 4096er-Suche & 8-Bit-Lattice-Besitzregel: **218,0 B/Zelle** bei **6,12M Zellen** (`13,37 s`, 10T) bzw. **110,7 B/Zelle** & **7,56× Speedup** (`1T -> 10T` auf 320k Zellen).
  * Zero-Copy `mmap` `MeshHandle` für 65.536er `.mphys`-Chunks ([`mesh_handle.py`](backend/native_rust/mesh_handle.py)), Subprocess-IPC-Isolation mit Hart-Cancel & RAM-Watchdog ([`async_jobs.py`](backend/ipc/async_jobs.py)) und generische `Registry[T]` (`cfdsolver.*`, `ModuleSpec`, `BoundaryConditionSpec`, `MaterialLaw`-`d_dT`-Parität in [`plugin_registry.py`](backend/physics_modules/plugin_registry.py)).
  * **WSL2 OpenFOAM v2606 Live-Gate (`openfoam2606` in `Ubuntu-26.04`):** `checkMesh`, `simpleFoam` (2 Iterationen) und `checkMesh -allRegions` (`Mesh OK.` auf Fluid + Solid) in Real-OpenFOAM verifiziert; verifiziert via [`test_sprint3_features_and_scale.py`](tests/test_sprint3_features_and_scale.py) (`L0 28 passed in 48.08s`, `L1 197 passed in 181.15s`).

---

## Phase 2.5: Dual-Mode Matrix-Free / Block-Coupled C++20/CUDA Solver & `.mphys` Streaming

*(Gemäß [`literature_survey_2026.md` §9.18–9.23, Kap. 10](resources/papers/literature_survey_2026.md) und [`SUGGESTIONS_BACKEND.md` §13–15](SUGGESTIONS_BACKEND.md))*

* [ ] **Primärer Standard — Matrix-Free / Block-Coupled Newton-Krylov Solver (`backend/solver/native/`):**
  * **Enzyme Forward-AD JFNK (`FGMRES` / `BiCGStab(2)`):** Matrixfreies $\mathcal{J}\mathbf{v} = \frac{\partial\mathbf{R}}{\partial\mathbf{U}}\mathbf{v}$ in $\mathcal{O}(N)$ VRAM (50–65 % weniger Speicher als voll assembliertes `Block-CSR`) mit unterrelaxations- und zeitschritt-konsistentem Rhie-Chow (*Moukalled, Mangani & Darwish*, `Snippet 30`).
  * **Modulare Rechts-Vorkonditionierer $\mathcal{P}^{-1}$:**
    1. *Krylov-SIMPLE & Augmented-Lagrangian (`AL-SIMPLE`, Klaij & Vuik):* Beseitigt Gitter- und $1000:1$-Grenzschicht-Stagnation.
    2. *$4\times 4$ MRF/AMI Block-Coupled & Two-Phase `PCD` (*Mangani/Darwish* & *El Ouafa*, `Snippets 21–22`):* Für Turbomaschinen und Dichte-/Viskositätssprünge bis $10^6$.
    3. *$5\times 5$ Pressure-Based All-Mach Block $(u,v,w,p,T)$:* Nahtlos von $\text{Ma} = 10^{-4}$ bis Überschall & Kavitation.
    4. *Zweistufiger `CPR` (Constrained Pressure Residual, `Snippet 30`):* 1 skalarer Druck-AMG-V-Zyklus + 1 zelllokaler GPU-Block-Jacobi/BILU(0)-Sweep für steife Mehrspezies-Chemie ($M = 10\text{–}50$ DOFs/Zelle).
* [ ] **Leichtgewichts-Modus — Transientes Segregiertes PISO / Fractional-Step ($\text{CFL} \le 1$):**
  * Mit Multi-Pass Rhie-Chow Re-Evaluation (`Snippet 15`), Kinetische-Energie-erhaltender (KEP) Konvektion und `GCRO-DR` Krylov-Subraum-Deflation für WALE-LES, FWH-Akustik und OPMIS SPH-FVM.
* [ ] **Mixed-Precision AMG (`FP32` innen / `FP64` außen) + Spektrale Graph-Sparsifikation & `AA(m)`:**
  * Normierte Defekt-Skalierung gegen `FP32`-Underflow, Operator-Complexity $\sigma_{\text{op}} \le 1{,}35$ auf Polyedergittern und Anderson Acceleration `AA(5)` (`Snippet 24`).
* [ ] **Rust High-Speed I/O (`foam_fast_io` mit `catch_unwind` & `i64`-Parität, `AUD-034`) + `CFDFLD03` In-Situ SPMC-Ring (`SUGGESTIONS_BACKEND.md` §13.3):**
  * Lock-freier Shared-Memory Triple-Buffer (Seqlock + Refcount) und 5 native In-Situ-Extraktoren (Polyeder-Slicer, Cardano-$\lambda_2$/$Q$, $\mathcal{O}(1)$ Face-Exit-Streamlines, 3-Ring-WSS, Echtzeit-Randintegrale) für 60–120 FPS ohne Disk-I/O.

---

## Phase 3: Die 13 Multiphysik-Module & Natives Rust `wgpu + egui` CAE-Cockpit

*(Deklarativ spezifiziert über `ModuleSpec` + Port-Hamiltonsche Dirac-Leistungs-Ports $(e_\alpha, f_\alpha)$ mit $\dim(e\cdot f) = [\text{W}]$, `Snippet 23`)*

* [ ] **Cluster A — Thermal, Turbulence, Porous, Radiation & Reacting Flows (`Snippets 16, 24, 29`):**
  * **Turbulence & All-$y^+$ Walls:** $k\text{-}\omega$ SST (Patankar-positiv), `SA-noft2`, WALE-LES ($\operatorname{tr}(\mathbf{g}^2)$), Spalding-Newton-$u_\tau$ & Jayatilleke-$P(Pr,Pr_t)$ inkl. Krümmungskorrektur (CCBC).
  * **Multi-Region CHT:** Monolithische harmonische Grenzflächenkopplung + 3-Schritt Aitken-$\Delta^2$ Robin-Robin-Kopplung.
  * **Species & Stiff Chemistry:** AoT Sparse-Chemie-Codegen (`generate_mechanism.py` mit strikter Massenerhaltung $\sum_i W_i\nu_{ir}=0$), Modified Patankar (MPRK) & 32-Zellen GPU-Shared-Memory Work-Stealing.
  * **Porous Media & Radiation:** Anisotropes Darcy-Forchheimer mit exakter analytischer Rang-1-Tangentenmatrix + P1-Marshak & $S_8/S_{16}$ Gauss-Legendre `fvDOM` mit Spiegel-Reflexion.
* [ ] **Cluster B — Compressible Real-Gas, Cavitation, OPMIS SPH-FVM & Wall-Film (`Snippet 27`):**
  * **All-Mach & Real-Gas:** Newton-freie quadratische Peng-Robinson-EOS in $w = \sqrt{T}$, All-Mach HLLC / AUSM+-up & Schnerr-Sauer-Kavitation.
  * **Hybrid SPH-FVM (`OPMIS`) & Thin Film (`faMesh`):** Wendland-$C^2$ MLS-Renormierung mit gekrümmter Geisterwand-Vervollständigung, Lagrange-Spray & 2D-Wandfilm mit Bai-Gosman 4-Regime-Tropfenaufprall (`Stick/Rebound/Spread/Splash`).
* [ ] **Cluster C — Battery Runaway, FWH Aeroacoustics, MHD, TPS Ablation & Monolithic FSI (`Snippets 23, 28`):**
  * **Battery Abuse:** 2-RC-Thevenin-ECM + 4-Stufen Hatchard-Spotnitz Thermal Runaway mit SEI-Tunnelbarriere $\exp(-t_{\text{SEI}}/t_{\text{SEI},0})$ und $5\times 5$ `expm_phi1`/`Ros2`-Integrator.
  * **Aeroacoustics, MHD & TPS:** Farassat 1A mit bewegten/permeablen Flächen ($\tau^*$-Bisektion), Ni et al. stromerhaltende 4-Schritt-MHD ($Ha = 1\dots 100+$), poröse PICA-Pyrolyse/Ablation sowie exakte **3D Swept-Volume Simpson-BDF2-DGCL-ALE** (`triangle_sweep` + `enforce_dgcl`, `AUD-024`).
* [ ] **Natives Rust Desktop-Cockpit (`frontend/`, `wgpu + egui`, [`SUGGESTIONS_FRONTEND.md`](SUGGESTIONS_FRONTEND.md)):**
  * Zero-Sync Optimistic State Store, 7-Exponenten-SI-Einheitenprüfung im UI, topologie-unabhängige räumliche BC-Selektor-DSL, GPU-Clipping/Isoflächen/LIC-Shader und Git-freundliche Trennung von Projekt-Intent (`project.toml`) und Binär-Cache (`cache/*.mphys`).

---

## Phase 4: Hybride Adjoint-Optimierung (CAD/NURBS + SIMP/FOSM) & ASME V&V 20

* [ ] **ASME V&V 20 Verifikations-Engine (`Snippets 26`, `Blueprint §3.4 Waffe 3`):**
  * Automatisches 3-Gitter-MMS-Orakel ($p_{\text{obs}} \ge 1{,}95$ auf verzerrten 3D-Polyedergittern durch implizite WLS-Gradienten-Einbettung $\mathbf{L}_{c,j}$ in $\mathbf{A}$) sowie Complex-Step- ($h_{\text{cs}} = 10^{-30}$) und Taylor-Restglied-Gate ($\mathcal{O}(\epsilon^2)$) für jeden Adjoint-Pfad.
* [ ] **Hybride Reverse-FVM / Forward-CAD & STEP-NURBS Formoptimierung (`Snippet 22`, `literature_survey_2026.md` §9.17 & §9.21):**
  * Diskreter IFT-Adjoint $\mathcal{J}^T\boldsymbol{\psi} = -\nabla_{\mathbf{U}} L$ (inkl. Rhie-Chow-Mobilitätsableitung $\partial D_f/\partial\gamma$, `Snippet 17`) + Revolve-Checkpointing, direkt gekoppelt mit parametrischem `differentiable_cad.py`-DAG *und* direkter lokaler STEP-NURBS-Kontrollpunkt-Kontraktion ($\partial\mathbf{X}_s/\partial\mathbf{P}_{i,j} = R_{i,j}(u,v)\mathbf{I}_3$ unter Farin-$G^1/C^2$-Kollinearität).
* [ ] **Thermo-Fluid SIMP/RAMP, Level-Set & Robuste FOSM-Optimierung (`Snippet 4–5`):**
  * Brinkman/RAMP-Interpolation, Kreisselmeier-Steinhauser-/Tropische Log-Sum-Exp-Aggregation, Svanberg-MMA, Hamilton-Jacobi-Level-Set und FOSM-Unsicherheitsquantifizierung ($\Phi_g = \mu_g + \kappa\sigma_g$), kombiniert mit globalem PSO/MIGA + lokalem L-BFGS Warm-Start.

---

## Phase 5 (Future Horizon): Verteilter MPI-Cluster (100M+ Zellen), In-Situ AI & Quantum-Ready CFD

* [ ] **MPI-Stufe 2 & Remote-Chunk-Streaming (`Blueprint §1.4.6`):**
  * Verteilter Morton-SFC-Octree (`find_leaf` mit Halo-Austausch & `MPI_Exscan`), progressives `.mphys`-Chunk-Streaming über WebSocket/QUIC mit XOR-Delta + LZ4/ZFP und SPMD Zero-Copy C++-ML-Kopplung (`ONNX Runtime` / `LibTorch` nach dem RWTH `AIxeleratorService`-Prinzip, $2\,\text{ms}$ Overhead).
* [ ] **Quantum-Ready CFD Research Bridge:**
  * Block-Encoding-Export symmetrischer FVM-Operatoren für VQLS/HHL, QUBO/QAOA für diskrete Topologieoptimierung und Madelung-/QLBM-Forschungsmodule.
