// nTop-style declarative block stack — rendered from the schema.get registry,
// with visible_if conditional evaluation and unit-aware expression inputs.
import { useEffect, useState } from "react";
import { BLOCKS, evalVisible, MATERIALS } from "../schema/registry";
import type { ActionDef, BlockDef, ParamDef } from "../schema/registry";
import { parseExpr, fmtDim, fmtSI } from "../units/parser";
import { useStore } from "../state/store";
import { rpc, reimportGeometry } from "../ipc/backend";
import { engineRef } from "./Viewport";

function ExprInput({ def }: { def: ParamDef }) {
  const value = useStore((s) => s.params[def.key]);
  const setParam = useStore((s) => s.setParam);
  const [txt, setTxt] = useState(String(value ?? ""));
  const [focused, setFocused] = useState(false);
  useEffect(() => {
    if (!focused) setTxt(String(value ?? ""));
  }, [value, focused]);
  const parsed = parseExpr(txt, def.dim);
  return (
    <div className="min-w-0">
      <div className="flex items-center justify-between gap-2">
        <span className="text-[11px] text-[#9DA0AA] truncate">{def.label}</span>
        {def.dim && (
          <span className="mono text-[8.5px] text-[#63666f] whitespace-nowrap" title="expected SI base dimension">
            {fmtDim(def.dim)}
          </span>
        )}
      </div>
      <div
        className={`mt-0.5 flex items-center gap-1.5 bg-[#111217] border rounded-sm px-2 py-1 ${
          parsed.ok ? "border-[#32333C] focus-within:border-[#004AFD]" : "border-[#EF233C]"
        }`}
      >
        <input
          value={txt}
          spellCheck={false}
          onChange={(e) => setTxt(e.target.value)}
          onFocus={() => setFocused(true)}
          onBlur={() => {
            setFocused(false);
            setParam(def.key, txt);
          }}
          onKeyDown={(e) => {
            if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          }}
          className="flex-1 bg-transparent outline-none mono text-[11.5px] text-[#E8E9ED] min-w-0"
        />
        {parsed.ok && (
          <span className="mono text-[9px] text-[#B9F500] whitespace-nowrap" title={`unit ${parsed.unit} → SI base`}>
            = {fmtSI(parsed.si)}
          </span>
        )}
      </div>
      {def.note && <div className="mt-0.5 text-[9px] text-[#63666f]">{def.note}</div>}
      {!parsed.ok && <div className="mt-0.5 text-[9px] text-[#EF233C]">{parsed.error}</div>}
    </div>
  );
}

function ParamWidget({ def }: { def: ParamDef }) {
  const value = useStore((s) => s.params[def.key]);
  const setParam = useStore((s) => s.setParam);
  if (def.visible_if && !evalVisible(def.visible_if, useStore.getState().params)) return null;

  if (def.type === "expr") return <ExprInput def={def} />;
  if (def.type === "enum")
    return (
      <div>
        <span className="text-[11px] text-[#9DA0AA]">{def.label}</span>
        <select
          value={String(value)}
          onChange={(e) => setParam(def.key, e.target.value)}
          className="mt-0.5 w-full bg-[#111217] border border-[#32333C] rounded-sm px-2 py-1 text-[11px] text-[#E8E9ED] outline-none focus:border-[#004AFD]"
        >
          {def.options?.map((o) => (
            <option key={o.v} value={o.v}>
              {o.label}
            </option>
          ))}
        </select>
      </div>
    );
  if (def.type === "toggle")
    return (
      <button
        disabled={def.locked}
        onClick={() => setParam(def.key, !value)}
        className={`flex items-center justify-between w-full px-2 py-1.5 rounded-sm border text-[11px] ${
          value ? "border-[#B9F500]/40 bg-[#B9F500]/5 text-[#D8FF4D]" : "border-[#32333C] text-[#9DA0AA]"
        } ${def.locked ? "opacity-60 cursor-not-allowed" : "hover:border-[#4A4B55]"}`}
      >
        {def.label}
        <span
          className={`w-7 h-3.5 rounded-full relative transition-colors ${value ? "bg-[#B9F500]/70" : "bg-[#32333C]"}`}
        >
          <span
            className={`absolute top-[1.5px] w-2.5 h-2.5 rounded-full bg-[#111217] transition-all ${
              value ? "left-[15px]" : "left-[2px]"
            }`}
          />
        </span>
      </button>
    );
  if (def.type === "slider")
    return (
      <div>
        <div className="flex justify-between text-[11px]">
          <span className="text-[#9DA0AA]">{def.label}</span>
          <span className="mono text-[10px] text-[#B9F500]">{Number(value).toFixed(3)}</span>
        </div>
        <input
          type="range"
          min={def.min ?? 0}
          max={def.max ?? 1}
          step={def.step ?? 0.01}
          value={Number(value)}
          onChange={(e) => setParam(def.key, Number(e.target.value))}
          className="w-full"
        />
      </div>
    );
  // int
  return (
    <div>
      <span className="text-[11px] text-[#9DA0AA]">{def.label}</span>
      <input
        type="number"
        min={def.min}
        max={def.max}
        step={def.step ?? 1}
        value={Number(value)}
        onChange={(e) => setParam(def.key, Number(e.target.value))}
        className="mt-0.5 w-full bg-[#111217] border border-[#32333C] rounded-sm px-2 py-1 mono text-[11px] text-[#E8E9ED] outline-none focus:border-[#004AFD]"
      />
    </div>
  );
}

const TONE: Record<ActionDef["tone"], string> = {
  primary: "bg-[#004AFD] text-white hover:bg-[#2A63FF]",
  normal: "bg-[#1E1E24] text-[#C6C8CF] border border-[#32333C] hover:border-[#4A4B55]",
  warn: "bg-[#1E1E24] text-[#FEC40B] border border-[#FEC40B]/40 hover:bg-[#FEC40B]/10",
  danger: "bg-[#1E1E24] text-[#EF233C] border border-[#EF233C]/40 hover:bg-[#EF233C]/10",
};

function Block({ b }: { b: BlockDef }) {
  const [busy, setBusy] = useState<string | null>(null);
  const ws = useStore((s) => s.ws);
  const params = useStore((s) => s.params);
  const run = async (a: ActionDef) => {
    setBusy(a.id);
    if (a.id === "import") {
      try {
        await reimportGeometry();
      } catch { /* logged */ }
    } else if (a.id === "load") {
      const s = useStore.getState();
      rpc.call("results.load", { t: s.tB || s.activeT }).catch(() => {});
    } else if (a.id === "png") {
      engineRef.current?.exportPNG();
    } else {
      const s = useStore.getState();
      try {
        const p = a.method === "geometry.patches.update" ? { patch: s.selPatch ?? -1 } : {};
        await rpc.call(a.method, p);
      } catch { /* logged */ }
    }
    setTimeout(() => setBusy(null), 250);
  };
  const rows = b.params.filter((pd) => !pd.visible_if || evalVisible(pd.visible_if, params));
  return (
    <div
      className="block-stripe bg-[#18181C] border border-[#26272E] rounded-md px-3 py-2.5"
      style={{ "--stripe": b.stripe } as React.CSSProperties}
    >
      <div className="flex items-center justify-between gap-2">
        <div className="text-[12px] font-semibold text-[#E8E9ED] truncate">{b.title}</div>
        <span
          className="mono text-[8px] tracking-wider px-1.5 py-0.5 rounded-sm whitespace-nowrap"
          style={{ color: b.stripe, background: `${b.stripe}14`, border: `1px solid ${b.stripe}33` }}
        >
          {b.cat}
        </span>
      </div>
      {b.desc && <div className="mt-1 text-[9.5px] leading-snug text-[#63666f]">{b.desc}</div>}
      {rows.length > 0 && (
        <div className="mt-2.5 grid grid-cols-2 gap-x-3 gap-y-2.5">
          {rows.map((pd) => (
            <div key={pd.key} className={pd.type === "toggle" ? "col-span-2" : pd.type === "expr" && pd.key === "p_ref" ? "" : ""}>
              <ParamWidget def={pd} />
            </div>
          ))}
        </div>
      )}
      {b.actions && (
        <div className="mt-2.5 flex flex-wrap gap-1.5">
          {b.actions.map((a) => (
            <ActionBtn2 key={a.id} a={a} busy={busy} run={run} ws={ws} />
          ))}
        </div>
      )}
    </div>
  );
}

function ActionBtn2({ a, busy, run, ws }: { a: ActionDef; busy: string | null; run: (a: ActionDef) => void; ws: number }) {
  // solver actions gated by solver state
  const solver = useStore((s) => s.solver);
  let disabled = busy !== null;
  if (a.method === "solver.start") disabled = solver === "running" || !useStore.getState().meshStats;
  if ((a.method === "solver.stop_graceful" || a.method === "solver.abort" || a.method === "job.cancel"))
    disabled = solver !== "running";
  if (a.method === "solver.write_now") disabled = solver !== "running";
  if (a.method === "results.load" && ws === 5 && useStore.getState().timesteps.length === 0) disabled = true;
  return (
    <button
      onClick={() => run(a)}
      disabled={disabled}
      className={`mono text-[10px] px-2 py-1.5 rounded-sm transition-all ${TONE[a.tone]} ${
        disabled ? "opacity-40 cursor-not-allowed" : ""
      }`}
    >
      {busy === a.id ? "▸ …" : a.label}
    </button>
  );
}

export function BlockStack() {
  const ws = useStore((s) => s.ws);
  const blocks = BLOCKS[ws] ?? [];
  return (
    <div className="w-[300px] shrink-0 border-r border-[#26272E] bg-[#111217] overflow-y-auto p-2.5 space-y-2.5">
      {blocks.map((b) => (
        <Block key={b.id} b={b} />
      ))}
      <div className="px-1 pb-2 text-[9px] text-[#4A4B55] mono leading-relaxed">
        {ws === 3 &&
          `materials.list → ${MATERIALS.map((m) => m.name).join(" · ")}
        `}
        <div>
          unit parser: strict SI 7-dim [kg m s K mol A cd] · p vs p_rgh · Δy₁ vs y⁺
        </div>
      </div>
    </div>
  );
}
