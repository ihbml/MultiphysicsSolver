#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #if defined(CFD_MESHER_EXPORTS)
    #define CFD_EXPORT __declspec(dllexport)
  #else
    #define CFD_EXPORT __declspec(dllexport) // Export when building DLL
  #endif
#else
  #define CFD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque native mesh handle
typedef struct CfdNativeMesh CfdNativeMesh;

// Declarative Meshing Configuration matching models.py MeshingConfig
typedef struct {
    double h_min;
    double h_max;
    double h0;
    double grading;
    double alpha_curv;
    double beta_prox;
    
    // Boundary Layer Inflation
    int32_t num_bl_layers;
    double bl_first_height;
    double bl_growth_rate;
    int32_t bl_pinching_enabled;
    double bl_pinching_angle_deg;
    
    // Graph & Solver Optimizations
    int32_t enable_rcm;
    int32_t enable_cht;
    int32_t enable_cyclic;
    double cyclic_angle_rad;
    int32_t distmesh_iters;
} MeshingConfigC;

// Boundary Patch Descriptor
typedef struct {
    char name[64];
    char patch_type[32]; // "wall", "inlet", "outlet", "coupledWall", "cyclic"
    int64_t start_face;
    int64_t num_faces;
    int32_t region_id;
} BoundaryPatchC;

// Core Lifecycle Functions
CFD_EXPORT CfdNativeMesh* cfd_mesher_create(void);
CFD_EXPORT void cfd_mesher_free(CfdNativeMesh* mesh);

// Generation Entry Points
CFD_EXPORT int32_t cfd_mesher_generate_from_surface(
    CfdNativeMesh* mesh,
    const double* points,
    int64_t n_points,
    const int32_t* triangles,
    int64_t n_triangles,
    const MeshingConfigC* config
);

CFD_EXPORT int32_t cfd_mesher_generate_2d_polygon(
    CfdNativeMesh* mesh,
    const double* polygon_xy,
    int64_t n_vertices,
    const MeshingConfigC* config
);

CFD_EXPORT int32_t cfd_mesher_generate_hexcore(
    CfdNativeMesh* mesh,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    const double* farfield_bbox,
    int32_t domain_mode,
    double h0,
    const MeshingConfigC* config
);

CFD_EXPORT int32_t cfd_mesher_build_from_cells(
    CfdNativeMesh* mesh,
    const double* points,
    int64_t n_points,
    const int64_t* cell_offsets,
    const int64_t* cell_nodes,
    int64_t n_cells,
    const int32_t* cell_regions
);

// Topology Array Accessors (Exact parity with FoamMesh C-ABI)
CFD_EXPORT int64_t cfd_mesher_get_n_points(const CfdNativeMesh* mesh);
CFD_EXPORT int64_t cfd_mesher_get_n_cells(const CfdNativeMesh* mesh);
CFD_EXPORT int64_t cfd_mesher_get_n_faces(const CfdNativeMesh* mesh);
CFD_EXPORT int64_t cfd_mesher_get_n_internal_faces(const CfdNativeMesh* mesh);
CFD_EXPORT int64_t cfd_mesher_get_n_patches(const CfdNativeMesh* mesh);

CFD_EXPORT const double* cfd_mesher_get_points_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const int64_t* cfd_mesher_get_owner_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const int64_t* cfd_mesher_get_neighbour_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_face_areas_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_face_centres_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_cell_centres_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_cell_volumes_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_delta_f_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_k_f_ptr(const CfdNativeMesh* mesh);

// Cell-Vertex Connectivity Accessors (for unstructured polyhedral cells)
CFD_EXPORT const int64_t* cfd_mesher_get_cell_offsets_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const int64_t* cfd_mesher_get_cell_nodes_ptr(const CfdNativeMesh* mesh);

// Face-to-Node Connectivity Accessors (CSR)
CFD_EXPORT const int64_t* cfd_mesher_get_face_offsets_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const int64_t* cfd_mesher_get_face_nodes_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT int64_t cfd_mesher_get_n_face_nodes(const CfdNativeMesh* mesh);

// Cell Types & Topological Dimension Accessors
CFD_EXPORT const int32_t* cfd_mesher_get_cell_types_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT int32_t cfd_mesher_get_topological_dim(const CfdNativeMesh* mesh);

// Error-Map & Quality Accessors (including CMS compensation fields)
CFD_EXPORT const double* cfd_mesher_get_non_orthogonality_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_skewness_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_aspect_ratio_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const int32_t* cfd_mesher_get_stabilization_mask_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_m_skew_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_k_f_comp_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_beta_upwind_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_omega_cell_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT const double* cfd_mesher_get_wsrd_alpha_ptr(const CfdNativeMesh* mesh);

// CHT & Symmetry Accessors
CFD_EXPORT const int32_t* cfd_mesher_get_cell_regions_ptr(const CfdNativeMesh* mesh);
CFD_EXPORT int32_t cfd_mesher_get_boundary_patch(
    const CfdNativeMesh* mesh,
    int64_t patch_idx,
    BoundaryPatchC* out_patch
);

// Extract rotational cyclic periodic boundary face pairs: R_z(theta) * c_f1 == c_f2
CFD_EXPORT int64_t cfd_mesher_extract_cyclic_pairs(
    CfdNativeMesh* mesh,
    double sector_angle_rad,
    double tolerance,
    int64_t* out_pair_f1,
    int64_t* out_pair_f2,
    int64_t max_pairs
);

// Physical Invariant Audit
CFD_EXPORT double cfd_mesher_compute_max_divergence_closure(const CfdNativeMesh* mesh);
CFD_EXPORT double cfd_mesher_compute_max_decomposition_error(const CfdNativeMesh* mesh);

// ==============================================================================
// Signed Distance Field (SDF) C-ABI Engine
// ==============================================================================
typedef struct CfdSdfEngine CfdSdfEngine;

CFD_EXPORT CfdSdfEngine* cfd_sdf_create(
    const double* vertices, int64_t n_vertices,
    const int32_t* faces, int64_t n_faces
);
CFD_EXPORT void cfd_sdf_free(CfdSdfEngine* engine);
CFD_EXPORT int32_t cfd_sdf_evaluate(
    CfdSdfEngine* engine,
    const double* points, int64_t n_points,
    double* out_phi, double* out_grad
);
CFD_EXPORT int32_t cfd_sdf_winding_number(
    CfdSdfEngine* engine,
    const double* points, int64_t n_points,
    double* out_wn
);
CFD_EXPORT int32_t cfd_sdf_voromesh_loss(
    const double* points, int64_t n_points,
    const double* seeds, int64_t n_seeds,
    int32_t k,
    double* out_loss, double* out_dists, double* out_grads,
    const double* seed_signs
);

#ifdef __cplusplus
}
#endif


