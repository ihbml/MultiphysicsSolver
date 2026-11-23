// Reverse-Z mesh inspection pass. The depth prepass supplies scene_depth to EDL.
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
@group(2) @binding(0) var scene_depth: texture_depth_2d;

const FLAG_FOLDED: u32 = 1u;
const FLAG_NAN: u32 = 2u;
const FLAG_ABOVE_THRESHOLD: u32 = 4u;

struct MeshVertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) scalar: f32,
    @location(3) metrics: vec4<f32>,
    @location(4) face_group_id: u32,
    @location(5) flags: u32,
}

struct MeshVertexOut {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) view_position: vec3<f32>,
    @location(1) view_normal: vec3<f32>,
    @location(2) scalar: f32,
    @location(3) @interpolate(flat) metrics: vec4<f32>,
    @location(4) @interpolate(flat) face_group_id: u32,
    @location(5) @interpolate(flat) flags: u32,
    @location(6) relative_position: vec3<f32>,
}

@vertex
fn vs_main(input: MeshVertexIn) -> MeshVertexOut {
    var output: MeshVertexOut;
    let relative = vec4<f32>(input.position, 1.0);
    output.clip_position = camera.view_projection * relative;
    output.view_position = input.position;
    output.view_normal = normalize(input.normal);
    output.scalar = input.scalar;
    output.metrics = input.metrics;
    output.face_group_id = input.face_group_id;
    output.flags = input.flags;
    output.relative_position = input.position;
    return output;
}

struct DepthVertexOut { @builtin(position) clip_position: vec4<f32>, }

@vertex
fn vs_depth(input: MeshVertexIn) -> DepthVertexOut {
    var output: DepthVertexOut;
    output.clip_position = camera.view_projection * vec4<f32>(input.position, 1.0);
    return output;
}

struct PickVertexOut {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) @interpolate(flat) face_group_id: u32,
}

@vertex
fn vs_pick(input: MeshVertexIn) -> PickVertexOut {
    var output: PickVertexOut;
    output.clip_position = camera.view_projection * vec4<f32>(input.position, 1.0);
    output.face_group_id = input.face_group_id;
    return output;
}

@fragment
fn fs_pick(input: PickVertexOut) -> @location(0) u32 {
    return input.face_group_id;
}

fn edl_shade(clip_position: vec4<f32>) -> f32 {
    let dimensions = vec2<i32>(textureDimensions(scene_depth));
    let pixel = clamp(vec2<i32>(clip_position.xy), vec2<i32>(1, 1), dimensions - vec2<i32>(2, 2));
    let center_depth = textureLoad(scene_depth, pixel, 0);
    let offsets = array<vec2<i32>, 8>(
        vec2<i32>(-1, -1), vec2<i32>(0, -1), vec2<i32>(1, -1), vec2<i32>(-1, 0),
        vec2<i32>(1, 0), vec2<i32>(-1, 1), vec2<i32>(0, 1), vec2<i32>(1, 1),
    );
    var relief = 0.0;
    for (var index = 0u; index < 8u; index += 1u) {
        let neighbor_depth = textureLoad(scene_depth, pixel + offsets[index], 0);
        relief += max(neighbor_depth - center_depth, 0.0);
    }
    return exp(-relief * style.edl_strength * 32.0);
}

fn quality_ramp(value: f32) -> vec3<f32> {
    let t = clamp(value, 0.0, 1.0);
    let cold = vec3<f32>(0.12, 0.20, 0.31);
    let mid = vec3<f32>(0.45, 0.63, 0.18);
    let hot = vec3<f32>(0.98, 0.59, 0.08);
    let bad = vec3<f32>(0.94, 0.07, 0.19);
    if (t < 0.55) { return mix(cold, mid, t / 0.55); }
    if (t < 0.82) { return mix(mid, hot, (t - 0.55) / 0.27); }
    return mix(hot, bad, (t - 0.82) / 0.18);
}

@fragment
fn fs_inspect(input: MeshVertexOut) -> @location(0) vec4<f32> {
    if (style.slice_enabled > 0.5) {
        let slice_distance = dot(vec4<f32>(input.relative_position, 1.0), style.slice_plane);
        if (slice_distance < -0.0005) { discard; }
        if (abs(slice_distance) < 0.0008) { return vec4<f32>(0.72, 0.96, 0.0, 1.0); }
    }

    if (style.mode == 1u) {
        var cell_value = input.metrics[min(style.metric, 3u)];
        if (style.contour_bands > 1.0) {
            cell_value = floor(cell_value * style.contour_bands + 0.5) / style.contour_bands;
        }
        var quality_color = quality_ramp(cell_value);
        if (style.isolate == 1u && cell_value < style.threshold) {
            quality_color = mix(quality_color, vec3<f32>(0.035, 0.04, 0.05), 0.84);
        }
        if ((input.flags & (FLAG_FOLDED | FLAG_NAN)) != 0u) {
            quality_color = vec3<f32>(0.94, 0.07, 0.19);
        }
        return vec4<f32>(quality_color, 1.0);
    }

    let normal = normalize(input.view_normal);
    let view_direction = normalize(-input.view_position);
    let key = normalize(vec3<f32>(-0.34, 0.71, 0.83));
    let diffuse = 0.32 + 0.58 * max(dot(normal, key), 0.0);
    let rim = pow(1.0 - max(dot(normal, view_direction), 0.0), 2.0) * 0.2;
    let cavity = edl_shade(input.clip_position);
    var color = vec3<f32>(0.31, 0.39, 0.49) * diffuse * cavity + rim * vec3<f32>(0.17, 0.48, 0.92);
    if (input.face_group_id == style.selected_face_group && style.selected_face_group != 0u) {
        color = mix(color, vec3<f32>(0.72, 0.96, 0.0), 0.57);
    }
    if ((input.flags & (FLAG_FOLDED | FLAG_NAN)) != 0u) {
        color = vec3<f32>(0.94, 0.07, 0.19);
    }
    return vec4<f32>(color, 1.0);
}