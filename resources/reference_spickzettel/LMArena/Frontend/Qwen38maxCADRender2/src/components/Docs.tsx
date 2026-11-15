import { memo, useEffect, useRef, useState } from "react";
import { useEngine } from "@/state/engine";
import { cn } from "@/utils/cn";
import { Reveal } from "./Chrome";

function useInView<T extends HTMLElement>() {
  const ref = useRef<T>(null);
  const [seen, setSeen] = useState(false);
  useEffect(() => {
    const el = ref.current;
    if (!el || seen) return;
    const io = new IntersectionObserver(
      ([e]) => {
        if (e.isIntersecting) {
          setSeen(true);
          io.disconnect();
        }
      },
      { threshold: 0.25 },
    );
    io.observe(el);
    return () => io.disconnect();
  }, [seen]);
  return { ref, seen };
}

/* ------------------------------------------------------------------ */
/* Architecture stack                                                  */
/* ------------------------------------------------------------------ */

const LAYERS = [
  {
    crate: "addons/*",
    title: "Addon crates",
    n: "12 shipped",
    color: "#8ede63",
    body: "Dynamic libraries loaded from manifests. Each declares components, systems and egui panels; the registry resolves the DAG and mounts them into the running World.",
  },
  {
    crate: "anvil_shell_egui",
    title: "Immediate-mode shell",
    n: "≈ 6k LOC",
    color: "#8b7fd6",
    body: "Docks, toolbars, inspectors and the gizmo layer. egui renders into its own wgpu pass and is composited over the CAD target, so panels never block viewport input.",
  },
  {
    crate: "anvil_core · bevy 0.16",
    title: "ECS + schedule",
    n: "archetypal",
    color: "#35c9e8",
    body: "Geometry, materials, constraints and UI state are all entities. Stage labels PreCad / Cad / PostCad let addons order themselves without knowing each other.",
  },
  {
    crate: "anvil_render",
    title: "Render graph & tessellation",
    n: "≈ 14k LOC",
    color: "#f5a524",
    body: "B-rep tessellation, frustum + occlusion culling, section clipping, PSO cache and the frame graph. Addons insert nodes by label, never by patching the core.",
  },
  {
    crate: "wgpu 24",
    title: "Portable HAL",
    n: "1 API",
    color: "#4fd2ee",
    body: "Buffers, bind groups, render and compute passes. One abstraction over every vendor API, with a feature table negotiated at device creation.",
  },
  {
    crate: "Vulkan 1.3 · DX12 · Metal · GL",
    title: "Native drivers",
    n: "4 backends",
    color: "#ff6b5a",
    body: "ash for Vulkan, windows-rs for DX12 (AgilitySDK + enhanced barriers), metal-rs and glow as the compatibility fallback.",
  },
];

export const Architecture = memo(function Architecture() {
  const [hover, setHover] = useState<number | null>(null);
  return (
    <div className="grid gap-8 lg:grid-cols-[1.25fr_1fr] lg:gap-12">
      <div className="relative">
        <span className="absolute left-[15px] top-4 bottom-4 w-px bg-[repeating-linear-gradient(to_bottom,#bcc8d6_0_5px,transparent_5px_10px)]" />
        {LAYERS.map((l, i) => (
          <Reveal key={l.crate} delay={i * 70}>
            <div
              onMouseEnter={() => setHover(i)}
              onMouseLeave={() => setHover(null)}
              onClick={() => setHover(hover === i ? null : i)}
              className={cn(
                "group relative mb-2 flex items-start gap-4 border bg-paper-50/90 py-3 pl-10 pr-4 transition-all duration-200",
                hover === i
                  ? "border-paper-900/40 shadow-[0_10px_30px_-18px_rgba(22,29,38,0.7)]"
                  : "border-paper-200",
              )}
              style={hover === i ? { transform: "translateX(4px)" } : undefined}
            >
              <span
                className="absolute left-[9px] top-1/2 h-[13px] w-[13px] -translate-y-1/2 rotate-45 border-2 bg-paper-50 transition-transform duration-200 group-hover:scale-125"
                style={{ borderColor: l.color }}
              />
              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-baseline gap-x-3 gap-y-1">
                  <span className="font-display text-[17px] font-bold leading-tight text-paper-900">
                    {l.title}
                  </span>
                  <code className="font-mono text-[11px] text-[#0d7f97]">{l.crate}</code>
                </div>
                <p
                  className={cn(
                    "overflow-hidden text-[13px] leading-relaxed text-paper-700 transition-all duration-300 md:max-h-0 md:opacity-0",
                    hover === i && "mt-1.5 max-h-40 opacity-100 md:max-h-40 md:opacity-100",
                  )}
                >
                  {l.body}
                </p>
              </div>
              <span
                className="shrink-0 border px-1.5 py-[2px] font-mono text-[9px] uppercase tracking-wider"
                style={{ color: l.color, borderColor: `${l.color}55`, background: `${l.color}12` }}
              >
                {l.n}
              </span>
            </div>
          </Reveal>
        ))}
      </div>

      <div className="lg:pt-2">
        <h3 className="font-display text-[26px] font-bold leading-tight text-paper-900">
          Five boundaries, one rule:
          <span className="block text-[#c97a09]">addons never touch the renderer.</span>
        </h3>
        <p className="mt-4 text-[14.5px] leading-relaxed text-paper-700">
          Everything above the render graph speaks ECS. An addon that wants a heat-map overlay does
          not patch a shader — it writes a scalar per vertex into a component, and the core renderer
          reads that storage buffer when the pass is enabled. That is what keeps a 40k-LOC engine
          hackable by a 90-line crate.
        </p>
        <dl className="mt-6 space-y-px border border-paper-200 bg-paper-200">
          {[
            ["Binary size", "8.4 MB stripped · 3 runtime deps"],
            ["Cold start", "312 ms to first frame (NVMe)"],
            ["Addon ABI", "stable Rust trait object + manifest"],
            ["Hot reload", "systems swapped, entity data preserved"],
            ["Renderer", "forward+ clustered, 4× MSAA, D24S8"],
            ["Determinism", "same tessellation hash on VK and DX12"],
          ].map(([k, v]) => (
            <div key={k} className="flex flex-wrap gap-x-4 bg-paper-50 px-3 py-2">
              <dt className="w-[130px] shrink-0 font-mono text-[10.5px] uppercase tracking-[0.12em] text-[#5b6b7c]">
                {k}
              </dt>
              <dd className="min-w-0 flex-1 text-[13px] text-paper-900">{v}</dd>
            </div>
          ))}
        </dl>
        <div className="mt-5 flex items-start gap-3 border-l-2 border-[#c97a09] bg-[#f5a524]/[0.08] px-3 py-2.5">
          <span className="font-mono text-[10px] uppercase tracking-widest text-[#c97a09]">note</span>
          <p className="text-[12.5px] leading-relaxed text-paper-700">
            The viewport above is a software reference rasterizer of the same scene graph — the
            shipping engine submits the identical draw list to wgpu and lets the driver do the work.
          </p>
        </div>
      </div>
    </div>
  );
});

/* ------------------------------------------------------------------ */
/* Frame pipeline                                                      */
/* ------------------------------------------------------------------ */

const STAGES: { name: string; base: number; color: string; note: string }[] = [
  { name: "winit::event", base: 0.08, color: "#93a4b6", note: "input + resize" },
  { name: "egui::pass", base: 0.44, color: "#8b7fd6", note: "panels, docks, gizmos" },
  { name: "ecs::systems", base: 1.2, color: "#35c9e8", note: "addon + core systems" },
  { name: "frustum_cull", base: 0.31, color: "#4fd2ee", note: "SIMD AABB sweep" },
  { name: "tessellate_clip", base: 0.86, color: "#8ede63", note: "B-rep → tris, section" },
  { name: "record_cmds", base: 0.62, color: "#f5a524", note: "command list per pass" },
  { name: "queue.submit", base: 0.22, color: "#ff9d5c", note: "fence + timeline" },
  { name: "present", base: 0.1, color: "#ff6b5a", note: "swapchain flip" },
];

export function Pipeline() {
  const { stats, backend } = useEngine();
  const { ref, seen } = useInView<HTMLDivElement>();
  const total = STAGES.reduce((a, s) => a + s.base, 0);
  const load = Math.max(0.45, (stats.cpuMs * 0.35 + stats.gpuMs * 0.4 + total * 0.6) / total);

  return (
    <div ref={ref} className="grid gap-8 lg:grid-cols-[1fr_360px] lg:gap-12">
      <div>
        <div className="mb-4 flex items-end justify-between border-b border-ink-700 pb-3">
          <div>
            <div className="font-mono text-[10px] uppercase tracking-[0.22em] text-ink-400">
              one frame · {backend.label}
            </div>
            <div className="mt-1 font-display text-[26px] font-bold leading-none text-paper-50">
              {(total * load).toFixed(2)}
              <span className="ml-1 text-[15px] font-medium text-ink-400">ms cpu+submit</span>
            </div>
          </div>
          <div className="text-right font-mono text-[10px] text-ink-400">
            <div>
              budget <span className="text-lime-signal">16.70 ms</span>
            </div>
            <div>
              headroom{" "}
              <span className="text-amber-signal">{(16.7 - total * load).toFixed(2)} ms</span>
            </div>
          </div>
        </div>

        <div className="mb-6 flex h-7 w-full overflow-hidden border border-ink-700 bg-ink-950">
          {STAGES.map((s, i) => (
            <div
              key={s.name}
              className="group relative h-full transition-all duration-700 ease-out"
              style={{
                width: seen ? `${(s.base / total) * 100 * load}%` : "0%",
                background: s.color,
                opacity: 0.75,
                transitionDelay: `${i * 55}ms`,
              }}
              title={`${s.name} · ${(s.base * load).toFixed(2)} ms`}
            />
          ))}
          <div className="h-full flex-1 hatch" />
        </div>

        <div className="space-y-px">
          {STAGES.map((s, i) => (
            <div
              key={s.name}
              className="group flex items-center gap-3 border-b border-ink-850 py-2 transition-colors hover:bg-ink-850/60"
            >
              <span className="w-5 shrink-0 text-right font-mono text-[10px] text-ink-600">
                {String(i + 1).padStart(2, "0")}
              </span>
              <span className="h-2.5 w-2.5 shrink-0" style={{ background: s.color }} />
              <span className="w-[132px] shrink-0 font-mono text-[12px] text-paper-100">
                {s.name}
              </span>
              <span className="hidden flex-1 truncate text-[12px] text-ink-400 sm:block">
                {s.note}
              </span>
              <span className="h-1.5 w-28 shrink-0 overflow-hidden bg-ink-800">
                <span
                  className="block h-full transition-all duration-700 ease-out"
                  style={{
                    width: seen ? `${Math.min(100, (s.base / 1.2) * 100 * load)}%` : "0%",
                    background: s.color,
                    transitionDelay: `${i * 55}ms`,
                  }}
                />
              </span>
              <span className="tabular w-[52px] shrink-0 text-right font-mono text-[11px] text-paper-200">
                {(s.base * load).toFixed(2)} ms
              </span>
            </div>
          ))}
        </div>
      </div>

      <aside className="border border-ink-700 bg-ink-900 p-5">
        <div className="font-mono text-[10px] uppercase tracking-[0.22em] text-amber-signal">
          stage labels
        </div>
        <p className="mt-3 text-[13.5px] leading-relaxed text-paper-300">
          The schedule is split so addons can slot in without racing the renderer:
        </p>
        <div className="mt-4 space-y-2.5">
          {[
            ["PreCad", "input, gizmos, selection, constraint solve"],
            ["Cad", "tessellation, section clipping, booleans"],
            ["Extract", "read the World into the render world"],
            ["PostCad", "egui paint, overlays, profiler queries"],
          ].map(([k, v]) => (
            <div key={k} className="border-l-2 border-cyan-signal/50 pl-3">
              <div className="font-mono text-[11.5px] text-cyan-signal">{k}</div>
              <div className="text-[12.5px] text-ink-400">{v}</div>
            </div>
          ))}
        </div>
        <div className="mt-5 border-t border-ink-800 pt-4 font-mono text-[10.5px] leading-relaxed text-ink-400">
          <div className="text-paper-200">// ordering an addon system</div>
          <div>
            app.add_systems(Update,
            <br />
            <span className="pl-4 text-amber-signal">my_pass</span>
            <br />
            <span className="pl-6">.after(CadStage::Clip)</span>
            <br />
            <span className="pl-6">.before(CadStage::Extract));</span>
          </div>
        </div>
      </aside>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Benchmarks                                                          */
/* ------------------------------------------------------------------ */

const BENCH: { scene: string; tris: string; vk: number; dx: number; vram: string; note: string }[] = [
  { scene: "BRG-220 housing", tris: "1 684", vk: 412, dx: 438, vram: "48 MB", note: "single part, 4× MSAA" },
  { scene: "MFL-6B flange", tris: "2 418", vk: 396, dx: 421, vram: "52 MB", note: "6 bolt collars, 3 gussets" },
  { scene: "GBX-L4 bracket", tris: "2 106", vk: 401, dx: 425, vram: "51 MB", note: "cast body + machined boss" },
  { scene: "Gearbox assembly", tris: "1.21 M", vk: 128, dx: 141, vram: "610 MB", note: "284 parts, instanced fasteners" },
  { scene: "Assembly + section", tris: "1.21 M", vk: 104, dx: 117, vram: "688 MB", note: "live half-space clip" },
  { scene: "Plant layout", tris: "8.64 M", vk: 47, dx: 53, vram: "2.9 GB", note: "GPU-driven indirect draws" },
];

export const Benchmarks = memo(function Benchmarks() {
  const { ref, seen } = useInView<HTMLDivElement>();
  const max = 460;
  return (
    <div ref={ref} className="border border-ink-700 bg-ink-900">
      <div className="grid grid-cols-[1fr_auto] items-end gap-4 border-b border-ink-700 bg-ink-850 px-4 py-3">
        <div className="font-mono text-[10px] uppercase tracking-[0.2em] text-ink-400">
          1440p · vsync off · Ryzen 9 7950X · measured with anvil_debug_inspector
        </div>
        <div className="flex items-center gap-3 font-mono text-[10px]">
          <Legend color="#f5a524" label="Vulkan 1.3" />
          <Legend color="#35c9e8" label="DX12" />
        </div>
      </div>

      <div className="hidden grid-cols-[1.5fr_0.7fr_1.6fr_1.6fr_0.6fr] gap-3 border-b border-ink-800 px-4 py-2 font-mono text-[9.5px] uppercase tracking-[0.16em] text-ink-500 md:grid">
        <span>scene</span>
        <span className="text-right">triangles</span>
        <span>fps</span>
        <span />
        <span className="text-right">vram</span>
      </div>

      {BENCH.map((b, i) => (
        <div
          key={b.scene}
          className="group grid grid-cols-2 items-center gap-x-3 gap-y-1.5 border-b border-ink-850 px-4 py-3 transition-colors last:border-0 hover:bg-ink-850/70 md:grid-cols-[1.5fr_0.7fr_1.6fr_1.6fr_0.6fr]"
        >
          <div className="col-span-1 min-w-0">
            <div className="truncate font-display text-[15px] font-bold text-paper-100 group-hover:text-amber-signal">
              {b.scene}
            </div>
            <div className="truncate font-mono text-[10px] text-ink-500">{b.note}</div>
          </div>
          <div className="tabular text-right font-mono text-[11px] text-cyan-signal/90 md:text-left">
            {b.tris}
          </div>
          <BarRow
            seen={seen}
            delay={i * 60}
            value={b.vk}
            max={max}
            color="#f5a524"
          />
          <BarRow
            seen={seen}
            delay={i * 60 + 30}
            value={b.dx}
            max={max}
            color="#35c9e8"
          />
          <div className="tabular text-right font-mono text-[11px] text-ink-400">{b.vram}</div>
        </div>
      ))}
      <div className="border-t border-ink-700 bg-ink-950 px-4 py-2.5 font-mono text-[10px] text-ink-500">
        DX12 leads by 4–11 % on this hardware thanks to enhanced barriers and a lower
        ExecuteCommandLists overhead; Vulkan wins on driver-agnostic determinism.
      </div>
    </div>
  );
});

function Legend({ color, label }: { color: string; label: string }) {
  return (
    <span className="flex items-center gap-1.5 text-ink-400">
      <span className="h-2 w-4" style={{ background: color }} />
      {label}
    </span>
  );
}

function BarRow({
  seen,
  delay,
  value,
  max,
  color,
}: {
  seen: boolean;
  delay: number;
  value: number;
  max: number;
  color: string;
}) {
  return (
    <div className="col-span-2 flex items-center gap-2 md:col-span-1">
      <span className="h-2 flex-1 overflow-hidden bg-ink-800">
        <span
          className="block h-full transition-all duration-1000 ease-out"
          style={{
            width: seen ? `${(value / max) * 100}%` : "0%",
            background: color,
            transitionDelay: `${delay}ms`,
          }}
        />
      </span>
      <span className="tabular w-8 text-right font-mono text-[11px]" style={{ color }}>
        {value}
      </span>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Roadmap                                                             */
/* ------------------------------------------------------------------ */

const ROAD = [
  {
    v: "0.6",
    date: "Q3 2025",
    state: "shipped",
    title: "First frame",
    items: ["wgpu surface + swapchain", "orbit camera & gizmos", "STEP AP214 import", "egui dock shell"],
  },
  {
    v: "0.7",
    date: "Q4 2025",
    state: "shipped",
    title: "Plugin engine",
    items: ["manifest DAG + hot reload", "stage labels PreCad/Cad/PostCad", "section plane addon", "measure & snap addon"],
  },
  {
    v: "0.8",
    date: "Q1 2026",
    state: "shipped",
    title: "DX12 parity",
    items: ["enhanced barriers", "root signature cache", "GPU-driven indirect draws", "cross-API tessellation hash"],
  },
  {
    v: "0.9",
    date: "Q2 2026",
    state: "current",
    title: "Analysis & metrology",
    items: [
      "FEM compute solver (P1 tets)",
      "GD&T ISO 1101 overlay",
      "boolean CSG on BSP trees",
      "8 M triangle plant layouts",
    ],
  },
  {
    v: "1.0",
    date: "Q4 2026",
    state: "planned",
    title: "Constraint core",
    items: ["3D sketcher + solver", "assembly mates", "PMI export to STEP AP242", "stable addon ABI 1.0"],
  },
];

export const Roadmap = memo(function Roadmap() {
  const [open, setOpen] = useState("0.9");
  return (
    <div className="space-y-2">
      {ROAD.map((r) => {
        const active = open === r.v;
        const tone =
          r.state === "shipped"
            ? "#8ede63"
            : r.state === "current"
              ? "#f5a524"
              : "#64748b";
        return (
          <div
            key={r.v}
            className={cn(
              "border transition-all duration-300",
              active ? "border-ink-600 bg-ink-850" : "border-ink-800 bg-ink-900/70 hover:border-ink-700",
            )}
          >
            <button
              type="button"
              onClick={() => setOpen(active ? "" : r.v)}
              className="flex w-full items-center gap-4 px-4 py-3 text-left"
            >
              <span
                className="font-display text-[26px] font-bold leading-none"
                style={{ color: active ? tone : "#3d4b5c" }}
              >
                {r.v}
              </span>
              <span className="w-[86px] shrink-0 font-mono text-[10px] uppercase tracking-wider text-ink-500">
                {r.date}
              </span>
              <span className="min-w-0 flex-1">
                <span className="block font-display text-[16px] font-bold text-paper-100">
                  {r.title}
                </span>
              </span>
              <span
                className="shrink-0 border px-2 py-[2px] font-mono text-[9px] uppercase tracking-[0.14em]"
                style={{ color: tone, borderColor: `${tone}55`, background: `${tone}12` }}
              >
                {r.state}
              </span>
              <span
                className={cn(
                  "shrink-0 font-mono text-[11px] transition-transform duration-300",
                  active ? "rotate-90" : "",
                )}
                style={{ color: tone }}
              >
                ▶
              </span>
            </button>
            <div
              className={cn(
                "grid overflow-hidden transition-all duration-300",
                active ? "grid-rows-[1fr]" : "grid-rows-[0fr]",
              )}
            >
              <ul className="min-h-0 space-y-1 border-t border-ink-800 px-4 py-3">
                {r.items.map((it) => (
                  <li key={it} className="flex items-center gap-2.5 text-[13px] text-paper-300">
                    <span className="h-1 w-1 shrink-0 rotate-45" style={{ background: tone }} />
                    {it}
                  </li>
                ))}
              </ul>
            </div>
          </div>
        );
      })}
    </div>
  );
});

/* ------------------------------------------------------------------ */
/* Drawing-sheet title block                                           */
/* ------------------------------------------------------------------ */

export function TitleBlock() {
  const { part, backend, lod } = useEngine();
  const today = new Date().toISOString().slice(0, 10);

  const cell = (k: string, v: string, wide = false) => (
    <div className={cn("border-r border-paper-300 px-3 py-2 last:border-r-0", wide && "col-span-2")}>
      <div className="font-mono text-[8.5px] uppercase tracking-[0.18em] text-[#5b6b7c]">{k}</div>
      <div className="mt-0.5 truncate font-mono text-[11.5px] text-paper-900">{v}</div>
    </div>
  );

  return (
    <Reveal>
      <div className="mt-12 border-[3px] border-paper-900/70 bg-paper-50">
        <div className="grid grid-cols-2 border-b border-paper-300 md:grid-cols-4">
          <div className="col-span-2 flex items-center gap-3 border-r border-paper-300 px-3 py-3 md:col-span-2">
            <ProjectionSymbol />
            <div>
              <div className="font-display text-[22px] font-bold leading-none tracking-[0.06em] text-paper-900">
                {part.code}
              </div>
              <div className="mt-1 font-mono text-[10px] uppercase tracking-[0.16em] text-[#5b6b7c]">
                {part.name} · {part.standard}
              </div>
            </div>
          </div>
          {cell("scale", "2:1")}
          {cell("units", "millimetre")}
        </div>
        <div className="grid grid-cols-2 border-b border-paper-300 md:grid-cols-4">
          {cell("material", part.materialNote, true)}
          {cell("mass", part.mass)}
          {cell("tessellation", `${["Coarse", "Standard", "Fine"][lod]} chord`)}
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4">
          {cell("drawn", "anvil::kernel")}
          {cell("checked", "—")}
          {cell("date", today)}
          {cell("renderer", `${backend.label} · sheet 1/1`)}
        </div>
      </div>
    </Reveal>
  );
}

function ProjectionSymbol() {
  return (
    <svg width="46" height="30" viewBox="0 0 46 30" fill="none" aria-hidden>
      <circle cx="9" cy="15" r="7" stroke="#161d26" strokeWidth="1.3" />
      <circle cx="9" cy="15" r="2.6" stroke="#161d26" strokeWidth="1.3" />
      <path d="M22 8.5h21L37.5 21.5H27.5z" stroke="#161d26" strokeWidth="1.3" />
      <path d="M22 8.5v13" stroke="#161d26" strokeWidth="1.3" strokeDasharray="2 2" />
    </svg>
  );
}
