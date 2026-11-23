// TypeScript mirror of cpp/cluster_a.hpp (same equations, same constants).
// The C++ header is the reference implementation verified by cpp/verify.cpp;
// this module powers the live, interactive engineering workbench.

export const SST = {
  betaStar: 0.09,
  a1: 0.31,
  kappa: 0.41,
  sigW2: 0.856,
  beta1: 0.075,
  beta2: 0.0828,
};

export const LAW = { kappa: 0.41, E: 9.8, Pr: 0.71, Prt: 0.85, beta1: 0.075, betaStar: 0.09 };

/* ---------------------------- k-omega SST ---------------------------- */

export interface SSTLocal {
  k: number;
  omega: number;
  nu: number;
  S: number;
  y: number;
  gradKdotGradW: number;
}

export function sstF1(f: SSTLocal): number {
  const w = Math.max(f.omega, 1e-300);
  const y = Math.max(f.y, 1e-300);
  const y2 = y * y;
  const sk = Math.sqrt(Math.max(f.k, 0));
  const cdkw = Math.max((2 * SST.sigW2 * f.gradKdotGradW) / w, 1e-10);
  const arg1 = Math.min(
    Math.max(sk / (SST.betaStar * w * y), (500 * f.nu) / (y2 * w)),
    (4 * SST.sigW2 * f.k) / (cdkw * y2),
  );
  return Math.tanh(arg1 ** 4);
}

export function sstF2(f: SSTLocal): number {
  const w = Math.max(f.omega, 1e-300);
  const y = Math.max(f.y, 1e-300);
  const sk = Math.sqrt(Math.max(f.k, 0));
  const arg2 = Math.max((2 * sk) / (SST.betaStar * w * y), (500 * f.nu) / (y * y * w));
  return Math.tanh(arg2 * arg2);
}

export function sstNut(k: number, omega: number, S: number, F2: number): number {
  return (SST.a1 * Math.max(k, 0)) / Math.max(SST.a1 * omega, S * F2);
}

export function sstPkLimited(nut: number, S: number, k: number, omega: number): number {
  return Math.min(nut * S * S, 10 * SST.betaStar * k * omega);
}

/** Patankar point-implicit k/omega update (V/dt given as VoverDt). */
export function patankarStep(
  k0: number,
  w0: number,
  Pk: number,
  Pw: number,
  crossDiff: number,
  beta: number,
  VoverDt: number,
) {
  const w = Math.max(w0, 1e-300);
  const kRhs = Math.max(k0, 0) * VoverDt + Math.max(Pk, 0);
  const kDiag = VoverDt + SST.betaStar * w;
  let wRhs = Math.max(w0, 0) * VoverDt + Math.max(Pw, 0);
  let wDiag = VoverDt + 2 * beta * w;
  if (crossDiff >= 0) wRhs += crossDiff;
  else wDiag += -crossDiff / w;
  return { k: kRhs / kDiag, omega: wRhs / wDiag };
}

export function explicitStep(k0: number, w0: number, Pk: number, Pw: number, dt: number, beta: number) {
  return {
    k: k0 + dt * (Pk - SST.betaStar * w0 * k0),
    omega: w0 + dt * (Pw - beta * w0 * w0),
  };
}

/* ------------------------------ WALE --------------------------------- */

export type Mat3 = number[][];

const mul = (A: Mat3, B: Mat3): Mat3 =>
  A.map((row) => [0, 1, 2].map((j) => row[0] * B[0][j] + row[1] * B[1][j] + row[2] * B[2][j]));

export function waleNuSgs(g: Mat3, delta: number, Cw = 0.325): number {
  const g2 = mul(g, g);
  const tr2 = g2[0][0] + g2[1][1] + g2[2][2];
  let SS = 0;
  let SdSd = 0;
  for (let i = 0; i < 3; i++)
    for (let j = 0; j < 3; j++) {
      const S = 0.5 * (g[i][j] + g[j][i]);
      const Sd = 0.5 * (g2[i][j] + g2[j][i]) - (i === j ? tr2 / 3 : 0);
      SS += S * S;
      SdSd += Sd * Sd;
    }
  const den = SS ** 2.5 + SdSd ** 1.25;
  if (den <= 1e-300) return 0;
  return (Cw * delta) ** 2 * (SdSd ** 1.5) / den;
}

/* ---------------------------- All-y+ --------------------------------- */

export const kaderGamma = (yp: number) => (0.01 * yp ** 4) / (1 + 5 * yp);

export function uPlus(yp: number, L = LAW): number {
  if (yp <= 0) return 0;
  const G = kaderGamma(yp);
  const visc = yp * Math.exp(-G);
  const log = G > 0 ? (Math.log1p(L.E * yp) / L.kappa) * Math.exp(-1 / G) : 0;
  return visc + log;
}

export const jayatilakaP = (L = LAW) => {
  const r = L.Pr / L.Prt;
  return 9.24 * (r ** 0.75 - 1) * (1 + 0.28 * Math.exp(-0.007 * r));
};

export function tPlus(yp: number, L = LAW): number {
  if (yp <= 0) return 0;
  const G = kaderGamma(yp);
  const visc = L.Pr * yp * Math.exp(-G);
  const log =
    G > 0 ? (L.Prt / L.kappa * Math.log1p(L.E * yp) + jayatilakaP(L)) * Math.exp(-1 / G) : 0;
  return visc + log;
}

export function solveUtau(U: number, y: number, nu: number, L = LAW): number {
  if (U <= 0) return 0;
  const f = (ut: number) => ut * uPlus((y * ut) / nu, L) - U;
  let lo = 0;
  let hi = U;
  while (f(hi) < 0) hi *= 2;
  for (let it = 0; it < 200; it++) {
    const mid = 0.5 * (lo + hi);
    if (f(mid) < 0) lo = mid;
    else hi = mid;
    if (hi - lo <= 1e-14 * hi) break;
  }
  return 0.5 * (lo + hi);
}

export function allYplus(U: number, yP: number, nu: number, rho: number, cp: number, Tw: number, TP: number) {
  const y = Math.max(Math.abs(yP), 1e-8);
  const utau = solveUtau(U, y, nu);
  const yplus = (y * utau) / nu;
  const tp = tPlus(yplus);
  const wVis = (6 * nu) / (LAW.beta1 * y * y);
  const wLog = utau / (Math.sqrt(LAW.betaStar) * LAW.kappa * y);
  return {
    utau,
    yplus,
    tauW: rho * utau * utau,
    qW: tp > 0 ? (rho * cp * utau * (Tw - TP)) / tp : 0,
    omegaP: Math.sqrt(wVis * wVis + wLog * wLog),
    wVis,
    wLog,
  };
}

/* ------------------------------ CHT ---------------------------------- */

export const harmonic = (kf: number, af: number, ks: number, as: number) => {
  const a = kf * af;
  const b = ks * as;
  return (a * b) / (a + b);
};

export interface Segment {
  k: number;
  L: number;
  n: number;
}
export type BC = { dirichlet: true; value: number; h: number } | { dirichlet: false; value: number; h: number };

export function conduction1D(segs: Segment[], left: BC, right: BC) {
  const k: number[] = [];
  const dx: number[] = [];
  for (const s of segs) for (let i = 0; i < s.n; i++) (k.push(s.k), dx.push(s.L / s.n));
  const N = k.length;
  const a = new Array(N).fill(0);
  const b = new Array(N).fill(0);
  const c = new Array(N).fill(0);
  const d = new Array(N).fill(0);
  for (let i = 0; i + 1 < N; i++) {
    const G = 1 / (dx[i] / (2 * k[i]) + dx[i + 1] / (2 * k[i + 1]));
    b[i] += G;
    b[i + 1] += G;
    c[i] -= G;
    a[i + 1] -= G;
  }
  const bcG = (bc: BC, kk: number, h: number) => (bc.dirichlet ? (2 * kk) / h : 1 / (h / (2 * kk) + 1 / bc.h));
  const GL = bcG(left, k[0], dx[0]);
  const GR = bcG(right, k[N - 1], dx[N - 1]);
  b[0] += GL;
  d[0] += GL * left.value;
  b[N - 1] += GR;
  d[N - 1] += GR * right.value;
  const T = thomas(a, b, c, d);
  const x: number[] = [];
  let xs = 0;
  for (let i = 0; i < N; i++) {
    x.push(xs + dx[i] / 2);
    xs += dx[i];
  }
  return { x, T, qLeft: GL * (left.value - T[0]), qRight: GR * (T[N - 1] - right.value) };
}

export function thomas(a: number[], b: number[], c: number[], d: number[]): number[] {
  const n = b.length;
  const bb = [...b];
  const dd = [...d];
  for (let i = 1; i < n; i++) {
    const m = a[i] / bb[i - 1];
    bb[i] -= m * c[i - 1];
    dd[i] -= m * dd[i - 1];
  }
  const x = new Array(n).fill(0);
  x[n - 1] = dd[n - 1] / bb[n - 1];
  for (let i = n - 2; i >= 0; i--) x[i] = (dd[i] - c[i] * x[i + 1]) / bb[i];
  return x;
}

/* ----------------------------- Porous -------------------------------- */

export function rotationZYZ(a: number, b: number, c: number): Mat3 {
  const rz = (t: number): Mat3 => [
    [Math.cos(t), -Math.sin(t), 0],
    [Math.sin(t), Math.cos(t), 0],
    [0, 0, 1],
  ];
  const ry = (t: number): Mat3 => [
    [Math.cos(t), 0, Math.sin(t)],
    [0, 1, 0],
    [-Math.sin(t), 0, Math.cos(t)],
  ];
  return mul(mul(rz(a), ry(b)), rz(c));
}

export function orientedTensor(R: Mat3, d: number[]): Mat3 {
  const RD = R.map((row) => row.map((v, j) => v * d[j]));
  return RD.map((row) => [0, 1, 2].map((j) => row[0] * R[j][0] + row[1] * R[j][1] + row[2] * R[j][2]));
}

export function ergunGradient(u: number, eps: number, dp: number, mu: number, rho: number): number {
  return (
    (150 * mu * (1 - eps) ** 2) / (eps ** 3 * dp * dp) * u +
    (1.75 * rho * (1 - eps)) / (eps ** 3 * dp) * u * u
  );
}

export function darcyForchheimerU(g: number, mu: number, rho: number, K: number, C2: number): number {
  const a = mu / K;
  const b = 0.5 * rho * C2;
  if (g <= 0) return 0;
  if (b <= 0) return g / a;
  return (-a + Math.sqrt(a * a + 4 * b * g)) / (2 * b);
}

/* ---------------------------- P1 radiation --------------------------- */

export function p1Solve(N: number, L: number, a: number, Geq: number, Gw: number): number[] {
  const dx = L / N;
  const G = 1 / (3 * a);
  const Gb = (2 * G) / (dx + 4 * G);
  const lo = new Array(N).fill(0);
  const di = new Array(N).fill(0);
  const up = new Array(N).fill(0);
  const rhs = new Array(N).fill(0);
  for (let i = 0; i < N; i++) {
    di[i] = a * dx;
    rhs[i] = a * Geq * dx;
    if (i > 0) {
      di[i] += G / dx;
      lo[i] = -G / dx;
    }
    if (i + 1 < N) {
      di[i] += G / dx;
      up[i] = -G / dx;
    }
  }
  di[0] += Gb;
  rhs[0] += Gb * Gw;
  di[N - 1] += Gb;
  rhs[N - 1] += Gb * Gw;
  return thomas(lo, di, up, rhs);
}

export function p1Analytic(x: number, L: number, a: number, Geq: number, Gw: number): number {
  const G = 1 / (3 * a);
  const lam = Math.sqrt(a / G);
  const A = (Gw - Geq) / (Math.cosh((lam * L) / 2) + 2 * G * lam * Math.sinh((lam * L) / 2));
  return Geq + A * Math.cosh(lam * (x - L / 2));
}

export const SIGMA_SB = 5.670374419e-8;
