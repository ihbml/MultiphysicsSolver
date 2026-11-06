#include "sdf_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline double clamp_val(double v, double min_v, double max_v) {
  return std::max(min_v, std::min(max_v, v));
}

inline double box_dist_sq(const Vec3& p, const Vec3& bmin, const Vec3& bmax) {
  double dx = std::max(0.0, std::max(bmin.x - p.x, p.x - bmax.x));
  double dy = std::max(0.0, std::max(bmin.y - p.y, p.y - bmax.y));
  double dz = std::max(0.0, std::max(bmin.z - p.z, p.z - bmax.z));
  return dx * dx + dy * dy + dz * dz;
}

}  // namespace

// ==============================================================================
// TriangleAABBTree Implementation
// ==============================================================================

TriangleAABBTree::TriangleAABBTree(const double* vertices, int64_t n_vertices,
                                   const int32_t* faces, int64_t n_faces,
                                   int32_t leaf_size) {
  vertices_.resize(n_vertices);
  for (int64_t i = 0; i < n_vertices; ++i) {
    vertices_[i] = Vec3(vertices[3 * i], vertices[3 * i + 1], vertices[3 * i + 2]);
  }

  faces_.resize(n_faces);
  tri_bmin_.resize(n_faces);
  tri_bmax_.resize(n_faces);
  tri_centroids_.resize(n_faces);
  tri_indices_.resize(n_faces);

  for (int64_t i = 0; i < n_faces; ++i) {
    int32_t i0 = faces[3 * i];
    int32_t i1 = faces[3 * i + 1];
    int32_t i2 = faces[3 * i + 2];
    faces_[i] = {i0, i1, i2};
    tri_indices_[i] = static_cast<int32_t>(i);

    const Vec3& v0 = vertices_[i0];
    const Vec3& v1 = vertices_[i1];
    const Vec3& v2 = vertices_[i2];

    tri_bmin_[i] = Vec3(
        std::min({v0.x, v1.x, v2.x}),
        std::min({v0.y, v1.y, v2.y}),
        std::min({v0.z, v1.z, v2.z})
    );

    tri_bmax_[i] = Vec3(
        std::max({v0.x, v1.x, v2.x}),
        std::max({v0.y, v1.y, v2.y}),
        std::max({v0.z, v1.z, v2.z})
    );

    tri_centroids_[i] = (v0 + v1 + v2) / 3.0;
  }

  if (n_faces > 0) {
    nodes_.reserve(2 * n_faces);
    build_recursive(0, static_cast<int32_t>(n_faces), leaf_size);
  }
}

int32_t TriangleAABBTree::build_recursive(int32_t start, int32_t count, int32_t leaf_size) {
  int32_t node_idx = static_cast<int32_t>(nodes_.size());
  nodes_.emplace_back();

  constexpr double kInf = std::numeric_limits<double>::infinity();
  Vec3 bmin(kInf, kInf, kInf);
  Vec3 bmax(-kInf, -kInf, -kInf);
  for (int32_t i = 0; i < count; ++i) {
    int32_t tid = tri_indices_[start + i];
    bmin.x = std::min(bmin.x, tri_bmin_[tid].x);
    bmin.y = std::min(bmin.y, tri_bmin_[tid].y);
    bmin.z = std::min(bmin.z, tri_bmin_[tid].z);
    bmax.x = std::max(bmax.x, tri_bmax_[tid].x);
    bmax.y = std::max(bmax.y, tri_bmax_[tid].y);
    bmax.z = std::max(bmax.z, tri_bmax_[tid].z);
  }

  nodes_[node_idx].bmin = bmin;
  nodes_[node_idx].bmax = bmax;

  if (count <= leaf_size) {
    nodes_[node_idx].tri_start = start;
    nodes_[node_idx].tri_count = count;
    return node_idx;
  }

  Vec3 cmin(kInf, kInf, kInf);
  Vec3 cmax(-kInf, -kInf, -kInf);
  for (int32_t i = 0; i < count; ++i) {
    int32_t tid = tri_indices_[start + i];
    cmin.x = std::min(cmin.x, tri_centroids_[tid].x);
    cmin.y = std::min(cmin.y, tri_centroids_[tid].y);
    cmin.z = std::min(cmin.z, tri_centroids_[tid].z);
    cmax.x = std::max(cmax.x, tri_centroids_[tid].x);
    cmax.y = std::max(cmax.y, tri_centroids_[tid].y);
    cmax.z = std::max(cmax.z, tri_centroids_[tid].z);
  }

  Vec3 extent = cmax - cmin;
  int32_t axis = 0;
  if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
  else if (extent.z > extent.x && extent.z >= extent.y) axis = 2;

  int32_t mid = start + count / 2;
  auto comp = [this, axis](int32_t a, int32_t b) {
    const Vec3& ca = tri_centroids_[a];
    const Vec3& cb = tri_centroids_[b];
    if (axis == 0) return ca.x < cb.x;
    if (axis == 1) return ca.y < cb.y;
    return ca.z < cb.z;
  };

  std::nth_element(tri_indices_.begin() + start,
                   tri_indices_.begin() + mid,
                   tri_indices_.begin() + start + count, comp);

  int32_t left_child = build_recursive(start, count / 2, leaf_size);
  int32_t right_child = build_recursive(mid, count - count / 2, leaf_size);

  nodes_[node_idx].left = left_child;
  nodes_[node_idx].right = right_child;
  return node_idx;
}

void TriangleAABBTree::point_to_triangle(const Vec3& p, int32_t tid, Vec3& out_q,
                                         Vec3& out_bary, double& out_dist) const {
  const auto& f = faces_[tid];
  const Vec3& a = vertices_[f[0]];
  const Vec3& b = vertices_[f[1]];
  const Vec3& c = vertices_[f[2]];

  Vec3 ab = b - a;
  Vec3 ac = c - a;
  Vec3 ap = p - a;

  double d1 = dot(ab, ap);
  double d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) {
    out_q = a;
    out_bary = Vec3(1.0, 0.0, 0.0);
    out_dist = (p - a).norm();
    return;
  }

  Vec3 bp = p - b;
  double d3 = dot(ab, bp);
  double d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) {
    out_q = b;
    out_bary = Vec3(0.0, 1.0, 0.0);
    out_dist = (p - b).norm();
    return;
  }

  double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    double denom = d1 - d3;
    double v = std::abs(denom) > 1e-30 ? d1 / denom : 0.0;
    out_q = a + ab * v;
    out_bary = Vec3(1.0 - v, v, 0.0);
    out_dist = (p - out_q).norm();
    return;
  }

  Vec3 cp = p - c;
  double d5 = dot(ab, cp);
  double d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) {
    out_q = c;
    out_bary = Vec3(0.0, 0.0, 1.0);
    out_dist = (p - c).norm();
    return;
  }

  double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    double denom = d2 - d6;
    double w = std::abs(denom) > 1e-30 ? d2 / denom : 0.0;
    out_q = a + ac * w;
    out_bary = Vec3(1.0 - w, 0.0, w);
    out_dist = (p - out_q).norm();
    return;
  }

  double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    double denom = (d4 - d3) + (d5 - d6);
    double w = std::abs(denom) > 1e-30 ? (d4 - d3) / denom : 0.0;
    out_q = b + (c - b) * w;
    out_bary = Vec3(0.0, 1.0 - w, w);
    out_dist = (p - out_q).norm();
    return;
  }

  double denom_sum = va + vb + vc;
  double inv_denom = std::abs(denom_sum) > 1e-30 ? 1.0 / denom_sum : 0.0;
  double v = vb * inv_denom;
  double w = vc * inv_denom;
  double u = 1.0 - v - w;
  out_q = a + ab * v + ac * w;
  out_bary = Vec3(u, v, w);
  out_dist = (p - out_q).norm();
}

void TriangleAABBTree::closest_point(const Vec3& p, int32_t prev_facet,
                                     Vec3& out_q, int32_t& out_facet,
                                     Vec3& out_bary, double& out_dist) const {
  if (nodes_.empty()) {
    out_dist = std::numeric_limits<double>::infinity();
    out_facet = -1;
    return;
  }

  double best_dist = std::numeric_limits<double>::infinity();
  double best_dist_sq = std::numeric_limits<double>::infinity();
  Vec3 best_q(0, 0, 0);
  int32_t best_facet = -1;
  Vec3 best_bary(0, 0, 0);

  // 1. Spatial coherence hint evaluation
  if (prev_facet >= 0 && prev_facet < static_cast<int32_t>(faces_.size())) {
    Vec3 q, bary;
    double d;
    point_to_triangle(p, prev_facet, q, bary, d);
    best_dist = d;
    best_dist_sq = d * d;
    best_q = q;
    best_facet = prev_facet;
    best_bary = bary;
  }

  // 2. Branch-and-bound traversal
  int32_t stack[128];
  int32_t stack_ptr = 0;
  stack[stack_ptr++] = 0;

  while (stack_ptr > 0) {
    int32_t node_idx = stack[--stack_ptr];
    const auto& node = nodes_[node_idx];

    double dsq = box_dist_sq(p, node.bmin, node.bmax);
    if (dsq >= best_dist_sq) continue;

    if (node.tri_count > 0) {
      for (int32_t i = 0; i < node.tri_count; ++i) {
        int32_t tid = tri_indices_[node.tri_start + i];
        Vec3 q, bary;
        double d;
        point_to_triangle(p, tid, q, bary, d);
        double d_sq = d * d;
        if (d_sq < best_dist_sq) {
          best_dist = d;
          best_dist_sq = d_sq;
          best_q = q;
          best_facet = tid;
          best_bary = bary;
        }
      }
    } else {
      const auto& left = nodes_[node.left];
      const auto& right = nodes_[node.right];
      double dl_sq = box_dist_sq(p, left.bmin, left.bmax);
      double dr_sq = box_dist_sq(p, right.bmin, right.bmax);

      if (dl_sq < dr_sq) {
        if (dr_sq < best_dist_sq && stack_ptr < 127) stack[stack_ptr++] = node.right;
        if (dl_sq < best_dist_sq && stack_ptr < 127) stack[stack_ptr++] = node.left;
      } else {
        if (dl_sq < best_dist_sq && stack_ptr < 127) stack[stack_ptr++] = node.left;
        if (dr_sq < best_dist_sq && stack_ptr < 127) stack[stack_ptr++] = node.right;
      }
    }
  }

  out_q = best_q;
  out_facet = best_facet;
  out_bary = best_bary;
  out_dist = best_dist;
}

// ==============================================================================
// Pseudonormals Implementation
// ==============================================================================

Pseudonormals::Pseudonormals(const std::vector<Vec3>& vertices,
                             const std::vector<std::array<int32_t, 3>>& faces) {
  faces_ = faces;
  size_t n_faces = faces.size();
  size_t n_vertices = vertices.size();

  face_normals_.resize(n_faces);
  vertex_pseudonormals_.assign(n_vertices, Vec3(0, 0, 0));
  face_edge_map_.resize(n_faces);

  // 1. Face unit normals
  for (size_t i = 0; i < n_faces; ++i) {
    const auto& f = faces[i];
    Vec3 e01 = vertices[f[1]] - vertices[f[0]];
    Vec3 e02 = vertices[f[2]] - vertices[f[0]];
    Vec3 c = cross(e01, e02);
    double n = c.norm();
    face_normals_[i] = n > 1e-20 ? c / n : Vec3(0, 0, 1);
  }

  // 2. Vertex pseudonormals (angle-weighted)
  for (size_t i = 0; i < n_faces; ++i) {
    const auto& f = faces[i];
    const Vec3& fn = face_normals_[i];

    // Corner 0
    Vec3 e01 = vertices[f[1]] - vertices[f[0]];
    Vec3 e02 = vertices[f[2]] - vertices[f[0]];
    double d01 = e01.norm();
    double d02 = e02.norm();
    double cos0 = (d01 > 1e-20 && d02 > 1e-20) ? dot(e01, e02) / (d01 * d02) : 1.0;
    double a0 = std::acos(clamp_val(cos0, -1.0, 1.0));
    vertex_pseudonormals_[f[0]] += fn * a0;

    // Corner 1
    Vec3 e12 = vertices[f[2]] - vertices[f[1]];
    Vec3 e10 = -e01;
    double d12 = e12.norm();
    double cos1 = (d12 > 1e-20 && d01 > 1e-20) ? dot(e12, e10) / (d12 * d01) : 1.0;
    double a1 = std::acos(clamp_val(cos1, -1.0, 1.0));
    vertex_pseudonormals_[f[1]] += fn * a1;

    // Corner 2
    Vec3 e20 = -e02;
    Vec3 e21 = -e12;
    double cos2 = (d02 > 1e-20 && d12 > 1e-20) ? dot(e20, e21) / (d02 * d12) : 1.0;
    double a2 = std::acos(clamp_val(cos2, -1.0, 1.0));
    vertex_pseudonormals_[f[2]] += fn * a2;
  }

  for (size_t i = 0; i < n_vertices; ++i) {
    double n = vertex_pseudonormals_[i].norm();
    vertex_pseudonormals_[i] = n > 1e-6 ? vertex_pseudonormals_[i] / n : Vec3(0, 0, 1);
  }

  // 3. Undirected edge pseudonormals
  std::map<std::pair<int32_t, int32_t>, int32_t> edge_map;
  std::vector<Vec3> edge_acc;

  for (size_t i = 0; i < n_faces; ++i) {
    const auto& f = faces[i];
    std::pair<int32_t, int32_t> edges[3] = {
        {std::min(f[0], f[1]), std::max(f[0], f[1])},
        {std::min(f[1], f[2]), std::max(f[1], f[2])},
        {std::min(f[2], f[0]), std::max(f[2], f[0])}
    };
    const Vec3& fn = face_normals_[i];

    for (int32_t e = 0; e < 3; ++e) {
      auto it = edge_map.find(edges[e]);
      if (it == edge_map.end()) {
        int32_t eid = static_cast<int32_t>(edge_acc.size());
        edge_map[edges[e]] = eid;
        edge_acc.push_back(fn);
        face_edge_map_[i][e] = eid;
      } else {
        edge_acc[it->second] += fn;
        face_edge_map_[i][e] = it->second;
      }
    }
  }

  edge_pseudonormals_.resize(edge_acc.size());
  for (size_t i = 0; i < edge_acc.size(); ++i) {
    double n = edge_acc[i].norm();
    edge_pseudonormals_[i] = n > 1e-6 ? edge_acc[i] / n : Vec3(0, 0, 1);
  }
}

Vec3 Pseudonormals::evaluate(int32_t face_idx, const Vec3& bary, double eps) const {
  double u = bary.x;
  double v = bary.y;
  double w = bary.z;

  const auto& f_edges = face_edge_map_[face_idx];
  const auto& f_nodes = faces_[face_idx];

  // Vertex classification
  if (u > 1.0 - 2.0 * eps) return vertex_pseudonormals_[f_nodes[0]];
  if (v > 1.0 - 2.0 * eps) return vertex_pseudonormals_[f_nodes[1]];
  if (w > 1.0 - 2.0 * eps) return vertex_pseudonormals_[f_nodes[2]];

  // Edge classification
  if (u <= eps && v > eps && w > eps) return edge_pseudonormals_[f_edges[1]]; // edge opposite v0 (v1-v2)
  if (v <= eps && u > eps && w > eps) return edge_pseudonormals_[f_edges[2]]; // edge opposite v1 (v2-v0)
  if (w <= eps && u > eps && v > eps) return edge_pseudonormals_[f_edges[0]]; // edge opposite v2 (v0-v1)

  // Face interior
  return face_normals_[face_idx];
}

// ==============================================================================
// MeshSDF Implementation
// ==============================================================================

MeshSDF::MeshSDF(const double* vertices, int64_t n_vertices,
                 const int32_t* faces, int64_t n_faces)
    : tree_(vertices, n_vertices, faces, n_faces),
      pseudonormals_(tree_.vertices(), tree_.faces()) {}

int32_t MeshSDF::evaluate(const double* points, int64_t n_points,
                          double* out_phi, double* out_grad) const {
  if (!points || !out_phi || n_points <= 0) return -1;

#ifdef _OPENMP
#pragma omp parallel
{
  int32_t prev_facet = -1;
  #pragma omp for schedule(dynamic, 64)
#else
  int32_t prev_facet = -1;
#endif
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    double p_max = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    if (p_max >= 1e30) {
      double p_norm = p.norm();
      Vec3 unit_p = p_norm > 1e-15 ? p / p_norm : Vec3(0, 0, 1);
      out_phi[i] = p_norm;
      if (out_grad) {
        out_grad[3 * i + 0] = unit_p.x;
        out_grad[3 * i + 1] = unit_p.y;
        out_grad[3 * i + 2] = unit_p.z;
      }
      continue;
    }

    Vec3 q, bary;
    int32_t facet = -1;
    double dist = 0.0;

    tree_.closest_point(p, prev_facet, q, facet, bary, dist);
    prev_facet = facet;

    if (facet < 0 || facet >= static_cast<int32_t>(tree_.faces().size())) {
      double p_norm = p.norm();
      Vec3 unit_p = p_norm > 1e-15 ? p / p_norm : Vec3(0, 0, 1);
      out_phi[i] = p_norm;
      if (out_grad) {
        out_grad[3 * i + 0] = unit_p.x;
        out_grad[3 * i + 1] = unit_p.y;
        out_grad[3 * i + 2] = unit_p.z;
      }
      continue;
    }

    Vec3 np = pseudonormals_.evaluate(facet, bary);
    Vec3 diff = p - q;
    double diff_norm = diff.norm();
    double dot_val = dot(diff, np);
    double sign = dot_val >= 0.0 ? 1.0 : -1.0;

    bool outside_bbox = (p.x < tree_.root_bmin().x || p.x > tree_.root_bmax().x ||
                         p.y < tree_.root_bmin().y || p.y > tree_.root_bmax().y ||
                         p.z < tree_.root_bmin().z || p.z > tree_.root_bmax().z);
    if (outside_bbox && sign < 0.0) {
      sign = 1.0;
    }

    out_phi[i] = sign * dist;

    if (out_grad) {
      Vec3 grad = diff_norm > 1e-12 ? (diff / diff_norm) * sign : np;
      grad = grad.normalized();
      out_grad[3 * i + 0] = grad.x;
      out_grad[3 * i + 1] = grad.y;
      out_grad[3 * i + 2] = grad.z;
    }
  }
#ifdef _OPENMP
}
#endif

  return 0;
}

int32_t MeshSDF::winding_number(const double* points, int64_t n_points,
                                double* out_wn) const {
  if (!points || !out_wn || n_points <= 0) return -1;

  const auto& verts = tree_.vertices();
  const auto& faces = tree_.faces();
  size_t n_faces = faces.size();

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
  for (int64_t i = 0; i < n_points; ++i) {
    Vec3 p(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    double total_omega = 0.0;

    for (size_t f = 0; f < n_faces; ++f) {
      const auto& tri = faces[f];
      Vec3 ra = verts[tri[0]] - p;
      Vec3 rb = verts[tri[1]] - p;
      Vec3 rc = verts[tri[2]] - p;

      double da = ra.norm();
      double db = rb.norm();
      double dc = rc.norm();

      Vec3 rb_cross_rc = cross(rb, rc);
      double num = dot(ra, rb_cross_rc);

      double denom = da * db * dc + dot(ra, rb) * dc + dot(ra, rc) * db + dot(rb, rc) * da;
      double omega = 2.0 * std::atan2(num, denom);
      total_omega += omega;
    }

    out_wn[i] = total_omega / (4.0 * kPi);
  }

  return 0;
}

int32_t MeshSDF::voromesh_loss(
    const double* points, int64_t n_points,
    const double* seeds, int64_t n_seeds,
    int32_t k,
    double* out_loss, double* out_dists, double* out_grads,
    const double* seed_signs
) {
  if (!points || !seeds || n_points <= 0 || n_seeds < 2 || k <= 0) return -1;

  int32_t k_eff = static_cast<int32_t>(std::min<int64_t>(k, n_seeds - 1));

  // Convert seeds to Vec3
  std::vector<Vec3> s(n_seeds);
  for (int64_t i = 0; i < n_seeds; ++i) {
    s[i] = Vec3(seeds[3 * i], seeds[3 * i + 1], seeds[3 * i + 2]);
  }

  // Precompute k nearest neighbors for each seed
  std::vector<std::vector<int32_t>> seed_neighbors(n_seeds);
  for (int64_t i = 0; i < n_seeds; ++i) {
    std::vector<std::pair<double, int32_t>> dists;
    dists.reserve(n_seeds - 1);
    for (int64_t j = 0; j < n_seeds; ++j) {
      if (i == j) continue;
      dists.emplace_back((s[i] - s[j]).norm2(), static_cast<int32_t>(j));
    }
    std::partial_sort(dists.begin(), dists.begin() + k_eff, dists.end());
    seed_neighbors[i].resize(k_eff);
    for (int32_t n = 0; n < k_eff; ++n) {
      seed_neighbors[i][n] = dists[n].second;
    }
  }

  double loss_acc = 0.0;

#ifdef _OPENMP
#pragma omp parallel for reduction(+:loss_acc) schedule(dynamic, 64)
#endif
  for (int64_t p_idx = 0; p_idx < n_points; ++p_idx) {
    Vec3 x(points[3 * p_idx], points[3 * p_idx + 1], points[3 * p_idx + 2]);

    // 1. Find nearest seed
    int64_t nearest_seed = 0;
    double best_seed_dsq = (x - s[0]).norm2();
    for (int64_t i = 1; i < n_seeds; ++i) {
      double dsq = (x - s[i]).norm2();
      if (dsq < best_seed_dsq) {
        best_seed_dsq = dsq;
        nearest_seed = i;
      }
    }

    // 2. Evaluate bisector distance among k neighbors
    const auto& nbrs = seed_neighbors[nearest_seed];
    const Vec3& qi = s[nearest_seed];

    double min_h_abs = std::numeric_limits<double>::infinity();
    double best_h = 0.0;
    Vec3 best_n(0, 0, 1);

    for (int32_t j_idx : nbrs) {
      if (seed_signs != nullptr) {
        if (seed_signs[nearest_seed] * seed_signs[j_idx] >= 0.0) {
          continue;
        }
      }
      const Vec3& qj = s[j_idx];
      Vec3 mid = (qi + qj) * 0.5;
      Vec3 delta = qj - qi;
      double delta_len = delta.norm();
      if (delta_len < 1e-15) continue;
      Vec3 n = delta / delta_len;

      double h = dot(x - mid, n);
      double abs_h = std::abs(h);
      if (abs_h < min_h_abs) {
        min_h_abs = abs_h;
        best_h = h;
        best_n = n;
      }
    }

    if (seed_signs != nullptr && std::isinf(min_h_abs)) {
      // Fallback if none of the k neighbors had opposite sign
      for (int32_t j_idx : nbrs) {
        const Vec3& qj = s[j_idx];
        Vec3 mid = (qi + qj) * 0.5;
        Vec3 delta = qj - qi;
        double delta_len = delta.norm();
        if (delta_len < 1e-15) continue;
        Vec3 n = delta / delta_len;

        double h = dot(x - mid, n);
        double abs_h = std::abs(h);
        if (abs_h < min_h_abs) {
          min_h_abs = abs_h;
          best_h = h;
          best_n = n;
        }
      }
    }

    loss_acc += min_h_abs * min_h_abs;
    if (out_dists) out_dists[p_idx] = min_h_abs;
    if (out_grads) {
      double sign = best_h >= 0.0 ? 1.0 : -1.0;
      Vec3 grad = best_n * sign;
      out_grads[3 * p_idx + 0] = grad.x;
      out_grads[3 * p_idx + 1] = grad.y;
      out_grads[3 * p_idx + 2] = grad.z;
    }
  }

  if (out_loss) {
    *out_loss = loss_acc / static_cast<double>(n_points);
  }

  return 0;
}

}  // namespace cfd
