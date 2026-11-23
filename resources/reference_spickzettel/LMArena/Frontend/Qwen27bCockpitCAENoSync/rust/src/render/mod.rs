//! Render subsystem — two-phase pipeline + shader library.

pub mod pipeline;

use wgpu::*;

/// Compiled shader library with per-stage entry points and the three
/// binding-group layouts (cam | field/lut/phase-1 textures | line color).
pub struct ShaderLib {
    pub module: ShaderModule,
    pub bind_layouts: [BindGroupLayout; 3],
}

impl ShaderLib {
    pub fn compile(device: &Device) -> Self {
        let module = device.create_shader_module(wgpu::ShaderSource::Wgsl(
            include_str!("shaders.wgsl").into(),
        ));

        // group 0: Cam uniform
        let cam_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("cam"),
            entries: &[BindGroupLayoutEntry {
                binding: 0,
                visibility: ShaderStages::VERTEX | ShaderStages::FRAGMENT,
                ty: BindingType::Buffer {
                    ty: BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: BufferSize { min: 96 },
                },
                count: None,
            }],
        });

        // group 1: Field uniform + LUT + phase-1 textures + sampler
        let field_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("field"),
            entries: &[
                BindGroupLayoutEntry {
                    binding: 0,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Buffer {
                        ty: BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: BufferSize { min: 128 },
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 1,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Filtering,
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 2,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Filtering,
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 3,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Filtering,
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 4,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Texture {
                        sample_type: TextureSampleType::Filtering,
                        view_dimension: TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 5,
                    visibility: ShaderStages::FRAGMENT,
                    ty: BindingType::Sampler(SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });

        // group 2: line / defect-edge color
        let line_layout = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("line-color"),
            entries: &[BindGroupLayoutEntry {
                binding: 0,
                visibility: ShaderStages::FRAGMENT,
                ty: BindingType::Buffer {
                    ty: BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: BufferSize { min: 16 },
                },
                count: None,
            }],
        });

        Self { module, bind_layouts: [cam_layout, field_layout, line_layout] }
    }

    /// Vertex-stage vertex buffer layouts for the phase-1 geometry programs.
    pub fn vertex_buffers(&self) -> Vec<VertexBufferLayout<'static>> {
        vec![
            VertexBufferLayout {
                array_stride: 28, // pos(12) + nrm(12) + grp(4)
                step_mode: VertexStepMode::Vertex,
                attributes: &wgpu::vertex_attr_array![
                    0 => Float32x3,
                    1 => Float32x3,
                    2 => Float32,
                ],
            },
            VertexBufferLayout {
                array_stride: 28, // mesh pass: pos + packed color + metric value
                step_mode: VertexStepMode::Vertex,
                attributes: &wgpu::vertex_attr_array![
                    0 => Float32x3,
                    1 => Float32x3,
                    2 => Float32,
                ],
            },
        ]
    }
}
