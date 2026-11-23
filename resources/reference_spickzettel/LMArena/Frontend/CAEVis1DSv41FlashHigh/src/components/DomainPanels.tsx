import React from "react";
import {
  Box, Droplets, Gauge, GripVertical, Mic, Move3d, Plus, RotateCw, Ruler,
  Sparkles, Trash2, TrendingUp, TriangleAlert, Wind, Waves, Shield, CircleDot,
} from "lucide-react";
import { useCockpit, ActorKind } from "../store";
import { Chip, IconBtn, NumField, Section, Segmented, Slider, StatBox, Toggle, cn } from "./ui";

export function LeftDomain() {
  const c = useCockpit();
  const { domain, setDomain } = c;
  const tipMachOk = c.tipMach < 0.8;
  const volume = (domain.upstream + domain.downstream) * c.targetL * (domain.radial * c.targetL) ** 2 * 0.74;
  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <Box size={12} className="text-[#6d7484]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Domain & rotating zone</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">CFDGEO01</span>
      </div>

      <Section title="Farfield sizing" icon={<Ruler size={11} />} subtitle="chord-normalized" dense>
        <div className="mb-2 grid grid-cols-3 gap-1">
          {[
            { k: "upstream", v: `${domain.upstream.toFixed(0)}L` },
            { k: "downstream", v: `${domain.downstream.toFixed(0)}L` },
            { k: "radial", v: `${domain.radial.toFixed(0)}L` },
          ].map((x) => (
            <div key={x.k} className="rounded-[4px] border border-[#282a32] bg-[#0f1014] px-1.5 py-1">
              <div className="text-[7px] uppercase tracking-[0.08em] text-[#5d6371]">{x.k}</div>
              <div className="mono text-[11px] text-[#d3d9e6]">{x.v}</div>
            </div>
          ))}
        </div>
        <Slider label="Upstream length" value={domain.upstream} onChange={(v) => setDomain((d) => ({ ...d, upstream: v }))} min={2} max={10} step={0.5} digits={1} unit="L" accent />
        <Slider label="Downstream wake" value={domain.downstream} onChange={(v) => setDomain((d) => ({ ...d, downstream: v }))} min={6} max={25} step={0.5} digits={1} unit="L" accent />
        <Slider label="Radial farfield" value={domain.radial} onChange={(v) => setDomain((d) => ({ ...d, radial: v }))} min={2} max={10} step={0.5} digits={1} unit="L" accent />
        <div className="inset mt-1.5 flex items-center justify-between px-2 py-1.5">
          <div>
            <div className="text-[8px] uppercase tracking-wider text-[#5d6371]">block extent</div>
            <div className="mono text-[9px] text-[#aeb4c2]">x [{(-1.05 - domain.upstream * 0.2).toFixed(2)}, {(0.92 + domain.downstream * 0.065).toFixed(2)}] m</div>
          </div>
          <div className="text-right">
            <div className="text-[8px] uppercase tracking-wider text-[#5d6371]">volume</div>
            <div className="mono text-[9px] text-[#aeb4c2]">{volume.toFixed(1)} m³</div>
          </div>
        </div>
        <div className="mt-1.5 flex items-center justify-between">
          <Toggle checked={domain.showDomain} onChange={(v) => setDomain((d) => ({ ...d, showDomain: v }))} label="Show farfield cage" />
          <Chip tone={tipMachOk ? "green" : "red"}>{tipMachOk ? "farfield adequate" : "extend domain"}</Chip>
        </div>
      </Section>

      <Section title="MRF / sliding mesh rotor" icon={<RotateCw size={11} />} right={<Chip tone="cyan">frozen rotor</Chip>} dense>
        <div className="mb-1.5 flex items-center justify-between">
          <span className="text-[10px] text-[#8a91a0]">Zone transform</span>
          <Toggle checked={domain.showMrf} onChange={(v) => setDomain((d) => ({ ...d, showMrf: v }))} />
        </div>
        <Slider label="Rotational speed" value={domain.rpm} onChange={(v) => setDomain((d) => ({ ...d, rpm: v }))} min={0} max={24000} step={100} digits={0} unit=" rpm" accent />
        <Slider label="Zone radius" value={domain.mrfRadius} onChange={(v) => setDomain((d) => ({ ...d, mrfRadius: v }))} min={0.8} max={1.7} step={0.01} digits={2} unit=" m" />
        <Slider label="Zone height" value={domain.mrfHeight} onChange={(v) => setDomain((d) => ({ ...d, mrfHeight: v }))} min={1.2} max={2.1} step={0.01} digits={2} unit=" m" />
        <div className="mb-1.5 mt-1 flex items-center justify-between">
          <span className="text-[10px] text-[#8a91a0]">Rotation axis</span>
          <Segmented options={[{ value: "x", label: "X" }, { value: "y", label: "Y" }, { value: "z", label: "Z" }]} value={domain.axis} onChange={(v) => setDomain((d) => ({ ...d, axis: v }))} />
        </div>
        <div className="flex gap-1.5">
          <StatBox label="tip speed" value={c.tipSpeed.toFixed(1)} unit="m/s" tone="blue" />
          <StatBox label="tip Mach" value={c.tipMach.toFixed(3)} tone={tipMachOk ? "green" : "red"} />
        </div>
        {!tipMachOk && (
          <div className="mt-1.5 flex items-start gap-1.5 rounded-[4px] border border-[#EF233C]/40 bg-[#EF233C]/[0.07] px-1.5 py-1">
            <TriangleAlert size={11} className="mt-[1px] shrink-0 text-[#ff7c8e]" />
            <div className="text-[8.5px] leading-[1.5] text-[#cf9aa3]">
              Tip Mach {c.tipMach.toFixed(3)} ≥ 0.80 — enable transonic preconditioning / shock-capturing flux limiter.
            </div>
          </div>
        )}
        <div className="mt-1.5 flex items-center justify-between text-[9px]">
          <span className="text-[#6d7484]">blade count</span>
          <span className="mono text-[#b7bfcd]">{domain.bladeCount} sectors · pitch {(360 / domain.bladeCount).toFixed(2)}°</span>
        </div>
        <div className="mono mt-[2px] flex items-center justify-between text-[8px] text-[#5d6371]">
          <span>tip radius r_t = {(domain.mrfRadius * 0.25).toFixed(3)} m</span>
          <span>ω = {((domain.rpm * Math.PI) / 30).toFixed(1)} rad/s</span>
        </div>
      </Section>

      <Section title="Mesh & numerics" icon={<Gauge size={11} />} dense>
        <div className="grid grid-cols-2 gap-1">
          {[
            { k: "cells", v: `${(domain.cells / 1e6).toFixed(2)} M` },
            { k: "prism layers", v: "34" },
            { k: "growth ratio", v: "1.18" },
            { k: "target y⁺", v: "< 1.0" },
            { k: "min ortho", v: "38.4°" },
            { k: "max skew", v: "0.62" },
          ].map((x) => (
            <div key={x.k} className="flex items-center justify-between rounded-[3px] border border-[#24262d] bg-[#0f1014] px-1.5 py-[4px]">
              <span className="text-[8px] text-[#6a7180]">{x.k}</span>
              <span className="mono text-[8.5px] text-[#b7bfcd]">{x.v}</span>
            </div>
          ))}
        </div>
        <div className="mt-1.5 flex items-center justify-between">
          <Toggle checked={domain.showMesh} onChange={(v) => setDomain((d) => ({ ...d, showMesh: v }))} label="Section overlay" />
          <Chip tone="green" dot="#23D18B">mesh valid</Chip>
        </div>
      </Section>

      <Section title="Zone summary" icon={<Shield size={11} />} defaultOpen={false} dense>
        <div className="space-y-1 text-[9px]">
          {[
            ["domain", "wind tunnel · hex-dominant"],
            ["rotor zone", "MRF · 12 400 rpm about +y"],
            ["interfaces", "2 mixing planes · conservative"],
            ["farfield", "5L up / 15L down / 5L radial"],
          ].map(([a, b]) => (
            <div key={a} className="flex justify-between gap-2">
              <span className="text-[#6d7484]">{a}</span><span className="mono truncate text-[#a7aebc]">{b}</span>
            </div>
          ))}
        </div>
      </Section>
    </div>
  );
}

const ACTOR_META: Record<ActorKind, { title: string; sub: string; icon: React.ReactNode; color: string }> = {
  injector: { title: "DPM spray injector", sub: "cone · axis · Rosin–Rammler d₃₂", icon: <Droplets size={14} />, color: "#FFB020" },
  mic: { title: "FWH microphone", sub: "observer x_mic · distance sphere", icon: <Mic size={14} />, color: "#41E0FF" },
  porous: { title: "Anisotropic porous", sub: "principal axes e₁ e₂ e₃ · κ", icon: <Waves size={14} />, color: "#A78BFA" },
};

export function RightDomain() {
  const c = useCockpit();
  const selected = c.selectedActor ?? (c.injectors[0] ? { kind: "injector" as ActorKind, id: c.injectors[0].id } : null);
  const inj = selected?.kind === "injector" ? c.injectors.find((x) => x.id === selected.id) : undefined;
  const mic = selected?.kind === "mic" ? c.mics.find((x) => x.id === selected.id) : undefined;
  const por = selected?.kind === "porous" ? c.porous.find((x) => x.id === selected.id) : undefined;
  const counts = { injector: c.injectors.length, mic: c.mics.length, porous: c.porous.length };

  const VecField = ({ label, value, onChange }: { label: string; value: [number, number, number]; onChange: (v: [number, number, number]) => void }) => (
    <div className="mb-1">
      <div className="mb-[3px] flex items-center justify-between">
        <span className="text-[8px] uppercase tracking-[0.08em] text-[#5d6371]">{label}</span>
        <span className="mono text-[7.5px] text-[#4e5a6c]">m</span>
      </div>
      <div className="grid grid-cols-3 gap-1">
        {(["x", "y", "z"] as const).map((ax, i) => (
          <div key={ax} className="flex h-[24px] items-center gap-0.5 rounded-[4px] border border-[#292b33] bg-[#0f1014] pl-1">
            <span className={`mono text-[7.5px] ${ax === "x" ? "text-[#ff8798]" : ax === "y" ? "text-[#65e5b1]" : "text-[#8fb0ff]"}`}>{ax}</span>
            <NumField value={Number(value[i].toFixed(4))} onChange={(v) => { const n = [...value] as [number, number, number]; n[i] = v; onChange(n); }} width="w-full" digits={2} />
          </div>
        ))}
      </div>
    </div>
  );

  return (
    <div className="scroll flex h-full min-h-0 flex-col overflow-y-auto bg-[#121318]">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-[#24252c] px-3">
        <Sparkles size={12} className="text-[#ffcb6b]" />
        <span className="text-[10px] font-semibold uppercase tracking-[0.11em] text-[#aeb4c2]">Physics actors</span>
        <span className="mono ml-auto text-[8px] text-[#5d6371]">{counts.injector + counts.mic + counts.porous} placed</span>
      </div>

      <div className="shrink-0 space-y-1 p-2">
        {(Object.keys(ACTOR_META) as ActorKind[]).map((kind) => {
          const m = ACTOR_META[kind];
          return (
            <div
              key={kind}
              draggable
              onDragStart={(e) => { e.dataTransfer.setData("text/actor", kind); e.dataTransfer.effectAllowed = "copy"; }}
              className="group flex cursor-grab items-center gap-2 rounded-[5px] border border-[#2b2d35] bg-[linear-gradient(180deg,#191b21,#14151a)] p-1.5 transition-colors hover:border-[#41434e] active:cursor-grabbing"
              title="Drag into the 3D viewport"
            >
              <GripVertical size={12} className="shrink-0 text-[#41444f] group-hover:text-[#6d7484]" />
              <span className="flex h-7 w-7 shrink-0 items-center justify-center rounded-[4px]" style={{ background: `${m.color}14`, color: m.color, border: `1px solid ${m.color}33` }}>{m.icon}</span>
              <span className="min-w-0 flex-1">
                <span className="block truncate text-[10px] text-[#d3d8e3]">{m.title}</span>
                <span className="mono block truncate text-[7.5px] text-[#656c7b]">{m.sub}</span>
              </span>
              <span className="flex shrink-0 items-center gap-0.5">
                <span className="mono mr-0.5 rounded-[2px] border border-[#31333c] px-1 text-[8px] text-[#7e8594]">{counts[kind]}</span>
                <IconBtn title={`Add ${m.title}`} onClick={() => c.addActor(kind)}><Plus size={11} /></IconBtn>
              </span>
            </div>
          );
        })}
        <div className="mono flex items-center justify-center gap-1 py-0.5 text-[7.5px] text-[#565d6b]">
          <Move3d size={9} /> drag a card into the viewport · apex snaps to surface
        </div>
      </div>

      {inj && (
        <div className="shrink-0 border-t border-[#24252c] p-2.5">
          <div className="mb-1.5 flex items-center justify-between">
            <div className="flex items-center gap-1.5">
              <span className="text-[#FFB020]"><Droplets size={12} /></span>
              <span className="mono text-[11px] text-[#e6eaf2]">{inj.id}</span>
              <Chip tone="amber">DPM</Chip>
            </div>
            <div className="flex items-center gap-0.5">
              <Toggle checked={inj.enabled} onChange={(v) => c.updateInjector(inj.id, { enabled: v })} size="xs" />
              <IconBtn danger title="Remove injector" onClick={() => c.removeActor("injector", inj.id)}><Trash2 size={11} /></IconBtn>
            </div>
          </div>
          <VecField label="apex position" value={inj.pos} onChange={(pos) => c.updateInjector(inj.id, { pos })} />
          <VecField label="spray axis (unit)" value={inj.axis} onChange={(axis) => c.updateInjector(inj.id, { axis })} />
          <Slider label="Cone half-angle θ" value={inj.cone} onChange={(v) => c.updateInjector(inj.id, { cone: v })} min={5} max={60} step={1} digits={0} unit="°" accent />
          <Slider label="Sauter mean diameter d₃₂" value={inj.d32} onChange={(v) => c.updateInjector(inj.id, { d32: v })} min={10} max={120} step={1} digits={0} unit=" µm" />
          <Slider label="Rosin–Rammler spread n" value={inj.spread} onChange={(v) => c.updateInjector(inj.id, { spread: v })} min={1} max={4.5} step={0.1} digits={1} />
          <div className="mt-1 rounded-[4px] border border-[#2b2d35] bg-[#0f1014] p-1.5">
            <div className="mb-0.5 flex items-center justify-between text-[8px] text-[#6d7484]">
              <span>Rosin–Rammler CDF</span>
              <span className="mono text-[#b9a26f]">d₃₂ {inj.d32} µm · n {inj.spread.toFixed(1)}</span>
            </div>
            <svg viewBox="0 0 200 38" className="w-full">
              {Array.from({ length: 40 }).map((_, i) => {
                const d = (i / 39) * 140;
                const y = 100 * (1 - Math.exp(-Math.pow(d / Math.max(1, inj.d32), inj.spread)));
                const x = 4 + (i / 39) * 190;
                const yy = 34 - (y / 100) * 30;
                if (i === 0) return null;
                const dPrev = ((i - 1) / 39) * 140;
                const yPrev = 100 * (1 - Math.exp(-Math.pow(dPrev / Math.max(1, inj.d32), inj.spread)));
                const xPrev = 4 + ((i - 1) / 39) * 190;
                return <line key={i} x1={xPrev} y1={34 - (yPrev / 100) * 30} x2={x} y2={yy} stroke="#FFB020" strokeWidth="1.2" opacity="0.85" />;
              })}
              <line x1="4" y1="34" x2="196" y2="34" stroke="#35363f" strokeWidth="0.6" />
              <line x1="4" y1="4" x2="4" y2="34" stroke="#35363f" strokeWidth="0.6" />
            </svg>
          </div>
          <div className="mt-1 grid grid-cols-2 gap-1">
            <StatBox label="exit velocity" value={Math.round(38 + inj.cone * 1.9)} unit="m/s" tone="amber" />
            <StatBox label="spray solidity" value={(inj.cone / 60).toFixed(2)} tone="neutral" />
          </div>
        </div>
      )}

      {mic && (
        <div className="shrink-0 border-t border-[#24252c] p-2.5">
          <div className="mb-1.5 flex items-center justify-between">
            <div className="flex items-center gap-1.5">
              <span className="text-[#41E0FF]"><Mic size={12} /></span>
              <span className="mono text-[11px] text-[#e6eaf2]">{mic.id}</span>
              <Chip tone="cyan">FWH</Chip>
            </div>
            <div className="flex items-center gap-0.5">
              <Toggle checked={mic.enabled} onChange={(v) => c.updateMic(mic.id, { enabled: v })} size="xs" />
              <IconBtn danger title="Remove mic" onClick={() => c.removeActor("mic", mic.id)}><Trash2 size={11} /></IconBtn>
            </div>
          </div>
          <VecField label="observer x_mic" value={mic.pos} onChange={(pos) => c.updateMic(mic.id, { pos })} />
          <Slider label="distance sphere" value={mic.radius} onChange={(v) => c.updateMic(mic.id, { radius: v })} min={0.1} max={1} step={0.01} digits={2} unit=" m" />
          <Slider label="measured SPL" value={mic.dbspl} onChange={(v) => c.updateMic(mic.id, { dbspl: v })} min={80} max={150} step={0.5} digits={1} unit=" dB" />
          <div className="mono mt-1 rounded-[4px] border border-[#27343a] bg-[#0c1417] p-1.5 text-[8px] leading-relaxed text-[#7ba3a8]">
            p′ FWH · permeable surface · retarded time τ = t − |x_mic − y| / a∞
          </div>
        </div>
      )}

      {por && (
        <div className="shrink-0 border-t border-[#24252c] p-2.5">
          <div className="mb-1.5 flex items-center justify-between">
            <div className="flex items-center gap-1.5">
              <span className="text-[#A78BFA]"><Waves size={12} /></span>
              <span className="mono text-[11px] text-[#e6eaf2]">{por.id}</span>
              <Chip tone="violet">porous</Chip>
            </div>
            <div className="flex items-center gap-0.5">
              <Toggle checked={por.enabled} onChange={(v) => c.updatePorous(por.id, { enabled: v })} size="xs" />
              <IconBtn danger title="Remove porous zone" onClick={() => c.removeActor("porous", por.id)}><Trash2 size={11} /></IconBtn>
            </div>
          </div>
          <VecField label="anchor" value={por.pos} onChange={(pos) => c.updatePorous(por.id, { pos })} />
          <VecField label="principal e₁" value={por.e1} onChange={(e1) => c.updatePorous(por.id, { e1 })} />
          <VecField label="principal e₂" value={por.e2} onChange={(e2) => c.updatePorous(por.id, { e2 })} />
          <VecField label="principal e₃" value={por.e3} onChange={(e3) => c.updatePorous(por.id, { e3 })} />
          <div className="mt-1 grid grid-cols-3 gap-1">
            {(["k1", "k2", "k3"] as const).map((k) => (
              <div key={k}>
                <div className="mb-[2px] text-[7px] uppercase text-[#5d6371]">{k} · m²</div>
                <NumField value={Number((por[k] * 1e9).toFixed(2))} onChange={(v) => c.updatePorous(por.id, { [k]: v * 1e-9 } as any)} unit="e-9" width="w-full" digits={2} />
              </div>
            ))}
          </div>
          <div className="mt-1 flex items-center gap-1.5 rounded-[4px] border border-[#342d4c] bg-[#130f1e] px-1.5 py-1 text-[8px] text-[#9f92c9]">
            <CircleDot size={9} /> anisotropy κ₂/κ₁ = {(por.k2 / por.k1).toFixed(1)} · κ₃/κ₁ = {(por.k3 / por.k1).toFixed(1)}
          </div>
        </div>
      )}

      <Section title="Scene actors" icon={<Wind size={11} />} defaultOpen dense>
        <div className="space-y-[3px]">
          {c.injectors.map((a) => (
            <ActorRow key={a.id} id={a.id} label={`${a.cone}° · d₃₂ ${a.d32}µm`} color="#FFB020" enabled={a.enabled} selected={c.selectedActor?.id === a.id} onSelect={() => c.setSelectedActor({ kind: "injector", id: a.id })} onToggle={() => c.updateInjector(a.id, { enabled: !a.enabled })} onRemove={() => c.removeActor("injector", a.id)} icon={<Droplets size={10} />} />
          ))}
          {c.mics.map((a) => (
            <ActorRow key={a.id} id={a.id} label={`r ${a.radius.toFixed(2)} m · ${a.dbspl} dB`} color="#41E0FF" enabled={a.enabled} selected={c.selectedActor?.id === a.id} onSelect={() => c.setSelectedActor({ kind: "mic", id: a.id })} onToggle={() => c.updateMic(a.id, { enabled: !a.enabled })} onRemove={() => c.removeActor("mic", a.id)} icon={<Mic size={10} />} />
          ))}
          {c.porous.map((a) => (
            <ActorRow key={a.id} id={a.id} label={`κ₁ ${(a.k1 * 1e9).toFixed(1)}e-9 m²`} color="#A78BFA" enabled={a.enabled} selected={c.selectedActor?.id === a.id} onSelect={() => c.setSelectedActor({ kind: "porous", id: a.id })} onToggle={() => c.updatePorous(a.id, { enabled: !a.enabled })} onRemove={() => c.removeActor("porous", a.id)} icon={<Waves size={10} />} />
          ))}
        </div>
      </Section>

      <div className="mt-auto shrink-0 border-t border-[#24252c] p-2">
        <div className="mono flex items-center justify-between text-[8px] text-[#5d6371]">
          <span className="flex items-center gap-1"><TrendingUp size={9} /> scene graph dirty</span>
          <span className="text-[#8fb0ff]">auto-synced to solver</span>
        </div>
      </div>
    </div>
  );
}

function ActorRow({ id, label, color, enabled, selected, onSelect, onToggle, onRemove, icon }: {
  id: string; label: string; color: string; enabled: boolean; selected: boolean; onSelect: () => void; onToggle: () => void; onRemove: () => void; icon: React.ReactNode;
}) {
  return (
    <div className={cn("flex items-center gap-1 rounded-[3px] border px-1 py-[3px]", selected ? "border-[#3c4c74] bg-[#171b24]" : "border-[#24262d] bg-[#111216]")}>
      <button onClick={onSelect} className="flex min-w-0 flex-1 items-center gap-1 text-left">
        <span style={{ color }}>{icon}</span>
        <span className="mono text-[8.5px] text-[#c2c9d6]">{id}</span>
        <span className="mono truncate text-[7.5px] text-[#5f6675]">{label}</span>
      </button>
      <button onClick={onToggle} title={enabled ? "Hide" : "Show"} className={cn("h-[14px] w-[14px] rounded-[2px] text-[7px]", enabled ? "bg-[#23D18B]/20 text-[#65e5b1]" : "bg-[#2a2b33] text-[#777e8d]")}>{enabled ? "◉" : "○"}</button>
      <IconBtn danger onClick={onRemove} title="Delete"><Trash2 size={9} /></IconBtn>
    </div>
  );
}
