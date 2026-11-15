use std::f32::consts::{PI, TAU};

use glam::{Mat4, Quat, Vec3};
use serde::{Deserialize, Serialize};

use crate::mesh::{CadMaterialData, CadMesh, CadVertex, CreaseConfig};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum DemoAssemblyKind {
    SpindleHousing,
    PlanetaryGearbox,
    HydraulicManifold,
}

impl DemoAssemblyKind {
    pub fn label(&self) -> &'static str {
        match self {
            Self::SpindleHousing => "CNC Spindle Housing",
            Self::PlanetaryGearbox => "Planetary Spur Gearbox",
            Self::HydraulicManifold => "Hydraulic Manifold Block",
        }
    }

    pub const ALL: [Self; 3] = [
        Self::SpindleHousing,
        Self::PlanetaryGearbox,
        Self::HydraulicManifold,
    ];
}

#[derive(Debug, Clone)]
pub struct AssemblyPartSpec {
    pub name: String,
    pub group: String,
    pub mesh: CadMesh,
    pub transform: Mat4,
    pub explode_offset: Vec3,
    pub material: CadMaterialData,
}

fn push_quad(
    verts: &mut Vec<CadVertex>,
    indices: &mut Vec<u32>,
    p0: Vec3,
    p1: Vec3,
    p2: Vec3,
    p3: Vec3,
    n: Vec3,
) {
    let base = verts.len() as u32;
    verts.push(CadVertex::new(p0, n, [0.0, 0.0]));
    verts.push(CadVertex::new(p1, n, [1.0, 0.0]));
    verts.push(CadVertex::new(p2, n, [1.0, 1.0]));
    verts.push(CadVertex::new(p3, n, [0.0, 1.0]));
    indices.extend_from_slice(&[base, base + 1, base + 2, base, base + 2, base + 3]);
}

pub fn make_box(name: impl Into<String>, size: Vec3) -> CadMesh {
    let h = size * 0.5;
    let mut verts = Vec::with_capacity(24);
    let mut indices = Vec::with_capacity(36);

    // +Y
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(-h.x, h.y, h.z),
        Vec3::new(h.x, h.y, h.z),
        Vec3::new(h.x, h.y, -h.z),
        Vec3::new(-h.x, h.y, -h.z),
        Vec3::Y,
    );
    // -Y
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(-h.x, -h.y, -h.z),
        Vec3::new(h.x, -h.y, -h.z),
        Vec3::new(h.x, -h.y, h.z),
        Vec3::new(-h.x, -h.y, h.z),
        Vec3::NEG_Y,
    );
    // +X
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(h.x, -h.y, h.z),
        Vec3::new(h.x, -h.y, -h.z),
        Vec3::new(h.x, h.y, -h.z),
        Vec3::new(h.x, h.y, h.z),
        Vec3::X,
    );
    // -X
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(-h.x, -h.y, -h.z),
        Vec3::new(-h.x, -h.y, h.z),
        Vec3::new(-h.x, h.y, h.z),
        Vec3::new(-h.x, h.y, -h.z),
        Vec3::NEG_X,
    );
    // +Z
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(-h.x, -h.y, h.z),
        Vec3::new(h.x, -h.y, h.z),
        Vec3::new(h.x, h.y, h.z),
        Vec3::new(-h.x, h.y, h.z),
        Vec3::Z,
    );
    // -Z
    push_quad(
        &mut verts,
        &mut indices,
        Vec3::new(h.x, -h.y, -h.z),
        Vec3::new(-h.x, -h.y, -h.z),
        Vec3::new(-h.x, h.y, -h.z),
        Vec3::new(h.x, h.y, -h.z),
        Vec3::NEG_Z,
    );

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

pub fn make_cylinder(name: impl Into<String>, radius: f32, height: f32, segments: u32) -> CadMesh {
    let segs = segments.max(6);
    let hy = height * 0.5;
    let r = radius.max(1e-4);

    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Side wall
    let side_base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        let n = Vec3::new(c, 0.0, s);
        verts.push(CadVertex::new(Vec3::new(r * c, -hy, r * s), n, [u, 0.0]));
        verts.push(CadVertex::new(Vec3::new(r * c, hy, r * s), n, [u, 1.0]));
    }
    for i in 0..segs {
        let i0 = side_base + i * 2;
        let i1 = i0 + 1;
        let i2 = i0 + 2;
        let i3 = i0 + 3;
        // Outward CCW viewed from +normal
        indices.extend_from_slice(&[i0, i1, i3, i0, i3, i2]);
    }

    // Top cap (+Y)
    let top_center = verts.len() as u32;
    verts.push(CadVertex::new(Vec3::new(0.0, hy, 0.0), Vec3::Y, [0.5, 0.5]));
    for i in 0..=segs {
        let a = (i as f32 / segs as f32) * TAU;
        let (s, c) = a.sin_cos();
        verts.push(CadVertex::new(
            Vec3::new(r * c, hy, r * s),
            Vec3::Y,
            [0.5 + 0.5 * c, 0.5 + 0.5 * s],
        ));
    }
    for i in 0..segs {
        indices.extend_from_slice(&[top_center, top_center + 1 + i + 1, top_center + 1 + i]);
    }

    // Bottom cap (-Y)
    let bot_center = verts.len() as u32;
    verts.push(CadVertex::new(
        Vec3::new(0.0, -hy, 0.0),
        Vec3::NEG_Y,
        [0.5, 0.5],
    ));
    for i in 0..=segs {
        let a = (i as f32 / segs as f32) * TAU;
        let (s, c) = a.sin_cos();
        verts.push(CadVertex::new(
            Vec3::new(r * c, -hy, r * s),
            Vec3::NEG_Y,
            [0.5 + 0.5 * c, 0.5 + 0.5 * s],
        ));
    }
    for i in 0..segs {
        indices.extend_from_slice(&[bot_center, bot_center + 1 + i, bot_center + 1 + i + 1]);
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// Hollow cylindrical bushing / sleeve / ring along Y axis.
pub fn make_tube(
    name: impl Into<String>,
    outer_r: f32,
    inner_r: f32,
    height: f32,
    segments: u32,
) -> CadMesh {
    let segs = segments.max(8);
    let r_out = outer_r.max(inner_r + 0.01);
    let r_in = inner_r.max(0.005);
    let hy = height * 0.5;

    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Outer wall
    let outer_base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        let n = Vec3::new(c, 0.0, s);
        verts.push(CadVertex::new(Vec3::new(r_out * c, -hy, r_out * s), n, [u, 0.0]));
        verts.push(CadVertex::new(Vec3::new(r_out * c, hy, r_out * s), n, [u, 1.0]));
    }
    for i in 0..segs {
        let i0 = outer_base + i * 2;
        indices.extend_from_slice(&[i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]);
    }

    // Inner bore wall (normals pointing inward toward axis)
    let inner_base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        let n = Vec3::new(-c, 0.0, -s);
        verts.push(CadVertex::new(Vec3::new(r_in * c, -hy, r_in * s), n, [u, 0.0]));
        verts.push(CadVertex::new(Vec3::new(r_in * c, hy, r_in * s), n, [u, 1.0]));
    }
    for i in 0..segs {
        let i0 = inner_base + i * 2;
        indices.extend_from_slice(&[i0, i0 + 3, i0 + 1, i0, i0 + 2, i0 + 3]);
    }

    // Top ring (+Y)
    let top_base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        verts.push(CadVertex::new(Vec3::new(r_in * c, hy, r_in * s), Vec3::Y, [u, 0.0]));
        verts.push(CadVertex::new(Vec3::new(r_out * c, hy, r_out * s), Vec3::Y, [u, 1.0]));
    }
    for i in 0..segs {
        let i0 = top_base + i * 2;
        indices.extend_from_slice(&[i0, i0 + 3, i0 + 1, i0, i0 + 2, i0 + 3]);
    }

    // Bottom ring (-Y)
    let bot_base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        verts.push(CadVertex::new(
            Vec3::new(r_in * c, -hy, r_in * s),
            Vec3::NEG_Y,
            [u, 0.0],
        ));
        verts.push(CadVertex::new(
            Vec3::new(r_out * c, -hy, r_out * s),
            Vec3::NEG_Y,
            [u, 1.0],
        ));
    }
    for i in 0..segs {
        let i0 = bot_base + i * 2;
        indices.extend_from_slice(&[i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]);
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// Multi-diameter revolved solid along Y axis (`sections` is slice of `(radius, y)`).
pub fn make_stepped_shaft(
    name: impl Into<String>,
    profile_ry: &[(f32, f32)],
    segments: u32,
) -> CadMesh {
    let segs = segments.max(12);
    let mut verts = Vec::new();
    let mut indices = Vec::new();

    if profile_ry.len() < 2 {
        return make_cylinder(name, 0.5, 1.0, segs);
    }

    // Revolve each segment cleanly with crisp flat face normals per step/bevel
    for w in profile_ry.windows(2) {
        let (r0, y0) = w[0];
        let (r1, y1) = w[1];
        let dr = r1 - r0;
        let dy = y1 - y0;
        if dr.abs() < 1e-6 && dy.abs() < 1e-6 {
            continue;
        }
        let base = verts.len() as u32;
        for i in 0..=segs {
            let u = i as f32 / segs as f32;
            let a = u * TAU;
            let (s, c) = a.sin_cos();
            let tangent = Vec3::new(-s, 0.0, c);
            let meridian = Vec3::new(dr * c, dy, dr * s);
            let n = tangent.cross(meridian).normalize_or(Vec3::Y);
            verts.push(CadVertex::new(Vec3::new(r0 * c, y0, r0 * s), n, [u, 0.0]));
            verts.push(CadVertex::new(Vec3::new(r1 * c, y1, r1 * s), n, [u, 1.0]));
        }
        for i in 0..segs {
            let i0 = base + i * 2;
            indices.extend_from_slice(&[i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]);
        }
    }

    // Bottom cap if r > 0
    let (r_start, y_start) = profile_ry[0];
    if r_start > 1e-5 {
        let c_idx = verts.len() as u32;
        verts.push(CadVertex::new(
            Vec3::new(0.0, y_start, 0.0),
            Vec3::NEG_Y,
            [0.5, 0.5],
        ));
        for i in 0..=segs {
            let a = (i as f32 / segs as f32) * TAU;
            let (s, c) = a.sin_cos();
            verts.push(CadVertex::new(
                Vec3::new(r_start * c, y_start, r_start * s),
                Vec3::NEG_Y,
                [0.5 + 0.5 * c, 0.5 + 0.5 * s],
            ));
        }
        for i in 0..segs {
            indices.extend_from_slice(&[c_idx, c_idx + 1 + i, c_idx + 1 + i + 1]);
        }
    }

    // Top cap if r > 0
    let (r_end, y_end) = *profile_ry.last().unwrap();
    if r_end > 1e-5 {
        let c_idx = verts.len() as u32;
        verts.push(CadVertex::new(
            Vec3::new(0.0, y_end, 0.0),
            Vec3::Y,
            [0.5, 0.5],
        ));
        for i in 0..=segs {
            let a = (i as f32 / segs as f32) * TAU;
            let (s, c) = a.sin_cos();
            verts.push(CadVertex::new(
                Vec3::new(r_end * c, y_end, r_end * s),
                Vec3::Y,
                [0.5 + 0.5 * c, 0.5 + 0.5 * s],
            ));
        }
        for i in 0..segs {
            indices.extend_from_slice(&[c_idx, c_idx + 1 + i + 1, c_idx + 1 + i]);
        }
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// Parametric chamfered mounting flange with central bore and raised hub.
pub fn make_flange(
    name: impl Into<String>,
    outer_r: f32,
    bore_r: f32,
    plate_h: f32,
    hub_r: f32,
    hub_h: f32,
    segments: u32,
) -> CadMesh {
    let segs = segments.max(24);
    let r_chamfer = (outer_r - plate_h * 0.14).max(bore_r + 0.05);
    let hub_r = hub_r.clamp(bore_r + 0.04, outer_r - 0.05);
    let y0 = 0.0;
    let y1 = plate_h * 0.86;
    let y2 = plate_h;
    let y3 = plate_h + hub_h;

    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Build closed revolved hollow profile with crisp normals:
    let segments_profile: [((f32, f32), (f32, f32)); 6] = [
        ((bore_r, y3), (bore_r, y0)),         // Inner bore
        ((bore_r, y0), (outer_r, y0)),        // Bottom face
        ((outer_r, y0), (outer_r, y1)),       // Outer rim
        ((outer_r, y1), (r_chamfer, y2)),     // Chamfer
        ((r_chamfer, y2), (hub_r, y2)),       // Flange shoulder
        ((hub_r, y2), (hub_r, y3)),           // Hub wall
    ];
    let top_seg = ((hub_r, y3), (bore_r, y3));

    for ((r0, py0), (r1, py1)) in segments_profile.into_iter().chain(std::iter::once(top_seg)) {
        let dr = r1 - r0;
        let dy = py1 - py0;
        let base = verts.len() as u32;
        for i in 0..=segs {
            let u = i as f32 / segs as f32;
            let a = u * TAU;
            let (s, c) = a.sin_cos();
            let tangent = Vec3::new(-s, 0.0, c);
            let meridian = Vec3::new(dr * c, dy, dr * s);
            let n = tangent.cross(meridian).normalize_or(Vec3::Y);
            verts.push(CadVertex::new(Vec3::new(r0 * c, py0, r0 * s), n, [u, 0.0]));
            verts.push(CadVertex::new(Vec3::new(r1 * c, py1, r1 * s), n, [u, 1.0]));
        }
        for i in 0..segs {
            let i0 = base + i * 2;
            indices.extend_from_slice(&[i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]);
        }
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// Involute-style spur gear centered around origin along Y axis with central bore.
pub fn make_spur_gear(
    name: impl Into<String>,
    pitch_radius: f32,
    teeth: u32,
    thickness: f32,
    bore_radius: f32,
) -> CadMesh {
    let teeth = teeth.max(8);
    let addendum = (pitch_radius * TAU / teeth as f32) * 0.28;
    let dedendum = addendum * 1.15;
    let r_outer = pitch_radius + addendum;
    let r_root = (pitch_radius - dedendum).max(bore_radius + 0.05);
    let r_bore = bore_radius.clamp(0.02, r_root - 0.03);
    let hy = thickness * 0.5;

    // 4 points per tooth around the pitch circle (root_start, tip_start, tip_end, root_end)
    let mut ring_pts: Vec<[f32; 2]> = Vec::with_capacity((teeth * 4) as usize);
    let tooth_angle = TAU / teeth as f32;
    for t in 0..teeth {
        let a0 = t as f32 * tooth_angle;
        let a_root0 = a0;
        let a_flank0 = a0 + tooth_angle * 0.16;
        let a_tip0 = a0 + tooth_angle * 0.28;
        let a_tip1 = a0 + tooth_angle * 0.52;
        let a_flank1 = a0 + tooth_angle * 0.64;
        let a_root1 = a0 + tooth_angle * 0.80;

        for (ang, rad) in [
            (a_root0, r_root),
            (a_flank0, r_root),
            (a_tip0, r_outer),
            (a_tip1, r_outer),
            (a_flank1, r_root),
            (a_root1, r_root),
        ] {
            let (s, c) = ang.sin_cos();
            ring_pts.push([rad * c, rad * s]);
        }
    }

    let n_ring = ring_pts.len();
    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Outer gear tooth wall (flat-shaded per facet for crisp CAD tooth creases!)
    for i in 0..n_ring {
        let p0 = ring_pts[i];
        let p1 = ring_pts[(i + 1) % n_ring];
        let a0 = Vec3::new(p0[0], -hy, p0[1]);
        let a1 = Vec3::new(p0[0], hy, p0[1]);
        let b1 = Vec3::new(p1[0], hy, p1[1]);
        let b0 = Vec3::new(p1[0], -hy, p1[1]);
        let n = (a1 - a0).cross(b0 - a0).normalize_or(Vec3::X);
        push_quad(&mut verts, &mut indices, a0, a1, b1, b0, n);
    }

    // Inner bore wall (corresponding 1:1 with ring_pts count for watertight annular caps!)
    for i in 0..n_ring {
        let ang0 = (i as f32 / n_ring as f32) * TAU;
        let ang1 = (((i + 1) % n_ring) as f32 / n_ring as f32) * TAU;
        let (s0, c0) = ang0.sin_cos();
        let (s1, c1) = ang1.sin_cos();
        let p_in0_bot = Vec3::new(r_bore * c0, -hy, r_bore * s0);
        let p_in0_top = Vec3::new(r_bore * c0, hy, r_bore * s0);
        let p_in1_top = Vec3::new(r_bore * c1, hy, r_bore * s1);
        let p_in1_bot = Vec3::new(r_bore * c1, -hy, r_bore * s1);
        let n = Vec3::new(-(c0 + c1), 0.0, -(s0 + s1)).normalize_or(Vec3::NEG_X);
        push_quad(
            &mut verts,
            &mut indices,
            p_in1_bot,
            p_in1_top,
            p_in0_top,
            p_in0_bot,
            n,
        );
    }

    // Annular top & bottom faces connecting tooth ring to bore ring
    // Match angular phase cleanly by aligning bore angles to outer point angles
    let mut inner_pts: Vec<[f32; 2]> = Vec::with_capacity(n_ring);
    for p in &ring_pts {
        let len = (p[0] * p[0] + p[1] * p[1]).sqrt().max(1e-5);
        inner_pts.push([p[0] / len * r_bore, p[1] / len * r_bore]);
    }

    for i in 0..n_ring {
        let j = (i + 1) % n_ring;
        let o0 = ring_pts[i];
        let o1 = ring_pts[j];
        let in0 = inner_pts[i];
        let in1 = inner_pts[j];

        // Top (+Y)
        push_quad(
            &mut verts,
            &mut indices,
            Vec3::new(in0[0], hy, in0[1]),
            Vec3::new(in1[0], hy, in1[1]),
            Vec3::new(o1[0], hy, o1[1]),
            Vec3::new(o0[0], hy, o0[1]),
            Vec3::Y,
        );

        // Bottom (-Y)
        push_quad(
            &mut verts,
            &mut indices,
            Vec3::new(o0[0], -hy, o0[1]),
            Vec3::new(o1[0], -hy, o1[1]),
            Vec3::new(in1[0], -hy, in1[1]),
            Vec3::new(in0[0], -hy, in0[1]),
            Vec3::NEG_Y,
        );
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// Socket/Hex cap bolt aligned along Y axis (`y = 0` at underside of head).
pub fn make_hex_bolt(
    name: impl Into<String>,
    head_r: f32,
    head_h: f32,
    shaft_r: f32,
    shaft_len: f32,
) -> CadMesh {
    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Hex head: y in [0, head_h]
    let hex = 6_u32;
    for i in 0..hex {
        let a0 = (i as f32 / hex as f32) * TAU;
        let a1 = ((i + 1) as f32 / hex as f32) * TAU;
        let p0 = Vec3::new(head_r * a0.cos(), 0.0, head_r * a0.sin());
        let p1 = Vec3::new(head_r * a0.cos(), head_h, head_r * a0.sin());
        let p2 = Vec3::new(head_r * a1.cos(), head_h, head_r * a1.sin());
        let p3 = Vec3::new(head_r * a1.cos(), 0.0, head_r * a1.sin());
        let n = (p1 - p0).cross(p3 - p0).normalize_or(Vec3::X);
        push_quad(&mut verts, &mut indices, p0, p1, p2, p3, n);
    }

    // Head top cap (+Y)
    let tc = verts.len() as u32;
    verts.push(CadVertex::new(Vec3::new(0.0, head_h, 0.0), Vec3::Y, [0.5, 0.5]));
    for i in 0..=hex {
        let a = (i as f32 / hex as f32) * TAU;
        verts.push(CadVertex::new(
            Vec3::new(head_r * a.cos(), head_h, head_r * a.sin()),
            Vec3::Y,
            [0.5, 0.5],
        ));
    }
    for i in 0..hex {
        indices.extend_from_slice(&[tc, tc + 1 + i + 1, tc + 1 + i]);
    }

    // Head underside (-Y)
    let bc = verts.len() as u32;
    verts.push(CadVertex::new(Vec3::ZERO, Vec3::NEG_Y, [0.5, 0.5]));
    for i in 0..=hex {
        let a = (i as f32 / hex as f32) * TAU;
        verts.push(CadVertex::new(
            Vec3::new(head_r * a.cos(), 0.0, head_r * a.sin()),
            Vec3::NEG_Y,
            [0.5, 0.5],
        ));
    }
    for i in 0..hex {
        indices.extend_from_slice(&[bc, bc + 1 + i, bc + 1 + i + 1]);
    }

    // Cylindrical shank: y in [-shaft_len, 0]
    let segs = 16_u32;
    let base = verts.len() as u32;
    for i in 0..=segs {
        let u = i as f32 / segs as f32;
        let a = u * TAU;
        let (s, c) = a.sin_cos();
        let n = Vec3::new(c, 0.0, s);
        verts.push(CadVertex::new(Vec3::new(shaft_r * c, -shaft_len, shaft_r * s), n, [u, 0.0]));
        verts.push(CadVertex::new(Vec3::new(shaft_r * c, 0.0, shaft_r * s), n, [u, 1.0]));
    }
    for i in 0..segs {
        let i0 = base + i * 2;
        indices.extend_from_slice(&[i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]);
    }

    let tip_c = verts.len() as u32;
    verts.push(CadVertex::new(
        Vec3::new(0.0, -shaft_len, 0.0),
        Vec3::NEG_Y,
        [0.5, 0.5],
    ));
    for i in 0..=segs {
        let a = (i as f32 / segs as f32) * TAU;
        let (s, c) = a.sin_cos();
        verts.push(CadVertex::new(
            Vec3::new(shaft_r * c, -shaft_len, shaft_r * s),
            Vec3::NEG_Y,
            [0.5, 0.5],
        ));
    }
    for i in 0..segs {
        indices.extend_from_slice(&[tip_c, tip_c + 1 + i, tip_c + 1 + i + 1]);
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

/// L-shaped precision machined mounting bracket.
pub fn make_bracket(
    name: impl Into<String>,
    width: f32,
    height: f32,
    depth: f32,
    thickness: f32,
) -> CadMesh {
    let hw = width * 0.5;
    let t = thickness.min(height * 0.45).min(depth * 0.45);
    let mut verts = Vec::new();
    let mut indices = Vec::new();

    // Extrude an L-profile in YZ along X in [-hw, hw]
    let profile = [
        Vec3::new(0.0, 0.0, 0.0),
        Vec3::new(0.0, 0.0, depth),
        Vec3::new(0.0, t, depth),
        Vec3::new(0.0, t, t),
        Vec3::new(0.0, height, t),
        Vec3::new(0.0, height, 0.0),
    ];

    // Side extruded faces
    for i in 0..profile.len() {
        let a = profile[i];
        let b = profile[(i + 1) % profile.len()];
        let p0 = Vec3::new(-hw, a.y, a.z);
        let p1 = Vec3::new(hw, a.y, a.z);
        let p2 = Vec3::new(hw, b.y, b.z);
        let p3 = Vec3::new(-hw, b.y, b.z);
        let n = (p1 - p0).cross(p3 - p0).normalize_or(Vec3::Y);
        push_quad(&mut verts, &mut indices, p0, p1, p2, p3, n);
    }

    // End caps decomposed as 2 rectangles (base + upright)
    for x_side in [-1.0_f32, 1.0_f32] {
        let x = x_side * hw;
        let n = Vec3::new(x_side, 0.0, 0.0);
        if x_side > 0.0 {
            push_quad(
                &mut verts,
                &mut indices,
                Vec3::new(x, 0.0, 0.0),
                Vec3::new(x, t, 0.0),
                Vec3::new(x, t, depth),
                Vec3::new(x, 0.0, depth),
                n,
            );
            push_quad(
                &mut verts,
                &mut indices,
                Vec3::new(x, t, 0.0),
                Vec3::new(x, height, 0.0),
                Vec3::new(x, height, t),
                Vec3::new(x, t, t),
                n,
            );
        } else {
            push_quad(
                &mut verts,
                &mut indices,
                Vec3::new(x, 0.0, depth),
                Vec3::new(x, t, depth),
                Vec3::new(x, t, 0.0),
                Vec3::new(x, 0.0, 0.0),
                n,
            );
            push_quad(
                &mut verts,
                &mut indices,
                Vec3::new(x, t, t),
                Vec3::new(x, height, t),
                Vec3::new(x, height, 0.0),
                Vec3::new(x, t, 0.0),
                n,
            );
        }
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

pub fn make_torus(
    name: impl Into<String>,
    major_r: f32,
    minor_r: f32,
    major_seg: u32,
    minor_seg: u32,
) -> CadMesh {
    let maj = major_seg.max(16);
    let min = minor_seg.max(10);
    let mut verts = Vec::with_capacity(((maj + 1) * (min + 1)) as usize);
    let mut indices = Vec::with_capacity((maj * min * 6) as usize);

    for i in 0..=maj {
        let u = i as f32 / maj as f32;
        let phi = u * TAU;
        let (sp, cp) = phi.sin_cos();
        let center = Vec3::new(major_r * cp, 0.0, major_r * sp);
        for j in 0..=min {
            let v = j as f32 / min as f32;
            let theta = v * TAU;
            let (st, ct) = theta.sin_cos();
            let radial = Vec3::new(cp * ct, st, sp * ct);
            let pos = center + radial * minor_r;
            verts.push(CadVertex::new(pos, radial, [u, v]));
        }
    }

    let stride = min + 1;
    for i in 0..maj {
        for j in 0..min {
            let a = i * stride + j;
            let b = (i + 1) * stride + j;
            let c = (i + 1) * stride + (j + 1);
            let d = i * stride + (j + 1);
            indices.extend_from_slice(&[a, b, c, a, c, d]);
        }
    }

    CadMesh::from_geometry(name, verts, indices, CreaseConfig::default())
}

pub fn build_demo_assembly(kind: DemoAssemblyKind) -> Vec<AssemblyPartSpec> {
    match kind {
        DemoAssemblyKind::SpindleHousing => build_spindle_housing(),
        DemoAssemblyKind::PlanetaryGearbox => build_planetary_gearbox(),
        DemoAssemblyKind::HydraulicManifold => build_hydraulic_manifold(),
    }
}

fn build_spindle_housing() -> Vec<AssemblyPartSpec> {
    let mut parts = Vec::new();

    // 1. Machined base mounting plate
    parts.push(AssemblyPartSpec {
        name: "Base Mounting Plate".into(),
        group: "Housing".into(),
        mesh: make_box("base_plate", Vec3::new(4.2, 0.34, 3.6)),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.17, 0.0)),
        explode_offset: Vec3::new(0.0, -1.2, 0.0),
        material: CadMaterialData::dark_oxide(),
    });

    // 2. Twin vertical precision pillow-block brackets
    for (idx, z_pos) in [-1.15_f32, 1.15_f32].into_iter().enumerate() {
        let yaw = if idx == 0 { 0.0 } else { PI };
        parts.push(AssemblyPartSpec {
            name: format!("Support Bracket #{}", idx + 1),
            group: "Housing".into(),
            mesh: make_bracket(format!("bracket_{idx}"), 3.2, 1.35, 0.52, 0.24),
            transform: Mat4::from_translation(Vec3::new(0.0, 0.34, z_pos))
                * Mat4::from_rotation_y(yaw),
            explode_offset: Vec3::new(0.0, -0.4, z_pos * 0.8),
            material: CadMaterialData::anodized_aluminum(),
        });
    }

    // 3. Main spindle housing barrel (horizontal along X!)
    let rot_to_x = Mat4::from_rotation_z(-PI * 0.5);
    parts.push(AssemblyPartSpec {
        name: "Spindle Stator Barrel".into(),
        group: "Housing".into(),
        mesh: make_tube("spindle_barrel", 1.12, 0.72, 2.5, 48),
        transform: Mat4::from_translation(Vec3::new(0.0, 1.65, 0.0)) * rot_to_x,
        explode_offset: Vec3::new(0.0, 0.8, 0.0),
        material: CadMaterialData::anodized_aluminum(),
    });

    // 4. Cooling jacket rings around the barrel
    for i in 0..4 {
        let x = -0.75 + i as f32 * 0.5;
        parts.push(AssemblyPartSpec {
            name: format!("Cooling Collar #{}", i + 1),
            group: "Thermal".into(),
            mesh: make_tube(format!("cooling_ring_{i}"), 1.26, 1.12, 0.16, 48),
            transform: Mat4::from_translation(Vec3::new(x, 1.65, 0.0)) * rot_to_x,
            explode_offset: Vec3::new(x * 0.6, 1.5, 0.0),
            material: CadMaterialData::cobalt_anodized(),
        });
    }

    // 5. Front & Rear Precision Bearing Flanges
    parts.push(AssemblyPartSpec {
        name: "Front Nose Flange".into(),
        group: "Flanges".into(),
        mesh: make_flange("front_flange", 1.34, 0.56, 0.24, 0.84, 0.34, 48),
        transform: Mat4::from_translation(Vec3::new(1.25, 1.65, 0.0)) * rot_to_x,
        explode_offset: Vec3::new(1.8, 0.0, 0.0),
        material: CadMaterialData::cobalt_anodized(),
    });
    parts.push(AssemblyPartSpec {
        name: "Rear Endcap Flange".into(),
        group: "Flanges".into(),
        mesh: make_flange("rear_flange", 1.32, 0.50, 0.24, 0.80, 0.28, 48),
        transform: Mat4::from_translation(Vec3::new(-1.25, 1.65, 0.0))
            * Mat4::from_rotation_z(PI * 0.5),
        explode_offset: Vec3::new(-1.8, 0.0, 0.0),
        material: CadMaterialData::anodized_aluminum(),
    });

    // 6. Brass angular-contact thrust bearing sleeves
    for (label, x_pos, dir) in [
        ("Front Brass Bearing", 1.05, 1.1),
        ("Rear Brass Bearing", -1.05, -1.1),
    ] {
        parts.push(AssemblyPartSpec {
            name: label.into(),
            group: "Bearings".into(),
            mesh: make_tube(label, 0.71, 0.48, 0.34, 40),
            transform: Mat4::from_translation(Vec3::new(x_pos, 1.65, 0.0)) * rot_to_x,
            explode_offset: Vec3::new(dir, 0.0, 0.0),
            material: CadMaterialData::brass_bearing(),
        });
    }

    // 7. Copper oil seal O-ring
    parts.push(AssemblyPartSpec {
        name: "Copper Thermal Seal Ring".into(),
        group: "Bearings".into(),
        mesh: make_torus("copper_seal", 0.62, 0.055, 40, 14),
        transform: Mat4::from_translation(Vec3::new(1.36, 1.65, 0.0)) * rot_to_x,
        explode_offset: Vec3::new(2.2, 0.0, 0.0),
        material: CadMaterialData::copper_seal(),
    });

    // 8. Multi-stepped CNC Rotor Shaft along X
    let shaft_profile = [
        (0.28, -2.05),
        (0.28, -1.50),
        (0.42, -1.46),
        (0.42, -0.95),
        (0.55, -0.92),
        (0.55, 0.92),
        (0.45, 0.96),
        (0.45, 1.48),
        (0.32, 1.54),
        (0.32, 2.15),
        (0.24, 2.25),
    ];
    parts.push(AssemblyPartSpec {
        name: "Stepped Spindle Rotor Shaft".into(),
        group: "Drivetrain".into(),
        mesh: make_stepped_shaft("rotor_shaft", &shaft_profile, 40),
        transform: Mat4::from_translation(Vec3::new(0.0, 1.65, 0.0)) * rot_to_x,
        explode_offset: Vec3::new(2.7, 0.0, 0.0),
        material: CadMaterialData::machined_steel(),
    });

    // 9. Rear Drive Spur Gear
    parts.push(AssemblyPartSpec {
        name: "Rear Timing Spur Gear".into(),
        group: "Drivetrain".into(),
        mesh: make_spur_gear("drive_gear", 0.95, 22, 0.30, 0.28),
        transform: Mat4::from_translation(Vec3::new(-1.82, 1.65, 0.0)) * rot_to_x,
        explode_offset: Vec3::new(-2.8, 0.0, 0.0),
        material: CadMaterialData::machined_steel(),
    });

    // 10. 6x Front Flange Hex Cap Bolts
    let bolt_mesh = make_hex_bolt("flange_bolt", 0.085, 0.09, 0.048, 0.24);
    let bolt_circle_r = 1.08_f32;
    for i in 0..6 {
        let ang = (i as f32 / 6.0) * TAU + PI / 6.0;
        let y = 1.65 + bolt_circle_r * ang.sin();
        let z = bolt_circle_r * ang.cos();
        parts.push(AssemblyPartSpec {
            name: format!("Flange Cap Bolt M8 #{}", i + 1),
            group: "Fasteners".into(),
            mesh: bolt_mesh.clone(),
            transform: Mat4::from_translation(Vec3::new(1.49, y, z)) * rot_to_x,
            explode_offset: Vec3::new(2.9, ang.sin() * 0.4, ang.cos() * 0.4),
            material: CadMaterialData::machined_steel(),
        });
    }

    // 11. 4x Base Hold-Down Bolts
    for (idx, (bx, bz)) in [
        (-1.75_f32, -1.45_f32),
        (1.75, -1.45),
        (-1.75, 1.45),
        (1.75, 1.45),
    ]
    .into_iter()
    .enumerate()
    {
        parts.push(AssemblyPartSpec {
            name: format!("Base Anchor Bolt #{}", idx + 1),
            group: "Fasteners".into(),
            mesh: make_hex_bolt("base_bolt", 0.12, 0.11, 0.07, 0.32),
            transform: Mat4::from_translation(Vec3::new(bx, 0.34, bz)),
            explode_offset: Vec3::new(bx * 0.3, 1.1, bz * 0.3),
            material: CadMaterialData::dark_oxide(),
        });
    }

    parts
}

fn build_planetary_gearbox() -> Vec<AssemblyPartSpec> {
    let mut parts = Vec::new();
    let sun_r = 0.62_f32;
    let planet_r = 0.56_f32;
    let carrier_r = sun_r + planet_r;

    parts.push(AssemblyPartSpec {
        name: "Gearbox Ring Housing".into(),
        group: "Housing".into(),
        mesh: make_tube("ring_housing", 2.15, 1.80, 0.85, 56),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.52, 0.0)),
        explode_offset: Vec3::new(0.0, 0.5, 0.0),
        material: CadMaterialData::dark_oxide(),
    });

    parts.push(AssemblyPartSpec {
        name: "Lower Carrier Plate".into(),
        group: "Carrier".into(),
        mesh: make_flange("carrier_plate", 1.85, 0.25, 0.20, 0.45, 0.18, 48),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.0, 0.0)),
        explode_offset: Vec3::new(0.0, -1.2, 0.0),
        material: CadMaterialData::anodized_aluminum(),
    });

    parts.push(AssemblyPartSpec {
        name: "Sun Gear (18T)".into(),
        group: "Gears".into(),
        mesh: make_spur_gear("sun_gear", sun_r, 18, 0.48, 0.22),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.52, 0.0)),
        explode_offset: Vec3::new(0.0, 1.2, 0.0),
        material: CadMaterialData::cobalt_anodized(),
    });

    parts.push(AssemblyPartSpec {
        name: "Input Sun Shaft".into(),
        group: "Drivetrain".into(),
        mesh: make_stepped_shaft(
            "sun_shaft",
            &[(0.18, -0.3), (0.22, 0.0), (0.22, 1.5), (0.15, 1.9)],
            32,
        ),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.52, 0.0)),
        explode_offset: Vec3::new(0.0, 2.2, 0.0),
        material: CadMaterialData::machined_steel(),
    });

    for i in 0..3 {
        let ang = (i as f32 / 3.0) * TAU;
        let x = carrier_r * ang.cos();
        let z = carrier_r * ang.sin();
        parts.push(AssemblyPartSpec {
            name: format!("Planet Gear #{} (16T)", i + 1),
            group: "Gears".into(),
            mesh: make_spur_gear(format!("planet_{i}"), planet_r, 16, 0.46, 0.16),
            transform: Mat4::from_translation(Vec3::new(x, 0.52, z))
                * Mat4::from_rotation_y(ang + 0.19),
            explode_offset: Vec3::new(ang.cos() * 1.4, 0.9, ang.sin() * 1.4),
            material: CadMaterialData::brass_bearing(),
        });

        parts.push(AssemblyPartSpec {
            name: format!("Planet Pin #{}", i + 1),
            group: "Carrier".into(),
            mesh: make_cylinder(format!("pin_{i}"), 0.155, 0.85, 24),
            transform: Mat4::from_translation(Vec3::new(x, 0.52, z)),
            explode_offset: Vec3::new(ang.cos() * 1.4, -0.4, ang.sin() * 1.4),
            material: CadMaterialData::machined_steel(),
        });
    }

    parts
}

fn build_hydraulic_manifold() -> Vec<AssemblyPartSpec> {
    let mut parts = Vec::new();

    parts.push(AssemblyPartSpec {
        name: "Billet Manifold Body".into(),
        group: "Body".into(),
        mesh: make_box("manifold_body", Vec3::new(3.2, 1.35, 1.9)),
        transform: Mat4::from_translation(Vec3::new(0.0, 0.675, 0.0)),
        explode_offset: Vec3::ZERO,
        material: CadMaterialData::anodized_aluminum(),
    });

    for (idx, x) in [-1.0_f32, 0.0, 1.0].into_iter().enumerate() {
        parts.push(AssemblyPartSpec {
            name: format!("Cartridge Valve #{}", idx + 1),
            group: "Valves".into(),
            mesh: make_stepped_shaft(
                format!("valve_{idx}"),
                &[(0.28, 0.0), (0.28, 0.42), (0.18, 0.46), (0.18, 0.78)],
                32,
            ),
            transform: Mat4::from_translation(Vec3::new(x, 1.35, 0.0)),
            explode_offset: Vec3::new(0.0, 1.4, 0.0),
            material: CadMaterialData::cobalt_anodized(),
        });

        parts.push(AssemblyPartSpec {
            name: format!("Copper Crush Washer #{}", idx + 1),
            group: "Seals".into(),
            mesh: make_torus(format!("washer_{idx}"), 0.28, 0.035, 28, 12),
            transform: Mat4::from_translation(Vec3::new(x, 1.37, 0.0)),
            explode_offset: Vec3::new(0.0, 0.85, 0.0),
            material: CadMaterialData::copper_seal(),
        });
    }

    for (idx, x) in [-0.8_f32, 0.8].into_iter().enumerate() {
        parts.push(AssemblyPartSpec {
            name: format!("SAE Port Flange #{}", idx + 1),
            group: "Ports".into(),
            mesh: make_flange(format!("port_{idx}"), 0.42, 0.18, 0.14, 0.26, 0.20, 32),
            transform: Mat4::from_translation(Vec3::new(x, 0.68, 0.95))
                * Mat4::from_rotation_x(PI * 0.5),
            explode_offset: Vec3::new(0.0, 0.0, 1.2),
            material: CadMaterialData::brass_bearing(),
        });
    }

    // End pressure gauge boss
    parts.push(AssemblyPartSpec {
        name: "Pressure Gauge Collar".into(),
        group: "Sensors".into(),
        mesh: make_tube("gauge_collar", 0.48, 0.36, 0.32, 36),
        transform: Mat4::from_translation(Vec3::new(1.76, 0.75, 0.0))
            * Mat4::from_rotation_z(-PI * 0.5),
        explode_offset: Vec3::new(1.3, 0.0, 0.0),
        material: CadMaterialData::dark_oxide(),
    });

    let _ = Quat::IDENTITY;
    parts
}
