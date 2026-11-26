#include "cfd_mesher_native.h"
#include "difvm_mesh.hpp"
#include "distmesh.hpp"
#include "inflation.hpp"
#include "rcm.hpp"
#include "cht_cyclic.hpp"
#include "sdf_engine.hpp"
#include "octree_hexcore.hpp"
#include "linear_octree.hpp"
#include <cstring>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <algorithm>
#include <exception>

static_assert(sizeof(MeshingConfigC) == 120, "MeshingConfigC ABI size drift");
static_assert(offsetof(MeshingConfigC, num_bl_layers) == 48, "MeshingConfigC offset drift");
static_assert(offsetof(MeshingConfigC, bl_first_height) == 56, "MeshingConfigC offset drift");
static_assert(offsetof(MeshingConfigC, enable_rcm) == 88, "MeshingConfigC offset drift");
static_assert(offsetof(MeshingConfigC, cyclic_angle_rad) == 104, "MeshingConfigC offset drift");
static_assert(offsetof(MeshingConfigC, distmesh_iters) == 112, "MeshingConfigC offset drift");
static_assert(sizeof(BoundaryPatchC) == 120, "BoundaryPatchC ABI size drift");
static_assert(offsetof(BoundaryPatchC, start_face) == 96, "BoundaryPatchC offset drift");

namespace {

thread_local std::string g_last_global_error;

void set_err(CfdNativeMesh* mesh, const char* msg) {
  g_last_global_error = msg ? msg : "";
  if (mesh) {
    mesh->last_error = g_last_global_error;
  }
}

bool validate_surface_inputs(
    CfdNativeMesh* mesh,
    const double* pts,
    int64_t n_points,
    const int32_t* tris,
    int64_t n_faces
) {
  if (!pts || !tris || n_points < 3 || n_faces <= 0) {
    set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=null_or_empty_surface");
    return false;
  }
  for (int64_t i = 0; i < 3 * n_points; ++i) {
    if (!std::isfinite(pts[i])) {
      set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=non_finite_vertex_coordinate");
      return false;
    }
  }
  for (int64_t f = 0; f < 3 * n_faces; ++f) {
    int32_t idx = tris[f];
    if (idx < 0 || static_cast<int64_t>(idx) >= n_points) {
      set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=triangle_vertex_index_out_of_bounds");
      return false;
    }
  }
  return true;
}

}  // namespace

extern "C" {

CFD_EXPORT int32_t cfd_mesher_abi_version(void) {
  return 2;
}

CFD_EXPORT const char* cfd_abi_layout_json(void) {
  static const char* kLayoutJson =
      "{\"abi_version\":2,\"structs\":{"
      "\"MeshingConfigC\":{\"size\":120,\"offsets\":{"
      "\"h_min\":0,\"h_max\":8,\"h0\":16,\"grading\":24,\"alpha_curv\":32,\"beta_prox\":40,"
      "\"num_bl_layers\":48,\"bl_first_height\":56,\"bl_growth_rate\":64,"
      "\"bl_pinching_enabled\":72,\"bl_pinching_angle_deg\":80,"
      "\"enable_rcm\":88,\"enable_cht\":92,\"enable_cyclic\":96,"
      "\"cyclic_angle_rad\":104,\"distmesh_iters\":112}},"
      "\"BoundaryPatchC\":{\"size\":120,\"offsets\":{"
      "\"name\":0,\"patch_type\":64,\"start_face\":96,\"num_faces\":104,\"region_id\":112}}"
      "}}";
  return kLayoutJson;
}

CFD_EXPORT int32_t cfd_mesher_last_error(const CfdNativeMesh* mesh, char* buf, size_t cap) {
  if (!buf || cap == 0) return -1;
  const std::string& src = (mesh && !mesh->last_error.empty()) ? mesh->last_error : g_last_global_error;
  std::snprintf(buf, cap, "%s", src.c_str());
  return static_cast<int32_t>(src.size());
}

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

CFD_EXPORT void cfd_mesher_free_mesh(CfdNativeMesh* mesh) {
  cfd_mesher_free(mesh);
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
    if (!mesh || !validate_surface_inputs(mesh, points, n_points, triangles, n_triangles)) {
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
    if (!mesh || !std::isfinite(h0) || h0 <= 0.0 ||
        !validate_surface_inputs(mesh, surf_points, n_points, surf_faces, n_faces)) {
      if (mesh && mesh->last_error.empty()) {
        set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=invalid_h0_or_null_mesh");
      }
      return -1;
    }
    if (farfield_bbox) {
      for (int k = 0; k < 6; ++k) {
        if (!std::isfinite(farfield_bbox[k])) {
          set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=non_finite_farfield_bbox");
          return -1;
        }
      }
      if (farfield_bbox[0] >= farfield_bbox[3] ||
          farfield_bbox[1] >= farfield_bbox[4] ||
          farfield_bbox[2] >= farfield_bbox[5]) {
        set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=inverted_farfield_bbox");
        return -1;
      }
    }
    mesh->last_error.clear();

    cfd::OctreeOptions options;
    options.h0 = h0;
    options.domain_mode = domain_mode;
    if (config) {
      if (config->h_min > 0.0) options.h_min = config->h_min;
      if (config->h_max > 0.0) options.h_max = config->h_max;
      if (config->grading > 0.0) options.grading = config->grading;
      if (config->alpha_curv > 0.0) options.alpha_curv = config->alpha_curv;
      if (config->beta_prox > 0.0) options.beta_prox = config->beta_prox;
      options.enable_rcm = (config->enable_rcm != 0);
      if (config->num_bl_layers >= 0) options.num_bl_layers = config->num_bl_layers;
      if (config->bl_first_height > 0.0) options.bl_first_height = config->bl_first_height;
      if (config->bl_growth_rate > 0.0) options.bl_growth_rate = config->bl_growth_rate;
      options.bl_pinching_enabled = (config->bl_pinching_enabled != 0);
      if (config->bl_pinching_angle_deg > 0.0) options.bl_pinching_angle_deg = config->bl_pinching_angle_deg;
      options.enable_cht = (config->enable_cht != 0);
      options.enable_cyclic = (config->enable_cyclic != 0);
      options.cyclic_angle_rad = config->cyclic_angle_rad;
      if (config->distmesh_iters >= 0) options.distmesh_iters = config->distmesh_iters;
      if (config->h_min > 0.0 && options.h0 > 0.0) {
        if (config->h_min >= options.h0) {
          options.max_depth = 0;
        } else {
          const int32_t raw_depth =
              static_cast<int32_t>(std::ceil(std::log2(options.h0 / config->h_min))) + 1;
          options.max_depth = (raw_depth < 1) ? 1 : ((raw_depth > 4) ? 4 : raw_depth);
        }
      }
    } else {
      options.h_min = h0 * 0.25;
      options.h_max = h0 * 4.0;
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
    if (res == -12) {
      set_err(mesh, "pass=octree_hexcore code=BUDGET_OR_OMP_ERROR detail=octree_node_budget_exceeded_or_omp_failure");
    } else if (res == -13) {
      set_err(mesh, "pass=refine_and_balance code=CONVERGENCE_ERROR detail=octree_2to1_balance_did_not_converge_within_32_iterations");
    } else if (res == -14) {
      set_err(mesh, "pass=faces_and_vertices code=CONVERGENCE_ERROR detail=conformal_midpoint_insertion_did_not_converge_within_8_passes");
    } else if (res < 0 && mesh->last_error.empty()) {
      set_err(mesh, "pass=octree_hexcore code=GENERATION_ERROR detail=hexcore_generation_failed");
    }
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

    for (int64_t i = 0; i < 3 * n_points; ++i) {
      if (!std::isfinite(points[i])) {
        set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=non_finite_point");
        return -1;
      }
    }
    for (int64_t c = 0; c < n_cells; ++c) {
      int64_t nv = cell_offsets[c + 1] - cell_offsets[c];
      if (nv < 3) {
        set_err(mesh, "pass=c_abi code=INVALID_ARGUMENT detail=cell_fewer_than_3_vertices");
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
    return mesh ? cfd::DiFVMTraversal::computeMaxDivergenceClosure(mesh->data)
                : std::numeric_limits<double>::quiet_NaN();
  } catch (...) {
    return std::numeric_limits<double>::quiet_NaN();
  }
}

CFD_EXPORT double cfd_mesher_compute_max_decomposition_error(const CfdNativeMesh* mesh) {
  try {
    return mesh ? cfd::DiFVMTraversal::computeMaxDecompositionError(mesh->data)
                : std::numeric_limits<double>::quiet_NaN();
  } catch (...) {
    return std::numeric_limits<double>::quiet_NaN();
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
    if (!validate_surface_inputs(nullptr, vertices, n_vertices, faces, n_faces)) {
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

CFD_EXPORT int32_t cfd_sdf_segment_blocked(
    void* sdf_handle,
    const double* pts_a,
    const double* pts_b,
    int64_t n_seg,
    uint8_t* out_blocked
) {
  try {
    auto* engine = static_cast<CfdSdfEngine*>(sdf_handle);
    if (!engine || n_seg < 0) {
      set_err(nullptr, "pass=sdf_segment_blocked code=INVALID_ARGUMENT detail=null_handle_or_negative_n_seg");
      return -1;
    }
    if (n_seg == 0) {
      return 0;
    }
    if (!pts_a || !pts_b || !out_blocked) {
      set_err(nullptr, "pass=sdf_segment_blocked code=INVALID_ARGUMENT detail=null_segment_buffer");
      return -1;
    }
    int32_t rc = engine->sdf.segments_blocked(pts_a, pts_b, n_seg, out_blocked);
    if (rc == -1) {
      set_err(nullptr, "pass=sdf_segment_blocked code=INVALID_ARGUMENT detail=non_finite_segment_coordinate");
    } else if (rc == -2) {
      set_err(nullptr, "pass=sdf_segment_blocked code=OMP_ERROR detail=openmp_exception_in_segment_blocked");
    }
    return rc;
  } catch (...) {
    return -2;
  }
}

CFD_EXPORT int32_t cfd_morton_octree_build_stats(
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    double h0,
    int32_t max_depth,
    const double* farfield_bbox,
    int64_t* out_n_leaves,
    int64_t* out_n_cells,
    int64_t* out_n_regular_faces,
    int64_t* out_n_general_faces,
    int64_t* out_n_vertices,
    uint64_t* out_peak_bytes,
    double* out_build_time_s,
    int32_t* out_balanced_ok
) {
  try {
    if (!std::isfinite(h0) || h0 <= 0.0 || max_depth < 0 ||
        !validate_surface_inputs(nullptr, surf_points, n_points, surf_faces, n_faces)) {
      if (g_last_global_error.empty()) {
        set_err(nullptr, "pass=morton_octree code=INVALID_ARGUMENT detail=invalid_h0_or_max_depth");
      }
      return -1;
    }
    if (farfield_bbox) {
      for (int k = 0; k < 6; ++k) {
        if (!std::isfinite(farfield_bbox[k])) {
          set_err(nullptr, "pass=morton_octree code=INVALID_ARGUMENT detail=non_finite_farfield_bbox");
          return -1;
        }
      }
      if (farfield_bbox[0] >= farfield_bbox[3] ||
          farfield_bbox[1] >= farfield_bbox[4] ||
          farfield_bbox[2] >= farfield_bbox[5]) {
        set_err(nullptr, "pass=morton_octree code=INVALID_ARGUMENT detail=inverted_farfield_bbox");
        return -1;
      }
    }

    cfd::octree::LinearMortonForest forest;
    cfd::octree::MortonBuildStats stats{};
    int32_t rc = forest.build(
        surf_points, n_points, surf_faces, n_faces, h0, max_depth, farfield_bbox, &stats
    );
    if (rc != 0) {
      set_err(nullptr, "pass=morton_octree code=BUILD_ERROR detail=linear_morton_forest_build_failed");
      return rc;
    }

    if (out_n_leaves) *out_n_leaves = stats.n_leaves;
    if (out_n_cells) *out_n_cells = stats.n_cells;
    if (out_n_regular_faces) *out_n_regular_faces = stats.n_regular_faces;
    if (out_n_general_faces) *out_n_general_faces = stats.n_general_faces;
    if (out_n_vertices) *out_n_vertices = stats.n_vertices;
    if (out_peak_bytes) *out_peak_bytes = stats.peak_bytes;
    if (out_build_time_s) *out_build_time_s = stats.build_time_s;
    if (out_balanced_ok) *out_balanced_ok = stats.balanced_ok;
    return 0;
  } catch (...) {
    return -2;
  }
}

} // extern "C"

