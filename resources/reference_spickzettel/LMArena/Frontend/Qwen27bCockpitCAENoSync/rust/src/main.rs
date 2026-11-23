//! HydroCore CFD cockpit — entry point.
//! egui/eframe shell; the wgpu viewport renders into egui's CentralPanel via
//! `SurfaceTexture`, the IPC client owns the backend child process.

mod ipc;
mod render;
mod ui;

use anyhow::Result;
use eframe::egui;

fn main() -> Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
    let backend_cmd = std::env::var("HYDROCORE_BACKEND").unwrap_or_else(|_| "foam-core-backend".into());

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1720.0, 1040.0]).with_title("HydroCore — Multiphysics CFD Cockpit"),
        ..Default::default()
    };

    eframe::run_native(
        "HydroCore Cockpit",
        options,
        Box::new(|cc| Ok(Box::new(ui::app::CockpitApp::new(cc, backend_cmd)))),
    )
    .map_err(|e| anyhow::anyhow!("eframe: {e}"))
}
