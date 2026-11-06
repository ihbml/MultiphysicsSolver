/**
 * Unstructured body-fitted mesher (DistMesh-style force equilibrium on a
 * Delaunay triangulation) + cell-centred finite-volume topology.
 *
 * The face based data structure (owner / neighbour / Sf) is exactly the one
 * required by DiFVM (Du, Li, Xu & Wang 2026): every FV operator becomes a
 * gather over `owner`/`neighbour` and a scatter-add back onto the cells, i.e.
 * message passing on the static mesh graph.
 */
import Delaunator from "delaunator";
import { Geometry, PATCH_ORDER, PatchKind, sdfGrad } from "./sdf";

export interface MeshParams {
  h0: number;
  grading: number;
  hmaxRatio: number;
  iters: number;
}

export interface MeshStats {
  nPoints: number;
  nCells: number;
  nFaces: number;
  nInternal: number;
  nBoundary: number;
  minVol: number;
  maxVol: number;
  meanVol: number;
  qMin: number;
  qMean: number;
  nonOrthoMax: number;
  nonOrthoMean: number;
  skewMax: number;
  skewMean: number;
  closureResidual: number;
  boundaryArea: number;
  exactPerimeter: number;
  patchFaces: Record<string, number>;
  bandwidthBefore: number;
  bandwidthAfter: number;
  avgDegree: number;
  arMax: number;
}

export interface FvmMesh {
  points: Float64Array;
  tris: Uint32Array;
  nCells: number;
  cc: Float64Array;
  cv: Float64Array;
  nFaces: number;
  owner: Int32Array;
  neigh: Int32Array;
  fc: Float64Array;
  fn: Float64Array;
  fa: Float64Array;
  fPatch: Int8Array;
  faceNodes: Uint32Array;
  nonOrtho: Float32Array;
  skew: Float32Array;
  weight: Float32Array;
  nonOrthoCorr: Float32Array;
  quality: Float32Array;
  cellSize: Float32Array;
  rcm: Uint32Array;
  stats: MeshStats;
}

const FSCALE = 1.2;
const DT = 0.22;

export function sizeFn(
  geo: Geometry,
  p: MeshParams,
  x: number,
  y: number,
): number {
  const d = Math.abs(geo.refineDist(x, y));
  const h = p.h0 * (1 + p.grading * (d / p.h0));
  return Math.min(h, p.h0 * p.hmaxRatio);
}

/* ------------------------------------------------------------------ */
/* iterative mesh generator (steppable so the UI can animate it)       */
/* ------------------------------------------------------------------ */

export class MeshJob {
  geo: Geometry;
  p: MeshParams;
  px: Float64Array;
  py: Float64Array;
  nFixed: number;
  iter = 0;
  tris: Uint32Array = new Uint32Array(0);
  maxMove = 1;
  private geps: number;
  private deps: number;
  private coords: Float64Array;

  constructor(geo: Geometry, p: MeshParams) {
    this.geo = geo;
    this.p = p;
    this.geps = 1e-3 * p.h0;
    this.deps = 1e-4 * p.h0;
    const [x0, y0, x1, y1] = geo.bbox;
    const h = p.h0;
    const rows = Math.ceil((y1 - y0) / (h * Math.sqrt(3) / 2)) + 1;
    const cols = Math.ceil((x1 - x0) / h) + 1;
    const cand: number[] = [];
    let rmax = 0;
    const dens: number[] = [];
    for (let j = 0; j <= rows; j++) {
      const yy = y0 + j * h * (Math.sqrt(3) / 2);
      for (let i = 0; i <= cols; i++) {
        const xx = x0 + i * h + (j % 2 ? h / 2 : 0);
        const dv = geo.sdf(xx, yy);
        if (dv > -0.05 * h) continue;
        const r = 1 / sizeFn(geo, p, xx, yy) ** 2;
        cand.push(xx, yy);
        dens.push(r);
        if (r > rmax) rmax = r;
      }
    }
    const xs: number[] = [];
    const ys: number[] = [];
    for (const [fx, fy] of geo.fixedPoints) {
      xs.push(fx);
      ys.push(fy);
    }
    this.nFixed = xs.length;
    // deterministic rejection sampling (hash based, so results are stable)
    let seed = 1337;
    const rnd = () => {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      return seed / 0x7fffffff;
    };
    for (let k = 0; k < dens.length; k++) {
      if (rnd() < dens[k] / rmax) {
        const xx = cand[2 * k];
        const yy = cand[2 * k + 1];
        let dup = false;
        for (let f = 0; f < this.nFixed; f++)
          if (Math.hypot(xx - xs[f], yy - ys[f]) < 0.6 * p.h0) dup = true;
        if (!dup) {
          xs.push(xx);
          ys.push(yy);
        }
      }
    }
    this.px = Float64Array.from(xs);
    this.py = Float64Array.from(ys);
    this.coords = new Float64Array(this.px.length * 2);
    this.retriangulate();
  }

  get n(): number {
    return this.px.length;
  }

  private retriangulate() {
    const n = this.n;
    if (this.coords.length !== 2 * n) this.coords = new Float64Array(2 * n);
    for (let i = 0; i < n; i++) {
      this.coords[2 * i] = this.px[i];
      this.coords[2 * i + 1] = this.py[i];
    }
    const d = new Delaunator(this.coords);
    const keep: number[] = [];
    const t = d.triangles;
    for (let k = 0; k < t.length; k += 3) {
      const a = t[k];
      const b = t[k + 1];
      const c = t[k + 2];
      const cx = (this.px[a] + this.px[b] + this.px[c]) / 3;
      const cy = (this.py[a] + this.py[b] + this.py[c]) / 3;
      if (this.geo.sdf(cx, cy) < -this.geps) keep.push(a, b, c);
    }
    this.tris = Uint32Array.from(keep);
  }

  /** one DistMesh force-balance iteration */
  step(): boolean {
    const { px, py, tris, geo, p } = this;
    const n = this.n;
    const edgeSet = new Map<number, number>();
    const ea: number[] = [];
    const eb: number[] = [];
    for (let k = 0; k < tris.length; k += 3) {
      for (let e = 0; e < 3; e++) {
        let i = tris[k + e];
        let j = tris[k + ((e + 1) % 3)];
        if (i > j) {
          const t = i;
          i = j;
          j = t;
        }
        const key = i * n + j;
        if (!edgeSet.has(key)) {
          edgeSet.set(key, ea.length);
          ea.push(i);
          eb.push(j);
        }
      }
    }
    const ne = ea.length;
    const L = new Float64Array(ne);
    const hb = new Float64Array(ne);
    let sumL2 = 0;
    let sumH2 = 0;
    for (let k = 0; k < ne; k++) {
      const i = ea[k];
      const j = eb[k];
      const dx = px[i] - px[j];
      const dy = py[i] - py[j];
      const l = Math.hypot(dx, dy) || 1e-12;
      L[k] = l;
      hb[k] = sizeFn(geo, p, (px[i] + px[j]) / 2, (py[i] + py[j]) / 2);
      sumL2 += l * l;
      sumH2 += hb[k] * hb[k];
    }
    const scale = FSCALE * Math.sqrt(sumL2 / Math.max(sumH2, 1e-30));
    const fx = new Float64Array(n);
    const fy = new Float64Array(n);
    for (let k = 0; k < ne; k++) {
      const l0 = hb[k] * scale;
      const f = Math.max(l0 - L[k], 0);
      const i = ea[k];
      const j = eb[k];
      const ux = (px[i] - px[j]) / L[k];
      const uy = (py[i] - py[j]) / L[k];
      fx[i] += f * ux;
      fy[i] += f * uy;
      fx[j] -= f * ux;
      fy[j] -= f * uy;
    }
    let maxMove = 0;
    for (let i = this.nFixed; i < n; i++) {
      const dx = DT * fx[i];
      const dy = DT * fy[i];
      px[i] += dx;
      py[i] += dy;
      const d = geo.sdf(px[i], py[i]);
      if (d > 0) {
        const [gx, gy] = sdfGrad(geo, px[i], py[i], this.deps);
        const g2 = gx * gx + gy * gy || 1e-30;
        px[i] -= (d * gx) / g2;
        py[i] -= (d * gy) / g2;
      }
      const m = Math.hypot(dx, dy);
      if (m > maxMove) maxMove = m;
    }
    this.maxMove = maxMove / p.h0;
    this.iter++;
    this.retriangulate();
    return this.iter >= this.p.iters;
  }
}

/* ------------------------------------------------------------------ */
/* FVM topology assembly                                               */
/* ------------------------------------------------------------------ */

/** drop degenerate boundary slivers (all three nodes on the hull, q < qmin) */
function removeSlivers(
  tri: number[],
  px: number[],
  py: number[],
  qmin: number,
  passes = 2,
): number[] {
  let cur = tri;
  const np = px.length;
  for (let pass = 0; pass < passes; pass++) {
    const cnt = new Map<number, number>();
    const triPerNode = new Int32Array(np);
    for (let k = 0; k < cur.length; k += 3) {
      for (let e = 0; e < 3; e++) {
        const i = cur[k + e];
        const j = cur[k + ((e + 1) % 3)];
        triPerNode[i]++;
        const key = Math.min(i, j) * np + Math.max(i, j);
        cnt.set(key, (cnt.get(key) ?? 0) + 1);
      }
    }
    const next: number[] = [];
    let removed = 0;
    for (let k = 0; k < cur.length; k += 3) {
      const a = cur[k];
      const b = cur[k + 1];
      const c = cur[k + 2];
      const la = Math.hypot(px[b] - px[c], py[b] - py[c]);
      const lb = Math.hypot(px[a] - px[c], py[a] - py[c]);
      const lc = Math.hypot(px[a] - px[b], py[a] - py[b]);
      const q =
        ((lb + lc - la) * (lc + la - lb) * (la + lb - lc)) /
        Math.max(la * lb * lc, 1e-30);
      const key = (i: number, j: number) =>
        Math.min(i, j) * np + Math.max(i, j);
      const nBndEdge =
        (cnt.get(key(a, b)) === 1 ? 1 : 0) +
        (cnt.get(key(b, c)) === 1 ? 1 : 0) +
        (cnt.get(key(c, a)) === 1 ? 1 : 0);
      const isolates =
        triPerNode[a] <= 1 || triPerNode[b] <= 1 || triPerNode[c] <= 1;
      if (q < qmin && nBndEdge >= 1 && !isolates) {
        removed++;
        triPerNode[a]--;
        triPerNode[b]--;
        triPerNode[c]--;
        continue;
      }
      next.push(a, b, c);
    }
    cur = next;
    if (!removed) break;
  }
  return cur;
}

export function buildFvm(job: MeshJob): FvmMesh {
  const geo = job.geo;
  // orient CCW on the global node set, clean up boundary slivers, then compact
  const gx = Array.from(job.px);
  const gy = Array.from(job.py);
  const gtri: number[] = [];
  const src = job.tris;
  for (let k = 0; k < src.length; k += 3) {
    const a = src[k];
    const b = src[k + 1];
    const c = src[k + 2];
    const area2 =
      (gx[b] - gx[a]) * (gy[c] - gy[a]) - (gx[c] - gx[a]) * (gy[b] - gy[a]);
    if (area2 < 0) gtri.push(a, c, b);
    else gtri.push(a, b, c);
  }
  const cleaned = removeSlivers(gtri, gx, gy, 0.16, 3);

  const used = new Map<number, number>();
  const px: number[] = [];
  const py: number[] = [];
  const tri: number[] = [];
  for (let k = 0; k < cleaned.length; k++) {
    const o = cleaned[k];
    let m = used.get(o);
    if (m === undefined) {
      m = px.length;
      used.set(o, m);
      px.push(gx[o]);
      py.push(gy[o]);
    }
    tri.push(m);
  }
  const points = new Float64Array(px.length * 2);
  for (let i = 0; i < px.length; i++) {
    points[2 * i] = px[i];
    points[2 * i + 1] = py[i];
  }
  const tris = Uint32Array.from(tri);
  const nCells = tris.length / 3;
  const np = px.length;

  const cc = new Float64Array(2 * nCells);
  const cv = new Float64Array(nCells);
  const quality = new Float32Array(nCells);
  const cellSize = new Float32Array(nCells);
  for (let c = 0; c < nCells; c++) {
    const a = tris[3 * c];
    const b = tris[3 * c + 1];
    const d = tris[3 * c + 2];
    cc[2 * c] = (px[a] + px[b] + px[d]) / 3;
    cc[2 * c + 1] = (py[a] + py[b] + py[d]) / 3;
    const A =
      0.5 *
      Math.abs(
        (px[b] - px[a]) * (py[d] - py[a]) - (px[d] - px[a]) * (py[b] - py[a]),
      );
    cv[c] = A;
    const la = Math.hypot(px[b] - px[d], py[b] - py[d]);
    const lb = Math.hypot(px[a] - px[d], py[a] - py[d]);
    const lc = Math.hypot(px[a] - px[b], py[a] - py[b]);
    quality[c] =
      ((lb + lc - la) * (lc + la - lb) * (la + lb - lc)) /
      Math.max(la * lb * lc, 1e-30);
    cellSize[c] = Math.sqrt(A);
  }

  // faces
  const fmap = new Map<number, number>();
  const owner: number[] = [];
  const neigh: number[] = [];
  const fnA: number[] = [];
  const fnB: number[] = [];
  const faceNodes: number[] = [];
  for (let c = 0; c < nCells; c++) {
    for (let e = 0; e < 3; e++) {
      const i = tris[3 * c + e];
      const j = tris[3 * c + ((e + 1) % 3)];
      const lo = Math.min(i, j);
      const hi = Math.max(i, j);
      const key = lo * np + hi;
      const ex = fmap.get(key);
      if (ex === undefined) {
        fmap.set(key, owner.length);
        owner.push(c);
        neigh.push(-1);
        faceNodes.push(i, j);
        // outward normal of CCW triangle edge i->j
        const dx = px[j] - px[i];
        const dy = py[j] - py[i];
        const l = Math.hypot(dx, dy) || 1e-30;
        fnA.push(dy / l);
        fnB.push(-dx / l);
      } else {
        neigh[ex] = c;
      }
    }
  }
  const nFaces = owner.length;
  const fc = new Float64Array(2 * nFaces);
  const fn = new Float64Array(2 * nFaces);
  const fa = new Float64Array(nFaces);
  const fPatch = new Int8Array(nFaces).fill(-1);
  const nonOrtho = new Float32Array(nFaces);
  const skew = new Float32Array(nFaces);
  const weight = new Float32Array(nFaces);
  const nonOrthoCorr = new Float32Array(nFaces);
  const patchFaces: Record<string, number> = {};
  let boundaryArea = 0;
  let nInternal = 0;

  for (let f = 0; f < nFaces; f++) {
    const i = faceNodes[2 * f];
    const j = faceNodes[2 * f + 1];
    const x1 = px[i];
    const y1 = py[i];
    const x2 = px[j];
    const y2 = py[j];
    fc[2 * f] = 0.5 * (x1 + x2);
    fc[2 * f + 1] = 0.5 * (y1 + y2);
    fa[f] = Math.hypot(x2 - x1, y2 - y1);
    fn[2 * f] = fnA[f];
    fn[2 * f + 1] = fnB[f];
    const O = owner[f];
    const N = neigh[f];
    let dx: number;
    let dy: number;
    if (N >= 0) {
      nInternal++;
      dx = cc[2 * N] - cc[2 * O];
      dy = cc[2 * N + 1] - cc[2 * O + 1];
    } else {
      dx = fc[2 * f] - cc[2 * O];
      dy = fc[2 * f + 1] - cc[2 * O + 1];
      const k = geo.patchAt(fc[2 * f], fc[2 * f + 1]);
      fPatch[f] = PATCH_ORDER.indexOf(k);
      patchFaces[k] = (patchFaces[k] ?? 0) + 1;
      boundaryArea += fa[f];
    }
    const dl = Math.hypot(dx, dy) || 1e-30;
    const cosT = Math.min(
      1,
      Math.max(-1, (dx * fn[2 * f] + dy * fn[2 * f + 1]) / dl),
    );
    nonOrtho[f] = (Math.acos(cosT) * 180) / Math.PI;
    // over-relaxed decomposition Sf = Ef + Tf, report |Tf|/|Sf|
    const Ex = (dx / dl) * (1 / Math.max(Math.abs(cosT), 1e-6));
    const Ey = (dy / dl) * (1 / Math.max(Math.abs(cosT), 1e-6));
    nonOrthoCorr[f] = Math.hypot(fn[2 * f] - Ex, fn[2 * f + 1] - Ey);
    // skewness: distance of face centre to the O-N intersection point
    if (N >= 0) {
      const ox = cc[2 * O];
      const oy = cc[2 * O + 1];
      const denom = fn[2 * f] * dx + fn[2 * f + 1] * dy;
      const t =
        (fn[2 * f] * (fc[2 * f] - ox) + fn[2 * f + 1] * (fc[2 * f + 1] - oy)) /
        (Math.abs(denom) < 1e-30 ? 1e-30 : denom);
      const ix = ox + t * dx;
      const iy = oy + t * dy;
      skew[f] = Math.hypot(fc[2 * f] - ix, fc[2 * f + 1] - iy) / dl;
      const dON = Math.hypot(cc[2 * N] - fc[2 * f], cc[2 * N + 1] - fc[2 * f + 1]);
      const dOO = Math.hypot(ox - fc[2 * f], oy - fc[2 * f + 1]);
      weight[f] = dON / Math.max(dON + dOO, 1e-30);
    } else {
      skew[f] = 0;
      weight[f] = 1;
    }
  }

  // zero-order consistency of the FV control volumes: sum_f Sf n_f = 0
  const clo = new Float64Array(2 * nCells);
  for (let f = 0; f < nFaces; f++) {
    const O = owner[f];
    const N = neigh[f];
    clo[2 * O] += fa[f] * fn[2 * f];
    clo[2 * O + 1] += fa[f] * fn[2 * f + 1];
    if (N >= 0) {
      clo[2 * N] -= fa[f] * fn[2 * f];
      clo[2 * N + 1] -= fa[f] * fn[2 * f + 1];
    }
  }
  let closure = 0;
  let meanL = 0;
  for (let c = 0; c < nCells; c++) {
    closure = Math.max(closure, Math.hypot(clo[2 * c], clo[2 * c + 1]));
    meanL += Math.sqrt(cv[c]);
  }
  meanL /= Math.max(nCells, 1);
  closure /= Math.max(meanL, 1e-30);

  // RCM bandwidth reduction on the cell graph (DiFVM static graph locality)
  const { perm, bw0, bw1 } = rcmReorder(nCells, owner, neigh, nFaces);

  let minVol = Infinity;
  let maxVol = 0;
  let sumVol = 0;
  let qMin = Infinity;
  let qSum = 0;
  for (let c = 0; c < nCells; c++) {
    minVol = Math.min(minVol, cv[c]);
    maxVol = Math.max(maxVol, cv[c]);
    sumVol += cv[c];
    qMin = Math.min(qMin, quality[c]);
    qSum += quality[c];
  }
  let noMax = 0;
  let noSum = 0;
  let skMax = 0;
  let skSum = 0;
  for (let f = 0; f < nFaces; f++) {
    if (neigh[f] < 0) continue;
    noMax = Math.max(noMax, nonOrtho[f]);
    noSum += nonOrtho[f];
    skMax = Math.max(skMax, skew[f]);
    skSum += skew[f];
  }
  // aspect ratio proxy: longest/shortest edge per cell
  let arMax = 1;
  for (let c = 0; c < nCells; c++) {
    const a = tris[3 * c];
    const b = tris[3 * c + 1];
    const d = tris[3 * c + 2];
    const l1 = Math.hypot(px[b] - px[a], py[b] - py[a]);
    const l2 = Math.hypot(px[d] - px[b], py[d] - py[b]);
    const l3 = Math.hypot(px[a] - px[d], py[a] - py[d]);
    arMax = Math.max(arMax, Math.max(l1, l2, l3) / Math.max(Math.min(l1, l2, l3), 1e-30));
  }

  const stats: MeshStats = {
    nPoints: np,
    nCells,
    nFaces,
    nInternal,
    nBoundary: nFaces - nInternal,
    minVol,
    maxVol,
    meanVol: sumVol / Math.max(nCells, 1),
    qMin,
    qMean: qSum / Math.max(nCells, 1),
    nonOrthoMax: noMax,
    nonOrthoMean: noSum / Math.max(nInternal, 1),
    skewMax: skMax,
    skewMean: skSum / Math.max(nInternal, 1),
    closureResidual: closure,
    boundaryArea,
    exactPerimeter: geo.perimeter,
    patchFaces,
    bandwidthBefore: bw0,
    bandwidthAfter: bw1,
    avgDegree: (2 * nInternal) / Math.max(nCells, 1),
    arMax,
  };

  return {
    points,
    tris,
    nCells,
    cc,
    cv,
    nFaces,
    owner: Int32Array.from(owner),
    neigh: Int32Array.from(neigh),
    fc,
    fn,
    fa,
    fPatch,
    faceNodes: Uint32Array.from(faceNodes),
    nonOrtho,
    skew,
    weight,
    nonOrthoCorr,
    quality,
    cellSize,
    rcm: perm,
    stats,
  };
}

/* ------------------------------------------------------------------ */
/* Reverse Cuthill-McKee on the dual (cell) graph                      */
/* ------------------------------------------------------------------ */

function rcmReorder(
  nCells: number,
  owner: number[],
  neigh: number[],
  nFaces: number,
): { perm: Uint32Array; bw0: number; bw1: number } {
  const deg = new Int32Array(nCells);
  for (let f = 0; f < nFaces; f++)
    if (neigh[f] >= 0) {
      deg[owner[f]]++;
      deg[neigh[f]]++;
    }
  const off = new Int32Array(nCells + 1);
  for (let c = 0; c < nCells; c++) off[c + 1] = off[c] + deg[c];
  const adj = new Int32Array(off[nCells]);
  const cur = Int32Array.from(off.subarray(0, nCells));
  let bw0 = 0;
  for (let f = 0; f < nFaces; f++) {
    if (neigh[f] < 0) continue;
    const a = owner[f];
    const b = neigh[f];
    adj[cur[a]++] = b;
    adj[cur[b]++] = a;
    bw0 = Math.max(bw0, Math.abs(a - b));
  }
  const perm = new Uint32Array(nCells);
  const visited = new Uint8Array(nCells);
  let k = 0;
  for (let start = 0; start < nCells; start++) {
    if (visited[start]) continue;
    const q = [start];
    visited[start] = 1;
    for (let qi = 0; qi < q.length; qi++) {
      const c = q[qi];
      perm[k++] = c;
      const nb: number[] = [];
      for (let p = off[c]; p < off[c + 1]; p++) {
        const m = adj[p];
        if (!visited[m]) {
          visited[m] = 1;
          nb.push(m);
        }
      }
      nb.sort((a, b) => deg[a] - deg[b]);
      for (const m of nb) q.push(m);
    }
  }
  // reverse
  const rev = new Uint32Array(nCells);
  for (let i = 0; i < nCells; i++) rev[i] = perm[nCells - 1 - i];
  const newIndex = new Int32Array(nCells);
  for (let i = 0; i < nCells; i++) newIndex[rev[i]] = i;
  let bw1 = 0;
  for (let f = 0; f < nFaces; f++) {
    if (neigh[f] < 0) continue;
    bw1 = Math.max(bw1, Math.abs(newIndex[owner[f]] - newIndex[neigh[f]]));
  }
  return { perm: rev, bw0, bw1 };
}

export function histogram(
  values: ArrayLike<number>,
  bins: number,
  lo: number,
  hi: number,
  filter?: (i: number) => boolean,
): number[] {
  const h = new Array(bins).fill(0);
  const span = hi - lo || 1;
  for (let i = 0; i < values.length; i++) {
    if (filter && !filter(i)) continue;
    let b = Math.floor(((values[i] - lo) / span) * bins);
    if (b < 0) b = 0;
    if (b >= bins) b = bins - 1;
    h[b]++;
  }
  return h;
}

export function patchName(i: number): PatchKind {
  return PATCH_ORDER[i] ?? "wall";
}
