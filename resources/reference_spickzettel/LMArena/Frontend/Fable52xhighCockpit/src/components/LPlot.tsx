import { rgbToCss } from '../lib/color';
import { analyze, getLut, lutColor, MAP_BY_ID } from '../lib/colormaps';

/**
 * Helligkeitsverlauf L* einer Colormap, jeder Punkt in seiner eigenen Farbe –
 * derselbe Plot-Typ wie in cmap_ref_perceptual_lightness.png.
 */
export default function LPlot({ id, className }: { id: string; className?: string }) {
  const a = analyze(id);
  const lut = getLut(id);
  const W = 220;
  const H = 76;
  const px = 18;
  const py = 7;
  const x = (i: number) => px + (i / (a.L.length - 1)) * (W - px - 4);
  const y = (L: number) => py + (1 - L / 100) * (H - 2 * py);
  return (
    <svg
      viewBox={`0 0 ${W} ${H}`}
      className={className}
      role="img"
      aria-label={`Helligkeit L* von ${MAP_BY_ID[id]?.name ?? id}: ${a.verdict}, ${a.lmin.toFixed(0)} bis ${a.lmax.toFixed(0)}`}
    >
      {[0, 50, 100].map((L) => (
        <g key={L}>
          <line x1={px} x2={W - 4} y1={y(L)} y2={y(L)} stroke="var(--c-line)" strokeWidth="1" />
          <text x={px - 4} y={y(L) + 3.5} textAnchor="end" fontSize="10" fill="var(--c-ink3)" fontFamily="var(--font-mono)">
            {L}
          </text>
        </g>
      ))}
      {a.L.map((L, i) => (
        <circle
          key={i}
          cx={x(i)}
          cy={y(L)}
          r="2.2"
          fill={rgbToCss(lutColor(lut, i / (a.L.length - 1)))}
          stroke="rgba(255,255,255,0.28)"
          strokeWidth="0.4"
        />
      ))}
    </svg>
  );
}
