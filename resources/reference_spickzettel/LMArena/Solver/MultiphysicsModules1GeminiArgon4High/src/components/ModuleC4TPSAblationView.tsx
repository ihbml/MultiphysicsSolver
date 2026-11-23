import React, { useState } from 'react';
import {
  Shield,
  Flame,
  Code2,
  Sparkles,
} from 'lucide-react';
import {
  TPSAblationALESimulation,
  tpsCharVirginRGB,
  infernoRGB,
  rgbToCss,
  clamp,
} from '../solvers/clusterCKernels';
import { MultiTraceChart } from './TelemetryCharts';

interface ModuleC4TPSAblationViewProps {
  sim: TPSAblationALESimulation;
  tickCount: number;
  onForceUpdate: () => void;
  onOpenCppSection: () => void;
}

export const ModuleC4TPSAblationView: React.FC<ModuleC4TPSAblationViewProps> = ({
  sim,
  onForceUpdate,
  onOpenCppSection,
}) => {
  const [colorBy, setColorBy] = useState<'char' | 'temp'>('char');

  const yCentersMm: number[] = [];
  const tempProfile: number[] = [];
  const rhoBulkProfile: number[] = [];
  const rhoResinAProfile: number[] = [];
  const rhoResinBProfile: number[] = [];

  for (let c = 0; c < sim.nCells; c++) {
    const yc = 0.5 * (sim.y_node[c] + sim.y_node[c + 1]) * 1000.0;
    yCentersMm.push(yc);
    tempProfile.push(sim.T[c]);
    rhoBulkProfile.push(sim.rho_bulk[c]);
    rhoResinAProfile.push(sim.rho_k0[c]);
    rhoResinBProfile.push(sim.rho_k1[c]);
  }

  const xTime = sim.history.map((h) => h.time_s);
  const recMm = sim.cumulativeRecession_m * 1000.0;

  return (
    <div className="grid grid-cols-1 gap-4 lg:grid-cols-12">
      {/* LEFT PARAMETER & SOLVER CONTROL DOCK (3 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-3">
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          <div className="mb-3 flex items-center justify-between border-b border-[#1E293B] pb-2.5">
            <div className="flex items-center gap-2">
              <Shield className="h-4 w-4 text-pink-400" />
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                TPS Entry & ALE Controls
              </h3>
            </div>
            <span className="rounded border border-pink-500/30 bg-pink-500/10 px-1.5 py-0.5 font-mono-tabular text-[10px] font-semibold text-pink-300">
              BDF2-DGCL
            </span>
          </div>

          {/* Atmospheric Entry Trajectory Presets */}
          <div className="mb-3.5">
            <label className="mb-1.5 block font-mono-tabular text-[10px] uppercase text-[#94A3B8]">
              Hypersonic Entry Trajectory Presets
            </label>
            <div className="grid grid-cols-1 gap-1.5">
              <button
                onClick={() => {
                  sim.q_aero_cold_MW_m2 = 3.6;
                  sim.p_stag_atm = 0.85;
                  sim.emissivity = 0.88;
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-pink-500/40 bg-pink-500/10 px-2.5 py-1.5 text-left text-xs font-medium text-pink-200 transition hover:bg-pink-500/20"
              >
                <span>Orion Lunar Return Peak</span>
                <span className="font-mono-tabular text-[10px] text-pink-400">3.6 MW/m²</span>
              </button>
              <button
                onClick={() => {
                  sim.q_aero_cold_MW_m2 = 6.8;
                  sim.p_stag_atm = 1.65;
                  sim.emissivity = 0.86;
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-amber-500/40 bg-amber-500/10 px-2.5 py-1.5 text-left text-xs font-medium text-amber-200 transition hover:bg-amber-500/20"
              >
                <span>Stardust Sample Return Extreme</span>
                <span className="font-mono-tabular text-[10px] text-amber-400">6.8 MW/m²</span>
              </button>
              <button
                onClick={() => {
                  sim.q_aero_cold_MW_m2 = 1.6;
                  sim.p_stag_atm = 0.25;
                  sim.emissivity = 0.90;
                  onForceUpdate();
                }}
                className="flex items-center justify-between rounded border border-[#1E293B] bg-[#131B28] px-2.5 py-1.5 text-left text-xs font-medium text-slate-300 transition hover:border-slate-600"
              >
                <span>Mars Entry Aeroshell (MSL)</span>
                <span className="font-mono-tabular text-[10px] text-cyan-400">1.6 MW/m²</span>
              </button>
            </div>
          </div>

          {/* Sliders */}
          <div className="space-y-3">
            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Cold-Wall Heat Flux q_aero</span>
                <span className="font-mono-tabular font-semibold text-pink-400">
                  {sim.q_aero_cold_MW_m2.toFixed(2)} MW/m²
                </span>
              </div>
              <input
                type="range"
                min={0.6}
                max={8.0}
                step={0.2}
                value={sim.q_aero_cold_MW_m2}
                onChange={(e) => {
                  sim.q_aero_cold_MW_m2 = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-pink-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Stagnation Pressure p_stag</span>
                <span className="font-mono-tabular font-semibold text-amber-400">
                  {sim.p_stag_atm.toFixed(2)} atm
                </span>
              </div>
              <input
                type="range"
                min={0.05}
                max={2.5}
                step={0.05}
                value={sim.p_stag_atm}
                onChange={(e) => {
                  sim.p_stag_atm = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-amber-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Char Surface Emissivity ε_w</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {sim.emissivity.toFixed(2)}
                </span>
              </div>
              <input
                type="range"
                min={0.65}
                max={0.96}
                step={0.01}
                value={sim.emissivity}
                onChange={(e) => {
                  sim.emissivity = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-pink-500"
              />
            </div>
          </div>

          {/* 3-Component Resin Matrix Summary */}
          <div className="mt-4 pt-3 border-t border-[#1E293B]">
            <div className="mb-1.5 font-mono-tabular text-[10px] uppercase text-[#94A3B8]">
              3-Component PICA / Phenolic Matrix (y = y_w)
            </div>
            <div className="grid grid-cols-3 gap-1.5 font-mono-tabular text-[10px]">
              <div className="rounded border border-[#1E293B] bg-[#070A0F] p-1.5">
                <div className="text-[#64748B]">Resin A (Γ₁)</div>
                <div className="font-semibold text-amber-300">
                  {sim.rho_k0[0].toFixed(1)} kg/m³
                </div>
              </div>
              <div className="rounded border border-[#1E293B] bg-[#070A0F] p-1.5">
                <div className="text-[#64748B]">Resin B (Γ₂)</div>
                <div className="font-semibold text-orange-300">
                  {sim.rho_k1[0].toFixed(1)} kg/m³
                </div>
              </div>
              <div className="rounded border border-[#1E293B] bg-[#070A0F] p-1.5">
                <div className="text-[#64748B]">Fiber (Γ₃)</div>
                <div className="font-semibold text-slate-200">
                  {sim.rho_k2[0].toFixed(1)} kg/m³
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Governing Equations Card */}
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3.5">
          <div className="mb-2 flex items-center justify-between">
            <span className="font-display text-[11px] font-bold tracking-wider uppercase text-pink-400">
              Pyrolysis & BDF2-DGCL ALE (C4)
            </span>
            <button
              onClick={onOpenCppSection}
              className="flex items-center gap-1 rounded border border-[#1E293B] bg-[#131B28] px-2 py-0.5 font-mono-tabular text-[10px] text-cyan-300 hover:border-cyan-500/40"
            >
              <Code2 className="h-3 w-3" />
              C++20 Lines 868–1054
            </button>
          </div>
          <div className="space-y-1.5 font-mono-tabular text-[11px] leading-relaxed text-slate-300">
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">Arrhenius Pyrolysis & Recession:</div>
              <div>∂ρ_s/∂t = -∑_k Γ_k (ρ_s,k - ρ_c,k)^n_k A_k exp(-E_k/RT)</div>
              <div>v_rec = (ṁ_char / ρ_wall) n_w</div>
            </div>
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">Surface Balance & BDF2-DGCL:</div>
              <div>q_aero·ψ_blow = εσ_SB(T_w⁴ - T_∞⁴) + ṁ_c Δh_abl + q_cond</div>
              <div>(3V_P^(n+1) - 4V_P^n + V_P^(n-1))/(2Δt) = ∑_f v_m,f·S_f</div>
            </div>
          </div>
        </div>
      </div>

      {/* CENTER PRIMARY 2D DEFORMING ALE POROUS TPS CROSS-SECTION CANVAS (5 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-5">
        <div className="flex flex-1 flex-col rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          <div className="mb-3 flex flex-wrap items-center justify-between gap-2">
            <div>
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                Deforming BDF2-DGCL ALE Heatshield Mesh (45 mm PICA)
              </h3>
              <p className="font-mono-tabular text-[11px] text-[#94A3B8]">
                Surface Recession Δs ={' '}
                <span className="text-pink-400 font-semibold">
                  {recMm.toFixed(2)} mm
                </span>{' '}
                (v_rec = {sim.v_rec_mm_s.toFixed(3)} mm/s)
              </p>
            </div>

            <div className="flex gap-1.5">
              <button
                onClick={() => setColorBy('char')}
                className={`rounded px-2.5 py-1 font-mono-tabular text-[11px] font-medium transition ${
                  colorBy === 'char'
                    ? 'border border-amber-500/50 bg-amber-500/15 text-amber-300'
                    : 'border border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                Char Fraction β_char
              </button>
              <button
                onClick={() => setColorBy('temp')}
                className={`rounded px-2.5 py-1 font-mono-tabular text-[11px] font-medium transition ${
                  colorBy === 'temp'
                    ? 'border border-pink-500/50 bg-pink-500/15 text-pink-300'
                    : 'border border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                Temperature T(y) [K]
              </button>
            </div>
          </div>

          {/* 2D Deforming ALE TPS Cross-Section SVG */}
          <div className="relative flex flex-1 items-center justify-center rounded-lg border border-[#1E293B] bg-[#070A0F] p-3">
            <svg viewBox="0 0 460 365" className="h-full max-h-[380px] w-full select-none">
              <defs>
                <linearGradient id="plasmaShockGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#EC4899" stopOpacity="0.65" />
                  <stop offset="60%" stopColor="#F97316" stopOpacity="0.28" />
                  <stop offset="100%" stopColor="#070A0F" stopOpacity="0.0" />
                </linearGradient>
              </defs>

              {/* Hypersonic Plasma Shock Layer Region */}
              <rect x={45} y={10} width={370} height={58} fill="url(#plasmaShockGrad)" rx={4} />
              <text
                x={230}
                y={28}
                textAnchor="middle"
                className="fill-pink-200 font-mono-tabular text-[10px] font-bold"
              >
                HYPERSONIC SHOCK LAYER • q_aero,net = {(sim.q_aero_net_W_m2 * 1e-6).toFixed(2)} MW/m²
              </text>
              <text
                x={230}
                y={43}
                textAnchor="middle"
                className="fill-amber-300 font-mono-tabular text-[9px]"
              >
                ⇈ Reradiation εσ_SB T_w⁴ = {(sim.q_rerad_W_m2 * 1e-6).toFixed(2)} MW/m² | Pyrolysis Blowing ṁ_g = {(sim.m_dot_pyro_kg_m2s * 1000).toFixed(1)} g/(m²·s) ⇈
              </text>

              {/* Original Virgin Surface Reference Line (y = 0.0 mm) */}
              <line
                x1={45}
                y1={72}
                x2={415}
                y2={72}
                stroke="#F43F5E"
                strokeDasharray="4 4"
                strokeWidth={1.3}
              />
              <text
                x={418}
                y={75}
                className="fill-rose-400 font-mono-tabular text-[9px]"
              >
                y₀=0
              </text>

              {/* Deforming BDF2-DGCL ALE Layers */}
              {Array.from({ length: sim.nCells }).map((_, c) => {
                const yTopMm = sim.y_node[c] * 1000.0;
                const yBotMm = sim.y_node[c + 1] * 1000.0;
                const py0 = 72 + (yTopMm / 45.0) * 265;
                const py1 = 72 + (yBotMm / 45.0) * 265;

                const rgb =
                  colorBy === 'char'
                    ? tpsCharVirginRGB(sim.beta_char[c])
                    : infernoRGB(clamp((sim.T[c] - 300.0) / 2300.0, 0, 1));

                return (
                  <g key={c}>
                    <rect
                      x={60}
                      y={py0}
                      width={330}
                      height={Math.max(1.2, py1 - py0)}
                      fill={rgbToCss(rgb, 0.92)}
                      stroke="rgba(15, 23, 42, 0.55)"
                      strokeWidth={0.65}
                    />
                    {/* Internal ALE vertical mesh lines */}
                    {c % 4 === 0 && (
                      <line
                        x1={60}
                        y1={py0}
                        x2={390}
                        y2={py0}
                        stroke="rgba(248, 250, 252, 0.18)"
                        strokeWidth={0.7}
                      />
                    )}
                  </g>
                );
              })}

              {/* Vertical ALE mesh columns */}
              {[115, 170, 225, 280, 335].map((vx) => {
                const pySurf = 72 + (recMm / 45.0) * 265;
                return (
                  <line
                    key={vx}
                    x1={vx}
                    y1={pySurf}
                    x2={vx}
                    y2={337}
                    stroke="rgba(248, 250, 252, 0.16)"
                    strokeWidth={0.8}
                  />
                );
              })}

              {/* Pyrolysis Gas Percolation Streamlines (Upward through porous char) */}
              {[95, 150, 205, 260, 315, 365].map((gx, idx) => {
                const pySurf = 72 + (recMm / 45.0) * 265;
                const offset = ((sim.time_s * 35 + idx * 14) % 70);
                const pyStart = Math.min(325, pySurf + 25 + offset);
                return (
                  <g key={gx}>
                    <line
                      x1={gx}
                      y1={pyStart}
                      x2={gx}
                      y2={pySurf - 12}
                      stroke="#FDE047"
                      strokeOpacity={0.65}
                      strokeDasharray="3 3"
                      strokeWidth={1.3}
                    />
                    <polygon
                      points={`${gx},${pySurf - 16} ${gx - 3.5},${pySurf - 8} ${gx + 3.5},${pySurf - 8}`}
                      fill="#FDE047"
                    />
                  </g>
                );
              })}

              {/* Depth Ruler Marks on Left */}
              {[0, 15, 30, 45].map((mm) => {
                const py = 72 + (mm / 45.0) * 265;
                return (
                  <g key={mm}>
                    <line x1={52} y1={py} x2={60} y2={py} stroke="#94A3B8" />
                    <text
                      x={48}
                      y={py + 3}
                      textAnchor="end"
                      className="fill-[#94A3B8] font-mono-tabular text-[9px]"
                    >
                      {mm}mm
                    </text>
                  </g>
                );
              })}

              {/* Structural Aluminum/Composite Backwall */}
              <rect x={60} y={337} width={330} height={14} fill="#334155" />
              <text
                x={225}
                y={347}
                textAnchor="middle"
                className="fill-slate-200 font-mono-tabular text-[9px] font-semibold"
              >
                TPS BACKWALL BONDLINE (y = 45.0 mm, T_back ={' '}
                {sim.T[sim.nCells - 1].toFixed(1)} K)
              </text>
            </svg>
          </div>

          {/* Colorbar Legend */}
          <div className="mt-3 flex items-center gap-3 pt-2 border-t border-[#1E293B]">
            <span className="font-mono-tabular text-[10px] text-[#94A3B8]">
              {colorBy === 'char' ? 'TPS State:' : 'Inferno Temp:'}
            </span>
            <div
              className="h-2.5 flex-1 rounded-full border border-[#1E293B]"
              style={{
                background:
                  colorBy === 'char'
                    ? 'linear-gradient(90deg, rgb(24,24,27) 0%, rgb(154,52,18) 50%, rgb(253,224,71) 100%)'
                    : 'linear-gradient(90deg, rgb(7,10,20) 0%, rgb(187,55,84) 50%, rgb(252,255,164) 100%)',
              }}
            />
            <span className="font-mono-tabular text-[10px] text-amber-300">
              {colorBy === 'char' ? 'Char (1.0) ➔ Virgin (0.0)' : '300 K ➔ 2600 K'}
            </span>
          </div>
        </div>
      </div>

      {/* RIGHT SYNCHRONIZED MULTI-TRACE TELEMETRY STACK (4 COLS) */}
      <div className="flex flex-col gap-3 lg:col-span-4">
        {/* KPI Row */}
        <div className="grid grid-cols-2 gap-2.5">
          <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
            <div className="flex items-center justify-between text-[11px] text-[#94A3B8]">
              <span>Ablating Wall Temp T_w</span>
              <Flame className="h-3.5 w-3.5 text-pink-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-pink-400">
              {sim.T_wall_K.toFixed(0)} K{' '}
              <span className="text-[10px] font-normal text-[#94A3B8]">
                (Δs={recMm.toFixed(2)} mm)
              </span>
            </div>
          </div>

          <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
            <div className="flex items-center justify-between text-[11px] text-[#94A3B8]">
              <span>BDF2-DGCL Residual</span>
              <Sparkles className="h-3.5 w-3.5 text-amber-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-emerald-400">
              {sim.maxDgclResidual.toExponential(2)}{' '}
              <span className="text-[10px] font-normal text-[#94A3B8]">
                m³/s
              </span>
            </div>
          </div>
        </div>

        {/* Chart 1: In-Depth Density & Pyrolysis Sub-Component Profiles */}
        <MultiTraceChart
          title="In-Depth Porous Solid Density & Resin Pyrolysis ρ_s(y)"
          subtitle="Virgin-to-char Arrhenius decomposition across receding ALE coordinates"
          xValues={yCentersMm}
          xUnit="mm"
          yUnit="kg/m³"
          yMinOverride={0}
          yMaxOverride={350}
          rightBadge={`y_w = ${recMm.toFixed(2)} mm`}
          series={[
            {
              label: 'ρ_bulk',
              color: '#FDE047',
              data: rhoBulkProfile,
              unit: 'kg/m³',
            },
            {
              label: 'Resin A (ρ_1)',
              color: '#F97316',
              data: rhoResinAProfile,
              dashed: true,
            },
            {
              label: 'Resin B (ρ_2)',
              color: '#EC4899',
              data: rhoResinBProfile,
              dashed: true,
            },
          ]}
        />

        {/* Chart 2: Surface Energy Balance Breakdown Over Time */}
        <MultiTraceChart
          title="Surface Energy Balance (Newton-Raphson Equilibrium)"
          subtitle="q_aero,net = εσ_SB(T_w⁴ - T_∞⁴) + q_cond + ṁ_char Δh_abl"
          xValues={xTime}
          xUnit="s"
          yUnit="MW/m²"
          series={[
            {
              label: 'q_aero,net',
              color: '#F43F5E',
              data: sim.history.map((h) => h.q_aero_net_MW_m2),
              unit: 'MW/m²',
            },
            {
              label: 'Reradiation',
              color: '#EC4899',
              data: sim.history.map((h) => h.q_rerad_MW_m2),
              unit: 'MW/m²',
            },
            {
              label: 'Conduction',
              color: '#F59E0B',
              data: sim.history.map((h) => h.q_cond_MW_m2),
              unit: 'MW/m²',
            },
          ]}
        />

        {/* Chart 3: Cumulative ALE Surface Recession & Pyrolysis Blowing */}
        <MultiTraceChart
          title="ALE Surface Recession Δs(t) & Pyrolysis Gas Blowing ṁ_g"
          subtitle="Recession velocity v_rec = ṁ_char / ρ_wall & gas percolation flux"
          xValues={xTime}
          xUnit="s"
          series={[
            {
              label: 'Recession Δs [mm]',
              color: '#06B6D4',
              data: sim.history.map((h) => h.recession_mm),
              unit: 'mm',
            },
            {
              label: 'Pyrolysis ṁ_g [g/m²s]',
              color: '#F59E0B',
              data: sim.history.map((h) => h.m_dot_pyro_g_m2s),
              dashed: true,
              unit: 'g/m²s',
            },
          ]}
        />
      </div>
    </div>
  );
};
