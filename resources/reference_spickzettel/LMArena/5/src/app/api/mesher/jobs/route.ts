import { NextResponse } from "next/server";

import { executeJob } from "@/lib/mesher/run";
import { listJobs } from "@/lib/mesher/repo";
import { EngineError } from "@/lib/mesher/engine";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";
export const maxDuration = 300;

/** GET /api/mesher/jobs - recent jobs (summaries only). */
export async function GET(request: Request) {
  const url = new URL(request.url);
  const limit = Math.min(200, Math.max(1, Number(url.searchParams.get("limit") ?? 50)));
  try {
    const jobs = await listJobs(limit);
    return NextResponse.json({ ok: true, count: jobs.length, jobs });
  } catch (e) {
    return NextResponse.json(
      { ok: false, error: e instanceof Error ? e.message : String(e) },
      { status: 500 },
    );
  }
}

/**
 * POST /api/mesher/jobs - run one mesher job.
 * body: { preset?: string, spec?: Partial<MeshSpec>, name?: string, threads?: number }
 */
export async function POST(request: Request) {
  let body: Record<string, unknown> = {};
  try {
    body = (await request.json()) as Record<string, unknown>;
  } catch {
    return NextResponse.json(
      { ok: false, error: "request body must be JSON" },
      { status: 400 },
    );
  }
  try {
    const result = await executeJob({
      preset: typeof body.preset === "string" ? body.preset : undefined,
      spec: body.spec as never,
      name: typeof body.name === "string" ? body.name : undefined,
      threads: typeof body.threads === "number" ? body.threads : undefined,
    });
    return NextResponse.json({ ok: true, ...result }, { status: 201 });
  } catch (e) {
    const status = e instanceof EngineError && e.code === "engine_missing" ? 503 : 400;
    return NextResponse.json(
      { ok: false, error: e instanceof Error ? e.message : String(e) },
      { status },
    );
  }
}
