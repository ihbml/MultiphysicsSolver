use bytemuck::{Pod, Zeroable};
use crate::buffers::CfdGeo01View;
use glam::{DVec3, Mat4, Vec4};
use wgpu::util::DeviceExt;

pub const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;
pub const ID_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::R32Uint;

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct CameraUniform {
    pub view_projection: [[f32; 4]; 4],
    pub origin_high: [f32; 4],
    pub origin_low: [f32; 4],
}

impl CameraUniform {
    pub fn from_relative_view(origin: DVec3, view_projection: Mat4) -> Self {
        let split = |value: f64| {
            let high = value as f32;
            let low = (value - high as f64) as f32;
            (high, low)
        };
        let (hx, lx) = split(origin.x);
        let (hy, ly) = split(origin.y);
        let (hz, lz) = split(origin.z);
        Self {
            view_projection: view_projection.to_cols_array_2d(),
            origin_high: [hx, hy, hz, 0.0],
            origin_low: [lx, ly, lz, 0.0],
        }
    }
}

/// Subtract in f64 before converting the small camera-relative delta to f32.
pub fn camera_relative_position(world: DVec3, origin: DVec3) -> [f32; 3] {
    let relative = world - origin;
    [relative.x as f32, relative.y as f32, relative.z as f32]
}

/// WebGPU 0..1 infinite reverse-Z projection. Near -> 1, infinity -> 0.
pub fn reverse_z_infinite_projection(vertical_fov: f32, aspect: f32, near: f32) -> Mat4 {
    let f = 1.0 / (vertical_fov * 0.5).tan();
    Mat4::from_cols(
        Vec4::new(f / aspect, 0.0, 0.0, 0.0),
        Vec4::new(0.0, f, 0.0, 0.0),
        Vec4::new(0.0, 0.0, 0.0, -1.0),
        Vec4::new(0.0, 0.0, near, 0.0),
    )
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct GpuVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub scalar: f32,
    pub metrics: [f32; 4],
    pub face_group_id: u32,
    pub flags: u32,
}

impl GpuVertex {
    pub const ATTRIBUTES: [wgpu::VertexAttribute; 6] = wgpu::vertex_attr_array![
        0 => Float32x3, 1 => Float32x3, 2 => Float32, 3 => Float32x4, 4 => Uint32, 5 => Uint32
    ];

    pub fn layout() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<Self>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &Self::ATTRIBUTES,
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum GeometryBuildError {
    #[error("triangle references vertex {0} outside the CFDGEO01 vertex buffer")]
    InvalidVertex(u32),
}

pub struct ExpandedGeometry {
    pub vertices: Vec<GpuVertex>,
    pub indices: Vec<u32>,
}

/// Expand per-face face_group_id values while translating each f64 point relative
/// to the camera origin before the final f32 cast. This is the GPU upload boundary.
pub fn expand_geometry(geo: &CfdGeo01View<'_>, origin: DVec3) -> Result<ExpandedGeometry, GeometryBuildError> {
    let triangle_count = geo.triangles_u32.len() / 3;
    let mut vertices = Vec::with_capacity(triangle_count * 3);
    let default_normal = [0.0, 1.0, 0.0];
    for (triangle_index, triangle) in geo.triangles_u32.chunks_exact(3).enumerate() {
        let face_group = geo.face_group_id.get(triangle_index).copied().unwrap_or(0);
        for vertex_index in triangle {
            let point_offset = *vertex_index as usize * 3;
            let position = match geo.vertices_f64.get(point_offset..point_offset + 3) {
                Some(point) => DVec3::new(point[0], point[1], point[2]),
                None => return Err(GeometryBuildError::InvalidVertex(*vertex_index)),
            };
            let normal = geo.normals_f32.get(point_offset..point_offset + 3).unwrap_or(&default_normal);
            vertices.push(GpuVertex {
                position: camera_relative_position(position, origin),
                normal: [normal[0], normal[1], normal[2]],
                scalar: 0.0,
                metrics: [0.0; 4],
                face_group_id: face_group,
                flags: 0,
            });
        }
    }
    let indices = (0..vertices.len() as u32).collect();
    Ok(ExpandedGeometry { vertices, indices })
}

pub fn open_edge_line_vertices(geo: &CfdGeo01View<'_>, origin: DVec3) -> Result<Vec<[f32; 3]>, GeometryBuildError> {
    let mut output = Vec::with_capacity(geo.open_non_manifold_edges.len());
    for vertex_index in geo.open_non_manifold_edges {
        let point_offset = *vertex_index as usize * 3;
        let point = match geo.vertices_f64.get(point_offset..point_offset + 3) {
            Some(point) => DVec3::new(point[0], point[1], point[2]),
            None => return Err(GeometryBuildError::InvalidVertex(*vertex_index)),
        };
        output.push(camera_relative_position(point, origin));
    }
    Ok(output)
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct MeshStyleUniform {
    pub mode: u32,
    pub metric: u32,
    pub palette: u32,
    pub isolate: u32,
    pub threshold: f32,
    pub contour_bands: f32,
    pub selected_face_group: u32,
    pub edl_strength: f32,
    pub viewport_size: [f32; 2],
    pub padding: [f32; 2],
    pub slice_plane: [f32; 4],
    pub slice_enabled: f32,
    pub padding_tail: [f32; 3],
}

pub struct SceneTargets {
    pub depth: wgpu::Texture,
    pub depth_view: wgpu::TextureView,
    pub object_ids: wgpu::Texture,
    pub object_ids_view: wgpu::TextureView,
    pub width: u32,
    pub height: u32,
}

impl SceneTargets {
    pub fn new(device: &wgpu::Device, width: u32, height: u32) -> Self {
        let depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("reverse-z depth32f"), size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: DEPTH_FORMAT, usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let depth_view = depth.create_view(&Default::default());
        let object_ids = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("face_group_id attachment"), size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
            mip_level_count: 1, sample_count: 1, dimension: wgpu::TextureDimension::D2,
            format: ID_FORMAT, usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let object_ids_view = object_ids.create_view(&Default::default());
        Self { depth, depth_view, object_ids, object_ids_view, width, height }
    }
}

pub struct CfdRenderer {
    pub inspect_pipeline: wgpu::RenderPipeline,
    pub depth_pipeline: wgpu::RenderPipeline,
    pub id_pipeline: wgpu::RenderPipeline,
    pub field_pipeline: wgpu::RenderPipeline,
    pub camera_layout: wgpu::BindGroupLayout,
    pub mesh_style_layout: wgpu::BindGroupLayout,
    pub edl_layout: wgpu::BindGroupLayout,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct FlowSettings {
    pub camera_right: [f32; 4],
    pub time: f32,
    pub ribbon_width: f32,
    pub pulse_amplitude: f32,
    pub padding: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct RibbonSegment {
    pub start: [f32; 4],
    pub end: [f32; 4],
    pub color: [f32; 4],
}

pub struct FlowRenderer {
    pub pipeline: wgpu::RenderPipeline,
    pub bind_group_layout: wgpu::BindGroupLayout,
}

impl FlowRenderer {
    pub fn new(device: &wgpu::Device, color_format: wgpu::TextureFormat) -> Self {
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("instanced flow ribbon inputs"),
            entries: &[
                uniform_layout_entry(0, wgpu::ShaderStages::VERTEX),
                uniform_layout_entry(1, wgpu::ShaderStages::VERTEX),
                wgpu::BindGroupLayoutEntry { binding: 2, visibility: wgpu::ShaderStages::VERTEX, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Storage { read_only: true }, has_dynamic_offset: false, min_binding_size: None }, count: None },
            ],
        });
        let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("GPU instanced velocity ribbons"), source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/streamline.wgsl").into()),
        });
        let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { label: Some("flow ribbon layout"), bind_group_layouts: &[&bind_group_layout], push_constant_ranges: &[] });
        let targets = [Some(wgpu::ColorTargetState { format: color_format, blend: Some(wgpu::BlendState::ALPHA_BLENDING), write_mask: wgpu::ColorWrites::ALL })];
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("instanced streamline ribbon pipeline"), layout: Some(&layout),
            vertex: wgpu::VertexState { module: &module, entry_point: "vs_ribbon", buffers: &[] },
            primitive: wgpu::PrimitiveState { topology: wgpu::PrimitiveTopology::TriangleStrip, strip_index_format: None, cull_mode: None, ..Default::default() },
            depth_stencil: Some(wgpu::DepthStencilState { format: DEPTH_FORMAT, depth_write_enabled: false, depth_compare: wgpu::CompareFunction::GreaterEqual, stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default() }),
            multisample: wgpu::MultisampleState::default(), fragment: Some(wgpu::FragmentState { module: &module, entry_point: "fs_ribbon", targets: &targets }), multiview: None,
        });
        Self { pipeline, bind_group_layout }
    }

    pub fn bind_group(&self, device: &wgpu::Device, camera: &wgpu::Buffer, settings: &wgpu::Buffer, segments: &wgpu::Buffer) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("flow camera / settings / segment instances"), layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: camera.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 1, resource: settings.as_entire_binding() },
                wgpu::BindGroupEntry { binding: 2, resource: segments.as_entire_binding() },
            ],
        })
    }

    pub fn encode(&self, pass: &mut wgpu::RenderPass<'_>, bind_group: &wgpu::BindGroup, segment_count: u32) {
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, bind_group, &[]);
        pass.draw(0..4, 0..segment_count);
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct SurfaceLicSettings {
    pub size: [u32; 2],
    pub steps: u32,
    pub step_length: f32,
    pub noise_scale: f32,
    pub contrast: f32,
    pub padding: f32,
    pub padding_tail: u32,
}

pub struct SurfaceLicPass {
    pub pipeline: wgpu::ComputePipeline,
    pub bind_group_layout: wgpu::BindGroupLayout,
}

impl SurfaceLicPass {
    pub fn new(device: &wgpu::Device) -> Self {
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("wall shear Surface-LIC inputs / output"),
            entries: &[
                texture_layout_entry(0, wgpu::TextureSampleType::Float { filterable: false }),
                texture_layout_entry(1, wgpu::TextureSampleType::Float { filterable: false }),
                wgpu::BindGroupLayoutEntry { binding: 2, visibility: wgpu::ShaderStages::COMPUTE, ty: wgpu::BindingType::StorageTexture { access: wgpu::StorageTextureAccess::WriteOnly, format: wgpu::TextureFormat::Rgba16Float, view_dimension: wgpu::TextureViewDimension::D2 }, count: None },
                uniform_layout_entry(3, wgpu::ShaderStages::COMPUTE),
            ],
        });
        let module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Surface-LIC wall-shear convolution"), source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/surface_lic.wgsl").into()),
        });
        let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { label: Some("Surface-LIC compute layout"), bind_group_layouts: &[&bind_group_layout], push_constant_ranges: &[] });
        let pipeline = device.create_compute_pipeline(&wgpu::ComputePipelineDescriptor { label: Some("Surface-LIC kernel"), layout: Some(&layout), module: &module, entry_point: "cs_surface_lic" });
        Self { pipeline, bind_group_layout }
    }

    pub fn bind_group(&self, device: &wgpu::Device, shear: &wgpu::TextureView, noise: &wgpu::TextureView, output: &wgpu::TextureView, settings: &wgpu::Buffer) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Surface-LIC textures and constants"), layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(shear) },
                wgpu::BindGroupEntry { binding: 1, resource: wgpu::BindingResource::TextureView(noise) },
                wgpu::BindGroupEntry { binding: 2, resource: wgpu::BindingResource::TextureView(output) },
                wgpu::BindGroupEntry { binding: 3, resource: settings.as_entire_binding() },
            ],
        })
    }

    pub fn encode(&self, encoder: &mut wgpu::CommandEncoder, bind_group: &wgpu::BindGroup, width: u32, height: u32) {
        let mut pass = encoder.begin_compute_pass(&wgpu::ComputePassDescriptor { label: Some("wall shear Surface-LIC"), timestamp_writes: None });
        pass.set_pipeline(&self.pipeline); pass.set_bind_group(0, bind_group, &[]);
        pass.dispatch_workgroups(width.div_ceil(8), height.div_ceil(8), 1);
    }
}

fn uniform_layout_entry(binding: u32, visibility: wgpu::ShaderStages) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry { binding, visibility, ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None }
}

fn texture_layout_entry(binding: u32, sample_type: wgpu::TextureSampleType) -> wgpu::BindGroupLayoutEntry {
    wgpu::BindGroupLayoutEntry { binding, visibility: wgpu::ShaderStages::COMPUTE, ty: wgpu::BindingType::Texture { sample_type, view_dimension: wgpu::TextureViewDimension::D2, multisampled: false }, count: None }
}

impl CfdRenderer {
    pub fn new(device: &wgpu::Device, color_format: wgpu::TextureFormat) -> Self {
        let camera_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("camera relative uniforms"), entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0, visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None,
            }],
        });
        let mesh_style_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("mesh inspection controls"), entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0, visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer { ty: wgpu::BufferBindingType::Uniform, has_dynamic_offset: false, min_binding_size: None }, count: None,
            }],
        });
        let edl_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("screen-space EDL depth"), entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0, visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Texture { sample_type: wgpu::TextureSampleType::Depth, view_dimension: wgpu::TextureViewDimension::D2, multisampled: false }, count: None,
            }],
        });
        let mesh_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("mesh matcap cavity / ID WGSL"), source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/mesh_inspect.wgsl").into()),
        });
        let field_module = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("unlit field Oklab WGSL"), source: wgpu::ShaderSource::Wgsl(include_str!("../../shaders/field_unlit.wgsl").into()),
        });
        let camera_only = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { label: Some("depth / ID layout"), bind_group_layouts: &[&camera_layout], push_constant_ranges: &[] });
        let inspect_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { label: Some("inspection layout"), bind_group_layouts: &[&camera_layout, &mesh_style_layout, &edl_layout], push_constant_ranges: &[] });
        let color_target = [Some(wgpu::ColorTargetState { format: color_format, blend: Some(wgpu::BlendState::REPLACE), write_mask: wgpu::ColorWrites::ALL })];
        let id_target = [Some(wgpu::ColorTargetState { format: ID_FORMAT, blend: None, write_mask: wgpu::ColorWrites::ALL })];
        let depth_state = wgpu::DepthStencilState {
            format: DEPTH_FORMAT, depth_write_enabled: true, depth_compare: wgpu::CompareFunction::GreaterEqual,
            stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default(),
        };
        let mesh_vertex = wgpu::VertexState { module: &mesh_module, entry_point: "vs_main", buffers: &[GpuVertex::layout()] };
        let inspect_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("CAD / mesh matcap + EDL"), layout: Some(&inspect_layout), vertex: mesh_vertex,
            primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() }, depth_stencil: Some(depth_state.clone()),
            multisample: wgpu::MultisampleState::default(), fragment: Some(wgpu::FragmentState { module: &mesh_module, entry_point: "fs_inspect", targets: &color_target }), multiview: None,
        });
        let depth_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("reverse-Z depth prepass"), layout: Some(&camera_only),
            vertex: wgpu::VertexState { module: &mesh_module, entry_point: "vs_depth", buffers: &[GpuVertex::layout()] },
            primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() }, depth_stencil: Some(depth_state.clone()),
            multisample: wgpu::MultisampleState::default(), fragment: None, multiview: None,
        });
        let id_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("face_group_id GPU pick pass"), layout: Some(&camera_only),
            vertex: wgpu::VertexState { module: &mesh_module, entry_point: "vs_pick", buffers: &[GpuVertex::layout()] },
            primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() }, depth_stencil: Some(depth_state),
            multisample: wgpu::MultisampleState::default(), fragment: Some(wgpu::FragmentState { module: &mesh_module, entry_point: "fs_pick", targets: &id_target }), multiview: None,
        });
        let field_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor { label: Some("unlit field layout"), bind_group_layouts: &[&camera_layout, &mesh_style_layout], push_constant_ranges: &[] });
        let field_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("scientific field / zero tone map"), layout: Some(&field_layout),
            vertex: wgpu::VertexState { module: &field_module, entry_point: "vs_field", buffers: &[GpuVertex::layout()] },
            primitive: wgpu::PrimitiveState { cull_mode: None, ..Default::default() },
            depth_stencil: Some(wgpu::DepthStencilState { format: DEPTH_FORMAT, depth_write_enabled: false, depth_compare: wgpu::CompareFunction::GreaterEqual, stencil: wgpu::StencilState::default(), bias: wgpu::DepthBiasState::default() }),
            multisample: wgpu::MultisampleState::default(),
            fragment: Some(wgpu::FragmentState { module: &field_module, entry_point: "fs_field", targets: &color_target }), multiview: None,
        });
        Self { inspect_pipeline, depth_pipeline, id_pipeline, field_pipeline, camera_layout, mesh_style_layout, edl_layout }
    }

    pub fn camera_bind_group(&self, device: &wgpu::Device, uniform: &wgpu::Buffer) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("camera uniforms"), layout: &self.camera_layout,
            entries: &[wgpu::BindGroupEntry { binding: 0, resource: uniform.as_entire_binding() }],
        })
    }

    pub fn style_bind_group(&self, device: &wgpu::Device, uniform: &wgpu::Buffer) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("inspection style uniforms"), layout: &self.mesh_style_layout,
            entries: &[wgpu::BindGroupEntry { binding: 0, resource: uniform.as_entire_binding() }],
        })
    }

    pub fn edl_bind_group(&self, device: &wgpu::Device, depth: &wgpu::TextureView) -> wgpu::BindGroup {
        device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("EDL depth input"), layout: &self.edl_layout,
            entries: &[wgpu::BindGroupEntry { binding: 0, resource: wgpu::BindingResource::TextureView(depth) }],
        })
    }

    /// Depth prepass -> EDL shaded surface -> integer ID pass. Reverse-Z clears to zero.
    pub fn encode_inspection_passes(
        &self,
        encoder: &mut wgpu::CommandEncoder,
        target: &wgpu::TextureView,
        targets: &SceneTargets,
        camera: &wgpu::BindGroup,
        style: &wgpu::BindGroup,
        edl: &wgpu::BindGroup,
        vertex: &wgpu::Buffer,
        index: &wgpu::Buffer,
        index_count: u32,
    ) {
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("reverse-Z depth prepass"), color_attachments: &[],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &targets.depth_view,
                    depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(0.0), store: wgpu::StoreOp::Store }),
                    stencil_ops: None,
                }), timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&self.depth_pipeline); pass.set_bind_group(0, camera, &[]);
            pass.set_vertex_buffer(0, vertex.slice(..)); pass.set_index_buffer(index.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(0..index_count, 0, 0..1);
        }
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("matcap / EDL / cavity"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment { view: target, resolve_target: None, ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK), store: wgpu::StoreOp::Store } })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment { view: &targets.depth_view, depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store }), stencil_ops: None }),
                timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&self.inspect_pipeline); pass.set_bind_group(0, camera, &[]); pass.set_bind_group(1, style, &[]); pass.set_bind_group(2, edl, &[]);
            pass.set_vertex_buffer(0, vertex.slice(..)); pass.set_index_buffer(index.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(0..index_count, 0, 0..1);
        }
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("GPU face_group_id buffer"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment { view: &targets.object_ids_view, resolve_target: None, ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT), store: wgpu::StoreOp::Store } })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment { view: &targets.depth_view, depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store }), stencil_ops: None }),
                timestamp_writes: None, occlusion_query_set: None,
            });
            pass.set_pipeline(&self.id_pipeline); pass.set_bind_group(0, camera, &[]);
            pass.set_vertex_buffer(0, vertex.slice(..)); pass.set_index_buffer(index.slice(..), wgpu::IndexFormat::Uint32);
            pass.draw_indexed(0..index_count, 0, 0..1);
        }
    }

    /// Unlit field pass over the reverse-Z depth buffer, with optional contours and slice clipping.
    pub fn encode_field_pass(
        &self,
        encoder: &mut wgpu::CommandEncoder,
        target: &wgpu::TextureView,
        depth: &wgpu::TextureView,
        camera: &wgpu::BindGroup,
        style: &wgpu::BindGroup,
        vertex: &wgpu::Buffer,
        index: &wgpu::Buffer,
        index_count: u32,
    ) {
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("unlit scientific field / zero tone map"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: target,
                resolve_target: None,
                ops: wgpu::Operations { load: wgpu::LoadOp::Clear(wgpu::Color::BLACK), store: wgpu::StoreOp::Store },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth,
                depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
        });
        pass.set_pipeline(&self.field_pipeline);
        pass.set_bind_group(0, camera, &[]);
        pass.set_bind_group(1, style, &[]);
        pass.set_vertex_buffer(0, vertex.slice(..));
        pass.set_index_buffer(index.slice(..), wgpu::IndexFormat::Uint32);
        pass.draw_indexed(0..index_count, 0, 0..1);
    }
}

/// Read one R32Uint ID pixel. Mapping is asynchronous; only the tiny padded row is copied.
pub fn read_id_pixel(device: &wgpu::Device, queue: &wgpu::Queue, ids: &wgpu::Texture, x: u32, y: u32) -> Option<u32> {
    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("1x1 asynchronous pick readback"), size: 256,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ, mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor { label: Some("copy picked face group") });
    encoder.copy_texture_to_buffer(
        wgpu::ImageCopyTexture { texture: ids, mip_level: 0, origin: wgpu::Origin3d { x, y, z: 0 }, aspect: wgpu::TextureAspect::All },
        wgpu::ImageCopyBuffer { buffer: &buffer, layout: wgpu::ImageDataLayout { offset: 0, bytes_per_row: Some(256), rows_per_image: Some(1) } },
        wgpu::Extent3d { width: 1, height: 1, depth_or_array_layers: 1 },
    );
    queue.submit(Some(encoder.finish()));
    let slice = buffer.slice(..);
    let (sender, receiver) = std::sync::mpsc::channel();
    slice.map_async(wgpu::MapMode::Read, move |result| { let _ = sender.send(result); });
    device.poll(wgpu::Maintain::Wait);
    receiver.recv().ok()?.ok()?;
    let mapped = slice.get_mapped_range();
    let result = bytemuck::pod_read_unaligned::<u32>(&mapped[0..4]);
    drop(mapped);
    buffer.unmap();
    Some(result)
}

/// Create a camera uniform buffer using the precision-preserving camera origin split.
pub fn create_camera_buffer(device: &wgpu::Device, camera: CameraUniform) -> wgpu::Buffer {
    device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
        label: Some("camera-relative f64 origin / f32 delta"), contents: bytemuck::bytes_of(&camera), usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
    })
}