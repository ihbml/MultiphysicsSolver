//! mesh.rs - Unstructured Finite Volume Mesh (Structure of Arrays) representation for CFDSolver.

use std::f64;

/// Boundary patch information from OpenFOAM constant/polyMesh/boundary
#[derive(Debug, Clone, PartialEq)]
pub struct FoamBoundaryPatch {
    pub name: String,
    pub patch_type: String,
    pub start_face: usize,
    pub n_faces: usize,
}

/// Core unstructured polyhedral mesh representation matching ARCHITECTURE.md Structure of Arrays (SoA).
#[derive(Debug, Clone, Default)]
pub struct FoamMesh {
    // Basic topological arrays
    pub points: Vec<[f64; 3]>,
    pub face_offsets: Vec<usize>, // Length: n_faces + 1
    pub face_nodes: Vec<usize>,   // Flattened nodes for all faces
    pub owner: Vec<usize>,        // Length: n_faces
    pub neighbour: Vec<usize>,    // Length: n_internal_faces
    pub n_cells: usize,
    pub boundaries: Vec<FoamBoundaryPatch>,

    // Precomputed geometric quantities (SoA layout)
    pub face_centres: Vec<[f64; 3]>,    // x_f
    pub face_areas: Vec<[f64; 3]>,      // S_f (normal vector with magnitude = area)
    pub face_magnitudes: Vec<f64>,      // |S_f|
    pub cell_centres: Vec<[f64; 3]>,    // x_C
    pub cell_volumes: Vec<f64>,         // V_C
    pub delta_f: Vec<[f64; 3]>,         // delta_f (orthogonal component vector)
    pub k_f: Vec<[f64; 3]>,             // k_f = S_f - delta_f (non-orthogonal correction vector)
}

impl FoamMesh {
    pub fn new() -> Self {
        Self::default()
    }

    #[inline]
    pub fn n_points(&self) -> usize {
        self.points.len()
    }

    #[inline]
    pub fn n_faces(&self) -> usize {
        self.owner.len()
    }

    #[inline]
    pub fn n_internal_faces(&self) -> usize {
        self.neighbour.len()
    }

    #[inline]
    pub fn n_boundary_faces(&self) -> usize {
        self.n_faces().saturating_sub(self.n_internal_faces())
    }

    /// Slice of node indices for face `i`
    #[inline]
    pub fn face_nodes_slice(&self, face_idx: usize) -> &[usize] {
        let start = self.face_offsets[face_idx];
        let end = self.face_offsets[face_idx + 1];
        &self.face_nodes[start..end]
    }

    /// Precomputes all geometric metrics (face centres, face normal area vectors S_f,
    /// cell centres, cell volumes V_C, delta_f, and non-orthogonal vectors k_f).
    pub fn compute_geometry(&mut self) {
        let n_faces = self.n_faces();
        let n_cells = self.n_cells;

        self.face_centres = vec![[0.0; 3]; n_faces];
        self.face_areas = vec![[0.0; 3]; n_faces];
        self.face_magnitudes = vec![0.0; n_faces];

        // 1. Calculate face centres and face area vectors S_f
        for f in 0..n_faces {
            let nodes = self.face_nodes_slice(f);
            let n_nodes = nodes.len();
            if n_nodes < 3 {
                continue;
            }

            // Face vertex average as preliminary centre
            let mut c_approx = [0.0; 3];
            for &n in nodes {
                let p = self.points[n];
                c_approx[0] += p[0];
                c_approx[1] += p[1];
                c_approx[2] += p[2];
            }
            let inv_n = 1.0 / (n_nodes as f64);
            c_approx[0] *= inv_n;
            c_approx[1] *= inv_n;
            c_approx[2] *= inv_n;

            // Decompose polygon into triangles from c_approx
            let mut face_area = [0.0; 3];
            let mut sum_triangle_centre_area = [0.0; 3];
            let mut total_area = 0.0;

            for i in 0..n_nodes {
                let j = (i + 1) % n_nodes;
                let p0 = self.points[nodes[i]];
                let p1 = self.points[nodes[j]];

                // Vectors relative to c_approx
                let v0 = [p0[0] - c_approx[0], p0[1] - c_approx[1], p0[2] - c_approx[2]];
                let v1 = [p1[0] - c_approx[0], p1[1] - c_approx[1], p1[2] - c_approx[2]];

                // Cross product 0.5 * (v0 x v1)
                let tri_n = [
                    0.5 * (v0[1] * v1[2] - v0[2] * v1[1]),
                    0.5 * (v0[2] * v1[0] - v0[0] * v1[2]),
                    0.5 * (v0[0] * v1[1] - v0[1] * v1[0]),
                ];
                let tri_area = (tri_n[0] * tri_n[0] + tri_n[1] * tri_n[1] + tri_n[2] * tri_n[2]).sqrt();

                face_area[0] += tri_n[0];
                face_area[1] += tri_n[1];
                face_area[2] += tri_n[2];
                total_area += tri_area;

                // Triangle centroid
                let tri_c = [
                    (c_approx[0] + p0[0] + p1[0]) / 3.0,
                    (c_approx[1] + p0[1] + p1[1]) / 3.0,
                    (c_approx[2] + p0[2] + p1[2]) / 3.0,
                ];

                sum_triangle_centre_area[0] += tri_c[0] * tri_area;
                sum_triangle_centre_area[1] += tri_c[1] * tri_area;
                sum_triangle_centre_area[2] += tri_c[2] * tri_area;
            }

            let mag_s = (face_area[0] * face_area[0] + face_area[1] * face_area[1] + face_area[2] * face_area[2]).sqrt();
            self.face_areas[f] = face_area;
            self.face_magnitudes[f] = mag_s;

            if total_area > 1e-15 {
                self.face_centres[f] = [
                    sum_triangle_centre_area[0] / total_area,
                    sum_triangle_centre_area[1] / total_area,
                    sum_triangle_centre_area[2] / total_area,
                ];
            } else {
                self.face_centres[f] = c_approx;
            }
        }

        // 2. Calculate cell centres and cell volumes V_C via divergence theorem pyramid decomposition
        self.cell_centres = vec![[0.0; 3]; n_cells];
        self.cell_volumes = vec![0.0; n_cells];

        let mut sum_pyr_c = vec![[0.0; 3]; n_cells];

        for f in 0..n_faces {
            let o = self.owner[f];
            let sf = self.face_areas[f];
            let cf = self.face_centres[f];

            // Owner contribution (outward normal is +S_f)
            let vol_pyr_o = (cf[0] * sf[0] + cf[1] * sf[1] + cf[2] * sf[2]) / 3.0;
            self.cell_volumes[o] += vol_pyr_o;
            let pyr_c = [0.75 * cf[0], 0.75 * cf[1], 0.75 * cf[2]];
            sum_pyr_c[o][0] += pyr_c[0] * vol_pyr_o;
            sum_pyr_c[o][1] += pyr_c[1] * vol_pyr_o;
            sum_pyr_c[o][2] += pyr_c[2] * vol_pyr_o;

            // Neighbour contribution if internal (outward normal is -S_f)
            if f < self.n_internal_faces() {
                let n = self.neighbour[f];
                let vol_pyr_n = -(cf[0] * sf[0] + cf[1] * sf[1] + cf[2] * sf[2]) / 3.0;
                self.cell_volumes[n] += vol_pyr_n;
                sum_pyr_c[n][0] += pyr_c[0] * vol_pyr_n;
                sum_pyr_c[n][1] += pyr_c[1] * vol_pyr_n;
                sum_pyr_c[n][2] += pyr_c[2] * vol_pyr_n;
            }
        }

        for c in 0..n_cells {
            let v = self.cell_volumes[c];
            if v > 1e-15 {
                self.cell_centres[c] = [
                    sum_pyr_c[c][0] / v,
                    sum_pyr_c[c][1] / v,
                    sum_pyr_c[c][2] / v,
                ];
            }
        }

        // 3. Calculate delta_f and non-orthogonal correction vector k_f
        let n_internal = self.n_internal_faces();
        self.delta_f = vec![[0.0; 3]; n_faces];
        self.k_f = vec![[0.0; 3]; n_faces];

        for f in 0..n_internal {
            let o = self.owner[f];
            let n = self.neighbour[f];
            let co = self.cell_centres[o];
            let cn = self.cell_centres[n];

            // d = C_neighbour - C_owner
            let d = [cn[0] - co[0], cn[1] - co[1], cn[2] - co[2]];
            let d_mag_sq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            let sf = self.face_areas[f];

            if d_mag_sq > 1e-15 {
                // Over-relaxed approach: delta = S_f . S_f / (S_f . d) * d
                let sf_dot_d = sf[0] * d[0] + sf[1] * d[1] + sf[2] * d[2];
                if sf_dot_d.abs() > 1e-15 {
                    let sf_sq = self.face_magnitudes[f] * self.face_magnitudes[f];
                    let factor = sf_sq / sf_dot_d;
                    let delta = [factor * d[0], factor * d[1], factor * d[2]];
                    self.delta_f[f] = delta;
                    self.k_f[f] = [sf[0] - delta[0], sf[1] - delta[1], sf[2] - delta[2]];
                } else {
                    self.delta_f[f] = sf;
                    self.k_f[f] = [0.0; 3];
                }
            } else {
                self.delta_f[f] = sf;
                self.k_f[f] = [0.0; 3];
            }
        }

        // Boundary faces delta_f is vector from cell centre to face centre
        for f in n_internal..n_faces {
            let o = self.owner[f];
            let co = self.cell_centres[o];
            let cf = self.face_centres[f];
            let d = [cf[0] - co[0], cf[1] - co[1], cf[2] - co[2]];
            self.delta_f[f] = d;
            self.k_f[f] = [0.0; 3];
        }
    }

    /// Verifies topological integrity and geometric invariants.
    pub fn validate_topology(&self) -> Result<(), String> {
        let n_faces = self.n_faces();
        let n_internal = self.n_internal_faces();

        if self.face_offsets.len() != n_faces + 1 {
            return Err(format!(
                "Invalid face_offsets length: expected {}, got {}",
                n_faces + 1,
                self.face_offsets.len()
            ));
        }

        if n_internal > n_faces {
            return Err(format!(
                "n_internal_faces ({}) exceeds total n_faces ({})",
                n_internal, n_faces
            ));
        }

        for (i, &o) in self.owner.iter().enumerate() {
            if o >= self.n_cells {
                return Err(format!("Owner for face {} ({}) >= n_cells ({})", i, o, self.n_cells));
            }
        }

        for (i, &n) in self.neighbour.iter().enumerate() {
            if n >= self.n_cells {
                return Err(format!("Neighbour for face {} ({}) >= n_cells ({})", i, n, self.n_cells));
            }
        }

        // Check boundary patches contiguous partition of boundary faces
        let mut expected_start = n_internal;
        for patch in &self.boundaries {
            if patch.start_face != expected_start {
                return Err(format!(
                    "Boundary patch '{}' starts at face {}, expected {}",
                    patch.name, patch.start_face, expected_start
                ));
            }
            expected_start += patch.n_faces;
        }

        if expected_start != n_faces {
            return Err(format!(
                "Total boundary faces ({}) do not match n_faces - n_internal ({})",
                expected_start - n_internal,
                n_faces - n_internal
            ));
        }

        Ok(())
    }

    /// Total mesh enclosed volume (sum of cell volumes)
    pub fn total_volume(&self) -> f64 {
        self.cell_volumes.iter().sum()
    }

    /// Axis-aligned bounding box of points: ([min_x, min_y, min_z], [max_x, max_y, max_z])
    pub fn bounding_box(&self) -> ([f64; 3], [f64; 3]) {
        if self.points.is_empty() {
            return ([0.0; 3], [0.0; 3]);
        }
        let mut min = self.points[0];
        let mut max = self.points[0];
        for p in &self.points[1..] {
            min[0] = min[0].min(p[0]);
            min[1] = min[1].min(p[1]);
            min[2] = min[2].min(p[2]);

            max[0] = max[0].max(p[0]);
            max[1] = max[1].max(p[1]);
            max[2] = max[2].max(p[2]);
        }
        (min, max)
    }
}
