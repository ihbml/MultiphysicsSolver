# CFDSolver — Backend Engine

Willkommen im **Backend-Bereich** der CFDSolver Platform!

Dieses Verzeichnis enthält die gesamte Berechnungs-, Modellierungs- und Orchestrierungslogik für OpenFOAM-Simulationen.

---

## 📚 Wichtige Dokumente & Nachschlagewerke

1. **[OPENFOAM_BACKEND_ATLAS.md](file:///g:/Datein/Projects/CFDSolver/backend/OPENFOAM_BACKEND_ATLAS.md)**:  
   **Das zentrale Nachschlagewerk!** Enthält den detaillierten Pfad- und Struktur-Atlas der Upstream OpenFOAM-Basis (`OpenFOAM_base/openfoam`), Mappings aller 12 Physik-Module, Dictionaries, FunctionObjects, C++ Bibliotheken und CLI-Befehle.
2. **[COMMUNICATION_BACKEND_FRONTEND.md](file:///g:/Datein/Projects/CFDSolver/COMMUNICATION_BACKEND_FRONTEND.md)**:  
   Schnittstellenvertrag und Datenübergabe zwischen Backend (`ModuleRegistry`, `CaseGenerator`) und Frontend (Trame UI).
3. **[FRONTEND_REQUIREMENTS_ON_BACKEND.md](file:///g:/Datein/Projects/CFDSolver/FRONTEND_REQUIREMENTS_ON_BACKEND.md)**:  
   Spezifikation der benötigten Eingabemasken, Schemata und Schalter für das UI.
4. **[SUGGESTIONS_BACKEND.md](file:///g:/Datein/Projects/CFDSolver/SUGGESTIONS_BACKEND.md)**:  
   Ideenpool für Performance-Tuning (Rust Fast-I/O, Jacobi-Stabilisierung, P/E-Core Tuning, GPU-Kerne).

---

## 📁 Verzeichnis-Struktur

```text
backend/
├── core/
│   └── case_generator.py        # Generiert vollständige OpenFOAM-Fälle (0/, constant/, system/)
├── geometry/                    # Industrielle CAD- & Geometrie-Ingestion Engine
│   ├── pipeline.py              # CADGeometryEngine Fassade (STEP, STL, OBJ, PRT -> OpenFOAM)
│   ├── parsers/                 # B-Rep STEP Tesselierung, cKDTree STL/OBJ Parser
│   ├── bridge/                  # Siemens NX 1926 CLI-Bridge (step214ug) für native .prt
│   ├── audit/                   # 2-Manifold, Watertight, Euler & 10 surfaceCheck Kriterien
│   ├── transformation/          # Invariante Einheiten-Skalierung (mm -> m)
│   └── exporter/                # constant/triSurface Exporter & snappyHexMeshDict Blocks
├── runner/
│   └── wsl_runner.py            # Headless-Simulation runner unter WSL2 (blockMesh, decomposePar, mpirun)
├── native_cpp/
│   └── smr_catalysis/           # C++ fvOptions mit analytischer Jacobi-Stabilisierung (wmake)
├── native_rust/
│   └── foam_fast_io/            # High-Speed Field I/O & Memory-Mapped Parser (PyO3)
├── physics_modules/
│   ├── base_module.py           # Abstrakte Basisklasse BasePhysicsModule
│   ├── registry.py              # Registry aller 12 Physik-Module
│   └── [ablation, acoustics, battery_thermal, dpm_lagrangian, energy_heat,
│        mhd_plasma, multiphase, porous_hex, radiation, species_chem,
│        viscous_turb, wall_film_ewf]
├── OPENFOAM_BACKEND_ATLAS.md    # Detail-Atlas der OpenFOAM-Basis
└── README.md                    # Diese Datei
```
