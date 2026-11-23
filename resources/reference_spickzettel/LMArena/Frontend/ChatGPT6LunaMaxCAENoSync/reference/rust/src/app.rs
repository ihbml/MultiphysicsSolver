use crate::physics::{UnitValue, DIMENSIONLESS, LENGTH, PRESSURE, TEMPERATURE, VELOCITY};
use crate::schema::{BlockInstance, ModuleSchema, ParameterValue};
use crate::Cockpit;
use egui::{Color32, RichText, Sense, Stroke, Vec2};
use std::collections::BTreeMap;

#[derive(Default)]
pub struct NativeUiState {
    pub workspace: usize,
    pub selected_block: Option<String>,
    pub inputs: BTreeMap<String, String>,
    pub metric_threshold: f32,
    pub isolate_defects: bool,
    pub wall_resolution: WallResolutionUi,
    pub monitor_tick: u64,
    pub show_ipc: bool,
}

#[derive(Default, Clone, Copy, PartialEq, Eq)]
pub enum WallResolutionUi {
    #[default]
    FirstLayer,
    TargetYPlus,
}

const WORKSPACES: [&str; 5] = [
    "1. CAD & Domain",
    "2. Meshing & Quality",
    "3. Physics & BC Stack",
    "4. Solve & Live Monitors",
    "5. Post-Processing & Compare",
];

/// Immediate-mode native desktop layout. The host owns the wgpu Surface and calls
/// `viewport::CfdRenderer::render` after egui's panel pass.
pub fn draw(ctx: &egui::Context, cockpit: &mut Cockpit, state: &mut NativeUiState) {
    egui::TopBottomPanel::top("workspace_bar").exact_height(42.0).show(ctx, |ui| {
        ui.horizontal_centered(|ui| {
            ui.heading(RichText::new("FLUX / CFD").color(Color32::from_rgb(185, 245, 0)).strong());
            ui.separator();
            for (index, label) in WORKSPACES.iter().enumerate() {
                if ui.selectable_label(state.workspace == index, *label).clicked() { state.workspace = index; }
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let status = if cockpit.nan_cells > 0 { "NAN CELLS / STOP" } else if cockpit.solver_running { "SOLVING" } else { "READY" };
                ui.label(RichText::new(status).monospace().color(if cockpit.nan_cells > 0 { Color32::RED } else { Color32::LIGHT_GREEN }));
            });
        });
    });

    egui::TopBottomPanel::bottom("monitor_dock").resizable(true).default_height(150.0).show(ctx, |ui| {
        ui.horizontal(|ui| {
            ui.strong("LIVE MONITOR DOCK");
            ui.separator();
            ui.label("Ux  Uy  Uz  p  h  k  ω");
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button(if cockpit.solver_running { "Stop gracefully" } else { "Start solver" }).clicked() { cockpit.solver_running = !cockpit.solver_running; }
                if ui.button("Write now").clicked() { state.monitor_tick = state.monitor_tick.saturating_add(1); }
            });
        });
        let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 86.0), Sense::hover());
        let painter = ui.painter_at(rect);
        for index in 0..4 {
            let y = rect.top() + index as f32 * 26.0;
            painter.line_segment([egui::pos2(rect.left(), y), egui::pos2(rect.right(), y)], Stroke::new(1.0, Color32::from_gray(47)));
        }
        let palette = [Color32::from_rgb(86, 169, 255), Color32::from_rgb(155, 123, 255), Color32::from_rgb(185, 245, 0), Color32::from_rgb(252, 117, 180)];
        for (series, color) in palette.into_iter().enumerate() {
            let points = (0..80).map(|index| {
                let x = rect.left() + index as f32 / 79.0 * rect.width();
                let decay = (-(index as f32) / (13.0 + series as f32 * 2.0)).exp();
                let ripple = ((index as f32 * 0.31 + series as f32 + state.monitor_tick as f32 * 0.03).sin() * 0.025).abs();
                let y = rect.bottom() - (0.08 + decay * 0.78 + ripple) * rect.height();
                egui::pos2(x, y)
            }).collect::<Vec<_>>();
            painter.add(egui::Shape::line(points, Stroke::new(1.3, color)));
        }
        if cockpit.nan_cells > 0 {
            ui.colored_label(Color32::RED, format!("EMERGENCY: {} NaN cells", cockpit.nan_cells));
            cockpit.solver_running = false;
        }
    });

    egui::SidePanel::left("declarative_stack").resizable(true).default_width(292.0).show(ctx, |ui| {
        ui.horizontal(|ui| { ui.heading("BLOCK GRAPH"); ui.label(RichText::new("SCHEMA / 3.8").small().color(Color32::GRAY)); });
        ui.separator();
        let registry = cockpit.registry.schema();
        for block in &mut cockpit.blocks {
            let module = match registry.module(&block.module_id) { Some(module) => module, None => continue };
            draw_block(ui, module, block, state);
        }
        if ui.button("+ Add block from registry").clicked() {
            if let Some(module) = registry.module("pressure.reference") {
                cockpit.blocks.push(BlockInstance {
                    id: format!("block-{}-{}", module.id, cockpit.blocks.len()),
                    module_id: module.id.clone(), enabled: true,
                    parameters: module.parameters.iter().map(|parameter| (parameter.id.clone(), parameter.default.clone())).collect(),
                });
            }
        }
    });

    egui::SidePanel::right("selection_inspector").resizable(true).default_width(275.0).show(ctx, |ui| {
        ui.heading("INSPECTOR");
        ui.separator();
        ui.label(format!("Patch · {}", cockpit.selected_patch.as_deref().unwrap_or("none")));
        ui.horizontal(|ui| {
            if ui.selectable_label(cockpit.pressure_variable == crate::physics::PressureVariable::AbsoluteP, "p / absolute").clicked() {
                cockpit.pressure_variable = crate::physics::PressureVariable::AbsoluteP;
            }
            if ui.selectable_label(cockpit.pressure_variable == crate::physics::PressureVariable::ReducedPRgh, "p_rgh / reduced").clicked() {
                cockpit.pressure_variable = crate::physics::PressureVariable::ReducedPRgh;
            }
        });
        ui.label(RichText::new("[kg, m, s, K, mol, A, cd]").monospace().small().color(Color32::GRAY));
        ui.separator();
        ui.label("Wall resolution");
        ui.horizontal(|ui| {
            if ui.selectable_label(state.wall_resolution == WallResolutionUi::FirstLayer, "Δy₁ / meters").clicked() { state.wall_resolution = WallResolutionUi::FirstLayer; }
            if ui.selectable_label(state.wall_resolution == WallResolutionUi::TargetYPlus, "Target y⁺").clicked() { state.wall_resolution = WallResolutionUi::TargetYPlus; }
        });
        ui.label(RichText::new(match state.wall_resolution {
            WallResolutionUi::FirstLayer => "Physical first prism height; estimated y+ is derived from wall shear.",
            WallResolutionUi::TargetYPlus => "Dimensionless wall coordinate; not a first-cell length.",
        }).small().color(Color32::from_gray(150)));
        ui.separator();
        ui.label(format!("Geometry r{} / mesh r{}", cockpit.revisions.geometry(), cockpit.revisions.mesh()));
        if !cockpit.revisions.patch_bindings_current() {
            ui.colored_label(Color32::YELLOW, "Patch bindings are stale after remesh");
        }
        if ui.button("Bind patch to current mesh revision").clicked() { cockpit.revisions.bind_patch_revision(); }
        if state.workspace == 1 {
            ui.separator();
            ui.label("4-quadrant cell quality · flat values");
            ui.add(egui::Slider::new(&mut state.metric_threshold, 0.0..=1.0).text("isolation threshold"));
            ui.checkbox(&mut state.isolate_defects, "Isolate cells above limit");
        }
        ui.separator();
        ui.checkbox(&mut state.show_ipc, "Show Channel B diagnostics");
        if state.show_ipc { ui.label(RichText::new("JSON-RPC / NDJSON · async · stderr ring: 256 lines").small().monospace()); }
    });

    egui::CentralPanel::default().show(ctx, |ui| {
        ui.horizontal(|ui| {
            ui.label(RichText::new("VIEWPORT / CAMERA-RELATIVE").monospace().color(Color32::LIGHT_GRAY));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("Surface").clicked() { state.workspace = 0; }
                if ui.button("Quality").clicked() { state.workspace = 1; }
                if ui.button("Field").clicked() { state.workspace = 4; }
            });
        });
        let (rect, response) = ui.allocate_exact_size(ui.available_size(), Sense::click_and_drag());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, Color32::from_rgb(17, 20, 25));
        let center = rect.center();
        painter.line_segment([egui::pos2(rect.left() + 22.0, center.y), egui::pos2(rect.right() - 22.0, center.y)], Stroke::new(1.0, Color32::from_gray(39)));
        painter.line_segment([egui::pos2(center.x, rect.top() + 20.0), egui::pos2(center.x, rect.bottom() - 20.0)], Stroke::new(1.0, Color32::from_gray(39)));
        let foil = [
            egui::pos2(center.x - 205.0, center.y + 4.0), egui::pos2(center.x - 145.0, center.y - 25.0),
            egui::pos2(center.x + 65.0, center.y - 30.0), egui::pos2(center.x + 210.0, center.y),
            egui::pos2(center.x + 60.0, center.y + 25.0), egui::pos2(center.x - 145.0, center.y + 20.0),
            egui::pos2(center.x - 205.0, center.y + 4.0),
        ];
        painter.add(egui::Shape::line(foil.to_vec(), Stroke::new(2.0, Color32::from_rgb(97, 139, 185))));
        for index in 0..7 {
            let y = center.y - 90.0 + index as f32 * 30.0;
            let mut points = Vec::new();
            for step in 0..50 {
                let x = rect.left() + 24.0 + step as f32 / 49.0 * (rect.width() - 48.0);
                let bend = (-(x - center.x).powi(2) / 15000.0).exp() * (if y < center.y { -1.0 } else { 1.0 }) * 22.0;
                points.push(egui::pos2(x, y + bend));
            }
            painter.add(egui::Shape::line(points, Stroke::new(1.0, Color32::from_rgb(53, 114, 143))));
        }
        if response.clicked() {
            cockpit.selected_patch = Some("blade-wall".into());
        }
        painter.text(rect.left_top() + Vec2::new(12.0, 12.0), egui::Align2::LEFT_TOP,
            format!("{} · f64 CAD / f32 camera-relative", WORKSPACES[state.workspace]), egui::FontId::monospace(10.0), Color32::from_gray(180));
        painter.text(rect.right_bottom() - Vec2::new(12.0, 12.0), egui::Align2::RIGHT_BOTTOM,
            "Drag to orbit · scroll to zoom · face group ID pick", egui::FontId::monospace(9.0), Color32::from_gray(110));
    });
}

fn draw_block(ui: &mut egui::Ui, module: &ModuleSchema, block: &mut BlockInstance, state: &mut NativeUiState) {
    let open = state.selected_block.as_deref() == Some(block.id.as_str());
    let header = format!("{}    {}", module.category.to_uppercase(), module.label);
    let response = egui::CollapsingHeader::new(RichText::new(header).size(11.0))
        .id_source(&block.id)
        .default_open(open)
        .show(ui, |ui| {
            let values = block.parameters.clone();
            for parameter in module.parameters.iter().filter(|parameter| parameter.visible_if.as_ref().map_or(true, |condition| condition.evaluate(&values))) {
                let key = format!("{}:{}:{}", block.id, module.id, parameter.id);
                let fallback = match &parameter.default {
                    ParameterValue::Number(number) => format!("{number} {}", parameter.display_unit),
                    ParameterValue::Text(text) => text.clone(),
                    ParameterValue::Bool(value) => value.to_string(),
                };
                let input = state.inputs.entry(key.clone()).or_insert(fallback);
                ui.horizontal(|ui| {
                    ui.label(RichText::new(&parameter.label).small().color(Color32::from_gray(170)));
                    let response = ui.add(egui::TextEdit::singleline(input).desired_width(105.0).font(egui::TextStyle::Monospace));
                    if response.lost_focus() && ui.input(|input| input.key_pressed(egui::Key::Enter)) {
                        match &parameter.default {
                            ParameterValue::Text(_) => { block.parameters.insert(parameter.id.clone(), ParameterValue::Text(input.clone())); }
                            ParameterValue::Bool(_) => match input.parse::<bool>() {
                                Ok(value) => { block.parameters.insert(parameter.id.clone(), ParameterValue::Bool(value)); }
                                Err(error) => { response.on_hover_text(error.to_string()); }
                            },
                            ParameterValue::Number(_) => match UnitValue::parse(input, parameter.dimensions) {
                                Ok(value) => { block.parameters.insert(parameter.id.clone(), ParameterValue::Number(value.si_value)); }
                                Err(error) => { response.on_hover_text(error.to_string()); }
                            },
                        }
                    }
                });
                ui.label(RichText::new(format!("SI · {:?} · {}", parameter.dimensions, parameter.display_unit)).size(8.0).color(Color32::from_gray(105)));
            }
        });
    if response.header_response.clicked() {
        state.selected_block = if open { None } else { Some(block.id.clone()) };
    }
}

#[allow(dead_code)]
fn _unit_examples() {
    let _ = UnitValue::parse("12.5 mm", LENGTH);
    let _ = UnitValue::parse("850 C", TEMPERATURE);
    let _ = UnitValue::parse("3.2 bar", PRESSURE);
    let _ = UnitValue::parse("12.5 m/s", VELOCITY);
    let _ = UnitValue::parse("30", DIMENSIONLESS);
}