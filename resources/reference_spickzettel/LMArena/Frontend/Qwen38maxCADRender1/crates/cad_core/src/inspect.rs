use glam::{Mat4, Vec3};

use crate::math::{Plane3, Ray3};
use crate::mesh::CadMesh;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct RayHit {
    pub distance: f32,
    pub world_pos: Vec3,
    pub world_normal: Vec3,
    pub triangle_index: usize,
}

/// Möller-Trumbore ray-triangle intersection in local mesh space (faster than transforming every vertex),
/// optionally respecting an active world-space section clipping plane.
pub fn ray_intersect_mesh(
    world_ray: &Ray3,
    mesh: &CadMesh,
    world_transform: Mat4,
    clip_plane: Option<Plane3>,
) -> Option<RayHit> {
    let inv_world = world_transform.inverse();
    let local_origin = inv_world.transform_point3(world_ray.origin);
    let local_dir = inv_world.transform_vector3(world_ray.dir).normalize_or_zero();
    if local_dir.length_squared() < 1e-10 {
        return None;
    }
    let local_ray = Ray3::new(local_origin, local_dir);

    // Quick AABB rejection
    mesh.aabb.intersect_ray(&local_ray)?;

    let mut closest: Option<RayHit> = None;

    for (tri_idx, tri) in mesh.indices.chunks_exact(3).enumerate() {
        let (i0, i1, i2) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
        if i0 >= mesh.vertices.len() || i1 >= mesh.vertices.len() || i2 >= mesh.vertices.len() {
            continue;
        }
        let v0 = Vec3::from(mesh.vertices[i0].position);
        let v1 = Vec3::from(mesh.vertices[i1].position);
        let v2 = Vec3::from(mesh.vertices[i2].position);

        if let Some((t, _u, _v)) = intersect_triangle(&local_ray, v0, v1, v2) {
            let local_hit = local_ray.point_at(t);
            let world_hit = world_transform.transform_point3(local_hit);
            if let Some(plane) = clip_plane
                && plane.signed_distance(world_hit) < 0.0
            {
                continue;
            }
            let world_dist = (world_hit - world_ray.origin).dot(world_ray.dir);
            if world_dist <= 0.0 {
                continue;
            }
            let replace = closest.is_none_or(|c| world_dist < c.distance);
            if replace {
                let local_n = (v1 - v0).cross(v2 - v0).normalize_or(Vec3::Y);
                let world_n = world_transform
                    .transform_vector3(local_n)
                    .normalize_or(Vec3::Y);
                closest = Some(RayHit {
                    distance: world_dist,
                    world_pos: world_hit,
                    world_normal: world_n,
                    triangle_index: tri_idx,
                });
            }
        }
    }

    closest
}

fn intersect_triangle(ray: &Ray3, v0: Vec3, v1: Vec3, v2: Vec3) -> Option<(f32, f32, f32)> {
    const EPS: f32 = 1e-6;
    let e1 = v1 - v0;
    let e2 = v2 - v0;
    let h = ray.dir.cross(e2);
    let a = e1.dot(h);
    if a.abs() < EPS {
        return None;
    }
    let f = 1.0 / a;
    let s = ray.origin - v0;
    let u = f * s.dot(h);
    if !(0.0..=1.0).contains(&u) {
        return None;
    }
    let q = s.cross(e1);
    let v = f * ray.dir.dot(q);
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let t = f * e2.dot(q);
    if t > EPS {
        Some((t, u, v))
    } else {
        None
    }
}

pub fn measure_distance(a: Vec3, b: Vec3) -> f32 {
    (b - a).length()
}

pub fn measure_angle_deg(n1: Vec3, n2: Vec3) -> f32 {
    let dot = n1
        .normalize_or(Vec3::Y)
        .dot(n2.normalize_or(Vec3::Y))
        .clamp(-1.0, 1.0);
    dot.acos().to_degrees()
}
