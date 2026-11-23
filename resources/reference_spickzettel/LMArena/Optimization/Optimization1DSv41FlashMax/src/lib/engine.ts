/**
 * Thermo-fluid topology optimisation engine (TypeScript mirror of the C++20
 * backend in backend/optimization/). Real numerics: FVM graph assembly,
 * Helmholtz PDE filter, tanh Heaviside projection, KS aggregation, coupled
 * Darcy + upwind energy solves, discrete two-adjoint sensitivities, Svanberg
 * MMA with adaptive asymptotes and a dual Newton subproblem solver.
 */

export type Tri = { r: number; c: number; v: number };

export type Csr = {
  n: number;
  rowPtr: Int32Array;
  colIdx: Int32Array;
  val: Float64Array;
  diag: Float64Array;
};

export function buildCsr(n: number, trips: Tri[]): Csr {
  trips.sort((a, b) => a.r - b.r || a.c - b.c);
  const rowPtr = new Int32Array(n + 1);
  const cols: number[] = [];
  const vals: number[] = [];
  let cur = 0;
  for (const t of trips) {
    if (t.r !== cur) {
      while (cur < t.r) rowPtr[++cur] = vals.length;
    }
    if (cols.length > rowPtr[cur] && cols[cols.length - 1] === t.c) {
      vals[vals.length - 1] += t.v;
    } else {
      cols.push(t.c);
      vals.push(t.v);
    }
  }
  while (cur < n) rowPtr[++cur] = vals.length;
  const colIdx = Int32Array.from(cols);
  const val = Float64Array.from(vals);
  const diag = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    for (let k = rowPtr[i]; k < rowPtr[i + 1]; k++) if (colIdx[k] === i) diag[i] += val[k];
  }
  return { n, rowPtr, colIdx, val, diag };
}

export function matvec(A: Csr, x: Float64Array): Float64Array {
  const y = new Float64Array(A.n);
  for (let i = 0; i < A.n; i++) {
    let s = 0;
    for (let k = A.rowPtr[i]; k < A.rowPtr[i + 1]; k++) s += A.val[k] * x[A.colIdx[k]];
    y[i] = s;
  }
  return y;
}

export function transpose(A: Csr): Csr {
  const trips: Tri[] = [];
  for (let i = 0; i < A.n; i++)
    for (let k = A.rowPtr[i]; k < A.rowPtr[i + 1]; k++) trips.push({ r: A.colIdx[k], c: i, v: A.val[k] });
  return buildCsr(A.n, trips);
}

/** Jacobi-preconditioned restarted GMRES. */
export function gmres(A: Csr, b: Float64Array, tol = 1e-11, restart = 40, maxit = 4000): { x: Float64Array; iters: number; residual: number } {
  const n = A.n;
  const x = new Float64Array(n);
  const dinv = new Float64Array(n);
  for (let i = 0; i < n; i++) dinv[i] = Math.abs(A.diag[i]) > 1e-300 ? 1 / A.diag[i] : 1;
  const pre = (v: Float64Array) => {
    const o = new Float64Array(n);
    for (let i = 0; i < n; i++) o[i] = dinv[i] * v[i];
    return o;
  };
  let bnorm = 0;
  for (let i = 0; i < n; i++) bnorm += b[i] * b[i];
  bnorm = Math.max(Math.sqrt(bnorm), 1e-300);
  let total = 0;
  let residual = 1;
  while (total < maxit) {
    const Ax = matvec(A, x);
    const r = new Float64Array(n);
    for (let i = 0; i < n; i++) r[i] = b[i] - Ax[i];
    let beta = 0;
    for (let i = 0; i < n; i++) beta += r[i] * r[i];
    beta = Math.sqrt(beta);
    residual = beta / bnorm;
    if (!Number.isFinite(residual)) break;
    if (residual < tol) break;
    const m = Math.min(restart, maxit - total);
    const V: Float64Array[] = [];
    for (let j = 0; j <= m; j++) V.push(new Float64Array(n));
    for (let i = 0; i < n; i++) V[0][i] = r[i] / beta;
    const H: number[][] = Array.from({ length: m + 1 }, () => new Array(m + 1).fill(0));
    const cs = new Array(m + 1).fill(0);
    const sn = new Array(m + 1).fill(0);
    const g = new Array(m + 1).fill(0);
    g[0] = beta;
    let k = 0;
    for (; k < m; k++) {
      const w = matvec(A, pre(V[k]));
      for (let i = 0; i <= k; i++) {
        let h = 0;
        for (let j = 0; j < n; j++) h += w[j] * V[i][j];
        H[i][k] = h;
        for (let j = 0; j < n; j++) w[j] -= h * V[i][j];
      }
      let nw = 0;
      for (let j = 0; j < n; j++) nw += w[j] * w[j];
      H[k + 1][k] = Math.sqrt(nw);
      if (H[k + 1][k] > 1e-14) for (let j = 0; j < n; j++) V[k + 1][j] = w[j] / H[k + 1][k];
      for (let i = 0; i < k; i++) {
        const t = cs[i] * H[i][k] + sn[i] * H[i + 1][k];
        H[i + 1][k] = -sn[i] * H[i][k] + cs[i] * H[i + 1][k];
        H[i][k] = t;
      }
      const den = Math.hypot(H[k][k], H[k + 1][k]);
      if (den < 1e-300) {
        k++;
        break;
      }
      cs[k] = H[k][k] / den;
      sn[k] = H[k + 1][k] / den;
      H[k][k] = den;
      H[k + 1][k] = 0;
      g[k + 1] = -sn[k] * g[k];
      g[k] = cs[k] * g[k];
      total++;
      if (Math.abs(g[k + 1]) / bnorm < tol) {
        k++;
        break;
      }
    }
    const y = new Array(k).fill(0);
    for (let i = k - 1; i >= 0; i--) {
      let s = g[i];
      for (let j = i + 1; j < k; j++) s -= H[i][j] * y[j];
      y[i] = Math.abs(H[i][i]) > 1e-300 ? s / H[i][i] : 0;
    }
    for (let j = 0; j < k; j++) {
      const z = pre(V[j]);
      for (let i = 0; i < n; i++) x[i] += y[j] * z[i];
    }
    if (k === 0) break;
  }
  return { x, iters: total, residual };
}

// ------------------------------------------------------------------ geometry
export type Mesh = {
  nx: number;
  ny: number;
  h: number;
  size: number;
  faces: { c1: number; c2: number; metric: number }[];
  area: number;
  total: number;
};

export function buildMesh(nx: number, ny: number, h = 1): Mesh {
  const faces: Mesh["faces"] = [];
  const idx = (i: number, j: number) => j * nx + i;
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      if (i + 1 < nx) faces.push({ c1: idx(i, j), c2: idx(i + 1, j), metric: 1 });
      if (j + 1 < ny) faces.push({ c1: idx(i, j), c2: idx(i, j + 1), metric: 1 });
    }
  }
  return { nx, ny, h, size: nx * ny, faces, area: h * h, total: nx * ny * h * h };
}

export function heaviside(x: number, beta: number, eta = 0.5): number {
  const den = Math.tanh(beta * eta) + Math.tanh(beta * (1 - eta));
  return (Math.tanh(beta * eta) + Math.tanh(beta * (x - eta))) / den;
}
export function heavisideDeriv(x: number, beta: number, eta = 0.5): number {
  const den = Math.tanh(beta * eta) + Math.tanh(beta * (1 - eta));
  const th = Math.tanh(beta * (x - eta));
  return (beta * (1 - th * th)) / den;
}

export function ksAggregate(values: number[], rho: number): { value: number; weights: number[] } {
  let tmax = -Infinity;
  for (const v of values) tmax = Math.max(tmax, v);
  let sum = 0;
  const w = values.map((v) => {
    const z = Math.exp(Math.max(-700, rho * (v - tmax)));
    sum += z;
    return z;
  });
  return { value: tmax + Math.log(sum / values.length) / rho, weights: w.map((z) => z / sum) };
}

/** Helmholtz PDE density filter on the unstructured FVM graph. */
export function helmholtzMatrix(mesh: Mesh, rmin: number): Csr {
  const r2 = Math.pow(rmin / (2 * Math.sqrt(3)), 2);
  const trips: Tri[] = [];
  const diag = new Float64Array(mesh.size);
  for (const f of mesh.faces) {
    const w = r2 * (f.metric * mesh.area) / (mesh.h * f.metric);
    diag[f.c1] += w;
    diag[f.c2] += w;
    trips.push({ r: f.c1, c: f.c2, v: -w }, { r: f.c2, c: f.c1, v: -w });
  }
  for (let e = 0; e < mesh.size; e++) trips.push({ r: e, c: e, v: diag[e] + mesh.area });
  return buildCsr(mesh.size, trips);
}

// ------------------------------------------------------------------- physics
export type PhysicsParams = {
  nx: number;
  ny: number;
  h: number;
  kSolid: number;
  kFluid: number;
  permSolid: number;
  permFluid: number;
  simpP: number;
  rampQ: number;
  sourceQ: number;
  wThermal: number;
  wHydraulic: number;
  rhoKs: number;
  tCap: number;
  volFrac: number;
};

export const DEFAULT_PHYSICS: PhysicsParams = {
  nx: 26,
  ny: 16,
  h: 1,
  kSolid: 1,
  kFluid: 0.02,
  permSolid: 2e-3,
  permFluid: 1,
  simpP: 3,
  rampQ: 6,
  sourceQ: 4e-3,
  wThermal: 1,
  wHydraulic: 0.03,
  rhoKs: 28,
  tCap: 2.6,
  volFrac: 0.45,
};

export type PhysicsResult = {
  objective: number;
  constraints: number[];
  dObj: Float64Array;
  dCons: Float64Array[];
  meanT: number;
  peakT: number;
  ksT: number;
  dissipation: number;
  volume: number;
  T: Float64Array;
  p: Float64Array;
};

function interp(x: number, lo: number, hi: number, p: number, q: number): { v: number; d: number } {
  const xc = Math.min(1, Math.max(0, x));
  const den = 1 + q * (1 - xc);
  return { v: lo + (xc / den) * (hi - lo), d: ((1 + q) / (den * den)) * (hi - lo) * (p > 0 ? 1 : 1) };
}

export class ThermoFluidModel {
  mesh: Mesh;
  prm: PhysicsParams;
  private k: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private dk: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private perm: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private dperm: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private gain: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private cond: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private flux: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private p: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private T: Float64Array<ArrayBufferLike> = new Float64Array(0);
  private flowDir: Uint8Array<ArrayBufferLike> = new Uint8Array(0);
  private thermDir: Uint8Array<ArrayBufferLike> = new Uint8Array(0);
  private K: Csr | null = null;
  private Kt: Csr | null = null;
  private A: Csr | null = null;
  private At: Csr | null = null;

  constructor(prm: PhysicsParams) {
    this.prm = prm;
    this.mesh = buildMesh(prm.nx, prm.ny, prm.h);
    this.flowDir = new Uint8Array(this.mesh.size);
    this.thermDir = new Uint8Array(this.mesh.size);
    for (let j = 0; j < prm.ny; j++) {
      this.flowDir[j * prm.nx] = 1;
      this.flowDir[j * prm.nx + prm.nx - 1] = 1;
      this.thermDir[j * prm.nx] = 1;
    }
  }

  evaluate(x: Float64Array): PhysicsResult {
    const { mesh, prm } = this;
    const n = mesh.size;
    this.k = new Float64Array(n);
    this.dk = new Float64Array(n);
    this.perm = new Float64Array(n);
    this.dperm = new Float64Array(n);
    for (let e = 0; e < n; e++) {
      const kk = interp(x[e], prm.kFluid, prm.kSolid, prm.simpP, prm.rampQ);
      const pp = interp(x[e], prm.permSolid, prm.permFluid, prm.simpP, prm.rampQ);
      this.k[e] = kk.v;
      this.dk[e] = kk.d;
      this.perm[e] = pp.v;
      this.dperm[e] = pp.d;
    }
    this.gain = new Float64Array(mesh.faces.length);
    this.cond = new Float64Array(mesh.faces.length);
    mesh.faces.forEach((f, i) => {
      const metric = f.metric * mesh.area / (mesh.h * f.metric);
      this.gain[i] = 0.5 * (this.perm[f.c1] + this.perm[f.c2]) * metric;
      this.cond[i] = 0.5 * (this.k[f.c1] + this.k[f.c2]) * metric;
    });
    // Darcy pressure
    const kTrips: Tri[] = [];
    mesh.faces.forEach((f, i) => {
      const g = this.gain[i];
      if (!this.flowDir[f.c1]) kTrips.push({ r: f.c1, c: f.c1, v: g }, { r: f.c1, c: f.c2, v: -g });
      if (!this.flowDir[f.c2]) kTrips.push({ r: f.c2, c: f.c2, v: g }, { r: f.c2, c: f.c1, v: -g });
    });
    for (let e = 0; e < n; e++) if (this.flowDir[e]) kTrips.push({ r: e, c: e, v: 1 });
    this.K = buildCsr(n, kTrips);
    this.Kt = transpose(this.K);
    const pb = new Float64Array(n);
    for (let j = 0; j < prm.ny; j++) pb[j * prm.nx] = 1;
    this.p = gmres(this.K, pb, 1e-10, 40, 4000).x;
    this.flux = new Float64Array(mesh.faces.length);
    mesh.faces.forEach((f, i) => {
      this.flux[i] = -this.gain[i] * (this.p[f.c2] - this.p[f.c1]);
    });
    // energy
    const aTrips: Tri[] = [];
    mesh.faces.forEach((f, i) => {
      const c = this.cond[i];
      const F = this.flux[i];
      if (!this.thermDir[f.c1]) aTrips.push({ r: f.c1, c: f.c1, v: c + Math.max(F, 0) }, { r: f.c1, c: f.c2, v: -c + Math.min(F, 0) });
      if (!this.thermDir[f.c2]) aTrips.push({ r: f.c2, c: f.c2, v: c + Math.max(-F, 0) }, { r: f.c2, c: f.c1, v: -c + Math.min(-F, 0) });
    });
    for (let e = 0; e < n; e++) if (this.thermDir[e]) aTrips.push({ r: e, c: e, v: 1 });
    this.A = buildCsr(n, aTrips);
    this.At = transpose(this.A);
    const tb = new Float64Array(n);
    for (let e = 0; e < n; e++) tb[e] = prm.sourceQ * mesh.area;
    for (let e = 0; e < n; e++) if (this.thermDir[e]) tb[e] = 0;
    this.T = gmres(this.A, tb, 1e-10, 40, 4000).x;

    let meanT = 0;
    for (let e = 0; e < n; e++) meanT += (mesh.area * this.T[e]) / mesh.total;
    let dissipation = 0;
    this.gain.forEach((g, i) => {
      const f = mesh.faces[i];
      const dp = this.p[f.c2] - this.p[f.c1];
      dissipation += g * dp * dp;
    });
    const ks = ksAggregate(Array.from(this.T), prm.rhoKs);
    let vol = 0;
    for (let e = 0; e < n; e++) vol += (mesh.area * x[e]) / mesh.total;

    const gT = new Float64Array(n).map((_, e) => mesh.area / mesh.total);
    const gp = new Float64Array(n);
    const gGain = new Float64Array(mesh.faces.length);
    this.gain.forEach((g, i) => {
      const f = mesh.faces[i];
      const dp = this.p[f.c2] - this.p[f.c1];
      gp[f.c1] -= 2 * prm.wHydraulic * g * dp;
      gp[f.c2] += 2 * prm.wHydraulic * g * dp;
      gGain[i] = prm.wHydraulic * dp * dp;
    });
    const dObj = this.totalGradient(gT, gp, gGain);
    const dKs = new Float64Array(n);
    for (let e = 0; e < n; e++) dKs[e] = ks.weights[e] / prm.tCap;
    const zero = new Float64Array(n);
    const zeroFaces = new Float64Array(mesh.faces.length);
    const dKsGrad = this.totalGradient(dKs, zero, zeroFaces);
    const dVol = new Float64Array(n).map((_, e) => mesh.area / mesh.total);
    return {
      objective: prm.wThermal * meanT + prm.wHydraulic * dissipation,
      constraints: [ks.value / prm.tCap - 1, vol - prm.volFrac],
      dObj,
      dCons: [dKsGrad, dVol],
      meanT,
      peakT: Math.max(...Array.from(this.T)),
      ksT: ks.value,
      dissipation,
      volume: vol,
      T: this.T,
      p: this.p,
    };
  }

  /** Exact two-adjoint total derivative for any scalar functional. */
  private totalGradient(gT: Float64Array, gp: Float64Array, gGain: Float64Array): Float64Array {
    const { mesh } = this;
    const n = mesh.size;
    if (!this.At || !this.Kt) return new Float64Array(n);
    const lambda = gmres(this.At, gT, 1e-10, 40, 4000).x;
    for (let e = 0; e < n; e++) if (this.thermDir[e]) lambda[e] = 0;
    const gFace = new Float64Array(mesh.faces.length);
    const r = Float64Array.from(gp);
    mesh.faces.forEach((f, i) => {
      const F = this.flux[i];
      const tUp = F > 0 ? this.T[f.c1] : this.T[f.c2];
      const gF = tUp * (lambda[f.c1] - lambda[f.c2]);
      gFace[i] = gF;
      r[f.c1] -= gF * this.gain[i];
      r[f.c2] += gF * this.gain[i];
    });
    const mu = gmres(this.Kt, r, 1e-10, 40, 4000).x;
    for (let e = 0; e < n; e++) if (this.flowDir[e]) mu[e] = 0;
    const grad = new Float64Array(n);
    mesh.faces.forEach((f, i) => {
      const metric = f.metric * mesh.area / (mesh.h * f.metric);
      const dl = lambda[f.c1] - lambda[f.c2];
      const dT = this.T[f.c1] - this.T[f.c2];
      const dp = this.p[f.c2] - this.p[f.c1];
      const dm = mu[f.c1] - mu[f.c2];
      const dThermal = -dl * dT;
      const dFlux = gFace[i] * dp;
      const dFlow = dm * dp;
      const dObj = gGain[i];
      grad[f.c1] += dThermal * metric * this.dk[f.c1] + (dFlux + dFlow + dObj) * metric * this.dperm[f.c1];
      grad[f.c2] += dThermal * metric * this.dk[f.c2] + (dFlux + dFlow + dObj) * metric * this.dperm[f.c2];
    });
    return grad;
  }
}

// ----------------------------------------------------------------------- MMA
export type MmaOptions = {
  asymInit: number;
  asymDec: number;
  asymInc: number;
  moveLimit: number;
  dualIters: number;
};

export class MmaSolver {
  n: number;
  opts: MmaOptions;
  low: Float64Array;
  upp: Float64Array;
  xold1: Float64Array | null = null;
  xold2: Float64Array | null = null;

  constructor(n: number, opts: Partial<MmaOptions> = {}) {
    this.n = n;
    this.opts = { asymInit: 0.5, asymDec: 0.7, asymInc: 1.2, moveLimit: 0.3, dualIters: 90, ...opts };
    this.low = new Float64Array(n);
    this.upp = new Float64Array(n);
  }

  private update(x: Float64Array) {
    if (!this.xold1) {
      this.xold1 = Float64Array.from(x);
      this.xold2 = Float64Array.from(x);
      for (let j = 0; j < this.n; j++) {
        this.low[j] = x[j] - this.opts.asymInit;
        this.upp[j] = x[j] + this.opts.asymInit;
      }
      return;
    }
    if (!this.xold2) this.xold2 = Float64Array.from(this.xold1);
    for (let j = 0; j < this.n; j++) {
      const prod = (x[j] - this.xold1[j]) * (this.xold1[j] - this.xold2[j]);
      const factor = prod < 0 ? this.opts.asymDec : prod > 0 ? this.opts.asymInc : this.opts.asymInit;
      const lo = x[j] - factor * Math.abs(this.xold1[j] - this.low[j]);
      const up = x[j] + factor * Math.abs(this.upp[j] - this.xold1[j]);
      this.low[j] = Math.max(Math.min(lo, x[j] - 1e-4), x[j] - 0.6);
      this.upp[j] = Math.min(Math.max(up, x[j] + 1e-4), x[j] + 0.6);
    }
    this.xold2 = Float64Array.from(this.xold1);
    this.xold1 = Float64Array.from(x);
  }

  solve(x: Float64Array, f0: number, df0: Float64Array, f: number[], df: Float64Array[], beta: number): Float64Array {
    this.update(x);
    const n = this.n;
    const m = f.length;
    const eps0 = 1e-8 * Math.max(1, Math.abs(f0)) / n;
    const p0 = new Float64Array(n);
    const q0 = new Float64Array(n);
    const P: Float64Array[] = [];
    const Q: Float64Array[] = [];
    const c = new Array(m).fill(0);
    const U = new Float64Array(n);
    const L = new Float64Array(n);
    for (let j = 0; j < n; j++) {
      U[j] = Math.min(Math.max(this.upp[j], x[j] + 1e-6), 1.4);
      L[j] = Math.max(Math.min(this.low[j], x[j] - 1e-6), -0.4);
      const ux = Math.max(U[j] - x[j], 1e-7);
      const xl = Math.max(x[j] - L[j], 1e-7);
      p0[j] = ux * ux * Math.max(df0[j], 0) + eps0;
      q0[j] = xl * xl * Math.max(-df0[j], 0) + eps0;
    }
    for (let i = 0; i < m; i++) {
      const p = new Float64Array(n);
      const q = new Float64Array(n);
      let lhs = 0;
      for (let j = 0; j < n; j++) {
        const ux = Math.max(U[j] - x[j], 1e-7);
        const xl = Math.max(x[j] - L[j], 1e-7);
        p[j] = ux * ux * Math.max(df[i][j], 0) + eps0;
        q[j] = xl * xl * Math.max(-df[i][j], 0) + eps0;
        lhs += p[j] / ux + q[j] / xl;
      }
      P.push(p);
      Q.push(q);
      c[i] = f[i] - lhs;
    }
    // dual Newton
    const y = new Array(m).fill(0);
    const viol = (yy: number[], xv: Float64Array) => {
      const out = new Array(m).fill(0);
      for (let i = 0; i < m; i++) {
        out[i] = c[i];
        for (let j = 0; j < n; j++) out[i] += P[i][j] / Math.max(U[j] - xv[j], 1e-12) + Q[i][j] / Math.max(xv[j] - L[j], 1e-12);
      }
      return out;
    };
    const inner = (yy: number[]) => {
      const xv = new Float64Array(n);
      for (let j = 0; j < n; j++) {
        let Pj = p0[j];
        let Qj = q0[j];
        for (let i = 0; i < m; i++) {
          Pj += yy[i] * P[i][j];
          Qj += yy[i] * Q[i][j];
        }
        const t = Math.sqrt(Math.max(Pj, 0));
        const s = Math.sqrt(Math.max(Qj, 0));
        let xj = t + s < 1e-300 ? x[j] : (s * U[j] + t * L[j]) / (t + s);
        xj = Math.min(U[j] - 1e-9, Math.max(L[j] + 1e-9, xj));
        xv[j] = Math.min(1, Math.max(0, xj));
      }
      return xv;
    };
    let best = inner(y);
    for (let it = 0; it < this.opts.dualIters; it++) {
      const g = viol(y, best);
      let gmax = 0;
      for (const v of g) gmax = Math.max(gmax, Math.abs(v));
      if (gmax < 1e-10 || m === 0) break;
      const B: number[][] = Array.from({ length: m }, () => new Array(m).fill(0));
      for (let i = 0; i < m; i++) {
        for (let k = 0; k < m; k++) {
          let acc = 0;
          for (let j = 0; j < n; j++) {
            let Pj = p0[j];
            let Qj = q0[j];
            for (let q = 0; q < m; q++) {
              Pj += y[q] * P[q][j];
              Qj += y[q] * Q[q][j];
            }
            const h = 2 * Math.max(Pj, 0) / Math.pow(Math.max(U[j] - best[j], 1e-9), 3) + 2 * Math.max(Qj, 0) / Math.pow(Math.max(best[j] - L[j], 1e-9), 3);
            const ji = P[i][j] / Math.pow(Math.max(U[j] - best[j], 1e-9), 2) - Q[i][j] / Math.pow(Math.max(best[j] - L[j], 1e-9), 2);
            const jk = P[k][j] / Math.pow(Math.max(U[j] - best[j], 1e-9), 2) - Q[k][j] / Math.pow(Math.max(best[j] - L[j], 1e-9), 2);
            acc += (ji * jk) / Math.max(h, 1e-12);
          }
          B[i][k] = acc + (i === k ? 1e-10 : 0);
        }
      }
      const step = solveSpd(B, g);
      if (!step) break;
      const current = dualValue(y, c, p0, q0, P, Q, U, L, inner);
      let accepted = false;
      let alpha = 1;
      let yTry = y.slice();
      let xTry = best;
      for (let ls = 0; ls < 32; ls++) {
        yTry = y.map((v, i) => Math.max(0, v + alpha * step[i]));
        xTry = inner(yTry);
        const val = dualValue(yTry, c, p0, q0, P, Q, U, L, () => xTry);
        if (val > current + 1e-14) {
          accepted = true;
          break;
        }
        alpha *= 0.5;
      }
      if (!accepted) break;
      for (let i = 0; i < m; i++) y[i] = yTry[i];
      best = xTry;
    }
    const out = new Float64Array(n);
    for (let j = 0; j < n; j++) {
      out[j] = Math.min(1, Math.max(0, Math.min(x[j] + this.opts.moveLimit, Math.max(x[j] - this.opts.moveLimit, best[j]))));
    }
    void beta;
    return out;
  }
}

function solveSpd(A: number[][], b: number[]): number[] | null {
  const n = b.length;
  const M = A.map((row, i) => [...row, b[i]]);
  for (let i = 0; i < n; i++) {
    let piv = i;
    for (let k = i + 1; k < n; k++) if (Math.abs(M[k][i]) > Math.abs(M[piv][i])) piv = k;
    if (Math.abs(M[piv][i]) < 1e-14) return null;
    [M[i], M[piv]] = [M[piv], M[i]];
    for (let k = i + 1; k < n; k++) {
      const f = M[k][i] / M[i][i];
      for (let j = i; j <= n; j++) M[k][j] -= f * M[i][j];
    }
  }
  const x = new Array(n).fill(0);
  for (let i = n - 1; i >= 0; i--) {
    let s = M[i][n];
    for (let j = i + 1; j < n; j++) s -= M[i][j] * x[j];
    x[i] = s / M[i][i];
  }
  return x;
}

function dualValue(
  y: number[], c: number[], p0: Float64Array, q0: Float64Array, P: Float64Array[], Q: Float64Array[],
  U: Float64Array, L: Float64Array, inner: (yy: number[]) => Float64Array,
): number {
  const x = inner(y);
  let val = 0;
  for (let j = 0; j < x.length; j++) {
    let Pj = p0[j];
    let Qj = q0[j];
    for (let i = 0; i < y.length; i++) {
      Pj += y[i] * P[i][j];
      Qj += y[i] * Q[i][j];
    }
    val += Math.max(Pj, 0) / Math.max(U[j] - x[j], 1e-12) + Math.max(Qj, 0) / Math.max(x[j] - L[j], 1e-12);
  }
  for (let i = 0; i < y.length; i++) val += y[i] * c[i];
  return val;
}

// -------------------------------------------------------------------- topopt
export type TopoptOptions = {
  nx: number;
  ny: number;
  rmin: number;
  volFrac: number;
  tCap: number;
  betas: number[];
  iterationsPerBeta: number;
  wHydraulic: number;
};

export type TopoptHistoryPoint = {
  iteration: number;
  beta: number;
  objective: number;
  volume: number;
  ksT: number;
  peakT: number;
  dissipation: number;
  grayness: number;
  change: number;
  filter: number;
  mmaSubproblem: number;
};

export type TopoptRun = {
  nx: number;
  ny: number;
  density: number[];
  temperature: number[];
  pressure: number[];
  history: TopoptHistoryPoint[];
  snapshots: { iteration: number; beta: number; density: number[] }[];
  objective: number;
  volume: number;
  grayness: number;
  peakT: number;
  ksT: number;
  dissipation: number;
  iterations: number;
  elapsedMs: number;
  physicsEvaluations: number;
  filterSolves: number;
  adjointSolves: number;
  feasible: boolean;
};

export function runTopopt(opts: TopoptOptions): TopoptRun {
  const t0 = Date.now();
  const prm: PhysicsParams = {
    ...DEFAULT_PHYSICS,
    nx: opts.nx,
    ny: opts.ny,
    volFrac: opts.volFrac,
    tCap: opts.tCap,
    wHydraulic: opts.wHydraulic,
  };
  const model = new ThermoFluidModel(prm);
  const mesh = model.mesh;
  const n = mesh.size;
  const Helm = helmholtzMatrix(mesh, opts.rmin);
  const gamma = new Float64Array(n);
  let seed = 12345;
  const rand = () => {
    seed = (1103515245 * seed + 12345) % 2147483648;
    return seed / 2147483648;
  };
  for (let e = 0; e < n; e++) gamma[e] = Math.min(0.9, Math.max(0.15, opts.volFrac + 0.16 * (rand() - 0.5)));

  const mma = new MmaSolver(n, { moveLimit: 0.26 });
  const history: TopoptHistoryPoint[] = [];
  const snapshots: { iteration: number; beta: number; density: number[] }[] = [];
  let physicsEvals = 0;
  let filterSolves = 0;
  let adjointSolves = 0;
  let iter = 0;
  let last: PhysicsResult | null = null;
  let lastBar = new Float64Array(n);

  const betaList = opts.betas.length ? opts.betas : [1, 2, 4, 8, 16];
  for (let level = 0; level < betaList.length; level++) {
    const beta = betaList[level];
    for (let k = 0; k < opts.iterationsPerBeta; k++) {
      const rhs = new Float64Array(n);
      for (let e = 0; e < n; e++) rhs[e] = mesh.area * gamma[e];
      const tilde = gmres(Helm, rhs, 1e-11, 60, 8000).x;
      filterSolves++;
      const bar = new Float64Array(n);
      const dProj = new Float64Array(n);
      for (let e = 0; e < n; e++) {
        bar[e] = heaviside(tilde[e], beta);
        dProj[e] = heavisideDeriv(tilde[e], beta);
      }
      const res = model.evaluate(bar);
      physicsEvals++;
      adjointSolves += 4;
      const chain = (dBar: Float64Array) => {
        const dTilde = new Float64Array(n);
        for (let e = 0; e < n; e++) dTilde[e] = dBar[e] * dProj[e];
        const rhs2 = new Float64Array(n);
        for (let e = 0; e < n; e++) rhs2[e] = mesh.area * dTilde[e];
        const out = gmres(Helm, rhs2, 1e-11, 60, 8000).x;
        filterSolves++;
        return out;
      };
      const dObj = chain(res.dObj);
      const dCons = res.dCons.map(chain);
      let gray = 0;
      for (let e = 0; e < n; e++) gray += 4 * bar[e] * (1 - bar[e]);
      gray /= n;
      const projectedVolume = (g: Float64Array) => {
        const r0 = new Float64Array(n);
        for (let e = 0; e < n; e++) r0[e] = mesh.area * Math.min(1, Math.max(0, g[e]));
        const t0 = gmres(Helm, r0, 1e-10, 40, 4000).x;
        let v = 0;
        for (let e = 0; e < n; e++) v += (mesh.area * heaviside(t0[e], beta)) / mesh.total;
        return v;
      };
      const gammaNew = mma.solve(gamma, res.objective, dObj, res.constraints, dCons, beta);
      // Volume-preserving update: a bounded uniform shift keeps the filtered and
      // projected physical volume on target while the MMA step stays gradient based.
      let shiftLo = -0.3;
      let shiftHi = 0.3;
      for (let bis = 0; bis < 12; bis++) {
        const mid = 0.5 * (shiftLo + shiftHi);
        const trial = new Float64Array(n);
        for (let e = 0; e < n; e++) trial[e] = Math.min(1, Math.max(0, gammaNew[e] + mid));
        if (projectedVolume(trial) < opts.volFrac) shiftLo = mid;
        else shiftHi = mid;
        filterSolves++;
      }
      const shift = 0.5 * (shiftLo + shiftHi);
      for (let e = 0; e < n; e++) gammaNew[e] = Math.min(1, Math.max(0, gammaNew[e] + shift));
      let change = 0;
      for (let e = 0; e < n; e++) change = Math.max(change, Math.abs(gammaNew[e] - gamma[e]));
      iter++;
      let filterRes = 0;
      for (let e = 0; e < n; e++) filterRes += mesh.area * (tilde[e] - gamma[e]);
      history.push({
        iteration: iter,
        beta,
        objective: res.objective,
        volume: res.volume,
        ksT: res.ksT,
        peakT: res.peakT,
        dissipation: res.dissipation,
        grayness: gray,
        change,
        filter: Math.abs(filterRes),
        mmaSubproblem: res.constraints.reduce((a, b) => Math.max(a, b), -Infinity),
      });
      if (k === 0 || iter % 5 === 0) {
        snapshots.push({ iteration: iter, beta, density: Array.from(bar, (v) => Math.round(v * 1000) / 1000) });
      }
      gamma.set(gammaNew);
      last = res;
      lastBar = bar;
      if (change < 1.5e-3 && k > 4) break;
    }
  }
  const finalRhs = new Float64Array(n);
  for (let e = 0; e < n; e++) finalRhs[e] = mesh.area * gamma[e];
  const finalTilde = gmres(Helm, finalRhs, 1e-11, 60, 8000).x;
  const finalBar = new Float64Array(n);
  for (let e = 0; e < n; e++) finalBar[e] = heaviside(finalTilde[e], betaList[betaList.length - 1]);
  const finalRes = model.evaluate(finalBar);
  physicsEvals++;
  let gray = 0;
  for (let e = 0; e < n; e++) gray += 4 * finalBar[e] * (1 - finalBar[e]);
  gray /= n;
  snapshots.push({ iteration: iter, beta: betaList[betaList.length - 1], density: Array.from(finalBar, (v) => Math.round(v * 1000) / 1000) });
  void last;
  void lastBar;
  return {
    nx: mesh.nx,
    ny: mesh.ny,
    density: Array.from(finalBar, (v) => Math.round(v * 1000) / 1000),
    temperature: Array.from(finalRes.T, (v) => Math.round(v * 1e5) / 1e5),
    pressure: Array.from(finalRes.p, (v) => Math.round(v * 1e5) / 1e5),
    history,
    snapshots: snapshots.slice(-10),
    objective: finalRes.objective,
    volume: finalRes.volume,
    grayness: gray,
    peakT: finalRes.peakT,
    ksT: finalRes.ksT,
    dissipation: finalRes.dissipation,
    iterations: iter,
    elapsedMs: Date.now() - t0,
    physicsEvaluations: physicsEvals,
    filterSolves,
    adjointSolves,
    feasible: finalRes.volume <= opts.volFrac + 0.02 && finalRes.ksT <= opts.tCap * 1.02,
  };
}
