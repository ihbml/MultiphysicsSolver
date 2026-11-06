import { useEffect, useRef } from "react";
import { FvmMesh, histogram } from "../lib/mesh";
import { SphData } from "../lib/sph";
import { InterpStats, SCHEME_LABEL, Scheme } from "../lib/coupling";
import { PATCH_COLOR, PATCH_LABEL, PatchKind } from "../lib/sdf";
import { fmt } from "../lib/colors";
import { Bars, Note, Panel, Row, Spark, Stat } from "./ui";

export interface CoupleSummary {
  nBuffer: number;
  nInterface: number;
  nCovered: number;
  nFull: number;
  nParticles: number;
  nPrunable: number;
  p2m: Record<Scheme, InterpStats>;
  m2p: Record<Scheme, InterpStats>;
}

function Sparsity({ mesh, rcm }: { mesh: FvmMesh; rcm: boolean }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const cv = ref.current;
    if (!cv) return;
    const ctx = cv.getContext("2d");
    if (!ctx) return;
    const S = 140;
    cv.width = S;
    cv.height = S;
    ctx.fillStyle = "#020617";
    ctx.fillRect(0, 0, S, S);
    const n = mesh.nCells;
    const idx = new Int32Array(n);
    if (rcm) for (let i = 0; i < n; i++) idx[mesh.rcm[i]] = i;
    else for (let i = 0; i < n; i++) idx[i] = i;
    const k = S / n;
    ctx.fillStyle = rcm ? "#34d399" : "#38bdf8";
    for (let i = 0; i < n; i++) ctx.fillRect(idx[i] * k, idx[i] * k, Math.max(0.7, k), Math.max(0.7, k));
    for (let f = 0; f < mesh.nFaces; f++) {
      const N = mesh.neigh[f];
      if (N < 0) continue;
      const a = idx[mesh.owner[f]];
      const b = idx[N];
      ctx.fillRect(b * k, a * k, Math.max(0.7, k), Math.max(0.7, k));
      ctx.fillRect(a * k, b * k, Math.max(0.7, k), Math.max(0.7, k));
    }
  }, [mesh, rcm]);
  return (
    <div className="flex-1">
      <canvas ref={ref} className="w-full rounded ring-1 ring-white/10" />
      <div className="mt-0.5 text-center text-[9px] text-slate-500">
        {rcm ? "RCM ordering" : "insertion ordering"}
      </div>
    </div>
  );
}

export function EquivalenceMetrics({
  mesh,
  sph,
}: {
  mesh: FvmMesh | null;
  sph: SphData | null;
}) {
  if (!mesh || !sph) return null;
  let pairs = 0;
  for (let i = 0; i < sph.n; i++) pairs += sph.nNeigh[i];
  pairs = Math.round(pairs / 2);
  const rows: [string, string, string][] = [
    ["degrees of freedom", mesh.nCells.toLocaleString(), sph.n.toLocaleString()],
    [
      "pair interactions",
      mesh.stats.nInternal.toLocaleString(),
      pairs.toLocaleString(),
    ],
    [
      "boundary entities",
      mesh.stats.nBoundary.toLocaleString(),
      sph.diag.nBoundary.toLocaleString(),
    ],
    [
      "Σ boundary area",
      fmt(mesh.stats.boundaryArea, 5),
      fmt(sph.diag.surfaceSum, 5),
    ],
    [
      "closure residual",
      mesh.stats.closureResidual.toExponential(1),
      sph.diag.eps0Bulk.toExponential(1),
    ],
  ];
  return (
    <Panel
      title="FVM ↔ Eulerian SPH"
      subtitle="A_ij = S_ij n_ij  ⟺  2 V_i V_j (∂W_ij/∂r_ij) e_ij"
    >
      <table className="w-full border-collapse text-[10.5px]">
        <thead>
          <tr className="text-slate-500">
            <th className="py-0.5 text-left font-medium"> </th>
            <th className="text-right font-medium">FVM</th>
            <th className="text-right font-medium">E-SPH</th>
          </tr>
        </thead>
        <tbody className="font-mono text-slate-300">
          {rows.map(([k, a, b]) => (
            <tr key={k}>
              <td className="py-0.5 text-left font-sans text-slate-500">{k}</td>
              <td className="text-right">{a}</td>
              <td className="text-right text-cyan-300">{b}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <Note>
        Both discretisations solve the same flux loop; the SPH cloud needs{" "}
        {(pairs / Math.max(mesh.stats.nInternal, 1)).toFixed(1)}× more pair
        interactions per degree of freedom, which is the price of its isotropic,
        mesh-free support — and the reason a strong FVM/SPH coupling pays off.
      </Note>
    </Panel>
  );
}

export function MeshMetrics({ mesh }: { mesh: FvmMesh | null }) {
  if (!mesh)
    return (
      <Panel title="FVM diagnostics">
        <p className="text-[11px] text-slate-500">
          Generate a mesh to see topology, orthogonality and graph statistics.
        </p>
      </Panel>
    );
  const s = mesh.stats;
  const areaErr =
    s.exactPerimeter > 0
      ? (100 * Math.abs(s.boundaryArea - s.exactPerimeter)) / s.exactPerimeter
      : 0;
  const noHist = histogram(mesh.nonOrtho, 24, 0, Math.max(s.nonOrthoMax, 1), (i) => mesh.neigh[i] >= 0);
  const qHist = histogram(mesh.quality, 24, 0, 1);
  return (
    <>
      <Panel title="Mesh topology" subtitle="cell-centred, face-based (owner / neighbour)">
        <Row>
          <Stat label="cells" value={s.nCells.toLocaleString()} />
          <Stat label="points" value={s.nPoints.toLocaleString()} />
          <Stat label="faces" value={s.nFaces.toLocaleString()} />
          <Stat label="internal / bnd" value={`${s.nInternal} / ${s.nBoundary}`} />
          <Stat label="avg graph degree" value={fmt(s.avgDegree, 3)} />
          <Stat label="aspect ratio max" value={fmt(s.arMax, 3)} />
        </Row>
      </Panel>

      <Panel title="Quality" subtitle="normalized triangle quality q ∈ (0, 1]">
        <Row>
          <Stat
            label="q min"
            value={fmt(s.qMin, 3)}
            tone={s.qMin > 0.5 ? "good" : s.qMin > 0.3 ? "warn" : "bad"}
          />
          <Stat label="q mean" value={fmt(s.qMean, 4)} tone="good" />
          <Stat
            label="non-ortho max"
            value={`${fmt(s.nonOrthoMax, 3)}°`}
            tone={s.nonOrthoMax < 35 ? "good" : s.nonOrthoMax < 60 ? "warn" : "bad"}
          />
          <Stat label="non-ortho mean" value={`${fmt(s.nonOrthoMean, 3)}°`} />
          <Stat
            label="skewness max"
            value={fmt(s.skewMax, 3)}
            tone={s.skewMax < 0.4 ? "good" : "warn"}
          />
          <Stat label="cell size √V" value={`${fmt(Math.sqrt(s.minVol), 2)}–${fmt(Math.sqrt(s.maxVol), 2)}`} />
        </Row>
        <div>
          <div className="mb-0.5 text-[10px] text-slate-500">face non-orthogonality (deg)</div>
          <Bars data={noHist} lo={0} hi={s.nonOrthoMax} color="#38bdf8" unit="°" />
        </div>
        <div>
          <div className="mb-0.5 text-[10px] text-slate-500">cell quality</div>
          <Bars data={qHist} lo={0} hi={1} color="#34d399" />
        </div>
      </Panel>

      <Panel
        title="Zero-order consistency"
        subtitle="each control volume must close: Σ_f S_f n_f = 0"
      >
        <Row>
          <Stat
            label="Σ S_f n_f / L"
            value={fmt(s.closureResidual, 3)}
            tone={s.closureResidual < 1e-12 ? "good" : "warn"}
            hint="machine-zero means every cell is a watertight control volume"
          />
          <Stat
            label="Σ A_bnd vs perimeter"
            value={`${areaErr.toExponential(1)} %`}
            tone={areaErr < 1e-8 ? "good" : "warn"}
          />
        </Row>
        <Note>
          This is the FVM property that Wang, Haidn &amp; Hu port into Eulerian SPH:
          interior cells close automatically, boundary cells are closed by the
          boundary surface itself — so Σ_j A_ij + A_iS = 0 holds everywhere.
        </Note>
      </Panel>

      <Panel title="Boundary patches">
        <div className="space-y-1">
          {Object.entries(s.patchFaces).map(([k, v]) => (
            <div key={k} className="flex items-center gap-2 text-[11px]">
              <span
                className="h-2 w-2 shrink-0 rounded-sm"
                style={{ background: PATCH_COLOR[k as PatchKind] }}
              />
              <span className="text-slate-300">{PATCH_LABEL[k as PatchKind]}</span>
              <span className="ml-auto font-mono text-slate-400">{v} faces</span>
            </div>
          ))}
        </div>
      </Panel>

      <Panel title="DiFVM graph" subtitle="static scatter / gather connectivity">
        <Row>
          <Stat label="MP edges" value={(2 * s.nInternal).toLocaleString()} hint="both directions of every internal face" />
          <Stat label="segment-sum targets" value={s.nCells.toLocaleString()} />
          <Stat label="bandwidth raw" value={s.bandwidthBefore.toLocaleString()} />
          <Stat
            label="bandwidth RCM"
            value={s.bandwidthAfter.toLocaleString()}
            tone={s.bandwidthAfter < s.bandwidthBefore ? "good" : "default"}
          />
        </Row>
        <div className="flex gap-2">
          <Sparsity mesh={mesh} rcm={false} />
          <Sparsity mesh={mesh} rcm />
        </div>
        <Note>
          Fluxes = gather(owner, neighbour) → compute → segment_sum back to cells,
          i.e. GNN message passing on a static graph. The RCM permutation improves
          memory locality of those gathers on GPU.
        </Note>
      </Panel>
    </>
  );
}

export function SphMetrics({
  sph,
  history,
}: {
  sph: SphData | null;
  history: number[];
}) {
  if (!sph)
    return (
      <Panel title="Eulerian SPH diagnostics">
        <p className="text-[11px] text-slate-500">
          Relax a particle distribution to measure zero-order consistency and the
          reconstructed boundary surfaces.
        </p>
      </Panel>
    );
  const d = sph.diag;
  const areaErr =
    d.exactPerimeter > 0
      ? (100 * Math.abs(d.surfaceSum - d.exactPerimeter)) / d.exactPerimeter
      : 0;
  const epsHist = histogram(sph.eps0, 24, 0, Math.max(d.eps0Max, 1e-9));
  const divHist = histogram(sph.divR, 24, 0, 2.4);
  return (
    <>
      <Panel title="Particle distribution">
        <Row>
          <Stat label="particles" value={d.n.toLocaleString()} />
          <Stat label="within boundaries" value={d.nBoundary.toLocaleString()} hint="div(r) < 0.75 d" />
          <Stat label="neighbours mean" value={fmt(d.neighMean, 3)} />
          <Stat label="neighbours min" value={String(d.neighMin)} />
          <Stat label="div(r) mean" value={fmt(d.divRMean, 3)} hint="→ d = 2 in 2D for full kernel support" />
          <Stat label="last Δx / dp" value={fmt(d.maxShift, 2)} tone={d.maxShift < 1e-3 ? "good" : "warn"} />
        </Row>
      </Panel>

      <Panel
        title="Consistency"
        subtitle="Σ_j V_j ∇W_ij = 0 (0th) · Σ_j V_j (r_j−r_i)⊗∇W_ij = I (1st)"
      >
        <Row>
          <Stat
            label="ε₀ bulk"
            value={d.eps0Bulk.toExponential(2)}
            tone={d.eps0Bulk < 1e-2 ? "good" : "warn"}
            hint="h·|Σ V_j ∇W| for particles with full support"
          />
          <Stat label="ε₀ mean (all)" value={d.eps0Mean.toExponential(2)} />
          <Stat label="ε₀ max" value={d.eps0Max.toExponential(2)} hint="attained in the outer two layers" />
          <Stat label="‖M − I‖_F" value={fmt(d.eps1Mean, 3)} />
        </Row>
        <div>
          <div className="mb-0.5 text-[10px] text-slate-500">relaxation history — mean h·|Σ V_j ∇W_ij| (log)</div>
          <Spark pts={history} />
        </div>
        <div>
          <div className="mb-0.5 text-[10px] text-slate-500">ε₀ distribution</div>
          <Bars data={epsHist} lo={0} hi={d.eps0Max} color="#f472b6" />
        </div>
        <div>
          <div className="mb-0.5 text-[10px] text-slate-500">div(r) distribution — γ = 1.5 flags boundary particles</div>
          <Bars data={divHist} lo={0} hi={2.4} color="#a78bfa" />
        </div>
      </Panel>

      <Panel
        title="Reconstructed boundary surface"
        subtitle="A_iS = −Σ_p A_ip ,  A_ip = 2 V_i V_p (∂W/∂r) e_ip"
      >
        <Row>
          <Stat label="Σ |A_iS|" value={fmt(d.surfaceSum, 5)} />
          <Stat label="exact perimeter" value={fmt(d.exactPerimeter, 5)} />
          <Stat
            label="surface error"
            value={`${fmt(areaErr, 3)} %`}
            tone={areaErr < 5 ? "good" : areaErr < 15 ? "warn" : "bad"}
            hint="how well the particle boundary surfaces recover the true geometry"
          />
          <Stat label="bnd fraction" value={`${fmt((100 * d.nBoundary) / d.n, 3)} %`} />
        </Row>
        <div className="space-y-1">
          {Object.entries(d.patchCount).map(([k, v]) => (
            <div key={k} className="flex items-center gap-2 text-[11px]">
              <span
                className="h-2 w-2 shrink-0 rounded-sm"
                style={{ background: PATCH_COLOR[k as PatchKind] }}
              />
              <span className="text-slate-300">{PATCH_LABEL[k as PatchKind]}</span>
              <span className="ml-auto font-mono text-slate-400">{v} particles</span>
            </div>
          ))}
        </div>
        <Note>
          Only the <em>inner</em> configuration is needed: the missing flux of a
          truncated kernel support is stored as (−|Σ_p A_ip|) in the ∂W/∂r·V_j slot
          and its unit normal in the e_ij slot — no contact configuration, no ghost
          particles.
        </Note>
      </Panel>
    </>
  );
}

export function CoupleMetrics({
  summary,
  scheme,
}: {
  summary: CoupleSummary | null;
  scheme: Scheme;
}) {
  if (!summary)
    return (
      <Panel title="Coupling diagnostics">
        <p className="text-[11px] text-slate-500">
          Enable the coupled mode and generate both discretisations to build the
          particle ↔ mesh transfer operators.
        </p>
      </Panel>
    );
  const saving = 100 * (1 - summary.nParticles / Math.max(summary.nFull, 1));
  const schemes: Scheme[] = ["shepard", "mls", "opmis"];
  const table = (title: string, data: Record<Scheme, InterpStats>) => (
    <div>
      <div className="mb-1 text-[10px] uppercase tracking-wider text-slate-500">{title}</div>
      <table className="w-full border-collapse text-[10.5px]">
        <thead>
          <tr className="text-slate-500">
            <th className="py-0.5 text-left font-medium">scheme</th>
            <th className="text-right font-medium">C₀ err</th>
            <th className="text-right font-medium">C₁ err</th>
            <th className="text-right font-medium">w min</th>
            <th className="text-right font-medium">over %</th>
          </tr>
        </thead>
        <tbody className="font-mono">
          {schemes.map((s) => (
            <tr
              key={s}
              className={s === scheme ? "text-cyan-300" : "text-slate-400"}
            >
              <td className="py-0.5 text-left font-sans">{SCHEME_LABEL[s]}</td>
              <td className="text-right">{data[s].c0Max.toExponential(1)}</td>
              <td className="text-right">{data[s].c1Mean.toExponential(1)}</td>
              <td className="text-right">{data[s].wMin.toFixed(3)}</td>
              <td
                className={
                  data[s].shockOvershoot > 1 ? "text-right text-rose-300" : "text-right"
                }
              >
                {data[s].shockOvershoot.toFixed(1)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
  return (
    <>
      <Panel title="Adaptive region bookkeeping">
        <Row>
          <Stat label="core particles" value={summary.nParticles.toLocaleString()} />
          <Stat label="buffer particles" value={summary.nBuffer.toLocaleString()} hint="Dirichlet data interpolated from the mesh" />
          <Stat label="interface cells" value={summary.nInterface.toLocaleString()} hint="reconstructed from particles (OPMIS)" />
          <Stat label="covered cells" value={summary.nCovered.toLocaleString()} hint="switched off, the SPH core owns them" />
          <Stat label="full-SPH particles" value={summary.nFull.toLocaleString()} />
          <Stat
            label="particle saving"
            value={`${saving.toFixed(1)} %`}
            tone={saving > 0 ? "good" : "default"}
          />
          <Stat
            label="prunable particles"
            value={summary.nPrunable.toLocaleString()}
            tone={summary.nPrunable > 0 ? "warn" : "good"}
            hint="outside the current core + overlap band: removed when the adaptive region is re-sized"
          />
        </Row>
      </Panel>
      <Panel
        title="Interpolation operators"
        subtitle="consistency of the transfer stencils · probe a target in the viewport"
      >
        {table("particles → interface cells", summary.p2m)}
        {table("mesh → buffer particles", summary.m2p)}
        <Row>
          <Stat label="stencil size p→m" value={fmt(summary.p2m[scheme].stencilMean, 3)} />
          <Stat label="stencil size m→p" value={fmt(summary.m2p[scheme].stencilMean, 3)} />
          <Stat label="cond(M) p→m" value={fmt(summary.p2m[scheme].condMean, 3)} />
          <Stat label="empty stencils" value={String(summary.p2m[scheme].nEmpty + summary.m2p[scheme].nEmpty)} tone={summary.p2m[scheme].nEmpty + summary.m2p[scheme].nEmpty > 0 ? "bad" : "good"} />
        </Row>
        <Note>
          “over %” is the overshoot produced when a 1000:1 pressure jump crosses the
          stencil. MLS is first-order accurate (C₁ ≈ 0) but generates negative
          weights and overshoot; Shepard is monotone but only zeroth-order. OPMIS
          keeps MLS wherever the moment matrix is well conditioned and relaxes
          towards Shepard exactly where the stencil would go unstable.
        </Note>
      </Panel>
    </>
  );
}
