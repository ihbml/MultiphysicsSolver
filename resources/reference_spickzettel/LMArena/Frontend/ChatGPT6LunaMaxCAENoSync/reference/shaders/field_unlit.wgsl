// Strictly unlit scientific field pass. Keep output linear and do not tone-map.
struct CameraUniform {
    view_projection: mat4x4<f32>,
    origin_high: vec4<f32>,
    origin_low: vec4<f32>,
}

struct MeshStyle {
    mode: u32,
    metric: u32,
    palette: u32,
    isolate: u32,
    threshold: f32,
    contour_bands: f32,
    selected_face_group: u32,
    edl_strength: f32,
    viewport_size: vec2<f32>,
    padding: vec2<f32>,
    slice_plane: vec4<f32>,
    slice_enabled: f32,
    padding_tail_0: f32,
    padding_tail_1: f32,
    padding_tail_2: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> style: MeshStyle;

const FLAG_FOLDED: u32 = 1u;
const FLAG_NAN: u32 = 2u;

struct FieldVertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) scalar: f32,
    @location(3) metrics: vec4<f32>,
    @location(4) face_group_id: u32,
    @location(5) flags: u32,
}

struct FieldVertexOut {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) scalar: f32,
    @location(1) @interpolate(flat) metrics: vec4<f32>,
    @location(2) @interpolate(flat) face_group_id: u32,
    @location(3) @interpolate(flat) flags: u32,
    @location(4) relative_position: vec3<f32>,
}

@vertex
fn vs_field(input: FieldVertexIn) -> FieldVertexOut {
    var output: FieldVertexOut;
    output.clip_position = camera.view_projection * vec4<f32>(input.position, 1.0);
    output.scalar = input.scalar;
    output.metrics = input.metrics;
    output.face_group_id = input.face_group_id;
    output.flags = input.flags;
    output.relative_position = input.position;
    return output;
}

fn lab_to_linear_rgb(lab: vec3<f32>) -> vec3<f32> {
    let l_ = lab.x + 0.39633778 * lab.y + 0.21580376 * lab.z;
    let m_ = lab.x - 0.10556135 * lab.y - 0.06385417 * lab.z;
    let s_ = lab.x - 0.08948418 * lab.y - 1.29148555 * lab.z;
    let l = l_ * l_ * l_;
    let m = m_ * m_ * m_;
    let s = s_ * s_ * s_;
    return max(vec3<f32>(
        4.07674166 * l - 3.30771159 * m + 0.23096993 * s,
        -1.26843800 * l + 2.60975740 * m - 0.34131940 * s,
        -0.00419609 * l - 0.70341861 * m + 1.70761470 * s,
    ), vec3<f32>(0.0));
}

fn ramp_oklab(t_in: f32, palette: u32) -> vec3<f32> {
    let t = clamp(t_in, 0.0, 1.0);
    var c0 = vec3<f32>(0.32, -0.05, -0.14);
    var c1 = vec3<f32>(0.51, -0.06, -0.08);
    var c2 = vec3<f32>(0.68, -0.08, 0.02);
    var c3 = vec3<f32>(0.83, -0.06, 0.11);
    var c4 = vec3<f32>(0.94, -0.02, 0.17);
    if (palette == 0u) { // Dark Ember
        c0 = vec3<f32>(0.13, 0.035, -0.025); c1 = vec3<f32>(0.32, 0.08, 0.035);
        c2 = vec3<f32>(0.55, 0.12, 0.09); c3 = vec3<f32>(0.76, 0.09, 0.13); c4 = vec3<f32>(0.94, 0.015, 0.12);
    } else if (palette == 1u) { // Fire
        c0 = vec3<f32>(0.16, 0.075, 0.035); c1 = vec3<f32>(0.42, 0.13, 0.10);
        c2 = vec3<f32>(0.66, 0.14, 0.15); c3 = vec3<f32>(0.84, 0.055, 0.14); c4 = vec3<f32>(0.97, -0.015, 0.08);
    } else if (palette == 2u) { // Moreland cool-warm
        c0 = vec3<f32>(0.43, -0.045, -0.16); c1 = vec3<f32>(0.61, -0.045, -0.12);
        c2 = vec3<f32>(0.86, 0.0, 0.005); c3 = vec3<f32>(0.70, 0.13, 0.09); c4 = vec3<f32>(0.49, 0.18, 0.14);
    } else if (palette == 3u) { // Traffic light
        c0 = vec3<f32>(0.57, -0.11, 0.09); c1 = vec3<f32>(0.75, -0.12, 0.12);
        c2 = vec3<f32>(0.88, -0.03, 0.17); c3 = vec3<f32>(0.88, 0.07, 0.16); c4 = vec3<f32>(0.63, 0.20, 0.15);
    }
    var lab: vec3<f32>;
    if (t < 0.25) { lab = mix(c0, c1, t * 4.0); }
    else if (t < 0.5) { lab = mix(c1, c2, (t - 0.25) * 4.0); }
    else if (t < 0.75) { lab = mix(c2, c3, (t - 0.5) * 4.0); }
    else { lab = mix(c3, c4, (t - 0.75) * 4.0); }
    return clamp(lab_to_linear_rgb(lab), vec3<f32>(0.0), vec3<f32>(1.0));
}

@fragment
fn fs_field(input: FieldVertexOut) -> @location(0) vec4<f32> {
    if (style.slice_enabled > 0.5) {
        let slice_distance = dot(vec4<f32>(input.relative_position, 1.0), style.slice_plane);
        if (slice_distance < -0.0005) { discard; }
        if (abs(slice_distance) < 0.0008) { return vec4<f32>(0.72, 0.96, 0.0, 1.0); }
    }
    var value = input.scalar;
    if (style.mode == 1u) { value = input.metrics[min(style.metric, 3u)]; }
    if (style.contour_bands > 1.0) {
        value = floor(value * style.contour_bands + 0.5) / style.contour_bands;
    }
    var color = ramp_oklab(value, style.palette);
    if (style.mode == 1u && style.isolate == 1u && value < style.threshold) {
        color = mix(color, vec3<f32>(0.035, 0.04, 0.05), 0.84);
    }
    if ((input.flags & (FLAG_FOLDED | FLAG_NAN)) != 0u) {
        color = vec3<f32>(0.94, 0.07, 0.19);
    }
    if (input.face_group_id == style.selected_face_group && style.selected_face_group != 0u) {
        color = mix(color, vec3<f32>(0.72, 0.96, 0.0), 0.5);
    }
    // No lighting, exposure, tone mapping, or gamma curve is applied here.
    return vec4<f32>(color, 1.0);
}