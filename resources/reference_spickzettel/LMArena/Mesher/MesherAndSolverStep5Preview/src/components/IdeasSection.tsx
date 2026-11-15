import { useMemo, useState } from "react";
import { IDEAS, type Idea } from "@/data/content";
import { cn } from "@/utils/cn";
import { Card, Chip } from "./ui";

const FILTERS = [
  { id: "alle", label: "Alle" },
  { id: "klein", label: "Aufwand klein" },
  { id: "mittel", label: "Aufwand mittel" },
  { id: "groß", label: "Aufwand groß" },
] as const;

const STATUS_TONE: Record<Idea["status"], "cyan" | "amber" | "emerald"> = {
  neu: "cyan",
  geplant: "amber",
  "im Bau": "emerald",
};

const EFFORT_TONE: Record<Idea["effort"], string> = {
  klein: "border-emerald-400/30 bg-emerald-400/10 text-emerald-300",
  mittel: "border-amber-400/30 bg-amber-400/10 text-amber-300",
  groß: "border-rose-400/30 bg-rose-400/10 text-rose-300",
};

const CATEGORIES = Array.from(new Set(IDEAS.map((i) => i.category)));

export function IdeasSection() {
  const [status, setStatus] = useState<(typeof FILTERS)[number]["id"]>("alle");
  const [cat, setCat] = useState<string>("alle");

  const ideas = useMemo(
    () =>
      IDEAS.filter(
        (i) => (status === "alle" || i.effort === status) && (cat === "alle" || i.category === cat),
      ),
    [status, cat],
  );

  const countFor = (id: string) =>
    id === "alle" ? IDEAS.length : IDEAS.filter((i) => i.effort === id).length;

  return (
    <div>
      <div className="mb-6 flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
        <div className="flex flex-wrap items-center gap-2">
          {FILTERS.map((f) => (
            <button
              key={f.id}
              type="button"
              onClick={() => setStatus(f.id)}
              className={cn(
                "rounded-full border px-3 py-1.5 text-[11.5px] font-medium transition-colors",
                status === f.id
                  ? "border-cyan-400/40 bg-cyan-400/10 text-cyan-300"
                  : "border-white/10 bg-white/[0.03] text-slate-400 hover:border-white/20 hover:text-slate-200",
              )}
            >
              {f.label} ({countFor(f.id)})
            </button>
          ))}
        </div>

        <div className="flex flex-wrap items-center gap-1.5">
          <span className="mr-1 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
            Bereich
          </span>
          <button
            type="button"
            onClick={() => setCat("alle")}
            className={cn(
              "rounded-lg border px-2.5 py-1 text-[11px] transition-colors",
              cat === "alle" ? "border-violet-400/40 bg-violet-400/10 text-violet-300" : "border-white/10 text-slate-500 hover:text-slate-300",
            )}
          >
            alle
          </button>
          {CATEGORIES.map((c) => (
            <button
              key={c}
              type="button"
              onClick={() => setCat(c)}
              className={cn(
                "rounded-lg border px-2.5 py-1 text-[11px] transition-colors",
                cat === c ? "border-violet-400/40 bg-violet-400/10 text-violet-300" : "border-white/10 text-slate-500 hover:text-slate-300",
              )}
            >
              {c}
            </button>
          ))}
        </div>
      </div>

      <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
        {ideas.map((idea) => (
          <Card key={idea.title} glow className="flex flex-col gap-3">
            <div className="flex items-start justify-between gap-2">
              <h3 className="text-[14px] leading-snug font-semibold text-white">{idea.title}</h3>
              <Chip tone={STATUS_TONE[idea.status]}>{idea.status}</Chip>
            </div>

            <div className="flex flex-wrap gap-1.5">
              <Chip tone="violet">{idea.category}</Chip>
              <span
                className={cn(
                  "inline-flex items-center rounded-full border px-2.5 py-0.5 font-mono text-[10px]",
                  EFFORT_TONE[idea.effort],
                )}
              >
                Aufwand: {idea.effort}
              </span>
            </div>

            <p className="text-[12.5px] leading-relaxed text-slate-400">{idea.why}</p>

            <ul className="mt-auto space-y-1.5 border-t border-white/5 pt-3">
              {idea.how.map((h) => (
                <li key={h} className="flex gap-2 text-[11.5px] leading-relaxed text-slate-500">
                  <span className="mt-1.5 h-1 w-1 shrink-0 rounded-full bg-violet-400/70" />
                  <span>{h}</span>
                </li>
              ))}
            </ul>
          </Card>
        ))}
      </div>

      {ideas.length === 0 ? (
        <div className="rounded-2xl border border-dashed border-white/10 p-10 text-center text-sm text-slate-500">
          Keine Idee in dieser Kombination.
        </div>
      ) : null}

      {/* Priorisierte nächste Schritte */}
      <div className="mt-8 rounded-2xl border border-white/10 bg-gradient-to-br from-cyan-400/[0.07] via-white/[0.03] to-violet-400/[0.06] p-6">
        <h3 className="mb-1 text-sm font-semibold text-white">
          Wenn ich nur drei Dinge sofort umsetzen würde
        </h3>
        <p className="mb-5 text-[12.5px] text-slate-400">
          Nach Aufwand/Nutzen sortiert — alles andere lässt sich später ohne Umbau nachziehen.
        </p>
        <div className="grid gap-4 md:grid-cols-3">
          <NextStep
            n="01"
            title="MMS-Suite + Autotuning"
            text="Die Konvergenzordnung jedes neuen Kernels misst die CI, und Kernel Tuner bestimmt ε_skew, ε_ortho und Blockgrößen. Das ist die Basis, auf der alle anderen Ideen verlässlich werden."
            tone="cyan"
          />
          <NextStep
            n="02"
            title="Mixed-Precision-Defektkontrolle"
            text="Iteratives Refinement mit FP64-Defekt macht das FP16/TF32-Tier erst beweisbar schnell — und die MeshErrorMap sagt, welche Zellen FP64 wirklich brauchen."
            tone="violet"
          />
          <NextStep
            n="03"
            title="Pilot + Metrik-Predictor im Produkt"
            text="Physics-Guided Pilot-Meshing und der neurale Metrik-Selector sind für den Anwender sofort sichtbar: bessere Netze in Sekunden statt Stunden und ein Startfeld, das Iterationen spart."
            tone="emerald"
          />
        </div>
      </div>
    </div>
  );
}

function NextStep({
  n,
  title,
  text,
  tone,
}: {
  n: string;
  title: string;
  text: string;
  tone: "cyan" | "violet" | "emerald";
}) {
  const tones = {
    cyan: "text-cyan-300",
    violet: "text-violet-300",
    emerald: "text-emerald-300",
  };
  return (
    <div className="rounded-2xl border border-white/10 bg-black/20 p-4">
      <div className={cn("mb-2 font-mono text-lg font-semibold", tones[tone])}>{n}</div>
      <div className="mb-1.5 text-[13px] font-semibold text-white">{title}</div>
      <p className="text-[12px] leading-relaxed text-slate-400">{text}</p>
    </div>
  );
}
