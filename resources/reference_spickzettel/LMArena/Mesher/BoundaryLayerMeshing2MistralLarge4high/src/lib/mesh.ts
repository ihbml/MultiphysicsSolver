import * as THREE from "three";
import type { CellData, MeshData } from "./types";

export const CELL_FACES: Record<number, number[][]> = {
  0: [
    [0, 3, 2, 1],
    [4, 5, 6, 7],
    [0, 1, 5, 4],
    [1, 2, 6, 5],
    [2, 3, 7, 6],
    [3, 0, 4, 7],
  ], // hex8
  1: [
    [0, 2, 1],
    [3, 4, 5],
    [0, 1, 4, 3],
    [1, 2, 5, 4],
    [2, 0, 3, 5],
  ], // prism6
};
export const CELL_NAMES: Record<number, string> = { 0: "Hex8", 1: "Prism6", 2: "Tet4", 3: "Pyr5" };

export type ColorMode = "jens" | "layer" | "patch" | "type";

const LAYER_COLORS = [0x38bdf8, 0x818cf8, 0xa78bfa, 0xf472b6, 0xfb923c, 0xfacc15, 0x4ade80];
const PATCH_COLORS = [0x60a5fa, 0xf472b6, 0x34d399, 0xfbbf24, 0xa78bfa, 0xf87171, 0x22d3ee];

export function jensColor(j: number): THREE.Color {
  // 0 -> red, 0.25 -> orange, 0.5 -> yellow, 0.75 -> light green, 1 -> green
  const stops: [number, THREE.Color][] = [
    [0.0, new THREE.Color(0xdc2626)],
    [0.25, new THREE.Color(0xf97316)],
    [0.5, new THREE.Color(0xfacc15)],
    [0.75, new THREE.Color(0xa3e635)],
    [1.0, new THREE.Color(0x16a34a)],
  ];
  const c = Math.max(0, Math.min(1, j));
  for (let i = 1; i < stops.length; i++) {
    if (c <= stops[i][0]) {
      const [c0, col0] = stops[i - 1];
      const [c1, col1] = stops[i];
      const t = (c - c0) / (c1 - c0);
      return col0.clone().lerp(col1, t);
    }
  }
  return stops[stops.length - 1][1];
}

export function cellColor(cell: CellData, mode: ColorMode, _patchNames?: string[]): THREE.Color {
  switch (mode) {
    case "jens":
      return jensColor(cell.j);
    case "layer":
      return cell.l < 0 ? new THREE.Color(0x94a3b8) : new THREE.Color(LAYER_COLORS[(cell.l - 1) % LAYER_COLORS.length]);
    case "patch":
      return cell.p < 0 ? new THREE.Color(0x94a3b8) : new THREE.Color(PATCH_COLORS[cell.p % PATCH_COLORS.length]);
    case "type":
      return cell.t === 0 ? new THREE.Color(0x38bdf8) : new THREE.Color(0xf472b6);
  }
}

export interface BuiltMesh {
  geometry: THREE.BufferGeometry;
  cellOfTri: number[];
  edges: THREE.BufferGeometry;
}

export function buildCellGeometry(data: MeshData, mode: ColorMode): BuiltMesh {
  const pos: number[] = [];
  const col: number[] = [];
  const cellOfTri: number[] = [];
  const edgeSet = new Set<string>();
  const edgePos: number[] = [];
  const V = data.vertices;
  for (let ci = 0; ci < data.cells.length; ci++) {
    const cell = data.cells[ci];
    const faces = CELL_FACES[cell.t] || CELL_FACES[0];
    const c = cellColor(cell, mode, data.patchNames);
    for (const f of faces) {
      for (let i = 1; i + 1 < f.length; i++) {
        const tri = [f[0], f[i], f[i + 1]];
        for (const li of tri) {
          const id = cell.n[li];
          pos.push(V[id][0], V[id][1], V[id][2]);
          col.push(c.r, c.g, c.b);
        }
        cellOfTri.push(ci);
      }
      for (let i = 0; i < f.length; i++) {
        const a = cell.n[f[i]];
        const b = cell.n[f[(i + 1) % f.length]];
        const key = a < b ? `${a}_${b}` : `${b}_${a}`;
        if (!edgeSet.has(key)) {
          edgeSet.add(key);
          edgePos.push(V[a][0], V[a][1], V[a][2], V[b][0], V[b][1], V[b][2]);
        }
      }
    }
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  geometry.setAttribute("color", new THREE.Float32BufferAttribute(col, 3));
  const edges = new THREE.BufferGeometry();
  edges.setAttribute("position", new THREE.Float32BufferAttribute(edgePos, 3));
  return { geometry, cellOfTri, edges };
}

export function buildFaGeometry(data: MeshData): THREE.BufferGeometry | null {
  if (!data.faMesh) return null;
  const pos: number[] = [];
  const col: number[] = [];
  const V = data.vertices;
  const areas = data.faMesh.faces.map((f) => f.area);
  const amin = Math.min(...areas);
  const amax = Math.max(...areas);
  for (const f of data.faMesh.faces) {
    const t = (f.area - amin) / Math.max(1e-30, amax - amin);
    const c = new THREE.Color(0x0ea5e9).lerp(new THREE.Color(0xf59e0b), t);
    const tri = data.surfTris[f.tri].n;
    for (const id of tri) {
      pos.push(V[id][0], V[id][1], V[id][2]);
      col.push(c.r, c.g, c.b);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute("color", new THREE.Float32BufferAttribute(col, 3));
  return g;
}

export interface Slice {
  on: boolean;
  val: number;
}
export interface Slices {
  x: Slice;
  y: Slice;
  z: Slice;
}

// intersection segments of a plane {axis, val} with all cell faces
export function buildSliceLines(data: MeshData, axis: "x" | "y" | "z", val: number): THREE.BufferGeometry {
  const V = data.vertices;
  const ai = axis === "x" ? 0 : axis === "y" ? 1 : 2;
  const seg: number[] = [];
  for (const cell of data.cells) {
    const faces = CELL_FACES[cell.t] || CELL_FACES[0];
    for (const f of faces) {
      for (let i = 1; i + 1 < f.length; i++) {
        const ids = [cell.n[f[0]], cell.n[f[i]], cell.n[f[i + 1]]];
        const d = ids.map((id) => V[id][ai] - val);
        const pts: number[][] = [];
        for (let e = 0; e < 3; e++) {
          const a = e;
          const b = (e + 1) % 3;
          if ((d[a] < 0 && d[b] > 0) || (d[a] > 0 && d[b] < 0)) {
            const t = d[a] / (d[a] - d[b]);
            const pa = V[ids[a]];
            const pb = V[ids[b]];
            pts.push([pa[0] + t * (pb[0] - pa[0]), pa[1] + t * (pb[1] - pa[1]), pa[2] + t * (pb[2] - pa[2])]);
          } else if (d[a] === 0) {
            pts.push([V[ids[a]][0], V[ids[a]][1], V[ids[a]][2]]);
          }
        }
        if (pts.length >= 2) {
          const p = pts[0];
          const q = pts[pts.length - 1];
          seg.push(p[0], p[1], p[2], q[0], q[1], q[2]);
        }
      }
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(seg, 3));
  return g;
}

export function bbox(data: MeshData): { min: THREE.Vector3; max: THREE.Vector3 } {
  const min = new THREE.Vector3(Infinity, Infinity, Infinity);
  const max = new THREE.Vector3(-Infinity, -Infinity, -Infinity);
  for (const p of data.vertices) {
    min.x = Math.min(min.x, p[0]);
    min.y = Math.min(min.y, p[1]);
    min.z = Math.min(min.z, p[2]);
    max.x = Math.max(max.x, p[0]);
    max.y = Math.max(max.y, p[1]);
    max.z = Math.max(max.z, p[2]);
  }
  return { min, max };
}

export function jensHistogram(data: MeshData): { bins: number[]; edges: number[] } {
  const edges = [0, 0.25, 0.5, 0.75, 1.0001];
  const bins = [0, 0, 0, 0];
  for (const c of data.cells) {
    let b = 3;
    if (c.j < 0.25) b = 0;
    else if (c.j < 0.5) b = 1;
    else if (c.j < 0.75) b = 2;
    bins[b]++;
  }
  return { bins, edges };
}

export function fmt(x: number): string {
  if (x === 0) return "0";
  const a = Math.abs(x);
  if (a >= 1e-3 || a < 1e-14) return x.toExponential(3);
  return x.toPrecision(4);
}
