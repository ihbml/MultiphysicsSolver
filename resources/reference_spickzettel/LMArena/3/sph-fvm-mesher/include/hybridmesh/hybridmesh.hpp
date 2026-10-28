#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace hybridmesh {

using Index = std::uint32_t;
inline constexpr Index no_index = std::numeric_limits<Index>::max();

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    double& operator[](std::size_t axis);
    const double& operator[](std::size_t axis) const;
};
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, double s);
Vec3 operator/(Vec3 a, double s);
Vec3& operator+=(Vec3& a, Vec3 b);
double dot(Vec3 a, Vec3 b);
double norm(Vec3 a);

enum class MeshRole : std::uint8_t { common, interpolation, sleeping };
enum class ParticleRole : std::uint8_t { common, transition, interpolation };
const char* name(MeshRole role);
const char* name(ParticleRole role);

struct Patch { std::string name; };
struct Face {
    std::vector<Index> vertices;  // ordered, outward with respect to owner
    Index owner = no_index;
    Index neighbor = no_index;     // no_index for a physical boundary
    Index patch = no_index;        // valid on physical boundary only
    Vec3 centroid;
    Vec3 area_vector;              // outward with respect to owner; length in 2D, area in 3D
};
struct Cell {
    std::vector<Index> vertices;  // original polygon order in 2D; unique vertex IDs in 3D
    std::vector<Index> faces;
    Vec3 centroid;
    Vec3 bounds_min, bounds_max;
    double volume = 0.0;         // area in 2D, volume in 3D
    std::uint8_t vtk_type = 0;    // 0 for an arbitrary OpenFOAM polyhedron
};
struct Mesh {
    int dimension = 0;
    std::vector<Vec3> points;
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<Patch> patches;
    Vec3 bounds_min, bounds_max;
};

// All coordinates are in physical units. These make quad/hex FVM meshes.
Mesh make_box2d(Index nx, Index ny, double lx, double ly);
Mesh make_box3d(Index nx, Index ny, Index nz, double lx, double ly, double lz);
// ASCII Gmsh 2.2, first-order triangle/quad/tetra/hex/prism/pyramid cells.
Mesh read_gmsh22(const std::filesystem::path& file);
// ASCII OpenFOAM constant/polyMesh (or a direct polyMesh folder); convex cells.
Mesh read_openfoam(const std::filesystem::path& directory);

// Immutable face-to-cell graph, with CSR cell neighbors and corresponding face IDs.
struct Graph {
    std::vector<Index> face_owner, face_neighbor;
    std::vector<std::size_t> neighbor_offsets;
    std::vector<Index> neighbor_ids, neighbor_face_ids;
};
Graph make_graph(const Mesh& mesh);
// One signed face flux per face (owner -> neighbor), including physical-boundary fluxes.
// Returns sum of incident signed fluxes, or divergence when divide_by_volume=true.
std::vector<double> scatter_face_flux(const Mesh& mesh, std::span<const double> flux,
                                      bool divide_by_volume = false);

struct Region {
    enum class Kind { box, sphere } kind = Kind::box;
    Vec3 low, high;     // box: bounds; sphere: low=center
    double radius = 0;  // sphere radius
    static Region box(Vec3 low, Vec3 high, int dim);
    static Region sphere(Vec3 center, double radius, int dim);
    double signed_distance(Vec3 point, int dim) const; // negative inside
    bool overlaps(Vec3 bounds_min, Vec3 bounds_max, double padding, int dim) const;
};

// Change the six (or four) box thresholds toward an observed interface, one
// quantized step at a time. Returns false when all changes lie in the dead band.
// The interface must remain in the mesh domain. No physical fields are modified.
bool adapt_box_to_interface(Region& core, std::span<const Vec3> interface_points,
                            const Mesh& mesh, double margin, double step);

struct Options {
    double spacing = 0.0;
    double support_ratio = 2.6;  // kernel cutoff / particle spacing
    double mesh_band = 2.0;      // interpolation-mesh band in units of spacing
};
struct Particle {
    Vec3 position;
    double volume = 0.0;
    Index owner_cell = no_index;
    ParticleRole role = ParticleRole::common;
    bool centroid_fallback = false;
    double signed_core_distance = 0.0;
    double position_divergence = 0.0;
    bool physical_boundary = false;
    Vec3 closure_area;    // virtual SPH boundary contribution: -sum_j V_j grad_i W_ij
    Vec3 closure_normal;  // closure_area normalized, zero if no closure
    double wall_distance = std::numeric_limits<double>::infinity();
    Index wall_patch = no_index;
};
struct ParticleSet {
    std::vector<Particle> particles;
    std::vector<MeshRole> cell_roles;
    // CSR of all ordered SPH neighbors within support radius (no self entries).
    std::vector<std::size_t> neighbor_offsets;
    std::vector<Index> neighbor_ids;
    // CSR of particles grouped by owner mesh cell (may contain several roles).
    std::vector<std::size_t> cell_particle_offsets;
    std::vector<Index> cell_particle_ids;
    double spacing = 0.0, support_radius = 0.0;
};
ParticleSet discretize(const Mesh& mesh, const Region& core, const Options& options);

struct TransferPlan {
    // P -> M: average Taylor reconstructions from common particles in each
    // interpolation cell. Empty cells use nearby common particles as fallback.
    std::vector<std::size_t> p2m_offsets; // per cell, including non-interpolation cells
    std::vector<Index> p2m_particles;
    // M -> P: affine-reproducing weighted least squares on common mesh centers.
    std::vector<std::size_t> m2p_offsets; // per particle, empty for common particles
    std::vector<Index> m2p_cells;
    std::vector<double> m2p_weights;
    // Geometry-only inverse of the corrected SPH moment per particle.
    std::vector<std::array<double, 9>> inverse_moments;
    std::vector<std::uint8_t> valid_moments;
    std::size_t p2m_fallbacks = 0, m2p_fallbacks = 0, singular_moments = 0;
};
TransferPlan make_transfer_plan(const Mesh& mesh, const ParticleSet& particles);
// Returns a cell-sized vector. Non-interpolation cells contain NaN.
std::vector<double> particle_to_mesh_opmis(const Mesh& mesh, const ParticleSet& particles,
                                           const TransferPlan& plan,
                                           std::span<const double> particle_scalar);
// Returns a particle-sized vector. Common particles contain NaN.
std::vector<double> mesh_to_halo_particles(const Mesh& mesh, const ParticleSet& particles,
                                           const TransferPlan& plan,
                                           std::span<const double> mesh_scalar);
// Zhang et al.'s linearly blended transition layer: 1 at the inner edge, 0 at the outer.
double blend_transition(const Particle& particle, double sph_value,
                        double fvm_value, double support_radius);

// CSV is deliberately simple and machine-readable; all face/node/patch/role IDs
// are zero-based, and -1 represents no_index in the exported files.
void write_csv(const std::filesystem::path& directory, const Mesh& mesh,
               const ParticleSet& particles, const TransferPlan& plan, bool include_links = true);

} // namespace hybridmesh
