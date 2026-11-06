#include "cfd_mesher_native.h"
#include "difvm_mesh.hpp"
#include "distmesh.hpp"
#include "inflation.hpp"
#include "rcm.hpp"
#include "cht_cyclic.hpp"
#include "sdf_engine.hpp"
#include "octree_hexcore.hpp"
#include <cstring>
#include <vector>
#include <algorithm>
#include <exception>

extern "C" {

CFD_EXPORT CfdNativeMesh* cfd_mesher_create(void) {
  try {
    return new CfdNativeMesh();
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT void cfd_mesher_free(CfdNativeMesh* mesh) {
  try {
    if (mesh) {
      delete mesh;
    }
  } catch (...) {
    // Suppress all exceptions during handle deletion
  }
}

CFD_EXPORT int32_t cfd_mesher_generate_from_surface(
    CfdNativeMesh* mesh,
    const double* points,
    int64_t n_points,
    const int32_t* triangles,
    int64_t n_triangles,
    const MeshingConfigC* config
) {
  try {
    if (!mesh || !points || !triangles || n_points <= 0 || n_triangles <= 0) {
      return -1;
    }

    cfd::DistMeshOptions options;
    if (config) {
      options.h0 = config->h0 > 0.0 ? config->h0 : 0.05;
      options.h_min = config->h_min > 0.0 ? config->h_min : 0.005;
      options.h_max = config->h_max > 0.0 ? config->h_max : 0.2;
      options.max_iters = config->distmesh_iters > 0 ? config->distmesh_iters : 45;
    }

    cfd::DistMeshEngine::generateFromSurface(
        mesh->data,
        points,
        n_points,
        triangles,
        n_triangles,
        options,
        config
    );

    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_mesher_generate_2d_polygon(
    CfdNativeMesh* mesh,
    const double* polygon_xy,
    int64_t n_vertices,
    const MeshingConfigC* config
) {
  try {
    if (!mesh || !polygon_xy || n_vertices < 3) {
      return -1;
    }

    cfd::DistMeshOptions options;
    options.h0 = (config && config->h0 > 0.0) ? config->h0 : 0.0;
    if (config && config->distmesh_iters > 0) {
      options.max_iters = config->distmesh_iters;
    }

    cfd::DistMeshEngine::generate2DPolygon(
        mesh->data,
        polygon_xy,
        n_vertices,
        options,
        config
    );

    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

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
) {
  try {
    if (!mesh || !surf_points || !surf_faces || n_points <= 0 || n_faces <= 0 || h0 <= 0.0) {
      return -1;
    }

    cfd::OctreeOptions options;
    options.h0 = h0;
    options.domain_mode = domain_mode;
    if (config) {
      if (config->h_min > 0.0) options.h_min = config->h_min;
      if (config->h_max > 0.0) options.h_max = config->h_max;
      options.enable_rcm = (config->enable_rcm != 0);
    }

    cfd::OctreeHexcore hexcore;
    int32_t res = hexcore.generate(
        mesh->data,
        surf_points,
        n_points,
        surf_faces,
        n_faces,
        farfield_bbox,
        options
    );
    return res;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_mesher_compute_shoelace_area(
    const double* polygon_xy,
    int64_t n_vertices,
    double* out_area
) {
  try {
    if (!polygon_xy || n_vertices < 3 || !out_area) return -1;
    std::vector<cfd::Vec2> poly(n_vertices);
    for (int64_t i = 0; i < n_vertices; ++i) {
      poly[i] = cfd::Vec2(polygon_xy[2 * i], polygon_xy[2 * i + 1]);
    }
    *out_area = cfd::computeShoelaceArea(poly);
    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_mesher_canonicalize_polygon_ccw(
    const double* polygon_xy,
    int64_t n_vertices,
    double* out_xy,
    int32_t* was_reversed,
    double* out_area
) {
  try {
    if (!polygon_xy || n_vertices < 3 || !out_xy || !was_reversed || !out_area) return -1;
    std::vector<cfd::Vec2> poly(n_vertices);
    for (int64_t i = 0; i < n_vertices; ++i) {
      poly[i] = cfd::Vec2(polygon_xy[2 * i], polygon_xy[2 * i + 1]);
    }
    double area = 0.0;
    bool rev = cfd::canonicalizePolygonCCW(poly, &area);
    if (std::abs(area) < 1e-14) return -1;
    for (int64_t i = 0; i < n_vertices; ++i) {
      out_xy[2 * i] = poly[i].x;
      out_xy[2 * i + 1] = poly[i].y;
    }
    *was_reversed = rev ? 1 : 0;
    *out_area = area;
    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_mesher_ear_clipping_triangulate(
    const double* polygon_xy,
    int64_t n_vertices,
    int32_t* out_triangles,
    int64_t* out_n_triangles
) {
  try {
    if (!polygon_xy || n_vertices < 3 || !out_triangles || !out_n_triangles) return -1;
    std::vector<cfd::Vec2> poly(n_vertices);
    for (int64_t i = 0; i < n_vertices; ++i) {
      poly[i] = cfd::Vec2(polygon_xy[2 * i], polygon_xy[2 * i + 1]);
    }
    auto tris = cfd::earClippingTriangulate(poly);
    *out_n_triangles = static_cast<int64_t>(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
      out_triangles[3 * i] = tris[i][0];
      out_triangles[3 * i + 1] = tris[i][1];
      out_triangles[3 * i + 2] = tris[i][2];
    }
    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_mesher_build_from_cells(
    CfdNativeMesh* mesh,
    const double* points,
    int64_t n_points,
    const int64_t* cell_offsets,
    const int64_t* cell_nodes,
    int64_t n_cells,
    const int32_t* cell_regions
) {
  try {
    if (!mesh || !points || !cell_offsets || !cell_nodes || n_points <= 0 || n_cells <= 0) {
      return -1;
    }

    // Comprehensive bounds checking on cell CSR buffers
    if (cell_offsets[0] != 0) {
      return -1;
    }
    for (int64_t c = 0; c < n_cells; ++c) {
      if (cell_offsets[c + 1] < cell_offsets[c]) {
        return -1;
      }
    }
    int64_t total_nodes = cell_offsets[n_cells];
    if (total_nodes < 0) {
      return -1;
    }
    for (int64_t k = 0; k < total_nodes; ++k) {
      if (cell_nodes[k] < 0 || cell_nodes[k] >= n_points) {
        return -1;
      }
    }

    std::vector<cfd::Vec3> node_coords(n_points);
    for (int64_t i = 0; i < n_points; ++i) {
      node_coords[i] = cfd::Vec3(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    }

    std::vector<std::vector<int64_t>> cells(n_cells);
    for (int64_t c = 0; c < n_cells; ++c) {
      int64_t off0 = cell_offsets[c];
      int64_t off1 = cell_offsets[c + 1];
      cells[c].reserve(off1 - off0);
      for (int64_t k = off0; k < off1; ++k) {
        cells[c].push_back(cell_nodes[k]);
      }
    }

    std::vector<int32_t> regions;
    if (cell_regions) {
      regions.assign(cell_regions, cell_regions + n_cells);
    }

    cfd::DiFVMTraversal::buildTopology(mesh->data, node_coords, cells, regions);
    return 0;
  } catch (const std::exception&) {
    return -2;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_points(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.n_points() : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_cells(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.n_cells() : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_faces(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.n_faces() : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_internal_faces(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.n_internal_faces() : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_patches(const CfdNativeMesh* mesh) {
  try {
    return mesh ? static_cast<int64_t>(mesh->data.patches.size()) : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT const double* cfd_mesher_get_points_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.points.empty()) ? mesh->data.points.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_owner_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.owner.empty()) ? mesh->data.owner.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_neighbour_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.neighbour.empty()) ? mesh->data.neighbour.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_face_areas_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.face_areas.empty()) ? mesh->data.face_areas.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_face_centres_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.face_centres.empty()) ? mesh->data.face_centres.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_cell_centres_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_centres.empty()) ? mesh->data.cell_centres.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_cell_volumes_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_volumes.empty()) ? mesh->data.cell_volumes.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_delta_f_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.delta_f.empty()) ? mesh->data.delta_f.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_k_f_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.k_f.empty()) ? mesh->data.k_f.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_cell_offsets_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_offsets.empty()) ? mesh->data.cell_offsets.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_cell_nodes_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_nodes.empty()) ? mesh->data.cell_nodes.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_face_offsets_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.face_offsets.empty()) ? mesh->data.face_offsets.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int64_t* cfd_mesher_get_face_nodes_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.face_nodes.empty()) ? mesh->data.face_nodes.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT int64_t cfd_mesher_get_n_face_nodes(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.n_face_nodes() : 0;
  } catch (...) {
    return 0;
  }
}

CFD_EXPORT const int32_t* cfd_mesher_get_cell_types_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_types.empty()) ? mesh->data.cell_types.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT int32_t cfd_mesher_get_topological_dim(const CfdNativeMesh* mesh) {
  try {
    return mesh ? mesh->data.topological_dim : 3;
  } catch (...) {
    return 3;
  }
}

CFD_EXPORT const double* cfd_mesher_get_non_orthogonality_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.non_ortho.empty()) ? mesh->data.non_ortho.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_skewness_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.skewness.empty()) ? mesh->data.skewness.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_aspect_ratio_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.aspect_ratio.empty()) ? mesh->data.aspect_ratio.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int32_t* cfd_mesher_get_stabilization_mask_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.stab_mask.empty()) ? mesh->data.stab_mask.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_m_skew_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.m_skew.empty()) ? mesh->data.m_skew.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_k_f_comp_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.k_f_comp.empty()) ? mesh->data.k_f_comp.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_beta_upwind_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.beta_upwind.empty()) ? mesh->data.beta_upwind.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_omega_cell_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.omega_cell.empty()) ? mesh->data.omega_cell.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const double* cfd_mesher_get_wsrd_alpha_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.wsrd_alpha.empty()) ? mesh->data.wsrd_alpha.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT const int32_t* cfd_mesher_get_cell_regions_ptr(const CfdNativeMesh* mesh) {
  try {
    return (mesh && !mesh->data.cell_regions.empty()) ? mesh->data.cell_regions.data() : nullptr;
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT int32_t cfd_mesher_get_boundary_patch(
    const CfdNativeMesh* mesh,
    int64_t patch_idx,
    BoundaryPatchC* out_patch
) {
  try {
    if (!mesh || !out_patch || patch_idx < 0 || patch_idx >= static_cast<int64_t>(mesh->data.patches.size())) {
      return -1;
    }
    *out_patch = mesh->data.patches[patch_idx];
    return 0;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int64_t cfd_mesher_extract_cyclic_pairs(
    CfdNativeMesh* mesh,
    double sector_angle_rad,
    double tolerance,
    int64_t* out_pair_f1,
    int64_t* out_pair_f2,
    int64_t max_pairs
) {
  try {
    if (!mesh || !out_pair_f1 || !out_pair_f2 || max_pairs <= 0) return 0;
    auto pairs = cfd::CHTCyclicProcessor::extractCyclicPairs(mesh->data, sector_angle_rad, tolerance);
    int64_t count = std::min(static_cast<int64_t>(pairs.size()), max_pairs);
    for (int64_t i = 0; i < count; ++i) {
      out_pair_f1[i] = pairs[i].first;
      out_pair_f2[i] = pairs[i].second;
    }
    return count;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT double cfd_mesher_compute_max_divergence_closure(const CfdNativeMesh* mesh) {
  try {
    return mesh ? cfd::DiFVMTraversal::computeMaxDivergenceClosure(mesh->data) : 0.0;
  } catch (...) {
    return 0.0;
  }
}

CFD_EXPORT double cfd_mesher_compute_max_decomposition_error(const CfdNativeMesh* mesh) {
  try {
    return mesh ? cfd::DiFVMTraversal::computeMaxDecompositionError(mesh->data) : 0.0;
  } catch (...) {
    return 0.0;
  }
}

// ==============================================================================
// Signed Distance Field (SDF) C-ABI Implementation
// ==============================================================================

struct CfdSdfEngine {
  cfd::MeshSDF sdf;
  CfdSdfEngine(const double* vertices, int64_t n_vertices,
               const int32_t* faces, int64_t n_faces)
      : sdf(vertices, n_vertices, faces, n_faces) {}
};

CFD_EXPORT CfdSdfEngine* cfd_sdf_create(
    const double* vertices, int64_t n_vertices,
    const int32_t* faces, int64_t n_faces
) {
  try {
    if (!vertices || !faces || n_vertices <= 0 || n_faces <= 0) {
      return nullptr;
    }
    return new CfdSdfEngine(vertices, n_vertices, faces, n_faces);
  } catch (...) {
    return nullptr;
  }
}

CFD_EXPORT void cfd_sdf_free(CfdSdfEngine* engine) {
  try {
    if (engine) {
      delete engine;
    }
  } catch (...) {
  }
}

CFD_EXPORT int32_t cfd_sdf_evaluate(
    CfdSdfEngine* engine,
    const double* points, int64_t n_points,
    double* out_phi, double* out_grad
) {
  try {
    if (!engine || !points || !out_phi || n_points <= 0) {
      return -1;
    }
    return engine->sdf.evaluate(points, n_points, out_phi, out_grad);
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_sdf_winding_number(
    CfdSdfEngine* engine,
    const double* points, int64_t n_points,
    double* out_wn
) {
  try {
    if (!engine || !points || !out_wn || n_points <= 0) {
      return -1;
    }
    return engine->sdf.winding_number(points, n_points, out_wn);
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_sdf_voromesh_loss(
    const double* points, int64_t n_points,
    const double* seeds, int64_t n_seeds,
    int32_t k,
    double* out_loss, double* out_dists, double* out_grads,
    const double* seed_signs
) {
  try {
    if (!points || !seeds || n_points <= 0 || n_seeds < 2 || k <= 0) {
      return -1;
    }
    return cfd::MeshSDF::voromesh_loss(
        points, n_points, seeds, n_seeds, k, out_loss, out_dists, out_grads, seed_signs
    );
  } catch (...) {
    return -2;
  }
}

} // extern "C"

