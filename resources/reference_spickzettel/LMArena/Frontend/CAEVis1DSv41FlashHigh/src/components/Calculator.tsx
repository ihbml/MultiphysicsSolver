import { useMemo, useState } from "react";
import {
  ArrowRight, Check, CircleAlert, CircleCheck, Code2, Cpu, Database, FlaskConical,
  FunctionSquare, Gauge, Layers, Play, Plus, RotateCcw, Sigma, Sparkles, Terminal,
  TriangleAlert, Upload, Variable, Wand2, Zap,
} from "lucide-react";
import { useCockpit } from "../store";
import {
  compileField, EXAMPLES, formatDim, quantityName, RHAI_HEADER, SYMBOLS, TraceNode,
} from "../lib/units";
import { colorAt } from "../lib/colormaps";
import { Btn, Chip, SearchInput, Section, StatBox, cn } from "./ui";

type Log = { time: string; kind: "ok" | "err" | "info" | "run"; text: string };
const now = () => new Date().toLocaleTimeString("en-GB", { hour12: false });

const UNIT_FOR: Record<string, string> = {
  Pressure: "Pa", Density: "kg·m⁻³", Velocity: "m·s⁻¹", Temperature: "K", Length: "m",
  "Specific kinetic energy": "m²·s⁻²", "Dynamic viscosity": "kg·m⁻¹·s⁻¹",
  "Thermal conductivity": "W·m⁻¹·K⁻¹", "Volumetric entropy generation": "W·m⁻³·K⁻¹",
  Dimensionless: "—", Power: "W", "Specific heat": "J·kg⁻¹·K⁻¹", "Rate": "s⁻¹",
};
const RANGE_FOR: Record<string, [number, number]> = {
  Pressure: [84500, 121500], Density: [0.4, 4.2], Velocity: [60, 245], Temperature: [288, 1200],
  "Specific kinetic energy": [0, 30000], "Volumetric entropy generation": [0, 4200],
  Dimensionless: [-2.2, 1.2], "Specific heat": [800, 1200], Rate: [-4, 4],
};

export function LeftCalc() {
  const c = useCockpit();
  const result = useMemo(() => compileField(c.calcExpr), [c.calcExpr]);
  const [q, setQ] = useState("");
  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <FunctionSquare size={12} className="text-[#8fb0ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Field library</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">{c.derivedFields.length + 9} fields</span>
      </div>

      <Section title="Derived fields" icon={<Database size={11} />} right={<Chip tone="blue">CFDFLD03</Chip>} dense>
        <div className="space-y-1">
          {c.derivedFields.length === 0 && (
            <div className="rounded-[4px] border border-dashed border-[#30323c] bg-[#0f1014] px-2 py-2 text-center text-[8.5px] leading-relaxed text-[#626978]">
              No derived fields yet.<br />Compile a dimensionally valid expression to upload a CFDFLD03 scalar buffer.
            </div>
          )}
          {c.derivedFields.map((f) => (
            <button
              key={f.name}
              onClick={() => c.setActiveField("custom")}
              className={cn("flex w-full items-center gap-1.5 rounded-[4px] border px-1.5 py-[5px] text-left transition-colors",
                c.activeField === "custom" && c.uploaded?.name === f.name ? "border-[#3e5b9e] bg-[#0e1830]" : "border-[#292b33] bg-[#101115] hover:border-[#3a3d48]")}
            >
              <span className="flex h-5 w-5 items-center justify-center rounded-[3px] bg-[#004AFD]/15 text-[#8fb0ff]"><Sparkles size={10} /></span>
              <span className="min-w-0 flex-1">
                <span className="mono block truncate text-[9.5px] text-[#d3d8e3]">{f.name}</span>
                <span className="mono block truncate text-[7.5px] text-[#5f6675]">{f.quantity} · {f.unit}</span>
              </span>
              <span className="mono shrink-0 rounded-[2px] border border-[#23D18B]/25 bg-[#0d1713] px-1 text-[6.5px] text-[#65e5b1]">GPU</span>
            </button>
          ))}
        </div>
      </Section>

      <Section title="Base SI fields" icon={<Variable size={11} />} dense>
        <div className="mb-1"><SearchInput value={q} onChange={setQ} placeholder="search symbols…" /></div>
        <div className="space-y-[2px]">
          {Object.entries(SYMBOLS).filter(([k, s]) => (k + s.label).toLowerCase().includes(q.toLowerCase())).slice(0, 12).map(([k, s]) => (
            <button key={k} onClick={() => c.setCalcExpr((e) => `${e}${e.endsWith(" ") ? "" : ""}${k}`)} className="flex w-full items-center gap-1.5 rounded-[3px] px-1 py-[2px] text-left hover:bg-white/[0.04]">
              <span className={cn("mono w-[58px] shrink-0 truncate text-[9px]", s.t.shape === "vector" ? "text-[#c3b2ff]" : "text-[#8fb0ff]")}>{k}</span>
              <span className="min-w-0 flex-1 truncate text-[8px] text-[#828998]">{s.label}</span>
              <span className="mono shrink-0 text-[7.5px] text-[#5f6675]">{s.unit}</span>
            </button>
          ))}
        </div>
      </Section>

      <Section title="Expression library" icon={<FlaskConical size={11} />} dense>
        <div className="space-y-[3px]">
          {EXAMPLES.map((ex) => {
            const valid = compileField(ex.expr).ok;
            const active = c.calcExpr === ex.expr;
            return (
              <button
                key={ex.name}
                onClick={() => c.setCalcExpr(ex.expr)}
                className={cn("w-full rounded-[4px] border px-1.5 py-1 text-left transition-colors",
                  active ? "border-[#3e5b9e] bg-[#0e1830]" : "border-[#282a32] bg-[#101115] hover:border-[#3a3d48]")}
              >
                <div className="flex items-center gap-1">
                  {valid ? <CircleCheck size={9} className="shrink-0 text-[#23D18B]" /> : <CircleAlert size={9} className="shrink-0 text-[#EF233C]" />}
                  <span className="mono truncate text-[9px] text-[#b7c0d1]">{ex.name}</span>
                  {!valid && <span className="ml-auto rounded-[2px] border border-[#EF233C]/40 px-1 text-[6.5px] text-[#ff8798]">reject</span>}
                </div>
                <div className="mono mt-[2px] truncate text-[7.5px] text-[#5f6675]">{ex.note}</div>
              </button>
            );
          })}
        </div>
      </Section>

      <Section title="Compiler pipeline" icon={<Cpu size={11} />} dense>
        <div className="space-y-0">
          {[
            { n: "parse", desc: "recursive descent · AST", ok: !!result.ast || !result.ok, active: true },
            { n: "SI check", desc: "7-vector propagation", ok: result.ok, active: !!result.ok },
            { n: "codegen", desc: "rhai → WGSL", ok: result.ok, active: !!result.ok },
            { n: "GPU upload", desc: "CFDFLD03", ok: !!c.uploaded, active: !!c.uploaded },
            { n: "visualize", desc: "Oklab 1D LUT", ok: !!c.uploaded, active: c.activeField === "custom" },
          ].map((s, i) => (
            <div key={s.n} className="flex items-center gap-1.5 py-[3px]">
              <span className={cn("flex h-[13px] w-[13px] shrink-0 items-center justify-center rounded-full border text-[7px]",
                s.ok ? "border-[#23D18B]/40 bg-[#23D18B]/15 text-[#65e5b1]" : "border-[#3a3c46] bg-[#191a20] text-[#6d7484]")}>
                {s.ok ? <Check size={8} /> : i + 1}
              </span>
              <span className={cn("mono text-[8.5px]", s.active ? "text-[#b7bfcd]" : "text-[#6d7484]")}>{s.n}</span>
              <span className="ml-auto truncate text-[7.5px] text-[#565d6b]">{s.desc}</span>
            </div>
          ))}
        </div>
        <div className="mono mt-1 rounded-[3px] border border-[#252b38] bg-[#0c1018] px-1.5 py-1 text-[7.5px] leading-relaxed text-[#7f8ba6]">
          upload::ScalarField {`{ cells: 8_412_096, dtype: f32 }`}<br />
          expected {((8_412_096 * 4) / 1e6).toFixed(1)} MB · zero-copy bytemuck
        </div>
      </Section>
    </div>
  );
}

export function RightCalc() {
  const c = useCockpit();
  const result = useMemo(() => compileField(c.calcExpr), [c.calcExpr]);
  const [logs, setLogs] = useState<Log[]>([
    { time: now(), kind: "info", text: "rhai engine v1.19 · SI registry loaded (20 symbols)" },
    { time: now(), kind: "info", text: "field_calculator.rhai · sandbox = true · ops limit 50k" },
    { time: now(), kind: "ok", text: "CFDFLD03 reader attached · 8 412 096 f32 cells" },
  ]);
  const [q, setQ] = useState("");

  const run = () => {
    const stamp = now();
    setLogs((l) => [...l, { time: stamp, kind: "run", text: `> compile_field("${c.calcExpr.slice(0, 54)}${c.calcExpr.length > 54 ? "…" : ""}")` }]);
    if (result.ok) {
      setLogs((l) => [...l,
        { time: now(), kind: "ok", text: `  type = ${result.quantityName} [${result.dimsText}] shape = scalar` },
        { time: now(), kind: "ok", text: `  Const/Var/Field nodes checked · 0 dimension errors` },
        { time: now(), kind: "info", text: `  ready → upload_cfdfld03("${result.target || "derived"}")` },
      ]);
    } else {
      setLogs((l) => [...l, { time: now(), kind: "err", text: `  ${result.error?.message}` }, ...(result.error?.hint ? [{ time: now(), kind: "info" as const, text: `  hint: ${result.error.hint}` }] : [])]);
    }
  };

  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <Sigma size={12} className="text-[#c3b2ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">SI inspector & Rhai</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">dim⁷</span>
      </div>

      <div className="shrink-0 border-b border-[#24252c] p-2.5">
        <div className="mb-1.5 flex items-center justify-between">
          <span className="text-[9px] font-medium uppercase tracking-[0.09em] text-[#8a91a0]">Dimension vector</span>
          <Chip tone={result.ok ? "green" : "red"}>{result.ok ? "consistent" : "invalid"}</Chip>
        </div>
        <div className="grid grid-cols-7 gap-[2px]">
          {["kg", "m", "s", "K", "mol", "A", "cd"].map((b, i) => {
            const e = result.type?.dim[i] ?? 0;
            return (
              <div key={b} className={cn("rounded-[3px] border py-[3px] text-center", Math.abs(e) > 1e-9 ? "border-[#3e5b9e] bg-[#0e1830]" : "border-[#2a2b33] bg-[#101115]")}>
                <div className="mono text-[7px] text-[#6d7484]">{b}</div>
                <div className={cn("mono text-[9px]", Math.abs(e) > 1e-9 ? "text-[#9fbcff]" : "text-[#4f5563]")}>{e === 0 ? "0" : Number.isInteger(e) ? e : e.toFixed(1)}</div>
              </div>
            );
          })}
        </div>
        <div className="mono mt-1.5 flex items-center justify-between rounded-[3px] border border-[#25272f] bg-[#0d0e12] px-1.5 py-1">
          <span className="text-[8px] text-[#6d7484]">result</span>
          <span className={cn("truncate text-[8.5px]", result.ok ? "text-[#65e5b1]" : "text-[#ff8798]")}>{result.ok ? `[${result.dimsText}]` : "unresolved"}</span>
        </div>
      </div>

      <Section title="SI symbol registry" icon={<Gauge size={11} />} dense>
        <div className="mb-1"><SearchInput value={q} onChange={setQ} placeholder="quantity, unit…" /></div>
        <div className="scroll max-h-[168px] space-y-[2px] overflow-y-auto pr-0.5">
          {Object.entries(SYMBOLS).filter(([k, s]) => (k + s.label + s.unit).toLowerCase().includes(q.toLowerCase())).map(([k, s]) => (
            <button key={k} onClick={() => c.setCalcExpr((e) => `${e}${/[\s(]$/.test(e) ? "" : " * "}${k}`)} className="w-full rounded-[3px] border border-transparent px-1 py-[3px] text-left hover:border-[#30323c] hover:bg-[#181a20]">
              <div className="flex items-center gap-1">
                <span className={cn("mono shrink-0 text-[9px]", s.t.shape === "vector" ? "text-[#c3b2ff]" : "text-[#8fb0ff]")}>{k}</span>
                <span className={cn("mono shrink-0 rounded-[2px] border px-0.5 text-[6px] uppercase", s.t.shape === "vector" ? "border-[#A78BFA]/40 text-[#b8a5f5]" : "border-[#3b4a63] text-[#7f91b4]")}>{s.t.shape}</span>
                <span className="ml-auto mono shrink-0 text-[7.5px] text-[#6d7484]">{s.unit}</span>
              </div>
              <div className="mono truncate text-[7.5px] text-[#565d6b]">[{formatDim(s.t.dim)}] · {quantityName(s.t.dim)}</div>
            </button>
          ))}
        </div>
      </Section>

      <Section title="Rhai post-processing console" icon={<Terminal size={11} />} right={<Btn size="xs" variant="success" icon={<Play size={9} />} onClick={run}>Run</Btn>} dense>
        <div className="mb-1 rounded-[4px] border border-[#2a2733] bg-[#0c0a10] p-1.5">
          <div className="mono mb-1 flex items-center justify-between text-[7px] text-[#6b6182]"><span>field_calculator.rhai</span><span>read-only sandbox</span></div>
          <pre className="mono max-h-[92px] overflow-auto whitespace-pre-wrap text-[7.5px] leading-[1.6] text-[#9585bd]">{RHAI_HEADER}</pre>
        </div>
        <div className="rounded-[4px] border border-[#25272f] bg-[#0b0c10] p-1.5">
          <div className="mono mb-1 flex items-center justify-between text-[7px] text-[#565d6b]">
            <span className="flex items-center gap-1"><span className="h-[5px] w-[5px] rounded-full bg-[#23D18B] dot-live" /> stdout</span>
            <button className="text-[#6d7484] hover:text-[#b7bfcd]" onClick={() => setLogs([])}>clear</button>
          </div>
          <div className="scroll max-h-[148px] space-y-[2px] overflow-y-auto">
            {logs.length === 0 && <div className="mono text-[7.5px] text-[#4f5564]">// console cleared</div>}
            {logs.map((l, i) => (
              <div key={i} className="mono text-[7.5px] leading-[1.55]">
                <span className="text-[#4b5160]">{l.time} </span>
                <span className={l.kind === "err" ? "text-[#ff8798]" : l.kind === "ok" ? "text-[#65e5b1]" : l.kind === "run" ? "text-[#9fbcff]" : "text-[#7f8594]"}>{l.text}</span>
              </div>
            ))}
          </div>
        </div>
        <div className="mono mt-1 flex justify-between text-[7px] text-[#565d6b]"><span>type errors precede codegen</span><span className="text-[#8fb0ff]">rhai 1.19</span></div>
      </Section>

      <Section title="Validation rules" icon={<Layers size={11} />} defaultOpen={false} dense>
        <div className="space-y-[3px] text-[8px] leading-relaxed text-[#6d7484]">
          <div><span className="mono text-[#8fb0ff]">+ −</span> operands must match the same 7-vector</div>
          <div><span className="mono text-[#8fb0ff]">× ÷</span> exponents add / subtract per base</div>
          <div><span className="mono text-[#8fb0ff]">dot</span> inner product adds dimensions</div>
          <div><span className="mono text-[#8fb0ff]">grad</span> divides by length [m⁻¹]</div>
          <div><span className="mono text-[#8fb0ff]">sqrt</span> halves every exponent</div>
        </div>
      </Section>
    </div>
  );
}

function TraceTable({ trace }: { trace: TraceNode[] }) {
  const unique = trace.filter((t, i) => t.expr !== trace[i - 1]?.expr).slice(-12);
  return (
    <div className="overflow-hidden rounded-[4px] border border-[#25272f]">
      <div className="mono grid grid-cols-[1fr_1.1fr_0.8fr_0.6fr] gap-1 border-b border-[#282a32] bg-[#14151a] px-1.5 py-1 text-[7px] uppercase tracking-wider text-[#565d6b]">
        <span>AST node</span><span>SI dimension</span><span>quantity</span><span className="text-right">shape</span>
      </div>
      <div className="scroll max-h-[150px] overflow-y-auto">
        {unique.map((t, i) => (
          <div key={i} className="mono grid grid-cols-[1fr_1.1fr_0.8fr_0.6fr] items-center gap-1 border-b border-[#1e1f26] px-1.5 py-[3px] text-[8px] last:border-0">
            <span className="truncate text-[#aeb7c7]" title={t.expr}>{t.expr}</span>
            <span className="truncate text-[#8fb0ff]">[{t.dims}]</span>
            <span className="truncate text-[#7a8190]">{t.quantity}</span>
            <span className="text-right text-[#6d7484]">{t.shape}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

export function CalculatorCenter() {
  const c = useCockpit();
  const result = useMemo(() => compileField(c.calcExpr), [c.calcExpr]);
  const [tab, setTab] = useState<"result" | "ast" | "buffer">("result");
  const [dirty, setDirty] = useState(false);

  const unit = result.ok ? UNIT_FOR[result.quantityName || ""] || "—" : "—";
  const [minR, maxR] = result.ok ? RANGE_FOR[result.quantityName || "Dimensionless"] || [-1, 1] : [-1, 1];

  const applyFix = () => {
    let e = c.calcExpr;
    e = e.replace(/0\.5\s*\*\s*(dot\(\s*U\s*,\s*U\s*\))/, "0.5 * rho * $1");
    e = e.replace(/(dot\(\s*U\s*,\s*U\s*\))/, "rho * $1");
    c.setCalcExpr(e);
    c.notify("Suggested rho factor inserted — re-checking dimensions");
  };

  const upload = () => {
    if (!result.ok) return;
    const name = result.target || "derived_field";
    c.addDerivedField({
      name,
      unit,
      dims: result.dimsText || "1",
      quantity: result.quantityName || "Derived",
      min: minR,
      max: maxR,
    });
    setDirty(false);
  };

  const preview = useMemo(() => {
    const rows = 14, cols = 44;
    return Array.from({ length: rows * cols }).map((_, i) => {
      const x = (i % cols) / cols, y = Math.floor(i / cols) / rows;
      const base = 0.5 + 0.34 * Math.sin(x * 8.4 + 0.4) * Math.cos(y * 8.8 - 0.6) + 0.12 * Math.sin(x * 19 + y * 5);
      const value = result.ok ? base : base * 0.35;
      return colorAt(result.ok ? "turbo" : "coolwarm", Math.min(1, Math.max(0, value)));
    });
  }, [result.ok]);

  return (
    <div className="scroll h-full min-h-0 overflow-y-auto bg-[#0f1014] p-2.5">
      <div className="mb-2 flex items-start justify-between gap-2">
        <div>
          <div className="flex items-center gap-2">
            <span className="text-[13px] font-medium text-[#e9ebf0]">Live SI-checked field calculator</span>
            <Chip tone="blue">dim⁷</Chip>
            <Chip tone="violet">Rhai</Chip>
          </div>
          <div className="mono mt-0.5 text-[8.5px] text-[#646b7a]">algebraic + vector AST → 7-element dimension propagation → CFDFLD03 scalar buffer</div>
        </div>
        <div className="flex items-center gap-1">
          <Chip tone={result.ok ? "green" : "red"} dot={result.ok ? "#23D18B" : "#EF233C"}>{result.ok ? "type-checked" : "type error"}</Chip>
          <Chip tone="neutral">{result.trace?.length ?? 0} nodes</Chip>
        </div>
      </div>

      {/* editor */}
      <div className={cn("overflow-hidden rounded-[6px] border bg-[#0b0c11]", result.ok ? "border-[#2b2d36]" : "border-[#EF233C]/50")}>
        <div className="flex h-[26px] items-center gap-1.5 border-b border-[#24262d] bg-[#131419] px-2">
          <Code2 size={10} className="text-[#6d7484]" />
          <span className="mono text-[8.5px] text-[#9aa1af]">derived_field.expr</span>
          <div className="ml-auto flex items-center gap-1">
            <span className="mono text-[7.5px] text-[#565d6b]">Rhai · SI</span>
            <button onClick={() => { c.setCalcExpr(""); }} title="Clear" className="text-[#6d7484] hover:text-white"><RotateCcw size={10} /></button>
          </div>
        </div>
        <div className="flex">
          <div className="mono select-none border-r border-[#22232a] bg-[#0e0f13] py-1.5 pl-2 pr-1.5 text-right text-[9px] leading-[1.6] text-[#3f4450]">
            {["1", "2", "3", "4", "5", "6"].map((n) => <div key={n}>{n}</div>)}
          </div>
          <textarea
            value={c.calcExpr}
            onChange={(e) => { c.setCalcExpr(e.target.value); setDirty(true); }}
            spellCheck={false}
            className="caret-blue min-h-[104px] flex-1 resize-none border-0 bg-transparent px-2 py-1.5 text-[11px] leading-[1.6] text-[#d6e0f5] outline-none focus:ring-0"
          />
        </div>
        <div className="flex items-center gap-1 border-t border-[#22232a] bg-[#101116] px-2 py-1.5">
          <Btn size="sm" variant="primary" icon={<Zap size={11} />} onClick={() => { if (result.ok) { setDirty(true); c.notify(`Dimensional analysis passed · ${result.quantityName} [${result.dimsText}]`); } else c.notify(result.error?.message || "Compile failed"); }}>
            Compile
          </Btn>
          <Btn size="sm" icon={<Wand2 size={11} />} onClick={() => c.setCalcExpr(EXAMPLES[0].expr)}>Insert example</Btn>
          <span className="mono ml-auto flex items-center gap-1 text-[8px] text-[#565d6b]">
            {result.ok ? <><CircleCheck size={9} className="text-[#23D18B]" /> {result.dimsText}</> : <><CircleAlert size={9} className="text-[#EF233C]" /> rejected at parse time</>}
          </span>
        </div>
      </div>

      {/* error / type result */}
      {!result.ok ? (
        <div className="mt-2 rounded-[6px] border border-[#EF233C]/45 bg-[linear-gradient(180deg,rgba(239,35,60,0.09),rgba(239,35,60,0.03))] p-2.5">
          <div className="flex items-start gap-2">
            <span className="mt-[1px] flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-[#EF233C]/15 text-[#ff8798]"><TriangleAlert size={12} /></span>
            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-1.5">
                <span className="text-[10.5px] font-semibold text-[#ff9aab]">Dimensional analysis failed</span>
                <Chip tone="red">parse-time</Chip>
              </div>
              <div className="mono mt-1 text-[9px] leading-[1.7] text-[#e0b8c0]">{result.error?.message}</div>
              {result.error?.hint && (
                <div className="mono mt-1 flex items-start gap-1 rounded-[3px] border border-[#EF233C]/25 bg-[#180c10] px-1.5 py-1 text-[8px] leading-[1.6] text-[#c993a0]">
                  <ArrowRight size={9} className="mt-[2px] shrink-0" /> {result.error.hint}
                </div>
              )}
              <div className="mt-1.5 flex items-center gap-1">
                <Btn size="xs" variant="danger" icon={<Wand2 size={9} />} onClick={applyFix}>Apply suggested rho</Btn>
                <Btn size="xs" icon={<Plus size={9} />} onClick={() => c.setCalcExpr("p + 0.5 * rho * dot(U, U)")}>Insert density</Btn>
                <span className="mono text-[7.5px] text-[#8a6670]">no GPU buffer was created</span>
              </div>
            </div>
          </div>
        </div>
      ) : (
        <div className="mt-2 rounded-[6px] border border-[#23D18B]/30 bg-[linear-gradient(180deg,rgba(35,209,139,0.06),rgba(35,209,139,0.015))] p-2">
          <div className="flex items-center gap-1.5">
            <span className="flex h-5 w-5 items-center justify-center rounded-full bg-[#23D18B]/15 text-[#65e5b1]"><Check size={11} /></span>
            <div className="min-w-0 flex-1">
              <div className="text-[10px] font-semibold text-[#9cecc9]">All dimension checks passed</div>
              <div className="mono truncate text-[8px] text-[#68a98e]">
                {result.target || "derived_field"} : <span className="text-[#9cecc9]">{result.quantityName}</span> [{result.dimsText}] {unit !== "—" ? `· ${unit}` : ""}
              </div>
            </div>
            <Chip tone="green">SI ✓</Chip>
          </div>
        </div>
      )}

      {/* tabs */}
      <div className="mt-2 flex items-center gap-1">
        {([["result", "Result & upload"], ["ast", "AST · SI propagation"], ["buffer", "GPU buffer"]] as const).map(([v, l]) => (
          <button key={v} onClick={() => setTab(v)} className={cn("h-[22px] rounded-[4px] border px-2 text-[9px] transition-colors",
            tab === v ? "border-[#3b4150] bg-[#24262e] text-[#e6eaf2]" : "border-transparent text-[#727a8a] hover:text-[#b7bfcd]")}>{l}</button>
        ))}
        <span className="mono ml-auto text-[7.5px] text-[#565d6b]">samples: 8 412 096 cells</span>
      </div>

      {tab === "result" && (
        <div className="mt-1.5 grid grid-cols-[1.15fr_1fr] gap-2">
          <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
            <div className="mb-1 flex items-center justify-between">
              <span className="text-[9.5px] font-medium text-[#d6dbe6]">Field preview · {result.ok ? result.target || "derived" : "—"}</span>
              <span className="mono text-[8px] text-[#646b7a]">{unit}</span>
            </div>
            <div className={cn("grid overflow-hidden rounded-[3px] border", result.ok ? "border-[#30323c]" : "border-[#EF233C]/30 opacity-50")} style={{ gridTemplateColumns: "repeat(44, minmax(0, 1fr))", aspectRatio: "44 / 16" }}>
              {preview.map((color, i) => <span key={i} style={{ background: color }} />)}
            </div>
            <div className="mono mt-1 flex justify-between text-[7.5px] text-[#5d6371]">
              <span>{minR.toFixed(0)} {unit}</span><span>Oklab turbo</span><span>{maxR.toFixed(0)} {unit}</span>
            </div>
            <div className="mt-1.5">
              <div className="mb-1 text-[8.5px] uppercase tracking-wider text-[#5d6371]">Upload target</div>
              <div className="rounded-[3px] border border-[#252b38] bg-[#0c1018] p-1.5">
                <div className="mono flex items-center justify-between text-[8px] text-[#7f91b4]"><span>CFDFLD03.scalar</span><span className="text-[#65e5b1]">f32 · 4 B</span></div>
                <div className="mono mt-[2px] flex items-center justify-between text-[8px] text-[#5f6675]"><span>stride / cells</span><span>{((8_412_096 * 4) / 1e6).toFixed(1)} MB · 8.41 M</span></div>
                <div className="mono mt-[2px] flex items-center justify-between text-[8px] text-[#5f6675]"><span>endianness / checksum</span><span>LE · crc32 ok</span></div>
              </div>
            </div>
            <div className="mt-2">
              <Btn className="w-full" size="md" variant="primary" icon={<Upload size={12} />} onClick={upload} disabled={!result.ok}>
                Upload to CFDFLD03 & visualize
              </Btn>
              {c.uploaded && (
                <div className="mono mt-1 flex items-center justify-center gap-1 text-[7.5px] text-[#65e5b1]">
                  <Check size={8} /> {c.uploaded.name} resident on GPU · bound to field slot 4
                </div>
              )}
            </div>
          </div>

          <div className="space-y-1.5">
            <div className="grid grid-cols-2 gap-1">
              <StatBox label="evaluated mean" value={result.ok ? (Array.isArray(result.value) ? result.value[0].toFixed(1) : Number(result.value).toExponential(3)) : "—"} tone={result.ok ? "blue" : "red"} />
              <StatBox label="quantity" value={<span className="text-[10px]">{result.ok ? result.quantityName : "rejected"}</span>} />
            </div>
            <div className="rounded-[6px] border border-[#272830] bg-[#101116] p-2">
              <div className="mb-1 flex items-center justify-between">
                <span className="text-[9px] text-[#9aa1af]">Dimensional formula</span>
                <Chip tone={result.ok ? "blue" : "neutral"}>7-vector</Chip>
              </div>
              <div className="mono rounded-[3px] border border-[#2a3040] bg-[#0c1120] px-1.5 py-1.5 text-[10px] text-[#9fbcff]">
                {result.ok ? `[${result.dimsText}]` : "[unresolved]"}
              </div>
              <div className="mt-1 space-y-[3px]">
                {["+ −", "×", "÷", "grad", "dot"].map((op) => (
                  <div key={op} className="flex items-center gap-1.5 text-[8px]">
                    <span className="mono inline-flex h-[14px] min-w-[22px] items-center justify-center rounded-[2px] border border-[#30323c] bg-[#17181e] px-1 text-[#b7bfcd]">{op}</span>
                    <span className="text-[#6d7484]">{op === "+ −" ? "exponents must match" : op === "×" ? "add exponents" : op === "÷" ? "subtract exponents" : op === "grad" ? "subtract one [m]" : "add operands' exponents"}</span>
                  </div>
                ))}
              </div>
            </div>
            <div className="rounded-[6px] border border-[#27343a] bg-[#0b1417] p-2">
              <div className="mb-1 flex items-center gap-1 text-[8.5px] text-[#7fb6bf]"><FunctionSquare size={10} /> Compiler</div>
              <div className="mono space-y-[2px] text-[7.5px] text-[#69979f]">
                <div className="flex justify-between"><span>parser</span><span className="text-[#9cc4c9]">recursive descent</span></div>
                <div className="flex justify-between"><span>dim semantics</span><span className="text-[#9cc4c9]">7-vector algebra</span></div>
                <div className="flex justify-between"><span>codegen</span><span className="text-[#9cc4c9]">rhai → AST → WGSL</span></div>
                <div className="flex justify-between"><span>type errors</span><span className="text-[#9cc4c9]">before upload</span></div>
              </div>
            </div>
          </div>
        </div>
      )}

      {tab === "ast" && (
        <div className="mt-1.5 space-y-1.5">
          <TraceTable trace={result.trace || []} />
          <div className="rounded-[5px] border border-[#252b38] bg-[#0c1018] p-2">
            <div className="mono mb-1 text-[7.5px] uppercase tracking-wider text-[#565d6b]">registered symbols</div>
            <div className="flex flex-wrap gap-1">
              {Object.entries(SYMBOLS).slice(0, 14).map(([k, s]) => (
                <span key={k} className="mono rounded-[2px] border border-[#2c3342] bg-[#111722] px-1 py-[1px] text-[7.5px] text-[#8fa3c4]">{k} : [{formatDim(s.t.dim)}]</span>
              ))}
            </div>
          </div>
        </div>
      )}

      {tab === "buffer" && (
        <div className="mono mt-1.5 rounded-[5px] border border-[#25272f] bg-[#0c0d12] p-2 text-[8.5px] leading-[1.75] text-[#8b93a4]">
          <div className="text-[#6d7484]">// CFDFLD03 scalar upload descriptor</div>
          <div><span className="text-[#8fb0ff]">magic</span>      = 0x4346443033 <span className="text-[#4f5564]">// "CFDFLD03"</span></div>
          <div><span className="text-[#8fb0ff]">name</span>       = "{result.ok ? result.target || "derived_field" : "invalid"}"</div>
          <div><span className="text-[#8fb0ff]">dim</span>        = [{result.ok ? result.dimsText : "—"}]</div>
          <div><span className="text-[#8fb0ff]">quantity</span>   = {result.ok ? result.quantityName : "—"}</div>
          <div><span className="text-[#8fb0ff]">components</span> = 1</div>
          <div><span className="text-[#8fb0ff]">dtype</span>      = f32 (bytemuck::Pod)</div>
          <div><span className="text-[#8fb0ff]">cells</span>      = 8_412_096</div>
          <div><span className="text-[#8fb0ff]">bytes</span>      = 33_648_384</div>
          <div><span className="text-[#8fb0ff]">gpu_binding</span> = @group(1) @binding(4) var&lt;storage, read&gt; derived: array&lt;f32&gt;;</div>
          <div><span className="text-[#8fb0ff]">status</span>     = <span className="text-[#65e5b1]">{c.uploaded ? `uploaded (${c.uploaded.name})` : "not uploaded"}</span></div>
        </div>
      )}

      <div className="mono mt-1.5 flex items-center justify-between rounded-[4px] border border-[#22242c] bg-[#121318] px-2 py-1 text-[7.5px] text-[#565d6b]">
        <span className="flex items-center gap-1"><Cpu size={9} /> every AST node validated before any buffer allocation</span>
        <span className={dirty && result.ok ? "text-[#ffcb6b]" : "text-[#65e5b1]"}>{dirty && result.ok ? "uncommitted changes" : "compiled"}</span>
      </div>
    </div>
  );
}
