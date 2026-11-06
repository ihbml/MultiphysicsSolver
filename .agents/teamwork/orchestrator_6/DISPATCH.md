## 2026-11-03T23:56:48Z
Produktionsreife Umsetzung von Work Package 4 (Echte 3D-Fluidgebiets- & Mehrbereichs-Vernetzung / Multiphysics Domain Meshing & GCI-Hierarchie) des CFD-Meshing-Subsystems für CFDSolver in Python und C++20 unter Einberufung des unabhängigen 3-Agenten-Audit-Gremiums (Paper-Fidelity Challenger, Code- & Software-Architektur Auditor, Mathematik- & Numerik-Auditor).

Working directory: G:\Datein\Projects\CFDSolver
Integrity mode: development
The team must NOT: take shortcuts (keine Dummy-Stubs, keine Magic Numbers, keine Heuristiken, keine stillen Fallbacks mit except: pass).

## Reference Material
- SUGGESTIONS_BACKEND.md (Abschnitte 4, 5, 6 als primäre Anforderungs- und Architekturspezifikation)
- Literature Survey & Berger & Giuliani (2024) für Cut-Cell Snapping, Small-Cell Merging und WSRD-Gewichte
- ANSYS Mosaic / PUMA Prinzip für polyedrische Übergänge ohne Hanging Nodes
- CAD-Dateien: resources/CADTestSamples/TestCAD.stp, resources/CADTestSamples/BladeFullRev1CFD.stp (scale=0.001)
- OpenFOAM-Referenzgeometrien in OpenFOAM_base/openfoam/tutorials/resources/geometry/ (z. B. snappyMultiRegionHeater)

## Verification Resources
- 579 bestehende Tests in tests/ und backend/
- Dedizierte Testsuite tests/test_wp4_multiphysics_hexcore.py inklusive CAD-Benchmarks mit moderaten Gittergrößen
- Unabhängiges 3-Agenten-Audit-Gremium für Gate WP4:
  1. Paper-Fidelity Challenger
  2. Code- & Software-Architektur Auditor (Google C++ Style Guide & PEP 8)
  3. Mathematik- & Numerik-Auditor (Strikt $V_i > 0$, relative Gauss-Closure $< 10^{-11}$, echte Projektionsidentität)

## Requirements

### R1. Echte 3D-Fluidgebiets-Vernetzung & Farfield-Patches
- Restlose Entfernung aller `len(verts) == 8`-Heuristiken und Millimeter-Clamps.
- Explizite Domänen-Modi in `MeshingConfig`: `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"`.
- Konfigurierbares `farfield_padding` (relativ zu $L_{\text{ref}}$, z. B. $5L$ stromaufwärts, $10L$ stromabwärts, $5L$ seitlich) oder explizite `farfield_bbox`.
- Automatische Zuweisung und Benennung der Farfield-Patches (`inlet` bei $x_{\min}$, `outlet` bei $x_{\max}$, `bottom` bei $z_{\min}$, `top` bei $z_{\max}$, `sides` bei $y_{\min}, y_{\max}$) sowie Vererbung der Körper-Patches (`SurfaceMesh.patches`).

### R2. 3D Mosaic Poly-Hexcore & Octree-Kern
- Konstruktion eines adaptiven, 2:1-balancierten Octree-Hexaeder-Gitters im Fluidgebiet ($\phi(\mathbf{x}) > 0$).
- Polyedrisches Übergangs-Interface ohne Hanging Nodes (ANSYS Mosaic / PUMA Prinzip): Wenn eine verfeinerte Zelle an eine unverfeinerte Nachbarzelle grenzt, wird das geteilte Quad-Face auf der ungeteilten Nachbarzelle durch die 4 koplanaren Teil-Faces ersetzt, sodass die Zelle als allgemeines Polyeder (`CellType::POLYHEDRON = 42`) im DiFVM-Graphen geführt wird.

### R3. Cut-Cell Snapping & Small-Cell Merging (Berger & Giuliani 2024)
- Oberflächen-Konformität: Schneiden von Randzellen mit der Null-Isofläche $\phi(\mathbf{x}) = 0$ bzw. Snapping grenznaher Knoten entlang $\nabla\phi$ auf die CAD-Oberfläche (unter Conformal AMIPS Energie $E_{\text{AMIPS}}$ oder VoroCrust-Saatpunktspiegelung).
- Small-Cell Merging: Verschmelzen von angeschnittenen Randzellen mit kleinem Volumen ($V_{\text{cut}} < 0.20 V_{\text{bg}}$) topologisch mit ihrer Nachbarzelle in Richtung $\nabla\phi$ zu einer geschlossenen Polyeder-Zelle.
- WSRD-Gewichte: Berechnung und Hinterlegung der Berger-Giuliani WSRD-Gewichte $w_{ij} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ für Cut-Cells mit $0.20 \le \alpha_i < 0.50$.

### R4. Automatisierte 3-Gitter-GCI-Hierarchie & Multiphysics CHT-Kopplung
- Integrierte, deterministische Generierung von 3 Verfeinerungsstufen (Grob, Mittel, Fein mit Verfeinerungsfaktor $r \approx 1{,}3\text{--}1{,}5$) unter exakter Erhaltung aller Patches und Topologie für automatisierte GCI-Studien ($\text{GCI} < 1\,\%$).
- Multiphysics Open Software Architecture: Unterstützung für reine Fluidgebiete (CFD), Festkörpergebiete (Struktur-FEM & Wärmeleitung) sowie konforme Mehrbereichs-Kopplung (CHT Twin-Faces an Fluid-Solid-Grenzflächen via `cell_regions`).

### R5. Ressourcen-Direktiven, Audit-Gremium & Strikte Zero-Shortcut Policy
- Max. 16 Threads (`OMP_NUM_THREADS=16`) für OpenMP, Pytest und Subprozesse (NICHT alle 28 Cores belegen, zum Schutz von Ansys).
- Strenger RAM-Schonmodus (moderate Gittergrößen in Unit-Tests, flache SoA-Arrays, sofortige Puffer-Freigabe via RAII / `cfd_mesher_free`; hochaufgelöste Fine-Mesh-CAD-Tests können separat später ausgeführt werden, wenn Ansys Fluent nicht mehr läuft).
- Unabhängiges 3-Agenten-Audit-Gremium für Gate WP4 mit einstimmigem 3:0 `APPROVE`.
- Zero-Shortcut Policy: Keine Dummy-Stubs, keine Magic Numbers, keine Heuristiken, keine stillen Fallbacks (`except: pass`). Verbindliche Einhaltung des Google C++ Style Guides für C++20 und PEP 8 für Python.
