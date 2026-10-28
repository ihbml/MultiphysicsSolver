#include "cht_cyclic.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

Vec3 CHTCyclicProcessor::rotateZ(const Vec3& v, double angle_rad) {
    double c = std::cos(angle_rad);
    double s = std::sin(angle_rad);
    return Vec3(
        v.x * c - v.y * s,
        v.x * s + v.y * c,
        v.z
    );
}

void CHTCyclicProcessor::extractCoupledInterfaces(NativeMeshData& mesh) {
    int64_t n_faces = mesh.n_faces();
    if (mesh.cell_regions.empty()) return;

    std::vector<int64_t> coupled_faces;
    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t o = mesh.owner[f];
        int64_t nb = mesh.neighbour[f];
        if (nb >= 0 && mesh.cell_regions[o] != mesh.cell_regions[nb]) {
            coupled_faces.push_back(f);
        }
    }

    if (!coupled_faces.empty()) {
        BoundaryPatchC patch_fluid;
        snprintf(patch_fluid.name, sizeof(patch_fluid.name), "coupledWall_fluid");
        snprintf(patch_fluid.patch_type, sizeof(patch_fluid.patch_type), "coupledWall");
        patch_fluid.start_face = coupled_faces.front();
        patch_fluid.num_faces = static_cast<int64_t>(coupled_faces.size());
        patch_fluid.region_id = 0;
        mesh.patches.push_back(patch_fluid);

        BoundaryPatchC patch_solid;
        snprintf(patch_solid.name, sizeof(patch_solid.name), "coupledWall_solid");
        snprintf(patch_solid.patch_type, sizeof(patch_solid.patch_type), "coupledWall");
        patch_solid.start_face = coupled_faces.front();
        patch_solid.num_faces = static_cast<int64_t>(coupled_faces.size());
        patch_solid.region_id = 1;
        mesh.patches.push_back(patch_solid);
    }
}

std::vector<std::pair<int64_t, int64_t>> CHTCyclicProcessor::extractCyclicPairs(
    NativeMeshData& mesh,
    double sector_angle_rad,
    double tolerance
) {
    std::vector<std::pair<int64_t, int64_t>> pairs;
    int64_t n_faces = mesh.n_faces();

    // Identify boundary faces
    std::vector<int64_t> bnd_faces;
    for (int64_t f = 0; f < n_faces; ++f) {
        if (mesh.neighbour[f] < 0) {
            bnd_faces.push_back(f);
        }
    }

    // Pair boundary faces where R_z(theta) * c_f1 == c_f2
    for (size_t i = 0; i < bnd_faces.size(); ++i) {
        int64_t f1 = bnd_faces[i];
        Vec3 c1(mesh.face_centres[3 * f1], mesh.face_centres[3 * f1 + 1], mesh.face_centres[3 * f1 + 2]);
        Vec3 rot_c1 = rotateZ(c1, sector_angle_rad);

        for (size_t j = 0; j < bnd_faces.size(); ++j) {
            if (i == j) continue;
            int64_t f2 = bnd_faces[j];
            Vec3 c2(mesh.face_centres[3 * f2], mesh.face_centres[3 * f2 + 1], mesh.face_centres[3 * f2 + 2]);

            if ((rot_c1 - c2).norm() < tolerance) {
                pairs.push_back({f1, f2});
                break;
            }
        }
    }

    return pairs;
}

} // namespace cfd
