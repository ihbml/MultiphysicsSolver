import { jsonError, jsonOk, parseId } from "@/lib/http";
import { getSimulation } from "@/lib/service";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

export async function GET(_request: Request, context: { params: Promise<{ id: string }> }) {
  try {
    const { id } = await context.params;
    const result = await getSimulation(parseId(id));
    if (!result) return jsonOk({ error: "simulation not found" }, { status: 404 });
    return jsonOk({
      simulation: result.simulation,
      mesh: result.mesh
        ? {
            id: result.mesh.id,
            meta: result.mesh.meta,
            stats: result.mesh.stats,
            geometry: result.mesh.geometry,
          }
        : null,
    });
  } catch (error) {
    return jsonError(error);
  }
}
