export const BASE = import.meta.env.BASE_URL;
export type CaseId = "bubble" | "sod" | "acoustic" | "film";
export type Settings = { scheme: number; eos: number; cfl: number; hybrid: boolean; cavitation: boolean };
export const cases = {
  bubble: {
    id: 0, number: "03", title: "Shock-Bubble Collapse", short: "2D hybrid SPH-FVM",
    group: "Multiphase flow", description: "A shock compresses a vapor bubble at a moving liquid-gas interface.",
    defaultFrame: 43, frameCount: 71, end: .448, defaultField: 0,
  },
  sod: {
    id: 1, number: "01", title: "Sod Shock Tube", short: "1D compressible Euler",
    group: "Compressible flow", description: "Resolve the shock, contact discontinuity, and rarefaction in the Sod Riemann problem.",
    defaultFrame: 48, frameCount: 65, end: .2, defaultField: 0,
  },
  acoustic: {
    id: 2, number: "02", title: "Low-Mach Acoustics", short: "1D acoustic pulse",
    group: "All-Mach flow", description: "Track the amplitude and shape of a weak acoustic pulse in a periodic low-Mach flow.",
    defaultFrame: 34, frameCount: 65, end: .43, defaultField: 1,
  },
  film: {
    id: 3, number: "04", title: "Spray-Fed Wall Film", short: "2D surface film + DPM",
    group: "Discrete phase", description: "Follow droplet impingement, surface film transport, and evaporation on a finite-area mesh.",
    defaultFrame: 41, frameCount: 65, end: .3, defaultField: 0,
  },
} satisfies Record<CaseId, { id: number; number: string; title: string; short: string; group: string; description: string;
  defaultFrame: number; frameCount: number; end: number; defaultField: number }>;

export function defaults(id: CaseId): Settings {
  return { scheme: 0, eos: id === "bubble" ? 1 : 0, cfl: .42, hybrid: id === "bubble", cavitation: id === "bubble" };
}

export type Grid = { nx: number; ny: number; lx: number; ly: number; end: number; count: number };
type Quantized = { packed: Uint16Array; shift: number; step: number };
export type Frame = { m: Float64Array; fields: (Quantized | Float32Array)[] };
export type Dataset = Grid & { frames: (Frame | undefined)[]; origin: "native" | "wasm" };

export function scalar(frame: Frame, channel: number, i: number) {
  const f = frame.fields[channel];
  return f instanceof Float32Array ? f[i] : f.shift + f.packed[i] * f.step;
}

export const METRICS = 23;
export const CHANNELS = 4;
export const fieldOptions = {
  liquid: [
    { label: "Density", symbol: "\u03c1 / \u03c1\u2080", map: 0, title: "Density Field" },
    { label: "Pressure", symbol: "p / p\u2080", map: 1, title: "Pressure Field" },
    { label: "Mach number", symbol: "Ma", map: 2, title: "Mach Number" },
    { label: "Vapor fraction", symbol: "\u03b1\u1d65", map: 3, title: "Vapor Fraction" },
    { label: "Schlieren", symbol: "|\u2207\u03c1|", map: 4, title: "Numerical Schlieren" },
  ],
  film: [
    { label: "Film thickness", symbol: "\u03bcm", map: 0, title: "Film Thickness" },
    { label: "Tangential speed", symbol: "m/s", map: 1, title: "Tangential Speed" },
    { label: "Specific enthalpy", symbol: "kJ/kg", map: 2, title: "Specific Enthalpy" },
    { label: "Deposition", symbol: "\u03bcm eq.", map: 3, title: "Droplet Deposition" },
  ],
};

export function fieldsFor(id: CaseId) {
  return id === "film" ? fieldOptions.film : id === "bubble" ? fieldOptions.liquid : fieldOptions.liquid.slice(0, 3);
}

const references = new Map<string, Promise<Dataset>>();
export function reference(id: CaseId, preview = false): Promise<Dataset> {
  const key = preview ? `${id}-preview` : id;
  const existing = references.get(key);
  if (existing) return existing;
  const task = (async () => {
    const res = await fetch(`${BASE}data/${key}.cbf.gz`);
    if (!res.ok || !res.body) throw new Error("Reference data could not be loaded.");
    const raw = await res.arrayBuffer();
    const bytes = new Uint8Array(raw, 0, Math.min(2, raw.byteLength));
    const buf = bytes[0] === 0x1f && bytes[1] === 0x8b
      ? await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer()
      : raw;
    const h = new DataView(buf);
    if (h.getUint32(0, false) !== 0x43424632 || h.getInt32(20, true) !== METRICS) throw new Error("Unrecognized reference format.");
    const nx = h.getInt32(4, true), ny = h.getInt32(8, true), n = nx * ny;
    const available = h.getInt32(12, true), channels = h.getInt32(16, true);
    const count = preview ? cases[id].frameCount : available;
    const frames: Dataset["frames"] = Array(count);
    let offset = 48;
    for (let j = 0; j < available; j++) {
      const m = new Float64Array(buf.slice(offset, offset + METRICS * 8));
      offset += METRICS * 8;
      const fields: Frame["fields"] = [];
      for (let k = 0; k < channels; k++) {
        fields.push({ shift: h.getFloat32(offset, true), step: h.getFloat32(offset + 4, true), packed: new Uint16Array(buf, offset + 8, n) });
        offset += 8 + n * 2;
      }
      frames[preview ? cases[id].defaultFrame : j] = { m, fields };
    }
    return { nx, ny, lx: h.getFloat64(24, true), ly: h.getFloat64(32, true), end: h.getFloat64(40, true), count, frames, origin: "native" as const };
  })();
  references.set(key, task);
  task.catch(() => references.delete(key));
  return task;
}

export type Check = { group: string; label: string; value: number; limit: number; compare: string; pass_: boolean };
export type Report = { compiler: string; checks: Check[] };
export type Request = { id: number; settings: Settings; base: string };
export type Reply = { type: "grid"; data: Grid } | { type: "frame"; index: number; metrics: Float64Array; values: Float32Array }
  | { type: "done" } | { type: "error"; error: string };

export function diagnosticName(id: CaseId) {
  return id === "film" ? "Peak Film Thickness" : "Peak Pressure";
}
export function formatExp(n: number, precision = 2) {
  return n === 0 ? "0.00" : n.toExponential(precision).replace("e-", "e\u2212").replace("e+", "e+");
}