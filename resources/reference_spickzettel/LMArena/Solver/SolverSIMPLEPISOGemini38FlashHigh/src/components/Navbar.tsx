import React from 'react';
import { 
  Activity, 
  Cpu, 
  Layers, 
  Flame, 
  GitCommit, 
  Code2, 
  CheckCircle2, 
  Zap
} from 'lucide-react';
import { ActiveTab } from '../types/solverTypes';

interface NavbarProps {
  activeTab: ActiveTab;
  setActiveTab: (tab: ActiveTab) => void;
  onOpenVerificationModal: () => void;
}

export const Navbar: React.FC<NavbarProps> = ({
  activeTab,
  setActiveTab,
  onOpenVerificationModal,
}) => {
  return (
    <header className="bg-slate-900 border-b border-slate-800 text-slate-100 sticky top-0 z-50">
      {/* Top Banner / System Telemetry */}
      <div className="px-4 py-1.5 bg-slate-950/80 border-b border-slate-800/60 flex flex-wrap items-center justify-between text-xs font-mono text-slate-400">
        <div className="flex items-center space-x-4">
          <span className="flex items-center text-emerald-400 font-semibold">
            <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse mr-2" />
            HPC ARCHITECTURE: C++20 / CUDA 12.4 / OpenMP 5.2
          </span>
          <span className="hidden md:inline-block text-slate-600">|</span>
          <span className="hidden md:inline-block">DATA LAKE: C-ABI SoA (Zero-Copy CAbiSpan)</span>
          <span className="hidden lg:inline-block text-slate-600">|</span>
          <span className="hidden lg:inline-block">LAYOUT: 4-Quadrant Hilbert Partition</span>
        </div>
        <div className="flex items-center space-x-3">
          <span className="text-cyan-400 bg-cyan-950/40 px-2 py-0.5 rounded border border-cyan-800/50">
            HBM3e: 3.2 TB/s Stream
          </span>
          <button
            onClick={onOpenVerificationModal}
            className="flex items-center space-x-1.5 bg-emerald-500/20 text-emerald-300 hover:bg-emerald-500/30 px-2.5 py-0.5 rounded border border-emerald-500/40 transition-colors cursor-pointer"
          >
            <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
            <span>Verification: 100% Passed</span>
          </button>
        </div>
      </div>

      {/* Main Nav Bar */}
      <div className="px-4 py-2.5 flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center space-x-3">
          <div className="w-9 h-9 rounded-lg bg-gradient-to-tr from-cyan-600 to-blue-600 flex items-center justify-center shadow-lg shadow-cyan-900/30">
            <Zap className="w-5 h-5 text-white" />
          </div>
          <div>
            <h1 className="font-bold text-base tracking-tight text-white flex items-center gap-2">
              AERO-CONTINUUM FVM
              <span className="text-[10px] font-mono font-medium px-2 py-0.5 rounded bg-blue-900/60 text-blue-300 border border-blue-700/50">
                v4.8-Enterprise
              </span>
            </h1>
            <p className="text-xs text-slate-400 font-sans hidden sm:block">
              Zero-Copy SoA Multiphysics FVM Solver &amp; Discrete Adjoint Engine
            </p>
          </div>
        </div>

        {/* Navigation Tabs */}
        <nav className="flex items-center overflow-x-auto space-x-1 p-1 bg-slate-950/60 rounded-xl border border-slate-800">
          <button
            onClick={() => setActiveTab('overview')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'overview'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <Cpu className="w-3.5 h-3.5" />
            <span>Data Lake &amp; SoA</span>
          </button>

          <button
            onClick={() => setActiveTab('fvm_rhiechow')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'fvm_rhiechow'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <Activity className="w-3.5 h-3.5" />
            <span>Rhie-Chow FVM</span>
          </button>

          <button
            onClick={() => setActiveTab('amg_mixed')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'amg_mixed'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <Layers className="w-3.5 h-3.5" />
            <span>Mixed-AMG Krylov</span>
          </button>

          <button
            onClick={() => setActiveTab('stiff_chem')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'stiff_chem'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <Flame className="w-3.5 h-3.5 text-amber-400" />
            <span>Stiff Chemistry DLB</span>
          </button>

          <button
            onClick={() => setActiveTab('discrete_adjoint')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'discrete_adjoint'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <GitCommit className="w-3.5 h-3.5 text-purple-400" />
            <span>Discrete Adjoint IFT</span>
          </button>

          <button
            onClick={() => setActiveTab('cpp_source')}
            className={`flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs font-medium transition-all cursor-pointer whitespace-nowrap ${
              activeTab === 'cpp_source'
                ? 'bg-blue-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/60'
            }`}
          >
            <Code2 className="w-3.5 h-3.5 text-emerald-400" />
            <span>C++20 / CUDA Code</span>
          </button>
        </nav>
      </div>
    </header>
  );
};
