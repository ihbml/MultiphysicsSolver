export type Role = "solver" | "kernel" | "coupler";
export type Gpu = "yes" | "partial" | "no" | "na";

export interface Solver {
  id: string;
  name: string;
  maintainer: string;
  role: Role;
  /** Numerical method in a few words. */
  method: string;
  language: string;
  license: string;
  /** BSD/MIT/Apache or LGPL: can be linked from closed-source code. */
  linkable: boolean;
  gpu: Gpu;
  gpuNote: string;
  parallel: string;
  /** Typical length scales as log10(metres). Null for scale-agnostic libraries. */
  range: [number, number] | null;
  chemistry: string;
  bestFor: string;
  watchOut: string;
  url: string;
  topPick?: boolean;
}

export const SOLVERS: Solver[] = [
  {
    id: "lammps",
    name: "LAMMPS",
    maintainer: "Sandia National Laboratories",
    role: "solver",
    method: "Molecular dynamics",
    language: "C++",
    license: "GPL-2.0",
    linkable: false,
    gpu: "yes",
    gpuNote: "Kokkos and GPU packages cover most common pair styles, including ReaxFF.",
    parallel: "MPI with spatial decomposition, plus OpenMP",
    range: [-10, -6],
    chemistry:
      "Reactive force fields (ReaxFF, AIREBO, COMB), template-based bond reactions, and machine-learned potentials such as SNAP and ACE.",
    bestFor:
      "Bond breaking and formation, interfaces, and extracting rate constants or transport coefficients to feed a continuum model.",
    watchOut:
      "Reachable time is nanoseconds to microseconds. Results are only as good as the force-field parameterisation.",
    url: "https://www.lammps.org",
    topPick: true,
  },
  {
    id: "espresso",
    name: "ESPResSo",
    maintainer: "University of Stuttgart",
    role: "solver",
    method: "Coarse-grained MD + lattice Boltzmann",
    language: "C++ core, Python API",
    license: "GPL-3.0",
    linkable: false,
    gpu: "partial",
    gpuNote: "Selected methods such as lattice Boltzmann and electrostatics run on CUDA GPUs.",
    parallel: "MPI",
    range: [-9, -6],
    chemistry:
      "Reaction-ensemble and constant-pH Monte Carlo, electrokinetics, and particles coupled to a lattice-Boltzmann fluid.",
    bestFor: "Electrolytes, polymers, colloids and other charged soft-matter systems.",
    watchOut:
      "Coarse-grained by design. Not the tool for bond-level chemistry in gases or crystalline solids.",
    url: "https://espressomd.org",
  },
  {
    id: "spparks",
    name: "SPPARKS",
    maintainer: "Sandia National Laboratories",
    role: "solver",
    method: "Kinetic Monte Carlo",
    language: "C++",
    license: "GPL",
    linkable: false,
    gpu: "no",
    gpuNote: "CPU only.",
    parallel: "MPI",
    range: [-9, -5],
    chemistry:
      "On- and off-lattice event models: surface reactions, diffusion, deposition, grain growth and sintering.",
    bestFor:
      "Surface catalysis and film growth over seconds to hours, which molecular dynamics cannot reach.",
    watchOut:
      "You supply the event catalogue and its rates, usually from DFT or MD. Expect to write an application class.",
    url: "https://spparks.github.io",
  },
  {
    id: "moose",
    name: "MOOSE",
    maintainer: "Idaho National Laboratory",
    role: "solver",
    method: "Implicit finite element / finite volume",
    language: "C++",
    license: "LGPL-2.1",
    linkable: true,
    gpu: "partial",
    gpuNote: "Kokkos and MFEM backends are recent and cover part of the framework.",
    parallel: "MPI plus threads, built on libMesh and PETSc",
    range: [-8, 1],
    chemistry:
      "Chemical Reactions, Geochemistry, Phase Field, Porous Flow, Navier–Stokes and Heat Transfer modules that couple fully implicitly with automatic differentiation.",
    bestFor:
      "Tightly coupled physics in one code base. The MultiApp and Transfer systems run fine-scale sub-simulations inside a coarse one.",
    watchOut:
      "Steep learning curve. A general implicit framework is slower than a specialised explicit code for the same problem, and there is no atomistic solver.",
    url: "https://mooseframework.inl.gov",
    topPick: true,
  },
  {
    id: "sparta",
    name: "SPARTA",
    maintainer: "Sandia National Laboratories",
    role: "solver",
    method: "Direct simulation Monte Carlo",
    language: "C++",
    license: "GPL",
    linkable: false,
    gpu: "yes",
    gpuNote: "Kokkos backend for GPUs and many-core CPUs.",
    parallel: "MPI with load-balanced grid decomposition",
    range: [-7, 0],
    chemistry: "Collision-based gas-phase reactions and gas–surface reaction models.",
    bestFor:
      "Rarefied and non-equilibrium gases: low-pressure deposition reactors, microchannels, hypersonic flow.",
    watchOut:
      "Gases only. Cost climbs steeply as the flow approaches the continuum regime, where a CFD code is the better choice.",
    url: "https://sparta.github.io",
  },
  {
    id: "openlb",
    name: "OpenLB",
    maintainer: "Karlsruhe Institute of Technology",
    role: "solver",
    method: "Lattice Boltzmann",
    language: "C++",
    license: "GPL-2.0",
    linkable: false,
    gpu: "yes",
    gpuNote: "CUDA backend alongside vectorised CPU execution.",
    parallel: "MPI plus OpenMP",
    range: [-7, -1],
    chemistry:
      "Advection–diffusion–reaction, multi-component flow, resolved particles and porous geometry.",
    bestFor:
      "Pore-scale reactive transport and packed beds, especially on geometry taken from CT scans.",
    watchOut:
      "Low-Mach, weakly compressible flow. Stiff detailed mechanisms are awkward. Palabos (AGPL-3.0) is the closest alternative.",
    url: "https://www.openlb.net",
  },
  {
    id: "pele",
    name: "Pele",
    maintainer: "LBNL, NREL and Sandia",
    role: "solver",
    method: "Finite volume with block-structured AMR",
    language: "C++",
    license: "BSD-3",
    linkable: true,
    gpu: "yes",
    gpuNote: "CUDA, HIP and SYCL through AMReX, including the chemistry integration.",
    parallel: "MPI plus OpenMP or GPU",
    range: [-6, 1],
    chemistry:
      "Detailed finite-rate chemistry from generated mechanism code (PelePhysics), stiff integration with SUNDIALS CVODE, multi-species transport, sprays and soot.",
    bestFor:
      "Direct and large-eddy simulation of reacting flow where throughput matters. PeleLMeX covers low-Mach, PeleC compressible.",
    watchOut:
      "Combustion-centred. Geometry goes through embedded boundaries, and the community is smaller than OpenFOAM's.",
    url: "https://amrex-combustion.github.io",
    topPick: true,
  },
  {
    id: "openfoam",
    name: "OpenFOAM",
    maintainer: "OpenCFD (ESI) and the OpenFOAM Foundation",
    role: "solver",
    method: "Unstructured finite volume",
    language: "C++",
    license: "GPL-3.0",
    linkable: false,
    gpu: "partial",
    gpuNote: "Linear solvers only, through external plugins such as PETSc, AmgX or Ginkgo.",
    parallel: "MPI domain decomposition",
    range: [-5, 2],
    chemistry:
      "Reacting-flow solvers with finite-rate chemistry, turbulence–chemistry interaction models, Lagrangian sprays and conjugate heat transfer.",
    bestFor:
      "Industrial geometry and the widest set of ready-made physics, tutorials and third-party extensions.",
    watchOut:
      "Second-order and CPU-bound. Chemistry integration dominates run time on detailed mechanisms, and the two main forks are drifting apart.",
    url: "https://www.openfoam.com",
  },
  {
    id: "dumux",
    name: "DuMux",
    maintainer: "University of Stuttgart",
    role: "solver",
    method: "Finite volume on DUNE grids",
    language: "C++",
    license: "GPL-3.0",
    linkable: false,
    gpu: "no",
    gpuNote: "CPU only.",
    parallel: "MPI plus shared-memory assembly",
    range: [-5, 3],
    chemistry:
      "Multi-phase, multi-component reactive transport, mineralisation, pore-network models and free-flow to porous-medium coupling.",
    bestFor: "Subsurface flow, fuel-cell and electrolyser layers, porous electrodes.",
    watchOut: "Porous media only, and the template-heavy code takes time to read.",
    url: "https://dumux.org",
  },
  {
    id: "cantera",
    name: "Cantera",
    maintainer: "Community project, NumFOCUS",
    role: "kernel",
    method: "Kinetics, thermodynamics, transport",
    language: "C++ core, Python API",
    license: "BSD-3",
    linkable: true,
    gpu: "no",
    gpuNote: "CPU only.",
    parallel: "Serial library; give each thread its own objects",
    range: null,
    chemistry:
      "Gas, surface and electrochemical kinetics, equations of state, transport properties, zero-dimensional reactor networks and one-dimensional flames.",
    bestFor:
      "Developing, validating and reducing a mechanism before it goes anywhere near a 3D solver. The reference most other tools are checked against.",
    watchOut:
      "Not designed for millions of batched per-cell calls. Use generated code or TChem inside the hot loop.",
    url: "https://cantera.org",
  },
  {
    id: "tchem",
    name: "TChem",
    maintainer: "Sandia National Laboratories",
    role: "kernel",
    method: "Batched kinetics on Kokkos",
    language: "C++",
    license: "BSD-2",
    linkable: true,
    gpu: "yes",
    gpuNote: "Evaluates many cells at once on GPUs or many-core CPUs through Kokkos.",
    parallel: "Kokkos (CUDA, OpenMP)",
    range: null,
    chemistry:
      "Thermodynamics, gas-phase and surface kinetics, source terms and Jacobians evaluated in batches.",
    bestFor: "The chemistry sub-step of your own GPU flow solver.",
    watchOut: "Small community and fewer models than Cantera.",
    url: "https://github.com/sandialabs/TChem",
  },
  {
    id: "reaktoro",
    name: "Reaktoro",
    maintainer: "ETH Zürich",
    role: "kernel",
    method: "Chemical equilibrium and kinetics",
    language: "C++ core, Python API",
    license: "LGPL-2.1",
    linkable: true,
    gpu: "no",
    gpuNote: "CPU only.",
    parallel: "Serial library; parallelise across cells yourself",
    range: null,
    chemistry:
      "Equilibrium and kinetics for aqueous, mineral and multi-phase systems, backed by standard thermodynamic databases.",
    bestFor: "The chemistry step in reactive-transport and geochemical models.",
    watchOut: "Aimed at aqueous and geochemical systems, not gas-phase combustion.",
    url: "https://reaktoro.org",
  },
  {
    id: "precice",
    name: "preCICE",
    maintainer: "University of Stuttgart and TU Munich",
    role: "coupler",
    method: "Partitioned coupling library",
    language: "C++",
    license: "LGPL-3.0",
    linkable: true,
    gpu: "na",
    gpuNote: "Not applicable; it moves data between solvers.",
    parallel: "MPI, with direct peer-to-peer links between solvers",
    range: null,
    chemistry:
      "None of its own. It handles data mapping between meshes, communication, time stepping and quasi-Newton acceleration of the coupling iteration.",
    bestFor:
      "Joining two existing continuum codes without rewriting either. Adapters exist for OpenFOAM, DuMux, deal.II, FEniCS, SU2 and others.",
    watchOut:
      "Built for continuum-to-continuum coupling. Atomistic-to-continuum hand-offs need your own averaging logic.",
    url: "https://precice.org",
  },
];
