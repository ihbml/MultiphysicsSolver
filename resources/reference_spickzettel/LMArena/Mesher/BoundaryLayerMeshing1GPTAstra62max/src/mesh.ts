export type Vec3 = [number, number, number];
export type Axis = "X" | "Y" | "Z";
export type Tool = "orbit" | "pan" | "pick";
export type ColorField = "type" | "quality" | "layer" | "distance";
export type ViewMode = "Solid + edges" | "Solid" | "Wireframe";
export type Geometry = 0 | 1;
export type MeshParams = { layers: number; first: number; growth: number };
export interface MeshStats {
  hangingNodes: number;
  coarseParents: number;
  coarseHexes: number;
  cells: number;
  vertices: number;
  faces: number;
  wallFaces: number;
  minJ: number;
  meanJ: number;
  minVolume: number;
  maxClosure: number;
  negative: number;
  unmatched: number;
  nonmanifold: number;
  minVisibility: number;
  minPrism: number;
  minStar: number;
  clearanceRatio: number;
  volume: number;
  expectedVolume: number;
  elapsedMs: number;
  squeezed: number;
  retries: number;
  counts: number[];
  dofs: number[];
}
export interface MeshData {
  id: string;
  version: string;
  geometry: Geometry;
  extent: Vec3;
  params: MeshParams;
  stats: MeshStats;
  points: Vec3[];
  cells: number[][];
  quality: number[];
  volumes: number[];
  layers: number[];
  boundary: number[][];
}
export interface FaceData {
  p: Vec3[];
  owner: number;
  cut: boolean;
  normal: Vec3;
  center: Vec3;
}
export const GEOMETRIES = ["Stepped L-channel", "Twin-plate cooling gap"];
export const FILES = ["l-channel", "twin-plate"];
export const PATCHES = [
  "Floor",
  "Ceiling",
  "Front wall",
  "Outlet wall",
  "Back wall",
  "Inlet wall",
];
export const CELL_TYPES = [
  { n: 8, label: "Hexahedra", short: "Hex8", color: "#7795ff" },
  { n: 6, label: "Prisms", short: "Prism6", color: "#edb765" },
  { n: 5, label: "Pyramids", short: "Pyr5", color: "#63c6af" },
  { n: 4, label: "Tetrahedra", short: "Tet4", color: "#b397e2" },
];
export const LOCAL_FACES: Record<number, number[][]> = {
  4: [
    [0, 2, 1],
    [0, 1, 3],
    [1, 2, 3],
    [2, 0, 3],
  ],
  5: [
    [0, 3, 2, 1],
    [0, 1, 4],
    [1, 2, 4],
    [2, 3, 4],
    [3, 0, 4],
  ],
  6: [
    [0, 2, 1],
    [3, 4, 5],
    [0, 1, 4, 3],
    [1, 2, 5, 4],
    [2, 0, 3, 5],
  ],
  8: [
    [0, 3, 2, 1],
    [4, 5, 6, 7],
    [0, 1, 5, 4],
    [1, 2, 6, 5],
    [2, 3, 7, 6],
    [3, 0, 4, 7],
  ],
};
export const sub = (a: Vec3, b: Vec3): Vec3 => [
  a[0] - b[0],
  a[1] - b[1],
  a[2] - b[2],
];
export const cross = (a: Vec3, b: Vec3): Vec3 => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export const dot = (a: Vec3, b: Vec3) =>
  a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const unit = (v: Vec3): Vec3 => {
  const s = Math.hypot(...v) || 1;
  return [v[0] / s, v[1] / s, v[2] / s];
};
export const avg = (ps: Vec3[]): Vec3 =>
  ps.reduce<Vec3>(
    (r, p) => [
      r[0] + p[0] / ps.length,
      r[1] + p[1] / ps.length,
      r[2] + p[2] / ps.length,
    ],
    [0, 0, 0],
  );
export const integer = (n: number) => n.toLocaleString("en-US");
export const sci = (n: number, digits = 2) =>
  n.toExponential(digits).replace("e+", "e");
// Exact distance to the analytic benchmark boundary, evaluated at cell centers.
export function wallDistance(mesh: MeshData, p: Vec3) {
  const [x, y, z] = p,
    [w, h, d] = mesh.extent;
  const outline = mesh.geometry
    ? [
        [0, 0],
        [w, 0],
        [w, d],
        [0, d],
      ]
    : [
        [0, 0],
        [w, 0],
        [w, 16],
        [28, 16],
        [28, d],
        [0, d],
      ];
  let best = Math.min(y, h - y);
  for (let i = 0; i < outline.length; i++) {
    const a = outline[i],
      b = outline[(i + 1) % outline.length],
      dx = b[0] - a[0],
      dz = b[1] - a[1];
    const t = Math.max(
      0,
      Math.min(1, ((x - a[0]) * dx + (z - a[1]) * dz) / (dx * dx + dz * dz)),
    );
    best = Math.min(best, Math.hypot(x - a[0] - t * dx, z - a[1] - t * dz));
  }
  return best;
}
const EDGES: Record<number, number[][]> = {};
for (const [n, fs] of Object.entries(LOCAL_FACES)) {
  const es = new Map<string, number[]>();
  for (const f of fs)
    for (let j = 0; j < f.length; j++) {
      const a = f[j],
        b = f[(j + 1) % f.length];
      es.set([Math.min(a, b), Math.max(a, b)].join(":"), [a, b]);
    }
  EDGES[+n] = [...es.values()];
}
function clipping(poly: Vec3[], axis: number, at: number, sign: number) {
  const out: Vec3[] = [];
  for (let i = 0; i < poly.length; i++) {
    const a = poly[i],
      b = poly[(i + 1) % poly.length],
      da = (a[axis] - at) * sign,
      db = (b[axis] - at) * sign;
    if (da <= 0) out.push(a);
    if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
      const t = da / (da - db);
      out.push([
        a[0] + t * (b[0] - a[0]),
        a[1] + t * (b[1] - a[1]),
        a[2] + t * (b[2] - a[2]),
      ]);
    }
  }
  return out;
}
export function surface(
  mesh: MeshData,
  slice: boolean,
  axisName: Axis,
  pct: number,
  flip: boolean,
  visible: number[],
  wallPatch: number | null,
): FaceData[] {
  const axis = "XYZ".indexOf(axisName),
    at = (mesh.extent[axis] * pct) / 100 + 1e-7;
  // A Y cut removes the near wall. Flipping reverses the retained half-space.
  const sign = (axis === 1 ? -1 : 1) * (flip ? -1 : 1),
    faces: FaceData[] = [];
  const add = (p: Vec3[], owner: number, cut: boolean, normal?: Vec3) => {
    if (p.length < 3) return;
    const n = normal ?? unit(cross(sub(p[1], p[0]), sub(p[2], p[0])));
    if (!cut && slice) p = clipping(p, axis, at, sign);
    if (p.length >= 3) faces.push({ p, owner, cut, normal: n, center: avg(p) });
  };
  if (visible.length === 4 || wallPatch !== null) {
    for (const b of mesh.boundary)
      if (
        (wallPatch === null || wallPatch === -1 || b[1] === wallPatch) &&
        visible.includes(mesh.cells[b[0]].length)
      )
        add(
          b.slice(2).map((i) => mesh.points[i]),
          b[0],
          false,
        );
  } else {
    const boundary = new Map<string, { ids: number[]; owner: number }>();
    mesh.cells.forEach((cell, i) => {
      if (!visible.includes(cell.length)) return;
      for (const f of LOCAL_FACES[cell.length]) {
        const ids = f.map((k) => cell[k]),
          id = [...ids].sort((a, b) => a - b).join(":");
        if (boundary.has(id)) boundary.delete(id);
        else boundary.set(id, { ids, owner: i });
      }
    });
    boundary.forEach(({ ids, owner }) =>
      add(
        ids.map((i) => mesh.points[i]),
        owner,
        false,
      ),
    );
  }
  if (slice && wallPatch === null)
    mesh.cells.forEach((cell, owner) => {
      if (!visible.includes(cell.length)) return;
      let lower = false,
        upper = false;
      for (const n of cell) {
        if (mesh.points[n][axis] < at) lower = true;
        else upper = true;
      }
      if (!lower || !upper) return;
      const points: Vec3[] = [];
      const intersect = (u: number, v: number) => {
        const a = mesh.points[cell[u]],
          b = mesh.points[cell[v]],
          da = a[axis] - at,
          db = b[axis] - at;
        if (da < 0 === db < 0) return;
        const w = da / (da - db);
        points.push([
          a[0] + (b[0] - a[0]) * w,
          a[1] + (b[1] - a[1]) * w,
          a[2] + (b[2] - a[2]) * w,
        ]);
      };
      for (const [u, v] of EDGES[cell.length]) intersect(u, v);
      // Use the engine's canonical diagonal on warped quadrilateral faces.
      // Its intersection adds the missing bend in the section perimeter.
      for (const f of LOCAL_FACES[cell.length])
        if (f.length === 4) {
          let anchor = 0;
          for (let j = 1; j < 4; j++)
            if (cell[f[j]] < cell[f[anchor]]) anchor = j;
          intersect(f[anchor], f[(anchor + 2) % 4]);
        }
      if (points.length < 3) return;
      const c = avg(points),
        a = (axis + 1) % 3,
        b = (axis + 2) % 3;
      points.sort(
        (u, v) =>
          Math.atan2(u[b] - c[b], u[a] - c[a]) -
          Math.atan2(v[b] - c[b], v[a] - c[a]),
      );
      const n: Vec3 = [0, 0, 0];
      n[axis] = sign;
      add(points, owner, true, n);
    });
  return faces;
}
export function download(
  name: string,
  content: BlobPart,
  type = "application/json",
) {
  const url = URL.createObjectURL(new Blob([content], { type })),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 800);
}
export function vtk(mesh: MeshData) {
  const types: Record<number, number> = { 4: 10, 5: 14, 6: 13, 8: 12 };
  return `# vtk DataFile Version 3.0\nHexaLab ${mesh.id}, millimetres\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS ${mesh.points.length} double\n${mesh.points.map((p) => p.join(" ")).join("\n")}\nCELLS ${mesh.cells.length} ${mesh.cells.reduce((a, c) => a + c.length + 1, 0)}\n${mesh.cells.map((c) => c.length + " " + c.join(" ")).join("\n")}\nCELL_TYPES ${mesh.cells.length}\n${mesh.cells.map((c) => types[c.length]).join("\n")}\nCELL_DATA ${mesh.cells.length}\nSCALARS JENS double 1\nLOOKUP_TABLE default\n${mesh.quality.join("\n")}\nSCALARS volume_mm3 double 1\nLOOKUP_TABLE default\n${mesh.volumes.join("\n")}\n`;
}
