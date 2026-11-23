// ─────────────────────────────────────────────────────────────────────────────
// CFDMSH02 cell mesh + quality metrics.
// Each cell is emitted as 12 independent triangles with per-vertex constant
// color/val — the renderer interpolates nothing, so defective / NaN cells are
// NEVER smoothed away (flat-faceting invariant).
// ─────────────────────────────────────────────────────────────────────────────
import type { DomainParams } from "../fields/analytics";
import { localTop } from "../fields/analytics";
import { buildLUT, sampleLUT } from "../fields/colormaps";
import type { Metric } from "../state/store";

export interface MeshData {
  nCells: number;
  triPos: Float32Array;
  triCol: Record<Metric, Float32Array>;
  triVal: Record<Metric, Float32Array>;
  nanCandidate: number[]; // cell indices that go NaN on solver blow-up
  foldedCells: number[];
  stats: {
    cells: number;
    verts: number;
    maxSkew: number;
    meanNonOrth: number;
    maxAspect: number;
    hist: number[]; // 24 skewness bins
  };
}

export const METRIC_META: Record<Metric, { label: string; unit: string; range: [number, number] }> = {
  skew: { label: "skewness", unit: "[0,1]", range: [0, 1] },
  nonorth: { label: "non_orth", unit: "[°]", range: [0, 89] },
  aspect: { label: "aspect_ratio", unit: "[–]", range: [0, 45] },
  vol: { label: "cell_volume", unit: "[m³]", range: [0, 1] },
};

const LUTS: Record<Metric, Uint8Array> = {
  skew: buildLUT("traffic", 0),
  nonorth: buildLUT("moreland", 0),
  aspect: buildLUT("viridis", 0),
  vol: buildLUT("fire", 0),
};

const KEYS: Metric[] = ["skew", "nonorth", "aspect", "vol"];

export function buildMesh(dp: DomainParams, cells: { x: number; y: number; z: number }): MeshData {
  const { L, H, hw, stepX, h1, cylX, cylY, cylR } = dp;
  const { x: nx, y: ny, z: nz } = cells;
  const dxc = L / nx, dyc = H / ny, dzc = 2 * hw / nz;

  const nTriVerts = 36;
  const total = nx * ny * nz * nTriVerts;
  const triPos = new Float32Array(total * 3);
  const triCol: Record<Metric, Float32Array> = {
    skew: new Float32Array(total * 3),
    nonorth: new Float32Array(total * 3),
    aspect: new Float32Array(total * 3),
    vol: new Float32Array(total * 3),
  };
  const triVal: Record<Metric, Float32Array> = {
    skew: new Float32Array(total),
    nonorth: new Float32Array(total),
    aspect: new Float32Array(total),
    vol: new Float32Array(total),
  };

  const faces = [
    [0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4],
    [2, 3, 7, 6], [1, 2, 6, 5], [0, 3, 7, 4],
  ];

  let nCells = 0;
  let maxSkew = 0;
  let sumNonOrth = 0;
  let maxAspect = 0;
  const hist = new Array(24).fill(0);
  const cellInfo: { cx: number; cy: number; cz: number; dStep: number }[] = [];

  for (let i = 0; i < nx; i++) {
    for (let j = 0; j < ny; j++) {
      for (let k = 0; k < nz; k++) {
        const cx = (i + 0.5) * dxc;
        const cy = (j + 0.5) * dyc;
        const cz = -hw + (k + 0.5) * dzc;
        if (cy > localTop(cx, dp) - 1e-4) continue;
        const qdx = cx - cylX, qdy = cy - cylY;
        if (qdx * qdx + qdy * qdy < cylR * cylR * 0.92) continue;

        const noise = 0.5 + 0.5 * Math.sin(cx * 3.1) * Math.cos(cy * 4.7) * Math.sin(cz * 2.3 + 1);
        const sp = Math.exp(-(((cx - stepX) ** 2) + (cy - h1) ** 2) / 0.9);
        const dR = Math.sqrt(qdx * qdx + qdy * qdy) - cylR;
        const cp = Math.exp(-((dR * dR) / 0.25));
        const hash = ((i * 73856093) ^ (j * 19349663) ^ (k * 83492791)) >>> 0;
        const spike = hash % 17 === 0 ? 0.33 : 0;

        const skew = Math.min(0.995, Math.max(0, 0.03 + 0.26 * noise + 0.6 * sp + 0.14 * cp + spike));
        const non = Math.min(89, 2 + 13 * noise + 58 * cp + 30 * sp);
        const asp = 1.2 + 2.2 * noise + 38 * sp * sp + 8 * cp;
        const vol = 0.3 + 0.5 * noise + 0.2 * sp;

        const cellIdx = nCells;
        nCells++;
        maxSkew = Math.max(maxSkew, skew);
        sumNonOrth += non;
        maxAspect = Math.max(maxAspect, asp);
        hist[Math.min(23, Math.floor(skew * 24))]++;
        cellInfo.push({ cx, cy, cz, dStep: (cx - stepX) ** 2 + (cy - h1) ** 2 });

        const hx = dxc * 0.47, hy = dyc * 0.47, hz = dzc * 0.47;
        const c: [number, number, number][] = [
          [cx - hx, cy - hy, cz - hz], [cx + hx, cy - hy, cz - hz],
          [cx + hx, cy + hy, cz - hz], [cx - hx, cy + hy, cz - hz],
          [cx - hx, cy - hy, cz + hz], [cx + hx, cy - hy, cz + hz],
          [cx + hx, cy + hy, cz + hz], [cx - hx, cy + hy, cz + hz],
        ];
        const v = [skew, non / 89, Math.min(1, asp / 45), vol];
        let tv = cellIdx * nTriVerts;
        for (const f of faces) {
          for (const t of [[f[0], f[1], f[2]], [f[0], f[2], f[3]]]) {
            for (const vi of t) {
              triPos[tv * 3] = c[vi][0];
              triPos[tv * 3 + 1] = c[vi][1];
              triPos[tv * 3 + 2] = c[vi][2];
              KEYS.forEach((key, ki) => {
                const rgb = sampleLUT(LUTS[key], Math.max(0, Math.min(1, v[ki])));
                const o = tv * 3;
                triCol[key][o] = rgb[0] / 255;
                triCol[key][o + 1] = rgb[1] / 255;
                triCol[key][o + 2] = rgb[2] / 255;
                triVal[key][tv] = v[ki];
              });
              tv++;
            }
          }
        }
      }
    }
  }

  // Folded faces: 3 cells closest to the step corner. NaN candidates: next 4.
  const order = cellInfo.map((c, i) => ({ i, d: c.dStep })).sort((a, b) => a.d - b.d);
  const foldedCells = order.slice(0, 3).map((e) => e.i);
  const nanCandidate = order.slice(3, 7).map((e) => e.i);

  const patch = (ci: number, rgb: [number, number, number], val: number) => {
    for (let t = 0; t < nTriVerts; t++) {
      const o = (ci * nTriVerts + t) * 3;
      KEYS.forEach((key) => {
        triCol[key][o] = rgb[0] / 255;
        triCol[key][o + 1] = rgb[1] / 255;
        triCol[key][o + 2] = rgb[2] / 255;
        triVal[key][ci * nTriVerts + t] = val;
      });
    }
  };
  foldedCells.forEach((ci) => patch(ci, [185, 245, 0], 1));
  nanCandidate.forEach((ci) => patch(ci, [239, 35, 60], -1));

  const histMax = Math.max(1, ...hist);
  return {
    nCells,
    triPos,
    triCol,
    triVal,
    nanCandidate,
    foldedCells,
    stats: {
      cells: nCells,
      verts: nCells * 8,
      maxSkew,
      meanNonOrth: sumNonOrth / Math.max(1, nCells),
      maxAspect,
      hist: hist.map((h) => h / histMax),
    },
  };
}
