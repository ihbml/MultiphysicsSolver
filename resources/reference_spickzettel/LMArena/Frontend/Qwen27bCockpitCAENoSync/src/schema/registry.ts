// ─────────────────────────────────────────────────────────────────────────────
// ModuleRegistry UI-schema mirror — what `schema.get`
// (ModuleRegistry::export_ui_schema()) returns over Channel B.
// The nTop-style block stack is rendered declaratively from these definitions,
// with `visible_if` conditional parameter evaluation.
// ─────────────────────────────────────────────────────────────────────────────
import type { Dim } from "../units/parser";
import type { CmapName } from "../fields/colormaps";
import { FIELD_DEFS } from "../fields/analytics";

export const ACCENT = {
  blue: "#004AFD",
  lime: "#B9F500",
  amber: "#FEC40B",
  pink: "#FC75B4",
  violet: "#723BFA",
  red: "#EF233C",
} as const;

export interface ParamDef {
  key: string;
  label: string;
  type: "expr" | "enum" | "toggle" | "slider" | "int";
  dim?: Dim; // expected SI base dimension for expr inputs
  unitHint?: string;
  options?: { v: string; label: string }[];
  visible_if?: string;
  min?: number;
  max?: number;
  step?: number;
  locked?: boolean;
  note?: string;
}

export interface ActionDef {
  id: string;
  label: string;
  method: string;
  tone: "primary" | "normal" | "warn" | "danger";
}

export interface BlockDef {
  id: string;
  title: string;
  cat: string;
  stripe: string;
  desc?: string;
  params: ParamDef[];
  actions?: ActionDef[];
}

export const DIMLESS: Dim = [0, 0, 0, 0, 0, 0, 0];
export const DIM_M: Dim = [0, 1, 0, 0, 0, 0, 0];
export const DIM_S: Dim = [0, 0, 1, 0, 0, 0, 0];
export const DIM_K: Dim = [0, 0, 0, 1, 0, 0, 0];
export const DIM_Pa: Dim = [1, -1, -2, 0, 0, 0, 0];
export const DIM_m_s: Dim = [0, 1, -1, 0, 0, 0, 0];
export const DIM_1_s: Dim = [0, 0, -1, 0, 0, 0, 0];
export const DIM_m2_s2: Dim = [0, 2, -2, 0, 0, 0, 0];
export const DIM_rho: Dim = [1, -3, 0, 0, 0, 0, 0];
export const DIM_mu: Dim = [1, 0, -1, 0, 0, 0, 0];

export const MATERIALS = [
  { id: "air_15", name: "Air 15 °C", rho: 1.225, mu: 1.8e-5, cp: 1006, cond: 0.0246 },
  { id: "air_20", name: "Air 20 °C", rho: 1.204, mu: 1.82e-5, cp: 1006, cond: 0.0251 },
  { id: "water_20", name: "Water 20 °C", rho: 998.2, mu: 1.002e-3, cp: 4182, cond: 0.598 },
  { id: "co2_25", name: "CO₂ 25 °C", rho: 1.836, mu: 1.64e-5, cp: 846, cond: 0.015 },
];

export const WORKSPACES = [
  { id: 1, name: "CAD & Domain", accent: ACCENT.blue, glyph: "M3 12h18M12 3v18" },
  { id: 2, name: "Meshing & Quality", accent: ACCENT.lime, glyph: "M4 4h7v7H4zM13 13h7v7h-7z" },
  { id: 3, name: "Physics & BC Stack", accent: ACCENT.amber, glyph: "M4 18c4 0 4-10 8-10s4 10 8 10" },
  { id: 4, name: "Solve & Live Monitors", accent: ACCENT.violet, glyph: "M4 19l5-6 4 3 7-9" },
  { id: 5, name: "Post-Processing & Compare", accent: ACCENT.pink, glyph: "M4 6h16M4 12h10M4 18h13" },
] as const;

export const PATCH_NAMES = [
  "inlet", "outlet", "topWall", "bottomWall", "stepFace", "cylinder", "symFront", "symBack",
];

export const BLOCKS: Record<number, BlockDef[]> = {
  1: [
    {
      id: "dom",
      title: "Domain — Step Channel + Cylinder",
      cat: "GEOMETRY · CFDGEO01",
      stripe: ACCENT.blue,
      desc: "Tessellated CAD · f64 vertices · f32 normals · u32 face_group_id",
      params: [
        { key: "length", label: "L  streamwise", type: "expr", dim: DIM_M },
        { key: "height", label: "H  top (far)", type: "expr", dim: DIM_M },
        { key: "half_width", label: "2·W  span", type: "expr", dim: DIM_M },
        { key: "step_x", label: "x_step", type: "expr", dim: DIM_M },
        { key: "top_low", label: "h₁  top (near)", type: "expr", dim: DIM_M },
        { key: "cyl_x", label: "x_cyl", type: "expr", dim: DIM_M },
        { key: "cyl_y", label: "y_cyl", type: "expr", dim: DIM_M },
        { key: "cyl_r", label: "r_cyl", type: "expr", dim: DIM_M },
        { key: "leave_gap", label: "Leave CAD gap (open-edge artifact)", type: "toggle" },
      ],
      actions: [
        { id: "import", label: "geometry.import", method: "geometry.import", tone: "primary" },
        { id: "rebind", label: "geometry.patches.update", method: "geometry.patches.update", tone: "normal" },
      ],
    },
    {
      id: "patches",
      title: "Boundary Patches",
      cat: "CFDGEO01 · face_group_id",
      stripe: ACCENT.blue,
      desc: "Click a surface in the viewport → one-click patch binding. Red #EF233C = open edges, pink #FC75B4 = non-manifold.",
      params: [],
    },
    {
      id: "mat",
      title: "Material — materials.list",
      cat: "PHYSICS · SI 7-dim",
      stripe: ACCENT.amber,
      desc: "Density ρ [kg·m⁻³], viscosity μ [kg·m⁻¹·s⁻¹] — unit parser enforces the dimension vector before RPC dispatch.",
      params: [
        {
          key: "mat_id", label: "fluid", type: "enum",
          options: MATERIALS.map((m) => ({ v: m.id, label: m.name })),
        },
        { key: "rho", label: "ρ  density", type: "expr", dim: DIM_rho },
        { key: "mu", label: "μ  viscosity", type: "expr", dim: DIM_mu },
      ],
    },
  ],
  2: [
    {
      id: "mesh",
      title: "Mesh — Structured Hex",
      cat: "MESHER · CFDMSH02",
      stripe: ACCENT.lime,
      desc: "Per-cell skewness · non_orthogonality · aspect_ratio · cell_volume · wsrd_weights · amips_energy · folded_face_indices",
      params: [
        { key: "cells_x", label: "cells / x", type: "int", min: 12, max: 96, step: 2 },
        { key: "cells_y", label: "cells / y", type: "int", min: 6, max: 24, step: 1 },
        { key: "cells_z", label: "cells / z", type: "int", min: 4, max: 24, step: 1 },
      ],
      actions: [{ id: "gen", label: "mesh.generate", method: "mesh.generate", tone: "primary" }],
    },
    {
      id: "qual",
      title: "MeshErrorMap · Threshold Isolator",
      cat: "QUALITY · location: cell",
      stripe: ACCENT.lime,
      desc: "4-quadrant map renders cell metrics STRICTLY flat — zero vertex interpolation, so defective / NaN cells are never smoothed away.",
      params: [
        {
          key: "metric", label: "primary metric", type: "enum",
          options: [
            { v: "skew", label: "skewness [0,1]" },
            { v: "nonorth", label: "non_orthogonality [°]" },
            { v: "aspect", label: "aspect_ratio [–]" },
            { v: "vol", label: "cell_volume [m³]" },
          ],
        },
        { key: "threshold", label: "threshold isolator (cells below → dimmed)", type: "slider", min: 0, max: 1, step: 0.005 },
        { key: "show_nan", label: "pulse NaN cells", type: "toggle" },
        { key: "flat", label: "flat faceting (locked — anti-smoothing invariant)", type: "toggle", locked: true },
      ],
    },
  ],
  3: [
    {
      id: "bc",
      title: "Patch BC Assignment",
      cat: "BOUNDARY · FoamField.location",
      stripe: ACCENT.pink,
      desc: "One-click from viewport pick. Unit parser enforces p [Pa abs] vs p_rgh [Pa gauge] and Δy₁ [m] vs estimated y⁺ [–].",
      params: [
        {
          key: "bc_type", label: "patchType", type: "enum",
          options: [
            { v: "velocityInlet", label: "velocityInlet" },
            { v: "pressureOutlet", label: "pressureOutlet" },
            { v: "wall", label: "wall (fixed)" },
            { v: "symmetry", label: "symmetry" },
            { v: "zeroGradient", label: "zeroGradient" },
          ],
        },
        { key: "vel_mag", label: "|U|_inlet", type: "expr", dim: DIM_m_s, visible_if: "bc_type === 'velocityInlet'" },
        { key: "T0", label: "T₀  temperature", type: "expr", dim: DIM_K, visible_if: "bc_type === 'velocityInlet' || bc_type === 'wall'" },
        { key: "k0", label: "k₀  turbulence KE", type: "expr", dim: DIM_m2_s2, visible_if: "bc_type === 'velocityInlet'" },
        { key: "om0", label: "ω₀  spec. dissipation", type: "expr", dim: DIM_1_s, visible_if: "bc_type === 'velocityInlet'" },
        { key: "gauge", label: "gauge p_rgh (vs absolute p)", type: "toggle", visible_if: "bc_type === 'pressureOutlet'" },
        { key: "p_ref", label: "p_ref  /  p_rgh", type: "expr", dim: DIM_Pa, visible_if: "bc_type === 'pressureOutlet'" },
        {
          key: "wall_treatment", label: "wall layer", type: "enum",
          options: [
            { v: "yplus", label: "estimated y⁺ target" },
            { v: "finelayer", label: "fixed Δy₁" },
          ],
          visible_if: "bc_type === 'wall'",
        },
        { key: "yplus_target", label: "y⁺ target (–)", type: "expr", dim: DIMLESS, visible_if: "bc_type === 'wall' && wall_treatment === 'yplus'" },
        { key: "dy1", label: "Δy₁  first layer", type: "expr", dim: DIM_M, visible_if: "bc_type === 'wall' && wall_treatment === 'finelayer'", note: "10⁻⁶ m resolves on 10 m domains — reverse-Z required" },
        { key: "roughness", label: "k_s  roughness", type: "expr", dim: DIM_M, visible_if: "bc_type === 'wall'" },
      ],
      actions: [
        { id: "apply", label: "apply → geometry.patches.update", method: "geometry.patches.update", tone: "primary" },
        { id: "validate", label: "case.validate", method: "case.validate", tone: "normal" },
      ],
    },
    {
      id: "turb",
      title: "Turbulence Model",
      cat: "PHYSICS · RAS",
      stripe: ACCENT.amber,
      params: [
        {
          key: "model", label: "model", type: "enum",
          options: [
            { v: "kOmegaSST", label: "k-ω SST (Menter)" },
            { v: "kOmega2016", label: "k-ω 2016" },
            { v: "SA", label: "Spalart-Allmaras" },
          ],
        },
      ],
    },
  ],
  4: [
    {
      id: "solver",
      title: "Solver — PISO / SMAC",
      cat: "SOLVE · solver.start",
      stripe: ACCENT.violet,
      desc: "Non-blocking NDJSON telemetry: `iter` + `timestep_written` streams. Cooperative job.cancel honored between iterations.",
      params: [
        { key: "max_iter", label: "max iterations", type: "expr", dim: DIMLESS },
        { key: "dt", label: "Δt  time step", type: "expr", dim: DIM_S },
        { key: "cour_target", label: "Courant target", type: "expr", dim: DIMLESS },
        { key: "agg_dt", label: "aggressive Δt ramp (NaN stress-test)", type: "toggle" },
        { key: "relax_u", label: "relaxation U", type: "expr", dim: DIMLESS },
        { key: "relax_p", label: "relaxation p", type: "expr", dim: DIMLESS },
        { key: "write_every", label: "write_every (iters)", type: "expr", dim: DIMLESS },
      ],
      actions: [
        { id: "generate", label: "case.generate", method: "case.generate", tone: "normal" },
        { id: "start", label: "solver.start", method: "solver.start", tone: "primary" },
        { id: "write", label: "solver.write_now", method: "solver.write_now", tone: "normal" },
        { id: "stop", label: "solver.stop_graceful", method: "solver.stop_graceful", tone: "normal" },
        { id: "abort", label: "solver.abort", method: "solver.abort", tone: "danger" },
        { id: "cancel", label: "job.cancel", method: "job.cancel", tone: "warn" },
      ],
    },
  ],
  5: [
    {
      id: "res",
      title: "Results — FoamField / CFDFLD03",
      cat: "POST · results.load",
      stripe: ACCENT.pink,
      desc: "3D fields with explicit 7-element SI dimensions · location: Cell | Point | BoundaryPatch · source: BoundaryField | OwnerCellFallback.",
      params: [
        {
          key: "field_id", label: "field", type: "enum",
          options: FIELD_DEFS.map((f) => ({ v: f.id, label: f.label })),
        },
        {
          key: "cmap", label: "colormap (Oklab LUT)", type: "enum",
          options: [
            { v: "viridis", label: "Viridis" },
            { v: "moreland", label: "Moreland C-W" },
            { v: "fire", label: "Fire" },
            { v: "darkEmber", label: "Dark Ember" },
            { v: "traffic", label: "Traffic Light" },
          ],
        },
        { key: "banding", label: "Ansys-style contour banding", type: "toggle" },
        { key: "bands", label: "band count", type: "int", min: 4, max: 64, step: 2, visible_if: "banding" },
      ],
      actions: [
        { id: "load", label: "results.load", method: "results.load", tone: "primary" },
        { id: "png", label: "export viewport PNG", method: "results.export", tone: "normal" },
      ],
    },
    {
      id: "cmp",
      title: "Compare Δ(tB − tA)",
      cat: "POST · Δfield",
      stripe: ACCENT.amber,
      desc: "Pointwise Δ over the two loaded times — min / max / RMS reported per field in the dock.",
      params: [],
    },
  ],
};

// ── visible_if compiler (safe: string literals preserved, identifiers → scope)
const compiled = new Map<string, (s: Record<string, unknown>) => boolean>();
export function evalVisible(expr: string | undefined, params: Record<string, unknown>): boolean {
  if (!expr) return true;
  let f = compiled.get(expr);
  if (!f) {
    let out = "";
    let i = 0;
    while (i < expr.length) {
      const c = expr[i];
      if (c === "'" || c === '"') {
        const j = expr.indexOf(c, i + 1);
        out += expr.slice(i, j + 1);
        i = j + 1;
      } else if (/[A-Za-z_$]/.test(c)) {
        let j = i;
        while (j < expr.length && /[\w$]/.test(expr[j])) j++;
        const w = expr.slice(i, j);
        out += w === "true" || w === "false" ? w : "s." + w;
        i = j;
      } else {
        out += c;
        i++;
      }
    }
    f = new Function("s", "return !!(" + out + ")") as (s: Record<string, unknown>) => boolean;
    compiled.set(expr, f);
  }
  try {
    return f(params);
  } catch {
    return true;
  }
}

export type { CmapName };
