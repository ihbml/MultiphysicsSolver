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

@group(0) @binding(0)
var<uniform> frame: FrameUniforms;

struct VsIn {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
};

struct VsOut {
    @builtin(position) clip_pos: vec4<f32>,
    @location(0) color: vec4<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    var clip = frame.view_proj * vec4<f32>(in.position, 1.0);
    // Slight Reverse-Z pull toward camera so overlay lines never z-fight with geometry
    clip.z += 0.00025 * clip.w;
    out.clip_pos = clip;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    return in.color;
}
