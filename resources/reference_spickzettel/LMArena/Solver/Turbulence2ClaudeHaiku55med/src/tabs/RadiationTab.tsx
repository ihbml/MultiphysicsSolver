import { useMemo, useState } from "react";
import { Card, LineChart, Slider, Stat, fmt } from "../components/ui";
import { SIGMA_SB, p1Analytic, p1Solve } from "../lib/physics";

export default function RadiationTab() {
  const [a, setA] = useState(2);       // absorption coefficient [1/m]
  const [L, setL] = useState(0.5);     // slab thickness [m]
  const [Tg, setTg] = useState(1200);  // gas temperature [K]
  const [Tw, setTw] = useState(600);   // wall temperature [K]
  const [n, setN] = useState(1.0);     // refractive index

  const N = 200;
  const Geq = 4 * n * n * SIGMA_SB * Tg ** 4;
  const Gw = 4 * SIGMA_SB * Tw ** 4;
  const Gamma = 1 / (3 * a);

  const sol = useMemo(() => {
    const G = p1Solve(N, L, a, Geq, Gw);
    const num: [number, number][] = G.map((g, i) => [((i + 0.5) / N) * L, g]);
    const an: [number, number][] = Array.from({ length: 101 }, (_, i) => {
      const x = (i / 100) * L;
      return [x, p1Analytic(x, L, a, Geq, Gw)];
    });
    const maxErr = G.reduce((m, g, i) => Math.max(m, Math.abs(g - p1Analytic(((i + 0.5) / N) * L, L, a, Geq, Gw))), 0);
    return { num, an, maxErr, center: G[Math.floor(N / 2)] };
  }, [a, L, Geq, Gw]);

  const tau = a * L;
  const srcCenter = a * (Geq - sol.center); // radiative source a(4n²σT⁴ − G) at mid-plane (W/m³)

  return (
    <div className="space-y-6">
      <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
        <Card title="Participating medium (P1 approximation)">
          <div className="space-y-4">
            <Slider label="Absorption coefficient a" unit="1/m" value={a} min={0.05} max={50} log onChange={setA} />
            <Slider label="Slab thickness L" unit="m" value={L} min={0.02} max={5} log onChange={setL} />
            <Slider label="Gas temperature T_g" unit="K" value={Tg} min={300} max={2500} step={10} onChange={setTg} />
            <Slider label="Wall temperature T_w" unit="K" value={Tw} min={300} max={2500} step={10} onChange={setTw} />
            <Slider label="Refractive index n" value={n} min={1} max={1.6} step={0.01} onChange={setN} />
            <p className="text-xs leading-relaxed text-slate-500">
              Emission source S = 4 a n² σ T⁴ (G<sub>eq</sub> = 4 n² σ T<sub>g</sub>⁴). Marshak walls (ε = 1):
              G + 2Γ ∂G/∂n = 4σT<sub>w</sub>⁴, with Γ = 1/(3a).
            </p>
          </div>
        </Card>

        <div className="grid gap-6">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="G_eq (gas)" value={fmt(Geq, 4)} unit="W/m²" />
            <Stat label="G_wall (Marshak)" value={fmt(Gw, 4)} unit="W/m²" />
            <Stat label="Optical thickness aL" value={fmt(tau, 3)} tone={tau > 3 ? "good" : "default"} />
            <Stat label="Diffusion coefficient Γ" value={fmt(Gamma, 4)} unit="m" />
            <Stat label="G at mid-plane (FV)" value={fmt(sol.center, 5)} unit="W/m²" />
            <Stat label="Radiative source at centre" value={fmt(srcCenter, 4)} unit="W/m³" />
            <Stat label="Max |FV − analytic|" value={fmt(sol.maxErr, 3)} unit="W/m²" tone={sol.maxErr < 1e-2 * Math.max(Geq, Gw) ? "good" : "warn"} />
            <Stat label="Grid" value={`${N} cells`} />
          </div>
          <Card title="Incident radiation G(x) across the slab">
            <LineChart
              xLabel="x  [m]"
              yLabel="G  [W/m²]"
              series={[
                { name: "C++ FV solve (N=200)", color: "#4f46e5", points: sol.num, dots: false },
                { name: "Analytic (uniform source + Marshak)", color: "#f43f5e", points: sol.an, dashed: true },
              ]}
            />
          </Card>
        </div>
      </div>
    </div>
  );
}
