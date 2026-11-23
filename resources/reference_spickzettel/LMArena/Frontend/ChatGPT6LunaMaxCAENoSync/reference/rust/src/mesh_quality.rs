use bytemuck::{Pod, Zeroable};
use serde::{Deserialize, Serialize};

pub const FLAG_FOLDED: u32 = 1 << 0;
pub const FLAG_NAN: u32 = 1 << 1;
pub const FLAG_ABOVE_THRESHOLD: u32 = 1 << 2;

/// CPU-side shape for CFDMSH02 per-cell quality records.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct CellMetrics {
    pub skewness: f32,
    pub non_orthogonality_deg: f32,
    pub aspect_ratio: f32,
    pub cell_volume: f32,
    pub wsrd_weights: [f32; 4],
    pub amips_energy: f32,
    pub folded_face_indices: Vec<u32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MetricKind { Skewness, NonOrthogonality, AspectRatio, CellVolume }

impl MetricKind {
    pub fn label(self) -> &'static str {
        match self {
            Self::Skewness => "Skewness",
            Self::NonOrthogonality => "Non-orthogonality",
            Self::AspectRatio => "Aspect ratio",
            Self::CellVolume => "Cell volume",
        }
    }

    fn value(self, metrics: &CellMetrics) -> f32 {
        match self {
            Self::Skewness => metrics.skewness,
            Self::NonOrthogonality => metrics.non_orthogonality_deg,
            Self::AspectRatio => metrics.aspect_ratio,
            Self::CellVolume => metrics.cell_volume,
        }
    }
}

/// Expanded triangle vertex. `cell_value` and `flags` are intentionally duplicated
/// over every corner so the GPU never interpolates a cell diagnostic across cells.
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct FlatQualityVertex {
    pub position: [f32; 3],
    pub cell_value: f32,
    pub cell_id: u32,
    pub flags: u32,
    pub face_group_id: u32,
}

#[derive(Clone, Debug, Default)]
pub struct MeshErrorMap4 {
    pub skewness: Vec<f32>,
    pub non_orthogonality: Vec<f32>,
    pub aspect_ratio: Vec<f32>,
    pub cell_volume: Vec<f32>,
    pub nan_cells: u64,
    pub folded_faces: u64,
}

impl MeshErrorMap4 {
    pub fn from_cells(cells: &[CellMetrics]) -> Self {
        let mut map = Self::default();
        for cell in cells {
            map.nan_cells += [cell.skewness, cell.non_orthogonality_deg, cell.aspect_ratio, cell.cell_volume]
                .iter().any(|value| !value.is_finite()) as u64;
            map.folded_faces += cell.folded_face_indices.len() as u64;
            map.skewness.push(cell.skewness);
            map.non_orthogonality.push(cell.non_orthogonality_deg);
            map.aspect_ratio.push(cell.aspect_ratio);
            map.cell_volume.push(cell.cell_volume);
        }
        map
    }

    /// Expand indices into a flat triangle list for a cell-location quality render.
    pub fn flat_vertices(
        &self,
        vertices: &[[f32; 3]],
        triangles: &[[u32; 3]],
        triangle_cell: &[u32],
        face_group_id: &[u32],
        cells: &[CellMetrics],
        metric: MetricKind,
        threshold: f32,
    ) -> Vec<FlatQualityVertex> {
        let mut output = Vec::with_capacity(triangles.len() * 3);
        for (triangle_index, triangle) in triangles.iter().enumerate() {
            let cell_id = match triangle_cell.get(triangle_index).copied() { Some(id) => id as usize, None => continue };
            let cell = match cells.get(cell_id) { Some(cell) => cell, None => continue };
            let raw = metric.value(cell);
            let is_nan = !raw.is_finite();
            let mut flags = if !cell.folded_face_indices.is_empty() { FLAG_FOLDED } else { 0 };
            if is_nan { flags |= FLAG_NAN; }
            if !is_nan && raw > threshold { flags |= FLAG_ABOVE_THRESHOLD; }
            let cell_value = if is_nan { 0.0 } else { raw };
            let group = face_group_id.get(triangle_index).copied().unwrap_or(0);
            for vertex_index in triangle {
                if let Some(position) = vertices.get(*vertex_index as usize) {
                    output.push(FlatQualityVertex { position: *position, cell_value, cell_id: cell_id as u32, flags, face_group_id: group });
                }
            }
        }
        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flat_expansion_repeats_cell_value_and_flags_nan() {
        let cells = [CellMetrics { skewness: f32::NAN, ..CellMetrics::default() }];
        let map = MeshErrorMap4::from_cells(&cells);
        let expanded = map.flat_vertices(
            &[[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
            &[[0, 1, 2]], &[0], &[17], &cells, MetricKind::Skewness, 0.5,
        );
        assert_eq!(expanded.len(), 3);
        assert!(expanded.iter().all(|vertex| vertex.flags & FLAG_NAN != 0));
        assert!(expanded.iter().all(|vertex| vertex.cell_value == 0.0));
    }
}