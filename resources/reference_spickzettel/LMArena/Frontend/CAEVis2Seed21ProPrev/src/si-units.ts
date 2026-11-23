// SI dimension vector arithmetic [kg, m, s, K, mol, A, cd]
// Mirrors the Rust enum/struct used in the rhai Field Calculator.

import type { DimVec } from './types';

export const DIMLESS: DimVec = [0, 0, 0, 0, 0, 0, 0];
export const KG: DimVec = [1, 0, 0, 0, 0, 0, 0];
export const M: DimVec = [0, 1, 0, 0, 0, 0, 0];
export const S: DimVec = [0, 0, 1, 0, 0, 0, 0];
export const K: DimVec = [0, 0, 0, 1, 0, 0, 0];
export const MOL: DimVec = [0, 0, 0, 0, 1, 0, 0];
export const A: DimVec = [0, 0, 0, 0, 0, 1, 0];
export const CD: DimVec = [0, 0, 0, 0, 0, 0, 1];

// Named derived dimensions
export const PRESSURE: DimVec = [1, -1, -2, 0, 0, 0, 0]; // kg·m⁻¹·s⁻²
export const DENSITY: DimVec = [1, -3, 0, 0, 0, 0, 0]; // kg·m⁻³
export const VELOCITY: DimVec = [0, 1, -1, 0, 0, 0, 0]; // m·s⁻¹
export const SPEC_KE: DimVec = [0, 2, -2, 0, 0, 0, 0]; // m²·s⁻² (but J/kg)
export const ENERGY_PER_VOL: DimVec = [1, -1, -2, 0, 0, 0, 0]; // = pressure (Pa)
export const TEMPERATURE: DimVec = K;
export const DYN_VISC: DimVec = [1, -1, -1, 0, 0, 0, 0]; // kg·m⁻¹·s⁻¹ = Pa·s
export const KIN_VISC: DimVec = [0, 2, -1, 0, 0, 0, 0]; // m²·s⁻¹
export const THERM_COND: DimVec = [1, 1, -3, -1, 0, 0, 0]; // W/(m·K)
export const LENGTH: DimVec = M;
export const TIME: DimVec = S;
export const MASS: DimVec = KG;
export const MACH: DimVec = DIMLESS;
export const CP_COEFF: DimVec = DIMLESS;
export const YPLUS: DimVec = DIMLESS;
export const NUSSELT: DimVec = DIMLESS;
export const TAU_WALL: DimVec = PRESSURE;
export const ENTROPY_PER_KELVIN: DimVec = [1, 2, -2, -1, 0, 0, 0]; // J/K per unit vol

export function addDim(a: DimVec, b: DimVec): DimVec {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3], a[4] + b[4], a[5] + b[5], a[6] + b[6]];
}
export function subDim(a: DimVec, b: DimVec): DimVec {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3], a[4] - b[4], a[5] - b[5], a[6] - b[6]];
}
export function mulDim(a: DimVec, b: DimVec): DimVec {
  return addDim(a, b);
}
export function divDim(a: DimVec, b: DimVec): DimVec {
  return subDim(a, b);
}
export function sqrtDim(a: DimVec): DimVec {
  return a.map((v) => v / 2) as DimVec;
}
export function powDim(a: DimVec, n: number): DimVec {
  return a.map((v) => v * n) as DimVec;
}
export function magDim(a: DimVec): DimVec {
  return a; // magnitude of vector has same dims as components
}
export function dotDim(a: DimVec, b: DimVec): DimVec {
  return addDim(a, b);
}
export function crossDim(a: DimVec, b: DimVec): DimVec {
  return addDim(a, b);
}

// Grad: /m ; Div: /m ; Curl: /m
export function gradDim(a: DimVec): DimVec {
  return [a[0], a[1] - 1, a[2], a[3], a[4], a[5], a[6]];
}
export function divDimVec(a: DimVec): DimVec {
  return [a[0], a[1] - 1, a[2], a[3], a[4], a[5], a[6]];
}
export function curlDimVec(a: DimVec): DimVec {
  return [a[0], a[1] - 1, a[2], a[3], a[4], a[5], a[6]];
}
export function magSqDim(a: DimVec): DimVec {
  return [a[0] * 2, a[1] * 2, a[2] * 2, a[3] * 2, a[4] * 2, a[5] * 2, a[6] * 2];
}

const SUPS = ['⁰', '¹', '²', '³', '⁴', '⁵', '⁶', '⁷', '⁸', '⁹'];
const SUBS = ['₀', '₁', '₂', '₃', '₄', '₅', '₆', '₇', '₈', '₉'];

function formatExp(n: number, pos: string, neg: string): string {
  if (n === 0) return '';
  const absN = Math.abs(n);
  const sign = n > 0 ? pos : neg;
  if (Number.isInteger(absN)) {
    const digits = absN
      .toString()
      .split('')
      .map((d) => (n > 0 ? SUPS[+d] : SUBS[+d]))
      .join('');
    return sign + digits;
  }
  return sign + absN.toFixed(2);
}

export function dimToHuman(d: DimVec): string {
  const units: Array<[string, number]> = [
    ['kg', d[0]],
    ['m', d[1]],
    ['s', d[2]],
    ['K', d[3]],
    ['mol', d[4]],
    ['A', d[5]],
    ['cd', d[6]],
  ];
  const parts = units
    .filter(([, v]) => v !== 0)
    .map(([name, v]) => {
      if (v === 1) return name;
      const exp = formatExp(v, '', '⁻');
      return name + exp;
    });
  if (parts.length === 0) return 'Dimensionless [-]';
  return '[' + parts.join('·') + ']';
}

export function dimEqual(a: DimVec, b: DimVec, tol = 1e-9): boolean {
  return a.every((v, i) => Math.abs(v - b[i]) < tol);
}

// Suggest a multiplicative fix: user tried to add `have` to `want` — return what to multiply by
export function suggestScaleFactor(have: DimVec, want: DimVec): { factor: DimVec; label: string } | null {
  const diff = subDim(want, have);
  // If diff corresponds to a known field var, name it
  if (dimEqual(diff, DENSITY)) return { factor: diff, label: 'Density ρ' };
  if (dimEqual(diff, PRESSURE)) return { factor: diff, label: 'Pressure p' };
  if (dimEqual(diff, DYN_VISC)) return { factor: diff, label: 'Dynamic Viscosity μ' };
  if (dimEqual(diff, KIN_VISC)) return { factor: diff, label: 'Kinematic Viscosity ν' };
  if (dimEqual(diff, THERM_COND)) return { factor: diff, label: 'Thermal Conductivity κ' };
  if (dimEqual(diff, VELOCITY)) return { factor: diff, label: 'Velocity |U|' };
  return { factor: diff, label: dimToHuman(diff) };
}
