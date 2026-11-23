// TypeScript mirror of the Cluster A C++20 kernels (mp_turbulence_cht.hpp).
// Used by the interactive verification workbench so every plot and check
// reproduces the compiled C++ formulas exactly.

export type Mat3 = number[][];

export const C = {
  betaStar: 0.09,
  beta1: 0.075,
  beta2: 0.0828,
  sigmaK1: 0.85,
  sigmaK2: 1.0,
  sigmaW1: 0.5,
  sigmaW2: 0.856,
  a1: 0.31,
  kappa: 0.41,
  Elog: 9.8,
  Prt: 0.85,
  Cw: 0.325,
  sigmaSB: 5.670374419e-8,
};

const fmax = (a: number, b: number) => (a > b ? a : b);
const fmin = (a: number, b: number) => (a < b ? a : b);

// ---------------------------------------------------------------- wall laws
export const kaderGamma = (yp: number) => (0.01 * yp ** 4) / (1 + 5 * yp);

export function uPlus(yp: number): number {
  if (yp <= 0) return 0;
  const G = kaderGamma(yp);
  const Gs = fmax(G, 1e-300);
  return Math.exp(-G) * yp + (Math.exp(-1 / Gs) * Math.log(C.Elog * yp)) / C.kappa;
}

export function kaderBetaT(Pr: number): number {
  const t = 3.85 * Math.cbrt(Pr) - 1.3;
  return t * t + 2.12 * Math.log(Pr);
}

export function tPlus(yp: number, Pr: number): number {
  if (yp <= 0) return 0;
  const G = kaderGamma(yp);
  const Gs = fmax(G, 1e-300);
  return Pr * yp * Math.exp(-G) + (2.12 * Math.log(yp) + kaderBetaT(Pr)) * Math.exp(-1 / Gs);
}

export function omegaP(y: number, nu: number, ut: number): number {
  const yy = fmax(y, 1e-300);
  const wv = (6 * nu) / (C.beta1 * yy * yy);
  const wl = ut / (Math.sqrt(C.betaStar) * C.kappa * yy);
  return Math.sqrt(wv * wv + wl * wl);
}

export function frictionVelocity(Up: number, y: number, nu: number): number {
  if (Up <= 0) return 0;
  const f = (ut: number) => ut * uPlus((ut * y) / nu) - Up;
  let lo = 0;
  let hi = Up;
  for (let i = 0; i < 60 && f(hi) < 0; i++) hi *= 2;
  for (let i = 0; i < 200; i++) {
    const mid = 0.5 * (lo + hi);
    if (f(mid) < 0) lo = mid;
    else hi = mid;
    if (hi - lo <= 1e-15 * hi) break;
  }
  return 0.5 * (lo + hi);
}

// ------------------------------------------------------------ SST / SA / WALE
export function sstF1(k: number, w: number, rho: number, nu: number, y: number, CD: number): number {
  const sk = Math.sqrt(fmax(k, 0));
  const t1 = sk / (C.betaStar * w * y);
  const t2 = (500 * nu) / (y * y * w);
  const t3 = (4 * rho * C.sigmaW2 * fmax(k, 0)) / (CD * y * y);
  const arg = fmin(fmax(t1, t2), t3);
  return Math.tanh(arg ** 4);
}

export function sstF2(k: number, w: number, nu: number, y: number): number {
  const sk = Math.sqrt(fmax(k, 0));
  const t1 = (2 * sk) / (C.betaStar * w * y);
  const t2 = (500 * nu) / (y * y * w);
  const arg = fmin(fmax(t1, t2), 100);
  return Math.tanh(arg * arg);
}

export const sstBeta = (F1: number) => F1 * C.beta1 + (1 - F1) * C.beta2;
export const sstGamma = (F1: number) => {
  const g1 = C.beta1 / C.betaStar - (C.sigmaW1 * C.kappa * C.kappa) / Math.sqrt(C.betaStar);
  const g2 = C.beta2 / C.betaStar - (C.sigmaW2 * C.kappa * C.kappa) / Math.sqrt(C.betaStar);
  return F1 * g1 + (1 - F1) * g2;
};

export const sstNut = (k: number, w: number, S: number, F2: number) =>
  (C.a1 * k) / fmax(C.a1 * w, S * F2);

// Patankar point updates (identical to the C++ sst_update_k / sst_update_omega)
export function updateK(kOld: number, w: number, rho: number, V: number, dt: number, Pk: number) {
  const kO = fmax(kOld, 1e-30);
  const aP = V / dt + C.betaStar * rho * w * V;
  const b = (V / dt) * kO + Pk * V;
  return b / aP;
}

export function updateOmega(
  wOld: number, rho: number, V: number, dt: number, beta: number, gamma: number, S2: number,
) {
  const wO = fmax(wOld, 1e-30);
  const aP = V / dt + 2 * beta * rho * wO * V;
  const b = (V / dt) * wO + gamma * rho * S2 * V + beta * rho * wO * wO * V;
  return b / aP;
}

export function waleNuSgs(g: Mat3, delta: number): number {
  const mul = (A: Mat3, B: Mat3) =>
    [0, 1, 2].map((i) => [0, 1, 2].map((j) => A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j]));
  const g2 = mul(g, g);
  const tr = g2[0][0] + g2[1][1] + g2[2][2];
  let SdSd = 0;
  let SS = 0;
  for (let i = 0; i < 3; i++)
    for (let j = 0; j < 3; j++) {
      const sd = 0.5 * (g2[i][j] + g2[j][i]) - (i === j ? tr / 3 : 0);
      const s = 0.5 * (g[i][j] + g[j][i]);
      SdSd += sd * sd;
      SS += s * s;
    }
  const num = SdSd ** 1.5;
  const den = SS ** 2.5 + SdSd ** 1.25;
  if (den <= 1e-300) return 0;
  return (C.Cw * delta) ** 2 * (num / den);
}

// ------------------------------------------------------------------ linear solve
export function thomas(a: number[], b0: number[], c: number[], d0: number[]): number[] {
  const n = b0.length;
  const b = [...b0];
  const d = [...d0];
  for (let i = 1; i < n; i++) {
    const m = a[i] / b[i - 1];
    b[i] -= m * c[i - 1];
    d[i] -= m * d[i - 1];
  }
  const x = new Array<number>(n).fill(0);
  x[n - 1] = d[n - 1] / b[n - 1];
  for (let i = n - 2; i >= 0; i--) x[i] = (d[i] - c[i] * x[i + 1]) / b[i];
  return x;
}

// ------------------------------------------------------------------------ CHT
export type Layer = { L: number; k: number; n: number };
export type Bnd = { h: number; Tinf: number };

export const bndConductance = (h: number, dx: number, k: number) =>
  h <= 0 ? 0 : 1 / (1 / h + dx / (2 * k));

export function solveLayers(layers: Layer[], left: Bnd, right: Bnd) {
  const dx: number[] = [];
  const kk: number[] = [];
  const x: number[] = [];
  let xp = 0;
  for (const L of layers)
    for (let i = 0; i < L.n; i++) {
      const h = L.L / L.n;
      dx.push(h);
      kk.push(L.k);
      x.push(xp + h / 2);
      xp += h;
    }
  const N = dx.length;
  const a = new Array<number>(N).fill(0);
  const b = new Array<number>(N).fill(0);
  const c = new Array<number>(N).fill(0);
  const d = new Array<number>(N).fill(0);
  for (let i = 0; i + 1 < N; i++) {
    const G = 1 / (dx[i] / (2 * kk[i]) + dx[i + 1] / (2 * kk[i + 1]));
    b[i] += G;
    b[i + 1] += G;
    c[i] = -G;
    a[i + 1] = -G;
  }
  const GL = bndConductance(left.h, dx[0], kk[0]);
  const GR = bndConductance(right.h, dx[N - 1], kk[N - 1]);
  b[0] += GL;
  d[0] += GL * left.Tinf;
  b[N - 1] += GR;
  d[N - 1] += GR * right.Tinf;
  const T = thomas(a, b, c, d);
  return { x, T, q: GL * (left.Tinf - T[0]), dx };
}

export function compositeFluxAnalytic(layers: Layer[], left: Bnd, right: Bnd) {
  let R = (left.h > 0 ? 1 / left.h : 0) + (right.h > 0 ? 1 / right.h : 0);
  for (const L of layers) R += L.L / L.k;
  return (left.Tinf - right.Tinf) / R;
}

export function partitionedRobin(
  fl: Layer, sl: Layer, extL: Bnd, extR: Bnd, gamma: number, omega: number, tol: number, maxIter: number,
) {
  const dxf = fl.L / fl.n;
  const dxs = sl.L / sl.n;
  let Thf = 0.5 * (extL.Tinf + extR.Tinf);
  let Ths = Thf;
  let it = 0;
  let mismatch = Infinity;
  let q = 0;
  let Ti = 0;
  let converged = false;
  const history: number[] = [];
  for (it = 1; it <= maxIter; it++) {
    const F = solveLayers([fl], extL, { h: gamma, Tinf: Thf });
    const Tlast = F.T[F.T.length - 1];
    const qout = bndConductance(gamma, dxf, fl.k) * (Tlast - Thf);
    const Tbf = Tlast - (qout * dxf) / (2 * fl.k);

    const S = solveLayers([sl], { h: gamma, Tinf: Ths }, extR);
    const Tfirst = S.T[0];
    const qin = bndConductance(gamma, dxs, sl.k) * (Ths - Tfirst);
    const Tbs = Tfirst + (qin * dxs) / (2 * sl.k);

    mismatch = Tbf - Tbs;
    q = qin;
    Ti = 0.5 * (Tbf + Tbs);
    history.push(Math.abs(mismatch));
    if (Math.abs(mismatch) <= tol * Math.max(1, Math.abs(Tbs))) {
      converged = true;
      break;
    }
    const ThfNew = Tbs - qin / gamma;
    const ThsNew = Tbf + qout / gamma;
    Thf = (1 - omega) * Thf + omega * ThfNew;
    Ths = (1 - omega) * Ths + omega * ThsNew;
  }
  return { iterations: Math.min(it, maxIter), converged, mismatch, q, Ti, history };
}

// ------------------------------------------------------------------- porous
export const rotZ = (t: number): Mat3 => {
  const c = Math.cos(t);
  const s = Math.sin(t);
  return [[c, -s, 0], [s, c, 0], [0, 0, 1]];
};

export function orientedTensor(R: Mat3, d: number[]): Mat3 {
  const T = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (let i = 0; i < 3; i++)
    for (let j = 0; j < 3; j++)
      for (let k = 0; k < 3; k++) T[i][j] += R[i][k] * d[k] * R[j][k];
  return T;
}

export const matVec = (A: Mat3, x: number[]) =>
  [0, 1, 2].map((i) => A[i][0] * x[0] + A[i][1] * x[1] + A[i][2] * x[2]);

export function sinkTensor(D: Mat3, F: Mat3, mu: number, rho: number, u: number[]): Mat3 {
  const speed = Math.hypot(u[0], u[1], u[2]);
  return D.map((row, i) => row.map((v, j) => mu * v + 0.5 * rho * speed * F[i][j]));
}

export function forchheimerU(G: number, a: number, b: number): number {
  if (G <= 0) return 0;
  return (2 * G) / (a + Math.sqrt(a * a + 4 * b * G));
}

// ----------------------------------------------------------------- P1 radiation
export function solveP1(T: number[], L: number, a: number, Tw0: number, Tw1: number): number[] {
  const N = T.length;
  const h = L / N;
  const Gam = 1 / (3 * a);
  const cR = (4 * Gam) / h;
  const s = (t: number) => 4 * C.sigmaSB * t ** 4;
  const inv = 1 / (h * h);
  const A = new Array<number>(N).fill(0);
  const B = new Array<number>(N).fill(0);
  const Cc = new Array<number>(N).fill(0);
  const D = new Array<number>(N).fill(0);
  for (let i = 0; i < N; i++) {
    B[i] = 2 * Gam * inv + a;
    if (i > 0) A[i] = -Gam * inv;
    if (i + 1 < N) Cc[i] = -Gam * inv;
    D[i] = a * s(T[i]);
  }
  const f = (2 * Gam * inv) / (1 + cR);
  B[0] = Gam * inv + f + a;
  D[0] += f * s(Tw0);
  B[N - 1] = Gam * inv + f + a;
  D[N - 1] += f * s(Tw1);
  return thomas(A, B, Cc, D);
}

export function p1Analytic(x: number, L: number, a: number, Tg: number, Tw: number): number {
  const sg = 4 * C.sigmaSB * Tg ** 4;
  const sw = 4 * C.sigmaSB * Tw ** 4;
  const Gam = 1 / (3 * a);
  const lam = Math.sqrt(3) * a;
  const A = (sw - sg) / (Math.cosh((lam * L) / 2) + 2 * Gam * lam * Math.sinh((lam * L) / 2));
  return sg + A * Math.cosh(lam * (x - L / 2));
}

// ----------------------------------------------------------- positivity demo
export function kwHistory(method: "explicit" | "patankar", k0: number, w0: number, dt: number, S2: number, steps: number) {
  const ks = [k0];
  const ws = [w0];
  let k = k0;
  let w = w0;
  const gamma = sstGamma(1);
  const beta = sstBeta(1);
  for (let n = 0; n < steps; n++) {
    const nut = sstNut(Math.max(k, 0), Math.max(w, 0), Math.sqrt(S2), 1);
    const P = nut * S2;
    if (method === "explicit") {
      const kn = k + dt * (P - C.betaStar * w * k);
      const wn = w + dt * (gamma * S2 - beta * w * w);
      k = kn;
      w = wn;
    } else {
      const kn = updateK(k, w, 1, 1, dt, P);
      const wn = updateOmega(w, 1, 1, dt, beta, gamma, S2);
      k = kn;
      w = wn;
    }
    ks.push(k);
    ws.push(w);
  }
  return { ks, ws };
}
