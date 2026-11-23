import React, { useState } from 'react';
import {
  Flame,
  Zap,
  Thermometer,
  Code2,
  AlertTriangle,
  Crosshair,
  ShieldAlert,
} from 'lucide-react';
import {
  BatteryPackSimulation,
  clamp,
  infernoRGB,
  rgbToCss,
} from '../solvers/clusterCKernels';
import { MultiTraceChart } from './TelemetryCharts';

interface ModuleC1BatteryViewProps {
  sim: BatteryPackSimulation;
  tickCount: number;
  onForceUpdate: () => void;
  onOpenCppSection: () => void;
}

type FieldMode = 'T' | 'q_abuse' | 'alpha_pe' | 'V_term';

export const ModuleC1BatteryView: React.FC<ModuleC1BatteryViewProps> = ({
  sim,
  onForceUpdate,
  onOpenCppSection,
}) => {
  const [fieldMode, setFieldMode] = useState<FieldMode>('T');
  const [clickAction, setClickAction] = useState<'inspect' | 'puncture'>('inspect');

  const sel = sim.selectedCell;
  let tMax = 0;
  let runawayCount = 0;
  for (let i = 0; i < sim.nCells; i++) {
    if (sim.T[i] > tMax) tMax = sim.T[i];
    if (sim.T[i] > 450.0 || sim.alpha_pe[i] > 0.25) runawayCount++;
  }

  const xTime = sim.history.map((h) => h.time_s);

  const handleCellClick = (idx: number) => {
    sim.selectedCell = idx;
    if (clickAction === 'puncture') {
      sim.triggerCellAbuse(idx, 485.0);
    }
    onForceUpdate();
  };

  return (
    <div className="grid grid-cols-1 gap-4 lg:grid-cols-12">
      {/* LEFT PARAMETER & SOLVER CONTROL DOCK (3 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-3">
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          <div className="mb-3 flex items-center justify-between border-b border-[#1E293B] pb-2.5">
            <div className="flex items-center gap-2">
              <Flame className="h-4 w-4 text-orange-500" />
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                2-RC ECM & Abuse Controls
              </h3>
            </div>
            <span className="rounded border border-orange-500/30 bg-orange-500/10 px-1.5 py-0.5 font-mono-tabular text-[10px] font-semibold text-orange-400">
              ROSENBROCK-W
            </span>
          </div>

          {/* Preset Scenario Buttons */}
          <div className="mb-3.5">
            <label className="mb-1.5 block font-mono-tabular text-[10px] uppercase text-[#94A3B8]">
              Abuse & Cooling Scenarios
            </label>
            <div className="grid grid-cols-1 gap-1.5">
              <button
                onClick={() => {
                  sim.I_pack_A = 165.0;
                  sim.h_coolant = 22.0;
                  sim.interCellPadK = 7.5;
                  sim.reset(true);
                  sim.triggerCellAbuse(sim.selectedCell, 485.0);
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-orange-500/40 bg-orange-500/10 px-2.5 py-1.5 text-left text-xs font-medium text-orange-200 transition hover:bg-orange-500/20"
              >
                <span>Nail Penetration Propagation</span>
                <span className="font-mono-tabular text-[10px] text-orange-400">T₀=485K</span>
              </button>
              <button
                onClick={() => {
                  sim.I_pack_A = 220.0;
                  sim.h_coolant = 4.0;
                  sim.interCellPadK = 14.0;
                  sim.reset(true);
                  sim.triggerCellAbuse(9, 495.0);
                  sim.triggerCellAbuse(10, 480.0);
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-red-500/30 bg-red-500/10 px-2.5 py-1.5 text-left text-xs font-medium text-red-200 transition hover:bg-red-500/20"
              >
                <span>Coolant Pump Failure Cascade</span>
                <span className="font-mono-tabular text-[10px] text-red-400">h=4 W/m²K</span>
              </button>
              <button
                onClick={() => {
                  sim.I_pack_A = 125.0;
                  sim.h_coolant = 160.0;
                  sim.interCellPadK = 2.5;
                  sim.reset(false);
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-[#1E293B] bg-[#131B28] px-2.5 py-1.5 text-left text-xs font-medium text-slate-300 transition hover:border-slate-600"
              >
                <span>Nominal 2.5C Liquid-Cooled Pack</span>
                <span className="font-mono-tabular text-[10px] text-emerald-400">No Abuse</span>
              </button>
            </div>
          </div>

          {/* Sliders */}
          <div className="space-y-3">
            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Pack Current I (50Ah Cell)</span>
                <span className="font-mono-tabular font-semibold text-orange-400">
                  {sim.I_pack_A.toFixed(0)} A ({(sim.I_pack_A / 50).toFixed(1)}C)
                </span>
              </div>
              <input
                type="range"
                min={0}
                max={300}
                step={5}
                value={sim.I_pack_A}
                onChange={(e) => {
                  sim.I_pack_A = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-orange-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Cold-Plate h_cool</span>
                <span className="font-mono-tabular font-semibold text-cyan-400">
                  {sim.h_coolant.toFixed(0)} W/(m²·K)
                </span>
              </div>
              <input
                type="range"
                min={2}
                max={350}
                step={2}
                value={sim.h_coolant}
                onChange={(e) => {
                  sim.h_coolant = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-cyan-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Inter-Cell Thermal Pad k</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {sim.interCellPadK.toFixed(1)} W/(m·K)
                </span>
              </div>
              <input
                type="range"
                min={0.5}
                max={20}
                step={0.5}
                value={sim.interCellPadK}
                onChange={(e) => {
                  sim.interCellPadK = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-orange-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Coolant Inlet Temp T_cool</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {sim.T_coolant_K.toFixed(1)} K
                </span>
              </div>
              <input
                type="range"
                min={280}
                max={330}
                step={1}
                value={sim.T_coolant_K}
                onChange={(e) => {
                  sim.T_coolant_K = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-cyan-500"
              />
            </div>
          </div>

          {/* Direct Abuse Injection Button */}
          <div className="mt-4 pt-3 border-t border-[#1E293B]">
            <button
              onClick={() => {
                sim.triggerCellAbuse(sim.selectedCell, 490.0);
                onForceUpdate();
              }}
              className="flex w-full items-center justify-center gap-2 rounded border border-red-500/50 bg-red-500/15 px-3 py-2 text-xs font-semibold text-red-300 shadow-sm transition hover:bg-red-500/25"
            >
              <ShieldAlert className="h-4 w-4 text-red-400" />
              Trigger Nail Penetration on Cell #{sel + 1}
            </button>
          </div>
        </div>

        {/* Mathematical Formulation Card */}
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3.5">
          <div className="mb-2 flex items-center justify-between">
            <span className="font-display text-[11px] font-bold tracking-wider uppercase text-orange-400">
              Governing Equations (Module C1)
            </span>
            <button
              onClick={onOpenCppSection}
              className="flex items-center gap-1 rounded border border-[#1E293B] bg-[#131B28] px-2 py-0.5 font-mono-tabular text-[10px] text-cyan-300 hover:border-cyan-500/40"
            >
              <Code2 className="h-3 w-3" />
              C++20 Lines 220–444
            </button>
          </div>
          <div className="space-y-1.5 font-mono-tabular text-[11px] leading-relaxed text-slate-300">
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">2-RC Thevenin ECM & Entropic Heat:</div>
              <div>d(SOC)/dt = -I / (3600·Q_nom)</div>
              <div>dV_k/dt = -V_k/(R_k C_k) + I/C_k, k∈{'{1,2}'}</div>
              <div>q̇_rev = I·T·(∂U_OCV/∂T), q̇_irr = |I(V_term - U_OCV)|</div>
            </div>
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">4-Stage Hatchard/Spotnitz Kinetics:</div>
              <div>R_SEI = A_SEI·exp(-E_SEI/RT)·c_SEI^m</div>
              <div>R_ne = A_ne·exp(-E_ne/RT)·exp(-t_SEI/t_0)·c_ne</div>
              <div>R_pe = A_pe·α_pe^m1(1-α_pe)^m2·exp(-E_pe/RT)</div>
              <div>R_e = A_e·exp(-E_e/RT)·c_e^m</div>
            </div>
          </div>
        </div>
      </div>

      {/* CENTER PRIMARY 2D/3D FVM PACK TELEMETRY CANVAS (5 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-5">
        <div className="flex flex-1 flex-col rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          {/* Canvas Top Toolbar */}
          <div className="mb-3 flex flex-wrap items-center justify-between gap-2">
            <div>
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                8×4 Prismatic Pack FVM Mesh (SoA CAbiSpan&lt;double&gt;)
              </h3>
              <p className="font-mono-tabular text-[11px] text-[#94A3B8]">
                Click cell to{' '}
                <span className="text-orange-300 font-semibold">
                  {clickAction === 'inspect' ? 'Inspect Telemetry' : 'Inject Nail Puncture'}
                </span>{' '}
                • Active Cell: #{sel + 1} (R{Math.floor(sel / 8) + 1}:C{(sel % 8) + 1})
              </p>
            </div>

            <div className="flex items-center gap-1.5">
              <button
                onClick={() => setClickAction('inspect')}
                className={`flex items-center gap-1 rounded border px-2 py-1 text-[11px] font-medium transition ${
                  clickAction === 'inspect'
                    ? 'border-cyan-500/50 bg-cyan-500/15 text-cyan-300'
                    : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                <Crosshair className="h-3 w-3" />
                Inspect
              </button>
              <button
                onClick={() => setClickAction('puncture')}
                className={`flex items-center gap-1 rounded border px-2 py-1 text-[11px] font-medium transition ${
                  clickAction === 'puncture'
                    ? 'border-red-500/50 bg-red-500/20 text-red-300'
                    : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                <Flame className="h-3 w-3" />
                Puncture
              </button>
            </div>
          </div>

          {/* Field Selector Pills */}
          <div className="mb-3 flex flex-wrap gap-1.5">
            {(
              [
                { id: 'T', label: 'Temp T [K] (Inferno)' },
                { id: 'q_abuse', label: 'Abuse Heat q̇_TR [MW/m³]' },
                { id: 'alpha_pe', label: 'Cathode Conv. α_pe' },
                { id: 'V_term', label: 'Terminal V_term [V]' },
              ] as const
            ).map((tab) => (
              <button
                key={tab.id}
                onClick={() => setFieldMode(tab.id)}
                className={`rounded px-2.5 py-1 font-mono-tabular text-[11px] font-medium transition ${
                  fieldMode === tab.id
                    ? 'border border-orange-500/50 bg-orange-500/15 text-orange-300'
                    : 'border border-[#1E293B] bg-[#131B28] text-[#94A3B8] hover:text-slate-200'
                }`}
              >
                {tab.label}
              </button>
            ))}
          </div>

          {/* 8x4 Interactive Battery Pack SVG + Liquid Cooling Manifold */}
          <div className="relative flex-1 rounded-lg border border-[#1E293B] bg-[#070A0F] p-3">
            {/* Top Coolant Manifold Ribbon */}
            <div className="mb-2 flex items-center justify-between rounded border border-cyan-500/20 bg-cyan-950/25 px-3 py-1 font-mono-tabular text-[10px] text-cyan-300">
              <span>LIQUID COLD-PLATE INLET ({sim.T_coolant_K.toFixed(1)} K)</span>
              <span>h_eff = {sim.h_coolant.toFixed(0)} W/(m²·K) ➔➔➔</span>
            </div>

            <div className="grid grid-cols-8 gap-2 my-2">
              {Array.from({ length: sim.nCells }).map((_, idx) => {
                const T = sim.T[idx];
                const qTrMW = sim.q_abuse[idx] * 1e-6;
                const alpha = sim.alpha_pe[idx];
                const vTerm = sim.V_term[idx];
                const isSelected = idx === sel;
                const isShorted = sim.shortTriggered[idx] === 1;

                let normVal = 0;
                let primaryText = '';
                let subText = '';

                if (fieldMode === 'T') {
                  normVal = clamp((T - 295.0) / 550.0, 0, 1);
                  primaryText = `${T.toFixed(0)}K`;
                  subText = `${vTerm.toFixed(2)}V`;
                } else if (fieldMode === 'q_abuse') {
                  normVal = clamp(Math.log10(Math.max(1, sim.q_abuse[idx])) / 8.5, 0, 1);
                  primaryText = `${qTrMW.toFixed(1)}`;
                  subText = `MW/m³`;
                } else if (fieldMode === 'alpha_pe') {
                  normVal = clamp(alpha, 0, 1);
                  primaryText = `${(alpha * 100).toFixed(0)}%`;
                  subText = `c_SEI:${sim.c_SEI[idx].toFixed(2)}`;
                } else {
                  normVal = clamp((vTerm - 2.5) / 1.7, 0, 1);
                  primaryText = `${vTerm.toFixed(2)}V`;
                  subText = `${(sim.SOC[idx] * 100).toFixed(0)}%`;
                }

                const rgb = infernoRGB(normVal);
                const bgCss = rgbToCss(rgb, 0.86);
                const textDark = normVal > 0.62;

                return (
                  <button
                    key={idx}
                    onClick={() => handleCellClick(idx)}
                    style={{ backgroundColor: bgCss }}
                    className={`group relative flex h-20 flex-col items-center justify-between rounded-md border p-1.5 font-mono-tabular transition-all ${
                      isSelected
                        ? 'ring-2 ring-cyan-400 border-white scale-[1.03] z-10'
                        : 'border-[#1E293B] hover:border-slate-400'
                    }`}
                  >
                    <div className="flex w-full items-center justify-between text-[9px]">
                      <span
                        className={`font-bold ${
                          textDark ? 'text-slate-950' : 'text-slate-300'
                        }`}
                      >
                        C{idx + 1}
                      </span>
                      {isShorted && (
                        <AlertTriangle
                          className={`h-3 w-3 ${
                            textDark ? 'text-red-900' : 'text-red-300'
                          }`}
                        />
                      )}
                    </div>

                    <div
                      className={`text-xs font-bold tracking-tight ${
                        textDark ? 'text-slate-950' : 'text-white'
                      }`}
                    >
                      {primaryText}
                    </div>

                    <div
                      className={`text-[9px] ${
                        textDark ? 'text-slate-900/90' : 'text-slate-300/90'
                      }`}
                    >
                      {subText}
                    </div>
                  </button>
                );
              })}
            </div>

            {/* Bottom Coolant Return Manifold */}
            <div className="mt-2 flex items-center justify-between rounded border border-cyan-500/20 bg-cyan-950/25 px-3 py-1 font-mono-tabular text-[10px] text-cyan-300">
              <span>⇽⇽⇽ SERPENTINE COOLANT RETURN MANIFOLD</span>
              <span>RUNAWAY CELLS: {runawayCount} / 32</span>
            </div>

            {/* Inferno Colorbar Legend */}
            <div className="mt-3 flex items-center gap-3 pt-2 border-t border-[#1E293B]">
              <span className="font-mono-tabular text-[10px] text-[#94A3B8]">
                Inferno Scale:
              </span>
              <div
                className="h-2.5 flex-1 rounded-full border border-[#1E293B]"
                style={{
                  background:
                    'linear-gradient(90deg, rgb(7,10,20) 0%, rgb(120,28,109) 33%, rgb(237,105,37) 68%, rgb(252,255,164) 100%)',
                }}
              />
              <span className="font-mono-tabular text-[10px] text-amber-200">
                295 K ➔ 850+ K
              </span>
            </div>
          </div>

          {/* Selected Cell 2-RC + 4-Stage Kinetics Live Readout Strip */}
          <div className="mt-3 grid grid-cols-4 gap-2 rounded-lg border border-[#1E293B] bg-[#070A0F] p-2.5 font-mono-tabular text-xs">
            <div>
              <div className="text-[10px] text-[#64748B]">c_SEI (Stage 1)</div>
              <div className="font-semibold text-amber-400">
                {sim.c_SEI[sel].toFixed(4)}
              </div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">c_ne (Stage 2)</div>
              <div className="font-semibold text-orange-400">
                {sim.c_ne[sel].toFixed(4)}
              </div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">α_pe (Stage 3)</div>
              <div className="font-semibold text-red-400">
                {sim.alpha_pe[sel].toFixed(4)}
              </div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">c_e (Stage 4)</div>
              <div className="font-semibold text-purple-400">
                {sim.c_e[sel].toFixed(4)}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* RIGHT SYNCHRONIZED MULTI-TRACE TELEMETRY STACK (4 COLS) */}
      <div className="flex flex-col gap-3 lg:col-span-4">
        {/* 4 KPI Cards */}
        <div className="grid grid-cols-2 gap-2.5">
          <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
            <div className="flex items-center justify-between text-[11px] text-[#94A3B8]">
              <span>Peak Pack Temp T_max</span>
              <Thermometer className="h-3.5 w-3.5 text-orange-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-orange-400">
              {tMax.toFixed(1)} K{' '}
              <span className="text-xs font-normal text-[#94A3B8]">
                ({(tMax - 273.15).toFixed(1)} °C)
              </span>
            </div>
          </div>

          <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
            <div className="flex items-center justify-between text-[11px] text-[#94A3B8]">
              <span>Cell #{sel + 1} V_term / SOC</span>
              <Zap className="h-3.5 w-3.5 text-cyan-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-cyan-300">
              {sim.V_term[sel].toFixed(3)} V{' '}
              <span className="text-xs font-normal text-[#94A3B8]">
                ({(sim.SOC[sel] * 100).toFixed(1)}%)
              </span>
            </div>
          </div>
        </div>

        {/* Chart 1: Thermal Runaway Temperature Trajectory */}
        <MultiTraceChart
          title="Thermal Runaway Temperature Evolution"
          subtitle="Implicit Rosenbrock-W + Patankar stiff thermal integration"
          xValues={xTime}
          xUnit="s"
          yUnit="K"
          rightBadge={`Cell #${sel + 1}`}
          series={[
            {
              label: 'T_max (Pack)',
              color: '#EF4444',
              data: sim.history.map((h) => h.T_max_K),
              unit: 'K',
            },
            {
              label: `T_cell (#${sel + 1})`,
              color: '#F97316',
              data: sim.history.map((h) => h.T_sel_K),
              unit: 'K',
            },
          ]}
        />

        {/* Chart 2: 4-Stage Arrhenius Reactant Depletion */}
        <MultiTraceChart
          title="4-Stage Arrhenius Abuse Kinetics (Hatchard/Spotnitz)"
          subtitle="Positivity-preserving Patankar source-sink diagonalization"
          xValues={xTime}
          xUnit="s"
          yMinOverride={0}
          yMaxOverride={1.05}
          series={[
            {
              label: 'c_SEI',
              color: '#FBBF24',
              data: sim.history.map((h) => h.c_SEI_sel),
            },
            {
              label: 'c_ne',
              color: '#F97316',
              data: sim.history.map((h) => h.c_ne_sel),
            },
            {
              label: 'α_pe',
              color: '#EF4444',
              data: sim.history.map((h) => h.alpha_pe_sel),
            },
            {
              label: 'c_e',
              color: '#A855F7',
              data: sim.history.map((h) => h.c_e_sel),
            },
          ]}
        />

        {/* Chart 3: 2-RC ECM Voltages */}
        <MultiTraceChart
          title="2-RC Thevenin ECM Terminal & OCV Voltage"
          subtitle="Coupled U_OCV(SOC,T) - I·R_0 - V_1 - V_2"
          xValues={xTime}
          xUnit="s"
          yUnit="V"
          series={[
            {
              label: 'U_OCV',
              color: '#38BDF8',
              data: sim.history.map((h) => h.U_ocv_sel),
              dashed: true,
              unit: 'V',
            },
            {
              label: 'V_term',
              color: '#10B981',
              data: sim.history.map((h) => h.V_term_sel),
              unit: 'V',
            },
          ]}
        />
      </div>
    </div>
  );
};
