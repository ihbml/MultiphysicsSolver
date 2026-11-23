//! wgpu reverse-Z viewport with an R32Uint face pick pass, Oklab LUT sampling,
//! spatial rule highlighting and synchronized A/B (wipe/split/delta) rendering.

use bytemuck::{Pod, Zeroable};
use glam::{Mat4, Vec2, Vec3, Vec4};
use wgpu::util::DeviceExt;

use crate::compare::CompareMode;

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */
#[derive(Clone, Copy, Debug)]
pub struct CameraState {
    pub target: Vec3,
    pub azimuth: f32,
    pub elevation: f32,
    pub distance: f32,
    pub fov_y: f32,
    pub near: f32,
}

impl Default for CameraState {
    fn default() -> Self {
        Self {
            target: Vec3::new(0.12, 0.02, 0.04),
            azimuth: -0.72,
            elevation: 0.34,
            distance: 6.4,
            fov_y: 31_f32.to_radians(),
            near: 0.08,
        }
    }
}

impl CameraState {
    pub fn eye(&self) -> Vec3 {
        let ce = self.elevation.cos();
        self.target
            + Vec3::new(
                self.distance * ce * self.azimuth.sin(),
                self.distance * self.elevation.sin(),
                self.distance * ce * self.azimuth.cos(),
            )
    }

    /// Reverse-Z infinite far plane: near → 1, infinity → 0, depth compare Greater.
    pub fn view_projection(&self, aspect: f32) -> Mat4 {
        let view = Mat4::look_at_rh(self.eye(), self.target, Vec3::Y);
        let proj = Mat4::perspective_infinite_reverse_rh(self.fov_y, aspect.max(0.01), self.near);
        proj * view
    }

    pub fn orbit(&mut self, delta: Vec2) {
        self.azimuth -= delta.x * 0.0072;
        self.elevation = (self.elevation + delta.y * 0.006).clamp(-0.28, 1.48);
    }

    pub fn pan(&mut self, delta: Vec2) {
        let right = Vec3::new(self.azimuth.cos(), 0.0, -self.azimuth.sin());
        let k = self.distance * 0.0016;
        self.target += right * (-delta.x * k) + Vec3::Y * (delta.y * k);
    }

    pub fn dolly(&mut self, scroll: f32) {
        self.distance = (self.distance * (scroll * 0.0011).exp()).clamp(1.9, 24.0);
    }
}

/* ------------------------------------------------------------------ */
/* GPU data                                                            */
/* ------------------------------------------------------------------ */
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct SurfaceVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    /// `face_group_id` straight from the pick buffer convention.
    pub group: u32,
    pub scalar_a: f32,
    pub scalar_b: f32,
    /// 1.0 when this vertex belongs to a face group with a BC overlap.
    pub conflict: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct LineVertex {
    pub position: [f32; 3],
    pub color: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct SceneUniforms {
    pub vp: [[f32; 4]; 4],
    pub eye: [f32; 4],
    pub range_a: [f32; 4],
    pub range_b: [f32; 4],
    /// x: delta half span, y: slice position, z/w: mode + flags.
    pub params: [f32; 4],
    /// x: highlight group (-1 = none), y: conflict group, z/w: reserved.
    pub groups: [f32; 4],
    pub slice_normal: [f32; 4],
}

#[derive(Clone, Debug)]
pub struct RunPayload {
    pub label: String,
    pub field_buffer: wgpu::Buffer,
    pub range: [f32; 2],
}

#[derive(Clone, Debug)]
pub struct RenderPlan {
    pub mode: CompareMode,
    pub wipe: f32,
    pub slice_enabled: bool,
    pub slice_position: f32,
    pub slice_normal: Vec3,
    pub highlight_group: i32,
    pub conflict_group: i32,
    pub delta_half_span: f32,
}

/* ------------------------------------------------------------------ */
/* Shaders                                                            */
/* ------------------------------------------------------------------ */
pub const SURFACE_WGSL: &str = include_str!("shaders/surface.wgsl");
pub const LINES_WGSL: &str = include_str!("shaders/lines.wgsl");
pub const PICK_WGSL: &str = include_str!("shaders/pick.wgsl");

/* ------------------------------------------------------------------ */
/* Oklab LUT (CPU side; uploaded as a 1D texture)                      */
/* ------------------------------------------------------------------ */
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Colormap {
    OklabTurbo,
    MorelandCoolWarm,
    Inferno,
}

impl Colormap {
    fn stops(self) -> &'static [[f32; 3]] {
        match self {
            Colormap::OklabTurbo => &[
                [0.188, 0.071, 0.231], [0.263, 0.376, 0.847], [0.165, 0.663, 1.0],
                [0.102, 0.894, 0.714], [0.651, 0.988, 0.235], [1.0, 0.847, 0.239],
                [0.949, 0.439, 0.039], [0.643, 0.078, 0.004],
            ],
            Colormap::MorelandCoolWarm => &[
                [0.231, 0.298, 0.753], [0.427, 0.561, 0.914], [0.608, 0.737, 0.949],
                [0.812, 0.851, 0.902], [0.894, 0.871, 0.859], [0.957, 0.765, 0.682],
                [0.953, 0.600, 0.482], [0.851, 0.373, 0.310], [0.706, 0.016, 0.149],
            ],
            Colormap::Inferno => &[
                [0.0, 0.0, 0.016], [0.106, 0.047, 0.255], [0.294, 0.047, 0.420],
                [0.478, 0.110, 0.427], [0.647, 0.173, 0.376], [0.824, 0.267, 0.271],
                [0.937, 0.412, 0.145], [0.984, 0.608, 0.024], [0.969, 0.820, 0.239],
                [0.988, 1.0, 0.643],
            ],
        }
    }

    pub fn lut(self, n: usize) -> Vec<[u8; 4]> {
        let stops = self.stops();
        (0..n)
            .map(|i| {
                let t = i as f32 / (n - 1).max(1) as f32;
                let p = t * (stops.len() - 1) as f32;
                let i0 = p.floor() as usize;
                let i1 = (i0 + 1).min(stops.len() - 1);
                let f = p - i0 as f32;
                let c = [
                    stops[i0][0] + (stops[i1][0] - stops[i0][0]) * f,
                    stops[i0][1] + (stops[i1][1] - stops[i0][1]) * f,
                    stops[i0][2] + (stops[i1][2] - stops[i0][2]) * f,
                ];
                [c[0], c[1], c[2], 1.0].map(|v| (v.clamp(0.0, 1.0) * 255.0) as u8)
            })
            .collect()
    }

    pub fn create_texture(self, device: &wgpu::Device, queue: &wgpu::Queue) -> wgpu::Texture {
        let data = self.lut(256);
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("field-lut"),
            size: wgpu::Extent3d { width: 256, height: 1, depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D1,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(
            wgpu::ImageCopyTexture {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            bytemuck::cast_slice(&data),
            wgpu::ImageDataLayout {
                offset: 0,
                bytes_per_row: Some(256 * 4),
                rows_per_image: Some(1),
            },
            wgpu::Extent3d { width: 256, height: 1, depth_or_array_layers: 1 },
        );
        texture
    }
}

/* ------------------------------------------------------------------ */
/* Pick target (R32Uint + reverse-Z depth)                             */
/* ------------------------------------------------------------------ */
pub struct PickTarget {
    pub color: wgpu::Texture,
    pub color_view: wgpu::TextureView,
    pub depth: wgpu::Texture,
    pub depth_view: wgpu::TextureView,
    pub readback: wgpu::Buffer,
    pub width: u32,
    pub height: u32,
    pub padded_row_bytes: u32,
}

impl PickTarget {
    pub fn new(device: &wgpu::Device, width: u32, height: u32) -> Self {
        let width = width.max(32);
        let height = height.max(32);
        let color = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("face-pick R32Uint"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::R32Uint,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("face-pick depth"),
            size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth32Float,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let padded_row_bytes = ((width * 4 + 255) / 256) * 256;
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("face-pick readback"),
            size: (padded_row_bytes * height) as u64,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        Self {
            color_view: color.create_view(&wgpu::TextureViewDescriptor::default()),
            depth_view: depth.create_view(&wgpu::TextureViewDescriptor::default()),
            color,
            depth,
            readback,
            width,
            height,
            padded_row_bytes,
        }
    }

    /// Resolve the integer under the cursor. `x,y` are logical pixels.
    pub fn read_pick(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        x: f32,
        y: f32,
    ) -> Option<u32> {
        let px = ((x / self.width as f32) * self.width as f32).clamp(0.0, self.width as f32 - 1.0) as u32;
        let py = ((y / self.height as f32) * self.height as f32).clamp(0.0, self.height as f32 - 1.0) as u32;
        encoder.copy_texture_to_buffer(
            wgpu::ImageCopyTexture {
                texture: &self.color,
                mip_level: 0,
                origin: wgpu::Origin3d { x: 0, y: 0, z: 0 },
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::ImageCopyBuffer {
                buffer: &self.readback,
                layout: wgpu::ImageDataLayout {
                    offset: 0,
                    bytes_per_row: Some(self.padded_row_bytes),
                    rows_per_image: Some(self.height),
                },
            },
            wgpu::Extent3d { width: self.width, height: self.height, depth_or_array_layers: 1 },
        );
        queue.submit(Some(encoder.finish()));
        self.readback.slice(..).map_async(wgpu::MapMode::Read, |_| {});
        device.poll(wgpu::Maintain::Wait);
        let data = self.readback.slice(..).get_mapped_range();
        let row = py as usize * self.padded_row_bytes as usize;
        let offset = row + px as usize * 4;
        let group = u32::from_le_bytes([
            data[offset],
            data[offset + 1],
            data[offset + 2],
            data[offset + 3],
        ]);
        drop(data);
        self.readback.unmap();
        if group == 0 { None } else { Some(group) }
    }
}

/* ------------------------------------------------------------------ */
/* Gizmos                                                              */
/* ------------------------------------------------------------------ */
#[derive(Default)]
pub struct GizmoBuilder {
    pub lines: Vec<LineVertex>,
    pub points: Vec<Vec3>,
}

impl GizmoBuilder {
    pub fn line(&mut self, a: Vec3, b: Vec3, color: [f32; 4]) {
        self.lines.push(LineVertex { position: a.to_array(), color });
        self.lines.push(LineVertex { position: b.to_array(), color });
    }

    pub fn circle(&mut self, center: Vec3, radius: f32, axis: usize, color: [f32; 4], segments: usize) {
        for i in 0..segments {
            let a0 = i as f32 / segments as f32 * std::f32::consts::TAU;
            let a1 = (i + 1) as f32 / segments as f32 * std::f32::consts::TAU;
            let at = |a: f32| -> Vec3 {
                let (u, v) = (a.cos() * radius, a.sin() * radius);
                match axis {
                    0 => center + Vec3::new(0.0, u, v),
                    1 => center + Vec3::new(u, 0.0, v),
                    _ => center + Vec3::new(u, v, 0.0),
                }
            };
            self.line(at(a0), at(a1), color);
        }
    }

    /// DPM injector cone: apex, axis, half-angle θ. Also used for the drag
    /// preview while the actor is dragged from the palette into the viewport.
    pub fn spray_cone(&mut self, apex: Vec3, axis: Vec3, half_angle_deg: f32, length: f32, color: [f32; 4]) {
        let axis = axis.normalize_or_zero();
        let helper = if axis.y.abs() > 0.9 { Vec3::X } else { Vec3::Y };
        let p1 = axis.cross(helper).normalize_or_zero();
        let p2 = axis.cross(p1);
        let tip = apex + axis * length;
        let radius = length * half_angle_deg.to_radians().tan();
        self.line(apex, tip, color);
        for i in 0..32 {
            let a = i as f32 / 32.0 * std::f32::consts::TAU;
            let p = tip + p1 * (a.cos() * radius) + p2 * (a.sin() * radius);
            self.line(apex, p, color);
        }
        self.circle(tip, radius, 1, color, 32);
        self.points.push(apex);
    }

    /// FWH microphone: x_mic plus a distance sphere.
    pub fn microphone(&mut self, x_mic: Vec3, radius: f32, color: [f32; 4]) {
        self.circle(x_mic, radius, 0, color, 40);
        self.circle(x_mic, radius, 1, color, 40);
        self.circle(x_mic, radius, 2, color, 40);
        self.points.push(x_mic);
    }

    /// Anisotropic porous media: local principal axes e1/e2/e3.
    pub fn porous_axes(&mut self, anchor: Vec3, e1: Vec3, e2: Vec3, e3: Vec3) {
        self.line(anchor, anchor + e1.normalize_or_zero() * 0.5, [0.95, 0.36, 0.42, 1.0]);
        self.line(anchor, anchor + e2.normalize_or_zero() * 0.46, [0.35, 0.86, 0.65, 1.0]);
        self.line(anchor, anchor + e3.normalize_or_zero() * 0.42, [0.4, 0.62, 1.0, 1.0]);
        self.points.push(anchor);
    }

    pub fn into_buffers(&self, device: &wgpu::Device) -> (wgpu::Buffer, wgpu::Buffer) {
        let lines = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("gizmo-lines"),
            contents: bytemuck::cast_slice(&self.lines),
            usage: wgpu::BufferUsages::VERTEX,
        });
        let pts = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("gizmo-points"),
            contents: bytemuck::cast_slice(&self.points),
            usage: wgpu::BufferUsages::VERTEX,
        });
        (lines, pts)
    }
}

/* ------------------------------------------------------------------ */
/* Renderer                                                            */
/* ------------------------------------------------------------------ */
pub struct ViewportRenderer {
    surface_pipeline: wgpu::RenderPipeline,
    line_pipeline: wgpu::RenderPipeline,
    pick_pipeline: wgpu::RenderPipeline,
    uniforms_buffer: wgpu::Buffer,
    uniform_bind_group: wgpu::BindGroup,
    lut_textures: [wgpu::TextureView; 3],
    pub pick: PickTarget,
    pub camera: CameraState,
    depth: wgpu::TextureView,
    surface_vertex_buffer: wgpu::Buffer,
    surface_index_buffer: wgpu::Buffer,
    surface_index_count: u32,
    pub line_buffer: wgpu::Buffer,
    pub line_vertex_count: u32,
}

pub const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;
pub const COLOR_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba16Float;

impl ViewportRenderer {
    pub fn new(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        format: wgpu::TextureFormat,
        vertices: &[SurfaceVertex],
        indices: &[u32],
        width: u32,
        height: u32,
    ) -> Self {
        let surface_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("surface"),
            source: wgpu::ShaderSource::Wgsl(SURFACE_WGSL.into()),
        });
        let pick_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("pick"),
            source: wgpu::ShaderSource::Wgsl(PICK_WGSL.into()),
        });
        let line_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("lines"),
            source: wgpu::ShaderSource::Wgsl(LINES_WGSL.into()),
        });

        let surface_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("scene-layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D1,
                        multisampled: false,
                    },
                    count: None,
                },
            ],
        });
        let uniforms_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("scene-uniforms"),
            size: std::mem::size_of::<SceneUniforms>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let luts = [
            Colormap::OklabTurbo.create_texture(device, queue),
            Colormap::MorelandCoolWarm.create_texture(device, queue),
            Colormap::Inferno.create_texture(device, queue),
        ];
        let lut_views: Vec<wgpu::TextureView> = luts
            .iter()
            .map(|t| t.create_view(&wgpu::TextureViewDescriptor::default()))
            .collect();
        // The LUT referenced by the bind group is switched per draw with a
        // second bind group; here the diverging LUT is the default.
        let uniform_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("scene-bind"),
            layout: &surface_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: uniforms_buffer.as_entire_binding() },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&lut_views[1]),
                },
            ],
        });

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("surface-layout"),
            bind_group_layouts: &[&surface_layout],
            push_constant_ranges: &[],
        });
        let surface_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("surface"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &surface_shader,
                entry_point: Some("vs_surface"),
                buffers: &[wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<SurfaceVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![
                        0 => Float32x3,
                        1 => Float32x3,
                        2 => Uint32,
                        3 => Float32,
                        4 => Float32,
                        5 => Float32
                    ],
                }],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &surface_shader,
                entry_point: Some("fs_surface"),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: DEPTH_FORMAT,
                depth_write_enabled: true,
                // Reverse-Z: nearer fragments have larger depth.
                depth_compare: wgpu::CompareFunction::Greater,
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: Default::default(),
            multiview: None,
            cache: None,
        });

        let line_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("gizmo-lines"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &line_shader,
                entry_point: Some("vs_line"),
                buffers: &[wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<LineVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![0 => Float32x3, 1 => Float32x4],
                }],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &line_shader,
                entry_point: Some("fs_line"),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::LineList,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: DEPTH_FORMAT,
                depth_write_enabled: false,
                depth_compare: wgpu::CompareFunction::GreaterEqual,
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: Default::default(),
            multiview: None,
            cache: None,
        });

        let pick_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("face-pick"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &pick_shader,
                entry_point: Some("vs_pick"),
                buffers: &[wgpu::VertexBufferLayout {
                    array_stride: std::mem::size_of::<SurfaceVertex>() as u64,
                    step_mode: wgpu::VertexStepMode::Vertex,
                    attributes: &wgpu::vertex_attr_array![0 => Float32x3, 2 => Uint32],
                }],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &pick_shader,
                entry_point: Some("fs_pick"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: wgpu::TextureFormat::R32Uint,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: Default::default(),
            depth_stencil: Some(wgpu::DepthStencilState {
                format: DEPTH_FORMAT,
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Greater,
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: Default::default(),
            multiview: None,
            cache: None,
        });

        let surface_vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("blade-vertices"),
            contents: bytemuck::cast_slice(vertices),
            usage: wgpu::BufferUsages::VERTEX,
        });
        let surface_index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("blade-indices"),
            contents: bytemuck::cast_slice(indices),
            usage: wgpu::BufferUsages::INDEX,
        });
        let depth = Self::create_depth(device, width, height);

        Self {
            surface_pipeline,
            line_pipeline,
            pick_pipeline,
            uniforms_buffer,
            uniform_bind_group,
            lut_textures: lut_views.try_into().unwrap_or_else(|_| unreachable!()),
            pick: PickTarget::new(device, width, height),
            camera: CameraState::default(),
            depth,
            surface_vertex_buffer,
            surface_index_buffer,
            surface_index_count: indices.len() as u32,
            line_buffer: device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("gizmo-lines"),
                size: 4096 * std::mem::size_of::<LineVertex>() as u64,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            }),
            line_vertex_count: 0,
        }
    }

    fn create_depth(device: &wgpu::Device, width: u32, height: u32) -> wgpu::TextureView {
        device
            .create_texture(&wgpu::TextureDescriptor {
                label: Some("reverse-z-depth"),
                size: wgpu::Extent3d { width: width.max(1), height: height.max(1), depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: DEPTH_FORMAT,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                view_formats: &[],
            })
            .create_view(&wgpu::TextureViewDescriptor::default())
    }

    pub fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        self.depth = Self::create_depth(device, width, height);
        self.pick = PickTarget::new(device, width, height);
    }

    fn write_uniforms(&self, queue: &wgpu::Queue, vp: Mat4, plan: &RenderPlan) {
        let eye = self.camera.eye();
        let uniforms = SceneUniforms {
            vp: vp.to_cols_array_2d(),
            eye: [eye.x, eye.y, eye.z, 1.0],
            range_a: [0.0, 1.0, 0.0, 0.0],
            range_b: [0.0, 1.0, 0.0, 0.0],
            params: [
                plan.delta_half_span,
                plan.slice_position,
                match plan.mode {
                    CompareMode::Wipe => 0.0,
                    CompareMode::Split => 1.0,
                    CompareMode::Delta => 2.0,
                },
                if plan.slice_enabled { 1.0 } else { 0.0 },
            ],
            groups: [
                plan.highlight_group as f32,
                plan.conflict_group as f32,
                0.0,
                0.0,
            ],
            slice_normal: plan.slice_normal.extend(0.0).to_array(),
        };
        queue.write_buffer(&self.uniforms_buffer, 0, bytemuck::bytes_of(&uniforms));
    }

    /// Render both runs. `wipe` uses one scissored pass with a shared projection;
    /// `split` renders two half-width viewports; `delta` uses the diverging LUT.
    pub fn render(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        color_view: &wgpu::TextureView,
        width: u32,
        height: u32,
        plan: &RenderPlan,
    ) {
        self.write_uniforms(queue, self.camera.view_projection(width as f32 / height.max(1) as f32), plan);
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("cockpit-viewport"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.066, g: 0.071, b: 0.086, a: 1.0 }),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: &self.depth,
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(0.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.surface_pipeline);
        pass.set_bind_group(0, &self.uniform_bind_group, &[]);
        pass.set_vertex_buffer(0, self.surface_vertex_buffer.slice(..));
        pass.set_index_buffer(self.surface_index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        match plan.mode {
            CompareMode::Split => {
                pass.set_viewport(0.0, 0.0, width as f32 / 2.0, height as f32, 0.0, 1.0);
                pass.draw_indexed(0..self.surface_index_count, 0, 0..1);
                pass.set_pipeline(&self.line_pipeline);
                pass.set_vertex_buffer(0, self.line_buffer.slice(..));
                pass.draw(0..self.line_vertex_count, 0..1);
                pass.set_viewport(width as f32 / 2.0, 0.0, width as f32 / 2.0, height as f32, 0.0, 1.0);
                pass.set_pipeline(&self.surface_pipeline);
                pass.draw_indexed(0..self.surface_index_count, 0, 1..2);
            }
            CompareMode::Wipe => {
                let split = (width as f32 * plan.wipe.clamp(0.02, 0.98)) as u32;
                pass.set_scissor_rect(0, 0, split, height);
                pass.draw_indexed(0..self.surface_index_count, 0, 0..1);
                pass.set_scissor_rect(split, 0, width - split, height);
                pass.draw_indexed(0..self.surface_index_count, 0, 2..3);
            }
            CompareMode::Delta => {
                pass.draw_indexed(0..self.surface_index_count, 0, 2..3);
            }
        }
    }

    /// Separate integer pass used for click and hover picking.
    pub fn render_pick(&mut self, encoder: &mut wgpu::CommandEncoder) {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("pick-pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &self.pick.color_view,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: &self.pick.depth_view,
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(0.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.pick_pipeline);
        pass.set_bind_group(0, &self.uniform_bind_group, &[]);
        pass.set_vertex_buffer(0, self.surface_vertex_buffer.slice(..));
        pass.set_index_buffer(self.surface_index_buffer.slice(..), wgpu::IndexFormat::Uint32);
        pass.draw_indexed(0..self.surface_index_count, 0, 0..1);
    }
}

pub fn face_group_to_patch(group: u32) -> Option<&'static str> {
    Some(match group {
        1 => "LE",
        2 => "SS",
        3 => "PS",
        4 => "TE",
        5 => "TIP",
        6 => "HUB",
        7 => "SHROUD",
        _ => return None,
    })
}
