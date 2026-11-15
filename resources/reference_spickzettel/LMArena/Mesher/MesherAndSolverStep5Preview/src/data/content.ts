/* ------------------------------------------------------------------ *
 *  Statische Inhalte des Architecture-Hub
 * ------------------------------------------------------------------ */

export type StackCategory =
  | "Lineare Algebra"
  | "Kinetik & Thermo"
  | "Solver-Frameworks"
  | "Kopplung & Transfer"
  | "GPU & Abstraktion"
  | "Werkzeuge & I/O";

export type StackEntry = {
  name: string;
  role: string;
  category: StackCategory;
  status: "im Stack" | "Vorschlag";
  backend: string;
  href?: string;
};

export const STACK: StackEntry[] = [
  // ---------------- Lineare Algebra ----------------
  { name: "ginkgo", role: "Krylov- & Multigrid-Loeser, GPU-resident", category: "Lineare Algebra", status: "im Stack", backend: "CUDA / HIP / SYCL / OpenMP", href: "https://github.com/ginkgo-project/ginkgo" },
  { name: "amgcl", role: "Header-only AMG fuer CPU/GPU-Vorkonditionierer", category: "Lineare Algebra", status: "im Stack", backend: "OpenMP / CUDA / OpenCL", href: "https://github.com/ddemidov/amgcl" },
  { name: "Eigen", role: "Dichte lineare Algebra, lokale 6x6/16x16 Bloecke", category: "Lineare Algebra", status: "im Stack", backend: "CPU (vectorisiert)", href: "https://gitlab.com/libeigen/eigen" },
  { name: "NVIDIA AMGX", role: "Druck-Poisson-Loeser fuer unstrukturierte FVM-Gitter", category: "Lineare Algebra", status: "im Stack", backend: "CUDA (nur NVIDIA)", href: "https://github.com/NVIDIA/AMGX" },
  { name: "SUNDIALS", role: "IDA/CVODE steife Zeitintegration + Newton", category: "Lineare Algebra", status: "im Stack", backend: "CPU / GPU-Kernels", href: "https://github.com/LLNL/sundials" },
  { name: "PETSc", role: "Skalierbare Löser, TS/SNES, TopOpt-Backend", category: "Lineare Algebra", status: "im Stack", backend: "MPI / CUDA / HIP", href: "https://gitlab.com/petsc/petsc" },
  { name: "Trilinos", role: "Belos/MueLu fuer monolithische Multiphisik-Systeme", category: "Lineare Algebra", status: "Vorschlag", backend: "MPI / CUDA" },
  { name: "s-Step / GCRO-DR", role: "Krylov-Subspace-Recycling & pipelined Allreduce", category: "Lineare Algebra", status: "Vorschlag", backend: "in ginkgo / PETSc" },

  // ---------------- Kinetik & Thermo ----------------
  { name: "Cantera", role: "Reaktionskinetik, Chemkin-Mechanismen", category: "Kinetik & Thermo", status: "im Stack", backend: "CPU (virtuell)", href: "https://github.com/Cantera/cantera" },
  { name: "CoolProp", role: "Kuehlmittel- & Kaeltemittel-Zustandsgroessen", category: "Kinetik & Thermo", status: "im Stack", backend: "CPU", href: "https://github.com/CoolProp/CoolProp" },
  { name: "PelePhysics / CEPTR", role: "Cantera -> verzweigungsfreier GPU-C++-Code inkl. analytischer Jacobi", category: "Kinetik & Thermo", status: "Vorschlag", backend: "CUDA / HIP", href: "https://github.com/AMReX-Combustion/PelePhysics" },
  { name: "TChem", role: "Homogene Gasphasen- & Oberflaechenkoinetik (Wandkatalyse)", category: "Kinetik & Thermo", status: "Vorschlag", backend: "Kokkos: CUDA / HIP / SYCL", href: "https://github.com/sandialabs/TChem" },
  { name: "Mutation++", role: "Hochtemperatur-Thermo, Mehrkomponenten-Diffusion, SU2-Interface", category: "Kinetik & Thermo", status: "Vorschlag", backend: "CUDA / CPU", href: "https://github.com/mutationpp/Mutationpp" },
  { name: "teqp", role: "Template-basierte Zustandsgleichungen, Enzyme-differenzierbar", category: "Kinetik & Thermo", status: "Vorschlag", backend: "C++20 / CPU-GPU", href: "https://github.com/usnistgov/teqp" },
  { name: "Reaktoro", role: "Mehrphasige Gleichgewichte & Kinetik (Gas/Fluessig/Fest)", category: "Kinetik & Thermo", status: "Vorschlag", backend: "C++ / Autodiff", href: "https://github.com/reaktoro/reaktoro" },

  // ---------------- Solver-Frameworks ----------------
  { name: "SU2", role: "Kompressibel/inkompressibel, CHT, diskreter Adjoint (CoDiPack)", category: "Solver-Frameworks", status: "im Stack", backend: "OpenMP / CUDA-Offload", href: "https://github.com/su2code/SU2" },
  { name: "ECO GEN", role: "Mehrphasen-Kompressibel-Relaxierung, Kapillaritaet", category: "Solver-Frameworks", status: "im Stack", backend: "MPI / OpenMP" },
  { name: "SPHinXsys", role: "Partikel-basierte Mehrphysik (SPH), Freistorahlen, Wassertanks", category: "Solver-Frameworks", status: "im Stack", backend: "OpenMP / CUDA (teilw.)", href: "https://github.com/Xiangyu-Hu/SPHinXsys" },
  { name: "AMReX + PeleC/PeleLMeX", role: "Block-strukturierte AMR fuer reaktive Stroemungen", category: "Solver-Frameworks", status: "Vorschlag", backend: "CUDA / HIP / SYCL", href: "https://github.com/AMReX-Codes/amrex" },
  { name: "MFEM", role: "Hochordnungs-FEM/DG, matrix-frei, partielle Assemblierung", category: "Solver-Frameworks", status: "Vorschlag", backend: "CUDA / HIP / RAJA", href: "https://github.com/mfem/mfem" },
  { name: "MOOSE", role: "Monolithische voll-implizite Multiphisik (Kernel-Klassen)", category: "Solver-Frameworks", status: "Vorschlag", backend: "libMesh + PETSc", href: "https://github.com/idaholab/moose" },
  { name: "deal.II + lethe", role: "FEM/VOF/CFD-DEM mit matrix-freien GPU-Loesern", category: "Solver-Frameworks", status: "Vorschlag", backend: "Kokkos / CUDA", href: "https://github.com/dealii/dealii" },
  { name: "NeoN (ex NeoFOAM)", role: "OpenFOAM-Architektur in C++20, GPU-resident via Kokkos+Ginkgo", category: "Solver-Frameworks", status: "Vorschlag", backend: "Kokkos", href: "https://github.com/exasim-project/NeoN" },
  { name: "OpenLB", role: "Modulares LBM-Framework (thermisch, Mehrphasen)", category: "Solver-Frameworks", status: "Vorschlag", backend: "CUDA / OpenCL", href: "https://github.com/CompPhysVienna/OpenLB" },
  { name: "FluidX3D", role: "LBM mit OpenCL, FP16C-Kompression, hocheffizient", category: "Solver-Frameworks", status: "Vorschlag", backend: "OpenCL (jede GPU)", href: "https://github.com/FluidX3D/FluidX3D" },

  // ---------------- Kopplung & Transfer ----------------
  { name: "preCICE", role: "Partitionierte Kopplung (CHT/FSI), Quasi-Newton, RBF-Mapping", category: "Kopplung & Transfer", status: "im Stack", backend: "MPI / TCP-IP", href: "https://github.com/precice/precice" },
  { name: "MUI", role: "Header-only Kopplung von Euler- und Partikel-Codes", category: "Kopplung & Transfer", status: "im Stack", backend: "MPI / TCP / GPU", href: "https://github.com/MxUI/MUI" },
  { name: "ArborX", role: "GPU-native Nachbarschaftssuche (BVH) fuer Gitter-Partikel-Transfer", category: "Kopplung & Transfer", status: "Vorschlag", backend: "Kokkos / CUDA", href: "https://github.com/arborx/ArborX" },
  { name: "DataTransferKit", role: "Konservative Felduebertragung zwischen nicht-konformen Gittern", category: "Kopplung & Transfer", status: "Vorschlag", backend: "MPI", href: "https://github.com/ORNL-CEES/DataTransferKit" },
  { name: "FMI 3.0", role: "Standard-Kopplung zu Systemsimulation (Modelica, Regler)", category: "Kopplung & Transfer", status: "Vorschlag", backend: "C-API", href: "https://fmi-standard.org/" },
  { name: "Overset / TIOGA", role: "Chimera-Gitter fuer rotierende Maschinen", category: "Kopplung & Transfer", status: "Vorschlag", backend: "MPI / CUDA" },

  // ---------------- GPU & Abstraktion ----------------
  { name: "Kokkos", role: "Performance-Portabilitaet: ein Kernel fuer CPU/GPU", category: "GPU & Abstraktion", status: "im Stack", backend: "CUDA / HIP / SYCL / OpenMP", href: "https://github.com/kokkos/kokkos" },
  { name: "Enzyme", role: "LLVM-basiertes AD fuer den Gesamt-Adjoint inkl. Solver", category: "GPU & Abstraktion", status: "im Stack", backend: "LLVM / CUDA", href: "https://github.com/EnzymeAD/Enzyme" },
  { name: "nlopt / pagmo2", role: "Gradientenbasierte & populationsbasierte Optimierer fuer den Design-Loop", category: "GPU & Abstraktion", status: "im Stack", backend: "CPU", href: "https://github.com/esa/pagmo2" },
  { name: "OptiX / Embree", role: "BVH- & Strahl-Dreieck-Schnitt auf RT-Cores", category: "GPU & Abstraktion", status: "im Stack", backend: "RT-Cores (Hardware)", href: "https://developer.nvidia.com/rtx/raytracing/optix" },
  { name: "RAJA", role: "Portable Loop-Abstraktion neben Kokkos", category: "GPU & Abstraktion", status: "Vorschlag", backend: "CUDA / HIP / OpenMP", href: "https://github.com/LLNL/RAJA" },
  { name: "CoDiPack", role: "AD ueber Operator-Overloading (SU2-Adjoint)", category: "GPU & Abstraktion", status: "im Stack", backend: "CPU" },
  { name: "ZFP / FP16-Booster", role: "Verlustbehaftete Halo-Kompression, Mixed-Precision-Tiers", category: "GPU & Abstraktion", status: "Vorschlag", backend: "CPU / GPU", href: "https://github.com/LLNL/zfp" },

  // ---------------- Werkzeuge & I/O ----------------
  { name: "OpenVDB", role: "Sparse Voxel-Pilot-Gitter, SDF, Wrapping", category: "Werkzeuge & I/O", status: "im Stack", backend: "CPU / OpenCL", href: "https://github.com/AcademySoftwareFoundation/openvdb" },
  { name: "CGNS / HDF5", role: "Standard-Format fuer Gitter + Loesungsfelder", category: "Werkzeuge & I/O", status: "im Stack", backend: "MPI-I/O", href: "https://github.com/CGNS/CGNS" },
  { name: "ADIOS2", role: "In-Situ-I/O mit STaging/Streaming statt VTK-Dumps", category: "Werkzeuge & I/O", status: "Vorschlag", backend: "MPI / CUDA", href: "https://github.com/ornladios/ADIOS2" },
  { name: "gmsh", role: "STL/STEP -> Tet/Hex, eingebettet als Meshing-Frontend", category: "Werkzeuge & I/O", status: "im Stack", backend: "CPU", href: "https://github.com/gmsh/gmsh" },
  { name: "Kernel Tuner", role: "Autotuning von Blockgroessen & Segment-Schwellen", category: "Werkzeuge & I/O", status: "Vorschlag", backend: "CUDA / OpenCL" },
  { name: "SCR", role: "Fehlertolerante Checkpoints fuer preemptible Cloud-Nodes", category: "Werkzeuge & I/O", status: "Vorschlag", backend: "MPI", href: "https://github.com/LLNL/scr" },
  { name: "Chaospy / UQ", role: "Polynomial-Chaos-Unsicherheitsquantifizierung", category: "Werkzeuge & I/O", status: "Vorschlag", backend: "CPU" },
];

/* ------------------------------------------------------------------ */

export type PipelineStep = {
  id: string;
  kicker: string;
  title: string;
  lead: string;
  bullets: string[];
  out: string;
  tags: string[];
};

export const PIPELINE: PipelineStep[] = [
  {
    id: "cad",
    kicker: "Schritt 0",
    title: "CAD → CADMeshSDF",
    lead: "Alles beginnt mit einer stetig differenzierbaren Abstandsfunktion zur Wand.",
    bullets: [
      "Signed Distance Field ϕ(x) aus STL/STEP via Winding Numbers + OpenVDB-Voxelung",
      "Inside/Outside-Tests, Spaltmessung d_gap und Dickenberechnung laufen 1:1 als Strahltests auf RT-Cores",
      "ϕ ist differenzierbar → spaeter Enzyme-Sensitivitäten ∂J/∂x_seed und exakte Wandprojektion",
    ],
    out: "SDF + Wandnormalen n(x) + Topologie-Index",
    tags: ["OpenVDB", "RT-Cores", "Winding Numbers"],
  },
  {
    id: "pilot",
    kicker: "Schritt 1",
    title: "Pilot-Lauf: Physics-Guided Guess",
    lead: "0,1–0,3 s GPU-LBM auf einem groben 128³-Hintergrundgitter (FP16/FP32-Tier).",
    bullets: [
      "Mantaflow-/Niagara-Prinzip: Semi-Lagrangian oder LBM, kein Mesh noetig",
      "Ergebnis: Wirbelstaerke ω = ∇×u_pilot, Scherrate γ̇, Druckgradient",
      "Der Mesher sieht damit zum ersten Mal, wo Nachlaufwirbel, Abloesung und Freistrahle entstehen",
    ],
    out: "ω_pilot, γ̇_pilot auf 128³ (≈ 8 MB in FP16)",
    tags: ["LBM", "FP16", "0,2 s"],
  },
  {
    id: "metric",
    kicker: "Schritt 2",
    title: "Ziel-Zellgroesse h(x)",
    lead: "Das Wirbelfeld steuert direkt die Verfeinerungskarte des Meshers.",
    bullets: [
      "h(x) = min( h_CAD(x), c_ω / (‖∇×u_pilot(x)‖ + ε) )",
      "Wandnachlauf: h_wall ~ f(d_SDF) fuer y⁺-Ziele der Grenzschicht",
      "Doppelgewinn: interpoliert als Startfeld (t=0) spart der echte FVM-Solver 50–70 % der Erstiterationen",
    ],
    out: "h_target(x) pro Zellkandidat",
    tags: ["Metrik-Feld", "y⁺", "Initial Guess"],
  },
  {
    id: "neural",
    kicker: "Schritt 2b (optional)",
    title: "Neural Metric Predictor",
    lead: "3D-U-Net / FNO sagt die anisotrope Riemannsche Metrik M(x) in ≈ 10 ms voraus.",
    bullets: [
      "Input: Bauteil-SDF ϕ(x) + (Re, Ma, Pr) → Output: Metrik M(x), keine Knoten-Kanten-Zuordnung",
      "Der mathematisch exakte Mesher garantiert anschliessend ein fehlerfreies Gitter",
      "Selbstlernend: jeder Adjoint-Lauf liefert das Trainingspaar (Geometrie → exakter Fehlerort)",
    ],
    out: "M(x) → direkt in den Mesher",
    tags: ["FNO", "3D-U-Net", "10 ms"],
  },
  {
    id: "mesh",
    kicker: "Schritt 3",
    title: "Mesher: Poly-Hexcore / Voronoi",
    lead: "Voronoi-, Poly-Hexcore-, Shrink-Wrap- und 2.5D-Swept-Modi erzeugen das Gitter.",
    bullets: [
      "Shrink-Wrap: eine elastische Membran ueberbrueckt Spalte < ε bei 'schmutzigem' Industrie-CAD",
      "2.5D-Swept: ein 2D-Slice wird in z gestapelt → > 90 % weniger Meshing-Zeit fuer prismatische Bauteile",
      "SPH-Particle-Packing mit μ→∞ (Stokes-Kriechen) liefert perfekte Kugelpackungen als Voronoi-Seeds",
      "Primal-dual: d_f steht exakt senkrecht auf S_f → Voraussetzung fuer Yee/DEC in der Elektrodynamik",
    ],
    out: "Polyedernetz + 4-Quadranten-Face-Partition",
    tags: ["Voronoi", "Shrink-Wrap", "2.5D"],
  },
  {
    id: "errormap",
    kicker: "Schritt 4",
    title: "MeshErrorMap",
    lead: "Qualitaetskarte m_f^skew, k_f^comp, β_f, ω_i — das Herzstueck der Stabilisierung.",
    bullets: [
      "Sortiert Flächen physisch im Speicher: orthogonal (Schnellpfad) vs. korrekturbeduerftig",
      "Speist Mesher-Selbstheilung, lokale CFL-Daempfung, AMG-Glaetterauswahl und TopOpt-Filter",
      "A-priori-Diskretisierungsfehler aus ‖m_f^skew‖ und Feldkruemmung ∇²u direkt im 3D-Cockpit",
    ],
    out: "ω_f ∈ (0,1], Segment-Offsets, CSR-Indizes",
    tags: ["Skewness", "Nicht-Orthogonalitaet", "4 Segmente"],
  },
  {
    id: "solve",
    kicker: "Schritt 5",
    title: "Solver: Zwei-Klassen-Kernels",
    lead: "Branchless Fast-Path fuer 90–95 % der Flächen, Korrekturkernel nur fuer den Rest.",
    bullets: [
      "Segment 0+2: rein impliziter 2-Punkt-Fluss, keine Gradienten, keine Skewness-Vektoren → −65 % VRAM-Traffic",
      "Segment 1+3: expliziter Korrekturfluss F_f^(1) mit ω_f gedaempft auf die rechte Seite",
      "Drei CUDA-Streams ueberdecken MPI-Halo-Austausch und Gradientenberechnung",
    ],
    out: "CSR-Matrix direkt im VRAM fuer ginkgo / amgcl",
    tags: ["Branchless", "3 Streams", "ginkgo"],
  },
  {
    id: "adjoint",
    kicker: "Schritt 6",
    title: "Adjoint → Optimierung → Lernen",
    lead: "Enzyme differenziert den gesamten Solver inkl. Chemie und Kopplung.",
    bullets: [
      "η_i = |ψ_iᵀ R_i| als exakter Fehlerort pro Zelle → Trainingspair fuer den Metrik-Predictor",
      "SIMP-Material-Update mit ω_i gewichtet → keine 'Geister-Zacken' auf schlechten Zellen",
      "Differenzierbares r-Morphing: Seeds gleiten entlang von Flammenfronten und Stosswellen",
      "Auto-Defeaturing: Kleinteile mit Sensitivität ≈ 0 werden weggewrappt",
    ],
    out: "∇J, Fehlerkarte, neues h(x) → zurueck zu Schritt 1",
    tags: ["Enzyme", "TopOpt", "Closed Loop"],
  },
];

/* ------------------------------------------------------------------ */

export type Snippet = {
  id: string;
  file: string;
  lang: string;
  blurb: string;
  code: string;
};

export const SNIPPETS: Snippet[] = [
  {
    id: "soa",
    file: "mesh/face_partition.hpp",
    lang: "C++20",
    blurb:
      "Structure-of-Arrays mit 128-Byte-Ausrichtung: Basis-Daten fuer alle Flächen, Korrektur-Vektoren nur fuer die ~6 % korrekturbeduerftigen Faces.",
    code: `#include <cstdint>

// 4-Quadranten-Partition: Mesher sortiert die Faces physisch im Speicher
struct FacePartitionOffsets {
    int32_t seg0_inner_ortho_end;   // [0,              s0)  innere Zellen, Klasse 0
    int32_t seg1_inner_corr_end;    // [s0,             s1)  innere Zellen, Klasse 1
    int32_t seg2_halo_ortho_end;    // [s1,             s2)  Halo-Grenzflaechen, Klasse 0
    int32_t seg3_halo_corr_end;     // [s2,             s3)  Halo-Grenzflaechen, Klasse 1
};

// Basis-Daten fuer ALLE Faces (Segmente 0..3) - 100 % koalesziert
struct FaceBaseSoA {
    const int32_t* __restrict__ owner;         // Zelle P
    const int32_t* __restrict__ neighbor;      // Zelle N
    const int32_t* __restrict__ csr_upper_idx; // direkter CSR-Index (P,N) - keine Binärsuche
    const int32_t* __restrict__ csr_lower_idx; // direkter CSR-Index (N,P)
    const double*  __restrict__ alpha_ortho;   // ||S_f||^2 / (d_f . S_f)
    const double*  __restrict__ beta_interp;   // Interpolationsgewicht beta_f
};

// Komprimierte MeshErrorMap NUR fuer Korrektur-Faces (Segmente 1 und 3)
struct FaceCorrectionSoA {
    const int32_t* __restrict__ base_face_id;  // Rückverweis auf FaceBaseSoA
    const double*  __restrict__ m_skew_x;      // Skewness-Vektor (SoA fuer Warp-Reads)
    const double*  __restrict__ m_skew_y;
    const double*  __restrict__ m_skew_z;
    const double*  __restrict__ k_comp_x;      // nicht-orthogonaler Korrekturvektor
    const double*  __restrict__ k_comp_y;
    const double*  __restrict__ k_comp_z;
    const double*  __restrict__ omega_damp;    // min(omega_P, omega_N) aus der MeshErrorMap
};`,
  },
  {
    id: "ortho",
    file: "kernels/ortho_fast.cu",
    lang: "CUDA",
    blurb:
      "Fast-Path fuer Segmente 0 und 2: kein einziger if, kein Zugriff auf Gradienten oder Skewness-Vektoren.",
    code: `// F_f^(0) = m_dot_f * (beta*phi_N + (1-beta)*phi_P) - Gamma_f * alpha * (phi_N - phi_P)
__global__ void assemble_ortho_fast_kernel(
    int32_t start_f, int32_t end_f,
    FaceBaseSoA faces,
    const double* __restrict__ m_dot,     // Massenstrom über die Fläche
    const double* __restrict__ gamma_f,   // Diffusionskoeffizient
    double* __restrict__ csr_diag,
    double* __restrict__ csr_offdiag)
{
    int32_t f = start_f + blockIdx.x * blockDim.x + threadIdx.x;
    if (f >= end_f) return;

    int32_t P = faces.owner[f];
    int32_t N = faces.neighbor[f];

    double mdot = m_dot[f];
    double diff = gamma_f[f] * faces.alpha_ortho[f];
    double beta = faces.beta_interp[f];

    // rein implizite Matrixkoeffizienten, ohne Skewness-/Non-Ortho-Overhead
    double a_PN =  mdot * beta       - diff;
    double a_NP = -mdot * (1.0 - beta) - diff;

    csr_offdiag[faces.csr_upper_idx[f]] = a_PN;
    csr_offdiag[faces.csr_lower_idx[f]] = a_NP;

    atomicAdd(&csr_diag[P],  mdot * (1.0 - beta) + diff);
    atomicAdd(&csr_diag[N], -mdot * beta          + diff);
}`,
  },
  {
    id: "skew",
    file: "kernels/skew_correction.cu",
    lang: "CUDA",
    blurb:
      "Korrektur-Kernel nur fuer die ~5 % Faces in Segment 1 und 3 — der explizite Anteil landet gedaempft auf der rechten Seite.",
    code: `// F_f^(1) = omega_f * ( m_dot_f * (grad phi)_f . m_f^skew - Gamma_f * (grad phi)_f . k_f^comp )
__global__ void assemble_skew_correction_kernel(
    int32_t start_c, int32_t end_c,
    FaceBaseSoA base,
    FaceCorrectionSoA corr,
    const double* __restrict__ m_dot,
    const double* __restrict__ gamma_f,
    const double* __restrict__ grad_phi_x,   // nur hier wird das Gradientenfeld gebraucht
    const double* __restrict__ grad_phi_y,
    const double* __restrict__ grad_phi_z,
    double* __restrict__ rhs)
{
    int32_t c = start_c + blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= end_c) return;

    int32_t f = corr.base_face_id[c];
    int32_t P = base.owner[f];
    int32_t N = base.neighbor[f];

    double beta = base.beta_interp[f];
    double om   = corr.omega_damp[c];

    // interpolierter Flächengradient (nabla phi)_f
    double gx = beta * grad_phi_x[N] + (1.0 - beta) * grad_phi_x[P];
    double gy = beta * grad_phi_y[N] + (1.0 - beta) * grad_phi_y[P];
    double gz = beta * grad_phi_z[N] + (1.0 - beta) * grad_phi_z[P];

    double skew_corr = gx * corr.m_skew_x[c] + gy * corr.m_skew_y[c] + gz * corr.m_skew_z[c];
    double orth_corr = gx * corr.k_comp_x[c] + gy * corr.k_comp_y[c] + gz * corr.k_comp_z[c];

    double flux_corr = om * (m_dot[f] * skew_corr - gamma_f[f] * orth_corr);

    atomicAdd(&rhs[P], -flux_corr);   // explizite Quellenkorrektur
    atomicAdd(&rhs[N],  flux_corr);
}`,
  },
  {
    id: "pilot",
    file: "mesh/pilot_metric.cu",
    lang: "CUDA",
    blurb:
      "Das Pilot-Wirbelfeld aus dem 128³-LBM-Vorlauf wird direkt zur Ziel-Zellgroesse — Physics-Guided Pilot-Meshing.",
    code: `// h(x) = min( h_CAD(x),  c_omega / (|curl u_pilot(x)| + eps) )
__global__ void pilot_metric_kernel(
    int32_t n_cells,
    const double* __restrict__ cell_centre,
    const double* __restrict__ omega_pilot,  // |curl u_pilot| aus dem Pilot-LBM
    const double* __restrict__ h_cad,        // geometrische Zielgroesse aus dem CAD
    const double* __restrict__ sdf,          // Wandabstand aus dem CADMeshSDF
    double c_omega, double eps,
    double h_min, double h_max,
    double* __restrict__ h_target)
{
    int32_t c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n_cells) return;

    double w = fabs(omega_pilot[c]);
    double h_phys = c_omega / (w + eps);            // Scherschicht -> kleine Zellen

    // Wandnachlauf: Grenzschicht soll das y^+-Ziel in wenigen Zellen treffen
    double d = fabs(sdf[c]);
    double h_wall = fmin(h_phys, 0.05 * d + 1e-4);

    double h = fmin(fmin(h_cad[c], h_wall), h_max);
    h_target[c] = fmax(h, h_min);
}`,
  },
  {
    id: "streams",
    file: "solver/multiphysics_step.cu",
    lang: "CUDA / MPI",
    blurb:
      "Takt 0/1/2: Der MPI-Halo-Austausch und die Gradientenberechnung verschwinden komplett hinter dem Fast-Path-Kernel.",
    code: `// Drei Streams: Netzwerklatenz + Gradienten werden hinter 90 % der Arbeit versteckt
void assemble_timestep(SolverState& S,
                       cudaStream_t s_inner,
                       cudaStream_t s_grad,
                       cudaStream_t s_halo)
{
    // ---- Takt 0: non-blockender Halo-Austausch (unstrukturiert, Hilbert-Sortierung) ----
    MPI_Irecv(S.halo_in.data(),  (int)S.halo_in.size(),  MPI_DOUBLE, S.nbr_lo, 1, S.comm, &S.req_in);
    MPI_Isend(S.halo_out.data(), (int)S.halo_out.size(), MPI_DOUBLE, S.nbr_hi, 1, S.comm, &S.req_out);
    cudaEventRecord(S.ev_halo_arrived, s_halo);

    // ---- Takt 0: 90 % der Faces ohne Skewness-Korrektur ----
    launch_ortho_fast<<<grid0, 256, 0, s_inner>>>(S.seg0.begin, S.seg0.end, S);
    cudaEventRecord(S.ev_ortho_done, s_inner);

    // Gradienten nur fuer Zellen, die an Segment 1 grenzen (nicht fuer das ganze Feld!)
    launch_gradients<<<gridG, 256, 0, s_grad>>>(S.seg1_adjacent_cells, S.phi);

    // ---- Takt 1: 5 % Korrektur-Faces ----
    cudaStreamWaitEvent(s_inner, S.ev_ortho_done, 0);
    launch_skew_correction<<<grid1, 128, 0, s_inner>>>(S.seg1.begin, S.seg1.end, S);

    // ---- Takt 2: Halo-Segmente, sobald das Netzwerk geliefert hat ----
    cudaStreamWaitEvent(s_halo, S.ev_halo_arrived, 0);
    launch_ortho_fast<<<grid2, 256, 0, s_halo>>>(S.seg2.begin, S.seg2.end, S);
    launch_skew_correction<<<grid3, 64, 0, s_halo>>>(S.seg3.begin, S.seg3.end, S);
    // -> fertige CSR-Matrix verlaesst den VRAM nie: direkt an ginkgo uebergeben
}`,
  },
  {
    id: "enzyme",
    file: "adjoint/mesh_morphing.cu",
    lang: "CUDA / Enzyme",
    blurb:
      "Differenzierbares r-Morphing: Enzyme bildet die Sensitivitaet direkt nach den Voronoi-Seeds, die Wandprojektion haelt die Knoten auf dem CAD.",
    code: `// Ziel-Funktional J haengt (u.a.) an den Voronoi-Seed-Positionen x_seed
double objective(int n_seed, const double* x_seed,
                 const double* x_seed_ref, const Mesh& M, const BC& bc);

void mesh_sensitivity(const Mesh& M, const BC& bc, double* dJ_dx_seed)
{
    std::vector<double> seed(M.n_seed * 3), seed_d(M.n_seed * 3, 0.0);
    std::memcpy(seed.data(), M.seeds.data(), seed.size() * sizeof(double));

    // Reverse-Mode durch den GESAMTEN Solver inkl. Chemie und Kopplung
    __enzyme_autodiff(objective, M.n_seed * 3,
                      seed.data(), seed_d.data(),
                      M.n_seed * 3, M.seeds.data(), M, bc);

    // Topologie-erhaltendes Gleiten: Seeds wandern zum groessten Diskretisierungsfehler
    //   x_seed += -eta * grad J
    // Randknoten: Projektion (I - n n^T) grad  ->  Knoten bleiben exakt auf der CAD-Wand
    for (int i = 0; i < M.n_boundary_nodes; ++i) {
        Vec3 n = M.sdf_gradient(M.bnode[i]);            // aus CADMeshSDF
        Vec3 g{seed_d[3*i], seed_d[3*i+1], seed_d[3*i+2]};
        Vec3 gtan = g - n * g.dot(n);                   // Tangentialanteil
        seed_d[3*i] = gtan.x; seed_d[3*i+1] = gtan.y; seed_d[3*i+2] = gtan.z;
    }
    std::memcpy(dJ_dx_seed, seed_d.data(), seed.size() * sizeof(double));
}`,
  },
];

/* ------------------------------------------------------------------ */

export type Idea = {
  title: string;
  category: string;
  status: "neu" | "geplant" | "im Bau";
  effort: "klein" | "mittel" | "groß";
  why: string;
  how: string[];
};

export const IDEAS: Idea[] = [
  {
    title: "Curved Elements & p-Adaption (IGA-Light)",
    category: "Gitter & Diskretisierung",
    status: "neu",
    effort: "mittel",
    why: "Polyeder-Gitter sind fuer Maxwell hoher Frequenz und fuer glatte Wände ein Nachteil: gekruemmte Kanten (isoparametrische Kanten 3. Ordnung) reduzieren den geometrischen Fehler um Groessenordnungen — und machen die MeshErrorMap an glatten Wänden fast leer.",
    how: [
      "Kanten als Quadratische/Bézier-Segmente im Gitter speichern (deal.II/p-MG, MFEM-Nurbs, PetIGA andocken)",
      "MeshErrorMap meldet dann nur noch Interpolations-, nicht mehr Geometriefehler",
      "p-Adaptivitaet lokal in Wirbelkernen: Ordnung 3→5 statt Zellen verdoppeln",
    ],
  },
  {
    title: "ROM- & Digital-Twin-Schicht (POD + DMD + Neural Operator)",
    category: "KI & Surrogate",
    status: "neu",
    effort: "groß",
    why: "Fuer Design-Studien mit tausenden Varianten ist der volle Solver zu langsam. Ein trainiertes ROM auf den Adjoint-Sensitivitaeten als Features liefert Druckverlust und Waermeuebergang in Millisekunden.",
    how: [
      "POD/DMD auf den transienten Snapshots, aktiviert bei Re > 10^4",
      "Adjoint-Gradient ∇J als zusaetzliche Input-Features fuer den FNO",
      "Hyper-Redukktion (ECSW) fuer die Chemie-Terme",
    ],
  },
  {
    title: "WebGPU-Pilot direkt im Browser-Cockpit",
    category: "KI & Surrogate",
    status: "neu",
    effort: "mittel",
    why: "Der 128³-LBM-Pilot laesst sich als WASM/WebGPU-Compute-Shader kompilieren. Damit bekommt der Anwender den Initial Guess und die Metrik-Karte VOR dem Submit — ohne Installation.",
    how: [
      "Gleicher LBM-Kernel via Kokkos-SYCL oder handgeschriebenem WGSL",
      "MeshErrorMap-Vorschau als Heatmap im 3D-Cockpit",
      "Ergebnis als h(x)-Datei an den Cluster-Solver uebergeben",
    ],
  },
  {
    title: "UQ & Bayes-Kalibrierung (Polynomial Chaos)",
    category: "Verifikation",
    status: "neu",
    effort: "mittel",
    why: "Unsicherheiten in Randbedingungen, Material und Gitter lassen sich als Unsicherheitsbudit an den Kunden ausgeben — die MeshErrorMap liefert den Diskretisierungsanteil direkt.",
    how: [
      "PCE mit Punkt-Kollokation auf den Zufallsparametern (Chaospy)",
      "Gitterfehler-Anteil: a-priori-Schaetzer aus ‖m_f^skew‖ + ∇²u",
      "Sobol-Indizes fuer die Kalibrierung von Stoffmodellen",
    ],
  },
  {
    title: "Mixed-Precision a-posteriori Fehlerkontrolle",
    category: "Numerik",
    status: "neu",
    effort: "klein",
    why: "FP16/TF32 im Vorkonditionierer ist schnell, aber der Rundungsfehler ist unsichtbar. Ihn wie einen Diskretisierungsfehler behandeln — und die MeshErrorMap sagt, wo FP64 wirklich noetig ist.",
    how: [
      "Iterative Refinement: Defekt r = b − Ax exakt in FP64, Loeser in FP16",
      "Rundungsfehler-Schaetzer pro Segment: η_round,i = ‖r_i‖ / ‖b_i‖",
      "Automatischer FP64-Fallback fuer Zellen mit η_round > tol",
    ],
  },
  {
    title: "GNN-gelernte Glaetter als AMG-Ersatz",
    category: "KI & Surrogate",
    status: "neu",
    effort: "groß",
    why: "Auf dem Polyeder-Graphen ist ein Graph Neural Network ein natuerlicher Kandidat fuer einen problemangepassten Glaetter — besonders dort, wo AMG wegen Nicht-Orthogonalitaet schwach wird.",
    how: [
      "Nachbarschaftsgraph = Face-Graph, Kantenmerkmale = m_f^skew, ω_f",
      "Training auf feinsten Gitterstufen, Inferenz on-the-fly pro Zeitschritt",
      "Hybrid: GNN-Glaetter fuer Segment 1/3, klassisches AMG fuer Segment 0",
    ],
  },
  {
    title: "MMS-Verifikationssuite in der CI",
    category: "Verifikation",
    status: "neu",
    effort: "klein",
    why: "Neue Kerne und Kopplungen sollen ihre Konvergenzordnung automatisch beweisen, nicht behaupten. Method of Manufactured Solutions ist dafuer der Standard.",
    how: [
      "Analytische Loesung in jede Erhaltungsgleichung einspeisen, Quellterm automatisch per Enzyme",
      "Gitterstudie L0→L3 misst die Ordnung pro Kernel (auch fuer den Korrektur-Kernel!)",
      "Regression: Ordnungsabfall < 0,1 schlaegt den Build fehl",
    ],
  },
  {
    title: "Overset/Chimera-Gitter fuer rotierende Maschinen",
    category: "Gitter & Diskretisierung",
    status: "neu",
    effort: "groß",
    why: "Turbinen, Pumpen und Ventile brauchen gleitende Grenzflaechen. Chimera-Gitter vermeiden taumelnde Netze — die Hilbert-Partition muss dann nur die Overlay-Zonen kennen.",
    how: [
      "Overlay-Konnektivitaet via TIOGA/PUMIpic oder eigener BVH (ArborX) auf der GPU",
      "MeshErrorMap erweitert um Loch-Zellen- und Orphan-Point-Flags",
      "Konservative Interpolation der Fluisse zwischen Hintergrund- und Rotorgitter",
    ],
  },
  {
    title: "In-Situ-Visualisierung mit ADIOS2 + ZFP",
    category: "Werkzeuge & I/O",
    status: "neu",
    effort: "mittel",
    why: "VTK-Dumps skalieren am schlechtesten. In-Situ-Rendering auf den GPU-Kernen plus ZFP-Kompression entfernt den I/O-Flaschenhals komplett.",
    how: [
      "Ascent/ParaView-Catalyst direkt im Solver-Loop",
      "ZFP-Kompression nur fuer Halo-Zonen mit niedriger MeshErrorMap-Sensitivitaet",
      "ADIOS2-SST-Streaming statt Dateisystem",
    ],
  },
  {
    title: "Fault-Toleranter Cloud-Betrieb (SCR + preemptible VMs)",
    category: "Cloud & HPC",
    status: "neu",
    effort: "mittel",
    why: "Spot-Instanzen sind 60–70 % guenstiger, koennen aber jederzeit verschwinden. Checkpoint/Restart auf dem Hilbert-Split ist dafuer perfekt, weil jeder Node sein Index-Intervall kennt.",
    how: [
      "SCR-Checkpoints in Intervallen, Halo-Zustand inklusive",
      "Enzyme/Sundials-Checkpoint-Recomputation statt Speicherung des Tapes",
      "Node-Ausfall → DynamicRedistributor verschiebt den Hilbert-Schnittpunkt",
    ],
  },
  {
    title: "Autotuning der Segment-Schwellen",
    category: "Numerik",
    status: "neu",
    effort: "klein",
    why: "ε_skew und ε_ortho entscheiden, wie viele Faces den teuren Pfad nehmen. Der optimale Wert haengt von GPU, Gitter und Feld ab — also messen statt raten.",
    how: [
      "Kernel Tuner swept (ε_skew, ε_ortho, Blockgroesse, Vector Width)",
      "Ziel: minimaler Gesamtzeit = Fast-Path-Anteil × Traffic + Korrektur-Kosten",
      "Gelernte Schwellen pro Geometrie-Familie im Metrik-Predictor ablegen",
    ],
  },
  {
    title: "FMI 3.0 als dritte Kopplungsschiene",
    category: "Kopplung & Transfer",
    status: "neu",
    effort: "klein",
    why: "Neben preCICE (partitioniert, stark) und MUI (header-only) fehlt der Standard fuer Systemsimulation: Regelungstechnik, Modelica-Modelle und Co-Simulation lassen sich per FMI 3.0 andocken.",
    how: [
      "FMI 3.0 Scheduled-Execution-Interface fuer feste Zeitschritt-Kopplung",
      "Solver als FMU exportieren → Dymola/OpenModelica als uebergeordneter Regler",
      "Gemeinsamer Mapping-Layer (RBF/konservativ) mit DataTransferKit",
    ],
  },
];

/* ------------------------------------------------------------------ */

export const NAV = [
  { id: "ueberblick", label: "Überblick", hint: "Stand der Architektur" },
  { id: "stack", label: "Bibliotheks-Stack", hint: "Lineare Algebra, Chemie, Kopplung" },
  { id: "pipeline", label: "Pipeline", hint: "CAD → Pilot → Mesh → Solve" },
  { id: "errormap", label: "MeshErrorMap", hint: "4-Segment-Partition interaktiv" },
  { id: "kernels", label: "Kernel & Streams", hint: "Code & Taktplan" },
  { id: "accel", label: "Hardware-Beschleuniger", hint: "RT-Cores, Tensor Cores, DEC" },
  { id: "scaling", label: "Cloud-Scaling", hint: "Hilbert, Halo, s-Step" },
  { id: "ideen", label: "Ideen & Roadmap", hint: "Was noch fehlt" },
] as const;

export type NavId = (typeof NAV)[number]["id"];
