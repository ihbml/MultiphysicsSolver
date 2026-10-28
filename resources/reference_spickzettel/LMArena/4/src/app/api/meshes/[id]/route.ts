import { jsonError, jsonOk, parseId } from "@/lib/http";
import { getMesh } from "@/lib/service";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

export async function GET(_request: Request, context: { params: Promise<{ id: string }> }) {
  try {
    const { id } = await context.params;
    const row = await getMesh(parseId(id));
    if (!row) return jsonOk({ error: "mesh not found" }, { status: 404 });
    return jsonOk({ mesh: row });
  } catch (error) {
    return jsonError(error);
  }
}
