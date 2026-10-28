import type { MeshSpec } from "./types";

export interface Preset {
  id: string;
  name: string;
  summary: string;
  /** which of the three reference papers the case exercises */
  papers: ("W1" | "W2" | "W3")[];
  features: string[];
  spec: MeshSpec;
}

const wallRules = [
  { name: "movingWall", bc: "moving", where: "ymin", priority: 9 },
  { name: "wall", bc: "wall", where: "all", priority: 0 },
] as const;

export const PRESETS: Preset[] = [
  {
    id: "eisph-cavity",
    name: "Lid-driven cavity (Eulerian SPH + FVM)",
    summary:
      "Square cavity discretised twice: an unstructured cell-centred FVM mesh with the DiFVM static graph, and a body-fitted Eulerian SPH particle set whose boundary particles carry the FVM-equivalent extra storage (|A_iS|, n_iS, wall distance) instead of a contact/ghost configuration.",
    papers: ["W1", "W3"],
    features: [
      "particle relaxation -> body-fitted distribution",
      "zero-order consistency sum_j V_j gradW_ij = 0",
      "divergence-of-position boundary detector (gamma = 0.75 d)",
      "owner/neighbour CSR graph + non-orthogonality correction",
    ],
    spec: {
      name: "eisph-lid-driven-cavity",
      geometry: { kind: "box", min: [0, 0], max: [1, 1] },
      fvm: {
        baseCellSize: 1 / 64,
        smoothingIters: 30,
        patchRules: [...wallRules],
      },
      sph: {
        particleSpacing: 1 / 64,
        kernel: "wendlandC2",
        supportRatio: 2.6,
        gradientCorrection: true,
        relaxation: { enabled: true, iterations: 250, coefficient: 0.3, boundarySnap: true },
      },
      output: { formats: ["vtu", "msh", "json"] },
    },
  },
  {
    id: "adaptive-sph-fvm-cylinder",
    name: "Adaptive SPH-FVM: cylinder in channel",
    summary:
      "The [W2] adaptive coupling model: Riemann-SPH only inside a pre-defined core around the cylinder, FVM everywhere else. The core is buffered, particles outside it are handed to the mesh, and the optimized particle-mesh interpolation scheme (OPMIS) is verified with a patch test against Shepard and nearest-neighbour transfer.",
    papers: ["W2", "W3", "W1"],
    features: [
      "adaptive particle domain (core / buffer / mesh)",
      "OPMIS: kernel-cell overlap quadrature + linear moment correction",
      "patch test on 1, x, y, x^2+y^2, sin*sin",
      "3-row stretched boundary layer on the body",
      "distance-based size field refinement",
    ],
    spec: {
      name: "adaptive-sph-fvm-cylinder",
      geometry: {
        kind: "difference",
        a: { kind: "box", min: [0, 0], max: [2.2, 0.41] },
        b: { kind: "circle", center: [0.2, 0.2], radius: 0.05 },
      },
      fvm: {
        baseCellSize: 0.011,
        smoothingIters: 25,
        vertexDual: true,
        boundaryLayer: { count: 3, firstHeight: 0.0022, growth: 1.3, patch: "body" },
        refinement: {
          targets: [
            { shape: "circle", center: [0.2, 0.2], radius: 0.05, hNear: 0.0045, width: 0.3 },
          ],
        },
        patchRules: [
          { name: "inlet", bc: "inflow", where: "xmin", priority: 9 },
          { name: "outlet", bc: "outflow", where: "xmax", priority: 9 },
          { name: "body", bc: "wall", where: "hole", priority: 8 },
          { name: "wall", bc: "wall", where: "all", priority: 0 },
        ],
      },
      sph: {
        particleSpacing: 0.007,
        kernel: "wendlandC2",
        levels: 3,
        levelField: {
          kind: "distance",
          targets: [
            { shape: "circle", center: [0.2, 0.2], radius: 0.05, width: 0.04, level: 2 },
            { shape: "circle", center: [0.2, 0.2], radius: 0.05, width: 0.1, level: 1 },
          ],
        },
        relaxation: { enabled: true, iterations: 120, coefficient: 0.3 },
      },
      coupling: {
        enabled: true,
        mode: "adaptive",
        coreRegion: { shape: "circle", center: [0.2, 0.2], radius: 0.12 },
        bufferWidth: 0.028,
        interpolation: {
          particleToMesh: "opmis",
          meshToParticle: "opmis",
          momentCorrection: true,
          quadrature: 3,
        },
        patchTest: true,
      },
      output: { formats: ["vtu", "msh", "json"] },
    },
  },
  {
    id: "shock-tube-coupled",
    name: "Shock tube (SPH core + FVM far field)",
    summary:
      "One-dimensional Riemann problem used in [W2] to verify accuracy, convergence and the stability of the particle-to-mesh transfer when a high-pressure wave crosses the coupling region. The particle slab sits in the middle, non-reflecting FVM boundaries absorb the wave at both ends.",
    papers: ["W2"],
    features: [
      "non-reflecting / outflow boundary patches",
      "slab core region with buffer overlap",
      "OPMIS transfer across a strong pressure jump",
    ],
    spec: {
      name: "shock-tube",
      geometry: { kind: "box", min: [0, 0], max: [1, 0.1] },
      fvm: {
        baseCellSize: 1 / 200,
        smoothingIters: 15,
        patchRules: [
          { name: "leftBC", bc: "nonreflecting", where: "xmin", priority: 9 },
          { name: "rightBC", bc: "nonreflecting", where: "xmax", priority: 9 },
          { name: "wall", bc: "reflecting", where: "all", priority: 0 },
        ],
      },
      sph: {
        particleSpacing: 1 / 200,
        kernel: "cubicSpline",
        supportRatio: 2.0,
        relaxation: { enabled: true, iterations: 80, coefficient: 0.3 },
      },
      coupling: {
        enabled: true,
        mode: "adaptive",
        coreRegion: { shape: "box", min: [0.25, 0], max: [0.75, 0.1] },
        bufferWidth: 0.03,
        interpolation: { particleToMesh: "opmis", meshToParticle: "opmis", quadrature: 3 },
        patchTest: true,
      },
    },
  },
  {
    id: "bubble-near-wall",
    name: "High-pressure bubble near a wall",
    summary:
      "Multiphase application of [W2]: a strongly compressible bubble core resolved with particles next to a solid wall, embedded in an FVM domain that carries the non-reflecting far field. The adaptive model keeps the particle count small while the interface region stays meshfree.",
    papers: ["W2", "W1"],
    features: [
      "circular particle core next to a no-slip wall",
      "wall / far-field / non-reflecting boundary conditions",
      "2-level adaptive particle refinement at the interface",
    ],
    spec: {
      name: "bubble-near-wall",
      geometry: { kind: "box", min: [0, 0], max: [0.8, 0.6] },
      fvm: {
        baseCellSize: 0.008,
        smoothingIters: 25,
        refinement: {
          targets: [{ shape: "circle", center: [0.4, 0.18], radius: 0.05, hNear: 0.004, width: 0.2 }],
        },
        patchRules: [
          { name: "wall", bc: "noslip", where: "ymin", priority: 9 },
          { name: "farfield", bc: "nonreflecting", where: "all", priority: 0 },
        ],
      },
      sph: {
        particleSpacing: 0.005,
        levels: 2,
        levelField: {
          kind: "distance",
          targets: [{ shape: "circle", center: [0.4, 0.18], radius: 0.05, width: 0.05, level: 1 }],
        },
        relaxation: { enabled: true, iterations: 100, coefficient: 0.3 },
      },
      coupling: {
        enabled: true,
        mode: "adaptive",
        coreRegion: { shape: "circle", center: [0.4, 0.18], radius: 0.1 },
        bufferWidth: 0.02,
        patchTest: true,
      },
    },
  },
  {
    id: "airfoil-farfield",
    name: "NACA 2412 with far-field boundary",
    summary:
      "External aerodynamics domain: a NACA 4-digit airfoil carved out of a far-field box, a stretched boundary layer, wake refinement and the far-field / non-reflecting boundary conditions of [W1]. The Eulerian SPH set is body-fitted around the profile by particle relaxation.",
    papers: ["W1", "W3"],
    features: [
      "NACA 4-digit profile from a signed distance field",
      "4-row boundary layer with geometric growth",
      "wake refinement band",
      "far-field + non-reflecting outlet patches",
    ],
    spec: {
      name: "naca2412-farfield",
      geometry: {
        kind: "difference",
        a: { kind: "box", min: [-0.6, -0.5], max: [2.0, 0.5] },
        b: { kind: "airfoil", naca: "2412", chord: 1.0, origin: [0, 0], angle: 4, samples: 140 },
      },
      fvm: {
        baseCellSize: 0.02,
        smoothingIters: 25,
        boundaryLayer: { count: 4, firstHeight: 0.0012, growth: 1.25, patch: "body" },
        refinement: {
          targets: [
            { shape: "box", min: [-0.1, -0.15], max: [1.6, 0.15], hNear: 0.008, width: 0.25 },
          ],
        },
        patchRules: [
          { name: "inlet", bc: "farfield", where: "xmin", priority: 9 },
          { name: "outlet", bc: "nonreflecting", where: "xmax", priority: 9 },
          { name: "farfield", bc: "farfield", where: "all", priority: 1 },
          { name: "body", bc: "noslip", where: "hole", priority: 8 },
        ],
      },
      sph: {
        particleSpacing: 0.012,
        levels: 2,
        levelField: {
          kind: "distance",
          targets: [{ shape: "box", min: [0, -0.1], max: [1, 0.1], width: 0.08, level: 1 }],
        },
        relaxation: { enabled: true, iterations: 120, coefficient: 0.3 },
      },
    },
  },
  {
    id: "dam-break-tank",
    name: "Dam-break tank (free surface)",
    summary:
      "Classic free-surface configuration: a water column in a tank. The particle set carries the free surface, the tank walls use the generalized no-slip boundary storage of [W1], and the FVM mesh provides the wall-resolved graph for the same geometry.",
    papers: ["W1", "W3"],
    features: ["free surface", "no-slip tank walls", "graded refinement near the gate"],
    spec: {
      name: "dam-break-tank",
      geometry: { kind: "box", min: [0, 0], max: [3.0, 1.5] },
      fvm: {
        baseCellSize: 0.02,
        smoothingIters: 25,
        refinement: {
          targets: [{ shape: "box", min: [0, 0], max: [1.2, 0.7], hNear: 0.01, width: 0.3 }],
        },
        patchRules: [
          { name: "tankWall", bc: "noslip", where: "all", priority: 0 },
          { name: "gate", bc: "moving", where: "xrange", from: [0.58, 0], to: [0.62, 0.7], priority: 9 },
        ],
      },
      sph: {
        particleSpacing: 0.014,
        levels: 2,
        levelField: {
          kind: "distance",
          targets: [{ shape: "box", min: [0.4, 0], max: [0.9, 0.8], width: 0.2, level: 1 }],
        },
        relaxation: { enabled: true, iterations: 100, coefficient: 0.3 },
      },
    },
  },
  {
    id: "annulus-rotor",
    name: "Annulus / rotor gap (periodic machinery)",
    summary:
      "A non-simply-connected domain (annulus) exercising the hole-contour handling of the mesher, symmetry and moving-wall patches, and the median-dual (vertex-centred) volumes that a collocated FV solver can use next to the cell-centred graph.",
    papers: ["W3", "W1"],
    features: ["hole contour", "moving inner wall", "vertex-centred dual volumes", "symmetry patch"],
    spec: {
      name: "annulus-rotor",
      geometry: { kind: "annulus", center: [0, 0], radius: 0.5, thickness: 0.3 },
      fvm: {
        baseCellSize: 0.018,
        smoothingIters: 30,
        vertexDual: true,
        boundaryLayer: { count: 2, firstHeight: 0.004, growth: 1.3, patch: "rotor" },
        patchRules: [
          { name: "rotor", bc: "moving", where: "circle", center: [0, 0], radius: 0.35, priority: 9 },
          { name: "stator", bc: "wall", where: "circle", center: [0, 0], radius: 0.65, priority: 9 },
          { name: "periodic", bc: "symmetry", where: "all", priority: 0 },
        ],
      },
      sph: {
        particleSpacing: 0.014,
        relaxation: { enabled: true, iterations: 120, coefficient: 0.3 },
      },
    },
  },
];

export function getPreset(id: string): Preset | undefined {
  return PRESETS.find((p) => p.id === id);
}

export function presetSummaries() {
  return PRESETS.map((p) => ({
    id: p.id,
    name: p.name,
    summary: p.summary,
    papers: p.papers,
    features: p.features,
    spec: p.spec,
  }));
}
