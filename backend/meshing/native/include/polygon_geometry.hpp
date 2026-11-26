#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "difvm_mesh.hpp"

namespace cfd::hexcore {

struct QuantizedKey {
  int64_t x;
  int64_t y;
  int64_t z;

  bool operator==(const QuantizedKey& o) const noexcept {
    return x == o.x && y == o.y && z == o.z;
  }

  bool operator<(const QuantizedKey& o) const noexcept {
    if (x != o.x) return x < o.x;
    if (y != o.y) return y < o.y;
    return z < o.z;
  }
};

struct QuantizedKeyHash {
  std::size_t operator()(const QuantizedKey& k) const noexcept {
    std::size_t h1 = std::hash<int64_t>()(k.x);
    std::size_t h2 = std::hash<int64_t>()(k.y);
    std::size_t h3 = std::hash<int64_t>()(k.z);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

inline QuantizedKey quantize(const Vec3& p, double tol) noexcept {
  double inv = 1.0 / tol;
  return QuantizedKey{
      static_cast<int64_t>(std::llround(p.x * inv)),
      static_cast<int64_t>(std::llround(p.y * inv)),
      static_cast<int64_t>(std::llround(p.z * inv)),
  };
}

inline double computeTetAMIPS(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3) noexcept {
  Vec3 e1 = v1 - v0;
  Vec3 e2 = v2 - v0;
  Vec3 e3 = v3 - v0;
  double det_j = dot(cross(e1, e2), e3);
  if (det_j <= 1e-18) {
    return 1e30;
  }

  double edge_sum_sq = (v0 - v1).norm2() + (v0 - v2).norm2() + (v0 - v3).norm2() +
                       (v1 - v2).norm2() + (v1 - v3).norm2() + (v2 - v3).norm2();

  double ratio = (27.0 / 16.0) * (edge_sum_sq * edge_sum_sq * edge_sum_sq) / (det_j * det_j);
  return std::cbrt(ratio);
}

inline std::pair<Vec3, Vec3> computePolygonNormalAndCentroid(
    const int64_t* face_nodes,
    size_t n,
    const std::vector<Vec3>& points
) {
  if (!face_nodes || n < 3) {
    return {Vec3(0, 0, 0), Vec3(0, 0, 0)};
  }

  Vec3 c_face(0, 0, 0);
  for (size_t i = 0; i < n; ++i) c_face += points[static_cast<size_t>(face_nodes[i])];
  c_face /= static_cast<double>(n);

  Vec3 normal_sum(0, 0, 0);
  Vec3 centroid_sum(0, 0, 0);
  double total_area = 0.0;

  for (size_t i = 0; i < n; ++i) {
    const Vec3& pi = points[static_cast<size_t>(face_nodes[i])];
    const Vec3& pj = points[static_cast<size_t>(face_nodes[(i + 1) % n])];
    Vec3 cross_p = cross(pi - c_face, pj - c_face);
    double area_tri = cross_p.norm() * 0.5;
    normal_sum += cross_p * 0.5;
    Vec3 tri_c = (c_face + pi + pj) / 3.0;
    centroid_sum += tri_c * area_tri;
    total_area += area_tri;
  }

  Vec3 centroid = (total_area > 1e-15) ? (centroid_sum / total_area) : c_face;
  return {normal_sum, centroid};
}

inline std::pair<Vec3, Vec3> computePolygonNormalAndCentroid(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points
) {
  return computePolygonNormalAndCentroid(face_nodes.data(), face_nodes.size(), points);
}

inline std::pair<Vec3, Vec3> computePolygonNormalAndCentroidReplaced(
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
    return (nid == replaced_node) ? replaced_pos : points[static_cast<size_t>(nid)];
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

  Vec3 centroid = (total_area > 1e-15) ? (centroid_sum / total_area) : c_face;
  return {normal_sum, centroid};
}

inline bool isFaceFolded(
    const int64_t* face_nodes,
    size_t n,
    const std::vector<Vec3>& points,
    const Vec3& normal_sum
) {
  if (!face_nodes || n < 3) return false;
  double norm_mag = normal_sum.norm();
  if (norm_mag <= 1e-15) return false;

  Vec3 c_face(0, 0, 0);
  for (size_t i = 0; i < n; ++i) c_face += points[static_cast<size_t>(face_nodes[i])];
  c_face /= static_cast<double>(n);

  for (size_t i = 0; i < n; ++i) {
    const Vec3& pi = points[static_cast<size_t>(face_nodes[i])];
    const Vec3& pj = points[static_cast<size_t>(face_nodes[(i + 1) % n])];
    Vec3 cross_p = cross(pi - c_face, pj - c_face);
    if (cross_p.norm() > 1e-15) {
      if (dot(cross_p, normal_sum) <= 0.0) return true;
    }
  }
  return false;
}

inline bool isFaceFolded(
    const std::vector<int64_t>& face_nodes,
    const std::vector<Vec3>& points,
    const Vec3& normal_sum
) {
  return isFaceFolded(face_nodes.data(), face_nodes.size(), points, normal_sum);
}

}  // namespace cfd::hexcore
