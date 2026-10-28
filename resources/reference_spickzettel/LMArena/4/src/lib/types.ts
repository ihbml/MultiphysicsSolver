export type PresetId =
  | "cylinder"
  | "shock_tube"
  | "explosion"
  | "vortex"
  | "box_obstacle";

export interface ObstacleSpec {
  type: "circle" | "rect";
  x?: number;
  y?: number;
  r?: number;
  x0?: number;
  y0?: number;
  x1?: number;
  y1?: number;
}

export interface MeshParams {
  preset: PresetId;
  name: string;
  domain: { xmin: number; xmax: number; ymin: number; ymax: number };
  target: number;
  jitter: number;
  relax: number;
  wallLayer: number;
  obstacles: ObstacleSpec[];
}

export type BoundaryKind = "wall" | "inlet" | "outlet" | "farfield";

export interface SolverParams {
  ic: "shock_obstacle" | "sod" | "explosion" | "vortex";
  mach: number;
  aoa: number;
  cfl: number;
  steps: number;
  order: 1 | 2;
  snapshots: number;
  gamma: number;
  rho0: number;
  p0: number;
  bcLeft: BoundaryKind;
  bcRight: BoundaryKind;
  bcTop: BoundaryKind;
  bcBottom: BoundaryKind;
  wallClosure: "sph" | "mirror";
  kappa: number;
  interpolation: boolean;
  sensitivity: boolean;
}

export interface MeshMeta {
  cellCount: number;
  vertexCount: number;
  faceCount: number;
  interiorFaces: number;
  boundaryFaces: number;
  obstacleFaces: number;
  seedCount: number;
  spacing: number;
  target: number;
  relaxIterations: number;
  jitter: number;
  wallLayer: number;
  wallClosure: string;
  buildMs: number;
  builder: string;
  domain: { xmin: number; xmax: number; ymin: number; ymax: number };
}

export interface MeshStats {
  minQuality: number;
  meanQuality: number;
  maxQuality: number;
  coveredArea: number;
  domainArea: number;
  solidArea: number;
  coverage: number;
  meanConsistencyDeficiency: number;
  maxConsistencyDeficiency: number;
  meanStencil: number;
}

export interface MeshGeometry {
  /** [cx, cy, area, quality, consistency, faceCount] per cell */
  cells: number[][];
  /** [x0, y0, x1, y1, ...] flattened vertex coordinates */
  verts: number[];
  /** [v0, v1, owner, neighbour(-1), length, cx, cy, bc, nx, ny] */
  faces: number[][];
  graph: { offsets: number[]; indices: number[] };
  cellArray: { mindims: number[]; minEdge: number; maxEdge: number };
}

export interface MeshResult {
  meta: MeshMeta;
  stats: MeshStats;
  geometry: MeshGeometry;
}

export interface Snapshot {
  step: number;
  time: number;
  dt: number;
  mass: number;
  energy: number;
  drag: number;
  lift: number;
  maxMach: number;
  minRho: number;
  maxRho: number;
  rho: number[];
  u: number[];
  v: number[];
  p: number[];
  Ma: number[];
}

export interface SolveStats {
  finalTime: number;
  avgDt: number;
  solveMs: number;
  faceEvalsPerSecond: number;
  totalMass: number;
  totalEnergy: number;
  drag: number;
  lift: number;
  consistencyDeficiencyMean: number;
  consistencyDeficiencyMax: number;
  gradMach: number;
  gradKappa: number;
  sensitivityRuns: number;
  sensitivityMs: number;
}

export interface InterpolationStats {
  nnz: number;
  meanStencil: number;
  sparsity: number;
  conservationError: number;
  l2Error: number;
  linfError: number;
  timeMs: number;
}

export interface SolveMeta {
  cellCount: number;
  steps: number;
  order: number;
  scheme: string;
  wallClosure: string;
  ic: string;
  mach: number;
  cfl: number;
  gamma: number;
  kappa: number;
}

export interface SurfaceFace {
  cx: number;
  cy: number;
  nx: number;
  ny: number;
  len: number;
  p: number;
}

export interface SolveResult {
  meta: SolveMeta;
  stats: SolveStats;
  interpolation: InterpolationStats;
  surface: number[][];
  snapshots: Snapshot[];
}

export interface NativeChainResult {
  mesh: { meta: MeshMeta; stats: MeshStats } & MeshGeometry;
  solve: SolveResult;
}

// ---- API response contracts shared with the client ----
export interface MeshResponse {
  mesh: {
    id: number;
    name: string;
    preset: PresetId;
    params: MeshParams;
    meta: MeshMeta;
    stats: MeshStats;
    geometry: MeshGeometry;
    cached?: boolean;
  };
  timings: { nativeWallMs: number; endToEndMs: number };
}

export interface SimulationResponse {
  simulation: {
    id: number;
    meshId: number;
    params: { mesh: MeshParams; solver: SolverParams };
    meta: SolveMeta;
    stats: SolveStats;
    interpolation: InterpolationStats;
    surface: number[][];
    snapshots: Snapshot[];
    computeMs: number;
    createdAt?: string;
  };
  mesh: {
    id: number;
    meta: MeshMeta;
    stats: MeshStats;
    geometry: MeshGeometry;
    cached?: boolean;
  };
  timings: { nativeWallMs: number; endToEndMs: number };
}

export interface RunSummary {
  id: number;
  meshId: number;
  params?: { mesh: MeshParams; solver: SolverParams };
  meta: SolveMeta;
  stats: SolveStats;
  interpolation?: InterpolationStats;
  computeMs: number;
  createdAt: string | Date;
}
