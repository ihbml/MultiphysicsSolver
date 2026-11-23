import { desc } from "drizzle-orm";

import { db } from "@/db";
import { optimizationRuns } from "@/db/schema";

export const dynamic = "force-dynamic";

export async function GET() {
  try {
    const rows = await db
      .select({
        id: optimizationRuns.id,
        kind: optimizationRuns.kind,
        title: optimizationRuns.title,
        metrics: optimizationRuns.metrics,
        durationMs: optimizationRuns.durationMs,
        createdAt: optimizationRuns.createdAt,
      })
      .from(optimizationRuns)
      .orderBy(desc(optimizationRuns.id))
      .limit(18);
    return Response.json({ runs: rows });
  } catch (error) {
    console.error("run list failed", error);
    return Response.json({ runs: [], degraded: true });
  }
}
