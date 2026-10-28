import { randomUUID } from "node:crypto";

import { getPreset } from "./presets";
import { runMesher } from "./engine";
import { createJobRow, failJobRow, finalizeJobRow } from "./repo";
import type { JobManifest, MeshSpec } from "./types";

export type Json = Record<string, unknown>;

/** Recursive merge of plain objects; arrays and scalars are replaced. */
export function deepMerge<T extends Json>(base: T, patch: unknown): T {
  if (!patch || typeof patch !== "object" || Array.isArray(patch)) return base;
  const out: Json = { ...(base as Json) };
  for (const [k, v] of Object.entries(patch as Json)) {
    if (v && typeof v === "object" && !Array.isArray(v) && out[k] && typeof out[k] === "object" &&
        !Array.isArray(out[k])) {
      out[k] = deepMerge(out[k] as Json, v);
    } else if (v !== undefined) {
      out[k] = v;
    }
  }
  return out as T;
}

export interface RunJobInput {
  preset?: string;
  spec?: Partial<MeshSpec> & Json;
  name?: string;
  threads?: number;
}

export interface RunJobOutput {
  id: string;
  name: string;
  preset: string | null;
  spec: MeshSpec;
  manifest: JobManifest;
}

/**
 * Resolve a request body into a full spec, persist a job row, execute the
 * native core and store the resulting metrics.
 */
export async function executeJob(input: RunJobInput): Promise<RunJobOutput> {
  const preset = input.preset ? getPreset(input.preset) : undefined;
  if (input.preset && !preset) {
    throw new Error(`unknown preset: ${input.preset}`);
  }
  const base = (preset?.spec ?? input.spec ?? {}) as MeshSpec & Json;
  if (!preset && !input.spec?.geometry) {
    throw new Error("a geometry definition is required (or pass a known preset id)");
  }
  const spec = deepMerge(base as Json, input.spec ?? {}) as unknown as MeshSpec;
  if (input.threads && input.threads > 0) spec.threads = input.threads;
  spec.name = input.name ?? spec.name ?? preset?.name ?? "mesher-job";

  const id = randomUUID();
  let outputDir = "";
  try {
    outputDir = `data/jobs/${id}`;
    await createJobRow({
      name: String(spec.name),
      preset: preset?.id ?? null,
      spec,
      outputDir,
    });
    const result = await runMesher(spec, { id });
    outputDir = result.dir;
    await finalizeJobRow(id, result.manifest, result.dir);
    return {
      id,
      name: String(spec.name),
      preset: preset?.id ?? null,
      spec,
      manifest: result.manifest,
    };
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    try {
      await failJobRow(id, message);
    } catch {
      /* the job row may not exist yet */
    }
    throw new Error(message);
  }
}
