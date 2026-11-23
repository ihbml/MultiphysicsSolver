// AEROFORGE Cockpit — surface pass (Reverse-Z, Oklab 1D LUT, spatial highlight)
struct Scene {
    vp: mat4x4<f32>,
    eye: vec4<f32>,
    range_a: vec4<f32>,
    range_b: vec4<f32>,
    params: vec4<f32>,      // delta half span, slice pos, mode, slice enabled
    groups: vec4<f32>,      // highlight group, conflict group, -, -
    slice_normal: vec4<f32>,
};

@group(0) @binding(0) var<uniform> scene: Scene;
@group(0) @binding(1) var lut: texture_1d<f32>;

struct VSIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) group: u32,
    @location(3) scalar_a: f32,
    @location(4) scalar_b: f32,
    @location(5) conflict: f32,
};

struct VSOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) world: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) @interpolate(flat) group: u32,
    @location(3) scalar_a: f32,
    @location(4) scalar_b: f32,
    @location(5) conflict: f32,
};

@vertex
fn vs_surface(in: VSIn) -> VSOut {
    var out: VSOut;
    out.world = in.position;
    out.normal = in.normal;
    out.group = in.group;
    out.scalar_a = in.scalar_a;
    out.scalar_b = in.scalar_b;
    out.conflict = in.conflict;
    out.clip = scene.vp * vec4<f32>(in.position, 1.0);
    return out;
}

fn lut_color(t: f32) -> vec3<f32> {
    let index = u32(clamp(t, 0.0, 1.0) * 255.0);
    return textureLoad(lut, i32(index), 0).rgb;
}

@fragment
fn fs_surface(in: VSOut) -> @location(0) vec4<f32> {
    if (scene.params.w > 0.5 && dot(in.world, scene.slice_normal.xyz) > scene.params.y) {
        discard;
    }

    var n = normalize(in.normal);
    if (!front_facing()) { n = -n; }

    let a_norm = clamp((in.scalar_a - scene.range_a.x) / max(0.0001, scene.range_a.y - scene.range_a.x), 0.0, 1.0);
    let b_norm = clamp((in.scalar_b - scene.range_b.x) / max(0.0001, scene.range_b.y - scene.range_b.x), 0.0, 1.0);
    let delta = clamp(0.5 + (in.scalar_b - in.scalar_a) / max(0.0001, 2.0 * scene.params.x), 0.0, 1.0);

    var albedo = lut_color(a_norm);
    if (scene.params.z > 1.5) { albedo = lut_color(delta); }
    else if (scene.params.z > 0.5) { albedo = lut_color(b_norm); }

    let light = normalize(vec3<f32>(0.42, 0.80, 0.55));
    let view = normalize(scene.eye.xyz - in.world);
    let wrapped = clamp(dot(n, light) * 0.5 + 0.5, 0.0, 1.0);
    let diffuse = 0.24 + 0.70 * pow(wrapped, 1.18);
    let rim = pow(1.0 - max(dot(n, view), 0.0), 3.0);
    let spec = pow(max(dot(reflect(-light, n), view), 0.0), 42.0);

    var color = albedo * diffuse
        + vec3<f32>(0.30, 0.47, 0.95) * rim * 0.30
        + vec3<f32>(1.0) * spec * 0.16;
    color = mix(vec3<f32>(0.035, 0.043, 0.055), color, 0.94);

    if (abs(f32(in.group) - scene.groups.x) < 0.5) {
        color = mix(color, vec3<f32>(0.0, 0.29, 0.99), 0.44);
    }
    if (in.conflict > 0.5 || abs(f32(in.group) - scene.groups.y) < 0.5) {
        color = mix(color, vec3<f32>(0.937, 0.137, 0.235), 0.62);
    }
    return vec4<f32>(color, 1.0);
}
