import { NAV } from "@/data/content";
import { cn } from "@/utils/cn";

export function Sidebar({ active }: { active: string }) {
  return (
    <aside className="fixed top-0 left-0 z-40 hidden h-screen w-[272px] flex-col border-r border-white/10 bg-[#070b14]/85 px-5 py-7 backdrop-blur-xl lg:flex">
      <a href="#ueberblick" className="mb-8 flex items-center gap-3">
        <Logo />
        <div>
          <div className="font-mono text-sm font-semibold tracking-[0.18em] text-white">
            MESHFORGE
          </div>
          <div className="text-[10px] tracking-wide text-slate-500">
            CFD / Multiphysics Architecture Hub
          </div>
        </div>
      </a>

      <nav className="flex-1 space-y-1 overflow-y-auto scroll-thin">
        {NAV.map((item, i) => {
          const isActive = active === item.id;
          return (
            <a
              key={item.id}
              href={`#${item.id}`}
              className={cn(
                "group relative flex items-start gap-3 rounded-xl px-3 py-2.5 transition-colors",
                isActive ? "bg-white/[0.06]" : "hover:bg-white/[0.04]",
              )}
            >
              <span
                className={cn(
                  "absolute top-1/2 left-0 h-6 w-0.5 -translate-y-1/2 rounded-full transition-all",
                  isActive ? "bg-cyan-400 opacity-100" : "opacity-0",
                )}
              />
              <span
                className={cn(
                  "mt-0.5 font-mono text-[10px] tabular-nums",
                  isActive ? "text-cyan-300" : "text-slate-600",
                )}
              >
                {String(i + 1).padStart(2, "0")}
              </span>
              <span className="min-w-0">
                <span
                  className={cn(
                    "block truncate text-[13px] font-medium",
                    isActive ? "text-white" : "text-slate-400 group-hover:text-slate-200",
                  )}
                >
                  {item.label}
                </span>
                <span className="block truncate text-[10.5px] text-slate-600">{item.hint}</span>
              </span>
            </a>
          );
        })}
      </nav>

      <div className="mt-6 space-y-2 border-t border-white/10 pt-5 text-[10.5px] leading-relaxed text-slate-500">
        <div className="flex items-center gap-2">
          <span className="h-1.5 w-1.5 rounded-full bg-emerald-400 animate-pulse-soft" />
          <span>Zwei-Klassen-Dispatching aktiv</span>
        </div>
        <p>
          Alle Zahlen sind Modellrechnungen aus der Face-Partition — keine Benchmarks.
        </p>
      </div>
    </aside>
  );
}

export function Logo({ className }: { className?: string }) {
  return (
    <svg
      viewBox="0 0 40 40"
      className={cn("h-9 w-9", className)}
      fill="none"
      aria-hidden="true"
    >
      <defs>
        <linearGradient id="mf-logo" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#22d3ee" />
          <stop offset="100%" stopColor="#a78bfa" />
        </linearGradient>
      </defs>
      <rect x="1.5" y="1.5" width="37" height="37" rx="10" stroke="url(#mf-logo)" strokeWidth="1.6" />
      <path d="M8 27 L16 13 L24 27" stroke="url(#mf-logo)" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
      <path d="M22 27 L32 13" stroke="url(#mf-logo)" strokeWidth="1.6" strokeLinecap="round" />
      <circle cx="16" cy="13" r="2.2" fill="#22d3ee" />
      <circle cx="32" cy="13" r="2.2" fill="#a78bfa" />
    </svg>
  );
}

export function MobileNav({ active }: { active: string }) {
  return (
    <div className="sticky top-0 z-40 border-b border-white/10 bg-[#070b14]/90 px-4 py-3 backdrop-blur-xl lg:hidden">
      <div className="flex items-center gap-3">
        <Logo className="h-7 w-7" />
        <span className="font-mono text-xs font-semibold tracking-[0.18em] text-white">
          MESHFORGE
        </span>
      </div>
      <div className="mt-3 flex gap-2 overflow-x-auto no-scrollbar">
        {NAV.map((item) => (
          <a
            key={item.id}
            href={`#${item.id}`}
            className={cn(
              "shrink-0 rounded-full border px-3 py-1.5 text-[11px] whitespace-nowrap transition-colors",
              active === item.id
                ? "border-cyan-400/40 bg-cyan-400/10 text-cyan-300"
                : "border-white/10 bg-white/5 text-slate-400",
            )}
          >
            {item.label}
          </a>
        ))}
      </div>
    </div>
  );
}
