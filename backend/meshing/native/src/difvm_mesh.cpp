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

bool isFaceFolded(const std::vector<int64_t>& face_nodes, const std::vector<Vec3>& points, const Vec3& normal_sum) {
    size_t n = face_nodes.size();
    if (n < 3) return false;
    double norm_mag = normal_sum.norm();
    if (norm_mag <= 1e-15) return false;
    const Vec3& p0 = points[face_nodes[0]];
    for (size_t i = 1; i + 1 < n; ++i) {
        const Vec3& pi = points[face_nodes[i]];
        const Vec3& pj = points[face_nodes[i + 1]];
        Vec3 cross_p = cross(pi - p0, pj - p0);
        if (cross_p.norm() > 1e-15) {
            if (dot(cross_p, normal_sum) <= 0.0) return true;
        }
    }
    return false;
}

// Decompose a standard 2D or 3D cell into its oriented boundary faces
std::vector<std::vector<int64_t>> extractCellFaces(
    const std::vector<int64_t>& cell,
    const std::vector<Vec3>& node_coords,
    CellType& out_type,
    bool* out_is_inverted = nullptr
) {
  std::vector<std::vector<int64_t>> faces;
  size_t nv = cell.size();

  if (out_type != CellType::POLYHEDRON && nv == 8) {
    Vec3 p_min = node_coords[cell[0]];
    Vec3 p_max = node_coords[cell[0]];
    for (size_t i = 1; i < nv; ++i) {
      p_min.x = std::min(p_min.x, node_coords[cell[i]].x);
      p_min.y = std::min(p_min.y, node_coords[cell[i]].y);
      p_min.z = std::min(p_min.z, node_coords[cell[i]].z);
      p_max.x = std::max(p_max.x, node_coords[cell[i]].x);
      p_max.y = std::max(p_max.y, node_coords[cell[i]].y);
      p_max.z = std::max(p_max.z, node_coords[cell[i]].z);
    }
    Vec3 span = p_max - p_min;
    double min_span = std::min({span.x, span.y, span.z});
    double max_span = std::max({span.x, span.y, span.z});
    if (max_span > 1e-12 && (min_span / max_span) < 1e-5) {
      out_type = CellType::POLYGON;
      for (size_t i = 0; i < nv; ++i) {
        faces.push_back({cell[i], cell[(i + 1) % nv]});
      }
    } else {
      out_type = CellType::HEXAHEDRON;
      // Hexahedron: 6 faces
      // Vertices: bottom: 0,1,2,3; top: 4,5,6,7
      faces.push_back({cell[0], cell[3], cell[2], cell[1]}); // bottom (-z)
      faces.push_back({cell[4], cell[5], cell[6], cell[7]}); // top (+z)
      faces.push_back({cell[0], cell[1], cell[5], cell[4]}); // front (-y)
      faces.push_back({cell[1], cell[2], cell[6], cell[5]}); // right (+x)
      faces.push_back({cell[2], cell[3], cell[7], cell[6]}); // back (+y)
      faces.push_back({cell[3], cell[0], cell[4], cell[7]}); // left (-x)
    }
  } else if (out_type != CellType::POLYHEDRON && nv == 4) {
    // Discriminate between 2D Quad and 3D Tetrahedron via coplanarity check
    Vec3 p0 = node_coords[cell[0]];
    Vec3 p1 = node_coords[cell[1]];
    Vec3 p2 = node_coords[cell[2]];
    Vec3 p3 = node_coords[cell[3]];
    Vec3 e1 = p1 - p0;
    Vec3 e2 = p2 - p0;
    Vec3 e3 = p3 - p0;
    double V_box = std::abs(dot(cross(e1, e2), e3));
    double L = std::max({e1.norm(), e2.norm(), e3.norm()});
    if (L > 1e-15 && (V_box / (L * L * L)) < 1e-6) {
      // 2D Quad (CellType::QUAD = 9) - 4 edge faces
      out_type = CellType::QUAD;
      faces.push_back({cell[0], cell[1]});
      faces.push_back({cell[1], cell[2]});
      faces.push_back({cell[2], cell[3]});
      faces.push_back({cell[3], cell[0]});
    } else {
      // 3D Tetrahedron (CellType::TETRA = 10) - 4 triangular faces
      out_type = CellType::TETRA;
      faces.push_back({cell[0], cell[2], cell[1]});
      faces.push_back({cell[0], cell[1], cell[3]});
      faces.push_back({cell[1], cell[2], cell[3]});
      faces.push_back({cell[2], cell[0], cell[3]});
    }
  } else if (out_type != CellType::POLYHEDRON && nv == 6) {
    out_type = CellType::WEDGE;
    // Wedge / Triangular prism: 5 faces
    faces.push_back({cell[0], cell[2], cell[1]});
    faces.push_back({cell[3], cell[4], cell[5]});
    faces.push_back({cell[0], cell[1], cell[4], cell[3]});
    faces.push_back({cell[1], cell[2], cell[5], cell[4]});
    faces.push_back({cell[2], cell[0], cell[3], cell[5]});
  } else if (out_type != CellType::POLYHEDRON && nv == 5) {
    out_type = CellType::PYRAMID;
    // Pyramid: 5 faces
    faces.push_back({cell[0], cell[3], cell[2], cell[1]});
    faces.push_back({cell[0], cell[1], cell[4]});
    faces.push_back({cell[1], cell[2], cell[4]});
    faces.push_back({cell[2], cell[3], cell[4]});
    faces.push_back({cell[3], cell[0], cell[4]});
  } else if (out_type != CellType::POLYHEDRON && nv == 3) {
    out_type = CellType::TRIANGLE;
    // 2D Triangle: 3 edge faces
    faces.push_back({cell[0], cell[1]});
    faces.push_back({cell[1], cell[2]});
    faces.push_back({cell[2], cell[0]});
  } else {
    if (nv < 3) {
      throw std::invalid_argument("extractCellFaces: cell must contain at least 3 vertices");
    }
    // Check if cell vertices are coplanar (2D polygon) or span a 3D volume (3D Polyhedron)
    Vec3 p0 = node_coords[cell[0]];
    Vec3 cell_c_est(0, 0, 0);
    double l_scale = 1e-12;
    for (int64_t v : cell) {
      cell_c_est += node_coords[v];
    }
    cell_c_est /= static_cast<double>(nv);
    for (int64_t v : cell) {
      l_scale = std::max(l_scale, (node_coords[v] - cell_c_est).norm());
    }

    bool is_coplanar = true;
    if (out_type == CellType::POLYHEDRON) {
      is_coplanar = false;
    } else if (nv >= 4) {
      // Find a non-collinear triplet to define plane
      Vec3 plane_n(0, 0, 0);
      for (size_t i = 1; i < nv && plane_n.norm() < 1e-6 * l_scale; ++i) {
        for (size_t j = i + 1; j < nv && plane_n.norm() < 1e-6 * l_scale; ++j) {
          plane_n = cross(node_coords[cell[i]] - p0, node_coords[cell[j]] - p0);
        }
      }
      if (plane_n.norm() > 1e-9 * l_scale * l_scale) {
        plane_n = plane_n.normalized();
        for (size_t i = 0; i < nv; ++i) {
          double dist = std::abs(dot(node_coords[cell[i]] - p0, plane_n));
          if (dist > 1e-4 * l_scale) {
            is_coplanar = false;
            break;
          }
        }
      }
    }

    if (is_coplanar) {
      // 2D Polygon: cycle of boundary edges
      out_type = CellType::POLYGON;
      for (size_t i = 0; i < nv; ++i) {
        faces.push_back({cell[i], cell[(i + 1) % nv]});
      }
    } else {
      // 3D Polyhedron (CellType::POLYHEDRON = 42)
      out_type = CellType::POLYHEDRON;

      // Extract supporting planar polygonal faces of the 3D cell
      std::set<std::vector<int64_t>> unique_faces;
      for (size_t i = 0; i < nv; ++i) {
        for (size_t j = i + 1; j < nv; ++j) {
          for (size_t k = j + 1; k < nv; ++k) {
            Vec3 pi = node_coords[cell[i]];
            Vec3 pj = node_coords[cell[j]];
            Vec3 pk = node_coords[cell[k]];
            Vec3 fn = cross(pj - pi, pk - pi);
            if (fn.norm() < 1e-9 * l_scale * l_scale) continue;
            fn = fn.normalized();
            if (dot(fn, pi - cell_c_est) < 0.0) fn = -fn;

            // Check if all vertices lie on or behind this supporting plane
            bool is_supporting = true;
            std::vector<int64_t> on_plane;
            for (size_t m = 0; m < nv; ++m) {
              double signed_d = dot(fn, node_coords[cell[m]] - pi);
              if (signed_d > 1e-4 * l_scale) {
                is_supporting = false;
                break;
              }
              if (std::abs(signed_d) <= 1e-4 * l_scale) {
                on_plane.push_back(cell[m]);
              }
            }

            if (is_supporting && on_plane.size() >= 3) {
              // Sort vertices in CCW cycle around fn
              Vec3 fc(0, 0, 0);
              for (int64_t v : on_plane) fc += node_coords[v];
              fc /= static_cast<double>(on_plane.size());

              Vec3 u = (node_coords[on_plane[0]] - fc).normalized();
              Vec3 v_ax = cross(fn, u);

              std::sort(on_plane.begin(), on_plane.end(), [&](int64_t a, int64_t b) {
                Vec3 da = node_coords[a] - fc;
                Vec3 db = node_coords[b] - fc;
                double ang_a = std::atan2(dot(da, v_ax), dot(da, u));
                double ang_b = std::atan2(dot(db, v_ax), dot(db, u));
                return ang_a < ang_b;
              });

              std::vector<int64_t> sorted_key = on_plane;
              std::sort(sorted_key.begin(), sorted_key.end());
              if (unique_faces.find(sorted_key) == unique_faces.end()) {
                unique_faces.insert(sorted_key);
                faces.push_back(on_plane);
              }
            }
          }
        }
      }

      if (faces.empty()) {
        // Fallback: create polygonal boundary loops
        for (size_t i = 0; i < nv; ++i) {
          faces.push_back({cell[i], cell[(i + 1) % nv]});
        }
      }
    }
  }

  // Ensure all extracted face normals point strictly outward from cell centroid
  Vec3 cell_c(0, 0, 0);
  for (int64_t v : cell) {
    cell_c = cell_c + node_coords[v];
  }
  if (!cell.empty()) {
    cell_c = cell_c * (1.0 / static_cast<double>(cell.size()));
  }

  bool is_2d = (out_type == CellType::TRIANGLE || out_type == CellType::QUAD || out_type == CellType::POLYGON);
  double div_factor = is_2d ? 2.0 : 3.0;
  double natural_vol = 0.0;

  for (auto& f : faces) {
    auto [fn, fc] = computePolygonNormalAndCentroid(f, node_coords);
    natural_vol += dot(fc - cell_c, fn) / div_factor;
    if (dot(fn, fc - cell_c) < 0.0) {
      std::reverse(f.begin(), f.end());
    }
  }

  if (out_is_inverted) {
    *out_is_inverted = (natural_vol < -1e-15);
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

    // 3. Extract unique faces and classify cell types
    mesh.cell_types.resize(n_cells);
    bool all_2d = true;

    struct TempFace {
        std::vector<int64_t> nodes;
        int64_t owner = -1;
        int64_t neighbour = -1;
        Vec3 normal_owner; // Outward normal from owner
        Vec3 centre;
        bool is_folded = false;
    };

    std::map<CanonicalFace, int64_t> face_map;
    std::vector<TempFace> temp_faces;

    // cell -> faces (with sign)
    std::vector<std::vector<std::pair<int64_t, double>>> cell_face_refs(n_cells);
    std::vector<bool> cell_is_inverted(n_cells, false);

    for (int64_t c = 0; c < n_cells; ++c) {
        CellType c_type = CellType::HEXAHEDRON;
        bool inv = false;
        auto cfaces = extractCellFaces(cells_connectivity[c], node_coords, c_type, &inv);
        cell_is_inverted[c] = inv;
        mesh.cell_types[c] = static_cast<int32_t>(c_type);
        if (c_type != CellType::TRIANGLE && c_type != CellType::QUAD && c_type != CellType::POLYGON) {
            all_2d = false;
        }

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
                tf.is_folded = isFaceFolded(f_nodes, node_coords, norm_v);

                int64_t f_idx = static_cast<int64_t>(temp_faces.size());
                temp_faces.push_back(tf);
                face_map[cf] = f_idx;

                cell_face_refs[c].push_back({f_idx, 1.0}); // +1 for owner
            } else {
                int64_t f_idx = it->second;
                // Strict manifoldness check: if face already has a neighbour, >2 cells share it!
                if (temp_faces[f_idx].neighbour != -1) {
                    throw std::runtime_error("Non-manifold mesh detected: face shared by >2 cells");
                }

                // Enforce upper-triangular ordering: owner < neighbour
                if (c < temp_faces[f_idx].owner) {
                    int64_t old_owner = temp_faces[f_idx].owner;
                    temp_faces[f_idx].owner = c;
                    temp_faces[f_idx].neighbour = old_owner;
                    temp_faces[f_idx].normal_owner = -temp_faces[f_idx].normal_owner;
                    std::reverse(temp_faces[f_idx].nodes.begin(), temp_faces[f_idx].nodes.end());

                    // Flip sign for the previous owner
                    for (auto& ref : cell_face_refs[old_owner]) {
                        if (ref.first == f_idx) {
                            ref.second = -1.0;
                            break;
                        }
                    }
                    cell_face_refs[c].push_back({f_idx, 1.0});
                } else {
                    temp_faces[f_idx].neighbour = c;
                    cell_face_refs[c].push_back({f_idx, -1.0}); // -1 for neighbour
                }
            }
        }
    }

    mesh.topological_dim = all_2d ? 2 : 3;

    // 4. Compute preliminary cell centres and volumes
    mesh.cell_centres.resize(3 * n_cells, 0.0);
    mesh.cell_volumes.resize(n_cells, 0.0);

    for (int64_t c = 0; c < n_cells; ++c) {
        Vec3 c_approx(0, 0, 0);
        int64_t n_verts = 0;
        for (auto v : cells_connectivity[c]) {
            c_approx += node_coords[v];
            n_verts++;
        }
        if (n_verts > 0) c_approx /= static_cast<double>(n_verts);

        bool is_2d_cell = (mesh.cell_types[c] == static_cast<int32_t>(CellType::QUAD) ||
                           mesh.cell_types[c] == static_cast<int32_t>(CellType::TRIANGLE) ||
                           mesh.cell_types[c] == static_cast<int32_t>(CellType::POLYGON));
        double div_factor = is_2d_cell ? 2.0 : 3.0; // Green's theorem (2D) vs Gauss divergence (3D)

        double vol_sum = 0.0;
        Vec3 centroid_weighted(0, 0, 0);

        for (const auto& ref : cell_face_refs[c]) {
            int64_t f_idx = ref.first;
            double sign = ref.second;
            const TempFace& tf = temp_faces[f_idx];
            Vec3 sf_out = tf.normal_owner * sign;
            double pyr_vol = dot(tf.centre - c_approx, sf_out) / div_factor;
            vol_sum += pyr_vol;
            Vec3 pyr_c = is_2d_cell ? (tf.centre * (2.0 / 3.0) + c_approx * (1.0 / 3.0))
                                    : (tf.centre * 0.75 + c_approx * 0.25);
            centroid_weighted += pyr_c * pyr_vol;
        }

        // Strict non-masked volume validation
        if (cell_is_inverted[c] && vol_sum > 1e-60) {
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = -vol_sum;
        } else if (vol_sum > 1e-60) {
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = vol_sum;
        } else if (vol_sum < -1e-60) {
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

    // CSR face-to-node connectivity
    mesh.face_offsets.resize(n_faces + 1, 0);
    int64_t total_face_nodes = 0;
    for (int64_t f = 0; f < n_faces; ++f) {
        total_face_nodes += static_cast<int64_t>(temp_faces[sorted_face_indices[f]].nodes.size());
    }
    mesh.face_nodes.reserve(total_face_nodes);
    for (int64_t f = 0; f < n_faces; ++f) {
        mesh.face_offsets[f] = static_cast<int64_t>(mesh.face_nodes.size());
        for (auto v : temp_faces[sorted_face_indices[f]].nodes) {
            mesh.face_nodes.push_back(v);
        }
    }
    mesh.face_offsets[n_faces] = static_cast<int64_t>(mesh.face_nodes.size());

    // CMS Error Map arrays
    mesh.m_skew.assign(3 * n_faces, 0.0);
    mesh.k_f_comp.assign(3 * n_faces, 0.0);
    mesh.beta_upwind.assign(n_faces, 1.0);
    mesh.omega_cell.assign(n_cells, 1.0);
    mesh.wsrd_alpha.assign(n_cells, 1.0);

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

            // Strict Non-orthogonality angle clamped in [-1.0, 1.0]
            double cos_theta = 1.0;
            if (sf_norm > 1e-15 && df_norm > 1e-15) {
                cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0);
            }
            double theta_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
            mesh.non_ortho[f] = theta_deg;

            // Skewness and m_skew displacement vector
            double skew = 0.0;
            Vec3 x_int = cf;
            if (sf_norm > 1e-15 && df_norm > 1e-15) {
                Vec3 nf = sf / sf_norm;
                double df_dot_nf = dot(df, nf);
                if (std::abs(df_dot_nf) > 1e-15) {
                    double t = dot(cf - c_o, nf) / df_dot_nf;
                    x_int = c_o + df * t;
                    skew = (cf - x_int).norm() / df_norm;
                }
            }
            mesh.skewness[f] = skew;
            Vec3 m_vec = cf - x_int;
            mesh.m_skew[3 * f + 0] = m_vec.x;
            mesh.m_skew[3 * f + 1] = m_vec.y;
            mesh.m_skew[3 * f + 2] = m_vec.z;

            // Damped non-orthogonal correction k_f_comp (alpha_max = 1.5)
            double k_norm = k.norm();
            double factor = std::min(1.0, (1.5 * delta.norm()) / (k_norm + 1e-15));
            Vec3 k_comp = k * factor;
            mesh.k_f_comp[3 * f + 0] = k_comp.x;
            mesh.k_f_comp[3 * f + 1] = k_comp.y;
            mesh.k_f_comp[3 * f + 2] = k_comp.z;

            // Upwind factor beta_upwind and stabilization tier mask
            int32_t tier = 0;
            if (tf.is_folded || sf_dot_df <= 0.0) {
                // Inverted or folded face: immediate escalation to Tier 3 and 100% upwind
                tier = 3;
                mesh.beta_upwind[f] = 0.0;
            } else {
                double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
                double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
                mesh.beta_upwind[f] = std::min(beta_ortho, beta_skew);

                if (theta_deg >= 75.0 || skew >= 1.0) {
                    tier = 3;
                } else if (theta_deg >= 65.0 || skew >= 0.7) {
                    tier = 2;
                } else if (theta_deg >= 40.0 || skew >= 0.4) {
                    tier = 1;
                }
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
            if (tf.is_folded || dot(sf, df) <= 0.0) {
                mesh.stab_mask[f] = 3;
                mesh.beta_upwind[f] = 0.0;
            } else {
                mesh.stab_mask[f] = 0;
                mesh.beta_upwind[f] = 1.0;
            }
        }
    }

    // 7. Cell-wise factors: omega_cell and wsrd_alpha
    std::vector<double> min_beta_per_cell(n_cells, 1.0);
    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t o = mesh.owner[f];
        int64_t nb = mesh.neighbour[f];
        min_beta_per_cell[o] = std::min(min_beta_per_cell[o], mesh.beta_upwind[f]);
        if (nb >= 0) {
            min_beta_per_cell[nb] = std::min(min_beta_per_cell[nb], mesh.beta_upwind[f]);
        }
    }
    for (int64_t c = 0; c < n_cells; ++c) {
        mesh.omega_cell[c] = std::max(0.1, min_beta_per_cell[c]);
    }

    // wsrd_alpha: Berger-Giuliani volume ratio against background cell volume
    std::vector<double> pos_vols;
    for (double v : mesh.cell_volumes) {
        if (v > 1e-60) pos_vols.push_back(v);
    }
    double v_target = 1.0;
    if (!pos_vols.empty()) {
        v_target = *std::max_element(pos_vols.begin(), pos_vols.end());
    }
    for (int64_t c = 0; c < n_cells; ++c) {
        mesh.wsrd_alpha[c] = std::clamp(mesh.cell_volumes[c] / (v_target > 1e-60 ? v_target : 1.0), 0.0, 1.0);
    }

    // 8. Aspect ratio per cell: maximum to minimum bounding box span along active coordinate axes
    mesh.aspect_ratio.resize(n_cells, 1.0);
    for (int64_t c = 0; c < n_cells; ++c) {
        int64_t off0 = mesh.cell_offsets[c];
        int64_t off1 = mesh.cell_offsets[c + 1];
        int64_t nv = off1 - off0;
        if (nv == 0) {
            mesh.aspect_ratio[c] = 1.0;
            continue;
        }
        double min_x = 1e30, max_x = -1e30;
        double min_y = 1e30, max_y = -1e30;
        double min_z = 1e30, max_z = -1e30;
        for (int64_t i = 0; i < nv; ++i) {
            int64_t vi = mesh.cell_nodes[off0 + i];
            double px = mesh.points[3 * vi];
            double py = mesh.points[3 * vi + 1];
            double pz = mesh.points[3 * vi + 2];
            min_x = std::min(min_x, px); max_x = std::max(max_x, px);
            min_y = std::min(min_y, py); max_y = std::max(max_y, py);
            min_z = std::min(min_z, pz); max_z = std::max(max_z, pz);
        }
        double spans[3] = { max_x - min_x, max_y - min_y, max_z - min_z };
        std::vector<double> valid_spans;
        for (int d = 0; d < 3; ++d) {
            if (spans[d] > 1e-12) {
                valid_spans.push_back(spans[d]);
            }
        }
        if (valid_spans.size() >= 2) {
            double s_min = *std::min_element(valid_spans.begin(), valid_spans.end());
            double s_max = *std::max_element(valid_spans.begin(), valid_spans.end());
            mesh.aspect_ratio[c] = std::max(1.0, s_max / std::max(s_min, 1e-15));
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

double computeNodalScaledJacobian(const Vec3& e1, const Vec3& e2, const Vec3& e3, double max_elem_edge) {
    double det_j = dot(e1, cross(e2, e3));
    double denom = max_elem_edge * max_elem_edge * max_elem_edge;
    return det_j / (denom + 1e-30);
}

double computeElementNormalizedScaledJacobian(double js_min, CellType type) {
    double js_opt = (type == CellType::HEXAHEDRON) ? 1.0 :
                    (type == CellType::WEDGE)      ? 0.8660254037844386 :
                                                     0.7071067811865476;
    if (js_min > js_opt) {
        return (1.0 + js_opt) - js_min;
    } else if (js_min < -js_opt) {
        return -(1.0 + js_opt) - js_min;
    } else {
        return js_min / js_opt;
    }
}

double checkPyramidBaseFictitiousHex(const std::vector<Vec3>& points, const std::vector<int64_t>& base_nodes, const Vec3& apex) {
    if (base_nodes.size() != 4) return -1.0;
    const Vec3& b0 = points[base_nodes[0]];
    const Vec3& b1 = points[base_nodes[1]];
    const Vec3& b2 = points[base_nodes[2]];
    const Vec3& b3 = points[base_nodes[3]];

    double l0 = (b1 - b0).norm();
    double l1 = (b2 - b1).norm();
    double l2 = (b3 - b2).norm();
    double l3 = (b0 - b3).norm();
    double h_bar = 0.25 * (l0 + l1 + l2 + l3);

    Vec3 n_base = cross(b2 - b0, b3 - b1);
    double n_mag = n_base.norm();
    if (n_mag < 1e-12) return -1.0;
    n_base = n_base / n_mag;

    Vec3 center = (b0 + b1 + b2 + b3) * 0.25;
    if (dot(apex - center, n_base) < 0.0) {
        n_base = -n_base;
    }

    std::array<Vec3, 8> hex_pts = {
        b0, b1, b2, b3,
        b0 + n_base * h_bar,
        b1 + n_base * h_bar,
        b2 + n_base * h_bar,
        b3 + n_base * h_bar
    };

    double max_e = std::max({l0, l1, l2, l3, h_bar});
    for (int k = 0; k < 4; ++k) {
        max_e = std::max(max_e, (hex_pts[(k + 1) % 4 + 4] - hex_pts[k + 4]).norm());
    }

    double s0 = computeNodalScaledJacobian(hex_pts[1]-hex_pts[0], hex_pts[3]-hex_pts[0], hex_pts[4]-hex_pts[0], max_e);
    double s1 = computeNodalScaledJacobian(hex_pts[2]-hex_pts[1], hex_pts[0]-hex_pts[1], hex_pts[5]-hex_pts[1], max_e);
    double s2 = computeNodalScaledJacobian(hex_pts[3]-hex_pts[2], hex_pts[1]-hex_pts[2], hex_pts[6]-hex_pts[2], max_e);
    double s3 = computeNodalScaledJacobian(hex_pts[0]-hex_pts[3], hex_pts[2]-hex_pts[3], hex_pts[7]-hex_pts[3], max_e);
    double s4 = computeNodalScaledJacobian(hex_pts[7]-hex_pts[4], hex_pts[5]-hex_pts[4], hex_pts[0]-hex_pts[4], max_e);
    double s5 = computeNodalScaledJacobian(hex_pts[4]-hex_pts[5], hex_pts[6]-hex_pts[5], hex_pts[1]-hex_pts[5], max_e);
    double s6 = computeNodalScaledJacobian(hex_pts[5]-hex_pts[6], hex_pts[7]-hex_pts[6], hex_pts[2]-hex_pts[6], max_e);
    double s7 = computeNodalScaledJacobian(hex_pts[6]-hex_pts[7], hex_pts[4]-hex_pts[7], hex_pts[3]-hex_pts[7], max_e);

    return std::min({s0, s1, s2, s3, s4, s5, s6, s7});
}

double prismJacobianBound(const std::vector<Vec3>& points, const std::array<int64_t, 6>& v) {
    const Vec3 a = points[static_cast<size_t>(v[1])] - points[static_cast<size_t>(v[0])];
    const Vec3 b = points[static_cast<size_t>(v[2])] - points[static_cast<size_t>(v[0])];
    const Vec3 at = points[static_cast<size_t>(v[4])] - points[static_cast<size_t>(v[3])];
    const Vec3 bt = points[static_cast<size_t>(v[5])] - points[static_cast<size_t>(v[3])];
    const Vec3 da = at - a;
    const Vec3 db = bt - b;
    const double max_a = std::max(a.norm(), at.norm());
    const double max_b = std::max(b.norm(), bt.norm());
    auto det3 = [](const Vec3& u, const Vec3& vv, const Vec3& w) -> double {
        return dot(u, cross(vv, w));
    };
    double result = 1.0;
    for (int i = 0; i < 3; ++i) {
        const Vec3 d = points[static_cast<size_t>(v[static_cast<size_t>(i + 3)])] -
                       points[static_cast<size_t>(v[static_cast<size_t>(i)])];
        const double A = det3(da, db, d);
        const double B = det3(da, b, d) + det3(a, db, d);
        const double C = det3(a, b, d);
        double m = std::min(C, A + B + C);
        if (A > 0.0) {
            const double t = -B / (2.0 * A);
            if (t > 0.0 && t < 1.0) {
                m = std::min(m, (A * t + B) * t + C);
            }
        }
        const double denom = max_a * max_b * d.norm();
        result = std::min(result, (denom > 0.0) ? (m / denom) : -1.0);
    }
    return result;
}

bool checkPyramidBaseConvexity(
    const std::vector<Vec3>& points,
    const std::vector<int64_t>& base_nodes,
    const Vec3& apex
) {
    if (base_nodes.size() != 4) return false;
    const std::array<Vec3, 4> base = {
        points[static_cast<size_t>(base_nodes[0])],
        points[static_cast<size_t>(base_nodes[1])],
        points[static_cast<size_t>(base_nodes[2])],
        points[static_cast<size_t>(base_nodes[3])],
    };
    Vec3 n_base = cross(base[2] - base[0], base[3] - base[1]);
    const double n_mag = n_base.norm();
    if (n_mag <= 1e-12) return false;
    n_base = n_base / n_mag;
    const Vec3 center = (base[0] + base[1] + base[2] + base[3]) * 0.25;
    if (dot(apex - center, n_base) < 0.0) {
        n_base = -n_base;
    }
    for (int k = 0; k < 4; ++k) {
        const Vec3& curr = base[static_cast<size_t>(k)];
        const Vec3& nxt = base[static_cast<size_t>((k + 1) % 4)];
        const Vec3& prv = base[static_cast<size_t>((k + 3) % 4)];
        if (dot(cross(nxt - curr, prv - curr), n_base) < 0.0) {
            return false;
        }
    }
    const double tet1_vol = dot(cross(base[1] - base[0], base[2] - base[0]), apex - base[0]);
    const double tet2_vol = dot(cross(base[2] - base[0], base[3] - base[0]), apex - base[0]);
    return (tet1_vol > 0.0) && (tet2_vol > 0.0);
}

double computeCellJENS(const std::vector<Vec3>& points, const std::vector<int64_t>& nodes, CellType type) {
    size_t n = nodes.size();
    if (type == CellType::HEXAHEDRON && n == 8) {
        std::array<Vec3, 8> p;
        for (int i = 0; i < 8; ++i) p[i] = points[nodes[i]];
        double max_e = 0.0;
        const int pairs[12][2] = {
            {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4}, {0,4}, {1,5}, {2,6}, {3,7}
        };
        for (int i = 0; i < 12; ++i) {
            max_e = std::max(max_e, (p[pairs[i][1]] - p[pairs[i][0]]).norm());
        }
        double s0 = computeNodalScaledJacobian(p[1]-p[0], p[3]-p[0], p[4]-p[0], max_e);
        double s1 = computeNodalScaledJacobian(p[2]-p[1], p[0]-p[1], p[5]-p[1], max_e);
        double s2 = computeNodalScaledJacobian(p[3]-p[2], p[1]-p[2], p[6]-p[2], max_e);
        double s3 = computeNodalScaledJacobian(p[0]-p[3], p[2]-p[3], p[7]-p[3], max_e);
        double s4 = computeNodalScaledJacobian(p[7]-p[4], p[5]-p[4], p[0]-p[4], max_e);
        double s5 = computeNodalScaledJacobian(p[4]-p[5], p[6]-p[5], p[1]-p[5], max_e);
        double s6 = computeNodalScaledJacobian(p[5]-p[6], p[7]-p[6], p[2]-p[6], max_e);
        double s7 = computeNodalScaledJacobian(p[6]-p[7], p[4]-p[7], p[3]-p[7], max_e);
        double min_sj = std::min({s0, s1, s2, s3, s4, s5, s6, s7});
        return computeElementNormalizedScaledJacobian(min_sj, CellType::HEXAHEDRON);
    } else if (type == CellType::WEDGE && n == 6) {
        std::array<Vec3, 6> p;
        for (int i = 0; i < 6; ++i) p[i] = points[nodes[i]];
        double max_e = 0.0;
        const int pairs[9][2] = {
            {0,1}, {1,2}, {2,0}, {3,4}, {4,5}, {5,3}, {0,3}, {1,4}, {2,5}
        };
        for (int i = 0; i < 9; ++i) {
            max_e = std::max(max_e, (p[pairs[i][1]] - p[pairs[i][0]]).norm());
        }
        double s0 = computeNodalScaledJacobian(p[1]-p[0], p[2]-p[0], p[3]-p[0], max_e);
        double s1 = computeNodalScaledJacobian(p[2]-p[1], p[0]-p[1], p[4]-p[1], max_e);
        double s2 = computeNodalScaledJacobian(p[0]-p[2], p[1]-p[2], p[5]-p[2], max_e);
        double s3 = computeNodalScaledJacobian(p[5]-p[3], p[4]-p[3], p[0]-p[3], max_e);
        double s4 = computeNodalScaledJacobian(p[3]-p[4], p[5]-p[4], p[1]-p[4], max_e);
        double s5 = computeNodalScaledJacobian(p[4]-p[5], p[3]-p[5], p[2]-p[5], max_e);
        double min_sj = std::min({s0, s1, s2, s3, s4, s5});
        return computeElementNormalizedScaledJacobian(min_sj, CellType::WEDGE);
    } else if (type == CellType::PYRAMID && n == 5) {
        std::vector<int64_t> base_nodes(nodes.begin(), nodes.begin() + 4);
        const Vec3& apex = points[nodes[4]];
        double base_sj = checkPyramidBaseFictitiousHex(points, base_nodes, apex);
        if (base_sj <= 0.0) return base_sj;

        std::array<Vec3, 5> p;
        for (int i = 0; i < 5; ++i) p[i] = points[nodes[i]];
        double max_e = 0.0;
        const int pairs[8][2] = {
            {0,1}, {1,2}, {2,3}, {3,0}, {0,4}, {1,4}, {2,4}, {3,4}
        };
        for (int i = 0; i < 8; ++i) {
            max_e = std::max(max_e, (p[pairs[i][1]] - p[pairs[i][0]]).norm());
        }
        double s0 = computeNodalScaledJacobian(p[1]-p[0], p[3]-p[0], p[4]-p[0], max_e);
        double s1 = computeNodalScaledJacobian(p[2]-p[1], p[0]-p[1], p[4]-p[1], max_e);
        double s2 = computeNodalScaledJacobian(p[3]-p[2], p[1]-p[2], p[4]-p[2], max_e);
        double s3 = computeNodalScaledJacobian(p[0]-p[3], p[2]-p[3], p[4]-p[3], max_e);
        Vec3 e0 = p[0] - apex, e1 = p[1] - apex, e2 = p[2] - apex, e3 = p[3] - apex;
        double a0 = computeNodalScaledJacobian(e1, e0, e2, max_e);
        double a1 = computeNodalScaledJacobian(e2, e1, e3, max_e);
        double a2 = computeNodalScaledJacobian(e3, e2, e0, max_e);
        double a3 = computeNodalScaledJacobian(e0, e3, e1, max_e);
        double min_sj = std::min({s0, s1, s2, s3, a0, a1, a2, a3, base_sj});
        return computeElementNormalizedScaledJacobian(min_sj, CellType::PYRAMID);
    } else if (type == CellType::TETRA && n == 4) {
        const Vec3& p0 = points[nodes[0]];
        const Vec3& p1 = points[nodes[1]];
        const Vec3& p2 = points[nodes[2]];
        const Vec3& p3 = points[nodes[3]];
        double max_e = std::max({
            (p1 - p0).norm(), (p2 - p0).norm(), (p3 - p0).norm(),
            (p2 - p1).norm(), (p3 - p1).norm(), (p3 - p2).norm()
        });
        double sj = computeNodalScaledJacobian(p1 - p0, p2 - p0, p3 - p0, max_e);
    return computeElementNormalizedScaledJacobian(sj, CellType::TETRA);
  } else if (type == CellType::POLYHEDRON) {
    return computeCellJENS(points, nodes, type, {});
  }
  return 1.0;
}

namespace {

struct Tri3D {
  Vec3 a;
  Vec3 b;
  Vec3 c;
};

double orient2dNorm(double ax, double ay, double bx, double by, double cx, double cy) {
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

void triangulateFace3D(
    const std::vector<Vec3>& points,
    const std::vector<int64_t>& face,
    std::vector<Tri3D>& out_tris,
    Vec3& out_sf
) {
  out_tris.clear();
  out_sf = Vec3(0, 0, 0);
  size_t m = face.size();
  if (m < 3) return;

  if (m == 3) {
    const Vec3& a = points[face[0]];
    const Vec3& b = points[face[1]];
    const Vec3& c = points[face[2]];
    out_sf = cross(b - a, c - a) * 0.5;
    out_tris.push_back({a, b, c});
    return;
  }

  Vec3 p_bar(0, 0, 0);
  for (int64_t vid : face) {
    p_bar += points[vid];
  }
  p_bar /= static_cast<double>(m);

  Vec3 sf_raw(0, 0, 0);
  for (size_t i = 0; i < m; ++i) {
    const Vec3& pi = points[face[i]];
    const Vec3& pj = points[face[(i + 1) % m]];
    sf_raw += cross(pi - p_bar, pj - p_bar) * 0.5;
  }
  double area_raw = sf_raw.norm();
  if (area_raw <= 1e-30) {
    return;
  }
  Vec3 n_hat = sf_raw / area_raw;

  // Check if face is star-shaped around p_bar
  bool star_shaped = true;
  for (size_t i = 0; i < m; ++i) {
    const Vec3& pi = points[face[i]];
    const Vec3& pj = points[face[(i + 1) % m]];
    Vec3 sub_sf = cross(pi - p_bar, pj - p_bar) * 0.5;
    if (dot(sub_sf, n_hat) <= 1e-12 * area_raw) {
      star_shaped = false;
      break;
    }
  }

  if (star_shaped) {
    out_sf = sf_raw;
    out_tris.reserve(m);
    for (size_t i = 0; i < m; ++i) {
      out_tris.push_back({p_bar, points[face[i]], points[face[(i + 1) % m]]});
    }
    return;
  }

  // Fallback for non-convex faces (e.g., L-shaped merged faces):
  // Project onto orthonormal 2D face frame (u, v, n_hat) and ear-clip.
  const Vec3& p0 = points[face[0]];
  Vec3 best_edge(0, 0, 0);
  double best_len2 = -1.0;
  for (size_t i = 0; i < m; ++i) {
    Vec3 e = points[face[(i + 1) % m]] - points[face[i]];
    Vec3 e_proj = e - n_hat * dot(e, n_hat);
    double l2 = e_proj.norm2();
    if (l2 > best_len2) {
      best_len2 = l2;
      best_edge = e_proj;
    }
  }
  Vec3 u_ax;
  if (best_len2 > 1e-30) {
    u_ax = best_edge / std::sqrt(best_len2);
  } else {
    Vec3 cand = (std::abs(n_hat.x) < 0.9) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    u_ax = cross(n_hat, cand).normalized();
  }
  Vec3 v_ax = cross(n_hat, u_ax);

  std::vector<double> px(m), py(m);
  double max_r = 1e-15;
  for (size_t i = 0; i < m; ++i) {
    Vec3 d = points[face[i]] - p0;
    px[i] = dot(d, u_ax);
    py[i] = dot(d, v_ax);
    max_r = std::max(max_r, std::sqrt(px[i] * px[i] + py[i] * py[i]));
  }
  double inv_r = 1.0 / max_r;
  for (size_t i = 0; i < m; ++i) {
    px[i] *= inv_r;
    py[i] *= inv_r;
  }

  double area2 = 0.0;
  for (size_t i = 0; i < m; ++i) {
    size_t j = (i + 1) % m;
    area2 += px[i] * py[j] - px[j] * py[i];
  }
  bool reversed = (area2 < 0.0);
  if (reversed) {
    std::reverse(px.begin(), px.end());
    std::reverse(py.begin(), py.end());
  }

  std::vector<size_t> rem(m);
  for (size_t i = 0; i < m; ++i) rem[i] = i;

  auto point_in_tri = [&](double tx, double ty, size_t ia, size_t ib, size_t ic) -> bool {
    double d0 = orient2dNorm(px[ia], py[ia], px[ib], py[ib], tx, ty);
    double d1 = orient2dNorm(px[ib], py[ib], px[ic], py[ic], tx, ty);
    double d2 = orient2dNorm(px[ic], py[ic], px[ia], py[ia], tx, ty);
    return (d0 >= -1e-12 && d1 >= -1e-12 && d2 >= -1e-12);
  };

  std::vector<std::array<size_t, 3>> local_tris;
  local_tris.reserve(m - 2);
  size_t max_iters = m * m;
  size_t iters = 0;
  while (rem.size() > 3 && iters < max_iters) {
    ++iters;
    size_t cur_m = rem.size();
    bool found = false;
    for (size_t i = 0; i < cur_m; ++i) {
      size_t prev_i = rem[(i + cur_m - 1) % cur_m];
      size_t curr_i = rem[i];
      size_t next_i = rem[(i + 1) % cur_m];
      double det = orient2dNorm(px[prev_i], py[prev_i], px[curr_i], py[curr_i], px[next_i], py[next_i]);
      if (det <= 1e-12) continue;

      bool is_ear = true;
      for (size_t j = 0; j < cur_m; ++j) {
        size_t k = rem[j];
        if (k == prev_i || k == curr_i || k == next_i) continue;
        if (point_in_tri(px[k], py[k], prev_i, curr_i, next_i)) {
          is_ear = false;
          break;
        }
      }
      if (is_ear) {
        local_tris.push_back({prev_i, curr_i, next_i});
        rem.erase(rem.begin() + static_cast<ptrdiff_t>(i));
        found = true;
        break;
      }
    }
    if (!found) {
      size_t best_pos = 0;
      double best_det = -1e30;
      for (size_t i = 0; i < cur_m; ++i) {
        size_t prev_i = rem[(i + cur_m - 1) % cur_m];
        size_t curr_i = rem[i];
        size_t next_i = rem[(i + 1) % cur_m];
        double det = orient2dNorm(px[prev_i], py[prev_i], px[curr_i], py[curr_i], px[next_i], py[next_i]);
        if (det > best_det) {
          best_det = det;
          best_pos = i;
        }
      }
      size_t prev_i = rem[(best_pos + cur_m - 1) % cur_m];
      size_t curr_i = rem[best_pos];
      size_t next_i = rem[(best_pos + 1) % cur_m];
      local_tris.push_back({prev_i, curr_i, next_i});
      rem.erase(rem.begin() + static_cast<ptrdiff_t>(best_pos));
    }
  }
  if (rem.size() == 3) {
    local_tris.push_back({rem[0], rem[1], rem[2]});
  }

  out_tris.reserve(local_tris.size());
  for (const auto& t : local_tris) {
    size_t i0 = reversed ? (m - 1 - t[0]) : t[0];
    size_t i1 = reversed ? (m - 1 - t[2]) : t[1];
    size_t i2 = reversed ? (m - 1 - t[1]) : t[2];
    const Vec3& a = points[face[i0]];
    const Vec3& b = points[face[i1]];
    const Vec3& c = points[face[i2]];
    out_sf += cross(b - a, c - a) * 0.5;
    out_tris.push_back({a, b, c});
  }
}

}  // namespace

double computeCellJENS(
    const std::vector<Vec3>& points,
    const std::vector<int64_t>& nodes,
    CellType type,
    const std::vector<std::vector<int64_t>>& cell_faces
) {
  if (type != CellType::POLYHEDRON) {
    return computeCellJENS(points, nodes, type);
  }
  if (nodes.size() < 4) {
    return 0.0;
  }

  std::vector<std::vector<int64_t>> faces_storage;
  const std::vector<std::vector<int64_t>>* faces_ptr = &cell_faces;

  if (cell_faces.empty()) {
    // Deduplicate nodes preserving order
    std::vector<int64_t> uniq_nodes;
    uniq_nodes.reserve(nodes.size());
    for (int64_t v : nodes) {
      if (std::find(uniq_nodes.begin(), uniq_nodes.end(), v) == uniq_nodes.end()) {
        uniq_nodes.push_back(v);
      }
    }
    if (uniq_nodes.size() < 4) return 0.0;

    CellType tmp_type = CellType::POLYHEDRON;
    bool inv = false;
    faces_storage = extractCellFaces(uniq_nodes, points, tmp_type, &inv);
    if (inv) {
      for (auto& f : faces_storage) {
        std::reverse(f.begin(), f.end());
      }
    }
    faces_ptr = &faces_storage;
  }

  const auto& faces = *faces_ptr;
  if (faces.size() < 4) {
    return 0.0;
  }

  // Translation-invariant reference point
  Vec3 p_ref(0, 0, 0);
  size_t ref_count = 0;
  for (const auto& f : faces) {
    for (int64_t v : f) {
      p_ref += points[v];
      ++ref_count;
    }
  }
  if (ref_count == 0) return 0.0;
  p_ref /= static_cast<double>(ref_count);

  std::vector<Tri3D> all_tris;
  all_tris.reserve(faces.size() * 4);
  std::vector<Tri3D> face_tris;
  Vec3 r_close(0, 0, 0);
  double a_tot = 0.0;
  double v_p = 0.0;
  Vec3 c_p_num(0, 0, 0);

  for (const auto& f : faces) {
    Vec3 sf_f(0, 0, 0);
    triangulateFace3D(points, f, face_tris, sf_f);
    double af = sf_f.norm();
    a_tot += af;
    r_close += sf_f;
    for (const auto& t : face_tris) {
      all_tris.push_back(t);
      Vec3 da = t.a - p_ref;
      Vec3 db = t.b - p_ref;
      Vec3 dc = t.c - p_ref;
      double vt_ref = dot(da, cross(db, dc)) / 6.0;
      v_p += vt_ref;
      c_p_num += ((da + db + dc) * 0.25) * vt_ref;
    }
  }

  if (all_tris.empty() || a_tot <= 1e-30 || std::abs(v_p) <= 1e-30 ||
      std::abs(v_p) <= 1e-14 * std::pow(a_tot, 1.5)) {
    return 0.0;
  }

  Vec3 c_p = p_ref + (c_p_num / v_p);

  std::vector<double> j_ens_tets;
  std::vector<double> v_tets;
  j_ens_tets.reserve(all_tris.size());
  v_tets.reserve(all_tris.size());

  bool all_pos = true;
  bool has_pos = false;
  double min_j_all = 1.0;
  double min_j_pos = 1.0;
  double sum_abs_v = 0.0;

  for (const auto& t : all_tris) {
    Vec3 e1 = t.a - c_p;
    Vec3 e2 = t.b - c_p;
    Vec3 e3 = t.c - c_p;
    double l_max = std::max({
        e1.norm(), e2.norm(), e3.norm(),
        (t.b - t.a).norm(), (t.c - t.b).norm(), (t.a - t.c).norm()
    });
    double det_t = dot(e1, cross(e2, e3));
    double vt = det_t / 6.0;
    double js_t = det_t / (l_max * l_max * l_max + 1e-30);
    double j_ens_t = computeElementNormalizedScaledJacobian(js_t, CellType::TETRA);

    j_ens_tets.push_back(j_ens_t);
    v_tets.push_back(vt);
    sum_abs_v += std::abs(vt);

    if (vt <= 0.0) {
      all_pos = false;
    } else {
      has_pos = true;
      min_j_pos = std::min(min_j_pos, j_ens_t);
    }
    min_j_all = std::min(min_j_all, j_ens_t);
  }

  if (v_p < 0.0) {
    return std::max(-1.0, min_j_all);
  }

  bool is_closed = (r_close.norm() <= 1e-3 * a_tot);
  if (all_pos) {
    return min_j_pos;
  }
  if (is_closed && has_pos) {
    double convexity_ratio = v_p / (sum_abs_v + 1e-30);
    return min_j_pos * convexity_ratio;
  }
  return std::max(-1.0, min_j_all);
}

std::vector<double> computeMeshJENS(const NativeMeshData& mesh) {
  int64_t n_cells = mesh.n_cells();
  int64_t n_pts = mesh.n_points();
  std::vector<double> j_ens(n_cells, 1.0);
  if (n_cells == 0 || n_pts == 0) return j_ens;

  std::vector<Vec3> pts(n_pts);
  for (int64_t i = 0; i < n_pts; ++i) {
    pts[i] = Vec3(mesh.points[3 * i], mesh.points[3 * i + 1], mesh.points[3 * i + 2]);
  }

  int64_t n_faces = mesh.n_faces();
  std::vector<std::vector<std::vector<int64_t>>> cell_faces_map(n_cells);
  if (n_faces > 0 && mesh.face_offsets.size() == static_cast<size_t>(n_faces + 1)) {
    for (int64_t f = 0; f < n_faces; ++f) {
      int64_t off0 = mesh.face_offsets[f];
      int64_t off1 = mesh.face_offsets[f + 1];
      std::vector<int64_t> fn(mesh.face_nodes.begin() + off0, mesh.face_nodes.begin() + off1);
      int64_t o = mesh.owner[f];
      if (o >= 0 && o < n_cells) {
        cell_faces_map[o].push_back(fn);
      }
      int64_t nb = mesh.neighbour[f];
      if (nb >= 0 && nb < n_cells) {
        std::reverse(fn.begin(), fn.end());
        cell_faces_map[nb].push_back(std::move(fn));
      }
    }
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    int64_t off0 = mesh.cell_offsets[c];
    int64_t off1 = mesh.cell_offsets[c + 1];
    std::vector<int64_t> cnodes(mesh.cell_nodes.begin() + off0, mesh.cell_nodes.begin() + off1);
    CellType ctype = (static_cast<size_t>(c) < mesh.cell_types.size())
                         ? static_cast<CellType>(mesh.cell_types[c])
                         : CellType::POLYHEDRON;
    j_ens[c] = computeCellJENS(pts, cnodes, ctype, cell_faces_map[c]);
  }
  return j_ens;
}

} // namespace cfd
