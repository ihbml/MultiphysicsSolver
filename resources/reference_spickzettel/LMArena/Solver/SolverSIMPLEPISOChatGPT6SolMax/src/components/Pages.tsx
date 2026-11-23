import { useMemo, useState, type CSSProperties, type ReactNode } from "react";
import {
  ArrowDownToLine, ArrowRight, Check, ChevronRight, Clipboard, Code2, Cpu,
  Play, Settings2, Sigma, Terminal,
} from "lucide-react";
import reference from "../../public/verification.json";
import continuumSource from "../../native/include/continuum.hpp?raw";
import linearSource from "../../native/include/linear.hpp?raw";
import chemistrySource from "../../native/include/chemistry.hpp?raw";
import adjointSource from "../../native/include/adjoint.hpp?raw";
import deviceSource from "../../native/include/device.cuh?raw";
import generatedSource from "../../native/include/generated/chain_abc.hpp?raw";
import cudaSource from "../../native/cuda/continuum_cuda.cu?raw";
import enzymeSource from "../../native/enzyme_bridge.cpp?raw";
import testSource from "../../native/tests/verify.cpp?raw";
import generatorSource from "../../native/tools/generate_mechanism.py?raw";
import mechanismSource from "../../native/mechanisms/chain.json?raw";
import readmeSource from "../../native/README.md?raw";
import type { AdjointResult, AMGResult, ChemistryResult, FluxInput, FluxResult } from "../engine/verification";

export type Page = "flux" | "pressure" | "amg" | "chemistry" | "adjoint" | "source";
const sources = [
  { name: "continuum.hpp", path: "native/include/continuum.hpp", content: continuumSource, language: "C++20" },
  { name: "linear.hpp", path: "native/include/linear.hpp", content: linearSource, language: "C++20" },
  { name: "chemistry.hpp", path: "native/include/chemistry.hpp", content: chemistrySource, language: "C++20" },
  { name: "adjoint.hpp", path: "native/include/adjoint.hpp", content: adjointSource, language: "C++20" },
  { name: "device.cuh", path: "native/include/device.cuh", content: deviceSource, language: "CUDA" },
  { name: "continuum_cuda.cu", path: "native/cuda/continuum_cuda.cu", content: cudaSource, language: "CUDA" },
  { name: "chain_abc.hpp", path: "native/include/generated/chain_abc.hpp", content: generatedSource, language: "AoT C++" },
  { name: "enzyme_bridge.cpp", path: "native/enzyme_bridge.cpp", content: enzymeSource, language: "C++20" },
  { name: "verify.cpp", path: "native/tests/verify.cpp", content: testSource, language: "C++20" },
  { name: "generate_mechanism.py", path: "native/tools/generate_mechanism.py", content: generatorSource, language: "Python" },
  { name: "chain.json", path: "native/mechanisms/chain.json", content: mechanismSource, language: "JSON" },
  { name: "README.md", path: "native/README.md", content: readmeSource, language: "Markdown" },
];

export function scientific(value: number, digits = 2) {
  return value.toExponential(digits).replace("e-0", "e-").replace("e+0", "e+");
}
export function saveFile(name: string, content: string, mime = "text/plain") {
  const url = URL.createObjectURL(new Blob([content], { type: mime }));
  const link = document.createElement("a");
  link.href = url; link.download = name; link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 2000);
}
function Eyebrow({ children }: { children: ReactNode }) {
  return <div className="eyebrow"><span className="eyebrow-line" />{children}</div>;
}
function SectionHeading({ index, title, description, action }: {
  index: string; title: ReactNode; description: string; action?: ReactNode;
}) {
  return <div className="section-heading"><div className="section-heading-copy"><Eyebrow>FIELD NOTE {index} / INTERACTIVE VERIFICATION</Eyebrow><h1>{title}</h1><p>{description}</p></div>{action && <div className="heading-action">{action}</div>}</div>;
}
function ControlSlider({ label, value, min, max, step, display, onChange, detail }: {
  label: string; value: number; min: number; max: number; step: number;
  display?: string; onChange: (value: number) => void; detail?: string;
}) {
  const fill = ((value - min) / (max - min)) * 100;
  return <label className="control-slider"><span className="control-top"><span>{label}</span><strong>{display ?? value.toFixed(2)}</strong></span>
    <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number(event.target.value))}
      style={{ "--fill": `${fill}%` } as CSSProperties} aria-label={label} />
    {detail && <span className="control-detail">{detail}</span>}</label>;
}
function Metric({ label, value, sub }: { label: string; value: string; sub?: string }) {
  return <div className="metric-row"><span className="metric-label">{label}</span><span className="metric-value">{value}</span>{sub && <span className="metric-sub">{sub}</span>}</div>;
}

function MeshDiagram({ input, result, replay }: { input: FluxInput; result: FluxResult; replay: number }) {
  const faceY = 175 + input.skew * 105;
  return <div className="mesh-diagram"><svg viewBox="0 0 800 340" role="img" aria-label="Skewed finite volume cells showing owner, neighbor, face centroid and face normal">
    <defs><pattern id="mesh-grid" width="27" height="27" patternUnits="userSpaceOnUse"><path d="M 27 0 L 0 0 0 27" fill="none" stroke="#27403c" strokeWidth="0.55" opacity="0.34" /></pattern>
      <linearGradient id="mesh-left" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stopColor="#1d3c38" stopOpacity="0.58" /><stop offset="1" stopColor="#17322f" stopOpacity="0.18" /></linearGradient>
      <linearGradient id="mesh-right" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stopColor="#273738" stopOpacity="0.4" /><stop offset="1" stopColor="#17282b" stopOpacity="0.14" /></linearGradient>
      <marker id="arrow-green" markerWidth="8" markerHeight="8" refX="6" refY="4" orient="auto"><path d="M1 1L7 4L1 7" fill="none" stroke="#b4f1c9" strokeWidth="1.5" /></marker>
      <marker id="arrow-soft" markerWidth="8" markerHeight="8" refX="6" refY="4" orient="auto"><path d="M1 1L7 4L1 7" fill="none" stroke="#91aba8" strokeWidth="1.5" /></marker></defs>
    <rect width="800" height="340" fill="#101c1d" /><rect width="800" height="340" fill="url(#mesh-grid)" />
    <path d="M55 63 L355 52 L341 288 L61 281 Z" fill="url(#mesh-left)" stroke="#608b7e" strokeWidth="1.1" />
    <path d="M355 52 L734 65 L741 278 L341 288 Z" fill="url(#mesh-right)" stroke="#4a6864" strokeWidth="1.1" />
    <path d="M188 173 L543 169" stroke="#88a5a0" strokeDasharray="5 7" strokeWidth="1.2" markerEnd="url(#arrow-soft)" />
    <path d={`M348 ${faceY} L460 ${faceY - 42}`} stroke="#b4f1c9" strokeWidth="2" markerEnd="url(#arrow-green)" />
    <path d="M355 52 L341 288" stroke="#aef2c5" strokeWidth="2.5" />
    <g key={replay}><circle r="3.4" fill="#c9ffd9" opacity="0"><animateMotion dur="3.2s" repeatCount="indefinite" path="M188 173 L543 169" /><animate attributeName="opacity" values="0;1;1;0" dur="3.2s" repeatCount="indefinite" /></circle>
      <circle r="2.6" fill="#c9ffd9" opacity="0"><animateMotion dur="3.2s" begin="1.05s" repeatCount="indefinite" path="M188 173 L543 169" /><animate attributeName="opacity" values="0;0;1;0" dur="3.2s" repeatCount="indefinite" /></circle></g>
    <circle cx="188" cy="173" r="21" fill="#b6f4c9" opacity="0.055" /><circle cx="543" cy="169" r="21" fill="#a8c6bf" opacity="0.07" />
    <circle cx="188" cy="173" r="5" fill="#c0fbd4" /><circle cx="543" cy="169" r="5" fill="#a3bdb7" /><circle cx="348" cy={faceY} r="6" fill="#b4f1c9" stroke="#101c1d" strokeWidth="2" />
    <text x="164" y="151" className="svg-small">OWNER</text><text x="180" y="210" className="svg-title">P</text>
    <text x="515" y="146" className="svg-small">NEIGHBOR</text><text x="537" y="209" className="svg-title">N</text>
    <text x="361" y={faceY - 12} className="svg-label">x_f</text><text x="451" y={faceY - 54} className="svg-label green">S_f</text><text x="363" y="157" className="svg-small">d_f</text>
    <text x="78" y="97" className="svg-note">COLLOCATED CELL CENTER</text><text x="579" y="95" className="svg-note">FACE-NORMAL FLUX</text>
    <text x="74" y="305" className="svg-note">p_P = {input.slope.toFixed(2)} x_P + delta_p</text><text x="528" y="305" className="svg-note">m_f = rho u_f . S_f + RC</text>
  </svg><div className="diagram-caption"><span><span className="caption-dot green-dot" />PLANE INTERSECTION <b>beta_f = {result.beta.toFixed(4)}</b></span><span><span className="caption-dot muted-dot" />OVER-RELAXED <b>alpha_f = {result.alpha.toFixed(4)}</b></span></div></div>;
}

function PartitionStrip() {
  return <div className="partition-strip"><div className="partition-top"><span>4-QUADRANT HILBERT FACE ORDER</span><span>COMPACT CORRECTION: 2 / 33 FACES</span></div>
    <div className="partition-bars" aria-label="Inner fast, inner corrected, halo fast, halo corrected segments">
      <div className="partition-seg seg-fast" style={{ flex: 19 }}><span>00</span><b>inner_fast</b></div>
      <div className="partition-seg seg-correct" style={{ flex: 1 }}><span>01</span></div>
      <div className="partition-seg seg-halo" style={{ flex: 12 }}><span>02</span><b>halo_fast</b></div>
      <div className="partition-seg seg-correct" style={{ flex: 1 }}><span>03</span></div>
    </div><div className="partition-foot"><span>CLASS 0: NO GRADIENT STORAGE</span><span>CLASS 1: sx / sy / sz / kx / ky / kz / omega</span></div></div>;
}

export function FluxPage({ input, setInput, result, replay, onRun, onNavigate }: {
  input: FluxInput; setInput: (value: FluxInput) => void; result: FluxResult;
  replay: number; onRun: () => void; onNavigate: (page: Page) => void;
}) {
  const update = (key: keyof FluxInput) => (value: number) => setInput({ ...input, [key]: value });
  return <><SectionHeading index="01" title={<>The face is <em>the proof.</em></>}
    description="Perturb a collocated face. The linear pressure mode must cancel; the checkerboard mode must not."
    action={<button className="button button-primary" onClick={onRun}><Play size={14} fill="currentColor" /> Run reconstruction</button>} />
    <div className="hero-workspace"><div className="diagram-panel"><div className="panel-topline"><span><span className="live-pulse" /> LIVE GEOMETRY / FACE 002</span><span>OWNER TO NEIGHBOR <ArrowRight size={13} /></span></div>
      <MeshDiagram input={input} result={result} replay={replay} /><PartitionStrip /></div>
      <div className="control-panel"><div className="control-panel-header"><Settings2 size={17} /><span>INPUT PARAMETERS</span><span className="panel-index">01 / 05</span></div>
        <div className="controls-stack"><ControlSlider label="Pressure slope" value={input.slope} min={-2} max={3} step={0.05} onChange={update("slope")} display={input.slope.toFixed(2)} detail="dp / dx" />
          <ControlSlider label="Face skew" value={input.skew} min={0} max={0.35} step={0.01} onChange={update("skew")} display={input.skew.toFixed(2)} detail="centroid offset / cell width" />
          <ControlSlider label="Checkerboard mode" value={input.checkerboard} min={0} max={0.8} step={0.01} onChange={update("checkerboard")} display={input.checkerboard.toFixed(2)} detail="alternating cell pressure" />
          <ControlSlider label="Mobility D" value={input.mobility} min={0.02} max={0.35} step={0.01} onChange={update("mobility")} display={input.mobility.toFixed(2)} detail="V / a_P" /></div>
        <div className="control-result"><span>LINEAR PRESSURE DEFECT</span><strong>{scientific(result.linearError)}</strong><span className="result-condition"><Check size={13} /> EXACT TO FP64 ROUNDING</span></div></div></div>
    <div className="below-grid"><div className="method-panel"><div className="block-heading"><span>01 / DISCRETE FLUX</span><button className="text-link" onClick={() => onNavigate("source")}>Inspect C++ kernel <ArrowRight size={14} /></button></div>
      <div className="formula">m_f = rho_f [u_bar . S_f - D_bar alpha_f (p_N - p_P - grad(p)_bar . d_f)]</div>
      <p>Plane-intersection beta_f and over-relaxed alpha_f use the actual face plane. Class 0 reads no correction arrays.</p></div>
      <div className="results-panel"><div className="block-heading"><span>LIVE BROWSER CHECKS</span><span className="tiny-pass"><Check size={12} /> PASS</span></div>
        <Metric label="Checkerboard response" value={scientific(result.checkerboardResponse)} sub="nonzero when perturbed" />
        <Metric label="Gradient recovery" value={scientific(result.gradientError)} sub="linear manufactured field" />
        <Metric label="Post-projection div(m)" value={scientific(result.maxDivergence)} sub="five owned cells" /></div></div>
    <NativeLedger onNavigate={onNavigate} /></>;
}

function NativeLedger({ onNavigate }: { onNavigate: (page: Page) => void }) {
  const rows = [
    { test: "Rhie-Chow linear exactness", threshold: "< 1e-13", actual: scientific(reference.face.linearError), section: "flux" as Page },
    { test: "Discrete mass conservation", threshold: "< 1e-11", actual: scientific(reference.face.massError), section: "pressure" as Page },
    { test: "FP64 backward error", threshold: "< 1e-10", actual: scientific(reference.linear.etaRound), section: "amg" as Page },
    { test: "Adjoint / central difference", threshold: "< 1e-8", actual: scientific(reference.adjoint.fdError), section: "adjoint" as Page },
  ];
  return <section className="native-ledger"><div className="ledger-header"><div><Eyebrow>EXECUTABLE REFERENCE / G++ -STD=C++20 -O2</Eyebrow><h2>Verified in native code.</h2></div><button className="text-link" onClick={() => onNavigate("source")}>Open verification test <ArrowRight size={14} /></button></div>
    <div className="ledger-table"><div className="ledger-thead"><span>ASSERTION</span><span>TOLERANCE</span><span>MEASURED</span><span>RESULT</span></div>
      {rows.map((row) => <button className="ledger-row" key={row.test} onClick={() => onNavigate(row.section)}><span>{row.test}</span><span>{row.threshold}</span><strong>{row.actual}</strong><span className="pass-cell"><Check size={13} /> PASS <ChevronRight size={14} /></span></button>)}
    </div></section>;
}

function DivergencePlot({ result }: { result: FluxResult }) {
  const max = Math.max(0.12, ...result.before.map(Math.abs));
  return <div className="divergence-plot"><div className="plot-head"><span>CELLFACECSR / OWNED-CELL GATHER</span><span>kg s^-1</span></div>
    <div className="plot-legend"><span><i className="legend-swatch before" /> predicted</span><span><i className="legend-swatch after" /> corrected</span></div>
    <div className="bar-chart"><div className="zero-line" />{result.before.map((value, i) => {
      const h = Math.max(2, Math.abs(value) / max * 92);
      return <div className="bar-column" key={i}><div className="bar-above">{value >= 0 && <div className="bar bar-before" style={{ height: h }} />}</div><div className="bar-below">{value < 0 && <div className="bar bar-before" style={{ height: h }} />}</div><span>P{i}</span></div>;
    })}</div><div className="plot-annotation"><span>max |div(m*)| = {scientific(Math.max(...result.before.map(Math.abs)))}</span><span>max |div(m)| = {scientific(result.maxDivergence)}</span></div></div>;
}

export function PressurePage({ input, setInput, result, onNavigate }: {
  input: FluxInput; setInput: (value: FluxInput) => void; result: FluxResult; onNavigate: (page: Page) => void;
}) {
  const [stage, setStage] = useState(1);
  const stages = [
    { label: "MOMENTUM PREDICTOR", equation: "a_P u_P* = sum(a_N u_N) + V (f - grad p)", detail: "Continuity-defect stabilization is added to the momentum diagonal." },
    { label: "PRESSURE POISSON", equation: "A p' = -div(m*) + div(rho D k . grad p')", detail: "An FP32 AMG V-cycle preconditions the FP64 outer solve." },
    { label: "FLUX + STATE UPDATE", equation: "m_f <- m_f* + g_f (p'_P - p'_N)", detail: "CellFaceCSR gathers corrected face fluxes without a scatter or atomic add." },
  ];
  return <><SectionHeading index="02" title={<>Close the <em>continuity gap.</em></>}
    description="Solve the pressure-correction equation against the exact face imbalance, then inspect every owned cell." />
    <div className="pressure-main"><div className="pressure-visual"><div className="panel-topline"><span><span className="live-pulse" /> PROJECTION / 05 OWNED CELLS</span><span>SIMPLE / PISO / PIMPLE</span></div><DivergencePlot result={result} />
      <div className="pressure-cells"><div><span>CELL</span><span>div(m*)</span><span>p'</span><span>div(m)</span></div>
        {result.before.map((v, i) => <div key={i}><strong>P{i}</strong><span>{scientific(v)}</span><span>{result.pressureCorrection[i].toFixed(4)}</span><strong className="positive">{scientific(result.after[i])}</strong></div>)}</div></div>
      <div className="pressure-side"><div className="control-panel-header"><Settings2 size={17} /><span>EXPERIMENT</span><span className="panel-index">02 / 05</span></div><div className="controls-stack">
        <ControlSlider label="Checkerboard amplitude" value={input.checkerboard} min={0} max={0.8} step={0.01} onChange={(v) => setInput({ ...input, checkerboard: v })} />
        <ControlSlider label="Momentum mobility" value={input.mobility} min={0.02} max={0.35} step={0.01} onChange={(v) => setInput({ ...input, mobility: v })} />
        <ControlSlider label="Predicted velocity" value={input.velocity} min={0.1} max={1.8} step={0.05} onChange={(v) => setInput({ ...input, velocity: v })} />
      </div><div className="side-rule" /><Metric label="Conservation error" value={scientific(result.maxDivergence)} sub="maximum over owned cells" /><Metric label="Matrix-free vs CSR" value={scientific(reference.face.matrixFreeError)} sub="native executable" /><Metric label="Native PISO residual" value={scientific(reference.face.pisoMassError)} sub="two pressure solves" /></div></div>
    <div className="stage-section"><div className="block-heading"><span>THREE-STEP PRESSURE CORRECTION</span><button className="text-link" onClick={() => onNavigate("source")}>Read pressure_matrix() <ArrowRight size={14} /></button></div>
      <div className="stage-tabs">{stages.map((item, i) => <button key={item.label} className={stage === i ? "active" : ""} onClick={() => setStage(i)}><span>0{i + 1}</span>{item.label}<ChevronRight size={15} /></button>)}</div>
      <div className="stage-detail"><code>{stages[stage].equation}</code><p>{stages[stage].detail}</p></div></div></>;
}

function ResidualChart({ live, native }: { live: number[]; native: number[] }) {
  const height = 280, width = 770, left = 55, right = 742, top = 24, bottom = 227;
  const maxX = Math.max(live.length, native.length) - 1;
  const y = (value: number) => top + (Math.max(0, Math.min(12, -Math.log10(Math.max(value, 1e-14)))) / 12) * (bottom - top);
  const path = (data: number[]) => data.map((v, i) => `${i ? "L" : "M"} ${left + i / maxX * (right - left)} ${y(v)}`).join(" ");
  return <div className="chart-wrap"><svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label="Log residual trace of native and browser mixed-precision iterative refinement">
    {[0, 2, 4, 6, 8, 10, 12].map((tick) => <g key={tick}><line x1={left} y1={top + tick / 12 * (bottom - top)} x2={right} y2={top + tick / 12 * (bottom - top)} stroke="#273638" strokeDasharray="3 6" /><text x="9" y={top + tick / 12 * (bottom - top) + 4} className="chart-label">1e-{tick}</text></g>)}
    <path d={path(native)} fill="none" stroke="#78938c" strokeWidth="2" strokeDasharray="5 6" />
    <path className="chart-line-draw" d={path(live)} fill="none" stroke="#b8f4cd" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />
    <circle cx={left + (live.length - 1) / maxX * (right - left)} cy={y(live[live.length - 1])} r="5" fill="#b8f4cd" />
    <text x={left} y="261" className="chart-label">0</text><text x={right - 52} y="261" className="chart-label">ITERATION</text>
  </svg><div className="plot-legend chart-legend"><span><i className="legend-swatch after" /> browser FP32 / FP64</span><span><i className="legend-swatch native" /> native reference</span></div></div>;
}

export function AMGPage({ result, grid, setGrid, coupling, setCoupling, onNavigate }: {
  result: AMGResult; grid: number; setGrid: (n: number) => void; coupling: number; setCoupling: (n: number) => void; onNavigate: (page: Page) => void;
}) {
  return <><SectionHeading index="03" title={<>Precision where <em>it matters.</em></>}
    description="Refine the true FP64 residual while a parallel FP32 aggregation V-cycle handles the inner work." />
    <div className="analysis-main"><div className="analysis-chart"><div className="panel-topline"><span><span className="live-pulse" /> RESIDUAL HISTORY / LOG SCALE</span><span>IR + AMG V-CYCLE</span></div><ResidualChart live={result.history} native={reference.linear.history} /></div>
      <div className="analysis-controls"><div className="control-panel-header"><Settings2 size={17} /><span>OPERATOR</span><span className="panel-index">03 / 05</span></div><div className="controls-stack">
        <ControlSlider label="Owned cells" value={grid} min={24} max={96} step={8} onChange={setGrid} display={`${grid}`} detail="1D SPD Poisson system" />
        <ControlSlider label="Face coupling" value={coupling} min={0.3} max={2} step={0.1} onChange={setCoupling} display={coupling.toFixed(1)} detail="off-diagonal conductance" />
      </div><div className="control-result"><span>TRUE FP64 BACKWARD ERROR</span><strong>{scientific(result.etaRound)}</strong><span className="result-condition"><Check size={13} /> {result.converged ? "CONVERGED" : "CHECK STAGNATION"}</span></div></div></div>
    <div className="solver-bottom"><div className="solver-explain"><div className="block-heading"><span>ALGEBRAIC HIERARCHY</span><button className="text-link" onClick={() => onNavigate("source")}>Inspect linear.hpp <ArrowRight size={14} /></button></div>
      <div className="hierarchy"><span>FINE <b>FP32</b><small>{grid} cells</small></span><i /><span>AGGREGATE <b>P^T A P</b><small>{result.coarse} coarse rows</small></span><i /><span>OUTER <b>FP64</b><small>{result.history.length - 1} refinements</small></span></div>
      <p>Native: deterministic PMIS coarsening, optional smoothed aggregation, Chebyshev or L1-Jacobi, and FP64 IR / BiCGStab / FGMRES.</p></div>
      <div className="results-panel solver-metrics"><div className="block-heading"><span>ROUNDING / STAGNATION</span><span className="tiny-pass"><Check size={12} /> VERIFIED</span></div>
        <Metric label="FP32 inner floor" value={scientific(result.fp32Floor)} sub="predicted from operator norm" /><Metric label="FP64 a-posteriori" value={scientific(result.etaRound)} sub="true residual / normwise scale" /><Metric label="Native hierarchy" value={`${reference.linear.levels} levels`} sub={`${reference.linear.irIterations} IR iterations`} /></div></div></>;
}

function SpeciesChart({ result, dt }: { result: ChemistryResult; dt: number }) {
  const width = 760, height = 280, left = 43, right = 730, top = 20, bottom = 226;
  const path = (key: "a" | "b" | "c") => result.trajectory.map((point, i) =>
    `${i ? "L" : "M"} ${left + point.time / dt * (right - left)} ${bottom - Math.max(0, Math.min(1, point[key])) * (bottom - top)}`).join(" ");
  return <div className="chart-wrap"><svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label="Stiff BDF2 species A, B, C concentration trajectory">
    {[0, 0.25, 0.5, 0.75, 1].map((tick) => <g key={tick}><line x1={left} x2={right} y1={bottom - tick * (bottom - top)} y2={bottom - tick * (bottom - top)} stroke="#273638" strokeDasharray="3 6" /><text x="9" y={bottom - tick * (bottom - top) + 4} className="chart-label">{tick.toFixed(tick === 0 || tick === 1 ? 0 : 2)}</text></g>)}
    <path className="chart-line-draw" d={path("a")} fill="none" stroke="#b8f4cd" strokeWidth="2.5" /><path className="chart-line-draw" d={path("b")} fill="none" stroke="#e9b881" strokeWidth="2.5" /><path className="chart-line-draw" d={path("c")} fill="none" stroke="#9ebcc5" strokeWidth="2.5" />
    <text x={left} y="261" className="chart-label">0</text><text x={right - 74} y="261" className="chart-label">t = {dt.toFixed(3)} s</text>
  </svg><div className="plot-legend chart-legend"><span><i className="legend-swatch species-a" /> species A</span><span><i className="legend-swatch species-b" /> species B</span><span><i className="legend-swatch species-c" /> species C</span></div></div>;
}

export function ChemistryPage({ result, temp, setTemp, dt, setDt, hot, setHot, onNavigate }: {
  result: ChemistryResult; temp: number; setTemp: (n: number) => void; dt: number; setDt: (n: number) => void;
  hot: number; setHot: (n: number) => void; onNavigate: (page: Page) => void;
}) {
  return <><SectionHeading index="04" title={<>Only the active cells <em>burn.</em></>}
    description="Compact the reaction queue first. Persistent workers integrate stiff cells; cold-flow cells are left untouched." />
    <div className="analysis-main"><div className="analysis-chart"><div className="panel-topline"><span><span className="live-pulse" /> AoT JACOBIAN / BDF2 TRAJECTORY</span><span>A TO B TO C</span></div><SpeciesChart result={result} dt={dt} /></div>
      <div className="analysis-controls"><div className="control-panel-header"><Settings2 size={17} /><span>REACTION BATCH</span><span className="panel-index">04 / 05</span></div><div className="controls-stack">
        <ControlSlider label="Flame temperature" value={temp} min={800} max={1300} step={25} onChange={setTemp} display={`${temp} K`} />
        <ControlSlider label="Chemistry time step" value={dt} min={0.005} max={0.05} step={0.005} onChange={setDt} display={`${dt.toFixed(3)} s`} />
        <ControlSlider label="Hot cells" value={hot} min={1} max={12} step={1} onChange={setHot} display={`${hot} / 32`} />
      </div><div className="control-result"><span>CONSERVATION / SUM(Y_k) - 1</span><strong>{scientific(result.massError)}</strong><span className="result-condition"><Check size={13} /> POSITIVE, MASS CLOSED</span></div></div></div>
    <div className="chem-bottom"><div className="queue-panel"><div className="block-heading"><span>ACTIVE-CELL COMPACTION QUEUE</span><span className="queue-count">{result.active} ACTIVE / {result.idle} IDLE</span></div><div className="cell-grid">{Array.from({ length: 32 }, (_, i) => <div key={i} className={i < result.active ? "active" : ""} title={`Cell ${i}: ${i < result.active ? "reaction active" : "cold-flow idle"}`}><span>{String(i).padStart(2, "0")}</span></div>)}</div><p>Flags are prefix-compacted before the atomic fetch-add work queue. Only active cells enter the BDF2 kernel.</p></div>
      <div className="results-panel chem-metrics"><div className="block-heading"><span>STIFF INTEGRATOR</span><button className="text-link" onClick={() => onNavigate("source")}>Inspect chemistry.hpp <ArrowRight size={14} /></button></div>
        <Metric label="Hot reaction rate" value={`${result.rates[0].toFixed(1)} s^-1`} sub="Arrhenius A to B" /><Metric label="Adaptive substeps" value={`${result.substeps}`} sub="BE bootstrap, then BDF2" /><Metric label="Analytical J / FD" value={scientific(result.jacobianError)} sub="sparse AoT derivative" /><Metric label="Native queued cells" value={`${reference.chemistry.active} / 32`} sub="g++ OpenMP compatible" /></div></div></>;
}

export function AdjointPage({ result, gamma, setGamma, faceX, setFaceX, normal, setNormal, onNavigate }: {
  result: AdjointResult; gamma: number; setGamma: (n: number) => void;
  faceX: number; setFaceX: (n: number) => void; normal: number; setNormal: (n: number) => void; onNavigate: (page: Page) => void;
}) {
  return <><SectionHeading index="05" title={<>Differentiate the <em>solved state.</em></>}
    description="Solve the transposed discrete pressure-velocity system, then compare IFT sensitivities against re-solved central differences." />
    <div className="adjoint-equation"><div><span>DISCRETE IFT SYSTEM</span><strong>(dR / dU)<sup>T</sup> lambda = -(dJ / dU)<sup>T</sup></strong></div><div><span>DESIGN CHAIN RULE</span><strong>dJ / dtheta = J_theta + lambda<sup>T</sup> R_theta</strong></div><div className="equation-ornament"><Sigma size={42} strokeWidth={1} /></div></div>
    <div className="adjoint-main"><div className="adjoint-table-panel"><div className="panel-topline"><span><span className="live-pulse" /> TRANSPOSE SOLVE / 03 DESIGN VARIABLES</span><span>h = 4e-5</span></div>
      <div className="sensitivity-table"><div className="sensitivity-head"><span>DESIGN PARAMETER</span><span>IFT ADJOINT</span><span>CENTRAL FD</span><span>ABS ERROR</span></div>
        {result.comparisons.map((row, i) => <div className="sensitivity-row" key={row.name}><span><b>0{i + 1}</b>{row.name}</span><strong>{scientific(row.adjoint, 5)}</strong><span>{scientific(row.finiteDifference, 5)}</span><em>{scientific(row.error)}</em></div>)}</div>
      <div className="adjoint-conclusion"><div><span>MAX GRADIENT DISAGREEMENT</span><strong>{scientific(result.fdError)}</strong></div><span className="result-condition"><Check size={13} /> BELOW 1e-8</span></div></div>
      <div className="analysis-controls"><div className="control-panel-header"><Settings2 size={17} /><span>DESIGN VARIABLES</span><span className="panel-index">05 / 05</span></div><div className="controls-stack">
        <ControlSlider label="Cell porosity gamma_1" value={gamma} min={0.45} max={0.95} step={0.01} onChange={setGamma} display={gamma.toFixed(2)} detail="SIMP Brinkman exponent: 3" />
        <ControlSlider label="Outlet centroid x_f" value={faceX} min={2.85} max={3.2} step={0.005} onChange={setFaceX} display={faceX.toFixed(3)} detail="CAD boundary point" />
        <ControlSlider label="Outlet normal S_fx" value={normal} min={0.8} max={1.25} step={0.01} onChange={setNormal} display={normal.toFixed(2)} detail="oriented face area" />
      </div><div className="control-result"><span>CAD DIRECTIONAL SENSITIVITY</span><strong>{scientific(result.cadGradient)}</strong><span className="result-condition">0.2 dgamma + 0.3 dx_f - 0.1 dS_f</span></div></div></div>
    <div className="below-grid"><div className="method-panel"><div className="block-heading"><span>IFT / SIMP / CAD BRIDGE</span><button className="text-link" onClick={() => onNavigate("source")}>Inspect adjoint.hpp <ArrowRight size={14} /></button></div><div className="formula">dJ / dgamma_i = J_gamma_i + lambda<sup>T</sup> R_gamma_i</div><p>Forward duals differentiate the discrete residual. A transposed sparse Jacobian solves for lambda; the CAD chain contracts centroid and normal sensitivities.</p></div>
      <div className="results-panel"><div className="block-heading"><span>STATE EQUATION CHECK</span><span className="tiny-pass"><Check size={12} /> VERIFIED</span></div><Metric label="Primal max |R|" value={scientific(result.stateResidual)} sub="browser twin" /><Metric label="Native FD agreement" value={scientific(reference.adjoint.fdError)} sub="five independent parameters" /><Metric label="Objective J" value={result.value.toFixed(7)} sub="solved pressure-velocity state" /></div></div></>;
}

export function SourcePage() {
  const [selected, setSelected] = useState(0);
  const [copied, setCopied] = useState(false);
  const file = sources[selected];
  const lines = useMemo(() => file.content.split("\n"), [file.content]);
  const copy = async () => {
    try { await navigator.clipboard.writeText(file.content); setCopied(true); window.setTimeout(() => setCopied(false), 1800); }
    catch { saveFile(file.name, file.content); }
  };
  return <><SectionHeading index="06" title={<>Inspect the <em>actual kernels.</em></>}
    description="The native headers, CUDA kernels, AoT generator, Enzyme bridge, and executable verification are embedded here verbatim."
    action={<button className="button button-secondary" onClick={() => saveFile("continuum-verification.json", JSON.stringify(reference, null, 2), "application/json")}><ArrowDownToLine size={15} /> Native results</button>} />
    <div className="source-shell"><div className="file-sidebar"><div className="file-sidebar-title"><Code2 size={15} /> SOURCE TREE <span>{sources.length} FILES</span></div>{sources.map((item, i) => <button key={item.path} className={selected === i ? "selected" : ""} onClick={() => setSelected(i)}><span className="file-icon">{item.name.endsWith(".cu") ? <Cpu size={15} /> : item.name.endsWith(".py") ? <Terminal size={15} /> : <Code2 size={15} />}</span><span>{item.name}</span><ChevronRight size={14} /></button>)}
      <div className="file-info"><span>COMPILE THE TEST</span><code>g++ -std=c++20 -O2 -I native/include native/tests/verify.cpp -o verify</code><span>./verify</span></div></div>
      <div className="code-reader"><div className="code-toolbar"><div><span className="file-status-dot" /><strong>{file.path}</strong><span className="code-lang">{file.language} / {lines.length} LINES</span></div><div><button title="Copy source" onClick={copy}>{copied ? <Check size={16} /> : <Clipboard size={16} />}{copied ? "Copied" : "Copy"}</button><button title="Download source" onClick={() => saveFile(file.name, file.content)}><ArrowDownToLine size={16} /> Download</button></div></div>
        <div className="code-scroll"><pre>{lines.map((line, i) => <span className="code-line" key={i}><span className="line-number">{i + 1}</span><code className={line.trim().startsWith("//") || line.trim().startsWith("# ") ? "comment-line" : ""}>{line || " "}</code></span>)}</pre></div></div></div>
    <div className="source-footnote"><span><Check size={15} /> The bundled JSON is generated by the C++ executable, not typed-in metrics.</span><span>CUDA compilation requires nvcc; the host verification runs with g++.</span></div></>;
}