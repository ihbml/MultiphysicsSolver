import type { ReactNode } from "react";

export function Card({ title, subtitle, children }: { title: string; subtitle?: string; children: ReactNode }) {
  return (
    <section className="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
      <h2 className="text-base font-semibold text-slate-900">{title}</h2>
      {subtitle && <p className="mt-1 text-xs text-slate-500">{subtitle}</p>}
      <div className="mt-4">{children}</div>
    </section>
  );
}

export function Slider({
  label, value, min, max, step, onChange, format, log = false,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
  format?: (v: number) => string;
  log?: boolean;
}) {
  // For log sliders the slider position is log10(value)
  const pos = log ? Math.log10(value) : value;
  return (
    <label className="block">
      <div className="flex justify-between text-xs font-medium text-slate-600">
        <span>{label}</span>
        <span className="font-mono text-slate-900">{format ? format(value) : value.toPrecision(4)}</span>
      </div>
      <input
        type="range"
        className="mt-1 w-full accent-indigo-600"
        min={min}
        max={max}
        step={step}
        value={pos}
        onChange={(e) => {
          const v = Number(e.target.value);
          onChange(log ? Math.pow(10, v) : v);
        }}
      />
    </label>
  );
}

export function Stat({ label, value, tone = "default" }: { label: string; value: string; tone?: "default" | "ok" | "bad" }) {
  const toneCls = tone === "ok" ? "text-emerald-700" : tone === "bad" ? "text-rose-700" : "text-slate-900";
  return (
    <div className="rounded-xl bg-slate-50 p-3">
      <div className="text-[11px] uppercase tracking-wide text-slate-500">{label}</div>
      <div className={`mt-1 font-mono text-sm font-semibold ${toneCls}`}>{value}</div>
    </div>
  );
}

export type Series = { name: string; color: string; points: [number, number][]; dashed?: boolean; markers?: boolean };

export function LineChart({
  series, xLog = false, yLog = false, xLabel, yLabel, height = 300,
}: {
  series: Series[];
  xLog?: boolean;
  yLog?: boolean;
  xLabel: string;
  yLabel: string;
  height?: number;
}) {
  const W = 640;
  const H = height;
  const pad = { l: 64, r: 16, t: 14, b: 44 };
  const tx = (v: number) => (xLog ? Math.log10(v) : v);
  const ty = (v: number) => (yLog ? Math.log10(v) : v);
  const valid = series.flatMap((s) =>
    s.points.filter(([x, y]) => Number.isFinite(x) && Number.isFinite(y) && (!xLog || x > 0) && (!yLog || y > 0)),
  );
  if (valid.length === 0) return <div className="text-sm text-slate-500">No data</div>;
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const [x, y] of valid) {
    x0 = Math.min(x0, tx(x)); x1 = Math.max(x1, tx(x));
    y0 = Math.min(y0, ty(y)); y1 = Math.max(y1, ty(y));
  }
  if (x1 === x0) x1 = x0 + 1;
  if (y1 === y0) y1 = y0 + 1;
  const pw = W - pad.l - pad.r;
  const ph = H - pad.t - pad.b;
  const X = (x: number) => pad.l + ((tx(x) - x0) / (x1 - x0)) * pw;
  const Y = (y: number) => pad.t + ph - ((ty(y) - y0) / (y1 - y0)) * ph;
  const ticks = (a: number, b: number, n = 5) => Array.from({ length: n + 1 }, (_, i) => a + ((b - a) * i) / n);
  const fmt = (v: number, log: boolean) => {
    const val = log ? Math.pow(10, v) : v;
    return Math.abs(val) >= 1e4 || (Math.abs(val) < 1e-2 && val !== 0)
      ? val.toExponential(1)
      : val.toFixed(2).replace(/\.?0+$/, "");
  };
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="w-full">
      <rect x={pad.l} y={pad.t} width={pw} height={ph} fill="#f8fafc" stroke="#e2e8f0" />
      {ticks(x0, x1).map((t, i) => (
        <g key={`xt${i}`}>
          <line x1={pad.l + (pw * i) / 5} x2={pad.l + (pw * i) / 5} y1={pad.t} y2={pad.t + ph} stroke="#e2e8f0" />
          <text x={pad.l + (pw * i) / 5} y={H - 22} fontSize="11" textAnchor="middle" fill="#64748b">
            {fmt(t, xLog)}
          </text>
        </g>
      ))}
      {ticks(y0, y1).map((t, i) => (
        <g key={`yt${i}`}>
          <line x1={pad.l} x2={pad.l + pw} y1={pad.t + ph - (ph * i) / 5} y2={pad.t + ph - (ph * i) / 5} stroke="#e2e8f0" />
          <text x={pad.l - 6} y={pad.t + ph - (ph * i) / 5 + 4} fontSize="11" textAnchor="end" fill="#64748b">
            {fmt(t, yLog)}
          </text>
        </g>
      ))}
      {series.map((s) => {
        const pts = s.points.filter(([x, y]) => Number.isFinite(y) && (!xLog || x > 0) && (!yLog || y > 0));
        const d = pts.map(([x, y], i) => `${i === 0 ? "M" : "L"}${X(x).toFixed(1)},${Y(y).toFixed(1)}`).join(" ");
        return (
          <g key={s.name}>
            <path d={d} fill="none" stroke={s.color} strokeWidth={2.2} strokeDasharray={s.dashed ? "6 4" : undefined} />
            {s.markers &&
              pts.map(([x, y], i) => <circle key={i} cx={X(x)} cy={Y(y)} r={3} fill={s.color} />)}
          </g>
        );
      })}
      <text x={pad.l + pw / 2} y={H - 4} fontSize="12" textAnchor="middle" fill="#334155">{xLabel}</text>
      <text transform={`translate(14 ${pad.t + ph / 2}) rotate(-90)`} fontSize="12" textAnchor="middle" fill="#334155">
        {yLabel}
      </text>
    </svg>
  );
}

export function Legend({ series }: { series: Series[] }) {
  return (
    <div className="mt-2 flex flex-wrap gap-4 text-xs text-slate-600">
      {series.map((s) => (
        <span key={s.name} className="inline-flex items-center gap-1.5">
          <span className="inline-block h-0.5 w-5" style={{ background: s.color, borderTop: s.dashed ? "2px dashed" : undefined, borderColor: s.color }} />
          {s.name}
        </span>
      ))}
    </div>
  );
}
