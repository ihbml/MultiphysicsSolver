//! Two-phase wgpu rendering pipeline.
//!
//! Phase 1 — geometry → MRT [albedo | normal | face_group_id] + Depth32Float
//! Phase 2 — full-screen compositor: procedural MatCap, screen-space EDL /
//!           cavity, selection & hover highlight; unlit passthrough (Mode 2)
//!
//! Invariants:
//!  * reverse-Z infinite projection (depth 1.0 @ near → 0.5 @ ∞, GreaterEqual)
//!  * camera-relative f64 → f32 translation via hi/lo split center (`uCamRel`)
//!  * flat-faceted `location:"cell"` quality metrics (zero interpolation)
//!  * GPU ID-buffer ray-pick: copy 1×1 id texel → staging → map_async

use std::sync::mpsc;

use wgpu::*;

use super::shaders::ShaderLib;
use crate::ui::app::FrameState;

/// Reverse-Z perspective, infinite far plane.
/// clip_z = near * w  →  ndc_z = near/|z_view| : 1.0 at near, 0.5 at ∞.
pub fn perspective_rev_z(fov_y: f32, aspect: f32, near: f32) -> glam::Mat4 {
    let f = 1.0 / near;
    glam::Mat4::from_cols(
        glam::Vec4::new(f / aspect, 0.0, 0.0, 0.0),
        glam::Vec4::new(0.0, f, 0.0, 0.0),
        glam::Vec4::new(0.0, 0.0, 0.0, -1.0),
        glam::Vec4::new(0.0, 0.0, near, 0.0),
    )
}

/// Depth state shared by every phase-1 draw: reverse-Z, no writes in the
/// compositor. 1 µm on a 10 m domain keeps ≈17 effective bits of separation.
fn depth_state() -> DepthStencilState {
    DepthStencilState {
        format: TextureFormat::Depth32Float,
        depth_write_enabled: true,
        compare: CompareFunction::GreaterEqual,
        bias: DepthBias::default(),
        stencil: DepthStencilStateDefault::STENCIL,
        clip_depth: false,
    }
}

/// MRT target descriptor for phase 1.
fn phase1_targets() -> [ColorTargetState; 3] {
    [
        ColorTargetState { format: TextureFormat::Rgba8Unorm, ..Default::default() },
        ColorTargetState { format: TextureFormat::Rgba8Unorm, ..Default::default() },
        ColorTargetState { format: TextureFormat::Rgba8Unorm, ..Default::default() },
    ]
}

fn make_texture(device: &Device, size: Extent3d, format: TextureFormat, usage: TextureUsages) -> Texture {
    device.create_texture(&TextureDescriptor {
        label: None,
        size,
        mip_level_count: 1,
        sample_count: 1,
        dimension: TextureDimension::D2,
        format,
        usage,
        view_formats: &[],
    })
}

pub struct Pipeline {
    pub device: Device,
    pub queue: Queue,
    pub config: SurfaceConfiguration,
    pub albedo: Texture,
    pub normal: Texture,
    pub id: Texture,
    pub depth: Texture,
    pub lut: Texture,

    sol_pipe: RenderPipeline,
    field_pipe: RenderPipeline,
    line_pipe: RenderPipeline,
    mesh_pipe: RenderPipeline,
    slice_pipe: RenderPipeline,
    screen_pipe: RenderPipeline,

    pub solid_pos: Option<Buffer>,
    pub solid_nrm: Option<Buffer>,
    pub solid_idx: Option<Buffer>,
    pub solid_count: u32,
    /// pos + col[4 metrics] + val[4 metrics] — per-metric VBOs let the
    /// 4-quadrant view draw each metric with zero vertex interpolation.
    pub mesh_pos: Option<Buffer>,
    pub mesh_col: [Option<Buffer>; 4],
    pub mesh_val: [Option<Buffer>; 4],
    pub mesh_count: u32,
}

impl Pipeline {
    pub fn new(
        device: &Device,
        queue: &Queue,
        surface: &Surface<'_>,
        size: Extent3d,
    ) -> Self {
        let config = SurfaceConfiguration {
            label: Some("hydrocore-viewport".into()),
            usage: TextureUsages::RENDER_ATTACHMENT,
            format: surface.get_capabilities(&surface.get_supported_formats()[0]).formats[0],
            width: size.width,
            height: size.height,
            present_mode: PresentMode::VSync,
            view_formats: vec![],
            alpha_mode: CompositeAlphaMode::Opaque,
        };
        surface.configure(device, &config);

        let attach = TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING;
        let albedo = make_texture(device, size, TextureFormat::Rgba8Unorm, attach);
        let normal = make_texture(device, size, TextureFormat::Rgba8Unorm, attach);
        let id = make_texture(device, size, TextureFormat::Rgba8Unorm, attach);
        let depth = make_texture(device, size, TextureFormat::Depth32Float, TextureUsages::RENDER_ATTACHMENT);
        let lut = make_texture(
            device,
            Extent3d { width: 256, height: 1, depth_or_array_layers: 1 },
            TextureFormat::Rgba8Unorm,
            TextureUsages::TEXTURE_BINDING,
        );

        let lib = ShaderLib::compile(device);
        let depth = depth_state();
        let targets = phase1_targets();

        // Phase-1 pipelines (VS + per-mode FS, 3 color outputs)
        let make_p1 = |vs: &str, fs: &str| -> RenderPipeline {
            device.create_render_pipeline(&RenderPipelineDescriptor {
                label: Some(fs),
                layout: Some(&device.create_pipeline_layout(&PipelineLayoutDescriptor {
                    label: None,
                    bind_group_layouts: &lib.bind_layouts[1],
                    push_constant_ranges: &[],
                })),
                vertex: VertexState {
                    module: lib.module.vs(vs),
                    entry_point: Some("vs_main"),
                    buffers: &lib.vertex_buffers(),
                    compilation_options: Default::default(),
                },
                fragment: Some(FragmentState {
                    module: lib.module.fs(fs),
                    entry_point: Some("fs_main"),
                    targets: &targets,
                    compilation_options: Default::default(),
                }),
                primitive: PrimitiveState { topology: PrimitiveTopology::TriangleList, ..Default::default() },
                depth_stencil: Some(depth),
                multisample: MultisampleState::default(),
                multiview: None,
            })
        };
        let sol_pipe = make_p1("solid", "fs_solid");
        let field_pipe = make_p1("solid", "fs_field");
        let line_pipe = make_p1("line", "fs_line");
        let mesh_pipe = make_p1("mesh", "fs_mesh");
        let slice_pipe = make_p1("slice", "fs_slice");

        // Phase-2 compositor: single output, no depth
        let screen_pipe = device.create_render_pipeline(&RenderPipelineDescriptor {
            label: Some("phase2-compositor"),
            layout: Some(&device.create_pipeline_layout(&PipelineLayoutDescriptor {
                label: None,
                bind_group_layouts: &lib.bind_layouts[2],
                push_constant_ranges: &[],
            })),
            vertex: VertexState {
                module: lib.module.vs("scr"),
                entry_point: Some("vs_main"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(FragmentState {
                module: lib.module.fs("fs_scr"),
                entry_point: Some("fs_main"),
                targets: &[ColorTargetState { format: config.format, ..Default::default() }],
                compilation_options: Default::default(),
            }),
            primitive: PrimitiveState { topology: PrimitiveTopology::TriangleStrip, ..Default::default() },
            depth_stencil: None,
            multisample: MultisampleState::default(),
            multiview: None,
        });

        Self {
            device: device.clone(),
            queue: queue.clone(),
            config,
            albedo,
            normal,
            id,
            depth,
            lut,
            sol_pipe,
            field_pipe,
            line_pipe,
            mesh_pipe,
            slice_pipe,
            screen_pipe,
            solid_pos: None,
            solid_nrm: None,
            solid_idx: None,
            solid_count: 0,
            mesh_pos: None,
            mesh_col: [None; 4],
            mesh_val: [None; 4],
            mesh_count: 0,
        }
    }

    /// Camera-relative translation source: f64 domain center split into
    /// (hi, lo) f32 pairs so the vertex stage only handles small magnitudes.
    pub fn cam_rel(center: [f64; 3]) -> [f32; 6] {
        let mut out = [0.0f32; 6];
        for i in 0..3 {
            out[i] = center[i] as f32;
            out[3 + i] = (center[i] - out[i] as f64 * out[i] as f64) as f32;
        }
        out
    }

    /// Upload the 256×1 Oklab LUT (already band-quantized on the CPU).
    pub fn set_lut(&self, data: &[u8]) {
        let buf = self.device.create_buffer(&BufferDescriptor {
            label: Some("lut-upload"),
            size: data.len() as u64,
            usage: BufferUsages::COPY_SRC,
            mapped_at_creation: true,
        });
        buf.slice(..).get_mapped_range_mut().copy_from_slice(data);
        let mut enc = self.device.create_command_encoder(&Default::default());
        enc.copy_buffer_to_texture(
            TexelCopyBufferInfo { buffer: &buf, buffer_layout: Default::default() },
            TexelCopyTextureInfo { texture: &self.lut, mip_level: 0, origin: wgpu::Origin3d::ZERO, aspect: TextureAspect::All },
            Extent3d { width: 256, height: 1, depth_or_array_layers: 1 },
        );
        self.queue.submit([enc.finish()]);
    }

    /// One frame: phase-1 into the MRT set, phase-2 into the surface view.
    pub fn render(&self, surface_view: &TextureView, fs: &FrameState) {
        let (albedo_view, normal_view, id_view, depth_view) = (
            self.albedo.create_view(&Default::default()),
            self.normal.create_view(&Default::default()),
            self.id.create_view(&Default::default()),
            self.depth.create_view(&Default::default()),
        );
        let mut enc = self.device.create_command_encoder(&Default::default());

        // ── Phase 1 ──────────────────────────────────────────────────────
        let mut pass = RenderPassDescriptor {
            label: Some("phase1"),
            color_attachments: &[
                Some(ColorAttachment { view: &albedo_view, resolve_target: None, ops: LoadOp::Clear(ClearColor::from_array([0.055, 0.06, 0.075, 1.0])) .. StoreOp::Store }),
                Some(ColorAttachment { view: &normal_view, resolve_target: None, ops: LoadOp::Clear(ClearColor::TRANSPARENT) .. StoreOp::Store }),
                Some(ColorAttachment { view: &id_view, resolve_target: None, ops: LoadOp::Clear(ClearColor::TRANSPARENT) .. StoreOp::Store }),
            ],
            depth_stencil_attachment: Some(DepthStencilAttachment {
                view: &depth_view,
                depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(0.0), store: true }), // 0.0 = infinitely far @ rev-Z
                stencil_ops: None,
            }),
            timestamp_writes: None,
            multiview_view_count: None,
        };
        {
            let mut pass = enc.begin_render_pass(&pass);
            pass.set_blend_state(None);
            fs.draw_phase1(&mut pass, self);
        }

        // ── Phase 2 ──────────────────────────────────────────────────────
        pass = RenderPassDescriptor {
            label: Some("phase2"),
            color_attachments: &[Some(ColorAttachment { view: surface_view, resolve_target: None, ops: wgpu::Operations::default() .. StoreOp::Store })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            multiview_view_count: None,
        };
        {
            let mut pass = enc.begin_render_pass(&pass);
            fs.draw_phase2(&mut pass, self);
        }

        self.queue.submit([enc.finish()]);
    }

    /// GPU ray-pick: 1×1 id-texel → staging → async map. Returns packed
    /// face_group_id (overlay flag in the alpha channel).
    pub async fn pick(&self, x: u32, y: u32) -> Option<u32> {
        let staging = self.device.create_buffer(&BufferDescriptor {
            label: Some("id-pick-staging"),
            size: 4,
            usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut enc = self.device.create_command_encoder(&Default::default());
        enc.copy_texture_to_buffer(
            TexelCopyTextureInfo {
                texture: &self.id,
                mip_level: 0,
                origin: wgpu::Origin3d { x, y, z: 0 },
                aspect: TextureAspect::All,
            },
            TexelCopyBufferInfo { buffer: &staging, buffer_layout: Default::default() },
            Extent3d { width: 1, height: 1, depth_or_array_layers: 1 },
        );
        self.queue.submit([enc.finish()]);

        let slice = staging.slice(..);
        let (tx, rx) = mpsc::channel();
        slice.map_async(MapMode::Read, move |res| {
            let _ = tx.send(res);
        });
        rx.recv().ok()?.ok()?;
        let texel = slice.get_mapped_range();
        Some(texel[0] | (texel[1] << 8) | (texel[2] << 16) | (texel[3] << 24))
    }
}
