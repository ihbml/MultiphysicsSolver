import { useState } from "react";
import VerificationTab from "./tabs/VerificationTab";
import TurbulenceTab from "./tabs/TurbulenceTab";
import WallTab from "./tabs/WallTab";
import CHTTab from "./tabs/CHTTab";
import PorousTab from "./tabs/PorousTab";
import RadiationTab from "./tabs/RadiationTab";

const TABS = [
  { id: "verify", label: "Verification", component: VerificationTab, tag: "C++20 · 36 checks" },
  { id: "turb", label: "k-ω SST & WALE", component: TurbulenceTab, tag: "Positivity-preserving" },
  { id: "wall", label: "All-y+ wall", component: WallTab, tag: "0.1 ≤ y+ ≤ 500" },
  { id: "cht", label: "Conjugate heat transfer", component: CHTTab, tag: "Harmonic twin faces" },
  { id: "porous", label: "Porous media", component: PorousTab, tag: "Anisotropic Darcy-Forchheimer" },
  { id: "rad", label: "P1 radiation", component: RadiationTab, tag: "Marshak walls" },
] as const;

export default function App() {
  const [active, setActive] = useState<(typeof TABS)[number]["id"]>("verify");
  const current = TABS.find((t) => t.id === active) ?? TABS[0];
  const Body = current.component;

  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-50 via-white to-indigo-50 text-slate-900">
      <header className="border-b border-slate-200 bg-white/80 backdrop-blur">
        <div className="mx-auto flex max-w-7xl flex-col gap-4 px-6 py-5 md:flex-row md:items-center md:justify-between">
          <div className="flex items-center gap-3">
            <div className="flex h-11 w-11 items-center justify-center rounded-xl bg-gradient-to-br from-indigo-500 to-violet-600 text-lg font-bold text-white shadow-lg shadow-indigo-200">
              A
            </div>
            <div>
              <h1 className="text-xl font-semibold tracking-tight">Cluster A · Turbulence, Wall, CHT, Porous & Radiation</h1>
              <p className="text-sm text-slate-500">Engineering verification workbench for the Multiphysics Suite</p>
            </div>
          </div>
          <nav className="flex flex-wrap gap-2">
            {TABS.map((t) => (
              <button
                key={t.id}
                onClick={() => setActive(t.id)}
                className={`rounded-xl px-3 py-2 text-left text-sm transition ${
                  active === t.id
                    ? "bg-indigo-600 text-white shadow-md shadow-indigo-200"
                    : "bg-slate-100 text-slate-700 hover:bg-slate-200"
                }`}
              >
                <div className="font-semibold">{t.label}</div>
                <div className={`text-[11px] ${active === t.id ? "text-indigo-100" : "text-slate-500"}`}>{t.tag}</div>
              </button>
            ))}
          </nav>
        </div>
      </header>

      <main className="mx-auto max-w-7xl px-6 py-8">
        <Body />
        <footer className="mt-10 border-t border-slate-200 pt-4 text-xs text-slate-500">
          C++ reference: <code>cpp/cluster_a.hpp</code> (header-only, CUDA-ready via <code>CA_HD</code>), verified by{" "}
          <code>cpp/verify.cpp</code> with <code>g++ -std=c++20 -O2 -Wall -Wextra -pedantic</code>. This workbench mirrors the same
          equations in <code>src/lib/physics.ts</code>.
        </footer>
      </main>
    </div>
  );
}
