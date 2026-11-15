import { useState } from 'react';
import { ChevronDown, CircleCheck, RotateCcw, TriangleAlert } from 'lucide-react';
import { analyze, COLORMAPS, getLut, GROUP_LABEL, GROUP_ORDER, KIND_LABEL, MAP_BY_ID, pickSignal } from '../lib/colormaps';
import { FIELDS, FIELD_BY_ID } from '../lib/flow';
import { useCockpit, type Bands, type Shading } from '../lib/store';
import { cn } from '../utils/cn';
import { Sym } from './Legend';
import { Group, PanelHead, Ramp, Segmented, Switch, UnitInput } from './ui';

const WHERE = { volume: 'Volumen', wall: 'Wand', slice: 'Schnitt' } as const;

export default function Inspector() {
  const c = useCockpit();
  const { s } = c;
  const def = FIELD_BY_ID[s.field];
  const map = MAP_BY_ID[c.mapId];
  const an = analyze(c.mapId);
  const sig = pickSignal(c.mapId);
  const [open, setOpen] = useState(false);
  const scalar = s.shading === 'unlit';
  const locked = s.zeroLock && def.signed && c.diverging;

  const advice =
    map.kind === 'rainbow'
      ? (map.note ?? null)
      : def.signed && map.kind !== 'diverging'
        ? 'Das Feld hat ein Vorzeichen. Eine divergente Map macht die Null sichtbar.'
        : !def.signed && map.kind === 'diverging'
          ? 'Das Feld hat kein Vorzeichen – die neutrale Mitte dieser Map trägt hier keine Bedeutung.'
          : null;

  return (
    <div className="inset-focus flex h-full min-h-0 flex-col bg-bg2">
      <PanelHead title="Inspector" right={<span className="text-[12px] text-ink3">Darstellung</span>} />
      <div className="min-h-0 flex-1 overflow-y-auto">
        {/* ------------------------------------------------------ Feld ---- */}
        <Group title="Feld">
          <div role="radiogroup" aria-label="Feld" className="-mx-1.5 space-y-px">
            {FIELDS.map((f) => {
              const on = f.id === s.field;
              return (
                <button
                  key={f.id}
                  type="button"
                  role="radio"
                  aria-checked={on}
                  onClick={() => c.setField(f.id)}
                  className={cn(
                    'flex min-h-[30px] w-full items-center gap-2 rounded-[3px] px-1.5 text-left text-[13px] max-sm:min-h-[40px]',
                    on ? 'bg-bg4 text-ink shadow-[inset_2px_0_0_var(--c-accent-ink)]' : 'text-ink2 hover:bg-bg3 hover:text-ink',
                  )}
                >
                  <span className="w-[76px] shrink-0 truncate">
                    <Sym s={f.symbol} />
                  </span>
                  <span className="min-w-0 flex-1 truncate">{f.name}</span>
                  <span className="font-cond text-[11.5px] tracking-[0.06em] text-ink3 uppercase">{WHERE[f.where]}</span>
                </button>
              );
            })}
          </div>
        </Group>

        {/* -------------------------------------------------- Colormap ---- */}
        <Group title="Colormap">
          <button
            type="button"
            aria-expanded={open}
            aria-controls="map-list"
            onClick={() => setOpen((o) => !o)}
            className="flex w-full items-center gap-2 rounded-[3px] border border-line bg-bg0 p-1.5 text-left hover:border-line2"
          >
            <Ramp lut={c.lut} bands={s.bands} className="h-[18px] min-w-0 flex-1 rounded-[2px]" />
            <span className="shrink-0 text-[13px] text-ink">{map.name}</span>
            <ChevronDown size={14} aria-hidden className={cn('shrink-0 text-ink3 transition-transform', open && 'rotate-180')} />
          </button>

          {open && (
            <div id="map-list" className="mt-1.5 max-h-[290px] overflow-y-auto rounded-[3px] border border-line bg-bg0 p-1">
              {GROUP_ORDER.map((g) => (
                <div key={g}>
                  <p className="label px-1 pt-2 pb-1 text-[11px]">{GROUP_LABEL[g]}</p>
                  {COLORMAPS.filter((m) => m.group === g).map((m) => {
                    const on = m.id === c.mapId;
                    return (
                      <button
                        key={m.id}
                        type="button"
                        aria-pressed={on}
                        onClick={() => {
                          c.setMap(m.id);
                          setOpen(false);
                        }}
                        className={cn(
                          'flex min-h-[30px] w-full items-center gap-2 rounded-[3px] px-1 text-left max-sm:min-h-[40px]',
                          on ? 'bg-bg4' : 'hover:bg-bg3',
                        )}
                      >
                        <Ramp lut={getLut(m.id)} className="h-[12px] w-[84px] shrink-0 rounded-[2px]" />
                        <span className={cn('min-w-0 flex-1 truncate text-[13px]', on ? 'text-ink' : 'text-ink2')}>{m.name}</span>
                        <span className="font-cond text-[11px] tracking-[0.05em] text-ink3 uppercase">{KIND_LABEL[m.kind].slice(0, 4)}.</span>
                      </button>
                    );
                  })}
                </div>
              ))}
            </div>
          )}

          <p className={cn('mt-2 flex items-start gap-1.5 text-[12.5px] leading-snug', an.ok ? 'text-ink2' : 'text-warn')}>
            {an.ok ? (
              <CircleCheck size={14} className="mt-[1px] shrink-0 text-ok" aria-hidden />
            ) : (
              <TriangleAlert size={14} className="mt-[1px] shrink-0" aria-hidden />
            )}
            <span>
              {KIND_LABEL[map.kind]} · {an.verdict}
            </span>
          </p>
          {advice && <p className="mt-1.5 text-[12.5px] leading-snug text-warn">{advice}</p>}
          <div className="mt-1.5">
            <Switch
              checked={s.reverse[s.field]}
              onChange={(v) => c.set({ reverse: { ...s.reverse, [s.field]: v } })}
              label="Umkehren"
            />
          </div>
        </Group>

        {/* --------------------------------------------------- Bereich ---- */}
        <Group
          title="Bereich"
          right={
            <button
              type="button"
              onClick={c.resetRange}
              className="inline-flex items-center gap-1 rounded-[3px] px-1 py-0.5 text-[12px] text-ink2 hover:bg-bg4 hover:text-ink"
            >
              <RotateCcw size={11} aria-hidden /> Zurücksetzen
            </button>
          }
        >
          <div className="grid grid-cols-2 gap-2">
            <UnitInput
              key={s.field + '-min'}
              label="Min"
              dim={def.dim}
              value={c.range[0]}
              echo={def.dim === 'temperature'}
              compact
              onCommit={(v) => c.setRangeEdge(0, v)}
              validate={(v) => (locked ? (v === 0 ? 'Darf nicht 0 sein' : null) : v < c.range[1] ? null : 'Min muss unter Max liegen')}
            />
            <UnitInput
              key={s.field + '-max'}
              label="Max"
              dim={def.dim}
              value={c.range[1]}
              echo={def.dim === 'temperature'}
              compact
              onCommit={(v) => c.setRangeEdge(1, v)}
              validate={(v) => (locked ? (v === 0 ? 'Darf nicht 0 sein' : null) : v > c.range[0] ? null : 'Max muss über Min liegen')}
            />
          </div>
          {def.dim === 'temperature' && (
            <p className="mt-1.5 text-[12px] leading-snug text-ink3">
              Versteht Einheiten: <span className="num text-ink2">850 C</span> wird zu <span className="num text-ink2">1123.15 K</span>.
            </p>
          )}
          {def.signed && (
            <div className="mt-1.5">
              <Switch
                checked={s.zeroLock}
                onChange={(v) => c.set({ zeroLock: v })}
                label="Null in der Mitte halten"
                hint={c.diverging ? 'Min und Max bleiben symmetrisch.' : 'Greift, sobald eine divergente Map aktiv ist.'}
              />
            </div>
          )}
        </Group>

        {/* ----------------------------------------------- Darstellung ---- */}
        <Group title="Darstellung">
          <Segmented<Bands>
            label="Iso-Bänder"
            stretch
            value={s.bands}
            onChange={(v) => c.set({ bands: v })}
            options={[
              { value: 0, label: 'stufenlos' },
              { value: 10, label: '10' },
              { value: 16, label: '16' },
              { value: 24, label: '24' },
            ]}
          />
          <div className="mt-2">
            <Segmented<Shading>
              label="Shading"
              stretch
              value={s.shading}
              onChange={(v) => c.set({ shading: v })}
              options={[
                { value: 'unlit', label: 'Skalar · unlit', title: 'Kein Licht, kein Tone-Mapping: Bildschirmfarbe = Legendenfarbe' },
                { value: 'matcap', label: 'MatCap', title: 'CAD- und Gitter-Modus ohne Skalarfeld' },
              ]}
            />
          </div>
          <div className="mt-1.5">
            <Switch checked={s.edges} onChange={(v) => c.set({ edges: v })} label="Netzkanten" />
          </div>
        </Group>

        {/* ---------------------------------------------- Schnittebene ---- */}
        <Group title="Schnittebene">
          {def.where === 'wall' ? (
            <p className="text-[12.5px] leading-snug text-ink3">
              <Sym s={def.symbol} /> liegt nur auf der Wand vor (boundaryField). Für eine Schnittebene braucht es ein Volumenfeld wie{' '}
              <Sym s="c_p" /> oder |U|.
            </p>
          ) : (
            <>
              <Switch
                checked={s.sliceOn || def.where === 'slice'}
                disabled={def.where === 'slice' || !scalar}
                onChange={(v) => c.set({ sliceOn: v })}
                label="Schnitt anzeigen"
                hint={def.where === 'slice' ? 'Dieses Feld existiert nur im Schnitt.' : !scalar ? 'Im MatCap-Modus ausgeblendet.' : undefined}
              />
              <label className="mt-1 flex items-center justify-between text-[13px] text-ink2" htmlFor="slice-h">
                <span>Höhe</span>
                <span className="num text-ink">{Math.round(s.sliceS * 100)} % Spannweite</span>
              </label>
              <input
                id="slice-h"
                type="range"
                min={0.04}
                max={0.96}
                step={0.01}
                value={s.sliceS}
                disabled={!scalar}
                onChange={(e) => c.set({ sliceS: Number(e.target.value) })}
                className="mt-1 h-6 w-full"
              />
            </>
          )}
        </Group>

        {/* --------------------------------------------------- Signale ---- */}
        <Group title="Signale">
          <Switch
            checked={s.nanOn}
            onChange={(v) => c.set({ nanOn: v })}
            label="NaN-Zellen einblenden (Demo)"
            hint={
              sig.dist - sig.other.dist < 1
                ? `Signalfarbe ${sig.name} (ΔE ${sig.dist.toFixed(0)} zur Map; ${sig.other.name} liegt gleichauf).`
                : `${sig.name} hat den größten Abstand zu dieser Map (ΔE ${sig.dist.toFixed(0)}, ${sig.other.name} nur ${sig.other.dist.toFixed(0)}).`
            }
          />
          <Switch
            checked={s.hatch}
            onChange={(v) => c.set({ hatch: v })}
            label="Außerhalb des Bereichs schraffieren"
            hint="Muster statt Farbe – bleibt mit jeder Map lesbar."
          />
        </Group>
      </div>
    </div>
  );
}
