import { useMemo, type ReactNode } from 'react';
import { ArrowRight } from 'lucide-react';
import type { Nav } from '../App';
import { Chip, PageHead, SectionHead } from '../components/ui';
import { contrast, deltaE, hexToRgb, lstar, mixOklab, rgbToOklab, simulateCvd, type RGB } from '../lib/color';
import { COLORMAPS, pickSignal } from '../lib/colormaps';

interface Agreed {
  title: string;
  text: string;
  live: boolean;
  action?: { label: string; run: () => void };
}

function Go({ onClick, children }: { onClick: () => void; children: ReactNode }) {
  return (
    <button type="button" onClick={onClick} className="mt-1.5 inline-flex items-center gap-1 rounded-[2px] text-[13px] text-accent-ink underline-offset-2 hover:underline">
      {children} <ArrowRight size={12} aria-hidden />
    </button>
  );
}

function AgreedList({ title, items }: { title: string; items: Agreed[] }) {
  return (
    <div>
      <h3 className="label text-[13px] text-ink2">{title}</h3>
      <ul className="mt-2">
        {items.map((it) => (
          <li key={it.title} className="border-t border-line py-3.5">
            <div className="flex flex-wrap items-center gap-x-2.5 gap-y-1">
              <p className="text-[15px] font-medium text-ink">{it.title}</p>
              <Chip tone={it.live ? 'ok' : 'neutral'}>{it.live ? 'im Cockpit sichtbar' : 'nur Rust-Build'}</Chip>
            </div>
            <p className="mt-1 max-w-[62ch] text-[14px] leading-snug text-ink2">{it.text}</p>
            {it.action && <Go onClick={it.action.run}>{it.action.label}</Go>}
          </li>
        ))}
      </ul>
    </div>
  );
}

export default function GuidelinesView({ nav }: { nav: Nav }) {
  const facts = useMemo(() => {
    const pink = COLORMAPS.filter((m) => pickSignal(m.id).hex === '#FC75B4').length;
    const panel = hexToRgb('#171920');
    const c = (h: string) => contrast(hexToRgb(h), panel).toFixed(1);
    return {
      pink,
      n: COLORMAPS.length,
      redRose: deltaE(hexToRgb('#EF233C'), hexToRgb('#E52150')).toFixed(1),
      blue: c('#004AFD'),
      violet: c('#723BFA'),
      mauve: c('#A5467C'),
      lGreen: lstar(hexToRgb('#009E73')).toFixed(0),
      lYellow: lstar(hexToRgb('#FEC40B')).toFixed(0),
      lRed: lstar(hexToRgb('#EF233C')).toFixed(0),
      blueViolet: deltaE(simulateCvd(hexToRgb('#004AFD'), 'protan'), simulateCvd(hexToRgb('#723BFA'), 'protan')).toFixed(1),
      midChroma: (() => {
        const ch = (x: RGB) => Math.hypot(rgbToOklab(x)[1], rgbToOklab(x)[2]);
        const g = hexToRgb('#009E73');
        const r = hexToRgb('#EF233C');
        return ((ch(mixOklab(g, r, 0.5)) / ((ch(g) + ch(r)) / 2)) * 100).toFixed(0);
      })(),
    };
  }, []);

  const maps: Agreed[] = [
    {
      title: 'Cool Warm mit festem Nullpunkt',
      text: 'Für jede Größe mit Vorzeichen. Im Inspector hält „Null in der Mitte halten“ Min und Max symmetrisch, sobald eine divergente Map aktiv ist.',
      live: true,
      action: { label: 'c_p mit Cool Warm ansehen', run: () => nav.openField('cp') },
    },
    {
      title: 'Turbo statt Jet',
      text: 'Der gewohnte Blau–Grün–Gelb–Rot-Look ohne die falschen Kanten des klassischen Regenbogens.',
      live: true,
      action: { label: 'Turbo im Cockpit', run: () => nav.openMap('turbo') },
    },
    {
      title: 'Schlieren / X-Ray',
      text: 'Perzeptuell lineare Graustufen für Gradienten und Wirbelkerne, per Schalter umkehrbar.',
      live: true,
      action: { label: 'Numerisches Schlieren ansehen', run: () => nav.openField('schlieren') },
    },
    {
      title: 'Iso-Bänder und NaN-Signal',
      text: 'Ein Schalter quantisiert jede Map in 10, 16 oder 24 Bänder. NaN-Zellen leuchten in einer Signalfarbe, die in der Map nicht vorkommt.',
      live: true,
      action: { label: 'Im Inspector ausprobieren', run: () => nav.go('cockpit') },
    },
  ];

  const gpu: Agreed[] = [
    {
      title: 'MatCap oder Unlit Scientific',
      text: 'Mit aktivem Skalarfeld gibt es weder Licht noch Tone-Mapping: Schaufel und Legende lesen dieselbe LUT, nichts verändert den Wert dazwischen. Die Form deutet nur eine Silhouette an.',
      live: true,
      action: { label: 'Mit der Probe nachprüfen', run: () => nav.openField('tw') },
    },
    {
      title: 'LUT in Oklab vorgebacken',
      text: '256 Texel je Map, Abtastung an der Texelmitte, Bänderung vor dem Lookup. Oklab sorgt für gleich große Schritte – gegen graue Mitten hilft es allein nicht, siehe Vorschlag 02.',
      live: true,
      action: { label: 'Vergleich ansehen', run: () => nav.go('colormaps') },
    },
    {
      title: 'Rendern nur bei Bedarf',
      text: 'Gezeichnet wird ausschließlich nach einer Änderung. Der Frame-Zähler unten links im Viewport steht still, solange nichts passiert.',
      live: true,
    },
    {
      title: 'Tabellarische Ziffern, Einheiten im Feld',
      text: 'Alle Zahlen laufen mit fester Ziffernbreite. Eingaben verstehen Einheiten und gehen SI-normiert ans Backend.',
      live: true,
    },
    {
      title: 'Kamera-relative f64 → f32 und Reverse-Z',
      text: 'Gitterkoordinaten bleiben in f64, vor dem Upload wird die Kameraposition abgezogen; Tiefe als Depth32Float mit Reverse-Z. Betrifft nur den nativen Renderer und ist hier nicht nachgebaut.',
      live: false,
    },
  ];

  const extra: { title: string; text: string; fact?: string }[] = [
    {
      title: 'NaN-Farbe je Colormap wählen',
      text: 'Eine feste Signalfarbe kollidiert mit manchen Maps. Der Loader nimmt je Map den Kandidaten mit dem größten Abstand zur LUT. Acid Lime bleibt in der Oberfläche das Zeichen für „in Ordnung“ und erscheint als NaN-Farbe nur im Viewport.',
      fact: `Vibrant Pink gewinnt bei ${facts.pink} von ${facts.n} Maps, Acid Lime bei ${facts.n - facts.pink}.`,
    },
    {
      title: 'Gegenfarben brauchen eine Stützstelle',
      text: 'Die Annahme, Oklab verhindere graue Übergänge zwischen Grün und Rot, hält der Rechnung nicht stand: Jede gerade Mischung von Gegenfarben läuft Richtung Grau, in jedem Farbraum. Maps mit Gegenfarben an den Enden brauchen eine Zwischenfarbe – wie Gelb in der Ampel.',
      fact: `Grün → Rot direkt in Oklab: nur ${facts.midChroma} % Buntheit in der Mitte.`,
    },
    {
      title: 'Kategorien nie nur über Farbe trennen',
      text: 'Electric Blue (Geometrie) und Electric Violet (Numerik) fallen bei Protanopie zusammen. Jeder Block trägt deshalb zusätzlich Icon und Namen; die Farbe ist nur der schnelle Anker.',
      fact: `Farbabstand unter Protanopie: ΔE ${facts.blueViolet}.`,
    },
    {
      title: 'Fehler nie nur über Farbe melden',
      text: 'Signal Red und Rose Red sind praktisch derselbe Ton. Ein Verbrennungs-Block und eine Fehlermeldung lassen sich farblich nicht trennen – Zustände brauchen immer Icon und Text.',
      fact: `Farbabstand ΔE ${facts.redRose}; sicher unterscheidbar ab etwa 10.`,
    },
    {
      title: 'Akzente als Schrift aufhellen',
      text: 'Electric Blue, Electric Violet und Mauve sind als Balken und Fläche richtig, als Text auf dem Chassis aber zu dunkel. Im Tab Farben steht je ein aufgehellter Tint mit gleichem Farbton.',
      fact: `Kontrast auf dem Panel: ${facts.blue} : 1, ${facts.violet} : 1, ${facts.mauve} : 1 – Text braucht 4.5 : 1.`,
    },
    {
      title: 'Bereichsüberschreitung als Muster',
      text: 'Werte außerhalb des Legendenbereichs werden schraffiert statt umgefärbt. Das funktioniert mit jeder Map und bei jeder Farbsehschwäche.',
    },
    {
      title: 'Ampel nur gebändert einsetzen',
      text: 'Grün und Rot sind fast gleich hell, Gelb deutlich heller. Stufenlos entsteht ein heller Wulst in der Mitte; mit Bändern und festen Schwellen bleibt die Ampel eindeutig.',
      fact: `L* Grün ${facts.lGreen}, Gelb ${facts.lYellow}, Rot ${facts.lRed}.`,
    },
    {
      title: 'Zelldaten flach, Knotendaten interpoliert',
      text: 'Qualität und NaN gehören zur Zelle. Wird interpoliert, verschwindet eine einzelne schlechte Zelle zwischen guten Nachbarn. Dieser Web-Prototyp interpoliert noch; der Rust-Renderer sollte Zellfelder flach zeichnen.',
    },
    {
      title: 'Keine Transparenz auf Skalarflächen',
      text: 'Alpha-Blending erzeugt Mischfarben, die auf der Legende nicht vorkommen. Besser ausblenden, schneiden oder rastern.',
    },
    {
      title: 'Die Legende gehört ins Bild',
      text: 'Jeder Export trägt Feld, Einheit, Bereich, Colormap und Zeitschritt. Sonst ist ein Screenshot nach einer Woche nicht mehr lesbar.',
    },
    {
      title: 'Feste Bereiche für Vergleiche',
      text: 'Ein Auto-Bereich pro Zeitschritt lässt Bilder gleich aussehen, die es nicht sind. Standard: Bereich je Größe im Fall speichern, Auto nur auf Knopfdruck.',
    },
    {
      title: 'Schriften mit freier Lizenz einbetten',
      text: 'Barlow und JetBrains Mono stehen unter der OFL und lassen sich als TTF in egui laden. Die Mono-Schrift bringt Griechisch für ρ, α, Δ mit.',
    },
    {
      title: 'Helle Themen nur für Berichte',
      text: 'T1 und T5 eignen sich für Druck und PDF. Maps mit weißem Ende (Grün–Weiß, Aero, Schlieren) brauchen dort eine Kontur um die Legende.',
    },
    {
      title: 'Farbformat festschreiben',
      text: 'palettes.json enthält sRGB-Hex. Der Loader muss sie als sRGB lesen und die LUT-Textur entsprechend kennzeichnen, sonst wird doppelt oder gar nicht kodiert.',
    },
  ];

  const open: { text: ReactNode; action?: { label: string; run: () => void } }[] = [
    {
      text: (
        <>
          <strong className="font-medium text-ink">„Aero“</strong> war nicht näher bestimmt. Der Vorschlag läuft von Nachtblau über Electric
          Blue nach Cloud. Passt die Richtung?
        </>
      ),
      action: { label: 'Aero ansehen', run: () => nav.openMap('aero') },
    },
    {
      text: (
        <>
          <span className="num text-ink">#11217</span> wird als <span className="num text-ink">#111217</span> geführt. Stimmt das?
        </>
      ),
    },
    {
      text: <>Dark Ember und Fire: Die Zwischenstufen sind rekonstruiert und sollten mit den Originalbildern abgeglichen werden.</>,
      action: { label: 'Zu den Paletten', run: () => nav.go('referenzen') },
    },
    {
      text: <>Die Originale der beiden UI-Screenshots fehlen hier und lassen sich im Tab Referenzen ablegen.</>,
      action: { label: 'Screenshots ablegen', run: () => nav.go('referenzen') },
    },
  ];

  return (
    <div className="space-y-14">
      <PageHead kicker="Designreferenz · Leitplanken" title="Was vereinbart ist und worauf wir zusätzlich achten sollten">
        <p>
          Oben steht, was aus dem bisherigen Austausch übernommen und im Cockpit bereits zu sehen ist. Darunter folgen {extra.length} weitere
          Vorschläge – einige mit Zahlen, die diese Seite direkt aus den Paletten rechnet.
        </p>
      </PageHead>

      <section>
        <SectionHead title="Vereinbart" />
        <div className="mt-5 grid gap-x-10 gap-y-8 lg:grid-cols-2">
          <AgreedList title="Colormap-Ergänzungen" items={maps} />
          <AgreedList title="Regeln für den Renderer" items={gpu} />
        </div>
      </section>

      <section>
        <SectionHead title="Zusätzlich vorgeschlagen" />
        <ol className="mt-5 grid gap-x-10 md:grid-cols-2">
          {extra.map((e, i) => (
            <li key={e.title} className="flex gap-4 border-t border-line py-4">
              <span className="num w-[2ch] shrink-0 pt-[2px] text-[13px] text-ink3">{String(i + 1).padStart(2, '0')}</span>
              <div className="min-w-0">
                <h3 className="text-[15.5px] leading-snug font-medium text-ink">{e.title}</h3>
                <p className="mt-1 text-[14px] leading-snug text-ink2">{e.text}</p>
                {e.fact && <p className="num mt-1.5 text-[12.5px] leading-snug text-accent-ink">{e.fact}</p>}
              </div>
            </li>
          ))}
        </ol>
      </section>

      <section>
        <SectionHead title="Braucht deine Bestätigung" />
        <ul className="mt-4 max-w-[78ch]">
          {open.map((o, i) => (
            <li key={i} className="border-t border-line py-3.5 text-[14.5px] leading-snug text-ink2">
              <p>{o.text}</p>
              {o.action && <Go onClick={o.action.run}>{o.action.label}</Go>}
            </li>
          ))}
        </ul>
      </section>
    </div>
  );
}
