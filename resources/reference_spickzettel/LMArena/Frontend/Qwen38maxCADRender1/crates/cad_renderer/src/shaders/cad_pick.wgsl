struct FrameUniforms {
    view_proj: mat4x4<f32>,
    inv_view_proj: mat4x4<f32>,
    view: mat4x4<f32>,
    camera_pos: vec4<f32>,
    clip_plane: vec4<f32>,
    bg_top: vec4<f32>,
    bg_bottom: vec4<f32>,
    params: vec4<f32>,
    viewport: vec4<f32>,
};

struct ModelUniforms {
    model: mat4x4<f32>,
    normal_matrix: mat4x4<f32>,
    base_color: vec4<f32>,
    edge_color: vec4<f32>,
    material: vec4<f32>,
    ids: vec4<u32>,        // x = pick_id
};

@group(0) @binding(0)
var<uniform> frame: FrameUniforms;

@group(1) @binding(0)
var<uniform> model: ModelUniforms;

struct VsIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

struct VsOut {
    @builtin(position) clip_pos: vec4<f32>,
    @location(0) world_pos: vec3<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    let wp = model.model * vec4<f32>(in.position, 1.0);
    out.world_pos = wp.xyz;
    out.clip_pos = frame.view_proj * wp;
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) u32 {
    if frame.params.y > 0.5 {
        let plane_dist = dot(in.world_pos, frame.clip_plane.xyz) + frame.clip_plane.w;
        if plane_dist < 0.0 {
            discard;
        }
    }
    return model.ids.x;
}
