// Gizmos and domain cage — unlit coloured lines / points.
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
    @location(0) color: vec4<f32>,
};

@vertex
fn vs_line(
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
) -> VSOut {
    var out: VSOut;
    out.clip = scene.vp * vec4<f32>(position, 1.0);
    out.color = color;
    return out;
}

@fragment
fn fs_line(in: VSOut) -> @location(0) vec4<f32> {
    return in.color;
}
