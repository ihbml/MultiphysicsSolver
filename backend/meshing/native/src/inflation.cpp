#include "inflation.hpp"
#include <cmath>
#include <map>

namespace cfd {

std::vector<double> BoundaryInflation::computeLayerThicknesses(const InflationOptions& options) {
    std::vector<double> thicknesses(options.num_layers);
    double h = options.first_layer_height;
    for (int32_t k = 0; k < options.num_layers; ++k) {
        thicknesses[k] = h;
        h *= options.growth_rate;
    }
    return thicknesses;
}

void BoundaryInflation::generateLayers(
    NativeMeshData& mesh,
    const std::vector<Vec3>& wall_nodes,
    const std::vector<std::vector<int64_t>>& wall_faces,
    const Vec3& nominal_normal,
    const InflationOptions& options
) {
    int64_t n_base_nodes = static_cast<int64_t>(wall_nodes.size());
    int32_t n_layers = options.num_layers;
    if (n_base_nodes == 0 || wall_faces.empty() || n_layers <= 0) return;

    // 1. Compute layer thicknesses
    std::vector<double> thicknesses = computeLayerThicknesses(options);

    // 2. Compute vertex normals and corner dihedral angles
    std::vector<Vec3> vert_normals(n_base_nodes, Vec3(0, 0, 0));
    std::vector<double> corner_max_angle(n_base_nodes, 0.0);
    std::vector<std::vector<Vec3>> vert_face_normals(n_base_nodes);

    for (const auto& f : wall_faces) {
        if (f.size() < 3) continue;
        Vec3 p0 = wall_nodes[f[0]];
        Vec3 p1 = wall_nodes[f[1]];
        Vec3 p2 = wall_nodes[f[2]];
        Vec3 fn = cross(p1 - p0, p2 - p0).normalized();
        if (dot(fn, nominal_normal) < 0.0) {
            fn = fn * (-1.0);
        }
        for (auto v : f) {
            vert_normals[v] += fn;
            vert_face_normals[v].push_back(fn);
        }
    }

    for (int64_t i = 0; i < n_base_nodes; ++i) {
        if (vert_normals[i].norm2() > 1e-12) {
            vert_normals[i] = vert_normals[i].normalized();
        } else {
            vert_normals[i] = nominal_normal.normalized();
        }

        // Compute maximum dihedral turning angle between sharing faces
        const auto& fns = vert_face_normals[i];
        double max_angle_deg = 0.0;
        for (size_t a = 0; a < fns.size(); ++a) {
            for (size_t b = a + 1; b < fns.size(); ++b) {
                double c = std::clamp(dot(fns[a], fns[b]), -1.0, 1.0);
                double deg = std::acos(c) * 180.0 / 3.14159265358979323846;
                if (deg > max_angle_deg) max_angle_deg = deg;
            }
        }
        corner_max_angle[i] = max_angle_deg;
    }

    // 3. Pinching factor per vertex (Feature 23)
    std::vector<double> pinch_factor(n_base_nodes, 1.0);
    if (options.pinching_enabled) {
        for (int64_t i = 0; i < n_base_nodes; ++i) {
            double angle = corner_max_angle[i];
            double rad = angle * 3.14159265358979323846 / 180.0;
            if (angle > options.pinching_angle_deg) {
                double factor = std::max(0.1, 1.0 - (rad / 3.14159265358979323846) * (rad / 3.14159265358979323846));
                pinch_factor[i] = factor;
            }
        }
    }

    // 4. Generate points for all layers: (n_layers + 1) * n_base_nodes
    int64_t total_nodes = (n_layers + 1) * n_base_nodes;
    std::vector<Vec3> all_points(total_nodes);

    for (int64_t i = 0; i < n_base_nodes; ++i) {
        all_points[i] = wall_nodes[i];
    }

    double current_dist = 0.0;
    for (int32_t k = 0; k < n_layers; ++k) {
        current_dist += thicknesses[k];
        int64_t layer_offset = (k + 1) * n_base_nodes;
        for (int64_t i = 0; i < n_base_nodes; ++i) {
            all_points[layer_offset + i] = wall_nodes[i] + vert_normals[i] * (current_dist * pinch_factor[i]);
        }
    }

    // 5. Generate cells
    std::vector<std::vector<int64_t>> all_cells;
    for (int32_t k = 0; k < n_layers; ++k) {
        int64_t off_bot = k * n_base_nodes;
        int64_t off_top = (k + 1) * n_base_nodes;

        for (const auto& f : wall_faces) {
            if (f.size() == 4) {
                // Extrude quad into 8-node hex
                // Bottom quad: off_bot + f[0], f[1], f[2], f[3]
                // Top quad:    off_top + f[0], f[1], f[2], f[3]
                all_cells.push_back({
                    off_bot + f[0],
                    off_bot + f[1],
                    off_bot + f[2],
                    off_bot + f[3],
                    off_top + f[0],
                    off_top + f[1],
                    off_top + f[2],
                    off_top + f[3]
                });
            } else if (f.size() == 3) {
                // Extrude triangle into 6-node wedge
                all_cells.push_back({
                    off_bot + f[0],
                    off_bot + f[1],
                    off_bot + f[2],
                    off_top + f[0],
                    off_top + f[1],
                    off_top + f[2]
                });
            }
        }
    }

    // 6. Build topology
    DiFVMTraversal::buildTopology(mesh, all_points, all_cells);
}

} // namespace cfd
