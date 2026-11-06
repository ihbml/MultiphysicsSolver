/**
 * Eulerian SPH particle generator.
 *
 * Implements the pipeline of Wang, Haidn & Hu (CPC 2025 / arXiv:2312.03542):
 *   1. lattice seeding inside a level set,
 *   2. physics-driven particle relaxation with level-set "surface bounding"
 *      until  sum_j V_j grad W_ij = 0  (zero-order consistency),
 *   3. detection of near-boundary particles through the divergence of the
 *      position field,  div(r)_i = -sum_j V_j r_ij dW/dr  ~ d  in the bulk,
 *      flagged when  div(r)_i < gamma = 0.75 d,
 *   4. reconstruction of the missing boundary surface
 *      A_iS = -sum_p A_ip ,  A_ij = 2 V_i V_j (dW_ij/dr_ij) e_ij
 *      which is the SPH analogue of the FVM face area vector S_ij n_ij.
 */
import { Geometry, PATCH_ORDER, PatchKind, projectToIso, sdfGrad } from "./sdf";

export type KernelId = "wendland" | "cubic" | "quintic";

export interface Kernel {
  id: KernelId;
  name: string;
  /** support radius in units of h */
  kappa: number;
  /** normalisation in 2D */
  alpha(h: number): number;
  w(q: number): number;
  dw(q: number): number;
}

export const KERNELS: Record<KernelId, Kernel> = {
  wendland: {
    id: "wendland",
    name: "Wendland C2 (2h)",
    kappa: 2,
    alpha: (h) => 7 / (4 * Math.PI * h * h),
    w: (q) => (q >= 2 ? 0 : (1 - q / 2) ** 4 * (2 * q + 1)),
    dw: (q) => (q >= 2 ? 0 : -5 * q * (1 - q / 2) ** 3),
  },
  cubic: {
    id: "cubic",
    name: "Cubic B-spline (2h)",
    kappa: 2,
    alpha: (h) => 10 / (7 * Math.PI * h * h),
    w: (q) =>
      q >= 2 ? 0 : q < 1 ? 1 - 1.5 * q * q + 0.75 * q ** 3 : 0.25 * (2 - q) ** 3,
    dw: (q) =>
      q >= 2 ? 0 : q < 1 ? -3 * q + 2.25 * q * q : -0.75 * (2 - q) ** 2,
  },
  quintic: {
    id: "quintic",
    name: "Quintic spline (3h)",
    kappa: 3,
    alpha: (h) => 7 / (478 * Math.PI * h * h),
    w: (q) => {
      if (q >= 3) return 0;
      let v = (3 - q) ** 5;
      if (q < 2) v -= 6 * (2 - q) ** 5;
      if (q < 1) v += 15 * (1 - q) ** 5;
      return v;
    },
    dw: (q) => {
      if (q >= 3) return 0;
      let v = -5 * (3 - q) ** 4;
      if (q < 2) v += 30 * (2 - q) ** 4;
      if (q < 1) v -= 75 * (1 - q) ** 4;
      return v;
    },
  },
};

export interface SphParams {
  dp: number;
  hFactor: number;
  iters: number;
  kernel: KernelId;
  refineRatio: number; // 1 = uniform, 2/4 = adaptive levels near the feature
  grading: number;
  volumeReinit: boolean;
  lattice: "square" | "hex";
}

export interface SphDiagnostics {
  n: number;
  nBoundary: number;
  eps0Mean: number;
  eps0Max: number;
  eps0Bulk: number;
  eps1Mean: number;
  divRMean: number;
  neighMean: number;
  neighMin: number;
  surfaceSum: number;
  exactPerimeter: number;
  patchCount: Record<string, number>;
  maxShift: number;
  ghostPairs: number;
  pairReduction: number;
}

export interface SphData {
  n: number;
  x: Float64Array;
  y: Float64Array;
  V: Float64Array;
  h: Float64Array;
  level: Uint8Array;
  divR: Float32Array;
  eps0: Float32Array;
  eps1: Float32Array;
  nNeigh: Int32Array;
  isBnd: Uint8Array;
  asMag: Float32Array; // |A_iS|
  asNx: Float32Array;
  asNy: Float32Array;
  patch: Int8Array;
  diag: SphDiagnostics;
}

/* ------------------------------------------------------------------ */
/* uniform background grid for neighbour search                        */
/* ------------------------------------------------------------------ */

export class NeighborGrid {
  cell: number;
  nx: number;
  ny: number;
  x0: number;
  y0: number;
  head: Int32Array;
  next: Int32Array;

  constructor(
    x: Float64Array,
    y: Float64Array,
    n: number,
    cell: number,
    bbox: [number, number, number, number],
  ) {
    const pad = 4 * cell;
    this.cell = cell;
    this.x0 = bbox[0] - pad;
    this.y0 = bbox[1] - pad;
    this.nx = Math.max(1, Math.ceil((bbox[2] - bbox[0] + 2 * pad) / cell));
    this.ny = Math.max(1, Math.ceil((bbox[3] - bbox[1] + 2 * pad) / cell));
    this.head = new Int32Array(this.nx * this.ny).fill(-1);
    this.next = new Int32Array(n).fill(-1);
    for (let i = 0; i < n; i++) {
      const cx = Math.min(
        this.nx - 1,
        Math.max(0, Math.floor((x[i] - this.x0) / cell)),
      );
      const cy = Math.min(
        this.ny - 1,
        Math.max(0, Math.floor((y[i] - this.y0) / cell)),
      );
      const c = cy * this.nx + cx;
      this.next[i] = this.head[c];
      this.head[c] = i;
    }
  }

  forEach(px: number, py: number, radius: number, cb: (j: number) => void) {
    const r = Math.ceil(radius / this.cell);
    const cx = Math.floor((px - this.x0) / this.cell);
    const cy = Math.floor((py - this.y0) / this.cell);
    for (let j = cy - r; j <= cy + r; j++) {
      if (j < 0 || j >= this.ny) continue;
      for (let i = cx - r; i <= cx + r; i++) {
        if (i < 0 || i >= this.nx) continue;
        let p = this.head[j * this.nx + i];
        while (p !== -1) {
          cb(p);
          p = this.next[p];
        }
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* particle job: seeding + relaxation                                  */
/* ------------------------------------------------------------------ */

export function targetSpacing(
  geo: Geometry,
  p: SphParams,
  x: number,
  y: number,
): number {
  const dpMin = p.dp / p.refineRatio;
  if (p.refineRatio <= 1) return p.dp;
  const d = Math.abs(geo.refineDist(x, y));
  return Math.min(p.dp, dpMin * (1 + p.grading * (d / dpMin)));
}

export class SphJob {
  geo: Geometry;
  p: SphParams;
  kern: Kernel;
  x: Float64Array;
  y: Float64Array;
  V: Float64Array;
  h: Float64Array;
  dp: Float64Array;
  level: Uint8Array;
  iter = 0;
  maxShift = 1;
  history: { it: number; eps: number }[] = [];

  constructor(geo: Geometry, p: SphParams, mask?: (x: number, y: number) => boolean) {
    this.geo = geo;
    this.p = p;
    this.kern = KERNELS[p.kernel];
    const xs: number[] = [];
    const ys: number[] = [];
    const ds: number[] = [];
    const lv: number[] = [];
    const maxLevel = Math.max(0, Math.round(Math.log2(p.refineRatio)));
    const [bx0, by0, bx1, by1] = geo.bbox;
    for (let L = 0; L <= maxLevel; L++) {
      const s = p.dp / 2 ** L;
      const rowH = p.lattice === "hex" ? s * (Math.sqrt(3) / 2) : s;
      const nRows = Math.ceil((by1 - by0) / rowH);
      const nCols = Math.ceil((bx1 - bx0) / s);
      for (let j = 0; j <= nRows; j++) {
        const yy = by0 + (j + 0.5) * rowH;
        if (yy > by1) continue;
        for (let i = 0; i <= nCols; i++) {
          const xx =
            bx0 + (i + 0.5) * s + (p.lattice === "hex" && j % 2 ? s / 2 : 0);
          if (xx > bx1) continue;
          if (geo.sdf(xx, yy) > -0.5 * s) continue;
          if (mask && !mask(xx, yy)) continue;
          const t = targetSpacing(geo, p, xx, yy);
          const lvl = Math.min(
            maxLevel,
            Math.max(0, Math.round(Math.log2(p.dp / t))),
          );
          if (lvl !== L) continue;
          xs.push(xx);
          ys.push(yy);
          ds.push(s);
          lv.push(L);
        }
      }
    }
    // multi-level seeding overlaps at the level transitions: drop the clumps
    if (maxLevel > 0) {
      const keep = new Uint8Array(xs.length).fill(1);
      const cell = p.dp;
      const map = new Map<string, number[]>();
      const key = (a: number, b: number) => `${a},${b}`;
      for (let i = 0; i < xs.length; i++) {
        const ci = Math.floor(xs[i] / cell);
        const cj = Math.floor(ys[i] / cell);
        let clash = false;
        for (let a = ci - 1; a <= ci + 1 && !clash; a++)
          for (let b = cj - 1; b <= cj + 1 && !clash; b++) {
            const list = map.get(key(a, b));
            if (!list) continue;
            for (const j of list) {
              if (!keep[j]) continue;
              const lim = 0.62 * Math.min(ds[i], ds[j]);
              if (Math.hypot(xs[i] - xs[j], ys[i] - ys[j]) < lim) {
                clash = true;
                break;
              }
            }
          }
        if (clash) keep[i] = 0;
        else {
          const k = key(ci, cj);
          const list = map.get(k);
          if (list) list.push(i);
          else map.set(k, [i]);
        }
      }
      const fx: number[] = [];
      const fy: number[] = [];
      const fd: number[] = [];
      const fl: number[] = [];
      for (let i = 0; i < xs.length; i++)
        if (keep[i]) {
          fx.push(xs[i]);
          fy.push(ys[i]);
          fd.push(ds[i]);
          fl.push(lv[i]);
        }
      xs.length = 0;
      xs.push(...fx);
      ys.length = 0;
      ys.push(...fy);
      ds.length = 0;
      ds.push(...fd);
      lv.length = 0;
      lv.push(...fl);
    }
    this.x = Float64Array.from(xs);
    this.y = Float64Array.from(ys);
    this.dp = Float64Array.from(ds);
    this.level = Uint8Array.from(lv);
    this.V = Float64Array.from(ds, (s) =>
      p.lattice === "hex" ? s * s * (Math.sqrt(3) / 2) : s * s,
    );
    this.h = Float64Array.from(ds, (s) => p.hFactor * s);
  }

  get n(): number {
    return this.x.length;
  }

  private maxSupport(): number {
    let m = 0;
    for (let i = 0; i < this.n; i++) m = Math.max(m, this.h[i]);
    return m * this.kern.kappa;
  }

  grid(): NeighborGrid {
    return new NeighborGrid(
      this.x,
      this.y,
      this.n,
      Math.max(this.maxSupport(), 1e-9),
      this.geo.bbox,
    );
  }

  /** one relaxation iteration (displacement driven by the kernel gradient sum) */
  step(): boolean {
    const { x, y, V, h, geo, kern } = this;
    const n = this.n;
    const g = this.grid();
    const sx = new Float64Array(n);
    const sy = new Float64Array(n);
    const res = new Float64Array(n);
    const sup = this.maxSupport();
    let epsSum = 0;
    let epsCnt = 0;
    for (let i = 0; i < n; i++) {
      let ax = 0;
      let ay = 0;
      g.forEach(x[i], y[i], sup, (j) => {
        if (j === i) return;
        const dx = x[i] - x[j];
        const dy = y[i] - y[j];
        const r = Math.hypot(dx, dy);
        const hij = 0.5 * (h[i] + h[j]);
        if (r >= kern.kappa * hij || r < 1e-12) return;
        const dW = (kern.alpha(hij) / hij) * kern.dw(r / hij);
        ax += V[j] * dW * (dx / r);
        ay += V[j] * dW * (dy / r);
      });
      res[i] = Math.hypot(ax, ay) * h[i];
      // displacement ~ -k h^2 sum_j V_j grad W  (clamped for stability)
      let ddx = -0.2 * h[i] * h[i] * ax;
      let ddy = -0.2 * h[i] * h[i] * ay;
      const mv = Math.hypot(ddx, ddy);
      const cap = 0.15 * this.dp[i];
      if (mv > cap) {
        ddx *= cap / mv;
        ddy *= cap / mv;
      }
      sx[i] = ddx;
      sy[i] = ddy;
    }
    let maxShift = 0;
    const eps = 1e-4 * this.p.dp;
    for (let i = 0; i < n; i++) {
      x[i] += sx[i];
      y[i] += sy[i];
      // level-set surface bounding: keep the outermost layer at dp/2
      const d = geo.sdf(x[i], y[i]);
      const target = -0.5 * this.dp[i];
      if (d > target) {
        const [gx, gy] = sdfGrad(geo, x[i], y[i], eps);
        const g2 = gx * gx + gy * gy || 1e-30;
        x[i] -= ((d - target) * gx) / g2;
        y[i] -= ((d - target) * gy) / g2;
      }
      // smooth adaptive resolution: follow the target spacing field
      if (this.p.refineRatio > 1) {
        const t = targetSpacing(geo, this.p, x[i], y[i]);
        this.dp[i] = t;
        h[i] = this.p.hFactor * t;
        this.level[i] = Math.max(
          0,
          Math.min(255, Math.round(Math.log2(this.p.dp / t))),
        );
      }
      // convergence is measured on particles with a complete kernel support
      if (-d > kern.kappa * h[i]) {
        epsSum += res[i];
        epsCnt++;
      }
      maxShift = Math.max(maxShift, Math.hypot(sx[i], sy[i]) / this.dp[i]);
    }
    this.maxShift = maxShift;
    this.iter++;
    this.history.push({ it: this.iter, eps: epsSum / Math.max(epsCnt, 1) });
    if (this.p.volumeReinit && this.iter % 5 === 0) this.reinitVolumes();
    return this.iter >= this.p.iters;
  }

  /** V_i = 1 / sum_j W_ij  (number-density volume, bulk particles only) */
  reinitVolumes() {
    const { x, y, h, kern } = this;
    const n = this.n;
    const g = this.grid();
    const sup = this.maxSupport();
    const sigma = new Float64Array(n);
    for (let i = 0; i < n; i++) {
      let s = 0;
      g.forEach(x[i], y[i], sup, (j) => {
        const dx = x[i] - x[j];
        const dy = y[i] - y[j];
        const r = Math.hypot(dx, dy);
        const hij = 0.5 * (h[i] + h[j]);
        if (r >= kern.kappa * hij) return;
        s += kern.alpha(hij) * kern.w(r / hij);
      });
      sigma[i] = s;
    }
    for (let i = 0; i < n; i++) {
      const v0 = this.p.lattice === "hex"
        ? this.dp[i] * this.dp[i] * (Math.sqrt(3) / 2)
        : this.dp[i] * this.dp[i];
      const v = 1 / Math.max(sigma[i], 1e-30);
      // only trust the summation in the bulk (truncated support near walls)
      this.V[i] = Math.min(Math.max(v, 0.6 * v0), 1.4 * v0);
    }
  }

  /** full diagnostics + FVM-equivalent boundary surface reconstruction */
  finalize(): SphData {
    const { x, y, V, h, geo, kern } = this;
    const n = this.n;
    const g = this.grid();
    const sup = this.maxSupport();
    const divR = new Float32Array(n);
    const eps0 = new Float32Array(n);
    const eps1 = new Float32Array(n);
    const nNeigh = new Int32Array(n);
    const isBnd = new Uint8Array(n);
    const asMag = new Float32Array(n);
    const asNx = new Float32Array(n);
    const asNy = new Float32Array(n);
    const patch = new Int8Array(n).fill(-1);
    const gamma = 0.75 * 2; // 0.75 * d, d = 2 in 2D
    const deps = 1e-4 * this.p.dp;

    let eps0Sum = 0;
    let eps0Max = 0;
    let eps0Bulk = 0;
    let bulkCnt = 0;
    let eps1Sum = 0;
    let divSum = 0;
    let neighSum = 0;
    let neighMin = 1e9;
    let surfaceSum = 0;
    let nb = 0;
    let sumBndNeigh = 0;
    const patchCount: Record<string, number> = {};

    for (let i = 0; i < n; i++) {
      let gx = 0;
      let gy = 0;
      let dr = 0;
      let cnt = 0;
      // first-order correction matrix B_i = (sum V_j (r_j-r_i) x gradW)^-1
      let m00 = 0;
      let m01 = 0;
      let m10 = 0;
      let m11 = 0;
      g.forEach(x[i], y[i], sup, (j) => {
        if (j === i) return;
        const dx = x[i] - x[j];
        const dy = y[i] - y[j];
        const r = Math.hypot(dx, dy);
        const hij = 0.5 * (h[i] + h[j]);
        if (r >= kern.kappa * hij || r < 1e-12) return;
        const dW = (kern.alpha(hij) / hij) * kern.dw(r / hij);
        const ex = dx / r;
        const ey = dy / r;
        gx += V[j] * dW * ex;
        gy += V[j] * dW * ey;
        dr += -V[j] * r * dW;
        m00 += -V[j] * dx * dW * ex;
        m01 += -V[j] * dx * dW * ey;
        m10 += -V[j] * dy * dW * ex;
        m11 += -V[j] * dy * dW * ey;
        cnt++;
      });
      divR[i] = dr;
      nNeigh[i] = cnt;
      eps0[i] = Math.hypot(gx, gy) * h[i];
      // ||M - I||_F with M = sum_j V_j (r_j - r_i) (x) grad W_ij
      eps1[i] = Math.hypot(m00 - 1, m01, m10, m11 - 1);
      eps0Sum += eps0[i];
      eps0Max = Math.max(eps0Max, eps0[i]);
      eps1Sum += eps1[i];
      divSum += dr;
      neighSum += cnt;
      neighMin = Math.min(neighMin, cnt);
      if (dr < gamma) {
        isBnd[i] = 1;
        nb++;
        sumBndNeigh += cnt;
        // A_iS = -sum_p A_ip,  A_ip = 2 V_i V_p dW/dr e_ip
        const ax = -2 * V[i] * gx;
        const ay = -2 * V[i] * gy;
        const mag = Math.hypot(ax, ay);
        asMag[i] = mag;
        asNx[i] = mag > 1e-30 ? ax / mag : 0;
        asNy[i] = mag > 1e-30 ? ay / mag : 0;
        surfaceSum += mag;
        const [sxp, syp] = projectToIso(geo, x[i], y[i], 0, deps, 2);
        const k: PatchKind = geo.patchAt(sxp, syp);
        patch[i] = PATCH_ORDER.indexOf(k);
        patchCount[k] = (patchCount[k] ?? 0) + 1;
      } else {
        eps0Bulk += eps0[i];
        bulkCnt++;
      }
    }

    // interactions that the *original* algorithm would need with ghost /
    // contact particles, and that the present boundary algorithm removes
    const bulkNeigh =
      bulkCnt > 0 ? (neighSum - sumBndNeigh) / Math.max(bulkCnt, 1) : 0;
    let ghostPairs = 0;
    for (let i = 0; i < n; i++)
      if (isBnd[i]) ghostPairs += Math.max(0, bulkNeigh - nNeigh[i]);

    return {
      n,
      x: this.x,
      y: this.y,
      V: this.V,
      h: this.h,
      level: this.level,
      divR,
      eps0,
      eps1,
      nNeigh,
      isBnd,
      asMag,
      asNx,
      asNy,
      patch,
      diag: {
        n,
        nBoundary: nb,
        eps0Mean: eps0Sum / Math.max(n, 1),
        eps0Max,
        eps0Bulk: eps0Bulk / Math.max(bulkCnt, 1),
        eps1Mean: eps1Sum / Math.max(n, 1),
        divRMean: divSum / Math.max(n, 1),
        neighMean: neighSum / Math.max(n, 1),
        neighMin: neighMin === 1e9 ? 0 : neighMin,
        surfaceSum,
        exactPerimeter: geo.perimeter,
        patchCount,
        maxShift: this.maxShift,
        ghostPairs,
        pairReduction: (100 * ghostPairs) / Math.max(neighSum, 1),
      },
    };
  }
}
