import { desc, eq } from "drizzle-orm";

import { db } from "@/db";
import { meshes, simulations } from "@/db/schema";
import { fingerprint } from "./params";
import type { MeshParams, MeshResult, SolveResult, SolverParams } from "./types";

export function meshFingerprint(params: MeshParams): string {
  return fingerprint({
    domain: params.domain,
    target: params.target,
    jitter: params.jitter,
    relax: params.relax,
    wallLayer: params.wallLayer,
    obstacles: params.obstacles,
  });
}

export function simulationFingerprint(meshParams: MeshParams, solverParams: SolverParams): string {
  return fingerprint({ mesh: meshFingerprint(meshParams), solver: solverParams });
}

export async function persistMesh(params: MeshParams, result: MeshResult) {
  const fp = meshFingerprint(params);
  const existing = await db.select().from(meshes).where(eq(meshes.fingerprint, fp)).limit(1);
  if (existing.length > 0) return { row: existing[0], cached: true };
  const inserted = await db
    .insert(meshes)
    .values({
      fingerprint: fp,
      name: params.name,
      preset: params.preset,
      params,
      meta: result.meta,
      stats: result.stats,
      geometry: result.geometry,
      cellCount: result.meta.cellCount,
      faceCount: result.meta.faceCount,
      buildMs: result.meta.buildMs,
    })
    .onConflictDoNothing({ target: meshes.fingerprint })
    .returning();
  if (inserted.length > 0) return { row: inserted[0], cached: false };
  const raced = await db.select().from(meshes).where(eq(meshes.fingerprint, fp)).limit(1);
  return { row: raced[0], cached: true };
}

export async function persistSimulation(args: {
  meshId: number;
  meshParams: MeshParams;
  solverParams: SolverParams;
  result: SolveResult;
  wallMs: number;
}) {
  const fp = simulationFingerprint(args.meshParams, args.solverParams);
  const inserted = await db
    .insert(simulations)
    .values({
      meshId: args.meshId,
      fingerprint: fp,
      params: { mesh: args.meshParams, solver: args.solverParams },
      meta: args.result.meta,
      stats: args.result.stats,
      interpolation: args.result.interpolation,
      surface: args.result.surface,
      snapshots: args.result.snapshots,
      computeMs: args.wallMs,
      status: "completed",
    })
    .returning();
  return inserted[0];
}

export async function listMeshes(limit = 12) {
  return db
    .select({
      id: meshes.id,
      name: meshes.name,
      preset: meshes.preset,
      params: meshes.params,
      meta: meshes.meta,
      stats: meshes.stats,
      cellCount: meshes.cellCount,
      faceCount: meshes.faceCount,
      buildMs: meshes.buildMs,
      createdAt: meshes.createdAt,
    })
    .from(meshes)
    .orderBy(desc(meshes.id))
    .limit(limit);
}

export async function getMesh(id: number) {
  const rows = await db.select().from(meshes).where(eq(meshes.id, id)).limit(1);
  return rows[0] ?? null;
}

export async function listSimulations(limit = 12) {
  return db
    .select({
      id: simulations.id,
      meshId: simulations.meshId,
      params: simulations.params,
      meta: simulations.meta,
      stats: simulations.stats,
      interpolation: simulations.interpolation,
      computeMs: simulations.computeMs,
      createdAt: simulations.createdAt,
    })
    .from(simulations)
    .orderBy(desc(simulations.id))
    .limit(limit);
}

export async function getSimulation(id: number) {
  const rows = await db.select().from(simulations).where(eq(simulations.id, id)).limit(1);
  const simulation = rows[0];
  if (!simulation) return null;
  const mesh = await getMesh(simulation.meshId);
  return { simulation, mesh };
}
