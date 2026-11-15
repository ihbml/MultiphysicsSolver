use std::collections::BTreeMap;
use std::path::PathBuf;

use bevy_ecs::prelude::*;
use cad_addons::RhaiAddonManager;
use cad_core::{DemoAssemblyKind, measure_angle_deg, measure_distance, ray_intersect_mesh};
use cad_plugin_api::{
    AdapterDetails, AddonPanelRegistry, AddonRegistry, AddonUiEvent, AddonWidget, CadMaterial,
    CadPart, CadPluginHost, CameraPreset, CameraState, MeasurementPoint, MeasurementToolState,
    MeshLibrary, PartVisibility, PickableId, RenderSettings, SceneCommand, SceneCommandQueue,
    SectionPlaneState, SelectionState, ShadingMode, ViewportStats, WorldTransform,
};
use glam::Vec3;

pub struct UiState {
    pub active_demo: DemoAssemblyKind,
    pub file_path_input: String,
    pub outliner_filter: String,
    pub show_addons_drawer: bool,
    pub selected_rhai_addon_id: Option<String>,
    pub rhai_editor_buffer: String,
    pub viewport_rect_px: [u32; 2],
    pub viewport_origin_pts: egui::Pos2,
    pub viewport_size_pts: egui::Vec2,
    pub pending_pick_pixel: Option<[u32; 2]>,
    pub pending_hover_pixel: Option<[u32; 2]>,
    pub pending_measure_uv: Option<[f32; 2]>,
}

impl Default for UiState {
    fn default() -> Self {
        Self {
            active_demo: DemoAssemblyKind::SpindleHousing,
            file_path_input: String::new(),
            outliner_filter: String::new(),
            show_addons_drawer: false,
            selected_rhai_addon_id: None,
            rhai_editor_buffer: String::new(),
            viewport_rect_px: [1280, 720],
            viewport_origin_pts: egui::Pos2::ZERO,
            viewport_size_pts: egui::vec2(1280.0, 720.0),
            pending_pick_pixel: None,
            pending_hover_pixel: None,
            pending_measure_uv: None,
        }
    }
}

pub fn apply_cad_theme(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = egui::Color32::from_rgb(20, 24, 31);
    visuals.window_fill = egui::Color32::from_rgb(23, 28, 36);
    visuals.extreme_bg_color = egui::Color32::from_rgb(13, 16, 22);
    visuals.faint_bg_color = egui::Color32::from_rgb(28, 33, 42);
    visuals.widgets.noninteractive.bg_stroke =
        egui::Stroke::new(1.0, egui::Color32::from_rgb(42, 50, 63));
    visuals.widgets.inactive.bg_fill = egui::Color32::from_rgb(33, 39, 49);
    visuals.widgets.hovered.bg_fill = egui::Color32::from_rgb(44, 54, 68);
    visuals.widgets.active.bg_fill = egui::Color32::from_rgb(34, 114, 156);
    visuals.selection.bg_fill = egui::Color32::from_rgb(34, 128, 178);
    visuals.selection.stroke = egui::Stroke::new(1.0, egui::Color32::from_rgb(72, 206, 255));
    ctx.set_visuals(visuals);
}

pub fn draw_cad_ui(
    ui: &mut egui::Ui,
    host: &mut CadPluginHost,
    ui_state: &mut UiState,
    viewport_tex_id: egui::TextureId,
    pixels_per_point: f32,
) {
    // 1. Top CAD Ribbon & Menu Bar
    egui::Panel::top("cad_top_bar").show(ui, |ui| {
        draw_top_toolbar(ui, &mut host.world, ui_state);
    });

    // 2. Bottom Telemetry Status Bar
    egui::Panel::bottom("cad_status_bar").show(ui, |ui| {
        draw_status_bar(ui, &host.world);
    });

    // 3. Optional Bottom Collapsible Addon Engine & Rhai Console Drawer
    if ui_state.show_addons_drawer {
        egui::Panel::bottom("cad_addons_drawer")
            .resizable(true)
            .default_size(220.0)
            .min_size(140.0)
            .max_size(420.0)
            .show(ui, |ui| {
                draw_addons_drawer(ui, &mut host.world, ui_state);
            });
    }

    // 4. Left Dock: Assembly Outliner
    egui::Panel::left("cad_outliner_panel")
        .resizable(true)
        .default_size(255.0)
        .min_size(190.0)
        .max_size(380.0)
        .show(ui, |ui| {
            draw_outliner_panel(ui, &mut host.world, ui_state);
        });

    // 5. Right Dock: Inspector + Rust ECS Addons + Rhai Script Addon Panels
    egui::Panel::right("cad_inspector_panel")
        .resizable(true)
        .default_size(300.0)
        .min_size(240.0)
        .max_size(440.0)
        .show(ui, |ui| {
            draw_inspector_panel(ui, host);
        });

    // 6. Central 3D Viewport
    egui::CentralPanel::no_frame().show(ui, |ui| {
        draw_viewport_panel(ui, &mut host.world, ui_state, viewport_tex_id, pixels_per_point);
    });
}

fn draw_top_toolbar(ui: &mut egui::Ui, world: &mut World, ui_state: &mut UiState) {
    ui.add_space(2.0);
    ui.horizontal_wrapped(|ui| {
        ui.label(
            egui::RichText::new("CAD-ECS")
                .strong()
                .color(egui::Color32::from_rgb(76, 212, 255)),
        );
        ui.separator();

        // Demo Assembly Selector
        egui::ComboBox::from_id_salt("demo_assembly_combo")
            .selected_text(ui_state.active_demo.label())
            .show_ui(ui, |ui| {
                for kind in DemoAssemblyKind::ALL {
                    if ui
                        .selectable_value(&mut ui_state.active_demo, kind, kind.label())
                        .clicked()
                    {
                        world
                            .resource_mut::<SceneCommandQueue>()
                            .push(SceneCommand::LoadDemoAssembly(kind));
                    }
                }
            });

        // Quick File Import
        ui.add(
            egui::TextEdit::singleline(&mut ui_state.file_path_input)
                .desired_width(175.0)
                .hint_text("Path to .stl / .obj / .glb ..."),
        );
        if ui.button("Import CAD").clicked() && !ui_state.file_path_input.trim().is_empty() {
            let p = PathBuf::from(ui_state.file_path_input.trim());
            world
                .resource_mut::<SceneCommandQueue>()
                .push(SceneCommand::LoadCadFile(p));
        }

        ui.separator();

        // Shading Mode Selector
        let mut settings = world.resource_mut::<RenderSettings>();
        egui::ComboBox::from_id_salt("shading_mode_combo")
            .selected_text(settings.shading_mode.label())
            .show_ui(ui, |ui| {
                for mode in ShadingMode::ALL {
                    ui.selectable_value(&mut settings.shading_mode, mode, mode.label());
                }
            });

        ui.toggle_value(&mut settings.show_edges, "Edges");
        ui.toggle_value(&mut settings.show_grid, "Grid");
        ui.toggle_value(&mut settings.isolate_selected, "Isolate");
        drop(settings);

        ui.separator();

        // Camera Presets
        if ui.button("Iso").clicked() {
            world
                .resource_mut::<CameraState>()
                .apply_preset(CameraPreset::Isometric);
        }
        if ui.button("Top").clicked() {
            world
                .resource_mut::<CameraState>()
                .apply_preset(CameraPreset::Top);
        }
        if ui.button("Front").clicked() {
            world
                .resource_mut::<CameraState>()
                .apply_preset(CameraPreset::Front);
        }
        if ui.button("Right").clicked() {
            world
                .resource_mut::<CameraState>()
                .apply_preset(CameraPreset::Right);
        }
        if ui.button("Fit").clicked() {
            world
                .resource_mut::<SceneCommandQueue>()
                .push(SceneCommand::FocusAll);
        }

        let mut cam = world.resource_mut::<CameraState>();
        let ortho_label = if cam.orbit.orthographic {
            "Ortho"
        } else {
            "Persp"
        };
        if ui.selectable_label(cam.orbit.orthographic, ortho_label).clicked() {
            cam.orbit.orthographic = !cam.orbit.orthographic;
        }
        drop(cam);

        ui.separator();

        // Section & Measurement toggles
        let mut sec_enabled = world.resource::<SectionPlaneState>().enabled;
        if ui.toggle_value(&mut sec_enabled, "Section Cut").clicked() {
            world.resource_mut::<SectionPlaneState>().enabled = sec_enabled;
        }

        let mut meas_active = world.resource::<MeasurementToolState>().active;
        if ui.toggle_value(&mut meas_active, "Measure").clicked() {
            let mut m = world.resource_mut::<MeasurementToolState>();
            m.active = meas_active;
            if !meas_active {
                m.points.clear();
                m.last_distance = None;
                m.last_angle_deg = None;
            }
        }

        ui.toggle_value(&mut ui_state.show_addons_drawer, "Addons Console");

        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            let details = world.resource::<AdapterDetails>();
            ui.label(
                egui::RichText::new(format!("{} | Rev-Z", details.backend_name))
                    .monospace()
                    .color(egui::Color32::from_rgb(132, 224, 165)),
            );
        });
    });
    ui.add_space(2.0);
}

fn draw_outliner_panel(ui: &mut egui::Ui, world: &mut World, ui_state: &mut UiState) {
    ui.heading("Assembly Tree");
    ui.horizontal(|ui| {
        ui.add(
            egui::TextEdit::singleline(&mut ui_state.outliner_filter)
                .desired_width(150.0)
                .hint_text("Filter parts..."),
        );
        if ui.button("Show All").clicked() {
            let mut q = world.query::<&mut PartVisibility>();
            for mut vis in q.iter_mut(world) {
                vis.visible = true;
            }
            world.resource_mut::<RenderSettings>().isolate_selected = false;
        }
    });
    ui.separator();

    struct OutlinerRow {
        entity: Entity,
        pick_id: u32,
        name: String,
        group: String,
        visible: bool,
        selected: bool,
        color: [f32; 4],
    }

    let filter = ui_state.outliner_filter.to_ascii_lowercase();
    let mut grouped: BTreeMap<String, Vec<OutlinerRow>> = BTreeMap::new();
    {
        let mut q = world.query::<(
            Entity,
            &PickableId,
            &CadPart,
            &PartVisibility,
            &SelectionState,
            &CadMaterial,
        )>();
        for (entity, pid, part, vis, sel, mat) in q.iter(world) {
            if !filter.is_empty()
                && !part.name.to_ascii_lowercase().contains(&filter)
                && !part.group.to_ascii_lowercase().contains(&filter)
            {
                continue;
            }
            grouped
                .entry(part.group.clone())
                .or_default()
                .push(OutlinerRow {
                    entity,
                    pick_id: pid.0,
                    name: part.name.clone(),
                    group: part.group.clone(),
                    visible: vis.visible,
                    selected: sel.selected,
                    color: mat.0.base_color,
                });
        }
    }

    let mut toggle_vis: Option<(Entity, bool)> = None;
    let mut select_pick: Option<u32> = None;
    let mut focus_pick: Option<u32> = None;

    egui::ScrollArea::vertical().show(ui, |ui| {
        for (group_name, mut rows) in grouped {
            rows.sort_by_key(|r| r.pick_id);
            egui::CollapsingHeader::new(format!("{} ({})", group_name, rows.len()))
                .default_open(true)
                .show(ui, |ui| {
                    for row in rows {
                        ui.horizontal(|ui| {
                            let mut v = row.visible;
                            if ui.checkbox(&mut v, "").changed() {
                                toggle_vis = Some((row.entity, v));
                            }
                            let c = row.color;
                            let swatch = egui::Color32::from_rgb(
                                (c[0].clamp(0.0, 1.0) * 255.0) as u8,
                                (c[1].clamp(0.0, 1.0) * 255.0) as u8,
                                (c[2].clamp(0.0, 1.0) * 255.0) as u8,
                            );
                            let (rect, _) = ui.allocate_exact_size(
                                egui::vec2(10.0, 10.0),
                                egui::Sense::hover(),
                            );
                            ui.painter().rect_filled(rect, 2.0, swatch);

                            let resp = ui.selectable_label(row.selected, &row.name);
                            if resp.clicked() {
                                select_pick = Some(row.pick_id);
                            }
                            if resp.double_clicked() {
                                select_pick = Some(row.pick_id);
                                focus_pick = Some(row.pick_id);
                            }
                        });
                    }
                });
        }
    });

    if let Some((e, v)) = toggle_vis
        && let Some(mut vis) = world.get_mut::<PartVisibility>(e)
    {
        vis.visible = v;
    }
    if let Some(pid) = select_pick {
        world
            .resource_mut::<SceneCommandQueue>()
            .push(SceneCommand::SelectPickId(Some(pid)));
    }
    if focus_pick.is_some() {
        world
            .resource_mut::<SceneCommandQueue>()
            .push(SceneCommand::FocusSelected);
    }
}

fn draw_inspector_panel(ui: &mut egui::Ui, host: &mut CadPluginHost) {
    egui::ScrollArea::vertical().show(ui, |ui| {
        ui.heading("Part Inspector");
        draw_selected_part_inspector(ui, &mut host.world);

        ui.separator();
        ui.heading("ECS Addon Modules");
        host.render_addon_uis(ui);

        ui.separator();
        draw_rhai_addon_panels(ui, &mut host.world);
    });
}

fn draw_selected_part_inspector(ui: &mut egui::Ui, world: &mut World) {
    let mut selected_info = None;
    {
        let meshes = world.resource::<MeshLibrary>();
        let mut q = world.query::<(
            Entity,
            &PickableId,
            &CadPart,
            &WorldTransform,
            &CadMaterial,
            &SelectionState,
        )>();
        for (entity, pid, part, wt, mat, sel) in q.iter(world) {
            if sel.selected {
                let (tris, edges, size) = meshes
                    .get(part.mesh)
                    .map(|m| {
                        let wa = m.aabb.transformed(wt.0);
                        (m.triangle_count(), m.edge_segment_count(), wa.size())
                    })
                    .unwrap_or((0, 0, Vec3::ZERO));
                selected_info = Some((
                    entity,
                    pid.0,
                    part.name.clone(),
                    part.group.clone(),
                    tris,
                    edges,
                    size,
                    mat.0,
                ));
                break;
            }
        }
    }

    if let Some((entity, pid, name, group, tris, edges, size, mut mat_data)) = selected_info {
        ui.label(egui::RichText::new(&name).strong());
        ui.monospace(format!("Group: {group}  |  PickID: #{pid}"));
        ui.monospace(format!("Triangles: {tris}  |  Crease Edges: {edges}"));
        ui.monospace(format!(
            "Bounds: {:.2} × {:.2} × {:.2}",
            size.x, size.y, size.z
        ));

        let mut changed = false;
        ui.horizontal(|ui| {
            ui.label("Base Color:");
            let mut rgb = [
                mat_data.base_color[0],
                mat_data.base_color[1],
                mat_data.base_color[2],
            ];
            if ui.color_edit_button_rgb(&mut rgb).changed() {
                mat_data.base_color[0] = rgb[0];
                mat_data.base_color[1] = rgb[1];
                mat_data.base_color[2] = rgb[2];
                changed = true;
            }
        });
        changed |= ui
            .add(egui::Slider::new(&mut mat_data.metallic, 0.0..=1.0).text("Metallic"))
            .changed();
        changed |= ui
            .add(egui::Slider::new(&mut mat_data.roughness, 0.05..=1.0).text("Roughness"))
            .changed();

        if changed && let Some(mut mat_comp) = world.get_mut::<CadMaterial>(entity) {
            mat_comp.0 = mat_data;
        }

        ui.horizontal(|ui| {
            if ui.button("Focus Part").clicked() {
                world
                    .resource_mut::<SceneCommandQueue>()
                    .push(SceneCommand::FocusSelected);
            }
            if ui.button("Clear Selection").clicked() {
                world
                    .resource_mut::<SceneCommandQueue>()
                    .push(SceneCommand::SelectPickId(None));
            }
        });
    } else {
        ui.label("Click any 3D part or select in the Assembly Tree.");
    }
}

fn draw_rhai_addon_panels(ui: &mut egui::Ui, world: &mut World) {
    let mut panel_reg = world.remove_resource::<AddonPanelRegistry>().unwrap();
    for panel in &panel_reg.panels {
        egui::CollapsingHeader::new(&panel.title)
            .default_open(true)
            .show(ui, |ui| {
                for widget in &panel.widgets {
                    match widget {
                        AddonWidget::Heading(t) => {
                            ui.label(egui::RichText::new(t).strong());
                        }
                        AddonWidget::Label(t) => {
                            ui.label(t);
                        }
                        AddonWidget::Readout { label, value } => {
                            ui.horizontal(|ui| {
                                ui.label(label);
                                ui.monospace(value);
                            });
                        }
                        AddonWidget::Button { id, label } => {
                            if ui.button(label).clicked() {
                                panel_reg.clicked_buttons.push(AddonUiEvent {
                                    addon_id: panel.addon_id.clone(),
                                    widget_id: id.clone(),
                                });
                            }
                        }
                        AddonWidget::Slider {
                            id,
                            label,
                            min,
                            max,
                            default,
                        } => {
                            let val = panel_reg
                                .float_values
                                .entry((panel.addon_id.clone(), id.clone()))
                                .or_insert(*default);
                            ui.add(egui::Slider::new(val, *min..=*max).text(label));
                        }
                        AddonWidget::Checkbox { id, label, default } => {
                            let val = panel_reg
                                .bool_values
                                .entry((panel.addon_id.clone(), id.clone()))
                                .or_insert(*default);
                            ui.checkbox(val, label);
                        }
                        AddonWidget::Separator => {
                            ui.separator();
                        }
                    }
                }
            });
    }
    world.insert_resource(panel_reg);
}

fn draw_addons_drawer(ui: &mut egui::Ui, world: &mut World, ui_state: &mut UiState) {
    ui.horizontal(|ui| {
        ui.heading("ECS Plugin Engine & Rhai Addon Studio");
        ui.separator();
        if ui.button("Rescan addons/ Directory").clicked() {
            let mut rhai = world.remove_resource::<RhaiAddonManager>().unwrap();
            let mut reg = world.resource_mut::<AddonRegistry>();
            rhai.scan_and_load(&mut reg);
            world.insert_resource(rhai);
        }
    });

    ui.columns(3, |cols| {
        // Col 0: Registered Addons List
        cols[0].label(egui::RichText::new("Loaded Plugins & Scripts").strong());
        egui::ScrollArea::vertical()
            .id_salt("addon_list_scroll")
            .show(&mut cols[0], |ui| {
                let mut reg = world.resource_mut::<AddonRegistry>();
                for entry in &mut reg.entries {
                    ui.group(|ui| {
                        ui.horizontal(|ui| {
                            ui.checkbox(&mut entry.enabled, "");
                            let is_sel =
                                ui_state.selected_rhai_addon_id.as_deref() == Some(&entry.id);
                            if entry.script_source.is_some() {
                                if ui.selectable_label(is_sel, &entry.name).clicked() {
                                    ui_state.selected_rhai_addon_id = Some(entry.id.clone());
                                    ui_state.rhai_editor_buffer =
                                        entry.script_source.clone().unwrap_or_default();
                                }
                            } else {
                                ui.label(egui::RichText::new(&entry.name).strong());
                            }
                        });
                        ui.small(format!("{} • v{}", entry.kind_label, entry.version));
                        if let Some(err) = &entry.last_error {
                            ui.colored_label(egui::Color32::from_rgb(255, 110, 100), err);
                        }
                    });
                }
            });

        // Col 1: Live Rhai Script Editor + Hot Reload
        cols[1].horizontal(|ui| {
            ui.label(egui::RichText::new("Rhai Live Hot-Reload Editor").strong());
            if let Some(sel_id) = ui_state.selected_rhai_addon_id.clone() {
                if ui.button("Apply & Hot-Reload").clicked() {
                    let mut rhai = world.remove_resource::<RhaiAddonManager>().unwrap();
                    let mut reg = world.remove_resource::<AddonRegistry>().unwrap();
                    let mut cmds = world.remove_resource::<SceneCommandQueue>().unwrap();
                    rhai.reload_script_from_source(
                        &sel_id,
                        ui_state.rhai_editor_buffer.clone(),
                        false,
                        &mut reg,
                        &mut cmds,
                    );
                    world.insert_resource(cmds);
                    world.insert_resource(reg);
                    world.insert_resource(rhai);
                }
                if ui.button("Save & Reload").clicked() {
                    let mut rhai = world.remove_resource::<RhaiAddonManager>().unwrap();
                    let mut reg = world.remove_resource::<AddonRegistry>().unwrap();
                    let mut cmds = world.remove_resource::<SceneCommandQueue>().unwrap();
                    rhai.reload_script_from_source(
                        &sel_id,
                        ui_state.rhai_editor_buffer.clone(),
                        true,
                        &mut reg,
                        &mut cmds,
                    );
                    world.insert_resource(cmds);
                    world.insert_resource(reg);
                    world.insert_resource(rhai);
                }
            }
        });
        egui::ScrollArea::vertical()
            .id_salt("rhai_editor_scroll")
            .show(&mut cols[1], |ui| {
                if ui_state.selected_rhai_addon_id.is_some() {
                    ui.add(
                        egui::TextEdit::multiline(&mut ui_state.rhai_editor_buffer)
                            .font(egui::TextStyle::Monospace)
                            .code_editor()
                            .desired_width(f32::INFINITY)
                            .desired_rows(8),
                    );
                } else {
                    ui.label("Click a Rhai addon on the left to inspect or hot-edit its code.");
                }
            });

        // Col 2: Event & Addon Log Console
        cols[2].label(egui::RichText::new("Addon Engine Log").strong());
        egui::ScrollArea::vertical()
            .id_salt("addon_log_scroll")
            .stick_to_bottom(true)
            .show(&mut cols[2], |ui| {
                let reg = world.resource::<AddonRegistry>();
                for item in reg.logs.iter().rev().take(40).collect::<Vec<_>>().into_iter().rev()
                {
                    let col = if item.is_error {
                        egui::Color32::from_rgb(255, 115, 105)
                    } else {
                        egui::Color32::from_rgb(175, 200, 222)
                    };
                    ui.colored_label(col, format!("[{}] {}", item.addon_id, item.message));
                }
            });
    });
}

fn draw_viewport_panel(
    ui: &mut egui::Ui,
    world: &mut World,
    ui_state: &mut UiState,
    viewport_tex_id: egui::TextureId,
    pixels_per_point: f32,
) {
    let avail = ui.available_size();
    let w_pts = avail.x.max(64.0);
    let h_pts = avail.y.max(64.0);
    ui_state.viewport_size_pts = egui::vec2(w_pts, h_pts);
    ui_state.viewport_rect_px = [
        ((w_pts * pixels_per_point).round() as u32).max(1),
        ((h_pts * pixels_per_point).round() as u32).max(1),
    ];

    let img_resp = ui.add(
        egui::Image::new((viewport_tex_id, egui::vec2(w_pts, h_pts)))
            .sense(egui::Sense::click_and_drag()),
    );
    let rect = img_resp.rect;
    ui_state.viewport_origin_pts = rect.min;

    // Camera Orbit / Pan / Zoom interactions
    if img_resp.dragged() {
        let delta = img_resp.drag_delta();
        let primary = ui.input(|i| i.pointer.primary_down());
        let secondary = ui.input(|i| i.pointer.secondary_down());
        let middle = ui.input(|i| i.pointer.middle_down());
        let shift = ui.input(|i| i.modifiers.shift);

        let mut cam = world.resource_mut::<CameraState>();
        if middle || secondary || (primary && shift) {
            let scale = cam.orbit.distance * 0.0018;
            let right = cam.orbit.right();
            let up = cam.orbit.up();
            cam.orbit.target -= right * (delta.x * scale);
            cam.orbit.target += up * (delta.y * scale);
        } else if primary {
            cam.turntable_active = false;
            cam.orbit.yaw += delta.x * 0.0075;
            cam.orbit.pitch = (cam.orbit.pitch + delta.y * 0.0075).clamp(-1.555, 1.555);
        }
    }

    if img_resp.hovered() {
        let scroll = ui.input(|i| i.smooth_scroll_delta.y);
        if scroll.abs() > 0.01 {
            let mut cam = world.resource_mut::<CameraState>();
            let factor = (1.0 - scroll * 0.0018).clamp(0.6, 1.5);
            cam.orbit.distance = (cam.orbit.distance * factor).clamp(0.15, 500.0);
        }
        if let Some(pos) = img_resp.hover_pos() {
            let local_x = ((pos.x - rect.min.x) * pixels_per_point).round() as i32;
            let local_y = ((pos.y - rect.min.y) * pixels_per_point).round() as i32;
            if local_x >= 0
                && local_y >= 0
                && (local_x as u32) < ui_state.viewport_rect_px[0]
                && (local_y as u32) < ui_state.viewport_rect_px[1]
            {
                ui_state.pending_hover_pixel = Some([local_x as u32, local_y as u32]);
            }
        }
    } else {
        ui_state.pending_hover_pixel = None;
    }

    if img_resp.clicked()
        && let Some(pos) = img_resp.interact_pointer_pos()
    {
        let local_x = ((pos.x - rect.min.x) * pixels_per_point).round() as i32;
        let local_y = ((pos.y - rect.min.y) * pixels_per_point).round() as i32;
        if local_x >= 0
            && local_y >= 0
            && (local_x as u32) < ui_state.viewport_rect_px[0]
            && (local_y as u32) < ui_state.viewport_rect_px[1]
        {
            if world.resource::<MeasurementToolState>().active {
                let u = (pos.x - rect.min.x) / rect.width().max(1.0);
                let v = (pos.y - rect.min.y) / rect.height().max(1.0);
                ui_state.pending_measure_uv = Some([u, v]);
            } else {
                ui_state.pending_pick_pixel = Some([local_x as u32, local_y as u32]);
            }
        }
    }

    // Resolve CPU exact raycast surface point if Measurement Tool is active
    if let Some(uv) = ui_state.pending_measure_uv.take() {
        resolve_measurement_click(world, uv, rect.width() / rect.height().max(1.0));
    }

    // Draw 3D Orientation Triad / ViewCube HUD in top-right corner of viewport
    draw_orientation_triad_hud(ui, world, rect);

    // Draw Hover & Measurement HUD in top-left corner of viewport
    draw_viewport_hud_overlays(ui, world, rect);
}

fn resolve_measurement_click(world: &mut World, uv: [f32; 2], aspect: f32) {
    let ray = world.resource::<CameraState>().orbit.ndc_to_ray(uv, aspect);
    let clip_plane = world.resource::<SectionPlaneState>().to_plane3();
    let meshes = world.remove_resource::<MeshLibrary>().unwrap();

    let mut best_hit: Option<(f32, Vec3, Vec3, String)> = None;
    {
        let mut q = world.query::<(&CadPart, &WorldTransform, &PartVisibility)>();
        for (part, wt, vis) in q.iter(world) {
            if !vis.is_drawn() {
                continue;
            }
            if let Some(mesh) = meshes.get(part.mesh)
                && let Some(hit) = ray_intersect_mesh(&ray, mesh, wt.0, clip_plane)
                && best_hit.as_ref().is_none_or(|b| hit.distance < b.0)
            {
                best_hit = Some((
                    hit.distance,
                    hit.world_pos,
                    hit.world_normal,
                    part.name.clone(),
                ));
            }
        }
    }
    world.insert_resource(meshes);

    if let Some((_, pos, normal, name)) = best_hit {
        let mut meas = world.resource_mut::<MeasurementToolState>();
        if meas.points.len() >= 2 {
            meas.points.clear();
        }
        meas.points.push(MeasurementPoint {
            world_pos: pos,
            world_normal: normal,
            part_name: name,
        });
        if meas.points.len() == 2 {
            let a = &meas.points[0];
            let b = &meas.points[1];
            meas.last_distance = Some(measure_distance(a.world_pos, b.world_pos));
            meas.last_angle_deg = Some(measure_angle_deg(a.world_normal, b.world_normal));
        } else {
            meas.last_distance = None;
            meas.last_angle_deg = None;
        }
    }
}

fn draw_orientation_triad_hud(ui: &egui::Ui, world: &mut World, rect: egui::Rect) {
    let center = egui::pos2(rect.max.x - 58.0, rect.min.y + 58.0);
    let painter = ui.painter_at(rect);

    painter.circle_filled(
        center,
        42.0,
        egui::Color32::from_rgba_unmultiplied(15, 19, 26, 200),
    );
    painter.circle_stroke(
        center,
        42.0,
        egui::Stroke::new(1.0, egui::Color32::from_rgb(56, 68, 86)),
    );

    let view = world.resource::<CameraState>().orbit.view_matrix();
    let axes = [
        (Vec3::X, "X", egui::Color32::from_rgb(235, 78, 78)),
        (Vec3::Y, "Y", egui::Color32::from_rgb(86, 215, 120)),
        (Vec3::Z, "Z", egui::Color32::from_rgb(76, 156, 248)),
    ];

    let mut projected: Vec<(f32, egui::Pos2, &str, egui::Color32)> = axes
        .into_iter()
        .map(|(dir, label, col)| {
            let v = view.transform_vector3(dir);
            let p = egui::pos2(center.x + v.x * 28.0, center.y - v.y * 28.0);
            (v.z, p, label, col)
        })
        .collect();
    projected.sort_by(|a, b| a.0.total_cmp(&b.0));

    for (_, end_pos, label, col) in projected {
        painter.line_segment([center, end_pos], egui::Stroke::new(2.2, col));
        painter.circle_filled(end_pos, 7.0, col);
        painter.text(
            end_pos,
            egui::Align2::CENTER_CENTER,
            label,
            egui::FontId::monospace(9.5),
            egui::Color32::BLACK,
        );
    }
}

fn draw_viewport_hud_overlays(ui: &egui::Ui, world: &World, rect: egui::Rect) {
    let painter = ui.painter_at(rect);
    let stats = world.resource::<ViewportStats>();
    let meas = world.resource::<MeasurementToolState>();
    let sec = world.resource::<SectionPlaneState>();

    let mut lines = Vec::new();
    if let Some(hovered) = &stats.hovered_part {
        lines.push(format!("Hover: {} (#{})", hovered, stats.hovered_pick_id));
    }
    if sec.enabled {
        let n = sec.active_normal();
        lines.push(format!(
            "Section Cut Active: n=({:.2}, {:.2}, {:.2}) offset={:.2}",
            n.x, n.y, n.z, sec.offset
        ));
    }
    if meas.active {
        if let (Some(d), Some(ang)) = (meas.last_distance, meas.last_angle_deg) {
            lines.push(format!("Measure Δd = {:.4} u  |  Face Angle = {:.2}°", d, ang));
        } else if meas.points.len() == 1 {
            lines.push(format!(
                "Measure Point A on '{}' — click Point B",
                meas.points[0].part_name
            ));
        } else {
            lines.push("Measure Mode: click two 3D surface points".to_string());
        }
    }

    if !lines.is_empty() {
        let box_pos = egui::pos2(rect.min.x + 14.0, rect.min.y + 14.0);
        let text = lines.join("\n");
        let galley = painter.layout_no_wrap(
            text,
            egui::FontId::monospace(12.0),
            egui::Color32::from_rgb(228, 240, 250),
        );
        let bg_rect = egui::Rect::from_min_size(box_pos, galley.size() + egui::vec2(16.0, 10.0));
        painter.rect_filled(
            bg_rect,
            4.0,
            egui::Color32::from_rgba_unmultiplied(12, 16, 22, 215),
        );
        painter.rect_stroke(
            bg_rect,
            4.0,
            egui::Stroke::new(1.0, egui::Color32::from_rgb(56, 130, 175)),
            egui::StrokeKind::Inside,
        );
        painter.galley(box_pos + egui::vec2(8.0, 5.0), galley, egui::Color32::WHITE);
    }
}

fn draw_status_bar(ui: &mut egui::Ui, world: &World) {
    let stats = world.resource::<ViewportStats>();
    let details = world.resource::<AdapterDetails>();
    ui.horizontal(|ui| {
        ui.monospace(format!(
            "GPU: {} ({}, {}x MSAA)",
            details.adapter_name, details.backend_name, details.msaa_samples
        ));
        ui.separator();
        ui.monospace(format!(
            "{:.1} FPS ({:.2} ms)  |  Draw Calls: {}",
            stats.fps, stats.frame_time_ms, stats.draw_calls
        ));
        ui.separator();
        ui.monospace(format!(
            "Parts: {}/{}  |  Tris: {}  |  Edges: {}",
            stats.visible_parts, stats.total_parts, stats.total_triangles, stats.total_edges
        ));
    });
}
