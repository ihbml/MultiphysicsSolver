#pragma once

#include "difvm_mesh.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace cfd {

struct AABBNode {
  Vec3 bmin;
  Vec3 bmax;
  int32_t left = -1;
  int32_t right = -1;
  int32_t tri_start = -1;
  int32_t tri_count = 0;
};

class TriangleAABBTree {
 public:
  TriangleAABBTree() = default;
  TriangleAABBTree(const double* vertices, int64_t n_vertices,
                   const int32_t* faces, int64_t n_faces,
                   int32_t leaf_size = 8);

  void closest_point(const Vec3& p, int32_t prev_facet,
                     Vec3& out_q, int32_t& out_facet,
                     Vec3& out_bary, double& out_dist) const;

  const std::vector<Vec3>& vertices() const { return vertices_; }
  const std::vector<std::array<int32_t, 3>>& faces() const { return faces_; }
  const Vec3& root_bmin() const {
    static const Vec3 kZero(0, 0, 0);
    return nodes_.empty() ? kZero : nodes_[0].bmin;
  }
  const Vec3& root_bmax() const {
    static const Vec3 kZero(0, 0, 0);
    return nodes_.empty() ? kZero : nodes_[0].bmax;
  }

 private:
  std::vector<Vec3> vertices_;
  std::vector<std::array<int32_t, 3>> faces_;
  std::vector<Vec3> tri_bmin_;
  std::vector<Vec3> tri_bmax_;
  std::vector<Vec3> tri_centroids_;
  std::vector<int32_t> tri_indices_;
  std::vector<AABBNode> nodes_;

  int32_t build_recursive(int32_t start, int32_t count, int32_t leaf_size);
  void point_to_triangle(const Vec3& p, int32_t tid, Vec3& out_q,
                         Vec3& out_bary, double& out_dist) const;
};

class Pseudonormals {
 public:
  Pseudonormals() = default;
  Pseudonormals(const std::vector<Vec3>& vertices,
                const std::vector<std::array<int32_t, 3>>& faces);

  Vec3 evaluate(int32_t face_idx, const Vec3& bary, double eps = 1e-5) const;
  const Vec3& face_normal(int32_t face_idx) const { return face_normals_[face_idx]; }

 private:
  std::vector<std::array<int32_t, 3>> faces_;
  std::vector<Vec3> face_normals_;
  std::vector<Vec3> vertex_pseudonormals_;
  std::vector<Vec3> edge_pseudonormals_;
  std::vector<std::array<int32_t, 3>> face_edge_map_;
};

class MeshSDF {
 public:
  MeshSDF(const double* vertices, int64_t n_vertices,
          const int32_t* faces, int64_t n_faces);

  int32_t evaluate(const double* points, int64_t n_points,
                   double* out_phi, double* out_grad) const;

  int32_t winding_number(const double* points, int64_t n_points,
                         double* out_wn) const;

  static int32_t voromesh_loss(
      const double* points, int64_t n_points,
      const double* seeds, int64_t n_seeds,
      int32_t k,
      double* out_loss, double* out_dists, double* out_grads,
      const double* seed_signs = nullptr
  );

 private:
  TriangleAABBTree tree_;
  Pseudonormals pseudonormals_;
};

}  // namespace cfd
