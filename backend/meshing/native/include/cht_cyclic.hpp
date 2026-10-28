#pragma once

#include <vector>
#include <string>
#include <utility>
#include "difvm_mesh.hpp"

namespace cfd {

class CHTCyclicProcessor {
public:
    // Extract CHT coupled twin-face interfaces (coupledWall / mappedWall)
    static void extractCoupledInterfaces(NativeMeshData& mesh);

    // Identify and link cyclic rotational periodic pairs
    static std::vector<std::pair<int64_t, int64_t>> extractCyclicPairs(
        NativeMeshData& mesh,
        double sector_angle_rad,
        double tolerance = 1e-5
    );

    // Apply 3D Z-rotation matrix to vector
    static Vec3 rotateZ(const Vec3& v, double angle_rad);
};

} // namespace cfd
