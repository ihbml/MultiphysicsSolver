import { useRef, useState, type KeyboardEvent } from "react";
import { STACKS } from "../data/stacks";
import { BANDS, type ScaleId } from "../lib/scale";
import { cn } from "../utils/cn";

const DOT: Record<ScaleId, string> = {
  micro: "bg-micro",
  meso: "bg-meso",
  macro: "bg-macro",
};

const LABEL: Record<ScaleId, string> = {
  micro: "text-micro-ink",
  meso: "text-meso-ink",
  macro: "text-macro-ink",
};

const bandName = (id: ScaleId) => BANDS.find((b) => b.id === id)?.label ?? id;

export function Stacks() {
  const [active, setActive] = useState(0);
  const tabs = useRef<(HTMLButtonElement | null)[]>([]);
  const stack = STACKS[active];

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const step = event.key === "ArrowRight" ? 1 : event.key === "ArrowLeft" ? -1 : 0;
    if (step === 0) return;
    event.preventDefault();
    const next = (active + step + STACKS.length) % STACKS.length;
    setActive(next);
    tabs.current[next]?.focus();
  };

  return (
    <section id="stacks" className="border-t border-rule bg-card">
      <div className="mx-auto max-w-6xl px-5 py-16 sm:px-8 sm:py-24">
        <p className="font-mono text-xs tracking-wide text-muted uppercase">Recommended stacks</p>
        <h2 className="mt-3 max-w-3xl font-display text-4xl leading-tight font-medium tracking-tight text-balance sm:text-5xl">
          Three combinations that work
        </h2>
        <p className="mt-4 max-w-2xl text-lg leading-relaxed text-pretty text-muted">
          Pick the problem closest to yours. Each scale resolves what the next one can only model,
          then passes averaged quantities upward.
        </p>

        <div
          role="tablist"
          aria-label="Problem type"
          onKeyDown={onKeyDown}
          className="mt-10 flex gap-6 border-b border-rule sm:gap-8"
        >
          {STACKS.map((item, i) => (
            <button
              key={item.id}
              ref={(el) => {
                tabs.current[i] = el;
              }}
              type="button"
              role="tab"
              id={`tab-${item.id}`}
              aria-selected={i === active}
              aria-controls={`panel-${item.id}`}
              tabIndex={i === active ? 0 : -1}
              onClick={() => setActive(i)}
              className={cn(
                "-mb-px min-h-11 border-b-2 pb-2 text-[15px] transition-colors sm:text-base",
                i === active
                  ? "border-ink font-medium text-ink"
                  : "border-transparent text-muted hover:text-ink",
              )}
            >
              {item.tab}
            </button>
          ))}
        </div>

        <div
          role="tabpanel"
          id={`panel-${stack.id}`}
          aria-labelledby={`tab-${stack.id}`}
          className="mt-10 grid gap-x-16 gap-y-12 lg:grid-cols-[minmax(0,1.5fr)_minmax(0,1fr)]"
        >
          <div>
            <h3 className="font-display text-3xl leading-tight font-medium tracking-tight">
              {stack.title}
            </h3>
            <ol className="mt-8">
              {stack.layers.map((layer, i) => {
                const last = i === stack.layers.length - 1;
                return (
                  <li key={layer.solver} className="grid grid-cols-[auto_minmax(0,1fr)] gap-x-5">
                    <div className="flex flex-col items-center pt-1" aria-hidden="true">
                      <span className={cn("size-3.5 rounded-full", DOT[layer.scale])} />
                      {!last && <span className="mt-1 w-px flex-1 bg-rule" />}
                    </div>
                    <div className={cn(!last && "pb-9")}>
                      <p
                        className={cn(
                          "font-mono text-xs font-medium tracking-wide uppercase",
                          LABEL[layer.scale],
                        )}
                      >
                        {bandName(layer.scale)}
                      </p>
                      <p className="mt-1 text-xl leading-snug font-semibold">{layer.solver}</p>
                      <p className="mt-1.5 max-w-xl leading-relaxed text-pretty text-muted">
                        {layer.resolves}
                      </p>
                      {layer.handsOn && (
                        <p className="mt-4 flex flex-wrap items-baseline gap-x-2 gap-y-1 text-sm">
                          <span className="font-mono text-xs text-muted">
                            <span aria-hidden="true">↓ </span>passes on
                          </span>
                          <span className="font-medium">{layer.handsOn}</span>
                        </p>
                      )}
                    </div>
                  </li>
                );
              })}
            </ol>
          </div>

          <aside className="lg:border-l lg:border-rule lg:pl-10">
            <dl className="space-y-8">
              <div>
                <dt className="font-mono text-xs tracking-wide text-muted uppercase">
                  Chemistry kernel
                </dt>
                <dd className="mt-1.5">
                  <p className="text-lg font-semibold">{stack.kernel.name}</p>
                  <p className="mt-1 leading-relaxed text-pretty text-muted">{stack.kernel.note}</p>
                </dd>
              </div>
              <div>
                <dt className="font-mono text-xs tracking-wide text-muted uppercase">Coupling</dt>
                <dd className="mt-1.5">
                  <p className="text-lg font-semibold">{stack.coupling.name}</p>
                  <p className="mt-1 leading-relaxed text-pretty text-muted">
                    {stack.coupling.note}
                  </p>
                </dd>
              </div>
              <div className="border-t border-rule pt-6">
                <dt className="font-mono text-xs tracking-wide text-muted uppercase">
                  Why this stack
                </dt>
                <dd className="mt-1.5 font-display text-xl leading-snug text-pretty">
                  {stack.why}
                </dd>
              </div>
            </dl>
          </aside>
        </div>
      </div>
    </section>
  );
}
