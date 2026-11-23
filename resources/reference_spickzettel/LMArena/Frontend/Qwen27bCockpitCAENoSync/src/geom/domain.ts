// ─────────────────────────────────────────────────────────────────────────────
// CFDGEO01 tessellation — step-channel + cylinder domain.
// f64 vertex storage (camera-relative translation source), f32 normals,
// u32 triangle indices, per-vertex face_group_id, plus open / non-manifold
// edge segment lists for neon highlighting.
// ─────────────────────────────────────────────────────────────────────────────
import type { DomainParams } from "../fields/analytics";
import { localTop } from "../fields/analytics";
import { PATCH_NAMES } from "../schema/registry";
import type { PatchMeta } from "../state/store";

export interface DomainGeo {
  verts: Float64Array;
  normals: Float32Array;
  tris: Uint32Array;
  faceGroup: Uint8Array;
  nTris: number;
  openEdges: Float32Array; // 6 floats per segment
  nonManifold: Float32Array;
  patches: PatchMeta[];
  gap: { x0: number; x1: number } | null;
}

type V3 = [number, number, number];

export function buildDomain(dp: DomainParams, leaveGap: boolean): DomainGeo {
  const { L, H, hw, stepX, h1, cylX, cylY, cylR } = dp;
  const verts: number[] = [];
  const normals: number[] = [];
  const groups: number[] = [];
  const tris: number[] = [];

  const quad = (a: V3, b: V3, c: V3, d: V3, n: V3, g: number) => {
    const i = verts.length / 3;
    for (const p of [a, b, c, d]) verts.push(p[0], p[1], p[2]);
    for (let k = 0; k < 4; k++) {
      normals.push(n[0], n[1], n[2]);
      groups.push(g);
    }
    tris.push(i, i + 1, i + 2, i, i + 2, i + 3);
  };

  const grid = (
    p0: V3, du: V3, dv: V3, nu: number, nv: number, n: V3, g: number,
    skip?: (u: number) => boolean
  ) => {
    for (let i = 0; i < nu; i++) {
      for (let j = 0; j < nv; j++) {
        if (skip && skip((i + 0.5) / nu)) continue;
        const add = (uu: number, vv: number): V3 => [
          p0[0] + du[0] * uu + dv[0] * vv,
          p0[1] + du[1] * uu + dv[1] * vv,
          p0[2] + du[2] * uu + dv[2] * vv,
        ];
        const u0 = i / nu, u1 = (i + 1) / nu, v0 = j / nv, v1 = (j + 1) / nv;
        quad(add(u0, v0), add(u1, v0), add(u1, v1), add(u0, v1), n, g);
      }
    }
  };

  const gap = leaveGap ? { x0: stepX * 0.45, x1: stepX * 0.45 + 0.11 * stepX } : null;

  // ── bottom (g3)
  grid([0, 0, -hw], [L, 0, 0], [0, 0, 2 * hw], 24, 6, [0, -1, 0], 3);
  // ── top low (g2), with optional CAD gap
  if (gap) {
    const segs: [number, number][] = [[0, gap.x0], [gap.x1, stepX]];
    segs.forEach(([a, b]) =>
      grid([a, h1, -hw], [b - a, 0, 0], [0, 0, 2 * hw], Math.max(2, Math.round((b - a) / 0.5)), 6, [0, 1, 0], 2)
    );
  } else {
    grid([0, h1, -hw], [stepX, 0, 0], [0, 0, 2 * hw], 12, 6, [0, 1, 0], 2);
  }
  // ── top high (g2)
  grid([stepX, H, -hw], [L - stepX, 0, 0], [0, 0, 2 * hw], 14, 6, [0, 1, 0], 2);
  // ── inlet (g0)
  grid([0, 0, -hw], [0, h1, 0], [0, 0, 2 * hw], 4, 6, [-1, 0, 0], 0);
  // ── outlet (g1)
  grid([L, 0, -hw], [0, H, 0], [0, 0, 2 * hw], 4, 8, [1, 0, 0], 1);
  // ── step face (g4)
  grid([stepX, h1, -hw], [0, H - h1, 0], [0, 0, 2 * hw], 4, 6, [-1, 0, 0], 4);
  // ── symmetry front / back (g6 / g7): L profile = two rectangles
  for (const [zs, g] of [[-hw, 6], [hw, 7]] as [number, number][]) {
    const prof: [number, number][][] = [
      [[0, 0], [stepX, 0], [stepX, h1], [0, h1]],
      [[stepX, 0], [L, 0], [L, H], [stepX, H]],
    ];
    for (const q of prof) {
      quad(
        [q[0][0], q[0][1], zs], [q[1][0], q[1][1], zs],
        [q[2][0], q[2][1], zs], [q[3][0], q[3][1], zs],
        [0, 0, zs < 0 ? -1 : 1], g
      );
    }
  }

  // ── cylinder (g5): side + caps
  const SEG = 48;
  const z0 = -hw + 0.02, z1 = hw - 0.02;
  for (let i = 0; i < SEG; i++) {
    const a0 = (i / SEG) * Math.PI * 2;
    const a1 = ((i + 1) / SEG) * Math.PI * 2;
    const c0 = Math.cos(a0), s0 = Math.sin(a0);
    const c1 = Math.cos(a1), s1 = Math.sin(a1);
    quad(
      [cylX + cylR * c0, cylY + cylR * s0, z0],
      [cylX + cylR * c1, cylY + cylR * s1, z0],
      [cylX + cylR * c1, cylY + cylR * s1, z1],
      [cylX + cylR * c0, cylY + cylR * s0, z1],
      [c0, s0, 0], 5
    );
  }
  const fan = (zc: number, nz: number, g: number) => {
    const ci = verts.length / 3;
    verts.push(cylX, cylY, zc);
    normals.push(0, 0, nz);
    groups.push(g);
    for (let i = 0; i < SEG; i++) {
      const a0 = (i / SEG) * Math.PI * 2;
      const a1 = ((i + 1) / SEG) * Math.PI * 2;
      const i0 = ci + 1 + i * 2;
      verts.push(
        cylX + cylR * Math.cos(a0), cylY + cylR * Math.sin(a0), zc,
        cylX + cylR * Math.cos(a1), cylY + cylR * Math.sin(a1), zc
      );
      normals.push(0, 0, nz, 0, 0, nz);
      groups.push(g, g);
      tris.push(ci, i0, i0 + 1);
    }
  };
  fan(z0, -1, 5);
  fan(z1, 1, 5);

  // ── defect edge segments
  const open: number[] = [];
  if (gap) {
    open.push(gap.x0, h1, -hw, gap.x0, h1, hw);
    open.push(gap.x1, h1, -hw, gap.x1, h1, hw);
  }
  const nonman: number[] = [stepX, h1, -0.5, stepX, h1, 0.5];

  // ── patch metadata (analytic areas)
  const gapArea = gap ? (gap.x1 - gap.x0) * 2 * hw : 0;
  const areas = [
    h1 * 2 * hw,                       // inlet
    H * 2 * hw,                        // outlet
    (stepX + (L - stepX)) * 2 * hw - gapArea, // top
    L * 2 * hw,                        // bottom
    (H - h1) * 2 * hw,                 // step
    2 * Math.PI * cylR * (z1 - z0) + 2 * Math.PI * cylR * cylR,
    stepX * h1 + (L - stepX) * H,      // sym front
    stepX * h1 + (L - stepX) * H,      // sym back
  ];
  const nTrisPer = new Array(8).fill(0);
  for (let t = 0; t < tris.length; t += 3) nTrisPer[groups[tris[t]]]++;
  const nrm: V3[] = [[-1, 0, 0], [1, 0, 0], [0, 1, 0], [0, -1, 0], [-1, 0, 0], [0, 0, 0], [0, 0, -1], [0, 0, 1]];
  const kinds = ["inlet", "outlet", "wall", "wall", "wall (step)", "wall (cylinder)", "symmetry", "symmetry"];

  const patches: PatchMeta[] = PATCH_NAMES.map((name, i) => ({
    id: i,
    name,
    kind: kinds[i],
    area: areas[i],
    normal: nrm[i],
    nTris: nTrisPer[i],
  }));

  return {
    verts: new Float64Array(verts),
    normals: new Float32Array(normals),
    tris: new Uint32Array(tris),
    faceGroup: new Uint8Array(groups),
    nTris: tris.length / 3,
    openEdges: new Float32Array(open),
    nonManifold: new Float32Array(nonman),
    patches,
    gap,
  };
}

export { localTop };
