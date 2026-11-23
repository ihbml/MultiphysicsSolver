import { useEffect, useRef } from "react";
import { useStore, RES_COLORS } from "../state/store";
import { MATERIALS, PATCH_NAMES, DIMLESS } from "../schema/registry";
import { FIELD_DEFS, fieldStats } from "../fields/analytics";
import { fmtDim } from "../units/parser";
import { rpc } from "../ipc/backend";
import { engineRef } from "./Viewport";

// ── chart helpers ────────────────────────────────────────────────────────────
function useChartCanvas(draw: (ctx: CanvasRenderingContext2D, w: number, h: number) => void) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    let alive = true;
    const doDraw = () => {
      if (!alive) return;
      const cv = ref.current;
      if (!cv) return;
      const w = cv.clientWidth, h = cv.clientHeight;
      if (w < 10) return;
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      if (cv.width !== w * dpr) {
        cv.width = w * dpr;
        cv.height = h * dpr;
      }
      const ctx = cv.getContext("2d");
      if (!ctx) return;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);
      draw(ctx, w, h);
    };
    doDraw();
    const un = useStore.subscribe((s, prev) => {
      if (s.chartTick !== prev.chartTick || s.geoVersion !== prev.geoVersion) doDraw();
    });
    const ro = new ResizeObserver(doDraw);
    if (ref.current) ro.observe(ref.current);
    return () => {
      alive = false;
      un();
      ro.disconnect();
    };
  }, [draw]);
  return ref;
}

function ResidualChart() {
  const ref = useChartCanvas((ctx, w, h) => {
    const s = useStore.getState();
    ctx.strokeStyle = "#26272E";
    ctx.lineWidth = 1;
    for (const e of [0, 2, 4]) {
      const y = h - ((e + 2) / 6) * h;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
      ctx.fillStyle = "#4A4B55";
      ctx.font = "8px 'IBM Plex Mono'";
      ctx.fillText(`1e${e}`, 2, y - 2);
    }
    const len = Math.max(1, ...Object.values(s.residuals).map((a) => a.length));
    for (const [k, arr] of Object.entries(s.residuals)) {
      if (!arr.length) continue;
      ctx.strokeStyle = RES_COLORS[k] ?? "#fff";
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      arr.forEach((v, i) => {
        const x = (i / Math.max(1, len - 1)) * w;
        const y = h - (Math.min(4, Math.max(-2, Math.log10(Math.max(1e-8, v))) + 2) / 6) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }
  });
  return (
    <div className="relative flex-1 min-w-0">
      <div className="flex items-center gap-2 px-2 pt-1">
        <span className="mono text-[9px] text-[#9DA0AA]">RESIDUALS (log₁₀)</span>
        {Object.keys(RES_COLORS).map((k) => (
          <span key={k} className="mono text-[8px] flex items-center gap-0.5 text-[#63666f]">
            <span className="w-2 h-0.5 inline-block" style={{ background: RES_COLORS[k] }} />
            {k}
          </span>
        ))}
      </div>
      <canvas ref={ref} className="w-full h-[calc(100%-16px)]" />
    </div>
  );
}

function CourantChart() {
  const ref = useChartCanvas((ctx, w, h) => {
    const s = useStore.getState();
    const max = 3.6;
    ctx.strokeStyle = "#26272E";
    for (let i = 0; i <= 3; i++) {
      const y = h - (i / 3) * h * (3 / max);
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }
    const drawArr = (arr: number[], col: string) => {
      if (!arr.length) return;
      const len = Math.max(1, s.courant.mean.length);
      ctx.strokeStyle = col;
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      arr.forEach((v, i) => {
        const x = (i / Math.max(1, len - 1)) * w;
        const y = h - (Math.min(max, v) / max) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
    };
    const target = Number((s.params.cour_target as string).split(" ")[0]) || 0.8;
    ctx.strokeStyle = "#004AFD";
    ctx.setLineDash([3, 3]);
    const ty = h - (target / max) * h;
    ctx.beginPath(); ctx.moveTo(0, ty); ctx.lineTo(w, ty); ctx.stroke();
    ctx.setLineDash([]);
    const y2 = h - (2.0 / max) * h;
    ctx.strokeStyle = "rgba(239,35,60,0.5)";
    ctx.beginPath(); ctx.moveTo(0, y2); ctx.lineTo(w, y2); ctx.stroke();
    drawArr(s.courant.mean, "#723BFA");
    drawArr(s.courant.max, "#FEC40B");
  });
  return (
    <div className="relative flex-1 min-w-0">
      <div className="flex items-center gap-2 px-2 pt-1">
        <span className="mono text-[9px] text-[#9DA0AA]">COURANT</span>
        <span className="mono text-[8px] text-[#723BFA]">co_mean</span>
        <span className="mono text-[8px] text-[#FEC40B]">co_max</span>
        <span className="mono text-[8px] text-[#EF233C]">— limit 2.0</span>
      </div>
      <canvas ref={ref} className="w-full h-[calc(100%-16px)]" />
    </div>
  );
}

function ForceChart() {
  const ref = useChartCanvas((ctx, w, h) => {
    const s = useStore.getState();
    ctx.strokeStyle = "#26272E";
    for (let i = 0; i <= 3; i++) {
      const y = (i / 3) * h;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }
    const drawArr = (arr: number[], min: number, max: number, col: string) => {
      if (!arr.length) return;
      const len = Math.max(1, s.forces.Cd.length);
      ctx.strokeStyle = col;
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      arr.forEach((v, i) => {
        const x = (i / Math.max(1, len - 1)) * w;
        const y = h - ((Math.min(max, Math.max(min, v)) - min) / (max - min)) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
    };
    drawArr(s.forces.Cd, 0, 0.5, "#B9F500");
    drawArr(s.forces.Cl, -0.05, 0.12, "#004AFD");
    drawArr(s.forces.Tmax, 297, 306, "#FEC40B");
  });
  return (
    <div className="relative flex-1 min-w-0">
      <div className="flex items-center gap-2 px-2 pt-1">
        <span className="mono text-[9px] text-[#9DA0AA]">FORCES / T</span>
        <span className="mono text-[8px] text-[#B9F500]">Cd</span>
        <span className="mono text-[8px] text-[#7FA3FF]">Cl</span>
        <span className="mono text-[8px] text-[#FEC40B]">T_max [K]</span>
      </div>
      <canvas ref={ref} className="w-full h-[calc(100%-16px)]" />
    </div>
  );
}

function SkewHist() {
  const ref = useChartCanvas((ctx, w, h) => {
    const s = useStore.getState();
    const hist = s.meshStats?.hist ?? [];
    const thr = Number(s.params.threshold);
    ctx.strokeStyle = "#26272E";
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);
    const bw = w / 24;
    for (let i = 0; i < 24; i++) {
      const v = hist[i] ?? 0;
      ctx.fillStyle = i / 24 >= thr ? "rgba(239,35,60,0.85)" : "rgba(185,245,0,0.7)";
      ctx.fillRect(i * bw + 1, h - v * (h - 6), bw - 2, v * (h - 6));
    }
    const tx = thr * w;
    ctx.strokeStyle = "#E8E9ED";
    ctx.setLineDash([3, 2]);
    ctx.beginPath(); ctx.moveTo(tx, 0); ctx.lineTo(tx, h); ctx.stroke();
    ctx.setLineDash([]);
  });
  return <canvas ref={ref} className="w-[210px] h-[92px] shrink-0" />;
}

// ── per-workspace content ────────────────────────────────────────────────────
function DockWS1() {
  const patches = useStore((s) => s.patches);
  const bcs = useStore((s) => s.bcs);
  const stale = useStore((s) => s.stalePatches);
  const selPatch = useStore((s) => s.selPatch);
  const setSelPatch = useStore((s) => s.setSelPatch);
  return (
    <div className="flex gap-3 p-3 h-full overflow-hidden">
      <div className="flex-1 overflow-auto">
        <div className="mono text-[9px] text-[#63666f] mb-1.5">CFDGEO01 · face_group_id — click to select, then bind BC in WS3</div>
        <table className="w-full mono text-[10px]">
          <thead>
            <tr className="text-[#63666f] text-left">
              <th className="py-0.5 pr-2 font-normal">g</th>
              <th className="pr-2 font-normal">patch</th>
              <th className="pr-2 font-normal">kind</th>
              <th className="pr-2 font-normal text-right">area [m²]</th>
              <th className="pr-2 font-normal text-right">tris</th>
              <th className="font-normal">BC binding</th>
            </tr>
          </thead>
          <tbody>
            {patches.map((p) => {
              const bc = bcs.find((b) => b.patch === p.id);
              const isStale = bc ? stale.includes(p.id) || bc.rev < useStore.getState().revisions.geo : false;
              return (
                <tr
                  key={p.id}
                  onClick={() => setSelPatch(p.id)}
                  className={`cursor-pointer border-b border-[#1E1E24] hover:bg-[#18181C] ${
                    selPatch === p.id ? "bg-[#004AFD]/10" : ""
                  }`}
                >
                  <td className="py-1 pr-2 text-[#63666f]">{p.id}</td>
                  <td className="pr-2 text-[#E8E9ED]">{p.name}</td>
                  <td className="pr-2 text-[#9DA0AA]">{p.kind}</td>
                  <td className="pr-2 text-right text-[#9DA0AA]">{p.area.toFixed(2)}</td>
                  <td className="pr-2 text-right text-[#63666f]">{p.nTris}</td>
                  <td className="text-[#9DA0AA]">
                    {bc ? (
                      <span className={isStale ? "text-[#FEC40B]" : "text-[#B9F500]"}>
                        {bc.type}
                        {isStale && (
                          <span className="ml-1.5 px-1 rounded-sm bg-[#FEC40B]/15 border border-[#FEC40B]/40 text-[8px]">
                            STALE·r{bc.rev}
                          </span>
                        )}
                      </span>
                    ) : (
                      <span className="text-[#4A4B55]">—</span>
                    )}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      <div className="w-[230px] shrink-0 mono text-[10px] text-[#9DA0AA] leading-relaxed">
        <div className="text-[#63666f] mb-1">TOPOLOGY AUDIT</div>
        <div>
          <span className="text-[#EF233C]">■</span> open edge segments:{" "}
          <span className="text-[#E8E9ED]">{useStore.getState().patches.length ? (useStore.getState().params.leave_gap ? 2 : 0) : 0}</span>
        </div>
        <div>
          <span className="text-[#FC75B4]">■</span> non-manifold: <span className="text-[#E8E9ED]">1</span> (step corner)
        </div>
        <div className="mt-2 text-[#63666f]">VERTS f64 · NRM f32 · TRI u32</div>
        <div className="text-[#63666f]">
          face_group → <span className="text-[#B9F500]">u8/vert</span>
        </div>
        <div className="mt-2 text-[9px] text-[#4A4B55] leading-snug">
          Remesh bumps mesh_revision and invalidates stale patch bindings — re-apply via geometry.patches.update.
        </div>
      </div>
    </div>
  );
}

function DockWS2() {
  const st = useStore((s) => s.meshStats);
  const threshold = useStore((s) => s.params.threshold);
  if (!st) return <div className="p-3 mono text-[10px] text-[#63666f]">no mesh — run mesh.generate</div>;
  return (
    <div className="flex gap-4 p-3 h-full items-center">
      <div>
        <div className="mono text-[9px] text-[#63666f] mb-1">
          SKEWNESS HISTOGRAM <span className="text-[#E8E9ED]">thr={Number(threshold).toFixed(2)}</span> (red = isolated above)
        </div>
        <SkewHist />
      </div>
      <div className="grid grid-cols-4 gap-x-8 gap-y-2 mono text-[10px]">
        <Stat k="cells" v={st.cells.toLocaleString()} />
        <Stat k="vertices" v={st.verts.toLocaleString()} />
        <Stat k="max skew" v={st.maxSkew.toFixed(3)} c={st.maxSkew > 0.9 ? "#EF233C" : undefined} />
        <Stat k="mean non-orth" v={`${st.meanNonOrth.toFixed(1)}°`} />
        <Stat k="max aspect" v={st.maxAspect.toFixed(1)} />
        <Stat k="wsrd weights" v="1.0 / 0.02" />
        <Stat k="folded faces" v={String(st.folded)} c="#B9F500" />
        <Stat k="n_nan_cells" v={String(st.nan)} c={st.nan > 0 ? "#EF233C" : undefined} />
      </div>
      <div className="mono text-[9px] text-[#4A4B55] leading-relaxed w-[250px]">
        <div className="text-[#63666f]">FLAT-FACETING INVARIANT</div>
        location:"cell" metrics are rendered as independent flat triangles — zero vertex interpolation. Defective /
        NaN cells can never be averaged away. Threshold isolator dims cells below τ.
      </div>
    </div>
  );
}
function Stat({ k, v, c }: { k: string; v: string; c?: string }) {
  return (
    <div>
      <span className="text-[#63666f]">{k} </span>
      <span style={{ color: c ?? "#E8E9ED" }}>{v}</span>
    </div>
  );
}

function DockWS3() {
  const bcs = useStore((s) => s.bcs);
  const validate = useStore((s) => s.validate);
  const rev = useStore((s) => s.revisions);
  const matId = useStore((s) => s.params.mat_id);
  const mat = MATERIALS.find((m) => m.id === matId) ?? MATERIALS[0];
  return (
    <div className="flex gap-4 p-3 h-full overflow-hidden">
      <div className="flex-1 overflow-auto">
        <div className="mono text-[9px] text-[#63666f] mb-1.5">BOUNDARY STACK — FoamField.location: BoundaryPatch</div>
        {bcs.length === 0 && <div className="mono text-[10px] text-[#4A4B55]">no BCs bound — pick a patch in the viewport (WS1) then apply.</div>}
        {bcs.map((b) => (
          <div key={b.patch} className="flex items-center gap-2 mono text-[10px] py-0.5 border-b border-[#1E1E24]">
            <span className="text-[#004AFD] w-4">{b.patch}</span>
            <span className="text-[#E8E9ED] w-24">{PATCH_NAMES[b.patch]}</span>
            <span className="text-[#FC75B4] w-28">{b.type}</span>
            <span className="text-[#9DA0AA] flex-1 truncate">{b.label}</span>
            {b.rev < rev.geo ? (
              <span className="px-1 rounded-sm bg-[#FEC40B]/15 border border-[#FEC40B]/40 text-[8px] text-[#FEC40B]">STALE</span>
            ) : (
              <span className="text-[#B9F500] text-[8px]">r{b.rev}✓</span>
            )}
          </div>
        ))}
      </div>
      <div className="w-[300px] shrink-0 mono text-[10px] overflow-auto">
        <div className="text-[#63666f] mb-1">case.validate</div>
        {validate === null ? (
          <div className="text-[#4A4B55]">not run — click case.validate in the BC block</div>
        ) : (
          <div className={validate.valid ? "text-[#B9F500]" : "text-[#FEC40B]"}>
            {validate.valid ? "PASS — watertight, BCs complete, quality OK" : `${validate.warnings.length} warning(s):`}
            <div className="mt-1 space-y-0.5 max-h-[80px] overflow-auto">
              {validate.warnings.map((w, i) => (
                <div key={i} className="text-[9px] text-[#9DA0AA] leading-snug">· {w}</div>
              ))}
            </div>
          </div>
        )}
        <div className="text-[#63666f] mt-2 mb-1">MATERIAL — {mat.name}</div>
        <div className="text-[#9DA0AA]">
          ρ = {mat.rho} kg·m⁻³ &nbsp; μ = {mat.mu.toExponential(2)} kg·m⁻¹·s⁻¹
          <br />
          cp = {mat.cp} J·kg⁻¹·K⁻¹ &nbsp; k = {mat.cond} W·m⁻¹·K⁻¹
        </div>
      </div>
    </div>
  );
}

function DockWS4() {
  return (
    <div className="flex gap-2 p-2 h-full">
      <ResidualChart />
      <div className="w-px bg-[#26272E]" />
      <CourantChart />
      <div className="w-px bg-[#26272E]" />
      <ForceChart />
    </div>
  );
}

function DockWS5() {
  const ts = useStore((s) => s.timesteps);
  const tA = useStore((s) => s.tA);
  const tB = useStore((s) => s.tB);
  const setCompare = useStore((s) => s.setCompare);
  const fieldId = String(useStore((s) => s.params.field_id));
  const setActiveT = useStore((s) => s.setActiveT);
  const fd = FIELD_DEFS.find((f) => f.id === fieldId) ?? FIELD_DEFS[0];
  const a = ts.find((t) => t.t === tA) ?? ts[0];
  const b = ts[ts.length - 1];
  const sa = fieldStats(fieldId, a?.t ?? 0);
  const sb = fieldStats(fieldId, b?.t ?? 0);
  const d = {
    max: Math.abs(sb.max - sa.max),
    min: Math.abs(sb.min - sa.min),
    mean: Math.abs(sb.mean - sa.mean),
  };
  return (
    <div className="flex gap-4 p-3 h-full overflow-hidden">
      <div className="flex-1 overflow-hidden">
        <div className="mono text-[9px] text-[#63666f] mb-1.5">
          results.list_times — {ts.length} checkpoint(s) · click to results.load · A/B for compare
        </div>
        <div className="flex gap-1 overflow-x-auto pb-1 h-[38px] items-center">
          {ts.length === 0 && <span className="mono text-[10px] text-[#4A4B55]">solve to generate timesteps (auto write + solver.write_now)</span>}
          {ts.map((t) => (
            <button
              key={t.id}
              onClick={() => {
                setActiveT(t.t);
                rpc.call("results.load", { t: t.t }).catch(() => {});
              }}
              className={`mono text-[9px] px-1.5 py-1 rounded-sm border shrink-0 ${
                (a?.t === t.t || b?.t === t.t) ? "border-[#FC75B4] text-[#FC75B4]" : "border-[#32333C] text-[#9DA0AA] hover:border-[#4A4B55]"
              }`}
            >
              {"t=" + t.t.toFixed(3)}
              <span
                className="ml-1 cursor-pointer"
                onClick={(e) => {
                  e.stopPropagation();
                  if (tA === t.t && tB !== t.t) setCompare(t.t, tA);
                  else setCompare(tA || 0, t.t);
                }}
                title="toggle compare slot"
              >
                {a?.t === t.t ? "A" : b?.t === t.t ? "B" : "±"}
              </span>
            </button>
          ))}
        </div>
        <div className="mono text-[9px] text-[#63666f] mt-2 mb-1">
          Δ({fd.id}: tB − tA) — A: {a ? a.t.toFixed(4) : "—"} s · B: {b ? b.t.toFixed(4) : "—"} s
        </div>
        <div className="flex gap-4 mono text-[10px] text-[#9DA0AA]">
          <span>Δmax <span className="text-[#FC75B4]">{d.max.toFixed(3)}</span></span>
          <span>Δmin <span className="text-[#FC75B4]">{d.min.toFixed(3)}</span></span>
          <span>Δmean <span className="text-[#FC75B4]">{d.mean.toFixed(3)}</span></span>
          <span className="text-[#63666f]">{fd.unit}</span>
        </div>
      </div>
      <div className="w-[340px] shrink-0 overflow-auto mono text-[9px] leading-relaxed">
        <div className="text-[#63666f] mb-1">CFDFLD03 · FoamField — 7-elem SI dimensions</div>
        {FIELD_DEFS.map((f) => (
          <div key={f.id} className="flex gap-2">
            <span className={`w-14 ${f.id === fieldId ? "text-[#FC75B4]" : "text-[#9DA0AA]"}`}>{f.id}</span>
            <span className="text-[#63666f] w-[120px]">
              {fmtDim(fieldDims(f.id))}
            </span>
            <span className="text-[#4A4B55]">{f.wallOnly ? "BoundaryPatch" : "Cell"}</span>
          </div>
        ))}
        <div className="mt-2 text-[#4A4B55]">
          export: <button className="text-[#004AFD] hover:underline" onClick={() => engineRef.current?.exportPNG()}>viewport PNG</button>
        </div>
      </div>
    </div>
  );
}

function fieldDims(id: string): [number, number, number, number, number, number, number] {
  switch (id) {
    case "velocity": return [0, 1, -1, 0, 0, 0, 0];
    case "p": return [1, -1, -2, 0, 0, 0, 0];
    case "T": return [0, 0, 0, 1, 0, 0, 0];
    case "k": return [0, 2, -2, 0, 0, 0, 0];
    case "omega": return [0, 0, -1, 0, 0, 0, 0];
    default: return [...DIMLESS] as [number, number, number, number, number, number, number];
  }
}

export function Dock() {
  const ws = useStore((s) => s.ws);
  return (
    <div className="h-[178px] shrink-0 border-t border-[#26272E] bg-[#111217]">
      {ws === 1 && <DockWS1 />}
      {ws === 2 && <DockWS2 />}
      {ws === 3 && <DockWS3 />}
      {ws === 4 && <DockWS4 />}
      {ws === 5 && <DockWS5 />}
    </div>
  );
}

// ── NDJSON console strip ─────────────────────────────────────────────────────
export function ConsoleStrip() {
  const open = useStore((s) => s.consoleOpen);
  const lines = useStore((s) => s.console);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (ref.current) ref.current.scrollTop = ref.current.scrollHeight;
  }, [lines, open]);
  if (!open) return null;
  return (
    <div className="h-[110px] shrink-0 border-t border-[#26272E] bg-[#0B0C0F] overflow-y-auto mono text-[9px] leading-[1.55] px-3 py-1.5" ref={ref}>
      {lines.map((l, i) => (
        <div key={i} className="whitespace-nowrap overflow-hidden text-ellipsis">
          <span
            className={
              l.dir === "↑" ? "text-[#7FA3FF]" : l.dir === "↓" ? "text-[#B9F500]" : "text-[#723BFA]"
            }
          >
            {l.dir}
          </span>{" "}
          <span className={l.line.includes('"error"') || l.line.includes("EMERGENCY") ? "text-[#EF233C]" : "text-[#9DA0AA]"}>
            {l.line}
          </span>
        </div>
      ))}
    </div>
  );
}
