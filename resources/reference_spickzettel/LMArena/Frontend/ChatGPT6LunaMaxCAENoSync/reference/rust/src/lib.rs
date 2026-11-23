//! Native modules for the FLUX / CFD desktop cockpit.

pub mod app;
pub mod buffers;
pub mod ipc;
pub mod mesh_quality;
pub mod physics;
pub mod schema;
pub mod viewport;

pub use ipc::{IpcBridge, IpcEvent, RevisionClock};
pub use physics::{PressureVariable, UnitValue, WallResolution};
pub use schema::{BlockInstance, ModuleRegistry, UiSchema};

/// App state shared by the native egui shell and the renderer.
pub struct Cockpit {
    pub registry: ModuleRegistry,
    pub blocks: Vec<BlockInstance>,
    pub revisions: RevisionClock,
    pub selected_patch: Option<String>,
    pub pressure_variable: PressureVariable,
    pub solver_running: bool,
    pub nan_cells: u64,
}

impl Default for Cockpit {
    fn default() -> Self {
        let registry = ModuleRegistry::reference_schema();
        let blocks = registry.reference_stack();
        Self {
            registry,
            blocks,
            revisions: RevisionClock::default(),
            selected_patch: Some("blade-wall".into()),
            pressure_variable: PressureVariable::AbsoluteP,
            solver_running: false,
            nan_cells: 0,
        }
    }
}