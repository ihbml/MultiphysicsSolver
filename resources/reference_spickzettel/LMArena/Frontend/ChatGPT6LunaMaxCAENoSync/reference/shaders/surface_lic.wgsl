// Screen-space Surface-LIC for wall shear / separation tracing.
// The input vector texture stores tangent-plane shear (tau_u, tau_v).
struct LicSettings { size: vec2<u32>, steps: u32, step_length: f32, noise_scale: f32, contrast: f32, padding: f32, }
@group(0) @binding(0) var wall_shear: texture_2d<f32>;
@group(0) @binding(1) var noise_texture: texture_2d<f32>;
@group(0) @binding(2) var lic_output: texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var<uniform> settings: LicSettings;

fn bounded_pixel(pixel: vec2<i32>) -> vec2<i32> {
    return clamp(pixel, vec2<i32>(0, 0), vec2<i32>(settings.size) - vec2<i32>(1, 1));
}

fn sample_shear(pixel: vec2<i32>) -> vec2<f32> {
    return textureLoad(wall_shear, bounded_pixel(pixel), 0).xy;
}

fn integrate_direction(origin: vec2<f32>, direction_sign: f32) -> f32 {
    var cursor = origin;
    var accumulated = 0.0;
    var weight_sum = 0.0;
    for (var step = 0u; step < settings.steps; step += 1u) {
        let shear = sample_shear(vec2<i32>(cursor));
        let magnitude = length(shear);
        if (magnitude < 1e-8) { break; }
        let tangent = shear / magnitude * direction_sign;
        let distance = f32(step) * settings.step_length;
        cursor += tangent * settings.step_length;
        let sample_pixel = bounded_pixel(vec2<i32>(cursor * settings.noise_scale));
        let noise = textureLoad(noise_texture, sample_pixel, 0).r;
        let kernel = exp(-distance * 0.035);
        accumulated += noise * kernel;
        weight_sum += kernel;
    }
    return select(0.0, accumulated / weight_sum, weight_sum > 0.0);
}

@compute @workgroup_size(8, 8, 1)
fn cs_surface_lic(@builtin(global_invocation_id) invocation: vec3<u32>) {
    if (any(invocation.xy >= settings.size)) { return; }
    let origin = vec2<f32>(invocation.xy) + vec2<f32>(0.5, 0.5);
    let forward = integrate_direction(origin, 1.0);
    let backward = integrate_direction(origin, -1.0);
    let lic_value = clamp((forward + backward) * 0.5 * settings.contrast, 0.0, 1.0);
    let shear = sample_shear(vec2<i32>(invocation.xy));
    let strength = clamp(length(shear), 0.0, 1.0);
    textureStore(lic_output, vec2<i32>(invocation.xy), vec4<f32>(lic_value, strength, 1.0 - lic_value, 1.0));
}