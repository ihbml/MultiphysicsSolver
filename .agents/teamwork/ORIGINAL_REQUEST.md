# Original User Request

## Initial Request — 2026-10-19T13:46:17Z

Entwicklung einer industrietauglichen CAD- & Geometrie-Ingestion-Engine für das CFDSolver-Backend (G:\Datein\Projects\CFDSolver\backend\geometry). Das Modul liest ISO-Standards (STEP AP203/214/242, STL, OBJ) ein, prüft Geometrien auf Dichtheit (Watertight/Manifold), skaliert korrekt (CAD-mm -> CFD-m) und exportiert optimierte Oberflächen für OpenFOAM snappyHexMesh. Zudem bindet eine optionale Bridge die lokal vorhandenen CAD-Installationen (Siemens NX 1926) an, um native Formate (.prt) automatisiert zu konvertieren.

Working directory: G:\Datein\Projects\CFDSolver\backend\geometry
Integrity mode: development
Requested team: Implementer (Developer) + Unabhängiger Validator / Kritiker

## Verification Resources & Local Environments

### 1. Reale Referenz-Dateien in G:\Datein\Projects\CFDSolver\resources\CADTestSamples\
- TestCAD.obj (Wavefront OBJ, 8 Vertices, 12 Faces)
- TestCAD.stl (Binary STL, 12 Facetten)
- TestCADSTPAP203.stp (STEP AP203)
- TestCADSTPAP214.stp (STEP AP214 aus Siemens NX 1938)
- TestCADSTPAP242.stp (STEP AP242)
- TestCAD.prt (Siemens NX 1938 Part)

### 2. Verfügbare lokale CAD-Installationen (Read-Access)
- Siemens NX 1926: C:\Program Files\Siemens\NX1926
  - Native Translatoren: NXBIN\step203ug.exe, step214ug.exe, step242.exe
- Autodesk Inventor 2026: C:\Program Files\Autodesk\Inventor 2026
- Ansys v252: C:\Program Files\ANSYS Inc\v252

## Requirements

### R1. CAD-Ingestion & Format-Unterstützung (Open Standards)
- Einlesen von ISO STEP-Dateien (.step, .stp AP203/214/242), STL (.stl ASCII/Binary) und OBJ (.obj).
- Tesselierung von B-Rep-Flächen mit einstellbarer Chordal Deflection und Winkelauflösung.
- Performante Architektur (C++/Rust-Kern oder performante Bibliotheken wie OpenCASCADE / Trimesh / Numpy).

### R2. Native CAD Bridge (Siemens NX & Co.)
- Erkennung lokaler CAD-Installationen (Siemens NX 1926).
- CLI-Wrapper zur automatischen Konvertierung nativer Formate (.prt) in STEP/STL über die NX-Translatoren (step214ug.exe etc.).

### R3. Geometrie-Audit & OpenFOAM-Vorbereitung
- Manifold- und Dichtheitsprüfung (Watertight: geschlossene Hülle, Euler-Charakteristik V - E + F = 2 für einfache Körper, 0 offene Kanten).
- Normalen-Prüfung und konsistente Ausrichtung (nach außen bzw. in die Strömungsdomäne).
- Robuste Einheiten-Transformation (200 mm -> 0.2 m) mit Bounding-Box-Verifikation.
- Export in OpenFOAM-konforme constant/triSurface/*.stl und *.obj mit sauber definierten Patch-Namen für snappyHexMeshDict.

### R4. Unabhängige Test-Suite & ASME V&V Verifikation
- Implementierung einer umfassenden Test-Suite durch einen unabhängigen Test-/Validator-Agenten.
- Verifikation gegen die realen Test-Samples in resources/CADTestSamples/.
- Programmatische Prüfung gegen OpenFOAM surfaceCheck-Kriterien (0 nicht-geschlossene Kanten, 0 degenerierte Flächen).

## Acceptance Criteria

### Funktionalität & Schnittstellen
- [ ] Alle Test-Samples (TestCAD.obj, TestCAD.stl, TestCADSTPAP203.stp, TestCADSTPAP214.stp, TestCADSTPAP242.stp) werden erfolgreich eingelesen und geometrisch analysiert.
- [ ] Die Bounding Box des gedrehten Würfels wird vor und nach der Skalierung (200 mm -> 0.2 m) exakt berechnet.
- [ ] Exportierte Oberflächennetze landen in OpenFOAM-konformer Struktur (constant/triSurface/).

### Geometrische Qualität & CFD-Tauglichkeit
- [ ] Watertight/Manifold-Check bestätigt: Geometrie ist vollständig geschlossen (keine Löcher, keine T-Junctions).
- [ ] Normalenvektoren zeigen konsistent nach außen.

### Verifikation & Testabdeckung
- [ ] Unabhängige Test-Suite durch den Validator-Agenten verifiziert die Pipeline automatisiert.
- [ ] Fehlerhafte / offene Geometrien werden erkannt und mit klaren Fehlermeldungen abgefangen.
