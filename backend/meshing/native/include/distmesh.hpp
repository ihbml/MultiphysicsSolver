#pragma once

#include <vector>
#include <functional>
#include "difvm_mesh.hpp"
#include "cfd_mesher_native.h"

namespace cfd {

struct DistMeshOptions {
    double h0 = 0.05;
    double h_min = 0.005;
    double h_max = 0.2;
    int32_t max_iters = 45;
    double dptol = 0.001;
    double delta_t = 0.22;
    double f_scale = 1.2;
};

class DistMeshEngine {
public:
    static void generateFromSurface(
        NativeMeshData& mesh,
        const double* surf_points,
        int64_t n_points,
        const int32_t* surf_triangles,
        int64_t n_triangles,
        const DistMeshOptions& options,
        const MeshingConfigC* config = nullptr
    );

    static void generate2DPolygon(
        NativeMeshData& mesh,
        const double* polygon_xy,
        int64_t n_vertices,
        const DistMeshOptions& options,
        const MeshingConfigC* config = nullptr
    );
};

} // namespace cfd
