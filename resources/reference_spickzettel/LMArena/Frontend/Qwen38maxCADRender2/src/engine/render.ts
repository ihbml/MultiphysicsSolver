import {
  type Camera,
  type Mat4,
  type Vec3,
  cameraBasis,
  dot,
  focal,
  norm,
  sub,
  transformDir,
  transformPoint,
  viewMatrix,
} from "./math3d";
import { MATERIALS, type PartDef } from "./parts";

export interface ViewOptions {
  shaded: boolean;
  edges: boolean;
  wireframe: boolean;
  grid: boolean;
  gizmo: boolean;
  xray: boolean;
  pbr: boolean;
  stress: boolean;
  gdt: boolean;
  dims: boolean;
  dxf: boolean;
  section: boolean;
  sectionAxis: "x" | "y" | "z";
  sectionPos: number;
  hovered: string | null;
  selected: string | null;
  pick?: { x: number; y: number } | null;
  history: number[];
  backend: string;
}

export interface FrameStats {
  tris: number;
  drawn: number;
  culled: number;
  clipped: number;
  lines: number;
  ms: number;
  picked: string | null;
}

type Poly = { p: Vec3[]; onPlane: boolean[] };

const LIGHT_KEY: Vec3 = norm([0.42, 0.78, 0.46]);
const LIGHT_FILL: Vec3 = norm([-0.62, 0.18, -0.5]);
const NEAR = 0.12;

const AXIS_N: Record<"x" | "y" | "z", Vec3> = {
  x: [1, 0, 0],
  y: [0, 1, 0],
  z: [0, 0, 1],
};

function clipHalfSpace(poly: Vec3[], n: Vec3, d: number): Poly {
  const pts: Vec3[] = [];
  const onPlane: boolean[] = [];
  const len = poly.length;
  for (let i = 0; i < len; i++) {
    const A = poly[i];
    const B = poly[(i + 1) % len];
    const da = dot(A, n) - d;
    const db = dot(B, n) - d;
    if (da <= 0) {
      pts.push(A);
      onPlane.push(Math.abs(da) < 1e-9);
    }
    if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
      const t = da / (da - db);
      pts.push([A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t, A[2] + (B[2] - A[2]) * t]);
      onPlane.push(true);
    }
  }
  return { p: pts, onPlane };
}

const RAMP: [number, Vec3][] = [
  [0.0, [16, 42, 122]],
  [0.25, [16, 132, 190]],
  [0.45, [40, 196, 140]],
  [0.65, [190, 214, 60]],
  [0.85, [242, 148, 44]],
  [1.0, [232, 58, 46]],
];

function stressColor(t: number): Vec3 {
  const x = Math.max(0, Math.min(1, t));
  for (let i = 1; i < RAMP.length; i++) {
    if (x <= RAMP[i][0]) {
      const [t0, c0] = RAMP[i - 1];
      const [t1, c1] = RAMP[i];
      const k = (x - t0) / (t1 - t0 || 1);
      return [c0[0] + (c1[0] - c0[0]) * k, c0[1] + (c1[1] - c0[1]) * k, c0[2] + (c1[2] - c0[2]) * k];
    }
  }
  return RAMP[RAMP.length - 1][1];
}

interface Item {
  z: number;
  kind: 0 | 1 | 2; // 0 = fill, 1 = line, 2 = overlay (drawn last within its depth band)
  pts?: number[];
  seg?: number[];
  color?: string;
  width?: number;
  alpha?: number;
  cap?: boolean[];
  capColor?: string;
}

export function renderFrame(
  ctx: CanvasRenderingContext2D,
  w: number,
  h: number,
  part: PartDef,
  cam: Camera,
  o: ViewOptions,
): FrameStats {
  const t0 = performance.now();
  const view: Mat4 = viewMatrix(cam);
  const f = focal(cam.fov, h);
  const cx = w / 2;
  const cy = h / 2;
  const basis = cameraBasis(cam);

  const items: Item[] = [];
  let tris = 0;
  let culled = 0;
  let clipped = 0;
  let lines = 0;
  let picked: string | null = null;
  let pickBest = -Infinity;

  const secN = o.section ? AXIS_N[o.sectionAxis] : null;
  const secD = o.sectionPos;

  const toView = (p: Vec3) => transformPoint(view, p);
  const proj = (v: Vec3): [number, number] => [cx + (v[0] / -v[2]) * f, cy - (v[1] / -v[2]) * f];

  const pointInPoly = (px: number, py: number, pts: number[]) => {
    let inside = false;
    for (let i = 0, j = pts.length / 2 - 1; i < pts.length / 2; j = i++) {
      const xi = pts[i * 2];
      const yi = pts[i * 2 + 1];
      const xj = pts[j * 2];
      const yj = pts[j * 2 + 1];
      if (yi > py !== yj > py && px < ((xj - xi) * (py - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  };

  /* ---- floor grid ------------------------------------------------ */
  if (o.grid) {
    const fy = part.support[1] - 0.02;
    const ext = 9;
    for (let i = -ext; i <= ext; i++) {
      const major = i % 3 === 0;
      const segs: [Vec3, Vec3][] = [
        [
          [i, fy, -ext],
          [i, fy, ext],
        ],
        [
          [-ext, fy, i],
          [ext, fy, i],
        ],
      ];
      for (const [a, b] of segs) {
        const va = toView(a);
        const vb = toView(b);
        const s = clipSegment(va, vb);
        if (!s) continue;
        const [x1, y1] = proj(s[0]);
        const [x2, y2] = proj(s[1]);
        const axis = i === 0;
        items.push({
          z: (s[0][2] + s[1][2]) / 2 - 0.03,
          kind: 1,
          seg: [x1, y1, x2, y2],
          color: axis
            ? "rgba(245,165,36,0.42)"
            : major
              ? "rgba(53,201,232,0.20)"
              : "rgba(120,155,180,0.10)",
          width: axis ? 1.1 : major ? 0.9 : 0.6,
        });
      }
    }
  }

  /* ---- DXF addon: imported sheet profile ------------------------- */
  if (o.dxf) {
    const fy = part.support[1] - 0.01;
    const loop: Vec3[] = [];
    const N = 120;
    for (let i = 0; i < N; i++) {
      const a = (i / N) * Math.PI * 2;
      const r = 4.6 + Math.sin(a * 6) * 0.28 + Math.cos(a * 2) * 0.16;
      loop.push([Math.cos(a) * r, fy, Math.sin(a) * r]);
    }
    for (let i = 0; i < N; i++) {
      const va = toView(loop[i]);
      const vb = toView(loop[(i + 1) % N]);
      const s = clipSegment(va, vb);
      if (!s) continue;
      const [x1, y1] = proj(s[0]);
      const [x2, y2] = proj(s[1]);
      items.push({
        z: (s[0][2] + s[1][2]) / 2 - 0.02,
        kind: 1,
        seg: [x1, y1, x2, y2],
        color: "rgba(142,222,99,0.55)",
        width: 1.1,
      });
    }
  }

  /* ---- section plane visualisation ------------------------------- */
  if (o.section && secN) {
    const a: Vec3 = secN[0]
      ? [secD, -3.4, -3.4]
      : secN[1]
        ? [-3.4, secD, -3.4]
        : [-3.4, -3.4, secD];
    const du: Vec3 = secN[0] ? [0, 6.8, 0] : [6.8, 0, 0];
    const dv: Vec3 = secN[2] ? [0, 6.8, 0] : [0, 0, 6.8];
    const quad = [a, add3(a, du), add3(add3(a, du), dv), add3(a, dv)];
    const vq = quad.map(toView);
    const c = clipHalfSpace(vq, [0, 0, 1], -NEAR);
    if (c.p.length > 2) {
      const flat: number[] = [];
      let zsum = 0;
      for (const p of c.p) {
        const [sx, sy] = proj(p);
        flat.push(sx, sy);
        zsum += p[2];
      }
      items.push({
        z: zsum / c.p.length,
        kind: 0,
        pts: flat,
        color: "rgba(245,165,36,0.10)",
        alpha: 1,
      });
      items.push({
        z: zsum / c.p.length + 0.02,
        kind: 1,
        seg: [...proj(c.p[0]), ...proj(c.p[1])],
        color: "rgba(245,165,36,0.55)",
        width: 1.2,
      });
    }
  }

  /* ---- solids ---------------------------------------------------- */
  for (const solid of part.solids) {
    const mat = MATERIALS[solid.material];
    const isSel = o.selected === solid.id;
    const isHov = o.hovered === solid.id;
    const mesh = solid.mesh;

    for (let i = 0; i < mesh.tris.length; i++) {
      tris++;
      const [a, b, c] = mesh.tris[i];
      let world = [a, b, c];
      let cutEdges: boolean[] | null = null;

      if (secN) {
        const cl = clipHalfSpace(world, secN, secD);
        if (cl.p.length < 3) {
          clipped++;
          continue;
        }
        if (cl.p.length !== 3 || cl.onPlane.some(Boolean)) clipped++;
        world = cl.p;
        cutEdges = cl.onPlane;
      }

      const vn = norm(transformDir(view, mesh.normals[i]));
      const vv = world.map(toView);
      const centV: Vec3 = [
        (vv[0][0] + vv[1][0] + vv[2][0]) / world.length,
        (vv[0][1] + vv[1][1] + vv[2][1]) / world.length,
        (vv[0][2] + vv[1][2] + vv[2][2]) / world.length,
      ];
      const facing = dot(vn, centV) < 0;
      if (!facing && !o.xray && !o.wireframe) {
        culled++;
        continue;
      }

      const cn = clipHalfSpace(vv, [0, 0, 1], -NEAR);
      if (cn.p.length < 3) continue;

      const flat: number[] = [];
      let zsum = 0;
      for (const p of cn.p) {
        const [sx, sy] = proj(p);
        flat.push(sx, sy);
        zsum += p[2];
      }
      const z = zsum / cn.p.length;

      let color: string;
      if (o.stress) {
        const wc: Vec3 = [
          (world[0][0] + world[1][0] + world[2][0]) / world.length,
          (world[0][1] + world[1][1] + world[2][1]) / world.length,
          (world[0][2] + world[1][2] + world[2][2]) / world.length,
        ];
        const s = stressAt(wc, part);
        const col = stressColor(s);
        const k = 0.55 + 0.45 * (facing ? 1 : 0.25);
        color = `rgb(${(col[0] * k) | 0},${(col[1] * k) | 0},${(col[2] * k) | 0})`;
      } else {
        color = shadeTri(
          mesh.normals[i],
          world,
          mat.base,
          mat.spec,
          basis.eye,
          facing,
          o.xray,
          o.pbr,
        );
      }

      if (o.shaded && !o.wireframe) {
        items.push({ z, kind: 0, pts: flat, color, alpha: o.xray ? 0.58 : 1 });
      }

      if (o.wireframe) {
        const wc = isSel
          ? "rgba(245,165,36,0.95)"
          : isHov
            ? "rgba(142,222,99,0.75)"
            : "rgba(53,201,232,0.40)";
        for (let k = 0; k < cn.p.length; k++) {
          const k2 = (k + 1) % cn.p.length;
          items.push({
            z: z + 0.012,
            kind: 1,
            seg: [flat[k * 2], flat[k * 2 + 1], flat[k2 * 2], flat[k2 * 2 + 1]],
            color: wc,
            width: isSel ? 1.1 : 0.7,
          });
        }
      }

      if (cutEdges) {
        for (let k = 0; k < cn.p.length; k++) {
          const k2 = (k + 1) % cn.p.length;
          if (cutEdges[k] && cutEdges[k2]) {
            items.push({
              z: z + 0.02,
              kind: 1,
              seg: [flat[k * 2], flat[k * 2 + 1], flat[k2 * 2], flat[k2 * 2 + 1]],
              color: "rgba(255,196,92,0.95)",
              width: 1.4,
            });
          }
        }
      }

      if (!o.wireframe) {
        if (isSel) {
          items.push({ z: z + 0.03, kind: 0, pts: flat, color: "rgba(245,165,36,0.18)" });
        } else if (isHov) {
          items.push({ z: z + 0.03, kind: 0, pts: flat, color: "rgba(142,222,99,0.12)" });
        }
      }

      if (o.pick && facing && z > pickBest) {
        let minX = Infinity;
        let minY = Infinity;
        let maxX = -Infinity;
        let maxY = -Infinity;
        for (let k = 0; k < flat.length; k += 2) {
          if (flat[k] < minX) minX = flat[k];
          if (flat[k] > maxX) maxX = flat[k];
          if (flat[k + 1] < minY) minY = flat[k + 1];
          if (flat[k + 1] > maxY) maxY = flat[k + 1];
        }
        if (
          o.pick.x >= minX &&
          o.pick.x <= maxX &&
          o.pick.y >= minY &&
          o.pick.y <= maxY &&
          pointInPoly(o.pick.x, o.pick.y, flat)
        ) {
          pickBest = z;
          picked = solid.id;
        }
      }
    }

    /* hard edges ------------------------------------------------ */
    if (o.edges && !o.wireframe) {
      for (const [ea, eb] of mesh.edges) {
        lines++;
        if (secN) {
          const ca = dot(ea, secN) - secD;
          const cb = dot(eb, secN) - secD;
          if (ca > 0 && cb > 0) continue;
        }
        const va = toView(ea);
        const vb = toView(eb);
        const s = clipSegment(va, vb);
        if (!s) continue;
        const [x1, y1] = proj(s[0]);
        const [x2, y2] = proj(s[1]);
        items.push({
          z: (s[0][2] + s[1][2]) / 2 + 0.02,
          kind: 1,
          seg: [x1, y1, x2, y2],
          color: o.wireframe
            ? isSel
              ? "rgba(245,165,36,0.95)"
              : "rgba(53,201,232,0.34)"
            : isSel
              ? "rgba(255,205,110,0.95)"
              : isHov
                ? "rgba(160,235,120,0.8)"
                : "rgba(6,9,13,0.72)",
          width: o.wireframe ? 0.75 : isSel ? 1.6 : 0.9,
        });
      }
    }
  }

  /* ---- sort & rasterise ------------------------------------------ */
  items.sort((p, q) => p.z - q.z);

  const bg = ctx.createLinearGradient(0, 0, 0, h);
  bg.addColorStop(0, "#0d1219");
  bg.addColorStop(0.55, "#0a0e14");
  bg.addColorStop(1, "#070a0e");
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, w, h);

  const glow = ctx.createRadialGradient(w * 0.5, h * 0.42, 10, w * 0.5, h * 0.42, Math.max(w, h) * 0.7);
  glow.addColorStop(0, "rgba(53,201,232,0.10)");
  glow.addColorStop(0.5, "rgba(53,201,232,0.03)");
  glow.addColorStop(1, "rgba(0,0,0,0)");
  ctx.fillStyle = glow;
  ctx.fillRect(0, 0, w, h);

  let drawn = 0;
  ctx.lineJoin = "round";
  ctx.lineCap = "round";
  for (const it of items) {
    if (it.kind === 0) {
      if (!it.pts || it.pts.length < 6) continue;
      ctx.globalAlpha = it.alpha ?? 1;
      ctx.beginPath();
      ctx.moveTo(it.pts[0], it.pts[1]);
      for (let k = 2; k < it.pts.length; k += 2) ctx.lineTo(it.pts[k], it.pts[k + 1]);
      ctx.closePath();
      if (it.color && it.color !== "rgba(0,0,0,0)") {
        ctx.fillStyle = it.color;
        ctx.fill();
      }
      drawn++;
    } else if (it.seg && it.seg.length === 4) {
      ctx.globalAlpha = 1;
      ctx.strokeStyle = it.color ?? "#fff";
      ctx.lineWidth = it.width ?? 1;
      ctx.beginPath();
      ctx.moveTo(it.seg[0], it.seg[1]);
      ctx.lineTo(it.seg[2], it.seg[3]);
      ctx.stroke();
    }
  }
  ctx.globalAlpha = 1;

  /* ---- overlay: GD&T frames -------------------------------------- */
  if (o.gdt) drawGdt(ctx, part, view, proj, cx, cy, f);
  /* ---- overlay: dimensions --------------------------------------- */
  if (o.dims) drawDims(ctx, part, view, proj);
  /* ---- gizmo ------------------------------------------------------ */
  if (o.gizmo) drawGizmo(ctx, view, w, h);
  /* ---- stress legend --------------------------------------------- */
  if (o.stress) drawLegend(ctx, w, h);
  /* ---- frame time graph ------------------------------------------ */
  drawHistory(ctx, o.history, w, h, o.backend);

  return {
    tris,
    drawn,
    culled,
    clipped,
    lines,
    ms: performance.now() - t0,
    picked,
  };
}

const add3 = (a: Vec3, b: Vec3): Vec3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];

function clipSegment(a: Vec3, b: Vec3): [Vec3, Vec3] | null {
  const lim = -NEAR;
  let za = a[2];
  let zb = b[2];
  if (za > lim && zb > lim) return null;
  let pa = a;
  let pb = b;
  if (za > lim) {
    const t = (lim - za) / (zb - za);
    pa = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, lim];
    za = lim;
  }
  if (zb > lim) {
    const t = (lim - za) / (zb - za);
    pb = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, lim];
  }
  return [pa, pb];
}

function shadeTri(
  n: Vec3,
  world: Vec3[],
  base: Vec3,
  spec: number,
  eye: Vec3,
  facing: boolean,
  xray: boolean,
  pbr = false,
): string {
  const cent: Vec3 = [
    (world[0][0] + world[1][0] + world[2][0]) / world.length,
    (world[0][1] + world[1][1] + world[2][1]) / world.length,
    (world[0][2] + world[1][2] + world[2][2]) / world.length,
  ];
  const vd = norm(sub(eye, cent));
  const nn = facing ? n : ([-n[0], -n[1], -n[2]] as Vec3);
  const key = Math.max(0, dot(nn, LIGHT_KEY));
  const fill = Math.max(0, dot(nn, LIGHT_FILL)) * 0.4;
  const ndv = Math.max(0, dot(nn, vd));
  const fres = Math.pow(1 - ndv, pbr ? 4.2 : 2.4);
  const rim = fres * (pbr ? 0.5 : 0.3);
  const half = norm(add3(LIGHT_KEY, vd));
  const f0 = pbr ? 0.06 + spec * 0.7 : spec * 0.4;
  const sp =
    Math.pow(Math.max(0, dot(nn, half)), pbr ? 72 : 28) * f0 * (pbr ? 1 + fres * 1.6 : 1);
  const amb = pbr ? 0.26 : 0.3;
  const k = amb + key * (pbr ? 0.9 : 0.78) + fill + rim + (xray ? 0.18 : 0);
  const r = Math.min(255, base[0] * k + sp * 214);
  const g = Math.min(255, base[1] * k + sp * 220);
  const b = Math.min(255, base[2] * k + sp * 230);
  return `rgb(${r | 0},${g | 0},${b | 0})`;
}

export function stressAt(p: Vec3, part: PartDef): number {
  const dl = Math.hypot(p[0] - part.load[0], p[1] - part.load[1], p[2] - part.load[2]);
  const ds = Math.hypot(p[0] - part.support[0], p[1] - part.support[1], p[2] - part.support[2]);
  const v = 0.72 * Math.exp(-dl / 1.25) + 0.42 * Math.exp(-ds / 2.4);
  return Math.max(0, Math.min(1, v));
}

function drawGdt(
  ctx: CanvasRenderingContext2D,
  part: PartDef,
  view: Mat4,
  proj: (v: Vec3) => [number, number],
  _cx: number,
  _cy: number,
  _f: number,
) {
  ctx.save();
  ctx.font = "600 11px 'IBM Plex Mono', monospace";
  ctx.textBaseline = "middle";
  for (const g of part.gdt) {
    const pa = transformPoint(view, g.anchor);
    const pe = transformPoint(view, g.elbow);
    const pt = transformPoint(view, g.tail);
    if (pa[2] > -NEAR || pe[2] > -NEAR) continue;
    const [ax, ay] = proj(pa);
    const [ex, ey] = proj(pe);
    const [tx, ty] = proj(pt);
    ctx.strokeStyle = "rgba(245,165,36,0.85)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(ax, ay);
    ctx.lineTo(ex, ey);
    ctx.lineTo(tx, ty);
    ctx.stroke();
    ctx.fillStyle = "rgba(245,165,36,0.95)";
    ctx.beginPath();
    ctx.arc(ax, ay, 2.4, 0, Math.PI * 2);
    ctx.fill();
    const wpx = ctx.measureText(g.frame).width + 26;
    ctx.fillStyle = "rgba(10,14,20,0.92)";
    ctx.strokeStyle = "rgba(245,165,36,0.9)";
    ctx.fillRect(tx, ty - 10, wpx, 20);
    ctx.strokeRect(tx, ty - 10, wpx, 20);
    ctx.beginPath();
    ctx.moveTo(tx + 17, ty - 10);
    ctx.lineTo(tx + 17, ty + 10);
    ctx.stroke();
    ctx.fillStyle = "#f5a524";
    ctx.textAlign = "center";
    ctx.fillText(g.datum, tx + 8.5, ty + 1);
    ctx.textAlign = "left";
    ctx.fillStyle = "#ffd9a0";
    ctx.fillText(g.frame, tx + 23, ty + 1);
  }
  ctx.restore();
}

function drawDims(ctx: CanvasRenderingContext2D, part: PartDef, view: Mat4, proj: (v: Vec3) => [number, number]) {
  ctx.save();
  ctx.font = "500 10.5px 'IBM Plex Mono', monospace";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  for (const d of part.dims) {
    const pa = transformPoint(view, d.a);
    const pb = transformPoint(view, d.b);
    if (pa[2] > -NEAR || pb[2] > -NEAR) continue;
    const [x1, y1] = proj(pa);
    const [x2, y2] = proj(pb);
    ctx.strokeStyle = "rgba(53,201,232,0.8)";
    ctx.lineWidth = 1;
    ctx.setLineDash([]);
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.stroke();
    const ang = Math.atan2(y2 - y1, x2 - x1);
    for (const [px, py, dir] of [
      [x1, y1, 1],
      [x2, y2, -1],
    ] as [number, number, number][]) {
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(px + Math.cos(ang + dir * 2.6) * 8, py + Math.sin(ang + dir * 2.6) * 8);
      ctx.moveTo(px, py);
      ctx.lineTo(px + Math.cos(ang - dir * 2.6) * 8, py + Math.sin(ang - dir * 2.6) * 8);
      ctx.stroke();
    }
    const mx = (x1 + x2) / 2;
    const my = (y1 + y2) / 2;
    const tw = ctx.measureText(d.label).width + 10;
    ctx.fillStyle = "rgba(8,12,17,0.9)";
    ctx.fillRect(mx - tw / 2, my - 8, tw, 16);
    ctx.strokeStyle = "rgba(53,201,232,0.55)";
    ctx.strokeRect(mx - tw / 2, my - 8, tw, 16);
    ctx.fillStyle = "#9be8f7";
    ctx.fillText(d.label, mx, my + 0.5);
  }
  ctx.restore();
}

function drawGizmo(ctx: CanvasRenderingContext2D, view: Mat4, w: number, h: number) {
  const ox = w - 56;
  const oy = h - 56;
  const r = 30;
  ctx.save();
  ctx.strokeStyle = "rgba(120,150,175,0.20)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(ox, oy, r + 8, 0, Math.PI * 2);
  ctx.stroke();
  const axes: [Vec3, string, string][] = [
    [[1, 0, 0], "#f5a524", "X"],
    [[0, 1, 0], "#8ede63", "Y"],
    [[0, 0, 1], "#35c9e8", "Z"],
  ];
  const drawnAxes = axes
    .map(([v, color, label]) => {
      const dv = transformDir(view, v);
      return { color, label, x: ox + dv[0] * r, y: oy - dv[1] * r, z: dv[2] };
    })
    .sort((a, b) => a.z - b.z);
  for (const a of drawnAxes) {
    ctx.strokeStyle = a.color;
    ctx.fillStyle = a.color;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.moveTo(ox, oy);
    ctx.lineTo(a.x, a.y);
    ctx.stroke();
    ctx.font = "600 10px 'IBM Plex Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(a.label, a.x + (a.x - ox) * 0.22, a.y + (a.y - oy) * 0.22);
  }
  ctx.restore();
}

function drawLegend(ctx: CanvasRenderingContext2D, w: number, h: number) {
  const bw = 168;
  const bh = 9;
  const x = w - bw - 26;
  const y = h * 0.55;
  ctx.save();
  ctx.font = "500 9.5px 'IBM Plex Mono', monospace";
  ctx.fillStyle = "rgba(10,14,20,0.82)";
  ctx.fillRect(x - 12, y - 24, bw + 24, 60);
  ctx.strokeStyle = "rgba(120,150,175,0.25)";
  ctx.strokeRect(x - 12, y - 24, bw + 24, 60);
  const grad = ctx.createLinearGradient(x, 0, x + bw, 0);
  for (const [t, c] of RAMP) grad.addColorStop(t, `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`);
  ctx.fillStyle = grad;
  ctx.fillRect(x, y, bw, bh);
  ctx.fillStyle = "#cfe3ee";
  ctx.textAlign = "left";
  ctx.fillText("σ vM  0 MPa", x, y - 10);
  ctx.textAlign = "right";
  ctx.fillText("412 MPa", x + bw, y - 10);
  ctx.textAlign = "left";
  ctx.fillStyle = "#7d8fa1";
  ctx.fillText("fem_solver · P1 tetra · 12 480 DOF", x, y + 24);
  ctx.restore();
}

function drawHistory(
  ctx: CanvasRenderingContext2D,
  hist: number[],
  w: number,
  _h: number,
  backend: string,
) {
  const bw = 148;
  const bh = 34;
  const x = w - bw - 26;
  const y = 24;
  ctx.save();
  ctx.fillStyle = "rgba(8,12,17,0.6)";
  ctx.fillRect(x - 10, y - 12, bw + 20, bh + 30);
  ctx.strokeStyle = "rgba(120,150,175,0.18)";
  ctx.strokeRect(x - 10, y - 12, bw + 20, bh + 30);
  ctx.font = "500 9px 'IBM Plex Mono', monospace";
  ctx.fillStyle = "#7d8fa1";
  ctx.textAlign = "left";
  ctx.fillText("FRAME TIME (ms)", x - 4, y - 2);
  const target = 16.7;
  const max = Math.max(target * 1.6, ...hist);
  ctx.strokeStyle = "rgba(142,222,99,0.35)";
  ctx.setLineDash([3, 3]);
  ctx.beginPath();
  const ty = y + bh - (target / max) * bh;
  ctx.moveTo(x - 4, ty);
  ctx.lineTo(x + bw + 4, ty);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.beginPath();
  for (let i = 0; i < hist.length; i++) {
    const px = x + (i / Math.max(1, hist.length - 1)) * bw;
    const py = y + bh - Math.min(1, hist[i] / max) * bh;
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  }
  ctx.strokeStyle = backend === "dx12" ? "#35c9e8" : backend === "metal" ? "#8ede63" : "#f5a524";
  ctx.lineWidth = 1.3;
  ctx.stroke();
  ctx.lineTo(x + bw, y + bh);
  ctx.lineTo(x, y + bh);
  ctx.closePath();
  ctx.globalAlpha = 0.12;
  ctx.fillStyle = ctx.strokeStyle;
  ctx.fill();
  ctx.globalAlpha = 1;
  ctx.restore();
}
