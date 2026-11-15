export interface Snippet {
  id: string;
  file: string;
  title: string;
  blurb: string;
  code: string;
}

export const SNIPPETS: Snippet[] = [
  {
    id: "plugin",
    file: "crates/anvil_plugin/src/lib.rs",
    title: "Plugin engine",
    blurb:
      "Addons are trait objects discovered from manifests, resolved through a dependency DAG and injected into the Bevy schedule — no core rebuild, no FFI marshalling of geometry.",
    code: `use bevy::{ecs::schedule::ScheduleLabel, prelude::*};
use std::{any::Any, collections::HashMap, path::PathBuf, sync::Arc};

/// Everything an addon may hand back to the engine.
pub trait AnvilPlugin: Any + Send + Sync {
    fn manifest(&self) -> Manifest;

    /// Called once after dependency resolution, before the schedule rebuilds.
    fn build(&self, app: &mut App, ctx: &mut PluginCtx);

    /// Optional egui dock contributions (panels, toolbars, inspectors).
    fn panels(&self) -> Vec<PanelSpec> { Vec::new() }

    /// Hot reload hook: keep component data, swap system pointers.
    fn on_reload(&mut self, _ctx: &mut PluginCtx) {}
}

#[derive(Resource, Default)]
pub struct PluginRegistry {
    loaded: HashMap<PluginId, Arc<dyn AnvilPlugin>>,
    /// Topological order derived from Manifest::requires.
    order: Vec<PluginId>,
}

impl PluginRegistry {
    /// Rejects cycles, missing deps and ABI mismatches before mounting.
    pub fn resolve(&mut self, m: &Manifest) -> Result<PluginId, ResolveError> {
        for dep in &m.requires {
            if !self.loaded.contains_key(dep) { return Err(ResolveError::Missing(*dep)); }
        }
        if self.depends_on_cycle(m.id, &m.requires) { return Err(ResolveError::Cycle); }
        self.loaded.insert(m.id, Arc::new(m.instantiate()?));
        self.order = topo_sort(&self.loaded)?;
        Ok(m.id)
    }

    pub fn mount_into(&self, app: &mut App) {
        let mut ctx = PluginCtx::new(app.world_mut());
        for id in &self.order {
            let plugin = &self.loaded[id];
            let span = tracing::info_span!("plugin", id = %id);
            let _guard = span.enter();
            plugin.build(app, &mut ctx);
            for panel in plugin.panels() { ctx.dock(panel); }
        }
        ctx.flush_panels();
    }
}

/// Scans ~/.anvil/plugins/*.manifest.ron at PreStartup and queues loads.
fn discover_manifests(mut cmds: Commands, root: Res<PluginRoot>) {
    for entry in root.dir().read_dir().flatten().flatten() {
        let Ok(manifest) = Manifest::from_file(&entry.path()) else {
            warn!(path = ?entry.path(), "skipping malformed manifest");
            continue;
        };
        cmds.queue(LoadPlugin { manifest, path: PathBuf::from(entry.path()) });
    }
}

#[macro_export]
macro_rules! anvil_addon {
    ($ty:ty, $id:expr, $ver:expr $(, requires = [$($dep:expr),*])?) => {
        #[no_mangle]
        pub extern "C" fn __anvil_abi() -> u32 { $crate::ABI_VERSION }

        #[no_mangle]
        pub extern "C" fn __anvil_register(reg: &mut $crate::PluginRegistry) {
            let m = $crate::Manifest::new($id, $ver)
                $(.with_requires(&[$($dep),*]))?;
            reg.mount($crate::boxed::<$ty>(m));
        }
    };
}`,
  },
  {
    id: "device",
    file: "crates/anvil_render/src/device.rs",
    title: "wgpu device",
    blurb:
      "One code path for Vulkan and DX12: wgpu-hal picks the backend, we only negotiate features and limits that CAD workloads actually need.",
    code: `use anyhow::{anyhow, Result};
use wgpu::{
    AdapterInfo, Backends, DeviceDescriptor, Features, InstanceDescriptor, Limits,
    MemoryHints, PowerPreference, RequestAdapterOptions, Surface,
};

pub enum Api { Vulkan, Dx12, Metal, Auto }

pub struct RenderConfig {
    pub api: Api,
    pub debug: bool,
    pub software: bool,
    pub msaa: u32,
}

/// Negotiates an adapter + device for the requested graphics API.
pub async fn create_device(cfg: &RenderConfig, surface: &Surface<'_>) -> Result<GpuDevice> {
    let instance = wgpu::Instance::new(&InstanceDescriptor {
        backends: match cfg.api {
            Api::Vulkan => Backends::VULKAN,
            Api::Dx12 => Backends::DX12,
            Api::Metal => Backends::METAL,
            Api::Auto => Backends::PRIMARY,
        },
        flags: wgpu::InstanceFlags::from_build_config().with(
            wgpu::InstanceFlags::VALIDATION, cfg.debug,
        ),
        ..Default::default()
    });

    let adapter = instance
        .request_adapter(&RequestAdapterOptions {
            power_preference: PowerPreference::HighPerformance,
            compatible_surface: Some(surface),
            force_fallback_adapter: cfg.software,
        })
        .await
        .ok_or_else(|| anyhow!("no adapter exposed for {:?}", cfg.api))?;

    let AdapterInfo { name, driver, backend, .. } = adapter.get_info();
    info!(%name, %driver, ?backend, "adapter acquired");

    let (device, queue) = adapter
        .request_device(
            &DeviceDescriptor {
                label: Some("anvil::cad"),
                // Large mesh buffers, indirect draws and timestamp queries.
                required_features: Features::MULTI_DRAW_INDIRECT
                    | Features::TIMESTAMP_QUERY
                    | Features::TEXTURE_COMPRESSION_BC
                    | Features::BGRA8UNORM_STORAGE,
                required_limits: Limits {
                    max_storage_buffer_binding_size: 1 << 29,
                    max_push_constant_size: 128,
                    ..Limits::default()
                },
                memory_hints: MemoryHints::Performance,
            },
            None,
        )
        .await?;

    Ok(GpuDevice {
        device,
        queue,
        backend,
        pipelines: PipelineCache::new(&device, cfg.msaa),
        frames: FrameRing::new(3),
    })
}`,
  },
  {
    id: "section",
    file: "addons/anvil_tool_section/src/lib.rs",
    title: "An addon end-to-end",
    blurb:
      "Ninety lines: a component, a resource, three systems and one egui panel. The section plane clips in the vertex stage and emits cap edges as a separate primitive.",
    code: `use anvil_plugin::{anvil_addon, AnvilPlugin, Manifest, PanelSpec, PluginCtx};
use bevy::prelude::*;

anvil_addon!(SectionPlugin, "tool.section_plane", "0.6.0",
    requires = ["geom.step_kernel"]);

#[derive(Component, Clone, Copy)]
pub struct SectionPlane {
    pub normal: Vec3,
    pub offset: f32,
    pub flip: bool,
}

#[derive(Resource)]
pub struct ClipParams {
    pub plane: Vec4,
    pub cap_edges: bool,
}

pub struct SectionPlugin;

impl AnvilPlugin for SectionPlugin {
    fn manifest(&self) -> Manifest { SectionPlugin::MANIFEST }

    fn build(&self, app: &mut App, ctx: &mut PluginCtx) {
        app.register_component::<SectionPlane>()
            .insert_resource(ClipParams { plane: Vec4::X, cap_edges: true })
            .add_systems(Update, (track_gizmo, clip_meshes, emit_cap_edges).chain())
            .add_systems(PostUpdate, upload_clip_uniform);

        ctx.dock(PanelSpec::right("Section", section_ui));
        ctx.toolbar(Icon::Section, "toggle_section_plane");
    }
}

/// Sutherland-Hodgman against the negative half-space, per triangle.
fn clip_meshes(params: Res<ClipParams>, mut q: Query<(&MeshHandle, &mut ClippedMesh)>) {
    for (handle, mut out) in &mut q {
        out.clear();
        for tri in handle.triangles() {
            let Some(poly) = clip_half_space(tri, params.plane) else { continue };
            out.push_fill(&poly);
            if params.cap_edges {
                out.push_cap_edges(&poly);
            }
        }
    }
}

fn section_ui(ui: &mut egui::Ui, mut plane: SectionPlane) {
    ui.horizontal(|ui| {
        ui.label("axis");
        ui.selectable_value(&mut plane.normal, Vec3::X, "X");
        ui.selectable_value(&mut plane.normal, Vec3::Y, "Y");
        ui.selectable_value(&mut plane.normal, Vec3::Z, "Z");
    });
    ui.add(egui::Slider::new(&mut plane.offset, -40.0..=40.0).text("mm"));
    ui.checkbox(&mut plane.flip, "invert half-space");
}`,
  },
  {
    id: "graph",
    file: "crates/anvil_render/src/graph.rs",
    title: "Render graph node",
    blurb:
      "The CAD pass is a bevy_render graph node, so addons can splice themselves in front of or behind it without touching the core renderer.",
    code: `use bevy::render::{
    render_graph::{Node, NodeRunError, RenderGraphContext},
    render_resource::*,
    renderer::RenderContext,
};

#[derive(RenderLabel, Debug, Hash, PartialEq, Eq, Clone)]
pub struct CadMainPass;

pub struct CadRenderNode;

impl Node for CadRenderNode {
    fn update(&mut self, world: &World) {
        // Rebuild PSOs only when a material or LOD changes.
        if world.resource::<PipelineDirty>().0 {
            self.pipelines = None;
        }
    }

    fn run<'w>(
        &self,
        _graph: &mut RenderGraphContext,
        ctx: &mut RenderContext<'w>,
        world: &'w World,
    ) -> Result<(), NodeRunError> {
        let draws = world.resource::<DrawList>();
        if draws.is_empty() { return Ok(()); }

        let mut encoder = ctx.command_encoder();
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("anvil::cad::main"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: draws.target_view(),
                resolve_target: draws.msaa_resolve(),
                ops: Operations {
                    load: LoadOp::Clear(WgpuColor::from_hex("#0b0e14")),
                    store: StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(RenderPassDepthStencilAttachment {
                view: draws.depth_view(),
                depth_ops: Some(Operations { load: LoadOp::Clear(1.0), store: StoreOp::Discard }),
                stencil_ops: Some(Operations { load: LoadOp::Clear(0), store: StoreOp::Discard }),
            }),
            ..Default::default()
        });

        pass.set_bind_group(0, draws.view_bindings(), &[]);
        for batch in draws.sorted_batches() {
            pass.set_pipeline(batch.pipeline());
            pass.set_vertex_buffer(0, batch.vertices.slice(..));
            pass.set_index_buffer(batch.indices.slice(..), IndexFormat::Uint32);
            pass.draw_indexed(0..batch.index_count, 0, 0..batch.instances);
        }
        Ok(())
    }
}`,
  },
];

export const SHELL_STEPS: { n: string; title: string; body: string; code?: string }[] = [
  {
    n: "01",
    title: "Scaffold",
    body: "The CLI generates a crate wired against the engine ABI, plus a manifest the registry can resolve.",
    code: "cargo install anvil-cli\nanvil new my_addon --kind tool\ncd my_addon && cargo anvil check",
  },
  {
    n: "02",
    title: "Declare data",
    body: "Components live in the Bevy World alongside core geometry. Register them so hot reload can migrate storage.",
    code: "app.register_component::<MyTag>()\n   .insert_resource(MyConfig::default());",
  },
  {
    n: "03",
    title: "Add systems",
    body: "Systems are plain Bevy systems, ordered against the engine's stage labels: PreCad, Cad, PostCad.",
    code: "app.add_systems(Update, my_pass.after(CadStage::Clip));",
  },
  {
    n: "04",
    title: "Ship the panel",
    body: "Return an egui panel spec and the shell docks it, persists its layout and routes input to the viewport.",
    code: "ctx.dock(PanelSpec::right(\"My Tool\", my_ui));",
  },
  {
    n: "05",
    title: "Hot reload",
    body: "Drop the .so/.dll into ~/.anvil/plugins — the file watcher rebuilds pipelines and keeps entity data alive.",
    code: "anvil watch --target release",
  },
];
