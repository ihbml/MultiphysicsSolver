#include "difvm_mesh.hpp"
#include <map>
#include <unordered_map>
#include <set>
#include <cmath>
#include <stdexcept>
#include <iostream>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd {

namespace {

// Helper: Canonical representation of a face for matching
struct CanonicalFace {
    std::vector<int64_t> sorted_nodes;
    std::vector<int64_t> original_nodes;
    int64_t cell_owner = -1;

    CanonicalFace() = default;
    CanonicalFace(const std::vector<int64_t>& nodes, int64_t cell)
        : original_nodes(nodes), cell_owner(cell) {
        sorted_nodes = nodes;
        std::sort(sorted_nodes.begin(), sorted_nodes.end());
    }

    bool operator<(const CanonicalFace& o) const {
        return sorted_nodes < o.sorted_nodes;
    }
};

// Face area normal vector and centroid via triangle fan
std::pair<Vec3, Vec3> computePolygonNormalAndCentroid(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points
) {
    size_t n = face_nodes.size();
    if (n < 3) {
        if (n == 2) {
            Vec3 p0 = points[face_nodes[0]];
            Vec3 p1 = points[face_nodes[1]];
            Vec3 edge = p1 - p0;
            Vec3 normal(edge.y, -edge.x, 0.0);
            return {normal, (p0 + p1) * 0.5};
        }
        return {Vec3(0, 0, 0), Vec3(0, 0, 0)};
    }

    Vec3 p0 = points[face_nodes[0]];
    Vec3 normal_sum(0, 0, 0);
    Vec3 centroid_sum(0, 0, 0);
    double total_area = 0.0;

    for (size_t i = 1; i + 1 < n; ++i) {
        Vec3 pi = points[face_nodes[i]];
        Vec3 pj = points[face_nodes[i + 1]];
        Vec3 cross_p = cross(pi - p0, pj - p0);
        double area_tri = cross_p.norm() * 0.5;
        normal_sum += cross_p * 0.5;
        Vec3 tri_c = (p0 + pi + pj) / 3.0;
        centroid_sum += tri_c * area_tri;
        total_area += area_tri;
    }

    Vec3 centroid;
    if (total_area > 1e-15) {
        centroid = centroid_sum / total_area;
    } else {
        centroid = Vec3(0, 0, 0);
        for (auto idx : face_nodes) centroid += points[idx];
        centroid /= static_cast<double>(n);
    }

    return {normal_sum, centroid};
}

// Decompose a standard 3D cell into its oriented boundary faces
std::vector<std::vector<int64_t>> extractCellFaces(const std::vector<int64_t>& cell) {
    std::vector<std::vector<int64_t>> faces;
    size_t nv = cell.size();

    if (nv == 8) {
        // Hexahedron: 6 faces
        // Vertices: bottom: 0,1,2,3; top: 4,5,6,7
        faces.push_back({cell[0], cell[3], cell[2], cell[1]}); // bottom (-z)
        faces.push_back({cell[4], cell[5], cell[6], cell[7]}); // top (+z)
        faces.push_back({cell[0], cell[1], cell[5], cell[4]}); // front (-y)
        faces.push_back({cell[1], cell[2], cell[6], cell[5]}); // right (+x)
        faces.push_back({cell[2], cell[3], cell[7], cell[6]}); // back (+y)
        faces.push_back({cell[3], cell[0], cell[4], cell[7]}); // left (-x)
    } else if (nv == 4) {
        // Tetrahedron: 4 faces
        faces.push_back({cell[0], cell[2], cell[1]});
        faces.push_back({cell[0], cell[1], cell[3]});
        faces.push_back({cell[1], cell[2], cell[3]});
        faces.push_back({cell[2], cell[0], cell[3]});
    } else if (nv == 6) {
        // Wedge / Triangular prism: 5 faces
        faces.push_back({cell[0], cell[2], cell[1]});
        faces.push_back({cell[3], cell[4], cell[5]});
        faces.push_back({cell[0], cell[1], cell[4], cell[3]});
        faces.push_back({cell[1], cell[2], cell[5], cell[4]});
        faces.push_back({cell[2], cell[0], cell[3], cell[5]});
    } else if (nv == 5) {
        // Pyramid: 5 faces
        faces.push_back({cell[0], cell[3], cell[2], cell[1]});
        faces.push_back({cell[0], cell[1], cell[4]});
        faces.push_back({cell[1], cell[2], cell[4]});
        faces.push_back({cell[2], cell[3], cell[4]});
        faces.push_back({cell[3], cell[0], cell[4]});
    } else if (nv == 3) {
        // 2D Triangle: 3 edge faces
        faces.push_back({cell[0], cell[1]});
        faces.push_back({cell[1], cell[2]});
        faces.push_back({cell[2], cell[0]});
    } else if (nv == 4) {
        // 2D Quad: 4 edge faces
        faces.push_back({cell[0], cell[1]});
        faces.push_back({cell[1], cell[2]});
        faces.push_back({cell[2], cell[3]});
        faces.push_back({cell[3], cell[0]});
    } else {
        // General fallback: cycle of edges
        for (size_t i = 0; i < nv; ++i) {
            faces.push_back({cell[i], cell[(i + 1) % nv]});
        }
    }
    return faces;
}

} // anonymous namespace

void DiFVMTraversal::buildTopology(
    NativeMeshData& mesh,
    const std::vector<Vec3>& node_coords,
    const std::vector<std::vector<int64_t>>& cells_connectivity,
    const std::vector<int32_t>& regions
) {
    mesh.clear();
    int64_t n_nodes = static_cast<int64_t>(node_coords.size());
    int64_t n_cells = static_cast<int64_t>(cells_connectivity.size());

    // 1. Copy points
    mesh.points.resize(3 * n_nodes);
    for (int64_t i = 0; i < n_nodes; ++i) {
        mesh.points[3 * i + 0] = node_coords[i].x;
        mesh.points[3 * i + 1] = node_coords[i].y;
        mesh.points[3 * i + 2] = node_coords[i].z;
    }

    // 2. Setup cell regions & CSR connectivity
    mesh.cell_regions.resize(n_cells, 0);
    if (!regions.empty() && regions.size() == static_cast<size_t>(n_cells)) {
        mesh.cell_regions = regions;
    }

    mesh.cell_offsets.resize(n_cells + 1, 0);
    int64_t total_nodes = 0;
    for (const auto& cell : cells_connectivity) {
        total_nodes += static_cast<int64_t>(cell.size());
    }
    mesh.cell_nodes.reserve(total_nodes);

    for (int64_t c = 0; c < n_cells; ++c) {
        mesh.cell_offsets[c] = static_cast<int64_t>(mesh.cell_nodes.size());
        for (auto v : cells_connectivity[c]) {
            mesh.cell_nodes.push_back(v);
        }
    }
    mesh.cell_offsets[n_cells] = static_cast<int64_t>(mesh.cell_nodes.size());

    // 3. Extract unique faces
    struct TempFace {
        std::vector<int64_t> nodes;
        int64_t owner = -1;
        int64_t neighbour = -1;
        Vec3 normal_owner; // Outward normal from owner
        Vec3 centre;
    };

    std::map<CanonicalFace, int64_t> face_map;
    std::vector<TempFace> temp_faces;

    // cell -> faces (with sign)
    std::vector<std::vector<std::pair<int64_t, double>>> cell_face_refs(n_cells);

    for (int64_t c = 0; c < n_cells; ++c) {
        auto cfaces = extractCellFaces(cells_connectivity[c]);
        for (const auto& f_nodes : cfaces) {
            CanonicalFace cf(f_nodes, c);
            auto it = face_map.find(cf);
            if (it == face_map.end()) {
                // First time face is encountered -> owner is cell c
                auto [norm_v, cent_v] = computePolygonNormalAndCentroid(f_nodes, node_coords);
                TempFace tf;
                tf.nodes = f_nodes;
                tf.owner = c;
                tf.neighbour = -1;
                tf.normal_owner = norm_v;
                tf.centre = cent_v;

                int64_t f_idx = static_cast<int64_t>(temp_faces.size());
                temp_faces.push_back(tf);
                face_map[cf] = f_idx;

                cell_face_refs[c].push_back({f_idx, 1.0}); // +1 for owner
            } else {
                // Second time -> neighbour is cell c
                int64_t f_idx = it->second;
                temp_faces[f_idx].neighbour = c;
                cell_face_refs[c].push_back({f_idx, -1.0}); // -1 for neighbour
            }
        }
    }

    // 4. Compute preliminary cell centres and volumes
    mesh.cell_centres.resize(3 * n_cells, 0.0);
    mesh.cell_volumes.resize(n_cells, 0.0);

    for (int64_t c = 0; c < n_cells; ++c) {
        Vec3 c_approx(0, 0, 0);
        int64_t n_verts = 0;
        // Determine whether cell is 2D (all points planar z ~ 0) or 3D
        bool is_2d = true;
        for (auto v : cells_connectivity[c]) {
            c_approx += node_coords[v];
            if (std::abs(node_coords[v].z) > 1e-6) is_2d = false;
            n_verts++;
        }
        if (n_verts > 0) c_approx /= static_cast<double>(n_verts);

        double vol_sum = 0.0;
        Vec3 centroid_weighted(0, 0, 0);
        double div_factor = is_2d ? 2.0 : 3.0; // Green's theorem (2D) vs Gauss divergence (3D)

        for (const auto& ref : cell_face_refs[c]) {
            int64_t f_idx = ref.first;
            double sign = ref.second;
            const TempFace& tf = temp_faces[f_idx];
            Vec3 sf_out = tf.normal_owner * sign;
            double pyr_vol = dot(tf.centre - c_approx, sf_out) / div_factor;
            vol_sum += pyr_vol;
            Vec3 pyr_c = tf.centre * 0.75 + c_approx * 0.25;
            centroid_weighted += pyr_c * pyr_vol;
        }

        // Strict non-masked volume validation
        if (vol_sum > 1e-15) {
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = vol_sum;
        } else if (vol_sum < -1e-15) {
            // Retain genuine negative signed volume: flags inverted cell error in validate()
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = vol_sum;
        } else {
            // Degenerate / collapsed cell: zero volume (no artificial 1.0 m^3 override)
            mesh.cell_volumes[c] = 0.0;
            centroid_weighted = c_approx;
        }

        mesh.cell_centres[3 * c + 0] = centroid_weighted.x;
        mesh.cell_centres[3 * c + 1] = centroid_weighted.y;
        mesh.cell_centres[3 * c + 2] = centroid_weighted.z;
    }

    // 5. Separate internal faces and boundary faces
    // Order: internal faces first (sorted by owner), boundary faces last
    std::vector<int64_t> internal_indices;
    std::vector<int64_t> boundary_indices;

    for (size_t i = 0; i < temp_faces.size(); ++i) {
        if (temp_faces[i].neighbour >= 0) {
            internal_indices.push_back(static_cast<int64_t>(i));
        } else {
            boundary_indices.push_back(static_cast<int64_t>(i));
        }
    }

    // Sort internal faces by owner index
    std::sort(internal_indices.begin(), internal_indices.end(), [&](int64_t a, int64_t b) {
        if (temp_faces[a].owner != temp_faces[b].owner)
            return temp_faces[a].owner < temp_faces[b].owner;
        return temp_faces[a].neighbour < temp_faces[b].neighbour;
    });

    std::vector<int64_t> sorted_face_indices;
    sorted_face_indices.reserve(temp_faces.size());
    for (auto idx : internal_indices) sorted_face_indices.push_back(idx);
    for (auto idx : boundary_indices) sorted_face_indices.push_back(idx);

    int64_t n_faces = static_cast<int64_t>(sorted_face_indices.size());
    mesh.owner.resize(n_faces);
    mesh.neighbour.resize(n_faces);
    mesh.face_areas.resize(3 * n_faces);
    mesh.face_centres.resize(3 * n_faces);
    mesh.delta_f.resize(3 * n_faces);
    mesh.k_f.resize(3 * n_faces);
    mesh.non_ortho.resize(n_faces);
    mesh.skewness.resize(n_faces);
    mesh.stab_mask.resize(n_faces);

    // 6. Populate face data & calculate non-orthogonal decomposition and error-map
    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t orig_idx = sorted_face_indices[f];
        const TempFace& tf = temp_faces[orig_idx];

        int64_t o = tf.owner;
        int64_t nb = tf.neighbour;
        mesh.owner[f] = o;
        mesh.neighbour[f] = nb;

        Vec3 sf = tf.normal_owner;
        Vec3 cf = tf.centre;

        mesh.face_areas[3 * f + 0] = sf.x;
        mesh.face_areas[3 * f + 1] = sf.y;
        mesh.face_areas[3 * f + 2] = sf.z;

        mesh.face_centres[3 * f + 0] = cf.x;
        mesh.face_centres[3 * f + 1] = cf.y;
        mesh.face_centres[3 * f + 2] = cf.z;

        Vec3 c_o(mesh.cell_centres[3 * o + 0], mesh.cell_centres[3 * o + 1], mesh.cell_centres[3 * o + 2]);

        if (nb >= 0) {
            // Internal face
            Vec3 c_nb(mesh.cell_centres[3 * nb + 0], mesh.cell_centres[3 * nb + 1], mesh.cell_centres[3 * nb + 2]);
            Vec3 df = c_nb - c_o;
            double df_norm = df.norm();
            double sf_norm = sf.norm();

            double sf_dot_df = dot(sf, df);
            Vec3 delta;
            Vec3 k;

            if (std::abs(sf_dot_df) > 1e-15) {
                delta = df * (sf.norm2() / sf_dot_df);
                k = sf - delta;
            } else {
                delta = sf;
                k = Vec3(0, 0, 0);
            }

            mesh.delta_f[3 * f + 0] = delta.x;
            mesh.delta_f[3 * f + 1] = delta.y;
            mesh.delta_f[3 * f + 2] = delta.z;

            mesh.k_f[3 * f + 0] = k.x;
            mesh.k_f[3 * f + 1] = k.y;
            mesh.k_f[3 * f + 2] = k.z;

            // Non-orthogonality angle
            double cos_theta = 1.0;
            if (sf_norm > 1e-15 && df_norm > 1e-15) {
                cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), 0.0, 1.0);
            }
            double theta_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
            mesh.non_ortho[f] = theta_deg;

            // Skewness
            double skew = 0.0;
            if (sf_norm > 1e-15 && df_norm > 1e-15) {
                Vec3 nf = sf / sf_norm;
                double df_dot_nf = dot(df, nf);
                if (std::abs(df_dot_nf) > 1e-15) {
                    double t = dot(cf - c_o, nf) / df_dot_nf;
                    Vec3 x_int = c_o + df * t;
                    skew = (cf - x_int).norm() / df_norm;
                }
            }
            mesh.skewness[f] = skew;

            // Stabilization tier mask (0, 1, 2, 3)
            int32_t tier = 0;
            if (theta_deg >= 75.0 || skew >= 1.0) {
                tier = 3;
            } else if (theta_deg >= 65.0 || skew >= 0.7) {
                tier = 2;
            } else if (theta_deg >= 40.0 || skew >= 0.4) {
                tier = 1;
            }
            mesh.stab_mask[f] = tier;
        } else {
            // Boundary face
            Vec3 df = cf - c_o;
            mesh.delta_f[3 * f + 0] = df.x;
            mesh.delta_f[3 * f + 1] = df.y;
            mesh.delta_f[3 * f + 2] = df.z;

            mesh.k_f[3 * f + 0] = 0.0;
            mesh.k_f[3 * f + 1] = 0.0;
            mesh.k_f[3 * f + 2] = 0.0;

            mesh.non_ortho[f] = 0.0;
            mesh.skewness[f] = 0.0;
            mesh.stab_mask[f] = 0;
        }
    }

    // 7. Aspect ratio per cell
    mesh.aspect_ratio.resize(n_cells, 1.0);
    for (int64_t c = 0; c < n_cells; ++c) {
        double min_e = 1e30;
        double max_e = 0.0;
        int64_t off0 = mesh.cell_offsets[c];
        int64_t off1 = mesh.cell_offsets[c + 1];
        int64_t nv = off1 - off0;
        for (int64_t i = 0; i < nv; ++i) {
            for (int64_t j = i + 1; j < nv; ++j) {
                int64_t v1 = mesh.cell_nodes[off0 + i];
                int64_t v2 = mesh.cell_nodes[off0 + j];
                Vec3 p1(mesh.points[3 * v1], mesh.points[3 * v1 + 1], mesh.points[3 * v1 + 2]);
                Vec3 p2(mesh.points[3 * v2], mesh.points[3 * v2 + 1], mesh.points[3 * v2 + 2]);
                double d = (p1 - p2).norm();
                if (d > 1e-15) {
                    min_e = std::min(min_e, d);
                    max_e = std::max(max_e, d);
                }
            }
        }
        if (min_e < 1e20 && min_e > 1e-15) {
            mesh.aspect_ratio[c] = std::max(1.0, max_e / min_e);
        } else {
            mesh.aspect_ratio[c] = 1.0;
        }
    }

    // 8. Default boundary patch for boundary faces
    int64_t n_int = static_cast<int64_t>(internal_indices.size());
    int64_t n_bnd = static_cast<int64_t>(boundary_indices.size());
    if (n_bnd > 0) {
        BoundaryPatchC patch;
        snprintf(patch.name, sizeof(patch.name), "wall");
        snprintf(patch.patch_type, sizeof(patch.patch_type), "wall");
        patch.start_face = n_int;
        patch.num_faces = n_bnd;
        patch.region_id = 0;
        mesh.patches.push_back(patch);
    }
}

double DiFVMTraversal::computeMaxDivergenceClosure(const NativeMeshData& mesh) {
    int64_t n_cells = mesh.n_cells();
    int64_t n_faces = mesh.n_faces();
    if (n_cells == 0 || n_faces == 0) return 0.0;

    std::vector<Vec3> closure(n_cells, Vec3(0, 0, 0));

    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t o = mesh.owner[f];
        int64_t nb = mesh.neighbour[f];
        Vec3 sf(mesh.face_areas[3 * f + 0], mesh.face_areas[3 * f + 1], mesh.face_areas[3 * f + 2]);

        closure[o] += sf;
        if (nb >= 0) {
            closure[nb] -= sf;
        }
    }

    double max_err = 0.0;
    for (int64_t c = 0; c < n_cells; ++c) {
        max_err = std::max(max_err, closure[c].norm());
    }
    return max_err;
}

double DiFVMTraversal::computeMaxDecompositionError(const NativeMeshData& mesh) {
    int64_t n_faces = mesh.n_faces();
    double max_err = 0.0;

    for (int64_t f = 0; f < n_faces; ++f) {
        if (mesh.neighbour[f] >= 0) {
            Vec3 delta(mesh.delta_f[3 * f + 0], mesh.delta_f[3 * f + 1], mesh.delta_f[3 * f + 2]);
            Vec3 k(mesh.k_f[3 * f + 0], mesh.k_f[3 * f + 1], mesh.k_f[3 * f + 2]);
            Vec3 sf(mesh.face_areas[3 * f + 0], mesh.face_areas[3 * f + 1], mesh.face_areas[3 * f + 2]);

            Vec3 diff = (delta + k) - sf;
            max_err = std::max(max_err, diff.norm());
        }
    }
    return max_err;
}

} // namespace cfd
