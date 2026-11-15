import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import { MAP_BY_ID, getLut, pickSignal } from './colormaps';
import { FIELDS, FIELD_BY_ID, type FieldId, type FlowState } from './flow';

// ------------------------------------------------------------- Cockpit ----

export type Bands = 0 | 10 | 16 | 24;
export type Shading = 'matcap' | 'unlit';

export interface CockpitState {
  field: FieldId;
  maps: Record<FieldId, string>;
  reverse: Record<FieldId, boolean>;
  ranges: Record<FieldId, [number, number]>;
  zeroLock: boolean;
  bands: Bands;
  shading: Shading;
  edges: boolean;
  sliceOn: boolean;
  sliceS: number;
  nanOn: boolean;
  hatch: boolean;
  /** Eingaben aus dem Block-Stack (SI bzw. Grad) */
  alphaDeg: number;
  tInlet: number;
  target: number;
  writeEvery: number;
  block: string;
}

const byField = <T,>(fn: (id: FieldId) => T) => Object.fromEntries(FIELDS.map((f) => [f.id, fn(f.id)])) as Record<FieldId, T>;

const INITIAL: CockpitState = {
  field: 'tw',
  maps: byField((id) => FIELD_BY_ID[id].defaultMap),
  reverse: byField(() => false),
  ranges: byField((id) => [...FIELD_BY_ID[id].range] as [number, number]),
  zeroLock: true,
  bands: 0,
  shading: 'unlit',
  edges: false,
  sliceOn: true,
  sliceS: 0.45,
  nanOn: false,
  hatch: true,
  alphaDeg: 2,
  tInlet: 1450,
  target: 1e-5,
  writeEvery: 50,
  block: 'thermal',
};

interface CockpitApi {
  s: CockpitState;
  set: (patch: Partial<CockpitState>) => void;
  setField: (id: FieldId) => void;
  setMap: (id: string) => void;
  setRangeEdge: (edge: 0 | 1, value: number) => void;
  resetRange: () => void;
  /** abgeleitete Größen für das aktive Feld */
  mapId: string;
  lut: Uint8Array;
  range: [number, number];
  nanColor: string;
  diverging: boolean;
}

const CockpitCtx = createContext<CockpitApi | null>(null);

export function useCockpit(): CockpitApi {
  const v = useContext(CockpitCtx);
  if (!v) throw new Error('CockpitProvider fehlt');
  return v;
}

// -------------------------------------------------------------- Solver ----

export type SolverStatus = 'running' | 'converged' | 'stopped' | 'aborted';

export interface Packet {
  n: number;
  p: number;
  U: number;
  h: number;
  k: number;
  co: number;
}

export const SERIES = [
  { key: 'p', label: 'p', color: '#84A6FF', r0: 0.6, tau: 105 },
  { key: 'U', label: 'U', color: '#B9F500', r0: 0.2, tau: 95 },
  { key: 'h', label: 'h', color: '#FEC40B', r0: 0.4, tau: 120 },
  { key: 'k', label: 'k', color: '#FC75B4', r0: 0.3, tau: 110 },
] as const;

const noise = (n: number, i: number) => {
  const v = Math.sin(n * 12.9898 + i * 78.233 + 1.7) * 43758.5453;
  return v - Math.floor(v);
};

/** Stellvertreter für ein NDJSON-Iterationspaket des Backends (deterministisch, kein echter Solver). */
export function packetAt(n: number): Packet {
  const r = SERIES.map((sr, i) => Math.max(2e-8, sr.r0 * Math.pow(10, -n / sr.tau) * (0.72 + 0.56 * noise(n, i))));
  return { n, p: r[0], U: r[1], h: r[2], k: r[3], co: 0.58 + 0.34 * Math.exp(-n / 80) + 0.05 * noise(n, 9) };
}

const isConverged = (p: Packet, target: number) => p.p < target && p.U < target && p.h < target && p.k < target;

export interface SolverState {
  status: SolverStatus;
  hist: Packet[];
  /** geschriebene Zeitverzeichnisse (Iterationsnummern) – entspricht list_time_dirs() */
  dirs: number[];
  /** Index des im Viewport geladenen Zeitverzeichnisses */
  current: number;
}

function advance(s: SolverState, steps: number, target: number, writeEvery: number): SolverState {
  if (s.status !== 'running') return s;
  const hist = s.hist.slice();
  const dirs = s.dirs.slice();
  let status: SolverStatus = 'running';
  const follow = s.current === s.dirs.length - 1;
  for (let k = 0; k < steps; k++) {
    const n = hist.length ? hist[hist.length - 1].n + 1 : 1;
    const p = packetAt(n);
    hist.push(p);
    if (n % writeEvery === 0) dirs.push(n);
    if (isConverged(p, target) || n >= 1500) {
      if (dirs[dirs.length - 1] !== n) dirs.push(n);
      status = 'converged';
      break;
    }
  }
  return { status, hist, dirs, current: follow ? dirs.length - 1 : s.current };
}

function runToEnd(target: number, writeEvery: number): SolverState {
  let s: SolverState = { status: 'running', hist: [], dirs: [0], current: 0 };
  while (s.status === 'running') s = advance(s, 50, target, writeEvery);
  return s;
}

interface SolverApi {
  sv: SolverState;
  iter: number;
  last: Packet | null;
  start: () => void;
  writeNow: () => void;
  stopGraceful: () => void;
  abort: () => void;
  select: (i: number) => void;
  /** Strömungszustand des geladenen Zeitverzeichnisses */
  flow: FlowState;
}

const SolverCtx = createContext<SolverApi | null>(null);

export function useSolver(): SolverApi {
  const v = useContext(SolverCtx);
  if (!v) throw new Error('CockpitProvider fehlt');
  return v;
}

// ------------------------------------------------------------ Provider ----

export function CockpitProvider({ children }: { children: ReactNode }) {
  const [s, setS] = useState<CockpitState>(INITIAL);
  const [sv, setSv] = useState<SolverState>(() => runToEnd(INITIAL.target, INITIAL.writeEvery));

  const set = useCallback((patch: Partial<CockpitState>) => setS((p) => ({ ...p, ...patch })), []);

  const setField = useCallback((id: FieldId) => {
    setS((p) => ({
      ...p,
      field: id,
      shading: 'unlit',
      sliceOn: FIELD_BY_ID[id].where === 'slice' ? true : p.sliceOn,
    }));
  }, []);

  const setMap = useCallback((id: string) => {
    setS((p) => {
      let range = p.ranges[p.field];
      // divergente Map auf vorzeichenbehaftetem Feld: Null gehört in die Mitte
      if (p.zeroLock && FIELD_BY_ID[p.field].signed && MAP_BY_ID[id]?.kind === 'diverging') {
        const m = Math.max(Math.abs(range[0]), Math.abs(range[1]));
        range = [-m, m];
      }
      // eine Colormap zu wählen heißt: Skalar-Darstellung
      return { ...p, shading: 'unlit', maps: { ...p.maps, [p.field]: id }, ranges: { ...p.ranges, [p.field]: range } };
    });
  }, []);

  const setRangeEdge = useCallback((edge: 0 | 1, value: number) => {
    setS((p) => {
      const cur = p.ranges[p.field];
      const locked = p.zeroLock && FIELD_BY_ID[p.field].signed && MAP_BY_ID[p.maps[p.field]]?.kind === 'diverging';
      let next: [number, number];
      if (locked) {
        const m = Math.abs(value) || Math.abs(cur[1]);
        next = [-m, m];
      } else {
        next = edge === 0 ? [value, cur[1]] : [cur[0], value];
      }
      return { ...p, ranges: { ...p.ranges, [p.field]: next } };
    });
  }, []);

  const resetRange = useCallback(() => {
    setS((p) => ({ ...p, ranges: { ...p.ranges, [p.field]: [...FIELD_BY_ID[p.field].range] as [number, number] } }));
  }, []);

  const mapId = s.maps[s.field];
  const reversed = s.reverse[s.field];
  const cockpit = useMemo<CockpitApi>(
    () => ({
      s,
      set,
      setField,
      setMap,
      setRangeEdge,
      resetRange,
      mapId,
      lut: getLut(mapId, reversed),
      range: s.ranges[s.field],
      nanColor: pickSignal(mapId).hex,
      diverging: MAP_BY_ID[mapId]?.kind === 'diverging',
    }),
    [s, set, setField, setMap, setRangeEdge, resetRange, mapId, reversed],
  );

  // Telemetrie-Takt: drei Iterationen alle 90 ms, solange der Lauf aktiv ist
  const running = sv.status === 'running';
  const { target, writeEvery } = s;
  useEffect(() => {
    if (!running) return;
    const id = window.setInterval(() => setSv((p) => advance(p, 3, target, writeEvery)), 90);
    return () => window.clearInterval(id);
  }, [running, target, writeEvery]);

  const start = useCallback(() => setSv({ status: 'running', hist: [], dirs: [0], current: 0 }), []);
  const writeNow = useCallback(
    () =>
      setSv((p) => {
        const n = p.hist.length ? p.hist[p.hist.length - 1].n : 0;
        if (p.status !== 'running' || p.dirs.includes(n)) return p;
        const follow = p.current === p.dirs.length - 1;
        const dirs = [...p.dirs, n];
        return { ...p, dirs, current: follow ? dirs.length - 1 : p.current };
      }),
    [],
  );
  const stopGraceful = useCallback(
    () =>
      setSv((p) => {
        if (p.status !== 'running') return p;
        const n = p.hist.length ? p.hist[p.hist.length - 1].n : 0;
        const dirs = p.dirs.includes(n) ? p.dirs : [...p.dirs, n];
        return { ...p, status: 'stopped', dirs, current: dirs.length - 1 };
      }),
    [],
  );
  const abort = useCallback(() => setSv((p) => (p.status === 'running' ? { ...p, status: 'aborted' } : p)), []);
  const select = useCallback(
    (i: number) => setSv((p) => ({ ...p, current: Math.min(p.dirs.length - 1, Math.max(0, i)) })),
    [],
  );

  const loaded = sv.dirs[sv.current] ?? 0;
  const flow = useMemo<FlowState>(() => {
    const relax = Math.exp(-loaded / 140);
    return {
      alphaInf: ((s.alphaDeg + 4 * relax * Math.cos(loaded / 18)) * Math.PI) / 180,
      tInlet: s.tInlet,
      progress: 1 - relax,
    };
  }, [loaded, s.alphaDeg, s.tInlet]);

  const solver = useMemo<SolverApi>(
    () => ({
      sv,
      iter: sv.hist.length ? sv.hist[sv.hist.length - 1].n : 0,
      last: sv.hist.length ? sv.hist[sv.hist.length - 1] : null,
      start,
      writeNow,
      stopGraceful,
      abort,
      select,
      flow,
    }),
    [sv, start, writeNow, stopGraceful, abort, select, flow],
  );

  return (
    <CockpitCtx.Provider value={cockpit}>
      <SolverCtx.Provider value={solver}>{children}</SolverCtx.Provider>
    </CockpitCtx.Provider>
  );
}
