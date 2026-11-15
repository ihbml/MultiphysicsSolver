import { useEffect, useState, type ReactNode } from 'react';
import { ArrowRight, Check, ClipboardCopy } from 'lucide-react';
import type { Nav } from '../App';
import { Sym } from '../components/Legend';
import { Chip, PageHead, SectionHead } from '../components/ui';
import { cn } from '../utils/cn';

function usePersist<T>(key: string, initial: T): [T, (v: T) => void] {
  const [v, setV] = useState<T>(() => {
    try {
      const raw = window.localStorage.getItem(key);
      return raw ? (JSON.parse(raw) as T) : initial;
    } catch {
      return initial;
    }
  });
  useEffect(() => {
    try {
      window.localStorage.setItem(key, JSON.stringify(v));
    } catch {
      /* ohne Speicher gilt die Auswahl nur für diese Sitzung */
    }
  }, [key, v]);
  return [v, setV];
}

const Code = ({ children }: { children: ReactNode }) => <code className="num rounded-[2px] bg-bg3 px-1 py-[1px] text-[0.9em] text-ink">{children}</code>;

const CHECKS = [
  {
    id: 'rust',
    kind: 'Pflicht',
    title: 'Rust-Toolchain auf Windows',
    text: (
      <>
        <Code>cargo</Code> ist im Standard-PATH noch nicht sichtbar. Einmalig <Code>winget install Rustlang.Rustup</Code>; die MSVC-Linker-Tools
        sollten durch die C++-Mesher-DLL schon vorhanden sein.
      </>
    ),
  },
  {
    id: 'crates',
    kind: 'Pflicht',
    title: 'Erster cargo build mit Internetzugriff',
    text: (
      <>
        Lädt einmal <Code>wgpu</Code>, <Code>winit</Code>, <Code>egui</Code>, <Code>egui_dock</Code>, <Code>egui_plot</Code>, <Code>glam</Code>,{' '}
        <Code>bytemuck</Code> und <Code>serde_json</Code> in den Cargo-Cache. Danach baut alles lokal.
      </>
    ),
  },
  {
    id: 'geo',
    kind: 'vorhanden',
    title: 'Testgeometrie',
    text: (
      <>
        <Code>BladeRev1CFD.obj</Code> und <Code>TestCAD.stl</Code> liegen bereits im Projekt. Damit rendert schon der erste Build eine echte Schaufel.
      </>
    ),
  },
  {
    id: 'case',
    kind: 'optional',
    title: 'Kleiner OpenFOAM-Testcase in cases/',
    text: (
      <>
        5 000 bis 30 000 Zellen aus dem Hexcore-Mesher mit den Feldern <Code>U</Code>, <Code>p</Code>, <Code>T</Code>. Bis dahin überbrückt ein
        eingebautes Mini-Testgitter.
      </>
    ),
  },
] as const;

const BASE = {
  c1: 'C1 – wgpu + egui direkt (egui_dock, egui_plot)',
  c2: 'C2 – Bevy + bevy_egui',
} as const;
const LEGACY = {
  archive: 'nach frontend/_legacy_trame/ archivieren',
  delete: 'löschen',
} as const;

export default function InterfacesView({ nav }: { nav: Nav }) {
  const [done, setDone] = usePersist<Record<string, boolean>>('cockpit.checks', { geo: true });
  const [base, setBase] = usePersist<keyof typeof BASE>('cockpit.base', 'c1');
  const [legacy, setLegacy] = usePersist<keyof typeof LEGACY>('cockpit.legacy', 'archive');
  const [copied, setCopied] = useState(false);
  useEffect(() => {
    if (!copied) return;
    const t = window.setTimeout(() => setCopied(false), 1600);
    return () => window.clearTimeout(t);
  }, [copied]);

  const nDone = CHECKS.filter((c) => done[c.id]).length;
  const answer = [
    'Entscheidungen für den Rough-Build:',
    `1) Unterbau: ${BASE[base]}`,
    `2) Trame-Prototyp (frontend/cockpit/app.py): ${LEGACY[legacy]}; neues Rust-Projekt unter frontend/cockpit_rs/`,
    `Voraussetzungen: ${CHECKS.map((c) => `${c.title} ${done[c.id] ? '✓' : '–'}`).join(' · ')}`,
  ].join('\n');

  const rows: { area: string; sends: ReactNode; needs: ReactNode; go?: { label: string; run: () => void } }[] = [
    {
      area: 'CAD & Domain',
      sends: 'Import-Parameter, Toleranzen, Patch-Gruppierung',
      needs: 'Dreiecke und Face-Group-IDs als flache Puffer; Segmente offener Kanten und Lecks zum Anleuchten am Bauteil',
      go: { label: 'Block CAD-Import', run: () => nav.go('cockpit') },
    },
    {
      area: 'Meshing',
      sends: (
        <>
          Octree-Hexcore- und DistMesh-Parameter, Grenzschicht (<Sym s="N_layers" />, Δy₁), Cut-Cell-Gewichte
        </>
      ),
      needs: (
        <>
          Zellqualität je Zelle: <Code>skewness</Code>, <Code>nonOrthogonality</Code>, <Code>amips</Code>, <Code>is_folded</Code>
        </>
      ),
      go: { label: 'Zellqualität ansehen', run: () => nav.openField('nonortho') },
    },
    {
      area: 'Physik-Blöcke & Randbedingungen',
      sends: 'Blockwerte, strikt in SI',
      needs: (
        <>
          <Code>export_ui_schema()</Code> mit <Code>visible_if</Code>-Regeln, SI-Dimension je Feld und Kategorie-Tag für die Blockfarbe
        </>
      ),
      go: { label: 'Block-Stack', run: () => nav.go('cockpit') },
    },
    {
      area: 'Solver & Live-Stream',
      sends: (
        <>
          <Code>start</Code>, <Code>write_now</Code>, <Code>stop_graceful</Code>, <Code>abort</Code>
        </>
      ),
      needs: 'NDJSON-Paket je Iteration: Residuen, Courant-Zahl, Zähler der NaN-Zellen',
      go: { label: 'Solver-Panel', run: () => nav.go('cockpit') },
    },
  ];

  return (
    <div className="space-y-14">
      <PageHead kicker="FRONTEND_REQUIREMENTS_ON_BACKEND · v0.2.0" title="Was das Cockpit vom Backend braucht">
        <p>
          Option C steht: natives Cockpit in Rust mit wgpu auf DX12/Vulkan und egui. Diese Seite fasst die ersten Schnittstellen, die Eingaben
          aus der Oberfläche und die Voraussetzungen für einen groben ersten Build zusammen. Gebaut wird noch nichts.
        </p>
      </PageHead>

      {/* ------------------------------------------------------------ Kanäle ---- */}
      <section>
        <SectionHead title="Zwei Kanäle" />
        <div className="mt-5 grid gap-4 md:grid-cols-2">
          <article className="rounded-[6px] border border-line bg-bg2 p-4">
            <div className="flex items-center gap-2.5">
              <span aria-hidden className="h-7 w-[4px] rounded-[1px]" style={{ background: '#004AFD' }} />
              <h3 className="text-[17px] font-medium text-ink">Kanal A · Zero-Copy 3D-Daten</h3>
            </div>
            <ul className="mt-3 space-y-2 text-[14px] leading-snug text-ink2">
              <li>
                Direktes Crate-Linking gegen <Code>foam_fast_io</Code> – das <Code>rlib</Code> ist dort schon vorbereitet.
              </li>
              <li>Flache Binärpuffer für CAD-Dreiecke und Face-Group-IDs, damit Flächen im Viewport anklickbar werden.</li>
              <li>Gitter und Felder gehen ohne Zwischenkopie in die GPU-Puffer.</li>
            </ul>
          </article>
          <article className="rounded-[6px] border border-line bg-bg2 p-4">
            <div className="flex items-center gap-2.5">
              <span aria-hidden className="h-7 w-[4px] rounded-[1px]" style={{ background: '#723BFA' }} />
              <h3 className="text-[17px] font-medium text-ink">Kanal B · Steuerung und Telemetrie</h3>
            </div>
            <ul className="mt-3 space-y-2 text-[14px] leading-snug text-ink2">
              <li>Lokaler JSON-RPC- und NDJSON-Kontrakt zwischen Cockpit und Python.</li>
              <li className="flex flex-wrap gap-1.5">
                {['CADGeometryEngine', 'MeshEngine', 'ModuleRegistry', 'CaseGenerator', 'WslSimulationRunner'].map((n) => (
                  <Code key={n}>{n}</Code>
                ))}
              </li>
              <li>Alle Zahlen in SI. Einheiten rechnet ausschließlich die Oberfläche um.</li>
            </ul>
          </article>
        </div>
      </section>

      {/* ----------------------------------------------------------- Eingaben ---- */}
      <section>
        <SectionHead title="Eingaben aus der Oberfläche">
          <p>Was das Cockpit je Bereich sendet und was es dafür zurückbraucht.</p>
        </SectionHead>
        <div className="mt-5 overflow-x-auto rounded-[6px] border border-line">
          <table className="w-full min-w-[720px] border-collapse text-left text-[14px]">
            <thead>
              <tr className="bg-bg2">
                {['Bereich', 'Cockpit sendet', 'Cockpit braucht', 'Im Prototyp'].map((h) => (
                  <th key={h} scope="col" className="label px-3 py-2 font-semibold">
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => (
                <tr key={r.area} className="border-t border-line align-top">
                  <th scope="row" className="px-3 py-3 font-medium text-ink">
                    {r.area}
                  </th>
                  <td className="px-3 py-3 leading-snug text-ink2">{r.sends}</td>
                  <td className="px-3 py-3 leading-snug text-ink2">{r.needs}</td>
                  <td className="px-3 py-3 whitespace-nowrap">
                    {r.go && (
                      <button type="button" onClick={r.go.run} className="inline-flex items-center gap-1 text-[13px] text-accent-ink underline-offset-2 hover:underline">
                        {r.go.label} <ArrowRight size={12} aria-hidden />
                      </button>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>

      {/* ----------------------------------------------------- foam_fast_io ---- */}
      <section>
        <SectionHead title="Drei Wünsche an foam_fast_io" />
        <ol className="mt-4 grid gap-4 md:grid-cols-3">
          {[
            {
              t: 'boundaryField parsen',
              d: (
                <>
                  <Code>fields.rs</Code> liest bisher nur <Code>internalField</Code>. Ohne <Code>boundaryField</Code> gibt es keine exakte
                  Wandtemperatur <Sym s="T_w" /> und kein y⁺ auf der Schaufel.
                </>
              ),
              go: { label: 'T_w im Prototyp', run: () => nav.openField('tw') },
            },
            {
              t: 'list_time_dirs',
              d: <>Schnelle Suche der Zeitverzeichnisse. Sie füllt die Zeitleiste unter dem Residuenplot.</>,
              go: { label: 'Zeitleiste ansehen', run: () => nav.go('cockpit') },
            },
            {
              t: 'Binärformat',
              d: (
                <>
                  <Code>writeFormat binary;</Code> zusätzlich zu ASCII und Gzip, damit große Fälle ohne Textparser laden.
                </>
              ),
            },
          ].map((w, i) => (
            <li key={w.t} className="rounded-[6px] border border-line bg-bg2 p-4">
              <p className="num text-[13px] text-ink3">{String(i + 1).padStart(2, '0')}</p>
              <h3 className="mt-1 text-[16px] font-medium text-ink">{w.t}</h3>
              <p className="mt-1.5 text-[14px] leading-snug text-ink2">{w.d}</p>
              {w.go && (
                <button type="button" onClick={w.go.run} className="mt-2 inline-flex items-center gap-1 text-[13px] text-accent-ink underline-offset-2 hover:underline">
                  {w.go.label} <ArrowRight size={12} aria-hidden />
                </button>
              )}
            </li>
          ))}
        </ol>
      </section>

      {/* -------------------------------------------------------------- Pakete ---- */}
      <section>
        <SectionHead title="Telemetrie-Pakete">
          <p>
            Vorschlag für das Format. Die Befehle und Inhalte stammen aus dem Anforderungsdokument, die Feldnamen sind mit dem Backend noch nicht
            abgestimmt.
          </p>
        </SectionHead>
        <pre className="num mt-4 overflow-x-auto rounded-[6px] border border-line bg-bg0 p-4 text-[12.5px] leading-[1.7] text-ink2">
          <span className="text-ink3">{'# Cockpit → Backend (JSON-RPC)\n'}</span>
          {'{"jsonrpc":"2.0","id":7,"method":"solver.start","params":{"case":"cases/blade_demo"}}\n'}
          <span className="text-ink3">{'# Backend → Cockpit (NDJSON, eine Zeile je Iteration)\n'}</span>
          {'{"t":"iter","n":412,"res":{"p":3.1e-5,"U":8.2e-6,"h":4.4e-5,"k":2.7e-5},"co_max":0.62,"nan_cells":0}\n'}
          {'{"t":"write","time":450}\n'}
          <span className="text-ink3">{'# Cockpit → Backend\n'}</span>
          {'{"jsonrpc":"2.0","id":8,"method":"solver.stop_graceful"}'}
        </pre>
      </section>

      {/* ---------------------------------------------------------- Checkliste ---- */}
      <section>
        <div className="flex flex-wrap items-end justify-between gap-3">
          <SectionHead title="Voraussetzungen für den Rough-Build">
            <p>Fenster im dunklen Chassis, 3D-Viewport auf DX12/Vulkan, linkes Block-Panel, echte Colormaps.</p>
          </SectionHead>
          <p className="num text-[13.5px] text-ink2">
            {nDone} von {CHECKS.length} erledigt
          </p>
        </div>
        <ul className="mt-4">
          {CHECKS.map((c) => {
            const on = !!done[c.id];
            return (
              <li key={c.id} className="border-t border-line">
                <label className="flex cursor-pointer gap-3.5 py-3.5">
                  <input type="checkbox" checked={on} onChange={(e) => setDone({ ...done, [c.id]: e.target.checked })} className="mt-[3px] h-[18px] w-[18px] shrink-0" />
                  <span className="min-w-0">
                    <span className="flex flex-wrap items-center gap-x-2.5 gap-y-1">
                      <span className={cn('text-[15px] font-medium', on ? 'text-ink2' : 'text-ink')}>{c.title}</span>
                      <Chip tone={c.kind === 'Pflicht' ? 'warn' : c.kind === 'vorhanden' ? 'ok' : 'neutral'}>{c.kind}</Chip>
                    </span>
                    <span className="mt-1 block max-w-[78ch] text-[14px] leading-snug text-ink2">{c.text}</span>
                  </span>
                </label>
              </li>
            );
          })}
        </ul>
      </section>

      {/* ------------------------------------------------------ Entscheidungen ---- */}
      <section>
        <SectionHead title="Zwei Entscheidungen vor dem Start">
          <p>Die Auswahl bleibt in diesem Browser gespeichert und lässt sich als fertige Antwort kopieren.</p>
        </SectionHead>
        <div className="mt-5 grid gap-6 lg:grid-cols-2">
          <fieldset>
            <legend className="label text-[13px] text-ink2">Unterbau</legend>
            <div className="mt-2 space-y-2">
              {[
                {
                  id: 'c1' as const,
                  t: 'C1 · wgpu + egui direkt',
                  d: 'Mit egui_dock und egui_plot. Kompiliert schnell, volle Kontrolle über Reverse-Z und eigene Compute-Shader, rendert nur bei Bedarf.',
                  rec: true,
                },
                {
                  id: 'c2' as const,
                  t: 'C2 · Bevy + bevy_egui',
                  d: 'Bringt ECS und fertige XR-Crates mit, hat aber schwerere Abhängigkeiten und läuft standardmäßig im dauerhaften Game-Loop.',
                  rec: false,
                },
              ].map((o) => (
                <label
                  key={o.id}
                  className={cn('flex cursor-pointer gap-3 rounded-[6px] border p-3.5', base === o.id ? 'border-accent-ink bg-bg3' : 'border-line bg-bg2 hover:border-line2')}
                >
                  <input type="radio" name="base" checked={base === o.id} onChange={() => setBase(o.id)} className="mt-[3px] h-[16px] w-[16px] shrink-0" />
                  <span>
                    <span className="flex flex-wrap items-center gap-2 text-[15px] font-medium text-ink">
                      {o.t} {o.rec && <Chip tone="ok">empfohlen</Chip>}
                    </span>
                    <span className="mt-1 block text-[14px] leading-snug text-ink2">{o.d}</span>
                  </span>
                </label>
              ))}
            </div>
          </fieldset>

          <fieldset>
            <legend className="label text-[13px] text-ink2">Bisheriger Trame-Prototyp</legend>
            <div className="mt-2 space-y-2">
              {[
                { id: 'archive' as const, t: 'Archivieren', d: 'frontend/cockpit/ wandert nach frontend/_legacy_trame/. Das Rust-Projekt entsteht unter frontend/cockpit_rs/.' },
                { id: 'delete' as const, t: 'Löschen', d: 'Der Python/Trame-Prototyp wird entfernt. Das Rust-Projekt entsteht unter frontend/cockpit_rs/.' },
              ].map((o) => (
                <label
                  key={o.id}
                  className={cn('flex cursor-pointer gap-3 rounded-[6px] border p-3.5', legacy === o.id ? 'border-accent-ink bg-bg3' : 'border-line bg-bg2 hover:border-line2')}
                >
                  <input type="radio" name="legacy" checked={legacy === o.id} onChange={() => setLegacy(o.id)} className="mt-[3px] h-[16px] w-[16px] shrink-0" />
                  <span>
                    <span className="text-[15px] font-medium text-ink">{o.t}</span>
                    <span className="mt-1 block text-[14px] leading-snug text-ink2">{o.d}</span>
                  </span>
                </label>
              ))}
            </div>
          </fieldset>
        </div>

        <div className="mt-5 rounded-[6px] border border-line bg-bg0">
          <pre className="num overflow-x-auto p-4 text-[12.5px] leading-[1.65] whitespace-pre-wrap text-ink2">{answer}</pre>
          <div className="flex items-center gap-3 border-t border-line px-4 py-2.5">
            <button
              type="button"
              onClick={() => navigator.clipboard?.writeText(answer).then(() => setCopied(true), () => setCopied(false))}
              className="inline-flex min-h-[34px] items-center gap-1.5 rounded-[4px] border border-accent-ink bg-accent px-3 text-[13.5px] text-white hover:brightness-110"
            >
              {copied ? <Check size={14} aria-hidden /> : <ClipboardCopy size={14} aria-hidden />}
              Antwort kopieren
            </button>
            <span className="text-[13px] text-ok" aria-live="polite">
              {copied ? 'In der Zwischenablage' : ''}
            </span>
          </div>
        </div>
      </section>
    </div>
  );
}
