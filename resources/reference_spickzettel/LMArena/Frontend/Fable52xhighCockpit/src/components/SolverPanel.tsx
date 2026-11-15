import { useMemo, type ReactNode } from 'react';
import { OctagonX, Play, Save, Square } from 'lucide-react';
import { nanDemoCount } from '../lib/flow';
import { SERIES, useCockpit, useSolver, type Packet, type SolverStatus } from '../lib/store';
import { cn } from '../utils/cn';
import { Chip, PanelHead, useSize } from './ui';

const STATUS: Record<SolverStatus, { label: string; tone: 'accent' | 'ok' | 'neutral' | 'bad' }> = {
  running: { label: 'läuft', tone: 'accent' },
  converged: { label: 'konvergiert', tone: 'ok' },
  stopped: { label: 'gestoppt', tone: 'neutral' },
  aborted: { label: 'abgebrochen', tone: 'bad' },
};

const expo = (v: number) => v.toExponential(1).replace('e+', 'e');

function Cmd({
  onClick,
  disabled,
  children,
  title,
  danger,
  primary,
}: {
  onClick: () => void;
  disabled?: boolean;
  children: ReactNode;
  title: string;
  danger?: boolean;
  primary?: boolean;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      disabled={disabled}
      title={title}
      className={cn(
        'num inline-flex min-h-[28px] items-center gap-1.5 rounded-[3px] border px-2 text-[12.5px] leading-none whitespace-nowrap transition-colors max-sm:min-h-[38px]',
        'disabled:border-line disabled:bg-transparent disabled:text-ink3 disabled:opacity-60',
        primary
          ? 'border-accent-ink bg-accent text-white hover:brightness-110'
          : danger
            ? 'border-bad/60 text-bad-ink hover:bg-bad/15'
            : 'border-line2 text-ink hover:bg-bg4',
      )}
    >
      {children}
    </button>
  );
}

export default function SolverPanel() {
  const { sv, iter, last, start, writeNow, stopGraceful, abort, select } = useSolver();
  const { s } = useCockpit();
  const [ref, size] = useSize<HTMLDivElement>();
  const running = sv.status === 'running';
  const loaded = sv.dirs[sv.current] ?? 0;
  const nan = useMemo(() => nanDemoCount(), []);
  const nanCells = s.nanOn ? nan : 0;

  // Plotgeometrie
  const padL = 44;
  const padR = 26;
  const padT = 8;
  const trackH = 30;
  const padB = 18 + trackH;
  const W = Math.max(0, size.w);
  const H = Math.max(0, size.h);
  const pw = Math.max(10, W - padL - padR);
  const ph = Math.max(10, H - padT - padB);
  const xMax = Math.max(600, Math.ceil((iter + 1) / 100) * 100);
  const X = (n: number) => padL + (n / xMax) * pw;
  const Y = (v: number) => padT + (Math.min(8, Math.max(0, -Math.log10(v))) / 8) * ph;

  const paths = useMemo(() => {
    const step = Math.max(1, Math.ceil(sv.hist.length / Math.max(1, pw)));
    return SERIES.map((sr) => {
      let d = '';
      for (let i = 0; i < sv.hist.length; i += step) {
        const p = sv.hist[i];
        d += `${d ? 'L' : 'M'}${X(p.n).toFixed(1)} ${Y(p[sr.key as keyof Packet]).toFixed(1)}`;
      }
      return d;
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sv.hist, pw, ph, xMax]);

  const xTicks: number[] = [];
  const xStep = xMax / pw > 2.2 ? 200 : 100;
  for (let n = 0; n <= xMax; n += xStep) xTicks.push(n);
  const st = STATUS[sv.status];

  // Endbeschriftungen entzerren, damit sich p/U/h/k nicht überlagern
  const ends = useMemo(() => {
    if (!last) return [];
    const raw = SERIES.map((sr) => ({ ...sr, y: Y(last[sr.key as keyof Packet]) })).sort((a, b) => a.y - b.y);
    for (let i = 1; i < raw.length; i++) if (raw[i].y - raw[i - 1].y < 12) raw[i].y = raw[i - 1].y + 12;
    return raw;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [last, ph]);

  return (
    <div className="inset-focus flex h-full min-h-0 flex-col bg-bg2">
      <PanelHead
        title="Solver · Live-Telemetrie"
        right={<Chip tone={st.tone === 'neutral' ? 'neutral' : st.tone}>{st.label}</Chip>}
      />

      <div className="flex flex-wrap items-center gap-x-4 gap-y-2 border-b border-line px-3 py-2">
        <div className="flex flex-wrap gap-1.5">
          <Cmd onClick={start} disabled={running} primary={!running} title="Lauf von Iteration 0 neu starten">
            <Play size={12} aria-hidden /> start
          </Cmd>
          <Cmd onClick={writeNow} disabled={running ? sv.dirs.includes(iter) : true} title="Aktuellen Stand sofort als Zeitverzeichnis schreiben">
            <Save size={12} aria-hidden /> write_now
          </Cmd>
          <Cmd onClick={stopGraceful} disabled={!running} title="Iteration beenden, Stand schreiben, dann anhalten">
            <Square size={11} aria-hidden /> stop_graceful
          </Cmd>
          <Cmd onClick={abort} disabled={!running} danger title="Sofort abbrechen, ohne zu schreiben">
            <OctagonX size={12} aria-hidden /> abort
          </Cmd>
        </div>
        <dl className="num flex flex-wrap gap-x-4 gap-y-1 text-[12.5px] leading-none">
          <div className="flex gap-1.5">
            <dt className="text-ink3">Iter</dt>
            <dd className="w-[4ch] text-right text-ink">{iter}</dd>
          </div>
          <div className="flex gap-1.5">
            <dt className="text-ink3">Zeit geladen</dt>
            <dd className="text-accent-ink">{loaded}</dd>
          </div>
          <div className="flex gap-1.5">
            <dt className="text-ink3">Co max</dt>
            <dd className="text-ink">{last ? last.co.toFixed(2) : '–'}</dd>
          </div>
          <div className="flex gap-1.5">
            <dt className="text-ink3">NaN-Zellen</dt>
            <dd style={nanCells ? { color: '#FC75B4' } : undefined} className={nanCells ? '' : 'text-ink'}>
              {nanCells}
            </dd>
          </div>
        </dl>
        <p className="num ml-auto hidden max-w-[44ch] truncate text-[11.5px] leading-none text-ink3 2xl:block" title="Letztes Paket des Telemetrie-Kanals">
          {last
            ? `{"n":${last.n},"res":{"p":${expo(last.p)},"U":${expo(last.U)},"h":${expo(last.h)},"k":${expo(last.k)}},"co_max":${last.co.toFixed(2)},"nan_cells":${nanCells}}`
            : '{"n":0}'}
        </p>
      </div>

      <div ref={ref} className="relative min-h-[170px] flex-1 lg:min-h-[120px]">
        {W > 0 && H > 0 && (
          <>
            <svg width={W} height={H} className="absolute inset-0 block" role="img" aria-label={`Residuenverlauf bis Iteration ${iter}, Status ${st.label}`}>
              {/* Dekaden */}
              {[0, 2, 4, 6, 8].map((e) => {
                const y = padT + (e / 8) * ph;
                return (
                  <g key={e}>
                    <line x1={padL} x2={padL + pw} y1={y} y2={y} stroke="var(--c-line)" strokeWidth="1" />
                    <text x={padL - 6} y={y + 3.5} textAnchor="end" className="num" fontSize="11" fill="var(--c-ink3)">
                      {e === 0 ? '1' : `1e-${e}`}
                    </text>
                  </g>
                );
              })}
              {xTicks.map((n) => (
                <text key={n} x={X(n)} y={padT + ph + 13} textAnchor="middle" className="num" fontSize="11" fill="var(--c-ink3)">
                  {n}
                </text>
              ))}
              {/* geschriebene Zeitverzeichnisse */}
              {sv.dirs.map((n) => (
                <line key={n} x1={X(n)} x2={X(n)} y1={padT} y2={padT + ph} stroke="var(--c-line)" strokeWidth="1" strokeDasharray="2 3" />
              ))}
              <line x1={X(loaded)} x2={X(loaded)} y1={padT} y2={padT + ph + 18} stroke="var(--c-accent-ink)" strokeWidth="1" />
              {/* Ziel */}
              <line x1={padL} x2={padL + pw} y1={Y(s.target)} y2={Y(s.target)} stroke="#B9F500" strokeWidth="1" strokeDasharray="5 4" opacity="0.8" />
              <text x={padL + 5} y={Y(s.target) - 4} className="num" fontSize="11" fill="#B9F500">
                Ziel {s.target.toExponential(0)}
              </text>
              {/* Residuen */}
              {paths.map((d, i) => (
                <path key={SERIES[i].key} d={d} fill="none" stroke={SERIES[i].color} strokeWidth="1.5" strokeLinejoin="round" />
              ))}
              {last &&
                ends.map((e) => (
                  <text key={e.key} x={Math.min(X(last.n) + 5, padL + pw + 6)} y={e.y + 4} className="num" fontSize="12" fontWeight="600" fill={e.color}>
                    {e.label}
                  </text>
                ))}
            </svg>

            {/* Zeitleiste: geschriebene Zeitverzeichnisse, teilt die x-Achse mit dem Plot */}
            <div className="absolute right-0 bottom-0 left-0 border-t border-line bg-bg1" style={{ height: trackH }}>
              <span className="label absolute top-1/2 left-2 -translate-y-1/2 text-[11px]">Zeit</span>
              {sv.dirs.map((n, i) => {
                const on = i === sv.current;
                return (
                  <button
                    key={n}
                    type="button"
                    aria-pressed={on}
                    aria-label={`Zeitverzeichnis ${n} laden`}
                    title={`Zeit ${n}`}
                    onClick={() => select(i)}
                    className="group absolute top-0 grid h-full w-[22px] -translate-x-1/2 place-items-center"
                    style={{ left: X(n) }}
                  >
                    <span
                      className={cn(
                        'block h-[9px] w-[9px] rotate-45 border transition-colors',
                        on ? 'border-accent-ink bg-accent-ink' : 'border-ink3 bg-bg1 group-hover:border-ink group-hover:bg-bg4',
                      )}
                    />
                  </button>
                );
              })}
            </div>
          </>
        )}
      </div>
    </div>
  );
}
