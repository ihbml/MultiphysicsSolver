import {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useRef,
  useState,
  type ReactNode,
  type RefObject,
} from "react";
import { buildPart, type PartDef } from "@/engine/parts";
import type { FrameStats } from "@/engine/render";

export type BackendId = "vulkan" | "dx12" | "metal" | "gl";

export interface Backend {
  id: BackendId;
  label: string;
  short: string;
  adapter: string;
  hal: string;
  color: string;
  tier: "primary" | "fallback";
  features: string[];
}

export const BACKENDS: Backend[] = [
  {
    id: "vulkan",
    label: "Vulkan 1.3",
    short: "VK",
    adapter: "AMD Radeon RX 7900 XTX · RADV NAVI31 · driver 24.2.7",
    hal: "wgpu-hal → ash 0.38 · SPIR-V 1.6",
    color: "#f5a524",
    tier: "primary",
    features: [
      "multi_draw_indirect_count",
      "timestamp_query",
      "rg11b10ufloat_renderable",
      "texture_compression_bc",
      "subgroup_barrier",
    ],
  },
  {
    id: "dx12",
    label: "Direct3D 12",
    short: "DX12",
    adapter: "NVIDIA GeForce RTX 4080 · AgilitySDK 1.616 · WDDM 3.1",
    hal: "wgpu-hal → windows-rs 0.58 · DXIL SM 6.7",
    color: "#35c9e8",
    tier: "primary",
    features: [
      "shader_model_6_7",
      "wave_intrinsics",
      "typed_uav_load",
      "mesh_shader_tier1",
      "enhanced_barrier",
    ],
  },
  {
    id: "metal",
    label: "Metal 3",
    short: "MTL",
    adapter: "Apple M3 Pro · Metal 3 · 18-core GPU",
    hal: "wgpu-hal → metal-rs · MSL 2.4",
    color: "#8ede63",
    tier: "fallback",
    features: ["argument_buffers_t2", "barycentric_coords", "tile_shading"],
  },
  {
    id: "gl",
    label: "OpenGL 4.6",
    short: "GL",
    adapter: "llvmpipe (software) · Mesa 24.3",
    hal: "wgpu-hal → glow · GLSL 460",
    color: "#8b7fd6",
    tier: "fallback",
    features: ["legacy_fallback_only"],
  },
];

export type PluginKind = "core" | "geometry" | "render" | "tool" | "analysis" | "debug";

export const KIND_COLOR: Record<PluginKind, string> = {
  core: "#35c9e8",
  geometry: "#8ede63",
  render: "#f5a524",
  tool: "#8b7fd6",
  analysis: "#ff6b5a",
  debug: "#93a4b6",
};

export interface PluginDef {
  id: string;
  name: string;
  crate: string;
  version: string;
  author: string;
  kind: PluginKind;
  size: string;
  desc: string;
  systems: string[];
  components: string[];
  resources: string[];
  deps: string[];
  default: boolean;
  locked?: boolean;
  loadMs: number;
}

export const PLUGINS: PluginDef[] = [
  {
    id: "backend.vulkan",
    name: "Vulkan Device",
    crate: "anvil_backend_vulkan",
    version: "0.9.3",
    author: "core",
    kind: "core",
    size: "412 kB",
    desc: "VkInstance / VkDevice provider. Registers the surface, swapchain and queue families with wgpu-hal, then hands a `GpuDevice` resource back to the schedule.",
    systems: ["vk::poll_events", "vk::acquire_swapchain", "vk::submit_and_present"],
    components: [],
    resources: ["GpuDevice", "VkQueueFamily", "Swapchain"],
    deps: ["shell.egui_dock"],
    default: true,
    locked: true,
    loadMs: 84,
  },
  {
    id: "backend.dx12",
    name: "DX12 Device",
    crate: "anvil_backend_dx12",
    version: "0.9.3",
    author: "core",
    kind: "core",
    size: "468 kB",
    desc: "IDXGIFactory6 adapter enumeration, root-signature cache and enhanced barriers. Same `GpuDevice` trait object, so no engine code changes when switching API.",
    systems: ["dx12::enumerate_adapters", "dx12::record_command_list", "dx12::execute_and_present"],
    components: [],
    resources: ["GpuDevice", "RootSignatureCache", "DescriptorHeap"],
    deps: ["shell.egui_dock"],
    default: true,
    locked: true,
    loadMs: 96,
  },
  {
    id: "shell.egui_dock",
    name: "egui Shell",
    crate: "anvil_shell_egui",
    version: "0.9.3",
    author: "core",
    kind: "core",
    size: "1.2 MB",
    desc: "Immediate-mode dock host. Every addon declares panels through `EguiPanel` and the shell lays them out, persists state and forwards input to the viewport.",
    systems: ["egui::begin_pass", "egui::layout_docks", "egui::paint_to_texture"],
    components: ["EguiPanel", "DockNode"],
    resources: ["EguiCtx", "DockLayout"],
    deps: [],
    default: true,
    locked: true,
    loadMs: 41,
  },
  {
    id: "geom.step_kernel",
    name: "STEP / B-rep Kernel",
    crate: "anvil_step_kernel",
    version: "0.7.1",
    author: "geometry guild",
    kind: "geometry",
    size: "3.8 MB",
    desc: "AP242 reader bridging OpenCascade over FFI. Converts B-rep faces to triangle soups and drives the chord-height tessellation slider.",
    systems: ["step::read_ap242", "step::tessellate_brep", "step::publish_mesh"],
    components: ["BrepFace", "Tessellation", "MeshHandle"],
    resources: ["OccKernel", "TessQuality"],
    deps: ["shell.egui_dock"],
    default: true,
    loadMs: 212,
  },
  {
    id: "import.dxf",
    name: "DXF Import",
    crate: "anvil_import_dxf",
    version: "0.4.2",
    author: "community",
    kind: "geometry",
    size: "188 kB",
    desc: "Parses DXF R12–R2018 entities (LWPOLYLINE, ARC, SPLINE) into a flat 2D profile that is projected onto the build plane for nesting and sketch reference.",
    systems: ["dxf::parse_entities", "dxf::flatten_splines", "dxf::draw_profile"],
    components: ["DxfProfile", "SketchLayer"],
    resources: ["DxfCache"],
    deps: ["geom.step_kernel"],
    default: true,
    loadMs: 27,
  },
  {
    id: "tool.section_plane",
    name: "Section Plane",
    crate: "anvil_tool_section",
    version: "0.6.0",
    author: "core",
    kind: "tool",
    size: "96 kB",
    desc: "Half-space clipper running in the vertex stage. Emits cap edges as a separate primitive so the cut face can be hatched and measured.",
    systems: ["section::update_plane", "section::clip_meshes", "section::emit_cap_edges"],
    components: ["SectionPlane", "CapEdge"],
    resources: ["ClipParams"],
    deps: ["geom.step_kernel"],
    default: true,
    loadMs: 18,
  },
  {
    id: "tool.measure",
    name: "Measure & Dimensions",
    crate: "anvil_tool_measure",
    version: "0.5.4",
    author: "community",
    kind: "tool",
    size: "142 kB",
    desc: "Snaps to edges, vertices and face centres, then draws ISO-129 dimension lines with witness lines and arrowheads in a screen-space overlay pass.",
    systems: ["measure::snap_query", "measure::build_dimensions", "measure::draw_overlay"],
    components: ["Dimension", "SnapTarget"],
    resources: ["MeasureTool"],
    deps: ["geom.step_kernel"],
    default: true,
    loadMs: 22,
  },
  {
    id: "overlay.gdt",
    name: "GD&T Overlay",
    crate: "anvil_overlay_gdt",
    version: "0.3.8",
    author: "metrology sig",
    kind: "tool",
    size: "210 kB",
    desc: "Renders ISO 1101 feature control frames and datum leaders anchored in 3D, with occlusion-aware screen-space billboarding.",
    systems: ["gdt::collect_frames", "gdt::anchor_to_geometry", "gdt::draw_frames"],
    components: ["DatumFrame", "DatumFeature", "LeaderLine"],
    resources: ["GdtRegistry"],
    deps: ["tool.measure"],
    default: false,
    loadMs: 31,
  },
  {
    id: "analysis.fem",
    name: "FEM Static Solver",
    crate: "anvil_analysis_fem",
    version: "0.2.6",
    author: "simulation sig",
    kind: "analysis",
    size: "2.4 MB",
    desc: "P1 tetrahedral linear-static solve on a compute queue. Writes a von-Mises scalar per vertex which the renderer maps through the σ colour ramp.",
    systems: ["fem::build_tet_mesh", "fem::assemble_stiffness", "fem::solve_cg", "fem::write_stress"],
    components: ["TetMesh", "BoundaryCondition", "StressField"],
    resources: ["ComputePipe", "SolverParams"],
    deps: ["geom.step_kernel", "backend.dx12"],
    default: false,
    loadMs: 388,
  },
  {
    id: "geom.boolean_ops",
    name: "Boolean / CSG",
    crate: "anvil_geom_boolean",
    version: "0.1.9",
    author: "geometry guild",
    kind: "geometry",
    size: "640 kB",
    desc: "Exact-ish boolean union, difference and intersection on BSP trees with fuzzy epsilon welding — used for quick fixture and pocket studies.",
    systems: ["csg::build_bsp", "csg::classify_polygons", "csg::reweld_vertices"],
    components: ["BooleanOp", "BspNode"],
    resources: ["CsgWorkspace"],
    deps: ["geom.step_kernel"],
    default: false,
    loadMs: 57,
  },
  {
    id: "render.pbr",
    name: "PBR Materials",
    crate: "anvil_render_pbr",
    version: "0.8.2",
    author: "core",
    kind: "render",
    size: "310 kB",
    desc: "Metallic-roughness BRDF with IBL from a prefiltered environment map, Fresnel-Schlick rim and GGX specular on the forward+ cluster pass.",
    systems: ["pbr::upload_materials", "pbr::prefilter_env", "pbr::cluster_lights"],
    components: ["PbrMaterial", "EnvProbe"],
    resources: ["BrdfLut", "IblCubemap"],
    deps: ["shell.egui_dock"],
    default: true,
    loadMs: 63,
  },
  {
    id: "debug.gpu_inspector",
    name: "GPU Inspector",
    crate: "anvil_debug_inspector",
    version: "0.4.0",
    author: "core",
    kind: "debug",
    size: "178 kB",
    desc: "Timestamp queries per pass, render-graph capture, pipeline-barrier log and live draw-call counters piped into the egui overlay.",
    systems: ["insp::begin_pass_query", "insp::resolve_timestamps", "insp::capture_graph"],
    components: ["PassQuery", "DrawCall"],
    resources: ["FrameCapture", "ProfilerRing"],
    deps: ["shell.egui_dock"],
    default: false,
    loadMs: 24,
  },
];

export interface LogLine {
  id: number;
  stamp: string;
  level: "info" | "ok" | "warn" | "err";
  text: string;
}

export interface CamState {
  yaw: number;
  pitch: number;
  dist: number;
  target: [number, number, number];
  spin: boolean;
}

export interface ViewFlags {
  shaded: boolean;
  edges: boolean;
  wireframe: boolean;
  grid: boolean;
  gizmo: boolean;
  xray: boolean;
}

export interface SectionState {
  on: boolean;
  axis: "x" | "y" | "z";
  pos: number;
}

export interface LiveStats extends FrameStats {
  fps: number;
  cpuMs: number;
  gpuMs: number;
}

interface EngineCtx {
  part: PartDef;
  partId: string;
  setPartId: (id: string) => void;
  lod: number;
  setLod: (n: number) => void;
  backend: Backend;
  setBackend: (id: BackendId) => void;
  enabled: string[];
  has: (id: string) => boolean;
  toggle: (id: string) => void;
  view: ViewFlags;
  setView: (patch: Partial<ViewFlags>) => void;
  section: SectionState;
  setSection: (patch: Partial<SectionState>) => void;
  selected: string | null;
  setSelected: (id: string | null) => void;
  hovered: string | null;
  setHovered: (id: string | null) => void;
  cam: RefObject<CamState>;
  stats: LiveStats;
  setStats: (s: LiveStats) => void;
  log: LogLine[];
  pushLog: (level: LogLine["level"], text: string) => void;
}

const Ctx = createContext<EngineCtx | null>(null);

const stamp = () => {
  const d = new Date();
  return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}:${String(
    d.getSeconds(),
  ).padStart(2, "0")}.${String(d.getMilliseconds()).padStart(3, "0")}`;
};

const SEED: Omit<LogLine, "id">[] = [
  { stamp: stamp(), level: "ok", text: "anvil 0.9.3-rc · wgpu 24.0 · bevy 0.16 · egui 0.31" },
  { stamp: stamp(), level: "info", text: "request_adapter(limits=high_performance) → Vulkan 1.3" },
  { stamp: stamp(), level: "ok", text: "device ready in 84 ms · 3 queue families · 8 GiB VRAM" },
  { stamp: stamp(), level: "info", text: "plugin registry: 12 manifests resolved, 7 loaded" },
];

export function EngineProvider({ children }: { children: ReactNode }) {
  const [partId, setPartIdRaw] = useState<string>("brg220");
  const [lod, setLodRaw] = useState(1);
  const [backendId, setBackendId] = useState<BackendId>("vulkan");
  const [enabled, setEnabled] = useState<string[]>(
    PLUGINS.filter((p) => p.default).map((p) => p.id),
  );
  const [view, setViewRaw] = useState<ViewFlags>({
    shaded: true,
    edges: true,
    wireframe: false,
    grid: true,
    gizmo: true,
    xray: false,
  });
  const [section, setSectionRaw] = useState<SectionState>({ on: false, axis: "x", pos: 0.2 });
  const [selected, setSelected] = useState<string | null>("boss_column");
  const [hovered, setHovered] = useState<string | null>(null);
  const [log, setLog] = useState<LogLine[]>(SEED.map((l, i) => ({ ...l, id: i })));
  const [stats, setStats] = useState<LiveStats>({
    tris: 0,
    drawn: 0,
    culled: 0,
    clipped: 0,
    lines: 0,
    ms: 0,
    picked: null,
    fps: 60,
    cpuMs: 4.2,
    gpuMs: 2.1,
  });
  const logId = useRef(SEED.length + 100);

  const cam = useRef<CamState>({ yaw: 0.66, pitch: 0.4, dist: 9.6, target: [0, 0.05, 0], spin: true });

  const pushLog = useCallback(
    (level: LogLine["level"], text: string) => {
      const id = ++logId.current;
      setLog((prev) => [...prev.slice(-60), { id, stamp: stamp(), level, text }]);
    },
    [],
  );

  const part = useMemo(() => buildPart(partId, lod), [partId, lod]);
  const backend = useMemo(
    () => BACKENDS.find((b) => b.id === backendId) ?? BACKENDS[0],
    [backendId],
  );

  const setPartId = useCallback(
    (id: string) => {
      const p = buildPart(id, lod);
      setPartIdRaw(id);
      cam.current.yaw = p.home.yaw;
      cam.current.pitch = p.home.pitch;
      cam.current.dist = p.home.dist;
      cam.current.target = [...p.home.target] as [number, number, number];
      setSelected(null);
      setSectionRaw((s) => ({ ...s, pos: 0.2 }));
      pushLog("info", `open_document("${p.code}.step") · ${p.solids.length} solids · ${p.mass}`);
    },
    [lod, pushLog],
  );

  const setLod = useCallback(
    (n: number) => {
      setLodRaw(n);
      pushLog("info", `tess_quality = ${["Coarse", "Standard", "Fine"][n]} (chord ${LOD_NOTE[n]})`);
    },
    [pushLog],
  );

  const setBackend = useCallback(
    (id: BackendId) => {
      const b = BACKENDS.find((x) => x.id === id);
      if (!b) return;
      setBackendId(id);
      pushLog(
        b.tier === "primary" ? "ok" : "warn",
        `request_adapter(${id}) → ${b.adapter.split(" · ")[0]} · pipelines rebuilt in ${
          38 + Math.round(Math.random() * 40)
        } ms`,
      );
    },
    [pushLog],
  );

  const enabledRef = useRef(enabled);
  enabledRef.current = enabled;

  const toggle = useCallback(
    (id: string) => {
      const def = PLUGINS.find((p) => p.id === id);
      if (!def || def.locked) return;
      const prev = enabledRef.current;

      if (prev.includes(id)) {
        const cascade = PLUGINS.filter(
          (p) => !p.locked && p.deps.includes(id) && prev.includes(p.id),
        ).map((p) => p.id);
        pushLog("warn", `unload ${def.crate} v${def.version} · -${def.systems.length} systems`);
        if (cascade.length) {
          pushLog("warn", `cascade: ${cascade.map((c) => PLUGINS.find((p) => p.id === c)?.crate).join(", ")} unloaded`);
        }
        setEnabled(prev.filter((x) => x !== id && !cascade.includes(x)));
        return;
      }

      const missing = def.deps.filter((d) => !prev.includes(d));
      if (missing.length) {
        pushLog("err", `${def.crate}: unresolved dependency ${missing.join(", ")} — load refused`);
        return;
      }
      pushLog(
        "ok",
        `load ${def.crate} v${def.version} · +${def.systems.length} systems · +${def.components.length} components · ${def.loadMs} ms`,
      );
      setEnabled([...prev, id]);
    },
    [pushLog],
  );

  const setView = useCallback((patch: Partial<ViewFlags>) => {
    setViewRaw((v) => ({ ...v, ...patch }));
  }, []);

  const setSection = useCallback((patch: Partial<SectionState>) => {
    setSectionRaw((s) => ({ ...s, ...patch }));
  }, []);

  const has = useCallback((id: string) => enabled.includes(id), [enabled]);

  const value: EngineCtx = {
    part,
    partId,
    setPartId,
    lod,
    setLod,
    backend,
    setBackend,
    enabled,
    has,
    toggle,
    view,
    setView,
    section,
    setSection,
    selected,
    setSelected,
    hovered,
    setHovered,
    cam,
    stats,
    setStats,
    log,
    pushLog,
  };

  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

const LOD_NOTE = ["0.18 mm", "0.06 mm", "0.02 mm"];

export function useEngine(): EngineCtx {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error("useEngine must be used inside <EngineProvider>");
  return ctx;
}
