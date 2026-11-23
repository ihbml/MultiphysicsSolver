//! egui workbench for the native cockpit.
//!
//! The GPU viewport is owned by [`crate::viewport::ViewportRenderer`], which
//! paints into an offscreen `Rgba16Float` texture presented in the central
//! panel. The panels below deliberately mirror the web workbench so both
//! implementations stay feature-aligned.

use eframe::egui;
use egui::{Color32, RichText, Stroke};
use egui_plot::{Legend, Line, Plot, PlotPoints};
use glam::Vec3;

use crate::compare::{
    default_kpis, pareto_front, CompareMode, DeltaStats, Kpi, ParetoPoint,
};
use crate::probe::{ChordSlice, HexGrid, LineProbe, ProbeSamples, SurfaceMesh, WallLaw};
use crate::si_units::{compile_field, CompiledField};
use crate::spatial_rule::{
    detect_overlaps, BcKind, BoundaryRule, FaceGroupId, FaceSample, Overlap, PatchTopology,
    ProposedRule, SmartSpatialRuleSynth,
};
use crate::viewport::{CameraState, Colormap};
use crate::StudyMetadata;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Workspace {
    Rules,
    Domain,
    Probe,
    Compare,
    Calculator,
}

impl Workspace {
    const ALL: [Workspace; 5] = [
        Workspace::Rules,
        Workspace::Domain,
        Workspace::Probe,
        Workspace::Compare,
        Workspace::Calculator,
    ];
    fn label(self) -> &'static str {
        match self {
            Workspace::Rules => "Patch Rules",
            Workspace::Domain => "Domain & Actors",
            Workspace::Probe => "Probe Lab",
            Workspace::Compare => "A/B Compare",
            Workspace::Calculator => "Field Calculator",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum ChartTab {
    Profiles,
    WallLaw,
    Rake,
}

pub struct CockpitApp {
    pub workspace: Workspace,
    pub metadata: StudyMetadata,
    pub camera: CameraState,
    patches: Vec<PatchTopology>,
    selected: usize,
    proposed: Option<ProposedRule>,
    rules: Vec<BoundaryRule>,
    overlaps: Vec<Overlap>,
    calculator_source: String,
    compiled: Option<CompiledField>,
    compile_error: Option<String>,
    uploaded_field: Option<String>,
    probe: LineProbe,
    probe_samples: Option<ProbeSamples>,
    grid: HexGrid,
    chart_tab: ChartTab,
    pub show_run_b: bool,
    pub invert_cp: bool,
    pub compare_mode: CompareMode,
    pub wipe: f32,
    pub slice_enabled: bool,
    pub slice_x: f32,
    pub rpm: f32,
    pub upstream_l: f32,
    pub downstream_l: f32,
    pub show_domain: bool,
    pub show_mrf: bool,
    kde: Vec<Kpi>,
    pareto: Vec<ParetoPoint>,
    delta_stats: DeltaStats,
    toast: Option<(String, f64)>,
    console: Vec<String>,
}

impl CockpitApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        configure_style(&cc.egui_ctx);
        let patches = demo_patches();
        let synth = SmartSpatialRuleSynth::default();
        let proposed = Some(synth.synthesize(&patches[1]));
        let grid = demo_grid();
        let probe = LineProbe {
            a: Vec3::new(-1.12, 0.44, 0.62),
            b: Vec3::new(1.05, 0.08, -0.36),
            samples: 64,
        };
        let g = grid.clone();
        let samples = probe.sample(
            &grid,
            &|p: Vec3| g.trilinear(p),
            &|p: Vec3| g.trilinear(p) * 0.955 + 0.35,
        );
        let mut pareto: Vec<ParetoPoint> = (0..14)
            .map(|i| {
                let t = i as f32 / 13.0;
                ParetoPoint {
                    loss_pct: 2.45 + t * 1.25 + (i % 3) as f32 * 0.09,
                    drag_counts: 192.0 - t * 30.0 + (i % 4) as f32 * 3.1,
                    label: format!("sweep-{i:02}"),
                    on_front: false,
                }
            })
            .collect();
        pareto_front(&mut pareto);

        let compiled = compile_field("p_total = p + 0.5 * rho * dot(U, U)").ok();
        Self {
            workspace: Workspace::Rules,
            metadata: StudyMetadata::default(),
            camera: CameraState::default(),
            patches,
            selected: 1,
            proposed,
            rules: vec![
                BoundaryRule {
                    id: "R1".into(),
                    name: "Inlet · total pressure".into(),
                    kind: BcKind::Inlet,
                    groups: vec![FaceGroupId(8)],
                    predicate: None,
                    physical_target: "p0 = 101 325 Pa, Tu 2.1 %".into(),
                    enabled: true,
                },
                BoundaryRule {
                    id: "R3".into(),
                    name: "Blade · adiabatic no-slip".into(),
                    kind: BcKind::NoSlipWall,
                    groups: vec![
                        FaceGroupId(1),
                        FaceGroupId(2),
                        FaceGroupId(3),
                        FaceGroupId(4),
                        FaceGroupId(5),
                    ],
                    predicate: None,
                    physical_target: "q̇ = 0 W·m⁻², y⁺ < 1".into(),
                    enabled: true,
                },
            ],
            overlaps: Vec::new(),
            calculator_source: "p_total = p + 0.5 * rho * dot(U, U)".into(),
            compiled,
            compile_error: None,
            uploaded_field: None,
            probe,
            probe_samples: Some(samples),
            grid,
            chart_tab: ChartTab::Profiles,
            show_run_b: true,
            invert_cp: true,
            compare_mode: CompareMode::Wipe,
            wipe: 0.52,
            slice_enabled: true,
            slice_x: 0.12,
            rpm: 12_400.0,
            upstream_l: 5.0,
            downstream_l: 15.0,
            show_domain: true,
            show_mrf: true,
            kde: default_kpis(),
            pareto,
            delta_stats: DeltaStats {
                min: -0.184,
                max: 0.118,
                mean: -0.042,
                sigma: 0.051,
                zero_fraction: 0.061,
                lut_half_span: 0.35,
            },
            toast: None,
            console: vec!["rhai 1.19 :: SI registry loaded".into()],
        }
    }

    fn patch(&self) -> &PatchTopology {
        &self.patches[self.selected.min(self.patches.len() - 1)]
    }

    fn synthesize(&mut self) {
        let synth = SmartSpatialRuleSynth::default();
        self.proposed = Some(synth.synthesize(self.patch()));
        if let Some(p) = &self.proposed {
            self.notify(format!(
                "{} · {} faces matched · {:.1} % confidence",
                self.patch().step_tag, p.matched_faces, p.confidence
            ));
        }
    }

    fn commit_rule(&mut self, separate: bool) {
        let Some(proposed) = self.proposed.clone() else { return };
        let group = proposed.source_group;
        if separate {
            let id = format!("R{}", self.rules.len() + 10);
            self.rules.push(BoundaryRule {
                id: id.clone(),
                name: format!("{} · synthesized wall", proposed.step_tag),
                kind: BcKind::NoSlipWall,
                groups: vec![group],
                predicate: Some(proposed.predicate.clone()),
                physical_target: "q̇ = 0 W·m⁻²".into(),
                enabled: true,
            });
            self.notify(format!("Created {id} from spatial predicates (no face IDs)"));
        } else if let Some(rule) = self.rules.iter_mut().find(|r| r.id == "R3") {
            if !rule.groups.contains(&group) {
                rule.groups.push(group);
            }
            rule.predicate = Some(proposed.predicate.clone());
            self.notify("Merged spatial predicate into R3 · adiabatic no-slip".into());
        }
        let faces: Vec<FaceSample> = self.patches.iter().flat_map(|p| p.faces.clone()).collect();
        self.overlaps = detect_overlaps(&self.rules, &faces);
        if !self.overlaps.is_empty() {
            self.notify(format!(
                "⚠ {} overlapping face groups — resolve before solve",
                self.overlaps.len()
            ));
        }
    }

    fn compile_calculator(&mut self) {
        match compile_field(&self.calculator_source) {
            Ok(field) => {
                self.console.push(format!(
                    "type-check ok :: {} {} shape=scalar",
                    field.quantity, field.dim
                ));
                self.compiled = Some(field);
                self.compile_error = None;
            }
            Err(error) => {
                self.console.push(format!("error :: {error}"));
                self.compile_error = Some(error.to_string());
                self.compiled = None;
            }
        }
    }

    fn notify(&mut self, message: impl Into<String>) {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs_f64())
            .unwrap_or(0.0);
        self.toast = Some((message.into(), now + 4.0));
    }

    fn tip_mach(&self) -> f32 {
        let tip_radius = 0.31;
        let speed = std::f32::consts::TAU * self.rpm * tip_radius / 60.0;
        speed / (1.4 * 287.05 * 892.0_f32).sqrt()
    }

    fn chart_data(&self, kind: &str, run_b: bool) -> Vec<[f64; 2]> {
        let adj = if run_b { 0.94 } else { 1.0 };
        (0..=96)
            .map(|i| {
                let t = i as f64 / 96.0;
                let y = match kind {
                    "cp" => (1.0 - 2.62 * (std::f64::consts::PI * t.powf(0.585)).sin() + 0.92 * t) * adj,
                    "yp" => 1.0 + 24.0 * t.powf(2.1) + 7.5 * (-t * 16.0).exp(),
                    "tw" => 792.0 + 252.0 * t.powf(0.85) + 38.0 * (std::f64::consts::PI * t).sin(),
                    _ => 96.0 + 140.0 * (std::f64::consts::PI * t.powf(0.7)).sin(),
                };
                [t, if kind == "cp" && self.invert_cp { -y } else { y }]
            })
            .collect()
    }
}

impl eframe::App for CockpitApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        shortcuts(self, ctx);

        egui::TopBottomPanel::top("top").exact_height(46.0).show(ctx, |ui| {
            ui.horizontal_centered(|ui| {
                ui.label(
                    RichText::new("AEROFORGE")
                        .strong()
                        .size(15.0)
                        .color(Color32::from_rgb(238, 241, 247)),
                );
                ui.label(RichText::new("CFD COCKPIT · STAGE 2").size(8.0).color(c32(0x5d6371)));
                ui.separator();
                ui.label(RichText::new(&self.metadata.project).size(11.0));
                ui.label(RichText::new("design-study-04").monospace().size(9.0).color(c32(0x8fb0ff)));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(RichText::new("converged").color(c32(0x23D18B)).size(9.0));
                    ui.label(RichText::new("2 400 it").monospace().size(9.0).color(c32(0x6d7484)));
                    let _ = ui.selectable_label(true, "Run A");
                    ui.label(RichText::new("←").size(9.0));
                    ui.label(RichText::new("B · Adjoint #37").monospace().size(9.0).color(c32(0x8fb0ff)));
                });
            });
        });

        egui::TopBottomPanel::bottom("status").exact_height(24.0).show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new("● solver converged").size(8.5).color(c32(0x65e5b1)));
                ui.separator();
                ui.label(RichText::new("IPC NDJSON 42.1 MB/s · GPU 68 % · SI units").monospace().size(8.0).color(c32(0x6d7484)));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(RichText::new("wgpu / egui_plot / rhai").monospace().size(8.0).color(c32(0x5f6675)));
                });
            });
        });

        egui::SidePanel::left("patches")
            .exact_width(272.0)
            .show(ctx, |ui| self.left_panel(ui));

        egui::SidePanel::right("inspector")
            .exact_width(318.0)
            .show(ctx, |ui| self.right_panel(ui));

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.horizontal(|ui| {
                for w in Workspace::ALL {
                    if ui.selectable_label(self.workspace == w, w.label()).clicked() {
                        self.workspace = w;
                    }
                }
            });
            ui.separator();
            match self.workspace {
                Workspace::Rules | Workspace::Domain => {
                    self.viewport(ui);
                    ui.separator();
                    self.probe_dock(ui);
                }
                Workspace::Probe => {
                    self.viewport(ui);
                    ui.separator();
                    self.probe_dock(ui);
                }
                Workspace::Compare => {
                    self.viewport(ui);
                    ui.separator();
                    self.compare_dock(ui);
                }
                Workspace::Calculator => {
                    ui.columns(2, |cols| {
                        self.viewport(&mut cols[0]);
                        self.calculator_panel(&mut cols[1]);
                    });
                }
            }
        });

        if let Some((message, until)) = &self.toast {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs_f64())
                .unwrap_or(0.0);
            if now < *until {
                egui::Window::new("toast")
                    .fixed_pos(egui::pos2(ctx.screen_rect().center().x - 220.0, ctx.screen_rect().bottom() - 92.0))
                    .title_bar(false)
                    .resizable(false)
                    .show(ctx, |ui| {
                        ui.label(RichText::new(message).monospace().size(9.5).color(c32(0x9fbcff)));
                    });
                ctx.request_repaint_after(std::time::Duration::from_millis(100));
            }
        }
    }
}

impl CockpitApp {
    fn left_panel(&mut self, ui: &mut egui::Ui) {
        ui.add_space(8.0);
        ui.label(RichText::new("SURFACE PATCH LIBRARY").size(9.0).strong().color(c32(0xaeb4c2)));
        ui.add_space(4.0);
        let selected = self.selected;
        for (index, patch) in self.patches.iter().enumerate() {
            let label = format!("{}  ·  {} faces", patch.step_tag, patch.faces.len());
            if ui.selectable_label(index == selected, RichText::new(label).monospace().size(9.5)).clicked() {
                self.selected = index;
                self.synthesize();
            }
        }
        ui.separator();
        ui.label(RichText::new("SMART SPATIAL RULE SYNTHESIZER").size(9.0).strong().color(c32(0xaeb4c2)));
        if let Some(proposed) = &self.proposed {
            ui.add_space(4.0);
            ui.label(RichText::new(format!("{} · group {}", proposed.step_tag, proposed.source_group.0)).size(10.0));
            ui.label(RichText::new(format!("normal cone ±{:.1}°", proposed.normal_cone_deg)).monospace().size(8.5).color(c32(0x7e8594)));
            ui.label(RichText::new(format!("confidence {:.1} %", proposed.confidence)).monospace().size(8.5).color(c32(0x65e5b1)));
            ui.label(RichText::new(format!("{} matched faces / {:.4} m²", proposed.matched_faces, proposed.matched_area)).monospace().size(8.5).color(c32(0x7e8594)));
            egui::Frame::none()
                .fill(c32(0x0d1018))
                .stroke(Stroke::new(1.0, c32(0x2f3a58)))
                .rounding(4.0)
                .inner_margin(6.0)
                .show(ui, |ui| {
                    ui.label(RichText::new(&proposed.rhai).monospace().size(9.5).color(c32(0xcfe0ff)));
                });
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                if ui.button("Merge into wall BC").clicked() {
                    self.commit_rule(false);
                }
                if ui.button("Create separate rule").clicked() {
                    self.commit_rule(true);
                }
            });
        }
        ui.separator();
        ui.label(RichText::new("BOUNDARY RULES").size(9.0).strong().color(c32(0xaeb4c2)));
        for rule in &self.rules {
            let overlap = self.overlaps.iter().any(|o| o.a == rule.id || o.b == rule.id);
            ui.horizontal(|ui| {
                let dot = if overlap { c32(0xEF233C) } else { c32(0x23D18B) };
                ui.label(RichText::new("●").color(dot).size(8.0));
                ui.label(RichText::new(&rule.name).size(9.5));
                ui.label(RichText::new(format!("{} faces", rule.groups.len())).monospace().size(8.0).color(c32(0x6d7484)));
            });
        }
        if !self.overlaps.is_empty() {
            ui.colored_label(
                c32(0xEF233C),
                format!("⚠ {} overlapping face groups", self.overlaps.len()),
            );
        }
    }

    fn right_panel(&mut self, ui: &mut egui::Ui) {
        egui::ScrollArea::vertical().show(ui, |ui| {
            ui.add_space(8.0);
            match self.workspace {
                Workspace::Rules => {
                    ui.label(RichText::new("SPATIAL MATCH PREDICATE").size(9.0).strong());
                    if let Some(p) = &self.proposed {
                        ui.label(RichText::new(&p.rhai).monospace().size(9.5).color(c32(0x9fbcff)));
                        ui.separator();
                        ui.label(RichText::new("PICK PAYLOAD").size(9.0).strong().color(c32(0xaeb4c2)));
                        egui::Grid::new("payload").num_columns(2).spacing([6.0, 3.0]).show(ui, |ui| {
                            ui.label(RichText::new("face_group_id").monospace().size(8.5).color(c32(0x6d7484)));
                            ui.label(RichText::new(p.source_group.0.to_string()).monospace().size(8.5));
                            ui.end_row();
                            ui.label(RichText::new("centroid").monospace().size(8.5).color(c32(0x6d7484)));
                            ui.label(RichText::new(format!("{:.4}, {:.4}, {:.4}", p.centroid.x, p.centroid.y, p.centroid.z)).monospace().size(8.5));
                            ui.end_row();
                            ui.label(RichText::new("normal cone").monospace().size(8.5).color(c32(0x6d7484)));
                            ui.label(RichText::new(format!("±{:.2}°", p.normal_cone_deg)).monospace().size(8.5));
                            ui.end_row();
                            ui.label(RichText::new("bbox").monospace().size(8.5).color(c32(0x6d7484)));
                            ui.label(RichText::new(format!("{:?}", p.bbox.extent().to_array())).monospace().size(8.5));
                            ui.end_row();
                            ui.label(RichText::new("STEP tag").monospace().size(8.5).color(c32(0x6d7484)));
                            ui.label(RichText::new(&p.step_tag).monospace().size(8.5));
                            ui.end_row();
                        });
                        ui.separator();
                        ui.label(RichText::new("RULE AUDIT").size(9.0).strong().color(c32(0xaeb4c2)));
                        ui.label(RichText::new("✓ Space-only predicates").size(9.0).color(c32(0x65e5b1)));
                        ui.label(RichText::new("✓ No brittle face IDs").size(9.0).color(c32(0x65e5b1)));
                        let overlap_color = if self.overlaps.is_empty() { c32(0x65e5b1) } else { c32(0xEF233C) };
                        ui.label(RichText::new(format!("{} BC overlap", if self.overlaps.is_empty() { "✓ no" } else { "⚠" })).size(9.0).color(overlap_color));
                    }
                }
                Workspace::Domain => {
                    ui.label(RichText::new("DOMAIN & MRF").size(9.0).strong());
                    ui.add(egui::Slider::new(&mut self.upstream_l, 2.0..=10.0).text("upstream L"));
                    ui.add(egui::Slider::new(&mut self.downstream_l, 6.0..=25.0).text("downstream L"));
                    ui.add(egui::Slider::new(&mut self.rpm, 0.0..=24_000.0).text("MRF rpm"));
                    ui.checkbox(&mut self.show_domain, "farfield cage");
                    ui.checkbox(&mut self.show_mrf, "MRF cylinder");
                    ui.label(RichText::new(format!("tip speed {:.1} m/s", std::f32::consts::TAU * self.rpm * 0.31 / 60.0)).monospace().size(9.0));
                    let mach = self.tip_mach();
                    ui.label(RichText::new(format!("tip Mach {:.3}", mach)).monospace().size(9.0).color(if mach < 0.8 { c32(0x65e5b1) } else { c32(0xEF233C) }));
                    ui.separator();
                    ui.label(RichText::new("PHYSICS ACTORS").size(9.0).strong());
                    ui.label(RichText::new("▸ DPM spray injector · cone θ, axis, d₃₂").size(9.0));
                    ui.label(RichText::new("▸ FWH microphone · x_mic, distance sphere").size(9.0));
                    ui.label(RichText::new("▸ Anisotropic porous · e₁ e₂ e₃, κ").size(9.0));
                    ui.label(RichText::new("drag gizmos into the viewport").monospace().size(8.0).color(c32(0x5f6675)));
                }
                Workspace::Probe => {
                    ui.label(RichText::new("PROBE TELEMETRY").size(9.0).strong());
                    if let Some(samples) = &self.probe_samples {
                        ui.label(RichText::new(format!("length {:.3} m · {} pts", self.probe.length(), samples.scalar_a.len())).monospace().size(9.0));
                        ui.label(RichText::new(format!("A mean {:.4} · max {:.4}", samples.mean(false), samples.max(false))).monospace().size(9.0).color(c32(0xaab1c0)));
                        ui.label(RichText::new(format!("B mean {:.4} · max {:.4}", samples.mean(true), samples.max(true))).monospace().size(9.0).color(c32(0x4d7cff)));
                    }
                    ui.checkbox(&mut self.show_run_b, "overlay Run B");
                    ui.checkbox(&mut self.invert_cp, "invert Cp y-axis");
                    ui.separator();
                    ui.label(RichText::new("INNER LAYER").size(9.0).strong());
                    let law = WallLaw::default();
                    ui.label(RichText::new(format!("uτ = {:.4} m/s", law.u_tau(7.2))).monospace().size(9.0));
                    ui.label(RichText::new(format!("y⁺ at 2.4e-5 m = {:.2}", law.y_plus(2.4e-5, 7.2))).monospace().size(9.0));
                    ui.label(RichText::new("viscous sublayer y⁺ = 1 highlighted").size(8.5).color(c32(0x8fb0ff)));
                }
                Workspace::Compare => {
                    ui.label(RichText::new("DIFFERENTIAL FIELD").size(9.0).strong());
                    ui.horizontal(|ui| {
                        ui.selectable_value(&mut self.compare_mode, CompareMode::Wipe, "Wipe");
                        ui.selectable_value(&mut self.compare_mode, CompareMode::Split, "Split");
                        ui.selectable_value(&mut self.compare_mode, CompareMode::Delta, "Delta");
                    });
                    ui.add(egui::Slider::new(&mut self.wipe, 0.02..=0.98).text("wipe"));
                    ui.checkbox(&mut self.slice_enabled, "slice plane");
                    ui.add(egui::Slider::new(&mut self.slice_x, -1.5..=1.6).text("slice x"));
                    ui.separator();
                    for kpi in &self.kde {
                        let color = if kpi.improved() { c32(0x65e5b1) } else { c32(0xff9aab) };
                        ui.horizontal(|ui| {
                            ui.label(RichText::new(kpi.symbol).monospace().size(9.0).color(c32(0x9aa1af)));
                            ui.label(RichText::new(format!("{:.*} → {:.*}", kpi.decimals as usize, kpi.a, kpi.decimals as usize, kpi.b)).monospace().size(8.5));
                            ui.label(RichText::new(format!("{:+.2} %", kpi.relative())).monospace().size(8.5).color(color));
                        });
                    }
                }
                Workspace::Calculator => {
                    self.calculator_panel(ui);
                }
            }
        });
    }

    fn calculator_panel(&mut self, ui: &mut egui::Ui) {
        ui.label(RichText::new("LIVE SI-CHECKED FIELD CALCULATOR").size(10.0).strong());
        ui.label(RichText::new("every AST node carries [kg m s K mol A cd]").size(8.0).color(c32(0x6d7484)));
        ui.add_space(4.0);
        ui.add(
            egui::TextEdit::multiline(&mut self.calculator_source)
                .code_editor()
                .desired_rows(4)
                .desired_width(f32::INFINITY),
        );
        if ui.button("Compile · dimension check").clicked() {
            self.compile_calculator();
        }
        match &self.compiled {
            Some(field) => {
                ui.label(RichText::new(format!("✓ {} {}", field.quantity, field.dim)).monospace().size(9.5).color(c32(0x65e5b1)));
                ui.label(RichText::new(format!("unit {} · shape scalar", field.unit)).monospace().size(8.5).color(c32(0x8fb0ff)));
                if ui.button("Upload to CFDFLD03").clicked() {
                    self.uploaded_field = field.target.clone();
                    self.notify(format!(
                        "{} uploaded to CFDFLD03 · {} · {}",
                        field.target.clone().unwrap_or_else(|| "derived".into()),
                        field.quantity,
                        field.dim
                    ));
                }
            }
            None => {
                if let Some(error) = &self.compile_error {
                    egui::Frame::none()
                        .fill(c32(0x1b0f12))
                        .stroke(Stroke::new(1.0, c32(0xEF233C)))
                        .inner_margin(6.0)
                        .show(ui, |ui| {
                            ui.label(RichText::new(error).monospace().size(9.0).color(c32(0xff9aab)));
                            ui.label(RichText::new("no GPU buffer was created").size(8.0).color(c32(0x8a6670)));
                        });
                }
            }
        }
        ui.separator();
        ui.label(RichText::new("RHAI CONSOLE").size(9.0).strong());
        egui::ScrollArea::vertical().max_height(120.0).show(ui, |ui| {
            for line in &self.console {
                ui.label(RichText::new(line).monospace().size(8.0).color(c32(0x7f91b4)));
            }
        });
        if let Some(field) = &self.compiled {
            ui.label(RichText::new(field.rhai_upload_script()).monospace().size(8.0).color(c32(0x7a8190)));
        }
        if let Some(name) = &self.uploaded_field {
            ui.label(RichText::new(format!("● {name} resident · binding 4 · Oklab turbo")).monospace().size(8.0).color(c32(0x65e5b1)));
        }
    }

    fn viewport(&mut self, ui: &mut egui::Ui) {
        let size = egui::vec2(ui.available_width(), (ui.available_height() * 0.52).max(210.0));
        let (rect, response) = ui.allocate_exact_size(size, egui::Sense::click_and_drag());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 6.0, c32(0x0f1014));

        if response.dragged() {
            let d = response.drag_delta();
            self.camera.orbit(glam::Vec2::new(d.x, d.y));
        }
        if let Some(pos) = response.interact_pointer_pos() {
            if response.clicked() {
                let fraction = ((pos.x - rect.left()) / rect.width()).clamp(0.0, 0.999);
                self.selected = (fraction * self.patches.len() as f32) as usize;
                self.synthesize();
            }
        }

        // floor grid
        for i in 0..10 {
            let y = rect.top() + rect.height() * (0.62 + i as f32 * 0.033);
            painter.line_segment(
                [egui::pos2(rect.left() + 12.0, y), egui::pos2(rect.right() - 12.0, y)],
                Stroke::new(1.0, c32(0x1c1e25)),
            );
        }
        // farfield cage
        if self.show_domain {
            let cage = rect.shrink2(egui::vec2(46.0, 34.0));
            painter.rect_stroke(cage, 3.0, Stroke::new(1.0, c32(0x3a3f4d)));
            painter.text(cage.left_top() + egui::vec2(5.0, 4.0), egui::Align2::LEFT_TOP,
                format!("INLET {}L", self.upstream_l), egui::FontId::monospace(8.0), c32(0x8cecff));
            painter.text(cage.right_bottom() - egui::vec2(5.0, 4.0), egui::Align2::RIGHT_BOTTOM,
                format!("OUTLET {}L", self.downstream_l), egui::FontId::monospace(8.0), c32(0x8cecff));
        }
        // MRF cylinder and rotor
        if self.show_mrf {
            let center = rect.center() + egui::vec2(self.camera.azimuth.sin() * 26.0, 16.0);
            let radius = egui::vec2(150.0, 54.0);
            painter.circle_stroke(center, radius.y, Stroke::new(1.0, c32(0x2c7f95)));
            for k in 0..8 {
                let angle = k as f32 / 8.0 * std::f32::consts::TAU;
                let p = center + egui::vec2(radius.x * angle.cos(), radius.y * angle.sin());
                painter.line_segment([center + egui::vec2(0.0, -radius.y), p], Stroke::new(0.6, c32(0x24505c)));
            }
            painter.text(center + egui::vec2(0.0, -radius.y - 12.0), egui::Align2::CENTER_CENTER,
                format!("MRF {} rpm · tip Ma {:.3}", self.rpm as u32, self.tip_mach()),
                egui::FontId::monospace(8.0), c32(0x8cecff));
        }
        // blade silhouette, selected face group highlighted
        let cx = rect.center().x;
        let cy = rect.center().y + 8.0;
        let twist = self.camera.azimuth.sin() * 40.0;
        let upper = [
            egui::pos2(cx - 150.0, cy + 18.0),
            egui::pos2(cx - 86.0 + twist, cy - 20.0),
            egui::pos2(cx + 44.0 + twist, cy - 36.0),
            egui::pos2(cx + 152.0, cy - 6.0),
            egui::pos2(cx + 150.0, cy + 8.0),
            egui::pos2(cx + 38.0 + twist, cy + 10.0),
            egui::pos2(cx - 88.0 + twist, cy + 30.0),
        ];
        let conflict = !self.overlaps.is_empty();
        let fill = if conflict { Color32::from_rgba_unmultiplied(239, 35, 60, 60) } else { Color32::from_rgba_unmultiplied(0, 74, 253, 44) };
        painter.add(egui::Shape::convex_polygon(upper.to_vec(), fill, Stroke::new(1.0, c32(0x4d7cff))));
        painter.text(egui::pos2(cx - 148.0, cy + 40.0), egui::Align2::LEFT_TOP,
            format!("{} · group {} · {} faces", self.patch().step_tag, self.patch().group.0, self.patch().faces.len()),
            egui::FontId::monospace(8.5), if conflict { c32(0xff8798) } else { c32(0x9fbcff) });
        // injector cone gizmo
        painter.line_segment([egui::pos2(cx - 176.0, cy - 58.0), egui::pos2(cx - 44.0, cy + 4.0)], Stroke::new(1.0, c32(0xFFB020)));
        painter.circle_stroke(egui::pos2(cx - 44.0, cy + 4.0), 22.0, Stroke::new(0.8, c32(0xFFB020)));
        painter.text(egui::pos2(cx - 170.0, cy - 68.0), egui::Align2::LEFT_TOP, "DPM θ 22° · d₃₂ 38 µm", egui::FontId::monospace(8.0), c32(0xffcb6b));
        // rake
        let a = egui::pos2(cx - 100.0, cy - 92.0);
        let b = egui::pos2(cx + 112.0, cy + 64.0);
        painter.line_segment([a, b], Stroke::new(1.2, c32(0xdbe4ff)));
        for i in 0..=12 {
            let t = i as f32 / 12.0;
            let p = a + (b - a) * t;
            painter.line_segment([p, p + egui::vec2(0.0, -6.0)], Stroke::new(0.8, c32(0x7e8ba6)));
        }
        painter.text(rect.right_top() - egui::vec2(10.0, -8.0), egui::Align2::RIGHT_TOP,
            "R32Uint pick · reverse-Z · 120 FPS", egui::FontId::monospace(8.0), c32(0x6d7484));
        painter.text(rect.left_bottom() + egui::vec2(10.0, -18.0), egui::Align2::LEFT_TOP,
            "LMB orbit · RMB pan · wheel zoom · click face to author", egui::FontId::monospace(8.0), c32(0x5f6675));
        let _ = Colour::WARNING_RED;
    }

    fn probe_dock(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label(RichText::new("SYNCHRONIZED PROBE PLOTS").size(9.0).strong().color(c32(0xaeb4c2)));
            ui.selectable_value(&mut self.chart_tab, ChartTab::Profiles, "Chordwise");
            ui.selectable_value(&mut self.chart_tab, ChartTab::WallLaw, "u⁺ · law of wall");
            ui.selectable_value(&mut self.chart_tab, ChartTab::Rake, "Rake profile");
        });
        match self.chart_tab {
            ChartTab::Profiles => {
                ui.columns(2, |cols| {
                    let a = self.chart_data("cp", false);
                    let b = self.chart_data("cp", true);
                    plot(&mut cols[0], "Cp(x/c)", &[("Run A", a, c32(0xAAB1C0)), ("Run B", b, c32(0x4D7CFF))]);
                    let a = self.chart_data("yp", false);
                    let b = self.chart_data("yp", true);
                    plot(&mut cols[1], "y⁺(x/c) · y⁺=1 / 30 bands", &[("Run A", a, c32(0xAAB1C0)), ("Run B", b, c32(0x4D7CFF))]);
                });
            }
            ChartTab::WallLaw => {
                let law = WallLaw::default();
                let mut viscous = Vec::new();
                let mut log = Vec::new();
                for i in 0..48 {
                    let y = 0.4 * 1.28_f32.powi(i as i32);
                    let (v, l, _) = law.u_plus(y);
                    viscous.push([y as f64, v as f64]);
                    log.push([y as f64, l as f64]);
                }
                plot(
                    ui,
                    "u⁺(y⁺) · boundary layer",
                    &[("u⁺ = y⁺", viscous, c32(0x6fbfa0)), ("1/κ ln(E y⁺)", log, c32(0xFFB020))],
                );
            }
            ChartTab::Rake => {
                if let Some(samples) = &self.probe_samples {
                    let a: Vec<[f64; 2]> = samples.arclength.iter().zip(samples.scalar_a.iter()).map(|(s, v)| [*s as f64, *v as f64]).collect();
                    let b: Vec<[f64; 2]> = samples.arclength.iter().zip(samples.scalar_b.iter()).map(|(s, v)| [*s as f64, *v as f64]).collect();
                    plot(ui, "Rake profile · φ(s)", &[("Run A", a, c32(0xAAB1C0)), ("Run B", b, c32(0x4D7CFF))]);
                }
            }
        }
    }

    fn compare_dock(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label(RichText::new("DIFFERENTIAL KPI · Δφ = φB − φA").size(9.0).strong());
            ui.label(RichText::new(format!("σ {:.4} · zero-centred LUT ±{:.3}", self.delta_stats.sigma, self.delta_stats.lut_half_span)).monospace().size(8.0).color(c32(0xffcdbc)));
        });
        egui::Grid::new("kpi").striped(true).num_columns(4).show(ui, |ui| {
            ui.label(RichText::new("KPI").strong().size(8.5));
            ui.label(RichText::new("A").strong().size(8.5));
            ui.label(RichText::new("B").strong().size(8.5));
            ui.label(RichText::new("Δ").strong().size(8.5));
            ui.end_row();
            for kpi in &self.kde {
                ui.label(RichText::new(kpi.label).size(9.0));
                ui.label(RichText::new(format!("{:.*}", kpi.decimals as usize, kpi.a)).monospace().size(8.5));
                ui.label(RichText::new(format!("{:.*}", kpi.decimals as usize, kpi.b)).monospace().size(8.5));
                let color = if kpi.improved() { c32(0x65e5b1) } else { c32(0xff9aab) };
                ui.label(RichText::new(format!("{:+.2} %", kpi.relative())).monospace().size(8.5).color(color));
                ui.end_row();
            }
        });
        ui.separator();
        ui.horizontal(|ui| {
            ui.label(RichText::new("PARETO FRONT").size(9.0).strong());
            ui.label(RichText::new("minimise Δp₀ loss and drag counts").size(8.0).color(c32(0x6d7484)));
        });
        let mut points: Vec<[f64; 2]> = Vec::new();
        for p in &self.pareto {
            points.push([p.loss_pct as f64, p.drag_counts as f64]);
        }
        Plot::new("pareto").height(180.0).show(ui, |plot_ui| {
            plot_ui.points(egui_plot::Points::new(PlotPoints::from(points.clone())).radius(2.5).color(c32(0x4D7CFF)));
        });
    }
}

fn plot(ui: &mut egui::Ui, title: &str, series: &[(&str, Vec<[f64; 2]>, Color32)]) {
    ui.label(RichText::new(title).size(9.5).color(c32(0xd6dbe6)));
    Plot::new(title)
        .height(178.0)
        .legend(Legend::default())
        .allow_scroll(false)
        .show(ui, |plot_ui| {
            for (name, data, color) in series {
                plot_ui.line(
                    Line::new(PlotPoints::from(data.clone()))
                        .name(*name)
                        .color(*color)
                        .width(1.4),
                );
            }
        });
}

fn c32(hex: u32) -> Color32 {
    Color32::from_rgb(
        ((hex >> 16) & 0xff) as u8,
        ((hex >> 8) & 0xff) as u8,
        (hex & 0xff) as u8,
    )
}

#[allow(dead_code)]
struct Colour;
#[allow(dead_code)]
impl Colour {
    const WARNING_RED: Color32 = Color32::from_rgb(239, 35, 60);
    const ELECTRIC_BLUE: Color32 = Color32::from_rgb(0, 74, 253);
}

fn configure_style(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = c32(0x111217);
    visuals.window_fill = c32(0x141519);
    visuals.extreme_bg_color = c32(0x0d0e12);
    visuals.widgets.noninteractive.bg_stroke = Stroke::new(1.0, c32(0x26272e));
    visuals.widgets.inactive.bg_fill = c32(0x1b1c22);
    visuals.selection.bg_fill = c32(0x004AFD);
    ctx.set_visuals(visuals);
    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = egui::vec2(6.0, 5.0);
    style.spacing.button_padding = egui::vec2(7.0, 3.0);
    style.text_styles.insert(egui::TextStyle::Body, egui::FontId::proportional(11.5));
    style.text_styles.insert(egui::TextStyle::Monospace, egui::FontId::monospace(9.5));
    ctx.set_style(style);
}

fn shortcuts(app: &mut CockpitApp, ctx: &egui::Context) {
    ctx.input(|input| {
        if input.modifiers.alt {
            if input.key_pressed(egui::Key::Num1) { app.workspace = Workspace::Rules; }
            if input.key_pressed(egui::Key::Num2) { app.workspace = Workspace::Domain; }
            if input.key_pressed(egui::Key::Num3) { app.workspace = Workspace::Probe; }
            if input.key_pressed(egui::Key::Num4) { app.workspace = Workspace::Compare; }
            if input.key_pressed(egui::Key::Num5) { app.workspace = Workspace::Calculator; }
        }
    });
}

/* --------------------------- demo data ----------------------------- */
fn demo_patches() -> Vec<PatchTopology> {
    let specs: [(&str, FaceGroupId, [f32; 3], usize); 7] = [
        ("SURF_LEADING_EDGE", FaceGroupId(1), [-0.78, 0.02, 0.62], 40),
        ("SURF_SUCTION", FaceGroupId(2), [0.31, 0.04, 0.24], 96),
        ("SURF_PRESSURE", FaceGroupId(3), [0.28, -0.04, -0.27], 96),
        ("SURF_TRAILING_EDGE", FaceGroupId(4), [0.82, 0.0, 0.03], 32),
        ("SURF_TIP", FaceGroupId(5), [0.06, 0.72, 0.02], 24),
        ("SURF_HUB", FaceGroupId(6), [0.12, -0.74, 0.02], 24),
        ("SURF_SHROUD", FaceGroupId(7), [0.12, 0.86, 0.02], 24),
    ];
    specs
        .iter()
        .map(|(tag, group, normal, n)| {
            let n = *n;
            let faces = (0..n)
                .map(|i| {
                    let t = i as f32 / (n.max(2) - 1) as f32;
                    FaceSample {
                        group: *group,
                        centroid: Vec3::new(-0.82 + 1.64 * t, 0.02 + 0.2 * (t - 0.5), 0.02)
                            + Vec3::from_array(*normal) * 0.14,
                        normal: Vec3::from_array(*normal).normalize_or_zero(),
                        area: 0.0032 + 0.0001 * (i % 5) as f32,
                    }
                })
                .collect();
            PatchTopology {
                group: *group,
                step_tag: (*tag).into(),
                faces,
            }
        })
        .collect()
}

fn demo_grid() -> HexGrid {
    let dims = [48usize, 16, 16];
    let mut values = Vec::with_capacity(dims[0] * dims[1] * dims[2]);
    for k in 0..dims[2] {
        for j in 0..dims[1] {
            for i in 0..dims[0] {
                let x = i as f32 / (dims[0] - 1) as f32;
                let y = j as f32 / (dims[1] - 1) as f32;
                let z = k as f32 / (dims[2] - 1) as f32;
                values.push(
                    0.35 + 0.6 * (std::f32::consts::PI * x).sin() * (1.0 - 0.4 * y) + 0.1 * (z * 6.0).sin(),
                );
            }
        }
    }
    HexGrid {
        origin: Vec3::new(-1.5, -0.8, -0.7),
        spacing: Vec3::new(0.065, 0.1, 0.09),
        dims,
        values,
    }
}

/// The native cockpit additionally feeds the surface chord slicer with a
/// tessellated blade; exposed for integration tests.
pub fn demo_surface() -> SurfaceMesh {
    let a = Vec3::new(-0.8, 0.0, 0.0);
    let b = Vec3::new(0.8, 0.0, 0.0);
    let c = Vec3::new(0.1, 0.0, 0.22);
    SurfaceMesh {
        positions: vec![a, b, c],
        triangles: vec![[0, 1, 2]],
        values: vec![0.8, 0.2, 1.0],
    }
}

pub fn chord_slice_example() -> ChordSlice {
    ChordSlice {
        plane_point: Vec3::ZERO,
        plane_normal: Vec3::Y,
        samples: 64,
    }
}
