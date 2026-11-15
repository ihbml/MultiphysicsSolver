import { analyze, COLORMAPS, pickSignal, SIGNAL_CANDIDATES } from './colormaps';
import { FIELDS } from './flow';
import { CATEGORIES, CHASSIS, STATE_COLORS, TRIADS } from './palettes';

/** Maschinenlesbare Spezifikation für den Rust-Theme- und LUT-Loader. */
export function buildPalettesJson() {
  return {
    schema: 'cockpit.palettes',
    version: '0.2.0',
    color_encoding: 'sRGB hex – beim Laden als sRGB interpretieren, nicht als linear',
    lut: { size: 256, bake_space: 'oklab', sampling: 'texel centre, clamp to edge', banding: 'in shader, before lookup' },
    notes: ['#11217 aus dem Briefing hatte fünf Stellen und wird als #111217 (Deep Obsidian) geführt.'],
    chassis: CHASSIS,
    triads: TRIADS.map((t) => ({ id: t.id, title: t.title, kind: t.kind, colors: t.swatches })),
    categories: [
      ...CATEGORIES.map((c) => ({ id: c.id, name: c.name, color: c.hex, color_name: c.colorName, scope: c.scope })),
      { id: 'flow', name: 'Strömung', color: '#DFE5EB', color_name: 'Cloud', scope: 'Einlass, Auslass, Wände (ohne Akzent)' },
    ],
    states: STATE_COLORS,
    signals: {
      nan_candidates: SIGNAL_CANDIDATES,
      nan_rule: 'per colormap: candidate with the largest minimum Oklab distance to the LUT',
      out_of_range: 'hatch pattern, not colour',
    },
    colormaps: COLORMAPS.map((m) => {
      const a = analyze(m.id);
      return {
        id: m.id,
        name: m.name,
        kind: m.kind,
        group: m.group,
        interpolation: m.interp,
        stops: m.stops,
        nan_color: pickSignal(m.id).hex,
        lightness: { shape: a.shape, min: +a.lmin.toFixed(1), max: +a.lmax.toFixed(1) },
        source: m.source,
        ...(m.cst ? { cfdpost_cst: m.cst } : {}),
        ...(m.note ? { note: m.note } : {}),
      };
    }),
    field_defaults: FIELDS.map((f) => ({
      field: f.foam,
      symbol: f.symbol,
      unit: f.unit,
      location: f.where,
      colormap: f.defaultMap,
      range: f.range,
      symmetric_about_zero: f.signed,
    })),
  };
}

export function downloadJson(filename: string, data: unknown) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}
