#include "octree_hexcore.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
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

// Internal Octree Node representation
struct OctreeNode {
  Vec3 bmin;
  Vec3 bmax;
  Vec3 center;
  Vec3 size;
  int32_t level = 0;
  int32_t parent = -1;
  int32_t children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  bool is_leaf = true;
  bool is_active = false;
  int32_t region = 0;       // 0 = fluid, 1 = solid
  int32_t cell_index = -1;  // Indexed among active leaves
  double phi_c = 0.0;
  Vec3 grad_c = Vec3(0, 0, 1);
  double v_bg = 0.0;
};

// Temporary Face representation during Mosaic assembly
struct MosaicFace {
  std::vector<int64_t> nodes;
  int64_t owner = -1;
  int64_t neighbour = -1;
  Vec3 normal_owner;
  Vec3 centre;
  std::string patch_name;
  bool is_farfield = false;
  bool is_cut_face = false;
};

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

  // Surface Bounding Box
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

  // Determine Domain Bounding Box
  Vec3 dom_min, dom_max;
  if (farfield_bbox) {
    dom_min = Vec3(farfield_bbox[0], farfield_bbox[2], farfield_bbox[4]);
    dom_max = Vec3(farfield_bbox[1], farfield_bbox[3], farfield_bbox[5]);
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

  // 2. Base Grid of Root Octants at level 0
  double h0 = options.h0;
  int nx = std::max(1, static_cast<int>(std::round(dom_span.x / h0)));
  int ny = std::max(1, static_cast<int>(std::round(dom_span.y / h0)));
  int nz = std::max(1, static_cast<int>(std::round(dom_span.z / h0)));
  double dx = dom_span.x / nx;
  double dy = dom_span.y / ny;
  double dz = dom_span.z / nz;

  std::vector<OctreeNode> nodes;
  nodes.reserve(nx * ny * nz * 8);

  for (int k = 0; k < nz; ++k) {
    for (int j = 0; j < ny; ++j) {
      for (int i = 0; i < nx; ++i) {
        OctreeNode node;
        node.bmin = Vec3(dom_min.x + i * dx, dom_min.y + j * dy, dom_min.z + k * dz);
        node.bmax = Vec3(dom_min.x + (i + 1) * dx, dom_min.y + (j + 1) * dy, dom_min.z + (k + 1) * dz);
        node.center = (node.bmin + node.bmax) * 0.5;
        node.size = node.bmax - node.bmin;
        node.level = 0;
        node.parent = -1;
        node.is_leaf = true;
        node.v_bg = node.size.x * node.size.y * node.size.z;
        nodes.push_back(node);
      }
    }
  }

  // Helper lambda to find leaf node at point p in O(1) time
  auto find_leaf_at_point = [&](const Vec3& p) -> int32_t {
    if (p.x < dom_min.x || p.x > dom_max.x ||
        p.y < dom_min.y || p.y > dom_max.y ||
        p.z < dom_min.z || p.z > dom_max.z) {
      return -1;
    }
    int i = std::clamp(static_cast<int>((p.x - dom_min.x) / dx), 0, nx - 1);
    int j = std::clamp(static_cast<int>((p.y - dom_min.y) / dy), 0, ny - 1);
    int k = std::clamp(static_cast<int>((p.z - dom_min.z) / dz), 0, nz - 1);
    int32_t curr = k * ny * nx + j * nx + i;
    while (curr >= 0 && curr < static_cast<int32_t>(nodes.size()) && !nodes[curr].is_leaf) {
      int cx = (p.x >= nodes[curr].center.x) ? 1 : 0;
      int cy = (p.y >= nodes[curr].center.y) ? 1 : 0;
      int cz = (p.z >= nodes[curr].center.z) ? 1 : 0;
      int child_slot = cz * 4 + cy * 2 + cx;
      int32_t child = nodes[curr].children[child_slot];
      if (child < 0) break;
      curr = child;
    }
    return (curr >= 0 && curr < static_cast<int32_t>(nodes.size()) && nodes[curr].is_leaf) ? curr : -1;
  };

  // Helper lambda to subdivide an octree node into 8 children
  auto subdivide_node = [&](int32_t node_idx) {
    OctreeNode& parent = nodes[node_idx];
    parent.is_leaf = false;
    Vec3 mid = parent.center;
    Vec3 pmin = parent.bmin;
    Vec3 pmax = parent.bmax;

    std::array<std::pair<Vec3, Vec3>, 8> octants = {{
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
      OctreeNode child;
      child.bmin = octants[c].first;
      child.bmax = octants[c].second;
      child.center = (child.bmin + child.bmax) * 0.5;
      child.size = child.bmax - child.bmin;
      child.level = parent.level + 1;
      child.parent = node_idx;
      child.is_leaf = true;
      child.v_bg = child.size.x * child.size.y * child.size.z;
      int32_t child_idx = static_cast<int32_t>(nodes.size());
      nodes.push_back(child);
      nodes[node_idx].children[c] = child_idx;
    }
  };

  // 3. Adaptive Surface Refinement Pass
  // Subdivide nodes that intersect or are in the narrow band of the CAD surface
  int32_t max_depth = std::clamp(options.max_depth, 0, 4);
  for (int pass = 0; pass < max_depth; ++pass) {
    size_t current_count = nodes.size();
    for (size_t i = 0; i < current_count; ++i) {
      if (!nodes[i].is_leaf) continue;
      if (nodes[i].level >= max_depth) continue;

      double diag = nodes[i].size.norm();
      double phi = 0.0;
      Vec3 grad;
      sdf.evaluate(&nodes[i].center.x, 1, &phi, &grad.x);

      // Narrow-band surface criterion
      if (std::abs(phi) <= 0.65 * diag) {
        subdivide_node(static_cast<int32_t>(i));
      }
    }
  }

  // 4. Fast O(1) 2:1 Balancing Pass Across All 26 Directions
  // Ripple propagation until |L_A - L_B| <= 1 for all adjacent leaves
  double eps_probe = 1e-5 * std::min({dx, dy, dz});
  bool balancing_changed = true;
  int balance_iterations = 0;
  while (balancing_changed && balance_iterations < 20) {
    balancing_changed = false;
    balance_iterations++;

    size_t current_count = nodes.size();
    for (size_t i = 0; i < current_count; ++i) {
      if (!nodes[i].is_leaf) continue;
      int32_t lev_a = nodes[i].level;
      Vec3 c = nodes[i].center;
      Vec3 h = nodes[i].size * 0.5;

      // Check all 26 neighbor directions
      for (int di = -1; di <= 1; ++di) {
        for (int dj = -1; dj <= 1; ++dj) {
          for (int dk = -1; dk <= 1; ++dk) {
            if (di == 0 && dj == 0 && dk == 0) continue;
            Vec3 p_probe(
                c.x + di * (h.x + eps_probe),
                c.y + dj * (h.y + eps_probe),
                c.z + dk * (h.z + eps_probe)
            );
            int32_t nb = find_leaf_at_point(p_probe);
            if (nb >= 0 && nodes[nb].is_leaf) {
              if (lev_a - nodes[nb].level > 1) {
                subdivide_node(nb);
                balancing_changed = true;
              }
            }
          }
        }
      }
    }
  }

  // 5. Evaluate SDF and Domain Activity on Leaf Nodes
  std::vector<int32_t> active_leaves;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].is_leaf) continue;

    double phi = 0.0;
    Vec3 grad;
    sdf.evaluate(&nodes[i].center.x, 1, &phi, &grad.x);
    nodes[i].phi_c = phi;
    nodes[i].grad_c = grad;

    double diag = nodes[i].size.norm();
    bool active = false;
    int32_t reg = 0;

    if (options.domain_mode == 0) {
      // external_fluid: fluid domain (phi > 0 or narrow cut-cells)
      if (phi >= -0.15 * diag) {
        active = true;
        reg = 0;
      }
    } else if (options.domain_mode == 2) {
      // solid_only: solid interior (phi <= 0 or narrow cut-cells)
      if (phi <= 0.15 * diag) {
        active = true;
        reg = 1;
      }
    } else if (options.domain_mode == 3) {
      // cht_coupled: both fluid and solid are active
      active = true;
      reg = (phi >= 0.0) ? 0 : 1;
    } else {
      // internal_fluid
      if (phi <= 0.15 * diag) {
        active = true;
        reg = 0;
      }
    }

    nodes[i].is_active = active;
    nodes[i].region = reg;
    if (active) {
      nodes[i].cell_index = static_cast<int32_t>(active_leaves.size());
      active_leaves.push_back(static_cast<int32_t>(i));
    }
  }

  int64_t n_active_cells = static_cast<int64_t>(active_leaves.size());
  if (n_active_cells == 0) {
    return -1;
  }

  // 6. Quantized Vertex Map for Watertight Mosaic Face Stitching
  double quant_tol = 1e-8 * std::min({dx, dy, dz});
  std::unordered_map<QuantizedKey, int64_t, QuantizedKeyHash> vmap;
  std::vector<Vec3> node_coords;
  node_coords.reserve(n_active_cells * 8);

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

  // 7. Mosaic Polyhedral Conformal Transition Interface (ANSYS Mosaic / PUMA)
  // For each face direction (+/-X, +/-Y, +/-Z) of each active leaf:
  // - If neighbor is same level: single shared quad face.
  // - If neighbor is 2:1 refined: replace face with 4 coplanar sub-quads on the unrefined cell.
  // - Coarse transition cells become CellType::POLYHEDRON = 42.
  // Zero hanging nodes!

  struct FaceKey {
    std::array<int64_t, 4> v;
    bool operator==(const FaceKey& o) const {
      return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2] && v[3] == o.v[3];
    }
  };

  struct FaceKeyHash {
    std::size_t operator()(const FaceKey& k) const {
      std::size_t h = std::hash<int64_t>()(k.v[0]);
      h ^= std::hash<int64_t>()(k.v[1]) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= std::hash<int64_t>()(k.v[2]) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= std::hash<int64_t>()(k.v[3]) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };

  std::unordered_map<FaceKey, int64_t, FaceKeyHash> face_registry;
  std::vector<MosaicFace> mosaic_faces;
  mosaic_faces.reserve(n_active_cells * 6);

  std::vector<std::vector<int64_t>> cell_to_faces(n_active_cells);
  std::vector<CellType> cell_types(n_active_cells, CellType::HEXAHEDRON);

  auto add_mosaic_face = [&](
      const std::vector<int64_t>& quad_nodes,
      int64_t cell_owner,
      int64_t cell_neighbour,
      const std::string& patch_label,
      bool is_farfield_face,
      bool is_cut
  ) {
    std::array<int64_t, 4> sorted_nodes = {quad_nodes[0], quad_nodes[1], quad_nodes[2], quad_nodes[3]};
    std::sort(sorted_nodes.begin(), sorted_nodes.end());
    FaceKey fkey{sorted_nodes};

    auto it = face_registry.find(fkey);
    if (it == face_registry.end()) {
      MosaicFace mf;
      mf.nodes = quad_nodes;
      mf.owner = cell_owner;
      mf.neighbour = cell_neighbour;
      mf.patch_name = patch_label;
      mf.is_farfield = is_farfield_face;
      mf.is_cut_face = is_cut;

      const Vec3& p0 = node_coords[quad_nodes[0]];
      const Vec3& p1 = node_coords[quad_nodes[1]];
      const Vec3& p2 = node_coords[quad_nodes[2]];
      const Vec3& p3 = node_coords[quad_nodes[3]];
      mf.normal_owner = (cross(p1 - p0, p2 - p0) + cross(p2 - p0, p3 - p0)) * 0.5;
      mf.centre = (p0 + p1 + p2 + p3) * 0.25;

      int64_t f_idx = static_cast<int64_t>(mosaic_faces.size());
      mosaic_faces.push_back(mf);
      face_registry[fkey] = f_idx;

      cell_to_faces[cell_owner].push_back(f_idx);
      if (cell_neighbour >= 0) {
        cell_to_faces[cell_neighbour].push_back(f_idx);
      }
    } else {
      int64_t f_idx = it->second;
      if (mosaic_faces[f_idx].neighbour == -1 && cell_neighbour != -1) {
        mosaic_faces[f_idx].neighbour = cell_neighbour;
        cell_to_faces[cell_neighbour].push_back(f_idx);
      }
    }
  };

  // Build faces for all active leaf cells using O(1) point queries
  for (int64_t c = 0; c < n_active_cells; ++c) {
    int32_t node_idx = active_leaves[c];
    const OctreeNode& cell_a = nodes[node_idx];

    // Check each of the 6 face planes
    for (int dir = 0; dir < 6; ++dir) {
      Vec3 n_dir;
      Vec3 q0, q1, q2, q3;
      double plane_val = 0.0;
      bool is_farfield = false;
      std::string farfield_patch;

      if (dir == 0) {  // -X
        n_dir = Vec3(-1, 0, 0);
        q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
        q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
        q2 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
        q3 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
        plane_val = cell_a.bmin.x;
        if (std::abs(plane_val - dom_min.x) < eps_probe) {
          is_farfield = true;
          farfield_patch = "inlet";
        }
      } else if (dir == 1) {  // +X
        n_dir = Vec3(1, 0, 0);
        q0 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
        q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
        q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
        q3 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
        plane_val = cell_a.bmax.x;
        if (std::abs(plane_val - dom_max.x) < eps_probe) {
          is_farfield = true;
          farfield_patch = "outlet";
        }
      } else if (dir == 2) {  // -Y
        n_dir = Vec3(0, -1, 0);
        q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
        q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
        q2 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
        q3 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
        plane_val = cell_a.bmin.y;
        if (std::abs(plane_val - dom_min.y) < eps_probe) {
          is_farfield = true;
          farfield_patch = "sides";
        }
      } else if (dir == 3) {  // +Y
        n_dir = Vec3(0, 1, 0);
        q0 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
        q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
        q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
        q3 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
        plane_val = cell_a.bmax.y;
        if (std::abs(plane_val - dom_max.y) < eps_probe) {
          is_farfield = true;
          farfield_patch = "sides";
        }
      } else if (dir == 4) {  // -Z
        n_dir = Vec3(0, 0, -1);
        q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
        q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
        q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
        q3 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
        plane_val = cell_a.bmin.z;
        if (std::abs(plane_val - dom_min.z) < eps_probe) {
          is_farfield = true;
          farfield_patch = "bottom";
        }
      } else {  // +Z
        n_dir = Vec3(0, 0, 1);
        q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
        q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
        q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
        q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
        plane_val = cell_a.bmax.z;
        if (std::abs(plane_val - dom_max.z) < eps_probe) {
          is_farfield = true;
          farfield_patch = "top";
        }
      }

      Vec3 fc = (q0 + q1 + q2 + q3) * 0.25;
      Vec3 p_probe = fc + n_dir * eps_probe;
      int32_t nb_leaf = find_leaf_at_point(p_probe);

      if (nb_leaf < 0) {
        // Outer domain boundary face
        int64_t v0 = get_or_add_vertex(q0);
        int64_t v1 = get_or_add_vertex(q1);
        int64_t v2 = get_or_add_vertex(q2);
        int64_t v3 = get_or_add_vertex(q3);
        std::string patch = is_farfield ? farfield_patch : "wall";
        add_mosaic_face({v0, v1, v2, v3}, c, -1, patch, is_farfield, !is_farfield);
      } else if (nodes[nb_leaf].level > cell_a.level) {
        // Neighbor is 2:1 refined: replace face on coarse cell C with 4 sub-quads!
        cell_types[c] = CellType::POLYHEDRON;

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
          int64_t v0 = get_or_add_vertex(sub_quads[sq][0]);
          int64_t v1 = get_or_add_vertex(sub_quads[sq][1]);
          int64_t v2 = get_or_add_vertex(sub_quads[sq][2]);
          int64_t v3 = get_or_add_vertex(sub_quads[sq][3]);

          Vec3 sub_c = (sub_quads[sq][0] + sub_quads[sq][1] + sub_quads[sq][2] + sub_quads[sq][3]) * 0.25;
          Vec3 sub_probe = sub_c + n_dir * eps_probe;
          int32_t fine_leaf = find_leaf_at_point(sub_probe);

          if (fine_leaf >= 0 && nodes[fine_leaf].is_active) {
            int64_t fine_c = nodes[fine_leaf].cell_index;
            int64_t o = std::min(c, fine_c);
            int64_t nb = std::max(c, fine_c);
            std::vector<int64_t> fnodes = (o == c) ? std::vector<int64_t>{v0, v1, v2, v3}
                                                   : std::vector<int64_t>{v0, v3, v2, v1};
            add_mosaic_face(fnodes, o, nb, "", false, false);
          } else {
            std::string patch = is_farfield ? farfield_patch : "wall";
            add_mosaic_face({v0, v1, v2, v3}, c, -1, patch, is_farfield, !is_farfield);
          }
        }
      } else {
        // Neighbor is at same level or coarser
        int64_t v0 = get_or_add_vertex(q0);
        int64_t v1 = get_or_add_vertex(q1);
        int64_t v2 = get_or_add_vertex(q2);
        int64_t v3 = get_or_add_vertex(q3);

        if (nodes[nb_leaf].is_active) {
          int64_t nb_c = nodes[nb_leaf].cell_index;
          if (c < nb_c) {
            add_mosaic_face({v0, v1, v2, v3}, c, nb_c, "", false, false);
          }
        } else {
          // Boundary to inactive region (e.g. CAD surface wall)
          std::string patch = is_farfield ? farfield_patch : "wall";
          add_mosaic_face({v0, v1, v2, v3}, c, -1, patch, is_farfield, !is_farfield);
        }
      }
    }
  }

  // 8. Cut-Cell Snapping along grad(phi) with Strict AMIPS Guard and V_i > 0
  if (options.snap_cut_cells) {
    std::unordered_set<int64_t> wall_vertices;
    for (const auto& mf : mosaic_faces) {
      if (mf.neighbour == -1 && !mf.is_farfield) {
        for (int64_t v : mf.nodes) {
          wall_vertices.insert(v);
        }
      }
    }

    std::unordered_map<int64_t, std::vector<int64_t>> node_to_cells;
    for (size_t f = 0; f < mosaic_faces.size(); ++f) {
      const auto& mf = mosaic_faces[f];
      for (int64_t v : mf.nodes) {
        node_to_cells[v].push_back(mf.owner);
        if (mf.neighbour >= 0) {
          node_to_cells[v].push_back(mf.neighbour);
        }
      }
    }

    double snap_tol = options.snap_tol_factor * options.h0;
    for (int64_t v : wall_vertices) {
      Vec3 pos = node_coords[v];
      double phi = 0.0;
      Vec3 grad;
      sdf.evaluate(&pos.x, 1, &phi, &grad.x);

      if (std::abs(phi) <= snap_tol && grad.norm2() > 1e-12) {
        Vec3 target = pos - (phi / grad.norm2()) * grad;
        Vec3 disp = target - pos;

        double best_lambda = 0.0;
        for (double lambda : {1.0, 0.5, 0.25, 0.125}) {
          Vec3 test_pos = pos + lambda * disp;
          bool valid = true;

          for (int64_t cell_idx : node_to_cells[v]) {
            const OctreeNode& onode = nodes[active_leaves[cell_idx]];
            double tet_energy = computeTetAMIPS(test_pos, onode.bmin, onode.bmax, onode.center);
            if (tet_energy > options.amips_max) {
              valid = false;
              break;
            }
          }

          if (valid) {
            best_lambda = lambda;
            break;
          }
        }

        if (best_lambda > 0.0) {
          node_coords[v] = pos + best_lambda * disp;
        }
      }
    }
  }

  // 9. Recompute Face Normals and Areas after Snapping
  for (auto& mf : mosaic_faces) {
    const Vec3& p0 = node_coords[mf.nodes[0]];
    const Vec3& p1 = node_coords[mf.nodes[1]];
    const Vec3& p2 = node_coords[mf.nodes[2]];
    const Vec3& p3 = node_coords[mf.nodes[3]];
    mf.normal_owner = (cross(p1 - p0, p2 - p0) + cross(p2 - p0, p3 - p0)) * 0.5;
    mf.centre = (p0 + p1 + p2 + p3) * 0.25;

    Vec3 c_owner = nodes[active_leaves[mf.owner]].center;
    if (dot(mf.normal_owner, mf.centre - c_owner) < 0.0) {
      mf.normal_owner = -mf.normal_owner;
      std::reverse(mf.nodes.begin(), mf.nodes.end());
    }
  }

  // 10. Compute Cell Volumes via Gauss Divergence Theorem
  std::vector<double> cell_vols(n_active_cells, 0.0);
  std::vector<Vec3> cell_cents(n_active_cells, Vec3(0, 0, 0));

  for (size_t f = 0; f < mosaic_faces.size(); ++f) {
    const auto& mf = mosaic_faces[f];
    int64_t o = mf.owner;
    int64_t nb = mf.neighbour;

    double v_o = dot(mf.centre, mf.normal_owner) / 3.0;
    cell_vols[o] += v_o;
    cell_cents[o] += mf.centre * (dot(mf.centre, mf.normal_owner));

    if (nb >= 0) {
      double v_nb = -dot(mf.centre, mf.normal_owner) / 3.0;
      cell_vols[nb] += v_nb;
      cell_cents[nb] -= mf.centre * (dot(mf.centre, mf.normal_owner));
    }
  }

  for (int64_t c = 0; c < n_active_cells; ++c) {
    if (cell_vols[c] > 1e-15) {
      cell_cents[c] /= (cell_vols[c] * 3.0);
    } else {
      cell_cents[c] = nodes[active_leaves[c]].center;
      cell_vols[c] = nodes[active_leaves[c]].v_bg;
    }
  }

  // 11. Small-Cell Topological Merging (V_cut < 0.20 V_bg) along grad(phi)
  if (options.merge_small_cells) {
    for (int64_t c = 0; c < n_active_cells; ++c) {
      double v_bg = nodes[active_leaves[c]].v_bg;
      double alpha = cell_vols[c] / (v_bg + 1e-30);

      if (alpha < options.small_cell_ratio && alpha > 0.0) {
        Vec3 grad = nodes[active_leaves[c]].grad_c;
        int64_t best_nb = -1;
        double max_align = -1e30;

        for (int64_t f_idx : cell_to_faces[c]) {
          const auto& mf = mosaic_faces[f_idx];
          int64_t other = (mf.owner == c) ? mf.neighbour : mf.owner;
          if (other >= 0 && other < n_active_cells) {
            Vec3 dir_nb = (cell_cents[other] - cell_cents[c]).normalized();
            double align = dot(dir_nb, grad);
            if (align > max_align) {
              max_align = align;
              best_nb = other;
            }
          }
        }

        if (best_nb >= 0) {
          double orig_nb_vol = cell_vols[best_nb];
          cell_vols[best_nb] += cell_vols[c];
          cell_cents[best_nb] = (cell_cents[best_nb] * orig_nb_vol + cell_cents[c] * cell_vols[c]) /
                                (cell_vols[best_nb] + 1e-30);
          cell_types[best_nb] = CellType::POLYHEDRON;

          for (int64_t f_idx : cell_to_faces[c]) {
            auto& mf = mosaic_faces[f_idx];
            if (mf.owner == c && mf.neighbour == best_nb) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == best_nb && mf.neighbour == c) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == c) {
              mf.owner = best_nb;
              cell_to_faces[best_nb].push_back(f_idx);
            } else if (mf.neighbour == c) {
              mf.neighbour = best_nb;
              cell_to_faces[best_nb].push_back(f_idx);
            }
          }

          cell_vols[c] = 0.0;
        }
      }
    }
  }

  std::vector<int64_t> old_to_new_cell(n_active_cells, -1);
  int64_t final_n_cells = 0;
  for (int64_t c = 0; c < n_active_cells; ++c) {
    if (cell_vols[c] > 1e-15) {
      old_to_new_cell[c] = final_n_cells++;
    }
  }

  // 12. Separate Internal Faces and Boundary Faces
  std::vector<MosaicFace> final_internal_faces;
  std::vector<MosaicFace> final_boundary_faces;

  for (auto& mf : mosaic_faces) {
    if (mf.owner < 0 && mf.neighbour < 0) continue;

    int64_t new_o = (mf.owner >= 0) ? old_to_new_cell[mf.owner] : -1;
    int64_t new_nb = (mf.neighbour >= 0) ? old_to_new_cell[mf.neighbour] : -1;

    if (new_o >= 0 && new_nb >= 0) {
      if (new_o > new_nb) {
        std::swap(new_o, new_nb);
        mf.normal_owner = -mf.normal_owner;
        std::reverse(mf.nodes.begin(), mf.nodes.end());
      }
      mf.owner = new_o;
      mf.neighbour = new_nb;
      final_internal_faces.push_back(mf);
    } else if (new_o >= 0 && new_nb < 0) {
      mf.owner = new_o;
      mf.neighbour = -1;
      final_boundary_faces.push_back(mf);
    } else if (new_nb >= 0 && new_o < 0) {
      mf.owner = new_nb;
      mf.neighbour = -1;
      mf.normal_owner = -mf.normal_owner;
      std::reverse(mf.nodes.begin(), mf.nodes.end());
      final_boundary_faces.push_back(mf);
    }
  }

  std::sort(final_internal_faces.begin(), final_internal_faces.end(),
            [](const MosaicFace& a, const MosaicFace& b) {
              if (a.owner != b.owner) return a.owner < b.owner;
              return a.neighbour < b.neighbour;
            });

  std::sort(final_boundary_faces.begin(), final_boundary_faces.end(),
            [](const MosaicFace& a, const MosaicFace& b) {
              return a.patch_name < b.patch_name;
            });

  std::vector<MosaicFace> all_faces;
  all_faces.reserve(final_internal_faces.size() + final_boundary_faces.size());
  for (auto& f : final_internal_faces) all_faces.push_back(f);
  for (auto& f : final_boundary_faces) all_faces.push_back(f);

  int64_t total_faces = static_cast<int64_t>(all_faces.size());
  int64_t n_internal = static_cast<int64_t>(final_internal_faces.size());

  // 13. Populate Output NativeMeshData Arrays
  mesh.points.resize(3 * node_coords.size());
  for (size_t i = 0; i < node_coords.size(); ++i) {
    mesh.points[3 * i + 0] = node_coords[i].x;
    mesh.points[3 * i + 1] = node_coords[i].y;
    mesh.points[3 * i + 2] = node_coords[i].z;
  }

  mesh.cell_centres.resize(3 * final_n_cells);
  mesh.cell_volumes.resize(final_n_cells);
  mesh.cell_types.resize(final_n_cells);
  mesh.cell_regions.resize(final_n_cells, 0);

  for (int64_t old_c = 0; old_c < n_active_cells; ++old_c) {
    int64_t new_c = old_to_new_cell[old_c];
    if (new_c >= 0) {
      mesh.cell_centres[3 * new_c + 0] = cell_cents[old_c].x;
      mesh.cell_centres[3 * new_c + 1] = cell_cents[old_c].y;
      mesh.cell_centres[3 * new_c + 2] = cell_cents[old_c].z;
      mesh.cell_volumes[new_c] = cell_vols[old_c];
      mesh.cell_types[new_c] = static_cast<int32_t>(cell_types[old_c]);
      mesh.cell_regions[new_c] = nodes[active_leaves[old_c]].region;
    }
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

  // CSR Face Nodes
  mesh.face_offsets.resize(total_faces + 1, 0);
  int64_t total_fnodes = 0;
  for (const auto& mf : all_faces) total_fnodes += static_cast<int64_t>(mf.nodes.size());
  mesh.face_nodes.reserve(total_fnodes);

  for (int64_t f = 0; f < total_faces; ++f) {
    mesh.face_offsets[f] = static_cast<int64_t>(mesh.face_nodes.size());
    for (int64_t v : all_faces[f].nodes) {
      mesh.face_nodes.push_back(v);
    }

    mesh.owner[f] = all_faces[f].owner;
    mesh.neighbour[f] = all_faces[f].neighbour;

    Vec3 sf = all_faces[f].normal_owner;
    Vec3 cf = all_faces[f].centre;

    mesh.face_areas[3 * f + 0] = sf.x;
    mesh.face_areas[3 * f + 1] = sf.y;
    mesh.face_areas[3 * f + 2] = sf.z;

    mesh.face_centres[3 * f + 0] = cf.x;
    mesh.face_centres[3 * f + 1] = cf.y;
    mesh.face_centres[3 * f + 2] = cf.z;

    int64_t o = mesh.owner[f];
    int64_t nb = mesh.neighbour[f];
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
      mesh.non_ortho[f] = theta_deg;

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
      mesh.skewness[f] = skew;
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
        mesh.stab_mask[f] = 3;
        mesh.beta_upwind[f] = 0.0;
      } else {
        double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
        double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
        mesh.beta_upwind[f] = std::min(beta_ortho, beta_skew);
        mesh.stab_mask[f] = (theta_deg >= 75.0 || skew >= 1.0) ? 3 : ((theta_deg >= 65.0 || skew >= 0.7) ? 2 : ((theta_deg >= 40.0 || skew >= 0.4) ? 1 : 0));
      }
    } else {
      Vec3 df = cf - co;
      mesh.delta_f[3 * f + 0] = df.x;
      mesh.delta_f[3 * f + 1] = df.y;
      mesh.delta_f[3 * f + 2] = df.z;
      mesh.k_f[3 * f + 0] = 0.0;
      mesh.k_f[3 * f + 1] = 0.0;
      mesh.k_f[3 * f + 2] = 0.0;
      mesh.non_ortho[f] = 0.0;
      mesh.skewness[f] = 0.0;
      mesh.stab_mask[f] = (dot(sf, df) <= 0.0) ? 3 : 0;
      mesh.beta_upwind[f] = (dot(sf, df) <= 0.0) ? 0.0 : 1.0;
    }
  }
  mesh.face_offsets[total_faces] = static_cast<int64_t>(mesh.face_nodes.size());

  // CSR Cell Nodes
  std::vector<std::vector<int64_t>> cell_nodes_temp(final_n_cells);
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t o = mesh.owner[f];
    for (int64_t v : all_faces[f].nodes) {
      cell_nodes_temp[o].push_back(v);
    }
    int64_t nb = mesh.neighbour[f];
    if (nb >= 0) {
      for (int64_t v : all_faces[f].nodes) {
        cell_nodes_temp[nb].push_back(v);
      }
    }
  }
  for (int64_t c = 0; c < final_n_cells; ++c) {
    std::sort(cell_nodes_temp[c].begin(), cell_nodes_temp[c].end());
    cell_nodes_temp[c].erase(std::unique(cell_nodes_temp[c].begin(), cell_nodes_temp[c].end()), cell_nodes_temp[c].end());
  }

  mesh.cell_offsets.resize(final_n_cells + 1, 0);
  int64_t total_cnodes = 0;
  for (int64_t c = 0; c < final_n_cells; ++c) total_cnodes += static_cast<int64_t>(cell_nodes_temp[c].size());
  mesh.cell_nodes.reserve(total_cnodes);

  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.cell_offsets[c] = static_cast<int64_t>(mesh.cell_nodes.size());
    for (int64_t v : cell_nodes_temp[c]) {
      mesh.cell_nodes.push_back(v);
    }
  }
  mesh.cell_offsets[final_n_cells] = static_cast<int64_t>(mesh.cell_nodes.size());

  // Aspect ratio per cell
  mesh.aspect_ratio.resize(final_n_cells, 1.0);
  for (int64_t c = 0; c < final_n_cells; ++c) {
    double min_e = 1e30;
    double max_e = 0.0;
    int64_t off0 = mesh.cell_offsets[c];
    int64_t off1 = mesh.cell_offsets[c + 1];
    int64_t nv = off1 - off0;
    for (int64_t i = 0; i < nv; ++i) {
      for (int64_t j = i + 1; j < nv; ++j) {
        int64_t v1 = mesh.cell_nodes[off0 + i];
        int64_t v2 = mesh.cell_nodes[off0 + j];
        Vec3 p1 = node_coords[v1];
        Vec3 p2 = node_coords[v2];
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

  // 14. Berger & Giuliani (2024) WSRD Weights for 0.20 <= alpha < 0.50
  for (int64_t old_c = 0; old_c < n_active_cells; ++old_c) {
    int64_t new_c = old_to_new_cell[old_c];
    if (new_c >= 0) {
      double v_bg = nodes[active_leaves[old_c]].v_bg;
      double alpha = std::clamp(cell_vols[old_c] / (v_bg + 1e-30), 0.0, 1.0);
      mesh.wsrd_alpha[new_c] = alpha;
    }
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
