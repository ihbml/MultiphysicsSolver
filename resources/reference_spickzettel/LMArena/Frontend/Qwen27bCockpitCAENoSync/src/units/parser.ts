// ─────────────────────────────────────────────────────────────────────────────
// Unit-Aware Expression Parser
// Converts operator inputs like `12.5 mm`, `850 C`, `3.2 bar`, `1500 rpm`
// into strict SI base units, validated against 7-element SI dimension vectors
// [kg, m, s, K, mol, A, cd] before any JSON-RPC payload is dispatched.
// ─────────────────────────────────────────────────────────────────────────────

export type Dim = [number, number, number, number, number, number, number];
export const DIM_NAMES = ["kg", "m", "s", "K", "mol", "A", "cd"] as const;

export const ZERO_DIM: Dim = [0, 0, 0, 0, 0, 0, 0];

const D = {
  dimless: [0, 0, 0, 0, 0, 0, 0] as Dim,
  m: [0, 1, 0, 0, 0, 0, 0] as Dim,
  kg: [1, 0, 0, 0, 0, 0, 0] as Dim,
  s: [0, 0, 1, 0, 0, 0, 0] as Dim,
  K: [0, 0, 0, 1, 0, 0, 0] as Dim,
  mol: [0, 0, 0, 0, 1, 0, 0] as Dim,
  A: [0, 0, 0, 0, 0, 1, 0] as Dim,
  cd: [0, 0, 0, 0, 0, 0, 1] as Dim,
  Pa: [1, -1, -2, 0, 0, 0, 0] as Dim, // kg·m⁻¹·s⁻²
  N: [1, 1, -2, 0, 0, 0, 0] as Dim,
  J: [1, 2, -2, 0, 0, 0, 0] as Dim,
  W: [1, 2, -3, 0, 0, 0, 0] as Dim,
  mPerS: [0, 1, -1, 0, 0, 0, 0] as Dim,
  radPerS: [0, 0, -1, 0, 0, 0, 0] as Dim,
  m2PerS2: [0, 2, -2, 0, 0, 0, 0] as Dim,
  kgPerM3: [1, -3, 0, 0, 0, 0, 0] as Dim,
  PaS: [1, 0, -1, 0, 0, 0, 0] as Dim, // dynamic viscosity
  Wm2K: [1, 0, -3, -1, 0, 0, 0] as Dim,
} as const;

interface UnitDef {
  dim: Dim;
  toSI: number;
  offset?: number; // Kelvin offset (e.g. °C → K)
  label: string;
}

export const UNITS: Record<string, UnitDef> = {
  "–": { dim: D.dimless, toSI: 1, label: "–" },
  "-": { dim: D.dimless, toSI: 1, label: "–" },
  "": { dim: D.dimless, toSI: 1, label: "–" },

  m: { dim: D.m, toSI: 1, label: "m" },
  mm: { dim: D.m, toSI: 1e-3, label: "mm" },
  cm: { dim: D.m, toSI: 1e-2, label: "cm" },
  km: { dim: D.m, toSI: 1e3, label: "km" },
  um: { dim: D.m, toSI: 1e-6, label: "µm" },
  nm: { dim: D.m, toSI: 1e-9, label: "nm" },

  kg: { dim: D.kg, toSI: 1, label: "kg" },
  g: { dim: D.kg, toSI: 1e-3, label: "g" },

  s: { dim: D.s, toSI: 1, label: "s" },
  ms: { dim: D.s, toSI: 1e-3, label: "ms" },
  min: { dim: D.s, toSI: 60, label: "min" },
  h: { dim: D.s, toSI: 3600, label: "h" },

  K: { dim: D.K, toSI: 1, label: "K" },
  C: { dim: D.K, toSI: 1, offset: 273.15, label: "°C" },
  degC: { dim: D.K, toSI: 1, offset: 273.15, label: "°C" },

  Pa: { dim: D.Pa, toSI: 1, label: "Pa" },
  kPa: { dim: D.Pa, toSI: 1e3, label: "kPa" },
  MPa: { dim: D.Pa, toSI: 1e6, label: "MPa" },
  mbar: { dim: D.Pa, toSI: 100, label: "mbar" },
  bar: { dim: D.Pa, toSI: 1e5, label: "bar" },
  atm: { dim: D.Pa, toSI: 101325, label: "atm" },

  N: { dim: D.N, toSI: 1, label: "N" },
  kN: { dim: D.N, toSI: 1e3, label: "kN" },
  J: { dim: D.J, toSI: 1, label: "J" },
  W: { dim: D.W, toSI: 1, label: "W" },

  "m/s": { dim: D.mPerS, toSI: 1, label: "m/s" },
  "km/h": { dim: D.mPerS, toSI: 1000 / 3600, label: "km/h" },
  "cm/s": { dim: D.mPerS, toSI: 1e-2, label: "cm/s" },

  "rad/s": { dim: D.radPerS, toSI: 1, label: "rad/s" },
  rpm: { dim: D.radPerS, toSI: (2 * Math.PI) / 60, label: "rpm" },
  Hz: { dim: D.radPerS, toSI: 1, label: "Hz" },

  "m²/s²": { dim: D.m2PerS2, toSI: 1, label: "m²/s²" },
  "m2/s2": { dim: D.m2PerS2, toSI: 1, label: "m²/s²" },
  "m²/s": { dim: [0, 2, -1, 0, 0, 0, 0], toSI: 1, label: "m²/s" },

  "kg/m³": { dim: D.kgPerM3, toSI: 1, label: "kg/m³" },
  "kg/m3": { dim: D.kgPerM3, toSI: 1, label: "kg/m³" },
  "kg/(m·s)": { dim: D.PaS, toSI: 1, label: "Pa·s" },
  "Pa·s": { dim: D.PaS, toSI: 1, label: "Pa·s" },
  "cP": { dim: D.PaS, toSI: 1e-3, label: "cP" },
  "W/(m·K)": { dim: D.Wm2K, toSI: 1, label: "W/(m·K)" },
};

export interface Parsed {
  ok: boolean;
  si: number; // strict SI base-unit value
  raw: number;
  unit: string;
  dim: Dim;
  error?: string;
  isOffsetApplied?: boolean;
}

export function fmtDim(dim: Dim): string {
  const sup = (n: number) => {
    if (n === 1) return "";
    const map: Record<string, string> = {
      "0": "⁰", "1": "¹", "2": "²", "3": "³", "4": "⁴",
      "5": "⁵", "6": "⁶", "7": "⁷", "8": "⁸", "9": "⁹", "-": "⁻",
    };
    return String(n)
      .split("")
      .map((c) => map[c] ?? c)
      .join("");
  };
  let out = "";
  DIM_NAMES.forEach((n, i) => {
    const e = dim[i];
    if (e === 0) return;
    out += (out ? "·" : "") + n + sup(e);
  });
  return out === "" ? "dimensionless" : out;
}

export function dimEq(a: Dim, b: Dim): boolean {
  for (let i = 0; i < 7; i++) if (a[i] !== b[i]) return false;
  return true;
}

/**
 * Parse an operator expression such as "12.5 mm" into strict SI.
 * Bare numbers are assumed to already be SI base units of `expected` dim.
 */
export function parseExpr(input: string, expected?: Dim): Parsed {
  const trimmed = input.trim().replace("µ", "u");
  if (trimmed === "") return { ok: false, si: 0, raw: 0, unit: "", dim: ZERO_DIM, error: "empty expression" };

  const m = trimmed.match(/^([+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?)\s*([A-Za-zu°/\.\-²³()]+)?$/);
  if (!m) return { ok: false, si: 0, raw: 0, unit: "", dim: ZERO_DIM, error: "not a valid expression" };

  const raw = parseFloat(m[1]);
  if (!isFinite(raw)) return { ok: false, si: 0, raw: 0, unit: "", dim: ZERO_DIM, error: "non-finite value" };

  let unit = (m[4] ?? "").trim();
  if (unit === "°C" || unit === "C") unit = "C";
  const def = UNITS[unit];

  if (def) {
    let si = raw * def.toSI;
    let offApplied = false;
    if (def.offset) {
      si = raw + def.offset;
      offApplied = true;
    }
    if (expected && !dimEq(def.dim, expected)) {
      return {
        ok: false, si, raw, unit: def.label, dim: def.dim,
        error: `dimension mismatch — got ${fmtDim(def.dim)}, expected ${fmtDim(expected)}`,
      };
    }
    return { ok: true, si, raw, unit: def.label, dim: def.dim, isOffsetApplied: offApplied };
  }

  // Bare number → assume SI base unit of the expected dimension.
  const dim = expected ? [...expected] as Dim : ZERO_DIM;
  return { ok: true, si: raw, raw, unit: "SI", dim };
}

/** Human display of an SI value in a sensible engineering unit. */
export function fmtSI(si: number): string {
  if (!isFinite(si)) return "—";
  const a = Math.abs(si);
  if (a === 0) return "0";
  if (a >= 1e6 || a < 1e-3) return si.toExponential(3);
  if (a >= 100) return si.toFixed(2);
  if (a >= 1) return si.toPrecision(4);
  return si.toPrecision(3);
}
