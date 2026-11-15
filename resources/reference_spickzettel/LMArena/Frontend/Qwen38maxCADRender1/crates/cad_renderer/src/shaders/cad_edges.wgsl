struct FrameUniforms {
    view_proj: mat4x4<f32>,
    inv_view_proj: mat4x4<f32>,
    view: mat4x4<f32>,
    camera_pos: vec4<f32>,
    clip_plane: vec4<f32>,
    bg_top: vec4<f32>,
    bg_bottom: vec4<f32>,
    params: vec4<f32>,     // x = shading_mode, y = clip_enabled, z = edge_bias, w = xray_alpha
    viewport: vec4<f32>,
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
    @location(1) kind: u32,
};

struct VsOut {
    @builtin(position) clip_pos: vec4<f32>,
    @location(0) world_pos: vec3<f32>,
    @location(1) @interpolate(flat) kind: u32,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    let wp = model.model * vec4<f32>(in.position, 1.0);
    out.world_pos = wp.xyz;
    var clip = frame.view_proj * wp;
    // Reverse-Z bias: larger Z is closer to the camera, preventing coplanar edge z-fighting.
    clip.z += frame.params.z * clip.w;
    out.clip_pos = clip;
    out.kind = in.kind;
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    if frame.params.y > 0.5 {
        let plane_dist = dot(in.world_pos, frame.clip_plane.xyz) + frame.clip_plane.w;
        if plane_dist < 0.0 {
            discard;
        }
    }

    let mode = u32(round(frame.params.x));
    var col = model.edge_color.rgb;
    var alpha = model.edge_color.a;

    if mode == 6u {
        // WireframeHidden mode: crisp light engineering ink
        col = mix(vec3<f32>(0.68, 0.78, 0.88), model.base_color.rgb, 0.35);
        alpha = 0.92;
    } else if in.kind == 1u {
        // Open boundary edge warning highlight
        col = vec3<f32>(1.0, 0.36, 0.28);
        alpha = 0.95;
    }

    if model.material.z > 0.5 {
        col = vec3<f32>(0.22, 0.88, 1.0);
        alpha = 1.0;
    } else if model.material.w > 0.5 {
        col = vec3<f32>(1.0, 0.78, 0.22);
        alpha = 1.0;
    }

    return vec4<f32>(col, alpha);
}
