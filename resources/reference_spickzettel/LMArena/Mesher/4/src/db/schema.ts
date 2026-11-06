import {
  index,
  integer,
  jsonb,
  pgTable,
  real,
  serial,
  text,
  timestamp,
  uniqueIndex,
} from "drizzle-orm/pg-core";

import type {
  InterpolationStats,
  MeshGeometry,
  MeshMeta,
  MeshParams,
  MeshStats,
  Snapshot,
  SolveMeta,
  SolveStats,
} from "@/lib/types";

/**
 * A generated FVM mesh (Voronoi dual of the SPH particle cloud).
 * Geometry is stored as a single jsonb payload: vertices, faces, CSR graph.
 */
export const meshes = pgTable(
  "meshes",
  {
    id: serial("id").primaryKey(),
    fingerprint: text("fingerprint").notNull(),
    name: text("name").notNull(),
    preset: text("preset").notNull().default("cylinder"),
    params: jsonb("params").$type<MeshParams>().notNull(),
    meta: jsonb("meta").$type<MeshMeta>().notNull(),
    stats: jsonb("stats").$type<MeshStats>().notNull(),
    geometry: jsonb("geometry").$type<MeshGeometry>().notNull(),
    cellCount: integer("cell_count").notNull(),
    faceCount: integer("face_count").notNull(),
    buildMs: real("build_ms").notNull(),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  },
  (table) => [uniqueIndex("meshes_fingerprint_idx").on(table.fingerprint)],
);

/**
 * A compressible FVM / Eulerian-SPH run: parameters, diagnostics, surface
 * pressures and the field snapshots used by the viewer.
 */
export const simulations = pgTable(
  "simulations",
  {
    id: serial("id").primaryKey(),
    meshId: integer("mesh_id")
      .notNull()
      .references(() => meshes.id, { onDelete: "cascade" }),
    fingerprint: text("fingerprint").notNull(),
    params: jsonb("params").$type<{ mesh: MeshParams; solver: unknown }>().notNull(),
    meta: jsonb("meta").$type<SolveMeta>().notNull(),
    stats: jsonb("stats").$type<SolveStats>().notNull(),
    interpolation: jsonb("interpolation").$type<InterpolationStats>().notNull(),
    surface: jsonb("surface").$type<number[][]>().notNull(),
    snapshots: jsonb("snapshots").$type<Snapshot[]>().notNull(),
    status: text("status").notNull().default("completed"),
    computeMs: real("compute_ms").notNull().default(0),
    createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  },
  (table) => [index("simulations_mesh_idx").on(table.meshId)],
);

export type MeshRow = typeof meshes.$inferSelect;
export type SimulationRow = typeof simulations.$inferSelect;
