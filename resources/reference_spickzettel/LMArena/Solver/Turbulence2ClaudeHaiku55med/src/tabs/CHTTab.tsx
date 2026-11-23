import { useMemo, useState } from "react";
import { Card, LineChart, Slider, Stat, fmt } from "../components/ui";
import { conduction1D, harmonic } from "../lib/physics";

export default function CHTTab() {
  // Interface stencil (fluid cell P and solid cell S)
  const [kf, setKf] = useState(0.6);
  const [ks, setKs] = useState(45);
  const [df, setDf] = useState(2);   // mm
  const [ds, setDs] = useState(4);   // mm
  const [TP, setTP] = useState(300);
  const [TS, setTS] = useState(350);

  // Composite slab with convective boundaries
  const [Lf, setLf] = useState(20); // mm
  const [Ls, setLs] = useState(5);  // mm
  const [h1, setH1] = useState(1000);
  const [Ta, setTa] = useState(400);
  const [h2, setH2] = useState(50);
  const [Tb, setTb] = useState(300);

  const af = 1 / (df * 1e-3);
  const as = 1 / (ds * 1e-3);
  const G = harmonic(kf, af, ks, as);
  const qInt = G * (TS - TP);
  const Ti = (kf * af * TP + ks * as * TS) / (kf * af + ks * as);
  const qFluidSide = (kf * af) * (Ti - TP);
  const qSolidSide = (ks * as) * (TS - Ti);

  const comp = useMemo(() => {
    const segs = [
      { k: kf, L: Lf * 1e-3, n: 60 },
      { k: ks, L: Ls * 1e-3, n: 30 },
    ];
    const sol = conduction1D(segs, { dirichlet: false, value: Ta, h: h1 }, { dirichlet: false, value: Tb, h: h2 });
    const qAn = (Ta - Tb) / (1 / h1 + (Lf * 1e-3) / kf + (Ls * 1e-3) / ks + 1 / h2);
    return { sol, qAn };
  }, [kf, ks, Lf, Ls, h1, Ta, h2, Tb]);

  const profile = comp.sol.x.map((x, i) => [x * 1e3, comp.sol.T[i]] as [number, number]);
  const Ttot = Ta - Tb;
  const Rtot = 1 / h1 + (Lf * 1e-3) / kf + (Ls * 1e-3) / ks + 1 / h2;
  const interfaceX = Lf;

  return (
    <div className="space-y-6">
      <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
        <Card title="Interface stencil (fluid P ↔ solid S)">
          <div className="space-y-4">
            <Slider label="Fluid conductivity κ_f" unit="W/mK" value={kf} min={0.02} max={100} log onChange={setKf} />
            <Slider label="Solid conductivity κ_s" unit="W/mK" value={ks} min={0.1} max={400} log onChange={setKs} />
            <Slider label="Fluid cell distance d_f" unit="mm" value={df} min={0.01} max={20} log onChange={setDf} />
            <Slider label="Solid cell distance d_s" unit="mm" value={ds} min={0.01} max={20} log onChange={setDs} />
            <Slider label="T_P (fluid)" unit="K" value={TP} min={250} max={600} step={1} onChange={setTP} />
            <Slider label="T_S (solid)" unit="K" value={TS} min={250} max={600} step={1} onChange={setTS} />
          </div>
        </Card>

        <div className="grid gap-6">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="Harmonic G = κfαf κsαs/(κfαf+κsαs)" value={fmt(G, 4)} unit="W/m²K" />
            <Stat label="Interface flux q_f→s" value={fmt(qInt, 4)} unit="W/m²" />
            <Stat label="Interface temperature" value={fmt(Ti, 4)} unit="K" />
            <Stat
              label="Flux continuity |q_f − q_s|"
              value={fmt(Math.abs(qFluidSide - qSolidSide), 3)}
              unit="W/m²"
              tone="good"
            />
          </div>
          <Card title="Composite fluid | solid slab: monolithic temperature profile">
            <div className="mb-4 grid gap-4 md:grid-cols-2">
              <Slider label="Fluid layer thickness" unit="mm" value={Lf} min={1} max={100} step={0.5} onChange={setLf} />
              <Slider label="Solid layer thickness" unit="mm" value={Ls} min={1} max={100} step={0.5} onChange={setLs} />
              <Slider label="Inlet film h₁ (fluid side)" unit="W/m²K" value={h1} min={10} max={1e5} log onChange={setH1} />
              <Slider label="Fluid reference T∞₁" unit="K" value={Ta} min={250} max={600} step={1} onChange={setTa} />
              <Slider label="Outer film h₂ (solid side)" unit="W/m²K" value={h2} min={1} max={1e4} log onChange={setH2} />
              <Slider label="Solid reference T∞₂" unit="K" value={Tb} min={250} max={600} step={1} onChange={setTb} />
            </div>
            <LineChart
              xLabel="x  [mm]"
              yLabel="T  [K]"
              vline={interfaceX}
              series={[
                { name: "Monolithic FV solve (unified matrix)", color: "#4f46e5", points: profile, dots: true },
              ]}
            />
            <div className="mt-4 grid grid-cols-2 gap-3 md:grid-cols-4">
              <Stat label="q FV (monolithic)" value={fmt(comp.sol.qLeft, 6)} unit="W/m²" tone="good" />
              <Stat label="q analytic (Σ R)" value={fmt(comp.qAn, 6)} unit="W/m²" />
              <Stat label="Total ΔT / R" value={fmt(Ttot / Rtot, 6)} unit="W/m²" />
              <Stat label="Relative error" value={fmt(Math.abs(comp.sol.qLeft - comp.qAn) / Math.abs(comp.qAn), 2)} />
            </div>
          </Card>
        </div>
      </div>
      <Card title="Formulation">
        <p className="text-sm leading-relaxed text-slate-600">
          Across a mapped fluid-solid twin face with opposite normals (S<sub>s</sub> = −S<sub>f</sub>), the harmonic
          resistance gives q<sub>f→s</sub> = G[(T<sub>S</sub> − T<sub>P</sub>) + ∇T<sub>f</sub>·k<sub>f</sub><sup>corr</sup>/α<sub>f</sub> + ∇T<sub>s</sub>·k<sub>s</sub><sup>corr</sup>/α<sub>s</sub>]
          with G = κ<sub>f</sub>α<sub>f</sub>κ<sub>s</sub>α<sub>s</sub>/(κ<sub>f</sub>α<sub>f</sub>+κ<sub>s</sub>α<sub>s</sub>). The C++ core
          assembles the same face weight into the unified energy matrix (monolithic) and also provides a Robin-Robin
          partitioned, Aitken-relaxed solver whose fixed point equals the monolithic solution.
        </p>
      </Card>
    </div>
  );
}
