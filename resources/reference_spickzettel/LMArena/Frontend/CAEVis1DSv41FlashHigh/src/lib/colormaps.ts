// Oklab-interpolated 1D LUTs (mirrors the wgpu LUT texture path).
type RGB = [number, number, number];
const hex = (h: string): RGB => [
  parseInt(h.slice(1, 3), 16) / 255,
  parseInt(h.slice(3, 5), 16) / 255,
  parseInt(h.slice(5, 7), 16) / 255,
];

const srgbToLinear = (c: number) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const linearToSrgb = (c: number) => (c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055);

function rgbToOklab([r, g, b]: RGB): RGB {
  const lr = srgbToLinear(r), lg = srgbToLinear(g), lb = srgbToLinear(b);
  const l = Math.cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb);
  const m = Math.cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb);
  const s = Math.cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb);
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}

function oklabToRgb([L, a, b]: RGB): RGB {
  const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  const s_ = L - 0.0894841775 * a - 1.291485548 * b;
  const l = l_ ** 3, m = m_ ** 3, s = s_ ** 3;
  return [
    linearToSrgb(Math.min(1, Math.max(0, 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s))),
    linearToSrgb(Math.min(1, Math.max(0, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s))),
    linearToSrgb(Math.min(1, Math.max(0, -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s))),
  ];
}

export const COLORMAPS = {
  turbo: ["#30123b", "#4360d8", "#2aa9ff", "#1ae4b6", "#a6fc3c", "#ffd83d", "#f2700a", "#a41401"],
  coolwarm: ["#3b4cc0", "#6d8fe9", "#9bbcf2", "#cfd9e6", "#e4dedb", "#f4c3ae", "#f3997b", "#d95f4f", "#b40426"],
  inferno: ["#000004", "#1c0c41", "#4b0c6b", "#7a1c6d", "#a52c60", "#d24445", "#ef6925", "#fb9b06", "#f7d13d", "#fcffa4"],
} as const;

export type ColormapName = keyof typeof COLORMAPS;

export function oklabLUT(name: ColormapName, n = 256): Uint8Array {
  const stops = COLORMAPS[name].map((h) => rgbToOklab(hex(h)));
  const data = new Uint8Array(n * 4);
  for (let i = 0; i < n; i++) {
    const t = i / (n - 1);
    const p = t * (stops.length - 1);
    const i0 = Math.min(stops.length - 1, Math.floor(p));
    const i1 = Math.min(stops.length - 1, i0 + 1);
    const f = p - i0;
    const lab: RGB = [
      stops[i0][0] + (stops[i1][0] - stops[i0][0]) * f,
      stops[i0][1] + (stops[i1][1] - stops[i0][1]) * f,
      stops[i0][2] + (stops[i1][2] - stops[i0][2]) * f,
    ];
    const c = oklabToRgb(lab);
    data[i * 4] = Math.round(c[0] * 255);
    data[i * 4 + 1] = Math.round(c[1] * 255);
    data[i * 4 + 2] = Math.round(c[2] * 255);
    data[i * 4 + 3] = 255;
  }
  return data;
}

export function colorAt(name: ColormapName, t: number): string {
  const stops = COLORMAPS[name].map((h) => rgbToOklab(hex(h)));
  const tc = Math.min(1, Math.max(0, t));
  const p = tc * (stops.length - 1);
  const i0 = Math.min(stops.length - 1, Math.floor(p));
  const i1 = Math.min(stops.length - 1, i0 + 1);
  const f = p - i0;
  const c = oklabToRgb([
    stops[i0][0] + (stops[i1][0] - stops[i0][0]) * f,
    stops[i0][1] + (stops[i1][1] - stops[i0][1]) * f,
    stops[i0][2] + (stops[i1][2] - stops[i0][2]) * f,
  ]);
  return `rgb(${Math.round(c[0] * 255)},${Math.round(c[1] * 255)},${Math.round(c[2] * 255)})`;
}

export const gradientCss = (name: ColormapName) =>
  `linear-gradient(90deg, ${COLORMAPS[name].join(", ")})`;
