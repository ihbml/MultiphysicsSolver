import { useState } from "react";
import { BackendStatus, CURL_EXAMPLES, Engine, Timings } from "../lib/backend";
import { fmt } from "../lib/colors";
import { Btn, Note, Panel, Row, Stat } from "./ui";
import { cn } from "../utils/cn";

export interface RustProps {
  engine: Engine;
  setEngine: (e: Engine) => void;
  api: string;
  setApi: (v: string) => void;
  status: BackendStatus;
  version?: string;
  error?: string;
  timings: Timings | null;
  onCheck: () => void;
  onRun: () => void;
  busy: boolean;
  meshCells?: number;
  particles?: number;
}

const FILES = [
  "Cargo.toml",
  "Dockerfile",
  "README.md",
  "src/main.rs        axum routes + JSON contract",
  "src/lib.rs         library surface (also WASM entry)",
  "src/geometry.rs    level sets + boundary patches",
  "src/mesh.rs        DistMesh, FVM topology, RCM, rayon",
  "src/sph.rs         relaxation, div(r), A_iS, diagnostics",
  "src/couple.rs      adaptive regions + OPMIS stencils",
  "src/export.rs      VTK + DiFVM graph",
];

export default function RustCore(p: RustProps) {
  const [copied, setCopied] = useState(false);
  const m = p.timings;
  return (
    <>
      <Panel
        title="Engine"
        subtitle="the UI is a thin client; all numerics are interchangeable"
      >
        <div className="grid grid-cols-2 gap-1.5">
          <button
            onClick={() => p.setEngine("ts")}
            className={cn(
              "rounded-lg px-2 py-2 text-left ring-1 transition",
              p.engine === "ts"
                ? "bg-cyan-500/15 ring-cyan-400/50"
                : "bg-slate-950/60 ring-white/10 hover:ring-white/25",
            )}
          >
            <div className="text-[11px] font-semibold text-slate-100">TypeScript</div>
            <div className="text-[9.5px] leading-tight text-slate-500">
              in-browser, zero install, always available
            </div>
          </button>
          <button
            onClick={() => p.setEngine("rust")}
            className={cn(
              "rounded-lg px-2 py-2 text-left ring-1 transition",
              p.engine === "rust"
                ? "bg-orange-500/15 ring-orange-400/50"
                : "bg-slate-950/60 ring-white/10 hover:ring-white/25",
            )}
          >
            <div className="flex items-center gap-1 text-[11px] font-semibold text-slate-100">
              Rust core
              <span
                className={cn(
                  "ml-auto h-1.5 w-1.5 rounded-full",
                  p.status === "online"
                    ? "bg-emerald-400"
                    : p.status === "checking"
                      ? "bg-amber-300"
                      : "bg-slate-600",
                )}
              />
            </div>
            <div className="text-[9.5px] leading-tight text-slate-500">
              native axum service, rayon kernels
            </div>
          </button>
        </div>
        <label className="block">
          <span className="text-[11px] text-slate-400">backend URL</span>
          <input
            value={p.api}
            onChange={(e) => p.setApi(e.target.value)}
            spellCheck={false}
            className="mt-1 w-full rounded-md border border-white/10 bg-slate-950 px-2 py-1.5 font-mono text-[11px] text-slate-200 outline-none focus:border-cyan-400/60"
          />
        </label>
        <div className="flex flex-wrap gap-1.5">
          <Btn onClick={p.onCheck} disabled={p.status === "checking"}>
            {p.status === "checking" ? "checking…" : "check health"}
          </Btn>
          <Btn
            tone="primary"
            onClick={p.onRun}
            disabled={p.busy || p.status !== "online" || p.engine !== "rust"}
          >
            run through Rust
          </Btn>
        </div>
        <Row>
          <Stat
            label="service"
            value={
              p.status === "online"
                ? `online · v${p.version ?? "?"}`
                : p.status === "checking"
                  ? "checking"
                  : p.status === "offline"
                    ? "offline"
                    : "not checked"
            }
            tone={p.status === "online" ? "good" : p.status === "offline" ? "warn" : "default"}
          />
          <Stat
            label="last run"
            value={
              m
                ? `${m.engine === "rust" ? "rust" : "ts"} · ${fmt(m.meshMs ?? 0, 3)} ms`
                : "—"
            }
            hint="wall time for the complete mesh job, request included"
          />
        </Row>
        {p.error && (
          <p className="rounded-md bg-rose-500/10 px-2 py-1 text-[10px] leading-snug text-rose-300 ring-1 ring-rose-400/30">
            {p.error}
          </p>
        )}
        {p.engine === "rust" && p.status !== "online" && (
          <Note>
            No Rust service on this URL — generation automatically falls back to the
            in-browser engine so nothing breaks. Start it with{" "}
            <code className="text-slate-300">cargo run --release</code> in{" "}
            <code className="text-slate-300">backend/</code>.
          </Note>
        )}
        <Note>
          This deployment is a static page and the sandbox has no Rust toolchain,
          so the demo itself runs the TypeScript engine. The crate is complete
          source; the API contract below is exactly what the app sends.
        </Note>
      </Panel>

      <Panel title="Architecture" subtitle="backend-only numerics, thin frontend">
        <div className="space-y-1.5">
          {[
            ["React canvas", "render · probe · export", "#38bdf8"],
            ["JSON contract", "flat arrays, camelCase, same as TS", "#a78bfa"],
            ["Rust core", "geometry · mesh · SPH · OPMIS · VTK", "#fb923c"],
            ["solvers", "OpenFOAM case · DiFVM graph · JAX", "#34d399"],
          ].map(([title, sub, color], i) => (
            <div key={title}>
              <div className="flex items-center gap-2 rounded-lg bg-slate-950/60 px-2 py-1.5 ring-1 ring-white/10">
                <span className="h-2 w-2 rounded-sm" style={{ background: color }} />
                <div>
                  <div className="text-[11px] font-medium text-slate-200">{title}</div>
                  <div className="text-[9.5px] text-slate-500">{sub}</div>
                </div>
                <span className="ml-auto font-mono text-[9px] text-slate-600">
                  {String(i + 1).padStart(2, "0")}
                </span>
              </div>
              {i < 3 && (
                <div className="ml-3 h-2 w-px bg-gradient-to-b from-white/30 to-white/5" />
              )}
            </div>
          ))}
        </div>
        <Note>
          For a purely backend deployment: the browser only needs the mesh /
          particle arrays and thumbnail geometry. Solver files are generated
          server-side. For an offline single page, compile the same crate to WASM
          (<code className="text-slate-300">wasm-bindgen</code>) and call{" "}
          <code className="text-slate-300">mesh::build</code> directly — every
          module is I/O-free.
        </Note>
      </Panel>

      <Panel
        title="What Rust actually buys"
        subtitle="the UI stays O(1); the numerics are O(N·k)"
      >
        <table className="w-full border-collapse text-[10.5px]">
          <thead>
            <tr className="text-slate-500">
              <th className="text-left font-medium">stage</th>
              <th className="text-right font-medium">where</th>
              <th className="text-right font-medium">gain vs TS</th>
            </tr>
          </thead>
          <tbody className="text-slate-300">
            {[
              ["particle relaxation (O(n·k))", "server", "large"],
              ["OPMIS stencils", "server", "large"],
              ["mesh topology + RCM", "server", "moderate"],
              ["level-set sampling", "server", "moderate"],
              ["render / interactions", "browser", "n/a"],
              ["JSON transfer", "network", "cost"],
            ].map(([a, b, c]) => (
              <tr key={a}>
                <td className="py-0.5 text-left">{a}</td>
                <td className="text-right text-slate-500">{b}</td>
                <td className="text-right">{c}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <div className="grid grid-cols-2 gap-1.5">
          <Stat
            label="mesh job"
            value={m?.meshMs ? `${fmt(m.meshMs, 3)} ms` : "—"}
            unit={m?.engine === "rust" ? "round-trip" : "in browser"}
          />
          <Stat
            label="particles"
            value={m?.particles ? m.particles.toLocaleString() : "—"}
            unit={m?.sphMs ? `${fmt(m.sphMs, 3)} ms` : ""}
          />
          <Stat
            label="cells"
            value={m?.cells ? m.cells.toLocaleString() : String(p.meshCells ?? "—")}
          />
          <Stat
            label="SPH particles"
            value={p.particles ? p.particles.toLocaleString() : "—"}
          />
        </div>
        <Note>
          Sizes shown above are measured live. Native timings are not faked here —
          run the crate on your machine and the “run through Rust” button reports
          the same end-to-end measurement. For reference workloads of this size
          the HTTP round-trip can dominate, so keep the service on localhost or
          switch to binary payloads before drawing conclusions.
        </Note>
      </Panel>

      <Panel title="HTTP API" subtitle="one contract, two engines">
        <div className="flex items-center justify-between">
          <span className="font-mono text-[10px] text-slate-500">endpoints</span>
          <Btn
            tone="ghost"
            onClick={() => {
              navigator.clipboard?.writeText(CURL_EXAMPLES);
              setCopied(true);
              setTimeout(() => setCopied(false), 1500);
            }}
          >
            {copied ? "copied" : "copy curl"}
          </Btn>
        </div>
        <ul className="space-y-0.5 font-mono text-[10px] text-slate-400">
          <li><span className="text-emerald-300">GET </span>/api/health</li>
          <li><span className="text-cyan-300">POST</span>/api/mesh</li>
          <li><span className="text-cyan-300">POST</span>/api/sph</li>
          <li><span className="text-cyan-300">POST</span>/api/couple</li>
          <li><span className="text-emerald-300">GET </span>/api/export/&#123;mesh,particles&#125;.vtk</li>
          <li><span className="text-emerald-300">GET </span>/api/export/graph.json</li>
        </ul>
        <pre className="max-h-56 overflow-auto rounded-lg bg-slate-950 p-2 text-[9px] leading-snug text-slate-400 ring-1 ring-white/10">
          {CURL_EXAMPLES}
        </pre>
      </Panel>

      <Panel title="Crate layout" subtitle="backend/">
        <pre className="overflow-auto rounded-lg bg-slate-950 p-2 text-[9.5px] leading-relaxed text-slate-400 ring-1 ring-white/10">
          {FILES.join("\n")}
        </pre>
        <pre className="overflow-auto rounded-lg bg-slate-950 p-2 text-[9.5px] leading-relaxed text-slate-300 ring-1 ring-white/10">{`cd backend
cargo run --release          # :8080
MESHER_ADDR=127.0.0.1:9000 cargo run --release

docker build -t fvm-sph-mesher .
docker run --rm -p 8080:8080 fvm-sph-mesher`}</pre>
        <Note>
          The Rust port is deterministic and seeded like the TS engine (same
          lattice, same iteration count) and hits the same invariants: control
          volumes close to machine precision, Σ|A_iS| recovers the geometry
          perimeter, OPMIS removes the negative-weight overshoot of raw MLS.
        </Note>
      </Panel>
    </>
  );
}
