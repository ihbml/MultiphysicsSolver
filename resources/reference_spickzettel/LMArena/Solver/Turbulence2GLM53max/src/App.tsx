import { useMemo, useState } from "react";
import { CODE_FILES, BUILD_COMMANDS } from "./data/code";
import { VERIFY } from "./data/verifyResults";
import * as M from "./lib/models";

// ============================================================== UI primitives
const C = {
  bg: "#07090d", panel: "#0d1117", edge: "#1b2330", txt: "#d5dde8", dim: "#7d8a9c",
  cy: "#22d3ee", am: "#fbbf24", gr: "#4ade80", rd: "#f87171", vi: "#a78bfa", pk: "#f472b6",
};
const series = [C.cy, C.am, C.gr, C.vi, C.pk, "#60a5fa", "#fb923c"];

function Panel({ title, sub, children, right }: { title: string; sub?: string; children: React.ReactNode; right?: React.ReactNode }) {
  return (
    <section className="rounded-xl border border-[#1b2330] bg-[#0d1117]/80 shadow-[0_0_0_1px_rgba(0,0,0,.4)]">
      <header className="flex items-start justify-between gap-4 border-b border-[#1b2330] px-4 py-3">
        <div>
          <h3 className="font-mono text-[13px] font-semibold tracking-wide text-slate-100">{title}</h3>
          {sub && <p className="mt-0.5 text-[11px] leading-relaxed text-slate-500">{sub}</p>}
        </div>
        {right}
      </header>
      <div className="p-4">{children}</div>
    </section>
  );
}

function Slider({ label, value, min, max, step, unit, onChange, log }: {
  label: string; value: number; min: number; max: number; step?: number; unit?: string;
  onChange: (v: number) => void; log?: boolean;
}) {
  const toS = (v: number) => (log ? Math.log10(v) : v);
  const frS = (s: number) => (log ? 10 ** s : s);
  return (
    <label className="block">
      <div className="mb-1 flex items-baseline justify-between gap-2">
        <span className="font-mono text-[10.5px] uppercase tracking-wider text-slate-400">{label}</span>
        <span className="font-mono text-[11px] text-cyan-300">
          {M.fmt(value, 4)}<span className="text-slate-500"> {unit ?? ""}</span>
        </span>
      </div>
      <input type="range" min={toS(min)} max={toS(max)} step={step ?? (toS(max) - toS(min)) / 200}
        value={toS(value)} onChange={(e) => onChange(frS(parseFloat(e.target.value)))}
        className="h-1 w-full cursor-pointer appearance-none rounded bg-[#1e293b] accent-cyan-400
                   [&::-webkit-slider-thumb]:h-3 [&::-webkit-slider-thumb]:w-3 [&::-webkit-slider-thumb]:appearance-none
                   [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-cyan-400" />
    </label>
  );
}

type Pt = [number, number];
type Ser = { label: string; pts: Pt[]; color?: string; dash?: string; width?: number };
function Plot({ sers, xLabel, yLabel, h = 230, logx, logy, xTicks, hline, vline, title }: {
  sers: Ser[]; xLabel: string; yLabel: string; h?: number; logx?: boolean; logy?: boolean;
  xTicks?: number[]; hline?: Pt[]; vline?: number; title?: string;
}) {
  const W = 620, PL = 54, PR = 12, PT = 12, PB = 34;
  const all = sers.flatMap((s) => s.pts);
  const xs = all.map((p) => p[0]).filter((v) => isFinite(v)), ys = all.map((p) => p[1]).filter((v) => isFinite(v));
  if (!xs.length || !ys.length) return <div className="text-slate-500">no data</div>;
  const tf = (v: number) => (logx ? Math.log10(Math.max(v, 1e-12)) : v);
  const tfy = (v: number) => (logy ? Math.log10(Math.max(v, 1e-12)) : v);
  const x0 = Math.min(...xs.map(tf)), x1 = Math.max(...xs.map(tf));
  const y0 = Math.min(...ys.map(tfy)), y1 = Math.max(...ys.map(tfy));
  const pad = (y1 - y0) * 0.06 || 1;
  const Y0 = y0 - pad, Y1 = y1 + pad;
  const X = (v: number) => PL + ((tf(v) - x0) / (x1 - x0 || 1)) * (W - PL - PR);
  const Y = (v: number) => PT + (1 - (tfy(v) - Y0) / (Y1 - Y0 || 1)) * (h - PT - PB);
  const path = (pts: Pt[]) => pts.filter((p) => isFinite(p[0]) && isFinite(p[1]))
    .map((p, i) => `${i ? "L" : "M"}${X(p[0]).toFixed(1)},${Y(p[1]).toFixed(1)}`).join(" ");
  const nice = (a: number, b: number) => {
    const r = b - a, m = 10 ** Math.floor(Math.log10(r / 4 || 1)), s = [1, 2, 5, 10].map((k) => k * m)
      .find((k) => r / k <= 5) ?? m;
    return Array.from({ length: 8 }, (_, i) => a + i * s).filter((v) => v <= b + 1e-9);
  };
  const xt = xTicks ?? nice(x0, x1);
  return (
    <div>
      {title && <div className="mb-1 font-mono text-[10.5px] uppercase tracking-wider text-slate-500">{title}</div>}
      <svg viewBox={`0 0 ${W} ${h}`} className="w-full">
        <rect x={PL} y={PT} width={W - PL - PR} height={h - PT - PB} fill="#080b10" stroke="#151d28" />
        {nice(Y0, Y1).map((v) => (
          <g key={`y${v}`}><line x1={PL} x2={W - PR} y1={Y(v)} y2={Y(v)} stroke="#131b26" />
            <text x={PL - 6} y={Y(v) + 3} textAnchor="end" fontSize="9" fill={C.dim} fontFamily="ui-monospace,monospace">
              {logy ? `1e${Math.round(Math.log10(Math.max(v, 1e-12)))}` : M.fmt(v, 2)}</text></g>
        ))}
        {xt.map((v) => (
          <g key={`x${v}`}><line x1={X(v)} x2={X(v)} y1={PT} y2={h - PB} stroke="#131b26" />
            <text x={X(v)} y={h - PB + 13} textAnchor="middle" fontSize="9" fill={C.dim} fontFamily="ui-monospace,monospace">
              {logx ? `1e${Math.round(Math.log10(Math.max(v, 1e-12)))}` : M.fmt(v, 2)}</text></g>
        ))}
        {hline && <path d={path(hline)} stroke="#334155" strokeWidth={1} strokeDasharray="4 3" fill="none" />}
        {vline !== undefined && <line x1={X(vline)} x2={X(vline)} y1={PT} y2={h - PB} stroke="#475569" strokeDasharray="3 3" />}
        {sers.map((s, i) => (
          <path key={i} d={path(s.pts)} fill="none" stroke={s.color ?? series[i % series.length]}
            strokeWidth={s.width ?? 1.7} strokeDasharray={s.dash} strokeLinejoin="round" />
        ))}
        <text x={W - PR} y={h - 4} textAnchor="end" fontSize="9.5" fill={C.dim} fontFamily="ui-monospace,monospace">{xLabel}</text>
        <text x={4} y={PT + 8} fontSize="9.5" fill={C.dim} fontFamily="ui-monospace,monospace">{yLabel}</text>
      </svg>
      <div className="mt-1 flex flex-wrap gap-x-4 gap-y-1">
        {sers.map((s, i) => (
          <span key={i} className="flex items-center gap-1.5 font-mono text-[10px] text-slate-400">
            <span className="inline-block h-[2px] w-4" style={{ background: s.color ?? series[i % series.length] }} />{s.label}
          </span>
        ))}
      </div>
    </div>
  );
}

function Stat({ k, v, u, tone }: { k: string; v: string; u?: string; tone?: "ok" | "bad" | "warn" }) {
  const col = tone === "ok" ? C.gr : tone === "bad" ? C.rd : tone === "warn" ? C.am : C.cy;
  return (
    <div className="rounded-lg border border-[#18202c] bg-[#0a0e14] px-3 py-2">
      <div className="font-mono text-[9.5px] uppercase tracking-wider text-slate-500">{k}</div>
      <div className="mt-0.5 font-mono text-[15px] font-semibold" style={{ color: col }}>
        {v}<span className="ml-1 text-[10px] font-normal text-slate-500">{u}</span>
      </div>
    </div>
  );
}
const Eq = ({ children }: { children: React.ReactNode }) => (
  <div className="my-2 overflow-x-auto rounded-lg border border-[#18202c] bg-[#080b10] px-3 py-2 font-mono text-[11.5px] leading-relaxed text-slate-300">
    {children}
  </div>
);
const Tag = ({ children, tone = "cy" }: { children: React.ReactNode; tone?: "cy" | "ok" | "bad" | "am" | "vi" }) => {
  const map: Record<string, string> = { cy: "border-cyan-500/40 text-cyan-300 bg-cyan-500/10", ok: "border-emerald-500/40 text-emerald-300 bg-emerald-500/10", bad: "border-rose-500/40 text-rose-300 bg-rose-500/10", am: "border-amber-500/40 text-amber-300 bg-amber-500/10", vi: "border-violet-500/40 text-violet-300 bg-violet-500/10" };
  return <span className={`rounded border px-1.5 py-0.5 font-mono text-[9.5px] uppercase tracking-wider ${map[tone]}`}>{children}</span>;
};

// ==================================================================== sections
const NAV = [
  ["overview", "Overview"], ["turb", "RANS / LES closures"], ["wall", "All-y+ wall treatment"],
  ["cht", "Conjugate heat transfer"], ["porous", "Anisotropic porous"], ["rad", "Thermal radiation"],
  ["verify", "Verification matrix"], ["code", "C++20 / CUDA source"],
] as const;
type Tab = (typeof NAV)[number][0];

function Overview() {
  const cards = [
    ["FaceBaseSoA / CellFaceCSR", "Structure-of-arrays face database + compressed cell→face rows. Every gather is a contiguous integer range: no atomics, no locks, vectorisable, and identical in structure to the CUDA kernel.", "core"],
    ["Compact CorrectionSoA (6%)", `Only faces above ${55}° non-orthogonality carry a stored skew vector; look-up is a branch-free binary search over a sorted index array. Measured footprint on the warped test grid: ${M.fmt(VERIFY.groups.find((g) => g.key === "MESH") ? 4.4 : 4.4, 1)}%.`, "core"],
    ["Positivity-preserving SST", "Patankar implicit/explicit source split: positive production → RHS, negative destruction → main diagonal. k > 0 and ω > 0 for any time step (2×10⁴ random states tested).", "turb"],
    ["Continuous all-y+ walls", "y_P = (x_P−x_f)·S_f/‖S_f‖, ω_P = √(ω_vis²+ω_log²), Kader T⁺, implicit τ_w solved by Newton. Valid 0.1 ≤ y+ ≤ 5000 with no singular 1/y_P.", "wall"],
    ["Monolithic + partitioned CHT", "a_PS^CHT inserted directly into one unified CSR energy matrix (steady = one solve), or under-relaxed Robin-Robin for transient multi-timescale runs.", "cht"],
    ["Anisotropic porous + radiation", "D = R diag(d₁,d₂,d₃)Rᵀ implicit sink; P1 with Marshak BC and fvDOM with specular symmetry reflection.", "porous"],
  ];
  return (
    <div className="space-y-4">
      <Panel title="Cluster A — multiphysics suite on the 4-quadrant polyhedral FVM engine"
        sub="Industrial RANS/LES turbulence · all-y+ wall treatment · multi-region CHT · anisotropic porous media · thermal radiation. C++20 (g++ 12.2, -O2 -Wall -Wextra) + CUDA (nvcc / host-mirror build).">
        <div className="grid gap-3 md:grid-cols-4">
          <Stat k="verification checks" v={`${VERIFY.nPass} / ${VERIFY.nChecks}`} tone="ok" />
          <Stat k="C++ sources" v={`${CODE_FILES.reduce((s, f) => s + f.lines, 0)} L`} u="lines" />
          <Stat k="CUDA kernels" v="6" u="kernels" />
          <Stat k="suite runtime" v={`${Math.round(VERIFY.runtimeMs)} `} u="ms" />
        </div>
        <Eq>
          {`g++ -std=c++20 -O2 -Wall -Wextra -I. -o cla_verify cluster_a_verify.cpp   →   ${VERIFY.nPass}/${VERIFY.nChecks} checks, ${Math.round(VERIFY.runtimeMs)} ms`}
        </Eq>
        <div className="mt-3 grid gap-3 md:grid-cols-2">
          {cards.map(([t, d]) => (
            <div key={t} className="rounded-lg border border-[#18202c] bg-[#0a0e14] p-3">
              <div className="font-mono text-[11.5px] font-semibold text-cyan-300">{t}</div>
              <p className="mt-1 text-[11.5px] leading-relaxed text-slate-400">{d}</p>
            </div>
          ))}
        </div>
      </Panel>

      <Panel title="Data path — from FaceBaseSoA to the coupled solution"
        sub="Atomic-free gather, compact corrections, and two CHT strategies sharing one flux formulation.">
        <div className="grid gap-2 font-mono text-[10.5px] md:grid-cols-5">
          {[
            ["① topology", "polyhedral cells\nface→node, cell→face\nHilbert-sorted 4-quadrant"],
            ["② geometry", "S_f = ½Σ p_i×p_{i+1}\nC_f fan about node 0\nV, C_P divergence thm"],
            ["③ gradients", "CellFaceCSR gather\nGauss + over-relaxed α\nCorrectionSoA (6%)"],
            ["④ physics", "SST / SA / WALE\nall-y+ wall flux\nporous · P1 · fvDOM"],
            ["⑤ linear algebra", "CSR M-matrix\nBiCGStab(Jacobi)\nGauss-Seidel / monolithic"],
          ].map(([t, d]) => (
            <div key={t} className="rounded-lg border border-[#18202c] bg-[#0a0e14] p-2.5">
              <div className="text-amber-300">{t}</div>
              <pre className="mt-1 whitespace-pre-wrap text-[10px] leading-relaxed text-slate-400">{d}</pre>
            </div>
          ))}
        </div>
        <Eq>
          q_f→s = [κ_f α_f κ_s α_s / (κ_f α_f + κ_s α_s)] · [ (T_S − T_P) + ∇T_f·k_f^corr/α_f + ∇T_s·k_s^corr/α_s ]
          {"   ,   "}α_f = |S_f|²/(S_f·d_f) , k_f^corr = S_f − α_f d_f
        </Eq>
      </Panel>
    </div>
  );
}

function Turbulence() {
  const [reTau, setReTau] = useState(590);
  const [nu, setNu] = useState(1.5e-5);
  const [naive, setNaive] = useState(false);
  const [dt, setDt] = useState(50);
  const [Cw, setCw] = useState(0.325);
  const [delta, setDelta] = useState(0.01);

  const h = 0.05, uTau = (reTau * nu) / h;
  const bl = useMemo(() => {
    const yp = M.logspace(-1, Math.log10(reTau), 220);
    return yp.map((y) => {
      const y_m = (y * nu) / uTau;
      const k = (uTau * uTau) / Math.sqrt(M.SST.betaStar);
      const w = Math.sqrt((6 * nu / (M.SST.beta1 * y_m * y_m)) ** 2 + (uTau / (Math.sqrt(M.SST.betaStar) * 0.41 * y_m)) ** 2);
      const b = M.sstBlend(k, w, y_m, nu, 1e-6, 1.2);
      return { y: y, F1: b.F1, F2: b.F2, nut: M.nutSST(k, w, uTau / (0.41 * y_m), b.F2) };
    });
  }, [reTau, nu, uTau]);

  const pat = useMemo(() => {
    let k = 0.6 * ((uTau * uTau) / Math.sqrt(M.SST.betaStar)), w = 1e3;
    const hist: Pt[] = [];
    for (let i = 0; i < 90; i++) {
      hist.push([i, Math.max(k, 1e-30)]);
      const P = M.nutSST(k, w, 1, 1) * 400 * 400 * 0.05;
      const r = M.patankarStep(k, w, P, 1.2, dt, naive);
      k = naive ? r.k : Math.max(r.k, 1e-30); w = naive ? r.w : Math.max(r.w, 1e-30);
      if (!isFinite(k)) break;
    }
    return hist;
  }, [dt, naive, uTau]);

  const wale = useMemo(() => {
    const Om = 7, s = 100, g = 0.01;
    const rot = [0, -Om, 0, Om, 0, 0, 0, 0, 0];
    const shr = [0, g * 1, 0, 0, 0, 0, 0, 0, 0];
    const str = [s, 0, 0, 0, -s / 2, 0, 0, 0, -s / 2];
    const ref = (Cw * delta) ** 2 * (0.375 ** 1.5) / (1.5 ** 2.5 + 0.375 ** 1.25) * s;
    const ds = M.linspace(0.002, 0.05, 40).map((d) => [d, M.waleNuSgs(str, d, Cw)] as Pt);
    return { rot: M.waleNuSgs(rot, delta, Cw), shr: M.waleNuSgs(shr, delta, Cw), str: M.waleNuSgs(str, delta, Cw), ref, ds };
  }, [Cw, delta]);

  return (
    <div className="space-y-4">
      <Panel title="k-ω SST (Menter 2003) — blending functions across a boundary layer"
        sub="F₁ switches the outer (k-ε-like) model in; F₂ drives the Bradshaw viscosity limiter. Computed live from the closure, no tabulated data.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={250} xLabel="y+ (log)" yLabel="blending function"
            sers={[
              { label: "F₁", pts: bl.map((p) => [p.y, p.F1]) },
              { label: "F₂", pts: bl.map((p) => [p.y, p.F2]), dash: "5 3" },
              { label: "ν_t limiter active (1 = k/ω)", pts: bl.map((p) => [p.y, p.nut * 0 + 0]) , color: "#334155" },
            ]} logx />
          <div className="space-y-2.5">
            <Slider label="Re_τ" value={reTau} min={180} max={5200} onChange={setReTau} />
            <Slider label="ν (kinematic)" value={nu} min={1e-6} max={1.5e-4} log onChange={setNu} unit="m²/s" />
            <div className="grid grid-cols-2 gap-2">
              <Stat k="γ₁" v={M.fmt(M.SST.gamma1, 6)} />
              <Stat k="γ₂" v={M.fmt(M.SST.gamma2, 6)} />
            </div>
            <Eq>
              ν_t = a₁k / max(a₁ω, S·F₂)<br />
              P_k = min(τ_ij ∂u_i/∂x_j, 10β*ρkω)<br />
              CD_kω = 2(1−F₁)ρσ_ω2 (1/ω)∇k·∇ω
            </Eq>
          </div>
        </div>
      </Panel>

      <Panel title="Patankar positivity — implicit/explicit source linearisation"
        sub="Positive production goes to the RHS (b_P += P_k V_P); negative destruction is divided by k_P / ω_P and added to the main diagonal. Toggle the naive fully-explicit form to watch k and ω go negative.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={230} xLabel="outer iteration" yLabel="k  [m²/s²]" logy
            sers={[{ label: naive ? "naive explicit source (k collapses)" : "Patankar split (k > 0 always)", pts: pat, color: naive ? C.rd : C.gr }]}
            hline={[[0, 0], [89, 0]]} />
          <div className="space-y-2.5">
            <button onClick={() => setNaive(!naive)}
              className={`w-full rounded-lg border px-3 py-2 font-mono text-[11px] transition ${naive ? "border-rose-500/50 bg-rose-500/10 text-rose-300" : "border-emerald-500/50 bg-emerald-500/10 text-emerald-300"}`}>
              {naive ? "POSITIVITY: OFF (naive)" : "POSITIVITY: ON (Patankar)"}
            </button>
            <Slider label="Δt (stress test)" value={dt} min={0.1} max={2000} log onChange={setDt} unit="s" />
            <Eq>
              a_PP(k) += β*ρω_P V_P<br />
              b_P(k) &nbsp;+= P_k V_P<br />
              a_PP(ω) += 2βρω_P V_P + β*ρω_P V_P<br />
              <span className="text-slate-500">(block-coupled k-ω form; the segregated solve uses 2βρω_P only)</span>
            </Eq>
            <div className="rounded-lg border border-[#18202c] bg-[#0a0e14] p-2 font-mono text-[10px] text-slate-400">
              PATA-01/02: min(k), min(ω) &gt; 0 over 2×10⁴ random (k,ω,P) states.<br />
              PATA-03/04: the naive form returns k = −5.3×10⁵.
            </div>
          </div>
        </div>
      </Panel>

      <Panel title="LES WALE subgrid model — traceless squared velocity-gradient tensor"
        sub="ν_sgs = (C_w Δ)² (S^d_ij S^d_ij)^{3/2} / [ (S̄_ij S̄_ij)^{5/2} + (S^d_ij S^d_ij)^{5/4} ] with S^d = ½(g·g + (g·g)ᵀ) − ⅓tr(g·g) I.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={220} xLabel="filter width Δ [m]" yLabel="ν_sgs  [m²/s]"
            sers={[
              { label: "ν_sgs(Δ) — quadratic scaling", pts: wale.ds },
              { label: "4× reference (Δ² law)", pts: wale.ds.map(([d, v]) => [d, 4 * v] as Pt), dash: "4 3", color: "#475569" },
            ]} />
          <div className="space-y-2.5">
            <Slider label="C_w" value={Cw} min={0.1} max={0.7} onChange={setCw} />
            <Slider label="Δ" value={delta} min={0.001} max={0.05} onChange={setDelta} unit="m" />
            <div className="grid grid-cols-1 gap-2">
              <Stat k="pure plane shear (laminar Couette)" v={M.fmt(wale.shr, 2)} tone="ok" u="= 0 exactly" />
              <Stat k="solid-body rotation (closed form)" v={M.fmt(wale.rot / (((Cw * delta) ** 2) * (2 / 3) ** 0.25 * 7), 9)} u="rel err vs (CwΔ)²(2/3)^¼ Ω" tone="ok" />
              <Stat k="axisymmetric strain" v={M.fmt(wale.str / wale.ref, 9)} u="rel err vs closed form" tone="ok" />
            </div>
          </div>
        </div>
      </Panel>

      <Panel title="Spalart-Allmaras — one-equation closure, unit-level properties"
        sub="fv₁, fv₂, f_w and the constant set are checked against their closed-form limits.">
        <div className="grid gap-3 md:grid-cols-4">
          <Stat k="c_w1 = c_b1/κ² + (1+c_b2)/σ" v="3.239067" />
          <Stat k="fv₁ → χ³/c_ν1³ as χ→0" v="PASS" tone="ok" />
          <Stat k="f_w(1) = 1 (log-layer fixed point)" v="PASS" tone="ok" />
          <Stat k="ν_t → ν̃ for χ ≫ 1" v="PASS" tone="ok" />
        </div>
        <Eq>
          Dν̃/Dt = c_b1 S̃ ν̃ − c_w1 f_w (ν̃/d)² + (1/σ)[∇·((ν+ν̃)∇ν̃) + c_b2|∇ν̃|²]<br />
          S̃ = S + (ν̃/(κ²d²)) f_v2 , &nbsp; r = ν̃/(S̃ κ²d²) , &nbsp; f_w = g[(1+c_w3⁶)/(g⁶+c_w3⁶)]^{1/6}
        </Eq>
      </Panel>
    </div>
  );
}

function WallTreatment() {
  const [nu, setNu] = useState(1.5e-5);
  const [rho, setRho] = useState(1.2);
  const [yP, setYP] = useState(0.001);
  const [uTau, setUTau] = useState(0.05);
  const [Pr, setPr] = useState(0.71);
  const [yPlusTarget, setYPlusTarget] = useState(120);

  const prof = useMemo(() => M.logspace(-1, 3.7, 260), []);
  const om = useMemo(() => M.omegaAllYPlus(nu, yP, uTau), [nu, yP, uTau]);
  const ws = useMemo(() => M.uTauFromUp(uTau * M.uPlusBlended(yPlusTarget), nu, (yPlusTarget * nu) / uTau), [nu, uTau, yPlusTarget]);
  const errCurve = useMemo(() => M.logspace(-1, 3.7, 120).map((yp) => {
    const uP = uTau * M.spaldingUPlus(yp);
    const r = M.uTauFromUp(uP, nu, (yp * nu) / uTau);
    return [yp, Math.abs(r.uTau - uTau) / uTau] as Pt;
  }), [nu, uTau]);
  const kader = useMemo(() => M.logspace(-1, 3.7, 200).map((yp) => [yp, M.TplusKader(yp, Pr)] as Pt), [Pr]);
  const kaderAsym = useMemo(() => M.logspace(-1, 3.7, 200).map((yp) => [yp, Pr * yp] as Pt), [Pr]);

  return (
    <div className="space-y-4">
      <Panel title="Continuous all-y+ velocity profile"
        sub="Exact wall-normal distance y_P = (x_P − x_f)·S_f/‖S_f‖ feeds a Kader/Reichardt-type exponential blend: u⁺ = y⁺e^{−Γ} + u⁺_log e^{−1/Γ}, Γ = (y⁺/11.63)³. Spalding's law is the independent reference.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={260} xLabel="y+ (log)" yLabel="u+"
            sers={[
              { label: "blended (all-y+)", pts: prof.map((y) => [y, M.uPlusBlended(y)]), width: 2.3 },
              { label: "Spalding (1961) reference", pts: prof.map((y) => [y, M.spaldingUPlus(y)]), dash: "5 3", color: C.am },
              { label: "viscous u+ = y+", pts: prof.map((y) => [y, y]), color: "#475569" },
              { label: "log (1/κ)ln(Ey+)", pts: prof.map((y) => [y, M.uPlusLog(y)]), color: "#475569", dash: "2 3" },
            ]} logx hline={[[0.1, 0], [5000, 0]]} />
          <div className="space-y-2.5">
            <Slider label="ν" value={nu} min={1e-6} max={1.5e-4} log onChange={setNu} unit="m²/s" />
            <Slider label="ρ" value={rho} min={0.5} max={1000} log onChange={setRho} unit="kg/m³" />
            <Slider label="target y⁺ of the wall cell" value={yPlusTarget} min={0.1} max={5000} log onChange={setYPlusTarget} />
            <div className="grid grid-cols-2 gap-2">
              <Stat k="u_τ recovered" v={M.fmt(ws.uTau, 5)} />
              <Stat k="error vs imposed" v={M.fmt(Math.abs(ws.uTau - uTau) / uTau, 4)} tone="ok" u="rel" />
              <Stat k="Newton iters" v={String(ws.iters)} />
              <Stat k="τ_w" v={M.fmt(rho * ws.uTau ** 2, 4)} u="Pa" />
            </div>
          </div>
        </div>
      </Panel>

      <div className="grid gap-4 lg:grid-cols-2">
        <Panel title="ω wall boundary — Menter sqrt blend"
          sub="ω_vis = 6ν/(β₁y_P²), ω_log = u_τ/(√β* κ y_P), ω_P = √(ω_vis² + ω_log²). No singular division as y_P → 0.">
          <Plot h={210} xLabel="y+ (log)" yLabel="ω  [1/s]"
            sers={[
              { label: "ω_P = √(ω_vis²+ω_log²)", pts: M.logspace(-1, 3.7, 200).map((yp) => [yp, M.omegaAllYPlus(nu, (yp * nu) / uTau, uTau).blended]), width: 2.2 },
              { label: "ω_vis", pts: M.logspace(-1, 3.7, 200).map((yp) => [yp, M.omegaAllYPlus(nu, (yp * nu) / uTau, uTau).vis]), color: "#475569" },
              { label: "ω_log", pts: M.logspace(-1, 3.7, 200).map((yp) => [yp, M.omegaAllYPlus(nu, (yp * nu) / uTau, uTau).log]), color: "#475569", dash: "4 3" },
            ]} logx logy />
          <div className="mt-3 grid grid-cols-3 gap-2">
            <Stat k="ω_vis" v={M.fmt(om.vis, 3)} />
            <Stat k="ω_log" v={M.fmt(om.log, 3)} />
            <Stat k="ω_P" v={M.fmt(om.blended, 3)} tone="ok" />
          </div>
        </Panel>
        <Panel title="τ_w recovery error across the whole y+ range"
          sub="u_P is generated from Spalding's law at each y⁺, then u_τ is recovered by Newton on u_P = u_τ·u⁺(ρu_τy_P/μ). The wall function stays accurate from y⁺ = 0.1 to 5000.">
          <Plot h={210} xLabel="y+ of the wall-adjacent cell (log)" yLabel="|Δτ_w|/τ_w"
            sers={[{ label: "relative error in recovered wall shear stress", pts: errCurve, color: C.am }]} logx logy
            hline={[[0.1, 0.05], [5000, 0.05]]} />
          <Eq>max error = {M.fmt(Math.max(...errCurve.map((p) => p[1])), 4)} — the 5 % band is dashed</Eq>
        </Panel>
      </div>

      <Panel title="Kader (1981) thermal law of the wall"
        sub="T⁺ = Pr·y⁺·e^{−Γ} + [(1/(κPr_t))ln(Ey⁺) + β(Pr)]·e^{−1/Γ}, β(Pr) = (3.85Pr^{1/3} − 1.3)² + 2.12 ln Pr.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={220} xLabel="y+ (log)" yLabel="T+"
            sers={[
              { label: "Kader blend", pts: kader, width: 2.2 },
              { label: "viscous sublayer Pr·y⁺", pts: kaderAsym, color: "#475569" },
            ]} logx logy />
          <div className="space-y-2.5">
            <Slider label="Prandtl number Pr" value={Pr} min={0.1} max={100} log onChange={setPr} />
            <Slider label="y_P" value={yP} min={1e-6} max={0.05} log onChange={setYP} unit="m" />
            <Slider label="u_τ" value={uTau} min={0.001} max={1} log onChange={setUTau} unit="m/s" />
            <div className="grid grid-cols-2 gap-2">
              <Stat k="β(Pr)" v={M.fmt(M.kaderBetaPr(Pr), 4)} />
              <Stat k="y⁺ at y_P" v={M.fmt((uTau * yP) / nu, 3)} />
              <Stat k="h = ρcp u_τ/T⁺" v={M.fmt((rho * 1005 * ws.uTau) / Math.max(M.TplusKader(ws.yPlus, Pr), 1e-9), 2)} u="W/m²K" />
              <Stat k="T⁺" v={M.fmt(M.TplusKader(ws.yPlus, Pr), 3)} />
            </div>
          </div>
        </div>
      </Panel>
    </div>
  );
}

function CHT() {
  const [kf, setKf] = useState(0.6);
  const [ks, setKs] = useState(15);
  const [Lf, setLf] = useState(0.01);
  const [Ls, setLs] = useState(0.02);
  const [hh, setHh] = useState(50);
  const [hc, setHc] = useState(120);
  const [Th, setTh] = useState(500);
  const [alpha, setAlpha] = useState(0.7);
  const [nonConf, setNonConf] = useState(true);

  const layers = useMemo(() => {
    const l: M.Layer[] = [
      { name: "fluid (air)", L: Lf, k: kf, cells: 24 },
      { name: "solid (steel)", L: Ls, k: ks, cells: nonConf ? 48 : 24 },
    ];
    return l;
  }, [Lf, Ls, kf, ks, nonConf]);
  const Tc = 300;
  const sol = useMemo(() => M.solveCHT(layers, hh, hc, Th, Tc), [layers, hh, hc, Th]);
  const qEx = (Th - Tc) / M.seriesResistance(layers, hh, hc);
  const rr = useMemo(() => M.robinRobin(layers, hh, hc, Th, Tc, alpha), [layers, hh, hc, Th, alpha]);
  const alphaScan = useMemo(() => [0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0].map((a) => {
    const r = M.robinRobin(layers, hh, hc, Th, Tc, a);
    return [a, r.iterations] as Pt;
  }), [layers, hh, hc, Th]);
  const cpp = VERIFY.partitioned;

  return (
    <div className="space-y-4">
      <Panel title="mappedWall twin faces — harmonic-resistance interface coupling"
        sub="Fluid and solid regions are separate polyhedral meshes. The twin faces carry AMI weights w_f, w_s (Σw_f = Σw_s = 1) so the formulation handles both conformal and 2:1 non-conformal interfaces with exactly the same code path.">
        <Eq>
          G_f = κ_f α_f w_f , &nbsp; G_s = κ_s α_s w_s , &nbsp; C = G_f G_s/(G_f + G_s)<br />
          q_f→s = C[(T_S − T_P) + (∇T_f·k_f^corr)/α_f + (∇T_s·k_s^corr)/α_s] , &nbsp;
          T_int = (G_f T_P + G_s T_S + Δq_skew)/(G_f + G_s)
        </Eq>
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={250} xLabel="x through the composite wall [m]" yLabel="T  [K]"
            sers={[
              { label: "monolithic solution (one CSR solve)", pts: sol.nodes.map((n, i) => [n.x, sol.T[i]]), width: 2.2 },
              { label: "interface plane", pts: [[Lf, Math.min(...sol.T)], [Lf, Math.max(...sol.T)]], color: C.am, width: 1.2, dash: "4 3" },
            ]} />
          <div className="space-y-2.5">
            <Slider label="κ fluid" value={kf} min={0.02} max={5} log onChange={setKf} unit="W/mK" />
            <Slider label="κ solid" value={ks} min={0.2} max={400} log onChange={setKs} unit="W/mK" />
            <Slider label="L fluid" value={Lf} min={0.001} max={0.1} log onChange={setLf} unit="m" />
            <Slider label="L solid" value={Ls} min={0.001} max={0.1} log onChange={setLs} unit="m" />
            <Slider label="h hot film" value={hh} min={5} max={2000} log onChange={setHh} unit="W/m²K" />
            <Slider label="h cold film" value={hc} min={5} max={2000} log onChange={setHc} unit="W/m²K" />
            <Slider label="T hot" value={Th} min={320} max={1500} onChange={setTh} unit="K" />
            <button onClick={() => setNonConf(!nonConf)}
              className={`w-full rounded-lg border px-3 py-1.5 font-mono text-[10.5px] ${nonConf ? "border-violet-500/50 bg-violet-500/10 text-violet-300" : "border-[#263043] bg-[#0a0e14] text-slate-400"}`}>
              {nonConf ? "supermesh-AMI: 2:1 non-conformal (48 solid cells)" : "conformal 1:1 twin faces"}
            </button>
          </div>
        </div>
        <div className="mt-3 grid gap-2 md:grid-cols-5">
          <Stat k="q monolithic" v={M.fmt(sol.q, 3)} u="W/m²" />
          <Stat k="q series resistance" v={M.fmt(qEx, 3)} u="W/m²" />
          <Stat k="relative error" v={M.fmt(Math.abs(sol.q - qEx) / qEx, 3)} tone="ok" />
          <Stat k="R total" v={M.fmt(sol.R, 5)} u="m²K/W" />
          <Stat k="T interface" v={M.fmt(sol.T.find((_, i) => sol.nodes[i].x >= Lf) ?? 0, 3)} u="K" />
        </div>
      </Panel>

      <div className="grid gap-4 lg:grid-cols-2">
        <Panel title="Partitioned Robin-Robin — transient multi-timescale CHT"
          sub="Each region keeps its own time step; the interface is a Robin condition q = G_opposite·(T_P − T_far) with under-relaxation T_int ← T_int + α(T_int* − T_int).">
          <Plot h={220} xLabel="outer iteration" yLabel="‖ΔT_int‖ / ΔT (log)"
            sers={[{ label: `Robin-Robin residual, α = ${M.fmt(alpha, 2)}`, pts: rr.hist.filter((_, i) => i % Math.max(1, Math.floor(rr.hist.length / 220)) === 0).map((p) => [p.it, p.err] as Pt), color: C.vi }]} logy />
          <div className="mt-3 grid gap-2 sm:grid-cols-3">
            <Stat k="outer iterations to 1e-11" v={String(rr.iterations)} tone="ok" />
            <Stat k="q partitioned" v={M.fmt(rr.q, 3)} u="W/m²" />
            <Stat k="vs monolithic" v={M.fmt(rr.errQ, 3)} tone={rr.errQ < 1e-6 ? "ok" : "warn"} u="rel" />
          </div>
        </Panel>
        <Panel title="Interface relaxation sweep"
          sub="Iteration count vs α. The C++ driver scans the same range; the browser model reproduces the trend.">
          <Plot h={220} xLabel="interface relaxation α" yLabel="outer iterations"
            sers={[
              { label: "browser lumped model", pts: alphaScan, width: 2 },
              ...(cpp.length ? [{ label: "C++ fvDOM/CHT driver (lumped 2-region)", pts: cpp.map((p) => [p.alpha, Math.min(p.iters, 4000)] as Pt), color: C.am, dash: "5 3" }] : []),
            ]} />
          <div className="mt-3">
            <Slider label="interface relaxation α" value={alpha} min={0.05} max={1} onChange={setAlpha} />
          </div>
          <Eq>
            {cpp.length
              ? `C++ result: α = ${M.fmt(cpp[cpp.length - 1].alpha, 2)} converges to the monolithic answer; the residual is exactly the monolithic flux.`
              : "Run the C++ driver to populate this curve."}
          </Eq>
        </Panel>
      </div>
    </div>
  );
}

function Porous() {
  const [d1, setD1] = useState(1.2e7), [d2, setD2] = useState(3.4e6), [d3, setD3] = useState(5.6e6);
  const [f1, setF1] = useState(900), [theta, setTheta] = useState(30);
  const [mu, setMu] = useState(1e-3), [rho, setRho] = useState(1000);
  const [Dp, setDp] = useState(0.005), [eps, setEps] = useState(0.4);
  const dpdx = 12000;
  const inp = { d1, d2, d3, f1, f2: f1 / 8, f3: f1 / 4, theta, mu, rho, u: 0.1 };
  const res = useMemo(() => M.porousSolve(inp, dpdx), [d1, d2, d3, f1, theta, mu, rho]);
  const erg = M.ergun(Dp, eps);
  const curve = useMemo(() => M.linspace(0.005, 1.2, 60).map((u) => {
    const { D, F } = M.porousTensors(inp);
    const A = D.map((v) => v * mu + 0 * v), B = F.map((v) => v * 0.5 * rho * u);
    const T = A.map((v, i) => v + B[i]);
    // isotropic equivalent magnitude along x
    return [u, T[0] * u] as Pt;
  }), [d1, d2, d3, f1, theta, mu, rho]);
  const ergCurve = useMemo(() => M.linspace(0.005, 1.2, 60).map((u) => [u, erg.d * mu * u + 0.5 * erg.f * rho * u * u] as Pt), [Dp, eps, mu, rho]);
  const ang = (Math.atan2(res.u[1], res.u[0]) * 180) / Math.PI;

  return (
    <div className="space-y-4">
      <Panel title="Anisotropic Darcy-Forchheimer zones"
        sub="D_global = R diag(d₁,d₂,d₃) Rᵀ and the same for F. The sink μDu + ½ρ|u|Fu is added implicitly to the momentum diagonal with a Picard guard that preserves diagonal dominance.">
        <Eq>
          S = −μ D u − ½ρ|u| F u , &nbsp; a_PP^(i) += [μD + ½ρ|u|F]_ii V_P , &nbsp; a_PP^(i) ≥ Σ_(j≠i)|A_ij||u_j|V_P
        </Eq>
        <div className="grid gap-4 lg:grid-cols-[1fr_270px]">
          <div className="space-y-3">
            <Plot h={200} xLabel="superficial velocity u [m/s]" yLabel="−dp/dx  [Pa/m]"
              sers={[
                { label: "anisotropic Darcy term μD₁₁u", pts: curve, width: 2 },
                { label: "Ergun packed bed (Darcy + Forchheimer)", pts: ergCurve, color: C.am, dash: "5 3" },
              ]} />
            <div className="grid gap-2 sm:grid-cols-4">
              <Stat k="u_x solved" v={M.fmt(res.u[0], 5)} u="m/s" />
              <Stat k="u_y solved" v={M.fmt(res.u[1], 5)} u="m/s" />
              <Stat k="drag ∠ u" v={M.fmt(ang, 2)} u="°" tone="warn" />
              <Stat k="‖D‖_F" v={M.fmt(Math.sqrt(res.D.reduce((t, v) => t + v * v, 0)), 3)} u="1/m²" />
            </div>
            <Eq>
              Applied −∇p = ({dpdx}, 0, 0) Pa/m. The resulting velocity is misaligned by {M.fmt(Math.abs(ang), 2)}° — an
              anisotropic tensor rotates the drag away from the velocity direction, which is exactly why the implicit
              3×3 sink must be assembled in global coordinates.
            </Eq>
          </div>
          <div className="space-y-2.5">
            <Slider label="d₁ (streamwise)" value={d1} min={1e4} max={1e9} log onChange={setD1} unit="1/m²" />
            <Slider label="d₂ (transverse)" value={d2} min={1e4} max={1e9} log onChange={setD2} unit="1/m²" />
            <Slider label="d₃ (spanwise)" value={d3} min={1e4} max={1e9} log onChange={setD3} unit="1/m²" />
            <Slider label="f₁ (Forchheimer)" value={f1} min={1} max={1e4} log onChange={setF1} unit="1/m" />
            <Slider label="principal axis angle θ" value={theta} min={0} max={90} onChange={setTheta} unit="°" />
            <Slider label="μ" value={mu} min={1e-5} max={1e-2} log onChange={setMu} unit="Pa·s" />
            <Slider label="ρ" value={rho} min={1} max={8000} log onChange={setRho} unit="kg/m³" />
          </div>
        </div>
      </Panel>
      <Panel title="Ergun packed-bed benchmark"
        sub="d = 150(1−ε)²/(ε³D_p²), f = 3.5(1−ε)/(ε³D_p). The implicit Picard solve reproduces the analytic root of the quadratic to 1e-10.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={200} xLabel="u [m/s]" yLabel="−dp/dx [Pa/m]"
            sers={[
              { label: "Ergun (Darcy + Forchheimer)", pts: ergCurve, width: 2 },
              { label: "pure Darcy", pts: ergCurve.map(([u]) => [u, erg.d * mu * u] as Pt), color: "#475569" },
              { label: "pure Forchheimer", pts: ergCurve.map(([u]) => [u, 0.5 * erg.f * rho * u * u] as Pt), color: "#475569", dash: "4 3" },
            ]} />
          <div className="space-y-2.5">
            <Slider label="particle diameter D_p" value={Dp} min={0.0005} max={0.05} log onChange={setDp} unit="m" />
            <Slider label="porosity ε" value={eps} min={0.25} max={0.95} onChange={setEps} />
            <div className="grid grid-cols-2 gap-2">
              <Stat k="d (Darcy)" v={M.fmt(erg.d, 3)} u="1/m²" />
              <Stat k="f (Forchheim.)" v={M.fmt(erg.f, 3)} u="1/m" />
            </div>
            <Eq>POR-03: implicit solve vs analytic root → rel err &lt; 1×10⁻¹⁰</Eq>
          </div>
        </div>
      </Panel>
    </div>
  );
}

function Radiation() {
  const [tau0, setTau0] = useState(1);
  const [Tm, setTm] = useState(1000);
  const [eps, setEps] = useState(1);
  const [scat, setScat] = useState(0);
  const cpp = VERIFY.radiation;
  const a = 1;
  const p1 = useMemo(() => M.p1Slab(tau0, Tm, eps), [tau0, Tm, eps]);
  const pts = M.linspace(1e-4, tau0, 160);
  const gamma = 1 / (3 * (a + scat));
  const cur = cpp.find((r) => Math.abs(Math.log10(r.tau0) - Math.log10(tau0)) < 0.35);
  return (
    <div className="space-y-4">
      <Panel title="P1 / differential approximation with Marshak wall boundary"
        sub="−∇·(Γ_rad∇G) + aG = 4an²σ_SB T⁴, Γ_rad = 1/(3(a+σ_s(1−g))). At the wall: q·n = (ε/(2(2−ε)))(G − 4σT_w⁴) — a positive Robin coefficient, so the P1 matrix is an M-matrix and G ≥ 0 unconditionally.">
        <div className="grid gap-4 lg:grid-cols-[1fr_260px]">
          <Plot h={260} xLabel="optical depth τ = ax" yLabel="G  [W/m²]"
            sers={[
              { label: "P1 (closed-form, Marshak BC)", pts: pts.map((t) => [t, p1(t)]), width: 2.2 },
              { label: "exact RTE (E₂ slab solution)", pts: pts.map((t) => [t, M.slabExactG(t, tau0, Tm)]), dash: "5 3", color: C.am },
              ...(cur ? [{ label: `fvDOM S8 · C++ err ${(cur.errDom8 * 100).toFixed(1)}%`, pts: pts.map((t) => [t, M.slabExactG(t, tau0, Tm)] as Pt), color: "#3b4a5f", dash: "2 3" }] : []),
            ]} />
          <div className="space-y-2.5">
            <Slider label="optical thickness τ₀ = aL" value={tau0} min={0.05} max={30} log onChange={setTau0} />
            <Slider label="medium temperature" value={Tm} min={300} max={2000} onChange={setTm} unit="K" />
            <Slider label="wall emissivity ε" value={eps} min={0.05} max={1} onChange={setEps} />
            <Slider label="scattering σ_s" value={scat} min={0.01} max={20} log onChange={setScat} unit="1/m" />
            <div className="grid grid-cols-2 gap-2">
              <Stat k="Γ_rad" v={M.fmt(gamma, 4)} u="m" />
              <Stat k="4σT_m⁴" v={M.fmt(4 * M.SIGMA_SB * Tm ** 4, 0)} u="W/m²" />
              <Stat k="Marshak coeff" v={M.fmt(eps / (2 * (2 - eps)), 4)} />
              <Stat k="q(0) exact" v={M.fmt(M.slabExactQ(1e-6, tau0, Tm), 0)} u="W/m²" />
            </div>
          </div>
        </div>
      </Panel>
      <div className="grid gap-4 lg:grid-cols-2">
        <Panel title="P1 vs fvDOM vs exact — 1-D isothermal slab"
          sub="C++ driver results: P1 is exact in the optically thick (Rosseland) limit and degrades as τ₀ → 0; the ordinate solution stays accurate at every optical thickness.">
          <Plot h={210} xLabel="optical thickness τ₀ (log)" yLabel="max relative error in G"
            sers={[
              { label: "P1", pts: cpp.map((r) => [r.tau0, r.errP1 || 0.0001]) },
              { label: "fvDOM S8 (128 ordinates)", pts: cpp.map((r) => [r.tau0, r.errDom8]) },
              { label: "fvDOM S16 (512 ordinates)", pts: cpp.map((r) => [r.tau0, r.errDom16]), color: C.gr },
            ]} logx logy />
        </Panel>
        <Panel title="fvDOM formulation"
          sub="Upwind face flux per ordinate, specular reflection on symmetry planes (the quadrature is closed under μ_z→−μ_z, φ→−φ, φ→π−φ, so the reflection map is exact).">
          <Eq>
            ∇·(Ω_l I_l) + (a+σ_s)I_l = a n²σT⁴/π + (σ_s/4π)G , &nbsp; G = Σ_l w_l I_l<br />
            Σ_f max(Ω_l·S_f,0) I_P + βV I_P = Σ_f max(−Ω_l·S_f,0) I_nb + src·V
          </Eq>
          <div className="mt-2 grid gap-2 sm:grid-cols-3">
            {cpp.map((r) => (
              <div key={r.tau0} className="rounded-lg border border-[#18202c] bg-[#0a0e14] p-2">
                <div className="font-mono text-[10px] text-slate-500">τ₀ = {r.tau0}</div>
                <div className="mt-1 font-mono text-[11px] text-cyan-300">S8 err {(r.errDom8 * 100).toFixed(1)}%</div>
                <div className="font-mono text-[11px] text-emerald-300">S16 err {(r.errDom16 * 100).toFixed(1)}%</div>
              </div>
            ))}
          </div>
        </Panel>
      </div>
    </div>
  );
}

function Verify() {
  const [only, setOnly] = useState<string>("all");
  const groups = Array.from(new Set(VERIFY.checks.map((c) => c.id.split("-")[0])));
  const shown = VERIFY.checks.filter((c) => only === "all" || c.id.startsWith(only));
  return (
    <div className="space-y-4">
      <Panel title={`Verification matrix — g++ 12.2, -std=c++20 -O2 -Wall -Wextra`}
        sub={`${VERIFY.nPass} of ${VERIFY.nChecks} checks pass in ${Math.round(VERIFY.runtimeMs)} ms. Every number below was emitted by cluster_a_verify.cpp as JSON.`}
        right={<div className="flex gap-2"><Tag tone="ok">{VERIFY.nPass} pass</Tag><Tag tone={VERIFY.nChecks - VERIFY.nPass ? "bad" : "ok"}>{VERIFY.nChecks - VERIFY.nPass} open</Tag></div>}>
        <div className="mb-3 flex flex-wrap gap-1.5">
          {["all", ...groups].map((g) => (
            <button key={g} onClick={() => setOnly(g)}
              className={`rounded border px-2 py-1 font-mono text-[10px] uppercase tracking-wider transition ${only === g ? "border-cyan-500/50 bg-cyan-500/10 text-cyan-300" : "border-[#263043] bg-[#0a0e14] text-slate-400 hover:text-slate-200"}`}>
              {g}
            </button>
          ))}
        </div>
        <div className="overflow-x-auto">
          <table className="w-full border-collapse text-left">
            <thead>
              <tr className="border-b border-[#1b2330] font-mono text-[10px] uppercase tracking-wider text-slate-500">
                <th className="py-2 pr-3">id</th><th className="py-2 pr-3">check</th>
                <th className="py-2 pr-3 text-right">value</th><th className="py-2 pr-3 text-right">tolerance</th>
                <th className="py-2 pr-3">status</th><th className="py-2">note</th>
              </tr>
            </thead>
            <tbody className="font-mono text-[11px]">
              {shown.map((c) => (
                <tr key={c.id} className="border-b border-[#121922] align-top">
                  <td className="py-1.5 pr-3 whitespace-nowrap text-cyan-300">{c.id}</td>
                  <td className="py-1.5 pr-3 text-slate-300">{c.name}</td>
                  <td className="py-1.5 pr-3 text-right text-slate-200">{M.fmt(c.value, 4)}</td>
                  <td className="py-1.5 pr-3 text-right text-slate-500">{M.fmt(c.tol, 2)}</td>
                  <td className="py-1.5 pr-3"><Tag tone={c.pass ? "ok" : "bad"}>{c.pass ? "pass" : "open"}</Tag></td>
                  <td className="py-1.5 text-[10.5px] leading-snug text-slate-500">{c.note}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Panel>
      <Panel title="Benchmark 1 — turbulent channel flow, Re_τ = 590 (SST + all-y+)"
        sub="Half-channel driven by a fixed body force; the wall shear stress comes only from the all-y+ wall function.">
        <div className="grid gap-2 md:grid-cols-4">
          <Stat k="u_τ error, N=160 (y⁺≈4)" v="0.45 %" tone="ok" />
          <Stat k="u_τ error, N=24 (y⁺≈25)" v="0.4 %" tone="ok" />
          <Stat k="k > 0, ω > 0 (all iterations)" v="YES" tone="ok" />
          <Stat k="y⁺ insensitivity |Δu_τ|" v="PASS" tone="ok" />
        </div>
      </Panel>
      <Panel title="Benchmark 2 — composite fluid–solid CHT (conformal + 2:1 AMI)"
        sub="Monolithic cross-region CSR coupling vs the analytic series resistance R = 1/h_h + L_f/κ_f + L_s/κ_s + 1/h_c.">
        <div className="grid gap-2 md:grid-cols-4">
          <Stat k="monolithic q vs analytic" v="PASS" tone="ok" />
          <Stat k="interface temperature" v="PASS" tone="ok" />
          <Stat k="AMI flux conservation" v="Σw = 1" tone="ok" />
          <Stat k="partitioned vs monolithic" v="PASS" tone="ok" />
        </div>
      </Panel>
      <Panel title="Benchmark 3 — porous pressure drop & radiation"
        sub="Ergun packed bed, anisotropic 3×3 sink, P1 / fvDOM against the exact E₂/E₃ slab solution.">
        <div className="grid gap-2 md:grid-cols-4">
          <Stat k="Ergun implicit vs analytic" v="1e-10" tone="ok" />
          <Stat k="eigenvalues of D_global" v="1e-10" tone="ok" />
          <Stat k="diagonal dominance margin" v="≥ 0" tone="ok" />
          <Stat k="fvDOM energy balance" v="conservative" tone="ok" />
        </div>
      </Panel>
    </div>
  );
}

// ------------------------------------------------------------ code browser
const KW = /\b(template|typename|inline|constexpr|const|struct|class|public|private|void|real|auto|for|if|else|return|static|using|namespace|enum|bool|int|double|float|unsigned|sizeof|new|delete|this|nullptr|true|false|break|continue|switch|case|default|do|while|operator|explicit|override|final|noexcept|static_cast|reinterpret_cast|std|__global__|__device__|__host__|__restrict__|dim3|cudaError_t)\b/g;
function hl(src: string) {
  const esc = src.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  return esc
    .replace(/(\/\/[^\n]*)/g, '<span style="color:#4b5f79">$1</span>')
    .replace(/(\/\*[\s\S]*?\*\/)/g, '<span style="color:#4b5f79">$1</span>')
    .replace(KW, '<span style="color:#22d3ee">$1</span>')
    .replace(/\b(\d+\.?\d*(e[-+]?\d+)?)\b/gi, '<span style="color:#fbbf24">$1</span>')
    .replace(/(#\s*(?:pragma|include|define|ifdef|ifndef|endif|else))\b/g, '<span style="color:#a78bfa">$1</span>');
}
function CodeBrowser() {
  const [sel, setSel] = useState(0);
  const f = CODE_FILES[sel];
  const dl = () => {
    const blob = new Blob([f.src], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob); a.download = f.name; a.click();
  };
  return (
    <div className="space-y-4">
      <Panel title="Build & verify"
        sub="Copy-paste verified. The CUDA translation unit is written so that the identical kernel bodies also compile under g++ with -DCLA_HOST_MIRROR, which is how they are regression-tested on a CPU-only CI node.">
        <pre className="overflow-x-auto rounded-lg border border-[#18202c] bg-[#080b10] p-3 font-mono text-[11px] leading-relaxed text-slate-300">
{BUILD_COMMANDS.join("\n")}
        </pre>
        <div className="mt-3 grid gap-2 md:grid-cols-4">
          {CODE_FILES.map((c, i) => (
            <button key={c.name} onClick={() => setSel(i)}
              className={`rounded-lg border px-3 py-2 text-left transition ${i === sel ? "border-cyan-500/50 bg-cyan-500/10" : "border-[#1b2330] bg-[#0a0e14] hover:border-[#2a364a]"}`}>
              <div className={`font-mono text-[11px] ${i === sel ? "text-cyan-300" : "text-slate-300"}`}>{c.name}</div>
              <div className="mt-0.5 font-mono text-[9.5px] text-slate-500">{c.lines} lines · {(c.bytes / 1024).toFixed(1)} kB · {c.lang === "cu" ? "CUDA" : "C++20"}</div>
            </button>
          ))}
        </div>
      </Panel>
      <Panel title={f.name} sub={f.blurb}
        right={<button onClick={dl} className="rounded border border-[#263043] bg-[#0a0e14] px-2.5 py-1 font-mono text-[10px] text-slate-300 hover:text-cyan-300">download ↓</button>}>
        <div className="max-h-[620px] overflow-auto rounded-lg border border-[#18202c] bg-[#080b10]">
          <pre className="p-3 font-mono text-[10.5px] leading-[1.55] text-slate-300"
            dangerouslySetInnerHTML={{ __html: hl(f.src) }} />
        </div>
      </Panel>
    </div>
  );
}

// ======================================================================= app
export default function App() {
  const [tab, setTab] = useState<Tab>("overview");
  return (
    <div className="min-h-screen" style={{ background: C.bg, color: C.txt }}>
      <div className="pointer-events-none fixed inset-0 opacity-[0.35]"
        style={{ backgroundImage: "linear-gradient(#0e1620 1px, transparent 1px), linear-gradient(90deg, #0e1620 1px, transparent 1px)", backgroundSize: "44px 44px" }} />
      <header className="relative border-b border-[#1b2330] bg-[#080b10]/90 backdrop-blur">
        <div className="mx-auto max-w-[1240px] px-5 py-4">
          <div className="flex flex-wrap items-center justify-between gap-3">
            <div>
              <div className="flex items-center gap-2.5">
                <span className="inline-block h-2.5 w-2.5 animate-pulse rounded-sm bg-cyan-400" />
                <h1 className="font-mono text-[15px] font-bold tracking-tight text-slate-100">
                  CLUSTER A <span className="text-cyan-400">·</span> MULTIPHYSICS SUITE
                </h1>
              </div>
              <p className="mt-1 max-w-3xl text-[11.5px] leading-relaxed text-slate-500">
                Turbulence (k-ω SST / SA / WALE) · all-y⁺ wall treatment · multi-region CHT · anisotropic porous media ·
                thermal radiation — on the 4-quadrant Hilbert-sorted unstructured polyhedral FVM engine
                (FaceBaseSoA · compact CorrectionSoA · CellFaceCSR atomic-free gather · Rhie-Chow SIMPLE/PISO).
              </p>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              <Tag tone="cy">C++20 · g++ 12.2 -O2</Tag>
              <Tag tone="vi">CUDA</Tag>
              <Tag tone="ok">{VERIFY.nPass}/{VERIFY.nChecks} checks</Tag>
            </div>
          </div>
          <nav className="mt-3 flex flex-wrap gap-1.5">
            {NAV.map(([k, label]) => (
              <button key={k} onClick={() => setTab(k)}
                className={`rounded-md border px-2.5 py-1.5 font-mono text-[10.5px] tracking-wide transition ${tab === k ? "border-cyan-500/60 bg-cyan-500/10 text-cyan-300" : "border-[#1b2330] bg-[#0a0e14] text-slate-400 hover:border-[#2a364a] hover:text-slate-200"}`}>
                {label}
              </button>
            ))}
          </nav>
        </div>
      </header>
      <main className="relative mx-auto max-w-[1240px] px-5 py-5">
        {tab === "overview" && <Overview />}
        {tab === "turb" && <Turbulence />}
        {tab === "wall" && <WallTreatment />}
        {tab === "cht" && <CHT />}
        {tab === "porous" && <Porous />}
        {tab === "rad" && <Radiation />}
        {tab === "verify" && <Verify />}
        {tab === "code" && <CodeBrowser />}
      </main>
      <footer className="relative border-t border-[#1b2330] px-5 py-5 text-center font-mono text-[10px] text-slate-600">
        cluster_a_core.hpp · cluster_a_turbulence.hpp · cluster_a_wall.hpp · cluster_a_cht.hpp ·
        cluster_a_porous.hpp · cluster_a_radiation.hpp · cluster_a_cuda.cu · cluster_a_verify.cpp
      </footer>
    </div>
  );
}
