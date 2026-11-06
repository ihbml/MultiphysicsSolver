//! Unstructured body-fitted mesher (DistMesh-style force equilibrium on a
//! Delaunay triangulation) and the cell-centred finite-volume topology.
//!
//! Hot loops are parallelised with rayon; the face list (owner / neighbour /
//! Sf) is the exact scatter-gather graph used by DiFVM.

use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap};

use crate::geometry::{self, GeometryId};

#[derive(Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct MeshParams {
    pub h0: f64,
    pub grading: f64,
    pub hmax_ratio: f64,
    pub iters: usize,
}

impl Default for MeshParams {
    fn default() -> Self {
        Self {
            h0: 0.05,
            grading: 0.45,
            hmax_ratio: 3.0,
            iters: 45,
        }
    }
}

#[derive(Serialize, Clone, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MeshStats {
    pub n_points: usize,
    pub n_cells: usize,
    pub n_faces: usize,
    pub n_internal: usize,
    pub n_boundary: usize,
    pub min_vol: f64,
    pub max_vol: f64,
    pub mean_vol: f64,
    pub q_min: f64,
    pub q_mean: f64,
    pub non_ortho_max: f64,
    pub non_ortho_mean: f64,
    pub skew_max: f64,
    pub skew_mean: f64,
    pub closure_residual: f64,
    pub boundary_area: f64,
    pub exact_perimeter: f64,
    pub patch_faces: BTreeMap<String, usize>,
    pub bandwidth_before: usize,
    pub bandwidth_after: usize,
    pub avg_degree: f64,
    pub ar_max: f64,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct MeshOut {
    pub points: Vec<f64>,
    pub tris: Vec<u32>,
    pub n_cells: usize,
    pub cc: Vec<f64>,
    pub cv: Vec<f64>,
    pub n_faces: usize,
    pub owner: Vec<i32>,
    pub neigh: Vec<i32>,
    pub fc: Vec<f64>,
    #[serde(rename = "fn")]
    pub normals: Vec<f64>,
    pub fa: Vec<f64>,
    pub f_patch: Vec<i8>,
    pub face_nodes: Vec<u32>,
    pub non_ortho: Vec<f32>,
    pub skew: Vec<f32>,
    pub weight: Vec<f32>,
    pub non_ortho_corr: Vec<f32>,
    pub quality: Vec<f32>,
    pub cell_size: Vec<f32>,
    pub rcm: Vec<u32>,
    pub stats: MeshStats,
    pub duration_ms: f64,
}

fn tri_quality(a: (f64, f64), b: (f64, f64), c: (f64, f64)) -> f64 {
    let la = ((b.0 - c.0).powi(2) + (b.1 - c.1).powi(2)).sqrt();
    let lb = ((a.0 - c.0).powi(2) + (a.1 - c.1).powi(2)).sqrt();
    let lc = ((a.0 - b.0).powi(2) + (a.1 - b.1).powi(2)).sqrt();
    2.0 * (3.0_f64).sqrt()
        * ((b.0 - a.0) * (c.1 - a.1) - (c.0 - a.0) * (b.1 - a.1)).abs()
        / (la * la + lb * lb + lc * lc).max(1e-30)
}

fn size_fn(id: GeometryId, p: &MeshParams, x: f64, y: f64) -> f64 {
    let d = geometry::refine_dist(id, x, y).abs();
    (p.h0 * (1.0 + p.grading * (d / p.h0))).min(p.h0 * p.hmax_ratio)
}

fn triangulate(x: &[f64], y: &[f64]) -> Vec<usize> {
    let mut coords = Vec::with_capacity(2 * x.len());
    for i in 0..x.len() {
        coords.push(x[i]);
        coords.push(y[i]);
    }
    let d = delaunator::Delaunator::new(&coords);
    d.triangles.to_vec()
}

fn ccw(x: &[f64], y: &[f64], a: usize, b: usize, c: usize) -> [usize; 3] {
    let area2 = (x[b] - x[a]) * (y[c] - y[a]) - (x[c] - x[a]) * (y[b] - y[a]);
    if area2 < 0.0 {
        [a, c, b]
    } else {
        [a, b, c]
    }
}

pub fn build(id: GeometryId, p: &MeshParams) -> MeshOut {
    let t0 = std::time::Instant::now();
    let [x0, y0, x1, y1] = geometry::bbox(id);
    let geps = 1e-3 * p.h0;

    /* ---------- lattice seeding + deterministic thinning ---------- */
    let mut xs: Vec<f64> = Vec::new();
    let mut ys: Vec<f64> = Vec::new();
    let fixed = geometry::fixed_points(id);
    let n_fixed = fixed.len();
    for f in &fixed {
        xs.push(f[0]);
        ys.push(f[1]);
    }
    let rows = ((y1 - y0) / (p.h0 * (3.0_f64).sqrt() / 2.0)).ceil() as usize + 1;
    let cols = ((x1 - x0) / p.h0).ceil() as usize + 1;
    let mut cand: Vec<(f64, f64, f64)> = Vec::new();
    let mut rmax: f64 = 0.0;
    for j in 0..=rows {
        let yy = y0 + j as f64 * p.h0 * (3.0_f64).sqrt() / 2.0;
        for i in 0..=cols {
            let xx = x0 + i as f64 * p.h0 + if j % 2 == 1 { p.h0 / 2.0 } else { 0.0 };
            if geometry::sdf(id, xx, yy) > -0.05 * p.h0 {
                continue;
            }
            let r = 1.0 / size_fn(id, p, xx, yy).powi(2);
            rmax = rmax.max(r);
            cand.push((xx, yy, r));
        }
    }
    let mut seed: u64 = 1337;
    let mut rnd = || {
        seed = (seed.wrapping_mul(1103515245).wrapping_add(12345)) & 0x7fff_ffff;
        seed as f64 / 0x7fff_ffff as f64
    };
    for (xx, yy, r) in cand {
        if rnd() > r / rmax {
            continue;
        }
        let mut dup = false;
        for f in 0..n_fixed {
            if ((xx - xs[f]).powi(2) + (yy - ys[f]).powi(2)).sqrt() < 0.6 * p.h0 {
                dup = true;
                break;
            }
        }
        if !dup {
            xs.push(xx);
            ys.push(yy);
        }
    }

    /* ---------- force equilibrium ---------- */
    let dt = 0.22;
    let deps = 1e-4 * p.h0;
    let mut tris: Vec<usize> = Vec::new();
    for _it in 0..p.iters {
        let mut edge_id: HashMap<(u32, u32), usize> = HashMap::new();
        let mut ea: Vec<usize> = Vec::new();
        let mut eb: Vec<usize> = Vec::new();
        let n = xs.len();
        if tris.is_empty() {
            tris = triangulate(&xs, &ys);
        }
        // filter triangles outside the level set
        let mut kept: Vec<usize> = Vec::with_capacity(tris.len());
        for k in (0..tris.len()).step_by(3) {
            let (a, b, c) = (tris[k], tris[k + 1], tris[k + 2]);
            let cx = (xs[a] + xs[b] + xs[c]) / 3.0;
            let cy = (ys[a] + ys[b] + ys[c]) / 3.0;
            if geometry::sdf(id, cx, cy) < -geps {
                kept.extend_from_slice(&[a, b, c]);
            }
        }
        tris = kept;
        for k in (0..tris.len()).step_by(3) {
            for e in 0..3 {
                let i = tris[k + e];
                let j = tris[k + (e + 1) % 3];
                let key = (i.min(j) as u32, i.max(j) as u32);
                if !edge_id.contains_key(&key) {
                    edge_id.insert(key, ea.len());
                    ea.push(i);
                    eb.push(j);
                }
            }
        }
        let mut l2 = 0.0;
        let mut h2 = 0.0;
        let lens: Vec<f64> = (0..ea.len())
            .map(|k| {
                ((xs[ea[k]] - xs[eb[k]]).powi(2) + (ys[ea[k]] - ys[eb[k]]).powi(2))
                    .sqrt()
                    .max(1e-12)
            })
            .collect();
        let hb: Vec<f64> = (0..ea.len())
            .map(|k| {
                size_fn(
                    id,
                    p,
                    (xs[ea[k]] + xs[eb[k]]) / 2.0,
                    (ys[ea[k]] + ys[eb[k]]) / 2.0,
                )
            })
            .collect();
        for k in 0..ea.len() {
            l2 += lens[k] * lens[k];
            h2 += hb[k] * hb[k];
        }
        let scale = 1.2 * (l2 / h2.max(1e-30)).sqrt();
        let mut fx = vec![0.0; n];
        let mut fy = vec![0.0; n];
        for k in 0..ea.len() {
            let f = (hb[k] * scale - lens[k]).max(0.0);
            let ux = (xs[ea[k]] - xs[eb[k]]) / lens[k];
            let uy = (ys[ea[k]] - ys[eb[k]]) / lens[k];
            fx[ea[k]] += f * ux;
            fy[ea[k]] += f * uy;
            fx[eb[k]] -= f * ux;
            fy[eb[k]] -= f * uy;
        }
        for i in n_fixed..n {
            xs[i] += dt * fx[i];
            ys[i] += dt * fy[i];
            let d = geometry::sdf(id, xs[i], ys[i]);
            if d > 0.0 {
                let (gx, gy) = geometry::sdf_grad(id, xs[i], ys[i], deps);
                let g2 = (gx * gx + gy * gy).max(1e-30);
                xs[i] -= d * gx / g2;
                ys[i] -= d * gy / g2;
            }
        }
        tris = triangulate(&xs, &ys);
    }
    // final filter + orientation + sliver cleanup on the retained triangulation
    let mut tri: Vec<usize> = Vec::with_capacity(tris.len());
    for k in (0..tris.len()).step_by(3) {
        let (a, b, c) = (tris[k], tris[k + 1], tris[k + 2]);
        let cx = (xs[a] + xs[b] + xs[c]) / 3.0;
        let cy = (ys[a] + ys[b] + ys[c]) / 3.0;
        if geometry::sdf(id, cx, cy) < -geps {
            tri.extend_from_slice(&ccw(&xs, &ys, a, b, c));
        }
    }
    for _pass in 0..3 {
        let mut cnt: HashMap<(u32, u32), usize> = HashMap::new();
        let np = xs.len() as u32;
        let mut per_node = vec![0i32; xs.len()];
        for k in (0..tri.len()).step_by(3) {
            for e in 0..3 {
                let i = tri[k + e];
                let j = tri[k + (e + 1) % 3];
                per_node[i] += 1;
                *cnt.entry((i.min(j) as u32, i.max(j) as u32)).or_insert(0) += 1;
            }
        }
        let _ = np;
        let edge_count = |i: usize, j: usize| {
            cnt.get(&(i.min(j) as u32, i.max(j) as u32))
                .copied()
                .unwrap_or(0)
        };
        let mut next: Vec<usize> = Vec::with_capacity(tri.len());
        let mut removed = 0;
        for k in (0..tri.len()).step_by(3) {
            let (a, b, c) = (tri[k], tri[k + 1], tri[k + 2]);
            let q = tri_quality((xs[a], ys[a]), (xs[b], ys[b]), (xs[c], ys[c]));
            let on_bnd = edge_count(a, b) == 1
                || edge_count(b, c) == 1
                || edge_count(c, a) == 1;
            let isolates = per_node[a] <= 1 || per_node[b] <= 1 || per_node[c] <= 1;
            if q < 0.16 && on_bnd && !isolates {
                removed += 1;
                per_node[a] -= 1;
                per_node[b] -= 1;
                per_node[c] -= 1;
                continue;
            }
            next.extend_from_slice(&[a, b, c]);
        }
        tri = next;
        if removed == 0 {
            break;
        }
    }

    /* ---------- compact nodes ---------- */
    let mut remap: Vec<i32> = vec![-1; xs.len()];
    let mut px: Vec<f64> = Vec::new();
    let mut py: Vec<f64> = Vec::new();
    let mut tris: Vec<u32> = Vec::with_capacity(tri.len());
    for &o in &tri {
        let mut m = remap[o];
        if m < 0 {
            m = px.len() as i32;
            remap[o] = m;
            px.push(xs[o]);
            py.push(ys[o]);
        }
        tris.push(m as u32);
    }
    let np = px.len();
    let n_cells = tris.len() / 3;

    /* ---------- cell geometry ---------- */
    use rayon::prelude::*;
    let cells: Vec<(f64, f64, f64, f64)> = (0..n_cells)
        .into_par_iter()
        .map(|c| {
            let (a, b, d) = (
                tris[3 * c] as usize,
                tris[3 * c + 1] as usize,
                tris[3 * c + 2] as usize,
            );
            let cx = (px[a] + px[b] + px[d]) / 3.0;
            let cy = (py[a] + py[b] + py[d]) / 3.0;
            let vol = 0.5
                * ((px[b] - px[a]) * (py[d] - py[a]) - (px[d] - px[a]) * (py[b] - py[a])).abs();
            let q = tri_quality((px[a], py[a]), (px[b], py[b]), (px[d], py[d]));
            (cx, cy, vol, q)
        })
        .collect();
    let mut cc = Vec::with_capacity(2 * n_cells);
    let mut cv = Vec::with_capacity(n_cells);
    let mut quality = Vec::with_capacity(n_cells);
    let mut cell_size = Vec::with_capacity(n_cells);
    for c in &cells {
        cc.push(c.0);
        cc.push(c.1);
        cv.push(c.2);
        quality.push(c.3 as f32);
        cell_size.push(c.2.sqrt() as f32);
    }

    /* ---------- faces ---------- */
    let mut fmap: HashMap<(u32, u32), usize> = HashMap::new();
    let mut owner: Vec<i32> = Vec::new();
    let mut neigh: Vec<i32> = Vec::new();
    let mut face_nodes: Vec<u32> = Vec::new();
    for c in 0..n_cells {
        for e in 0..3 {
            let i = tris[3 * c + e];
            let j = tris[3 * c + (e + 1) % 3];
            let key = (i.min(j), i.max(j));
            if let Some(&f) = fmap.get(&key) {
                neigh[f] = c as i32;
            } else {
                fmap.insert(key, owner.len());
                owner.push(c as i32);
                neigh.push(-1);
                face_nodes.push(i);
                face_nodes.push(j);
            }
        }
    }
    let n_faces = owner.len();
    let mut fc = vec![0.0; 2 * n_faces];
    let mut normals = vec![0.0; 2 * n_faces];
    let mut fa = vec![0.0; n_faces];
    let mut f_patch = vec![-1i8; n_faces];
    let mut non_ortho = vec![0.0f32; n_faces];
    let mut skew = vec![0.0f32; n_faces];
    let mut weight = vec![0.0f32; n_faces];
    let mut non_ortho_corr = vec![0.0f32; n_faces];
    let mut patch_faces: BTreeMap<String, usize> = BTreeMap::new();
    let mut boundary_area = 0.0;
    let mut n_internal = 0usize;
    for f in 0..n_faces {
        let i = face_nodes[2 * f] as usize;
        let j = face_nodes[2 * f + 1] as usize;
        let (x1p, y1p, x2p, y2p) = (px[i], py[i], px[j], py[j]);
        fc[2 * f] = 0.5 * (x1p + x2p);
        fc[2 * f + 1] = 0.5 * (y1p + y2p);
        let dl_face = ((x2p - x1p).powi(2) + (y2p - y1p).powi(2)).sqrt();
        fa[f] = dl_face;
        let nx = (y2p - y1p) / dl_face.max(1e-30);
        let ny = -(x2p - x1p) / dl_face.max(1e-30);
        normals[2 * f] = nx;
        normals[2 * f + 1] = ny;
        let o = owner[f] as usize;
        let nb = neigh[f];
        let (dx, dy) = if nb >= 0 {
            n_internal += 1;
            (cc[2 * nb as usize] - cc[2 * o], cc[2 * nb as usize + 1] - cc[2 * o + 1])
        } else {
            let k = geometry::patch_at(id, fc[2 * f], fc[2 * f + 1]);
            f_patch[f] = k.idx();
            *patch_faces.entry(k.key().to_string()).or_insert(0) += 1;
            boundary_area += fa[f];
            (fc[2 * f] - cc[2 * o], fc[2 * f + 1] - cc[2 * o + 1])
        };
        let dl = (dx * dx + dy * dy).sqrt().max(1e-30);
        let cos_t = ((dx * nx + dy * ny) / dl).clamp(-1.0, 1.0);
        non_ortho[f] = cos_t.acos().to_degrees() as f32;
        let ex = dx / dl / cos_t.abs().max(1e-6);
        let ey = dy / dl / cos_t.abs().max(1e-6);
        non_ortho_corr[f] = (nx - ex).hypot(ny - ey) as f32;
        if nb >= 0 {
            let denom = nx * dx + ny * dy;
            let t = (nx * (fc[2 * f] - cc[2 * o]) + ny * (fc[2 * f + 1] - cc[2 * o + 1]))
                / if denom.abs() < 1e-30 { 1e-30 } else { denom };
            skew[f] = (fc[2 * f] - (cc[2 * o] + t * dx)).hypot(
                fc[2 * f + 1] - (cc[2 * o + 1] + t * dy),
            ) as f32
                / dl as f32;
            let d_on = ((cc[2 * nb as usize] - fc[2 * f]).powi(2)
                + (cc[2 * nb as usize + 1] - fc[2 * f + 1]).powi(2))
            .sqrt();
            let d_oo = ((cc[2 * o] - fc[2 * f]).powi(2)
                + (cc[2 * o + 1] - fc[2 * f + 1]).powi(2))
            .sqrt();
            weight[f] = (d_on / (d_on + d_oo).max(1e-30)) as f32;
        } else {
            weight[f] = 1.0;
        }
    }

    /* ---------- zero-order consistency ---------- */
    let mut closure: Vec<f64> = vec![0.0; 2 * n_cells];
    for f in 0..n_faces {
        let o = owner[f] as usize;
        closure[2 * o] += fa[f] * normals[2 * f];
        closure[2 * o + 1] += fa[f] * normals[2 * f + 1];
        let nb = neigh[f];
        if nb >= 0 {
            closure[2 * nb as usize] -= fa[f] * normals[2 * f];
            closure[2 * nb as usize + 1] -= fa[f] * normals[2 * f + 1];
        }
    }
    let mean_l = (cv.iter().sum::<f64>() / n_cells as f64).sqrt();
    let mut closure_residual: f64 = 0.0;
    for c in 0..n_cells {
        closure_residual = closure_residual.max(closure[2 * c].hypot(closure[2 * c + 1]));
    }
    closure_residual /= mean_l.max(1e-30);

    /* ---------- RCM reordering of the cell graph ---------- */
    let (perm, bw0, bw1) = rcm(n_cells, &owner, &neigh);

    let min_vol = cv.iter().cloned().fold(f64::INFINITY, f64::min);
    let max_vol = cv.iter().cloned().fold(0.0, f64::max);
    let mean_vol = cv.iter().sum::<f64>() / n_cells as f64;
    let q_min = quality.iter().cloned().fold(f32::INFINITY, f32::min) as f64;
    let q_mean = quality.iter().map(|v| *v as f64).sum::<f64>() / n_cells as f64;
    let mut no_max: f64 = 0.0;
    let mut no_sum = 0.0;
    let mut sk_max: f64 = 0.0;
    let mut sk_sum = 0.0;
    for f in 0..n_faces {
        if neigh[f] < 0 {
            continue;
        }
        no_max = no_max.max(non_ortho[f] as f64);
        no_sum += non_ortho[f] as f64;
        sk_max = sk_max.max(skew[f] as f64);
        sk_sum += skew[f] as f64;
    }
    let mut ar_max: f64 = 1.0;
    for c in 0..n_cells {
        let (a, b, d) = (
            tris[3 * c] as usize,
            tris[3 * c + 1] as usize,
            tris[3 * c + 2] as usize,
        );
        let l1 = (px[b] - px[a]).hypot(py[b] - py[a]);
        let l2 = (px[d] - px[b]).hypot(py[d] - py[b]);
        let l3 = (px[a] - px[d]).hypot(py[a] - py[d]);
        ar_max = ar_max.max(l1.max(l2).max(l3) / l1.min(l2).min(l3).max(1e-30));
    }

    let stats = MeshStats {
        n_points: np,
        n_cells,
        n_faces,
        n_internal,
        n_boundary: n_faces - n_internal,
        min_vol,
        max_vol,
        mean_vol,
        q_min,
        q_mean,
        non_ortho_max: no_max,
        non_ortho_mean: no_sum / n_internal.max(1) as f64,
        skew_max: sk_max,
        skew_mean: sk_sum / n_internal.max(1) as f64,
        closure_residual,
        boundary_area,
        exact_perimeter: geometry::perimeter(id),
        patch_faces,
        bandwidth_before: bw0,
        bandwidth_after: bw1,
        avg_degree: 2.0 * n_internal as f64 / n_cells as f64,
        ar_max,
    };

    let mut points = Vec::with_capacity(2 * np);
    for i in 0..np {
        points.push(px[i]);
        points.push(py[i]);
    }

    MeshOut {
        points,
        tris,
        n_cells,
        cc,
        cv,
        n_faces,
        owner,
        neigh,
        fc,
        normals,
        fa,
        f_patch,
        face_nodes,
        non_ortho,
        skew,
        weight,
        non_ortho_corr,
        quality,
        cell_size,
        rcm: perm,
        stats,
        duration_ms: t0.elapsed().as_secs_f64() * 1e3,
    }
}

fn rcm(n: usize, owner: &[i32], neigh: &[i32]) -> (Vec<u32>, usize, usize) {
    let mut deg = vec![0usize; n];
    let mut bw0 = 0usize;
    for f in 0..owner.len() {
        if neigh[f] < 0 {
            continue;
        }
        let (a, b) = (owner[f] as usize, neigh[f] as usize);
        deg[a] += 1;
        deg[b] += 1;
        bw0 = bw0.max(a.abs_diff(b));
    }
    let mut off = vec![0usize; n + 1];
    for c in 0..n {
        off[c + 1] = off[c] + deg[c];
    }
    let mut adj = vec![0i32; off[n]];
    let mut cur = off.clone();
    for f in 0..owner.len() {
        if neigh[f] < 0 {
            continue;
        }
        let (a, b) = (owner[f] as usize, neigh[f] as usize);
        adj[cur[a]] = b as i32;
        cur[a] += 1;
        adj[cur[b]] = a as i32;
        cur[b] += 1;
    }
    let mut perm = vec![0u32; n];
    let mut visited = vec![false; n];
    let mut k = 0;
    for start in 0..n {
        if visited[start] {
            continue;
        }
        let mut queue = std::collections::VecDeque::new();
        queue.push_back(start);
        visited[start] = true;
        while let Some(c) = queue.pop_front() {
            perm[k] = c as u32;
            k += 1;
            let mut nb: Vec<usize> = (off[c]..off[c + 1])
                .map(|p| adj[p] as usize)
                .filter(|&m| !visited[m])
                .collect();
            for &m in &nb {
                visited[m] = true;
            }
            nb.sort_by_key(|&m| deg[m]);
            for m in nb {
                queue.push_back(m);
            }
        }
    }
    perm.reverse();
    let mut idx = vec![0usize; n];
    for (i, &c) in perm.iter().enumerate() {
        idx[c as usize] = i;
    }
    let mut bw1 = 0;
    for f in 0..owner.len() {
        if neigh[f] < 0 {
            continue;
        }
        bw1 = bw1.max(idx[owner[f] as usize].abs_diff(idx[neigh[f] as usize]));
    }
    (perm, bw0, bw1)
}
