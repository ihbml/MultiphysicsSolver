pub mod inspect;
pub mod io;
pub mod math;
pub mod mesh;
pub mod primitives;

pub use inspect::{RayHit, measure_angle_deg, measure_distance, ray_intersect_mesh};
pub use io::{CadIoError, ImportedAssembly, ImportedPart, load_cad_file};
pub use math::{
    Aabb3, OrbitCameraMath, Plane3, Ray3, reverse_z_orthographic_rh, reverse_z_perspective_rh,
};
pub use mesh::{
    CadMaterialData, CadMesh, CadVertex, CreaseConfig, EdgeKind, EdgeVertex, MassProperties,
    compute_mass_properties, extract_crease_edges,
};
pub use primitives::{
    AssemblyPartSpec, DemoAssemblyKind, build_demo_assembly, make_box, make_bracket,
    make_cylinder, make_flange, make_hex_bolt, make_spur_gear, make_stepped_shaft, make_torus,
    make_tube,
};
