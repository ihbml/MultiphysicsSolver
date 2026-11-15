import { useEffect, useRef, useState } from "react";
import { cn } from "@/utils/cn";

const T_LOOP = 6000;

type Block = {
  from: number;
  to: number;
  label: string;
  tone: "main" | "corr" | "muted" | "net";
};

type Lane = { name: string; hint: string; blocks: Block[] };

const LANES: Lane[] = [
  {
    name: "stream_inner",
    hint: "Segment 0 → 1 → CSR",
    blocks: [
      { from: 0, to: 2600, label: "assemble_ortho_fast_kernel · Segment 0 (≈90 % der Faces)", tone: "main" },
      { from: 2600, to: 4600, label: "assemble_skew_correction_kernel · Segment 1 (≈5 %)", tone: "corr" },
      { from: 4600, to: 6000, label: "CSR → ginkgo", tone: "muted" },
    ],
  },
  {
    name: "stream_grad",
    hint: "Gradienten nur wo nötig",
    blocks: [
      { from: 150, to: 2500, label: "∇ϕ nur für Seg-1-Nachbarzellen", tone: "main" },
      { from: 2500, to: 4600, label: "∇ϕ Halo-Zellen", tone: "main" },
    ],
  },
  {
    name: "stream_halo",
    hint: "MPI + Halo-Segmente",
    blocks: [
      { from: 0, to: 1800, label: "MPI_Isend/Irecv · Halo ϕ", tone: "net" },
      { from: 1800, to: 4600, label: "Warten auf Netzwerk — Rechenkerne bleiben frei", tone: "muted" },
      { from: 4600, to: 6000, label: "Segmente 2+3 → CSR", tone: "corr" },
    ],
  },
];

const TAKTS = [
  { from: 0, to: 2600, label: "Takt 0 · Fast-Path + non-blockende Halo-Kommunikation" },
  { from: 2600, to: 4600, label: "Takt 1 · Korrektur-Faces + Gradienten" },
  { from: 4600, to: 6000, label: "Takt 2 · Halo-Segmente, Matrix an den Löser" },
];

const TONE_CLASS: Record<Block["tone"], string> = {
  main: "border-cyan-400/40 bg-cyan-400/15 text-cyan-100",
  corr: "border-amber-400/40 bg-amber-400/15 text-amber-100",
  muted: "border-white/10 bg-white/[0.04] text-slate-500",
  net: "border-emerald-400/40 bg-emerald-400/15 text-emerald-100",
};

export function KernelTimeline() {
  const [t, setT] = useState(0);
  const [playing, setPlaying] = useState(true);
  const playingRef = useRef(playing);
  playingRef.current = playing;

  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const loop = (now: number) => {
      const dt = now - last;
      last = now;
      if (playingRef.current) setT((prev) => (prev + dt) % T_LOOP);
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  const takt = TAKTS.find((k) => t >= k.from && t < k.to) ?? TAKTS[TAKTS.length - 1];

  return (
    <div className="rounded-2xl border border-white/10 bg-[#080d18] p-5">
      <div className="mb-5 flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
            Asynchroner Taktplan pro Zeitschritt
          </div>
          <div className="mt-1 text-[13px] font-medium text-white">{takt.label}</div>
        </div>
        <div className="flex items-center gap-3">
          <span className="font-mono text-[11px] text-slate-500 tabular-nums">
            t = {(t / 1000).toFixed(2)} s
          </span>
          <button
            type="button"
            onClick={() => setPlaying((p) => !p)}
            className="flex items-center gap-1.5 rounded-lg border border-white/10 bg-white/5 px-3 py-1.5 text-[11.5px] text-slate-300 transition-colors hover:border-white/25 hover:text-white"
          >
            {playing ? (
              <>
                <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="currentColor">
                  <rect x="6" y="5" width="4" height="14" rx="1" />
                  <rect x="14" y="5" width="4" height="14" rx="1" />
                </svg>
                Pause
              </>
            ) : (
              <>
                <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="currentColor">
                  <path d="M8 5v14l11-7z" />
                </svg>
                Abspielen
              </>
            )}
          </button>
        </div>
      </div>

      {/* Spuren */}
      <div className="space-y-3">
        {LANES.map((lane) => (
          <div key={lane.name} className="flex items-center gap-3">
            <div className="w-[112px] shrink-0">
              <div className="font-mono text-[11.5px] text-slate-300">{lane.name}</div>
              <div className="text-[10px] text-slate-600">{lane.hint}</div>
            </div>
            <div className="relative h-11 flex-1 overflow-hidden rounded-xl border border-white/5 bg-white/[0.02]">
              {/* Takt-Raster */}
              {TAKTS.slice(1).map((k) => (
                <span
                  key={k.from}
                  className="absolute top-0 bottom-0 w-px bg-white/10"
                  style={{ left: `${(k.from / T_LOOP) * 100}%` }}
                />
              ))}
              {lane.blocks.map((b) => {
                const active = t >= b.from && t < b.to;
                return (
                  <div
                    key={`${b.from}-${b.label}`}
                    className={cn(
                      "absolute top-1.5 bottom-1.5 flex items-center overflow-hidden rounded-lg border px-2.5 transition-opacity",
                      TONE_CLASS[b.tone],
                      active ? "opacity-100" : "opacity-45",
                    )}
                    style={{
                      left: `${(b.from / T_LOOP) * 100}%`,
                      width: `${((b.to - b.from) / T_LOOP) * 100}%`,
                    }}
                  >
                    <span className="truncate text-[10.5px] font-medium whitespace-nowrap">
                      {b.label}
                    </span>
                  </div>
                );
              })}
              {/* Abspielkopf */}
              <span
                className="pointer-events-none absolute top-0 bottom-0 w-0.5 bg-white shadow-[0_0_12px_rgba(255,255,255,0.8)]"
                style={{ left: `${(t / T_LOOP) * 100}%` }}
              />
            </div>
          </div>
        ))}
      </div>

      {/* Zeitachse */}
      <div className="mt-3 flex items-center gap-3">
        <div className="w-[112px] shrink-0" />
        <div className="relative h-5 flex-1 font-mono text-[10px] text-slate-600">
          {TAKTS.map((k) => (
            <span
              key={k.from}
              className="absolute top-0"
              style={{ left: `${(k.from / T_LOOP) * 100}%` }}
            >
              {(k.from / 1000).toFixed(1)} s
            </span>
          ))}
        </div>
      </div>

      {/* Scrubber */}
      <div className="mt-4 flex items-center gap-3 border-t border-white/5 pt-4">
        <span className="font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
          Scrub
        </span>
        <input
          type="range"
          min={0}
          max={T_LOOP}
          step={10}
          value={Math.round(t)}
          onChange={(e) => {
            setPlaying(false);
            setT(Number(e.target.value));
          }}
          className="flex-1"
        />
      </div>

      <p className="mt-4 text-[12px] leading-relaxed text-slate-500">
        Der MPI-Halo-Austausch (grün) läuft non-blockend; die Wartezeit des Netzwerks
        überlappt sich zu etwa 100 % mit den 90 % orthogonalen Faces im inneren Segment.
        Erst wenn die Halo-Daten da sind, werden die kleinen Rand-Segmente 2 und 3
        nachgeschoben — die Matrix verlässt den VRAM nie.
      </p>
    </div>
  );
}
