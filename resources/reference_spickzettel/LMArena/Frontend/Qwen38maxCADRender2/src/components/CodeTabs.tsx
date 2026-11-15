import { memo, useMemo, useState } from "react";
import { highlightRust } from "@/lib/rust";
import { SHELL_STEPS, SNIPPETS } from "@/lib/snippets";
import { cn } from "@/utils/cn";

export const CodeTabs = memo(function CodeTabs() {
  const [active, setActive] = useState(SNIPPETS[0].id);
  const [copied, setCopied] = useState(false);
  const s = SNIPPETS.find((x) => x.id === active) ?? SNIPPETS[0];
  const html = useMemo(() => highlightRust(s.code), [s]);
  const count = useMemo(() => s.code.split("\n").length, [s]);

  return (
    <div className="border border-ink-700 bg-ink-900 shadow-[0_24px_60px_-30px_rgba(0,0,0,0.9)]">
      <div className="flex flex-wrap items-stretch border-b border-ink-700 bg-ink-850">
        {SNIPPETS.map((sn) => (
          <button
            key={sn.id}
            type="button"
            onClick={() => setActive(sn.id)}
            className={cn(
              "relative px-4 py-2.5 text-left font-mono text-[10.5px] uppercase tracking-[0.14em] transition-colors",
              sn.id === active ? "bg-ink-900 text-amber-signal" : "text-ink-400 hover:text-paper-200",
            )}
          >
            {sn.title}
            {sn.id === active && (
              <span className="absolute inset-x-0 top-0 h-[2px] bg-amber-signal" />
            )}
          </button>
        ))}
        <div className="ml-auto flex items-center gap-2 px-3">
          <span className="hidden font-mono text-[10px] text-ink-500 md:inline">{s.file}</span>
          <button
            type="button"
            onClick={() => {
              navigator.clipboard?.writeText(s.code).catch(() => undefined);
              setCopied(true);
              setTimeout(() => setCopied(false), 1400);
            }}
            className="border border-ink-600 px-2 py-1 font-mono text-[9.5px] uppercase tracking-wider text-ink-400 transition-colors hover:border-cyan-signal/60 hover:text-cyan-signal"
          >
            {copied ? "copied" : "copy"}
          </button>
        </div>
      </div>

      <p className="border-b border-ink-800 bg-ink-900 px-4 py-2.5 text-[12.5px] leading-relaxed text-ink-400">
        {s.blurb}
      </p>

      <div className="thin-scroll max-h-[560px] overflow-auto bg-ink-950">
        <div className="flex min-w-max">
          <div className="sticky left-0 select-none border-r border-ink-800 bg-ink-950 px-3 py-4 text-right font-mono text-[11px] leading-[18.6px] text-ink-700">
            {Array.from({ length: count }, (_, i) => (
              <div key={i}>{i + 1}</div>
            ))}
          </div>
          <pre className="px-4 py-4 font-mono text-[11.5px] leading-[18.6px] text-paper-200">
            <code dangerouslySetInnerHTML={{ __html: html }} />
          </pre>
        </div>
      </div>
    </div>
  );
});

export const QuickStart = memo(function QuickStart() {
  return (
    <ol className="relative">
      <span className="absolute left-[19px] top-2 bottom-2 w-px bg-[repeating-linear-gradient(to_bottom,#bcc8d6_0_4px,transparent_4px_9px)]" />
      {SHELL_STEPS.map((st) => (
        <li key={st.n} className="group relative flex gap-5 pb-7 last:pb-0">
          <span className="relative z-10 flex h-10 w-10 shrink-0 items-center justify-center border border-paper-300 bg-paper-50 font-display text-[13px] font-bold text-paper-900 transition-colors group-hover:border-[#c97a09] group-hover:bg-[#f5a524]">
            {st.n}
          </span>
          <div className="min-w-0 flex-1 pt-1">
            <h4 className="font-display text-[19px] font-bold leading-tight text-paper-900">
              {st.title}
            </h4>
            <p className="mt-1 max-w-[58ch] text-[13.5px] leading-relaxed text-paper-700">
              {st.body}
            </p>
            {st.code && (
              <pre className="thin-scroll mt-2.5 overflow-x-auto border-l-2 border-[#c97a09]/40 bg-[#101720] px-3 py-2 font-mono text-[11px] leading-relaxed text-[#9fd8e6]">
                {st.code}
              </pre>
            )}
          </div>
        </li>
      ))}
    </ol>
  );
});
