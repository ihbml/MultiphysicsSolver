mod app;
mod ui;

use std::path::PathBuf;

use cad_core::DemoAssemblyKind;
use cad_plugin_api::{BackendPreference, ShadingMode};

use crate::app::{AppLaunchConfig, run_desktop_app, run_headless_screenshot};

fn print_help() {
    println!(
        "CAD Renderer (DX12 / Vulkan via wgpu + egui + bevy_ecs + Rhai Addons)
USAGE:
  cad-renderer [OPTIONS]

OPTIONS:
  --backend <vulkan|dx12|both|auto>    Select GPU backend preference (default: both)
  --scene <spindle|gearbox|manifold>   Select initial CAD demo assembly (default: spindle)
  --load <PATH>                        Load .stl, .obj, or .gltf/.glb CAD model
  --shading <shaded|smooth|gooch|matcap|zebra|xray|wireframe>
                                       Initial CAD shading mode (default: shaded)
  --section                            Enable Z section cut plane at startup
  --explode <FLOAT>                    Initial assembly explode factor (default: 0.0)
  --addons-dir <PATH>                  Path to Rhai runtime addons folder (default: ./addons)
  --headless-screenshot <OUT.png>      Render offscreen frame via Vulkan/DX12 and save PNG
  --width <U32>                        Headless render width (default: 1280)
  --height <U32>                       Headless render height (default: 720)
  --run-rhai-bolt-addon                Invoke Rhai bolt-circle generator addon before render
  -h, --help                           Show this help"
    );
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut cfg = AppLaunchConfig {
        backend_pref: BackendPreference::VulkanAndDx12,
        demo_scene: DemoAssemblyKind::SpindleHousing,
        load_file: None,
        shading_mode: ShadingMode::ShadedWithEdges,
        section_enabled: false,
        explode_factor: 0.0,
        addons_dir: resolve_default_addons_dir(),
    };

    let mut headless_out: Option<PathBuf> = None;
    let mut width = 1280_u32;
    let mut height = 720_u32;
    let mut trigger_bolt_addon = false;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-h" | "--help" => {
                print_help();
                return Ok(());
            }
            "--backend" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.backend_pref = match v.to_ascii_lowercase().as_str() {
                        "vulkan" | "vk" => BackendPreference::VulkanOnly,
                        "dx12" | "d3d12" => BackendPreference::Dx12Only,
                        "auto" => BackendPreference::AutoAll,
                        _ => BackendPreference::VulkanAndDx12,
                    };
                }
            }
            "--scene" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.demo_scene = match v.to_ascii_lowercase().as_str() {
                        "gearbox" => DemoAssemblyKind::PlanetaryGearbox,
                        "manifold" => DemoAssemblyKind::HydraulicManifold,
                        _ => DemoAssemblyKind::SpindleHousing,
                    };
                }
            }
            "--load" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.load_file = Some(PathBuf::from(v));
                }
            }
            "--shading" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.shading_mode = match v.to_ascii_lowercase().as_str() {
                        "smooth" => ShadingMode::ShadedSmooth,
                        "gooch" => ShadingMode::GoochTechnical,
                        "matcap" => ShadingMode::MatCapCurvature,
                        "zebra" => ShadingMode::ZebraStripes,
                        "xray" => ShadingMode::XRayGhost,
                        "wireframe" => ShadingMode::WireframeHidden,
                        _ => ShadingMode::ShadedWithEdges,
                    };
                }
            }
            "--section" => {
                cfg.section_enabled = true;
            }
            "--explode" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.explode_factor = v.parse().unwrap_or(0.0);
                }
            }
            "--addons-dir" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    cfg.addons_dir = PathBuf::from(v);
                }
            }
            "--headless-screenshot" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    headless_out = Some(PathBuf::from(v));
                }
            }
            "--width" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    width = v.parse().unwrap_or(1280);
                }
            }
            "--height" => {
                i += 1;
                if let Some(v) = args.get(i) {
                    height = v.parse().unwrap_or(720);
                }
            }
            "--run-rhai-bolt-addon" => {
                trigger_bolt_addon = true;
            }
            other => {
                eprintln!("Unknown argument: {other}");
                print_help();
                return Ok(());
            }
        }
        i += 1;
    }

    if let Some(out_path) = headless_out {
        run_headless_screenshot(&cfg, out_path, width, height, trigger_bolt_addon)?;
    } else {
        run_desktop_app(cfg)?;
    }

    Ok(())
}

fn resolve_default_addons_dir() -> PathBuf {
    let cwd_addons = PathBuf::from("addons");
    if cwd_addons.exists() {
        return cwd_addons;
    }
    if let Ok(exe) = std::env::current_exe()
        && let Some(dir) = exe.parent()
    {
        let sibling = dir.join("addons");
        if sibling.exists() {
            return sibling;
        }
    }
    cwd_addons
}
