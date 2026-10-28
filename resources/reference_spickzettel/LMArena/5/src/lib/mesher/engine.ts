import { spawn, execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile, stat } from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";

import type { JobManifest, MeshSpec } from "./types";

const ROOT = process.cwd();
const NATIVE_DIR = path.join(ROOT, "native");
export const ENGINE_PATH = path.join(NATIVE_DIR, "build", "mesher");
export const DATA_DIR = path.join(ROOT, "data", "jobs");

export class EngineError extends Error {
  readonly code: string;
  constructor(message: string, code = "engine_error") {
    super(message);
    this.name = "EngineError";
    this.code = code;
  }
}

export interface EngineInfo {
  available: boolean;
  path: string;
  engine?: string;
  openmp?: boolean;
  maxThreads?: number;
  error?: string;
}

let buildPromise: Promise<void> | null = null;

/** Compile the native core on demand (only when the binary is missing). */
function buildEngine(): Promise<void> {
  if (!buildPromise) {
    buildPromise = new Promise<void>((resolve, reject) => {
      execFile(
        "make",
        ["-C", NATIVE_DIR, "-j4"],
        { timeout: 240_000, maxBuffer: 8 * 1024 * 1024 },
        (err, stdout, stderr) => {
          if (err) {
            buildPromise = null;
            reject(
              new EngineError(
                `native core build failed: ${err.message}\n${stderr?.slice(-1500) ?? ""}`,
                "build_failed",
              ),
            );
            return;
          }
          resolve();
        },
      );
    });
  }
  return buildPromise;
}

export async function ensureEngine(): Promise<void> {
  if (existsSync(ENGINE_PATH)) return;
  await buildEngine();
  if (!existsSync(ENGINE_PATH)) {
    throw new EngineError(
      "native mesher core not found and could not be built. Run `make -C native` manually.",
      "engine_missing",
    );
  }
}

export async function engineInfo(): Promise<EngineInfo> {
  const base = { available: false, path: ENGINE_PATH } as EngineInfo;
  try {
    await ensureEngine();
  } catch (e) {
    return { ...base, error: e instanceof Error ? e.message : String(e) };
  }
  const out = await runBinary(["version"], 15_000);
  try {
    const parsed = JSON.parse(out.trim()) as Record<string, unknown>;
    return {
      available: true,
      path: ENGINE_PATH,
      engine: String(parsed.engine ?? "unknown"),
      openmp: Boolean(parsed.openmp),
      maxThreads: Number(parsed.maxThreads ?? 1),
    };
  } catch {
    return { ...base, error: "engine returned an unparsable version payload" };
  }
}

function runBinary(args: string[], timeoutMs: number): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(ENGINE_PATH, args, {
      cwd: ROOT,
      stdio: ["pipe", "pipe", "pipe"],
      env: { ...process.env, OMP_PROC_BIND: "false" },
    });
    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new EngineError(`native core timed out after ${timeoutMs}ms`, "timeout"));
    }, timeoutMs);
    child.stdout.on("data", (d) => (stdout += d.toString()));
    child.stderr.on("data", (d) => (stderr += d.toString()));
    child.on("error", (err) => {
      clearTimeout(timer);
      reject(new EngineError(`cannot execute native core: ${err.message}`, "spawn_failed"));
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      if (code !== 0) {
        reject(
          new EngineError(
            `native core exited with code ${code}: ${(stderr || stdout).slice(-2000)}`,
            "core_failed",
          ),
        );
        return;
      }
      resolve(stdout);
    });
  });
}

export interface RunResult {
  id: string;
  dir: string;
  manifest: JobManifest;
}

/**
 * Execute one mesher job through the native core.
 * The spec is handed over as a temporary file; the core writes all artifacts
 * plus manifest.json into the job directory.
 */
export async function runMesher(
  spec: MeshSpec,
  opts?: { timeoutMs?: number; id?: string },
): Promise<RunResult> {
  await ensureEngine();
  const id = opts?.id ?? randomUUID();
  const dir = path.join(DATA_DIR, id);
  await mkdir(dir, { recursive: true });

  const fullSpec: MeshSpec = {
    ...spec,
    output: { formats: ["vtu", "msh", "json"], preview: true, previewLimit: 18000, ...spec.output },
  };
  const specPath = path.join(dir, "spec.json");
  await writeFile(specPath, JSON.stringify(fullSpec, null, 2), "utf8");

  const threads = spec.threads && spec.threads > 0 ? spec.threads : 0;
  const args = ["run", "--spec", specPath, "--out", dir];
  if (threads > 0) args.push("--threads", String(threads));

  const timeoutMs = opts?.timeoutMs ?? 180_000;
  try {
    await runBinary(args, timeoutMs);
  } catch (e) {
    await writeFile(path.join(dir, "error.txt"), e instanceof Error ? e.message : String(e), "utf8");
    throw e;
  }

  const manifestRaw = await readFile(path.join(dir, "manifest.json"), "utf8");
  const manifest = JSON.parse(manifestRaw) as JobManifest;
  return { id, dir, manifest };
}

export async function runBenchmark(threads?: number): Promise<Record<string, unknown>> {
  await ensureEngine();
  const args = ["bench"];
  if (threads && threads > 0) args.push("--threads", String(threads));
  const out = await runBinary(args, 300_000);
  return JSON.parse(out.trim()) as Record<string, unknown>;
}

export interface ArtifactFile {
  name: string;
  bytes: number;
}

export async function listArtifacts(dir: string): Promise<ArtifactFile[]> {
  const { readdir } = await import("node:fs/promises");
  const entries = await readdir(dir);
  const out: ArtifactFile[] = [];
  for (const name of entries) {
    if (name === "spec.json" || name === "manifest.json") continue;
    const s = await stat(path.join(dir, name)).catch(() => null);
    if (s?.isFile()) out.push({ name, bytes: s.size });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

export async function readArtifact(dir: string, name: string): Promise<Buffer> {
  const safe = path.basename(name);
  const full = path.join(dir, safe);
  if (!full.startsWith(dir)) throw new EngineError("invalid artifact name", "bad_request");
  if (!existsSync(full)) throw new EngineError(`artifact not found: ${safe}`, "not_found");
  return readFile(full);
}

export const ARTIFACT_CONTENT_TYPES: Record<string, string> = {
  ".vtu": "application/vnd.vtk",
  ".vtk": "application/vnd.vtk",
  ".msh": "model/vnd.gmsh",
  ".json": "application/json",
  ".txt": "text/plain",
};
