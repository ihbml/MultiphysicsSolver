import { useCallback, useEffect, useRef, useState, type CSSProperties, type ReactNode } from "react";
import { AnimatePresence, motion, useReducedMotion } from "motion/react";
import {
  Activity, ArrowDownToLine, ArrowRight, AudioWaveform, BookOpen, ChartNoAxesCombined, Check, ChevronDown,
  ChevronRight, CircleDot, CircleHelp, Cpu, Droplets, FlaskConical, Grid3X3, Layers2, Maximize2, Minimize2,
  Pause, Play, RotateCcw, Scan, ShieldCheck, SkipBack, SkipForward, SlidersHorizontal, Waves, X,
  type LucideIcon,
} from "lucide-react";
import { FlowPlot, Trend } from "./Plots";
import {
  BASE, cases, defaults, diagnosticName, fieldsFor, formatExp, reference,
  type CaseId, type Check as Measurement, type Dataset, type Frame, type Reply, type Report, type Request, type Settings,
} from "./simulation";

const caseOrder: CaseId[] = ["bubble", "sod", "acoustic", "film"];
const caseIcons: Record<CaseId, LucideIcon> = { bubble: CircleDot, sod: Activity, acoustic: AudioWaveform, film: Droplets };
const schemeOptions = [[0, "All-Mach HLLC"], [1, "AUSM+up"], [2, "Standard HLLC"]] as const;
const eosOptions = [[0, "Ideal Gas"], [1, "Stiffened Gas"], [2, "Peng-Robinson"]] as const;
const tabDefs = [
  { id: "flow", name: "Flow Field", icon: Waves },
  { id: "diagnostics", name: "Diagnostics", icon: ChartNoAxesCombined },
  { id: "verification", name: "Verification", icon: ShieldCheck },
] as const;
type Tab = (typeof tabDefs)[number]["id"];

function Brand() {
  return <div className="brand">
    <div className="brand-mark" aria-hidden="true">
      <svg width="26" height="26" viewBox="0 0 32 32" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinejoin="round" strokeLinecap="round">
        <path d="M16 4.5 28 11.5 16 18.5 4 11.5 16 4.5Z" fill="currentColor" fillOpacity=".13" />
        <path d="m4 16.5 12 7 12-7M4 21.5l12 7 12-7" /><path d="m11 11.5 5-2.9 5 2.9" opacity=".53" />
      </svg>
    </div>
    <div><div className="brand-name">Cluster B</div><div className="brand-caption">MULTIPHYSICS SOLVER SUITE</div></div>
  </div>;
}

function SelectControl({ options, value, change, label, className = "" }: {
  options: readonly (readonly [number, string])[]; value: number; change: (n: number) => void; label: string; className?: string;
}) {
  return <div className={`select-control ${className}`}>
    <select value={value} onChange={e => change(+e.target.value)} aria-label={label}>
      {options.map(([id, text]) => <option key={id} value={id}>{text}</option>)}
    </select><ChevronDown size={13} aria-hidden="true" />
  </div>;
}

function Toggle({ label, hint, enabled, onChange }: { label: string; hint?: string; enabled: boolean; onChange: () => void }) {
  return <button className="toggle-line" type="button" role="switch" aria-checked={enabled} aria-label={label} onClick={onChange}>
    <span>{label}{hint && <small>{hint}</small>}</span><i className={`switch ${enabled ? "on" : ""}`} />
  </button>;
}

function Sidebar({ current, choose, settings, change, dirty }: {
  current: CaseId; choose: (id: CaseId) => void; settings: Settings; change: (p: Partial<Settings>) => void; dirty: boolean;
}) {
  const [open, setOpen] = useState(false);
  return <aside className="sidebar" aria-label="Simulation cases and solver configuration">
    <div className="case-section">
      <div className="section-label case-label"><span>Benchmark Cases</span><span className="section-count">04</span></div>
      <nav className="case-list" aria-label="Benchmark cases">
        {caseOrder.map(id => {
          const c = cases[id], Icon = caseIcons[id];
          return <button key={id} type="button" className={`case-button ${id === current ? "selected" : ""}`} aria-current={id === current ? "page" : undefined} onClick={() => choose(id)}>
            <div className="case-icon"><Icon size={17} strokeWidth={1.65} /></div>
            <span className="case-copy"><b>{c.title}</b><small>{c.short}</small></span>
            {id === current && <span className="case-active" aria-hidden="true" />}
          </button>;
        })}
      </nav>
    </div>
    <div className={`config-section ${open ? "config-open" : ""}`}>
      <div className="section-label config-label"><span>Solver Configuration</span><SlidersHorizontal size={14} /></div>
      <button type="button" className="section-label config-mobile" aria-expanded={open} onClick={() => setOpen(v => !v)}>
        <SlidersHorizontal size={14} /><span>Solver Configuration</span><ChevronDown size={14} />
      </button>
      <div className="config-controls">
        {current !== "film" ? <>
          <label className="config-field"><span>Riemann Solver</span>
            <SelectControl label="Riemann Solver" value={settings.scheme} options={schemeOptions} change={scheme => change({ scheme })} />
          </label>
          <label className="config-field"><span>Equation of State</span>
            <SelectControl label="Equation of State" value={settings.eos} options={eosOptions} change={eos => change({ eos })} />
          </label>
        </> : <div className="film-config"><b>Finite-area transport</b><span>Water film &middot; 998 kg/m&sup3;</span></div>}
        <label className="config-field cfl-field">
          <div className="range-header"><span>{current === "film" ? "Step Scale" : "CFL Number"}</span><output>{settings.cfl.toFixed(2)}</output></div>
          <input type="range" className="slider config-range" aria-label="CFL Number" min="0.10" max="0.65" step="0.01" value={settings.cfl}
            style={{ "--progress": `${(settings.cfl - .10) / .55 * 100}%` } as CSSProperties} onChange={e => change({ cfl: +e.target.value })} />
          <div className="range-bounds"><span>0.10</span><span>0.65</span></div>
        </label>
        {current === "bubble" && <div className="toggle-stack">
          <Toggle label="SPH-FVM Coupling" hint="OPMIS transition zone" enabled={settings.hybrid} onChange={() => change({ hybrid: !settings.hybrid })} />
          <Toggle label="Cavitation" hint="Schnerr-Sauer model" enabled={settings.cavitation} onChange={() => change({ cavitation: !settings.cavitation })} />
        </div>}
        {dirty && <div className="config-notice"><CircleDot size={12} /> Apply changes with Run Simulation</div>}
      </div>
    </div>
    <div className="core-footer"><span className="core-icon"><Cpu size={17} strokeWidth={1.55} /></span><div><b>Cluster B Core</b><small>C++20 / WebAssembly</small></div></div>
  </aside>;
}

function IconButton({ icon: Icon, label, action, pressed, disabled, className = "" }: {
  icon: LucideIcon; label: string; action: () => void; pressed?: boolean; disabled?: boolean; className?: string;
}) {
  return <button type="button" className={`icon-button ${pressed ? "pressed" : ""} ${className}`} aria-label={label}
    title={label} aria-pressed={pressed} onClick={action} disabled={disabled}><Icon size={16} strokeWidth={1.75} /></button>;
}

function Timeline({ data, frame, position, seek, playing, toggle, speed, setSpeed, disabled }: {
  data: Dataset | null; frame?: Frame; position: number; seek: (p: number) => void; playing: boolean; toggle: () => void;
  speed: number; setSpeed: (n: number) => void; disabled: boolean;
}) {
  const [first, last] = data ? [data.frames.findIndex(Boolean), data.frames.reduce((n, f, i) => f ? i : n, -1)] : [0, 0];
  const ready = first === 0 && last >= first;
  return <div className="timeline" aria-label="Simulation timeline">
    <div className="transport">
      <IconButton className="skip" icon={SkipBack} label="Previous frame" action={() => seek(position - 1)} disabled={!ready || position === 0} />
      <IconButton className="timeline-play" icon={playing ? Pause : Play} label={playing ? "Pause playback" : "Play timeline"} action={toggle} disabled={disabled} />
      <IconButton className="skip" icon={SkipForward} label="Next frame" action={() => seek(position + 1)} disabled={!ready || position >= last} />
    </div>
    <div className="time-track">
      <div className="timeline-info"><span>Simulation Timeline</span><output>{frame ? String(position + 1).padStart(2, "0") : "--"} <em>/</em> {data?.count ?? "--"} <span>frames</span></output></div>
      <input type="range" aria-label="Simulation frame" className="slider timeline-range" min={0} max={Math.max((data?.count ?? 2) - 1, 1)}
        step={1} value={position} style={{ "--progress": `${data ? position / (data.count - 1) * 100 : 0}%` } as CSSProperties}
        onChange={e => seek(+e.target.value)} disabled={!ready} />
      <div className="time-bounds"><span>0.000</span><span>{data?.end.toFixed(3) ?? "--"}</span></div>
    </div>
    <SelectControl options={[[.5, "0.5\u00d7"], [1, "1\u00d7"], [2, "2\u00d7"], [4, "4\u00d7"]]} value={speed} label="Playback speed" change={setSpeed} className="speed-select" />
  </div>;
}

function Viewport({ id, data, frame, field, select, mesh, setMesh, interfaceVisible, setInterface }: {
  id: CaseId; data: Dataset | null; frame?: Frame; field: number; select: (p: number) => void;
  mesh: boolean; setMesh: () => void; interfaceVisible: boolean; setInterface: () => void;
}) {
  const section = useRef<HTMLElement>(null);
  const [full, setFull] = useState(false);
  const [expanded, setExpanded] = useState(false);
  useEffect(() => {
    const onChange = () => setFull(!!document.fullscreenElement && document.fullscreenElement === section.current);
    const onEscape = (event: KeyboardEvent) => { if (event.key === "Escape") setExpanded(false); };
    document.addEventListener("fullscreenchange", onChange);
    document.addEventListener("keydown", onEscape);
    return () => { document.removeEventListener("fullscreenchange", onChange); document.removeEventListener("keydown", onEscape); };
  }, []);
  const options = fieldsFor(id);
  return <section ref={section} className={`panel viewport ${expanded ? "expanded" : ""}`} aria-labelledby="field-title">
    <header className="viewport-head">
      <div className="viewport-title"><span className="viewport-icon"><Waves size={17} strokeWidth={1.75} /></span><div>
        <h2 id="field-title">{options[field].title}</h2>
        <div>{id === "film" ? "FINITE-AREA SOLUTION" : id === "bubble" ? "2D FLOW VISUALIZATION" : "1D FLOW PROFILE"}</div>
      </div></div>
      <div className="viewport-tools">
        <SelectControl options={options.map((a, i) => [i, a.label])} value={field} label="Displayed field" change={select} className="field-select" />
        <span className="tool-separator" />
        <IconButton label="Toggle mesh grid" icon={Grid3X3} pressed={mesh} action={setMesh} />
        {id === "bubble" && <IconButton label="Toggle OPMIS interface" icon={Layers2} pressed={interfaceVisible} action={setInterface} />}
        <IconButton label={full || expanded ? "Exit fullscreen" : "Fullscreen plot"} icon={full || expanded ? Minimize2 : Maximize2} action={() => {
          if (expanded) setExpanded(false);
          else if (full) void document.exitFullscreen();
          else if (document.fullscreenEnabled && section.current?.requestFullscreen) void section.current.requestFullscreen().catch(() => setExpanded(true));
          else setExpanded(true);
        }} />
      </div>
    </header>
    {frame && data ? <FlowPlot id={id} frame={frame} data={data} field={field} mesh={mesh} interfaceVisible={interfaceVisible} /> : <div className="flow-empty"><span className="loader" /> Loading field data</div>}
  </section>;
}

function Metric({ label, value, highlight = false }: { label: ReactNode; value: ReactNode; highlight?: boolean }) {
  return <div className={`metric ${highlight ? "metric-highlight" : ""}`}><dt>{label}</dt><dd>{value}</dd></div>;
}

const num = (m: Float64Array | undefined, i: number, precision = 3) => m ? m[i].toFixed(precision) : "\u2013";
const count = (m: Float64Array | undefined, i: number) => m ? m[i].toLocaleString() : "\u2013";

function Region({ color, code, label, value }: { color: string; code?: ReactNode; label: string; value: string }) {
  return <div className="region"><i style={{ "--zone-color": color } as CSSProperties} />{code && <b>{code}</b>}<span>{label}</span><small>{value}</small></div>;
}

function Inspector({ id, data, frame, position, playing, report, navigate }: {
  id: CaseId; data: Dataset | null; frame?: Frame; position: number; playing: boolean; report: Report | null; navigate: () => void;
}) {
  const m = frame?.m, film = id === "film";
  const pass = caseOrder.filter(key => report?.checks.some(c => c.group === key) && report.checks.filter(c => c.group === key).every(c => c.pass_)).length;
  return <aside className="inspector" aria-label="Run diagnostics">
    <section className="panel diagnostic-panel">
      <header><div className="diagnostic-heading"><Activity size={16} strokeWidth={1.75} /><h2>Live Diagnostics</h2></div>
        <span className={`live-chip ${playing ? "is-running" : ""}`}><span />{playing ? "LIVE" : "READY"}</span>
      </header>
      <dl className="metrics">
        <Metric label="Simulation Time" value={<>{num(m, 0, 4)} <small>{film ? "s" : "t*"}</small></>} />
        <Metric label={<>Time Step <span>(&Delta;t)</span></>} value={m ? formatExp(m[1]) : "\u2013"} />
        <Metric label={film ? "Max. Film Speed" : "Max. Mach Number"} value={<>{num(m, 6)}{film && <small> m/s</small>}</>} />
        <Metric label="Mass Residual" value={m ? formatExp(m[3]) : "\u2013"} highlight />
      </dl>
      <section className="inspector-chart">
        <div className="chart-header"><span>{diagnosticName(id)}</span><strong>{num(m, 5, film ? 1 : 3)} <small>{film ? "\u03bcm" : "p/p\u2080"}</small></strong></div>
        {data ? <Trend data={data} position={position} /> : <div className="trend-blank" />}
      </section>
      {id === "bubble" && <section className="coupling">
        <div className="section-label"><span>Coupling Zones</span><Layers2 size={13} /></div>
        <Region color="#4ad8ee" code={<>P<sub>com</sub></>} label="Co-moving" value={count(m, 13)} />
        <Region color="#a98afc" code={<>P<sub>trn</sub></>} label="Transition" value={count(m, 14)} />
        <Region color="#6a9af6" code={<>P<sub>imt</sub></>} label="Interpolation" value={count(m, 15)} />
      </section>}
      {film && <section className="coupling">
        <div className="section-label"><span>Impact Regimes</span><Droplets size={13} /></div>
        {[["#4ad8ee", "Stick"], ["#a98afc", "Rebound"], ["#6a9af6", "Spread"], ["#f0b471", "Splash"]].map(([color, label], i) =>
          <Region key={label} color={color} label={label} value={count(m, 19 + i)} />)}
      </section>}
      {id !== "bubble" && !film && <section className="coupling method-detail">
        <div className="section-label"><span>Run Summary</span><Cpu size={13} /></div>
        <Region color="#4ad8ee" label="Integration steps" value={count(m, 2)} />
        <Region color="#6a9af6" label="Fallback attempts" value={count(m, 16)} />
      </section>}
    </section>
    <button type="button" className="panel verification-link" onClick={navigate}>
      <span className="verified-icon"><ShieldCheck size={19} strokeWidth={1.7} /></span>
      <span><b>Verification Suite</b><small>{report ? `${pass} of 4 benchmarks passing` : "View native test results"}</small></span><ChevronRight size={16} />
    </button>
  </aside>;
}

function Diagnostics({ id, data, frame, position }: { id: CaseId; data: Dataset | null; frame?: Frame; position: number }) {
  const film = id === "film", m = frame?.m;
  const charts = [
    { id: 5, name: diagnosticName(id), format: num(m, 5), unit: film ? "\u03bcm" : "p/p\u2080", color: "#42d9f4" },
    { id: 6, name: film ? "Peak surface speed" : "Maximum Mach number", format: num(m, 6), unit: film ? "m/s" : "Ma", color: "#a58df6" },
    { id: 3, name: "Relative mass residual", format: m ? formatExp(m[3]) : "\u2013", unit: "", color: "#5ee1b0" },
    { id: film ? 18 : id === "bubble" ? 9 : 4, name: film ? "Evaporated mass" : id === "bubble" ? "Vapor area" : "Relative energy residual",
      format: m ? formatExp(m[film ? 18 : id === "bubble" ? 9 : 4]) : "\u2013", unit: film ? "kg" : "", color: "#7aafff" },
  ];
  const exportCsv = () => {
    if (!data) return;
    const rows = [["time", "dt", "step", "relative_mass_residual", "relative_energy_residual", film ? "peak_film_um" : "peak_pressure", film ? "peak_speed_m_s" : "max_mach"].join(",")];
    for (const f of data.frames) if (f) rows.push(Array.from(f.m.slice(0,7)).join(","));
    const url = URL.createObjectURL(new Blob([rows.join("\n")], { type: "text/csv" }));
    const button = document.createElement("a"); button.href = url; button.download = `cluster-b-${id}-diagnostics.csv`; button.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  return <section className="subpage">
    <header className="subpage-head"><div><h2>Run Diagnostics</h2><p>Measured quantities across the current simulation.</p></div>
      <button type="button" className="secondary-button" disabled={!frame} onClick={exportCsv}><ArrowDownToLine size={15} /> Export CSV</button></header>
    <div className="diagnostic-charts">
      {charts.map(c => <section className="panel metric-chart" key={c.id}><div className="chart-header"><span>{c.name}</span><strong>{c.format} <small>{c.unit}</small></strong></div>
        {data ? <Trend data={data} position={position} metric={c.id} color={c.color} large signed={c.id === 3 || c.id === 4} /> : <div className="trend-blank" />}</section>)}
    </div>
    <div className="subpage-tip"><CircleHelp size={15} /> Mass and energy residuals include boundary fluxes. The wall-film mass budget includes vapor, airborne parcels, and escaped droplets.</div>
  </section>;
}

function testNumber(value: number) {
  if (!value) return "0";
  return Math.abs(value) < .01 ? formatExp(value, 3) : value.toPrecision(5);
}

function CheckList({ checks }: { checks: Measurement[] }) {
  return <div className="test-table" role="table" aria-label="Native test measurements">
    <div className="test-row test-head" role="row"><span role="columnheader">Check</span><span role="columnheader">Measured</span><span role="columnheader">Criterion</span><span role="columnheader">Result</span></div>
    {checks.map((c, i) => <div className="test-row" key={i} role="row"><span role="cell">{c.label}</span><code role="cell">{testNumber(c.value)}</code><code role="cell">{c.compare} {testNumber(c.limit)}</code><span role="cell" className={c.pass_ ? "test-pass" : "test-fail"}>{c.pass_ ? <Check size={13} /> : <X size={13} />}{c.pass_ ? "Pass" : "Fail"}</span></div>)}
  </div>;
}

const methodGroups = [
  ["flux", "Riemann solvers and MUSCL"], ["eos", "Equations of state and phase change"],
  ["ghost", "Zeroth-order ghost completion"], ["opmis", "First-order particle-mesh transfer"],
] as const;

function Verification({ id, report }: { id: CaseId; report: Report | null }) {
  const [group, setGroup] = useState<string>(id);
  useEffect(() => setGroup(id), [id]);
  const selected = report?.checks.filter(c => c.group === group) ?? [];
  const total = report?.checks.filter(c => c.pass_).length;
  return <section className="subpage verify-page">
    <header className="subpage-head"><div><h2>Native Verification</h2><p>{report ? `${total} of ${report.checks.length} checks passed using ${report.compiler}.` : "Loading the measured test report."}</p></div>
      <a className="secondary-button" href={`${BASE}data/verification.json`} download><ArrowDownToLine size={15} /> Download report</a></header>
    <div className="verify-cases" aria-label="Benchmark reports">{caseOrder.map(key => {
      const Icon = caseIcons[key], checks = report?.checks.filter(c => c.group === key);
      return <button type="button" className={group === key ? "chosen" : ""} key={key} onClick={() => setGroup(key)} aria-pressed={group === key}>
        <Icon size={19} /><span>{cases[key].title}</span>{checks?.length ? <small className={checks.every(c => c.pass_) ? "test-pass" : "test-fail"}>{checks.every(c => c.pass_) ? "Passed" : "Failed"}</small> : <small>Pending</small>}
      </button>;
    })}</div>
    <CheckList checks={selected} />
    <div className="method-tests"><h3>Component tests</h3>{methodGroups.map(([key, label]) => <details key={key}>
      <summary><ChevronRight size={15} /> {label}<small>{report?.checks.filter(c => c.group === key).length ?? 0} checks</small></summary>
      <CheckList checks={report?.checks.filter(c => c.group === key) ?? []} />
    </details>)}</div>
    <div className="subpage-tip"><CircleHelp size={15} /> Benchmark results are from the C++20 CPU build. CUDA device execution is not part of this report.</div>
  </section>;
}

function Documentation({ close }: { close: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const el = dialog.current;
    if (el && !el.open) el.showModal();
  }, []);
  return <dialog className="document-dialog" ref={dialog} onCancel={e => { e.preventDefault(); close(); }} onClick={e => { if (e.target === dialog.current) close(); }} aria-labelledby="doc-title">
    <div className="document-inner">
      <header><span className="viewport-icon"><BookOpen size={18} /></span><div><span className="section-label">Cluster B</span><h2 id="doc-title">Workbench Guide</h2></div><IconButton label="Close documentation" icon={X} action={close} /></header>
      <h3>Replay a benchmark or solve a variant</h3>
      <p>Presets open on a representative frame of a verified native run. Play, step, or drag the timeline. Changing the flux, EOS, or coupling settings and pressing Run Simulation starts a new C++20 WebAssembly solve in a worker.</p>
      <h3>Inspect the solution</h3>
      <p>Change the displayed field, toggle the mesh or the moving interpolation zone, and hover to probe values. Bubble density uses a numerical schlieren accent on strong gradients. Flow benchmarks are nondimensional; the wall film uses SI units.</p>
      <h3>Reproduce the native tests</h3>
      <p>The source archive includes the benchmark driver, tests, CUDA kernels, and build instructions. The browser runs on the CPU, not CUDA.</p>
      <pre>make verify<br />make cuda</pre>
      <div className="document-actions"><a className="primary-button" href={`${BASE}data/cluster-b-native.zip`} download><ArrowDownToLine size={15} /> Download source</a>
        <a className="secondary-button" href={`${BASE}data/verification.json`} download>Test report <ArrowRight size={14} /></a></div>
    </div>
  </dialog>;
}

export default function App() {
  const [id, setId] = useState<CaseId>("bubble");
  const [tab, setTab] = useState<Tab>("flow");
  const [settings, setSettings] = useState(() => defaults("bubble"));
  const [applied, setApplied] = useState(() => defaults("bubble"));
  const [data, setData] = useState<Dataset | null>(null);
  const [position, setPosition] = useState(cases.bubble.defaultFrame);
  const [playing, setPlaying] = useState(false);
  const [solving, setSolving] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [field, setField] = useState(0);
  const [mesh, setMesh] = useState(false);
  const [interfaceVisible, setInterface] = useState(true);
  const [documentation, setDocumentation] = useState(false);
  const [report, setReport] = useState<Report | null>(null);
  const [error, setError] = useState("");
  const [failedSolve, setFailedSolve] = useState(false);
  const worker = useRef<Worker | null>(null);
  const liveRequested = useRef(false);
  const currentCase = useRef<CaseId>("bubble");
  const active = useRef({ data, solving, position });
  const reduceMotion = useReducedMotion();
  const dirty = JSON.stringify(settings) !== JSON.stringify(applied);
  const selected = cases[id];
  const frame = data?.frames[position];

  useEffect(() => { active.current = { data, solving, position }; }, [data, solving, position]);
  useEffect(() => {
    let canceled = false;
    let full = false;
    if (id === "bubble") void reference(id, true).then(d => {
      if (!canceled && !full && !liveRequested.current) setData(d);
    }).catch(() => {});
    void reference(id).then(d => {
      full = true;
      if (!canceled && !liveRequested.current) setData(d);
    }).catch(e => { if (!canceled && !liveRequested.current) { setPlaying(false); setError(e.message); } });
    return () => { canceled = true; };
  }, [id]);
  useEffect(() => {
    void fetch(`${BASE}data/verification.json`).then(r => r.ok ? r.json() as Promise<Report> : null).then(setReport).catch(() => {});
    return () => worker.current?.terminate();
  }, []);
  useEffect(() => {
    if (!playing) return;
    const timer = window.setInterval(() => {
      const a = active.current;
      if (a.data && a.position >= a.data.count - 1) setPlaying(false);
      else if (a.data?.frames[a.position + 1]) setPosition(a.position + 1);
    }, 95 / speed);
    return () => clearInterval(timer);
  }, [playing, speed]);

  const choose = (next: CaseId) => {
    if (next === id) return;
    currentCase.current = next;
    worker.current?.terminate(); worker.current = null; liveRequested.current = false;
    setPlaying(false); setSolving(false); setError(""); setFailedSolve(false); setData(null);
    setId(next); setTab("flow"); setSettings(defaults(next)); setApplied(defaults(next));
    setField(cases[next].defaultField); setPosition(cases[next].defaultFrame);
  };
  const seek = (p: number) => {
    if (!data) return;
    const available = data.frames.reduce((max, f, i) => f ? i : max, 0);
    const index = Math.max(0, Math.min(p, available));
    if (data.frames[index]) { setPlaying(false); setPosition(index); }
  };
  const startSolve = useCallback(() => {
    worker.current?.terminate();
    liveRequested.current = true;
    setError(""); setFailedSolve(false); setApplied(settings); setData(null); setPosition(0); setSolving(true); setPlaying(true);
    const w = new Worker(new URL("./engine.worker.ts", import.meta.url), { type: "module" });
    worker.current = w;
    const fail = (reason: string) => {
      if (worker.current !== w) return;
      setError(reason); setSolving(false); setPlaying(false); setFailedSolve(true); w.terminate(); worker.current = null;
    };
    w.onmessage = ({ data: msg }: MessageEvent<Reply>) => {
      if (worker.current !== w) return;
      if (msg.type === "grid") setData({ ...msg.data, frames: Array(msg.data.count), origin: "wasm" });
      if (msg.type === "frame") setData(old => {
        if (!old) return old;
        const frames = old.frames.slice(), n = old.nx * old.ny;
        frames[msg.index] = { m: msg.metrics, fields: Array.from({ length: 4 }, (_, k) => msg.values.subarray(k * n, (k + 1) * n)) };
        return { ...old, frames };
      });
      if (msg.type === "done") { setSolving(false); w.terminate(); worker.current = null; }
      if (msg.type === "error") fail(msg.error);
    };
    w.onerror = () => fail("The WebAssembly engine could not start. Reference runs are still available.");
    const request: Request = { id: selected.id, settings, base: new URL(BASE, document.baseURI).href };
    w.postMessage(request);
  }, [selected.id, settings]);

  const run = () => {
    if (dirty || failedSolve) { startSolve(); return; }
    if (!data) return;
    if (playing) { setPlaying(false); return; }
    if (position === data.count - 1) setPosition(0);
    setPlaying(true);
  };
  const restore = () => {
    worker.current?.terminate(); worker.current = null; liveRequested.current = false;
    setSolving(false); setPlaying(false); setFailedSolve(false); setError(""); setSettings(defaults(id)); setApplied(defaults(id));
    void reference(id).then(d => { if (!liveRequested.current && currentCase.current === id) { setData(d); setPosition(0); } })
      .catch(e => { if (!liveRequested.current && currentCase.current === id) setError(e.message); });
  };
  const status = solving ? "Computing" : playing ? "Playing" : error ? "Check settings" : data ? "Solver ready" : "Initializing";

  return <div className="workbench">
    <header className="topbar">
      <Brand />
      <div className="header-trail"><span /><Scan size={15} /><b>Simulation Workbench</b></div>
      <div className="topbar-actions"><span className={`engine-status ${solving || playing ? "is-running" : ""} ${error ? "has-error" : ""}`}><i />{status}</span>
        <span className="top-separator" />
        <button type="button" className="docs-button" aria-label="Documentation" onClick={() => setDocumentation(true)}><BookOpen size={16} strokeWidth={1.7} /><span>Documentation</span></button>
      </div>
    </header>
    <div className="workbench-grid">
      <Sidebar current={id} choose={choose} settings={settings} change={patch => {
        setSettings(s => ({ ...s, ...patch })); setPlaying(false);
      }} dirty={dirty} />
      <main className="workspace">
        <section className="case-heading" aria-labelledby="case-title">
          <div className="case-head-copy"><div className="eyebrow"><span /> BENCHMARK {selected.number} <b>/</b> {selected.group}</div>
            <h1 id="case-title">{selected.title}</h1><p>{selected.description}</p></div>
          <div className="case-actions">
            <IconButton icon={RotateCcw} label="Reset to first frame" action={() => { if (failedSolve || !data?.frames[0]) restore(); else seek(0); }} disabled={!data && !error} className="reset-button" />
            <button type="button" className="primary-button run-button" onClick={run} disabled={!data && !dirty && !failedSolve}>
              {playing && !dirty ? <Pause size={16} fill="currentColor" /> : <Play size={15} fill="currentColor" />}
              {playing && !dirty ? "Pause Simulation" : "Run Simulation"}
            </button>
          </div>
        </section>
        <nav className="tabs" aria-label="Workbench view">
          {tabDefs.map(({ id: t, name, icon: Icon }) => <button className={tab === t ? "tab-active" : ""} type="button" key={t}
            aria-current={tab === t ? "page" : undefined} onClick={() => setTab(t)}><Icon size={16} strokeWidth={1.7} />{name}
            {tab === t && <motion.span className="tab-indicator" layoutId="tab-indicator" transition={{ duration: reduceMotion ? 0 : .2 }} />}</button>)}
        </nav>
        {error && <div className="error-strip" role="alert"><CircleHelp size={16} /><span>{error}</span><button type="button" onClick={restore}>Load reference</button><IconButton icon={X} label="Dismiss error" action={() => setError("")} /></div>}
        <AnimatePresence initial={false} mode="wait">
          <motion.div key={`${id}-${tab}`} className="tab-body" initial={{ opacity: 0, y: reduceMotion ? 0 : 5 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }} transition={{ duration: reduceMotion ? 0 : .15 }}>
            {tab === "flow" && <div className="flow-layout">
              <div className="flow-main">
                <Viewport id={id} data={data} frame={frame} field={field} select={setField} mesh={mesh} setMesh={() => setMesh(v => !v)} interfaceVisible={interfaceVisible} setInterface={() => setInterface(v => !v)} />
                <Timeline data={data} frame={frame} position={position} seek={seek} playing={playing} toggle={run} speed={speed} setSpeed={setSpeed} disabled={!data} />
              </div>
              <Inspector id={id} data={data} frame={frame} position={position} playing={playing} report={report} navigate={() => setTab("verification")} />
            </div>}
            {tab === "diagnostics" && <Diagnostics id={id} data={data} frame={frame} position={position} />}
            {tab === "verification" && <Verification id={id} report={report} />}
          </motion.div>
        </AnimatePresence>
        <footer className="workspace-footer">
          <span><span className="footer-dot" />{data?.origin === "wasm" ? "CPU / WebAssembly" : "Native reference run"}</span>
          <span className="footer-divider" /><span><Grid3X3 size={13} />{data ? (data.ny > 1 ? `${data.nx} \u00d7 ${data.ny}` : data.nx) : "--"} {id === "film" ? "surface cells" : "mesh"}</span>
          <span className="footer-divider" /><span><FlaskConical size={13} /> {id === "film" ? "EWF / DPM" : "MUSCL / SSP-RK2"}</span>
          <button className="footer-docs" type="button" onClick={() => setDocumentation(true)}>Numerical methods <ArrowRight size={13} /></button>
        </footer>
      </main>
    </div>
    {documentation && <Documentation close={() => setDocumentation(false)} />}
  </div>;
}