import { ReactNode } from "react";
import { cn } from "../utils/cn";
import { fmt } from "../lib/colors";

export function Panel({
  title,
  subtitle,
  children,
  right,
}: {
  title: string;
  subtitle?: string;
  children: ReactNode;
  right?: ReactNode;
}) {
  return (
    <section className="rounded-xl border border-white/10 bg-slate-900/50 p-3">
      <header className="mb-2.5 flex items-baseline justify-between gap-2">
        <div>
          <h3 className="text-[11px] font-semibold uppercase tracking-[0.14em] text-slate-300">
            {title}
          </h3>
          {subtitle && (
            <p className="mt-0.5 text-[10px] leading-tight text-slate-500">{subtitle}</p>
          )}
        </div>
        {right}
      </header>
      <div className="space-y-2.5">{children}</div>
    </section>
  );
}

export function Slider({
  label,
  value,
  min,
  max,
  step,
  onChange,
  format,
  hint,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
  format?: (v: number) => string;
  hint?: string;
}) {
  return (
    <label className="block">
      <div className="flex items-baseline justify-between">
        <span className="text-[11px] text-slate-400">{label}</span>
        <span className="font-mono text-[11px] text-cyan-300">
          {format ? format(value) : fmt(value)}
        </span>
      </div>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(parseFloat(e.target.value))}
        className="mt-1 h-1 w-full cursor-pointer appearance-none rounded-full bg-slate-700 accent-cyan-400 [&::-webkit-slider-thumb]:h-3 [&::-webkit-slider-thumb]:w-3 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-cyan-400"
      />
      {hint && <p className="mt-0.5 text-[10px] leading-tight text-slate-500">{hint}</p>}
    </label>
  );
}

export function Select<T extends string>({
  label,
  value,
  options,
  onChange,
}: {
  label: string;
  value: T;
  options: { value: T; label: string }[];
  onChange: (v: T) => void;
}) {
  return (
    <label className="block">
      <span className="text-[11px] text-slate-400">{label}</span>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value as T)}
        className="mt-1 w-full rounded-md border border-white/10 bg-slate-950 px-2 py-1.5 text-[12px] text-slate-200 outline-none focus:border-cyan-400/60"
      >
        {options.map((o) => (
          <option key={o.value} value={o.value}>
            {o.label}
          </option>
        ))}
      </select>
    </label>
  );
}

export function Toggle({
  label,
  checked,
  onChange,
  color,
}: {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
  color?: string;
}) {
  return (
    <button
      onClick={() => onChange(!checked)}
      className={cn(
        "flex items-center gap-1.5 rounded-md px-2 py-1 text-[11px] ring-1 transition",
        checked
          ? "bg-cyan-500/15 text-cyan-200 ring-cyan-400/40"
          : "bg-slate-800/60 text-slate-400 ring-white/5 hover:text-slate-200",
      )}
    >
      <span
        className="h-1.5 w-1.5 rounded-full"
        style={{ background: checked ? color ?? "#22d3ee" : "#475569" }}
      />
      {label}
    </button>
  );
}

export function Btn({
  children,
  onClick,
  tone = "default",
  disabled,
  className,
}: {
  children: ReactNode;
  onClick?: () => void;
  tone?: "default" | "primary" | "ghost";
  disabled?: boolean;
  className?: string;
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className={cn(
        "rounded-md px-2.5 py-1.5 text-[11px] font-medium transition disabled:cursor-not-allowed disabled:opacity-40",
        tone === "primary" &&
          "bg-cyan-500 text-slate-950 hover:bg-cyan-400 shadow-[0_0_20px_-6px] shadow-cyan-400/70",
        tone === "default" && "bg-slate-800 text-slate-200 ring-1 ring-white/10 hover:bg-slate-700",
        tone === "ghost" && "text-slate-400 hover:text-slate-100",
        className,
      )}
    >
      {children}
    </button>
  );
}

export function Stat({
  label,
  value,
  unit,
  tone = "default",
  hint,
}: {
  label: string;
  value: string;
  unit?: string;
  tone?: "default" | "good" | "warn" | "bad";
  hint?: string;
}) {
  return (
    <div className="rounded-lg bg-slate-950/60 px-2 py-1.5 ring-1 ring-white/5" title={hint}>
      <div className="text-[9.5px] uppercase tracking-wider text-slate-500">{label}</div>
      <div
        className={cn(
          "font-mono text-[12.5px] leading-tight",
          tone === "good" && "text-emerald-300",
          tone === "warn" && "text-amber-300",
          tone === "bad" && "text-rose-300",
          tone === "default" && "text-slate-100",
        )}
      >
        {value}
        {unit && <span className="ml-0.5 text-[10px] text-slate-500">{unit}</span>}
      </div>
    </div>
  );
}

export function Bars({
  data,
  lo,
  hi,
  color = "#22d3ee",
  height = 42,
  unit = "",
}: {
  data: number[];
  lo: number;
  hi: number;
  color?: string;
  height?: number;
  unit?: string;
}) {
  const max = Math.max(1, ...data);
  return (
    <div>
      <div className="flex items-end gap-[1px]" style={{ height }}>
        {data.map((v, i) => (
          <div
            key={i}
            className="flex-1 rounded-t-[1px]"
            style={{
              height: `${(v / max) * 100}%`,
              background: color,
              opacity: 0.35 + 0.65 * (v / max),
            }}
            title={`${fmt(lo + ((hi - lo) * i) / data.length)} – ${fmt(
              lo + ((hi - lo) * (i + 1)) / data.length,
            )}: ${v}`}
          />
        ))}
      </div>
      <div className="mt-0.5 flex justify-between font-mono text-[9px] text-slate-500">
        <span>
          {fmt(lo)}
          {unit}
        </span>
        <span>
          {fmt(hi)}
          {unit}
        </span>
      </div>
    </div>
  );
}

export function Spark({
  pts,
  height = 46,
  color = "#34d399",
}: {
  pts: number[];
  height?: number;
  color?: string;
}) {
  if (pts.length < 2)
    return <div style={{ height }} className="rounded bg-slate-950/60" />;
  const logs = pts.map((v) => Math.log10(Math.max(v, 1e-12)));
  const lo = Math.min(...logs);
  const hi = Math.max(...logs);
  const span = hi - lo || 1;
  const d = logs
    .map((v, i) => {
      const x = (i / (logs.length - 1)) * 100;
      const y = 100 - ((v - lo) / span) * 100;
      return `${i === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
    })
    .join(" ");
  return (
    <div className="relative">
      <svg viewBox="0 0 100 100" preserveAspectRatio="none" style={{ height }} className="w-full">
        <path d={d} fill="none" stroke={color} strokeWidth={1.6} vectorEffect="non-scaling-stroke" />
      </svg>
      <div className="flex justify-between font-mono text-[9px] text-slate-500">
        <span>1e{hi.toFixed(1)}</span>
        <span>→ 1e{logs[logs.length - 1].toFixed(1)}</span>
      </div>
    </div>
  );
}

export function Row({ children }: { children: ReactNode }) {
  return <div className="grid grid-cols-2 gap-1.5">{children}</div>;
}

export function Note({ children }: { children: ReactNode }) {
  return (
    <p className="rounded-md border-l-2 border-cyan-400/40 bg-cyan-400/5 px-2 py-1.5 text-[10.5px] leading-snug text-slate-400">
      {children}
    </p>
  );
}
