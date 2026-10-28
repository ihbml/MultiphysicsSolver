#include "cfd_mesher_native.h"
#include "difvm_mesh.hpp"
#include "distmesh.hpp"
#include "inflation.hpp"
#include "rcm.hpp"
#include "cht_cyclic.hpp"
#include <cstring>
#include <vector>

extern "C" {

CFD_EXPORT CfdNativeMesh* cfd_mesher_create(void) {
    return new CfdNativeMesh();
}

CFD_EXPORT void cfd_mesher_free(CfdNativeMesh* mesh) {
    if (mesh) {
        delete mesh;
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
}

CFD_EXPORT int32_t cfd_mesher_generate_2d_polygon(
    CfdNativeMesh* mesh,
    const double* polygon_xy,
    int64_t n_vertices,
    const MeshingConfigC* config
) {
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
    if (!mesh || !points || !cell_offsets || !cell_nodes || n_points <= 0 || n_cells <= 0) {
        return -1;
    }

    std::vector<cfd::Vec3> node_coords(n_points);
    for (int64_t i = 0; i < n_points; ++i) {
        node_coords[i] = cfd::Vec3(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    }

    std::vector<std::vector<int64_t>> cells(n_cells);
    for (int64_t c = 0; c < n_cells; ++c) {
        int64_t off0 = cell_offsets[c];
        int64_t off1 = cell_offsets[c + 1];
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
}

CFD_EXPORT int64_t cfd_mesher_get_n_points(const CfdNativeMesh* mesh) {
    return mesh ? mesh->data.n_points() : 0;
}

CFD_EXPORT int64_t cfd_mesher_get_n_cells(const CfdNativeMesh* mesh) {
    return mesh ? mesh->data.n_cells() : 0;
}

CFD_EXPORT int64_t cfd_mesher_get_n_faces(const CfdNativeMesh* mesh) {
    return mesh ? mesh->data.n_faces() : 0;
}

CFD_EXPORT int64_t cfd_mesher_get_n_internal_faces(const CfdNativeMesh* mesh) {
    return mesh ? mesh->data.n_internal_faces() : 0;
}

CFD_EXPORT int64_t cfd_mesher_get_n_patches(const CfdNativeMesh* mesh) {
    return mesh ? static_cast<int64_t>(mesh->data.patches.size()) : 0;
}

CFD_EXPORT const double* cfd_mesher_get_points_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.points.empty()) ? mesh->data.points.data() : nullptr;
}

CFD_EXPORT const int64_t* cfd_mesher_get_owner_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.owner.empty()) ? mesh->data.owner.data() : nullptr;
}

CFD_EXPORT const int64_t* cfd_mesher_get_neighbour_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.neighbour.empty()) ? mesh->data.neighbour.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_face_areas_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.face_areas.empty()) ? mesh->data.face_areas.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_face_centres_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.face_centres.empty()) ? mesh->data.face_centres.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_cell_centres_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.cell_centres.empty()) ? mesh->data.cell_centres.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_cell_volumes_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.cell_volumes.empty()) ? mesh->data.cell_volumes.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_delta_f_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.delta_f.empty()) ? mesh->data.delta_f.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_k_f_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.k_f.empty()) ? mesh->data.k_f.data() : nullptr;
}

CFD_EXPORT const int64_t* cfd_mesher_get_cell_offsets_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.cell_offsets.empty()) ? mesh->data.cell_offsets.data() : nullptr;
}

CFD_EXPORT const int64_t* cfd_mesher_get_cell_nodes_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.cell_nodes.empty()) ? mesh->data.cell_nodes.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_non_orthogonality_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.non_ortho.empty()) ? mesh->data.non_ortho.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_skewness_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.skewness.empty()) ? mesh->data.skewness.data() : nullptr;
}

CFD_EXPORT const double* cfd_mesher_get_aspect_ratio_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.aspect_ratio.empty()) ? mesh->data.aspect_ratio.data() : nullptr;
}

CFD_EXPORT const int32_t* cfd_mesher_get_stabilization_mask_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.stab_mask.empty()) ? mesh->data.stab_mask.data() : nullptr;
}

CFD_EXPORT const int32_t* cfd_mesher_get_cell_regions_ptr(const CfdNativeMesh* mesh) {
    return (mesh && !mesh->data.cell_regions.empty()) ? mesh->data.cell_regions.data() : nullptr;
}

CFD_EXPORT int32_t cfd_mesher_get_boundary_patch(
    const CfdNativeMesh* mesh,
    int64_t patch_idx,
    BoundaryPatchC* out_patch
) {
    if (!mesh || !out_patch || patch_idx < 0 || patch_idx >= static_cast<int64_t>(mesh->data.patches.size())) {
        return -1;
    }
    *out_patch = mesh->data.patches[patch_idx];
    return 0;
}

CFD_EXPORT int64_t cfd_mesher_extract_cyclic_pairs(
    CfdNativeMesh* mesh,
    double sector_angle_rad,
    double tolerance,
    int64_t* out_pair_f1,
    int64_t* out_pair_f2,
    int64_t max_pairs
) {
    if (!mesh || !out_pair_f1 || !out_pair_f2 || max_pairs <= 0) return 0;
    auto pairs = cfd::CHTCyclicProcessor::extractCyclicPairs(mesh->data, sector_angle_rad, tolerance);
    int64_t count = std::min(static_cast<int64_t>(pairs.size()), max_pairs);
    for (int64_t i = 0; i < count; ++i) {
        out_pair_f1[i] = pairs[i].first;
        out_pair_f2[i] = pairs[i].second;
    }
    return count;
}

CFD_EXPORT double cfd_mesher_compute_max_divergence_closure(const CfdNativeMesh* mesh) {
    return mesh ? cfd::DiFVMTraversal::computeMaxDivergenceClosure(mesh->data) : 0.0;
}

CFD_EXPORT double cfd_mesher_compute_max_decomposition_error(const CfdNativeMesh* mesh) {
    return mesh ? cfd::DiFVMTraversal::computeMaxDecompositionError(mesh->data) : 0.0;
}

} // extern "C"
