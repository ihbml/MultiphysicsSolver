import type { ReactNode } from "react";
import { cn } from "@/utils/cn";

export function Section({
  id,
  index,
  kicker,
  title,
  lead,
  children,
}: {
  id: string;
  index: string;
  kicker: string;
  title: string;
  lead: string;
  children: ReactNode;
}) {
  return (
    <section id={id} className="scroll-mt-24 border-t border-white/5 px-5 py-16 sm:px-8 lg:px-14">
      <div className="mx-auto max-w-6xl">
        <header className="mb-10 max-w-3xl">
          <div className="mb-3 flex items-center gap-3">
            <span className="rounded-md border border-cyan-400/30 bg-cyan-400/10 px-2 py-0.5 font-mono text-[11px] tracking-widest text-cyan-300">
              {index}
            </span>
            <span className="font-mono text-[11px] tracking-[0.2em] text-slate-500 uppercase">
              {kicker}
            </span>
          </div>
          <h2 className="text-2xl font-semibold tracking-tight text-white sm:text-3xl">{title}</h2>
          <p className="mt-3 text-[15px] leading-relaxed text-slate-400">{lead}</p>
        </header>
        {children}
      </div>
    </section>
  );
}

export function Card({
  className,
  children,
  glow,
}: {
  className?: string;
  children: ReactNode;
  glow?: boolean;
}) {
  return (
    <div
      className={cn(
        "rounded-2xl border border-white/10 bg-white/[0.03] p-5 backdrop-blur-sm transition-colors",
        glow && "hover:border-cyan-400/40 hover:bg-white/[0.05]",
        className,
      )}
    >
      {children}
    </div>
  );
}

export function Chip({
  children,
  tone = "slate",
}: {
  children: ReactNode;
  tone?: "slate" | "cyan" | "emerald" | "amber" | "violet" | "rose";
}) {
  const tones: Record<string, string> = {
    slate: "border-white/10 bg-white/5 text-slate-300",
    cyan: "border-cyan-400/30 bg-cyan-400/10 text-cyan-300",
    emerald: "border-emerald-400/30 bg-emerald-400/10 text-emerald-300",
    amber: "border-amber-400/30 bg-amber-400/10 text-amber-300",
    violet: "border-violet-400/30 bg-violet-400/10 text-violet-300",
    rose: "border-rose-400/30 bg-rose-400/10 text-rose-300",
  };
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-full border px-2.5 py-0.5 font-mono text-[10px] tracking-wide whitespace-nowrap",
        tones[tone],
      )}
    >
      {children}
    </span>
  );
}

export function StatBlock({
  value,
  label,
  sub,
  tone = "cyan",
}: {
  value: string;
  label: string;
  sub?: string;
  tone?: "cyan" | "violet" | "emerald" | "amber";
}) {
  const tones: Record<string, string> = {
    cyan: "text-cyan-300",
    violet: "text-violet-300",
    emerald: "text-emerald-300",
    amber: "text-amber-300",
  };
  return (
    <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-4">
      <div className={cn("font-mono text-2xl font-semibold tabular-nums", tones[tone])}>{value}</div>
      <div className="mt-1 text-xs font-medium text-slate-300">{label}</div>
      {sub ? <div className="mt-1 text-[11px] leading-snug text-slate-500">{sub}</div> : null}
    </div>
  );
}

export function Toggle({
  checked,
  onChange,
  label,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
  label: string;
}) {
  return (
    <button
      type="button"
      onClick={() => onChange(!checked)}
      className="group flex items-center gap-2.5 text-left text-xs text-slate-300"
      aria-pressed={checked}
    >
      <span
        className={cn(
          "relative h-5 w-9 shrink-0 rounded-full border transition-colors",
          checked ? "border-cyan-400/60 bg-cyan-400/30" : "border-white/15 bg-white/5",
        )}
      >
        <span
          className={cn(
            "absolute top-0.5 h-3.5 w-3.5 rounded-full transition-all",
            checked ? "left-[18px] bg-cyan-300" : "left-0.5 bg-slate-500",
          )}
        />
      </span>
      <span className="group-hover:text-white">{label}</span>
    </button>
  );
}
