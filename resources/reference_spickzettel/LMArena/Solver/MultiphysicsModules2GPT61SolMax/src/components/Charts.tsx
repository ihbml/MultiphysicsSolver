import { useId, useState } from "react";

export type ChartSeries = {
  name: string;
  color: string;
  values: number[];
  dashed?: boolean;
};

export function LineChart({
  series,
  xMin = 0,
  xMax = 120,
  logX = false,
  yMin,
  yMax,
  unit = "",
  xUnit = "s",
  labels,
  compact = false,
  xValues,
}: {
  series: ChartSeries[];
  xMin?: number;
  xMax?: number;
  logX?: boolean;
  yMin?: number;
  yMax?: number;
  unit?: string;
  xUnit?: string;
  labels?: string[];
  compact?: boolean;
  xValues?: number[];
}) {
  const id = useId().replace(/:/g, "");
  const [hover, setHover] = useState<number | null>(null);
  const all = series.flatMap((s) => s.values).filter(Number.isFinite);
  const dataMin = Math.min(...all);
  const dataMax = Math.max(...all);
  const range = dataMax - dataMin || Math.max(0.1, Math.abs(dataMax) * 0.1);
  const magnitude = 10 ** Math.floor(Math.log10(range / 4));
  const step =
    ([1, 2, 2.5, 5, 10].find((n) => n * magnitude >= range / 4) || 10) *
    magnitude;
  const min = yMin ?? Math.floor((dataMin - 0.05 * range) / step) * step;
  const max = yMax ?? Math.ceil((dataMax + 0.05 * range) / step) * step;
  const w = 720;
  const h = compact ? 174 : 205;
  const p = { l: 44, r: 16, t: 15, b: 31 };
  const transform = (value: number) => (logX ? Math.log(value) : value);
  const x = (i: number, n: number) => {
    const fraction = xValues
      ? (transform(xValues[i]) - transform(xMin)) /
        (transform(xMax) - transform(xMin))
      : i / Math.max(1, n - 1);
    return p.l + fraction * (w - p.l - p.r);
  };
  const y = (v: number) =>
    p.t + (1 - (v - min) / (max - min || 1)) * (h - p.t - p.b);
  const length = series[0]?.values.length || 1;
  const hi =
    hover === null
      ? -1
      : xValues
        ? xValues.reduce(
            (nearest, _, i) =>
              Math.abs(x(i, length) - p.l - hover * (w - p.l - p.r)) <
              Math.abs(x(nearest, length) - p.l - hover * (w - p.l - p.r))
                ? i
                : nearest,
            0,
          )
        : Math.round(hover * (length - 1));
  return (
    <div className="line-chart">
      <svg
        viewBox={`0 0 ${w} ${h}`}
        role="img"
        aria-label={`${series.map((s) => s.name).join(", ")} chart`}
        onMouseLeave={() => setHover(null)}
        onMouseMove={(event) => {
          const bounds = event.currentTarget.getBoundingClientRect();
          setHover(
            Math.max(
              0,
              Math.min(
                1,
                (((event.clientX - bounds.left) / bounds.width) * w - p.l) /
                  (w - p.l - p.r),
              ),
            ),
          );
        }}
      >
        <defs>
          {series.map((s, i) => (
            <linearGradient
              key={s.name}
              id={`${id}-${i}`}
              x1="0"
              x2="0"
              y1="0"
              y2="1"
            >
              <stop offset="0%" stopColor={s.color} stopOpacity="0.16" />
              <stop offset="100%" stopColor={s.color} stopOpacity="0" />
            </linearGradient>
          ))}
        </defs>
        {[0, 1, 2, 3, 4].map((i) => (
          <g key={i}>
            <line
              x1={p.l}
              x2={w - p.r}
              y1={p.t + (i / 4) * (h - p.t - p.b)}
              y2={p.t + (i / 4) * (h - p.t - p.b)}
              stroke="#26303d"
              strokeDasharray="3 5"
              strokeOpacity=".65"
            />
            <text
              x={p.l - 12}
              y={p.t + (i / 4) * (h - p.t - p.b) + 4}
              textAnchor="end"
              className="chart-label"
            >
              {(max - (i / 4) * (max - min)).toFixed(
                max - min < 1 ? 2 : max - min < 10 ? 1 : 0,
              )}
            </text>
          </g>
        ))}
        {[0, 1, 2, 3, 4, 5, 6].map((i) => (
          <text
            key={i}
            x={p.l + (i / 6) * (w - p.l - p.r)}
            y={h - 8}
            textAnchor="middle"
            className="chart-label"
          >
            {labels?.[i] ??
              `${+(logX ? xMin * (xMax / xMin) ** (i / 6) : xMin + (i / 6) * (xMax - xMin)).toFixed(1)}${i === 6 ? ` ${xUnit}` : ""}`}
          </text>
        ))}
        {series.map((s, si) => {
          const path = s.values
            .map(
              (v, i) =>
                `${i === 0 ? "M" : "L"}${x(i, s.values.length).toFixed(2)},${y(v).toFixed(2)}`,
            )
            .join(" ");
          return (
            <g key={s.name}>
              {si === 0 && (
                <path
                  d={`${path} L${x(s.values.length - 1, s.values.length)},${h - p.b} L${x(0, s.values.length)},${h - p.b} Z`}
                  fill={`url(#${id}-${si})`}
                />
              )}
              <path
                d={path}
                fill="none"
                stroke={s.color}
                strokeWidth={si === 0 ? 2.4 : 1.6}
                strokeDasharray={s.dashed ? "5 5" : undefined}
                strokeLinecap="round"
                strokeLinejoin="round"
              />
              {hi >= 0 && (
                <circle
                  cx={x(hi, length)}
                  cy={y(s.values[hi] ?? 0)}
                  r="3.5"
                  fill={s.color}
                  stroke="#111722"
                  strokeWidth="2"
                />
              )}
            </g>
          );
        })}
        {hi >= 0 && (
          <line
            x1={x(hi, length)}
            x2={x(hi, length)}
            y1={p.t}
            y2={h - p.b}
            stroke="#8993a6"
            strokeDasharray="3 4"
          />
        )}
        <text x="3" y="13" className="chart-label">
          {unit}
        </text>
      </svg>
      {hi >= 0 && (
        <div
          className="chart-tooltip"
          style={{
            left: `clamp(8px, calc(${(hover ?? 0) * 100}% - 95px), calc(100% - 200px))`,
          }}
        >
          <span>
            {(
              xValues?.[hi] ??
              xMin + (hi / Math.max(1, length - 1)) * (xMax - xMin)
            ).toFixed(1)}{" "}
            {xUnit}
          </span>
          {series.map((s) => (
            <div key={s.name}>
              <i style={{ background: s.color }} />
              {s.name}
              <b>
                {s.values[hi]?.toFixed(2)} {unit}
              </b>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function Sparkline({
  color,
  values,
}: {
  color: string;
  values: number[];
}) {
  const min = Math.min(...values);
  const max = Math.max(...values);
  const path = values
    .map(
      (v, i) =>
        `${i === 0 ? "M" : "L"}${(i / Math.max(1, values.length - 1)) * 90},${35 - ((v - min) / (max - min || 1)) * 30}`,
    )
    .join(" ");
  return (
    <svg className="sparkline" viewBox="0 0 94 40" aria-hidden="true">
      <path
        d={path}
        stroke={color}
        strokeWidth="1.7"
        fill="none"
        strokeLinecap="round"
      />
    </svg>
  );
}

export function Legend({
  items,
}: {
  items: { name: string; color: string; dashed?: boolean }[];
}) {
  return (
    <div className="chart-legend">
      {items.map((item) => (
        <span key={item.name}>
          <i
            style={{ background: item.color, opacity: item.dashed ? 0.6 : 1 }}
          />
          {item.name}
        </span>
      ))}
    </div>
  );
}
