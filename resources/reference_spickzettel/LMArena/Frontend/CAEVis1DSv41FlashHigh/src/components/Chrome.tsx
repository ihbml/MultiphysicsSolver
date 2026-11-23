import { useMemo, useState } from "react";
import {
  Activity, ArrowLeftRight, Bell, BookOpen, Box, BrainCircuit,
  Check, ChevronDown, CircleHelp, Command, Cpu, Database, FunctionSquare,
  GitBranch, Layers, LayoutGrid, LifeBuoy, Play, Radar,
  Ruler, Settings, ShieldCheck, Sparkles, TriangleAlert, Wand2, Wind, X,
} from "lucide-react";
import { useCockpit, Workspace } from "../store";
import { FIELDS, FieldKey } from "../lib/sim";
import { Btn, Chip, InlineKey, cn } from "./ui";

export const WORKSPACES: { id: Workspace; label: string; short: string; icon: React.ReactNode; key: string }[] = [
  { id: "rules", label: "Patch Rules", short: "Rules", icon: <Wand2 size={13} />, key: "1" },
  { id: "domain", label: "Domain & Actors", short: "Domain", icon: <Box size={13} />, key: "2" },
  { id: "probe", label: "Probe Lab", short: "Probes", icon: <Ruler size={13} />, key: "3" },
  { id: "compare", label: "A/B Compare", short: "Compare", icon: <ArrowLeftRight size={13} />, key: "4" },
  { id: "calc", label: "Field Calculator", short: "Calc", icon: <FunctionSquare size={13} />, key: "5" },
];

export function Logo({ size = 26 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" fill="none">
      <defs>
        <linearGradient id="lg" x1="0" y1="0" x2="32" y2="32">
          <stop stopColor="#4D7CFF" /><stop offset="1" stopColor="#004AFD" />
        </linearGradient>
      </defs>
      <path d="M16 2.6 28.3 9.4v13.2L16 29.4 3.7 22.6V9.4L16 2.6Z" stroke="url(#lg)" strokeWidth="1.5" fill="#0b1020" />
      <path d="M16 8.2 23.5 12.4v8.6L16 25.2 8.5 21V12.4L16 8.2Z" fill="url(#lg)" opacity="0.16" />
      <path d="M11 21.5 16 10l5 11.5M13 18h6" stroke="#DCE7FF" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

export function TopBar() {
  const c = useCockpit();
  const [run, setRun] = useState<"A" | "B">("A");
  return (
    <header className="relative z-30 flex h-[46px] shrink-0 items-center gap-2 border-b border-[#26272e] bg-[#0d0e12] px-2.5">
      <div className="flex shrink-0 items-center gap-2">
        <Logo size={24} />
        <div className="leading-none">
          <div className="flex items-center gap-1.5">
            <span className="text-[12.5px] font-semibold tracking-[0.1em] text-[#eef1f7]">AEROFORGE</span>
            <span className="rounded-[2px] border border-[#3a4b70] bg-[#121a2a] px-1 py-[1px] text-[6.5px] font-medium tracking-wider text-[#8fb0ff]">CFD</span>
          </div>
          <div className="mt-[2px] flex items-center gap-1 text-[7px] uppercase tracking-[0.2em] text-[#5d6371]">
            cockpit <span className="h-[2px] w-[2px] rounded-full bg-[#4d5260]" /> stage 2
          </div>
        </div>
      </div>

      <div className="mx-1.5 h-6 w-px bg-[#2a2b33]" />

      <div className="hidden min-w-0 items-center gap-1.5 md:flex">
        <button className="flex h-[30px] items-center gap-1.5 rounded-[4px] px-1.5 text-left hover:bg-white/[0.04]">
          <div className="min-w-0">
            <div className="flex items-center gap-1 text-[10.5px] font-medium text-[#d6dae4]">
              Rotor–Stator Stage 2 <ChevronDown size={10} className="text-[#6d7484]" />
            </div>
            <div className="mono flex items-center gap-1 text-[7.5px] text-[#5f6675]">
              <GitBranch size={8} /> design-study-04 · adjoint sweep
            </div>
          </div>
        </button>
        <div className="hidden items-center gap-1 xl:flex">
          <Chip tone="green" dot="#23D18B">converged</Chip>
          <Chip tone="neutral">2 400 it</Chip>
          <Chip tone="neutral">CFDMSH02</Chip>
        </div>
      </div>

      <div className="ml-auto flex items-center gap-1.5">
        <div className="hidden items-center gap-1 rounded-[5px] border border-[#2b2c34] bg-[#121318] p-[2px] lg:flex">
          <span className="ml-1 mr-0.5 flex items-center gap-1 text-[8px] uppercase tracking-wider text-[#5f6675]"><Layers size={9} /> run</span>
          <button
            onClick={() => { setRun("A"); c.setWorkspace("compare"); }}
            className={cn("h-[21px] rounded-[3px] px-2 text-[9.5px] transition-colors", run === "A" ? "bg-[#2b2d36] text-white" : "text-[#818898] hover:text-[#c9cdd7]")}
          >
            A · Baseline
          </button>
          <button
            onClick={() => { setRun("B"); c.setWorkspace("compare"); }}
            className={cn("h-[21px] rounded-[3px] px-2 text-[9.5px] transition-colors", run === "B" ? "bg-[#1b3a7a] text-[#dbe7ff]" : "text-[#818898] hover:text-[#c9cdd7]")}
          >
            B · Adjoint #37
          </button>
        </div>

        <div className="hidden h-[26px] items-center gap-1.5 rounded-[4px] border border-[#252b32] bg-[#0e1613] px-2 sm:flex">
          <span className="h-[5px] w-[5px] rounded-full bg-[#23D18B] dot-live" />
          <span className="mono text-[8px] text-[#79c3a5]">NDJSON IPC</span>
          <span className="mono text-[8px] text-[#4f6d61]">42.1 MB/s</span>
        </div>

        <div className="mono hidden items-center gap-1 text-[8px] text-[#5f6675] lg:flex">
          <Cpu size={10} />
          <span>GPU 68 %</span>
          <div className="h-[3px] w-[34px] overflow-hidden rounded-full bg-[#24252c]"><div className="h-full w-[68%] bg-[#4D7CFF]" /></div>
        </div>

        <div className="mx-0.5 h-5 w-px bg-[#2a2b33]" />

        <button onClick={() => c.setShowHelp(true)} title="Help & shortcuts" className="flex h-[25px] w-[25px] items-center justify-center rounded-[4px] text-[#7e8494] hover:bg-[#1e1f26] hover:text-white">
          <CircleHelp size={14} />
        </button>
        <button onClick={() => c.notify(c.conflicts.size ? `${c.conflicts.size} boundary rule conflicts require attention` : "No new notifications")} title="Notifications" className="relative flex h-[25px] w-[25px] items-center justify-center rounded-[4px] text-[#7e8494] hover:bg-[#1e1f26] hover:text-white">
          <Bell size={14} />
          {c.conflicts.size > 0 && <span className="absolute right-[5px] top-[5px] h-[4px] w-[4px] rounded-full bg-[#EF233C]" />}
        </button>
        <div className="flex h-[24px] w-[24px] items-center justify-center rounded-full border border-[#3b4b6a] bg-[linear-gradient(140deg,#1b2b4d,#0f1522)] text-[8.5px] font-semibold text-[#a8c0f0]">AK</div>
      </div>
    </header>
  );
}

export function TabBar() {
  const c = useCockpit();
  const fieldOptions = useMemo(() => {
    const base = (Object.keys(FIELDS) as FieldKey[]).filter((f) => f !== "delta");
    return base;
  }, []);
  const active = c.activeField === "custom" && c.uploaded ? c.uploaded : FIELDS[c.activeField as FieldKey];
  const cmap = c.activeField === "custom" ? "turbo" : FIELDS[c.activeField as FieldKey]?.colormap || "turbo";
  return (
    <div className="relative z-20 flex h-[40px] shrink-0 items-center border-b border-[#26272e] bg-[#111217] px-2">
      <div className="scroll flex min-w-0 flex-1 items-center gap-0.5 overflow-x-auto hide-scroll">
        {WORKSPACES.map((w) => {
          const isActive = c.workspace === w.id;
          return (
            <button
              key={w.id}
              onClick={() => c.setWorkspace(w.id)}
              className={cn("group relative flex h-[38px] shrink-0 items-center gap-1.5 rounded-t-[4px] px-2.5 text-[10.5px] transition-colors",
                isActive ? "bg-[#181a20] text-[#eef1f7]" : "text-[#858c9b] hover:bg-white/[0.025] hover:text-[#c9ced9]")}
            >
              <span className={cn(isActive ? "text-[#8fb0ff]" : "text-[#68707f] group-hover:text-[#9aa1b0]")}>{w.icon}</span>
              <span className="hidden sm:inline">{w.label}</span>
              <span className="sm:hidden">{w.short}</span>
              <InlineKey>⌥{w.key}</InlineKey>
              {isActive && <span className="absolute inset-x-1.5 bottom-0 h-[2px] rounded-t-full bg-[linear-gradient(90deg,#004AFD,#41E0FF)]" />}
            </button>
          );
        })}
      </div>

      <div className="ml-2 flex shrink-0 items-center gap-1.5">
        <div className="hidden items-center gap-1 rounded-[4px] border border-[#2b2c34] bg-[#0f1014] px-1.5 py-[3px] md:flex">
          <span className="text-[7.5px] uppercase tracking-wider text-[#5f6675]">field</span>
          <span className="h-[10px] w-[3px] rounded-full" style={{ background: cmap === "coolwarm" ? "#e2736a" : "#4D7CFF" }} />
          <select
            value={c.activeField}
            onChange={(e) => c.setActiveField(e.target.value as FieldKey | "custom")}
            className="h-[18px] max-w-[140px] border-0 bg-transparent text-[9.5px] text-[#d4d9e4] outline-none"
          >
            {fieldOptions.map((f) => <option key={f} value={f} className="bg-[#17181d]">{FIELDS[f].sym} · {FIELDS[f].label}</option>)}
            {c.derivedFields.map((f) => <option key={f.name} value="custom" className="bg-[#17181d]">{f.name} · derived</option>)}
            {c.derivedFields.length === 0 && <option value="custom" disabled className="bg-[#17181d]">derived (none compiled)</option>}
          </select>
          <span className="mono text-[7.5px] text-[#5f6675]">{active?.unit || "—"}</span>
        </div>
        <button onClick={() => c.notify("Case saved to study-04/cockpit.stage2 · 12.4 MB")} className="hidden h-[24px] items-center gap-1 rounded-[4px] border border-[#2b2c34] bg-[#1a1b21] px-2 text-[9px] text-[#b7bdc9] hover:bg-[#22232a] sm:flex">
          <Database size={10} /> Save
        </button>
        <button onClick={() => c.notify("IPC request queued · solve case with updated spatial rules")} className="flex h-[24px] items-center gap-1 rounded-[4px] border border-[#3b70ff] bg-[#004AFD] px-2 text-[9px] font-medium text-white hover:bg-[#1b5dff]">
          <Play size={9} /> Run
        </button>
      </div>
    </div>
  );
}

export function LeftRail() {
  const c = useCockpit();
  const extras = [
    { icon: <Layers size={15} />, label: "Scene tree", action: () => c.notify("Scene tree: domain / rotor / stator / physics actors") },
    { icon: <Wind size={15} />, label: "Field viewer", action: () => c.setWorkspace("rules") },
    { icon: <Radar size={15} />, label: "Probes", action: () => c.setWorkspace("probe") },
    { icon: <BrainCircuit size={15} />, label: "Rule engine", action: () => c.setWorkspace("rules") },
    { icon: <FunctionSquare size={15} />, label: "Calculator", action: () => c.setWorkspace("calc") },
  ];
  return (
    <nav className="hidden w-[46px] shrink-0 flex-col items-center border-r border-[#24252c] bg-[#0d0e12] py-1.5 md:flex">
      <div className="flex flex-col items-center gap-[3px]">
        {WORKSPACES.map((w) => (
          <button
            key={w.id}
            title={w.label}
            onClick={() => c.setWorkspace(w.id)}
            className={cn("relative flex h-[32px] w-[32px] items-center justify-center rounded-[5px] transition-colors",
              c.workspace === w.id ? "bg-[#16213a] text-[#a8c3ff]" : "text-[#656c7b] hover:bg-[#1a1b21] hover:text-[#c2c8d4]")}
          >
            {w.icon}
            {c.workspace === w.id && <span className="absolute -left-[7px] h-[15px] w-[2px] rounded-r-full bg-[#4D7CFF]" />}
            {w.id === "rules" && c.conflicts.size > 0 && <span className="absolute right-[4px] top-[4px] h-[4px] w-[4px] rounded-full bg-[#EF233C]" />}
          </button>
        ))}
      </div>
      <div className="my-1.5 h-px w-[24px] bg-[#26272e]" />
      <div className="flex flex-col items-center gap-[2px]">
        {extras.slice(0, 3).map((e) => (
          <button key={e.label} title={e.label} onClick={e.action} className="flex h-[30px] w-[30px] items-center justify-center rounded-[5px] text-[#5d6473] hover:bg-[#1a1b21] hover:text-[#c2c8d4]">{e.icon}</button>
        ))}
      </div>
      <div className="mt-auto flex flex-col items-center gap-[2px]">
        <button onClick={() => c.setShowHelp(true)} title="Documentation" className="flex h-[30px] w-[30px] items-center justify-center rounded-[5px] text-[#5d6473] hover:bg-[#1a1b21] hover:text-[#c2c8d4]"><LifeBuoy size={15} /></button>
        <button onClick={() => c.notify("Cockpit settings · theme Obsidian, 120 Hz, SI units")} title="Settings" className="flex h-[30px] w-[30px] items-center justify-center rounded-[5px] text-[#5d6473] hover:bg-[#1a1b21] hover:text-[#c2c8d4]"><Settings size={15} /></button>
        <div className="mt-1 flex h-[22px] w-[22px] items-center justify-center rounded-full border border-[#33415c] bg-[#131a28] text-[7px] font-semibold text-[#8fb0ff]">AK</div>
      </div>
    </nav>
  );
}

export function StatusBar() {
  const c = useCockpit();
  const meta = {
    rules: "patch rules · R32Uint pick · rule synthesizer",
    domain: "domain · MRF · DPM / FWH / porous actors",
    probe: "probe lab · trilinear sampling · egui_plot",
    compare: "A/B differential · GPU Δφ · Moreland cool-warm",
    calc: "SI-checked calculator · rhai · CFDFLD03",
  }[c.workspace];
  return (
    <footer className="relative z-20 flex h-[25px] shrink-0 items-center gap-2 overflow-hidden border-t border-[#26272e] bg-[#0b0c10] px-2">
      <span className="flex shrink-0 items-center gap-1"><span className="h-[5px] w-[5px] rounded-full bg-[#23D18B] dot-live" /><span className="mono text-[8px] text-[#7fc0a4]">solver converged</span></span>
      <span className="h-[9px] w-px bg-[#2a2b33]" />
      <span className="mono hidden items-center gap-1 text-[8px] text-[#6f7685] sm:flex"><Activity size={9} className="text-[#4D7CFF]" /> IPC · NDJSON 42.1 MB/s</span>
      <span className="h-[9px] w-px bg-[#2a2b33]" />
      <span className="mono hidden items-center gap-1 text-[8px] text-[#6f7685] md:flex"><Cpu size={9} /> 68 % GPU · 4.8/12 GB</span>
      {c.conflicts.size > 0 ? (
        <>
          <span className="h-[9px] w-px bg-[#2a2b33]" />
          <span className="mono flex items-center gap-1 text-[8px] text-[#ff8798]"><TriangleAlert size={9} /> {c.conflicts.size} BC overlap</span>
        </>
      ) : (
        <>
          <span className="h-[9px] w-px bg-[#2a2b33]" />
          <span className="mono hidden items-center gap-1 text-[8px] text-[#6f7685] lg:flex"><ShieldCheck size={9} className="text-[#5fb892]" /> rules valid</span>
        </>
      )}
      <span className="mono hidden truncate text-[8px] text-[#4f5564] lg:inline">{meta}</span>
      <div className="ml-auto flex shrink-0 items-center gap-2">
        <span className="mono hidden items-center gap-1 text-[8px] text-[#6f7685] sm:flex"><Ruler size={9} /> SI · m·kg·s·K</span>
        <span className="h-[9px] w-px bg-[#2a2b33]" />
        <span className="mono hidden text-[8px] text-[#5f6675] md:inline">wgpu 24.0 · egui 0.29 · rhai 1.19</span>
        <span className="mono text-[8px] text-[#8fa9e8]">stage 2 · 0.14.2</span>
      </div>
    </footer>
  );
}

export function Toast() {
  const c = useCockpit();
  if (!c.toast) return null;
  const text = c.toast;
  const bad = /conflict|overlap|resolve|fail|error/i.test(text);
  const good = /upload|merged|accepted|added|converged|valid|passed|exported/i.test(text) && !bad;
  return (
    <div className="pointer-events-none fixed bottom-9 left-1/2 z-50 -translate-x-1/2">
      <div className={cn("toast-in flex items-center gap-2 rounded-[6px] border px-3 py-2 shadow-[0_12px_40px_rgba(0,0,0,0.55)] backdrop-blur",
        bad ? "border-[#EF233C]/50 bg-[#1b0f12]/95" : good ? "border-[#23D18B]/40 bg-[#0c1713]/95" : "border-[#3a4b70] bg-[#111725]/95")}>
        {bad ? <TriangleAlert size={13} className="shrink-0 text-[#ff8798]" /> : good ? <Check size={13} className="shrink-0 text-[#65e5b1]" /> : <Sparkles size={13} className="shrink-0 text-[#8fb0ff]" />}
        <span className={cn("mono text-[9.5px]", bad ? "text-[#e2b4bc]" : good ? "text-[#b6e5d2]" : "text-[#b9c9e8]")}>{text}</span>
        <button onClick={() => c.setToast(null)} className="ml-2 shrink-0 text-[#6d7484] hover:text-white"><X size={11} /></button>
      </div>
    </div>
  );
}

export function HelpModal() {
  const c = useCockpit();
  if (!c.showHelp) return null;
  const sections = [
    {
      title: "Pick & author", icon: <Wand2 size={13} />,
      items: [
        ["LMB drag", "Orbit camera (synced across A/B)"],
        ["MMB / RMB drag", "Pan target"],
        ["Wheel", "Dolly / zoom"],
        ["Click blade face", "Read R32Uint face_group_id and synthesize a spatial rule"],
        ["Drag pA / pB", "Move rake endpoints on the camera plane"],
      ],
    },
    {
      title: "Workspaces", icon: <LayoutGrid size={13} />,
      items: [
        ["Alt 1 / 2", "Patch rules · domain & actors"],
        ["Alt 3 / 4", "Probe lab · A/B compare"],
        ["Alt 5", "SI-checked field calculator"],
        ["Drag actor card", "Drop DPM / FWH / porous actor into the domain"],
      ],
    },
    {
      title: "Scientific conventions", icon: <BookOpen size={13} />,
      items: [
        ["Cp", "Inverted y-axis convention supported"],
        ["u⁺(y⁺)", "Inner scaling, κ = 0.41, E = 9.8"],
        ["Δφ", "Run B − Run A, Moreland cool–warm centered at 0"],
        ["SI", "7-vector [kg, m, s, K, mol, A, cd] before execution"],
      ],
    },
  ];
  return (
    <div className="fixed inset-0 z-[60] flex items-center justify-center bg-black/65 p-4 backdrop-blur-sm" onClick={() => c.setShowHelp(false)}>
      <div className="panel glow-blue max-h-[86vh] w-full max-w-2xl overflow-y-auto scroll" onClick={(e) => e.stopPropagation()}>
        <div className="flex items-center gap-2 border-b border-[#26272e] px-4 py-3">
          <Logo size={22} />
          <div>
            <div className="text-[12px] font-medium">AEROFORGE Cockpit · Stage 2</div>
            <div className="mono text-[8px] text-[#646b7a]">spatial authoring · scientific charting · differential fields</div>
          </div>
          <button onClick={() => c.setShowHelp(false)} className="ml-auto text-[#7e8594] hover:text-white"><X size={15} /></button>
        </div>
        <div className="grid gap-3 p-4 sm:grid-cols-3">
          {sections.map((s) => (
            <div key={s.title} className="rounded-[5px] border border-[#292b33] bg-[#111217] p-2.5">
              <div className="mb-1.5 flex items-center gap-1.5 text-[10px] font-medium text-[#d3d9e5]"><span className="text-[#8fb0ff]">{s.icon}</span>{s.title}</div>
              <div className="space-y-1.5">
                {s.items.map(([k, v]) => (
                  <div key={k}>
                    <div className="mono text-[8.5px] text-[#9fbcff]">{k}</div>
                    <div className="text-[8.5px] leading-[1.45] text-[#7a8190]">{v}</div>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>
        <div className="flex items-center gap-2 border-t border-[#26272e] px-4 py-2.5">
          <div className="mono flex items-center gap-1 text-[8px] text-[#5f6675]"><Command size={9} /> Rust reference implementation included under <span className="text-[#8fb0ff]">rust-reference/</span></div>
          <Btn className="ml-auto" size="sm" variant="primary" onClick={() => c.setShowHelp(false)}>Got it</Btn>
        </div>
      </div>
    </div>
  );
}
