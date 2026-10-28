/**
 * Job descriptions accepted by the native core. The shapes mirror the JSON
 * spec consumed by `native/build/mesher run --spec ...`.
 */

export type BcKind =
  | "wall"
  | "noslip"
  | "slip"
  | "reflecting"
  | "inflow"
  | "outflow"
  | "farfield"
  | "nonreflecting"
  | "symmetry"
  | "moving";

export interface PatchRule {
  name: string;
  bc: BcKind;
  where:
    | "all"
    | "xmin"
    | "xmax"
    | "ymin"
    | "ymax"
    | "xrange"
    | "yrange"
    | "circle"
    | "hole"
    | "outer";
  priority?: number;
  from?: [number, number];
  to?: [number, number];
  center?: [number, number];
  radius?: number;
}

export interface RefinementTarget {
  shape: "point" | "circle" | "box";
  center?: [number, number];
  radius?: number;
  min?: [number, number];
  max?: [number, number];
  hNear?: number;
  width?: number;
}

export interface GeometryNode {
  kind:
    | "box"
    | "circle"
    | "ellipse"
    | "polygon"
    | "airfoil"
    | "annulus"
    | "star"
    | "halfplane"
    | "union"
    | "intersection"
    | "difference"
    | "transform"
    | "array";
  [key: string]: unknown;
}

export interface MeshSpec {
  name?: string;
  geometry: GeometryNode;
  domain?: { min: [number, number]; max: [number, number] };
  fvm?: {
    enabled?: boolean;
    baseCellSize?: number;
    minCellSize?: number;
    smoothingIters?: number;
    smoothingWeight?: number;
    vertexDual?: boolean;
    boundaryLayer?: {
      count?: number;
      firstHeight?: number;
      growth?: number;
      patch?: string;
      onlyMatchingPatch?: boolean;
    };
    refinement?: { targets?: RefinementTarget[] };
    patchRules?: PatchRule[];
  };
  sph?: {
    enabled?: boolean;
    particleSpacing?: number;
    kernel?: "wendlandC2" | "cubicSpline" | "wendlandC4";
    /** kernel support radius R = supportRatio * dp  (SPHinXsys: R = 2h, h = 1.3 dp) */
    supportRatio?: number;
    volumeModel?: "nominal" | "shepard";
    gradientCorrection?: boolean;
    gammaFactor?: number;
    levels?: number;
    levelField?: {
      kind?: "off" | "distance" | "core";
      radius?: number;
      targets?: (RefinementTarget & { level?: number })[];
    };
    relaxation?: {
      enabled?: boolean;
      iterations?: number;
      coefficient?: number;
      boundarySnap?: boolean;
      bandFactor?: number;
    };
  };
  coupling?: {
    enabled?: boolean;
    mode?: "adaptive" | "fixed" | "sphOnly";
    coreRegion?: {
      shape?: "circle" | "box" | "all";
      center?: [number, number];
      radius?: number;
      min?: [number, number];
      max?: [number, number];
    };
    bufferWidth?: number;
    interpolation?: {
      particleToMesh?: "opmis" | "shepard" | "nearest";
      meshToParticle?: "opmis" | "shepard" | "nearest";
      momentCorrection?: boolean;
      quadrature?: 1 | 3 | 7;
    };
    patchTest?: boolean;
  };
  output?: {
    formats?: ("vtu" | "msh" | "json")[];
    previewLimit?: number;
    preview?: boolean;
  };
  threads?: number;
}

export interface PatchTestResult {
  schemes: string[];
  fields: string[];
  relativeL2: number[][];
}

export interface JobManifest {
  ok: boolean;
  engine: string;
  threads: number;
  caseName: string;
  totalMs: number;
  timings: Record<string, number>;
  counts: {
    nodes: number;
    cells: number;
    faces: number;
    interiorFaces: number;
    boundaryFaces: number;
    patches: number;
    particles: number;
    particleNeighbourPairs: number;
    p2mNnz: number;
    m2pNnz: number;
  };
  throughput: { cellsPerSecond: number; particlesPerSecond: number; pipelineMs: number };
  mesh: {
    valid: boolean;
    domainArea: number;
    perimeter: number;
    aspectMin: number;
    aspectMax: number;
    aspectAvg: number;
    skewMax: number;
    skewAvg: number;
    nonOrthoMax: number;
    nonOrthoAvg: number;
    nonOrthoP99: number;
    invertedCells: number;
    minVolume: number;
    maxVolume: number;
    patches: { name: string; bc: string; faces: number; length: number }[];
  };
  sph: {
    valid: boolean;
    kernel: string;
    supportRatio: number;
    baseSpacing: number;
    levels: number;
    nInterior: number;
    nBoundary: number;
    nGhost: number;
    nCore: number;
    nBuffer: number;
    meanNeighbours: number;
    spacingCV: number;
    divPosMean: number;
    divPosMin: number;
    gammaThreshold: number;
    consistencyMean: number;
    consistencyMeanInterior: number;
    consistencyScaleInterior: number;
    relaxResidualBefore: number;
    relaxResidualAfter: number;
    relaxIterations: number;
    boundaryAreaSum: number;
    halfPerimeter: number;
    perimeterErrorRel: number;
    volumeErrorRel: number;
    sphRegionVolumeErrorRel: number;
  };
  coupling: {
    enabled: boolean;
    mode: string;
    bufferWidth: number;
    p2mScheme: string;
    m2pScheme: string;
    fvmCells: number;
    bufferCells: number;
    ghostCells: number;
    interfaceFaces: number;
    coreParticles: number;
    bufferParticles: number;
    coreArea: number;
    bufferArea: number;
    p2mNnz: number;
    m2pNnz: number;
    p2mRowSumErrMax: number;
    m2pRowSumErrMax: number;
    patchTest: PatchTestResult;
  };
  warnings: string[];
  artifacts: { name: string; path: string; format: string; bytes: number }[];
  preview?: {
    bboxMinX: number;
    bboxMinY: number;
    bboxMaxX: number;
    bboxMaxY: number;
    cellStride: number;
    cellX: number[];
    cellY: number[];
    cellRegion: number[];
    boundarySeg: number[];
    boundaryPatch: number[];
    particleStride: number;
    particleX: number[];
    particleY: number[];
    particleCategory: number[];
    particleRegion: number[];
    particleDivPos: number[];
    particleLevel: number[];
    interfaceX: number[];
    interfaceY: number[];
  };
}
