import { useEffect, useRef, useState } from 'react';
import {
  Flame,
  Radio,
  Magnet,
  Shield,
  Play,
  Pause,
  StepForward,
  RotateCcw,
  Code2,
  Cpu,
  CheckCircle2,
  Database,
} from 'lucide-react';
import {
  BatteryPackSimulation,
  Farassat1AFWHSimulation,
  InductionlessMHDSimulation,
  TPSAblationALESimulation,
} from './solvers/clusterCKernels';
import { ModuleC1BatteryView } from './components/ModuleC1BatteryView';
import { ModuleC2AeroacousticsView } from './components/ModuleC2AeroacousticsView';
import { ModuleC3MHDView } from './components/ModuleC3MHDView';
import { ModuleC4TPSAblationView } from './components/ModuleC4TPSAblationView';
import { CppSourceDrawer } from './components/CppSourceDrawer';
import { CppSectionBookmark } from './data/cppSourceCode';

type ClusterModuleId = 'c1' | 'c2' | 'c3' | 'c4';

export function App() {
  const [activeModule, setActiveModule] = useState<ClusterModuleId>('c1');
  const [isRunning, setIsRunning] = useState<boolean>(true);
  const [tickCount, setTickCount] = useState<number>(0);
  const [drawerOpen, setDrawerOpen] = useState<boolean>(false);
  const [drawerSection, setDrawerSection] =
    useState<CppSectionBookmark['id']>('all');

  // Persistent SoA simulation instances
  const batterySimRef = useRef<BatteryPackSimulation | null>(null);
  const fwhSimRef = useRef<Farassat1AFWHSimulation | null>(null);
  const mhdSimRef = useRef<InductionlessMHDSimulation | null>(null);
  const tpsSimRef = useRef<TPSAblationALESimulation | null>(null);

  if (!batterySimRef.current) {
    const bSim = new BatteryPackSimulation();
    for (let i = 0; i < 45; i++) bSim.step(0.12);
    batterySimRef.current = bSim;
  }
  if (!fwhSimRef.current) {
    fwhSimRef.current = new Farassat1AFWHSimulation();
  }
  if (!mhdSimRef.current) {
    mhdSimRef.current = new InductionlessMHDSimulation();
  }
  if (!tpsSimRef.current) {
    const tSim = new TPSAblationALESimulation();
    for (let i = 0; i < 45; i++) tSim.step(0.06);
    tpsSimRef.current = tSim;
  }

  const forceUpdate = () => setTickCount((c) => c + 1);

  // Real-time simulation loop
  useEffect(() => {
    if (!isRunning) return;
    const interval = setInterval(() => {
      if (activeModule === 'c1' && batterySimRef.current) {
        batterySimRef.current.step(0.12);
      } else if (activeModule === 'c2' && fwhSimRef.current) {
        fwhSimRef.current.phaseRad =
          (fwhSimRef.current.phaseRad + 0.14) % (2.0 * Math.PI);
      } else if (activeModule === 'c4' && tpsSimRef.current) {
        tpsSimRef.current.step(0.06);
      }
      setTickCount((c) => c + 1);
    }, 90);

    return () => clearInterval(interval);
  }, [isRunning, activeModule]);

  const handleSingleStep = () => {
    if (activeModule === 'c1' && batterySimRef.current) {
      batterySimRef.current.step(0.15);
    } else if (activeModule === 'c2' && fwhSimRef.current) {
      fwhSimRef.current.phaseRad += 0.22;
    } else if (activeModule === 'c4' && tpsSimRef.current) {
      tpsSimRef.current.step(0.08);
    }
    forceUpdate();
  };

  const handleResetActive = () => {
    if (activeModule === 'c1' && batterySimRef.current) {
      batterySimRef.current.reset(true);
    } else if (activeModule === 'c2' && fwhSimRef.current) {
      fwhSimRef.current.tipMach = 0.62;
      fwhSimRef.current.bpfHz = 160.0;
      fwhSimRef.current.bladeCount = 4;
      fwhSimRef.current.loadingAmplitudePa = 520.0;
      fwhSimRef.current.phaseRad = 0;
    } else if (activeModule === 'c3' && mhdSimRef.current) {
      mhdSimRef.current.Ha = 25.0;
      mhdSimRef.current.meshSkewFactor = 0.14;
      mhdSimRef.current.sigma_S_m = 1.0e6;
    } else if (activeModule === 'c4' && tpsSimRef.current) {
      tpsSimRef.current.reset();
    }
    forceUpdate();
  };

  const openCodeSection = (section: CppSectionBookmark['id']) => {
    setDrawerSection(section);
    setDrawerOpen(true);
  };

  const modules = [
    {
      id: 'c1' as const,
      code: 'C1',
      title: '2-RC Battery & Thermal Runaway',
      subtitle: 'Hatchard/Spotnitz 4-Stage + Rosenbrock-W',
      icon: Flame,
      accent: '#F97316',
      borderActive: 'border-orange-500/60 bg-orange-500/15 text-orange-300',
    },
    {
      id: 'c2' as const,
      code: 'C2',
      title: 'Farassat 1A FWH Aeroacoustics',
      subtitle: 'Retarded-Time Integral + Radix-2 Hann FFT',
      icon: Radio,
      accent: '#06B6D4',
      borderActive: 'border-cyan-500/60 bg-cyan-500/15 text-cyan-300',
    },
    {
      id: 'c3' as const,
      code: 'C3',
      title: 'Conservative Inductionless MHD',
      subtitle: 'Ni et al. 4-Step Projection + Hartmann Exact',
      icon: Magnet,
      accent: '#8B5CF6',
      borderActive: 'border-violet-500/60 bg-violet-500/15 text-violet-300',
    },
    {
      id: 'c4' as const,
      code: 'C4',
      title: 'TPS Ablation & BDF2-DGCL ALE',
      subtitle: '3-Comp Pyrolysis + Surface Recession',
      icon: Shield,
      accent: '#EC4899',
      borderActive: 'border-pink-500/60 bg-pink-500/15 text-pink-300',
    },
  ];

  return (
    <div className="min-h-screen flex flex-col bg-[#070A0F] text-[#F1F5F9] bg-telemetry-grid">
      {/* TOP COMMAND & MODULE NAVIGATION BAR */}
      <header className="sticky top-0 z-30 border-b border-[#1E293B] bg-[#0D121B]/95 backdrop-blur-md">
        <div className="mx-auto flex max-w-[1680px] flex-wrap items-center justify-between gap-3 px-4 py-2.5">
          {/* Left Branding */}
          <div className="flex items-center gap-3">
            <div className="flex h-9 w-9 items-center justify-center rounded-lg border border-cyan-500/40 bg-cyan-500/10 text-cyan-400 shadow-inner">
              <Cpu className="h-5 w-5" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <span className="font-display text-sm font-bold tracking-wider uppercase text-white">
                  CLUSTER C // MULTIPHYSICS SOLVER SUITE
                </span>
                <span className="hidden sm:inline-flex items-center gap-1 rounded border border-emerald-500/30 bg-emerald-500/10 px-1.5 py-0.5 font-mono-tabular text-[10px] font-semibold text-emerald-400">
                  <CheckCircle2 className="h-3 w-3" />
                  g++ -std=c++20 -O2
                </span>
              </div>
              <div className="font-mono-tabular text-[11px] text-[#94A3B8]">
                SoA <code className="text-cyan-300">CAbiSpan&lt;T&gt;</code> Data Lake •
                Over-Relaxed Polyhedral FVM Topology (α_f, k_f^corr)
              </div>
            </div>
          </div>

          {/* Center 4-Module Segmented Switcher */}
          <nav className="flex flex-wrap items-center gap-1.5">
            {modules.map((m) => {
              const Icon = m.icon;
              const isActive = activeModule === m.id;
              return (
                <button
                  key={m.id}
                  onClick={() => setActiveModule(m.id)}
                  className={`flex items-center gap-2 rounded-md border px-3 py-1.5 text-left transition ${
                    isActive
                      ? m.borderActive
                      : 'border-[#1E293B] bg-[#131B28]/80 text-[#94A3B8] hover:border-[#334155] hover:text-slate-200'
                  }`}
                >
                  <Icon
                    className="h-4 w-4 shrink-0"
                    style={{ color: isActive ? m.accent : undefined }}
                  />
                  <div>
                    <div className="font-display text-xs font-bold leading-tight">
                      [{m.code}] {m.title}
                    </div>
                    <div className="hidden xl:block font-mono-tabular text-[9px] opacity-80">
                      {m.subtitle}
                    </div>
                  </div>
                </button>
              );
            })}
          </nav>

          {/* Right Solver Execution Controls & C++20 Code Inspector Trigger */}
          <div className="flex items-center gap-1.5">
            <button
              onClick={() => setIsRunning((r) => !r)}
              className={`flex items-center gap-1.5 rounded border px-2.5 py-1.5 font-mono-tabular text-xs font-semibold transition ${
                isRunning
                  ? 'border-amber-500/40 bg-amber-500/15 text-amber-300 hover:bg-amber-500/25'
                  : 'border-emerald-500/40 bg-emerald-500/15 text-emerald-300 hover:bg-emerald-500/25'
              }`}
            >
              {isRunning ? (
                <>
                  <Pause className="h-3.5 w-3.5" />
                  <span>Pause</span>
                </>
              ) : (
                <>
                  <Play className="h-3.5 w-3.5" />
                  <span>Run</span>
                </>
              )}
            </button>

            <button
              onClick={handleSingleStep}
              disabled={isRunning}
              className="flex items-center gap-1 rounded border border-[#1E293B] bg-[#131B28] px-2.5 py-1.5 font-mono-tabular text-xs font-medium text-slate-200 transition hover:border-slate-500 disabled:opacity-40"
              title="Advance Single Time Step"
            >
              <StepForward className="h-3.5 w-3.5" />
              <span className="hidden sm:inline">Step</span>
            </button>

            <button
              onClick={handleResetActive}
              className="flex items-center gap-1 rounded border border-[#1E293B] bg-[#131B28] px-2.5 py-1.5 font-mono-tabular text-xs font-medium text-slate-200 transition hover:border-slate-500"
              title="Reset Active Solver State"
            >
              <RotateCcw className="h-3.5 w-3.5" />
              <span className="hidden sm:inline">Reset</span>
            </button>

            <button
              onClick={() => openCodeSection(activeModule)}
              className="ml-1 flex items-center gap-1.5 rounded border border-cyan-500/50 bg-cyan-500/15 px-3 py-1.5 font-mono-tabular text-xs font-semibold text-cyan-300 shadow-sm transition hover:bg-cyan-500/25"
            >
              <Code2 className="h-4 w-4" />
              <span>C++20 Source & Verify</span>
            </button>
          </div>
        </div>
      </header>

      {/* MAIN WORKBENCH VIEWPORT */}
      <main className="mx-auto w-full max-w-[1680px] flex-1 px-4 py-4">
        {activeModule === 'c1' && batterySimRef.current && (
          <ModuleC1BatteryView
            sim={batterySimRef.current}
            tickCount={tickCount}
            onForceUpdate={forceUpdate}
            onOpenCppSection={() => openCodeSection('c1')}
          />
        )}

        {activeModule === 'c2' && fwhSimRef.current && (
          <ModuleC2AeroacousticsView
            sim={fwhSimRef.current}
            tickCount={tickCount}
            onForceUpdate={forceUpdate}
            onOpenCppSection={() => openCodeSection('c2')}
          />
        )}

        {activeModule === 'c3' && mhdSimRef.current && (
          <ModuleC3MHDView
            sim={mhdSimRef.current}
            tickCount={tickCount}
            onForceUpdate={forceUpdate}
            onOpenCppSection={() => openCodeSection('c3')}
          />
        )}

        {activeModule === 'c4' && tpsSimRef.current && (
          <ModuleC4TPSAblationView
            sim={tpsSimRef.current}
            tickCount={tickCount}
            onForceUpdate={forceUpdate}
            onOpenCppSection={() => openCodeSection('c4')}
          />
        )}
      </main>

      {/* BOTTOM SOA CABISPAN & POLYHEDRAL FVM TELEMETRY STATUS BAR */}
      <footer className="border-t border-[#1E293B] bg-[#0D121B] px-4 py-2">
        <div className="mx-auto flex max-w-[1680px] flex-wrap items-center justify-between gap-2 font-mono-tabular text-[11px] text-[#94A3B8]">
          <div className="flex flex-wrap items-center gap-4">
            <span className="flex items-center gap-1.5 text-slate-300">
              <Database className="h-3.5 w-3.5 text-cyan-400" />
              <span>SoA Memory Layout:</span>
              <code className="text-cyan-300">CAbiSpan&lt;double&gt;</code> (Zero-Copy
              64-bit Contiguous Spans)
            </span>
            <span>
              Non-Orthogonal Laplacian:{' '}
              <code className="text-emerald-300">
                α_f = (S_f·S_f)/(S_f·d_PN), k_f^corr = S_f - α_f d_PN
              </code>
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <button
              onClick={() => openCodeSection('cabispan')}
              className="rounded border border-[#1E293B] bg-[#131B28] px-2 py-0.5 text-[10px] text-slate-300 hover:border-cyan-500/40 hover:text-cyan-300"
            >
              Inspect CAbiSpan & FVM
            </button>
            <button
              onClick={() => openCodeSection('verify')}
              className="rounded border border-emerald-500/30 bg-emerald-500/10 px-2 py-0.5 text-[10px] text-emerald-300 hover:bg-emerald-500/20"
            >
              View g++ -std=c++20 Verification Harness
            </button>
          </div>
        </div>
      </footer>

      {/* SLIDE-OVER C++20 SOURCE & VERIFICATION DRAWER */}
      <CppSourceDrawer
        isOpen={drawerOpen}
        onClose={() => setDrawerOpen(false)}
        initialSection={drawerSection}
      />
    </div>
  );
}
export default App;
