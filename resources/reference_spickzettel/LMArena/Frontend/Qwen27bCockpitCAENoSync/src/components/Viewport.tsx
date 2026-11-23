import { useEffect, useRef, useState } from "react";
import { Engine } from "../gl/engine";
import { useStore } from "../state/store";
import { FIELD_DEFS } from "../fields/analytics";
import { lutCss } from "../fields/colormaps";
import { PATCH_NAMES } from "../schema/registry";
import { rpc } from "../ipc/backend";

export const engineRef: { current: Engine | null } = { current: null };

const Q_LABELS = [
  { m: "skew", label: "SKEWNESS", unit: "[0,1]", x: "left-2", y: "top-2" },
  { m: "nonorth", label: "NON_ORTHO", unit: "[°]", x: "left-1/2 ml-2", y: "top-2" },
  { m: "aspect", label: "ASPECT_R", unit: "[–]", x: "left-2", y: "bottom-4" },
  { m: "vol", label: "CELL_VOL", unit: "[m³]", x: "left-1/2 ml-2", y: "bottom-4" },
] as const;

export function Viewport() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const ws = useStore((s) => s.ws);
  const view = useStore((s) => s.view);
  const setView = useStore((s) => s.setView);
  const params = useStore((s) => s.params);
  const selPatch = useStore((s) => s.selPatch);
  const hoverPatch = useStore((s) => s.hoverPatch);
  const alert = useStore((s) => s.alert);
  const nNaN = useStore((s) => s.nNaN);
  const [hud, setHud] = useState({ fps: 0, draws: 0, tris: 0 });

  useEffect(() => {
    const cv = canvasRef.current!;
    const eng = new Engine(cv);
    engineRef.current = eng;
    eng.start();
    const ro = new ResizeObserver(() => {
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      cv.width = Math.max(4, Math.floor(cv.clientWidth * dpr));
      cv.height = Math.max(4, Math.floor(cv.clientHeight * dpr));
    });
    ro.observe(cv);
    const iv = setInterval(() => {
      if (engineRef.current) setHud({ ...engineRef.current.stats });
    }, 500);
    return () => {
      ro.disconnect();
      clearInterval(iv);
      eng.destroy();
      engineRef.current = null;
    };
  }, []);

  // default mode per workspace
  useEffect(() => {
    if (ws === 5) setView({ mode: "field" });
    else if (ws !== 2) setView({ mode: ws === 1 ? "cad" : "cad" });
  }, [ws, setView]);

  const toPx = (e: React.PointerEvent | React.WheelEvent) => {
    const cv = canvasRef.current!;
    const r = cv.getBoundingClientRect();
    return [
      (e.clientX - r.left) * (cv.width / r.width),
      (e.clientY - r.top) * (cv.height / r.height),
    ] as const;
  };

  const fd = FIELD_DEFS.find((f) => f.id === params.field_id) ?? FIELD_DEFS[0];
  const bands = params.banding ? Number(params.bands) : 0;

  return (
    <div className={`relative flex-1 min-w-0 bg-[#0F1013] ${alert ? "alert-frame" : ""}`}>
      <canvas
        ref={canvasRef}
        className="absolute inset-0 w-full h-full cursor-crosshair touch-none"
        onContextMenu={(e) => e.preventDefault()}
        onPointerDown={(e) => {
          (e.target as HTMLCanvasElement).setPointerCapture(e.pointerId);
          const [x, y] = toPx(e);
          engineRef.current?.onDown(x, y, e.button);
        }}
        onPointerMove={(e) => {
          const [x, y] = toPx(e);
          engineRef.current?.onMove(x, y);
        }}
        onPointerUp={(e) => {
          const [x, y] = toPx(e);
          engineRef.current?.onUp(x, y, e.button);
        }}
        onWheel={(e) => engineRef.current?.onWheel(e.deltaY)}
      />

      {/* ── mode / shading controls */}
      <div className="absolute top-2 left-2 z-10 flex flex-col gap-1.5">
        {ws === 2 ? (
          <div className="mono text-[10px] px-2.5 py-1.5 rounded-sm bg-[#18181C]/90 border border-[#B9F500]/30 text-[#B9F500]">
            4-QUADRANT MeshErrorMap · location: cell · strictly flat (0 interpolation)
          </div>
        ) : (
          <div className="flex rounded-sm overflow-hidden border border-[#32333C] bg-[#18181C]/90">
            {(["cad", "field"] as const).map((m) => (
              <button
                key={m}
                onClick={() => setView({ mode: m })}
                className={`mono text-[10px] px-2.5 py-1.5 transition-colors ${
                  view.mode === m
                    ? m === "cad"
                      ? "bg-[#004AFD] text-white"
                      : "bg-[#FC75B4] text-[#111217]"
                    : "text-[#9DA0AA] hover:text-[#E8E9ED]"
                }`}
              >
                {m === "cad" ? "MODE 1 · CAD/MESH" : "MODE 2 · FIELD/POST"}
              </button>
            ))}
          </div>
        )}
        {ws !== 2 && view.mode === "cad" && (
          <div className="flex gap-1">
            {(["edl", "cavity", "flat"] as const).map((sh) => (
              <button
                key={sh}
                onClick={() => setView({ shade: sh })}
                className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                  view.shade === sh
                    ? "border-[#004AFD] text-[#7FA3FF] bg-[#004AFD]/10"
                    : "border-[#32333C] text-[#9DA0AA]"
                }`}
              >
                {sh === "edl" ? "MatCap+EDL" : sh === "cavity" ? "Cavity" : "Flat"}
              </button>
            ))}
            <button
              onClick={() => setView({ wire: !view.wire })}
              className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                view.wire ? "border-[#B9F500] text-[#B9F500] bg-[#B9F500]/10" : "border-[#32333C] text-[#9DA0AA]"
              }`}
            >
              wire
            </button>
          </div>
        )}
        {ws !== 2 && view.mode === "field" && (
          <div className="flex gap-1 flex-wrap">
            <button
              onClick={() => setView({ sliceOn: !view.sliceOn })}
              className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                view.sliceOn ? "border-[#B9F500] text-[#B9F500] bg-[#B9F500]/10" : "border-[#32333C] text-[#9DA0AA]"
              }`}
            >
              slice
            </button>
            {(["x", "y", "z"] as const).map((a) => (
              <button
                key={a}
                onClick={() => setView({ sliceAxis: a })}
                className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                  view.sliceOn && view.sliceAxis === a
                    ? "border-[#B9F500] text-[#B9F500] bg-[#B9F500]/10"
                    : "border-[#32333C] text-[#63666f]"
                }`}
              >
                {a.toUpperCase()}
              </button>
            ))}
            <button
              onClick={() => setView({ streamlines: !view.streamlines })}
              className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                view.streamlines ? "border-[#004AFD] text-[#7FA3FF] bg-[#004AFD]/10" : "border-[#32333C] text-[#63666f]"
              }`}
            >
              streamlines
            </button>
            <button
              onClick={() => setView({ lic: !view.lic })}
              className={`mono text-[9.5px] px-2 py-1 rounded-sm border ${
                view.lic ? "border-[#FC75B4] text-[#FC75B4] bg-[#FC75B4]/10" : "border-[#32333C] text-[#63666f]"
              }`}
            >
              surface-LIC
            </button>
          </div>
        )}
      </div>

      {/* ── colorbar / patch legend */}
      {ws !== 2 && view.mode === "field" ? (
        <div className="absolute top-2 right-2 z-10 w-[190px] bg-[#18181C]/90 border border-[#32333C] rounded-sm p-2">
          <div className="flex justify-between items-baseline">
            <span className="mono text-[10px] text-[#E8E9ED]">{fd.label}</span>
            <span className="mono text-[8.5px] text-[#63666f]">{fd.unit}</span>
          </div>
          <div
            className="mt-1.5 h-3 rounded-[2px] border border-[#00000066]"
            style={{ background: lutCss(params.cmap as never, bands) }}
          />
          <div className="mt-0.5 flex justify-between mono text-[9px] text-[#9DA0AA]">
            <span>{fd.range[0]}</span>
            {params.banding && (
              <span className="text-[#63666f]">{params.bands} bands</span>
            )}
            <span>{fd.range[1]}</span>
          </div>
        </div>
      ) : (
        ws !== 2 && (
          <div className="absolute top-2 right-2 z-10 flex flex-col items-end gap-1">
            {hoverPatch !== null && (
              <div className="mono text-[10px] px-2 py-1 rounded-sm bg-[#18181C]/90 border border-[#B9F500]/40 text-[#B9F500]">
                hover · {PATCH_NAMES[hoverPatch]} (g={hoverPatch})
              </div>
            )}
            {selPatch !== null && (
              <div className="mono text-[10px] px-2 py-1 rounded-sm bg-[#18181C]/90 border border-[#EF233C]/50 text-[#FF8095]">
                selected · {PATCH_NAMES[selPatch]} — WS3 to bind BC
              </div>
            )}
            <div className="mono text-[9px] px-2 py-1 rounded-sm bg-[#18181C]/70 border border-[#32333C] text-[#63666f]">
              <span className="text-[#EF233C]">—</span> open edges ·{" "}
              <span className="text-[#FC75B4]">—</span> non-manifold
            </div>
          </div>
        )
      )}

      {/* ── quadrant labels (WS2) */}
      {ws === 2 &&
        Q_LABELS.map((q) => (
          <div
            key={q.m}
            className={`absolute ${q.x} ${q.y} z-10 mono text-[10px] px-2 py-1 rounded-sm bg-[#18181C]/85 border ${
              params.metric === q.m ? "border-[#B9F500]/60 text-[#B9F500]" : "border-[#32333C] text-[#9DA0AA]"
            }`}
          >
            {q.label} <span className="text-[#63666f]">{q.unit}</span>
          </div>
        ))}

      {/* ── NaN emergency banner */}
      {alert && (
        <div className="absolute top-2 left-1/2 -translate-x-1/2 z-20 flex items-center gap-3 bg-[#2A0A10]/95 border border-[#EF233C] rounded-sm px-3 py-1.5">
          <span className="mono text-[11px] text-[#FF8095] blink">⚠ n_nan_cells = {nNaN}</span>
          <span className="mono text-[10px] text-[#C6C8CF]">residual divergence — red-pulsing cells in WS2</span>
          <button
            onClick={() => rpc.call("solver.abort").catch(() => {})}
            className="mono text-[10px] px-2 py-0.5 rounded-sm bg-[#EF233C] text-white hover:bg-[#FF3B52]"
          >
            solver.abort
          </button>
        </div>
      )}

      {/* ── HUD */}
      <div className="absolute bottom-1.5 left-2 z-10 mono text-[9px] leading-[1.5] text-[#63666f]">
        <div>
          <span className="text-[#9DA0AA]">{hud.fps}</span> fps · <span className="text-[#9DA0AA]">{hud.draws}</span>{" "}
          draws · <span className="text-[#9DA0AA]">{hud.tris.toLocaleString()}</span> tris
        </div>
        <div className="text-[#4A4B55]">
          reverse-Z ∞ · Depth32F·GEQUAL · f32 camera-relative ·{" "}
          {ws === 2 ? "flat facets" : view.mode === "cad" ? "phase2: MatCap+EDL" : "unlit · no tone-map"}
        </div>
      </div>
    </div>
  );
}
