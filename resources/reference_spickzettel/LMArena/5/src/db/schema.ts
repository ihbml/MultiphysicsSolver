import {
  doublePrecision,
  integer,
  jsonb,
  pgTable,
  text,
  timestamp,
  uuid,
} from "drizzle-orm/pg-core";

/**
 * A mesher job: one full pipeline run of the native core
 * (contour -> unstructured FVM mesh + DiFVM graph -> Eulerian SPH particles
 *  with the FVM-equivalent boundary storage -> adaptive SPH/FVM coupling).
 */
export const meshJobs = pgTable("mesh_jobs", {
  id: uuid("id").primaryKey().defaultRandom(),
  name: text("name").notNull(),
  preset: text("preset"),
  status: text("status").notNull().default("pending"),
  engine: text("engine"),
  threads: integer("threads"),
  spec: jsonb("spec").notNull(),
  /** full manifest emitted by the native core (metrics + decimated preview) */
  manifest: jsonb("manifest"),
  counts: jsonb("counts"),
  metrics: jsonb("metrics"),
  timings: jsonb("timings"),
  artifacts: jsonb("artifacts"),
  warnings: jsonb("warnings"),
  error: text("error"),
  cells: integer("cells"),
  particles: integer("particles"),
  faces: integer("faces"),
  totalMs: doublePrecision("total_ms"),
  cellsPerSecond: doublePrecision("cells_per_second"),
  outputDir: text("output_dir"),
  createdAt: timestamp("created_at", { withTimezone: true }).defaultNow().notNull(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).defaultNow().notNull(),
});

/** Physical artifact files written by the native core for a job. */
export const meshArtifacts = pgTable("mesh_artifacts", {
  id: uuid("id").primaryKey().defaultRandom(),
  jobId: uuid("job_id")
    .notNull()
    .references(() => meshJobs.id, { onDelete: "cascade" }),
  name: text("name").notNull(),
  format: text("format").notNull(),
  path: text("path").notNull(),
  bytes: doublePrecision("bytes").notNull().default(0),
  createdAt: timestamp("created_at", { withTimezone: true }).defaultNow().notNull(),
});

/** Scaling / throughput runs of the native core benchmark harness. */
export const benchmarkRuns = pgTable("benchmark_runs", {
  id: uuid("id").primaryKey().defaultRandom(),
  engine: text("engine"),
  threads: integer("threads"),
  runs: jsonb("runs").notNull(),
  totalMs: doublePrecision("total_ms"),
  createdAt: timestamp("created_at", { withTimezone: true }).defaultNow().notNull(),
});

export type MeshJob = typeof meshJobs.$inferSelect;
export type NewMeshJob = typeof meshJobs.$inferInsert;
export type MeshArtifact = typeof meshArtifacts.$inferSelect;
export type BenchmarkRun = typeof benchmarkRuns.$inferSelect;
