use bevy_ecs::prelude::*;
use cad_core::{MassProperties, compute_mass_properties};
use cad_plugin_api::{
    AddonDescriptor, AddonKind, AddonRegistry, CadAddon, CadPart, CadPluginHost, CadSchedule,
    CadTime, CameraState, ExplodeState, MeshLibrary, PartMassCache, PartVisibility,
    RenderOverlayQueue, SectionAxis, SectionPlaneState, SelectionState, ViewportStats,
    WorldTransform,
};
use glam::Vec3;

pub fn register_builtin_rust_addons(host: &mut CadPluginHost) {
    host.register_addon(Box::new(MassPropertiesAddon));
    host.register_addon(Box::new(SectionInspectorAddon));
    host.register_addon(Box::new(ExplodeInspectorAddon));
    host.register_addon(Box::new(TurntableInspectorAddon));
}

#[derive(Resource, Debug, Default, Clone)]
pub struct AssemblyMassSummary {
    pub total_volume: f32,
    pub total_surface_area: f32,
    pub center_of_mass: Vec3,
    pub selected_mass: Option<(String, MassProperties)>,
    pub show_com_overlay: bool,
}

pub struct MassPropertiesAddon;

impl CadAddon for MassPropertiesAddon {
    fn descriptor(&self) -> AddonDescriptor {
        AddonDescriptor {
            id: "builtin.mass_properties".into(),
            name: "Mass & Metrology Analyzer".into(),
            version: "1.0.0".into(),
            author: "CAD Core".into(),
            description: "Computes signed closed-mesh volume, surface area, and weighted center of mass via ECS systems.".into(),
            kind: AddonKind::NativeRust,
        }
    }

    fn build(&self, host: &mut CadPluginHost) {
        host.world.insert_resource(AssemblyMassSummary {
            show_com_overlay: true,
            ..Default::default()
        });
        host.add_systems(CadSchedule::Update, mass_properties_system);
    }

    fn on_ui(&self, ui: &mut egui::Ui, world: &mut World) {
        let mut summary = world.resource::<AssemblyMassSummary>().clone();
        ui.checkbox(&mut summary.show_com_overlay, "Show Center-of-Mass Crosshair");
        world.resource_mut::<AssemblyMassSummary>().show_com_overlay = summary.show_com_overlay;

        egui::Grid::new("mass_props_grid")
            .num_columns(2)
            .spacing([12.0, 4.0])
            .show(ui, |ui| {
                ui.label("Assembly Volume:");
                ui.monospace(format!("{:.4} u³", summary.total_volume));
                ui.end_row();

                ui.label("Surface Area:");
                ui.monospace(format!("{:.4} u²", summary.total_surface_area));
                ui.end_row();

                ui.label("Center of Mass:");
                ui.monospace(format!(
                    "({:.3}, {:.3}, {:.3})",
                    summary.center_of_mass.x, summary.center_of_mass.y, summary.center_of_mass.z
                ));
                ui.end_row();
            });

        if let Some((name, mp)) = summary.selected_mass {
            ui.separator();
            ui.label(egui::RichText::new(format!("Selected: {name}")).strong());
            ui.monospace(format!("  Vol: {:.4} u³  |  Area: {:.4} u²", mp.volume, mp.surface_area));
            ui.monospace(format!(
                "  CoM: ({:.3}, {:.3}, {:.3})",
                mp.center_of_mass.x, mp.center_of_mass.y, mp.center_of_mass.z
            ));
        }
    }
}

fn mass_properties_system(
    registry: Res<AddonRegistry>,
    meshes: Res<MeshLibrary>,
    mut summary: ResMut<AssemblyMassSummary>,
    mut overlay: ResMut<RenderOverlayQueue>,
    mut query: Query<(
        &CadPart,
        &WorldTransform,
        &PartVisibility,
        &SelectionState,
        &mut PartMassCache,
    )>,
) {
    if !registry.is_enabled("builtin.mass_properties") {
        return;
    }

    let mut total_vol = 0.0_f32;
    let mut total_area = 0.0_f32;
    let mut weighted_com = Vec3::ZERO;
    let mut selected_mass = None;

    for (part, wt, vis, sel, mut cache) in query.iter_mut() {
        if cache.0.is_none()
            && let Some(mesh) = meshes.get(part.mesh)
        {
            cache.0 = Some(compute_mass_properties(mesh));
        }
        if let Some(mp) = cache.0
            && vis.is_drawn()
        {
            let world_com = wt.0.transform_point3(mp.center_of_mass);
            total_vol += mp.volume;
            total_area += mp.surface_area;
            weighted_com += world_com * mp.volume.max(1e-5);
            if sel.selected {
                let mut world_mp = mp;
                world_mp.center_of_mass = world_com;
                selected_mass = Some((part.name.clone(), world_mp));
            }
        }
    }

    let final_com = if total_vol > 1e-5 {
        weighted_com / total_vol
    } else {
        Vec3::ZERO
    };

    summary.total_volume = total_vol;
    summary.total_surface_area = total_area;
    summary.center_of_mass = final_com;
    summary.selected_mass = selected_mass;

    if summary.show_com_overlay && total_vol > 1e-5 {
        overlay.push_crosshair(final_com, 0.25, [1.0, 0.85, 0.20, 0.95]);
    }
}

pub struct SectionInspectorAddon;

impl CadAddon for SectionInspectorAddon {
    fn descriptor(&self) -> AddonDescriptor {
        AddonDescriptor {
            id: "builtin.section_plane".into(),
            name: "Section Cut Inspector".into(),
            version: "1.0.0".into(),
            author: "CAD Core".into(),
            description: "Real-time shader clipping plane with backface cross-hatching & 3D cut frame.".into(),
            kind: AddonKind::NativeRust,
        }
    }

    fn build(&self, host: &mut CadPluginHost) {
        host.add_systems(CadSchedule::PostUpdate, section_frame_overlay_system);
    }

    fn on_ui(&self, ui: &mut egui::Ui, world: &mut World) {
        let aabb = world.resource::<ViewportStats>().scene_aabb;
        let extent = if aabb.is_valid() {
            (aabb.diagonal() * 0.55).max(1.5)
        } else {
            3.0
        };
        let mut sec = world.resource_mut::<SectionPlaneState>();
        ui.checkbox(&mut sec.enabled, "Enable Section Clipping");
        ui.horizontal(|ui| {
            ui.label("Axis:");
            ui.selectable_value(&mut sec.axis, SectionAxis::X, "X");
            ui.selectable_value(&mut sec.axis, SectionAxis::Y, "Y");
            ui.selectable_value(&mut sec.axis, SectionAxis::Z, "Z");
            ui.selectable_value(&mut sec.axis, SectionAxis::Custom, "Custom");
        });
        ui.add(egui::Slider::new(&mut sec.offset, -extent..=extent).text("Plane Offset"));
        ui.horizontal(|ui| {
            ui.checkbox(&mut sec.flip, "Flip Side");
            ui.checkbox(&mut sec.show_frame, "Show 3D Plane Frame");
        });
        if sec.axis == SectionAxis::Custom {
            ui.horizontal(|ui| {
                ui.add(egui::DragValue::new(&mut sec.custom_normal.x).speed(0.02).prefix("nx:"));
                ui.add(egui::DragValue::new(&mut sec.custom_normal.y).speed(0.02).prefix("ny:"));
                ui.add(egui::DragValue::new(&mut sec.custom_normal.z).speed(0.02).prefix("nz:"));
            });
        }
    }
}

fn section_frame_overlay_system(
    registry: Res<AddonRegistry>,
    sec: Res<SectionPlaneState>,
    stats: Res<ViewportStats>,
    mut overlay: ResMut<RenderOverlayQueue>,
) {
    if !registry.is_enabled("builtin.section_plane") || !sec.enabled || !sec.show_frame {
        return;
    }

    let n = sec.active_normal();
    let offset = if sec.flip { -sec.offset } else { sec.offset };
    let center = if stats.scene_aabb.is_valid() {
        let c = stats.scene_aabb.center();
        c + n * (offset - n.dot(c))
    } else {
        n * offset
    };

    let up_hint = if n.y.abs() > 0.9 { Vec3::X } else { Vec3::Y };
    let u = n.cross(up_hint).normalize_or(Vec3::X);
    let v = u.cross(n).normalize_or(Vec3::Z);
    let half = if stats.scene_aabb.is_valid() {
        (stats.scene_aabb.diagonal() * 0.45).max(1.2)
    } else {
        2.5
    };

    let p0 = center - u * half - v * half;
    let p1 = center + u * half - v * half;
    let p2 = center + u * half + v * half;
    let p3 = center - u * half + v * half;
    let frame_col = [1.0, 0.56, 0.14, 0.92];

    overlay.push_line(p0, p1, frame_col, false);
    overlay.push_line(p1, p2, frame_col, false);
    overlay.push_line(p2, p3, frame_col, false);
    overlay.push_line(p3, p0, frame_col, false);
    overlay.push_line(center, center + n * (half * 0.45), [1.0, 0.72, 0.22, 1.0], true);
}

pub struct ExplodeInspectorAddon;

impl CadAddon for ExplodeInspectorAddon {
    fn descriptor(&self) -> AddonDescriptor {
        AddonDescriptor {
            id: "builtin.explode_view".into(),
            name: "Assembly Explode Controller".into(),
            version: "1.0.0".into(),
            author: "CAD Core".into(),
            description: "Parametric assembly explosion along radial & axial disassembly vectors.".into(),
            kind: AddonKind::NativeRust,
        }
    }

    fn build(&self, _host: &mut CadPluginHost) {}

    fn on_ui(&self, ui: &mut egui::Ui, world: &mut World) {
        let mut explode = world.resource_mut::<ExplodeState>();
        ui.add(egui::Slider::new(&mut explode.factor, 0.0..=1.5).text("Explode Factor"));
        ui.horizontal(|ui| {
            ui.checkbox(&mut explode.animate, "Auto-Animate");
            if ui.button("Collapse").clicked() {
                explode.animate = false;
                explode.factor = 0.0;
            }
            if ui.button("Max").clicked() {
                explode.animate = false;
                explode.factor = 1.0;
            }
        });
    }
}

pub struct TurntableInspectorAddon;

impl CadAddon for TurntableInspectorAddon {
    fn descriptor(&self) -> AddonDescriptor {
        AddonDescriptor {
            id: "builtin.turntable".into(),
            name: "Studio Turntable Orbit".into(),
            version: "1.0.0".into(),
            author: "CAD Core".into(),
            description: "Smooth inspection turntable camera drive.".into(),
            kind: AddonKind::NativeRust,
        }
    }

    fn build(&self, host: &mut CadPluginHost) {
        host.add_systems(CadSchedule::Update, turntable_orbit_system);
    }

    fn on_ui(&self, ui: &mut egui::Ui, world: &mut World) {
        let mut cam = world.resource_mut::<CameraState>();
        ui.checkbox(&mut cam.turntable_active, "Enable Turntable Rotation");
        ui.add(egui::Slider::new(&mut cam.turntable_speed, 0.05..=2.0).text("Angular Speed"));
    }
}

fn turntable_orbit_system(
    registry: Res<AddonRegistry>,
    time: Res<CadTime>,
    mut cam: ResMut<CameraState>,
) {
    if !registry.is_enabled("builtin.turntable") || !cam.turntable_active {
        return;
    }
    cam.orbit.yaw += cam.turntable_speed * time.delta;
}
