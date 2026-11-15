import { TABLES, type Stops } from '../data/tables';
import {
  deltaE,
  hexToRgb,
  lstar,
  oklabToRgb,
  rgbToCss,
  rgbToOklab,
  simulateCvd,
  type Cvd,
  type RGB,
} from './color';

export type MapKind = 'sequential' | 'diverging' | 'quality' | 'mono' | 'rainbow';
export type MapGroup = 'wunsch' | 'referenz' | 'ansys' | 'ergaenzung';

export interface Colormap {
  id: string;
  name: string;
  kind: MapKind;
  group: MapGroup;
  stops: Stops;
  /** Raum, in dem zwischen den Stützstellen interpoliert wird. */
  interp: 'oklab' | 'srgb';
  source: string;
  url?: string;
  /** Dateiname im jrwrigh-Repo, falls als CFD-Post-.cst vorhanden. */
  cst?: string;
  use: string;
  note?: string;
}

export const KIND_LABEL: Record<MapKind, string> = {
  sequential: 'Sequenziell',
  diverging: 'Divergent',
  quality: 'Qualität',
  mono: 'Graustufen',
  rainbow: 'Regenbogen',
};

export const GROUP_LABEL: Record<MapGroup, string> = {
  wunsch: 'Deine Vorgaben',
  referenz: 'Aus den Referenzbildern',
  ansys: 'Ansys CFD-Post · Moreland-Tabellen',
  ergaenzung: 'Ergänzungen',
};

export const GROUP_ORDER: MapGroup[] = ['wunsch', 'referenz', 'ansys', 'ergaenzung'];

const even = (hexes: string[]): Stops => hexes.map((h, i) => [i / (hexes.length - 1), h]);

const MORELAND = 'https://www.kennethmoreland.com/color-advice/';

export const COLORMAPS: Colormap[] = [
  // ---- Deine Vorgaben -----------------------------------------------------
  {
    id: 'ampel',
    name: 'Ampel',
    kind: 'quality',
    group: 'wunsch',
    stops: [
      [0, '#009E73'],
      [0.5, '#FEC40B'],
      [1, '#EF233C'],
    ],
    interp: 'oklab',
    source: 'Eigenentwurf nach Briefing · Grün mit Blauanteil (#009E73), Amber Gold, Signal Red',
    use: 'Zellqualität: Skewness, Nicht-Orthogonalität, y⁺-Zielband, Konvergenzstatus',
    note: 'Das Grün trägt Blau und hebt sich so bei Rot-Grün-Schwäche über die Blau-Gelb-Achse vom Rot ab. Die Enden sind aber fast gleich hell (L* 58 / 52) – deshalb immer mit Iso-Bändern und festen Schwellwerten einsetzen.',
  },
  {
    id: 'inferno',
    name: 'Inferno',
    kind: 'sequential',
    group: 'wunsch',
    stops: TABLES['inferno'],
    interp: 'oklab',
    source: 'matplotlib (van der Walt, Smith) · Tabelle von Moreland',
    url: MORELAND + '#inferno',
    cst: 'Inferno.cst',
    use: 'Temperatur, Wärmestromdichte, Strahlungsintensität',
  },
  {
    id: 'aero',
    name: 'Aero',
    kind: 'sequential',
    group: 'wunsch',
    stops: even(['#050B2E', '#0A2A8C', '#004AFD', '#3F9BFF', '#9AD4FF', '#DFE5EB']),
    interp: 'oklab',
    source: 'Eigenentwurf · Nachtblau → Electric Blue → Cloud',
    use: 'Geschwindigkeitsbetrag, Mach-Zahl, Totaldruck',
    note: '„Aero“ war im Briefing nicht näher bestimmt. Dieser Vorschlag baut auf Electric Blue und Cloud auf – bitte bestätigen oder Vorlage nennen.',
  },
  {
    id: 'gruen-weiss',
    name: 'Grün–Weiß',
    kind: 'sequential',
    group: 'wunsch',
    stops: even(['#04231A', '#0B5D3B', '#1F9D55', '#7FD18B', '#D2F2D0', '#FFFFFF']),
    interp: 'oklab',
    source: 'Eigenentwurf · monotone Helligkeit von Tannengrün nach Weiß',
    use: 'Massenbruch einzelner Spezies, Mischungsbruch, Füllgrad',
  },
  // ---- Aus den Referenzbildern -------------------------------------------
  {
    id: 'dark-ember',
    name: 'Dark Ember',
    kind: 'sequential',
    group: 'referenz',
    stops: even([
      '#03071E',
      '#370617',
      '#6A040F',
      '#9D0208',
      '#D00000',
      '#DC2F02',
      '#E85D04',
      '#F48C06',
      '#FAA307',
      '#FBB539',
    ]),
    interp: 'oklab',
    source: 'cmap_ref_dark_ember_10step.png',
    use: 'Verbrennung, Flammenfront, Reaktionsrate – gut auf dunklem Chassis',
    note: 'Endpunkte laut Briefing (#03071E … #FBB539). Die acht Zwischenstufen sind aus der bekannten 10er-Palette rekonstruiert und müssen gegen das Originalbild geprüft werden.',
  },
  {
    id: 'fire',
    name: 'Fire',
    kind: 'sequential',
    group: 'referenz',
    stops: even(['#801100', '#B62203', '#D73502', '#FC6400', '#FF7500', '#FAC000']),
    interp: 'oklab',
    source: 'cmap_ref_fire_scheme.png',
    use: 'Wärmefreisetzung, OH-Konzentration, Batterie-Hotspots',
    note: 'Endpunkte laut Briefing (#801100 … #FAC000), Zwischenstufen rekonstruiert. Schmaler Helligkeitsumfang – eher für Akzent-Plots als für feine Gradienten.',
  },
  // ---- Ansys CFD-Post · Moreland ------------------------------------------
  {
    id: 'smooth-cool-warm',
    name: 'Smooth Cool Warm',
    kind: 'diverging',
    group: 'ansys',
    stops: TABLES['smooth-cool-warm'],
    interp: 'oklab',
    source: 'Moreland 2009 · „Diverging Color Maps for Scientific Visualization“',
    url: MORELAND + '#smooth-cool-warm',
    cst: 'Smooth_Cool_Warm.cst',
    use: 'Größen mit Vorzeichen: c_p, p − p∞, U_x (Rückströmung), Wandwärmestrom',
    note: 'Neutraler Mittelpunkt = Null. Bereich immer symmetrisch setzen.',
  },
  {
    id: 'bent-cool-warm',
    name: 'Bent Cool Warm',
    kind: 'diverging',
    group: 'ansys',
    stops: TABLES['bent-cool-warm'],
    interp: 'oklab',
    source: 'Moreland · lineare Helligkeit mit Knick',
    url: MORELAND + '#bent-cool-warm',
    cst: 'Bent_Cool_Warm.cst',
    use: 'Wie Smooth Cool Warm, wenn die Mitte nicht auswaschen soll',
    note: 'Der Knick erzeugt laut Moreland ein sichtbares Artefakt am Nullpunkt.',
  },
  {
    id: 'fast',
    name: 'Fast',
    kind: 'diverging',
    group: 'ansys',
    stops: TABLES['fast'],
    interp: 'oklab',
    source: 'Samsel, Scott, Moreland 2024 · als neuer ParaView-Standard entworfen',
    url: MORELAND + '#fast',
    use: 'Divergente Größen auf beleuchteten 3D-Flächen; mehr Trennschärfe als Cool Warm',
    note: 'Nur bei Moreland gelistet, nicht als .cst im jrwrigh-Repo.',
  },
  {
    id: 'black-body',
    name: 'Black Body',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['black-body'],
    interp: 'oklab',
    source: 'Moreland · Schwarzkörper-Farben mit linearem L*',
    url: MORELAND + '#black-body',
    cst: 'Black_Body.cst',
    use: 'Temperatur und Strahlung auf 2D-Schnitten',
  },
  {
    id: 'extended-black-body',
    name: 'Extended Black Body',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['extended-black-body'],
    interp: 'oklab',
    source: 'Moreland · Black Body mit zusätzlichen Blau-/Violett-Tönen',
    url: MORELAND,
    cst: 'Extended_Black_Body.cst',
    use: 'Temperatur mit großem Dynamikumfang',
  },
  {
    id: 'kindlmann',
    name: 'Kindlmann',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['kindlmann'],
    interp: 'oklab',
    source: 'Kindlmann, Reinhard, Creem 2002 · Regenbogen mit monotoner Helligkeit',
    url: MORELAND + '#kindlmann',
    cst: 'Kindlmann.cst',
    use: 'Wenn Regenbogen-Farbtöne gewünscht sind, die Helligkeit aber ehrlich bleiben soll',
  },
  {
    id: 'extended-kindlmann',
    name: 'Extended Kindlmann',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['extended-kindlmann'],
    interp: 'oklab',
    source: 'Moreland · Kindlmann mit mehr als 360° Farbtonumlauf',
    url: MORELAND + '#extended-kindlmann',
    cst: 'Extended_Kindlmann.cst',
    use: 'Viele unterscheidbare Stufen bei Iso-Bändern',
  },
  {
    id: 'plasma',
    name: 'Plasma',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['plasma'],
    interp: 'oklab',
    source: 'matplotlib (van der Walt, Smith)',
    url: MORELAND + '#plasma',
    cst: 'Plasma.cst',
    use: 'Skalare auf 3D-Flächen – kein Schwarz am unteren Ende',
  },
  {
    id: 'viridis',
    name: 'Viridis',
    kind: 'sequential',
    group: 'ansys',
    stops: TABLES['viridis'],
    interp: 'oklab',
    source: 'matplotlib (Firing, van der Walt, Smith)',
    url: MORELAND + '#viridis',
    cst: 'Viridis.cst',
    use: 'Sicherer Standard für jede sequenzielle Größe ohne Vorzeichen',
  },
  {
    id: 'ansys-rainbow',
    name: 'Ansys Rainbow',
    kind: 'rainbow',
    group: 'ansys',
    stops: even(['#0000FF', '#00FFFF', '#00FF00', '#FFFF00', '#FF0000']),
    interp: 'srgb',
    source: 'Klassischer CFD-Post-Standard (Blau–Cyan–Grün–Gelb–Rot)',
    use: 'Nur zum Abgleich mit Altberichten',
    note: 'L* ist nicht monoton: Cyan und Gelb erzeugen Kanten, die im Feld nicht existieren.',
  },
  // ---- Ergänzungen ----------------------------------------------------------
  {
    id: 'turbo',
    name: 'Turbo',
    kind: 'rainbow',
    group: 'ergaenzung',
    stops: TABLES['turbo'],
    interp: 'oklab',
    source: 'Mikhailov (Google) 2019 · Stützstellen aus d3-scale-chromatic',
    url: 'https://research.google/blog/turbo-an-improved-rainbow-colormap-for-visualization/',
    use: 'Ersatz für Jet/Rainbow, wenn der Ansys-Look verlangt wird',
    note: 'Glatte, aber nicht monotone Helligkeit – nicht für Graustufen-Druck geeignet.',
  },
  {
    id: 'cividis',
    name: 'Cividis',
    kind: 'sequential',
    group: 'ergaenzung',
    stops: TABLES['cividis'],
    interp: 'oklab',
    source: 'Nuñez, Anderton, Renslow 2018 · Stützstellen aus d3-scale-chromatic',
    url: 'https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0199239',
    use: 'Berichte, die für jede Farbsehschwäche identisch lesbar sein müssen',
  },
  {
    id: 'schlieren',
    name: 'Schlieren / X-Ray',
    kind: 'mono',
    group: 'ergaenzung',
    stops: even(['#FFFFFF', '#000000']),
    interp: 'oklab',
    source: 'Eigenentwurf · perzeptuell lineare Graustufen, umkehrbar',
    use: 'Dichtegradient ‖∇ρ‖ (Stöße), Q-Kriterium, Gitter-Überlagerung',
    note: 'Normal = Schlieren (dunkle Stöße auf Weiß), umgekehrt = X-Ray.',
  },
];

export const MAP_BY_ID: Record<string, Colormap> = Object.fromEntries(COLORMAPS.map((m) => [m.id, m]));

// ---------------------------------------------------------------- LUT ----

export const LUT_SIZE = 256;

/** Backt eine Colormap in 256 sRGB-Texel (RGBA8). Interpolation in Oklab. */
export function bakeLut(map: Colormap, reverse = false): Uint8Array {
  const out = new Uint8Array(LUT_SIZE * 4);
  const cols = map.stops.map(([, h]) => hexToRgb(h));
  const labs = cols.map(rgbToOklab);
  let seg = 0;
  for (let i = 0; i < LUT_SIZE; i++) {
    const t = i / (LUT_SIZE - 1);
    while (seg < map.stops.length - 2 && t > map.stops[seg + 1][0]) seg++;
    const p0 = map.stops[seg][0];
    const p1 = map.stops[seg + 1][0];
    const f = p1 > p0 ? Math.min(1, Math.max(0, (t - p0) / (p1 - p0))) : 0;
    let c: RGB;
    if (map.interp === 'srgb') {
      const a = cols[seg];
      const b = cols[seg + 1];
      c = [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
    } else {
      const a = labs[seg];
      const b = labs[seg + 1];
      c = oklabToRgb([a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f]);
    }
    const j = (reverse ? LUT_SIZE - 1 - i : i) * 4;
    out[j] = Math.round(c[0] * 255);
    out[j + 1] = Math.round(c[1] * 255);
    out[j + 2] = Math.round(c[2] * 255);
    out[j + 3] = 255;
  }
  return out;
}

const lutCache = new Map<string, Uint8Array>();

export function getLut(id: string, reverse = false): Uint8Array {
  const key = id + (reverse ? ':r' : '');
  let lut = lutCache.get(key);
  if (!lut) {
    lut = bakeLut(MAP_BY_ID[id] ?? COLORMAPS[0], reverse);
    lutCache.set(key, lut);
  }
  return lut;
}

/** Wert 0..1 → Farbe; lineare Filterung zwischen Texeln wie auf der GPU. */
export function lutColor(lut: Uint8Array, t: number): RGB {
  const x = Math.min(1, Math.max(0, t)) * (LUT_SIZE - 1);
  const i = Math.min(LUT_SIZE - 2, Math.floor(x));
  const f = x - i;
  const a = i * 4;
  const b = a + 4;
  return [
    (lut[a] + (lut[b] - lut[a]) * f) / 255,
    (lut[a + 1] + (lut[b + 1] - lut[a + 1]) * f) / 255,
    (lut[a + 2] + (lut[b + 2] - lut[a + 2]) * f) / 255,
  ];
}

/** Dieselbe Bandlogik wie im Fragment-Shader: Farbe aus der Bandmitte. */
export function bandT(t: number, bands: number): number {
  if (bands <= 0) return t;
  return (Math.min(Math.floor(t * bands), bands - 1) + 0.5) / bands;
}

export interface RampOpts {
  bands?: number;
  dir?: 'to right' | 'to top';
  cvd?: Cvd;
}

export function rampCss(lut: Uint8Array, opts: RampOpts = {}): string {
  const { bands = 0, dir = 'to right', cvd = 'normal' } = opts;
  const col = (t: number) => rgbToCss(simulateCvd(lutColor(lut, t), cvd));
  const parts: string[] = [];
  if (bands > 0) {
    for (let k = 0; k < bands; k++) {
      const c = col((k + 0.5) / bands);
      parts.push(`${c} ${((k / bands) * 100).toFixed(3)}%`, `${c} ${(((k + 1) / bands) * 100).toFixed(3)}%`);
    }
  } else {
    const n = 40;
    for (let k = 0; k <= n; k++) parts.push(`${col(k / n)} ${((k / n) * 100).toFixed(2)}%`);
  }
  return `linear-gradient(${dir}, ${parts.join(', ')})`;
}

// ------------------------------------------------------------ Analyse ----

export interface MapAnalysis {
  /** L* an 64 Stützstellen (0..100) */
  L: number[];
  lmin: number;
  lmax: number;
  /** 'up' | 'down' = monoton, 'peak' = hell in der Mitte (divergent), 'no' = Umkehrpunkte */
  shape: 'up' | 'down' | 'peak' | 'no';
  /** Streuung der Farbschritte (ΔE) relativ zum Mittelwert, in % – kleiner = gleichmäßiger */
  spread: number;
  verdict: string;
  ok: boolean;
}

const analysisCache = new Map<string, MapAnalysis>();

export function analyze(id: string): MapAnalysis {
  const hit = analysisCache.get(id);
  if (hit) return hit;
  const lut = getLut(id);
  const n = 64;
  const cols: RGB[] = Array.from({ length: n }, (_, i) => lutColor(lut, i / (n - 1)));
  const L = cols.map(lstar);
  const tol = 0.6;
  let up = true;
  let down = true;
  for (let i = 1; i < n; i++) {
    if (L[i] < L[i - 1] - tol) up = false;
    if (L[i] > L[i - 1] + tol) down = false;
  }
  const iMax = L.indexOf(Math.max(...L));
  let peak = !up && !down && iMax > n * 0.3 && iMax < n * 0.7;
  if (peak) {
    for (let i = 1; i < n; i++) {
      if (i <= iMax && L[i] < L[i - 1] - tol) peak = false;
      if (i > iMax && L[i] > L[i - 1] + tol) peak = false;
    }
  }
  const steps = cols.slice(1).map((c, i) => deltaE(c, cols[i]));
  const mean = steps.reduce((a, b) => a + b, 0) / steps.length;
  const sd = Math.sqrt(steps.reduce((a, b) => a + (b - mean) ** 2, 0) / steps.length);
  const shape: MapAnalysis['shape'] = up ? 'up' : down ? 'down' : peak ? 'peak' : 'no';
  const kind = (MAP_BY_ID[id] ?? COLORMAPS[0]).kind;
  let verdict: string;
  if (kind === 'rainbow') verdict = shape === 'no' ? 'L* mit mehreren Umkehrpunkten' : 'L* steigt und fällt wieder';
  else if (shape === 'up') verdict = 'L* steigt monoton';
  else if (shape === 'down') verdict = 'L* fällt monoton';
  else if (shape === 'peak') verdict = kind === 'quality' ? 'L* gipfelt in der Mitte (Gelb)' : 'L* symmetrisch um die helle Mitte';
  else verdict = 'L* nicht monoton';
  const res: MapAnalysis = {
    L,
    lmin: Math.min(...L),
    lmax: Math.max(...L),
    shape,
    spread: (sd / mean) * 100,
    verdict,
    ok: kind !== 'rainbow' && shape !== 'no',
  };
  analysisCache.set(id, res);
  return res;
}

// ------------------------------------------------------- Signalfarben ----

export const SIGNAL_CANDIDATES = [
  { hex: '#FC75B4', name: 'Vibrant Pink' },
  { hex: '#B9F500', name: 'Acid Lime' },
] as const;

export interface SignalPick {
  hex: string;
  name: string;
  /** kleinster Abstand (ΔE) zur gesamten LUT */
  dist: number;
  other: { hex: string; name: string; dist: number };
}

const signalCache = new Map<string, SignalPick>();

/** Wählt die NaN-Farbe, die am weitesten von allen Farben der Colormap entfernt liegt. */
export function pickSignal(id: string): SignalPick {
  const hit = signalCache.get(id);
  if (hit) return hit;
  const lut = getLut(id);
  const scored = SIGNAL_CANDIDATES.map((c) => {
    const rgb = hexToRgb(c.hex);
    let min = Infinity;
    for (let i = 0; i < 64; i++) min = Math.min(min, deltaE(rgb, lutColor(lut, i / 63)));
    return { ...c, dist: min };
  }).sort((a, b) => b.dist - a.dist);
  const res: SignalPick = { ...scored[0], other: scored[1] };
  signalCache.set(id, res);
  return res;
}
