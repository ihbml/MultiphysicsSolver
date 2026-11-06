//! Solver-ready writers: legacy VTK and the DiFVM scatter/gather graph.

use serde_json::json;

use crate::geometry;
use crate::mesh::MeshOut;
use crate::sph::SphOut;

pub fn mesh_vtk(m: &MeshOut) -> String {
    let mut s = String::with_capacity(64 * 1024);
    s.push_str("# vtk DataFile Version 3.0\nFVM/SPH Mesher - Rust core\nASCII\nDATASET UNSTRUCTURED_GRID\n");
    s.push_str(&format!("POINTS {} double\n", m.points.len() / 2));
    for i in 0..m.points.len() / 2 {
        s.push_str(&format!(
            "{} {} 0\n",
            m.points[2 * i], m.points[2 * i + 1]
        ));
    }
    s.push_str(&format!(
        "CELLS {} {}\n",
        m.n_cells,
        4 * m.n_cells
    ));
    for c in 0..m.n_cells {
        s.push_str(&format!(
            "3 {} {} {}\n",
            m.tris[3 * c],
            m.tris[3 * c + 1],
            m.tris[3 * c + 2]
        ));
    }
    s.push_str(&format!("CELL_TYPES {}\n", m.n_cells));
    for _ in 0..m.n_cells {
        s.push_str("5\n");
    }
    s.push_str(&format!("CELL_DATA {}\n", m.n_cells));
    s.push_str("SCALARS quality float 1\nLOOKUP_TABLE default\n");
    for q in &m.quality {
        s.push_str(&format!("{q}\n"));
    }
    s.push_str("SCALARS volume float 1\nLOOKUP_TABLE default\n");
    for v in &m.cv {
        s.push_str(&format!("{v:e}\n"));
    }
    s
}

pub fn particles_vtk(p: &SphOut) -> String {
    let mut s = String::with_capacity(32 * 1024);
    s.push_str("# vtk DataFile Version 3.0\nEulerian SPH particles\nASCII\nDATASET POLYDATA\n");
    s.push_str(&format!("POINTS {} double\n", p.n));
    for i in 0..p.n {
        s.push_str(&format!("{} {} 0\n", p.x[i], p.y[i]));
    }
    s.push_str(&format!("VERTICES {} {}\n", p.n, 2 * p.n));
    for i in 0..p.n {
        s.push_str(&format!("1 {i}\n"));
    }
    s.push_str(&format!("POINT_DATA {}\n", p.n));
    s.push_str("SCALARS Volume float 1\nLOOKUP_TABLE default\n");
    for v in &p.volume {
        s.push_str(&format!("{v:e}\n"));
    }
    s.push_str("SCALARS SmoothingLength float 1\nLOOKUP_TABLE default\n");
    for v in &p.h {
        s.push_str(&format!("{v:e}\n"));
    }
    s.push_str("SCALARS BoundarySurface float 1\nLOOKUP_TABLE default\n");
    for v in &p.as_mag {
        s.push_str(&format!("{v:e}\n"));
    }
    s.push_str("SCALARS ZeroOrderResidual float 1\nLOOKUP_TABLE default\n");
    for v in &p.eps0 {
        s.push_str(&format!("{v:e}\n"));
    }
    s.push_str("VECTORS BoundaryNormal float\n");
    for i in 0..p.n {
        s.push_str(&format!("{} {} 0\n", p.as_nx[i], p.as_ny[i]));
    }
    s
}

pub fn graph_json(m: &MeshOut, id: geometry::GeometryId) -> serde_json::Value {
    let mut src = Vec::new();
    let mut dst = Vec::new();
    for f in 0..m.n_faces {
        let nb = m.neigh[f];
        if nb < 0 {
            continue;
        }
        let o = m.owner[f];
        src.push(o);
        dst.push(nb);
        src.push(nb);
        dst.push(o);
    }
    json!({
        "_format": "difvm-graph/1.0",
        "geometry": format!("{id:?}").to_lowercase(),
        "nCells": m.n_cells,
        "nFaces": m.n_faces,
        "nInternalFaces": m.stats.n_internal,
        "cellCentres": m.cc,
        "cellVolumes": m.cv,
        "owner": m.owner,
        "neighbour": m.neigh,
        "faceCentres": m.fc,
        "faceAreas": m.fa,
        "faceNormals": m.normals,
        "faceWeights": m.weight,
        "nonOrthoAngleDeg": m.non_ortho,
        "nonOrthoCorrection": m.non_ortho_corr,
        "edgeSrc": src,
        "edgeDst": dst,
        "rcmPermutation": m.rcm,
        "stats": m.stats,
    })
}
