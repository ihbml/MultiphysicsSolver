import { useState } from "react";
import { COLORMAPS, TRIADS, CATEGORY_COLORS } from "./colormaps";
import { ColormapBar } from "./components/ColormapBar";
import { PaletteTriad } from "./components/PaletteTriad";
import {
  nTopBlocksMockup,
  BlenderDarkMockup,
} from "./components/UIMockup";

type TabId =
  | "overview"
  | "palettes"
  | "colormaps"
  | "ui-reference"
  | "architecture"
  | "gpu-rules"
  | "backend-api";

const TABS: { id: TabId; label: string; hint?: string }[] = [
  { id: "overview",     label: "00 · Übersicht" },
  { id: "palettes",     label: "01 · Farbpaletten" },
  { id: "colormaps",    label: "02 · CFD-Colormaps" },
  { id: "ui-reference", label: "03 · UI-Referenzen" },
  { id: "architecture", label: "04 · Zero-Color-Interference" },
  { id: "gpu-rules",    label: "05 · GPU-Regeln" },
  { id: "backend-api",  label: "06 · Backend-API" },
];

export default function App() {
  const [tab, setTab] = useState<TabId>("overview");
  const [bandMode, setBandMode] = useState<"continuous" | 10 | 16 | 24>(
    "continuous",
  );

  return (
    <div
      className="min-h-screen w-full text-zinc-200"
      style={{
        background:
          "radial-gradient(ellipse at top, #1d1d25 0%, #111217 55%, #0a0a0d 100%)",
      }}
    >
      {/* Top header */}
      <header className="sticky top-0 z-20 border-b border-white/10 bg-[#111217]/80 backdrop-blur">
        <div className="mx-auto flex max-w-[1400px] items-center justify-between px-6 py-3">
          <div className="flex items-center gap-3">
            <div className="relative h-9 w-9 rounded-lg overflow-hidden border border-white/20 bg-black">
              <div
                className="absolute inset-0"
                style={{
                  background:
                    "linear-gradient(135deg, #111217 0%, #18181C 40%, #723BFA 70%, #B9F500 100%)",
                }}
              />
            </div>
            <div>
              <div className="font-mono text-[10px] uppercase tracking-[0.2em] text-zinc-500">
                Design-Referenz · v0.2.0
              </div>
              <div className="text-base font-semibold text-zinc-100">
                CFD-Cockpit <span className="text-[#B9F500]">Rust + wgpu + egui</span>
              </div>
            </div>
          </div>
          <div className="hidden md:flex items-center gap-2 font-mono text-[11px] text-zinc-500">
            <span className="rounded border border-white/10 bg-black/30 px-2 py-1">
              DX12 / Vulkan
            </span>
            <span className="rounded border border-white/10 bg-black/30 px-2 py-1">
              Reverse-Z
            </span>
            <span className="rounded border border-[#B9F500]/40 bg-[#B9F500]/10 px-2 py-1 text-[#B9F500]">
              Option C
            </span>
          </div>
        </div>

        {/* Tab nav */}
        <nav className="mx-auto flex max-w-[1400px] flex-wrap gap-1 px-4 pb-2">
          {TABS.map((t) => (
            <button
              key={t.id}
              onClick={() => setTab(t.id)}
              className={
                "rounded-t-md px-3 py-1.5 font-mono text-[11px] transition-colors " +
                (tab === t.id
                  ? "bg-[#18181C] text-zinc-100 border border-white/10 border-b-transparent"
                  : "text-zinc-500 hover:text-zinc-300 hover:bg-white/5")
              }
            >
              {t.label}
            </button>
          ))}
        </nav>
      </header>

      <main className="mx-auto max-w-[1400px] px-6 py-8 space-y-10">
        {tab === "overview" && <Overview setTab={setTab} />}
        {tab === "palettes" && <Palettes />}
        {tab === "colormaps" && (
          <Colormaps bandMode={bandMode} setBandMode={setBandMode} />
        )}
        {tab === "ui-reference" && <UIReference />}
        {tab === "architecture" && <Architecture />}
        {tab === "gpu-rules" && <GPURules />}
        {tab === "backend-api" && <BackendAPI />}
      </main>

      <footer className="border-t border-white/10 bg-black/30">
        <div className="mx-auto max-w-[1400px] px-6 py-6 font-mono text-[11px] text-zinc-600 flex flex-wrap justify-between gap-4">
          <span>
            DESIGN_REFERENCE.md · palettes.json ·{" "}
            <span className="text-zinc-400">Frontend-Live-Version</span>
          </span>
          <span>
            #F0E5DB · #474326 · #FC75B4 &nbsp;|&nbsp; #111217 · #DFE5EB · #EF233C
            &nbsp;|&nbsp; #18181C · #723BFA · #B9F500
          </span>
        </div>
      </footer>
    </div>
  );
}

// =============================================================================
// Section: Overview
// =============================================================================
function Overview({ setTab }: { setTab: (t: TabId) => void }) {
  return (
    <section className="space-y-8">
      <div className="rounded-2xl border border-white/10 bg-black/30 p-8 shadow-xl shadow-black/50">
        <div className="flex items-center gap-2 font-mono text-[11px] text-[#B9F500]">
          ▶ Option C  · bestätigt
        </div>
        <h1 className="mt-2 text-4xl font-bold tracking-tight text-zinc-50 md:text-5xl">
          Natives CFD-Cockpit für schaufelgroße Datensätze.
        </h1>
        <p className="mt-4 max-w-3xl text-lg leading-relaxed text-zinc-400">
          Ein <span className="text-zinc-100">Rust + wgpu</span>-Desktop-Client
          mit <span className="text-zinc-100">egui</span> als UI-Layer –
          ZieldirectX12 auf Windows, Vulkan auf Linux, DX12 als Primärziel für
          Gasturbinen-CFD. Keine Chromium- oder Electron-Overhead-Schicht,
          keine Browser-Limitierungen bei großen Gitterdatensätzen.
        </p>

        <div className="mt-6 flex flex-wrap gap-2 font-mono text-[11px]">
          {[
            "wgpu 24",
            "egui 0.29 + egui_dock + egui_plot",
            "glam",
            "bytemuck",
            "serde_json",
            "foam_fast_io (rlib)",
          ].map((c) => (
            <span
              key={c}
              className="rounded border border-white/10 bg-white/5 px-2 py-1 text-zinc-300"
            >
              {c}
            </span>
          ))}
        </div>
      </div>

      <div className="grid md:grid-cols-3 gap-4">
        {[
          {
            num: "01",
            title: "Zero Color Interference",
            body: "Dunkles UI-Chassis (T3 Obsidian + T4 Carbon) überstrahlt niemals die CFD-Skalen. Akzentfarben nur für die Kategorisierung der nTop-Blöcke.",
            tab: "architecture" as TabId,
            accent: "#B9F500",
          },
          {
            num: "02",
            title: "Perzeptive Colormaps",
            body: "Inferno, Ampel (rot-grün-blind-sicher), Moreland Cool-Warm, Turbo, Aero, Dark Ember, Schlieren – alle mit Oklab/CIELAB-Interpolation.",
            tab: "colormaps" as TabId,
            accent: "#FC75B4",
          },
          {
            num: "03",
            title: "Reverse-Z + f64→f32",
            body: "Kamera-relative Koordinaten, Depth32Float-Reverse-Z, Zero-Copy-Gitterpuffer. 1µm Grenzschicht-Zellen ohne Z-Fighting.",
            tab: "gpu-rules" as TabId,
            accent: "#004AFD",
          },
        ].map((card) => (
          <button
            key={card.num}
            onClick={() => setTab(card.tab)}
            className="group rounded-xl border border-white/10 bg-black/30 p-5 text-left transition hover:border-white/20 hover:bg-black/40"
          >
            <div
              className="font-mono text-5xl font-bold"
              style={{ color: card.accent }}
            >
              {card.num}
            </div>
            <h3 className="mt-4 text-lg font-semibold text-zinc-100">
              {card.title}
            </h3>
            <p className="mt-2 text-sm leading-relaxed text-zinc-400">
              {card.body}
            </p>
            <div className="mt-4 font-mono text-[10px] uppercase tracking-widest text-zinc-500 group-hover:text-zinc-300">
              Kapitel öffnen →
            </div>
          </button>
        ))}
      </div>

      <div className="rounded-2xl border border-white/10 bg-gradient-to-br from-[#723BFA]/10 via-transparent to-[#B9F500]/10 p-6">
        <h2 className="text-xl font-semibold text-zinc-100">
          Hinweis zur Normierung
        </h2>
        <p className="mt-2 text-sm text-zinc-400">
          Die ursprüngliche Angabe <code className="font-mono text-zinc-300">#11217</code> hatte
          5 Hex-Stellen und wurde als{" "}
          <code className="font-mono text-[#B9F500]">#111217 · Deep Obsidian</code>{" "}
          normiert (T3 Background). Diese Korrektur ist in der Paletten-Tabelle
          und der JSON-Spezifikation hinterlegt.
        </p>
      </div>
    </section>
  );
}

// =============================================================================
// Section: Paletten (Triads)
// =============================================================================
function Palettes() {
  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="01"
        title="UI-Farbpaletten (6 Triaden)"
        subtitle="T1–T6: Basis-Chassis, Akzente und Block-Kategoriefarben."
      />

      <div className="grid md:grid-cols-2 gap-4">
        {TRIADS.map((t) => (
          <PaletteTriad key={t.id} triad={t} />
        ))}
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          Physik-Block-Kategoriefarben (linker Rand der nTop-Blöcke)
        </h3>
        <p className="mt-1 text-xs text-zinc-500">
          Diese Farben codieren die Modulkategorie – nicht den Skalarwert im
          Viewport. Sie erscheinen nur im Block-Stack und im Outliner, nie in
          der Legende.
        </p>
        <div className="mt-4 grid md:grid-cols-2 lg:grid-cols-4 gap-2">
          {CATEGORY_COLORS.map((c) => (
            <div
              key={c.label}
              className="flex items-center gap-3 rounded-md border border-white/10 bg-black/20 p-2"
            >
              <div
                className="h-10 w-3 rounded-sm"
                style={{
                  backgroundColor: c.hex,
                  boxShadow: `0 0 10px ${c.hex}70`,
                }}
              />
              <div className="min-w-0 flex-1">
                <div
                  className="font-mono text-sm font-semibold tabular-nums"
                  style={{ color: c.hex }}
                >
                  {c.hex}
                </div>
                <div className="truncate text-[11px] font-medium text-zinc-200">
                  {c.label}
                </div>
                <div className="truncate text-[10px] text-zinc-500">
                  {c.desc}
                </div>
              </div>
            </div>
          ))}
        </div>
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          Dark-Chassis-Hierarchie (T3 + T4)
        </h3>
        <p className="mt-1 text-xs text-zinc-500">
          Die zwei dunklen Triaden sind das Haupt-Chassis. Helle Varianten (T1,
          T5) nur für Dokumenten-Export, Print oder Light-Mode.
        </p>
        <div className="mt-4 grid md:grid-cols-4 gap-3">
          {[
            { name: "Base 0 (farthest)", hex: "#0a0a0d" },
            { name: "T3 BG · Obsidian", hex: "#111217" },
            { name: "T4 BG · Carbon", hex: "#18181C" },
            { name: "Panel", hex: "#24242b" },
            { name: "Panel hover", hex: "#2b2b33" },
            { name: "Border", hex: "#3a3a42" },
            { name: "Border bright", hex: "#565661" },
            { name: "T3 Neutral · Text", hex: "#DFE5EB" },
          ].map((s) => (
            <div
              key={s.hex}
              className="flex items-center gap-2 rounded-md border border-white/10 p-2"
            >
              <div
                className="h-8 w-8 rounded border border-white/20"
                style={{ backgroundColor: s.hex }}
              />
              <div className="text-[11px] leading-tight">
                <div className="text-zinc-300">{s.name}</div>
                <div className="font-mono text-[10px] text-zinc-500">
                  {s.hex}
                </div>
              </div>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}

// =============================================================================
// Section: Colormaps
// =============================================================================
function Colormaps({
  bandMode,
  setBandMode,
}: {
  bandMode: "continuous" | 10 | 16 | 24;
  setBandMode: (m: "continuous" | 10 | 16 | 24) => void;
}) {
  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="02"
        title="CFD-Colormaps · 1D-LUT"
        subtitle="Stufenlos und diskret (Iso-Bänder). Alle LUTs werden beim Start in Oklab vorinterpoliert und als 1D-Textur an wgpu übergeben."
      />

      <div className="flex flex-wrap items-center gap-2 rounded-xl border border-white/10 bg-black/30 p-4">
        <span className="font-mono text-[11px] text-zinc-400">Darstellung:</span>
        {(["continuous", 10, 16, 24] as const).map((m) => (
          <button
            key={String(m)}
            onClick={() => setBandMode(m)}
            className={
              "rounded-md px-3 py-1 font-mono text-[11px] transition " +
              (bandMode === m
                ? "bg-[#723BFA] text-white shadow-lg shadow-violet-900/40"
                : "border border-white/10 bg-white/5 text-zinc-400 hover:bg-white/10")
            }
          >
            {m === "continuous" ? "stufenlos" : `${m} Iso-Bänder`}
          </button>
        ))}
        <span className="ml-auto font-mono text-[10px] text-zinc-500">
          1-Klick-Shader-Toggle im Fragment-Shader
        </span>
      </div>

      <div className="grid gap-4">
        {COLORMAPS.map((cm) => (
          <div
            key={cm.id}
            className="rounded-xl border border-white/10 bg-black/30 p-5 shadow-lg shadow-black/40"
          >
            <div className="flex flex-wrap items-start justify-between gap-3">
              <div>
                <div className="flex items-center gap-2">
                  <h3 className="text-lg font-semibold text-zinc-100">
                    {cm.name}
                  </h3>
                  <span className="rounded border border-white/10 bg-white/5 px-1.5 py-0.5 font-mono text-[9px] uppercase text-zinc-400">
                    {cm.category}
                  </span>
                  {cm.colorBlindSafe && (
                    <span className="rounded border border-[#B9F500]/40 bg-[#B9F500]/10 px-1.5 py-0.5 font-mono text-[9px] uppercase text-[#B9F500]">
                      CB-safe
                    </span>
                  )}
                </div>
                <p className="mt-1 max-w-3xl text-sm text-zinc-400">
                  {cm.description}
                </p>
              </div>
              <div className="rounded-md border border-white/10 bg-black/40 px-3 py-2 text-right">
                <div className="font-mono text-[10px] uppercase tracking-widest text-zinc-500">
                  Use Case
                </div>
                <div className="mt-0.5 max-w-xs text-[11px] text-zinc-300">
                  {cm.useCase}
                </div>
              </div>
            </div>

            <div className="mt-4 space-y-2">
              <ColormapBar
                cmap={cm}
                bands={bandMode === "continuous" ? undefined : bandMode}
                labelMin="0.0"
                labelMax="1.0"
              />
              <div className="flex flex-wrap gap-1">
                {cm.stops.map((stop, i) => (
                  <div
                    key={i}
                    className="flex items-center gap-1 rounded-sm px-1.5 py-0.5 font-mono text-[10px]"
                    style={{ backgroundColor: stop.hex + "22" }}
                  >
                    <div
                      className="h-2.5 w-2.5 rounded-sm border border-white/30"
                      style={{ backgroundColor: stop.hex }}
                    />
                    <span className="text-zinc-300">{stop.hex}</span>
                    <span className="text-zinc-500">
                      @ {stop.t.toFixed(2)}
                    </span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        ))}
      </div>

      {/* NaN/Clamp signal colors */}
      <div className="rounded-xl border border-[#EF233C]/30 bg-gradient-to-br from-[#EF233C]/10 via-black/40 to-[#B9F500]/10 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          NaN / Inf / Clamp · Signalfarben (Debug-Modus)
        </h3>
        <p className="mt-1 text-xs text-zinc-400">
          Divergierende oder geclampte Zellen leuchten sofort im Viewport auf
          – unabhängig von der aktiven Colormap.
        </p>
        <div className="mt-4 grid md:grid-cols-3 gap-3">
          {[
            { label: "NaN / Inf", hex: "#B9F500", dark: true },
            { label: "Clamp High", hex: "#FC75B4" },
            { label: "Clamp Low", hex: "#723BFA", dark: true },
            { label: "Offen / Leak-Patch", hex: "#EF233C" },
            { label: "Boundary Wall selected", hex: "#004AFD", dark: true },
            { label: "Selected Cell", hex: "#F4EB06" },
          ].map((s) => (
            <div
              key={s.label}
              className="flex items-center gap-3 rounded-md border border-white/10 p-2"
            >
              <div
                className="h-10 w-16 rounded border border-white/20"
                style={{
                  backgroundColor: s.hex,
                  boxShadow: `0 0 12px ${s.hex}70`,
                }}
              />
              <div>
                <div className="text-[11px] text-zinc-200">{s.label}</div>
                <div
                  className="font-mono text-[10px]"
                  style={{ color: s.hex }}
                >
                  {s.hex}
                </div>
              </div>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}

// =============================================================================
// Section: UI-Reference
// =============================================================================
function UIReference() {
  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="03"
        title="UI-Referenzen · nTop Blocks + Blender Dark"
        subtitle="Vorbilder für Layout, Docking, Outliner und Viewport. Gesicherte Assets: ui_ref_ntop_blocks.jpg, ui_ref_blender_dark.jpg."
      />

      <div className="rounded-xl border border-white/10 bg-black/30 p-4">
        <div className="mb-3 flex items-baseline justify-between">
          <h3 className="text-sm font-semibold text-zinc-100">
            nTop Blocks · MatCap Viewport
          </h3>
          <span className="font-mono text-[10px] text-zinc-500">
            ui_ref_ntop_blocks.jpg · nachgebaut
          </span>
        </div>
        {nTopBlocksMockup()}
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-4">
        <div className="mb-3 flex items-baseline justify-between">
          <h3 className="text-sm font-semibold text-zinc-100">
            Blender Dark Workspace · Outliner · Timeline
          </h3>
          <span className="font-mono text-[10px] text-zinc-500">
            ui_ref_blender_dark.jpg · nachgebaut
          </span>
        </div>
        {BlenderDarkMockup()}
      </div>

      <div className="grid md:grid-cols-3 gap-4">
        <div className="rounded-xl border border-white/10 bg-black/30 p-4">
          <h4 className="text-xs font-mono uppercase tracking-widest text-zinc-500">
            cmap_ref_dark_ember
          </h4>
          <p className="mt-1 text-[11px] text-zinc-400">
            10-stufige Skala, referenziert in palettes.json.
          </p>
          <div className="mt-3">
            <ColormapBar
              cmap={COLORMAPS.find((c) => c.id === "dark-ember")!}
              bands={10}
              height={28}
              showLabels={false}
            />
          </div>
        </div>
        <div className="rounded-xl border border-white/10 bg-black/30 p-4">
          <h4 className="text-xs font-mono uppercase tracking-widest text-zinc-500">
            cmap_ref_fire_scheme
          </h4>
          <p className="mt-1 text-[11px] text-zinc-400">
            6-stufiges Fire-Schema für einfache T-Maps.
          </p>
          <div className="mt-3">
            <ColormapBar
              cmap={COLORMAPS.find((c) => c.id === "fire-scheme")!}
              bands={6}
              height={28}
              showLabels={false}
            />
          </div>
        </div>
        <div className="rounded-xl border border-white/10 bg-black/30 p-4">
          <h4 className="text-xs font-mono uppercase tracking-widest text-zinc-500">
            cmap_ref_perceptual_lightness
          </h4>
          <p className="mt-1 text-[11px] text-zinc-400">
            L*-Helligkeitslinearität aus jrwrigh/cfdpost_colormaps.
          </p>
          <div className="mt-3">
            <PerceptualLightness />
          </div>
        </div>
      </div>
    </section>
  );
}

function PerceptualLightness() {
  // visualise L* (perceptual lightness) vs nominal t for a couple of the maps
  const width = 300;
  const height = 80;
  const maps = [
    { id: "inferno", color: "#F48C06" },
    { id: "ampel", color: "#009E73" },
    { id: "turbo", color: "#78F148" },
  ] as const;
  return (
    <svg viewBox={`0 0 ${width} ${height}`} className="w-full">
      <rect width={width} height={height} fill="#0d0d11" rx="4" />
      {/* grid */}
      {[0.25, 0.5, 0.75].map((g) => (
        <line
          key={g}
          x1={0}
          x2={width}
          y1={height - g * height}
          y2={height - g * height}
          stroke="#ffffff"
          strokeOpacity={0.06}
        />
      ))}
      <line
        x1={0}
        x2={width}
        y1={height}
        y2={height}
        stroke="#ffffff"
        strokeOpacity={0.2}
      />
      {maps.map((m) => {
        const cmap = COLORMAPS.find((c) => c.id === m.id)!;
        // approximate L* from sRGB (very rough but illustrative)
        const points: string[] = [];
        for (let i = 0; i <= 50; i++) {
          const t = i / 50;
          const hex = sample(cmap.stops, t);
          const [r, g, b] = hexToRgb(hex);
          const lin = (v: number) => {
            const s = v / 255;
            return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
          };
          const Y = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
          const L = Y > 0.008856 ? 116 * Math.pow(Y, 1 / 3) - 16 : 903.3 * Y;
          const x = t * width;
          const y = height - (L / 100) * height;
          points.push(`${x.toFixed(1)},${y.toFixed(1)}`);
        }
        return (
          <polyline
            key={m.id}
            points={points.join(" ")}
            fill="none"
            stroke={m.color}
            strokeWidth={1.5}
            opacity={0.9}
          />
        );
      })}
    </svg>
  );
}

// small inline helpers (duplicated from colormaps.ts so PerceptualLightness can run client-side without re-exporting)
function hexToRgb(hex: string): [number, number, number] {
  const h = hex.replace("#", "");
  const v = parseInt(h, 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}
function sample(stops: { t: number; hex: string }[], t: number): string {
  if (t <= stops[0].t) return stops[0].hex;
  if (t >= stops[stops.length - 1].t) return stops[stops.length - 1].hex;
  for (let i = 0; i < stops.length - 1; i++) {
    const a = stops[i];
    const b = stops[i + 1];
    if (t >= a.t && t <= b.t) {
      const k = (t - a.t) / (b.t - a.t);
      const [r1, g1, b1] = hexToRgb(a.hex);
      const [r2, g2, b2] = hexToRgb(b.hex);
      const c = (x: number, y: number) =>
        Math.max(0, Math.min(255, Math.round(x + (y - x) * k)));
      return (
        "#" +
        [c(r1, r2), c(g1, g2), c(b1, b2)]
          .map((v) => v.toString(16).padStart(2, "0"))
          .join("")
          .toUpperCase()
      );
    }
  }
  return stops[stops.length - 1].hex;
}

// =============================================================================
// Section: Architecture (Zero Color Interference)
// =============================================================================
function Architecture() {
  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="04"
        title="Zero Color Interference"
        subtitle="Strikte Trennung: UI-Chassis vs. 3D-Messdaten-Farben."
      />

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <p className="text-zinc-300 leading-relaxed">
          Das dunkle Chassis (T3 <code className="text-[#B9F500]">#111217</code> +
          T4 <code className="text-[#B9F500]">#18181C</code>) ist optisch{" "}
          <span className="font-semibold text-zinc-100">
            vollkommen neutral
          </span>
          . Kein bunter UI-Hintergrund verändert das menschliche Farbempfinden
          beim Ablesen der CFD-Skalen. Bunte Akzente sind{" "}
          <em>ausschließlich</em> für zwei Zwecke erlaubt:
        </p>
        <ul className="mt-4 space-y-2 text-sm text-zinc-400">
          <li className="flex gap-3">
            <span className="mt-1 h-1.5 w-1.5 flex-none rounded-full bg-[#B9F500]" />
            <span>
              <span className="text-zinc-100 font-medium">
                Block-Kategoriefarben
              </span>{" "}
              (linker Rand der nTop-Blöcke) – sie erscheinen im Block-Stack
              und im Outliner, niemals aber im 3D-Viewport neben Skalardaten.
            </span>
          </li>
          <li className="flex gap-3">
            <span className="mt-1 h-1.5 w-1.5 flex-none rounded-full bg-[#B9F500]" />
            <span>
              <span className="text-zinc-100 font-medium">
                Debug-Signale
              </span>{" "}
              (NaN, Clamp, Leak-Patches) – pulsierend und nur, wenn der
              Debug-Modus aktiv ist.
            </span>
          </li>
        </ul>
      </div>

      <div className="grid md:grid-cols-2 gap-4">
        <div className="rounded-xl border border-white/10 bg-black/30 p-5">
          <h3 className="text-sm font-semibold text-zinc-100">
            Modus A · CAD / Mesh (MatCap)
          </h3>
          <p className="mt-2 text-sm text-zinc-400">
            MatCap + Screen-Space Cavity + Eye-Dome Lighting – genau deshalb
            wirkt das graue Bauteil im nTop-Screenshot ohne Lampen-Setup so
            plastisch und scharf. Selektierte Patches leuchten in{" "}
            <span className="font-mono text-[#004AFD]">#004AFD</span>,
            Gitterlinien in neutralem Grau.
          </p>
          <div className="mt-4 rounded-md border border-white/10 bg-[#2a2a30] p-2">
            <div className="h-28 w-full">
              <svg viewBox="0 0 400 120" className="h-full w-full">
                <defs>
                  <radialGradient id="mc1" cx="40%" cy="35%" r="70%">
                    <stop offset="0%" stopColor="#e8e8ec" />
                    <stop offset="40%" stopColor="#8f8f97" />
                    <stop offset="100%" stopColor="#14141a" />
                  </radialGradient>
                </defs>
                <path
                  d="M30,80 C60,20 220,0 360,30 C370,70 320,110 230,105 C150,100 60,100 30,80 Z"
                  fill="url(#mc1)"
                  stroke="#000"
                />
              </svg>
            </div>
          </div>
        </div>

        <div className="rounded-xl border border-white/10 bg-black/30 p-5">
          <h3 className="text-sm font-semibold text-zinc-100">
            Modus B · CFD-Skalarfeld (Unlit Scientific)
          </h3>
          <p className="mt-2 text-sm text-zinc-400">
            Sobald ein Skalarfeld aktiv ist, darf{" "}
            <em>kein Tone-Mapping / Schattenwurf</em> den Farbwert verändern.
            900 K auf der Schaufel = exakt derselbe sRGB-Wert wie 900 K auf
            der Legende. Geometrieform nur über dezente Netz- /
            Silhouettenkanten.
          </p>
          <div className="mt-4 rounded-md border border-white/10 bg-[#2a2a30] p-2">
            <div className="h-28 w-full">
              <svg viewBox="0 0 400 120" className="h-full w-full">
                <defs>
                  <linearGradient id="inf2" x1="0" y1="0" x2="1" y2="0">
                    <stop offset="0%" stopColor="#000004" />
                    <stop offset="30%" stopColor="#781C6D" />
                    <stop offset="60%" stopColor="#ED4560" />
                    <stop offset="100%" stopColor="#FCFFA4" />
                  </linearGradient>
                </defs>
                <path
                  d="M30,80 C60,20 220,0 360,30 C370,70 320,110 230,105 C150,100 60,100 30,80 Z"
                  fill="url(#inf2)"
                  stroke="#000"
                  strokeWidth="0.5"
                />
              </svg>
            </div>
          </div>
        </div>
      </div>

      <div className="rounded-xl border border-[#B9F500]/30 bg-[#B9F500]/5 p-6">
        <h3 className="text-sm font-semibold text-[#B9F500]">
          Ergänzung: Tabular Monospace & Einheiten-Inputs
        </h3>
        <p className="mt-2 text-sm text-zinc-300">
          Alle Zahlen, Exponenten (<code className="font-mono">1.42e-5</code>)
          und Koordinaten nutzen feste Ziffernbreiten (tabular-nums), damit
          Live-Anzeigen nie zittern. Eingabefelder verstehen direkt Einheiten
          wie in nTop:{" "}
          <code className="font-mono">12 mm</code>,{" "}
          <code className="font-mono">850 C</code>,{" "}
          <code className="font-mono">3 bar</code>, Speicherung intern immer
          SI.
        </p>
      </div>
    </section>
  );
}

// =============================================================================
// Section: GPU rules
// =============================================================================
function GPURules() {
  const rules = [
    {
      num: "①",
      title: "Zwei Shading-Modi: MatCap vs. Unlit Scientific",
      body: "CAD/Gitter: MatCap + SS-Cavity + EDLC. Skalarfeld-Modus: 100% unlit, kein Tone-Mapping, keine Schatten, nur subtile Silhouetten-/Netzkanten zur Formerhaltung.",
      color: "#FC75B4",
    },
    {
      num: "②",
      title: "Camera-relative f64 → f32 + Reverse-Z Depth32Float",
      body: "Gitterkoordinaten bleiben im RAM in f64, vor Upload wird die Kameraposition abgezogen. Reverse-Z verhindert Z-Fighting beim Reinzoomen in 1 µm dünne Grenzschichtzellen (y+ ≈ 1).",
      color: "#004AFD",
    },
    {
      num: "③",
      title: "LUT-Interpolation in Oklab / CIELAB",
      body: "Lineares RGB-Mischen in wgpu lässt Zwischenfarben (Grün→Rot) schmutzig-grau wirken. Alle 1D-LUT-Texturen werden beim Start in Oklab vorberechnet und linear als 256/1024-Entry-Textur hochgeladen.",
      color: "#723BFA",
    },
    {
      num: "④",
      title: "Iso-Bänderung + NaN/Clamp-Signalfarben (1-Klick)",
      body: "Fragment-Shader-Schalter schaltet jede Colormap zwischen stufenlos und diskreten Bändern (10/16/24) um. NaN/Inf-Zellen leuchten in #B9F500 / #FC75B4 – divergierende Zellen in einer Sekunde gefunden.",
      color: "#B9F500",
    },
  ];

  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="05"
        title="Vier kritische GPU-Regeln"
        subtitle="Für 120+ FPS auf Industriegittern und messtechnisch korrekte Farbwiedergabe."
      />

      <div className="grid gap-4 md:grid-cols-2">
        {rules.map((r) => (
          <div
            key={r.num}
            className="rounded-xl border border-white/10 bg-black/30 p-5"
          >
            <div className="flex items-center gap-3">
              <div
                className="flex h-10 w-10 items-center justify-center rounded-lg border border-white/20 font-mono text-lg font-bold"
                style={{ color: r.color, backgroundColor: r.color + "12" }}
              >
                {r.num}
              </div>
              <h3 className="text-base font-semibold text-zinc-100">
                {r.title}
              </h3>
            </div>
            <p className="mt-3 text-sm leading-relaxed text-zinc-400">
              {r.body}
            </p>
          </div>
        ))}
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          Render-Pipeline (schematisch)
        </h3>
        <div className="mt-4 overflow-x-auto">
          <pre className="font-mono text-[11px] leading-relaxed text-zinc-300">
{`foam_fast_io (rlib, zero-copy)
        │
        ▼
  [CAD/Mesh]  ─► f64 coords in RAM
        │         ├─ subtract camera_origin
        │         └─ pack as f32 vertex-buffer (f64→f32 camera-relative)
        ▼
  wgpu RenderPass
        ├─ Depth32Float  (REVERSE_Z, clip 0..1 → 1..0)
        ├─ Mode A: MatCap + SS-Cavity + EDLC  (CAD/Mesh)
        └─ Mode B: Unlit Scientific           (Scalar field)
                ├─ 1D-LUT texture (1024 texels, Oklab-interpolated)
                ├─ Iso-band toggle (disc / continuous)
                └─ NaN/Inf override → signal color (#B9F500/#FC75B4)
        │
        ▼
  egui UI layer  (dockable panels, plot, outliner, inspector)
`}
          </pre>
        </div>
      </div>
    </section>
  );
}

// =============================================================================
// Section: Backend API
// =============================================================================
function BackendAPI() {
  const channels = [
    {
      letter: "A",
      title: "Zero-Copy 3D-Daten",
      body: "Direktes Rust-Crate-Linking gegen foam_fast_io (rlib ist vorbereitet). Flache Binärpuffer für CAD-Dreiecke und Face-Group-IDs, damit Patches klickbar und in #004AFD im Viewport selektierbar sind.",
      color: "#004AFD",
    },
    {
      letter: "B",
      title: "Steuerung & Live-Telemetrie",
      body: "Lokaler JSON-RPC / NDJSON-IPC-Kontrakt zum Python-Stack: CADGeometryEngine, MeshEngine, ModuleRegistry, CaseGenerator, WslSimulationRunner.",
      color: "#723BFA",
    },
  ];

  return (
    <section className="space-y-6">
      <SectionHeader
        chapter="06"
        title="Frontend → Backend Anforderungen"
        subtitle="FRONTEND_REQUIREMENTS_ON_BACKEND.md v0.2.0 · zwei Kommunikationskanäle."
      />

      <div className="grid md:grid-cols-2 gap-4">
        {channels.map((c) => (
          <div
            key={c.letter}
            className="rounded-xl border border-white/10 bg-black/30 p-5"
          >
            <div className="flex items-center gap-3">
              <div
                className="flex h-10 w-10 items-center justify-center rounded-lg border font-mono text-xl font-bold"
                style={{
                  color: c.color,
                  borderColor: c.color,
                  backgroundColor: c.color + "15",
                }}
              >
                {c.letter}
              </div>
              <h3 className="text-base font-semibold text-zinc-100">
                {c.title}
              </h3>
            </div>
            <p className="mt-3 text-sm leading-relaxed text-zinc-400">
              {c.body}
            </p>
          </div>
        ))}
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          UI/UX-Inputs (streng SI-normiert)
        </h3>
        <div className="mt-4 grid md:grid-cols-2 gap-4 text-sm">
          {[
            {
              t: "CAD & Domain",
              b: "Import-Parameter, Toleranzen, Patch-Gruppierung, Leck-/Offene-Kanten-Segmente zum Anleuchten am Bauteil.",
              c: "#004AFD",
            },
            {
              t: "Meshing",
              b: "Octree-Hexcore / DistMesh, Grenzschicht (N_layers, Δy₁), Cut-Cell-Gewichte, Zellqualitäts-Felder (skewness, nonOrthogonality, AMIPS, is_folded) für Ampel-Colormap.",
              c: "#B9F500",
            },
            {
              t: "Physik-Block-Stack & BCs",
              b: "JSON-Schema export_ui_schema() mit visible_if-Regeln, SI-Dimensionen, Kategorie-Tags für nTop-Blockfarben.",
              c: "#FC75B4",
            },
            {
              t: "Solver & Live-Stream",
              b: "start / write_now / stop_graceful / abort. NDJSON-Iterationspakete für Residuen, Courant-Zahl, NaN-Zellzähler.",
              c: "#EF233C",
            },
          ].map((s) => (
            <div
              key={s.t}
              className="rounded-lg border border-white/10 bg-black/40 p-4"
            >
              <div
                className="font-mono text-[10px] uppercase tracking-widest"
                style={{ color: s.c }}
              >
                {s.t}
              </div>
              <p className="mt-2 text-sm leading-relaxed text-zinc-400">
                {s.b}
              </p>
            </div>
          ))}
        </div>
      </div>

      <div className="rounded-xl border border-[#FEC40B]/30 bg-[#FEC40B]/5 p-6">
        <h3 className="text-sm font-semibold text-[#FEC40B]">
          3 Erweiterungswünsche an foam_fast_io
        </h3>
        <ol className="mt-3 list-decimal space-y-2 pl-5 text-sm text-zinc-300">
          <li>
            Parsing des <code className="font-mono">boundaryField</code>-Blocks
            in <code className="font-mono">fields.rs</code> (aktuell nur{" "}
            <code className="font-mono">internalField</code>; wir brauchen
            boundaryField für exakte Wand-Temperaturen T_w und yPlus).
          </li>
          <li>
            Schnelle Zeitschritt-Discovery{" "}
            <code className="font-mono">list_time_dirs</code> für die
            Blender-Zeitleiste unten.
          </li>
          <li>
            Binärformat-Support (<code className="font-mono">writeFormat binary;</code>)
            neben ASCII/Gzip.
          </li>
        </ol>
      </div>

      <div className="rounded-xl border border-white/10 bg-black/30 p-6">
        <h3 className="text-sm font-semibold text-zinc-100">
          Was für den ersten Rough-Build benötigt wird
        </h3>
        <div className="mt-4 space-y-4">
          <div>
            <div className="font-mono text-[11px] uppercase tracking-widest text-[#B9F500]">
              A. Toolchain (Windows)
            </div>
            <p className="mt-1 text-sm text-zinc-400">
              <code className="font-mono">winget install Rustlang.Rustup</code>{" "}
              (MSVC-Linker ist durch die C++-Mesher-DLL bereits vorhanden).
              Beim ersten <code className="font-mono">cargo build</code> lädt
              Cargo die Crates (wgpu, winit, egui, egui_dock, egui_plot, glam,
              bytemuck, serde_json) – danach alle Iterationen lokal in
              Sekunden.
            </p>
          </div>
          <div>
            <div className="font-mono text-[11px] uppercase tracking-widest text-[#004AFD]">
              B. Testdaten (teilweise bereits vorhanden)
            </div>
            <p className="mt-1 text-sm text-zinc-400">
              <span className="text-zinc-200">Bereits da:</span>{" "}
              <code className="font-mono">BladeRev1CFD.obj</code>,{" "}
              <code className="font-mono">TestCAD.stl</code> (reicht für
              ersten MatCap-Viewport + LUT-Test mit 120+ FPS).{" "}
              <span className="text-zinc-200">Wünschenswert:</span> ein
              kleiner OpenFOAM-Case in{" "}
              <code className="font-mono">cases/</code> (5.000–30.000 Zellen,
              U / p / T) für echte Volumengitter via foam_fast_io.
            </p>
          </div>

          <div className="grid md:grid-cols-2 gap-3">
            <div className="rounded-lg border border-[#B9F500]/40 bg-[#B9F500]/5 p-4">
              <div className="flex items-center justify-between">
                <div className="font-mono text-[11px] uppercase tracking-widest text-[#B9F500]">
                  C1 · Empfohlen
                </div>
                <span className="rounded bg-[#B9F500]/20 px-1.5 py-0.5 font-mono text-[9px] text-[#B9F500]">
                  Default
                </span>
              </div>
              <div className="mt-2 text-sm text-zinc-100 font-semibold">
                wgpu + egui (egui_dock + egui_plot)
              </div>
              <p className="mt-1 text-xs text-zinc-400">
                Extrem schnelle Kompilierung, 0 % Overhead, volle Kontrolle
                über Reverse-Z, Custom CFD-Compute-Shader, 0 % GPU-Last im
                Stillstand.
              </p>
            </div>
            <div className="rounded-lg border border-white/10 bg-black/40 p-4">
              <div className="font-mono text-[11px] uppercase tracking-widest text-zinc-500">
                C2 · Optional
              </div>
              <div className="mt-2 text-sm text-zinc-300 font-semibold">
                Bevy + bevy_egui
              </div>
              <p className="mt-1 text-xs text-zinc-400">
                Bringt Game-Engine-ECS & XR-Crates mit, aber schwerere
                Abhängigkeiten und permanenter Game-Loop.
              </p>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}

// =============================================================================
// Small utility components
// =============================================================================
function SectionHeader({
  chapter,
  title,
  subtitle,
}: {
  chapter: string;
  title: string;
  subtitle: string;
}) {
  return (
    <div>
      <div className="flex items-baseline gap-3">
        <span className="font-mono text-[11px] uppercase tracking-[0.2em] text-[#B9F500]">
          Kapitel {chapter}
        </span>
        <div className="h-px flex-1 bg-gradient-to-r from-[#B9F500]/40 via-white/10 to-transparent" />
      </div>
      <h2 className="mt-2 text-3xl font-bold tracking-tight text-zinc-50">
        {title}
      </h2>
      <p className="mt-2 max-w-3xl text-zinc-400">{subtitle}</p>
    </div>
  );
}
