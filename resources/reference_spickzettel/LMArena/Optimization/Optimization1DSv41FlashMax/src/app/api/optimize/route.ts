import { db } from "@/db";
import { optimizationRuns } from "@/db/schema";
import { runTopopt, type TopoptOptions } from "@/lib/engine";
import { planRevolve, runFosm, runHybrid, type FosmParams, type HybridParams } from "@/lib/modules";

export const dynamic = "force-dynamic";

const clamp = (v: unknown, lo: number, hi: number, fallback: number) => {
  const n = typeof v === "number" && Number.isFinite(v) ? v : fallback;
  return Math.min(hi, Math.max(lo, n));
};

type Kind = "topopt" | "fosm" | "revolve" | "hybrid";

export async function POST(request: Request) {
  let body: Record<string, unknown>;
  try {
    body = (await request.json()) as Record<string, unknown>;
  } catch {
    return Response.json({ error: "invalid JSON body" }, { status: 400 });
  }
  const kind = String(body.kind ?? "") as Kind;
  const p = (body.params ?? {}) as Record<string, unknown>;
  const started = Date.now();
  try {
    let title = "";
    let metrics: Record<string, unknown> = {};
    let result: unknown;
    let config: Record<string, unknown>;

    if (kind === "topopt") {
      const levels = Math.round(clamp(p.betaLevels, 2, 6, 5));
      const betas = [1, 2, 4, 8, 16, 32].slice(0, levels);
      const opts: TopoptOptions = {
        nx: Math.round(clamp(p.nx, 14, 40, 26)),
        ny: Math.round(clamp(p.ny, 10, 26, 16)),
        rmin: clamp(p.rmin, 1.5, 6, 2.8),
        volFrac: clamp(p.volFrac, 0.2, 0.8, 0.45),
        tCap: clamp(p.tCap, 0.8, 12, 2.6),
        betas,
        iterationsPerBeta: Math.round(clamp(p.iterationsPerBeta, 4, 24, 12)),
        wHydraulic: clamp(p.wHydraulic, 0, 0.3, 0.03),
      };
      const run = runTopopt(opts);
      result = run;
      config = { ...opts };
      metrics = {
        objective: run.objective,
        volume: run.volume,
        grayness: run.grayness,
        peakT: run.peakT,
        ksT: run.ksT,
        dissipation: run.dissipation,
        iterations: run.iterations,
        feasible: run.feasible,
      };
      title = `Thermo-fluid SIMP/RAMP · ${opts.nx}×${opts.ny} · vf ${(opts.volFrac * 100).toFixed(0)}%`;
    } else if (kind === "fosm") {
      const params: FosmParams = {
        nx: Math.round(clamp(p.nx, 4, 8, 6)),
        ny: Math.round(clamp(p.ny, 4, 8, 5)),
        sigmaXi: clamp(p.sigmaXi, 0.02, 0.6, 0.22),
        corrLength: clamp(p.corrLength, 0.5, 8, 2.2),
        kappa: clamp(p.kappa, 0.5, 4, 2),
        rhoKs: clamp(p.rhoKs, 8, 60, 22),
        simpP: clamp(p.simpP, 1, 5, 3),
        sourceQ: 4e-3,
      };
      const run = runFosm(params);
      result = run;
      config = { ...params };
      metrics = { mean: run.mean, sigma: run.sigma, robust: run.robust, kappa: run.kappa, solves: run.solves };
      title = `FOSM robust · σξ ${params.sigmaXi} · ℓ ${params.corrLength}`;
    } else if (kind === "revolve") {
      const nsteps = Math.round(clamp(p.nsteps, 4, 160, 64));
      const slots = Math.round(clamp(p.slots, 0, 20, 8));
      const plan = planRevolve(nsteps, slots);
      result = plan;
      config = { nsteps, slots };
      metrics = {
        peakSlots: plan.peakSlots,
        checkpoints: plan.checkpoints,
        reconciles: plan.restores,
        forwardExecutions: plan.predictedForwardExecutions,
        zeroMemoryExecutions: plan.zeroMemoryExecutions,
        binomial: plan.binomial,
      };
      title = `Revolve · N ${nsteps} · S ${slots}`;
    } else if (kind === "hybrid") {
      const params: HybridParams = {
        nx: Math.round(clamp(p.nx, 12, 22, 16)),
        ny: Math.round(clamp(p.ny, 8, 14, 10)),
        blobs: Math.round(clamp(p.blobs, 1, 3, 2)),
        generations: Math.round(clamp(p.generations, 2, 12, 5)),
        islands: Math.round(clamp(p.islands, 2, 4, 3)),
        popPerIsland: Math.round(clamp(p.popPerIsland, 6, 14, 8)),
        psoSwarm: Math.round(clamp(p.psoSwarm, 6, 18, 10)),
        psoIterations: Math.round(clamp(p.psoIterations, 4, 20, 8)),
        usePso: p.usePso !== false,
      };
      const run = runHybrid(params);
      result = run;
      config = { ...params };
      metrics = {
        globalBest: run.globalBest,
        best: run.best,
        globalEvaluations: run.globalEvaluations,
        localEvaluations: run.localEvaluations,
        nmEvaluations: run.nmEvaluations,
        lbfgsEvaluations: run.lbfgsEvaluations,
      };
      title = `MIGA+PSO → NM/L-BFGS · ${params.blobs} obstacles`;
    } else {
      return Response.json({ error: "unknown optimisation kind" }, { status: 400 });
    }

    let id: number | null = null;
    try {
      const inserted = await db
        .insert(optimizationRuns)
        .values({
          kind,
          title,
          config,
          metrics,
          result: result as Record<string, unknown>,
          durationMs: Date.now() - started,
        })
        .returning({ id: optimizationRuns.id });
      id = inserted[0]?.id ?? null;
    } catch (dbError) {
      console.error("run persistence failed", dbError);
    }
    return Response.json({ id, kind, title, metrics, result, durationMs: Date.now() - started });
  } catch (error) {
    console.error("optimisation failed", error);
    return Response.json(
      { error: error instanceof Error ? error.message : "optimisation failed" },
      { status: 500 },
    );
  }
}
