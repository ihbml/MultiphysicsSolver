import { useMemo, useState, type ReactNode } from 'react';
import { Atom, Box, ChevronDown, Cpu, Flame, FlaskConical, Grid3x3, Power, Thermometer, Wind, type LucideIcon } from 'lucide-react';
import { textTint } from '../lib/color';
import type { FieldId } from '../lib/flow';
import { CAT_BY_ID, type CategoryId } from '../lib/palettes';
import { useCockpit, type CockpitState } from '../lib/store';
import { fmtSi } from '../lib/units';
import { cn } from '../utils/cn';
import { Chip, PanelHead, Switch, UnitInput } from './ui';

type BlockId = 'geometry' | 'mesh' | 'flow' | 'thermal' | 'combustion' | 'species' | 'catalysis' | 'numerics';

interface BlockDef {
  id: BlockId;
  /** 'flow' = Basis-Strömung ohne eigene Akzentfarbe (Cloud) */
  cat: CategoryId | 'flow';
  title: string;
  icon: LucideIcon;
  /** Ansicht, die der Viewport beim Auswählen des Blocks einnimmt */
  view?: Partial<CockpitState> & { field?: FieldId };
  viewLabel?: string;
}

const BLOCKS: BlockDef[] = [
  { id: 'geometry', cat: 'geometry', title: 'CAD-Import', icon: Box, view: { shading: 'matcap', edges: false }, viewLabel: 'MatCap' },
  { id: 'mesh', cat: 'mesh', title: 'Hexcore-Mesher', icon: Grid3x3, view: { field: 'nonortho', edges: true }, viewLabel: 'Zellqualität' },
  { id: 'flow', cat: 'flow', title: 'Einlass', icon: Wind, view: { field: 'cp', sliceOn: true, edges: false }, viewLabel: 'c_p + Schnitt' },
  { id: 'thermal', cat: 'thermal', title: 'Wärmeübergang', icon: Thermometer, view: { field: 'tw', edges: false }, viewLabel: 'T_w' },
  { id: 'combustion', cat: 'combustion', title: 'Verbrennung', icon: Flame },
  { id: 'species', cat: 'species', title: 'Spezies-Transport', icon: Atom },
  { id: 'catalysis', cat: 'catalysis', title: 'Wandkatalyse', icon: FlaskConical },
  { id: 'numerics', cat: 'numerics', title: 'Solver', icon: Cpu, view: { field: 'speed', sliceOn: true, edges: false }, viewLabel: '|U| + Schnitt' },
];

const FLOW_CAT = { hex: '#DFE5EB', name: 'Strömung', colorName: 'Cloud' };

interface Params {
  tol: number;
  layers: number;
  dy1: number;
  growth: number;
  p0: number;
  radiation: boolean;
  emis: number;
  phi: number;
  nSpecies: number;
  gamma: number;
}

export default function BlockStack() {
  const c = useCockpit();
  const { s } = c;
  const [open, setOpen] = useState<Record<string, boolean>>({ thermal: true, flow: true });
  const [active, setActive] = useState<Record<string, boolean>>({
    geometry: true,
    mesh: true,
    flow: true,
    thermal: true,
    combustion: false,
    species: false,
    catalysis: false,
    numerics: true,
  });
  const [p, setP] = useState<Params>({
    tol: 1e-5,
    layers: 12,
    dy1: 1e-6,
    growth: 1.2,
    p0: 3e5,
    radiation: false,
    emis: 0.85,
    phi: 0.8,
    nSpecies: 5,
    gamma: 0.01,
  });
  const up = (patch: Partial<Params>) => setP((q) => ({ ...q, ...patch }));

  const tints = useMemo(
    () => Object.fromEntries(BLOCKS.map((b) => [b.id, textTint(b.cat === 'flow' ? FLOW_CAT.hex : CAT_BY_ID[b.cat].hex, '#171920', 4.5)])),
    [],
  );

  const select = (b: BlockDef) => {
    c.set({ block: b.id });
    if (!b.view || !active[b.id]) return;
    const { field, ...rest } = b.view;
    if (field) c.setField(field);
    c.set({ block: b.id, ...rest });
  };

  // Gesamtdicke der Prismenschicht: geometrische Reihe
  const delta = p.growth === 1 ? p.dy1 * p.layers : (p.dy1 * (Math.pow(p.growth, p.layers) - 1)) / (p.growth - 1);

  const body: Record<BlockId, ReactNode> = {
    geometry: (
      <>
        <UnitInput label="Toleranz" dim="length" value={p.tol} initialText="0.01 mm" onCommit={(v) => up({ tol: v })} validate={(v) => (v > 0 ? null : 'Muss größer als 0 sein')} />
        <p className="mt-1.5 text-[12.5px] text-ink2">Patch-Gruppen</p>
        <div className="mt-1 flex flex-wrap gap-1">
          {['inlet', 'outlet', 'blade', 'platform'].map((n) => (
            <span key={n} className="num rounded-[3px] border border-line px-1.5 py-[1px] text-[11.5px] text-ink2">
              {n}
            </span>
          ))}
        </div>
        <p className="mt-2 text-[12px] leading-snug text-ink3">
          Hier steht eine Demo-Schaufel aus Joukowski-Profilen. Im Rust-Build lädt dieser Block BladeRev1CFD.obj.
        </p>
      </>
    ),
    mesh: (
      <>
        <UnitInput label={<>Lagen N</>} dim="count" value={p.layers} onCommit={(v) => up({ layers: v })} echo={false} validate={(v) => (v >= 1 && v <= 60 ? null : 'Zwischen 1 und 60')} />
        <UnitInput label={<>Erste Zellhöhe Δy₁</>} dim="length" value={p.dy1} initialText="1 µm" onCommit={(v) => up({ dy1: v })} validate={(v) => (v > 0 ? null : 'Muss größer als 0 sein')} />
        <UnitInput label="Wachstumsrate" dim="ratio" value={p.growth} onCommit={(v) => up({ growth: v })} echo={false} validate={(v) => (v >= 1 && v <= 2 ? null : 'Zwischen 1 und 2')} />
        <p className="mt-1.5 flex items-baseline justify-between gap-2 text-[12.5px] text-ink2">
          <span>Grenzschicht gesamt δ</span>
          <span className="num text-ink">{fmtSi(delta, 'length', 3)}</span>
        </p>
      </>
    ),
    flow: (
      <>
        <UnitInput
          label={<>Anströmwinkel α∞</>}
          dim="angle"
          value={(s.alphaDeg * Math.PI) / 180}
          initialText="2 deg"
          onCommit={(v) => c.set({ alphaDeg: (v * 180) / Math.PI })}
          validate={(v) => (Math.abs((v * 180) / Math.PI) <= 15 ? null : 'Zwischen −15° und 15°')}
        />
        <UnitInput label={<>Totaldruck p₀</>} dim="pressure" value={p.p0} initialText="3 bar" onCommit={(v) => up({ p0: v })} validate={(v) => (v > 0 ? null : 'Muss größer als 0 sein')} />
        <p className="mt-1.5 text-[12px] leading-snug text-ink3">α∞ wirkt sofort auf das Demo-Feld im Viewport.</p>
      </>
    ),
    thermal: (
      <>
        <UnitInput
          label={<>Totaltemperatur T₀</>}
          dim="temperature"
          value={s.tInlet}
          initialText="1450 K"
          onCommit={(v) => c.set({ tInlet: v })}
          validate={(v) => (v >= 800 && v <= 2200 ? null : 'Zwischen 800 K und 2200 K')}
        />
        <Switch checked={p.radiation} onChange={(v) => up({ radiation: v })} label="Strahlung" />
        {/* visible_if: radiation == true */}
        {p.radiation && (
          <UnitInput label="Emissivität ε" dim="ratio" value={p.emis} onCommit={(v) => up({ emis: v })} echo={false} validate={(v) => (v >= 0 && v <= 1 ? null : 'Zwischen 0 und 1')} />
        )}
        <p className="mt-1.5 text-[12px] leading-snug text-ink3">
          Probier <span className="num text-ink2">1250 C</span> – T₀ skaliert die Wandtemperatur im Viewport.
        </p>
      </>
    ),
    combustion: (
      <UnitInput label="Äquivalenzverhältnis φ" dim="ratio" value={p.phi} onCommit={(v) => up({ phi: v })} echo={false} validate={(v) => (v > 0 && v <= 3 ? null : 'Zwischen 0 und 3')} />
    ),
    species: (
      <UnitInput label="Anzahl Spezies" dim="count" value={p.nSpecies} onCommit={(v) => up({ nSpecies: v })} echo={false} validate={(v) => (v >= 1 && v <= 200 ? null : 'Zwischen 1 und 200')} />
    ),
    catalysis: (
      <UnitInput label="Rekombinationskoeffizient γ" dim="ratio" value={p.gamma} onCommit={(v) => up({ gamma: v })} echo={false} validate={(v) => (v >= 0 && v <= 1 ? null : 'Zwischen 0 und 1')} />
    ),
    numerics: (
      <>
        <UnitInput
          label="Zielresiduum"
          dim="ratio"
          value={s.target}
          initialText="1e-5"
          echo={false}
          onCommit={(v) => c.set({ target: v })}
          validate={(v) => (v >= 1e-7 && v <= 1e-2 ? null : 'Zwischen 1e-7 und 1e-2')}
        />
        <UnitInput
          label="Schreibintervall"
          dim="count"
          value={s.writeEvery}
          echo={false}
          onCommit={(v) => c.set({ writeEvery: v })}
          validate={(v) => (v >= 10 && v <= 500 ? null : 'Zwischen 10 und 500 Iterationen')}
        />
        <p className="mt-1.5 text-[12px] leading-snug text-ink3">Beides greift beim nächsten Start im Solver-Panel.</p>
      </>
    ),
  };

  const payload = {
    geometry: { tolerance_m: p.tol },
    mesh: { n_layers: p.layers, first_cell_height_m: p.dy1, growth_rate: p.growth },
    inlet: { alpha_rad: +((s.alphaDeg * Math.PI) / 180).toFixed(6), p0_Pa: p.p0, T0_K: s.tInlet },
    thermal: { radiation: p.radiation, ...(p.radiation ? { emissivity: p.emis } : {}) },
    solver: { residual_target: s.target, write_interval: s.writeEvery },
    active: Object.keys(active).filter((k) => active[k]),
  };

  const nActive = Object.values(active).filter(Boolean).length;

  return (
    <div className="inset-focus flex h-full min-h-0 flex-col bg-bg2">
      <PanelHead
        title="Block-Stack"
        right={
          <span className="num text-[12px] text-ink3">
            {nActive}/{BLOCKS.length} aktiv
          </span>
        }
      />
      <div className="min-h-0 flex-1 overflow-y-auto p-2">
        <ol className="space-y-1.5">
          {BLOCKS.map((b) => {
            const cat = b.cat === 'flow' ? FLOW_CAT : CAT_BY_ID[b.cat];
            const on = active[b.id];
            const sel = s.block === b.id;
            const isOpen = !!open[b.id];
            const Icon = b.icon;
            return (
              <li
                key={b.id}
                className={cn(
                  'relative overflow-hidden rounded-[4px] border bg-bg1 transition-colors',
                  sel ? 'border-accent-ink' : 'border-line',
                )}
              >
                <span aria-hidden className="absolute top-0 bottom-0 left-0 w-[4px]" style={{ background: cat.hex, opacity: on ? 1 : 0.35 }} />
                <div className="flex items-stretch pl-[4px]">
                  <button
                    type="button"
                    onClick={() => select(b)}
                    aria-pressed={sel}
                    className={cn('flex min-h-[44px] min-w-0 flex-1 items-center gap-2.5 px-2.5 py-1.5 text-left', sel ? 'bg-bg3' : 'hover:bg-bg3', !on && 'opacity-55')}
                  >
                    <Icon size={16} aria-hidden style={{ color: tints[b.id] }} className="shrink-0" />
                    <span className="min-w-0">
                      <span className="block truncate text-[14px] leading-tight font-medium text-ink">{b.title}</span>
                      <span className="mt-0.5 block truncate font-cond text-[12px] leading-tight tracking-[0.05em] text-ink3 uppercase">
                        {cat.name}
                        {!on ? ' · inaktiv' : b.viewLabel ? ` · zeigt ${b.viewLabel}` : ''}
                      </span>
                    </span>
                  </button>
                  <button
                    type="button"
                    role="switch"
                    aria-checked={on}
                    aria-label={`${b.title} aktiv`}
                    title={on ? 'Block überbrücken' : 'Block aktivieren'}
                    onClick={() => setActive((a) => ({ ...a, [b.id]: !a[b.id] }))}
                    className={cn('grid w-9 shrink-0 place-items-center hover:bg-bg3', on ? 'text-ink2' : 'text-ink3')}
                  >
                    <Power size={14} aria-hidden />
                  </button>
                  <button
                    type="button"
                    aria-expanded={isOpen}
                    aria-label={`${b.title} Parameter`}
                    onClick={() => setOpen((o) => ({ ...o, [b.id]: !o[b.id] }))}
                    className="grid w-9 shrink-0 place-items-center text-ink3 hover:bg-bg3 hover:text-ink"
                  >
                    <ChevronDown size={15} aria-hidden className={cn('transition-transform', isOpen && 'rotate-180')} />
                  </button>
                </div>
                {isOpen && (
                  <div className={cn('border-t border-line py-2 pr-2.5 pl-[14px]', !on && 'opacity-55')}>
                    {b.id === 'flow' && (
                      <Chip className="mb-1.5">Basis-Strömung · ohne Akzent</Chip>
                    )}
                    {body[b.id]}
                  </div>
                )}
              </li>
            );
          })}
        </ol>

        <details className="mt-3 rounded-[4px] border border-line bg-bg1">
          <summary className="label cursor-pointer px-2.5 py-2 select-none hover:text-ink2">Payload ans Backend (SI)</summary>
          <pre className="num overflow-x-auto border-t border-line px-2.5 py-2 text-[11.5px] leading-[1.5] text-ink2">{JSON.stringify(payload, null, 1)}</pre>
        </details>
      </div>
    </div>
  );
}
