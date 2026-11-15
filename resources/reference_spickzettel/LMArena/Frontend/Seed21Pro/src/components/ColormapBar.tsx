import { colormapSamples, type Colormap } from "../colormaps";

interface Props {
  cmap: Colormap;
  bands?: number; // set to e.g. 10 for iso-band (discrete) mode; undefined = continuous
  height?: number;
  showLabels?: boolean;
  labelMin?: string;
  labelMax?: string;
}

export function ColormapBar({
  cmap,
  bands,
  height = 44,
  showLabels = true,
  labelMin = "min",
  labelMax = "max",
}: Props) {
  const width = 560; // internal SVG viewBox width – scales via CSS

  // Build rect segments so we can show continuous OR discrete cleanly.
  const segments = bands ?? 128;
  const samples = colormapSamples(cmap.stops, bands ? bands : segments);
  const segW = width / samples.length;

  return (
    <div className="w-full">
      <div className="flex items-center gap-3">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="h-auto w-full rounded-md border border-white/10 shadow-inner shadow-black/40"
          preserveAspectRatio="none"
          style={{ display: "block" }}
        >
          {/* background checkerboard to visualise NaN / transparency if ever needed */}
          <defs>
            <pattern id="cb" width="8" height="8" patternUnits="userSpaceOnUse">
              <rect width="4" height="8" fill="#1a1a1e" />
              <rect x="4" width="4" height="8" fill="#232328" />
            </pattern>
          </defs>
          <rect width={width} height={height} fill="url(#cb)" />
          {samples.map((hex, i) => (
            <rect
              key={i}
              x={i * segW}
              y={0}
              width={segW + 0.5}
              height={height}
              fill={hex}
            />
          ))}
          {/* subtle outline */}
          <rect
            x={0.5}
            y={0.5}
            width={width - 1}
            height={height - 1}
            fill="none"
            stroke="rgba(255,255,255,0.08)"
          />
        </svg>
      </div>
      {showLabels && (
        <div className="mt-1 flex justify-between font-mono text-[11px] tabular-nums text-zinc-500">
          <span>{labelMin}</span>
          <span className="text-zinc-600">
            {bands ? `${bands} Iso-Bänder` : "stufenlos"}
          </span>
          <span>{labelMax}</span>
        </div>
      )}
    </div>
  );
}
