import { useState } from 'react';
import { Navbar } from './components/Navbar';
import { DataLakeView } from './components/DataLakeView';
import { RhieChowFVMView } from './components/RhieChowFVMView';
import { MixedPrecisionAMGView } from './components/MixedPrecisionAMGView';
import { StiffChemistryDLBView } from './components/StiffChemistryDLBView';
import { DiscreteAdjointView } from './components/DiscreteAdjointView';
import { CppSourceView } from './components/CppSourceView';
import { VerificationSummaryModal } from './components/VerificationSummaryModal';
import { ActiveTab } from './types/solverTypes';
import { 
  CheckCircle2, 
  Zap 
} from 'lucide-react';

export default function App() {
  const [activeTab, setActiveTab] = useState<ActiveTab>('overview');
  const [isVerificationModalOpen, setIsVerificationModalOpen] = useState<boolean>(false);

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col font-sans selection:bg-blue-600 selection:text-white">
      {/* Top Application Navbar */}
      <Navbar
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        onOpenVerificationModal={() => setIsVerificationModalOpen(true)}
      />

      {/* Main Content Workspace */}
      <main className="flex-1 max-w-7xl w-full mx-auto p-4 sm:p-6 lg:p-8 space-y-8">
        {activeTab === 'overview' && <DataLakeView />}
        {activeTab === 'fvm_rhiechow' && <RhieChowFVMView />}
        {activeTab === 'amg_mixed' && <MixedPrecisionAMGView />}
        {activeTab === 'stiff_chem' && <StiffChemistryDLBView />}
        {activeTab === 'discrete_adjoint' && <DiscreteAdjointView />}
        {activeTab === 'cpp_source' && <CppSourceView />}
      </main>

      {/* Architectural Telemetry Footer */}
      <footer className="bg-slate-950 border-t border-slate-800/80 mt-auto py-6 px-4 sm:px-8 text-xs font-mono text-slate-400">
        <div className="max-w-7xl mx-auto flex flex-col md:flex-row items-center justify-between gap-4">
          <div className="flex items-center space-x-3">
            <div className="w-6 h-6 rounded bg-blue-600/20 border border-blue-500/40 flex items-center justify-center text-blue-400">
              <Zap className="w-3.5 h-3.5" />
            </div>
            <span>Aero-Continuum Multiphysics FVM &amp; Discrete Adjoint Engine</span>
            <span className="text-slate-600 hidden sm:inline">|</span>
            <span className="text-emerald-400 hidden sm:inline flex items-center gap-1">
              <CheckCircle2 className="w-3.5 h-3.5" /> C++20 Verified (<code className="text-slate-300">g++ -std=c++20 -O2</code>)
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-4 text-slate-500">
            <span>CUDA SM 9.0 (Hopper)</span>
            <span>&bull;</span>
            <span>OpenMP 5.2</span>
            <span>&bull;</span>
            <span>LLVM Enzyme Autodiff</span>
            <span>&bull;</span>
            <button
              onClick={() => setIsVerificationModalOpen(true)}
              className="text-cyan-400 hover:text-cyan-300 transition-colors cursor-pointer"
            >
              View Verification Certificate &rarr;
            </button>
          </div>
        </div>
      </footer>

      {/* Verification Modal */}
      <VerificationSummaryModal
        isOpen={isVerificationModalOpen}
        onClose={() => setIsVerificationModalOpen(false)}
      />
    </div>
  );
}
