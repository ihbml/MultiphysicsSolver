use std::path::PathBuf;
use std::sync::Arc;
use std::time::Instant;

use cad_addons::{RhaiAddonHost, register_builtin_rust_addons};
use cad_core::{CreaseConfig, DemoAssemblyKind, build_demo_assembly, load_cad_file};
use cad_plugin_api::{
    AdapterDetails, BackendPreference, CadPluginHost, CameraState, ExplodeState, RenderSettings,
    SceneCommand, SceneCommandQueue, SectionAxis, SectionPlaneState, ShadingMode, ViewportStats,
    spawn_assembly_parts, spawn_imported_assembly,
};
use cad_renderer::{CadRenderer, GpuContext};
use egui_wgpu::ScreenDescriptor;
use winit::application::ApplicationHandler;
use winit::dpi::LogicalSize;
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::window::{Window, WindowId};

use crate::ui::{UiState, apply_cad_theme, draw_cad_ui};

pub struct AppLaunchConfig {
    pub backend_pref: BackendPreference,
    pub demo_scene: DemoAssemblyKind,
    pub load_file: Option<PathBuf>,
    pub shading_mode: ShadingMode,
    pub section_enabled: bool,
    pub explode_factor: f32,
    pub addons_dir: PathBuf,
}

pub fn build_initialized_host(cfg: &AppLaunchConfig) -> CadPluginHost {
    let mut host = CadPluginHost::new();
    register_builtin_rust_addons(&mut host);
    host.register_addon(Box::new(RhaiAddonHost::new(&cfg.addons_dir)));

    if let Some(path) = &cfg.load_file {
        match load_cad_file(path, CreaseConfig::default()) {
            Ok(asm) => {
                spawn_imported_assembly(&mut host.world, asm);
            }
            Err(err) => {
                eprintln!("Warning: could not load {:?}: {err}. Falling back to demo assembly.", path);
                let specs = build_demo_assembly(cfg.demo_scene);
                spawn_assembly_parts(&mut host.world, cfg.demo_scene.label(), specs);
            }
        }
    } else {
        let specs = build_demo_assembly(cfg.demo_scene);
        spawn_assembly_parts(&mut host.world, cfg.demo_scene.label(), specs);
    }

    host.world.resource_mut::<RenderSettings>().shading_mode = cfg.shading_mode;
    host.world.resource_mut::<ExplodeState>().factor = cfg.explode_factor;
    if cfg.section_enabled {
        let mut sec = host.world.resource_mut::<SectionPlaneState>();
        sec.enabled = true;
        sec.axis = SectionAxis::Z;
        sec.offset = 0.0;
    }

    host.startup();
    // Run one initial tick so scene_aabb is computed, then frame camera cleanly
    host.tick(1.0 / 60.0);
    let aabb = host.world.resource::<ViewportStats>().scene_aabb;
    if aabb.is_valid() {
        host.world.resource_mut::<CameraState>().orbit.frame_aabb(&aabb);
    }
    host
}

pub fn run_headless_screenshot(
    cfg: &AppLaunchConfig,
    out_path: PathBuf,
    width: u32,
    height: u32,
    trigger_bolt_addon: bool,
) -> Result<(), Box<dyn std::error::Error>> {
    let gpu = GpuContext::new_headless(cfg.backend_pref)?;
    let mut host = build_initialized_host(cfg);
    host.world.insert_resource(gpu.details.clone());

    if trigger_bolt_addon {
        // Simulate clicking the Rhai bolt-circle addon spawn button in ECS!
        {
            let mut panel_reg = host.world.resource_mut::<cad_plugin_api::AddonPanelRegistry>();
            panel_reg
                .clicked_buttons
                .push(cad_plugin_api::AddonUiEvent {
                    addon_id: "rhai.bolt_circle".into(),
                    widget_id: "spawn_flange_btn".into(),
                });
        }
        host.tick(1.0 / 60.0);
        host.tick(1.0 / 60.0);
        let aabb = host.world.resource::<ViewportStats>().scene_aabb;
        if aabb.is_valid() {
            host.world.resource_mut::<CameraState>().orbit.frame_aabb(&aabb);
        }
    }

    host.tick(1.0 / 60.0);
    let mut renderer = CadRenderer::new(&gpu, width, height);
    renderer.render_world(&gpu.device, &gpu.queue, &mut host.world, true);

    // Test GPU picking right at viewport center!
    let center_pick = renderer.read_pick_id(&gpu.device, &gpu.queue, width / 2, height / 2);
    if let Some(parent) = out_path.parent()
        && !parent.as_os_str().is_empty()
    {
        std::fs::create_dir_all(parent)?;
    }
    renderer.save_png(&gpu.device, &gpu.queue, &out_path)?;

    let stats = host.world.resource::<ViewportStats>();
    let details = host.world.resource::<AdapterDetails>();
    println!(
        "Rendered {}x{} CAD frame -> {:?}\n  Adapter: {} ({}, {}x MSAA, Reverse-Z)\n  Parts: {}/{}, Triangles: {}, Crease Edges: {}, Draw Calls: {}, Center PickID: {}",
        width,
        height,
        out_path,
        details.adapter_name,
        details.backend_name,
        details.msaa_samples,
        stats.visible_parts,
        stats.total_parts,
        stats.total_triangles,
        stats.total_edges,
        stats.draw_calls,
        center_pick
    );

    Ok(())
}

pub fn run_desktop_app(cfg: AppLaunchConfig) -> Result<(), Box<dyn std::error::Error>> {
    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);
    let mut app = DesktopApp::new(cfg);
    event_loop.run_app(&mut app)?;
    Ok(())
}

struct WindowRuntime {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    surface_config: wgpu::SurfaceConfiguration,
    gpu: GpuContext,
    cad_renderer: CadRenderer,
    egui_ctx: egui::Context,
    egui_winit: egui_winit::State,
    egui_renderer: egui_wgpu::Renderer,
    viewport_tex_id: egui::TextureId,
    last_frame: Instant,
    fps_accum_time: f32,
    fps_accum_frames: u32,
}

struct DesktopApp {
    cfg: AppLaunchConfig,
    host: CadPluginHost,
    ui_state: UiState,
    runtime: Option<WindowRuntime>,
}

impl DesktopApp {
    fn new(cfg: AppLaunchConfig) -> Self {
        let host = build_initialized_host(&cfg);
        let mut ui_state = UiState::default();
        ui_state.active_demo = cfg.demo_scene;
        Self {
            cfg,
            host,
            ui_state,
            runtime: None,
        }
    }
}

impl ApplicationHandler for DesktopApp {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.runtime.is_some() {
            return;
        }

        let attrs = Window::default_attributes()
            .with_title("CAD Renderer — DX12 / Vulkan • wgpu + egui + bevy_ecs")
            .with_inner_size(LogicalSize::new(1480.0, 920.0));
        let window = Arc::new(
            event_loop
                .create_window(attrs)
                .expect("Failed to create window"),
        );

        let backends = GpuContext::backends_for_preference(self.cfg.backend_pref);
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends,
            flags: wgpu::InstanceFlags::from_env_or_default(),
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        });

        let surface = instance
            .create_surface(window.clone())
            .expect("Failed to create wgpu surface");

        let gpu = GpuContext::from_instance_and_surface(
            instance,
            Some(&surface),
            self.cfg.backend_pref,
        )
        .expect("Failed to initialize Vulkan/DX12 GPU context");

        self.host.world.insert_resource(gpu.details.clone());

        let inner = window.inner_size();
        let caps = surface.get_capabilities(&gpu.adapter);
        let surface_format = caps
            .formats
            .iter()
            .copied()
            .find(|f| !f.is_srgb())
            .unwrap_or(caps.formats[0]);

        let mut surface_config = surface
            .get_default_config(&gpu.adapter, inner.width.max(1), inner.height.max(1))
            .expect("Surface incompatible with adapter");
        surface_config.format = surface_format;
        surface_config.present_mode = wgpu::PresentMode::AutoVsync;
        surface.configure(&gpu.device, &surface_config);

        let cad_renderer = CadRenderer::new(&gpu, 1024, 720);

        let egui_ctx = egui::Context::default();
        apply_cad_theme(&egui_ctx);

        let egui_winit = egui_winit::State::new(
            egui_ctx.clone(),
            egui::ViewportId::ROOT,
            window.as_ref(),
            Some(window.scale_factor() as f32),
            None,
            None,
        );

        let mut egui_renderer = egui_wgpu::Renderer::new(
            &gpu.device,
            surface_format,
            egui_wgpu::RendererOptions::default(),
        );
        let viewport_tex_id = egui_renderer.register_native_texture(
            &gpu.device,
            cad_renderer.color_view(),
            wgpu::FilterMode::Linear,
        );

        self.runtime = Some(WindowRuntime {
            window,
            surface,
            surface_config,
            gpu,
            cad_renderer,
            egui_ctx,
            egui_winit,
            egui_renderer,
            viewport_tex_id,
            last_frame: Instant::now(),
            fps_accum_time: 0.0,
            fps_accum_frames: 0,
        });
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        let Some(rt) = &mut self.runtime else {
            return;
        };

        let resp = rt.egui_winit.on_window_event(&rt.window, &event);
        if resp.repaint {
            rt.window.request_redraw();
        }

        match event {
            WindowEvent::CloseRequested => {
                event_loop.exit();
            }
            WindowEvent::Resized(new_size) => {
                if new_size.width > 0 && new_size.height > 0 {
                    rt.surface_config.width = new_size.width;
                    rt.surface_config.height = new_size.height;
                    rt.surface.configure(&rt.gpu.device, &rt.surface_config);
                }
            }
            WindowEvent::RedrawRequested => {
                let now = Instant::now();
                let dt = (now - rt.last_frame).as_secs_f32().clamp(0.0001, 0.25);
                rt.last_frame = now;

                rt.fps_accum_time += dt;
                rt.fps_accum_frames += 1;
                if rt.fps_accum_time >= 0.35 {
                    let fps = rt.fps_accum_frames as f32 / rt.fps_accum_time;
                    let ms = 1000.0 / fps.max(0.001);
                    let mut stats = self.host.world.resource_mut::<ViewportStats>();
                    stats.fps = fps;
                    stats.frame_time_ms = ms;
                    rt.fps_accum_time = 0.0;
                    rt.fps_accum_frames = 0;
                }

                // 1. Tick ECS schedules & addons
                self.host.tick(dt);

                // 2. Resize offscreen CAD viewport if CentralPanel size changed
                let [vp_w, vp_h] = self.ui_state.viewport_rect_px;
                if rt.cad_renderer.ensure_size(&rt.gpu.device, vp_w, vp_h) {
                    rt.egui_renderer.update_egui_texture_from_wgpu_texture(
                        &rt.gpu.device,
                        rt.cad_renderer.color_view(),
                        wgpu::FilterMode::Linear,
                        rt.viewport_tex_id,
                    );
                }

                // 3. Render 3D CAD Scene (+ GPU Picking pass when hovering or clicking)
                let need_pick = self.ui_state.pending_pick_pixel.is_some()
                    || self.ui_state.pending_hover_pixel.is_some();
                rt.cad_renderer.render_world(
                    &rt.gpu.device,
                    &rt.gpu.queue,
                    &mut self.host.world,
                    need_pick,
                );

                // 4. Resolve GPU ID Picking for Click & Hover
                if let Some([px, py]) = self.ui_state.pending_pick_pixel.take() {
                    let pid = rt.cad_renderer.read_pick_id(&rt.gpu.device, &rt.gpu.queue, px, py);
                    let cmd = if pid > 0 {
                        SceneCommand::SelectPickId(Some(pid))
                    } else {
                        SceneCommand::SelectPickId(None)
                    };
                    self.host.world.resource_mut::<SceneCommandQueue>().push(cmd);
                }
                if let Some([hx, hy]) = self.ui_state.pending_hover_pixel {
                    let pid = rt.cad_renderer.read_pick_id(&rt.gpu.device, &rt.gpu.queue, hx, hy);
                    let cmd = if pid > 0 {
                        SceneCommand::SetHoverPickId(Some(pid))
                    } else {
                        SceneCommand::SetHoverPickId(None)
                    };
                    self.host.world.resource_mut::<SceneCommandQueue>().push(cmd);
                } else {
                    self.host
                        .world
                        .resource_mut::<SceneCommandQueue>()
                        .push(SceneCommand::SetHoverPickId(None));
                }

                // 5. Run egui UI
                let raw_input = rt.egui_winit.take_egui_input(&rt.window);
                let pixels_per_point = rt.egui_ctx.pixels_per_point();
                let full_output = rt.egui_ctx.run_ui(raw_input, |ui| {
                    draw_cad_ui(
                        ui,
                        &mut self.host,
                        &mut self.ui_state,
                        rt.viewport_tex_id,
                        pixels_per_point,
                    );
                });

                rt.egui_winit
                    .handle_platform_output(&rt.window, full_output.platform_output);

                let paint_jobs = rt
                    .egui_ctx
                    .tessellate(full_output.shapes, full_output.pixels_per_point);

                let screen_desc = ScreenDescriptor {
                    size_in_pixels: [rt.surface_config.width, rt.surface_config.height],
                    pixels_per_point: full_output.pixels_per_point,
                };

                // 6. Acquire swapchain & composite egui
                let frame_tex = match rt.surface.get_current_texture() {
                    wgpu::CurrentSurfaceTexture::Success(tex) => tex,
                    wgpu::CurrentSurfaceTexture::Suboptimal(tex) => {
                        rt.surface.configure(&rt.gpu.device, &rt.surface_config);
                        tex
                    }
                    wgpu::CurrentSurfaceTexture::Outdated
                    | wgpu::CurrentSurfaceTexture::Lost => {
                        rt.surface.configure(&rt.gpu.device, &rt.surface_config);
                        return;
                    }
                    wgpu::CurrentSurfaceTexture::Timeout
                    | wgpu::CurrentSurfaceTexture::Occluded
                    | wgpu::CurrentSurfaceTexture::Validation => return,
                };

                let frame_view = frame_tex
                    .texture
                    .create_view(&wgpu::TextureViewDescriptor::default());

                let mut encoder =
                    rt.gpu
                        .device
                        .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                            label: Some("egui_composite_encoder"),
                        });

                for (id, delta) in &full_output.textures_delta.set {
                    rt.egui_renderer
                        .update_texture(&rt.gpu.device, &rt.gpu.queue, *id, delta);
                }

                let extra_cmds = rt.egui_renderer.update_buffers(
                    &rt.gpu.device,
                    &rt.gpu.queue,
                    &mut encoder,
                    &paint_jobs,
                    &screen_desc,
                );

                {
                    let rpass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                        label: Some("egui_render_pass"),
                        color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                            view: &frame_view,
                            depth_slice: None,
                            resolve_target: None,
                            ops: wgpu::Operations {
                                load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                                store: wgpu::StoreOp::Store,
                            },
                        })],
                        depth_stencil_attachment: None,
                        timestamp_writes: None,
                        occlusion_query_set: None,
                        multiview_mask: None,
                    });
                    rt.egui_renderer.render(
                        &mut rpass.forget_lifetime(),
                        &paint_jobs,
                        &screen_desc,
                    );
                }

                rt.gpu.queue.submit(
                    extra_cmds
                        .into_iter()
                        .chain(std::iter::once(encoder.finish())),
                );
                frame_tex.present();

                for id in &full_output.textures_delta.free {
                    rt.egui_renderer.free_texture(id);
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(rt) = &self.runtime {
            rt.window.request_redraw();
        }
    }
}
