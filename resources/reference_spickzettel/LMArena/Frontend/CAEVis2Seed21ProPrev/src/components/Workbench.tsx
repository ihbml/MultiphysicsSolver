// The main CFD Cockpit workbench — combines all Stage 2 workspaces
// 1. BC Authoring (click face → smart rule synthesis → 3D highlight, conflict warning)
// 2. Gizmos (MRF rotor, spray injector, mic, farfield, porous)
// 3. Line-probe / chord slicer + engineering plots
// 4. A/B splitscreen diff (dual viewport, synchronized cameras, ΔCp, KPI table, Pareto)
// 5. SI-checked Field Calculator

import React, { useCallback, useMemo, useState } from 'react';
import { CFDViewport, defaultPatches } from './CFDViewport';
import type { FacePatch, SpatialRule, Gizmo, Vec3 } from '../types';
import { createRuleFromPatch } from '../smart-rules';
import { ScientificChart } from './ScientificChart';
import { chordwiseCp, chordwiseYPlus, chordwiseTw, uPlus, ransUPlus, deltaCp } from '../cfd-samplers';
import { FieldCalculator } from './FieldCalculator';
import type { CameraState } from './CFDViewport';

type Workspace = 'bc' | 'gizmos' | 'probe' | 'ab' | 'calc';

const WORKSPACES: { id: Workspace; label: string; icon: string; hint: string }[] = [
  { id: 'bc', label: 'BC Rules', icon: '⛝', hint: 'Click-to-author spatial boundary rules' },
  { id: 'gizmos', label: 'Gizmos', icon: '◎', hint: 'MRF · Injectors · Mics · Porous Axes' },
  { id: 'probe', label: 'Line Probe', icon: '⟋', hint: '3D line probe / chord slice + 1D plots' },
  { id: 'ab', label: 'A/B Diff', icon: '⇔', hint: 'Multi-run splitscreen differential view' },
  { id: 'calc', label: 'Field Calc', icon: '∑', hint: 'SI-checked Rhai field calculator' },
];

// ---------- KPI / run data ----------
interface Run { name: string; cd: number; p0_loss: number; tMax: number; color: string; }
const RUN_A: Run = { name: 'Baseline (Run A)', cd: 0.0284, p0_loss: 4.72, tMax: 312.4, color: '#60A5FA' };
const RUN_B: Run = { name: 'Adjoint-Optimized (Run B)', cd: 0.0251, p0_loss: 3.88, tMax: 305.8, color: '#F59E0B' };

// ---------- Chart data helpers ----------
function linspace(a: number, b: number, n: number): number[] {
  const out = new Array(n);
  for (let i = 0; i < n; i++) out[i] = a + (b - a) * (i / (n - 1));
  return out;
}

export const Workbench: React.FC = () => {
  const [ws, setWs] = useState<Workspace>('bc');
  const [syncCam, setSyncCam] = useState<CameraState | null>(null);

  // ---------- BC authoring state ----------
  const [rules, setRules] = useState<SpatialRule[]>([]);
  const [selectedPatchId, setSelectedPatchId] = useState<number | null>(null);
  const [lastPickedPatch, setLastPickedPatch] = useState<FacePatch | null>(null);

  const handlePickPatch = useCallback((patch: FacePatch) => {
    setSelectedPatchId(patch.id);
    setLastPickedPatch(patch);
  }, []);

  const addRuleFromSelected = useCallback(() => {
    if (!lastPickedPatch) return;
    const r = createRuleFromPatch(lastPickedPatch);
    setRules((prev) => {
      // Detect conflicts (overlapping face IDs)
      const claimed = new Set<number>();
      for (const ex of prev) for (const fid of ex.matchedFaceIds) claimed.add(fid);
      const conflict = r.matchedFaceIds.some((id) => claimed.has(id));
      // Also mark any existing rule that now overlaps with our new faces as conflicting
      const updated = prev.map((pr) => {
        const overlaps = pr.matchedFaceIds.some((id) => r.matchedFaceIds.includes(id));
        return overlaps ? { ...pr, conflicting: true } : pr;
      });
      return [...updated, { ...r, conflicting: conflict }];
    });
  }, [lastPickedPatch]);

  const deleteRule = useCallback((id: string) => {
    setRules((prev) => {
      const next = prev.filter((r) => r.id !== id);
      // Re-evaluate conflicts among remaining rules
      const claimed = new Map<number, string>();
      const conflicts = new Set<string>();
      for (const r of next) {
        for (const fid of r.matchedFaceIds) {
          if (claimed.has(fid)) {
            conflicts.add(claimed.get(fid)!);
            conflicts.add(r.id);
          } else {
            claimed.set(fid, r.id);
          }
        }
      }
      return next.map((r) => ({ ...r, conflicting: conflicts.has(r.id) }));
    });
  }, []);

  const setRuleBcType = useCallback((id: string, bcType: SpatialRule['bcType']) => {
    setRules((prev) => prev.map((r) => r.id === id ? { ...r, bcType } : r));
  }, []);

  // ---------- Gizmos state ----------
  const [gizmos, setGizmos] = useState<Gizmo[]>([
    { id: 'ff1', kind: 'farfield', position: [0, 0, 0], axis: [1, 0, 0] },
    { id: 'mrf1', kind: 'mrf', position: [1.5, 0.35, 0], axis: [1, 0, 0], radius: 0.22, height: 0.35, rpm: 24000 },
    { id: 'spray1', kind: 'spray', position: [-0.45, 0.1, 0], axis: [1, 0.05, 0], coneHalfAngle: 15 * Math.PI / 180, d32: 25e-6 },
    { id: 'mic1', kind: 'mic', position: [2.0, 0.9, 0.7], axis: [0, 0, 1], distanceSphere: 0.8 },
    { id: 'por1', kind: 'porous', position: [0.6, 0.1, 0], axis: [0, 1, 0] },
  ]);

  const tipMachForMrf = (g: Gizmo) => {
    const r = g.radius ?? 0.22;
    const rpm = g.rpm ?? 0;
    const omega = rpm * 2 * Math.PI / 60;
    const Utip = omega * r;
    const c = 340.0; // speed of sound m/s at ISA
    return Utip / c;
  };

  const updateGizmo = (id: string, patch: Partial<Gizmo>) => {
    setGizmos((prev) => prev.map((g) => g.id === id ? { ...g, ...patch } : g));
  };
  const toggleGizmo = (id: string) => {
    setGizmos((prev) => {
      const exists = prev.some((g) => g.id === id);
      if (exists) return prev.filter((g) => g.id !== id);
      return prev;
    });
  };

  // ---------- Line probe state ----------
  const [pA, setPA] = useState<Vec3>([1.0, -0.06, 0]);
  const [pB, setPB] = useState<Vec3>([2.0, 0.06, 0]);
  const [probeMode, setProbeMode] = useState<'chord' | 'line' | 'bl'>('chord');

  // Chordwise Cp data
  const xc = useMemo(() => linspace(0.0, 1.0, 80), []);
  const cpSuction = useMemo(() => xc.map((x) => chordwiseCp(x, true)), [xc]);
  const cpPressure = useMemo(() => xc.map((x) => chordwiseCp(x, false)), [xc]);
  const yplusArr = useMemo(() => xc.map(chordwiseYPlus), [xc]);
  const twSuction = useMemo(() => xc.map((x) => chordwiseTw(x, true)), [xc]);

  // Boundary-layer u+(y+) data
  const ypSamp = useMemo(() => {
    const out: number[] = [];
    for (let y = 0.5; y <= 500; y *= 1.08) out.push(y);
    return out;
  }, []);
  const uVisc = ypSamp.map((yp) => yp);
  const uLog = ypSamp.map((yp) => uPlus(yp));
  const uRans = ypSamp.map((yp) => ransUPlus(yp));

  // A/B delta data
  const dcpSuction = useMemo(() => xc.map((x) => deltaCp(x, true)), [xc]);

  const lineProbeFor3D = ws === 'probe' ? { pA, pB } : null;

  const sharedGizmosForBC = ws === 'bc' ? [] : ws === 'gizmos' ? gizmos : ws === 'probe' ? [] : ws === 'ab' ? gizmos.filter((g) => g.kind === 'farfield' || g.kind === 'mrf') : [];

  // ---------- Left sidebar ----------
  const sidebar = (
    <div className="flex flex-col gap-1 p-2 bg-[#0b0c10] border-r border-zinc-800">
      <div className="px-2 py-2 border-b border-zinc-800 mb-1">
        <div className="text-[10px] font-mono text-zinc-500">CFD COCKPIT</div>
        <div className="text-sm font-bold text-zinc-100 tracking-tight">Stage 2 · Workbench</div>
      </div>
      {WORKSPACES.map((w) => (
        <button
          key={w.id}
          onClick={() => setWs(w.id)}
          className={`flex flex-col items-start gap-0.5 px-2 py-2 rounded text-left transition border ${
            ws === w.id
              ? 'bg-[#004AFD]/20 border-[#004AFD]/60 text-white'
              : 'border-transparent hover:bg-zinc-800/50 text-zinc-400 hover:text-zinc-200'
          }`}
        >
          <div className="flex items-center gap-2 w-full">
            <span className="text-base leading-none">{w.icon}</span>
            <span className="text-[12.5px] font-semibold">{w.label}</span>
          </div>
          <div className="text-[10px] font-mono text-zinc-500 pl-6 leading-tight">{w.hint}</div>
        </button>
      ))}
      <div className="mt-auto pt-3 border-t border-zinc-800 text-[10px] font-mono text-zinc-500 px-1 space-y-0.5">
        <div className="text-zinc-400 font-semibold mb-1">Pipeline</div>
        <div><span className="text-emerald-400">●</span> wgpu Reverse-Z</div>
        <div><span className="text-emerald-400">●</span> CFDGEO01 / CFDMSH02</div>
        <div><span className="text-emerald-400">●</span> CFDFLD03 · Oklab LUT</div>
        <div><span className="text-emerald-400">●</span> Surface-LIC</div>
        <div><span className="text-amber-400">●</span> NDJSON IPC: 42 msg/s</div>
      </div>
    </div>
  );

  // ---------- Right panel content varies per workspace ----------
  const rightPanel = (() => {
    if (ws === 'bc') {
      return (
        <div className="flex flex-col gap-3 p-3 overflow-y-auto">
          <div className="p-3 rounded-md border border-zinc-800 bg-[#111217]">
            <h3 className="text-sm font-semibold text-zinc-100 mb-1">Smart Spatial Rule Synthesizer</h3>
            <p className="text-[11px] text-zinc-400 leading-relaxed">
              Click any CAD face in the viewport. The synthesizer inspects the patch's normal cone, centroid, bounding box,
              cylindrical radius, and STEP tag to propose a <em>topology-independent</em> rule. Matched faces highlight{' '}
              <span className="text-[#3b82ff] font-semibold">Electric Blue (#004AFD)</span>; overlap conflicts highlight{' '}
              <span className="text-[#ef4444] font-semibold">Red (#EF233C)</span>.
            </p>
          </div>
          {lastPickedPatch ? (
            <div className="p-3 rounded-md border border-[#004AFD]/40 bg-[#004AFD]/10">
              <div className="text-[11px] font-mono text-sky-300 mb-1">PICKED PATCH · face_group_id = {lastPickedPatch.id}</div>
              <div className="text-xs text-zinc-300 font-mono grid grid-cols-2 gap-x-2 gap-y-0.5">
                <div>centroid:</div><div>[{lastPickedPatch.centroid.map((v) => v.toFixed(3)).join(', ')}]</div>
                <div>normal:</div><div>[{lastPickedPatch.normal.map((v) => v.toFixed(3)).join(', ')}]</div>
                <div>cone θ:</div><div>{(lastPickedPatch.normalConeHalfAngle * 57.3).toFixed(2)}°</div>
                <div>cyl r_yz:</div><div>{lastPickedPatch.cylRadius.toFixed(4)} m</div>
                <div>STEP tag:</div><div className="truncate">{lastPickedPatch.stepTag}</div>
              </div>
              <button
                onClick={addRuleFromSelected}
                className="mt-2.5 w-full px-3 py-1.5 rounded bg-[#004AFD] hover:bg-[#1a5fff] text-white text-xs font-semibold"
              >
                Synthesize Rule ⇒
              </button>
            </div>
          ) : (
            <div className="p-3 rounded-md border border-dashed border-zinc-700 bg-[#111217] text-[11px] text-zinc-500 text-center">
              Click a CAD face (inlet, outlet, wall, airfoil, hub) in the 3D viewport to begin authoring a rule.
            </div>
          )}

          <div>
            <div className="flex items-center justify-between mb-1.5">
              <h3 className="text-sm font-semibold text-zinc-100">Authorized Boundary Conditions</h3>
              <span className="text-[10px] font-mono text-zinc-500">{rules.length} rules</span>
            </div>
            {rules.length === 0 && (
              <div className="p-2 rounded border border-dashed border-zinc-700 bg-[#111217] text-[11px] text-zinc-500">
                No rules yet.
              </div>
            )}
            <div className="space-y-2">
              {rules.map((r) => (
                <div
                  key={r.id}
                  className={`p-2.5 rounded-md border text-[11.5px] font-mono leading-relaxed ${
                    r.conflicting
                      ? 'border-red-500/60 bg-red-500/10'
                      : 'border-zinc-700 bg-[#18181C]'
                  }`}
                >
                  <div className="flex items-center justify-between gap-2">
                    <div className="flex items-center gap-2">
                      <span
                        className="w-2.5 h-2.5 rounded-sm"
                        style={{ background: r.conflicting ? '#EF233C' : r.color }}
                      />
                      <span className="text-zinc-100 font-semibold text-[12px] font-sans">{r.label}</span>
                      <select
                        value={r.bcType}
                        onChange={(e) => setRuleBcType(r.id, e.target.value as SpatialRule['bcType'])}
                        className="text-[10px] bg-[#0b0c10] border border-zinc-700 rounded px-1 py-0.5 text-zinc-300"
                      >
                        <option value="wall">wall</option>
                        <option value="inlet">inlet</option>
                        <option value="outlet">outlet</option>
                        <option value="farfield">farfield</option>
                        <option value="symmetry">symmetry</option>
                        <option value="mrf">mrf</option>
                      </select>
                    </div>
                    <button
                      onClick={() => deleteRule(r.id)}
                      className="text-[10px] text-zinc-500 hover:text-red-400"
                    >✕</button>
                  </div>
                  {r.conflicting && (
                    <div className="mt-1 text-red-300 text-[10.5px] font-sans">
                      ⚠ Overlap: face(s) already claimed by another BC. Refine predicate or reassign.
                    </div>
                  )}
                  <pre className="mt-1.5 text-[10.5px] text-sky-200 whitespace-pre-wrap break-all">
{r.expr}
                  </pre>
                  <div className="mt-1 text-[10px] text-zinc-500 font-sans">
                    <span className="text-emerald-400">∧</span> {r.humanReadable.replace(/\n\s*∧\s*/g, ' · ')}
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>
      );
    }
    if (ws === 'gizmos') {
      return (
        <div className="flex flex-col gap-3 p-3 overflow-y-auto">
          <div className="p-3 rounded-md border border-zinc-800 bg-[#111217]">
            <h3 className="text-sm font-semibold text-zinc-100 mb-1">3D Domain / Physics Actor Gizmos</h3>
            <p className="text-[11px] text-zinc-400">
              Drag the 3D handles in the viewport (values mirrored here for precision). Live readouts update the Rust
              MRF RPM → tip-speed Mach and spray Rosin-Rammler d<sub>32</sub>.
            </p>
          </div>
          {gizmos.map((g) => {
            const enabled = true;
            return (
              <div key={g.id} className="p-2.5 rounded-md border border-zinc-700 bg-[#18181C]">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <span className="text-[12px] font-semibold text-zinc-100">
                      {g.kind === 'mrf' && 'MRF Rotor Zone'}
                      {g.kind === 'spray' && 'DPM Spray Injector'}
                      {g.kind === 'mic' && 'FWH Acoustic Mic'}
                      {g.kind === 'farfield' && 'Wind-Tunnel Farfield'}
                      {g.kind === 'porous' && 'Anisotropic Porous Zone'}
                    </span>
                    <span className="text-[10px] font-mono text-zinc-500">{g.id}</span>
                  </div>
                  <button
                    onClick={() => toggleGizmo(g.id)}
                    className="text-[10px] text-zinc-400 hover:text-red-400"
                  >{enabled ? 'hide' : 'show'}</button>
                </div>
                <div className="grid grid-cols-3 gap-1.5 mt-2">
                  {(['x', 'y', 'z'] as const).map((ax, i) => (
                    <label key={ax} className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">pos {ax}</span>
                      <input
                        type="number"
                        step={0.01}
                        value={g.position[i].toFixed(3)}
                        onChange={(e) => {
                          const np = [...g.position] as Vec3;
                          np[i] = parseFloat(e.target.value);
                          updateGizmo(g.id, { position: np });
                        }}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200"
                      />
                    </label>
                  ))}
                </div>
                {g.kind === 'mrf' && (
                  <div className="grid grid-cols-2 gap-1.5 mt-2">
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">radius [m]</span>
                      <input type="number" step={0.01} value={g.radius} onChange={(e) => updateGizmo(g.id, { radius: parseFloat(e.target.value) })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">height [m]</span>
                      <input type="number" step={0.01} value={g.height} onChange={(e) => updateGizmo(g.id, { height: parseFloat(e.target.value) })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">RPM</span>
                      <input type="number" step={100} value={g.rpm} onChange={(e) => updateGizmo(g.id, { rpm: parseFloat(e.target.value) })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                    <div className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">Utip / c (Mach)</span>
                      <div className={`px-1 py-0.5 text-[11px] font-mono rounded border ${
                        tipMachForMrf(g) > 1.0 ? 'text-red-400 border-red-500/50 bg-red-500/10' : 'text-emerald-300 border-emerald-700/40 bg-emerald-500/5'
                      }`}>{tipMachForMrf(g).toFixed(3)}</div>
                    </div>
                  </div>
                )}
                {g.kind === 'spray' && (
                  <div className="grid grid-cols-2 gap-1.5 mt-2">
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">θcone [°]</span>
                      <input type="number" step={1} value={((g.coneHalfAngle ?? 0) * 57.3).toFixed(1)}
                        onChange={(e) => updateGizmo(g.id, { coneHalfAngle: parseFloat(e.target.value) * Math.PI / 180 })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">d32 [µm]</span>
                      <input type="number" step={1} value={((g.d32 ?? 0) * 1e6).toFixed(1)}
                        onChange={(e) => updateGizmo(g.id, { d32: parseFloat(e.target.value) * 1e-6 })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                  </div>
                )}
                {g.kind === 'mic' && (
                  <div className="grid grid-cols-1 gap-1.5 mt-2">
                    <label className="flex flex-col">
                      <span className="text-[9px] font-mono text-zinc-500">distance sphere r [m]</span>
                      <input type="number" step={0.05} value={g.distanceSphere}
                        onChange={(e) => updateGizmo(g.id, { distanceSphere: parseFloat(e.target.value) })}
                        className="px-1 py-0.5 text-[10.5px] font-mono bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200" />
                    </label>
                  </div>
                )}
                {g.kind === 'farfield' && (
                  <div className="mt-2 text-[10.5px] text-zinc-400 font-mono leading-relaxed">
                    Sizing: 5L upstream · 15L downstream · 5L cross-stream (L = 1.0 m chord)
                  </div>
                )}
                {g.kind === 'porous' && (
                  <div className="mt-2 text-[10.5px] text-zinc-400">
                    Principal axes: <span className="text-red-400">e₁</span>, <span className="text-emerald-400">e₂</span>,{' '}
                    <span className="text-sky-400">e₃</span>
                  </div>
                )}
              </div>
            );
          })}
        </div>
      );
    }
    if (ws === 'probe') {
      return (
        <div className="flex flex-col gap-3 p-3 overflow-y-auto">
          <div className="p-3 rounded-md border border-zinc-800 bg-[#111217]">
            <h3 className="text-sm font-semibold text-zinc-100 mb-1">Line-Probe & Chord Slicer</h3>
            <p className="text-[11px] text-zinc-400">
              Trilinear/barycentric interpolation through the CFDFLD03 volume. Switch between an arbitrary 3D line, a
              chordwise slice (x/c ∈ [0,1] on the airfoil), and a wall-normal BL profile.
            </p>
            <div className="flex gap-1 mt-2">
              {(['chord', 'line', 'bl'] as const).map((m) => (
                <button
                  key={m}
                  onClick={() => setProbeMode(m)}
                  className={`px-2 py-1 rounded text-[11px] font-semibold border ${
                    probeMode === m
                      ? 'bg-[#004AFD]/20 border-[#004AFD]/60 text-white'
                      : 'border-zinc-700 bg-[#0b0c10] text-zinc-400 hover:text-zinc-200'
                  }`}
                >{m === 'chord' ? 'Chordwise (x/c)' : m === 'line' ? '3D Line Probe' : 'BL u+(y+)'}</button>
              ))}
            </div>
          </div>

          {probeMode === 'line' && (
            <div className="p-3 rounded-md border border-zinc-700 bg-[#18181C]">
              <div className="grid grid-cols-2 gap-2 text-[11px] font-mono">
                {(['pA', 'pB'] as const).map((n) => {
                  const p = n === 'pA' ? pA : pB;
                  const setP = n === 'pA' ? setPA : setPB;
                  return (
                    <div key={n}>
                      <div className="text-emerald-300 font-semibold mb-1">{n}</div>
                      {([0, 1, 2] as const).map((i) => (
                        <label key={i} className="flex items-center justify-between mb-0.5">
                          <span className="text-zinc-500">{['x', 'y', 'z'][i]}</span>
                          <input
                            type="number"
                            step={0.01}
                            value={p[i].toFixed(3)}
                            onChange={(e) => {
                              const np = [...p] as Vec3;
                              np[i] = parseFloat(e.target.value);
                              setP(np);
                            }}
                            className="w-20 px-1 py-0.5 bg-[#0b0c10] border border-zinc-700 rounded text-zinc-200"
                          />
                        </label>
                      ))}
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          <div className="p-3 rounded-md border border-zinc-800 bg-[#111217]">
            <h3 className="text-[12px] font-semibold text-zinc-100 mb-2">Active Field</h3>
            <div className="flex flex-wrap gap-1">
              {['p', 'Cp', 'U', 'T', 'yPlus', 'tau_w', 'Nu', 'Mach'].map((f) => (
                <span key={f} className="px-2 py-0.5 rounded text-[10.5px] font-mono border border-zinc-700 bg-[#0b0c10] text-zinc-300">{f}</span>
              ))}
            </div>
            <div className="mt-2 text-[10.5px] text-zinc-500">80 trilinear samples · barycentric STL probe · 4.2 ms/frame</div>
          </div>
        </div>
      );
    }
    if (ws === 'ab') {
      const dCd = (RUN_B.cd - RUN_A.cd) * 1e4; // drag counts
      const dP0 = RUN_B.p0_loss - RUN_A.p0_loss;
      const dT = RUN_B.tMax - RUN_A.tMax;
      return (
        <div className="flex flex-col gap-3 p-3 overflow-y-auto">
          <div className="p-3 rounded-md border border-zinc-800 bg-[#111217]">
            <h3 className="text-sm font-semibold text-zinc-100 mb-1">Synchronized A/B Differential View</h3>
            <p className="text-[11px] text-zinc-400">
              Camera orbit/pan/zoom linked at 120 FPS. Δφ = φ<sub>B</sub> − φ<sub>A</sub> computed on GPU with Moreland
              Cool-Warm Oklab diverging LUT, centered strictly at Δφ = 0.
            </p>
          </div>

          <div className="p-3 rounded-md border border-zinc-700 bg-[#18181C]">
            <h3 className="text-[12px] font-semibold text-zinc-200 mb-2">Delta KPI Summary</h3>
            <div className="grid grid-cols-2 gap-2 text-[11.5px] font-mono">
              <KPI label="ΔCd" val={dCd} unit="drag counts" goodIf="negative" />
              <KPI label="Δp₀ loss" val={dP0} unit="%" goodIf="negative" />
              <KPI label="ΔTmax" val={dT} unit="K" goodIf="negative" />
              <KPI label="Δη_tt" val={+0.82} unit="pts" goodIf="positive" />
            </div>
          </div>

          <div className="p-3 rounded-md border border-zinc-700 bg-[#18181C]">
            <h3 className="text-[12px] font-semibold text-zinc-200 mb-2">Runs</h3>
            <div className="space-y-1.5">
              {[RUN_A, RUN_B].map((r) => (
                <div key={r.name} className="flex items-center gap-2 text-[11px] font-mono">
                  <span className="w-3 h-3 rounded-sm" style={{ background: r.color }} />
                  <span className="text-zinc-200 flex-1">{r.name}</span>
                  <span className="text-zinc-400">Cd={r.cd.toFixed(4)}</span>
                  <span className="text-zinc-400">p₀={r.p0_loss.toFixed(2)}%</span>
                </div>
              ))}
            </div>
          </div>

          <div className="p-3 rounded-md border border-zinc-700 bg-[#18181C]">
            <h3 className="text-[12px] font-semibold text-zinc-200 mb-2">Pareto Front (Cd ← → p₀ ↓)</h3>
            <ParetoChart />
          </div>
        </div>
      );
    }
    if (ws === 'calc') return <FieldCalculator />;
    return null;
  })();

  // ---------- Bottom plots area ----------
  const bottomPlots = (() => {
    if (ws === 'probe' && probeMode === 'chord') {
      return (
        <div className="grid grid-cols-1 xl:grid-cols-3 gap-2 p-2 bg-[#0b0c10] border-t border-zinc-800">
          <ScientificChart
            title="Chordwise Pressure Coefficient · Cp(x/c)"
            xLabel="x/c [-]" yLabel="Cp [-]"
            invertY
            series={[
              { label: 'Suction side (Run A)', x: xc, y: cpSuction, color: '#60A5FA' },
              { label: 'Pressure side (Run A)', x: xc, y: cpPressure, color: '#F59E0B' },
            ]}
            height={260}
          />
          <ScientificChart
            title="Wall y+(x/c) · target bands"
            xLabel="x/c [-]" yLabel="y+ [-]"
            series={[{ label: 'y+ wall', x: xc, y: yplusArr, color: '#A78BFA' }]}
            bands={[
              { yMin: 0, yMax: 1, color: 'rgba(34,197,94,0.08)', label: '' },
              { yMin: 1, yMax: 30, color: 'rgba(245,158,11,0.06)' },
            ]}
            hlines={[
              { y: 1, color: '#22C55E', dash: [4, 3], label: 'y+=1 viscous' },
              { y: 30, color: '#F59E0B', dash: [4, 3], label: 'y+=30 log-layer' },
            ]}
            height={260}
          />
          <ScientificChart
            title="Wall Temperature Tw(x/c)"
            xLabel="x/c [-]" yLabel="Tw [K]"
            series={[{ label: 'Tw suction', x: xc, y: twSuction, color: '#EF4444' }]}
            height={260}
          />
        </div>
      );
    }
    if (ws === 'probe' && probeMode === 'bl') {
      return (
        <div className="grid grid-cols-1 xl:grid-cols-2 gap-2 p-2 bg-[#0b0c10] border-t border-zinc-800">
          <ScientificChart
            title="Law of the Wall · u+(y+)"
            xLabel="y+ [-]" yLabel="u+ [-]"
            series={[
              { label: 'u+ = y+ (viscous)', x: ypSamp, y: uVisc, color: '#22C55E', dash: [3, 3], width: 1.25 },
              { label: 'κ⁻¹ ln(E y+) (log law)', x: ypSamp, y: uLog, color: '#F59E0B', dash: [3, 3], width: 1.25 },
              { label: 'RANS (k-ω SST)', x: ypSamp, y: uRans, color: '#60A5FA', width: 2 },
            ]}
            xMin={0.5} xMax={500}
            height={280}
          />
          <div className="p-3 rounded border border-zinc-800 bg-[#111217]">
            <h4 className="text-[12px] font-semibold text-zinc-100 mb-2">Boundary-Layer Integrals</h4>
            <div className="text-[11px] font-mono text-zinc-300 space-y-1">
              <Row k="δ₉₉" v="1.72 mm" />
              <Row k="θ (mom.)" v="0.21 mm" />
              <Row k="δ* (disp.)" v="0.32 mm" />
              <Row k="H = δ*/θ" v="1.52" />
              <Row k="Reθ" v="2410" />
              <Row k="Cf (avg)" v="0.00312" />
            </div>
            <h4 className="text-[12px] font-semibold text-zinc-100 mt-3 mb-1">Marker bands</h4>
            <ul className="text-[10.5px] text-zinc-400 space-y-0.5">
              <li><span className="inline-block w-3 h-0.5 bg-[#22C55E] mr-2 align-middle"/>Viscous sublayer y+ ≤ 5</li>
              <li><span className="inline-block w-3 h-0.5 bg-[#F59E0B] mr-2 align-middle"/>Buffer layer 5 &lt; y+ &lt; 30</li>
              <li><span className="inline-block w-3 h-0.5 bg-[#60A5FA] mr-2 align-middle"/>Log layer y+ ≥ 30</li>
            </ul>
          </div>
        </div>
      );
    }
    if (ws === 'ab') {
      return (
        <div className="grid grid-cols-1 xl:grid-cols-2 gap-2 p-2 bg-[#0b0c10] border-t border-zinc-800">
          <ScientificChart
            title="ΔCp(x/c) — Run B − Run A"
            xLabel="x/c [-]" yLabel="ΔCp [-]"
            series={[
              { label: 'ΔCp suction', x: xc, y: dcpSuction, color: '#A78BFA', width: 2 },
            ]}
            hlines={[
              { y: 0, color: '#9CA3AF', dash: [2, 2], width: 1 },
            ]}
            invertY={false}
            height={260}
          />
          <div className="p-3 rounded border border-zinc-800 bg-[#111217] flex flex-col">
            <h4 className="text-[12px] font-semibold text-zinc-100 mb-2">Field Differential Stats</h4>
            <div className="text-[11px] font-mono text-zinc-300 space-y-1">
              <Row k="Vol. median ΔCp" v="+0.003" />
              <Row k="Vol. RMS ΔCp" v="0.021" />
              <Row k="Max |ΔCp|" v="0.154 (shock foot)" />
              <Row k="GPU Δ kernel" v="trilinear · 2.1 ms @ 4M cells" />
              <Row k="LUT" v="Moreland Cool-Warm Oklab" />
              <Row k="Symmetry" v="centered @ Δ = 0 (exact)" />
            </div>
            <div className="mt-auto pt-2">
              <div className="h-6 rounded border border-zinc-700 overflow-hidden"
                style={{
                  background: 'linear-gradient(to right, #3b82f6 0%, #60a5fa 25%, #f8fafc 50%, #f59e0b 75%, #ef4444 100%)',
                }}
              />
              <div className="flex justify-between text-[9.5px] font-mono text-zinc-400 mt-0.5">
                <span>−0.2</span><span>0</span><span>+0.2</span>
              </div>
            </div>
          </div>
        </div>
      );
    }
    return null;
  })();

  // ---------- Viewport(s) ----------
  const viewportArea = (() => {
    if (ws === 'ab') {
      return (
        <div className="absolute inset-0 flex">
          <div className="w-1/2 h-full relative">
            <CFDViewport
              label="Run A · BASELINE"
              patches={defaultPatches}
              rules={rules}
              gizmos={sharedGizmosForBC}
              selectedPatchId={selectedPatchId}
              onPickPatch={handlePickPatch}
              splitMode="left"
              deltaMode={false}
              onCamera={setSyncCam}
              slaveTo={syncCam}
            />
          </div>
          <div className="w-px bg-zinc-700 relative">
            <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-6 h-6 rounded-full bg-[#111217] border border-zinc-600 flex items-center justify-center text-[10px] font-bold text-zinc-300">Δ</div>
          </div>
          <div className="w-1/2 h-full relative">
            <CFDViewport
              label="Run B · ADJOINT-OPTIMIZED · Δφ"
              patches={defaultPatches}
              rules={rules}
              gizmos={sharedGizmosForBC}
              selectedPatchId={selectedPatchId}
              onPickPatch={handlePickPatch}
              splitMode="right"
              deltaMode
              onCamera={setSyncCam}
              slaveTo={syncCam}
            />
          </div>
        </div>
      );
    }
    return (
      <CFDViewport
        label={ws === 'bc' ? 'BC Authoring Viewport' : ws === 'gizmos' ? 'Physics Actor Viewport' : ws === 'probe' ? 'Line Probe Viewport' : 'CFD Viewport'}
        patches={defaultPatches}
        rules={rules}
        gizmos={sharedGizmosForBC}
        selectedPatchId={selectedPatchId}
        onPickPatch={handlePickPatch}
        lineProbe={lineProbeFor3D}
      />
    );
  })();

  return (
    <div className="flex h-screen w-screen text-zinc-200" style={{ fontFamily: "'Inter', system-ui, sans-serif", background: '#0b0c10' }}>
      <div className="w-44 flex-shrink-0">{sidebar}</div>
      <div className="flex-1 flex flex-col min-w-0">
        {/* Top header */}
        <div className="h-10 flex items-center px-3 border-b border-zinc-800 bg-[#0b0c10] gap-3 flex-shrink-0">
          <div className="text-sm font-semibold tracking-tight">
            CFD Cockpit <span className="text-zinc-500 font-normal">/</span>{' '}
            <span className="text-[#004AFD]">{WORKSPACES.find((w) => w.id === ws)?.label}</span>
          </div>
          <div className="ml-auto flex items-center gap-2 text-[10.5px] font-mono">
            <span className="px-1.5 py-0.5 rounded bg-emerald-500/10 text-emerald-400 border border-emerald-700/40">GPU: wgpu 0.20</span>
            <span className="px-1.5 py-0.5 rounded bg-sky-500/10 text-sky-400 border border-sky-700/40">egui 0.28 · egui_plot</span>
            <span className="px-1.5 py-0.5 rounded bg-violet-500/10 text-violet-400 border border-violet-700/40">rhai 1.19</span>
            <span className="px-1.5 py-0.5 rounded bg-amber-500/10 text-amber-400 border border-amber-700/40">120 FPS</span>
          </div>
        </div>
        <div className="flex-1 flex min-h-0">
          <div className="flex-1 flex flex-col min-w-0">
            <div className="flex-1 relative min-h-0">
              {viewportArea}
            </div>
            {bottomPlots}
          </div>
          <div className="w-[360px] flex-shrink-0 border-l border-zinc-800 bg-[#0f1014]">
            {rightPanel}
          </div>
        </div>
      </div>
    </div>
  );
};

// ---------- Small helper components ----------
const Row: React.FC<{ k: string; v: string }> = ({ k, v }) => (
  <div className="flex justify-between">
    <span className="text-zinc-500">{k}</span>
    <span className="text-zinc-200">{v}</span>
  </div>
);

const KPI: React.FC<{ label: string; val: number; unit: string; goodIf: 'positive' | 'negative' }> = ({ label, val, unit, goodIf }) => {
  const good = goodIf === 'positive' ? val > 0 : val < 0;
  const color = good ? 'text-emerald-300 border-emerald-700/40 bg-emerald-500/5' : 'text-red-300 border-red-700/40 bg-red-500/10';
  return (
    <div className={`p-2 rounded border ${color}`}>
      <div className="text-[10px] text-zinc-400 font-sans">{label}</div>
      <div className="text-[14px] font-bold">{val >= 0 ? '+' : ''}{val.toFixed(2)}</div>
      <div className="text-[9.5px] text-zinc-500">{unit}</div>
    </div>
  );
};

const ParetoChart: React.FC = () => {
  // Simple inline SVG Pareto front
  const points = [
    { cd: 284, p0: 4.72 },
    { cd: 278, p0: 4.45 },
    { cd: 270, p0: 4.30 },
    { cd: 262, p0: 4.10 },
    { cd: 255, p0: 3.95 },
    { cd: 251, p0: 3.88 },
  ];
  const cdMin = 245, cdMax = 290;
  const p0Min = 3.6, p0Max = 4.9;
  const W = 280, H = 160, pl = 40, pb = 28, pr = 8, pt = 8;
  const toX = (cd: number) => pl + ((cd - cdMin) / (cdMax - cdMin)) * (W - pl - pr);
  const toY = (p0: number) => H - pb - ((p0 - p0Min) / (p0Max - p0Min)) * (H - pb - pt);
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="w-full">
      <rect x={0} y={0} width={W} height={H} fill="#0b0c10" />
      <rect x={pl} y={pt} width={W - pl - pr} height={H - pb - pt} fill="#18181C" stroke="#2A2D36" />
      {[250, 260, 270, 280].map((cd) => (
        <g key={cd}>
          <line x1={toX(cd)} x2={toX(cd)} y1={pt} y2={H - pb} stroke="#272932" />
          <text x={toX(cd)} y={H - pb + 10} fontSize={8} fill="#9CA3AF" textAnchor="middle" fontFamily="monospace">{cd}</text>
        </g>
      ))}
      {[3.8, 4.2, 4.6].map((p0) => (
        <g key={p0}>
          <line x1={pl} x2={W - pr} y1={toY(p0)} y2={toY(p0)} stroke="#272932" />
          <text x={pl - 4} y={toY(p0) + 2} fontSize={8} fill="#9CA3AF" textAnchor="end" fontFamily="monospace">{p0.toFixed(1)}</text>
        </g>
      ))}
      <polyline
        points={points.map((p) => `${toX(p.cd)},${toY(p.p0)}`).join(' ')}
        fill="none" stroke="#004AFD" strokeWidth={1.5}
      />
      {points.map((p, i) => (
        <circle key={i} cx={toX(p.cd)} cy={toY(p.p0)} r={i === 0 ? 3.5 : 2.5}
          fill={i === 0 ? '#60A5FA' : i === points.length - 1 ? '#F59E0B' : '#004AFD'}
          stroke="#0b0c10" strokeWidth={1}
        />
      ))}
      <text x={W / 2} y={H - 2} fontSize={9} fill="#9CA3AF" textAnchor="middle" fontFamily="Inter">Cd × 10⁴ (↓)</text>
      <text x={10} y={pt + 8} fontSize={9} fill="#9CA3AF" fontFamily="Inter">p₀ loss %</text>
    </svg>
  );
};
