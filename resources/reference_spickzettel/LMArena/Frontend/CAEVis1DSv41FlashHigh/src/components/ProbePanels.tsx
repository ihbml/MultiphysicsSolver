import { useMemo, useState } from "react";
import {
  Crosshair, Download, FileSpreadsheet, FlaskConical, Gauge,
  Layers, Move3d, Play, Radio, Ruler, ScanLine, Sparkles, Target, Waves,
} from "lucide-react";
import { useCockpit } from "../store";
import { FIELDS, FieldKey, chordProfile, lawOfWall, lineProfile, uPlusSamples } from "../lib/sim";
import { Btn, Chip, NumField, Section, Segmented, Slider, StatBox, Toggle, cn } from "./ui";
import { ProfileChart } from "./Charts";

const FIELD_OPTS: FieldKey[] = ["Cp", "p", "U", "T", "Mach", "yPlus", "tau_w", "Nu", "wallT"];

export function LeftProbe() {
  const c = useCockpit();
  const p = c.probe;
  const set = (patch: Partial<typeof p>) => c.setProbe((s) => ({ ...s, ...patch }));
  const [chordStart, setChordStart] = useState(0);
  const [chordEnd, setChordEnd] = useState(0.32);
  const [spanStation, setSpanStation] = useState(0.5);
  const [sliceNormal, setSliceNormal] = useState<[number, number, number]>([0.24, 0, 0.97]);
  const Vec = ({ label, value, onChange }: { label: string; value: [number, number, number]; onChange: (v: [number, number, number]) => void }) => (
    <div className="mb-1">
      <div className="mb-1 text-[8px] uppercase tracking-[0.08em] text-[#5d6371]">{label}</div>
      <div className="grid grid-cols-3 gap-1">
        {value.map((v, i) => (
          <div key={i} className="flex h-[24px] items-center gap-0.5 rounded-[4px] border border-[#292b33] bg-[#0f1014] pl-1">
            <span className={cn("mono text-[7.5px]", i === 0 ? "text-[#ff8798]" : i === 1 ? "text-[#65e5b1]" : "text-[#8fb0ff]")}>{["x", "y", "z"][i]}</span>
            <NumField value={Number(v.toFixed(3))} onChange={(nv) => { const a = [...value] as [number, number, number]; a[i] = nv; onChange(a); }} width="w-full" digits={2} />
          </div>
        ))}
      </div>
    </div>
  );

  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <Ruler size={12} className="text-[#8fb0ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Line probe & chord slicer</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">3D rake</span>
      </div>

      <Section title="Probe definition" icon={<Crosshair size={11} />} dense>
        <Segmented
          full
          options={[{ value: "line", label: "3D line pA↔pB" }, { value: "chord", label: "Surface chord" }]}
          value={p.kind}
          onChange={(v) => set({ kind: v })}
        />
        <div className="mt-2">
          {p.kind === "line" ? (
            <>
              <Vec label="endpoint pA" value={p.a} onChange={(a) => set({ a })} />
              <Vec label="endpoint pB" value={p.b} onChange={(b) => set({ b })} />
              <div className="mono mt-1 flex items-center justify-between rounded-[4px] border border-[#2b2d35] bg-[#0f1014] px-1.5 py-1 text-[8px] text-[#7a8292]">
                <span>direction n̂</span>
                <span className="text-[#b7bfcd]">
                  {(() => {
                    const d = [p.b[0] - p.a[0], p.b[1] - p.a[1], p.b[2] - p.a[2]];
                    const l = Math.hypot(...d) || 1;
                    return d.map((v) => (v / l).toFixed(3)).join(", ");
                  })()}
                </span>
              </div>
            </>
          ) : (
            <>
              <div className="mb-2 rounded-[4px] border border-[#2b3a5c] bg-[#0d1320] p-1.5">
                <div className="mb-1 flex items-center gap-1 text-[8.5px] text-[#8fb0ff]"><ScanLine size={10} /> plane–wall intersection</div>
                <div className="mono text-[8px] leading-relaxed text-[#7f91b4]">
                  Π: n̂ = (0.24, 0.00, 0.97)<br />
                  x/c ∈ [0.000, 1.000] · span 50 %<br />
                  intersection curve · 1 connected loop
                </div>
              </div>
              <Slider label="chord start x/c" value={chordStart} onChange={setChordStart} min={0} max={0.5} step={0.01} digits={2} />
              <Slider label="chord end x/c" value={chordEnd} onChange={setChordEnd} min={0.6} max={1} step={0.01} digits={2} />
              <Slider label="span station y/b" value={spanStation} onChange={setSpanStation} min={0} max={1} step={0.05} digits={2} />
              <Vec label="slice-plane normal" value={sliceNormal} onChange={setSliceNormal} />
            </>
          )}
        </div>
        <Slider label="sample count" value={p.samples} onChange={(v) => set({ samples: v })} min={16} max={256} step={8} digits={0} accent />
        <div className="mt-1 flex items-center justify-between">
          <Toggle checked={p.followSurface} onChange={(v) => set({ followSurface: v })} label="Snap to wall surface" />
          <Chip tone="blue">barycentric</Chip>
        </div>
        <div className="mono mt-1 flex items-center gap-1 text-[8px] leading-relaxed text-[#5f6675]">
          <Layers size={9} className="shrink-0" /> CSF cell search → trilinear weights per sample → wall-normal extrapolation
        </div>
      </Section>

      <Section title="Sampled field" icon={<Waves size={11} />} dense>
        <div className="grid grid-cols-3 gap-1">
          {FIELD_OPTS.map((f) => (
            <button
              key={f}
              onClick={() => set({ field: f })}
              className={cn("flex h-[25px] items-center justify-between gap-1 rounded-[4px] border px-1.5 transition-colors",
                p.field === f ? "border-[#3c5ea8] bg-[#0f1930] text-[#cfe0ff]" : "border-[#292b33] bg-[#101115] text-[#8a91a0] hover:border-[#3a3d48]")}
            >
              <span className="mono text-[9.5px]">{FIELDS[f].sym}</span>
              <span className="mono truncate text-[6.5px] text-[#5f6777]">{FIELDS[f].unit}</span>
            </button>
          ))}
        </div>
        <div className="mt-2 space-y-[3px]">
          <Toggle checked={p.runB} onChange={(v) => set({ runB: v })} label="Overlay Run B (adjoint #37)" />
          <Toggle checked={p.invertY} onChange={(v) => set({ invertY: v })} label="Invert Cp y-axis (aero)" />
          <Toggle checked={p.targetBands} onChange={(v) => set({ targetBands: v })} label="Highlight y⁺ = 1 / 30 bands" />
          <Toggle checked={p.lawWall} onChange={(v) => set({ lawWall: v })} label="Law-of-the-wall reference" />
        </div>
      </Section>

      <Section title="Probe transform" icon={<Move3d size={11} />} defaultOpen={false} dense>
        <div className="space-y-1 text-[9px]">
          {[["frame", "global · Cartesian"], ["rotation", "aligned to surface normal"], ["active run", "A ↔ B synchronized"], ["units", "SI · m, m/s, Pa, K"]].map(([a, b]) => (
            <div key={a} className="flex justify-between gap-2"><span className="text-[#6d7484]">{a}</span><span className="mono truncate text-[#a7aebc]">{b}</span></div>
          ))}
        </div>
      </Section>

      <div className="mt-auto shrink-0 border-t border-[#24252c] p-2">
        <div className="mono flex items-center justify-between text-[8px] text-[#5d6371]">
          <span className="flex items-center gap-1"><Radio size={9} className="text-[#23D18B]" /> live sampling</span>
          <span className="text-[#8fb0ff]">{p.samples} pts · 0.42 ms</span>
        </div>
      </div>
    </div>
  );
}

export function RightProbe() {
  const c = useCockpit();
  const p = c.probe;
  const dataA = useMemo(() => lineProfile(p.field, "A", p.samples, p.a, p.b), [p.field, p.samples, p.a, p.b]);
  const dataB = useMemo(() => lineProfile(p.field, "B", p.samples, p.a, p.b), [p.field, p.samples, p.a, p.b]);
  const valsA = dataA.map((d) => d.y);
  const mins = { a: Math.min(...valsA), b: Math.min(...dataB.map((d) => d.y)) };
  const maxs = { a: Math.max(...valsA), b: Math.max(...dataB.map((d) => d.y)) };
  const mean = valsA.reduce((s, v) => s + v, 0) / valsA.length;
  const meanB = dataB.reduce((s, v) => s + v.y, 0) / dataB.length;
  const L = Math.hypot(p.b[0] - p.a[0], p.b[1] - p.a[1], p.b[2] - p.a[2]);
  const fmt = (v: number) => (Math.abs(v) >= 1000 ? v.toFixed(0) : v.toFixed(FIELDS[p.field].digits));

  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <Gauge size={12} className="text-[#8fb0ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Probe telemetry</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">A/B</span>
      </div>
      <div className="space-y-1.5 p-2.5">
        <div className="flex gap-1.5">
          <StatBox label="rake length" value={L.toFixed(3)} unit="m" />
          <StatBox label="samples" value={p.samples} unit="pts" tone="blue" />
        </div>
        <div className="rounded-[5px] border border-[#292b33] bg-[#101116] p-2">
          <div className="mb-1.5 flex items-center justify-between">
            <span className="text-[9px] text-[#8a91a0]">Field statistics · {FIELDS[p.field].sym}</span>
            <span className="mono text-[8px] text-[#5f6777]">{FIELDS[p.field].unit}</span>
          </div>
          <div className="grid grid-cols-3 gap-1 text-center">
            {[
              { k: "min", a: mins.a, b: mins.b },
              { k: "mean", a: mean, b: meanB },
              { k: "max", a: maxs.a, b: maxs.b },
            ].map((s) => (
              <div key={s.k} className="rounded-[3px] bg-[#0c0d11] px-1 py-1">
                <div className="text-[7px] uppercase tracking-wider text-[#5d6371]">{s.k}</div>
                <div className="mono text-[10px] text-[#e4e8f1]">{fmt(s.a)}</div>
                <div className="mono text-[8px] text-[#5f9dff]">{fmt(s.b)}</div>
              </div>
            ))}
          </div>
          <div className="mt-1.5 flex items-center justify-between border-t border-[#24252c] pt-1.5">
            <span className="text-[8.5px] text-[#7a8190]">Δ mean (B − A)</span>
            <span className={cn("mono text-[9.5px]", meanB - mean < 0 ? "text-[#65e5b1]" : "text-[#ffcb6b]")}>{(meanB - mean >= 0 ? "+" : "") + fmt(meanB - mean)} {FIELDS[p.field].unit}</span>
          </div>
          <div className="mt-1 flex items-center justify-between">
            <span className="text-[8.5px] text-[#7a8190]">max sensitivity ∂φ/∂rpm</span>
            <span className="mono text-[9px] text-[#b7bfcd]">{(Math.abs(meanB - mean) / 12400 * 1e4).toFixed(3)} ×10⁻⁴</span>
          </div>
        </div>

        <div className="rounded-[5px] border border-[#292b33] bg-[#101116] p-2">
          <div className="mb-1 flex items-center justify-between">
            <span className="text-[9px] text-[#8a91a0]">Sample table</span>
            <Chip tone="blue">trilinear</Chip>
          </div>
          <div className="grid grid-cols-[1fr_1fr_1fr_1.1fr] gap-x-1 border-b border-[#282a32] pb-1 text-[7px] uppercase tracking-wider text-[#5a6170]">
            <span>#</span><span className="text-right">x/c</span><span className="text-right">A</span><span className="text-right">B</span>
          </div>
          <div className="scroll mt-0.5 max-h-[148px] overflow-y-auto">
            {dataA.filter((_, i) => i % Math.max(1, Math.floor(p.samples / 12)) === 0).map((d, i) => {
              const idx = i * Math.max(1, Math.floor(p.samples / 12));
              return (
                <div key={i} className="mono grid grid-cols-[1fr_1fr_1fr_1.1fr] gap-x-1 py-[1.5px] text-[7.5px] tabular">
                  <span className="text-[#4f5564]">{String(idx).padStart(2, "0")}</span>
                  <span className="text-right text-[#7a8190]">{(d.x / 1.62).toFixed(3)}</span>
                  <span className="text-right text-[#b7bfcd]">{fmt(d.y)}</span>
                  <span className="text-right text-[#7ea8ff]">{fmt(dataB[idx]?.y ?? d.y)}</span>
                </div>
              );
            })}
          </div>
        </div>

        <div className="rounded-[5px] border border-[#27343a] bg-[#0b1417] p-2">
          <div className="mb-1 flex items-center gap-1 text-[8.5px] text-[#7fb6bf]"><FlaskConical size={10} /> Interpolation audit</div>
          <div className="mono space-y-[2px] text-[8px] text-[#69979f]">
            <div className="flex justify-between"><span>cell hits</span><span>{p.samples} / {p.samples}</span></div>
            <div className="flex justify-between"><span>outside domain</span><span>0</span></div>
            <div className="flex justify-between"><span>max residual</span><span>2.4e-07</span></div>
            <div className="flex justify-between"><span>wall shear frame</span><span>tangential</span></div>
          </div>
        </div>

        <div className="grid grid-cols-2 gap-1">
          <Btn size="sm" icon={<FileSpreadsheet size={11} />} onClick={() => c.notify("Probe CSV exported · 64 rows × 12 fields")}>CSV</Btn>
          <Btn size="sm" icon={<Download size={11} />} onClick={() => c.notify("Publication SVG queued · 600 dpi, SI labels")}>SVG plot</Btn>
        </div>
      </div>
    </div>
  );
}

export function ProbeDock() {
  const c = useCockpit();
  const [tab, setTab] = useState<"profiles" | "wall" | "rake">("profiles");
  const p = c.probe;
  const cpA = useMemo(() => chordProfile("Cp", "A"), []);
  const cpB = useMemo(() => chordProfile("Cp", "B"), []);
  const ypA = useMemo(() => chordProfile("yPlus", "A"), []);
  const ypB = useMemo(() => chordProfile("yPlus", "B"), []);
  const twA = useMemo(() => chordProfile("wallT", "A"), []);
  const twB = useMemo(() => chordProfile("wallT", "B"), []);
  const wall = useMemo(() => lawOfWall(), []);
  const uA = useMemo(() => uPlusSamples("A"), []);
  const uB = useMemo(() => uPlusSamples("B"), []);
  const rakeA = useMemo(() => lineProfile(p.field, "A", p.samples, p.a, p.b), [p.field, p.samples, p.a, p.b]);
  const rakeB = useMemo(() => lineProfile(p.field, "B", p.samples, p.a, p.b), [p.field, p.samples, p.a, p.b]);

  const cpMarker = cpA.reduce((a, b) => (b.y < a.y ? b : a), cpA[0]);

  return (
    <div className="flex h-full min-h-0 flex-col border-t border-[#2a2b33] bg-[#0f1014]">
      <div className="flex h-[30px] shrink-0 items-center gap-1.5 border-b border-[#24252c] px-2">
        <span className="flex items-center gap-1 text-[9px] font-medium uppercase tracking-[0.09em] text-[#9aa1af]">
          <Sparkles size={10} className="text-[#8fb0ff]" /> Synchronized probe plots
        </span>
        <Chip tone="green" dot="#23D18B">probe ↔ 3D</Chip>
        <div className="ml-2 flex items-center gap-[2px] rounded-[4px] border border-[#2b2c34] bg-[#111216] p-[1px]">
          {([["profiles", "Chordwise"], ["wall", "u⁺ · law of wall"], ["rake", "Rake profile"]] as const).map(([v, l]) => (
            <button key={v} onClick={() => setTab(v)} className={cn("h-[20px] rounded-[3px] px-2 text-[9px] transition-colors", tab === v ? "bg-[#2b2d36] text-white" : "text-[#7f8594] hover:text-[#c9cdd7]")}>{l}</button>
          ))}
        </div>
        <div className="mono ml-auto flex items-center gap-2 text-[8px] text-[#5f6675]">
          <span className="flex items-center gap-1"><span className="h-[2px] w-[9px] bg-[#AAB1C0]" />A</span>
          <span className="flex items-center gap-1"><span className="h-[2px] w-[9px] bg-[#4D7CFF]" />B</span>
          <span className="h-[9px] w-px bg-[#33343d]" />
          <span>{p.samples} samples · barycentric</span>
          <span className="h-[9px] w-px bg-[#33343d]" />
          <span className="text-[#8fb0ff]">egui_plot::Plot</span>
        </div>
      </div>
      <div className="scroll min-h-0 flex-1 overflow-y-auto p-1.5">
        {tab === "profiles" && (
          <div className="grid grid-cols-1 gap-1.5 xl:grid-cols-2">
            <ProfileChart
              title="Cp(x/c) · chordwise pressure" subtitle={`suction side · α = 31.2° · Re = 1.84e6`}
              series={[{ name: "Run A · baseline", color: "#AAB1C0", points: cpA, area: true }, ...(p.runB ? [{ name: "Run B · adjoint", color: "#4D7CFF", points: cpB, dashed: true, width: 1.3 }] : [])]}
              xLabel="x/c" yLabel="Cp [—]" invertY={p.invertY} height={186} hlines={[{ y: 0, color: "#4a5468", label: "Cp = 0", dashed: true }]}
              markers={[{ x: cpMarker.x, y: cpMarker.y, label: `Cp_min ${cpMarker.y.toFixed(2)}`, color: "#8fb0ff" }]}
              unit="—"
            />
            <ProfileChart
              title="Wall temperature Tw(x/c)" subtitle="adiabatic wall · conjugate solid interface"
              series={[{ name: "Run A · baseline", color: "#F09A4C", points: twA, area: true }, ...(p.runB ? [{ name: "Run B · adjoint", color: "#41E0FF", points: twB, dashed: true, width: 1.3 }] : [])]}
              xLabel="x/c" yLabel="Tw [K]" height={186} yDomain={[740, 1120]} unit="K"
            />
            <ProfileChart
              title="First-cell wall y⁺(x/c)" subtitle="target band y⁺ = 1 sublayer · y⁺ = 30 log-layer"
              series={[{ name: "Run A", color: "#AAB1C0", points: ypA }, ...(p.runB ? [{ name: "Run B", color: "#4D7CFF", points: ypB, dashed: true }] : [])]}
              xLabel="x/c" yLabel="y⁺ [—]" height={186} yDomain={[0, 36]}
              bands={p.targetBands ? [
                { from: 0, to: 1, color: "rgba(0,74,253,0.18)", label: "viscous sublayer" },
                { from: 28, to: 32, color: "rgba(255,176,32,0.14)", label: "log-layer target y⁺≈30" },
              ] : []}
              hlines={p.targetBands ? [{ y: 1, color: "#4D7CFF", label: "y⁺ = 1" }, { y: 30, color: "#FFB020", label: "y⁺ = 30", dashed: true }] : []}
            />
            <ProfileChart
              title="u⁺(y⁺) · boundary-layer velocity" subtitle="inner scaling · κ = 0.41, E = 9.8"
              series={[
                { name: "Run A", color: "#AAB1C0", points: uA, dots: true, width: 1.2 },
                ...(p.runB ? [{ name: "Run B", color: "#4D7CFF", points: uB, dots: true, width: 1.2 }] : []),
                ...(p.lawWall ? [
                  { name: "u⁺ = y⁺", color: "#6fbfa0", points: wall.visc, dashed: true, width: 1 },
                  { name: "log law", color: "#FFB020", points: wall.log.filter((d) => d.x > 10), dashed: true, width: 1 },
                ] : []),
              ]}
              xLabel="y⁺ [—] · log scale" yLabel="u⁺ [—]" xScale="log" xDomain={[0.4, 10000]} yDomain={[0, 34]} height={186}
              bands={p.targetBands ? [{ from: 0, to: 11.2, color: "rgba(77,124,255,0.06)", label: "viscous" }, { from: 30, to: 400, color: "rgba(255,176,32,0.05)", label: "log" }] : []}
            />
          </div>
        )}
        {tab === "wall" && (
          <div className="grid grid-cols-1 gap-1.5 xl:grid-cols-[1.6fr_1fr]">
            <ProfileChart
              title="u⁺(y⁺) · law-of-the-wall overlay" subtitle="barycentric wall data · 42 cells extruded through 34 prism layers"
              series={[
                { name: "Run A · baseline", color: "#AAB1C0", points: uA, dots: true, width: 1.4 },
                { name: "Run B · adjoint", color: "#4D7CFF", points: uB, dots: true, width: 1.4 },
                { name: "u⁺ = y⁺", color: "#6fbfa0", points: wall.visc, dashed: true },
                { name: "1/κ ln(E y⁺)", color: "#FFB020", points: wall.log, dashed: true },
              ]}
              xLabel="y⁺ [—] (log)" yLabel="u⁺ [—]" xScale="log" xDomain={[0.4, 10000]} yDomain={[0, 34]} height={330}
              hlines={[{ y: 11.2, color: "#4D7CFF", label: "buffer layer ≈ 11.2", dashed: true }]}
              markers={[{ x: 30, y: (1 / 0.41) * Math.log(30 * 0.41) + 5.2, label: "y⁺=30", color: "#FFB020" }]}
            />
            <div className="space-y-1.5">
              <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
                <div className="mb-1 text-[9.5px] font-medium text-[#d6dbe6]">Inner-layer resolution</div>
                <div className="grid grid-cols-2 gap-1">
                  {[
                    ["uτ (A)", "2.614 m/s"], ["uτ (B)", "2.708 m/s"],
                    ["Reτ", "1842"], ["δ99", "0.0412 m"],
                    ["y⁺₁ (A)", "1.02"], ["y⁺₁ (B)", "0.94"],
                  ].map(([k, v]) => (
                    <div key={k} className="rounded-[3px] border border-[#24262d] bg-[#0d0e12] px-1.5 py-1">
                      <div className="text-[7px] uppercase tracking-wider text-[#5d6371]">{k}</div>
                      <div className="mono text-[9px] text-[#b7bfcd]">{v}</div>
                    </div>
                  ))}
                </div>
              </div>
              <div className="rounded-[6px] border border-[#27343a] bg-[#0b1417] p-2">
                <div className="mb-1 flex items-center gap-1 text-[9px] text-[#7fb6bf]"><Target size={10} /> Wall treatment verdict</div>
                <div className="text-[8.5px] leading-relaxed text-[#7ba0a6]">
                  Both runs resolve the viscous sublayer (y⁺ &lt; 1.1). Run B keeps 2 more nodes inside y⁺ &lt; 5, reducing the law-of-the-wall mismatch to 3.1 % near the trailing edge. No wall function correction applied.
                </div>
                <div className="mono mt-1.5 flex items-center gap-1 text-[8px] text-[#65e5b1]"><Play size={9} /> low-Re resolved · k–ω SST</div>
              </div>
              <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
                <div className="mb-1 text-[9px] text-[#9aa1af]">u⁺ deviation from law</div>
                <div className="space-y-[3px]">
                  {[["y⁺ 5", 0.4], ["y⁺ 30", 1.2], ["y⁺ 100", -0.8], ["y⁺ 1000", 1.6]].map(([k, v]) => (
                    <div key={k as string} className="flex items-center gap-2">
                      <span className="mono w-[44px] text-[8px] text-[#6d7484]">{k}</span>
                      <div className="relative h-[4px] flex-1 rounded-full bg-[#1f2027]">
                        <div className="absolute left-1/2 top-0 h-full w-px bg-[#3a3c46]" />
                        <div className="absolute top-0 h-full rounded-full bg-[#FFB020]" style={{ width: `${Math.abs(v as number) * 12}%`, left: (v as number) > 0 ? "50%" : undefined, right: (v as number) < 0 ? "50%" : undefined }} />
                      </div>
                      <span className="mono w-[30px] text-right text-[8px] text-[#9aa1af]">{(v as number).toFixed(1)}</span>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          </div>
        )}
        {tab === "rake" && (
          <div className="grid grid-cols-1 gap-1.5 xl:grid-cols-2">
            <ProfileChart
              title={`${FIELDS[p.field].sym} along probe arclength · Run A/B`} subtitle={`${p.kind === "line" ? "3D rake pA → pB" : "surface chord slice"} · trilinear interpolation`}
              series={[{ name: "Run A", color: "#AAB1C0", points: rakeA, area: true }, ...(p.runB ? [{ name: "Δ pressure side (B)", color: "#4D7CFF", points: rakeB, dashed: true }] : [])]}
              xLabel="arclength s [m]" yLabel={`${FIELDS[p.field].sym} [${FIELDS[p.field].unit}]`} height={312} unit={FIELDS[p.field].unit}
            />
            <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
              <div className="mb-1.5 flex items-center justify-between">
                <span className="text-[9.5px] font-medium text-[#d6dbe6]">Δφ B − A along rake</span>
                <span className="mono text-[8px] text-[#8fb0ff]">diverging · centered 0</span>
              </div>
              <div className="h-11 w-full rounded-[2px]" style={{ background: "linear-gradient(90deg,#3b4cc0,#c9d6e8,#b40426)" }} />
              <div className="mt-2 grid grid-cols-3 gap-1">
                {[
                  { k: "min Δ", v: Math.min(...rakeB.map((d, i) => d.y - rakeA[i].y)) },
                  { k: "mean Δ", v: rakeB.reduce((s, d, i) => s + d.y - rakeA[i].y, 0) / rakeB.length },
                  { k: "max Δ", v: Math.max(...rakeB.map((d, i) => d.y - rakeA[i].y)) },
                ].map((x) => (
                  <div key={x.k} className="rounded-[3px] border border-[#25272f] bg-[#0d0e12] p-1.5 text-center">
                    <div className="text-[7px] uppercase tracking-wider text-[#5d6371]">{x.k}</div>
                    <div className="mono text-[10px] text-[#ffcdbc]">{x.v.toFixed(3)}</div>
                  </div>
                ))}
              </div>
              <div className="mt-2 space-y-[3px] text-[8px] text-[#6d7484]">
                <div className="flex justify-between"><span>total pressure recovery σ</span><span className="mono text-[#b7bfcd]">0.9612</span></div>
                <div className="flex justify-between"><span>wake half-width</span><span className="mono text-[#b7bfcd]">0.0384 m</span></div>
                <div className="flex justify-between"><span>integrated loss</span><span className="mono text-[#65e5b1]">−1.84 %</span></div>
              </div>
              <div className="mono mt-2 rounded-[4px] border border-[#2a3348] bg-[#0c1220] p-1.5 text-[8px] leading-relaxed text-[#7f91b4]">
                probe::sample(&amp;grid, pA, pB, 64)<br />
                → FieldSamples {`{ scalar: [f32; 64], run_b: [f32; 64] }`}
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
