import { jsonError, jsonOk, readJson } from "@/lib/http";
import { runRaw } from "@/lib/native";
import { buildNativeConfig, parseMeshParams, parseSolverParams } from "@/lib/params";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * Well-balancedness / consistency certification: runs the discrete residual
 * operator on a uniform state and returns the residual norms. A consistent
 * FVM/SPH operator must reproduce a free-stream state exactly (zero residual).
 */
export async function POST(request: Request) {
  try {
    const body = await readJson(request);
    const meshBody = (body.mesh ?? {}) as Record<string, unknown>;
    const presetId = typeof meshBody.preset === "string" ? meshBody.preset : "cylinder";
    const meshParams = parseMeshParams(meshBody, presetId as never);
    const solverParams = parseSolverParams(body.solver ?? {}, meshParams.preset);
    const uniform = {
      ...solverParams,
      ic: "shock_obstacle" as const,
      mach: 0,
      snapshots: 0,
      interpolation: false,
      sensitivity: false,
    };
    const config = buildNativeConfig(meshParams, uniform).replace("op=solve", "op=probe");
    const raw = await runRaw(config, 120_000);
    const residual = raw as unknown as {
      cells: number;
      maxRhoRes: number;
      maxMomRes: number;
      maxERes: number;
      nonZeroMassRes?: number;
      dt: number;
      stateRange?: number[];
    };
    const tolerance = 1e-9;
    return jsonOk({
      check: "uniform-state free-stream preservation",
      passed:
        residual.maxRhoRes <= tolerance &&
        residual.maxMomRes <= tolerance &&
        residual.maxERes <= tolerance,
      tolerance,
      residual: {
        cells: residual.cells,
        maxMassResidual: residual.maxRhoRes,
        maxMomentumResidual: residual.maxMomRes,
        maxEnergyResidual: residual.maxERes,
        nonzeroMassResidualCells: residual.nonZeroMassRes ?? null,
      },
      meshParams,
      solverParams: uniform,
      note:
        "The Eulerian-SPH consistent wall closure and the interior HLLC fluxes must both annihilate a uniform state; any non-zero value indicates a topology or boundary inconsistency.",
    });
  } catch (error) {
    return jsonError(error);
  }
}
