//! fields.rs - Parsing and storage of OpenFOAM scalar and vector fields.

use crate::parser::{read_file_or_gz, FoamStream};
use std::path::Path;

#[derive(Debug, Clone)]
pub enum FoamFieldData {
    Scalar(Vec<f64>),
    Vector(Vec<[f64; 3]>),
}

#[derive(Debug, Clone)]
pub struct FoamField {
    pub name: String,
    pub class_name: String,
    pub dimensions: [i32; 7],
    pub internal_field: FoamFieldData,
}

impl FoamField {
    pub fn load(field_path: &Path, expected_size: Option<usize>) -> Result<Self, String> {
        let raw_bytes = read_file_or_gz(field_path)?;
        let mut stream = FoamStream::new(&raw_bytes);
        stream.skip_foam_header()?;

        let mut dimensions = [0; 7];
        let mut field_data: Option<FoamFieldData> = None;
        let mut class_name = String::new();

        while let Some(tok) = stream.next_token() {
            match tok {
                "dimensions" => {
                    // Expect '['
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
                            // Vector value (x y z)
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
                            // Scalar value
                            let val: f64 = next.parse().map_err(|e| format!("Invalid uniform scalar: {}", e))?;
                            let count = expected_size.unwrap_or(1);
                            field_data = Some(FoamFieldData::Scalar(vec![val; count]));
                            class_name = "volScalarField".to_string();
                        }
                        if let Some(";") = stream.next_token() {}
                    } else if kind == "nonuniform" {
                        // e.g. nonuniform List<scalar> 2500 ( ... ) or nonuniform List<vector> 2500 ( ... )
                        let type_tok = stream.next_token().ok_or("Missing nonuniform type")?;
                        let is_vector = type_tok.contains("vector");

                        let count_str = stream.next_token().ok_or("Missing nonuniform count")?;
                        let count: usize = count_str.parse().map_err(|e| format!("Invalid nonuniform count: {}", e))?;

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
                "boundaryField" => {
                    // We successfully parsed internalField, stop here for fast extraction
                    break;
                }
                _ => {}
            }
        }

        let internal_field = field_data.ok_or_else(|| format!("No internalField found in {}", field_path.display()))?;
        let name = field_path.file_name().and_then(|s| s.to_str()).unwrap_or("unknown").to_string();

        Ok(Self {
            name,
            class_name,
            dimensions,
            internal_field,
        })
    }
}
