import { useEffect, useRef, useState } from "react";
import { useEngine } from "@/state/engine";
import { renderFrame, type ViewOptions } from "@/engine/render";
import { cameraBasis } from "@/engine/math3d";
import { PARTS } from "@/engine/parts";
import { cn } from "@/utils/cn";
import {
  IconAxes,
  IconCube,
  IconEdge,
  IconFit,
  IconGrid,
  IconSection,
  IconSpin,
  IconWire,
  IconXray,
} from "./Icons";

const clamp = (v: number, a: number, b: number) => Math.max(a, Math.min(b, v));
const fmt = (n: number, d = 0) => n.toFixed(d);

const GPU_FACTOR: Record<string, number> = { vulkan: 1, dx12: 0.88, metal: 1.24, gl: 3.1 };

export default function Viewport() {
  const {
    part,
    partId,
    setPartId,
    lod,
    setLod,
    backend,
    view,
    setView,
    section,
    setSection,
    selected,
    setSelected,
    hovered,
    setHovered,
    has,
    cam,
    stats,
    setStats,
  } = useEngine();

  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const partRef = useRef(part);
  const opts = useRef<ViewOptions>(null as unknown as ViewOptions);
  const hist = useRef<number[]>(Array.from({ length: 72 }, () => 7));
  const pick = useRef<{ x: number; y: number } | null>(null);
  const drag = useRef<{ mode: "orbit" | "pan"; x: number; y: number; moved: number } | null>(null);
  const hoverId = useRef<string | null>(null);
  const lastTouch = useRef(0);
  const [spin, setSpin] = useState(true);

  useEffect(() => {
    cam.current.spin = spin;
  }, [spin, cam]);

  partRef.current = part;
  opts.current = {
    shaded: view.shaded,
    edges: view.edges,
    wireframe: view.wireframe,
    grid: view.grid,
    gizmo: view.gizmo,
    xray: view.xray,
    pbr: has("render.pbr"),
    stress: has("analysis.fem"),
    gdt: has("overlay.gdt"),
    dims: has("tool.measure"),
    dxf: has("import.dxf"),
    section: section.on && has("tool.section_plane"),
    sectionAxis: section.axis,
    sectionPos: section.pos,
    hovered,
    selected,
    pick: pick.current,
    history: hist.current,
    backend: backend.id,
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    const wrap = wrapRef.current;
    if (!canvas || !wrap) return;
    const ctx = canvas.getContext("2d", { alpha: false });
    if (!ctx) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    let w = 800;
    let h = 500;
    let raf = 0;
    let last = performance.now();
    let acc = 0;
    let frames = 0;

    const resize = () => {
      const r = wrap.getBoundingClientRect();
      w = Math.max(280, Math.floor(r.width));
      h = Math.max(240, Math.floor(r.height));
      canvas.width = Math.floor(w * dpr);
      canvas.height = Math.floor(h * dpr);
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    const ro = new ResizeObserver(resize);
    ro.observe(wrap);
    resize();

    const loop = (now: number) => {
      raf = requestAnimationFrame(loop);
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      const c = cam.current;
      if (c.spin && now - lastTouch.current > 2400) c.yaw += dt * 0.14;
      c.pitch = clamp(c.pitch, -1.45, 1.45);
      c.dist = clamp(c.dist, 3.5, 34);

      const t0 = performance.now();
      const st = renderFrame(
        ctx,
        w,
        h,
        partRef.current,
        { target: c.target, yaw: c.yaw, pitch: c.pitch, dist: c.dist, fov: 40 },
        opts.current,
      );
      const cpu = performance.now() - t0;
      hist.current.push(cpu);
      if (hist.current.length > 72) hist.current.shift();

      if (st.picked !== hoverId.current) {
        hoverId.current = st.picked;
        setHovered(st.picked);
      }

      acc += dt;
      frames++;
      if (acc >= 0.25) {
        const fps = frames / acc;
        const gpu = (cpu * 0.52 + partRef.current.solids.length * 0.05) * GPU_FACTOR[backend.id];
        setStats({
          ...st,
          fps,
          cpuMs: cpu,
          gpuMs: gpu + Math.sin(now / 700) * 0.12,
        });
        acc = 0;
        frames = 0;
      }
    };
    raf = requestAnimationFrame(loop);
    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [backend.id]);

  /* wheel zoom needs a non-passive listener */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      lastTouch.current = performance.now();
      const c = cam.current;
      c.dist = clamp(c.dist * Math.exp(e.deltaY * 0.0011), 3.5, 34);
    };
    canvas.addEventListener("wheel", onWheel, { passive: false });
    return () => canvas.removeEventListener("wheel", onWheel);
  }, [cam]);

  const onDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    lastTouch.current = performance.now();
    drag.current = {
      mode: e.button === 2 || e.shiftKey ? "pan" : "orbit",
      x: e.clientX,
      y: e.clientY,
      moved: 0,
    };
  };

  const onMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    pick.current = { x: e.clientX - r.left, y: e.clientY - r.top };
    lastTouch.current = performance.now();
    const d = drag.current;
    if (!d) return;
    const dx = e.clientX - d.x;
    const dy = e.clientY - d.y;
    d.x = e.clientX;
    d.y = e.clientY;
    d.moved += Math.abs(dx) + Math.abs(dy);
    const c = cam.current;
    if (d.mode === "orbit") {
      c.yaw -= dx * 0.0075;
      c.pitch += dy * 0.007;
    } else {
      const b = cameraBasis({ target: c.target, yaw: c.yaw, pitch: c.pitch, dist: c.dist, fov: 40 });
      const k = c.dist * 0.0016;
      c.target = [
        c.target[0] + (-b.xAxis[0] * dx + b.yAxis[0] * dy) * k,
        c.target[1] + (-b.xAxis[1] * dx + b.yAxis[1] * dy) * k,
        c.target[2] + (-b.xAxis[2] * dx + b.yAxis[2] * dy) * k,
      ];
    }
  };

  const onUp = () => {
    const d = drag.current;
    drag.current = null;
    if (d && d.moved < 5) setSelected(hoverId.current);
  };

  const fit = () => {
    const c = cam.current;
    c.yaw = part.home.yaw;
    c.pitch = part.home.pitch;
    c.dist = part.home.dist;
    c.target = [...part.home.target] as [number, number, number];
    lastTouch.current = performance.now();
  };

  const railBtn = (
    active: boolean,
    label: string,
    onClick: () => void,
    icon: React.ReactNode,
    disabled = false,
  ) => (
    <button
      type="button"
      onClick={onClick}
      disabled={disabled}
      title={label}
      className={cn(
        "group relative flex h-9 w-9 items-center justify-center border transition-all duration-150",
        active
          ? "border-amber-signal/60 bg-amber-signal/15 text-amber-signal"
          : "border-transparent text-ink-400 hover:border-ink-600 hover:bg-ink-800 hover:text-paper-100",
        disabled && "cursor-not-allowed opacity-30 hover:border-transparent hover:bg-transparent",
      )}
    >
      {icon}
      <span className="pointer-events-none absolute left-11 z-20 hidden whitespace-nowrap border border-ink-600 bg-ink-900/95 px-2 py-1 font-mono text-[10px] tracking-wide text-paper-200 group-hover:block">
        {label}
      </span>
    </button>
  );

  const needsSection = has("tool.section_plane");
  const needsStep = has("geom.step_kernel");

  return (
    <div ref={wrapRef} className="relative h-full w-full overflow-hidden bg-ink-950">
      <canvas
        ref={canvasRef}
        className="absolute inset-0 block touch-none select-none"
        style={{ cursor: drag.current ? "grabbing" : hovered ? "pointer" : "grab" }}
        onPointerDown={onDown}
        onPointerMove={onMove}
        onPointerUp={onUp}
        onPointerCancel={onUp}
        onPointerLeave={() => {
          pick.current = null;
          drag.current = null;
          setHovered(null);
        }}
        onContextMenu={(e) => e.preventDefault()}
      />

      {/* ambient chrome */}
      <div className="pointer-events-none absolute inset-0 rail-grid opacity-[0.06]" />
      <div className="pointer-events-none absolute inset-x-0 top-0 h-36 bg-gradient-to-b from-ink-950/80 to-transparent" />
      <div className="pointer-events-none absolute inset-x-0 bottom-0 h-48 bg-gradient-to-t from-ink-950/90 to-transparent" />
      <div className="pointer-events-none absolute inset-0 shadow-[inset_0_0_140px_rgba(0,0,0,0.8)]" />
      <div className="pointer-events-none absolute left-3 top-3 h-4 w-4 border-l border-t border-amber-signal/40" />
      <div className="pointer-events-none absolute right-3 top-3 h-4 w-4 border-r border-t border-amber-signal/40" />
      <div className="pointer-events-none absolute bottom-3 left-3 h-4 w-4 border-b border-l border-amber-signal/40" />

      {/* floating tool rail */}
      <div className="absolute left-3 top-1/2 z-10 -translate-y-1/2 border border-ink-700 bg-ink-900/85 p-1 backdrop-blur-sm">
        {railBtn(view.shaded, "Shaded faces", () => setView({ shaded: !view.shaded }), <IconCube />)}
        {railBtn(view.edges, "Hard edges", () => setView({ edges: !view.edges }), <IconEdge />)}
        {railBtn(view.wireframe, "Wireframe", () => setView({ wireframe: !view.wireframe }), <IconWire />)}
        {railBtn(view.xray, "X-Ray (no cull)", () => setView({ xray: !view.xray }), <IconXray />)}
        <div className="my-1 h-px bg-ink-700" />
        {railBtn(view.grid, "Build plane grid", () => setView({ grid: !view.grid }), <IconGrid />)}
        {railBtn(view.gizmo, "Axis gizmo", () => setView({ gizmo: !view.gizmo }), <IconAxes />)}
        {railBtn(
          section.on && needsSection,
          needsSection ? "Section plane" : "Section plane — needs tool.section_plane",
          () => setSection({ on: !section.on }),
          <IconSection />,
          !needsSection,
        )}
        <div className="my-1 h-px bg-ink-700" />
        {railBtn(
          spin,
          spin ? "Idle orbit: on" : "Idle orbit: off",
          () => setSpin((s) => !s),
          <IconSpin />,
        )}
        {railBtn(false, "Fit to view", fit, <IconFit />)}
      </div>

      {/* top-left: document tabs + tessellation */}
      <div className="absolute left-16 top-4 z-10 flex max-w-[calc(100%-5.5rem)] flex-col gap-2">
        <div className="thin-scroll flex items-stretch overflow-x-auto border border-ink-700 bg-ink-900/85 backdrop-blur-sm">
          {PARTS.map((p) => (
            <button
              key={p.id}
              type="button"
              onClick={() => setPartId(p.id)}
              className={cn(
                "group relative px-3 py-1.5 text-left transition-colors",
                p.id === partId ? "bg-ink-800" : "hover:bg-ink-850",
              )}
            >
              <span
                className={cn(
                  "absolute inset-y-0 left-0 w-[2px] transition-colors",
                  p.id === partId ? "bg-amber-signal" : "bg-transparent group-hover:bg-ink-600",
                )}
              />
              <span
                className={cn(
                  "block font-display text-[13px] font-bold leading-none tracking-wide",
                  p.id === partId ? "text-amber-signal" : "text-paper-200",
                )}
              >
                {p.code}
              </span>
              <span className="mt-0.5 block font-mono text-[9px] uppercase tracking-[0.14em] text-ink-400">
                {p.name}
              </span>
            </button>
          ))}
        </div>

        <div className="flex items-center gap-2 border border-ink-700 bg-ink-900/85 px-2 py-1 font-mono text-[10px] uppercase tracking-[0.12em] text-ink-400 backdrop-blur-sm">
          <span>tess</span>
          <div className="flex">
            {["Coarse", "Standard", "Fine"].map((label, i) => (
              <button
                key={label}
                type="button"
                disabled={!needsStep}
                onClick={() => setLod(i)}
                className={cn(
                  "border px-1.5 py-0.5 transition-colors",
                  lod === i && needsStep
                    ? "border-cyan-signal/50 bg-cyan-signal/15 text-cyan-signal"
                    : "border-ink-700 text-ink-400 hover:text-paper-200",
                  !needsStep && "cursor-not-allowed opacity-40",
                )}
              >
                {label}
              </button>
            ))}
          </div>
          {!needsStep && <span className="text-red-signal/80">geom.step_kernel unloaded</span>}
        </div>
      </div>

      {/* bottom-left: live stats */}
      <div className="absolute bottom-4 left-16 z-10 hidden border border-ink-700 bg-ink-900/85 px-3 py-2 font-mono text-[10px] leading-[1.7] backdrop-blur-sm sm:block">
        <div className="mb-1 flex items-center gap-2">
          <span
            className="inline-block h-1.5 w-1.5 animate-blink rounded-full"
            style={{ background: backend.color }}
          />
          <span className="text-[9px] uppercase tracking-[0.2em] text-ink-400">
            pipeline · {backend.short}
          </span>
        </div>
        <div className="grid grid-cols-[auto_auto] gap-x-4 gap-y-0">
          <Stat k="fps" v={fmt(stats.fps, 0)} accent={stats.fps > 50 ? "#8ede63" : "#f5a524"} />
          <Stat k="frame" v={`${fmt(stats.ms, 2)} ms`} />
          <Stat k="tris" v={stats.tris.toLocaleString("en-US")} />
          <Stat k="drawn" v={stats.drawn.toLocaleString("en-US")} />
          <Stat k="culled" v={stats.culled.toLocaleString("en-US")} />
          <Stat k="clipped" v={stats.clipped.toLocaleString("en-US")} />
          <Stat k="gpu" v={`${fmt(stats.gpuMs, 2)} ms`} accent="#35c9e8" />
          <Stat k="sel" v={selected ?? "—"} accent={selected ? "#f5a524" : undefined} />
        </div>
      </div>

      {/* bottom-centre: section control */}
      {section.on && needsSection && (
        <div className="absolute bottom-4 left-1/2 z-10 flex -translate-x-1/2 items-center gap-2 border border-amber-signal/30 bg-ink-900/90 px-2 py-2 backdrop-blur-sm sm:gap-3 sm:px-3">
          <span className="font-mono text-[9px] uppercase tracking-[0.2em] text-amber-signal">
            section
          </span>
          <div className="flex">
            {(["x", "y", "z"] as const).map((a) => (
              <button
                key={a}
                type="button"
                onClick={() => setSection({ axis: a })}
                className={cn(
                  "h-6 w-6 border font-mono text-[10px] uppercase transition-colors",
                  section.axis === a
                    ? "border-amber-signal/60 bg-amber-signal/20 text-amber-signal"
                    : "border-ink-700 text-ink-400 hover:text-paper-200",
                )}
              >
                {a}
              </button>
            ))}
          </div>
          <input
            type="range"
            min={part.sectionRange[0]}
            max={part.sectionRange[1]}
            step={0.01}
            value={section.pos}
            onChange={(e) => setSection({ pos: parseFloat(e.target.value) })}
            className="h-1 w-24 cursor-pointer appearance-none rounded bg-ink-600 accent-[#f5a524] sm:w-40"
          />
          <span className="tabular hidden font-mono text-[10px] text-paper-200 sm:inline">
            {section.axis} = {fmt(section.pos, 2)}
          </span>
        </div>
      )}

      {/* bottom-right hint */}
      <div className="pointer-events-none absolute bottom-4 right-24 z-10 hidden text-right font-mono text-[9.5px] leading-relaxed text-ink-400 lg:block">
        <div>
          drag <span className="text-ink-500">orbit</span> · wheel{" "}
          <span className="text-ink-500">zoom</span> · shift+drag{" "}
          <span className="text-ink-500">pan</span> · click{" "}
          <span className="text-ink-500">select</span>
        </div>
        <div className="text-ink-500">
          {hovered ? (
            <span className="text-lime-signal">hover {hovered}</span>
          ) : (
            <span>{part.code} · {part.standard}</span>
          )}
        </div>
      </div>
    </div>
  );
}

function Stat({ k, v, accent }: { k: string; v: string; accent?: string }) {
  return (
    <>
      <span className="pr-2 text-right uppercase tracking-[0.12em] text-ink-500">{k}</span>
      <span className="tabular" style={{ color: accent ?? "#d7e2ec" }}>
        {v}
      </span>
    </>
  );
}
