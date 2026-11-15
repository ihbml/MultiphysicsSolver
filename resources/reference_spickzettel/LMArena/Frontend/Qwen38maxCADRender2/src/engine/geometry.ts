import {
  type Mat4,
  type Vec3,
  mat4Identity,
  mat4Mul,
  mat4RotationX,
  mat4RotationY,
  mat4RotationZ,
  mat4Translation,
  transformPoint,
  transformDir,
  norm,
  cross,
} from "./math3d";

export type Tri = [Vec3, Vec3, Vec3];

export interface MeshBuilder {
  tris: Tri[];
  edges: [Vec3, Vec3][];
  /** unit normals parallel to `tris`, baked in world space */
  normals: Vec3[];
}

export function newBuilder(): MeshBuilder {
  return { tris: [], edges: [], normals: [] };
}

const sub3 = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];

function pushTri(b: MeshBuilder, a: Vec3, c: Vec3, d: Vec3) {
  b.tris.push([a, c, d]);
  b.normals.push(norm(cross(sub3(c, a), sub3(d, a))));
}

/** Transform = translate * rotZ * rotY * rotX */
export function tr(pos: Vec3, rot: Vec3 = [0, 0, 0]): Mat4 {
  let m = mat4Translation(pos);
  if (rot[2]) m = mat4Mul(m, mat4RotationZ(rot[2]));
  if (rot[1]) m = mat4Mul(m, mat4RotationY(rot[1]));
  if (rot[0]) m = mat4Mul(m, mat4RotationX(rot[0]));
  return m;
}

const H = Math.PI / 2;
/** Rotation mapping a primitive's local +Y onto a world axis. */
export function axisXform(axis: "x" | "y" | "z", pos: Vec3): Mat4 {
  if (axis === "x") return mat4Mul(mat4Translation(pos), mat4RotationZ(-H));
  if (axis === "z") return mat4Mul(mat4Translation(pos), mat4RotationX(H));
  return mat4Translation(pos);
}

export function applyXform(b: MeshBuilder, m: Mat4): MeshBuilder {
  const out = newBuilder();
  for (let i = 0; i < b.tris.length; i++) {
    const [a, c, d] = b.tris[i];
    out.tris.push([transformPoint(m, a), transformPoint(m, c), transformPoint(m, d)]);
    out.normals.push(norm(transformDir(m, b.normals[i])));
  }
  for (const [a, c] of b.edges) out.edges.push([transformPoint(m, a), transformPoint(m, c)]);
  return out;
}

const seg1 = (n: number) => Math.max(1, Math.min(9, Math.round(n)));

/**
 * Axis-aligned box centred on the transform origin. Faces are subdivided into
 * ~`cell`-sized quads so the depth sort stays stable, while only the 12 true
 * silhouette edges are emitted as hard edges.
 */
export function boxMesh(
  sx: number,
  sy: number,
  sz: number,
  m: Mat4 = mat4Identity(),
  cell = 0.7,
): MeshBuilder {
  const b = newBuilder();
  const x = sx / 2;
  const y = sy / 2;
  const z = sz / 2;
  const faces: { o: Vec3; u: Vec3; v: Vec3 }[] = [
    { o: [-x, -y, -z], u: [2 * x, 0, 0], v: [0, 0, 2 * z] }, // -y
    { o: [-x, y, -z], u: [0, 0, 2 * z], v: [2 * x, 0, 0] }, // +y
    { o: [-x, y, -z], u: [2 * x, 0, 0], v: [0, -2 * y, 0] }, // -z
    { o: [x, y, -z], u: [0, 0, 2 * z], v: [0, -2 * y, 0] }, // +x
    { o: [-x, -y, z], u: [2 * x, 0, 0], v: [0, 2 * y, 0] }, // +z
    { o: [-x, -y, -z], u: [0, 0, 2 * z], v: [0, 2 * y, 0] }, // -x
  ];
  const corners: Vec3[] = [
    [-x, -y, -z],
    [x, -y, -z],
    [x, -y, z],
    [-x, -y, z],
    [-x, y, -z],
    [x, y, -z],
    [x, y, z],
    [-x, y, z],
  ];
  for (const f of faces) {
    const nu = seg1(Math.hypot(...f.u) / cell);
    const nv = seg1(Math.hypot(...f.v) / cell);
    for (let i = 0; i < nu; i++) {
      for (let j = 0; j < nv; j++) {
        const p = (ii: number, jj: number): Vec3 => [
          f.o[0] + (f.u[0] * ii) / nu + (f.v[0] * jj) / nv,
          f.o[1] + (f.u[1] * ii) / nu + (f.v[1] * jj) / nv,
          f.o[2] + (f.u[2] * ii) / nu + (f.v[2] * jj) / nv,
        ];
        pushTri(b, p(i, j), p(i + 1, j), p(i + 1, j + 1));
        pushTri(b, p(i, j), p(i + 1, j + 1), p(i, j + 1));
      }
    }
  }
  const edgeIdx: [number, number][] = [
    [0, 1],
    [1, 2],
    [2, 3],
    [3, 0],
    [4, 5],
    [5, 6],
    [6, 7],
    [7, 4],
    [0, 4],
    [1, 5],
    [2, 6],
    [3, 7],
  ];
  for (const [i, j] of edgeIdx) b.edges.push([corners[i], corners[j]]);
  return applyXform(b, m);
}

function capRings(r: number, cell: number) {
  return Math.max(1, Math.min(6, Math.round(r / cell)));
}

/** Solid cylinder along +Y with its base at y = 0. */
export function cylMesh(
  r: number,
  h: number,
  segs = 28,
  m: Mat4 = mat4Identity(),
  capTop = true,
  capBottom = true,
  cell = 0.62,
): MeshBuilder {
  const b = newBuilder();
  const ring = (rad: number, y: number) => {
    const pts: Vec3[] = [];
    for (let i = 0; i < segs; i++) {
      const a = (i / segs) * Math.PI * 2;
      pts.push([Math.cos(a) * rad, y, Math.sin(a) * rad]);
    }
    return pts;
  };
  const bot = ring(r, 0);
  const top = ring(r, h);
  for (let i = 0; i < segs; i++) {
    const j = (i + 1) % segs;
    pushTri(b, bot[i], top[j], bot[j]);
    pushTri(b, bot[i], top[i], top[j]);
    b.edges.push([bot[i], bot[j]]);
    b.edges.push([top[i], top[j]]);
  }
  const rings = capRings(r, cell);
  const radii: number[] = [];
  for (let k = 1; k <= rings; k++) radii.push((r * k) / rings);
  if (capBottom) {
    const c: Vec3 = [0, 0, 0];
    const first = ring(radii[0], 0);
    for (let i = 0; i < segs; i++) pushTri(b, c, first[i], first[(i + 1) % segs]);
    for (let k = 1; k < rings; k++) {
      const a = ring(radii[k - 1], 0);
      const bb = ring(radii[k], 0);
      for (let i = 0; i < segs; i++) {
        const j = (i + 1) % segs;
        pushTri(b, bb[i], bb[j], a[j]);
        pushTri(b, bb[i], a[j], a[i]);
      }
    }
  }
  if (capTop) {
    const c: Vec3 = [0, h, 0];
    const first = ring(radii[0], h);
    for (let i = 0; i < segs; i++) pushTri(b, c, first[(i + 1) % segs], first[i]);
    for (let k = 1; k < rings; k++) {
      const a = ring(radii[k - 1], h);
      const bb = ring(radii[k], h);
      for (let i = 0; i < segs; i++) {
        const j = (i + 1) % segs;
        pushTri(b, bb[i], a[j], bb[j]);
        pushTri(b, bb[i], a[i], a[j]);
      }
    }
  }
  return applyXform(b, m);
}

/** Hollow tube: annular caps plus inner and outer walls, along +Y from y = 0. */
export function tubeMesh(
  rOut: number,
  rIn: number,
  h: number,
  segs = 28,
  m: Mat4 = mat4Identity(),
  capTop = true,
  capBottom = true,
  cell = 0.62,
): MeshBuilder {
  const b = newBuilder();
  const ring = (rad: number, y: number) => {
    const pts: Vec3[] = [];
    for (let i = 0; i < segs; i++) {
      const a = (i / segs) * Math.PI * 2;
      pts.push([Math.cos(a) * rad, y, Math.sin(a) * rad]);
    }
    return pts;
  };
  const ob = ring(rOut, 0);
  const ot = ring(rOut, h);
  const ib = ring(rIn, 0);
  const it = ring(rIn, h);
  for (let i = 0; i < segs; i++) {
    const j = (i + 1) % segs;
    pushTri(b, ob[i], ot[j], ob[j]);
    pushTri(b, ob[i], ot[i], ot[j]);
    pushTri(b, ib[i], ib[j], it[j]);
    pushTri(b, ib[i], it[j], it[i]);
    b.edges.push([ob[i], ob[j]]);
    b.edges.push([ot[i], ot[j]]);
    b.edges.push([ib[i], ib[j]]);
    b.edges.push([it[i], it[j]]);
  }
  const rings = capRings(rOut - rIn, cell);
  const emitCap = (y: number, top: boolean) => {
    const radii: number[] = [];
    for (let k = 0; k <= rings; k++) radii.push(rIn + ((rOut - rIn) * k) / rings);
    for (let k = 0; k < rings; k++) {
      const a = ring(radii[k], y);
      const bb = ring(radii[k + 1], y);
      for (let i = 0; i < segs; i++) {
        const j = (i + 1) % segs;
        if (top) {
          pushTri(b, bb[i], a[j], bb[j]);
          pushTri(b, bb[i], a[i], a[j]);
        } else {
          pushTri(b, bb[i], bb[j], a[j]);
          pushTri(b, bb[i], a[j], a[i]);
        }
      }
    }
  };
  if (capTop) emitCap(h, true);
  if (capBottom) emitCap(0, false);
  return applyXform(b, m);
}

/** Triangular prism (gusset / rib). Points are given in the XY plane, extruded along Z. */
export function wedgeMesh(
  p0: [number, number],
  p1: [number, number],
  p2: [number, number],
  depth: number,
  m: Mat4 = mat4Identity(),
): MeshBuilder {
  let pts: [number, number][] = [p0, p1, p2];
  const area =
    (pts[1][0] - pts[0][0]) * (pts[2][1] - pts[0][1]) -
    (pts[2][0] - pts[0][0]) * (pts[1][1] - pts[0][1]);
  if (area < 0) pts = [p0, p2, p1];
  const b = newBuilder();
  const z = depth / 2;
  const near = pts.map((p) => [p[0], p[1], -z] as Vec3);
  const far = pts.map((p) => [p[0], p[1], z] as Vec3);
  for (let i = 0; i < 3; i++) {
    const j = (i + 1) % 3;
    pushTri(b, near[i], near[j], far[j]);
    pushTri(b, near[i], far[j], far[i]);
    b.edges.push([near[i], near[j]]);
    b.edges.push([far[i], far[j]]);
    b.edges.push([near[i], far[i]]);
  }
  pushTri(b, near[0], near[2], near[1]);
  pushTri(b, far[0], far[1], far[2]);
  return applyXform(b, m);
}
