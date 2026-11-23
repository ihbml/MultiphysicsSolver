#include "octree_hexcore.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd {

namespace {

size_t getMaxOctreeNodes() {
  constexpr size_t kDefaultMaxNodes = 24000000ULL;
#ifdef _MSC_VER
  char* env_val = nullptr;
  size_t env_len = 0;
  if (_dupenv_s(&env_val, &env_len, "CFD_MAX_OCTREE_NODES") == 0 && env_val != nullptr) {
    char* end_ptr = nullptr;
    unsigned long long parsed = std::strtoull(env_val, &end_ptr, 10);
    bool ok = (env_val[0] != '\0' && end_ptr != nullptr && *end_ptr == '\0' && parsed > 0ULL);
    std::free(env_val);
    if (ok) {
      return static_cast<size_t>(parsed);
    }
  }
#else
  const char* env_val = std::getenv("CFD_MAX_OCTREE_NODES");
  if (env_val != nullptr && env_val[0] != '\0') {
    char* end_ptr = nullptr;
    unsigned long long parsed = std::strtoull(env_val, &end_ptr, 10);
    if (end_ptr != nullptr && *end_ptr == '\0' && parsed > 0ULL) {
      return static_cast<size_t>(parsed);
    }
  }
#endif
  return kDefaultMaxNodes;
}

// Quantized 3D coordinate for deduplicating vertices with machine precision
struct QuantizedKey {
  int64_t x;
  int64_t y;
  int64_t z;

  bool operator==(const QuantizedKey& o) const {
    return x == o.x && y == o.y && z == o.z;
  }

  bool operator<(const QuantizedKey& o) const {
    if (x != o.x) return x < o.x;
    if (y != o.y) return y < o.y;
    return z < o.z;
  }
};

struct QuantizedKeyHash {
  std::size_t operator()(const QuantizedKey& k) const {
    std::size_t h1 = std::hash<int64_t>()(k.x);
    std::size_t h2 = std::hash<int64_t>()(k.y);
    std::size_t h3 = std::hash<int64_t>()(k.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

inline QuantizedKey quantize(const Vec3& p, double tol) {
  double inv = 1.0 / tol;
  return QuantizedKey{
      static_cast<int64_t>(std::llround(p.x * inv)),
      static_cast<int64_t>(std::llround(p.y * inv)),
      static_cast<int64_t>(std::llround(p.z * inv))
  };
}

// 3D AMIPS energy of a tetrahedron with vertices (v0, v1, v2, v3)
double computeTetAMIPS(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3) {
  Vec3 e1 = v1 - v0;
  Vec3 e2 = v2 - v0;
  Vec3 e3 = v3 - v0;
  double det_j = dot(cross(e1, e2), e3);
  if (det_j <= 1e-18) {
    return 1e30;  // Inverted or degenerate element
  }

  double edge_sum_sq = (v0 - v1).norm2() + (v0 - v2).norm2() + (v0 - v3).norm2() +
                       (v1 - v2).norm2() + (v1 - v3).norm2() + (v2 - v3).norm2();

  double ratio = (27.0 / 16.0) * (edge_sum_sq * edge_sum_sq * edge_sum_sq) / (det_j * det_j);
  return std::cbrt(ratio);
}

// Internal Octree Node representation (64-bit indices)
struct OctreeNode {
  Vec3 bmin;
  Vec3 bmax;
  Vec3 center;
  Vec3 size;
  int32_t level = 0;
  int64_t parent = -1;
  int64_t children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  bool is_leaf = true;
  bool is_active = false;
  int32_t region = 0;       // 0 = fluid, 1 = solid
  int64_t cell_index = -1;  // Indexed among active leaves
  double phi_c = 0.0;
  Vec3 grad_c = Vec3(0, 0, 1);
  double v_bg = 0.0;
};

struct RawQuad {
  std::array<Vec3, 4> pts;
  int64_t owner = -1;
  int64_t neighbour = -1;
  std::string patch_name;
  bool is_farfield = false;
  bool is_cut_face = false;
};

std::vector<double> buildGradedAxisCoords(
    double d_min,
    double b_min,
    double b_max,
    double d_max,
    double h_core,
    double growth_ratio,
    double max_stretch
) {
  double c_min = std::max(d_min, b_min - 1.5 * h_core);
  double c_max = std::min(d_max, b_max + 1.5 * h_core);
  if (c_min - d_min < 1.2 * h_core) c_min = d_min;
  if (d_max - c_max < 1.2 * h_core) c_max = d_max;
  if (c_max <= c_min) {
    c_min = d_min;
    c_max = d_max;
  }

  int64_t n_core = std::max<int64_t>(1, static_cast<int64_t>(std::llround((c_max - c_min) / h_core)));
  double dx_core = (c_max - c_min) / static_cast<double>(n_core);
  double max_h = max_stretch * dx_core;

  auto build_slab_steps = [&](double length) -> std::vector<double> {
    std::vector<double> steps;
    if (length <= 1e-12 * h_core) return steps;
    double cur_h = dx_core;
    double sum_h = 0.0;
    while (sum_h < length) {
      double next_h = std::min(max_h, cur_h * growth_ratio);
      if (!steps.empty() && sum_h + 0.5 * next_h >= length) {
        break;
      }
      steps.push_back(next_h);
      sum_h += next_h;
      cur_h = next_h;
    }
    double scale = length / sum_h;
    for (double& s : steps) s *= scale;
    return steps;
  };

  std::vector<double> left_steps = build_slab_steps(c_min - d_min);
  std::vector<double> right_steps = build_slab_steps(d_max - c_max);

  std::vector<double> coords;
  coords.reserve(left_steps.size() + static_cast<size_t>(n_core) + right_steps.size() + 1);

  if (!left_steps.empty()) {
    double pos = d_min;
    coords.push_back(d_min);
    for (size_t idx = left_steps.size(); idx > 1; --idx) {
      pos += left_steps[idx - 1];
      coords.push_back(pos);
    }
    coords.push_back(c_min);
  } else {
    coords.push_back(c_min);
  }

  for (int64_t i = 1; i < n_core; ++i) {
    coords.push_back(c_min + static_cast<double>(i) * dx_core);
  }
  coords.push_back(c_max);

  if (!right_steps.empty()) {
    double pos = c_max;
    for (size_t idx = 0; idx + 1 < right_steps.size(); ++idx) {
      pos += right_steps[idx];
      coords.push_back(pos);
    }
    coords.push_back(d_max);
  }

  return coords;
}

std::vector<int64_t> computeFastRCM(int64_t n_cells, const std::vector<MosaicFace>& internal_faces) {
  std::vector<int64_t> perm(n_cells);
  for (int64_t i = 0; i < n_cells; ++i) perm[i] = i;
  if (n_cells <= 1 || internal_faces.empty()) return perm;

  std::vector<int64_t> deg(n_cells, 0);
  for (const auto& mf : internal_faces) {
    if (mf.owner >= 0 && mf.neighbour >= 0 && mf.owner != mf.neighbour) {
      ++deg[mf.owner];
      ++deg[mf.neighbour];
    }
  }

  std::vector<int64_t> offsets(n_cells + 1, 0);
  for (int64_t c = 0; c < n_cells; ++c) {
    offsets[c + 1] = offsets[c] + deg[c];
  }
  std::vector<int64_t> adj(static_cast<size_t>(offsets[n_cells]));
  std::vector<int64_t> cursor = offsets;
  for (const auto& mf : internal_faces) {
    int64_t o = mf.owner;
    int64_t nb = mf.neighbour;
    if (o >= 0 && nb >= 0 && o != nb) {
      adj[static_cast<size_t>(cursor[o]++)] = nb;
      adj[static_cast<size_t>(cursor[nb]++)] = o;
    }
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    auto beg = adj.begin() + offsets[c];
    auto end = adj.begin() + offsets[c + 1];
    std::sort(beg, end);
    auto u_end = std::unique(beg, end);
    deg[c] = static_cast<int64_t>(u_end - beg);
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    auto beg = adj.begin() + offsets[c];
    auto end = beg + deg[c];
    std::sort(beg, end, [&](int64_t a, int64_t b) {
      if (deg[a] != deg[b]) return deg[a] < deg[b];
      return a < b;
    });
  }

  std::vector<uint8_t> visited(n_cells, 0);
  std::vector<int64_t> cm_order;
  cm_order.reserve(n_cells);
  std::queue<int64_t> q;

  while (static_cast<int64_t>(cm_order.size()) < n_cells) {
    int64_t start_node = -1;
    int64_t min_deg = std::numeric_limits<int64_t>::max();
    for (int64_t c = 0; c < n_cells; ++c) {
      if (!visited[c] && deg[c] < min_deg) {
        min_deg = deg[c];
        start_node = c;
      }
    }
    if (start_node < 0) break;

    visited[start_node] = 1;
    q.push(start_node);
    while (!q.empty()) {
      int64_t u = q.front();
      q.pop();
      cm_order.push_back(u);
      int64_t beg = offsets[u];
      int64_t end = beg + deg[u];
      for (int64_t idx = beg; idx < end; ++idx) {
        int64_t v = adj[static_cast<size_t>(idx)];
        if (!visited[v]) {
          visited[v] = 1;
          q.push(v);
        }
      }
    }
  }

  for (int64_t i = 0; i < n_cells; ++i) {
    perm[i] = cm_order[static_cast<size_t>(n_cells - 1 - i)];
  }
  return perm;
}

// Area-weighted normal and centroid via center-point fan
std::pair<Vec3, Vec3> computePolygonNormalAndCentroid(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points
) {
  size_t n = face_nodes.size();
  if (n < 3) {
    return {Vec3(0, 0, 0), Vec3(0, 0, 0)};
  }

  Vec3 c_face(0, 0, 0);
  for (int64_t idx : face_nodes) c_face += points[idx];
  c_face /= static_cast<double>(n);

  Vec3 normal_sum(0, 0, 0);
  Vec3 centroid_sum(0, 0, 0);
  double total_area = 0.0;

  for (size_t i = 0; i < n; ++i) {
    const Vec3& pi = points[face_nodes[i]];
    const Vec3& pj = points[face_nodes[(i + 1) % n]];
    Vec3 cross_p = cross(pi - c_face, pj - c_face);
    double area_tri = cross_p.norm() * 0.5;
    normal_sum += cross_p * 0.5;
    Vec3 tri_c = (c_face + pi + pj) / 3.0;
    centroid_sum += tri_c * area_tri;
    total_area += area_tri;
  }

  Vec3 centroid;
  if (total_area > 1e-15) {
    centroid = centroid_sum / total_area;
  } else {
    centroid = c_face;
  }

  return {normal_sum, centroid};
}

// Area-weighted normal and centroid when one node is replaced
std::pair<Vec3, Vec3> computePolygonNormalAndCentroidReplaced(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points,
    int64_t replaced_node,
    const Vec3& replaced_pos
) {
  size_t n = face_nodes.size();
  if (n < 3) {
    return {Vec3(0, 0, 0), Vec3(0, 0, 0)};
  }

  auto get_pt = [&](size_t idx) -> Vec3 {
    int64_t nid = face_nodes[idx];
    return (nid == replaced_node) ? replaced_pos : points[nid];
  };

  Vec3 c_face(0, 0, 0);
  for (size_t i = 0; i < n; ++i) c_face += get_pt(i);
  c_face /= static_cast<double>(n);

  Vec3 normal_sum(0, 0, 0);
  Vec3 centroid_sum(0, 0, 0);
  double total_area = 0.0;

  for (size_t i = 0; i < n; ++i) {
    Vec3 pi = get_pt(i);
    Vec3 pj = get_pt((i + 1) % n);
    Vec3 cross_p = cross(pi - c_face, pj - c_face);
    double area_tri = cross_p.norm() * 0.5;
    normal_sum += cross_p * 0.5;
    Vec3 tri_c = (c_face + pi + pj) / 3.0;
    centroid_sum += tri_c * area_tri;
    total_area += area_tri;
  }

  Vec3 centroid;
  if (total_area > 1e-15) {
    centroid = centroid_sum / total_area;
  } else {
    centroid = c_face;
  }

  return {normal_sum, centroid};
}

// Folded / inverted face detection
bool isFaceFolded(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points,
    const Vec3& normal_sum
) {
  size_t n = face_nodes.size();
  if (n < 3) return false;
  double norm_mag = normal_sum.norm();
  if (norm_mag <= 1e-15) return false;

  Vec3 c_face(0, 0, 0);
  for (int64_t idx : face_nodes) c_face += points[idx];
  c_face /= static_cast<double>(n);

  for (size_t i = 0; i < n; ++i) {
    const Vec3& pi = points[face_nodes[i]];
    const Vec3& pj = points[face_nodes[(i + 1) % n]];
    Vec3 cross_p = cross(pi - c_face, pj - c_face);
    if (cross_p.norm() > 1e-15) {
      if (dot(cross_p, normal_sum) <= 0.0) return true;
    }
  }
  return false;
}

}  // namespace

int32_t OctreeHexcore::generate(
    NativeMeshData& mesh,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    const double* farfield_bbox,
    const OctreeOptions& options
) {
  if (!surf_points || !surf_faces || n_points <= 0 || n_faces <= 0 || options.h0 <= 0.0) {
    return -1;
  }

  mesh.clear();

  // 1. Build MeshSDF for signed distance and gradient queries
  MeshSDF sdf(surf_points, n_points, surf_faces, n_faces);

  // Surface Bounding Box and closed-shell topology check
  Vec3 s_min(1e30, 1e30, 1e30), s_max(-1e30, -1e30, -1e30);
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(surf_points[3 * i], surf_points[3 * i + 1], surf_points[3 * i + 2]);
    s_min.x = std::min(s_min.x, p.x);
    s_min.y = std::min(s_min.y, p.y);
    s_min.z = std::min(s_min.z, p.z);
    s_max.x = std::max(s_max.x, p.x);
    s_max.y = std::max(s_max.y, p.y);
    s_max.z = std::max(s_max.z, p.z);
  }

  bool is_closed_surface = false;
  double surf_signed_vol = 0.0;
  if (n_faces >= 4) {
    std::vector<uint64_t> undirected_edges;
    undirected_edges.reserve(static_cast<size_t>(n_faces) * 3);
    for (int64_t f = 0; f < n_faces; ++f) {
      int32_t i0 = surf_faces[3 * f + 0];
      int32_t i1 = surf_faces[3 * f + 1];
      int32_t i2 = surf_faces[3 * f + 2];
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= n_points || i1 >= n_points || i2 >= n_points) {
        continue;
      }
      Vec3 v0(surf_points[3 * i0] - s_min.x, surf_points[3 * i0 + 1] - s_min.y, surf_points[3 * i0 + 2] - s_min.z);
      Vec3 v1(surf_points[3 * i1] - s_min.x, surf_points[3 * i1 + 1] - s_min.y, surf_points[3 * i1 + 2] - s_min.z);
      Vec3 v2(surf_points[3 * i2] - s_min.x, surf_points[3 * i2 + 1] - s_min.y, surf_points[3 * i2 + 2] - s_min.z);
      surf_signed_vol += dot(v0, cross(v1, v2)) / 6.0;

      int32_t tri[3] = {i0, i1, i2};
      for (int e = 0; e < 3; ++e) {
        uint32_t a = static_cast<uint32_t>(std::min(tri[e], tri[(e + 1) % 3]));
        uint32_t b = static_cast<uint32_t>(std::max(tri[e], tri[(e + 1) % 3]));
        undirected_edges.push_back((static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b));
      }
    }
    if (!undirected_edges.empty()) {
      std::sort(undirected_edges.begin(), undirected_edges.end());
      bool all_even = true;
      size_t idx = 0;
      while (idx < undirected_edges.size()) {
        size_t next = idx + 1;
        while (next < undirected_edges.size() && undirected_edges[next] == undirected_edges[idx]) {
          ++next;
        }
        if (((next - idx) % 2) != 0) {
          all_even = false;
          break;
        }
        idx = next;
      }
      is_closed_surface = all_even;
    }
  }

  // Determine Domain Bounding Box (disambiguating standard vs interleaved farfield_bbox)
  Vec3 dom_min, dom_max;
  if (farfield_bbox) {
    bool std_valid = (farfield_bbox[3] > farfield_bbox[0] &&
                      farfield_bbox[4] > farfield_bbox[1] &&
                      farfield_bbox[5] > farfield_bbox[2]);
    bool int_valid = (farfield_bbox[1] > farfield_bbox[0] &&
                      farfield_bbox[3] > farfield_bbox[2] &&
                      farfield_bbox[5] > farfield_bbox[4]);
    Vec3 std_min(farfield_bbox[0], farfield_bbox[1], farfield_bbox[2]);
    Vec3 std_max(farfield_bbox[3], farfield_bbox[4], farfield_bbox[5]);
    Vec3 int_min(farfield_bbox[0], farfield_bbox[2], farfield_bbox[4]);
    Vec3 int_max(farfield_bbox[1], farfield_bbox[3], farfield_bbox[5]);
    auto encloses_surf = [&](const Vec3& bmin, const Vec3& bmax) -> bool {
      return bmin.x <= s_min.x + 1e-9 && bmax.x >= s_max.x - 1e-9 &&
             bmin.y <= s_min.y + 1e-9 && bmax.y >= s_max.y - 1e-9 &&
             bmin.z <= s_min.z + 1e-9 && bmax.z >= s_max.z - 1e-9;
    };
    if (std_valid && int_valid) {
      bool std_enc = encloses_surf(std_min, std_max);
      bool int_enc = encloses_surf(int_min, int_max);
      if (int_enc && !std_enc) {
        dom_min = int_min;
        dom_max = int_max;
      } else {
        dom_min = std_min;
        dom_max = std_max;
      }
    } else if (std_valid) {
      dom_min = std_min;
      dom_max = std_max;
    } else if (int_valid) {
      dom_min = int_min;
      dom_max = int_max;
    } else {
      dom_min = std_min;
      dom_max = std_max;
    }
  } else if (options.domain_mode == 0) {
    // external_fluid default: 5L upstream, 10L downstream, 5L lateral
    Vec3 span = s_max - s_min;
    double l_ref = std::max({span.x, span.y, span.z});
    if (l_ref < 1e-12) l_ref = 1.0;
    dom_min = Vec3(s_min.x - 5.0 * l_ref, s_min.y - 5.0 * l_ref, s_min.z - 5.0 * l_ref);
    dom_max = Vec3(s_max.x + 10.0 * l_ref, s_max.y + 5.0 * l_ref, s_max.z + 5.0 * l_ref);
  } else {
    // solid_only / internal_fluid: tight fit on geometry
    dom_min = s_min;
    dom_max = s_max;
  }

  Vec3 dom_span = dom_max - dom_min;
  if (dom_span.x <= 1e-12 || dom_span.y <= 1e-12 || dom_span.z <= 1e-12) {
    return -1;
  }

  // 2. Hybrid Separable Graded Farfield Root Grid + Isotropic Core (64-bit)
  double h0 = options.h0;
  double growth_ratio = options.grading;
  if (growth_ratio <= 1.0) growth_ratio = 1.0 + growth_ratio;
  growth_ratio = std::clamp(growth_ratio, 1.05, 1.35);

  Vec3 surf_span = s_max - s_min;
  double l_ref = std::max({surf_span.x, surf_span.y, surf_span.z, 1e-12});
  double dom_max_span = std::max({dom_span.x, dom_span.y, dom_span.z});
  double dom_min_span = std::min({dom_span.x, dom_span.y, dom_span.z});

  std::vector<double> x_coords, y_coords, z_coords;
  int32_t effective_max_depth = std::clamp(options.max_depth, 0, 12);

  if (options.domain_mode == 0 && (dom_max_span / h0) > 20.0) {
    double h_target_far = std::max({options.h_max, 0.25 * l_ref, h0});
    int32_t refine_depth = 0;
    while (refine_depth < 10 && h0 * static_cast<double>(1LL << refine_depth) < h_target_far) {
      ++refine_depth;
    }
    while (refine_depth > 0 && h0 * static_cast<double>(1LL << refine_depth) > 0.5 * dom_min_span) {
      --refine_depth;
    }
    double h_root = h0 * static_cast<double>(1LL << refine_depth);
    effective_max_depth = (refine_depth > 0) ? refine_depth : std::clamp(options.max_depth, 0, 12);

    x_coords = buildGradedAxisCoords(dom_min.x, s_min.x, s_max.x, dom_max.x, h_root, growth_ratio, 8.0);
    y_coords = buildGradedAxisCoords(dom_min.y, s_min.y, s_max.y, dom_max.y, h_root, growth_ratio, 8.0);
    z_coords = buildGradedAxisCoords(dom_min.z, s_min.z, s_max.z, dom_max.z, h_root, growth_ratio, 8.0);
  } else if (options.domain_mode == 2 && (dom_max_span / h0) > 32.0) {
    int32_t refine_depth = 0;
    while (refine_depth < 8 && (dom_max_span / (h0 * static_cast<double>(1LL << refine_depth))) > 32.0) {
      ++refine_depth;
    }
    double h_root = h0 * static_cast<double>(1LL << refine_depth);
    effective_max_depth = std::clamp(options.max_depth + refine_depth, 0, 10);

    int64_t nx_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.x / h_root)));
    int64_t ny_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.y / h_root)));
    int64_t nz_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.z / h_root)));
    x_coords.resize(static_cast<size_t>(nx_u) + 1);
    y_coords.resize(static_cast<size_t>(ny_u) + 1);
    z_coords.resize(static_cast<size_t>(nz_u) + 1);
    for (int64_t i = 0; i <= nx_u; ++i) x_coords[static_cast<size_t>(i)] = dom_min.x + (dom_span.x * i) / nx_u;
    for (int64_t j = 0; j <= ny_u; ++j) y_coords[static_cast<size_t>(j)] = dom_min.y + (dom_span.y * j) / ny_u;
    for (int64_t k = 0; k <= nz_u; ++k) z_coords[static_cast<size_t>(k)] = dom_min.z + (dom_span.z * k) / nz_u;
  } else {
    int64_t nx_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.x / h0)));
    int64_t ny_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.y / h0)));
    int64_t nz_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.z / h0)));
    x_coords.resize(static_cast<size_t>(nx_u) + 1);
    y_coords.resize(static_cast<size_t>(ny_u) + 1);
    z_coords.resize(static_cast<size_t>(nz_u) + 1);
    for (int64_t i = 0; i <= nx_u; ++i) x_coords[static_cast<size_t>(i)] = dom_min.x + (dom_span.x * i) / nx_u;
    for (int64_t j = 0; j <= ny_u; ++j) y_coords[static_cast<size_t>(j)] = dom_min.y + (dom_span.y * j) / ny_u;
    for (int64_t k = 0; k <= nz_u; ++k) z_coords[static_cast<size_t>(k)] = dom_min.z + (dom_span.z * k) / nz_u;
  }

  int64_t nx = static_cast<int64_t>(x_coords.size()) - 1;
  int64_t ny = static_cast<int64_t>(y_coords.size()) - 1;
  int64_t nz = static_cast<int64_t>(z_coords.size()) - 1;

  double min_root_h = 1e30;
  for (int64_t i = 0; i < nx; ++i) min_root_h = std::min(min_root_h, x_coords[i + 1] - x_coords[i]);
  for (int64_t j = 0; j < ny; ++j) min_root_h = std::min(min_root_h, y_coords[j + 1] - y_coords[j]);
  for (int64_t k = 0; k < nz; ++k) min_root_h = std::min(min_root_h, z_coords[k + 1] - z_coords[k]);

  double max_abs_coord = std::max({
      std::abs(dom_min.x), std::abs(dom_min.y), std::abs(dom_min.z),
      std::abs(dom_max.x), std::abs(dom_max.y), std::abs(dom_max.z)
  });
  double min_leaf_h = min_root_h / static_cast<double>(1LL << effective_max_depth);
  double eps_probe = std::max(1e-4 * min_leaf_h, 1e-11 * std::max(1.0, max_abs_coord));
  double quant_tol = std::max(1e-6 * min_leaf_h, 1e-12 * std::max(1.0, max_abs_coord));
  double dom_tol = std::max(1e-7 * min_root_h, 1e-11 * std::max(1.0, max_abs_coord));

  const size_t max_octree_nodes = getMaxOctreeNodes();
  if (nx <= 0 || ny <= 0 || nz <= 0) {
    return -1;
  }
  const size_t sx = static_cast<size_t>(nx);
  const size_t sy = static_cast<size_t>(ny);
  const size_t sz = static_cast<size_t>(nz);
  if (sx > max_octree_nodes || sy > max_octree_nodes / sx) {
    return -12;
  }
  const size_t n_xy = sx * sy;
  if (sz > max_octree_nodes / n_xy) {
    return -12;
  }
  size_t n_root = n_xy * sz;
  std::vector<OctreeNode> nodes;
  const size_t reserve_count = (n_root <= max_octree_nodes / 4) ? (n_root * 4) : max_octree_nodes;
  nodes.reserve(reserve_count);
  nodes.resize(n_root);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t idx = 0; idx < static_cast<int64_t>(n_root); ++idx) {
    int64_t i = idx % nx;
    int64_t rem = idx / nx;
    int64_t j = rem % ny;
    int64_t k = rem / ny;
    OctreeNode& node = nodes[static_cast<size_t>(idx)];
    node.bmin = Vec3(x_coords[i], y_coords[j], z_coords[k]);
    node.bmax = Vec3(x_coords[i + 1], y_coords[j + 1], z_coords[k + 1]);
    node.center = (node.bmin + node.bmax) * 0.5;
    node.size = node.bmax - node.bmin;
    node.level = 0;
    node.parent = -1;
    node.is_leaf = true;
    node.v_bg = node.size.x * node.size.y * node.size.z;
  }

  auto find_axis_idx = [](const std::vector<double>& coords, double val, int64_t n) -> int64_t {
    if (n <= 1) return 0;
    auto it = std::upper_bound(coords.begin() + 1, coords.end() - 1, val);
    int64_t idx = static_cast<int64_t>(it - coords.begin()) - 1;
    return std::clamp<int64_t>(idx, 0, n - 1);
  };

  // Helper lambda to find leaf node at point p in O(log n + depth) time (thread-safe read-only)
  auto find_leaf_at_point = [&](const Vec3& p) -> int64_t {
    if (p.x < dom_min.x || p.x > dom_max.x ||
        p.y < dom_min.y || p.y > dom_max.y ||
        p.z < dom_min.z || p.z > dom_max.z) {
      return -1;
    }
    int64_t i = find_axis_idx(x_coords, p.x, nx);
    int64_t j = find_axis_idx(y_coords, p.y, ny);
    int64_t k = find_axis_idx(z_coords, p.z, nz);
    int64_t curr = (k * ny + j) * nx + i;
    int64_t n_nodes_sz = static_cast<int64_t>(nodes.size());
    while (curr >= 0 && curr < n_nodes_sz && !nodes[static_cast<size_t>(curr)].is_leaf) {
      int cx = (p.x >= nodes[static_cast<size_t>(curr)].center.x) ? 1 : 0;
      int cy = (p.y >= nodes[static_cast<size_t>(curr)].center.y) ? 1 : 0;
      int cz = (p.z >= nodes[static_cast<size_t>(curr)].center.z) ? 1 : 0;
      int child_slot = cz * 4 + cy * 2 + cx;
      int64_t child = nodes[static_cast<size_t>(curr)].children[child_slot];
      if (child < 0) break;
      curr = child;
    }
    return (curr >= 0 && curr < n_nodes_sz && nodes[static_cast<size_t>(curr)].is_leaf) ? curr : -1;
  };

  // Helper lambda to subdivide a batch of unique leaf nodes in parallel with memory guard
  auto subdivide_batch = [&](const std::vector<int64_t>& split_ids, std::vector<int64_t>& out_children) -> bool {
    const size_t n_split = split_ids.size();
    out_children.clear();
    if (n_split == 0) return true;
    if (nodes.size() > max_octree_nodes || n_split > (max_octree_nodes - nodes.size()) / 8) {
      return false;
    }
    const size_t old_n = nodes.size();
    nodes.resize(old_n + n_split * 8);
    out_children.resize(n_split * 8);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t s = 0; s < static_cast<int64_t>(n_split); ++s) {
      const int64_t parent_idx = split_ids[static_cast<size_t>(s)];
      const int32_t parent_level = nodes[static_cast<size_t>(parent_idx)].level;
      const Vec3 mid = nodes[static_cast<size_t>(parent_idx)].center;
      const Vec3 pmin = nodes[static_cast<size_t>(parent_idx)].bmin;
      const Vec3 pmax = nodes[static_cast<size_t>(parent_idx)].bmax;

      const std::array<std::pair<Vec3, Vec3>, 8> octants = {{
          {{pmin.x, pmin.y, pmin.z}, {mid.x, mid.y, mid.z}},
          {{mid.x, pmin.y, pmin.z}, {pmax.x, mid.y, mid.z}},
          {{pmin.x, mid.y, pmin.z}, {mid.x, pmax.y, mid.z}},
          {{mid.x, mid.y, pmin.z}, {pmax.x, pmax.y, mid.z}},
          {{pmin.x, pmin.y, mid.z}, {mid.x, mid.y, pmax.z}},
          {{mid.x, pmin.y, mid.z}, {pmax.x, mid.y, pmax.z}},
          {{pmin.x, mid.y, mid.z}, {mid.x, pmax.y, pmax.z}},
          {{mid.x, mid.y, mid.z}, {pmax.x, pmax.y, pmax.z}},
      }};

      nodes[static_cast<size_t>(parent_idx)].is_leaf = false;
      for (int c = 0; c < 8; ++c) {
        const size_t child_slot_idx = old_n + static_cast<size_t>(s) * 8 + static_cast<size_t>(c);
        const int64_t child_idx = static_cast<int64_t>(child_slot_idx);
        OctreeNode& child = nodes[child_slot_idx];
        child.bmin = octants[static_cast<size_t>(c)].first;
        child.bmax = octants[static_cast<size_t>(c)].second;
        child.center = (child.bmin + child.bmax) * 0.5;
        child.size = child.bmax - child.bmin;
        child.level = parent_level + 1;
        child.parent = parent_idx;
        child.is_leaf = true;
        child.v_bg = child.size.x * child.size.y * child.size.z;
        nodes[static_cast<size_t>(parent_idx)].children[c] = child_idx;
        out_children[static_cast<size_t>(s) * 8 + static_cast<size_t>(c)] = child_idx;
      }
    }
    return true;
  };

  // 3. Adaptive Surface Refinement Pass (Frontier + OpenMP)
  std::vector<int64_t> frontier(n_root);
  for (size_t i = 0; i < n_root; ++i) frontier[i] = static_cast<int64_t>(i);

  for (int32_t pass = 0; pass < effective_max_depth; ++pass) {
    size_t n_eval = frontier.size();
    if (n_eval == 0) break;

    // At extreme coordinate offsets (10^6), single-precision float loss in double is ~1e-10,
    // so snap leaf centers back to local surface-relative precision for SDF queries.
    Vec3 s_mid = (s_min + s_max) * 0.5;
    std::vector<double> leaf_centers(n_eval * 3);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t k = 0; k < static_cast<int64_t>(n_eval); ++k) {
      const Vec3& c = nodes[static_cast<size_t>(frontier[static_cast<size_t>(k)])].center;
      leaf_centers[3 * k + 0] = c.x;
      leaf_centers[3 * k + 1] = c.y;
      leaf_centers[3 * k + 2] = c.z;
    }

    std::vector<double> phis(n_eval);
    sdf.evaluate(leaf_centers.data(), static_cast<int64_t>(n_eval), phis.data(), nullptr);

    std::vector<uint8_t> should_split(n_eval, 0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t k = 0; k < static_cast<int64_t>(n_eval); ++k) {
      int64_t idx = frontier[static_cast<size_t>(k)];
      const Vec3& nsz = nodes[static_cast<size_t>(idx)].size;
      double max_edge = std::max({nsz.x, nsz.y, nsz.z});
      if (options.h_min > 0.0 && max_edge * 0.5 < 0.48 * options.h_min) {
        continue;
      }
      double diag = nsz.norm();
      double band_factor =
          0.75 + 0.15 * std::clamp(0.25 - options.alpha_curv, 0.0, 0.25)
               + 0.15 * std::clamp(0.25 - options.beta_prox, 0.0, 0.25);
      if (std::abs(phis[static_cast<size_t>(k)]) <= band_factor * diag) {
        should_split[static_cast<size_t>(k)] = 1;
      }
    }

    std::vector<int64_t> split_ids;
    split_ids.reserve(n_eval);
    for (size_t k = 0; k < n_eval; ++k) {
      if (should_split[k]) {
        split_ids.push_back(frontier[k]);
      }
    }
    std::vector<int64_t> next_frontier;
    if (!subdivide_batch(split_ids, next_frontier)) {
      return -12;
    }
    frontier.swap(next_frontier);
  }

  // 4. Parallel 2:1 Balancing Pass Across All 26 Directions (Zero omp critical)
  std::vector<int64_t> leaves;
  leaves.reserve(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].is_leaf) leaves.push_back(static_cast<int64_t>(i));
  }

  int max_threads = 1;
#ifdef _OPENMP
  max_threads = std::max(1, omp_get_max_threads());
#endif

  int balance_iterations = 0;
  while (balance_iterations < 32) {
    ++balance_iterations;
    std::vector<std::vector<int64_t>> thread_splits(static_cast<size_t>(max_threads));

#ifdef _OPENMP
#pragma omp parallel
    {
      int tid = omp_get_thread_num();
      auto& local_splits = thread_splits[static_cast<size_t>(tid)];
#pragma omp for schedule(dynamic, 64)
      for (int64_t li = 0; li < static_cast<int64_t>(leaves.size()); ++li) {
        int64_t node_i = leaves[static_cast<size_t>(li)];
        if (!nodes[static_cast<size_t>(node_i)].is_leaf) continue;
        int32_t lev_a = nodes[static_cast<size_t>(node_i)].level;
        Vec3 c = nodes[static_cast<size_t>(node_i)].center;
        Vec3 h = nodes[static_cast<size_t>(node_i)].size * 0.5;

        for (int di = -1; di <= 1; ++di) {
          for (int dj = -1; dj <= 1; ++dj) {
            for (int dk = -1; dk <= 1; ++dk) {
              if (di == 0 && dj == 0 && dk == 0) continue;
              Vec3 p_probe(
                  c.x + di * (h.x + eps_probe),
                  c.y + dj * (h.y + eps_probe),
                  c.z + dk * (h.z + eps_probe)
              );
              int64_t nb = find_leaf_at_point(p_probe);
              if (nb >= 0 && nodes[static_cast<size_t>(nb)].is_leaf) {
                if (lev_a - nodes[static_cast<size_t>(nb)].level > 1) {
                  local_splits.push_back(nb);
                }
              }
            }
          }
        }
      }
    }
#else
    {
      auto& local_splits = thread_splits[0];
      for (size_t li = 0; li < leaves.size(); ++li) {
        int64_t node_i = leaves[li];
        if (!nodes[static_cast<size_t>(node_i)].is_leaf) continue;
        int32_t lev_a = nodes[static_cast<size_t>(node_i)].level;
        Vec3 c = nodes[static_cast<size_t>(node_i)].center;
        Vec3 h = nodes[static_cast<size_t>(node_i)].size * 0.5;
        for (int di = -1; di <= 1; ++di) {
          for (int dj = -1; dj <= 1; ++dj) {
            for (int dk = -1; dk <= 1; ++dk) {
              if (di == 0 && dj == 0 && dk == 0) continue;
              Vec3 p_probe(
                  c.x + di * (h.x + eps_probe),
                  c.y + dj * (h.y + eps_probe),
                  c.z + dk * (h.z + eps_probe)
              );
              int64_t nb = find_leaf_at_point(p_probe);
              if (nb >= 0 && nodes[static_cast<size_t>(nb)].is_leaf) {
                if (lev_a - nodes[static_cast<size_t>(nb)].level > 1) {
                  local_splits.push_back(nb);
                }
              }
            }
          }
        }
      }
    }
#endif

    std::vector<int64_t> to_split;
    for (const auto& ts : thread_splits) {
      to_split.insert(to_split.end(), ts.begin(), ts.end());
    }
    if (to_split.empty()) break;

    std::sort(to_split.begin(), to_split.end());
    to_split.erase(std::unique(to_split.begin(), to_split.end()), to_split.end());

    std::vector<int64_t> split_ids;
    split_ids.reserve(to_split.size());
    for (int64_t nb : to_split) {
      if (nodes[static_cast<size_t>(nb)].is_leaf) {
        split_ids.push_back(nb);
      }
    }
    std::vector<int64_t> new_children;
    if (!subdivide_batch(split_ids, new_children)) {
      return -12;
    }

    std::vector<int64_t> next_leaves;
    next_leaves.reserve(leaves.size() + new_children.size());
    for (int64_t l : leaves) {
      if (nodes[static_cast<size_t>(l)].is_leaf) next_leaves.push_back(l);
    }
    next_leaves.insert(next_leaves.end(), new_children.begin(), new_children.end());
    leaves.swap(next_leaves);
  }

  // 5. Batched SDF & Closed-Shell Winding Number Sign Resolution on All Leaves
  std::vector<int64_t> all_leaves;
  all_leaves.reserve(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].is_leaf) all_leaves.push_back(static_cast<int64_t>(i));
  }
  int64_t n_leaves = static_cast<int64_t>(all_leaves.size());
  if (n_leaves == 0) return -1;

  std::vector<double> leaf_centers(static_cast<size_t>(n_leaves) * 3);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t k = 0; k < n_leaves; ++k) {
    const Vec3& c = nodes[static_cast<size_t>(all_leaves[static_cast<size_t>(k)])].center;
    leaf_centers[3 * k + 0] = c.x;
    leaf_centers[3 * k + 1] = c.y;
    leaf_centers[3 * k + 2] = c.z;
  }

  std::vector<double> leaf_phis(static_cast<size_t>(n_leaves), 0.0);
  std::vector<double> leaf_grads(static_cast<size_t>(n_leaves) * 3, 0.0);
  sdf.evaluate(leaf_centers.data(), n_leaves, leaf_phis.data(), leaf_grads.data());

  // Resolve pseudonormal sign flips on closed multi-shell thin-walled surfaces (>1 connected shell) via winding number
  bool is_multi_shell_closed = false;
  if (is_closed_surface && surf_signed_vol > 0.0 && n_points > 4) {
    std::vector<int32_t> parent(static_cast<size_t>(n_points));
    for (int64_t i = 0; i < n_points; ++i) parent[static_cast<size_t>(i)] = static_cast<int32_t>(i);
    auto find_root = [&](int32_t u) -> int32_t {
      while (parent[static_cast<size_t>(u)] != u) {
        parent[static_cast<size_t>(u)] = parent[static_cast<size_t>(parent[static_cast<size_t>(u)])];
        u = parent[static_cast<size_t>(u)];
      }
      return u;
    };
    for (int64_t f = 0; f < n_faces; ++f) {
      int32_t i0 = surf_faces[3 * f + 0];
      int32_t i1 = surf_faces[3 * f + 1];
      int32_t i2 = surf_faces[3 * f + 2];
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= n_points || i1 >= n_points || i2 >= n_points) continue;
      int32_t r0 = find_root(i0);
      int32_t r1 = find_root(i1);
      int32_t r2 = find_root(i2);
      if (r0 != r1) parent[static_cast<size_t>(r1)] = r0;
      if (r0 != r2) parent[static_cast<size_t>(r2)] = r0;
    }
    int32_t first_root = -1;
    for (int64_t f = 0; f < n_faces; ++f) {
      int32_t i0 = surf_faces[3 * f + 0];
      if (i0 < 0 || i0 >= n_points) continue;
      int32_t r = find_root(i0);
      if (first_root < 0) {
        first_root = r;
      } else if (r != first_root) {
        is_multi_shell_closed = true;
        break;
      }
    }
  }

  if (is_multi_shell_closed) {
    std::vector<int64_t> wn_leaf_pos;
    std::vector<double> wn_pts;
    wn_leaf_pos.reserve(static_cast<size_t>(n_leaves));
    wn_pts.reserve(static_cast<size_t>(n_leaves) * 3);
    for (int64_t k = 0; k < n_leaves; ++k) {
      const OctreeNode& ln = nodes[static_cast<size_t>(all_leaves[static_cast<size_t>(k)])];
      double pad = ln.size.norm();
      if (ln.center.x >= s_min.x - pad && ln.center.x <= s_max.x + pad &&
          ln.center.y >= s_min.y - pad && ln.center.y <= s_max.y + pad &&
          ln.center.z >= s_min.z - pad && ln.center.z <= s_max.z + pad) {
        wn_leaf_pos.push_back(k);
        wn_pts.push_back(ln.center.x);
        wn_pts.push_back(ln.center.y);
        wn_pts.push_back(ln.center.z);
      }
    }
    if (!wn_leaf_pos.empty()) {
      std::vector<double> wns(wn_leaf_pos.size(), 0.0);
      sdf.winding_number(wn_pts.data(), static_cast<int64_t>(wn_leaf_pos.size()), wns.data());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
      for (int64_t j = 0; j < static_cast<int64_t>(wn_leaf_pos.size()); ++j) {
        int64_t k = wn_leaf_pos[static_cast<size_t>(j)];
        bool inside = (wns[static_cast<size_t>(j)] > 0.5);
        double phi = leaf_phis[static_cast<size_t>(k)];
        if (inside && phi > 0.0) {
          leaf_phis[static_cast<size_t>(k)] = -phi;
          leaf_grads[3 * k + 0] = -leaf_grads[3 * k + 0];
          leaf_grads[3 * k + 1] = -leaf_grads[3 * k + 1];
          leaf_grads[3 * k + 2] = -leaf_grads[3 * k + 2];
        } else if (!inside && phi < 0.0) {
          leaf_phis[static_cast<size_t>(k)] = -phi;
          leaf_grads[3 * k + 0] = -leaf_grads[3 * k + 0];
          leaf_grads[3 * k + 1] = -leaf_grads[3 * k + 1];
          leaf_grads[3 * k + 2] = -leaf_grads[3 * k + 2];
        }
      }
    }
  }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t k = 0; k < n_leaves; ++k) {
    OctreeNode& ln = nodes[static_cast<size_t>(all_leaves[static_cast<size_t>(k)])];
    double phi = leaf_phis[static_cast<size_t>(k)];
    ln.phi_c = phi;
    ln.grad_c = Vec3(leaf_grads[3 * k + 0], leaf_grads[3 * k + 1], leaf_grads[3 * k + 2]);
    double diag = ln.size.norm();
    bool active = false;
    int32_t reg = 0;

    if (options.domain_mode == 0) {
      if (phi >= -0.15 * diag) {
        active = true;
        reg = 0;
      }
    } else if (options.domain_mode == 2) {
      if (phi <= 0.15 * diag) {
        active = true;
        reg = 1;
      }
    } else if (options.domain_mode == 3) {
      active = true;
      reg = (phi >= 0.0) ? 0 : 1;
    } else {
      if (phi <= 0.15 * diag) {
        active = true;
        reg = 0;
      }
    }
    ln.is_active = active;
    ln.region = reg;
  }

  std::vector<int64_t> active_leaves;
  active_leaves.reserve(static_cast<size_t>(n_leaves));
  for (int64_t k = 0; k < n_leaves; ++k) {
    int64_t node_i = all_leaves[static_cast<size_t>(k)];
    if (nodes[static_cast<size_t>(node_i)].is_active) {
      nodes[static_cast<size_t>(node_i)].cell_index = static_cast<int64_t>(active_leaves.size());
      active_leaves.push_back(node_i);
    }
  }

  int64_t n_active_cells = static_cast<int64_t>(active_leaves.size());
  if (n_active_cells == 0) {
    return -1;
  }

  // 6 & 7. Parallel Owner-Unique Mosaic Face Extraction + Vertex Deduplication + Canonical Hex8 Corners
  std::vector<CellType> cell_types(static_cast<size_t>(n_active_cells), CellType::HEXAHEDRON);
  std::vector<std::vector<RawQuad>> thread_quads(static_cast<size_t>(max_threads));

#ifdef _OPENMP
#pragma omp parallel
  {
    int tid = omp_get_thread_num();
    int n_threads = omp_get_num_threads();
    int64_t chunk = (n_active_cells + n_threads - 1) / n_threads;
    int64_t c_beg = tid * chunk;
    int64_t c_end = std::min(n_active_cells, c_beg + chunk);
    auto& local_quads = thread_quads[static_cast<size_t>(tid)];
    if (c_end > c_beg) {
      local_quads.reserve(static_cast<size_t>(c_end - c_beg) * 4);
    }
#else
  {
    int64_t c_beg = 0;
    int64_t c_end = n_active_cells;
    auto& local_quads = thread_quads[0];
    local_quads.reserve(static_cast<size_t>(n_active_cells) * 4);
#endif
    for (int64_t c = c_beg; c < c_end; ++c) {
      int64_t node_idx = active_leaves[static_cast<size_t>(c)];
      const OctreeNode& cell_a = nodes[static_cast<size_t>(node_idx)];

      for (int dir = 0; dir < 6; ++dir) {
        Vec3 n_dir;
        Vec3 q0, q1, q2, q3;
        double plane_val = 0.0;
        bool is_farfield = false;
        std::string farfield_patch;

        if (dir == 0) {
          n_dir = Vec3(-1, 0, 0);
          q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
          q1 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
          q2 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
          q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
          plane_val = cell_a.bmin.x;
          if (std::abs(plane_val - dom_min.x) < dom_tol) {
            is_farfield = true;
            farfield_patch = "inlet";
          }
        } else if (dir == 1) {
          n_dir = Vec3(1, 0, 0);
          q0 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
          q1 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
          q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
          q3 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
          plane_val = cell_a.bmax.x;
          if (std::abs(plane_val - dom_max.x) < dom_tol) {
            is_farfield = true;
            farfield_patch = "outlet";
          }
        } else if (dir == 2) {
          n_dir = Vec3(0, -1, 0);
          q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
          q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
          q2 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
          q3 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
          plane_val = cell_a.bmin.y;
          if (std::abs(plane_val - dom_min.y) < dom_tol) {
            is_farfield = true;
            farfield_patch = "sides";
          }
        } else if (dir == 3) {
          n_dir = Vec3(0, 1, 0);
          q0 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
          q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
          q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
          q3 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
          plane_val = cell_a.bmax.y;
          if (std::abs(plane_val - dom_max.y) < dom_tol) {
            is_farfield = true;
            farfield_patch = "sides";
          }
        } else if (dir == 4) {
          n_dir = Vec3(0, 0, -1);
          q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
          q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
          q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
          q3 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
          plane_val = cell_a.bmin.z;
          if (std::abs(plane_val - dom_min.z) < dom_tol) {
            is_farfield = true;
            farfield_patch = "bottom";
          }
        } else {
          n_dir = Vec3(0, 0, 1);
          q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
          q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
          q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
          q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
          plane_val = cell_a.bmax.z;
          if (std::abs(plane_val - dom_max.z) < dom_tol) {
            is_farfield = true;
            farfield_patch = "top";
          }
        }

        Vec3 fc = (q0 + q1 + q2 + q3) * 0.25;
        Vec3 p_probe = fc + n_dir * eps_probe;
        int64_t nb_leaf = find_leaf_at_point(p_probe);

        if (nb_leaf < 0) {
          std::string patch = is_farfield ? farfield_patch : "wall";
          local_quads.push_back({{q0, q1, q2, q3}, c, -1, patch, is_farfield, !is_farfield});
        } else if (nodes[static_cast<size_t>(nb_leaf)].level > cell_a.level) {
          cell_types[static_cast<size_t>(c)] = CellType::POLYHEDRON;

          Vec3 m01 = (q0 + q1) * 0.5;
          Vec3 m12 = (q1 + q2) * 0.5;
          Vec3 m23 = (q2 + q3) * 0.5;
          Vec3 m30 = (q3 + q0) * 0.5;
          Vec3 mc = (q0 + q1 + q2 + q3) * 0.25;

          std::array<std::array<Vec3, 4>, 4> sub_quads = {{
              {q0, m01, mc, m30},
              {m01, q1, m12, mc},
              {mc, m12, q2, m23},
              {m30, mc, m23, q3},
          }};

          for (int sq = 0; sq < 4; ++sq) {
            Vec3 sub_c = (sub_quads[sq][0] + sub_quads[sq][1] + sub_quads[sq][2] + sub_quads[sq][3]) * 0.25;
            Vec3 sub_probe = sub_c + n_dir * eps_probe;
            int64_t fine_leaf = find_leaf_at_point(sub_probe);

            if (fine_leaf >= 0 && nodes[static_cast<size_t>(fine_leaf)].is_active) {
              int64_t fine_c = nodes[static_cast<size_t>(fine_leaf)].cell_index;
              int64_t o = std::min(c, fine_c);
              int64_t nb = std::max(c, fine_c);
              if (o == c) {
                local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3]}, o, nb, "", false, false});
              } else {
                local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][3], sub_quads[sq][2], sub_quads[sq][1]}, o, nb, "", false, false});
              }
            } else {
              std::string patch = is_farfield ? farfield_patch : "wall";
              local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3]}, c, -1, patch, is_farfield, !is_farfield});
            }
          }
        } else if (!nodes[static_cast<size_t>(nb_leaf)].is_active) {
          std::string patch = is_farfield ? farfield_patch : "wall";
          local_quads.push_back({{q0, q1, q2, q3}, c, -1, patch, is_farfield, !is_farfield});
        } else if (nodes[static_cast<size_t>(nb_leaf)].level < cell_a.level) {
          // Fine cell facing active coarse cell: coarse cell emits 4 sub-quads
        } else {
          int64_t nb_c = nodes[static_cast<size_t>(nb_leaf)].cell_index;
          if (c < nb_c) {
            local_quads.push_back({{q0, q1, q2, q3}, c, nb_c, "", false, false});
          }
        }
      }
    }
  }

  std::unordered_map<QuantizedKey, int64_t, QuantizedKeyHash> vmap;
  vmap.reserve(static_cast<size_t>(n_active_cells) * 2);
  std::vector<Vec3> node_coords;
  node_coords.reserve(static_cast<size_t>(n_active_cells) * 2);

  auto get_or_add_vertex = [&](const Vec3& pt) -> int64_t {
    QuantizedKey k = quantize(pt, quant_tol);
    auto it = vmap.find(k);
    if (it != vmap.end()) {
      return it->second;
    }
    int64_t id = static_cast<int64_t>(node_coords.size());
    node_coords.push_back(pt);
    vmap[k] = id;
    return id;
  };

  // Record canonical VTK Hex8 (0..7) corner vertex IDs BEFORE snapping
  std::vector<std::array<int64_t, 8>> hex_corners(static_cast<size_t>(n_active_cells));
  for (int64_t c = 0; c < n_active_cells; ++c) {
    const OctreeNode& onode = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(c)])];
    const Vec3& mn = onode.bmin;
    const Vec3& mx = onode.bmax;
    hex_corners[static_cast<size_t>(c)] = {
        get_or_add_vertex(Vec3(mn.x, mn.y, mn.z)),  // 0: (0,0,0)
        get_or_add_vertex(Vec3(mx.x, mn.y, mn.z)),  // 1: (1,0,0)
        get_or_add_vertex(Vec3(mx.x, mx.y, mn.z)),  // 2: (1,1,0)
        get_or_add_vertex(Vec3(mn.x, mx.y, mn.z)),  // 3: (0,1,0)
        get_or_add_vertex(Vec3(mn.x, mn.y, mx.z)),  // 4: (0,0,1)
        get_or_add_vertex(Vec3(mx.x, mn.y, mx.z)),  // 5: (1,0,1)
        get_or_add_vertex(Vec3(mx.x, mx.y, mx.z)),  // 6: (1,1,1)
        get_or_add_vertex(Vec3(mn.x, mx.y, mx.z)),  // 7: (0,1,1)
    };
  }

  size_t total_raw_quads = 0;
  for (const auto& tq : thread_quads) total_raw_quads += tq.size();

  std::vector<MosaicFace> mosaic_faces;
  mosaic_faces.reserve(total_raw_quads);
  std::vector<std::vector<int64_t>> cell_to_faces(static_cast<size_t>(n_active_cells));

  for (const auto& tq : thread_quads) {
    for (const auto& rq : tq) {
      MosaicFace mf;
      mf.nodes = {
          get_or_add_vertex(rq.pts[0]),
          get_or_add_vertex(rq.pts[1]),
          get_or_add_vertex(rq.pts[2]),
          get_or_add_vertex(rq.pts[3]),
      };
      mf.owner = rq.owner;
      mf.neighbour = rq.neighbour;
      mf.patch_name = rq.patch_name;
      mf.is_farfield = rq.is_farfield;
      mf.is_cut_face = rq.is_cut_face;

      int64_t f_idx = static_cast<int64_t>(mosaic_faces.size());
      mosaic_faces.push_back(std::move(mf));
      cell_to_faces[static_cast<size_t>(rq.owner)].push_back(f_idx);
      if (rq.neighbour >= 0) {
        cell_to_faces[static_cast<size_t>(rq.neighbour)].push_back(f_idx);
      }
    }
  }

  // 7b. Conformal Edge-Midpoint Insertion (eliminate hanging T-junction edges before snapping)
  for (int split_pass = 0; split_pass < 3; ++split_pass) {
    bool any_inserted = false;
    for (auto& mf : mosaic_faces) {
      std::vector<int64_t> refined_nodes;
      refined_nodes.reserve(mf.nodes.size() * 2);
      size_t nv = mf.nodes.size();
      bool face_changed = false;
      for (size_t i = 0; i < nv; ++i) {
        int64_t u = mf.nodes[i];
        int64_t w = mf.nodes[(i + 1) % nv];
        refined_nodes.push_back(u);
        Vec3 mid_pt = (node_coords[static_cast<size_t>(u)] + node_coords[static_cast<size_t>(w)]) * 0.5;
        auto it_mid = vmap.find(quantize(mid_pt, quant_tol));
        if (it_mid != vmap.end() && it_mid->second != u && it_mid->second != w) {
          refined_nodes.push_back(it_mid->second);
          face_changed = true;
          any_inserted = true;
        }
      }
      if (face_changed) {
        mf.nodes.swap(refined_nodes);
        if (mf.owner >= 0) {
          cell_types[static_cast<size_t>(mf.owner)] = CellType::POLYHEDRON;
        }
        if (mf.neighbour >= 0) {
          cell_types[static_cast<size_t>(mf.neighbour)] = CellType::POLYHEDRON;
        }
      }
    }
    if (!any_inserted) break;
  }

  // 8. Cut-Cell Snapping along grad(phi) with Strict AMIPS Guard, Winding Sign Fix, and Hex8 Guard
  if (options.snap_cut_cells) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t f = 0; f < static_cast<int64_t>(mosaic_faces.size()); ++f) {
      auto [norm, cent] = computePolygonNormalAndCentroid(mosaic_faces[f].nodes, node_coords);
      mosaic_faces[f].normal_owner = norm;
      mosaic_faces[f].centre = cent;
    }

    std::vector<Vec3> cell_approx_center(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t c = 0; c < n_active_cells; ++c) {
      Vec3 c_approx(0, 0, 0);
      int64_t count = 0;
      for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
        c_approx += mosaic_faces[static_cast<size_t>(f_idx)].centre;
        ++count;
      }
      cell_approx_center[static_cast<size_t>(c)] =
          (count > 0) ? (c_approx / static_cast<double>(count))
                      : nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(c)])].center;
    }

    std::unordered_set<int64_t> wall_vertices;
    for (const auto& mf : mosaic_faces) {
      if (mf.neighbour == -1 && !mf.is_farfield) {
        for (int64_t v : mf.nodes) {
          wall_vertices.insert(v);
        }
      }
    }

    // Restrict node_to_cells / node_to_faces strictly to wall_vertices
    std::unordered_map<int64_t, std::vector<int64_t>> node_to_cells;
    std::unordered_map<int64_t, std::vector<int64_t>> node_to_faces;
    node_to_cells.reserve(wall_vertices.size());
    node_to_faces.reserve(wall_vertices.size());
    for (size_t f = 0; f < mosaic_faces.size(); ++f) {
      const auto& mf = mosaic_faces[f];
      for (int64_t v : mf.nodes) {
        if (wall_vertices.find(v) == wall_vertices.end()) continue;
        node_to_cells[v].push_back(mf.owner);
        if (mf.neighbour >= 0) {
          node_to_cells[v].push_back(mf.neighbour);
        }
        node_to_faces[v].push_back(static_cast<int64_t>(f));
      }
    }
    for (auto& kv : node_to_cells) {
      std::sort(kv.second.begin(), kv.second.end());
      kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
    }
    for (auto& kv : node_to_faces) {
      std::sort(kv.second.begin(), kv.second.end());
      kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
    }

    std::vector<int64_t> wall_verts(wall_vertices.begin(), wall_vertices.end());
    std::sort(wall_verts.begin(), wall_verts.end());
    size_t n_wall = wall_verts.size();
    std::vector<double> wall_pts(n_wall * 3);
    for (size_t i = 0; i < n_wall; ++i) {
      const Vec3& p = node_coords[static_cast<size_t>(wall_verts[i])];
      wall_pts[3 * i + 0] = p.x;
      wall_pts[3 * i + 1] = p.y;
      wall_pts[3 * i + 2] = p.z;
    }
    std::vector<double> phis(n_wall);
    std::vector<double> grads(n_wall * 3);
    if (n_wall > 0) {
      sdf.evaluate(wall_pts.data(), static_cast<int64_t>(n_wall), phis.data(), grads.data());
      if (is_multi_shell_closed) {
        std::vector<double> wall_wns(n_wall, 0.0);
        sdf.winding_number(wall_pts.data(), static_cast<int64_t>(n_wall), wall_wns.data());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int64_t i = 0; i < static_cast<int64_t>(n_wall); ++i) {
          bool inside = (wall_wns[static_cast<size_t>(i)] > 0.5);
          double phi = phis[static_cast<size_t>(i)];
          if ((inside && phi > 0.0) || (!inside && phi < 0.0)) {
            phis[static_cast<size_t>(i)] = -phi;
            grads[3 * i + 0] = -grads[3 * i + 0];
            grads[3 * i + 1] = -grads[3 * i + 1];
            grads[3 * i + 2] = -grads[3 * i + 2];
          }
        }
      }
    }

    std::vector<Vec3> snapped_pos(n_wall);
    std::vector<uint8_t> was_snapped(n_wall, 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
    for (int64_t i = 0; i < static_cast<int64_t>(n_wall); ++i) {
      int64_t v = wall_verts[static_cast<size_t>(i)];
      Vec3 pos = node_coords[static_cast<size_t>(v)];
      double phi = phis[static_cast<size_t>(i)];
      Vec3 grad(grads[3 * i + 0], grads[3 * i + 1], grads[3 * i + 2]);

      if (grad.norm2() > 1e-12) {
        Vec3 target = pos - (phi / grad.norm2()) * grad;
        Vec3 disp = target - pos;

        double best_lambda = 0.0;
        for (double lambda : {1.0, 0.5, 0.25, 0.125}) {
          Vec3 test_pos = pos + lambda * disp;
          bool valid = true;

          auto it_cells = node_to_cells.find(v);
          if (it_cells != node_to_cells.end()) {
            for (int64_t cell_idx : it_cells->second) {
              const OctreeNode& onode = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(cell_idx)])];
              int cx = (pos.x >= onode.center.x) ? 1 : 0;
              int cy = (pos.y >= onode.center.y) ? 1 : 0;
              int cz = (pos.z >= onode.center.z) ? 1 : 0;

              Vec3 v_corner(cx == 0 ? onode.bmin.x : onode.bmax.x,
                            cy == 0 ? onode.bmin.y : onode.bmax.y,
                            cz == 0 ? onode.bmin.z : onode.bmax.z);

              if (std::abs(pos.x - v_corner.x) > 1e-4 * onode.size.x ||
                  std::abs(pos.y - v_corner.y) > 1e-4 * onode.size.y ||
                  std::abs(pos.z - v_corner.z) > 1e-4 * onode.size.z) {
                continue;
              }

              Vec3 vx = v_corner + Vec3((1 - 2 * cx) * onode.size.x, 0, 0);
              Vec3 vy = v_corner + Vec3(0, (1 - 2 * cy) * onode.size.y, 0);
              Vec3 vz = v_corner + Vec3(0, 0, (1 - 2 * cz) * onode.size.z);

              int s = (1 - 2 * cx) * (1 - 2 * cy) * (1 - 2 * cz);
              Vec3 v1 = (s > 0) ? vx : vy;
              Vec3 v2 = (s > 0) ? vy : vx;
              Vec3 v3 = vz;

              double tet_energy = computeTetAMIPS(test_pos, v1, v2, v3);
              if (tet_energy > options.amips_max) {
                valid = false;
                break;
              }
            }
          }

          if (valid) {
            auto it_faces = node_to_faces.find(v);
            if (it_faces != node_to_faces.end()) {
              for (int64_t f_idx : it_faces->second) {
                const auto& mf = mosaic_faces[static_cast<size_t>(f_idx)];
                auto [sf_new, cf_new] = computePolygonNormalAndCentroidReplaced(
                    mf.nodes, node_coords, v, test_pos);

                int64_t o = mf.owner;
                if (o >= 0) {
                  const OctreeNode& onode = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(o)])];
                  Vec3 ref_c = cell_approx_center[static_cast<size_t>(o)];
                  double pyr_vol = dot(cf_new - ref_c, sf_new) / 3.0;
                  if (pyr_vol <= 1e-15 * onode.v_bg) {
                    valid = false;
                    break;
                  }
                }

                int64_t nb = mf.neighbour;
                if (nb >= 0) {
                  const OctreeNode& onode = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(nb)])];
                  Vec3 ref_c = cell_approx_center[static_cast<size_t>(nb)];
                  double pyr_vol = dot(cf_new - ref_c, -sf_new) / 3.0;
                  if (pyr_vol <= 1e-15 * onode.v_bg) {
                    valid = false;
                    break;
                  }
                }
              }
            }
          }

          if (valid) {
            best_lambda = lambda;
            break;
          }
        }

        if (best_lambda > 0.0) {
          snapped_pos[static_cast<size_t>(i)] = pos + best_lambda * disp;
          was_snapped[static_cast<size_t>(i)] = 1;
        }
      }
    }

    std::vector<Vec3> orig_wall_coords(n_wall);
    std::unordered_map<int64_t, size_t> wall_vert_to_idx;
    wall_vert_to_idx.reserve(n_wall);
    for (size_t i = 0; i < n_wall; ++i) {
      orig_wall_coords[i] = node_coords[static_cast<size_t>(wall_verts[i])];
      wall_vert_to_idx[wall_verts[i]] = i;
      if (was_snapped[i]) {
        node_coords[static_cast<size_t>(wall_verts[i])] = snapped_pos[i];
      }
    }

    // Post-snap Hex8 Jacobian rollback guard: prevent simultaneous wall snapping from inverting any HEXAHEDRON
    for (size_t guard_pass = 0; guard_pass <= n_wall; ++guard_pass) {
      std::vector<uint8_t> revert_wall(n_wall, 0);
      bool any_reverted = false;
      for (int64_t c = 0; c < n_active_cells; ++c) {
        if (cell_types[static_cast<size_t>(c)] != CellType::HEXAHEDRON) continue;
        const auto& hc = hex_corners[static_cast<size_t>(c)];
        std::vector<int64_t> hnodes(hc.begin(), hc.end());
        double j_val = computeCellJENS(node_coords, hnodes, CellType::HEXAHEDRON);
        bool folded = false;
        if (j_val > 0.0) {
          for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
            auto [fn, fc_tmp] = computePolygonNormalAndCentroid(mosaic_faces[static_cast<size_t>(f_idx)].nodes, node_coords);
            (void)fc_tmp;
            if (isFaceFolded(mosaic_faces[static_cast<size_t>(f_idx)].nodes, node_coords, fn)) {
              folded = true;
              break;
            }
          }
        }
        if (j_val <= 0.0 || folded) {
          for (int64_t vid : hc) {
            auto it = wall_vert_to_idx.find(vid);
            if (it != wall_vert_to_idx.end() && was_snapped[it->second]) {
              revert_wall[it->second] = 1;
              any_reverted = true;
            }
          }
        }
      }
      if (!any_reverted) break;
      for (size_t i = 0; i < n_wall; ++i) {
        if (revert_wall[i]) {
          node_coords[static_cast<size_t>(wall_verts[i])] = orig_wall_coords[i];
          was_snapped[i] = 0;
        }
      }
    }
  }

  // 9. Recompute Face Normals and Areas after Snapping (OpenMP)
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < static_cast<int64_t>(mosaic_faces.size()); ++f) {
    auto [normal, centre] = computePolygonNormalAndCentroid(mosaic_faces[f].nodes, node_coords);
    mosaic_faces[f].normal_owner = normal;
    mosaic_faces[f].centre = centre;
    mosaic_faces[f].is_folded = isFaceFolded(mosaic_faces[f].nodes, node_coords, normal);
  }

  // 9a. Split non-planar / warped or folded polygonal faces (nv >= 4) into strictly planar triangles
  //     so Green-Gauss linear-field exactness holds to machine precision on cut cells.
  {
    size_t orig_n_faces = mosaic_faces.size();
    for (size_t f = 0; f < orig_n_faces; ++f) {
      size_t nv = mosaic_faces[f].nodes.size();
      if (nv < 4) continue;
      Vec3 fn = mosaic_faces[f].normal_owner;
      Vec3 fc = mosaic_faces[f].centre;
      double area = fn.norm();
      if (area <= 1e-24) continue;
      Vec3 unit_n = fn / area;
      double max_dev = 0.0;
      for (int64_t vid : mosaic_faces[f].nodes) {
        max_dev = std::max(max_dev, std::abs(dot(node_coords[static_cast<size_t>(vid)] - fc, unit_n)));
      }
      if (!mosaic_faces[f].is_folded && max_dev <= 1e-11 * std::sqrt(area)) {
        continue;
      }

      MosaicFace orig = mosaic_faces[f];
      auto make_tri_face = [&](int64_t a, int64_t b, int64_t c) -> MosaicFace {
        MosaicFace tf;
        tf.nodes = {a, b, c};
        tf.owner = orig.owner;
        tf.neighbour = orig.neighbour;
        tf.patch_name = orig.patch_name;
        tf.is_farfield = orig.is_farfield;
        tf.is_cut_face = orig.is_cut_face;
        tf.is_folded = false;
        const Vec3& pa = node_coords[static_cast<size_t>(a)];
        const Vec3& pb = node_coords[static_cast<size_t>(b)];
        const Vec3& pc = node_coords[static_cast<size_t>(c)];
        tf.normal_owner = cross(pb - pa, pc - pa) * 0.5;
        tf.centre = (pa + pb + pc) / 3.0;
        return tf;
      };

      if (nv == 4) {
        int64_t n0 = orig.nodes[0], n1 = orig.nodes[1], n2 = orig.nodes[2], n3 = orig.nodes[3];
        MosaicFace t02_a = make_tri_face(n0, n1, n2);
        MosaicFace t02_b = make_tri_face(n0, n2, n3);
        double min02 = std::min(dot(t02_a.normal_owner, unit_n), dot(t02_b.normal_owner, unit_n));

        MosaicFace t13_a = make_tri_face(n1, n2, n3);
        MosaicFace t13_b = make_tri_face(n1, n3, n0);
        double min13 = std::min(dot(t13_a.normal_owner, unit_n), dot(t13_b.normal_owner, unit_n));

        if (min13 > min02) {
          mosaic_faces[f] = std::move(t13_a);
          int64_t new_f = static_cast<int64_t>(mosaic_faces.size());
          mosaic_faces.push_back(std::move(t13_b));
          if (orig.owner >= 0) cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        } else {
          mosaic_faces[f] = std::move(t02_a);
          int64_t new_f = static_cast<int64_t>(mosaic_faces.size());
          mosaic_faces.push_back(std::move(t02_b));
          if (orig.owner >= 0) cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        }
      } else {
        int64_t c_vid = static_cast<int64_t>(node_coords.size());
        node_coords.push_back(fc);
        mosaic_faces[f] = make_tri_face(orig.nodes[0], orig.nodes[1], c_vid);
        for (size_t i = 1; i < nv; ++i) {
          MosaicFace extra = make_tri_face(orig.nodes[i], orig.nodes[(i + 1) % nv], c_vid);
          int64_t new_f = static_cast<int64_t>(mosaic_faces.size());
          mosaic_faces.push_back(std::move(extra));
          if (orig.owner >= 0) cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        }
      }

      if (orig.owner >= 0) cell_types[static_cast<size_t>(orig.owner)] = CellType::POLYHEDRON;
      if (orig.neighbour >= 0) cell_types[static_cast<size_t>(orig.neighbour)] = CellType::POLYHEDRON;
    }
  }

  std::vector<Vec3> cell_approx_center(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    Vec3 c_approx(0, 0, 0);
    int64_t count = 0;
    for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
      c_approx += mosaic_faces[static_cast<size_t>(f_idx)].centre;
      ++count;
    }
    cell_approx_center[static_cast<size_t>(c)] =
        (count > 0) ? (c_approx / static_cast<double>(count))
                    : nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(c)])].center;
  }

  // 10. Compute Cell Volumes & Centroids via reference-point pyramid decomposition (OpenMP)
  std::vector<double> cell_vols(static_cast<size_t>(n_active_cells), 0.0);
  std::vector<Vec3> cell_cents(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    Vec3 c_approx = cell_approx_center[static_cast<size_t>(c)];
    double vol_sum = 0.0;
    Vec3 cent_sum(0, 0, 0);

    for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
      const auto& mf = mosaic_faces[static_cast<size_t>(f_idx)];
      double sign = (mf.owner == c) ? 1.0 : -1.0;
      Vec3 sf_out = mf.normal_owner * sign;
      double pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0;
      vol_sum += pyr_vol;
      Vec3 pyr_c = mf.centre * 0.75 + c_approx * 0.25;
      cent_sum += pyr_c * pyr_vol;
    }

    cell_vols[static_cast<size_t>(c)] = vol_sum;
    if (std::abs(vol_sum) > 1e-15) {
      cell_cents[static_cast<size_t>(c)] = cent_sum / vol_sum;
    } else {
      cell_cents[static_cast<size_t>(c)] = c_approx;
      cell_vols[static_cast<size_t>(c)] = 0.0;
    }
  }

  // 11. Small-Cell Topological Merging (V_cut < 0.20 V_bg) along grad(phi)
  std::vector<std::vector<int64_t>> cell_verts(static_cast<size_t>(n_active_cells));
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
      for (int64_t v : mosaic_faces[static_cast<size_t>(f_idx)].nodes) {
        cell_verts[static_cast<size_t>(c)].push_back(v);
      }
    }
    std::sort(cell_verts[static_cast<size_t>(c)].begin(), cell_verts[static_cast<size_t>(c)].end());
    cell_verts[static_cast<size_t>(c)].erase(
        std::unique(cell_verts[static_cast<size_t>(c)].begin(), cell_verts[static_cast<size_t>(c)].end()),
        cell_verts[static_cast<size_t>(c)].end());
  }

  if (options.merge_small_cells) {
    for (int64_t c = 0; c < n_active_cells; ++c) {
      double v_bg = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(c)])].v_bg;
      double alpha = cell_vols[static_cast<size_t>(c)] / (v_bg + 1e-30);

      if (alpha < options.small_cell_ratio && alpha > 0.0) {
        Vec3 grad = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(c)])].grad_c;
        int64_t best_nb = -1;
        double max_align = -1e30;

        for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
          const auto& mf = mosaic_faces[static_cast<size_t>(f_idx)];
          int64_t other = (mf.owner == c) ? mf.neighbour : mf.owner;
          if (other >= 0 && other < n_active_cells &&
              cell_vols[static_cast<size_t>(other)] > 1e-15) {
            Vec3 dir_nb = (cell_cents[static_cast<size_t>(other)] - cell_cents[static_cast<size_t>(c)]).normalized();
            double align = dot(dir_nb, grad);
            if (align > max_align) {
              max_align = align;
              best_nb = other;
            }
          }
        }

        if (best_nb >= 0) {
          double orig_nb_vol = cell_vols[static_cast<size_t>(best_nb)];
          cell_vols[static_cast<size_t>(best_nb)] += cell_vols[static_cast<size_t>(c)];
          cell_cents[static_cast<size_t>(best_nb)] =
              (cell_cents[static_cast<size_t>(best_nb)] * orig_nb_vol +
               cell_cents[static_cast<size_t>(c)] * cell_vols[static_cast<size_t>(c)]) /
              (cell_vols[static_cast<size_t>(best_nb)] + 1e-30);
          cell_types[static_cast<size_t>(best_nb)] = CellType::POLYHEDRON;

          cell_verts[static_cast<size_t>(best_nb)].insert(
              cell_verts[static_cast<size_t>(best_nb)].end(),
              cell_verts[static_cast<size_t>(c)].begin(),
              cell_verts[static_cast<size_t>(c)].end());
          std::sort(cell_verts[static_cast<size_t>(best_nb)].begin(),
                    cell_verts[static_cast<size_t>(best_nb)].end());
          cell_verts[static_cast<size_t>(best_nb)].erase(
              std::unique(cell_verts[static_cast<size_t>(best_nb)].begin(),
                          cell_verts[static_cast<size_t>(best_nb)].end()),
              cell_verts[static_cast<size_t>(best_nb)].end());

          for (int64_t f_idx : cell_to_faces[static_cast<size_t>(c)]) {
            auto& mf = mosaic_faces[static_cast<size_t>(f_idx)];
            if (mf.owner == c && mf.neighbour == best_nb) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == best_nb && mf.neighbour == c) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == c) {
              mf.owner = best_nb;
              cell_to_faces[static_cast<size_t>(best_nb)].push_back(f_idx);
            } else if (mf.neighbour == c) {
              mf.neighbour = best_nb;
              cell_to_faces[static_cast<size_t>(best_nb)].push_back(f_idx);
            }
          }

          cell_vols[static_cast<size_t>(c)] = 0.0;
        }
      }
    }
  }

  std::vector<int64_t> old_to_new_cell(static_cast<size_t>(n_active_cells), -1);
  std::vector<int64_t> new_to_old_active;
  new_to_old_active.reserve(static_cast<size_t>(n_active_cells));
  int64_t final_n_cells = 0;
  for (int64_t c = 0; c < n_active_cells; ++c) {
    if (cell_vols[static_cast<size_t>(c)] > 1e-15) {
      old_to_new_cell[static_cast<size_t>(c)] = final_n_cells++;
      new_to_old_active.push_back(c);
    }
  }
  if (final_n_cells == 0) {
    return -1;
  }

  // 12. Separate Internal Faces and Boundary Faces (Sort-based Deduplication, no std::map)
  std::vector<MosaicFace> raw_internal_faces;
  raw_internal_faces.reserve(mosaic_faces.size());
  std::vector<MosaicFace> final_boundary_faces;
  final_boundary_faces.reserve(mosaic_faces.size() / 4 + 64);

  for (auto& mf : mosaic_faces) {
    if (mf.owner < 0 && mf.neighbour < 0) continue;

    int64_t new_o = (mf.owner >= 0) ? old_to_new_cell[static_cast<size_t>(mf.owner)] : -1;
    int64_t new_nb = (mf.neighbour >= 0) ? old_to_new_cell[static_cast<size_t>(mf.neighbour)] : -1;

    if (new_o >= 0 && new_nb >= 0) {
      if (new_o == new_nb) {
        continue;
      }
      if (new_o > new_nb) {
        std::swap(new_o, new_nb);
        mf.normal_owner = -mf.normal_owner;
        std::reverse(mf.nodes.begin(), mf.nodes.end());
      }
      mf.owner = new_o;
      mf.neighbour = new_nb;
      raw_internal_faces.push_back(std::move(mf));
    } else if (new_o >= 0 && new_nb < 0) {
      mf.owner = new_o;
      mf.neighbour = -1;
      final_boundary_faces.push_back(std::move(mf));
    } else if (new_nb >= 0 && new_o < 0) {
      mf.owner = new_nb;
      mf.neighbour = -1;
      mf.normal_owner = -mf.normal_owner;
      std::reverse(mf.nodes.begin(), mf.nodes.end());
      final_boundary_faces.push_back(std::move(mf));
    }
  }

  size_t n_raw_int = raw_internal_faces.size();
  std::vector<size_t> raw_order(n_raw_int);
  for (size_t i = 0; i < n_raw_int; ++i) raw_order[i] = i;
  std::sort(raw_order.begin(), raw_order.end(),
            [&](size_t a, size_t b) {
              if (raw_internal_faces[a].owner != raw_internal_faces[b].owner) {
                return raw_internal_faces[a].owner < raw_internal_faces[b].owner;
              }
              return raw_internal_faces[a].neighbour < raw_internal_faces[b].neighbour;
            });

  std::vector<MosaicFace> final_internal_faces;
  final_internal_faces.reserve(n_raw_int);
  for (size_t idx = 0; idx < n_raw_int;) {
    size_t first_i = raw_order[idx];
    size_t run_end = idx + 1;
    while (run_end < n_raw_int &&
           raw_internal_faces[raw_order[run_end]].owner == raw_internal_faces[first_i].owner &&
           raw_internal_faces[raw_order[run_end]].neighbour == raw_internal_faces[first_i].neighbour) {
      ++run_end;
    }
    if (run_end - idx == 1) {
      final_internal_faces.push_back(raw_internal_faces[first_i]);
    } else {
      bool all_coplanar = true;
      Vec3 n0 = raw_internal_faces[first_i].normal_owner;
      Vec3 c0 = raw_internal_faces[first_i].centre;
      double a0 = n0.norm();
      if (a0 <= 1e-24) {
        all_coplanar = false;
      } else {
        Vec3 u0 = n0 / a0;
        double plane_tol = 1e-11 * std::sqrt(a0 * static_cast<double>(run_end - idx));
        for (size_t g = idx; g < run_end && all_coplanar; ++g) {
          const auto& fk = raw_internal_faces[raw_order[g]];
          double ak = fk.normal_owner.norm();
          if (ak <= 1e-24 || dot(fk.normal_owner / ak, u0) < 1.0 - 1e-10) {
            all_coplanar = false;
            break;
          }
          for (int64_t vid : fk.nodes) {
            if (std::abs(dot(node_coords[static_cast<size_t>(vid)] - c0, u0)) > plane_tol) {
              all_coplanar = false;
              break;
            }
          }
        }
      }

      if (!all_coplanar) {
        for (size_t g = idx; g < run_end; ++g) {
          final_internal_faces.push_back(raw_internal_faces[raw_order[g]]);
        }
      } else {
        MosaicFace merged;
      merged.owner = raw_internal_faces[first_i].owner;
      merged.neighbour = raw_internal_faces[first_i].neighbour;
      merged.patch_name = "internal";
      merged.is_farfield = false;
      merged.is_cut_face = false;
      merged.is_folded = false;

      Vec3 sum_normal(0, 0, 0);
      Vec3 sum_centre(0, 0, 0);
      double sum_area = 0.0;

      std::map<std::pair<int64_t, int64_t>, int> edge_counts;
      for (size_t g = idx; g < run_end; ++g) {
        const auto& f = raw_internal_faces[raw_order[g]];
        sum_normal += f.normal_owner;
        double a = f.normal_owner.norm();
        sum_centre += f.centre * a;
        sum_area += a;
        if (f.is_folded) merged.is_folded = true;

        size_t nv = f.nodes.size();
        for (size_t i = 0; i < nv; ++i) {
          int64_t u = f.nodes[i];
          int64_t v = f.nodes[(i + 1) % nv];
          edge_counts[{u, v}]++;
        }
      }

      merged.normal_owner = sum_normal;
      if (sum_area > 1e-15) {
        merged.centre = sum_centre / sum_area;
      } else {
        merged.centre = raw_internal_faces[first_i].centre;
      }

      std::vector<std::pair<int64_t, int64_t>> perimeter_edges;
      for (const auto& [edge, count] : edge_counts) {
        int64_t u = edge.first;
        int64_t v = edge.second;
        auto it_rev = edge_counts.find({v, u});
        int rev_count = (it_rev != edge_counts.end()) ? it_rev->second : 0;
        if (count > rev_count) {
          for (int k = 0; k < count - rev_count; ++k) {
            perimeter_edges.push_back({u, v});
          }
        }
      }

      std::vector<int64_t> cycle;
      if (!perimeter_edges.empty()) {
        std::unordered_map<int64_t, int64_t> next_node;
        for (const auto& e : perimeter_edges) {
          next_node[e.first] = e.second;
        }
        int64_t start = perimeter_edges[0].first;
        int64_t curr = start;
        std::unordered_set<int64_t> visited;
        while (visited.insert(curr).second) {
          cycle.push_back(curr);
          auto it = next_node.find(curr);
          if (it == next_node.end()) break;
          curr = it->second;
          if (curr == start) break;
        }
      }

      if (cycle.size() >= 3) {
        merged.nodes = std::move(cycle);
      } else {
        std::set<int64_t> all_n;
        for (size_t g = idx; g < run_end; ++g) {
          for (int64_t v : raw_internal_faces[raw_order[g]].nodes) all_n.insert(v);
        }
        merged.nodes.assign(all_n.begin(), all_n.end());
      }

        final_internal_faces.push_back(std::move(merged));
      }
    }
    idx = run_end;
  }

  // Native Lossless RCM Reordering (before CSR cell_nodes assembly so Hex8 order is preserved)
  if (options.enable_rcm && final_n_cells > 1) {
    std::vector<int64_t> perm = computeFastRCM(final_n_cells, final_internal_faces);
    std::vector<int64_t> inv_perm(static_cast<size_t>(final_n_cells));
    std::vector<int64_t> rcm_to_old_active(static_cast<size_t>(final_n_cells));
    for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
      int64_t old_final_c = perm[static_cast<size_t>(new_c)];
      inv_perm[static_cast<size_t>(old_final_c)] = new_c;
      rcm_to_old_active[static_cast<size_t>(new_c)] = new_to_old_active[static_cast<size_t>(old_final_c)];
    }
    new_to_old_active.swap(rcm_to_old_active);

    // Build face-permutation indices instead of sorting MosaicFace structs directly
    // (avoiding MSVC C++20 std::sort move-assignment optimization issues with std::string)
    size_t n_int_f = final_internal_faces.size();
    std::vector<size_t> f_order(n_int_f);
    for (size_t f = 0; f < n_int_f; ++f) {
      MosaicFace& mf = final_internal_faces[f];
      int64_t o = inv_perm[static_cast<size_t>(mf.owner)];
      int64_t nb = inv_perm[static_cast<size_t>(mf.neighbour)];
      if (o > nb) {
        std::swap(o, nb);
        mf.normal_owner = -mf.normal_owner;
        std::reverse(mf.nodes.begin(), mf.nodes.end());
      }
      mf.owner = o;
      mf.neighbour = nb;
      f_order[f] = f;
    }

    std::sort(f_order.begin(), f_order.end(),
              [&](size_t a, size_t b) {
                if (final_internal_faces[a].owner != final_internal_faces[b].owner) {
                  return final_internal_faces[a].owner < final_internal_faces[b].owner;
                }
                return final_internal_faces[a].neighbour < final_internal_faces[b].neighbour;
              });

    std::vector<MosaicFace> sorted_int_faces;
    sorted_int_faces.reserve(n_int_f);
    for (size_t idx_f : f_order) {
      sorted_int_faces.push_back(final_internal_faces[idx_f]);
    }
    final_internal_faces.swap(sorted_int_faces);

    for (size_t f = 0; f < final_boundary_faces.size(); ++f) {
      MosaicFace& mf = final_boundary_faces[f];
      mf.owner = inv_perm[static_cast<size_t>(mf.owner)];
    }
  }

  size_t n_bnd_f = final_boundary_faces.size();
  std::vector<size_t> bnd_order(n_bnd_f);
  for (size_t f = 0; f < n_bnd_f; ++f) {
    if (final_boundary_faces[f].patch_name.empty()) {
      final_boundary_faces[f].patch_name = "wall";
    }
    bnd_order[f] = f;
  }
  std::stable_sort(bnd_order.begin(), bnd_order.end(),
                   [&](size_t a, size_t b) {
                     return final_boundary_faces[a].patch_name < final_boundary_faces[b].patch_name;
                   });
  std::vector<MosaicFace> sorted_bnd_faces;
  sorted_bnd_faces.reserve(n_bnd_f);
  for (size_t idx_f : bnd_order) {
    sorted_bnd_faces.push_back(final_boundary_faces[idx_f]);
  }
  final_boundary_faces.swap(sorted_bnd_faces);

  std::vector<MosaicFace> all_faces;
  all_faces.reserve(final_internal_faces.size() + final_boundary_faces.size());
  for (auto& f : final_internal_faces) all_faces.push_back(std::move(f));
  for (auto& f : final_boundary_faces) all_faces.push_back(std::move(f));

  int64_t total_faces = static_cast<int64_t>(all_faces.size());
  int64_t n_internal = static_cast<int64_t>(final_internal_faces.size());

  // 13. Populate Output NativeMeshData Arrays (OpenMP)
  mesh.points.resize(3 * node_coords.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t i = 0; i < static_cast<int64_t>(node_coords.size()); ++i) {
    mesh.points[3 * i + 0] = node_coords[static_cast<size_t>(i)].x;
    mesh.points[3 * i + 1] = node_coords[static_cast<size_t>(i)].y;
    mesh.points[3 * i + 2] = node_coords[static_cast<size_t>(i)].z;
  }

  mesh.cell_centres.resize(3 * final_n_cells);
  mesh.cell_volumes.resize(final_n_cells);
  mesh.cell_types.resize(final_n_cells);
  mesh.cell_regions.resize(final_n_cells, 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    int64_t old_c = new_to_old_active[static_cast<size_t>(new_c)];
    mesh.cell_centres[3 * new_c + 0] = cell_cents[static_cast<size_t>(old_c)].x;
    mesh.cell_centres[3 * new_c + 1] = cell_cents[static_cast<size_t>(old_c)].y;
    mesh.cell_centres[3 * new_c + 2] = cell_cents[static_cast<size_t>(old_c)].z;
    mesh.cell_volumes[static_cast<size_t>(new_c)] = cell_vols[static_cast<size_t>(old_c)];
    mesh.cell_types[static_cast<size_t>(new_c)] = static_cast<int32_t>(cell_types[static_cast<size_t>(old_c)]);
    mesh.cell_regions[static_cast<size_t>(new_c)] =
        nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(old_c)])].region;
  }

  mesh.owner.resize(total_faces);
  mesh.neighbour.resize(total_faces);
  mesh.face_areas.resize(3 * total_faces);
  mesh.face_centres.resize(3 * total_faces);
  mesh.delta_f.resize(3 * total_faces);
  mesh.k_f.resize(3 * total_faces);
  mesh.non_ortho.resize(total_faces);
  mesh.skewness.resize(total_faces);
  mesh.stab_mask.resize(total_faces);
  mesh.m_skew.assign(3 * total_faces, 0.0);
  mesh.k_f_comp.assign(3 * total_faces, 0.0);
  mesh.beta_upwind.assign(total_faces, 1.0);
  mesh.omega_cell.assign(final_n_cells, 1.0);
  mesh.wsrd_alpha.assign(final_n_cells, 1.0);

  // CSR Face Nodes via prefix sum
  mesh.face_offsets.resize(total_faces + 1, 0);
  for (int64_t f = 0; f < total_faces; ++f) {
    mesh.face_offsets[static_cast<size_t>(f + 1)] =
        mesh.face_offsets[static_cast<size_t>(f)] + static_cast<int64_t>(all_faces[static_cast<size_t>(f)].nodes.size());
  }
  mesh.face_nodes.resize(static_cast<size_t>(mesh.face_offsets[static_cast<size_t>(total_faces)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t off = mesh.face_offsets[static_cast<size_t>(f)];
    const auto& fnodes = all_faces[static_cast<size_t>(f)].nodes;
    for (size_t k = 0; k < fnodes.size(); ++k) {
      mesh.face_nodes[static_cast<size_t>(off) + k] = fnodes[k];
    }

    mesh.owner[static_cast<size_t>(f)] = all_faces[static_cast<size_t>(f)].owner;
    mesh.neighbour[static_cast<size_t>(f)] = all_faces[static_cast<size_t>(f)].neighbour;

    Vec3 sf = all_faces[static_cast<size_t>(f)].normal_owner;
    Vec3 cf = all_faces[static_cast<size_t>(f)].centre;

    mesh.face_areas[3 * f + 0] = sf.x;
    mesh.face_areas[3 * f + 1] = sf.y;
    mesh.face_areas[3 * f + 2] = sf.z;

    mesh.face_centres[3 * f + 0] = cf.x;
    mesh.face_centres[3 * f + 1] = cf.y;
    mesh.face_centres[3 * f + 2] = cf.z;

    int64_t o = mesh.owner[static_cast<size_t>(f)];
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
    Vec3 co(mesh.cell_centres[3 * o + 0], mesh.cell_centres[3 * o + 1], mesh.cell_centres[3 * o + 2]);

    if (nb >= 0) {
      Vec3 cnb(mesh.cell_centres[3 * nb + 0], mesh.cell_centres[3 * nb + 1], mesh.cell_centres[3 * nb + 2]);
      Vec3 df = cnb - co;
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

      double cos_theta = 1.0;
      if (sf_norm > 1e-15 && df_norm > 1e-15) {
        cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0);
      }
      double theta_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
      mesh.non_ortho[static_cast<size_t>(f)] = theta_deg;

      double skew = 0.0;
      Vec3 x_int = cf;
      if (sf_norm > 1e-15 && df_norm > 1e-15) {
        Vec3 nf = sf / sf_norm;
        double df_dot_nf = dot(df, nf);
        if (std::abs(df_dot_nf) > 1e-15) {
          double t = dot(cf - co, nf) / df_dot_nf;
          x_int = co + df * t;
          skew = (cf - x_int).norm() / df_norm;
        }
      }
      mesh.skewness[static_cast<size_t>(f)] = skew;
      Vec3 m_vec = cf - x_int;
      mesh.m_skew[3 * f + 0] = m_vec.x;
      mesh.m_skew[3 * f + 1] = m_vec.y;
      mesh.m_skew[3 * f + 2] = m_vec.z;

      double k_norm = k.norm();
      double factor = std::min(1.0, (1.5 * delta.norm()) / (k_norm + 1e-15));
      Vec3 k_comp = k * factor;
      mesh.k_f_comp[3 * f + 0] = k_comp.x;
      mesh.k_f_comp[3 * f + 1] = k_comp.y;
      mesh.k_f_comp[3 * f + 2] = k_comp.z;

      if (sf_dot_df <= 0.0) {
        mesh.stab_mask[static_cast<size_t>(f)] = 3;
        mesh.beta_upwind[static_cast<size_t>(f)] = 0.0;
      } else {
        double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
        double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
        mesh.beta_upwind[static_cast<size_t>(f)] = std::min(beta_ortho, beta_skew);
        mesh.stab_mask[static_cast<size_t>(f)] =
            (theta_deg >= 75.0 || skew >= 1.0) ? 3
            : ((theta_deg >= 65.0 || skew >= 0.7) ? 2
               : ((theta_deg >= 40.0 || skew >= 0.4) ? 1 : 0));
      }
    } else {
      Vec3 df = cf - co;
      mesh.delta_f[3 * f + 0] = df.x;
      mesh.delta_f[3 * f + 1] = df.y;
      mesh.delta_f[3 * f + 2] = df.z;
      mesh.k_f[3 * f + 0] = 0.0;
      mesh.k_f[3 * f + 1] = 0.0;
      mesh.k_f[3 * f + 2] = 0.0;
      mesh.non_ortho[static_cast<size_t>(f)] = 0.0;
      mesh.skewness[static_cast<size_t>(f)] = 0.0;
      mesh.stab_mask[static_cast<size_t>(f)] = (dot(sf, df) <= 0.0) ? 3 : 0;
      mesh.beta_upwind[static_cast<size_t>(f)] = (dot(sf, df) <= 0.0) ? 0.0 : 1.0;
    }
  }

  // Minimum face beta_upwind per cell -> omega_cell
  std::vector<double> min_beta_per_cell(static_cast<size_t>(final_n_cells), 1.0);
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t o = mesh.owner[static_cast<size_t>(f)];
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
    if (o >= 0 && o < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(o)] =
          std::min(min_beta_per_cell[static_cast<size_t>(o)], mesh.beta_upwind[static_cast<size_t>(f)]);
    }
    if (nb >= 0 && nb < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(nb)] =
          std::min(min_beta_per_cell[static_cast<size_t>(nb)], mesh.beta_upwind[static_cast<size_t>(f)]);
    }
  }
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.omega_cell[static_cast<size_t>(c)] = std::max(0.1, min_beta_per_cell[static_cast<size_t>(c)]);
  }

  // CSR Cell Nodes:
  // - CellType::HEXAHEDRON (12): preserve canonical VTK Hex8 (0..7) corner order WITHOUT std::sort!
  // - CellType::POLYHEDRON (42): union cell_verts and incident face nodes + std::sort + std::unique.
  std::vector<std::vector<int64_t>> cell_nodes_temp(static_cast<size_t>(final_n_cells));
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t o = mesh.owner[static_cast<size_t>(f)];
    if (mesh.cell_types[static_cast<size_t>(o)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      for (int64_t v : all_faces[static_cast<size_t>(f)].nodes) {
        cell_nodes_temp[static_cast<size_t>(o)].push_back(v);
      }
    }
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
    if (nb >= 0 && nb < final_n_cells &&
        mesh.cell_types[static_cast<size_t>(nb)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      for (int64_t v : all_faces[static_cast<size_t>(f)].nodes) {
        cell_nodes_temp[static_cast<size_t>(nb)].push_back(v);
      }
    }
  }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    int64_t old_c = new_to_old_active[static_cast<size_t>(new_c)];
    if (mesh.cell_types[static_cast<size_t>(new_c)] == static_cast<int32_t>(CellType::HEXAHEDRON)) {
      const auto& hc = hex_corners[static_cast<size_t>(old_c)];
      bool distinct = true;
      for (int i = 0; i < 8 && distinct; ++i) {
        for (int j = i + 1; j < 8; ++j) {
          if (hc[static_cast<size_t>(i)] == hc[static_cast<size_t>(j)]) {
            distinct = false;
            break;
          }
        }
      }
      if (distinct) {
        cell_nodes_temp[static_cast<size_t>(new_c)].assign(hc.begin(), hc.end());
      } else {
        mesh.cell_types[static_cast<size_t>(new_c)] = static_cast<int32_t>(CellType::POLYHEDRON);
        auto& cv = cell_nodes_temp[static_cast<size_t>(new_c)];
        cv.insert(cv.end(), cell_verts[static_cast<size_t>(old_c)].begin(), cell_verts[static_cast<size_t>(old_c)].end());
        std::sort(cv.begin(), cv.end());
        cv.erase(std::unique(cv.begin(), cv.end()), cv.end());
      }
    } else {
      auto& cv = cell_nodes_temp[static_cast<size_t>(new_c)];
      cv.insert(cv.end(), cell_verts[static_cast<size_t>(old_c)].begin(), cell_verts[static_cast<size_t>(old_c)].end());
      std::sort(cv.begin(), cv.end());
      cv.erase(std::unique(cv.begin(), cv.end()), cv.end());
    }
  }

  mesh.cell_offsets.resize(final_n_cells + 1, 0);
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.cell_offsets[static_cast<size_t>(c + 1)] =
        mesh.cell_offsets[static_cast<size_t>(c)] + static_cast<int64_t>(cell_nodes_temp[static_cast<size_t>(c)].size());
  }
  mesh.cell_nodes.resize(static_cast<size_t>(mesh.cell_offsets[static_cast<size_t>(final_n_cells)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < final_n_cells; ++c) {
    int64_t off = mesh.cell_offsets[static_cast<size_t>(c)];
    const auto& cn = cell_nodes_temp[static_cast<size_t>(c)];
    for (size_t k = 0; k < cn.size(); ++k) {
      mesh.cell_nodes[static_cast<size_t>(off) + k] = cn[k];
    }
  }

  // Aspect ratio & WSRD alpha per cell (OpenMP)
  mesh.aspect_ratio.resize(final_n_cells, 1.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    double min_e = 1e30;
    double max_e = 0.0;
    int64_t off0 = mesh.cell_offsets[static_cast<size_t>(new_c)];
    int64_t off1 = mesh.cell_offsets[static_cast<size_t>(new_c + 1)];
    int64_t nv = off1 - off0;
    for (int64_t i = 0; i < nv; ++i) {
      for (int64_t j = i + 1; j < nv; ++j) {
        int64_t v1 = mesh.cell_nodes[static_cast<size_t>(off0 + i)];
        int64_t v2 = mesh.cell_nodes[static_cast<size_t>(off0 + j)];
        Vec3 p1 = node_coords[static_cast<size_t>(v1)];
        Vec3 p2 = node_coords[static_cast<size_t>(v2)];
        double d = (p1 - p2).norm();
        if (d > 1e-15) {
          min_e = std::min(min_e, d);
          max_e = std::max(max_e, d);
        }
      }
    }
    if (min_e < 1e20 && min_e > 1e-15) {
      mesh.aspect_ratio[static_cast<size_t>(new_c)] = std::max(1.0, max_e / min_e);
    } else {
      mesh.aspect_ratio[static_cast<size_t>(new_c)] = 1.0;
    }

    int64_t old_c = new_to_old_active[static_cast<size_t>(new_c)];
    double v_bg = nodes[static_cast<size_t>(active_leaves[static_cast<size_t>(old_c)])].v_bg;
    double alpha = std::clamp(cell_vols[static_cast<size_t>(old_c)] / (v_bg + 1e-30), 0.0, 1.0);
    mesh.wsrd_alpha[static_cast<size_t>(new_c)] = alpha;
  }

  // 15. Contiguous Boundary Patch Assignment
  if (final_boundary_faces.size() > 0) {
    std::string current_patch = final_boundary_faces[0].patch_name;
    int64_t start_f = n_internal;
    int64_t count = 0;

    for (size_t i = 0; i < final_boundary_faces.size(); ++i) {
      if (final_boundary_faces[i].patch_name == current_patch) {
        count++;
      } else {
        BoundaryPatchC bp;
        snprintf(bp.name, sizeof(bp.name), "%s", current_patch.c_str());
        snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", current_patch.c_str());
        bp.start_face = start_f;
        bp.num_faces = count;
        bp.region_id = 0;
        mesh.patches.push_back(bp);

        current_patch = final_boundary_faces[i].patch_name;
        start_f = n_internal + static_cast<int64_t>(i);
        count = 1;
      }
    }

    if (count > 0) {
      BoundaryPatchC bp;
      snprintf(bp.name, sizeof(bp.name), "%s", current_patch.c_str());
      snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", current_patch.c_str());
      bp.start_face = start_f;
      bp.num_faces = count;
      bp.region_id = 0;
      mesh.patches.push_back(bp);
    }
  }

  mesh.topological_dim = 3;
  return 0;
}

}  // namespace cfd
