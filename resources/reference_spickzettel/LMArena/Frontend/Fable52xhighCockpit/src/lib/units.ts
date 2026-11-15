// Einheiten-Eingaben wie in nTop: „12 mm“, „850 C“, „3 bar“ → strikt SI für das Backend.

export type Dim = 'length' | 'temperature' | 'pressure' | 'angle' | 'degree' | 'ratio' | 'count';

type Conv = (v: number) => number;

const UNITS: Record<Dim, Record<string, Conv>> = {
  length: {
    m: (v) => v,
    cm: (v) => v * 1e-2,
    mm: (v) => v * 1e-3,
    um: (v) => v * 1e-6,
    'µm': (v) => v * 1e-6,
    'μm': (v) => v * 1e-6,
    nm: (v) => v * 1e-9,
    in: (v) => v * 0.0254,
  },
  temperature: {
    k: (v) => v,
    c: (v) => v + 273.15,
    '°c': (v) => v + 273.15,
    f: (v) => ((v - 32) * 5) / 9 + 273.15,
    '°f': (v) => ((v - 32) * 5) / 9 + 273.15,
  },
  pressure: {
    pa: (v) => v,
    hpa: (v) => v * 100,
    kpa: (v) => v * 1e3,
    mpa: (v) => v * 1e6,
    bar: (v) => v * 1e5,
    mbar: (v) => v * 100,
    atm: (v) => v * 101325,
    psi: (v) => v * 6894.757,
  },
  angle: {
    rad: (v) => v,
    deg: (v) => (v * Math.PI) / 180,
    '°': (v) => (v * Math.PI) / 180,
  },
  // Gradangaben, die auch im Backend in Grad bleiben (z. B. Nicht-Orthogonalität in OpenFOAM)
  degree: {
    deg: (v) => v,
    '°': (v) => v,
  },
  ratio: {
    '': (v) => v,
    '-': (v) => v,
    '–': (v) => v,
    '%': (v) => v / 100,
  },
  count: {
    '': (v) => v,
  },
};

/** Einheit, die angenommen wird, wenn nur eine Zahl eingegeben wurde. */
const DEFAULT_UNIT: Record<Dim, string> = {
  length: 'm',
  temperature: 'k',
  pressure: 'pa',
  angle: 'deg',
  degree: 'deg',
  ratio: '',
  count: '',
};

export const SI_UNIT: Record<Dim, string> = {
  length: 'm',
  temperature: 'K',
  pressure: 'Pa',
  angle: 'rad',
  degree: '°',
  ratio: '',
  count: '',
};

export const UNIT_HINT: Record<Dim, string> = {
  length: 'm, cm, mm, µm, nm, in',
  temperature: 'K, C, F',
  pressure: 'Pa, kPa, MPa, bar, mbar, atm, psi',
  angle: 'deg, rad',
  degree: '°',
  ratio: 'Zahl oder %',
  count: 'ganze Zahl',
};

export type ParseResult = { ok: true; si: number } | { ok: false; error: string };

const NUM = /^\s*([+-]?(?:\d+(?:[.,]\d*)?|[.,]\d+)(?:e[+-]?\d+)?)\s*(.*?)\s*$/i;

export function parseQuantity(input: string, dim: Dim): ParseResult {
  const m = NUM.exec(input);
  if (!m) return { ok: false, error: 'Keine Zahl erkannt' };
  const value = Number(m[1].replace(',', '.'));
  if (!Number.isFinite(value)) return { ok: false, error: 'Keine Zahl erkannt' };
  const unit = (m[2] || DEFAULT_UNIT[dim]).toLowerCase();
  const conv = UNITS[dim][unit];
  if (!conv) return { ok: false, error: `Einheit „${m[2]}“ unbekannt – erlaubt: ${UNIT_HINT[dim]}` };
  const si = conv(value);
  if (dim === 'count' && !Number.isInteger(si)) return { ok: false, error: 'Ganze Zahl erwartet' };
  if (dim === 'temperature' && si < 0) return { ok: false, error: 'Unter dem absoluten Nullpunkt' };
  return { ok: true, si };
}

const NNBSP = '\u202f';

/** Kompakte Zahl mit festen Ziffernbreiten; Exponent bei sehr großen/kleinen Werten. */
export function fmtNum(v: number, sig = 4): string {
  if (!Number.isFinite(v)) return Number.isNaN(v) ? 'NaN' : v > 0 ? 'Inf' : '−Inf';
  if (v === 0) return '0';
  const a = Math.abs(v);
  let s: string;
  if (a >= 1e6 || a < 1e-3) {
    const [mant, exp] = v.toExponential(Math.max(1, sig - 2)).split('e');
    s = `${mant.replace(/(\.\d)\d*?0+$/, '$1').replace(/(\.\d*?[1-9])0+$/, '$1')}e${exp.replace('+', '')}`;
  } else {
    const digits = Math.max(0, sig - 1 - Math.floor(Math.log10(a)));
    s = v.toFixed(Math.min(digits, 6));
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    const [int, frac] = s.split('.');
    const grouped = int.replace(/\B(?=(\d{3})+(?!\d))/g, NNBSP);
    s = frac !== undefined ? `${grouped}.${frac}` : grouped;
  }
  return s.replace('-', '−');
}

export function fmtSi(v: number, dim: Dim, sig = 6): string {
  const u = SI_UNIT[dim];
  return u ? `${fmtNum(v, sig)}${u === '°' ? '' : NNBSP}${u}` : fmtNum(v, sig);
}

/** Feste Nachkommastellen (für Legenden-Ticks und Live-Werte). */
export function fmtFixed(v: number, digits: number): string {
  if (!Number.isFinite(v)) return 'NaN';
  const s = v.toFixed(digits);
  const [int, frac] = s.replace('-', '').split('.');
  const grouped = int.replace(/\B(?=(\d{3})+(?!\d))/g, NNBSP);
  return (v < 0 && Number(s) !== 0 ? '−' : '') + (frac !== undefined ? `${grouped}.${frac}` : grouped);
}

/** Text für ein Eingabefeld: ohne Zifferngruppierung, damit er wieder geparst werden kann. */
export function inputText(v: number, dim: Dim): string {
  const n = String(+v.toPrecision(7));
  const u = SI_UNIT[dim];
  return u === '°' ? `${n}°` : u ? `${n} ${u}` : n;
}
