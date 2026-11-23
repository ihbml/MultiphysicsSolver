import React, { useId, useMemo, useRef, useState } from "react";
import { Pt } from "../lib/sim";

const GRID = "#24252c";
const TEXT = "#7e8594";
const TEXT_HI = "#c8cdd8";

type Series = { name: string; color: string; points: Pt[]; dashed?: boolean; width?: number; dots?: boolean; area?: boolean };
type Band = { from: number; to: number; color: string; label?: string };
type HLine = { y: number; color: string; label?: string; dashed?: boolean };

function niceDomain(values: number[], pad = 0.08): [number, number] {
  if (!values.length) return [0, 1];
  const mn = Math.min(...values), mx = Math.max(...values);
  const span = mx - mn || Math.abs(mx) || 1;
  return [mn - span * pad, mx + span * pad];
}

function ticks([a, b]: [number, number], count = 5) {
  const step = (b - a) / count;
  return Array.from({ length: count + 1 }, (_, i) => a + step * i);
}

export function ProfileChart({
  title, subtitle, series, xLabel, yLabel, height = 216, invertY = false, bands = [], hlines = [],
  yDomain, xDomain, xScale = "linear", markers = [], legend = true, xTicks, unit = "", markerValue,
}: {
  title: string; subtitle?: string; series: Series[]; xLabel: string; yLabel: string; height?: number;
  invertY?: boolean; bands?: Band[]; hlines?: HLine[]; yDomain?: [number, number]; xDomain?: [number, number];
  xScale?: "linear" | "log"; markers?: { x: number; y: number; label?: string; color?: string }[];
  legend?: boolean; xTicks?: number; unit?: string; markerValue?: (x: number) => string;
}) {
  const uid = useId().replace(/:/g, "");
  const W = 540;
  const M = { l: 46, r: 14, t: 30, b: 28 };
  const H = height;
  const pw = W - M.l - M.r, ph = H - M.t - M.b;
  const allPts = series.flatMap((s) => s.points);
  const [x0, x1] = xDomain ?? (xScale === "log" ? [0.4, 10000] : niceDomain(allPts.map((p) => p.x), 0.02));
  const [y0, y1] = yDomain ?? niceDomain(allPts.map((p) => p.y), 0.12);
  const [hoverX, setHoverX] = useState<number | null>(null);
  const svgRef = useRef<SVGSVGElement>(null);

  const sx = (x: number) => {
    if (xScale === "log") {
      const lo = Math.log10(Math.max(1e-4, x0)), hi = Math.log10(Math.max(1e-3, x1));
      return M.l + ((Math.log10(Math.max(1e-4, x)) - lo) / (hi - lo)) * pw;
    }
    return M.l + ((x - x0) / (x1 - x0 || 1)) * pw;
  };
  const sy = (y: number) => {
    const f = (y - y0) / (y1 - y0 || 1);
    return M.t + (invertY ? f : 1 - f) * ph;
  };
  const path = (pts: Pt[]) => pts.map((p, i) => `${i === 0 ? "M" : "L"}${sx(p.x).toFixed(2)},${sy(p.y).toFixed(2)}`).join(" ");
  const yt = ticks([y0, y1], 4);
  const xt = xScale === "log"
    ? [0.5, 1, 10, 100, 1000, 10000].filter((v) => v >= x0 && v <= x1)
    : xTicks ? ticks([x0, x1], xTicks) : ticks([x0, x1], 5);

  const hover = useMemo(() => {
    if (hoverX == null) return null;
    const target = (x0 + (hoverX - M.l) / pw * (x1 - x0));
    let best: { s: Series; p: Pt } | null = null;
    series.forEach((s) => s.points.forEach((p) => {
      if (!best || Math.abs(p.x - target) < Math.abs(best.p.x - target)) best = { s, p };
    }));
    return best as { s: Series; p: Pt } | null;
  }, [hoverX, series, x0, x1, pw, M.l]);

  return (
    <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
      <div className="mb-1 flex items-start justify-between gap-2 px-0.5">
        <div className="min-w-0">
          <div className="truncate text-[10.5px] font-medium text-[#d6dbe6]">{title}</div>
          {subtitle && <div className="mono truncate text-[8.5px] text-[#646a78]">{subtitle}</div>}
        </div>
        {legend && (
          <div className="flex shrink-0 flex-wrap items-center justify-end gap-x-2 gap-y-0.5">
            {series.map((s) => (
              <span key={s.name} className="mono flex items-center gap-1 text-[8px] text-[#8b91a0]">
                <span className="h-[2px] w-[9px] rounded-full" style={{ background: s.color }} />
                {s.name}
              </span>
            ))}
          </div>
        )}
      </div>
      <svg
        ref={svgRef}
        viewBox={`0 0 ${W} ${H}`}
        className="block w-full"
        style={{ height }}
        onMouseMove={(e) => {
          const r = svgRef.current!.getBoundingClientRect();
          const vx = ((e.clientX - r.left) / r.width) * W;
          setHoverX(vx >= M.l && vx <= W - M.r ? vx : null);
        }}
        onMouseLeave={() => setHoverX(null)}
      >
        <defs>
          <clipPath id={`clip-${uid}`}><rect x={M.l} y={M.t} width={pw} height={ph} /></clipPath>
          <linearGradient id={`g-${uid}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor={series[0]?.color || "#4d7cff"} stopOpacity="0.24" />
            <stop offset="100%" stopColor={series[0]?.color || "#4d7cff"} stopOpacity="0" />
          </linearGradient>
        </defs>
        <rect x={M.l} y={M.t} width={pw} height={ph} fill="#0d0e12" stroke="#22232a" rx="2" />
        {bands.map((b, i) => (
          <g key={i}>
            <rect
              x={M.l} y={Math.min(sy(b.from), sy(b.to))} width={pw}
              height={Math.abs(sy(b.to) - sy(b.from))} fill={b.color}
            />
            {b.label && (
              <text x={W - M.r - 4} y={Math.min(sy(b.from), sy(b.to)) + 9} textAnchor="end" fontSize="7.5" fill={b.color} fontFamily="JetBrains Mono">
                {b.label}
              </text>
            )}
          </g>
        ))}
        {yt.map((t, i) => (
          <g key={i}>
            <line x1={M.l} x2={W - M.r} y1={sy(t)} y2={sy(t)} stroke={GRID} strokeWidth="1" />
            <text x={M.l - 6} y={sy(t) + 3} textAnchor="end" fontSize="8" fill={TEXT} fontFamily="JetBrains Mono">
              {Math.abs(t) >= 10000 ? (t / 1000).toFixed(1) + "k" : Math.abs(t) >= 100 ? t.toFixed(0) : Math.abs(t) >= 10 ? t.toFixed(1) : t.toFixed(Math.abs(t) < 1 ? 2 : 1)}
            </text>
          </g>
        ))}
        {xt.map((t, i) => (
          <g key={i}>
            <line x1={sx(t)} x2={sx(t)} y1={M.t} y2={M.t + ph} stroke={GRID} strokeWidth="1" strokeDasharray={i === 0 ? "" : "2 3"} />
            <text x={sx(t)} y={H - 12} textAnchor="middle" fontSize="8" fill={TEXT} fontFamily="JetBrains Mono">
              {t >= 1000 ? `${(t / 1000).toFixed(t % 1000 ? 1 : 0)}k` : t >= 10 ? t.toFixed(0) : t.toFixed(t < 1 ? 1 : 1)}
            </text>
          </g>
        ))}
        {invertY && (
          <text x={W - M.r - 3} y={M.t + 9} textAnchor="end" fontSize="7" fill="#8b93a6" fontFamily="JetBrains Mono">INVERTED Y ⇣</text>
        )}
        {hlines.map((h, i) => (
          <g key={i}>
            <line x1={M.l} x2={W - M.r} y1={sy(h.y)} y2={sy(h.y)} stroke={h.color} strokeWidth="0.85" strokeDasharray={h.dashed ? "4 3" : ""} opacity="0.9" />
            {h.label && (
              <text x={M.l + 4} y={sy(h.y) - 3} fontSize="7" fill={h.color} fontFamily="JetBrains Mono">{h.label}</text>
            )}
          </g>
        ))}
        <g clipPath={`url(#clip-${uid})`}>
          {series.map((s, si) => (
            <g key={s.name}>
              {s.area && si === 0 && <path d={`${path(s.points)} L${sx(s.points[s.points.length - 1]?.x ?? x1)},${sy(y0)} L${sx(s.points[0]?.x ?? x0)},${sy(y0)} Z`} fill={`url(#g-${uid})`} />}
              <path d={path(s.points)} fill="none" stroke={s.color} strokeWidth={s.width ?? 1.5} strokeDasharray={s.dashed ? "4 3" : ""} strokeLinejoin="round" strokeLinecap="round" />
              {s.dots && s.points.map((p, i) => <circle key={i} cx={sx(p.x)} cy={sy(p.y)} r={1.5} fill={s.color} />)}
            </g>
          ))}
          {markers.map((m, i) => (
            <g key={i}>
              <circle cx={sx(m.x)} cy={sy(m.y)} r="3.2" fill="#0d0e12" stroke={m.color || "#004AFD"} strokeWidth="1.6" />
              {m.label && <text x={sx(m.x) + 6} y={sy(m.y) - 5} fontSize="7.5" fill={m.color || "#9fbcff"} fontFamily="JetBrains Mono">{m.label}</text>}
            </g>
          ))}
          {hoverX != null && <line x1={hoverX} x2={hoverX} y1={M.t} y2={M.t + ph} stroke="#8fa9e8" strokeWidth="0.8" strokeDasharray="2 2" opacity="0.8" />}
        </g>
        {hover && (
          <g>
            <rect x={Math.min(hoverX! + 8, W - 150)} y={M.t + 5} width="140" height="28" rx="3" fill="rgba(13,14,18,0.95)" stroke="#343641" />
            <text x={Math.min(hoverX! + 14, W - 144)} y={M.t + 16} fontSize="7.5" fill={TEXT_HI} fontFamily="JetBrains Mono">{hover.s.name}</text>
            <text x={Math.min(hoverX! + 14, W - 144)} y={M.t + 26} fontSize="7.5" fill={hover.s.color} fontFamily="JetBrains Mono">
              x {hover.p.x.toFixed(3)} · y {hover.p.y.toFixed(3)} {unit}
            </text>
          </g>
        )}
        <text x={M.l + pw / 2} y={H - 1} textAnchor="middle" fontSize="7.8" fill={TEXT} fontFamily="JetBrains Mono">{xLabel}</text>
        <text transform={`translate(9 ${M.t + ph / 2}) rotate(-90)`} textAnchor="middle" fontSize="7.8" fill={TEXT} fontFamily="JetBrains Mono">{yLabel}</text>
      </svg>
      {markerValue && <div className="mono mt-0.5 text-[8px] text-[#5e6472]">{markerValue(0)}</div>}
    </div>
  );
}

export function ParetoChart({
  points, height = 190, xLabel, yLabel,
}: { points: { x: number; y: number; kind: "sweep" | "a" | "b"; label?: string }[]; height?: number; xLabel: string; yLabel: string }) {
  const W = 360, H = height, M = { l: 38, r: 12, t: 16, b: 26 };
  const pw = W - M.l - M.r, ph = H - M.t - M.b;
  const [x0, x1] = niceDomain(points.map((p) => p.x), 0.1);
  const [y0, y1] = niceDomain(points.map((p) => p.y), 0.1);
  const sx = (x: number) => M.l + ((x - x0) / (x1 - x0)) * pw;
  const sy = (y: number) => M.t + (1 - (y - y0) / (y1 - y0)) * ph;
  const front = useMemo(() => {
    const sorted = [...points.filter((p) => p.kind === "sweep")].sort((a, b) => a.x - b.x);
    const pf: typeof sorted = [];
    let best = Infinity;
    sorted.forEach((p) => { if (p.y < best) { best = p.y; pf.push(p); } });
    return pf;
  }, [points]);
  const [hovered, setHovered] = useState<number | null>(null);
  return (
    <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
      <div className="mb-1 flex items-center justify-between">
        <span className="text-[10px] font-medium text-[#d6dbe6]">Pareto front · sweep #37</span>
        <span className="mono text-[8px] text-[#646a78]">n = {points.filter((p) => p.kind === "sweep").length} · non-dominated</span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} className="block w-full" style={{ height }}>
        <rect x={M.l} y={M.t} width={pw} height={ph} fill="#0d0e12" stroke="#22232a" rx="2" />
        {ticks([y0, y1], 4).map((t, i) => (
          <g key={i}>
            <line x1={M.l} x2={W - M.r} y1={sy(t)} y2={sy(t)} stroke={GRID} />
            <text x={M.l - 5} y={sy(t) + 3} textAnchor="end" fontSize="7.5" fill={TEXT} fontFamily="JetBrains Mono">{t.toFixed(0)}</text>
          </g>
        ))}
        {ticks([x0, x1], 4).map((t, i) => (
          <text key={i} x={sx(t)} y={H - 10} textAnchor="middle" fontSize="7.5" fill={TEXT} fontFamily="JetBrains Mono">{t.toFixed(1)}</text>
        ))}
        <polyline points={front.map((p) => `${sx(p.x)},${sy(p.y)}`).join(" ")} fill="none" stroke="#4d7cff" strokeWidth="1" strokeDasharray="3 3" opacity="0.6" />
        {points.map((p, i) => {
          const col = p.kind === "a" ? "#AAB1C0" : p.kind === "b" ? "#41E0FF" : "#3b6bff";
          const r = p.kind === "sweep" ? 3 : 5;
          return (
            <g key={i} onMouseEnter={() => setHovered(i)} onMouseLeave={() => setHovered(null)} style={{ cursor: "crosshair" }}>
              <circle cx={sx(p.x)} cy={sy(p.y)} r={r + 4} fill="transparent" />
              <circle cx={sx(p.x)} cy={sy(p.y)} r={r} fill={p.kind === "sweep" ? "#101116" : col} stroke={col} strokeWidth={p.kind === "sweep" ? 1.3 : 1.8} />
              {p.kind !== "sweep" && (
                <text x={sx(p.x) + 8} y={sy(p.y) - 6} fontSize="8" fill={col} fontFamily="JetBrains Mono">{p.kind === "a" ? "A · baseline" : "B · adjoint"}</text>
              )}
              {hovered === i && p.kind === "sweep" && (
                <g>
                  <rect x={sx(p.x) + 7} y={sy(p.y) - 20} width="72" height="16" rx="2" fill="#14151a" stroke="#353742" />
                  <text x={sx(p.x) + 11} y={sy(p.y) - 9} fontSize="7" fill="#c8cdd8" fontFamily="JetBrains Mono">Δp₀ {p.x.toFixed(2)} · Cd {p.y}</text>
                </g>
              )}
            </g>
          );
        })}
        <text x={M.l + pw / 2} y={H - 1} textAnchor="middle" fontSize="7.5" fill={TEXT} fontFamily="JetBrains Mono">{xLabel}</text>
        <text transform={`translate(8 ${M.t + ph / 2}) rotate(-90)`} textAnchor="middle" fontSize="7.5" fill={TEXT} fontFamily="JetBrains Mono">{yLabel}</text>
      </svg>
    </div>
  );
}

export function DeltaBar({ label, value, unit, max, good = "down" }: { label: string; value: number; unit: string; max: number; good?: "down" | "up" }) {
  const frac = Math.min(1, Math.abs(value) / max);
  const positiveGood = good === "up" ? value > 0 : value < 0;
  const col = positiveGood ? "#23D18B" : "#EF233C";
  return (
    <div className="flex items-center gap-2">
      <span className="w-[74px] shrink-0 truncate text-[9px] text-[#7e8594]">{label}</span>
      <div className="relative h-[5px] flex-1 rounded-[2px] bg-[#1e1f26]">
        <div className="absolute left-1/2 top-0 h-full w-px bg-[#3a3c46]" />
        <div
          className="absolute top-0 h-full rounded-[2px]"
          style={{
            width: `${Math.max(2, frac * 50)}%`,
            [value >= 0 ? "left" : "right"]: "50%",
            background: col,
          } as React.CSSProperties}
        />
      </div>
      <span className="mono w-[74px] shrink-0 text-right text-[8.5px] tabular" style={{ color: col }}>
        {value > 0 ? "+" : ""}{value.toFixed(Math.abs(value) >= 10 ? 1 : Math.abs(value) >= 1 ? 2 : 4)} {unit}
      </span>
    </div>
  );
}

export function MiniSpark({ points, color = "#4d7cff", height = 26 }: { points: Pt[]; color?: string; height?: number }) {
  const W = 100, H = height;
  const xs = points.map((p) => p.x), ys = points.map((p) => p.y);
  const [x0, x1] = [Math.min(...xs), Math.max(...xs)];
  const [y0, y1] = niceDomain(ys, 0.05);
  const d = points.map((p, i) => `${i === 0 ? "M" : "L"}${(((p.x - x0) / (x1 - x0 || 1)) * W).toFixed(1)},${(H - ((p.y - y0) / (y1 - y0 || 1)) * H).toFixed(1)}`).join(" ");
  return <svg viewBox={`0 0 ${W} ${H}`} style={{ height, width: "100%" }} preserveAspectRatio="none"><path d={d} fill="none" stroke={color} strokeWidth="1.2" /></svg>;
}
