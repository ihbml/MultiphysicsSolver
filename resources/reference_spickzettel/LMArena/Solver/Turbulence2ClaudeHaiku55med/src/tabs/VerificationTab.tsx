import verification from "../../cpp/verification.json";
import { Card, LineChart, Stat, fmt } from "../components/ui";
import { LAW, p1Analytic, uPlus, tPlus } from "../lib/physics";

type Check = {
  group: string;
  name: string;
  expected: string;
  measured: number;
  reference: number;
  tolerance: number;
  pass: boolean;
};

const checks = verification.checks as Check[];
const groups = Array.from(new Set(checks.map((c) => c.group)));
const passed = checks.filter((c) => c.pass).length;

export default function VerificationTab() {
  const wallLaw = verification.wallLaw as number[][];
  const p1 = verification.p1 as number[][];
  const composite = verification.composite as number[][];
  const residual = verification.partitionedResidual as number[];

  const logLaw = wallLaw.map(([yp, , , ll]) => [yp, ll] as [number, number]);
  const wallSeries = [
    { name: "Kader u+ (blended)", color: "#4f46e5", points: wallLaw.map(([yp, u]) => [yp, u] as [number, number]) },
    { name: "Log law (1/κ)ln(E y+)", color: "#f59e0b", points: logLaw, dashed: true },
    { name: "Kader T+ (Pr=0.71)", color: "#10b981", points: wallLaw.map(([yp, , t]) => [yp, t] as [number, number]) },
  ];

  const p1Num = p1.map(([x, g]) => [x, g] as [number, number]);
  const p1An = Array.from({ length: 101 }, (_, i) => {
    const x = i / 100;
    return [x, p1Analytic(x, 1, 1, 0.5, 1.2)] as [number, number];
  });

  return (
    <div className="space-y-6">
      <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
        <Stat label="Checks passed" value={`${passed} / ${checks.length}`} tone={passed === checks.length ? "good" : "warn"} />
        <Stat label="Test groups" value={String(groups.length)} />
        <Stat label="Build" value="g++ -std=c++20 -O2" />
        <Stat label="Failures" value={String(verification.failed)} tone={verification.failed === 0 ? "good" : "warn"} />
      </div>

      <Card title="Analytical verification report (cpp/verify.cpp output)">
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead className="text-xs uppercase text-slate-500">
              <tr>
                <th className="py-2 pr-3">Group</th>
                <th className="py-2 pr-3">Check</th>
                <th className="py-2 pr-3">Reference</th>
                <th className="py-2 pr-3 text-right">Measured</th>
                <th className="py-2 pr-3 text-right">Tol.</th>
                <th className="py-2 text-center">Status</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-100">
              {checks.map((c, i) => (
                <tr key={i} className="hover:bg-slate-50">
                  <td className="py-2 pr-3 font-medium text-slate-700">{c.group}</td>
                  <td className="py-2 pr-3 text-slate-600">{c.name}</td>
                  <td className="py-2 pr-3 font-mono text-xs text-slate-500">{c.expected}</td>
                  <td className="py-2 pr-3 text-right font-mono">{fmt(c.measured, 6)}</td>
                  <td className="py-2 pr-3 text-right font-mono text-xs text-slate-400">{c.tolerance.toExponential(0)}</td>
                  <td className="py-2 text-center">
                    <span
                      className={`rounded-full px-2 py-0.5 text-xs font-semibold ${
                        c.pass ? "bg-emerald-100 text-emerald-700" : "bg-rose-100 text-rose-700"
                      }`}
                    >
                      {c.pass ? "PASS" : "FAIL"}
                    </span>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Card>

      <div className="grid gap-6 lg:grid-cols-2">
        <Card title="All-y+ wall law (C++ dataset)">
          <LineChart series={wallSeries} xLog xLabel="y+" yLabel="u+ , T+" height={300} />
          <p className="mt-2 text-xs text-slate-500">
            Kader blend recovers u+ = y+ in the viscous sublayer and (1/κ) ln(E y+) in the log layer for 0.1 ≤ y+ ≤ 500.
          </p>
        </Card>
        <Card title="P1 radiation: Marshak walls (C++ FV vs analytic)">
          <LineChart
            series={[
              { name: "C++ finite volume", color: "#4f46e5", points: p1Num, dots: true },
              { name: "Analytic", color: "#f43f5e", points: p1An },
            ]}
            xLabel="x / L"
            yLabel="G  [W/m²]"
            height={300}
          />
        </Card>
        <Card title="Composite fluid | solid conduction (monolithic matrix)">
          <LineChart
            series={[{ name: "T(x) monolithic", color: "#0ea5e9", points: composite.map(([x, T]) => [x, T] as [number, number]), dots: true }]}
            xLabel="x  [m]"
            yLabel="T  [K]"
            height={300}
          />
        </Card>
        <Card title="Robin-Robin partitioned CHT convergence (Aitken)">
          <LineChart
            series={[{ name: "|Δ T_interface| per iteration", color: "#7c3aed", points: residual.map((r, i) => [i + 1, r] as [number, number]), dots: true }]}
            xLabel="iteration"
            yLabel="residual [K]"
            yLog
            height={300}
          />
          <p className="mt-2 text-xs text-slate-500">
            Partitioned solution matches the monolithic solve to ~1e-10 K. Log-law reference: u+ at y+=100 = {fmt(uPlus(100, LAW), 4)}, T+ = {fmt(tPlus(100, LAW), 4)}.
          </p>
        </Card>
      </div>
    </div>
  );
}
