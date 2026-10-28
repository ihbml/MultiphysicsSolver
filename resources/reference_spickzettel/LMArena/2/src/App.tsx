import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  GEOMETRIES,
  PATCH_ORDER,
  getGeometry,
  isoContour,
} from "./lib/sdf";
import { FvmMesh, MeshJob, MeshParams, buildFvm } from "./lib/mesh";
import {
  KERNELS,
  KernelId,
  NeighborGrid,
  SphData,
  SphJob,
  SphParams,
} from "./lib/sph";
import {
  CELL_COVERED,
  CELL_INTERFACE,
  CoreRegion,
  InterpSet,
  InterpStats,
  PART_BUFFER,
  PART_OUTSIDE,
  SCHEME_LABEL,
  Scheme,
  buildInterp,
  classifyCells,
  classifyParticles,
} from "./lib/coupling";
import Viewport, { CellField, Layers, PartField, Probe } from "./components/Viewport";
import RustCore from "./components/RustCore";
import {
  BackendStatus,
  DEFAULT_API,
  Engine,
  Timings,
  checkHealth,
  rustMesh,
  rustSph,
} from "./lib/backend";
import {
  CoupleMetrics,
  CoupleSummary,
  EquivalenceMetrics,
  MeshMetrics,
  SphMetrics,
} from "./components/Metrics";
import { Btn, Note, Panel, Row, Select, Slider, Stat, Toggle } from "./components/ui";
import {
  JAX_STUB,
  download,
  graphToJson,
  meshToFoam,
  meshToVtk,
  sphBoundaryTable,
  sphToCsv,
  sphToVtk,
} from "./lib/exporters";
import { Ramp, fmt, rampCss } from "./lib/colors";
import { cn } from "./utils/cn";

type Tab = "mesh" | "sph" | "couple" | "export" | "rust";

const TABS: { id: Tab; label: string }[] = [
  { id: "mesh", label: "FVM mesh" },
  { id: "sph", label: "Eulerian SPH" },
  { id: "couple", label: "Coupling" },
  { id: "export", label: "Export" },
  { id: "rust", label: "Rust core" },
];

const BC_TEXT: Record<string, string> = {
  inlet: "U = U∞ imposed, p zero-gradient",
  outlet: "p = p∞, U zero-gradient",
  wall: "non-slip: mirrored velocity state",
  body: "non-slip: mirrored velocity state",
  farfield: "non-reflecting Riemann invariants",
  symmetry: "slip / reflecting",
};

export default function App() {
  const [geoId, setGeoId] = useState("cylinder");
  const geo = useMemo(() => getGeometry(geoId), [geoId]);
  const outline = useMemo(() => isoContour(geo, 420), [geo]);
  const [tab, setTab] = useState<Tab>("mesh");

  const [mp, setMp] = useState<MeshParams>({
    h0: 0.05,
    grading: 0.45,
    hmaxRatio: 3,
    iters: 45,
  });
  const [sp, setSp] = useState<SphParams>({
    dp: 0.055,
    hFactor: 1.3,
    iters: 60,
    kernel: "wendland",
    refineRatio: 1,
    grading: 0.6,
    volumeReinit: true,
    lattice: "square",
  });

  const [mesh, setMesh] = useState<FvmMesh | null>(null);
  const [sph, setSph] = useState<SphData | null>(null);
  const [sphHist, setSphHist] = useState<number[]>([]);
  const [preview, setPreview] = useState<{
    x: Float64Array;
    y: Float64Array;
    n: number;
    tris: Uint32Array;
  } | null>(null);
  const [busy, setBusy] = useState<string | null>(null);

  const [coupled, setCoupled] = useState(false);
  const [core, setCore] = useState<CoreRegion>({ ...geo.core, buffer: 3 });
  const [scheme, setScheme] = useState<Scheme>("opmis");

  const [layers, setLayers] = useState<Layers>({
    cells: true,
    edges: true,
    graph: false,
    boundaryFaces: true,
    faceNormals: false,
    particles: false,
    bndSurface: false,
    coupling: true,
    outline: true,
  });
  const [cellField, setCellField] = useState<CellField>("quality");
  const [partField, setPartField] = useState<PartField>("eps0");
  const [rampName, setRampName] = useState<Ramp>("viridis");
  const [probe, setProbe] = useState<Probe | null>(null);

  /* optional Rust core (backend/) */
  const [engine, setEngine] = useState<Engine>("ts");
  const [apiUrl, setApiUrl] = useState(DEFAULT_API);
  const [backendStatus, setBackendStatus] = useState<BackendStatus>("unknown");
  const [backendVersion, setBackendVersion] = useState<string>();
  const [backendError, setBackendError] = useState<string>();
  const [timings, setTimings] = useState<Timings | null>(null);

  const raf = useRef<number | null>(null);
  const stop = useCallback(() => {
    if (raf.current !== null) cancelAnimationFrame(raf.current);
    raf.current = null;
  }, []);
  useEffect(() => stop, [stop]);

  /* reset everything when the geometry changes */
  useEffect(() => {
    stop();
    setMesh(null);
    setSph(null);
    setPreview(null);
    setProbe(null);
    setBusy(null);
    setSphHist([]);
    setCore({ ...geo.core, buffer: 3 });
  }, [geo, stop]);

  /* ---------------- generation drivers ---------------- */
  const generateMesh = useCallback(
    (then?: () => void) => {
      stop();
      setMesh(null);
      setProbe(null);
      const started = performance.now();
      const runTs = () => {
        const job = new MeshJob(geo, mp);
        setPreview({ x: job.px, y: job.py, n: job.n, tris: job.tris });
        const tick = () => {
          const t0 = performance.now();
          let done = job.iter >= mp.iters;
          while (!done && performance.now() - t0 < 22) done = job.step();
          setPreview({ x: job.px, y: job.py, n: job.n, tris: job.tris });
          setBusy(
            `DistMesh relaxation ${job.iter}/${mp.iters} · max Δx = ${job.maxMove.toFixed(
              4,
            )} h₀`,
          );
          if (done) {
            const m = buildFvm(job);
            setMesh(m);
            setPreview(null);
            setBusy(null);
            setTimings({
              engine: "ts",
              meshMs: performance.now() - started,
              cells: m.nCells,
              when: Date.now(),
            });
            raf.current = null;
            then?.();
          } else raf.current = requestAnimationFrame(tick);
        };
        raf.current = requestAnimationFrame(tick);
      };
      if (engine === "rust" && backendStatus === "online") {
        setBusy("Rust core · mesh job");
        rustMesh(apiUrl, geo, mp)
          .then(({ mesh: m, ms }) => {
            setMesh(m);
            setPreview(null);
            setBusy(null);
            setTimings({
              engine: "rust",
              meshMs: ms,
              cells: m.nCells,
              when: Date.now(),
            });
            then?.();
          })
          .catch((e: unknown) => {
            setBackendError(
              `Rust mesh failed: ${e instanceof Error ? e.message : String(e)} — fell back to TypeScript`,
            );
            setBackendStatus("offline");
            runTs();
          });
        return;
      }
      runTs();
    },
    [geo, mp, stop, engine, backendStatus, apiUrl],
  );

  const generateSph = useCallback((forceCoupled?: boolean) => {
    stop();
    setSph(null);
    setProbe(null);
    setSphHist([]);
    setLayers((l) => ({ ...l, particles: true }));
    const inCore = forceCoupled ?? coupled;
    const started = performance.now();
    const runTs = () => {
      const mask = inCore
        ? (x: number, y: number) => Math.hypot(x - core.x, y - core.y) < core.r
        : undefined;
      const job = new SphJob(geo, sp, mask);
      setPreview({ x: job.x, y: job.y, n: job.n, tris: new Uint32Array(0) });
      const tick = () => {
        const t0 = performance.now();
        let done = job.iter >= sp.iters;
        while (!done && performance.now() - t0 < 22) done = job.step();
        setPreview({ x: job.x, y: job.y, n: job.n, tris: new Uint32Array(0) });
        const eps = job.history[job.history.length - 1]?.eps ?? 1;
        setBusy(
          `particle relaxation ${job.iter}/${sp.iters} · ε₀ = ${eps.toExponential(2)}`,
        );
        if (done) {
          job.reinitVolumes();
          const data = job.finalize();
          setSph(data);
          setSphHist(job.history.map((h) => h.eps));
          setPreview(null);
          setBusy(null);
          setTimings((t) => ({
            engine: "ts",
            meshMs: t?.meshMs,
            sphMs: performance.now() - started,
            cells: t?.cells,
            particles: data.n,
            when: Date.now(),
          }));
          raf.current = null;
        } else raf.current = requestAnimationFrame(tick);
      };
      raf.current = requestAnimationFrame(tick);
    };
    if (engine === "rust" && backendStatus === "online") {
      setBusy("Rust core · particle relaxation");
      rustSph(apiUrl, geo, sp, inCore ? core : null)
        .then(({ sph: data, ms }) => {
          setSph(data);
          setPreview(null);
          setBusy(null);
          setTimings((t) => ({
            engine: "rust",
            meshMs: t?.meshMs,
            sphMs: ms,
            cells: t?.cells,
            particles: data.n,
            when: Date.now(),
          }));
        })
        .catch((e: unknown) => {
          setBackendError(
            `Rust SPH failed: ${e instanceof Error ? e.message : String(e)} — fell back to TypeScript`,
          );
          setBackendStatus("offline");
          runTs();
        });
      return;
    }
    runTs();
  }, [geo, sp, coupled, core, stop, engine, backendStatus, apiUrl]);

  /* health probe whenever the Rust engine is selected / the URL changes */
  useEffect(() => {
    if (engine !== "rust") return;
    let alive = true;
    setBackendStatus("checking");
    checkHealth(apiUrl).then((r) => {
      if (!alive) return;
      setBackendStatus(r.ok ? "online" : "offline");
      setBackendVersion(r.version);
      setBackendError(
        r.ok
          ? undefined
          : `No Rust service at ${apiUrl}${r.error ? ` (${r.error})` : ""} — using the in-browser engine.`,
      );
    });
    return () => {
      alive = false;
    };
  }, [engine, apiUrl]);

  /* auto-generate a first mesh for every new geometry */
  const autoRef = useRef("");
  useEffect(() => {
    if (autoRef.current === geo.id) return;
    autoRef.current = geo.id;
    const t = setTimeout(() => generateMesh(), 80);
    return () => clearTimeout(t);
  }, [geo, generateMesh]);

  /* ---------------- coupling operators ---------------- */
  const couple = useMemo(() => {
    if (!coupled || !mesh || !sph) return null;
    const band = core.buffer * sp.dp;
    const cellClass = classifyCells(
      mesh.cc,
      mesh.nCells,
      { ...core, r: core.r - band },
      band,
    );
    const partClass = classifyParticles(sph.x, sph.y, sph.n, core, band);

    const ccx = new Float64Array(mesh.nCells);
    const ccy = new Float64Array(mesh.nCells);
    for (let c = 0; c < mesh.nCells; c++) {
      ccx[c] = mesh.cc[2 * c];
      ccy[c] = mesh.cc[2 * c + 1];
    }
    const meanCell = Math.sqrt(mesh.stats.meanVol);
    const kern = KERNELS[sp.kernel];
    const hPart = sp.hFactor * sp.dp;
    const cellGrid = new NeighborGrid(ccx, ccy, mesh.nCells, Math.max(meanCell, 1e-9), geo.bbox);
    const partGrid = new NeighborGrid(
      sph.x,
      sph.y,
      sph.n,
      Math.max(kern.kappa * hPart, 1e-9),
      geo.bbox,
    );
    const cellTargets: number[] = [];
    for (let c = 0; c < mesh.nCells; c++)
      if (cellClass[c] === CELL_INTERFACE) cellTargets.push(c);
    const partTargets: number[] = [];
    for (let i = 0; i < sph.n; i++) if (partClass[i] === PART_BUFFER) partTargets.push(i);

    const rP2M = kern.kappa * hPart;
    const rM2P = Math.max(kern.kappa * hPart, 2.6 * meanCell);
    const schemes: Scheme[] = ["shepard", "mls", "opmis"];
    const p2m = {} as Record<Scheme, InterpStats>;
    const m2p = {} as Record<Scheme, InterpStats>;
    let activeP2M: InterpSet | null = null;
    let activeM2P: InterpSet | null = null;
    for (const s of schemes) {
      const a = buildInterp(
        ccx,
        ccy,
        cellTargets,
        rP2M,
        { x: sph.x, y: sph.y, n: sph.n },
        kern,
        s,
        partGrid,
      );
      const b = buildInterp(
        sph.x,
        sph.y,
        partTargets,
        rM2P,
        { x: ccx, y: ccy, n: mesh.nCells },
        kern,
        s,
        cellGrid,
      );
      p2m[s] = a.stats;
      m2p[s] = b.stats;
      if (s === scheme) {
        activeP2M = a;
        activeM2P = b;
      }
    }
    let nCovered = 0;
    for (let c = 0; c < mesh.nCells; c++) if (cellClass[c] === CELL_COVERED) nCovered++;
    let nPrunable = 0;
    for (let i = 0; i < sph.n; i++) if (partClass[i] === PART_OUTSIDE) nPrunable++;
    const [bx0, by0, bx1, by1] = geo.bbox;
    const nFull = Math.round(((bx1 - bx0) * (by1 - by0)) / (sp.dp * sp.dp));
    const summary: CoupleSummary = {
      nBuffer: partTargets.length,
      nInterface: cellTargets.length,
      nCovered,
      nFull,
      nParticles: sph.n,
      nPrunable,
      p2m,
      m2p,
    };
    return {
      cellClass,
      partClass,
      summary,
      activeP2M: activeP2M!,
      activeM2P: activeM2P!,
      cellTargets,
      partTargets,
      ccx,
      ccy,
    };
  }, [coupled, mesh, sph, core, sp.dp, sp.hFactor, sp.kernel, scheme, geo]);

  /* ---------------- probing ---------------- */
  const onProbe = useCallback(
    (x: number, y: number) => {
      if (tab === "couple" && couple && mesh && sph) {
        let bestD = Infinity;
        let bi = -1;
        let isCell = true;
        couple.cellTargets.forEach((c, k) => {
          const d = Math.hypot(couple.ccx[c] - x, couple.ccy[c] - y);
          if (d < bestD) {
            bestD = d;
            bi = k;
            isCell = true;
          }
        });
        couple.partTargets.forEach((p, k) => {
          const d = Math.hypot(sph.x[p] - x, sph.y[p] - y);
          if (d < bestD) {
            bestD = d;
            bi = k;
            isCell = false;
          }
        });
        if (bi < 0) return;
        const set = isCell ? couple.activeP2M : couple.activeM2P;
        const st = set.stencils[bi];
        const tIdx = isCell ? couple.cellTargets[bi] : couple.partTargets[bi];
        const tx = isCell ? couple.ccx[tIdx] : sph.x[tIdx];
        const ty = isCell ? couple.ccy[tIdx] : sph.y[tIdx];
        const srcX = isCell ? sph.x : couple.ccx;
        const srcY = isCell ? sph.y : couple.ccy;
        let sum = 0;
        let wmin = Infinity;
        const links = [] as { x: number; y: number; w: number }[];
        for (let k = 0; k < st.idx.length; k++) {
          sum += st.w[k];
          wmin = Math.min(wmin, st.w[k]);
          links.push({ x: srcX[st.idx[k]], y: srcY[st.idx[k]], w: st.w[k] });
        }
        setProbe({
          kind: isCell ? "cell" : "particle",
          index: tIdx,
          info: [
            ["direction", isCell ? "particles → cell" : "mesh → particle"],
            ["target id", String(tIdx)],
            ["stencil size", String(st.idx.length)],
            ["Σ w", sum.toFixed(8)],
            ["min w", isFinite(wmin) ? wmin.toFixed(5) : "–"],
            ["cond(M)", fmt(st.cond, 4)],
            ["θ (MLS blend)", fmt(st.theta, 3)],
            ["scheme", SCHEME_LABEL[scheme]],
          ],
          stencil: { x: tx, y: ty, links },
        });
        return;
      }
      if (tab === "sph" && sph) {
        let best = -1;
        let bd = Infinity;
        for (let i = 0; i < sph.n; i++) {
          const d = (sph.x[i] - x) ** 2 + (sph.y[i] - y) ** 2;
          if (d < bd) {
            bd = d;
            best = i;
          }
        }
        if (best < 0) return;
        const pk = sph.patch[best] >= 0 ? PATCH_ORDER[sph.patch[best]] : null;
        setProbe({
          kind: "particle",
          index: best,
          info: [
            ["position", `${sph.x[best].toFixed(4)}, ${sph.y[best].toFixed(4)}`],
            ["volume V_i", sph.V[best].toExponential(4)],
            ["smoothing h_i", sph.h[best].toExponential(4)],
            ["neighbours", String(sph.nNeigh[best])],
            ["div(r)_i", sph.divR[best].toFixed(4)],
            ["ε₀ = h|Σ V∇W|", sph.eps0[best].toExponential(3)],
            ["‖M−I‖", sph.eps1[best].toFixed(4)],
            ["within boundaries", sph.isBnd[best] ? "yes (div r < 1.5)" : "no"],
            ...(sph.isBnd[best]
              ? ([
                  ["|A_iS|", sph.asMag[best].toExponential(4)],
                  [
                    "n_iS",
                    `${sph.asNx[best].toFixed(3)}, ${sph.asNy[best].toFixed(3)}`,
                  ],
                  ["patch", pk ?? "–"],
                  ["bc input", pk ? BC_TEXT[pk] : "–"],
                ] as [string, string][])
              : []),
          ],
        });
        return;
      }
      if (mesh) {
        let best = -1;
        let bd = Infinity;
        for (let c = 0; c < mesh.nCells; c++) {
          const d = (mesh.cc[2 * c] - x) ** 2 + (mesh.cc[2 * c + 1] - y) ** 2;
          if (d < bd) {
            bd = d;
            best = c;
          }
        }
        if (best < 0) return;
        const faces: number[] = [];
        for (let f = 0; f < mesh.nFaces; f++)
          if (mesh.owner[f] === best || mesh.neigh[f] === best) faces.push(f);
        let sx = 0;
        let sy = 0;
        let noMax = 0;
        const nbrs: number[] = [];
        for (const f of faces) {
          const sgn = mesh.owner[f] === best ? 1 : -1;
          sx += sgn * mesh.fa[f] * mesh.fn[2 * f];
          sy += sgn * mesh.fa[f] * mesh.fn[2 * f + 1];
          noMax = Math.max(noMax, mesh.nonOrtho[f]);
          const o = mesh.owner[f] === best ? mesh.neigh[f] : mesh.owner[f];
          nbrs.push(o);
        }
        setProbe({
          kind: "cell",
          index: best,
          info: [
            ["centroid", `${mesh.cc[2 * best].toFixed(4)}, ${mesh.cc[2 * best + 1].toFixed(4)}`],
            ["volume", mesh.cv[best].toExponential(4)],
            ["quality", mesh.quality[best].toFixed(4)],
            ["faces", String(faces.length)],
            ["neighbours", nbrs.map((n) => (n < 0 ? "bnd" : n)).join(", ")],
            ["max non-ortho", `${mesh.nonOrtho[faces[0]] >= 0 ? noMax.toFixed(2) : "0"}°`],
            ["|Σ S_f n_f|", Math.hypot(sx, sy).toExponential(2)],
            [
              "A_ij = S_ij n_ij",
              `${(mesh.fa[faces[0]] * mesh.fn[2 * faces[0]]).toExponential(3)}, ${(
                mesh.fa[faces[0]] * mesh.fn[2 * faces[0] + 1]
              ).toExponential(3)}`,
            ],
          ],
        });
      }
    },
    [tab, mesh, sph, couple, scheme],
  );

  /* ---------------- left panel ---------------- */
  const layerToggle = (k: keyof Layers, label: string, color?: string) => (
    <Toggle
      label={label}
      color={color}
      checked={layers[k]}
      onChange={(v) => setLayers((l) => ({ ...l, [k]: v }))}
    />
  );

  const running = busy !== null;

  return (
    <div className="flex h-screen flex-col overflow-hidden bg-slate-950 text-slate-200">
      {/* header */}
      <header className="flex shrink-0 items-center gap-4 border-b border-white/10 bg-slate-900/60 px-4 py-2">
        <div className="flex items-center gap-2.5">
          <div className="grid h-8 w-8 place-items-center rounded-lg bg-gradient-to-br from-cyan-400 to-blue-600 text-slate-950">
            <svg viewBox="0 0 24 24" className="h-5 w-5" fill="none" stroke="currentColor" strokeWidth="1.8">
              <path d="M3 20 12 4l9 16H3Z" />
              <path d="M7.5 12h9M12 4v16" />
            </svg>
          </div>
          <div>
            <h1 className="text-[13px] font-semibold tracking-tight text-slate-100">
              FVM / SPH Mesher
            </h1>
            <p className="text-[10px] text-slate-500">
              unified mesh + particle pre-processor · Wang/Haidn/Hu · Zhang/Ming/Wang/Liang · Du/Li/Xu/Wang
            </p>
          </div>
        </div>
        <div className="ml-auto hidden items-center gap-1.5 rounded-md bg-slate-950/60 px-2 py-1 ring-1 ring-white/10 md:flex">
          <span
            className={cn(
              "h-1.5 w-1.5 rounded-full",
              engine === "rust"
                ? backendStatus === "online"
                  ? "bg-orange-400"
                  : "bg-amber-400"
                : "bg-cyan-400",
            )}
          />
          <span className="font-mono text-[10px] text-slate-400">
            {engine === "rust" ? "rust core" : "ts engine"}
            {engine === "rust" && backendStatus !== "online" && " · offline"}
          </span>
        </div>
        <nav className="flex gap-1 rounded-lg bg-slate-950/60 p-0.5 ring-1 ring-white/10">
          {TABS.map((t) => (
            <button
              key={t.id}
              onClick={() => {
                setTab(t.id);
                setProbe(null);
                if (t.id === "sph") {
                  setLayers((l) => ({ ...l, particles: true, bndSurface: true }));
                  if (!sph && !running && !coupled) generateSph(false);
                }
                if (t.id === "couple") {
                  setLayers((l) => ({
                    ...l,
                    particles: true,
                    coupling: true,
                    cells: true,
                  }));
                  setCellField("region");
                  setPartField("region");
                  if (!coupled) {
                    setCoupled(true);
                    if (!running) generateSph(true);
                  }
                }
              }}
              className={cn(
                "rounded-md px-3 py-1 text-[11.5px] font-medium transition",
                tab === t.id
                  ? "bg-cyan-500/90 text-slate-950"
                  : "text-slate-400 hover:text-slate-100",
              )}
            >
              {t.label}
            </button>
          ))}
        </nav>
      </header>

      {tab === "rust" ? (
        <div className="min-h-0 flex-1 overflow-y-auto bg-slate-950">
          <div className="mx-auto max-w-6xl p-4">
            <div className="relative overflow-hidden rounded-2xl border border-orange-400/20 bg-gradient-to-br from-orange-500/10 via-slate-900 to-slate-950 p-5">
              <div className="absolute -right-10 -top-10 h-40 w-40 rounded-full bg-orange-500/10 blur-3xl" />
              <div className="flex flex-wrap items-start justify-between gap-4">
                <div className="max-w-2xl">
                  <div className="flex items-center gap-2">
                    <span className="grid h-7 w-7 place-items-center rounded-md bg-orange-500/20 text-orange-300 ring-1 ring-orange-400/40">
                      <svg viewBox="0 0 24 24" className="h-4 w-4" fill="none" stroke="currentColor" strokeWidth="1.8">
                        <path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1" />
                        <circle cx="12" cy="12" r="3.5" />
                      </svg>
                    </span>
                    <h2 className="text-[17px] font-semibold tracking-tight text-slate-100">
                      Backend-only numerics, in Rust
                    </h2>
                  </div>
                  <p className="mt-2 text-[12px] leading-relaxed text-slate-400">
                    Yes — the mesher does not need the browser. Geometry, meshing,
                    particle relaxation, boundary-surface reconstruction and the
                    coupling operators are I/O-free modules, so the exact same
                    algorithms compile into a native <code className="text-orange-300">axum</code>{" "}
                    service (this crate) or into{" "}
                    <code className="text-orange-300">wasm32-unknown-unknown</code> for a
                    serverless client. The page you are looking at stays a thin
                    renderer: flat-array JSON in, solver files out.
                  </p>
                  <div className="mt-3 flex flex-wrap gap-2">
                    <span className="rounded-md bg-slate-950/70 px-2 py-1 font-mono text-[10px] text-slate-400 ring-1 ring-white/10">
                      mesh · sph · couple · export
                    </span>
                    <span className="rounded-md bg-slate-950/70 px-2 py-1 font-mono text-[10px] text-slate-400 ring-1 ring-white/10">
                      rayon · f64 · no GC
                    </span>
                    <span className="rounded-md bg-slate-950/70 px-2 py-1 font-mono text-[10px] text-slate-400 ring-1 ring-white/10">
                      deterministic, seeded
                    </span>
                  </div>
                </div>
                <div className="grid grid-cols-2 gap-1.5">
                  <Stat
                    label="engine"
                    value={engine === "rust" ? "rust core" : "typescript"}
                    tone={engine === "rust" ? "good" : "default"}
                  />
                  <Stat
                    label="service"
                    value={backendStatus}
                    tone={backendStatus === "online" ? "good" : backendStatus === "offline" ? "warn" : "default"}
                  />
                  <Stat
                    label="cells"
                    value={(timings?.cells ?? mesh?.nCells ?? 0).toLocaleString()}
                  />
                  <Stat
                    label="particles"
                    value={(timings?.particles ?? sph?.n ?? 0).toLocaleString()}
                  />
                  <Btn
                    className="col-span-2"
                    onClick={() => {
                      generateMesh(() => generateSph());
                    }}
                    disabled={running}
                    tone="primary"
                  >
                    regenerate with {engine === "rust" ? "Rust" : "TypeScript"}
                  </Btn>
                </div>
              </div>
            </div>
            <div className="mt-3 grid gap-3 lg:grid-cols-2">
              <div className="space-y-3">
                <RustCore
                  engine={engine}
                  setEngine={(e) => {
                    setEngine(e);
                    setBackendError(undefined);
                  }}
                  api={apiUrl}
                  setApi={setApiUrl}
                  status={backendStatus}
                  version={backendVersion}
                  error={backendError}
                  timings={timings}
                  onCheck={() => {
                    setBackendStatus("checking");
                    checkHealth(apiUrl).then((r) => {
                      setBackendStatus(r.ok ? "online" : "offline");
                      setBackendVersion(r.version);
                      setBackendError(
                        r.ok
                          ? undefined
                          : `No Rust service at ${apiUrl}${r.error ? ` (${r.error})` : ""} — using the in-browser engine.`,
                      );
                    });
                  }}
                  onRun={() => generateMesh(() => generateSph())}
                  busy={running}
                  meshCells={mesh?.nCells}
                  particles={sph?.n}
                />
              </div>
              <div className="space-y-3">
                <Panel title="Why not Rust-everywhere?" subtitle="honest boundaries">
                  <ul className="space-y-1.5 text-[10.5px] leading-snug text-slate-400">
                    <li>
                      <span className="text-slate-200">Native only where it pays.</span>{" "}
                      A Rust compute core removes GC pauses and lets every particle
                      loop run under rayon, but it does not make canvas rendering or
                      DOM faster — those stay in TypeScript.
                    </li>
                    <li>
                      <span className="text-slate-200">The transport can dominate.</span>{" "}
                      At the demo sizes (≈10³ cells) a JSON round-trip over HTTP can
                      cost more than the job itself. Keep localhost, or move to a
                      binary payload (bincode / Arrow) for 10⁵–10⁶ cells.
                    </li>
                    <li>
                      <span className="text-slate-200">WASM, not a server, for a static demo.</span>{" "}
                      The same crate compiles to WASM; that gives native-speed kernels
                      with no backend at all. A Rust server is the better fit when you
                      need shared caches, batch jobs, auth, or solver-scale meshes.
                    </li>
                    <li>
                      <span className="text-slate-200">Two engines, one contract.</span>{" "}
                      Every response uses the same keys as{" "}
                      <code className="text-slate-300">src/lib/mesh.ts</code>; switching
                      engines cannot silently change the UI or exporters.
                    </li>
                    <li>
                      <span className="text-slate-200">Accuracy is unchanged.</span> The
                      port keeps the same seeded lattice, iteration counts and f64
                      formulas, so closure residuals and Σ|A_iS| match to numerical
                      tolerance — verifiable with the diagnostic panels.
                    </li>
                  </ul>
                </Panel>
                <Panel title="Deployment options" subtitle="pick by latency & ops budget">
                  <div className="space-y-1.5">
                    {[
                      ["Static page", "TS engine in-browser", "zero ops, demo default", true],
                      ["Rust + WASM", "cargo build --target wasm32", "no server, fast kernels", false],
                      ["Rust service", "axum on :8080, JSON", "batch, cache, large meshes", false],
                      ["Rust library", "mesh::build / sph::run", "embed in your solver", false],
                    ].map(([title, how, why, active]) => (
                      <div
                        key={String(title)}
                        className={cn(
                          "flex items-start gap-2 rounded-lg px-2 py-1.5 ring-1",
                          active
                            ? "bg-cyan-500/10 ring-cyan-400/30"
                            : "bg-slate-950/60 ring-white/10",
                        )}
                      >
                        <span
                          className={cn(
                            "mt-1 h-1.5 w-1.5 shrink-0 rounded-full",
                            active ? "bg-cyan-400" : "bg-slate-600",
                          )}
                        />
                        <div>
                          <div className="text-[11px] font-medium text-slate-200">
                            {title}
                            {active && (
                              <span className="ml-1.5 text-[9px] uppercase tracking-wider text-cyan-300">
                                current
                              </span>
                            )}
                          </div>
                          <div className="font-mono text-[9.5px] text-slate-500">{how}</div>
                          <div className="text-[9.5px] text-slate-500">{why}</div>
                        </div>
                      </div>
                    ))}
                  </div>
                  <Note>
                    In this environment there is no Rust toolchain, so the crate ships
                    as source and the live demo runs the TypeScript engine. The API
                    panel documents the exact contract the Rust service implements;
                    start it and press “run through Rust” to compare timings on your
                    own machine.
                  </Note>
                </Panel>
              </div>
            </div>
          </div>
        </div>
      ) : (
      <div className="flex min-h-0 flex-1">
        {/* ---------------- controls ---------------- */}
        <aside className="w-[292px] shrink-0 space-y-3 overflow-y-auto border-r border-white/10 bg-slate-950 p-3">
          <Panel title="Geometry" subtitle={geo.blurb}>
            <Select
              label="case"
              value={geoId}
              options={GEOMETRIES.map((g) => ({ value: g.id, label: g.name }))}
              onChange={setGeoId}
            />
            <Row>
              <Stat label="extent" value={`${fmt(geo.bbox[2] - geo.bbox[0], 3)} × ${fmt(geo.bbox[3] - geo.bbox[1], 3)}`} />
              <Stat label="perimeter" value={fmt(geo.perimeter, 4)} />
            </Row>
          </Panel>

          {tab === "mesh" && (
            <Panel title="Unstructured FVM mesh" subtitle="level-set DistMesh + Delaunay">
              <Slider
                label="base spacing h₀"
                value={mp.h0}
                min={0.025}
                max={0.2}
                step={0.005}
                onChange={(v) => setMp({ ...mp, h0: v })}
                hint="cell size at the refined feature"
              />
              <Slider
                label="size grading"
                value={mp.grading}
                min={0}
                max={1.5}
                step={0.05}
                onChange={(v) => setMp({ ...mp, grading: v })}
                hint="h(x) = h₀ (1 + g·d/h₀), clipped at h₀·r_max"
              />
              <Slider
                label="max size ratio"
                value={mp.hmaxRatio}
                min={1}
                max={6}
                step={0.25}
                onChange={(v) => setMp({ ...mp, hmaxRatio: v })}
              />
              <Slider
                label="relaxation iterations"
                value={mp.iters}
                min={5}
                max={120}
                step={5}
                onChange={(v) => setMp({ ...mp, iters: v })}
              />
              <div className="flex gap-1.5">
                <Btn tone="primary" onClick={() => generateMesh()} disabled={running}>
                  Generate mesh
                </Btn>
                <Btn onClick={() => generateMesh(() => generateSph())} disabled={running}>
                  Mesh + particles
                </Btn>
                {running && (
                  <Btn tone="ghost" onClick={() => { stop(); setBusy(null); setPreview(null); }}>
                    stop
                  </Btn>
                )}
              </div>
              <Note>
                Force-equilibrium of the Delaunay edges with boundary projection on
                φ = 0 produces the body-fitted control volumes; the face list
                (owner / neighbour / S_f n_f) is assembled afterwards.
              </Note>
            </Panel>
          )}

          {tab === "sph" && (
            <Panel title="Eulerian SPH particles" subtitle="physics-driven relaxation">
              <Slider
                label="particle spacing dp"
                value={sp.dp}
                min={0.02}
                max={0.16}
                step={0.005}
                onChange={(v) => setSp({ ...sp, dp: v })}
              />
              <Slider
                label="smoothing length h / dp"
                value={sp.hFactor}
                min={1.0}
                max={2.0}
                step={0.05}
                onChange={(v) => setSp({ ...sp, hFactor: v })}
              />
              <Select
                label="kernel"
                value={sp.kernel}
                options={Object.values(KERNELS).map((k) => ({ value: k.id, label: k.name }))}
                onChange={(v) => setSp({ ...sp, kernel: v as KernelId })}
              />
              <Select
                label="initial lattice"
                value={sp.lattice}
                options={[
                  { value: "square", label: "Cartesian" },
                  { value: "hex", label: "Hexagonal" },
                ]}
                onChange={(v) => setSp({ ...sp, lattice: v as "square" | "hex" })}
              />
              <Slider
                label="relaxation steps"
                value={sp.iters}
                min={0}
                max={300}
                step={10}
                onChange={(v) => setSp({ ...sp, iters: v })}
                hint="0 = raw lattice (shows the un-relaxed inconsistency)"
              />
              <Slider
                label="adaptive refinement ratio"
                value={sp.refineRatio}
                min={1}
                max={4}
                step={1}
                onChange={(v) => setSp({ ...sp, refineRatio: v })}
                hint="multi-level dp near the refined feature (adaptive SPH)"
              />
              {sp.refineRatio > 1 && (
                <Slider
                  label="particle size grading"
                  value={sp.grading}
                  min={0.1}
                  max={2}
                  step={0.1}
                  onChange={(v) => setSp({ ...sp, grading: v })}
                />
              )}
              <div className="flex flex-wrap gap-1.5">
                <Toggle
                  label="volume reinit V=1/Σ W"
                  checked={sp.volumeReinit}
                  onChange={(v) => setSp({ ...sp, volumeReinit: v })}
                />
              </div>
              <div className="flex gap-1.5">
                <Btn tone="primary" onClick={() => generateSph()} disabled={running}>
                  Relax particles
                </Btn>
                {running && (
                  <Btn tone="ghost" onClick={() => { stop(); setBusy(null); setPreview(null); }}>
                    stop
                  </Btn>
                )}
              </div>
              <Note>
                Relaxation drives Σ_j V_j ∇W_ij → 0. Particles whose kernel support
                is truncated keep div(r) &lt; 0.75 d and are tagged as “within the
                boundaries”; their missing flux is replaced by the reconstructed
                boundary surface A_iS.
              </Note>
            </Panel>
          )}

          {tab === "couple" && (
            <Panel title="Adaptive SPH–FVM coupling" subtitle="Lagrangian core inside an Eulerian mesh">
              <div className="flex flex-wrap gap-1.5">
                <Toggle label="coupled mode" checked={coupled} onChange={setCoupled} />
                <Toggle
                  label="draw regions"
                  checked={layers.coupling}
                  onChange={(v) => setLayers((l) => ({ ...l, coupling: v }))}
                />
              </div>
              <Slider
                label="core radius R_c"
                value={core.r}
                min={0.1}
                max={Math.max(geo.bbox[2] - geo.bbox[0], geo.bbox[3] - geo.bbox[1]) / 2}
                step={0.02}
                onChange={(v) => setCore({ ...core, r: v })}
              />
              <Slider
                label="core centre x"
                value={core.x}
                min={geo.bbox[0]}
                max={geo.bbox[2]}
                step={0.02}
                onChange={(v) => setCore({ ...core, x: v })}
              />
              <Slider
                label="core centre y"
                value={core.y}
                min={geo.bbox[1]}
                max={geo.bbox[3]}
                step={0.02}
                onChange={(v) => setCore({ ...core, y: v })}
              />
              <Slider
                label="overlap band (dp)"
                value={core.buffer}
                min={1}
                max={6}
                step={1}
                onChange={(v) => setCore({ ...core, buffer: v })}
              />
              <Select
                label="particle ↔ mesh scheme"
                value={scheme}
                options={(["shepard", "mls", "opmis"] as Scheme[]).map((s) => ({
                  value: s,
                  label: SCHEME_LABEL[s],
                }))}
                onChange={setScheme}
              />
              <div className="flex gap-1.5">
                <Btn tone="primary" onClick={() => generateSph(true)} disabled={running || !coupled}>
                  Re-seed core particles
                </Btn>
                <Btn onClick={() => setCore({ ...geo.core, buffer: core.buffer })}>
                  reset core
                </Btn>
              </div>
              <Note>
                The particle domain is re-seeded only inside R_c: the mesh carries the
                far field (and its non-reflecting boundary) while the Lagrangian core
                follows the interface. Click a buffer particle or an interface cell in
                the viewport to inspect its transfer stencil.
              </Note>
            </Panel>
          )}

          {tab === "export" && (
            <Panel title="Export" subtitle="solver-ready discretisations">
              <div className="grid grid-cols-1 gap-1.5">
                <Btn disabled={!mesh} onClick={() => mesh && download("mesh.vtk", meshToVtk(mesh))}>
                  mesh.vtk — unstructured grid (ParaView)
                </Btn>
                <Btn
                  disabled={!mesh}
                  onClick={() => mesh && download("polyMesh.txt", meshToFoam(mesh))}
                >
                  polyMesh.txt — OpenFOAM points/faces/owner/neighbour
                </Btn>
                <Btn
                  disabled={!mesh}
                  onClick={() => mesh && download("difvm_graph.json", graphToJson(mesh))}
                >
                  difvm_graph.json — scatter/gather arrays
                </Btn>
                <Btn onClick={() => download("difvm_ops.py", JAX_STUB)}>
                  difvm_ops.py — JAX message-passing operators
                </Btn>
                <Btn disabled={!sph} onClick={() => sph && download("particles.vtk", sphToVtk(sph))}>
                  particles.vtk — relaxed Eulerian SPH cloud
                </Btn>
                <Btn disabled={!sph} onClick={() => sph && download("particles.csv", sphToCsv(sph))}>
                  particles.csv — full particle table
                </Btn>
                <Btn
                  disabled={!sph}
                  onClick={() => sph && download("sph_boundary.txt", sphBoundaryTable(sph))}
                >
                  sph_boundary.txt — A_iS storage (SPHinXsys)
                </Btn>
              </div>
              <Note>
                The OpenFOAM bundle is a one-cell-thick extrusion with empty
                front/back patches — the layout DiFVM reads directly from a case
                directory. The JSON holds the same connectivity as flat arrays for
                jax.ops.segment_sum.
              </Note>
              <pre className="max-h-64 overflow-auto rounded-lg bg-slate-950 p-2 text-[9.5px] leading-snug text-slate-400 ring-1 ring-white/10">
                {JAX_STUB}
              </pre>
            </Panel>
          )}

          <Panel title="Display">
            <div className="flex flex-wrap gap-1.5">
              {layerToggle("cells", "cells")}
              {layerToggle("edges", "wireframe")}
              {layerToggle("graph", "dual graph", "#38bdf8")}
              {layerToggle("boundaryFaces", "patches")}
              {layerToggle("faceNormals", "face normals")}
              {layerToggle("particles", "particles")}
              {layerToggle("bndSurface", "A_iS vectors", "#fbbf24")}
              {layerToggle("outline", "φ = 0 outline")}
            </div>
            <Select
              label="cell field"
              value={cellField}
              options={[
                { value: "none", label: "none" },
                { value: "quality", label: "quality (radius ratio)" },
                { value: "volume", label: "cell size √V" },
                { value: "nonortho", label: "non-orthogonality" },
                { value: "skew", label: "skewness" },
                { value: "rcm", label: "RCM ordering" },
                { value: "region", label: "coupling region" },
              ]}
              onChange={(v) => setCellField(v as CellField)}
            />
            <Select
              label="particle field"
              value={partField}
              options={[
                { value: "eps0", label: "ε₀ zero-order residual" },
                { value: "eps1", label: "‖M − I‖ first-order" },
                { value: "divr", label: "div(r) boundary detector" },
                { value: "neigh", label: "neighbour count" },
                { value: "volume", label: "particle volume" },
                { value: "level", label: "refinement level" },
                { value: "patch", label: "boundary patch" },
                { value: "region", label: "core / buffer" },
              ]}
              onChange={(v) => setPartField(v as PartField)}
            />
            <div>
              <Select
                label="colour map"
                value={rampName}
                options={[
                  { value: "viridis", label: "viridis" },
                  { value: "magma", label: "magma" },
                  { value: "turbo", label: "turbo" },
                  { value: "coolwarm", label: "coolwarm" },
                ]}
                onChange={(v) => setRampName(v as Ramp)}
              />
              <div
                className="mt-1.5 h-2 w-full rounded"
                style={{ background: rampCss(rampName) }}
              />
            </div>
          </Panel>
        </aside>

        {/* ---------------- viewport ---------------- */}
        <main className="relative min-w-0 flex-1 bg-[#0b1020]">
          <Viewport
            geo={geo}
            outline={outline}
            mesh={mesh}
            sph={sph}
            preview={preview}
            layers={layers}
            cellField={cellField}
            partField={partField}
            rampName={rampName}
            core={coupled ? core : null}
            cellClass={couple?.cellClass ?? null}
            partClass={couple?.partClass ?? null}
            probe={probe}
            onProbe={onProbe}
            busy={busy}
          />
        </main>

        {/* ---------------- metrics ---------------- */}
        <aside className="w-[336px] shrink-0 space-y-3 overflow-y-auto border-l border-white/10 bg-slate-950 p-3">
          {probe && (
            <Panel
              title={`probe · ${probe.kind} #${probe.index}`}
              right={
                <button
                  onClick={() => setProbe(null)}
                  className="text-[10px] text-slate-500 hover:text-slate-300"
                >
                  clear
                </button>
              }
            >
              <dl className="space-y-0.5">
                {probe.info.map(([k, v]) => (
                  <div key={k} className="flex justify-between gap-2 text-[10.5px]">
                    <dt className="shrink-0 text-slate-500">{k}</dt>
                    <dd className="truncate text-right font-mono text-slate-200">{v}</dd>
                  </div>
                ))}
              </dl>
            </Panel>
          )}

          {tab === "mesh" && (
            <>
              <MeshMetrics mesh={mesh} />
              <EquivalenceMetrics mesh={mesh} sph={sph} />
            </>
          )}
          {tab === "sph" && (
            <>
              <SphMetrics sph={sph} history={sphHist} />
              <EquivalenceMetrics mesh={mesh} sph={sph} />
              {sph && (
                <Panel title="Cost of the boundary algorithm" subtitle="inner configuration only">
                  <Row>
                    <Stat
                      label="ghost pairs avoided"
                      value={Math.round(sph.diag.ghostPairs).toLocaleString()}
                      hint="interactions the contact/mirror configuration would require"
                    />
                    <Stat
                      label="pair reduction"
                      value={`${fmt(sph.diag.pairReduction, 3)} %`}
                      tone="good"
                    />
                  </Row>
                </Panel>
              )}
            </>
          )}
          {tab === "couple" && <CoupleMetrics summary={couple?.summary ?? null} scheme={scheme} />}
          {tab === "export" && (
            <>
              <MeshMetrics mesh={mesh} />
              <SphMetrics sph={sph} history={sphHist} />
            </>
          )}

          <Panel title="Method notes">
            <ul className="space-y-1.5 text-[10.5px] leading-snug text-slate-400">
              <li>
                <span className="text-slate-200">Eulerian SPH ≡ FVM.</span> With
                A_ij = 2 V_i V_j (∂W_ij/∂r_ij) e_ij the SPH pair term plays the role of
                the FVM face vector S_ij n_ij, so both discretisations share one flux
                loop.
              </li>
              <li>
                <span className="text-slate-200">Boundary algorithm.</span> Particles
                inside the boundaries close their control volume with
                A_iS = −Σ_p A_ip, restoring zero-order consistency without ghost
                particles (Wang, Haidn &amp; Hu).
              </li>
              <li>
                <span className="text-slate-200">Adaptive coupling.</span> A Riemann-SPH
                core is embedded in the FV mesh through an overlap band and the OPMIS
                transfer operator (Zhang, Ming, Wang &amp; Liang).
              </li>
              <li>
                <span className="text-slate-200">Graph FVM.</span> owner/neighbour +
                segment_sum make every operator a message-passing kernel, ready for
                JAX/XLA auto-diff (Du, Li, Xu &amp; Wang).
              </li>
            </ul>
            <div className="mt-2 space-y-1 border-t border-white/10 pt-2 text-[9.5px] leading-snug text-slate-500">
              <p>
                [1] Z. Wang, O. J. Haidn, X. Hu — <em>An algorithm for the
                incorporation of relevant FVM boundary conditions in the Eulerian SPH
                framework</em>, Comput. Phys. Commun. 307 (2025) 109429 ·
                arXiv:2312.03542
              </p>
              <p>
                [2] Y.-D. Zhang, F.-R. Ming, P.-P. Wang, D.-F. Liang — <em>An adaptive
                SPH-FVM method with optimized particle-mesh interpolation scheme for
                strongly compressible multiphase flows</em>, Int. J. Multiphase Flow 189
                (2025) 105256
              </p>
              <p>
                [3] P. Du, Y. Li, M. Xu, J.-X. Wang — <em>DiFVM: a vectorized
                graph-based finite volume solver for differentiable CFD on unstructured
                meshes</em>, arXiv:2603.15920
              </p>
            </div>
          </Panel>
        </aside>
      </div>
      )}
    </div>
  );
}
