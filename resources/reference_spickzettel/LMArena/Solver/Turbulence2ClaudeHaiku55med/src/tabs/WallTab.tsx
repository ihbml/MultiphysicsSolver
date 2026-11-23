import { useMemo, useState } from "react";
import { Card, LineChart, Slider, Stat, fmt } from "../components/ui";
import { LAW, allYplus, tPlus, uPlus } from "../lib/physics";

export default function WallTab() {
  const [U, setU] = useState(10);
  const [yP, setYP] = useState(5e-4);
  const [nu, setNu] = useState(1.5e-5);
  const [Tw, setTw] = useState(350);
  const [TP, setTP] = useState(300);
  const rho = 1.2;
  const cp = 1005;

  const w = allYplus(U, yP, nu, rho, cp, Tw, TP);
  const regime = w.yplus < 5 ? "viscous sublayer" : w.yplus < 30 ? "buffer layer" : w.yplus <= 500 ? "log layer" : "outer (y+ > 500)";

  const curves = useMemo(() => {
    const u: [number, number][] = [];
    const t: [number, number][] = [];
    const lg: [number, number][] = [];
    const vis: [number, number][] = [];
    for (let i = 0; i <= 80; i++) {
      const yp = 10 ** (-1 + (i * 3.7) / 80);
      u.push([yp, uPlus(yp, LAW)]);
      t.push([yp, tPlus(yp, LAW)]);
      lg.push([yp, Math.log(LAW.E * yp) / LAW.kappa]);
      vis.push([yp, yp]);
    }
    return { u, t, lg, vis };
  }, []);

  return (
    <div className="space-y-6">
      <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
        <Card title="Wall-adjacent cell">
          <div className="space-y-4">
            <Slider label="Cell-centre velocity U_P" unit="m/s" value={U} min={0.1} max={100} log onChange={setU} />
            <Slider label="Wall distance y_P" unit="m" value={yP} min={1e-7} max={0.1} log onChange={setYP} />
            <Slider label="Kinematic viscosity ν" unit="m²/s" value={nu} min={1e-6} max={1e-4} log onChange={setNu} />
            <Slider label="Wall temperature T_w" unit="K" value={Tw} min={250} max={600} step={1} onChange={setTw} />
            <Slider label="Cell temperature T_P" unit="K" value={TP} min={250} max={600} step={1} onChange={setTP} />
            <p className="text-xs text-slate-500">
              Air properties: ρ = 1.2 kg/m³, c<sub>p</sub> = 1005 J/(kg K). The wall law is solved for u<sub>τ</sub> by
              monotone bisection, so it is valid for any y<sub>P</sub> with no switch between sublayer and log formulas.
            </p>
          </div>
        </Card>

        <div className="grid gap-6">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="Friction velocity u_τ" value={fmt(w.utau, 4)} unit="m/s" />
            <Stat label="y+" value={fmt(w.yplus, 4)} tone={w.yplus >= 0.1 && w.yplus <= 500 ? "good" : "warn"} />
            <Stat label="Wall shear τ_w" value={fmt(w.tauW, 4)} unit="Pa" />
            <Stat label="Wall heat flux q_w" value={fmt(w.qW, 4)} unit="W/m²" />
            <Stat label="ω_vis" value={fmt(w.wVis, 4)} unit="1/s" />
            <Stat label="ω_log" value={fmt(w.wLog, 4)} unit="1/s" />
            <Stat label="ω_P (blended)" value={fmt(w.omegaP, 4)} unit="1/s" />
            <Stat label="Regime" value={regime} />
          </div>
          <Card title="Kader blended u+ and T+ with current y+ (dashed = log law)">
            <LineChart
              xLog
              xLabel="y+"
              yLabel="u+ , T+"
              vline={w.yplus}
              series={[
                { name: "u+ Kader blend", color: "#4f46e5", points: curves.u },
                { name: "u+ = y+ (viscous)", color: "#94a3b8", points: curves.vis, dashed: true },
                { name: "(1/κ) ln(E y+)", color: "#f59e0b", points: curves.lg, dashed: true },
                { name: "T+ Kader (Pr=0.71)", color: "#10b981", points: curves.t },
              ]}
            />
          </Card>
        </div>
      </div>
    </div>
  );
}
