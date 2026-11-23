import React, { useMemo } from 'react';
import {
  Radio,
  Volume2,
  Activity,
  Code2,
} from 'lucide-react';
import {
  Farassat1AFWHSimulation,
  coolWarmRGB,
  rgbToCss,
  clamp,
} from '../solvers/clusterCKernels';
import { MultiTraceChart, ThirdOctaveBarChart } from './TelemetryCharts';

interface ModuleC2AeroacousticsViewProps {
  sim: Farassat1AFWHSimulation;
  tickCount: number;
  onForceUpdate: () => void;
  onOpenCppSection: () => void;
}

export const ModuleC2AeroacousticsView: React.FC<ModuleC2AeroacousticsViewProps> = ({
  sim,
  tickCount,
  onForceUpdate,
  onOpenCppSection,
}) => {
  const telemetry = useMemo(
    () => sim.computeTelemetry(),
    [
      sim,
      sim.tipMach,
      sim.bpfHz,
      sim.bladeCount,
      sim.loadingAmplitudePa,
      sim.permeableMode,
      sim.selectedMicIndex,
      tickCount,
    ]
  );

  const selectedMic =
    sim.microphones[sim.selectedMicIndex] ?? sim.microphones[0];

  // Build polar directivity SVG polygon points
  const cx = 210;
  const cy = 210;
  const maxR = 148;

  const polarPoints = telemetry.directivityPolar
    .map((pt) => {
      const rad = (pt.angleDeg * Math.PI) / 180;
      const normDb = clamp((pt.oaspl_dB - 95) / 55, 0.15, 1.0);
      const r = normDb * maxR;
      const x = cx + r * Math.cos(rad);
      const y = cy - r * Math.sin(rad);
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');

  return (
    <div className="grid grid-cols-1 gap-4 lg:grid-cols-12">
      {/* LEFT PARAMETER & SOLVER CONTROL DOCK (3 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-3">
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          <div className="mb-3 flex items-center justify-between border-b border-[#1E293B] pb-2.5">
            <div className="flex items-center gap-2">
              <Radio className="h-4 w-4 text-cyan-400" />
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                Farassat 1A FWH Controls
              </h3>
            </div>
            <span className="rounded border border-cyan-500/30 bg-cyan-500/10 px-1.5 py-0.5 font-mono-tabular text-[10px] font-semibold text-cyan-300">
              RETARDED-TIME
            </span>
          </div>

          {/* Surface Formulation Mode Toggle */}
          <div className="mb-3.5">
            <label className="mb-1.5 block font-mono-tabular text-[10px] uppercase text-[#94A3B8]">
              FVM Surface Integration Surface
            </label>
            <div className="grid grid-cols-2 gap-1.5">
              <button
                onClick={() => {
                  sim.permeableMode = true;
                  onForceUpdate();
                }}
                className={`rounded border px-2.5 py-1.5 font-mono-tabular text-[11px] font-medium transition ${
                  sim.permeableMode
                    ? 'border-cyan-500/50 bg-cyan-500/15 text-cyan-300'
                    : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                Permeable FWH
              </button>
              <button
                onClick={() => {
                  sim.permeableMode = false;
                  onForceUpdate();
                }}
                className={`rounded border px-2.5 py-1.5 font-mono-tabular text-[11px] font-medium transition ${
                  !sim.permeableMode
                    ? 'border-blue-500/50 bg-blue-500/15 text-blue-300'
                    : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8]'
                }`}
              >
                Solid Curle
              </button>
            </div>
          </div>

          {/* Sliders */}
          <div className="space-y-3">
            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Patch Tip Mach M_tip</span>
                <span className="font-mono-tabular font-semibold text-cyan-400">
                  M = {sim.tipMach.toFixed(2)}
                </span>
              </div>
              <input
                type="range"
                min={0.2}
                max={0.86}
                step={0.02}
                value={sim.tipMach}
                onChange={(e) => {
                  sim.tipMach = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-cyan-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Blade Passing Freq (BPF)</span>
                <span className="font-mono-tabular font-semibold text-blue-400">
                  {sim.bpfHz.toFixed(0)} Hz
                </span>
              </div>
              <input
                type="range"
                min={80}
                max={360}
                step={10}
                value={sim.bpfHz}
                onChange={(e) => {
                  sim.bpfHz = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-blue-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">Unsteady Loading Δp_f</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {sim.loadingAmplitudePa.toFixed(0)} Pa
                </span>
              </div>
              <input
                type="range"
                min={100}
                max={1400}
                step={20}
                value={sim.loadingAmplitudePa}
                onChange={(e) => {
                  sim.loadingAmplitudePa = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-cyan-500"
              />
            </div>

            <div>
              <div className="mb-1 flex justify-between text-xs">
                <span className="text-[#94A3B8]">FWH Rotor Blade Count N_b</span>
                <span className="font-mono-tabular font-semibold text-slate-200">
                  {sim.bladeCount} Patches
                </span>
              </div>
              <input
                type="range"
                min={2}
                max={8}
                step={1}
                value={sim.bladeCount}
                onChange={(e) => {
                  sim.bladeCount = Number(e.target.value);
                  onForceUpdate();
                }}
                className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#131B28] accent-blue-500"
              />
            </div>
          </div>

          {/* Microphone Selector Grid */}
          <div className="mt-4 pt-3 border-t border-[#1E293B]">
            <label className="mb-1.5 block font-mono-tabular text-[10px] uppercase text-[#94A3B8]">
              3D Microphone Receiver Array x_mic
            </label>
            <div className="grid grid-cols-2 gap-1.5">
              {sim.microphones.map((m, idx) => (
                <button
                  key={m.id}
                  onClick={() => {
                    sim.selectedMicIndex = idx;
                    onForceUpdate();
                  }}
                  className={`rounded border px-2 py-1 font-mono-tabular text-[11px] transition ${
                    sim.selectedMicIndex === idx
                      ? 'border-cyan-400 bg-cyan-500/20 text-white font-semibold'
                      : 'border-[#1E293B] bg-[#131B28] text-[#94A3B8] hover:text-slate-200'
                  }`}
                >
                  {m.label}
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* Governing Equations Card */}
        <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3.5">
          <div className="mb-2 flex items-center justify-between">
            <span className="font-display text-[11px] font-bold tracking-wider uppercase text-cyan-400">
              Farassat 1A Integrals (Module C2)
            </span>
            <button
              onClick={onOpenCppSection}
              className="flex items-center gap-1 rounded border border-[#1E293B] bg-[#131B28] px-2 py-0.5 font-mono-tabular text-[10px] text-cyan-300 hover:border-cyan-500/40"
            >
              <Code2 className="h-3 w-3" />
              C++20 Lines 446–644
            </button>
          </div>
          <div className="space-y-1.5 font-mono-tabular text-[11px] leading-relaxed text-slate-300">
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">Thickness Noise p'_T(x,t):</div>
              <div>4π p'_T = ∫ [ρ₀ U̇_n/(r|1-M_r|²) + ρ₀ U_n(r Ṁ_r + c₀(M_r-M²))/(r²|1-M_r|³)]_ret dS</div>
            </div>
            <div className="rounded bg-[#070A0F] px-2.5 py-1.5 border border-[#1E293B]/60">
              <div className="text-[10px] text-[#64748B]">Loading Noise p'_L(x,t) & OASPL:</div>
              <div>4π p'_L = (1/c₀)∫ [L̇_r/(r|1-M_r|²) + c₀(L_r-L_M)/(r²|1-M_r|²) + ...]_ret dS</div>
              <div>OASPL = 20·log₁₀(p_rms / 20 μPa) [dB]</div>
            </div>
          </div>
        </div>
      </div>

      {/* CENTER PRIMARY 2D ACOUSTIC WAVEFRONT & POLAR DIRECTIVITY CANVAS (5 COLS) */}
      <div className="flex flex-col gap-3.5 lg:col-span-5">
        <div className="flex flex-1 flex-col rounded-lg border border-[#1E293B] bg-[#0D121B] p-4">
          <div className="mb-2.5 flex flex-wrap items-center justify-between gap-2">
            <div>
              <h3 className="font-display text-xs font-bold tracking-wider uppercase text-slate-100">
                Doppler Wavefront Radiation & Polar Directivity Map
              </h3>
              <p className="font-mono-tabular text-[11px] text-[#94A3B8]">
                Active Receiver: <span className="text-cyan-300">{selectedMic.label}</span> at r ={' '}
                {sim.observerRadiusM.toFixed(1)} m • Click any MIC node to switch
              </p>
            </div>
            <span className="rounded border border-cyan-500/30 bg-cyan-500/10 px-2 py-0.5 font-mono-tabular text-[10px] text-cyan-300">
              |1 - M_r| ∈ [{telemetry.dopplerFactorMin.toFixed(2)}, {telemetry.dopplerFactorMax.toFixed(2)}]
            </span>
          </div>

          <div className="relative flex flex-1 items-center justify-center rounded-lg border border-[#1E293B] bg-[#070A0F] p-2">
            <svg viewBox="0 0 420 420" className="h-full max-h-[385px] w-full select-none">
              {/* Acoustic Doppler Wavefront Rings (Cool-Warm Diverging Colormap) */}
              {Array.from({ length: 14 }).map((_, k) => {
                const baseR = ((k + 1) * 11 + (sim.phaseRad * 14) % 11);
                const dopplerShiftX = -sim.tipMach * baseR * 0.28;
                const phaseVal = Math.sin(k * 0.95 - sim.phaseRad * 2.0);
                const rgb = coolWarmRGB(0.5 + 0.48 * phaseVal);
                return (
                  <circle
                    key={k}
                    cx={cx + dopplerShiftX}
                    cy={cy}
                    r={baseR}
                    fill="none"
                    stroke={rgbToCss(rgb, 0.36)}
                    strokeWidth={3.2}
                  />
                );
              })}

              {/* Polar Coordinate Reference Circles */}
              {[50, 100, 148].map((r, idx) => (
                <g key={r}>
                  <circle
                    cx={cx}
                    cy={cy}
                    r={r}
                    fill="none"
                    stroke="#1E293B"
                    strokeDasharray="3 3"
                    strokeWidth={1}
                  />
                  <text
                    x={cx + 4}
                    y={cy - r + 10}
                    className="fill-[#64748B] font-mono-tabular text-[9px]"
                  >
                    {110 + idx * 15} dB
                  </text>
                </g>
              ))}

              {/* Crosshair Axes */}
              <line x1={cx - 165} y1={cy} x2={cx + 165} y2={cy} stroke="#1E293B" />
              <line x1={cx} y1={cy - 165} x2={cx} y2={cy + 165} stroke="#1E293B" />

              {/* OASPL Polar Directivity Lobe Polygon */}
              <polygon
                points={polarPoints}
                fill="rgba(6, 182, 212, 0.16)"
                stroke="#06B6D4"
                strokeWidth={2}
              />

              {/* Rotating FWH Patches & Radiation Vectors r_hat to Active Microphone */}
              {Array.from({ length: sim.bladeCount }).map((_, b) => {
                const theta =
                  (2.0 * Math.PI * b) / sim.bladeCount + sim.phaseRad;
                const px = cx + 34 * Math.cos(theta);
                const py = cy - 34 * Math.sin(theta);

                const micRad = (selectedMic.angleDeg * Math.PI) / 180;
                const mx = cx + maxR * Math.cos(micRad);
                const my = cy - maxR * Math.sin(micRad);

                return (
                  <g key={b}>
                    {/* Radiation vector r_hat = (x_mic - y) / ||x_mic - y|| */}
                    <line
                      x1={px}
                      y1={py}
                      x2={mx}
                      y2={my}
                      stroke="#38BDF8"
                      strokeOpacity={0.45}
                      strokeDasharray="3 3"
                      strokeWidth={1.1}
                    />
                    {/* Rotor blade / FWH patch */}
                    <line
                      x1={cx}
                      y1={cy}
                      x2={px}
                      y2={py}
                      stroke="#F1F5F9"
                      strokeWidth={2.5}
                    />
                    <circle
                      cx={px}
                      cy={py}
                      r={4}
                      fill="#F97316"
                      stroke="#070A0F"
                      strokeWidth={1}
                    />
                  </g>
                );
              })}

              {/* Permeable Control Surface Ring */}
              {sim.permeableMode && (
                <circle
                  cx={cx}
                  cy={cy}
                  r={42}
                  fill="none"
                  stroke="#F59E0B"
                  strokeDasharray="4 2"
                  strokeWidth={1.5}
                />
              )}

              {/* 8 Interactive Microphone Receiver Nodes */}
              {sim.microphones.map((m, idx) => {
                const rad = (m.angleDeg * Math.PI) / 180;
                const mx = cx + maxR * Math.cos(rad);
                const my = cy - maxR * Math.sin(rad);
                const isSel = idx === sim.selectedMicIndex;

                return (
                  <g
                    key={m.id}
                    className="cursor-pointer"
                    onClick={() => {
                      sim.selectedMicIndex = idx;
                      onForceUpdate();
                    }}
                  >
                    <circle
                      cx={mx}
                      cy={my}
                      r={isSel ? 8 : 5.5}
                      fill={isSel ? '#06B6D4' : '#1E293B'}
                      stroke={isSel ? '#F8FAFC' : '#64748B'}
                      strokeWidth={isSel ? 2 : 1.2}
                    />
                    <text
                      x={cx + (maxR + 22) * Math.cos(rad)}
                      y={cy - (maxR + 22) * Math.sin(rad) + 3}
                      textAnchor="middle"
                      className={`font-mono-tabular text-[9px] ${
                        isSel ? 'fill-cyan-300 font-bold' : 'fill-[#94A3B8]'
                      }`}
                    >
                      M{idx + 1} ({m.angleDeg}°)
                    </text>
                  </g>
                );
              })}
            </svg>
          </div>

          {/* Cool-Warm Colormap Legend */}
          <div className="mt-3 flex items-center gap-3 pt-2 border-t border-[#1E293B]">
            <span className="font-mono-tabular text-[10px] text-[#94A3B8]">
              Cool-Warm p'(x,t):
            </span>
            <div
              className="h-2.5 flex-1 rounded-full border border-[#1E293B]"
              style={{
                background:
                  'linear-gradient(90deg, rgb(59,76,192) 0%, rgb(241,245,249) 50%, rgb(180,4,38) 100%)',
              }}
            />
            <span className="font-mono-tabular text-[10px] text-cyan-300">
              -200 Pa ➔ +200 Pa
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
              <span>Overall SPL (OASPL)</span>
              <Volume2 className="h-3.5 w-3.5 text-cyan-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-cyan-300">
              {telemetry.oaspl_dB.toFixed(2)} dB{' '}
              <span className="text-[10px] font-normal text-[#94A3B8]">
                re 20 μPa
              </span>
            </div>
          </div>

          <div className="rounded-lg border border-[#1E293B] bg-[#0D121B] p-3">
            <div className="flex items-center justify-between text-[11px] text-[#94A3B8]">
              <span>Acoustic RMS & BPF Tone</span>
              <Activity className="h-3.5 w-3.5 text-blue-400" />
            </div>
            <div className="mt-1 font-mono-tabular text-lg font-bold text-blue-300">
              {telemetry.p_rms_Pa.toFixed(2)} Pa{' '}
              <span className="text-xs font-normal text-[#94A3B8]">
                @ {telemetry.peak_freq_Hz.toFixed(0)} Hz
              </span>
            </div>
          </div>
        </div>

        {/* Chart 1: Retarded-Time Thickness & Loading Noise */}
        <MultiTraceChart
          title="Farassat 1A Retarded-Time Acoustic Waveform"
          subtitle="Thickness p'_T(x,t) + Loading p'_L(x,t) at active receiver"
          xValues={telemetry.time_ms}
          xUnit="ms"
          yUnit="Pa"
          rightBadge={selectedMic.label}
          series={[
            {
              label: "p'_T (Thickness)",
              color: '#06B6D4',
              data: telemetry.p_thickness_Pa,
              unit: 'Pa',
            },
            {
              label: "p'_L (Loading)",
              color: '#3B82F6',
              data: telemetry.p_loading_Pa,
              unit: 'Pa',
            },
            {
              label: "p'_total",
              color: '#F43F5E',
              data: telemetry.p_total_Pa,
              dashed: true,
              unit: 'Pa',
            },
          ]}
        />

        {/* Chart 2: Narrowband PSD Spectrum */}
        <MultiTraceChart
          title="Radix-2 Hann-Windowed FFT Narrowband Spectrum"
          subtitle="Power Spectral Density [dB re (20 μPa)²/Hz] & Narrowband SPL"
          xValues={telemetry.freq_Hz}
          xUnit="Hz"
          yUnit="dB"
          rightBadge="N=512 Hann"
          series={[
            {
              label: 'Narrowband SPL',
              color: '#06B6D4',
              data: telemetry.spl_narrow_dB,
              unit: 'dB',
            },
            {
              label: 'PSD',
              color: '#8B5CF6',
              data: telemetry.psd_dB_Hz,
              dashed: true,
              unit: 'dB/Hz',
            },
          ]}
        />

        {/* Chart 3: 1/3-Octave Bands */}
        <ThirdOctaveBarChart bands={telemetry.thirdOctaveBands} />
      </div>
    </div>
  );
};
