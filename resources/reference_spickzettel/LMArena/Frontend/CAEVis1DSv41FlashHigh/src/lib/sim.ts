import { V3, norm } from "./math3d";

export type FieldKey =
  | "Cp"
  | "p"
  | "U"
  | "T"
  | "yPlus"
  | "tau_w"
  | "Nu"
  | "Mach"
  | "wallT"
  | "delta";

export type FieldDef = {
  key: FieldKey;
  label: string;
  sym: string;
  unit: string;
  min: number;
  max: number;
  colormap: "turbo" | "coolwarm" | "inferno";
  description: string;
  digits: number;
};

export const FIELDS: Record<FieldKey, FieldDef> = {
  Cp: { key: "Cp", label: "Pressure coefficient", sym: "Cp", unit: "—", min: -2.2, max: 1.2, colormap: "turbo", description: "Dimensionless static pressure", digits: 3 },
  p: { key: "p", label: "Static pressure", sym: "p", unit: "Pa", min: 84500, max: 102800, colormap: "turbo", description: "Absolute static pressure", digits: 0 },
  U: { key: "U", label: "Velocity magnitude", sym: "|U|", unit: "m·s⁻¹", min: 60, max: 245, colormap: "turbo", description: "Velocity magnitude", digits: 1 },
  T: { key: "T", label: "Static temperature", sym: "T", unit: "K", min: 289, max: 354, colormap: "inferno", description: "Static temperature", digits: 1 },
  yPlus: { key: "yPlus", label: "Wall y+", sym: "y⁺", unit: "—", min: 0.2, max: 38, colormap: "inferno", description: "First-cell non-dimensional wall distance", digits: 2 },
  tau_w: { key: "tau_w", label: "Wall shear stress", sym: "τw", unit: "Pa", min: 0.4, max: 46, colormap: "turbo", description: "Wall shear stress magnitude", digits: 2 },
  Nu: { key: "Nu", label: "Nusselt number", sym: "Nu", unit: "—", min: 40, max: 2100, colormap: "coolwarm", description: "Convective heat transfer coefficient", digits: 0 },
  Mach: { key: "Mach", label: "Mach number", sym: "Ma", unit: "—", min: 0.08, max: 0.74, colormap: "turbo", description: "Local Mach number", digits: 3 },
  wallT: { key: "wallT", label: "Wall temperature", sym: "Tw", unit: "K", min: 690, max: 1160, colormap: "inferno", description: "Adiabatic wall temperature", digits: 0 },
  delta: { key: "delta", label: "Differential Δφ (B − A)", sym: "Δφ", unit: "auto", min: -1, max: 1, colormap: "coolwarm", description: "Run B minus Run A on GPU", digits: 3 },
};

export const FIELD_ORDER: FieldKey[] = ["Cp", "p", "U", "T", "Mach", "yPlus", "tau_w", "Nu", "wallT"];

export type Patch = {
  id: string;
  group: number;
  name: string;
  short: string;
  tag: string;
  kind: "wall" | "inlet" | "outlet" | "interface" | "periodic";
  area: number;
  faces: number;
  normal: V3;
  centroid: V3;
  bboxMin: V3;
  bboxMax: V3;
  cyl?: { center: [number, number]; range: [number, number] };
  rule: string;
  description: string;
  pickable: boolean;
};

const P = (v: Partial<Patch> & Pick<Patch, "id" | "group" | "name" | "short" | "tag" | "kind" | "rule" | "description">): Patch => ({
  area: 0.1284,
  faces: 2048,
  normal: [0, 1, 0],
  centroid: [0, 0, 0],
  bboxMin: [-0.85, -0.72, -0.16],
  bboxMax: [0.85, 0.72, 0.16],
  pickable: true,
  ...v,
});

export const PATCHES: Patch[] = [
  P({
    id: "LE", group: 1, name: "Blade · Leading Edge", short: "LE_01", tag: "SURF_LEADING_EDGE", kind: "wall",
    area: 0.0412, faces: 384, normal: [0.086, 0.02, -0.996], centroid: [-0.842, 0.04, 0.021],
    bboxMin: [-0.86, -0.72, -0.048], bboxMax: [-0.79, 0.72, 0.09],
    rule: "cyl_radius_yz(0.0, 0.0) in [0.018, 0.022] and normal.x < -0.32",
    description: "Blunt elliptical LE, 2.4 mm radius, rotor-relative inflow 31.2°",
  }),
  P({
    id: "SS", group: 2, name: "Blade · Suction Side", short: "SS_01", tag: "SURF_SUCTION", kind: "wall",
    area: 0.3268, faces: 3072, normal: [0.412, 0.034, 0.910], centroid: [0.31, 0.36, 0.214],
    bboxMin: [-0.64, -0.72, 0.006], bboxMax: [0.812, 0.72, 0.292],
    rule: "normal.z > 0.905 and abs(y - 0.360) < 1e-3",
    description: "Aerodynamic suction surface · peak suction at x/c 0.41",
  }),
  P({
    id: "PS", group: 3, name: "Blade · Pressure Side", short: "PS_01", tag: "SURF_PRESSURE", kind: "wall",
    area: 0.3184, faces: 3072, normal: [0.373, -0.041, -0.927], centroid: [0.28, 0.26, -0.208],
    bboxMin: [-0.64, -0.72, -0.288], bboxMax: [0.81, 0.72, -0.004],
    rule: "normal.z < -0.995 and abs(x - bbox.x_min) < 1e-4",
    description: "Concave pressure surface · secondary-flow corner separation at TE",
  }),
  P({
    id: "TE", group: 4, name: "Blade · Trailing Edge", short: "TE_01", tag: "SURF_TRAILING_EDGE", kind: "wall",
    area: 0.0264, faces: 256, normal: [-0.997, 0.008, 0.062], centroid: [0.826, 0.02, 0.031],
    bboxMin: [0.802, -0.72, -0.061], bboxMax: [0.842, 0.72, 0.104],
    rule: "normal.x > 0.992 and bbox.x_max > 0.82",
    description: "Cut-back TE, 0.4 mm land · wake refinement target",
  }),
  P({
    id: "TIP", group: 5, name: "Blade · Tip / Shroud Gap", short: "TIP_01", tag: "SURF_TIP", kind: "wall",
    area: 0.0142, faces: 192, normal: [0.02, 0.997, 0.014], centroid: [0.06, 0.724, 0.011],
    bboxMin: [-0.59, 0.712, -0.14], bboxMax: [0.71, 0.736, 0.17],
    rule: "abs(y - 0.724) < 1e-4 and normal.y > 0.95",
    description: "Tip cap · 0.35 mm running clearance, leakage vortex origin",
  }),
  P({
    id: "HUB", group: 6, name: "Hub · Rotor Platform", short: "HUB_01", tag: "SURF_HUB", kind: "wall",
    area: 0.4618, faces: 4096, normal: [0, -0.998, 0.061], centroid: [0.12, -0.74, 0.02],
    bboxMin: [-0.88, -0.76, -0.31], bboxMax: [0.9, -0.71, 0.33],
    rule: "abs(y + 0.74) < 1e-4 and normal.y < -0.95", description: "Adiabatic rotating hub with fillet radius 2.0 mm",
  }),
  P({
    id: "SHROUD", group: 7, name: "Shroud · Stationary", short: "SHR_01", tag: "SURF_SHROUD", kind: "wall",
    area: 0.6942, faces: 5200, normal: [0, 1, 0], centroid: [0.12, 0.86, 0.02],
    bboxMin: [-0.9, 0.82, -0.32], bboxMax: [0.92, 0.9, 0.34],
    rule: "abs(y - 0.86) < 1e-4 and normal.y > 0.98", description: "Counter-rotating shroud wall, no-slip adiabatic",
  }),
  P({
    id: "INLET", group: 8, name: "Domain · Inlet", short: "INLET", tag: "BC_INLET", kind: "inlet",
    area: 5.184, faces: 256, normal: [1, 0, 0], centroid: [-4.12, 0.02, 0], bboxMin: [-4.14, -2.1, -1.9], bboxMax: [-4.1, 2.1, 1.9],
    rule: "x < -4.1 + 1e-5", description: "Total pressure inlet · 101 325 Pa, Tu 2.1 %, 892 K",
  }),
  P({
    id: "OUTLET", group: 9, name: "Domain · Outlet", short: "OUTLET", tag: "BC_OUTLET", kind: "outlet",
    area: 4.72, faces: 256, normal: [-1, 0, 0], centroid: [6.1, 0.02, 0], bboxMin: [6.08, -2.1, -1.9], bboxMax: [6.12, 2.1, 1.9],
    rule: "x > 6.1 - 1e-5", description: "Static pressure outlet · 98 640 Pa, backflow total T 861 K",
  }),
  P({
    id: "MRF", group: 10, name: "MRF · Rotor Zone", short: "MRF_01", tag: "ZONE_MRF", kind: "interface",
    area: 2.214, faces: 1024, normal: [1, 0, 0], centroid: [0, 0, 0], bboxMin: [-1.5, -0.86, -1.3], bboxMax: [1.5, 0.86, 1.3],
    rule: "cyl_radius_yz(0, 0) < 1.3", description: "Rotating frame · 12 400 rpm, 48 blades, tip Mach 0.617",
  }),
];

export const patchById = (id: string) => PATCHES.find((p) => p.id === id) || PATCHES[0];
export const patchByGroup = (group: number) => PATCHES.find((p) => p.group === group) || null;

/* ------------------------------------------------------------------ */
/* Procedural blade geometry (chord in x, span in y, thickness in z)  */
/* ------------------------------------------------------------------ */
export type MeshData = {
  positions: Float32Array;
  normals: Float32Array;
  groups: Float32Array;
  base: Float32Array; // t, side
  indices: Uint32Array;
  triCount: number;
};

const airfoilPoint = (t: number, side: number, span: number) => {
  const chord = 1.72;
  const halfSpan = 0.72;
  const camber = 0.16 * Math.sin(Math.PI * Math.pow(t, 1.15)) - 0.055 * t;
  const thickness =
    0.082 * Math.sin(Math.PI * Math.pow(t, 0.72)) * (1 - 0.18 * t) + 0.0045;
  let x = (t - 0.44) * chord;
  let z = camber + side * thickness;
  const twist = 0.26 + 0.42 * (span / halfSpan);
  const sx = x * Math.cos(twist) - z * Math.sin(twist);
  const sz = x * Math.sin(twist) + z * Math.cos(twist);
  x = sx;
  z = sz;
  const y = span * halfSpan;
  const group = span > 0.985 ? 5 : t < 0.045 ? 1 : t > 0.962 ? 4 : side > 0 ? 2 : 3;
  return { pos: [x, y, z] as V3, group, t, side };
};

export function buildBladeMesh(chordSeg = 68, spanSeg = 20): MeshData {
  const positions: number[] = [];
  const normals: number[] = [];
  const groups: number[] = [];
  const base: number[] = [];
  const indices: number[] = [];
  const grid: { pos: V3; group: number; idx: number }[][][] = [];

  for (let side = 1; side >= -1; side -= 2) {
    const rows: { pos: V3; group: number; idx: number }[][] = [];
    for (let j = 0; j <= spanSeg; j++) {
      const span = -1 + (2 * j) / spanSeg;
      const row: { pos: V3; group: number; idx: number }[] = [];
      for (let i = 0; i <= chordSeg; i++) {
        const t = i / chordSeg;
        const pt = airfoilPoint(t, side, span);
        const idx = positions.length / 3;
        positions.push(...pt.pos);
        normals.push(0, 0, 0);
        groups.push(pt.group);
        base.push(t, side);
        row.push({ pos: pt.pos, group: pt.group, idx });
      }
      rows.push(row);
    }
    grid.push(rows);
  }

  // triangulate both surfaces
  for (let s = 0; s < 2; s++) {
    const rows = grid[s];
    for (let j = 0; j < spanSeg; j++) {
      for (let i = 0; i < chordSeg; i++) {
        const a = rows[j][i].idx;
        const b = rows[j][i + 1].idx;
        const c = rows[j + 1][i].idx;
        const d = rows[j + 1][i + 1].idx;
        indices.push(a, b, c, b, d, c);
      }
    }
  }

  // close tip cap (top row of both surfaces)
  const upper = grid[0][spanSeg];
  const lower = grid[1][spanSeg];
  for (let i = 0; i < chordSeg; i++) {
    const a = upper[i].idx, b = upper[i + 1].idx, c = lower[i].idx, d = lower[i + 1].idx;
    indices.push(a, c, b, b, c, d);
  }

  // accumulate smooth normals
  const pos = new Float32Array(positions);
  const nrm = new Float32Array(normals);
  const idx = new Uint32Array(indices);
  for (let i = 0; i < idx.length; i += 3) {
    const ia = idx[i] * 3, ib = idx[i + 1] * 3, ic = idx[i + 2] * 3;
    const ax = pos[ib] - pos[ia], ay = pos[ib + 1] - pos[ia + 1], az = pos[ib + 2] - pos[ia + 2];
    const bx = pos[ic] - pos[ia], by = pos[ic + 1] - pos[ia + 1], bz = pos[ic + 2] - pos[ia + 2];
    const nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    for (const v of [ia, ib, ic]) {
      nrm[v] += nx; nrm[v + 1] += ny; nrm[v + 2] += nz;
    }
  }
  for (let i = 0; i < nrm.length; i += 3) {
    const l = Math.hypot(nrm[i], nrm[i + 1], nrm[i + 2]) || 1;
    nrm[i] /= l; nrm[i + 1] /= l; nrm[i + 2] /= l;
  }

  return {
    positions: pos,
    normals: nrm,
    groups: new Float32Array(groups),
    base: new Float32Array(base),
    indices: idx,
    triCount: idx.length / 3,
  };
}

/* ------------------------------------------------------------------ */
/* Pseudo-solver field sampling                                       */
/* ------------------------------------------------------------------ */
const smooth = (t: number) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));

export function simFieldAt(pos: V3, field: FieldKey, run: "A" | "B" = "A"): number {
  const [x, y, z] = pos;
  const t = Math.min(1, Math.max(0, (x + 0.84) / 1.68));
  const adj = run === "B" ? 1 : 0;
  const wobble = 0.07 * Math.sin(2.4 * y + 1.7 * z) * Math.sin(3.1 * t + 0.6);
  switch (field) {
    case "Cp":
      return (t < 0.5 ? 0.55 - 2.45 * Math.sin(Math.PI * Math.pow(t, 0.61)) : -1.05 + 1.9 * Math.pow(t - 0.5, 1.4)) * (run === "B" ? 0.94 : 1) + wobble;
    case "p": {
      const cp = simFieldAt(pos, "Cp", run);
      return 101325 + cp * 0.5 * 1.085 * 190 * 190;
    }
    case "U":
      return 96 + 140 * Math.sin(Math.PI * Math.pow(t, 0.7)) * (run === "B" ? 1.021 : 1) + 10 * Math.sin(3 * y) + 5 * wobble;
    case "T":
      return 292 + 44 * Math.pow(1 - t, 0.8) + 9 * Math.sin(2.1 * y) - adj * 2.4 + 3 * wobble;
    case "yPlus":
      return 1.1 + 22 * Math.pow(t, 1.9) + 6 * Math.exp(-t * 18) + (run === "B" ? -1.3 : 0) + 1.2 * wobble;
    case "tau_w":
      return 3.2 + 31 * Math.pow(t, 1.35) + 5 * Math.exp(-t * 12) - adj * 1.8 + wobble;
    case "Nu":
      return 165 + 980 * Math.pow(t, 1.6) + 120 * Math.exp(-t * 15) - adj * 34 + wobble * 60;
    case "Mach": {
      const u = simFieldAt(pos, "U", run);
      const temp = simFieldAt(pos, "T", run);
      return u / Math.sqrt(1.4 * 287.05 * temp);
    }
    case "wallT":
      return 780 + 258 * Math.pow(t, 0.8) + 24 * Math.sin(2.2 * y) - adj * 22 + wobble * 30;
    case "delta":
      return simFieldAt(pos, "wallT", "B") - simFieldAt(pos, "wallT", "A");
    default:
      return 0;
  }
}

export const normalizeField = (field: FieldKey, value: number) => {
  const f = FIELDS[field];
  return Math.min(1, Math.max(0, (value - f.min) / (f.max - f.min)));
};

/* ------------------------------------------------------------------ */
/* Smart spatial rule synthesis                                       */
/* ------------------------------------------------------------------ */
export type SynthRule = {
  expression: string;
  human: string;
  clauses: { label: string; value: string }[];
  confidence: number;
  matchedFaces: number;
  normalCone: number;
};

export function synthesizeRule(patch: Patch): SynthRule {
  const [nx, ny, nz] = patch.normal;
  const axis = Math.abs(nx) > Math.abs(ny) && Math.abs(nx) > Math.abs(nz) ? "x" : Math.abs(ny) > Math.abs(nz) ? "y" : "z";
  const primary = patch.normal[axis === "x" ? 0 : axis === "y" ? 1 : 2];
  const bounds = patch.bboxMax[axis === "x" ? 0 : axis === "y" ? 1 : 2] - patch.bboxMin[axis === "x" ? 0 : axis === "y" ? 1 : 2];
  const cylRange: [number, number] = patch.cyl?.range ?? [0.018, 0.022];
  const clauses: { label: string; value: string }[] = [];
  let expression = "";

  if (patch.id === "LE") {
    expression = `cyl_radius_yz(0, 0) in [${cylRange[0].toFixed(3)}, ${cylRange[1].toFixed(3)}] and normal.x < -0.32`;
    clauses.push({ label: "cyl_radius_yz", value: `[${cylRange[0].toFixed(3)}, ${cylRange[1].toFixed(3)}] m` });
    clauses.push({ label: "normal.x", value: "< −0.32" });
  } else if (patch.id === "TE") {
    expression = `normal.x > 0.992 and bbox.x_max > ${(patch.bboxMax[0] - 0.012).toFixed(3)}`;
    clauses.push({ label: "normal.x", value: "> 0.992" });
    clauses.push({ label: "bbox.x_max", value: `> ${(patch.bboxMax[0] - 0.012).toFixed(3)} m` });
  } else if (patch.id === "TIP" || patch.id === "HUB") {
    expression = `abs(${axis} - ${patch.centroid[1].toFixed(3)}) < 1e-4 and normal.${axis} ${primary < 0 ? "<" : ">"} ${(Math.abs(primary) * 0.95).toFixed(3)}`;
    clauses.push({ label: `abs(${axis} − c)`, value: `< 1e-4 m` });
    clauses.push({ label: `normal.${axis}`, value: `${primary < 0 ? "<" : ">"} ${(Math.abs(primary) * 0.95).toFixed(3)}` });
  } else if (patch.id === "SS" || patch.id === "PS") {
    const sign = primary < 0 ? "<" : ">";
    expression = `normal.${axis} ${sign} ${(primary * 0.995).toFixed(3)} and abs(y - ${patch.centroid[1].toFixed(3)}) < ${Math.max(1e-3, bounds * 0.001).toExponential(0)}`;
    clauses.push({ label: `normal.${axis}`, value: `${sign} ${(primary * 0.995).toFixed(3)}` });
    clauses.push({ label: "abs(y − c)", value: `< ${Math.max(1e-3, bounds * 0.001).toExponential(0)} m` });
  } else if (patch.kind === "inlet" || patch.kind === "outlet") {
    expression = `${patch.kind === "inlet" ? "x" : "x"} ${patch.kind === "inlet" ? "<" : ">"} ${patch.centroid[0].toFixed(4)} ${patch.kind === "inlet" ? "+" : "-"} 1e-5`;
    clauses.push({ label: patch.kind === "inlet" ? "x_min" : "x_max", value: `±1e-5 m` });
    clauses.push({ label: "normal.x", value: `${patch.normal[0] > 0 ? "+" : "−"}1.000` });
  } else {
    expression = `normal.z ${nz < 0 ? "<" : ">"} ${(nz * 0.995).toFixed(3)} and abs(x - ${patch.centroid[0].toFixed(3)}) < 1e-3`;
    clauses.push({ label: "normal.z", value: `${nz < 0 ? "<" : ">"} ${(nz * 0.995).toFixed(3)}` });
    clauses.push({ label: "abs(x − c)", value: "< 1e-3 m" });
  }

  const normalCone = Math.acos(Math.min(1, Math.max(-1, Math.abs(primary)))) * (180 / Math.PI);
  const matchedFaces = patch.faces;
  const confidence = Math.min(99.8, 95.4 + (patch.faces % 41) / 10);

  return {
    expression,
    human: `${patch.description}. Topology-independent match: ${clauses.length} spatial predicates, normal cone ±${normalCone.toFixed(1)}°.`,
    clauses,
    confidence,
    matchedFaces,
    normalCone,
  };
}

/* ------------------------------------------------------------------ */
/* Charts: chordwise profiles + wall-law data                          */
/* ------------------------------------------------------------------ */
export type Pt = { x: number; y: number; x2?: number; y2?: number };

export function chordProfile(field: "Cp" | "yPlus" | "wallT" | "Mach", run: "A" | "B", n = 81): Pt[] {
  const pts: Pt[] = [];
  for (let i = 0; i < n; i++) {
    const t = i / (n - 1);
    let v = 0;
    if (field === "Cp") {
      const peak = run === "B" ? 2.32 : 2.62;
      v = (1 - peak * Math.sin(Math.PI * Math.pow(t, 0.585)) + 0.92 * t) * (t > 0.97 ? 1 - (t - 0.97) * 14 : 1);
    } else if (field === "yPlus") {
      v = (1.0 + 24 * Math.pow(t, 2.1) + 7.5 * Math.exp(-t * 16)) * (run === "B" ? 0.945 : 1);
    } else if (field === "wallT") {
      v = 792 + 252 * Math.pow(t, 0.85) + 38 * Math.sin(Math.PI * t) - (run === "B" ? 21 : 0);
    } else {
      v = (0.16 + 0.5 * Math.sin(Math.PI * Math.pow(t, 0.62))) * (run === "B" ? 0.965 : 1);
      if (t < 0.06) v = 0.16 + (t / 0.06) * 0.28;
    }
    pts.push({ x: t, y: v });
  }
  return pts;
}

export function lawOfWall(): { visc: Pt[]; log: Pt[] } {
  const visc: Pt[] = [];
  const log: Pt[] = [];
  for (let i = 0; i <= 28; i++) {
    const yp = 0.4 * Math.pow(1.28, i);
    visc.push({ x: yp, y: yp });
    log.push({ x: yp, y: (1 / 0.41) * Math.log(Math.max(yp, 11) * 0.41) + 5.2 });
  }
  return { visc, log };
}

export function uPlusSamples(run: "A" | "B"): Pt[] {
  const ys = [0.55, 0.9, 1.6, 2.8, 4.6, 7.2, 10.2, 12.8, 16, 22, 31, 44, 63, 92, 140, 220, 360, 620, 1100, 2200, 4800];
  return ys.map((yp, i) => {
    const law = yp < 11.2 ? yp : (1 / 0.41) * Math.log(Math.max(yp, 11.2) * 0.41) + 5.2;
    const bump = run === "B" ? 0.34 * Math.sin(i * 0.9) : -0.42 * Math.sin(i * 0.7) + (i > 14 ? 0.18 * (i - 14) : 0);
    return { x: yp, y: Math.round((law + bump) * 100) / 100 };
  });
}

export function lineProfile(
  field: FieldKey,
  run: "A" | "B",
  n = 64,
  a: V3 = [-1.12, 0.44, 0.62],
  b: V3 = [1.05, 0.08, -0.36]
): Pt[] {
  const pts: Pt[] = [];
  const length = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  for (let i = 0; i < n; i++) {
    const t = n === 1 ? 0 : i / (n - 1);
    const pos: V3 = [a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])];
    pts.push({ x: t * length, y: simFieldAt(pos, field === "delta" ? "wallT" : field, run) });
  }
  return pts;
}

/* ------------------------------------------------------------------ */
/* A/B comparison data                                                */
/* ------------------------------------------------------------------ */
export type Kpi = {
  key: string;
  label: string;
  sym: string;
  a: number;
  b: number;
  unit: string;
  better: "lower" | "higher";
  digits: number;
  pct: boolean;
};

export const KPIS: Kpi[] = [
  { key: "cd", label: "Drag coefficient", sym: "Cd", a: 0.01842, b: 0.01714, unit: "—", better: "lower", digits: 5, pct: false },
  { key: "p0", label: "Total pressure loss", sym: "p₀ loss", a: 3.42, b: 2.91, unit: "%", better: "lower", digits: 2, pct: true },
  { key: "tmax", label: "Peak wall temperature", sym: "Tmax", a: 1128, b: 1074, unit: "K", better: "lower", digits: 0, pct: false },
  { key: "mdot", label: "Mass flow", sym: "ṁ", a: 7.842, b: 7.911, unit: "kg·s⁻¹", better: "higher", digits: 3, pct: false },
  { key: "eta", label: "Isentropic efficiency", sym: "η", a: 0.9021, b: 0.9246, unit: "—", better: "higher", digits: 4, pct: false },
  { key: "yp", label: "Max wall y+", sym: "y⁺max", a: 34.8, b: 28.2, unit: "—", better: "lower", digits: 1, pct: false },
  { key: "mach", label: "Peak relative Mach", sym: "Mamax", a: 0.682, b: 0.657, unit: "—", better: "lower", digits: 3, pct: false },
];

export const PARETO: { x: number; y: number; kind: "sweep" | "a" | "b"; label?: string }[] = [
  { x: 2.45, y: 179, kind: "sweep" }, { x: 2.58, y: 176, kind: "sweep" },
  { x: 2.68, y: 181, kind: "sweep" }, { x: 2.76, y: 173, kind: "sweep" },
  { x: 2.82, y: 178, kind: "sweep" }, { x: 2.96, y: 170, kind: "sweep" },
  { x: 3.05, y: 174, kind: "sweep" }, { x: 3.12, y: 168, kind: "sweep" },
  { x: 3.2, y: 172, kind: "sweep" }, { x: 3.3, y: 166, kind: "sweep" },
  { x: 3.42, y: 184, kind: "a" },
  { x: 2.91, y: 164, kind: "b" },
  { x: 3.55, y: 180, kind: "sweep" }, { x: 3.62, y: 189, kind: "sweep" },
  { x: 2.88, y: 186, kind: "sweep" }, { x: 3.24, y: 158, kind: "sweep" },
  { x: 3.45, y: 155, kind: "sweep" }, { x: 3.7, y: 160, kind: "sweep" },
];

export type RuleSeed = {
  id: string;
  name: string;
  kind: "inlet" | "outlet" | "wall" | "interface" | "periodic";
  patches: string[];
  active: boolean;
  expression: string;
  target: string;
  synthesized: boolean;
  color: string;
  faces: number;
};

export const RULES_SEED: RuleSeed[] = [
  {
    id: "R1",
    name: "Inlet · total pressure",
    kind: "inlet" as const,
    patches: ["INLET"],
    active: true,
    expression: "x < -4.1 + 1e-5",
    target: "p₀ = 101 325 Pa, Tu = 2.1 %, T₀ = 892 K",
    synthesized: false,
    color: "#41E0FF",
    faces: 256,
  },
  {
    id: "R2",
    name: "Outlet · static pressure",
    kind: "outlet" as const,
    patches: ["OUTLET"],
    active: true,
    expression: "x > 6.1 - 1e-5",
    target: "ps = 98 640 Pa, backflow T₀ 861 K",
    synthesized: false,
    color: "#A78BFA",
    faces: 256,
  },
  {
    id: "R3",
    name: "Blade · adiabatic no-slip",
    kind: "wall" as const,
    patches: ["SS", "PS", "LE", "TE", "TIP"],
    active: true,
    expression: "patch_group in {1,2,3,4,5}",
    target: "No-slip, q̇ = 0 W·m⁻², y+ target < 1",
    synthesized: false,
    color: "#7C8798",
    faces: 6976,
  },
  {
    id: "R4",
    name: "Hub · rotating wall",
    kind: "wall" as const,
    patches: ["HUB"],
    active: true,
    expression: "abs(y + 0.74) < 1e-4 and normal.y < -0.95",
    target: "Moving wall · ω = 12 400 rpm about +y",
    synthesized: true,
    color: "#FFB020",
    faces: 4096,
  },
  {
    id: "R5",
    name: "Rotor ↔ Stator interface",
    kind: "interface" as const,
    patches: ["MRF"],
    active: true,
    expression: "cyl_radius_yz(0, 0) in [1.299, 1.301]",
    target: "Mixing-plane, conservative flux, 48 sectors",
    synthesized: false,
    color: "#23D18B",
    faces: 1024,
  },
];

export const KPI_COLORS = { a: "#7C8798", b: "#4D7CFF" };
export const fmt = (v: number, digits = 3) =>
  Math.abs(v) >= 1000 ? v.toLocaleString("en-US", { maximumFractionDigits: digits }) : v.toFixed(digits);
export { smooth, norm };
