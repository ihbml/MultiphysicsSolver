import { useCallback, useEffect, useRef, useState } from "react";
import { Geometry, PATCH_COLOR, PATCH_ORDER } from "../lib/sdf";
import { FvmMesh } from "../lib/mesh";
import { SphData } from "../lib/sph";
import { Ramp, ramp } from "../lib/colors";
import { CELL_COVERED, CELL_INTERFACE, CoreRegion } from "../lib/coupling";

export interface Layers {
  cells: boolean;
  edges: boolean;
  graph: boolean;
  boundaryFaces: boolean;
  faceNormals: boolean;
  particles: boolean;
  bndSurface: boolean;
  coupling: boolean;
  outline: boolean;
}

export type CellField = "none" | "quality" | "volume" | "nonortho" | "skew" | "rcm" | "region";
export type PartField = "eps0" | "eps1" | "divr" | "neigh" | "volume" | "level" | "patch" | "region";

export interface Probe {
  kind: "cell" | "particle";
  index: number;
  info: [string, string][];
  stencil?: { x: number; y: number; links: { x: number; y: number; w: number }[] };
}

interface Props {
  geo: Geometry;
  outline: Float64Array[];
  mesh: FvmMesh | null;
  sph: SphData | null;
  preview: { x: Float64Array; y: Float64Array; n: number; tris: Uint32Array } | null;
  layers: Layers;
  cellField: CellField;
  partField: PartField;
  rampName: Ramp;
  core: CoreRegion | null;
  cellClass: Uint8Array | null;
  partClass: Uint8Array | null;
  probe: Probe | null;
  onProbe: (x: number, y: number) => void;
  busy: string | null;
}

const BG = "#0b1020";

export default function Viewport(props: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const [view, setView] = useState({ s: 1, ox: 0, oy: 0 });
  const [size, setSize] = useState({ w: 800, h: 600 });
  const drag = useRef<{ x: number; y: number; ox: number; oy: number } | null>(null);
  const moved = useRef(0);
  const [cursor, setCursor] = useState<{ x: number; y: number } | null>(null);

  const fit = useCallback(
    (w: number, h: number) => {
      const [x0, y0, x1, y1] = props.geo.bbox;
      const bw = x1 - x0;
      const bh = y1 - y0;
      const s = Math.min(w / (bw * 1.12), h / (bh * 1.12));
      setView({
        s,
        ox: (w - bw * s) / 2 - x0 * s,
        oy: (h - bh * s) / 2 - y0 * s,
      });
    },
    [props.geo],
  );

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => {
      const w = el.clientWidth;
      const h = el.clientHeight;
      setSize({ w, h });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    fit(size.w, size.h);
  }, [fit, size.w, size.h]);

  /* ---------------- drawing ---------------- */
  useEffect(() => {
    const cv = canvasRef.current;
    if (!cv) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    cv.width = size.w * dpr;
    cv.height = size.h * dpr;
    const ctx = cv.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const { s, ox, oy } = view;
    const H = size.h;
    const X = (x: number) => x * s + ox;
    const Y = (y: number) => H - (y * s + oy);

    ctx.fillStyle = BG;
    ctx.fillRect(0, 0, size.w, size.h);

    // subtle grid
    const gstep = niceStep(120 / s);
    ctx.strokeStyle = "rgba(148,163,184,0.07)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    const [bx0, by0, bx1, by1] = props.geo.bbox;
    for (let gx = Math.floor(bx0 / gstep) * gstep; gx <= bx1 + gstep; gx += gstep) {
      ctx.moveTo(X(gx), Y(by0 - gstep));
      ctx.lineTo(X(gx), Y(by1 + gstep));
    }
    for (let gy = Math.floor(by0 / gstep) * gstep; gy <= by1 + gstep; gy += gstep) {
      ctx.moveTo(X(bx0 - gstep), Y(gy));
      ctx.lineTo(X(bx1 + gstep), Y(gy));
    }
    ctx.stroke();

    const { mesh, sph, layers, preview } = props;

    /* live preview during generation */
    if (preview) {
      ctx.strokeStyle = "rgba(96,165,250,0.35)";
      ctx.lineWidth = 0.6;
      ctx.beginPath();
      for (let k = 0; k < preview.tris.length; k += 3) {
        const a = preview.tris[k];
        const b = preview.tris[k + 1];
        const c = preview.tris[k + 2];
        ctx.moveTo(X(preview.x[a]), Y(preview.y[a]));
        ctx.lineTo(X(preview.x[b]), Y(preview.y[b]));
        ctx.lineTo(X(preview.x[c]), Y(preview.y[c]));
        ctx.closePath();
      }
      ctx.stroke();
      if (preview.tris.length === 0) {
        ctx.fillStyle = "rgba(125,211,252,0.8)";
        for (let i = 0; i < preview.n; i++) {
          ctx.fillRect(X(preview.x[i]) - 1, Y(preview.y[i]) - 1, 2, 2);
        }
      }
    }

    /* filled cells */
    if (mesh && layers.cells && props.cellField !== "none") {
      const NB = 28;
      const buckets: Path2D[] = Array.from({ length: NB }, () => new Path2D());
      const vals = cellValues(mesh, props.cellField, props.cellClass);
      for (let c = 0; c < mesh.nCells; c++) {
        let b = Math.floor(vals.t[c] * NB);
        if (b < 0) b = 0;
        if (b >= NB) b = NB - 1;
        const p = buckets[b];
        const a = mesh.tris[3 * c];
        const bb = mesh.tris[3 * c + 1];
        const cc2 = mesh.tris[3 * c + 2];
        p.moveTo(X(mesh.points[2 * a]), Y(mesh.points[2 * a + 1]));
        p.lineTo(X(mesh.points[2 * bb]), Y(mesh.points[2 * bb + 1]));
        p.lineTo(X(mesh.points[2 * cc2]), Y(mesh.points[2 * cc2 + 1]));
        p.closePath();
      }
      for (let b = 0; b < NB; b++) {
        ctx.fillStyle =
          props.cellField === "region"
            ? regionColor(b / (NB - 1))
            : ramp((b + 0.5) / NB, props.rampName);
        ctx.fill(buckets[b]);
      }
    }

    /* wireframe */
    if (mesh && layers.edges) {
      ctx.strokeStyle = `rgba(226,232,240,${mesh.nCells > 9000 ? 0.1 : 0.22})`;
      ctx.lineWidth = Math.min(1, 0.45 + s / 1600);
      const p = new Path2D();
      for (let f = 0; f < mesh.nFaces; f++) {
        const i = mesh.faceNodes[2 * f];
        const j = mesh.faceNodes[2 * f + 1];
        p.moveTo(X(mesh.points[2 * i]), Y(mesh.points[2 * i + 1]));
        p.lineTo(X(mesh.points[2 * j]), Y(mesh.points[2 * j + 1]));
      }
      ctx.stroke(p);
    }

    /* dual graph (DiFVM message passing) */
    if (mesh && layers.graph) {
      ctx.strokeStyle = "rgba(56,189,248,0.42)";
      ctx.lineWidth = 0.7;
      const p = new Path2D();
      for (let f = 0; f < mesh.nFaces; f++) {
        const N = mesh.neigh[f];
        if (N < 0) continue;
        const O = mesh.owner[f];
        p.moveTo(X(mesh.cc[2 * O]), Y(mesh.cc[2 * O + 1]));
        p.lineTo(X(mesh.cc[2 * N]), Y(mesh.cc[2 * N + 1]));
      }
      ctx.stroke(p);
      const r = Math.max(0.8, Math.min(2.4, s / 400));
      ctx.fillStyle = "rgba(250,204,21,0.95)";
      const pn = new Path2D();
      for (let c = 0; c < mesh.nCells; c++) {
        pn.moveTo(X(mesh.cc[2 * c]) + r, Y(mesh.cc[2 * c + 1]));
        pn.arc(X(mesh.cc[2 * c]), Y(mesh.cc[2 * c + 1]), r, 0, 6.2832);
      }
      ctx.fill(pn);
    }

    /* boundary faces by patch */
    if (mesh && layers.boundaryFaces) {
      const byPatch = new Map<number, Path2D>();
      for (let f = 0; f < mesh.nFaces; f++) {
        if (mesh.neigh[f] >= 0) continue;
        const pid = mesh.fPatch[f];
        let p = byPatch.get(pid);
        if (!p) {
          p = new Path2D();
          byPatch.set(pid, p);
        }
        const i = mesh.faceNodes[2 * f];
        const j = mesh.faceNodes[2 * f + 1];
        p.moveTo(X(mesh.points[2 * i]), Y(mesh.points[2 * i + 1]));
        p.lineTo(X(mesh.points[2 * j]), Y(mesh.points[2 * j + 1]));
      }
      ctx.lineWidth = 2.4;
      for (const [pid, p] of byPatch) {
        ctx.strokeStyle = PATCH_COLOR[PATCH_ORDER[pid] ?? "wall"];
        ctx.stroke(p);
      }
      if (layers.faceNormals) {
        ctx.strokeStyle = "rgba(248,250,252,0.6)";
        ctx.lineWidth = 0.9;
        const q = new Path2D();
        for (let f = 0; f < mesh.nFaces; f++) {
          if (mesh.neigh[f] >= 0) continue;
          const cxp = mesh.fc[2 * f];
          const cyp = mesh.fc[2 * f + 1];
          const L = mesh.fa[f] * 1.1;
          q.moveTo(X(cxp), Y(cyp));
          q.lineTo(X(cxp + mesh.fn[2 * f] * L), Y(cyp + mesh.fn[2 * f + 1] * L));
        }
        ctx.stroke(q);
      }
    }

    /* particles */
    if (sph && layers.particles) {
      const vals = partValues(sph, props.partField, props.partClass);
      const NB = 28;
      const buckets: Path2D[] = Array.from({ length: NB }, () => new Path2D());
      const categorical = props.partField === "patch" || props.partField === "region";
      const catPaths = new Map<string, Path2D>();
      for (let i = 0; i < sph.n; i++) {
        const rr = Math.max(0.8, 0.4 * Math.sqrt(sph.V[i]) * s);
        const xx = X(sph.x[i]);
        const yy = Y(sph.y[i]);
        if (categorical) {
          const col = vals.color!(i);
          let p = catPaths.get(col);
          if (!p) {
            p = new Path2D();
            catPaths.set(col, p);
          }
          p.moveTo(xx + rr, yy);
          p.arc(xx, yy, rr, 0, 6.2832);
        } else {
          let b = Math.floor(vals.t[i] * NB);
          if (b < 0) b = 0;
          if (b >= NB) b = NB - 1;
          buckets[b].moveTo(xx + rr, yy);
          buckets[b].arc(xx, yy, rr, 0, 6.2832);
        }
      }
      if (categorical) {
        for (const [col, p] of catPaths) {
          ctx.fillStyle = col;
          ctx.fill(p);
        }
      } else {
        for (let b = 0; b < NB; b++) {
          ctx.fillStyle = ramp((b + 0.5) / NB, props.rampName);
          ctx.fill(buckets[b]);
        }
      }
    }

    /* reconstructed boundary surface A_iS */
    if (sph && layers.bndSurface) {
      let maxA = 1e-30;
      for (let i = 0; i < sph.n; i++) if (sph.isBnd[i]) maxA = Math.max(maxA, sph.asMag[i]);
      ctx.lineWidth = 1.2;
      for (let i = 0; i < sph.n; i++) {
        if (!sph.isBnd[i]) continue;
        const L = (sph.asMag[i] / maxA) * 1.6 * sph.h[i];
        const x1 = sph.x[i] + sph.asNx[i] * L;
        const y1 = sph.y[i] + sph.asNy[i] * L;
        ctx.strokeStyle =
          sph.patch[i] >= 0
            ? PATCH_COLOR[PATCH_ORDER[sph.patch[i]]]
            : "rgba(248,250,252,0.8)";
        ctx.beginPath();
        ctx.moveTo(X(sph.x[i]), Y(sph.y[i]));
        ctx.lineTo(X(x1), Y(y1));
        ctx.stroke();
      }
    }

    /* coupling regions */
    if (props.core && layers.coupling) {
      const c = props.core;
      ctx.setLineDash([6, 4]);
      ctx.strokeStyle = "rgba(244,114,182,0.95)";
      ctx.lineWidth = 1.6;
      ctx.beginPath();
      ctx.arc(X(c.x), Y(c.y), c.r * s, 0, 6.2832);
      ctx.stroke();
      ctx.strokeStyle = "rgba(56,189,248,0.8)";
      ctx.beginPath();
      ctx.arc(X(c.x), Y(c.y), Math.max(0, c.r - c.buffer) * s, 0, 6.2832);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    /* probe + stencil */
    if (props.probe?.stencil) {
      const st = props.probe.stencil;
      let wmax = 1e-9;
      for (const l of st.links) wmax = Math.max(wmax, Math.abs(l.w));
      for (const l of st.links) {
        const a = Math.min(1, Math.abs(l.w) / wmax);
        ctx.strokeStyle = l.w >= 0 ? `rgba(74,222,128,${0.25 + 0.7 * a})` : `rgba(248,113,113,${0.25 + 0.7 * a})`;
        ctx.lineWidth = 0.6 + 2.2 * a;
        ctx.beginPath();
        ctx.moveTo(X(st.x), Y(st.y));
        ctx.lineTo(X(l.x), Y(l.y));
        ctx.stroke();
      }
      ctx.strokeStyle = "#fde047";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(X(st.x), Y(st.y), 6, 0, 6.2832);
      ctx.stroke();
    }

    /* geometry outline */
    if (layers.outline) {
      ctx.strokeStyle = "rgba(248,250,252,0.85)";
      ctx.lineWidth = 1.4;
      const p = new Path2D();
      for (const sgm of props.outline) {
        p.moveTo(X(sgm[0]), Y(sgm[1]));
        p.lineTo(X(sgm[2]), Y(sgm[3]));
      }
      ctx.stroke(p);
      if (props.geo.interfaceCircle) {
        const ic = props.geo.interfaceCircle;
        ctx.strokeStyle = "rgba(251,191,36,0.9)";
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.arc(X(ic.x), Y(ic.y), ic.r * s, 0, 6.2832);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }

    // scale bar
    const barWorld = niceStep(140 / s);
    ctx.strokeStyle = "rgba(226,232,240,0.9)";
    ctx.fillStyle = "rgba(226,232,240,0.9)";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(16, size.h - 22);
    ctx.lineTo(16 + barWorld * s, size.h - 22);
    ctx.moveTo(16, size.h - 26);
    ctx.lineTo(16, size.h - 18);
    ctx.moveTo(16 + barWorld * s, size.h - 26);
    ctx.lineTo(16 + barWorld * s, size.h - 18);
    ctx.stroke();
    ctx.font = "11px ui-monospace, monospace";
    ctx.fillText(`${barWorld} m`, 16, size.h - 30);
  }, [props, view, size]);

  /* ---------------- interaction ---------------- */
  const toWorld = (cx: number, cy: number) => {
    const rect = canvasRef.current!.getBoundingClientRect();
    const px = cx - rect.left;
    const py = cy - rect.top;
    return {
      x: (px - view.ox) / view.s,
      y: (size.h - py - view.oy) / view.s,
    };
  };

  return (
    <div ref={wrapRef} className="relative h-full w-full overflow-hidden">
      <canvas
        ref={canvasRef}
        style={{ width: size.w, height: size.h }}
        className="block cursor-crosshair touch-none"
        onWheel={(e) => {
          const rect = canvasRef.current!.getBoundingClientRect();
          const px = e.clientX - rect.left;
          const py = e.clientY - rect.top;
          const f = Math.exp(-e.deltaY * 0.0015);
          setView((v) => {
            const ns = Math.max(1, Math.min(1e6, v.s * f));
            const k = ns / v.s;
            return {
              s: ns,
              ox: px - k * (px - v.ox),
              oy: size.h - py - k * (size.h - py - v.oy),
            };
          });
        }}
        onPointerDown={(e) => {
          (e.target as HTMLElement).setPointerCapture(e.pointerId);
          drag.current = { x: e.clientX, y: e.clientY, ox: view.ox, oy: view.oy };
          moved.current = 0;
        }}
        onPointerMove={(e) => {
          const w = toWorld(e.clientX, e.clientY);
          setCursor(w);
          if (!drag.current) return;
          const dx = e.clientX - drag.current.x;
          const dy = e.clientY - drag.current.y;
          moved.current = Math.max(moved.current, Math.hypot(dx, dy));
          setView((v) => ({ ...v, ox: drag.current!.ox + dx, oy: drag.current!.oy - dy }));
        }}
        onPointerUp={(e) => {
          const wasDrag = moved.current > 4;
          drag.current = null;
          if (!wasDrag) {
            const w = toWorld(e.clientX, e.clientY);
            props.onProbe(w.x, w.y);
          }
        }}
      />
      <div className="pointer-events-none absolute left-3 top-3 flex flex-col gap-1">
        <div className="rounded-md bg-slate-950/70 px-2 py-1 font-mono text-[11px] text-slate-300 ring-1 ring-white/10">
          {cursor ? `x ${cursor.x.toFixed(4)}  y ${cursor.y.toFixed(4)}` : "—"}
        </div>
        {props.busy && (
          <div className="rounded-md bg-sky-500/20 px-2 py-1 font-mono text-[11px] text-sky-200 ring-1 ring-sky-400/40">
            {props.busy}
          </div>
        )}
      </div>
      <button
        onClick={() => fit(size.w, size.h)}
        className="pointer-events-auto absolute right-3 top-3 rounded-md bg-slate-950/70 px-2.5 py-1 text-[11px] font-medium text-slate-300 ring-1 ring-white/10 hover:bg-slate-800"
      >
        reset view
      </button>
    </div>
  );
}

/* ---------------- helpers ---------------- */

function niceStep(x: number): number {
  const e = Math.pow(10, Math.floor(Math.log10(Math.max(x, 1e-9))));
  const m = x / e;
  const n = m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10;
  return n * e;
}

function regionColor(t: number): string {
  if (t < 0.25) return "#1e3a5f";
  if (t < 0.75) return "#b45309";
  return "#334155";
}

function cellValues(
  m: FvmMesh,
  field: CellField,
  cls: Uint8Array | null,
): { t: Float32Array; lo: number; hi: number } {
  const t = new Float32Array(m.nCells);
  let lo = Infinity;
  let hi = -Infinity;
  const raw = new Float32Array(m.nCells);
  if (field === "region") {
    for (let c = 0; c < m.nCells; c++) {
      const k = cls ? cls[c] : 0;
      t[c] = k === CELL_COVERED ? 1 : k === CELL_INTERFACE ? 0.5 : 0;
    }
    return { t, lo: 0, hi: 1 };
  }
  if (field === "nonortho" || field === "skew") {
    const arr = field === "nonortho" ? m.nonOrtho : m.skew;
    for (let f = 0; f < m.nFaces; f++) {
      const O = m.owner[f];
      const N = m.neigh[f];
      if (N < 0) continue;
      raw[O] = Math.max(raw[O], arr[f]);
      raw[N] = Math.max(raw[N], arr[f]);
    }
  } else if (field === "quality") {
    raw.set(m.quality);
  } else if (field === "volume") {
    for (let c = 0; c < m.nCells; c++) raw[c] = Math.sqrt(m.cv[c]);
  } else if (field === "rcm") {
    const inv = new Int32Array(m.nCells);
    for (let i = 0; i < m.nCells; i++) inv[m.rcm[i]] = i;
    for (let c = 0; c < m.nCells; c++) raw[c] = inv[c];
  }
  for (let c = 0; c < m.nCells; c++) {
    lo = Math.min(lo, raw[c]);
    hi = Math.max(hi, raw[c]);
  }
  const span = hi - lo || 1;
  for (let c = 0; c < m.nCells; c++) t[c] = (raw[c] - lo) / span;
  return { t, lo, hi };
}

function partValues(
  s: SphData,
  field: PartField,
  cls: Uint8Array | null,
): { t: Float32Array; color?: (i: number) => string; lo: number; hi: number } {
  const t = new Float32Array(s.n);
  if (field === "patch") {
    return {
      t,
      lo: 0,
      hi: 1,
      color: (i) =>
        s.isBnd[i]
          ? PATCH_COLOR[PATCH_ORDER[s.patch[i]] ?? "wall"]
          : "rgba(100,116,139,0.75)",
    };
  }
  if (field === "region") {
    return {
      t,
      lo: 0,
      hi: 1,
      color: (i) =>
        !cls || cls[i] === 0
          ? "#38bdf8"
          : cls[i] === 1
            ? "#f472b6"
            : "rgba(100,116,139,0.5)",
    };
  }
  const raw = new Float32Array(s.n);
  if (field === "eps0") raw.set(s.eps0);
  else if (field === "eps1") raw.set(s.eps1);
  else if (field === "divr") raw.set(s.divR);
  else if (field === "neigh") for (let i = 0; i < s.n; i++) raw[i] = s.nNeigh[i];
  else if (field === "volume") for (let i = 0; i < s.n; i++) raw[i] = Math.sqrt(s.V[i]);
  else if (field === "level") for (let i = 0; i < s.n; i++) raw[i] = s.level[i];
  let lo = Infinity;
  let hi = -Infinity;
  for (let i = 0; i < s.n; i++) {
    lo = Math.min(lo, raw[i]);
    hi = Math.max(hi, raw[i]);
  }
  if (field === "eps0" || field === "eps1") {
    // log-ish scaling makes the boundary layer visible
    lo = 0;
    hi = Math.max(hi, 1e-6);
    for (let i = 0; i < s.n; i++) t[i] = Math.min(1, Math.sqrt(raw[i] / hi));
    return { t, lo, hi };
  }
  const span = hi - lo || 1;
  for (let i = 0; i < s.n; i++) t[i] = (raw[i] - lo) / span;
  return { t, lo, hi };
}
