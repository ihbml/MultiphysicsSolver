import { useMemo, useState } from "react";
import {
  ArrowDownRight, ArrowLeftRight, ArrowUpRight, Camera, Check, ChevronDown, Columns2, Download,
  Equal, Gauge, Grid2x2, Layers, Maximize2, Move3d, Play, ScanLine, Sliders, Sparkles,
  SplitSquareVertical, Target, TrendingDown, TriangleAlert, Zap,
} from "lucide-react";
import { useCockpit } from "../store";
import { FIELDS, FieldKey, KPIS, PARETO } from "../lib/sim";
import { Btn, Chip, Section, Segmented, Slider, StatBox, Toggle, Colorbar, cn } from "./ui";
import { ParetoChart } from "./Charts";

const FIELD_OPTS: FieldKey[] = ["Cp", "p", "U", "T", "Mach", "wallT", "Nu"];

export function LeftCompare() {
  const c = useCockpit();
  const cmp = c.compare;
  const set = (p: Partial<typeof cmp>) => c.setCompare((s) => ({ ...s, ...p }));
  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <ArrowLeftRight size={12} className="text-[#8fb0ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">A/B design difference</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">Δφ</span>
      </div>

      <Section title="Presentation mode" icon={<Columns2 size={11} />} dense>
        <div className="grid grid-cols-3 gap-1">
          {([
            { v: "wipe", label: "Wipe", icon: <SplitSquareVertical size={14} />, desc: "single camera · wipe split" },
            { v: "split", label: "Split", icon: <Columns2 size={14} />, desc: "dual viewport · synchronized" },
            { v: "delta", label: "Delta", icon: <Equal size={14} />, desc: "full-frame differential" },
          ] as const).map((m) => (
            <button
              key={m.v}
              onClick={() => set({ mode: m.v })}
              className={cn("flex flex-col items-center gap-1 rounded-[4px] border px-1 py-1.5 transition-colors",
                cmp.mode === m.v ? "border-[#3e5b9e] bg-[#0e1830] text-[#cfe0ff]" : "border-[#292b33] bg-[#101115] text-[#8a91a0] hover:border-[#3a3d48]")}
            >
              {m.icon}
              <span className="text-[9px]">{m.label}</span>
            </button>
          ))}
        </div>
        <div className="mono mt-1 text-[7.5px] leading-relaxed text-[#5f6675]">
          {cmp.mode === "wipe" ? "left: Run A · right: Δφ Moreland LUT" : cmp.mode === "split" ? "camera orbit / pan / zoom locked at 1:1" : "GPU subtraction: Δφ(x) = φB(x) − φA(x)"}
        </div>
        {cmp.mode === "wipe" && <Slider label="wipe position" value={cmp.wipe * 100} onChange={(v) => set({ wipe: v / 100 })} min={4} max={96} step={1} digits={0} unit="%" accent />}
        {cmp.mode === "split" && (
          <div className="mt-1.5 space-y-[3px]">
            <Toggle checked={cmp.sync} onChange={(v) => set({ sync: v })} label="Synchronize camera + slice" />
            <div className="mono flex items-center justify-between text-[8px] text-[#5f6675]"><span className="flex items-center gap-1"><Camera size={9} /> orbit lock</span><span className="text-[#65e5b1]">active</span></div>
            <div className="mono flex items-center justify-between text-[8px] text-[#5f6675]"><span className="flex items-center gap-1"><Move3d size={9} /> slice transform</span><span className="text-[#65e5b1]">active</span></div>
          </div>
        )}
      </Section>

      <Section title="Compared runs" icon={<Layers size={11} />} dense>
        <div className="space-y-1">
          {[
            { id: "A", name: "Baseline", sub: "SIMPLE-C · 2 400 it · 8.41 M", color: "#AAB1C0", solver: "converged", tag: "baseline" },
            { id: "B", name: "Adjoint‑optimized #37", sub: "SIMPLE-C · 2 400 it · 8.41 M", color: "#4D7CFF", solver: "converged", tag: "field-aligned ribs" },
          ].map((r) => (
            <div key={r.id} className="rounded-[4px] border border-[#2a2c34] bg-[#111217] p-1.5">
              <div className="flex items-center gap-1.5">
                <span className="flex h-[17px] w-[17px] items-center justify-center rounded-[3px] text-[9px] font-semibold" style={{ background: `${r.color}1c`, color: r.color, border: `1px solid ${r.color}44` }}>{r.id}</span>
                <span className="min-w-0 flex-1">
                  <span className="block truncate text-[10px] text-[#d3d8e3]">{r.name}</span>
                  <span className="mono block truncate text-[7.5px] text-[#5f6675]">{r.sub}</span>
                </span>
                <Chip tone="green">{r.solver}</Chip>
              </div>
              <div className="mono mt-1 flex items-center justify-between text-[8px] text-[#646b7a]">
                <span>{r.tag}</span><span>CFDFLD03 · {r.id}</span>
              </div>
              <div className="mt-1 h-[3px] w-full overflow-hidden rounded-full bg-[#22232a]">
                <div className="h-full w-full" style={{ background: r.id === "A" ? "#6b7180" : "linear-gradient(90deg,#004AFD,#41E0FF)" }} />
              </div>
            </div>
          ))}
        </div>
      </Section>

      <Section title="Differential field" icon={<Sliders size={11} />} dense>
        <div className="flex items-center justify-between py-1">
          <span className="text-[9.5px] text-[#7f8594]">Δ of field</span>
          <select
            value={cmp.field}
            onChange={(e) => set({ field: e.target.value as FieldKey })}
            className="h-[24px] rounded-[4px] border border-[#2b2d35] bg-[#0f1014] px-1.5 text-[9.5px] text-[#c6ccd8]"
          >
            {FIELD_OPTS.map((f) => <option key={f} value={f} className="bg-[#17181d]">{FIELDS[f].sym} · {FIELDS[f].label}</option>)}
          </select>
        </div>
        <Colorbar colormap="coolwarm" min={-1} max={1} center={0} label="Moreland cool–warm · Oklab interpolated" digits={1} />
        <div className="mono mt-1 grid grid-cols-2 gap-1 text-[8px]">
          <div className="flex justify-between rounded-[3px] border border-[#2a3040] bg-[#0d1220] px-1.5 py-1"><span className="text-[#8fb0ff]">− Δ</span><span className="text-[#7f91b4]">B &lt; A</span></div>
          <div className="flex justify-between rounded-[3px] border border-[#3f3030] bg-[#160f10] px-1.5 py-1"><span className="text-[#ff9aab]">+ Δ</span><span className="text-[#b08a92]">B &gt; A</span></div>
        </div>
        <div className="mt-1 flex items-center justify-between">
          <Toggle checked={cmp.showContours} onChange={(v) => set({ showContours: v })} label="Isocontours" />
          <Chip tone="amber">zero-centered</Chip>
        </div>
        <div className="mt-1.5 flex items-center justify-between">
          <Toggle checked={cmp.slice} onChange={(v) => set({ slice: v })} label="Clip slice x = const" />
        </div>
        {cmp.slice && <Slider label="slice position x" value={cmp.sliceX} onChange={(v) => set({ sliceX: v })} min={-1.5} max={1.6} step={0.01} digits={2} unit=" m" />}
      </Section>

      <Section title="Compute" icon={<Zap size={11} />} defaultOpen={false} dense>
        <div className="mono space-y-1 text-[8px] text-[#6d7484]">
          <div className="flex justify-between"><span>kernel</span><span className="text-[#a7aebc]">delta_field.wgsl</span></div>
          <div className="flex justify-between"><span>upload</span><span className="text-[#a7aebc]">CFDFLD03 float32</span></div>
          <div className="flex justify-between"><span>frame time</span><span className="text-[#65e5b1]">1.24 ms</span></div>
          <div className="flex justify-between"><span>target</span><span className="text-[#a7aebc]">120 Hz</span></div>
        </div>
      </Section>

      <div className="mt-auto shrink-0 border-t border-[#24252c] p-2">
        <Btn size="sm" variant="primary" className="w-full" icon={<Check size={11} />} onClick={() => c.notify("Run B design accepted · adjoint #37 promoted to design candidate")}>Accept Run B design</Btn>
      </div>
    </div>
  );
}

export function RightCompare() {
  const c = useCockpit();
  const rows = useMemo(() => KPIS.map((k) => {
    const delta = k.b - k.a;
    const pct = (delta / Math.abs(k.a)) * 100;
    const better = k.better === "lower" ? delta < 0 : delta > 0;
    return { ...k, delta, pct, better };
  }), []);
  const [expanded, setExpanded] = useState(true);
  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <TrendingDown size={12} className="text-[#65e5b1]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Delta summary</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">B − A</span>
      </div>

      <div className="p-2.5">
        <div className="rounded-[5px] border border-[#2a3b33] bg-[#0e1713] p-2">
          <div className="mb-1 flex items-center gap-1 text-[8.5px] uppercase tracking-wider text-[#6fa38c]"><Sparkles size={10} /> Design verdict</div>
          <div className="text-[11px] font-medium leading-tight text-[#d9efe5]">Adjoint #37 improves 6 of 7 KPIs</div>
          <div className="mt-1 flex items-center gap-1.5">
            <Chip tone="green">Pareto-optimal</Chip>
            <Chip tone="blue">−12.8 drag counts</Chip>
          </div>
        </div>
        <div className="mt-2 grid grid-cols-3 gap-1">
          <StatBox label="ΔCd" value="−12.8" unit="cts" tone="green" />
          <StatBox label="ΔTmax" value="−54" unit="K" tone="green" />
          <StatBox label="Δp₀" value="−14.9" unit="%" tone="green" />
        </div>
      </div>

      <Section title="KPI deltas" icon={<Gauge size={11} />} right={<button onClick={() => setExpanded((v) => !v)} className="text-[#6d7484]"><ChevronDown size={11} className={cn("transition-transform", !expanded && "-rotate-90")} /></button>} dense>
        {expanded && (
          <div className="space-y-1">
            <div className="mono grid grid-cols-[1fr_44px_44px_58px] gap-1 pb-0.5 text-right text-[7px] uppercase tracking-wider text-[#565d6b]">
              <span className="text-left">metric</span><span>A</span><span>B</span><span>Δ</span>
            </div>
            {rows.map((r) => (
              <div key={r.key} className="mono grid grid-cols-[1fr_44px_44px_58px] items-center gap-1 rounded-[3px] border border-[#25272f] bg-[#0f1014] px-1.5 py-[4px] text-right text-[8px] tabular">
                <span className="truncate text-left text-[#9aa1af]">{r.sym}</span>
                <span className="text-[#848b9a]">{r.a.toFixed(r.digits > 4 ? 4 : r.digits)}</span>
                <span className="text-[#c3cffa]">{r.b.toFixed(r.digits > 4 ? 4 : r.digits)}</span>
                <span className={cn("flex items-center justify-end gap-0.5", r.better ? "text-[#65e5b1]" : "text-[#ff9aab]")}>
                  {r.better ? <ArrowDownRight size={8} /> : <ArrowUpRight size={8} />}
                  {Math.abs(r.pct) < 0.05 ? "≈" : `${r.pct > 0 ? "+" : ""}${r.pct.toFixed(2)}%`}
                </span>
              </div>
            ))}
            <div className="mono mt-1 flex items-center justify-between text-[7.5px] text-[#5f6675]">
              <span>ΔCd in drag counts = Δ(0.5 Cd) × 10⁴</span>
              <span className="text-[#65e5b1]">6 improved</span>
            </div>
          </div>
        )}
      </Section>

      <Section title="Δφ distribution" icon={<ScanLine size={11} />} dense>
        <div className="space-y-1">
          {[
            { k: "min ΔCp", v: -0.184, tone: "text-[#8fb0ff]" },
            { k: "mean ΔCp", v: -0.042, tone: "text-[#b7bfcd]" },
            { k: "max ΔCp", v: 0.118, tone: "text-[#ff9aab]" },
            { k: "σ(Δφ)", v: 0.051, tone: "text-[#b7bfcd]" },
          ].map((x) => (
            <div key={x.k} className="flex items-center justify-between text-[8.5px]">
              <span className="text-[#6d7484]">{x.k}</span><span className={cn("mono", x.tone)}>{x.v.toFixed(3)}</span>
            </div>
          ))}
          <div className="mt-1 h-[42px] w-full overflow-hidden rounded-[3px] border border-[#2b2d35]">
            <svg viewBox="0 0 200 42" className="h-full w-full" preserveAspectRatio="none">
              {Array.from({ length: 34 }).map((_, i) => {
                const x = i * 6;
                const h = 6 + 30 * Math.exp(-Math.pow((i - 17) / 8, 2));
                return <rect key={i} x={x + 0.6} y={42 - h} width="4.6" height={h} fill={i < 17 ? "#4d7cff" : "#e2736a"} opacity="0.75" />;
              })}
            </svg>
          </div>
          <div className="mono flex justify-between text-[7.5px] text-[#5d6371]"><span>−0.35</span><span className="text-[#9aa1af]">Δφ = 0</span><span>+0.35</span></div>
        </div>
      </Section>

      <Section title="Consistency" icon={<Target size={11} />} defaultOpen={false} dense>
        <div className="mono space-y-[3px] text-[8px]">
          {[["mesh identity", "bit-identical"], ["camera sync", "quaternion-locked"], ["slice sync", "shared uniform"], ["fields", "CFDFLD03 checksum ok"]].map(([a, b]) => (
            <div key={a} className="flex justify-between gap-2"><span className="text-[#5f6675]">{a}</span><span className="text-[#65e5b1]">{b}</span></div>
          ))}
        </div>
      </Section>

      <div className="mt-auto shrink-0 border-t border-[#24252c] p-2">
        <div className="grid grid-cols-2 gap-1">
          <Btn size="sm" icon={<Download size={11} />} onClick={() => c.notify("Δ KPI table exported · CSV + LaTeX")}>Export</Btn>
          <Btn size="sm" icon={<Maximize2 size={11} />} onClick={() => c.notify("Differential viewport opened fullscreen")}>Compare</Btn>
        </div>
      </div>
    </div>
  );
}

export function CompareDock() {
  const c = useCockpit();
  const rows = useMemo(() => KPIS.map((k) => {
    const delta = k.b - k.a;
    const pct = (delta / Math.abs(k.a)) * 100;
    return { ...k, delta, pct, better: k.better === "lower" ? delta < 0 : delta > 0 };
  }), []);
  const [view, setView] = useState<"table" | "bars">("table");
  return (
    <div className="flex h-full min-h-0 flex-col border-t border-[#2a2b33] bg-[#0f1014]">
      <div className="flex h-[30px] shrink-0 items-center gap-1.5 border-b border-[#24252c] px-2">
        <span className="flex items-center gap-1 text-[9px] font-medium uppercase tracking-[0.09em] text-[#9aa1af]">
          <Grid2x2 size={10} className="text-[#8fb0ff]" /> Differential KPI & Pareto
        </span>
        <Chip tone="green" dot="#23D18B">GPU Δ live</Chip>
        <span className="mono text-[8px] text-[#5f6675]">Δφ = φB − φA · Moreland Oklab</span>
        <div className="ml-auto flex items-center gap-1">
          <Segmented options={[{ value: "table", label: "Table" }, { value: "bars", label: "Bars" }]} value={view} onChange={(v) => setView(v)} />
          <span className="mono flex items-center gap-1 rounded-[3px] border border-[#2b3a33] bg-[#0d1713] px-1.5 py-[2px] text-[8px] text-[#65e5b1]"><Zap size={8} /> 120 Hz</span>
        </div>
      </div>
      <div className="scroll grid min-h-0 flex-1 grid-cols-1 gap-1.5 overflow-y-auto p-1.5 xl:grid-cols-[1.7fr_1.15fr_1fr]">
        <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
          <div className="mb-1 flex items-center justify-between">
            <span className="text-[9.5px] font-medium text-[#d6dbe6]">Baseline A vs adjoint B</span>
            <span className="mono text-[8px] text-[#646b7a]">mesh identity · bit-identical</span>
          </div>
          {view === "table" ? (
            <div>
              <div className="mono grid grid-cols-[1.3fr_0.8fr_0.8fr_0.9fr_0.9fr] gap-1 border-b border-[#282a32] pb-1 text-right text-[7px] uppercase tracking-wider text-[#565d6b]">
                <span className="text-left">KPI</span><span>A</span><span>B</span><span>Δ</span><span>verdict</span>
              </div>
              {rows.map((r) => (
                <div key={r.key} className="grid grid-cols-[1.3fr_0.8fr_0.8fr_0.9fr_0.9fr] items-center gap-1 border-b border-[#202128] py-[3.5px] text-right text-[8.5px] last:border-0">
                  <span className="truncate text-left text-[#a3aab8]">{r.label}</span>
                  <span className="mono tabular text-[#7f8594]">{r.a.toLocaleString("en-US", { maximumFractionDigits: r.digits })}</span>
                  <span className="mono tabular text-[#c3cffa]">{r.b.toLocaleString("en-US", { maximumFractionDigits: r.digits })}</span>
                  <span className={cn("mono tabular", r.better ? "text-[#65e5b1]" : "text-[#ff9aab]")}>
                    {r.key === "cd" ? `${(-r.delta * 10000).toFixed(1)} cts` : `${r.delta > 0 ? "+" : ""}${r.delta.toLocaleString("en-US", { maximumFractionDigits: r.digits })} ${r.unit === "—" ? "" : r.unit}`}
                  </span>
                  <span className="flex justify-end">
                    <span className={cn("mono inline-flex items-center gap-0.5 rounded-[2px] border px-1 py-[1px] text-[7px]", r.better ? "border-[#23D18B]/30 bg-[#23D18B]/10 text-[#65e5b1]" : "border-[#EF233C]/30 bg-[#EF233C]/10 text-[#ff9aab]")}>
                      {r.better ? <Check size={7} /> : <TriangleAlert size={7} />}{r.better ? "improved" : "regressed"}
                    </span>
                  </span>
                </div>
              ))}
            </div>
          ) : (
            <div className="space-y-[5px]">
              {rows.map((r) => {
                const frac = Math.min(1, Math.abs(r.pct) / 15);
                return (
                  <div key={r.key} className="flex items-center gap-2">
                    <span className="w-[72px] shrink-0 truncate text-[8.5px] text-[#8a91a0]">{r.sym}</span>
                    <div className="relative h-[6px] flex-1 rounded-full bg-[#1c1d24]">
                      <div className="absolute left-1/2 top-0 h-full w-px bg-[#383a44]" />
                      <div className={cn("absolute top-0 h-full rounded-full", r.better ? "bg-[#23D18B]" : "bg-[#EF233C]")} style={{ width: `${frac * 50}%`, [r.pct >= 0 ? "left" : "right"]: "50%" } as React.CSSProperties} />
                    </div>
                    <span className={cn("mono w-[52px] shrink-0 text-right text-[8px]", r.better ? "text-[#65e5b1]" : "text-[#ff9aab]")}>{r.pct > 0 ? "+" : ""}{r.pct.toFixed(2)}%</span>
                  </div>
                );
              })}
            </div>
          )}
          <div className="mono mt-1.5 flex items-center gap-2 rounded-[3px] border border-[#252b38] bg-[#0c1018] px-1.5 py-1 text-[7.5px] text-[#7f8ba6]">
            <ArrowLeftRight size={9} /> ΔCd reported in drag counts; p₀ loss relative to inlet total pressure; Tmax over all wall faces.
          </div>
        </div>
        <ParetoChart points={PARETO} xLabel="total pressure loss Δp₀ [%]" yLabel="drag coefficient [counts]" height={196} />
        <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
          <div className="mb-1 flex items-center justify-between">
            <span className="text-[9.5px] font-medium text-[#d6dbe6]">Field difference sample</span>
            <Chip tone="cyan">Δφ</Chip>
          </div>
          <div className="grid grid-cols-2 gap-1">
            {[
              { k: "Δp mean", v: "−1.84 %", tone: "text-[#65e5b1]" },
              { k: "ΔT wall max", v: "−21.6 K", tone: "text-[#65e5b1]" },
              { k: "separation index", v: "0.142", tone: "text-[#b7bfcd]" },
              { k: "cells clamped", v: "312", tone: "text-[#ffcb6b]" },
            ].map((x) => (
              <div key={x.k} className="rounded-[3px] border border-[#25272f] bg-[#0d0e12] px-1.5 py-1">
                <div className="text-[7px] uppercase tracking-wider text-[#5d6371]">{x.k}</div>
                <div className={cn("mono text-[9.5px]", x.tone)}>{x.v}</div>
              </div>
            ))}
          </div>
          <div className="mt-1.5 text-[8.5px] leading-relaxed text-[#7a8190]">
            Run B reduces the cross-passage pressure gradient; the Δφ field is strictly centered at zero with ±symmetric Moreland Oklab bins.
          </div>
          <div className="mt-1.5 grid grid-cols-2 gap-1">
            <Btn size="sm" icon={<Play size={10} />} onClick={() => c.notify("Delta animation sweep started · A → B morph")}>Animate</Btn>
            <Btn size="sm" icon={<Download size={10} />} onClick={() => c.notify("Differential dataset exported to CFDFLD03 delta.bin")}>Δ field</Btn>
          </div>
        </div>
      </div>
    </div>
  );
}
