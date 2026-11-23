import { create } from "zustand";

export type WS = 1 | 2 | 3 | 4 | 5;
export type Metric = "skew" | "nonorth" | "aspect" | "vol";

export interface PatchMeta {
  id: number;
  name: string;
  kind: string;
  area: number;
  normal: [number, number, number];
  nTris: number;
}

export interface MeshStats {
  cells: number;
  verts: number;
  maxSkew: number;
  meanNonOrth: number;
  maxAspect: number;
  folded: number;
  nan: number;
  hist: number[];
  nanCells: number[];
  foldedCells: number[];
}

export interface BCBinding {
  patch: number;
  type: string;
  label: string;
  rev: number;
}

export interface TimeEntry {
  id: number;
  t: number;
  tag: string;
}

export interface ConsoleLine {
  t: number;
  dir: "↑" | "↓" | "·";
  line: string;
}

export interface ValidationResult {
  valid: boolean;
  warnings: string[];
}

export type SolverState = "idle" | "validating" | "generating" | "running" | "stopped" | "aborted";

interface ViewCfg {
  mode: "cad" | "field";
  shade: "edl" | "cavity" | "flat";
  wire: boolean;
  sliceOn: boolean;
  sliceAxis: "x" | "y" | "z";
  sliceT: number;
  streamlines: boolean;
  lic: boolean;
}

export type ParamValue = string | boolean | number;
const DEFAULT_PARAMS: Record<string, ParamValue> = {
  length: "10 m",
  height: "2 m",
  half_width: "1 m",
  step_x: "4 m",
  top_low: "1.4 m",
  cyl_x: "6 m",
  cyl_y: "0.5 m",
  cyl_r: "0.5 m",
  leave_gap: true,
  mat_id: "air_20",
  rho: "1.204 kg/m³",
  mu: "1.82e-5 kg/(m·s)",
  cells_x: 48,
  cells_y: 10,
  cells_z: 10,
  metric: "skew",
  threshold: 0.85,
  show_nan: true,
  flat: true,
  bc_type: "velocityInlet",
  vel_mag: "8 m/s",
  T0: "20 C",
  k0: "0.09 m2/s2",
  om0: "30 rad/s",
  gauge: true,
  p_ref: "101325 Pa",
  wall_treatment: "yplus",
  yplus_target: "30",
  dy1: "5e-5 m",
  roughness: "5e-4 m",
  model: "kOmegaSST",
  max_iter: "1200",
  dt: "0.002 s",
  cour_target: "0.8",
  agg_dt: false,
  relax_u: "0.7",
  relax_p: "0.3",
  write_every: "50",
  field_id: "velocity",
  cmap: "viridis",
  banding: false,
  bands: 24,
};

interface Cockpit {
  ws: WS;
  setWs: (w: WS) => void;

  params: Record<string, ParamValue>;
  setParam: (k: string, v: ParamValue) => void;

  view: ViewCfg;
  setView: (p: Partial<ViewCfg>) => void;

  metric: Metric;
  setMetric: (m: Metric) => void;

  selPatch: number | null;
  setSelPatch: (p: number | null) => void;
  hoverPatch: number | null;
  setHoverPatch: (p: number | null) => void;

  patches: PatchMeta[];
  setPatches: (p: PatchMeta[]) => void;
  meshStats: MeshStats | null;
  setMeshStats: (m: MeshStats | null) => void;

  revisions: { geo: number; mesh: number };
  geoVersion: number;
  bumpGeo: () => void;
  setRevisions: (r: { geo: number; mesh: number }) => void;

  stalePatches: number[];
  setStalePatches: (p: number[]) => void;
  bcs: BCBinding[];
  setBcs: (b: BCBinding[]) => void;

  validate: ValidationResult | null;
  setValidate: (v: ValidationResult | null) => void;

  // ── solver / telemetry
  solver: SolverState;
  setSolver: (s: SolverState) => void;
  iter: number;
  setIter: (i: number) => void;
  residuals: Record<string, number[]>;
  courant: { mean: number[]; max: number[] };
  forces: { Cd: number[]; Cl: number[]; Tmax: number[] };
  nNaN: number;
  setNan: (n: number) => void;
  timesteps: TimeEntry[];
  setTimesteps: (t: TimeEntry[]) => void;
  activeT: number;
  setActiveT: (t: number) => void;
  tA: number;
  tB: number;
  setCompare: (a: number, b: number) => void;
  chartTick: number;
  incChart: () => void;

  // ── IPC console
  console: ConsoleLine[];
  log: (dir: "↑" | "↓" | "·", line: string) => void;
  consoleOpen: boolean;
  setConsoleOpen: (o: boolean) => void;
  backendReady: boolean;
  setBackendReady: (b: boolean) => void;
  bootLine: string;
  setBootLine: (l: string) => void;

  alert: boolean;
  setAlert: (a: boolean) => void;
}

const RES_KEYS = ["Ux", "Uy", "Uz", "p", "h", "k", "omega"];
const emptyArrays = () =>
  RES_KEYS.reduce((acc, k) => {
    acc[k] = [] as number[];
    return acc;
  }, {} as Record<string, number[]>);

export const useStore = create<Cockpit>((set) => ({
  ws: 1,
  setWs: (w) => set({ ws: w }),

  params: { ...DEFAULT_PARAMS },
  setParam: (k, v) => set((s) => ({ params: { ...s.params, [k]: v } })),

  view: {
    mode: "cad",
    shade: "edl",
    wire: false,
    sliceOn: true,
    sliceAxis: "z",
    sliceT: 0.5,
    streamlines: true,
    lic: true,
  },
  setView: (p) => set((s) => ({ view: { ...s.view, ...p } })),

  metric: "skew",
  setMetric: (m) => set({ metric: m }),

  selPatch: null,
  setSelPatch: (p) => set({ selPatch: p }),
  hoverPatch: null,
  setHoverPatch: (p) => set({ hoverPatch: p }),

  patches: [],
  setPatches: (p) => set({ patches: p }),
  meshStats: null,
  setMeshStats: (m) => set({ meshStats: m }),

  revisions: { geo: 0, mesh: 0 },
  geoVersion: 0,
  bumpGeo: () => set((s) => ({ geoVersion: s.geoVersion + 1 })),
  setRevisions: (r) => set({ revisions: r }),

  stalePatches: [],
  setStalePatches: (p) => set({ stalePatches: p }),
  bcs: [],
  setBcs: (b) => set({ bcs: b }),

  validate: null,
  setValidate: (v) => set({ validate: v }),

  solver: "idle",
  setSolver: (s) => set({ solver: s }),
  iter: 0,
  setIter: (i) => set({ iter: i }),
  residuals: emptyArrays(),
  courant: { mean: [], max: [] },
  forces: { Cd: [], Cl: [], Tmax: [] },
  nNaN: 0,
  setNan: (n) => set({ nNaN: n }),
  timesteps: [],
  setTimesteps: (t) => set({ timesteps: t }),
  activeT: 0,
  setActiveT: (t) => set({ activeT: t }),
  tA: 0,
  tB: 0,
  setCompare: (a, b) => set({ tA: a, tB: b }),
  chartTick: 0,
  incChart: () => set((s) => ({ chartTick: s.chartTick + 1 })),

  console: [],
  log: (dir, line) =>
    set((s) => ({ console: [...s.console.slice(-219), { t: Date.now(), dir, line }] })),
  consoleOpen: false,
  setConsoleOpen: (o) => set({ consoleOpen: o }),
  backendReady: false,
  setBackendReady: (b) => set({ backendReady: b }),
  bootLine: "spawning backend…",
  setBootLine: (l) => set({ bootLine: l }),

  alert: false,
  setAlert: (a) => set({ alert: a }),
}));

export const RES_COLORS: Record<string, string> = {
  Ux: "#004AFD",
  Uy: "#B9F500",
  Uz: "#723BFA",
  p: "#FEC40B",
  h: "#FC75B4",
  k: "#EF233C",
  omega: "#4FD1C5",
};
