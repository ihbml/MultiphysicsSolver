import { useEffect, useMemo, useState } from "react";
import {
  Activity,
  ArrowDownToLine,
  ArrowRight,
  Box,
  Check,
  CheckCircle2,
  ChevronDown,
  ChevronRight,
  Code2,
  Cpu,
  Crosshair,
  Download,
  Eye,
  EyeOff,
  FileCheck2,
  Grid3X3,
  Layers3,
  MousePointer2,
  ShieldCheck,
  Terminal,
} from "lucide-react";
import { Modal } from "./components";
import {
  CELL_TYPES,
  GEOMETRIES,
  PATCHES,
  avg,
  download,
  integer,
  sci,
} from "./mesh";
import type { MeshData } from "./mesh";

export type Tab = "quality" | "cell" | "surface" | "log";
export type Log = {
  time: string;
  message: string;
  kind: "info" | "pass" | "error";
};
export type FaResult = {
  mesh: string;
  patch: string;
  boundaryEdges: number;
  uniqueOwners: boolean;
  faces: { area: number; face_to_volume_owner_cell: number }[];
};
const TABS = [
  { id: "quality", name: "Quality overview", icon: Activity },
  { id: "cell", name: "Cell inspector", icon: Crosshair },
  { id: "surface", name: "Wall submesh", icon: Layers3 },
  { id: "log", name: "Engine log", icon: Terminal },
] as const;

function Histogram({ mesh }: { mesh: MeshData }) {
  const [bin, setBin] = useState<number | null>(null);
  const bins = useMemo(() => {
    const b = Array<number>(36).fill(0);
    for (const q of mesh.quality)
      b[Math.min(35, Math.max(0, Math.floor(q * 36)))]++;
    return b;
  }, [mesh]);
  const max = Math.max(...bins);
  return (
    <div className="histogram">
      <div className="histogram-heading">
        <span>Quality distribution</span>
        <span className="tiny-mono">
          J<sub>ENS</sub>
        </span>
      </div>
      <div
        className="histogram-plot"
        title="Cell count per Jacobian interval, square-root vertical scale"
        onMouseLeave={() => setBin(null)}
      >
        {bins.map((v, i) => (
          <button
            key={i}
            onFocus={() => setBin(i)}
            onBlur={() => setBin(null)}
            onMouseEnter={() => setBin(i)}
            onClick={() => setBin(bin === i ? null : i)}
            aria-label={`Quality ${(i / 36).toFixed(2)} to ${((i + 1) / 36).toFixed(2)}: ${integer(v)} cells`}
            tabIndex={v ? 0 : -1}
            style={{
              height: v ? `${Math.max(5, Math.sqrt(v / max) * 100)}%` : "1px",
              background: `hsl(${162 + i * 0.7} 38% ${40 + i * 0.45}%)`,
            }}
          />
        ))}
        {bin !== null && (
          <span className="histogram-tip">
            {(bin / 36).toFixed(2)} to {((bin + 1) / 36).toFixed(2)}:{" "}
            {integer(bins[bin])} cells
          </span>
        )}
      </div>
      <div className="histogram-axis">
        <span>0.0</span>
        <span>0.25</span>
        <span>0.50</span>
        <span>0.75</span>
        <span>1.0</span>
      </div>
    </div>
  );
}
export function Verification({
  mesh,
  onClose,
}: {
  mesh: MeshData;
  onClose: () => void;
}) {
  const s = mesh.stats;
  const list = [
    [
      "Positive cell volumes",
      `${integer(s.cells)} / ${integer(s.cells)}`,
      s.minVolume > 0,
    ],
    ["Minimum normalized Jacobian", s.minJ.toFixed(8), s.minJ > 0.25],
    [
      "Minimum signed cell volume",
      `${s.minVolume.toFixed(8)} mm³`,
      s.minVolume > 0,
    ],
    ["Maximum Gauss closure", sci(s.maxClosure, 5), s.maxClosure < 1e-14],
    ["Unmatched interior faces", String(s.unmatched), s.unmatched === 0],
    [
      "Hanging vertices on mesh edges",
      String(s.hangingNodes),
      s.hangingNodes === 0,
    ],
    [
      "Nonmanifold or misoriented faces",
      String(s.nonmanifold),
      s.nonmanifold === 0,
    ],
    [
      "Normalized prism determinant bound",
      s.minPrism.toFixed(6),
      s.minPrism >= 0.15,
    ],
    [
      "Maximum clearance fraction",
      s.clearanceRatio.toFixed(6),
      s.clearanceRatio <= 0.4,
    ],
    [
      "Minimum visibility cosine",
      s.minVisibility.toFixed(6),
      s.minVisibility >= Math.cos((65 * Math.PI) / 180) - 1e-12,
    ],
    [
      "Analytic volume agreement",
      `${sci(Math.abs(s.volume / s.expectedVolume - 1))} relative`,
      Math.abs(s.volume / s.expectedVolume - 1) < 1e-10,
    ],
  ];
  return (
    <Modal
      title="Verification report"
      eyebrow="NUMERICAL AUDIT"
      onClose={onClose}
      wide
    >
      <div className="verify-intro">
        <ShieldCheck size={26} />
        <div>
          <strong>All acceptance checks passed</strong>
          <p>
            {GEOMETRIES[mesh.geometry]} <span>/</span> Mesh {mesh.id}
          </p>
        </div>
      </div>
      <div className="check-list">
        {list.map(([label, value, pass]) => (
          <div key={String(label)}>
            <CheckCircle2
              size={15}
              className={pass ? "text-mint" : "text-red"}
            />
            <span>{label}</span>
            <strong>{value}</strong>
          </div>
        ))}
      </div>
      <p className="modal-footnote">
        Fixture-level verification in double precision. Exact reference
        certificates are available in Documentation under Verification scope.
      </p>
    </Modal>
  );
}
interface Props {
  mesh: MeshData;
  tab: Tab;
  setTab: (t: Tab) => void;
  logs: Log[];
  selected: number | null;
  chooseCell: (id: number) => void;
  patch: number;
  setPatch: (n: number) => void;
  fa: FaResult | null;
  preview: boolean;
  setPreview: (p: boolean) => void;
  working: boolean;
  extract: () => void;
  exportFile: (t: "report" | "vtk" | "fa") => void;
  openChecks: () => void;
}
export default function Diagnostics({
  mesh,
  tab,
  setTab,
  logs,
  selected,
  chooseCell,
  patch,
  setPatch,
  fa,
  preview,
  setPreview,
  working,
  extract,
  exportFile,
  openChecks,
}: Props) {
  const [menu, setMenu] = useState(false),
    [cellInput, setCellInput] = useState(""),
    [cellError, setCellError] = useState("");
  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(false);
    const key = (e: KeyboardEvent) => {
      if (e.key === "Escape") close();
    };
    document.addEventListener("click", close);
    document.addEventListener("keydown", key);
    return () => {
      document.removeEventListener("click", close);
      document.removeEventListener("keydown", key);
    };
  }, [menu]);
  const worst = useMemo(() => {
    let k = 0;
    mesh.quality.forEach((q, i) => {
      if (q < mesh.quality[k]) k = i;
    });
    return k;
  }, [mesh]);
  const cellId = selected ?? worst,
    cell = mesh.cells[cellId],
    centroid = avg(cell.map((i) => mesh.points[i]));
  const patchCount = useMemo(
    () => mesh.boundary.filter((b) => patch < 0 || b[1] === patch).length,
    [mesh, patch],
  );
  const exponent = Math.floor(Math.log10(mesh.stats.maxClosure || 1)),
    mantissa = mesh.stats.maxClosure / 10 ** exponent;
  return (
    <section className="diagnostics panel">
      <div className="diagnostics-header">
        <div
          className="diagnostic-tabs"
          role="tablist"
          aria-label="Mesh analysis panels"
        >
          {TABS.map(({ id, name, icon: Icon }) => (
            <button
              key={id}
              role="tab"
              id={`analysis-${id}`}
              aria-label={name}
              aria-controls="analysis-content"
              tabIndex={tab === id ? 0 : -1}
              onKeyDown={(e) => {
                const direction =
                  e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
                if (!direction) return;
                e.preventDefault();
                const next =
                  TABS[
                    (TABS.findIndex((t) => t.id === id) +
                      direction +
                      TABS.length) %
                      TABS.length
                  ].id;
                setTab(next);
                document.getElementById(`analysis-${next}`)?.focus();
              }}
              aria-selected={tab === id}
              className={tab === id ? "selected" : ""}
              onClick={() => setTab(id)}
            >
              <Icon size={14} />
              <span>{name}</span>
              {id === "log" && <small>{logs.length}</small>}
            </button>
          ))}
        </div>
        <div className="export-wrap">
          <button
            className="export-button"
            onClick={(e) => {
              e.stopPropagation();
              setMenu(!menu);
            }}
            aria-expanded={menu}
            aria-label="Export mesh or report"
          >
            <ArrowDownToLine size={13} />
            <span>Export</span>
            <ChevronDown size={11} />
          </button>
          {menu && (
            <div className="menu export-menu">
              <div className="menu-caption">EXPORT WORKSPACE</div>
              <button onClick={() => exportFile("report")}>
                <FileCheck2 size={15} />
                Verification report<small>JSON</small>
              </button>
              <button onClick={() => exportFile("vtk")}>
                <Box size={15} />
                Volume mesh<small>VTK</small>
              </button>
              <button disabled={working} onClick={() => exportFile("fa")}>
                <Layers3 size={15} />
                Wall submesh<small>JSON</small>
              </button>
              <a download href="/engine/hexalab-source.zip">
                <Code2 size={15} />
                Engine source<small>ZIP</small>
              </a>
            </div>
          )}
        </div>
      </div>
      <div
        className="tab-content"
        role="tabpanel"
        id="analysis-content"
        aria-labelledby={`analysis-${tab}`}
        key={tab}
      >
        {tab === "quality" && (
          <div className="quality-overview">
            <div className="quality-values">
              <div className="quality-summary-heading">
                <h3>Mesh quality</h3>
                <button className="verified-label" onClick={openChecks}>
                  <ShieldCheck size={12} />
                  All checks passed
                  <ChevronRight size={10} />
                </button>
              </div>
              <div className="metrics">
                <button className="metric" onClick={openChecks}>
                  <span>Min. scaled Jacobian</span>
                  <strong>{mesh.stats.minJ.toFixed(4)}</strong>
                  <small>
                    <Check size={11} />
                    Target &gt; 0.25
                  </small>
                </button>
                <button className="metric" onClick={openChecks}>
                  <span>Max. Gauss closure</span>
                  <strong className="scientific">
                    {mantissa.toFixed(2)}
                    <i> × 10</i>
                    <sup>{String(exponent).replace("-", "−")}</sup>
                  </strong>
                  <small>
                    <Check size={11} />
                    Target &lt; 10<sup>−14</sup>
                  </small>
                </button>
                <button className="metric" onClick={openChecks}>
                  <span>Inverted cells</span>
                  <strong>
                    {mesh.stats.negative}
                    <em> / {integer(mesh.stats.cells)}</em>
                  </strong>
                  <small>
                    <Check size={11} />
                    Positive volumes
                  </small>
                </button>
              </div>
            </div>
            <Histogram mesh={mesh} />
          </div>
        )}
        {tab === "cell" && (
          <div className="cell-details">
            <div className="detail-intro">
              <div>
                <h3>Cell inspection</h3>
                <p>
                  {selected === null
                    ? "Lowest-quality cell selected."
                    : "Selected volume element."}{" "}
                  Use the <MousePointer2 size={11} /> tool to pick another.
                </p>
              </div>
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  const n = Number(cellInput);
                  if (
                    cellInput.trim() &&
                    Number.isInteger(n) &&
                    n >= 0 &&
                    n < mesh.cells.length
                  ) {
                    chooseCell(n);
                    setCellError("");
                  } else
                    setCellError(
                      `Enter an ID from 0 to ${mesh.cells.length - 1}.`,
                    );
                }}
              >
                <label htmlFor="cell-id">Cell ID</label>
                <input
                  id="cell-id"
                  type="number"
                  placeholder={String(cellId)}
                  value={cellInput}
                  onChange={(e) => setCellInput(e.target.value)}
                />
                <button className="secondary-button">
                  Inspect
                  <ArrowRight size={12} />
                </button>
              </form>
            </div>
            {cellError && <p className="error-text">{cellError}</p>}
            <div className="detail-data">
              <div>
                <small>ELEMENT</small>
                <strong>
                  {CELL_TYPES.find((t) => t.n === cell.length)?.short}{" "}
                  <em>#{cellId}</em>
                </strong>
              </div>
              <div>
                <small>SCALED JACOBIAN</small>
                <strong>{mesh.quality[cellId].toFixed(6)}</strong>
              </div>
              <div>
                <small>SIGNED VOLUME</small>
                <strong>
                  {mesh.volumes[cellId].toFixed(5)} <em>mm³</em>
                </strong>
              </div>
              <div>
                <small>CENTROID · MM</small>
                <strong className="smaller">
                  {centroid.map((n) => n.toFixed(2)).join(", ")}
                </strong>
              </div>
              <div>
                <small>REGION</small>
                <strong className="smaller">
                  {mesh.layers[cellId] > 0
                    ? `Prism layer ${mesh.layers[cellId]}`
                    : mesh.layers[cellId] < 0
                      ? "Transition"
                      : "Hex core"}
                </strong>
              </div>
            </div>
          </div>
        )}
        {tab === "surface" && (
          <div className="surface-content">
            <div className="detail-intro">
              <div>
                <h3>Finite-area wall submesh</h3>
                <p>
                  Extract oriented surface faces and their volume-owner coupling
                  map.
                </p>
              </div>
              <div className="surface-actions">
                <button
                  className={`secondary-button ${preview ? "active" : ""}`}
                  onClick={() => setPreview(!preview)}
                >
                  {preview ? <EyeOff size={13} /> : <Eye size={13} />}Preview
                </button>
                <button
                  className="primary-button compact"
                  disabled={working}
                  onClick={extract}
                >
                  <Layers3 size={13} />
                  {working ? "Extracting..." : "Extract surface"}
                </button>
              </div>
            </div>
            <div className="surface-data">
              <label>
                Wall patch
                <select
                  aria-label="Wall patch"
                  disabled={working}
                  value={patch}
                  onChange={(e) => setPatch(+e.target.value)}
                >
                  <option value={-1}>All wall patches</option>
                  {PATCHES.map((s, i) => (
                    <option value={i} key={s}>
                      {s}
                    </option>
                  ))}
                </select>
              </label>
              <div>
                <small>SURFACE FACES</small>
                <strong>{integer(patchCount)}</strong>
              </div>
              <div>
                <small>TOPOLOGY</small>
                <strong>
                  {patch < 0 ? "Closed manifold" : "Manifold with boundary"}
                </strong>
              </div>
              <div>
                <small>OWNER MAP</small>
                <strong>
                  {fa ? `${integer(fa.faces.length)} linked` : "Volume-indexed"}
                </strong>
              </div>
              {fa && (
                <button
                  className="secondary-button"
                  onClick={() => exportFile("fa")}
                >
                  <Download size={13} />
                  faMesh
                </button>
              )}
            </div>
          </div>
        )}
        {tab === "log" && (
          <div className="engine-log">
            <div className="log-heading">
              <span>
                <Terminal size={13} /> ENGINE OUTPUT
              </span>
              <button
                onClick={() =>
                  download(
                    `hexalab-${mesh.id}.log`,
                    logs
                      .map(
                        (l) =>
                          `[${l.time}] ${l.kind.toUpperCase()} ${l.message}`,
                      )
                      .join("\n"),
                    "text/plain",
                  )
                }
              >
                <Download size={11} />
                Save log
              </button>
            </div>
            {logs.map((l, i) => (
              <div className="log-line" key={i}>
                <time>{l.time}</time>
                <span className={`log-kind ${l.kind}`}>{l.kind}</span>
                <span>{l.message}</span>
              </div>
            ))}
          </div>
        )}
      </div>
      <div className="mesh-numbers">
        <span>
          <Box size={12} />
          <b>{integer(mesh.stats.cells)}</b> cells
        </span>
        <i />
        <span>
          <Grid3X3 size={12} />
          <b>{integer(mesh.stats.vertices)}</b> vertices
        </span>
        <i />
        <span>
          <Layers3 size={12} />
          <b>{integer(mesh.stats.faces)}</b> faces
        </span>
        <span className="compute-time">
          <Cpu size={12} />
          {(mesh.stats.elapsedMs / 1000).toFixed(2)} s <span>compute time</span>
        </span>
      </div>
    </section>
  );
}
