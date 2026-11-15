// Die sechs Triaden aus dem Briefing und ihre Rollen im Cockpit.

export interface Swatch {
  hex: string;
  name: string;
  role: string;
}

export interface Triad {
  id: string;
  title: string;
  kind: 'chassis' | 'akzent' | 'report';
  summary: string;
  swatches: [Swatch, Swatch, Swatch];
}

export const TRIADS: Triad[] = [
  {
    id: 'T1',
    title: 'Bone · Olive · Pink',
    kind: 'report',
    summary: 'Helles Report-Thema „Papier“. Pink wandert als Kategorie- und Signalfarbe ins dunkle Cockpit.',
    swatches: [
      { hex: '#F0E5DB', name: 'Bone', role: 'Hintergrund Report-Thema' },
      { hex: '#474326', name: 'Olive Drab', role: 'Text auf hellen Themen' },
      { hex: '#FC75B4', name: 'Vibrant Pink', role: 'Kategorie Spezies · NaN-Signal' },
    ],
  },
  {
    id: 'T2',
    title: 'Blush · Mauve · Electric Blue',
    kind: 'akzent',
    summary: 'Liefert zwei Kategoriefarben. Blush bleibt Tint für Auswahlflächen in hellen Themen.',
    swatches: [
      { hex: '#FAC7E1', name: 'Blush', role: 'Weiche Auswahlfläche (hell)' },
      { hex: '#A5467C', name: 'Mauve', role: 'Kategorie Wandkatalyse' },
      { hex: '#004AFD', name: 'Electric Blue', role: 'Kategorie Geometrie · Selektion' },
    ],
  },
  {
    id: 'T3',
    title: 'Obsidian · Cloud · Signal Red',
    kind: 'chassis',
    summary: 'Chassis A. Neutrales Dunkel, helle Schrift, Rot ausschließlich für Fehler und Abbruch.',
    swatches: [
      { hex: '#111217', name: 'Deep Obsidian', role: 'Chassis A · Grundfläche' },
      { hex: '#DFE5EB', name: 'Cloud', role: 'Text, Linien, Zahlen' },
      { hex: '#EF233C', name: 'Signal Red', role: 'Fehler · Abbruch · Divergenz' },
    ],
  },
  {
    id: 'T4',
    title: 'Carbon · Violet · Acid Lime',
    kind: 'chassis',
    summary: 'Chassis B. Etwas wärmeres Dunkel mit Violett als Auswahlfarbe, Lime für „alles in Ordnung“.',
    swatches: [
      { hex: '#18181C', name: 'Carbon', role: 'Chassis B · Grundfläche' },
      { hex: '#723BFA', name: 'Electric Violet', role: 'Kategorie Numerik · Akustik · MHD' },
      { hex: '#B9F500', name: 'Acid Lime', role: 'Kategorie Meshing · Konvergenz OK' },
    ],
  },
  {
    id: 'T5',
    title: 'Cream · Burgundy · Amber Gold',
    kind: 'report',
    summary: 'Helles Report-Thema „Creme“. Amber Gold ist im Cockpit Thermik-Kategorie und Warnfarbe.',
    swatches: [
      { hex: '#FDEFD6', name: 'Cream', role: 'Hintergrund Report-Thema' },
      { hex: '#731328', name: 'Burgundy', role: 'Überschriften auf Creme' },
      { hex: '#FEC40B', name: 'Amber Gold', role: 'Kategorie Thermik · Warnung' },
    ],
  },
  {
    id: 'T6',
    title: 'Oxblood · Rose Red · Sunshine',
    kind: 'akzent',
    summary: 'Die lauteste Triade. Nur punktuell: kritische Banner, Verbrennung, Probe-Marker.',
    swatches: [
      { hex: '#6D0317', name: 'Oxblood', role: 'Fläche kritischer Banner' },
      { hex: '#E52150', name: 'Rose Red', role: 'Kategorie Verbrennung · Batterie' },
      { hex: '#F4EB06', name: 'Sunshine Yellow', role: 'Probe-Marker · Hinweis' },
    ],
  },
];

export type CategoryId = 'geometry' | 'mesh' | 'thermal' | 'combustion' | 'species' | 'catalysis' | 'numerics';

export interface Category {
  id: CategoryId;
  name: string;
  hex: string;
  colorName: string;
  scope: string;
}

/** Kategoriefarben der Physik-Blöcke (linker Rand im Block-Stack). */
export const CATEGORIES: Category[] = [
  { id: 'geometry', name: 'Geometrie', hex: '#004AFD', colorName: 'Electric Blue', scope: 'CAD-Patches, Flächenselektion' },
  { id: 'mesh', name: 'Meshing', hex: '#B9F500', colorName: 'Acid Lime', scope: 'Gitter, Grenzschicht-Check, Konvergenz OK' },
  { id: 'thermal', name: 'Thermik', hex: '#FEC40B', colorName: 'Amber Gold', scope: 'Wärmeübergang, Strahlung' },
  { id: 'combustion', name: 'Verbrennung', hex: '#E52150', colorName: 'Rose Red', scope: 'Verbrennung, Batterie' },
  { id: 'species', name: 'Spezies', hex: '#FC75B4', colorName: 'Vibrant Pink', scope: 'Spezies-Transport' },
  { id: 'catalysis', name: 'Wandkatalyse', hex: '#A5467C', colorName: 'Mauve', scope: 'Oberflächenreaktionen' },
  { id: 'numerics', name: 'Numerik', hex: '#723BFA', colorName: 'Electric Violet', scope: 'Solver-Numerik, Akustik, MHD' },
];

export const CAT_BY_ID = Object.fromEntries(CATEGORIES.map((c) => [c.id, c])) as Record<CategoryId, Category>;

export type ChassisId = 'obsidian' | 'carbon';

export const CHASSIS: Record<ChassisId, { name: string; triad: string; bg: string; panel: string; ink: string; select: string }> = {
  obsidian: { name: 'Obsidian', triad: 'T3', bg: '#111217', panel: '#171920', ink: '#DFE5EB', select: '#004AFD' },
  carbon: { name: 'Carbon', triad: 'T4', bg: '#18181C', panel: '#1E1E23', ink: '#E6E6EA', select: '#723BFA' },
};

export const STATE_COLORS = {
  ok: { hex: '#B9F500', name: 'Acid Lime', use: 'Konvergiert, Check bestanden' },
  warn: { hex: '#FEC40B', name: 'Amber Gold', use: 'Warnung, Grenzwert nahe' },
  error: { hex: '#EF233C', name: 'Signal Red', use: 'Fehler, Abbruch, Divergenz' },
  marker: { hex: '#F4EB06', name: 'Sunshine Yellow', use: 'Probe-Marker im Viewport' },
};

/** Alle Hintergründe, auf denen eine Colormap landen könnte (für den Interferenz-Vergleich). */
export const TRIAD_GROUNDS = TRIADS.map((t) => ({ id: t.id, hex: t.swatches[0].hex, name: t.swatches[0].name }));
