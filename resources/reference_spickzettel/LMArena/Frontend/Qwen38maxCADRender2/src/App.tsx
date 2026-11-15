import Viewport from "@/components/Viewport";
import Dock from "@/components/Dock";
import { Footer, Reveal, SectionHead, TopBar } from "@/components/Chrome";
import { CodeTabs, QuickStart } from "@/components/CodeTabs";
import { Architecture, Benchmarks, Pipeline, Roadmap, TitleBlock } from "@/components/Docs";
import { BACKENDS, EngineProvider, useEngine } from "@/state/engine";
import { cn } from "@/utils/cn";

const TICKER = [
  "wgpu 24",
  "bevy_ecs 0.16",
  "egui 0.31",
  "ash · Vulkan 1.3",
  "windows-rs · DX12",
  "naga · SPIR-V / DXIL",
  "OpenCascade AP242",
  "glam",
  "libm · CGAL bindings",
  "winit 0.30",
  "crossbeam channel",
  "rayon tessellation",
];

export default function App() {
  return (
    <EngineProvider>
      <Shell />
    </EngineProvider>
  );
}

function Shell() {
  const { part, backend, stats, enabled } = useEngine();

  return (
    <div className="min-h-screen bg-ink-950">
      <TopBar />

      {/* ================= engine shell ================= */}
      <section id="engine" className="relative border-b border-ink-700">
        <div className="mx-auto max-w-[1600px] px-3 lg:px-6">
          <div className="flex flex-wrap items-center gap-x-5 gap-y-1.5 border-b border-ink-800 py-2.5">
            <span className="flex items-center gap-2 font-display text-[12px] font-bold uppercase tracking-[0.26em] text-paper-100">
              <span className="inline-block h-2 w-2 animate-blink" style={{ background: backend.color }} />
              engine shell
            </span>
            <span className="font-mono text-[10.5px] text-ink-400">
              {part.code.toLowerCase()}.step · {part.name} · {part.solids.length} solids · {part.mass}
            </span>
            <span className="ml-auto hidden font-mono text-[10.5px] text-ink-500 lg:block">
              {backend.hal}
            </span>
          </div>

          <div className="grid grid-cols-1 xl:grid-cols-[minmax(0,1fr)_386px] xl:h-[calc(100vh-160px)] xl:min-h-[600px]">
            <div className="relative h-[62vh] min-h-[440px] xl:h-full">
              <Viewport />
            </div>
            <div className="h-[620px] xl:h-full xl:min-h-0">
              <Dock />
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-x-4 gap-y-1.5 border-t border-ink-800 py-2 font-mono text-[10px] text-ink-500">
            <span className="text-lime-signal">● ready</span>
            <span>{backend.adapter}</span>
            <span className="hidden text-ink-700 md:inline">|</span>
            <span className="hidden md:inline">
              {stats.drawn.toLocaleString("en-US")} draws · {stats.culled.toLocaleString("en-US")}{" "}
              back-face culled
            </span>
            <span className="ml-auto flex flex-wrap gap-1.5">
              {backend.features.slice(0, 4).map((f) => (
                <span
                  key={f}
                  className="border border-ink-700 px-1.5 py-[1px] text-[9px] text-ink-400"
                >
                  {f}
                </span>
              ))}
            </span>
          </div>
        </div>
      </section>

      {/* ================= ticker ================= */}
      <div className="overflow-hidden border-b border-ink-800 bg-ink-900 py-2">
        <div className="marquee flex w-max gap-8 whitespace-nowrap">
          {[...TICKER, ...TICKER].map((t, i) => (
            <span key={i} className="flex items-center gap-8 font-mono text-[10.5px] uppercase tracking-[0.2em] text-ink-500">
              {t}
              <span className="text-amber-signal/50">◆</span>
            </span>
          ))}
        </div>
      </div>

      {/* ================= manifesto ================= */}
      <section className="relative overflow-hidden border-b border-ink-700 bg-ink-950">
        <div className="pointer-events-none absolute inset-0 rail-grid opacity-[0.05]" />
        <div
          className="pointer-events-none absolute -right-40 top-0 h-[520px] w-[520px] animate-drift rounded-full opacity-[0.16] blur-[110px]"
          style={{ background: backend.color }}
        />
        <div className="relative mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <div className="grid gap-12 lg:grid-cols-[1.15fr_0.85fr] lg:gap-16">
            <Reveal>
              <p className="mb-5 flex items-center gap-3 font-mono text-[10.5px] uppercase tracking-[0.26em] text-amber-signal">
                <span className="h-px w-10 bg-amber-signal/60" />
                rust · wgpu · bevy · egui
              </p>
              <h1 className="font-display text-[clamp(2.4rem,6.4vw,5.2rem)] font-bold leading-[0.92] tracking-[-0.025em] text-paper-50">
                A CAD renderer
                <span className="block text-ink-500">that treats the GPU</span>
                <span className="block">
                  as a{" "}
                  <span className="relative inline-block">
                    <span className="relative z-10 text-amber-signal">first-class citizen</span>
                    <span className="absolute inset-x-0 bottom-[0.14em] z-0 h-[0.14em] bg-amber-signal/25" />
                  </span>
                  .
                </span>
              </h1>
              <p className="mt-7 max-w-[60ch] text-[16px] leading-relaxed text-paper-300">
                ANVIL is a parametric solid modeller and renderer written end to end in Rust. Geometry
                lives in a Bevy ECS world, the frame graph is recorded through wgpu and submitted
                natively to <span className="text-amber-signal">Vulkan 1.3</span> or{" "}
                <span className="text-cyan-signal">Direct3D 12</span>, and every tool — section
                planes, GD&T, solvers — is an addon crate mounted at runtime.
              </p>
              <div className="mt-8 flex flex-wrap items-center gap-3">
                <a
                  href="#addons"
                  className="group flex items-center gap-2 bg-amber-signal px-5 py-2.5 font-mono text-[11px] uppercase tracking-[0.16em] text-ink-950 transition-all hover:bg-[#ffb840] hover:shadow-[0_0_0_4px_rgba(245,165,36,0.16)]"
                >
                  write an addon
                  <span className="transition-transform group-hover:translate-x-1">→</span>
                </a>
                <a
                  href="#architecture"
                  className="flex items-center gap-2 border border-ink-600 px-5 py-2.5 font-mono text-[11px] uppercase tracking-[0.16em] text-paper-200 transition-colors hover:border-cyan-signal hover:text-cyan-signal"
                >
                  read the architecture
                </a>
              </div>
            </Reveal>

            <Reveal delay={120}>
              <div className="border border-ink-700 bg-ink-900/70">
                <div className="border-b border-ink-700 px-4 py-2.5 font-mono text-[10px] uppercase tracking-[0.22em] text-ink-400">
                  device providers
                </div>
                {BACKENDS.map((b) => {
                  const active = b.id === backend.id;
                  return (
                    <div
                      key={b.id}
                      className={cn(
                        "border-b border-ink-800 px-4 py-3 transition-colors last:border-0",
                        active && "bg-ink-850",
                      )}
                    >
                      <div className="flex items-center gap-2.5">
                        <span
                          className="h-2 w-2 shrink-0"
                          style={{ background: active ? b.color : "#2b3644" }}
                        />
                        <span
                          className={cn(
                            "font-display text-[15px] font-bold",
                            active ? "text-paper-50" : "text-ink-400",
                          )}
                        >
                          {b.label}
                        </span>
                        <span className="ml-auto font-mono text-[9px] uppercase tracking-wider text-ink-500">
                          {b.tier}
                        </span>
                      </div>
                      <div className="mt-1 pl-[18px] font-mono text-[10.5px] text-ink-500">{b.hal}</div>
                      <div
                        className={cn(
                          "grid overflow-hidden pl-[18px] transition-all duration-500",
                          active ? "mt-2 grid-rows-[1fr] opacity-100" : "grid-rows-[0fr] opacity-0",
                        )}
                      >
                        <div className="min-h-0">
                          <div className="flex flex-wrap gap-1">
                            {b.features.map((f) => (
                              <code
                                key={f}
                                className="border px-1.5 py-[1px] font-mono text-[9px]"
                                style={{
                                  color: b.color,
                                  borderColor: `${b.color}44`,
                                  background: `${b.color}10`,
                                }}
                              >
                                {f}
                              </code>
                            ))}
                          </div>
                        </div>
                      </div>
                    </div>
                  );
                })}
                <div className="grid grid-cols-3 gap-px border-t border-ink-700 bg-ink-700">
                  {[
                    ["plugins live", `${enabled.length}`],
                    ["frame", `${stats.ms.toFixed(2)} ms`],
                    ["gpu", `${stats.gpuMs.toFixed(2)} ms`],
                  ].map(([k, v]) => (
                    <div key={k} className="bg-ink-900 px-3 py-2.5 text-center">
                      <div className="tabular font-display text-[20px] font-bold leading-none text-paper-50">
                        {v}
                      </div>
                      <div className="mt-1.5 font-mono text-[8.5px] uppercase tracking-[0.16em] text-ink-500">
                        {k}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            </Reveal>
          </div>
        </div>
      </section>

      {/* ================= architecture (light) ================= */}
      <section id="architecture" className="blueprint border-b border-paper-300">
        <div className="mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <SectionHead
            index="01"
            kicker="layered engine"
            title={
              <>
                Six layers between
                <br />
                your addon and the driver.
              </>
            }
            lede="Each boundary is a trait, not a convention. Addons register components and systems against the ECS; the render graph is assembled from labels; the HAL is negotiated once at device creation. Nothing reaches across a layer."
            meta={
              <div className="flex gap-6 border-l-2 border-[#c97a09] pl-4">
                {[
                  ["40k", "LOC core"],
                  ["12", "addon crates"],
                  ["0", "unsafe in the plugin API"],
                ].map(([v, k]) => (
                  <div key={k}>
                    <div className="font-display text-[28px] font-bold leading-none text-paper-900">
                      {v}
                    </div>
                    <div className="mt-1 font-mono text-[9.5px] uppercase tracking-[0.14em] text-[#5b6b7c]">
                      {k}
                    </div>
                  </div>
                ))}
              </div>
            }
          />
          <Architecture />
          <TitleBlock />
        </div>
      </section>

      {/* ================= addons / code (dark) ================= */}
      <section id="addons" className="relative border-b border-ink-700 bg-ink-900">
        <div className="pointer-events-none absolute inset-0 rail-grid opacity-[0.04]" />
        <div className="relative mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <SectionHead
            dark
            index="02"
            kicker="lightweight ecs plugin engine"
            title={
              <>
                Addons are crates,
                <br />
                not forks.
              </>
            }
            lede="A manifest declares what a plugin needs and what it registers. The engine resolves the dependency graph, mounts the systems into the schedule and docks the panels — then keeps watching the file so you can rebuild without losing your open document."
            meta={
              <div className="flex flex-col gap-1.5 font-mono text-[10.5px] text-ink-400">
                <span>
                  <span className="text-lime-signal">✓</span> stable ABI · trait object + ron manifest
                </span>
                <span>
                  <span className="text-lime-signal">✓</span> hot reload preserves entity storage
                </span>
                <span>
                  <span className="text-lime-signal">✓</span> per-plugin tracing spans & timings
                </span>
                <span>
                  <span className="text-amber-signal">!</span> GPU work goes through the graph, never
                  raw handles
                </span>
              </div>
            }
          />
          <CodeTabs />
        </div>
      </section>

      {/* ================= authoring (light) ================= */}
      <section className="blueprint border-b border-paper-300">
        <div className="mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <div className="grid gap-12 lg:grid-cols-[0.85fr_1.15fr] lg:gap-16">
            <Reveal>
              <SectionHead
                index="03"
                kicker="authoring"
                title={
                  <>
                    Ship a tool
                    <br />
                    in an afternoon.
                  </>
                }
                lede="Five steps from cargo new to a docked panel in the running editor. The template already wires the manifest, the ABI export and the egui callback."
              />
              <div className="border border-paper-300 bg-paper-50/80 p-4">
                <div className="font-mono text-[10px] uppercase tracking-[0.2em] text-[#5b6b7c]">
                  manifest.ron
                </div>
                <pre className="mt-2 overflow-x-auto font-mono text-[11px] leading-relaxed text-paper-900">
{`(id: "tool.my_addon",
 version: "0.1.0",
 kind: Tool,
 requires: ["geom.step_kernel"],
 abi: 7,
 panels: [Dock::Right("My Tool")],
 systems: [Stage::Cad])`}
                </pre>
              </div>
            </Reveal>
            <Reveal delay={100}>
              <QuickStart />
            </Reveal>
          </div>
        </div>
      </section>

      {/* ================= pipeline (dark) ================= */}
      <section id="pipeline" className="border-b border-ink-700 bg-ink-950">
        <div className="mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <SectionHead
            dark
            index="04"
            kicker="frame anatomy"
            title={
              <>
                Every millisecond
                <br />
                has an owner.
              </>
            }
            lede="The frame is a fixed sequence of stages with named budgets. The profiler addon attributes GPU timestamps to the addon that requested the pass, so a slow solver can never hide inside the renderer."
          />
          <Pipeline />
        </div>
      </section>

      {/* ================= benchmarks (dark) ================= */}
      <section id="benchmarks" className="border-b border-ink-700 bg-ink-900">
        <div className="mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <SectionHead
            dark
            index="05"
            kicker="throughput"
            title={
              <>
                From a 1.7k-triangle
                <br />
                bracket to an 8.6M plant.
              </>
            }
            lede="Frames per second at 2560 × 1440 with 4× MSAA, section clipping enabled where noted. Small parts are input-bound — that is why the interactive shell stays at monitor refresh no matter which API you pick."
          />
          <Benchmarks />
        </div>
      </section>

      {/* ================= roadmap (dark) ================= */}
      <section id="roadmap" className="border-b border-ink-700 bg-ink-950">
        <div className="mx-auto max-w-[1600px] px-4 py-16 lg:px-6 lg:py-24">
          <SectionHead
            dark
            index="06"
            kicker="roadmap"
            title={
              <>
                0.9 today,
                <br />
                constraints at 1.0.
              </>
            }
            lede="The renderer is done enough to be boring. What is left is the part that makes it a CAD system: a 3D sketcher, a constraint solver and PMI round-tripping."
          />
          <div className="grid gap-8 lg:grid-cols-[1.1fr_0.9fr] lg:gap-12">
            <Roadmap />
            <Reveal delay={80}>
              <div className="border border-ink-700 bg-ink-900 p-5">
                <div className="font-mono text-[10px] uppercase tracking-[0.22em] text-amber-signal">
                  what will not happen
                </div>
                <ul className="mt-4 space-y-3">
                  {[
                    ["No cloud account", "Documents stay on disk in an open container format."],
                    ["No proprietary kernel lock-in", "OpenCascade today, any B-rep kernel behind the trait tomorrow."],
                    ["No WebGL-only path", "A browser build exists, but the target is a native desktop tool."],
                    ["No closed plugin API", "Everything an addon can do, the core does the same way."],
                  ].map(([t, d]) => (
                    <li key={t} className="border-l-2 border-ink-700 pl-3">
                      <div className="font-display text-[15px] font-bold text-paper-100">{t}</div>
                      <div className="text-[12.5px] leading-relaxed text-ink-400">{d}</div>
                    </li>
                  ))}
                </ul>
                <div className="mt-5 border-t border-ink-800 pt-4 font-mono text-[10.5px] leading-relaxed text-ink-500">
                  next milestone review · public RFC #41 “constraint solver”
                </div>
              </div>
            </Reveal>
          </div>
        </div>
      </section>

      <Footer />
    </div>
  );
}
