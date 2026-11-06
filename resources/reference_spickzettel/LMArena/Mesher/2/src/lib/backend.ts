/**
 * Client for the optional Rust core (backend/README.md).
 *
 * The Rust service speaks the exact same camelCase JSON schema as the
 * in-browser TypeScript engine, so exchanging one for the other never changes
 * the UI, the probe or the exporters. If the service is unreachable the app
 * automatically falls back to the in-browser engine.
 */
import { FvmMesh, MeshParams } from "./mesh";
import { SphData, SphParams, KernelId } from "./sph";
import type { Scheme } from "./coupling";
import type { CoupleSummary } from "../components/Metrics";
import { Geometry } from "./sdf";

export const DEFAULT_API = "http://127.0.0.1:8080";

export type Engine = "ts" | "rust";
export type BackendStatus = "unknown" | "checking" | "online" | "offline";

export interface Timings {
  engine: Engine;
  meshMs?: number;
  sphMs?: number;
  cells?: number;
  particles?: number;
  when: number;
}

async function request<T>(
  url: string,
  init: RequestInit = {},
  timeoutMs = 60_000,
): Promise<T> {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const res = await fetch(url, { ...init, signal: ctl.signal });
    if (!res.ok) {
      let detail = `${res.status} ${res.statusText}`;
      try {
        const body = await res.json();
        if (body?.error) detail = String(body.error);
      } catch {
        /* non-JSON error body */
      }
      throw new Error(detail);
    }
    return (await res.json()) as T;
  } finally {
    clearTimeout(t);
  }
}

const post = <T>(api: string, path: string, body: unknown, timeout = 60_000) =>
  request<T>(
    `${api.replace(/\/$/, "")}${path}`,
    {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(body),
    },
    timeout,
  );

export async function checkHealth(
  api: string,
): Promise<{ ok: boolean; version?: string; error?: string }> {
  try {
    const j = await request<{ status: string; version: string }>(
      `${api.replace(/\/$/, "")}/api/health`,
      {},
      4000,
    );
    return { ok: j.status === "ok", version: j.version };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : String(e) };
  }
}

function typedMesh(j: any): FvmMesh {
  return {
    points: Float64Array.from(j.points),
    tris: Uint32Array.from(j.tris),
    nCells: j.nCells,
    cc: Float64Array.from(j.cc),
    cv: Float64Array.from(j.cv),
    nFaces: j.nFaces,
    owner: Int32Array.from(j.owner),
    neigh: Int32Array.from(j.neigh),
    fc: Float64Array.from(j.fc),
    fn: Float64Array.from(j.fn),
    fa: Float64Array.from(j.fa),
    fPatch: Int8Array.from(j.fPatch),
    faceNodes: Uint32Array.from(j.faceNodes),
    nonOrtho: Float32Array.from(j.nonOrtho),
    skew: Float32Array.from(j.skew),
    weight: Float32Array.from(j.weight),
    nonOrthoCorr: Float32Array.from(j.nonOrthoCorr),
    quality: Float32Array.from(j.quality),
    cellSize: Float32Array.from(j.cellSize),
    rcm: Uint32Array.from(j.rcm),
    stats: j.stats,
  };
}

function typedSph(j: any): SphData {
  return {
    n: j.n,
    x: Float64Array.from(j.x),
    y: Float64Array.from(j.y),
    V: Float64Array.from(j.V),
    h: Float64Array.from(j.h),
    level: Uint8Array.from(j.level),
    divR: Float32Array.from(j.divR),
    eps0: Float32Array.from(j.eps0),
    eps1: Float32Array.from(j.eps1),
    nNeigh: Int32Array.from(j.nNeigh),
    isBnd: Uint8Array.from(j.isBnd),
    asMag: Float32Array.from(j.asMag),
    asNx: Float32Array.from(j.asNx),
    asNy: Float32Array.from(j.asNy),
    patch: Int8Array.from(j.patch),
    diag: j.diag,
  };
}

export async function rustMesh(
  api: string,
  geo: Geometry,
  params: MeshParams,
): Promise<{ mesh: FvmMesh; ms: number }> {
  const t0 = performance.now();
  const json = await post<any>(api, "/api/mesh", {
    geometry: geo.id,
    params: {
      h0: params.h0,
      grading: params.grading,
      hmaxRatio: params.hmaxRatio,
      iters: params.iters,
    },
  });
  return { mesh: typedMesh(json), ms: performance.now() - t0 };
}

export async function rustSph(
  api: string,
  geo: Geometry,
  params: SphParams,
  core?: { x: number; y: number; r: number } | null,
): Promise<{ sph: SphData; ms: number }> {
  const t0 = performance.now();
  const json = await post<any>(api, "/api/sph", {
    geometry: geo.id,
    params: {
      dp: params.dp,
      hFactor: params.hFactor,
      iters: params.iters,
      kernel: params.kernel as KernelId,
      refineRatio: params.refineRatio,
      grading: params.grading,
      volumeReinit: params.volumeReinit,
      lattice: params.lattice,
      ...(core ? { core: [core.x, core.y, core.r] } : {}),
    },
  });
  return { sph: typedSph(json), ms: performance.now() - t0 };
}

export async function rustCouple(
  api: string,
  geo: Geometry,
  args: {
    dp: number;
    hFactor: number;
    kernel: KernelId;
    core: { x: number; y: number; r: number; buffer: number };
    scheme: Scheme;
    mesh?: MeshParams;
    sph?: SphParams;
  },
): Promise<CoupleSummary & { durationMs?: number }> {
  return post<CoupleSummary & { durationMs?: number }>(api, "/api/couple", {
    geometry: geo.id,
    dp: args.dp,
    hFactor: args.hFactor,
    kernel: args.kernel,
    core: args.core,
    scheme: args.scheme,
    mesh: args.mesh,
    sph: args.sph,
  });
}

export const CURL_EXAMPLES = `# mesh (flat arrays, same schema as the browser engine)
curl -s localhost:8080/api/mesh -H 'content-type: application/json' \\
  -d '{"geometry":"cylinder","params":{"h0":0.05,"grading":0.45,"hmaxRatio":3,"iters":45}}'

# relaxed Eulerian-SPH cloud
curl -s localhost:8080/api/sph -H 'content-type: application/json' \\
  -d '{"geometry":"airfoil","params":{"dp":0.05,"hFactor":1.3,"iters":80,"kernel":"wendland"}}'

# SPH <-> FVM transfer operators
curl -s localhost:8080/api/couple -H 'content-type: application/json' \\
  -d '{"geometry":"bubble","dp":0.055,"hFactor":1.3,"kernel":"wendland",
       "core":{"x":1.5,"y":1.5,"r":0.95,"buffer":3},"scheme":"opmis"}'

# solver-ready output
curl -s 'localhost:8080/api/export/graph.json?geometry=cylinder' > graph.json
curl -s 'localhost:8080/api/export/mesh.vtk?geometry=cylinder' > mesh.vtk`;
