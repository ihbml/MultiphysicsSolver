import { memo, useEffect, useRef, useState, type ReactNode } from "react";
import { BACKENDS, useEngine, type BackendId } from "@/state/engine";
import { cn } from "@/utils/cn";

export function Reveal({
  children,
  delay = 0,
  className,
}: {
  children: ReactNode;
  delay?: number;
  className?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const io = new IntersectionObserver(
      ([e]) => {
        if (e.isIntersecting) {
          el.classList.add("is-in");
          io.disconnect();
        }
      },
      { threshold: 0.1, rootMargin: "0px 0px -60px 0px" },
    );
    io.observe(el);
    return () => io.disconnect();
  }, []);
  return (
    <div ref={ref} className={cn("reveal", className)} style={{ transitionDelay: `${delay}ms` }}>
      {children}
    </div>
  );
}

export function Mark({ size = 30 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" fill="none" aria-hidden>
      <path d="M16 2.5 29 10v12L16 29.5 3 22V10z" stroke="#f5a524" strokeWidth="1.4" />
      <path d="M16 8.5 23.5 13v8L16 25.5 8.5 21v-8z" fill="#f5a524" fillOpacity="0.14" />
      <path d="M16 8.5 23.5 13 16 17.5 8.5 13z" fill="#35c9e8" fillOpacity="0.5" />
      <path d="M8.5 13v8L16 25.5v-8z" fill="#f5a524" fillOpacity="0.55" />
      <path d="M23.5 13v8L16 25.5v-8z" fill="#f5a524" fillOpacity="0.28" />
    </svg>
  );
}

const NAV = [
  { href: "#engine", label: "Engine" },
  { href: "#architecture", label: "Architecture" },
  { href: "#addons", label: "Addons" },
  { href: "#pipeline", label: "Pipeline" },
  { href: "#benchmarks", label: "Benchmarks" },
  { href: "#roadmap", label: "Roadmap" },
];

export function TopBar() {
  const { backend, setBackend, stats } = useEngine();
  const [solid, setSolid] = useState(false);

  useEffect(() => {
    const onScroll = () => setSolid(window.scrollY > 24);
    onScroll();
    window.addEventListener("scroll", onScroll, { passive: true });
    return () => window.removeEventListener("scroll", onScroll);
  }, []);

  return (
    <header
      className={cn(
        "sticky top-0 z-50 border-b transition-colors duration-300",
        solid ? "border-ink-700 bg-ink-950/92 backdrop-blur-md" : "border-transparent bg-transparent",
      )}
    >
      <div className="mx-auto flex h-14 max-w-[1600px] items-center gap-4 px-4 lg:px-6">
        <a href="#engine" className="group flex items-center gap-2.5">
          <span className="transition-transform duration-300 group-hover:rotate-[18deg]">
            <Mark size={26} />
          </span>
          <span className="font-display text-[19px] font-bold leading-none tracking-[0.14em] text-paper-50">
            ANVIL
          </span>
          <span className="hidden border border-ink-600 px-1.5 py-[1px] font-mono text-[9px] text-ink-400 sm:inline">
            0.9.3-rc
          </span>
        </a>

        <nav className="ml-2 hidden items-center gap-1 lg:flex">
          {NAV.map((n) => (
            <a
              key={n.href}
              href={n.href}
              className="group relative px-2.5 py-1.5 font-mono text-[10.5px] uppercase tracking-[0.14em] text-ink-400 transition-colors hover:text-paper-100"
            >
              {n.label}
              <span className="absolute inset-x-2.5 bottom-1 h-px origin-left scale-x-0 bg-amber-signal transition-transform duration-300 group-hover:scale-x-100" />
            </a>
          ))}
        </nav>

        <div className="ml-auto flex items-center gap-3">
          <div className="hidden items-center gap-2 font-mono text-[10px] text-ink-400 md:flex">
            <span
              className="inline-block h-1.5 w-1.5 animate-blink rounded-full"
              style={{ background: backend.color }}
            />
            <span className="tabular text-paper-200">{stats.fps.toFixed(0)}</span>
            <span>fps</span>
            <span className="text-ink-600">·</span>
            <span className="tabular">{stats.tris.toLocaleString("en-US")}</span>
            <span>tris</span>
          </div>

          <div className="flex border border-ink-700 bg-ink-900">
            {BACKENDS.map((b) => (
              <button
                key={b.id}
                type="button"
                onClick={() => setBackend(b.id as BackendId)}
                title={b.adapter}
                className={cn(
                  "relative px-2 py-1 font-mono text-[10px] font-medium tracking-wider transition-colors",
                  backend.id === b.id ? "text-ink-950" : "text-ink-400 hover:text-paper-100",
                  b.tier === "fallback" && backend.id !== b.id && "opacity-60",
                )}
                style={backend.id === b.id ? { background: b.color } : undefined}
              >
                {b.short}
              </button>
            ))}
          </div>

          <a
            href="#addons"
            className="group hidden items-center gap-1.5 border border-amber-signal/50 bg-amber-signal/10 px-3 py-1.5 font-mono text-[10px] uppercase tracking-[0.14em] text-amber-signal transition-colors hover:bg-amber-signal hover:text-ink-950 sm:flex"
          >
            cargo install
            <span className="transition-transform group-hover:translate-x-0.5">→</span>
          </a>
        </div>
      </div>
      <div className="h-px w-full bg-gradient-to-r from-transparent via-ink-600 to-transparent" />
    </header>
  );
}

export function SectionHead({
  index,
  kicker,
  title,
  lede,
  meta,
  dark,
}: {
  index: string;
  kicker: string;
  title: ReactNode;
  lede: ReactNode;
  meta?: ReactNode;
  dark?: boolean;
}) {
  return (
    <div className="mb-10 grid gap-6 border-b pb-6 lg:grid-cols-[1fr_auto] lg:items-end"
      style={{ borderColor: dark ? "#1f2833" : "#bcc8d6" }}
    >
      <div>
        <div className="mb-3 flex items-center gap-3">
          <span
            className="font-mono text-[11px] tracking-[0.2em]"
            style={{ color: dark ? "#f5a524" : "#c97a09" }}
          >
            {index}
          </span>
          <span className="h-px w-8" style={{ background: dark ? "#2b3644" : "#bcc8d6" }} />
          <span
            className="font-mono text-[10.5px] uppercase tracking-[0.24em]"
            style={{ color: dark ? "#64748b" : "#5b6b7c" }}
          >
            {kicker}
          </span>
        </div>
        <h2
          className={cn(
            "font-display text-[clamp(2rem,4.4vw,3.4rem)] font-bold leading-[0.98] tracking-[-0.02em]",
            dark ? "text-paper-50" : "text-paper-900",
          )}
        >
          {title}
        </h2>
        <p
          className={cn(
            "mt-4 max-w-[62ch] text-[15px] leading-relaxed",
            dark ? "text-paper-300" : "text-paper-700",
          )}
        >
          {lede}
        </p>
      </div>
      {meta && <div className="lg:pb-2">{meta}</div>}
    </div>
  );
}

export const Footer = memo(function Footer() {
  return (
    <footer className="relative overflow-hidden border-t border-ink-700 bg-ink-950">
      <div className="pointer-events-none absolute inset-0 rail-grid opacity-[0.07]" />
      <div className="relative mx-auto max-w-[1600px] px-4 py-14 lg:px-6">
        <div className="grid gap-10 md:grid-cols-[1.4fr_1fr_1fr_1fr]">
          <div>
            <div className="flex items-center gap-2.5">
              <Mark size={30} />
              <span className="font-display text-[22px] font-bold tracking-[0.14em] text-paper-50">
                ANVIL
              </span>
            </div>
            <p className="mt-3 max-w-[42ch] text-[13px] leading-relaxed text-ink-400">
              A CAD renderer for people who care about draw calls. Rust, wgpu, Bevy ECS and egui —
              Vulkan and DX12 first, everything else a fallback.
            </p>
            <p className="mt-4 font-mono text-[10px] uppercase tracking-[0.18em] text-ink-500">
              MIT OR Apache-2.0 · no telemetry · no cloud
            </p>
          </div>
          <FooterCol
            title="engine"
            links={["anvil_render", "anvil_plugin", "anvil_shell_egui", "anvil_step_kernel", "anvil_math"]}
          />
          <FooterCol
            title="learn"
            links={["Quick start", "Addon guide", "Render graph", "Tessellation notes", "GD&T layer"]}
          />
          <FooterCol
            title="community"
            links={["Matrix #anvil-cad", "Discussions", "Issue tracker", "RFC process", "Bounties"]}
          />
        </div>
        <div className="mt-12 flex flex-col gap-3 border-t border-ink-800 pt-5 font-mono text-[10px] text-ink-500 sm:flex-row sm:items-center sm:justify-between">
          <span>
            built for the demo with a software reference rasterizer — the shipping renderer records
            wgpu command buffers.
          </span>
          <span className="text-ink-600">© 2026 anvil contributors</span>
        </div>
      </div>
    </footer>
  );
});

function FooterCol({ title, links }: { title: string; links: string[] }) {
  return (
    <div>
      <div className="mb-3 font-mono text-[10px] uppercase tracking-[0.22em] text-amber-signal/80">
        {title}
      </div>
      <ul className="space-y-1.5">
        {links.map((l) => (
          <li key={l}>
            <a
              href="#engine"
              className="group inline-flex items-center gap-1.5 text-[13px] text-ink-400 transition-colors hover:text-paper-100"
            >
              <span className="h-px w-0 bg-amber-signal transition-all duration-300 group-hover:w-3" />
              {l}
            </a>
          </li>
        ))}
      </ul>
    </div>
  );
}
