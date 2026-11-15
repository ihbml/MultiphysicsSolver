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
    @builtin(position) clip_pos: vec4<f32>,
    @location(0) near_pt: vec3<f32>,
    @location(1) far_pt: vec3<f32>,
};

fn unproject(ndc_xy: vec2<f32>, ndc_z: f32) -> vec3<f32> {
    let h = frame.inv_view_proj * vec4<f32>(ndc_xy, ndc_z, 1.0);
    return h.xyz / h.w;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VsOut {
    let x = f32(i32(vid & 1u) * 4 - 1);
    let y = f32(i32(vid >> 1u) * 4 - 1);
    var out: VsOut;
    out.clip_pos = vec4<f32>(x, y, 0.5, 1.0);
    out.near_pt = unproject(vec2<f32>(x, y), 1.0); // Reverse-Z near = 1.0
    out.far_pt = unproject(vec2<f32>(x, y), 0.0);  // Reverse-Z far = 0.0
    return out;
}

struct FragOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};

fn grid_mask(coord: vec2<f32>, scale: f32) -> f32 {
    let p = coord * scale;
    let deriv = max(fwidth(p), vec2<f32>(1e-5));
    let cell = abs(fract(p - 0.5) - 0.5) / deriv;
    let line = min(cell.x, cell.y);
    return 1.0 - min(line, 1.0);
}

@fragment
fn fs_main(in: VsOut) -> FragOut {
    let denom = in.far_pt.y - in.near_pt.y;
    let safe_denom = select(denom, 1e-6, abs(denom) < 1e-6);
    let t = -in.near_pt.y / safe_denom;
    let world_p = mix(in.near_pt, in.far_pt, clamp(t, 0.0, 1.0));
    let coord = world_p.xz;

    // Compute derivatives before uniform/non-uniform discard
    let minor = grid_mask(coord, 2.0); // 0.5 unit pitch
    let major = grid_mask(coord, 0.5); // 2.0 unit pitch
    let fw_coord = max(fwidth(coord), vec2<f32>(1e-4));
    let axis_x = 1.0 - min(abs(coord.y) / (fw_coord.y * 1.4), 1.0);
    let axis_z = 1.0 - min(abs(coord.x) / (fw_coord.x * 1.4), 1.0);

    if abs(denom) < 1e-6 || t <= 0.0 || t >= 1.0 {
        discard;
    }

    let dist_from_origin = length(coord);
    let cam_dist = max(length(frame.camera_pos.xyz), 2.0);
    let max_radius = max(cam_dist * 4.5, 24.0);
    let radial_fade = 1.0 - smoothstep(max_radius * 0.28, max_radius, dist_from_origin);

    let ray_dir = normalize(in.far_pt - in.near_pt);
    let grazing_fade = smoothstep(0.015, 0.14, abs(ray_dir.y));

    var col = vec3<f32>(0.24, 0.29, 0.36) * minor * 0.38;
    col = mix(col, vec3<f32>(0.36, 0.44, 0.56), major * 0.65);
    var alpha = max(minor * 0.25, major * 0.52);

    if axis_x > 0.01 {
        col = mix(col, vec3<f32>(0.88, 0.28, 0.28), axis_x);
        alpha = max(alpha, axis_x * 0.85);
    }
    if axis_z > 0.01 {
        col = mix(col, vec3<f32>(0.28, 0.56, 0.92), axis_z);
        alpha = max(alpha, axis_z * 0.85);
    }

    alpha *= radial_fade * grazing_fade;
    if alpha < 0.004 {
        discard;
    }

    let clip = frame.view_proj * vec4<f32>(world_p, 1.0);
    let depth = clamp(clip.z / clip.w, 0.0, 1.0);

    var out: FragOut;
    out.color = vec4<f32>(col, alpha);
    out.depth = depth;
    return out;
}
