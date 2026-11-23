//! egui application — 5-workspace shell, nTop-style schema-driven block stack,
//! unit-aware expression fields, live convergence monitors, and the wgpu
//! viewport rendered into egui's CentralPanel via `screen_descriptor()`.

use std::collections::VecDeque;
use std::time::{Duration, Instant};

use eframe::egui;
use serde_json::json;
use wgpu::RenderPass;

use crate::ipc::client::{IpcClient, Telemetry};
use crate::render::pipeline::{self, Pipeline};

// ── unit-aware expression parser (7-elem SI dimension enforcement) ──────────
pub type Dim = [i8; 7];
pub const DIMLESS: Dim = [0, 0, 0, 0, 0, 0, 0];
const UNITS: &[(&str, Dim, f64, f64)] = &[
    ("m", [0, 1, 0, 0, 0, 0, 0], 1.0, 0.0),
    ("mm", [0, 1, 0, 0, 0, 0, 0], 1e-3, 0.0),
    ("cm", [0, 1, 0, 0, 0, 0, 0], 1e-2, 0.0),
    ("km", [0, 1, 0, 0, 0, 0, 0], 1e3, 0.0),
    ("kg", [1, 0, 0, 0, 0, 0, 0], 1.0, 0.0),
    ("s", [0, 0, 1, 0, 0, 0, 0], 1.0, 0.0),
    ("ms", [0, 0, 1, 0, 0, 0, 0], 1e-3, 0.0),
    ("K", [0, 0, 0, 1, 0, 0, 0], 1.0, 0.0),
    ("C", [0, 0, 0, 1, 0, 0, 0], 1.0, 273.15), // offset unit
    ("Pa", [1, -1, -2, 0, 0, 0, 0], 1.0, 0.0),
    ("kPa", [1, -1, -2, 0, 0, 0, 0], 1e3, 0.0),
    ("bar", [1, -1, -2, 0, 0, 0, 0], 1e5, 0.0),
    ("MPa", [1, -1, -2, 0, 0, 0, 0], 1e6, 0.0),
    ("m/s", [0, 1, -1, 0, 0, 0, 0], 1.0, 0.0),
    ("km/h", [0, 1, -1, 0, 0, 0, 0], 1000.0 / 3600.0, 0.0),
    ("rpm", [0, 0, -1, 0, 0, 0, 0], std::f64::consts::TAU / 60.0, 0.0),
    ("rad/s", [0, 0, -1, 0, 0, 0, 0], 1.0, 0.0),
    ("kg/m3", [1, -3, 0, 0, 0, 0, 0], 1.0, 0.0),
    ("Pa·s", [1, 0, -1, 0, 0, 0, 0], 1.0, 0.0),
    ("m2/s2", [0, 2, -2, 0, 0, 0, 0], 1.0, 0.0),
];

/// `12.5 mm` → 0.0125, `3.2 bar` → 3.2e5, `1500 rpm` → 157.08, `850 C` → 1123.15
/// Bare numbers are accepted only when `expected` is the dimless vector.
pub fn parse_expr(input: &str, expected: Dim) -> Option<(f64, Dim)> {
    let t = input.trim();
    let split: Option<(f64, String)> = t
        .split_once(char::is_whitespace)
        .map(|(a, b)| (a.parse::<f64>().ok()?, b.trim().to_string()))
        .or_else(|| Some((t.parse::<f64>().ok()?, String::new())));
    let (raw, unit) = split?;
    if unit.is_empty() {
        if expected != DIMLESS {
            return None; // bare value with dimensional expectation → reject
        }
        return Some((raw, DIMLESS));
    }
    let (dim, scale, offset) = UNITS.iter().find(|(n, ..)| *n == unit)?.clone();
    if *dim != expected {
        return None; // dimension-vector mismatch — RPC payload never dispatched
    }
    Some((raw * scale + offset, dim))
}

// ── block schema (ModuleRegistry.export_ui_schema mirror) ────────────────────
#[derive(Clone, Copy)]
pub struct Param {
    pub key: &'static str,
    pub label: &'static str,
    pub dim: Dim,
    pub default: &'static str,
    /// evaluated against the whole param map; returns visibility
    pub visible_if: Option<&'static str>,
}
#[derive(Clone, Copy)]
pub struct Block {
    pub title: &'static str,
    pub cat: &'static str,
    pub accent: egui::Color32,
    pub params: &'static [Param],
}

const ACC_BLUE: egui::Color32 = egui::Color32::from_rgb(0, 74, 253);
const ACC_LIME: egui::Color32 = egui::Color32::from_rgb(185, 245, 0);
const ACC_AMBER: egui::Color32 = egui::Color32::from_rgb(254, 196, 11);
const ACC_PINK: egui::Color32 = egui::Color32::from_rgb(252, 117, 180);
const ACC_VIOLET: egui::Color32 = egui::Color32::from_rgb(114, 59, 250);

const DIM_M: Dim = [0, 1, 0, 0, 0, 0, 0];
const DIM_S: Dim = [0, 0, 1, 0, 0, 0, 0];
const DIM_P: Dim = [1, -1, -2, 0, 0, 0, 0];
const DIM_V: Dim = [0, 1, -1, 0, 0, 0, 0];
const DIM_RHO: Dim = [1, -3, 0, 0, 0, 0, 0];
const DIM_MU: Dim = [1, 0, -1, 0, 0, 0, 0];

pub const BLOCKS: [([&'static Block],); 5] = [
    (&[
        Block {
            title: "Domain — Step Channel + Cylinder",
            cat: "GEOMETRY · CFDGEO01",
            accent: ACC_BLUE,
            params: &[
                Param { key: "length", label: "L streamwise", dim: DIM_M, default: "10 m", visible_if: None },
                Param { key: "height", label: "H top (far)", dim: DIM_M, default: "2 m", visible_if: None },
                Param { key: "half_width", label: "2·W span", dim: DIM_M, default: "1 m", visible_if: None },
                Param { key: "step_x", label: "x_step", dim: DIM_M, default: "4 m", visible_if: None },
                Param { key: "top_low", label: "h1 top (near)", dim: DIM_M, default: "1.4 m", visible_if: None },
                Param { key: "cyl_x", label: "x_cyl", dim: DIM_M, default: "6 m", visible_if: None },
                Param { key: "cyl_y", label: "y_cyl", dim: DIM_M, default: "0.5 m", visible_if: None },
                Param { key: "cyl_r", label: "r_cyl", dim: DIM_M, default: "0.5 m", visible_if: None },
            ],
        },
        Block {
            title: "Material — materials.list",
            cat: "PHYSICS · SI 7-dim",
            accent: ACC_AMBER,
            params: &[
                Param { key: "rho", label: "rho density", dim: DIM_RHO, default: "1.204 kg/m3", visible_if: None },
                Param { key: "mu", label: "mu viscosity", dim: DIM_MU, default: "1.82e-5 Pa·s", visible_if: None },
            ],
        },
    ][..],),
    (&[
        Block {
            title: "Mesh — Structured Hex",
            cat: "MESHER · CFDMSH02",
            accent: ACC_LIME,
            params: &[
                Param { key: "cells_x", label: "cells / x", dim: DIMLESS, default: "48", visible_if: None },
                Param { key: "cells_y", label: "cells / y", dim: DIMLESS, default: "10", visible_if: None },
                Param { key: "cells_z", label: "cells / z", dim: DIMLESS, default: "10", visible_if: None },
            ],
        },
        Block {
            title: "MeshErrorMap · Threshold Isolator",
            cat: "QUALITY · location: cell",
            accent: ACC_LIME,
            params: &[
                Param { key: "thr_skew", label: "skew threshold", dim: DIMLESS, default: "0.85", visible_if: None },
                Param { key: "thr_nonorth", label: "non-orth threshold (deg)", dim: DIMLESS, default: "70", visible_if: None },
            ],
        },
    ][..],),
    (&[
        Block {
            title: "Patch BC Assignment",
            cat: "BOUNDARY · FoamField.location",
            accent: ACC_PINK,
            params: &[
                Param { key: "vel_mag", label: "|U| inlet", dim: DIM_V, default: "8 m/s", visible_if: Some("bc_type == velocityInlet") },
                Param { key: "p_ref", label: "p / p_rgh", dim: DIM_P, default: "101325 Pa", visible_if: Some("bc_type == pressureOutlet") },
                Param { key: "T0", label: "T0 temperature", dim: [0, 0, 0, 1, 0, 0, 0], default: "20 C", visible_if: None },
                Param { key: "k0", label: "k0 turbulence KE", dim: [0, 2, -2, 0, 0, 0, 0], default: "0.09 m2/s2", visible_if: Some("bc_type == velocityInlet") },
                Param { key: "yplus", label: "y+ target (estimated)", dim: DIMLESS, default: "30", visible_if: Some("wall == yplus") },
                Param { key: "dy1", label: "dy1 fixed layer", dim: DIM_M, default: "5e-5 m", visible_if: Some("wall == finelayer") },
            ],
        },
    ][..],),
    (&[
        Block {
            title: "Solver — PISO / SMAC",
            cat: "SOLVE · solver.start",
            accent: ACC_VIOLET,
            params: &[
                Param { key: "max_iter", label: "max iterations", dim: DIMLESS, default: "1200", visible_if: None },
                Param { key: "dt", label: "dt time step", dim: DIM_S, default: "0.002 s", visible_if: None },
                Param { key: "cour", label: "Courant target", dim: DIMLESS, default: "0.8", visible_if: None },
                Param { key: "write_every", label: "write_every (iters)", dim: DIMLESS, default: "50", visible_if: None },
            ],
        },
    ][..],),
    (&[
        Block {
            title: "Results — FoamField / CFDFLD03",
            cat: "POST · results.load",
            accent: ACC_PINK,
            params: &[
                Param { key: "cmp_eps", label: "compare epsilon", dim: DIMLESS, default: "1e-6", visible_if: None },
            ],
        },
    ][..],),
];

const WS_NAMES: [&str; 5] = ["CAD & Domain", "Meshing & Quality", "Physics & BC Stack", "Solve & Live Monitors", "Post-Processing & Compare"];
const WS_ACCENT: [egui::Color32; 5] = [ACC_BLUE, ACC_LIME, ACC_AMBER, ACC_VIOLET, ACC_PINK];

// ── per-frame render state ───────────────────────────────────────────────────
pub struct FrameState {
    pub mvp: glam::Mat4,
    pub cam_rel: [f32; 6],
    pub mode: u32,       // 0 cad · 1 field/unlit
    pub quadrants: bool, // 4× metric view (WS2)
    pub thr: f32,
    pub time: f64,
    pub nan_active: bool,
    pub sel: f32,
    pub hover: f32,
}

impl FrameState {
    /// Phase-1: geometry into the MRT set. (buffer/VAO bookkeeping lives in
    /// `Pipeline`; the metric quadrants re-draw the same pos buffer with four
    /// per-metric color/value buffers — flat faceting, zero interpolation.)
    pub fn draw_phase1(&self, pass: &mut RenderPass, p: &Pipeline) {
        // pass.set_bind_group(0, cam_bg, &[]);
        // pass.set_bind_group(1, field_bg, &[]);
        if self.quadrants {
            for (i, rect) in [(0, 0), (1, 0), (0, 1), (1, 1)].iter() {
                pass.set_viewport(*rect.0 as f32, *rect.1 as f32, 0.5, 0.5, 0.0, 1.0);
                pass.set_pipeline(&p.mesh_pipe);
                // per-metric col/val VBOs bound here (see Pipeline::mesh_col/val)
                pass.draw(0, self.mesh_count(p));
            }
        } else if self.mode == 0 {
            pass.set_pipeline(&p.sol_pipe);
        } else {
            pass.set_pipeline(&p.field_pipe); // unlit + Surface-LIC
        }
        pass.draw(0, self.mesh_count(p));
    }

    /// Phase-2: MatCap+EDL compositor (cad) or raw unlit passthrough (field).
    pub fn draw_phase2(&self, pass: &mut RenderPass, p: &Pipeline) {
        // pass.set_bind_group(1, phase2_bg(albedo|normal|id), &[]);
        pass.set_pipeline(&p.screen_pipe);
        pass.draw(0, 4); // full-screen strip
    }

    fn mesh_count(&self, p: &Pipeline) -> u32 {
        p.mesh_count
    }
}

// ── the app ──────────────────────────────────────────────────────────────────
pub struct UiState {
    pub ws: usize,
    pub params: std::collections::HashMap<String, String>,
    pub sel: Option<u32>,
    pub hover: Option<u32>,
    pub geo_rev: u64,
    pub mesh_rev: u64,
    pub bcs: Vec<(u32, String, u64)>, // (patch, type, bound_revision)
    pub residuals: [VecDeque<f64>; 7],
    pub co: (VecDeque<f64>, VecDeque<f64>),
    pub forces: (VecDeque<f64>, VecDeque<f64>, VecDeque<f64>),
    pub n_nan: u32,
    pub solving: bool,
    pub timesteps: VecDeque<f64>,
    pub active_t: f64,
    pub console: VecDeque<String>,
    pub alert: bool,
}

impl Default for UiState {
    fn default() -> Self {
        let mut params = std::collections::HashMap::new();
        for blocks in BLOCKS.iter() {
            for b in blocks.0 {
                for p in b.params {
                    params.insert(p.key.to_string(), p.default.to_string());
                }
            }
        }
        Self {
            ws: 0,
            params,
            sel: None,
            hover: None,
            geo_rev: 0,
            mesh_rev: 0,
            bcs: Vec::new(),
            residuals: Default::default(),
            co: (Default::default(), Default::default()),
            forces: (Default::default(), Default::default(), Default::default()),
            n_nan: 0,
            solving: false,
            timesteps: VecDeque::new(),
            active_t: 0.0,
            console: VecDeque::with_capacity(512),
            alert: false,
        }
    }
}

pub struct CockpitApp {
    rt: tokio::runtime::Runtime,
    ipc: Option<IpcClient>,
    state: UiState,
    frame: FrameState,
    last: Instant,
}

impl CockpitApp {
    pub fn new(cc: &eframe::CreationContext<'_>, backend_cmd: String) -> Self {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .expect("tokio rt");
        let app = Self {
            rt,
            ipc: None,
            state: UiState::default(),
            frame: FrameState {
                mvp: glam::Mat4::IDENTITY,
                cam_rel: [0.0; 6],
                mode: 0,
                quadrants: false,
                thr: 0.85,
                time: 0.0,
                nan_active: false,
                sel: -1.0,
                hover: -1.0,
            },
            last: Instant::now(),
        };
        // boot: hello → schema.get → materials.list → geometry.import → mesh.generate
        rt.spawn(async move {
            let client = match IpcClient::spawn(&backend_cmd).await {
                Ok(c) => c,
                Err(e) => {
                    log::error!("backend boot failed: {e}");
                    return;
                }
            };
            let _ = client.call("schema.get", json!({})).await;
            let _ = client.call("materials.list", json!({})).await;
            let _ = client.call("geometry.import", json!({})).await;
            let _ = client.call("mesh.generate", json!({})).await;
            // hand the client back to the UI thread via the event bus
            // (in the full build: oneshot; here the runtime is owned by the app)
            std::mem::forget(client); // placeholder — see note in README
        });
        let _ = cc;
        app
    }

    /// Drain the telemetry bus without blocking the frame.
    fn pump(&mut self) {
        if let Some(ipc) = &self.ipc {
            while let Ok(ev) = ipc.rx.try_recv() {
                match ev {
                    Telemetry::Iter(f) => {
                        for (i, k) in ["Ux", "Uy", "Uz", "p", "h", "k", "omega"].iter().enumerate() {
                            if let Some(v) = f.residuals.get(*k) {
                                self.state.residuals[i].push_back(*v);
                                if self.state.residuals[i].len() > 900 {
                                    self.state.residuals[i].pop_front();
                                }
                            }
                        }
                        self.state.co.0.push_back(f.co_mean);
                        self.state.co.1.push_back(f.co_max);
                        self.state.forces.0.push_back(f.cd);
                        self.state.forces.1.push_back(f.cl);
                        self.state.forces.2.push_back(f.t_max);
                        for d in self.state.forces.iter().chain(self.state.co.iter()) {
                            if d.len() > 900 { d.pop_front(); }
                        }
                        self.state.n_nan = f.n_nan_cells;
                        if f.n_nan_cells > 0 {
                            self.state.alert = true;
                            self.push_console(format!("EMERGENCY n_nan_cells={} @ iter {}", f.n_nan_cells, f.iter));
                        }
                        self.frame.nan_active = f.n_nan_cells > 0;
                    }
                    Telemetry::TimestepWritten(t) => {
                        self.state.timesteps.push_back(t);
                        self.state.active_t = t;
                    }
                    Telemetry::Diag(line) => self.push_console(line),
                    Telemetry::Dying => self.push_console("[backend] process exited".into()),
                }
            }
            self.state.geo_rev = ipc.geometry_revision();
            self.state.mesh_rev = ipc.mesh_revision();
        }
    }

    fn push_console(&mut self, line: String) {
        self.state.console.push_back(line);
        if self.state.console.len() > 512 {
            self.state.console.pop_front();
        }
    }

    /// Simple `key == value` visible_if evaluator over the param map.
    fn visible(&self, expr: &str) -> bool {
        for part in expr.split(" && ") {
            let (k, v) = match part.trim().split_once("==") {
                Some(p) => p,
                None => return false,
            };
            let want = v.trim().trim_matches('\'');
            let got = self.state.params.get(k.trim()).map(String::as_str).unwrap_or("");
            if got != want {
                return false;
            }
        }
        true
    }

    fn top_bar(&mut self, ui: &mut egui::Ui) {
        egui::TopBottomPanel::top("workspaces").show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                ui.label(egui::RichText::new("HYDROCORE").strong().size(14.0));
                ui.label(egui::RichText::new("multiphysics CFD cockpit").weak().size(10.0));
                ui.separator();
                for (i, name) in WS_NAMES.iter().enumerate() {
                    let active = self.state.ws == i;
                    let (resp, vis) = ui.select_label(
                        egui::RectSense { ..Default::default() },
                        format!("{}. {name}", i + 1),
                    );
                    let _ = vis;
                    if resp.clicked() {
                        self.state.ws = i;
                        self.frame.quadrants = i == 1;
                    }
                    if active {
                        ui.painter().hline(ui.cursor().min.x..ui.cursor().max.x, ui.cursor().max.y, 2.0, WS_ACCENT[i]);
                    }
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    let nan = self.state.n_nan > 0;
                    let c = if nan { egui::Color32::from_rgb(239, 35, 60) } else { ACC_LIME };
                    ui.colored_label(c, if nan {
                        format!("⚠ n_nan={}", self.state.n_nan)
                    } else if self.state.solving {
                        "solving".into()
                    } else {
                        "backend · stdin/stdout".into()
                    });
                    ui.label(format!("geo#{} mesh#{}", self.state.geo_rev, self.state.mesh_rev));
                });
            });
            ui.style_mut().visuals.window_rounding = egui::Rounding::none;
        });
    }

    fn block_stack(&mut self, ui: &mut egui::Ui) {
        egui::SidePanel::left("blocks")
            .resizable(true)
            .default_width(300.0)
            .show(ui, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    for b in BLOCKS[self.state.ws].0 {
                        ui.add(
                            egui::Frame::new()
                                .fill(egui::Color32::from_rgb(24, 24, 28))
                                .stroke(egui::Stroke::new(1.0, egui::Color32::from_rgb(38, 39, 46)))
                                .rounding(egui::Rounding::same(6.0)),
                        ).inner(|ui| {
                            ui.horizontal(|ui| {
                                ui.painter().rect_stroke(
                                    ui.allocate_space(egui::vec2(3.0, 20.0)).rect,
                                    0.0,
                                    egui::Stroke::new(3.0, b.accent),
                                );
                                ui.label(egui::RichText::new(b.title).strong().size(12.5));
                                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                    ui.colored_label(b.accent, egui::RichText::new(b.cat).monospace().size(8.5));
                                });
                            });
                            for p in b.params {
                                if let Some(expr) = p.visible_if {
                                    if !self.visible(expr) {
                                        continue;
                                    }
                                }
                                self.expr_field(ui, p);
                            }
                        });
                        ui.add_space(10.0);
                    }
                });
            });
    }

    /// Unit-aware expression field: edits a string, shows the SI resolution
    /// + expected 7-dim vector, blocks commit on dimension mismatch.
    fn expr_field(&mut self, ui: &mut egui::Ui, p: &Param) {
        ui.horizontal(|ui| {
            ui.label(egui::RichText::new(p.label).size(11.0).weak());
            let mut text = self.state.params.get(p.key).cloned().unwrap_or_default();
            let resp = ui
                .add(egui::TextEdit::singleline(&mut text).desired_width(f32::INFINITY).monospace().size(11.5))
                .response;
            let parsed = parse_expr(&text, p.dim);
            if resp.changed() {
                if parsed.is_some() {
                    self.state.params.insert(p.key.to_string(), text);
                }
            }
            match &parsed {
                Some((si, _)) => {
                    ui.colored_label(ACC_LIME, egui::RichText::new(format!("= {si:.6}")).monospace().size(9.5));
                }
                None if !text.trim().is_empty() => {
                    ui.colored_label(egui::Color32::from_rgb(239, 35, 60), egui::RichText::new("dim mismatch").monospace().size(9.5));
                }
                None => {}
            }
        });
    }

    fn monitors(&mut self, ui: &mut egui::Ui) {
        egui::BottomPanel::bottom("monitors")
            .resizable(true)
            .default_height(178.0)
            .show_animated(self.state.ws == 3 || self.state.ws == 4, ui, |ui| {
                ui.horizontal(|ui| {
                    let keys = ["Ux", "Uy", "Uz", "p", "h", "k", "omega"];
                    let colors = [
                        ACC_BLUE, ACC_LIME, ACC_VIOLET, ACC_AMBER, ACC_PINK,
                        egui::Color32::from_rgb(239, 35, 60),
                        egui::Color32::from_rgb(79, 209, 197),
                    ];
                    for (i, k) in keys.iter().enumerate() {
                        let arr: Vec<egui::PlotPoint> = self.state.residuals[i]
                            .iter()
                            .enumerate()
                            .map(|(x, &y)| egui::PlotPoint::new(x as f64, y))
                            .collect();
                        ui.add(
                            egui::Plot::new(format!("res_{k}"))
                                .height(140.0)
                                .include_y(0.0)
                                .logarithmic(true)
                                .legend(egui::PlotLegend::default().default_shown(false))
                                .new_series(egui::Series::lines(arr).width(1.2).color(colors[i])),
                        );
                    }
                });
            });
    }

    fn viewport(&mut self, ctx: &egui::Context, ui: &mut egui::Ui) {
        egui::CentralPanel::default().show(ui, |ui| {
            let area = ui.available_rect_before_wrap();
            ui.put(egui::Label::new(egui::RichText::new(format!(
                "phase1 → [albedo|nrm|id] MRT · Depth32F rev-Z · f64→f32 hi/lo cam-rel · pick: id-texel readback  ·  {}×{}",
                area.width(), area.height(),
            ))).size(9.5).weak());
            if let Some(screen) = ctx.screen_descriptor() {
                if let Ok(tex) = screen.acquire_texture(&self.rt.device()) {
                    // pipeline.render(&tex.texture_view(), &self.frame)
                    tex.present();
                }
            }
            let _ = area;
        });
    }
}

// helper adapter so the snippet above reads cleanly
trait RtDevice {
    fn device(&self) -> &wgpu::Device;
}
impl RtDevice for tokio::runtime::Runtime {
    fn device(&self) -> &wgpu::Device {
        // in the full build the wgpu Device lives in CockpitApp, not the runtime
        panic!("device owned by CockpitApp — see full source")
    }
}

impl eframe::App for CockpitApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let _dt = self.last.elapsed();
        self.last = Instant::now();
        self.frame.time = self.frame.time + _dt.as_secs_f64();
        self.pump();

        // NaN emergency → red frame
        if self.state.alert {
            let r = ctx.screen_rect();
            ctx.layer_painter(egui::LayerId::new(egui::Order::Foreground, Default::default()))
                .rect_stroke(r.shrink2(2.0), 0.0, egui::Stroke::new(4.0, egui::Color32::from_rgb(239, 35, 60)));
        }

        self.top_bar(ctx);
        self.block_stack(ctx);
        self.monitors(ctx);
        self.viewport(ctx, ctx);

        // solver control (WS3): keyboard shortcuts
        if ctx.input(|i| i.key_pressed(egui::Key::S)) {
            if self.ipc.is_some() && !self.state.solving {
                let ipc = self.ipc.as_ref().unwrap();
                let out = self.rt.block_on(ipc.call("solver.start", json!({
                    "max_iter": "1200", "dt": "0.002 s", "cour_target": "0.8"
                })));
                self.state.solving = out.is_ok();
            }
        }
        if ctx.input(|i| i.key_pressed(egui::Key::Q)) {
            if let Some(ipc) = self.ipc.as_ref() {
                let _ = self.rt.block_on(ipc.call("solver.stop_graceful", json!({})));
                self.state.solving = false;
                self.state.alert = false;
            }
        }
        let _ = Duration::from_secs(0);
    }
}
