// Farbmathematik für die Referenz: sRGB ↔ linear ↔ Oklab, CIE L*, WCAG-Kontrast,
// Simulation von Farbsehschwächen (Machado et al. 2009, Schweregrad 1.0).
// Dieselben Formeln gehören später in den Rust-LUT-Baker.

export type RGB = [number, number, number]; // sRGB, 0..1
export type Lab = [number, number, number]; // Oklab

export function hexToRgb(hex: string): RGB {
  let h = hex.trim().replace('#', '');
  if (h.length === 3) h = h.split('').map((c) => c + c).join('');
  const n = parseInt(h, 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

export function rgbToHex(rgb: RGB): string {
  return (
    '#' +
    rgb
      .map((v) => Math.round(clamp01(v) * 255).toString(16).padStart(2, '0'))
      .join('')
      .toUpperCase()
  );
}

export function rgbToCss(rgb: RGB): string {
  return `rgb(${Math.round(clamp01(rgb[0]) * 255)} ${Math.round(clamp01(rgb[1]) * 255)} ${Math.round(clamp01(rgb[2]) * 255)})`;
}

export const toLinear = (c: number) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
export const toGamma = (c: number) => (c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055);

export function rgbToOklab(rgb: RGB): Lab {
  const r = toLinear(rgb[0]);
  const g = toLinear(rgb[1]);
  const b = toLinear(rgb[2]);
  const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}

/** Oklab → sRGB ohne Begrenzung (Werte außerhalb 0..1 = außerhalb des Gamuts). */
export function oklabToRgbRaw(lab: Lab): RGB {
  const l = Math.pow(lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2], 3);
  const m = Math.pow(lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2], 3);
  const s = Math.pow(lab[0] - 0.0894841775 * lab[1] - 1.291485548 * lab[2], 3);
  return [
    toGamma(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
    toGamma(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
    toGamma(-0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s),
  ];
}

export function oklabToRgb(lab: Lab): RGB {
  const c = oklabToRgbRaw(lab);
  return [clamp01(c[0]), clamp01(c[1]), clamp01(c[2])];
}

export function relLuminance(rgb: RGB): number {
  return 0.2126 * toLinear(rgb[0]) + 0.7152 * toLinear(rgb[1]) + 0.0722 * toLinear(rgb[2]);
}

/** CIE L* (0..100) – die Größe, die das Auge als „Helligkeit“ einer Colormap liest. */
export function lstar(rgb: RGB): number {
  const y = relLuminance(rgb);
  return y <= 216 / 24389 ? y * (24389 / 27) : 116 * Math.cbrt(y) - 16;
}

export function contrast(a: RGB, b: RGB): number {
  const la = relLuminance(a);
  const lb = relLuminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

/** Farbabstand in Oklab, ×100 skaliert (≈ 2 gerade noch sichtbar, < 10 leicht zu verwechseln). */
export function deltaE(a: RGB, b: RGB): number {
  const x = rgbToOklab(a);
  const y = rgbToOklab(b);
  return 100 * Math.hypot(x[0] - y[0], x[1] - y[1], x[2] - y[2]);
}

export type Cvd = 'normal' | 'protan' | 'deutan' | 'tritan';

const CVD_M: Record<Exclude<Cvd, 'normal'>, number[]> = {
  protan: [0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998],
  deutan: [0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.01182, 0.04294, 0.968881],
  tritan: [1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.3039],
};

export function simulateCvd(rgb: RGB, type: Cvd): RGB {
  if (type === 'normal') return rgb;
  const m = CVD_M[type];
  const r = toLinear(rgb[0]);
  const g = toLinear(rgb[1]);
  const b = toLinear(rgb[2]);
  return [
    clamp01(toGamma(clamp01(m[0] * r + m[1] * g + m[2] * b))),
    clamp01(toGamma(clamp01(m[3] * r + m[4] * g + m[5] * b))),
    clamp01(toGamma(clamp01(m[6] * r + m[7] * g + m[8] * b))),
  ];
}

export const CVD_LABEL: Record<Cvd, string> = {
  normal: 'Normal',
  protan: 'Protanopie',
  deutan: 'Deuteranopie',
  tritan: 'Tritanopie',
};

export function mixOklab(a: RGB, b: RGB, t: number): RGB {
  const x = rgbToOklab(a);
  const y = rgbToOklab(b);
  return oklabToRgb([x[0] + (y[0] - x[0]) * t, x[1] + (y[1] - x[1]) * t, x[2] + (y[2] - x[2]) * t]);
}

/** Naives Mischen der sRGB-Bytes (so interpoliert z. B. ein CSS-Verlauf). */
export function mixSrgb(a: RGB, b: RGB, t: number): RGB {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

/** Mischen in linearem Licht (so interpoliert die GPU eine sRGB-Textur). */
export function mixLinear(a: RGB, b: RGB, t: number): RGB {
  return [0, 1, 2].map((i) => toGamma(toLinear(a[i]) + (toLinear(b[i]) - toLinear(a[i])) * t)) as RGB;
}

/** Mischen über den Farbton (OKLCH, kürzerer Bogen) – bleibt auch zwischen Gegenfarben bunt. */
export function mixOklch(a: RGB, b: RGB, t: number): RGB {
  const x = rgbToOklab(a);
  const y = rgbToOklab(b);
  const ca = Math.hypot(x[1], x[2]);
  const cb = Math.hypot(y[1], y[2]);
  const ha = Math.atan2(x[2], x[1]);
  let dh = Math.atan2(y[2], y[1]) - ha;
  if (dh > Math.PI) dh -= 2 * Math.PI;
  if (dh < -Math.PI) dh += 2 * Math.PI;
  const L = x[0] + (y[0] - x[0]) * t;
  const C = ca + (cb - ca) * t;
  const h = ha + dh * t;
  let k = 1;
  let c = oklabToRgbRaw([L, C * Math.cos(h), C * Math.sin(h)]);
  while (!inGamut(c) && k > 0) {
    k -= 0.02;
    c = oklabToRgbRaw([L, C * k * Math.cos(h), C * k * Math.sin(h)]);
  }
  return [clamp01(c[0]), clamp01(c[1]), clamp01(c[2])];
}

function inGamut(c: RGB): boolean {
  return c.every((v) => v >= -0.0005 && v <= 1.0005);
}

/**
 * Hellt einen Akzent in Oklab so weit auf, bis er als Text auf `bg` den Zielkontrast
 * erreicht. Farbton bleibt, Chroma wird nur reduziert, wenn der Gamut es erzwingt.
 */
export function textTint(hex: string, bgHex: string, target = 4.5): string {
  const bg = hexToRgb(bgHex);
  const base = hexToRgb(hex);
  if (contrast(base, bg) >= target) return hex.toUpperCase();
  const lab = rgbToOklab(base);
  for (let L = lab[0]; L <= 1; L += 0.005) {
    let k = 1;
    let c = oklabToRgbRaw([L, lab[1], lab[2]]);
    while (!inGamut(c) && k > 0) {
      k -= 0.04;
      c = oklabToRgbRaw([L, lab[1] * k, lab[2] * k]);
    }
    const cc: RGB = [clamp01(c[0]), clamp01(c[1]), clamp01(c[2])];
    if (contrast(cc, bg) >= target) return rgbToHex(cc);
  }
  return '#FFFFFF';
}

/** Schwarz oder Weiß – je nachdem, was auf der Fläche besser lesbar ist. */
export function onColor(hex: string): string {
  const c = hexToRgb(hex);
  return contrast(c, [0.04, 0.045, 0.06]) >= contrast(c, [1, 1, 1]) ? '#0A0B0F' : '#FFFFFF';
}
