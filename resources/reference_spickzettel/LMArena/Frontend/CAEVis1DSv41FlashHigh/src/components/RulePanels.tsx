import React, { useMemo, useState } from "react";
import {
  AlertTriangle, ArrowRight, Box, BrainCircuit, Check, ChevronDown, ChevronRight, CircleDot,
  Copy, Cylinder, FileBadge, GitMerge, Layers3, Link2, ListFilter, Plus, ScanLine,
  Sparkles, Trash2, TriangleAlert, Wand2, Wind, CircleCheck, ShieldCheck,
} from "lucide-react";
import { PATCHES, Patch } from "../lib/sim";
import { useCockpit } from "../store";
import { Btn, Chip, IconBtn, SearchInput, Section, Toggle, cn } from "./ui";

const KIND_TONE: Record<string, "blue" | "cyan" | "violet" | "amber" | "green" | "neutral"> = {
  inlet: "cyan", outlet: "violet", wall: "blue", interface: "green", periodic: "amber",
};

export function Expr({ text, className }: { text: string; className?: string }) {
  const parts = text.split(/(normal\.[xyz]|bbox\.[a-z_]+|cyl_radius_yz|\b(?:and|or|in|abs)\b|[-+]?\d*\.?\d+(?:e[-+]?\d+)?|[<>]=?|[{}[\],])/gi);
  return (
    <code className={cn("mono break-words text-[10px] leading-[1.7]", className)}>
      {parts.map((p, i) => {
        if (/^normal\.|^bbox\.|^cyl_radius_yz$/i.test(p)) return <span key={i} className="text-[#8fb0ff]">{p}</span>;
        if (/^(and|or|in|abs)$/i.test(p)) return <span key={i} className="text-[#c3b2ff]">{p}</span>;
        if (/^[-+]?\d/.test(p)) return <span key={i} style={{ color: "#ffcb6b" }}>{p}</span>;
        if (/^[<>]=?$/.test(p)) return <span key={i} className="text-[#ff9eaf]">{p}</span>;
        return <span key={i} className="text-[#b9c0ce]">{p}</span>;
      })}
    </code>
  );
}

function PatchGlyph({ id }: { id: string }) {
  const map: Record<string, React.ReactNode> = {
    LE: <Wind size={11} />, SS: <Layers3 size={11} />, PS: <Layers3 size={11} />, TE: <ScanLine size={11} />,
    TIP: <Box size={11} />, HUB: <Cylinder size={11} />, SHROUD: <Cylinder size={11} />,
    INLET: <Wind size={11} />, OUTLET: <Wind size={11} />, MRF: <CircleDot size={11} />,
  };
  return <span className="text-[#6d7484]">{map[id] || <Box size={11} />}</span>;
}

export function LeftRules() {
  const c = useCockpit();
  const [q, setQ] = useState("");
  const [filter, setFilter] = useState<"all" | "solid" | "domain">("all");
  const list = useMemo(() => PATCHES.filter((p) => {
    if (filter === "solid" && !p.pickable) return false;
    if (filter === "domain" && p.pickable) return false;
    return (p.name + p.tag + p.short).toLowerCase().includes(q.toLowerCase());
  }), [q, filter]);

  const row = (p: Patch) => {
    const active = c.selectedPatch.id === p.id;
    const conflict = c.conflicts.has(p.id);
    const covered = c.rules.some((r) => r.active && r.patches.includes(p.id));
    return (
      <button
        key={p.id}
        onClick={() => c.selectPatch(p.id)}
        onMouseEnter={() => c.setHoveredPatch(p.id)}
        onMouseLeave={() => c.setHoveredPatch(null)}
        className={cn(
          "group relative flex w-full items-center gap-2 border-l-2 px-2.5 py-[7px] text-left transition-colors",
          active ? "border-l-[#004AFD] bg-[#004AFD]/[0.09]" : "border-l-transparent hover:bg-white/[0.03]"
        )}
      >
        <span className={cn("transition-colors", active ? "text-[#8fb0ff]" : "text-[#6d7484]")}><PatchGlyph id={p.id} /></span>
        <span className="min-w-0 flex-1">
          <span className={cn("flex items-center gap-1 text-[10.5px] leading-tight", active ? "text-[#e7ecf8]" : "text-[#c2c7d2]")}>
            <span className="mono">{p.short}</span>
            {conflict && <span className="h-[5px] w-[5px] rounded-full bg-[#EF233C]" title="Overlapping boundary rule" />}
          </span>
          <span className="mt-[2px] flex items-center gap-1.5 text-[8.5px] text-[#686f7e]">
            <span>{p.faces.toLocaleString()} faces</span>
            <span className="h-[2px] w-[2px] rounded-full bg-[#4d5260]" />
            <span className="mono">{p.area.toFixed(4)} m²</span>
          </span>
        </span>
        {covered ? (
          <span className="shrink-0 rounded-[2px] border border-[#23D18B]/25 px-1 py-[1px] text-[7px] uppercase tracking-wide text-[#5dbf99]">BC</span>
        ) : (
          <span className="shrink-0 rounded-[2px] border border-[#4a4c57] px-1 py-[1px] text-[7px] uppercase tracking-wide text-[#777e8d]">new</span>
        )}
      </button>
    );
  };

  return (
    <div className="flex h-full min-h-0 flex-col bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <ListFilter size={12} className="text-[#6d7484]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Surface patches</span>
        <span className="mono ml-auto text-[8.5px] text-[#5d6371]">{list.length}/10</span>
      </div>
      <div className="shrink-0 space-y-1.5 border-b border-[#212228] p-2">
        <SearchInput value={q} onChange={setQ} placeholder="filter STEP tags, surfaces…" />
        <div className="flex gap-1">
          {(["all", "solid", "domain"] as const).map((f) => (
            <button
              key={f}
              onClick={() => setFilter(f)}
              className={cn("h-[19px] flex-1 rounded-[3px] border text-[9px] capitalize transition-colors",
                filter === f ? "border-[#3c3e49] bg-[#24252d] text-[#d5dae5]" : "border-transparent text-[#6f7685] hover:text-[#aeb4c2]")}
            >
              {f}
            </button>
          ))}
        </div>
      </div>
      <div className="scroll min-h-0 flex-1 overflow-y-auto">
        <div className="px-2.5 pb-1 pt-2 text-[8px] font-semibold uppercase tracking-[0.13em] text-[#575d6b]">Solid · blade row 1</div>
        {list.filter((p) => p.pickable).map(row)}
        <div className="mt-1 border-t border-[#212228] px-2.5 pb-1 pt-2 text-[8px] font-semibold uppercase tracking-[0.13em] text-[#575d6b]">Domain boundaries & zones</div>
        {list.filter((p) => !p.pickable).map(row)}
      </div>
      <div className="shrink-0 border-t border-[#24252c] p-2">
        <div className="rounded-[5px] border border-[#292a32] bg-[#0f1014] p-2">
          <div className="mb-1 flex items-center justify-between">
            <span className="flex items-center gap-1 text-[8.5px] uppercase tracking-[0.1em] text-[#747b8a]"><FileBadge size={10} /> STEP AP242</span>
            <Chip tone="green">watertight</Chip>
          </div>
          <div className="mono space-y-[2px] text-[8.5px] text-[#646b7a]">
            <div className="flex justify-between"><span>topology</span><span className="text-[#a7aebc]">1 solid · 7 faces</span></div>
            <div className="flex justify-between"><span>tessellation</span><span className="text-[#a7aebc]">0.05 mm / 12°</span></div>
            <div className="flex justify-between"><span>pick buffer</span><span className="text-[#8fb0ff]">R32Uint</span></div>
          </div>
        </div>
      </div>
    </div>
  );
}

function ConfidenceRing({ value }: { value: number }) {
  const r = 15, c = 2 * Math.PI * r;
  return (
    <div className="relative h-[38px] w-[38px] shrink-0">
      <svg viewBox="0 0 38 38" className="h-full w-full -rotate-90">
        <circle cx="19" cy="19" r={r} fill="none" stroke="#2a2b33" strokeWidth="2.5" />
        <circle cx="19" cy="19" r={r} fill="none" stroke="#4D7CFF" strokeWidth="2.5" strokeLinecap="round"
          strokeDasharray={`${(value / 100) * c} ${c}`} />
      </svg>
      <div className="absolute inset-0 flex flex-col items-center justify-center">
        <span className="mono text-[8.5px] font-semibold text-[#dbe4ff]">{value.toFixed(1)}</span>
        <span className="text-[5.5px] uppercase tracking-wide text-[#6f839f]">conf</span>
      </div>
    </div>
  );
}

export function RightRules() {
  const c = useCockpit();
  const p = c.selectedPatch;
  const conflict = c.conflicts.has(p.id);
  const related = c.rules.filter((r) => r.active && r.patches.includes(p.id));
  const [copied, setCopied] = useState(false);
  const [editExpr, setEditExpr] = useState(false);
  const [draft, setDraft] = useState(c.synth.expression);

  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <BrainCircuit size={12} className="text-[#7ea8ff]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Spatial rule synthesizer</span>
        <span className="mono ml-auto flex items-center gap-1 text-[8px] text-[#5d6371]"><span className="h-[5px] w-[5px] rounded-full bg-[#004AFD]" />r32u</span>
      </div>

      {/* picked payload */}
      <div className="shrink-0 border-b border-[#24252c] p-2.5">
        <div className="mb-2 flex items-start justify-between gap-2">
          <div className="min-w-0">
            <div className="flex items-center gap-1.5">
              <span className="mono text-[12px] text-white">{p.short}</span>
              <Chip tone={KIND_TONE[p.kind]}>{p.kind}</Chip>
            </div>
            <div className="mt-0.5 truncate text-[9.5px] text-[#7e8594]">{p.name}</div>
          </div>
          <ConfidenceRing value={c.synth.confidence} />
        </div>
        <div className="grid grid-cols-2 gap-[3px]">
          {[
            { k: "face_group", v: String(p.group) },
            { k: "normal cone", v: `±${c.synth.normalCone.toFixed(1)}°` },
            { k: "centroid", v: p.centroid.map((v) => v.toFixed(2)).join(", ") },
            { k: "bbox Δ", v: p.bboxMax.map((v, i) => (v - p.bboxMin[i]).toFixed(2)).join(" × ") },
            { k: "cyl radius", v: p.cyl ? `${p.cyl.range[0]}–${p.cyl.range[1]}` : "n/a" },
            { k: "STEP tag", v: p.tag.replace("SURF_", "") },
          ].map((x) => (
            <div key={x.k} className="rounded-[3px] border border-[#26272f] bg-[#0f1014] px-1.5 py-[4px]">
              <div className="text-[7.5px] uppercase tracking-[0.07em] text-[#5d6371]">{x.k}</div>
              <div className="mono truncate text-[8.5px] text-[#b7bfcd]">{x.v}</div>
            </div>
          ))}
        </div>
      </div>

      {/* synthesized expression */}
      <div className="shrink-0 border-b border-[#24252c] p-2.5">
        <div className="mb-1.5 flex items-center justify-between">
          <span className="flex items-center gap-1 text-[8.5px] uppercase tracking-[0.09em] text-[#7e8594]">
            <Sparkles size={10} className="text-[#8fb0ff]" /> Proposed topology-independent rule
          </span>
          <div className="flex items-center gap-0.5">
            <IconBtn title="Edit expression" onClick={() => setEditExpr((v) => !v)} active={editExpr}><Wand2 size={11} /></IconBtn>
            <IconBtn title="Copy Rhai rule" onClick={() => { setCopied(true); navigator.clipboard?.writeText(`bc.rule { ${c.synth.expression} }`); c.notify("Spatial rule copied as Rhai"); setTimeout(() => setCopied(false), 1400); }}>
              {copied ? <Check size={11} className="text-[#23D18B]" /> : <Copy size={11} />}
            </IconBtn>
          </div>
        </div>
        {editExpr ? (
          <div className="rounded-[4px] border border-[#3f5fa8]/60 bg-[#0c1120] p-1.5">
            <div className="mono mb-1 text-[8px] text-[#6b7c9c]">match_faces {p.short.toLowerCase()}</div>
            <textarea
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
              className="h-[58px] w-full resize-none rounded-[3px] border border-[#233152] bg-[#0a0d16] p-1.5 text-[10px] leading-relaxed text-[#c9d6f5] caret-blue focus:border-[#4d7cff]"
            />
          </div>
        ) : (
          <div className="rounded-[4px] border border-[#2f3a58] bg-[linear-gradient(180deg,#101624,#0d1018)] p-2">
            <div className="mono mb-1 flex items-center justify-between text-[8px] text-[#5f6c88]">
              <span>spatial_match({p.short})</span>
              <span className="text-[#48628f]">rhai</span>
            </div>
            <Expr text={c.synth.expression} className="text-[10px] text-[#dbe4f8]" />
          </div>
        )}
        <div className="mt-1.5 flex flex-wrap gap-1">
          {c.synth.clauses.map((cl, i) => (
            <span key={i} className="mono inline-flex items-center gap-1 rounded-[3px] border border-[#2c2e37] bg-[#181a20] px-1.5 py-[2px] text-[8px] text-[#8790a2]">
              <span className="text-[#5f6a7f]">{cl.label}</span>
              <span className="text-[#aeb9cc]">{cl.value}</span>
            </span>
          ))}
        </div>
        <p className="mt-1.5 text-[9px] leading-[1.5] text-[#7a8190]">{c.synth.human}</p>
        <div className="mt-2 grid grid-cols-2 gap-1">
          <Btn variant="secondary" size="sm" icon={<GitMerge size={11} />} onClick={c.mergeDraft} disabled={p.kind !== "wall"} title={p.kind !== "wall" ? "Only wall patches can merge into the adiabatic wall BC" : "Merge into Blade · adiabatic no-slip"}>Merge to wall</Btn>
          <Btn variant="primary" size="sm" icon={<Plus size={11} />} onClick={c.separateDraft}>New rule</Btn>
        </div>
      </div>

      {/* warning */}
      {(conflict || related.length > 1) && (
        <div className="mx-2.5 mt-2.5 rounded-[5px] border border-[#EF233C]/45 bg-[#EF233C]/[0.08] p-2">
          <div className="flex items-start gap-1.5">
            <TriangleAlert size={12} className="mt-[1px] shrink-0 text-[#ff6f83]" />
            <div className="min-w-0">
              <div className="text-[10px] font-semibold text-[#ff97a6]">Overlapping boundary condition</div>
              <div className="mt-0.5 text-[9px] leading-[1.5] text-[#c08a93]">
                {p.short} matches {related.length} active rules ({related.map((r) => r.id).join(", ")}). Faces are highlighted in <span className="text-[#ff8798]">#EF233C</span>. A face may only belong to one BC.
              </div>
              <div className="mt-1.5 flex gap-1">
                <Btn size="xs" variant="danger" onClick={c.mergeDraft}>Merge rules</Btn>
                <Btn size="xs" variant="ghost" onClick={() => related.slice(1).forEach((r) => c.deleteRule(r.id))}>Resolve</Btn>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* rules list */}
      <Section title="Boundary rules" icon={<Link2 size={11} />} subtitle={`${c.rules.length} active`} right={<Chip tone="neutral">{c.rules.reduce((s, r) => s + r.faces, 0).toLocaleString()} faces</Chip>} dense>
        <div className="space-y-1">
          {c.rules.map((r) => {
            const sel = c.selectedRule === r.id;
            const cl = c.conflictRules.has(r.id);
            return (
              <div key={r.id} className={cn("overflow-hidden rounded-[4px] border transition-colors", cl ? "border-[#EF233C]/45 bg-[#EF233C]/[0.04]" : sel ? "border-[#3c4c74] bg-[#171b24]" : "border-[#272830] bg-[#14151a]")}>
                <div className="flex items-center gap-1.5 px-1.5 py-[5px]">
                  <button onClick={() => c.setSelectedRule(sel ? null : r.id)} className="flex min-w-0 flex-1 items-center gap-1.5 text-left">
                    {sel ? <ChevronDown size={11} className="shrink-0 text-[#7e8594]" /> : <ChevronRight size={11} className="shrink-0 text-[#586070]" />}
                    <span className="h-[6px] w-[6px] shrink-0 rounded-full" style={{ background: cl ? "#EF233C" : r.color }} />
                    <span className="min-w-0 flex-1">
                      <span className="block truncate text-[10px] text-[#d3d8e3]">{r.name}</span>
                      <span className="mono flex items-center gap-1 text-[7.5px] text-[#626978]">
                        {r.id} · {r.kind} · {r.patches.join(" + ")}
                        {r.synthesized && <span className="rounded-[2px] border border-[#4d7cff]/40 px-0.5 text-[6.5px] text-[#8fb0ff]">SYNTH</span>}
                      </span>
                    </span>
                  </button>
                  <Toggle checked={r.active} onChange={() => c.toggleRule(r.id)} size="xs" />
                </div>
                {sel && (
                  <div className="border-t border-[#282a33] px-2 pb-2 pt-1.5">
                    <div className="mb-1 text-[7.5px] uppercase tracking-wider text-[#5d6474]">match expression</div>
                    <div className="rounded-[3px] bg-[#0c0d12] px-1.5 py-1"><Expr text={r.expression} className="text-[9px]" /></div>
                    <div className="mt-1.5 text-[7.5px] uppercase tracking-wider text-[#5d6474]">physical target</div>
                    <div className="text-[9px] leading-snug text-[#a6adbb]">{r.target}</div>
                    <div className="mt-1.5 flex items-center justify-between">
                      <span className="mono text-[8px] text-[#6b7281]">{r.faces.toLocaleString()} matched faces</span>
                      <button onClick={() => c.deleteRule(r.id)} className="flex items-center gap-1 rounded-[3px] px-1 py-[2px] text-[8px] text-[#8c6670] hover:bg-[#EF233C]/10 hover:text-[#ff8798]">
                        <Trash2 size={9} /> delete
                      </button>
                    </div>
                  </div>
                )}
              </div>
            );
          })}
        </div>
      </Section>

      <Section title="Solver readiness" icon={<ShieldCheck size={11} />} defaultOpen dense>
        <div className="space-y-[5px]">
          {[
            { n: "Inlet / outlet pair", ok: true, v: "closed" },
            { n: "No-slip wall coverage", ok: true, v: "100 %" },
            { n: "MRF interface flux", ok: true, v: "conservative" },
            { n: "BC overlap check", ok: c.conflicts.size === 0, v: c.conflicts.size ? `${c.conflicts.size} conflict` : "clean" },
            { n: "Topology independence", ok: true, v: "all spatial" },
          ].map((x) => (
            <div key={x.n} className="flex items-center gap-1.5 text-[9px]">
              {x.ok ? <CircleCheck size={10} className="text-[#23D18B]" /> : <AlertTriangle size={10} className="text-[#EF233C]" />}
              <span className="flex-1 text-[#8a91a0]">{x.n}</span>
              <span className={cn("mono text-[8px]", x.ok ? "text-[#7fbd9f]" : "text-[#ff8798]")}>{x.v}</span>
            </div>
          ))}
          <div className="pt-1">
            <div className="mb-[3px] flex justify-between text-[8px] text-[#666d7c]"><span>face coverage</span><span className="mono text-[#a7aebc]">100.0 %</span></div>
            <div className="h-[4px] overflow-hidden rounded-full bg-[#24252c]">
              <div className="h-full w-full bg-[linear-gradient(90deg,#004AFD,#41E0FF)]" />
            </div>
          </div>
        </div>
      </Section>

      <div className="mt-auto shrink-0 border-t border-[#24252c] p-2.5">
        <Btn variant="primary" className="w-full" icon={<Check size={12} />} onClick={() => c.notify(c.conflicts.size ? "Resolve boundary overlaps before writing the case" : "Boundary rules compiled → solver case written (12.4 MB)")}>
          Compile spatial rules
        </Btn>
        <div className="mono mt-1 flex items-center justify-center gap-1 text-[8px] text-[#5d6371]">
          <ArrowRight size={8} /> generated rhai · deterministic · no face IDs
        </div>
      </div>
    </div>
  );
}
