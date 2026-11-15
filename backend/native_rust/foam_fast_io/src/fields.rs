//! fields.rs - Parsing and storage of OpenFOAM scalar and vector fields.

use crate::mesh::FoamMesh;
use crate::parser::{read_file_or_gz, FoamStream};
use std::collections::HashMap;
use std::path::Path;

#[derive(Debug, Clone, PartialEq)]
pub enum FoamFieldData {
    Scalar(Vec<f64>),
    Vector(Vec<[f64; 3]>),
}

#[derive(Debug, Clone, PartialEq)]
pub enum PatchFieldData {
    UniformScalar(f64),
    UniformVector([f64; 3]),
    NonuniformScalar(Vec<f64>),
    NonuniformVector(Vec<[f64; 3]>),
}

#[derive(Debug, Clone, PartialEq)]
pub struct PatchFieldValues {
    pub patch_type: String,
    pub data: PatchFieldData,
    pub is_fallback_from_owner: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TimeStepInfo {
    pub time: f64,
    pub time_dir: String,
    pub reconstructed: bool,
    pub available_fields: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct FoamField {
    pub name: String,
    pub class_name: String,
    pub dimensions: [i32; 7],
    pub internal_field: FoamFieldData,
    pub boundary_fields: HashMap<String, PatchFieldValues>,
}

impl FoamField {
    pub fn load(field_path: &Path, expected_size: Option<usize>) -> Result<Self, String> {
        Self::load_with_mesh(field_path, None, expected_size)
    }

    pub fn load_with_mesh(
        field_path: &Path,
        mesh: Option<&FoamMesh>,
        expected_size: Option<usize>,
    ) -> Result<Self, String> {
        let raw_bytes = read_file_or_gz(field_path)?;
        let mut stream = FoamStream::new(&raw_bytes);
        stream.skip_foam_header()?;

        let mut dimensions = [0; 7];
        let mut field_data: Option<FoamFieldData> = None;
        let mut class_name = String::new();
        let mut raw_boundaries: HashMap<String, (String, Option<PatchFieldData>)> = HashMap::new();

        while let Some(tok) = stream.next_token() {
            match tok {
                "dimensions" => {
                    match stream.next_token() {
                        Some("[") => {}
                        other => return Err(format!("Expected '[' after dimensions, got {:?}", other)),
                    }
                    for i in 0..7 {
                        let dim_str = stream.next_token().ok_or("Missing dimension integer")?;
                        dimensions[i] = dim_str.parse().map_err(|e| format!("Invalid dimension: {}", e))?;
                    }
                    match stream.next_token() {
                        Some("]") => {}
                        other => return Err(format!("Expected ']' after dimensions, got {:?}", other)),
                    }
                    if let Some(";") = stream.next_token() {}
                }
                "internalField" => {
                    let kind = stream.next_token().ok_or("Missing internalField specifier")?;
                    if kind == "uniform" {
                        let next = stream.next_token().ok_or("Missing uniform value")?;
                        if next == "(" {
                            let x: f64 = stream.next_token().ok_or("Missing x")?.parse().map_err(|e| format!("{}", e))?;
                            let y: f64 = stream.next_token().ok_or("Missing y")?.parse().map_err(|e| format!("{}", e))?;
                            let z: f64 = stream.next_token().ok_or("Missing z")?.parse().map_err(|e| format!("{}", e))?;
                            match stream.next_token() {
                                Some(")") => {}
                                other => return Err(format!("Expected ')' ending vector, got {:?}", other)),
                            }
                            let count = expected_size.unwrap_or(1);
                            field_data = Some(FoamFieldData::Vector(vec![[x, y, z]; count]));
                            class_name = "volVectorField".to_string();
                        } else {
                            let val: f64 = next.parse().map_err(|e| format!("Invalid uniform scalar: {}", e))?;
                            let count = expected_size.unwrap_or(1);
                            field_data = Some(FoamFieldData::Scalar(vec![val; count]));
                            class_name = "volScalarField".to_string();
                        }
                        if let Some(";") = stream.next_token() {}
                    } else if kind == "nonuniform" {
                        let type_tok = stream.next_token().ok_or("Missing nonuniform type")?;
                        let is_vector = type_tok.contains("vector");

                        let count_str = stream.next_token().ok_or("Missing nonuniform count")?;
                        let count: usize = count_str.parse().map_err(|e| format!("Invalid nonuniform count: {}", e))?;

                        if stream.is_binary() {
                            if is_vector {
                                let vecs = stream.read_binary_vector_slice(count)?;
                                field_data = Some(FoamFieldData::Vector(vecs));
                                class_name = "volVectorField".to_string();
                            } else {
                                let scalars = stream.read_binary_f64_slice(count)?;
                                field_data = Some(FoamFieldData::Scalar(scalars));
                                class_name = "volScalarField".to_string();
                            }
                        } else {
                            match stream.next_token() {
                                Some("(") => {}
                                other => return Err(format!("Expected '(' before nonuniform values, got {:?}", other)),
                            }

                            if is_vector {
                                let mut vecs = Vec::with_capacity(count);
                                for _ in 0..count {
                                    match stream.next_token() {
                                        Some("(") => {}
                                        other => return Err(format!("Expected '(' starting vector, got {:?}", other)),
                                    }
                                    let x: f64 = stream.next_token().ok_or("Missing x")?.parse().map_err(|e| format!("{}", e))?;
                                    let y: f64 = stream.next_token().ok_or("Missing y")?.parse().map_err(|e| format!("{}", e))?;
                                    let z: f64 = stream.next_token().ok_or("Missing z")?.parse().map_err(|e| format!("{}", e))?;
                                    match stream.next_token() {
                                        Some(")") => {}
                                        other => return Err(format!("Expected ')' ending vector, got {:?}", other)),
                                    }
                                    vecs.push([x, y, z]);
                                }
                                field_data = Some(FoamFieldData::Vector(vecs));
                                class_name = "volVectorField".to_string();
                            } else {
                                let mut scalars = Vec::with_capacity(count);
                                for _ in 0..count {
                                    let val: f64 = stream.next_token().ok_or("Missing scalar value")?.parse().map_err(|e| format!("{}", e))?;
                                    scalars.push(val);
                                }
                                field_data = Some(FoamFieldData::Scalar(scalars));
                                class_name = "volScalarField".to_string();
                            }

                            match stream.next_token() {
                                Some(")") => {}
                                other => return Err(format!("Expected ')' ending nonuniform list, got {:?}", other)),
                            }
                            if let Some(";") = stream.next_token() {}
                        }
                    }
                }
                "boundaryField" => {
                    match stream.next_token() {
                        Some("{") => {}
                        tok => return Err(format!("Expected '{{' after boundaryField, got {:?}", tok)),
                    }

                    loop {
                        let patch_name = match stream.next_token() {
                            Some("}") => break,
                            Some(name) => name.to_string(),
                            None => break,
                        };

                        match stream.next_token() {
                            Some("{") => {}
                            tok => return Err(format!("Expected '{{' starting boundary patch '{}', got {:?}", patch_name, tok)),
                        }

                        let mut patch_type = String::from("patch");
                        let mut p_data: Option<PatchFieldData> = None;

                        loop {
                            let key = match stream.next_token() {
                                Some("}") => break,
                                Some(k) => k,
                                None => break,
                            };

                            match key {
                                "type" => {
                                    patch_type = stream.next_token().ok_or("Missing patch type")?.to_string();
                                    if let Some(";") = stream.next_token() {}
                                }
                                "value" => {
                                    let kind = stream.next_token().ok_or("Missing value specifier")?;
                                    if kind == "uniform" {
                                        let val_tok = stream.next_token().ok_or("Missing uniform value")?;
                                        if val_tok == "(" {
                                            let x: f64 = stream.next_token().ok_or("Missing x")?.parse().map_err(|e| format!("{}", e))?;
                                            let y: f64 = stream.next_token().ok_or("Missing y")?.parse().map_err(|e| format!("{}", e))?;
                                            let z: f64 = stream.next_token().ok_or("Missing z")?.parse().map_err(|e| format!("{}", e))?;
                                            if let Some(")") = stream.next_token() {}
                                            p_data = Some(PatchFieldData::UniformVector([x, y, z]));
                                        } else {
                                            let v: f64 = val_tok.parse().map_err(|e| format!("Invalid uniform scalar: {}", e))?;
                                            p_data = Some(PatchFieldData::UniformScalar(v));
                                        }
                                        if let Some(";") = stream.next_token() {}
                                    } else if kind == "nonuniform" {
                                        let type_tok = stream.next_token().ok_or("Missing nonuniform type")?;
                                        let is_vector = type_tok.contains("vector");
                                        let count_str = stream.next_token().ok_or("Missing count")?;
                                        let count: usize = count_str.parse().map_err(|e| format!("Invalid count: {}", e))?;

                                        if stream.is_binary() {
                                            if is_vector {
                                                let vecs = stream.read_binary_vector_slice(count)?;
                                                p_data = Some(PatchFieldData::NonuniformVector(vecs));
                                            } else {
                                                let scalars = stream.read_binary_f64_slice(count)?;
                                                p_data = Some(PatchFieldData::NonuniformScalar(scalars));
                                            }
                                        } else {
                                            if let Some("(") = stream.next_token() {}
                                            if is_vector {
                                                let mut vecs = Vec::with_capacity(count);
                                                for _ in 0..count {
                                                    if let Some("(") = stream.next_token() {}
                                                    let x: f64 = stream.next_token().ok_or("Missing x")?.parse().map_err(|e| format!("{}", e))?;
                                                    let y: f64 = stream.next_token().ok_or("Missing y")?.parse().map_err(|e| format!("{}", e))?;
                                                    let z: f64 = stream.next_token().ok_or("Missing z")?.parse().map_err(|e| format!("{}", e))?;
                                                    if let Some(")") = stream.next_token() {}
                                                    vecs.push([x, y, z]);
                                                }
                                                p_data = Some(PatchFieldData::NonuniformVector(vecs));
                                            } else {
                                                let mut scalars = Vec::with_capacity(count);
                                                for _ in 0..count {
                                                    let v: f64 = stream.next_token().ok_or("Missing scalar")?.parse().map_err(|e| format!("{}", e))?;
                                                    scalars.push(v);
                                                }
                                                p_data = Some(PatchFieldData::NonuniformScalar(scalars));
                                            }
                                            if let Some(")") = stream.next_token() {}
                                            if let Some(";") = stream.next_token() {}
                                        }
                                    }
                                }
                                _ => {
                                    // Skip unknown entries until ';' or block
                                    let mut tok = stream.next_token();
                                    while let Some(t) = tok {
                                        if t == ";" {
                                            break;
                                        }
                                        if t == "{" {
                                            let mut d = 1;
                                            while d > 0 {
                                                match stream.next_token() {
                                                    Some("{") => d += 1,
                                                    Some("}") => d -= 1,
                                                    _ => {}
                                                }
                                            }
                                            break;
                                        }
                                        tok = stream.next_token();
                                    }
                                }
                            }
                        }

                        raw_boundaries.insert(patch_name, (patch_type, p_data));
                    }
                }
                _ => {}
            }
        }

        let internal_field = field_data.ok_or_else(|| format!("No internalField found in {}", field_path.display()))?;
        let name = field_path.file_name().and_then(|s| s.to_str()).unwrap_or("unknown").to_string();

        // Resolve boundaryField fallback with mesh if available
        let mut boundary_fields = HashMap::new();
        for (p_name, (p_type, p_val)) in raw_boundaries {
            if let Some(val) = p_val {
                boundary_fields.insert(
                    p_name,
                    PatchFieldValues {
                        patch_type: p_type,
                        data: val,
                        is_fallback_from_owner: false,
                    },
                );
            } else if let Some(m) = mesh {
                // Fallback to owner cell values for zeroGradient / calculated
                if let Some(patch) = m.boundaries.iter().find(|b| b.name == p_name) {
                    let n_faces = patch.n_faces;
                    let start_f = patch.start_face;
                    match &internal_field {
                        FoamFieldData::Scalar(scalars) => {
                            let mut patch_scalars = Vec::with_capacity(n_faces);
                            for f_idx in start_f..start_f + n_faces {
                                let o = m.owner.get(f_idx).copied().unwrap_or(0);
                                let val = scalars.get(o).copied().unwrap_or(0.0);
                                patch_scalars.push(val);
                            }
                            boundary_fields.insert(
                                p_name,
                                PatchFieldValues {
                                    patch_type: p_type,
                                    data: PatchFieldData::NonuniformScalar(patch_scalars),
                                    is_fallback_from_owner: true,
                                },
                            );
                        }
                        FoamFieldData::Vector(vectors) => {
                            let mut patch_vectors = Vec::with_capacity(n_faces);
                            for f_idx in start_f..start_f + n_faces {
                                let o = m.owner.get(f_idx).copied().unwrap_or(0);
                                let val = vectors.get(o).copied().unwrap_or([0.0, 0.0, 0.0]);
                                patch_vectors.push(val);
                            }
                            boundary_fields.insert(
                                p_name,
                                PatchFieldValues {
                                    patch_type: p_type,
                                    data: PatchFieldData::NonuniformVector(patch_vectors),
                                    is_fallback_from_owner: true,
                                },
                            );
                        }
                    }
                } else {
                    // Default fallback
                    let def_data = match &internal_field {
                        FoamFieldData::Scalar(_) => PatchFieldData::UniformScalar(0.0),
                        FoamFieldData::Vector(_) => PatchFieldData::UniformVector([0.0, 0.0, 0.0]),
                    };
                    boundary_fields.insert(
                        p_name,
                        PatchFieldValues {
                            patch_type: p_type,
                            data: def_data,
                            is_fallback_from_owner: false,
                        },
                    );
                }
            } else {
                let def_data = match &internal_field {
                    FoamFieldData::Scalar(_) => PatchFieldData::UniformScalar(0.0),
                    FoamFieldData::Vector(_) => PatchFieldData::UniformVector([0.0, 0.0, 0.0]),
                };
                boundary_fields.insert(
                    p_name,
                    PatchFieldValues {
                        patch_type: p_type,
                        data: def_data,
                        is_fallback_from_owner: false,
                    },
                );
            }
        }

        Ok(Self {
            name,
            class_name,
            dimensions,
            internal_field,
            boundary_fields,
        })
    }
}

/// Discovers numeric time directories in an OpenFOAM case, tagging reconstructed vs processor-only status.
pub fn list_time_dirs(case_path: &Path) -> Result<Vec<TimeStepInfo>, String> {
    if !case_path.exists() {
        return Err(format!("Case directory does not exist: {}", case_path.display()));
    }

    let mut time_map: HashMap<String, (f64, bool)> = HashMap::new();

    // 1. Scan root case directory for numeric time directories
    let entries = std::fs::read_dir(case_path)
        .map_err(|e| format!("Failed to read case dir {}: {}", case_path.display(), e))?;

    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("");
            if let Ok(t_val) = name.parse::<f64>() {
                time_map.insert(name.to_string(), (t_val, true));
            }
        }
    }

    // 2. Scan processor0..N directories for decomposed time steps
    let mut proc_idx = 0;
    loop {
        let proc_dir = case_path.join(format!("processor{}", proc_idx));
        if !proc_dir.exists() {
            break;
        }
        if let Ok(proc_entries) = std::fs::read_dir(&proc_dir) {
            for entry in proc_entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("");
                    if let Ok(t_val) = name.parse::<f64>() {
                        time_map.entry(name.to_string()).or_insert((t_val, false));
                    }
                }
            }
        }
        proc_idx += 1;
    }

    let mut time_steps: Vec<TimeStepInfo> = Vec::new();

    for (dir_name, (t_val, reconstructed)) in time_map {
        let target_dir = if reconstructed {
            case_path.join(&dir_name)
        } else {
            case_path.join("processor0").join(&dir_name)
        };

        let mut available_fields = Vec::new();
        if let Ok(field_entries) = std::fs::read_dir(&target_dir) {
            for fe in field_entries.flatten() {
                let fp = fe.path();
                if fp.is_file() {
                    let fname = fp.file_name().and_then(|s| s.to_str()).unwrap_or("");
                    if !fname.starts_with('.') && fname != "uniform" {
                        let clean_name = fname.strip_suffix(".gz").unwrap_or(fname).to_string();
                        if !available_fields.contains(&clean_name) {
                            available_fields.push(clean_name);
                        }
                    }
                }
            }
        }
        available_fields.sort();

        time_steps.push(TimeStepInfo {
            time: t_val,
            time_dir: dir_name,
            reconstructed,
            available_fields,
        });
    }

    time_steps.sort_by(|a, b| a.time.partial_cmp(&b.time).unwrap_or(std::cmp::Ordering::Equal));

    Ok(time_steps)
}
