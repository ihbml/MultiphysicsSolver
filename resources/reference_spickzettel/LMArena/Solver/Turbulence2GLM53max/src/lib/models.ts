// =============================================================================
//  Browser-side mirrors of the Cluster-A C++20 kernels.
//  Same formulas, same order of operations - used by the workbench so every
//  plot is computed live from the model, not hard-coded.
// =============================================================================

export const SIGMA_SB = 5.670374419e-8;

// ------------------------------------------------------------- k-omega SST
export const SST = {
  a1: 0.31, beta1: 0.075, beta2: 0.0828, betaStar: 0.09,
  sigmaK1: 0.85, sigmaK2: 1.0, sigmaW1: 0.5, sigmaW2: 0.856, kappa: 0.41,
  get gamma1() { return this.beta1 / this.betaStar - (this.sigmaW1 * this.kappa ** 2) / Math.sqrt(this.betaStar); },
  get gamma2() { return this.beta2 / this.betaStar - (this.sigmaW2 * this.kappa ** 2) / Math.sqrt(this.betaStar); },
};

export type Blend = {
  F1: number; F2: number; arg1: number; arg2: number; CD: number;
  beta: number; gamma: number; sigmaK: number; sigmaW: number; crossDiff: number;
};

export function sstBlend(k: number, w: number, y: number, nu: number, dkdw: number, rho: number): Blend {
  k = Math.max(k, 1e-30); w = Math.max(w, 1e-30); y = Math.max(y, 1e-30);
  const CD = (2 * rho * SST.sigmaW2 * dkdw) / w;
  const arg1 = Math.min(
    Math.max(Math.sqrt(k) / (SST.betaStar * w * y), (500 * nu) / (y * y * w)),
    (4 * rho * SST.sigmaW2) / (Math.max(CD, 1e-10) * y),
  );
  const F1 = Math.tanh(arg1 ** 4);
  const arg2 = Math.max((2 * Math.sqrt(k)) / (SST.betaStar * w * y), (500 * nu) / (y * y * w));
  const F2 = Math.tanh(arg2 ** 2);
  return {
    F1, F2, arg1, arg2, CD,
    beta: F1 * SST.beta1 + (1 - F1) * SST.beta2,
    gamma: F1 * SST.gamma1 + (1 - F1) * SST.gamma2,
    sigmaK: F1 * SST.sigmaK1 + (1 - F1) * SST.sigmaK2,
    sigmaW: F1 * SST.sigmaW1 + (1 - F1) * SST.sigmaW2,
    crossDiff: (2 * (1 - F1) * rho * SST.sigmaW2 * dkdw) / w,
  };
}
export const nutSST = (k: number, w: number, S: number, F2: number) =>
  (SST.a1 * Math.max(k, 0)) / Math.max(SST.a1 * Math.max(w, 1e-30), S * F2);

/** Patankar vs naive point-implicit update of k and omega. */
export function patankarStep(k: number, w: number, P: number, rho: number, dt: number, naive: boolean) {
  const destK = SST.betaStar * rho * w;
  const destW = 2 * SST.beta1 * rho * w + SST.betaStar * rho * w;
  const nut = Math.max(nutSST(k, w, 1, 1), 1e-30);
  const prodW = (SST.gamma1 * P) / nut;
  if (naive) {
    return {
      k: k + dt * (P - destK * k),
      w: w + dt * (prodW - destW * w),
      diagK: 0, diagW: 0,
    };
  }
  const diagK = destK, diagW = destW;
  return { k: (Math.max(P, 0) * 1) / diagK, w: prodW / diagW, diagK, diagW };
}

// --------------------------------------------------------- all-y+ wall laws
export const KAPPA = 0.41, ELOG = 8.43341, BW = 5.2;
export const uPlusLog = (yp: number) => Math.log(ELOG * Math.max(yp, 1e-12)) / KAPPA;
export const blendGamma = (yp: number) => Math.pow(Math.max(yp, 1e-12) / 11.63, 3);
export const uPlusBlended = (yp: number) =>
  yp * Math.exp(-blendGamma(yp)) + uPlusLog(yp) * Math.exp(-1 / Math.max(blendGamma(yp), 1e-12));
export const omegaAllYPlus = (nu: number, yP: number, uTau: number) => {
  const y = Math.max(yP, 1e-14);
  const vis = (6 * nu) / (SST.beta1 * y * y);
  const log = uTau / (Math.sqrt(SST.betaStar) * KAPPA * y);
  return { vis, log, blended: Math.sqrt(vis * vis + log * log) };
};
export const spaldingYPlus = (up: number) => {
  const ku = KAPPA * up, c = Math.exp(-KAPPA * BW);
  return up + c * (Math.exp(ku) - 1 - ku - (ku * ku) / 2 - (ku * ku * ku) / 6);
};
export function spaldingUPlus(yp: number) {
  if (yp <= 0) return 0;
  let lo = 0, hi = yp;
  for (let i = 0; i < 120; i++) {
    const mid = 0.5 * (lo + hi);
    if (spaldingYPlus(mid) < yp) lo = mid; else hi = mid;
    if (hi - lo < 1e-14 * (1 + hi)) break;
  }
  return 0.5 * (lo + hi);
}
/** Newton solve of u_P = u_tau * uPlus(rho u_tau y_P / mu) */
export function uTauFromUp(uP: number, nu: number, yP: number) {
  yP = Math.max(yP, 1e-14); uP = Math.abs(uP);
  if (uP < 1e-30) return { uTau: 0, yPlus: 0, iters: 0 };
  let u = Math.sqrt(Math.max((uP * nu) / yP, 1e-30));
  const F = (x: number) => x * uPlusBlended((x * yP) / nu) - uP;
  let it = 0;
  for (; it < 60; it++) {
    const f = F(u), h = 1e-7 * u;
    const df = (F(u + h) - F(u - h)) / (2 * h);
    if (Math.abs(df) < 1e-30) break;
    let step = f / df;
    step = Math.max(-0.45 * u, Math.min(0.45 * u, step));
    u = Math.max(u - step, 1e-14);
    if (Math.abs(f) < 1e-13 * (1 + uP)) break;
  }
  return { uTau: u, yPlus: (u * yP) / nu, iters: it + 1 };
}
export const kaderBetaPr = (Pr: number) => (3.85 * Math.cbrt(Pr) - 1.3) ** 2 + 2.12 * Math.log(Pr);
export const TplusKader = (yp: number, Pr: number, Prt = 0.85) => {
  const G = blendGamma(yp);
  const tLog = Math.log(ELOG * Math.max(yp, 1e-12)) / (KAPPA * Prt) + kaderBetaPr(Pr);
  return Pr * yp * Math.exp(-G) + tLog * Math.exp(-1 / Math.max(G, 1e-12));
};
export const uTauReichardt = (yp: number) =>
  (1 / KAPPA) * Math.log(1 + KAPPA * yp) + BW * (1 - Math.exp(-yp / 3) - (yp / 3) * Math.exp(-yp / 3));

// --------------------------------------------------------------- WALE model
export function waleNuSgs(g: number[], delta: number, Cw = 0.325) {
  const G = mm(g, g), Gt = transpose(G);
  const Sd = sub(scale(add(G, Gt), 0.5), scale(ident(), trace3(G) / 3));
  const S = sym(g);
  const SdSd = ddot(Sd, Sd), SS = ddot(S, S);
  if (SS ** 1.25 + SdSd ** 1.25 < 1e-30) return 0;
  return (Cw * delta) ** 2 * (SdSd ** 1.5) / (SS ** 2.5 + SdSd ** 1.25);
}
const ident = () => [1, 0, 0, 0, 1, 0, 0, 0, 1];
const transpose = (a: number[]) => [a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]];
const sym = (a: number[]) => [(a[0] + a[0]) / 2, (a[1] + a[3]) / 2, (a[2] + a[6]) / 2, (a[3] + a[1]) / 2, (a[4] + a[4]) / 2, (a[5] + a[7]) / 2, (a[6] + a[2]) / 2, (a[7] + a[5]) / 2, (a[8] + a[8]) / 2];
const trace3 = (a: number[]) => a[0] + a[4] + a[8];
const ddot = (a: number[], b: number[]) => a.reduce((s, v, i) => s + v * b[i], 0);
const add = (a: number[], b: number[]) => a.map((v, i) => v + b[i]);
const sub = (a: number[], b: number[]) => a.map((v, i) => v - b[i]);
const scale = (a: number[], s: number) => a.map((v) => v * s);
function mm(a: number[], b: number[]) {
  const r = new Array(9).fill(0);
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
    let s = 0; for (let k = 0; k < 3; k++) s += a[3 * i + k] * b[3 * k + j]; r[3 * i + j] = s;
  }
  return r;
}

// ----------------------------------------------------------- CHT (1-D, FVM)
export type Layer = { name: string; L: number; k: number; cells: number };

export function seriesResistance(layers: Layer[], hHot: number, hCold: number) {
  let R = 1 / hHot + 1 / hCold;
  for (const l of layers) R += l.L / l.k;
  return R;
}

/** Monolithic 1-D multi-region solve: one unified tridiagonal system. */
export function solveCHT(layers: Layer[], hHot: number, hCold: number, Th: number, Tc: number) {
  type Node = { x: number; k: number; region: number; name: string };
  const nodes: Node[] = [];
  let x = 0;
  layers.forEach((l, i) => {
    for (let c = 0; c < l.cells; c++) {
      const x0 = x + (c * l.L) / l.cells, x1 = x + ((c + 1) * l.L) / l.cells;
      nodes.push({ x: 0.5 * (x0 + x1), k: l.k, region: i, name: l.name });
    }
    x += l.L;
  });
  const n = nodes.length;
  const lower = new Array(n).fill(0), diag = new Array(n).fill(0), rhs = new Array(n).fill(0);
  const G = (i: number, j: number) => {
    if (j < 0 || j >= n) return 0;
    return (0.5 * (nodes[i].k + nodes[j].k) * 1) / Math.abs(nodes[j].x - nodes[i].x);
  };
  for (let i = 0; i < n; i++) {
    if (i > 0) { lower[i] = -G(i, i - 1); diag[i] += G(i, i - 1); }
    if (i < n - 1) diag[i] += G(i, i + 1);
  }
  // Monolithic CHT coupling between regions.  The harmonic-resistance
  // coefficient C = Gf Gs/(Gf+Gs) is inserted straight into the same matrix:
  //   a_PP += C ,  a_PS^CHT = -C
  // which is why steady CHT converges in ONE linear solve.
  for (let i = 0; i < n - 1; i++) {
    if (nodes[i].region !== nodes[i + 1].region) {
      const Gf = nodes[i].k / Math.max(nodes[i].x - 0, 1e-12);
      const Gs = nodes[i + 1].k / Math.max(layers[layers.length - 1].L - nodes[i + 1].x, 1e-12);
      const C = (Gf * Gs) / (Gf + Gs);
      diag[i] += C; diag[i + 1] += C; lower[i + 1] -= C;
    }
  }
  // convective films (Robin) on the two outer faces
  diag[0] += hHot; diag[n - 1] += hCold;
  rhs[0] += hHot * Th; rhs[n - 1] += hCold * Tc;
  // Thomas algorithm
  const T = new Array(n).fill(0);
  {
    const cp = new Array(n).fill(0), dp = new Array(n).fill(0);
    cp[0] = -G(0, 1) / diag[0]; dp[0] = rhs[0] / diag[0];
    for (let i = 1; i < n; i++) {
      const m = diag[i] + lower[i] * cp[i - 1];
      cp[i] = (i < n - 1 ? -G(i, i + 1) : 0) / m;
      dp[i] = (rhs[i] - lower[i] * dp[i - 1]) / m;
    }
    T[n - 1] = dp[n - 1];
    for (let i = n - 2; i >= 0; i--) T[i] = dp[i] - cp[i] * T[i + 1];
  }
  const q = (Th - T[0]) * hHot;
  return { nodes, T, q, R: seriesResistance(layers, hHot, hCold) };
}

/** Partitioned Robin-Robin outer iteration history (fluid region = layer 0). */
export function robinRobin(layers: Layer[], hHot: number, hCold: number, Th: number, Tc: number, alpha: number) {
  // lumped 2-region model: G = k*A/L per region (matches the FVM chain exactly)
  const Gf = layers[0].k * layers[0].cells / layers[0].L;    // total fluid conductance
  const Gs = layers.slice(1).reduce((s, l) => s + (l.k * l.cells) / l.L, 0);
  const qExact = (Th - Tc) / seriesResistance(layers, hHot, hCold);
  let Tf = Th, Ts = Tc, Tint = 0.5 * (Th + Tc);
  const hist: { it: number; err: number }[] = [];
  for (let it = 1; it <= 4000; it++) {
    // fluid region: film + conduction chain, Robin condition at the interface
    const qf = (Th - Tint) / (1 / hHot + 1 / Gf);
    Tf = Th - qf / hHot;
    const qs = (Tint - Tc) / (1 / Gs + 1 / hCold);
    Ts = Tc + qs / hCold;
    const Tnew = (Gf * Tf + Gs * Ts) / (Gf + Gs);
    const err = Math.abs(Tnew - Tint);
    Tint += alpha * (Tnew - Tint);
    hist.push({ it, err: err / Math.max(Math.abs(Th - Tc), 1e-12) });
    if (err < 1e-11) break;
  }
  const q = (Th - Tint) / (1 / hHot + 1 / Gf);
  return { hist, q, qExact, errQ: Math.abs(q - qExact) / qExact, iterations: hist.length };
}

// ------------------------------------------------ anisotropic porous media
export type PorousInput = { d1: number; d2: number; d3: number; f1: number; f2: number; f3: number; theta: number; mu: number; rho: number; u: number };
export function porousTensors(p: PorousInput) {
  const t = (p.theta * Math.PI) / 180, c = Math.cos(t), s = Math.sin(t);
  const R = [c, -s, 0, s, c, 0, 0, 0, 1];
  const Dl = [p.d1, 0, 0, 0, p.d2, 0, 0, 0, p.d3];
  const Fl = [p.f1, 0, 0, 0, p.f2, 0, 0, 0, p.f3];
  return { R, D: mm(R, mm(Dl, transpose(R))), F: mm(R, mm(Fl, transpose(R))) };
}
export function porousSolve(p: PorousInput, dpdx: number) {
  const { D, F } = porousTensors(p);
  let u = [0.01, 0.005, 0.0];
  for (let it = 0; it < 400; it++) {
    const spd = Math.hypot(...u);
    const A = new Array(9).fill(0);
    for (let i = 0; i < 9; i++) A[i] = D[i] * p.mu + F[i] * 0.5 * p.rho * spd;
    // 3x3 solve by Cramer
    const rhs = [-dpdx, 0, 0];
    const det =
      A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
    if (Math.abs(det) < 1e-30) break;
    const un = [0, 0, 0];
    for (let col = 0; col < 3; col++) {
      const M = A.slice();
      M[col] = rhs[0]; M[3 + col] = rhs[1]; M[6 + col] = rhs[2];
      un[col] =
        (M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) + M[2] * (M[3] * M[7] - M[4] * M[6])) / det;
    }
    u = un;
  }
  return { u, D, F };
}
export const ergun = (Dp: number, eps: number) => ({
  d: (150 * (1 - eps) ** 2) / (eps ** 3 * Dp ** 2),
  f: (3.5 * (1 - eps)) / (eps ** 3 * Dp),
});

// ------------------------------------------------------------ radiation P1
export function expIntegral(n: number, x: number) {
  // 32-point Gauss-Legendre on [0,1]
  const t = [-0.0950125, -0.28160355, -0.45801678, -0.61787624, -0.75540441, -0.8656312, -0.94457502, -0.98940093,
             0.0950125, 0.28160355, 0.45801678, 0.61787624, 0.75540441, 0.8656312, 0.94457502, 0.98940093];
  const w = [0.18945061, 0.18260342, 0.16915652, 0.14959599, 0.12462897, 0.09515851, 0.06225352, 0.02715246,
             0.18945061, 0.18260342, 0.16915652, 0.14959599, 0.12462897, 0.09515851, 0.06225352, 0.02715246];
  let s = 0;
  const p = n - 2;
  for (let i = 0; i < 16; i++) {
    const mu = 0.5 * (t[i] + 1);
    s += 0.5 * w[i] * Math.pow(mu, p) * Math.exp(-x / Math.max(mu, 1e-12));
  }
  return s;
}
export const slabExactG = (tau: number, tau0: number, Tm: number) =>
  4 * SIGMA_SB * Tm ** 4 * (1 - 0.5 * (expIntegral(2, tau) + expIntegral(2, tau0 - tau)));
export const slabExactQ = (tau: number, tau0: number, Tm: number) =>
  2 * SIGMA_SB * Tm ** 4 * (expIntegral(3, tau0 - tau) - expIntegral(3, tau));

/** Closed-form P1 solution of the isothermal slab with Marshak boundaries. */
export function p1Slab(tau0: number, Tm: number, epsW = 1) {
  const Cm = epsW / (2 * (2 - epsW));
  const kap = Math.sqrt(3);
  const s = Math.sinh(kap * tau0), c = Math.cosh(kap * tau0);
  const den = kap * s + 3 * Cm * c + 3 * Cm * c + (9 * Cm * Cm * s) / kap;
  const num = -3 * Cm * (1 + c + (3 * Cm * s) / kap);
  const A = num / den;
  const B = (3 * Cm * (1 + A)) / kap;
  return (tau: number) => 4 * SIGMA_SB * Tm ** 4 * (1 + A * Math.cosh(kap * tau) + B * Math.sinh(kap * tau));
}

// --------------------------------------------------------------- utilities
export const logspace = (a: number, b: number, n: number) =>
  Array.from({ length: n }, (_, i) => 10 ** (a + ((b - a) * i) / (n - 1)));
export const linspace = (a: number, b: number, n: number) =>
  Array.from({ length: n }, (_, i) => a + ((b - a) * i) / (n - 1));
export const fmt = (v: number, d = 3) => {
  if (!isFinite(v)) return "—";
  const a = Math.abs(v);
  if (a !== 0 && (a < 1e-3 || a >= 1e5)) return v.toExponential(Math.min(d, 3));
  return v.toFixed(d);
};
