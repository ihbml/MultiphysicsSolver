import { useEffect, useMemo, useRef, useState } from "react";
import { KIND_COLOR, PLUGINS, useEngine, type PluginDef } from "@/state/engine";
import { MATERIALS } from "@/engine/parts";
import { stressAt } from "@/engine/render";
import { cn } from "@/utils/cn";
import { IconChip, IconPlug, IconTerminal, IconTree } from "./Icons";

type Tab = "ecs" | "addons" | "console";

export default function Dock() {
  const { enabled, log, part } = useEngine();
  const [tab, setTab] = useState<Tab>("ecs");

  const tabs: { id: Tab; label: string; icon: React.ReactNode; badge?: string }[] = [
    { id: "ecs", label: "ECS", icon: <IconTree />, badge: `${part.solids.length} mesh` },
    { id: "addons", label: "ADDONS", icon: <IconPlug />, badge: `${enabled.length}/${PLUGINS.length}` },
    { id: "console", label: "CONSOLE", icon: <IconTerminal />, badge: String(log.length) },
  ];

  return (
    <div className="flex h-full min-h-0 flex-col border-l border-ink-700 bg-ink-900">
      <div className="flex shrink-0 border-b border-ink-700 bg-ink-850">
        {tabs.map((t) => (
          <button
            key={t.id}
            type="button"
            onClick={() => setTab(t.id)}
            className={cn(
              "relative flex flex-1 items-center justify-center gap-1.5 py-2.5 font-mono text-[10px] uppercase tracking-[0.18em] transition-colors",
              tab === t.id ? "text-amber-signal" : "text-ink-400 hover:text-paper-200",
            )}
          >
            {t.icon}
            <span>{t.label}</span>
            {t.badge && (
              <span
                className={cn(
                  "tabular rounded-[2px] px-1 text-[9px]",
                  tab === t.id ? "bg-amber-signal/15 text-amber-signal" : "bg-ink-750 text-ink-400",
                )}
              >
                {t.badge}
              </span>
            )}
            {tab === t.id && (
              <span className="absolute inset-x-0 bottom-0 h-[2px] bg-amber-signal" />
            )}
          </button>
        ))}
      </div>
      <div className="thin-scroll min-h-0 flex-1 overflow-y-auto">
        {tab === "ecs" && <EcsPanel />}
        {tab === "addons" && <PluginPanel />}
        {tab === "console" && <ConsolePanel />}
      </div>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* ECS inspector                                                       */
/* ------------------------------------------------------------------ */

interface Ent {
  id: string;
  name: string;
  tag: string;
  solidId?: string;
  accent?: string;
  comps: { name: string; fields: string[][] }[];
}

function EcsPanel() {
  const { part, cam, has, section, selected, setSelected, lod, backend, stats } = useEngine();
  const [, force] = useState(0);
  const [open, setOpen] = useState<string[]>(["Camera::Main"]);

  useEffect(() => {
    const t = setInterval(() => force((n) => n + 1), 140);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    if (selected) setOpen((o) => (o.includes(selected) ? o : [...o, selected]));
  }, [selected]);

  const c = cam.current;
  const triOf = (id: string) =>
    part.solids.find((s) => s.id === id)?.mesh.tris.length ?? 0;
  const edgeOf = (id: string) => part.solids.find((s) => s.id === id)?.mesh.edges.length ?? 0;

  const ents: Ent[] = useMemo(() => {
    const list: Ent[] = [
      {
        id: "Camera::Main",
        name: "Camera::Main",
        tag: "view",
        comps: [
          {
            name: "Orbit",
            fields: [
              ["yaw", c.yaw.toFixed(4)],
              ["pitch", c.pitch.toFixed(4)],
              ["dist", c.dist.toFixed(3)],
              ["target", `[${c.target.map((v) => v.toFixed(2)).join(", ")}]`],
            ],
          },
          {
            name: "Projection",
            fields: [
              ["fov", "40.0°"],
              ["near", "0.12"],
              ["far", "240.0"],
            ],
          },
          {
            name: "RenderDevice",
            fields: [
              ["api", backend.label],
              ["vsync", "fifo"],
              ["msaa", "4×"],
            ],
          },
        ],
      },
      {
        id: "Light::Key",
        name: "Light::Key",
        tag: "light",
        comps: [
          {
            name: "Directional",
            fields: [
              ["dir", "[0.42, 0.78, 0.46]"],
              ["intensity", "3200 lux"],
              ["color", "#fff4e2"],
            ],
          },
          { name: "ShadowCaster", fields: [["map", "2048²"], ["bias", "0.0004"]] },
        ],
      },
      {
        id: "Document::Root",
        name: "Document::Root",
        tag: "doc",
        accent: "#8ede63",
        comps: [
          {
            name: "StepDocument",
            fields: [
              ["file", `${part.code.toLowerCase()}.step`],
              ["schema", "AP242"],
              ["units", "millimetre"],
              ["solids", String(part.solids.length)],
            ],
          },
          {
            name: "Tessellation",
            fields: [
              ["quality", ["Coarse", "Standard", "Fine"][lod]],
              ["tris", String(stats.tris)],
            ],
          },
        ],
      },
      ...part.solids.map((s): Ent => ({
        id: s.entity,
        name: s.entity,
        tag: "mesh",
        solidId: s.id,
        comps: [
          {
            name: "MeshData",
            fields: [
              ["tris", String(s.mesh.tris.length)],
              ["edges", String(s.mesh.edges.length)],
              ["winding", "ccw-out"],
            ],
          },
          {
            name: "Material",
            fields: [
              ["id", s.material],
              ["alloy", MATERIALS[s.material].label],
            ],
          },
          { name: "Pickable", fields: [["layer", "geometry"], ["id", s.id]] },
          ...(has("analysis.fem")
            ? [
                {
                  name: "StressField",
                  fields: [
                    ["σmax", `${(stressAt(s.mesh.tris[0][0], part) * 412).toFixed(1)} MPa`],
                    ["solver", "P1 CG"],
                  ],
                },
              ]
            : []),
        ],
      })),
    ];

    if (has("tool.section_plane") && section.on)
      list.push({
        id: "Section::Plane",
        name: "Section::Plane",
        tag: "addon",
        accent: "#f5a524",
        comps: [
          {
            name: "ClipPlane",
            fields: [
              ["axis", section.axis.toUpperCase()],
              ["offset", section.pos.toFixed(3)],
              ["keep", "negative half-space"],
              ["cap_edges", "emit"],
            ],
          },
        ],
      });
    if (has("analysis.fem"))
      list.push({
        id: "Fem::Solve",
        name: "Fem::Solve",
        tag: "addon",
        accent: "#ff6b5a",
        comps: [
          {
            name: "TetMesh",
            fields: [
              ["tets", "12 480"],
              ["dof", "37 443"],
              ["load", `[${part.load.map((v) => v.toFixed(2)).join(", ")}]`],
            ],
          },
          { name: "BoundaryCondition", fields: [["fixed", "base plate"], ["force", "1 200 N"]] },
        ],
      });
    if (has("overlay.gdt"))
      part.gdt.forEach((g) =>
        list.push({
          id: `Gdt::Datum[${g.datum}]`,
          name: `Gdt::Datum[${g.datum}]`,
          tag: "addon",
          accent: "#f5a524",
          comps: [
            { name: "FeatureFrame", fields: [["text", g.frame]] },
            {
              name: "Leader",
              fields: [["anchor", `[${g.anchor.map((v) => v.toFixed(2)).join(", ")}]`]],
            },
          ],
        }),
      );
    if (has("import.dxf"))
      list.push({
        id: "Dxf::Profile",
        name: "Dxf::Profile",
        tag: "addon",
        accent: "#8ede63",
        comps: [
          {
            name: "Polyline",
            fields: [
              ["source", "nesting_sheet.dxf"],
              ["entities", "120 arcs · 4 splines"],
              ["layer", "0 · closed"],
            ],
          },
        ],
      });
    if (has("tool.measure"))
      list.push({
        id: "Measure::DimSet",
        name: "Measure::DimSet",
        tag: "addon",
        accent: "#35c9e8",
        comps: [
          { name: "Dimension", fields: [["count", String(part.dims.length)], ["style", "ISO-129"]] },
        ],
      });
    if (has("debug.gpu_inspector"))
      list.push({
        id: "Inspector::Capture",
        name: "Inspector::Capture",
        tag: "addon",
        accent: "#93a4b6",
        comps: [
          {
            name: "PassQuery",
            fields: [
              ["passes", "7"],
              ["barriers", "3"],
              ["draw_calls", String(4 + part.solids.length)],
            ],
          },
        ],
      });
    if (has("geom.boolean_ops"))
      list.push({
        id: "Csg::Workspace",
        name: "Csg::Workspace",
        tag: "addon",
        accent: "#8ede63",
        comps: [{ name: "BspNode", fields: [["depth", "14"], ["epsilon", "1e-5 mm"]] }],
      });
    return list;
  }, [part, c.yaw, c.pitch, c.dist, c.target, has, section, lod, backend, stats.tris]);

  const systemRows = useMemo(() => {
    const rows: [string, string, string][] = [
      ["core", "input::poll_winit", "0.08"],
      ["core", "camera::orbit_update", "0.05"],
      ["core", "scene::cull_frustum", "0.31"],
    ];
    for (const p of PLUGINS) {
      if (!PLUGINS.find((x) => x.id === p.id)) continue;
      if (p.locked || has(p.id)) {
        for (const s of p.systems) {
          rows.push([
            p.id.split(".")[0],
            s,
            (0.04 + Math.random() * (p.kind === "analysis" ? 1.6 : 0.4)).toFixed(2),
          ]);
        }
      }
    }
    rows.push(["core", "render::record_passes", "0.62"]);
    rows.push(["core", "egui::paint", "0.44"]);
    return rows;
  }, [has, part]);

  const systems = systemRows.length;

  return (
    <div className="p-3">
      <div className="mb-3 grid grid-cols-3 gap-px overflow-hidden border border-ink-700 bg-ink-700">
        <Counter label="entities" value={ents.length} />
        <Counter label="archetypes" value={Math.min(14, 4 + Math.round(ents.length / 3))} />
        <Counter label="systems" value={systems} />
      </div>

      <Section label="world">
        {ents.map((e) => {
          const isOpen = open.includes(e.id);
          const isSel = e.solidId && e.solidId === selected;
          return (
            <div
              key={e.id}
              className={cn(
                "border-l-2 transition-colors",
                isSel ? "border-amber-signal bg-amber-signal/[0.07]" : "border-transparent",
              )}
            >
              <button
                type="button"
                onClick={() => {
                  setOpen((o) => (isOpen ? o.filter((x) => x !== e.id) : [...o, e.id]));
                  if (e.solidId) setSelected(isSel ? null : e.solidId);
                }}
                className="flex w-full items-center gap-2 px-2 py-1 text-left hover:bg-ink-800/70"
              >
                <span
                  className={cn(
                    "font-mono text-[9px] transition-transform",
                    isOpen ? "rotate-90 text-amber-signal" : "text-ink-500",
                  )}
                >
                  ▶
                </span>
                <span
                  className={cn(
                    "truncate font-mono text-[11px]",
                    isSel ? "text-amber-signal" : "text-paper-200",
                  )}
                >
                  {e.name}
                </span>
                <span
                  className="ml-auto shrink-0 rounded-[2px] px-1 font-mono text-[8.5px] uppercase tracking-wider"
                  style={{
                    color: e.accent ?? "#5c6d80",
                    background: `${e.accent ?? "#5c6d80"}18`,
                  }}
                >
                  {e.tag}
                </span>
              </button>
              {isOpen && (
                <div className="pb-1.5 pl-6 pr-2">
                  {e.comps.map((cmp) => (
                    <div key={cmp.name} className="mt-1 border border-ink-750 bg-ink-950/60">
                      <div className="flex items-center gap-1.5 border-b border-ink-750 px-2 py-0.5">
                        <IconChip width={9} height={9} className="text-cyan-signal/70" />
                        <span className="font-mono text-[9.5px] text-cyan-signal/90">{cmp.name}</span>
                      </div>
                      {cmp.fields.map(([k, v]) => (
                        <div key={k} className="flex gap-2 px-2 py-[1px]">
                          <span className="w-[62px] shrink-0 font-mono text-[9.5px] text-ink-400">
                            {k}
                          </span>
                          <span className="tabular truncate font-mono text-[9.5px] text-paper-300">
                            {v}
                          </span>
                        </div>
                      ))}
                    </div>
                  ))}
                </div>
              )}
            </div>
          );
        })}
      </Section>

      <Section label="schedule · per frame">
        <div className="border border-ink-750 bg-ink-950/50">
          {systemRows.map(([stage, name, ms]) => (
            <div
              key={name}
              className="flex items-center gap-2 border-b border-ink-850 px-2 py-[3px] last:border-0"
            >
              <span className="w-[46px] shrink-0 font-mono text-[8.5px] uppercase text-ink-500">
                {stage}
              </span>
              <span className="truncate font-mono text-[10px] text-paper-300">{name}</span>
              <span className="ml-auto flex items-center gap-1.5">
                <span className="h-[3px] w-10 overflow-hidden bg-ink-750">
                  <span
                    className="block h-full bg-cyan-signal/70"
                    style={{ width: `${Math.min(100, (parseFloat(ms) / 1.7) * 100)}%` }}
                  />
                </span>
                <span className="tabular w-[34px] text-right font-mono text-[9px] text-ink-400">
                  {ms}ms
                </span>
              </span>
            </div>
          ))}
        </div>
        <p className="mt-2 font-mono text-[9px] leading-relaxed text-ink-500">
          {triOf(selected ?? "") || edgeOf(selected ?? "")
            ? `selected geometry: ${triOf(selected ?? "")} tris / ${edgeOf(selected ?? "")} edges`
            : "click an entity to inspect its components · hover geometry in the viewport"}
        </p>
      </Section>
    </div>
  );
}

function Counter({ label, value }: { label: string; value: number }) {
  return (
    <div className="bg-ink-850 px-2 py-1.5 text-center">
      <div className="tabular font-display text-[17px] font-bold leading-none text-paper-100">
        {value}
      </div>
      <div className="mt-1 font-mono text-[8.5px] uppercase tracking-[0.16em] text-ink-400">
        {label}
      </div>
    </div>
  );
}

function Section({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="mb-4">
      <div className="mb-1.5 flex items-center gap-2">
        <span className="font-mono text-[9px] uppercase tracking-[0.22em] text-ink-400">
          {label}
        </span>
        <span className="h-px flex-1 bg-ink-750" />
      </div>
      {children}
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Addon registry                                                      */
/* ------------------------------------------------------------------ */

function PluginPanel() {
  const { enabled, has, toggle } = useEngine();
  const [detail, setDetail] = useState<string | null>(null);

  return (
    <div className="p-3">
      <p className="mb-3 border border-ink-750 bg-ink-950/50 p-2 font-mono text-[9.5px] leading-relaxed text-ink-400">
        Addons are dynamic libraries resolved at startup from{" "}
        <span className="text-cyan-signal">~/.anvil/plugins</span>. Each manifest declares
        components, systems and egui panels; the engine injects them into the Bevy schedule with no
        core rebuild.
      </p>
      {PLUGINS.map((p) => (
        <PluginCard
          key={p.id}
          p={p}
          on={enabled.includes(p.id)}
          open={detail === p.id}
          depsMissing={p.deps.filter((d) => !has(d))}
          onToggle={() => toggle(p.id)}
          onOpen={() => setDetail(detail === p.id ? null : p.id)}
        />
      ))}
    </div>
  );
}

function PluginCard({
  p,
  on,
  open,
  depsMissing,
  onToggle,
  onOpen,
}: {
  p: PluginDef;
  on: boolean;
  open: boolean;
  depsMissing: string[];
  onToggle: () => void;
  onOpen: () => void;
}) {
  const color = KIND_COLOR[p.kind];
  return (
    <div
      className={cn(
        "mb-1.5 border transition-colors",
        on ? "border-ink-600 bg-ink-850" : "border-ink-750 bg-ink-900/60",
      )}
    >
      <div className="flex items-start gap-2 p-2">
        <button
          type="button"
          onClick={onToggle}
          disabled={p.locked}
          className={cn(
            "mt-[3px] h-[14px] w-[26px] shrink-0 rounded-full border transition-colors",
            on ? "border-transparent" : "border-ink-600 bg-ink-800",
            p.locked && "cursor-not-allowed opacity-60",
          )}
          style={on ? { background: color } : undefined}
        >
          <span
            className={cn(
              "block h-[10px] w-[10px] rounded-full bg-ink-950 transition-transform duration-200",
              on ? "translate-x-[13px]" : "translate-x-[2px]",
            )}
            style={on ? { background: "#0b0e14" } : { background: "#3d4b5c" }}
          />
        </button>
        <button type="button" onClick={onOpen} className="min-w-0 flex-1 text-left">
          <div className="flex items-baseline gap-2">
            <span className={cn("font-display text-[13px] font-bold", on ? "text-paper-50" : "text-paper-300")}>
              {p.name}
            </span>
            <span className="tabular font-mono text-[9px] text-ink-500">v{p.version}</span>
          </div>
          <div className="mt-0.5 flex items-center gap-1.5 font-mono text-[9px]">
            <span
              className="rounded-[2px] px-1 uppercase tracking-wider"
              style={{ color, background: `${color}1a` }}
            >
              {p.kind}
            </span>
            <span className="truncate text-ink-500">{p.crate}</span>
          </div>
        </button>
        <span
          className={cn(
            "mt-1 font-mono text-[8.5px] uppercase tracking-wider",
            on ? "text-lime-signal" : "text-ink-500",
          )}
        >
          {p.locked ? "static" : on ? "live" : "off"}
        </span>
      </div>
      {open && (
        <div className="border-t border-ink-750 px-2.5 py-2">
          <p className="text-[11px] leading-relaxed text-paper-300">{p.desc}</p>
          <div className="mt-2 grid grid-cols-2 gap-x-3 gap-y-1 font-mono text-[9px]">
            <Meta k="author" v={p.author} />
            <Meta k="size" v={p.size} />
            <Meta k="load" v={`${p.loadMs} ms`} />
            <Meta k="deps" v={p.deps.length ? p.deps.join(", ") : "—"} />
          </div>
          {p.systems.length > 0 && (
            <div className="mt-2">
              <div className="font-mono text-[8.5px] uppercase tracking-[0.18em] text-ink-500">
                systems
              </div>
              <div className="mt-1 flex flex-wrap gap-1">
                {p.systems.map((s) => (
                  <code key={s} className="border border-ink-700 bg-ink-950 px-1 py-[1px] font-mono text-[9px] text-cyan-signal/85">
                    {s}
                  </code>
                ))}
              </div>
            </div>
          )}
          {(p.components.length > 0 || p.resources.length > 0) && (
            <div className="mt-2">
              <div className="font-mono text-[8.5px] uppercase tracking-[0.18em] text-ink-500">
                registers
              </div>
              <div className="mt-1 flex flex-wrap gap-1">
                {[...p.components.map((x) => `#${x}`), ...p.resources.map((x) => `@${x}`)].map((s) => (
                  <code key={s} className="border border-ink-700 bg-ink-950 px-1 py-[1px] font-mono text-[9px] text-amber-signal/85">
                    {s}
                  </code>
                ))}
              </div>
            </div>
          )}
          {depsMissing.length > 0 && (
            <div className="mt-2 border border-red-signal/40 bg-red-signal/10 px-2 py-1 font-mono text-[9px] text-red-signal">
              unresolved dependency: {depsMissing.join(", ")}
            </div>
          )}
        </div>
      )}
    </div>
  );
}

function Meta({ k, v }: { k: string; v: string }) {
  return (
    <div className="flex gap-1.5">
      <span className="text-ink-500">{k}</span>
      <span className="truncate text-paper-300">{v}</span>
    </div>
  );
}

/* ------------------------------------------------------------------ */
/* Console                                                             */
/* ------------------------------------------------------------------ */

const HELP = [
  "help                     this list",
  "open <brg220|mfl6b|gbxl4>  load a document",
  "backend <vulkan|dx12|metal|gl>",
  "load <plugin_id>         mount an addon",
  "unload <plugin_id>",
  "tess <0|1|2>             chord tolerance",
  "section <on|off> [x|y|z]",
  "stats                    pipeline summary",
];

function ConsolePanel() {
  const { log, pushLog, setBackend, setPartId, toggle, has, setLod, setSection, stats, backend, part } =
    useEngine();
  const [cmd, setCmd] = useState("");
  const endRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    endRef.current?.scrollIntoView({ block: "end" });
  }, [log]);

  const run = (raw: string) => {
    const line = raw.trim();
    if (!line) return;
    pushLog("info", `❯ ${line}`);
    const [c, ...rest] = line.split(/\s+/);
    const arg = rest[0];
    switch (c.toLowerCase()) {
      case "help":
        HELP.forEach((h) => pushLog("info", h));
        break;
      case "open":
        if (["brg220", "mfl6b", "gbxl4"].includes(arg)) setPartId(arg);
        else pushLog("err", `unknown document "${arg ?? ""}"`);
        break;
      case "backend":
        if (["vulkan", "dx12", "metal", "gl"].includes(arg)) setBackend(arg as never);
        else pushLog("err", `unknown backend "${arg ?? ""}"`);
        break;
      case "load":
        if (PLUGINS.some((p) => p.id === arg)) {
          if (!has(arg)) toggle(arg);
          else pushLog("warn", `${arg} already mounted`);
        } else pushLog("err", `no manifest for "${arg ?? ""}"`);
        break;
      case "unload":
        if (PLUGINS.some((p) => p.id === arg)) {
          if (has(arg)) toggle(arg);
          else pushLog("warn", `${arg} is not loaded`);
        } else pushLog("err", `no manifest for "${arg ?? ""}"`);
        break;
      case "tess":
        if (["0", "1", "2"].includes(arg)) setLod(Number(arg));
        else pushLog("err", "tess expects 0 | 1 | 2");
        break;
      case "section":
        if (arg === "on" || arg === "off") setSection({ on: arg === "on" });
        if (rest[1] && ["x", "y", "z"].includes(rest[1]))
          setSection({ axis: rest[1] as "x" | "y" | "z" });
        break;
      case "stats":
        pushLog(
          "ok",
          `${backend.label} · ${stats.tris} tris · ${stats.drawn} draws · ${stats.culled} culled · ${stats.fps.toFixed(0)} fps`,
        );
        break;
      default:
        pushLog("err", `command not found: ${c} (try 'help')`);
    }
  };

  const colors: Record<string, string> = {
    info: "text-paper-300",
    ok: "text-lime-signal",
    warn: "text-amber-signal",
    err: "text-red-signal",
  };

  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="thin-scroll min-h-0 flex-1 overflow-y-auto p-2 font-mono text-[10px] leading-[1.65]">
        {log.map((l) => (
          <div key={l.id} className="log-line flex gap-2 whitespace-pre-wrap">
            <span className="shrink-0 text-ink-600">{l.stamp}</span>
            <span className={colors[l.level]}>{l.text}</span>
          </div>
        ))}
        <div ref={endRef} />
      </div>
      <div className="shrink-0 border-t border-ink-700 bg-ink-950 p-2">
        <div className="mb-1.5 font-mono text-[9px] text-ink-500">
          {part.code}.anvil · {backend.short} · type <span className="text-cyan-signal">help</span>
        </div>
        <form
          onSubmit={(e) => {
            e.preventDefault();
            run(cmd);
            setCmd("");
          }}
          className="flex items-center gap-2 border border-ink-700 bg-ink-900 px-2 py-1 focus-within:border-cyan-signal/50"
        >
          <span className="font-mono text-[11px] text-amber-signal">❯</span>
          <input
            value={cmd}
            onChange={(e) => setCmd(e.target.value)}
            spellCheck={false}
            autoComplete="off"
            placeholder="backend dx12"
            className="w-full bg-transparent font-mono text-[11px] text-paper-100 outline-none placeholder:text-ink-600"
          />
          <span className="inline-block h-3.5 w-[7px] animate-blink bg-cyan-signal/70" />
        </form>
        <div className="mt-1.5 flex flex-wrap gap-1">
          {["help", "backend dx12", "load analysis.fem", "section on", "tess 2", "open mfl6b"].map(
            (s) => (
              <button
                key={s}
                type="button"
                onClick={() => run(s)}
                className="border border-ink-700 px-1.5 py-[2px] font-mono text-[9px] text-ink-400 transition-colors hover:border-cyan-signal/50 hover:text-cyan-signal"
              >
                {s}
              </button>
            ),
          )}
        </div>
      </div>
    </div>
  );
}
