import { sql } from "drizzle-orm";

import { db } from "@/db";
import { jsonError, jsonOk } from "@/lib/http";
import { nativeHealth } from "@/lib/native";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

export async function GET() {
  const started = Date.now();
  try {
    let database = "down";
    try {
      await db.execute(sql`select 1`);
      database = "up";
    } catch {
      database = "down";
    }
    const native = await nativeHealth();
    return jsonOk({
      status: database === "up" && native.ok ? "ok" : "degraded",
      service: "fvm-sph-mesher",
      database,
      native,
      latencyMs: Date.now() - started,
      timestamp: new Date().toISOString(),
    });
  } catch (error) {
    return jsonError(error);
  }
}
