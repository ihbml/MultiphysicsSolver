use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use bevy_ecs::prelude::*;
use cad_core::{
    Aabb3, make_box, make_cylinder, make_flange, make_hex_bolt, make_spur_gear, make_tube,
};
use cad_plugin_api::{
    AddonDescriptor, AddonKind, AddonPanelRegistry, AddonPanelSpec, AddonRegistry,
    AddonStatusEntry, AddonWidget, CadAddon, CadEvent, CadPart, CadPluginHost, CadSchedule,
    CadTime, MeshLibrary, OverlayLine, PartVisibility, PickableId, RenderOverlayQueue,
    SceneCommand, SceneCommandQueue, SelectionState, ShadingMode, ViewportStats, WorldTransform,
};
use glam::{EulerRot, Mat4, Quat, Vec3};
use rhai::{AST, Array, Dynamic, Engine, Map, Scope};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
struct AddonTomlManifest {
    id: String,
    name: String,
    #[serde(default = "default_version")]
    version: String,
    #[serde(default = "default_author")]
    author: String,
    #[serde(default)]
    description: String,
    #[serde(default = "default_entry")]
    entry: String,
    #[serde(default = "default_enabled")]
    enabled: bool,
}

fn default_version() -> String {
    "0.1.0".into()
}
fn default_author() -> String {
    "Community".into()
}
fn default_entry() -> String {
    "main.rhai".into()
}
fn default_enabled() -> bool {
    true
}

#[derive(Clone)]
struct PartSnapshot {
    pick_id: u32,
    name: String,
    group: String,
    aabb: Aabb3,
    selected: bool,
    visible: bool,
}

struct ScriptSharedState {
    active_addon_id: String,
    parts: Vec<PartSnapshot>,
    selected_pick_id: u32,
    selected_name: String,
    elapsed: f32,
    delta: f32,
    float_values: BTreeMap<(String, String), f64>,
    bool_values: BTreeMap<(String, String), bool>,
    clicked_buttons: HashSet<(String, String)>,

    out_commands: Vec<SceneCommand>,
    out_overlays: Vec<OverlayLine>,
    out_widgets: Vec<AddonWidget>,
    out_logs: Vec<(String, String, bool)>,
}

impl Default for ScriptSharedState {
    fn default() -> Self {
        Self {
            active_addon_id: String::new(),
            parts: Vec::new(),
            selected_pick_id: 0,
            selected_name: String::new(),
            elapsed: 0.0,
            delta: 0.0,
            float_values: BTreeMap::new(),
            bool_values: BTreeMap::new(),
            clicked_buttons: HashSet::new(),
            out_commands: Vec::new(),
            out_overlays: Vec::new(),
            out_widgets: Vec::new(),
            out_logs: Vec::new(),
        }
    }
}

#[derive(Clone)]
pub struct CadScriptHandle {
    shared: Arc<Mutex<ScriptSharedState>>,
}

fn to_f32(v: Dynamic) -> f32 {
    if let Ok(f) = v.as_float() {
        f as f32
    } else if let Ok(i) = v.as_int() {
        i as f32
    } else {
        0.0
    }
}

fn to_f64(v: Dynamic) -> f64 {
    if let Ok(f) = v.as_float() {
        f
    } else if let Ok(i) = v.as_int() {
        i as f64
    } else {
        0.0
    }
}

fn to_i64(v: Dynamic) -> i64 {
    if let Ok(i) = v.as_int() {
        i
    } else if let Ok(f) = v.as_float() {
        f as i64
    } else {
        0
    }
}

impl CadScriptHandle {
    fn log(&mut self, msg: &str) {
        let mut s = self.shared.lock().unwrap();
        let id = s.active_addon_id.clone();
        s.out_logs.push((id, msg.to_string(), false));
    }

    fn elapsed(&mut self) -> f64 {
        self.shared.lock().unwrap().elapsed as f64
    }

    fn dt(&mut self) -> f64 {
        self.shared.lock().unwrap().delta as f64
    }

    fn part_count(&mut self) -> i64 {
        self.shared.lock().unwrap().parts.len() as i64
    }

    fn selected_pick_id(&mut self) -> i64 {
        self.shared.lock().unwrap().selected_pick_id as i64
    }

    fn selected_name(&mut self) -> String {
        self.shared.lock().unwrap().selected_name.clone()
    }

    fn list_parts(&mut self) -> Array {
        let s = self.shared.lock().unwrap();
        s.parts
            .iter()
            .map(|p| {
                let c = p.aabb.center();
                let mut m = Map::new();
                m.insert("pick_id".into(), Dynamic::from(p.pick_id as i64));
                m.insert("name".into(), Dynamic::from(p.name.clone()));
                m.insert("group".into(), Dynamic::from(p.group.clone()));
                m.insert("selected".into(), Dynamic::from(p.selected));
                m.insert("visible".into(), Dynamic::from(p.visible));
                m.insert("min_x".into(), Dynamic::from(p.aabb.min.x as f64));
                m.insert("min_y".into(), Dynamic::from(p.aabb.min.y as f64));
                m.insert("min_z".into(), Dynamic::from(p.aabb.min.z as f64));
                m.insert("max_x".into(), Dynamic::from(p.aabb.max.x as f64));
                m.insert("max_y".into(), Dynamic::from(p.aabb.max.y as f64));
                m.insert("max_z".into(), Dynamic::from(p.aabb.max.z as f64));
                m.insert("center_x".into(), Dynamic::from(c.x as f64));
                m.insert("center_y".into(), Dynamic::from(c.y as f64));
                m.insert("center_z".into(), Dynamic::from(c.z as f64));
                Dynamic::from(m)
            })
            .collect()
    }

    fn min_aabb_distance(&mut self, idx_a: i64, idx_b: i64) -> f64 {
        let s = self.shared.lock().unwrap();
        let (Some(a), Some(b)) = (s.parts.get(idx_a as usize), s.parts.get(idx_b as usize)) else {
            return f64::INFINITY;
        };
        a.aabb.min_distance_to(&b.aabb) as f64
    }

    fn clear_my_parts(&mut self) {
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::RemoveAddonParts(aid));
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_box(
        &mut self,
        name: &str,
        sx: Dynamic,
        sy: Dynamic,
        sz: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let mesh = make_box(name, Vec3::new(to_f32(sx), to_f32(sy), to_f32(sz)));
        let transform = Mat4::from_translation(Vec3::new(to_f32(x), to_f32(y), to_f32(z)));
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::ZERO,
            color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
            metallic: 0.65,
            roughness: 0.34,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_cylinder(
        &mut self,
        name: &str,
        radius: Dynamic,
        height: Dynamic,
        segs: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let mesh = make_cylinder(
            name,
            to_f32(radius),
            to_f32(height),
            to_i64(segs).max(6) as u32,
        );
        let transform = Mat4::from_translation(Vec3::new(to_f32(x), to_f32(y), to_f32(z)));
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::ZERO,
            color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
            metallic: 0.72,
            roughness: 0.30,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_tube(
        &mut self,
        name: &str,
        outer_r: Dynamic,
        inner_r: Dynamic,
        height: Dynamic,
        segs: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let mesh = make_tube(
            name,
            to_f32(outer_r),
            to_f32(inner_r),
            to_f32(height),
            to_i64(segs).max(8) as u32,
        );
        let transform = Mat4::from_translation(Vec3::new(to_f32(x), to_f32(y), to_f32(z)));
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::ZERO,
            color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
            metallic: 0.75,
            roughness: 0.30,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_flange(
        &mut self,
        name: &str,
        outer_r: Dynamic,
        bore_r: Dynamic,
        plate_h: Dynamic,
        hub_r: Dynamic,
        hub_h: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let mesh = make_flange(
            name,
            to_f32(outer_r),
            to_f32(bore_r),
            to_f32(plate_h),
            to_f32(hub_r),
            to_f32(hub_h),
            44,
        );
        let transform = Mat4::from_translation(Vec3::new(to_f32(x), to_f32(y), to_f32(z)));
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::new(0.0, 0.5, 0.0),
            color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
            metallic: 0.70,
            roughness: 0.32,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_gear(
        &mut self,
        name: &str,
        pitch_r: Dynamic,
        teeth: Dynamic,
        thickness: Dynamic,
        bore_r: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let mesh = make_spur_gear(
            name,
            to_f32(pitch_r),
            to_i64(teeth).max(8) as u32,
            to_f32(thickness),
            to_f32(bore_r),
        );
        let transform = Mat4::from_translation(Vec3::new(to_f32(x), to_f32(y), to_f32(z)));
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::new(0.0, 0.7, 0.0),
            color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
            metallic: 0.85,
            roughness: 0.28,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn spawn_bolt(
        &mut self,
        name: &str,
        head_r: Dynamic,
        head_h: Dynamic,
        shaft_r: Dynamic,
        shaft_len: Dynamic,
        x: Dynamic,
        y: Dynamic,
        z: Dynamic,
        rx_deg: Dynamic,
        ry_deg: Dynamic,
        rz_deg: Dynamic,
    ) {
        let mesh = make_hex_bolt(
            name,
            to_f32(head_r),
            to_f32(head_h),
            to_f32(shaft_r),
            to_f32(shaft_len),
        );
        let rot = Quat::from_euler(
            EulerRot::YXZ,
            to_f32(ry_deg).to_radians(),
            to_f32(rx_deg).to_radians(),
            to_f32(rz_deg).to_radians(),
        );
        let transform = Mat4::from_rotation_translation(
            rot,
            Vec3::new(to_f32(x), to_f32(y), to_f32(z)),
        );
        let mut s = self.shared.lock().unwrap();
        let aid = s.active_addon_id.clone();
        s.out_commands.push(SceneCommand::SpawnPart {
            name: name.to_string(),
            group: format!("Addon:{aid}"),
            mesh,
            transform,
            explode_offset: Vec3::new(0.0, 0.8, 0.0),
            color: [0.56, 0.60, 0.66, 1.0],
            metallic: 0.88,
            roughness: 0.25,
            addon_id: Some(aid),
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn draw_line(
        &mut self,
        x0: Dynamic,
        y0: Dynamic,
        z0: Dynamic,
        x1: Dynamic,
        y1: Dynamic,
        z1: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
        always_on_top: bool,
    ) {
        let mut s = self.shared.lock().unwrap();
        s.out_overlays.push(OverlayLine {
            start: Vec3::new(to_f32(x0), to_f32(y0), to_f32(z0)),
            end: Vec3::new(to_f32(x1), to_f32(y1), to_f32(z1)),
            color: [to_f32(r), to_f32(g), to_f32(b), 0.95],
            always_on_top,
        });
    }

    #[allow(clippy::too_many_arguments)]
    fn draw_box(
        &mut self,
        min_x: Dynamic,
        min_y: Dynamic,
        min_z: Dynamic,
        max_x: Dynamic,
        max_y: Dynamic,
        max_z: Dynamic,
        r: Dynamic,
        g: Dynamic,
        b: Dynamic,
    ) {
        let min = Vec3::new(to_f32(min_x), to_f32(min_y), to_f32(min_z));
        let max = Vec3::new(to_f32(max_x), to_f32(max_y), to_f32(max_z));
        let col = [to_f32(r), to_f32(g), to_f32(b), 0.95];
        let corners = [
            Vec3::new(min.x, min.y, min.z),
            Vec3::new(max.x, min.y, min.z),
            Vec3::new(max.x, min.y, max.z),
            Vec3::new(min.x, min.y, max.z),
            Vec3::new(min.x, max.y, min.z),
            Vec3::new(max.x, max.y, min.z),
            Vec3::new(max.x, max.y, max.z),
            Vec3::new(min.x, max.y, max.z),
        ];
        let edges = [
            (0, 1),
            (1, 2),
            (2, 3),
            (3, 0),
            (4, 5),
            (5, 6),
            (6, 7),
            (7, 4),
            (0, 4),
            (1, 5),
            (2, 6),
            (3, 7),
        ];
        let mut s = self.shared.lock().unwrap();
        for (a, b_idx) in edges {
            s.out_overlays.push(OverlayLine {
                start: corners[a],
                end: corners[b_idx],
                color: col,
                always_on_top: false,
            });
        }
    }

    fn set_shading_mode(&mut self, mode: i64) {
        let sm = match mode {
            0 => ShadingMode::ShadedWithEdges,
            1 => ShadingMode::ShadedSmooth,
            2 => ShadingMode::GoochTechnical,
            3 => ShadingMode::MatCapCurvature,
            4 => ShadingMode::ZebraStripes,
            5 => ShadingMode::XRayGhost,
            _ => ShadingMode::WireframeHidden,
        };
        self.shared
            .lock()
            .unwrap()
            .out_commands
            .push(SceneCommand::SetShadingMode(sm));
    }

    fn set_section_plane(
        &mut self,
        enabled: bool,
        nx: Dynamic,
        ny: Dynamic,
        nz: Dynamic,
        offset: Dynamic,
    ) {
        self.shared
            .lock()
            .unwrap()
            .out_commands
            .push(SceneCommand::SetSectionPlane {
                enabled,
                normal: Vec3::new(to_f32(nx), to_f32(ny), to_f32(nz)),
                offset: to_f32(offset),
            });
    }

    fn set_part_color(&mut self, pick_id: i64, r: Dynamic, g: Dynamic, b: Dynamic) {
        if pick_id > 0 {
            self.shared
                .lock()
                .unwrap()
                .out_commands
                .push(SceneCommand::SetPartColor {
                    pick_id: pick_id as u32,
                    color: [to_f32(r), to_f32(g), to_f32(b), 1.0],
                });
        }
    }

    fn focus_all(&mut self) {
        self.shared
            .lock()
            .unwrap()
            .out_commands
            .push(SceneCommand::FocusAll);
    }

    fn ui_heading(&mut self, text: &str) {
        self.shared
            .lock()
            .unwrap()
            .out_widgets
            .push(AddonWidget::Heading(text.to_string()));
    }

    fn ui_label(&mut self, text: &str) {
        self.shared
            .lock()
            .unwrap()
            .out_widgets
            .push(AddonWidget::Label(text.to_string()));
    }

    fn ui_readout(&mut self, label: &str, value: &str) {
        self.shared
            .lock()
            .unwrap()
            .out_widgets
            .push(AddonWidget::Readout {
                label: label.to_string(),
                value: value.to_string(),
            });
    }

    fn ui_separator(&mut self) {
        self.shared
            .lock()
            .unwrap()
            .out_widgets
            .push(AddonWidget::Separator);
    }

    fn ui_slider(&mut self, id: &str, label: &str, min: Dynamic, max: Dynamic, default: Dynamic) -> f64 {
        let min_v = to_f64(min);
        let max_v = to_f64(max);
        let def_v = to_f64(default);
        let mut s = self.shared.lock().unwrap();
        let key = (s.active_addon_id.clone(), id.to_string());
        let val = *s.float_values.entry(key).or_insert(def_v);
        s.out_widgets.push(AddonWidget::Slider {
            id: id.to_string(),
            label: label.to_string(),
            min: min_v,
            max: max_v,
            default: def_v,
        });
        val
    }

    fn ui_checkbox(&mut self, id: &str, label: &str, default: bool) -> bool {
        let mut s = self.shared.lock().unwrap();
        let key = (s.active_addon_id.clone(), id.to_string());
        let val = *s.bool_values.entry(key).or_insert(default);
        s.out_widgets.push(AddonWidget::Checkbox {
            id: id.to_string(),
            label: label.to_string(),
            default,
        });
        val
    }

    fn ui_button(&mut self, id: &str, label: &str) -> bool {
        let mut s = self.shared.lock().unwrap();
        let key = (s.active_addon_id.clone(), id.to_string());
        let clicked = s.clicked_buttons.remove(&key);
        s.out_widgets.push(AddonWidget::Button {
            id: id.to_string(),
            label: label.to_string(),
        });
        clicked
    }
}

pub struct LoadedRhaiAddon {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub dir_path: PathBuf,
    pub script_path: PathBuf,
    pub script_source: String,
    pub ast: Option<AST>,
    pub scope: Scope<'static>,
    pub has_init: bool,
    pub has_update: bool,
    pub has_ui: bool,
    pub has_pick: bool,
    pub initialized: bool,
    pub last_error: Option<String>,
}

#[derive(Resource)]
pub struct RhaiAddonManager {
    engine: Engine,
    shared: Arc<Mutex<ScriptSharedState>>,
    pub addons: Vec<LoadedRhaiAddon>,
    pub addons_dir: PathBuf,
}

impl RhaiAddonManager {
    pub fn new(addons_dir: PathBuf) -> Self {
        let mut engine = Engine::new();
        engine.set_max_operations(150_000);
        engine.set_max_call_levels(32);

        engine.register_type_with_name::<CadScriptHandle>("CadContext");
        engine.register_fn("log", CadScriptHandle::log);
        engine.register_fn("elapsed", CadScriptHandle::elapsed);
        engine.register_fn("dt", CadScriptHandle::dt);
        engine.register_fn("part_count", CadScriptHandle::part_count);
        engine.register_fn("selected_pick_id", CadScriptHandle::selected_pick_id);
        engine.register_fn("selected_name", CadScriptHandle::selected_name);
        engine.register_fn("list_parts", CadScriptHandle::list_parts);
        engine.register_fn("min_aabb_distance", CadScriptHandle::min_aabb_distance);
        engine.register_fn("clear_my_parts", CadScriptHandle::clear_my_parts);
        engine.register_fn("spawn_box", CadScriptHandle::spawn_box);
        engine.register_fn("spawn_cylinder", CadScriptHandle::spawn_cylinder);
        engine.register_fn("spawn_tube", CadScriptHandle::spawn_tube);
        engine.register_fn("spawn_flange", CadScriptHandle::spawn_flange);
        engine.register_fn("spawn_gear", CadScriptHandle::spawn_gear);
        engine.register_fn("spawn_bolt", CadScriptHandle::spawn_bolt);
        engine.register_fn("draw_line", CadScriptHandle::draw_line);
        engine.register_fn("draw_box", CadScriptHandle::draw_box);
        engine.register_fn("set_shading_mode", CadScriptHandle::set_shading_mode);
        engine.register_fn("set_section_plane", CadScriptHandle::set_section_plane);
        engine.register_fn("set_part_color", CadScriptHandle::set_part_color);
        engine.register_fn("focus_all", CadScriptHandle::focus_all);
        engine.register_fn("ui_heading", CadScriptHandle::ui_heading);
        engine.register_fn("ui_label", CadScriptHandle::ui_label);
        engine.register_fn("ui_readout", CadScriptHandle::ui_readout);
        engine.register_fn("ui_separator", CadScriptHandle::ui_separator);
        engine.register_fn("ui_slider", CadScriptHandle::ui_slider);
        engine.register_fn("ui_checkbox", CadScriptHandle::ui_checkbox);
        engine.register_fn("ui_button", CadScriptHandle::ui_button);

        Self {
            engine,
            shared: Arc::new(Mutex::new(ScriptSharedState::default())),
            addons: Vec::new(),
            addons_dir,
        }
    }

    pub fn scan_and_load(&mut self, registry: &mut AddonRegistry) {
        self.addons.clear();
        registry.entries.retain(|e| e.kind_label != AddonKind::RhaiScript.label());

        if !self.addons_dir.exists() {
            return;
        }

        let mut dirs: Vec<PathBuf> = match fs::read_dir(&self.addons_dir) {
            Ok(rd) => rd
                .filter_map(|e| e.ok().map(|entry| entry.path()))
                .filter(|p| p.is_dir())
                .collect(),
            Err(_) => Vec::new(),
        };
        dirs.sort();

        for dir in dirs {
            let manifest_path = dir.join("addon.toml");
            if !manifest_path.exists() {
                continue;
            }
            match fs::read_to_string(&manifest_path)
                .map_err(|e| e.to_string())
                .and_then(|s| toml::from_str::<AddonTomlManifest>(&s).map_err(|e| e.to_string()))
            {
                Ok(manifest) => {
                    let script_path = dir.join(&manifest.entry);
                    let source = fs::read_to_string(&script_path).unwrap_or_default();
                    let (ast, err, has_init, has_update, has_ui, has_pick) =
                        self.compile_source(&source);

                    registry.entries.push(AddonStatusEntry {
                        id: manifest.id.clone(),
                        name: manifest.name.clone(),
                        version: manifest.version.clone(),
                        author: manifest.author.clone(),
                        description: manifest.description.clone(),
                        kind_label: AddonKind::RhaiScript.label().to_string(),
                        enabled: manifest.enabled,
                        last_error: err.clone(),
                        source_path: Some(script_path.clone()),
                        script_source: Some(source.clone()),
                    });

                    if let Some(e) = &err {
                        registry.log(manifest.id.clone(), format!("Compile error: {e}"), true);
                    } else {
                        registry.log(
                            manifest.id.clone(),
                            format!("Loaded Rhai script {}", manifest.name),
                            false,
                        );
                    }

                    self.addons.push(LoadedRhaiAddon {
                        id: manifest.id,
                        name: manifest.name,
                        version: manifest.version,
                        author: manifest.author,
                        description: manifest.description,
                        dir_path: dir,
                        script_path,
                        script_source: source,
                        ast,
                        scope: Scope::new(),
                        has_init,
                        has_update,
                        has_ui,
                        has_pick,
                        initialized: false,
                        last_error: err,
                    });
                }
                Err(err) => {
                    registry.log("rhai_loader", format!("Manifest error in {:?}: {err}", dir), true);
                }
            }
        }
    }

    pub fn reload_script_from_source(
        &mut self,
        addon_id: &str,
        new_source: String,
        save_to_disk: bool,
        registry: &mut AddonRegistry,
        commands: &mut SceneCommandQueue,
    ) {
        let (ast, err, has_init, has_update, has_ui, has_pick) = self.compile_source(&new_source);
        if let Some(addon) = self.addons.iter_mut().find(|a| a.id == addon_id) {
            if save_to_disk {
                let _ = fs::write(&addon.script_path, &new_source);
            }
            addon.script_source = new_source.clone();
            addon.ast = ast;
            addon.scope = Scope::new();
            addon.has_init = has_init;
            addon.has_update = has_update;
            addon.has_ui = has_ui;
            addon.has_pick = has_pick;
            addon.initialized = false;
            addon.last_error = err.clone();
            commands.push(SceneCommand::RemoveAddonParts(addon_id.to_string()));
        }

        if let Some(entry) = registry.entries.iter_mut().find(|e| e.id == addon_id) {
            entry.script_source = Some(new_source);
            entry.last_error = err.clone();
        }
        if let Some(e) = err {
            registry.log(addon_id, format!("Hot-reload compile error: {e}"), true);
        } else {
            registry.log(addon_id, "Hot-reloaded script successfully", false);
        }
    }

    fn compile_source(&self, src: &str) -> (Option<AST>, Option<String>, bool, bool, bool, bool) {
        match self.engine.compile(src) {
            Ok(ast) => {
                let mut has_init = false;
                let mut has_update = false;
                let mut has_ui = false;
                let mut has_pick = false;
                for f in ast.iter_functions() {
                    match f.name {
                        "on_init" => has_init = true,
                        "on_update" => has_update = true,
                        "on_ui" => has_ui = true,
                        "on_pick" => has_pick = true,
                        _ => {}
                    }
                }
                (Some(ast), None, has_init, has_update, has_ui, has_pick)
            }
            Err(err) => (None, Some(err.to_string()), false, false, false, false),
        }
    }
}

pub struct RhaiAddonHost {
    pub addons_dir: PathBuf,
}

impl RhaiAddonHost {
    pub fn new(addons_dir: impl Into<PathBuf>) -> Self {
        Self {
            addons_dir: addons_dir.into(),
        }
    }
}

impl CadAddon for RhaiAddonHost {
    fn descriptor(&self) -> AddonDescriptor {
        AddonDescriptor {
            id: "builtin.rhai_host".into(),
            name: "Rhai Script Addon Runtime".into(),
            version: "1.0.0".into(),
            author: "CAD Core".into(),
            description: "Hot-reloadable Rhai script engine bridged directly into the ECS world & egui inspector.".into(),
            kind: AddonKind::NativeRust,
        }
    }

    fn build(&self, host: &mut CadPluginHost) {
        let mut manager = RhaiAddonManager::new(self.addons_dir.clone());
        {
            let mut reg = host.world.resource_mut::<AddonRegistry>();
            manager.scan_and_load(&mut reg);
        }
        host.world.insert_resource(manager);
        host.add_systems(CadSchedule::Update, rhai_runtime_tick_system);
    }
}

fn rhai_runtime_tick_system(
    mut manager: ResMut<RhaiAddonManager>,
    mut registry: ResMut<AddonRegistry>,
    mut panel_reg: ResMut<AddonPanelRegistry>,
    mut cmd_queue: ResMut<SceneCommandQueue>,
    mut overlay_queue: ResMut<RenderOverlayQueue>,
    mut events: MessageReader<CadEvent>,
    time: Res<CadTime>,
    stats: Res<ViewportStats>,
    meshes: Res<MeshLibrary>,
    parts_query: Query<(&CadPart, &WorldTransform, &PickableId, &PartVisibility, &SelectionState)>,
) {
    if !registry.is_enabled("builtin.rhai_host") {
        panel_reg.panels.clear();
        return;
    }

    let picked_events: Vec<(u32, String)> = events
        .read()
        .filter_map(|e| match e {
            CadEvent::PartPicked { pick_id, name } => Some((*pick_id, name.clone())),
            _ => None,
        })
        .collect();

    // 1. Snapshot ECS scene & UI inputs for scripts
    let mut parts_snap = Vec::new();
    for (part, wt, pid, vis, sel) in parts_query.iter() {
        let aabb = meshes
            .get(part.mesh)
            .map(|m| m.aabb.transformed(wt.0))
            .unwrap_or_default();
        parts_snap.push(PartSnapshot {
            pick_id: pid.0,
            name: part.name.clone(),
            group: part.group.clone(),
            aabb,
            selected: sel.selected,
            visible: vis.is_drawn(),
        });
    }

    {
        let mut shared = manager.shared.lock().unwrap();
        shared.parts = parts_snap;
        shared.selected_pick_id = stats.selected_pick_id;
        shared.selected_name = stats.selected_part.clone().unwrap_or_default();
        shared.elapsed = time.elapsed;
        shared.delta = time.delta;
        shared.float_values = panel_reg.float_values.clone();
        shared.bool_values = panel_reg.bool_values.clone();
        shared.clicked_buttons.clear();
        for click in panel_reg.clicked_buttons.drain(..) {
            shared
                .clicked_buttons
                .insert((click.addon_id, click.widget_id));
        }
        shared.out_commands.clear();
        shared.out_overlays.clear();
        shared.out_logs.clear();
    }

    panel_reg.panels.clear();

    let handle = CadScriptHandle {
        shared: manager.shared.clone(),
    };
    let RhaiAddonManager {
        engine,
        shared: shared_arc,
        addons,
        ..
    } = &mut *manager;

    for addon in addons {
        let is_enabled = registry.is_enabled(&addon.id);
        if !is_enabled {
            continue;
        }
        let Some(ast) = &addon.ast else {
            continue;
        };

        {
            let mut s = shared_arc.lock().unwrap();
            s.active_addon_id = addon.id.clone();
            s.out_widgets.clear();
        }

        let mut error_msg: Option<String> = None;

        if !addon.initialized && addon.has_init {
            if let Err(err) =
                engine.call_fn::<Dynamic>(&mut addon.scope, ast, "on_init", (handle.clone(),))
            {
                error_msg = Some(format!("on_init: {err}"));
            }
            addon.initialized = true;
        }

        if error_msg.is_none() && addon.has_pick {
            for (pid, name) in &picked_events {
                if let Err(err) = engine.call_fn::<Dynamic>(
                    &mut addon.scope,
                    ast,
                    "on_pick",
                    (handle.clone(), *pid as i64, name.clone()),
                ) {
                    error_msg = Some(format!("on_pick: {err}"));
                    break;
                }
            }
        }

        if error_msg.is_none() && addon.has_update {
            if let Err(err) = engine.call_fn::<Dynamic>(
                &mut addon.scope,
                ast,
                "on_update",
                (handle.clone(), time.delta as f64),
            ) {
                error_msg = Some(format!("on_update: {err}"));
            }
        }

        if error_msg.is_none() && addon.has_ui {
            if let Err(err) =
                engine.call_fn::<Dynamic>(&mut addon.scope, ast, "on_ui", (handle.clone(),))
            {
                error_msg = Some(format!("on_ui: {err}"));
            } else {
                let s = shared_arc.lock().unwrap();
                if !s.out_widgets.is_empty() {
                    panel_reg.panels.push(AddonPanelSpec {
                        addon_id: addon.id.clone(),
                        title: format!("{} (Rhai)", addon.name),
                        widgets: s.out_widgets.clone(),
                    });
                }
            }
        }

        if let Some(err) = error_msg {
            addon.last_error = Some(err.clone());
            if let Some(entry) = registry.entries.iter_mut().find(|e| e.id == addon.id) {
                entry.last_error = Some(err.clone());
            }
            registry.log(addon.id.clone(), err, true);
        }
    }

    // 3. Flush script outputs into ECS
    let (cmds, overlays, logs, floats, bools) = {
        let mut s = manager.shared.lock().unwrap();
        (
            std::mem::take(&mut s.out_commands),
            std::mem::take(&mut s.out_overlays),
            std::mem::take(&mut s.out_logs),
            s.float_values.clone(),
            s.bool_values.clone(),
        )
    };

    for (k, v) in floats {
        panel_reg.float_values.entry(k).or_insert(v);
    }
    for (k, v) in bools {
        panel_reg.bool_values.entry(k).or_insert(v);
    }
    for cmd in cmds {
        cmd_queue.push(cmd);
    }
    overlay_queue.lines.extend(overlays);
    for (aid, msg, is_err) in logs {
        registry.log(aid, msg, is_err);
    }
}
