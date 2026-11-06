/**
 * Level-set geometry layer.
 *
 * All domains are described by a signed distance function phi(x,y) < 0 inside.
 * This is the common geometric substrate used by
 *   - the DistMesh-style unstructured FVM mesher (boundary projection),
 *   - the Eulerian-SPH particle relaxation (level-set "surface bounding",
 *     Wang, Haidn & Hu 2024), and
 *   - the SPH <-> FVM coupling region bookkeeping (Zhang et al. 2025).
 */

export type PatchKind =
  | "inlet"
  | "outlet"
  | "wall"
  | "body"
  | "farfield"
  | "symmetry";

export const PATCH_ORDER: PatchKind[] = [
  "inlet",
  "outlet",
  "wall",
  "body",
  "farfield",
  "symmetry",
];

export const PATCH_COLOR: Record<PatchKind, string> = {
  inlet: "#38bdf8",
  outlet: "#f472b6",
  wall: "#94a3b8",
  body: "#fbbf24",
  farfield: "#a78bfa",
  symmetry: "#34d399",
};

export const PATCH_LABEL: Record<PatchKind, string> = {
  inlet: "Inlet (Dirichlet U)",
  outlet: "Outlet (zero-gradient)",
  wall: "No-slip wall",
  body: "Body / no-slip",
  farfield: "Far-field (non-reflecting)",
  symmetry: "Symmetry / slip",
};

export interface Vec2 {
  x: number;
  y: number;
}

export interface Geometry {
  id: string;
  name: string;
  blurb: string;
  bbox: [number, number, number, number]; // xmin ymin xmax ymax
  /** signed distance, negative inside the fluid domain */
  sdf(x: number, y: number): number;
  /** patch tag for the boundary point closest to (x,y) */
  patchAt(x: number, y: number): PatchKind;
  /** distance to the feature that drives local refinement (walls / body) */
  refineDist(x: number, y: number): number;
  /** corners that must be preserved exactly by the mesher */
  fixedPoints: [number, number][];
  /** exact perimeter of the domain boundary - used to validate sum|A_iS| */
  perimeter: number;
  /** default Lagrangian (SPH) core region for the coupled mode */
  core: { x: number; y: number; r: number };
  /** optional secondary interface drawn for multiphase cases */
  interfaceCircle?: { x: number; y: number; r: number };
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */

export function dBox(
  x: number,
  y: number,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
): number {
  const dx = Math.max(x0 - x, x - x1);
  const dy = Math.max(y0 - y, y - y1);
  const outside = Math.hypot(Math.max(dx, 0), Math.max(dy, 0));
  const inside = Math.min(Math.max(dx, dy), 0);
  return outside + inside;
}

export function dCircle(
  x: number,
  y: number,
  cx: number,
  cy: number,
  r: number,
): number {
  return Math.hypot(x - cx, y - cy) - r;
}

export function dSegment(
  px: number,
  py: number,
  ax: number,
  ay: number,
  bx: number,
  by: number,
): number {
  const vx = bx - ax;
  const vy = by - ay;
  const wx = px - ax;
  const wy = py - ay;
  const L2 = vx * vx + vy * vy || 1e-30;
  let t = (wx * vx + wy * vy) / L2;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return Math.hypot(wx - t * vx, wy - t * vy);
}

/** signed distance to a closed polygon (negative inside) */
export function dPolygon(
  x: number,
  y: number,
  poly: Float64Array,
): number {
  const n = poly.length / 2;
  let best = Infinity;
  let inside = false;
  for (let i = 0, j = n - 1; i < n; j = i++) {
    const ax = poly[2 * i];
    const ay = poly[2 * i + 1];
    const bx = poly[2 * j];
    const by = poly[2 * j + 1];
    const d = dSegment(x, y, ax, ay, bx, by);
    if (d < best) best = d;
    if (ay > y !== by > y && x < ((bx - ax) * (y - ay)) / (by - ay) + ax)
      inside = !inside;
  }
  return inside ? -best : best;
}

export function polygonPerimeter(poly: Float64Array): number {
  const n = poly.length / 2;
  let p = 0;
  for (let i = 0, j = n - 1; i < n; j = i++)
    p += Math.hypot(poly[2 * i] - poly[2 * j], poly[2 * i + 1] - poly[2 * j + 1]);
  return p;
}

/** numerical gradient of a (possibly non-exact) level set */
export function sdfGrad(
  g: { sdf(x: number, y: number): number },
  x: number,
  y: number,
  eps: number,
): [number, number] {
  const dx = (g.sdf(x + eps, y) - g.sdf(x - eps, y)) / (2 * eps);
  const dy = (g.sdf(x, y + eps) - g.sdf(x, y - eps)) / (2 * eps);
  return [dx, dy];
}

/** project a point onto the iso-contour phi = target (Newton on the level set) */
export function projectToIso(
  g: { sdf(x: number, y: number): number },
  x: number,
  y: number,
  target: number,
  eps: number,
  iters = 2,
): [number, number] {
  let px = x;
  let py = y;
  for (let k = 0; k < iters; k++) {
    const d = g.sdf(px, py) - target;
    const [gx, gy] = sdfGrad(g, px, py, eps);
    const g2 = gx * gx + gy * gy;
    if (g2 < 1e-20) break;
    px -= (d * gx) / g2;
    py -= (d * gy) / g2;
  }
  return [px, py];
}

function nearestKind(
  entries: [number, PatchKind][],
): PatchKind {
  let best = Infinity;
  let kind: PatchKind = "wall";
  for (const [d, k] of entries) {
    if (d < best) {
      best = d;
      kind = k;
    }
  }
  return kind;
}

/* ------------------------------------------------------------------ */
/* NACA 4-digit section                                                */
/* ------------------------------------------------------------------ */

export function nacaPolygon(
  thickness = 0.12,
  camber = 0,
  camberPos = 0.4,
  alphaDeg = 6,
  n = 120,
): Float64Array {
  const pts: number[] = [];
  const ca = Math.cos((-alphaDeg * Math.PI) / 180);
  const sa = Math.sin((-alphaDeg * Math.PI) / 180);
  const yt = (xc: number) =>
    (thickness / 0.2) *
    (0.2969 * Math.sqrt(xc) -
      0.126 * xc -
      0.3516 * xc * xc +
      0.2843 * xc ** 3 -
      0.1036 * xc ** 4);
  const camberLine = (xc: number): [number, number] => {
    if (camber === 0) return [0, 0];
    const m = camber;
    const p = camberPos;
    if (xc < p)
      return [(m / (p * p)) * (2 * p * xc - xc * xc), ((2 * m) / (p * p)) * (p - xc)];
    return [
      (m / ((1 - p) * (1 - p))) * (1 - 2 * p + 2 * p * xc - xc * xc),
      ((2 * m) / ((1 - p) * (1 - p))) * (p - xc),
    ];
  };
  const push = (xc: number, upper: boolean) => {
    const t = yt(xc);
    const [yc, dyc] = camberLine(xc);
    const th = Math.atan(dyc);
    const xx = upper ? xc - t * Math.sin(th) : xc + t * Math.sin(th);
    const yy = upper ? yc + t * Math.cos(th) : yc - t * Math.cos(th);
    const sx = xx - 0.3;
    pts.push(sx * ca - yy * sa + 0.3, sx * sa + yy * ca);
  };
  for (let i = 0; i <= n; i++) {
    const beta = (Math.PI * i) / n;
    push(0.5 * (1 - Math.cos(beta)), true);
  }
  for (let i = n - 1; i > 0; i--) {
    const beta = (Math.PI * i) / n;
    push(0.5 * (1 - Math.cos(beta)), false);
  }
  return Float64Array.from(pts);
}

/* ------------------------------------------------------------------ */
/* geometry catalogue                                                  */
/* ------------------------------------------------------------------ */

function cylinderFlow(): Geometry {
  const L = 4.0;
  const H = 1.6;
  const cx = 1.1;
  const cy = 0.8;
  const r = 0.25;
  return {
    id: "cylinder",
    name: "Cylinder in channel",
    blurb: "Von-Karman street benchmark · inlet/outlet + no-slip body",
    bbox: [0, 0, L, H],
    sdf: (x, y) => Math.max(dBox(x, y, 0, 0, L, H), -dCircle(x, y, cx, cy, r)),
    patchAt: (x, y) =>
      nearestKind([
        [Math.abs(x), "inlet"],
        [Math.abs(L - x), "outlet"],
        [Math.abs(y), "wall"],
        [Math.abs(H - y), "wall"],
        [Math.abs(dCircle(x, y, cx, cy, r)), "body"],
      ]),
    refineDist: (x, y) =>
      Math.min(
        Math.abs(dCircle(x, y, cx, cy, r)),
        Math.abs(dBox(x, y, 0, 0, L, H)) * 1.6,
      ),
    fixedPoints: [
      [0, 0],
      [L, 0],
      [L, H],
      [0, H],
    ],
    perimeter: 2 * (L + H) + 2 * Math.PI * r,
    core: { x: cx, y: cy, r: 0.62 },
  };
}

function backwardStep(): Geometry {
  const L = 4.0;
  const H = 1.0;
  const sx = 1.0;
  const sy = 0.4;
  return {
    id: "step",
    name: "Backward-facing step",
    blurb: "Separated shear layer · re-attachment boundary layer refinement",
    bbox: [0, 0, L, H],
    sdf: (x, y) =>
      Math.max(dBox(x, y, 0, 0, L, H), -dBox(x, y, -1, -1, sx, sy)),
    patchAt: (x, y) =>
      nearestKind([
        [y > sy ? Math.abs(x) : 1e9, "inlet"],
        [Math.abs(L - x), "outlet"],
        [Math.abs(y), "wall"],
        [Math.abs(H - y), "wall"],
        [Math.abs(dBox(x, y, -1, -1, sx, sy)), "wall"],
      ]),
    refineDist: (x, y) =>
      Math.min(
        Math.abs(dBox(x, y, -1, -1, sx, sy)),
        Math.abs(dBox(x, y, 0, 0, L, H)) * 2.0,
      ),
    fixedPoints: [
      [0, sy],
      [0, H],
      [L, 0],
      [L, H],
      [sx, sy],
      [sx, 0],
    ],
    perimeter: 2 * L + 2 * H,
    core: { x: 1.35, y: 0.45, r: 0.55 },
  };
}

function airfoilFlow(): Geometry {
  const poly = nacaPolygon(0.12, 0.02, 0.4, 8, 140);
  const per = polygonPerimeter(poly);
  const x0 = -1.2;
  const y0 = -1.1;
  const x1 = 2.4;
  const y1 = 1.1;
  return {
    id: "airfoil",
    name: "NACA 2412 · far-field",
    blurb: "Body-fitted section at 8° AoA · non-reflecting outer boundary",
    bbox: [x0, y0, x1, y1],
    sdf: (x, y) => Math.max(dBox(x, y, x0, y0, x1, y1), -dPolygon(x, y, poly)),
    patchAt: (x, y) =>
      nearestKind([
        [Math.abs(x - x0), "inlet"],
        [Math.abs(x1 - x), "outlet"],
        [Math.abs(y - y0), "farfield"],
        [Math.abs(y1 - y), "farfield"],
        [Math.abs(dPolygon(x, y, poly)), "body"],
      ]),
    refineDist: (x, y) => Math.abs(dPolygon(x, y, poly)),
    fixedPoints: [
      [x0, y0],
      [x1, y0],
      [x1, y1],
      [x0, y1],
    ],
    perimeter: 2 * (x1 - x0 + y1 - y0) + per,
    core: { x: 0.35, y: 0.0, r: 0.85 },
  };
}

function nozzle(): Geometry {
  const L = 3.0;
  const xT = 1.3;
  const R = (x: number) => 0.22 + 0.055 * (x - xT) * (x - xT) + 0.02 * x;
  const g: Geometry = {
    id: "nozzle",
    name: "Laval nozzle",
    blurb: "Compressible duct · supersonic outflow, far-field exit",
    bbox: [0, -0.62, L, 0.62],
    sdf: (x, y) => Math.max(Math.abs(y) - R(x), -x, x - L),
    patchAt: (x, y) =>
      nearestKind([
        [Math.abs(x), "inlet"],
        [Math.abs(L - x), "outlet"],
        [Math.abs(Math.abs(y) - R(x)), "wall"],
      ]),
    refineDist: (x, y) => Math.abs(Math.abs(y) - R(x)),
    fixedPoints: [
      [0, R(0)],
      [0, -R(0)],
      [L, R(L)],
      [L, -R(L)],
    ],
    perimeter: 0,
    core: { x: xT, y: 0, r: 0.5 },
  };
  // arc-length of both walls + inlet/outlet faces
  let p = 2 * R(0) + 2 * R(L);
  const N = 400;
  for (let i = 0; i < N; i++) {
    const xa = (i * L) / N;
    const xb = ((i + 1) * L) / N;
    p += 2 * Math.hypot(xb - xa, R(xb) - R(xa));
  }
  g.perimeter = p;
  return g;
}

function cavity(): Geometry {
  const S = 1.0;
  return {
    id: "cavity",
    name: "Lid-driven cavity",
    blurb: "Closed domain · three no-slip walls + moving lid",
    bbox: [0, 0, S, S],
    sdf: (x, y) => dBox(x, y, 0, 0, S, S),
    patchAt: (x, y) =>
      nearestKind([
        [Math.abs(S - y), "inlet"],
        [Math.abs(x), "wall"],
        [Math.abs(S - x), "wall"],
        [Math.abs(y), "wall"],
      ]),
    refineDist: (x, y) => Math.abs(dBox(x, y, 0, 0, S, S)),
    fixedPoints: [
      [0, 0],
      [S, 0],
      [S, S],
      [0, S],
    ],
    perimeter: 4 * S,
    core: { x: 0.5, y: 0.5, r: 0.34 },
  };
}

function bubble(): Geometry {
  const S = 3.0;
  return {
    id: "bubble",
    name: "High-pressure bubble",
    blurb: "Strongly compressible multiphase · adaptive Lagrangian core",
    bbox: [0, 0, S, S],
    sdf: (x, y) => dBox(x, y, 0, 0, S, S),
    patchAt: () => "farfield",
    refineDist: (x, y) =>
      Math.min(
        Math.abs(dBox(x, y, 0, 0, S, S)) * 2.2,
        Math.abs(dCircle(x, y, 1.5, 1.5, 0.45)),
      ),
    fixedPoints: [
      [0, 0],
      [S, 0],
      [S, S],
      [0, S],
    ],
    perimeter: 4 * S,
    core: { x: 1.5, y: 1.5, r: 0.95 },
    interfaceCircle: { x: 1.5, y: 1.5, r: 0.45 },
  };
}

export const GEOMETRIES: Geometry[] = [
  cylinderFlow(),
  backwardStep(),
  airfoilFlow(),
  nozzle(),
  cavity(),
  bubble(),
];

export function getGeometry(id: string): Geometry {
  return GEOMETRIES.find((g) => g.id === id) ?? GEOMETRIES[0];
}

/* ------------------------------------------------------------------ */
/* marching squares outline of phi = 0                                 */
/* ------------------------------------------------------------------ */

export function isoContour(
  g: Geometry,
  n = 320,
  level = 0,
): Float64Array[] {
  const [x0, y0, x1, y1] = g.bbox;
  const pad = 0.06 * Math.max(x1 - x0, y1 - y0);
  const ax = x0 - pad;
  const ay = y0 - pad;
  const bx = x1 + pad;
  const by = y1 + pad;
  const m = Math.max(16, Math.round((n * (by - ay)) / (bx - ax)));
  const dx = (bx - ax) / n;
  const dy = (by - ay) / m;
  const f = new Float64Array((n + 1) * (m + 1));
  for (let j = 0; j <= m; j++)
    for (let i = 0; i <= n; i++)
      f[j * (n + 1) + i] = g.sdf(ax + i * dx, ay + j * dy) - level;

  const segs: Float64Array[] = [];
  const interp = (
    xa: number,
    ya: number,
    va: number,
    xb: number,
    yb: number,
    vb: number,
  ): [number, number] => {
    const t = va / (va - vb);
    return [xa + t * (xb - xa), ya + t * (yb - ya)];
  };
  for (let j = 0; j < m; j++) {
    for (let i = 0; i < n; i++) {
      const v0 = f[j * (n + 1) + i];
      const v1 = f[j * (n + 1) + i + 1];
      const v2 = f[(j + 1) * (n + 1) + i + 1];
      const v3 = f[(j + 1) * (n + 1) + i];
      const X0 = ax + i * dx;
      const Y0 = ay + j * dy;
      const X1 = X0 + dx;
      const Y1 = Y0 + dy;
      const idx =
        (v0 < 0 ? 1 : 0) | (v1 < 0 ? 2 : 0) | (v2 < 0 ? 4 : 0) | (v3 < 0 ? 8 : 0);
      if (idx === 0 || idx === 15) continue;
      const pts: [number, number][] = [];
      if ((v0 < 0) !== (v1 < 0)) pts.push(interp(X0, Y0, v0, X1, Y0, v1));
      if ((v1 < 0) !== (v2 < 0)) pts.push(interp(X1, Y0, v1, X1, Y1, v2));
      if ((v2 < 0) !== (v3 < 0)) pts.push(interp(X1, Y1, v2, X0, Y1, v3));
      if ((v3 < 0) !== (v0 < 0)) pts.push(interp(X0, Y1, v3, X0, Y0, v0));
      for (let k = 0; k + 1 < pts.length; k += 2)
        segs.push(
          Float64Array.from([pts[k][0], pts[k][1], pts[k + 1][0], pts[k + 1][1]]),
        );
    }
  }
  return segs;
}
