import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { Download } from 'lucide-react';
import { Segmented } from './components/ui';
import { MAP_BY_ID } from './lib/colormaps';
import { buildPalettesJson, downloadJson } from './lib/exportJson';
import type { FieldId } from './lib/flow';
import type { ChassisId } from './lib/palettes';
import { CockpitProvider, useCockpit } from './lib/store';
import { cn } from './utils/cn';
import CockpitView from './views/CockpitView';
import ColormapsView from './views/ColormapsView';
import GuidelinesView from './views/GuidelinesView';
import InterfacesView from './views/InterfacesView';
import PalettesView from './views/PalettesView';
import ReferencesView from './views/ReferencesView';

export type TabId = 'cockpit' | 'farben' | 'colormaps' | 'referenzen' | 'leitplanken' | 'schnittstellen';

const TABS: { id: TabId; label: string }[] = [
  { id: 'cockpit', label: 'Cockpit' },
  { id: 'referenzen', label: 'Referenzen' },
  { id: 'farben', label: 'Farben' },
  { id: 'colormaps', label: 'Colormaps' },
  { id: 'leitplanken', label: 'Leitplanken' },
  { id: 'schnittstellen', label: 'Schnittstellen' },
];

const readHash = (): TabId => {
  const h = window.location.hash.replace('#', '') as TabId;
  return TABS.some((t) => t.id === h) ? h : 'cockpit';
};

const readChassis = (): ChassisId => {
  try {
    return window.localStorage.getItem('cockpit.chassis') === 'carbon' ? 'carbon' : 'obsidian';
  } catch {
    return 'obsidian';
  }
};

export interface Nav {
  go: (tab: TabId) => void;
  /** Colormap im Cockpit auf einem passenden Feld öffnen */
  openMap: (mapId: string) => void;
  /** Feld im Cockpit öffnen */
  openField: (field: FieldId) => void;
}

function Shell() {
  const [tab, setTab] = useState<TabId>(readHash);
  const [chassis, setChassis] = useState<ChassisId>(readChassis);
  const cockpit = useCockpit();
  const tabRefs = useRef<Record<string, HTMLButtonElement | null>>({});
  const scroller = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const on = () => setTab(readHash());
    window.addEventListener('hashchange', on);
    return () => window.removeEventListener('hashchange', on);
  }, []);

  useEffect(() => {
    document.documentElement.dataset.chassis = chassis;
    try {
      window.localStorage.setItem('cockpit.chassis', chassis);
    } catch {
      /* Speicher nicht verfügbar – Auswahl gilt nur für diese Sitzung */
    }
  }, [chassis]);

  const go = useCallback((t: TabId) => {
    if (window.location.hash.replace('#', '') !== t) window.location.hash = t;
    setTab(t);
    scroller.current?.scrollTo({ top: 0 });
  }, []);

  const { setField, setMap, s } = cockpit;
  const openMap = useCallback(
    (mapId: string) => {
      const kind = MAP_BY_ID[mapId]?.kind;
      const field: FieldId =
        kind === 'diverging' ? 'cp' : kind === 'quality' ? 'nonortho' : kind === 'mono' ? 'schlieren' : s.field === 'cp' || s.field === 'schlieren' ? 'tw' : s.field;
      setField(field);
      setMap(mapId);
      go('cockpit');
    },
    [setField, setMap, s.field, go],
  );
  const openField = useCallback(
    (field: FieldId) => {
      setField(field);
      go('cockpit');
    },
    [setField, go],
  );
  const nav: Nav = { go, openMap, openField };

  const onTabKey = (e: KeyboardEvent, i: number) => {
    const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const n = TABS[(i + d + TABS.length) % TABS.length];
    go(n.id);
    tabRefs.current[n.id]?.focus();
  };

  return (
    <div className="flex h-full flex-col">
      <header className="flex shrink-0 flex-wrap items-center border-b border-line bg-bg1 lg:flex-nowrap">
        <a href="#cockpit" onClick={() => go('cockpit')} className="flex h-11 shrink-0 items-center gap-2.5 px-3 text-ink no-underline">
          <svg width="26" height="16" viewBox="0 0 26 16" aria-hidden>
            <path d="M1 9.2C4.5 2.4 13 0.6 25 6.4 15.5 6.2 10 12.6 1 9.2Z" fill="currentColor" />
            <path d="M3 14.5h20" stroke="var(--c-accent-ink)" strokeWidth="1.5" strokeLinecap="round" />
          </svg>
          <span className="font-cond text-[17px] leading-none font-semibold tracking-[0.16em] uppercase">Cockpit</span>
          <span className="hidden border-l border-line2 pl-2.5 text-[12.5px] leading-tight text-ink3 xl:block">
            Design- &amp; Rendering-Referenz
            <span className="num"> v0.2.0</span>
          </span>
        </a>

        <div role="tablist" aria-label="Arbeitsbereiche" className="order-3 flex w-full overflow-x-auto border-t border-line lg:order-none lg:w-auto lg:flex-1 lg:border-t-0 lg:pl-2">
          {TABS.map((t, i) => {
            const on = t.id === tab;
            return (
              <button
                key={t.id}
                ref={(el) => {
                  tabRefs.current[t.id] = el;
                }}
                role="tab"
                type="button"
                id={`tab-${t.id}`}
                aria-selected={on}
                aria-controls="panel"
                tabIndex={on ? 0 : -1}
                onClick={() => go(t.id)}
                onKeyDown={(e) => onTabKey(e, i)}
                className={cn(
                  'relative h-11 shrink-0 px-3.5 text-[14px] whitespace-nowrap transition-colors focus-visible:outline-offset-[-3px]',
                  on ? 'bg-bg2 text-ink' : 'text-ink2 hover:bg-bg2/60 hover:text-ink',
                )}
              >
                {t.label}
                <span aria-hidden className={cn('absolute right-0 bottom-0 left-0 h-[2px]', on ? 'bg-accent-ink' : 'bg-transparent')} />
              </button>
            );
          })}
        </div>

        <div className="ml-auto flex h-11 shrink-0 items-center gap-2 px-3">
          <Segmented<ChassisId>
            label="Chassis"
            value={chassis}
            onChange={setChassis}
            options={[
              { value: 'obsidian', label: 'Obsidian', title: 'Chassis A · Triade T3' },
              { value: 'carbon', label: 'Carbon', title: 'Chassis B · Triade T4' },
            ]}
          />
          <button
            type="button"
            onClick={() => downloadJson('palettes.json', buildPalettesJson())}
            title="Themes, Kategorien und LUT-Stützstellen als JSON für den Rust-Loader"
            className="num inline-flex min-h-[32px] items-center gap-1.5 rounded-[4px] border border-line2 px-2.5 text-[12.5px] text-ink hover:bg-bg4"
          >
            <Download size={13} aria-hidden />
            <span className="max-xl:sr-only">palettes.json</span>
          </button>
        </div>
      </header>

      <main id="panel" role="tabpanel" aria-labelledby={`tab-${tab}`} className="relative min-h-0 flex-1">
        {/* Das Cockpit bleibt eingehängt, damit Kamera und Solver-Lauf beim Tab-Wechsel erhalten bleiben */}
        <div className={cn('h-full', tab !== 'cockpit' && 'hidden')}>
          <CockpitView />
        </div>
        {tab !== 'cockpit' && (
          <div ref={scroller} className="h-full overflow-y-auto">
            <div className="mx-auto max-w-[1180px] px-4 py-8 sm:px-8 sm:py-12">
              {tab === 'referenzen' && <ReferencesView nav={nav} />}
              {tab === 'farben' && <PalettesView chassis={chassis} />}
              {tab === 'colormaps' && <ColormapsView nav={nav} />}
              {tab === 'leitplanken' && <GuidelinesView nav={nav} />}
              {tab === 'schnittstellen' && <InterfacesView nav={nav} />}
            </div>
          </div>
        )}
      </main>
    </div>
  );
}

export default function App() {
  return (
    <CockpitProvider>
      <Shell />
    </CockpitProvider>
  );
}
