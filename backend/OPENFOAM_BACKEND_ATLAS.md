# OpenFOAM & Backend Reference Atlas
> **Das zentrale Nachschlagewerk für das CFDSolver-Backend**  
> *Enthält die vollständige Strukturierung der Upstream OpenFOAM-Basis (`OpenFOAM_base/openfoam`), die Mappings zu allen 12 Physik-Modulen, C++ wmake-Bibliotheken, Dictionaries, Tutorials und native Erweiterungen.*

---

## 1. Übersicht & Backend-Architektur

Das Backend verbindet die Finite-Volume-Numerik von OpenFOAM mit Python-Orchestrierung, C++ Matrixstabilisierung und Rust High-Speed I/O.

### Workspace-Pfade & Dual-World Mapping:
* **Windows Host-Pfad:** `G:\Datein\Projects\CFDSolver\`
* **WSL2 Linux-Pfad:** `/mnt/g/Datein/Projects/CFDSolver/`
* **Upstream OpenFOAM-Basis:** `G:\Datein\Projects\CFDSolver\OpenFOAM_base\openfoam\` (OpenCFD Release API `2606`)
* **Backend Source-Tree:** `G:\Datein\Projects\CFDSolver\backend\`

```text
backend/
├── core/                        # Case-Generator, Dictionary-Assembly, Template-Engine
│   └── case_generator.py        # Erstellt system/, constant/, 0/ Verzeichnisse
├── runner/                      # Headless Orchestrierung & MPI-Execution
│   └── wsl_runner.py            # WSL2-Brücke, blockMesh, decomposePar, mpirun
├── native_cpp/                  # Eigene OpenFOAM C++ Shared Libraries (wmake)
│   └── smr_catalysis/           # Analytisch Jacobi-stabilisierte fvOptions für Wandkatalyse
├── native_rust/                 # High-Performance Field-Parser & Zero-Copy I/O
│   └── foam_fast_io/            # PyO3 + memmap2 + rayon für Multi-GB Mesh/Feld-Extraktion
└── physics_modules/             # 12 gekapselte Physik-Module
    ├── base_module.py           # Abstrakte Basisklasse BasePhysicsModule
    ├── registry.py              # Zentrale ModuleRegistry für Fluent-Style UI & Solver-Verknüpfung
    └── [ablation, acoustics, battery_thermal, dpm_lagrangian, energy_heat,
         mhd_plasma, multiphase, porous_hex, radiation, species_chem,
         viscous_turb, wall_film_ewf]
```

---

## 2. Struktur-Atlas: Upstream OpenFOAM Base (`OpenFOAM_base/openfoam/`)

### A. Hauptverzeichnisse im Überblick

| Verzeichnis | Beschreibung & Backend-Relevanz |
| :--- | :--- |
| `applications/solvers/` | Quellcode aller Standard-Solver, kategorisiert nach Strömungsphysik. |
| `applications/utilities/` | Mesh-Generatoren (`blockMesh`, `snappyHexMesh`), Pre- & Post-Processing, Konverter. |
| `src/` | Der gesamte C++ Kern: `finiteVolume`, `TurbulenceModels`, `thermophysicalModels`, etc. |
| `tutorials/` | Fertig konfigurierte Referenzfälle für fast jedes physikalische Phänomen. |
| `etc/caseDicts/` | **Goldgrube für Templates:** Vollständig annotierte Referenz-Dictionaries und FunctionObjects. |
| `etc/templates/` | Komplette Case-Skelette (`0/`, `constant/`, `system/`) für Standard-Strömungsgeometrien. |
| `wmake/` | OpenFOAM Build-System (Rules, Compiler-Flags, Makefiles). |
| `bin/` | Shell-Wrapper und Hilfsskripte (`foamDictionary`, `foamListTimes`, `paraFoam`). |

---

### B. Solver-Katalog (`applications/solvers/`)

| Kategorie | Enthaltene Solver | Primäre Verwendung im Backend |
| :--- | :--- | :--- |
| **`heatTransfer/`** | `chtMultiRegionFoam`, `buoyantPimpleFoam`, `buoyantSimpleFoam`, `solidFoam`, `thermoFoam` | Conjugate Heat Transfer (Gas + Festkörper), natürliche Konvektion |
| **`incompressible/`** | `simpleFoam`, `pimpleFoam`, `pisoFoam`, `icoFoam`, `adjointOptimisationFoam` | Stationäre/transiente inkompressible Strömungen, Aerodynamik |
| **`compressible/`** | `rhoPimpleFoam`, `rhoSimpleFoam`, `rhoCentralFoam`, `sonicFoam` | Transsonische/überschallschnelle Strömungen, kompressible Gase |
| **`combustion/`** | `reactingFoam`, `fireFoam`, `chemFoam`, `PDRFoam`, `XiFoam` | Finite-Rate Gasphasenreaktionen, Verbrennung, Flammen |
| **`multiphase/`** | `interFoam`, `interIsoFoam`, `twoPhaseEulerFoam`, `multiphaseEulerFoam`, `cavitatingFoam` | Freie Oberflächen (VOF), Tropfen/Blasen (Euler-Euler), Kavitation |
| **`lagrangian/`** | `DPMFoam`, `sprayFoam`, `reactingParcelFoam`, `coalChemistryFoam` | Diskrete Partikel, Einspritzung, Spray-Breakup & Verdampfung |
| **`electromagnetics/`** | `mhdFoam`, `magneticFoam`, `electrostaticFoam` | Magnetohydrodynamik (MHD), Lorentz-Kräfte, Plasmen |
| **`finiteArea/`** | `liquidFilmFoam`, `surfactantFoam` | 2D-Flüssigkeitsfilme auf Wänden (Eulerian Wall Film) |
| **`acoustic/`** | `acousticFoam` | Direkte lineare akustische Wellenausbreitung |
| **`stressAnalysis/`** | `solidDisplacementFoam`, `solidEquilibriumDisplacementFoam` | Strukturmechanik, thermische Spannungen |
| **`basic/`** | `laplacianFoam`, `potentialFoam`, `scalarTransportFoam` | Reine Wärmeleitung, Potenzialströmung, passiver Skalartransport |

---

### C. C++ Kern-Bibliotheken (`src/`) für `native_cpp` & C++ Erweiterungen

Beim Schreiben von benutzerdefinierten C++ Erweiterungen (`Make/options`) müssen folgende Include-Pfade und Bibliotheken verwendet werden:

| Modul / Bibliothek | Include-Pfad (`EXE_INC`) | Linker-Flag (`LIB_LIBS`) | Zweck |
| :--- | :--- | :--- | :--- |
| **Finite Volume** | `-I$(LIB_SRC)/finiteVolume/lnInclude` | `-lfiniteVolume` | `fvMesh`, `fvMatrix`, `volScalarField`, Randbedingungen |
| **fvOptions** | `-I$(LIB_SRC)/fvOptions/lnInclude` | `-lfvOptions` | Quellterme, Wärmequellen, Porosität, Katalyse |
| **Mesh Tools** | `-I$(LIB_SRC)/meshTools/lnInclude` | `-lmeshTools` | Zellmengen, geometrische Suchen, Flächenzonen |
| **Sampling** | `-I$(LIB_SRC)/sampling/lnInclude` | `-lsampling` | Schnittebenen, Proben, Liniendaten |
| **Turbulence Models** | `-I$(LIB_SRC)/TurbulenceModels/turbulenceModels/lnInclude`<br/>`-I$(LIB_SRC)/TurbulenceModels/compressible/lnInclude`<br/>`-I$(LIB_SRC)/TurbulenceModels/incompressible/lnInclude` | `-lturbulenceModels`<br/>`-lcompressibleTurbulenceModels`<br/>`-lincompressibleTurbulenceModels` | Turbulenzviskosität $\nu_t / \mu_t$, $k-\omega$ SST, LES WALE |
| **Thermophysical Models**| `-I$(LIB_SRC)/thermophysicalModels/basic/lnInclude`<br/>`-I$(LIB_SRC)/thermophysicalModels/specie/lnInclude`<br/>`-I$(LIB_SRC)/thermophysicalModels/reactionThermo/lnInclude` | `-lfluidThermophysicalModels`<br/>`-lspecie`<br/>`-lreactionThermophysicalModels` | Zustandsgleichungen, NASA-Polynome, $h$, $c_p$, Spezies |
| **Radiation** | `-I$(LIB_SRC)/thermophysicalModels/radiation/lnInclude` | `-lradiationModels` | fvDOM, P1, Einstrahlung $G$, Strahlungswärmestrom $q_r$ |
| **Lagrangian / DPM** | `-I$(LIB_SRC)/lagrangian/basic/lnInclude`<br/>`-I$(LIB_SRC)/lagrangian/intermediate/lnInclude` | `-llagrangian`<br/>`-llagrangianIntermediate` | Partikelwolken, Kinematik, Injektoren |
| **Surface Film (EWF)** | `-I$(LIB_SRC)/regionModels/surfaceFilmModels/lnInclude` | `-lsurfaceFilmModels` | Wandfilmhöhe $\delta_f$, Wandfilmtemperatur, Verdampfung |
| **Pyrolysis / Ablation**| `-I$(LIB_SRC)/regionModels/pyrolysisModels/lnInclude` | `-lpyrolysisModels` | 1D-Reaktionsfronten im Festkörper, Ablation |

---

### D. Blueprint & Template-Bibliothek (`etc/caseDicts/`)

Hier befinden sich die offiziellen OpenCFD-Vorlagen für alle Eingabedateien:

* **Annotierte Dictionaries:** `OpenFOAM_base/openfoam/etc/caseDicts/annotated/`
  * `blockMeshDict`: Gitterdefinition mit Blöcken, Vertices, Kanten und Patches.
  * `snappyHexMeshDict`: 3D Hex-dominant Meshing mit Refinement-Surfaces, Baffles und Prismen-Layers.
  * `decomposeParDict`: Parallele Zerlegung (Scotch, Hierarchical, KaHIP) mit `processorWeights`.
  * `createPatchDict`: Ändern von Patch-Typen (z. B. Cyclic, Wall, Symmetry).
  * `topoSetDict`: Zell-, Flächen- und Punktzonen (`cellZone`, `faceZone`) definieren.
  * `meshQualityDict`: Kriterien für Schiefe (skewness), Nicht-Orthogonalität und Aspektverhältnis.

* **Echtzeit-FunctionObjects:** `OpenFOAM_base/openfoam/etc/caseDicts/postProcessing/`
  * Eingebunden in `system/controlDict` via `#includeFunc <name>`:
  * `fields/yPlus`: Berechnet den wandnormalen $y^+$-Wert zur Überprüfung der Grenzschichtauflösung.
  * `fields/wallShearStress`: Berechnet Wandschubspannungs-Tensoren $\boldsymbol{\tau}_w$.
  * `fields/wallHeatFlux`: Berechnet den Wärmestrom an Wänden $\dot{q}_w$.
  * `fields/Q` / `fields/Lambda2`: Wirbelidentifikation (Vortex Cores).
  * `pressure/totalPressureCompressible`: Gesamtdruckfelder.
  * `visualization/streamLine`: Stromlinien-Generierung direkt zur Laufzeit.
  * `utilities/runTimeControl`: Konvergenzüberwachung & automatischer Simulationsstopp bei Residuen-Schwellwert.
  * `utilities/vtkWrite`: Direkter Export von OpenFOAM-Feldern im VTK-Format für den Viewport.

---

## 3. Physik-Modul-Mapping: Die 12 Kernmodule

Jedes Modul im `backend/physics_modules/` korrespondiert mit spezifischen OpenFOAM-Solvern, C++ Klassen, Dictionaries und Tutorials:

```text
+-------------------------+---------------------------------+----------------------------------------+
| Physik-Modul (Backend)  | OpenFOAM Upstream Solver        | Referenz-Tutorial / Blueprint          |
+-------------------------+---------------------------------+----------------------------------------+
| 1. Energy & Heat        | chtMultiRegionFoam              | tutorials/heatTransfer/chtMultiRegion  |
| 2. Viscous & Turbulence | simpleFoam, pimpleFoam          | tutorials/incompressible/simpleFoam    |
| 3. Multiphase           | interFoam, twoPhaseEulerFoam    | tutorials/multiphase/interFoam         |
| 4. Species & Chemistry  | reactingFoam, rhoReactingFoam   | tutorials/combustion/reactingFoam      |
| 5. Discrete Phase (DPM) | DPMFoam, sprayFoam              | tutorials/lagrangian/DPMFoam           |
| 6. Eulerian Wall Film   | surfaceFilmFoam                 | tutorials/heatTransfer/windshield      |
| 7. Radiation            | buoyantReactingFoam, chtMulti   | tutorials/heatTransfer/externalSolar   |
| 8. Acoustics            | pimpleFoam + Curle / FWH        | tutorials/incompressible/pimpleFoam    |
| 9. Ablation & TPS       | reactingFoam + pyrolysisModels  | src/regionModels/pyrolysisModels       |
| 10. Battery Thermal     | chtMultiRegion + jouleHeating   | src/fvOptions/sources/derived/joule    |
| 11. MHD & Plasma        | mhdFoam, magneticFoam           | tutorials/electromagnetics/mhdFoam     |
| 12. Porous & Heat Exch. | chtMultiRegion + porousZone     | src/fvOptions/sources/derived/explicitP|
+-------------------------+---------------------------------+----------------------------------------+
```

### Detaillierte Modul-Steckbriefe:

#### 1. Energy & Heat (`backend/physics_modules/energy_heat/`)
* **OpenFOAM Solver:** `chtMultiRegionFoam`, `buoyantPimpleFoam`
* **Wichtige Dictionaries:**
  * `constant/thermophysicalProperties` (`heRhoThermo`, `pureMixture`, `perfectGas`)
  * `system/fvSchemes` (`div(phi,h) Gauss limitedLinear 1;`)
  * `0/T` (Temperatur-Randbedingungen wie `turbulentTemperatureCoupledBaffleMixed`)
* **Upstream C++ Code:** `src/thermophysicalModels/basic/`
* **Referenz-Tutorial:** `tutorials/heatTransfer/chtMultiRegionFoam/multiRegionHeater`

#### 2. Viscous & Turbulence (`backend/physics_modules/viscous_turb/`)
* **OpenFOAM Solver:** `simpleFoam`, `pimpleFoam`, `rhoPimpleFoam`
* **Wichtige Dictionaries:**
  * `constant/turbulenceProperties` (`simulationType RAS;` / `LES;`)
  * `RAS { RASModel kOmegaSST; turbulence on; }`
* **Unterstützte Modelle:**
  * RANS: `kOmegaSST`, `kOmegaSSTSAS`, `kEpsilon`, `realizableKE`, `SpalartAllmaras`, `GEKO`
  * LES: `WALE`, `Smagorinsky`, `dynamicKEqn`
* **Wandfunktionen:** `kLowReWallFunction`, `omegaWallFunction`, `nutkWallFunction`
* **Upstream C++ Code:** `src/TurbulenceModels/turbulenceModels/`
* **Referenz-Tutorial:** `tutorials/incompressible/simpleFoam/turbulentFlatPlate`

#### 3. Multiphase (`backend/physics_modules/multiphase/`)
* **OpenFOAM Solver:** `interFoam` (VOF mit MULES / isoAdvector), `twoPhaseEulerFoam`
* **Wichtige Dictionaries:**
  * `constant/transportProperties` (Phasendichten $\rho_1, \rho_2$, Oberflächenspannung $\sigma$)
  * `system/fvSchemes` (`div(phi,alpha) Gauss vanLeer;`)
* **Felder:** `alpha.water`, `p_rgh`, `U`
* **Upstream C++ Code:** `applications/solvers/multiphase/interFoam/`
* **Referenz-Tutorial:** `tutorials/multiphase/interFoam/laminar/damBreak`

#### 4. Species & Chemistry (`backend/physics_modules/species_chem/`)
* **OpenFOAM Solver:** `reactingFoam`, `chtMultiRegionFoam`
* **Wichtige Dictionaries:**
  * `constant/chemistryProperties` (`chemistryType { method ode; solver EulerImplicit; }`)
  * `constant/reactions` (Arrhenius-Kinetiken, reversible Reaktionen)
  * `constant/thermo.data` (NASA 7/9-Koeffizienten-Polynome)
* **Wandkatalyse Backend-Hook:** `backend/native_cpp/smr_catalysis/smrKineticsSource.C`
* **Upstream C++ Code:** `src/thermophysicalModels/reactionThermo/`
* **Referenz-Tutorial:** `tutorials/combustion/reactingFoam/RAS/sandiaD_LTS`

#### 5. Discrete Phase (DPM) (`backend/physics_modules/dpm_lagrangian/`)
* **OpenFOAM Solver:** `DPMFoam`, `sprayFoam`
* **Wichtige Dictionaries:**
  * `constant/kinematicCloudProperties` (Injektoren, Drag-Law: Sphere / Non-Spherical)
  * Verteilungsmodelle: `RosinRammler`, `exponential`, `normal` in `src/lagrangian/distributionModels/`
* **Kollisionsmodelle:** Bai-Gosman Spray-Wand-Interaktion (`stick`, `rebound`, `splash`)
* **Upstream C++ Code:** `src/lagrangian/intermediate/`
* **Referenz-Tutorial:** `tutorials/lagrangian/DPMFoam/Goldschmidt`

#### 6. Eulerian Wall Film (EWF) (`backend/physics_modules/wall_film_ewf/`)
* **OpenFOAM Solver:** `surfaceFilmFoam`, `chtMultiRegionFoam`
* **Wichtige Dictionaries:**
  * `constant/surfaceFilmProperties` (`surfaceFilmModel kinematicThinFilm;`)
* **Gleichungen:** 2D-Flüssigkeitsfilm auf Wandflächen mit `finiteArea` (`faMesh`)
* **Upstream C++ Code:** `src/regionModels/surfaceFilmModels/` & `src/finiteArea/`
* **Referenz-Tutorial:** `tutorials/heatTransfer/chtMultiRegionFoam/windshieldDefrost`

#### 7. Radiation (`backend/physics_modules/radiation/`)
* **OpenFOAM Solver:** `chtMultiRegionFoam`, `buoyantReactingFoam`
* **Wichtige Dictionaries:**
  * `constant/radiationProperties` (`radiationModel fvDOM;`, `fvDOMCoeffs { nPhi 3; nTheta 3; }`)
* **Absorptionsmodelle:** `constantAbsorptionEmission`, `greyMeanAbsorptionEmission`
* **Upstream C++ Code:** `src/thermophysicalModels/radiation/`
* **Referenz-Tutorial:** `tutorials/heatTransfer/chtMultiRegionFoam/externalSolarLoad`

#### 8. Acoustics (`backend/physics_modules/acoustics/`)
* **OpenFOAM Solver:** `pimpleFoam` mit Akustik-FunctionObjects
* **Wichtige Dictionaries:**
  * `system/controlDict`: Einbindung von `Curle` oder `proudmanAcousticPower`
* **Analytische Methoden:** Ffowcs Williams-Hawkings (FWH) Oberflächen-Integral, Curle-Akustikdipole
* **Upstream C++ Code:** `src/functionObjects/field/Curle/` & `src/functionObjects/field/proudmanAcousticPower/`
* **Referenz-Tutorial:** `tutorials/incompressible/pimpleFoam/laminar/cylinder2D`

#### 9. Ablation & TPS (`backend/physics_modules/ablation/`)
* **OpenFOAM Solver:** `reactingFoam` mit Pyrolyse-Kopplung
* **Wichtige Dictionaries:**
  * `constant/pyrolysisProperties` (`pyrolysisModel reactingOneDim;`)
* **Upstream C++ Code:** `src/regionModels/pyrolysisModels/`
* **Kenngrößen:** Oberflächenrezessionsrate $\dot{s}$, Verkohlungsdichte $\rho_{char}$, Arrhenius-Gasabspaltung.

#### 10. Battery Thermal (`backend/physics_modules/battery_thermal/`)
* **OpenFOAM Solver:** `chtMultiRegionFoam` gekoppelt mit C++ fvOptions
* **Wichtige Dictionaries:**
  * `system/fvOptions`: `jouleHeatingSource` oder benutzerdefiniertes 2-RC ECM Quellterm-Modell
* **Gekoppelte Physik:** Joulesche Wärme ($I^2 R_0$), reversible Entropiewärme ($I T \frac{dU}{dT}$), Arrhenius Thermal Runaway.
* **Upstream C++ Code:** `src/fvOptions/sources/derived/jouleHeatingSource/`

#### 11. Magnetohydrodynamics (MHD) (`backend/physics_modules/mhd_plasma/`)
* **OpenFOAM Solver:** `mhdFoam`, `magneticFoam`
* **Wichtige Dictionaries:**
  * `constant/transportProperties`: Elektrische Leitfähigkeit $\sigma_{el}$, magnetische Permeabilität $\mu_{mag}$
* **Kopplung:** Navier-Stokes mit Lorentz-Kraft $\mathbf{f}_L = \mathbf{J} \times \mathbf{B}$
* **Upstream C++ Code:** `applications/solvers/electromagnetics/mhdFoam/`
* **Referenz-Tutorial:** `tutorials/electromagnetics/mhdFoam/hartmann`

#### 12. Porous Media & Heat Exchanger (`backend/physics_modules/porous_hex/`)
* **OpenFOAM Solver:** `simpleFoam`, `pimpleFoam`, `chtMultiRegionFoam`
* **Wichtige Dictionaries:**
  * `system/fvOptions`: `explicitPorositySource` mit Darcy-Forchheimer Tensoren ($\mathbf{D}, \mathbf{F}$)
* **Gleichung:** $\mathbf{S}_p = -\left(\mu \mathbf{D} + \frac{1}{2} \rho |\mathbf{U}| \mathbf{F}\right) \mathbf{U}$
* **Upstream C++ Code:** `src/fvOptions/sources/derived/explicitPorositySource/`

---

## 4. Kompilierungs-Leitfaden für `backend/native_cpp` (wmake)

OpenFOAM nutzt das Werkzeug `wmake` zum Erstellen dynamischer Bibliotheken (`.so`).

### Aufbau des `Make/`-Verzeichnisses:
1. **`Make/files`:**
   ```text
   smrKineticsSource.C

   LIB = $(FOAM_USER_LIBBIN)/libsmrKineticsSource
   ```
2. **`Make/options`:**
   ```text
   EXE_INC = \
       -I$(LIB_SRC)/finiteVolume/lnInclude \
       -I$(LIB_SRC)/fvOptions/lnInclude \
       -I$(LIB_SRC)/meshTools/lnInclude \
       -I$(LIB_SRC)/sampling/lnInclude

   LIB_LIBS = \
       -lfiniteVolume \
       -lfvOptions \
       -lmeshTools
   ```

### Kompilier-Workflow in WSL2:
```bash
# In WSL2 ausführen:
source /usr/lib/openfoam/openfoam/etc/bashrc
cd /mnt/g/Datein/Projects/CFDSolver/backend/native_cpp/smr_catalysis
wmake libso
```
Die erzeugte Bibliothek landet in `$FOAM_USER_LIBBIN` (typischerweise `~/OpenFOAM/$USER-v2606/platforms/linux64GccDPInt32Opt/lib/libsmrKineticsSource.so`).

### Einbindung im OpenFOAM-Case (`system/controlDict`):
```text
libs
(
    "libsmrKineticsSource.so"
);
```

---

## 5. Rust High-Speed I/O Leitfaden (`backend/native_rust/foam_fast_io/`)

### Zielsetzung:
Vermeidung des Flaschenhalses bei Python-Dateilesern für Multi-Millionen-Zellen-Gitter.

### Kern-Architektur:
1. **`memmap2`:** Virtuelles Memory-Mapping von OpenFOAM-Feldern (`0/U`, `1.0/T`, `constant/polyMesh/points`).
2. **`fast-float`:** Schnelles SIMD-Parsing von Gleitkommazahlen ohne ASCII-String-Overhead.
3. **`rayon`:** Paralleles Extrahieren von Schnittebenen über alle CPU-Kerne.
4. **`PyO3` / `numpy`:** Zero-Copy Pointer-Übergabe direkt an NumPy-Arrays, die vom Frontend (PyVista / Trame) gerendert werden.

---

## 6. Case-Generierungs-Cheat-Sheet

### Verzeichnisstruktur eines validen Cases:
```text
case_root/
├── 0/                           # Anfangs- und Randbedingungen (T, p, U, k, omega, etc.)
├── constant/                    # Gitter und physikalische Stoffwerte
│   ├── polyMesh/                # points, faces, owner, neighbour, boundary
│   ├── thermophysicalProperties # Thermodynamik-Definition
│   └── turbulenceProperties     # Turbulenzmodell (RAS/LES)
└── system/                      # Numerische Steuerungsdateien
    ├── controlDict              # Solver, Zeitschritt, writeInterval, libs, functionObjects
    ├── fvSchemes                # Diskretisierung (Euler, Gauss linear, limitedLinear)
    ├── fvSolution               # Matrixlöser (GAMG, PBiCGStab, smoothSolver, PIMPLE-Schleifen)
    └── decomposeParDict         # MPI-Domain-Decomposition & Core-Gewichtung
```

### Wichtigste OpenFOAM CLI-Befehle (über `WslSimulationRunner`):
* **Gitter-Erstellung:** `blockMesh`, `snappyHexMesh -overwrite`
* **Gitter-Prüfung:** `checkMesh -allTopology -allGeometry`
* **Gitter-Skalierung (z. B. mm -> m):** `transformPoints -scale "(0.001 0.001 0.001)"`
* **Case-Zerlegung:** `decomposePar -force`
* **MPI-Simulation:** `mpirun -np <ranks> <solver> -parallel > log.<solver> 2>&1`
* **Rekonstruktion:** `reconstructPar -latestTime`
* **Post-Processing zur Laufzeit:** `postProcess -func yPlus -latestTime`
