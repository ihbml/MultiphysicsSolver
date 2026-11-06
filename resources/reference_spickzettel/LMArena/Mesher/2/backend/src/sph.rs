//! Eulerian SPH particle generation.
//!
//! Physics-driven relaxation until sum_j V_j grad W_ij -> 0, detection of
//! particles inside the boundaries via div(r) < 0.75 d, and reconstruction of
//! the FVM-equivalent boundary surface A_iS = -sum_p A_ip
//! (Wang, Haidn & Hu, Comput. Phys. Commun. 307 (2025) 109429).

use rayon::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

use crate::geometry::{self, GeometryId};

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum KernelId {
    Wendland,
    Cubic,
    Quintic,
}

#[derive(Clone, Copy, Debug)]
pub struct Kernel {
    pub id: KernelId,
    pub kappa: f64,
}

impl Kernel {
    pub fn of(id: KernelId) -> Self {
        match id {
            KernelId::Wendland => Self { id, kappa: 2.0 },
            KernelId::Cubic => Self { id, kappa: 2.0 },
            KernelId::Quintic => Self { id, kappa: 3.0 },
        }
    }
    pub fn alpha(&self, h: f64) -> f64 {
        match self.id {
            KernelId::Wendland => 7.0 / (4.0 * std::f64::consts::PI * h * h),
            KernelId::Cubic => 10.0 / (7.0 * std::f64::consts::PI * h * h),
            KernelId::Quintic => 7.0 / (478.0 * std::f64::consts::PI * h * h),
        }
    }
    pub fn w(&self, q: f64) -> f64 {
        match self.id {
            KernelId::Wendland => {
                if q >= 2.0 {
                    0.0
                } else {
                    (1.0 - q / 2.0).powi(4) * (2.0 * q + 1.0)
                }
            }
            KernelId::Cubic => {
                if q >= 2.0 {
                    0.0
                } else if q < 1.0 {
                    1.0 - 1.5 * q * q + 0.75 * q.powi(3)
                } else {
                    0.25 * (2.0 - q).powi(3)
                }
            }
            KernelId::Quintic => {
                if q >= 3.0 {
                    return 0.0;
                }
                let mut v = (3.0 - q).powi(5);
                if q < 2.0 {
                    v -= 6.0 * (2.0 - q).powi(5);
                }
                if q < 1.0 {
                    v += 15.0 * (1.0 - q).powi(5);
                }
                v
            }
        }
    }
    pub fn dw(&self, q: f64) -> f64 {
        match self.id {
            KernelId::Wendland => {
                if q >= 2.0 {
                    0.0
                } else {
                    -5.0 * q * (1.0 - q / 2.0).powi(3)
                }
            }
            KernelId::Cubic => {
                if q >= 2.0 {
                    0.0
                } else if q < 1.0 {
                    -3.0 * q + 2.25 * q * q
                } else {
                    -0.75 * (2.0 - q).powi(2)
                }
            }
            KernelId::Quintic => {
                if q >= 3.0 {
                    return 0.0;
                }
                let mut v = -5.0 * (3.0 - q).powi(4);
                if q < 2.0 {
                    v += 30.0 * (2.0 - q).powi(4);
                }
                if q < 1.0 {
                    v -= 75.0 * (1.0 - q).powi(4);
                }
                v
            }
        }
    }
}

#[derive(Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct SphParams {
    pub dp: f64,
    pub h_factor: f64,
    pub iters: usize,
    pub kernel: KernelId,
    #[serde(default = "one")]
    pub refine_ratio: f64,
    #[serde(default = "default_grading")]
    pub grading: f64,
    #[serde(default = "yes")]
    pub volume_reinit: bool,
    #[serde(default = "hex_default")]
    pub lattice: String,
    /// optional Lagrangian core for the coupled mode: (x, y, r)
    #[serde(default)]
    pub core: Option<[f64; 3]>,
}

fn one() -> f64 {
    1.0
}
fn yes() -> bool {
    true
}
fn default_grading() -> f64 {
    0.6
}
fn hex_default() -> String {
    "square".to_string()
}

#[derive(Serialize, Clone, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct SphDiag {
    pub n: usize,
    pub n_boundary: usize,
    pub eps0_mean: f64,
    pub eps0_max: f64,
    pub eps0_bulk: f64,
    pub eps1_mean: f64,
    pub div_r_mean: f64,
    pub neigh_mean: f64,
    pub neigh_min: usize,
    pub surface_sum: f64,
    pub exact_perimeter: f64,
    pub patch_count: BTreeMap<String, usize>,
    pub max_shift: f64,
    pub ghost_pairs: f64,
    pub pair_reduction: f64,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct SphOut {
    pub n: usize,
    pub x: Vec<f64>,
    pub y: Vec<f64>,
    #[serde(rename = "V")]
    pub volume: Vec<f64>,
    pub h: Vec<f64>,
    pub level: Vec<u8>,
    pub div_r: Vec<f32>,
    pub eps0: Vec<f32>,
    pub eps1: Vec<f32>,
    pub n_neigh: Vec<i32>,
    pub is_bnd: Vec<u8>,
    pub as_mag: Vec<f32>,
    pub as_nx: Vec<f32>,
    pub as_ny: Vec<f32>,
    pub patch: Vec<i8>,
    pub diag: SphDiag,
    pub duration_ms: f64,
}

pub struct Grid {
    cell: f64,
    x0: f64,
    y0: f64,
    nx: i32,
    ny: i32,
    head: Vec<i32>,
    next: Vec<i32>,
}

impl Grid {
    pub fn new(x: &[f64], y: &[f64], cell: f64, bbox: [f64; 4]) -> Self {
        let pad = 4.0 * cell;
        let x0 = bbox[0] - pad;
        let y0 = bbox[1] - pad;
        let nx = (((bbox[2] - bbox[0] + 2.0 * pad) / cell).ceil() as i32).max(1);
        let ny = (((bbox[3] - bbox[1] + 2.0 * pad) / cell).ceil() as i32).max(1);
        let mut head = vec![-1i32; (nx * ny) as usize];
        let mut next = vec![-1i32; x.len()];
        for i in 0..x.len() {
            let cx = (((x[i] - x0) / cell).floor() as i32).clamp(0, nx - 1);
            let cy = (((y[i] - y0) / cell).floor() as i32).clamp(0, ny - 1);
            let c = (cy * nx + cx) as usize;
            next[i] = head[c];
            head[c] = i as i32;
        }
        Self { cell, x0, y0, nx, ny, head, next }
    }
    pub fn for_each<F: FnMut(usize)>(&self, px: f64, py: f64, radius: f64, mut cb: F) {
        let r = (radius / self.cell).ceil() as i32;
        let cx = ((px - self.x0) / self.cell).floor() as i32;
        let cy = ((py - self.y0) / self.cell).floor() as i32;
        for j in (cy - r)..=(cy + r) {
            if j < 0 || j >= self.ny {
                continue;
            }
            for i in (cx - r)..=(cx + r) {
                if i < 0 || i >= self.nx {
                    continue;
                }
                let mut p = self.head[(j * self.nx + i) as usize];
                while p != -1 {
                    cb(p as usize);
                    p = self.next[p as usize];
                }
            }
        }
    }
}

fn target_spacing(id: GeometryId, p: &SphParams, x: f64, y: f64) -> f64 {
    if p.refine_ratio <= 1.0 {
        return p.dp;
    }
    let dp_min = p.dp / p.refine_ratio;
    let d = geometry::refine_dist(id, x, y).abs();
    (dp_min * (1.0 + p.grading * (d / dp_min))).min(p.dp)
}

pub fn run(id: GeometryId, p: &SphParams) -> SphOut {
    let t0 = std::time::Instant::now();
    let kern = Kernel::of(p.kernel);
    let bbox = geometry::bbox(id);
    let [bx0, by0, bx1, by1] = bbox;
    let max_level = (p.refine_ratio.max(1.0).log2().round() as i32).max(0);
    let mut xs: Vec<f64> = Vec::new();
    let mut ys: Vec<f64> = Vec::new();
    let mut dp: Vec<f64> = Vec::new();
    for l in 0..=max_level {
        let s = p.dp / 2f64.powi(l);
        let row_h = if p.lattice == "hex" { s * (3.0_f64).sqrt() / 2.0 } else { s };
        let n_rows = ((by1 - by0) / row_h).ceil() as i32;
        let n_cols = ((bx1 - bx0) / s).ceil() as i32;
        for j in 0..=n_rows {
            let yy = by0 + (j as f64 + 0.5) * row_h;
            if yy > by1 {
                continue;
            }
            for i in 0..=n_cols {
                let xx = bx0
                    + (i as f64 + 0.5) * s
                    + if p.lattice == "hex" && j % 2 == 1 { s / 2.0 } else { 0.0 };
                if xx > bx1 || geometry::sdf(id, xx, yy) > -0.5 * s {
                    continue;
                }
                if let Some([cx, cy, cr]) = p.core {
                    if ((xx - cx).powi(2) + (yy - cy).powi(2)).sqrt() >= cr {
                        continue;
                    }
                }
                let t = target_spacing(id, p, xx, yy);
                let lvl = (p.dp / t).log2().round() as i32;
                if lvl != l {
                    continue;
                }
                xs.push(xx);
                ys.push(yy);
                dp.push(s);
            }
        }
    }
    let mut h: Vec<f64> = dp.iter().map(|d| p.h_factor * d).collect();
    let mut vol: Vec<f64> = dp
        .iter()
        .map(|s| {
            if p.lattice == "hex" {
                s * s * (3.0_f64).sqrt() / 2.0
            } else {
                s * s
            }
        })
        .collect();
    let mut level: Vec<u8> = vec![0; xs.len()];
    let mut iter = 0usize;
    let n = xs.len();
    let max_support = h.iter().cloned().fold(0.0, f64::max) * kern.kappa;
    let deps = 1e-4 * p.dp;
    let mut max_shift = 1.0;

    while iter < p.iters {
        let grid = Grid::new(&xs, &ys, max_support.max(1e-9), bbox);
        let mut dx = vec![0.0; n];
        let mut dy = vec![0.0; n];
        for i in 0..n {
            let mut ax = 0.0;
            let mut ay = 0.0;
            let (xi, yi, hi) = (xs[i], ys[i], h[i]);
            grid.for_each(xi, yi, max_support, |j| {
                if j == i {
                    return;
                }
                let ddx = xi - xs[j];
                let ddy = yi - ys[j];
                let r = (ddx * ddx + ddy * ddy).sqrt();
                let hij = 0.5 * (hi + h[j]);
                if r >= kern.kappa * hij || r < 1e-12 {
                    return;
                }
                let dw = kern.alpha(hij) / hij * kern.dw(r / hij);
                ax += vol[j] * dw * ddx / r;
                ay += vol[j] * dw * ddy / r;
            });
            let mut mx = -0.2 * hi * hi * ax;
            let mut my = -0.2 * hi * hi * ay;
            let cap = 0.15 * dp[i];
            let mv = (mx * mx + my * my).sqrt();
            if mv > cap {
                mx *= cap / mv;
                my *= cap / mv;
            }
            dx[i] = mx;
            dy[i] = my;
        }
        let mut shift = 0.0;
        for i in 0..n {
            xs[i] += dx[i];
            ys[i] += dy[i];
            let d = geometry::sdf(id, xs[i], ys[i]);
            let target = -0.5 * dp[i];
            if d > target {
                let (gx, gy) = geometry::sdf_grad(id, xs[i], ys[i], deps);
                let g2 = (gx * gx + gy * gy).max(1e-30);
                xs[i] -= (d - target) * gx / g2;
                ys[i] -= (d - target) * gy / g2;
            }
            shift = shift.max((dx[i] * dx[i] + dy[i] * dy[i]).sqrt() / dp[i]);
        }
        max_shift = shift;
        iter += 1;
        if p.volume_reinit && iter % 5 == 0 {
            reinit_volumes(&xs, &ys, &h, &dp, &mut vol, kern);
        }
    }
    reinit_volumes(&xs, &ys, &h, &dp, &mut vol, kern);

    /* ------------------ diagnostics + boundary surfaces ------------------ */
    let grid = Grid::new(&xs, &ys, max_support.max(1e-9), bbox);
    struct Row {
        div_r: f32,
        eps0: f32,
        eps1: f32,
        cnt: i32,
        bnd: bool,
        mag: f32,
        nx: f32,
        ny: f32,
        patch: i8,
    }
    let rows: Vec<Row> = (0..n)
        .into_par_iter()
        .map(|i| {
            let mut gx = 0.0;
            let mut gy = 0.0;
            let mut dr = 0.0;
            let mut cnt = 0;
            let mut m = [0.0f64; 4];
            grid.for_each(xs[i], ys[i], max_support, |j| {
                if j == i {
                    return;
                }
                let ddx = xs[i] - xs[j];
                let ddy = ys[i] - ys[j];
                let r = (ddx * ddx + ddy * ddy).sqrt();
                let hij = 0.5 * (h[i] + h[j]);
                if r >= kern.kappa * hij || r < 1e-12 {
                    return;
                }
                let dw = kern.alpha(hij) / hij * kern.dw(r / hij);
                let (ex, ey) = (ddx / r, ddy / r);
                gx += vol[j] * dw * ex;
                gy += vol[j] * dw * ey;
                dr += -vol[j] * r * dw;
                m[0] += -vol[j] * ddx * dw * ex;
                m[1] += -vol[j] * ddx * dw * ey;
                m[2] += -vol[j] * ddy * dw * ex;
                m[3] += -vol[j] * ddy * dw * ey;
                cnt += 1;
            });
            let bnd = dr < 1.5;
            let (mut mag, mut nx, mut ny, mut patch) = (0.0f32, 0.0f32, 0.0f32, -1i8);
            if bnd {
                let ax = -2.0 * vol[i] * gx;
                let ay = -2.0 * vol[i] * gy;
                let a = (ax * ax + ay * ay).sqrt();
                mag = a as f32;
                if a > 1e-30 {
                    nx = (ax / a) as f32;
                    ny = (ay / a) as f32;
                }
                let (px, py) = geometry::project(id, xs[i], ys[i], 0.0, deps);
                patch = geometry::patch_at(id, px, py).idx();
            }
            Row {
                div_r: dr as f32,
                eps0: ((gx * gx + gy * gy).sqrt() * h[i]) as f32,
                eps1: ((m[0] - 1.0).powi(2) + m[1] * m[1] + m[2] * m[2] + (m[3] - 1.0).powi(2))
                    .sqrt() as f32,
                cnt,
                bnd,
                mag,
                nx,
                ny,
                patch,
            }
        })
        .collect();

    let mut div_r = Vec::with_capacity(n);
    let mut eps0 = Vec::with_capacity(n);
    let mut eps1 = Vec::with_capacity(n);
    let mut n_neigh = Vec::with_capacity(n);
    let mut is_bnd = Vec::with_capacity(n);
    let mut as_mag = Vec::with_capacity(n);
    let mut as_nx = Vec::with_capacity(n);
    let mut as_ny = Vec::with_capacity(n);
    let mut patch = Vec::with_capacity(n);
    let mut patch_count: BTreeMap<String, usize> = BTreeMap::new();
    let mut nb = 0usize;
    let mut eps0_sum = 0.0;
    let mut eps0_max = 0.0f64;
    let mut eps0_bulk = 0.0;
    let mut bulk_cnt = 0usize;
    let mut eps1_sum = 0.0;
    let mut div_sum = 0.0;
    let mut neigh_sum = 0i64;
    let mut neigh_min = usize::MAX;
    let mut surface_sum = 0.0;
    let mut bnd_neigh = 0i64;
    for (i, r) in rows.iter().enumerate() {
        div_r.push(r.div_r);
        eps0.push(r.eps0);
        eps1.push(r.eps1);
        n_neigh.push(r.cnt);
        is_bnd.push(r.bnd as u8);
        as_mag.push(r.mag);
        as_nx.push(r.nx);
        as_ny.push(r.ny);
        patch.push(r.patch);
        eps0_sum += r.eps0 as f64;
        eps0_max = eps0_max.max(r.eps0 as f64);
        eps1_sum += r.eps1 as f64;
        div_sum += r.div_r as f64;
        neigh_sum += r.cnt as i64;
        neigh_min = neigh_min.min(r.cnt as usize);
        if r.bnd {
            nb += 1;
            surface_sum += r.mag as f64;
            bnd_neigh += r.cnt as i64;
            let key = match r.patch {
                0 => "inlet",
                1 => "outlet",
                2 => "wall",
                3 => "body",
                4 => "farfield",
                5 => "symmetry",
                _ => "wall",
            };
            *patch_count.entry(key.to_string()).or_insert(0) += 1;
        } else {
            eps0_bulk += r.eps0 as f64;
            bulk_cnt += 1;
        }
        let _ = i;
    }
    let bulk_mean = if bulk_cnt > 0 {
        (neigh_sum - bnd_neigh) as f64 / bulk_cnt as f64
    } else {
        0.0
    };
    let mut ghost = 0.0;
    for i in 0..n {
        if is_bnd[i] == 1 {
            ghost += (bulk_mean - n_neigh[i] as f64).max(0.0);
        }
    }
    let diag = SphDiag {
        n,
        n_boundary: nb,
        eps0_mean: eps0_sum / n as f64,
        eps0_max,
        eps0_bulk: eps0_bulk / bulk_cnt.max(1) as f64,
        eps1_mean: eps1_sum / n as f64,
        div_r_mean: div_sum / n as f64,
        neigh_mean: neigh_sum as f64 / n as f64,
        neigh_min: if neigh_min == usize::MAX { 0 } else { neigh_min },
        surface_sum,
        exact_perimeter: geometry::perimeter(id),
        patch_count,
        max_shift,
        ghost_pairs: ghost,
        pair_reduction: 100.0 * ghost / (neigh_sum as f64).max(1.0),
    };
    SphOut {
        n,
        x: xs,
        y: ys,
        volume: vol,
        h,
        level: {
            for (i, d) in dp.iter().enumerate() {
                level[i] = (p.dp / d).log2().round().max(0.0) as u8;
            }
            level
        },
        div_r,
        eps0,
        eps1,
        n_neigh,
        is_bnd,
        as_mag,
        as_nx,
        as_ny,
        patch,
        diag,
        duration_ms: t0.elapsed().as_secs_f64() * 1e3,
    }
}

fn reinit_volumes(
    x: &[f64],
    y: &[f64],
    h: &[f64],
    dp: &[f64],
    vol: &mut [f64],
    kern: Kernel,
) {
    let n = x.len();
    let max_support = h.iter().cloned().fold(0.0, f64::max) * kern.kappa;
    let bbox = [
        x.iter().cloned().fold(f64::INFINITY, f64::min),
        y.iter().cloned().fold(f64::INFINITY, f64::min),
        x.iter().cloned().fold(f64::NEG_INFINITY, f64::max),
        y.iter().cloned().fold(f64::NEG_INFINITY, f64::max),
    ];
    let grid = Grid::new(x, y, max_support.max(1e-9), bbox);
    let sigma: Vec<f64> = (0..n)
        .map(|i| {
            let mut s = 0.0;
            grid.for_each(x[i], y[i], max_support, |j| {
                let r = ((x[i] - x[j]).powi(2) + (y[i] - y[j]).powi(2)).sqrt();
                let hij = 0.5 * (h[i] + h[j]);
                if r < kern.kappa * hij {
                    s += kern.alpha(hij) * kern.w(r / hij);
                }
            });
            s
        })
        .collect();
    for i in 0..n {
        let v0 = dp[i] * dp[i];
        let v = 1.0 / sigma[i].max(1e-30);
        vol[i] = v.clamp(0.6 * v0, 1.4 * v0);
    }
}
