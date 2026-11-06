//! Adaptive SPH-FVM coupling: region bookkeeping and the optimized
//! particle-mesh interpolation scheme (OPMIS).
//!
//! Reference: Zhang, Ming, Wang & Liang, Int. J. Multiphase Flow 189 (2025)
//! 105256. OPMIS is a limited MLS operator: first-order reproduction where
//! the moment matrix is well conditioned, relaxed towards Shepard where
//! negative weights would otherwise destabilise a passing strong wave.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

use crate::geometry::GeometryId;
use crate::mesh::MeshOut;
use crate::sph::{Grid, Kernel, SphOut};

#[derive(Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct CoreReq {
    pub x: f64,
    pub y: f64,
    pub r: f64,
    pub buffer: f64,
}

#[derive(Serialize, Clone, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct SchemeStats {
    pub n_targets: usize,
    pub n_empty: usize,
    pub stencil_mean: f64,
    pub c0_max: f64,
    pub c1_mean: f64,
    pub c1_max: f64,
    pub w_min: f64,
    pub cond_mean: f64,
    pub linear_err: f64,
    pub shock_overshoot: f64,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct CoupleSummary {
    pub n_particles: usize,
    pub n_full: usize,
    pub n_buffer: usize,
    pub n_interface: usize,
    pub n_covered: usize,
    pub n_prunable: usize,
    pub p2m: BTreeMap<String, SchemeStats>,
    pub m2p: BTreeMap<String, SchemeStats>,
    pub duration_ms: f64,
}

pub fn classify_cells(cc: &[f64], core: &CoreReq, band: f64) -> Vec<u8> {
    (0..cc.len() / 2)
        .map(|c| {
            let r = ((cc[2 * c] - core.x).powi(2) + (cc[2 * c + 1] - core.y).powi(2)).sqrt();
            if r < core.r - band {
                2
            } else if r < core.r {
                1
            } else {
                0
            }
        })
        .collect()
}

pub fn classify_particles(s: &SphOut, core: &CoreReq, band: f64) -> Vec<u8> {
    (0..s.n)
        .map(|i| {
            let r = ((s.x[i] - core.x).powi(2) + (s.y[i] - core.y).powi(2)).sqrt();
            if r > core.r + band {
                2
            } else if r > core.r - band {
                1
            } else {
                0
            }
        })
        .collect()
}

fn inv3(m: &[f64; 9]) -> Option<[f64; 9]> {
    let a = m[4] * m[8] - m[5] * m[7];
    let b = -(m[3] * m[8] - m[5] * m[6]);
    let c = m[3] * m[7] - m[4] * m[6];
    let det = m[0] * a + m[1] * b + m[2] * c;
    if !det.is_finite() || det.abs() < 1e-14 {
        return None;
    }
    let id = 1.0 / det;
    Some([
        a * id,
        (m[2] * m[7] - m[1] * m[8]) * id,
        (m[1] * m[5] - m[2] * m[4]) * id,
        b * id,
        (m[0] * m[8] - m[2] * m[6]) * id,
        (m[2] * m[3] - m[0] * m[5]) * id,
        c * id,
        (m[1] * m[6] - m[0] * m[7]) * id,
        (m[0] * m[4] - m[1] * m[3]) * id,
    ])
}

#[allow(clippy::too_many_arguments)]
fn interp(
    tx: &[f64],
    ty: &[f64],
    targets: &[usize],
    radius: f64,
    src_x: &[f64],
    src_y: &[f64],
    grid: &Grid,
    kern: Kernel,
    scheme: &str,
) -> SchemeStats {
    let h = radius / kern.kappa;
    let (mut empty, mut s_mean, mut c0_max, mut c1_sum, mut c1_max): (usize, f64, f64, f64, f64) =
        (0, 0.0, 0.0, 0.0, 0.0);
    let (mut w_min, mut cond_sum, mut lin_err, mut over) = (0.0f64, 0.0, 0.0, 0.0);
    for &t in targets {
        let (px, py) = (tx[t], ty[t]);
        let mut idx: Vec<usize> = Vec::new();
        let mut wk: Vec<f64> = Vec::new();
        grid.for_each(px, py, radius, |j| {
            let r = ((src_x[j] - px).powi(2) + (src_y[j] - py).powi(2)).sqrt();
            if r >= radius {
                return;
            }
            let w = kern.alpha(h) * kern.w(r / h);
            if w > 0.0 {
                idx.push(j);
                wk.push(w);
            }
        });
        let m = idx.len();
        if m == 0 {
            empty += 1;
            continue;
        }
        s_mean += m as f64;
        let sw: f64 = wk.iter().sum();
        let mut wf: Vec<f64> = wk.iter().map(|w| w / sw).collect();
        let mut cond = 1.0;
        if scheme != "shepard" && m >= 4 {
            let mut mm = [0.0f64; 9];
            let mut p = vec![[0.0f64; 3]; m];
            for k in 0..m {
                p[k] = [
                    1.0,
                    (src_x[idx[k]] - px) / h,
                    (src_y[idx[k]] - py) / h,
                ];
                for a in 0..3 {
                    for b in 0..3 {
                        mm[3 * a + b] += wk[k] * p[k][a] * p[k][b];
                    }
                }
            }
            if let Some(mi) = inv3(&mm) {
                let fro = |a: &[f64; 9]| a.iter().map(|v| v * v).sum::<f64>().sqrt();
                cond = fro(&mm) * fro(&mi);
                let mls: Vec<f64> = (0..m)
                    .map(|k| {
                        (mi[0] * p[k][0] + mi[1] * p[k][1] + mi[2] * p[k][2]) * wk[k]
                    })
                    .collect();
                if scheme == "mls" {
                    wf = mls;
                } else {
                    let neg: f64 = mls.iter().filter(|w| **w < 0.0).map(|w| -w).sum();
                    let cond_fac = (1.0 - (cond - 12.0) / 60.0).max(0.0);
                    let neg_fac = (1.0 - neg / 0.25).max(0.0);
                    let theta = cond_fac.min(neg_fac).clamp(0.0, 1.0);
                    let shepard = wf.clone();
                    wf = (0..m)
                        .map(|k| theta * mls[k] + (1.0 - theta) * shepard[k])
                        .collect();
                }
            }
        }
        let (mut s0, mut s1x, mut s1y, mut l1, mut fl, mut mn) =
            (0.0, 0.0, 0.0, 0.0, 0.0, f64::INFINITY);
        for k in 0..m {
            let w = wf[k];
            s0 += w;
            l1 += w.abs();
            s1x += w * (src_x[idx[k]] - px) / h;
            s1y += w * (src_y[idx[k]] - py) / h;
            mn = mn.min(w);
            fl += w * (1.0 + 2.0 * src_x[idx[k]] - 1.5 * src_y[idx[k]]);
        }
        c0_max = c0_max.max((s0 - 1.0).abs());
        let c1 = (s1x * s1x + s1y * s1y).sqrt();
        c1_sum += c1;
        c1_max = c1_max.max(c1);
        w_min = w_min.min(mn);
        cond_sum += cond;
        lin_err = lin_err.max((fl - (1.0 + 2.0 * px - 1.5 * py)).abs() / 3.0);
        over = over.max(((l1 - s0) / 2.0).max(0.0));
    }
    let nt = targets.len().max(1) as f64;
    SchemeStats {
        n_targets: targets.len(),
        n_empty: empty,
        stencil_mean: if targets.is_empty() { 0.0 } else { s_mean / nt },
        c0_max,
        c1_mean: if targets.is_empty() { 0.0 } else { c1_sum / nt },
        c1_max,
        w_min: if w_min.is_finite() { w_min } else { 0.0 },
        cond_mean: if targets.is_empty() { 0.0 } else { cond_sum / nt },
        linear_err: lin_err,
        shock_overshoot: 100.0 * over,
    }
}

pub fn summary(
    id: GeometryId,
    mesh: &MeshOut,
    sph: &SphOut,
    core: &CoreReq,
    dp: f64,
    h_factor: f64,
    kernel: KernelId,
    scheme: &str,
) -> CoupleSummary {
    let t0 = std::time::Instant::now();
    let kern = Kernel::of(kernel);
    let band = core.buffer * dp;
    let cells = classify_cells(&mesh.cc, core, band);
    let parts = classify_particles(sph, core, band);
    let cell_targets: Vec<usize> = (0..mesh.n_cells).filter(|&c| cells[c] == 1).collect();
    let part_targets: Vec<usize> = (0..sph.n).filter(|&i| parts[i] == 1).collect();
    let n_covered = cells.iter().filter(|&&c| c == 2).count();
    let n_prunable = parts.iter().filter(|&&c| c == 2).count();

    let mean_cell = mesh.stats.mean_vol.sqrt();
    let h_part = h_factor * dp;
    let bbox = geometry_bbox(id);
    let p_grid = Grid::new(&sph.x, &sph.y, (kern.kappa * h_part).max(1e-9), bbox);
    let cx: Vec<f64> = (0..mesh.n_cells).map(|c| mesh.cc[2 * c]).collect();
    let cy: Vec<f64> = (0..mesh.n_cells).map(|c| mesh.cc[2 * c + 1]).collect();
    let c_grid = Grid::new(&cx, &cy, mean_cell.max(1e-9), bbox);

    let r_p2m = kern.kappa * h_part;
    let r_m2p = (kern.kappa * h_part).max(2.6 * mean_cell);
    let mut p2m = BTreeMap::new();
    let mut m2p = BTreeMap::new();
    for s in ["shepard", "mls", "opmis"] {
        p2m.insert(
            s.to_string(),
            interp(&cx, &cy, &cell_targets, r_p2m, &sph.x, &sph.y, &p_grid, kern, s),
        );
        m2p.insert(
            s.to_string(),
            interp(&sph.x, &sph.y, &part_targets, r_m2p, &cx, &cy, &c_grid, kern, s),
        );
    }
    let _ = scheme;
    let [x0, y0, x1, y1] = geometry_bbox(id);
    let n_full = (((x1 - x0) * (y1 - y0)) / (dp * dp)).round() as usize;
    CoupleSummary {
        n_particles: sph.n,
        n_full,
        n_buffer: part_targets.len(),
        n_interface: cell_targets.len(),
        n_covered,
        n_prunable,
        p2m,
        m2p,
        duration_ms: t0.elapsed().as_secs_f64() * 1e3,
    }
}

use crate::sph::KernelId;

fn geometry_bbox(id: GeometryId) -> [f64; 4] {
    crate::geometry::bbox(id)
}
