// ─────────────────────────────────────────────────────────────────────────────
// 1-D Oklab perceptual colormaps, precomputed to 256×1 RGBA LUT textures
// (mirrors rust/src/render/shaders.wgsl LUT upload in the wgpu build).
// Optional Ansys-style contour banding quantizes the LUT itself so band edges
// are pixel-exact and can be isoline-darkened in the fragment shader.
// ─────────────────────────────────────────────────────────────────────────────

export type CmapName = "viridis" | "moreland" | "fire" | "darkEmber" | "traffic";
export const CMAPS: { id: CmapName; label: string }[] = [
  { id: "viridis", label: "Viridis" },
  { id: "moreland", label: "Moreland C-W" },
  { id: "fire", label: "Fire" },
  { id: "darkEmber", label: "Dark Ember" },
  { id: "traffic", label: "Traffic Light" },
];

// ── sRGB ⇄ Oklab (Ottosson) ──────────────────────────────────────────────
function s2l(c: number) {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}
function l2s(c: number) {
  const v = c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
  return Math.min(1, Math.max(0, v));
}
function hex2rgb(h: string): [number, number, number] {
  return [
    parseInt(h.slice(1, 3), 16) / 255,
    parseInt(h.slice(3, 5), 16) / 255,
    parseInt(h.slice(5, 7), 16) / 255,
  ];
}
function rgb2oklab(r: number, g: number, b: number): [number, number, number] {
  const R = s2l(r), G = s2l(g), B = s2l(b);
  let l = 0.4122214708 * R + 0.5363325363 * G + 0.0514459929 * B;
  let m = 0.2119034982 * R + 0.6806995451 * G + 0.1073969566 * B;
  let s = 0.0883024619 * R + 0.2817188376 * G + 0.6299787005 * B;
  l = Math.cbrt(Math.max(l, 0)); m = Math.cbrt(Math.max(m, 0)); s = Math.cbrt(Math.max(s, 0));
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}
function oklab2rgb(L: number, a: number, b: number): [number, number, number] {
  let l = L + 0.3963377774 * a + 0.2158037573 * b;
  let m = L - 0.1055613458 * a - 0.0638541728 * b;
  let s = L - 0.0894841775 * a - 1.291485548 * b;
  l = l * l * l; m = m * m * m; s = s * s * s;
  const R = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  const G = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  const B = -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s;
  return [l2s(R), l2s(G), l2s(B)];
}

const TABLES: Record<CmapName, string[]> = {
  viridis: ["#440154", "#482878", "#3E4A89", "#31688E", "#26828E", "#35B779", "#7AD151", "#BEE54B", "#FDE725"],
  moreland: ["#3B4CC0", "#7787CC", "#BBBBBB", "#DE9383", "#B40426"],
  fire: ["#060104", "#3D0B14", "#8A1E0D", "#D94E08", "#F5A20B", "#FFE66D", "#FFFBEE"],
  darkEmber: ["#050308", "#170A13", "#36100F", "#622210", "#9C3E12", "#D47A1B", "#F2C04A"],
  traffic: ["#1E9E46", "#4FBE4E", "#96C83C", "#D8C81F", "#F0A20E", "#EF6C1E", "#E5372C"],
};

// Control points converted once into Oklab space for perceptual interpolation.
const OKLAB_TABLES: Record<CmapName, [number, number, number][]> = (
  Object.keys(TABLES) as CmapName[]
).reduce((acc, k) => {
  acc[k] = TABLES[k].map((h) => {
    const [r, g, b] = hex2rgb(h);
    return rgb2oklab(r, g, b);
  });
  return acc;
}, {} as Record<CmapName, [number, number, number][]>);

/** Build a 256×1 RGBA8 LUT. `bands>0` quantizes into N discrete contour bands. */
export function buildLUT(name: CmapName, bands: number): Uint8Array {
  const lut = new Uint8Array(256 * 4);
  const pts = OKLAB_TABLES[name];
  const n = pts.length - 1;
  for (let i = 0; i < 256; i++) {
    let t = i / 255;
    if (bands > 1) t = Math.floor(t * bands) / bands + 0.5 / bands;
    const f = Math.min(n - 1e-6, Math.max(0, t)) * n;
    const i0 = Math.floor(f);
    const fr = f - i0;
    const A = pts[i0];
    const B = pts[Math.min(n, i0 + 1)];
    const [r, g, b] = oklab2rgb(
      A[0] + (B[0] - A[0]) * fr,
      A[1] + (B[1] - A[1]) * fr,
      A[2] + (B[2] - A[2]) * fr
    );
    lut[i * 4] = Math.round(r * 255);
    lut[i * 4 + 1] = Math.round(g * 255);
    lut[i * 4 + 2] = Math.round(b * 255);
    lut[i * 4 + 3] = 255;
  }
  return lut;
}

export function sampleLUT(lut: Uint8Array, t: number): [number, number, number] {
  const i = Math.min(255, Math.max(0, Math.round(t * 255))) * 4;
  return [lut[i], lut[i + 1], lut[i + 2]];
}

/** CSS gradient for HTML colorbars — samples the same quantized LUT. */
export function lutCss(name: CmapName, bands: number, steps = 24): string {
  const lut = buildLUT(name, bands);
  const stops: string[] = [];
  for (let i = 0; i < steps; i++) {
    const [r, g, b] = sampleLUT(lut, i / (steps - 1));
    stops.push(`rgb(${r},${g},${b}) ${((i / (steps - 1)) * 100).toFixed(1)}%`);
  }
  return `linear-gradient(90deg, ${stops.join(",")})`;
}
