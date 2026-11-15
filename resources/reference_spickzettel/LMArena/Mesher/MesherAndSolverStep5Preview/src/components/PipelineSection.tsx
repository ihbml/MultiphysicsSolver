import { useState } from "react";
import { PIPELINE } from "@/data/content";
import { cn } from "@/utils/cn";
import { Chip } from "./ui";

export function PipelineSection() {
  const [idx, setIdx] = useState(0);
  const step = PIPELINE[idx];

  return (
    <div>
      {/* Step-Rail */}
      <div className="mb-6 overflow-x-auto pb-2 scroll-thin">
        <div className="flex min-w-max items-center gap-2">
          {PIPELINE.map((s, i) => {
            const active = i === idx;
            const done = i < idx;
            return (
              <button
                key={s.id}
                type="button"
                onClick={() => setIdx(i)}
                className={cn(
                  "group flex items-center gap-2 rounded-xl border px-3 py-2 text-left transition-colors",
                  active
                    ? "border-cyan-400/40 bg-cyan-400/10"
                    : "border-white/10 bg-white/[0.02] hover:border-white/20",
                )}
              >
                <span
                  className={cn(
                    "flex h-6 w-6 shrink-0 items-center justify-center rounded-lg font-mono text-[10px] font-semibold",
                    active
                      ? "bg-cyan-400 text-[#06202a]"
                      : done
                        ? "bg-emerald-400/20 text-emerald-300"
                        : "bg-white/5 text-slate-500",
                  )}
                >
                  {i}
                </span>
                <span
                  className={cn(
                    "max-w-[150px] truncate text-[12px] font-medium",
                    active ? "text-white" : "text-slate-400 group-hover:text-slate-200",
                  )}
                >
                  {s.title}
                </span>
              </button>
            );
          })}
        </div>
      </div>

      {/* Detail */}
      <div className="grid gap-5 lg:grid-cols-[1.6fr_1fr]">
        <div className="rounded-2xl border border-white/10 bg-gradient-to-br from-white/[0.06] to-white/[0.02] p-6">
          <div className="mb-2 font-mono text-[11px] tracking-[0.18em] text-cyan-300 uppercase">
            {step.kicker}
          </div>
          <h3 className="text-xl font-semibold text-white">{step.title}</h3>
          <p className="mt-2 text-[14px] leading-relaxed text-slate-300">{step.lead}</p>

          <ul className="mt-5 space-y-3">
            {step.bullets.map((b) => (
              <li key={b} className="flex gap-3 text-[13px] leading-relaxed text-slate-400">
                <span className="mt-1.5 h-1.5 w-1.5 shrink-0 rounded-full bg-cyan-400/80" />
                <span>{b}</span>
              </li>
            ))}
          </ul>

          <div className="mt-6 flex flex-wrap items-center gap-2">
            <button
              type="button"
              onClick={() => setIdx((i) => Math.max(0, i - 1))}
              disabled={idx === 0}
              className="rounded-lg border border-white/10 bg-white/5 px-3 py-1.5 text-[12px] text-slate-300 transition-colors hover:border-white/25 hover:text-white disabled:opacity-30"
            >
              ← Zurück
            </button>
            <button
              type="button"
              onClick={() => setIdx((i) => Math.min(PIPELINE.length - 1, i + 1))}
              disabled={idx === PIPELINE.length - 1}
              className="rounded-lg border border-cyan-400/30 bg-cyan-400/10 px-3 py-1.5 text-[12px] text-cyan-300 transition-colors hover:bg-cyan-400/20 disabled:opacity-30"
            >
              Weiter →
            </button>
          </div>
        </div>

        <div className="space-y-3">
          <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
            <div className="mb-2 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Output
            </div>
            <div className="font-mono text-[13px] leading-relaxed text-emerald-300">
              {step.out}
            </div>
          </div>
          <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
            <div className="mb-3 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Beteiligte Bausteine
            </div>
            <div className="flex flex-wrap gap-1.5">
              {step.tags.map((t) => (
                <Chip key={t} tone="cyan">
                  {t}
                </Chip>
              ))}
            </div>
          </div>
          <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
            <div className="mb-3 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Fortschritt
            </div>
            <div className="flex gap-1">
              {PIPELINE.map((s, i) => (
                <span
                  key={s.id}
                  className={cn(
                    "h-1.5 flex-1 rounded-full transition-colors",
                    i <= idx ? "bg-cyan-400" : "bg-white/10",
                  )}
                />
              ))}
            </div>
            <div className="mt-3 text-[11px] text-slate-500">
              Schritt {idx + 1} von {PIPELINE.length} · Rückkopplung: Adjoint → Metrik → Netz
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
