use glam::{Mat4, Vec3, Vec4};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Aabb3 {
    pub min: Vec3,
    pub max: Vec3,
}

impl Default for Aabb3 {
    fn default() -> Self {
        Self::EMPTY
    }
}

impl Aabb3 {
    pub const EMPTY: Self = Self {
        min: Vec3::splat(f32::INFINITY),
        max: Vec3::splat(f32::NEG_INFINITY),
    };

    pub fn from_points(points: impl IntoIterator<Item = Vec3>) -> Self {
        let mut aabb = Self::EMPTY;
        for p in points {
            aabb.expand_point(p);
        }
        aabb
    }

    pub fn is_valid(&self) -> bool {
        self.min.x <= self.max.x && self.min.y <= self.max.y && self.min.z <= self.max.z
    }

    pub fn expand_point(&mut self, p: Vec3) {
        self.min = self.min.min(p);
        self.max = self.max.max(p);
    }

    pub fn merge(&self, other: &Self) -> Self {
        if !self.is_valid() {
            return *other;
        }
        if !other.is_valid() {
            return *self;
        }
        Self {
            min: self.min.min(other.min),
            max: self.max.max(other.max),
        }
    }

    pub fn transformed(&self, m: Mat4) -> Self {
        if !self.is_valid() {
            return Self::EMPTY;
        }
        let corners = [
            Vec3::new(self.min.x, self.min.y, self.min.z),
            Vec3::new(self.max.x, self.min.y, self.min.z),
            Vec3::new(self.min.x, self.max.y, self.min.z),
            Vec3::new(self.max.x, self.max.y, self.min.z),
            Vec3::new(self.min.x, self.min.y, self.max.z),
            Vec3::new(self.max.x, self.min.y, self.max.z),
            Vec3::new(self.min.x, self.max.y, self.max.z),
            Vec3::new(self.max.x, self.max.y, self.max.z),
        ];
        Self::from_points(corners.into_iter().map(|c| m.transform_point3(c)))
    }

    pub fn center(&self) -> Vec3 {
        if self.is_valid() {
            (self.min + self.max) * 0.5
        } else {
            Vec3::ZERO
        }
    }

    pub fn size(&self) -> Vec3 {
        if self.is_valid() {
            self.max - self.min
        } else {
            Vec3::ZERO
        }
    }

    pub fn diagonal(&self) -> f32 {
        self.size().length()
    }

    pub fn min_distance_to(&self, other: &Self) -> f32 {
        if !self.is_valid() || !other.is_valid() {
            return f32::INFINITY;
        }
        let dx = (self.min.x - other.max.x)
            .max(other.min.x - self.max.x)
            .max(0.0);
        let dy = (self.min.y - other.max.y)
            .max(other.min.y - self.max.y)
            .max(0.0);
        let dz = (self.min.z - other.max.z)
            .max(other.min.z - self.max.z)
            .max(0.0);
        Vec3::new(dx, dy, dz).length()
    }

    pub fn intersect_ray(&self, ray: &Ray3) -> Option<(f32, f32)> {
        if !self.is_valid() {
            return None;
        }
        let mut tmin = 0.0_f32;
        let mut tmax = f32::INFINITY;

        for axis in 0..3 {
            let origin = ray.origin[axis];
            let dir = ray.dir[axis];
            let bmin = self.min[axis];
            let bmax = self.max[axis];

            if dir.abs() < 1e-8 {
                if origin < bmin || origin > bmax {
                    return None;
                }
            } else {
                let inv_d = 1.0 / dir;
                let mut t1 = (bmin - origin) * inv_d;
                let mut t2 = (bmax - origin) * inv_d;
                if t1 > t2 {
                    std::mem::swap(&mut t1, &mut t2);
                }
                tmin = tmin.max(t1);
                tmax = tmax.min(t2);
                if tmin > tmax {
                    return None;
                }
            }
        }
        Some((tmin, tmax))
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Ray3 {
    pub origin: Vec3,
    pub dir: Vec3,
}

impl Ray3 {
    pub fn new(origin: Vec3, dir: Vec3) -> Self {
        Self {
            origin,
            dir: dir.normalize_or_zero(),
        }
    }

    pub fn point_at(&self, t: f32) -> Vec3 {
        self.origin + self.dir * t
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Plane3 {
    pub normal: Vec3,
    pub offset: f32,
}

impl Plane3 {
    pub fn new(normal: Vec3, offset: f32) -> Self {
        Self {
            normal: normal.normalize_or(Vec3::Y),
            offset,
        }
    }

    /// Clip-space plane equation: dot(world_pos, normal) + d >= 0 is kept.
    /// When `offset` is position along `normal`, a point `p` with `dot(p, n) <= offset`
    /// is kept if normal points away from kept half-space, or `dot(p, n) - offset >= 0`
    /// if keeping the positive half-space. Here we define `d = -offset` so
    /// `dot(p, normal) - offset >= 0` is kept.
    pub fn to_clip_vec4(&self) -> Vec4 {
        let n = self.normal.normalize_or(Vec3::Y);
        Vec4::new(n.x, n.y, n.z, -self.offset)
    }

    pub fn signed_distance(&self, p: Vec3) -> f32 {
        self.normal.normalize_or(Vec3::Y).dot(p) - self.offset
    }
}

/// Right-handed Reverse-Z Perspective matrix for WebGPU/wgpu NDC depth `[0, 1]`.
/// Near plane maps to `z_ndc = 1.0`, far plane maps to `z_ndc = 0.0`.
pub fn reverse_z_perspective_rh(fov_y_rad: f32, aspect: f32, near: f32, far: f32) -> Mat4 {
    let f = 1.0 / (0.5 * fov_y_rad).tan();
    let aspect = aspect.max(1e-4);
    let near = near.max(1e-4);
    let far = far.max(near + 1.0);
    let inv_range = 1.0 / (far - near);
    let a = near * inv_range;
    let b = far * near * inv_range;

    Mat4::from_cols(
        Vec4::new(f / aspect, 0.0, 0.0, 0.0),
        Vec4::new(0.0, f, 0.0, 0.0),
        Vec4::new(0.0, 0.0, a, -1.0),
        Vec4::new(0.0, 0.0, b, 0.0),
    )
}

/// Right-handed Reverse-Z Orthographic matrix for WebGPU/wgpu NDC depth `[0, 1]`.
/// Near plane (`z_view = -near`) maps to `z_ndc = 1.0`, far plane (`z_view = -far`) maps to `z_ndc = 0.0`.
pub fn reverse_z_orthographic_rh(half_height: f32, aspect: f32, near: f32, far: f32) -> Mat4 {
    let half_h = half_height.max(1e-4);
    let half_w = half_h * aspect.max(1e-4);
    let inv_range = 1.0 / (far - near).max(1e-3);

    Mat4::from_cols(
        Vec4::new(1.0 / half_w, 0.0, 0.0, 0.0),
        Vec4::new(0.0, 1.0 / half_h, 0.0, 0.0),
        Vec4::new(0.0, 0.0, inv_range, 0.0),
        Vec4::new(0.0, 0.0, far * inv_range, 1.0),
    )
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct OrbitCameraMath {
    pub target: Vec3,
    pub yaw: f32,
    pub pitch: f32,
    pub distance: f32,
    pub fov_y_rad: f32,
    pub near: f32,
    pub far: f32,
    pub orthographic: bool,
}

impl Default for OrbitCameraMath {
    fn default() -> Self {
        Self {
            target: Vec3::new(0.0, 0.5, 0.0),
            yaw: 0.72,
            pitch: 0.48,
            distance: 9.5,
            fov_y_rad: 38.0_f32.to_radians(),
            near: 0.05,
            far: 2000.0,
            orthographic: false,
        }
    }
}

impl OrbitCameraMath {
    pub fn eye(&self) -> Vec3 {
        let cp = self.pitch.cos();
        let sp = self.pitch.sin();
        let cy = self.yaw.cos();
        let sy = self.yaw.sin();
        let offset = Vec3::new(cp * cy, sp, cp * sy) * self.distance.max(0.05);
        self.target + offset
    }

    pub fn forward(&self) -> Vec3 {
        (self.target - self.eye()).normalize_or(Vec3::NEG_Z)
    }

    pub fn right(&self) -> Vec3 {
        let fwd = self.forward();
        let up_hint = if fwd.y.abs() > 0.995 { Vec3::NEG_Z } else { Vec3::Y };
        fwd.cross(up_hint).normalize_or(Vec3::X)
    }

    pub fn up(&self) -> Vec3 {
        self.right().cross(self.forward()).normalize_or(Vec3::Y)
    }

    pub fn view_matrix(&self) -> Mat4 {
        let eye = self.eye();
        let fwd = (self.target - eye).normalize_or(Vec3::NEG_Z);
        let up_hint = if fwd.y.abs() > 0.995 {
            // Stable up vector for exact top/bottom CAD views
            Vec3::new(-self.yaw.cos(), 0.0, -self.yaw.sin())
        } else {
            Vec3::Y
        };
        let f = fwd;
        let s = f.cross(up_hint).normalize_or(Vec3::X);
        let u = s.cross(f);
        Mat4::from_cols(
            Vec4::new(s.x, u.x, -f.x, 0.0),
            Vec4::new(s.y, u.y, -f.y, 0.0),
            Vec4::new(s.z, u.z, -f.z, 0.0),
            Vec4::new(-s.dot(eye), -u.dot(eye), f.dot(eye), 1.0),
        )
    }

    pub fn proj_matrix(&self, aspect: f32) -> Mat4 {
        if self.orthographic {
            let half_h = self.distance * (self.fov_y_rad * 0.5).tan();
            reverse_z_orthographic_rh(half_h, aspect, -self.far * 0.5, self.far)
        } else {
            reverse_z_perspective_rh(self.fov_y_rad, aspect, self.near, self.far)
        }
    }

    pub fn view_proj(&self, aspect: f32) -> Mat4 {
        self.proj_matrix(aspect) * self.view_matrix()
    }

    /// Generate a world-space ray from normalized pixel coordinates in `[0, 1] x [0, 1]`.
    pub fn ndc_to_ray(&self, uv: [f32; 2], aspect: f32) -> Ray3 {
        let ndc_x = uv[0] * 2.0 - 1.0;
        let ndc_y = 1.0 - uv[1] * 2.0;
        let inv_vp = self.view_proj(aspect).inverse();
        // Reverse-Z: near is z = 1.0, far is z = 0.0
        let p_near_h = inv_vp * Vec4::new(ndc_x, ndc_y, 1.0, 1.0);
        let p_far_h = inv_vp * Vec4::new(ndc_x, ndc_y, 0.0, 1.0);
        let p_near = p_near_h.truncate() / p_near_h.w;
        let p_far = p_far_h.truncate() / p_far_h.w;
        Ray3::new(p_near, p_far - p_near)
    }

    pub fn frame_aabb(&mut self, aabb: &Aabb3) {
        if !aabb.is_valid() {
            return;
        }
        self.target = aabb.center();
        let radius = (aabb.diagonal() * 0.5).max(0.25);
        self.distance = (radius / (self.fov_y_rad * 0.5).tan()) * 1.35;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reverse_z_perspective_depth_mapping() {
        let near = 0.1_f32;
        let far = 1000.0_f32;
        let proj = reverse_z_perspective_rh(45.0_f32.to_radians(), 16.0 / 9.0, near, far);

        let p_near = proj * Vec4::new(0.0, 0.0, -near, 1.0);
        let z_near = p_near.z / p_near.w;
        assert!((z_near - 1.0).abs() < 1e-5, "expected z=1.0 at near, got {z_near}");

        let p_far = proj * Vec4::new(0.0, 0.0, -far, 1.0);
        let z_far = p_far.z / p_far.w;
        assert!(z_far.abs() < 1e-5, "expected z=0.0 at far, got {z_far}");

        let p_mid = proj * Vec4::new(0.0, 0.0, -10.0, 1.0);
        let z_mid = p_mid.z / p_mid.w;
        assert!(z_mid > 0.0 && z_mid < 1.0);
    }

    #[test]
    fn reverse_z_orthographic_depth_mapping() {
        let near = -100.0_f32;
        let far = 500.0_f32;
        let proj = reverse_z_orthographic_rh(5.0, 1.0, near, far);

        let p_near = proj * Vec4::new(0.0, 0.0, -near, 1.0);
        assert!((p_near.z / p_near.w - 1.0).abs() < 1e-5);

        let p_far = proj * Vec4::new(0.0, 0.0, -far, 1.0);
        assert!((p_far.z / p_far.w).abs() < 1e-5);
    }

    #[test]
    fn ray_aabb_hit() {
        let aabb = Aabb3 {
            min: Vec3::splat(-1.0),
            max: Vec3::splat(1.0),
        };
        let ray = Ray3::new(Vec3::new(0.0, 0.0, 5.0), Vec3::NEG_Z);
        let hit = aabb.intersect_ray(&ray);
        assert!(hit.is_some());
        let (tmin, tmax) = hit.unwrap();
        assert!((tmin - 4.0).abs() < 1e-5);
        assert!((tmax - 6.0).abs() < 1e-5);
    }
}
