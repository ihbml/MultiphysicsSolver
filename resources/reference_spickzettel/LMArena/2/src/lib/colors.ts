const VIRIDIS: [number, number, number][] = [
  [68, 1, 84],
  [59, 82, 139],
  [33, 145, 140],
  [94, 201, 98],
  [253, 231, 37],
];
const MAGMA: [number, number, number][] = [
  [0, 0, 4],
  [81, 18, 124],
  [183, 55, 121],
  [252, 137, 97],
  [252, 253, 191],
];
const COOLWARM: [number, number, number][] = [
  [59, 76, 192],
  [144, 178, 254],
  [220, 220, 220],
  [246, 148, 122],
  [180, 4, 38],
];
const TURBO: [number, number, number][] = [
  [48, 18, 59],
  [37, 160, 226],
  [100, 221, 100],
  [250, 186, 57],
  [165, 15, 21],
];

export type Ramp = "viridis" | "magma" | "coolwarm" | "turbo";

const RAMPS: Record<Ramp, [number, number, number][]> = {
  viridis: VIRIDIS,
  magma: MAGMA,
  coolwarm: COOLWARM,
  turbo: TURBO,
};

export function ramp(t: number, name: Ramp = "viridis"): string {
  const stops = RAMPS[name];
  const x = Math.max(0, Math.min(1, isFinite(t) ? t : 0)) * (stops.length - 1);
  const i = Math.min(stops.length - 2, Math.floor(x));
  const f = x - i;
  const a = stops[i];
  const b = stops[i + 1];
  const r = Math.round(a[0] + (b[0] - a[0]) * f);
  const g = Math.round(a[1] + (b[1] - a[1]) * f);
  const bl = Math.round(a[2] + (b[2] - a[2]) * f);
  return `rgb(${r},${g},${bl})`;
}

export function rampCss(name: Ramp = "viridis"): string {
  const stops = RAMPS[name];
  return `linear-gradient(90deg, ${stops
    .map((c, i) => `rgb(${c[0]},${c[1]},${c[2]}) ${(100 * i) / (stops.length - 1)}%`)
    .join(", ")})`;
}

export function fmt(v: number, digits = 3): string {
  if (!isFinite(v)) return "–";
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e5 || a < 1e-3) return v.toExponential(digits - 1);
  return v.toFixed(Math.max(0, digits - Math.max(0, Math.floor(Math.log10(a)) + 1)));
}
