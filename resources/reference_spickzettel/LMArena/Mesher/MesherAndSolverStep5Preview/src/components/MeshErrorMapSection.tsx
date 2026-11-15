import { useMemo, useState } from "react";
import { cn } from "@/utils/cn";
import { Chip, Toggle } from "./ui";

/* ---------------------------------------------------------------- *
 * Deterministischer Pseudo-Zufall, damit die Demo reproduzierbar ist
 * ---------------------------------------------------------------- */
function mulberry32(seed: number) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const W = 760;
const H = 470;
const MARGIN = 34;
const COLS = 15;
const ROWS = 10;

type Pt = { x: number; y: number };
type Face = {
  id: string;
  a: Pt;
  b: Pt;
  owner: [number, number];
  neighbor: [number, number];
  quality: number;
  halo: boolean;
};

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

function qualityColor(q: number) {
  const hue = q < 0.5 ? 350 + (45 - 350) * (q / 0.5) : 45 + (155 - 45) * ((q - 0.5) / 0.5);
  return `hsl(${hue.toFixed(0)} 72% ${(11 + 13 * q).toFixed(0)}%)`;
}

function buildMesh() {
  const rnd = mulberry32(20260214);

  // veraenderte Seeds -> Voronoi-artige Zellen
  const P: Pt[][] = [];
  for (let r = 0; r < ROWS; r++) {
    const row: Pt[] = [];
    for (let c = 0; c < COLS; c++) {
      const jx = (rnd() - 0.5) * 26;
      const jy = (rnd() - 0.5) * 24;
      row.push({
        x: MARGIN + (c / (COLS - 1)) * (W - 2 * MARGIN) + jx,
        y: MARGIN + (r / (ROWS - 1)) * (H - 2 * MARGIN) + jy,
      });
    }
    P.push(row);
  }

  // Zellqualitaet (Hintergrundfaerbung): diagonaler Nachlauf + Abloesungsblase
  const cellQ: number[][] = [];
  for (let r = 0; r < ROWS - 1; r++) {
    const rowQ: number[] = [];
    for (let c = 0; c < COLS - 1; c++) {
      const u = (c + 0.5) / (COLS - 1);
      const v = (r + 0.5) / (ROWS - 1);
      const wake = Math.exp(-Math.pow(u - 0.1 - 0.84 * v, 2) / 0.01);
      const dx = u - 0.87;
      const dy = v - 0.78;
      const bubble = Math.exp(-(dx * dx + dy * dy) / 0.006);
      let q = 1 - wake * 0.65 - bubble * 0.6 + (rnd() - 0.5) * 0.05;
      rowQ.push(clamp01(q));
    }
    cellQ.push(rowQ);
  }

  // Face-Qualitaet direkt am Face-Schwerpunkt: duenner Trail schlechter Faces
  const faceQuality = (a: Pt, b: Pt) => {
    const mx = (a.x + b.x) / 2;
    const my = (a.y + b.y) / 2;
    const u = (mx - MARGIN) / (W - 2 * MARGIN);
    const v = (my - MARGIN) / (H - 2 * MARGIN);
    const wake = Math.exp(-Math.pow(u - 0.1 - 0.84 * v, 2) / 0.004);
    const dx = u - 0.87;
    const dy = v - 0.78;
    const bubble = Math.exp(-(dx * dx + dy * dy) / 0.006);
    return clamp01(1 - wake * 0.75 - bubble * 0.6 + (rnd() - 0.5) * 0.05);
  };

  const faces: Face[] = [];
  for (let r = 0; r < ROWS - 1; r++) {
    for (let c = 0; c < COLS - 1; c++) {
      // horizontale Trennflaeche zwischen (r,c) und (r,c+1)
      if (c < COLS - 2) {
        faces.push({
          id: `h${r}-${c}`,
          a: P[r][c + 1],
          b: P[r + 1][c + 1],
          owner: [r, c],
          neighbor: [r, c + 1],
          quality: faceQuality(P[r][c + 1], P[r + 1][c + 1]),
          halo: c === 0 || c === COLS - 3,
        });
      }
      // vertikale Trennflaeche zwischen (r,c) und (r+1,c)
      if (r < ROWS - 2) {
        faces.push({
          id: `v${r}-${c}`,
          a: P[r + 1][c],
          b: P[r + 1][c + 1],
          owner: [r, c],
          neighbor: [r + 1, c],
          quality: faceQuality(P[r + 1][c], P[r + 1][c + 1]),
          halo: r === 0 || r === ROWS - 3,
        });
      }
    }
  }

  const cells = [];
  for (let r = 0; r < ROWS - 1; r++) {
    for (let c = 0; c < COLS - 1; c++) {
      cells.push({ r, c, pts: [P[r][c], P[r][c + 1], P[r + 1][c + 1], P[r + 1][c]], q: cellQ[r][c] });
    }
  }
  return { P, faces, cells, cellQ };
}

const MESH = buildMesh();

function Formula({
  title,
  eq,
  note,
  tone = "cyan",
}: {
  title: string;
  eq: string;
  note: string;
  tone?: "cyan" | "emerald" | "amber";
}) {
  const tones = {
    cyan: "text-cyan-300",
    emerald: "text-emerald-300",
    amber: "text-amber-300",
  };
  return (
    <div className="rounded-xl border border-white/10 bg-black/25 p-4">
      <div className={`mb-2 font-mono text-[10px] tracking-wide ${tones[tone]}`}>{title}</div>
      <div className="font-mono text-[12.5px] leading-relaxed break-words text-slate-100">{eq}</div>
      <div className="mt-2 text-[11px] leading-relaxed text-slate-500">{note}</div>
    </div>
  );
}

export function MeshErrorMapSection() {
  const [threshold, setThreshold] = useState(0.82);
  const [hideCorrection, setHideCorrection] = useState(false);
  const [showWake, setShowWake] = useState(true);

  const stats = useMemo(() => {
    const n = MESH.faces.length;
    let n0 = 0;
    let n1 = 0;
    let h0 = 0;
    let h1 = 0;
    for (const f of MESH.faces) {
      const fast = f.quality >= threshold;
      if (fast) {
        n0++;
        if (f.halo) h0++;
      } else {
        n1++;
        if (f.halo) h1++;
      }
    }
    const seg = [
      { name: "Segment 0", desc: "innere Zellen · orthogonal", count: n0 - h0, tone: "emerald" },
      { name: "Segment 1", desc: "innere Zellen · Korrektur", count: n1 - h1, tone: "amber" },
      { name: "Segment 2", desc: "Halo-Grenzflächen · orthogonal", count: h0, tone: "cyan" },
      { name: "Segment 3", desc: "Halo-Grenzflächen · Korrektur", count: h1, tone: "rose" },
    ];
    // VRAM-Modell: Klasse 0 = 44 B/Face, Klasse 1 = 140 B/Face (Gradienten 2 x 48 B)
    const saving = (96 * n0) / (140 * n);
    return { n, n0, n1, seg, saving };
  }, [threshold]);

  const pct = (v: number) => `${((v / stats.n) * 100).toFixed(1)} %`;

  return (
    <div className="grid gap-5 xl:grid-cols-[1.55fr_1fr]">
      {/* ---------------- Visualisierung ---------------- */}
      <div className="rounded-2xl border border-white/10 bg-[#080d18] p-4">
        <svg viewBox={`0 0 ${W} ${H}`} className="h-auto w-full" role="img">
          <defs>
            <linearGradient id="wakeGrad" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0%" stopColor="#f43f5e" stopOpacity="0.06" />
              <stop offset="45%" stopColor="#fb923c" stopOpacity="0.26" />
              <stop offset="100%" stopColor="#f43f5e" stopOpacity="0.05" />
            </linearGradient>
          </defs>

          {/* Wake-Band = Wirbelstaerke des Pilot-Laufs (diagonal, entlang u - 0.1 - 0.84v) */}
          {showWake ? (
            <polygon points="88,55 669,457 699,415 118,13" fill="url(#wakeGrad)" />
          ) : null}

          {/* Zellen */}
          {MESH.cells.map((cell) => (
            <polygon
              key={`c${cell.r}-${cell.c}`}
              points={cell.pts.map((p) => `${p.x},${p.y}`).join(" ")}
              fill={qualityColor(cell.q)}
              stroke="rgba(148,163,184,0.16)"
              strokeWidth="0.8"
            />
          ))}

          {/* Faces */}
          {MESH.faces.map((f) => {
            const fast = f.quality >= threshold;
            if (hideCorrection && !fast) return null;
            return (
              <line
                key={f.id}
                x1={f.a.x}
                y1={f.a.y}
                x2={f.b.x}
                y2={f.b.y}
                stroke={fast ? "#34d399" : "#fbbf24"}
                strokeWidth={fast ? 1.3 : 3}
                strokeDasharray={fast ? undefined : "7 4"}
                strokeLinecap="round"
                opacity={fast ? 0.95 : 0.95}
              />
            );
          })}

          {/* Seeds */}
          {MESH.P.flat().map((p, i) => (
            <circle key={i} cx={p.x} cy={p.y} r="2" fill="rgba(226,232,240,0.55)" />
          ))}
        </svg>

        <div className="mt-3 flex flex-wrap items-center gap-x-5 gap-y-2 border-t border-white/5 pt-3 text-[11px] text-slate-400">
          <span className="flex items-center gap-1.5">
            <span className="h-0.5 w-5 rounded bg-emerald-400" /> Klasse 0 · orthogonal (Fast-Path)
          </span>
          <span className="flex items-center gap-1.5">
            <span className="h-0.5 w-5 rounded bg-amber-400" /> Klasse 1 · Korrektur nötig
          </span>
          <span className="flex items-center gap-1.5">
            <span className="h-2.5 w-4 rounded-sm bg-gradient-to-r from-emerald-700/60 to-rose-700/60" />{" "}
            Zellqualität ω
          </span>
        </div>
      </div>

      {/* ---------------- Steuerung & Kennzahlen ---------------- */}
      <div className="space-y-3">
        <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
          <div className="mb-4 flex items-center justify-between">
            <span className="font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
              Klassifikations-Schwelle
            </span>
            <span className="font-mono text-sm text-cyan-300">{threshold.toFixed(2)}</span>
          </div>
          <input
            type="range"
            min={0.2}
            max={0.98}
            step={0.01}
            value={threshold}
            onChange={(e) => setThreshold(Number(e.target.value))}
            className="w-full"
          />
          <p className="mt-3 text-[11.5px] leading-relaxed text-slate-500">
            Faces mit Qualität ≥ Schwelle gelten als orthogonal
            (‖m_f^skew‖ &lt; ε_skew, ‖k_f^comp‖ &lt; ε_ortho) und nehmen den
            branchless Fast-Path. Alles darunter landet im Korrektur-Kernel.
          </p>

          <div className="mt-4 space-y-2.5 border-t border-white/5 pt-4">
            <Toggle checked={hideCorrection} onChange={setHideCorrection} label="Nur Fast-Path anzeigen" />
            <Toggle checked={showWake} onChange={setShowWake} label="Pilot-Wirbelfeld ω einblenden" />
          </div>
        </div>

        {/* 4-Segment-Tabelle */}
        <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-5">
          <div className="mb-3 font-mono text-[10px] tracking-[0.18em] text-slate-500 uppercase">
            4-Quadranten-Face-Partition
          </div>
          <div className="space-y-2.5">
            {stats.seg.map((s) => (
              <div key={s.name} className="flex items-center gap-3">
                <span className="w-[86px] shrink-0 font-mono text-[11px] text-slate-300">
                  {s.name}
                </span>
                <span className="h-2 flex-1 overflow-hidden rounded-full bg-white/5">
                  <span
                    className={cn(
                      "block h-full rounded-full transition-all",
                      s.tone === "emerald" && "bg-emerald-400",
                      s.tone === "amber" && "bg-amber-400",
                      s.tone === "cyan" && "bg-cyan-400",
                      s.tone === "rose" && "bg-rose-400",
                    )}
                    style={{ width: `${(s.count / stats.n) * 100}%` }}
                  />
                </span>
                <span className="w-[68px] shrink-0 text-right font-mono text-[11px] tabular-nums text-slate-400">
                  {s.count} · {pct(s.count)}
                </span>
              </div>
            ))}
          </div>
        </div>

        {/* Ergebnis-Kennzahlen */}
        <div className="rounded-2xl border border-cyan-400/20 bg-cyan-400/[0.06] p-5">
          <div className="flex items-baseline justify-between">
            <span className="text-[12.5px] text-slate-300">Eingesparte VRAM-Bandbreite</span>
            <span className="font-mono text-2xl font-semibold text-cyan-300 tabular-nums">
              {(stats.saving * 100).toFixed(1)} %
            </span>
          </div>
          <p className="mt-2 font-mono text-[10.5px] leading-relaxed text-slate-500">
            Δ = 96 B · N₀ / (140 B · N) — Klasse 1 lädt 2·48 B Zellgradienten, Klasse 0 nicht.
          </p>
          <div className="mt-4 flex flex-wrap gap-1.5">
            <Chip tone="emerald">N₀ = {stats.n0} Faces</Chip>
            <Chip tone="amber">N₁ = {stats.n1} Faces</Chip>
            <Chip>N = {stats.n} Faces</Chip>
          </div>
        </div>

        {/* Weitere Nutzungen */}
        <div className="rounded-2xl border border-white/10 bg-white/[0.02] p-5">
          <div className="mb-2 text-[12.5px] font-semibold text-white">
            Was die Map sonst noch steuert
          </div>
          <ul className="space-y-1.5 text-[11.5px] leading-relaxed text-slate-400">
            <li>· Gezielte Netz-OP nur auf den 0,2 % schlechtesten Zellen</li>
            <li>· SIMP-Update mit ω_i gewichtet → keine „Geister-Zacken“</li>
            <li>· Lokale CFL-Dämpfung Δt_i = ω_i · Δt</li>
            <li>· AMG wählt lokal ILU statt Jacobi bei geschwächter Diagonaldominanz</li>
            <li>· A-priori-Diskretisierungsfehler pro Region im Cockpit</li>
          </ul>
        </div>
      </div>

      {/* Fluss-Spaltung: Herleitung der beiden Klassen */}
      <div className="xl:col-span-2 rounded-2xl border border-white/10 bg-white/[0.02] p-5">
        <div className="mb-4 flex flex-wrap items-center justify-between gap-2">
          <h3 className="text-[13.5px] font-semibold text-white">
            Zerlegung des Flusses über eine innere Fläche
          </h3>
          <Chip tone="violet">Klasse 0 + Klasse 1</Chip>
        </div>

        <div className="grid gap-3 lg:grid-cols-3">
          <Formula
            title="Zerlegung"
            eq="S_f = E_f + k_f^comp ,   E_f = α_f · d_f"
            note="α_f = (d_f · S_f) / (S_f · S_f) — der zu d_f kollineare Anteil"
          />
          <Formula
            title="Klasse 0 · implizit"
            eq="F_f⁽⁰⁾ = ṁ_f (β_f φ_N + (1−β_f) φ_P) − Γ_f α_f (φ_N − φ_P)"
            note="rein impliziter 2-Punkt-Fluss, geht direkt in die CSR-Matrix"
            tone="emerald"
          />
          <Formula
            title="Klasse 1 · explizit"
            eq="F_f⁽¹⁾ = ω_f ( ṁ_f (∇φ)_f · m_f^skew − Γ_f (∇φ)_f · k_f^comp )"
            note="ω_f = min(ω_P, ω_N) ∈ (0,1] gedaempft auf die rechte Seite"
            tone="amber"
          />
        </div>

        <p className="mt-4 text-[12px] leading-relaxed text-slate-500">
          Weicht der Flächenschwerpunkt x_f vom Geradenschnittpunkt
          x_int = β_f x_N + (1−β_f) x_P um m_f^skew = x_f − x_int ab, liefert die
          Taylor-Entwicklung erster Ordnung zusammen mit der exakten Richtungsableitung
          (∇φ)_f · d_f = φ_N − φ_P obige Aufspaltung. Für Klasse 0 ist F_f⁽¹⁾ ≡ 0 — der
          Kernel lädt deshalb weder m_f^skew noch k_f^comp noch die beiden Zellgradienten
          (48 B pro Skalarfeld, 144 B für das Geschwindigkeitsfeld).
        </p>
      </div>
    </div>
  );
}
