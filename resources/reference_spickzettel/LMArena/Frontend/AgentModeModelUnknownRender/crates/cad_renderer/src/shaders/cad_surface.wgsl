struct FrameUniforms {
    view_proj: mat4x4<f32>,
    inv_view_proj: mat4x4<f32>,
    view: mat4x4<f32>,
    camera_pos: vec4<f32>,
    clip_plane: vec4<f32>,
    bg_top: vec4<f32>,
    bg_bottom: vec4<f32>,
    params: vec4<f32>,     // x = shading_mode, y = clip_enabled, z = edge_bias, w = xray_alpha
    viewport: vec4<f32>,   // x = width, y = height, z = time, w = show_grid
};

struct ModelUniforms {
    model: mat4x4<f32>,
    normal_matrix: mat4x4<f32>,
    base_color: vec4<f32>,
    edge_color: vec4<f32>,
    material: vec4<f32>,   // x = metallic, y = roughness, z = selected, w = hovered
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
    @location(1) world_normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    let wp = model.model * vec4<f32>(in.position, 1.0);
    out.world_pos = wp.xyz;
    out.world_normal = normalize((model.normal_matrix * vec4<f32>(in.normal, 0.0)).xyz);
    out.clip_pos = frame.view_proj * wp;
    out.uv = in.uv;
    return out;
}

fn linear_to_srgb(c: vec3<f32>) -> vec3<f32> {
    let clamped = clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
    let lo = clamped * 12.92;
    let hi = 1.055 * pow(clamped, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(hi, lo, clamped <= vec3<f32>(0.0031308));
}

fn aces_tonemap(x: vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn studio_pbr_lighting(N: vec3<f32>, V: vec3<f32>, albedo: vec3<f32>, metallic: f32, roughness: f32) -> vec3<f32> {
    let L1 = normalize(vec3<f32>(0.55, 0.80, 0.45));
    let L2 = normalize(vec3<f32>(-0.70, 0.30, -0.55));
    let L3 = normalize(vec3<f32>(0.10, 0.45, -0.90));

    let H1 = normalize(L1 + V);
    let H2 = normalize(L2 + V);

    let ndl1 = max(dot(N, L1), 0.0);
    let ndl2 = max(dot(N, L2), 0.0);
    let ndl3 = max(dot(N, L3), 0.0);
    let ndv = max(dot(N, V), 0.001);

    let r = clamp(roughness, 0.08, 0.95);
    let shininess = mix(160.0, 12.0, r * r);
    let spec_norm = (shininess + 8.0) / 25.1327;

    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let fresnel = f0 + (1.0 - f0) * pow(1.0 - ndv, 4.5);

    let diffuse_color = albedo * (1.0 - metallic * 0.72);
    let spec1 = fresnel * pow(max(dot(N, H1), 0.0), shininess) * spec_norm * 0.28 * ndl1;
    let spec2 = fresnel * pow(max(dot(N, H2), 0.0), shininess * 0.6) * 0.14 * ndl2;

    // Hemisphere ambient CAD studio bounce
    let hemi = mix(vec3<f32>(0.10, 0.12, 0.16), vec3<f32>(0.26, 0.30, 0.36), N.y * 0.5 + 0.5);
    let rim = fresnel * pow(1.0 - ndv, 3.0) * (0.35 + 0.45 * ndl3);

    var color = diffuse_color * (hemi + vec3<f32>(0.98, 0.96, 0.92) * ndl1 * 0.95
                                      + vec3<f32>(0.42, 0.54, 0.72) * ndl2 * 0.52
                                      + vec3<f32>(0.55, 0.62, 0.75) * ndl3 * 0.25);
    color += spec1 * vec3<f32>(1.05, 1.02, 0.96) + spec2 * vec3<f32>(0.65, 0.78, 0.95) + rim;
    return color;
}

fn gooch_shading(N: vec3<f32>, V: vec3<f32>, albedo: vec3<f32>) -> vec3<f32> {
    let L = normalize(vec3<f32>(0.55, 0.80, 0.45));
    let t = 0.5 * dot(N, L) + 0.5;
    let cool = vec3<f32>(0.10, 0.24, 0.52) + 0.35 * albedo;
    let warm = vec3<f32>(0.88, 0.68, 0.42) + 0.40 * albedo;
    let H = normalize(L + V);
    let spec = pow(max(dot(N, H), 0.0), 48.0) * 0.65;
    return mix(cool, warm, t) + vec3<f32>(spec);
}

fn matcap_curvature(N: vec3<f32>, V: vec3<f32>, albedo: vec3<f32>) -> vec3<f32> {
    let n_view = normalize((frame.view * vec4<f32>(N, 0.0)).xyz);
    let u = n_view.x * 0.5 + 0.5;
    let v = n_view.y * 0.5 + 0.5;
    let r = length(n_view.xy);

    let sky = mix(vec3<f32>(0.14, 0.17, 0.22), vec3<f32>(0.86, 0.91, 0.98), smoothstep(0.25, 0.88, v));
    let studio_strip = smoothstep(0.76, 0.82, v) * smoothstep(0.0, 0.5, 1.0 - abs(u - 0.5) * 1.4);
    let horizon = exp(-abs(n_view.y) * 9.0) * 0.22;
    let rim = pow(clamp(r, 0.0, 1.0), 3.0) * 0.25;
    let tint = mix(vec3<f32>(0.85, 0.88, 0.92), albedo, 0.35);
    return tint * (sky + vec3<f32>(studio_strip * 0.75 + horizon + rim));
}

fn zebra_continuity(N: vec3<f32>, V: vec3<f32>) -> vec3<f32> {
    let R = reflect(-V, N);
    let phase = R.y * 22.0 + R.x * 6.0;
    let fw = max(fwidth(phase), 0.001);
    let band = fract(phase);
    let stripe = smoothstep(0.5 - fw, 0.5 + fw, band) * (1.0 - smoothstep(1.0 - fw, 1.0, band));
    let dark_band = vec3<f32>(0.08, 0.10, 0.13);
    let light_band = vec3<f32>(0.95, 0.97, 1.00);
    return mix(dark_band, light_band, stripe);
}

@fragment
fn fs_main(in: VsOut, @builtin(front_facing) front_facing: bool) -> @location(0) vec4<f32> {
    var plane_dist = 1000.0;
    if frame.params.y > 0.5 {
        plane_dist = dot(in.world_pos, frame.clip_plane.xyz) + frame.clip_plane.w;
        if plane_dist < 0.0 {
            discard;
        }
    }

    var N = normalize(in.world_normal);
    if !front_facing {
        N = -N;
    }
    let V = normalize(frame.camera_pos.xyz - in.world_pos);
    let mode = u32(round(frame.params.x));

    var color = vec3<f32>(0.5);
    var alpha = 1.0;

    if mode == 2u {
        color = gooch_shading(N, V, model.base_color.rgb);
    } else if mode == 3u {
        color = matcap_curvature(N, V, model.base_color.rgb);
    } else if mode == 4u {
        color = zebra_continuity(N, V);
    } else if mode == 5u {
        let fresnel = pow(1.0 - abs(dot(N, V)), 2.0);
        let base = mix(model.base_color.rgb, vec3<f32>(0.35, 0.80, 1.0), fresnel * 0.75);
        color = base * (0.45 + 0.85 * fresnel);
        alpha = clamp(frame.params.w * 0.45 + fresnel * 0.78, 0.08, 0.92);
    } else {
        color = studio_pbr_lighting(N, V, model.base_color.rgb, model.material.x, model.material.y);
    }

    // Section interior backface cross-hatch tint for crisp solid cut readability
    if !front_facing && frame.params.y > 0.5 {
        let hatch_coord = (in.world_pos.x + in.world_pos.y + in.world_pos.z) * 18.0;
        let hatch = select(0.75, 1.0, fract(hatch_coord) > 0.5);
        color = mix(color * 0.55, vec3<f32>(0.55, 0.25, 0.08) * hatch, 0.58);
    }

    // Glowing section cut rim highlight
    if frame.params.y > 0.5 && plane_dist < 0.025 {
        let rim_t = 1.0 - smoothstep(0.0, 0.025, plane_dist);
        color = mix(color, vec3<f32>(1.0, 0.52, 0.10), rim_t * 0.90);
    }

    // Selection & hover highlights
    let fresnel_sel = pow(1.0 - max(dot(N, V), 0.0), 2.5);
    if model.material.z > 0.5 {
        color = mix(color, vec3<f32>(0.15, 0.80, 1.0), 0.18 + 0.42 * fresnel_sel);
    } else if model.material.w > 0.5 {
        color = mix(color, vec3<f32>(1.0, 0.76, 0.20), 0.14 + 0.30 * fresnel_sel);
    }

    color = aces_tonemap(color * frame.camera_pos.w);
    return vec4<f32>(linear_to_srgb(color), alpha);
}
