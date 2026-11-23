export type SchemaPredicate = {
  parameter: string;
  equals: string | number | boolean;
};

export type UiModule = {
  id: string;
  type: string;
  title: string;
  summary: string;
  accent: string;
  status: string;
  parameters: { id: string; dimensions: number[]; unit: string; visible_if?: SchemaPredicate }[];
};

export type ModuleUiSchema = { version: string; modules: UiModule[] };
export type StackBlock = Omit<UiModule, "parameters">;

// The browser preview uses the same exported shape that schema.get returns.
export const referenceUiSchema: ModuleUiSchema = {
  version: "3.8",
  modules: [
    { id: "geometry", type: "GEOMETRY / 01", title: "Import geometry", summary: "naca0018_revC.step", accent: "blue", status: "READY", parameters: [{ id: "source", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "path" }, { id: "scale", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "1" }] },
    { id: "domain", type: "DOMAIN / 02", title: "Enclosure", summary: "10.0 × 0.8 × 0.8 m", accent: "lime", status: "READY", parameters: [{ id: "length", dimensions: [0, 1, 0, 0, 0, 0, 0], unit: "m" }, { id: "height", dimensions: [0, 1, 0, 0, 0, 0, 0], unit: "m" }] },
    { id: "mesh", type: "MESH / 03", title: "Hex-dominant mesh", summary: "2.84 M cells · 5 regions", accent: "amber", status: "BUILT", parameters: [{ id: "base_cell", dimensions: [0, 1, 0, 0, 0, 0, 0], unit: "m" }, { id: "refinement", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "levels" }] },
    { id: "fluid", type: "MATERIAL / 04", title: "Air · incompressible", summary: "ρ 1.204 kg/m³", accent: "violet", status: "LINKED", parameters: [{ id: "density", dimensions: [1, -3, 0, 0, 0, 0, 0], unit: "kg/m³" }, { id: "nu", dimensions: [0, 2, -1, 0, 0, 0, 0], unit: "m²/s" }] },
    { id: "inlet", type: "BOUNDARY / 05", title: "Inlet velocity", summary: "12.5 m/s · uniform", accent: "pink", status: "ACTIVE", parameters: [{ id: "profile", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "mode" }, { id: "magnitude", dimensions: [0, 1, -1, 0, 0, 0, 0], unit: "m/s" }, { id: "turbulence", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "1", visible_if: { parameter: "profile", equals: "Fully developed" } }] },
    { id: "wall", type: "BOUNDARY / 06", title: "Wall function", summary: "k-ω SST · Δy₁ 0.08 mm", accent: "pink", status: "ACTIVE", parameters: [{ id: "first_layer", dimensions: [0, 1, 0, 0, 0, 0, 0], unit: "m" }, { id: "y_plus", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "1" }] },
    { id: "thermal", type: "PHYSICS / 07", title: "Conjugate heat transfer", summary: "energy · wall 850 C", accent: "pink", status: "LINKED", parameters: [{ id: "energy", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "bool" }, { id: "wall_temperature", dimensions: [0, 0, 0, 1, 0, 0, 0], unit: "K" }] },
    { id: "rotor", type: "MOTION / 08", title: "Rotating reference frame", summary: "1,500 rpm · 157.1 rad/s", accent: "amber", status: "LINKED", parameters: [{ id: "angular_velocity", dimensions: [0, 0, -1, 0, 0, 0, 0], unit: "rad/s" }] },
    { id: "solver", type: "SOLVER / 09", title: "Steady RANS", summary: "SIMPLE · 1st order", accent: "violet", status: "READY", parameters: [{ id: "model", dimensions: [0, 0, 0, 0, 0, 0, 0], unit: "enum" }] },
  ],
};

export function generateBlockStack(schema: ModuleUiSchema): StackBlock[] {
  return schema.modules.map((module) => ({
    id: module.id,
    type: module.type,
    title: module.title,
    summary: module.summary,
    accent: module.accent,
    status: module.status,
  }));
}

export function evaluateVisibleIf(predicate: SchemaPredicate | undefined, values: Record<string, string | number | boolean>): boolean {
  if (!predicate) return true;
  return values[predicate.parameter] === predicate.equals;
}