import { useMemo, useState } from "react";
import { Card, LineChart, Legend, Slider, Stat, type Series } from "./components/ui";
import {
  C, compositeFluxAnalytic, forchheimerU, frictionVelocity, kwHistory, matVec, omegaP, orientedTensor,
  p1Analytic, partitionedRobin, rotZ, sinkTensor, solveLayers, solveP1, sstBeta, sstF1, sstF2, sstGamma, sstNut,
  tPlus, updateK, updateOmega, uPlus, waleNuSgs, type Layer, type Bnd, kaderGamma,
} from "./lib/clusterA";

type TabId = "wall" | "cht" | "porous" | "turb" | "p1" | "verify";
const TABS: { id: TabId; label: string; hint: string }[] = [
  { id: "wall", label: "All-y+ wall", hint: "Kader blending · u_tau · omega_P" },
  { id: "cht", label: "Multi-region CHT", hint: "Harmonic interface · monolithic / Robin-Robin" },
  { id: "porous", label: "Porous Darcy-Forch.", hint: "Anisotropic D = R diag R^T" },
  { id: "turb", label: "SST & WALE", hint: "Patankar positivity · WALE" },
  { id: "p1", label: "P1 radiation", hint: "Marshak walls · order of accuracy" },
  { id: "verify", label: "Verification", hint: "Analytic & invariant test suite" },
];

const Mono = ({ children }: { children: React.ReactNode }) => (
  <span className="font-mono text-xs text-slate-700">{children}</span>
);

// ============================================================ 1. All-y+ wall
function WallTab() {
  const [nu, setNu] = useState(1.5e-5);
  const [yP, setYP] = useState(1e-4);
  const [Up, setUp] = useState(10);
  const [Pr] = useState(0.71);
  const [dT, setDT] = useState(10);
  const rho = 1.2;
  const cp = 1005;

  const r = useMemo(() => {
    const ut = frictionVelocity(Up, yP, nu);
    const yp = (ut * yP) / nu;
    return {
      ut,
      yp,
      tau: rho * ut * ut,
      qw: (rho * cp * ut * dT) / Math.max(tPlus(yp, Pr), 1e-300),
      wP: omegaP(yP, nu, ut),
      uplus: uPlus(yp),
      gamma: kaderGamma(yp),
    };
  }, [nu, yP, Up, dT, Pr]);

  const series: Series[] = useMemo(() => {
    const pts = Array.from({ length: 200 }, (_, i) => 10 ** (-1 + (3.7 * i) / 199));
    return [
      { name: "All-y+ (Kader) u+", color: "#4f46e5", points: pts.map((y) => [y, uPlus(y)]) },
      { name: "Viscous u+ = y+", color: "#64748b", dashed: true, points: pts.filter((y) => y <= 12).map((y) => [y, y]) },
      {
        name: "Log law (1/κ) ln(E y+)",
        color: "#059669",
        dashed: true,
        points: pts.filter((y) => y >= 3).map((y) => [y, Math.log(C.Elog * y) / C.kappa]),
      },
      { name: "Current state", color: "#e11d48", points: [[r.yp, r.uplus]], markers: true },
    ];
  }, [r]);

  return (
    <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
      <Card title="Wall-face inputs" subtitle="Cell-centred y_P from (x_P - x_f)·S_f/|S_f|">
        <div className="space-y-4">
          <Slider label="Wall distance y_P [m]" value={yP} min={-6} max={-1} step={0.01} log onChange={setYP} format={(v) => v.toExponential(2)} />
          <Slider label="Near-wall velocity U_P [m/s]" value={Up} min={0.5} max={60} step={0.1} onChange={setUp} />
          <Slider label="Kinematic viscosity ν [m²/s]" value={nu} min={-6} max={-4} step={0.01} log onChange={setNu} format={(v) => v.toExponential(2)} />
          <Slider label="Wall temperature difference ΔT [K]" value={dT} min={0} max={50} step={0.5} onChange={setDT} />
        </div>
      </Card>
      <div className="space-y-6">
        <Card title="Blended wall state" subtitle="Solved by monotone bisection on u_tau·u+(u_tau y/ν) = U_P">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="u_tau [m/s]" value={r.ut.toFixed(4)} />
            <Stat label="y+" value={r.yp.toFixed(3)} tone={r.yp >= 0.1 && r.yp <= 500 ? "ok" : "bad"} />
            <Stat label="τ_w [Pa]" value={r.tau.toFixed(4)} />
            <Stat label="q_w [W/m²]" value={r.qw.toFixed(2)} />
            <Stat label="ω_P [1/s]" value={r.wP.toExponential(3)} />
            <Stat label="Γ(y+) Kader" value={r.gamma.toFixed(4)} />
            <Stat label="u+(y+)" value={r.uplus.toFixed(3)} />
            <Stat label="Pr / Prt" value={`${Pr} / ${C.Prt}`} />
          </div>
        </Card>
        <Card title="u+ vs y+ — Kader blend between sublayer and log layer">
          <LineChart series={series} xLog xLabel="y+ (log)" yLabel="u+" />
          <Legend series={series} />
          <p className="mt-3 text-xs text-slate-500">
            The blend recovers u+ = y+ as y+→0 and the log law (1/κ)ln(E y+) for y+ ≳ 100, with a smooth transition in
            between — the wall function stays valid for 0.1 ≤ y+ ≤ 500 without switching.
          </p>
        </Card>
      </div>
    </div>
  );
}

// ============================================================ 2. CHT
function CHTTab() {
  const [kf, setKf] = useState(0.6);
  const [Lf, setLf] = useState(0.02);
  const [nf, setNf] = useState(40);
  const [ks, setKs] = useState(50);
  const [Ls, setLs] = useState(0.005);
  const [ns, setNs] = useState(20);
  const [hL, setHL] = useState(500);
  const [hR, setHR] = useState(100);
  const [TinfL, setTinfL] = useState(350);
  const [TinfR, setTinfR] = useState(300);
  const [gFac, setGFac] = useState(1);
  const [omega, setOmega] = useState(1);

  const r = useMemo(() => {
    const layers: Layer[] = [
      { L: Lf, k: kf, n: Math.round(nf) },
      { L: Ls, k: ks, n: Math.round(ns) },
    ];
    const left: Bnd = { h: hL, Tinf: TinfL };
    const right: Bnd = { h: hR, Tinf: TinfR };
    const qA = compositeFluxAnalytic(layers, left, right);
    const sol = solveLayers(layers, left, right);
    const nFluid = layers[0].n;
    const gamma = gFac / (0.5 * (Lf / kf + Ls / ks));
    const part = partitionedRobin(layers[0], layers[1], left, right, gamma, omega, 1e-12, 4000);
    const TsurfL = TinfL - qA / hL;
    const Ti = TsurfL - (qA * Lf) / kf;
    // analytic piecewise-linear profile
    const xs = sol.x;
    const Tan = xs.map((x) => (x < Lf ? TsurfL - (qA * x) / kf : Ti - (qA * (x - Lf)) / ks));
    // harmonic interface temperature from the last fluid and first solid cell
    const dxf = Lf / nFluid;
    const dxs = Ls / layers[1].n;
    const a = kf * (2 / dxf);
    const b = ks * (2 / dxs);
    const TiFVM = (a * sol.T[nFluid - 1] + b * sol.T[nFluid]) / (a + b);
    return {
      qA,
      q: sol.q,
      errQ: Math.abs(sol.q - qA) / Math.abs(qA),
      Ti,
      TiFVM,
      sol,
      Tan,
      part,
      nFluid,
    };
  }, [kf, Lf, nf, ks, Ls, ns, hL, hR, TinfL, TinfR, gFac, omega]);

  const series: Series[] = [
    { name: "FVM monolithic T(x)", color: "#4f46e5", points: r.sol.x.map((x, i) => [x * 1000, r.sol.T[i]]), markers: r.sol.x.length < 80 },
    { name: "Analytic composite", color: "#f59e0b", dashed: true, points: r.sol.x.map((x, i) => [x * 1000, r.Tan[i]]) },
  ];

  return (
    <div className="grid gap-6 lg:grid-cols-[340px_1fr]">
      <Card title="Fluid / solid composite" subtitle="Convective fluid-side and solid-side boundaries">
        <div className="space-y-3">
          <div className="text-xs font-semibold uppercase tracking-wide text-slate-500">Fluid region</div>
          <Slider label="k_f [W/m/K]" value={kf} min={0.05} max={5} step={0.01} onChange={setKf} />
          <Slider label="L_f [mm]" value={Lf * 1000} min={2} max={60} step={0.5} onChange={(v) => setLf(v / 1000)} />
          <Slider label="Fluid cells n_f" value={nf} min={5} max={120} step={1} onChange={setNf} />
          <div className="text-xs font-semibold uppercase tracking-wide text-slate-500">Solid region</div>
          <Slider label="k_s [W/m/K]" value={ks} min={0.5} max={400} step={0.5} onChange={setKs} />
          <Slider label="L_s [mm]" value={Ls * 1000} min={1} max={30} step={0.5} onChange={(v) => setLs(v / 1000)} />
          <Slider label="Solid cells n_s" value={ns} min={5} max={120} step={1} onChange={setNs} />
          <div className="text-xs font-semibold uppercase tracking-wide text-slate-500">Boundaries</div>
          <Slider label="h_left [W/m²K]" value={hL} min={10} max={2000} step={5} onChange={setHL} />
          <Slider label="T∞ left [K]" value={TinfL} min={280} max={450} step={1} onChange={setTinfL} />
          <Slider label="h_right [W/m²K]" value={hR} min={10} max={2000} step={5} onChange={setHR} />
          <Slider label="T∞ right [K]" value={TinfR} min={250} max={400} step={1} onChange={setTinfR} />
          <div className="text-xs font-semibold uppercase tracking-wide text-slate-500">Partitioned coupling</div>
          <Slider label="Robin γ factor" value={gFac} min={0.1} max={10} step={0.05} onChange={setGFac} format={(v) => v.toFixed(2)} />
          <Slider label="Relaxation ω_r" value={omega} min={0.1} max={1} step={0.01} onChange={setOmega} format={(v) => v.toFixed(2)} />
        </div>
      </Card>
      <div className="space-y-6">
        <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
          <Stat label="q analytic [W/m²]" value={r.qA.toFixed(4)} />
          <Stat label="q monolithic [W/m²]" value={r.q.toFixed(4)} />
          <Stat label="relative error" value={r.errQ.toExponential(2)} tone={r.errQ < 1e-9 ? "ok" : "bad"} />
          <Stat label="T_interface (formula)" value={`${r.TiFVM.toFixed(4)} K`} />
          <Stat label="T_interface (analytic)" value={`${r.Ti.toFixed(4)} K`} />
          <Stat label="Robin-Robin sweeps" value={`${r.part.iterations}${r.part.converged ? "" : " (no conv.)"}`} tone={r.part.converged ? "ok" : "bad"} />
          <Stat label="Partitioned flux error" value={Math.abs(r.part.q - r.q).toExponential(2)} />
          <Stat label="Interface mismatch" value={r.part.mismatch.toExponential(2)} />
        </div>
        <Card title="Temperature through the composite">
          <LineChart series={series} xLabel="x [mm]" yLabel="T [K]" />
          <Legend series={series} />
        </Card>
        <Card title="Robin-Robin convergence" subtitle="Interface temperature mismatch per partitioned sweep (log scale)">
          <LineChart
            series={[{ name: "|T_f,b − T_s,b|", color: "#e11d48", points: r.part.history.map((v, i) => [i + 1, v]), markers: true }]}
            yLog
            xLabel="sweep"
            yLabel="mismatch [K]"
            height={220}
          />
        </Card>
      </div>
    </div>
  );
}

// ============================================================ 3. Porous
function PorousTab() {
  const [mu, setMu] = useState(1.8e-5);
  const [rho, setRho] = useState(1.2);
  const [inv1, setInv1] = useState(1e7);
  const [inv2, setInv2] = useState(1e6);
  const [inv3, setInv3] = useState(1e5);
  const [c1, setC1] = useState(100);
  const [c2, setC2] = useState(200);
  const [c3, setC3] = useState(300);
  const [angle, setAngle] = useState(30);
  const [U, setU] = useState(0.8);
  const [len, setLen] = useState(1);

  const r = useMemo(() => {
    const R = rotZ((angle * Math.PI) / 180);
    const D = orientedTensor(R, [inv1, inv2, inv3]);
    const F = orientedTensor(R, [c1, c2, c3]);
    const e1 = [R[0][0], R[1][0], R[2][0]];
    const u = e1.map((v) => v * U);
    const sink = sinkTensor(D, F, mu, rho, u);
    const gp = matVec(sink, u);
    const dp = len * (gp[0] * e1[0] + gp[1] * e1[1] + gp[2] * e1[2]);
    const expected = len * (mu * inv1 * U + 0.5 * rho * c1 * U * U);
    const sweepTensor = (axis: 1 | 2 | 3) => {
      const ax = [R[0][axis - 1], R[1][axis - 1], R[2][axis - 1]];
      return Array.from({ length: 50 }, (_, i) => {
        const v = (i + 1) * 0.1;
        const uu = ax.map((x) => x * v);
        const sk = sinkTensor(D, F, mu, rho, uu);
        const gg = matVec(sk, uu);
        return [v, len * (gg[0] * ax[0] + gg[1] * ax[1] + gg[2] * ax[2])] as [number, number];
      });
    };
    return { D, F, dp, expected, sweep1: sweepTensor(1), sweep2: sweepTensor(2), eps: Math.abs(dp - expected) / Math.abs(expected), u: U };
  }, [mu, rho, inv1, inv2, inv3, c1, c2, c3, angle, U, len]);

  const series: Series[] = [
    { name: "Axis 1 (K1, C2₁)", color: "#4f46e5", points: r.sweep1 },
    { name: "Axis 2 (K2, C2₂)", color: "#0891b2", points: r.sweep2 },
    { name: "Darcy-only axis 1", color: "#64748b", dashed: true, points: r.sweep1.map(([v]) => [v, mu * inv1 * v * len]) },
    { name: "Operating point", color: "#e11d48", points: [[r.u, r.dp]], markers: true },
  ];

  const fmtM = (m: number[][]) => m.map((row) => row.map((v) => v.toExponential(2)).join("  ")).join("\n");

  return (
    <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
      <Card title="Anisotropic porous zone" subtitle="Local principal values rotated into the global frame">
        <div className="space-y-3">
          <Slider label="μ [Pa·s]" value={mu} min={-6} max={-3} step={0.01} log onChange={setMu} format={(v) => v.toExponential(2)} />
          <Slider label="ρ [kg/m³]" value={rho} min={0.5} max={5} step={0.05} onChange={setRho} format={(v) => v.toFixed(2)} />
          <Slider label="Principal rotation θ [°]" value={angle} min={0} max={90} step={1} onChange={setAngle} format={(v) => v.toFixed(0)} />
          <Slider label="1/K₁ [1/m²]" value={inv1} min={5} max={8} step={0.01} log onChange={setInv1} format={(v) => v.toExponential(1)} />
          <Slider label="1/K₂ [1/m²]" value={inv2} min={5} max={8} step={0.01} log onChange={setInv2} format={(v) => v.toExponential(1)} />
          <Slider label="1/K₃ [1/m²]" value={inv3} min={5} max={8} step={0.01} log onChange={setInv3} format={(v) => v.toExponential(1)} />
          <Slider label="C₂₁ [1/m]" value={c1} min={1} max={1000} step={1} onChange={setC1} format={(v) => v.toFixed(0)} />
          <Slider label="C₂₂ [1/m]" value={c2} min={1} max={1000} step={1} onChange={setC2} format={(v) => v.toFixed(0)} />
          <Slider label="C₂₃ [1/m]" value={c3} min={1} max={1000} step={1} onChange={setC3} format={(v) => v.toFixed(0)} />
          <Slider label="Superficial velocity U [m/s]" value={U} min={0.05} max={5} step={0.01} onChange={setU} format={(v) => v.toFixed(2)} />
          <Slider label="Zone length L [m]" value={len} min={0.1} max={5} step={0.05} onChange={setLen} format={(v) => v.toFixed(2)} />
        </div>
      </Card>
      <div className="space-y-6">
        <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
          <Stat label="Δp (FVM sink) [Pa]" value={r.dp.toFixed(3)} />
          <Stat label="Δp analytic [Pa]" value={r.expected.toFixed(3)} />
          <Stat label="relative error" value={r.eps.toExponential(2)} tone={r.eps < 1e-10 ? "ok" : "bad"} />
          <Stat label="Re-check: Forchheimer u" value={forchheimerU(r.dp / len, mu / 1e7, 0.5 * rho * 100).toFixed(4)} />
        </div>
        <Card title="Pressure drop vs superficial velocity" subtitle="Δp = L · (μD + ½ρ|u|F)u · û, evaluated per principal axis">
          <LineChart series={series} xLabel="U [m/s]" yLabel="Δp [Pa]" />
          <Legend series={series} />
        </Card>
        <Card title="Global tensors" subtitle="D = R diag(1/K) Rᵀ and F = R diag(C₂) Rᵀ">
          <div className="grid gap-4 md:grid-cols-2">
            <pre className="overflow-x-auto rounded-xl bg-slate-900 p-3 text-xs text-emerald-200">{"D [1/m²]\n" + fmtM(r.D)}</pre>
            <pre className="overflow-x-auto rounded-xl bg-slate-900 p-3 text-xs text-sky-200">{"F [1/m]\n" + fmtM(r.F)}</pre>
          </div>
        </Card>
      </div>
    </div>
  );
}

// ============================================================ 4. SST & WALE
function TurbTab() {
  const [dt, setDt] = useState(0.5);
  const [S2, setS2] = useState(4);
  const [k0, setK0] = useState(1);
  const [w0, setW0] = useState(10);
  const [steps, setSteps] = useState(40);
  const [shear, setShear] = useState(2);
  const [strain, setStrain] = useState(1);
  const [delta, setDelta] = useState(0.05);

  const hist = useMemo(() => {
    const ex = kwHistory("explicit", k0, w0, dt, S2, Math.round(steps));
    const im = kwHistory("patankar", k0, w0, dt, S2, Math.round(steps));
    const t = (i: number) => i * dt;
    return {
      kEx: ex.ks.map((v, i) => [t(i), v] as [number, number]),
      kIm: im.ks.map((v, i) => [t(i), v] as [number, number]),
      wEx: ex.ws.map((v, i) => [t(i), v] as [number, number]),
      wIm: im.ws.map((v, i) => [t(i), v] as [number, number]),
      minEx: Math.min(...ex.ks, ...ex.ws),
      minIm: Math.min(...im.ks, ...im.ws),
    };
  }, [dt, S2, k0, w0, steps]);

  const F1nw = useMemo(() => {
    const nu = 1e-6;
    const y = 1e-5;
    const w = (6 * nu) / (C.beta1 * y * y);
    const k = 1e-8;
    return sstF1(k, w, 1, nu, y, 1e-20);
  }, []);

  const wale = useMemo(() => {
    const g = [
      [0, shear, 0],
      [0, 0, 0],
      [0, 0, 0],
    ];
    const gs = [
      [strain, shear, 0],
      [0, -strain, 0],
      [0, 0, 0],
    ];
    const nuPure = waleNuSgs(g, delta);
    const nuMix = waleNuSgs(gs, delta);
    const curve: [number, number][] = Array.from({ length: 60 }, (_, i) => {
      const a = (i + 1) * 0.2;
      return [a, waleNuSgs([[a, shear, 0], [0, -a, 0], [0, 0, 0]], delta)];
    });
    return { nuPure, nuMix, curve };
  }, [shear, strain, delta]);

  const sST = useMemo(() => {
    const beta = sstBeta(F1nw);
    const gamma = sstGamma(F1nw);
    const F2 = sstF2(1e-8, (6 * 1e-6) / (C.beta1 * 1e-10), 1e-6, 1e-5);
    return { beta, gamma, F2, kNew: updateK(1, 10, 1, 1, dt, S2 * 0.1), wNew: updateOmega(10, 1, 1, dt, beta, gamma, S2) };
  }, [F1nw, dt, S2]);

  const tKEx: Series = { name: "Explicit k", color: "#e11d48", dashed: true, points: hist.kEx };
  const tKIm: Series = { name: "Patankar k (implicit)", color: "#4f46e5", points: hist.kIm };
  const tWEx: Series = { name: "Explicit ω", color: "#e11d48", dashed: true, points: hist.wEx };
  const tWIm: Series = { name: "Patankar ω (Newton)", color: "#4f46e5", points: hist.wIm };

  return (
    <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
      <Card title="Positivity stress test" subtitle="Same production / destruction, explicit vs Patankar">
        <div className="space-y-3">
          <Slider label="Time step Δt [s]" value={dt} min={-3} max={2} step={0.05} log onChange={setDt} format={(v) => v.toPrecision(3)} />
          <Slider label="Strain-rate² S² [1/s²]" value={S2} min={0} max={5} step={0.05} log onChange={setS2} format={(v) => v.toPrecision(3)} />
          <Slider label="Initial k₀ [m²/s²]" value={k0} min={-3} max={2} step={0.05} log onChange={setK0} format={(v) => v.toPrecision(3)} />
          <Slider label="Initial ω₀ [1/s]" value={w0} min={-1} max={4} step={0.05} log onChange={setW0} format={(v) => v.toPrecision(3)} />
          <Slider label="Steps" value={steps} min={5} max={120} step={1} onChange={setSteps} />
        </div>
        <div className="mt-5 grid grid-cols-2 gap-2">
          <Stat label="min(k,ω) explicit" value={hist.minEx.toExponential(2)} tone={hist.minEx > 0 ? "ok" : "bad"} />
          <Stat label="min(k,ω) Patankar" value={hist.minIm.toExponential(2)} tone={hist.minIm > 0 ? "ok" : "bad"} />
          <Stat label="F1 near wall" value={F1nw.toFixed(6)} tone={F1nw > 0.999 ? "ok" : "bad"} />
          <Stat label="β(F1) / γ(F1)" value={`${sST.beta.toFixed(4)} / ${sST.gamma.toFixed(4)}`} />
        </div>
      </Card>
      <div className="space-y-6">
        <div className="grid gap-6 md:grid-cols-2">
          <Card title="k(t)">
            <LineChart series={[tKEx, tKIm]} xLabel="t [s]" yLabel="k" height={240} />
            <Legend series={[tKEx, tKIm]} />
          </Card>
          <Card title="ω(t)">
            <LineChart series={[tWEx, tWIm]} xLabel="t [s]" yLabel="ω" height={240} />
            <Legend series={[tWEx, tWIm]} />
          </Card>
        </div>
        <Card title="LES WALE sub-grid viscosity" subtitle="ν_sgs = (CwΔ)² (Sᵈ:Sᵈ)^{3/2} / ((S:S)^{5/2} + (Sᵈ:Sᵈ)^{5/4})">
          <div className="grid gap-6 lg:grid-cols-[260px_1fr]">
            <div className="space-y-3">
              <Slider label="Shear γ̇ [1/s]" value={shear} min={0} max={10} step={0.05} onChange={setShear} format={(v) => v.toFixed(2)} />
              <Slider label="Strain a [1/s]" value={strain} min={0} max={10} step={0.05} onChange={setStrain} format={(v) => v.toFixed(2)} />
              <Slider label="Filter width Δ [m]" value={delta} min={-3} max={0} step={0.05} log onChange={setDelta} format={(v) => v.toPrecision(3)} />
              <Stat label="ν_sgs pure shear" value={wale.nuPure.toExponential(3)} tone={Math.abs(wale.nuPure) < 1e-12 ? "ok" : "default"} />
              <Stat label="ν_sgs shear + strain" value={wale.nuMix.toExponential(3)} />
            </div>
            <div>
              <LineChart
                series={[{ name: "ν_sgs(a) at fixed γ̇", color: "#0891b2", points: wale.curve }]}
                xLabel="strain a [1/s]"
                yLabel="ν_sgs [m²/s]"
                height={240}
              />
              <p className="mt-2 text-xs text-slate-500">
                WALE is zero for pure shear (the traceless square tensor vanishes), so the model is not dissipative in
                laminar shear: this is its key advantage over Smagorinsky.
              </p>
            </div>
          </div>
        </Card>
      </div>
    </div>
  );
}

// ============================================================ 5. P1 radiation
function P1Tab() {
  const [N, setN] = useState(40);
  const [a, setA] = useState(1);
  const [Tg, setTg] = useState(1000);
  const [Tw, setTw] = useState(600);
  const L = 1;

  const r = useMemo(() => {
    const n = Math.round(N);
    const T = new Array(n).fill(Tg);
    const G = solveP1(T, L, a, Tw, Tw);
    const h = L / n;
    const sG = 4 * C.sigmaSB * Tg ** 4;
    let emax = 0;
    const xs: number[] = [];
    const pts: [number, number][] = [];
    const an: [number, number][] = [];
    for (let i = 0; i < n; i++) {
      const x = (i + 0.5) * h;
      xs.push(x);
      pts.push([x, G[i]]);
      const ex = p1Analytic(x, L, a, Tg, Tw);
      an.push([x, ex]);
      emax = Math.max(emax, Math.abs(G[i] - ex) / sG);
    }
    const errAt = (m: number) => {
      const Tm = new Array(m).fill(Tg);
      const Gm = solveP1(Tm, L, a, Tw, Tw);
      const hm = L / m;
      const sGm = 4 * C.sigmaSB * Tg ** 4;
      let e = 0;
      for (let i = 0; i < m; i++) e = Math.max(e, Math.abs(Gm[i] - p1Analytic((i + 0.5) * hm, L, a, Tg, Tw)) / sGm);
      return e;
    };
    const conv = [10, 20, 40, 80].map((m) => ({ m, e: errAt(m) }));
    return { pts, an, emax, conv, sG, lam: Math.sqrt(3) * a };
  }, [N, a, Tg, Tw]);

  const series: Series[] = [
    { name: "P1 FVM G(x)", color: "#4f46e5", points: r.pts, markers: r.pts.length <= 40 },
    { name: "Analytic cosh solution", color: "#f59e0b", dashed: true, points: r.an },
  ];

  return (
    <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
      <Card title="1-D P1 slab" subtitle="−∇·(Γ∇G) + aG = 4aσT⁴,  Γ = 1/(3a),  Marshak walls">
        <div className="space-y-3">
          <Slider label="Cells N" value={N} min={10} max={200} step={1} onChange={setN} />
          <Slider label="Absorption a [1/m]" value={a} min={-1} max={0.7} step={0.01} log onChange={setA} format={(v) => v.toPrecision(3)} />
          <Slider label="Gas temperature T_g [K]" value={Tg} min={300} max={1500} step={10} onChange={setTg} />
          <Slider label="Wall temperature T_w [K]" value={Tw} min={300} max={1500} step={10} onChange={setTw} />
        </div>
        <div className="mt-5 grid grid-cols-2 gap-2">
          <Stat label="L∞ error / 4σT_g⁴" value={r.emax.toExponential(2)} tone={r.emax < 5e-3 ? "ok" : "bad"} />
          <Stat label="λ = √3 a [1/m]" value={r.lam.toFixed(3)} />
        </div>
      </Card>
      <div className="space-y-6">
        <Card title="Incident radiation G(x)">
          <LineChart series={series} xLabel="x [m]" yLabel="G [W/m²]" />
          <Legend series={series} />
        </Card>
        <Card title="Spatial convergence (Marshak walls)">
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="text-xs uppercase text-slate-500">
                <tr><th className="py-2">Cells</th><th>L∞ error</th><th>Observed order</th></tr>
              </thead>
              <tbody className="font-mono text-slate-800">
                {r.conv.map((c, i) => (
                  <tr key={c.m} className="border-t border-slate-100">
                    <td className="py-2">{c.m}</td>
                    <td>{c.e.toExponential(3)}</td>
                    <td>{i === 0 ? "—" : Math.log2(r.conv[i - 1].e / c.e).toFixed(3)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className="mt-3 text-xs text-slate-500">
            Expected second-order convergence. The Marshak condition 4σT_w⁴ = G_w − 2Γ∂G/∂n is eliminated exactly at the wall half-cell.
          </p>
        </Card>
      </div>
    </div>
  );
}

// ============================================================ 6. Verification
type CheckRow = { group: string; name: string; ok: boolean; value: number; tol: number };

function runSuite(): CheckRow[] {
  const rows: CheckRow[] = [];
  const add = (group: string, name: string, ok: boolean, value: number, tol: number) =>
    rows.push({ group, name, ok, value, tol });

  // SST blending
  {
    const nu = 1e-6, y = 1e-5, k = 1e-8;
    const w = (6 * nu) / (C.beta1 * y * y);
    const F1 = sstF1(k, w, 1, nu, y, 1e-20);
    const F2 = sstF2(k, w, nu, y);
    add("SST", "F1 → 1 in viscous sublayer", F1 > 0.999, F1, 1e-3);
    add("SST", "F2 → 1 in viscous sublayer", F2 > 0.999, F2, 1e-3);
    const Ff = sstF1(1e-4, 1, 1, 1e-5, 10, 1e-20);
    add("SST", "F1 → 0 in free stream", Ff < 1e-2, Ff, 1e-2);
  }
  // Positivity
  {
    let seed = 11;
    const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
    let minv = Infinity;
    for (let i = 0; i < 3000; i++) {
      const kO = 10 ** (-6 + 12 * rnd());
      const wO = 10 ** (-3 + 9 * rnd());
      const S2 = 10 ** (6 * rnd());
      const dt = 10 ** (9 * rnd());
      const nut = sstNut(kO, wO, Math.sqrt(S2), 1);
      const kN = updateK(kO, wO, 1, 1, dt, nut * S2);
      const wN = updateOmega(wO, 1, 1, dt, C.beta1, sstGamma(1), S2);
      minv = Math.min(minv, kN, wN);
    }
    add("Positivity", "k, ω > 0 for 3000 stiff states (dt·β*ω up to 1e9)", minv > 0, minv, 0);
    const ex = 1 + 2 * (0 - C.betaStar * 10 * 1);
    add("Positivity", "explicit Euler violates k>0 at dt·β*ω = 1.8", ex < 0, ex, 0);
    let k = 1e-3;
    for (let n = 0; n < 4000; n++) k = updateK(k, 5, 1, 1, 0.5, 0.5);
    const rel = Math.abs(k - 0.5 / (C.betaStar * 5)) / (0.5 / (C.betaStar * 5));
    add("Positivity", "Patankar steady state k* = P/(β* ω)", rel < 1e-8, rel, 1e-8);
  }
  // WALE
  {
    const v = waleNuSgs([[0, 3, 0], [0, 0, 0], [0, 0, 0]], 0.1);
    add("WALE", "ν_sgs = 0 for pure shear", Math.abs(v) < 1e-14, v, 1e-14);
    const g = [[0.3, -1.1, 0.4], [0.8, 0.2, -0.5], [-0.6, 0.9, -0.5]];
    const b = waleNuSgs(g, 0.05);
    const s = waleNuSgs(g.map((r) => r.map((x) => 3.7 * x)), 0.05) / (3.7 * b) - 1;
    add("WALE", "degree-1 homogeneity in |∇u|", Math.abs(s) < 1e-10, s, 1e-10);
  }
  // Wall functions
  {
    const e = Math.abs(uPlus(1e-4) - 1e-4);
    add("All-y+", "u+(y+→0) = y+", e < 1e-8, e, 1e-8);
    const rel = Math.abs(uPlus(200) - Math.log(C.Elog * 200) / C.kappa) / (Math.log(C.Elog * 200) / C.kappa);
    add("All-y+", "log law recovered at y+ = 200 (relative)", rel < 1e-3, rel, 1e-3);
    const nu = 1.5e-5, y = 5e-4, ut0 = 0.5;
    const Up = ut0 * uPlus((ut0 * y) / nu);
    const ut = frictionVelocity(Up, y, nu);
    const eu = Math.abs(ut - ut0) / ut0;
    add("All-y+", "u_τ round-trip (bisection)", eu < 1e-10, eu, 1e-10);
    const wn = omegaP(1e-12, nu, ut0);
    const wv = (6 * nu) / (C.beta1 * 1e-24);
    const ew = Math.abs(wn / wv - 1);
    add("All-y+", "ω_P finite as y_P→0", Number.isFinite(wn) && ew < 1e-6, ew, 1e-6);
    const pr = tPlus(60, 0.71);
    add("All-y+", "T+ finite and positive at y+ = 60", Number.isFinite(pr) && pr > 0, pr, 0);
  }
  // CHT
  {
    const layers: Layer[] = [
      { L: 0.02, k: 0.6, n: 40 },
      { L: 0.005, k: 50, n: 20 },
    ];
    const left: Bnd = { h: 500, Tinf: 350 };
    const right: Bnd = { h: 100, Tinf: 300 };
    const qA = compositeFluxAnalytic(layers, left, right);
    const sol = solveLayers(layers, left, right);
    const e = Math.abs(sol.q - qA) / Math.abs(qA);
    add("CHT", "monolithic flux = series-resistance analytic", e < 1e-10, e, 1e-10);
    const gamma = 1 / (0.5 * (0.02 / 0.6 + 0.005 / 50));
    const p = partitionedRobin(layers[0], layers[1], left, right, gamma, 0.5, 1e-12, 4000);
    const ep = Math.abs(p.q - sol.q) / Math.abs(sol.q);
    add("CHT", "Robin-Robin (ω_r = 0.5) → monolithic", p.converged && ep < 1e-8, ep, 1e-8);
    const dq = (400 - 300) / (0.02 / 0.6 + 0.005 / 50);
    const dsol = solveLayers(layers, { h: 1e30, Tinf: 400 }, { h: 1e30, Tinf: 300 });
    const ed = Math.abs(dsol.q - dq) / dq;
    add("CHT", "Dirichlet composite flux = ΔT / Σ(L/k)", ed < 1e-9, ed, 1e-9);
  }
  // Porous
  {
    const mu = 1.8e-5, rho = 1.2;
    const I = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    const D = orientedTensor(I, [1e7, 1e7, 1e7]);
    const F = orientedTensor(I, [100, 100, 100]);
    const u = [1, 0, 0];
    const gp = matVec(sinkTensor(D, F, mu, rho, u), u)[0];
    const an = mu * 1e7 + 0.5 * rho * 100;
    const e = Math.abs(gp - an) / an;
    add("Porous", "Darcy-Forchheimer dp/dL analytic", e < 1e-12, e, 1e-12);
    const ub = forchheimerU(an, mu * 1e7, 0.5 * rho * 100);
    const eu = Math.abs(ub - 1);
    add("Porous", "Forchheimer inverse recovers U", eu < 1e-12, eu, 1e-12);
    const R = rotZ(Math.PI / 6);
    const Dr = orientedTensor(R, [1e7, 1e6, 1e5]);
    const Fr = orientedTensor(R, [100, 200, 300]);
    const e1 = [R[0][0], R[1][0], R[2][0]];
    const uu = e1.map((x) => 0.8 * x);
    const gr = matVec(sinkTensor(Dr, Fr, mu, rho, uu), uu);
    const proj = gr[0] * e1[0] + gr[1] * e1[1] + gr[2] * e1[2];
    const ex = (mu * 1e7 + 0.5 * rho * 0.8 * 100) * 0.8;
    const ea = Math.abs(proj - ex) / ex;
    add("Porous", "anisotropic axis-1 flow uses (K₁, C₂₁)", ea < 1e-12, ea, 1e-12);
  }
  // P1
  {
    const a = 1, L = 1, Tg = 1000, Tw = 600;
    const sg = 4 * C.sigmaSB * Tg ** 4;
    const m40 = 40;
    const G = solveP1(new Array(m40).fill(Tg), L, a, Tw, Tw);
    let e = 0;
    for (let i = 0; i < m40; i++) e = Math.max(e, Math.abs(G[i] - p1Analytic(((i + 0.5) * L) / m40, L, a, Tg, Tw)) / sg);
    add("P1", "FVM vs analytic cosh solution (N = 40)", e < 5e-3, e, 5e-3);
    const errAt = (m: number) => {
      const Gm = solveP1(new Array(m).fill(Tg), L, a, Tw, Tw);
      let ee = 0;
      for (let i = 0; i < m; i++) ee = Math.max(ee, Math.abs(Gm[i] - p1Analytic(((i + 0.5) * L) / m, L, a, Tg, Tw)));
      return ee / sg;
    };
    const order = Math.log2(errAt(40) / errAt(80));
    add("P1", "observed spatial order ≈ 2", order > 1.8 && order < 2.2, order, 0.2);
  }
  return rows;
}

function VerifyTab() {
  const rows = useMemo(() => runSuite(), []);
  const passed = rows.filter((r) => r.ok).length;
  const groups = Array.from(new Set(rows.map((r) => r.group)));
  return (
    <div className="space-y-6">
      <div className="grid gap-3 md:grid-cols-3">
        <Stat label="Checks passed" value={`${passed} / ${rows.length}`} tone={passed === rows.length ? "ok" : "bad"} />
        <Stat label="Native C++ suite" value="cpp/tests/mp_verify.cpp · 37 checks" />
        <Stat label="Build" value="g++ -std=c++20 -O2" />
      </div>
      <Card title="Verification matrix" subtitle="Browser re-run of the analytic and invariant checks (same formulas as the C++20 kernels)">
        <div className="space-y-6">
          {groups.map((g) => (
            <div key={g}>
              <h3 className="mb-2 text-sm font-semibold text-slate-700">{g}</h3>
              <div className="overflow-x-auto">
                <table className="w-full text-left text-sm">
                  <thead className="text-xs uppercase text-slate-500">
                    <tr><th className="py-2">Status</th><th>Check</th><th>Value</th><th>Tolerance</th></tr>
                  </thead>
                  <tbody>
                    {rows.filter((r) => r.group === g).map((r) => (
                      <tr key={r.name} className="border-t border-slate-100">
                        <td className="py-2">
                          <span className={`rounded-full px-2 py-0.5 text-xs font-semibold ${r.ok ? "bg-emerald-100 text-emerald-800" : "bg-rose-100 text-rose-800"}`}>
                            {r.ok ? "PASS" : "FAIL"}
                          </span>
                        </td>
                        <td className="text-slate-800">{r.name}</td>
                        <td><Mono>{Number.isFinite(r.value) ? r.value.toExponential(2) : String(r.value)}</Mono></td>
                        <td><Mono>{r.tol.toExponential(1)}</Mono></td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          ))}
        </div>
      </Card>
      <Card title="Reproduce the native C++ verification">
        <pre className="overflow-x-auto rounded-xl bg-slate-900 p-4 text-xs text-slate-100">
{`cd cpp/tests
g++ -std=c++20 -O2 -Wall -Wextra -pedantic -I../include mp_verify.cpp -o mp_verify
./mp_verify          # 37 / 37 checks passed`}
        </pre>
        <p className="mt-3 text-xs text-slate-500">
          The CUDA batch kernels in <Mono>cpp/cuda/cluster_a_kernels.cu</Mono> share the same MP_HD device functions and
          compile with <Mono>nvcc -std=c++20 --expt-relaxed-constexpr</Mono>; they require a CUDA toolkit, which is not present in this sandbox.
        </p>
      </Card>
    </div>
  );
}

// ============================================================ Shell
export default function App() {
  const [tab, setTab] = useState<TabId>("wall");
  const current = TABS.find((t) => t.id === tab)!;
  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-50 via-white to-indigo-50/40 text-slate-900">
      <header className="border-b border-slate-200 bg-white/80 backdrop-blur">
        <div className="mx-auto flex max-w-7xl flex-col gap-4 px-6 py-5 md:flex-row md:items-center md:justify-between">
          <div className="flex items-center gap-3">
            <div className="flex h-11 w-11 items-center justify-center rounded-xl bg-gradient-to-br from-violet-500 to-indigo-600 text-lg font-bold text-white shadow-lg shadow-indigo-200">
              A
            </div>
            <div>
              <h1 className="text-xl font-semibold tracking-tight">Cluster A · Turbulence, Wall, CHT, Porous & Radiation</h1>
              <p className="text-xs text-slate-500">Engineering verification workbench · mirrors the C++20 / CUDA kernels</p>
            </div>
          </div>
          <nav className="flex flex-wrap gap-2">
            {TABS.map((t) => (
              <button
                key={t.id}
                onClick={() => setTab(t.id)}
                className={`rounded-xl px-3 py-2 text-left text-sm transition ${
                  tab === t.id ? "bg-indigo-600 text-white shadow" : "bg-white text-slate-700 ring-1 ring-slate-200 hover:bg-slate-50"
                }`}
              >
                <div className="font-semibold">{t.label}</div>
                <div className={`text-[10px] ${tab === t.id ? "text-indigo-100" : "text-slate-400"}`}>{t.hint}</div>
              </button>
            ))}
          </nav>
        </div>
      </header>
      <main className="mx-auto max-w-7xl px-6 py-8">
        <div className="mb-6">
          <h2 className="text-lg font-semibold">{current.label}</h2>
          <p className="text-sm text-slate-500">{current.hint}</p>
        </div>
        {tab === "wall" && <WallTab />}
        {tab === "cht" && <CHTTab />}
        {tab === "porous" && <PorousTab />}
        {tab === "turb" && <TurbTab />}
        {tab === "p1" && <P1Tab />}
        {tab === "verify" && <VerifyTab />}
      </main>
    </div>
  );
}
