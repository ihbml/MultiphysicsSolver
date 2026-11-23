import { AnimatePresence, motion } from "framer-motion";
import {
  Activity,
  ArrowDownToLine,
  ArrowRight,
  Battery,
  Bell,
  BookOpen,
  Box,
  Check,
  CheckCheck,
  CheckCircle2,
  ChevronDown,
  ChevronRight,
  CircleHelp,
  Code2,
  Copy,
  Cpu,
  Download,
  ExternalLink,
  FileCode2,
  FileJson,
  FlaskConical,
  Gauge,
  GitBranch,
  Layers3,
  LayoutDashboard,
  LoaderCircle,
  Magnet,
  Maximize2,
  Menu,
  MoreHorizontal,
  Pause,
  Play,
  Plus,
  Radio,
  RotateCcw,
  Search,
  Settings2,
  ShieldCheck,
  SlidersHorizontal,
  Terminal,
  Thermometer,
  Waves,
  Wind,
  X,
  Zap,
} from "lucide-react";
import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import {
  AcousticField,
  BatteryField,
  MhdField,
  TpsField,
} from "./components/FieldViews";
import { Legend, LineChart, Sparkline } from "./components/Charts";
import {
  acousticSignal,
  defaultParameters,
  downloadText,
  hartmannProfile,
  reactionColors,
  reactionNames,
  runBrowserTests,
  simulateBattery,
  simulateTps,
  type BrowserTest,
  type ModuleId,
  type Parameters,
} from "./physics";

type Page = "telemetry" | "runs" | "mesh" | "verification" | "source";
type Modal = "newrun" | "field" | "settings" | "notifications" | null;
type SavedRun = {
  id: string;
  name: string;
  module: ModuleId;
  parameters: Parameters;
  time: number;
  created: string;
};
type NativeReport = {
  compiler: string;
  command: string;
  tests: {
    name: string;
    module: string;
    passed: boolean;
    value: string;
    detail: string;
  }[];
};
const modules = [
  {
    id: "battery" as const,
    number: "01",
    name: "Battery thermal",
    full: "Battery thermal runaway",
    subtitle: "Electrochemistry & runaway",
    icon: Battery,
    method: "2-RC ECM + 4-stage Arrhenius",
  },
  {
    id: "acoustics" as const,
    number: "02",
    name: "Aeroacoustics",
    full: "FWH aeroacoustics",
    subtitle: "FWH & spectral analysis",
    icon: Waves,
    method: "Farassat 1A + Hann-windowed FFT",
  },
  {
    id: "mhd" as const,
    number: "03",
    name: "Magnetohydrodynamics",
    full: "Inductionless MHD",
    subtitle: "Current-conservative flow",
    icon: Magnet,
    method: "Electric potential + Lorentz coupling",
  },
  {
    id: "tps" as const,
    number: "04",
    name: "Ablation & TPS",
    full: "Ablation & pyrolysis",
    subtitle: "Pyrolysis & surface recession",
    icon: Layers3,
    method: "Porous thermal response + BDF2 ALE",
  },
];
const degreeC = "\u00b0C";
const pageLabels: Record<Page, string> = {
  telemetry: "Workbench",
  runs: "Simulation runs",
  mesh: "Mesh explorer",
  verification: "Verification",
  source: "Source & API",
};
const ease = [0.22, 1, 0.36, 1] as const;

function Panel({
  title,
  eyebrow,
  action,
  children,
  className = "",
}: {
  title: string;
  eyebrow?: string;
  action?: ReactNode;
  children: ReactNode;
  className?: string;
}) {
  return (
    <section className={`panel ${className}`}>
      <div className="panel-heading">
        <div>
          <h2>{title}</h2>
          {eyebrow && <p>{eyebrow}</p>}
        </div>
        {action}
      </div>
      {children}
    </section>
  );
}
function StatusDot({ color = "green" }: { color?: string }) {
  return <i className={`status-dot ${color}`} />;
}
function Metric({
  label,
  value,
  unit,
  detail,
  color,
  values,
  icon,
}: {
  label: string;
  value: string;
  unit?: string;
  detail: string;
  color: string;
  values: number[];
  icon: ReactNode;
}) {
  return (
    <div className="metric">
      <div className="metric-label">
        {label}
        {icon}
      </div>
      <div className="metric-value">
        {value}
        <span>{unit}</span>
      </div>
      <div className="metric-bottom">
        <span>
          <i style={{ background: color }} />
          {detail}
        </span>
        <Sparkline color={color} values={values} />
      </div>
    </div>
  );
}
function ParameterSlider({
  label,
  value,
  min,
  max,
  step = 1,
  unit,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit: string;
  onChange: (n: number) => void;
}) {
  return (
    <label className="parameter-slider">
      <span>
        {label}
        <b>
          {value} <em>{unit}</em>
        </b>
      </span>
      <input
        aria-label={label}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        style={
          {
            "--range-progress": `${((value - min) / (max - min)) * 100}%`,
          } as React.CSSProperties
        }
        onChange={(e) => onChange(+e.target.value)}
      />
      <span className="range-extents">
        <small>
          {min} {unit}
        </small>
        <small>
          {max} {unit}
        </small>
      </span>
    </label>
  );
}

function NumberParameter({
  label,
  ariaLabel,
  value,
  min,
  max,
  step = 1,
  unit,
  onChange,
}: {
  label: string;
  ariaLabel?: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit: string;
  onChange: (value: number) => void;
}) {
  const [draft, setDraft] = useState(String(value));
  useEffect(() => setDraft(String(value)), [value]);
  function commit() {
    const parsed = draft.trim() ? Number(draft) : value;
    const next = Number.isFinite(parsed)
      ? Math.max(min, Math.min(max, parsed))
      : value;
    setDraft(String(next));
    onChange(next);
  }
  return (
    <label className="number-row">
      <span>{label}</span>
      <div>
        <input
          type="number"
          min={min}
          max={max}
          step={step}
          aria-label={ariaLabel || label}
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === "Enter") e.currentTarget.blur();
          }}
        />
        <span>{unit}</span>
      </div>
    </label>
  );
}

export default function App() {
  const [activeModule, setActiveModule] = useState<ModuleId>("battery");
  const [page, setPage] = useState<Page>("telemetry");
  const [params, setParams] = useState<Parameters>(defaultParameters);
  const [time, setTime] = useState(50);
  const [running, setRunning] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [selectedCell, setSelectedCell] = useState(27);
  const [mic, setMic] = useState(0);
  const [section, setSection] = useState(false);
  const [field, setField] = useState("Temperature");
  const [chartMode, setChartMode] = useState("Temperature");
  const [spectralMode, setSpectralMode] = useState("Narrowband");
  const [logOpen, setLogOpen] = useState(true);
  const [modal, setModal] = useState<Modal>(null);
  const [exportOpen, setExportOpen] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [toast, setToast] = useState("");
  const [runName, setRunName] = useState("Thermal propagation study");
  const [runs, setRuns] = useState<SavedRun[]>(() => {
    try {
      return JSON.parse(localStorage.getItem("vector-runs") || "[]");
    } catch {
      return [];
    }
  });
  const [runSearch, setRunSearch] = useState("");
  const [browserTests, setBrowserTests] = useState<BrowserTest[] | null>(null);
  const [testing, setTesting] = useState(false);
  const [nativeReport, setNativeReport] = useState<NativeReport | null>(null);
  const [verificationTab, setVerificationTab] = useState("Native C++20");
  const [sourceText, setSourceText] = useState(
    "Loading C++20 reference source...",
  );
  const [sourceTab, setSourceTab] = useState("Overview");
  const [meshTab, setMeshTab] = useState("Cell fields");
  const [meshImport, setMeshImport] = useState<{
    cells: number;
    faces: number;
    filename: string;
  } | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const batteryFrames = useMemo(() => simulateBattery(params), [params]);
  const tpsFrames = useMemo(() => simulateTps(params), [params]);
  const acoustic = useMemo(() => acousticSignal(params, mic), [params, mic]);
  const mhd = useMemo(
    () => hartmannProfile(params.hartmann),
    [params.hartmann],
  );
  const frame =
    batteryFrames[
      Math.min(
        batteryFrames.length - 1,
        Math.round(time / (batteryFrames[1]?.time || 0.5)),
      )
    ];
  const tps = tpsFrames[Math.min(tpsFrames.length - 1, Math.floor(time))];
  const module = modules.find((m) => m.id === activeModule)!;

  useEffect(() => {
    fetch("/verification.json")
      .then((r) => {
        if (!r.ok) throw new Error();
        return r.json();
      })
      .then(setNativeReport)
      .catch(() => setNativeReport(null));
    fetch("/solver/cluster_c.cpp")
      .then((r) =>
        r.ok ? r.text() : "Source unavailable. Please reload the workbench.",
      )
      .then(setSourceText)
      .catch(() =>
        setSourceText("Source unavailable. Please reload the workbench."),
      );
  }, []);
  useEffect(() => {
    try {
      localStorage.setItem("vector-runs", JSON.stringify(runs));
    } catch {
      setToast(
        "Local storage is unavailable. This run is kept in memory only.",
      );
    }
  }, [runs]);
  useEffect(() => {
    if (!running) return;
    const timer = setInterval(
      () => setTime((t) => Math.min(120, t + 0.5 * speed)),
      100,
    );
    return () => clearInterval(timer);
  }, [running, speed]);
  useEffect(() => {
    if (time >= 120) setRunning(false);
  }, [time]);
  useEffect(() => {
    if (!toast) return;
    const timer = setTimeout(() => setToast(""), 3600);
    return () => clearTimeout(timer);
  }, [toast]);
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        setModal(null);
        setExportOpen(false);
        setSidebarOpen(false);
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);
  useEffect(
    () => () => {
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    },
    [],
  );

  function navigate(next: Page) {
    setPage(next);
    setSidebarOpen(false);
    setExportOpen(false);
  }
  function selectModule(id: ModuleId) {
    setActiveModule(id);
    setChartMode("Temperature");
    setRunning(false);
    setTime(50);
    setPage("telemetry");
  }
  function updateParam(key: keyof Parameters, value: number) {
    setParams((p) => ({ ...p, [key]: value }));
    setRunning(false);
  }
  function reset() {
    setTime(0);
    setRunning(false);
    setToast("Scenario reset. Parameters have been preserved.");
  }
  function play() {
    if (time >= 120) setTime(0);
    setRunning((r) => !r);
  }
  function saveRun() {
    const name = runName.trim();
    if (!name) return;
    setRuns((r) => [
      {
        id: `C-${String(Date.now()).slice(-6)}`,
        name,
        module: activeModule,
        parameters: { ...params },
        time,
        created: new Date().toISOString(),
      },
      ...r,
    ]);
    setModal(null);
    setToast("Run snapshot saved to this browser.");
  }
  function exportData(format: "csv" | "json") {
    let rows: Record<string, number>[];
    if (activeModule === "battery")
      rows = batteryFrames.map((f) => ({
        time_s: f.time,
        peak_K: f.peak,
        mean_K: f.mean,
        minimum_K: f.min,
        SOC: f.soc,
        terminal_V: f.voltage,
        electrothermal_heat_W: f.heat,
        irreversible_heat_W: f.heatIrreversible,
        entropic_heat_W: f.heatReversible,
        OCV_V: f.ocv,
      }));
    else if (activeModule === "acoustics")
      rows = acoustic.psd.map((f) => ({
        frequency_Hz: f.frequency,
        PSD_Pa2_per_Hz: f.power,
      }));
    else if (activeModule === "mhd")
      rows = mhd.points.map((p) => ({
        y_over_L: p.y,
        numerical_u_over_umax: p.numerical,
        exact_u_over_umax: p.exact,
      }));
    else
      rows = tpsFrames.map((f) => ({
        time_s: f.time,
        surface_K: f.wall,
        backface_K: f.temperatures[39],
        recession_m: f.recession,
        pyrolysis_gas_kg_m2_s: f.gas,
        char_mass_flux_kg_m2_s: f.char,
      }));
    if (format === "json")
      downloadText(
        `vector-${activeModule}.json`,
        JSON.stringify(
          {
            engine: "browser-reference",
            module: activeModule,
            parameters: params,
            data: rows,
          },
          null,
          2,
        ),
        "application/json",
      );
    else
      downloadText(
        `vector-${activeModule}.csv`,
        [
          Object.keys(rows[0]).join(","),
          ...rows.map((row) => Object.values(row).join(",")),
        ].join("\n"),
        "text/csv",
      );
    setExportOpen(false);
    setToast(`${format.toUpperCase()} telemetry exported.`);
  }
  function executeBrowserTests() {
    setTesting(true);
    setVerificationTab("Browser kernels");
    timeoutRef.current = setTimeout(() => {
      setBrowserTests(runBrowserTests());
      setTesting(false);
      setToast("Browser verification suite finished.");
    }, 700);
  }
  function importMesh(file: File | undefined) {
    if (!file) return;
    file
      .text()
      .then((text) => {
        const mesh = JSON.parse(text);
        if (
          !Array.isArray(mesh.volume) ||
          !Array.isArray(mesh.owner) ||
          !Array.isArray(mesh.neighbor) ||
          mesh.owner.length !== mesh.neighbor.length ||
          !mesh.volume.every((v: unknown) => typeof v === "number" && v > 0) ||
          !mesh.owner.every(
            (o: number) =>
              Number.isInteger(o) && o >= 0 && o < mesh.volume.length,
          ) ||
          !mesh.neighbor.every(
            (o: number) =>
              Number.isInteger(o) && o >= -1 && o < mesh.volume.length,
          )
        )
          throw new Error(
            "Expected positive volume[], and valid owner[] / neighbor[] arrays.",
          );
        setMeshImport({
          cells: mesh.volume.length,
          faces: mesh.owner.length,
          filename: file.name,
        });
        setToast(
          "Mesh connectivity validated. Native geometry import is documented in Source & API.",
        );
      })
      .catch((error) =>
        setToast(`Import failed: ${error.message || "Invalid JSON mesh."}`),
      );
  }

  const reactionProgress = frame.reaction.map((c, i) =>
    Math.max(
      0,
      Math.min(1, i === 2 ? (c - 0.04) / 0.96 : 1 - c / [0.15, 0.75, 1, 1][i]),
    ),
  );
  const fieldView =
    activeModule === "battery" ? (
      <BatteryField
        frame={frame}
        selected={selectedCell}
        onSelect={setSelectedCell}
        section={section}
        field={field}
      />
    ) : activeModule === "acoustics" ? (
      <AcousticField running={running} mic={mic} onMic={setMic} />
    ) : activeModule === "mhd" ? (
      <MhdField params={params} running={running} />
    ) : (
      <TpsField frame={tps} params={params} />
    );
  const xSeries = batteryFrames.map((f) => f.time);
  const metrics =
    activeModule === "battery"
      ? [
          {
            label: "Peak temperature",
            value: (frame.peak - 273.15).toFixed(1),
            unit: degreeC,
            detail: `Cell ${String(frame.temperatures.indexOf(frame.peak) + 1).padStart(2, "0")} / hottest zone`,
            color: "#e6ac85",
            values: batteryFrames
              .filter((f) => f.time <= time)
              .map((f) => f.peak),
            icon: <Thermometer size={15} />,
          },
          {
            label: "State of charge",
            value: (frame.soc * 100).toFixed(1),
            unit: "%",
            detail: "Discharge-positive convention",
            color: "#b59bdf",
            values: batteryFrames
              .filter((f) => f.time <= time)
              .map((f) => f.soc),
            icon: <Battery size={15} />,
          },
          {
            label: "Terminal voltage",
            value: frame.voltage.toFixed(3),
            unit: "V",
            detail: "2-RC Thevenin ECM",
            color: "#85c3b0",
            values: batteryFrames
              .filter((f) => f.time <= time)
              .map((f) => f.voltage),
            icon: <Zap size={15} />,
          },
          {
            label: "Thermal spread",
            value: (frame.peak - frame.min).toFixed(1),
            unit: "K",
            detail: "Maximum cell-to-cell difference",
            color: "#8faddb",
            values: batteryFrames
              .filter((f) => f.time <= time)
              .map((f) => f.peak - f.min),
            icon: <Activity size={15} />,
          },
        ]
      : activeModule === "acoustics"
        ? [
            {
              label: "Overall sound level",
              value: acoustic.oaspl.toFixed(1),
              unit: "dB",
              detail: "Unweighted / ref. 20 microPa",
              color: "#b59bdf",
              values: acoustic.signal
                .filter((_, i) => i % 80 === 0)
                .map(Math.abs),
              icon: <Radio size={15} />,
            },
            {
              label: "Blade-passing frequency",
              value: acoustic.bpf.toFixed(0),
              unit: "Hz",
              detail: "Four blades / shaft harmonic",
              color: "#85c3b0",
              values: [1, 1, 1, 1],
              icon: <Waves size={15} />,
            },
            {
              label: "Acoustic pressure",
              value: acoustic.rms.toFixed(3),
              unit: "Pa RMS",
              detail: `Receiver MIC 0${mic + 1}`,
              color: "#e6ac85",
              values: acoustic.signal.slice(0, 100),
              icon: <Gauge size={15} />,
            },
            {
              label: "Propagation delay",
              value: (acoustic.delay * 1000).toFixed(2),
              unit: "ms",
              detail: "Retarded-time / c = 343 m/s",
              color: "#8faddb",
              values: [1, 1, 1, 1],
              icon: <GitBranch size={15} />,
            },
          ]
        : activeModule === "mhd"
          ? [
              {
                label: "Hartmann number",
                value: `${params.hartmann}`,
                unit: "Ha",
                detail: "Magnetic / viscous force ratio",
                color: "#b59bdf",
                values: mhd.points
                  .filter((_, i) => i % 10 === 0)
                  .map((p) => p.numerical),
                icon: <Magnet size={15} />,
              },
              {
                label: "Profile RMS error",
                value: (mhd.error * 100).toFixed(3),
                unit: "%",
                detail: "Against analytical solution",
                color: "#85c3b0",
                values: mhd.points
                  .filter((_, i) => i % 10 === 0)
                  .map((p) => Math.abs(p.numerical - p.exact)),
                icon: <ShieldCheck size={15} />,
              },
              {
                label: "Linear residual",
                value: mhd.residual.toExponential(1),
                unit: "",
                detail: "Discrete channel momentum",
                color: "#8faddb",
                values: [2, 1, 0.4, 0.1, 0.01, 0],
                icon: <Activity size={15} />,
              },
              {
                label: "Hartmann layer",
                value: (mhd.layer * 100).toFixed(2),
                unit: "% L",
                detail: "Characteristic thickness / L",
                color: "#e6ac85",
                values: [1, 1, 1, 1],
                icon: <Layers3 size={15} />,
              },
            ]
          : [
              {
                label: "Surface temperature",
                value: tps.wall.toFixed(0),
                unit: "K",
                detail: "Nonlinear wall energy balance",
                color: "#e6ac85",
                values: tpsFrames
                  .filter((f) => f.time <= time)
                  .map((f) => f.wall),
                icon: <Thermometer size={15} />,
              },
              {
                label: "Surface recession",
                value: (tps.recession * 1000).toFixed(3),
                unit: "mm",
                detail: "Integrated char mass loss",
                color: "#b59bdf",
                values: tpsFrames
                  .filter((f) => f.time <= time)
                  .map((f) => f.recession),
                icon: <Layers3 size={15} />,
              },
              {
                label: "Pyrolysis gas flux",
                value: (tps.gas * 1000).toFixed(2),
                unit: "g/m2s",
                detail: "Virgin-to-char conversion",
                color: "#85c3b0",
                values: tpsFrames
                  .filter((f) => f.time <= time)
                  .map((f) => f.gas),
                icon: <Wind size={15} />,
              },
              {
                label: "Backface temperature",
                value: tps.temperatures[39].toFixed(1),
                unit: "K",
                detail: "Adiabatic rear boundary",
                color: "#8faddb",
                values: tpsFrames
                  .filter((f) => f.time <= time)
                  .map((f) => f.temperatures[39]),
                icon: <ShieldCheck size={15} />,
              },
            ];

  const telemetry = (
    <>
      <motion.div
        className="metrics-grid"
        initial={{ opacity: 0, y: 10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.45, delay: 0.1, ease }}
      >
        {metrics.map((metric) => (
          <Metric key={metric.label} {...metric} />
        ))}
      </motion.div>
      <div className="workbench-grid">
        <div className="visual-column">
          <Panel
            className="field-panel"
            title={
              activeModule === "battery"
                ? "Battery pack thermal field"
                : activeModule === "acoustics"
                  ? "Acoustic source & receivers"
                  : activeModule === "mhd"
                    ? "Hartmann channel"
                    : "Thermal protection cross-section"
            }
            eyebrow={
              activeModule === "battery"
                ? "Liquid-cooled pack / 8 \u00d7 6 homogenized cell zones"
                : activeModule === "acoustics"
                  ? "Select a microphone to inspect the received signal"
                  : activeModule === "mhd"
                    ? "Collocated FVM / insulating walls / imposed transverse field"
                    : "Charring porous solid / one-dimensional reference case"
            }
            action={
              <div className="panel-actions">
                {activeModule === "battery" && (
                  <div className="mini-segment">
                    <button
                      className={!section ? "active" : ""}
                      onClick={() => setSection(false)}
                    >
                      3D
                    </button>
                    <button
                      className={section ? "active" : ""}
                      onClick={() => setSection(true)}
                    >
                      Section
                    </button>
                  </div>
                )}
                <button
                  className="icon-button"
                  aria-label="Expand field visualization"
                  onClick={() => setModal("field")}
                >
                  <Maximize2 size={15} />
                </button>
              </div>
            }
          >
            <div className="field-toolbar">
              <label className="field-selector">
                {activeModule === "battery" ? (
                  <>
                    <i className="purple-dot" />
                    <select
                      value={field}
                      aria-label="Visualized field"
                      onChange={(e) => setField(e.target.value)}
                    >
                      <option>Temperature</option>
                      <option>State of charge</option>
                      <option>Reaction progress</option>
                    </select>
                  </>
                ) : (
                  <>
                    <i className="purple-dot" />
                    <span>
                      {activeModule === "acoustics"
                        ? "Acoustic pressure"
                        : activeModule === "mhd"
                          ? "Normalized velocity"
                          : "Solid temperature"}
                    </span>
                  </>
                )}
              </label>
              <span className="viewport-tag">
                <StatusDot color={running ? "green" : "purple"} />
                {running ? "PLAYING" : "NUMERICAL PREVIEW"}
              </span>
            </div>
            {fieldView}
            <div className="field-bottom">
              <div className="field-selection">
                <Box size={13} />
                <span>
                  {activeModule === "battery"
                    ? `CELL ${String(selectedCell + 1).padStart(2, "0")}`
                    : activeModule === "acoustics"
                      ? `MIC 0${mic + 1}`
                      : activeModule === "mhd"
                        ? "256 INTERVALS"
                        : "40 CELL LAYERS"}
                </span>
                <b>
                  {activeModule === "battery"
                    ? field === "State of charge"
                      ? `${(frame.soc * 100).toFixed(1)} %`
                      : field === "Reaction progress"
                        ? `${(frame.conversion[selectedCell] * 100).toFixed(1)} %`
                        : `${(frame.temperatures[selectedCell] - 273.15).toFixed(1)} ${degreeC}`
                    : activeModule === "acoustics"
                      ? `${acoustic.oaspl.toFixed(1)} dB`
                      : activeModule === "mhd"
                        ? `Ha ${params.hartmann}`
                        : `${tps.wall.toFixed(0)} K`}
                </b>
              </div>
              <div className="color-scale">
                <span>
                  {activeModule === "battery"
                    ? field !== "Temperature"
                      ? "0%"
                      : "25"
                    : activeModule === "mhd"
                      ? "0.0"
                      : activeModule === "tps"
                        ? "300"
                        : "-p"}
                </span>
                <i
                  className={
                    field === "State of charge" && activeModule === "battery"
                      ? "soc-scale"
                      : ""
                  }
                />
                <span>
                  {activeModule === "battery"
                    ? field !== "Temperature"
                      ? "100%"
                      : `${(Math.max(650, Math.ceil(frame.peak / 100) * 100) - 273.15).toFixed(0)} ${degreeC}`
                    : activeModule === "mhd"
                      ? "1.0 u/max"
                      : activeModule === "tps"
                        ? "1800 K"
                        : "+p"}
                </span>
              </div>
            </div>
          </Panel>
          <Panel
            className="evolution-panel"
            title={
              activeModule === "battery"
                ? chartMode === "Temperature"
                  ? "Temperature evolution"
                  : "Electrical response"
                : activeModule === "acoustics"
                  ? "Acoustic spectrum"
                  : activeModule === "mhd"
                    ? "Velocity profile validation"
                    : "Through-thickness response"
            }
            action={
              activeModule === "battery" ? (
                <div className="mini-segment">
                  {["Temperature", "Voltage"].map((tab) => (
                    <button
                      key={tab}
                      className={chartMode === tab ? "active" : ""}
                      onClick={() => setChartMode(tab)}
                    >
                      {tab}
                    </button>
                  ))}
                </div>
              ) : activeModule === "acoustics" ? (
                <div className="mini-segment">
                  {["Narrowband", "1/3 octave"].map((tab) => (
                    <button
                      key={tab}
                      className={spectralMode === tab ? "active" : ""}
                      onClick={() => setSpectralMode(tab)}
                    >
                      {tab}
                    </button>
                  ))}
                </div>
              ) : activeModule === "tps" ? (
                <div className="mini-segment">
                  {["Temperature", "Density"].map((tab) => (
                    <button
                      key={tab}
                      className={chartMode === tab ? "active" : ""}
                      onClick={() => setChartMode(tab)}
                    >
                      {tab}
                    </button>
                  ))}
                </div>
              ) : (
                <span className="subtle-badge">
                  {activeModule === "mhd"
                    ? "ANALYTICAL BENCHMARK"
                    : `t = ${time.toFixed(1)} s`}
                </span>
              )
            }
          >
            {activeModule === "battery" ? (
              <>
                <Legend
                  items={
                    chartMode === "Temperature"
                      ? [
                          { name: "Peak temperature", color: "#b59bdf" },
                          { name: "Pack average", color: "#7fc3b0" },
                          { name: "Coolant", color: "#748fae", dashed: true },
                        ]
                      : [
                          { name: "Terminal voltage", color: "#b59bdf" },
                          { name: "Open-circuit voltage", color: "#7fc3b0" },
                        ]
                  }
                />
                <LineChart
                  series={
                    chartMode === "Temperature"
                      ? [
                          {
                            name: "Peak",
                            color: "#b59bdf",
                            values: batteryFrames.map((f) => f.peak - 273.15),
                          },
                          {
                            name: "Average",
                            color: "#7fc3b0",
                            values: batteryFrames.map((f) => f.mean - 273.15),
                          },
                          {
                            name: "Coolant",
                            color: "#748fae",
                            values: xSeries.map(() => params.coolant - 273.15),
                            dashed: true,
                          },
                        ]
                      : [
                          {
                            name: "Terminal voltage",
                            color: "#b59bdf",
                            values: batteryFrames.map((f) => f.voltage),
                          },
                          {
                            name: "Open-circuit voltage",
                            color: "#7fc3b0",
                            values: batteryFrames.map((f) => f.ocv),
                            dashed: true,
                          },
                        ]
                  }
                  unit={chartMode === "Temperature" ? degreeC : "V"}
                  yMin={chartMode === "Temperature" ? 0 : undefined}
                />
              </>
            ) : activeModule === "acoustics" ? (
              <>
                <Legend
                  items={[
                    {
                      name:
                        spectralMode === "Narrowband"
                          ? "Hann-windowed PSD"
                          : "Integrated band level",
                      color: "#b59bdf",
                    },
                    {
                      name: `${acoustic.bpf} Hz blade passing`,
                      color: "#7fc3b0",
                    },
                  ]}
                />
                <LineChart
                  series={[
                    {
                      name:
                        spectralMode === "Narrowband" ? "PSD" : "Band level",
                      color: "#b59bdf",
                      values:
                        spectralMode === "Narrowband"
                          ? acoustic.psd
                              .slice(0, 257)
                              .map(
                                (p) =>
                                  10 *
                                  Math.log10(Math.max(p.power, 1e-16) / 4e-10),
                              )
                          : acoustic.bands.map((b) => b.db),
                    },
                  ]}
                  xMin={
                    spectralMode === "Narrowband"
                      ? 0
                      : acoustic.bands[0].frequency
                  }
                  xMax={
                    spectralMode === "Narrowband"
                      ? 1024
                      : acoustic.bands[acoustic.bands.length - 1].frequency
                  }
                  logX={spectralMode === "1/3 octave"}
                  xValues={
                    spectralMode === "Narrowband"
                      ? acoustic.psd.slice(0, 257).map((p) => p.frequency)
                      : acoustic.bands.map((b) => b.frequency)
                  }
                  xUnit="Hz"
                  unit={spectralMode === "Narrowband" ? "dB/Hz" : "dB"}
                  labels={
                    spectralMode === "1/3 octave"
                      ? Array.from({ length: 7 }, (_, i) => {
                          const f =
                            acoustic.bands[0].frequency *
                            (acoustic.bands[acoustic.bands.length - 1]
                              .frequency /
                              acoustic.bands[0].frequency) **
                              (i / 6);
                          return `${f >= 1000 ? `${(f / 1000).toFixed(1)}k` : Math.round(f)}${i === 6 ? " Hz" : ""}`;
                        })
                      : undefined
                  }
                />
              </>
            ) : activeModule === "mhd" ? (
              <>
                <Legend
                  items={[
                    { name: "FVM numerical", color: "#b59bdf" },
                    {
                      name: "Hartmann analytical",
                      color: "#7fc3b0",
                      dashed: true,
                    },
                  ]}
                />
                <LineChart
                  series={[
                    {
                      name: "Numerical",
                      color: "#b59bdf",
                      values: mhd.points.map((p) => p.numerical),
                    },
                    {
                      name: "Analytical",
                      color: "#7fc3b0",
                      values: mhd.points.map((p) => p.exact),
                      dashed: true,
                    },
                  ]}
                  xMin={-1}
                  xMax={1}
                  xValues={mhd.points.map((p) => p.y)}
                  xUnit="y/L"
                  yMin={0}
                  yMax={1.2}
                  unit="u/u max"
                  labels={[
                    "-1.0",
                    "-0.67",
                    "-0.33",
                    "0",
                    "0.33",
                    "0.67",
                    "1.0 y/L",
                  ]}
                />
              </>
            ) : (
              <>
                <Legend
                  items={[
                    {
                      name:
                        chartMode === "Density"
                          ? "Solid density"
                          : "Temperature",
                      color: chartMode === "Density" ? "#7fc3b0" : "#e6ac85",
                    },
                  ]}
                />
                <LineChart
                  series={[
                    {
                      name: chartMode === "Density" ? "Density" : "Temperature",
                      color: chartMode === "Density" ? "#7fc3b0" : "#e6ac85",
                      values:
                        chartMode === "Density"
                          ? tps.density
                          : tps.temperatures,
                    },
                  ]}
                  xMax={params.thickness}
                  xValues={tps.temperatures.map(
                    (_, i) =>
                      ((i + 0.5) / tps.temperatures.length) * params.thickness,
                  )}
                  xUnit="mm"
                  unit={chartMode === "Density" ? "kg/m3" : "K"}
                  yMin={0}
                  yMax={chartMode === "Density" ? 320 : 2000}
                />
              </>
            )}
            <div className="chart-note">
              <span>
                <i className="purple-dot" />
                {activeModule === "battery"
                  ? "Full computed scenario trajectory"
                  : activeModule === "acoustics"
                    ? "2048 samples / 8192 Hz / unweighted"
                    : activeModule === "mhd"
                      ? `Relative RMS error: ${(mhd.error * 100).toFixed(4)}%`
                      : "Fixed-grid browser preview; ALE is in the native reference"}
              </span>
              <span>FLOAT64</span>
            </div>
          </Panel>
        </div>
        <div className="controls-column">
          <Panel
            title="Run configuration"
            action={<SlidersHorizontal size={15} className="muted" />}
            className="configuration-panel"
          >
            <label className="form-label">
              REFERENCE SCENARIO
              <select
                className="styled-select"
                aria-label="Reference scenario"
                value={activeModule}
                onChange={(e) => selectModule(e.target.value as ModuleId)}
              >
                <option value="battery">NMC pack / localized abuse</option>
                <option value="acoustics">Rotor / harmonic patch</option>
                <option value="mhd">Hartmann / plane channel</option>
                <option value="tps">Charring TPS / constant flux</option>
              </select>
            </label>
            <div className="parameter-controls">
              {activeModule === "battery" && (
                <>
                  <ParameterSlider
                    label="Discharge current"
                    value={params.current}
                    min={0}
                    max={60}
                    unit="A"
                    onChange={(n) => updateParam("current", n)}
                  />
                  <NumberParameter
                    label="Coolant temperature"
                    value={params.coolant}
                    min={273}
                    max={350}
                    unit="K"
                    onChange={(n) => updateParam("coolant", n)}
                  />
                </>
              )}
              {activeModule === "acoustics" && (
                <>
                  <ParameterSlider
                    label="Rotor speed"
                    value={params.rpm}
                    min={600}
                    max={6000}
                    step={100}
                    unit="rpm"
                    onChange={(n) => updateParam("rpm", n)}
                  />
                  <ParameterSlider
                    label="Receiver distance"
                    value={params.distance}
                    min={1}
                    max={20}
                    step={0.5}
                    unit="m"
                    onChange={(n) => updateParam("distance", n)}
                  />
                </>
              )}
              {activeModule === "mhd" && (
                <>
                  <ParameterSlider
                    label="Hartmann number"
                    value={params.hartmann}
                    min={1}
                    max={100}
                    unit="Ha"
                    onChange={(n) => updateParam("hartmann", n)}
                  />
                  <NumberParameter
                    label="Conductivity"
                    ariaLabel="Electrical conductivity"
                    value={params.conductivity}
                    min={0.1}
                    max={10}
                    step={0.1}
                    unit="MS/m"
                    onChange={(n) => updateParam("conductivity", n)}
                  />
                </>
              )}
              {activeModule === "tps" && (
                <>
                  <ParameterSlider
                    label="Incident heat flux"
                    value={params.heatflux}
                    min={50}
                    max={800}
                    step={10}
                    unit="kW/m2"
                    onChange={(n) => updateParam("heatflux", n)}
                  />
                  <ParameterSlider
                    label="Initial thickness"
                    value={params.thickness}
                    min={10}
                    max={60}
                    unit="mm"
                    onChange={(n) => updateParam("thickness", n)}
                  />
                </>
              )}
            </div>
            <div className="numerics-rows">
              <label className="number-row">
                <span>
                  {activeModule === "battery"
                    ? "Time step"
                    : activeModule === "mhd"
                      ? "Magnetic field B0"
                      : "Integrator"}
                </span>
                {activeModule === "battery" ? (
                  <select
                    aria-label="Integration time step"
                    value={params.timestep}
                    onChange={(e) => updateParam("timestep", +e.target.value)}
                  >
                    <option value={0.1}>0.10 s</option>
                    <option value={0.25}>0.25 s</option>
                    <option value={0.5}>0.50 s</option>
                    <option value={1}>1.00 s</option>
                  </select>
                ) : (
                  <b className="mono">
                    {activeModule === "mhd"
                      ? `${((params.hartmann * Math.sqrt(0.002 / (params.conductivity * 1e6))) / 0.01).toFixed(3)} T`
                      : activeModule === "tps"
                        ? "IMPLICIT"
                        : "SOURCE TIME"}
                  </b>
                )}
              </label>
              <div className="number-row">
                <span>Discretization</span>
                <span className="value-label">
                  {activeModule === "acoustics"
                    ? "Surface integral"
                    : "Finite volume"}
                </span>
              </div>
            </div>
            <div className="run-buttons">
              <button className="primary-button run-button" onClick={play}>
                {running ? (
                  <Pause size={16} fill="currentColor" />
                ) : (
                  <Play size={16} fill="currentColor" />
                )}
                {running
                  ? "Pause simulation"
                  : time > 0 && time < 120
                    ? "Resume simulation"
                    : "Run simulation"}
              </button>
              <button
                className="reset-button"
                aria-label="Reset simulation"
                onClick={reset}
              >
                <RotateCcw size={16} />
              </button>
            </div>
            <div className="run-engine">
              <Cpu size={12} />
              <span>Browser reference kernels</span>
              <span>CPU</span>
            </div>
          </Panel>
          <Panel
            title={
              activeModule === "battery"
                ? "Reaction kinetics"
                : activeModule === "acoustics"
                  ? "Signal decomposition"
                  : activeModule === "mhd"
                    ? "Conservation diagnostics"
                    : "Surface energy balance"
            }
            eyebrow={
              activeModule === "battery"
                ? "Trigger cell / reaction conversion"
                : activeModule === "acoustics"
                  ? "Surface sources / stationary test case"
                  : activeModule === "mhd"
                    ? "Native conservative projection"
                    : "Incident flux partition at the wall"
            }
            action={
              <button
                className="icon-button"
                aria-label="Read numerical methods"
                onClick={() => {
                  setSourceTab("Overview");
                  navigate("source");
                }}
              >
                <CircleHelp size={14} />
              </button>
            }
            className="reaction-panel"
          >
            {activeModule === "battery" ? (
              <div className="reaction-list">
                {reactionNames.map((name, i) => (
                  <div className="reaction-row" key={name}>
                    <div>
                      <span>
                        <i style={{ background: reactionColors[i] }} />
                        {name}
                      </span>
                      <b>
                        {(reactionProgress[i] * 100).toFixed(1)}
                        <small>%</small>
                      </b>
                    </div>
                    <div className="progress-track">
                      <motion.i
                        animate={{
                          width: `${Math.max(0.4, reactionProgress[i] * 100)}%`,
                        }}
                        transition={{ duration: 0.5 }}
                        style={{ background: reactionColors[i] }}
                      />
                    </div>
                  </div>
                ))}
              </div>
            ) : activeModule === "acoustics" ? (
              <div className="diagnostic-list">
                <div>
                  <span>Thickness source</span>
                  <b>Q = rho0 vn</b>
                </div>
                <div>
                  <span>Loading source</span>
                  <b>L = p n</b>
                </div>
                <div>
                  <span>Doppler factor</span>
                  <b>1.000</b>
                </div>
                <div>
                  <span>Quadrupole volume</span>
                  <b className="muted">Not included</b>
                </div>
                <p>
                  Native kernel supports moving, subsonic solid and permeable
                  patches.
                </p>
              </div>
            ) : activeModule === "mhd" ? (
              <div className="diagnostic-list">
                <div>
                  <span>Current projection</span>
                  <b className="positive">Face-conservative</b>
                </div>
                <div>
                  <span>Electric boundary</span>
                  <b>Insulating</b>
                </div>
                <div>
                  <span>Magnetic Reynolds</span>
                  <b>Re_m &lt;&lt; 1</b>
                </div>
                <div>
                  <span>Verification range</span>
                  <b>Ha 1 to 100</b>
                </div>
                <p>
                  Flux-conservation tests and non-orthogonal geometry are in the
                  native suite.
                </p>
              </div>
            ) : (
              <div className="energy-list">
                {[
                  {
                    name: "Reradiation",
                    value: tps.radiation,
                    color: "#e6ac85",
                  },
                  {
                    name: "Pyrolysis enthalpy",
                    value: tps.gas * 240000,
                    color: "#b59bdf",
                  },
                  {
                    name: "Sublimation",
                    value: tps.char * 2e7,
                    color: "#7fc3b0",
                  },
                  {
                    name: "Net conduction",
                    value:
                      params.heatflux * 1000 -
                      tps.radiation -
                      tps.gas * 240000 -
                      tps.char * 2e7,
                    color: "#8faddb",
                  },
                ].map((item) => (
                  <div className="energy-item" key={item.name}>
                    <span>
                      <i style={{ background: item.color }} />
                      {item.name}
                    </span>
                    <b>
                      {(item.value / 1000).toFixed(1)} <small>kW/m2</small>
                    </b>
                  </div>
                ))}
              </div>
            )}
            <div className="method-link">
              <span>
                <GitBranch size={12} />
                {activeModule === "battery"
                  ? "Patankar-diagonalized sources"
                  : activeModule === "acoustics"
                    ? "Farassat Formulation 1A"
                    : activeModule === "mhd"
                      ? "Ni-style current reconstruction"
                      : "Arrhenius + implicit conduction"}
              </span>
              <button
                aria-label="View method source"
                onClick={() => navigate("source")}
              >
                <ArrowUpRightIcon />
              </button>
            </div>
          </Panel>
        </div>
      </div>
      <div className="timeline-panel">
        <button
          className="timeline-play"
          aria-label={running ? "Pause playback" : "Play playback"}
          onClick={play}
        >
          {running ? (
            <Pause size={13} fill="currentColor" />
          ) : (
            <Play size={13} fill="currentColor" />
          )}
        </button>
        <span className="timeline-time">
          {time.toFixed(1)} <small>/ 120.0 s</small>
        </span>
        <input
          type="range"
          min="0"
          max="120"
          step=".5"
          aria-label="Simulation time"
          value={time}
          onChange={(e) => {
            setTime(+e.target.value);
            setRunning(false);
          }}
          style={
            {
              "--range-progress": `${(time / 120) * 100}%`,
            } as React.CSSProperties
          }
        />
        <select
          value={speed}
          aria-label="Playback speed"
          onChange={(e) => setSpeed(+e.target.value)}
        >
          <option value=".5">0.5x</option>
          <option value="1">1x</option>
          <option value="2">2x</option>
          <option value="4">4x</option>
        </select>
        <button
          className="icon-button"
          onClick={() => setModal("newrun")}
          aria-label="Save run snapshot"
        >
          <ArrowDownToLine size={15} />
        </button>
      </div>
      <section className={`solver-log ${logOpen ? "" : "collapsed"}`}>
        <button className="log-heading" onClick={() => setLogOpen((v) => !v)}>
          <Terminal size={14} />
          <span>Solver activity</span>
          <span className="log-count">3 events</span>
          <ChevronDown size={14} className={logOpen ? "" : "rotated"} />
        </button>
        {logOpen && (
          <div className="log-lines">
            <div>
              <time>00:00.000</time>
              <span className="log-info">INIT</span>
              <p>
                {activeModule === "battery"
                  ? "Pack topology initialized. 48 cell zones, conservative nearest-neighbor thermal coupling."
                  : `${module.full} reference scenario initialized. Float64 numerical kernels ready.`}
              </p>
            </div>
            <div>
              <time>COMPUTED</time>
              <span className="log-info">SOLVE</span>
              <p>
                {activeModule === "battery"
                  ? `Computed 120 s trajectory with dt = ${params.timestep} s. Reaction fractions remain bounded.`
                  : activeModule === "acoustics"
                    ? "Computed retarded signal and normalized one-sided Hann PSD. 2048 samples."
                    : activeModule === "mhd"
                      ? `Tridiagonal channel solve complete. RMS profile error = ${mhd.error.toExponential(3)}.`
                      : "Nonlinear wall balance and implicit porous-solid conduction complete."}
              </p>
            </div>
            <div>
              <time>{time.toFixed(3).padStart(9, "0")}</time>
              <span className="log-success">READY</span>
              <p>
                Preview at t = {time.toFixed(1)} s. Download the C++20 source
                for the native SoA/FVM implementation.
              </p>
            </div>
          </div>
        )}
      </section>
    </>
  );

  const verification = (
    <div className="secondary-page">
      <div className="section-intro">
        <div>
          <span className="eyebrow">TRUST THE DISCRETIZATION</span>
          <h2>Verification, not assumption.</h2>
          <p>
            Executable invariants, analytical limits, and convergence checks for
            every module.
          </p>
        </div>
        <button
          className="primary-button"
          onClick={executeBrowserTests}
          disabled={testing}
        >
          {testing ? (
            <LoaderCircle size={15} className="spin" />
          ) : (
            <Play size={15} />
          )}
          {testing ? "Running tests..." : "Run browser tests"}
        </button>
      </div>
      <div className="verification-summary">
        <ShieldCheck size={30} />
        <div>
          <b>
            {verificationTab === "Native C++20"
              ? nativeReport
                ? `${nativeReport.tests.filter((t) => t.passed).length} / ${nativeReport.tests.length} native checks passed`
                : "Native report loading"
              : browserTests
                ? `${browserTests.filter((t) => t.passed).length} / ${browserTests.length} browser checks passed`
                : "Browser suite ready"}
          </b>
          <p>
            {verificationTab === "Native C++20"
              ? nativeReport?.compiler || "Build-time C++ compilation report"
              : "Independent TypeScript demonstrator kernels; executed in your browser."}
          </p>
        </div>
        <span className="verified-badge">
          <CheckCheck size={14} />
          {verificationTab === "Native C++20"
            ? "BUILD-TIME REPORT"
            : "LIVE VERIFICATION"}
        </span>
      </div>
      <div className="table-panel">
        <div className="table-toolbar">
          <div className="text-tabs">
            {["Native C++20", "Browser kernels"].map((tab) => (
              <button
                key={tab}
                onClick={() => setVerificationTab(tab)}
                className={verificationTab === tab ? "active" : ""}
              >
                {tab}
              </button>
            ))}
          </div>
          <a href="/verification.json" download className="text-button">
            <Download size={14} />
            Report
          </a>
        </div>
        <div className="verification-table">
          <div className="table-header">
            <span>VERIFICATION CASE</span>
            <span>MODULE</span>
            <span>MEASURED RESULT</span>
            <span>STATUS</span>
          </div>
          {(verificationTab === "Native C++20"
            ? nativeReport?.tests || []
            : browserTests || []
          ).map((test, i) => (
            <div className="test-row" key={`${test.name}-${i}`}>
              <div>
                <b>{test.name}</b>
                <p>{test.detail}</p>
              </div>
              <span className="test-module">{test.module}</span>
              <code>{test.value}</code>
              <span className={test.passed ? "pass-status" : "fail-status"}>
                {test.passed ? <CheckCircle2 size={13} /> : <X size={13} />}
                {test.passed ? "Passed" : "Failed"}
              </span>
            </div>
          ))}
          {verificationTab === "Browser kernels" && !browserTests && (
            <div className="empty-state">
              <FlaskConical size={32} />
              <h3>Ready to put the kernels to the test</h3>
              <p>
                Run eight independent checks on charge integration, stiffness,
                spectra, MHD, and geometric conservation.
              </p>
              <button
                className="secondary-button"
                onClick={executeBrowserTests}
              >
                Run verification suite <ArrowRight size={14} />
              </button>
            </div>
          )}
        </div>
      </div>
      <p className="safety-note">
        <ShieldCheck size={15} />
        Numerical verification is not experimental validation. Reference
        materials and reduced test cases are not certified for safety-critical
        design.
      </p>
    </div>
  );

  const source = (
    <div className="secondary-page">
      <div className="section-intro">
        <div>
          <span className="eyebrow">INSPECT. COMPILE. EXTEND.</span>
          <h2>The physics is in the source.</h2>
          <p>
            A dependency-free C++20 reference, with an explicit SoA ABI and
            executable tests.
          </p>
        </div>
        <a className="primary-button" href="/solver/cluster_c.cpp" download>
          <Download size={15} />
          Download C++20
        </a>
      </div>
      <div className="source-layout">
        <div className="source-main">
          <div className="text-tabs source-tabs">
            {["Overview", "C++ source", "ABI contract"].map((tab) => (
              <button
                key={tab}
                className={sourceTab === tab ? "active" : ""}
                onClick={() => setSourceTab(tab)}
              >
                {tab}
              </button>
            ))}
          </div>
          {sourceTab === "C++ source" ? (
            <div className="code-view">
              <div>
                <FileCode2 size={14} />
                <span>cluster_c.cpp</span>
                <button
                  className="icon-button"
                  aria-label="Copy C++ source"
                  onClick={() =>
                    navigator.clipboard
                      .writeText(sourceText)
                      .then(() => setToast("Source copied to clipboard."))
                      .catch(() =>
                        setToast("Clipboard unavailable. Use Download C++20."),
                      )
                  }
                >
                  <Copy size={14} />
                </button>
              </div>
              <pre>
                <code>{sourceText}</code>
              </pre>
            </div>
          ) : sourceTab === "ABI contract" ? (
            <div className="source-copy">
              <h3>A transparent boundary to your data lake.</h3>
              <p>
                The original proprietary ABI was not supplied. This reference
                defines a documented adapter with a pointer and 64-bit element
                count for each contiguous array. It does not assume binary
                compatibility with an unknown implementation.
              </p>
              <pre>
                <code>
                  {
                    "struct CAbiSpan { double* data; uint64_t size; };\nstruct CAbiIndexSpan { const int32_t* data; uint64_t size; };\n\n// Cell-centered SoA, owner-oriented face geometry\n// neighbor == -1 denotes a boundary face\n// S_f = alpha_f * (x_N - x_P) + k_corr\n// Outward current for neighbor is -J_n,f"
                  }
                </code>
              </pre>
              <h3>Geometry and units</h3>
              <p>
                Coordinates are meters, temperature kelvin, pressure pascals,
                time seconds, and face fluxes are integrated over face area.
                Face normals point outward from the owner. Every span is
                validated before use.
              </p>
              <a className="text-button" href="/solver/README.md" download>
                <Download size={14} />
                Download integration notes
              </a>
            </div>
          ) : (
            <div className="source-copy">
              <h3>One topology. Four coupled physics modules.</h3>
              <p>
                The native reference uses conservative owner-neighbor face
                assembly, harmonic diffusion, deferred non-orthogonal
                correction, and explicit state ownership. Browser plots are
                independent numerical demonstrators, not a connected native
                process.
              </p>
              <div className="method-descriptions">
                {modules.map((m) => (
                  <div key={m.id}>
                    <m.icon size={19} />
                    <div>
                      <h4>{m.full}</h4>
                      <p>{m.method}</p>
                    </div>
                    <span>C.{m.number}</span>
                  </div>
                ))}
              </div>
              <h3>Important modeling contracts</h3>
              <ul>
                <li>
                  Positive current means discharge. I(Vterm - Uocv) is recorded
                  as a signed electrical term; heat generation uses its
                  negative.
                </li>
                <li>
                  Battery abuse parameters are illustrative
                  Hatchard/Spotnitz-style kinetics, not a calibrated chemistry
                  database.
                </li>
                <li>
                  FWH is a subsonic surface-source formulation. Volume
                  quadrupoles are not included.
                </li>
                <li>
                  Discrete charge conservation does not alone prove exact
                  Hartmann balance on an arbitrary mesh.
                </li>
                <li>
                  The native TPS demonstration is one-dimensional ALE; the
                  generic FVM pyrolysis source and wall balance are reusable in
                  3D.
                </li>
              </ul>
            </div>
          )}
        </div>
        <aside className="source-aside">
          <Code2 size={24} className="accent" />
          <h3>Built to be compiled.</h3>
          <p>
            No third-party numerical dependencies. One source file. One command.
          </p>
          <pre>
            {
              "g++ -std=c++20 -O2 \\\n  cluster_c.cpp -o cluster_c\n./cluster_c --verify"
            }
          </pre>
          <button
            className="secondary-button full-width"
            onClick={() =>
              navigator.clipboard
                .writeText(
                  "g++ -std=c++20 -O2 cluster_c.cpp -o cluster_c && ./cluster_c --verify",
                )
                .then(() => setToast("Compile command copied."))
                .catch(() => setToast("Select and copy the command above."))
            }
          >
            <Copy size={14} />
            Copy command
          </button>
          <div className="source-file">
            <FileCode2 size={15} />
            <a href="/solver/cluster_c.cpp" download>
              cluster_c.cpp
            </a>
            <ArrowDownToLine size={13} />
          </div>
          <div className="source-file">
            <BookOpen size={15} />
            <a href="/solver/README.md" download>
              README.md
            </a>
            <ArrowDownToLine size={13} />
          </div>
          <div className="source-file">
            <FileJson size={15} />
            <a href="/verification.json" download>
              verification.json
            </a>
            <ArrowDownToLine size={13} />
          </div>
          <p className="source-small">
            <StatusDot />
            Compiled with g++ / C++20
            <br />
            See the verification report for measured results.
          </p>
        </aside>
      </div>
    </div>
  );

  const runPage = (
    <div className="secondary-page">
      <div className="section-intro">
        <div>
          <span className="eyebrow">YOUR EXPERIMENT NOTEBOOK</span>
          <h2>Every configuration, captured.</h2>
          <p>
            Save a scenario snapshot, then restore its parameters and playback
            position.
          </p>
        </div>
        <button className="primary-button" onClick={() => setModal("newrun")}>
          <Plus size={16} />
          Capture current run
        </button>
      </div>
      <div className="table-panel">
        <div className="table-toolbar">
          <div className="search-input">
            <Search size={15} />
            <input
              aria-label="Search saved runs"
              placeholder="Search runs..."
              value={runSearch}
              onChange={(e) => setRunSearch(e.target.value)}
            />
          </div>
          <span className="muted small">
            {runs.length} saved / local storage
          </span>
        </div>
        {runs.length === 0 ? (
          <div className="empty-state">
            <GitBranch size={34} />
            <h3>Your next discovery starts here.</h3>
            <p>
              No saved runs yet. Capture the current computed scenario to
              compare configurations later.
            </p>
            <button
              className="secondary-button"
              onClick={() => setModal("newrun")}
            >
              <Plus size={14} />
              Save first run
            </button>
          </div>
        ) : (
          <div className="run-table">
            <div className="table-header">
              <span>RUN NAME</span>
              <span>PHYSICS MODULE</span>
              <span>SNAPSHOT</span>
              <span>ACTIONS</span>
            </div>
            {runs
              .filter((run) =>
                `${run.name} ${run.module} ${run.id}`
                  .toLowerCase()
                  .includes(runSearch.toLowerCase()),
              )
              .map((run) => (
                <div className="saved-run" key={run.id}>
                  <div>
                    <b>{run.name}</b>
                    <small>
                      {run.id} / {new Date(run.created).toLocaleString()}
                    </small>
                  </div>
                  <span>{modules.find((m) => m.id === run.module)?.name}</span>
                  <code>{run.time.toFixed(1)} s</code>
                  <div>
                    <button
                      className="text-button"
                      onClick={() => {
                        setParams(run.parameters);
                        setActiveModule(run.module);
                        setTime(run.time);
                        setRunning(false);
                        setPage("telemetry");
                        setToast(`Restored ${run.name}.`);
                      }}
                    >
                      Restore <ArrowRight size={13} />
                    </button>
                    <button
                      className="icon-button"
                      aria-label={`Delete ${run.name}`}
                      onClick={() => {
                        setRuns((r) =>
                          r.filter((saved) => saved.id !== run.id),
                        );
                        setToast("Saved run removed.");
                      }}
                    >
                      <X size={14} />
                    </button>
                  </div>
                </div>
              ))}
          </div>
        )}
      </div>
    </div>
  );

  const meshPage = (
    <div className="secondary-page">
      <div className="section-intro">
        <div>
          <span className="eyebrow">THE SHARED COMPUTATIONAL FABRIC</span>
          <h2>A face-conservative foundation.</h2>
          <p>
            Inspect the reference pack connectivity and cell-centered
            structure-of-arrays.
          </p>
        </div>
        <button
          className="secondary-button"
          onClick={() => fileRef.current?.click()}
        >
          <ArrowDownToLine size={15} className="upload-icon" />
          Validate mesh JSON
        </button>
        <input
          ref={fileRef}
          type="file"
          accept="application/json,.json"
          className="hidden-input"
          onChange={(e) => {
            importMesh(e.target.files?.[0]);
            e.target.value = "";
          }}
        />
      </div>
      {meshImport && (
        <div className="import-summary">
          <CheckCircle2 size={17} />
          <span>
            {meshImport.filename}: {meshImport.cells} cells, {meshImport.faces}{" "}
            faces. Connectivity is valid. The native API accepts full polyhedral
            geometry.
          </span>
          <button
            className="icon-button"
            onClick={() => setMeshImport(null)}
            aria-label="Dismiss imported mesh result"
          >
            <X size={14} />
          </button>
        </div>
      )}
      <div className="mesh-stats">
        {[
          { value: "48", name: "Control volumes" },
          { value: "206", name: "Total faces" },
          { value: "82", name: "Internal faces" },
          { value: "126", name: "Mesh nodes" },
        ].map((s) => (
          <div key={s.name}>
            <span>{s.name}</span>
            <b>{s.value}</b>
          </div>
        ))}
      </div>
      <div className="mesh-layout">
        <Panel
          title="Reference topology"
          eyebrow="8 x 6 x 1 hexahedral finite-volume cells"
          action={<span className="subtle-badge">OWNER-ORIENTED</span>}
        >
          <BatteryField
            frame={frame}
            selected={selectedCell}
            onSelect={setSelectedCell}
            section={true}
            field="Temperature"
          />
          <div className="mesh-hint">
            Select a cell to inspect its neighbors and state.
          </div>
        </Panel>
        <Panel
          title={`Cell ${String(selectedCell + 1).padStart(2, "0")}`}
          eyebrow="SoA field inspector"
        >
          <div className="text-tabs small-tabs">
            {["Cell fields", "Face topology"].map((t) => (
              <button
                key={t}
                className={meshTab === t ? "active" : ""}
                onClick={() => setMeshTab(t)}
              >
                {t}
              </button>
            ))}
          </div>
          <div className="diagnostic-list">
            {meshTab === "Cell fields" ? (
              <>
                <div>
                  <span>Index (zero-based)</span>
                  <b>{selectedCell}</b>
                </div>
                <div>
                  <span>temperature[i]</span>
                  <b>{frame.temperatures[selectedCell].toFixed(3)} K</b>
                </div>
                <div>
                  <span>soc[i]</span>
                  <b>{frame.soc.toFixed(6)}</b>
                </div>
                <div>
                  <span>volume[i]</span>
                  <b>2.80e-5 m3</b>
                </div>
                <div>
                  <span>Memory layout</span>
                  <b>Contiguous / Float64</b>
                </div>
              </>
            ) : (
              <>
                {[
                  {
                    label: "West neighbor",
                    id: selectedCell % 8 > 0 ? selectedCell - 1 : -1,
                  },
                  {
                    label: "East neighbor",
                    id: selectedCell % 8 < 7 ? selectedCell + 1 : -1,
                  },
                  {
                    label: "South neighbor",
                    id: selectedCell >= 8 ? selectedCell - 8 : -1,
                  },
                  {
                    label: "North neighbor",
                    id: selectedCell < 40 ? selectedCell + 8 : -1,
                  },
                ].map((n) => (
                  <div key={n.label}>
                    <span>{n.label}</span>
                    {n.id >= 0 ? (
                      <button
                        className="text-button"
                        onClick={() => setSelectedCell(n.id)}
                      >
                        Cell {n.id + 1} <ArrowRight size={12} />
                      </button>
                    ) : (
                      <b>Boundary</b>
                    )}
                  </div>
                ))}
                <div>
                  <span>Top / bottom</span>
                  <b>Cooling boundaries</b>
                </div>
              </>
            )}
          </div>
          <button
            className="secondary-button full-width"
            onClick={() =>
              downloadText(
                "vector-mesh-contract.json",
                JSON.stringify(
                  {
                    note: "Minimal connectivity example; provide coordinates and owner-oriented face geometry for native solves.",
                    volume: [2.8e-5, 2.8e-5],
                    owner: [0, 0, 1],
                    neighbor: [-1, 1, -1],
                  },
                  null,
                  2,
                ),
                "application/json",
              )
            }
          >
            <FileJson size={14} />
            Download mesh example
          </button>
        </Panel>
      </div>
    </div>
  );

  return (
    <div className="app-shell">
      {sidebarOpen && (
        <div
          className="sidebar-backdrop"
          onClick={() => setSidebarOpen(false)}
        />
      )}
      <aside className={`sidebar ${sidebarOpen ? "is-open" : ""}`}>
        <a
          href="#"
          className="brand"
          aria-label="Vector Multiphysics home"
          onClick={(e) => {
            e.preventDefault();
            navigate("telemetry");
          }}
        >
          <svg viewBox="0 0 32 33" fill="none">
            <path d="M3 6h8l8 14-4 7L3 6Z" fill="#c4a9ed" />
            <path d="M17 6h11L17 27h-9L20 6" fill="#9e83c8" />
            <path d="M13 6h8l-4 7-4-7Z" fill="#e1cef9" />
          </svg>
          <div>
            VECTOR<span>MULTIPHYSICS</span>
          </div>
          <span className="brand-version">C</span>
        </a>
        <button
          className="workspace-switch"
          onClick={() => setModal("settings")}
        >
          <div className="workspace-icon">
            <Box size={16} />
          </div>
          <div>
            Research workspace<span>Local environment</span>
          </div>
          <ChevronDown size={13} />
        </button>
        <div className="nav-label">WORKSPACE</div>
        <nav className="main-nav">
          {[
            {
              id: "telemetry" as const,
              name: "Workbench",
              icon: LayoutDashboard,
            },
            { id: "runs" as const, name: "Simulation runs", icon: GitBranch },
            { id: "mesh" as const, name: "Mesh explorer", icon: Box },
            {
              id: "verification" as const,
              name: "Verification",
              icon: ShieldCheck,
            },
            { id: "source" as const, name: "Source & API", icon: Code2 },
          ].map((item) => (
            <button
              className={page === item.id ? "active" : ""}
              onClick={() => navigate(item.id)}
              key={item.id}
            >
              <item.icon size={17} />
              <span>{item.name}</span>
              {item.id === "runs" && (
                <small>{String(runs.length).padStart(2, "0")}</small>
              )}
              {item.id === "verification" && <i className="nav-verified" />}
            </button>
          ))}
        </nav>
        <div className="nav-label physics-label">
          PHYSICS MODULES<span>04</span>
        </div>
        <nav className="physics-nav">
          {modules.map((m) => (
            <button
              key={m.id}
              className={activeModule === m.id ? "active" : ""}
              onClick={() => {
                selectModule(m.id);
                setSidebarOpen(false);
              }}
            >
              <span className="physics-line" />
              <span>
                {m.id === "battery"
                  ? "Battery thermal"
                  : m.id === "mhd"
                    ? "Magnetohydrodynamics"
                    : m.name}
              </span>
              {activeModule === m.id && <i />}
            </button>
          ))}
        </nav>
        <div className="sidebar-bottom">
          <div className="engine-status">
            <div>
              <Cpu size={16} />
              <span>
                Reference engine
                <StatusDot />
              </span>
            </div>
            <p>
              C++20 <span>/</span> SoA CAbiSpan
            </p>
            <div className="engine-version">
              <span>CLUSTER C</span>
              <span>v1.0.0</span>
            </div>
          </div>
          <button
            className="help-link"
            onClick={() => {
              navigate("source");
              setSourceTab("Overview");
            }}
          >
            <CircleHelp size={16} />
            Documentation
            <ExternalLink size={12} />
          </button>
          <button className="user-profile" onClick={() => setModal("settings")}>
            <div className="avatar">SC</div>
            <div>
              Scientist console<span>Engineering workspace</span>
            </div>
            <MoreHorizontal size={16} />
          </button>
        </div>
      </aside>
      <main className="main-shell">
        <header className="topbar">
          <div className="breadcrumb">
            <button
              className="mobile-menu icon-button"
              aria-label="Open navigation"
              onClick={() => setSidebarOpen(true)}
            >
              <Menu size={19} />
            </button>
            <span className="breadcrumb-home">Workspace</span>
            <ChevronRight size={13} />
            <span>Cluster C</span>
            <ChevronRight size={13} />
            <b>{pageLabels[page]}</b>
          </div>
          <div className="topbar-right">
            <span className="local-status">
              <StatusDot />
              Local environment
            </span>
            <div className="topbar-divider" />
            <button
              className="icon-button"
              aria-label="View notifications"
              onClick={() => setModal("notifications")}
            >
              <Bell size={16} />
              <i className="notification-dot" />
            </button>
            <button
              className="icon-button"
              aria-label="Workspace settings"
              onClick={() => setModal("settings")}
            >
              <Settings2 size={16} />
            </button>
            <div className="top-avatar">SC</div>
          </div>
        </header>
        <div className="page-content">
          <div className="page-heading">
            <div>
              <div className="page-eyebrow">
                <span className="cluster-square">C</span>ADVANCED PHYSICS SUITE
                <span className="eyebrow-divider" />
                C++20
              </div>
              <h1>
                {page === "telemetry"
                  ? "Cluster C workbench"
                  : pageLabels[page]}
              </h1>
              <p>
                {page === "telemetry"
                  ? "Four physics. One computational core."
                  : page === "verification"
                    ? "Make every numerical claim testable."
                    : page === "source"
                      ? "Open methods. Explicit assumptions. Reproducible results."
                      : page === "mesh"
                        ? "Shared topology for coupled multiphysics."
                        : "Your local simulation history and saved configurations."}
              </p>
            </div>
            <div className="heading-actions">
              <button
                className="secondary-button source-action"
                onClick={() => navigate("source")}
              >
                <Code2 size={15} />
                View source
              </button>
              <div className="export-wrap">
                <button
                  className="secondary-button export-button"
                  onClick={() => setExportOpen((v) => !v)}
                >
                  <Download size={15} />
                  Export data
                  <ChevronDown size={13} />
                </button>
                {exportOpen && (
                  <>
                    <div
                      className="dropdown-backdrop"
                      onClick={() => setExportOpen(false)}
                    />
                    <div className="export-menu">
                      <span>EXPORT {module.name.toUpperCase()}</span>
                      <button onClick={() => exportData("csv")}>
                        <Activity size={15} />
                        <div>
                          Telemetry CSV<small>Computed numerical series</small>
                        </div>
                      </button>
                      <button onClick={() => exportData("json")}>
                        <FileJson size={15} />
                        <div>
                          Scenario JSON<small>Parameters and all results</small>
                        </div>
                      </button>
                      <a
                        href="/solver/cluster_c.cpp"
                        download
                        onClick={() => setExportOpen(false)}
                      >
                        <FileCode2 size={15} />
                        <div>
                          C++20 source
                          <small>Native reference implementation</small>
                        </div>
                      </a>
                    </div>
                  </>
                )}
              </div>
            </div>
          </div>
          {page === "telemetry" && (
            <div
              className="module-tabs"
              role="tablist"
              aria-label="Physics modules"
            >
              {modules.map((m) => (
                <button
                  key={m.id}
                  role="tab"
                  aria-selected={activeModule === m.id}
                  className={activeModule === m.id ? "active" : ""}
                  onClick={() => selectModule(m.id)}
                >
                  <div className="module-icon">
                    <m.icon size={20} strokeWidth={1.6} />
                  </div>
                  <div>
                    <strong>{m.name}</strong>
                    <span>{m.subtitle}</span>
                  </div>
                  <small>C.{m.number}</small>
                  {activeModule === m.id && (
                    <motion.i
                      layoutId="module-active"
                      className="module-active-line"
                      transition={{
                        type: "spring",
                        stiffness: 450,
                        damping: 40,
                      }}
                    />
                  )}
                </button>
              ))}
            </div>
          )}
          <AnimatePresence mode="wait">
            <motion.div
              key={page === "telemetry" ? activeModule : page}
              initial={{ opacity: 0, y: 8 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.25, ease }}
            >
              {page === "telemetry"
                ? telemetry
                : page === "verification"
                  ? verification
                  : page === "source"
                    ? source
                    : page === "mesh"
                      ? meshPage
                      : runPage}
            </motion.div>
          </AnimatePresence>
          <footer className="page-footer">
            <span>
              <StatusDot />
              All systems nominal<span className="footer-separator">/</span>
              <b>SoA data layout</b>
            </span>
            <span>
              Numerical reference environment<small>VECTOR / CLUSTER C</small>
            </span>
          </footer>
        </div>
      </main>
      <AnimatePresence>
        {toast && (
          <motion.div
            className="toast"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: 10 }}
          >
            <CheckCircle2 size={17} />
            <span>{toast}</span>
            <button
              className="icon-button"
              aria-label="Dismiss notification"
              onClick={() => setToast("")}
            >
              <X size={14} />
            </button>
          </motion.div>
        )}
      </AnimatePresence>
      <AnimatePresence>
        {modal && (
          <motion.div
            className="modal-backdrop"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            onClick={() => setModal(null)}
          >
            <motion.div
              className={`modal ${modal === "field" ? "field-modal" : ""}`}
              role="dialog"
              aria-modal="true"
              aria-label={
                modal === "newrun"
                  ? "Save simulation run"
                  : modal === "settings"
                    ? "Workspace settings"
                    : modal === "notifications"
                      ? "Notifications"
                      : "Expanded field visualization"
              }
              initial={{ opacity: 0, scale: 0.97, y: 12 }}
              animate={{ opacity: 1, scale: 1, y: 0 }}
              exit={{ opacity: 0, scale: 0.98 }}
              onClick={(e) => e.stopPropagation()}
            >
              <button
                className="modal-close icon-button"
                aria-label="Close dialog"
                onClick={() => setModal(null)}
              >
                <X size={18} />
              </button>
              {modal === "newrun" ? (
                <>
                  <div className="modal-symbol">
                    <GitBranch size={24} />
                  </div>
                  <h2>Capture this configuration.</h2>
                  <p>
                    Save the current parameters and time position to your local
                    experiment notebook.
                  </p>
                  <form
                    onSubmit={(e) => {
                      e.preventDefault();
                      saveRun();
                    }}
                  >
                    <label className="form-label">
                      RUN NAME
                      <input
                        className="text-input"
                        autoFocus
                        value={runName}
                        onChange={(e) => setRunName(e.target.value)}
                        placeholder="Name your experiment"
                        required
                        maxLength={80}
                      />
                    </label>
                    <div className="snapshot-details">
                      <span>{module.full}</span>
                      <b>t = {time.toFixed(1)} s</b>
                    </div>
                    <button className="primary-button full-width" type="submit">
                      <Check size={15} />
                      Save run snapshot
                    </button>
                  </form>
                </>
              ) : modal === "settings" ? (
                <>
                  <div className="modal-symbol">
                    <Settings2 size={24} />
                  </div>
                  <h2>Research workspace</h2>
                  <p>
                    A local, reproducible environment. No account or remote
                    compute connection is required.
                  </p>
                  <div className="diagnostic-list">
                    <div>
                      <span>Compute backend</span>
                      <b>Browser / Float64</b>
                    </div>
                    <div>
                      <span>Native reference</span>
                      <b>C++20 / g++ -O2</b>
                    </div>
                    <div>
                      <span>Run storage</span>
                      <b>This browser only</b>
                    </div>
                    <div>
                      <span>Playback speed</span>
                      <select
                        value={speed}
                        onChange={(e) => setSpeed(+e.target.value)}
                        aria-label="Default playback speed"
                      >
                        <option value=".5">0.5x</option>
                        <option value="1">1x</option>
                        <option value="2">2x</option>
                        <option value="4">4x</option>
                      </select>
                    </div>
                  </div>
                  <button
                    className="secondary-button full-width"
                    onClick={() => {
                      setParams(defaultParameters);
                      setTime(50);
                      setRunning(false);
                      setModal(null);
                      setToast("Default scenario parameters restored.");
                    }}
                  >
                    <RotateCcw size={14} />
                    Restore default parameters
                  </button>
                </>
              ) : modal === "notifications" ? (
                <>
                  <div className="modal-symbol">
                    <Bell size={24} />
                  </div>
                  <h2>Workspace activity</h2>
                  <p>Your reference environment is ready.</p>
                  <div className="notification-item">
                    <ShieldCheck size={20} />
                    <div>
                      <b>Native verification report available</b>
                      <p>
                        {nativeReport
                          ? `${nativeReport.tests.filter((t) => t.passed).length} of ${nativeReport.tests.length} C++ tests passed at build time.`
                          : "Open Verification to inspect the available test report."}
                      </p>
                      <button
                        className="text-button"
                        onClick={() => {
                          setModal(null);
                          navigate("verification");
                        }}
                      >
                        View report <ArrowRight size={13} />
                      </button>
                    </div>
                  </div>
                  <div className="notification-item">
                    <Cpu size={20} />
                    <div>
                      <b>Browser reference mode</b>
                      <p>
                        Telemetry is computed locally. No native solver process
                        is connected.
                      </p>
                    </div>
                  </div>
                </>
              ) : (
                <>
                  <div className="page-eyebrow">{module.method}</div>
                  <h2>{module.full}</h2>
                  <div className="expanded-field">{fieldView}</div>
                  <div className="expanded-controls">
                    <span>t = {time.toFixed(1)} s</span>
                    <input
                      aria-label="Expanded simulation time"
                      type="range"
                      min="0"
                      max="120"
                      step=".5"
                      value={time}
                      onChange={(e) => {
                        setTime(+e.target.value);
                        setRunning(false);
                      }}
                    />
                    <button className="primary-button" onClick={play}>
                      {running ? <Pause size={14} /> : <Play size={14} />}
                      {running ? "Pause" : "Play"}
                    </button>
                  </div>
                </>
              )}
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}

function ArrowUpRightIcon() {
  return (
    <svg
      width="13"
      height="13"
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
    >
      <path d="M4 12 12 4M4 4h8v8" />
    </svg>
  );
}
