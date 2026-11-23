// GPU-instanced velocity ribbons: one storage segment creates one view-facing quad.
struct CameraUniform { view_projection: mat4x4<f32>, origin_high: vec4<f32>, origin_low: vec4<f32>, }
struct FlowSettings { camera_right: vec4<f32>, time: f32, ribbon_width: f32, pulse_amplitude: f32, padding: f32, }
struct RibbonSegment { start: vec4<f32>, end: vec4<f32>, color: vec4<f32>, }

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(0) @binding(1) var<uniform> flow: FlowSettings;
@group(0) @binding(2) var<storage, read> segments: array<RibbonSegment>;

struct RibbonOut {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) distance_along: f32,
}

@vertex
fn vs_ribbon(@builtin(vertex_index) vertex_index: u32, @builtin(instance_index) instance_index: u32) -> RibbonOut {
    let segment = segments[instance_index];
    let corners = array<vec2<f32>, 4>(
        vec2<f32>(0.0, -1.0), vec2<f32>(1.0, -1.0),
        vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 1.0),
    );
    let corner = corners[vertex_index];
    let start = segment.start.xyz;
    let end = segment.end.xyz;
    let tangent = normalize(end - start);
    let side = normalize(cross(tangent, flow.camera_right.xyz));
    let speed = max(segment.start.w, 0.0);
    let phase = flow.time * (0.8 + speed * 0.12) + segment.end.w;
    let center = mix(start, end, corner.x);
    let pulse = 1.0 + flow.pulse_amplitude * sin(phase + corner.x * 6.28318);
    let world_position = center + side * corner.y * flow.ribbon_width * pulse;
    var output: RibbonOut;
    output.clip_position = camera.view_projection * vec4<f32>(world_position, 1.0);
    output.color = segment.color;
    output.distance_along = corner.x;
    return output;
}

@fragment
fn fs_ribbon(input: RibbonOut) -> @location(0) vec4<f32> {
    let edge = 1.0 - smoothstep(0.75, 1.0, abs(input.distance_along - 0.5) * 2.0);
    return vec4<f32>(input.color.rgb, input.color.a * edge);
}