import { Workbench } from "@/components/workbench";
import { PRESETS } from "@/lib/params";
import { listSimulations } from "@/lib/service";
import type { PresetId, RunSummary } from "@/lib/types";

export const dynamic = "force-dynamic";

const PRESET_ORDER: PresetId[] = ["cylinder", "shock_tube", "explosion", "vortex", "box_obstacle"];

async function loadRuns(): Promise<RunSummary[]> {
  try {
    const rows = await listSimulations(8);
    return rows.map((row) => ({
      id: row.id,
      meshId: row.meshId,
      params: row.params as RunSummary["params"],
      meta: row.meta,
      stats: row.stats,
      interpolation: row.interpolation,
      computeMs: row.computeMs,
      createdAt: row.createdAt.toISOString(),
    }));
  } catch {
    return [];
  }
}

const ENDPOINTS: { method: string; path: string; summary: string }[] = [
  { method: "GET", path: "/api/health", summary: "Native engine capabilities, OpenMP thread count, PostgreSQL probe." },
  { method: "GET", path: "/api/presets", summary: "Presets, numeric limits, discretisation summary." },
  { method: "POST", path: "/api/meshes", summary: "Generate + persist a Voronoi FVM mesh (geometry, CSR graph, quality audit)." },
  { method: "GET", path: "/api/meshes", summary: "List cached meshes by fingerprint." },
  { method: "GET", path: "/api/meshes/:id", summary: "Fetch a stored mesh with full geometry payload." },
  { method: "POST", path: "/api/simulations", summary: "Mesh + HLLC FVM run: snapshots, forces, interpolation operator, dJ/dθ." },
  { method: "GET", path: "/api/simulations", summary: "Recent runs with solver telemetry." },
  { method: "GET", path: "/api/simulations/:id", summary: "Full run payload including mesh geometry." },
  { method: "POST", path: "/api/diagnostics", summary: "Uniform-state residual certification (free-stream preservation)." },
];

const PAPER_ROWS: { paper: string; implementation: string }[] = [
  {
    paper: "Wang, Haidn & Hu — FVM boundary algorithm inside Eulerian SPH (J. Comput. Phys. / CPC 2025)",
    implementation:
      "Zero-order consistency deficiency χ computed per cell from a Wendland C2 kernel including virtual wall particles; wall states are reconstructed with a deficiency-scaled mirror extrapolation of the least-squares boundary gradient, so a uniform state is preserved exactly.",
  },
  {
    paper: "Zhang & Ming — adaptive SPH–FVM with optimised particle–mesh interpolation",
    implementation:
      "Support-normalised, partition-of-unity P2M/M2P transfer operator on a uniform support grid. Weights are conservative by construction; sparsity, stencil size, L2/L∞ and conservation error are reported per run.",
  },
  {
    paper: "Du, Li, Xu & Wang — DiFVM vectorised graph-based differentiable FVM",
    implementation:
      "CSR cell→face adjacency, face-contiguous flux array and race-free gather/scatter residual kernel (OpenMP). Goal-function gradients dJ/dMach and dJ/dκ are evaluated through the vectorised kernel.",
  },
];

export default async function HomePage() {
  const runs = await loadRuns();
  const presets = PRESET_ORDER.map((id) => PRESETS[id]);

  return (
    <main className="mx-auto max-w-[1500px] px-4 pb-16 pt-6 sm:px-6">
      <header className="panel relative overflow-hidden p-5 sm:p-7">
        <div className="pointer-events-none absolute inset-y-0 right-0 hidden w-1/2 opacity-30 lg:block"
          style={{
            backgroundImage:
              "repeating-linear-gradient(90deg, rgba(69,224,208,0.22) 0 1px, transparent 1px 26px), repeating-linear-gradient(0deg, rgba(184,242,74,0.14) 0 1px, transparent 1px 22px)",
            maskImage: "linear-gradient(90deg, transparent, black)",
          }}
        />
        <div className="flex flex-wrap items-start justify-between gap-6">
          <div className="max-w-3xl">
            <div className="flex flex-wrap items-center gap-2">
              <span className="chip !border-[#b8f24a]/40 !text-[#d7ffa1]">backend · native c++17</span>
              <span className="chip">openmp</span>
              <span className="chip">drizzle + postgres</span>
              <span className="chip">next app router api</span>
            </div>
            <h1 className="mt-4 text-[clamp(1.7rem,3.4vw,2.6rem)] font-semibold leading-[1.05] tracking-tight">
              FVM / SPH Mesher
            </h1>
            <p className="mt-2 max-w-2xl text-[13.5px] leading-relaxed text-[#93aaab]">
              A performance-first backend that turns SPH particle clouds into conforming
              finite-volume meshes, solves the compressible Euler equations with HLLC fluxes on the
              resulting cell graph, and certifies the Eulerian-SPH boundary algorithm against FVM
              free-stream preservation. No browser-side numerics: everything below is computed by
              the C++ core and persisted through PostgreSQL.
            </p>
          </div>
          <dl className="grid grid-cols-3 gap-2 sm:gap-3">
            {[
              { k: "mesher", v: "Voronoi + Lloyd" },
              { k: "flux", v: "HLLC / MUSCL" },
              { k: "graph", v: "CSR scatter" },
              { k: "boundary", v: "ESPH-consistent" },
              { k: "kernels", v: "Wendland C2" },
              { k: "themes", v: "3 papers" },
            ].map((item) => (
              <div key={item.k} className="panel-inset px-3 py-2">
                <dt className="label">{item.k}</dt>
                <dd className="mono mt-1 text-[11.5px] text-[#d9e7e5]">{item.v}</dd>
              </div>
            ))}
          </dl>
        </div>
      </header>

      <div className="mt-4">
        <Workbench presets={presets} initialRuns={runs} />
      </div>

      <section className="mt-4 grid grid-cols-1 gap-3 lg:grid-cols-[1.35fr_1fr]">
        <div className="panel p-4">
          <h2 className="mono text-[12px] tracking-[0.16em] text-[#b8f24a]">REST SURFACE</h2>
          <p className="mono mt-1 text-[10.5px] text-[#6d888b]">
            JSON in / JSON out. Validation guards cell-step budgets; native runs are off the Node event loop.
          </p>
          <div className="mt-3 overflow-x-auto scroll-thin">
            <table className="data">
              <thead>
                <tr>
                  <th>method</th>
                  <th>path</th>
                  <th>purpose</th>
                </tr>
              </thead>
              <tbody>
                {ENDPOINTS.map((endpoint) => (
                  <tr key={`${endpoint.method}-${endpoint.path}`}>
                    <td className={endpoint.method === "POST" ? "text-[#b8f24a]" : "text-[#45e0d0]"}>{endpoint.method}</td>
                    <td className="whitespace-nowrap">{endpoint.path}</td>
                    <td className="text-[#93aaab]">{endpoint.summary}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <pre className="panel-inset mono mt-3 overflow-x-auto p-3 text-[10.5px] leading-relaxed text-[#a9c6c4]">
{`curl -X POST $HOST/api/simulations \\
  -H 'content-type: application/json' \\
  -d '{
    "mesh":   { "preset": "cylinder", "target": 2400 },
    "solver": { "steps": 320, "mach": 2, "order": 1,
                "wallClosure": "sph", "sensitivity": true }
  }'`}
          </pre>
        </div>

        <div className="panel p-4">
          <h2 className="mono text-[12px] tracking-[0.16em] text-[#45e0d0]">PAPER → CODE MAP</h2>
          <div className="mt-3 flex flex-col gap-2">
            {PAPER_ROWS.map((row) => (
              <div key={row.paper} className="panel-inset p-3">
                <p className="text-[11.5px] font-semibold leading-snug text-[#d9e7e5]">{row.paper}</p>
                <p className="mt-1 text-[11px] leading-relaxed text-[#8aa4a5]">{row.implementation}</p>
              </div>
            ))}
          </div>
          <p className="mono mt-3 text-[10px] leading-relaxed text-[#5f777a]">
            Native sources: native/src/mesh.cpp (Voronoi mesher, kernel consistency, P2M operator),
            native/src/solver.cpp (HLLC graph solver, boundary closure, sensitivities, serialiser).
          </p>
        </div>
      </section>

      <footer className="mono mt-6 flex flex-wrap items-center justify-between gap-2 text-[10px] text-[#4f6669]">
        <span>FVM / SPH Mesher · native C++17 core driven by Next.js route handlers</span>
        <span>units are non-dimensional · solver uses SSP-RK2 + HLLC</span>
      </footer>
    </main>
  );
}
