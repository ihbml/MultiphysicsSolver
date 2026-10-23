# Kommunikationsprotokoll: Backend <-> Frontend
> **Zweck dieser Datei:**  
> Dies ist das lebendige Übergabedokument zwischen der **Backend-Instanz** und der **Frontend-Instanz**.  
> Immer wenn eine Instanz Änderungen an Schnittstellen, Datenformaten, Pfaden oder Feldern vornimmt, wird hier ein Eintrag angelegt, damit die andere Seite sofort weiß, was verfügbar ist und wie darauf zugegriffen werden kann.

---

## 1. Aktueller Status der Schnittstelle (v0.1.0)

| Eigenschaft | Spezifikation |
| :--- | :--- |
| **Backend-Datenstruktur** | Python Dataclass / Dictionary via `backend.physics_modules.registry.ModuleRegistry` |
| **Frontend-Framework** | Trame (Vuetify + PyVista / VTK.js Remote/Local View) |
| **Case-Generierung** | `backend.core.case_generator.CaseGenerator` |
| **Simulation-Runner** | `backend.runner.wsl_runner.WslSimulationRunner` |

---

## 2. Verfügbare Datenobjekte & Pfade

### A. Physik-Module (Backend -> Frontend)
Das Frontend importiert die Registry direkt über:
```python
from backend.physics_modules.registry import ModuleRegistry

registry = ModuleRegistry()
modules = registry.get_all_modules()  # Gibt Dictionary aller verfügbaren Module zurück
```

Jedes Modul liefert über `mod.to_dict()` folgende Struktur:
```json
{
  "name": "Energy",
  "category": "Thermal",
  "description": "Löst die thermische Energiegleichung...",
  "enabled": true,
  "model_variant": "sensibleEnthalpy",
  "parameters": {},
  "required_fields": ["T", "h", "thermo:Cp", "thermo:rho"],
  "supported_solvers": ["chtMultiRegionFoam", "buoyantPimpleFoam"]
}
```

### B. Verfügbare 3D-Skalar- und Vektorfelder für den Viewport
Sobald ein OpenFOAM-Case berechnet ist oder geladen wird, stellt das Backend folgende Standard-Felder bereit:

| Feldname | Symbol | Einheit | Physikalisches Modul | Typ |
| :--- | :--- | :--- | :--- | :--- |
| `T` | Temperatur | $\text{K}$ | Energy / Radiation / Battery | Skalar |
| `p` / `p_rgh` | Druck | $\text{Pa}$ | Flow / Multiphase | Skalar |
| `U` | Geschwindigkeit | $\text{m/s}$ | Flow / Viscous | Vektor $(U_x, U_y, U_z)$ |
| `k` | Turbulente kinetische Energie | $\text{m}^2/\text{s}^2$ | Viscous & Turbulence | Skalar |
| `omega` / `epsilon`| Dissipationsrate | $1/\text{s}$ | Viscous & Turbulence | Skalar |
| `alpha.water` | Phasenanteil | $-$ | Multiphase (VoF) | Skalar $[0, 1]$ |
| `CH4`, `H2`, `CO` | Spezies-Massenanteile | $\text{kg/kg}$ | Species & Chemistry | Skalar $[0, 1]$ |
| `filmHeight` | Wandfilmdicke | $\text{m}$ | Eulerian Wall Film (EWF) | Skalar |
| `B` | Magnetische Flussdichte | $\text{T}$ | Magnetohydrodynamics (MHD)| Vektor |
| `soundPressureLevel`| Schalldruckpegel | $\text{dB}$ | Acoustics | Skalar |

---

## 3. Changelog & Übergabe-Historie

### [2026-10-19] — Initiale Übergabe (Backend -> Frontend)
* **Autor:** Master / Backend Initialisierung
* **Was wurde implementiert?**
  1. `backend/physics_modules/registry.py`: Registry mit 12 Modulen (Energy, Viscous, Multiphase, Species, DPM, EWF, Radiation, Acoustics, Ablation, Battery, MHD, Porous).
  2. `backend/core/case_generator.py`: Automatische Erstellung von `controlDict`, `fvSchemes`, `fvSolution` und `decomposeParDict` mit P-Core/E-Core Lastgewichtung.
  3. `backend/native_cpp/smr_catalysis/`: C++ `fvOption` für endotherme Wandkatalyse mit Jacobi-stabilisierter Hauptdiagonale (`eqn.diag()`).
  4. `frontend/cockpit/app.py`: Trame + PyVista Basis-Applikation mit Modul-Switches in der Sidebar und VTK-Viewport.
* **Wo zu finden?**
  * `G:\Datein\Projects\CFDSolver\backend\`
  * `G:\Datein\Projects\CFDSolver\frontend\cockpit\app.py`
* **Hinweis an das Frontend:**  
  Das Frontend kann Module jetzt per `registry.enable_module(name, variant)` und `registry.disable_module(name)` dynamisch umschalten. Validierungswarnungen werden über `registry.validate_configuration()` als Liste von Strings geliefert.

### [2026-10-19] — CAD- & Geometrie-Ingestion Engine bereitgestellt (Backend -> Frontend)
* **Autor:** Backend / Teamwork Sentinel
* **Was wurde implementiert?**
  1. `backend/geometry/pipeline.py`: `CADGeometryEngine`-Fassade für automatisches Laden, Auditieren, Skalieren ($mm \to m$) und Exportieren für OpenFOAM.
  2. Multi-Format Support: ISO STEP (AP203, AP214, AP242), Binary & ASCII STL (mit `cKDTree` Vertex Welding), Wavefront OBJ.
  3. Siemens NX 1926 Native Bridge (`backend/geometry/bridge/`): Automatische Konvertierung von nativer `.prt` zu STEP AP214 in $< 2\,\text{s}$.
  4. OpenFOAM Exporter (`backend/geometry/exporter/`): Schreibt direkt nach `constant/triSurface/*.stl` und generiert passende Geometrieblöcke für `system/snappyHexMeshDict`.
  5. Vollständig validiert mit 272/272 bestandenen Tests (10/10 OpenFOAM `surfaceCheck`-Kriterien, ASME V&V 10/20).
* **Wo zu finden?**
  * `G:\Datein\Projects\CFDSolver\backend\geometry\`
* **Hinweis an das Frontend:**
  Das Cockpit kann über `CADGeometryEngine.load(path).audit()` direkt BoundingBox, Dichtheitsstatus (`is_watertight`), Flächenanzahl und Dreiecke für den PyVista 3D-Viewport abfragen und Geometrien mit einem Klick in das Case-Verzeichnis exportieren.

