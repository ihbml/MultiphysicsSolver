Ja. Wichtig ist jedoch die fachliche Abgrenzung: Das DiFVM-Paper beschreibt **keinen eigenständigen Netzgenerator**, sondern die Umwandlung eines vorhandenen unstrukturierten Netzes in statische Graph-/SoA-Arrays. Der passende Baustein ist daher primär ein **DiFVM Mesh Compiler und Preprocessor**. Ein Octree-Cut-Cell-Generator kann als zweite Stufe ergänzt werden.

Für diese Aufgabe würde ich **C++23** verwenden. C++ eignet sich am besten für robuste Geometriealgorithmen, BVH-/Octree-Strukturen, kontrolliertes Speicherlayout, SIMD, Parallelisierung und eine stabile C-ABI. Fortran ist für Geometrie und Topologie weniger geeignet; Rust wäre ähnlich schnell, würde aber die Implementierung komplexer geometrischer Algorithmen derzeit weniger direkt machen.

Nachfolgend ist der auf den Mesher reduzierte Prompt.

---

# Implementierungsauftrag: DiFVM Mesh Compiler und Octree-Cut-Cell-Mesher

## 1. Ziel

Implementiere einen eigenständigen, hochperformanten **C++23-Mesher und Mesh-Preprocessor** für einen graphbasierten Finite-Volume-Solver nach dem DiFVM-Prinzip.

Das Modul soll:

1. vorhandene unstrukturierte Polyeder-, Tetraeder-, Hexaeder-, Prismen- und Hybridnetze importieren,
2. optional aus triangulierten Oberflächen ein adaptives Octree-Cut-Cell-Netz erzeugen,
3. die vollständige Zell- und Flächengeometrie berechnen,
4. die Netzqualität analysieren,
5. eine statische `MeshErrorMap` erzeugen,
6. das Netz in solverfertige, 64-Byte-ausgerichtete SoA-Grapharrays übersetzen,
7. ohne Dateisystemzugriffe im späteren Solverbetrieb vollständig im RAM verbleiben.

Der Mesher enthält **keinen CFD-Solver**, keine PISO-Iteration, keine Physikmodelle und keine Optimierungsverfahren.

---

## 2. Sprache und Entwicklungsstandard

### Verbindlich

- C++23
- Google C++ Style Guide
- `double`/FP64 für sämtliche geometrischen Berechnungen
- 64-Bit-Indizes für große Netze
- `std::span` für nicht-besitzende Arrayansichten
- RAII und klare Besitzverhältnisse
- keine tiefen Vererbungshierarchien
- keine kryptischen Makros
- keine versteckten Heap-Allokationen in inneren Schleifen
- keine Abhängigkeit von OpenFOAM zur Laufzeit

### Parallelisierung

CPU-Basis:

- `std::execution`, eigener Thread-Pool oder optional OpenMP
- parallele Verarbeitung von Zellen und Flächen
- thread-lokale temporäre Speicher
- keine globalen Locks in Geometriekernen
- deterministischer Ausgabemodus für Tests

Optionale spätere Erweiterung:

- CUDA/HIP/SYCL für BVH-Abfragen und Flächen-/Zellmetriken
- der CPU-Code muss vollständig eigenständig funktionieren

---

## 3. Realistischer Funktionsumfang

Das DiFVM-Paper setzt ein vorhandenes Netz voraus. Deshalb wird das Projekt in zwei klar getrennte Komponenten aufgeteilt.

### A. Verpflichtend: `DifvmMeshCompiler`

Konvertiert bestehende Netze in ein statisches DiFVM-Graphlayout.

Unterstützte Eingaben:

- OpenFOAM `constant/polyMesh`
- VTK Unstructured Grid `.vtu`
- Legacy VTK `.vtk`
- internes binäres Meshformat
- programmgesteuerte In-Memory-Eingabe über C-ABI

### B. Zweite Ausbaustufe: `OctreeCutCellMesher`

Erzeugt aus triangulierten Oberflächen ein adaptives, überwiegend hexaedrisches Octree-Cut-Cell-Netz.

Unterstützte Oberflächenformate:

- STL binär und ASCII
- OBJ
- triangulierte In-Memory-Oberflächen

Nicht Bestandteil der ersten Version:

- direkter STEP-BRep-Import
- direkter NX-`.prt`-Import
- vollständige CAD-Healing-Engine
- Boundary-Layer-Prismen
- bewegte oder deformierende Netze

Direkter STEP-Support erfordert in der Praxis einen CAD-Kernel wie OpenCascade. Das wäre eine optionale Integration und keine Zero-Dependency-Komponente.

---

## 4. Projektstruktur

```text
backend/native_cpp/difvm_mesher/
├── CMakeLists.txt
├── include/difvm_mesh/
│   ├── mesh_types.h
│   ├── raw_mesh.h
│   ├── compiled_mesh.h
│   ├── mesh_compiler.h
│   ├── mesh_validator.h
│   ├── geometry_kernel.h
│   ├── mesh_error_map.h
│   ├── mesh_reordering.h
│   ├── octree.h
│   ├── cut_cell_mesher.h
│   ├── surface_bvh.h
│   ├── binary_format.h
│   └── c_api.h
├── src/
│   ├── mesh_compiler.cc
│   ├── mesh_validator.cc
│   ├── geometry_kernel.cc
│   ├── mesh_error_map.cc
│   ├── mesh_reordering.cc
│   ├── octree.cc
│   ├── cut_cell_mesher.cc
│   ├── surface_bvh.cc
│   ├── openfoam_reader.cc
│   ├── vtk_reader.cc
│   ├── vtk_writer.cc
│   ├── binary_format.cc
│   └── c_api.cc
├── tests/
│   ├── cube_test.cc
│   ├── tetra_test.cc
│   ├── hybrid_mesh_test.cc
│   ├── orientation_test.cc
│   ├── nonorthogonality_test.cc
│   ├── skewness_test.cc
│   ├── cut_cell_test.cc
│   └── roundtrip_test.cc
└── benchmarks/
    ├── mesh_compile_benchmark.cc
    └── octree_benchmark.cc
```

---

## 5. Internes Rohnetz

Ein importiertes Netz wird zunächst in einer allgemeinen, noch nicht solveroptimierten Struktur gespeichert:

```cpp
struct RawMesh {
  std::vector<std::array<double, 3>> points;

  // CSR-Darstellung der Punktindizes jeder Fläche.
  std::vector<std::uint64_t> face_point_offsets;
  std::vector<std::uint64_t> face_points;

  // Owner ist für jede Fläche vorhanden.
  std::vector<std::uint64_t> face_owner;

  // UINT64_MAX kennzeichnet eine Randfläche.
  std::vector<std::uint64_t> face_neighbour;

  std::vector<std::int32_t> face_patch_id;
  std::vector<std::string> patch_names;

  std::uint64_t cell_count = 0;
};
```

Die Rohstruktur darf variable Polygon- und Polyedergrößen enthalten.

---

## 6. Validierung und Reparatur

Vor der Graphkompilierung muss das Netz geprüft werden.

### Verpflichtende Prüfungen

- ungültige Punktindizes
- NaN- und Inf-Koordinaten
- doppelte aufeinanderfolgende Flächenpunkte
- Flächen mit weniger als drei eindeutigen Punkten
- degenerierte Flächen
- Flächen mit nahezu null Fläche
- Zellen mit nahezu null oder negativem Volumen
- nicht geschlossene Zellen
- interne Flächen ohne genau zwei angrenzende Zellen
- Randflächen mit ungültigem Patch
- nicht-manifold Topologie
- falsch orientierte Flächen
- doppelte Flächen
- isolierte Punkte und Zellen

### Orientierung

Für jede Fläche muss der Flächenvektor `S_f`:

- bei internen Flächen vom Owner zum Neighbour zeigen,
- bei Randflächen vom Owner aus dem Rechengebiet heraus zeigen.

Bei internen Flächen gilt:

\[
\mathbf S_f\cdot(\mathbf x_N-\mathbf x_O)>0.
\]

Falls diese Bedingung verletzt ist, werden Punktreihenfolge und Flächenvektor umgedreht.

### Reparaturmodus

Der Mesher soll zwei Modi anbieten:

```cpp
enum class ValidationMode {
  kStrict,       // Fehler abbrechen.
  kRepairSafe    // Nur eindeutig sichere Reparaturen durchführen.
};
```

Sichere Reparaturen:

- identische aufeinanderfolgende Punkte entfernen
- Flächenorientierung umkehren
- unbenutzte Punkte entfernen
- Patchflächen gruppieren
- eindeutige Duplikate beseitigen

Topologische Mehrdeutigkeiten dürfen nicht stillschweigend repariert werden.

---

## 7. Geometrieberechnung

Für jede Fläche werden berechnet:

- Flächenzentrum \(\mathbf x_f\)
- orientierter Flächenvektor \(\mathbf S_f\)
- Fläche \(A_f=|\mathbf S_f|\)
- Einheitsnormale \(\hat{\mathbf S}_f\)

Für jede Zelle:

- Zellzentrum \(\mathbf x_i\)
- Volumen \(V_i\)
- umschließende AABB
- charakteristische Länge
- minimale und maximale Flächenentfernung
- optional Cut-Cell-Volumenanteil

Beliebige Polygonflächen müssen robust trianguliert werden. Die Volumen- und Schwerpunktberechnung soll über orientierte Tetraederbeiträge erfolgen.

Numerische Anforderungen:

- Skalen müssen von Mikrometern bis Kilometern funktionieren.
- Toleranzen werden relativ zur Bounding-Box-Diagonale bestimmt.
- Bei Summen mit stark unterschiedlichen Größenordnungen ist kompensierte Summation zu verwenden.
- Negative oder nicht endliche Volumina sind Fehler.

---

## 8. DiFVM-Graphrepräsentation

Das kompilierte Netz verwendet statische Structure-of-Arrays-Daten.

```cpp
struct CompiledMesh {
  std::uint64_t cell_count;
  std::uint64_t internal_face_count;
  std::uint64_t boundary_face_count;
  std::uint64_t face_count;

  AlignedVector<std::uint64_t, 64> owner;
  AlignedVector<std::uint64_t, 64> neighbour;

  AlignedVector<double, 64> cell_center_x;
  AlignedVector<double, 64> cell_center_y;
  AlignedVector<double, 64> cell_center_z;
  AlignedVector<double, 64> cell_volume;

  AlignedVector<double, 64> face_center_x;
  AlignedVector<double, 64> face_center_y;
  AlignedVector<double, 64> face_center_z;

  AlignedVector<double, 64> face_area_x;
  AlignedVector<double, 64> face_area_y;
  AlignedVector<double, 64> face_area_z;
  AlignedVector<double, 64> face_area_magnitude;

  AlignedVector<std::int32_t, 64> boundary_patch_id;

  MeshErrorMap error_map;
  MeshPermutation permutation;
};
```

Interne Flächen werden zuerst gespeichert, danach Randflächen. `neighbour` enthält nur Einträge für interne Flächen oder alternativ einen klar dokumentierten Sentinel für Randflächen.

Zusätzlich soll optional ein gerichteter Graph erzeugt werden:

```text
src_cell[e]
dst_cell[e]
source_face[e]
orientation_sign[e]
```

Eine interne Fläche erzeugt dabei zwei gerichtete Kanten:

```text
owner -> neighbour, sign = +1
neighbour -> owner, sign = -1
```

Eine Randfläche erzeugt eine Kante vom Randzustand beziehungsweise Ghost-Knoten zum Owner.

---

## 9. Vorberechnete FVM-Geometrie

Für jede interne Fläche mit Owner \(O\) und Neighbour \(N\):

\[
\mathbf d_f=\mathbf x_N-\mathbf x_O,
\qquad
\hat{\mathbf d}_f=
\frac{\mathbf d_f}{|\mathbf d_f|}.
\]

Der Nichtorthogonalitätswinkel ist:

\[
\cos\theta_f=
\frac{|\mathbf S_f\cdot\mathbf d_f|}
{|\mathbf S_f|\,|\mathbf d_f|},
\qquad
\theta_f=\arccos(\operatorname{clamp}(\cos\theta_f,0,1)).
\]

Die over-relaxed orthogonale Zerlegung wird vorberechnet:

\[
\boldsymbol{\Delta}_f=
\frac{|\mathbf S_f|}
{\max(|\hat{\mathbf S}_f\cdot\hat{\mathbf d}_f|,\epsilon)}
\hat{\mathbf d}_f,
\]

\[
\mathbf k_f=\mathbf S_f-\boldsymbol{\Delta}_f.
\]

Zu speichernde Arrays:

```text
d_x, d_y, d_z
distance
inverse_distance
interpolation_weight
delta_x, delta_y, delta_z
correction_x, correction_y, correction_z
nonorthogonality_angle
```

---

## 10. Skewness-Berechnung

Der Schnittpunkt der Zellverbindungslinie mit der Flächenebene wird berechnet als:

\[
\mathbf x_{\text{int}}
=
\mathbf x_O+t_f(\mathbf x_N-\mathbf x_O),
\]

\[
t_f=
\frac{(\mathbf x_f-\mathbf x_O)\cdot\mathbf S_f}
{(\mathbf x_N-\mathbf x_O)\cdot\mathbf S_f}.
\]

Der Skewness-Vektor ist:

\[
\mathbf m_f^{\text{skew}}
=
\mathbf x_f-\mathbf x_{\text{int}}.
\]

Zu speichern:

```text
skew_x, skew_y, skew_z
skew_magnitude
normalized_skewness
```

Die normalisierte Skewness soll dimensionslos sein, beispielsweise:

\[
s_f=
\frac{|\mathbf m_f^{\text{skew}}|}
{\max(|\mathbf d_f|,\epsilon)}.
\]

Für Randflächen ist ein geometrisch sinnvoller Owner-zu-Flächen-Stencil zu verwenden.

---

## 11. `MeshErrorMap`

```cpp
struct MeshErrorMap {
  AlignedVector<double, 64> nonorthogonality_rad;
  AlignedVector<double, 64> skew_x;
  AlignedVector<double, 64> skew_y;
  AlignedVector<double, 64> skew_z;
  AlignedVector<double, 64> normalized_skewness;

  AlignedVector<double, 64> delta_x;
  AlignedVector<double, 64> delta_y;
  AlignedVector<double, 64> delta_z;

  AlignedVector<double, 64> correction_x;
  AlignedVector<double, 64> correction_y;
  AlignedVector<double, 64> correction_z;

  AlignedVector<double, 64> face_beta;
  AlignedVector<double, 64> cell_omega;

  AlignedVector<double, 64> cell_volume_ratio;
  AlignedVector<double, 64> cut_volume_fraction;
  AlignedVector<std::uint32_t, 64> quality_flags;
};
```

### Flächenfaktor \(\beta_f\)

`face_beta` liegt in \([0,1]\):

- `1`: volle höherwertige Rekonstruktion möglich
- `0`: auf robuste Upwind-/Erstordnungsbehandlung zurückfallen

Der Wert wird glatt aus Nichtorthogonalität und Skewness berechnet. Keine harte Verzweigung im späteren Solver.

Beispiel:

\[
\beta_{\theta,f}
=
\frac12
\left[
1-\tanh
\left(
a_\theta
\frac{\theta_f-\theta_{\text{crit}}}
{\theta_{\text{max}}-\theta_{\text{crit}}}
\right)
\right],
\]

\[
\beta_{s,f}
=
\frac12
\left[
1-\tanh
\left(
a_s
\frac{s_f-s_{\text{crit}}}
{s_{\text{max}}-s_{\text{crit}}}
\right)
\right],
\]

\[
\beta_f=\operatorname{clamp}
(\beta_{\theta,f}\beta_{s,f},0,1).
\]

Alle Grenzwerte müssen konfigurierbar sein.

### Zellfaktor \(\omega_i\)

`cell_omega` liegt in \((0,1]\) und wird aus folgenden Kriterien gebildet:

- minimale angrenzende `face_beta`
- Volumensprung zu Nachbarzellen
- Cut-Cell-Volumenanteil
- maximale Nichtorthogonalität
- maximale Skewness
- minimale Zellgröße

Kritische Zellen erhalten kleine \(\omega_i\), gute Zellen \(\omega_i=1\).

### Qualitätsflags

Bitmaske, beispielsweise:

```cpp
enum class CellQualityFlag : std::uint32_t {
  kNone = 0,
  kHighNonOrthogonality = 1u << 0,
  kHighSkewness = 1u << 1,
  kSmallVolume = 1u << 2,
  kLargeVolumeJump = 1u << 3,
  kCutCell = 1u << 4,
  kSliver = 1u << 5
};
```

---

## 12. Zell- und Flächenreihenfolge

Für cachefreundliche Gather-/Scatter-Operationen soll eine optionale Neuordnung implementiert werden:

- Morton/Z-Order anhand der Zellzentren
- Reverse Cuthill-McKee anhand des Zellgraphen
- unveränderte Reihenfolge als Referenzmodus

Bei Neuordnung müssen alle Indizes konsistent aktualisiert werden.

Auszugeben sind:

```text
old_to_new_cell
new_to_old_cell
old_to_new_face
new_to_old_face
```

Interne Flächen sollen nach Möglichkeit so sortiert werden, dass benachbarte Zellen räumlich und im Speicher nahe liegen.

---

## 13. Octree-Cut-Cell-Mesher

Die optionale Netzerzeugung verwendet eine adaptive Octree-Struktur.

### Pipeline

1. STL/OBJ laden.
2. Dreiecke validieren.
3. Oberflächen-BVH aufbauen.
4. globale AABB bestimmen.
5. Wurzelzelle mit konfigurierbarem Padding erzeugen.
6. Zellen anhand von Oberflächennähe und Krümmung verfeinern.
7. 2:1-Balancing durchführen.
8. Zellen als innen, außen oder geschnitten klassifizieren.
9. geschnittene Zellen mit Oberflächendreiecken clippen.
10. Cut-Cell-Polygone und Polyeder erzeugen.
11. sehr kleine Cut Cells agglomerieren.
12. Boundary-Patches aus Oberflächenregionen übernehmen.
13. erzeugtes Netz durch `DifvmMeshCompiler` laufen lassen.

### Klassifikation

Die Innen-/Außenklassifikation soll robust erfolgen über:

- BVH-beschleunigte Ray-Casts mit mehreren Richtungen oder
- Generalized Winding Number

Mehrdeutige Punkte müssen deterministisch behandelt werden.

### Verfeinerungskriterien

- Abstand zur Oberfläche
- lokale Oberflächenkrümmung
- benutzerdefinierte Refinement-Boxen
- Patch-spezifische Zielauflösung
- maximale Octree-Tiefe
- minimale Zellgröße

### Cut-Cell-Schutz

Zellen mit

\[
\alpha_i=\frac{V_i^{\text{fluid}}}{V_i^{\text{background}}}
<\alpha_{\min}
\]

werden nicht unverändert ausgegeben. Sie müssen entweder:

- mit einer geeigneten Nachbarzelle agglomeriert werden oder
- durch lokale topologische Anpassung entfernt werden.

Die Agglomeration muss konservative Volumina und eine geschlossene Zellhülle erhalten.

---

## 14. Binäres Zero-Copy-Format

Definiere ein versioniertes binäres Format für direktes Memory-Mapping.

Eigenschaften:

- fester Header
- Magic Number
- Versionsnummer
- Endianness-Markierung
- Datentyp- und Indexbreite
- Anzahl Zellen, Flächen, Kanten und Patches
- 64-Byte-ausgerichtete Array-Offsets
- Prüfsumme
- keine eingebetteten Zeiger
- rückwärtskompatible Versionsprüfung

Beispiel:

```cpp
struct MeshFileHeader {
  char magic[8];                 // "DIFVMMSH"
  std::uint32_t version;
  std::uint32_t endian_marker;
  std::uint64_t cell_count;
  std::uint64_t face_count;
  std::uint64_t internal_face_count;
  std::uint64_t boundary_face_count;
  std::uint64_t directed_edge_count;
  std::uint64_t section_table_offset;
  std::uint64_t checksum;
};
```

---

## 15. Öffentliche C++-API

```cpp
namespace difvm::mesh {

struct CompileOptions {
  ValidationMode validation_mode = ValidationMode::kStrict;
  ReorderingMethod reordering = ReorderingMethod::kMorton;
  bool build_directed_graph = true;
  bool compute_error_map = true;
  bool deterministic = false;
};

struct CompileResult {
  CompiledMesh mesh;
  MeshQualityReport quality;
  std::vector<Diagnostic> diagnostics;
};

CompileResult CompileMesh(
    const RawMesh& raw_mesh,
    const CompileOptions& options);

RawMesh ReadOpenFoamPolyMesh(std::string_view directory);
RawMesh ReadVtu(std::string_view file);
void WriteVtu(const CompiledMesh& mesh, std::string_view file);
void WriteBinaryMesh(const CompiledMesh& mesh, std::string_view file);

}  // namespace difvm::mesh
```

---

## 16. C-ABI

Für Rust, Fortran und Python muss eine stabile C-Schnittstelle vorhanden sein:

```c
typedef struct difvm_mesh_handle difvm_mesh_handle;

typedef struct {
  uint64_t cell_count;
  uint64_t face_count;
  uint64_t internal_face_count;

  const uint64_t* owner;
  const uint64_t* neighbour;

  const double* cell_center_x;
  const double* cell_center_y;
  const double* cell_center_z;
  const double* cell_volume;

  const double* face_area_x;
  const double* face_area_y;
  const double* face_area_z;

  const double* face_beta;
  const double* cell_omega;
} difvm_mesh_view;

int difvm_mesh_load(
    const char* path,
    difvm_mesh_handle** output);

int difvm_mesh_get_view(
    const difvm_mesh_handle* handle,
    difvm_mesh_view* output);

void difvm_mesh_destroy(
    difvm_mesh_handle* handle);
```

Die C-API darf keine C++-Exceptions über die ABI-Grenze propagieren.

---

## 17. Qualitätsbericht

Der Mesher muss einen strukturierten Bericht erzeugen:

```text
cell_count
face_count
internal_face_count
boundary_face_count
patch_count

min/max/mean cell volume
min/max face area
maximum nonorthogonality
p95/p99 nonorthogonality
maximum normalized skewness
p95/p99 skewness
minimum face_beta
minimum cell_omega
minimum cut-cell fraction
negative volume count
degenerate face count
non-manifold face count
agglomerated cut-cell count
```

Optional als JSON:

```json
{
  "cells": 1000000,
  "faces": 3021457,
  "max_nonorthogonality_deg": 71.2,
  "p99_nonorthogonality_deg": 43.6,
  "max_skewness": 0.81,
  "critical_cells": 127,
  "valid": true
}
```

---

## 18. Performanceanforderungen

Der Mesh-Compiler soll:

- lineare oder nahezu lineare Laufzeit in der Anzahl der Flächen erreichen,
- Geometriearrays nur einmal final allokieren,
- temporäre Allokationen in Flächenschleifen vermeiden,
- parallele Flächen- und Zellberechnung unterstützen,
- mindestens mehrere Millionen Zellen auf einer Standard-CPU verarbeiten können,
- für wiederholte Solverläufe nur das binäre, bereits kompilierte Netz laden müssen.

Zielwerte auf einer modernen Desktop-CPU:

- mindestens \(5\times10^6\) Flächen/s bei geometrischer Vorberechnung,
- mindestens \(10^6\) Zellen/s bei Qualitätsmetriken,
- Memory-Mapped-Laden eines bereits kompilierten Netzes ohne erneute Geometrieberechnung.

Diese Werte sind zu benchmarken, nicht hart zu garantieren.

---

## 19. Tests und Abnahmekriterien

### Geometrische Tests

1. Einheitswürfel:
   - Volumen exakt \(1\) innerhalb numerischer Toleranz
   - Gesamtfläche \(6\)
   - Zellzentrum \((0.5,0.5,0.5)\)

2. Reguläres Hexaedernetz:
   - Nichtorthogonalität nahe \(0^\circ\)
   - Skewness nahe null
   - `face_beta` nahe 1
   - `cell_omega` nahe 1

3. Absichtlich schiefes Netz:
   - erwartete Nichtorthogonalität analytisch überprüfbar
   - korrekter Skewness-Vektor

4. Tetraedernetz:
   - positive Zellvolumina
   - korrekte Owner-/Neighbour-Orientierung

5. Hybridnetz:
   - Tetraeder, Hexaeder, Prisma und Pyramide gemeinsam

6. Fehlerhafte Orientierung:
   - automatische sichere Korrektur im Repair-Modus
   - Fehler im Strict-Modus

7. Degenerierte Flächen:
   - zuverlässige Diagnose
   - keine NaN-Werte in der Ausgabe

8. Octree-Cut-Cell-Test:
   - Kugel in Würfel
   - monotone Volumenkonvergenz bei Verfeinerung
   - keine negativen Volumina
   - keine offenen Zellen

### Erhaltungstests

Für jede geschlossene Zelle muss gelten:

\[
\left|\sum_{f\in F(i)}\mathbf S_{f,i}\right|
<\epsilon_S.
\]

Das summierte Zellvolumen muss dem Domänenvolumen innerhalb einer konfigurierbaren Toleranz entsprechen.

### Roundtrip-Test

```text
OpenFOAM → RawMesh → CompiledMesh → Binary → Reload
```

Alle Topologie-, Geometrie- und Patchdaten müssen erhalten bleiben.

---

## 20. Ausdrückliche Nicht-Ziele

Nicht implementieren:

- Navier–Stokes-Solver
- PISO/SIMPLE
- Rhie-Chow-Interpolation
- lineare Gleichungslöser
- automatische Differentiation durch diskrete Topologieänderungen
- SIMP, Level-Set oder Adjoint
- Turbulenz-, Wärme- oder Mehrphasenmodelle
- GUI und WebSocket-Streaming
- Quantum-Backends
- OpenFOAM als Laufzeitabhängigkeit

Der Mesher berechnet lediglich alle statischen Daten, die solche Solver später benötigen.

---

## 21. Implementierungsreihenfolge

### Meilenstein 1: Mesh Compiler

- `RawMesh`
- OpenFOAM-Import
- Validierung
- Flächengeometrie
- Zellvolumen und Zellzentrum
- Owner-/Neighbour-Korrektur
- `CompiledMesh`

### Meilenstein 2: DiFVM-Geometrie

- \(\mathbf d_f\)
- Interpolationsgewichte
- \(\boldsymbol{\Delta}_f\)
- \(\mathbf k_f\)
- Nichtorthogonalität
- Skewness
- gerichteter Graph

### Meilenstein 3: `MeshErrorMap`

- `face_beta`
- `cell_omega`
- Qualitätsflags
- JSON-Qualitätsbericht

### Meilenstein 4: Performance

- 64-Byte-ausgerichtete SoA-Arrays
- parallele Geometriekernels
- Morton- und RCM-Reihenfolge
- binäres Memory-Mapped-Format
- Benchmarks

### Meilenstein 5: Octree-Cut-Cell-Generator

- STL/OBJ
- BVH
- adaptives Octree
- 2:1-Balancing
- Cut-Cell-Erzeugung
- Small-Cell-Agglomeration
- Patchübernahme

---

### Empfohlene erste lauffähige Version

Für ein belastbares MVP sollte zuerst ausschließlich Folgendes implementiert werden:

```text
OpenFOAM polyMesh / VTU
        ↓
Validierung und Orientierung
        ↓
Zell- und Flächengeometrie
        ↓
DiFVM owner/neighbour-Graph
        ↓
Non-Orthogonality + Skewness
        ↓
MeshErrorMap
        ↓
Morton-Reordering
        ↓
64-Byte-SoA-Binärdatei
```

Das ist der Teil, der direkt aus dem DiFVM-Konzept ableitbar ist. Der Octree-Cut-Cell-Netzgenerator sollte danach als getrenntes Modul gebaut werden, weil er wesentlich komplexer ist und im DiFVM-Paper selbst nicht spezifiziert wird.