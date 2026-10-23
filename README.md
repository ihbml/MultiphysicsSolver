# CFDSolver Platform
> **Eigenständige, hochperformante „Optimization-First“ Multiphysics CFD- & Topologieoptimierungs-Plattform**  
> *Angetrieben durch Google-Style C++, Modern Fortran (`iso_c_binding`), Rust (In-Memory I/O & Streaming) und Trame + PyVista (WebGL 60 FPS) — mit OpenFOAM als mathematischer Blaupause.*

---

## 1. Vision & Strategisches Leitbild

Die **CFDSolver Platform** ist ein eigenständiger, modularer Multiphysics-Solver, der von Tag 1 an für **Topologieoptimierung (SIMP), Gradient Descent (Adjoint / Auto-Diff), Particle Swarm Optimization (PSO) und Monte-Carlo-Simulationen** entworfen wurde.

### Warum ein eigenständiger Kern (mit OpenFOAM als Orientierung)?
1. **In-Memory Zero-Disk Architektur:**  
   Klassische Solver wie OpenFOAM erzwingen bei jedem Lauf Dateisystem-Zugriffe (`0/`, `constant/`, `system/`). Unser Solver hält Gitter, Felder und Matrizen permanent im RAM – ideal für tausende PSO-/Monte-Carlo-Varianten mit **Warm-Start (0 ms I/O-Overhead)**.
2. **Differenzierbare Physik & SIMP Topologieoptimierung ohne Remeshing:**  
   Jede Gitterzelle besitzt von Haus aus eine kontinuierliche Designvariable $\gamma_i \in [0, 1]$ (Fluid $\leftrightarrow$ Festkörper via Brinkman-Penalisierung). Durch klare, makrolose C++- und Fortran-Rechenkerne lassen sich exakte Gradienten ($\frac{dJ}{d\gamma_i}$) für **Gradient Descent / L-BFGS / MMA** berechnen.
3. **Tri-Language HPC Performance** (nach den Richtlinien aus `Sources/ProgrammingLanguage`):
   - **C++ (Google C++ Style Guide):** Unstrukturierte Gittertopologie, *Structure of Arrays (SoA)* Datenlayout, FVM-Matrix-Assembly ohne OpenFOAM-Makro-Ballast.
   - **Modern Fortran (Fortran 2018/2023 `iso_c_binding`):** Überlappungsfreie, 100 % SIMD-vektorisierte Rechenkerne für Flüsse, steife Chemie-ODEs, Strahlung, Thermodynamik und Gleichungslöser.
   - **Rust (Rust API Guidelines):** Speichersicheres High-Speed File-I/O (`memmap2`), Multithreaded Batch-Orchestrierung (`rayon`) und Live-WebSocket-Streaming ans Cockpit.
   - **Python (Trame + PyVista):** Interaktives 60-FPS 3D-Web-Cockpit im Browser.
4. **Rolle von `OpenFOAM_base/`:**  
   Dient als **mathematischer Spickzettel** für bewährte Diskretisierungen und als **ASME V&V 20 Referenz-Benchmark**.

👉 **Alle Details zur Architektur stehen in [ARCHITECTURE.md](file:///G:/Datein/Projects/CFDSolver/ARCHITECTURE.md).**

---

## 2. Repository-Architektur

```text
CFDSolver/
├── ARCHITECTURE.md              # Verbindlicher Architektur- & Sprach-Blueprint (C++/Fortran/Rust + SIMP)
├── ROADMAP.md                   # 4-Phasen Entwicklungsplan inkl. Topologie- & Schwarmoptimierung
├── PROJECT.md                   # Spezifikation & Status der CAD & Geometry Ingestion Engine
│
├── backend/                     # Eigenständige HPC Berechnungs- & Optimierungs-Engine
│   ├── geometry/                # CAD & Geometry Ingestion (STEP AP203/214/242, STL, OBJ, NX .prt)
│   ├── native_cpp/              # Google-Style C++ FVM-Gerüst & unstrukturierte Gittertopologie (SoA)
│   ├── native_fortran/          # Modern Fortran Numerik-, Physik- & Adjoint-Kerne (iso_c_binding)
│   ├── native_rust/             # Rust In-Memory Pool, Fast-I/O (memmap2) & PSO/Monte-Carlo Runner
│   ├── optimization/            # SIMP Topologieoptimierung, Gradient Descent (MMA/L-BFGS), PSO, Monte Carlo
│   ├── physics_modules/         # Die 12 gekapselten Multiphysik-Module
│   ├── core/                    # Case-Konfiguration & Cross-Validation Exporter
│   ├── runner/                  # Solver-Execution & Benchmark-Brücke zu WSL2/OpenFOAM
│   └── OPENFOAM_BACKEND_ATLAS.md# Nachschlagewerk der OpenFOAM-Mathematik & Referenzstrukturen
│
├── frontend/                    # Interaktives 3D-Cockpit & GUI
│   ├── cockpit/                 # Trame Web-Applikation (app.py) mit WebGL/VTK.js
│   ├── components/              # Reaktive Vuetify-Widgets (Fluent-Style Model & Optimization Panel)
│   └── requirements.txt         # Python-Abhängigkeiten (trame, pyvista, vtk)
│
├── resources/                   # Wissenschaftliches Fundament & Test-Assets
│   ├── papers/                  # Literatur-Synthese 2024–2026 (literature_survey_2026.md)
│   ├── CADTestSamples/          # Reale CAD-Prüfkörper (.prt, .stp, .stl, .obj, .igs)
│   ├── validation_benchmarks/   # ASME V&V 20 Referenzfälle
│   └── materials_db/            # Thermophysikalische Stoffwerte, NASA-Polynome, Kinetiken
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

## 3. Die 12 Multiphysik-Module & Optimierungs-Verfahren

### A. Multiphysik-Module (`backend/physics_modules/`):
1. **Energy & Heat (CHT):** Thermische Enthalpie, gekoppelte Fluid-Festkörper-Wärmeleitung.
2. **Viscous & Turbulence:** Laminar, RANS ($k-\omega$ SST, Spalart-Allmaras), LES (WALE), DES.
3. **Multiphase:** Volume of Fluid (VOF), Euler-Euler Zweiphasenströmung, Kavitation.
4. **Species & Chemistry:** Mehrspezies-Transport, Finite-Rate-Kinetik (Cantera/Chemkin), Wandkatalyse.
5. **Discrete Phase (DPM):** Lagrange-Partikeltracking, Spray-Breakup, Bai-Gosman Wandkollision.
6. **Eulerian Wall Film (EWF):** 2D-Flüssigkeitsfilme auf Wänden, Verdampfung & Kondensation.
7. **Radiation:** Diskrete Ordinaten (`fvDOM`), P1, Einstrahlzahlen.
8. **Acoustics:** Aeroakustik nach Ffowcs Williams-Hawkings (FWH) & Curle.
9. **Ablation & TPS:** Oberflächenrezession, Pyrolyse & Darcy-Brinkman-Stokes Hitzeschutz.
10. **Battery Simulation:** Elektrochemisch-thermisches 2-RC Equivalent Circuit Model & Thermal Runaway.
11. **Magnetohydrodynamics (MHD):** Lorentz-Kraft ($\mathbf{J} \times \mathbf{B}$), elektrische Potentialgleichung.
12. **Porous Media & Heat Exchanger:** Darcy-Forchheimer, direkt verknüpft mit dem SIMP-Designfeld $\gamma_i$.

### B. Optimierungs- & Stochastik-Verfahren (`backend/optimization/`):
* **SIMP / Brinkman Topologieoptimierung:** Freiform-Optimierung von Strömungskanälen und Kühlstrukturen über das kontinuierliche Zellfeld $\gamma_i \in [0, 1]$ ohne Neuvernetzung.
* **Gradient Descent / L-BFGS / MMA:** Hochdimensionale Optimierung mittels Adjoint-Gleichungen und automatischer Differenzierung.
* **Particle Swarm Optimization (PSO):** Globale, gradientenfreie Schwarmoptimierung im RAM.
* **Monte Carlo & Uncertainty Quantification (UQ):** Tausende schnelle In-Memory-Durchläufe für Toleranz- und Robustheitsanalysen.
