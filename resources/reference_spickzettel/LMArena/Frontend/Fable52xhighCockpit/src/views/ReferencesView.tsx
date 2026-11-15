import { useCallback, useEffect, useId, useRef, useState, type DragEvent, type ReactNode } from 'react';
import { ArrowRight, ImagePlus, Trash2 } from 'lucide-react';
import type { Nav } from '../App';
import LPlot from '../components/LPlot';
import { Chip, CopyHex, PageHead, Ramp } from '../components/ui';
import { analyze, COLORMAPS, getLut, MAP_BY_ID } from '../lib/colormaps';
import { cn } from '../utils/cn';

// --------------------------------------------------- Bildablage (IndexedDB) ----

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open('cockpit-refs', 1);
    req.onupgradeneeded = () => req.result.createObjectStore('img');
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
async function tx<T>(mode: IDBTransactionMode, run: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  const db = await openDb();
  return new Promise<T>((resolve, reject) => {
    const req = run(db.transaction('img', mode).objectStore('img'));
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function useRefImage(key: string) {
  const [url, setUrl] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const current = useRef<string | null>(null);
  const show = useCallback((blob: Blob | null) => {
    if (current.current) URL.revokeObjectURL(current.current);
    current.current = blob ? URL.createObjectURL(blob) : null;
    setUrl(current.current);
  }, []);

  useEffect(() => {
    let alive = true;
    tx<Blob | undefined>('readonly', (s) => s.get(key)).then(
      (b) => alive && b && show(b),
      () => undefined,
    );
    return () => {
      alive = false;
      if (current.current) URL.revokeObjectURL(current.current);
      current.current = null;
    };
  }, [key, show]);

  const put = useCallback(
    (file: File | undefined) => {
      if (!file) return;
      if (!file.type.startsWith('image/')) return setError('Das ist keine Bilddatei. Erlaubt sind JPG, PNG oder WebP.');
      setError(null);
      show(file);
      tx('readwrite', (s) => s.put(file, key)).catch(() => setError('Bild wird angezeigt, konnte aber nicht dauerhaft gespeichert werden.'));
    },
    [key, show],
  );
  const clear = useCallback(() => {
    show(null);
    setError(null);
    tx('readwrite', (s) => s.delete(key)).catch(() => undefined);
  }, [key, show]);

  return { url, error, put, clear };
}

// ---------------------------------------------------------------- Bausteine ----

function Marker({ n, className }: { n: number; className?: string }) {
  return (
    <span
      className={cn('num grid h-[20px] w-[20px] place-items-center rounded-full text-[12px] leading-none font-semibold text-[#0A0B0F]', className)}
      style={{ background: '#F4EB06' }}
    >
      {n}
    </span>
  );
}

function UiRef({
  file,
  title,
  schema,
  points,
  nav,
}: {
  file: string;
  title: string;
  schema: ReactNode;
  points: { t: string; d: string }[];
  nav: Nav;
}) {
  const img = useRefImage(file);
  const [over, setOver] = useState(false);
  const inputId = useId();
  const onDrop = (e: DragEvent) => {
    e.preventDefault();
    setOver(false);
    img.put(e.dataTransfer.files?.[0]);
  };
  return (
    <article className="grid gap-x-8 gap-y-5 border-t border-line py-8 lg:grid-cols-[minmax(0,1.15fr)_minmax(0,1fr)]">
      <div className="min-w-0">
        <div
          onDragOver={(e) => {
            e.preventDefault();
            setOver(true);
          }}
          onDragLeave={() => setOver(false)}
          onDrop={onDrop}
          className={cn('relative overflow-hidden rounded-[6px] border bg-bg0', over ? 'border-accent-ink' : 'border-line')}
        >
          {img.url ? (
            <img src={img.url} alt={`Referenz-Screenshot ${file}`} className="block max-h-[520px] w-full object-contain" />
          ) : (
            schema
          )}
          {over && (
            <div className="absolute inset-0 grid place-items-center bg-bg0/85 text-[14px] text-ink">Loslassen, um das Original abzulegen</div>
          )}
        </div>
        <div className="mt-2.5 flex flex-wrap items-center gap-x-3 gap-y-2">
          <label
            htmlFor={inputId}
            className="inline-flex min-h-[32px] cursor-pointer items-center gap-1.5 rounded-[3px] border border-line2 px-2.5 text-[13px] text-ink hover:bg-bg4 has-[:focus-visible]:outline has-[:focus-visible]:outline-2 has-[:focus-visible]:outline-accent-ink"
          >
            <ImagePlus size={14} aria-hidden />
            {img.url ? 'Anderes Bild wählen' : 'Original-Screenshot ablegen oder wählen'}
            <input
              id={inputId}
              type="file"
              accept="image/*"
              className="sr-only"
              onChange={(e) => {
                img.put(e.target.files?.[0]);
                e.target.value = '';
              }}
            />
          </label>
          {img.url && (
            <button type="button" onClick={img.clear} className="inline-flex min-h-[32px] items-center gap-1.5 rounded-[3px] px-2 text-[13px] text-ink2 hover:bg-bg4 hover:text-ink">
              <Trash2 size={13} aria-hidden /> Entfernen
            </button>
          )}
          <span className="text-[12.5px] text-ink3">{img.url ? 'Liegt nur in diesem Browser.' : 'Bis dahin steht hier das Schema.'}</span>
        </div>
        {img.error && (
          <p role="alert" className="mt-1.5 text-[13px] text-bad-ink">
            {img.error}
          </p>
        )}
      </div>

      <div className="min-w-0">
        <div className="flex flex-wrap items-center gap-2">
          <span className="num text-[12.5px] text-ink3">{file}</span>
          <Chip tone={img.url ? 'ok' : 'warn'}>{img.url ? 'Original geladen' : 'Original fehlt'}</Chip>
        </div>
        <h2 className="display mt-1.5 text-[26px] text-ink">{title}</h2>
        <ol className="mt-4 space-y-3">
          {points.map((p, i) => (
            <li key={p.t} className="flex gap-3">
              <Marker n={i + 1} className="mt-[1px] shrink-0" />
              <p className="text-[14px] leading-snug">
                <span className="font-medium text-ink">{p.t}</span>
                <span className="block text-ink2">{p.d}</span>
              </p>
            </li>
          ))}
        </ol>
        <button
          type="button"
          onClick={() => nav.go('cockpit')}
          className="mt-5 inline-flex min-h-[32px] items-center gap-1.5 rounded-[3px] border border-line2 px-2.5 text-[13px] text-ink hover:bg-bg4"
        >
          Umsetzung im Cockpit <ArrowRight size={13} aria-hidden />
        </button>
      </div>
    </article>
  );
}

const BAR = ['#004AFD', '#B9F500', '#DFE5EB', '#FEC40B', '#E52150', '#723BFA'];

const NtopSchema = (
  <div className="relative flex aspect-[16/10] bg-[#14161b]" role="img" aria-label="Schema: links ein Stapel farbcodierter Blöcke, rechts ein grauer MatCap-Körper im Viewport">
    <div className="flex w-[32%] flex-col gap-[4%] border-r border-white/10 p-[3%]">
      {BAR.map((c, i) => (
        <div key={c} className="flex-1 rounded-[2px] bg-white/[0.06]" style={{ borderLeft: `3px solid ${c}`, opacity: i > 3 ? 0.55 : 1 }}>
          <div className="mt-[9%] ml-[8%] h-[18%] w-[52%] rounded-[1px] bg-white/25" />
          <div className="mt-[6%] ml-[8%] h-[14%] w-[34%] rounded-[1px] bg-white/10" />
        </div>
      ))}
    </div>
    <div className="relative flex-1">
      <div
        className="absolute top-[14%] left-[26%] h-[72%] w-[40%] -rotate-[14deg]"
        style={{
          borderRadius: '58% 42% 48% 52% / 62% 60% 40% 38%',
          background: 'radial-gradient(circle at 34% 26%, #e3e6ea 0%, #9aa0a9 34%, #4b5059 68%, #23262c 100%)',
        }}
      />
      <div className="absolute right-[5%] bottom-[7%] h-[9%] w-[26%] rounded-[2px] border border-white/15 bg-white/[0.05]">
        <div className="mt-[9%] ml-[8%] h-[38%] w-[46%] rounded-[1px] bg-white/30" />
      </div>
    </div>
    <Marker n={1} className="absolute top-[6%] left-[27%]" />
    <Marker n={2} className="absolute top-[18%] left-[60%]" />
    <Marker n={3} className="absolute right-[2.5%] bottom-[17%]" />
  </div>
);

const BlenderSchema = (
  <div className="relative flex aspect-[16/10] flex-col bg-[#14161b]" role="img" aria-label="Schema: Workspace-Tabs oben, Viewport in der Mitte, Outliner und Inspector rechts, Zeitleiste unten">
    <div className="flex h-[9%] items-end gap-[1.2%] border-b border-white/10 px-[2%]">
      {[1, 0, 0, 0, 0].map((on, i) => (
        <div key={i} className={cn('h-[62%] w-[9%] rounded-t-[2px]', on ? 'bg-white/25' : 'bg-white/[0.07]')} />
      ))}
    </div>
    <div className="flex min-h-0 flex-1">
      <div className="relative flex-1 overflow-hidden">
        <div
          className="absolute inset-0 opacity-40"
          style={{
            backgroundImage:
              'linear-gradient(rgba(255,255,255,0.08) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,0.08) 1px, transparent 1px)',
            backgroundSize: '9% 14%',
          }}
        />
        <div className="absolute top-[24%] left-[34%] h-[46%] w-[22%] rounded-[3px] bg-gradient-to-br from-[#aeb3bb] to-[#3a3e46]" />
      </div>
      <div className="flex w-[27%] flex-col border-l border-white/10">
        <div className="h-[38%] space-y-[7%] border-b border-white/10 p-[7%]">
          {[70, 52, 60, 44].map((w) => (
            <div key={w} className="h-[10%] rounded-[1px] bg-white/15" style={{ width: `${w}%` }} />
          ))}
        </div>
        <div className="flex-1 space-y-[6%] p-[7%]">
          {[0, 1, 2, 3].map((i) => (
            <div key={i} className="flex h-[11%] gap-[6%]">
              <div className="w-[38%] rounded-[1px] bg-white/10" />
              <div className="flex-1 rounded-[1px] bg-white/[0.18]" />
            </div>
          ))}
        </div>
      </div>
    </div>
    <div className="relative h-[15%] border-t border-white/10">
      <div className="absolute inset-x-[3%] top-1/2 h-px bg-white/20" />
      {Array.from({ length: 13 }, (_, i) => (
        <div key={i} className="absolute top-[34%] h-[32%] w-px bg-white/25" style={{ left: `${5 + i * 7.4}%` }} />
      ))}
      <div className="absolute top-[14%] h-[72%] w-[2px] bg-[#84A6FF]" style={{ left: '64%' }} />
    </div>
    <Marker n={1} className="absolute top-[1.5%] left-[13%]" />
    <Marker n={2} className="absolute top-[14%] right-[3%]" />
    <Marker n={3} className="absolute bottom-[3%] left-[67%]" />
    <Marker n={4} className="absolute top-[40%] left-[5%]" />
  </div>
);

function PaletteRef({ file, mapId, cols, nav }: { file: string; mapId: string; cols: string; nav: Nav }) {
  const m = MAP_BY_ID[mapId];
  return (
    <article className="border-t border-line py-8">
      <div className="flex flex-wrap items-center gap-2">
        <span className="num text-[12.5px] text-ink3">{file}</span>
        <Chip tone="accent">aus den Werten nachgebaut</Chip>
      </div>
      <h2 className="display mt-1.5 text-[26px] text-ink">
        {m.name} · {m.stops.length} Stufen
      </h2>
      <ul className={cn('mt-4 grid gap-x-1 gap-y-3', cols)}>
        {m.stops.map(([, hex]) => (
          <li key={hex} className="min-w-0">
            <div className="h-16 rounded-[3px] shadow-[inset_0_0_0_1px_rgba(255,255,255,0.06)]" style={{ background: hex }} />
            <CopyHex hex={hex} className="mt-1 -ml-1 text-[11.5px]" />
          </li>
        ))}
      </ul>
      <div className="mt-4 grid items-center gap-x-4 gap-y-1.5 sm:grid-cols-[150px_minmax(0,1fr)]">
        <p className="text-[13px] text-ink3">Daraus gebackene LUT</p>
        <Ramp lut={getLut(mapId)} className="h-5 rounded-[2px]" />
      </div>
      <p className="mt-3 max-w-[78ch] text-[14px] leading-snug text-ink2">{m.note}</p>
      <button
        type="button"
        onClick={() => nav.openMap(mapId)}
        className="mt-4 inline-flex min-h-[32px] items-center gap-1.5 rounded-[3px] border border-line2 px-2.5 text-[13px] text-ink hover:bg-bg4"
      >
        Als Colormap im Cockpit <ArrowRight size={13} aria-hidden />
      </button>
    </article>
  );
}

export default function ReferencesView({ nav }: { nav: Nav }) {
  return (
    <div>
      <PageHead kicker="Designreferenz · Referenzbilder" title="Fünf Vorlagen und was wir daraus übernehmen">
        <p>
          Die fünf Bilddateien selbst sind in diesem Projekt nicht angekommen. Die beiden Paletten und der Helligkeitsplot sind deshalb aus
          ihren Werten live nachgebaut. Für die zwei Screenshots zeigt die Seite ein Schema der übernommenen Muster; das Original lässt sich
          pro Karte ablegen und bleibt in diesem Browser gespeichert.
        </p>
      </PageHead>

      <div className="mt-10">
        <UiRef
          file="ui_ref_ntop_blocks.jpg"
          title="nTop · Block-Stack und MatCap-Viewport"
          schema={NtopSchema}
          nav={nav}
          points={[
            { t: 'Block-Stack links', d: 'Jeder Physik-Schritt ist ein Block mit Kategoriefarbe am Rand – einklappbar und überbrückbar.' },
            { t: 'MatCap-Viewport', d: 'Bauteil wirkt plastisch ohne Lampen-Setup. Cavity und Eye-Dome-Lighting kommen im Rust-Build dazu.' },
            { t: 'Einheiten im Eingabefeld', d: '„12 mm“, „850 C“, „3 bar“ werden direkt verstanden und SI-normiert weitergegeben.' },
          ]}
        />
        <UiRef
          file="ui_ref_blender_dark.jpg"
          title="Blender · Workspaces, Outliner, Inspector"
          schema={BlenderSchema}
          nav={nav}
          points={[
            { t: 'Workspace-Tabs oben', d: 'Ein Arbeitsbereich je Aufgabe – hier Cockpit, Referenzen, Farben, Colormaps, Leitplanken, Schnittstellen.' },
            { t: 'Inspector rechts', d: 'Auswahl oben, Eigenschaften darunter; dichte Zeilen, klare Gruppen.' },
            { t: 'Zeitleiste unten', d: 'Geschriebene Zeitschritte als Marken, der geladene Stand als Playhead.' },
            { t: 'Neutrales dunkles Chassis', d: 'Keine Farbe im Rahmen, damit der Viewport die einzige farbige Fläche ist.' },
          ]}
        />
        <PaletteRef file="cmap_ref_dark_ember_10step.png" mapId="dark-ember" cols="grid-cols-5 lg:grid-cols-10" nav={nav} />
        <PaletteRef file="cmap_ref_fire_scheme.png" mapId="fire" cols="grid-cols-3 sm:grid-cols-6" nav={nav} />

        <article className="border-t border-line py-8">
          <div className="flex flex-wrap items-center gap-2">
            <span className="num text-[12.5px] text-ink3">cmap_ref_perceptual_lightness.png</span>
            <Chip tone="accent">live aus unseren LUTs gerechnet</Chip>
          </div>
          <h2 className="display mt-1.5 text-[26px] text-ink">Helligkeit L* für alle Maps</h2>
          <p className="mt-2 max-w-[78ch] text-[14.5px] leading-[1.55] text-ink2">
            Derselbe Plot-Typ wie im{' '}
            <a href="https://github.com/jrwrigh/cfdpost_colormaps" target="_blank" rel="noreferrer">
              jrwrigh-Repo
            </a>
            : jeder Punkt ist eine Stelle der Colormap, die Höhe ihre wahrgenommene Helligkeit. Eine gerade Linie heißt, dass gleiche
            Werteschritte gleich große Helligkeitsschritte ergeben. Gezeichnet wird, was der Viewport tatsächlich abtastet.
          </p>
          <ul className="mt-5 grid grid-cols-2 gap-x-4 gap-y-5 md:grid-cols-3 xl:grid-cols-4">
            {COLORMAPS.map((m) => {
              const a = analyze(m.id);
              return (
                <li key={m.id} className="min-w-0">
                  <LPlot id={m.id} className="block w-full" />
                  <p className="mt-1 text-[13px] leading-tight text-ink">{m.name}</p>
                  <p className={cn('text-[12px] leading-tight', a.ok ? 'text-ink3' : 'text-warn')}>{a.verdict}</p>
                </li>
              );
            })}
          </ul>
        </article>
      </div>
    </div>
  );
}
