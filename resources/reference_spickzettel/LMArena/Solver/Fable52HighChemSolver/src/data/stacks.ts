import type { ScaleId } from "../lib/scale";

export interface Layer {
  scale: ScaleId;
  solver: string;
  resolves: string;
  /** What this layer passes to the next coarser one. */
  handsOn?: string;
}

export interface Stack {
  id: string;
  tab: string;
  title: string;
  why: string;
  layers: Layer[];
  kernel: { name: string; note: string };
  coupling: { name: string; note: string };
}

export const STACKS: Stack[] = [
  {
    id: "combustion",
    tab: "Reacting flow",
    title: "Reacting gas flow and combustion",
    why: "The scales separate cleanly here, so nothing needs to be coupled at run time. Fine-scale results become a better mechanism, and the flow solver does the heavy lifting on GPUs.",
    layers: [
      {
        scale: "micro",
        solver: "LAMMPS with ReaxFF",
        resolves: "Reaction pathways and rates for species your mechanism does not cover yet.",
        handsOn: "Arrhenius parameters, transport coefficients",
      },
      {
        scale: "meso",
        solver: "SPARTA (only if rarefied)",
        resolves: "Shocks, low-pressure zones and microchannels where the continuum assumption fails.",
        handsOn: "Slip and accommodation boundary conditions",
      },
      {
        scale: "macro",
        solver: "PeleLMeX or PeleC",
        resolves:
          "Flame structure and turbulence on adaptive meshes. Switch to OpenFOAM when complex industrial geometry matters more than throughput.",
      },
    ],
    kernel: {
      name: "Cantera, then PelePhysics",
      note: "Build, validate and reduce the mechanism in Cantera. PelePhysics turns it into generated code for the solver.",
    },
    coupling: {
      name: "Sequential hand-off",
      note: "Run the fine scale offline and pass parameters upward as files.",
    },
  },
  {
    id: "catalysis",
    tab: "Surfaces",
    title: "Catalysis, surfaces and electrochemistry",
    why: "Surface chemistry lives on timescales that molecular dynamics cannot reach and continuum models cannot resolve. Kinetic Monte Carlo bridges the two, and MOOSE can run it as a sub-application.",
    layers: [
      {
        scale: "micro",
        solver: "LAMMPS",
        resolves:
          "Adsorption, diffusion and interface structure with reactive or machine-learned potentials. Energy barriers usually come from DFT upstream.",
        handsOn: "Event rates and barriers",
      },
      {
        scale: "meso",
        solver: "SPPARKS",
        resolves: "Surface coverage, deposition and microstructure evolution over seconds to hours.",
        handsOn: "Effective surface rates, microstructure",
      },
      {
        scale: "macro",
        solver: "MOOSE",
        resolves:
          "Device-scale species transport, heat and mechanics, with phase-field models for evolving interfaces.",
      },
    ],
    kernel: {
      name: "Cantera",
      note: "Surface and electrochemical kinetics with consistent thermodynamics.",
    },
    coupling: {
      name: "MOOSE MultiApps, or preCICE",
      note: "MultiApps runs sub-simulations inside MOOSE. Use preCICE to bring in OpenFOAM for the fluid side.",
    },
  },
  {
    id: "porous",
    tab: "Porous media",
    title: "Porous media and reactive transport",
    why: "Resolve real pore geometry once, average it, and let a Darcy-scale code carry the result across metres. The chemistry step is a library call in each cell.",
    layers: [
      {
        scale: "micro",
        solver: "LAMMPS (optional)",
        resolves: "Mineral–fluid interfaces, wetting and ion transport in nanopores.",
        handsOn: "Surface rates, contact angles",
      },
      {
        scale: "meso",
        solver: "OpenLB",
        resolves: "Flow and reaction in resolved pore space, straight from CT images.",
        handsOn: "Permeability, effective diffusivity, effective reaction rates",
      },
      {
        scale: "macro",
        solver: "DuMux or MOOSE",
        resolves:
          "Darcy-scale multi-phase reactive transport. DuMux is the specialist; MOOSE adds mechanics and heat through its Porous Flow and Geochemistry modules.",
      },
    ],
    kernel: {
      name: "Reaktoro",
      note: "Aqueous equilibrium and mineral kinetics, called once per cell.",
    },
    coupling: {
      name: "preCICE",
      note: "Couples free flow in OpenFOAM to the porous domain in DuMux across a shared interface.",
    },
  },
];
