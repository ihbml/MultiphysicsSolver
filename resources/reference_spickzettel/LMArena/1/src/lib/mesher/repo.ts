import { and, desc, eq } from "drizzle-orm";

import { db } from "@/db";
import { benchmarkRuns, meshArtifacts, meshJobs, type MeshJob } from "@/db/schema";
import type { JobManifest, MeshSpec } from "./types";

export interface JobSummary {
  id: string;
  name: string;
  preset: string | null;
  status: string;
  engine: string | null;
  threads: number | null;
  cells: number | null;
  particles: number | null;
  faces: number | null;
  totalMs: number | null;
  cellsPerSecond: number | null;
  warnings: unknown;
  error: string | null;
  createdAt: Date;
}

export async function createJobRow(input: {
  name: string;
  preset: string | null;
  spec: MeshSpec;
  outputDir: string;
}): Promise<string> {
  const [row] = await db
    .insert(meshJobs)
    .values({
      name: input.name,
      preset: input.preset,
      status: "running",
      spec: input.spec as unknown as Record<string, unknown>,
      outputDir: input.outputDir,
    })
    .returning({ id: meshJobs.id });
  return row.id;
}

export async function finalizeJobRow(
  id: string,
  manifest: JobManifest,
  outputDir: string,
): Promise<void> {
  const metrics = {
    mesh: manifest.mesh,
    sph: manifest.sph,
    coupling: manifest.coupling,
    throughput: manifest.throughput,
  };
  await db
    .update(meshJobs)
    .set({
      status: "done",
      engine: manifest.engine,
      threads: manifest.threads,
      counts: manifest.counts as unknown as Record<string, unknown>,
      metrics: metrics as unknown as Record<string, unknown>,
      timings: manifest.timings as unknown as Record<string, unknown>,
      artifacts: manifest.artifacts as unknown as Record<string, unknown>[],
      warnings: manifest.warnings as unknown as string[],
      cells: manifest.counts?.cells ?? 0,
      particles: manifest.counts?.particles ?? 0,
      faces: manifest.counts?.faces ?? 0,
      totalMs: manifest.totalMs ?? 0,
      cellsPerSecond: manifest.throughput?.cellsPerSecond ?? 0,
      outputDir,
      updatedAt: new Date(),
    })
    .where(eq(meshJobs.id, id));

  if (manifest.artifacts?.length) {
    await db.insert(meshArtifacts).values(
      manifest.artifacts.map((a) => ({
        jobId: id,
        name: a.name,
        format: a.format.replace(/^\./, "") || "bin",
        path: a.path,
        bytes: a.bytes ?? 0,
      })),
    );
  }
}

export async function failJobRow(id: string, message: string): Promise<void> {
  await db
    .update(meshJobs)
    .set({ status: "error", error: message.slice(0, 4000), updatedAt: new Date() })
    .where(eq(meshJobs.id, id));
}

const summaryCols = {
  id: meshJobs.id,
  name: meshJobs.name,
  preset: meshJobs.preset,
  status: meshJobs.status,
  engine: meshJobs.engine,
  threads: meshJobs.threads,
  cells: meshJobs.cells,
  particles: meshJobs.particles,
  faces: meshJobs.faces,
  totalMs: meshJobs.totalMs,
  cellsPerSecond: meshJobs.cellsPerSecond,
  warnings: meshJobs.warnings,
  error: meshJobs.error,
  createdAt: meshJobs.createdAt,
};

export async function listJobs(limit = 50): Promise<JobSummary[]> {
  const rows = await db
    .select(summaryCols)
    .from(meshJobs)
    .orderBy(desc(meshJobs.createdAt))
    .limit(limit);
  return rows as unknown as JobSummary[];
}

export async function getJob(id: string): Promise<MeshJob | undefined> {
  const rows = await db.select().from(meshJobs).where(eq(meshJobs.id, id)).limit(1);
  return rows[0];
}

export async function getJobArtifacts(id: string) {
  return db.select().from(meshArtifacts).where(eq(meshArtifacts.jobId, id));
}

export async function deleteJob(id: string): Promise<boolean> {
  const rows = await db.delete(meshJobs).where(eq(meshJobs.id, id)).returning({ id: meshJobs.id });
  return rows.length > 0;
}

export async function saveBenchmark(payload: {
  engine?: string;
  threads?: number;
  runs: unknown;
  totalMs?: number;
}) {
  const [row] = await db
    .insert(benchmarkRuns)
    .values({
      engine: payload.engine ?? null,
      threads: payload.threads ?? null,
      runs: payload.runs as unknown as Record<string, unknown>[],
      totalMs: payload.totalMs ?? null,
    })
    .returning({ id: benchmarkRuns.id });
  return row.id;
}

export async function listBenchmarks(limit = 10) {
  return db
    .select({
      id: benchmarkRuns.id,
      engine: benchmarkRuns.engine,
      threads: benchmarkRuns.threads,
      totalMs: benchmarkRuns.totalMs,
      createdAt: benchmarkRuns.createdAt,
    })
    .from(benchmarkRuns)
    .orderBy(desc(benchmarkRuns.createdAt))
    .limit(limit);
}

export async function findJobBySpecName(name: string) {
  const rows = await db
    .select(summaryCols)
    .from(meshJobs)
    .where(and(eq(meshJobs.name, name), eq(meshJobs.status, "done")))
    .orderBy(desc(meshJobs.createdAt))
    .limit(1);
  return rows[0] as unknown as JobSummary | undefined;
}
