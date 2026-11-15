use std::fs::File;
use std::io::BufReader;
use std::path::Path;

use glam::{Mat4, Vec3};
use thiserror::Error;

use crate::mesh::{CadMaterialData, CadMesh, CadVertex, CreaseConfig};

#[derive(Debug, Error)]
pub enum CadIoError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Unsupported CAD file extension: {0}")]
    UnsupportedExtension(String),
    #[error("OBJ parse error: {0}")]
    Obj(#[from] tobj::LoadError),
    #[error("glTF parse error: {0}")]
    Gltf(#[from] gltf::Error),
    #[error("No valid mesh geometry found in file")]
    EmptyGeometry,
}

#[derive(Debug, Clone)]
pub struct ImportedPart {
    pub name: String,
    pub mesh: CadMesh,
    pub transform: Mat4,
    pub material: CadMaterialData,
}

#[derive(Debug, Clone)]
pub struct ImportedAssembly {
    pub name: String,
    pub parts: Vec<ImportedPart>,
}

pub fn load_cad_file(path: &Path, crease_cfg: CreaseConfig) -> Result<ImportedAssembly, CadIoError> {
    let ext = path
        .extension()
        .and_then(|s| s.to_str())
        .map(|s| s.to_ascii_lowercase())
        .unwrap_or_default();

    let name = path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("ImportedAssembly")
        .to_string();

    match ext.as_str() {
        "stl" => load_stl(path, name, crease_cfg),
        "obj" => load_obj(path, name, crease_cfg),
        "gltf" | "glb" => load_gltf(path, name, crease_cfg),
        other => Err(CadIoError::UnsupportedExtension(other.to_string())),
    }
}

fn load_stl(
    path: &Path,
    name: String,
    crease_cfg: CreaseConfig,
) -> Result<ImportedAssembly, CadIoError> {
    let mut file = BufReader::new(File::open(path)?);
    let indexed = stl_io::read_stl(&mut file)?;
    if indexed.faces.is_empty() {
        return Err(CadIoError::EmptyGeometry);
    }

    let mut verts = Vec::with_capacity(indexed.faces.len() * 3);
    let mut indices = Vec::with_capacity(indexed.faces.len() * 3);

    for face in &indexed.faces {
        let p0 = Vec3::from(indexed.vertices[face.vertices[0]].0);
        let p1 = Vec3::from(indexed.vertices[face.vertices[1]].0);
        let p2 = Vec3::from(indexed.vertices[face.vertices[2]].0);

        let computed_n = (p1 - p0).cross(p2 - p0).normalize_or_zero();
        let file_n = Vec3::from(face.normal.0);
        let n = if computed_n.length_squared() > 1e-8 {
            computed_n
        } else {
            file_n.normalize_or(Vec3::Y)
        };

        let base = verts.len() as u32;
        verts.push(CadVertex::new(p0, n, [0.0, 0.0]));
        verts.push(CadVertex::new(p1, n, [1.0, 0.0]));
        verts.push(CadVertex::new(p2, n, [0.0, 1.0]));
        indices.extend_from_slice(&[base, base + 1, base + 2]);
    }

    let mesh = CadMesh::from_geometry(name.clone(), verts, indices, crease_cfg);
    Ok(ImportedAssembly {
        name: name.clone(),
        parts: vec![ImportedPart {
            name,
            mesh,
            transform: Mat4::IDENTITY,
            material: CadMaterialData::anodized_aluminum(),
        }],
    })
}

fn load_obj(
    path: &Path,
    name: String,
    crease_cfg: CreaseConfig,
) -> Result<ImportedAssembly, CadIoError> {
    let (models, materials_res) = tobj::load_obj(
        path,
        &tobj::LoadOptions {
            triangulate: true,
            single_index: true,
            ..Default::default()
        },
    )?;
    let materials = materials_res.unwrap_or_default();
    if models.is_empty() {
        return Err(CadIoError::EmptyGeometry);
    }

    let mut parts = Vec::with_capacity(models.len());
    for model in models {
        let m = &model.mesh;
        if m.indices.is_empty() || m.positions.len() < 9 {
            continue;
        }
        let has_normals = m.normals.len() == m.positions.len();
        let has_uvs = m.texcoords.len() / 2 == m.positions.len() / 3;
        let v_count = m.positions.len() / 3;

        let mut verts = Vec::with_capacity(v_count);
        for i in 0..v_count {
            let pos = Vec3::new(
                m.positions[i * 3],
                m.positions[i * 3 + 1],
                m.positions[i * 3 + 2],
            );
            let norm = if has_normals {
                Vec3::new(m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2])
            } else {
                Vec3::Y
            };
            let uv = if has_uvs {
                [m.texcoords[i * 2], m.texcoords[i * 2 + 1]]
            } else {
                [0.0, 0.0]
            };
            verts.push(CadVertex::new(pos, norm, uv));
        }

        if !has_normals {
            recompute_vertex_normals(&mut verts, &m.indices);
        }

        let mat = m
            .material_id
            .and_then(|id| materials.get(id))
            .map(|mtl| {
                let d = mtl.diffuse.unwrap_or([0.65, 0.70, 0.76]);
                CadMaterialData {
                    base_color: [d[0], d[1], d[2], 1.0],
                    metallic: 0.4,
                    roughness: 0.42,
                    ..Default::default()
                }
            })
            .unwrap_or_default();

        let part_name = if model.name.is_empty() {
            format!("{name}_part_{}", parts.len() + 1)
        } else {
            model.name.clone()
        };
        let mesh = CadMesh::from_geometry(part_name.clone(), verts, m.indices.clone(), crease_cfg);
        parts.push(ImportedPart {
            name: part_name,
            mesh,
            transform: Mat4::IDENTITY,
            material: mat,
        });
    }

    if parts.is_empty() {
        return Err(CadIoError::EmptyGeometry);
    }

    Ok(ImportedAssembly { name, parts })
}

fn load_gltf(
    path: &Path,
    name: String,
    crease_cfg: CreaseConfig,
) -> Result<ImportedAssembly, CadIoError> {
    let (doc, buffers, _images) = gltf::import(path)?;
    let mut parts = Vec::new();

    for scene in doc.scenes() {
        for node in scene.nodes() {
            collect_gltf_node(&node, Mat4::IDENTITY, &buffers, crease_cfg, &mut parts);
        }
    }

    if parts.is_empty() {
        return Err(CadIoError::EmptyGeometry);
    }

    Ok(ImportedAssembly { name, parts })
}

fn collect_gltf_node(
    node: &gltf::Node<'_>,
    parent_transform: Mat4,
    buffers: &[gltf::buffer::Data],
    crease_cfg: CreaseConfig,
    parts: &mut Vec<ImportedPart>,
) {
    let local = Mat4::from_cols_array_2d(&node.transform().matrix());
    let world = parent_transform * local;

    if let Some(mesh) = node.mesh() {
        let base_name = node
            .name()
            .or_else(|| mesh.name())
            .unwrap_or("gltf_part")
            .to_string();

        for (prim_idx, prim) in mesh.primitives().enumerate() {
            let reader = prim.reader(|b| buffers.get(b.index()).map(|d| d.0.as_slice()));
            let Some(positions) = reader.read_positions() else {
                continue;
            };
            let pos_vec: Vec<[f32; 3]> = positions.collect();
            let normals_vec: Option<Vec<[f32; 3]>> = reader.read_normals().map(|n| n.collect());
            let uvs_vec: Option<Vec<[f32; 2]>> =
                reader.read_tex_coords(0).map(|tc| tc.into_f32().collect());

            let indices: Vec<u32> = if let Some(ind) = reader.read_indices() {
                ind.into_u32().collect()
            } else {
                (0..pos_vec.len() as u32).collect()
            };

            let mut verts = Vec::with_capacity(pos_vec.len());
            for (i, p) in pos_vec.iter().enumerate() {
                let n = normals_vec
                    .as_ref()
                    .and_then(|nv| nv.get(i).copied())
                    .unwrap_or([0.0, 1.0, 0.0]);
                let uv = uvs_vec
                    .as_ref()
                    .and_then(|uvv| uvv.get(i).copied())
                    .unwrap_or([0.0, 0.0]);
                verts.push(CadVertex::new(Vec3::from(*p), Vec3::from(n), uv));
            }
            if normals_vec.is_none() {
                recompute_vertex_normals(&mut verts, &indices);
            }

            let pbr = prim.material().pbr_metallic_roughness();
            let base_color = pbr.base_color_factor();
            let mat = CadMaterialData {
                base_color,
                metallic: pbr.metallic_factor(),
                roughness: pbr.roughness_factor(),
                ..Default::default()
            };

            let part_name = if mesh.primitives().len() > 1 {
                format!("{base_name}_{prim_idx}")
            } else {
                base_name.clone()
            };
            let cad_mesh = CadMesh::from_geometry(part_name.clone(), verts, indices, crease_cfg);
            parts.push(ImportedPart {
                name: part_name,
                mesh: cad_mesh,
                transform: world,
                material: mat,
            });
        }
    }

    for child in node.children() {
        collect_gltf_node(&child, world, buffers, crease_cfg, parts);
    }
}

fn recompute_vertex_normals(verts: &mut [CadVertex], indices: &[u32]) {
    let mut accum = vec![Vec3::ZERO; verts.len()];
    for tri in indices.chunks_exact(3) {
        let (i0, i1, i2) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
        if i0 >= verts.len() || i1 >= verts.len() || i2 >= verts.len() {
            continue;
        }
        let p0 = Vec3::from(verts[i0].position);
        let p1 = Vec3::from(verts[i1].position);
        let p2 = Vec3::from(verts[i2].position);
        let n = (p1 - p0).cross(p2 - p0);
        accum[i0] += n;
        accum[i1] += n;
        accum[i2] += n;
    }
    for (v, n) in verts.iter_mut().zip(accum) {
        v.normal = n.normalize_or(Vec3::Y).to_array();
    }
}
