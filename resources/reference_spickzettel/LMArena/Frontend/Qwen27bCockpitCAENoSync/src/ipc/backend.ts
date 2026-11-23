// ─────────────────────────────────────────────────────────────────────────────
// Channel B — async JSON-RPC 2.0 over NDJSON (stdio mirror).
// Web prototype: the backend is simulated in-process with realistic latency,
// request/response id correlation, `iter` + `timestep_written` telemetry
// streams, revision tracking, and cooperative job.cancel.
// Rust mirror: rust/src/ipc/client.rs (tokio::process, stdin/stdout pipes).
// ─────────────────────────────────────────────────────────────────────────────
import { useStore } from "../state/store";
import { parseExpr } from "../units/parser";
import { DIM_M, DIM_S, DIMLESS, MATERIALS } from "../schema/registry";
import { buildDomain } from "../geom/domain";
import { buildMesh } from "../geom/mesh";
import { geoCache } from "../geom/cache";
import { devFactor, FIELD_DEFS } from "../fields/analytics";
import type { DomainParams } from "../fields/analytics";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const log = (dir: "↑" | "↓" | "·", line: string) => useStore.getState().log(dir, line);
const S = () => useStore.getState();

function domainParamsFromStore(): DomainParams {
  const p = S().params;
  const need = (key: string, fallback: number) => {
    const r = parseExpr(String(p[key]), DIM_M);
    return r.ok ? r.si : fallback;
  };
  return {
    L: need("length", 10),
    H: need("height", 2),
    hw: need("half_width", 1),
    stepX: need("step_x", 4),
    h1: need("top_low", 1.4),
    cylX: need("cyl_x", 6),
    cylY: need("cyl_y", 0.5),
    cylR: need("cyl_r", 0.5),
  };
}

function geomParamsValid(): string[] {
  const p = S().params;
  const errs: string[] = [];
  const map: [string, string][] = [
    ["length", "DIM_M"], ["height", "DIM_M"], ["half_width", "DIM_M"], ["step_x", "DIM_M"],
    ["top_low", "DIM_M"], ["cyl_x", "DIM_M"], ["cyl_y", "DIM_M"], ["cyl_r", "DIM_M"],
  ];
  for (const [k] of map) {
    const r = parseExpr(String(p[k]), DIM_M);
    if (!r.ok) errs.push(`${k}: ${r.error}`);
  }
  return errs;
}

const R0: Record<string, number> = { Ux: 1.5e3, Uy: 2.2e2, Uz: 9e1, p: 4.5e4, h: 5e1, k: 3e2, omega: 1.2e1 };
const TAU: Record<string, number> = { Ux: 160, Uy: 240, Uz: 320, p: 150, h: 90, k: 300, omega: 240 };

let solveTimer: ReturnType<typeof setInterval> | null = null;
let solveCfg: {
  maxIter: number; dt: number; cour: number; agg: boolean; writeEvery: number; nanAt: number;
} | null = null;

function stopSolveTimer() {
  if (solveTimer) clearInterval(solveTimer);
  solveTimer = null;
}

function tick() {
  const s = S();
  if (!solveCfg || s.solver !== "running") return;
  const cfg = solveCfg;
  const it = s.iter + 1;
  const res = { ...s.residuals };
  const nNaN = s.nNaN;
  let nan = nNaN;
  let alertNew = false;
  if (cfg.agg && it >= cfg.nanAt && it % 24 === 0) {
    nan = nNaN + 1;
    if (nNaN === 0) alertNew = true;
  }
  if (alertNew) {
    log("↓", `{"method":"iter","params":{"iter":${it},"alert":"n_nan_cells > 0 — EMERGENCY","n_nan":${nan}}}`);
    s.setAlert(true);
  }
  for (const k of Object.keys(res)) {
    let base = R0[k] * Math.exp(-it / TAU[k]);
    if (nan > 0) base = Math.max(base, 2.5) * (1 + (it - cfg.nanAt) * 0.02);
    const v = base * (0.7 + 0.6 * Math.random());
    res[k] = [...res[k].slice(-899), v];
  }
  const cm = cfg.agg
    ? Math.min(3.4, 0.28 + it * 0.024)
    : Math.min(cfg.cour, 0.08 + it * 0.02);
  const cmax = cm * (1.25 + 0.55 * Math.random());
  const dev = devFactor(it * cfg.dt);
  const Cd = 0.345 * dev + 0.006 * Math.sin(it / 37) + (Math.random() - 0.5) * 0.005;
  const Cl = 0.019 * dev * (1 - 0.5 * Math.exp(-it / 200)) + 0.004 * Math.sin(it / 23 + 1);
  const Tmax = 298 + 6.1 * dev + 0.25 * Math.sin(it / 50);

  const ts = s.timesteps;
  let newTs: typeof ts = ts;
  if (it % cfg.writeEvery === 0) {
    newTs = [...ts, { id: it, t: it * cfg.dt, tag: "auto" }];
    log("↓", `{"method":"timestep_written","params":{"time":${(it * cfg.dt).toFixed(4)},"iter":${it},"file":"t${String(it * cfg.dt).replace(".", "_")}"}}`);
  }
  useStore.setState({
    iter: it,
    residuals: res,
    courant: { mean: [...s.courant.mean.slice(-899), cm], max: [...s.courant.max.slice(-899), cmax] },
    forces: {
      Cd: [...s.forces.Cd.slice(-899), Cd],
      Cl: [...s.forces.Cl.slice(-899), Cl],
      Tmax: [...s.forces.Tmax.slice(-899), Tmax],
    },
    nNaN: nan,
    timesteps: newTs,
    activeT: it * cfg.dt,
    chartTick: s.chartTick + 1,
  });
  if (nan > 0 && s.meshStats) {
    useStore.setState({ meshStats: { ...s.meshStats, nan } });
  }
  log(
    "↓",
    `{"method":"iter","params":{"iter":${it},"res":{"p":${res.p[res.p.length - 1].toExponential(1)},"Ux":${res.Ux[res.Ux.length - 1].toExponential(1)}},"co":{"mean":${cm.toFixed(2)},"max":${cmax.toFixed(2)}},"forces":{"Cd":${Cd.toFixed(4)},"Cl":${Cl.toFixed(4)},"T_max":${Tmax.toFixed(2)}},"n_nan":${nan}}}`
  );
  if (it >= cfg.maxIter) {
    stopSolveTimer();
    S().setSolver("stopped");
    log("↓", `{"method":"iter","params":{"iter":${it},"done":true,"status":"converged/stopped"}}`);
    return;
  }
}

const rpc = {
  nextId: 1,
  async call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    const id = this.nextId++;
    log("↑", JSON.stringify({ jsonrpc: "2.0", id, method, params: trimParams(method, params) }));
    await sleep(28 + Math.random() * 110);
    try {
      const result = await this.dispatch(method, params);
      log("↓", JSON.stringify({ jsonrpc: "2.0", id, result: sumResult(method, result) }));
      return result as T;
    } catch (e) {
      log("↓", JSON.stringify({ jsonrpc: "2.0", id, error: { code: -32000, message: String(e) } }));
      throw e;
    }
  },

  dispatch(method: string, params: Record<string, unknown>): Promise<unknown> {
    const s = S();
    switch (method) {
      case "system.hello":
        return Promise.resolve({
          name: "foam-core-backend",
          version: "1.4.2",
          io: "foam_fast_io (zero-copy)",
          channels: ["CFDGEO01", "CFDMSH02", "CFDFLD03"],
          rpc: "JSON-RPC 2.0 / NDJSON",
        });
      case "schema.get":
        return Promise.resolve({
          registry: "ModuleRegistry.export_ui_schema()",
          workspaces: 5,
          modules: [
            "geometry.import", "geometry.patches.update", "mesh.generate", "job.cancel",
            "materials.list", "case.validate", "case.generate", "solver.start", "solver.write_now",
            "solver.stop_graceful", "solver.abort", "results.list_times", "results.list_fields",
            "results.load",
          ],
          streams: ["iter", "timestep_written"],
        });
      case "materials.list":
        return Promise.resolve(MATERIALS);
      case "geometry.import": {
        const errs = geomParamsValid();
        if (errs.length) throw new Error("unit-parser rejected: " + errs[0]);
        const dp = domainParamsFromStore();
        const geo = buildDomain(dp, !!s.params.leave_gap);
        geoCache.params = dp;
        geoCache.domain = geo;
        geoCache.mesh = null;
        s.setPatches(geo.patches);
        s.setRevisions({ geo: s.revisions.geo + 1, mesh: s.revisions.mesh });
        if (s.bcs.length) s.setStalePatches(s.bcs.map((b) => b.patch));
        s.bumpGeo();
        return Promise.resolve({
          geometry_revision: s.revisions.geo,
          vertices: geo.verts.length / 3,
          triangles: geo.nTris,
          face_groups: geo.patches.length,
          open_edge_segments: geo.openEdges.length / 6,
          non_manifold_segments: geo.nonManifold.length / 6,
          header: "CFDGEO01",
        });
      }
      case "geometry.patches.update": {
        const patch = Number(params.patch ?? s.selPatch);
        if (Number.isNaN(patch) || patch < 0) throw new Error("no patch selected — pick a surface in the viewport");
        const bcType = String(s.params.bc_type);
        const label = patchLabel(bcType, s.params);
        const bcs = [...s.bcs.filter((b) => b.patch !== patch), {
          patch, type: bcType, label, rev: s.revisions.geo,
        }];
        s.setBcs(bcs);
        s.setStalePatches(s.stalePatches.filter((x) => x !== patch));
        return Promise.resolve({
          patch,
          patchType: bcType,
          binding: label,
          bound_to: s.revisions.geo,
          stale_cleared: true,
        });
      }
      case "mesh.generate": {
        if (!geoCache.domain || !geoCache.params) throw new Error("no geometry imported (call geometry.import)");
        const cells = {
          x: Number(s.params.cells_x) || 48,
          y: Number(s.params.cells_y) || 10,
          z: Number(s.params.cells_z) || 10,
        };
        const mesh = buildMesh(geoCache.params, cells);
        geoCache.mesh = mesh;
        s.setRevisions({ geo: s.revisions.geo, mesh: s.revisions.mesh + 1 });
        s.setMeshStats({
          cells: mesh.stats.cells,
          verts: mesh.stats.verts,
          maxSkew: mesh.stats.maxSkew,
          meanNonOrth: mesh.stats.meanNonOrth,
          maxAspect: mesh.stats.maxAspect,
          folded: mesh.foldedCells.length,
          nan: s.nNaN,
          hist: mesh.stats.hist,
          nanCells: mesh.nanCandidate,
          foldedCells: mesh.foldedCells,
        });
        s.bumpGeo();
        return Promise.resolve({
          mesh_revision: s.revisions.mesh,
          header: "CFDMSH02",
          cells: mesh.stats.cells,
          vertices: mesh.stats.verts,
          max_skew: +mesh.stats.maxSkew.toFixed(3),
          mean_non_orth_deg: +mesh.stats.meanNonOrth.toFixed(1),
          folded_face_indices: mesh.foldedCells.length,
          wsrd_weights: "computed",
          amips_energy: +(0.041 + Math.random() * 0.02).toFixed(4),
        });
      }
      case "case.validate": {
        const dom = geoCache.domain;
        const warnings: string[] = [];
        if (dom && dom.openEdges.length)
          warnings.push(`${dom.openEdges.length / 6} open edge segment(s) — domain not watertight (CFDGEO01) — heal or accept as inlet slit`);
        if (dom && dom.nonManifold.length) warnings.push("1 non-manifold edge at step corner — flagged, solver will ignore");
        if (s.bcs.length === 0) warnings.push("no patch BCs assigned yet — geometry.patches.update");
        const st = s.meshStats;
        if (st && st.maxSkew > 0.92) warnings.push(`max skewness ${st.maxSkew.toFixed(3)} — 4-quadrant isolator shows hot cells near step`);
        if (st && st.folded > 0) warnings.push(`${st.folded} folded face(s) — lime in MeshErrorMap`);
        const gauge = s.params.gauge;
        if (s.params.bc_type === "pressureOutlet")
          warnings.unshift(gauge ? "p_rgh gauge mode — unit parser enforced [kg·m⁻¹·s⁻²] relative" : "absolute p mode — p_ref = 101325 Pa");
        const valid = warnings.length === 0;
        s.setValidate({ valid, warnings });
        return Promise.resolve({ valid, warnings });
      }
      case "case.generate":
        return Promise.resolve({
          case: "step_cyl_kOmega/case",
          files: ["blockMeshDict", "fvSchemes", "fvSolution", "0/U", "0/p_rgh", "0/k", "0/omega", "constant/boundary"],
          units: "strict SI (parser-enforced)",
        });
      case "solver.start": {
        stopSolveTimer();
        const p = s.params;
        const dt = parseExpr(String(p.dt), DIM_S);
        if (!dt.ok) throw new Error(`dt rejected by unit parser: ${dt.error}`);
        const maxIter = Math.max(20, Math.round(parseExpr(String(p.max_iter), DIMLESS).si));
        const writeEvery = Math.max(10, Math.round(parseExpr(String(p.write_every), DIMLESS).si));
        const cour = parseExpr(String(p.cour_target), DIMLESS).si || 0.8;
        const agg = !!p.agg_dt;
        solveCfg = {
          maxIter, dt: dt.si, cour, agg, writeEvery,
          nanAt: agg ? 110 + Math.round(Math.random() * 50) : 1e9,
        };
        useStore.setState({
          solver: "running", iter: 0, nNaN: 0, alert: false,
          residuals: { Ux: [], Uy: [], Uz: [], p: [], h: [], k: [], omega: [] },
          courant: { mean: [], max: [] },
          forces: { Cd: [], Cl: [], Tmax: [] },
          timesteps: [], activeT: 0,
        });
        if (s.meshStats) useStore.setState({ meshStats: { ...s.meshStats, nan: 0 } });
        solveTimer = setInterval(tick, 90);
        log("·", `[backend] PISO loop spawned — Δt=${dt.si} s, maxIter=${maxIter}, writeEvery=${writeEvery}${agg ? " (AGGRESSIVE Δt)" : ""}`);
        return Promise.resolve({ job: "solve-1", scheme: "PISO + SMAC", dt: dt.si, maxIter });
      }
      case "solver.write_now": {
        if (!solveCfg) return Promise.resolve({ skipped: "no active solve" });
        const it = S().iter;
        const entry = { id: it, t: it * solveCfg.dt, tag: "now" };
        const ts = S().timesteps;
        useStore.setState({ timesteps: [...ts, entry], activeT: entry.t });
        log("↓", `{"method":"timestep_written","params":{"time":${entry.t.toFixed(4)},"iter":${it},"file":"t${String(entry.t).replace(".", "_")}","cause":"write_now"}}`);
        return Promise.resolve(entry);
      }
      case "solver.stop_graceful": {
        if (s.solver !== "running") throw new Error("no running solver");
        log("·", "[backend] flushing openFOAM timesteps…");
        stopSolveTimer();
        S().setSolver("stopped");
        S().setAlert(false);
        return Promise.resolve({ status: "stopped", final_iter: s.iter });
      }
      case "solver.abort": {
        stopSolveTimer();
        S().setSolver("aborted");
        log("·", "[backend] SIGTERM — checkpoint not flushed");
        return Promise.resolve({ status: "aborted" });
      }
      case "job.cancel": {
        if (s.solver === "running") {
          stopSolveTimer();
          S().setSolver("stopped");
          S().setAlert(false);
          log("·", "[backend] job.cancel honored between iterations");
          return Promise.resolve({ cancelled: "solve-1", mode: "cooperative" });
        }
        return Promise.resolve({ cancelled: null, note: "no active job" });
      }
      case "results.list_times":
        return Promise.resolve(S().timesteps.map((t) => ({ id: t.id, time: t.t, tag: t.tag })));
      case "results.list_fields":
        return Promise.resolve(
          FIELD_DEFS.map((f) => ({
            name: f.id,
            kind: f.kind,
            location: f.wallOnly ? "BoundaryPatch" : "Cell",
            source: f.wallOnly ? "BoundaryField" : "OwnerCellFallback",
            dims: fieldDims(f.id),
            unit: f.unit,
          }))
        );
      case "results.load": {
        const t = Number(params.t ?? 0);
        S().setActiveT(t);
        return Promise.resolve({ loaded: `t${String(t).replace(".", "_")}`, fields: FIELD_DEFS.length, dims: "7-elem SI" });
      }
      case "results.export":
        return Promise.resolve({ format: "png", note: "see viewport export" });
      default:
        return Promise.reject(new Error(`unknown method ${method}`));
    }
  },
};

function fieldDims(id: string): number[] {
  switch (id) {
    case "velocity": return [0, 1, -1, 0, 0, 0, 0];
    case "p": return [1, -1, -2, 0, 0, 0, 0];
    case "T": return [0, 0, 0, 1, 0, 0, 0];
    case "k": return [0, 2, -2, 0, 0, 0, 0];
    case "omega": return [0, 0, -1, 0, 0, 0, 0];
    case "tau_w": return [1, -1, -2, 0, 0, 0, 0];
    default: return [0, 0, 0, 0, 0, 0, 0];
  }
}

function patchLabel(bc: string, p: Record<string, unknown>): string {
  const px = (k: string) => parseExpr(String(p[k])).si;
  switch (bc) {
    case "velocityInlet":
      return `U=(${px("vel_mag").toFixed(2)},0,0) m/s, k=${px("k0").toFixed(3)}, ω=${px("om0").toFixed(1)}`;
    case "pressureOutlet":
      return p.gauge ? `p_rgh=${px("p_ref").toFixed(0)} Pa (gauge)` : `p=${px("p_ref").toFixed(0)} Pa (abs)`;
    case "wall":
      return p.wall_treatment === "yplus"
        ? `no-slip, y⁺≈${px("yplus_target").toFixed(0)}`
        : `no-slip, Δy₁=${px("dy1").toExponential(1)} m`;
    case "symmetry": return "symmetry (zeroGradient)";
    default: return bc;
  }
}

function trimParams(method: string, params: Record<string, unknown>): Record<string, unknown> {
  if (method === "solver.start" || method === "geometry.patches.update") return params;
  return params;
}

function sumResult(method: string, r: unknown): unknown {
  const o = r as Record<string, unknown>;
  if (method === "schema.get") return { ...o, modules: "14 + 2 streams" };
  if (method === "case.validate") return o;
  return o;
}

let booted = false;
export async function bootBackend() {
  if (booted) return;
  booted = true;
  const s = S();
  s.setBootLine("spawn foam-core-backend (NDJSON stdio)…");
  await rpc.call("system.hello");
  s.setBootLine("schema.get — ModuleRegistry…");
  await rpc.call("schema.get");
  await rpc.call("materials.list");
  s.setBootLine("importing CFDGEO01 tessellation…");
  await rpc.call("geometry.import");
  s.setBootLine("mesh.generate — CFDMSH02…");
  await rpc.call("mesh.generate");
  s.setBackendReady(true);
  s.setBootLine("ready — 16 endpoints + 2 streams");
}

export async function reimportGeometry() {
  await rpc.call("geometry.import");
  await rpc.call("mesh.generate");
}

export { rpc, stopSolveTimer };
export type { DomainParams };
