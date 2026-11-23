export type UnitDimension =
  | "length"
  | "velocity"
  | "temperature"
  | "pressure"
  | "angularVelocity"
  | "density"
  | "kinematicViscosity"
  | "dimensionless";

export type ParsedUnit = {
  valueSI: number;
  unitSI: string;
  dimension: UnitDimension;
  dimensions: [number, number, number, number, number, number, number];
};

type UnitDefinition = {
  dimension: UnitDimension;
  factor: number;
  offset?: number;
  unitSI: string;
};

const units: Record<string, UnitDefinition> = {
  m: { dimension: "length", factor: 1, unitSI: "m" },
  mm: { dimension: "length", factor: 1e-3, unitSI: "m" },
  cm: { dimension: "length", factor: 1e-2, unitSI: "m" },
  km: { dimension: "length", factor: 1e3, unitSI: "m" },
  "m/s": { dimension: "velocity", factor: 1, unitSI: "m/s" },
  "km/h": { dimension: "velocity", factor: 1 / 3.6, unitSI: "m/s" },
  kph: { dimension: "velocity", factor: 1 / 3.6, unitSI: "m/s" },
  k: { dimension: "temperature", factor: 1, unitSI: "K" },
  c: { dimension: "temperature", factor: 1, offset: 273.15, unitSI: "K" },
  "°c": { dimension: "temperature", factor: 1, offset: 273.15, unitSI: "K" },
  degc: { dimension: "temperature", factor: 1, offset: 273.15, unitSI: "K" },
  pa: { dimension: "pressure", factor: 1, unitSI: "Pa" },
  kpa: { dimension: "pressure", factor: 1e3, unitSI: "Pa" },
  mpa: { dimension: "pressure", factor: 1e6, unitSI: "Pa" },
  bar: { dimension: "pressure", factor: 1e5, unitSI: "Pa" },
  atm: { dimension: "pressure", factor: 101325, unitSI: "Pa" },
  rpm: { dimension: "angularVelocity", factor: (2 * Math.PI) / 60, unitSI: "rad/s" },
  "rad/s": { dimension: "angularVelocity", factor: 1, unitSI: "rad/s" },
  "kg/m3": { dimension: "density", factor: 1, unitSI: "kg/m^3" },
  "m2/s": { dimension: "kinematicViscosity", factor: 1, unitSI: "m^2/s" },
  "m^2/s": { dimension: "kinematicViscosity", factor: 1, unitSI: "m^2/s" },
  "1": { dimension: "dimensionless", factor: 1, unitSI: "-" },
  "y+": { dimension: "dimensionless", factor: 1, unitSI: "-" },
};

const siDimensions: Record<UnitDimension, ParsedUnit["dimensions"]> = {
  length: [0, 1, 0, 0, 0, 0, 0],
  velocity: [0, 1, -1, 0, 0, 0, 0],
  temperature: [0, 0, 0, 1, 0, 0, 0],
  pressure: [1, -1, -2, 0, 0, 0, 0],
  angularVelocity: [0, 0, -1, 0, 0, 0, 0],
  density: [1, -3, 0, 0, 0, 0, 0],
  kinematicViscosity: [0, 2, -1, 0, 0, 0, 0],
  dimensionless: [0, 0, 0, 0, 0, 0, 0],
};

const defaults: Record<UnitDimension, string> = {
  length: "m",
  velocity: "m/s",
  temperature: "K",
  pressure: "Pa",
  angularVelocity: "rad/s",
  density: "kg/m3",
  kinematicViscosity: "m2/s",
  dimensionless: "1",
};

export function parseUnitExpression(
  expression: string,
  expectedDimension: UnitDimension,
): ParsedUnit | null {
  const match = expression.trim().match(/^([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*([^\s]*)$/i);
  if (!match) return null;

  const value = Number(match[1]);
  const rawUnit = match[2] || defaults[expectedDimension];
  const normalizedUnit = rawUnit.toLowerCase().replace(/\s+/g, "");
  const definition = units[normalizedUnit];
  if (!definition || definition.dimension !== expectedDimension || !Number.isFinite(value)) return null;

  return {
    valueSI: value * definition.factor + (definition.offset ?? 0),
    unitSI: definition.unitSI,
    dimension: definition.dimension,
    dimensions: siDimensions[definition.dimension],
  };
}

export function formatSI(value: number, unit: string): string {
  if (!Number.isFinite(value)) return "invalid";
  const precision = Math.abs(value) >= 1000 || (Math.abs(value) > 0 && Math.abs(value) < 0.01) ? 3 : 4;
  return `${Number(value.toPrecision(precision)).toString()} ${unit}`;
}