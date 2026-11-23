import { WORKSPACES } from "../schema/registry";
import { useStore } from "../state/store";

export function TopBar() {
  const ws = useStore((s) => s.ws);
  const setWs = useStore((s) => s.setWs);
  const rev = useStore((s) => s.revisions);
  const ready = useStore((s) => s.backendReady);
  const boot = useStore((s) => s.bootLine);
  const consoleOpen = useStore((s) => s.consoleOpen);
  const setConsoleOpen = useStore((s) => s.setConsoleOpen);
  const alert = useStore((s) => s.alert);
  const solver = useStore((s) => s.solver);

  return (
    <div className="flex items-stretch h-[46px] bg-[#0D0E12] border-b border-[#26272e] select-none shrink-0">
      <div className="flex items-center gap-2.5 px-4 border-r border-[#26272e]">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none">
          <path d="M12 2 21 7v10l-9 5-9-5V7l9-5Z" stroke="#004AFD" strokeWidth="1.6" fill="rgba(0,74,253,0.12)" />
          <path d="M7 14c2.2 0 2.2-4.5 4.4-4.5S13.6 14 15.8 14" stroke="#B9F500" strokeWidth="1.5" fill="none" />
        </svg>
        <div className="leading-tight">
          <div className="text-[13px] font-700 font-semibold tracking-wide">
            HYDRO<span className="text-[#004AFD]">CORE</span>
          </div>
          <div className="text-[9px] text-[#63666f] mono tracking-wider -mt-0.5">MULTIPHYSICS CFD COCKPIT</div>
        </div>
        <div className="mono text-[9px] text-[#63666f] ml-2 pt-1">v0.9.1</div>
      </div>

      <div className="flex items-stretch flex-1 overflow-x-auto">
        {WORKSPACES.map((w) => {
          const active = ws === w.id;
          return (
            <button
              key={w.id}
              onClick={() => setWs(w.id as 1 | 2 | 3 | 4 | 5)}
              className={`relative flex items-center gap-2 px-4 text-[12px] whitespace-nowrap transition-colors ${
                active ? "bg-[#18181C] text-[#E8E9ED]" : "text-[#9DA0AA] hover:bg-[#141519] hover:text-[#C6C8CF]"
              }`}
            >
              <span
                className="mono text-[10px] px-1.5 py-0.5 rounded-sm"
                style={{
                  color: active ? "#111217" : w.accent,
                  background: active ? w.accent : "transparent",
                  border: active ? "none" : `1px solid ${w.accent}44`,
                }}
              >
                {w.id}
              </span>
              {w.name}
              {active && (
                <span className="absolute left-0 right-0 bottom-0 h-[2px]" style={{ background: w.accent }} />
              )}
            </button>
          );
        })}
      </div>

      <div className="flex items-center gap-3 px-4 border-l border-[#26272e]">
        <div className="mono text-[10px] text-[#9DA0AA] hidden lg:flex items-center gap-1.5">
          <span className="text-[#63666f]">geo</span>
          <span className="px-1.5 py-px rounded-sm bg-[#1E1E24] border border-[#32333C] text-[#B9F500]">#{rev.geo}</span>
          <span className="text-[#63666f] ml-1">mesh</span>
          <span className="px-1.5 py-px rounded-sm bg-[#1E1E24] border border-[#32333C] text-[#B9F500]">#{rev.mesh}</span>
        </div>
        <button
          onClick={() => setConsoleOpen(!consoleOpen)}
          className={`mono text-[10px] px-2 py-1 rounded-sm border transition-colors ${
            alert
              ? "border-[#EF233C] text-[#EF233C]"
              : solver === "running"
                ? "border-[#723BFA] text-[#B79CFF]"
                : "border-[#32333C] text-[#9DA0AA] hover:border-[#4A4B55]"
          }`}
          title="NDJSON console"
        >
          {solver === "running" ? (
            <span className="flex items-center gap-1.5">
              <span className="w-1.5 h-1.5 rounded-full bg-[#723BFA] blink" />
              SOLVE
            </span>
          ) : alert ? (
            <span className="flex items-center gap-1.5">
              <span className="w-1.5 h-1.5 rounded-full bg-[#EF233C] blink" />
              NaN ALERT
            </span>
          ) : (
            "NDJSON"
          )}
        </button>
        <button
          onClick={() => setConsoleOpen(!consoleOpen)}
          className={`flex items-center gap-1.5 text-[10px] mono px-2 py-1 rounded-sm border ${
            ready ? "border-[#2E4A1E] text-[#B9F500]" : "border-[#32333C] text-[#9DA0AA]"
          }`}
          title="backend status"
        >
          <span
            className={`w-1.5 h-1.5 rounded-full ${ready ? "bg-[#B9F500]" : "bg-[#FEC40B] blink"}`}
          />
          {ready ? "backend · stdin/stdout" : boot}
        </button>
      </div>
    </div>
  );
}
