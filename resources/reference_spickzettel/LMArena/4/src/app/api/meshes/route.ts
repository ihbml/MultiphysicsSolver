import { jsonError, jsonOk, readJson } from "@/lib/http";
import { runMesh } from "@/lib/native";
import { buildNativeConfig, parseMeshParams } from "@/lib/params";
import { listMeshes, persistMesh } from "@/lib/service";
import type { MeshResult } from "@/lib/types";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

export async function GET(request: Request) {
  try {
    const url = new URL(request.url);
    const limit = Math.min(50, Math.max(1, Number(url.searchParams.get("limit") ?? 12)));
    const rows = await listMeshes(limit);
    return jsonOk({ count: rows.length, meshes: rows });
  } catch (error) {
    return jsonError(error);
  }
}

/**
 * Generates a Voronoi finite-volume mesh from an SPH-style particle seed cloud.
 * Body: { mesh?: MeshParams } (partial overrides are merged with the preset).
 */
export async function POST(request: Request) {
  try {
    const body = await readJson(request);
    const rawMesh = (body.mesh ?? body) as Record<string, unknown>;
    const params = parseMeshParams(rawMesh);
    const started = Date.now();
    const native = await runMesh(buildNativeConfig(params));
    const result: MeshResult = {
      meta: native.meta,
      stats: native.stats,
      geometry: native.geometry,
    };
    const { row, cached } = await persistMesh(params, result);
    return jsonOk(
      {
        mesh: {
          id: row.id,
          name: params.name,
          preset: params.preset,
          params,
          meta: result.meta,
          stats: result.stats,
          geometry: result.geometry,
          cached,
        },
        timings: {
          nativeWallMs: native.meta.buildMs,
          endToEndMs: Date.now() - started,
        },
        papers: {
          mesher:
            "Voronoi/Lloyd dual of the SPH particle cloud with zero-order consistency diagnostics",
          boundary: "Wang, Haidn & Hu (2024): FVM boundary algorithm in Eulerian SPH",
          interpolation: "Zhang & Ming (2024): adaptive SPH-FVM particle-mesh interpolation",
          graph: "Du et al. (2025): DiFVM vectorised graph FVM",
        },
      },
      { status: cached ? 200 : 201 },
    );
  } catch (error) {
    return jsonError(error);
  }
}
