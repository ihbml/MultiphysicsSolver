import type { ReactNode } from "react";

export function Card({ title, children, className = "" }: { title?: string; children: ReactNode; className?: string }) {
  return (
    <section className={`rounded-2xl border border-slate-200 bg-white p-5 shadow-sm ${className}`}>
      {title && <h2 className="mb-3 text-sm font-semibold uppercase tracking-wide text-slate-500">{title}</h2>}
      {children}
    </section>
  );
}

export function fmt(v: number, digits = 4): string {
  if (!Number.isFinite(v)) return "—";
  const a = Math.abs(v);
  if (a !== 0 && (a >= 1e5 || a < 1e-3)) return v.toExponential(digits - 1);
  return String(Number(v.toPrecision(digits)));
}

interface SliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit?: string;
  log?: boolean; // min/max are raw positive values; slider moves in log10 space
  onChange: (v: number) => void;
}

export function Slider({ label, value, min, max, step = 1, unit, log = false, onChange }: SliderProps) {
  const lo = log ? Math.log10(min) : min;
  const hi = log ? Math.log10(max) : max;
  const cur = log ? Math.log10(Math.max(value, min)) : value;
  const st = log ? 0.01 : step;
  return (
    <label className="block">
      <div className="mb-1 flex items-baseline justify-between text-sm">
        <span className="font-medium text-slate-700">{label}</span>
        <span className="font-mono text-slate-900">
          {fmt(value, 4)} {unit && <span className="text-slate-400">{unit}</span>}
        </span>
      </div>
      <input
        type="range"
        className="w-full accent-indigo-600"
        min={lo}
        max={hi}
        step={st}
        value={cur}
        onChange={(e) => {
          const v = Number(e.target.value);
          onChange(log ? 10 ** v : v);
        }}
      />
    </label>
  );
}

export function Stat({ label, value, unit, tone = "default" }: { label: string; value: string; unit?: string; tone?: "default" | "good" | "warn" }) {
  const toneCls = tone === "good" ? "text-emerald-600" : tone === "warn" ? "text-amber-600" : "text-slate-900";
  return (
    <div className="rounded-xl bg-slate-50 p-3">
      <div className="text-xs font-medium text-slate-500">{label}</div>
      <div className={`mt-1 font-mono text-lg font-semibold ${toneCls}`}>
        {value} {unit && <span className="text-xs font-normal text-slate-400">{unit}</span>}
      </div>
    </div>
  );
}

export interface Series {
  name: string;
  color: string;
  points: [number, number][];
  dashed?: boolean;
  dots?: boolean;
}

interface ChartProps {
  series: Series[];
  xLabel: string;
  yLabel: string;
  xLog?: boolean;
  yLog?: boolean;
  vline?: number;
  height?: number;
  yDomain?: [number, number];
}

function decadeTicks(lo: number, hi: number): number[] {
  const out: number[] = [];
  for (let e = Math.ceil(lo); e <= Math.floor(hi); e++) out.push(e);
  return out;
}

export function LineChart({ series, xLabel, yLabel, xLog = false, yLog = false, vline, height = 300, yDomain }: ChartProps) {
  const W = 640;
  const H = height;
  const pad = { l: 64, r: 16, t: 14, b: 46 };
  const valid = (x: number, y: number) =>
    Number.isFinite(x) && Number.isFinite(y) && (!xLog || x > 0) && (!yLog || y > 0);
  const pts = series.flatMap((s) => s.points.filter(([x, y]) => valid(x, y)));
  const xs = pts.map((p) => p[0]);
  const ys = pts.map((p) => p[1]);
  const tx = (v: number) => (xLog ? Math.log10(v) : v);
  const ty = (v: number) => (yLog ? Math.log10(v) : v);
  const X0 = xs.length ? tx(Math.min(...xs)) : 0;
  const X1 = xs.length ? tx(Math.max(...xs)) : 1;
  const yMin = yDomain ? yDomain[0] : Math.min(...ys);
  const yMax = yDomain ? yDomain[1] : Math.max(...ys);
  const Y0 = ty(yMin);
  const Y1 = ty(yMax);
  const dX = X1 - X0 || 1;
  const dY = Y1 - Y0 || 1;
  const sx = (x: number) => pad.l + ((tx(x) - X0) / dX) * (W - pad.l - pad.r);
  const sy = (y: number) => H - pad.b - ((ty(y) - Y0) / dY) * (H - pad.t - pad.b);

  const xTicks = xLog ? decadeTicks(X0, X1).map((e) => 10 ** e) : Array.from({ length: 6 }, (_, i) => X0 + (i * dX) / 5);
  const yTicks = yLog ? decadeTicks(Y0, Y1).map((e) => 10 ** e) : Array.from({ length: 5 }, (_, i) => yMin + (i * (yMax - yMin)) / 4);

  const pathOf = (s: Series) => {
    let d = "";
    let pen = false;
    for (const [x, y] of s.points) {
      if (!valid(x, y)) {
        pen = false;
        continue;
      }
      d += `${pen ? "L" : "M"}${sx(x).toFixed(2)},${sy(y).toFixed(2)} `;
      pen = true;
    }
    return d;
  };

  return (
    <div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full h-auto select-none">
        <rect x={pad.l} y={pad.t} width={W - pad.l - pad.r} height={H - pad.t - pad.b} fill="#f8fafc" />
        {yTicks.map((t, i) => (
          <g key={`y${i}`}>
            <line x1={pad.l} x2={W - pad.r} y1={sy(t)} y2={sy(t)} stroke="#e2e8f0" />
            <text x={pad.l - 6} y={sy(t) + 4} fontSize="11" textAnchor="end" fill="#64748b">
              {fmt(t, 3)}
            </text>
          </g>
        ))}
        {xTicks.map((t, i) => (
          <g key={`x${i}`}>
            <line x1={sx(t)} x2={sx(t)} y1={pad.t} y2={H - pad.b} stroke="#e2e8f0" />
            <text x={sx(t)} y={H - pad.b + 14} fontSize="11" textAnchor="middle" fill="#64748b">
              {fmt(t, 3)}
            </text>
          </g>
        ))}
        {vline !== undefined && vline > 0 && (
          <line x1={sx(vline)} x2={sx(vline)} y1={pad.t} y2={H - pad.b} stroke="#f43f5e" strokeDasharray="4 3" />
        )}
        {series.map((s) => (
          <g key={s.name}>
            <path d={pathOf(s)} fill="none" stroke={s.color} strokeWidth={2} strokeDasharray={s.dashed ? "6 4" : undefined} />
            {s.dots &&
              s.points.filter(([x, y]) => valid(x, y)).map(([x, y], i) => (
                <circle key={i} cx={sx(x)} cy={sy(y)} r={2.5} fill={s.color} />
              ))}
          </g>
        ))}
        <text x={(pad.l + W - pad.r) / 2} y={H - 6} fontSize="12" textAnchor="middle" fill="#334155">
          {xLabel}
        </text>
        <text transform={`translate(14 ${(pad.t + H - pad.b) / 2}) rotate(-90)`} fontSize="12" textAnchor="middle" fill="#334155">
          {yLabel}
        </text>
      </svg>
      <div className="mt-2 flex flex-wrap gap-4 text-xs text-slate-600">
        {series.map((s) => (
          <span key={s.name} className="inline-flex items-center gap-1.5">
            <span className="inline-block h-0.5 w-5" style={{ background: s.color, borderTop: s.dashed ? `2px dashed ${s.color}` : undefined }} />
            {s.name}
          </span>
        ))}
      </div>
    </div>
  );
}
