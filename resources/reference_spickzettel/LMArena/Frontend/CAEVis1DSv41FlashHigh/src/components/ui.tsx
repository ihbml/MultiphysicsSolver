import React, { useState } from "react";
import { ChevronDown, ChevronRight, Search, Minus, Plus } from "lucide-react";
import { ColormapName, gradientCss } from "../lib/colormaps";

export const cn = (...a: (string | false | null | undefined)[]) => a.filter(Boolean).join(" ");

export function Section({
  title, icon, right, children, defaultOpen = true, dense = false, subtitle, className,
}: {
  title: string; icon?: React.ReactNode; right?: React.ReactNode;
  children: React.ReactNode; defaultOpen?: boolean; dense?: boolean; subtitle?: string; className?: string;
}) {
  const [open, setOpen] = useState(defaultOpen);
  return (
    <div className={cn("border-b border-[#212228]", className)}>
      <button
        onClick={() => setOpen((o) => !o)}
        className="flex h-9 w-full items-center gap-2 px-3 text-left hover:bg-white/[0.02] transition-colors"
      >
        <span className="text-[#5f6472]">{open ? <ChevronDown size={12} /> : <ChevronRight size={12} />}</span>
        {icon && <span className="text-[#79808f]">{icon}</span>}
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">{title}</span>
        {subtitle && <span className="mono truncate text-[9px] text-[#5b616e]">{subtitle}</span>}
        <span className="ml-auto flex items-center gap-1.5">{right}</span>
      </button>
      {open && <div className={cn(dense ? "px-3 pb-2" : "px-3 pb-3", "pt-0.5")}>{children}</div>}
    </div>
  );
}

export function Field({ label, children, hint, right }: { label: React.ReactNode; children: React.ReactNode; hint?: string; right?: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between gap-2 py-1" title={hint}>
      <span className="flex min-w-0 items-center gap-1 text-[10.5px] text-[#838998]">{label}{right}</span>
      <div className="flex shrink-0 items-center gap-1">{children}</div>
    </div>
  );
}

export function NumField({
  value, onChange, unit, min, max, width = "w-[92px]", digits,
}: { value: number; onChange: (v: number) => void; unit?: string; step?: number; min?: number; max?: number; width?: string; digits?: number }) {
  const [text, setText] = useState<string | null>(null);
  const display = text ?? (digits != null ? value.toFixed(digits) : String(value));
  return (
    <div className={cn("flex h-[26px] items-center overflow-hidden rounded-[5px] border border-[#2a2b33] bg-[#0f1014]", width)}>
      <input
        value={display}
        onChange={(e) => {
          setText(e.target.value);
          const n = Number(e.target.value);
          if (!Number.isNaN(n)) {
            const clamped = Math.min(max ?? Infinity, Math.max(min ?? -Infinity, n));
            onChange(clamped);
          }
        }}
        onBlur={() => setText(null)}
        className="h-full min-w-0 flex-1 border-0 bg-transparent px-2 text-right text-[10.5px] tabular text-[#dde1ea] outline-none focus:ring-0"
      />
      {unit && <span className="mono shrink-0 border-l border-[#24252c] px-1.5 text-[9px] text-[#6a7080]">{unit}</span>}
    </div>
  );
}

export function Slider({
  label, value, onChange, min, max, step = 1, unit, digits = 0, disabled, accent = false, compact,
}: {
  label: React.ReactNode; value: number; onChange: (v: number) => void; min: number; max: number;
  step?: number; unit?: string; digits?: number; disabled?: boolean; accent?: boolean; compact?: boolean;
}) {
  return (
    <div className={cn("py-1", disabled && "opacity-40 pointer-events-none")}>
      <div className="mb-[5px] flex items-center justify-between">
        <span className="text-[10.5px] text-[#888e9c]">{label}</span>
        <span className="mono rounded-[3px] border border-[#2a2b33] bg-[#16171c] px-1.5 py-[1px] text-[9.5px] tabular text-[#c5cad6]">
          {value.toFixed(digits)}{unit ? <span className="ml-0.5 text-[#6d7381]">{unit}</span> : null}
        </span>
      </div>
      {!compact && (
        <input type="range" min={min} max={max} step={step} value={value} onChange={(e) => onChange(Number(e.target.value))} className={accent ? "accent-blue" : ""} />
      )}
    </div>
  );
}

export function Toggle({ checked, onChange, label, size = "sm" }: { checked: boolean; onChange: (v: boolean) => void; label?: React.ReactNode; size?: "sm" | "xs" }) {
  return (
    <label className="flex cursor-pointer items-center justify-between gap-2">
      {label && <span className="text-[10.5px] text-[#888e9c]">{label}</span>}
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        onClick={() => onChange(!checked)}
        className={cn(
          "relative shrink-0 rounded-full border transition-all duration-150",
          size === "sm" ? "h-[15px] w-[26px]" : "h-[13px] w-[23px]",
          checked ? "border-[#4d7cff]/60 bg-[#004AFD]" : "border-[#34353d] bg-[#26272e]"
        )}
      >
        <span className={cn(
          "absolute top-1/2 -translate-y-1/2 rounded-full bg-white shadow transition-all duration-150",
          size === "sm" ? "h-[9px] w-[9px]" : "h-[7px] w-[7px]",
          checked ? (size === "sm" ? "left-[14px]" : "left-[13px]") : "left-[3px]"
        )} />
      </button>
    </label>
  );
}

type BtnVariant = "primary" | "secondary" | "ghost" | "danger" | "success";
export function Btn({
  children, onClick, variant = "secondary", icon, className, disabled, title, size = "md",
}: {
  children?: React.ReactNode; onClick?: () => void; variant?: BtnVariant; icon?: React.ReactNode;
  className?: string; disabled?: boolean; title?: string; size?: "sm" | "md" | "xs";
}) {
  const variants: Record<BtnVariant, string> = {
    primary: "bg-[#004AFD] text-white border-[#3b70ff] hover:bg-[#1b5dff] shadow-[0_1px_0_rgba(255,255,255,0.12)_inset,0_4px_14px_-6px_rgba(0,74,253,0.9)]",
    secondary: "bg-[#1d1e24] text-[#d4d8e2] border-[#33343d] hover:bg-[#26272f] hover:border-[#41424c]",
    ghost: "bg-transparent text-[#969cab] border-transparent hover:bg-white/[0.05] hover:text-white",
    danger: "bg-[#EF233C]/12 text-[#ff7c8e] border-[#EF233C]/45 hover:bg-[#EF233C]/20",
    success: "bg-[#23D18B]/12 text-[#6ff0be] border-[#23D18B]/35 hover:bg-[#23D18B]/20",
  };
  return (
    <button
      title={title}
      disabled={disabled}
      onClick={onClick}
      className={cn(
        "inline-flex items-center justify-center gap-1.5 rounded-[5px] border font-medium transition-all duration-120 disabled:cursor-not-allowed disabled:opacity-40",
        size === "md" ? "h-[28px] px-2.5 text-[11px]" : size === "sm" ? "h-[24px] px-2 text-[10px]" : "h-[20px] px-1.5 text-[9.5px]",
        variants[variant], className
      )}
    >
      {icon}{children}
    </button>
  );
}

export function IconBtn({ children, onClick, active, title, className, danger }: {
  children: React.ReactNode; onClick?: () => void; active?: boolean; title?: string; className?: string; danger?: boolean;
}) {
  return (
    <button
      title={title}
      onClick={onClick}
      className={cn(
        "inline-flex h-[24px] w-[24px] items-center justify-center rounded-[4px] border transition-colors",
        active ? "border-[#004AFD]/60 bg-[#004AFD]/18 text-[#9fbcff]" : "border-transparent text-[#7e8494] hover:border-[#31323a] hover:bg-[#202127] hover:text-white",
        danger && "hover:text-[#ff7c8e] hover:border-[#EF233C]/40",
        className
      )}
    >
      {children}
    </button>
  );
}

export function Chip({ children, tone = "neutral", dot, className }: {
  children: React.ReactNode; tone?: "neutral" | "blue" | "red" | "amber" | "green" | "violet" | "cyan"; dot?: string; className?: string;
}) {
  const tones = {
    neutral: "border-[#33343d] bg-[#1c1d23] text-[#9ba1af]",
    blue: "border-[#004AFD]/50 bg-[#004AFD]/12 text-[#8fb0ff]",
    red: "border-[#EF233C]/45 bg-[#EF233C]/12 text-[#ff8798]",
    amber: "border-[#FFB020]/35 bg-[#FFB020]/10 text-[#ffcb6b]",
    green: "border-[#23D18B]/35 bg-[#23D18B]/10 text-[#65e5b1]",
    violet: "border-[#A78BFA]/35 bg-[#A78BFA]/10 text-[#c3b2ff]",
    cyan: "border-[#41E0FF]/35 bg-[#41E0FF]/10 text-[#8cecff]",
  };
  return (
    <span className={cn("mono inline-flex items-center gap-1 whitespace-nowrap rounded-[3px] border px-1.5 py-[2px] text-[8.5px] uppercase tracking-wide", tones[tone], className)}>
      {dot && <span className="h-1 w-1 rounded-full" style={{ background: dot }} />}
      {children}
    </span>
  );
}

export function Segmented<T extends string>({
  options, value, onChange, className, full,
}: { options: { value: T; label: React.ReactNode }[]; value: T; onChange: (v: T) => void; className?: string; full?: boolean }) {
  return (
    <div className={cn("flex items-center gap-[2px] rounded-[5px] border border-[#2b2c34] bg-[#111216] p-[2px]", full && "w-full", className)}>
      {options.map((o) => (
        <button
          key={o.value}
          onClick={() => onChange(o.value)}
          className={cn(
            "flex h-[22px] items-center justify-center gap-1 rounded-[3px] px-2 text-[10px] font-medium transition-all",
            full && "flex-1",
            value === o.value ? "bg-[#2b2d36] text-white shadow-[0_1px_2px_rgba(0,0,0,0.4)]" : "text-[#7f8594] hover:text-[#c9cdd7]"
          )}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function SearchInput({ value, onChange, placeholder }: { value: string; onChange: (v: string) => void; placeholder?: string }) {
  return (
    <div className="flex h-[26px] items-center gap-1.5 rounded-[5px] border border-[#2a2b33] bg-[#0e0f13] px-2 focus-within:border-[#3f5fa8]">
      <Search size={11} className="shrink-0 text-[#5f6472]" />
      <input
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        className="h-full w-full border-0 bg-transparent p-0 text-[10px] text-[#d4d8e2] outline-none placeholder:text-[#50555f] focus:ring-0"
      />
    </div>
  );
}

export function Colorbar({
  colormap, min, max, unit, label, digits = 2, center,
}: { colormap: ColormapName; min: number; max: number; unit?: string; label?: string; digits?: number; center?: number }) {
  const ticks = center != null ? [min, center, max] : [min, (min + max) / 2, max];
  return (
    <div className="w-full">
      {label && <div className="mb-1 text-[9.5px] text-[#838998]">{label}</div>}
      <div className="h-[7px] w-full rounded-[2px] border border-white/10" style={{ background: gradientCss(colormap) }} />
      <div className="mt-1 flex justify-between">
        {ticks.map((t, i) => (
          <span key={i} className="mono text-[8.5px] tabular text-[#6d7381]">
            {Math.abs(t) >= 10000 ? (t / 1000).toFixed(1) + "k" : t.toFixed(digits)}{i === ticks.length - 1 && unit ? ` ${unit}` : ""}
          </span>
        ))}
      </div>
    </div>
  );
}

export function StatBox({ label, value, unit, tone = "neutral", icon, mono = true }: {
  label: string; value: React.ReactNode; unit?: string; tone?: "neutral" | "blue" | "green" | "amber" | "red"; icon?: React.ReactNode; mono?: boolean;
}) {
  const colors = {
    neutral: "text-[#e9ebf0]", blue: "text-[#8fb0ff]", green: "text-[#65e5b1]", amber: "text-[#ffcb6b]", red: "text-[#ff8798]",
  };
  return (
    <div className="min-w-0 flex-1 rounded-[5px] border border-[#272830] bg-[#141519] px-2 py-[6px]">
      <div className="mb-[2px] flex items-center gap-1 text-[8.5px] uppercase tracking-[0.08em] text-[#6b7180]">
        {icon}{label}
      </div>
      <div className={cn("truncate text-[15px] leading-tight", mono && "mono tabular", colors[tone])}>
        {value}
        {unit && <span className="ml-1 text-[9px] font-normal text-[#6a7080]">{unit}</span>}
      </div>
    </div>
  );
}

export function InlineKey({ children }: { children: React.ReactNode }) {
  return <kbd className="kbd">{children}</kbd>;
}

export { Minus, Plus };
