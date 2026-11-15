use bevy_ecs::prelude::*;
use bevy_ecs::schedule::{IntoScheduleConfigs, ScheduleLabel, Schedules};
use bevy_ecs::system::ScheduleSystem;
use cad_core::{
    Aabb3, AssemblyPartSpec, CadMaterialData, CreaseConfig, ImportedAssembly, load_cad_file,
};
use glam::{Mat4, Vec3};

use crate::components::{
    AddonOwned, CadMaterial, CadPart, PartMassCache, PartTransform, PartVisibility, PickableId,
    SelectionState, WorldTransform,
};
use crate::resources::{
    AdapterDetails, AddonPanelRegistry, AddonRegistry, AddonStatusEntry, CadEvent, CadTime,
    CameraState, ExplodeState, MeshLibrary, MeasurementToolState, RenderOverlayQueue,
    RenderSettings, SceneCommand, SceneCommandQueue, SectionAxis, SectionPlaneState,
    ViewportStats,
};

#[derive(ScheduleLabel, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CadSchedule {
    Startup,
    PreUpdate,
    Update,
    PostUpdate,
    PreRender,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AddonKind {
    NativeRust,
    RhaiScript,
    DynamicAbi,
}

impl AddonKind {
    pub fn label(&self) -> &'static str {
        match self {
            Self::NativeRust => "Rust ECS Plugin",
            Self::RhaiScript => "Rhai Runtime Script",
            Self::DynamicAbi => "Dynamic ABI Addon",
        }
    }
}

#[derive(Debug, Clone)]
pub struct AddonDescriptor {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub kind: AddonKind,
}

pub trait CadAddon: Send + Sync + 'static {
    fn descriptor(&self) -> AddonDescriptor;
    fn build(&self, host: &mut CadPluginHost);
    fn on_ui(&self, _ui: &mut egui::Ui, _world: &mut World) {}
}

#[derive(Resource, Debug)]
struct NextPickId(u32);

impl Default for NextPickId {
    fn default() -> Self {
        Self(1)
    }
}

impl NextPickId {
    fn alloc(&mut self) -> u32 {
        let id = self.0.max(1);
        self.0 = id + 1;
        id
    }
}

pub struct CadPluginHost {
    pub world: World,
    addons: Vec<Box<dyn CadAddon>>,
    started: bool,
}

impl Default for CadPluginHost {
    fn default() -> Self {
        Self::new()
    }
}

impl CadPluginHost {
    pub fn new() -> Self {
        let mut world = World::new();
        world.init_resource::<MeshLibrary>();
        world.init_resource::<CameraState>();
        world.init_resource::<RenderSettings>();
        world.init_resource::<SectionPlaneState>();
        world.init_resource::<ExplodeState>();
        world.init_resource::<MeasurementToolState>();
        world.init_resource::<AdapterDetails>();
        world.init_resource::<ViewportStats>();
        world.init_resource::<RenderOverlayQueue>();
        world.init_resource::<SceneCommandQueue>();
        world.init_resource::<CadTime>();
        world.init_resource::<AddonPanelRegistry>();
        world.init_resource::<AddonRegistry>();
        world.init_resource::<Messages<CadEvent>>();
        world.init_resource::<NextPickId>();

        let mut schedules = Schedules::default();
        for label in [
            CadSchedule::Startup,
            CadSchedule::PreUpdate,
            CadSchedule::Update,
            CadSchedule::PostUpdate,
            CadSchedule::PreRender,
        ] {
            schedules.insert(Schedule::new(label));
        }
        world.insert_resource(schedules);

        let mut host = Self {
            world,
            addons: Vec::new(),
            started: false,
        };

        host.add_systems(CadSchedule::PreUpdate, process_scene_commands_system);
        host.add_systems(
            CadSchedule::PostUpdate,
            (
                explode_animation_system,
                sync_world_transforms_system,
                sync_isolation_system,
                measurement_overlay_system,
                compute_viewport_stats_system,
            )
                .chain(),
        );

        host
    }

    pub fn add_systems<M>(
        &mut self,
        schedule: CadSchedule,
        systems: impl IntoScheduleConfigs<ScheduleSystem, M>,
    ) -> &mut Self {
        let mut schedules = self.world.resource_mut::<Schedules>();
        schedules.add_systems(schedule, systems);
        self
    }

    pub fn register_addon(&mut self, addon: Box<dyn CadAddon>) {
        let desc = addon.descriptor();
        {
            let mut reg = self.world.resource_mut::<AddonRegistry>();
            reg.entries.push(AddonStatusEntry {
                id: desc.id.clone(),
                name: desc.name.clone(),
                version: desc.version.clone(),
                author: desc.author.clone(),
                description: desc.description.clone(),
                kind_label: desc.kind.label().to_string(),
                enabled: true,
                last_error: None,
                source_path: None,
                script_source: None,
            });
            reg.log(desc.id.clone(), format!("Loaded {}", desc.name), false);
        }
        addon.build(self);
        self.addons.push(addon);
    }

    pub fn startup(&mut self) {
        if !self.started {
            self.world.run_schedule(CadSchedule::Startup);
            self.started = true;
        }
    }

    pub fn tick(&mut self, dt: f32) {
        self.startup();

        {
            let mut time = self.world.resource_mut::<CadTime>();
            time.delta = dt.clamp(0.0, 0.25);
            time.elapsed += time.delta;
            time.frame_count += 1;
        }

        // Clear per-frame immediate overlay lines before schedules populate them
        self.world.resource_mut::<RenderOverlayQueue>().lines.clear();

        self.world.run_schedule(CadSchedule::PreUpdate);
        self.world.run_schedule(CadSchedule::Update);
        self.world.run_schedule(CadSchedule::PostUpdate);
        self.world.run_schedule(CadSchedule::PreRender);

        self.world.resource_mut::<Messages<CadEvent>>().update();
    }

    pub fn render_addon_uis(&mut self, ui: &mut egui::Ui) {
        let enabled_ids: Vec<String> = {
            let reg = self.world.resource::<AddonRegistry>();
            reg.entries
                .iter()
                .filter(|e| e.enabled)
                .map(|e| e.id.clone())
                .collect()
        };

        for addon in &self.addons {
            let desc = addon.descriptor();
            if enabled_ids.iter().any(|id| id == &desc.id) {
                egui::CollapsingHeader::new(format!("{} (v{})", desc.name, desc.version))
                    .default_open(true)
                    .show(ui, |ui| {
                        addon.on_ui(ui, &mut self.world);
                    });
            }
        }
    }
}

pub fn spawn_assembly_parts(world: &mut World, name: &str, specs: Vec<AssemblyPartSpec>) {
    clear_non_addon_parts(world);

    let count = specs.len();
    for spec in specs {
        let mesh_handle = world.resource_mut::<MeshLibrary>().add_mesh(spec.mesh);
        let pick_id = world.resource_mut::<NextPickId>().alloc();
        world.spawn((
            CadPart {
                name: spec.name,
                group: spec.group,
                mesh: mesh_handle,
            },
            PartTransform {
                base_matrix: spec.transform,
                explode_offset: spec.explode_offset,
            },
            WorldTransform(spec.transform),
            CadMaterial(spec.material),
            PickableId(pick_id),
            PartVisibility::default(),
            SelectionState::default(),
            PartMassCache(None),
        ));
    }

    world
        .resource_mut::<Messages<CadEvent>>()
        .write(CadEvent::AssemblyLoaded {
            name: name.to_string(),
            part_count: count,
        });
}

pub fn spawn_imported_assembly(world: &mut World, assembly: ImportedAssembly) {
    clear_non_addon_parts(world);
    let count = assembly.parts.len();
    for part in assembly.parts {
        let mesh_handle = world.resource_mut::<MeshLibrary>().add_mesh(part.mesh);
        let pick_id = world.resource_mut::<NextPickId>().alloc();
        world.spawn((
            CadPart {
                name: part.name,
                group: "Imported".to_string(),
                mesh: mesh_handle,
            },
            PartTransform {
                base_matrix: part.transform,
                explode_offset: Vec3::ZERO,
            },
            WorldTransform(part.transform),
            CadMaterial(part.material),
            PickableId(pick_id),
            PartVisibility::default(),
            SelectionState::default(),
            PartMassCache(None),
        ));
    }

    world
        .resource_mut::<Messages<CadEvent>>()
        .write(CadEvent::AssemblyLoaded {
            name: assembly.name.clone(),
            part_count: count,
        });
}

fn clear_non_addon_parts(world: &mut World) {
    let mut to_despawn = Vec::new();
    let mut query = world.query_filtered::<Entity, With<CadPart>>();
    for e in query.iter(world) {
        to_despawn.push(e);
    }
    for e in to_despawn {
        world.despawn(e);
    }
    world.resource_mut::<MeshLibrary>().clear();
    world.resource_mut::<NextPickId>().0 = 1;
    world.resource_mut::<MeasurementToolState>().points.clear();
    world.resource_mut::<MeasurementToolState>().last_distance = None;
    world.resource_mut::<MeasurementToolState>().last_angle_deg = None;
}

fn process_scene_commands_system(world: &mut World) {
    let cmds = {
        let mut q = world.resource_mut::<SceneCommandQueue>();
        std::mem::take(&mut q.commands)
    };
    if cmds.is_empty() {
        return;
    }

    for cmd in cmds {
        match cmd {
            SceneCommand::LoadDemoAssembly(kind) => {
                let specs = cad_core::build_demo_assembly(kind);
                spawn_assembly_parts(world, kind.label(), specs);
                focus_all_in_world(world);
            }
            SceneCommand::LoadCadFile(path) => match load_cad_file(&path, CreaseConfig::default()) {
                Ok(asm) => {
                    let msg = format!("Loaded {} ({} parts)", asm.name, asm.parts.len());
                    spawn_imported_assembly(world, asm);
                    focus_all_in_world(world);
                    world
                        .resource_mut::<AddonRegistry>()
                        .log("cad_io", msg, false);
                }
                Err(err) => {
                    world
                        .resource_mut::<AddonRegistry>()
                        .log("cad_io", format!("Failed to load {:?}: {err}", path), true);
                }
            },
            SceneCommand::SpawnPart {
                name,
                group,
                mesh,
                transform,
                explode_offset,
                color,
                metallic,
                roughness,
                addon_id,
            } => {
                let handle = world.resource_mut::<MeshLibrary>().add_mesh(mesh);
                let pick_id = world.resource_mut::<NextPickId>().alloc();
                let mat = CadMaterialData {
                    base_color: color,
                    metallic,
                    roughness,
                    ..Default::default()
                };
                let mut entity = world.spawn((
                    CadPart {
                        name,
                        group,
                        mesh: handle,
                    },
                    PartTransform {
                        base_matrix: transform,
                        explode_offset,
                    },
                    WorldTransform(transform),
                    CadMaterial(mat),
                    PickableId(pick_id),
                    PartVisibility::default(),
                    SelectionState::default(),
                    PartMassCache(None),
                ));
                if let Some(aid) = addon_id {
                    entity.insert(AddonOwned { addon_id: aid });
                }
            }
            SceneCommand::RemoveAddonParts(addon_id) => {
                let mut entities = Vec::new();
                let mut q = world.query::<(Entity, &AddonOwned)>();
                for (e, owner) in q.iter(world) {
                    if owner.addon_id == addon_id {
                        entities.push(e);
                    }
                }
                for e in entities {
                    world.despawn(e);
                }
            }
            SceneCommand::SelectPickId(maybe_id) => {
                let mut picked_info = None;
                let mut q = world.query::<(&PickableId, &CadPart, &mut SelectionState)>();
                for (pid, part, mut sel) in q.iter_mut(world) {
                    let is_match = maybe_id == Some(pid.0);
                    sel.selected = is_match;
                    if is_match {
                        picked_info = Some((pid.0, part.name.clone()));
                    }
                }
                if let Some((pick_id, name)) = picked_info {
                    world
                        .resource_mut::<Messages<CadEvent>>()
                        .write(CadEvent::PartPicked { pick_id, name });
                }
            }
            SceneCommand::SetHoverPickId(maybe_id) => {
                let mut q = world.query::<(&PickableId, &mut SelectionState)>();
                for (pid, mut sel) in q.iter_mut(world) {
                    sel.hovered = maybe_id == Some(pid.0);
                }
            }
            SceneCommand::FocusAll => {
                focus_all_in_world(world);
            }
            SceneCommand::FocusSelected => {
                let mut aabb = Aabb3::EMPTY;
                let meshes = world.remove_resource::<MeshLibrary>().unwrap();
                {
                    let mut q =
                        world.query::<(&CadPart, &WorldTransform, &SelectionState, &PartVisibility)>();
                    for (part, wt, sel, vis) in q.iter(world) {
                        if sel.selected
                            && vis.is_drawn()
                            && let Some(m) = meshes.get(part.mesh)
                        {
                            aabb = aabb.merge(&m.aabb.transformed(wt.0));
                        }
                    }
                }
                world.insert_resource(meshes);
                if aabb.is_valid() {
                    world.resource_mut::<CameraState>().orbit.frame_aabb(&aabb);
                }
            }
            SceneCommand::SetPartColor { pick_id, color } => {
                let mut q = world.query::<(&PickableId, &mut CadMaterial)>();
                for (pid, mut mat) in q.iter_mut(world) {
                    if pid.0 == pick_id {
                        mat.0.base_color = color;
                    }
                }
            }
            SceneCommand::SetShadingMode(mode) => {
                world.resource_mut::<RenderSettings>().shading_mode = mode;
            }
            SceneCommand::SetSectionPlane {
                enabled,
                normal,
                offset,
            } => {
                let mut sec = world.resource_mut::<SectionPlaneState>();
                sec.enabled = enabled;
                sec.axis = SectionAxis::Custom;
                sec.custom_normal = normal.normalize_or(Vec3::Y);
                sec.offset = offset;
                sec.flip = false;
            }
        }
    }
}

fn focus_all_in_world(world: &mut World) {
    let meshes = world.remove_resource::<MeshLibrary>().unwrap();
    let mut aabb = Aabb3::EMPTY;
    {
        let mut q = world.query::<(&CadPart, &WorldTransform, &PartVisibility)>();
        for (part, wt, vis) in q.iter(world) {
            if vis.is_drawn()
                && let Some(m) = meshes.get(part.mesh)
            {
                aabb = aabb.merge(&m.aabb.transformed(wt.0));
            }
        }
    }
    world.insert_resource(meshes);
    if aabb.is_valid() {
        world.resource_mut::<CameraState>().orbit.frame_aabb(&aabb);
    }
}

fn explode_animation_system(time: Res<CadTime>, mut explode: ResMut<ExplodeState>) {
    if explode.animate {
        explode.factor = 0.5 * (1.0 - (time.elapsed * explode.speed * 2.0).cos());
    }
}

fn sync_world_transforms_system(
    explode: Res<ExplodeState>,
    mut query: Query<(&PartTransform, &mut WorldTransform)>,
) {
    let f = explode.factor;
    for (pt, mut wt) in query.iter_mut() {
        wt.0 = Mat4::from_translation(pt.explode_offset * f) * pt.base_matrix;
    }
}

fn sync_isolation_system(
    settings: Res<RenderSettings>,
    mut query: Query<(&SelectionState, &mut PartVisibility)>,
) {
    if !settings.isolate_selected {
        for (_, mut vis) in query.iter_mut() {
            vis.isolated_hidden = false;
        }
        return;
    }

    let any_selected = query.iter().any(|(sel, _)| sel.selected);
    for (sel, mut vis) in query.iter_mut() {
        vis.isolated_hidden = any_selected && !sel.selected;
    }
}

fn measurement_overlay_system(
    meas: Res<MeasurementToolState>,
    mut overlay: ResMut<RenderOverlayQueue>,
) {
    for pt in &meas.points {
        overlay.push_crosshair(pt.world_pos, 0.14, [0.15, 0.95, 0.72, 1.0]);
        overlay.push_line(
            pt.world_pos,
            pt.world_pos + pt.world_normal * 0.30,
            [1.0, 0.75, 0.15, 1.0],
            true,
        );
    }
    if meas.points.len() >= 2 {
        let a = meas.points[meas.points.len() - 2].world_pos;
        let b = meas.points[meas.points.len() - 1].world_pos;
        overlay.push_line(a, b, [0.20, 0.95, 1.0, 1.0], true);
    }
}

fn compute_viewport_stats_system(
    meshes: Res<MeshLibrary>,
    mut stats: ResMut<ViewportStats>,
    mut overlay: ResMut<RenderOverlayQueue>,
    query: Query<(
        &CadPart,
        &WorldTransform,
        &PickableId,
        &PartVisibility,
        &SelectionState,
    )>,
) {
    let mut total_parts = 0usize;
    let mut visible_parts = 0usize;
    let mut total_triangles = 0usize;
    let mut total_edges = 0usize;
    let mut scene_aabb = Aabb3::EMPTY;
    let mut selected_pick_id = 0u32;
    let mut selected_part = None;
    let mut hovered_pick_id = 0u32;
    let mut hovered_part = None;

    for (part, wt, pid, vis, sel) in query.iter() {
        total_parts += 1;
        if let Some(m) = meshes.get(part.mesh) {
            let world_aabb = m.aabb.transformed(wt.0);
            if vis.is_drawn() {
                visible_parts += 1;
                total_triangles += m.triangle_count();
                total_edges += m.edge_segment_count();
                scene_aabb = scene_aabb.merge(&world_aabb);
            }
            if sel.selected {
                selected_pick_id = pid.0;
                selected_part = Some(part.name.clone());
                overlay.push_aabb(&world_aabb, [0.15, 0.85, 1.0, 0.95], false);
            }
            if sel.hovered {
                hovered_pick_id = pid.0;
                hovered_part = Some(part.name.clone());
            }
        }
    }

    stats.total_parts = total_parts;
    stats.visible_parts = visible_parts;
    stats.total_triangles = total_triangles;
    stats.total_edges = total_edges;
    stats.scene_aabb = scene_aabb;
    stats.selected_pick_id = selected_pick_id;
    stats.selected_part = selected_part;
    stats.hovered_pick_id = hovered_pick_id;
    stats.hovered_part = hovered_part;
}
