/**
 * SPH <-> FVM coupling layer, following the adaptive SPH-FVM model of
 * Zhang, Ming, Wang & Liang (Int. J. Multiphase Flow 189, 2025):
 *
 *   - a Lagrangian (Riemann-SPH) *core* region that is adaptively re-sized
 *     and re-positioned around the region of interest (bubble / body / shock),
 *   - a mesh (FVM) region covering the rest of the domain, which carries the
 *     non-reflecting far-field boundary,
 *   - an overlap band where information is exchanged in both directions:
 *       mesh  -> buffer particles  (Dirichlet data for the SPH side)
 *       particles -> interface cells (OPMIS reconstruction for the FVM side)
 *
 * Three interpolation schemes are implemented and compared:
 *   SHEPARD  : zeroth-order kernel normalisation (robust, diffusive)
 *   MLS      : first-order moving least squares (accurate, can overshoot)
 *   OPMIS    : optimized particle-mesh interpolation = MLS blended back to
 *              Shepard through a conditioning + negative-weight limiter,
 *              which removes the numerical instability observed when strong
 *              pressure waves travel from particles to the mesh.
 */
import { Kernel } from "./sph";

export type Scheme = "shepard" | "mls" | "opmis";

export const SCHEME_LABEL: Record<Scheme, string> = {
  shepard: "Shepard (0th order)",
  mls: "MLS (1st order)",
  opmis: "OPMIS (limited MLS)",
};

export interface Stencil {
  idx: Int32Array;
  w: Float32Array;
  cond: number;
  theta: number;
}

export interface InterpStats {
  nTargets: number;
  nEmpty: number;
  stencilMean: number;
  c0Max: number;
  c1Mean: number;
  c1Max: number;
  wMin: number;
  condMean: number;
  linearErr: number;
  shockOvershoot: number;
}

export interface InterpSet {
  stencils: Stencil[];
  stats: InterpStats;
}

function inv3(
  m: number[],
): number[] | null {
  const [a, b, c, d, e, f, g, h, i] = m;
  const A = e * i - f * h;
  const B = -(d * i - f * g);
  const C = d * h - e * g;
  const det = a * A + b * B + c * C;
  if (!isFinite(det) || Math.abs(det) < 1e-14) return null;
  const id = 1 / det;
  return [
    A * id,
    (c * h - b * i) * id,
    (b * f - c * e) * id,
    B * id,
    (a * i - c * g) * id,
    (c * d - a * f) * id,
    C * id,
    (b * g - a * h) * id,
    (a * e - b * d) * id,
  ];
}

function fro(m: number[]): number {
  let s = 0;
  for (const v of m) s += v * v;
  return Math.sqrt(s);
}

export interface Cloud {
  x: Float64Array | number[];
  y: Float64Array | number[];
  n: number;
}

/**
 * Build the interpolation operator from a source cloud onto target points.
 * `radius` is the interpolation support (kappa * h_target).
 */
export function buildInterp(
  tx: ArrayLike<number>,
  ty: ArrayLike<number>,
  targets: number[],
  radius: number,
  src: { x: ArrayLike<number>; y: ArrayLike<number>; n: number },
  kern: Kernel,
  scheme: Scheme,
  grid: { forEach(x: number, y: number, r: number, cb: (j: number) => void): void },
): InterpSet {
  const stencils: Stencil[] = [];
  const h = radius / kern.kappa;
  let nEmpty = 0;
  let sMean = 0;
  let c0Max = 0;
  let c1Mean = 0;
  let c1Max = 0;
  let wMin = 0;
  let condMean = 0;
  let linErr = 0;
  let overshoot = 0;

  // analytic probe: a linear field (first-order reproduction test)
  const fLin = (x: number, y: number) => 1 + 2 * x - 1.5 * y;

  for (const t of targets) {
    const px = tx[t];
    const py = ty[t];
    const idx: number[] = [];
    const wk: number[] = [];
    grid.forEach(px, py, radius, (j) => {
      const dx = src.x[j] - px;
      const dy = src.y[j] - py;
      const r = Math.hypot(dx, dy);
      if (r >= radius) return;
      const w = kern.alpha(h) * kern.w(r / h);
      if (w <= 0) return;
      idx.push(j);
      wk.push(w);
    });
    const m = idx.length;
    if (m === 0) {
      nEmpty++;
      stencils.push({ idx: new Int32Array(0), w: new Float32Array(0), cond: 0, theta: 0 });
      continue;
    }
    sMean += m;
    // Shepard
    let sw = 0;
    for (const w of wk) sw += w;
    const wShep = wk.map((w) => w / sw);

    let wFinal = wShep;
    let cond = 1;
    let theta = 0;
    if (scheme !== "shepard" && m >= 4) {
      const M = new Array(9).fill(0);
      for (let k = 0; k < m; k++) {
        const p = [1, (src.x[idx[k]] - px) / h, (src.y[idx[k]] - py) / h];
        for (let a = 0; a < 3; a++)
          for (let b = 0; b < 3; b++) M[3 * a + b] += wk[k] * p[a] * p[b];
      }
      const Mi = inv3(M);
      if (Mi) {
        cond = fro(M) * fro(Mi);
        const wm = new Array(m);
        for (let k = 0; k < m; k++) {
          const p = [1, (src.x[idx[k]] - px) / h, (src.y[idx[k]] - py) / h];
          wm[k] = (Mi[0] * p[0] + Mi[1] * p[1] + Mi[2] * p[2]) * wk[k];
        }
        if (scheme === "mls") {
          wFinal = wm;
          theta = 1;
        } else {
          // OPMIS limiter: degrade to Shepard where the moment matrix is
          // ill-conditioned or where MLS produces strongly negative weights
          let negSum = 0;
          for (const w of wm) if (w < 0) negSum -= w;
          const condFac = Math.max(0, 1 - (cond - 12) / 60);
          const negFac = Math.max(0, 1 - negSum / 0.25);
          theta = Math.max(0, Math.min(1, Math.min(condFac, negFac)));
          wFinal = wm.map((w, k) => theta * w + (1 - theta) * wShep[k]);
        }
      }
    }

    let s0 = 0;
    let s1x = 0;
    let s1y = 0;
    let mn = Infinity;
    let fl = 0;
    let l1 = 0;
    for (let k = 0; k < m; k++) {
      const w = wFinal[k];
      s0 += w;
      l1 += Math.abs(w);
      s1x += (w * (src.x[idx[k]] - px)) / h;
      s1y += (w * (src.y[idx[k]] - py)) / h;
      mn = Math.min(mn, w);
      fl += w * fLin(src.x[idx[k]], src.y[idx[k]]);
    }
    c0Max = Math.max(c0Max, Math.abs(s0 - 1));
    const c1 = Math.hypot(s1x, s1y);
    c1Mean += c1;
    c1Max = Math.max(c1Max, c1);
    wMin = Math.min(wMin, mn);
    condMean += cond;
    linErr = Math.max(linErr, Math.abs(fl - fLin(px, py)) / 3);
    // sharp worst-case over/undershoot for any field bounded by the stencil
    // range (attained by a two-valued field, e.g. a 1000:1 pressure jump):
    //   max overshoot = (||w||_1 - sum(w)) / 2 * (f_max - f_min)
    overshoot = Math.max(overshoot, (l1 - s0) / 2);
    stencils.push({
      idx: Int32Array.from(idx),
      w: Float32Array.from(wFinal),
      cond,
      theta,
    });
  }
  const nt = Math.max(targets.length, 1);
  return {
    stencils,
    stats: {
      nTargets: targets.length,
      nEmpty,
      stencilMean: sMean / nt,
      c0Max,
      c1Mean: c1Mean / nt,
      c1Max,
      wMin,
      condMean: condMean / nt,
      linearErr: linErr,
      shockOvershoot: overshoot * 100,
    },
  };
}

/* ------------------------------------------------------------------ */
/* region bookkeeping                                                  */
/* ------------------------------------------------------------------ */

export interface CoreRegion {
  x: number;
  y: number;
  r: number;
  buffer: number; // band width in units of dp
}

export const CELL_FVM = 0;
export const CELL_INTERFACE = 1;
export const CELL_COVERED = 2;

export const PART_CORE = 0;
export const PART_BUFFER = 1;
export const PART_OUTSIDE = 2;

export function classifyCells(
  cc: Float64Array,
  nCells: number,
  core: CoreRegion,
  band: number,
): Uint8Array {
  const out = new Uint8Array(nCells);
  for (let c = 0; c < nCells; c++) {
    const r = Math.hypot(cc[2 * c] - core.x, cc[2 * c + 1] - core.y);
    if (r < core.r - band) out[c] = CELL_COVERED;
    else if (r < core.r) out[c] = CELL_INTERFACE;
    else out[c] = CELL_FVM;
  }
  return out;
}

export function classifyParticles(
  x: Float64Array,
  y: Float64Array,
  n: number,
  core: CoreRegion,
  band: number,
): Uint8Array {
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i++) {
    const r = Math.hypot(x[i] - core.x, y[i] - core.y);
    out[i] =
      r > core.r + band
        ? PART_OUTSIDE // pruned when the core region is re-sized
        : r > core.r - band
          ? PART_BUFFER
          : PART_CORE;
  }
  return out;
}
