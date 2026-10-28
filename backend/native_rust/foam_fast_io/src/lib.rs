//! foam_fast_io - High-performance zero-copy OpenFOAM case & field reader for CFDSolver.

pub mod ffi;
pub mod fields;
pub mod mesh;
pub mod parser;

pub use fields::{FoamField, FoamFieldData};
pub use mesh::{FoamBoundaryPatch, FoamMesh};
pub use parser::load_poly_mesh;

#[cfg(feature = "python")]
use pyo3::prelude::*;

#[cfg(feature = "python")]
#[pymodule]
fn foam_fast_io(_py: Python, m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add("__version__", env!("CARGO_PKG_VERSION"))?;
    Ok(())
}
