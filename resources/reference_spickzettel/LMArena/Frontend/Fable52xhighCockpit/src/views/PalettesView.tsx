import { useMemo, useState } from 'react';
import { Chip, CopyHex, PageHead, Ramp, SectionHead, Segmented } from '../components/ui';
import { contrast, CVD_LABEL, deltaE, hexToRgb, onColor, simulateCvd, textTint, type Cvd } from '../lib/color';
import { getLut } from '../lib/colormaps';
import { CATEGORIES, CHASSIS, STATE_COLORS, TRIAD_GROUNDS, TRIADS, type ChassisId } from '../lib/palettes';

const KIND = { chassis: 'Chassis', akzent: 'Akzente', report: 'Report-Thema' } as const;

const ACCENTS = [
  ...CATEGORIES.map((c) => ({ hex: c.hex, name: c.colorName })),
  { hex: '#EF233C', name: 'Signal Red' },
  { hex: '#F4EB06', name: 'Sunshine Yellow' },
];

interface Pair {
  a: (typeof ACCENTS)[number];
  b: (typeof ACCENTS)[number];
  normal: number;
  worst: number;
  worstIn: Cvd;
}

function confusable(): Pair[] {
  const out: Pair[] = [];
  const kinds: Cvd[] = ['normal', 'deutan', 'protan', 'tritan'];
  for (let i = 0; i < ACCENTS.length; i++) {
    for (let j = i + 1; j < ACCENTS.length; j++) {
      const a = hexToRgb(ACCENTS[i].hex);
      const b = hexToRgb(ACCENTS[j].hex);
      let worst = Infinity;
      let worstIn: Cvd = 'normal';
      for (const k of kinds) {
        const d = deltaE(simulateCvd(a, k), simulateCvd(b, k));
        if (d < worst) {
          worst = d;
          worstIn = k;
        }
      }
      out.push({ a: ACCENTS[i], b: ACCENTS[j], normal: deltaE(a, b), worst, worstIn });
    }
  }
  return out.filter((p) => p.worst < 11).sort((x, y) => x.worst - y.worst);
}

const DEMO_MAPS = [
  { value: 'inferno', label: 'Inferno' },
  { value: 'viridis', label: 'Viridis' },
  { value: 'smooth-cool-warm', label: 'Cool Warm' },
  { value: 'ampel', label: 'Ampel' },
];

export default function PalettesView({ chassis }: { chassis: ChassisId }) {
  const ch = CHASSIS[chassis];
  const pairs = useMemo(confusable, []);
  const [demo, setDemo] = useState('inferno');

  return (
    <div className="space-y-14">
      <PageHead kicker="Designreferenz · Farben" title="Sechs Triaden, ein neutrales Chassis">
        <p>
          Zwei Triaden liefern das dunkle Chassis, die übrigen vier ihre lauten Töne als Kategorie- und Zustandsfarben. Die hellen
          Grundtöne bleiben für Report-Themen reserviert. So steht nie eine farbige Fläche neben einer Colormap.
        </p>
        <p className="text-[13.5px] text-ink3">
          <span className="num text-ink2">#11217</span> hatte im Briefing fünf Stellen und wird hier als{' '}
          <span className="num text-ink2">#111217</span> geführt. Falls ein anderer Wert gemeint war, bitte korrigieren.
        </p>
      </PageHead>

      {/* -------------------------------------------------------- Triaden ---- */}
      <section className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
        {TRIADS.map((t) => (
          <article key={t.id} className="overflow-hidden rounded-[6px] border border-line bg-bg2">
            <div className="grid h-[104px] grid-cols-3">
              {t.swatches.map((sw) => (
                <div
                  key={sw.hex}
                  className="flex items-end p-2 shadow-[inset_0_0_0_1px_rgba(255,255,255,0.05)]"
                  style={{ background: sw.hex, color: onColor(sw.hex) }}
                >
                  <span className="num text-[12.5px] leading-none">{sw.hex}</span>
                </div>
              ))}
            </div>
            <div className="p-3.5">
              <div className="flex flex-wrap items-center gap-x-2 gap-y-1">
                <span className="num text-[13px] text-ink3">{t.id}</span>
                <h3 className="text-[15.5px] leading-tight font-medium text-ink">{t.title}</h3>
                <Chip tone={t.kind === 'chassis' ? 'accent' : 'neutral'}>{KIND[t.kind]}</Chip>
              </div>
              <p className="mt-1.5 text-[13.5px] leading-snug text-ink2">{t.summary}</p>
              <ul className="mt-3 space-y-1 border-t border-line pt-2.5">
                {t.swatches.map((sw) => (
                  <li key={sw.hex} className="flex items-center gap-2 text-[13px]">
                    <span aria-hidden className="h-3 w-3 shrink-0 rounded-[2px] shadow-[inset_0_0_0_1px_rgba(255,255,255,0.12)]" style={{ background: sw.hex }} />
                    <span className="min-w-0 flex-1">
                      <span className="text-ink">{sw.name}</span>
                      <span className="text-ink3"> · {sw.role}</span>
                    </span>
                    <CopyHex hex={sw.hex} className="shrink-0" />
                  </li>
                ))}
              </ul>
            </div>
          </article>
        ))}
      </section>

      {/* ---------------------------------------------------------- Rollen ---- */}
      <section>
        <SectionHead title="Kategoriefarben im Block-Stack">
          <p>
            Jede Farbe steht für genau eine Physik-Kategorie. Der Kontrast ist gegen die Panel-Fläche des gewählten Chassis ({ch.name},{' '}
            <span className="num">{ch.panel}</span>) gerechnet. Wo ein Ton als Schrift zu dunkel ist, steht daneben die aufgehellte
            Variante mit gleichem Farbton.
          </p>
        </SectionHead>
        <div className="mt-5 overflow-x-auto rounded-[6px] border border-line">
          <table className="w-full min-w-[680px] border-collapse text-left text-[13.5px]">
            <thead>
              <tr className="bg-bg2 text-ink3">
                {['Kategorie', 'Farbe', 'Umfang', 'Kontrast', 'Als Schrift'].map((h) => (
                  <th key={h} scope="col" className="label px-3 py-2 font-semibold">
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {CATEGORIES.map((c) => {
                const ratio = contrast(hexToRgb(c.hex), hexToRgb(ch.panel));
                const tint = textTint(c.hex, ch.panel, 4.5);
                const okText = ratio >= 4.5;
                return (
                  <tr key={c.id} className="border-t border-line">
                    <th scope="row" className="px-3 py-2.5 font-medium text-ink">
                      <span className="flex items-center gap-2.5">
                        <span aria-hidden className="h-6 w-[4px] rounded-[1px]" style={{ background: c.hex }} />
                        {c.name}
                      </span>
                    </th>
                    <td className="px-3 py-2.5 whitespace-nowrap text-ink2">
                      {c.colorName} <CopyHex hex={c.hex} />
                    </td>
                    <td className="px-3 py-2.5 text-ink2">{c.scope}</td>
                    <td className="num px-3 py-2.5 whitespace-nowrap text-ink">
                      {ratio.toFixed(1)} : 1{' '}
                      <span className={ratio >= 3 ? 'text-ink3' : 'text-warn'}>{ratio >= 3 ? '' : '· unter 3 : 1'}</span>
                    </td>
                    <td className="px-3 py-2.5 whitespace-nowrap">
                      {okText ? (
                        <span style={{ color: c.hex }}>direkt nutzbar</span>
                      ) : (
                        <span className="inline-flex items-center gap-2">
                          <span style={{ color: tint }}>Tint</span>
                          <CopyHex hex={tint} />
                        </span>
                      )}
                    </td>
                  </tr>
                );
              })}
              <tr className="border-t border-line">
                <th scope="row" className="px-3 py-2.5 font-medium text-ink">
                  <span className="flex items-center gap-2.5">
                    <span aria-hidden className="h-6 w-[4px] rounded-[1px] bg-ink" />
                    Strömung
                  </span>
                </th>
                <td className="px-3 py-2.5 whitespace-nowrap text-ink2">
                  Cloud <CopyHex hex="#DFE5EB" />
                </td>
                <td className="px-3 py-2.5 text-ink2">Einlass, Auslass, Wände – Ergänzung: Basis-Strömung bleibt ohne Akzent</td>
                <td className="num px-3 py-2.5 text-ink">{contrast(hexToRgb('#DFE5EB'), hexToRgb(ch.panel)).toFixed(1)} : 1</td>
                <td className="px-3 py-2.5 text-ink">direkt nutzbar</td>
              </tr>
            </tbody>
          </table>
        </div>

        <ul className="mt-4 grid gap-2 sm:grid-cols-2 lg:grid-cols-4">
          {Object.entries(STATE_COLORS).map(([k, v]) => (
            <li key={k} className="flex items-center gap-2.5 rounded-[4px] border border-line bg-bg2 px-3 py-2">
              <span aria-hidden className="h-3.5 w-3.5 shrink-0 rounded-full" style={{ background: v.hex }} />
              <span className="min-w-0 text-[13px] leading-snug">
                <span className="block text-ink">{v.use}</span>
                <span className="text-ink3">{v.name}</span>
              </span>
            </li>
          ))}
        </ul>
      </section>

      {/* ------------------------------------------------ Verwechslungen ---- */}
      <section>
        <SectionHead title="Paare, die sich zu ähnlich sind">
          <p>
            Farbabstand ΔE in Oklab zwischen allen Akzenten, zusätzlich unter simulierter Farbsehschwäche. Unter etwa 10 lassen sich zwei
            Töne ohne direkten Vergleich nicht mehr sicher auseinanderhalten.
          </p>
        </SectionHead>
        <ul className="mt-5 grid gap-3 md:grid-cols-2">
          {pairs.map((p) => (
            <li key={p.a.hex + p.b.hex} className="flex gap-3 rounded-[6px] border border-line bg-bg2 p-3">
              <div className="flex shrink-0 overflow-hidden rounded-[4px]">
                <span className="h-12 w-8" style={{ background: p.a.hex }} />
                <span className="h-12 w-8" style={{ background: p.b.hex }} />
              </div>
              <div className="min-w-0 text-[13.5px] leading-snug">
                <p className="text-ink">
                  {p.a.name} / {p.b.name}
                </p>
                <p className="num mt-0.5 text-ink2">
                  ΔE {p.normal.toFixed(1)}
                  {p.worstIn !== 'normal' && (
                    <>
                      {' '}
                      · {CVD_LABEL[p.worstIn]} {p.worst.toFixed(1)}
                    </>
                  )}
                </p>
                <p className="mt-1 text-ink3">
                  {p.normal < 6
                    ? 'Praktisch derselbe Ton. Nie als zwei verschiedene Bedeutungen nebeneinander einsetzen; Zustände immer mit Icon und Text absichern.'
                    : p.normal < 11
                      ? 'Leicht zu verwechseln. Nur in klar getrennten Rollen verwenden.'
                      : `Für Normalsichtige eindeutig, unter ${CVD_LABEL[p.worstIn]} kritisch. Zusätzlich über Position oder Icon unterscheiden.`}
                </p>
              </div>
            </li>
          ))}
        </ul>
      </section>

      {/* ------------------------------------------------- Interferenz ---- */}
      <section>
        <div className="flex flex-wrap items-end justify-between gap-4">
          <SectionHead title="Warum das Chassis neutral bleibt">
            <p>
              Dieselbe Colormap auf den Grundtönen aller sechs Triaden. Die Rampe ist in jeder Kachel bytegleich – trotzdem wirken die
              Farben auf Blush oder Oxblood verschoben. Deshalb tragen nur T3 und T4 den Viewport.
            </p>
          </SectionHead>
          <Segmented label="Colormap" value={demo} onChange={setDemo} options={DEMO_MAPS} />
        </div>
        <div className="mt-5 grid grid-cols-2 gap-3 md:grid-cols-3">
          {TRIAD_GROUNDS.map((g) => {
            const neutral = g.id === 'T3' || g.id === 'T4';
            return (
              <figure key={g.id} className="overflow-hidden rounded-[6px] border border-line" style={{ background: g.hex }}>
                <div className="px-5 py-7 sm:px-7 sm:py-9">
                  <Ramp lut={getLut(demo)} className="h-9 rounded-[2px]" />
                </div>
                <figcaption className="flex items-center justify-between gap-2 px-3 pb-2.5 text-[12.5px]" style={{ color: onColor(g.hex) }}>
                  <span>
                    {g.id} · {g.name}
                  </span>
                  <span className="font-cond tracking-[0.06em] uppercase opacity-80">{neutral ? 'Chassis' : 'färbt ab'}</span>
                </figcaption>
              </figure>
            );
          })}
        </div>
      </section>
    </div>
  );
}
