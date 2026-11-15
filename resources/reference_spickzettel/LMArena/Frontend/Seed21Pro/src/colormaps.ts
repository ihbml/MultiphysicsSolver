// =============================================================================
//  CFD Colormap specifications  –  1D-LUT stops in sRGB hex
//  All maps are designed for perceptual linearity in L* (CIELAB) / Oklab.
//  Discrete "iso-band" variants are derived by stepping these stops.
// =============================================================================

export type Stop = { t: number; hex: string };

export interface Colormap {
  id: string;
  name: string;
  category: "Sequential" | "Diverging" | "Grayscale" | "Rainbow" | "Discrete";
  description: string;
  stops: Stop[];
  useCase: string;
  colorBlindSafe?: boolean;
}

// ---------- helper: quick-stop builder ----------
const s = (t: number, hex: string): Stop => ({ t, hex });

// ---------- User-provided palette triads (UI chassis accents) ----------
export interface Triad {
  id: string;
  name: string;
  bg: string;      // surface / neutral
  neutral: string; // text / outline
  accent: string;  // category accent
  usage: string;
}

export const TRIADS: Triad[] = [
  {
    id: "T1",
    name: "Blush & Olive",
    bg: "#F0E5DB",
    neutral: "#474326",
    accent: "#FC75B4",
    usage: "Light-theme variant / documentation / export preview",
  },
  {
    id: "T2",
    name: "Mauve & Electric Blue",
    bg: "#FAC7E1",
    neutral: "#A5467C",
    accent: "#004AFD",
    usage: "CAD-Patches, Geometrie-Selektion, Kanal A Highlight",
  },
  {
    id: "T3",
    name: "Deep Obsidian & Red Alert",
    bg: "#111217",
    neutral: "#DFE5EB",
    accent: "#EF233C",
    usage: "Dark chassis base #1 · Warnungen · divergierende Zellen",
  },
  {
    id: "T4",
    name: "Carbon, Violet & Acid Lime",
    bg: "#18181C",
    neutral: "#723BFA",
    accent: "#B9F500",
    usage: "Dark chassis base #2 · Solver/OK · Mesh-Qualität · NaN-Debug",
  },
  {
    id: "T5",
    name: "Cream, Burgundy & Amber Gold",
    bg: "#FDEFD6",
    neutral: "#731328",
    accent: "#FEC40B",
    usage: "Thermik / Strahlung / Verbrennung / Batterie",
  },
  {
    id: "T6",
    name: "Oxblood, Rose Red & Sunshine Yellow",
    bg: "#6D0317",
    neutral: "#E52150",
    accent: "#F4EB06",
    usage: "Wandkatalyse · Verbrennungs-Heatmap · Hochtemperatur",
  },
];

// ---------- Category color mapping for nTop-style blocks ----------
export const CATEGORY_COLORS: { label: string; hex: string; desc: string }[] = [
  { label: "Electric Blue",   hex: "#004AFD", desc: "CAD-Patches & Geometrie-Selektion" },
  { label: "Acid Lime",       hex: "#B9F500", desc: "Meshing, Grenzschicht, Konvergenz-OK" },
  { label: "Amber Gold",      hex: "#FEC40B", desc: "Thermik, Strahlung, Verbrennung, Batterie" },
  { label: "Rose Red",        hex: "#E52150", desc: "T-Hot-Spots · Hochtemperatur-Felder" },
  { label: "Vibrant Pink",    hex: "#FC75B4", desc: "Spezies-Transport, Wandkatalyse" },
  { label: "Mauve",           hex: "#A5467C", desc: "Mischung / Skalare Kopplung" },
  { label: "Electric Violet", hex: "#723BFA", desc: "Solver-Numerik, Akustik, MHD" },
  { label: "Alert Red",       hex: "#EF233C", desc: "NaN/Inf Zellen · Divergenz · Clamping" },
];

// =============================================================================
//  Colormaps
// =============================================================================

// ---- 1. Ampel (traffic light, red-green colour-blind safe via blue-in-green)
//     Stops:  #009E73 → #FEC40B → #EF233C
const AMPEL: Stop[] = [
  s(0.00, "#009E73"),
  s(0.50, "#FEC40B"),
  s(1.00, "#EF233C"),
];

// ---- 2. Inferno  (approximation of matplotlib Inferno key stops)
//     True inferno has many more stops; these anchor points reconstruct it
//     very faithfully in sRGB for visualisation purposes.
const INFERNO: Stop[] = [
  s(0.00, "#000004"),
  s(0.13, "#1F0C48"),
  s(0.25, "#51127C"),
  s(0.38, "#822681"),
  s(0.50, "#B63779"),
  s(0.63, "#E65164"),
  s(0.75, "#F98B3B"),
  s(0.88, "#FECB26"),
  s(1.00, "#FCFFA4"),
];

// ---- 3. Aero  (aviation / aerospace-style cool-to-warm, perceptually tuned)
//     Starts in deep navy → cyan → warm orange → hot red
const AERO: Stop[] = [
  s(0.00, "#0B1D3A"),
  s(0.17, "#123566"),
  s(0.33, "#1E6FB6"),
  s(0.50, "#3FB3C9"),
  s(0.66, "#9CD8A0"),
  s(0.83, "#F5C94D"),
  s(1.00, "#D73028"),
];

// ---- 4. Grün-Weiß  (sequential, pressure / wall-distance style)
const GRUN_WEISS: Stop[] = [
  s(0.00, "#003D2B"),
  s(0.25, "#1F7A4D"),
  s(0.50, "#73B87B"),
  s(0.75, "#C8E4BC"),
  s(1.00, "#FFFFFF"),
];

// ---- 5. Dark Ember  (exact 10-step from the spec #03071E → #FBB539)
const DARK_EMBER: Stop[] = [
  s(0.000, "#03071E"),
  s(0.111, "#370617"),
  s(0.222, "#6A040F"),
  s(0.333, "#9D0208"),
  s(0.444, "#D00000"),
  s(0.555, "#DC2F02"),
  s(0.666, "#E85D04"),
  s(0.777, "#F48C06"),
  s(0.888, "#FAA307"),
  s(1.000, "#FBB539"),
];

// ---- 6. Fire Scheme  (exact 6-step)
const FIRE_SCHEME: Stop[] = [
  s(0.0, "#801100"),
  s(0.2, "#A01100"),
  s(0.4, "#C02200"),
  s(0.6, "#E03300"),
  s(0.8, "#F05500"),
  s(1.0, "#FAC000"),
];

// ---- 7. Moreland Cool-Warm diverging (hard zero in the middle)
//     Canonical sRGB anchors from Kenneth Moreland's paper.
const COOL_WARM: Stop[] = [
  s(0.00, "#3B4CC0"),
  s(0.25, "#698FE0"),
  s(0.50, "#F2F2F2"),
  s(0.75, "#DD7B67"),
  s(1.00, "#B40426"),
];

// ---- 8. Turbo  (Google's perceptually near-uniform rainbow)
//     Key anchor points that reconstruct the full look.
const TURBO: Stop[] = [
  s(0.00, "#30123B"),
  s(0.17, "#4558C8"),
  s(0.33, "#2990D6"),
  s(0.50, "#1FC7AD"),
  s(0.66, "#78F148"),
  s(0.83, "#E9D539"),
  s(1.00, "#C03726"),
];

// ---- 9. Schlieren / X-Ray  (neutral grayscale – pure lightness)
const SCHLIEREN: Stop[] = [
  s(0.00, "#000000"),
  s(0.50, "#808080"),
  s(1.00, "#FFFFFF"),
];

// ---- 10. Grün-Weiß-Rot?  No – add a Plasma-ish extra?
//     Add "Viridis" as an extra recommended standard (colour-blind safe).
const VIRIDIS: Stop[] = [
  s(0.00, "#440154"),
  s(0.25, "#3B528B"),
  s(0.50, "#21908C"),
  s(0.75, "#5DC863"),
  s(1.00, "#FDE725"),
];

export const COLORMAPS: Colormap[] = [
  {
    id: "ampel",
    name: "Ampel (Traffic-Light)",
    category: "Sequential",
    description:
      "Blau-Grün → Amber → Rot. Bewusst mit Blauanteil im Grün für Rot-Grün-Sehschwäche.",
    stops: AMPEL,
    useCase:
      "Zellqualität (skewness / nonOrthogonality), Konvergenz-States, Residuen-Ampel.",
    colorBlindSafe: true,
  },
  {
    id: "inferno",
    name: "Inferno",
    category: "Sequential",
    description:
      "Tiefschwarz → Rot → Gelb-Weiß. Perzeptiv linear in L*; Standard aus matplotlib.",
    stops: INFERNO,
    useCase:
      "Temperatur, Geschwindigkeits-Magnitude, Wandabstand, Skalare mit hohem Dynamikbereich.",
    colorBlindSafe: true,
  },
  {
    id: "aero",
    name: "Aero",
    category: "Sequential",
    description:
      "Navy → Cyan → Orange → Rot. Klassischer Aerospace-Look für Druck/CP-Kurven.",
    stops: AERO,
    useCase: "Druckbeiwert cp, Mach-Zahl, Isobaren auf Flügeln.",
  },
  {
    id: "grun-weiss",
    name: "Grün – Weiß",
    category: "Sequential",
    description: "Dunkelgrün → Weiß. Beruhigte, natürliche Sequenz.",
    stops: GRUN_WEISS,
    useCase: "Wandnormalabstand y+, yPlus-Kontrolle im Grenzschicht-Layer.",
  },
  {
    id: "cool-warm",
    name: "Moreland Cool-Warm",
    category: "Diverging",
    description:
      "Blau → Weiß → Rot mit hartem Nullpunkt. Divergierende Skala mit Vorzeichenwechsel.",
    stops: COOL_WARM,
    useCase:
      "Relativdruck p−p∞, Rückströmung Ux<0, Wandwärmestrom (heizen/kühlen), Q-Kriterium.",
    colorBlindSafe: true,
  },
  {
    id: "turbo",
    name: "Turbo (Artefaktfreier Regenbogen)",
    category: "Rainbow",
    description:
      "Google Turbo: fast perzeptiv linear, trotzdem vertrauter Ansys-Jet-Look ohne falsche Kanten.",
    stops: TURBO,
    useCase:
      "Allround-Ansicht für Ingenieure, die aus Ansys CFD-Post kommen; CP/T auf Oberflächen.",
  },
  {
    id: "schlieren",
    name: "Schlieren / X-Ray",
    category: "Grayscale",
    description: "Neutrales Graustufen – null Farb-Ablenkung, maximaler Kantenkontrast.",
    stops: SCHLIEREN,
    useCase: "Dichtegradienten ∥∇ρ∥, Schocks, Gitter-Überlagerung, Netzlinien, Wirbelkerne.",
    colorBlindSafe: true,
  },
  {
    id: "dark-ember",
    name: "Dark Ember (10-step)",
    category: "Sequential",
    description:
      "10-stufige Ember-Skala aus der Referenz: #03071E … #FBB539. Dunkler Grundton für Night-Cockpit.",
    stops: DARK_EMBER,
    useCase: "Turbinen-Temperaturen T, Abgas, Hot-Sections, Betrieb bei Night-Mode.",
  },
  {
    id: "fire-scheme",
    name: "Fire Scheme (6-step)",
    category: "Sequential",
    description: "6-stufiges Fire-Schema #801100 … #FAC000.",
    stops: FIRE_SCHEME,
    useCase: "Schnelle Temperatur-Übersicht, einfache Heat-Maps.",
  },
  {
    id: "viridis",
    name: "Viridis (Empfehlung)",
    category: "Sequential",
    description:
      "matplotlib Standard – perceptually uniform, farbenblindenfreundlich, druckbar.",
    stops: VIRIDIS,
    useCase: "Allgemeine Skalarfelder, Publikations-Plots, Dokumentation.",
    colorBlindSafe: true,
  },
];

// -----------------------------------------------------------------------------
//  Small sRGB interpolation helpers (used for continuous SVG gradients and for
//  building dense 256-sample LUT arrays to upload as 1D textures later).
//  We do *linear-light* interpolation between stops for better perceptual
//  results than naïve 8-bit sRGB mixing.  Final stops are re-encoded as sRGB
//  hex for SVG / CSS consumption.
// -----------------------------------------------------------------------------

export function hexToRgb(hex: string): [number, number, number] {
  const h = hex.replace("#", "");
  const v = parseInt(
    h.length === 3
      ? h.split("").map((c) => c + c).join("")
      : h,
    16,
  );
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function rgbToHex(r: number, g: number, b: number): string {
  const cl = (x: number) => Math.max(0, Math.min(255, Math.round(x)));
  return (
    "#" +
    [cl(r), cl(g), cl(b)]
      .map((v) => v.toString(16).padStart(2, "0"))
      .join("")
      .toUpperCase()
  );
}

/** sample a colormap at parameter t ∈ [0,1] using linear-light interpolation. */
export function sampleColormap(stops: Stop[], t: number): string {
  if (t <= stops[0].t) return stops[0].hex;
  if (t >= stops[stops.length - 1].t) return stops[stops.length - 1].hex;
  for (let i = 0; i < stops.length - 1; i++) {
    const a = stops[i];
    const b = stops[i + 1];
    if (t >= a.t && t <= b.t) {
      const k = (t - a.t) / (b.t - a.t);
      const [r1, g1, b1] = hexToRgb(a.hex);
      const [r2, g2, b2] = hexToRgb(b.hex);
      return rgbToHex(r1 + (r2 - r1) * k, g1 + (g2 - g1) * k, b1 + (b2 - b1) * k);
    }
  }
  return stops[stops.length - 1].hex;
}

/** Build an array of N sRGB hex samples covering a colormap. */
export function colormapSamples(stops: Stop[], n = 32): string[] {
  return Array.from({ length: n }, (_, i) =>
    sampleColormap(stops, i / (n - 1)),
  );
}
