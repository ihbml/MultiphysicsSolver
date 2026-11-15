use std::num::NonZeroU64;
use std::path::Path;
use std::sync::mpsc;

use bevy_ecs::prelude::*;
use bytemuck::{Pod, Zeroable};
use cad_core::{CadVertex, EdgeVertex};
use cad_plugin_api::{
    CadMaterial, CadPart, CadTime, CameraState, MeshHandle, MeshLibrary, PartVisibility,
    PickableId, RenderOverlayQueue, RenderSettings, SectionPlaneState, SelectionState,
    ShadingMode, ViewportStats, WorldTransform,
};
use glam::Vec4;
use wgpu::util::DeviceExt;

use crate::device::{GpuContext, RendererError};
use crate::gpu_mesh::GpuMeshCache;

#[repr(C)]
#[derive(Debug, Clone, Copy, Pod, Zeroable)]
struct FrameUniforms {
    view_proj: [[f32; 4]; 4],
    inv_view_proj: [[f32; 4]; 4],
    view: [[f32; 4]; 4],
    camera_pos: [f32; 4],
    clip_plane: [f32; 4],
    bg_top: [f32; 4],
    bg_bottom: [f32; 4],
    params: [f32; 4],   // x=shading_mode, y=clip_enabled, z=edge_bias, w=xray_alpha
    viewport: [f32; 4], // x=width, y=height, z=time, w=show_grid
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Pod, Zeroable)]
struct ModelUniforms {
    model: [[f32; 4]; 4],
    normal_matrix: [[f32; 4]; 4],
    base_color: [f32; 4],
    edge_color: [f32; 4],
    material: [f32; 4], // x=metallic, y=roughness, z=selected, w=hovered
    ids: [u32; 4],      // x=pick_id
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Pod, Zeroable)]
struct OverlayVertex {
    position: [f32; 3],
    color: [f32; 4],
}

pub struct RenderFrameOutput {
    pub draw_calls: u32,
}

struct DrawItem {
    mesh: MeshHandle,
    uniform: ModelUniforms,
}

struct ViewportTargets {
    width: u32,
    height: u32,
    msaa_samples: u32,
    color_resolved: wgpu::Texture,
    color_resolved_view: wgpu::TextureView,
    msaa_color: Option<wgpu::Texture>,
    msaa_color_view: Option<wgpu::TextureView>,
    depth_texture: wgpu::Texture,
    depth_view: wgpu::TextureView,
    pick_texture: wgpu::Texture,
    pick_view: wgpu::TextureView,
    pick_depth: wgpu::Texture,
    pick_depth_view: wgpu::TextureView,
}

impl ViewportTargets {
    fn new(
        device: &wgpu::Device,
        width: u32,
        height: u32,
        msaa_samples: u32,
        color_format: wgpu::TextureFormat,
        depth_format: wgpu::TextureFormat,
    ) -> Self {
        let width = width.max(1);
        let height = height.max(1);
        let extent = wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        };

        let color_resolved = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("cad_color_resolved"),
            size: extent,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: color_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let color_resolved_view = color_resolved.create_view(&wgpu::TextureViewDescriptor::default());

        let (msaa_color, msaa_color_view) = if msaa_samples > 1 {
            let tex = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("cad_color_msaa"),
                size: extent,
                mip_level_count: 1,
                sample_count: msaa_samples,
                dimension: wgpu::TextureDimension::D2,
                format: color_format,
                usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                view_formats: &[],
            });
            let view = tex.create_view(&wgpu::TextureViewDescriptor::default());
            (Some(tex), Some(view))
        } else {
            (None, None)
        };

        let depth_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("cad_depth_reverse_z"),
            size: extent,
            mip_level_count: 1,
            sample_count: msaa_samples,
            dimension: wgpu::TextureDimension::D2,
            format: depth_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let depth_view = depth_texture.create_view(&wgpu::TextureViewDescriptor::default());

        let pick_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("cad_pick_ids"),
            size: extent,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::R32Uint,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let pick_view = pick_texture.create_view(&wgpu::TextureViewDescriptor::default());

        let pick_depth = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("cad_pick_depth"),
            size: extent,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: depth_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        });
        let pick_depth_view = pick_depth.create_view(&wgpu::TextureViewDescriptor::default());

        Self {
            width,
            height,
            msaa_samples,
            color_resolved,
            color_resolved_view,
            msaa_color,
            msaa_color_view,
            depth_texture,
            depth_view,
            pick_texture,
            pick_view,
            pick_depth,
            pick_depth_view,
        }
    }
}

pub struct CadRenderer {
    frame_buffer: wgpu::Buffer,
    frame_bind_group: wgpu::BindGroup,

    model_bind_group_layout: wgpu::BindGroupLayout,
    model_buffer: wgpu::Buffer,
    model_bind_group: wgpu::BindGroup,
    model_stride: usize,
    model_capacity: usize,
    model_staging: Vec<u8>,

    bg_pipeline: wgpu::RenderPipeline,
    surface_pipeline: wgpu::RenderPipeline,
    xray_pipeline: wgpu::RenderPipeline,
    edges_pipeline: wgpu::RenderPipeline,
    grid_pipeline: wgpu::RenderPipeline,
    pick_pipeline: wgpu::RenderPipeline,
    overlay_depth_pipeline: wgpu::RenderPipeline,
    overlay_xray_pipeline: wgpu::RenderPipeline,

    overlay_depth_vb: wgpu::Buffer,
    overlay_depth_cap: usize,
    overlay_xray_vb: wgpu::Buffer,
    overlay_xray_cap: usize,

    pick_readback_buffer: wgpu::Buffer,

    mesh_cache: GpuMeshCache,
    targets: ViewportTargets,
    msaa_samples: u32,
    color_format: wgpu::TextureFormat,
    depth_format: wgpu::TextureFormat,
}

fn align_up(value: usize, align: usize) -> usize {
    let a = align.max(1);
    value.div_ceil(a) * a
}

impl CadRenderer {
    pub fn new(ctx: &GpuContext, initial_width: u32, initial_height: u32) -> Self {
        let device = &ctx.device;
        let color_format = ctx.color_format;
        let depth_format = ctx.depth_format;
        let msaa_samples = ctx.details.msaa_samples;

        let frame_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("cad_frame_bgl"),
                entries: &[wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: NonZeroU64::new(
                            std::mem::size_of::<FrameUniforms>() as u64,
                        ),
                    },
                    count: None,
                }],
            });

        let model_bind_group_layout =
            device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
                label: Some("cad_model_bgl"),
                entries: &[wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: true,
                        min_binding_size: NonZeroU64::new(
                            std::mem::size_of::<ModelUniforms>() as u64,
                        ),
                    },
                    count: None,
                }],
            });

        let frame_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("cad_frame_uniform"),
            contents: bytemuck::bytes_of(&FrameUniforms::zeroed()),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });

        let frame_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("cad_frame_bg"),
            layout: &frame_bind_group_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: frame_buffer.as_entire_binding(),
            }],
        });

        let min_align = device.limits().min_uniform_buffer_offset_alignment as usize;
        let model_stride = align_up(std::mem::size_of::<ModelUniforms>(), min_align);
        let model_capacity = 256;
        let model_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("cad_model_dynamic_ub"),
            size: (model_stride * model_capacity) as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let model_bind_group =
            create_model_bind_group(device, &model_bind_group_layout, &model_buffer);

        let frame_only_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("cad_frame_only_pl"),
            bind_group_layouts: &[Some(&frame_bind_group_layout)],
            immediate_size: 0,
        });

        let frame_model_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("cad_frame_model_pl"),
            bind_group_layouts: &[Some(&frame_bind_group_layout), Some(&model_bind_group_layout)],
            immediate_size: 0,
        });

        let bg_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_bg_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_bg.wgsl").into()),
        });
        let surface_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_surface_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_surface.wgsl").into()),
        });
        let edges_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_edges_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_edges.wgsl").into()),
        });
        let grid_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_grid_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_grid.wgsl").into()),
        });
        let pick_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_pick_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_pick.wgsl").into()),
        });
        let overlay_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("cad_overlay_shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shaders/cad_overlay.wgsl").into()),
        });

        let cad_vertex_layout = wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<CadVertex>() as u64,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 12,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 24,
                    shader_location: 2,
                    format: wgpu::VertexFormat::Float32x2,
                },
            ],
        };

        let edge_vertex_layout = wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<EdgeVertex>() as u64,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 12,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Uint32,
                },
            ],
        };

        let overlay_vertex_layout = wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<OverlayVertex>() as u64,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    offset: 0,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: 12,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x4,
                },
            ],
        };

        let bg_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_bg_pipeline"),
            layout: Some(&frame_only_layout),
            vertex: wgpu::VertexState {
                module: &bg_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(false),
                depth_compare: Some(wgpu::CompareFunction::Always),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: msaa_samples,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            fragment: Some(wgpu::FragmentState {
                module: &bg_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let surface_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_surface_pipeline"),
            layout: Some(&frame_model_layout),
            vertex: wgpu::VertexState {
                module: &surface_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(cad_vertex_layout.clone())],
            },
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: None, // Allow seeing solid interiors when section cut is active
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(true),
                depth_compare: Some(wgpu::CompareFunction::GreaterEqual), // Reverse-Z!
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: msaa_samples,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            fragment: Some(wgpu::FragmentState {
                module: &surface_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let xray_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_xray_pipeline"),
            layout: Some(&frame_model_layout),
            vertex: wgpu::VertexState {
                module: &surface_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(cad_vertex_layout.clone())],
            },
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(false),
                depth_compare: Some(wgpu::CompareFunction::GreaterEqual),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: msaa_samples,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            fragment: Some(wgpu::FragmentState {
                module: &surface_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let edges_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_edges_pipeline"),
            layout: Some(&frame_model_layout),
            vertex: wgpu::VertexState {
                module: &edges_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(edge_vertex_layout)],
            },
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::LineList,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(false),
                depth_compare: Some(wgpu::CompareFunction::GreaterEqual),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: msaa_samples,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            fragment: Some(wgpu::FragmentState {
                module: &edges_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let grid_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_grid_pipeline"),
            layout: Some(&frame_only_layout),
            vertex: wgpu::VertexState {
                module: &grid_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(false),
                depth_compare: Some(wgpu::CompareFunction::GreaterEqual),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: msaa_samples,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            fragment: Some(wgpu::FragmentState {
                module: &grid_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let pick_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("cad_pick_pipeline"),
            layout: Some(&frame_model_layout),
            vertex: wgpu::VertexState {
                module: &pick_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[Some(cad_vertex_layout)],
            },
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: Some(true),
                depth_compare: Some(wgpu::CompareFunction::GreaterEqual),
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: wgpu::MultisampleState::default(),
            fragment: Some(wgpu::FragmentState {
                module: &pick_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: wgpu::TextureFormat::R32Uint,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let overlay_depth_pipeline =
            device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                label: Some("cad_overlay_depth_pipeline"),
                layout: Some(&frame_only_layout),
                vertex: wgpu::VertexState {
                    module: &overlay_shader,
                    entry_point: Some("vs_main"),
                    compilation_options: Default::default(),
                    buffers: &[Some(overlay_vertex_layout.clone())],
                },
                primitive: wgpu::PrimitiveState {
                    topology: wgpu::PrimitiveTopology::LineList,
                    ..Default::default()
                },
                depth_stencil: Some(wgpu::DepthStencilState {
                    format: depth_format,
                    depth_write_enabled: Some(false),
                    depth_compare: Some(wgpu::CompareFunction::GreaterEqual),
                    stencil: Default::default(),
                    bias: Default::default(),
                }),
                multisample: wgpu::MultisampleState {
                    count: msaa_samples,
                    mask: !0,
                    alpha_to_coverage_enabled: false,
                },
                fragment: Some(wgpu::FragmentState {
                    module: &overlay_shader,
                    entry_point: Some("fs_main"),
                    compilation_options: Default::default(),
                    targets: &[Some(wgpu::ColorTargetState {
                        format: color_format,
                        blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                        write_mask: wgpu::ColorWrites::ALL,
                    })],
                }),
                multiview_mask: None,
                cache: None,
            });

        let overlay_xray_pipeline =
            device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                label: Some("cad_overlay_xray_pipeline"),
                layout: Some(&frame_only_layout),
                vertex: wgpu::VertexState {
                    module: &overlay_shader,
                    entry_point: Some("vs_main"),
                    compilation_options: Default::default(),
                    buffers: &[Some(overlay_vertex_layout)],
                },
                primitive: wgpu::PrimitiveState {
                    topology: wgpu::PrimitiveTopology::LineList,
                    ..Default::default()
                },
                depth_stencil: Some(wgpu::DepthStencilState {
                    format: depth_format,
                    depth_write_enabled: Some(false),
                    depth_compare: Some(wgpu::CompareFunction::Always),
                    stencil: Default::default(),
                    bias: Default::default(),
                }),
                multisample: wgpu::MultisampleState {
                    count: msaa_samples,
                    mask: !0,
                    alpha_to_coverage_enabled: false,
                },
                fragment: Some(wgpu::FragmentState {
                    module: &overlay_shader,
                    entry_point: Some("fs_main"),
                    compilation_options: Default::default(),
                    targets: &[Some(wgpu::ColorTargetState {
                        format: color_format,
                        blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                        write_mask: wgpu::ColorWrites::ALL,
                    })],
                }),
                multiview_mask: None,
                cache: None,
            });

        let overlay_depth_cap = 2048;
        let overlay_depth_vb = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("cad_overlay_depth_vb"),
            size: (overlay_depth_cap * std::mem::size_of::<OverlayVertex>()) as u64,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let overlay_xray_cap = 2048;
        let overlay_xray_vb = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("cad_overlay_xray_vb"),
            size: (overlay_xray_cap * std::mem::size_of::<OverlayVertex>()) as u64,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let pick_readback_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("cad_pick_readback"),
            size: 256,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        let targets = ViewportTargets::new(
            device,
            initial_width,
            initial_height,
            msaa_samples,
            color_format,
            depth_format,
        );

        Self {
            frame_buffer,
            frame_bind_group,
            model_bind_group_layout,
            model_buffer,
            model_bind_group,
            model_stride,
            model_capacity,
            model_staging: vec![0u8; model_stride * model_capacity],
            bg_pipeline,
            surface_pipeline,
            xray_pipeline,
            edges_pipeline,
            grid_pipeline,
            pick_pipeline,
            overlay_depth_pipeline,
            overlay_xray_pipeline,
            overlay_depth_vb,
            overlay_depth_cap,
            overlay_xray_vb,
            overlay_xray_cap,
            pick_readback_buffer,
            mesh_cache: GpuMeshCache::default(),
            targets,
            msaa_samples,
            color_format,
            depth_format,
        }
    }

    pub fn viewport_size(&self) -> (u32, u32) {
        (self.targets.width, self.targets.height)
    }

    pub fn color_view(&self) -> &wgpu::TextureView {
        &self.targets.color_resolved_view
    }

    /// Resize offscreen viewport & pick targets if dimensions changed. Returns true if resized.
    pub fn ensure_size(&mut self, device: &wgpu::Device, width: u32, height: u32) -> bool {
        let w = width.max(1);
        let h = height.max(1);
        if self.targets.width == w && self.targets.height == h {
            return false;
        }
        self.targets = ViewportTargets::new(
            device,
            w,
            h,
            self.msaa_samples,
            self.color_format,
            self.depth_format,
        );
        true
    }

    pub fn render_world(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        world: &mut World,
        run_pick_pass: bool,
    ) -> RenderFrameOutput {
        // 1. Sync GPU meshes
        {
            let library = world.resource::<MeshLibrary>();
            self.mesh_cache.sync_from_library(device, library);
        }

        // 2. Collect draw items from ECS
        let mut draw_items = Vec::new();
        {
            let mut q = world.query::<(
                &CadPart,
                &WorldTransform,
                &CadMaterial,
                &PickableId,
                &PartVisibility,
                &SelectionState,
            )>();
            for (part, wt, mat, pid, vis, sel) in q.iter(world) {
                if !vis.is_drawn() {
                    continue;
                }
                let normal_mat = wt.0.inverse().transpose();
                draw_items.push(DrawItem {
                    mesh: part.mesh,
                    uniform: ModelUniforms {
                        model: wt.0.to_cols_array_2d(),
                        normal_matrix: normal_mat.to_cols_array_2d(),
                        base_color: mat.0.base_color,
                        edge_color: mat.0.edge_color,
                        material: [
                            mat.0.metallic,
                            mat.0.roughness,
                            if sel.selected { 1.0 } else { 0.0 },
                            if sel.hovered { 1.0 } else { 0.0 },
                        ],
                        ids: [pid.0, 0, 0, 0],
                    },
                });
            }
        }

        // 3. Upload dynamic model uniform buffer
        if draw_items.len() > self.model_capacity {
            self.model_capacity = draw_items.len().next_power_of_two().max(64);
            self.model_buffer = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("cad_model_dynamic_ub"),
                size: (self.model_stride * self.model_capacity) as u64,
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
            self.model_bind_group = create_model_bind_group(
                device,
                &self.model_bind_group_layout,
                &self.model_buffer,
            );
            self.model_staging
                .resize(self.model_stride * self.model_capacity, 0);
        }

        for (idx, item) in draw_items.iter().enumerate() {
            let offset = idx * self.model_stride;
            let bytes = bytemuck::bytes_of(&item.uniform);
            self.model_staging[offset..offset + bytes.len()].copy_from_slice(bytes);
        }
        if !draw_items.is_empty() {
            queue.write_buffer(
                &self.model_buffer,
                0,
                &self.model_staging[..draw_items.len() * self.model_stride],
            );
        }

        // 4. Upload frame uniforms
        let aspect = self.targets.width as f32 / self.targets.height.max(1) as f32;
        let (frame_uniform, settings_snapshot) = {
            let cam = world.resource::<CameraState>();
            let settings = world.resource::<RenderSettings>().clone();
            let section = world.resource::<SectionPlaneState>();
            let time = world.resource::<CadTime>();

            let view = cam.orbit.view_matrix();
            let vp = cam.orbit.view_proj(aspect);
            let inv_vp = vp.inverse();
            let eye = cam.orbit.eye();
            let clip_vec = section
                .to_plane3()
                .map(|p| p.to_clip_vec4())
                .unwrap_or(Vec4::ZERO);

            let fu = FrameUniforms {
                view_proj: vp.to_cols_array_2d(),
                inv_view_proj: inv_vp.to_cols_array_2d(),
                view: view.to_cols_array_2d(),
                camera_pos: [eye.x, eye.y, eye.z, settings.exposure],
                clip_plane: clip_vec.to_array(),
                bg_top: [
                    settings.background_top[0],
                    settings.background_top[1],
                    settings.background_top[2],
                    1.0,
                ],
                bg_bottom: [
                    settings.background_bottom[0],
                    settings.background_bottom[1],
                    settings.background_bottom[2],
                    1.0,
                ],
                params: [
                    settings.shading_mode as u32 as f32,
                    if section.enabled { 1.0 } else { 0.0 },
                    settings.edge_bias,
                    settings.xray_alpha,
                ],
                viewport: [
                    self.targets.width as f32,
                    self.targets.height as f32,
                    time.elapsed,
                    if settings.show_grid { 1.0 } else { 0.0 },
                ],
            };
            (fu, settings)
        };
        queue.write_buffer(&self.frame_buffer, 0, bytemuck::bytes_of(&frame_uniform));

        // 5. Upload overlay line buffers
        let (depth_lines, xray_lines) = {
            let overlay = world.resource::<RenderOverlayQueue>();
            let mut d = Vec::new();
            let mut x = Vec::new();
            for line in &overlay.lines {
                let v0 = OverlayVertex {
                    position: line.start.to_array(),
                    color: line.color,
                };
                let v1 = OverlayVertex {
                    position: line.end.to_array(),
                    color: line.color,
                };
                if line.always_on_top {
                    x.push(v0);
                    x.push(v1);
                } else {
                    d.push(v0);
                    d.push(v1);
                }
            }
            (d, x)
        };

        if depth_lines.len() > self.overlay_depth_cap {
            self.overlay_depth_cap = depth_lines.len().next_power_of_two();
            self.overlay_depth_vb = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("cad_overlay_depth_vb"),
                size: (self.overlay_depth_cap * std::mem::size_of::<OverlayVertex>()) as u64,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
        }
        if !depth_lines.is_empty() {
            queue.write_buffer(&self.overlay_depth_vb, 0, bytemuck::cast_slice(&depth_lines));
        }

        if xray_lines.len() > self.overlay_xray_cap {
            self.overlay_xray_cap = xray_lines.len().next_power_of_two();
            self.overlay_xray_vb = device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("cad_overlay_xray_vb"),
                size: (self.overlay_xray_cap * std::mem::size_of::<OverlayVertex>()) as u64,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            });
        }
        if !xray_lines.is_empty() {
            queue.write_buffer(&self.overlay_xray_vb, 0, bytemuck::cast_slice(&xray_lines));
        }

        // 6. Encode GPU passes
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("cad_render_encoder"),
        });

        let mut draw_calls = 0u32;

        // Optional GPU ID Picking Pass
        if run_pick_pass {
            let mut pick_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("cad_pick_pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &self.targets.pick_view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &self.targets.pick_depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(0.0), // Reverse-Z far clear
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            pick_pass.set_pipeline(&self.pick_pipeline);
            pick_pass.set_bind_group(0, &self.frame_bind_group, &[]);
            for (idx, item) in draw_items.iter().enumerate() {
                if let Some(gpu_mesh) = self.mesh_cache.get(item.mesh) {
                    let dyn_off = (idx * self.model_stride) as u32;
                    pick_pass.set_bind_group(1, &self.model_bind_group, &[dyn_off]);
                    pick_pass.set_vertex_buffer(0, gpu_mesh.vertex_buffer.slice(..));
                    pick_pass
                        .set_index_buffer(gpu_mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                    pick_pass.draw_indexed(0..gpu_mesh.index_count, 0, 0..1);
                }
            }
        }

        // Main MSAA Color + Reverse-Z Depth Pass
        {
            let (color_view, resolve_target) =
                if let Some(msaa_view) = &self.targets.msaa_color_view {
                    (msaa_view, Some(&self.targets.color_resolved_view))
                } else {
                    (&self.targets.color_resolved_view, None)
                };

            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("cad_main_pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: color_view,
                    depth_slice: None,
                    resolve_target,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color {
                            r: 0.05,
                            g: 0.06,
                            b: 0.08,
                            a: 1.0,
                        }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &self.targets.depth_view,
                    depth_ops: Some(wgpu::Operations {
                        load: wgpu::LoadOp::Clear(0.0), // Reverse-Z far plane
                        store: wgpu::StoreOp::Store,
                    }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });

            // Background studio gradient
            pass.set_pipeline(&self.bg_pipeline);
            pass.set_bind_group(0, &self.frame_bind_group, &[]);
            pass.draw(0..3, 0..1);
            draw_calls += 1;

            let is_xray = settings_snapshot.shading_mode == ShadingMode::XRayGhost;
            let is_wire = settings_snapshot.shading_mode == ShadingMode::WireframeHidden;

            if is_xray && settings_snapshot.show_grid {
                pass.set_pipeline(&self.grid_pipeline);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                pass.draw(0..3, 0..1);
                draw_calls += 1;
            }

            if !is_wire {
                let surf_pipe = if is_xray {
                    &self.xray_pipeline
                } else {
                    &self.surface_pipeline
                };
                pass.set_pipeline(surf_pipe);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                for (idx, item) in draw_items.iter().enumerate() {
                    if let Some(gpu_mesh) = self.mesh_cache.get(item.mesh) {
                        let dyn_off = (idx * self.model_stride) as u32;
                        pass.set_bind_group(1, &self.model_bind_group, &[dyn_off]);
                        pass.set_vertex_buffer(0, gpu_mesh.vertex_buffer.slice(..));
                        pass.set_index_buffer(
                            gpu_mesh.index_buffer.slice(..),
                            wgpu::IndexFormat::Uint32,
                        );
                        pass.draw_indexed(0..gpu_mesh.index_count, 0, 0..1);
                        draw_calls += 1;
                    }
                }
            }

            if !is_xray && settings_snapshot.show_grid {
                pass.set_pipeline(&self.grid_pipeline);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                pass.draw(0..3, 0..1);
                draw_calls += 1;
            }

            // Topological crease & boundary edge overlay
            let draw_edges = settings_snapshot.show_edges
                || settings_snapshot.shading_mode == ShadingMode::ShadedWithEdges
                || is_wire;
            if draw_edges {
                pass.set_pipeline(&self.edges_pipeline);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                for (idx, item) in draw_items.iter().enumerate() {
                    if let Some(gpu_mesh) = self.mesh_cache.get(item.mesh)
                        && let Some(edge_vb) = &gpu_mesh.edge_buffer
                        && gpu_mesh.edge_vertex_count > 0
                    {
                        let dyn_off = (idx * self.model_stride) as u32;
                        pass.set_bind_group(1, &self.model_bind_group, &[dyn_off]);
                        pass.set_vertex_buffer(0, edge_vb.slice(..));
                        pass.draw(0..gpu_mesh.edge_vertex_count, 0..1);
                        draw_calls += 1;
                    }
                }
            }

            // Depth-tested debug/gizmo/selection AABB lines
            if !depth_lines.is_empty() {
                pass.set_pipeline(&self.overlay_depth_pipeline);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                pass.set_vertex_buffer(0, self.overlay_depth_vb.slice(..));
                pass.draw(0..depth_lines.len() as u32, 0..1);
                draw_calls += 1;
            }

            // Always-on-top measurement/CoM lines
            if !xray_lines.is_empty() {
                pass.set_pipeline(&self.overlay_xray_pipeline);
                pass.set_bind_group(0, &self.frame_bind_group, &[]);
                pass.set_vertex_buffer(0, self.overlay_xray_vb.slice(..));
                pass.draw(0..xray_lines.len() as u32, 0..1);
                draw_calls += 1;
            }
        }

        queue.submit(std::iter::once(encoder.finish()));

        if let Some(mut stats) = world.get_resource_mut::<ViewportStats>() {
            stats.draw_calls = draw_calls;
        }

        RenderFrameOutput { draw_calls }
    }

    /// Reads the `R32Uint` GPU picking ID at pixel `(x, y)` inside the current viewport.
    pub fn read_pick_id(&self, device: &wgpu::Device, queue: &wgpu::Queue, x: u32, y: u32) -> u32 {
        if x >= self.targets.width || y >= self.targets.height {
            return 0;
        }
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("cad_pick_copy_encoder"),
        });
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &self.targets.pick_texture,
                mip_level: 0,
                origin: wgpu::Origin3d { x, y, z: 0 },
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &self.pick_readback_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(256),
                    rows_per_image: None,
                },
            },
            wgpu::Extent3d {
                width: 1,
                height: 1,
                depth_or_array_layers: 1,
            },
        );
        queue.submit(std::iter::once(encoder.finish()));

        let slice = self.pick_readback_buffer.slice(0..4);
        let (tx, rx) = mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |res| {
            let _ = tx.send(res);
        });
        let _ = device.poll(wgpu::PollType::wait_indefinitely());

        let mut pick_id = 0u32;
        if let Ok(Ok(())) = rx.recv()
            && let Ok(data) = slice.get_mapped_range()
        {
            if data.len() >= 4 {
                pick_id = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
            }
            drop(data);
        }
        self.pick_readback_buffer.unmap();
        pick_id
    }

    /// Captures the resolved viewport color image to a tightly packed RGBA8 byte buffer.
    pub fn read_color_rgba8(&self, device: &wgpu::Device, queue: &wgpu::Queue) -> Vec<u8> {
        let w = self.targets.width;
        let h = self.targets.height;
        let unpadded_row = (w * 4) as usize;
        let padded_row = align_up(unpadded_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT as usize);

        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("cad_color_readback"),
            size: (padded_row * h as usize) as u64,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });

        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("cad_color_copy_encoder"),
        });
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &self.targets.color_resolved,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_row as u32),
                    rows_per_image: Some(h),
                },
            },
            wgpu::Extent3d {
                width: w,
                height: h,
                depth_or_array_layers: 1,
            },
        );
        queue.submit(std::iter::once(encoder.finish()));

        let slice = readback.slice(..);
        let (tx, rx) = mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |res| {
            let _ = tx.send(res);
        });
        let _ = device.poll(wgpu::PollType::wait_indefinitely());

        let mut rgba = Vec::with_capacity(unpadded_row * h as usize);
        if let Ok(Ok(())) = rx.recv()
            && let Ok(mapped) = slice.get_mapped_range()
        {
            for row in 0..h as usize {
                let start = row * padded_row;
                rgba.extend_from_slice(&mapped[start..start + unpadded_row]);
            }
            drop(mapped);
        }
        readback.unmap();
        rgba
    }

    pub fn save_png(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        path: &Path,
    ) -> Result<(), RendererError> {
        let rgba = self.read_color_rgba8(device, queue);
        image::save_buffer(
            path,
            &rgba,
            self.targets.width,
            self.targets.height,
            image::ColorType::Rgba8,
        )?;
        Ok(())
    }
}

fn create_model_bind_group(
    device: &wgpu::Device,
    layout: &wgpu::BindGroupLayout,
    buffer: &wgpu::Buffer,
) -> wgpu::BindGroup {
    device.create_bind_group(&wgpu::BindGroupDescriptor {
        label: Some("cad_model_dynamic_bg"),
        layout,
        entries: &[wgpu::BindGroupEntry {
            binding: 0,
            resource: wgpu::BindingResource::Buffer(wgpu::BufferBinding {
                buffer,
                offset: 0,
                size: NonZeroU64::new(std::mem::size_of::<ModelUniforms>() as u64),
            }),
        }],
    })
}

/// Helper to align `CameraState` aspect if needed by headless callers.
pub fn reset_world_camera_for_bounds(world: &mut World) {
    let aabb = world.resource::<ViewportStats>().scene_aabb;
    if aabb.is_valid() {
        world.resource_mut::<CameraState>().orbit.frame_aabb(&aabb);
    }
}
