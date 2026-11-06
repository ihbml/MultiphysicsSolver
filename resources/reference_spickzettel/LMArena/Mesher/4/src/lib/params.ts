import { createHash } from "node:crypto";

import type {
  BoundaryKind,
  MeshParams,
  ObstacleSpec,
  PresetId,
  SolverParams,
} from "./types";

export class ApiError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
    this.name = "ApiError";
  }
}

export interface PresetDefinition {
  id: PresetId;
  label: string;
  description: string;
  papers: string;
  mesh: MeshParams;
  solver: SolverParams;
}

function mesh(
  preset: PresetId,
  name: string,
  domain: MeshParams["domain"],
  target: number,
  obstacles: ObstacleSpec[],
  overrides: Partial<MeshParams> = {},
): MeshParams {
  return {
    preset,
    name,
    domain,
    target,
    jitter: 0.12,
    relax: 2,
    wallLayer: 1,
    obstacles,
    ...overrides,
  };
}

function solver(overrides: Partial<SolverParams> = {}): SolverParams {
  return {
    ic: "shock_obstacle",
    mach: 2,
    aoa: 0,
    cfl: 0.35,
    steps: 320,
    order: 1,
    snapshots: 6,
    gamma: 1.4,
    rho0: 1,
    p0: 1,
    bcLeft: "inlet",
    bcRight: "outlet",
    bcTop: "wall",
    bcBottom: "wall",
    wallClosure: "sph",
    kappa: 1,
    interpolation: true,
    sensitivity: true,
    ...overrides,
  };
}

export const PRESETS: Record<PresetId, PresetDefinition> = {
  cylinder: {
    id: "cylinder",
    label: "Mach 2 cylinder",
    description:
      "Supersonic inflow over a circular obstacle. Bow shock, stagnation pressure and wake are resolved on the Voronoi FVM mesh with an Eulerian-SPH consistent wall closure.",
    papers: "Wang/Haidn/Hu wall closure + Zhang/Ming adaptive SPH-FVM interpolation",
    mesh: mesh("cylinder", "Mach 2 cylinder", { xmin: 0, xmax: 1, ymin: 0, ymax: 1 }, 2400, [
      { type: "circle", x: 0.35, y: 0.5, r: 0.12 },
    ]),
    solver: solver(),
  },
  shock_tube: {
    id: "shock_tube",
    label: "Sod shock tube",
    description:
      "Classic density-driven Riemann problem on a closed box. Used to verify conservation and the absence of spurious boundary mass flux.",
    papers: "DiFVM conservation check",
    mesh: mesh(
      "shock_tube",
      "Sod shock tube",
      { xmin: 0, xmax: 1, ymin: 0, ymax: 0.25 },
      2200,
      [],
      { wallLayer: 0, relax: 3 },
    ),
    solver: solver({
      ic: "sod",
      mach: 0,
      steps: 260,
      bcLeft: "wall",
      bcRight: "wall",
      bcTop: "wall",
      bcBottom: "wall",
      snapshots: 6,
      sensitivity: false,
    }),
  },
  explosion: {
    id: "explosion",
    label: "Blast wave",
    description:
      "High-pressure core released in a quiescent chamber: circular shock and contact wave interacting with the mesh boundary.",
    papers: "Eulerian-SPH boundary consistency",
    mesh: mesh(
      "explosion",
      "Blast wave",
      { xmin: 0, xmax: 1, ymin: 0, ymax: 1 },
      2600,
      [],
      { jitter: 0.18, relax: 2, wallLayer: 0 },
    ),
    solver: solver({
      ic: "explosion",
      mach: 0,
      steps: 220,
      bcLeft: "wall",
      bcRight: "wall",
      bcTop: "wall",
      bcBottom: "wall",
      sensitivity: false,
    }),
  },
  vortex: {
    id: "vortex",
    label: "Isentropic vortex",
    description:
      "Freestream-preserving accuracy test: the vortex must convect without changing shape. Second-order limited reconstruction is enabled by default.",
    papers: "Eulerian SPH / FVM equivalence, zeroth-order consistency",
    mesh: mesh(
      "vortex",
      "Isentropic vortex",
      { xmin: 0, xmax: 1, ymin: 0, ymax: 1 },
      3200,
      [],
      { jitter: 0.05, relax: 3, wallLayer: 0 },
    ),
    solver: solver({
      ic: "vortex",
      mach: 0.5,
      steps: 200,
      order: 2,
      bcLeft: "farfield",
      bcRight: "farfield",
      bcTop: "farfield",
      bcBottom: "farfield",
      sensitivity: false,
    }),
  },
  box_obstacle: {
    id: "box_obstacle",
    label: "Mach 2.5 box",
    description:
      "Rectangular bluff body in a supersonic stream. Tests the ghost-seed carving of non-circular solids and the sensitivity operator.",
    papers: "DiFVM differentiable graph solver",
    mesh: mesh(
      "box_obstacle",
      "Mach 2.5 box",
      { xmin: 0, xmax: 1.4, ymin: 0, ymax: 0.8 },
      3200,
      [{ type: "rect", x0: 0.45, y0: 0.28, x1: 0.62, y1: 0.52 }],
      { wallLayer: 1, relax: 2 },
    ),
    solver: solver({ mach: 2.5, steps: 360, order: 2 }),
  },
};

export function clonePreset(id: PresetId): { mesh: MeshParams; solver: SolverParams } {
  const preset = PRESETS[id];
  return {
    mesh: structuredClone(preset.mesh),
    solver: structuredClone(preset.solver),
  };
}

// ---------------------------------------------------------------------------
// validation
// ---------------------------------------------------------------------------
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function num(value: unknown, fallback: number, label: string, min: number, max: number): number {
  if (value === undefined || value === null) return fallback;
  const parsed = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(parsed)) throw new ApiError(400, `${label} must be a finite number`);
  if (parsed < min || parsed > max) {
    throw new ApiError(400, `${label} must be between ${min} and ${max}`);
  }
  return parsed;
}

function str<T extends string>(value: unknown, fallback: T, label: string, allowed: readonly T[]): T {
  if (value === undefined || value === null) return fallback;
  if (typeof value !== "string" || !allowed.includes(value as T)) {
    throw new ApiError(400, `${label} must be one of ${allowed.join(", ")}`);
  }
  return value as T;
}

function bool(value: unknown, fallback: boolean): boolean {
  if (value === undefined || value === null) return fallback;
  if (typeof value === "boolean") return value;
  if (value === "true") return true;
  if (value === "false") return false;
  return fallback;
}

function int(value: unknown, fallback: number, label: string, min: number, max: number): number {
  return Math.round(num(value, fallback, label, min, max));
}

const BC_VALUES: readonly BoundaryKind[] = ["wall", "inlet", "outlet", "farfield"];

export function parseMeshParams(input: unknown, basePreset: PresetId = "cylinder"): MeshParams {
  const body = isRecord(input) ? input : {};
  const preset = str(body.preset, basePreset, "mesh.preset", Object.keys(PRESETS) as PresetId[]);
  const defaults = clonePreset(preset).mesh;
  const rawDomain = isRecord(body.domain) ? body.domain : {};
  const domain = {
    xmin: num(rawDomain.xmin, defaults.domain.xmin, "domain.xmin", -50, 50),
    xmax: num(rawDomain.xmax, defaults.domain.xmax, "domain.xmax", -50, 50),
    ymin: num(rawDomain.ymin, defaults.domain.ymin, "domain.ymin", -50, 50),
    ymax: num(rawDomain.ymax, defaults.domain.ymax, "domain.ymax", -50, 50),
  };
  const width = domain.xmax - domain.xmin;
  const height = domain.ymax - domain.ymin;
  if (width < 0.05 || height < 0.05) throw new ApiError(400, "domain must be at least 0.05 wide and tall");
  if (width > 20 || height > 20) throw new ApiError(400, "domain must be at most 20 x 20");

  const target = int(body.target, defaults.target, "mesh.target", 150, 20000);
  const obstaclesRaw = Array.isArray(body.obstacles) ? body.obstacles : defaults.obstacles;
  if (obstaclesRaw.length > 3) throw new ApiError(400, "at most 3 obstacles are supported");
  const obstacles: ObstacleSpec[] = obstaclesRaw.map((item, index) => {
    if (!isRecord(item)) throw new ApiError(400, `obstacles[${index}] must be an object`);
    const type = str(item.type, "circle", `obstacles[${index}].type`, ["circle", "rect"] as const);
    if (type === "circle") {
      const cx = num(item.x, domain.xmin + 0.35 * width, `obstacles[${index}].x`, domain.xmin, domain.xmax);
      const cy = num(item.y, domain.ymin + 0.5 * height, `obstacles[${index}].y`, domain.ymin, domain.ymax);
      const r = num(item.r, 0.12 * Math.min(width, height), `obstacles[${index}].r`, 0.01, 0.45 * Math.max(width, height));
      if (cx - r < domain.xmin || cx + r > domain.xmax || cy - r < domain.ymin || cy + r > domain.ymax) {
        throw new ApiError(400, `obstacles[${index}] must lie completely inside the domain`);
      }
      return { type, x: cx, y: cy, r };
    }
    const x0 = num(item.x0, domain.xmin + 0.4 * width, `obstacles[${index}].x0`, domain.xmin, domain.xmax);
    const y0 = num(item.y0, domain.ymin + 0.3 * height, `obstacles[${index}].y0`, domain.ymin, domain.ymax);
    const x1 = num(item.x1, domain.xmin + 0.6 * width, `obstacles[${index}].x1`, domain.xmin, domain.xmax);
    const y1 = num(item.y1, domain.ymin + 0.7 * height, `obstacles[${index}].y1`, domain.ymin, domain.ymax);
    if (x1 - x0 < 0.01 || y1 - y0 < 0.01) throw new ApiError(400, `obstacles[${index}] must have positive size`);
    if (x1 > domain.xmax || y1 > domain.ymax) {
      throw new ApiError(400, `obstacles[${index}] must lie completely inside the domain`);
    }
    return { type, x0, y0, x1, y1 };
  });

  return {
    preset,
    name: typeof body.name === "string" && body.name.trim().length > 0 ? body.name.trim().slice(0, 80) : defaults.name,
    domain,
    target,
    jitter: num(body.jitter, defaults.jitter, "mesh.jitter", 0, 0.45),
    relax: int(body.relax, defaults.relax, "mesh.relax", 0, 8),
    wallLayer: num(body.wallLayer, defaults.wallLayer, "mesh.wallLayer", 0, 1),
    obstacles,
  };
}

export function parseSolverParams(input: unknown, basePreset: PresetId = "cylinder"): SolverParams {
  const body = isRecord(input) ? input : {};
  const defaults = clonePreset(basePreset).solver;
  const orderRaw = int(body.order, defaults.order, "solver.order", 1, 2);
  return {
    ic: str(body.ic, defaults.ic, "solver.ic", [
      "shock_obstacle",
      "sod",
      "explosion",
      "vortex",
    ] as const),
    mach: num(body.mach, defaults.mach, "solver.mach", 0, 8),
    aoa: num(body.aoa, defaults.aoa, "solver.aoa", -45, 45),
    cfl: num(body.cfl, defaults.cfl, "solver.cfl", 0.05, 0.8),
    steps: int(body.steps, defaults.steps, "solver.steps", 5, 1500),
    order: orderRaw === 2 ? 2 : 1,
    snapshots: int(body.snapshots, defaults.snapshots, "solver.snapshots", 0, 8),
    gamma: num(body.gamma, defaults.gamma, "solver.gamma", 1.05, 1.7),
    rho0: num(body.rho0, defaults.rho0, "solver.rho0", 0.05, 10),
    p0: num(body.p0, defaults.p0, "solver.p0", 0.01, 20),
    bcLeft: str(body.bcLeft, defaults.bcLeft, "solver.bcLeft", BC_VALUES),
    bcRight: str(body.bcRight, defaults.bcRight, "solver.bcRight", BC_VALUES),
    bcTop: str(body.bcTop, defaults.bcTop, "solver.bcTop", BC_VALUES),
    bcBottom: str(body.bcBottom, defaults.bcBottom, "solver.bcBottom", BC_VALUES),
    wallClosure: str(body.wallClosure, defaults.wallClosure, "solver.wallClosure", ["sph", "mirror"] as const),
    kappa: num(body.kappa, defaults.kappa, "solver.kappa", 0, 3),
    interpolation: bool(body.interpolation, defaults.interpolation),
    sensitivity: bool(body.sensitivity, defaults.sensitivity),
  };
}

// ---------------------------------------------------------------------------
// native config text
// ---------------------------------------------------------------------------
function fmt(value: number): string {
  return Number.isFinite(value) ? value.toPrecision(12) : "0";
}

export function buildNativeConfig(meshParams: MeshParams, solverParams?: SolverParams): string {
  const lines: string[] = [];
  lines.push(`op=${solverParams ? "solve" : "mesh"}`);
  lines.push(`xmin=${fmt(meshParams.domain.xmin)}`);
  lines.push(`xmax=${fmt(meshParams.domain.xmax)}`);
  lines.push(`ymin=${fmt(meshParams.domain.ymin)}`);
  lines.push(`ymax=${fmt(meshParams.domain.ymax)}`);
  lines.push(`target=${meshParams.target}`);
  lines.push(`jitter=${fmt(meshParams.jitter)}`);
  lines.push(`relax=${meshParams.relax}`);
  lines.push(`walllayer=${fmt(meshParams.wallLayer)}`);
  for (const obstacle of meshParams.obstacles) {
    if (obstacle.type === "circle") {
      lines.push(`obstacle=circle ${fmt(obstacle.x ?? 0)} ${fmt(obstacle.y ?? 0)} ${fmt(obstacle.r ?? 0)}`);
    } else {
      lines.push(
        `obstacle=rect ${fmt(obstacle.x0 ?? 0)} ${fmt(obstacle.y0 ?? 0)} ${fmt(obstacle.x1 ?? 0)} ${fmt(
          obstacle.y1 ?? 0,
        )}`,
      );
    }
  }
  if (!solverParams) return `${lines.join("\n")}\n`;

  const s = solverParams;
  lines.push(`ic=${s.ic}`);
  lines.push(`mach=${fmt(s.mach)}`);
  lines.push(`aoa=${fmt((s.aoa * Math.PI) / 180)}`);
  lines.push(`cfl=${fmt(s.cfl)}`);
  lines.push(`steps=${s.steps}`);
  lines.push(`order=${s.order}`);
  lines.push(`snapshots=${s.snapshots}`);
  lines.push(`gamma=${fmt(s.gamma)}`);
  lines.push(`rho0=${fmt(s.rho0)}`);
  lines.push(`p0=${fmt(s.p0)}`);
  lines.push(`bcleft=${s.bcLeft}`);
  lines.push(`bcright=${s.bcRight}`);
  lines.push(`bctop=${s.bcTop}`);
  lines.push(`bcbottom=${s.bcBottom}`);
  lines.push(`wallclosure=${s.wallClosure}`);
  lines.push(`kappa=${fmt(s.kappa)}`);
  lines.push(`interpolation=${s.interpolation ? 1 : 0}`);
  lines.push(`sensitivity=${s.sensitivity ? 1 : 0}`);
  lines.push("threads=0");
  return `${lines.join("\n")}\n`;
}

export function canonical(value: unknown): string {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  const entries = Object.entries(value as Record<string, unknown>)
    .filter(([, v]) => v !== undefined)
    .sort(([a], [b]) => a.localeCompare(b));
  return `{${entries.map(([k, v]) => `${JSON.stringify(k)}:${canonical(v)}`).join(",")}}`;
}

export function fingerprint(value: unknown): string {
  return createHash("sha256").update(canonical(value)).digest("hex");
}

export const LIMITS = {
  maxTarget: 20000,
  maxSteps: 1500,
  maxWork: 24_000_000, // cell-steps per request
  maxPayloadCells: 20000,
} as const;

export function assertWorkBudget(meshParams: MeshParams, solverParams: SolverParams): void {
  const work = meshParams.target * solverParams.steps;
  if (work > LIMITS.maxWork) {
    throw new ApiError(
      400,
      `request too large: ${meshParams.target} cells x ${solverParams.steps} steps exceeds the ${LIMITS.maxWork} cell-step budget`,
    );
  }
}
