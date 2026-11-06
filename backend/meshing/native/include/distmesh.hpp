#pragma once

#include <vector>
#include <array>
#include <functional>
#include <cstdint>
#include <cmath>
#include "difvm_mesh.hpp"
#include "cfd_mesher_native.h"

namespace cfd {

struct DistMeshOptions {
  double h0 = 0.05;
  double h_min = 0.005;
  double h_max = 0.2;
  int32_t max_iters = 45;
  double dptol = 0.001;
  double delta_t = 0.22;
  double f_scale = 1.2;
};

// 2D Vector & Predicates for Delaunay & Ear-Clipping Triangulation
struct Vec2 {
  double x = 0.0;
  double y = 0.0;
  constexpr Vec2() = default;
  constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
  Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
  Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
  Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
  Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
  double norm2() const { return x * x + y * y; }
  double norm() const { return std::sqrt(norm2()); }
};

inline double orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Shoelace Formula & CCW Canonicalization
double computeShoelaceArea(const std::vector<Vec2>& poly);
bool canonicalizePolygonCCW(std::vector<Vec2>& poly, double* out_area = nullptr);

// Boundary Edge Subdivision
std::vector<Vec2> subdivideBoundaryEdges(
    const std::vector<Vec2>& poly,
    double h0,
    std::vector<uint8_t>& is_corner
);

// Ear-Clipping Triangulation (Held 2001 / Eberly)
std::vector<std::array<int32_t, 3>> earClippingTriangulate(
    const std::vector<Vec2>& poly
);

class DistMeshEngine {
 public:
  static void generateFromSurface(
      NativeMeshData& mesh,
      const double* surf_points,
      int64_t n_points,
      const int32_t* surf_triangles,
      int64_t n_triangles,
      const DistMeshOptions& options,
      const MeshingConfigC* config = nullptr
  );

  static void generate2DPolygon(
      NativeMeshData& mesh,
      const double* polygon_xy,
      int64_t n_vertices,
      const DistMeshOptions& options,
      const MeshingConfigC* config = nullptr
  );
};

} // namespace cfd
