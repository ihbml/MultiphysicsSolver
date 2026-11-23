import { useEffect, useMemo, useRef, useState, type ChangeEvent, type CSSProperties, type ReactNode } from "react";
import ViewportCanvas, { type ViewportCanvasHandle } from "@/components/ViewportCanvas";
import { formatSI, parseUnitExpression, type ParsedUnit, type UnitDimension } from "@/lib/units";
import { DemoRpcClient, rpcEndpoints, type RpcLogEntry } from "@/lib/rpc";
import { evaluateVisibleIf, generateBlockStack, referenceUiSchema, type ModuleUiSchema, type StackBlock } from "@/lib/schema";
import type { FieldName, PaletteName, QualityMetric, ViewportMode, ViewportOptions } from "@/lib/viewportRenderer";

type IconName = "mark" | "cube" | "mesh" | "sliders" | "activity" | "compare" | "plus" | "chevron" | "more" | "search" | "settings" | "cloud" | "play" | "pause" | "stop" | "close" | "cursor" | "orbit" | "move" | "fit" | "eye" | "code" | "download" | "upload" | "check" | "alert" | "link" | "file" | "terminal" | "layers" | "refresh" | "clock" | "filter" | "maximize" | "panel" | "arrow" | "lock" | "radio";

const icons: Record<IconName, ReactNode> = {
  mark: <><path d="M4 4h9l7 7-9 9-7-7V4Z" /><path d="M4 12h7l9-9" /><path d="M9 8h.01" /></>,
  cube: <><path d="m12 3 8 4.5v9L12 21l-8-4.5v-9L12 3Z" /><path d="m4.4 7.6 7.6 4.3 7.6-4.3M12 12v9" /></>,
  mesh: <><path d="m12 3 8 4.5v9L12 21l-8-4.5v-9L12 3Z" /><path d="m4 7.5 8 5 8-5M8 5.2v9.6m8-9.6v9.6M4 12l8 5 8-5" /></>,
  sliders: <><path d="M4 6h8m4 0h4M4 12h3m4 0h9M4 18h8m4 0h4" /><circle cx="14" cy="6" r="2" /><circle cx="9" cy="12" r="2" /><circle cx="14" cy="18" r="2" /></>,
  activity: <><path d="M3 12h4l2.3-7 4.4 14 2.2-7H21" /></>,
  compare: <><path d="M8 4H4v16h4M16 4h4v16h-4M12 7v10M9.5 9.5 12 7l2.5 2.5M9.5 14.5 12 17l2.5-2.5" /></>,
  plus: <><path d="M12 5v14M5 12h14" /></>,
  chevron: <><path d="m8 10 4 4 4-4" /></>,
  more: <><circle cx="5" cy="12" r="1" /><circle cx="12" cy="12" r="1" /><circle cx="19" cy="12" r="1" /></>,
  search: <><circle cx="10.8" cy="10.8" r="6.6" /><path d="m16 16 4.4 4.4" /></>,
  settings: <><circle cx="12" cy="12" r="3" /><path d="m19.4 15 .1.1a1.7 1.7 0 1 1-2.4 2.4l-.1-.1a1.7 1.7 0 0 0-2.9 1.2v.2a1.7 1.7 0 1 1-3.4 0v-.2A1.7 1.7 0 0 0 8 17.4l-.1.1a1.7 1.7 0 1 1-2.4-2.4l.1-.1a1.7 1.7 0 0 0-1.2-2.9h-.2a1.7 1.7 0 1 1 0-3.4h.2A1.7 1.7 0 0 0 5.6 6l-.1-.1a1.7 1.7 0 1 1 2.4-2.4l.1.1a1.7 1.7 0 0 0 2.9-1.2v-.2a1.7 1.7 0 1 1 3.4 0v.2A1.7 1.7 0 0 0 17.2 4l.1-.1a1.7 1.7 0 1 1 2.4 2.4l-.1.1a1.7 1.7 0 0 0 1.2 2.9h.2a1.7 1.7 0 1 1 0 3.4h-.2a1.7 1.7 0 0 0-1.5 2.3Z" /></>,
  cloud: <><path d="M6.5 18h11a4 4 0 0 0 .3-8 6 6 0 0 0-11.4 1.3A3.4 3.4 0 0 0 6.5 18Z" /><path d="m9 13 3 3 3-3" /></>,
  play: <><path d="m8 5 11 7-11 7V5Z" /></>,
  pause: <><path d="M8 5v14M16 5v14" /></>,
  stop: <><rect x="5" y="5" width="14" height="14" rx="1" /></>,
  close: <><path d="m6 6 12 12M18 6 6 18" /></>,
  cursor: <><path d="m5 3 14 11-7 1-3 6L5 3Z" /></>,
  orbit: <><circle cx="12" cy="12" r="7" /><path d="M4 5v4h4M20 19v-4h-4M4.8 9A8 8 0 0 1 18 5m1.2 10A8 8 0 0 1 6 19" /></>,
  move: <><path d="M12 3v18M3 12h18M12 3l-3 3m3-3 3 3M21 12l-3-3m3 3-3 3M12 21l-3-3m3 3 3-3M3 12l3-3m-3 3 3 3" /></>,
  fit: <><path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5" /><path d="m8 8 8 8m0-8-8 8" /></>,
  eye: <><path d="M2.5 12s3.4-6 9.5-6 9.5 6 9.5 6-3.4 6-9.5 6-9.5-6-9.5-6Z" /><circle cx="12" cy="12" r="2.5" /></>,
  code: <><path d="m8 7-5 5 5 5m8-10 5 5-5 5m-2-13-4 16" /></>,
  download: <><path d="M12 3v12m-5-5 5 5 5-5M4 19v2h16v-2" /></>,
  upload: <><path d="M12 16V4m-5 5 5-5 5 5M4 19v2h16v-2" /></>,
  check: <><path d="m5 12 4 4L19 6" /></>,
  alert: <><path d="M12 3 2.7 19h18.6L12 3Z" /><path d="M12 9v4m0 3h.01" /></>,
  link: <><path d="M10 13a5 5 0 0 0 7.1 0l2-2A5 5 0 0 0 12 3.9L10.8 5M14 11a5 5 0 0 0-7.1 0l-2 2A5 5 0 0 0 12 20.1l1.2-1.1" /></>,
  file: <><path d="M6 3h8l4 4v14H6V3Z" /><path d="M14 3v5h5M9 13h6m-6 4h6" /></>,
  terminal: <><path d="m4 6 5 5-5 5m8 0h8" /></>,
  layers: <><path d="m12 3 9 5-9 5-9-5 9-5Z" /><path d="m3 12 9 5 9-5M3 16l9 5 9-5" /></>,
  refresh: <><path d="M20 7v5h-5M4 17v-5h5" /><path d="M5.6 9A7 7 0 0 1 18 6l2 6M4 12l2 6a7 7 0 0 0 12.4-3" /></>,
  clock: <><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3.5 2" /></>,
  filter: <><path d="M3 5h18l-7 8v6l-4 2v-8L3 5Z" /></>,
  maximize: <><path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5" /></>,
  panel: <><rect x="3" y="4" width="18" height="16" rx="1" /><path d="M8 4v16" /></>,
  arrow: <><path d="M4 12h15m-6-6 6 6-6 6" /></>,
  lock: <><rect x="5" y="10" width="14" height="11" rx="1" /><path d="M8 10V7a4 4 0 0 1 8 0v3" /></>,
  radio: <><circle cx="12" cy="12" r="2" /><path d="M7 7a7 7 0 0 0 0 10m10 0a7 7 0 0 0 0-10M4 4a11 11 0 0 0 0 16m16 0a11 11 0 0 0 0-16" /></>,
};

function Icon({ name, size = 16 }: { name: IconName; size?: number }) {
  return <svg aria-hidden="true" width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round">{icons[name]}</svg>;
}

const workspaces: { id: number; name: string; short: string; icon: IconName }[] = [
  { id: 0, name: "CAD & Domain", short: "CAD", icon: "cube" },
  { id: 1, name: "Meshing & Quality", short: "MESH", icon: "mesh" },
  { id: 2, name: "Physics & BC Stack", short: "PHYSICS", icon: "sliders" },
  { id: 3, name: "Solve & Live Monitors", short: "SOLVE", icon: "activity" },
  { id: 4, name: "Post-Processing & Compare", short: "POST", icon: "compare" },
];

type Block = StackBlock;

type FieldProps = {
  label: string;
  value: string;
  dimension: UnitDimension;
  helper?: string;
  error?: string;
  onChange: (value: string) => void;
  onCommit?: (parsed: ParsedUnit) => void;
};

function UnitField({ label, value, dimension, helper, error, onChange, onCommit }: FieldProps) {
  const [touched, setTouched] = useState(false);
  const parsed = useMemo(() => parseUnitExpression(value, dimension), [value, dimension]);
  const localError = touched && !parsed ? `Enter a valid ${dimension} expression` : error;
  return (
    <label className={`unit-field ${localError ? "has-error" : ""}`}>
      <span className="field-label">{label}</span>
      <input
        value={value}
        spellCheck={false}
        onChange={(event) => { onChange(event.target.value); if (touched) setTouched(false); }}
        onBlur={() => {
          setTouched(true);
          if (parsed && !error) onCommit?.(parsed);
        }}
        aria-invalid={Boolean(localError)}
      />
      <span className="field-conversion">{parsed ? <><span>SI</span> {formatSI(parsed.valueSI, parsed.unitSI)}</> : <span className="field-placeholder">{localError || helper || "unit-aware · strict SI"}</span>}</span>
    </label>
  );
}

function SelectField({ label, value, options, onChange }: { label: string; value: string; options: string[]; onChange: (value: string) => void }) {
  return (
    <label className="select-field">
      <span className="field-label">{label}</span>
      <span className="select-control"><select value={value} onChange={(event) => onChange(event.target.value)}>{options.map((option) => <option key={option}>{option}</option>)}</select><Icon name="chevron" size={13} /></span>
    </label>
  );
}

type EditorProps = {
  id: string;
  values: Record<string, string>;
  profile: string;
  resolution: "dy1" | "yplus";
  onValue: (key: string, value: string) => void;
  onCommit: (key: string, parsed: ParsedUnit) => void;
  onProfile: (value: string) => void;
  onResolution: (value: "dy1" | "yplus") => void;
  onImport: () => void;
};

function BlockEditor({ id, values, profile, resolution, onValue, onCommit, onProfile, onResolution, onImport }: EditorProps) {
  if (id === "geometry") return (
    <div className="block-editor">
      <div className="file-source"><span className="file-chip"><Icon name="file" size={13} /> STEP</span><span className="file-name">{values.geometryFile}</span><button className="tiny-button" onClick={onImport}>Replace</button></div>
      <UnitField label="Model scale" value={values.scale} dimension="dimensionless" onChange={(value) => onValue("scale", value)} onCommit={(parsed) => onCommit("scale", parsed)} />
      <div className="schema-hint"><span>schema.geometry.import</span><span className="good-dot" /> f64 vertices</div>
    </div>
  );
  if (id === "domain") return (
    <div className="block-editor">
      <UnitField label="Domain length" value={values.domainLength} dimension="length" onChange={(value) => onValue("domainLength", value)} onCommit={(parsed) => onCommit("domainLength", parsed)} />
      <div className="two-fields"><UnitField label="Height" value={values.domainHeight} dimension="length" onChange={(value) => onValue("domainHeight", value)} onCommit={(parsed) => onCommit("domainHeight", parsed)} /><UnitField label="Span" value={values.domainSpan} dimension="length" onChange={(value) => onValue("domainSpan", value)} onCommit={(parsed) => onCommit("domainSpan", parsed)} /></div>
      <div className="schema-hint"><span>visible_if: geometry.status == ready</span><span>3D</span></div>
    </div>
  );
  if (id === "mesh") return (
    <div className="block-editor">
      <UnitField label="Base cell size" value={values.baseCell} dimension="length" onChange={(value) => onValue("baseCell", value)} onCommit={(parsed) => onCommit("baseCell", parsed)} />
      <div className="param-row"><div className="param-copy"><span>Refinement</span><small>surface · 3 levels</small></div><button className="select-inline" onClick={() => onValue("refinement", values.refinement === "3 levels" ? "4 levels" : "3 levels")}>{values.refinement}<Icon name="chevron" size={12} /></button></div>
      <div className="schema-hint"><span>CFDMSH02 · 2.84 M cells</span><span>5.1 GB</span></div>
    </div>
  );
  if (id === "fluid") return (
    <div className="block-editor">
      <SelectField label="Material" value={values.material} options={["Air · incompressible", "Water · liquid", "Custom fluid"]} onChange={(value) => onValue("material", value)} />
      <UnitField label="Density" value={values.density} dimension="density" onChange={(value) => onValue("density", value)} onCommit={(parsed) => onCommit("density", parsed)} />
      <UnitField label="Kinematic viscosity" value={values.viscosity} dimension="kinematicViscosity" onChange={(value) => onValue("viscosity", value)} onCommit={(parsed) => onCommit("viscosity", parsed)} />
    </div>
  );
  if (id === "inlet") {
    const turbulenceVisible = evaluateVisibleIf({ parameter: "profile", equals: "Fully developed" }, { profile });
    return (
    <div className="block-editor">
      <SelectField label="Velocity profile" value={profile} options={["Uniform", "Fully developed", "Mapped field"]} onChange={onProfile} />
      {turbulenceVisible && <div className="conditional-tag">visible_if: profile == fully_developed</div>}
      <UnitField label="Magnitude" value={values.inletSpeed} dimension="velocity" onChange={(value) => onValue("inletSpeed", value)} onCommit={(parsed) => onCommit("inletSpeed", parsed)} />
      <UnitField label="Temperature" value={values.inletTemperature} dimension="temperature" onChange={(value) => onValue("inletTemperature", value)} onCommit={(parsed) => onCommit("inletTemperature", parsed)} />
      {turbulenceVisible && <UnitField label="Turbulence intensity" value={values.turbulence} dimension="dimensionless" onChange={(value) => onValue("turbulence", value)} onCommit={(parsed) => onCommit("turbulence", parsed)} />}
      <div className="schema-hint"><span>velocity · m·s⁻¹</span><span>face-group 03</span></div>
    </div>
    );
  }
  if (id === "wall") return (
    <div className="block-editor">
      <div className="resolution-switch" aria-label="Wall resolution control">
        <button className={resolution === "dy1" ? "selected" : ""} onClick={() => onResolution("dy1")}>Δy₁</button>
        <button className={resolution === "yplus" ? "selected" : ""} onClick={() => onResolution("yplus")}>Target y⁺</button>
      </div>
      {resolution === "dy1" ? <UnitField label="First layer height · Δy₁" value={values.firstLayer} dimension="length" helper="absolute prism thickness" onChange={(value) => onValue("firstLayer", value)} onCommit={(parsed) => onCommit("firstLayer", parsed)} /> : <UnitField label="Target y⁺" value={values.targetYplus} dimension="dimensionless" helper="dimensionless wall coordinate" onChange={(value) => onValue("targetYplus", value)} onCommit={(parsed) => onCommit("targetYplus", parsed)} />}
      <UnitField label="Surface roughness" value={values.roughness} dimension="length" onChange={(value) => onValue("roughness", value)} onCommit={(parsed) => onCommit("roughness", parsed)} />
      <div className="distinction-note"><span className="note-mark">!</span><span>Δy₁ is a length. Estimated y⁺ ≈ {resolution === "dy1" ? "32.4" : "30.0"} is derived, not interchangeable.</span></div>
    </div>
  );
  if (id === "solver") return (
    <div className="block-editor">
      <SelectField label="Turbulence model" value={values.turbulenceModel} options={["k-ω SST", "Spalart–Allmaras", "k-ε realizable"]} onChange={(value) => onValue("turbulenceModel", value)} />
      <SelectField label="Pressure–velocity" value={values.coupling} options={["SIMPLE", "SIMPLEC", "PIMPLE"]} onChange={(value) => onValue("coupling", value)} />
      <div className="schema-hint"><span>steady · incompressible</span><span>7 equations</span></div>
    </div>
  );
  if (id === "rotor") return (
    <div className="block-editor">
      <UnitField label="Angular velocity" value={values.rotationalSpeed} dimension="angularVelocity" onChange={(value) => onValue("rotationalSpeed", value)} onCommit={(parsed) => onCommit("rotationalSpeed", parsed)} />
      <div className="schema-hint"><span>ω · rotating reference frame</span><span>rad/s</span></div>
    </div>
  );
  if (id === "surface-lic") return (
    <div className="block-editor">
      <SelectField label="Input field" value="wallShearStress" options={["wallShearStress", "velocity"]} onChange={() => undefined} />
      <div className="schema-hint"><span>2D LIC kernel · Surface-LIC</span><span>GPU</span></div>
    </div>
  );
  return (
    <div className="block-editor">
      <SelectField label="Thermal model" value={values.thermalModel} options={["Conjugate heat transfer", "Isothermal wall"]} onChange={(value) => onValue("thermalModel", value)} />
      <UnitField label="Wall temperature" value={values.wallTemperature} dimension="temperature" onChange={(value) => onValue("wallTemperature", value)} onCommit={(parsed) => onCommit("wallTemperature", parsed)} />
      <div className="schema-hint"><span>visible_if: energy == enabled</span><span>thermal</span></div>
    </div>
  );
}

function MiniHeatmap({ metric, selected, onSelect }: { metric: QualityMetric; selected: boolean; onSelect: () => void }) {
  const meta: Record<QualityMetric, { title: string; value: string; short: string }> = {
    skewness: { title: "Skewness", value: "0.81", short: "MAX" },
    nonOrthogonality: { title: "Non-orthog.", value: "68.4°", short: "MAX" },
    aspectRatio: { title: "Aspect ratio", value: "42.7", short: "MAX" },
    cellVolume: { title: "Cell volume", value: "2.1e-5", short: "MIN" },
  };
  return (
    <button className={`heatmap-tile ${selected ? "selected" : ""}`} onClick={onSelect}>
      <span className="heatmap-head"><span>{meta[metric].title}</span><span>{meta[metric].short}</span></span>
      <span className="heatmap-grid">{Array.from({ length: 64 }, (_, index) => {
        const intensity = Math.abs(Math.sin((index + 1) * (metric.length * 0.31) + index * 0.77));
        const color = intensity > 0.78 ? "#ef3245" : intensity > 0.55 ? "#f2a42a" : intensity > 0.3 ? "#829731" : "#334966";
        return <i key={index} style={{ backgroundColor: color, opacity: 0.5 + intensity * 0.5 }} />;
      })}</span>
      <span className="heatmap-value">{meta[metric].value}<span> flat · cell</span></span>
    </button>
  );
}

function QualityPanel({ metric, setMetric, threshold, setThreshold, isolate, setIsolate, onRemesh, stale }: {
  metric: QualityMetric;
  setMetric: (value: QualityMetric) => void;
  threshold: number;
  setThreshold: (value: number) => void;
  isolate: boolean;
  setIsolate: (value: boolean) => void;
  onRemesh: () => void;
  stale: boolean;
}) {
  const metrics: QualityMetric[] = ["skewness", "nonOrthogonality", "aspectRatio", "cellVolume"];
  return (
    <>
      <div className="inspector-section-heading"><span>4-QUADRANT ERROR MAP</span><span className="small-count">CELL / FLAT</span></div>
      <div className="quality-map-grid">{metrics.map((item) => <MiniHeatmap key={item} metric={item} selected={item === metric} onSelect={() => setMetric(item)} />)}</div>
      <div className="quality-summary"><span><b className="red-dot" /> 2 folded faces</span><span>1.8% above limit</span></div>
      <div className="isolator-block">
        <div className="isolator-head"><span>THRESHOLD ISOLATOR</span><span>{Math.round(threshold * 100)}%</span></div>
        <input aria-label="Mesh quality threshold" type="range" min="0" max="100" value={Math.round(threshold * 100)} onChange={(event) => setThreshold(Number(event.target.value) / 100)} />
        <div className="isolator-controls"><span>show cells above limit</span><button className={`micro-toggle ${isolate ? "on" : ""}`} onClick={() => setIsolate(!isolate)} aria-label="Toggle defect isolation"><i /></button></div>
      </div>
      <div className="quality-footnote"><Icon name="layers" size={13} /><span>Metrics are flat per-cell values. NaN / folded cells are never interpolated.</span></div>
      <button className={`remesh-button ${stale ? "stale" : ""}`} onClick={onRemesh}><Icon name="refresh" size={14} />{stale ? "Regenerate mesh · bindings stale" : "Regenerate mesh"}<Icon name="arrow" size={13} /></button>
    </>
  );
}

function chartPath(series: number, tick: number) {
  const points: string[] = [];
  for (let index = 0; index < 48; index++) {
    const x = 8 + (index / 47) * 430;
    const decay = 0.79 * Math.exp(-index / (11 + series * 2.3)) + 0.015 + Math.abs(Math.sin(index * 0.27 + series * 1.1 + tick * 0.02)) * 0.015;
    const y = 12 + (1 - Math.min(1, decay)) * 86;
    points.push(`${index === 0 ? "M" : "L"}${x.toFixed(1)},${y.toFixed(1)}`);
  }
  return points.join(" ");
}

const seriesColors = ["#56a9ff", "#9b7bff", "#b9f500", "#e2a2f4", "#fec40b", "#fc75b4", "#42d4b1"];
const residualNames = ["Ux", "Uy", "Uz", "p", "h", "k", "ω"];
const metricsByMode = {
  Residuals: residualNames,
  Forces: ["Cd", "Cl", "T max"],
  Courant: ["Co mean", "Co max"],
};

function MonitorDock({
  runState,
  tick,
  activeTab,
  setActiveTab,
  log,
  onWrite,
  onInject,
  nanCount,
}: {
  runState: "running" | "paused" | "stopped";
  tick: number;
  activeTab: string;
  setActiveTab: (value: string) => void;
  log: RpcLogEntry[];
  onWrite: () => void;
  onInject: () => void;
  nanCount: number;
}) {
  const chartNames = activeTab === "IPC Stream" ? [] : metricsByMode[activeTab as keyof typeof metricsByMode];
  return (
    <section className={`monitor-dock ${nanCount > 0 ? "has-nan" : ""} ${activeTab === "Collapsed" ? "collapsed" : ""}`}>
      {nanCount > 0 && <div className="nan-alert"><Icon name="alert" size={15} /><strong>EMERGENCY · {nanCount} NaN CELLS</strong><span>Non-finite cell field detected. Solver should be stopped.</span><button onClick={onInject}>Clear test fault</button></div>}
      <header className="dock-header">
        <div className="dock-title"><span className={`live-indicator ${runState !== "running" ? "dim" : ""}`} /><div><strong>LIVE MONITOR DOCK</strong><span>NDJSON TELEMETRY <i>·</i> ITER {String(140 + tick).padStart(4, "0")}</span></div></div>
        <nav className="dock-tabs" aria-label="Monitor views">{["Residuals", "Forces", "Courant", "IPC Stream"].map((tab) => <button key={tab} className={activeTab === tab ? "selected" : ""} onClick={() => setActiveTab(tab)}>{tab}</button>)}</nav>
        <div className="dock-actions"><span className="dock-dt">Δt <b>0.0025 s</b></span><button className="icon-button" title="Write results now" onClick={onWrite}><Icon name="download" size={14} /></button><button className="icon-button" title="Inject NaN test alert" onClick={onInject}><Icon name="alert" size={14} /></button><button className="icon-button" title="Collapse monitor dock" onClick={() => setActiveTab(activeTab === "Collapsed" ? "Residuals" : "Collapsed")}><Icon name={activeTab === "Collapsed" ? "maximize" : "chevron"} size={14} /></button></div>
      </header>
      {activeTab !== "Collapsed" && <div className="dock-body">
        {activeTab === "IPC Stream" ? <div className="ipc-feed">
          <div className="ipc-feed-head"><span>CHANNEL B / JSON-RPC 2.0 · NDJSON</span><span><i className="live-indicator" /> demo transport</span></div>
          <div className="ipc-log-list">{log.slice(-6).reverse().map((entry, index) => <div className="ipc-log-row" key={`${entry.id}-${index}-${entry.time}`}><span className={`log-kind ${entry.kind}`}>{entry.kind === "stream" ? "STREAM" : entry.kind.toUpperCase()}</span><code>{entry.method}</code><span>{entry.message}</span><time>{entry.time}</time></div>)}</div>
          <div className="endpoint-strip"><span>16 methods</span><span>iter</span><span>timestep_written</span><span>stderr ring · 256 lines</span></div>
        </div> : <>
          <div className="series-legend">{chartNames.map((name, index) => <span key={name}><i style={{ backgroundColor: seriesColors[index] }} />{name}</span>)}</div>
          <div className="chart-wrap">
            <div className="chart-axis"><span>{activeTab === "Residuals" ? "1e0" : activeTab === "Courant" ? "1.2" : "0.8"}</span><span>{activeTab === "Residuals" ? "1e-2" : activeTab === "Courant" ? "0.6" : "0.4"}</span><span>{activeTab === "Residuals" ? "1e-4" : activeTab === "Courant" ? "0.0" : "0.0"}</span></div>
            <svg className="residual-chart" viewBox="0 0 450 112" preserveAspectRatio="none" aria-label={`${activeTab} live chart`}>
              {[12, 40, 68, 98].map((y) => <line key={y} x1="0" x2="450" y1={y} y2={y} className="chart-gridline" />)}
              {chartNames.map((name, index) => <path key={name} d={chartPath(index, tick)} stroke={seriesColors[index]} className="chart-series" />)}
            </svg>
            <div className="chart-time"><span>ITER {140 + tick - 48}</span><span>ITER {140 + tick}</span></div>
          </div>
          <div className="monitor-readouts">
            <div><span>Co mean</span><strong className="lime-text">0.42</strong><small>stable</small></div>
            <div><span>Co max</span><strong className="amber-text">1.08</strong><small>limit 5.0</small></div>
            <div><span>Cd</span><strong>0.432</strong><small>±0.003</small></div>
            <div><span>Cl</span><strong>0.192</strong><small>±0.001</small></div>
            <div><span>T max</span><strong>331.8 <small>K</small></strong><small>wall field</small></div>
            <div><span>n_nan_cells</span><strong className={nanCount ? "red-text" : "lime-text"}>{nanCount}</strong><small>{nanCount ? "fault" : "nominal"}</small></div>
          </div>
        </>}
      </div>}
    </section>
  );
}

function App() {
  const [activeWorkspace, setActiveWorkspace] = useState(2);
  const [blocks, setBlocks] = useState<Block[]>(() => generateBlockStack(referenceUiSchema));
  const [activeBlock, setActiveBlock] = useState("inlet");
  const [renderMode, setRenderMode] = useState<ViewportMode>("field");
  const [field, setField] = useState<FieldName>("velocity");
  const [metric, setMetric] = useState<QualityMetric>("skewness");
  const [palette, setPalette] = useState<PaletteName>("Viridis");
  const [contours, setContours] = useState(false);
  const [wireframe, setWireframe] = useState(true);
  const [isolate, setIsolate] = useState(true);
  const [threshold, setThreshold] = useState(0.58);
  const [selectedPatch, setSelectedPatch] = useState("blade-wall");
  const [pressureVariable, setPressureVariable] = useState<"p" | "p_rgh">("p");
  const [profile, setProfile] = useState("Uniform");
  const [resolution, setResolution] = useState<"dy1" | "yplus">("dy1");
  const [values, setValues] = useState<Record<string, string>>({
    geometryFile: "naca0018_revC.step", scale: "1.0", domainLength: "10 m", domainHeight: "0.8 m", domainSpan: "0.8 m",
    baseCell: "25 mm", refinement: "3 levels", material: "Air · incompressible", density: "1.204 kg/m3", viscosity: "1.5e-5 m2/s",
    inletSpeed: "12.5 m/s", inletTemperature: "293.15 K", turbulence: "0.05", firstLayer: "0.08 mm", targetYplus: "30", roughness: "0.05 mm",
    turbulenceModel: "k-ω SST", coupling: "SIMPLE", thermalModel: "Conjugate heat transfer", wallTemperature: "850 C", pressureReference: "3.2 bar", referenceElevation: "0 m", rotationalSpeed: "1500 rpm",
  });
  const [runState, setRunState] = useState<"running" | "paused" | "stopped">("running");
  const [resultTime, setResultTime] = useState("0.3500");
  const [tick, setTick] = useState(0);
  const [activeDockTab, setActiveDockTab] = useState("Residuals");
  const [rpcLog, setRpcLog] = useState<RpcLogEntry[]>([]);
  const [toast, setToast] = useState("");
  const [nanCount, setNanCount] = useState(0);
  const [geometryRevision, setGeometryRevision] = useState(18);
  const [meshRevision, setMeshRevision] = useState(18);
  const [bindingsStale, setBindingsStale] = useState(false);
  const [showReference, setShowReference] = useState(false);
  const [fullscreen, setFullscreen] = useState(false);
  const [pickMode, setPickMode] = useState(false);
  const [activeTool, setActiveTool] = useState("orbit");
  const [sliceEnabled, setSliceEnabled] = useState(false);
  const [sliceAxis, setSliceAxis] = useState<"x" | "y" | "z">("z");
  const [slicePosition, setSlicePosition] = useState(0);
  const [showSliceControls, setShowSliceControls] = useState(false);
  const [referenceTab, setReferenceTab] = useState("Overview");
  const fileInputRef = useRef<HTMLInputElement>(null);
  const canvasRef = useRef<ViewportCanvasHandle>(null);
  const rpcRef = useRef(new DemoRpcClient());
  const timeoutRef = useRef<number | undefined>(undefined);
  const emergencyHandledRef = useRef(false);

  const viewportOptions: ViewportOptions = useMemo(() => ({ mode: renderMode, metric, field, palette, threshold, contours, isolate, wireframe, selectedPatch, sliceEnabled, sliceAxis, slicePosition }), [renderMode, metric, field, palette, threshold, contours, isolate, wireframe, selectedPatch, sliceEnabled, sliceAxis, slicePosition]);
  const showReducedPressure = evaluateVisibleIf({ parameter: "pressureVariable", equals: "p_rgh" }, { pressureVariable });

  useEffect(() => {
    rpcRef.current.setLogListener((entry) => setRpcLog((previous) => [...previous, entry].slice(-80)));
    void rpcRef.current.request("system.hello", { client: "flux / cockpit", protocol: "ndjson" });
    void rpcRef.current.request("schema.get", { ui: true, schema: "ModuleRegistry.export_ui_schema" }).then(({ result }) => {
      const schema = result as ModuleUiSchema;
      if (Array.isArray(schema.modules)) setBlocks(generateBlockStack(schema));
    });
  }, []);

  useEffect(() => {
    const interval = window.setInterval(() => {
      setTick((previous) => previous + 1);
      const event = tick % 3 === 0 ? "iter" : "timestep_written";
      const entry: RpcLogEntry = {
        id: 0, method: event, message: event === "iter" ? `residual update · ${140 + tick}` : `t = ${(tick * 0.0025).toFixed(4)} s`,
        time: new Date().toLocaleTimeString("en-US", { hour12: false }), kind: "stream",
      };
      setRpcLog((previous) => [...previous, entry].slice(-80));
    }, runState === "paused" || runState === "stopped" ? 2200 : 950);
    return () => window.clearInterval(interval);
  }, [runState, tick]);

  useEffect(() => () => window.clearTimeout(timeoutRef.current), []);

  const notify = (message: string) => {
    setToast(message);
    window.clearTimeout(timeoutRef.current);
    timeoutRef.current = window.setTimeout(() => setToast(""), 2600);
  };

  const sendRpc = async (method: string, params: Record<string, unknown> = {}) => {
    await rpcRef.current.request(method, params);
  };

  useEffect(() => {
    if (nanCount > 0 && !emergencyHandledRef.current) {
      emergencyHandledRef.current = true;
      setRunState("stopped");
      void sendRpc("solver.abort", { job_id: "job-042", reason: "n_nan_cells > 0", n_nan_cells: nanCount });
      notify(`Emergency abort · ${nanCount} non-finite cells`);
    } else if (nanCount === 0) emergencyHandledRef.current = false;
  }, [nanCount]);

  const commitParameter = (key: string, parsed: ParsedUnit) => {
    if (key === "pressureReference" && pressureVariable === "p" && parsed.valueSI <= 0) {
      notify("Absolute pressure p must be greater than 0 Pa");
      return;
    }
    void sendRpc("case.validate", {
      parameter: key,
      value_si: parsed.valueSI,
      unit_si: parsed.unitSI,
      dimensions: parsed.dimensions,
      pressure_variable: key === "pressureReference" ? pressureVariable : undefined,
      geometry_revision: geometryRevision,
      mesh_revision: meshRevision,
    });
  };

  const updateValue = (key: string, value: string) => setValues((previous) => ({ ...previous, [key]: value }));

  const handlePatchPick = (patch: string) => {
    setSelectedPatch(patch);
    setPickMode(false);
    const group = patch === "blade-wall" ? 1 : patch === "top-wall" || patch === "bottom-wall" ? 2 : patch === "inlet" ? 3 : 4;
    void sendRpc("geometry.patches.update", { patch, face_group_id: group, geometry_revision: geometryRevision, mesh_revision: meshRevision });
    setBindingsStale(false);
    notify(`ID buffer · face_group_id ${group} → ${patch}`);
  };

  const handlePatchUpdate = () => {
    const faceGroupId = selectedPatch === "blade-wall" ? 1 : selectedPatch === "top-wall" || selectedPatch === "bottom-wall" ? 2 : selectedPatch === "inlet" ? 3 : 4;
    void sendRpc("geometry.patches.update", { patch: selectedPatch, face_group_id: faceGroupId, geometry_revision: geometryRevision, mesh_revision: meshRevision });
    setBindingsStale(false);
    notify(`Patch ${selectedPatch} assigned at mesh revision ${meshRevision}`);
  };

  const handleRemesh = async () => {
    await sendRpc("mesh.generate", { geometry_revision: geometryRevision, source_mesh_revision: meshRevision });
    setMeshRevision((revision) => revision + 1);
    setBindingsStale(true);
    notify("Mesh revision advanced. Patch bindings need review.");
  };

  const handleImport = () => fileInputRef.current?.click();

  const onFileSelected = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;
    updateValue("geometryFile", file.name);
    setGeometryRevision((revision) => revision + 1);
    setBindingsStale(true);
    void sendRpc("geometry.import", { file_name: file.name, geometry_revision: geometryRevision + 1 });
    notify(`Queued geometry.import · ${file.name}`);
    event.target.value = "";
  };

  const startSolver = async () => {
    setRunState("running");
    await sendRpc("case.validate", { geometry_revision: geometryRevision, mesh_revision: meshRevision, strict_si: true });
    await sendRpc("case.generate", { geometry_revision: geometryRevision, mesh_revision: meshRevision, pressure_variable: pressureVariable });
    await sendRpc("solver.start", { geometry_revision: geometryRevision, mesh_revision: meshRevision, field_dimensions: [0, 0, 0, 0, 0, 0, 0] });
    notify("Solver start queued · telemetry stream active");
  };

  const stopSolver = () => {
    setRunState("stopped");
    void sendRpc("solver.stop_graceful", { flush: true, job_id: "job-042" });
    notify("Graceful stop requested · output flush enabled");
  };

  const pauseSolver = () => {
    if (runState === "running") {
      setRunState("paused");
      void sendRpc("job.cancel", { job_id: "job-042", cooperative: true });
      notify("Cooperative job cancellation requested");
    } else startSolver();
  };

  const abortSolver = () => {
    setRunState("stopped");
    void sendRpc("solver.abort", { job_id: "job-042", reason: "user_abort" });
    notify("Immediate solver abort requested");
  };

  const writeNow = () => {
    void sendRpc("solver.write_now", { time: 0.35, fields: ["U", "p", "T", "wallShearStress"] });
    notify("Write-now command queued");
  };

  const injectFault = () => {
    if (nanCount > 0) { setNanCount(0); notify("Test fault cleared · NaN guard remains armed"); }
    else { setNanCount(3); notify("Non-finite cell injection · emergency alert armed"); }
  };

  const chooseWorkspace = (id: number) => {
    setActiveWorkspace(id);
    if (id === 1) setRenderMode("quality");
    else if (id === 3 || id === 4) setRenderMode("field");
    else if (id === 0 || id === 2) setRenderMode(id === 2 ? "field" : "mesh");
    if (id === 4) {
      void sendRpc("results.list_times", { case_id: "NACA-07" });
      void sendRpc("results.list_fields", { case_id: "NACA-07" });
    }
  };

  const chooseMode = (mode: ViewportMode) => {
    setRenderMode(mode);
    if (mode === "quality") setActiveWorkspace(1);
    if (mode === "field") setActiveWorkspace(4);
  };

  const downloadCase = () => {
    const payload = { schema: "CFDCase/v1", geometry_revision: geometryRevision, mesh_revision: meshRevision, pressure_variable: pressureVariable, parameters_si: values, blocks };
    const url = URL.createObjectURL(new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" }));
    const link = document.createElement("a"); link.href = url; link.download = "flux-case-reference.json"; link.click(); URL.revokeObjectURL(url);
    notify("Case parameters exported as JSON");
  };

  const toggleFullscreen = async () => {
    try {
      if (!document.fullscreenElement) { await document.documentElement.requestFullscreen(); setFullscreen(true); }
      else { await document.exitFullscreen(); setFullscreen(false); }
    } catch { notify("Fullscreen is not available in this preview"); }
  };

  const addBlock = () => {
    if (blocks.some((block) => block.id === "surface-lic")) { setActiveBlock("surface-lic"); return; }
    const licBlock: Block = { id: "surface-lic", type: "POST / 10", title: "Surface-LIC analysis", summary: "wall shear · GPU compute", accent: "lime", status: "NEW" };
    setBlocks((previous) => [...previous, licBlock]);
    setActiveBlock("surface-lic");
    notify("Surface-LIC block added from schema registry");
  };

  return (
    <div className="app-shell">
      <header className="topbar">
        <button className="brand-lockup" onClick={() => setShowReference(!showReference)} aria-label="Open FLUX reference information">
          <span className="brand-mark"><Icon name="mark" size={21} /></span>
          <span className="brand-copy"><strong>FLUX<span>/</span>CFD</strong><small>ENGINEERING STUDIO</small></span>
        </button>
        <div className="topbar-divider" />
        <nav className="workspace-nav" aria-label="Workspaces">{workspaces.map((workspace) => <button key={workspace.id} className={`workspace-tab ${activeWorkspace === workspace.id ? "active" : ""}`} onClick={() => chooseWorkspace(workspace.id)}>
          <span className="workspace-number">{workspace.id + 1}</span><Icon name={workspace.icon} size={14} /><span className="workspace-name">{workspace.name}</span><span className="workspace-short">{workspace.short}</span>
        </button>)}</nav>
        <div className="topbar-spacer" />
        <div className="top-project"><span className="project-kicker">PROJECT</span><button onClick={downloadCase}>NACA / 07 <Icon name="chevron" size={13} /></button></div>
        <div className="top-status"><span className="status-led" /><span>DEMO FEED</span><button className="status-more" onClick={() => setActiveDockTab("IPC Stream")} title="Open IPC stream"><Icon name="radio" size={14} /></button></div>
        <button className="header-icon" title="Export case JSON" onClick={downloadCase}><Icon name="download" size={15} /></button>
        <button className="run-button" onClick={runState === "running" ? pauseSolver : startSolver}><Icon name={runState === "running" ? "stop" : "play"} size={13} />{runState === "running" ? "CANCEL JOB" : runState === "paused" ? "RESUME" : "RUN CASE"}<span className="run-key">⌘↵</span></button>
        {runState !== "stopped" && <><button className="stop-button" onClick={stopSolver} title="Graceful solver stop"><Icon name="stop" size={13} /></button><button className="abort-button" onClick={abortSolver} title="Abort solver immediately"><Icon name="alert" size={13} /></button></>}
        {runState === "stopped" && <button className="stop-button restart" onClick={startSolver} title="Restart solver"><Icon name="refresh" size={13} /></button>}
      </header>

      <div className="subbar">
        <div className="breadcrumbs"><span>CASES</span><b>/</b><span>NACA / 07</span><b>/</b><strong>{workspaces[activeWorkspace].name.toUpperCase()}</strong></div>
        <div className="revision-track"><span>GEOM <b>r{geometryRevision}</b></span><span className="revision-link" /><span>MESH <b className={bindingsStale ? "amber-text" : ""}>r{meshRevision}</b></span>{bindingsStale && <span className="stale-tag"><Icon name="alert" size={10} /> BINDINGS STALE</span>}</div>
        <span className="subbar-spacer" />
        <button className={`subbar-link ${pickMode ? "is-selected" : ""}`} onClick={() => setPickMode(!pickMode)}><Icon name="cursor" size={13} />{pickMode ? "PICK PATCH · ON" : "PICK PATCH"}</button>
        <button className="subbar-link" onClick={() => setShowReference(true)}><Icon name="code" size={13} />REFERENCE</button>
        <span className="sync-label"><span className="sync-dot" />SYNCED <time>14:32:08</time></span>
      </div>

      <main className="workspace-layout">
        <aside className="left-panel">
          <div className="panel-heading">
            <div><span className="panel-eyebrow">SCHEMA / 3.8</span><h2>BLOCK GRAPH <span>07</span></h2></div>
            <button className="square-button" onClick={addBlock} title="Add a physics block"><Icon name="plus" size={15} /></button>
          </div>
          <div className="graph-context"><span className="graph-context-mark"><Icon name="layers" size={15} /></span><div><strong>external flow / NACA 0018</strong><small>{blocks.length} declarative blocks · 3 active</small></div><button className="icon-button" title="Block graph actions"><Icon name="more" size={15} /></button></div>
          <div className="block-list">
            {blocks.map((block, index) => <article key={block.id} className={`block-node accent-${block.accent} ${activeBlock === block.id ? "is-active" : ""}`} style={{ "--block-accent": `var(--${block.accent})` } as CSSProperties}>
              <button className="block-node-head" onClick={() => setActiveBlock(activeBlock === block.id ? "" : block.id)}>
                <span className="node-rail" />
                <span className="node-index">{String(index + 1).padStart(2, "0")}</span>
                <span className="node-main"><span className="node-type">{block.type}</span><strong>{block.title}</strong><small>{activeBlock === block.id ? block.summary : block.summary}</small></span>
                <span className={`node-state ${block.status === "ACTIVE" || block.status === "BUILT" ? "state-good" : ""}`}>{block.status}</span>
                <Icon name="chevron" size={13} />
              </button>
              {activeBlock === block.id && <BlockEditor id={block.id} values={values} profile={profile} resolution={resolution} onValue={(key, value) => { updateValue(key, value); if (key === "material") void sendRpc("materials.list", { selected: value }); }} onCommit={commitParameter} onProfile={(value) => { setProfile(value); void sendRpc("case.validate", { parameter: "inlet.profile", value }); }} onResolution={setResolution} onImport={handleImport} />}
            </article>)}
            <button className="add-block-row" onClick={addBlock}><Icon name="plus" size={13} /><span>ADD BLOCK FROM REGISTRY</span><kbd>A</kbd></button>
          </div>
          <div className="left-footer"><span><Icon name="check" size={12} /> ALL INPUTS VALID</span><button onClick={() => { setActiveDockTab("IPC Stream"); notify("RPC request / response stream opened"); }}><Icon name="terminal" size={12} /> 2 STREAMS</button></div>
          <input ref={fileInputRef} type="file" accept=".step,.stp,.iges,.igs,.obj,.stl" hidden onChange={onFileSelected} />
        </aside>

        <section className="center-panel">
          <div className="viewport-toolbar">
            <div className="view-tools"><span className="viewport-breadcrumb">{renderMode === "field" ? "SOLUTION" : renderMode === "quality" ? "MESH INSPECTION" : "GEOMETRY"}</span><span className="toolbar-divider" />
              {(["cursor", "orbit", "move"] as const).map((tool) => <button key={tool} className={`tool-button ${activeTool === tool ? "selected" : ""}`} title={tool === "cursor" ? "Select / pick" : tool === "orbit" ? "Orbit camera" : "Pan camera"} onClick={() => setActiveTool(tool)}><Icon name={tool} size={14} /></button>)}
              <button className="tool-button" title="Fit view" onClick={() => canvasRef.current?.resetCamera()}><Icon name="fit" size={14} /></button>
            </div>
            <div className="toolbar-modes" aria-label="Viewport render mode">{(["mesh", "quality", "field"] as ViewportMode[]).map((mode) => <button key={mode} className={renderMode === mode ? "selected" : ""} onClick={() => chooseMode(mode)}>{mode === "mesh" ? "SURFACE" : mode === "quality" ? "QUALITY" : "FIELD"}</button>)}</div>
            <div className="toolbar-options">
              {renderMode === "field" && <label className="toolbar-select"><span>FIELD</span><select aria-label="Scientific field" value={field} onChange={(event) => setField(event.target.value as FieldName)}><option value="velocity">|U| m/s</option><option value="pressure">p · Pa</option><option value="temperature">T · K</option><option value="wall">τw · Pa</option></select><Icon name="chevron" size={11} /></label>}
              {activeWorkspace === 4 && <><label className="toolbar-select"><span>TIME</span><select value={resultTime} aria-label="Result time" onChange={(event) => setResultTime(event.target.value)}><option value="0.1000">0.1000 s</option><option value="0.2500">0.2500 s</option><option value="0.3500">0.3500 s</option><option value="0.5000">0.5000 s</option></select><Icon name="chevron" size={11} /></label><button className="tool-button" title="Load selected result time" onClick={() => { void sendRpc("results.load", { time: Number(resultTime), field, location: "Cell" }); notify(`Loading ${field} at t=${resultTime} s`); }}><Icon name="refresh" size={14} /></button></>}
              {renderMode === "quality" && <label className="toolbar-select"><span>METRIC</span><select value={metric} aria-label="Mesh quality metric" onChange={(event) => setMetric(event.target.value as QualityMetric)}><option value="skewness">Skewness</option><option value="nonOrthogonality">Non-orthogonality</option><option value="aspectRatio">Aspect ratio</option><option value="cellVolume">Cell volume</option></select><Icon name="chevron" size={11} /></label>}
              {renderMode !== "mesh" && <label className="palette-control"><span className="palette-swatch" /><select value={palette} aria-label="Colormap" onChange={(event) => setPalette(event.target.value as PaletteName)}><option>Viridis</option><option>Dark Ember</option><option>Fire</option><option>Cool-Warm</option><option>Traffic Light</option></select><Icon name="chevron" size={11} /></label>}
              <button className={`tool-button ${sliceEnabled ? "selected" : ""}`} title="Open 3D slice plane controls" onClick={() => setShowSliceControls(!showSliceControls)}><Icon name="layers" size={14} /></button>
              <button className={`tool-button ${wireframe ? "selected" : ""}`} title="Toggle mesh edges" onClick={() => setWireframe(!wireframe)}><Icon name="mesh" size={14} /></button>
              {renderMode !== "mesh" && <button className={`tool-button ${contours ? "selected" : ""}`} title="Toggle contour bands" onClick={() => setContours(!contours)}><Icon name="filter" size={14} /></button>}
              <button className="tool-button" title={fullscreen ? "Exit full screen" : "Full screen"} onClick={toggleFullscreen}><Icon name="maximize" size={14} /></button>
            </div>
          </div>

          <div className={`viewport-wrap ${pickMode ? "pick-active" : ""}`}>
            <ViewportCanvas ref={canvasRef} options={viewportOptions} onPatchPick={handlePatchPick} />
            <div className="viewport-overlay-top"><div className="scene-heading"><span className="scene-icon"><Icon name={renderMode === "quality" ? "mesh" : renderMode === "field" ? "activity" : "cube"} size={14} /></span><div><strong>{renderMode === "quality" ? "Hex-dominant mesh" : renderMode === "field" ? `${field === "velocity" ? "Velocity magnitude" : field === "pressure" ? pressureVariable : field === "temperature" ? "Temperature" : "Wall shear stress"}` : "Surface / MatCap"}</strong><small>{renderMode === "quality" ? "CELL-BASED ERROR FIELD · 2.84 M" : renderMode === "field" ? `CFDFLD03 · cell · ${field === "velocity" ? "m/s" : field === "pressure" ? "Pa" : field === "temperature" ? "K" : "Pa"}` : "CFDGEO01 · f64 POSITION"}</small></div></div><div className="scene-tags"><span>Δy₁ <b>0.08 mm</b></span><span>Y⁺ <b>32.4</b></span></div></div>
            {showSliceControls && <div className="slice-controls">
              <div className="slice-control-head"><span><Icon name="layers" size={13} />3D SLICE PLANE</span><button className={`micro-toggle ${sliceEnabled ? "on" : ""}`} onClick={() => setSliceEnabled(!sliceEnabled)} aria-label="Toggle slice plane"><i /></button></div>
              <div className="slice-axis-row">{(["x", "y", "z"] as const).map((axis) => <button key={axis} className={sliceAxis === axis ? "selected" : ""} onClick={() => setSliceAxis(axis)}>{axis.toUpperCase()}</button>)}<span>normal axis</span></div>
              <label className="slice-position-control"><span>PLANE OFFSET <b>{slicePosition >= 0 ? "+" : ""}{slicePosition.toFixed(3)} m</b></span><input aria-label="Slice plane offset" type="range" min="-0.24" max="0.24" step="0.004" value={slicePosition} onChange={(event) => setSlicePosition(Number(event.target.value))} /></label>
              <div className="slice-control-foot"><span className={`slice-indicator ${sliceEnabled ? "active" : ""}`} />{sliceEnabled ? "CLIP HALF-SPACE · CAMERA RELATIVE" : "PLANE DISABLED"}<button onClick={() => setSlicePosition(0)} title="Center slice plane">CENTER</button></div>
            </div>}
            {pickMode && <div className="pick-instruction"><span className="pick-ring"><Icon name="cursor" size={14} /></span><span>CLICK A SURFACE TO ASSIGN PATCH</span><button onClick={() => setPickMode(false)}><Icon name="close" size={13} /></button></div>}
            <div className="view-axis"><span className="axis-y">Y</span><span className="axis-x">X</span><span className="axis-z">Z</span><i /></div>
            <div className="view-scale"><span>1.0 m</span><i /></div>
            <div className="scene-bottom-left"><span className="scene-coordinate">WORLD / METERS</span><span className="scene-axis-readout"><i>X</i> 0.000 <i>Y</i> 0.000 <i>Z</i> 0.000</span></div>
            <div className="legend-block"><span>{renderMode === "quality" ? metric.replace(/([A-Z])/g, " $1").toUpperCase() : field === "velocity" ? "VELOCITY · m/s" : field === "pressure" ? `${pressureVariable.toUpperCase()} · Pa` : field === "temperature" ? "TEMPERATURE · K" : "WALL SHEAR · Pa"}</span><div className={`legend-gradient palette-${palette.toLowerCase().replace(/[^a-z]+/g, "-")}`} /><div className="legend-values"><span>{renderMode === "quality" ? "0.00" : field === "velocity" ? "0.0" : field === "pressure" ? "98.2k" : field === "temperature" ? "288" : "0.0"}</span><span>{renderMode === "quality" ? "1.00" : field === "velocity" ? "18.6" : field === "pressure" ? "102.4k" : field === "temperature" ? "354" : "42.0"}</span></div></div>
            {renderMode === "quality" && <div className="quality-view-stamp"><span className="red-dot" />ISOLATOR ACTIVE <b>{Math.round(threshold * 100)}%</b><small>2 folded faces · 0 NaN</small></div>}
            {renderMode === "field" && <div className="field-view-stamp"><span className="live-indicator" />STREAMLINE TRACE <b>GPU / 128</b></div>}
            <div className="canvas-nav-hint"><Icon name="orbit" size={12} /> DRAG ORBIT <span /> SCROLL ZOOM</div>
          </div>

          <MonitorDock runState={runState} tick={tick} activeTab={activeDockTab} setActiveTab={setActiveDockTab} log={rpcLog} onWrite={writeNow} onInject={injectFault} nanCount={nanCount} />
        </section>

        <aside className="right-panel">
          <div className="panel-heading inspector-heading"><div><span className="panel-eyebrow">INSPECTOR / {renderMode === "quality" ? "MESH" : "PATCH"}</span><h2>PROPERTIES <Icon name="more" size={14} /></h2></div><button className="square-button" title="Inspector options"><Icon name="settings" size={14} /></button></div>
          <div className="selection-summary"><span className="selection-glyph"><Icon name={renderMode === "quality" ? "mesh" : "layers"} size={15} /></span><div><small>ACTIVE SELECTION</small><strong>{renderMode === "quality" ? "quality / cell-set" : selectedPatch.replace("-", " ").toUpperCase()}</strong></div><button className="icon-button" title="Clear selection" onClick={() => setSelectedPatch("")}><Icon name="close" size={13} /></button></div>

          {renderMode === "quality" ? <div className="inspector-content quality-inspector">
            <QualityPanel metric={metric} setMetric={setMetric} threshold={threshold} setThreshold={setThreshold} isolate={isolate} setIsolate={setIsolate} onRemesh={handleRemesh} stale={bindingsStale} />
            <div className="inspector-section-heading boundary-heading"><span>PATCH BINDINGS</span><span className="small-count">REV {meshRevision}</span></div>
            <p className="patch-stale-copy">{bindingsStale ? "Remesh invalidated patch bindings. Review and reassign before generating a case." : "Face groups are bound to the current mesh revision."}</p>
          </div> : <div className="inspector-content">
            <div className="inspector-section-heading"><span>BOUNDARY ASSIGNMENT</span><span className="small-count">{bindingsStale ? "STALE" : "REV " + meshRevision}</span></div>
            <div className="patch-selector-wrap"><span className="patch-color" style={{ background: selectedPatch === "blade-wall" ? "var(--pink)" : selectedPatch === "inlet" ? "var(--blue)" : selectedPatch === "outlet" ? "var(--amber)" : "var(--lime)" }} /><select value={selectedPatch} onChange={(event) => setSelectedPatch(event.target.value)} aria-label="Select boundary patch"><option value="blade-wall">blade-wall / wall</option><option value="inlet">inlet / velocity-inlet</option><option value="outlet">outlet / pressure-outlet</option><option value="top-wall">top-wall / slip</option><option value="bottom-wall">bottom-wall / wall</option></select><Icon name="chevron" size={13} /></div>
            <div className="patch-data-grid"><div><span>face_group_id</span><b>{selectedPatch === "blade-wall" ? "01" : selectedPatch === "inlet" ? "03" : selectedPatch === "outlet" ? "04" : "02"}</b></div><div><span>faces</span><b>{selectedPatch === "blade-wall" ? "18,240" : selectedPatch === "inlet" ? "1,088" : "864"}</b></div><div><span>source</span><b>CFDGEO01</b></div><div><span>binding</span><b className={bindingsStale ? "amber-text" : "lime-text"}>{bindingsStale ? "STALE" : "VALID"}</b></div></div>
            <div className="patch-actions"><button className={`outline-action ${pickMode ? "is-selected" : ""}`} onClick={() => setPickMode(!pickMode)}><Icon name="cursor" size={13} />{pickMode ? "Click viewport to select" : "Pick faces in viewport"}</button><button className="square-button" onClick={handlePatchUpdate} title="Apply patch binding"><Icon name="check" size={14} /></button></div>

            <div className="inspector-divider" />
            <div className="inspector-section-heading"><span>PRESSURE FIELD</span><span className="small-count">7 SI DIMENSIONS</span></div>
            <div className="pressure-segment"><button className={pressureVariable === "p" ? "selected" : ""} onClick={() => setPressureVariable("p")}><b>p</b><span>absolute</span></button><button className={pressureVariable === "p_rgh" ? "selected" : ""} onClick={() => setPressureVariable("p_rgh")}><b>p_rgh</b><span>reduced</span></button></div>
            <div className="pressure-note"><span className="pressure-symbol">{pressureVariable === "p" ? "p" : "pᵣgh"}</span><span>{pressureVariable === "p" ? "Absolute thermodynamic pressure. Must remain > 0 Pa." : "Relative pressure with hydrostatic head removed. May be negative."}</span></div>
            <UnitField label={pressureVariable === "p" ? "Reference absolute pressure" : "Reference reduced pressure"} value={values.pressureReference} dimension="pressure" helper="bar · kPa · MPa · Pa" onChange={(value) => updateValue("pressureReference", value)} onCommit={(parsed) => commitParameter("pressureReference", parsed)} />
            {showReducedPressure && <><div className="conditional-tag">visible_if: pressure_variable == p_rgh</div><UnitField label="Reference elevation" value={values.referenceElevation} dimension="length" onChange={(value) => updateValue("referenceElevation", value)} onCommit={(parsed) => commitParameter("referenceElevation", parsed)} /></>}
            <div className="dimensions-row"><span>SI dimensions</span><code>[1, -1, -2, 0, 0, 0, 0]</code><small>[kg, m, s, K, mol, A, cd]</small></div>
            <div className="inspector-divider" />
            <div className="inspector-section-heading"><span>FIELD SOURCE</span><span className="source-badge">OWNER FALLBACK</span></div>
            <div className="source-note"><Icon name="link" size={13} /><span>BoundaryField preferred. OwnerCellFallback used for unset patch values.</span></div>
            <div className="field-location"><span>location</span><b>BoundaryPatch</b><span>source</span><b>BoundaryField</b></div>
            <button className="patch-action-wide" onClick={handlePatchUpdate}><Icon name="check" size={14} />APPLY PATCH BINDING <Icon name="arrow" size={13} /></button>
          </div>}
          <div className="right-footer"><span><Icon name="lock" size={12} /> REVISION LOCKED</span><span>SI / BASE</span></div>
        </aside>
      </main>

      <footer className="statusbar">
        <div className="status-left"><span className="status-green"><i />{runState === "running" ? "SOLVING / DEMO" : runState.toUpperCase()}</span><span>ITER {String(140 + tick).padStart(4, "0")}</span><span>Co <b>0.42</b></span><span>MEM <b>5.1 GB</b></span></div>
        <div className="status-center"><span>gpu</span><b>WebGL2</b><span>·</span><span>f64 → camera-relative f32</span></div>
        <div className="status-right"><span>NDJSON / STDIO ADAPTER</span><span className="status-green"><i />CONNECTED <small>(SIMULATED)</small></span><button onClick={() => setShowReference(true)}><Icon name="code" size={12} /></button></div>
      </footer>

      {toast && <div className="toast-message"><span className="toast-mark"><Icon name="check" size={14} /></span>{toast}</div>}

      {showReference && <div className="reference-scrim" onMouseDown={(event) => { if (event.target === event.currentTarget) setShowReference(false); }}>
        <section className="reference-drawer">
          <header className="reference-header"><div className="reference-title-mark"><Icon name="code" size={17} /></div><div><small>REFERENCE IMPLEMENTATION</small><h2>FLUX / CFD COCKPIT</h2></div><button className="square-button" onClick={() => setShowReference(false)}><Icon name="close" size={15} /></button></header>
          <nav className="reference-tabs">{["Overview", "Rust crate", "WGSL", "RPC"].map((tab) => <button key={tab} className={referenceTab === tab ? "selected" : ""} onClick={() => setReferenceTab(tab)}>{tab}</button>)}</nav>
          {referenceTab === "Overview" ? <div className="reference-content"><p>A desktop-first, schema-driven CFD cockpit reference. The preview runs an interactive WebGL2 viewport and a simulated NDJSON stream; attach the native stdio transport to replace the demo adapter.</p><div className="reference-grid"><div><span>CHANNEL A</span><strong>Zero-copy binary buffers</strong><small>CFDGEO01 · CFDMSH02 · CFDFLD03</small></div><div><span>CHANNEL B</span><strong>Async JSON-RPC 2.0</strong><small>16 methods · iter · timestep_written</small></div><div><span>RENDERER</span><strong>WebGL2 live preview</strong><small>ID-buffer pick · flat cell metrics</small></div><div><span>UNITS</span><strong>Strict SI normalization</strong><small>[kg, m, s, K, mol, A, cd]</small></div></div><p className="reference-path-note">Rust and WGSL source modules are in <code>reference/rust/</code> and <code>reference/shaders/</code>.</p></div> : referenceTab === "Rust crate" ? <div className="reference-file-list">{[
            ["Cargo.toml", "wgpu · egui · tokio · serde"], ["src/lib.rs", "egui app shell + typed module registry"], ["src/ipc.rs", "NDJSON stdio bridge + cancellation"], ["src/viewport.rs", "reverse-Z camera + two-pass renderer"], ["src/mesh_quality.rs", "flat per-cell defect isolator"],
          ].map(([file, label]) => <div key={file}><Icon name="file" size={14} /><strong>{file}</strong><span>{label}</span><Icon name="check" size={12} /></div>)}</div> : referenceTab === "WGSL" ? <div className="reference-file-list">{[["mesh_inspect.wgsl", "MatCap · EDL · ID pass · reverse-Z"], ["field_unlit.wgsl", "Oklab LUT · flat-cell metrics · isolator"], ["streamline.wgsl", "Instanced ribbon / slice-plane output"], ["surface_lic.wgsl", "Wall-shear Surface-LIC kernel"]].map(([file, label]) => <div key={file}><Icon name="layers" size={14} /><strong>{file}</strong><span>{label}</span><Icon name="check" size={12} /></div>)}</div> : <div className="reference-rpc-list">{rpcEndpoints.map((method, index) => <button key={method} onClick={() => { void sendRpc(method, { preview: true }); notify(`${method} queued`); }}><span>{String(index + 1).padStart(2, "0")}</span><code>{method}</code><Icon name="arrow" size={11} /></button>)}<div className="rpc-stream-foot"><span>LIVE STREAMS</span><code>iter</code><code>timestep_written</code><small>NDJSON · stdout</small></div></div>}
          <div className="reference-footer"><span><span className="live-indicator" />PREVIEW INSTANCE</span><button onClick={downloadCase}><Icon name="download" size={13} />Export current case</button></div>
        </section>
      </div>}
    </div>
  );
}

export default App;