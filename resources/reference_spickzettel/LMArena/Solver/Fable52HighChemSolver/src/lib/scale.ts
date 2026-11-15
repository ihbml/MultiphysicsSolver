import type { CSSProperties } from "react";

/** The shared axis: log10 of length in metres, from 1 Å to 1 km. */
export const AXIS_LO = -10;
export const AXIS_HI = 3;
export const DECADES = AXIS_HI - AXIS_LO;

export type ScaleId = "micro" | "meso" | "macro";

export const BANDS: { id: ScaleId; lo: number; hi: number; label: string; hint: string }[] = [
  { id: "micro", lo: -10, hi: -7, label: "Micro", hint: "atoms, molecules" },
  { id: "meso", lo: -7, hi: -4, label: "Meso", hint: "pores, grains, films" },
  { id: "macro", lo: -4, hi: 3, label: "Macro", hint: "devices, reactors, reservoirs" },
];

export const TICKS: { exp: number; label: string }[] = [
  { exp: -10, label: "1 Å" },
  { exp: -9, label: "1 nm" },
  { exp: -6, label: "1 µm" },
  { exp: -3, label: "1 mm" },
  { exp: 0, label: "1 m" },
  { exp: 3, label: "1 km" },
];

/** Fraction (0–1) along the axis for a given exponent. */
export const pos = (exp: number) => (exp - AXIS_LO) / DECADES;

export const SCALE_GRADIENT =
  "linear-gradient(90deg, var(--color-micro) 0%, var(--color-meso) 36%, var(--color-macro) 100%)";

/**
 * Positions an element on the axis and paints it as a window onto the
 * full-axis gradient, so colour always means the same length scale.
 */
export function gradientWindow(lo: number, hi: number): CSSProperties {
  const left = pos(lo);
  const width = Math.max(pos(hi) - left, 0.0001);
  return {
    left: `${left * 100}%`,
    width: `${width * 100}%`,
    backgroundImage: SCALE_GRADIENT,
    backgroundSize: `${100 / width}% 100%`,
    backgroundPosition: `${width < 1 ? (left / (1 - width)) * 100 : 0}% 0`,
  };
}

const LENGTHS: Record<number, string> = {
  [-10]: "0.1 nm",
  [-9]: "1 nm",
  [-8]: "10 nm",
  [-7]: "100 nm",
  [-6]: "1 µm",
  [-5]: "10 µm",
  [-4]: "100 µm",
  [-3]: "1 mm",
  [-2]: "1 cm",
  [-1]: "10 cm",
  [0]: "1 m",
  [1]: "10 m",
  [2]: "100 m",
  [3]: "1 km",
};

export const formatLength = (exp: number) => LENGTHS[exp] ?? `10^${exp} m`;

/** A solver counts as covering a band when it overlaps it by two decades or more. */
export function bandsCovered(range: [number, number]): ScaleId[] {
  return BANDS.filter((b) => Math.min(range[1], b.hi) - Math.max(range[0], b.lo) >= 2).map(
    (b) => b.id,
  );
}
