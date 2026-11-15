import { Fragment } from 'react';
import { lutColor } from '../lib/colormaps';
import { rgbToCss } from '../lib/color';
import type { FieldDef } from '../lib/flow';
import { fmtFixed } from '../lib/units';
import { Ramp } from './ui';

/** „T_w“ → T mit tiefgestelltem w. */
export function Sym({ s }: { s: string }) {
  const parts = s.split(/_([A-Za-z0-9∞]+)/);
  return (
    <span className="num">
      {parts.map((p, i) => (i % 2 ? <sub key={i}>{p}</sub> : <Fragment key={i}>{p}</Fragment>))}
    </span>
  );
}

const HATCH_UP =
  'repeating-linear-gradient(45deg, rgba(255,255,255,0.6) 0 3.5px, transparent 3.5px 7px)';
const HATCH_DOWN = 'repeating-linear-gradient(45deg, rgba(0,0,0,0.6) 0 3.5px, transparent 3.5px 7px)';

export default function Legend({
  lut,
  range,
  bands,
  def,
  mapName,
  hatch,
  nanOn,
  nanColor,
  nanName,
  probe,
}: {
  lut: Uint8Array;
  range: [number, number];
  bands: number;
  def: FieldDef;
  mapName: string;
  hatch: boolean;
  nanOn: boolean;
  nanColor: string;
  nanName: string;
  probe: { value: number; nan: boolean } | null;
}) {
  const ticks = [0, 0.25, 0.5, 0.75, 1];
  const span = range[1] - range[0];
  // feine Bereiche brauchen mehr Nachkommastellen als die Feld-Voreinstellung
  const digits = Math.max(def.digits, span < 0.4 ? 3 : span < 4 ? 2 : span < 40 ? 1 : 0);
  const pt = probe && !probe.nan ? (probe.value - range[0]) / span : null;
  const ptc = pt === null ? null : Math.min(1, Math.max(0, pt));

  return (
    <div className="pointer-events-none flex flex-col items-end gap-1.5 rounded-[4px] border border-line bg-bg1/85 px-2.5 py-2 backdrop-blur-[2px]">
      <p className="text-right text-[13px] leading-none text-ink">
        <Sym s={def.symbol} />
        {def.unit !== '–' && <span className="num ml-1 text-ink2">[{def.unit}]</span>}
      </p>

      <div className="flex items-stretch">
        <div className="relative w-[58px]">
          {ticks.map((t) => (
            <span
              key={t}
              className="num absolute right-[7px] -translate-y-1/2 text-[11.5px] leading-none whitespace-nowrap text-ink2"
              style={{ top: `calc(${hatch ? 9 : 0}px + (100% - ${hatch ? 18 : 0}px) * ${1 - t})` }}
            >
              {fmtFixed(range[0] + span * t, digits)}
            </span>
          ))}
        </div>
        <div className="flex w-[14px] flex-col">
          {hatch && (
            <div
              className="h-[9px] rounded-t-[2px]"
              style={{ backgroundImage: HATCH_UP, backgroundColor: rgbToCss(lutColor(lut, 1)) }}
              title="oberhalb des Bereichs"
            />
          )}
          <div className="relative h-[clamp(120px,30vh,260px)]">
            <Ramp lut={lut} bands={bands} dir="to top" className="absolute inset-0" />
            {ticks.map((t) => (
              <span key={t} className="absolute -left-[4px] h-px w-[4px] bg-ink3" style={{ top: `${(1 - t) * 100}%` }} />
            ))}
            {ptc !== null && (
              <span
                className="absolute -right-[7px] -left-[4px] h-[2px] -translate-y-1/2"
                style={{ top: `${(1 - ptc) * 100}%`, background: '#F4EB06', boxShadow: '0 0 0 1px rgba(0,0,0,0.7)' }}
              />
            )}
          </div>
          {hatch && (
            <div
              className="h-[9px] rounded-b-[2px]"
              style={{ backgroundImage: HATCH_DOWN, backgroundColor: rgbToCss(lutColor(lut, 0)) }}
              title="unterhalb des Bereichs"
            />
          )}
        </div>
      </div>

      <p className="text-right text-[11.5px] leading-tight text-ink3">
        {mapName}
        <br />
        {bands ? `${bands} Iso-Bänder` : 'stufenlos'}
      </p>
      {nanOn && (
        <p className="flex items-center gap-1.5 text-[11.5px] leading-none text-ink2">
          <span className="h-[10px] w-[10px] rounded-[1px]" style={{ background: nanColor }} />
          <span>
            NaN · <span className="text-ink3">{nanName}</span>
          </span>
        </p>
      )}
    </div>
  );
}
