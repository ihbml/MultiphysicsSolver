import { useEffect, useMemo, useRef, useState } from "react";
import type { CSSProperties, MouseEvent } from "react";
import { motion } from "framer-motion";
import {
  Activity,
  ArrowDownToLine,
  ArrowRight,
  Check,
  ChevronRight,
  Code2,
  Copy,
  Download,
  Expand,
  Grid2X2,
  Info,
  Layers3,
  Pause,
  Play,
  RotateCcw,
  Settings2,
  SkipForward,
  SlidersHorizontal,
  Thermometer,
  Waves,
} from "lucide-react";
import rawSimulation from "./data/cantilever.json";

type Field = "temperature" | "displacement";

type SimulationFrame = {
  time: number;
  temperature: number[];
  displacement: number[];
  thermalIterations: number;
  mechanicalIterations: number;
  thermalResidual: number;
  mechanicalResidual: number;
  peakTemperature: number;
  maxDisplacement: number;
};

type SimulationData = {
  meta: {
    name: string;
    width: number;
    height: number;
    referenceTemperature: number;
    baseHeatSource: number;
    timeStep: number;
    totalTime: number;
    material: string;
  };
  nodes: [number, number][];
  triangles: [number, number, number][];
  frames: SimulationFrame[];
};

const simulation = rawSimulation as unknown as SimulationData;
const lastFrame = simulation.frames.length - 1;
const referenceTemperature = simulation.meta.referenceTemperature;
const archiveUrl = "fieldline-cpp-source.zip";

const quickStart = `#include <fieldline/solver.hpp>

fieldline::ProblemView problem{
    {nodes, triangles}, materials, heat_sources, {},
    temperature_bcs, displacement_bcs, options
};

fieldline::ThermoelasticSolver solver(problem);
for (int step = 0; step < 32; ++step) {
    auto diagnostics = solver.step();
    auto temperature = solver.temperatures();
    auto displacement = solver.displacements();
}`;

const temperatureStops = [
  { at: 0, color: [26, 67, 82] },
  { at: 0.18, color: [42, 131, 147] },
  { at: 0.42, color: [124, 191, 165] },
  { at: 0.61, color: [236, 202, 123] },
  { at: 0.8, color: [247, 142, 83] },
  { at: 1, color: [236, 91, 69] },
];

const displacementStops = [
  { at: 0, color: [41, 54, 91] },
  { at: 0.25, color: [75, 103, 156] },
  { at: 0.55, color: [139, 150, 205] },
  { at: 0.8, color: [209, 164, 196] },
  { at: 1, color: [245, 197, 138] },
];

function colorAt(value: number, stops: typeof temperatureStops) {
  const amount = Math.max(0, Math.min(1, value));
  let upper = 1;
  while (upper < stops.length - 1 && amount > stops[upper].at) upper += 1;
  const start = stops[upper - 1];
  const end = stops[upper];
  const fraction = (amount - start.at) / (end.at - start.at);
  const channels = start.color.map((channel, index) =>
    Math.round(channel + (end.color[index] - channel) * fraction),
  );
  return `rgb(${channels.join(",")})`;
}

function LogoMark({ compact = false }: { compact?: boolean }) {
  return (
    <svg
      width={compact ? 30 : 34}
      height={compact ? 30 : 34}
      viewBox="0 0 34 34"
      fill="none"
      aria-hidden="true"
    >
      <path d="M5 27V7h23" stroke="#D8F1E9" strokeWidth="2.4" strokeLinecap="square" />
      <path d="M5 17h17" stroke="#D8F1E9" strokeWidth="2.4" strokeLinecap="square" />
      <path d="M18 27 29 16" stroke="#F1A073" strokeWidth="2.5" strokeLinecap="square" />
      <circle cx="28.5" cy="6.5" r="2.5" fill="#F1A073" />
    </svg>
  );
}

function Sidebar() {
  return (
    <aside className="side-rail" aria-label="Primary navigation">
      <a href="#workbench" className="rail-logo" aria-label="Fieldline home">
        <LogoMark compact />
      </a>
      <nav className="rail-navigation">
        <a href="#workbench" className="rail-link rail-link-active" aria-label="Workbench" title="Workbench">
          <Grid2X2 size={19} strokeWidth={1.8} />
        </a>
        <a href="#method" className="rail-link" aria-label="Numerical method" title="Numerical method">
          <Layers3 size={19} strokeWidth={1.8} />
        </a>
        <a href="#source" className="rail-link" aria-label="C++ source" title="C++ source">
          <Code2 size={19} strokeWidth={1.8} />
        </a>
      </nav>
      <div className="rail-bottom">
        <a href="#method" className="rail-link" aria-label="About this solver" title="About this solver">
          <Info size={18} strokeWidth={1.8} />
        </a>
        <span className="rail-version">v0.1</span>
      </div>
    </aside>
  );
}

function ResponseChart({
  field,
  heatScale,
  frameIndex,
  onSelect,
}: {
  field: Field;
  heatScale: number;
  frameIndex: number;
  onSelect: (index: number) => void;
}) {
  const values = simulation.frames.map((frame) =>
    field === "temperature"
      ? heatScale * (frame.peakTemperature - referenceTemperature)
      : heatScale * frame.maxDisplacement * 1e6,
  );
  const maximum = Math.max(...values, 1);
  const points = values.map((value, index) => ({
    x: 8 + (index / lastFrame) * 264,
    y: 83 - (value / maximum) * 65,
  }));
  const path = points.map((point, index) =>
    `${index === 0 ? "M" : "L"}${point.x.toFixed(2)} ${point.y.toFixed(2)}`,
  ).join(" ");
  const area = `${path} L272 84 L8 84 Z`;
  const active = points[frameIndex];

  function selectFromChart(event: MouseEvent<SVGSVGElement>) {
    const bounds = event.currentTarget.getBoundingClientRect();
    const relative = (event.clientX - bounds.left) / bounds.width;
    onSelect(Math.max(0, Math.min(lastFrame, Math.round(relative * lastFrame))));
  }

  return (
    <svg
      className="response-chart"
      viewBox="0 0 280 96"
      preserveAspectRatio="none"
      onClick={selectFromChart}
      role="img"
      aria-label={`${field === "temperature" ? "Peak temperature" : "Maximum displacement"} history; click to select a time`}
    >
      <line x1="8" y1="19" x2="272" y2="19" className="chart-gridline" />
      <line x1="8" y1="51" x2="272" y2="51" className="chart-gridline" />
      <line x1="8" y1="84" x2="272" y2="84" className="chart-gridline" />
      <motion.path
        key={`${field}-area`}
        d={area}
        className={field === "temperature" ? "chart-area chart-area-warm" : "chart-area chart-area-cool"}
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        transition={{ duration: 0.45 }}
      />
      <motion.path
        key={field}
        d={path}
        fill="none"
        stroke={field === "temperature" ? "#efa36f" : "#b4a9e4"}
        strokeWidth="2.2"
        strokeLinecap="round"
        strokeLinejoin="round"
        initial={{ pathLength: 0 }}
        animate={{ pathLength: 1 }}
        transition={{ duration: 0.85, ease: "easeOut" }}
      />
      <line x1={active.x} y1="16" x2={active.x} y2="84" className="chart-cursor" />
      <circle cx={active.x} cy={active.y} r="5" fill={field === "temperature" ? "#efa36f" : "#b4a9e4"} stroke="#17232b" strokeWidth="2" />
    </svg>
  );
}

function FieldViewer({
  field,
  setField,
  frame,
  heatScale,
  deformationScale,
  showMesh,
  setShowMesh,
  onExport,
}: {
  field: Field;
  setField: (field: Field) => void;
  frame: SimulationFrame;
  heatScale: number;
  deformationScale: number;
  showMesh: boolean;
  setShowMesh: (show: boolean) => void;
  onExport: () => void;
}) {
  const viewerRef = useRef<HTMLDivElement>(null);
  const [fullscreen, setFullscreen] = useState(false);

  useEffect(() => {
    const update = () => setFullscreen(document.fullscreenElement === viewerRef.current);
    document.addEventListener("fullscreenchange", update);
    return () => document.removeEventListener("fullscreenchange", update);
  }, []);

  async function toggleFullscreen() {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await viewerRef.current?.requestFullscreen();
    } catch {
      // Fullscreen may be unavailable inside an embedded preview.
    }
  }

  const coordinates = useMemo(() => simulation.nodes.map(([x, y], index) => {
    const ux = frame.displacement[2 * index] * heatScale * deformationScale;
    const uy = frame.displacement[2 * index + 1] * heatScale * deformationScale;
    return {
      x: 156 + ((x + ux) / simulation.meta.width) * 690,
      y: 390 - ((y + uy) / simulation.meta.height) * 259,
    };
  }), [frame, heatScale, deformationScale]);

  const polygons = useMemo(() => simulation.triangles.map(([a, b, c]) => {
    let amount: number;
    if (field === "temperature") {
      const mean = (frame.temperature[a] + frame.temperature[b] + frame.temperature[c]) / 3;
      amount = heatScale * (mean - referenceTemperature) / 157;
    } else {
      const magnitude = (index: number) => Math.hypot(
        frame.displacement[2 * index], frame.displacement[2 * index + 1],
      );
      amount = heatScale * (magnitude(a) + magnitude(b) + magnitude(c)) /
        (3 * 0.00025);
    }
    const vertices = [coordinates[a], coordinates[b], coordinates[c]];
    return {
      points: vertices.map(({ x, y }) => `${x.toFixed(2)},${y.toFixed(2)}`).join(" "),
      fill: colorAt(amount, field === "temperature" ? temperatureStops : displacementStops),
    };
  }), [coordinates, field, frame, heatScale]);

  const rowLength = simulation.nodes.findIndex(([, y]) => y > simulation.nodes[0][1]);
  const rows = simulation.nodes.length / rowLength;
  const outlineIndices = [
    ...Array.from({ length: rowLength }, (_, i) => i),
    ...Array.from({ length: rows - 1 }, (_, i) => (i + 1) * rowLength + rowLength - 1),
    ...Array.from({ length: rowLength - 1 }, (_, i) =>
      (rows - 1) * rowLength + rowLength - 2 - i),
    ...Array.from({ length: rows - 2 }, (_, i) => (rows - 2 - i) * rowLength),
  ];
  const outline = outlineIndices.map((index, i) => {
    const point = coordinates[index];
    return `${i === 0 ? "M" : "L"}${point.x.toFixed(2)} ${point.y.toFixed(2)}`;
  }).join(" ") + " Z";

  return (
    <motion.div
      className="viewer-shell"
      ref={viewerRef}
      initial={{ opacity: 0, y: 16 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.65, delay: 0.1, ease: [0.2, 0.7, 0.2, 1] }}
    >
      <div className="viewer-toolbar">
        <div className="viewer-tabs" role="tablist" aria-label="Field view">
          <span className="toolbar-caption">FIELD VIEW</span>
          <button
            type="button"
            className={`viewer-tab ${field === "temperature" ? "viewer-tab-active" : ""}`}
            role="tab"
            aria-selected={field === "temperature"}
            onClick={() => setField("temperature")}
          >
            <Thermometer size={15} /> Temperature
          </button>
          <button
            type="button"
            className={`viewer-tab ${field === "displacement" ? "viewer-tab-active" : ""}`}
            role="tab"
            aria-selected={field === "displacement"}
            onClick={() => setField("displacement")}
          >
            <Waves size={16} /> Displacement
          </button>
        </div>
        <div className="viewer-actions">
          <button
            type="button"
            className={`toolbar-button mesh-button ${showMesh ? "toolbar-button-selected" : ""}`}
            onClick={() => setShowMesh(!showMesh)}
            aria-pressed={showMesh}
            title="Toggle element edges"
          >
            <Grid2X2 size={15} /> <span>Mesh</span>
          </button>
          <span className="toolbar-separator" />
          <button type="button" className="icon-button" onClick={onExport} title="Export current frame as CSV" aria-label="Export current frame as CSV">
            <Download size={17} />
          </button>
          <button type="button" className="icon-button" onClick={toggleFullscreen} title={fullscreen ? "Exit fullscreen" : "Fullscreen viewer"} aria-label={fullscreen ? "Exit fullscreen" : "Fullscreen viewer"}>
            <Expand size={16} />
          </button>
        </div>
      </div>

      <div className="viewer-viewport">
        <svg
          className="field-svg"
          viewBox="0 0 1000 500"
          preserveAspectRatio="xMidYMid meet"
          role="img"
          aria-label={`${field === "temperature" ? "Temperature" : "Displacement"} finite element field at ${frame.time} seconds`}
        >
          <defs>
            <pattern id="stage-grid" width="41" height="41" patternUnits="userSpaceOnUse">
              <path d="M 41 0 L 0 0 0 41" fill="none" stroke="#9ab2b6" strokeWidth="0.65" opacity="0.11" />
            </pattern>
            <radialGradient id="heat-halo">
              <stop offset="0" stopColor="#d97951" stopOpacity="0.18" />
              <stop offset="1" stopColor="#d97951" stopOpacity="0" />
            </radialGradient>
            <linearGradient id="stage-shade" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" stopColor="#111f29" />
              <stop offset="1" stopColor="#0b1720" />
            </linearGradient>
          </defs>
          <rect width="1000" height="500" fill="url(#stage-shade)" />
          <rect width="1000" height="500" fill="url(#stage-grid)" />
          <ellipse cx="776" cy="256" rx="330" ry="225" fill="url(#heat-halo)" />

          <text x="52" y="58" className="stage-small-label">FIELDLINE / SOLVER VIEW</text>
          <text x="52" y="77" className="stage-faint-label">2D PLANE STRESS  /  P1 TRIANGLES</text>
          <text x="914" y="58" className="stage-small-label" textAnchor="end">T + {String(frame.time).padStart(2, "0")} S</text>

          <rect x="156" y="131" width="690" height="259" fill="none" stroke="#a6bdc1" strokeWidth="1" strokeDasharray="6 7" opacity="0.39" />

          <g className="stage-clamp">
            {Array.from({ length: 15 }, (_, index) => (
              <line key={index} x1="131" y1={139 + index * 17} x2="151" y2={127 + index * 17} />
            ))}
            <line x1="153" y1="125" x2="153" y2="396" className="clamp-edge" />
          </g>
          <text x="106" y="112" className="stage-annotation" textAnchor="middle">FIXED</text>

          <text x="713" y="96" className="stage-heat-label" textAnchor="middle">INTERNAL HEAT SOURCE</text>
          <g className="source-arrows">
            {[624, 680, 736, 792].map((x) => (
              <g key={x}>
                <line x1={x} y1="105" x2={x} y2="123" />
                <path d={`M${x - 4} 119 L${x} 125 L${x + 4} 119`} />
              </g>
            ))}
          </g>

          <g className="element-field">
            {polygons.map((polygon, index) => (
              <polygon
                key={index}
                points={polygon.points}
                fill={polygon.fill}
                stroke={showMesh ? "#102b32" : polygon.fill}
                strokeWidth={showMesh ? 0.62 : 0.85}
                strokeOpacity={showMesh ? 0.38 : 1}
                strokeLinejoin="round"
              />
            ))}
          </g>
          <path d={outline} fill="none" stroke="#d1e7df" strokeWidth="1.25" opacity="0.72" />

          <g className="stage-dimension">
            <line x1="156" y1="446" x2="846" y2="446" />
            <line x1="156" y1="438" x2="156" y2="453" />
            <line x1="846" y1="438" x2="846" y2="453" />
            <rect x="471" y="435" width="60" height="22" fill="#0d1922" />
            <text x="501" y="450" textAnchor="middle">120 mm</text>
          </g>
          <text x="52" y="479" className="stage-faint-label">{simulation.nodes.length.toLocaleString("en-US")} NODES  /  {simulation.triangles.length.toLocaleString("en-US")} ELEMENTS</text>
          <text x="948" y="479" className="stage-faint-label" textAnchor="end">DEFORMATION x{deformationScale}</text>
        </svg>
      </div>

      <div className="viewer-legend">
        <div className="legend-heading">
          <span className="legend-square" />
          <span>{field === "temperature" ? "TEMPERATURE FIELD" : "DISPLACEMENT MAGNITUDE"}</span>
          <span className="legend-unit">{field === "temperature" ? "KELVIN" : "MICROMETERS"}</span>
        </div>
        <div className="legend-scale">
          <span>{field === "temperature" ? "293 K" : "0 um"}</span>
          <div className={`legend-gradient ${field === "displacement" ? "legend-gradient-displacement" : ""}`} />
          <span>{field === "temperature" ? "450+ K" : "250+ um"}</span>
        </div>
      </div>
    </motion.div>
  );
}

function App() {
  const [frameIndex, setFrameIndex] = useState(14);
  const [playing, setPlaying] = useState(false);
  const [field, setField] = useState<Field>("temperature");
  const [heatScale, setHeatScale] = useState(1);
  const [deformationScale, setDeformationScale] = useState(38);
  const [showMesh, setShowMesh] = useState(true);
  const [copied, setCopied] = useState(false);
  const frame = simulation.frames[frameIndex];

  useEffect(() => {
    if (!playing) return;
    const timer = window.setInterval(() => {
      setFrameIndex((current) => (current + 1) % simulation.frames.length);
    }, 480);
    return () => window.clearInterval(timer);
  }, [playing]);

  function togglePlayback() {
    if (!playing && frameIndex === lastFrame) setFrameIndex(0);
    setPlaying(!playing);
  }

  function selectFrame(index: number) {
    setPlaying(false);
    setFrameIndex(index);
  }

  function exportCsv() {
    const rows = ["node,x_m,y_m,temperature_K,ux_m,uy_m"];
    simulation.nodes.forEach(([x, y], index) => {
      const temperature = referenceTemperature +
        heatScale * (frame.temperature[index] - referenceTemperature);
      rows.push([
        index, x, y, temperature,
        heatScale * frame.displacement[2 * index],
        heatScale * frame.displacement[2 * index + 1],
      ].join(","));
    });
    const url = URL.createObjectURL(new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" }));
    const link = document.createElement("a");
    link.href = url;
    link.download = `fieldline-frame-${String(frame.time).padStart(2, "0")}s.csv`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  async function copyCode() {
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(quickStart);
      } else {
        const input = document.createElement("textarea");
        input.value = quickStart;
        input.style.position = "fixed";
        input.style.opacity = "0";
        document.body.appendChild(input);
        input.select();
        const success = document.execCommand("copy");
        input.remove();
        if (!success) throw new Error("Copy unavailable");
      }
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2200);
    } catch {
      setCopied(false);
    }
  }

  const peakTemperature = referenceTemperature +
    heatScale * (frame.peakTemperature - referenceTemperature);
  const peakDisplacement = heatScale * frame.maxDisplacement * 1e6;
  const timelineStyle = { "--fill": `${(frameIndex / lastFrame) * 100}%` } as CSSProperties;
  const heatStyle = { "--fill": `${((heatScale - 0.25) / 1.5) * 100}%` } as CSSProperties;
  const deformationStyle = { "--fill": `${((deformationScale - 1) / 59) * 100}%` } as CSSProperties;

  return (
    <div className="app-shell" id="workbench">
      <Sidebar />
      <main className="workspace">
        <header className="topbar">
          <a className="wordmark" href="#workbench" aria-label="Fieldline workbench home">
            <span className="wordmark-logo"><LogoMark /></span>
            <span>FIELDLINE<span className="wordmark-period">.</span></span>
          </a>
          <span className="topbar-divider" />
          <span className="topbar-product">MULTIPHYSICS WORKBENCH</span>
          <div className="topbar-spacer" />
          <span className="engine-status"><span /> C++20 ENGINE</span>
          <a className="topbar-download" href={archiveUrl} download>
            Get source <ArrowDownToLine size={15} />
          </a>
        </header>

        <div className="workspace-content">
          <motion.div
            className="study-header"
            initial={{ opacity: 0, y: 12 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, ease: "easeOut" }}
          >
            <div className="study-copy">
              <div className="eyebrow"><span className="eyebrow-line" /> STUDY 01 <span className="eyebrow-slash">/</span> COUPLED PHYSICS</div>
              <h1>Thermoelastic cantilever<span className="heading-dot">.</span></h1>
              <p>Watch heat move through a structure, then see the structure respond.</p>
            </div>
            <button type="button" className="primary-button" onClick={togglePlayback}>
              {playing ? <Pause size={17} fill="currentColor" /> : <Play size={17} fill="currentColor" />}
              {playing ? "Pause simulation" : "Play simulation"}
              <span className="primary-button-arrow"><ArrowRight size={17} /></span>
            </button>
          </motion.div>

          <div className="simulation-layout">
            <div className="simulation-main">
              <FieldViewer
                field={field}
                setField={setField}
                frame={frame}
                heatScale={heatScale}
                deformationScale={deformationScale}
                showMesh={showMesh}
                setShowMesh={setShowMesh}
                onExport={exportCsv}
              />

              <div className="timeline">
                <button type="button" className="transport-play" onClick={togglePlayback} aria-label={playing ? "Pause playback" : "Play simulation"}>
                  {playing ? <Pause size={16} fill="currentColor" /> : <Play size={16} fill="currentColor" />}
                </button>
                <div className="timeline-content">
                  <div className="timeline-labels">
                    <span>SIMULATION TIME</span>
                    <span><strong>{frame.time.toFixed(1)} s</strong> <span className="timeline-total">/ {simulation.meta.totalTime.toFixed(1)} s</span></span>
                  </div>
                  <input
                    className="range-input timeline-range"
                    type="range"
                    min="0"
                    max={lastFrame}
                    step="1"
                    value={frameIndex}
                    style={timelineStyle}
                    onChange={(event) => selectFrame(Number(event.target.value))}
                    aria-label="Simulation time"
                  />
                  <div className="timeline-ticks"><span>0 s</span><span>8 s</span><span>16 s</span><span>24 s</span><span>32 s</span></div>
                </div>
                <button type="button" className="transport-step" onClick={() => selectFrame(Math.min(lastFrame, frameIndex + 1))} aria-label="Next time step" title="Next time step">
                  <SkipForward size={18} />
                </button>
              </div>

              <div className="solver-caption">
                <span><span className="caption-indicator" /> SOLUTION CONVERGED</span>
                <span>IMPLICIT FEM <span className="caption-divider">/</span> {simulation.meta.material.toUpperCase()} <span className="caption-divider">/</span> 2D PLANE STRESS</span>
              </div>
            </div>

            <motion.aside
              className="inspector"
              initial={{ opacity: 0, x: 14 }}
              animate={{ opacity: 1, x: 0 }}
              transition={{ duration: 0.65, delay: 0.18, ease: "easeOut" }}
            >
              <div className="inspector-topline"><SlidersHorizontal size={15} /> PARAMETERS <span>01 / 02</span></div>
              <h2>Control the conditions<span>.</span></h2>
              <p className="inspector-description">Change the applied heat and inspect the resulting response.</p>

              <div className="control-group">
                <div className="control-heading">
                  <label htmlFor="heat-input">Heat generation</label>
                  <strong>{(simulation.meta.baseHeatSource * heatScale / 1e6).toFixed(1)} <small>MW/m3</small></strong>
                </div>
                <input
                  id="heat-input"
                  className="range-input heat-range"
                  type="range"
                  min="0.25"
                  max="1.75"
                  step="0.05"
                  value={heatScale}
                  style={heatStyle}
                  onChange={(event) => setHeatScale(Number(event.target.value))}
                />
                <div className="range-endpoints"><span>4.5 MW/m3</span><span>31.5 MW/m3</span></div>
              </div>

              <div className="control-group control-group-second">
                <div className="control-heading">
                  <label htmlFor="deformation-input">Deformation scale</label>
                  <strong>{deformationScale}<small>x visual</small></strong>
                </div>
                <input
                  id="deformation-input"
                  className="range-input deformation-range"
                  type="range"
                  min="1"
                  max="60"
                  step="1"
                  value={deformationScale}
                  style={deformationStyle}
                  onChange={(event) => setDeformationScale(Number(event.target.value))}
                />
                <div className="range-endpoints"><span>1x actual</span><span>60x visual</span></div>
              </div>

              <div className="inspector-rule" />
              <div className="section-mini-heading"><span>AT THIS TIME STEP</span><span>T + {String(frame.time).padStart(2, "0")} S</span></div>
              <div className="reading-row">
                <div className="reading-name"><Thermometer size={16} /> Peak temperature</div>
                <div className="reading-value">{peakTemperature.toFixed(1)} <small>K</small></div>
              </div>
              <div className="reading-row">
                <div className="reading-name"><Activity size={16} /> Max displacement</div>
                <div className="reading-value">{peakDisplacement.toFixed(1)} <small>um</small></div>
              </div>
              <div className="reading-row">
                <div className="reading-name"><Settings2 size={16} /> Reference solve</div>
                <div className="reading-value reading-value-small">{frame.thermalIterations} <small>+ {frame.mechanicalIterations} iter.</small></div>
              </div>

              <div className="inspector-rule history-rule" />
              <div className="section-mini-heading"><span>RESPONSE HISTORY</span><span>{field === "temperature" ? "TEMP / TIME" : "DISP / TIME"}</span></div>
              <ResponseChart field={field} heatScale={heatScale} frameIndex={frameIndex} onSelect={selectFrame} />
              <div className="chart-axis"><span>0 SEC</span><span>32 SEC</span></div>

              <div className="inspector-note"><Info size={14} /><span>Load scaling is exact for this linear model. Snapshots come from the included C++ solver.</span></div>
            </motion.aside>
          </div>

          <section className="method-section" id="method">
            <div className="section-intro">
              <div className="eyebrow"><span className="eyebrow-line" /> 02 <span className="eyebrow-slash">/</span> THE METHOD</div>
              <h2>Built for the equations.</h2>
              <p>Real finite elements under the interface. A compact, reusable foundation for coupled simulations.</p>
            </div>
            <div className="method-steps">
              <div className="method-step">
                <span className="method-number">01 / PRECOMPUTE</span>
                <h3>Assemble once.</h3>
                <p>CSR sparsity, consistent thermal mass, elastic stiffness, and boundary elimination are prepared from your in-memory mesh.</p>
              </div>
              <div className="method-step">
                <span className="method-number">02 / ADVANCE</span>
                <h3>Solve heat implicitly.</h3>
                <p>An A-stable theta method advances temperature. Warm-started PCG with IC(0) tracks a verified residual.</p>
              </div>
              <div className="method-step">
                <span className="method-number">03 / COUPLE</span>
                <h3>Resolve the response.</h3>
                <p>Element thermal strain drives a plane-stress solve, returning nodal displacement alongside the temperature field.</p>
              </div>
            </div>
          </section>

          <section className="source-section" id="source">
            <div className="source-copy">
              <div className="eyebrow"><span className="eyebrow-line" /> 03 <span className="eyebrow-slash">/</span> THE SOURCE</div>
              <h2>Your mesh.<br />Your solver.</h2>
              <p>The C++20 core is self-contained and ready to extend. Includes the native example, numerical tests, and CMake setup.</p>
              <a href={archiveUrl} className="source-download" download>Download C++ source <ArrowDownToLine size={17} /></a>
              <span className="source-footnote">No runtime dependencies. Optional OpenMP.</span>
            </div>
            <div className="code-window">
              <div className="code-topbar"><div><span className="code-dots"><i /><i /><i /></span><span>quickstart.cpp</span></div><button type="button" onClick={copyCode} aria-label="Copy quick start code">{copied ? <Check size={15} /> : <Copy size={15} />}{copied ? "Copied" : "Copy"}</button></div>
              <pre><code>{quickStart}</code></pre>
              <div className="code-bottom"><span><span className="code-ready-dot" /> C++20</span><span>fieldline/solver.hpp <ChevronRight size={14} /></span></div>
            </div>
          </section>

          <footer className="page-footer">
            <span><LogoMark compact /> FIELDLINE<span className="wordmark-period">.</span></span>
            <span>AN OPEN BASE FOR COUPLED PHYSICS</span>
            <a href="#workbench" onClick={() => setPlaying(false)}>BACK TO TOP <RotateCcw size={14} /></a>
          </footer>
        </div>
      </main>
    </div>
  );
}

export default App;
