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

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VsOut {
    // Full-screen triangle in clip space at z = 0.0 (Reverse-Z far plane)
    let x = f32(i32(vid & 1u) * 4 - 1);
    let y = f32(i32(vid >> 1u) * 4 - 1);
    var out: VsOut;
    out.pos = vec4<f32>(x, y, 0.0, 1.0);
    out.uv = vec2<f32>(x * 0.5 + 0.5, 0.5 - y * 0.5);
    return out;
}

fn linear_to_srgb(c: vec3<f32>) -> vec3<f32> {
    let clamped = clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
    let lo = clamped * 12.92;
    let hi = 1.055 * pow(clamped, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(hi, lo, clamped <= vec3<f32>(0.0031308));
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let t = clamp(1.0 - in.uv.y, 0.0, 1.0);
    let radial = length(in.uv - vec2<f32>(0.5, 0.45));
    var col = mix(frame.bg_bottom.rgb, frame.bg_top.rgb, smoothstep(0.0, 1.0, t));
    col *= 1.0 - 0.14 * smoothstep(0.25, 0.95, radial);
    return vec4<f32>(linear_to_srgb(col), 1.0);
}
