//! AEROFORGE CFD Cockpit — Stage 2 reference library.
//!
//! Module map
//! ----------
//! * [`spatial_rule`] — smart spatial boundary-rule synthesizer + overlap audit.
//! * [`si_units`]     — 7-vector SI dimension checker for the field calculator.
//! * [`probe`]        — trilinear / barycentric line, rake and chord sampling.
//! * [`solver_data`]  — zero-copy CFDGEO01 / CFDMSH02 / CFDFLD03 readers.
//! * [`viewport`]     — wgpu reverse-Z viewport, R32Uint picking, Oklab LUTs,
//!                      physics-actor gizmos and A/B wipe compositing.
//! * [`compare`]      — GPU delta field, KPI deltas, Pareto front.
//! * [`app`]          — egui workbench chrome, panels and egui_plot chart dock.

pub mod app;
pub mod compare;
pub mod probe;
pub mod si_units;
pub mod solver_data;
pub mod spatial_rule;
pub mod viewport;

pub use app::{CockpitApp, Workspace};

/// Canonical colour tokens shared by the native and web cockpit.
pub struct Theme;
impl Theme {
    pub const OBSIDIAN: u32 = 0x111217;
    pub const CARBON: u32 = 0x18181C;
    pub const ELECTRIC_BLUE: u32 = 0x004AFD;
    pub const WARNING_RED: u32 = 0xEF233C;
    pub const ACTOR_AMBER: u32 = 0xFFB020;
    pub const COOLWARM: u32 = 0xB40426;
}

/// Fixed name of the derived-field GPU binding used by the calculator upload.
pub const DERIVED_FIELD_BINDING: u32 = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Units {
    Si,
    User,
}

#[derive(Debug, Clone)]
pub struct StudyMetadata {
    pub project: String,
    pub study: String,
    pub runs: [&'static str; 2],
    pub units: Units,
}

impl Default for StudyMetadata {
    fn default() -> Self {
        Self {
            project: "Rotor–Stator Stage 2".into(),
            study: "design-study-04".into(),
            runs: ["Baseline", "Adjoint-optimized #37"],
            units: Units::Si,
        }
    }
}
