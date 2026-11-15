use std::collections::HashMap;

use bytemuck::{Pod, Zeroable};
use glam::Vec3;
use serde::{Deserialize, Serialize};

use crate::math::Aabb3;

#[repr(C)]
#[derive(Debug, Clone, Copy, Pod, Zeroable)]
pub struct CadVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
}

impl CadVertex {
    pub fn new(pos: Vec3, normal: Vec3, uv: [f32; 2]) -> Self {
        Self {
            position: pos.to_array(),
            normal: normal.normalize_or(Vec3::Y).to_array(),
            uv,
        }
    }
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum EdgeKind {
    Crease = 0,
    Boundary = 1,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Pod, Zeroable)]
pub struct EdgeVertex {
    pub position: [f32; 3],
    pub kind: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct CadMaterialData {
    pub base_color: [f32; 4],
    pub metallic: f32,
    pub roughness: f32,
    pub edge_color: [f32; 4],
}

impl Default for CadMaterialData {
    fn default() -> Self {
        Self {
            base_color: [0.68, 0.73, 0.78, 1.0],
            metallic: 0.45,
            roughness: 0.38,
            edge_color: [0.06, 0.08, 0.11, 1.0],
        }
    }
}

impl CadMaterialData {
    pub fn anodized_aluminum() -> Self {
        Self {
            base_color: [0.64, 0.69, 0.75, 1.0],
            metallic: 0.72,
            roughness: 0.34,
            edge_color: [0.05, 0.07, 0.10, 1.0],
        }
    }

    pub fn machined_steel() -> Self {
        Self {
            base_color: [0.48, 0.52, 0.56, 1.0],
            metallic: 0.85,
            roughness: 0.28,
            edge_color: [0.04, 0.05, 0.08, 1.0],
        }
    }

    pub fn brass_bearing() -> Self {
        Self {
            base_color: [0.80, 0.62, 0.27, 1.0],
            metallic: 0.86,
            roughness: 0.25,
            edge_color: [0.09, 0.06, 0.02, 1.0],
        }
    }

    pub fn cobalt_anodized() -> Self {
        Self {
            base_color: [0.18, 0.45, 0.76, 1.0],
            metallic: 0.65,
            roughness: 0.32,
            edge_color: [0.03, 0.07, 0.14, 1.0],
        }
    }

    pub fn dark_oxide() -> Self {
        Self {
            base_color: [0.21, 0.24, 0.28, 1.0],
            metallic: 0.55,
            roughness: 0.48,
            edge_color: [0.02, 0.03, 0.05, 1.0],
        }
    }

    pub fn copper_seal() -> Self {
        Self {
            base_color: [0.82, 0.42, 0.24, 1.0],
            metallic: 0.88,
            roughness: 0.26,
            edge_color: [0.10, 0.04, 0.02, 1.0],
        }
    }
}

#[derive(Debug, Clone)]
pub struct CadMesh {
    pub name: String,
    pub vertices: Vec<CadVertex>,
    pub indices: Vec<u32>,
    pub edge_vertices: Vec<EdgeVertex>,
    pub aabb: Aabb3,
}

impl CadMesh {
    pub fn from_geometry(
        name: impl Into<String>,
        vertices: Vec<CadVertex>,
        indices: Vec<u32>,
        crease_cfg: CreaseConfig,
    ) -> Self {
        let aabb = Aabb3::from_points(vertices.iter().map(|v| Vec3::from(v.position)));
        let edge_vertices = extract_crease_edges(&vertices, &indices, crease_cfg);
        Self {
            name: name.into(),
            vertices,
            indices,
            edge_vertices,
            aabb,
        }
    }

    pub fn triangle_count(&self) -> usize {
        self.indices.len() / 3
    }

    pub fn edge_segment_count(&self) -> usize {
        self.edge_vertices.len() / 2
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct CreaseConfig {
    pub crease_angle_deg: f32,
    pub include_boundary_edges: bool,
    pub quantize_epsilon: f32,
}

impl Default for CreaseConfig {
    fn default() -> Self {
        Self {
            crease_angle_deg: 28.0,
            include_boundary_edges: true,
            quantize_epsilon: 1e-4,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
struct QuantizedPos(i64, i64, i64);

impl QuantizedPos {
    fn from_vec3(p: Vec3, inv_eps: f32) -> Self {
        Self(
            (p.x * inv_eps).round() as i64,
            (p.y * inv_eps).round() as i64,
            (p.z * inv_eps).round() as i64,
        )
    }
}

/// Extract sharp dihedral crease edges and open boundary edges from any triangle mesh.
/// Welding via quantized position ensures flat-shaded / STL meshes with duplicate vertices
/// still produce clean topological B-Rep style crease outlines.
pub fn extract_crease_edges(
    vertices: &[CadVertex],
    indices: &[u32],
    config: CreaseConfig,
) -> Vec<EdgeVertex> {
    if vertices.is_empty() || indices.len() < 3 {
        return Vec::new();
    }

    let inv_eps = 1.0 / config.quantize_epsilon.max(1e-6);
    let cos_threshold = config.crease_angle_deg.to_radians().cos();

    struct EdgeBucket {
        p0: Vec3,
        p1: Vec3,
        normals: Vec<Vec3>,
    }

    let mut edge_map: HashMap<(QuantizedPos, QuantizedPos), EdgeBucket> =
        HashMap::with_capacity(indices.len());

    for tri in indices.chunks_exact(3) {
        let (i0, i1, i2) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
        if i0 >= vertices.len() || i1 >= vertices.len() || i2 >= vertices.len() {
            continue;
        }
        let v0 = Vec3::from(vertices[i0].position);
        let v1 = Vec3::from(vertices[i1].position);
        let v2 = Vec3::from(vertices[i2].position);

        let face_n = (v1 - v0).cross(v2 - v0);
        let len_sq = face_n.length_squared();
        if len_sq < 1e-14 {
            continue;
        }
        let n = face_n / len_sq.sqrt();

        let pairs = [(v0, v1), (v1, v2), (v2, v0)];
        for (a, b) in pairs {
            let qa = QuantizedPos::from_vec3(a, inv_eps);
            let qb = QuantizedPos::from_vec3(b, inv_eps);
            if qa == qb {
                continue;
            }
            let key = if qa < qb { (qa, qb) } else { (qb, qa) };
            let bucket = edge_map.entry(key).or_insert_with(|| EdgeBucket {
                p0: a,
                p1: b,
                normals: Vec::with_capacity(2),
            });
            bucket.normals.push(n);
        }
    }

    let mut out = Vec::with_capacity(edge_map.len());
    for bucket in edge_map.into_values() {
        let kind = if bucket.normals.len() == 1 {
            if !config.include_boundary_edges {
                continue;
            }
            EdgeKind::Boundary as u32
        } else {
            let mut is_crease = false;
            'outer: for i in 0..bucket.normals.len() {
                for j in (i + 1)..bucket.normals.len() {
                    if bucket.normals[i].dot(bucket.normals[j]) < cos_threshold {
                        is_crease = true;
                        break 'outer;
                    }
                }
            }
            if !is_crease {
                continue;
            }
            EdgeKind::Crease as u32
        };

        out.push(EdgeVertex {
            position: bucket.p0.to_array(),
            kind,
        });
        out.push(EdgeVertex {
            position: bucket.p1.to_array(),
            kind,
        });
    }

    out
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Serialize, Deserialize)]
pub struct MassProperties {
    pub volume: f32,
    pub surface_area: f32,
    pub center_of_mass: Vec3,
    pub aabb: Aabb3,
}

/// Computes signed closed-mesh volume, surface area, and center of mass.
pub fn compute_mass_properties(mesh: &CadMesh) -> MassProperties {
    let mut signed_vol_6 = 0.0_f64;
    let mut area = 0.0_f64;
    let mut weighted_centroid = glam::DVec3::ZERO;

    for tri in mesh.indices.chunks_exact(3) {
        let (i0, i1, i2) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
        if i0 >= mesh.vertices.len() || i1 >= mesh.vertices.len() || i2 >= mesh.vertices.len() {
            continue;
        }
        let v0 = Vec3::from(mesh.vertices[i0].position).as_dvec3();
        let v1 = Vec3::from(mesh.vertices[i1].position).as_dvec3();
        let v2 = Vec3::from(mesh.vertices[i2].position).as_dvec3();

        let cross = (v1 - v0).cross(v2 - v0);
        area += 0.5 * cross.length();

        let v_tet_6 = v0.dot(v1.cross(v2));
        signed_vol_6 += v_tet_6;
        weighted_centroid += (v0 + v1 + v2) * (v_tet_6 / 4.0);
    }

    let volume = (signed_vol_6 / 6.0).abs() as f32;
    let center_of_mass = if signed_vol_6.abs() > 1e-9 {
        (weighted_centroid / signed_vol_6).as_vec3()
    } else {
        mesh.aabb.center()
    };

    MassProperties {
        volume,
        surface_area: area as f32,
        center_of_mass,
        aabb: mesh.aabb,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::primitives::make_box;

    #[test]
    fn cube_crease_edges_and_mass_properties() {
        let cube = make_box("test_cube", Vec3::new(2.0, 3.0, 4.0));
        // A box has 12 sharp 90-degree edges (24 EdgeVertex points)
        assert_eq!(cube.edge_segment_count(), 12);
        let props = compute_mass_properties(&cube);
        assert!((props.volume - 24.0).abs() < 1e-3);
        assert!((props.surface_area - 52.0).abs() < 1e-3);
        assert!(props.center_of_mass.length() < 1e-4);
    }
}
