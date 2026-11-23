// R32Uint face_group_id pick pass. This is never stored long term: the integer
// is immediately resolved to a PatchTopology and re-synthesized spatially.
struct Scene {
    vp: mat4x4<f32>,
    eye: vec4<f32>,
    range_a: vec4<f32>,
    range_b: vec4<f32>,
    params: vec4<f32>,
    groups: vec4<f32>,
    slice_normal: vec4<f32>,
};
@group(0) @binding(0) var<uniform> scene: Scene;
@group(0) @binding(1) var lut: texture_1d<f32>;

struct VSOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) @interpolate(flat) group: u32,
};

@vertex
fn vs_pick(
    @location(0) position: vec3<f32>,
    @location(2) group: u32,
) -> VSOut {
    var out: VSOut;
    out.clip = scene.vp * vec4<f32>(position, 1.0);
    out.group = group;
    return out;
}

@fragment
fn fs_pick(in: VSOut) -> @location(0) u32 {
    return in.group;
}
