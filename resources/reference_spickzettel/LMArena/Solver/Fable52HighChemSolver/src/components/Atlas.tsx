import { useMemo, useState, type ReactNode } from "react";
import { SOLVERS, type Gpu, type Solver } from "../data/solvers";
import {
  BANDS,
  TICKS,
  bandsCovered,
  formatLength,
  gradientWindow,
  pos,
  type ScaleId,
} from "../lib/scale";
import { cn } from "../utils/cn";

type ScaleFilter = "all" | ScaleId;

const ROW_GRID =
  "grid grid-cols-1 gap-x-6 gap-y-2 px-3 md:grid-cols-[13.5rem_minmax(0,1fr)_10rem] md:items-center";

const BAND_TEXT: Record<ScaleId, string> = {
  micro: "text-micro-ink",
  meso: "text-meso-ink",
  macro: "text-macro-ink",
};

const BAND_BORDER: Record<ScaleId, string> = {
  micro: "border-micro",
  meso: "border-meso",
  macro: "border-macro",
};

const GPU_LABEL: Record<Gpu, string> = {
  yes: "GPU",
  partial: "GPU partial",
  no: "CPU",
  na: "n/a",
};

function Chevron({ open }: { open: boolean }) {
  return (
    <svg
      viewBox="0 0 16 16"
      aria-hidden="true"
      className={cn("size-4 shrink-0 text-muted transition-transform duration-200", open && "rotate-180")}
    >
      <path d="M3.5 6l4.5 4.5L12.5 6" fill="none" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  );
}

function Axis() {
  return (
    <div className={cn(ROW_GRID, "pb-2")} aria-hidden="true">
      <div className="hidden md:block" />
      <div>
        {/* Scale bands */}
        <div className="relative h-7">
          {BANDS.map((band) => (
            <div
              key={band.id}
              className={cn(
                "absolute inset-y-0 flex items-end border-b-2 pb-1 pl-0.5 font-mono text-[11px] tracking-wide",
                BAND_BORDER[band.id],
                BAND_TEXT[band.id],
              )}
              style={{
                left: `calc(${pos(band.lo) * 100}% + 1px)`,
                width: `calc(${(pos(band.hi) - pos(band.lo)) * 100}% - 2px)`,
              }}
            >
              <span className="truncate">
                <span className="font-medium uppercase">{band.label}</span>
                <span className="hidden text-muted lg:inline"> · {band.hint}</span>
              </span>
            </div>
          ))}
        </div>
        {/* Tick labels */}
        <div className="relative mt-1.5 h-4 font-mono text-[11px] text-muted">
          {TICKS.map((tick, i) => (
            <span
              key={tick.exp}
              className={cn(
                "absolute top-0 whitespace-nowrap",
                i > 0 && i < TICKS.length - 1 && "-translate-x-1/2",
                i === TICKS.length - 1 && "-translate-x-full",
                // The track is narrowest on phones and again at md, where the side columns appear.
                tick.exp === -9 && "hidden sm:inline md:hidden lg:inline",
              )}
              style={{ left: `${pos(tick.exp) * 100}%` }}
            >
              {tick.label}
            </span>
          ))}
        </div>
      </div>
      <div className="hidden md:block" />
    </div>
  );
}

function Track({ solver }: { solver: Solver }) {
  return (
    <span className="decade-grid relative block h-7 border-r border-rule">
      {solver.range ? (
        <span
          className="absolute inset-y-1.5 rounded-full"
          style={gradientWindow(solver.range[0], solver.range[1])}
        />
      ) : (
        <span className="absolute inset-x-0 top-1/2 flex -translate-y-1/2 items-center gap-2">
          <span className="h-px flex-1 border-t border-dashed border-muted" />
          <span className="font-mono text-[11px] whitespace-nowrap text-muted">
            {solver.role === "coupler" ? "connects the solvers above" : "called from the solvers above"}
          </span>
          <span className="h-px flex-1 border-t border-dashed border-muted" />
        </span>
      )}
    </span>
  );
}

function Detail({ solver }: { solver: Solver }) {
  const facts = [
    { term: "Maintained by", value: solver.maintainer },
    { term: "Language", value: solver.language },
    { term: "Licence", value: solver.license },
    { term: "Parallelism", value: solver.parallel },
    { term: "GPU", value: solver.gpuNote },
    ...(solver.range
      ? [
          {
            term: "Typical scales",
            value: `${formatLength(solver.range[0])} to ${formatLength(solver.range[1])}`,
          },
        ]
      : []),
  ];

  return (
    <div className="bg-card px-3 pt-2 pb-6 md:px-6">
      <div className="grid gap-x-10 gap-y-5 md:grid-cols-3">
        <div>
          <h4 className="font-mono text-xs tracking-wide text-muted uppercase">Chemistry</h4>
          <p className="mt-1.5 leading-relaxed text-pretty">{solver.chemistry}</p>
        </div>
        <div>
          <h4 className="font-mono text-xs tracking-wide text-muted uppercase">Reach for it when</h4>
          <p className="mt-1.5 leading-relaxed text-pretty">{solver.bestFor}</p>
        </div>
        <div>
          <h4 className="font-mono text-xs tracking-wide text-muted uppercase">Watch out for</h4>
          <p className="mt-1.5 leading-relaxed text-pretty">{solver.watchOut}</p>
        </div>
      </div>
      <dl className="mt-6 grid gap-x-10 gap-y-3 border-t border-rule pt-4 text-sm sm:grid-cols-2 md:grid-cols-3">
        {facts.map((fact) => (
          <div key={fact.term}>
            <dt className="text-muted">{fact.term}</dt>
            <dd className="mt-0.5">{fact.value}</dd>
          </div>
        ))}
      </dl>
      <a
        href={solver.url}
        target="_blank"
        rel="noopener noreferrer"
        className="mt-5 inline-flex items-center gap-1.5 rounded-sm text-sm font-medium underline decoration-rule underline-offset-4 transition-colors hover:decoration-ink"
      >
        {solver.name} project site
        <span aria-hidden="true">↗</span>
        <span className="sr-only">(opens in a new tab)</span>
      </a>
    </div>
  );
}

function Row({ solver, open, onToggle }: { solver: Solver; open: boolean; onToggle: () => void }) {
  const panelId = `detail-${solver.id}`;

  return (
    <li className="border-b border-rule">
      <h3>
        <button
          type="button"
          aria-expanded={open}
          aria-controls={panelId}
          onClick={onToggle}
          className={cn(
            ROW_GRID,
            "group w-full py-3.5 text-left transition-colors hover:bg-card",
            open && "bg-card",
          )}
        >
          <span className="block min-w-0">
            <span className="flex flex-wrap items-center gap-x-2 gap-y-1">
              <span className="text-[17px] leading-tight font-semibold">{solver.name}</span>
              {solver.topPick && (
                <span className="rounded-full bg-ink px-2 py-0.5 font-mono text-[10px] tracking-wide text-paper uppercase">
                  Top pick
                </span>
              )}
            </span>
            <span className="mt-0.5 block text-sm text-muted">{solver.method}</span>
          </span>

          <Track solver={solver} />

          <span className="flex items-center justify-between gap-3">
            <span className="flex flex-wrap items-center gap-x-2 gap-y-1 font-mono text-xs text-muted md:flex-col md:items-start md:gap-y-0.5">
              <span>{solver.license}</span>
              <span
                className={cn(
                  solver.gpu === "yes" && "font-medium text-ink",
                  solver.gpu === "na" && "hidden",
                )}
              >
                <span className="md:hidden" aria-hidden="true">
                  ·{" "}
                </span>
                {GPU_LABEL[solver.gpu]}
              </span>
            </span>
            <Chevron open={open} />
          </span>
        </button>
      </h3>
      {open && (
        <div id={panelId}>
          <Detail solver={solver} />
        </div>
      )}
    </li>
  );
}

function FilterButton({
  pressed,
  onClick,
  children,
}: {
  pressed: boolean;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <button
      type="button"
      aria-pressed={pressed}
      onClick={onClick}
      className={cn(
        "min-h-10 rounded-full border px-4 text-sm transition-colors",
        pressed
          ? "border-ink bg-ink text-paper"
          : "border-rule bg-card text-ink hover:border-ink",
      )}
    >
      {children}
    </button>
  );
}

export function Atlas() {
  const [scale, setScale] = useState<ScaleFilter>("all");
  const [gpuOnly, setGpuOnly] = useState(false);
  const [linkableOnly, setLinkableOnly] = useState(false);
  const [openId, setOpenId] = useState<string | null>("moose");

  const visible = useMemo(
    () =>
      SOLVERS.filter((s) => {
        if (gpuOnly && s.gpu !== "yes") return false;
        if (linkableOnly && !s.linkable) return false;
        // Libraries are scale-agnostic, so a scale filter never hides them.
        if (scale !== "all" && s.range && !bandsCovered(s.range).includes(scale)) return false;
        return true;
      }),
    [scale, gpuOnly, linkableOnly],
  );

  const solvers = visible.filter((s) => s.role === "solver");
  const libraries = visible.filter((s) => s.role !== "solver");
  const filtered = scale !== "all" || gpuOnly || linkableOnly;

  const reset = () => {
    setScale("all");
    setGpuOnly(false);
    setLinkableOnly(false);
  };

  const toggle = (id: string) => setOpenId((current) => (current === id ? null : id));

  return (
    <section id="atlas" className="border-t border-rule">
      <div className="mx-auto max-w-6xl px-5 py-16 sm:px-8 sm:py-24">
        <p className="font-mono text-xs tracking-wide text-muted uppercase">Scale atlas</p>
        <h2 className="mt-3 max-w-3xl font-display text-4xl leading-tight font-medium tracking-tight text-balance sm:text-5xl">
          What each code can actually resolve
        </h2>
        <p className="mt-4 max-w-2xl text-lg leading-relaxed text-pretty text-muted">
          Each bar spans the length scales a solver is typically used at. Select a row for its
          chemistry capabilities and the caveats that matter.
        </p>

        {/* Filters */}
        <div className="mt-10 flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div className="flex flex-wrap items-center gap-2" role="group" aria-label="Filter by scale">
            <FilterButton pressed={scale === "all"} onClick={() => setScale("all")}>
              All scales
            </FilterButton>
            {BANDS.map((band) => (
              <FilterButton
                key={band.id}
                pressed={scale === band.id}
                onClick={() => setScale(band.id)}
              >
                {band.label}
              </FilterButton>
            ))}
          </div>
          <div className="flex flex-wrap items-center gap-2" role="group" aria-label="Other filters">
            <FilterButton pressed={gpuOnly} onClick={() => setGpuOnly((v) => !v)}>
              GPU-native
            </FilterButton>
            <FilterButton pressed={linkableOnly} onClick={() => setLinkableOnly((v) => !v)}>
              BSD or LGPL
            </FilterButton>
          </div>
        </div>

        <p className="mt-4 text-sm text-muted" aria-live="polite">
          Showing {visible.length} of {SOLVERS.length}
          {filtered && (
            <>
              {" · "}
              <button
                type="button"
                onClick={reset}
                className="rounded-sm text-ink underline decoration-rule underline-offset-4 hover:decoration-ink"
              >
                Clear filters
              </button>
            </>
          )}
        </p>

        <div className="mt-8">
          <Axis />

          {visible.length === 0 && (
            <div className="border-y border-rule py-12 text-center">
              <p className="font-display text-2xl">Nothing matches all of those at once.</p>
              <p className="mt-2 text-muted">
                Try dropping one filter.{" "}
                <button
                  type="button"
                  onClick={reset}
                  className="rounded-sm text-ink underline decoration-rule underline-offset-4 hover:decoration-ink"
                >
                  Clear filters
                </button>
              </p>
            </div>
          )}

          {solvers.length > 0 && (
            <ul className="border-t border-ink">
              {solvers.map((solver) => (
                <Row
                  key={solver.id}
                  solver={solver}
                  open={openId === solver.id}
                  onToggle={() => toggle(solver.id)}
                />
              ))}
            </ul>
          )}

          {libraries.length > 0 && (
            <>
              <p className="mt-10 mb-3 px-3 font-mono text-xs tracking-wide text-muted uppercase">
                Chemistry kernels and couplers
              </p>
              <ul className="border-t border-ink">
                {libraries.map((solver) => (
                  <Row
                    key={solver.id}
                    solver={solver}
                    open={openId === solver.id}
                    onToggle={() => toggle(solver.id)}
                  />
                ))}
              </ul>
            </>
          )}
        </div>

        <p className="mt-6 max-w-2xl text-sm leading-relaxed text-muted">
          Ranges are indicative, not hard limits. A code is listed under a scale when its usual range
          overlaps that band by two decades or more.
        </p>
      </div>
    </section>
  );
}
