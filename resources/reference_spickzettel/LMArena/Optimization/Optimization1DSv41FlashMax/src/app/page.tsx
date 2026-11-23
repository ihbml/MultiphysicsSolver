"use client";

import { useCallback, useEffect, useMemo, useState } from "react";

import verificationData from "@/data/verification.json";
import type { TopoptRun } from "@/lib/engine";
import type { FosmRun, HybridRun, RevolvePlan } from "@/lib/modules";

type Verification = {
  generatedBy: string;
  compiler: string;
  totalSeconds: number;
  chain: {
    designSize: number;
    complexStepMaxRelError: number;
    fdMaxRelError: number;
    volumeConservationError: number;
    endpointLow: number;
    endpointHigh: number;
    monotoneViolations: number;
    ksShiftValue: number;
    ksOverflowGuard: number;
  };
  physics: {
    designSize: number;
    coupledAdjointVsFdMaxRelError: number;
    richardsonMaxRelError: number;
    meanTemperature: number;
    peakTemperature: number;
    pressureDrop: number;
    objective: number;
    thermalResidual: number;
    flowResidual: number;
  };
  mma: {
    quadraticObjective: number;
    referenceObjective: number;
    objectiveGap: number;
    maxConstraintViolation: number;
    kktResidual: number;
    iterations: number;
    converged: boolean;
    dipVsDualObjectiveGap: number;
    concaveTestRelError: number;
  };
  revolve: {
    nsteps: number;
    slots: number;
    peakSlots: number;
    memoryRespected: boolean;
    forwardExecutions: number;
    recomputedSteps: number;
    predictedExecutions: number;
    checkpoints: number;
    restores: number;
    actions: number;
    adjointMaxRelError: number;
    fdAdjointMaxRelError: number;
    binomialCoefficient: number;
  };
  fosm: {
    designSize: number;
    samples: number;
    kappa: number;
    meanValue: number;
    robustValue: number;
    exactSigma: number;
    mcSigma: number;
    sigmaRelError: number;
    maxSigmaSensitivityError: number;
    mcSigmaSensitivityError?: number;
    xiDerivativeError: number;
    exactSeconds: number;
    mcSeconds: number;
    speedup: number;
  };
  hybrid: {
    globalBest: number;
    finalBest: number;
    reachedGlobalOptimum: boolean;
    globalEvaluations: number;
    localEvaluations: number;
    shapeGradientRelError: number;
    shapeObjectiveStart: number;
    shapeObjectiveFinal: number;
    shapeImproved: boolean;
    shapeGlobalEvaluations: number;
  };
  topopt: {
    iterations: number;
    objectiveInitial: number;
    objectiveFinal: number;
    volumeFinal: number;
    graynessFinal: number;
    chainGradientRelError: number;
    peakTemperature: number;
    pressureDrop: number;
    elapsedSeconds: number;
    feasible: boolean;
  };
};

type RunSummary = {
  id: number;
  kind: string;
  title: string;
  metrics: Record<string, unknown>;
  durationMs: number;
  createdAt: string;
};

const V = verificationData as unknown as Verification;

type Tab = "topology" | "robust" | "revolve" | "hybrid" | "verification";

const TABS: { id: Tab; label: string; tag: string }[] = [
  { id: "topology", label: "Thermo-Fluid Topology", tag: "SIMP / RAMP" },
  { id: "robust", label: "FOSM Robust Design", tag: "NO MC" },
  { id: "revolve", label: "Unsteady Adjoint Memory", tag: "REVOLVE" },
  { id: "hybrid", label: "Hybrid Shape Search", tag: "MIGA + PSO" },
  { id: "verification", label: "Mathematical Verification", tag: "C++20" },
];

const fmt = (v: number | undefined | null, digits = 4): string => {
  if (v === undefined || v === null || !Number.isFinite(v)) return "–";
  const a = Math.abs(v);
  if (a !== 0 && (a < 1e-3 || a >= 1e5)) return v.toExponential(digits > 3 ? 3 : 2);
  return v.toFixed(digits);
};

const interpColor = (stops: [number, number, number][], t: number): string => {
  const clamped = Math.min(1, Math.max(0, t));
  const pos = clamped * (stops.length - 1);
  const i = Math.min(stops.length - 2, Math.floor(pos));
  const f = pos - i;
  const c = stops[i].map((v, k) => Math.round(v + f * (stops[i + 1][k] - v)));
  return `rgb(${c[0]},${c[1]},${c[2]})`;
};

const DENSITY_STOPS: [number, number, number][] = [
  [7, 12, 22],
  [12, 45, 74],
  [14, 116, 144],
  [45, 212, 191],
  [250, 204, 21],
  [254, 243, 199],
];
const THERMAL_STOPS: [number, number, number][] = [
  [8, 12, 24],
  [30, 58, 138],
  [14, 165, 233],
  [250, 204, 21],
  [248, 113, 113],
];

function FieldMap({
  values,
  nx,
  ny,
  kind = "density",
  height = 250,
  caption,
}: {
  values: number[];
  nx: number;
  ny: number;
  kind?: "density" | "thermal" | "temperature" | "pressure";
  height?: number;
  caption?: string;
}) {
  const stops = kind === "density" ? DENSITY_STOPS : kind === "pressure" ? DENSITY_STOPS : THERMAL_STOPS;
  const max = Math.max(...values, 1e-12);
  const min = Math.min(...values, 0);
  return (
    <div>
      <svg viewBox={`0 0 ${nx} ${ny}`} className="w-full rounded-lg ring-1 ring-white/10" style={{ height, background: "#060a12" }} shapeRendering="crispEdges">
        {values.map((v, i) => (
          <rect
            key={i}
            x={i % nx}
            y={ny - 1 - Math.floor(i / nx)}
            width={1.02}
            height={1.02}
            fill={interpColor(stops, (v - min) / Math.max(max - min, 1e-9))}
          />
        ))}
      </svg>
      {caption ? <p className="mt-2 text-[11px] leading-relaxed text-slate-500">{caption}</p> : null}
    </div>
  );
}

function LineChart({
  series,
  height = 170,
  yLabel,
  xLabel,
}: {
  series: { name: string; color: string; values: number[] }[];
  height?: number;
  yLabel?: string;
  xLabel?: string;
}) {
  const width = 560;
  const all = series.flatMap((s) => s.values).filter((v) => Number.isFinite(v));
  const min = Math.min(...all, 0);
  const max = Math.max(...all, 1e-9);
  const len = Math.max(...series.map((s) => s.values.length), 2);
  const pad = 26;
  const px = (i: number) => pad + (i / Math.max(len - 1, 1)) * (width - pad - 8);
  const py = (v: number) => 8 + (1 - (v - min) / Math.max(max - min, 1e-12)) * (height - pad - 8);
  return (
    <div>
      <svg viewBox={`0 0 ${width} ${height}`} className="w-full" style={{ height }}>
        {[0, 0.25, 0.5, 0.75, 1].map((f) => (
          <g key={f}>
            <line x1={pad} x2={width - 8} y1={8 + f * (height - pad - 8)} y2={8 + f * (height - pad - 8)} stroke="rgba(148,163,184,0.14)" strokeDasharray="2 4" />
            <text x={pad - 4} y={10 + f * (height - pad - 8)} fill="#64748b" fontSize="8" textAnchor="end">
              {fmt(max - f * (max - min), 2)}
            </text>
          </g>
        ))}
        {series.map((s) => (
          <polyline
            key={s.name}
            fill="none"
            stroke={s.color}
            strokeWidth={1.6}
            points={s.values.map((v, i) => `${px(i)},${py(v)}`).join(" ")}
          />
        ))}
      </svg>
      <div className="mt-1 flex flex-wrap items-center gap-3 text-[10px] uppercase tracking-wider text-slate-500">
        {series.map((s) => (
          <span key={s.name} className="inline-flex items-center gap-1.5">
            <i className="h-1.5 w-4 rounded-full" style={{ background: s.color }} />
            {s.name}
          </span>
        ))}
        {yLabel ? <span className="ml-auto normal-case tracking-normal">{yLabel}</span> : null}
        {xLabel ? <span className="normal-case tracking-normal">{xLabel}</span> : null}
      </div>
    </div>
  );
}

function Metric({ label, value, unit, tone = "cyan", hint }: { label: string; value: string; unit?: string; tone?: "cyan" | "amber" | "rose" | "emerald" | "slate"; hint?: string }) {
  const colors: Record<string, string> = {
    cyan: "text-cyan-300 border-cyan-400/20 from-cyan-400/10",
    amber: "text-amber-300 border-amber-400/20 from-amber-400/10",
    rose: "text-rose-300 border-rose-400/20 from-rose-400/10",
    emerald: "text-emerald-300 border-emerald-400/20 from-emerald-400/10",
    slate: "text-slate-200 border-white/10 from-white/5",
  };
  return (
    <div className={`rounded-lg border bg-gradient-to-b to-transparent px-3 py-2.5 ${colors[tone]}`}>
      <div className="text-[9.5px] font-medium uppercase tracking-[0.14em] text-slate-500">{label}</div>
      <div className={`mt-1 font-mono text-[19px] leading-none ${colors[tone].split(" ")[0]}`}>
        {value}
        {unit ? <span className="ml-1 text-[10px] text-slate-500">{unit}</span> : null}
      </div>
      {hint ? <div className="mt-1 text-[9.5px] text-slate-600">{hint}</div> : null}
    </div>
  );
}

function Panel({ title, subtitle, right, children, className = "" }: { title: string; subtitle?: string; right?: React.ReactNode; children: React.ReactNode; className?: string }) {
  return (
    <section className={`rounded-xl border border-white/10 bg-[#0a1018]/80 shadow-[0_20px_60px_-40px_rgba(34,211,238,0.5)] ${className}`}>
      <header className="flex items-start justify-between gap-3 border-b border-white/5 px-4 py-3">
        <div>
          <h3 className="font-mono text-[11px] uppercase tracking-[0.18em] text-slate-300">{title}</h3>
          {subtitle ? <p className="mt-0.5 text-[11px] text-slate-500">{subtitle}</p> : null}
        </div>
        {right}
      </header>
      <div className="p-4">{children}</div>
    </section>
  );
}

function Field({ label, value, min, max, step, onChange }: { label: string; value: number; min: number; max: number; step: number; onChange: (v: number) => void }) {
  return (
    <label className="block">
      <span className="flex items-center justify-between text-[10px] uppercase tracking-[0.12em] text-slate-500">
        {label}
        <span className="font-mono text-[10px] text-slate-400">{value}</span>
      </span>
      <input
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        onChange={(e) => onChange(Number(e.target.value))}
        className="mt-1 w-full rounded-md border border-white/10 bg-[#060a11] px-2 py-1.5 font-mono text-[12px] text-cyan-200 outline-none transition focus:border-cyan-400/50 focus:ring-1 focus:ring-cyan-400/20"
      />
      <input type="range" value={value} min={min} max={max} step={step} onChange={(e) => onChange(Number(e.target.value))} className="mt-1.5 w-full accent-cyan-400" />
    </label>
  );
}

function Badge({ children, tone = "cyan" }: { children: React.ReactNode; tone?: "cyan" | "emerald" | "amber" | "rose" | "slate" }) {
  const map: Record<string, string> = {
    cyan: "border-cyan-400/30 bg-cyan-400/10 text-cyan-200",
    emerald: "border-emerald-400/30 bg-emerald-400/10 text-emerald-200",
    amber: "border-amber-400/30 bg-amber-400/10 text-amber-200",
    rose: "border-rose-400/30 bg-rose-400/10 text-rose-200",
    slate: "border-white/10 bg-white/5 text-slate-300",
  };
  return <span className={`inline-flex items-center gap-1 rounded border px-1.5 py-0.5 font-mono text-[9.5px] uppercase tracking-[0.12em] ${map[tone]}`}>{children}</span>;
}

const formula = (children: React.ReactNode) => (
  <div className="overflow-x-auto rounded-lg border border-white/10 bg-[#060a11] px-3 py-2 font-mono text-[11.5px] leading-relaxed text-cyan-100/90">{children}</div>
);

export default function CockpitPage() {
  const [tab, setTab] = useState<Tab>("topology");
  const [runs, setRuns] = useState<RunSummary[]>([]);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [elapsed, setElapsed] = useState(0);

  const [topoParams, setTopoParams] = useState({ nx: 26, ny: 16, rmin: 2.8, volFrac: 0.45, tCap: 2.6, wHydraulic: 0.03, betaLevels: 5, iterationsPerBeta: 12 });
  const [topo, setTopo] = useState<TopoptRun | null>(null);
  const [snapshotIndex, setSnapshotIndex] = useState(-1);
  const [fieldView, setFieldView] = useState<"density" | "temperature" | "pressure">("density");

  const [fosmParams, setFosmParams] = useState({ nx: 6, ny: 5, sigmaXi: 0.22, corrLength: 2.2, kappa: 2, rhoKs: 22, simpP: 3 });
  const [fosm, setFosm] = useState<FosmRun | null>(null);

  const [revolveParams, setRevolveParams] = useState({ nsteps: 64, slots: 8 });
  const [revolve, setRevolve] = useState<RevolvePlan | null>(null);

  const [hybridParams, setHybridParams] = useState({ nx: 16, ny: 10, blobs: 2, generations: 5, islands: 3, popPerIsland: 8, psoSwarm: 10, psoIterations: 8 });
  const [hybrid, setHybrid] = useState<HybridRun | null>(null);

  const refreshRuns = useCallback(async () => {
    try {
      const res = await fetch("/api/runs", { cache: "no-store" });
      const json = (await res.json()) as { runs?: RunSummary[] };
      setRuns(json.runs ?? []);
    } catch {
      setRuns([]);
    }
  }, []);

  useEffect(() => {
    void refreshRuns();
  }, [refreshRuns]);

  useEffect(() => {
    if (!busy) return;
    const t0 = Date.now();
    const timer = setInterval(() => setElapsed(Math.round((Date.now() - t0) / 100) / 10), 100);
    return () => clearInterval(timer);
  }, [busy]);

  const execute = useCallback(
    async <T,>(kind: string, params: Record<string, unknown>, apply: (result: T) => void) => {
      setBusy(kind);
      setError(null);
      const t0 = Date.now();
      try {
        const res = await fetch("/api/optimize", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ kind, params }),
        });
        const json = (await res.json()) as { result?: T; error?: string };
        if (!res.ok || !json.result) throw new Error(json.error ?? "optimisation failed");
        apply(json.result);
        if (kind === "topology") setSnapshotIndex(-1);
        void refreshRuns();
      } catch (e) {
        setError(e instanceof Error ? e.message : "unexpected error");
      } finally {
        setElapsed(Math.round((Date.now() - t0) / 100) / 10);
        setBusy(null);
      }
    },
    [refreshRuns],
  );

  const activeSnapshot = useMemo(() => {
    if (!topo) return null;
    if (snapshotIndex < 0 || snapshotIndex >= topo.snapshots.length) return null;
    return topo.snapshots[snapshotIndex];
  }, [topo, snapshotIndex]);

  const displayField = activeSnapshot ? activeSnapshot.density : topo?.density ?? [];
  const betaNow = activeSnapshot ? activeSnapshot.beta : topo?.history.at(-1)?.beta ?? 1;

  return (
    <div className="min-h-screen bg-[#05080e] text-slate-200">
      <div className="pointer-events-none fixed inset-0 opacity-[0.35]" style={{ backgroundImage: "radial-gradient(circle at 15% 0%, rgba(34,211,238,0.12), transparent 45%), radial-gradient(circle at 90% 10%, rgba(251,191,36,0.08), transparent 40%), linear-gradient(rgba(148,163,184,0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(148,163,184,0.05) 1px, transparent 1px)", backgroundSize: "auto, auto, 44px 44px, 44px 44px" }} />
      <div className="relative mx-auto flex min-h-screen max-w-[1680px]">
        <aside className="hidden w-[268px] shrink-0 flex-col border-r border-white/10 bg-[#070b12]/90 px-4 py-5 lg:flex">
          <div className="flex items-center gap-2.5">
            <div className="grid h-9 w-9 place-items-center rounded-md border border-cyan-400/30 bg-gradient-to-br from-cyan-400/20 to-transparent font-mono text-[13px] font-bold text-cyan-300">
              M
            </div>
            <div>
              <div className="font-mono text-[12px] font-semibold tracking-[0.18em] text-slate-100">MDO · PHASE 4</div>
              <div className="text-[10px] uppercase tracking-[0.22em] text-slate-500">optimisation cockpit</div>
            </div>
          </div>

          <div className="mt-6 rounded-lg border border-emerald-400/20 bg-emerald-400/5 px-3 py-2.5">
            <div className="flex items-center gap-1.5 text-[10px] uppercase tracking-[0.16em] text-emerald-300">
              <span className="h-1.5 w-1.5 rounded-full bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.9)]" /> C++20 core verified
            </div>
            <div className="mt-1.5 font-mono text-[10.5px] leading-relaxed text-emerald-200/80">
              chain VJP {fmt(V.chain.complexStepMaxRelError, 2)}
              <br />
              MMA KKT {fmt(V.mma.kktResidual, 2)}
              <br />
              revolve adjoint {fmt(V.revolve.adjointMaxRelError, 1)}
            </div>
          </div>

          <nav className="mt-5 space-y-1">
            {TABS.map((t, i) => (
              <button
                key={t.id}
                onClick={() => setTab(t.id)}
                className={`group flex w-full items-center gap-2 rounded-md border px-2.5 py-2 text-left transition ${
                  tab === t.id ? "border-cyan-400/30 bg-cyan-400/10 text-cyan-100" : "border-transparent text-slate-400 hover:border-white/10 hover:bg-white/5"
                }`}
              >
                <span className={`font-mono text-[10px] ${tab === t.id ? "text-cyan-400" : "text-slate-600"}`}>0{i + 1}</span>
                <span className="flex-1 text-[11.5px] leading-tight">{t.label}</span>
                <span className={`rounded px-1 py-0.5 font-mono text-[8px] uppercase tracking-wider ${tab === t.id ? "bg-cyan-400/20 text-cyan-200" : "bg-white/5 text-slate-500"}`}>{t.tag}</span>
              </button>
            ))}
          </nav>

          <div className="mt-6 flex-1 overflow-hidden">
            <div className="flex items-center justify-between px-1 text-[9.5px] uppercase tracking-[0.18em] text-slate-500">
              <span>Run ledger</span>
              <span className="font-mono text-slate-600">{runs.length}</span>
            </div>
            <div className="mt-2 max-h-[300px] space-y-1.5 overflow-y-auto pr-1">
              {runs.length === 0 ? <p className="px-1 text-[10.5px] leading-relaxed text-slate-600">No runs persisted yet. Launch a module to write to PostgreSQL.</p> : null}
              {runs.map((r) => (
                <div key={r.id} className="rounded-md border border-white/5 bg-white/[0.02] px-2 py-1.5">
                  <div className="flex items-center justify-between">
                    <span className="font-mono text-[9px] uppercase tracking-wider text-cyan-400/80">{r.kind}</span>
                    <span className="font-mono text-[9px] text-slate-600">{r.durationMs} ms</span>
                  </div>
                  <div className="mt-0.5 truncate text-[10px] text-slate-400">{r.title}</div>
                </div>
              ))}
            </div>
          </div>

          <div className="mt-4 rounded-lg border border-white/10 bg-white/[0.02] px-3 py-2 text-[10px] leading-relaxed text-slate-500">
            <div className="font-mono text-[9px] uppercase tracking-[0.16em] text-slate-400">solver stack</div>
            Helmholtz PCG · upwind FVM · two-adjoint chain · MMA dual Newton · Revolve DP · FOSM VHP
          </div>
        </aside>

        <main className="min-w-0 flex-1 px-4 py-5 sm:px-6">
          <header className="flex flex-wrap items-start justify-between gap-3">
            <div>
              <div className="flex items-center gap-2">
                <h1 className="text-[22px] font-semibold tracking-tight text-slate-50">{TABS.find((t) => t.id === tab)?.label}</h1>
                <Badge tone="cyan">live engine</Badge>
              </div>
              <p className="mt-1 max-w-3xl text-[12px] leading-relaxed text-slate-400">
                {tab === "topology" && "Three-field density chain on the FVM graph with exact discrete adjoints, KS peak-temperature aggregation and Svanberg MMA continuation."}
                {tab === "robust" && "Generalized first-order second-moment robust design. Covariance-kernel uncertainty, exact σ-gradient via one tangent-over-adjoint vector-Hessian product."}
                {tab === "revolve" && "Griewank–Walther optimal binomial checkpointing for unsteady adjoints: O(√N) memory with DP-optimal recomputation splits."}
                {tab === "hybrid" && "Two-stage shape search: multi-island GA + PSO explore the basin, then Nelder–Mead and L-BFGS polish with analytic shape gradients."}
                {tab === "verification" && "Every number below was produced by the standalone C++20 harness (g++ -std=c++20 -O2). Nothing is illustrative."}
              </p>
            </div>
            <div className="flex flex-col items-end gap-1 text-right">
              <div className="font-mono text-[10px] uppercase tracking-[0.2em] text-slate-500">backend/optimization</div>
              <div className="font-mono text-[10px] text-slate-400">{V.generatedBy}</div>
              <div className="flex gap-1.5">
                <Badge tone={V.chain.complexStepMaxRelError < 1e-11 ? "emerald" : "amber"}>complex-step &lt; 1e-11</Badge>
                <Badge tone="slate">{V.totalSeconds.toFixed(1)} s harness</Badge>
              </div>
            </div>
          </header>

          {error ? <div className="mt-4 rounded-lg border border-rose-400/30 bg-rose-500/10 px-3 py-2 text-[12px] text-rose-200">{error}</div> : null}

          {tab === "topology" ? (
            <div className="mt-4 space-y-4">
              <div className="grid gap-4 xl:grid-cols-[300px_minmax(0,1fr)_280px]">
                <Panel
                  title="Design controls"
                  subtitle="Three-field chain · continuation schedule"
                  right={<Badge tone="amber">MMA</Badge>}
                >
                  <div className="grid grid-cols-2 gap-3">
                    <Field label="cells x" value={topoParams.nx} min={14} max={40} step={1} onChange={(v) => setTopoParams({ ...topoParams, nx: v })} />
                    <Field label="cells y" value={topoParams.ny} min={10} max={26} step={1} onChange={(v) => setTopoParams({ ...topoParams, ny: v })} />
                    <Field label="r_min" value={topoParams.rmin} min={1.5} max={6} step={0.1} onChange={(v) => setTopoParams({ ...topoParams, rmin: v })} />
                    <Field label="volume frac" value={topoParams.volFrac} min={0.2} max={0.7} step={0.01} onChange={(v) => setTopoParams({ ...topoParams, volFrac: v })} />
                    <Field label="β levels" value={topoParams.betaLevels} min={2} max={6} step={1} onChange={(v) => setTopoParams({ ...topoParams, betaLevels: v })} />
                    <Field label="MMA iters" value={topoParams.iterationsPerBeta} min={4} max={24} step={1} onChange={(v) => setTopoParams({ ...topoParams, iterationsPerBeta: v })} />
                    <Field label="T cap" value={topoParams.tCap} min={0.8} max={12} step={0.1} onChange={(v) => setTopoParams({ ...topoParams, tCap: v })} />
                    <Field label="w hydraulic" value={topoParams.wHydraulic} min={0} max={0.3} step={0.005} onChange={(v) => setTopoParams({ ...topoParams, wHydraulic: v })} />
                  </div>
                  <div className="mt-3 rounded-lg border border-white/10 bg-[#060a11] p-2.5 font-mono text-[10.5px] leading-relaxed text-slate-400">
                    β ∈ {"{"}
                    {[1, 2, 4, 8, 16, 32]
                      .slice(0, Math.max(2, Math.min(6, Math.round(topoParams.betaLevels))))
                      .join(", ")}
                    {"}"} · η = 0.5 · r_min = {topoParams.rmin}
                  </div>
                  <button
                    onClick={() => void execute<TopoptRun>("topopt", topoParams, setTopo)}
                    disabled={busy !== null}
                    className="mt-3 w-full rounded-md border border-cyan-400/40 bg-cyan-400/15 px-3 py-2 font-mono text-[11px] uppercase tracking-[0.16em] text-cyan-200 transition hover:bg-cyan-400/25 disabled:cursor-not-allowed disabled:opacity-50"
                  >
                    {busy === "topopt" ? `optimising… ${elapsed}s` : "run topology optimisation"}
                  </button>
                  <div className="mt-2 text-[10px] leading-relaxed text-slate-500">
                    Live TypeScript mirror of the C++ chain: filter PCG + GMRES primal/adjoint solves + MMA dual Newton. Runs are persisted to PostgreSQL.
                  </div>
                </Panel>

                <Panel
                  title="Design field"
                  subtitle={topo ? `β = ${fmt(betaNow, 1)} · ${displayField.length} cells · ${fieldView}` : "awaiting run"}
                  right={
                    <div className="flex gap-1">
                      {(["density", "temperature", "pressure"] as const).map((v) => (
                        <button
                          key={v}
                          onClick={() => setFieldView(v)}
                          className={`rounded border px-1.5 py-0.5 font-mono text-[9px] uppercase ${fieldView === v ? "border-cyan-400/40 bg-cyan-400/15 text-cyan-200" : "border-white/10 text-slate-500"}`}
                        >
                          {v.slice(0, 3)}
                        </button>
                      ))}
                    </div>
                  }
                >
                  {topo ? (
                    <>
                      <FieldMap
                        values={fieldView === "density" ? displayField : fieldView === "temperature" ? topo.temperature : topo.pressure}
                        nx={topo.nx}
                        ny={topo.ny}
                        kind={fieldView}
                        height={286}
                        caption={
                          fieldView === "density"
                            ? "Projected physical density γ̄. Bright = solid conducting material, dark = fluid channel with advective heat removal."
                            : fieldView === "temperature"
                              ? "Steady energy field T: upwind convection–diffusion on the FVM graph with a cold Dirichlet inlet."
                              : "Darcy pressure field p: inlet 1.0 (left) → outlet 0.0 (right) through the permeability interpolation κ(γ̄)."
                        }
                      />
                      <div className="mt-3 flex items-center gap-2">
                        <span className="font-mono text-[9px] uppercase tracking-wider text-slate-500">snapshots</span>
                        <button
                          onClick={() => setSnapshotIndex(-1)}
                          className={`rounded border px-1.5 py-0.5 font-mono text-[9px] ${snapshotIndex < 0 ? "border-cyan-400/40 bg-cyan-400/15 text-cyan-200" : "border-white/10 text-slate-500"}`}
                        >
                          final
                        </button>
                        {topo.snapshots.slice(0, 5).map((s, i) => (
                          <button
                            key={`${s.iteration}-${i}`}
                            onClick={() => setSnapshotIndex(i)}
                            className={`rounded border px-1.5 py-0.5 font-mono text-[9px] ${snapshotIndex === i ? "border-cyan-400/40 bg-cyan-400/15 text-cyan-200" : "border-white/10 text-slate-500"}`}
                          >
                            β{fmt(s.beta, 0)}·{s.iteration}
                          </button>
                        ))}
                      </div>
                    </>
                  ) : (
                    <div className="grid h-[330px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11]">
                      <div className="text-center">
                        <div className="mx-auto mb-3 h-10 w-10 animate-pulse rounded-full border border-cyan-400/30 bg-cyan-400/10" />
                        <p className="font-mono text-[11px] text-slate-500">launch a run to populate the design field</p>
                      </div>
                    </div>
                  )}
                </Panel>

                <div className="space-y-3">
                  <Metric label="objective J" value={topo ? fmt(topo.objective, 4) : "–"} tone="cyan" hint="w_T·mean(T) + w_P·Φ_flow" />
                  <Metric label="physical volume" value={topo ? fmt(topo.volume, 4) : "–"} tone={topo && topo.volume <= topoParams.volFrac + 0.02 ? "emerald" : "amber"} hint={`target ${topoParams.volFrac}`} />
                  <Metric label="grayness" value={topo ? fmt(topo.grayness, 4) : "–"} tone={topo && topo.grayness < 0.15 ? "emerald" : "slate"} hint="4γ̄(1−γ̄) averaged" />
                  <Metric label="KS peak T" value={topo ? fmt(topo.ksT, 4) : "–"} tone={topo && topo.ksT <= topoParams.tCap ? "emerald" : "rose"} hint={`limit ${topoParams.tCap}`} />
                  <Metric label="hydraulic power" value={topo ? fmt(topo.dissipation, 4) : "–"} tone="slate" hint="Σ κ_f (Δp)²" />
                  <Metric label="runtime" value={topo ? `${(topo.elapsedMs / 1000).toFixed(2)} s` : "–"} tone="slate" hint={topo ? `${topo.physicsEvaluations} primal · ${topo.filterSolves} filter solves` : undefined} />
                </div>
              </div>

              <div className="grid gap-4 lg:grid-cols-2">
                <Panel title="Convergence trace" subtitle="MMA continuation with adaptive asymptotes">
                  {topo ? (
                    <LineChart
                      series={[
                        { name: "objective J", color: "#22d3ee", values: topo.history.map((h) => h.objective) },
                        { name: "peak T (KS)", color: "#fbbf24", values: topo.history.map((h) => h.ksT) },
                        { name: "volume", color: "#a78bfa", values: topo.history.map((h) => h.volume) },
                      ]}
                      yLabel="iteration →"
                    />
                  ) : (
                    <p className="text-[11.5px] text-slate-500">Convergence history will render after the first run.</p>
                  )}
                </Panel>
                <Panel title="Regularisation chain" subtitle="exact VJP backpropagation">
                  <div className="space-y-2">
                    {formula(<>−(r<sub>min</sub>/2√3)² ∇²γ̃ + γ̃ = γ &nbsp;·&nbsp; Helmholtz PDE filter (SPD graph Laplacian)</>)}
                    {formula(<>γ̄ = [tanh(βη) + tanh(β(γ̃−η))] / [tanh(βη) + tanh(β(1−η))]</>)}
                    {formula(<>g<sub>KS</sub> = T<sub>max</sub> + ρ<sup>−1</sup> ln( N<sup>−1</sup> Σ exp(ρ(T<sub>i</sub>−T<sub>max</sub>)) )</>)}
                    {formula(<>dJ/dγ = A<sub>Helm</sub><sup>−T</sup>( ∂γ̄/∂γ̃ ⊙ dJ/dγ̄ )</>)}
                  </div>
                  <div className="mt-3 grid grid-cols-2 gap-2 text-[10.5px]">
                    <div className="rounded border border-emerald-400/20 bg-emerald-400/5 px-2 py-1.5">
                      <div className="text-slate-500">complex-step max rel. err</div>
                      <div className="font-mono text-emerald-300">{fmt(V.chain.complexStepMaxRelError, 2)}</div>
                    </div>
                    <div className="rounded border border-emerald-400/20 bg-emerald-400/5 px-2 py-1.5">
                      <div className="text-slate-500">central FD max rel. err</div>
                      <div className="font-mono text-emerald-300">{fmt(V.chain.fdMaxRelError, 2)}</div>
                    </div>
                    <div className="rounded border border-white/10 bg-white/[0.02] px-2 py-1.5">
                      <div className="text-slate-500">filter volume drift</div>
                      <div className="font-mono text-cyan-300">{fmt(V.chain.volumeConservationError, 2)}</div>
                    </div>
                    <div className="rounded border border-white/10 bg-white/[0.02] px-2 py-1.5">
                      <div className="text-slate-500">KS overflow-shift guard</div>
                      <div className="font-mono text-cyan-300">{fmt(V.chain.ksShiftValue, 1)}</div>
                    </div>
                  </div>
                </Panel>
              </div>

              <Panel title="Discrete adjoint chain" subtitle="one thermal + one pressure adjoint per functional">
                <div className="grid gap-2 md:grid-cols-4">
                  {[
                    { k: "γ", l: "design", d: "density variables" },
                    { k: "γ̃", l: "Helmholtz solve", d: "PCG · SPD" },
                    { k: "γ̄", l: "Heaviside", d: "continuation β" },
                    { k: "T, p", l: "primal states", d: "GMRES · upwind FVM" },
                  ].map((s, i) => (
                    <div key={s.k} className="relative rounded-lg border border-white/10 bg-[#060a11] px-3 py-2">
                      <div className="font-mono text-[13px] text-cyan-300">{s.k}</div>
                      <div className="text-[10.5px] text-slate-400">{s.l}</div>
                      <div className="font-mono text-[9px] uppercase tracking-wider text-slate-600">{s.d}</div>
                      {i < 3 ? <span className="absolute -right-1.5 top-1/2 hidden -translate-y-1/2 font-mono text-cyan-500/60 md:block">›</span> : null}
                    </div>
                  ))}
                </div>
                <div className="mt-3 grid gap-2 md:grid-cols-4">
                  <Metric label="coupled grad vs FD" value={fmt(V.physics.coupledAdjointVsFdMaxRelError, 2)} tone="emerald" hint={`${V.physics.designSize} design cells`} />
                  <Metric label="thermal residual" value={fmt(V.physics.thermalResidual, 1)} tone="slate" />
                  <Metric label="pressure residual" value={fmt(V.physics.flowResidual, 1)} tone="slate" />
                  <Metric label="mean / peak T" value={`${fmt(V.physics.meanTemperature, 3)} / ${fmt(V.physics.peakTemperature, 3)}`} tone="amber" />
                </div>
              </Panel>
            </div>
          ) : null}

          {tab === "robust" ? (
            <div className="mt-4 grid gap-4 xl:grid-cols-[300px_minmax(0,1fr)_300px]">
              <Panel title="Uncertainty model" subtitle="exponential covariance kernel" right={<Badge tone="cyan">FOSM</Badge>}>
                <div className="grid grid-cols-2 gap-3">
                  <Field label="grid x" value={fosmParams.nx} min={4} max={8} step={1} onChange={(v) => setFosmParams({ ...fosmParams, nx: v })} />
                  <Field label="grid y" value={fosmParams.ny} min={4} max={8} step={1} onChange={(v) => setFosmParams({ ...fosmParams, ny: v })} />
                  <Field label="σ ξ" value={fosmParams.sigmaXi} min={0.02} max={0.6} step={0.01} onChange={(v) => setFosmParams({ ...fosmParams, sigmaXi: v })} />
                  <Field label="ℓ corr" value={fosmParams.corrLength} min={0.5} max={8} step={0.1} onChange={(v) => setFosmParams({ ...fosmParams, corrLength: v })} />
                  <Field label="κ (robust)" value={fosmParams.kappa} min={0.5} max={4} step={0.1} onChange={(v) => setFosmParams({ ...fosmParams, kappa: v })} />
                  <Field label="ρ KS" value={fosmParams.rhoKs} min={8} max={60} step={1} onChange={(v) => setFosmParams({ ...fosmParams, rhoKs: v })} />
                  <Field label="SIMP p" value={fosmParams.simpP} min={1} max={5} step={0.5} onChange={(v) => setFosmParams({ ...fosmParams, simpP: v })} />
                </div>
                <button
                  onClick={() => void execute<FosmRun>("fosm", fosmParams, setFosm)}
                  disabled={busy !== null}
                  className="mt-3 w-full rounded-md border border-cyan-400/40 bg-cyan-400/15 px-3 py-2 font-mono text-[11px] uppercase tracking-[0.16em] text-cyan-200 transition hover:bg-cyan-400/25 disabled:cursor-not-allowed disabled:opacity-50"
                >
                  {busy === "fosm" ? `solving… ${elapsed}s` : "evaluate robust design"}
                </button>
                <div className="mt-3 space-y-2">
                  {formula(<>Σ<sub>ij</sub> = σ<sub>ξ</sub>² exp(−‖x<sub>i</sub>−x<sub>j</sub>‖ / ℓ<sub>corr</sub>)</>)}
                  {formula(<>Φ = μ<sub>g</sub> + κσ<sub>g</sub>, &nbsp; σ<sub>g</sub> = √(d<sup>T</sup>Σd), &nbsp; d = dg/dξ</>)}
                  {formula(<>v<sub>ξ</sub> = Σd / σ<sub>g</sub> → one VHP: dσ/dγ = λ<sup>T</sup>A<sub>,γ</sub>μ + η<sup>T</sup>A<sub>,γ</sub>T</>)}
                </div>
              </Panel>

              <div className="space-y-4">
                <Panel title="Robust objective" subtitle="mean + κ·standard deviation">
                  <div className="grid gap-2 sm:grid-cols-4">
                    <Metric label="μ (nominal)" value={fosm ? fmt(fosm.mean, 4) : "–"} tone="cyan" />
                    <Metric label="σ (FOSM)" value={fosm ? fmt(fosm.sigma, 3) : "–"} tone="amber" />
                    <Metric label="Φ robust" value={fosm ? fmt(fosm.robust, 4) : "–"} tone="emerald" />
                    <Metric label="κ" value={fosm ? fmt(fosm.kappa, 2) : "–"} tone="slate" />
                  </div>
                  <div className="mt-3">
                    {fosm ? (
                      <FieldMap values={fosm.temperature} nx={fosm.nx} ny={fosm.ny} kind="thermal" height={190} caption="Nominal temperature field T(γ, μ_ξ) used as the FOSM mean state." />
                    ) : (
                      <div className="grid h-[190px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11] font-mono text-[11px] text-slate-500">no evaluation yet</div>
                    )}
                  </div>
                </Panel>
                <div className="grid gap-4 md:grid-cols-2">
                  <Panel title="Covariance kernel" subtitle={`${fosm ? fosm.nx * fosm.ny : 30} × ${fosm ? fosm.nx * fosm.ny : 30} Σ`}>
                    {fosm ? (
                      <div>
                        <svg viewBox={`0 0 ${fosm.covariance.length} ${fosm.covariance.length}`} className="w-full rounded ring-1 ring-white/10" style={{ height: 190, background: "#060a12" }} shapeRendering="crispEdges">
                          {fosm.covariance.map((row, i) =>
                            row.map((v, j) => (
                              <rect key={`${i}-${j}`} x={j} y={i} width={1.02} height={1.02} fill={interpColor(DENSITY_STOPS, v / Math.max(fosm.sigmaXi * fosm.sigmaXi, 1e-12))} />
                            )),
                          )}
                        </svg>
                        <p className="mt-1.5 text-[10px] leading-relaxed text-slate-500">Spatially correlated material uncertainty. Bright = strong correlation; correlation length ℓ = {fosm.corrLength}.</p>
                      </div>
                    ) : (
                      <div className="grid h-[190px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11] font-mono text-[11px] text-slate-500">kernel renders after evaluation</div>
                    )}
                  </Panel>
                  <Panel title="Sensitivity signatures" subtitle="dσ/dγ vs dμ/dγ per design cell">
                    {fosm ? (
                      <div className="space-y-2">
                        {fosm.dSigmaDgamma.slice(0, 12).map((v, i) => {
                          const scale = Math.max(...fosm.dSigmaDgamma.map((x) => Math.abs(x)), 1e-12);
                          const muScale = Math.max(...fosm.dMuDgamma.map((x) => Math.abs(x)), 1e-12);
                          return (
                            <div key={i} className="flex items-center gap-2">
                              <span className="w-8 font-mono text-[9px] text-slate-500">c{i}</span>
                              <div className="relative h-2 flex-1 rounded bg-white/5">
                                <span className="absolute top-0 h-2 rounded bg-cyan-400/70" style={{ left: `${50 + (50 * v) / scale}%`, width: `${(50 * Math.abs(v)) / scale}%`, transform: v < 0 ? "translateX(-100%)" : undefined }} />
                              </div>
                              <div className="relative h-2 flex-1 rounded bg-white/5">
                                <span className="absolute top-0 h-2 rounded bg-amber-400/70" style={{ left: `${50 + (50 * fosm.dMuDgamma[i]) / muScale}%`, width: `${(50 * Math.abs(fosm.dMuDgamma[i])) / muScale}%`, transform: fosm.dMuDgamma[i] < 0 ? "translateX(-100%)" : undefined }} />
                              </div>
                            </div>
                          );
                        })}
                        <div className="flex justify-between font-mono text-[9px] uppercase tracking-wider text-slate-600">
                          <span className="text-cyan-300">dσ/dγ</span>
                          <span className="text-amber-300">dμ/dγ</span>
                        </div>
                      </div>
                    ) : (
                      <div className="grid h-[190px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11] font-mono text-[11px] text-slate-500">run to inspect the VHP gradient</div>
                    )}
                  </Panel>
                </div>
              </div>

              <div className="space-y-3">
                <Metric label="FOSM samples needed" value="0" tone="emerald" hint="no Monte Carlo" />
                <Metric label="adjoint solves / evaluation" value={fosm ? String(fosm.solves) : "5"} tone="cyan" hint="primal + adjoint + VHP" />
                <Metric label="σ vs 10k MC (C++)" value={`${fmt(V.fosm.sigmaRelError * 100, 2)} %`} tone="emerald" hint={`MC σ = ${fmt(V.fosm.mcSigma, 3)}`} />
                <Metric label="σ-gradient vs exact FD" value={`${fmt(V.fosm.maxSigmaSensitivityError * 100, 1)} %`} tone="amber" hint="second-order adjoint" />
                <Metric label="MC speed-up (C++)" value={`${fmt(V.fosm.speedup, 0)}×`} tone="emerald" hint={`${V.fosm.samples} samples vs 5 solves`} />
                <div className="rounded-lg border border-white/10 bg-white/[0.02] p-3 text-[10.5px] leading-relaxed text-slate-400">
                  <div className="font-mono text-[9px] uppercase tracking-[0.16em] text-slate-500">dg/dξ validation</div>
                  <div className="mt-1 font-mono text-[13px] text-cyan-300">{fmt(V.fosm.xiDerivativeError, 2)}</div>
                  max relative error against central differences of the state.
                </div>
              </div>
            </div>
          ) : null}

          {tab === "revolve" ? (
            <div className="mt-4 grid gap-4 xl:grid-cols-[300px_minmax(0,1fr)_300px]">
              <Panel title="Checkpointing policy" subtitle="N steps · S RAM slots" right={<Badge tone="cyan">binomial</Badge>}>
                <div className="grid grid-cols-2 gap-3">
                  <Field label="time steps N" value={revolveParams.nsteps} min={8} max={160} step={1} onChange={(v) => setRevolveParams({ ...revolveParams, nsteps: v })} />
                  <Field label="checkpoint slots S" value={revolveParams.slots} min={0} max={20} step={1} onChange={(v) => setRevolveParams({ ...revolveParams, slots: v })} />
                </div>
                <button
                  onClick={() => void execute<RevolvePlan>("revolve", revolveParams, setRevolve)}
                  disabled={busy !== null}
                  className="mt-3 w-full rounded-md border border-cyan-400/40 bg-cyan-400/15 px-3 py-2 font-mono text-[11px] uppercase tracking-[0.16em] text-cyan-200 transition hover:bg-cyan-400/25 disabled:cursor-not-allowed disabled:opacity-50"
                >
                  {busy === "revolve" ? "planning…" : "build revolve schedule"}
                </button>
                <div className="mt-3 space-y-2">
                  {formula(<>cost(n,s) = min<sub>i</sub> [ i + cost(i,s) + cost(n−i,s−1) ]</>)}
                  {formula(<>cost(n,s) = n &nbsp;if&nbsp; n ≤ s+1&nbsp;; &nbsp;n(n+1)/2 &nbsp;if&nbsp; s = 0</>)}
                  {formula(<>memory = S states &nbsp;→&nbsp; O(√N) at S = ⌈√N⌉</>)}
                </div>
                <div className="mt-3 rounded-lg border border-white/10 bg-[#060a11] p-2 text-[10.5px] leading-relaxed text-slate-400">
                  “Revolve” Advancing/Reversing recursion, Griewank &amp; Walther, ACM TOMS 26(2), 2000.
                </div>
              </Panel>

              <Panel title="Schedule" subtitle="action stream over the adjoint sweep">
                <div className="grid gap-2 sm:grid-cols-3">
                  <Metric label="peak slots used" value={revolve ? String(revolve.peakSlots) : "–"} tone={revolve && revolve.peakSlots <= revolveParams.slots ? "emerald" : "rose"} hint={revolve ? `capacity ${revolve.slots}` : undefined} />
                  <Metric label="forward executions" value={revolve ? String(revolve.predictedForwardExecutions) : "–"} tone="cyan" hint="DP-optimal" />
                  <Metric label="zero-memory baseline" value={revolve ? String(revolve.zeroMemoryExecutions) : "–"} tone="slate" hint="N(N+1)/2" />
                </div>
                <div className="mt-3 space-y-1.5">
                  {revolve ? (
                    <>
                      {revolve.dpTable.map((row) => (
                        <div key={row.s} className="flex items-center gap-2">
                          <span className="w-12 font-mono text-[9px] text-slate-500">S={row.s}</span>
                          <div className="h-2.5 flex-1 rounded bg-white/5">
                            <div className="h-2.5 rounded bg-gradient-to-r from-cyan-400/70 to-emerald-400/70" style={{ width: `${(row.cost / Math.max(revolve.zeroMemoryExecutions, 1)) * 100}%` }} />
                          </div>
                          <span className="w-14 text-right font-mono text-[9px] text-slate-400">{row.cost}</span>
                        </div>
                      ))}
                      <p className="pt-1 text-[10px] text-slate-500">Forward-step executions versus checkpoint capacity. The harness measures the same counts on a nonlinear scalar ODE adjoint.</p>
                    </>
                  ) : (
                    <div className="grid h-[140px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11] font-mono text-[11px] text-slate-500">plan a schedule to inspect the DP frontier</div>
                  )}
                </div>
                <div className="mt-3 max-h-[210px] overflow-y-auto rounded-lg border border-white/10 bg-[#060a11] p-2">
                  {revolve ? (
                    <div className="flex flex-wrap gap-1">
                      {revolve.timeline.map((a, i) => (
                        <span
                          key={i}
                          title={`${a.label} · slot ${a.slot}`}
                          className={`inline-block h-4 w-4 rounded-sm ${
                            a.kind === "checkpoint"
                              ? "bg-cyan-400/80"
                              : a.kind === "reverse"
                                ? "bg-amber-400/80"
                                : a.kind === "restore"
                                  ? "bg-violet-400/80"
                                  : "bg-slate-600"
                          }`}
                        />
                      ))}
                    </div>
                  ) : (
                    <p className="font-mono text-[10px] text-slate-600">cyan = checkpoint · amber = adjoint step · violet = restore</p>
                  )}
                </div>
                <div className="mt-2 flex gap-3 font-mono text-[9px] uppercase tracking-wider text-slate-500">
                  <span className="text-cyan-300">checkpoint {revolve?.checkpoints ?? "–"}</span>
                  <span className="text-amber-300">reverse {revolve?.reverses ?? "–"}</span>
                  <span className="text-violet-300">restore {revolve?.restores ?? "–"}</span>
                </div>
              </Panel>

              <div className="space-y-3">
                <Metric label="C++ peak slots" value={`${V.revolve.peakSlots}`} tone={V.revolve.memoryRespected ? "emerald" : "rose"} hint={`capacity ${V.revolve.slots} · N=${V.revolve.nsteps}`} />
                <Metric label="C++ forward executions" value={`${V.revolve.forwardExecutions}`} tone="cyan" hint={`${V.revolve.actions} actions`} />
                <Metric label="recomputed steps" value={`${V.revolve.recomputedSteps}`} tone="slate" />
                <Metric label="adjoint vs full storage" value={fmt(V.revolve.adjointMaxRelError, 1)} tone="emerald" hint="checkpointed gradient exact" />
                <Metric label="adjoint vs FD" value={fmt(V.revolve.fdAdjointMaxRelError, 2)} tone="emerald" />
                <Metric label="C(N,S) binomial" value={fmt(V.revolve.binomialCoefficient, 0)} tone="amber" />
              </div>
            </div>
          ) : null}

          {tab === "hybrid" ? (
            <div className="mt-4 grid gap-4 xl:grid-cols-[300px_minmax(0,1fr)_300px]">
              <Panel title="Shape search budget" subtitle="global stage → local handoff" right={<Badge tone="amber">2-stage</Badge>}>
                <div className="grid grid-cols-2 gap-3">
                  <Field label="mesh x" value={hybridParams.nx} min={12} max={22} step={1} onChange={(v) => setHybridParams({ ...hybridParams, nx: v })} />
                  <Field label="mesh y" value={hybridParams.ny} min={8} max={14} step={1} onChange={(v) => setHybridParams({ ...hybridParams, ny: v })} />
                  <Field label="obstacles" value={hybridParams.blobs} min={1} max={3} step={1} onChange={(v) => setHybridParams({ ...hybridParams, blobs: v })} />
                  <Field label="islands" value={hybridParams.islands} min={2} max={4} step={1} onChange={(v) => setHybridParams({ ...hybridParams, islands: v })} />
                  <Field label="pop / island" value={hybridParams.popPerIsland} min={6} max={14} step={1} onChange={(v) => setHybridParams({ ...hybridParams, popPerIsland: v })} />
                  <Field label="generations" value={hybridParams.generations} min={2} max={12} step={1} onChange={(v) => setHybridParams({ ...hybridParams, generations: v })} />
                  <Field label="PSO swarm" value={hybridParams.psoSwarm} min={6} max={18} step={1} onChange={(v) => setHybridParams({ ...hybridParams, psoSwarm: v })} />
                  <Field label="PSO iters" value={hybridParams.psoIterations} min={4} max={20} step={1} onChange={(v) => setHybridParams({ ...hybridParams, psoIterations: v })} />
                </div>
                <button
                  onClick={() => void execute<HybridRun>("hybrid", hybridParams, setHybrid)}
                  disabled={busy !== null}
                  className="mt-3 w-full rounded-md border border-amber-400/40 bg-amber-400/15 px-3 py-2 font-mono text-[11px] uppercase tracking-[0.16em] text-amber-200 transition hover:bg-amber-400/25 disabled:cursor-not-allowed disabled:opacity-50"
                >
                  {busy === "hybrid" ? `searching… ${elapsed}s` : "run hybrid shape search"}
                </button>
                <div className="mt-3 space-y-2">
                  {formula(<>MIGA: ring migration, SBX (η<sub>c</sub>=15), polynomial mutation (η<sub>m</sub>=20)</>)}
                  {formula(<>PSO: v ← ωv + c<sub>1</sub>r<sub>1</sub>(p−x) + c<sub>2</sub>r<sub>2</sub>(g−x)</>)}
                  {formula(<>NM simplex (α,γ,ρ,σ)=(1,2,½,½) → L-BFGS two-loop recursion</>)}
                  {formula(<>dJ/dθ<sub>k</sub> = Σ<sub>e</sub> (dJ/dγ<sub>e</sub>)(∂γ<sub>e</sub>/∂θ<sub>k</sub>)</>)}
                </div>
              </Panel>

              <div className="space-y-4">
                <Panel title="Best shape" subtitle="obstacle parameters mapped to the density field">
                  {hybrid ? (
                    <div className="grid gap-3 md:grid-cols-[minmax(0,1fr)_160px]">
                      <FieldMap values={hybrid.density} nx={hybrid.nx} ny={hybrid.ny} height={240} caption="Smooth obstacle bank (bright = solid) after global + local optimisation of the coupled thermo-fluid objective." />
                      <div className="space-y-2">
                        <Metric label="global best" value={fmt(hybrid.globalBest, 4)} tone="amber" />
                        <Metric label="local polish" value={fmt(hybrid.best, 4)} tone="emerald" />
                        <Metric label="total evaluations" value={`${hybrid.globalEvaluations + hybrid.localEvaluations}`} tone="cyan" />
                        <Metric label="elapsed" value={`${(hybrid.elapsedMs / 1000).toFixed(2)} s`} tone="slate" />
                      </div>
                    </div>
                  ) : (
                    <div className="grid h-[250px] place-items-center rounded-lg border border-dashed border-white/10 bg-[#060a11] font-mono text-[11px] text-slate-500">run the two-stage search</div>
                  )}
                </Panel>
                <Panel title="Stage convergence" subtitle="global exploration then local acceleration">
                  {hybrid ? (
                    <LineChart
                      series={[
                        { name: "MIGA best", color: "#22d3ee", values: hybrid.history.filter((h) => h.stage === "MIGA").map((h) => h.value) },
                        { name: "PSO best", color: "#a78bfa", values: hybrid.history.filter((h) => h.stage === "PSO").map((h) => h.value) },
                        { name: "local (NM + L-BFGS)", color: "#fbbf24", values: hybrid.history.filter((h) => h.stage === "Nelder-Mead" || h.stage === "L-BFGS").map((h) => h.value) },
                      ]}
                      yLabel="merit"
                    />
                  ) : (
                    <p className="text-[11.5px] text-slate-500">Stage histories appear after a run.</p>
                  )}
                </Panel>
              </div>

              <div className="space-y-3">
                <Metric label="C++ Rastrigin global" value={fmt(V.hybrid.globalBest, 3)} tone="amber" hint="MIGA+PSO basin" />
                <Metric label="C++ Rastrigin final" value={fmt(V.hybrid.finalBest, 3)} tone="emerald" hint="after NM + L-BFGS" />
                <Metric label="global optimum reached" value={V.hybrid.reachedGlobalOptimum ? "yes" : "no"} tone={V.hybrid.reachedGlobalOptimum ? "emerald" : "rose"} />
                <Metric label="shape objective" value={`${fmt(V.hybrid.shapeObjectiveStart, 3)} → ${fmt(V.hybrid.shapeObjectiveFinal, 3)}`} tone="cyan" hint="thermo-fluid model" />
                <Metric label="shape gradient vs FD" value={fmt(V.hybrid.shapeGradientRelError, 2)} tone="amber" hint="chain rule through blobs" />
                <Metric label="local evaluations" value={`${V.hybrid.localEvaluations}`} tone="slate" />
                <div className="rounded-lg border border-white/10 bg-white/[0.02] p-3 text-[10.5px] leading-relaxed text-slate-400">
                  Global stage {V.hybrid.globalEvaluations} evaluations · local stage {V.hybrid.localEvaluations}. The in-memory warm start passes the global best chromosome directly to the simplex.
                </div>
              </div>
            </div>
          ) : null}

          {tab === "verification" ? (
            <div className="mt-4 space-y-4">
              <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
                {[
                  { k: "chain VJP", v: fmt(V.chain.complexStepMaxRelError, 2), t: "complex-step vs analytic", ok: V.chain.complexStepMaxRelError < 1e-11 },
                  { k: "coupled adjoint", v: fmt(V.physics.coupledAdjointVsFdMaxRelError, 2), t: "vs central differences", ok: V.physics.coupledAdjointVsFdMaxRelError < 1e-4 },
                  { k: "MMA KKT", v: fmt(V.mma.kktResidual, 2), t: `${V.mma.iterations} iterations`, ok: V.mma.kktResidual < 1e-6 },
                  { k: "revolve adjoint", v: fmt(V.revolve.adjointMaxRelError, 1), t: "checkpointed vs full storage", ok: V.revolve.adjointMaxRelError < 1e-9 },
                ].map((c) => (
                  <div key={c.k} className="rounded-xl border border-white/10 bg-[#0a1018]/80 p-3">
                    <div className="flex items-center justify-between">
                      <span className="font-mono text-[9.5px] uppercase tracking-[0.14em] text-slate-500">{c.k}</span>
                      <Badge tone={c.ok ? "emerald" : "rose"}>{c.ok ? "pass" : "review"}</Badge>
                    </div>
                    <div className="mt-1 font-mono text-[18px] text-cyan-300">{c.v}</div>
                    <div className="text-[10px] text-slate-500">{c.t}</div>
                  </div>
                ))}
              </div>

              <div className="grid gap-4 lg:grid-cols-2">
                <Panel title="Three-field chain" subtitle="backend/optimization/include/mdo/fvm.hpp">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["design cells", String(V.chain.designSize)],
                        ["complex-step max rel. error", fmt(V.chain.complexStepMaxRelError, 3)],
                        ["central FD max rel. error", fmt(V.chain.fdMaxRelError, 3)],
                        ["filter volume drift", fmt(V.chain.volumeConservationError, 3)],
                        ["γ̄(0), |γ̄(1)−1|", `${fmt(V.chain.endpointLow, 1)}, ${fmt(V.chain.endpointHigh, 1)}`],
                        ["monotonicity violations", String(V.chain.monotoneViolations)],
                        ["KS shift guard", fmt(V.chain.ksShiftValue, 1)],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
                <Panel title="Coupled thermo-fluid adjoint" subtitle="include/mdo/thermofluid.hpp">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["primal design cells", String(V.physics.designSize)],
                        ["adjoint vs FD", fmt(V.physics.coupledAdjointVsFdMaxRelError, 3)],
                        ["Richardson extrapolated", fmt(V.physics.richardsonMaxRelError, 3)],
                        ["objective", fmt(V.physics.objective, 5)],
                        ["mean / peak T", `${fmt(V.physics.meanTemperature, 4)} / ${fmt(V.physics.peakTemperature, 4)}`],
                        ["hydraulic dissipation", fmt(V.physics.pressureDrop, 5)],
                        ["energy / Darcy residual", `${fmt(V.physics.thermalResidual, 1)} / ${fmt(V.physics.flowResidual, 1)}`],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
                <Panel title="Svanberg MMA" subtitle="include/mdo/mma.hpp · dual Newton + SolveDIP">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["quadratic objective", fmt(V.mma.quadraticObjective, 8)],
                        ["analytic reference", fmt(V.mma.referenceObjective, 8)],
                        ["objective gap", fmt(V.mma.objectiveGap, 2)],
                        ["max constraint violation", fmt(V.mma.maxConstraintViolation, 2)],
                        ["KKT residual", fmt(V.mma.kktResidual, 2)],
                        ["iterations / converged", `${V.mma.iterations} / ${V.mma.converged ? "yes" : "no"}`],
                        ["dual Newton vs SolveDIP gap", fmt(V.mma.dipVsDualObjectiveGap, 2)],
                        ["concave sqrt test rel. error", fmt(V.mma.concaveTestRelError, 2)],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
                <Panel title="Revolve checkpointing" subtitle="include/mdo/revolve.hpp">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["N steps / S slots", `${V.revolve.nsteps} / ${V.revolve.slots}`],
                        ["peak checkpoint slots", `${V.revolve.peakSlots} (${V.revolve.memoryRespected ? "within capacity" : "exceeded"})`],
                        ["forward executions", `${V.revolve.forwardExecutions} (predicted ${V.revolve.predictedExecutions})`],
                        ["recomputed steps", String(V.revolve.recomputedSteps)],
                        ["checkpoints / restores / actions", `${V.revolve.checkpoints} / ${V.revolve.restores} / ${V.revolve.actions}`],
                        ["adjoint vs full-storage", fmt(V.revolve.adjointMaxRelError, 2)],
                        ["adjoint vs finite difference", fmt(V.revolve.fdAdjointMaxRelError, 2)],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
                <Panel title="Generalized FOSM" subtitle="include/mdo/fosm.hpp · no Monte Carlo">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["design cells / ξ dimension", String(V.fosm.designSize)],
                        ["κ robust multiplier", fmt(V.fosm.kappa, 2)],
                        ["μ / σ / Φ", `${fmt(V.fosm.meanValue, 4)} / ${fmt(V.fosm.exactSigma, 4)} / ${fmt(V.fosm.robustValue, 4)}`],
                        [`Monte Carlo σ (${V.fosm.samples} samples)`, fmt(V.fosm.mcSigma, 4)],
                        ["σ relative difference", fmt(V.fosm.sigmaRelError, 3)],
                        ["dg/dξ vs FD", fmt(V.fosm.xiDerivativeError, 3)],
                        ["dσ/dγ vs exact FD", fmt(V.fosm.maxSigmaSensitivityError, 3)],
                        ["MC time / 5-solve exact", `${fmt(V.fosm.mcSeconds, 3)} s / ${fmt(V.fosm.exactSeconds, 4)} s`],
                        ["measured speed-up", `${fmt(V.fosm.speedup, 0)}×`],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
                <Panel title="Hybrid search + topology driver" subtitle="include/mdo/hybrid.hpp · topopt.hpp">
                  <table className="w-full text-[11px]">
                    <tbody>
                      {[
                        ["Rastrigin global / final", `${fmt(V.hybrid.globalBest, 3)} / ${fmt(V.hybrid.finalBest, 3)}`],
                        ["global optimum reached", V.hybrid.reachedGlobalOptimum ? "yes" : "no"],
                        ["evaluations global / local", `${V.hybrid.globalEvaluations} / ${V.hybrid.localEvaluations}`],
                        ["shape J start / final", `${fmt(V.hybrid.shapeObjectiveStart, 3)} / ${fmt(V.hybrid.shapeObjectiveFinal, 3)}`],
                        ["topopt iterations", String(V.topopt.iterations)],
                        ["J start / final", `${fmt(V.topopt.objectiveInitial, 3)} / ${fmt(V.topopt.objectiveFinal, 3)}`],
                        ["final volume / grayness", `${fmt(V.topopt.volumeFinal, 3)} / ${fmt(V.topopt.graynessFinal, 3)}`],
                        ["3-field chain vs FD", fmt(V.topopt.chainGradientRelError, 2)],
                        ["driver wall time", `${fmt(V.topopt.elapsedSeconds, 2)} s`],
                      ].map(([a, b]) => (
                        <tr key={a} className="border-t border-white/5">
                          <td className="py-1.5 text-slate-400">{a}</td>
                          <td className="py-1.5 text-right font-mono text-cyan-200">{b}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </Panel>
              </div>

              <Panel title="Reproducibility" subtitle="standalone C++20, no external dependencies">
                <div className="grid gap-3 md:grid-cols-2">
                  <div className="space-y-2">
                    {formula(<>cd backend/optimization &amp;&amp; make -j</>)}
                    {formula(<>./build/verify &nbsp;# writes verification.json + reference_topopt.json</>)}
                    {formula(<>g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src/verify.cpp</>)}
                  </div>
                  <div className="text-[11px] leading-relaxed text-slate-400">
                    <p>Headers: <span className="font-mono text-cyan-300">linalg · fvm · thermofluid · mma · revolve · fosm · hybrid · topopt</span>.</p>
                    <p className="mt-2">The cockpit re-implements the same chain in TypeScript for interactive runs, while this report is emitted by the C++ binary ({V.compiler}, {V.totalSeconds.toFixed(1)} s wall time on the sandbox).</p>
                    <p className="mt-2 text-slate-500">Monte Carlo evidence: {V.fosm.samples.toLocaleString()} samples, σ relative difference {fmt(V.fosm.sigmaRelError * 100, 2)} %, exact sensitivity {fmt(V.fosm.speedup, 0)}× cheaper.</p>
                  </div>
                </div>
              </Panel>
            </div>
          ) : null}

          <footer className="mt-6 flex flex-wrap items-center justify-between gap-2 border-t border-white/5 pt-3 font-mono text-[9.5px] uppercase tracking-[0.16em] text-slate-600">
            <span>phase 4 · multiphysics mdo platform · backend/optimization</span>
            <span>{busy ? `running ${busy} · ${elapsed}s` : "engine idle"}</span>
          </footer>
        </main>
      </div>
    </div>
  );
}
