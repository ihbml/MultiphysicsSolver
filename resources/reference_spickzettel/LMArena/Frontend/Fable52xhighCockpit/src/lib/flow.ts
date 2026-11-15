// Demo-Geometrie und Demo-Felder für den Viewport, solange das Backend noch keinen
// Case in cases/ ablegt.
//
// Geometrie: verwundene, verjüngte Schaufel aus Joukowski-Profilschnitten.
// Strömung:  exakte 2D-Potentialströmung je Profilschnitt (Kutta-Bedingung erfüllt).
//            Das ist kein CFD-Ergebnis, aber physikalisch konsistent: c_p auf der Wand
//            und c_p in der Schnittebene stammen aus derselben Lösung.
// T_w und Zellqualität sind synthetisch (plausible Verteilung, keine Rechnung).
//
// Die Funktionen hier sind 1:1 im Fragment-Shader der Schnittebene gespiegelt.

import type { Dim } from './units';

export type FieldId = 'cp' | 'speed' | 'tw' | 'nonortho' | 'schlieren';

export interface Section {
  s: number;
  eps: number;
  mu: number;
  R: number;
  beta: number;
  scale: number;
  twist: number;
  cosT: number;
  sinT: number;
  /** lokaler Anstellwinkel = Anströmwinkel − Verwindung */
  alpha: number;
}

export const NS = 44; // Stationen über die Spannweite
export const NT = 144; // Punkte über den Profilumfang (gerade Zahl)
export const SPAN = 5.2;

export function sectionAt(s: number, alphaInf: number): Section {
  const eps = 0.16 - 0.05 * s;
  const mu = 0.3 - 0.1 * s;
  const R = Math.hypot(1 + eps, mu);
  const beta = Math.asin(mu / R);
  const scale = 0.8 * (1 - 0.24 * s);
  const twist = ((-4 + 12 * s) * Math.PI) / 180;
  return { s, eps, mu, R, beta, scale, twist, cosT: Math.cos(twist), sinT: Math.sin(twist), alpha: alphaInf - twist };
}

export function localToWorld(sec: Section, x: number, y: number): [number, number] {
  return [(x * sec.cosT - y * sec.sinT) * sec.scale, (x * sec.sinT + y * sec.cosT) * sec.scale];
}

export function worldToLocal(sec: Section, X: number, Z: number): [number, number] {
  return [(X * sec.cosT + Z * sec.sinT) / sec.scale, (-X * sec.sinT + Z * sec.cosT) / sec.scale];
}

/** Punkt auf dem Profil (lokal) und Betrag der Wandgeschwindigkeit |U|/U∞ zum Kreiswinkel θ. */
export function surfaceAt(sec: Section, theta: number): { x: number; y: number; speed: number } {
  const zx = -sec.eps + sec.R * Math.cos(theta);
  const zy = sec.mu + sec.R * Math.sin(theta);
  const m = zx * zx + zy * zy;
  // dz/dζ = 1 − 1/ζ²
  const dx = 1 - (zx * zx - zy * zy) / (m * m);
  const dy = (2 * zx * zy) / (m * m);
  const dz = Math.hypot(dx, dy);
  const w = 2 * Math.abs(Math.sin(theta - sec.alpha) + Math.sin(sec.alpha + sec.beta));
  return { x: zx + zx / m, y: zy - zy / m, speed: Math.min(w / Math.max(dz, 1e-4), 8) };
}

/** c_p an einem lokalen Punkt außerhalb des Profils; null = im Profil. */
export function cpAt(sec: Section, x: number, y: number): number | null {
  // ζ aus z = ζ + 1/ζ  →  ζ = (z ± √(z² − 4)) / 2, gewählt wird die Wurzel außerhalb des Kreises
  const ax = x * x - y * y - 4;
  const ay = 2 * x * y;
  const r = Math.sqrt(Math.hypot(ax, ay));
  const ph = Math.atan2(ay, ax) * 0.5;
  const sx = r * Math.cos(ph);
  const sy = r * Math.sin(ph);
  const cx = -sec.eps;
  const cy = sec.mu;
  let zx = 0.5 * (x + sx);
  let zy = 0.5 * (y + sy);
  const bx = 0.5 * (x - sx);
  const by = 0.5 * (y - sy);
  if (Math.hypot(bx - cx, by - cy) > Math.hypot(zx - cx, zy - cy)) {
    zx = bx;
    zy = by;
  }
  const dx = zx - cx;
  const dy = zy - cy;
  if (Math.hypot(dx, dy) < sec.R - 1e-4) return null;
  const ca = Math.cos(sec.alpha);
  const sa = Math.sin(sec.alpha);
  // d² und 1/d², 1/d
  const d2x = dx * dx - dy * dy;
  const d2y = 2 * dx * dy;
  const d2m = d2x * d2x + d2y * d2y;
  const dm = dx * dx + dy * dy;
  const R2 = sec.R * sec.R;
  // w' = e^{-iα} − R² e^{iα}/d² + i·2R·sin(α+β)/d
  const g = 2 * sec.R * Math.sin(sec.alpha + sec.beta);
  const wx = ca - (R2 * (ca * d2x + sa * d2y)) / d2m + (g * dy) / dm;
  const wy = -sa - (R2 * (sa * d2x - ca * d2y)) / d2m + (g * dx) / dm;
  // dz/dζ = 1 − 1/ζ²
  const m = zx * zx + zy * zy;
  const ex = 1 - (zx * zx - zy * zy) / (m * m);
  const ey = (2 * zx * zy) / (m * m);
  const sp = Math.min(Math.hypot(wx, wy) / Math.max(Math.hypot(ex, ey), 1e-4), 8);
  return 1 - sp * sp;
}

const FD_H = 0.012;

/** Wert des gewählten Felds in der Schnittebene (Weltkoordinaten X, Z). */
export function sliceValue(field: FieldId, sec: Section, X: number, Z: number): number | null {
  const [x, y] = worldToLocal(sec, X, Z);
  const cp = cpAt(sec, x, y);
  if (cp === null) return null;
  if (field === 'cp') return cp;
  if (field === 'speed') return Math.sqrt(Math.max(0, 1 - cp));
  if (field === 'schlieren') {
    const a = cpAt(sec, x + FD_H, y);
    const b = cpAt(sec, x, y + FD_H);
    const gx = a === null ? 0 : (a - cp) / FD_H;
    const gy = b === null ? 0 : (b - cp) / FD_H;
    return Math.hypot(gx, gy) * 4; // × Sehnenlänge (lokal ≈ 4)
  }
  return null;
}

// -------------------------------------------------------------- Schaufel ----

export interface Blade {
  positions: Float32Array;
  indices: Uint32Array;
  /** Anzahl der Mantel-Knoten; dahinter folgen die Deckel-Knoten der Schaufelspitze */
  nSurf: number;
  /** je Deckel-Knoten: Index des zugehörigen Mantel-Knotens */
  capSrc: Uint32Array;
  /** 1 = Zelle für die NaN-Demo markiert */
  nan: Float32Array;
  nanCount: number;
  /** Liniensegmente des ausgedünnten Oberflächennetzes */
  edges: Float32Array;
}

const thetaAt = (sec: Section, j: number) => -sec.beta + (2 * Math.PI * j) / NT;

export function buildBlade(): Blade {
  const nSurf = (NS + 1) * NT;
  const nCap = NT;
  const positions = new Float32Array((nSurf + nCap) * 3);
  const nan = new Float32Array(nSurf + nCap);
  const capSrc = new Uint32Array(nCap);
  let nanCount = 0;

  for (let i = 0; i <= NS; i++) {
    const s = i / NS;
    const sec = sectionAt(s, 0);
    for (let j = 0; j < NT; j++) {
      const p = surfaceAt(sec, thetaAt(sec, j));
      const [X, Z] = localToWorld(sec, p.x, p.y);
      const k = (i * NT + j) * 3;
      positions[k] = X;
      positions[k + 1] = s * SPAN;
      positions[k + 2] = Z;
      const u = j / NT;
      if (Math.abs(s - 0.66) < 0.04 && Math.abs(u - 0.3) < 0.016) {
        nan[i * NT + j] = 1;
        nanCount++;
      }
    }
  }
  for (let j = 0; j < NT; j++) {
    const src = NS * NT + j;
    capSrc[j] = src;
    positions.set(positions.subarray(src * 3, src * 3 + 3), (nSurf + j) * 3);
  }

  const idx: number[] = [];
  for (let i = 0; i < NS; i++) {
    for (let j = 0; j < NT; j++) {
      const a = i * NT + j;
      const b = i * NT + ((j + 1) % NT);
      const c = (i + 1) * NT + j;
      const d = (i + 1) * NT + ((j + 1) % NT);
      idx.push(a, b, d, a, d, c);
    }
  }
  // Deckel: Saug- und Druckseite paarweise verbinden (Profil ist nicht konvex)
  const capFrom = idx.length;
  const cv = (j: number) => nSurf + (j % NT);
  idx.push(cv(0), cv(1), cv(NT - 1));
  for (let j = 1; j < NT / 2; j++) {
    idx.push(cv(j), cv(j + 1), cv(NT - j));
    if (j + 1 !== NT - j - 1) idx.push(cv(j + 1), cv(NT - j - 1), cv(NT - j));
  }

  // Orientierung prüfen: Mantel so drehen, dass die Normalen nach außen zeigen
  let vol = 0;
  for (let t = 0; t < capFrom; t += 3) {
    const a = idx[t] * 3;
    const b = idx[t + 1] * 3;
    const c = idx[t + 2] * 3;
    vol +=
      positions[a] * (positions[b + 1] * positions[c + 2] - positions[b + 2] * positions[c + 1]) -
      positions[a + 1] * (positions[b] * positions[c + 2] - positions[b + 2] * positions[c]) +
      positions[a + 2] * (positions[b] * positions[c + 1] - positions[b + 1] * positions[c]);
  }
  if (vol < 0) {
    for (let t = 0; t < capFrom; t += 3) {
      const tmp = idx[t + 1];
      idx[t + 1] = idx[t + 2];
      idx[t + 2] = tmp;
    }
  }
  // Deckel: Normale soll nach +Y zeigen
  {
    const a = idx[capFrom + 3] * 3;
    const b = idx[capFrom + 4] * 3;
    const c = idx[capFrom + 5] * 3;
    const ny =
      (positions[b + 2] - positions[a + 2]) * (positions[c] - positions[a]) -
      (positions[b] - positions[a]) * (positions[c + 2] - positions[a + 2]);
    if (ny < 0) {
      for (let t = capFrom; t < idx.length; t += 3) {
        const tmp = idx[t + 1];
        idx[t + 1] = idx[t + 2];
        idx[t + 2] = tmp;
      }
    }
  }

  // Ausgedünntes Oberflächennetz als Linien
  const e: number[] = [];
  const push = (a: number, b: number) => {
    e.push(positions[a * 3], positions[a * 3 + 1], positions[a * 3 + 2], positions[b * 3], positions[b * 3 + 1], positions[b * 3 + 2]);
  };
  for (let i = 0; i <= NS; i += 2) for (let j = 0; j < NT; j++) push(i * NT + j, i * NT + ((j + 1) % NT));
  for (let j = 0; j < NT; j += 3) for (let i = 0; i < NS; i++) push(i * NT + j, (i + 1) * NT + j);

  return { positions, indices: new Uint32Array(idx), nSurf, capSrc, nan, nanCount, edges: new Float32Array(e) };
}

// ----------------------------------------------------------- Wandfelder ----

export interface FlowState {
  /** Anströmwinkel in rad */
  alphaInf: number;
  /** Totaltemperatur am Einlass in K */
  tInlet: number;
  /** 0 = Startlösung, 1 = auskonvergiert */
  progress: number;
}

const hash = (i: number, j: number) => {
  const v = Math.sin(i * 12.9898 + j * 78.233) * 43758.5453;
  return v - Math.floor(v);
};
const smooth = (a: number, b: number, x: number) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};

/** Füllt `out` mit dem Wandwert des Felds je Knoten. Gibt false zurück, wenn das Feld keine Wandwerte hat. */
export function bladeScalars(blade: Blade, field: FieldId, st: FlowState, out: Float32Array): boolean {
  if (field === 'schlieren') return false;
  for (let i = 0; i <= NS; i++) {
    const s = i / NS;
    const sec = sectionAt(s, st.alphaInf);
    const row = i * NT;
    for (let j = 0; j < NT; j++) {
      const u = j / NT; // 0 = Hinterkante, 0.5 ≈ Vorderkante; u < 0.5 Saugseite
      const sp = surfaceAt(sec, thetaAt(sec, j)).speed;
      const cp = 1 - sp * sp;
      let v = 0;
      if (field === 'cp') v = cp;
      else if (field === 'speed') v = sp;
      else if (field === 'tw') {
        const stag = smooth(0.15, 1, cp);
        const ps = smooth(0.52, 0.62, u);
        let film = 0;
        const holes = 0.5 + 0.5 * Math.cos(2 * Math.PI * s * 18);
        // zwei Filmkühlreihen auf der Saugseite (stromab = kleineres u), eine auf der Druckseite
        for (const ur of [0.43, 0.3]) {
          film += 80 * Math.exp(-(((u - ur) / 0.011) ** 2)) * holes;
          if (u < ur) film += 42 * Math.exp(-(ur - u) / 0.07);
        }
        film += 80 * Math.exp(-(((u - 0.6) / 0.011) ** 2)) * holes;
        if (u > 0.6) film += 40 * Math.exp(-(u - 0.6) / 0.09);
        // Umschlag auf der Saugseite hebt den Wärmeübergang lokal an
        const trans = Math.exp(-(((u - 0.2) / 0.07) ** 2));
        const final = st.tInlet * (0.6 + 0.145 * stag + 0.115 * Math.pow(s, 1.3) + 0.03 * ps + 0.035 * trans) - film;
        v = 700 + (final - 700) * st.progress;
      } else if (field === 'nonortho') {
        const te = Math.min(u, 1 - u);
        v =
          12 +
          6 * hash(i, j) +
          36 * Math.exp(-te / 0.022) +
          24 * Math.exp(-s / 0.05) +
          18 * Math.exp(-(1 - s) / 0.045) +
          11 * Math.exp(-Math.abs(u - 0.5) / 0.02) +
          26 * Math.exp(-(((s - 0.9) / 0.05) ** 2 + ((u - 0.05) / 0.03) ** 2));
        v = Math.min(88, Math.max(2, v));
      }
      out[row + j] = v;
    }
    // Hinterkante: Grenzwert aus den Nachbarn (0/0 an der Spitze)
    if (field === 'cp' || field === 'speed' || field === 'tw') out[row] = 0.5 * (out[row + 1] + out[row + NT - 1]);
  }
  for (let j = 0; j < blade.capSrc.length; j++) out[blade.nSurf + j] = out[blade.capSrc[j]];
  return true;
}

// ------------------------------------------------------ Felddefinitionen ----


export interface FieldDef {
  id: FieldId;
  symbol: string;
  name: string;
  unit: string;
  dim: Dim;
  /** wo das Feld vorliegt: Volumen + Wand, nur Wand (boundaryField) oder nur Schnitt */
  where: 'volume' | 'wall' | 'slice';
  defaultMap: string;
  range: [number, number];
  signed: boolean;
  digits: number;
  /** woher die Demo-Daten stammen */
  demo: string;
  /** OpenFOAM-Feld, das später an diese Stelle tritt */
  foam: string;
}

export const FIELDS: FieldDef[] = [
  {
    id: 'cp',
    symbol: 'c_p',
    name: 'Druckbeiwert',
    unit: '–',
    dim: 'ratio',
    where: 'volume',
    defaultMap: 'smooth-cool-warm',
    range: [-2.5, 2.5],
    signed: true,
    digits: 2,
    demo: 'Potentialströmung je Profilschnitt (analytisch)',
    foam: 'p',
  },
  {
    id: 'speed',
    symbol: '|U|/U∞',
    name: 'Geschwindigkeit',
    unit: '–',
    dim: 'ratio',
    where: 'volume',
    defaultMap: 'aero',
    range: [0, 2],
    signed: false,
    digits: 2,
    demo: 'Potentialströmung je Profilschnitt (analytisch)',
    foam: 'U',
  },
  {
    id: 'tw',
    symbol: 'T_w',
    name: 'Wandtemperatur',
    unit: 'K',
    dim: 'temperature',
    where: 'wall',
    defaultMap: 'inferno',
    range: [700, 1300],
    signed: false,
    digits: 0,
    demo: 'synthetische Verteilung (Staupunkt, Spitze, Filmkühlreihen)',
    foam: 'T · boundaryField',
  },
  {
    id: 'nonortho',
    symbol: 'nonOrtho',
    name: 'Nicht-Orthogonalität',
    unit: '°',
    dim: 'degree',
    where: 'wall',
    defaultMap: 'ampel',
    range: [0, 80],
    signed: false,
    digits: 0,
    demo: 'synthetische Verteilung (Hinterkante, Fuß, Spitze)',
    foam: 'nonOrthogonality',
  },
  {
    id: 'schlieren',
    symbol: '‖∇c_p‖·c',
    name: 'Numerisches Schlieren',
    unit: '–',
    dim: 'ratio',
    where: 'slice',
    defaultMap: 'schlieren',
    range: [0, 6],
    signed: false,
    digits: 1,
    demo: 'Gradient der Potentiallösung; im Solver später ‖∇ρ‖',
    foam: 'mag(grad(rho))',
  },
];

export const FIELD_BY_ID = Object.fromEntries(FIELDS.map((f) => [f.id, f])) as Record<FieldId, FieldDef>;

/** Ausdehnung der Schnittebene in Weltkoordinaten (X, Z). */
export const SLICE_BOUNDS = { x0: -3.3, x1: 4.3, z0: -2.5, z1: 2.5 };

/** Zellen der Schnittebene, die in der NaN-Demo aufleuchten (Zentrum in X, Z; Kantenlänge 0.14). */
export const SLICE_NAN_CELLS: [number, number][] = [
  [2.31, 0.77],
  [2.45, 0.77],
  [2.45, 0.91],
  [2.59, 0.91],
];
export const SLICE_NAN_HALF = 0.07;

/** Anzahl der Zellen, die die NaN-Demo markiert (Wand + Schnittebene). */
export function nanDemoCount(): number {
  let n = SLICE_NAN_CELLS.length;
  for (let i = 0; i <= NS; i++) for (let j = 0; j < NT; j++) if (Math.abs(i / NS - 0.66) < 0.04 && Math.abs(j / NT - 0.3) < 0.016) n++;
  return n;
}
