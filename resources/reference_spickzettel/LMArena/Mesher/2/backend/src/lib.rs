//! Library surface of the Rust core: geometry, meshing, Eulerian SPH,
//! adaptive coupling operators and exporters.
//!
//! Use it directly (CLI, tests, WASM) or through the HTTP layer in `main.rs`:
//!
//! ```rust
//! use fvm_sph_mesher::{geometry::GeometryId, mesh, sph};
//!
//! let m = mesh::build(GeometryId::Cylinder, &mesh::MeshParams::default());
//! println!("{} cells, closure {:.1e}", m.stats.n_cells, m.stats.closure_residual);
//!
//! let p = sph::run(GeometryId::Cylinder, &sph::SphParams {
//!     dp: 0.055, h_factor: 1.3, iters: 60, kernel: sph::KernelId::Wendland,
//!     refine_ratio: 1.0, grading: 0.6, volume_reinit: true,
//!     lattice: "square".into(), core: None,
//! });
//! println!("{} particles, {} within boundaries", p.n, p.diag.n_boundary);
//! ```

pub mod couple;
pub mod export;
pub mod geometry;
pub mod mesh;
pub mod sph;
