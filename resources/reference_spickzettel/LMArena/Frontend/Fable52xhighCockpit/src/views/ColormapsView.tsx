import { useState } from 'react';
import { ArrowRight, CircleCheck, TriangleAlert } from 'lucide-react';
import type { Nav } from '../App';
import { Sym } from '../components/Legend';
import LPlot from '../components/LPlot';
import { Chip, PageHead, Ramp, SectionHead, Segmented } from '../components/ui';
import { CVD_LABEL, hexToRgb, lstar, mixLinear, mixOklab, mixOklch, mixSrgb, rgbToCss, rgbToOklab, type Cvd, type RGB } from '../lib/color';
import { analyze, COLORMAPS, getLut, GROUP_LABEL, GROUP_ORDER, KIND_LABEL, MAP_BY_ID, pickSignal } from '../lib/colormaps';
import type { Bands } from '../lib/store';
import { cn } from '../utils/cn';

const CVD_SHORT: Record<Cvd, string> = { normal: 'Normal', protan: 'Protan', deutan: 'Deutan', tritan: 'Tritan' };

const REPO = 'https://github.com/jrwrigh/cfdpost_colormaps/blob/master/colormaps/';

const DEFAULTS: { q: string; sym: string; map: string; rule: string }[] = [
  { q: 'Temperatur', sym: 'T, T_w', map: 'inferno', rule: 'Feste physikalische Grenzen statt Auto-Bereich' },
  { q: 'Druckbeiwert, Relativdruck', sym: 'c_p, p − p∞', map: 'smooth-cool-warm', rule: 'Symmetrisch um 0' },
  { q: 'Axialgeschwindigkeit', sym: 'U_x', map: 'smooth-cool-warm', rule: 'Symmetrisch um 0 – Blau zeigt Rückströmung' },
  { q: 'Wandwärmestrom', sym: 'q_w', map: 'fast', rule: 'Symmetrisch um 0 – Heizen gegen Kühlen' },
  { q: 'Geschwindigkeitsbetrag, Mach-Zahl', sym: '|U|, Ma', map: 'aero', rule: '0 bis Maximum' },
  { q: 'Zellqualität', sym: 'nonOrtho, skewness', map: 'ampel', rule: 'Gebändert; Schwellen aus checkMesh (70° bzw. 4)' },
  { q: 'Dimensionsloser Wandabstand', sym: 'y⁺', map: 'ampel', rule: 'Gebändert; Zielband des Turbulenzmodells' },
  { q: 'Dichtegradient', sym: '‖∇ρ‖', map: 'schlieren', rule: 'Logarithmisch oder exp(−k·x)' },
  { q: 'Spezies-Massenbruch', sym: 'Y_i', map: 'gruen-weiss', rule: '0 bis 1' },
  { q: 'Wärmefreisetzung', sym: 'Q̇', map: 'dark-ember', rule: '0 bis Maximum' },
];

function gradient(a: RGB, b: RGB, mix: (a: RGB, b: RGB, t: number) => RGB): string {
  const n = 32;
  return `linear-gradient(to right, ${Array.from({ length: n + 1 }, (_, i) => rgbToCss(mix(a, b, i / n))).join(', ')})`;
}
const chroma = (c: RGB) => {
  const l = rgbToOklab(c);
  return Math.hypot(l[1], l[2]);
};

export default function ColormapsView({ nav }: { nav: Nav }) {
  const [bands, setBands] = useState<Bands>(0);
  const [cvd, setCvd] = useState<Cvd>('normal');
  const [ca, setCa] = useState('#009E73');
  const [cb, setCb] = useState('#EF233C');
  const A = hexToRgb(ca);
  const B = hexToRgb(cb);
  const mixes: { label: string; hint: string; fn: (a: RGB, b: RGB, t: number) => RGB }[] = [
    { label: 'sRGB-Bytes gemischt', hint: 'naiver Verlauf', fn: mixSrgb },
    { label: 'Linear gemischt', hint: 'GPU-Filter auf einer sRGB-Textur', fn: mixLinear },
    { label: 'Oklab, gerade Linie', hint: 'so backen wir zwischen Stützstellen', fn: mixOklab },
    { label: 'OKLCH über den Farbton', hint: 'Umweg statt gerader Linie', fn: mixOklch },
  ];
  const endChroma = (chroma(A) + chroma(B)) / 2;
  const la = lstar(A);
  const lb = lstar(B);
  /** größte Abweichung der Helligkeit von der Geraden zwischen den Enden */
  const bend = (fn: (a: RGB, b: RGB, t: number) => RGB) => {
    let worst = 0;
    for (let i = 0; i <= 32; i++) worst = Math.max(worst, Math.abs(lstar(fn(A, B, i / 32)) - (la + (lb - la) * (i / 32))));
    return worst;
  };

  return (
    <div className="space-y-14">
      <PageHead kicker="Designreferenz · Colormaps" title={`${COLORMAPS.length} Colormaps, jede mit Helligkeitsnachweis`}>
        <p>
          Deine vier Vorgaben, die zwei Paletten aus den Referenzbildern, die Ansys-CFD-Post-Maps aus dem jrwrigh-Repo (das sind
          Morelands Tabellen als <span className="num">.cst</span>) und drei Ergänzungen (Turbo, Cividis, Schlieren). Jede Map wird in Oklab zu 256 Texeln gebacken;
          der Punktplot rechts zeigt, wie hell das Auge jede Stelle liest.
        </p>
      </PageHead>

      <div>
        <div className="sticky top-0 z-10 -mx-4 flex flex-wrap items-center gap-x-5 gap-y-2 border-b border-line bg-bg1/95 px-4 py-2.5 backdrop-blur-sm sm:-mx-8 sm:px-8">
          <div className="flex max-w-full min-w-0 items-center gap-2">
            <span className="label">Bänder</span>
            <Segmented<Bands>
              label="Iso-Bänder"
              value={bands}
              onChange={setBands}
              options={[
                { value: 0, label: 'stufenlos' },
                { value: 10, label: '10' },
                { value: 16, label: '16' },
                { value: 24, label: '24' },
              ]}
            />
          </div>
          <div className="flex max-w-full min-w-0 items-center gap-2">
            <span className="label">Sehen</span>
            <Segmented<Cvd>
              label="Farbsehen simulieren"
              value={cvd}
              onChange={setCvd}
              options={(Object.keys(CVD_LABEL) as Cvd[]).map((k) => ({ value: k, label: CVD_SHORT[k], title: CVD_LABEL[k] }))}
            />
          </div>
        </div>

        {GROUP_ORDER.map((g) => (
          <section key={g} className="mt-9">
            <h2 className="label text-[13px] text-ink2">{GROUP_LABEL[g]}</h2>
            <div className="mt-2">
              {COLORMAPS.filter((m) => m.group === g).map((m) => {
                const a = analyze(m.id);
                const sig = pickSignal(m.id);
                return (
                  <article key={m.id} className="grid gap-x-6 gap-y-3 border-t border-line py-5 md:grid-cols-[minmax(0,1fr)_236px]">
                    <div className="min-w-0">
                      <div className="flex flex-wrap items-center gap-x-2.5 gap-y-1">
                        <h3 className="display text-[22px] text-ink">{m.name}</h3>
                        <Chip>{KIND_LABEL[m.kind]}</Chip>
                        {m.cst && (
                          <a href={REPO + m.cst} target="_blank" rel="noreferrer" className="num text-[12px]" title="CFD-Post-Datei im jrwrigh-Repo">
                            {m.cst}
                          </a>
                        )}
                      </div>
                      <Ramp lut={getLut(m.id)} bands={bands} cvd={cvd} className="mt-2.5 h-8 rounded-[3px] shadow-[inset_0_0_0_1px_rgba(255,255,255,0.06)]" />
                      <p className="mt-2.5 text-[14px] leading-snug text-ink">{m.use}</p>
                      {m.note && <p className="mt-1 max-w-[70ch] text-[13.5px] leading-snug text-ink2">{m.note}</p>}
                      <p className="mt-1.5 text-[12.5px] leading-snug text-ink3">
                        Quelle:{' '}
                        {m.url ? (
                          <a href={m.url} target="_blank" rel="noreferrer">
                            {m.source}
                          </a>
                        ) : (
                          m.source
                        )}
                        {' · '}NaN-Farbe{' '}
                        <span className="inline-flex items-center gap-1 text-ink2">
                          <span aria-hidden className="inline-block h-2.5 w-2.5 rounded-[1px]" style={{ background: sig.hex }} />
                          {sig.name}
                        </span>
                      </p>
                    </div>
                    <div className="min-w-0">
                      <LPlot id={m.id} className="block w-full max-w-[260px]" />
                      <p className={cn('mt-1 flex items-start gap-1.5 text-[12.5px] leading-snug', a.ok ? 'text-ink2' : 'text-warn')}>
                        {a.ok ? <CircleCheck size={14} className="mt-[1px] shrink-0 text-ok" aria-hidden /> : <TriangleAlert size={14} className="mt-[1px] shrink-0" aria-hidden />}
                        <span>
                          {a.verdict}
                          <span className="num text-ink3">
                            {' '}
                            · {a.lmin.toFixed(0)}–{a.lmax.toFixed(0)}
                          </span>
                        </span>
                      </p>
                      <button
                        type="button"
                        onClick={() => nav.openMap(m.id)}
                        className="mt-2 inline-flex min-h-[30px] items-center gap-1.5 rounded-[3px] border border-line2 px-2.5 text-[13px] text-ink hover:bg-bg4"
                      >
                        Im Cockpit ansehen <ArrowRight size={13} aria-hidden />
                      </button>
                    </div>
                  </article>
                );
              })}
            </div>
          </section>
        ))}
      </div>

      {/* ------------------------------------------------------ Zuordnung ---- */}
      <section>
        <SectionHead title="Welche Größe bekommt welche Map">
          <p>Voreinstellungen für den Feld-Wechsel. Sie landen als field_defaults in palettes.json und bleiben pro Fall überschreibbar.</p>
        </SectionHead>
        <div className="mt-5 overflow-x-auto rounded-[6px] border border-line">
          <table className="w-full min-w-[640px] border-collapse text-left text-[13.5px]">
            <thead>
              <tr className="bg-bg2">
                {['Größe', 'Symbol', 'Colormap', 'Bereichsregel'].map((h) => (
                  <th key={h} scope="col" className="label px-3 py-2 font-semibold">
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {DEFAULTS.map((d) => (
                <tr key={d.q} className="border-t border-line align-middle">
                  <th scope="row" className="px-3 py-2 font-medium text-ink">
                    {d.q}
                  </th>
                  <td className="px-3 py-2 whitespace-nowrap text-ink2">
                    <Sym s={d.sym} />
                  </td>
                  <td className="px-3 py-2">
                    <button type="button" onClick={() => nav.openMap(d.map)} className="group flex items-center gap-2.5 rounded-[3px] text-left" title="Im Cockpit ansehen">
                      <Ramp lut={getLut(d.map)} className="h-3.5 w-[92px] shrink-0 rounded-[2px]" />
                      <span className="text-accent-ink underline-offset-2 group-hover:underline">{MAP_BY_ID[d.map].name}</span>
                    </button>
                  </td>
                  <td className="px-3 py-2 text-ink2">{d.rule}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="mt-2 text-[13px] text-ink3">Residuen bekommen keine Colormap, sondern den logarithmischen Linienplot im Solver-Panel.</p>
      </section>

      {/* -------------------------------------------------- Interpolation ---- */}
      <section>
        <SectionHead title="Wie zwischen zwei Stützstellen gemischt wird">
          <p>
            Zwei Endfarben, vier Wege dazwischen. Naiv gemischt wird die Mitte dunkel und grau. Linear gemischt stimmt das Licht, aber die
            Helligkeit läuft je nach Farbpaar krumm. Oklab hält sie gerade und die Schritte gleich groß. Bei Gegenfarben wie Grün und Rot
            führt trotzdem jede gerade Linie Richtung Grau – bunt bleibt nur der Weg über den Farbton. Deshalb wird in Oklab gebacken, und
            die Ampel bekommt Gelb als eigene Stützstelle.
          </p>
        </SectionHead>
        <div className="mt-5 flex flex-wrap items-center gap-4">
          {[
            { label: 'Start', v: ca, set: setCa },
            { label: 'Ende', v: cb, set: setCb },
          ].map((f) => (
            <label key={f.label} className="flex items-center gap-2 text-[13.5px] text-ink2">
              {f.label}
              <input
                type="color"
                value={f.v}
                onChange={(e) => f.set(e.target.value.toUpperCase())}
                className="h-8 w-11 cursor-pointer rounded-[3px] border border-line2 bg-bg0 p-[2px]"
              />
              <span className="num text-ink">{f.v}</span>
            </label>
          ))}
          <button
            type="button"
            onClick={() => {
              setCa('#009E73');
              setCb('#EF233C');
            }}
            className="rounded-[3px] border border-line2 px-2.5 py-1 text-[13px] text-ink hover:bg-bg4"
          >
            Ampel-Enden
          </button>
          <button
            type="button"
            onClick={() => {
              setCa('#004AFD');
              setCb('#F4EB06');
            }}
            className="rounded-[3px] border border-line2 px-2.5 py-1 text-[13px] text-ink hover:bg-bg4"
          >
            Blau → Gelb
          </button>
        </div>
        <div className="mt-5 overflow-x-auto">
          <table className="w-full min-w-[560px] border-collapse text-left">
            <thead>
              <tr>
                <th scope="col" className="label w-[210px] pb-2 font-semibold">Mischraum</th>
                <th scope="col" className="label pb-2 font-semibold">Verlauf</th>
                <th scope="col" className="label w-[96px] pb-2 text-right font-semibold" title="Buntheit in der Mitte relativ zu den Enden">Buntheit Mitte</th>
                <th scope="col" className="label w-[96px] pb-2 text-right font-semibold" title="Größte Abweichung von L* gegenüber der Geraden zwischen den Enden">L* krumm um</th>
              </tr>
            </thead>
            <tbody>
              {mixes.map((m) => {
                const rel = endChroma > 0.001 ? (chroma(m.fn(A, B, 0.5)) / endChroma) * 100 : 100;
                const dev = bend(m.fn);
                return (
                  <tr key={m.label} className="border-t border-line">
                    <th scope="row" className="py-2.5 pr-4 text-[13.5px] leading-snug font-normal">
                      <span className="text-ink">{m.label}</span>
                      <span className="block text-[12.5px] text-ink3">{m.hint}</span>
                    </th>
                    <td className="py-2.5">
                      <div aria-hidden className="h-9 rounded-[3px]" style={{ backgroundImage: gradient(A, B, m.fn) }} />
                    </td>
                    <td className={cn('num py-2.5 pl-4 text-right text-[13.5px]', rel < 60 ? 'text-warn' : 'text-ink')}>{rel.toFixed(0)} %</td>
                    <td className={cn('num py-2.5 pl-4 text-right text-[13.5px]', dev > 4 ? 'text-warn' : 'text-ink')}>{dev.toFixed(1)}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}
