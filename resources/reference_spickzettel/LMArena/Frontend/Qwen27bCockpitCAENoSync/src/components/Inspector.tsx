import { useStore } from "../state/store";
import { METRIC_META } from "../geom/mesh";
import { FIELD_DEFS } from "../fields/analytics";
import { MATERIALS } from "../schema/registry";
import { parseExpr, fmtDim, fmtSI } from "../units/parser";

function Card({ title, accent, children }: { title: string; accent?: string; children: React.ReactNode }) {
  return (
    <div className="bg-[#18181C] border border-[#26272E] rounded-md p-2.5">
      <div className="mono text-[9px] tracking-wider mb-1.5" style={{ color: accent ?? "#63666F" }}>
        {title}
      </div>
      <div className="mono text-[10px] text-[#9DA0AA] leading-relaxed">{children}</div>
    </div>
  );
}
const Row = ({ k, v, c }: { k: string; v: string; c?: string }) => (
  <div className="flex justify-between gap-2">
    <span className="text-[#63666f]">{k}</span>
    <span style={{ color: c ?? "#E8E9ED" }} className="text-right break-all">{v}</span>
  </div>
);

export function Inspector() {
  const ws = useStore((s) => s.ws);
  const selPatch = useStore((s) => s.selPatch);
  const patches = useStore((s) => s.patches);
  const rev = useStore((s) => s.revisions);
  const st = useStore((s) => s.meshStats);
  const params = useStore((s) => s.params);
  const solver = useStore((s) => s.solver);
  const iter = useStore((s) => s.iter);
  const nNaN = useStore((s) => s.nNaN);
  const bcs = useStore((s) => s.bcs);
  const residuals = useStore((s) => s.residuals);
  const forces = useStore((s) => s.forces);
  const courant = useStore((s) => s.courant);
  const activeT = useStore((s) => s.activeT);
  const ts = useStore((s) => s.timesteps);

  const patch = selPatch !== null ? patches[selPatch] : null;
  const bc = selPatch !== null ? bcs.find((b) => b.patch === selPatch) : null;
  const last = (a: number[]) => (a.length ? a[a.length - 1] : 0);

  return (
    <div className="w-[262px] shrink-0 border-l border-[#26272E] bg-[#111217] overflow-y-auto p-2.5 space-y-2.5">
      {ws === 1 && (
        <>
          <Card title="SELECTION · face_group_id" accent="#004AFD">
            {patch ? (
              <>
                <Row k="patch" v={patch.name} c="#EF233C" />
                <Row k="kind" v={patch.kind} />
                <Row k="area" v={`${patch.area.toFixed(3)} m²`} />
                <Row k="normal" v={patch.normal.map((n) => n.toFixed(0)).join(", ")} />
                <Row k="tris" v={String(patch.nTris)} />
                <Row k="BC" v={bc ? `${bc.type}` : "unbound"} c={bc ? "#B9F500" : "#63666f"} />
                {bc && bc.rev < rev.geo && (
                  <div className="mt-1 text-[9px] text-[#FEC40B]">⚠ bound to r{bc.rev} — geo is r{rev.geo} → STALE, re-apply</div>
                )}
                <div className="mt-1.5 text-[9px] text-[#4A4B55]">→ WS3 · Patch BC Assignment to bind</div>
              </>
            ) : (
              <div className="text-[#4A4B55]">click a surface in the viewport — GPU ID-buffer ray-pick</div>
            )}
          </Card>
          <Card title="REVISIONS · zero-copy handle" accent="#B9F500">
            <Row k="geometry_revision" v={`#${rev.geo}`} />
            <Row k="mesh_revision" v={`#${rev.mesh}`} />
            <div className="mt-1 text-[9px] text-[#4A4B55] leading-snug">
              remesh invalidates stale patch bindings; re-apply via geometry.patches.update
            </div>
          </Card>
        </>
      )}

      {ws === 2 && st && (
        <>
          <Card title="MESH · CFDMSH02" accent="#B9F500">
            <Row k="cells" v={st.cells.toLocaleString()} />
            <Row k="vertices" v={st.verts.toLocaleString()} />
            <Row k="max skew" v={st.maxSkew.toFixed(3)} c={st.maxSkew > 0.9 ? "#EF233C" : undefined} />
            <Row k="mean non-orth" v={`${st.meanNonOrth.toFixed(1)}°`} />
            <Row k="max aspect" v={st.maxAspect.toFixed(1)} />
            <Row k="folded_face_indices" v={String(st.folded)} c={st.folded ? "#B9F500" : undefined} />
            <Row k="n_nan_cells" v={String(st.nan)} c={st.nan > 0 ? "#EF233C" : undefined} />
          </Card>
          <Card title="ACTIVE METRIC" accent="#004AFD">
            {(() => {
              const m = params.metric as keyof typeof METRIC_META;
              const meta = METRIC_META[m];
              return (
                <>
                  <Row k="metric" v={meta.label} />
                  <Row k="range" v={meta.unit} />
                  <Row k="threshold τ" v={Number(params.threshold).toFixed(3)} />
                  <div className="mt-1 text-[9px] text-[#4A4B55] leading-snug">
                    flat faceting: per-triangle constant color → defective cells cannot be smoothed by interpolation.
                  </div>
                </>
              );
            })()}
          </Card>
        </>
      )}

      {ws === 3 && (
        <>
          <Card title="UNIT-PARSER RESOLUTION" accent="#FC75B4">
            <Row k="patch" v={selPatch !== null ? String(selPatch) : "— pick surface"} />
            <Row k="patchType" v={String(params.bc_type)} c="#FC75B4" />
            {params.bc_type === "velocityInlet" && (
              <>
                <Row
                  k="|U|_inlet"
                  v={`${fmtSI(parseExpr(String(params.vel_mag)).si)} m·s⁻¹`}
                />
                <Row k="k₀" v={`${fmtSI(parseExpr(String(params.k0)).si)} m²·s⁻²`} />
                <Row k="ω₀" v={`${fmtSI(parseExpr(String(params.om0)).si)} s⁻¹`} />
              </>
            )}
            {params.bc_type === "pressureOutlet" && (
              <Row
                k={params.gauge ? "p_rgh (gauge)" : "p (absolute)"}
                v={`${fmtSI(parseExpr(String(params.p_ref)).si)} ${fmtDim([1, -1, -2, 0, 0, 0, 0])}`}
              />
            )}
            {params.bc_type === "wall" &&
              (params.wall_treatment === "yplus" ? (
                <Row k="y⁺ target (estimated)" v={String(params.yplus_target)} />
              ) : (
                <Row k="Δy₁ fixed layer" v={`${fmtSI(parseExpr(String(params.dy1)).si)} m`} />
              ))}
          </Card>
          <Card title="ONE-CLICK FLOW" accent="#004AFD">
            <div className="text-[9px] text-[#4A4B55] leading-relaxed">
              1. pick surface (ID buffer) → 2. set patchType → 3. apply → geometry.patches.update binds to current
              geometry_revision. p vs p_rgh and Δy₁ vs y⁺ are enforced by dimension vectors, not by convention.
            </div>
          </Card>
        </>
      )}

      {ws === 4 && (
        <>
          <Card title={`SOLVER · ${solver.toUpperCase()}`} accent={solver === "running" ? "#723BFA" : "#63666F"}>
            <Row k="iter" v={`${iter} / ${parseExpr(String(params.max_iter)).si | 0}`} />
            <Row k="Δt" v={`${fmtSI(parseExpr(String(params.dt)).si)} s`} />
            <Row k="co_mean / co_max" v={`${last(courant.mean).toFixed(2)} / ${last(courant.max).toFixed(2)}`} c={last(courant.max) > 2 ? "#EF233C" : undefined} />
            <Row k="Cd / Cl" v={`${last(forces.Cd).toFixed(4)} / ${last(forces.Cl).toFixed(4)}`} />
            <Row k="T_max" v={`${last(forces.Tmax).toFixed(2)} K`} />
            <Row k="n_nan_cells" v={String(nNaN)} c={nNaN > 0 ? "#EF233C" : "#B9F500"} />
          </Card>
          <Card title="RESIDUALS (last)" accent="#004AFD">
            {Object.entries(residuals).map(([k, a]) => (
              <Row key={k} k={k} v={a.length ? a[a.length - 1].toExponential(2) : "—"} />
            ))}
          </Card>
        </>
      )}

      {ws === 5 && (
        <>
          <Card title="LOADED INSTANCE" accent="#FC75B4">
            <Row k="time" v={`${activeT.toFixed(4)} s`} />
            <Row k="checkpoints" v={String(ts.length)} />
            <Row k="field" v={String(params.field_id)} />
            <Row
              k="range"
              v={`${(FIELD_DEFS.find((f) => f.id === params.field_id) ?? FIELD_DEFS[0]).range.join(" … ")}`}
            />
          </Card>
          <Card title="FIELD REGISTRY · CFDFLD03" accent="#B9F500">
            {FIELD_DEFS.map((f) => (
              <div key={f.id} className="flex justify-between">
                <span>{f.id}</span>
                <span className="text-[#63666f]">{f.unit}</span>
              </div>
            ))}
          </Card>
          <Card title="MATERIAL ACTIVE" accent="#FEC40B">
            <Row k="fluid" v={(MATERIALS.find((m) => m.id === params.mat_id) ?? MATERIALS[0]).name} />
          </Card>
        </>
      )}
    </div>
  );
}
