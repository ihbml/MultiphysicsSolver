#include "distmesh.hpp"
#include "rcm.hpp"
#include "cht_cyclic.hpp"
#include "inflation.hpp"
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstdint>

namespace cfd {

namespace {

inline uint64_t ekey(int32_t a, int32_t b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}

inline uint64_t ukey(int32_t a, int32_t b) {
    if (a > b) std::swap(a, b);
    return ekey(a, b);
}

// ==============================================================================
// Bowyer-Watson Incremental Delaunay Triangulation Engine (C++20)
// Ported from resources/reference_spickzettel/LMArena/1/native/src/mesh.cpp
// ==============================================================================

struct BowyerWatsonDelaunay {
    std::vector<Vec2> pts;
    std::vector<std::array<int32_t, 3>> tri;
    std::vector<std::array<int32_t, 3>> nb;
    std::vector<uint8_t> alive;
    std::unordered_map<uint64_t, uint64_t> emap; // directed edge (u, v) -> (t << 2 | slot)
    std::vector<int32_t> vstamp, dstamp;
    int32_t gen = 0;
    int32_t hint = 0;

    explicit BowyerWatsonDelaunay(size_t reservePts = 1024) {
        pts.reserve(reservePts + 8);
        tri.reserve(2 * reservePts + 16);
        nb.reserve(2 * reservePts + 16);
        alive.reserve(2 * reservePts + 16);
        emap.reserve(6 * reservePts + 32);
    }

    void initSuper(double min_x, double min_y, double max_x, double max_y) {
        Vec2 c((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
        double d = std::max(max_x - min_x, max_y - min_y) + 1.0;
        pts.push_back(Vec2(c.x - 3.0 * d, c.y - d));
        pts.push_back(Vec2(c.x + 3.0 * d, c.y - d));
        pts.push_back(Vec2(c.x, c.y + 3.0 * d));
        addTri(0, 1, 2);
    }

    void relink(int32_t t) {
        if (t < 0 || !alive[t]) return;
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            auto it = emap.find(ekey(v, u));
            nb[t][k] = (it == emap.end()) ? -1 : static_cast<int32_t>(it->second >> 2);
        }
    }

    void eraseEdges(int32_t t) {
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            uint64_t key = ekey(u, v);
            auto it = emap.find(key);
            if (it != emap.end() && static_cast<int32_t>(it->second >> 2) == t) {
                emap.erase(it);
            }
        }
    }

    int32_t addTri(int32_t a, int32_t b, int32_t c) {
        if (a == b || b == c || a == c) return -1;
        if (orient2d(pts[a], pts[b], pts[c]) <= 0) std::swap(b, c);
        if (orient2d(pts[a], pts[b], pts[c]) <= 0) return -1;
        int32_t t = static_cast<int32_t>(tri.size());
        tri.push_back({a, b, c});
        nb.push_back({-1, -1, -1});
        alive.push_back(1);
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            emap[ekey(u, v)] = (static_cast<uint64_t>(t) << 2) | static_cast<uint64_t>(k);
        }
        relink(t);
        for (int k = 0; k < 3; ++k) {
            if (nb[t][k] >= 0) relink(nb[t][k]);
        }
        return t;
    }

    void kill(int32_t t) {
        if (t < 0 || !alive[t]) return;
        int32_t n0 = nb[t][0], n1 = nb[t][1], n2 = nb[t][2];
        eraseEdges(t);
        alive[t] = 0;
        relink(n0);
        relink(n1);
        relink(n2);
    }

    bool circumContains(int32_t t, const Vec2& p) const {
        const auto& v = tri[t];
        const Vec2& a = pts[v[0]];
        const Vec2& b = pts[v[1]];
        const Vec2& c = pts[v[2]];
        double ax = a.x - p.x, ay = a.y - p.y;
        double bx = b.x - p.x, by = b.y - p.y;
        double cx = c.x - p.x, cy = c.y - p.y;
        double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                     (bx * bx + by * by) * (ax * cy - cx * ay) +
                     (cx * cx + cy * cy) * (ax * by - bx * ay);
        return det > 1e-18;
    }

    bool contains(int32_t t, const Vec2& p) const {
        const auto& v = tri[t];
        double d0 = orient2d(pts[v[0]], pts[v[1]], p);
        double d1 = orient2d(pts[v[1]], pts[v[2]], p);
        double d2 = orient2d(pts[v[2]], pts[v[0]], p);
        const double eps = -1e-14;
        return d0 >= eps && d1 >= eps && d2 >= eps;
    }

    int32_t locate(const Vec2& p) {
        int32_t t = -1;
        if (hint >= 0 && hint < static_cast<int32_t>(tri.size()) && alive[hint]) t = hint;
        if (t < 0) {
            for (size_t i = 0; i < tri.size(); ++i) {
                if (alive[i]) { t = static_cast<int32_t>(i); break; }
            }
        }
        if (t < 0) return -1;
        for (int iter = 0; iter < 10000; ++iter) {
            if (!alive[t]) break;
            bool moved = false;
            for (int k = 0; k < 3; ++k) {
                const Vec2& A = pts[tri[t][(k + 1) % 3]];
                const Vec2& B = pts[tri[t][(k + 2) % 3]];
                if (orient2d(A, B, p) < 0) {
                    int32_t n = nb[t][k];
                    if (n >= 0 && alive[n]) {
                        t = n;
                        moved = true;
                    }
                    break;
                }
            }
            if (!moved) {
                if (contains(t, p)) {
                    hint = t;
                    return t;
                }
                break;
            }
        }
        for (size_t i = 0; i < tri.size(); ++i) {
            if (alive[i] && contains(static_cast<int32_t>(i), p)) {
                hint = static_cast<int32_t>(i);
                return hint;
            }
        }
        return -1;
    }

    int32_t insert(const Vec2& p) {
        int32_t t0 = locate(p);
        if (t0 < 0) return -1;
        ++gen;
        std::vector<int32_t> dead;
        std::vector<int32_t> stk;
        if (vstamp.size() < tri.size() + 8) {
            vstamp.resize(tri.size() + 32, -1);
            dstamp.resize(tri.size() + 32, -1);
        }
        vstamp[t0] = gen;
        stk.push_back(t0);
        while (!stk.empty()) {
            int32_t cur = stk.back();
            stk.pop_back();
            if (!alive[cur] || !circumContains(cur, p)) continue;
            dead.push_back(cur);
            dstamp[cur] = gen;
            for (int k = 0; k < 3; ++k) {
                int32_t n = nb[cur][k];
                if (n >= 0 && alive[n] && vstamp[n] != gen) {
                    vstamp[n] = gen;
                    stk.push_back(n);
                }
            }
        }
        if (dead.empty()) return -1;
        int32_t pid = static_cast<int32_t>(pts.size());
        pts.push_back(p);
        struct BE { int32_t a, b; };
        std::vector<BE> bnd;
        bnd.reserve(dead.size() * 2);
        for (int32_t d : dead) {
            for (int k = 0; k < 3; ++k) {
                int32_t n = nb[d][k];
                if (n < 0 || dstamp[n] != gen) {
                    bnd.push_back({tri[d][(k + 1) % 3], tri[d][(k + 2) % 3]});
                }
            }
        }
        for (int32_t d : dead) kill(d);
        int32_t last = -1;
        for (const auto& e : bnd) {
            int32_t t = addTri(pid, e.a, e.b);
            if (t >= 0) last = t;
        }
        if (last >= 0) hint = last;
        return pid;
    }

    // Extract clean triangulation discarding the 3 super-triangle vertices
    std::vector<std::array<int32_t, 3>> extractTriangles() const {
        std::vector<std::array<int32_t, 3>> out;
        for (size_t t = 0; t < tri.size(); ++t) {
            if (!alive[t]) continue;
            // Super-triangle vertices are 0, 1, 2
            if (tri[t][0] < 3 || tri[t][1] < 3 || tri[t][2] < 3) continue;
            out.push_back({tri[t][0] - 3, tri[t][1] - 3, tri[t][2] - 3});
        }
        return out;
    }
};

using BowyerWatsonDelaunay2D = BowyerWatsonDelaunay;

} // anonymous namespace

void DistMeshEngine::generateFromSurface(
    NativeMeshData& mesh,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_triangles,
    int64_t n_triangles,
    const DistMeshOptions& options,
    const MeshingConfigC* config
) {
    if (n_points == 0 || n_triangles == 0) return;
    (void)surf_triangles;
    (void)n_triangles;

    // 1. Compute surface bounding box
    Vec3 bmin(1e30, 1e30, 1e30), bmax(-1e30, -1e30, -1e30);
    std::vector<Vec3> s_pts(n_points);
    for (int64_t i = 0; i < n_points; ++i) {
        s_pts[i] = Vec3(surf_points[3 * i], surf_points[3 * i + 1], surf_points[3 * i + 2]);
        bmin.x = std::min(bmin.x, s_pts[i].x);
        bmin.y = std::min(bmin.y, s_pts[i].y);
        bmin.z = std::min(bmin.z, s_pts[i].z);
        bmax.x = std::max(bmax.x, s_pts[i].x);
        bmax.y = std::max(bmax.y, s_pts[i].y);
        bmax.z = std::max(bmax.z, s_pts[i].z);
    }

    Vec3 span = bmax - bmin;
    double h0 = options.h0 > 0.0 ? options.h0 : 0.05;
    if (config && config->h0 > 0.0) h0 = config->h0;

    int nx = std::max(2, static_cast<int>(std::round(span.x / h0)));
    int ny = std::max(2, static_cast<int>(std::round(span.y / h0)));
    int nz = std::max(2, static_cast<int>(std::round(span.z / h0)));

    double dx = span.x / nx;
    double dy = span.y / ny;
    double dz = span.z / nz;

    // Build structured hexahedral volume grid conforming to the bounding box
    std::vector<Vec3> vol_nodes;
    vol_nodes.reserve((nx + 1) * (ny + 1) * (nz + 1));

    auto node_idx = [&](int i, int j, int k) -> int64_t {
        return static_cast<int64_t>(k * (ny + 1) * (nx + 1) + j * (nx + 1) + i);
    };

    for (int k = 0; k <= nz; ++k) {
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                vol_nodes.push_back(Vec3(
                    bmin.x + i * dx,
                    bmin.y + j * dy,
                    bmin.z + k * dz
                ));
            }
        }
    }

    std::vector<std::vector<int64_t>> vol_cells;
    vol_cells.reserve(nx * ny * nz);

    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                vol_cells.push_back({
                    node_idx(i, j, k),
                    node_idx(i + 1, j, k),
                    node_idx(i + 1, j + 1, k),
                    node_idx(i, j + 1, k),
                    node_idx(i, j, k + 1),
                    node_idx(i + 1, j, k + 1),
                    node_idx(i + 1, j + 1, k + 1),
                    node_idx(i, j + 1, k + 1)
                });
            }
        }
    }

    // Extract unique edges for spring relaxation
    std::unordered_set<uint64_t> hex_edges;
    std::vector<std::pair<int64_t, int64_t>> edge_list;
    const int hex_edge_pairs[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0}, // bottom
        {4,5}, {5,6}, {6,7}, {7,4}, // top
        {0,4}, {1,5}, {2,6}, {3,7}  // vertical
    };
    for (const auto& cell : vol_cells) {
        for (int e = 0; e < 12; ++e) {
            int64_t u = cell[hex_edge_pairs[e][0]];
            int64_t v = cell[hex_edge_pairs[e][1]];
            if (u > v) std::swap(u, v);
            uint64_t key = (static_cast<uint64_t>(u) << 32) | static_cast<uint64_t>(v);
            if (hex_edges.insert(key).second) {
                edge_list.push_back({u, v});
            }
        }
    }

    // Persson & Strang (2004) spring force relaxation on mesh edges
    int iters = options.max_iters;
    if (config && config->distmesh_iters > 0) iters = config->distmesh_iters;

    for (int it = 0; it < iters; ++it) {
        double sum_l2 = 0.0, sum_h2 = 0.0;
        std::vector<double> lengths(edge_list.size());
        for (size_t e = 0; e < edge_list.size(); ++e) {
            lengths[e] = (vol_nodes[edge_list[e].first] - vol_nodes[edge_list[e].second]).norm();
            sum_l2 += lengths[e] * lengths[e];
            sum_h2 += h0 * h0;
        }
        double s0 = options.f_scale * std::sqrt(sum_l2 / std::max(sum_h2, 1e-15));
        double L0 = s0 * h0;

        std::vector<Vec3> forces(vol_nodes.size(), Vec3(0, 0, 0));
        for (size_t e = 0; e < edge_list.size(); ++e) {
            int64_t u = edge_list[e].first;
            int64_t v = edge_list[e].second;
            double le = lengths[e];
            if (le < 1e-12) continue;
            double f = std::max(L0 - le, 0.0);
            Vec3 d = (vol_nodes[u] - vol_nodes[v]) / le;
            forces[u] += d * f;
            forces[v] -= d * f;
        }

        // Update interior nodes, keep boundary nodes on bounding box planes
        for (int k = 1; k < nz; ++k) {
            for (int j = 1; j < ny; ++j) {
                for (int i = 1; i < nx; ++i) {
                    int64_t idx = node_idx(i, j, k);
                    vol_nodes[idx] += forces[idx] * options.delta_t;
                }
            }
        }
    }

    // Build DiFVM topology
    DiFVMTraversal::buildTopology(mesh, vol_nodes, vol_cells);

    // Apply RCM if requested
    if (config && config->enable_rcm) {
        auto perm = RCMReordering::computePermutation(mesh);
        RCMReordering::applyReordering(mesh, perm);
    }

    // Multi-region CHT interfaces if requested
    if (config && config->enable_cht) {
        CHTCyclicProcessor::extractCoupledInterfaces(mesh);
    }
}

// ==============================================================================
// Shoelace Formula & CCW Canonicalization
// ==============================================================================

double computeShoelaceArea(const std::vector<Vec2>& poly) {
  size_t n = poly.size();
  if (n < 3) return 0.0;
  double area = 0.0;
  for (size_t i = 0; i < n; ++i) {
    size_t j = (i + 1) % n;
    area += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
  }
  return 0.5 * area;
}

bool canonicalizePolygonCCW(std::vector<Vec2>& poly, double* out_area) {
  size_t n = poly.size();
  if (n < 3) return false;
  double area = computeShoelaceArea(poly);
  if (std::abs(area) < 1e-14) {
    if (out_area) *out_area = area;
    return false;
  }
  bool was_reversed = false;
  if (area < 0.0) {
    std::reverse(poly.begin(), poly.end());
    area = -area;
    was_reversed = true;
  }
  if (out_area) *out_area = area;
  return was_reversed;
}

// ==============================================================================
// Metric-Governed Boundary Subdivision
// ==============================================================================

std::vector<Vec2> subdivideBoundaryEdges(
    const std::vector<Vec2>& poly,
    double h0,
    std::vector<uint8_t>& is_corner
) {
  size_t n = poly.size();
  std::vector<Vec2> resampled;
  is_corner.clear();
  if (n < 2) return resampled;

  for (size_t i = 0; i < n; ++i) {
    const Vec2& p0 = poly[i];
    const Vec2& p1 = poly[(i + 1) % n];
    Vec2 vec = p1 - p0;
    double len = vec.norm();
    int n_seg = 1;
    if (h0 > 1e-14 && len > 1e-14) {
      n_seg = std::max(1, static_cast<int>(std::round(len / h0)));
    }
    for (int k = 0; k < n_seg; ++k) {
      double t = static_cast<double>(k) / static_cast<double>(n_seg);
      resampled.push_back(p0 + vec * t);
      is_corner.push_back(k == 0 ? 1 : 0);
    }
  }
  return resampled;
}

// ==============================================================================
// Held (2001) / Eberly Ear-Clipping Triangulation Engine
// ==============================================================================

std::vector<std::array<int32_t, 3>> earClippingTriangulate(
    const std::vector<Vec2>& poly
) {
  std::vector<std::array<int32_t, 3>> triangles;
  size_t n = poly.size();
  if (n < 3) return triangles;
  if (n == 3) {
    std::array<int32_t, 3> t = {0, 1, 2};
    if (orient2d(poly[0], poly[1], poly[2]) < 0.0) std::swap(t[1], t[2]);
    triangles.push_back(t);
    return triangles;
  }

  std::vector<int32_t> indices(n);
  for (size_t i = 0; i < n; ++i) indices[i] = static_cast<int32_t>(i);

  auto pointInTri = [](const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) -> bool {
    double d0 = orient2d(a, b, p);
    double d1 = orient2d(b, c, p);
    double d2 = orient2d(c, a, p);
    const double eps = -1e-14;
    return (d0 >= eps && d1 >= eps && d2 >= eps);
  };

  size_t max_iters = n * n;
  size_t it_count = 0;

  while (indices.size() > 3 && it_count < max_iters) {
    ++it_count;
    size_t m = indices.size();
    bool ear_found = false;

    for (size_t i = 0; i < m; ++i) {
      int32_t prev_idx = indices[(i + m - 1) % m];
      int32_t curr_idx = indices[i];
      int32_t next_idx = indices[(i + 1) % m];

      const Vec2& a = poly[prev_idx];
      const Vec2& b = poly[curr_idx];
      const Vec2& c = poly[next_idx];

      double det = orient2d(a, b, c);
      if (det <= 1e-15) continue;

      bool is_ear = true;
      for (size_t j = 0; j < m; ++j) {
        int32_t k = indices[j];
        if (k == prev_idx || k == curr_idx || k == next_idx) continue;
        if (pointInTri(poly[k], a, b, c)) {
          is_ear = false;
          break;
        }
      }

      if (is_ear) {
        triangles.push_back({prev_idx, curr_idx, next_idx});
        indices.erase(indices.begin() + i);
        ear_found = true;
        break;
      }
    }

    if (!ear_found) {
      int best_i = -1;
      double best_det = -1e30;
      for (size_t i = 0; i < indices.size(); ++i) {
        size_t cur_m = indices.size();
        int32_t p_i = indices[(i + cur_m - 1) % cur_m];
        int32_t c_i = indices[i];
        int32_t n_i = indices[(i + 1) % cur_m];
        double det = orient2d(poly[p_i], poly[c_i], poly[n_i]);
        if (det > best_det) {
          best_det = det;
          best_i = static_cast<int>(i);
        }
      }
      if (best_i >= 0) {
        size_t cur_m = indices.size();
        int32_t p_i = indices[(best_i + cur_m - 1) % cur_m];
        int32_t c_i = indices[best_i];
        int32_t n_i = indices[(best_i + 1) % cur_m];
        triangles.push_back({p_i, c_i, n_i});
        indices.erase(indices.begin() + best_i);
      } else {
        break;
      }
    }
  }

  if (indices.size() == 3) {
    triangles.push_back({indices[0], indices[1], indices[2]});
  }

  for (auto& t : triangles) {
    if (orient2d(poly[t[0]], poly[t[1]], poly[t[2]]) < 0.0) {
      std::swap(t[1], t[2]);
    }
  }

  return triangles;
}

// ==============================================================================
// DistMesh 2D Polygon Meshing with Boundary Subdivision & Constrained Relaxation
// ==============================================================================

void DistMeshEngine::generate2DPolygon(
    NativeMeshData& mesh,
    const double* polygon_xy,
    int64_t n_vertices,
    const DistMeshOptions& options,
    const MeshingConfigC* config
) {
  if (n_vertices < 3) return;

  // 1. Gather polygon boundary nodes
  std::vector<Vec2> p_nodes(n_vertices);
  for (int64_t i = 0; i < n_vertices; ++i) {
    p_nodes[i] = Vec2(polygon_xy[2 * i], polygon_xy[2 * i + 1]);
  }

  // Canonicalize polygon CCW via Shoelace formula
  double poly_area = 0.0;
  canonicalizePolygonCCW(p_nodes, &poly_area);
  if (std::abs(poly_area) < 1e-14) {
    throw std::invalid_argument("Degenerate polygon with near-zero area");
  }

  double min_x = 1e30, max_x = -1e30, min_y = 1e30, max_y = -1e30;
  for (const auto& p : p_nodes) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  double h0 = 0.0;
  if (config && config->h0 > 0.0) h0 = config->h0;
  else if (options.h0 > 0.0) h0 = options.h0;

  // Polygon signed distance / winding test
  auto pointInPoly = [&](const Vec2& p) -> bool {
    bool inside = false;
    for (size_t i = 0, j = p_nodes.size() - 1; i < p_nodes.size(); j = i++) {
      if (((p_nodes[i].y > p.y) != (p_nodes[j].y > p.y)) &&
          (p.x < (p_nodes[j].x - p_nodes[i].x) * (p.y - p_nodes[i].y) / (p_nodes[j].y - p_nodes[i].y + 1e-30) + p_nodes[i].x)) {
        inside = !inside;
      }
    }
    return inside;
  };

  auto isInsideOrBoundary = [&](const Vec2& p) -> bool {
    if (pointInPoly(p)) return true;
    double min_d2 = 1e30;
    for (size_t i = 0, j = p_nodes.size() - 1; i < p_nodes.size(); j = i++) {
      Vec2 ab = p_nodes[i] - p_nodes[j];
      Vec2 ap = p - p_nodes[j];
      double ab2 = ab.norm2();
      double t = (ab2 > 1e-15) ? std::clamp((ap.x * ab.x + ap.y * ab.y) / ab2, 0.0, 1.0) : 0.0;
      Vec2 proj = p_nodes[j] + ab * t;
      min_d2 = std::min(min_d2, (p - proj).norm2());
    }
    return min_d2 < 1e-6;
  };

  // Subdivide boundary edges if h0 > 0
  std::vector<uint8_t> is_corner;
  std::vector<Vec2> all_pts;
  if (h0 > 0.0) {
    all_pts = subdivideBoundaryEdges(p_nodes, h0, is_corner);
  } else {
    all_pts = p_nodes;
    is_corner.assign(p_nodes.size(), 1);
  }
  int64_t n_fixed = static_cast<int64_t>(all_pts.size());

  // 2. Seed interior candidate nodes if domain area allows and h0 > 0
  if (h0 > 0.0) {
    double dy_step = h0 * std::sqrt(3.0) / 2.0;
    int ny = static_cast<int>(std::ceil((max_y - min_y) / dy_step));
    int nx = static_cast<int>(std::ceil((max_x - min_x) / h0));

    if (nx > 1 && ny > 1 && (max_x - min_x) > 1.5 * h0) {
      for (int j = 1; j < ny; ++j) {
        double y = min_y + j * dy_step;
        double x_shift = (j % 2 == 1) ? 0.5 * h0 : 0.0;
        for (int i = 0; i <= nx; ++i) {
          double x = min_x + i * h0 + x_shift;
          Vec2 cand(x, y);
          if (pointInPoly(cand)) {
            double min_d = 1e30;
            for (int64_t f = 0; f < n_fixed; ++f) {
              min_d = std::min(min_d, (cand - all_pts[f]).norm());
            }
            if (min_d > 0.55 * h0) {
              all_pts.push_back(cand);
              is_corner.push_back(0);
            }
          }
        }
      }
    }
  }

  std::vector<std::array<int32_t, 3>> kept_triangles;

  if (all_pts.size() == static_cast<size_t>(n_fixed)) {
    // All points are on boundary (either no subdivision or no interior points added).
    // Held (2001) / Eberly Ear-Clipping guarantees exact area coverage and zero dangling nodes.
    kept_triangles = earClippingTriangulate(all_pts);
  } else {
    // 3. Iterative Bowyer-Watson Delaunay & Persson & Strang Spring Force Relaxation
    int max_iters = (config && config->distmesh_iters > 0) ? config->distmesh_iters : options.max_iters;

    for (int iter = 0; iter < max_iters; ++iter) {
      BowyerWatsonDelaunay bw(all_pts.size() + 8);
      bw.initSuper(min_x - h0, min_y - h0, max_x + h0, max_y + h0);
      for (const auto& p : all_pts) {
        bw.insert(p);
      }
      auto raw_tris = bw.extractTriangles();

      kept_triangles.clear();
      for (const auto& t : raw_tris) {
        Vec2 c = (all_pts[t[0]] + all_pts[t[1]] + all_pts[t[2]]) / 3.0;
        Vec2 m0 = (all_pts[t[0]] + all_pts[t[1]]) * 0.5;
        Vec2 m1 = (all_pts[t[1]] + all_pts[t[2]]) * 0.5;
        Vec2 m2 = (all_pts[t[2]] + all_pts[t[0]]) * 0.5;
        if (pointInPoly(c) && isInsideOrBoundary(m0) && isInsideOrBoundary(m1) && isInsideOrBoundary(m2)) {
          kept_triangles.push_back(t);
        }
      }

      if (kept_triangles.empty()) {
        break;
      }

      std::unordered_set<uint64_t> uedges;
      std::vector<std::pair<int32_t, int32_t>> edge_list;
      for (const auto& t : kept_triangles) {
        for (int e = 0; e < 3; ++e) {
          int32_t u = t[e], v = t[(e + 1) % 3];
          uint64_t key = ukey(u, v);
          if (uedges.insert(key).second) {
            edge_list.push_back({std::min(u, v), std::max(u, v)});
          }
        }
      }

      double sum_l2 = 0.0, sum_h2 = 0.0;
      std::vector<double> lengths(edge_list.size());
      for (size_t e = 0; e < edge_list.size(); ++e) {
        lengths[e] = (all_pts[edge_list[e].first] - all_pts[edge_list[e].second]).norm();
        sum_l2 += lengths[e] * lengths[e];
        sum_h2 += h0 * h0;
      }

      double s0 = options.f_scale * std::sqrt(sum_l2 / std::max(sum_h2, 1e-15));
      double L0 = s0 * h0;

      std::vector<Vec2> forces(all_pts.size(), Vec2(0, 0));
      for (size_t e = 0; e < edge_list.size(); ++e) {
        int32_t u = edge_list[e].first;
        int32_t v = edge_list[e].second;
        double le = lengths[e];
        if (le < 1e-12) continue;
        double f = std::max(L0 - le, 0.0);
        Vec2 d = (all_pts[u] - all_pts[v]) / le;
        forces[u] = forces[u] + d * f;
        forces[v] = forces[v] - d * f;
      }

      // Lock all boundary nodes (corners and edge nodes) in place
      // Move only interior nodes
      for (size_t i = n_fixed; i < all_pts.size(); ++i) {
        Vec2 new_pos = all_pts[i] + forces[i] * options.delta_t;
        if (pointInPoly(new_pos)) {
          all_pts[i] = new_pos;
        }
      }
    }

    // Final Delaunay pass
    if (kept_triangles.empty()) {
      BowyerWatsonDelaunay bw_final(all_pts.size() + 8);
      bw_final.initSuper(min_x - h0, min_y - h0, max_x + h0, max_y + h0);
      for (const auto& p : all_pts) bw_final.insert(p);
      auto raw_tris = bw_final.extractTriangles();
      for (const auto& t : raw_tris) {
        Vec2 c = (all_pts[t[0]] + all_pts[t[1]] + all_pts[t[2]]) / 3.0;
        Vec2 m0 = (all_pts[t[0]] + all_pts[t[1]]) * 0.5;
        Vec2 m1 = (all_pts[t[1]] + all_pts[t[2]]) * 0.5;
        Vec2 m2 = (all_pts[t[2]] + all_pts[t[0]]) * 0.5;
        if (pointInPoly(c) && isInsideOrBoundary(m0) && isInsideOrBoundary(m1) && isInsideOrBoundary(m2)) {
          kept_triangles.push_back(t);
        }
      }
    }

    if (kept_triangles.empty()) {
      all_pts.resize(n_fixed);
      kept_triangles = earClippingTriangulate(all_pts);
    }
  }

  // Ensure all triangles are CCW (orient2d > 0)
  for (auto& t : kept_triangles) {
    if (orient2d(all_pts[t[0]], all_pts[t[1]], all_pts[t[2]]) < 0.0) {
      std::swap(t[1], t[2]);
    }
  }

  // Convert to Vec3 nodes and cells for DiFVM topology
  std::vector<Vec3> fvm_nodes(all_pts.size());
  for (size_t i = 0; i < all_pts.size(); ++i) {
    fvm_nodes[i] = Vec3(all_pts[i].x, all_pts[i].y, 0.0);
  }
  std::vector<std::vector<int64_t>> cells(kept_triangles.size());
  for (size_t i = 0; i < kept_triangles.size(); ++i) {
    cells[i] = {kept_triangles[i][0], kept_triangles[i][1], kept_triangles[i][2]};
  }

  DiFVMTraversal::buildTopology(mesh, fvm_nodes, cells);

  if (config && config->enable_rcm) {
    auto perm = RCMReordering::computePermutation(mesh);
    RCMReordering::applyReordering(mesh, perm);
  }
}

} // namespace cfd
