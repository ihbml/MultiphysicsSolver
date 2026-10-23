# CAD & Geometry Ingestion Engine

Industrietaugliche CAD-Ingestion- und Geometrieverarbeitungs-Engine für das CFDSolver-Backend.

---

## 🌟 Kernfunktionen

1. **Multi-Format-Unterstützung:**
   - **ISO STEP:** AP203, AP214, AP242 (B-Rep Tesselierung via Gmsh/OpenCASCADE mit parametrisierbarer Durchbiegung).
   - **STL:** Binär und ASCII mit räumlicher `cKDTree` + Union-Find Vertex-Verschweißung (Topological Welding).
   - **Wavefront OBJ:** Polygon-Fan-Triangulierung und Gruppenerkennung.
   - **Siemens NX 1926 Native (.prt):** Automatisierte Headless-Übersetzung via `step214ug.cmd` in $< 2\,\text{s}$.
2. **Geometrie- & Manifold-Audit (OpenFOAM surfaceCheck-Konformität):**
   - 2-Manifold-Verifikation (keine Kanten mit mehr als 2 Nachbarflächen, keine T-Junctions).
   - Wasserdichtheits-Prüfung (Wasserdicht = 0 offene Berandungen).
   - Euler-Poincaré-Charakteristik ($\chi = V - E + F = 2$ für topologische Sphären).
   - Gaußscher Integralsatz (Divergenzsatz): Vorzeichenbehaftetes Volumen und automatische Reparatur invertierter Normalen.
   - Vollständige Prüfung aller 10 OpenFOAM `surfaceCheck.C`-Kriterien.
3. **Präzise Einheiten-Transformation:**
   - Skalierung von CAD-Millimetern ($mm$) auf OpenFOAM-Meter ($m$) unter exakter Wahrung linearer ($s$), quadratischer ($s^2$) und kubischer ($s^3$) Invarianten.
4. **OpenFOAM Export:**
   - Exportiert bereinigte STL- und OBJ-Oberflächen direkt nach `<case_dir>/constant/triSurface/`.
   - Generiert passende Geometrieblöcke für `system/snappyHexMeshDict`.

---

## 🚀 Schnellstart & Verwendung

```python
from backend.geometry.pipeline import CADGeometryEngine

# 1. Engine initialisieren
engine = CADGeometryEngine(auto_repair_normals=True, default_scale_factor=0.001)

# 2. Datei laden (unterstützt .stp, .step, .stl, .obj, .prt)
engine.load("resources/CADTestSamples/TestCAD.prt")

# 3. Geometrie auditieren
report = engine.audit()
print(f"Watertight: {report.is_watertight}, Manifold: {report.is_manifold}, Euler: {report.euler_characteristic}")

# 4. Von CAD-Millimetern auf OpenFOAM-Meter skalieren
engine.scale(0.001)

# 5. Für OpenFOAM exportieren
engine.export_openfoam(case_dir="cases/my_case", surface_name="cube")
```

Oder alles in einem einzigen Durchlauf:
```python
result = engine.process_for_openfoam(
    input_file="resources/CADTestSamples/TestCADSTPAP214.stp",
    case_dir="cases/my_case",
    surface_name="cube_boundary"
)
```

---

## 🧪 Testabdeckung & Verifikation

* **272 / 272 automatisierte Tests bestanden (100 % Erfolgsquote, 0 Warnungen)**
* Test-Suiten:
  * `backend/geometry/tests/`: Modulare Unit-Tests für alle Submodule.
  * `tests/test_cad_e2e.py`: 4-stufige End-to-End-Pipeline-Tests an realen NX-Referenzteilen.
  * `tests/test_surface_check.py`: Programmatische Verifikation aller 10 OpenFOAM `surfaceCheck`-Kriterien.
  * `tests/test_asme_vv.py`: Formale ASME V&V 10/20 Code- und Invarianten-Verifikation.
  * `tests/test_adversarial_*.py`: Härtung gegen entartete Geometrien, Bow-Tie-Knoten und offene Löcher.
