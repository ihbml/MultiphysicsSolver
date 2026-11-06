import { jsonError, jsonOk, readJson } from "@/lib/http";
import { runSolve } from "@/lib/native";
import { assertWorkBudget, buildNativeConfig, parseMeshParams, parseSolverParams } from "@/lib/params";
import { listSimulations, persistMesh, persistSimulation } from "@/lib/service";
import type { MeshResult } from "@/lib/types";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";
export const maxDuration = 300;

export async function GET(request: Request) {
  try {
    const url = new URL(request.url);
    const limit = Math.min(50, Math.max(1, Number(url.searchParams.get("limit") ?? 12)));
    const rows = await listSimulations(limit);
    return jsonOk({ count: rows.length, simulations: rows });
  } catch (error) {
    return jsonError(error);
  }
}

/**
 * Runs the compressible graph-based FVM solver on a (deterministically
 * regenerated) Voronoi mesh. Body: { mesh?: MeshParams, solver?: SolverParams }.
 */
export async function POST(request: Request) {
  try {
    const body = await readJson(request);
    const meshBody = (body.mesh ?? {}) as Record<string, unknown>;
    const presetId = typeof meshBody.preset === "string" ? meshBody.preset : "cylinder";
    const meshParams = parseMeshParams(meshBody, presetId as never);
    const solverParams = parseSolverParams(body.solver ?? {}, meshParams.preset);
    assertWorkBudget(meshParams, solverParams);

    const started = Date.now();
    const nativeResult = await runSolve(buildNativeConfig(meshParams, solverParams));
    const meshResult: MeshResult = {
      meta: nativeResult.mesh.meta,
      stats: nativeResult.mesh.stats,
      geometry: nativeResult.mesh.geometry,
    };
    const { row: meshRow, cached } = await persistMesh(meshParams, meshResult);
    const simulation = await persistSimulation({
      meshId: meshRow.id,
      meshParams,
      solverParams,
      result: nativeResult.solve,
      wallMs: nativeResult.wallMs,
    });

    const includeGeometry = body.includeGeometry !== false;
    return jsonOk(
      {
        simulation: {
          id: simulation.id,
          meshId: meshRow.id,
          params: { mesh: meshParams, solver: solverParams },
          meta: nativeResult.solve.meta,
          stats: nativeResult.solve.stats,
          interpolation: nativeResult.solve.interpolation,
          surface: nativeResult.solve.surface,
          snapshots: nativeResult.solve.snapshots,
          computeMs: simulation.computeMs,
          createdAt: simulation.createdAt,
        },
        mesh: {
          id: meshRow.id,
          meta: nativeResult.mesh.meta,
          stats: nativeResult.mesh.stats,
          geometry: includeGeometry ? nativeResult.mesh.geometry : undefined,
          cached,
        },
        timings: {
          nativeWallMs: nativeResult.wallMs,
          endToEndMs: Date.now() - started,
        },
      },
      { status: 201 },
    );
  } catch (error) {
    return jsonError(error);
  }
}
