import type { Triad } from "../colormaps";

export function PaletteTriad({ triad }: { triad: Triad }) {
  const swatch = (hex: string, label: string, role: string) => {
    // Computed readable text color
    const rgb = hex.replace("#", "");
    const r = parseInt(rgb.substring(0, 2), 16);
    const g = parseInt(rgb.substring(2, 4), 16);
    const b = parseInt(rgb.substring(4, 6), 16);
    const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
    const textColor = lum > 0.55 ? "#0b0b0f" : "#f5f5f7";
    return (
      <div
        className="relative flex flex-1 flex-col justify-between p-3"
        style={{ backgroundColor: hex, color: textColor }}
      >
        <div className="font-mono text-[10px] uppercase tracking-wider opacity-70">
          {role}
        </div>
        <div className="font-mono text-sm font-semibold tabular-nums">
          {hex}
        </div>
        <div className="text-[10px] opacity-60">{label}</div>
      </div>
    );
  };

  return (
    <div className="overflow-hidden rounded-lg border border-white/10 bg-black/20 shadow-lg shadow-black/40">
      <div className="flex items-baseline justify-between border-b border-white/10 bg-black/30 px-3 py-2">
        <div className="flex items-baseline gap-2">
          <span className="font-mono text-[11px] text-zinc-500">{triad.id}</span>
          <h3 className="text-sm font-semibold text-zinc-100">{triad.name}</h3>
        </div>
      </div>
      <div className="flex h-28">
        {swatch(triad.bg, "Surface / BG", "bg")}
        {swatch(triad.neutral, "Text / Outline", "fg")}
        {swatch(triad.accent, "Accent", "accent")}
      </div>
      <div className="border-t border-white/5 bg-black/30 px-3 py-2 text-[11px] leading-relaxed text-zinc-400">
        {triad.usage}
      </div>
    </div>
  );
}
