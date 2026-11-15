import { useMemo, useState } from "react";
import { STACK, type StackCategory } from "@/data/content";
import { cn } from "@/utils/cn";
import { Card, Chip } from "./ui";

const CATEGORIES: StackCategory[] = [
  "Lineare Algebra",
  "Kinetik & Thermo",
  "Solver-Frameworks",
  "Kopplung & Transfer",
  "GPU & Abstraktion",
  "Werkzeuge & I/O",
];

const STATUS_FILTERS = ["alle", "im Stack", "Vorschlag"] as const;

export function StackSection() {
  const [cat, setCat] = useState<StackCategory | "alle">("alle");
  const [status, setStatus] = useState<(typeof STATUS_FILTERS)[number]>("alle");
  const [query, setQuery] = useState("");

  const entries = useMemo(() => {
    const q = query.trim().toLowerCase();
    return STACK.filter((e) => {
      if (cat !== "alle" && e.category !== cat) return false;
      if (status !== "alle" && e.status !== status) return false;
      if (q && !`${e.name} ${e.role} ${e.backend}`.toLowerCase().includes(q)) return false;
      return true;
    });
  }, [cat, status, query]);

  const inStack = STACK.filter((e) => e.status === "im Stack").length;

  return (
    <div>
      {/* Steuerung */}
      <div className="mb-6 space-y-4">
        <div className="flex flex-wrap items-center gap-2">
          <FilterPill active={cat === "alle"} onClick={() => setCat("alle")}>
            Alle Kategorien
          </FilterPill>
          {CATEGORIES.map((c) => (
            <FilterPill key={c} active={cat === c} onClick={() => setCat(c)}>
              {c}
            </FilterPill>
          ))}
        </div>

        <div className="flex flex-wrap items-center gap-3">
          <div className="flex gap-1.5 rounded-xl border border-white/10 bg-white/[0.03] p-1">
            {STATUS_FILTERS.map((s) => (
              <button
                key={s}
                type="button"
                onClick={() => setStatus(s)}
                className={cn(
                  "rounded-lg px-3 py-1.5 text-[11.5px] font-medium transition-colors",
                  status === s ? "bg-cyan-400/15 text-cyan-300" : "text-slate-400 hover:text-slate-200",
                )}
              >
                {s === "alle" ? `Alle (${STACK.length})` : s === "im Stack" ? `Im Stack (${inStack})` : `Vorschläge (${STACK.length - inStack})`}
              </button>
            ))}
          </div>

          <div className="relative min-w-[220px] flex-1">
            <svg
              viewBox="0 0 24 24"
              className="pointer-events-none absolute top-1/2 left-3 h-4 w-4 -translate-y-1/2 text-slate-500"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
            >
              <circle cx="11" cy="11" r="7" />
              <path d="m20 20-3.5-3.5" />
            </svg>
            <input
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="Suchen: ginkgo, Kokkos, VOF, Enzyme …"
              className="w-full rounded-xl border border-white/10 bg-white/[0.03] py-2 pr-3 pl-9 text-[13px] text-slate-200 placeholder:text-slate-600 focus:border-cyan-400/40 focus:outline-none"
            />
          </div>
        </div>
      </div>

      {/* Ergebnis */}
      <div className="mb-4 font-mono text-[11px] text-slate-500">
        {entries.length} Bausteine ·{" "}
        <span className="text-slate-400">
          {cat === "alle" ? "alle Kategorien" : cat}
        </span>
      </div>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {entries.map((e) => (
          <Card key={e.name} glow className="flex flex-col gap-2.5">
            <div className="flex items-start justify-between gap-2">
              <h3 className="font-mono text-[13.5px] font-semibold text-white">
                {e.href ? (
                  <a
                    href={e.href}
                    target="_blank"
                    rel="noreferrer noopener"
                    className="hover:text-cyan-300 hover:underline"
                  >
                    {e.name}
                  </a>
                ) : (
                  e.name
                )}
              </h3>
              <Chip tone={e.status === "im Stack" ? "emerald" : "amber"}>{e.status}</Chip>
            </div>
            <p className="text-[12.5px] leading-relaxed text-slate-400">{e.role}</p>
            <div className="mt-auto flex flex-wrap items-center gap-1.5 pt-1">
              <Chip tone="violet">{e.category}</Chip>
              <Chip>{e.backend}</Chip>
            </div>
          </Card>
        ))}
      </div>

      {entries.length === 0 ? (
        <div className="rounded-2xl border border-dashed border-white/10 p-10 text-center text-sm text-slate-500">
          Kein Treffer — Filter zurücksetzen?
        </div>
      ) : null}

      <div className="mt-6 rounded-2xl border border-white/10 bg-white/[0.02] p-5">
        <h3 className="mb-2 text-sm font-semibold text-white">Lückenlogik</h3>
        <p className="text-[12.5px] leading-relaxed text-slate-400">
          Der bestehende Stack deckt Löser, Thermo, Kinetik und Optimierung bereits breit ab.
          Zwei Lücken bleiben: (1){" "}
          <span className="text-slate-200">
            GPU-fähige Kinik/Thermodynamik
          </span>{" "}
          — Cantera und CoolProp sind wegen virtueller Methoden und dynamischer Allokation
          nicht direkt in Device-Kerneln oder durch Enzyme ableitbar; Abhilfe schaffen
          codegenerierende Übersetzer (PelePhysics/CEPTR, TChem, teqp). (2){" "}
          <span className="text-slate-200">
            Kopplungs- und Mapping-Schicht
          </span>{" "}
          zwischen ECO GEN, SPHinXsys und SU2 — dafür sind preCICE, MUI, ArborX und
          DataTransferKit vorgesehen.
        </p>
      </div>
    </div>
  );
}

function FilterPill({
  active,
  onClick,
  children,
}: {
  active: boolean;
  onClick: () => void;
  children: React.ReactNode;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={cn(
        "rounded-full border px-3 py-1.5 text-[11.5px] font-medium transition-colors",
        active
          ? "border-cyan-400/40 bg-cyan-400/10 text-cyan-300"
          : "border-white/10 bg-white/[0.03] text-slate-400 hover:border-white/20 hover:text-slate-200",
      )}
    >
      {children}
    </button>
  );
}
