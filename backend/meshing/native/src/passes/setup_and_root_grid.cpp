#include "hexcore_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cfd::hexcore {

size_t getMaxOctreeNodes() {
#ifdef _MSC_VER
  char* env_val = nullptr;
  size_t env_len = 0;
  if (_dupenv_s(&env_val, &env_len, "CFD_MAX_OCTREE_NODES") == 0 && env_val != nullptr) {
    char* end_ptr = nullptr;
    unsigned long long parsed = std::strtoull(env_val, &end_ptr, 10);
    bool ok = (env_val[0] != '\0' && end_ptr != nullptr && *end_ptr == '\0');
    std::free(env_val);
    if (ok) {
      if (parsed == 0ULL) {
        return (std::numeric_limits<size_t>::max)();
      }
      return static_cast<size_t>(parsed);
    }
  }
#else
  const char* env_val = std::getenv("CFD_MAX_OCTREE_NODES");
  if (env_val != nullptr && env_val[0] != '\0') {
    char* end_ptr = nullptr;
    unsigned long long parsed = std::strtoull(env_val, &end_ptr, 10);
    if (end_ptr != nullptr && *end_ptr == '\0') {
      if (parsed == 0ULL) {
        return (std::numeric_limits<size_t>::max)();
      }
      return static_cast<size_t>(parsed);
    }
  }
#endif
#ifdef _WIN32
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    const uint64_t commit_budget = (static_cast<uint64_t>(ms.ullAvailPageFile) * 60ULL) / 100ULL;
    const uint64_t phys_budget = (static_cast<uint64_t>(ms.ullTotalPhys) * 125ULL) / 100ULL;
    const uint64_t avail_budget_bytes = (commit_budget < phys_budget) ? commit_budget : phys_budget;
    const uint64_t est_nodes = avail_budget_bytes / 1200ULL;
    constexpr uint64_t kMinFloorNodes = 500000ULL;
    return static_cast<size_t>((est_nodes > kMinFloorNodes) ? est_nodes : kMinFloorNodes);
  }
  return 500000ULL;
#else
  return 64000000ULL;
#endif
}

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

static int64_t findAxisIdx(const std::vector<double>& coords, double val, int64_t n) {
  if (n <= 1) return 0;
  auto it = std::upper_bound(coords.begin() + 1, coords.end() - 1, val);
  int64_t idx = static_cast<int64_t>(it - coords.begin()) - 1;
  return std::clamp<int64_t>(idx, 0, n - 1);
}

int64_t HexcoreState::findLeafAtPoint(const Vec3& p) const {
  if (p.x < dom_min.x || p.x > dom_max.x ||
      p.y < dom_min.y || p.y > dom_max.y ||
      p.z < dom_min.z || p.z > dom_max.z) {
    return -1;
  }
  int64_t i = findAxisIdx(x_coords, p.x, nx);
  int64_t j = findAxisIdx(y_coords, p.y, ny);
  int64_t k = findAxisIdx(z_coords, p.z, nz);
  int64_t curr = (k * ny + j) * nx + i;
  int64_t n_nodes_sz = static_cast<int64_t>(nodes.size());
  while (curr >= 0 && curr < n_nodes_sz && !nodes[static_cast<size_t>(curr)].is_leaf) {
    const OctreeNode& cur_node = nodes[static_cast<size_t>(curr)];
    const Vec3 c = cur_node.center();
    int cx = (p.x >= c.x) ? 1 : 0;
    int cy = (p.y >= c.y) ? 1 : 0;
    int cz = (p.z >= c.z) ? 1 : 0;
    int child_slot = cz * 4 + cy * 2 + cx;
    if (cur_node.first_child < 0) break;
    curr = cur_node.first_child + child_slot;
  }
  return (curr >= 0 && curr < n_nodes_sz && nodes[static_cast<size_t>(curr)].is_leaf) ? curr : -1;
}

bool HexcoreState::subdivideBatch(const std::vector<int64_t>& split_ids, std::vector<int64_t>& out_children) {
  const size_t n_split = split_ids.size();
  out_children.clear();
  if (n_split == 0) return true;
  if (max_octree_nodes < (std::numeric_limits<size_t>::max)()) {
    if (nodes.size() > max_octree_nodes || n_split > (max_octree_nodes - nodes.size()) / 8) {
      return false;
    }
  } else if (n_split > ((std::numeric_limits<size_t>::max)() - nodes.size()) / 8) {
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
    OctreeNode& pnode = nodes[static_cast<size_t>(parent_idx)];
    const int32_t parent_level = pnode.level;
    const Vec3 pmin = pnode.bmin;
    const Vec3 pmax = pnode.bmax;
    const Vec3 mid = (pmin + pmax) * 0.5;
    const size_t first_child_slot = old_n + static_cast<size_t>(s) * 8;
    pnode.first_child = static_cast<int64_t>(first_child_slot);
    pnode.is_leaf = 0;

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

    for (int c = 0; c < 8; ++c) {
      const size_t child_slot_idx = first_child_slot + static_cast<size_t>(c);
      const int64_t child_idx = static_cast<int64_t>(child_slot_idx);
      OctreeNode& child = nodes[child_slot_idx];
      child.bmin = octants[static_cast<size_t>(c)].first;
      child.bmax = octants[static_cast<size_t>(c)].second;
      child.first_child = -1;
      child.cell_index = -1;
      child.level = parent_level + 1;
      child.region = 0;
      child.is_leaf = 1;
      child.is_active = 0;
      out_children[static_cast<size_t>(s) * 8 + static_cast<size_t>(c)] = child_idx;
    }
  }
  return true;
}

int32_t passSetupAndRootGrid(
    HexcoreState& st,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    const double* farfield_bbox
) {
  Vec3 raw_s_min(1e30, 1e30, 1e30);
  Vec3 raw_s_max(-1e30, -1e30, -1e30);
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(surf_points[3 * i], surf_points[3 * i + 1], surf_points[3 * i + 2]);
    raw_s_min.x = std::min(raw_s_min.x, p.x);
    raw_s_min.y = std::min(raw_s_min.y, p.y);
    raw_s_min.z = std::min(raw_s_min.z, p.z);
    raw_s_max.x = std::max(raw_s_max.x, p.x);
    raw_s_max.y = std::max(raw_s_max.y, p.y);
    raw_s_max.z = std::max(raw_s_max.z, p.z);
  }

  // Shift geometry to local surface-midpoint origin for 10^6+ m translation invariance (AUD-031)
  st.origin_shift = (raw_s_min + raw_s_max) * 0.5;
  st.shifted_surf_points.resize(static_cast<size_t>(n_points) * 3);
  for (int64_t i = 0; i < n_points; ++i) {
    st.shifted_surf_points[static_cast<size_t>(3 * i + 0)] = surf_points[3 * i + 0] - st.origin_shift.x;
    st.shifted_surf_points[static_cast<size_t>(3 * i + 1)] = surf_points[3 * i + 1] - st.origin_shift.y;
    st.shifted_surf_points[static_cast<size_t>(3 * i + 2)] = surf_points[3 * i + 2] - st.origin_shift.z;
  }

  st.s_min = raw_s_min - st.origin_shift;
  st.s_max = raw_s_max - st.origin_shift;
  st.sdf = std::make_unique<MeshSDF>(st.shifted_surf_points.data(), n_points, surf_faces, n_faces);

  st.is_closed_surface = false;
  st.surf_signed_vol = 0.0;
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
      Vec3 v0(st.shifted_surf_points[static_cast<size_t>(3 * i0 + 0)] - st.s_min.x,
              st.shifted_surf_points[static_cast<size_t>(3 * i0 + 1)] - st.s_min.y,
              st.shifted_surf_points[static_cast<size_t>(3 * i0 + 2)] - st.s_min.z);
      Vec3 v1(st.shifted_surf_points[static_cast<size_t>(3 * i1 + 0)] - st.s_min.x,
              st.shifted_surf_points[static_cast<size_t>(3 * i1 + 1)] - st.s_min.y,
              st.shifted_surf_points[static_cast<size_t>(3 * i1 + 2)] - st.s_min.z);
      Vec3 v2(st.shifted_surf_points[static_cast<size_t>(3 * i2 + 0)] - st.s_min.x,
              st.shifted_surf_points[static_cast<size_t>(3 * i2 + 1)] - st.s_min.y,
              st.shifted_surf_points[static_cast<size_t>(3 * i2 + 2)] - st.s_min.z);
      st.surf_signed_vol += dot(v0, cross(v1, v2)) / 6.0;

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
      st.is_closed_surface = all_even;
    }
  }

  if (farfield_bbox) {
    st.dom_min = Vec3(farfield_bbox[0], farfield_bbox[1], farfield_bbox[2]) - st.origin_shift;
    st.dom_max = Vec3(farfield_bbox[3], farfield_bbox[4], farfield_bbox[5]) - st.origin_shift;
    if (st.dom_min.x >= st.dom_max.x || st.dom_min.y >= st.dom_max.y || st.dom_min.z >= st.dom_max.z) {
      return -1;
    }
  } else if (st.options.domain_mode == 0) {
    Vec3 span = st.s_max - st.s_min;
    double l_ref = std::max({span.x, span.y, span.z});
    if (l_ref < 1e-12) l_ref = 1.0;
    st.dom_min = Vec3(st.s_min.x - 5.0 * l_ref, st.s_min.y - 5.0 * l_ref, st.s_min.z - 5.0 * l_ref);
    st.dom_max = Vec3(st.s_max.x + 10.0 * l_ref, st.s_max.y + 5.0 * l_ref, st.s_max.z + 5.0 * l_ref);
  } else {
    st.dom_min = st.s_min;
    st.dom_max = st.s_max;
  }

  Vec3 dom_span = st.dom_max - st.dom_min;
  if (dom_span.x <= 1e-12 || dom_span.y <= 1e-12 || dom_span.z <= 1e-12) {
    return -1;
  }

  double h0 = st.options.h0;
  double growth_ratio = st.options.grading;
  if (growth_ratio <= 1.0) growth_ratio = 1.0 + growth_ratio;
  growth_ratio = std::clamp(growth_ratio, 1.05, 1.35);

  Vec3 surf_span = st.s_max - st.s_min;
  double l_ref = std::max({surf_span.x, surf_span.y, surf_span.z, 1e-12});
  double dom_max_span = std::max({dom_span.x, dom_span.y, dom_span.z});
  double dom_min_span = std::min({dom_span.x, dom_span.y, dom_span.z});

  st.effective_max_depth = std::clamp(st.options.max_depth, 0, 12);

  if (st.options.domain_mode == 0 && (dom_max_span / h0) > 20.0) {
    double h_target_far = std::max({st.options.h_max, 0.25 * l_ref, h0});
    int32_t refine_depth = 0;
    while (refine_depth < 10 && h0 * static_cast<double>(1LL << refine_depth) < h_target_far) {
      ++refine_depth;
    }
    while (refine_depth > 0 && h0 * static_cast<double>(1LL << refine_depth) > 0.5 * dom_min_span) {
      --refine_depth;
    }
    double h_root = h0 * static_cast<double>(1LL << refine_depth);
    st.effective_max_depth = (refine_depth > 0) ? refine_depth : std::clamp(st.options.max_depth, 0, 12);

    st.x_coords = buildGradedAxisCoords(st.dom_min.x, st.s_min.x, st.s_max.x, st.dom_max.x, h_root, growth_ratio, 8.0);
    st.y_coords = buildGradedAxisCoords(st.dom_min.y, st.s_min.y, st.s_max.y, st.dom_max.y, h_root, growth_ratio, 8.0);
    st.z_coords = buildGradedAxisCoords(st.dom_min.z, st.s_min.z, st.s_max.z, st.dom_max.z, h_root, growth_ratio, 8.0);
  } else if (st.options.domain_mode == 2 && (dom_max_span / h0) > 32.0) {
    int32_t refine_depth = 0;
    while (refine_depth < 8 && (dom_max_span / (h0 * static_cast<double>(1LL << refine_depth))) > 32.0) {
      ++refine_depth;
    }
    double h_root = h0 * static_cast<double>(1LL << refine_depth);
    st.effective_max_depth = std::clamp(st.options.max_depth + refine_depth, 0, 10);

    int64_t nx_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.x / h_root)));
    int64_t ny_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.y / h_root)));
    int64_t nz_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.z / h_root)));
    st.x_coords.resize(static_cast<size_t>(nx_u) + 1);
    st.y_coords.resize(static_cast<size_t>(ny_u) + 1);
    st.z_coords.resize(static_cast<size_t>(nz_u) + 1);
    for (int64_t i = 0; i <= nx_u; ++i) st.x_coords[static_cast<size_t>(i)] = st.dom_min.x + (dom_span.x * i) / nx_u;
    for (int64_t j = 0; j <= ny_u; ++j) st.y_coords[static_cast<size_t>(j)] = st.dom_min.y + (dom_span.y * j) / ny_u;
    for (int64_t k = 0; k <= nz_u; ++k) st.z_coords[static_cast<size_t>(k)] = st.dom_min.z + (dom_span.z * k) / nz_u;
  } else {
    int64_t nx_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.x / h0)));
    int64_t ny_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.y / h0)));
    int64_t nz_u = std::max<int64_t>(1, static_cast<int64_t>(std::llround(dom_span.z / h0)));
    st.x_coords.resize(static_cast<size_t>(nx_u) + 1);
    st.y_coords.resize(static_cast<size_t>(ny_u) + 1);
    st.z_coords.resize(static_cast<size_t>(nz_u) + 1);
    for (int64_t i = 0; i <= nx_u; ++i) st.x_coords[static_cast<size_t>(i)] = st.dom_min.x + (dom_span.x * i) / nx_u;
    for (int64_t j = 0; j <= ny_u; ++j) st.y_coords[static_cast<size_t>(j)] = st.dom_min.y + (dom_span.y * j) / ny_u;
    for (int64_t k = 0; k <= nz_u; ++k) st.z_coords[static_cast<size_t>(k)] = st.dom_min.z + (dom_span.z * k) / nz_u;
  }

  st.nx = static_cast<int64_t>(st.x_coords.size()) - 1;
  st.ny = static_cast<int64_t>(st.y_coords.size()) - 1;
  st.nz = static_cast<int64_t>(st.z_coords.size()) - 1;

  double min_root_h = 1e30;
  for (int64_t i = 0; i < st.nx; ++i) min_root_h = std::min(min_root_h, st.x_coords[static_cast<size_t>(i + 1)] - st.x_coords[static_cast<size_t>(i)]);
  for (int64_t j = 0; j < st.ny; ++j) min_root_h = std::min(min_root_h, st.y_coords[static_cast<size_t>(j + 1)] - st.y_coords[static_cast<size_t>(j)]);
  for (int64_t k = 0; k < st.nz; ++k) min_root_h = std::min(min_root_h, st.z_coords[static_cast<size_t>(k + 1)] - st.z_coords[static_cast<size_t>(k)]);

  double max_abs_coord = std::max({
      std::abs(st.dom_min.x), std::abs(st.dom_min.y), std::abs(st.dom_min.z),
      std::abs(st.dom_max.x), std::abs(st.dom_max.y), std::abs(st.dom_max.z)
  });
  double min_leaf_h = min_root_h / static_cast<double>(1LL << st.effective_max_depth);
  st.eps_probe = std::max(1e-4 * min_leaf_h, 1e-11 * std::max(1.0, max_abs_coord));
  st.quant_tol = std::max(1e-6 * min_leaf_h, 1e-12 * std::max(1.0, max_abs_coord));
  st.dom_tol = std::max(1e-7 * min_root_h, 1e-11 * std::max(1.0, max_abs_coord));

  st.max_octree_nodes = getMaxOctreeNodes();
  if (st.nx <= 0 || st.ny <= 0 || st.nz <= 0) {
    return -1;
  }
  const size_t sx = static_cast<size_t>(st.nx);
  const size_t sy = static_cast<size_t>(st.ny);
  const size_t sz = static_cast<size_t>(st.nz);
  if (sx > st.max_octree_nodes || sy > st.max_octree_nodes / sx) {
    return -12;
  }
  const size_t n_xy = sx * sy;
  if (sz > st.max_octree_nodes / n_xy) {
    return -12;
  }
  st.n_root = n_xy * sz;
  st.nodes.resize(st.n_root);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t idx = 0; idx < static_cast<int64_t>(st.n_root); ++idx) {
    int64_t i = idx % st.nx;
    int64_t rem = idx / st.nx;
    int64_t j = rem % st.ny;
    int64_t k = rem / st.ny;
    OctreeNode& node = st.nodes[static_cast<size_t>(idx)];
    node.bmin = Vec3(st.x_coords[static_cast<size_t>(i)], st.y_coords[static_cast<size_t>(j)], st.z_coords[static_cast<size_t>(k)]);
    node.bmax = Vec3(st.x_coords[static_cast<size_t>(i + 1)], st.y_coords[static_cast<size_t>(j + 1)], st.z_coords[static_cast<size_t>(k + 1)]);
    node.first_child = -1;
    node.cell_index = -1;
    node.level = 0;
    node.region = 0;
    node.is_leaf = 1;
    node.is_active = 0;
  }

  return 0;
}

}  // namespace cfd::hexcore
