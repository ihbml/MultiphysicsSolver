// Simulated nTop + Blender dark UI reference, rendered in pure SVG/CSS so it
// is viewable immediately in the design doc without external image assets.

export function nTopBlocksMockup() {
  const blocks = [
    { color: "#004AFD", label: "Import CAD", sub: "BladeRev1CFD.step" },
    { color: "#004AFD", label: "Patch Grouping", sub: "Blade / Hub / Shroud" },
    { color: "#B9F500", label: "Hexcore Mesher", sub: "Octree · BL N=8 Δy₁=1 µm" },
    { color: "#B9F500", label: "Quality Check", sub: "skewness < 0.6" },
    { color: "#723BFA", label: "Compressible Solver", sub: "RANS · k-ω SST" },
    { color: "#FEC40B", label: "Thermal Walls", sub: "Tw = 1250 K" },
    { color: "#FC75B4", label: "Species Transport", sub: "Fuel / Oxydizer" },
    { color: "#E52150", label: "Combustion", sub: "Eddy Dissipation" },
    { color: "#EF233C", label: "NaN Monitor", sub: "0 diverging cells" },
  ];

  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a1f] shadow-xl shadow-black/60">
      {/* titlebar */}
      <div className="flex items-center gap-2 border-b border-white/10 bg-[#0f0f12] px-3 py-1.5">
        <div className="flex gap-1.5">
          <div className="h-2.5 w-2.5 rounded-full bg-[#ff5f57]" />
          <div className="h-2.5 w-2.5 rounded-full bg-[#febc2e]" />
          <div className="h-2.5 w-2.5 rounded-full bg-[#28c840]" />
        </div>
        <div className="ml-3 font-mono text-[10px] text-zinc-500">
          nTop-style Block Stack · Physics Pipeline
        </div>
      </div>

      <div className="flex">
        {/* block list */}
        <div className="w-[280px] p-3 space-y-1.5">
          {blocks.map((b, i) => (
            <div
              key={i}
              className="group flex items-stretch overflow-hidden rounded-md border border-white/10 bg-[#24242b] shadow-sm shadow-black/30"
            >
              <div
                className="w-1"
                style={{ backgroundColor: b.color, boxShadow: `0 0 8px ${b.color}80` }}
              />
              <div className="flex-1 px-2.5 py-1.5">
                <div className="text-[11px] font-medium text-zinc-100">{b.label}</div>
                <div className="font-mono text-[9px] text-zinc-500">{b.sub}</div>
              </div>
              <div className="flex items-center gap-0.5 pr-1.5 text-zinc-600">
                <span className="text-[9px]">▸</span>
              </div>
            </div>
          ))}
        </div>

        {/* viewport mock – simulated MatCap blade */}
        <div className="relative flex-1 border-l border-white/10 bg-[#2a2a30]">
          <MatCapBlade />
          {/* axis tripod */}
          <div className="absolute left-3 bottom-3 font-mono text-[10px] text-zinc-400">
            <div className="flex items-center gap-1">
              <span className="text-[#ff6b6b]">●</span> x
              <span className="ml-2 text-[#69db7c]">●</span> y
              <span className="ml-2 text-[#74c0fc]">●</span> z
            </div>
          </div>
          {/* viewport label */}
          <div className="absolute right-3 top-3 rounded bg-black/50 px-2 py-0.5 font-mono text-[10px] text-zinc-300 backdrop-blur">
            MatCap · SS-Cavity · EDLC
          </div>
          <div className="absolute left-3 top-3 rounded bg-black/50 px-2 py-0.5 font-mono text-[10px] text-zinc-300 backdrop-blur">
            246 fps · 0.2 ms GPU
          </div>
        </div>
      </div>
    </div>
  );
}

export function MatCapBlade() {
  // stylised SVG blade shaded with a radial grey gradient to mimic a MatCap
  return (
    <svg viewBox="0 0 400 260" className="h-full w-full">
      <defs>
        <radialGradient id="matcap" cx="45%" cy="35%" r="80%">
          <stop offset="0%" stopColor="#e8e8ec" />
          <stop offset="35%" stopColor="#8f8f97" />
          <stop offset="70%" stopColor="#3a3a41" />
          <stop offset="100%" stopColor="#14141a" />
        </radialGradient>
        <radialGradient id="hot" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stopColor="#FBB539" stopOpacity="0.85" />
          <stop offset="60%" stopColor="#E85D04" stopOpacity="0.2" />
          <stop offset="100%" stopColor="#000" stopOpacity="0" />
        </radialGradient>
      </defs>

      {/* background grid */}
      <g stroke="#3a3a41" strokeWidth="0.5" opacity="0.4">
        {Array.from({ length: 20 }, (_, i) => (
          <line key={"v" + i} x1={i * 20} y1={0} x2={i * 20} y2={260} />
        ))}
        {Array.from({ length: 13 }, (_, i) => (
          <line key={"h" + i} x1={0} y1={i * 20} x2={400} y2={i * 20} />
        ))}
      </g>

      {/* blade (airfoil) shape */}
      <path
        d="M60,180 C90,80 220,30 350,60 C360,110 330,200 260,225 C200,240 110,230 60,180 Z"
        fill="url(#matcap)"
        stroke="#000"
        strokeWidth="1"
      />
      {/* hot overlay simulating T field on suction side */}
      <path
        d="M60,180 C90,80 220,30 350,60 C360,110 330,200 260,225 C200,240 110,230 60,180 Z"
        fill="url(#hot)"
      />
      {/* edge highlight */}
      <path
        d="M70,170 C100,85 225,40 345,65"
        fill="none"
        stroke="#ffffff"
        strokeOpacity="0.3"
        strokeWidth="1.2"
      />
    </svg>
  );
}

export function BlenderDarkMockup() {
  return (
    <div className="rounded-lg border border-white/10 bg-[#2b2b2b] shadow-xl shadow-black/60 text-[11px] text-zinc-200 font-sans">
      {/* top menu */}
      <div className="flex items-center gap-2 border-b border-black/40 bg-[#1e1e1e] px-2 py-1">
        {["File", "Edit", "Render", "Window", "Help"].map((m) => (
          <span key={m} className="px-2 py-0.5 hover:bg-white/10 cursor-default rounded-sm">
            {m}
          </span>
        ))}
        <div className="ml-auto flex gap-1">
          {["Layout", "Modeling", "Sculpting", "UV Editing"].map((t, i) => (
            <span
              key={t}
              className={
                "px-2 py-0.5 rounded-sm cursor-default " +
                (i === 0 ? "bg-[#3f3f3f]" : "hover:bg-white/10")
              }
            >
              {t}
            </span>
          ))}
        </div>
      </div>

      <div className="flex">
        {/* outliner */}
        <div className="w-48 border-r border-black/40 bg-[#252525] p-1 font-mono text-[10px] space-y-0.5">
          <div className="text-zinc-400 px-1">Outliner</div>
          {[
            { icon: "▾", name: "Collection", color: "" },
            { icon: "  ◆", name: "Blade", color: "#004AFD" },
            { icon: "  ◆", name: "Hub", color: "#004AFD" },
            { icon: "  ◆", name: "Shroud", color: "#004AFD" },
            { icon: "▸", name: "Mesh", color: "" },
            { icon: "▸", name: "Fields", color: "" },
            { icon: "  ∿", name: "p_rgh", color: "#723BFA" },
            { icon: "  ∿", name: "U", color: "#B9F500" },
            { icon: "  ∿", name: "T", color: "#E52150" },
            { icon: "▸", name: "Boundary", color: "" },
          ].map((r, i) => (
            <div key={i} className="flex gap-1 px-1 py-0.5 hover:bg-white/5">
              <span style={{ color: r.color || "#888" }}>{r.icon}</span>
              <span className="text-zinc-300">{r.name}</span>
            </div>
          ))}
        </div>

        {/* 3D viewport (same blade with colormap) */}
        <div className="flex-1">
          <div className="relative aspect-[16/9] w-full bg-[#3a3a41]">
            <ColormappedBlade />
            {/* timeline */}
            <div className="absolute inset-x-0 bottom-0 h-8 border-t border-black/40 bg-[#1e1e1e] px-2 py-1 font-mono text-[10px] text-zinc-400 flex items-center gap-2">
              <span className="text-zinc-200">▶</span>
              <span>⏮</span>
              <span className="tabular-nums">Frame: 1250 / 2000</span>
              <div className="flex-1 h-1 rounded bg-[#444] mx-2 relative">
                <div className="h-1 rounded bg-[#B9F500]" style={{ width: "62%" }} />
              </div>
              <span className="tabular-nums text-[#B9F500]">Converged</span>
            </div>
          </div>
        </div>

        {/* inspector */}
        <div className="w-60 border-l border-black/40 bg-[#252525] p-2 space-y-2">
          <div className="text-zinc-400 font-mono text-[10px]">Properties</div>
          {[
            { label: "Field", val: "T [K]", c: "#E52150" },
            { label: "Colormap", val: "Inferno" },
            { label: "Range", val: "300 — 1450" },
            { label: "Clip min", val: "auto" },
            { label: "Clip max", val: "1450 K" },
            { label: "Iso-Bands", val: "off" },
            { label: "NaN debug", val: "#B9F500", c: "#B9F500" },
          ].map((r, i) => (
            <div key={i} className="flex items-center justify-between rounded px-1.5 py-1 bg-[#2d2d2d]">
              <span className="text-zinc-500 text-[10px]">{r.label}</span>
              <span
                className="font-mono text-[10px] text-zinc-200"
                style={{ color: r.c || undefined }}
              >
                {r.val}
              </span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

export function ColormappedBlade() {
  // blade with an inferno-like gradient fill across it to simulate scalar field
  return (
    <svg viewBox="0 0 400 200" className="h-full w-full">
      <defs>
        <linearGradient id="inferno" x1="0" y1="0" x2="1" y2="0">
          <stop offset="0%" stopColor="#000004" />
          <stop offset="20%" stopColor="#3B1564" />
          <stop offset="40%" stopColor="#8C2981" />
          <stop offset="60%" stopColor="#DC4F49" />
          <stop offset="80%" stopColor="#F8B238" />
          <stop offset="100%" stopColor="#FCFFA4" />
        </linearGradient>
        <linearGradient id="mesh" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#fff" stopOpacity="0.0" />
          <stop offset="100%" stopColor="#fff" stopOpacity="0.08" />
        </linearGradient>
      </defs>
      {/* grid */}
      <g stroke="#50505a" strokeWidth="0.5" opacity="0.35">
        {Array.from({ length: 22 }, (_, i) => (
          <line key={"v" + i} x1={i * 19} y1={0} x2={i * 19} y2={200} />
        ))}
        {Array.from({ length: 11 }, (_, i) => (
          <line key={"h" + i} x1={0} y1={i * 19} x2={400} y2={i * 19} />
        ))}
      </g>
      <path
        d="M40,140 C70,60 200,20 340,45 C350,90 320,165 240,180 C180,190 90,180 40,140 Z"
        fill="url(#inferno)"
        stroke="#000"
        strokeWidth="0.8"
      />
      {/* mesh overlay */}
      <path
        d="M40,140 C70,60 200,20 340,45 C350,90 320,165 240,180 C180,190 90,180 40,140 Z"
        fill="url(#mesh)"
      />
      {/* contour lines */}
      {[0.2, 0.4, 0.6, 0.8].map((f) => (
        <path
          key={f}
          d={`M${50 + f * 80},${140 - f * 60} Q ${180},${110 - f * 20} ${330 - f * 30},${55 + f * 20}`}
          fill="none"
          stroke="#ffffff"
          strokeOpacity={0.15}
          strokeWidth={0.6}
        />
      ))}
    </svg>
  );
}
