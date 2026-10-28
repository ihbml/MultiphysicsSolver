"use client";

import { useMemo, useState } from "react";

import { ColorLegend, MeshCanvas } from "./mesh-canvas";
import type {
  MeshGeometry,
  MeshMeta,
  MeshParams,
  MeshResponse,
  ObstacleSpec,
  PresetId,
  RunSummary,
  SimulationResponse,
  SolverParams,
} from "@/lib/types";

export interface PresetPayload {
  id: PresetId;
  label: string;
  description: string;
  papers: string;
  mesh: MeshParams;
  solver: SolverParams;
}

type Busy = "mesh" | "solve" | "diagnostics" | null;

interface DiagnosticsPayload {
  passed: boolean;
  tolerance: number;
  residual: {
    cells: number;
    maxMassResidual: number;
    maxMomentumResidual: number;
    maxEnergyResidual: number;
    nonzeroMassResidualCells: number | null;
  };
  note: string;
}

const FIELD_OPTIONS: { id: string; label: string; unit: string; solverOnly: boolean }[] = [
  { id: "Ma", label: "Mach number", unit: "", solverOnly: true },
  { id: "rho", label: "Density ρ", unit: "", solverOnly: true },
  { id: "p", label: "Pressure p", unit: "", solverOnly: true },
  { id: "vel", label: "Velocity |u|", unit: "", solverOnly: true },
  { id: "quality", label: "Cell quality r_in/r_out", unit: "", solverOnly: false },
  { id: "consistency", label: "SPH consistency deficiency", unit: "", solverOnly: false },
  { id: "area", label: "Cell volume", unit: "a²", solverOnly: false },
];

async function requestJson<T>(input: string, init?: RequestInit): Promise<T> {
  const response = await fetch(input, {
    ...init,
    headers: { "content-type": "application/json", ...(init?.headers ?? {}) },
  });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) {
    const message =
      payload && typeof payload === "object" && "error" in payload
        ? String((payload as { error: unknown }).error)
        : `request failed with ${response.status}`;
    throw new Error(message);
  }
  return payload as T;
}

function fmt(value: number | undefined | null, digits = 4): string {
  if (value === undefined || value === null || !Number.isFinite(value)) return "—";
  if (value !== 0 && (Math.abs(value) < 1e-3 || Math.abs(value) >= 1e5)) return value.toExponential(3);
  return value.toFixed(digits);
}

function Stat({ label, value, hint }: { label: string; value: string; hint?: string }) {
  return (
    <div className="panel-inset px-3 py-2">
      <div className="label">{label}</div>
      <div className="mono mt-1 text-[15px] text-[#e6f0f0]">{value}</div>
      {hint ? <div className="mono mt-0.5 text-[10px] text-[#5f777a]">{hint}</div> : null}
    </div>
  );
}

function NumberField({
  label,
  value,
  onChange,
  step = 1,
  min,
  max,
  suffix,
}: {
  label: string;
  value: number;
  onChange: (value: number) => void;
  step?: number;
  min?: number;
  max?: number;
  suffix?: string;
}) {
  return (
    <label className="flex flex-col gap-1">
      <span className="label">{label}</span>
      <div className="flex items-center gap-1">
        <input
          className="field"
          type="number"
          value={Number.isFinite(value) ? value : 0}
          step={step}
          min={min}
          max={max}
          onChange={(event) => {
            const next = Number(event.target.value);
            if (Number.isFinite(next)) onChange(next);
          }}
        />
        {suffix ? <span className="mono text-[10px] text-[#5f777a]">{suffix}</span> : null}
      </div>
    </label>
  );
}

function Sparkline({
  series,
  label,
  unit,
  color,
}: {
  series: number[];
  label: string;
  unit: string;
  color: string;
}) {
  const safe = series.filter((v) => Number.isFinite(v));
  if (safe.length < 2) {
    return (
      <div className="panel-inset p-3">
        <div className="label">{label}</div>
        <div className="mono mt-2 text-[11px] text-[#5f777a]">no time series</div>
      </div>
    );
  }
  const lo = Math.min(...safe);
  const hi = Math.max(...safe);
  const span = Math.abs(hi - lo) < 1e-12 ? 1 : hi - lo;
  const points = safe
    .map((v, i) => {
      const x = (i / (safe.length - 1)) * 100;
      const y = 30 - ((v - lo) / span) * 26 - 2;
      return `${x.toFixed(2)},${y.toFixed(2)}`;
    })
    .join(" ");
  return (
    <div className="panel-inset p-3">
      <div className="flex items-baseline justify-between">
        <span className="label">{label}</span>
        <span className="mono text-[11px] text-[#a9c6c4]">
          {fmt(safe[safe.length - 1], 3)} {unit}
        </span>
      </div>
      <svg viewBox="0 0 100 30" preserveAspectRatio="none" className="mt-2 h-10 w-full">
        <polyline points={points} fill="none" stroke={color} strokeWidth="1.2" vectorEffect="non-scaling-stroke" />
      </svg>
      <div className="mono flex justify-between text-[10px] text-[#5f777a]">
        <span>{fmt(lo, 3)}</span>
        <span>{fmt(hi, 3)}</span>
      </div>
    </div>
  );
}

export function Workbench({
  presets,
  initialRuns,
}: {
  presets: PresetPayload[];
  initialRuns: RunSummary[];
}) {
  const [presetId, setPresetId] = useState<PresetId>(presets[0]?.id ?? "cylinder");
  const [meshParams, setMeshParams] = useState<MeshParams>(presets[0].mesh);
  const [solverParams, setSolverParams] = useState<SolverParams>(presets[0].solver);
  const [busy, setBusy] = useState<Busy>(null);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<SimulationResponse | null>(null);
  const [meshOnly, setMeshOnly] = useState<MeshResponse | null>(null);
  const [field, setField] = useState<string>("quality");
  const [snapIndex, setSnapIndex] = useState(0);
  const [runs, setRuns] = useState<RunSummary[]>(initialRuns);
  const [diagnostics, setDiagnostics] = useState<DiagnosticsPayload | null>(null);

  const preset = presets.find((item) => item.id === presetId) ?? presets[0];

  const applyPreset = (id: PresetId) => {
    const next = presets.find((item) => item.id === id);
    if (!next) return;
    setPresetId(id);
    setMeshParams(structuredClone(next.mesh));
    setSolverParams(structuredClone(next.solver));
    setResult(null);
    setMeshOnly(null);
    setDiagnostics(null);
    setError(null);
    setSnapIndex(0);
    setField("quality");
  };

  const patchMesh = (patch: Partial<MeshParams>) => setMeshParams((prev) => ({ ...prev, ...patch }));
  const patchDomain = (patch: Partial<MeshParams["domain"]>) =>
    setMeshParams((prev) => ({ ...prev, domain: { ...prev.domain, ...patch } }));
  const patchSolver = (patch: Partial<SolverParams>) => setSolverParams((prev) => ({ ...prev, ...patch }));

  const patchObstacle = (index: number, patch: Partial<ObstacleSpec>) =>
    setMeshParams((prev) => ({
      ...prev,
      obstacles: prev.obstacles.map((item, i) => (i === index ? { ...item, ...patch } : item)),
    }));

  const setObstacleType = (index: number, type: "circle" | "rect") =>
    setMeshParams((prev) => ({
      ...prev,
      obstacles: prev.obstacles.map((item, i) => {
        if (i !== index) return item;
        if (type === "circle") {
          return { type, x: item.x ?? 0.35, y: item.y ?? 0.5, r: item.r ?? 0.12 };
        }
        return { type, x0: item.x0 ?? 0.45, y0: item.y0 ?? 0.3, x1: item.x1 ?? 0.62, y1: item.y1 ?? 0.55 };
      }),
    }));

  const geometry: MeshGeometry | null = result?.mesh.geometry ?? meshOnly?.mesh.geometry ?? null;
  const meta: MeshMeta | null = result?.mesh.meta ?? meshOnly?.mesh.meta ?? null;
  const stats = result?.mesh.stats ?? meshOnly?.mesh.stats ?? null;
  const simulation = result?.simulation ?? null;

  const snapshot = simulation?.snapshots[Math.min(snapIndex, simulation.snapshots.length - 1)];

  const fieldValues = useMemo<number[] | null>(() => {
    if (!geometry) return null;
    const cells = geometry.cells;
    if (field === "quality") return cells.map((cell) => cell[3]);
    if (field === "consistency") return cells.map((cell) => cell[4]);
    if (field === "area") return cells.map((cell) => cell[2]);
    if (!snapshot) return null;
    if (field === "rho") return snapshot.rho;
    if (field === "p") return snapshot.p;
    if (field === "Ma") return snapshot.Ma;
    if (field === "vel") return snapshot.u.map((u, i) => Math.hypot(u, snapshot.v[i] ?? 0));
    return null;
  }, [geometry, field, snapshot]);

  const range = useMemo<[number, number]>(() => {
    if (!fieldValues || fieldValues.length === 0) return [0, 1];
    let lo = Number.POSITIVE_INFINITY;
    let hi = Number.NEGATIVE_INFINITY;
    for (const value of fieldValues) {
      if (!Number.isFinite(value)) continue;
      if (value < lo) lo = value;
      if (value > hi) hi = value;
    }
    if (!Number.isFinite(lo) || !Number.isFinite(hi)) return [0, 1];
    if (hi - lo < 1e-12) return [lo - 0.5, hi + 0.5];
    return [lo, hi];
  }, [fieldValues]);

  const refreshRuns = async () => {
    try {
      const data = await requestJson<{ simulations: RunSummary[] }>("/api/simulations?limit=8");
      setRuns(data.simulations);
    } catch {
      // history refresh is non critical
    }
  };

  const buildMesh = async () => {
    setBusy("mesh");
    setError(null);
    setDiagnostics(null);
    try {
      const data = await requestJson<MeshResponse>("/api/meshes", {
        method: "POST",
        body: JSON.stringify({ mesh: meshParams }),
      });
      setMeshOnly(data);
      setResult(null);
      setSnapIndex(0);
      setField((prev) => (prev === "Ma" || prev === "rho" || prev === "p" || prev === "vel" ? "quality" : prev));
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "mesh build failed");
    } finally {
      setBusy(null);
    }
  };

  const runSolver = async () => {
    setBusy("solve");
    setError(null);
    setDiagnostics(null);
    try {
      const data = await requestJson<SimulationResponse>("/api/simulations", {
        method: "POST",
        body: JSON.stringify({ mesh: meshParams, solver: solverParams, includeGeometry: true }),
      });
      setResult(data);
      setMeshOnly(null);
      setSnapIndex(Math.max(0, data.simulation.snapshots.length - 1));
      setField(solverParams.ic === "shock_obstacle" ? "Ma" : "rho");
      await refreshRuns();
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "solver run failed");
    } finally {
      setBusy(null);
    }
  };

  const loadRun = async (id: number) => {
    setBusy("solve");
    setError(null);
    try {
      const data = await requestJson<SimulationResponse>(`/api/simulations/${id}`);
      setResult(data);
      setMeshOnly(null);
      setSnapIndex(Math.max(0, (data.simulation?.snapshots?.length ?? 1) - 1));
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "could not load run");
    } finally {
      setBusy(null);
    }
  };

  const certify = async () => {
    setBusy("diagnostics");
    setError(null);
    try {
      const data = await requestJson<DiagnosticsPayload>("/api/diagnostics", {
        method: "POST",
        body: JSON.stringify({ mesh: meshParams, solver: solverParams }),
      });
      setDiagnostics(data);
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : "diagnostics failed");
    } finally {
      setBusy(null);
    }
  };

  const solverFieldOptions = FIELD_OPTIONS.filter((option) => !option.solverOnly || simulation);

  return (
    <div className="grid grid-cols-1 gap-4 xl:grid-cols-[382px_1fr]">
      {/* ------------------------------- controls ------------------------------- */}
      <div className="flex flex-col gap-3">
        <section className="panel p-4">
          <div className="flex items-center justify-between">
            <h2 className="mono text-[12px] tracking-[0.16em] text-[#b8f24a]">MESH SETUP</h2>
            <span className="chip">voronoi</span>
          </div>
          <div className="mt-3 flex flex-col gap-2">
            <label className="flex flex-col gap-1">
              <span className="label">Preset</span>
              <select className="field" value={presetId} onChange={(event) => applyPreset(event.target.value as PresetId)}>
                {presets.map((item) => (
                  <option key={item.id} value={item.id}>
                    {item.label}
                  </option>
                ))}
              </select>
            </label>
            <p className="mono text-[10.5px] leading-relaxed text-[#6d888b]">{preset.description}</p>
          </div>

          <div className="mt-3 grid grid-cols-2 gap-2">
            <NumberField label="x min" value={meshParams.domain.xmin} step={0.05} onChange={(v) => patchDomain({ xmin: v })} />
            <NumberField label="x max" value={meshParams.domain.xmax} step={0.05} onChange={(v) => patchDomain({ xmax: v })} />
            <NumberField label="y min" value={meshParams.domain.ymin} step={0.05} onChange={(v) => patchDomain({ ymin: v })} />
            <NumberField label="y max" value={meshParams.domain.ymax} step={0.05} onChange={(v) => patchDomain({ ymax: v })} />
          </div>

          <div className="mt-2 grid grid-cols-2 gap-2">
            <NumberField label="target cells" value={meshParams.target} step={100} min={150} max={20000} onChange={(v) => patchMesh({ target: Math.round(v) })} />
            <NumberField label="jitter" value={meshParams.jitter} step={0.02} min={0} max={0.45} onChange={(v) => patchMesh({ jitter: v })} />
            <NumberField label="relax (Lloyd)" value={meshParams.relax} step={1} min={0} max={8} onChange={(v) => patchMesh({ relax: Math.round(v) })} />
            <NumberField label="wall layer" value={meshParams.wallLayer} step={1} min={0} max={1} onChange={(v) => patchMesh({ wallLayer: Math.round(v) })} />
          </div>

          <div className="mt-3 border-t border-[#1d2b31] pt-3">
            <div className="flex items-center justify-between">
              <span className="label">Solid obstacles</span>
              <button
                type="button"
                className="btn btn-ghost !px-2 !py-1 text-[10px]"
                onClick={() => {
                  const next: ObstacleSpec = { type: "circle", x: 0.5, y: 0.5, r: 0.1 };
                  patchMesh({ obstacles: [...meshParams.obstacles, next].slice(0, 3) });
                }}
              >
                + add
              </button>
            </div>
            {meshParams.obstacles.length === 0 ? (
              <p className="mono mt-2 text-[10.5px] text-[#5f777a]">none — closed or open box only</p>
            ) : null}
            {meshParams.obstacles.map((obstacle, index) => (
              <div key={`${obstacle.type}-${index}`} className="panel-inset mt-2 p-2">
                <div className="flex items-center gap-2">
                  <select
                    className="field !py-1 !text-[11px]"
                    value={obstacle.type}
                    onChange={(event) => setObstacleType(index, event.target.value as "circle" | "rect")}
                  >
                    <option value="circle">circle</option>
                    <option value="rect">rect</option>
                  </select>
                  <button
                    type="button"
                    className="btn btn-ghost !px-2 !py-1 text-[10px] text-[#ff6b81]"
                    onClick={() => patchMesh({ obstacles: meshParams.obstacles.filter((_, i) => i !== index) })}
                  >
                    remove
                  </button>
                </div>
                {obstacle.type === "circle" ? (
                  <div className="mt-2 grid grid-cols-3 gap-2">
                    <NumberField label="cx" value={obstacle.x ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { x: v })} />
                    <NumberField label="cy" value={obstacle.y ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { y: v })} />
                    <NumberField label="r" value={obstacle.r ?? 0.1} step={0.01} onChange={(v) => patchObstacle(index, { r: v })} />
                  </div>
                ) : (
                  <div className="mt-2 grid grid-cols-2 gap-2">
                    <NumberField label="x0" value={obstacle.x0 ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { x0: v })} />
                    <NumberField label="y0" value={obstacle.y0 ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { y0: v })} />
                    <NumberField label="x1" value={obstacle.x1 ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { x1: v })} />
                    <NumberField label="y1" value={obstacle.y1 ?? 0} step={0.02} onChange={(v) => patchObstacle(index, { y1: v })} />
                  </div>
                )}
              </div>
            ))}
          </div>

          <div className="mt-3 flex gap-2">
            <button type="button" className="btn btn-ghost flex-1" onClick={buildMesh} disabled={busy !== null}>
              {busy === "mesh" ? "meshing…" : "Build mesh"}
            </button>
            <button type="button" className="btn btn-ghost flex-1" onClick={certify} disabled={busy !== null}>
              {busy === "diagnostics" ? "checking…" : "Certify"}
            </button>
          </div>
          <p className="mono mt-2 text-[10px] text-[#5f777a]">
            Seed cloud → Voronoi dual + Lloyd relaxation → conforming polygon cells, CSR graph, SPH
            consistency diagnostics.
          </p>
        </section>

        <section className="panel p-4">
          <div className="flex items-center justify-between">
            <h2 className="mono text-[12px] tracking-[0.16em] text-[#45e0d0]">FVM SOLVER</h2>
            <span className="chip">hllc · rk2</span>
          </div>
          <div className="mt-3 grid grid-cols-2 gap-2">
            <NumberField label="steps" value={solverParams.steps} step={20} min={5} max={1500} onChange={(v) => patchSolver({ steps: Math.round(v) })} />
            <NumberField label="cfl" value={solverParams.cfl} step={0.05} min={0.05} max={0.8} onChange={(v) => patchSolver({ cfl: v })} />
            <NumberField label="mach" value={solverParams.mach} step={0.1} min={0} max={8} onChange={(v) => patchSolver({ mach: v })} />
            <NumberField label="snapshots" value={solverParams.snapshots} step={1} min={0} max={8} onChange={(v) => patchSolver({ snapshots: Math.round(v) })} />
            <NumberField label="kappa (SPH)" value={solverParams.kappa} step={0.1} min={0} max={3} onChange={(v) => patchSolver({ kappa: v })} />
            <NumberField label="gamma" value={solverParams.gamma} step={0.05} min={1.05} max={1.7} onChange={(v) => patchSolver({ gamma: v })} />
          </div>
          <div className="mt-2 grid grid-cols-2 gap-2">
            <label className="flex flex-col gap-1">
              <span className="label">initial condition</span>
              <select className="field" value={solverParams.ic} onChange={(event) => patchSolver({ ic: event.target.value as SolverParams["ic"] })}>
                <option value="shock_obstacle">freestream</option>
                <option value="sod">Sod tube</option>
                <option value="explosion">blast</option>
                <option value="vortex">isentropic vortex</option>
              </select>
            </label>
            <label className="flex flex-col gap-1">
              <span className="label">order</span>
              <select className="field" value={solverParams.order} onChange={(event) => patchSolver({ order: Number(event.target.value) === 2 ? 2 : 1 })}>
                <option value={1}>1 · first order</option>
                <option value={2}>2 · MUSCL (Barth-Jespersen)</option>
              </select>
            </label>
            <label className="flex flex-col gap-1">
              <span className="label">wall closure</span>
              <select
                className="field"
                value={solverParams.wallClosure}
                onChange={(event) => patchSolver({ wallClosure: event.target.value === "mirror" ? "mirror" : "sph" })}
              >
                <option value="sph">Eulerian-SPH consistent</option>
                <option value="mirror">plain mirror</option>
              </select>
            </label>
            <label className="flex flex-col gap-1">
              <span className="label">inflow side</span>
              <select className="field" value={solverParams.bcLeft} onChange={(event) => patchSolver({ bcLeft: event.target.value as SolverParams["bcLeft"] })}>
                <option value="inlet">inlet</option>
                <option value="outlet">outlet</option>
                <option value="wall">wall</option>
                <option value="farfield">farfield</option>
              </select>
            </label>
          </div>
          <div className="mt-3 flex flex-wrap gap-3">
            <label className="mono flex items-center gap-2 text-[11px] text-[#a9c6c4]">
              <input
                type="checkbox"
                checked={solverParams.interpolation}
                onChange={(event) => patchSolver({ interpolation: event.target.checked })}
              />
              particle↔mesh interpolation
            </label>
            <label className="mono flex items-center gap-2 text-[11px] text-[#a9c6c4]">
              <input
                type="checkbox"
                checked={solverParams.sensitivity}
                onChange={(event) => patchSolver({ sensitivity: event.target.checked })}
              />
              sensitivities dJ/dθ
            </label>
          </div>
          <button type="button" className="btn btn-primary mt-3 w-full" onClick={runSolver} disabled={busy !== null}>
            {busy === "solve" ? "solving…" : "Run solver"}
          </button>
          {error ? (
            <div className="mono mt-3 rounded-lg border border-[#ff6b81]/40 bg-[#2a1015] px-3 py-2 text-[11px] text-[#ff9aa8]">{error}</div>
          ) : null}
        </section>
      </div>

      {/* ------------------------------- results -------------------------------- */}
      <div className="flex flex-col gap-3">
        <section className="panel p-4">
          <div className="flex flex-wrap items-end justify-between gap-3">
            <div>
              <h2 className="mono text-[12px] tracking-[0.16em] text-[#e6f0f0]">
                {simulation ? `RUN #${simulation.id} · ${simulation.params.solver.ic.toUpperCase()}` : meta ? "MESH PREVIEW" : "RESULTS"}
              </h2>
              <p className="mono mt-1 text-[10.5px] text-[#6d888b]">
                {meta
                  ? `${meta.cellCount} cells · ${meta.faceCount} faces · ${meta.obstacleFaces} solid faces · spacing ${meta.spacing.toExponential(3)}`
                  : "build a mesh or run the solver to populate the viewer"}
              </p>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              {result ? <span className="chip">cached mesh {result.mesh.cached ? "hit" : "store"}</span> : null}
              {simulation ? <span className="chip">native {simulation.stats.solveMs.toFixed(1)} ms</span> : null}
              {result ? <span className="chip">round-trip {result.timings.endToEndMs} ms</span> : null}
            </div>
          </div>

          <div className="mt-3 flex flex-wrap items-center gap-2">
            {solverFieldOptions.map((option) => (
              <button
                key={option.id}
                type="button"
                className={`btn !px-2.5 !py-1 text-[10.5px] ${field === option.id ? "border-[#b8f24a]/70 text-[#d7ffa1]" : "btn-ghost"}`}
                onClick={() => setField(option.id)}
              >
                {option.label}
              </button>
            ))}
            <div className="ml-auto flex items-center gap-2">
              <button
                type="button"
                className="btn btn-ghost !px-2 !py-1 text-[11px]"
                disabled={!simulation || simulation.snapshots.length < 2}
                onClick={() => setSnapIndex((i) => Math.max(0, i - 1))}
              >
                ◀
              </button>
              <input
                type="range"
                min={0}
                max={Math.max(0, (simulation?.snapshots.length ?? 1) - 1)}
                value={Math.min(snapIndex, Math.max(0, (simulation?.snapshots.length ?? 1) - 1))}
                onChange={(event) => setSnapIndex(Number(event.target.value))}
                disabled={!simulation || simulation.snapshots.length < 2}
                className="w-28"
              />
              <button
                type="button"
                className="btn btn-ghost !px-2 !py-1 text-[11px]"
                disabled={!simulation || snapIndex >= (simulation?.snapshots.length ?? 1) - 1}
                onClick={() => setSnapIndex((i) => Math.min((simulation?.snapshots.length ?? 1) - 1, i + 1))}
              >
                ▶
              </button>
              <span className="mono text-[10px] text-[#6d888b]">
                {snapshot ? `step ${snapshot.step} · t=${snapshot.time.toFixed(4)}` : "mesh only"}
              </span>
            </div>
          </div>

          <div className="mt-3">
            <MeshCanvas
              geometry={geometry}
              domain={meta?.domain ?? meshParams.domain}
              values={fieldValues}
              range={range}
              label={`${FIELD_OPTIONS.find((option) => option.id === field)?.label ?? field}`}
            />
          </div>
          <div className="mt-2">
            <ColorLegend
              range={range}
              label={FIELD_OPTIONS.find((option) => option.id === field)?.label ?? field}
              unit={FIELD_OPTIONS.find((option) => option.id === field)?.unit}
            />
          </div>
        </section>

        {simulation ? (
          <>
            <section className="panel p-4">
              <div className="flex items-center justify-between">
                <h3 className="mono text-[12px] tracking-[0.16em] text-[#b8f24a]">SOLVER TELEMETRY</h3>
                <span className="chip">
                  {(simulation.stats.faceEvalsPerSecond / 1e6).toFixed(1)} Mface-evals/s
                </span>
              </div>
              <div className="mt-3 grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-4">
                <Stat label="final time" value={fmt(simulation.stats.finalTime, 4)} hint={`dt̄ ${simulation.stats.avgDt.toExponential(3)}`} />
                <Stat label="steps" value={`${simulation.meta.steps}`} hint={`${simulation.meta.scheme} · order ${simulation.meta.order}`} />
                <Stat label="drag F_x" value={fmt(simulation.stats.drag, 4)} hint={`lift ${fmt(simulation.stats.lift, 4)}`} />
                <Stat label="max Mach" value={fmt(snapshot?.maxMach, 3)} hint={`ρ ∈ [${fmt(snapshot?.minRho, 3)}, ${fmt(snapshot?.maxRho, 3)}]`} />
                <Stat label="total mass" value={fmt(simulation.stats.totalMass, 5)} />
                <Stat label="total energy" value={fmt(simulation.stats.totalEnergy, 5)} />
                <Stat
                  label="consistency χ"
                  value={fmt(simulation.stats.consistencyDeficiencyMean, 4)}
                  hint={`max ${fmt(simulation.stats.consistencyDeficiencyMax, 4)}`}
                />
                <Stat label="native solve" value={`${simulation.stats.solveMs.toFixed(1)} ms`} hint={`${simulation.computeMs} ms round-trip`} />
              </div>
              <div className="mt-3 grid grid-cols-1 gap-2 sm:grid-cols-2 lg:grid-cols-3">
                <Sparkline series={simulation.snapshots.map((item) => item.maxMach)} label="max Mach" unit="" color="#b8f24a" />
                <Sparkline series={simulation.snapshots.map((item) => item.drag)} label="drag F_x" unit="" color="#45e0d0" />
                <Sparkline series={simulation.snapshots.map((item) => item.mass)} label="mass" unit="" color="#ffb545" />
              </div>
            </section>

            <section className="panel p-4">
              <h3 className="mono text-[12px] tracking-[0.16em] text-[#45e0d0]">NUMERICS &amp; SENSITIVITIES</h3>
              <div className="mt-3 grid grid-cols-1 gap-3 lg:grid-cols-2">
                <table className="data">
                  <tbody>
                    <tr>
                      <td>P2M/M2P non-zeros</td>
                      <td className="text-right">{simulation.interpolation.nnz.toLocaleString()}</td>
                    </tr>
                    <tr>
                      <td>mean stencil</td>
                      <td className="text-right">{fmt(simulation.interpolation.meanStencil, 3)}</td>
                    </tr>
                    <tr>
                      <td>operator sparsity</td>
                      <td className="text-right">{fmt(simulation.interpolation.sparsity, 4)}</td>
                    </tr>
                    <tr>
                      <td>mass conservation error</td>
                      <td className="text-right text-[#b8f24a]">{simulation.interpolation.conservationError.toExponential(2)}</td>
                    </tr>
                    <tr>
                      <td>L2 / L∞ transfer error</td>
                      <td className="text-right">
                        {simulation.interpolation.l2Error.toExponential(2)} / {simulation.interpolation.linfError.toExponential(2)}
                      </td>
                    </tr>
                    <tr>
                      <td>transfer time</td>
                      <td className="text-right">{simulation.interpolation.timeMs.toFixed(2)} ms</td>
                    </tr>
                  </tbody>
                </table>
                <table className="data">
                  <tbody>
                    <tr>
                      <td>dJ/dMach (goal: F_x + K)</td>
                      <td className="text-right">{fmt(simulation.stats.gradMach, 5)}</td>
                    </tr>
                    <tr>
                      <td>dJ/dκ (boundary closure)</td>
                      <td className="text-right">{fmt(simulation.stats.gradKappa, 5)}</td>
                    </tr>
                    <tr>
                      <td>perturbed runs</td>
                      <td className="text-right">{simulation.stats.sensitivityRuns}</td>
                    </tr>
                    <tr>
                      <td>sensitivity cost</td>
                      <td className="text-right">{simulation.stats.sensitivityMs.toFixed(1)} ms</td>
                    </tr>
                    <tr>
                      <td>surface faces</td>
                      <td className="text-right">{simulation.surface.length}</td>
                    </tr>
                    <tr>
                      <td>p range on solid</td>
                      <td className="text-right">
                        {simulation.surface.length > 0
                          ? `${fmt(Math.min(...simulation.surface.map((face) => face[5])), 3)} → ${fmt(
                              Math.max(...simulation.surface.map((face) => face[5])),
                              3,
                            )}`
                          : "—"}
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </section>
          </>
        ) : null}

        {meta && stats ? (
          <section className="panel p-4">
            <h3 className="mono text-[12px] tracking-[0.16em] text-[#e6f0f0]">MESH QUALITY AUDIT</h3>
            <div className="mt-3 grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-4">
              <Stat label="cells / faces" value={`${meta.cellCount} / ${meta.faceCount}`} hint={`${meta.interiorFaces} interior`} />
              <Stat label="min quality" value={fmt(stats.minQuality, 3)} hint={`mean ${fmt(stats.meanQuality, 3)}`} />
              <Stat label="coverage" value={`${(stats.coverage * 100).toFixed(2)} %`} hint="Σ cell volume / fluid domain" />
              <Stat label="mean stencil" value={fmt(stats.meanStencil, 2)} hint="faces per cell (CSR)" />
              <Stat label="χ consistency" value={fmt(stats.meanConsistencyDeficiency, 4)} hint={`max ${fmt(stats.maxConsistencyDeficiency, 4)}`} />
              <Stat label="solid faces" value={`${meta.obstacleFaces}`} hint={`${meta.boundaryFaces} boundary faces`} />
              <Stat label="seeds" value={`${meta.seedCount}`} hint={`relax ${meta.relaxIterations}`} />
              <Stat label="mesh build" value={`${meta.buildMs.toFixed(1)} ms`} hint={`${meta.builder.split(",")[0]}`} />
            </div>
          </section>
        ) : null}

        {diagnostics ? (
          <section className="panel p-4">
            <div className="flex items-center justify-between">
              <h3 className="mono text-[12px] tracking-[0.16em] text-[#e6f0f0]">FREE-STREAM PRESERVATION</h3>
              <span
                className={`chip ${diagnostics.passed ? "!border-[#b8f24a]/60 !text-[#d7ffa1]" : "!border-[#ff6b81]/60 !text-[#ff9aa8]"}`}
              >
                {diagnostics.passed ? "PASS" : "FAIL"}
              </span>
            </div>
            <table className="data mt-2">
              <tbody>
                <tr>
                  <td>cells audited</td>
                  <td className="text-right">{diagnostics.residual.cells}</td>
                </tr>
                <tr>
                  <td>‖mass residual/A‖∞</td>
                  <td className="text-right">{diagnostics.residual.maxMassResidual.toExponential(3)}</td>
                </tr>
                <tr>
                  <td>‖momentum residual/A‖∞</td>
                  <td className="text-right">{diagnostics.residual.maxMomentumResidual.toExponential(3)}</td>
                </tr>
                <tr>
                  <td>‖energy residual/A‖∞</td>
                  <td className="text-right">{diagnostics.residual.maxEnergyResidual.toExponential(3)}</td>
                </tr>
              </tbody>
            </table>
            <p className="mono mt-2 text-[10px] leading-relaxed text-[#6d888b]">{diagnostics.note}</p>
          </section>
        ) : null}

        <section className="panel p-4">
          <div className="flex items-center justify-between">
            <h3 className="mono text-[12px] tracking-[0.16em] text-[#e6f0f0]">RECENT RUNS</h3>
            <button type="button" className="btn btn-ghost !px-2 !py-1 text-[10px]" onClick={refreshRuns}>
              refresh
            </button>
          </div>
          {runs.length === 0 ? (
            <p className="mono mt-2 text-[11px] text-[#5f777a]">no stored runs yet</p>
          ) : (
            <div className="mt-2 overflow-x-auto scroll-thin">
              <table className="data">
                <thead>
                  <tr>
                    <th>id</th>
                    <th>case</th>
                    <th>cells</th>
                    <th>steps</th>
                    <th>drag</th>
                    <th>native ms</th>
                    <th />
                  </tr>
                </thead>
                <tbody>
                  {runs.map((run) => (
                    <tr key={run.id}>
                      <td>#{run.id}</td>
                      <td>{run.params?.mesh.preset ?? run.meta.ic}</td>
                      <td>{run.meta.cellCount}</td>
                      <td>{run.meta.steps}</td>
                      <td>{fmt(run.stats?.drag, 3)}</td>
                      <td>{run.stats?.solveMs?.toFixed(1) ?? "—"}</td>
                      <td className="text-right">
                        <button type="button" className="btn btn-ghost !px-2 !py-0.5 text-[10px]" onClick={() => loadRun(run.id)}>
                          load
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </section>
      </div>
    </div>
  );
}
