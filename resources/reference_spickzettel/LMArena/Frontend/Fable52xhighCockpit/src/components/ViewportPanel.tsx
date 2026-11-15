import { useCallback, useMemo, useState } from 'react';
import { MAP_BY_ID, pickSignal } from '../lib/colormaps';
import { FIELD_BY_ID, nanDemoCount } from '../lib/flow';
import { useCockpit, useSolver } from '../lib/store';
import { fmtFixed } from '../lib/units';
import { cn } from '../utils/cn';
import Legend, { Sym } from './Legend';
import { Chip, useSize } from './ui';
import Viewport3D, { type ProbeHit, type ViewPreset } from './Viewport3D';

const VIEW_BTNS: { id: ViewPreset; label: string; title: string }[] = [
  { id: 'iso', label: 'Iso', title: 'Schrägansicht auf Vorderkante und Saugseite' },
  { id: 'suction', label: 'Saug', title: 'Blick auf die Saugseite' },
  { id: 'pressure', label: 'Druck', title: 'Blick auf die Druckseite' },
  { id: 'top', label: 'Oben', title: 'Draufsicht auf die Schnittebene' },
];

const WHERE_LABEL = { volume: 'Volumen + Wand', wall: 'nur Wand', slice: 'nur Schnitt' } as const;

export default function ViewportPanel() {
  const c = useCockpit();
  const { s } = c;
  const { flow, sv } = useSolver();
  const [probe, setProbe] = useState<ProbeHit | null>(null);
  const [view, setView] = useState<{ preset: ViewPreset; nonce: number }>({ preset: 'iso', nonce: 0 });
  const [ref, size] = useSize<HTMLDivElement>();
  const onProbe = useCallback((h: ProbeHit | null) => setProbe(h), []);

  const def = FIELD_BY_ID[s.field];
  const scalar = s.shading === 'unlit';
  const map = MAP_BY_ID[c.mapId];
  const sig = pickSignal(c.mapId);
  const nanCount = useMemo(() => nanDemoCount(), []);
  const loaded = sv.dirs[sv.current] ?? 0;
  const out = probe && !probe.nan && (probe.value < c.range[0] || probe.value > c.range[1]);

  return (
    <div
      ref={ref}
      className="relative h-full min-h-0 overflow-hidden"
      style={{ background: 'radial-gradient(120% 95% at 50% 30%, var(--c-bg3) 0%, var(--c-bg0) 72%)' }}
    >
      <Viewport3D
        field={s.field}
        lut={c.lut}
        range={c.range}
        bands={s.bands}
        shading={s.shading}
        edges={s.edges}
        sliceOn={s.sliceOn}
        sliceS={s.sliceS}
        nanOn={s.nanOn}
        nanColor={c.nanColor}
        hatch={s.hatch}
        flow={flow}
        view={view}
        onProbe={onProbe}
      />

      {/* Feldtitel */}
      <div className="pointer-events-none absolute top-2.5 left-3 max-w-[min(calc(100%-215px),280px)] max-sm:max-w-[calc(100%-24px)]">
        {scalar ? (
          <>
            <p className="flex flex-wrap items-center gap-x-2 gap-y-1 text-[15px] leading-tight text-ink">
              <span>
                <Sym s={def.symbol} /> <span className="text-ink2">· {def.name}</span>
              </span>
              <Chip>{WHERE_LABEL[def.where]}</Chip>
            </p>
            <p className="mt-1 text-[12px] leading-snug text-ink3">
              Zeit <span className="num text-ink2">{loaded}</span> · Demo-Daten: {def.demo}
            </p>
          </>
        ) : (
          <>
            <p className="text-[15px] leading-tight text-ink">MatCap · CAD- und Gitter-Modus</p>
            <p className="mt-1 text-[12px] leading-snug text-ink3">Kein Skalarfeld aktiv, deshalb keine Legende.</p>
          </>
        )}
        {s.nanOn && (
          <p className="mt-2 inline-flex items-center gap-1.5 rounded-[3px] border border-line bg-bg1/85 px-1.5 py-1 text-[12px] leading-none text-ink">
            <span className="h-[9px] w-[9px] rounded-[1px]" style={{ background: c.nanColor }} />
            <span className="num">{nanCount}</span> NaN-Zellen
          </p>
        )}
      </div>

      {/* Ansichten */}
      <div className="absolute top-2.5 right-3 flex rounded-[4px] border border-line bg-bg1/85 p-[2px] max-sm:top-auto max-sm:right-auto max-sm:bottom-[74px] max-sm:left-2">
        {VIEW_BTNS.map((b) => (
          <button
            key={b.id}
            type="button"
            title={b.title}
            onClick={() => setView((v) => ({ preset: b.id, nonce: v.nonce + 1 }))}
            className={cn(
              'min-h-[26px] rounded-[3px] px-2 text-[12.5px] leading-none text-ink2 hover:bg-bg4 hover:text-ink max-sm:min-h-[34px] max-sm:px-2.5',
            )}
          >
            {b.label}
          </button>
        ))}
      </div>

      {/* Legende */}
      {scalar && (
        <div className="absolute right-3 bottom-3">
          <Legend
            lut={c.lut}
            range={c.range}
            bands={s.bands}
            def={def}
            mapName={map.name + (s.reverse[s.field] ? ' (umgekehrt)' : '')}
            hatch={s.hatch}
            nanOn={s.nanOn}
            nanColor={c.nanColor}
            nanName={sig.name}
            probe={probe}
          />
        </div>
      )}

      {/* Probe */}
      {probe && scalar && (
        <div
          className="pointer-events-none absolute z-10 rounded-[3px] border border-line2 bg-bg1/95 px-2 py-1.5 shadow-[0_4px_14px_rgba(0,0,0,0.45)]"
          style={{
            left: Math.max(6, Math.min(probe.x + 14, size.w - 176)),
            top: Math.max(6, Math.min(probe.y + 16, size.h - 58)),
          }}
        >
          <p className="font-cond text-[11.5px] leading-none font-semibold tracking-[0.08em] text-ink3 uppercase">Probe · {probe.where}</p>
          {probe.nan ? (
            <p className="num mt-1 text-[14px] leading-none" style={{ color: c.nanColor }}>
              NaN <span className="text-[12px] text-ink3">Zelle divergiert</span>
            </p>
          ) : (
            <p className="num mt-1 text-[14px] leading-none text-ink">
              <Sym s={def.symbol} /> = {fmtFixed(probe.value, def.digits + 1)}
              {def.unit !== '–' && <span className="text-ink2"> {def.unit}</span>}
            </p>
          )}
          {out && <p className="mt-1 text-[11.5px] leading-none text-warn">außerhalb des Bereichs</p>}
        </div>
      )}
    </div>
  );
}
