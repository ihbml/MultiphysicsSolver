// DiFVM Mesher — compiled static graph mesh (SoA) + MeshErrorMap.
//
// This is the data structure the solver consumes: everything the finite-volume
// kernels need is precomputed once here and then treated as immutable, 64-byte
// aligned Structure-of-Arrays. It mirrors DiFVM Sec. 2.4.1 ("static graph
// construction") extended with the mesh-quality compensation map described in
// the project specification (MeshErrorMap / CMS-style error compensation).
#pragma once

#include <string>
#include <vector>

#include "difvm/core.hpp"
#include "difvm/polymesh.hpp"

namespace difvm {

// ---------------------------------------------------------------------------
// MeshErrorMap: tunables of the precomputed quality-compensation fields.
// ---------------------------------------------------------------------------
struct MeshErrorMapParams {
  // Non-orthogonality guard: above theta_crit the over-relaxed correction
  // vector k_f is smoothly (tanh) damped to zero.
  double theta_crit_deg = 65.0;
  double theta_width_deg = 8.0;
  // Skewness guard: sigma_f = |m_f^skew| / |d_ij|.  beta_f -> 0 around sigma0.
  double skew_sigma0 = 0.50;
  double skew_width = 0.15;
  // Cell-level shield: volume-jump penalty scale (neighbour ratio).
  double vol_ratio_scale = 8.0;
  // Floor values so that nothing is ever fully singular.
  double omega_min = 0.05;
  double beta_min = 0.0;
  // Below this normalization (relative to bbox diag) a mesh is treated as 2D.
  double degenerate_eps = 1e-12;
};

// ---------------------------------------------------------------------------
// Static graph mesh.  Face arrays are ordered exactly like OpenFOAM:
//   [0, n_internal)            -> internal faces (owner <-> neighbour)
//   [n_internal, n_faces)      -> boundary faces (owner only, neighbour = -1)
// The signed area vector Sf always points *out of the owner cell*.
// ---------------------------------------------------------------------------
struct GraphMesh {
  // ---- counts -----------------------------------------------------------
  gidx n_cells = 0;
  gidx n_internal = 0;
  gidx n_boundary = 0;
  gidx n_faces = 0;
  gidx n_boundary_faces() const { return n_boundary; }

  // ---- cell fields (SoA) ------------------------------------------------
  Vec3SoA cf;                     // cell centroid x_i
  AlignedVector<double> vol;      // cell volume V_i
  AlignedVector<float> cell_quality;   // 0..1 combined quality indicator
  AlignedVector<double> omega;    // MeshErrorMap: per-cell shield in (0,1]
  AlignedVector<double> vol_ratio;     // V_i / min_j V_j over face neighbours
  AlignedVector<double> cell_skew;     // max face skewness of the cell
  AlignedVector<double> cell_theta;    // max face non-orthogonality [deg]

  // ---- face / edge fields (SoA) ----------------------------------------
  AlignedVector<idx> owner;       // n_faces
  AlignedVector<idx> neighbour;   // n_faces, -1 on boundary
  AlignedVector<idx> patch_of;    // n_faces, -1 internal, else patch index
  Vec3SoA Sf;                     // signed area vector (out of owner)
  Vec3SoA xf;                     // face centroid
  Vec3SoA d_ij;                   // x_j - x_i (internal) / x_f - x_i (boundary)
  Vec3SoA delta_f;                // over-relaxed orthogonal part of Sf
  Vec3SoA k_f;                    // Sf - delta_f  (raw non-orthogonal part)
  Vec3SoA k_f_comp;               // tanh-damped non-orthogonal part
  Vec3SoA m_skew;                 // skewness offset vector (xf - x_intersect)
  AlignedVector<double> magSf;    // |Sf|
  AlignedVector<double> mag_d;    // |d_ij|
  AlignedVector<double> cos_ortho;    // cos(theta_f), theta_f = angle(Sf, d_ij)
  AlignedVector<double> theta;    // theta_f [deg]
  AlignedVector<double> non_ortho_corr;  // |Sf| / cos(theta)  (over-relaxed weight)
  AlignedVector<double> skewness; // sigma_f = |m_skew| / |d_ij|
  AlignedVector<double> beta;     // MeshErrorMap: upwind blending in [0,1]
  AlignedVector<double> face_warp;    // 1 - |Sf| / sum(triangle areas) of the face

  // ---- face -> vertex connectivity (CSR) --------------------------------
  std::vector<gidx> fv_offsets;   // n_faces + 1
  std::vector<idx> fv;            // concatenated point indices
  std::vector<Vec3> points;       // (deduplicated) mesh points

  // ---- node (cell) -> face / cell adjacency (CSR) -----------------------
  std::vector<gidx> cf_offsets;   // n_cells + 1   (cell -> faces)
  std::vector<idx> cf_faces;      // face ids, outward orientation implied by Sf
  std::vector<gidx> nn_offsets;   // n_cells + 1   (cell -> neighbouring cells)
  std::vector<idx> nn_cells;      // neighbour cell ids
  std::vector<std::int8_t> nn_sign;   // +1 if this cell is owner of the face, -1 if neighbour

  // ---- patches ----------------------------------------------------------
  std::vector<PatchInfo> patches;

  // ---- provenance -------------------------------------------------------
  std::string source;             // "openfoam:/path", "vtk:/path", "cutcell:sphere.stl"
  MeshErrorMapParams err_params;

  double cell_volume(gidx i) const { return vol[static_cast<std::size_t>(i)]; }
  Vec3 cell_centre(gidx i) const { return cf.get(static_cast<std::size_t>(i)); }
  gidx n_faces_of_cell(gidx i) const {
    return cf_offsets[static_cast<std::size_t>(i) + 1] - cf_offsets[static_cast<std::size_t>(i)];
  }
  bool is_boundary_face(gidx f) const { return f >= n_internal; }

  // Bytes allocated by the graph (approximate, useful for reporting).
  std::size_t memory_bytes() const;
  void clear();
};

// ---------------------------------------------------------------------------
// Build / compile
// ---------------------------------------------------------------------------
struct BuildOptions {
  // Weld coincident vertices (needed for STL/VTK sources, harmless otherwise).
  bool merge_duplicate_points = true;
  double merge_tolerance_rel = 1e-9;  // relative to bbox diagonal
  bool remove_unused_points = true;
  // Enforce outward face orientation from geometry (VTK / generic input).
  bool orient_faces = true;
  // Face orientation is already outward (generators guarantee it).  The
  // geometric heuristic uses the cell's face-centroid average as an interior
  // point, which is wrong for strongly non-convex cut cells, so callers that
  // know the orientation must say so.
  bool assume_oriented = false;
  // Build cell->face / cell->cell adjacency (needed by solvers, costs 2 passes).
  bool build_adjacency = true;
  MeshErrorMapParams err;
  int threads = 0;   // 0 -> all hardware threads
};

// Quality / integrity audit of a compiled mesh.
struct MeshAudit {
  gidx n_cells = 0, n_faces = 0;
  double total_volume = 0.0, boundary_area = 0.0;
  Vec3 lo{}, hi{};
  double min_volume = 0.0, max_volume = 0.0;
  double min_vol_ratio = 1.0, max_vol_ratio = 1.0;
  gidx n_negative_cells = 0;
  gidx n_zero_volume_cells = 0;
  double max_closure_residual = 0.0;   // max_i |sum_f Sf| / sum_f |Sf|
  gidx n_open_cells = 0;
  double max_face_warp = 0.0;
  double max_non_ortho_deg = 0.0, mean_non_ortho_deg = 0.0;
  double p95_non_ortho_deg = 0.0;
  double max_skewness = 0.0, mean_skewness = 0.0;
  double min_beta = 1.0, mean_beta = 1.0;
  double min_omega = 1.0, mean_omega = 1.0;
  gidx n_cells_beta_below_0p5 = 0;
  // face-pair consistency: sum of |Sf| of both sides of an internal face must match
  double max_face_area_mismatch = 0.0;
  gidx n_boundary_faces = 0;
  std::string to_string(bool verbose = false) const;
  bool ok() const;
};

// Permute face arrays. order[i] = source face index for output face i.  The
// caller guarantees that internal faces stay grouped before boundary faces;
// cell adjacency, patch offsets and face->vertex CSR are rebuilt.
void permute_faces(GraphMesh& gm, const std::vector<idx>& order);

// Re-partition the boundary faces into patches: faces lying on the bounding
// box become xmin/xmax/... patches, every other boundary face (cut-cell or
// embedded interface) goes to `interface_name`.
void auto_patch_boundary(GraphMesh& gm, const Vec3& lo, const Vec3& hi,
                         double tol_rel = 1e-6,
                         const std::string& interface_name = "solidInterface",
                         const std::string& interface_type = "wall");

// Compile a raw polyhedral mesh into the static graph + error map.
GraphMesh build_graph_mesh(const PolyMeshRaw& raw, const BuildOptions& opts = {});

// Audit a compiled mesh (parallel, allocates temporaries).
MeshAudit audit_mesh(const GraphMesh& gm, int threads = 0);

// Histogram helper used by the CLI "info" command.
std::string quality_histogram(const GraphMesh& gm, int bins = 10);

}  // namespace difvm
