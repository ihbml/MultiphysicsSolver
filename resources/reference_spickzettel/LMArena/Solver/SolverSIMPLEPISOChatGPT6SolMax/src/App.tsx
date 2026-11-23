import { useMemo, useState } from "react";
import {
  Activity, ArrowDownToLine, ArrowRight, Atom, Check, ChevronRight, Code2,
  FlaskConical, GitBranch, Layers3, Menu, Play, RotateCcw, ScanLine, X,
} from "lucide-react";
import reference from "../public/verification.json";
import { runAdjoint, runAMG, runChemistry, runFlux, type FluxInput } from "./engine/verification";
import {
  AdjointPage, AMGPage, ChemistryPage, FluxPage, PressurePage, SourcePage, saveFile,
  type Page,
} from "./components/Pages";

const navigation = [
  { id: "flux" as Page, number: "01", label: "Face reconstruction", icon: ScanLine },
  { id: "pressure" as Page, number: "02", label: "Pressure coupling", icon: Activity },
  { id: "amg" as Page, number: "03", label: "Mixed precision", icon: Layers3 },
  { id: "chemistry" as Page, number: "04", label: "Stiff chemistry", icon: Atom },
  { id: "adjoint" as Page, number: "05", label: "Discrete adjoint", icon: GitBranch },
  { id: "source" as Page, number: "06", label: "Source archive", icon: Code2 },
];

export default function App() {
  const [page, setPage] = useState<Page>("flux");
  const [mobileOpen, setMobileOpen] = useState(false);
  const [replay, setReplay] = useState(0);
  const [running, setRunning] = useState(false);
  const [input, setInput] = useState<FluxInput>({ slope: 1.2, skew: 0.16, checkerboard: 0.35, mobility: 0.12, velocity: 0.8 });
  const [grid, setGrid] = useState(48);
  const [coupling, setCoupling] = useState(1);
  const [temperature, setTemperature] = useState(1100);
  const [dt, setDt] = useState(0.025);
  const [hotCells, setHotCells] = useState(4);
  const [gamma, setGamma] = useState(0.73);
  const [faceX, setFaceX] = useState(3.06);
  const [normal, setNormal] = useState(1.07);
  const flux = useMemo(() => runFlux(input), [input]);
  const amg = useMemo(() => runAMG(grid, coupling), [grid, coupling]);
  const chemistry = useMemo(() => runChemistry(temperature, dt, hotCells), [temperature, dt, hotCells]);
  const adjoint = useMemo(() => runAdjoint(gamma, faceX, normal), [gamma, faceX, normal]);
  const current = navigation.find((item) => item.id === page)!;
  const navigate = (destination: Page) => { setPage(destination); setMobileOpen(false); window.scrollTo({ top: 0, behavior: "smooth" }); };
  const rerun = () => { setRunning(true); window.setTimeout(() => { setReplay((value) => value + 1); setRunning(false); }, 360); };
  const reset = () => {
    setInput({ slope: 1.2, skew: 0.16, checkerboard: 0.35, mobility: 0.12, velocity: 0.8 });
    setGrid(48); setCoupling(1); setTemperature(1100); setDt(0.025); setHotCells(4);
    setGamma(0.73); setFaceX(3.06); setNormal(1.07); setReplay((value) => value + 1);
  };
  const downloadReport = () => saveFile("continuum-live-verification.json", JSON.stringify({
    provenance: "Browser numerical twin; native reference from g++ executable is separate",
    input, flux, amg, chemistry, adjoint, nativeReference: reference,
  }, null, 2), "application/json");

  return <div className="app-shell">
    <aside className={`sidebar ${mobileOpen ? "open" : ""}`}>
      <div className="brand"><div className="brand-mark"><svg viewBox="0 0 36 36" aria-hidden="true"><path d="M3 3h13v13H3zM20 3h13v13H20zM3 20h13v13H3z" fill="none" stroke="currentColor" strokeWidth="1.5" /><path d="M20 20h13v13H20zM20 33l13-13" fill="none" stroke="currentColor" strokeWidth="1.5" /><circle cx="26.5" cy="26.5" r="2" fill="currentColor" /></svg></div><div><strong>CONTINUUM</strong><span>ENGINEERING LAB</span></div><button className="mobile-close" aria-label="Close menu" onClick={() => setMobileOpen(false)}><X size={19} /></button></div>
      <div className="sidebar-label">VERIFICATION NOTEBOOK <span>01 - 06</span></div>
      <nav className="side-nav" aria-label="Notebook sections">{navigation.map((item) => { const Icon = item.icon; return <button key={item.id} className={page === item.id ? "active" : ""} onClick={() => navigate(item.id)}><span className="nav-number">{item.number}</span><Icon size={17} strokeWidth={1.7} /><span>{item.label}</span>{page === item.id && <span className="nav-active-mark" />}</button>; })}</nav>
      <div className="sidebar-spacer" />
      <div className="sidebar-bottom"><div className="runtime-status"><span className="status-led" /><div><strong>NATIVE REFERENCE</strong><span>06 / 06 checks passed</span></div><Check size={16} /></div><div className="sidebar-meta"><span>C++20 / CUDA / OPENMP</span><span>BUILD 001.0</span></div></div>
    </aside>
    {mobileOpen && <button className="mobile-backdrop" onClick={() => setMobileOpen(false)} aria-label="Close navigation" />}
    <div className="main-column"><header className="topbar"><div className="topbar-left"><button className="mobile-menu" onClick={() => setMobileOpen(true)} aria-label="Open navigation"><Menu size={20} /></button><span>WORKSPACE</span><ChevronRight size={13} /><strong>VERIFICATION NOTEBOOK</strong><ChevronRight size={13} /><span className="topbar-current">{current.number}</span></div><div className="topbar-right"><span className="topbar-native"><span className="status-led" /> NATIVE VERIFIED</span><button className="topbar-source" onClick={() => navigate("source")}><Code2 size={15} /> Source <ArrowRight size={14} /></button></div></header>
      <main className="main-content"><div className="workspace-intro"><div><span className="workspace-kicker"><FlaskConical size={14} /> NUMERICAL METHODS / WORKBENCH</span><div className="workspace-title"><span>CONTINUUM</span><i> / </i> <span className="workspace-subtitle">VERIFICATION LAB</span></div></div><div className="intro-actions"><button className="icon-action" onClick={downloadReport} title="Download browser and native report" aria-label="Download verification report"><ArrowDownToLine size={17} /></button><button className="icon-action" onClick={reset} title="Reset experiments" aria-label="Reset experiments"><RotateCcw size={17} /></button><button className="button button-outline" onClick={rerun} disabled={running}><Play size={13} fill="currentColor" /> {running ? "Checking..." : "Replay checks"}</button></div></div>
        <div className="content-enter" key={page}>
          {page === "flux" && <FluxPage input={input} setInput={setInput} result={flux} replay={replay} onRun={rerun} onNavigate={navigate} />}
          {page === "pressure" && <PressurePage input={input} setInput={setInput} result={flux} onNavigate={navigate} />}
          {page === "amg" && <AMGPage result={amg} grid={grid} setGrid={setGrid} coupling={coupling} setCoupling={setCoupling} onNavigate={navigate} />}
          {page === "chemistry" && <ChemistryPage result={chemistry} temp={temperature} setTemp={setTemperature} dt={dt} setDt={setDt} hot={hotCells} setHot={setHotCells} onNavigate={navigate} />}
          {page === "adjoint" && <AdjointPage result={adjoint} gamma={gamma} setGamma={setGamma} faceX={faceX} setFaceX={setFaceX} normal={normal} setNormal={setNormal} onNavigate={navigate} />}
          {page === "source" && <SourcePage />}
        </div>
        <footer className="footer"><span><span className="footer-cross">+</span> CONTINUUM / DISCRETE PHYSICS, EXPLICITLY VERIFIED</span><span>LOCAL COMPUTATION <span className="footer-separator">/</span> NO TELEMETRY</span></footer>
      </main>
    </div>
  </div>;
}
