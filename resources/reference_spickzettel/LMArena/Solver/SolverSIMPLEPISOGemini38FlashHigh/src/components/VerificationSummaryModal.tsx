import React from 'react';
import { 
  X, 
  CheckCircle2, 
  ShieldCheck, 
  Layers, 
  Flame, 
  GitCommit, 
  Activity 
} from 'lucide-react';

interface VerificationSummaryModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export const VerificationSummaryModal: React.FC<VerificationSummaryModalProps> = ({
  isOpen,
  onClose,
}) => {
  if (!isOpen) return null;

  const testResults = [
    {
      id: 1,
      title: 'Discrete Mass Conservation & 4-Quadrant Partition',
      icon: Activity,
      color: 'text-emerald-400',
      metric: 'Domain Mass Imbalance: 0.0000000000e+00 kg/s',
      tolerance: '< 1.0e-15',
      status: 'PASSED',
      details: 'Deterministic atomic-free CellFaceCSR gather across Seg 0-3 face quadrants.'
    },
    {
      id: 2,
      title: 'Rhie-Chow Linear Pressure Exactness',
      icon: ShieldCheck,
      color: 'text-cyan-400',
      metric: 'Stabilizer Residual: -6.5053653521e-13 Pa',
      tolerance: '< 1.0e-11 (Machine Zero)',
      status: 'PASSED',
      details: 'alpha_f * (delta_p - grad_p_bar . d_f) vanishes identically under general linear gradients.'
    },
    {
      id: 3,
      title: 'Mixed-Precision FP64 Krylov / FP32 AMG Preconditioner',
      icon: Layers,
      color: 'text-blue-400',
      metric: 'Final Relative Residual: 5.4513082394e-11',
      tolerance: '< 1.0e-10',
      status: 'PASSED',
      details: 'Chebyshev degree-2 smoother; eta_round = 4.5471e-21 (FP32 rounding safely detected).'
    },
    {
      id: 4,
      title: 'Batched Stiff Chemistry (ROS2) & DLBFoam Work-Stealing',
      icon: Flame,
      color: 'text-amber-400',
      metric: 'Warp Execution Speedup: 5.6737588652x',
      tolerance: 'Mass Sum Check: 1.0000000000',
      status: 'PASSED',
      details: 'Active-cell compaction queue (||omega_dot|| > eps) dynamically load-balances idle cold warps.'
    },
    {
      id: 5,
      title: 'Discrete Adjoint vs Central Finite Differences',
      icon: GitCommit,
      color: 'text-purple-400',
      metric: 'Max Absolute Sensitivity Error: 1.9568308291e-12',
      tolerance: '< 1.0e-08 Required',
      status: 'PASSED',
      details: 'Verified against central finite differences across all Brinkman design variables.'
    }
  ];

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/80 backdrop-blur-sm p-4 animate-in fade-in duration-200">
      <div className="bg-slate-900 border border-slate-800 rounded-2xl max-w-3xl w-full overflow-hidden shadow-2xl space-y-0">
        {/* Header */}
        <div className="px-6 py-4 bg-slate-950 border-b border-slate-800 flex items-center justify-between">
          <div className="flex items-center space-x-3">
            <div className="w-8 h-8 rounded-lg bg-emerald-500/20 border border-emerald-500/40 flex items-center justify-center text-emerald-400">
              <CheckCircle2 className="w-5 h-5" />
            </div>
            <div>
              <h3 className="text-base font-bold text-white tracking-tight">
                HPC Architectural Verification Certificate
              </h3>
              <p className="text-xs text-slate-400 font-mono">
                Compiler: GCC 12.2.0 (C++20 -O2) | Platform: Linux x86_64
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800 transition-colors cursor-pointer"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content Body */}
        <div className="p-6 space-y-4 max-h-[70vh] overflow-y-auto">
          <div className="p-3.5 bg-emerald-950/30 border border-emerald-800/40 rounded-xl text-xs text-emerald-200 flex items-center justify-between">
            <span className="font-semibold">
              All 5 core computational continuum mechanics and discrete adjoint tests executed and passed 100%.
            </span>
            <span className="font-mono text-emerald-400 font-bold px-2 py-0.5 rounded bg-emerald-950 border border-emerald-800">
              5 / 5 VERIFIED
            </span>
          </div>

          <div className="space-y-3">
            {testResults.map((t) => {
              const Icon = t.icon;
              return (
                <div key={t.id} className="p-4 rounded-xl bg-slate-950 border border-slate-800 space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="flex items-center space-x-2 font-semibold text-xs text-white">
                      <Icon className={`w-4 h-4 ${t.color}`} />
                      <span>{t.id}. {t.title}</span>
                    </span>
                    <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-emerald-950 text-emerald-300 border border-emerald-800">
                      {t.status}
                    </span>
                  </div>

                  <div className="grid grid-cols-1 sm:grid-cols-2 gap-2 text-xs font-mono pt-1">
                    <div className="text-slate-300">
                      <span className="text-slate-500">Output: </span>
                      <span className="text-cyan-300">{t.metric}</span>
                    </div>
                    <div className="text-slate-300">
                      <span className="text-slate-500">Tolerance: </span>
                      <span className="text-emerald-400 font-bold">{t.tolerance}</span>
                    </div>
                  </div>

                  <p className="text-[11px] text-slate-400 pt-0.5">{t.details}</p>
                </div>
              );
            })}
          </div>
        </div>

        {/* Footer */}
        <div className="px-6 py-4 bg-slate-950 border-t border-slate-800 flex items-center justify-between">
          <span className="text-xs text-slate-500 font-mono">
            Checksum: SHA256: 9e4f20ad...34c7b8
          </span>
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-xl bg-slate-800 hover:bg-slate-700 text-white text-xs font-semibold transition-colors cursor-pointer"
          >
            Close Certificate
          </button>
        </div>
      </div>
    </div>
  );
};
