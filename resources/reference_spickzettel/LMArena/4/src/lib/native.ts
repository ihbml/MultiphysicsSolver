import { execFile, spawn } from "node:child_process";
import { access, mkdir, stat, writeFile } from "node:fs/promises";
import path from "node:path";

import type { MeshGeometry, MeshMeta, MeshStats, SolveResult } from "./types";

const SOURCES = ["native/src/main.cpp", "native/src/mesh.cpp", "native/src/solver.cpp"];
const HEADERS = ["native/src/engine.hpp"];

export class NativeError extends Error {
  readonly detail: string;
  constructor(message: string, detail = "") {
    super(message);
    this.name = "NativeError";
    this.detail = detail;
  }
}

export interface NativeHealth {
  ok: boolean;
  engine: string;
  version: string;
  language: string;
  openmp: boolean;
  maxThreads: number;
  features: string[];
  binary: string;
  compileMs: number;
  compiled: boolean;
}

interface BinaryInfo {
  path: string;
  compileMs: number;
  compiled: boolean;
}

const globalForNative = globalThis as typeof globalThis & {
  __fvmSphBinary?: Promise<BinaryInfo>;
};

function projectRoot(): string {
  return process.cwd();
}

async function isExecutable(file: string): Promise<boolean> {
  try {
    await access(file);
    return true;
  } catch {
    return false;
  }
}

async function newestSourceMtime(root: string): Promise<number> {
  let newest = 0;
  for (const rel of [...SOURCES, ...HEADERS]) {
    try {
      const s = await stat(path.join(root, rel));
      newest = Math.max(newest, s.mtimeMs);
    } catch {
      // ignore
    }
  }
  return newest;
}

function compile(root: string, output: string): Promise<number> {
  const args = [
    "-std=c++17",
    "-O3",
    "-fopenmp",
    "-pthread",
    "-funroll-loops",
    "-fno-math-errno",
    "-o",
    output,
    ...SOURCES.map((rel) => path.join(root, rel)),
  ];
  const started = Date.now();
  return new Promise((resolve, reject) => {
    execFile("g++", args, { cwd: root, timeout: 120_000 }, (error, _stdout, stderr) => {
      if (error) {
        // Retry without OpenMP for environments without libgomp.
        const fallback = args.filter((a) => a !== "-fopenmp");
        execFile("g++", fallback, { cwd: root, timeout: 120_000 }, (err2, _o2, stderr2) => {
          if (err2) {
            reject(new NativeError("native build failed", (stderr || stderr2).slice(-2000)));
            return;
          }
          resolve(Date.now() - started);
        });
        return;
      }
      resolve(Date.now() - started);
    });
  });
}

async function buildBinary(): Promise<BinaryInfo> {
  const root = projectRoot();
  const primary = path.join(root, "native", "bin", "fvm_sph_core");
  const sourceMtime = await newestSourceMtime(root);
  let binaryMtime = 0;
  try {
    binaryMtime = (await stat(primary)).mtimeMs;
  } catch {
    binaryMtime = 0;
  }
  if (binaryMtime > sourceMtime && (await isExecutable(primary))) {
    return { path: primary, compileMs: 0, compiled: false };
  }
  try {
    await mkdir(path.dirname(primary), { recursive: true });
    const ms = await compile(root, primary);
    return { path: primary, compileMs: ms, compiled: true };
  } catch (primaryError) {
    const fallbackDir = path.join("/tmp", "fvm-sph-core");
    const fallback = path.join(fallbackDir, "fvm_sph_core");
    if (await isExecutable(fallback)) {
      return { path: fallback, compileMs: 0, compiled: false };
    }
    try {
      await mkdir(fallbackDir, { recursive: true });
      const ms = await compile(root, fallback);
      return { path: fallback, compileMs: ms, compiled: true };
    } catch {
      throw primaryError;
    }
  }
}

export async function ensureBinary(): Promise<BinaryInfo> {
  if (!globalForNative.__fvmSphBinary) {
    globalForNative.__fvmSphBinary = buildBinary().catch((error: unknown) => {
      globalForNative.__fvmSphBinary = undefined;
      throw error;
    });
  }
  return globalForNative.__fvmSphBinary;
}

function runProcess(binary: string, config: string, timeoutMs: number): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(binary, [], {
      stdio: ["pipe", "pipe", "pipe"],
      env: { ...process.env, OMP_NUM_THREADS: process.env.OMP_NUM_THREADS ?? "4" },
    });
    const chunks: Buffer[] = [];
    const errChunks: Buffer[] = [];
    let settled = false;
    const timer = setTimeout(() => {
      if (settled) return;
      settled = true;
      child.kill("SIGKILL");
      reject(new NativeError("native solver timed out"));
    }, timeoutMs);

    child.stdout.on("data", (chunk: Buffer) => chunks.push(chunk));
    child.stderr.on("data", (chunk: Buffer) => errChunks.push(chunk));
    child.on("error", (error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(new NativeError("native solver could not start", error.message));
    });
    child.on("close", (code) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      const out = Buffer.concat(chunks).toString("utf8");
      if (code !== 0) {
        reject(
          new NativeError(
            "native solver exited with an error",
            (Buffer.concat(errChunks).toString("utf8") || out).slice(-2000) ||
              `exit code ${code}`,
          ),
        );
        return;
      }
      resolve(out);
    });
    child.stdin.write(config);
    child.stdin.end();
  });
}

export async function runRaw(config: string, timeoutMs: number): Promise<Record<string, unknown>> {
  const info = await ensureBinary();
  const stdout = await runProcess(info.path, config, timeoutMs);
  let parsed: unknown;
  try {
    parsed = JSON.parse(stdout);
  } catch {
    throw new NativeError("native solver returned malformed payload", stdout.slice(0, 500));
  }
  if (!parsed || typeof parsed !== "object") {
    throw new NativeError("native solver returned an empty payload");
  }
  const record = parsed as Record<string, unknown>;
  if (typeof record.error === "string") throw new NativeError(record.error);
  return record;
}

export interface RawMeshPayload {
  meta: MeshMeta;
  stats: MeshStats;
  cells: number[][];
  verts: number[];
  faces: number[][];
  graph: { offsets: number[]; indices: number[] };
  cellArray: { mindims: number[]; minEdge: number; maxEdge: number };
}

function geometryFrom(raw: RawMeshPayload): MeshGeometry {
  return {
    cells: raw.cells,
    verts: raw.verts,
    faces: raw.faces,
    graph: raw.graph,
    cellArray: raw.cellArray,
  };
}

export interface MeshRunResult {
  meta: MeshMeta;
  stats: MeshStats;
  geometry: MeshGeometry;
  wallMs: number;
}

export async function runMesh(config: string): Promise<MeshRunResult> {
  const raw = (await runRaw(config, 120_000)) as unknown as RawMeshPayload;
  return {
    meta: raw.meta,
    stats: raw.stats,
    geometry: geometryFrom(raw),
    wallMs: Date.now(),
  };
}

export interface SolveRunResult {
  mesh: MeshRunResult;
  solve: SolveResult;
  wallMs: number;
}

export async function runSolve(config: string, timeoutMs = 240_000): Promise<SolveRunResult> {
  const started = Date.now();
  const raw = await runRaw(config, timeoutMs);
  const mesh = raw.mesh as unknown as RawMeshPayload;
  const solve = raw.solve as unknown as SolveResult;
  return {
    mesh: {
      meta: mesh.meta,
      stats: mesh.stats,
      geometry: geometryFrom(mesh),
      wallMs: mesh.meta.buildMs,
    },
    solve,
    wallMs: Date.now() - started,
  };
}

export async function nativeHealth(): Promise<NativeHealth> {
  const info = await ensureBinary();
  const raw = (await runRaw("op=health\n", 30_000)) as unknown as Omit<
    NativeHealth,
    "binary" | "compileMs" | "compiled"
  >;
  return {
    ...raw,
    ok: true,
    binary: info.path,
    compileMs: info.compileMs,
    compiled: info.compiled,
  };
}

export async function writeConfigFile(text: string): Promise<void> {
  // Kept for debugging native runs from the sandbox.
  await writeFile("/tmp/fvm-sph-last-config.txt", text, "utf8");
}
