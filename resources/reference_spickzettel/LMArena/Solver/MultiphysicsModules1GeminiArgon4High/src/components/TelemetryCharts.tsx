import React, { useState } from 'react';

export interface TraceSeries {
  label: string;
  color: string;
  data: number[];
  dashed?: boolean;
  unit?: string;
}

interface MultiTraceChartProps {
  title: string;
  subtitle?: string;
  xValues: number[];
  xUnit?: string;
  yUnit?: string;
  series: TraceSeries[];
  height?: number;
  yMinOverride?: number;
  yMaxOverride?: number;
  rightBadge?: string;
}

export const MultiTraceChart: React.FC<MultiTraceChartProps> = ({
  title,
  subtitle,
  xValues,
  xUnit = 's',
  yUnit = '',
  series,
  height = 152,
  yMinOverride,
  yMaxOverride,
  rightBadge,
}) => {
  const [hoverIdx, setHoverIdx] = useState<number | null>(null);

  const width = 520;
  const padL = 46;
  const padR = 14;
  const padT = 14;
  const padB = 24;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  const n = xValues.length;
  const xMin = n > 0 ? xValues[0] : 0;
  const xMax = n > 1 ? xValues[n - 1] : xMin + 1;

  let yMin = yMinOverride ?? Infinity;
  let yMax = yMaxOverride ?? -Infinity;

  if (yMinOverride === undefined || yMaxOverride === undefined) {
    for (const s of series) {
      for (const v of s.data) {
        if (Number.isFinite(v)) {
          if (yMinOverride === undefined && v < yMin) yMin = v;
          if (yMaxOverride === undefined && v > yMax) yMax = v;
        }
      }
    }
  }
  if (!Number.isFinite(yMin) || !Number.isFinite(yMax) || Math.abs(yMax - yMin) < 1e-9) {
    yMin = 0;
    yMax = 1;
  } else if (yMinOverride === undefined && yMaxOverride === undefined) {
    const span = yMax - yMin;
    yMin -= span * 0.06;
    yMax += span * 0.08;
  }

  const mapX = (x: number) =>
    padL + ((x - xMin) / Math.max(1e-12, xMax - xMin)) * plotW;
  const mapY = (y: number) =>
    padT + plotH - ((y - yMin) / Math.max(1e-12, yMax - yMin)) * plotH;

  const yTicks = [0, 0.5, 1].map((t) => yMin + t * (yMax - yMin));

  const formatNum = (val: number) => {
    const abs = Math.abs(val);
    if (abs >= 1000) return val.toFixed(0);
    if (abs >= 10) return val.toFixed(1);
    if (abs >= 0.1) return val.toFixed(2);
    if (abs === 0) return '0.00';
    return val.toExponential(1);
  };

  return (
    <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
      {/* Header */}
      <div className="mb-1.5 flex flex-wrap items-center justify-between gap-2">
        <div>
          <div className="flex items-center gap-2">
            <h4 className="font-display text-xs font-semibold tracking-wide text-slate-100">
              {title}
            </h4>
            {rightBadge && (
              <span className="rounded border border-[#1E293B] bg-[#131B28] px-1.5 py-0.5 font-mono-tabular text-[10px] text-cyan-300">
                {rightBadge}
              </span>
            )}
          </div>
          {subtitle && (
            <p className="font-mono-tabular text-[10px] text-[#64748B]">
              {subtitle}
            </p>
          )}
        </div>

        {/* Compact Legend */}
        <div className="flex flex-wrap items-center gap-2.5">
          {series.map((s) => {
            const latestVal =
              hoverIdx !== null && s.data[hoverIdx] !== undefined
                ? s.data[hoverIdx]
                : s.data[s.data.length - 1];
            return (
              <div key={s.label} className="flex items-center gap-1 text-[10px]">
                <span
                  className="inline-block h-2 w-2 rounded-full"
                  style={{ backgroundColor: s.color }}
                />
                <span className="text-[#94A3B8]">{s.label}:</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {latestVal !== undefined ? formatNum(latestVal) : '--'}
                  {s.unit ?? ''}
                </span>
              </div>
            );
          })}
        </div>
      </div>

      {/* SVG Plot */}
      <svg
        viewBox={`0 0 ${width} ${height}`}
        className="w-full select-none overflow-visible"
        onMouseMove={(e) => {
          if (n < 2) return;
          const rect = e.currentTarget.getBoundingClientRect();
          const relX = ((e.clientX - rect.left) / rect.width) * width;
          const frac = (relX - padL) / plotW;
          const idx = Math.round(Math.max(0, Math.min(1, frac)) * (n - 1));
          setHoverIdx(idx);
        }}
        onMouseLeave={() => setHoverIdx(null)}
      >
        {/* Gridlines */}
        {yTicks.map((yv, i) => {
          const py = mapY(yv);
          return (
            <g key={i}>
              <line
                x1={padL}
                y1={py}
                x2={width - padR}
                y2={py}
                stroke="#1E293B"
                strokeDasharray={i === 1 ? '3 3' : undefined}
                strokeWidth={1}
              />
              <text
                x={padL - 5}
                y={py + 3}
                textAnchor="end"
                className="fill-[#64748B] font-mono-tabular text-[9px]"
              >
                {formatNum(yv)}
              </text>
            </g>
          );
        })}

        {/* X Axis labels */}
        <text
          x={padL}
          y={height - 5}
          textAnchor="start"
          className="fill-[#64748B] font-mono-tabular text-[9px]"
        >
          {formatNum(xMin)} {xUnit}
        </text>
        {yUnit && (
          <text
            x={padL + plotW / 2}
            y={height - 5}
            textAnchor="middle"
            className="fill-[#64748B] font-mono-tabular text-[9px]"
          >
            [{yUnit}]
          </text>
        )}
        <text
          x={width - padR}
          y={height - 5}
          textAnchor="end"
          className="fill-[#64748B] font-mono-tabular text-[9px]"
        >
          {formatNum(xMax)} {xUnit}
        </text>

        {/* Polylines */}
        {series.map((s) => {
          if (s.data.length < 2) return null;
          const pts = s.data
            .map((v, i) => {
              const xv = xValues[i] ?? i;
              const px = mapX(xv);
              const py = mapY(Math.max(yMin, Math.min(yMax, v)));
              return `${px.toFixed(1)},${py.toFixed(1)}`;
            })
            .join(' ');
          return (
            <polyline
              key={s.label}
              fill="none"
              stroke={s.color}
              strokeWidth={1.85}
              strokeDasharray={s.dashed ? '4 3' : undefined}
              points={pts}
            />
          );
        })}

        {/* Hover crosshair */}
        {hoverIdx !== null && xValues[hoverIdx] !== undefined && (
          <g>
            <line
              x1={mapX(xValues[hoverIdx])}
              y1={padT}
              x2={mapX(xValues[hoverIdx])}
              y2={padT + plotH}
              stroke="#94A3B8"
              strokeWidth={1}
              strokeDasharray="2 2"
            />
            {series.map((s) => {
              const v = s.data[hoverIdx];
              if (v === undefined) return null;
              return (
                <circle
                  key={s.label}
                  cx={mapX(xValues[hoverIdx])}
                  cy={mapY(Math.max(yMin, Math.min(yMax, v)))}
                  r={3}
                  fill={s.color}
                  stroke="#070A0F"
                  strokeWidth={1}
                />
              );
            })}
          </g>
        )}
      </svg>
    </div>
  );
};

interface ThirdOctaveBarChartProps {
  bands: { centerHz: number; spl_dB: number }[];
  height?: number;
}

export const ThirdOctaveBarChart: React.FC<ThirdOctaveBarChartProps> = ({
  bands,
  height = 148,
}) => {
  const width = 520;
  const padL = 42;
  const padR = 12;
  const padT = 14;
  const padB = 26;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  const yMin = 40;
  const yMax = 145;

  return (
    <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
      <div className="mb-1.5 flex items-center justify-between">
        <div>
          <h4 className="font-display text-xs font-semibold tracking-wide text-slate-100">
            ANSI S1.11 1/3-Octave Band Acoustic Spectrum
          </h4>
          <p className="font-mono-tabular text-[10px] text-[#64748B]">
            Integrated band SPL [dB re 20 μPa] from Hann-windowed Radix-2 FFT PSD
          </p>
        </div>
        <span className="rounded border border-cyan-500/30 bg-cyan-500/10 px-1.5 py-0.5 font-mono-tabular text-[10px] text-cyan-300">
          15 Bands (50 Hz – 1.25 kHz)
        </span>
      </div>

      <svg viewBox={`0 0 ${width} ${height}`} className="w-full select-none">
        {[50, 80, 110, 140].map((db) => {
          const py = padT + plotH - ((db - yMin) / (yMax - yMin)) * plotH;
          return (
            <g key={db}>
              <line
                x1={padL}
                y1={py}
                x2={width - padR}
                y2={py}
                stroke="#1E293B"
                strokeDasharray="3 3"
              />
              <text
                x={padL - 5}
                y={py + 3}
                textAnchor="end"
                className="fill-[#64748B] font-mono-tabular text-[9px]"
              >
                {db} dB
              </text>
            </g>
          );
        })}

        {bands.map((b, i) => {
          const slotW = plotW / bands.length;
          const barW = Math.max(6, slotW * 0.64);
          const x = padL + i * slotW + (slotW - barW) * 0.5;
          const clamped = Math.max(yMin, Math.min(yMax, b.spl_dB));
          const barH = ((clamped - yMin) / (yMax - yMin)) * plotH;
          const y = padT + plotH - barH;
          const isPeak = b.spl_dB > 112;

          return (
            <g key={b.centerHz}>
              <rect
                x={x}
                y={y}
                width={barW}
                height={barH}
                rx={2}
                fill={isPeak ? '#06B6D4' : '#3B82F6'}
                fillOpacity={isPeak ? 0.9 : 0.65}
              />
              <text
                x={x + barW / 2}
                y={height - 7}
                textAnchor="middle"
                className="fill-[#94A3B8] font-mono-tabular text-[8px]"
              >
                {b.centerHz >= 1000 ? `${(b.centerHz / 1000).toFixed(2)}k` : b.centerHz}
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
};
