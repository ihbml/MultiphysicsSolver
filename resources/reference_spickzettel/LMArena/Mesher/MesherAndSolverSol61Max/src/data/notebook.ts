import geometryCode from '../snippets/face_geometry.hpp?raw';
import layoutCode from '../snippets/face_layout.hpp?raw';
import baseCode from '../snippets/base_coefficients.cu?raw';
import correctionCode from '../snippets/correction_flux.cu?raw';
import gatherCode from '../snippets/gather_residual.cu?raw';
import haloCode from '../snippets/halo_schedule.cpp?raw';
import referenceCode from '../snippets/flux_reference.hpp?raw';
import testCode from '../snippets/reference_test.cpp?raw';
import petscCode from '../snippets/solver.options?raw';

export type CategoryId = 'mesh' | 'gpu' | 'multiphysics' | 'scaling' | 'verification';
export type ViewId = 'overview' | 'review' | 'snippets' | 'libraries' | 'architecture' | 'saved' | CategoryId;
export const categories: { id: CategoryId; title: string; short: string; description: string }[] = [
  { id: 'mesh', title: 'Mesh & Geometrie', short: 'MESHING', description: 'Von robuster Geometrie zu gezielter Adaption.' },
  { id: 'gpu', title: 'GPU & Performance', short: 'GPU COMPUTING', description: 'Weniger Datenbewegung. Klarere Kernel. Messbarer Fortschritt.' },
  { id: 'multiphysics', title: 'Multiphysics', short: 'COUPLED PHYSICS', description: 'Physikalische Schnittstellen, die mehr als Daten austauschen.' },
  { id: 'scaling', title: 'Distributed Computing', short: 'DISTRIBUTED SYSTEMS', description: 'Explizite Abh\u00e4ngigkeiten statt unsichtbarer Kommunikation.' },
  { id: 'verification', title: 'Validierung & Tests', short: 'VERIFICATION', description: 'Erst nachweisen, dann beschleunigen.' },
];

export interface Snippet {
  id: string;
  filename: string;
  title: string;
  language: 'cpp' | 'bash';
  label: string;
  category: CategoryId;
  description: string;
  code: string;
  status: 'CPU-getestet' | 'CUDA-Beispiel' | 'Pseudocode' | 'Konfiguration';
  note: string;
}

export const snippets: Snippet[] = [
  { id: 'geometry', filename: 'face_geometry.hpp', title: 'Robuste Fl\u00e4chengeometrie', language: 'cpp', label: 'C++20', category: 'mesh', description: 'Dimensionslose Qualit\u00e4tsma\u00dfe, Ebenenschnitt und ein expliziter Invalid-Pfad.', code: geometryCode, status: 'CPU-getestet', note: 'Mit reference_test.cpp getestet. Extremf\u00e4lle und Einheiten m\u00fcssen an den eigenen Geometriekern angepasst werden.' },
  { id: 'layout', filename: 'face_layout.hpp', title: 'Kompaktes SoA-Speicherlayout', language: 'cpp', label: 'C++20', category: 'gpu', description: 'Basisdaten f\u00fcr alle Fl\u00e4chen, separate Korrekturdaten und lokal besessene Zellzeilen.', code: layoutCode, status: 'CPU-getestet', note: 'Kernel-Interface ohne Speicherverwaltung. Bei mehr als 2^31 lokalen Eintr\u00e4gen Index verbreitern.' },
  { id: 'base', filename: 'base_coefficients.cu', title: 'Basisfluss auf allen Fl\u00e4chen', language: 'cpp', label: 'CUDA', category: 'gpu', description: 'Compile-time Upwind oder Central. Vier konservative Koeffizienten pro Fl\u00e4che.', code: baseCode, status: 'CUDA-Beispiel', note: 'Nicht mit nvcc ausgef\u00fchrt. Physikalische Randbedingungen, Zeit- und Quellterme kommen separat hinzu.' },
  { id: 'correction', filename: 'correction_flux.cu', title: 'Sparse Geometriekorrektur', language: 'cpp', label: 'CUDA', category: 'gpu', description: 'Explizite Skewness- und Nichtorthogonalit\u00e4tskorrektur f\u00fcr zentrale Konvektion.', code: correctionCode, status: 'CUDA-Beispiel', note: 'Gesamtes Ausgabefeld vorher auf null setzen. Nachbargradienten und Materialwerte m\u00fcssen aktuell sein. Nicht mit unver\u00e4nderter Upwind-Rekonstruktion kombinieren.' },
  { id: 'gather', filename: 'gather_residual.cu', title: 'Deterministischer Cell-Gather', language: 'cpp', label: 'CUDA', category: 'gpu', description: 'Eine Zellzeile pro Thread statt konkurrierender Face-Scatter-Atomics.', code: gatherCode, status: 'CUDA-Beispiel', note: 'Feste Reihenfolge ist lokal reproduzierbar, aber keine Garantie f\u00fcr bitgleiche Ergebnisse auf verschiedenen GPUs oder MPI-Zerlegungen.' },
  { id: 'halo', filename: 'halo_schedule.cpp', title: 'Halo-Abh\u00e4ngigkeiten sichtbar machen', language: 'cpp', label: 'MPI / CUDA', category: 'scaling', description: 'Feld- und Gradienten-Halos mit explizitem GPU/MPI-Handoff.', code: haloCode, status: 'Pseudocode', note: 'Orchestrierungsskizze mit applikationsspezifischen Hilfsfunktionen. Kein eigenst\u00e4ndig kompilierbares MPI-Programm.' },
  { id: 'reference', filename: 'flux_reference.hpp', title: 'Lesbare CPU-Flussreferenz', language: 'cpp', label: 'C++20', category: 'verification', description: 'Ein kleiner Referenzoperator als Gegenprobe f\u00fcr optimierte GPU-Kernels.', code: referenceCode, status: 'CPU-getestet', note: 'Ben\u00f6tigt face_geometry.hpp. Modelliert interne Fl\u00e4chen mit skalarer, nichtnegativer Diffusivit\u00e4t.' },
  { id: 'tests', filename: 'reference_test.cpp', title: 'Konstanz, Linearit\u00e4t, Erhaltung', language: 'cpp', label: 'C++20', category: 'verification', description: 'Ausf\u00fchrbare Referenztests f\u00fcr den korrigierten Fluss und die Geometrieklassifizierung.', code: testCode, status: 'CPU-getestet', note: 'Die drei Dateien face_geometry.hpp, flux_reference.hpp und reference_test.cpp in denselben Ordner laden. Befehl: c++ -std=c++20 -O2 reference_test.cpp -o reference_test && ./reference_test' },
  { id: 'petsc', filename: 'solver.options', title: 'PETSc: ehrliche Solver-Diagnostik', language: 'bash', label: 'PETSc', category: 'scaling', description: 'True residual, Abbruchgrund und Profiling vor Pipeline-Experimenten aktivieren.', code: petscCode, status: 'Konfiguration', note: 'Startkonfiguration, kein universeller Preconditioner. GAMG-Eignung und Backend-Unterst\u00fctzung am eigenen Operator pr\u00fcfen.' },
];

export interface Section {
  heading: string;
  body: string[];
  points?: string[];
  formula?: string;
  snippet?: string;
}
export interface Article {
  id: string;
  title: string;
  summary: string;
  category: CategoryId;
  type: 'Fundament' | 'Architektur' | 'Experiment';
  minutes: number;
  sections: Section[];
  sources: { label: string; url: string }[];
}

export const articles: Article[] = [
  {
    id: 'two-class-dispatch', title: 'Zwei Pfade. Ein konservativer Fluss.',
    summary: 'Das Zwei-Klassen-Dispatching richtig aufbauen: Basisfluss f\u00fcr alle, Geometriekorrektur nur bei Bedarf.',
    category: 'gpu', type: 'Fundament', minutes: 8,
    sections: [
      { heading: 'Die wichtigste Korrektur zuerst', body: ['Im urspr\u00fcnglichen Ablauf fehlt der Basisoperator auf den korrigierten Fl\u00e4chen. Segmente 1 und 3 brauchen nicht nur einen Korrekturfluss, sondern auch denselben impliziten Zwei-Punkt-Anteil wie Segmente 0 und 2. Sonst fehlen Kopplungen in der Matrix.', 'Die Aufteilung lautet deshalb: Basis auf allen vier Segmenten; Korrektur ausschlie\u00dflich auf der kompakten Teilmenge. Physikalische Randfl\u00e4chen werden separat behandelt.'], formula: 'F_f = F_base,f + F_corr,f   |   base: alle Fl\u00e4chen   |   corr: nur Teilmenge', snippet: 'base' },
      { heading: 'Zentrale Konvektion ist nicht automatisch stabil', body: ['Die zentrale Interpolation kann bei hohem Zell-Peclet-Wert nichtmonotone L\u00f6sungen erzeugen, auch auf einem perfekten Netz. F\u00fcr eine robuste Baseline ist Upwind sinnvoll; h\u00f6herwertige Konvektion ben\u00f6tigt einen konsistenten Limiter und eine eigene Rekonstruktion.', 'Der gezeigte Skewness-Term geh\u00f6rt zur zentralen Interpolation. Ihn einfach auf Upwind aufzuschlagen ergibt nicht automatisch ein konsistentes High-Order-Verfahren. Ein orthogonales Netz spart au\u00dferdem nicht die Gradienten, die ein MUSCL- oder Turbulenzmodell weiterhin ben\u00f6tigt.'], snippet: 'correction' },
      { heading: 'Taylor-N\u00e4herung statt exakter Identit\u00e4t', body: ['grad(phi)_f \u00b7 d = phi_N - phi_P gilt f\u00fcr lineare Felder exakt, f\u00fcr allgemeine Felder aber nur als diskrete N\u00e4herung. Auch kleine Geometrieschwellwerte machen einen Korrekturterm nicht mathematisch null.', 'Wird omega dauerhaft kleiner als eins gesetzt, bleibt die Flusspaarung zwar konservativ, aber die vollst\u00e4ndige lineare Geometriekorrektur geht verloren. D\u00e4mpfung braucht deshalb einen dokumentierten Genauigkeits- und Konvergenztest.'] },
      { heading: 'Ein Fluss, zwei Vorzeichen', body: ['Berechne den orientierten Fluss einmal pro Fl\u00e4che. Der Owner erh\u00e4lt +F, der Neighbour -F. Die explizite rechte Seite verwendet entsprechend -F_corr und +F_corr. Eine feste Cell-Gather-Reihenfolge vermeidet konkurrierende Schreibzugriffe.'], snippet: 'reference' },
    ],
    sources: [{ label: 'OpenFOAM: skewCorrectedSnGrad', url: 'https://www.openfoam.com/documentation/guides/v2206/api/classFoam_1_1fv_1_1skewCorrectedSnGrad.html' }, { label: 'CUDA Best Practices Guide', url: 'https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html' }],
  },
  {
    id: 'mesh-quality', title: 'Netzqualit\u00e4t ist noch kein Fehlerma\u00df.',
    summary: 'Die MeshErrorMap in Geometrie, L\u00f6sungsindikatoren und belastbare Fehlersch\u00e4tzer aufteilen.',
    category: 'mesh', type: 'Fundament', minutes: 6,
    sections: [
      { heading: 'Drei Karten statt einer vermeintlichen Wahrheit', body: ['Skewness, Nichtorthogonalit\u00e4t und Zellvolumen beschreiben Geometrie. Sie liefern weder einen Prozentwert des Diskretisierungsfehlers noch automatisch die optimale Verfeinerung. Ein schlechtes Netz kann ein konstantes Feld exakt abbilden; ein perfektes Netz kann eine Flamme v\u00f6llig unteraufl\u00f6sen.'], points: ['GeometryMap: Volumen, Jacobi-Determinante, relative Skewness und Nichtorthogonalit\u00e4t.', 'SolutionIndicator: Gradienten, Kr\u00fcmmung, Spr\u00fcnge und physikalische L\u00e4ngenskalen.', 'ErrorEstimator: Residuen, Rekonstruktion oder zielgr\u00f6\u00dfenbasierte Sch\u00e4tzung mit dokumentierten Annahmen.'] },
      { heading: 'Dimensionslos klassifizieren', body: ['Vergleiche den Skewness-Abstand mit einer lokalen L\u00e4ngenskala und den Korrekturvektor mit der Fl\u00e4chengr\u00f6\u00dfe. Absolute Epsilon-Werte in Metern sind zwischen Mikrokanal und Windpark nicht \u00fcbertragbar.', 'Pr\u00fcfe zuerst positive Volumina, Fl\u00e4chenorientierung und d \u00b7 S > 0. Negative oder degenerierte Geometrie geh\u00f6rt in einen expliziten Fehlerpfad, nicht hinter einen Clamp im Nenner. Beta au\u00dferhalb [0,1] ist gesondert zu diagnostizieren.'], formula: 'q_skew = ||x_f - x_int|| / h_ref     q_nonortho = ||S - alpha d|| / ||S||', snippet: 'geometry' },
      { heading: 'Weitere sinnvolle Anwendungen', body: ['Nutze die Geometriekarte f\u00fcr gezielte lokale Reparaturen, Kernel-Buckets, Rekonstruktions-Konditionszahlen und Warnungen bei deformierenden Netzen. F\u00fcr Materialoptimierung ist ein konsistenter Filter im Optimierungsproblem besser als ein nachtr\u00e4gliches, willk\u00fcrliches Gewichten des Adjoint-Gradienten.'] },
    ], sources: [{ label: 'OpenFOAM: Mesh quality', url: 'https://www.openfoam.com/documentation/guides/latest/doc/guide-meshing-snappyhexmesh-meshquality.html' }],
  },
  {
    id: 'halo-contract', title: 'Der Halo ist ein Datenvertrag.',
    summary: 'Warum ein phi-Halo nicht ausreicht und CUDA-Streams den MPI-Fortschritt nicht garantieren.',
    category: 'scaling', type: 'Architektur', minutes: 7,
    sections: [
      { heading: 'Vom Stencil zur Kommunikationsliste', body: ['Bestimme f\u00fcr jeden Operator, welche Nachbarwerte er wirklich liest. Der Zwei-Punkt-Fluss braucht Zust\u00e4nde und Materialkoeffizienten. Der korrigierte Fluss braucht zus\u00e4tzlich konsistente Gradienten auf beiden Seiten.', 'Ein Austausch von phi liefert nicht automatisch den Gradienten der entfernten Zelle. Du brauchst entweder einen zus\u00e4tzlichen Gradienten-Halo oder gen\u00fcgend tiefe Feld-Halos, um den fremden Gradienten identisch zu rekonstruieren. Die Zahl der Schichten folgt aus dem Stencil, nicht aus einer festen Faustregel.'] },
      { heading: 'Inner ist eine Eigenschaft des ganzen Stencils', body: ['Zwei lokal besessene Endzellen machen eine Fl\u00e4che noch nicht kommunikationsfrei. Wenn die Gradientenrekonstruktion einer dieser Zellen Ghost-Werte liest, muss auch diese innere Fl\u00e4che auf den Halo warten.', 'Baue die unabh\u00e4ngigen und abh\u00e4ngigen Arbeitslisten aus der vollst\u00e4ndigen Operatorabh\u00e4ngigkeit. Nach jeder Topologie\u00e4nderung, Stencil-\u00c4nderung oder Repartitionierung m\u00fcssen die Listen neu validiert werden.'] },
      { heading: 'GPU, MPI und Fortschritt explizit verbinden', body: ['CUDA-aware MPI bedeutet nicht generell stream-aware MPI. Vor einem MPI_Send muss der Pack-Kernel fertig sein; nach einem Empfang muss die dokumentierte Sichtbarkeits- und Synchronisationsgarantie der MPI-Implementierung beachtet werden.', 'Der portable Ausgangspunkt synchronisiert ein CUDA-Event auf dem Host vor dem MPI-Handoff. Auch zwischen zwei GPU-Streams m\u00fcssen Abh\u00e4ngigkeiten mit Events verkn\u00fcpft werden. Erst danach lohnen sich implementierungsspezifische Stream-Integrationen und Progress-Threads.'], snippet: 'halo' },
      { heading: 'Nur lokal besessene Zeilen schreiben', body: ['Ghost-Zellen sind Lesekopien, keine regul\u00e4ren CSR-Zeilen des eigenen Ranks. Ein blindes atomicAdd auf rhs[N] kann bei einer Rank-Grenze falsche Daten aktualisieren.', 'Lege eine eindeutige Zust\u00e4ndigkeit pro gemeinsamer Fl\u00e4che fest und tausche den berechneten Fluss aus. Eine doppelte Auswertung ist nur dann gleichwertig, wenn Orientierung, Rekonstruktion, Rundung und Zeitstand konsistent sind.'] },
      { heading: 'Pipeline ist nicht gleich s-step', body: ['Pipelined Krylov \u00fcberlappt globale Reduktionen mit Arbeit. Kommunikationsvermeidende s-step-Verfahren b\u00fcndeln mehrere Schritte und haben andere Speicher- und Stabilit\u00e4tskosten. Zus\u00e4tzliche Halo-Schichten allein ersetzen weder globale Skalarprodukte noch einen geeigneten Algorithmus.'], snippet: 'petsc' },
    ], sources: [{ label: 'PETSc: Pipelined Krylov methods', url: 'https://petsc.org/main/manual/ksp/' }, { label: 'PETSc: MPI asynchronous progress', url: 'https://petsc.org/release/faq/' }],
  },
  {
    id: 'verification-first', title: 'Eine Testpyramide f\u00fcr echte Physik.',
    summary: 'Ein kleiner CPU-Referenzoperator ist wertvoller als eine weitere unbelegte Speedup-Zahl.',
    category: 'verification', type: 'Fundament', minutes: 6,
    sections: [
      { heading: 'Mit Invarianten anfangen', body: ['Teste konstante Felder, lineare Felder auf verzerrten Netzen und die paarweise Flusserhaltung. Pr\u00fcfe au\u00dferdem positive Diffusivit\u00e4t, ung\u00fcltige Fl\u00e4chenorientierung und die Vorzeichen des Upwind-Operators.', 'Diese Referenztests sind bewusst klein und ohne CUDA lauff\u00e4hig. Sie beweisen keinen vollst\u00e4ndigen CFD-Solver, schaffen aber eine klare Gegenprobe f\u00fcr die optimierten Kernel.'], snippet: 'tests' },
      { heading: 'Von Operator-Tests zur Konvergenzstudie', body: ['Erweitere die Tests um Manufactured Solutions mit analytisch erzeugtem Quellterm. Vergleiche L1-, L2- und Linf-Fehler auf systematisch verfeinerten Netzen. Eine reine Residuenreduktion zeigt nur, dass das diskrete System gel\u00f6st wurde, nicht dass die PDE genau approximiert ist.'], points: ['CPU gegen GPU mit skalierten Toleranzen vergleichen.', 'Ein Rank gegen mehrere Ranks bei gleicher physikalischer Aufgabe testen.', 'Randbedingungen, zeitliche Ordnung und bewegte Geometrie gesondert testen.', 'F\u00fcr Chemie: Speziespositivit\u00e4t, Elementbilanzen und Energiekonsistenz \u00fcberwachen.'] },
      { heading: 'Performance reproduzierbar dokumentieren', body: ['Berichte Hardware, Pr\u00e4zision, Problemgr\u00f6\u00dfe, Toleranz, Initialzustand und Gesamtlaufzeit. Vergleiche Zeit bis zur gleichen Genauigkeit, nicht nur Nanosekunden pro Kernel. Behauptungen wie 100x schneller oder 70 Prozent weniger Iterationen bleiben ohne konkreten Benchmark Hypothesen.'] },
    ], sources: [{ label: 'NASA: CFD verification and validation', url: 'https://www.grc.nasa.gov/www/wind/valid/tutorial/tutorial.html' }],
  },
  {
    id: 'guided-adaptation', title: 'Pilot-Physik als Hinweis, nicht als Orakel.',
    summary: 'Grobe Vorl\u00e4ufe und neuronale Metrikfelder sinnvoll nutzen, mit einem klassischen Fallback.',
    category: 'mesh', type: 'Experiment', minutes: 7,
    sections: [
      { heading: 'Der Pilot muss die richtige Physik sehen', body: ['Ein grober CFD-Vorlauf kann Nachlauf, Scherschichten oder thermische Fronten f\u00fcr die Vernetzung sichtbar machen. Blender- und Game-Engine-Fluids sind aber nicht automatisch ein physikalisch kompatibler Startzustand: Randbedingungen, Reynolds-Zahl, Kompressibilit\u00e4t und Turbulenzmodell k\u00f6nnen abweichen.', 'Bevorzuge einen groben Modus desselben Solvers oder ein bewusst validiertes Ersatzmodell. Ein interpoliertes Geschwindigkeitsfeld ben\u00f6tigt unter Umst\u00e4nden eine Druckprojektion; Dichte, Energie und Spezies sollten konservativ und positiv \u00fcbertragen werden.'] },
      { heading: 'Eine dimensionsrichtige Metrik lernen', body: ['Die Heuristik h = c / |curl u| ist ohne Definition der Einheiten und Referenzskalen nicht vollst\u00e4ndig. Normiere Indikatoren und begrenze h zwischen h_min und h_max. F\u00fcr anisotrope Verfeinerung muss die Metrik symmetrisch positiv definit sein.', 'Ein Netz kann einen Cholesky-Faktor vorhersagen: M = L L^T + epsilon I. Danach folgen Eigenwertbegrenzung, Gl\u00e4ttung, CAD-Grenzschichten und eine unabh\u00e4ngige Netzpr\u00fcfung. Kein gelernter Indikator garantiert ein fehlerfreies Netz.'], formula: 'M(x) = L(x) L(x)^T + epsilon I     h_min <= h_i <= h_max' },
      { heading: 'Fehlerindikatoren mit sauberem Lernziel', body: ['Ein konvergiertes diskretes Residuum ist fast null und daher allein kein perfektes Trainingslabel f\u00fcr den Diskretisierungsfehler. Dual-weighted residuals brauchen eine geeignete angereicherte Rekonstruktion und eine klar definierte Zielgr\u00f6\u00dfe.', 'Speichere neben Geometrie und Re/Ma auch Randbedingungen, Materialgesetze, Unsicherheit und das erreichte Fehlerniveau. Au\u00dferhalb der Trainingsverteilung wird auf klassische Adaption zur\u00fcckgeschaltet.'] },
    ], sources: [{ label: 'MMG: anisotropic mesh adaptation', url: 'https://www.mmgtools.org/' }],
  },
  {
    id: 'mixed-precision', title: 'Pr\u00e4zision nach Bedarf, nicht nach Marketing.',
    summary: 'Mixed Precision und Tensor Cores dort einsetzen, wo die numerische Struktur sie wirklich tr\u00e4gt.',
    category: 'gpu', type: 'Experiment', minutes: 6,
    sections: [
      { heading: 'Dichte Batches statt magischer Sparse-Beschleunigung', body: ['Tensor Cores beschleunigen geeignete Matrixprodukte. Eine allgemeine Sparse-FVM-Matrix oder eine steife Chemie-Jacobi-Matrix wird dadurch nicht automatisch schnell. Kleine dichte Reaktorbl\u00f6cke sind ein Kandidat, m\u00fcssen aber passend gebatcht und skaliert werden.', 'Linear l\u00f6sen statt die Jacobi-Matrix explizit invertieren. Nutze geeignete Faktorisierungen, pr\u00fcfe Pivotierung, Kondition und den Aufwand f\u00fcr Padding. Nicht jede GPU hat dieselbe FP64-Tensorleistung.'] },
      { heading: 'Ein kontrollierter Mixed-Precision-Regelkreis', body: ['Berechne das wahre Residuum in hoher Pr\u00e4zision. Nutze niedrigere Pr\u00e4zision im inneren L\u00f6ser nur, solange die Defektkorrektur nachweisbar konvergiert. Bei Stagnation, NaNs oder verlorener Positivit\u00e4t wird hochgeschaltet.', 'Ein variabler oder nichtlinearer Preconditioner legt einen flexiblen Krylov-L\u00f6ser nahe. Die Geometriequalit\u00e4t allein entscheidet nicht \u00fcber die n\u00f6tige Pr\u00e4zision; relevante Faktoren sind auch Konditionierung und physikalische Skalierung.'], formula: 'r_k = b - A x_k  [FP64]     M_low delta = r_k     x_(k+1) = x_k + delta' },
      { heading: 'Komprimierte Halos zuerst isoliert erproben', body: ['Verlustbehaftete Kommunikation ver\u00e4ndert den Operator oder Preconditioner. Beginne mit dem inneren Preconditioner, einem expliziten Fehlerbudget und einem Vergleich gegen unkomprimierte Kommunikation. In einer konservativen Flussbilanz darf eine Kompression keine inkonsistenten Grenzfl\u00fcsse erzeugen.'] },
    ], sources: [{ label: 'Ginkgo documentation', url: 'https://ginkgo-project.github.io/ginkgo/doc/stable/' }, { label: 'PETSc: flexible Krylov solvers', url: 'https://petsc.org/main/manual/ksp/' }],
  },
  {
    id: 'coupling-contracts', title: 'Kopplung braucht eine Energiebilanz.',
    summary: 'Intensive und extensive Gr\u00f6\u00dfen unterscheiden, Zeitschritte synchronisieren und implizite Wiederholungen richtig behandeln.',
    category: 'multiphysics', type: 'Architektur', minutes: 7,
    sections: [
      { heading: 'Die Schnittstelle als physikalischen Vertrag definieren', body: ['Dokumentiere Einheiten, Vorzeichen, Koordinatensystem, Zeitintervall und die Frage, ob ein Wert eine Dichte oder bereits ein integrierter Beitrag ist. Temperatur ist eine intensive Gr\u00f6\u00dfe. Eine nodale Kraft oder integrierte W\u00e4rmeleistung ist extensiv.', 'Bei preCICE erh\u00e4lt consistent eine konstante intensive Gr\u00f6\u00dfe; conservative erh\u00e4lt die nodale Summe. Eine W\u00e4rmestromdichte in W/m^2 braucht f\u00fcr Integral-Erhaltung Fl\u00e4chengewichte oder ein geeignetes scaled-consistent-surface-Mapping, nicht blind conservative.'] },
      { heading: 'Rollback ist Teil des Algorithmus', body: ['Implizite partitionierte Kopplung kann denselben Zeitschritt mehrfach ausf\u00fchren. Speichere vor jedem Kopplungsfenster alle relevanten Zust\u00e4nde: L\u00f6sungsfelder, Chemiehistorie, Partikel, Meshpositionen und zeitabh\u00e4ngige Randwerte.', 'Ein zur\u00fcckgesetztes CFD-Feld bei weitergelaufener Chemie ist kein g\u00fcltiger Checkpoint. Kontrolliere pro Fenster die Energiebilanz und das Kopplungsresiduum; Quasi-Newton-Beschleunigung ersetzt keine physikalisch korrekte Schnittstelle.'] },
      { heading: 'Weitere n\u00fctzliche Bausteine', body: ['F\u00fcr eine modulare Kopplung ist preCICE der erste Kandidat. MUI bietet flexible r\u00e4umlich-zeitliche Abtastung mit MPI. ArborX beschleunigt geometrische Suchabfragen, stellt aber selbst keine konservative Feld\u00fcbertragung oder vollst\u00e4ndige Kopplungsiteration bereit.'] },
    ], sources: [{ label: 'preCICE: mapping constraints', url: 'https://precice.org/docs/configuration/xml-reference/' }, { label: 'preCICE: mesh exchange', url: 'https://precice.org/docs/configuration/basics/coupling-mesh-exchange/' }],
  },
  {
    id: 'reaction-kinetics', title: 'Chemie beschleunigen, Invarianten behalten.',
    summary: 'Generierter GPU-Code, adaptive Tabulation und kontrolliertes Zustands-Clustering.',
    category: 'multiphysics', type: 'Experiment', minutes: 7,
    sections: [
      { heading: 'Die passende Bibliothek zur Aufgabe', body: ['PelePhysics mit CEPTR generiert Mechanismus-spezifischen C++-Code aus Cantera-Dateien. TChem nutzt Kokkos f\u00fcr thermochemische Berechnungen. Mutation++ adressiert unter anderem Hochtemperatur-Nichtgleichgewicht; Reaktoro chemische Gleichgewichte und reaktive Systeme.', 'Das Vorliegen einer analytischen Jacobi-Matrix garantiert weder Branch-Freiheit noch automatische Enzyme-Kompatibilit\u00e4t. Mechanismus, Transportmodell, Backend und AD-Pfad brauchen einen eigenen Integrationstest.'] },
      { heading: 'Zustands-Clustering mit Akzeptanztest', body: ['\u00c4hnliche thermochemische Zust\u00e4nde k\u00f6nnen tabelliert oder gemeinsam angen\u00e4hert werden. In Z\u00fcnd- und L\u00f6schregionen k\u00f6nnen jedoch kleine Zustandsunterschiede gro\u00dfe Unterschiede im Integrationsresultat ausl\u00f6sen.', 'Die Taylor-R\u00fcckprojektion R(phi_bar) + J delta_phi ist eine lokale N\u00e4herung, keine exakte Zellrate. Verwende normierte Zustandsabst\u00e4nde, eine Fehlertoleranz, Positivit\u00e4ts- und Elementbilanztests sowie einen Fallback auf die vollst\u00e4ndige Integration.'] },
      { heading: 'Zeitskalen und Kosten messen', body: ['Protokolliere ODE-Schritte, verworfene Schritte und Jacobi-Auswertungen pro Zelle. Diese Messwerte eignen sich sowohl f\u00fcr adaptive Chemie-Batches als auch f\u00fcr gewichtete MPI-Partitionierung. Ein Geometrie-basiertes Zellgewicht allein erfasst steife Chemie nicht.'] },
    ], sources: [{ label: 'PelePhysics & CEPTR', url: 'https://github.com/Pele-Suite/PelePhysics' }, { label: 'TChem', url: 'https://github.com/sandialabs/TChem' }, { label: 'Cantera', url: 'https://cantera.org/' }],
  },
  {
    id: 'electromagnetics', title: 'Maxwell braucht Struktur, nicht nur Zellen.',
    summary: 'Kompatible diskrete R\u00e4ume, Skin-Tiefe und die klaren Grenzen von RT-Core-Beschleunigung.',
    category: 'multiphysics', type: 'Fundament', minutes: 8,
    sections: [
      { heading: 'Die Topologie liefert die diskrete Identit\u00e4t', body: ['Bei einer kompatiblen Diskretisierung folgt div(curl) = 0 aus den Inzidenzmatrizen des Zellkomplexes: D C = 0. Das ist eine topologische Eigenschaft, kein exklusiver Vorteil eines orthogonalen Voronoi-Netzes.', 'Materialgesetze stecken in Hodge- beziehungsweise Massenmatrizen. Ihre Positivit\u00e4t, Randbedingungen, Quellen und der Zeitintegrator bleiben entscheidend. In FEM sind H(curl)-konforme Nedelec-Elemente ein etablierter Weg gegen unphysikalische Moden.'], formula: 'D C = 0     |     delta_skin = sqrt(2 / (omega mu sigma))' },
      { heading: 'Welche L\u00e4ngenskalen muss das Netz sehen?', body: ['Skin-Tiefe in guten Leitern, Wellenl\u00e4nge, Materialgrenzen und Randfelder stellen andere Anforderungen als eine reine CFD-Aufgabe. Wie viele Elemente n\u00f6tig sind, h\u00e4ngt von Ordnung und Fehlertoleranz ab.', 'Ein konformes Multi-Region-Netz ist eine Option, keine allgemeine Pflicht. Nichtkonforme Kopplung, BEM und Infinite Elements k\u00f6nnen je nach Problem den Au\u00dfenraum oder die Luftbox anders behandeln.'] },
      { heading: 'RT Cores beschleunigen Geometrie, nicht Maxwell allgemein', body: ['Shooting-and-Bouncing-Rays ist f\u00fcr hochfrequente, elektrisch gro\u00dfe Geometrien geeignet. Es ersetzt keine niederfrequente Wirbelstromsimulation, keine vollwellige Resonanzanalyse und keine allgemeine FEM-L\u00f6sung.', 'Hardware-Raytracing liefert beschleunigte BVH-Traversierung und Schnitttests. Exakte Abst\u00e4nde, Cut-Cell-Volumina, Beugungsmodelle und Phononenstreuung brauchen zus\u00e4tzliche Algorithmen. AMD- und Intel-Hardware wird zudem nicht \u00fcber NVIDIA OptiX angesteuert.'] },
    ], sources: [{ label: 'MFEM: electromagnetic examples', url: 'https://mfem.org/examples/' }, { label: 'NVIDIA OptiX', url: 'https://developer.nvidia.com/rtx/ray-tracing/optix' }],
  },
  {
    id: 'moving-mesh', title: 'Bewegte Geometrie muss Volumen erhalten.',
    summary: 'ALE, Cut Cells und r-Adaption mit Geometric Conservation Law und konservativem Remapping.',
    category: 'mesh', type: 'Architektur', minutes: 6,
    sections: [
      { heading: 'Das Netz darf keine Masse erfinden', body: ['Wenn sich Zellen bewegen, muss ein konstantes physikalisches Feld konstant bleiben. Daf\u00fcr muss die diskrete Volumen\u00e4nderung zu den durch die Fl\u00e4chen geschobenen Volumina passen: die Geometric Conservation Law.', 'Bei einem Remesh m\u00fcssen Masse, Impuls, Gesamtenergie und Spezies konservativ \u00fcbertragen werden. Einfache Punktinterpolation reicht daf\u00fcr nicht. Kleine Cut Cells erfordern zus\u00e4tzlich Stabilisierung, zum Beispiel Aggregation oder konservative Flussumverteilung.'] },
      { heading: 'SDF-Projektion ist meist nur ein Newton-Schritt', body: ['Ein Dreiecks-SDF ist an der medialen Achse und an scharfen Features nicht global glatt. Auch ein CAD-Abstand ist nicht \u00fcberall stetig differenzierbar. AD durch Remeshing und Topologie\u00e4nderungen braucht deshalb eine klare G\u00fcltigkeitsannahme.', 'Die Tangentialprojektion verhindert normale Bewegung nur bis zur ersten Ordnung. Nach einem endlichen Schritt ist eine erneute Projektion auf die Geometrie n\u00f6tig. Dabei m\u00fcssen scharfe Kanten und Zellinversionen gesch\u00fctzt werden.'], formula: 'x_new = x_trial - phi(x_trial) grad(phi) / ||grad(phi)||^2' },
      { heading: 'Lokale physikalische Zeitschritte sind kein einfacher Faktor', body: ['F\u00fcr station\u00e4re Pseudozeit sind lokale Schritte n\u00fctzlich. F\u00fcr transiente Physik braucht echtes Local Time Stepping zeitkonsistente, konservative Grenzfl\u00fcsse und gegebenenfalls Subcycling. Ein Faktor omega_i auf dt_i allein garantiert das nicht.'] },
    ], sources: [{ label: 'AMReX documentation', url: 'https://amrex-codes.github.io/amrex/docs_html/' }],
  },
  {
    id: 'assembly-layout', title: 'Daten bewegen sich nicht kostenlos.',
    summary: 'SoA, Gathering, Matrixindizes und reproduzierbare Reduktionen realistisch einordnen.',
    category: 'gpu', type: 'Architektur', minutes: 6,
    sections: [
      { heading: 'SoA hilft beim Streaming, nicht bei jedem Zugriff', body: ['Benachbarte Threads lesen zusammenh\u00e4ngende Geometriearrays effizient. Owner- und Neighbour-Zellwerte bleiben aber indirekte Gather-Zugriffe; ein CSR-Scatter ist nicht automatisch koalesziert.', 'Ein Warp mit 32 Double-Werten liest 256 Byte, also mehrere 32-Byte-Sektoren. 128-Byte-Ausrichtung bedeutet nicht ein Speicherzyklus. Entscheidend sind tats\u00e4chliche Transaktionen, Cachetreffer und die Latenzverdeckung.'], snippet: 'layout' },
      { heading: 'Unique writes sind eine Vertragsfrage', body: ['Direkte CSR-Zielindizes vermeiden Suchen. Mehrere Fl\u00e4chen zwischen demselben Zellpaar k\u00f6nnen aber auf denselben Eintrag zeigen. Ein einfaches Gleichheitszeichen ist nur dann sicher, wenn die Einzigartigkeit bewiesen ist.', 'Eine Alternative ist: pro Fl\u00e4che Koeffizienten oder Fl\u00fcsse berechnen, danach pro lokal besessener Zeile in fester Reihenfolge zusammenfassen. Das kostet tempor\u00e4ren Speicher, vermeidet aber Atomics im Zell-Gather.'], snippet: 'gather' },
      { heading: 'Den gesamten Aufwand bilanzieren', body: ['Vier Face-Buckets reduzieren geometriebedingte Verzweigungen. Zus\u00e4tzliche Launches, Repartitionierung, tempor\u00e4re Arrays und Cacheeffekte k\u00f6nnen den Gewinn jedoch aufheben. Profile beide Varianten auf realen Netzen.', 'Die abschlie\u00dfende Bounds-Pr\u00fcfung ist kein problematischer datenabh\u00e4ngiger Schemawechsel. Enzyme garantiert bei einem kleinen Primal-Kernel weder einen spill-freien Adjoint noch eine korrekte Ableitung der Kommunikation.'] },
    ], sources: [{ label: 'CUDA: Memory optimizations', url: 'https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html' }, { label: 'Enzyme project', url: 'https://enzyme.mit.edu/' }],
  },
  {
    id: 'scaling-budget', title: 'Skalieren hei\u00dft Kosten sichtbar machen.',
    summary: 'Gewichtete Partitionierung, AMG-Reuse, Checkpoints und ein belastbares Cloud-Kostenmodell.',
    category: 'scaling', type: 'Architektur', minutes: 7,
    sections: [
      { heading: 'Last nach Rechenarbeit verteilen', body: ['Morton- oder Hilbert-Kurven bieten eine einfache Partitionierung mit r\u00e4umlicher Lokalit\u00e4t. Sie minimieren aber nicht garantiert den Kommunikationsschnitt und ersetzen keine gewichtete Graphpartitionierung.', 'Nutze gemessene Gewichte aus Chemie, Partikeln und Zelltypen. Eine dynamische Umverteilung verschiebt Felder, Partikel, Adjazenz, Halos und gegebenenfalls Solverstrukturen. Das ist keine Zero-Copy-Operation. Repartitioniere nur, wenn die erwartete Ersparnis die Migration \u00fcbersteigt.'] },
      { heading: 'Grobe Netze behalten, Transferoperatoren pr\u00fcfen', body: ['Eine Mesher-Hierarchie kann geometrisches Multigrid oder eine grobe Initialisierung unterst\u00fctzen. Sinnvolle Restriktion, Prolongation, Randbehandlung und geeignete Groboperatoren entstehen daraus nicht automatisch.', 'Auch Krylov-Recycling und Preconditioner-Reuse lohnen sich bei \u00e4hnlichen Systemen. Miss dabei Setup-Zeit, Iterationen und Speicherbedarf. Ein pauschaler Gewinn von 40 bis 70 Prozent ist keine Eigenschaft des Verfahrens.'] },
      { heading: 'Cloud-tauglich bedeutet auch wiederaufnehmbar', body: ['Plane versionierte Checkpoints mit Topologie, physikalischem Zustand, Integratorhistorie und Kopplungszustand. Schreibe kollektiv oder in geb\u00fcndelten Dateien, nicht eine winzige Datei pro Rank und Zeitschritt.', 'F\u00fcr die Cloud z\u00e4hlen Kosten bis zur L\u00f6sung, nicht nur Node-Stunden. Vergleiche Strong und Weak Scaling, Netzwerktyp, GPU-Direct-Unterst\u00fctzung, Checkpoint-I/O und den verbleibenden seriellen Anteil. Aktuelle VM- und Netzwerkspezifikationen beim Anbieter pr\u00fcfen.'] },
    ], sources: [{ label: 'PETSc: successive linear systems', url: 'https://petsc.org/main/manual/ksp/' }, { label: 'AMReX: parallel programming', url: 'https://amrex-codes.github.io/amrex/docs_html/' }],
  },
];

export interface Correction {
  id: string;
  title: string;
  severity: 'Kritisch' | 'Wichtig' | 'Einordnen';
  before: string;
  after: string;
  article: string;
}
export const corrections: Correction[] = [
  { id: 'base-all', title: 'Der Basisoperator fehlt auf den Korrekturfl\u00e4chen', severity: 'Kritisch', before: 'Basis-Kernel nur auf Segment 0 und 2; Korrektur-Kernel auf Segment 1 und 3.', after: 'Basis-Kernel auf allen vier Segmenten ausf\u00fchren. Anschlie\u00dfend den expliziten Korrekturfluss nur auf 1 und 3 erg\u00e4nzen. Sonst fehlen wesentliche Matrixeintr\u00e4ge.', article: 'two-class-dispatch' },
  { id: 'gradient-halo', title: 'Ein Feld-Halo liefert keine Nachbargradienten', severity: 'Kritisch', before: 'Nach dem phi-Austausch kann sofort die Halo-Korrektur starten.', after: 'Entweder Gradienten austauschen oder einen ausreichend tiefen phi-Halo mit konsistenter Rekonstruktion bereitstellen. Die Datenabh\u00e4ngigkeiten als Operatorvertrag dokumentieren.', article: 'halo-contract' },
  { id: 'ghost-writes', title: 'Ghost-Zellen sind keine eigenen Matrixzeilen', severity: 'Kritisch', before: 'Auf jeder Rank-Grenzfl\u00e4che rhs[P] und rhs[N] mit atomicAdd aktualisieren.', after: 'Nur lokal besessene Zeilen schreiben. Einen eindeutigen Fl\u00e4chen-Owner definieren und gemeinsame Fl\u00fcsse konsistent kommunizieren.', article: 'halo-contract' },
  { id: 'central', title: 'Zentrale Konvektion ist nicht automatisch beschr\u00e4nkt', severity: 'Wichtig', before: 'Ein orthogonales Netz macht den zentralen Zwei-Punkt-Fluss robust.', after: 'Bei hohem Zell-Peclet-Wert kann zentrale Konvektion oszillieren. Upwind als Baseline oder eine validierte, limitierte High-Order-Rekonstruktion verwenden.', article: 'two-class-dispatch' },
  { id: 'taylor', title: 'Die Richtungsableitung ist nur f\u00fcr lineare Felder exakt', severity: 'Wichtig', before: 'grad(phi)_f \u00b7 d = phi_N - phi_P ist eine allgemeine exakte Identit\u00e4t.', after: 'Als Taylor-basierte diskrete Approximation behandeln. Mit linearen Feldern Exaktheit und mit nichtlinearen Manufactured Solutions die Konvergenzordnung testen.', article: 'verification-first' },
  { id: 'mesh-error', title: 'Skewness ist kein prozentualer Diskretisierungsfehler', severity: 'Wichtig', before: 'Die MeshErrorMap zeigt direkt, wie viel Prozent Fehler eine Region verursacht.', after: 'Geometriequalit\u00e4t, L\u00f6sungsindikator und Fehlersch\u00e4tzer getrennt f\u00fchren. Ein Fehlerprozentsatz braucht eine definierte Norm und einen geeigneten Sch\u00e4tzer oder Referenzvergleich.', article: 'mesh-quality' },
  { id: 'stream-mpi', title: 'CUDA-Streams garantieren kein MPI-Overlap', severity: 'Wichtig', before: 'Drei Streams verstecken die Kommunikationslatenz vollst\u00e4ndig.', after: 'GPU/MPI-Handoff, echte Progress-F\u00e4higkeit und alle Read/Write-Abh\u00e4ngigkeiten explizit absichern. Den Overlap messen, nicht voraussetzen.', article: 'halo-contract' },
  { id: 'coalescing', title: 'Ausrichtung ist nicht gleich ein Speicherzyklus', severity: 'Einordnen', before: 'Ein 128-Byte-ausgerichtetes SoA ist zu 100 Prozent koalesziert und wird in einem Zyklus geladen.', after: '32 Double-Werte belegen 256 Byte. Indirekte Zellzugriffe und CSR-Schreibmuster separat analysieren. Sektoren, Cache und Bandbreite mit einem Profiler messen.', article: 'assembly-layout' },
  { id: 'local-dt', title: 'Lokales dt ist bei transienter Physik nicht beliebig', severity: 'Wichtig', before: 'dt_i = omega_i dt stabilisiert transiente Rechnungen ohne weitere \u00c4nderungen.', after: 'Lokale physikalische Schritte ben\u00f6tigen konservative Zeitintegration und konsistente Grenzfl\u00fcsse. F\u00fcr station\u00e4re Pseudozeit gelten andere Anforderungen.', article: 'moving-mesh' },
  { id: 'dec', title: 'div(curl) = 0 ist eine topologische Eigenschaft', severity: 'Einordnen', before: 'Nur Voronoi-/Delaunay-Netze liefern automatisch bis auf 16 Stellen div B = 0.', after: 'Kompatible Inzidenzoperatoren erf\u00fcllen D C = 0 auch auf anderen Zellkomplexen. Materialoperatoren, Randbedingungen und der Zeitintegrator bleiben relevant.', article: 'electromagnetics' },
  { id: 'rt', title: 'RT Cores sind keine universellen PDE-Beschleuniger', severity: 'Einordnen', before: 'RT Cores l\u00f6sen Maxwell, Cut-Cell-Volumina und exakte SDFs praktisch direkt.', after: 'Sie beschleunigen geeignete Traversierungs- und Schnitttests. Vollwellenphysik, Abstandsberechnung, Volumenintegration und Streumodelle brauchen weitere Verfahren.', article: 'electromagnetics' },
  { id: 'speedups', title: 'Speedups sind Messergebnisse, keine Garantien', severity: 'Einordnen', before: '10 ms Inferenz, 100x Adaption, 70 Prozent weniger Iterationen, Zero-Copy-Rebalancing.', after: 'Als zu pr\u00fcfende Hypothesen kennzeichnen. Gesamtlaufzeit bis zur gleichen Genauigkeit, Speicherbedarf, Migrationskosten und Hardware dokumentieren.', article: 'scaling-budget' },
];

export interface Repository {
  name: string; owner: string; url: string; category: 'Kopplung' | 'Kinetik' | 'Numerik' | 'Geometrie';
  description: string; backend: string; caution: string;
}
export const repositories: Repository[] = [
  { name: 'preCICE', owner: 'precice/precice', url: 'https://github.com/precice/precice', category: 'Kopplung', description: 'Partitionierte CHT- und FSI-Kopplung, Mapping und implizite Beschleunigung.', backend: 'C++ / MPI', caution: 'Mapping nach intensiven und extensiven Gr\u00f6\u00dfen ausw\u00e4hlen; Rollback implementieren.' },
  { name: 'MUI', owner: 'MxUI/MUI', url: 'https://github.com/MxUI/MUI', category: 'Kopplung', description: 'Flexible r\u00e4umlich-zeitliche Datenschnittstelle f\u00fcr Multiscale-Kopplung.', backend: 'C++ / MPI', caution: 'Header-basiert bedeutet nicht ohne Abh\u00e4ngigkeiten: MPI und der Adapter bleiben Teil des Stacks.' },
  { name: 'PelePhysics', owner: 'Pele-Suite/PelePhysics', url: 'https://github.com/Pele-Suite/PelePhysics', category: 'Kinetik', description: 'Thermodynamik, Transport und Mechanismus-Codegenerierung mit CEPTR.', backend: 'AMReX / GPU', caution: 'Mechanismus- und Backend-Unterst\u00fctzung sowie Jacobian-Pfad konkret pr\u00fcfen.' },
  { name: 'TChem', owner: 'sandialabs/TChem', url: 'https://github.com/sandialabs/TChem', category: 'Kinetik', description: 'Parallele thermochemische Berechnungen und Reaktionskinetik.', backend: 'C++ / Kokkos', caution: 'Gew\u00fcnschte Gas-/Oberfl\u00e4chenmodelle und GPU-Konfiguration im gew\u00e4hlten Release pr\u00fcfen.' },
  { name: 'Mutation++', owner: 'mutationpp/Mutationpp', url: 'https://github.com/mutationpp/Mutationpp', category: 'Kinetik', description: 'Hochtemperatur-Thermodynamik, Transport und Nichtgleichgewichtsmodelle.', backend: 'C++', caution: 'Kein allgemeiner GPU-Drop-in-Ersatz f\u00fcr Cantera.' },
  { name: 'Reaktoro', owner: 'reaktoro/reaktoro', url: 'https://github.com/reaktoro/reaktoro', category: 'Kinetik', description: 'Chemische Gleichgewichte, Kinetik und reaktive multiphasige Systeme.', backend: 'C++ / Python', caution: 'Datenbanken, Lizenzbedingungen und die physikalischen Modelle zur Anwendung passend ausw\u00e4hlen.' },
  { name: 'Ginkgo', owner: 'ginkgo-project/ginkgo', url: 'https://github.com/ginkgo-project/ginkgo', category: 'Numerik', description: 'Sparse lineare Algebra und iterative L\u00f6ser f\u00fcr heterogene Systeme.', backend: 'CUDA / HIP / DPC++', caution: 'Backend- und Solver-Unterst\u00fctzung unterscheiden sich; nicht jede Pr\u00e4zision ist \u00fcberall verf\u00fcgbar.' },
  { name: 'MFEM', owner: 'mfem/mfem', url: 'https://github.com/mfem/mfem', category: 'Numerik', description: 'High-order FEM, kompatible R\u00e4ume und partielle Assemblierung.', backend: 'C++ / GPU', caution: 'FEM-Baustein, kein fertiger universeller CFD-Solver.' },
  { name: 'libCEED', owner: 'CEED/libCEED', url: 'https://github.com/CEED/libCEED', category: 'Numerik', description: 'Portable matrixfreie Elementoperatoren f\u00fcr High-order-Verfahren.', backend: 'CPU / CUDA / HIP', caution: 'Mesh, globale Assemblierung und Kommunikation bleiben Aufgabe des Frameworks.' },
  { name: 'ArborX', owner: 'arborx/ArborX', url: 'https://github.com/arborx/ArborX', category: 'Geometrie', description: 'Portable geometrische Suchabfragen und Nachbarschaftssuche.', backend: 'C++ / Kokkos', caution: 'Eine Suchbibliothek ist noch kein konservativer Mapping-Operator.' },
  { name: 'MMG', owner: 'MmgTools/mmg', url: 'https://github.com/MmgTools/mmg', category: 'Geometrie', description: 'Isotrope und anisotrope Adaption von Simplex-Netzen.', backend: 'C / CPU', caution: 'Geeignetheit f\u00fcr Boundary Layers und eigene Elementtypen vorher bewerten.' },
  { name: 'Kokkos', owner: 'kokkos/kokkos', url: 'https://github.com/kokkos/kokkos', category: 'Numerik', description: 'Performance-Portabilit\u00e4t f\u00fcr parallele Datenstrukturen und Kernels.', backend: 'CUDA / HIP / SYCL', caution: 'Portabler Code ist nicht automatisch auf jeder Architektur optimal.' },
];

export const roadmap = [
  { id: 'reference', stage: '01', title: 'Die numerische Referenz', description: 'CPU-Operator, klare Vorzeichen und Tests f\u00fcr Konstanz, Linearit\u00e4t und Erhaltung.', article: 'verification-first', scope: 'BASELINE' },
  { id: 'geometry', stage: '02', title: 'Geometrie und Datenvertr\u00e4ge', description: 'Dimensionslose Qualit\u00e4tsma\u00dfe, Invalid-Pfad, getrennte Basis- und Korrekturdaten.', article: 'mesh-quality', scope: 'FOUNDATION' },
  { id: 'dispatch', stage: '03', title: 'Konservatives GPU-Dispatching', description: 'Alle Basisfl\u00fcsse, sparse Korrektur und konfliktfreier Cell-Gather.', article: 'two-class-dispatch', scope: 'GPU' },
  { id: 'mpi', stage: '04', title: 'Ein Rank. Dann viele.', description: 'Eindeutiger Face-Owner, Feld- und Gradienten-Halos, explizite Stream-Events.', article: 'halo-contract', scope: 'DISTRIBUTED' },
  { id: 'measure', stage: '05', title: 'Messen vor Optimieren', description: 'True residual, Zeit bis zur Genauigkeit und reproduzierbare Scaling-Studien.', article: 'scaling-budget', scope: 'PROFILING' },
  { id: 'couple', stage: '06', title: 'Physikalische Kopplung', description: 'Schnittstellenbilanz, konservativer Transfer und vollst\u00e4ndige Checkpoints.', article: 'coupling-contracts', scope: 'MULTIPHYSICS' },
  { id: 'adapt', stage: '07', title: 'Kontrollierte Adaption', description: 'L\u00f6sungsindikatoren, GCL und konservatives Remapping mit Fehlerkontrolle.', article: 'moving-mesh', scope: 'ADAPTATION' },
  { id: 'experiment', stage: '08', title: 'Die n\u00e4chsten Experimente', description: 'Mixed Precision, Pilot-Physik und lernbasierte Metriken mit sicherem Fallback.', article: 'guided-adaptation', scope: 'RESEARCH' },
];