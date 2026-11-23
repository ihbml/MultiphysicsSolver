import { integer, jsonb, pgTable, serial, text, timestamp } from "drizzle-orm/pg-core";

/** Persisted optimisation runs of the MDO cockpit. */
export const optimizationRuns = pgTable("optimization_runs", {
  id: serial("id").primaryKey(),
  kind: text("kind").notNull(),
  title: text("title").notNull(),
  config: jsonb("config").notNull(),
  metrics: jsonb("metrics").notNull(),
  result: jsonb("result"),
  durationMs: integer("duration_ms").notNull().default(0),
  createdAt: timestamp("created_at", { withTimezone: true }).defaultNow().notNull(),
});

export type OptimizationRunRow = typeof optimizationRuns.$inferSelect;
export type NewOptimizationRun = typeof optimizationRuns.$inferInsert;
