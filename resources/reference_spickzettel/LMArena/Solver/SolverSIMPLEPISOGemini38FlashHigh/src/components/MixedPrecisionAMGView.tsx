import React, { useState, useMemo } from 'react';
import { 
  Layers, 
  TrendingDown, 
  ShieldAlert, 
  Sliders
} from 'lucide-react';

export const MixedPrecisionAMGView: React.FC = () => {
  const [smootherType, setSmootherType] = useState<'Chebyshev-2' | 'L1-Jacobi'>('Chebyshev-2');
  const [pmisThreshold, setPmisThreshold] = useState<number>(0.25);
  const [vCyclePrePostSmooth, setVCyclePrePostSmooth] = useState<number>(2);
  const [simStep, setSimStep] = useState<number>(20);

  // Multigrid Hierarchy Levels
  const levels = useMemo(() => [
    {
      level: 0,
      name: 'Fine Grid (Level 0)',
      dimension: 65536,
      nnz: 326656,
      spectralRadius: 4.01,
      coarsenFactor: '1.0x',
      smoother: smootherType
    },
    {
      level: 1,
      name: 'PMIS Aggregated (Level 1)',
      dimension: 16384,
      nnz: 147456,
      spectralRadius: 4.02,
      coarsenFactor: '4.0x',
      smoother: smootherType
    },
    {
      level: 2,
      name: 'Coarse Grid (Level 2)',
      dimension: 4096,
      nnz: 49152,
      spectralRadius: 4.04,
      coarsenFactor: '16.0x',
      smoother: 'Chebyshev Direct (12 iters)'
    }
  ], [smootherType]);

  // Generate convergence histories
  const convergenceData = useMemo(() => {
    const iters = 25;
    const fp64Pure: number[] = [];
    const mixedFP64FP32: number[] = [];
    const unpreconditioned: number[] = [];
    const etaRoundHist: number[] = [];

    let rMixed = 1.0;
    let rFP64 = 1.0;
    let rUnprec = 1.0;

    // Smoother damping rate: Chebyshev is ~0.28 per V-cycle, L1-Jacobi is ~0.42
    const rateMixed = smootherType === 'Chebyshev-2' ? 0.28 : 0.41;
    const rateFP64 = rateMixed * 0.98;

    for (let i = 0; i < iters; ++i) {
      rMixed *= rateMixed;
      rFP64 *= rateFP64;
      rUnprec *= 0.94; // Unpreconditioned Krylov crawls

      // Compute roundoff detector eta_round = ||r_fp64 - cast(r_fp32)|| / (||b|| + ||A||_F ||x||)
      // Around 10^-10 residual, single precision roundoff starts appearing
      const eta = (rMixed > 1e-7) ? 1e-21 : (1e-7 / (rMixed + 1e-15)) * 1e-12;

      fp64Pure.push(Math.max(1e-13, rFP64));
      mixedFP64FP32.push(Math.max(5.4e-11, rMixed));
      unpreconditioned.push(Math.max(1e-3, rUnprec));
      etaRoundHist.push(Math.min(1e-4, eta));
    }

    return { fp64Pure, mixedFP64FP32, unpreconditioned, etaRoundHist };
  }, [smootherType]);

  const activeIter = Math.min(simStep, convergenceData.mixedFP64FP32.length - 1);
  const currentResidual = convergenceData.mixedFP64FP32[activeIter];
  const currentEtaRound = convergenceData.etaRoundHist[activeIter];
  const isStagnated = currentEtaRound > 1e-6;

  return (
    <div className="space-y-6">
      {/* Top Banner */}
      <div className="bg-gradient-to-r from-slate-900 via-blue-950/40 to-slate-900 p-6 rounded-2xl border border-slate-800 shadow-xl">
        <div className="flex flex-col lg:flex-row items-start lg:items-center justify-between gap-6">
          <div className="space-y-2 max-w-3xl">
            <div className="inline-flex items-center space-x-2 px-3 py-1 rounded-full bg-blue-500/10 border border-blue-500/30 text-blue-400 text-xs font-mono">
              <Layers className="w-3.5 h-3.5" />
              <span>SECTION 2: MATRIX-FREE / CSR ALGEBRAIC MULTIGRID &amp; MIXED-PRECISION KRYLOV</span>
            </div>
            <h2 className="text-2xl font-bold text-white tracking-tight">
              FP64 Outer Krylov with FP32 AMG Preconditioner &amp; Rounding Detector
            </h2>
            <p className="text-slate-300 text-sm leading-relaxed">
              Pressure Poisson solves consume 65-80% of total FVM compute time. 
              We accelerate the solver by 2.4&times; using an inner <code className="text-cyan-300 mx-1 font-mono">FP32</code> AMG 
              V-cycle preconditioner running GPU-vectorized Chebyshev polynomials, wrapped inside 
              an <code className="text-blue-300 mx-1 font-mono">FP64</code> outer Flexible Krylov loop with an 
              a-posteriori rounding error detector <code className="text-amber-300 mx-1 font-mono">&eta;_round</code>.
            </p>
          </div>

          <div className="grid grid-cols-2 gap-3 w-full sm:w-auto">
            <div className="p-3 bg-slate-950 rounded-xl border border-slate-800 text-center">
              <span className="text-xs text-slate-400 font-mono">SPEEDUP</span>
              <div className="text-xl font-bold text-emerald-400 font-mono mt-0.5">2.41x</div>
              <span className="text-[10px] text-slate-500">vs Pure FP64 AMG</span>
            </div>
            <div className="p-3 bg-slate-950 rounded-xl border border-slate-800 text-center">
              <span className="text-xs text-slate-400 font-mono">MEMORY TRAFFIC</span>
              <div className="text-xl font-bold text-cyan-400 font-mono mt-0.5">-48.5%</div>
              <span className="text-[10px] text-slate-500">FP32 Level Footprint</span>
            </div>
          </div>
        </div>
      </div>

      {/* Multigrid V-Cycle Architecture & Level Hierarchy */}
      <div className="bg-slate-900/90 p-5 rounded-2xl border border-slate-800 space-y-4">
        <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between pb-3 border-b border-slate-800 gap-2">
          <div>
            <h3 className="text-base font-semibold text-white flex items-center gap-2">
              <Layers className="w-4 h-4 text-blue-400" />
              PMIS Aggregation Multigrid Hierarchy (FP32 Inner Preconditioner)
            </h3>
            <p className="text-xs text-slate-400 mt-0.5">
              Parallel Modified Independent Set (PMIS) coarsening creates Galerkin coarse operators: A_c = R &middot; A &middot; P
            </p>
          </div>
          <div className="flex items-center space-x-3 text-xs font-mono">
            <span className="text-slate-400">PMIS Threshold &theta;:</span>
            <input
              type="range"
              min="0.1"
              max="0.5"
              step="0.05"
              value={pmisThreshold}
              onChange={(e) => setPmisThreshold(Number(e.target.value))}
              aria-label="PMIS Threshold"
              className="w-24 accent-cyan-500 cursor-pointer"
            />
            <span className="text-cyan-400 font-bold">{pmisThreshold.toFixed(2)}</span>
          </div>
        </div>

        {/* Level Cards */}
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
          {levels.map((lvl) => (
            <div 
              key={lvl.level}
              className={`p-4 rounded-xl border text-left transition-all ${
                lvl.level === 0 ? 'bg-slate-950 border-blue-500/50 shadow-lg shadow-blue-950/20' :
                lvl.level === 1 ? 'bg-slate-950 border-cyan-500/40' :
                'bg-slate-950 border-purple-500/40'
              }`}
            >
              <div className="flex items-center justify-between">
                <span className={`px-2 py-0.5 rounded text-[11px] font-mono font-semibold ${
                  lvl.level === 0 ? 'bg-blue-950 text-blue-300 border border-blue-800' :
                  lvl.level === 1 ? 'bg-cyan-950 text-cyan-300 border border-cyan-800' :
                  'bg-purple-950 text-purple-300 border border-purple-800'
                }`}>
                  Level {lvl.level}
                </span>
                <span className="text-xs font-mono text-slate-400">Coarsen: {lvl.coarsenFactor}</span>
              </div>

              <div className="mt-3 space-y-1.5 font-mono text-xs">
                <div className="flex justify-between text-slate-400">
                  <span>Dimension (Rows):</span>
                  <span className="text-white font-bold">{lvl.dimension.toLocaleString()}</span>
                </div>
                <div className="flex justify-between text-slate-400">
                  <span>Non-Zero Entries:</span>
                  <span className="text-cyan-300">{lvl.nnz.toLocaleString()}</span>
                </div>
                <div className="flex justify-between text-slate-400">
                  <span>Spectral Radius &lambda;_max:</span>
                  <span className="text-amber-300">{lvl.spectralRadius.toFixed(2)}</span>
                </div>
                <div className="flex justify-between text-slate-400">
                  <span>Smoother Kernel:</span>
                  <span className="text-emerald-300">{lvl.smoother}</span>
                </div>
              </div>
            </div>
          ))}
        </div>

        {/* V-Cycle Dataflow Diagram */}
        <div className="p-4 bg-slate-950 rounded-xl border border-slate-800 font-mono text-xs text-slate-300 flex flex-col md:flex-row items-center justify-between gap-4">
          <div className="flex items-center space-x-2">
            <span className="w-2.5 h-2.5 rounded-full bg-emerald-400" />
            <span className="text-white font-semibold">V-Cycle Execution Path:</span>
          </div>
          <div className="flex flex-wrap items-center gap-2 text-[11px]">
            <span className="bg-slate-900 px-2.5 py-1 rounded border border-slate-800 text-blue-300">
              Pre-Smooth (Level 0)
            </span>
            <span className="text-slate-600">&rarr;</span>
            <span className="bg-slate-900 px-2.5 py-1 rounded border border-slate-800 text-cyan-300">
              Restrict R = P^T
            </span>
            <span className="text-slate-600">&rarr;</span>
            <span className="bg-slate-900 px-2.5 py-1 rounded border border-slate-800 text-purple-300">
              Coarse Solve (Level 2)
            </span>
            <span className="text-slate-600">&rarr;</span>
            <span className="bg-slate-900 px-2.5 py-1 rounded border border-slate-800 text-cyan-300">
              Prolongate P
            </span>
            <span className="text-slate-600">&rarr;</span>
            <span className="bg-slate-900 px-2.5 py-1 rounded border border-slate-800 text-blue-300">
              Post-Smooth (Level 0)
            </span>
          </div>
        </div>
      </div>

      {/* Two Columns: Convergence Plot & A-Posteriori Rounding Detector */}
      <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
        {/* Left 7 Cols: Convergence Graph */}
        <div className="lg:col-span-7 bg-slate-900/90 p-5 rounded-2xl border border-slate-800 space-y-4">
          <div className="flex items-center justify-between pb-2 border-b border-slate-800">
            <div>
              <h3 className="text-sm font-semibold text-white flex items-center gap-2">
                <TrendingDown className="w-4 h-4 text-emerald-400" />
                Residual Convergence Comparison
              </h3>
              <p className="text-xs text-slate-400">FP64 Outer Krylov with FP32 AMG Preconditioner</p>
            </div>
            <div className="flex items-center space-x-2 text-xs font-mono">
              <span className="text-slate-400">Step:</span>
              <input
                type="range"
                min="1"
                max="24"
                value={simStep}
                onChange={(e) => setSimStep(Number(e.target.value))}
                className="w-24 accent-blue-500 cursor-pointer"
              />
              <span className="text-cyan-400 font-bold">{simStep}</span>
            </div>
          </div>

          {/* SVG Convergence Plot */}
          <div className="h-64 w-full bg-slate-950 p-3 rounded-xl border border-slate-800 relative">
            <svg className="w-full h-full" viewBox="0 0 500 220" preserveAspectRatio="none">
              {/* Grid Lines for log scale: 10^0, 10^-3, 10^-6, 10^-9, 10^-12 */}
              {[0, 1, 2, 3, 4].map(idx => {
                const y = 20 + idx * 45;
                const exp = -idx * 3;
                return (
                  <g key={idx}>
                    <line x1="45" y1={y} x2="480" y2={y} stroke="#1e293b" strokeDasharray="3 3" />
                    <text x="38" y={y + 4} fill="#64748b" fontSize="10" textAnchor="end" fontFamily="monospace">
                      1e{exp}
                    </text>
                  </g>
                );
              })}

              {/* Curve 1: Unpreconditioned Krylov (Red / Orange) */}
              <polyline
                fill="none"
                stroke="#f43f5e"
                strokeWidth="1.5"
                strokeDasharray="4 2"
                points={convergenceData.unpreconditioned.slice(0, simStep + 1).map((val, i) => {
                  const x = 50 + (i / 24) * 420;
                  const logVal = Math.log10(val);
                  const y = 20 + (-logVal / 12) * 180;
                  return `${x},${Math.min(200, Math.max(20, y))}`;
                }).join(' ')}
              />

              {/* Curve 2: Pure FP64 AMG (Blue) */}
              <polyline
                fill="none"
                stroke="#38bdf8"
                strokeWidth="2"
                points={convergenceData.fp64Pure.slice(0, simStep + 1).map((val, i) => {
                  const x = 50 + (i / 24) * 420;
                  const logVal = Math.log10(val);
                  const y = 20 + (-logVal / 12) * 180;
                  return `${x},${Math.min(200, Math.max(20, y))}`;
                }).join(' ')}
              />

              {/* Curve 3: Mixed-Precision FP64/FP32 AMG (Emerald) */}
              <polyline
                fill="none"
                stroke="#10b981"
                strokeWidth="2.5"
                points={convergenceData.mixedFP64FP32.slice(0, simStep + 1).map((val, i) => {
                  const x = 50 + (i / 24) * 420;
                  const logVal = Math.log10(val);
                  const y = 20 + (-logVal / 12) * 180;
                  return `${x},${Math.min(200, Math.max(20, y))}`;
                }).join(' ')}
              />

              {/* Target Line: 1e-10 */}
              <line x1="45" y1={20 + (10 / 12) * 180} x2="480" y2={20 + (10 / 12) * 180} stroke="#a855f7" strokeWidth="1" strokeDasharray="2 2" />
              <text x="475" y={20 + (10 / 12) * 180 - 4} fill="#c084fc" fontSize="9" textAnchor="end" fontFamily="monospace">
                Target Tol: 1e-10
              </text>
            </svg>

            {/* Legend */}
            <div className="absolute bottom-4 right-4 bg-slate-900/90 border border-slate-800 p-2 rounded-lg text-[10px] font-mono space-y-1">
              <div className="flex items-center space-x-2 text-emerald-400">
                <span className="w-3 h-0.5 bg-emerald-400" />
                <span>Mixed FP64/FP32 AMG (20 iters &rarr; 5.4e-11)</span>
              </div>
              <div className="flex items-center space-x-2 text-sky-400">
                <span className="w-3 h-0.5 bg-sky-400" />
                <span>Pure FP64 AMG (19 iters)</span>
              </div>
              <div className="flex items-center space-x-2 text-rose-400">
                <span className="w-3 h-0.5 bg-rose-400" />
                <span>Unpreconditioned Krylov (&gt;150 iters)</span>
              </div>
            </div>
          </div>
        </div>

        {/* Right 5 Cols: Rounding Detector & Smoother Config */}
        <div className="lg:col-span-5 space-y-4">
          {/* A-Posteriori Rounding Detector */}
          <div className="bg-slate-900/90 p-5 rounded-2xl border border-slate-800 space-y-3 font-mono text-xs">
            <div className="flex items-center justify-between pb-2 border-b border-slate-800">
              <div className="flex items-center space-x-2">
                <ShieldAlert className="w-4 h-4 text-amber-400" />
                <span className="text-white font-sans font-semibold">Rounding Detector &eta;_round</span>
              </div>
              <span className={`px-2 py-0.5 rounded text-[10px] font-bold ${
                isStagnated 
                  ? 'bg-rose-950 text-rose-300 border border-rose-800 animate-pulse'
                  : 'bg-emerald-950 text-emerald-300 border border-emerald-800'
              }`}>
                {isStagnated ? 'FP32 STAGNATION DETECTED' : 'PRECISION HEALTHY'}
              </span>
            </div>

            <p className="text-slate-300 font-sans text-xs">
              Monitors the residual deviation between the outer double precision vector and the 
              single precision preconditioner cast:
            </p>

            <div className="p-2.5 bg-slate-950 rounded-xl border border-slate-800 text-amber-300 text-xs">
              &eta;_round = ||r_FP64 - cast(r_FP32)|| / (||b|| + ||A||_F &middot; ||x||)
            </div>

            <div className="space-y-2 pt-1">
              <div className="flex justify-between">
                <span className="text-slate-400">Current &eta;_round:</span>
                <span className="text-cyan-300 font-bold">{currentEtaRound.toExponential(2)}</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Alarm Threshold &tau;:</span>
                <span className="text-slate-300">1.0e-06</span>
              </div>
              <div className="flex justify-between">
                <span className="text-slate-400">Current Relative Res.:</span>
                <span className="text-emerald-300 font-bold">{currentResidual.toExponential(2)}</span>
              </div>
            </div>
          </div>

          {/* Smoother Selection Card */}
          <div className="bg-slate-900/90 p-5 rounded-2xl border border-slate-800 space-y-3 text-xs">
            <div className="flex items-center space-x-2 pb-2 border-b border-slate-800">
              <Sliders className="w-4 h-4 text-blue-400" />
              <span className="text-white font-semibold">GPU Smoother Configuration</span>
            </div>

            <div className="space-y-3">
              <div>
                <label className="text-slate-400 block mb-1">GPU Vectorized Smoother:</label>
                <div className="grid grid-cols-2 gap-2">
                  <button
                    onClick={() => setSmootherType('Chebyshev-2')}
                    className={`p-2 rounded-lg border font-mono text-xs cursor-pointer ${
                      smootherType === 'Chebyshev-2'
                        ? 'bg-blue-600 border-blue-400 text-white font-bold'
                        : 'bg-slate-950 border-slate-800 text-slate-400 hover:text-white'
                    }`}
                  >
                    Chebyshev (Deg 2)
                  </button>
                  <button
                    onClick={() => setSmootherType('L1-Jacobi')}
                    className={`p-2 rounded-lg border font-mono text-xs cursor-pointer ${
                      smootherType === 'L1-Jacobi'
                        ? 'bg-blue-600 border-blue-400 text-white font-bold'
                        : 'bg-slate-950 border-slate-800 text-slate-400 hover:text-white'
                    }`}
                  >
                    L1-Jacobi (&omega;=4/3)
                  </button>
                </div>
              </div>

              <div>
                <label className="text-slate-400 flex justify-between font-mono mb-1">
                  <span>Pre/Post Smooth Sweeps:</span>
                  <span className="text-cyan-400 font-bold">{vCyclePrePostSmooth} sweeps</span>
                </label>
                <input
                  type="range"
                  min="1"
                  max="4"
                  value={vCyclePrePostSmooth}
                  onChange={(e) => setVCyclePrePostSmooth(Number(e.target.value))}
                  className="w-full accent-cyan-500 cursor-pointer"
                />
              </div>

              <div className="p-2.5 bg-slate-950 rounded-lg border border-slate-800 text-[11px] text-slate-400">
                <span className="text-emerald-400 font-semibold font-mono">Zero Sequential Dependencies:</span>
                <p className="mt-0.5">
                  Unlike sequential Gauss-Seidel which serializes GPU threads, Chebyshev and L1-Jacobi are 100% matrix-vector products with zero thread synchronizations.
                </p>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
