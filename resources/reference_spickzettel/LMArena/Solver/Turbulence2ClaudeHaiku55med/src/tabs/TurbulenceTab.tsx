import { useMemo, useState } from "react";
import { Card, LineChart, Slider, Stat, fmt } from "../components/ui";
import {
  explicitStep,
  Mat3,
  patankarStep,
  sstF1,
  sstF2,
  sstNut,
  sstPkLimited,
  waleNuSgs,
  SST,
} from "../lib/physics";

export default function TurbulenceTab() {
  const [k, setK] = useState(0.5);
  const [omega, setOmega] = useState(50);
  const [nu, setNu] = useState(1.5e-5);
  const [S, setS] = useState(200);
  const [y, setY] = useState(1e-3);
  const [gkgw, setGkgw] = useState(0);

  const [dt, setDt] = useState(1);
  const [crossSign, setCrossSign] = useState(-1);

  const [shear, setShear] = useState(40);
  const [a, setA] = useState(0.5);
  const [b, setB] = useState(-0.2);
  const [delta, setDelta] = useState(0.002);

  const local = { k, omega, nu, S, y, gradKdotGradW: gkgw };
  const F1 = sstF1(local);
  const F2 = sstF2(local);
  const nut = sstNut(k, omega, S, F2);
  const Praw = nut * S * S;
  const Pk = sstPkLimited(nut, S, k, omega);
  const beta = F1 * SST.beta1 + (1 - F1) * SST.beta2;

  const profile = useMemo(() => {
    const f1: [number, number][] = [];
    const f2: [number, number][] = [];
    for (let i = 0; i <= 120; i++) {
      const yy = 10 ** (-6 + (i * 6) / 120);
      const loc = { k, omega, nu, S, y: yy, gradKdotGradW: gkgw };
      f1.push([yy, sstF1(loc)]);
      f2.push([yy, sstF2(loc)]);
    }
    return { f1, f2 };
  }, [k, omega, nu, S, gkgw]);

  // Positivity demo: destruction dominated cell with a cross-diffusion term.
  const VoverDt = 1 / dt;
  const kP = patankarStep(k, omega, Pk, 0.0, 0, beta, VoverDt);
  const kE = explicitStep(k, omega, Pk, 0.0, dt, beta);
  const cross = crossSign * 2 * Math.abs(omega) * 0.5;
  const kPc = patankarStep(k, omega, Pk, 0.0, cross, beta, VoverDt);

  const G: Mat3 = [
    [a, shear, 0],
    [0, b, 0.3 * shear * 0.1],
    [0, 0, -(a + b)],
  ];
  const nuSgs = waleNuSgs(G, delta);
  const nuSgsNorm = nuSgs / nu;

  return (
    <div className="space-y-6">
      <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
        <Card title="k-ω SST inputs (kinematic)">
          <div className="space-y-4">
            <Slider label="k" unit="m²/s²" value={k} min={1e-4} max={100} log onChange={setK} />
            <Slider label="ω" unit="1/s" value={omega} min={1e-1} max={1e5} log onChange={setOmega} />
            <Slider label="ν" unit="m²/s" value={nu} min={1e-7} max={1e-3} log onChange={setNu} />
            <Slider label="Strain S" unit="1/s" value={S} min={1e-2} max={1e4} log onChange={setS} />
            <Slider label="Wall distance y" unit="m" value={y} min={1e-7} max={1} log onChange={setY} />
            <Slider label="∇k · ∇ω" value={gkgw} min={-1e3} max={1e3} step={1} onChange={setGkgw} />
          </div>
        </Card>

        <div className="grid gap-6">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="F1 (blend)" value={fmt(F1, 4)} tone={F1 > 0.5 ? "good" : "default"} />
            <Stat label="F2 (ν_t limiter)" value={fmt(F2, 4)} />
            <Stat label="ν_t" value={fmt(nut, 4)} unit="m²/s" />
            <Stat label="ν_t / ν" value={fmt(nut / nu, 4)} />
            <Stat label="P_k raw = ν_t S²" value={fmt(Praw, 4)} />
            <Stat label="P_k limited" value={fmt(Pk, 4)} tone={Pk < Praw ? "warn" : "default"} />
            <Stat label="Blended β" value={fmt(beta, 4)} />
            <Stat label="Limiter active" value={Pk < Praw ? "yes" : "no"} />
          </div>
          <Card title="Blending functions vs wall distance (log y)">
            <LineChart
              xLog
              xLabel="y  [m]"
              yLabel="F1, F2"
              yDomain={[0, 1]}
              vline={y}
              series={[
                { name: "F1", color: "#4f46e5", points: profile.f1 },
                { name: "F2", color: "#10b981", points: profile.f2, dashed: true },
              ]}
            />
          </Card>
        </div>
      </div>

      <div className="grid gap-6 lg:grid-cols-2">
        <Card title="Patankar positivity vs explicit update">
          <div className="space-y-4">
            <Slider label="Time step Δt" unit="s" value={dt} min={1e-3} max={1e4} log onChange={setDt} />
            <div className="flex items-center gap-2 text-sm">
              <span className="text-slate-600">Cross-diffusion sign:</span>
              {[-1, 1].map((s) => (
                <button
                  key={s}
                  onClick={() => setCrossSign(s)}
                  className={`rounded-lg px-3 py-1 text-xs font-semibold ${
                    crossSign === s ? "bg-indigo-600 text-white" : "bg-slate-100 text-slate-600"
                  }`}
                >
                  {s < 0 ? "negative (implicit)" : "positive (RHS)"}
                </button>
              ))}
            </div>
            <div className="grid grid-cols-2 gap-3">
              <Stat label="k explicit" value={fmt(kE.k, 4)} tone={kE.k < 0 ? "warn" : "good"} />
              <Stat label="k Patankar" value={fmt(kP.k, 4)} tone="good" />
              <Stat label="ω explicit" value={fmt(kE.omega, 4)} tone={kE.omega < 0 ? "warn" : "good"} />
              <Stat label="ω Patankar" value={fmt(kP.omega, 4)} tone="good" />
              <Stat label="ω Patankar (with cross-diff)" value={fmt(kPc.omega, 4)} tone={kPc.omega > 0 ? "good" : "warn"} />
              <Stat label="k Patankar (with cross-diff)" value={fmt(kPc.k, 4)} tone="good" />
            </div>
            <p className="text-xs leading-relaxed text-slate-500">
              Destruction is linearised implicitly: a<sub>PP</sub> += β* ω<sub>P</sub> V<sub>P</sub> for k, and
              a<sub>PP</sub> += 2 β ω<sub>P</sub> V<sub>P</sub> for ω (ω² linearised as ω<sub>old</sub>·ω<sub>new</sub>).
              Positive production stays on the RHS, so k, ω &gt; 0 for every Δt. A negative cross-diffusion term is moved to
              the diagonal rather than the RHS.
            </p>
          </div>
        </Card>

        <Card title="LES WALE subgrid viscosity">
          <div className="space-y-4">
            <Slider label="Shear rate γ̇ (g₁₂)" unit="1/s" value={shear} min={0.1} max={1e4} log onChange={setShear} />
            <Slider label="Normal strain a (g₁₁)" unit="1/s" value={a} min={-5} max={5} step={0.01} onChange={setA} />
            <Slider label="Normal strain b (g₂₂)" unit="1/s" value={b} min={-5} max={5} step={0.01} onChange={setB} />
            <Slider label="Filter width Δ" unit="m" value={delta} min={1e-4} max={0.1} log onChange={setDelta} />
            <div className="grid grid-cols-2 gap-3">
              <Stat label="ν_sgs (WALE)" value={fmt(nuSgs, 4)} unit="m²/s" />
              <Stat label="ν_sgs / ν" value={fmt(nuSgsNorm, 4)} />
            </div>
            <p className="text-xs text-slate-500">
              ν_sgs = (C_w Δ)² (S<sup>d</sup>:S<sup>d</sup>)<sup>3/2</sup> / [(S:S)<sup>5/2</sup> + (S<sup>d</sup>:S<sup>d</sup>)<sup>5/4</sup>], C_w = 0.325. Pure
              shear gives exactly zero (no spurious SGS viscosity in laminar shear), a key WALE property.
            </p>
          </div>
        </Card>
      </div>
    </div>
  );
}
