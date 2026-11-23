import { useCallback, useEffect, useRef, useState } from "react";
import {
  ArrowLeftRight,
  Box,
  Boxes,
  BookOpen,
  Check,
  ChevronDown,
  ChevronRight,
  CircleCheck,
  CircleHelp,
  Cpu,
  Folder,
  Layers3,
  LayoutGrid,
  LoaderCircle,
  Play,
  RotateCcw,
  ScanLine,
  Settings2,
  SlidersHorizontal,
  TriangleAlert,
  X,
} from "lucide-react";
import {
  Documentation,
  GeometryLibrary,
  LoadingView,
  Logo,
  Parameters,
  Toggle,
} from "./components";
import MeshViewport from "./MeshViewport";
import Diagnostics, { Verification } from "./Diagnostics";
import type { Tab, Log, FaResult } from "./Diagnostics";
import {
  FILES,
  GEOMETRIES,
  PATCHES,
  download,
  integer,
  sci,
  vtk,
} from "./mesh";
import type { Axis, ColorField, Geometry, MeshData, MeshParams } from "./mesh";

type Dialog = "library" | "docs" | "parameters" | "checks" | null;
const time = () => new Date().toLocaleTimeString("en-GB", { hour12: false });
const cache = new Map<Geometry, MeshData>();
const defaults: MeshParams = { layers: 6, first: 0.18, growth: 1.22 };
export default function App() {
  const [geometry, setGeometry] = useState<Geometry>(0),
    [mesh, setMesh] = useState<MeshData | null>(null),
    [loadError, setLoadError] = useState<string | null>(null),
    [retry, setRetry] = useState(0);
  const [dialog, setDialog] = useState<Dialog>(null),
    [docPage, setDocPage] = useState(0),
    [tab, setTab] = useState<Tab>("quality");
  const [axis, setAxis] = useState<Axis>("Y"),
    [position, setPosition] = useState(42),
    [slice, setSlice] = useState(true),
    [flip, setFlip] = useState(false),
    [field, setField] = useState<ColorField>("distance");
  const [edges, setEdges] = useState(true),
    [grid, setGrid] = useState(true),
    [plane, setPlane] = useState(true),
    [visible, setVisible] = useState([8, 6, 5, 4]),
    [resetKey, setResetKey] = useState(0);
  const [selected, setSelected] = useState<number | null>(null),
    [working, setWorking] = useState<"verify" | "build" | "fa" | null>(null),
    [logs, setLogs] = useState<Log[]>([]),
    [toast, setToast] = useState<{ text: string; error?: boolean } | null>(
      null,
    );
  const [patch, setPatch] = useState(-1),
    [previewWall, setPreviewWall] = useState(false),
    [fa, setFA] = useState<FaResult | null>(null);
  const [geometryMenu, setGeometryMenu] = useState(false),
    [showInspector, setShowInspector] = useState(true);
  const worker = useRef<Worker | null>(null),
    surfaceDownload = useRef(false),
    toastTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const notify = useCallback((text: string, error = false) => {
    setToast({ text, error });
    if (toastTimer.current) clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), 5000);
  }, []);
  const log = useCallback(
    (message: string, kind: Log["kind"] = "info") =>
      setLogs((l) => [...l, { time: time(), message, kind }].slice(-100)),
    [],
  );
  useEffect(
    () => () => {
      worker.current?.terminate();
      if (toastTimer.current) clearTimeout(toastTimer.current);
    },
    [],
  );
  useEffect(() => {
    const controller = new AbortController();
    setLoadError(null);
    setMesh(null);
    setFA(null);
    setPreviewWall(false);
    setSelected(null);
    setWorking(null);
    worker.current?.terminate();
    worker.current = null;
    const load = async () => {
      try {
        let m = cache.get(geometry);
        if (!m) {
          const r = await fetch(`/meshes/${FILES[geometry]}.json`, {
            signal: controller.signal,
          });
          if (!r.ok)
            throw new Error("The reference geometry could not be loaded.");
          m = (await r.json()) as MeshData;
          cache.set(geometry, m);
        }
        if (controller.signal.aborted) return;
        setMesh(m);
        setLogs([
          {
            time: time(),
            kind: "info",
            message: `Loaded ${GEOMETRIES[geometry]} from native C++20 fixture.`,
          },
          {
            time: time(),
            kind: "pass",
            message: `Verified ${integer(m.stats.cells)} cells. Positive volumes, conformal faces, and Gauss closure passed.`,
          },
        ]);
      } catch (e) {
        if (!controller.signal.aborted)
          setLoadError(e instanceof Error ? e.message : "Unable to load mesh.");
      }
    };
    void load();
    return () => controller.abort();
  }, [geometry, retry]);
  useEffect(() => {
    if (!geometryMenu) return;
    const close = () => setGeometryMenu(false);
    const escape = (e: KeyboardEvent) => {
      if (e.key === "Escape") close();
    };
    document.addEventListener("click", close);
    document.addEventListener("keydown", escape);
    return () => {
      document.removeEventListener("click", close);
      document.removeEventListener("keydown", escape);
    };
  }, [geometryMenu]);
  const selectGeometry = (g: Geometry) => {
    setGeometryMenu(false);
    setDialog(null);
    setGeometry(g);
    setTab("quality");
    setVisible([8, 6, 5, 4]);
    setSlice(true);
    setAxis(g ? "Z" : "Y");
    setPosition(g ? 56 : 42);
    setFlip(false);
    setResetKey((n) => n + 1);
  };
  const reset = () => {
    setSlice(true);
    setAxis(geometry ? "Z" : "Y");
    setPosition(geometry ? 56 : 42);
    setFlip(false);
    setField("distance");
    setEdges(true);
    setGrid(true);
    setPlane(true);
    setVisible([8, 6, 5, 4]);
    setPreviewWall(false);
    setSelected(null);
    setResetKey((k) => k + 1);
    notify("Inspection view reset");
  };
  const run = (
    action: "verify" | "build" | "fa",
    params: MeshParams = mesh?.params ?? defaults,
    downloadAfter = false,
  ) => {
    if (working || !mesh) return;
    setDialog(null);
    setWorking(action);
    surfaceDownload.current = downloadAfter;
    log(
      action === "fa"
        ? `Extracting finite-area mesh: ${patch < 0 ? "all walls" : PATCHES[patch]}.`
        : `Starting C++20 ${action === "build" ? "rebuild" : "verification"} for ${GEOMETRIES[geometry]}.`,
    );
    if (!worker.current) worker.current = new Worker("/engine/worker.js");
    worker.current.onmessage = ({ data }) => {
      if (data.stage) {
        log(data.stage + ".");
        return;
      }
      setWorking(null);
      if (data.error) {
        log(data.error, "error");
        notify(`Mesh rejected: ${data.error}`, true);
        return;
      }
      if (data.mesh) {
        const next = data.mesh as MeshData;
        setMesh(next);
        cache.set(geometry, next);
        setFA(null);
        setSelected(null);
        log(
          `PASS: ${integer(next.stats.cells)} cells; min JENS ${next.stats.minJ.toFixed(6)}; closure ${sci(next.stats.maxClosure)}.`,
          "pass",
        );
        notify(
          action === "build"
            ? "Mesh rebuilt. All quality checks passed."
            : "Verification complete. All checks passed.",
        );
      }
      if (data.fa) {
        const result = data.fa as FaResult;
        setFA(result);
        log(
          `Extracted ${integer(result.faces.length)} finite-area faces with unique volume owners.`,
          "pass",
        );
        notify(`${integer(result.faces.length)} wall faces extracted`);
        if (surfaceDownload.current)
          download(
            `${FILES[geometry]}-${result.patch}-faMesh.json`,
            JSON.stringify(result),
          );
      }
    };
    worker.current.onerror = (e) => {
      setWorking(null);
      log(e.message, "error");
      notify("The engine worker could not finish. Please retry.", true);
      worker.current?.terminate();
      worker.current = null;
    };
    worker.current.postMessage({
      action: action === "fa" ? "surface" : "build",
      geometry,
      params,
      patch,
    });
  };
  const exportFile = (type: "report" | "vtk" | "fa") => {
    if (!mesh) return;
    if (type === "fa") {
      if (fa) {
        download(
          `${FILES[geometry]}-${fa.patch}-faMesh.json`,
          JSON.stringify(fa),
        );
        notify("Finite-area mesh exported");
      } else run("fa", mesh.params, true);
      return;
    }
    if (type === "vtk")
      download(`${FILES[geometry]}.vtk`, vtk(mesh), "text/plain");
    else
      download(
        `${FILES[geometry]}-verification.json`,
        JSON.stringify(
          {
            engine: "HexaLab C++20",
            version: mesh.version,
            geometry: GEOMETRIES[geometry],
            meshId: mesh.id,
            units: "mm",
            parameters: mesh.params,
            thresholds: {
              minSignedVolume: 0,
              minJENS: 0.25,
              maxNormalizedGaussClosure: 1e-14,
            },
            scope:
              "Numerical verification of the selected analytic fixture; not a universal CAD proof.",
            results: mesh.stats,
          },
          null,
          2,
        ),
      );
    notify(
      type === "vtk"
        ? "VTK volume mesh exported"
        : "Verification report exported",
    );
    log(
      `Exported ${type === "vtk" ? "VTK unstructured grid" : "JSON verification report"}.`,
    );
  };
  const chooseCell = (id: number) => {
    setSelected(id);
    setTab("cell");
  };
  const chooseTab = (t: Tab) => {
    setTab(t);
    if (t !== "surface") setPreviewWall(false);
  };
  return (
    <div className="app-shell">
      <header className="topbar">
        <button
          className="brand-button"
          aria-label="HexaLab workspace"
          onClick={() => {
            setDialog(null);
            chooseTab("quality");
          }}
        >
          <Logo />
        </button>
        <span className="brand-divider" />
        <nav className="main-nav" aria-label="Main navigation">
          <button
            className={
              dialog !== "library" && dialog !== "docs" ? "active" : ""
            }
            onClick={() => {
              setDialog(null);
              chooseTab("quality");
            }}
          >
            <LayoutGrid size={15} />
            Workspace
          </button>
          <button
            className={dialog === "library" ? "active" : ""}
            onClick={() => setDialog("library")}
          >
            <Boxes size={16} />
            Mesh library
          </button>
          <button
            className={dialog === "docs" ? "active" : ""}
            onClick={() => {
              setDocPage(0);
              setDialog("docs");
            }}
          >
            <BookOpen size={15} />
            Documentation
          </button>
        </nav>
        <div className="topbar-status">
          <span className={`status-dot ${working ? "working" : ""}`} />
          {working ? "Engine running" : "Engine ready"}
          <span className="version">v1.0.0</span>
        </div>
      </header>
      <main>
        <section className="workspace-intro">
          <div>
            <div className="breadcrumb">
              <Folder size={11} />
              <span>Workspace</span>
              <ChevronRight size={10} />
              <span>Mesh analysis</span>
            </div>
            <h1>Mesh inspector</h1>
            <p>Hybrid mesh visualization and quality diagnostics.</p>
          </div>
          <div className="workspace-actions">
            <div className="relative">
              <button
                className="geometry-selector"
                onClick={(e) => {
                  e.stopPropagation();
                  setGeometryMenu(!geometryMenu);
                }}
                aria-haspopup="menu"
                aria-expanded={geometryMenu}
                disabled={!!working}
              >
                <Box size={16} />
                <span>{GEOMETRIES[geometry]}</span>
                <ChevronDown size={13} />
              </button>
              {geometryMenu && (
                <div className="menu geometry-menu" role="menu">
                  <div className="menu-caption">REFERENCE GEOMETRY</div>
                  {([0, 1] as Geometry[]).map((g) => (
                    <button
                      key={g}
                      role="menuitem"
                      onClick={() => selectGeometry(g)}
                    >
                      <Box size={14} />
                      {GEOMETRIES[g]}
                      {geometry === g && (
                        <Check className="text-mint" size={14} />
                      )}
                    </button>
                  ))}
                </div>
              )}
            </div>
            <button
              className="primary-button verify-button"
              disabled={!!working || !mesh}
              onClick={() => run("verify")}
            >
              {working ? (
                <LoaderCircle size={15} className="spin" />
              ) : (
                <Play size={14} fill="currentColor" strokeWidth={1.5} />
              )}
              <span>
                {working === "verify"
                  ? "Verifying..."
                  : working === "build"
                    ? "Rebuilding..."
                    : working === "fa"
                      ? "Extracting..."
                      : "Run verification"}
              </span>
            </button>
          </div>
        </section>
        {!mesh ? (
          <LoadingView error={loadError} retry={() => setRetry((n) => n + 1)} />
        ) : (
          <>
            <div className="inspection-layout">
              <MeshViewport
                mesh={mesh}
                slice={slice && !previewWall}
                axis={axis}
                position={position}
                flip={flip}
                field={field}
                edges={edges}
                grid={grid}
                plane={plane}
                visible={visible}
                wallPatch={previewWall ? patch : null}
                selected={selected}
                onPick={chooseCell}
                onVisibility={(n) =>
                  setVisible((v) =>
                    v.includes(n) ? v.filter((i) => i !== n) : [...v, n],
                  )
                }
                resetKey={resetKey}
                busy={!!working}
                setEdges={setEdges}
              />
              <aside
                className={`inspector panel ${showInspector ? "" : "inspector-folded"}`}
              >
                <div className="panel-heading">
                  <div className="panel-title">
                    <SlidersHorizontal size={16} />
                    <h2>Inspector</h2>
                  </div>
                  <div className="flex items-center gap-2">
                    <button
                      className="icon-button"
                      onClick={reset}
                      aria-label="Reset inspection controls"
                      title="Reset inspection controls"
                    >
                      <RotateCcw size={14} />
                    </button>
                    <button
                      className="mobile-fold icon-button"
                      onClick={() => setShowInspector(!showInspector)}
                      aria-label="Toggle inspector"
                    >
                      <ChevronDown size={15} />
                    </button>
                  </div>
                </div>
                <div className="inspector-content">
                  <div className="inspection-section slice-section">
                    <div className="section-label">
                      <span>
                        <ScanLine size={13} />
                        Slice plane
                      </span>
                      <Toggle
                        label="Enable slice plane"
                        value={slice}
                        onChange={(v) => {
                          setSlice(v);
                          setPreviewWall(false);
                        }}
                      />
                    </div>
                    <div className={!slice ? "disabled-section" : ""}>
                      <div className="axis-label">
                        <span>Plane normal</span>
                        <button
                          className={flip ? "flipped" : ""}
                          onClick={() => setFlip(!flip)}
                          disabled={!slice}
                          title="Reverse the retained half-space"
                        >
                          <ArrowLeftRight size={11} />
                          Flip
                        </button>
                      </div>
                      <div
                        className="axis-switch"
                        role="group"
                        aria-label="Slice plane normal"
                      >
                        {(["X", "Y", "Z"] as Axis[]).map((a) => (
                          <button
                            key={a}
                            disabled={!slice}
                            aria-pressed={axis === a}
                            className={axis === a ? "selected" : ""}
                            onClick={() => setAxis(a)}
                          >
                            {a}
                            <span> axis</span>
                          </button>
                        ))}
                      </div>
                      <div className="position-label">
                        <label htmlFor="slice-position">Slice position</label>
                        <span className="position-value">
                          {position.toFixed(1)}
                          <i>%</i>
                        </span>
                      </div>
                      <input
                        id="slice-position"
                        className="range"
                        type="range"
                        min="0"
                        max="100"
                        step="0.5"
                        value={position}
                        disabled={!slice}
                        style={
                          { "--range": `${position}%` } as React.CSSProperties
                        }
                        onChange={(e) => setPosition(+e.target.value)}
                      />
                      <div className="range-labels">
                        <span>0%</span>
                        <span>50%</span>
                        <span>100%</span>
                      </div>
                    </div>
                  </div>
                  <div className="inspection-section display-section">
                    <div className="section-label">
                      <span>
                        <Settings2 size={13} />
                        Display
                      </span>
                    </div>
                    <label className="field-label" htmlFor="color-field">
                      Color by
                    </label>
                    <div className="select-wrapper">
                      <span className="color-key">
                        <i />
                        <i />
                        <i />
                      </span>
                      <select
                        id="color-field"
                        value={field}
                        onChange={(e) => setField(e.target.value as ColorField)}
                      >
                        <option value="distance">Wall distance</option>
                        <option value="type">Element type</option>
                        <option value="quality">Scaled Jacobian</option>
                        <option value="layer">Boundary-layer depth</option>
                      </select>
                      <ChevronDown size={13} />
                    </div>
                    <div className="switch-line">
                      <span>Cell edges</span>
                      <Toggle
                        label="Show cell edges"
                        value={edges}
                        onChange={setEdges}
                      />
                    </div>
                    <div className="switch-line">
                      <span>Reference grid</span>
                      <Toggle
                        label="Show reference grid"
                        value={grid}
                        onChange={setGrid}
                      />
                    </div>
                    <div className="switch-line">
                      <span>Plane guide</span>
                      <Toggle
                        label="Show plane guide"
                        value={plane}
                        onChange={setPlane}
                      />
                    </div>
                  </div>
                  <button
                    className="parameters-link"
                    onClick={() => setDialog("parameters")}
                    disabled={!!working}
                  >
                    <span className="parameter-icon">
                      <Layers3 size={17} />
                    </span>
                    <span>
                      <strong>Meshing parameters</strong>
                      <small>
                        {mesh.params.layers} layers <b>/</b>{" "}
                        {mesh.params.growth.toFixed(2)}x growth
                      </small>
                    </span>
                    <ChevronRight size={14} />
                  </button>
                </div>
              </aside>
            </div>
            <Diagnostics
              mesh={mesh}
              tab={tab}
              setTab={chooseTab}
              logs={logs}
              selected={selected}
              chooseCell={chooseCell}
              patch={patch}
              setPatch={(n) => {
                setPatch(n);
                setFA(null);
              }}
              fa={fa}
              preview={previewWall}
              setPreview={(p) => {
                setPreviewWall(p);
                setVisible([8, 6, 5, 4]);
              }}
              working={!!working}
              extract={() => run("fa")}
              exportFile={exportFile}
              openChecks={() => setDialog("checks")}
            />
          </>
        )}
      </main>
      <footer className="statusbar">
        <div>
          <span className={`status-dot ${working ? "working" : ""}`} />
          <span>{working ? "Computing" : "Ready"}</span>
          <i />
          <span className="status-id">
            {mesh ? `Mesh ${mesh.id}` : "Initializing"}
          </span>
        </div>
        <div>
          <Cpu size={12} />
          <span>C++20 engine</span>
          <i />
          <span>Float64</span>
          <i />
          <span>Units: mm</span>
          <button
            aria-label="Open technical documentation"
            title="Technical documentation"
            onClick={() => {
              setDocPage(1);
              setDialog("docs");
            }}
          >
            <CircleHelp size={13} />
          </button>
        </div>
      </footer>
      {dialog === "library" && (
        <GeometryLibrary
          current={geometry}
          onSelect={selectGeometry}
          onClose={() => setDialog(null)}
        />
      )}
      {dialog === "docs" && (
        <Documentation initial={docPage} onClose={() => setDialog(null)} />
      )}
      {dialog === "parameters" && mesh && (
        <Parameters
          mesh={mesh}
          onBuild={(p) => run("build", p)}
          onClose={() => setDialog(null)}
        />
      )}
      {dialog === "checks" && mesh && (
        <Verification mesh={mesh} onClose={() => setDialog(null)} />
      )}
      {toast && (
        <div className={`toast ${toast.error ? "error" : ""}`} role="status">
          {toast.error ? (
            <TriangleAlert size={16} />
          ) : (
            <CircleCheck size={16} />
          )}
          <span>{toast.text}</span>
          <button
            aria-label="Dismiss notification"
            onClick={() => setToast(null)}
          >
            <X size={13} />
          </button>
        </div>
      )}
    </div>
  );
}
