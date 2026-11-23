import { useMemo, useState } from "react";
import { Card, LineChart, Slider, Stat, fmt } from "../components/ui";
import { ergunGradient, Mat3, orientedTensor, rotationZYZ, darcyForchheimerU } from "../lib/physics";

const MU = 1.8e-5;
const RHO = 1.2;

const matvec = (A: Mat3, x: number[]) => A.map((r) => r[0] * x[0] + r[1] * x[1] + r[2] * x[2]);

export default function PorousTab() {
  // Isotropic packed bed (Ergun) benchmark
  const [eps, setEps] = useState(0.4);
  const [dp, setDp] = useState(5); // particle diameter, mm
  const [U, setU] = useState(0.8);
  const [L, setL] = useState(0.5);

  // Anisotropic catalyst bed: principal (1/K) values and orientation
  const [d1, setD1] = useState(1e3);
  const [d2, setD2] = useState(4e3);
  const [d3, setD3] = useState(9e3);
  const [tY, setTY] = useState(40); // deg about Y
  const [tZ, setTZ] = useState(20); // deg about Z

  const dpM = dp * 1e-3;
  const K = (eps ** 3 * dpM * dpM) / (150 * (1 - eps) ** 2);
  const C2 = (3.5 * (1 - eps)) / (eps ** 3 * dpM);

  const curve = useMemo(() => {
    const total: [number, number][] = [];
    const darcy: [number, number][] = [];
    const forch: [number, number][] = [];
    for (let i = 0; i <= 80; i++) {
      const u = 10 ** (-2 + (i * 3) / 80);
      const g = ergunGradient(u, eps, dpM, MU, RHO);
      total.push([u, g / L]);
      darcy.push([u, (MU / K) * u / L]);
      forch.push([u, (0.5 * RHO * C2 * u * u) / L]);
    }
    return { total, darcy, forch };
  }, [eps, dpM, L, K, C2]);

  const gradU = ergunGradient(U, eps, dpM, MU, RHO);
  const inv = darcyForchheimerU(gradU, MU, RHO, K, C2);
  const darcyFrac = ((MU / K) * U) / gradU;

  // Anisotropic tensor from R diag(d) R^T
  const R = rotationZYZ(0, (tY * Math.PI) / 180, (tZ * Math.PI) / 180);
  const D = orientedTensor(R, [d1, d2, d3]);
  const fI = (C2 * 0.5);
  // Forchheimer principal coefficients scale with the same anisotropy ratios (d_i / d_3).
  const F = orientedTensor(R, [(fI * d1) / d3, (fI * d2) / d3, fI]);
  const uG = [U, 0, 0];
  const Du = matvec(D, uG);
  const gradAniso = Du.map((v) => MU * v);
  const gMag = Math.hypot(...gradAniso);
  const dEff = Du[0] / U;
  const misalign = (Math.acos(Math.max(-1, Math.min(1, gradAniso[0] / (gMag || 1)))) * 180) / Math.PI;
  const sinkMag = Math.hypot(
    ...matvec(
      D.map((r, i) => r.map((v, j) => MU * v + 0.5 * RHO * U * F[i][j])),
      uG,
    ),
  );

  return (
    <div className="space-y-6">
      <div className="grid gap-6 lg:grid-cols-[320px_1fr]">
        <Card title="Packed bed (Ergun)">
          <div className="space-y-4">
            <Slider label="Porosity ε" value={eps} min={0.3} max={0.9} step={0.01} onChange={setEps} />
            <Slider label="Particle diameter d_p" unit="mm" value={dp} min={0.5} max={20} log onChange={setDp} />
            <Slider label="Superficial velocity U" unit="m/s" value={U} min={0.01} max={20} log onChange={setU} />
            <Slider label="Bed length L" unit="m" value={L} min={0.05} max={5} log onChange={setL} />
          </div>
        </Card>
        <div className="grid gap-6">
          <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
            <Stat label="Permeability K" value={fmt(K, 4)} unit="m²" />
            <Stat label="Forchheimer C₂" value={fmt(C2, 4)} unit="1/m" />
            <Stat label="Δp over L" value={fmt(gradU * L, 4)} unit="Pa" />
            <Stat label="Darcy share of Δp" value={fmt(darcyFrac * 100, 3)} unit="%" />
            <Stat label="Inverted velocity" value={fmt(inv, 6)} unit="m/s" tone="good" />
            <Stat label="Inversion error" value={fmt(Math.abs(inv - U), 2)} unit="m/s" tone="good" />
          </div>
          <Card title="Pressure gradient vs superficial velocity (Ergun split)">
            <LineChart
              xLog
              yLog
              xLabel="U  [m/s]"
              yLabel="−∇p  [Pa/m]"
              vline={U}
              series={[
                { name: "Total (Ergun)", color: "#4f46e5", points: curve.total },
                { name: "Darcy μ/K·U", color: "#10b981", points: curve.darcy, dashed: true },
                { name: "Forchheimer ½ρC₂U²", color: "#f43f5e", points: curve.forch, dashed: true },
              ]}
            />
          </Card>
        </div>
      </div>

      <Card title="Anisotropic Darcy-Forchheimer zone:  D = R diag(d₁,d₂,d₃) Rᵀ">
        <div className="grid gap-6 lg:grid-cols-2">
          <div className="space-y-4">
            <Slider label="Principal d₁ (1/K along axis 1)" unit="1/m²" value={d1} min={1e2} max={1e5} log onChange={setD1} />
            <Slider label="Principal d₂" unit="1/m²" value={d2} min={1e2} max={1e5} log onChange={setD2} />
            <Slider label="Principal d₃" unit="1/m²" value={d3} min={1e2} max={1e5} log onChange={setD3} />
            <Slider label="Orientation θ_Y" unit="°" value={tY} min={0} max={90} step={1} onChange={setTY} />
            <Slider label="Orientation θ_Z" unit="°" value={tZ} min={0} max={90} step={1} onChange={setTZ} />
          </div>
          <div className="grid grid-cols-2 gap-3 content-start">
            <Stat label="tr(D) = Σ dᵢ" value={fmt(D[0][0] + D[1][1] + D[2][2], 5)} unit="1/m²" />
            <Stat label="Effective d along flow (n·D·n)" value={fmt(dEff, 5)} unit="1/m²" />
            <Stat label="Momentum sink |(μD+½ρ|u|F)u|" value={fmt(sinkMag, 4)} unit="Pa/m" />
            <Stat
              label="Gradient–velocity misalignment"
              value={fmt(misalign, 3)}
              unit="°"
              tone={misalign > 1 ? "warn" : "default"}
            />
          </div>
        </div>
        <p className="mt-4 text-xs leading-relaxed text-slate-500">
          Rotating a diagonal permeability tensor produces a full symmetric D. When the flow is not aligned with a principal
          axis, the pressure gradient is rotated away from the velocity, which is why the anisotropic sink must be applied as
          a 3×3 implicit term rather than a scalar. The Forchheimer term uses the same orientation with its own principal
          coefficients.
        </p>
      </Card>
    </div>
  );
}
