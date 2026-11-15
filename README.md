# CFDSolver Platform
> **Modulare CAD-Ingestion, Unstrukturierte Vernetzungs- & OpenFOAM-Case-Export-Plattform (Prototyp)**  
> *Fokus auf CAD-Import (STEP, STL, OBJ, NX), unstrukturierte Vernetzung (Cut-Cell / Octree Hexcore) und OpenFOAM-Case-Export. Standalone In-Memory FVM-Gleichungslöser, Fortran-Rechenkerne und Optimierungsmodule stellen die geplante Zielarchitektur dar.*

---

## 1. Vision & Strategisches Leitbild

> **Aktueller Entwicklungsstand:** Funktionaler Prototyp für CAD-Import (STEP, STL, OBJ), unstrukturierte Vernetzung (Cut-Cell / Octree Hexcore) und OpenFOAM-Case-/Gitter-Generierung. Standalone In-Memory FVM-Gleichungslöser, Fortran-Rechenkerne und Optimierungsmodule (SIMP / Adjoint / PSO) stellen die geplante Zielarchitektur dar.

Die **CFDSolver Platform** ist ein modularer Multiphysics-Solver-Prototyp, der konzeptionell für **Topologieoptimierung (SIMP), Gradient Descent (Adjoint / Auto-Diff), Particle Swarm Optimization (PSO) und Monte-Carlo-Simulationen** entworfen wird.

### Warum ein eigenständiger Kern (mit OpenFOAM als Orientierung)?
1. **In-Memory Zero-Disk Architektur (Geplante Zielarchitektur):**  
   Klassische Solver wie OpenFOAM erzwingen bei jedem Lauf Dateisystem-Zugriffe (`0/`, `constant/`, `system/`). Unser Ziel ist es, Gitter, Felder und Matrizen permanent im RAM zu halten – ideal für tausende PSO-/Monte-Carlo-Varianten mit **Warm-Start (0 ms I/O-Overhead)**.
2. **Differenzierbare Physik & SIMP Topologieoptimierung ohne Remeshing (Geplant):**  
   Jede Gitterzelle soll eine kontinuierliche Designvariable $\gamma_i \in [0, 1]$ besitzen (Fluid $\leftrightarrow$ Festkörper via Brinkman-Penalisierung). Durch klare, makrolose C++- und Fortran-Rechenkerne lassen sich exakte Gradienten ($\frac{dJ}{d\gamma_i}$) für **Gradient Descent / L-BFGS / MMA** berechnen.
3. **Tri-Language HPC Performance (Zielarchitektur)**:
   - **C++ (Google C++ Style Guide):** Unstrukturierte Gittertopologie, *Structure of Arrays (SoA)* Datenlayout, FVM-Matrix-Assembly ohne OpenFOAM-Makro-Ballast.
   - **Modern Fortran (Fortran 2018/2023 `iso_c_binding`):** Überlappungsfreie, 100 % SIMD-vektorisierte Rechenkerne für Flüsse, steife Chemie-ODEs, Strahlung, Thermodynamik und Gleichungslöser (in Entwicklung).
   - **Rust (Rust API Guidelines):** Speichersicheres High-Speed File-I/O (`memmap2`) und OpenFOAM-Gitter-Reader.
   - **Python (Trame + PyVista):** Interaktives 3D-Web-Cockpit im Browser.
4. **Rolle von `OpenFOAM_base/`:**  
   Dient als **mathematischer Spickzettel** für bewährte Diskretisierungen und als **ASME V&V 20 Referenz-Benchmark**.

👉 **Alle Details zur Architektur stehen in [ARCHITECTURE.md](ARCHITECTURE.md).**

---

## 2. Repository-Architektur

```text
CFDSolver/
├── ARCHITECTURE.md              # Verbindlicher Architektur- & Sprach-Blueprint (C++/Fortran/Rust + SIMP)
├── ROADMAP.md                   # 4-Phasen Entwicklungsplan inkl. Topologie- & Schwarmoptimierung
├── PROJECT.md                   # Spezifikation & Status der CAD & Geometry Ingestion Engine
│
├── backend/                     # Berechnungs-, Vernetzungs- & OpenFOAM-Case-Export-Engine
│   ├── geometry/                # CAD & Geometry Ingestion (STEP AP203/214/242, STL, OBJ, NX .prt)
│   ├── native_cpp/              # C++ Meshing (Octree Hexcore, DiFVM Topologie, SDF Engine)
│   ├── native_fortran/          # Modern Fortran Numerik- & Physik-Kerne (geplant / in Entwicklung)
│   ├── native_rust/             # Rust OpenFOAM-Reader (In-Memory ASCII PolyMesh Reader)
│   ├── optimization/            # SIMP Topologieoptimierung, Gradient Descent, PSO (geplant / in Entwicklung)
│   ├── physics_modules/         # 12 gekapselte OpenFOAM Dictionary- & Case-Vorlagen-Generatoren
│   ├── core/                    # Case-Konfiguration & OpenFOAM-Export
│   ├── runner/                  # OpenFOAM Execution-Runner & WSL-Brücke
│   └── OPENFOAM_BACKEND_ATLAS.md# Nachschlagewerk der OpenFOAM-Mathematik & Referenzstrukturen
│
├── frontend/                    # 3D-Cockpit & GUI Prototyp
│   ├── cockpit/                 # Trame Web-Applikation (app.py) mit WebGL/VTK.js
│   ├── components/              # Reaktive Vuetify-Widgets (geplant)
│   └── requirements.txt         # Python-Abhängigkeiten (trame, pyvista, vtk)
│
├── resources/                   # Wissenschaftliches Fundament & Test-Assets
│   ├── papers/                  # Literatur-Synthese 2024–2026 (literature_survey_2026.md)
│   ├── CADTestSamples/          # Reale CAD-Prüfkörper (.prt, .stp, .stl, .obj)
│   ├── validation_benchmarks/   # ASME V&V 20 Referenzfälle (geplant)
│   └── materials_db/            # Thermophysikalische Stoffwerte (geplant)
│
├── OpenFOAM_base/               # Upstream OpenFOAM-Quellcode (Mathematik-Orientierung & Benchmark)
│   └── openfoam/
│
├── COMMUNICATION_BACKEND_FRONTEND.md   # Übergabeprotokoll Backend <-> Frontend
├── FRONTEND_REQUIREMENTS_ON_BACKEND.md # Parameter- & Input-Spezifikationen
├── SUGGESTIONS_BACKEND.md              # Ideen & HPC-Tuning für das Backend
└── SUGGESTIONS_FRONTEND.md             # UI/UX & Visualisierungs-Ideen für das Frontend
```

---

## 3. Die 12 Multiphysik-Vorlagengeneratoren & Geplante Optimierungs-Verfahren

### A. Multiphysik-Vorlagengeneratoren (`backend/physics_modules/` - OpenFOAM Dictionary-Export):
1. **Energy & Heat (CHT):** Thermische Enthalpie, gekoppelte Fluid-Festkörper-Wärmeleitung.
2. **Viscous & Turbulence:** Laminar, RANS ($k-\omega$ SST, Spalart-Allmaras), LES (WALE), DES.
3. **Multiphase:** Volume of Fluid (VOF), Euler-Euler Zweiphasenströmung, Kavitation.
4. **Species & Chemistry:** Mehrspezies-Transport, Finite-Rate-Kinetik, Wandkatalyse-Vorlagen.
5. **Discrete Phase (DPM):** Lagrange-Partikeltracking, Spray-Breakup, Wandkollision.
6. **Eulerian Wall Film (EWF):** 2D-Flüssigkeitsfilme auf Wänden, Verdampfung & Kondensation.
7. **Radiation:** Diskrete Ordinaten (`fvDOM`), P1, Einstrahlzahlen.
8. **Acoustics:** Aeroakustik nach Ffowcs Williams-Hawkings (FWH) & Curle.
9. **Ablation & TPS:** Oberflächenrezession, Pyrolyse-Vorlagen.
10. **Battery Simulation:** Elektrochemisch-thermisches 2-RC Equivalent Circuit Model.
11. **Magnetohydrodynamics (MHD):** Lorentz-Kraft ($\mathbf{J} \times \mathbf{B}$), elektrische Potentialgleichung.
12. **Porous Media & Heat Exchanger:** Darcy-Forchheimer Modellierung.

### B. Geplante Optimierungs- & Stochastik-Verfahren (`backend/optimization/` - Geplante Zielarchitektur):
* **SIMP / Brinkman Topologieoptimierung:** Freiform-Optimierung von Strömungskanälen und Kühlstrukturen über kontinuierliches Zellfeld $\gamma_i \in [0, 1]$. *(Geplant)*
* **Gradient Descent / L-BFGS / MMA:** Hochdimensionale Optimierung mittels Adjoint-Gleichungen. *(Geplant)*
* **Particle Swarm Optimization (PSO):** Globale, gradientenfreie Schwarmoptimierung im RAM. *(Geplant)*
* **Monte Carlo & Uncertainty Quantification (UQ):** In-Memory-Durchläufe für Toleranz- und Robustheitsanalysen. *(Geplant)*
