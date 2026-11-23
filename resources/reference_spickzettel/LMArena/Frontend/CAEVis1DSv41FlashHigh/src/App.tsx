import { useEffect } from "react";
import { CockpitProvider, useCockpit, Workspace } from "./store";
import { HelpModal, LeftRail, StatusBar, TabBar, Toast, TopBar } from "./components/Chrome";
import Viewport3D from "./components/Viewport3D";
import { LeftRules, RightRules } from "./components/RulePanels";
import { LeftDomain, RightDomain } from "./components/DomainPanels";
import { LeftProbe, ProbeDock, RightProbe } from "./components/ProbePanels";
import { CompareDock, LeftCompare, RightCompare } from "./components/ComparePanels";
import { CalculatorCenter, LeftCalc, RightCalc } from "./components/Calculator";
import { cn } from "./components/ui";

const WORKSPACE_KEYS: Record<string, Workspace> = {
  "1": "rules", "2": "domain", "3": "probe", "4": "compare", "5": "calc",
};

function Shell() {
  const c = useCockpit();

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT")) {
        if (e.key === "Escape") (t as HTMLInputElement).blur();
        return;
      }
      if (e.altKey && WORKSPACE_KEYS[e.key]) {
        e.preventDefault();
        c.setWorkspace(WORKSPACE_KEYS[e.key]);
      } else if (e.key === "?") {
        c.setShowHelp(true);
      } else if (e.key === "Escape") {
        c.setShowHelp(false);
      } else if (e.key.toLowerCase() === "h" && !e.metaKey && !e.ctrlKey) {
        c.setHideUi(!c.hideUi);
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [c]);

  const left = c.workspace === "rules" ? <LeftRules /> : c.workspace === "domain" ? <LeftDomain /> : c.workspace === "probe" ? <LeftProbe /> : c.workspace === "compare" ? <LeftCompare /> : <LeftCalc />;
  const right = c.workspace === "rules" ? <RightRules /> : c.workspace === "domain" ? <RightDomain /> : c.workspace === "probe" ? <RightProbe /> : c.workspace === "compare" ? <RightCompare /> : <RightCalc />;

  return (
    <div className="flex h-full w-full flex-col overflow-hidden bg-[#0e0f13] text-[#e9ebf0]">
      <TopBar />
      <TabBar />
      <div className="flex min-h-0 flex-1">
        {!c.hideUi && <LeftRail />}
        {!c.hideUi && (
          <aside className="hidden w-[266px] shrink-0 flex-col border-r border-[#24252c] lg:flex xl:w-[272px] min-h-0">
            {left}
          </aside>
        )}

        <main className="flex min-h-0 min-w-0 flex-1 flex-col">
          {c.workspace === "calc" ? (
            <div className="flex min-h-0 flex-1 flex-col md:flex-row">
              <div className="flex min-h-0 min-w-0 flex-1 flex-col border-b border-[#2a2b33] md:border-b-0 md:border-r">
                <CalculatorCenter />
              </div>
              <div className="relative h-[42%] min-h-[220px] w-full shrink-0 md:h-auto md:min-h-0 md:w-[42%] md:max-w-[520px]">
                <Viewport3D />
              </div>
            </div>
          ) : (
            <div className="flex min-h-0 flex-1 flex-col">
              <div className={cn("relative min-h-0", c.workspace === "probe" ? "h-[41%] min-h-[230px]" : c.workspace === "compare" ? "h-[55%] min-h-[300px]" : "flex-1")}>
                <Viewport3D />
              </div>
              {c.workspace === "probe" && (
                <div className="h-[59%] min-h-0">
                  <ProbeDock />
                </div>
              )}
              {c.workspace === "compare" && (
                <div className="h-[45%] min-h-0">
                  <CompareDock />
                </div>
              )}
            </div>
          )}
        </main>

        {!c.hideUi && (
          <aside className="hidden w-[306px] shrink-0 flex-col border-l border-[#24252c] xl:flex min-h-0">
            {right}
          </aside>
        )}
      </div>
      <StatusBar />
      <Toast />
      <HelpModal />
      {c.hideUi && (
        <button
          onClick={() => c.setHideUi(false)}
          className="fixed right-3 top-[96px] z-40 rounded-[4px] border border-[#33415c] bg-[#131a28] px-2 py-1 text-[9px] text-[#9fbcff]"
        >
          show panels (H)
        </button>
      )}
    </div>
  );
}

export default function App() {
  return (
    <CockpitProvider>
      <Shell />
    </CockpitProvider>
  );
}
