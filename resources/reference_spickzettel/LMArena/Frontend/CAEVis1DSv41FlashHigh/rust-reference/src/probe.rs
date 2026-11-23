//! 3D line / rake probes and surface chord slices.
//!
//! Sampling uses trilinear interpolation on the structured solver grid and
//! barycentric interpolation on the extracted surface. The same evaluator feeds
//! the egui_plot panels, so the 3D rake and the charts can never disagree.

use glam::{Vec3, Vec4};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone)]
pub struct HexGrid {
    pub origin: Vec3,
    pub spacing: Vec3,
    pub dims: [usize; 3],
    /// Cell-centred scalar field, x-fastest.
    pub values: Vec<f32>,
}

impl HexGrid {
    pub fn index(&self, i: usize, j: usize, k: usize) -> usize {
        i + self.dims[0] * (j + self.dims[1] * k)
    }

    pub fn value(&self, i: usize, j: usize, k: usize) -> f32 {
        let i = i.min(self.dims[0] - 1);
        let j = j.min(self.dims[1] - 1);
        let k = k.min(self.dims[2] - 1);
        self.values[self.index(i, j, k)]
    }

    /// Continuous trilinear interpolation at a world-space point.
    pub fn trilinear(&self, p: Vec3) -> f32 {
        let g = (p - self.origin) / self.spacing;
        let x = g.x.clamp(0.0, (self.dims[0] - 1) as f32);
        let y = g.y.clamp(0.0, (self.dims[1] - 1) as f32);
        let z = g.z.clamp(0.0, (self.dims[2] - 1) as f32);
        let i = x.floor() as usize;
        let j = y.floor() as usize;
        let k = z.floor() as usize;
        let fx = x - i as f32;
        let fy = y - j as f32;
        let fz = z - k as f32;
        let c = |di, dj, dk| self.value(i + di, j + dj, k + dk);
        let c00 = c(0, 0, 0) * (1.0 - fx) + c(1, 0, 0) * fx;
        let c10 = c(0, 1, 0) * (1.0 - fx) + c(1, 1, 0) * fx;
        let c01 = c(0, 0, 1) * (1.0 - fx) + c(1, 0, 1) * fx;
        let c11 = c(0, 1, 1) * (1.0 - fx) + c(1, 1, 1) * fx;
        let c0 = c00 * (1.0 - fy) + c10 * fy;
        let c1 = c01 * (1.0 - fy) + c11 * fy;
        c0 * (1.0 - fz) + c1 * fz
    }
}

#[derive(Debug, Clone)]
pub struct SurfaceMesh {
    pub positions: Vec<Vec3>,
    pub triangles: Vec<[u32; 3]>,
    pub values: Vec<f32>,
}

impl SurfaceMesh {
    /// Closest-triangle barycentric sample. For the cockpit's probe budget this
    /// brute-force loop is negligible; a BVH is a drop-in replacement.
    pub fn barycentric(&self, p: Vec3) -> f32 {
        let mut best = (f32::INFINITY, 0usize, Vec3::ZERO);
        for (ti, tri) in self.triangles.iter().enumerate() {
            let a = self.positions[tri[0] as usize];
            let b = self.positions[tri[1] as usize];
            let c = self.positions[tri[2] as usize];
            let (w, d) = barycentric_closest(p, a, b, c);
            if d < best.0 {
                best = (d, ti, w);
            }
        }
        let tri = self.triangles[best.1];
        let w = best.2;
        self.values[tri[0] as usize] * w.x
            + self.values[tri[1] as usize] * w.y
            + self.values[tri[2] as usize] * w.z
    }
}

fn barycentric_closest(p: Vec3, a: Vec3, b: Vec3, c: Vec3) -> (Vec3, f32) {
    let v0 = b - a;
    let v1 = c - a;
    let v2 = p - a;
    let d00 = v0.dot(v0);
    let d01 = v0.dot(v1);
    let d11 = v1.dot(v1);
    let d20 = v2.dot(v0);
    let d21 = v2.dot(v1);
    let denom = d00 * d11 - d01 * d01;
    if denom.abs() < 1.0e-12 {
        return (Vec3::new(1.0, 0.0, 0.0), p.distance(a));
    }
    let v = (d11 * d20 - d01 * d21) / denom;
    let w = (d00 * d21 - d01 * d20) / denom;
    let u = 1.0 - v - w;
    let clamped = Vec3::new(u, v, w).max(Vec3::ZERO);
    let sum = clamped.x + clamped.y + clamped.z;
    let w3 = if sum > 0.0 { clamped / sum } else { Vec3::new(1.0, 0.0, 0.0) };
    let hit = a * w3.x + b * w3.y + c * w3.z;
    (w3, hit.distance(p))
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ProbeSamples {
    pub arclength: Vec<f32>,
    pub scalar_a: Vec<f32>,
    pub scalar_b: Vec<f32>,
    pub positions: Vec<[f32; 3]>,
}

impl ProbeSamples {
    pub fn field(&self, run_b: bool) -> &[f32] {
        if run_b { &self.scalar_b } else { &self.scalar_a }
    }
    pub fn min(&self, run_b: bool) -> f32 {
        self.field(run_b).iter().copied().fold(f32::INFINITY, f32::min)
    }
    pub fn max(&self, run_b: bool) -> f32 {
        self.field(run_b).iter().copied().fold(f32::NEG_INFINITY, f32::max)
    }
    pub fn mean(&self, run_b: bool) -> f32 {
        let f = self.field(run_b);
        f.iter().sum::<f32>() / f.len().max(1) as f32
    }
    pub fn delta(&self) -> Vec<f32> {
        self.scalar_b
            .iter()
            .zip(self.scalar_a.iter())
            .map(|(b, a)| b - a)
            .collect()
    }
}

#[derive(Clone, Debug)]
pub struct LineProbe {
    pub a: Vec3,
    pub b: Vec3,
    pub samples: usize,
}

impl LineProbe {
    pub fn length(&self) -> f32 {
        self.a.distance(self.b)
    }

    /// `eval_a` and `eval_b` are closures over the active CFDFLD03 field of each
    /// run. This is where any scalar/vector field can be sampled uniformly.
    pub fn sample(
        &self,
        grid: &HexGrid,
        eval_a: &dyn Fn(Vec3) -> f32,
        eval_b: &dyn Fn(Vec3) -> f32,
    ) -> ProbeSamples {
        let n = self.samples.max(2);
        let mut out = ProbeSamples {
            arclength: Vec::with_capacity(n),
            scalar_a: Vec::with_capacity(n),
            scalar_b: Vec::with_capacity(n),
            positions: Vec::with_capacity(n),
        };
        let length = self.length();
        for i in 0..n {
            let t = i as f32 / (n - 1) as f32;
            let p = self.a.lerp(self.b, t);
            out.arclength.push(t * length);
            out.positions.push(p.to_array());
            let _ = grid;
            out.scalar_a.push(eval_a(p));
            out.scalar_b.push(eval_b(p));
        }
        out
    }
}

#[derive(Clone, Debug)]
pub struct ChordSlice {
    pub plane_point: Vec3,
    pub plane_normal: Vec3,
    pub samples: usize,
}

impl ChordSlice {
    /// Plane–wall intersection curve. The extracted loop is re-parameterised by
    /// chord fraction x/c so Cp(x/c) has a monotone abscissa.
    pub fn intersect(&self, mesh: &SurfaceMesh) -> Vec<Vec3> {
        let mut curve = Vec::new();
        for tri in &mesh.triangles {
            let p = [
                mesh.positions[tri[0] as usize],
                mesh.positions[tri[1] as usize],
                mesh.positions[tri[2] as usize],
            ];
            let d = [
                (p[0] - self.plane_point).dot(self.plane_normal),
                (p[1] - self.plane_point).dot(self.plane_normal),
                (p[2] - self.plane_point).dot(self.plane_normal),
            ];
            for (i, j) in [(0usize, 1usize), (1, 2), (2, 0)] {
                if (d[i] <= 0.0) != (d[j] <= 0.0) {
                    let t = d[i] / (d[i] - d[j]);
                    curve.push(p[i].lerp(p[j], t));
                }
            }
        }
        curve.sort_by(|a, b| a.x.partial_cmp(&b.x).unwrap_or(std::cmp::Ordering::Equal));
        curve
    }

    pub fn sampled_curve(&self, mesh: &SurfaceMesh) -> ProbeSamples {
        let curve = self.intersect(mesh);
        let mut out = ProbeSamples {
            arclength: Vec::new(),
            scalar_a: Vec::new(),
            scalar_b: Vec::new(),
            positions: Vec::new(),
        };
        if curve.len() < 2 {
            return out;
        }
        let x0 = curve.first().unwrap().x;
        let x1 = curve.last().unwrap().x;
        for p in curve {
            out.arclength.push((p.x - x0) / (x1 - x0).max(1.0e-9));
            out.positions.push(p.to_array());
            out.scalar_a.push(mesh.barycentric(p));
            out.scalar_b.push(mesh.barycentric(p));
        }
        out
    }
}

/// Inner-layer quantities used by the u+ plot.
pub struct WallLaw {
    pub kappa: f32,
    pub e: f32,
    pub mu: f32,
    pub rho: f32,
}

impl Default for WallLaw {
    fn default() -> Self {
        Self { kappa: 0.41, e: 9.8, mu: 3.7e-5, rho: 1.08 }
    }
}

impl WallLaw {
    pub fn u_tau(&self, tau_w: f32) -> f32 {
        (tau_w / self.rho).sqrt()
    }
    pub fn y_plus(&self, y: f32, tau_w: f32) -> f32 {
        self.rho * self.u_tau(tau_w) * y / self.mu
    }
    /// Viscous sublayer u+ = y+, logarithmic layer (1/κ) ln(E y+).
    pub fn u_plus(&self, y_plus: f32) -> (f32, f32, f32) {
        let viscous = y_plus;
        let log = (y_plus.max(1.0e-6) * self.e).ln() / self.kappa;
        let blended = if y_plus < 11.2 { viscous } else { log };
        (viscous, log, blended)
    }
    pub fn layer(&self, y_plus: f32) -> Layer {
        if y_plus <= 5.0 {
            Layer::Viscous
        } else if y_plus <= 30.0 {
            Layer::Buffer
        } else {
            Layer::Log
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Layer {
    Viscous,
    Buffer,
    Log,
}

pub fn u_plus_samples() -> Vec<Vec4> {
    // y+ log-spaced from 0.4 to 1e4; w = measured Run A, z unused.
    (0..24)
        .map(|i| {
            let y = 0.4 * 1.28_f32.powi(i);
            Vec4::new(y, y.min(11.2), 0.0, 0.0)
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn grid() -> HexGrid {
        HexGrid {
            origin: Vec3::ZERO,
            spacing: Vec3::splat(1.0),
            dims: [2, 2, 2],
            values: vec![0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0],
        }
    }

    #[test]
    fn trilinear_matches_corners() {
        let g = grid();
        assert_eq!(g.trilinear(Vec3::new(0.0, 0.0, 0.0)), 0.0);
        assert_eq!(g.trilinear(Vec3::new(1.0, 0.0, 0.0)), 1.0);
        let mid = g.trilinear(Vec3::new(0.5, 0.5, 0.5));
        assert!((mid - 0.5).abs() < 1.0e-5);
    }

    #[test]
    fn line_probe_has_monotone_arclength() {
        let g = grid();
        let probe = LineProbe { a: Vec3::ZERO, b: Vec3::new(1.0, 0.0, 0.0), samples: 17 };
        let s = probe.sample(&g, &|p| g.trilinear(p), &|p| g.trilinear(p) * 0.98);
        assert_eq!(s.arclength.len(), 17);
        assert!((s.arclength[16] - 1.0).abs() < 1.0e-6);
        assert!(s.delta()[16] < 0.0);
    }

    #[test]
    fn wall_law_uvisc_and_log() {
        let law = WallLaw::default();
        let (v, _, _) = law.u_plus(3.0);
        assert!((v - 3.0).abs() < 1.0e-6);
        let (_, l, _) = law.u_plus(100.0);
        assert!(l > 10.0);
        assert_eq!(law.layer(0.8), Layer::Viscous);
        assert_eq!(law.layer(30.0), Layer::Buffer);
    }
}
