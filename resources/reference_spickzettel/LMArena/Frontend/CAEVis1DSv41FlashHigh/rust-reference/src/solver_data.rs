//! Zero-copy binary readers for CFDGEO01 / CFDMSH02 / CFDFLD03.
//!
//! All headers are `#[repr(C)]` + `bytemuck::Pod`; payloads are validated once
//! and then borrowed directly from the memory map. No per-cell copies are made
//! before the data reaches `wgpu::Queue::write_buffer`.

use bytemuck::{Pod, Zeroable};
use memmap2::Mmap;
use std::fs::File;
use std::ops::Range;
use thiserror::Error;

pub const MAGIC_GEO: [u8; 8] = *b"CFDGEO01";
pub const MAGIC_MSH: [u8; 8] = *b"CFDMSH02";
pub const MAGIC_FLD: [u8; 8] = *b"CFDFLD03";

#[derive(Error, Debug)]
pub enum CfdError {
    #[error("cannot open {path}: {source}")]
    Io {
        path: String,
        #[source]
        source: std::io::Error,
    },
    #[error("invalid magic: expected {expected}, found {found:?}")]
    Magic {
        expected: &'static str,
        found: [u8; 8],
    },
    #[error("buffer “{0}” is too short for its declared payload")]
    Truncated(&'static str),
    #[error("payload is not aligned for the declared component type")]
    Alignment,
    #[error("SI dimension mismatch: field has {found}, expected {expected}")]
    Dimension { expected: String, found: String },
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct GeoHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub vertex_count: u32,
    pub index_count: u32,
    pub _reserved: u32,
    pub aabb_min: [f32; 3],
    pub aabb_max: [f32; 3],
    pub tolerance: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct MeshHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub dims: [u32; 3],
    pub cell_count: u64,
    pub cell_offset: u64,
    pub cell_dtype: u32,
    pub _padding: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct FieldHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub components: u32,
    pub cell_count: u64,
    pub time: f64,
    /// SI exponent vector [kg, m, s, K, mol, A, cd].
    pub dim: [f32; 7],
    pub dimension_pad: f32,
    pub quantity: [u8; 32],
    pub unit: [u8; 16],
}

impl FieldHeader {
    pub fn quantity_str(&self) -> &str {
        let end = self.quantity.iter().position(|b| *b == 0).unwrap_or(32);
        std::str::from_utf8(&self.quantity[..end]).unwrap_or("unknown")
    }
    pub fn unit_str(&self) -> &str {
        let end = self.unit.iter().position(|b| *b == 0).unwrap_or(16);
        std::str::from_utf8(&self.unit[..end]).unwrap_or("—")
    }
    pub fn dim_text(&self) -> String {
        const BASE: [&str; 7] = ["kg", "m", "s", "K", "mol", "A", "cd"];
        const SUP: [&str; 10] = ["⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"];
        let mut out = Vec::new();
        for (i, e) in self.dim.iter().enumerate() {
            let n = e.round() as i32;
            if n == 0 {
                continue;
            }
            if n == 1 {
                out.push(BASE[i].to_string());
            } else {
                let mut s = BASE[i].to_string();
                let mut v = n.abs();
                if n < 0 {
                    s.push('⁻');
                }
                let digits = v.to_string();
                v = 0;
                let _ = v;
                for d in digits.chars() {
                    s.push_str(SUP[d.to_digit(10).unwrap_or(0) as usize]);
                }
                out.push(s);
            }
        }
        if out.is_empty() { "1".into() } else { out.join("·") }
    }
    pub fn payload_bytes(&self) -> usize {
        self.cell_count as usize * self.components as usize * std::mem::size_of::<f32>()
    }
}

/// A memory-mapped field whose scalar payload is borrowed, never copied.
pub struct MappedField {
    map: Mmap,
    pub header: FieldHeader,
    payload: Range<usize>,
}

impl MappedField {
    pub fn open(path: impl AsRef<std::path::Path>) -> Result<Self, CfdError> {
        let path = path.as_ref();
        let file = File::open(path).map_err(|e| CfdError::Io {
            path: path.display().to_string(),
            source: e,
        })?;
        let map = unsafe { Mmap::map(&file) }.map_err(|e| CfdError::Io {
            path: path.display().to_string(),
            source: e,
        })?;
        if map.len() < std::mem::size_of::<FieldHeader>() {
            return Err(CfdError::Truncated("CFDFLD03 header"));
        }
        let header: FieldHeader = *bytemuck::from_bytes(&map[..std::mem::size_of::<FieldHeader>()]);
        if header.magic != MAGIC_FLD {
            return Err(CfdError::Magic {
                expected: "CFDFLD03",
                found: header.magic,
            });
        }
        let start = std::mem::size_of::<FieldHeader>();
        let end = start
            .checked_add(header.payload_bytes())
            .ok_or(CfdError::Truncated("CFDFLD03 payload"))?;
        if end > map.len() {
            return Err(CfdError::Truncated("CFDFLD03 payload"));
        }
        if start % std::mem::align_of::<f32>() != 0 {
            return Err(CfdError::Alignment);
        }
        Ok(Self { map, header, payload: start..end })
    }

    /// Zero-copy view of the scalar payload.
    pub fn scalars(&self) -> Result<&[f32], CfdError> {
        bytemuck::try_cast_slice(&self.map[self.payload.clone()]).map_err(|_| CfdError::Alignment)
    }

    pub fn byte_len(&self) -> usize {
        self.payload.len()
    }

    /// Upload straight into the GPU buffer. `wgpu` copies from this borrowed
    /// slice, so no intermediate `Vec<f32>` is allocated.
    pub fn write_to_buffer(&self, queue: &wgpu::Queue, buffer: &wgpu::Buffer) {
        if let Ok(values) = self.scalars() {
            queue.write_buffer(buffer, 0, bytemuck::cast_slice(values));
        }
    }
}

pub fn validate_geo(path: impl AsRef<std::path::Path>) -> Result<GeoHeader, CfdError> {
    let path = path.as_ref();
    let file = File::open(path).map_err(|e| CfdError::Io {
        path: path.display().to_string(),
        source: e,
    })?;
    let map = unsafe { Mmap::map(&file) }.map_err(|e| CfdError::Io {
        path: path.display().to_string(),
        source: e,
    })?;
    if map.len() < std::mem::size_of::<GeoHeader>() {
        return Err(CfdError::Truncated("CFDGEO01 header"));
    }
    let header: GeoHeader = *bytemuck::from_bytes(&map[..std::mem::size_of::<GeoHeader>()]);
    if header.magic != MAGIC_GEO {
        return Err(CfdError::Magic {
            expected: "CFDGEO01",
            found: header.magic,
        });
    }
    Ok(header)
}

/// A derived field produced by the SI-checked calculator. The dimension vector
/// travels with the GPU buffer so downstream visualisation can label the LUT.
#[derive(Clone, Debug)]
pub struct DerivedField {
    pub name: String,
    pub dim: [f32; 7],
    pub quantity: &'static str,
    pub unit: &'static str,
    pub min: f32,
    pub max: f32,
    pub scalars: Vec<f32>,
}

impl DerivedField {
    pub fn gpu_bytes(&self) -> u64 {
        std::mem::size_of_val(self.scalars.as_slice()) as u64
    }

    /// CFDFLD03 write: header + payload, both POD, suitable for bytemuck.
    pub fn encode(&self) -> Vec<u8> {
        let mut header = FieldHeader {
            magic: MAGIC_FLD,
            version: 3,
            components: 1,
            cell_count: self.scalars.len() as u64,
            time: 0.0,
            dim: self.dim,
            dimension_pad: 0.0,
            quantity: [0; 32],
            unit: [0; 16],
        };
        let q = self.quantity.as_bytes();
        header.quantity[..q.len().min(32)].copy_from_slice(&q[..q.len().min(32)]);
        let u = self.unit.as_bytes();
        header.unit[..u.len().min(16)].copy_from_slice(&u[..u.len().min(16)]);
        let mut out = Vec::with_capacity(std::mem::size_of::<FieldHeader>() + self.gpu_bytes() as usize);
        out.extend_from_slice(bytemuck::bytes_of(&header));
        out.extend_from_slice(bytemuck::cast_slice(&self.scalars));
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dimension_text_has_superscripts() {
        let header = FieldHeader {
            magic: MAGIC_FLD,
            version: 3,
            components: 1,
            cell_count: 4,
            time: 0.0,
            dim: [1.0, -1.0, -2.0, 0.0, 0.0, 0.0, 0.0],
            dimension_pad: 0.0,
            quantity: [0; 32],
            unit: [0; 16],
        };
        assert_eq!(header.dim_text(), "kg·m⁻¹·s⁻²");
        assert_eq!(header.payload_bytes(), 16);
    }

    #[test]
    fn derived_field_roundtrip_header() {
        let field = DerivedField {
            name: "p_total".into(),
            dim: [1.0, -1.0, -2.0, 0.0, 0.0, 0.0, 0.0],
            quantity: "Pressure",
            unit: "Pa",
            min: 0.0,
            max: 1.0,
            scalars: vec![1.0, 2.0, 3.0],
        };
        let bytes = field.encode();
        let header: &FieldHeader = bytemuck::from_bytes(&bytes[..std::mem::size_of::<FieldHeader>()]);
        assert_eq!(header.magic, MAGIC_FLD);
        assert_eq!(header.quantity_str(), "Pressure");
        assert_eq!(header.cell_count, 3);
    }
}
