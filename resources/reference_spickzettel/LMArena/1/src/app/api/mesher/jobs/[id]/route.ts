import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";

import { NextResponse } from "next/server";

import { DATA_DIR } from "@/lib/mesher/engine";
import { deleteJob, getJob, getJobArtifacts } from "@/lib/mesher/repo";
import type { JobManifest } from "@/lib/mesher/types";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

interface Ctx {
  params: Promise<{ id: string }>;
}

/** GET /api/mesher/jobs/:id - job row, artifacts and the full manifest. */
export async function GET(_request: Request, ctx: Ctx) {
  const { id } = await ctx.params;
  const job = await getJob(id);
  if (!job) return NextResponse.json({ ok: false, error: "job not found" }, { status: 404 });
  const artifacts = await getJobArtifacts(id);

  let manifest: JobManifest | null = null;
  const dir = job.outputDir ?? path.join(DATA_DIR, id);
  const manifestPath = path.join(dir, "manifest.json");
  if (existsSync(manifestPath)) {
    try {
      manifest = JSON.parse(await readFile(manifestPath, "utf8")) as JobManifest;
    } catch {
      manifest = null;
    }
  }
  return NextResponse.json({
    ok: true,
    job: { ...job, manifest: undefined },
    artifacts,
    manifest,
    hasPreview: Boolean(manifest?.preview),
  });
}

/** DELETE /api/mesher/jobs/:id - drop the database record. */
export async function DELETE(_request: Request, ctx: Ctx) {
  const { id } = await ctx.params;
  const removed = await deleteJob(id);
  return NextResponse.json({ ok: removed, id });
}
