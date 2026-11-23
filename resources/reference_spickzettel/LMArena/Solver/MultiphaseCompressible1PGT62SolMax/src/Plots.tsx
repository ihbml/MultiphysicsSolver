import { useEffect, useId, useMemo, useRef, useState, type CSSProperties, type PointerEvent } from "react";
import { Crosshair } from "lucide-react";
import { fieldsFor, scalar, type CaseId, type Dataset, type Frame } from "./simulation";

type RGB = [number, number, number];
const palette: [number, RGB][] = [
  [0, [4, 12, 29]], [.17, [7, 23, 55]], [.36, [13, 42, 92]], [.55, [21, 64, 133]],
  [.68, [18, 93, 158]], [.83, [22, 156, 181]], [.94, [105, 225, 200]], [1, [251, 229, 143]],
];
const thermal: [number, RGB][] = [
  [0, [6, 13, 30]], [.2, [22, 31, 93]], [.42, [55, 63, 163]], [.61, [40, 122, 210]],
  [.78, [44, 204, 224]], [.91, [143, 233, 199]], [1, [255, 230, 151]],
];
const clip = (v: number) => Math.min(Math.max(v, 0), 1);
const lerp = (a: number, b: number, r: number) => a + (b - a) * r;
function color(v: number, map: [number, RGB][]) {
  let i = 1;
  for (; i < map.length - 1 && map[i][0] < v; i++);
  const [a, c] = map[i - 1], [b, d] = map[i];
  const r = clip((v - a) / (b - a));
  return [lerp(c[0], d[0], r), lerp(c[1], d[1], r), lerp(c[2], d[2], r)];
}
function isThermal(id: CaseId, channel: number) { return id === "film" || channel === 4; }
function gradient(id: CaseId, channel: number) {
  const lut = isThermal(id, channel) ? thermal : palette;
  return `linear-gradient(90deg,${lut.map(([t,c]) => `rgb(${c.join(",")}) ${t * 100}%`).join(",")})`;
}

export function rangeFor(id: CaseId, field: number, f: Frame, grid: Dataset): [number, number] {
  if (id === "bubble") {
    if (field === 0) return [0.08, Math.max(1.84, f.m[8])];
    if (field === 1) return [0, Math.max(6, f.m[5])];
    if (field === 2) return [0, Math.max(1.6, f.m[6])];
    if (field === 3) return [0, 1];
    return [0, 0.45];
  }
  if (id === "film" && field === 0) return [0, Math.max(36, Math.ceil(f.m[5] / 4) * 4)];
  if (id === "film" && field === 1) return [0, Math.max(.24, Math.ceil(f.m[6] * 4) / 4)];
  if (id === "film" && field === 2) return [0, 215];
  if (id === "acoustic" && field === 1) return [.998, 1.008];
  if (id === "acoustic" && field === 2) return [.018, Math.max(.032, f.m[6])];
  if (id === "acoustic" && field === 0) {
    const base = Math.min(1, f.m[7]);
    return [base - .0008, Math.max(1.005, f.m[8] * 1.0008)];
  }
  if (id === "sod") {
    if (field < 2) return [0, 1.1];
    return [0, Math.max(1.2, f.m[6])];
  }
  let high = 0;
  for (let i = 0; i < grid.nx * grid.ny; i++) high = Math.max(high, scalar(f, field, i));
  return [0, Math.max(high, 1)];
}

function gradAt(f: Frame, w: number, h: number, i: number) {
  const x = i % w, y = Math.floor(i / w);
  const gx = (scalar(f, 0, y * w + Math.min(w - 1, x + 1)) - scalar(f, 0, y * w + Math.max(0, x - 1))) * .5;
  const gy = (scalar(f, 0, Math.min(h - 1, y + 1) * w + x) - scalar(f, 0, Math.max(0, y - 1) * w + x)) * .5;
  return Math.hypot(gx, gy);
}

type ViewProps = { id: CaseId; frame: Frame; field: number; data: Dataset; mesh: boolean; interfaceVisible: boolean };

function Raster({ id, frame, field, data }: ViewProps) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const ctx = ref.current?.getContext("2d", { alpha: false });
    if (!ctx) return;
    const w = data.nx, h = data.ny;
    const raster = ctx.createImageData(w, h), pixels = raster.data;
    const [min, max] = rangeFor(id, field, frame, data);
    const map = isThermal(id, field) ? thermal : palette;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (h - y - 1) * w + x, p = (y * w + x) * 4;
      const g = field === 4 || (field === 0 && id === "bubble") ? gradAt(frame, w, h, i) : 0;
      const value = field === 4 ? g : scalar(frame, field, i);
      const d = clip((value - min) / (max - min));
      const c = color(field === 4 || id === "film" ? Math.pow(d, .64) : d, map);
      if (id === "bubble" && field === 0) {
        const edge = (1 - Math.exp(-g * 6.25)) * .91;
        const warm = clip((g - .10) * 4.3);
        c[0] = lerp(c[0], lerp(55, 255, warm), edge);
        c[1] = lerp(c[1], lerp(221, 223, warm), edge);
        c[2] = lerp(c[2], lerp(224, 119, warm), edge);
      }
      pixels[p] = c[0]; pixels[p + 1] = c[1]; pixels[p + 2] = c[2]; pixels[p + 3] = 255;
    }
    ctx.putImageData(raster, 0, 0);
  }, [data, field, frame, id]);
  return <canvas ref={ref} width={data.nx} height={data.ny} className="raster" aria-label={`${fieldsFor(id)[field].title} at time ${frame.m[0].toFixed(4)}`} />;
}

function Profile({ frame, field, data, id }: ViewProps) {
  const uid = useId();
  const [min, max] = rangeFor(id, field, frame, data);
  const points = Array.from({ length: data.nx }, (_, i) => [i / (data.nx - 1) * 760, 350 * (1 - clip((scalar(frame, field, i) - min) / (max - min)))]);
  const line = points.map(([x, y], j) => `${j ? "L" : "M"}${x.toFixed(2)} ${y.toFixed(2)}`).join("");
  return <svg className="profile-plot" viewBox="0 0 760 350" preserveAspectRatio="none" role="img" aria-label={`${fieldsFor(id)[field].label} profile`}>
    <defs><linearGradient id={uid} x1="0" x2="0" y1="0" y2="1"><stop stopColor="#37d9ee" stopOpacity=".31" /><stop offset="1" stopColor="#299df7" stopOpacity=".01" /></linearGradient></defs>
    <path d={`${line}L760 350H0Z`} fill={`url(#${uid})`} />
    <path d={line} stroke="#52dcf1" strokeWidth="2.1" fill="none" vectorEffect="non-scaling-stroke" />
  </svg>;
}

function Interface({ frame, data }: { frame: Frame; data: Dataset }) {
  if (!frame.m[13]) return null;
  const unit = 1000 / data.ly, w = data.lx * unit;
  const x = frame.m[10] * unit, y = (data.ly - frame.m[11]) * unit;
  const r = Math.max(.25, frame.m[12] * 1.4) * unit;
  const end = x + r * .638, rise = y - r * .770, out = Math.min(w - 280, end + 108);
  const labelY = Math.max(53, rise - 84);
  return <svg className="interface-overlay" viewBox={`0 0 ${w} 1000`} preserveAspectRatio="none" aria-label="Co-moving SPH-FVM patch">
    <circle cx={x} cy={y} r={r} fill="none" stroke="#aaebff" strokeOpacity=".65" strokeWidth="2.3" strokeDasharray="12 13" />
    <circle cx={x} cy={y} r={r + .14 * unit} fill="none" stroke="#c1a5ff" strokeOpacity=".23" strokeWidth="2" strokeDasharray="4 13" />
    <polyline points={`${end},${rise} ${out},${labelY} ${out + 27},${labelY}`} fill="none" stroke="#bbedff" strokeOpacity=".65" strokeWidth="2.3" />
    <circle cx={end} cy={rise} r="6" fill="#c8f3ff" fillOpacity=".84" />
    <rect x={out + 27} y={labelY - 24} width="229" height="47" rx="9" fill="#071429" fillOpacity=".80" stroke="#8bc7f8" strokeOpacity=".32" strokeWidth="1.8" />
    <text x={out + 141} y={labelY + 8} textAnchor="middle" fill="#c3e9fa" fontSize="25" letterSpacing="1.4">OPMIS INTERFACE</text>
  </svg>;
}

export function FlowPlot(props: ViewProps) {
  const { frame, field, data, id, mesh, interfaceVisible } = props;
  const [cursor, setCursor] = useState<{ x: number; y: number } | null>(null);
  const is2d = data.ny > 1;
  const fieldDef = fieldsFor(id)[field];
  const [min, max] = useMemo(() => rangeFor(id, field, frame, data), [id, field, frame, data]);
  const xTicks = Array.from({ length: 5 }, (_, i) => (i * data.lx / 4).toFixed(id === "film" ? 2 : 1));
  const yTicks = Array.from({ length: 4 }, (_, i) => (is2d ? (1 - i / 3) * data.ly : lerp(max, min, i / 3)).toFixed(id === "acoustic" ? 3 : 2));
  const getCursor = (ev: PointerEvent<HTMLDivElement>) => {
    const r = ev.currentTarget.getBoundingClientRect();
    setCursor({ x: clip((ev.clientX - r.left) / r.width), y: clip((ev.clientY - r.top) / r.height) });
  };
  const index = cursor ? Math.min(data.nx - 1, Math.floor(data.nx * cursor.x)) + Math.min(data.ny - 1, Math.floor(data.ny * (1 - cursor.y))) * data.nx : 0;
  const probe = cursor ? (field === 4 ? gradAt(frame, data.nx, data.ny, index) : scalar(frame, field, index)) : null;

  return <div className="flow-body">
    <div className="axis-shell">
      <div className="y-axis">
        <span className="axis-name y-name">{is2d ? (id === "film" ? "y [m]" : "y / L\u2080") : fieldDef.symbol}</span>
        {yTicks.map((tick, i) => <span className="axis-tick" style={{ top: `${i / 3 * 100}%` }} key={i}>{tick}</span>)}
      </div>
      <div className="plot-area" style={{ "--aspect": is2d ? data.lx / data.ly : 2.14, "--nx": data.nx, "--ny": data.ny } as CSSProperties}
        onPointerMove={getCursor} onPointerLeave={() => setCursor(null)}>
        {is2d ? <Raster {...props} /> : <Profile {...props} />}
        <div className="plot-grid" aria-hidden="true" />
        {mesh && <div className={`cell-grid ${!is2d ? "one-d" : ""}`} aria-hidden="true" />}
        {id === "bubble" && interfaceVisible && <Interface frame={frame} data={data} />}
        {is2d && <span className="plot-plane">{id === "film" ? "WALL SURFACE" : "XY PLANE"}</span>}
        {cursor && probe !== null && <div className="probe-anchor" style={{ left: `${cursor.x * 100}%`, top: `${cursor.y * 100}%` }}>
          <span className="probe-line probe-x" /><span className="probe-line probe-y" />
          <span className="probe-tip" style={{ transform: cursor.x > .69 ? "translate(-106%, -125%)" : cursor.y < .20 ? "translate(12px, 12px)" : undefined }}>
            <b>{probe.toPrecision(4)} {fieldDef.symbol}</b><small>x {(cursor.x * data.lx).toFixed(3)}{is2d ? ` / y ${((1 - cursor.y) * data.ly).toFixed(3)}` : ""}</small>
          </span>
        </div>}
      </div>
      <div className="x-axis">
        {xTicks.map((tick, i) => <span className="axis-tick" style={{ left: `${i * 25}%` }} key={i}>{tick}</span>)}
        <span className="axis-name x-name">{id === "film" ? "x [m]" : "x / L\u2080"}</span>
      </div>
    </div>
    <div className="plot-legend">
      {is2d ? <><div className="scale-name"><span>{fieldDef.label}</span><small>{fieldDef.symbol}</small></div>
        <div className="scale-content"><div className="scale-bar" style={{ background: gradient(id, field) }} />
          <div className="scale-ticks">{[0, .25, .5, .75, 1].map(v => <span key={v}>{lerp(min, max, isThermal(id, field) ? Math.pow(v, 1 / .64) : v).toFixed(max > 20 ? 0 : 2)}</span>)}</div>
        </div></> : <div className="line-key"><i /> {fieldDef.label} profile <small>{fieldDef.symbol}</small></div>}
      <span className="probe-label"><Crosshair size={14} /> Hover to probe</span>
    </div>
  </div>;
}

export function Trend({ data, position, metric = 5, large = false, signed = false, color: accent = "#42d9f4" }:
  { data: Dataset; position: number; metric?: number; large?: boolean; signed?: boolean; color?: string }) {
  const id = useId(), w = 240, h = large ? 110 : 65, bottom = h - 3;
  const values = data.frames.flatMap((f, i) => f ? [{ index: i, value: f.m[metric] }] : []);
  if (!values.length) return <div className="trend-blank">Waiting for measurements</div>;
  const low = Math.min(...values.map(v => v.value)), high = Math.max(...values.map(v => v.value));
  const padding = Math.max((high - low) * .14, Math.abs(high) * .02, 1.e-19);
  const ax = (i: number) => i / (data.count - 1) * (w - 6) + 3;
  const ay = (v: number) => bottom - (v - (signed ? low - padding : Math.max(0, low - padding))) /
    (high + padding - (signed ? low - padding : Math.max(0, low - padding))) * (h - 8);
  const line = values.map((v, j) => `${j ? "L" : "M"}${ax(v.index).toFixed(3)},${ay(v.value).toFixed(3)}`).join(" ");
  const current = data.frames[position];
  const point = current?.m[metric];
  return <div className={`trend ${large ? "large" : ""}`}>
    <svg viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none" role="img" aria-label="Diagnostic history over time">
      <defs><linearGradient id={id} x1="0" x2="0" y1="0" y2="1"><stop stopColor={accent} stopOpacity=".25" /><stop offset="1" stopColor={accent} stopOpacity=".015" /></linearGradient></defs>
      {[.15,.42,.69,.96].map(y => <line key={y} x1="0" x2={w} y1={h * y} y2={h * y} className="trend-grid" />)}
      {[.01,.25,.50,.75,.99].map(x => <line key={x} x1={w * x} x2={w * x} y1="0" y2={h - 2} className="trend-grid" />)}
      <path d={`${line} L${ax(values[values.length - 1].index)},${bottom} L${ax(values[0].index)},${bottom}Z`} fill={`url(#${id})`} />
      <path d={line} fill="none" stroke={accent} strokeWidth="1.55" vectorEffect="non-scaling-stroke" strokeLinejoin="round" />
      {point !== undefined && <><line x1={ax(position)} x2={ax(position)} y1={ay(point)} y2={bottom} stroke={accent} strokeOpacity=".48" strokeDasharray="3 3" />
        <circle cx={ax(position)} cy={ay(point)} r={large ? "2.5" : "3.4"} fill={accent} stroke="#101826" strokeWidth="1" /></>}
    </svg>
    <div className="trend-ticks"><span>0.00</span><span>{(data.end / 2).toFixed(2)}</span><span>{data.end.toFixed(2)}</span></div>
  </div>;
}