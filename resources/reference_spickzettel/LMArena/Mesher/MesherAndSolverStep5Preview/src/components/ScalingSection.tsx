import { useMemo, useState } from "react";
import { cn } from "@/utils/cn";
import { Chip } from "./ui";

/* ---------------- Hilbert-Kurve (Ordnung 4 = 256 Punkte) ---------------- */
function hilbertD2XY(order: number, d: number): [number, number] {
  let x = 0;
  let y = 0;
  let t = d;
  for (let s = 1; s < 1 << order; s *= 2) {
    const rx = 1 & (t >> 1);
    const ry = 1 & (t ^ rx);
    if (ry === 0) {
      if (rx === 1) {
        x = s - 1 - x;
        y = s - 1 - y;
      }
      const tmp = x;
      x = y;
      y = tmp;
    }
    x += s * rx;
    y += s * ry;
    t >>= 2;
  }
  return [x, y];
}

const HILBERT_ORDER = 4;
const HILBERT_POINTS = 1 << (2 * HILBERT_ORDER); // 256
const HILBERT_PATH = Array.from({ length: HILBERT_POINTS }, (_, d) =>
  hilbertD2XY(HILBERT_ORDER, d),
);

const BOX = 244;
const PAD = 12;
const toX = (px: number) => PAD + ((px + 0.5) * BOX) / (1 << HILBERT_ORDER);
const toY = (py: number) => PAD + ((py + 0.5) * BOX) / (1 << HILBERT_ORDER);

/* ---------------- Skalierungsmodell ---------------- */
const TOTAL_CELLS = 64e6;
const HALO_LAYERS = 2;
const FIELDS = 9; // u, p, T, k, eps, Y1..Y3, omega
const GAMMA_SYNC = 0.1;
const CG_ITERATIONS = 40;
const S_STEP = 4;

const haloFraction = (k: number) =>
  Math.min(0.6, (6 * HALO_LAYERS) / Math.cbrt(TOTAL_CELLS / k));

function efficiency(k: number, sStep: boolean, compress: boolean) {
  const f = haloFraction(k) * (compress ? 0.5 : 1);
  const sync = (GAMMA_SYNC * Math.log2(k)) / (sStep ? S_STEP : 1);
  return 1 / (1 + 6 * f + sync);
}

const speedup = (k: number, sStep: boolean, compress: boolean) =>
  k * efficiency(k, sStep, compress);

const nf = new Intl.NumberFormat("de-DE");
const fmtM = (v: number) => (v >= 1e6 ? `${(v / 1e6).toFixed(1)} M` : `${nf.format(Math.round(v / 1e3))} k`);

const NODE_CHIPS = [1, 4, 8, 16, 32, 64, 128];

export function ScalingSection() {
  const [k, setK] = useState(8);

  const model = useMemo(() => {
    const perNode = TOTAL_CELLS / k;
    const f = haloFraction(k);
    const haloPerNode = perNode * f;
    const effClassic = efficiency(k, false, false);
    const effS = efficiency(k, true, false);
    const effSC = efficiency(k, true, true);
    const syncsClassic = 2 * CG_ITERATIONS;
    const syncsS = 2 * Math.ceil(CG_ITERATIONS / S_STEP);
    return {
      perNode,
      f,
      haloPerNode,
      haloBytes: haloPerNode * k * FIELDS * 8,
      effClassic,
      effS,
      effSC,
      syncsClassic,
      syncsS,
      spClassic: speedup(k, false, false),
      spSC: speedup(k, true, true),
    };
  }, [k]);

  /* Hilbert-Kurve in K gleich schwere Stücke schneiden */
  const chunks = useMemo(() => {
    const out: { d: string; color: string; start: [number, number] }[] = [];
    for (let i = 0; i < k; i++) {
      const from = Math.floor((i * HILBERT_POINTS) / k);
      const to = Math.floor(((i + 1) * HILBERT_POINTS) / k);
      const pts = HILBERT_PATH.slice(from, Math.max(to, from + 1));
      if (pts.length < 2) continue;
      const d = pts.map(([px, py], idx) => `${idx === 0 ? "M" : "L"}${toX(px)},${toY(py)}`).join(" ");
      out.push({
        d,
        color: `hsl(${(190 + (i / k) * 115).toFixed(0)} 80% 62%)`,
        start: [toX(pts[0][0]), toY(pts[0][1])],
      });
    }
    return out;
  }, [k]);

  /* Effizienzkurven */
  const curves = useMemo(() => {
    const build = (sStep: boolean, compress: boolean) => {
      const pts: string[] = [];
      for (let kk = 1; kk <= 128; kk++) {
        const x = (Math.log2(kk) / 7) * 100;
        const y = 100 - efficiency(kk, sStep, compress) * 100;
        pts.push(`${x.toFixed(2)},${y.toFixed(2)}`);
      }
      return pts.join(" ");
    };
    return {
      classic: build(false, false),
      sstep: build(true, false),
      sstepZfp: build(true, true),
    };
  }, []);

  return (
    <div className="grid gap-5 xl:grid-cols-[1fr_1.25fr]">
      {/* ---------------- Steuerung + Hilbert ---------------- */}
      <div className="space-y-4">
        <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
          <div className="mb-4 flex items-center justify-between">
            <span className="font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Cloud-Knoten
            </span>
            <span className="font-mono text-2xl font-semibold text-cyan-300 tabular-nums">{k}</span>
          </div>
          <input
            type="range"
            min={1}
            max={128}
            step={1}
            value={k}
            onChange={(e) => setK(Number(e.target.value))}
            className="w-full"
          />
          <div className="mt-3 flex flex-wrap gap-1.5">
            {NODE_CHIPS.map((n) => (
              <button
                key={n}
                type="button"
                onClick={() => setK(n)}
                className={cn(
                  "rounded-lg border px-2.5 py-1 font-mono text-[11px] transition-colors",
                  k === n
                    ? "border-cyan-400/40 bg-cyan-400/10 text-cyan-300"
                    : "border-white/10 bg-white/[0.02] text-slate-500 hover:text-slate-300",
                )}
              >
                {n}
              </button>
            ))}
          </div>

          <div className="mt-5 grid grid-cols-2 gap-3 border-t border-white/5 pt-4">
            <Metric label="Zellen gesamt" value={`${fmtM(TOTAL_CELLS)}`} />
            <Metric label="Zellen / Node" value={fmtM(model.perNode)} />
            <Metric
              label="Halo-Anteil"
              value={`${(model.f * 100).toFixed(1)} %`}
              hint={`${HALO_LAYERS} Ghost-Lagen`}
            />
            <Metric
              label="Halo-Volumen / Austausch"
              value={`${(model.haloBytes / 1e9).toFixed(1)} GB`}
              hint={`${FIELDS} Felder × 8 B`}
            />
          </div>
        </div>

        <div className="rounded-2xl border border-white/10 bg-[#080d18] p-5">
          <div className="mb-1 text-[12.5px] font-semibold text-white">
            Hilbert-Partitionierung
          </div>
          <p className="mb-4 text-[11.5px] leading-relaxed text-slate-500">
            Die 3D-Hilbert-Kurve schneidet das Gitter wie eine Perlenschnur in{" "}
            {k} gleich schwere Stücke. Kontiguität entlang der Kurve minimiert die
            Oberfläche — und damit das Halo-Volumen.
          </p>
          <svg viewBox={`0 0 ${BOX + 2 * PAD} ${BOX + 2 * PAD}`} className="mx-auto h-auto w-full max-w-[300px]">
            <rect
              x={PAD / 2}
              y={PAD / 2}
              width={BOX + PAD}
              height={BOX + PAD}
              rx="10"
              fill="rgba(255,255,255,0.02)"
              stroke="rgba(255,255,255,0.07)"
            />
            {chunks.map((c, i) => (
              <path
                key={i}
                d={c.d}
                fill="none"
                stroke={c.color}
                strokeWidth={k <= 32 ? 7 : 4}
                strokeLinecap="round"
                strokeLinejoin="round"
                opacity="0.9"
              />
            ))}
            {chunks.slice(0, 64).map((c, i) => (
              <circle key={i} cx={c.start[0]} cy={c.start[1]} r="2.4" fill="#e2e8f0" opacity="0.8" />
            ))}
          </svg>
          <div className="mt-3 flex flex-wrap gap-1.5">
            <Chip tone="cyan">K = {k}</Chip>
            <Chip>{Math.floor(HILBERT_POINTS / k)} Zellblöcke / Node</Chip>
            <Chip tone="violet">sortiert & kontiguierlich</Chip>
          </div>
        </div>
      </div>

      {/* ---------------- Modell + Diagramme ---------------- */}
      <div className="space-y-4">
        <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
          <div className="mb-4 flex flex-wrap items-end justify-between gap-3">
            <div>
              <div className="font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
                Strong-Scaling-Modell
              </div>
              <div className="mt-1 text-[12.5px] text-slate-400">
                η(K) = 1 / (1 + 6·f_halo(K) + γ·log₂K / s)
              </div>
            </div>
            <div className="text-right">
              <div className="font-mono text-3xl font-semibold text-cyan-300 tabular-nums">
                {(model.effSC * 100).toFixed(0)} %
              </div>
              <div className="text-[11px] text-slate-500">Effizienz bei K = {k}</div>
            </div>
          </div>

          <svg viewBox="0 0 100 100" preserveAspectRatio="none" className="h-52 w-full">
            {/* Gitter */}
            {[0, 0.25, 0.5, 0.75, 1].map((g) => (
              <line
                key={g}
                x1="0"
                x2="100"
                y1={g * 100}
                y2={g * 100}
                stroke="rgba(148,163,184,0.12)"
                strokeWidth="0.4"
              />
            ))}
            {[1, 4, 16, 64, 128].map((kk) => (
              <line
                key={kk}
                x1={(Math.log2(kk) / 7) * 100}
                x2={(Math.log2(kk) / 7) * 100}
                y1="0"
                y2="100"
                stroke="rgba(148,163,184,0.10)"
                strokeWidth="0.4"
              />
            ))}

            <polyline points={curves.classic} fill="none" stroke="#fb7185" strokeWidth="1.4" vectorEffect="non-scaling-stroke" />
            <polyline points={curves.sstep} fill="none" stroke="#a78bfa" strokeWidth="1.4" vectorEffect="non-scaling-stroke" />
            <polyline points={curves.sstepZfp} fill="none" stroke="#22d3ee" strokeWidth="1.8" vectorEffect="non-scaling-stroke" />

            {/* Aktueller Knoten */}
            <line
              x1={(Math.log2(k) / 7) * 100}
              x2={(Math.log2(k) / 7) * 100}
              y1="0"
              y2="100"
              stroke="rgba(255,255,255,0.5)"
              strokeWidth="0.6"
              strokeDasharray="2 2"
              vectorEffect="non-scaling-stroke"
            />
            {[
              { eff: model.effClassic, c: "#fb7185" },
              { eff: model.effS, c: "#a78bfa" },
              { eff: model.effSC, c: "#22d3ee" },
            ].map((p) => (
              <circle
                key={p.c}
                cx={(Math.log2(k) / 7) * 100}
                cy={(1 - p.eff) * 100}
                r="1.8"
                fill={p.c}
                vectorEffect="non-scaling-stroke"
              />
            ))}
          </svg>

          <div className="mt-3 flex flex-wrap gap-x-4 gap-y-1.5 text-[11px]">
            <Legend color="#fb7185" label={`klassisch · ${(model.effClassic * 100).toFixed(0)} %`} />
            <Legend color="#a78bfa" label={`s-Step (s=${S_STEP}) · ${(model.effS * 100).toFixed(0)} %`} />
            <Legend color="#22d3ee" label={`s-Step + ZFP-Halo · ${(model.effSC * 100).toFixed(0)} %`} />
          </div>

          <div className="mt-4 grid grid-cols-2 gap-3 border-t border-white/5 pt-4 sm:grid-cols-4">
            <Metric label="Speedup" value={`${model.spSC.toFixed(1)}×`} />
            <Metric label="vs. ideal" value={`${(model.spSC / k * 100).toFixed(0)} %`} />
            <Metric label="Syncs klassisch" value={`${model.syncsClassic}`} hint="2 · CG-Iterationen" />
            <Metric label="Syncs s-Step" value={`${model.syncsS}`} hint={`s = ${S_STEP}`} tone="emerald" />
          </div>
        </div>

        {/* Inner/Halo-Überlappung */}
        <div className="rounded-2xl border border-white/10 bg-[#080d18] p-5">
          <div className="mb-4 text-[12.5px] font-semibold text-white">
            Inner/Halo-Splitting: Kommunikation und Rechnung überlappen
          </div>
          <svg viewBox="0 0 420 130" className="h-auto w-full">
            <defs>
              <pattern id="haloHatch" width="7" height="7" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">
                <line x1="0" y1="0" x2="0" y2="7" stroke="rgba(251,191,36,0.55)" strokeWidth="2.4" />
              </pattern>
            </defs>

            {/* Node A */}
            <rect x="20" y="24" width="170" height="82" rx="8" fill="rgba(34,211,238,0.10)" stroke="rgba(34,211,238,0.35)" />
            <rect x="150" y="24" width="40" height="82" fill="url(#haloHatch)" />
            <text x="30" y="44" fill="#e2e8f0" fontSize="11" fontFamily="monospace">Node A</text>
            <text x="30" y="94" fill="rgba(226,232,240,0.55)" fontSize="9" fontFamily="monospace">
              innere Zellen · 0 … N_inner
            </text>

            {/* Node B */}
            <rect x="230" y="24" width="170" height="82" rx="8" fill="rgba(167,139,250,0.10)" stroke="rgba(167,139,250,0.35)" />
            <rect x="230" y="24" width="40" height="82" fill="url(#haloHatch)" />
            <text x="240" y="44" fill="#e2e8f0" fontSize="11" fontFamily="monospace">Node B</text>
            <text x="240" y="94" fill="rgba(226,232,240,0.55)" fontSize="9" fontFamily="monospace">
              Halo-Zellen · N_inner … N_local
            </text>

            {/* Austauschpfeil */}
            <path d="M192 65 L228 65" stroke="#34d399" strokeWidth="1.6" strokeDasharray="4 3" markerEnd="url(#arrowMF)" />
            <defs>
              <marker id="arrowMF" markerWidth="7" markerHeight="7" refX="6" refY="3.5" orient="auto">
                <path d="M0 0 L7 3.5 L0 7 z" fill="#34d399" />
              </marker>
            </defs>
            <text x="196" y="20" fill="#34d399" fontSize="9" fontFamily="monospace">
              MPI · non-blockend
            </text>
            <text x="20" y="124" fill="rgba(148,163,184,0.7)" fontSize="9" fontFamily="monospace">
              Sortierung: innere Zellen zuerst → Ghost-Lagen am Ende des Indexbereichs
            </text>
          </svg>
          <div className="mt-3 flex flex-wrap gap-1.5">
            <Chip tone="emerald">Overlap ≈ 100 %</Chip>
            <Chip tone="amber">1–2 Ghost-Lagen</Chip>
            <Chip tone="cyan">Zero-Copy Lastverschiebung</Chip>
          </div>
        </div>
      </div>
    </div>
  );
}

function Metric({
  label,
  value,
  hint,
  tone = "cyan",
}: {
  label: string;
  value: string;
  hint?: string;
  tone?: "cyan" | "emerald";
}) {
  return (
    <div>
      <div className="text-[10.5px] text-slate-500">{label}</div>
      <div
        className={cn(
          "font-mono text-[15px] font-semibold tabular-nums",
          tone === "cyan" ? "text-slate-100" : "text-emerald-300",
        )}
      >
        {value}
      </div>
      {hint ? <div className="text-[10px] text-slate-600">{hint}</div> : null}
    </div>
  );
}

function Legend({ color, label }: { color: string; label: string }) {
  return (
    <span className="flex items-center gap-1.5 text-slate-400">
      <span className="h-0.5 w-4 rounded" style={{ background: color }} />
      {label}
    </span>
  );
}
