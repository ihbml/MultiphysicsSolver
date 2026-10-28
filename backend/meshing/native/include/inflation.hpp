#pragma once

#include <vector>
#include <cstdint>
#include "difvm_mesh.hpp"

namespace cfd {

struct InflationOptions {
    int32_t num_layers = 5;
    double first_layer_height = 0.001;
    double growth_rate = 1.2;
    int32_t pinching_enabled = 1;
    double pinching_angle_deg = 60.0;
};

class BoundaryInflation {
public:
    static void generateLayers(
        NativeMeshData& mesh,
        const std::vector<Vec3>& wall_nodes,
        const std::vector<std::vector<int64_t>>& wall_faces,
        const Vec3& nominal_normal,
        const InflationOptions& options
    );

    static std::vector<double> computeLayerThicknesses(const InflationOptions& options);
};

} // namespace cfd
