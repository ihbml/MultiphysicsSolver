import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  Box,
  Check,
  ChevronDown,
  Crosshair,
  Expand,
  Focus,
  Grid2X2,
  Hand,
  Maximize2,
  Minus,
  MousePointer2,
  Orbit,
  Plus,
  X,
} from "lucide-react";
import {
  CELL_TYPES,
  avg,
  cross,
  dot,
  integer,
  surface,
  unit,
  wallDistance,
} from "./mesh";
import type {
  Axis,
  ColorField,
  FaceData,
  MeshData,
  Tool,
  Vec3,
  ViewMode,
} from "./mesh";

interface Props {
  mesh: MeshData;
  axis: Axis;
  position: number;
  slice: boolean;
  flip: boolean;
  field: ColorField;
  edges: boolean;
  grid: boolean;
  plane: boolean;
  visible: number[];
  wallPatch: number | null;
  selected: number | null;
  onPick: (n: number) => void;
  onVisibility: (n: number) => void;
  resetKey: number;
  busy: boolean;
  setEdges: (v: boolean) => void;
}
type Camera = {
  yaw: number;
  pitch: number;
  zoom: number;
  panX: number;
  panY: number;
};
type Projected = { poly: number[][]; owner: number };
const HOME: Camera = { yaw: 1.04, pitch: 0.58, zoom: 1.0, panX: 0, panY: 0 };
const mix = (a: number[], b: number[], t: number) =>
  a.map((v, i) => v * (1 - t) + b[i] * t);
const color = (v: number[], k = 1) =>
  `rgb(${v.map((n) => Math.round(Math.min(255, Math.max(0, n * k)))).join(",")})`;
const palette: Record<number, number[]> = {
  8: [109, 139, 234],
  6: [237, 178, 88],
  5: [76, 190, 168],
  4: [177, 146, 220],
};
function qColor(q: number) {
  const stops = [
    [204, 104, 88],
    [221, 170, 85],
    [122, 195, 150],
    [67, 180, 199],
    [103, 143, 237],
  ];
  const a = Math.max(0, Math.min(3.999, q * 4)),
    i = Math.floor(a);
  return mix(stops[i], stops[i + 1], a - i);
}
function distanceColor(d: number) {
  const stops = [
    [59, 91, 158],
    [76, 147, 193],
    [100, 193, 190],
    [177, 209, 164],
    [236, 205, 130],
    [242, 174, 90],
    [222, 146, 75],
  ];
  const v = Math.min(5.9999, Math.max(0, d) * 6),
    i = Math.floor(v);
  return mix(stops[i], stops[i + 1], v - i);
}
function isOver(p: number[][], x: number, y: number) {
  let hit = false;
  for (let i = 0, j = p.length - 1; i < p.length; j = i++) {
    const a = p[i],
      b = p[j];
    if (
      a[1] > y !== b[1] > y &&
      x < ((b[0] - a[0]) * (y - a[1])) / (b[1] - a[1]) + a[0]
    )
      hit = !hit;
  }
  return hit;
}

export default function MeshViewport(props: Props) {
  const {
    mesh,
    axis,
    position,
    slice,
    flip,
    field,
    edges,
    grid,
    plane,
    visible,
    wallPatch,
    selected,
    onPick,
    onVisibility,
    resetKey,
    busy,
    setEdges,
  } = props;
  const canvas = useRef<HTMLCanvasElement>(null),
    stage = useRef<HTMLDivElement>(null),
    panel = useRef<HTMLElement>(null);
  const [size, setSize] = useState([800, 390]),
    [camera, setCamera] = useState<Camera>(HOME),
    [tool, setTool] = useState<Tool>("orbit"),
    [mode, setMode] = useState<ViewMode>("Solid + edges");
  const [modeMenu, setModeMenu] = useState(false),
    [expanded, setExpanded] = useState(false),
    [viewLabel, setViewLabel] = useState("Isometric");
  const [hover, setHover] = useState<{
    id: number;
    x: number;
    y: number;
  } | null>(null);
  const projected = useRef<Projected[]>([]),
    drag = useRef<{
      x: number;
      y: number;
      startX: number;
      startY: number;
      button: number;
    } | null>(null);
  const faces = useMemo(
    () => surface(mesh, slice, axis, position, flip, visible, wallPatch),
    [mesh, slice, axis, position, flip, visible, wallPatch],
  );
  const distances = useMemo(
    () =>
      field === "distance"
        ? mesh.cells.map((c) =>
            wallDistance(mesh, avg(c.map((i) => mesh.points[i]))),
          )
        : [],
    [mesh, field],
  );
  const reset = useCallback(() => {
    setCamera(HOME);
    setViewLabel("Isometric");
    setHover(null);
  }, []);
  useEffect(() => {
    reset();
    setTool("orbit");
    setMode("Solid + edges");
    setModeMenu(false);
  }, [resetKey, mesh.geometry, reset]);
  useEffect(() => {
    const el = stage.current;
    if (!el) return;
    const ob = new ResizeObserver(([e]) =>
      setSize([e.contentRect.width, e.contentRect.height]),
    );
    ob.observe(el);
    return () => ob.disconnect();
  }, []);
  useEffect(() => {
    const el = canvas.current;
    if (!el) return;
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      setCamera((c) => ({
        ...c,
        zoom: Math.max(0.4, Math.min(4, c.zoom * Math.exp(-e.deltaY * 0.0015))),
      }));
    };
    el.addEventListener("wheel", wheel, { passive: false });
    return () => el.removeEventListener("wheel", wheel);
  }, []);
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        setExpanded(false);
        setModeMenu(false);
        setHover(null);
      }
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, []);
  useEffect(() => {
    if (!expanded) return;
    const before = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    return () => {
      document.body.style.overflow = before;
    };
  }, [expanded]);
  useEffect(() => {
    if (!modeMenu) return;
    const close = () => setModeMenu(false);
    document.addEventListener("click", close);
    return () => document.removeEventListener("click", close);
  }, [modeMenu]);
  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const ctx = c.getContext("2d");
    if (!ctx) return;
    const [width, height] = size,
      dpr = Math.min(window.devicePixelRatio || 1, 2);
    c.width = Math.round(width * dpr);
    c.height = Math.round(height * dpr);
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);
    const { yaw, pitch, zoom, panX, panY } = camera;
    const view: Vec3 = mesh.geometry
      ? [
          Math.cos(yaw) * Math.cos(pitch),
          Math.sin(yaw) * Math.cos(pitch),
          Math.sin(pitch),
        ]
      : [
          Math.cos(yaw) * Math.cos(pitch),
          -Math.sin(pitch),
          Math.sin(yaw) * Math.cos(pitch),
        ];
    const right: Vec3 = mesh.geometry
        ? [-Math.sin(yaw), Math.cos(yaw), 0]
        : [-Math.sin(yaw), 0, Math.cos(yaw)],
      up = unit(cross(view, right));
    const extent = mesh.extent;
    // Keep the long wall horizontal for both benchmark orientations.
    let bounds = [Infinity, Infinity, -Infinity, -Infinity];
    const vertices: Vec3[] = [];
    for (const x of [0, extent[0]])
      for (const y of [0, extent[1]])
        for (const z of [0, extent[2]]) {
          if (!mesh.geometry && x > 28 && z > 16) continue;
          vertices.push([x, y, z]);
        }
    if (!mesh.geometry) {
      for (const y of [0, extent[1]]) vertices.push([28, y, 40], [80, y, 16]);
    }
    for (const p of vertices) {
      if (slice && axis === "Y") {
        const split = (extent[1] * position) / 100;
        p[1] = flip ? Math.min(p[1], split) : Math.max(p[1], split);
      }
      const u = dot(p, right),
        v = dot(p, up);
      bounds = [
        Math.min(u, bounds[0]),
        Math.min(v, bounds[1]),
        Math.max(u, bounds[2]),
        Math.max(v, bounds[3]),
      ];
    }
    const dimX = bounds[2] - bounds[0],
      dimY = bounds[3] - bounds[1];
    const scale =
      Math.min(
        (width - (width < 500 ? 78 : 100)) / Math.max(1, dimX),
        (height - (width < 500 ? 82 : 65)) / Math.max(1, dimY),
      ) * zoom;
    const midX = (bounds[0] + bounds[2]) / 2,
      midY = (bounds[1] + bounds[3]) / 2;
    const project = (p: Vec3): [number, number] => [
      width * 0.512 + (dot(p, right) - midX) * scale + panX,
      height * 0.524 - (dot(p, up) - midY) * scale + panY,
    ];
    const path = (poly: number[][]) => {
      ctx.beginPath();
      poly.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
      ctx.closePath();
    };
    if (grid) {
      ctx.lineWidth = 0.6;
      const floor = extent[1] + 2;
      const floorPoint = (x: number, z: number): Vec3 =>
        mesh.geometry ? [x, z, -1] : [x, floor, z];
      for (let i = -180; i <= 260; i += 5) {
        const big = i % 20 === 0;
        ctx.strokeStyle = big
          ? "rgba(105,142,179,.18)"
          : "rgba(100,140,180,.07)";
        for (const [a, b] of [
          [floorPoint(i, -180), floorPoint(i, 260)],
          [floorPoint(-180, i), floorPoint(260, i)],
        ] as [Vec3, Vec3][]) {
          ctx.beginPath();
          ctx.moveTo(...project(a));
          ctx.lineTo(...project(b));
          ctx.stroke();
        }
      }
    }
    if (faces.length) {
      const floor = extent[1] + 1;
      const shadow: Vec3[] = mesh.geometry
        ? [
            [0, 0, -0.5],
            [80, 0, -0.5],
            [80, 32, -0.5],
            [0, 32, -0.5],
          ]
        : [
            [0, floor, 0],
            [80, floor, 0],
            [80, floor, mesh.geometry ? 5 : 16],
            ...(mesh.geometry
              ? []
              : ([
                  [28, floor, 16],
                  [28, floor, 40],
                ] as Vec3[])),
            [0, floor, extent[2]],
          ];
      ctx.save();
      ctx.filter = "blur(12px)";
      ctx.fillStyle = "rgba(0,4,13,.55)";
      path(shadow.map(project));
      ctx.fill();
      ctx.restore();
    }
    const at = (extent["XYZ".indexOf(axis)] * position) / 100;
    if (slice && plane) {
      const axisI = "XYZ".indexOf(axis);
      const a = (axisI + 1) % 3,
        b = (axisI + 2) % 3;
      const quad: Vec3[] = [
        [0, 0, 0],
        [0, 0, 0],
        [0, 0, 0],
        [0, 0, 0],
      ];
      quad.forEach((q, i) => {
        q[axisI] = at;
        q[a] = i === 1 || i === 2 ? extent[a] + 4 : -4;
        q[b] = i >= 2 ? extent[b] + 4 : -4;
      });
      path(quad.map(project));
      ctx.fillStyle = "rgba(80,218,211,.055)";
      ctx.fill();
      ctx.strokeStyle = "rgba(96,209,203,.46)";
      ctx.lineWidth = 0.8;
      ctx.stroke();
    }
    const ordered = faces
      .map((f) => ({ f, depth: dot(f.center, view) }))
      .filter(
        ({ f }) =>
          mode === "Wireframe" ||
          wallPatch !== null ||
          dot(f.normal, view) > -0.00001,
      )
      .sort((a, b) => a.depth - b.depth);
    const polygons: Projected[] = [];
    const faceColor = (f: FaceData) => {
      const q = mesh.quality[f.owner],
        type = mesh.cells[f.owner].length;
      if (field === "distance")
        return distanceColor(distances[f.owner] / (mesh.geometry ? 2.5 : 12));
      if (field === "quality") return qColor(q);
      if (field === "layer")
        return mesh.layers[f.owner] > 0
          ? mix(
              [55, 133, 180],
              [248, 190, 95],
              mesh.layers[f.owner] / mesh.params.layers,
            )
          : [109, 144, 211];
      return palette[type];
    };
    ctx.lineJoin = "round";
    for (const { f } of ordered) {
      const poly = f.p.map(project);
      path(poly);
      const n = f.normal,
        lit = f.cut
          ? 1.02
          : 0.75 + 0.25 * Math.max(0, dot(n, [-0.35, -0.82, 0.45]));
      const tinted = faceColor(f),
        k = lit * (0.98 + (0.03 * f.center[0]) / 80);
      if (mode !== "Wireframe") {
        ctx.fillStyle = selected === f.owner ? "#b2fff0" : color(tinted, k);
        ctx.fill();
        if (!edges) {
          ctx.strokeStyle = ctx.fillStyle;
          ctx.lineWidth = 0.65;
          ctx.stroke();
        }
      }
      if (edges || mode === "Wireframe") {
        ctx.strokeStyle =
          selected === f.owner
            ? "#edfffc"
            : mode === "Wireframe"
              ? color(mix(tinted, [39, 60, 86], 0.42))
              : f.cut
                ? "rgba(22,40,56,.72)"
                : "rgba(17,33,55,.7)";
        ctx.lineWidth = selected === f.owner ? 1.75 : f.cut ? 0.62 : 0.56;
        ctx.stroke();
      }
      polygons.push({ poly, owner: f.owner });
    }
    projected.current = polygons;
    // A thin orientation triad stays legible independently of the scene zoom.
    const ox = 39,
      oy = height - 34;
    const axisColors = ["#ea8790", "#9ed398", "#88a8ed"];
    ctx.font = '9px "IBM Plex Mono", monospace';
    ctx.lineWidth = 1.45;
    (
      [
        [1, 0, 0],
        [0, 1, 0],
        [0, 0, 1],
      ] as Vec3[]
    ).forEach((q, i) => {
      const x = ox + dot(q, right) * 22,
        y = oy - dot(q, up) * 22;
      ctx.strokeStyle = axisColors[i];
      ctx.fillStyle = axisColors[i];
      ctx.beginPath();
      ctx.moveTo(ox, oy);
      ctx.lineTo(x, y);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(x, y, 1.7, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillText("XYZ"[i], x + (x > ox ? 3 : -8), y + (y > oy ? 7 : -3));
    });
    ctx.beginPath();
    ctx.arc(ox, oy, 2, 0, Math.PI * 2);
    ctx.fillStyle = "#aab9c4";
    ctx.fill();
  }, [
    faces,
    size,
    camera,
    mesh,
    axis,
    position,
    slice,
    flip,
    field,
    edges,
    grid,
    plane,
    selected,
    mode,
    wallPatch,
    distances,
  ]);
  const pick = (x: number, y: number) => {
    const list = projected.current;
    for (let i = list.length - 1; i >= 0; i--)
      if (isOver(list[i].poly, x, y)) return list[i].owner;
    return null;
  };
  const zoom = (k: number) =>
    setCamera((c) => ({ ...c, zoom: Math.max(0.4, Math.min(4, c.zoom * k)) }));
  const chooseView = () => {
    if (viewLabel === "Isometric") {
      setCamera({ ...HOME, yaw: -Math.PI / 2, pitch: 0.0001 });
      setViewLabel("Front");
    } else if (viewLabel === "Front") {
      setCamera({ ...HOME, yaw: -Math.PI / 2, pitch: Math.PI / 2 - 0.001 });
      setViewLabel("Top");
    } else reset();
  };
  const title = mesh.geometry ? "TWIN_PLATE_02" : "L_CHANNEL_01";
  return (
    <section
      ref={panel}
      className={`viewport panel ${expanded ? "viewport-expanded" : ""}`}
      aria-label="Interactive 3D mesh viewport"
    >
      <div className="panel-heading">
        <div className="panel-title">
          <Box size={16} />
          <h2>3D viewport</h2>
        </div>
        <div className="viewport-actions">
          <div className="relative">
            <button
              className="view-mode"
              onClick={(e) => {
                e.stopPropagation();
                setModeMenu(!modeMenu);
              }}
            >
              <Grid2X2 size={13} />
              {mode === "Wireframe" ? mode : edges ? "Solid + edges" : "Solid"}
              <ChevronDown size={12} />
            </button>
            {modeMenu && (
              <div className="menu mode-menu">
                {(["Solid + edges", "Solid", "Wireframe"] as ViewMode[]).map(
                  (m) => (
                    <button
                      key={m}
                      onClick={() => {
                        setMode(m);
                        setEdges(m !== "Solid");
                        setModeMenu(false);
                      }}
                    >
                      {m}
                      {((m === "Wireframe" && mode === m) ||
                        (mode !== "Wireframe" &&
                          m === (edges ? "Solid + edges" : "Solid"))) && (
                        <Check size={13} />
                      )}
                    </button>
                  ),
                )}
              </div>
            )}
          </div>
          <span className="vertical-rule" />
          <button
            className="icon-button"
            title={expanded ? "Exit fullscreen" : "Expand viewport"}
            aria-label={expanded ? "Exit fullscreen" : "Expand viewport"}
            onClick={() => setExpanded(!expanded)}
          >
            {expanded ? <X size={15} /> : <Expand size={15} />}
          </button>
        </div>
      </div>
      <div ref={stage} className={`mesh-stage tool-${tool}`}>
        <canvas
          ref={canvas}
          aria-label="Rotatable three-dimensional hybrid mesh. Drag to orbit, scroll to zoom, or select the pick tool to inspect a cell."
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === "Home") reset();
            if (e.key === "+" || e.key === "=") zoom(1.15);
            if (e.key === "-") zoom(1 / 1.15);
            if (e.key.startsWith("Arrow")) {
              e.preventDefault();
              setCamera((c) => ({
                ...c,
                yaw:
                  c.yaw +
                  (e.key === "ArrowLeft"
                    ? -0.12
                    : e.key === "ArrowRight"
                      ? 0.12
                      : 0),
                pitch: Math.max(
                  -1.4,
                  Math.min(
                    1.4,
                    c.pitch +
                      (e.key === "ArrowUp"
                        ? 0.1
                        : e.key === "ArrowDown"
                          ? -0.1
                          : 0),
                  ),
                ),
              }));
              setViewLabel("Custom");
            }
          }}
          onContextMenu={(e) => e.preventDefault()}
          onPointerDown={(e) => {
            e.currentTarget.setPointerCapture(e.pointerId);
            drag.current = {
              x: e.clientX,
              y: e.clientY,
              startX: e.clientX,
              startY: e.clientY,
              button: e.button,
            };
            setHover(null);
          }}
          onPointerMove={(e) => {
            const d = drag.current;
            if (d) {
              const dx = e.clientX - d.x,
                dy = e.clientY - d.y;
              d.x = e.clientX;
              d.y = e.clientY;
              if (tool === "pan" || e.shiftKey || d.button === 2)
                setCamera((c) => ({
                  ...c,
                  panX: c.panX + dx,
                  panY: c.panY + dy,
                }));
              else if (tool === "orbit") {
                setCamera((c) => ({
                  ...c,
                  yaw: c.yaw - dx * 0.007,
                  pitch: Math.max(-1.45, Math.min(1.45, c.pitch + dy * 0.006)),
                }));
                setViewLabel("Custom");
              }
            } else if (tool === "pick") {
              const r = e.currentTarget.getBoundingClientRect(),
                x = e.clientX - r.left,
                y = e.clientY - r.top,
                id = pick(x, y);
              setHover(id === null ? null : { id, x, y });
            }
          }}
          onPointerUp={(e) => {
            const d = drag.current;
            if (
              d &&
              Math.hypot(e.clientX - d.startX, e.clientY - d.startY) < 5 &&
              tool === "pick"
            ) {
              const r = e.currentTarget.getBoundingClientRect(),
                n = pick(e.clientX - r.left, e.clientY - r.top);
              if (n !== null) onPick(n);
            }
            drag.current = null;
          }}
          onPointerCancel={() => {
            drag.current = null;
          }}
          onPointerLeave={() => setHover(null)}
        />
        <div className="scene-label">
          <span className="scene-cross">+</span>
          <span>{title}</span>
          <span className="scene-label-line" />
          <span>{viewLabel}</span>
        </div>
        <button
          className="orientation-cube"
          onClick={chooseView}
          title="Cycle isometric, front, and top views"
          aria-label="Cycle camera orientation"
        >
          <svg viewBox="0 0 64 65" fill="none">
            <path d="M32 3 57 16 32 30 7 16Z" fill="#354452" stroke="#627585" />
            <path d="m7 16 25 14v28L7 43Z" fill="#25323e" stroke="#536675" />
            <path d="m32 30 25-14v27L32 58Z" fill="#1d2b37" stroke="#536675" />
            <g
              fill="#b7c6d0"
              fontSize="7"
              fontFamily="IBM Plex Mono"
              textAnchor="middle"
            >
              <text transform="matrix(.94 -.05 .3 .7 32 19)">TOP</text>
              <text transform="matrix(.78 .45 0 .94 19 37)">FRONT</text>
              <text transform="matrix(.78 -.45 0 .94 45 37)">RIGHT</text>
            </g>
          </svg>
        </button>
        <div className="tool-rail" aria-label="Viewport tools">
          {(
            [
              { name: "orbit", Icon: Orbit, label: "Orbit mesh" },
              { name: "pan", Icon: Hand, label: "Pan mesh" },
              { name: "pick", Icon: MousePointer2, label: "Pick cell" },
            ] as const
          ).map(({ name, Icon, label }) => (
            <button
              key={name}
              className={tool === name ? "active" : ""}
              onClick={() => {
                setTool(name);
                setHover(null);
              }}
              title={label}
              aria-label={label}
              aria-pressed={tool === name}
            >
              <Icon size={16} />
            </button>
          ))}
          <i />
          <button
            onClick={() => zoom(1.2)}
            title="Zoom in"
            aria-label="Zoom in"
          >
            <Plus size={16} />
          </button>
          <button
            onClick={() => zoom(1 / 1.2)}
            title="Zoom out"
            aria-label="Zoom out"
          >
            <Minus size={16} />
          </button>
          <button
            onClick={reset}
            title="Fit mesh to view"
            aria-label="Fit mesh to view"
          >
            <Focus size={16} />
          </button>
        </div>
        <div className="scene-bottom">
          <span className="live-dot" />
          {wallPatch !== null
            ? "Surface view"
            : slice
              ? "Section view"
              : "Volume view"}
          <span className="scene-bottom-divider">/</span>
          <span>
            {slice
              ? `${axis} = ${((mesh.extent["XYZ".indexOf(axis)] * position) / 100).toFixed(2)} mm`
              : "Unclipped"}
          </span>
        </div>
        {hover && (
          <div
            className="pick-hint"
            style={{
              left: Math.min(hover.x + 12, size[0] - 130),
              top: Math.max(24, hover.y - 34),
            }}
          >
            Cell {integer(hover.id)}
            <span>
              J<sub>ENS</sub> {mesh.quality[hover.id].toFixed(4)}
            </span>
          </div>
        )}
        {!faces.length && (
          <div className="empty-scene">
            <Crosshair size={22} />
            <span>No visible elements</span>
            <small>Enable an element type or move the slice plane.</small>
          </div>
        )}
        {busy && <div className="scan-line" />}
      </div>
      <div className="viewport-legend">
        {field === "type" ? (
          <>
            <span className="legend-title">ELEMENTS</span>
            {CELL_TYPES.map((c) => (
              <button
                key={c.n}
                className={!visible.includes(c.n) ? "muted" : ""}
                onClick={() => onVisibility(c.n)}
                title={`Toggle ${c.label.toLowerCase()}`}
                aria-pressed={visible.includes(c.n)}
              >
                <i style={{ background: c.color }} />
                {c.label}
              </button>
            ))}
          </>
        ) : (
          <>
            <span className="legend-title">
              {field === "quality"
                ? "JENS"
                : field === "distance"
                  ? "WALL DISTANCE"
                  : "LAYER"}
            </span>
            <span className="scale-number">
              {field === "quality"
                ? "0.00"
                : field === "distance"
                  ? "0.0"
                  : "Core"}
            </span>
            <span
              className={`color-ramp ${field === "layer" ? "layer-ramp" : field === "distance" ? "distance-ramp" : ""}`}
            />
            <span className="scale-number">
              {field === "quality"
                ? "1.00"
                : field === "distance"
                  ? mesh.geometry
                    ? "2.5 mm"
                    : "12.0 mm+"
                  : mesh.params.layers}
            </span>
          </>
        )}
        <span className="legend-unit">
          <Maximize2 size={10} /> mm
        </span>
      </div>
    </section>
  );
}
