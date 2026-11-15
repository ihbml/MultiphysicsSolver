import { useState } from "react";
import { SNIPPETS } from "@/data/content";
import { highlight } from "@/lib/highlight";
import { cn } from "@/utils/cn";

export function CodeSection() {
  const [active, setActive] = useState(SNIPPETS[0].id);
  const [copied, setCopied] = useState(false);
  const snippet = SNIPPETS.find((s) => s.id === active) ?? SNIPPETS[0];

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(snippet.code);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1800);
    } catch {
      setCopied(false);
    }
  };

  return (
    <div className="overflow-hidden rounded-2xl border border-white/10 bg-[#080d18]">
      {/* Tab-Leiste */}
      <div className="flex items-center gap-1 overflow-x-auto border-b border-white/10 bg-white/[0.02] px-2 py-2 scroll-thin">
        {SNIPPETS.map((s) => (
          <button
            key={s.id}
            type="button"
            onClick={() => setActive(s.id)}
            className={cn(
              "shrink-0 rounded-lg px-3 py-1.5 font-mono text-[11.5px] transition-colors",
              s.id === active
                ? "bg-cyan-400/12 text-cyan-300"
                : "text-slate-500 hover:bg-white/5 hover:text-slate-300",
            )}
          >
            {s.file}
          </button>
        ))}
      </div>

      {/* Kopfzeile */}
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-white/10 px-5 py-3">
        <div className="flex items-center gap-3">
          <span className="rounded-md border border-violet-400/30 bg-violet-400/10 px-2 py-0.5 font-mono text-[10px] text-violet-300">
            {snippet.lang}
          </span>
          <span className="font-mono text-[12px] text-slate-400">{snippet.file}</span>
        </div>
        <button
          type="button"
          onClick={copy}
          className={cn(
            "flex items-center gap-1.5 rounded-lg border px-2.5 py-1.5 font-mono text-[11px] transition-colors",
            copied
              ? "border-emerald-400/40 bg-emerald-400/10 text-emerald-300"
              : "border-white/10 bg-white/5 text-slate-400 hover:border-white/25 hover:text-slate-200",
          )}
        >
          {copied ? (
            <>
              <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="none" stroke="currentColor" strokeWidth="2.5">
                <path d="m5 13 4 4L19 7" />
              </svg>
              kopiert
            </>
          ) : (
            <>
              <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="none" stroke="currentColor" strokeWidth="2">
                <rect x="9" y="9" width="11" height="11" rx="2" />
                <path d="M5 15V5a2 2 0 0 1 2-2h8" />
              </svg>
              kopieren
            </>
          )}
        </button>
      </div>

      <p className="border-b border-white/10 px-5 py-3 text-[12.5px] leading-relaxed text-slate-400">
        {snippet.blurb}
      </p>

      {/* Code */}
      <pre className="overflow-x-auto px-5 py-4 font-mono text-[12px] leading-[1.65] text-slate-300 scroll-thin">
        <code>{highlight(snippet.code)}</code>
      </pre>
    </div>
  );
}
