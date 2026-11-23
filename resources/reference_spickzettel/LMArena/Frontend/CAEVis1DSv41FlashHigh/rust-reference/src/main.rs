//! Native entry point. The cockpit embeds wgpu directly for the reverse-Z
//! viewport and presents the egui workbench on top of the same device.

use aeroforge_cockpit::{app::CockpitApp, StudyMetadata};

fn main() -> eframe::Result<()> {
    env_logger::init();
    log::info!("AEROFORGE cockpit stage 2 · {:?}", StudyMetadata::default());

    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([1600.0, 980.0])
            .with_min_inner_size([1180.0, 720.0])
            .with_title("AEROFORGE — CFD Cockpit · Stage 2"),
        renderer: eframe::Renderer::Wgpu,
        vsync: true,
        ..Default::default()
    };

    eframe::run_native(
        "aeroforge-cockpit",
        native_options,
        Box::new(|cc| Ok(Box::new(CockpitApp::new(cc)))),
    )
}
