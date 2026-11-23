use crate::physics::Dimensions;
use bytemuck::{Pod, Zeroable};
use thiserror::Error;

const GEO_MAGIC: &[u8; 8] = b"CFDGEO01";
const MESH_MAGIC: &[u8; 8] = b"CFDMSH02";
const FIELD_MAGIC: &[u8; 8] = b"CFDFLD03";

#[derive(Debug, Error)]
pub enum BufferError {
    #[error("buffer is shorter than its header")]
    ShortHeader,
    #[error("unexpected binary format magic")]
    BadMagic,
    #[error("unsupported format version {0}")]
    Version(u32),
    #[error("invalid array offset or count")]
    Bounds,
    #[error("binary array is not aligned for a zero-copy view")]
    Alignment,
    #[error("unknown field location {0}")]
    Location(u32),
    #[error("unknown field source {0}")]
    Source(u32),
    #[error("field component count must be 1 or 3")]
    Components,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct GeoHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub flags: u32,
    pub vertex_count: u64,
    pub normal_count: u64,
    pub triangle_count: u64,
    pub edge_segment_count: u64,
}

pub struct CfdGeo01View<'a> {
    pub vertices_f64: &'a [f64],
    pub normals_f32: &'a [f32],
    pub triangles_u32: &'a [u32],
    pub face_group_id: &'a [u32],
    pub open_non_manifold_edges: &'a [u32],
}

impl<'a> CfdGeo01View<'a> {
    /// Borrow the packed little-endian payload without copying any vertex/index data.
    pub fn parse(bytes: &'a [u8]) -> Result<Self, BufferError> {
        let header = read_header::<GeoHeader>(bytes)?;
        if &header.magic != GEO_MAGIC { return Err(BufferError::BadMagic); }
        if header.version != 1 { return Err(BufferError::Version(header.version)); }
        let triangle_count = usize::try_from(header.triangle_count).map_err(|_| BufferError::Bounds)?;
        let edge_count = usize::try_from(header.edge_segment_count).map_err(|_| BufferError::Bounds)?;
        let mut offset = std::mem::size_of::<GeoHeader>();
        let vertices = cast_array::<f64>(bytes, &mut offset, checked_count(header.vertex_count, 3)?)?;
        let normals = cast_array::<f32>(bytes, &mut offset, checked_count(header.normal_count, 3)?)?;
        let triangles = cast_array::<u32>(bytes, &mut offset, triangle_count.checked_mul(3).ok_or(BufferError::Bounds)?)?;
        let groups = cast_array::<u32>(bytes, &mut offset, triangle_count)?;
        let edges = cast_array::<u32>(bytes, &mut offset, edge_count.checked_mul(2).ok_or(BufferError::Bounds)?)?;
        Ok(Self { vertices_f64: vertices, normals_f32: normals, triangles_u32: triangles, face_group_id: groups, open_non_manifold_edges: edges })
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct MeshHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub reserved: u32,
    pub cell_count: u64,
    pub folded_face_index_count: u64,
}

pub struct CfdMsh02View<'a> {
    pub skewness: &'a [f32],
    pub non_orthogonality: &'a [f32],
    pub aspect_ratio: &'a [f32],
    pub cell_volume: &'a [f32],
    pub wsrd_weights: &'a [f32],
    pub amips_energy: &'a [f32],
    pub folded_face_indices: &'a [u32],
}

impl<'a> CfdMsh02View<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self, BufferError> {
        let header = read_header::<MeshHeader>(bytes)?;
        if &header.magic != MESH_MAGIC { return Err(BufferError::BadMagic); }
        if header.version != 2 { return Err(BufferError::Version(header.version)); }
        let cell_count = usize::try_from(header.cell_count).map_err(|_| BufferError::Bounds)?;
        let mut offset = std::mem::size_of::<MeshHeader>();
        let skewness = cast_array::<f32>(bytes, &mut offset, cell_count)?;
        let non_orthogonality = cast_array::<f32>(bytes, &mut offset, cell_count)?;
        let aspect_ratio = cast_array::<f32>(bytes, &mut offset, cell_count)?;
        let cell_volume = cast_array::<f32>(bytes, &mut offset, cell_count)?;
        let wsrd_weights = cast_array::<f32>(bytes, &mut offset, checked_count(header.cell_count, 4)?)?;
        let amips_energy = cast_array::<f32>(bytes, &mut offset, cell_count)?;
        let folded_count = usize::try_from(header.folded_face_index_count).map_err(|_| BufferError::Bounds)?;
        let folded_face_indices = cast_array::<u32>(bytes, &mut offset, folded_count)?;
        Ok(Self { skewness, non_orthogonality, aspect_ratio, cell_volume, wsrd_weights, amips_energy, folded_face_indices })
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct FieldHeader {
    pub magic: [u8; 8],
    pub version: u32,
    pub location: u32,
    pub source: u32,
    pub component_count: u32,
    pub value_count: u64,
    pub dimensions: [i8; 7],
    pub reserved: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FieldLocation { Cell, Point, BoundaryPatch }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FieldSource { BoundaryField, OwnerCellFallback }

pub struct FoamFieldView<'a> {
    pub location: FieldLocation,
    pub source: FieldSource,
    pub dimensions: Dimensions,
    pub component_count: u32,
    pub values_f32: &'a [f32],
}

impl<'a> FoamFieldView<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self, BufferError> {
        let header = read_header::<FieldHeader>(bytes)?;
        if &header.magic != FIELD_MAGIC { return Err(BufferError::BadMagic); }
        if header.version != 3 { return Err(BufferError::Version(header.version)); }
        if header.component_count != 1 && header.component_count != 3 { return Err(BufferError::Components); }
        let location = match header.location { 0 => FieldLocation::Cell, 1 => FieldLocation::Point, 2 => FieldLocation::BoundaryPatch, other => return Err(BufferError::Location(other)) };
        let source = match header.source { 0 => FieldSource::BoundaryField, 1 => FieldSource::OwnerCellFallback, other => return Err(BufferError::Source(other)) };
        let count = checked_count(header.value_count, header.component_count as usize)?;
        let mut offset = std::mem::size_of::<FieldHeader>();
        let values_f32 = cast_array::<f32>(bytes, &mut offset, count)?;
        Ok(Self { location, source, dimensions: header.dimensions, component_count: header.component_count, values_f32 })
    }
}

fn read_header<T: Pod>(bytes: &[u8]) -> Result<T, BufferError> {
    let size = std::mem::size_of::<T>();
    let header = bytes.get(..size).ok_or(BufferError::ShortHeader)?;
    bytemuck::try_pod_read_unaligned(header).map_err(|_| BufferError::ShortHeader)
}

fn cast_array<'a, T: Pod>(bytes: &'a [u8], offset: &mut usize, count: usize) -> Result<&'a [T], BufferError> {
    let byte_count = count.checked_mul(std::mem::size_of::<T>()).ok_or(BufferError::Bounds)?;
    let end = offset.checked_add(byte_count).ok_or(BufferError::Bounds)?;
    let slice = bytes.get(*offset..end).ok_or(BufferError::Bounds)?;
    *offset = end;
    bytemuck::try_cast_slice(slice).map_err(|_| BufferError::Alignment)
}

fn checked_count(value: u64, multiplier: usize) -> Result<usize, BufferError> {
    usize::try_from(value).ok().and_then(|value| value.checked_mul(multiplier)).ok_or(BufferError::Bounds)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_short_and_wrong_magic_buffers() {
        assert!(matches!(CfdGeo01View::parse(&[0; 3]), Err(BufferError::ShortHeader)));
        let header = GeoHeader { magic: *b"NOTGEO00", version: 1, flags: 0, vertex_count: 0, normal_count: 0, triangle_count: 0, edge_segment_count: 0 };
        assert!(matches!(CfdGeo01View::parse(bytemuck::bytes_of(&header)), Err(BufferError::BadMagic)));
    }
}