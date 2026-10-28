//! Level-set geometry catalogue.
//!
//! Every domain is a signed distance function phi(x, y) < 0 inside; this is the
//! single source of truth for the mesher (boundary projection), the particle
//! relaxation (surface bounding) and the coupling-interface bookkeeping.

use serde::Deserialize;
use std::sync::OnceLock;

#[derive(Deserialize, Clone, Copy, Debug, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum GeometryId {
    Cylinder,
    Step,
    Airfoil,
    Nozzle,
    Cavity,
    Bubble,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Patch {
    Inlet,
    Outlet,
    Wall,
    Body,
    Farfield,
    Symmetry,
}

impl Patch {
    pub fn idx(self) -> i8 {
        match self {
            Patch::Inlet => 0,
            Patch::Outlet => 1,
            Patch::Wall => 2,
            Patch::Body => 3,
            Patch::Farfield => 4,
            Patch::Symmetry => 5,
        }
    }
    pub fn key(self) -> &'static str {
        match self {
            Patch::Inlet => "inlet",
            Patch::Outlet => "outlet",
            Patch::Wall => "wall",
            Patch::Body => "body",
            Patch::Farfield => "farfield",
            Patch::Symmetry => "symmetry",
        }
    }
}

pub fn bbox(id: GeometryId) -> [f64; 4] {
    match id {
        GeometryId::Cylinder => [0.0, 0.0, 4.0, 1.6],
        GeometryId::Step => [0.0, 0.0, 4.0, 1.0],
        GeometryId::Airfoil => [-1.2, -1.1, 2.4, 1.1],
        GeometryId::Nozzle => [0.0, -0.62, 3.0, 0.62],
        GeometryId::Cavity => [0.0, 0.0, 1.0, 1.0],
        GeometryId::Bubble => [0.0, 0.0, 3.0, 3.0],
    }
}

pub fn core(id: GeometryId) -> (f64, f64, f64) {
    match id {
        GeometryId::Cylinder => (1.1, 0.8, 0.62),
        GeometryId::Step => (1.35, 0.45, 0.55),
        GeometryId::Airfoil => (0.35, 0.0, 0.85),
        GeometryId::Nozzle => (1.3, 0.0, 0.5),
        GeometryId::Cavity => (0.5, 0.5, 0.34),
        GeometryId::Bubble => (1.5, 1.5, 0.95),
    }
}

pub fn naca_polygon() -> &'static Vec<f64> {
    static POLY: OnceLock<Vec<f64>> = OnceLock::new();
    POLY.get_or_init(|| {
        let (thickness, camber, camber_pos, alpha) = (0.12_f64, 0.02_f64, 0.4_f64, 8.0_f64);
        let ca = (-alpha * std::f64::consts::PI / 180.0).cos();
        let sa = (-alpha * std::f64::consts::PI / 180.0).sin();
        let yt = |xc: f64| {
            (thickness / 0.2)
                * (0.2969 * xc.sqrt()
                    - 0.126 * xc
                    - 0.3516 * xc * xc
                    + 0.2843 * xc.powi(3)
                    - 0.1036 * xc.powi(4))
        };
        let camber_line = |xc: f64| -> (f64, f64) {
            if camber == 0.0 {
                return (0.0, 0.0);
            }
            let p = camber_pos;
            if xc < p {
                (
                    (camber / (p * p)) * (2.0 * p * xc - xc * xc),
                    (2.0 * camber / (p * p)) * (p - xc),
                )
            } else {
                (
                    (camber / ((1.0 - p) * (1.0 - p)))
                        * (1.0 - 2.0 * p + 2.0 * p * xc - xc * xc),
                    (2.0 * camber / ((1.0 - p) * (1.0 - p))) * (p - xc),
                )
            }
        };
        let n = 140usize;
        let mut out = Vec::with_capacity(2 * n);
        let mut push = |xc: f64, upper: bool| {
            let t = yt(xc);
            let (yc, dyc) = camber_line(xc);
            let th = dyc.atan();
            let xx = if upper {
                xc - t * th.sin()
            } else {
                xc + t * th.sin()
            };
            let yy = if upper {
                yc + t * th.cos()
            } else {
                yc - t * th.cos()
            };
            let sx = xx - 0.3;
            out.push(sx * ca - yy * sa + 0.3);
            out.push(sx * sa + yy * ca);
        };
        for i in 0..=n {
            let beta = std::f64::consts::PI * i as f64 / n as f64;
            push(0.5 * (1.0 - beta.cos()), true);
        }
        for i in (1..n).rev() {
            let beta = std::f64::consts::PI * i as f64 / n as f64;
            push(0.5 * (1.0 - beta.cos()), false);
        }
        out
    })
}

fn d_box(x: f64, y: f64, x0: f64, y0: f64, x1: f64, y1: f64) -> f64 {
    let dx = (x0 - x).max(x - x1);
    let dy = (y0 - y).max(y - y1);
    let outside = (dx.max(0.0).powi(2) + dy.max(0.0).powi(2)).sqrt();
    outside + dx.max(dy).min(0.0)
}

fn d_circle(x: f64, y: f64, cx: f64, cy: f64, r: f64) -> f64 {
    ((x - cx).powi(2) + (y - cy).powi(2)).sqrt() - r
}

fn d_segment(px: f64, py: f64, ax: f64, ay: f64, bx: f64, by: f64) -> f64 {
    let vx = bx - ax;
    let vy = by - ay;
    let wx = px - ax;
    let wy = py - ay;
    let l2 = vx * vx + vy * vy;
    let t = if l2 <= 1e-30 {
        0.0
    } else {
        ((wx * vx + wy * vy) / l2).clamp(0.0, 1.0)
    };
    ((wx - t * vx).powi(2) + (wy - t * vy).powi(2)).sqrt()
}

pub fn d_polygon(x: f64, y: f64, poly: &[f64]) -> f64 {
    let n = poly.len() / 2;
    let mut best = f64::INFINITY;
    let mut inside = false;
    let mut j = n - 1;
    for i in 0..n {
        let (ax, ay) = (poly[2 * i], poly[2 * i + 1]);
        let (bx, by) = (poly[2 * j], poly[2 * j + 1]);
        best = best.min(d_segment(x, y, ax, ay, bx, by));
        if (ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay + 1e-30) + ax {
            inside = !inside;
        }
        j = i;
    }
    if inside {
        -best
    } else {
        best
    }
}

fn nozzle_r(x: f64) -> f64 {
    0.22 + 0.055 * (x - 1.3).powi(2) + 0.02 * x
}

pub fn sdf(id: GeometryId, x: f64, y: f64) -> f64 {
    match id {
        GeometryId::Cylinder => {
            d_box(x, y, 0.0, 0.0, 4.0, 1.6).max(-d_circle(x, y, 1.1, 0.8, 0.25))
        }
        GeometryId::Step => {
            d_box(x, y, 0.0, 0.0, 4.0, 1.0).max(-d_box(x, y, -1.0, -1.0, 1.0, 0.4))
        }
        GeometryId::Airfoil => d_box(x, y, -1.2, -1.1, 2.4, 1.1)
            .max(-d_polygon(x, y, naca_polygon())),
        GeometryId::Nozzle => (y.abs() - nozzle_r(x)).max(-x).max(x - 3.0),
        GeometryId::Cavity => d_box(x, y, 0.0, 0.0, 1.0, 1.0),
        GeometryId::Bubble => d_box(x, y, 0.0, 0.0, 3.0, 3.0),
    }
}

pub fn refine_dist(id: GeometryId, x: f64, y: f64) -> f64 {
    match id {
        GeometryId::Cylinder => d_circle(x, y, 1.1, 0.8, 0.25)
            .abs()
            .min(d_box(x, y, 0.0, 0.0, 4.0, 1.6).abs() * 1.6),
        GeometryId::Step => d_box(x, y, -1.0, -1.0, 1.0, 0.4)
            .abs()
            .min(d_box(x, y, 0.0, 0.0, 4.0, 1.0).abs() * 2.0),
        GeometryId::Airfoil => d_polygon(x, y, naca_polygon()).abs(),
        GeometryId::Nozzle => (y.abs() - nozzle_r(x)).abs(),
        GeometryId::Cavity => d_box(x, y, 0.0, 0.0, 1.0, 1.0).abs(),
        GeometryId::Bubble => d_circle(x, y, 1.5, 1.5, 0.45)
            .abs()
            .min(d_box(x, y, 0.0, 0.0, 3.0, 3.0).abs() * 2.2),
    }
}

pub fn patch_at(id: GeometryId, x: f64, y: f64) -> Patch {
    let [x0, y0, x1, y1] = bbox(id);
    let mut best = (f64::INFINITY, Patch::Wall);
    let mut consider = |d: f64, p: Patch| {
        if d < best.0 {
            best = (d, p);
        }
    };
    match id {
        GeometryId::Cylinder => {
            consider(x.abs(), Patch::Inlet);
            consider((x1 - x).abs(), Patch::Outlet);
            consider(y.abs(), Patch::Wall);
            consider((y1 - y).abs(), Patch::Wall);
            consider(d_circle(x, y, 1.1, 0.8, 0.25).abs(), Patch::Body);
        }
        GeometryId::Step => {
            let dx = if y > 0.4 { x.abs() } else { f64::INFINITY };
            consider(dx, Patch::Inlet);
            consider((x1 - x).abs(), Patch::Outlet);
            consider(y.abs(), Patch::Wall);
            consider((y1 - y).abs(), Patch::Wall);
            consider(d_box(x, y, -1.0, -1.0, 1.0, 0.4).abs(), Patch::Wall);
        }
        GeometryId::Airfoil => {
            consider((x - x0).abs(), Patch::Inlet);
            consider((x1 - x).abs(), Patch::Outlet);
            consider((y - y0).abs(), Patch::Farfield);
            consider((y1 - y).abs(), Patch::Farfield);
            consider(d_polygon(x, y, naca_polygon()).abs(), Patch::Body);
        }
        GeometryId::Nozzle => {
            consider(x.abs(), Patch::Inlet);
            consider((x1 - x).abs(), Patch::Outlet);
            consider((y.abs() - nozzle_r(x)).abs(), Patch::Wall);
        }
        GeometryId::Cavity => {
            consider((y1 - y).abs(), Patch::Inlet);
            consider(x.abs(), Patch::Wall);
            consider((x1 - x).abs(), Patch::Wall);
            consider(y.abs(), Patch::Wall);
        }
        GeometryId::Bubble => return Patch::Farfield,
    }
    best.1
}

pub fn perimeter(id: GeometryId) -> f64 {
    match id {
        GeometryId::Cylinder => 2.0 * (4.0 + 1.6) + 2.0 * std::f64::consts::PI * 0.25,
        GeometryId::Step => 2.0 * 4.0 + 2.0 * 1.0,
        GeometryId::Airfoil => {
            let poly = naca_polygon();
            let n = poly.len() / 2;
            let mut p = 0.0;
            let mut j = n - 1;
            for i in 0..n {
                p += ((poly[2 * i] - poly[2 * j]).powi(2)
                    + (poly[2 * i + 1] - poly[2 * j + 1]).powi(2))
                .sqrt();
                j = i;
            }
            2.0 * (2.4 + 1.2 + 2.2) + p
        }
        GeometryId::Nozzle => {
            let mut p = 2.0 * nozzle_r(0.0) + 2.0 * nozzle_r(3.0);
            let n = 400;
            for i in 0..n {
                let xa = 3.0 * i as f64 / n as f64;
                let xb = 3.0 * (i + 1) as f64 / n as f64;
                p += 2.0 * ((xb - xa).powi(2) + (nozzle_r(xb) - nozzle_r(xa)).powi(2)).sqrt();
            }
            p
        }
        GeometryId::Cavity => 4.0,
        GeometryId::Bubble => 12.0,
    }
}

pub fn fixed_points(id: GeometryId) -> Vec<[f64; 2]> {
    let [x0, y0, x1, y1] = bbox(id);
    let mut v = vec![[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    if id == GeometryId::Step {
        v.push([0.0, 0.4]);
        v.push([1.0, 0.4]);
        v.push([1.0, 0.0]);
    }
    v
}

/// newton projection onto phi = target
pub fn project(id: GeometryId, x: f64, y: f64, target: f64, eps: f64) -> (f64, f64) {
    let mut px = x;
    let mut py = y;
    for _ in 0..2 {
        let d = sdf(id, px, py) - target;
        let gx = (sdf(id, px + eps, py) - sdf(id, px - eps, py)) / (2.0 * eps);
        let gy = (sdf(id, px, py + eps) - sdf(id, px, py - eps)) / (2.0 * eps);
        let g2 = gx * gx + gy * gy;
        if g2 < 1e-20 {
            break;
        }
        px -= d * gx / g2;
        py -= d * gy / g2;
    }
    (px, py)
}

pub fn sdf_grad(id: GeometryId, x: f64, y: f64, eps: f64) -> (f64, f64) {
    (
        (sdf(id, x + eps, y) - sdf(id, x - eps, y)) / (2.0 * eps),
        (sdf(id, x, y + eps) - sdf(id, x, y - eps)) / (2.0 * eps),
    )
}
