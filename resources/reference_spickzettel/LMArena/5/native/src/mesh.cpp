// Unstructured FVM mesh generation and the DiFVM-style static graph build.
//
// Pipeline
//   1. adaptive resampling of the SDF contour loops to the local size field
//   2. optional stretched boundary-layer node rows (geometric growth)
//   3. variable-radius dart throwing for the interior nodes
//   4. incremental Bowyer-Watson Delaunay with a directed-edge hash table
//   5. constraint enforcement by flip-to-insert (with midpoint split fallback)
//   6. removal of exterior cells through the signed distance field
//   7. constrained Laplacian smoothing
//   8. face/cell topology -> owner-neighbour CSR, face area vectors,
//      over-relaxed non-orthogonality correction (the static graph arrays a
//      vectorised gather/scatter FV kernel needs, cf. DiFVM [W3]).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "core.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mesher {
namespace {

inline uint64_t ekey(int a, int b) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
inline uint64_t ukey(int a, int b) {
  if (a > b) std::swap(a, b);
  return ekey(a, b);
}

// ---------------------------------------------------------------- Delaunay ----
struct Delaunay {
  std::vector<V2> pts;
  std::vector<std::array<int, 3>> tri;
  std::vector<std::array<int, 3>> nb;
  std::vector<char> alive;
  std::unordered_map<uint64_t, uint64_t> emap;  // directed edge -> (t<<2|slot)
  std::vector<int> vstamp, dstamp;
  std::vector<int> nodeAny;
  int gen = 0;
  int hint = 0;
  // centroid bucket grid for robust triangle location
  std::vector<int> cgHead;
  V2 cgLo{0, 0};
  Real cgCell = 1;
  int cgNx = 1, cgNy = 1;

  explicit Delaunay(size_t reservePts) {
    pts.reserve(reservePts + 8);
    tri.reserve(2 * reservePts + 16);
    nb.reserve(2 * reservePts + 16);
    alive.reserve(2 * reservePts + 16);
    emap.reserve(6 * reservePts + 32);
  }

  void initSuper(const AABB& box) {
    const V2 c = box.center();
    const Real d = std::max(box.hi.x - box.lo.x, box.hi.y - box.lo.y) + 1e-9;
    pts.push_back(V2(c.x - 3 * d, c.y - d));
    pts.push_back(V2(c.x + 3 * d, c.y - d));
    pts.push_back(V2(c.x, c.y + 3 * d));
    addTri(0, 1, 2);
  }

  void relink(int t) {
    if (t < 0 || !alive[t]) return;
    for (int k = 0; k < 3; ++k) {
      const int u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
      auto it = emap.find(ekey(v, u));
      nb[t][k] = (it == emap.end()) ? -1 : static_cast<int>(it->second >> 2);
    }
  }

  void eraseEdges(int t) {
    for (int k = 0; k < 3; ++k) {
      const int u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
      const uint64_t key = ekey(u, v);
      auto it = emap.find(key);
      if (it != emap.end() && static_cast<int>(it->second >> 2) == t) emap.erase(it);
    }
  }

  int addTri(int a, int b, int c) {
    if (a == b || b == c || a == c) return -1;
    if (orient2d(pts[a], pts[b], pts[c]) <= 0) std::swap(b, c);
    if (orient2d(pts[a], pts[b], pts[c]) <= 0) return -1;
    const int t = static_cast<int>(tri.size());
    tri.push_back({a, b, c});
    nb.push_back({-1, -1, -1});
    alive.push_back(1);
    for (int k = 0; k < 3; ++k) {
      const int u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
      emap[ekey(u, v)] = (static_cast<uint64_t>(t) << 2) | static_cast<uint64_t>(k);
    }
    relink(t);
    for (int k = 0; k < 3; ++k)
      if (nb[t][k] >= 0) relink(nb[t][k]);
    if (nodeAny.size() < pts.size()) nodeAny.resize(pts.size(), -1);
    nodeAny[tri[t][0]] = t;
    nodeAny[tri[t][1]] = t;
    nodeAny[tri[t][2]] = t;
    return t;
  }

  void kill(int t) {
    if (t < 0 || !alive[t]) return;
    const int n0 = nb[t][0], n1 = nb[t][1], n2 = nb[t][2];
    eraseEdges(t);
    alive[t] = 0;
    relink(n0);
    relink(n1);
    relink(n2);
  }

  bool hasEdge(int a, int b) const {
    return emap.count(ekey(a, b)) || emap.count(ekey(b, a));
  }

  bool circumContains(int t, const V2& p) const {
    const auto& v = tri[t];
    const V2& a = pts[v[0]];
    const V2& b = pts[v[1]];
    const V2& c = pts[v[2]];
    const Real ax = a.x - p.x, ay = a.y - p.y;
    const Real bx = b.x - p.x, by = b.y - p.y;
    const Real cx = c.x - p.x, cy = c.y - p.y;
    const Real det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                     (bx * bx + by * by) * (ax * cy - cx * ay) +
                     (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det > 1e-18;
  }

  void rebuildCentroidGrid() {
    const size_t nt = tri.size();
    if (!nt) return;
    const Real cell = std::max(1e-9, cgCell);
    cgHead.assign(static_cast<size_t>(cgNx) * cgNy, -1);
    for (size_t t = 0; t < nt; ++t) {
      if (!alive[t]) continue;
      const V2& a = pts[tri[t][0]];
      const V2& b = pts[tri[t][1]];
      const V2& c = pts[tri[t][2]];
      const V2 g((a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0);
      int i = static_cast<int>((g.x - cgLo.x) / cell);
      int j = static_cast<int>((g.y - cgLo.y) / cell);
      i = std::clamp(i, 0, cgNx - 1);
      j = std::clamp(j, 0, cgNy - 1);
      cgHead[static_cast<size_t>(j) * cgNx + i] = static_cast<int>(t);
    }
  }

  void setupCentroidGrid(const AABB& box, Real cell) {
    cgLo = box.lo - V2(cell, cell);
    cgCell = std::max(cell, 1e-9);
    cgNx = std::max(2, static_cast<int>(std::ceil((box.hi.x - box.lo.x) / cgCell)) + 3);
    cgNy = std::max(2, static_cast<int>(std::ceil((box.hi.y - box.lo.y) / cgCell)) + 3);
    rebuildCentroidGrid();
  }

  int gridCandidate(const V2& p) const {
    if (cgHead.empty()) return -1;
    int ci = static_cast<int>((p.x - cgLo.x) / cgCell);
    int cj = static_cast<int>((p.y - cgLo.y) / cgCell);
    for (int r = 0; r <= 4; ++r) {
      for (int j = cj - r; j <= cj + r; ++j) {
        for (int i = ci - r; i <= ci + r; ++i) {
          if (i < 0 || j < 0 || i >= cgNx || j >= cgNy) continue;
          if (r > 0 && std::max(std::abs(i - ci), std::abs(j - cj)) != r) continue;
          const int t = cgHead[static_cast<size_t>(j) * cgNx + i];
          if (t >= 0 && alive[t]) return t;
        }
      }
    }
    return -1;
  }

  bool contains(int t, const V2& p) const {
    const auto& v = tri[t];
    const Real d0 = orient2d(pts[v[0]], pts[v[1]], p);
    const Real d1 = orient2d(pts[v[1]], pts[v[2]], p);
    const Real d2 = orient2d(pts[v[2]], pts[v[0]], p);
    const Real eps = -1e-14;
    return d0 >= eps && d1 >= eps && d2 >= eps;
  }

  int locate(const V2& p) {
    int t = -1;
    if (hint >= 0 && hint < static_cast<int>(tri.size()) && alive[hint]) t = hint;
    if (t < 0) t = gridCandidate(p);
    if (t < 0) {
      for (size_t i = 0; i < tri.size(); ++i)
        if (alive[i]) {
          t = static_cast<int>(i);
          break;
        }
    }
    if (t < 0) return -1;
    for (int iter = 0; iter < 20000; ++iter) {
      if (!alive[t]) {
        const int g = gridCandidate(p);
        if (g < 0) break;
        t = g;
        continue;
      }
      bool moved = false;
      for (int k = 0; k < 3; ++k) {
        const V2& A = pts[tri[t][(k + 1) % 3]];
        const V2& B = pts[tri[t][(k + 2) % 3]];
        if (orient2d(A, B, p) < 0) {
          const int n = nb[t][k];
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
        break;  // inconsistent adjacency -> fall back to the brute force scan
      }
    }
    // brute force fallback (rare)
    for (size_t i = 0; i < tri.size(); ++i)
      if (alive[i] && contains(static_cast<int>(i), p)) {
        hint = static_cast<int>(i);
        return hint;
      }
    hint = t;
    return t;
  }

  // Insert a point; returns its index or -1 when it is a near duplicate.
  int insert(const V2& p) {
    const int t0 = locate(p);
    if (t0 < 0) return -1;
    ++gen;
    std::vector<int> dead;
    std::vector<int> stk;
    vstamp.resize(tri.size() + 8, -1);
    dstamp.resize(tri.size() + 8, -1);
    vstamp[t0] = gen;
    stk.push_back(t0);
    while (!stk.empty()) {
      const int cur = stk.back();
      stk.pop_back();
      if (!alive[cur] || !circumContains(cur, p)) continue;
      dead.push_back(cur);
      dstamp[cur] = gen;
      for (int k = 0; k < 3; ++k) {
        const int n = nb[cur][k];
        if (n >= 0 && alive[n] && vstamp[n] != gen) {
          vstamp[n] = gen;
          stk.push_back(n);
        }
      }
    }
    if (dead.empty()) return -1;
    const int pid = static_cast<int>(pts.size());
    pts.push_back(p);
    struct BE {
      int a, b;
    };
    std::vector<BE> bnd;
    bnd.reserve(dead.size() * 2);
    // collect the cavity boundary FIRST: killing a triangle relinks its
    // neighbours, which would corrupt the adjacency of triangles that are
    // themselves part of the cavity.
    for (const int d : dead) {
      for (int k = 0; k < 3; ++k) {
        const int n = nb[d][k];
        if (n < 0 || dstamp[n] != gen)
          bnd.push_back({tri[d][(k + 1) % 3], tri[d][(k + 2) % 3]});
      }
    }
    for (const int d : dead) kill(d);
    int last = -1;
    for (const auto& e : bnd) {
      const int t = addTri(pid, e.a, e.b);
      if (t >= 0) last = t;
    }
    if (last >= 0) hint = last;
    if (static_cast<int>(pts.size()) % 8192 == 0) rebuildCentroidGrid();
    return pid;
  }

  void refreshNodeAny() {
    nodeAny.assign(pts.size(), -1);
    for (size_t t = 0; t < tri.size(); ++t)
      if (alive[t])
        for (int k = 0; k < 3; ++k) nodeAny[tri[t][k]] = static_cast<int>(t);
  }

  // Flip-to-insert of a constrained edge. Returns true when the edge exists
  // afterwards. `splitQueue` receives midpoint splits as a last resort.
  bool constrain(int a, int b, const std::unordered_set<uint64_t>& locked,
                 std::vector<std::pair<int, int>>& splitQueue) {
    if (a == b) return true;
    if (hasEdge(a, b)) return true;
    for (int attempt = 0; attempt < 64; ++attempt) {
      if (hasEdge(a, b)) return true;
      int start = nodeAny.size() > static_cast<size_t>(a) ? nodeAny[a] : -1;
      if (start < 0 || !alive[start]) {
        refreshNodeAny();
        start = nodeAny.size() > static_cast<size_t>(a) ? nodeAny[a] : -1;
      }
      if (start < 0 || !alive[start]) break;
      int found = -1, fx = -1, fy = -1;
      int cur = start;
      for (int step = 0; step < 512; ++step) {
        if (cur < 0 || !alive[cur]) break;
        int i = -1;
        for (int k = 0; k < 3; ++k)
          if (tri[cur][k] == a) i = k;
        if (i < 0) break;
        const int x = tri[cur][(i + 1) % 3], y = tri[cur][(i + 2) % 3];
        const Real e1 = orient2d(pts[a], pts[x], pts[b]);
        const Real e2 = orient2d(pts[a], pts[b], pts[y]);
        const Real e3 = orient2d(pts[x], pts[y], pts[b]);
        const Real scale = std::max(1e-30, norm(pts[x] - pts[a]) * norm(pts[b] - pts[a]));
        if (e1 >= -1e-12 * scale && e2 >= -1e-12 * scale && e3 < 0) {
          found = cur;
          fx = x;
          fy = y;
          break;
        }
        const int nxt = nb[cur][(i + 2) % 3];
        if (nxt == start || nxt < 0) break;
        cur = nxt;
      }
      if (found < 0) break;
      // locate the triangle on the other side of edge (x,y)
      int slot = -1;
      for (int k = 0; k < 3; ++k)
        if (tri[found][(k + 1) % 3] == fx && tri[found][(k + 2) % 3] == fy) slot = k;
      if (slot < 0) break;
      const int t2 = nb[found][slot];
      if (t2 < 0 || !alive[t2]) break;
      if (locked.count(ukey(fx, fy))) break;
      int z = -1;
      for (int k = 0; k < 3; ++k)
        if (tri[t2][k] != fx && tri[t2][k] != fy) z = tri[t2][k];
      if (z < 0) break;
      const Real q1 = orient2d(pts[a], pts[fx], pts[z]);
      const Real q2 = orient2d(pts[a], pts[z], pts[fy]);
      if (q1 <= 1e-14 || q2 <= 1e-14) break;  // non-convex quadrilateral
      kill(found);
      kill(t2);
      addTri(a, fx, z);
      addTri(a, z, fy);
      hint = static_cast<int>(tri.size()) - 1;
    }
    if (hasEdge(a, b)) return true;
    // last resort: subdivide the constraint
    const V2 m = (pts[a] + pts[b]) * 0.5;
    const int mi = insert(m);
    if (mi < 0) return hasEdge(a, b);
    splitQueue.push_back({a, mi});
    splitQueue.push_back({mi, b});
    return true;
  }
};

Real triArea(const V2& a, const V2& b, const V2& c) { return 0.5 * orient2d(a, b, c); }

}  // namespace

// ------------------------------------------------------------ mesh builder ----
Mesh buildFvmMesh(const Spec& spec, const ContourLoops& contours, Timer& timer,
                  std::vector<std::string>& warnings) {
  Mesh mesh;
  const FvmSpec& fs = spec.fvm;
  const Sdf& sdf = spec.geom;
  if (!fs.enabled) return mesh;

  // ---- 1. adaptive resampling of the contour loops -----------------------
  std::vector<std::vector<V2>> loops;
  std::vector<int> loopHole;
  std::vector<int> loopOfPoint;
  {
    ScopedTimer st(timer, "contourResample");
    loops.reserve(contours.loops.size());
    for (size_t li = 0; li < contours.loops.size(); ++li) {
      const auto& src = contours.loops[li];
      std::vector<V2> out;
      out.reserve(src.size());
      // corner detection: a vertex whose turn angle exceeds cornerAngle is
      // always kept so that sharp features are not rounded away
      const Real cornerCos = std::cos(35.0 * kPi / 180.0);
      auto isCorner = [&](size_t k) {
        const size_t m = src.size();
        const V2 t1 = normalized(src[k] - src[(k + m - 1) % m]);
        const V2 t2 = normalized(src[(k + 1) % m] - src[k]);
        return dot(t1, t2) < cornerCos;
      };
      V2 last = src[0];
      out.push_back(last);
      Real acc = 0;
      for (size_t k = 1; k < src.size(); ++k) {
        const V2& p = src[k];
        const Real seg = norm(p - last);
        const Real hloc = sizeField(fs, p, sdf);
        if (acc + seg >= 0.75 * hloc || isCorner(k)) {
          out.push_back(p);
          last = p;
          acc = 0;
        } else {
          acc += seg;
        }
      }
      // avoid a degenerate closing edge
      if (out.size() > 3 && norm(out.front() - out.back()) < 0.45 * sizeField(fs, out.back(), sdf))
        out.pop_back();
      if (out.size() >= 3) {
        loops.push_back(std::move(out));
        loopHole.push_back(contours.isHole[li]);
      }
    }
    if (loops.empty()) {
      warnings.push_back("geometry produced no closed contour; mesh disabled");
      return mesh;
    }
  }

  // ---- 2/3. node list: contour + boundary layers + interior darts --------
  std::vector<V2> nodes;
  std::vector<int> nodeLoop;    // loop id for contour nodes, -1 otherwise
  std::vector<int> nodeBlLevel; // 0 = on the wall, k = k-th BL row
  std::vector<char> nodeFixed;
  std::vector<std::vector<int>> loopNodeIds;   // ordered contour node ids per loop
  nodes.reserve(1 << 16);
  auto patchForPoint = [&](const V2& p, int holeFlag) -> int {
    for (size_t ri = 0; ri < fs.rules.size(); ++ri) {
      const BoundaryRule& r = fs.rules[ri];
      const Real tol = 1e-6 * norm(spec.domain.size()) + 1e-9;
      if (r.where == "all") return static_cast<int>(ri);
      if (r.where == "xmin" && std::fabs(p.x - spec.domain.lo.x) < tol * 100) return static_cast<int>(ri);
      if (r.where == "xmax" && std::fabs(p.x - spec.domain.hi.x) < tol * 100) return static_cast<int>(ri);
      if (r.where == "ymin" && std::fabs(p.y - spec.domain.lo.y) < tol * 100) return static_cast<int>(ri);
      if (r.where == "ymax" && std::fabs(p.y - spec.domain.hi.y) < tol * 100) return static_cast<int>(ri);
      if (r.where == "xrange" && p.x >= r.a.x - tol && p.x <= r.b.x + tol && p.y >= r.a.y - tol &&
          p.y <= r.b.y + tol)
        return static_cast<int>(ri);
      if (r.where == "yrange" && p.y >= r.a.y - tol && p.y <= r.b.y + tol && p.x >= r.a.x - tol &&
          p.x <= r.b.x + tol)
        return static_cast<int>(ri);
      if (r.where == "circle" && std::fabs(norm(p - r.a) - r.radius) < std::max(r.radius * 1e-3, tol * 100))
        return static_cast<int>(ri);
      if ((r.where == "hole" || r.where == "body") && holeFlag == 1) return static_cast<int>(ri);
      if (r.where == "outer" && holeFlag == 0) return static_cast<int>(ri);
    }
    return -1;
  };

  {
    ScopedTimer st(timer, "boundaryNodes");
    for (size_t li = 0; li < loops.size(); ++li) {
      const auto& L = loops[li];
      const size_t n = L.size();
      std::vector<int> contourIds(n);
      std::vector<V2> normals(n);
      for (size_t k = 0; k < n; ++k) {
        const V2& prev = L[(k + n - 1) % n];
        const V2& next = L[(k + 1) % n];
        const V2 t = normalized(next - prev);
        normals[k] = V2(-t.y, t.x);  // inward for CCW outer / CW hole
        contourIds[k] = static_cast<int>(nodes.size());
        nodes.push_back(L[k]);
        nodeLoop.push_back(static_cast<int>(li));
        nodeBlLevel.push_back(0);
        nodeFixed.push_back(1);
      }
      loopNodeIds.push_back(contourIds);
      // boundary layer rows
      if (fs.blCount > 0 && fs.blFirst > 0) {
        // dominant patch of the loop (not just its first point)
        std::map<int, int> votes;
        for (size_t k = 0; k < n; k += std::max<size_t>(1, n / 32))
          votes[patchForPoint(L[k], loopHole[li])]++;
        int pid = -1, bestVotes = -1;
        for (auto& kv : votes)
          if (kv.second > bestVotes) {
            bestVotes = kv.second;
            pid = kv.first;
          }
        const bool apply = !fs.blOnlyMatching || pid < 0 || fs.rules[pid].name == fs.blPatch ||
                           fs.rules[pid].bc == fs.blPatch || fs.blPatch == "any";
        if (apply) {
          Real height = fs.blFirst;
          for (int layer = 1; layer <= fs.blCount; ++layer) {
            Real off = 0;
            for (int l = 1; l < layer; ++l) off += fs.blFirst * std::pow(fs.blGrowth, l - 1);
            off += height;
            for (size_t k = 0; k < n; ++k) {
              const V2 p = L[k] + normals[k] * off;
              if (sdf(p) > -1e-9) continue;  // would leave the domain
              nodes.push_back(p);
              nodeLoop.push_back(static_cast<int>(li));
              nodeBlLevel.push_back(layer);
              nodeFixed.push_back(1);
            }
            height *= fs.blGrowth;
          }
        }
      }
    }
  }

  const size_t nBoundaryNodes = nodes.size();
  (void)nBoundaryNodes;
  Real hMinGlobal = sizeFieldMin(fs, sdf);
  for (const auto& t : fs.targets) hMinGlobal = std::min(hMinGlobal, t.hNear > 0 ? t.hNear : fs.base * 0.25);
  if (fs.blCount > 0 && fs.blFirst > 0) hMinGlobal = std::min(hMinGlobal, fs.blFirst * 1.5);
  hMinGlobal = std::max(hMinGlobal, fs.base * 1e-3);
  hMinGlobal = std::max(hMinGlobal, norm(spec.domain.size()) * 1e-5);

  // ---- 3. variable radius dart throwing ---------------------------------
  {
    ScopedTimer st(timer, "interiorSampling");
    Grid accept;
    AABB box = spec.domain;
    box.lo = box.lo - V2(hMinGlobal, hMinGlobal);
    box.hi = box.hi + V2(hMinGlobal, hMinGlobal);
    accept.build(box, hMinGlobal, nodes.size(), nodes.data());
    std::vector<V2> newPts;
    newPts.reserve(1 << 16);
    const int nx = accept.nx, ny = accept.ny;
    std::vector<int> order(static_cast<size_t>(nx) * ny);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(12345);
    std::shuffle(order.begin(), order.end(), rng);
    std::uniform_real_distribution<Real> jitter(-0.42, 0.42);
    const Real ratioCap = 16.0;
    for (const int cell : order) {
      const int ci = cell % nx, cj = cell / nx;
      const V2 base(box.lo.x + (ci + 0.5) * hMinGlobal, box.lo.y + (cj + 0.5) * hMinGlobal);
      for (int attempt = 0; attempt < 2; ++attempt) {
        const V2 p(base.x + jitter(rng) * hMinGlobal, base.y + jitter(rng) * hMinGlobal);
        const Real d = sdf(p);
        const Real hloc = sizeField(fs, p, sdf);
        if (d > -0.05 * hloc) continue;
        const Real hq = std::min(hloc, ratioCap * hMinGlobal);
        Real minDist = kInf;
        bool ok = true;
        accept.forNear(p, hq, [&](int idx) {
          const V2& q = (idx < static_cast<int>(nodes.size())) ? nodes[idx] : newPts[idx - nodes.size()];
          const Real dist = norm(p - q);
          if (dist < minDist) minDist = dist;
        });
        // distance requirement against the local size of both points
        if (minDist < 0.72 * hq) ok = false;
        if (!ok) continue;
        const int id = static_cast<int>(nodes.size()) + static_cast<int>(newPts.size());
        newPts.push_back(p);
        accept.next.push_back(-1);
        accept.insert(id, p);
        break;
      }
    }
    for (auto& p : newPts) {
      nodes.push_back(p);
      nodeLoop.push_back(-1);
      nodeBlLevel.push_back(-1);
      nodeFixed.push_back(0);
    }
  }

  if (nodes.size() < 3) {
    warnings.push_back("not enough nodes generated; check the geometry/size field");
    return mesh;
  }

  // ---- 4. Delaunay ------------------------------------------------------
  std::vector<std::array<int, 3>> tris;
  {
    ScopedTimer st(timer, "delaunay");
    Delaunay dt(nodes.size());
    dt.initSuper(spec.domain);
    const Real avgH = std::sqrt(std::max(1e-12, contours.area)) / std::max(1.0, std::sqrt(static_cast<Real>(nodes.size())));
    dt.setupCentroidGrid(spec.domain, std::max(avgH, hMinGlobal));
    dt.vstamp.assign(64, -1);
    dt.dstamp.assign(64, -1);
    dt.nodeAny.assign(8, -1);
    // node index -> triangulation point index (an insert may be rejected as a
    // near duplicate, hence the explicit mapping)
    std::vector<int> ptOf(nodes.size(), -1);
    std::vector<int> nodeOfPt;
    size_t failedInserts = 0;
    const bool dbg = std::getenv("MESHER_DEBUG") != nullptr;
    for (size_t i = 0; i < nodes.size(); ++i) {
      const int pid = dt.insert(nodes[i]);
      if (pid < 0) failedInserts++;
      ptOf[i] = pid;
      if (pid >= 0) {
        if (nodeOfPt.size() <= static_cast<size_t>(pid)) nodeOfPt.resize(pid + 1, -1);
        nodeOfPt[pid] = static_cast<int>(i);
      }
    }
    // drop the super triangle
    for (size_t t = 0; t < dt.tri.size(); ++t) {
      if (!dt.alive[t]) continue;
      const auto& v = dt.tri[t];
      if (v[0] < 3 || v[1] < 3 || v[2] < 3) dt.kill(static_cast<int>(t));
    }
    // ---- 5. constraints -------------------------------------------------
    {
      ScopedTimer st2(timer, "constraints");
      std::unordered_set<uint64_t> locked;
      std::vector<std::pair<int, int>> queue;
      for (size_t li = 0; li < loopNodeIds.size(); ++li) {
        const auto& ids = loopNodeIds[li];
        const size_t n = ids.size();
        for (size_t k = 0; k < n; ++k) {
          const int a = ptOf[ids[k]];
          const int b = ptOf[ids[(k + 1) % n]];
          if (a >= 0 && b >= 0 && a != b) queue.push_back({a, b});
        }
      }
      int guard = 0;
      while (!queue.empty() && guard++ < 4000000) {
        const auto e = queue.back();
        queue.pop_back();
        if (locked.count(ukey(e.first, e.second))) continue;
        dt.constrain(e.first, e.second, locked, queue);
        locked.insert(ukey(e.first, e.second));
      }
      if (guard >= 4000000) warnings.push_back("constraint enforcement hit the iteration guard");
      // constraint splits may have introduced new points: register them
      for (size_t pid = 0; pid < dt.pts.size(); ++pid) {
        if (nodeOfPt.size() <= pid) nodeOfPt.resize(pid + 1, -1);
        if (nodeOfPt[pid] < 0 && pid >= 3) {
          nodeOfPt[pid] = static_cast<int>(nodes.size());
          nodes.push_back(dt.pts[pid]);
          nodeLoop.push_back(-1);
          nodeBlLevel.push_back(-1);
          nodeFixed.push_back(1);
        }
      }
    }
    // ---- 6. exterior removal -------------------------------------------
    {
      ScopedTimer st2(timer, "exteriorRemoval");
      const size_t nt = dt.tri.size();
      std::vector<char> keep(nt, 0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
      for (long long t = 0; t < static_cast<long long>(nt); ++t) {
        if (!dt.alive[t]) continue;
        const auto& v = dt.tri[t];
        if (v[0] < 3 || v[1] < 3 || v[2] < 3) continue;
        const V2 c((dt.pts[v[0]].x + dt.pts[v[1]].x + dt.pts[v[2]].x) / 3.0,
                   (dt.pts[v[0]].y + dt.pts[v[1]].y + dt.pts[v[2]].y) / 3.0);
        // tight tolerance: a generous one keeps the thin cap triangles that
        // marching-squares faceting produces between three contour nodes
        const Real tol = 1e-6 * sizeField(fs, c, sdf);
        if (sdf(c) < tol) keep[t] = 1;
      }
      size_t aliveCount = 0, keptCount = 0, unmapped = 0;
      for (size_t t = 0; t < nt; ++t) {
        if (!keep[t]) continue;
        keptCount++;
        const auto& v = dt.tri[t];
        const int a = nodeOfPt[v[0]], b = nodeOfPt[v[1]], c = nodeOfPt[v[2]];
        if (a < 0 || b < 0 || c < 0) {
          unmapped++;
          continue;
        }
        tris.push_back({a, b, c});
      }
      for (size_t t = 0; t < nt; ++t)
        if (dt.alive[t]) aliveCount++;
      // safety net: drop numerically degenerate cells (they would produce
      // deltaCoeff = 0 faces and singular diffusion stencils)
      {
        Real meanA = 0;
        for (auto& t : tris)
          meanA += triArea(nodes[t[0]], nodes[t[1]], nodes[t[2]]);
        meanA /= std::max<size_t>(1, tris.size());
        const Real thresh = 1e-7 * std::max(meanA, 1e-30);
        std::vector<std::array<int, 3>> good;
        good.reserve(tris.size());
        size_t dropped = 0;
        for (auto& t : tris) {
          const V2& a = nodes[t[0]];
          const V2& b = nodes[t[1]];
          const V2& c2 = nodes[t[2]];
          const Real ar = triArea(a, b, c2);
          const Real emin = std::min({norm(b - a), norm(c2 - b), norm(a - c2)});
          if (ar < thresh || emin < 1e-9 * std::max(meanA, 1e-30)) {
            dropped++;
            continue;
          }
          good.push_back(t);
        }
        if (dropped) {
          warnings.push_back("dropped " + std::to_string(dropped) + " degenerate cells");
          tris.swap(good);
        }
      }
      if (dbg)
        std::fprintf(stderr,
                     "[mesher] nodes=%zu failedInserts=%zu dtPts=%zu dtTris=%zu alive=%zu "
                     "kept=%zu unmapped=%zu finalTris=%zu\n",
                     nodes.size(), failedInserts, dt.pts.size(), nt, aliveCount, keptCount,
                     unmapped, tris.size());
    }
  }

  if (tris.empty()) {
    warnings.push_back("triangulation produced no cells inside the domain");
    return mesh;
  }

  // ---- 7. smoothing -----------------------------------------------------
  if (fs.smoothIters > 0) {
    ScopedTimer st(timer, "smoothing");
    // node adjacency
    std::unordered_map<uint64_t, std::pair<int, int>> edges;
    edges.reserve(tris.size() * 3);
    for (auto& t : tris)
      for (int k = 0; k < 3; ++k) {
        const int a = t[k], b = t[(k + 1) % 3];
        edges[ukey(a, b)] = {a, b};
      }
    std::vector<std::vector<int>> adj(nodes.size());
    for (auto& kv : edges) {
      adj[kv.second.first].push_back(kv.second.second);
      adj[kv.second.second].push_back(kv.second.first);
    }
    std::vector<V2> tmp(nodes.size());
    const Real tol = hMinGlobal * 0.05;
    for (int it = 0; it < fs.smoothIters; ++it) {
      const Real w = fs.smoothWeight * (1.0 - 0.5 * static_cast<Real>(it) / std::max(1, fs.smoothIters));
#ifdef _OPENMP
#pragma omp parallel for if(nodes.size() > 512) schedule(static)
#endif
      for (long long i = 0; i < static_cast<long long>(nodes.size()); ++i) {
        if (nodeFixed[i] || adj[i].empty()) {
          tmp[i] = nodes[i];
          continue;
        }
        V2 avg(0, 0);
        for (const int j : adj[i]) avg += nodes[j];
        avg = avg / static_cast<Real>(adj[i].size());
        tmp[i] = nodes[i] + (avg - nodes[i]) * w;
      }
      nodes.swap(tmp);
      // keep nodes inside the domain
#ifdef _OPENMP
#pragma omp parallel for if(nodes.size() > 512) schedule(static)
#endif
      for (long long i = 0; i < static_cast<long long>(nodes.size()); ++i) {
        if (nodeFixed[i]) continue;
        const Real d = sdf(nodes[i]);
        if (d > -tol) {
          V2 g = sdf.grad(nodes[i], 1e-6);
          const Real gn = norm(g);
          if (gn > 1e-12) nodes[i] = nodes[i] - g * ((d + tol) / gn);
        }
      }
    }
  }

  // ---- 8. topology + DiFVM graph ----------------------------------------
  {
    ScopedTimer st(timer, "fvGraph");
    // renumber used nodes
    std::vector<int> remap(nodes.size(), -1);
    for (auto& t : tris)
      for (int k = 0; k < 3; ++k)
        if (remap[t[k]] < 0) remap[t[k]] = static_cast<int>(mesh.nodes.size()), mesh.nodes.push_back(nodes[t[k]]);
    std::vector<int> remapLoop(nodes.size(), -1), remapBl(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i)
      if (remap[i] >= 0) {
        remapLoop[remap[i]] = nodeLoop[i];
        remapBl[remap[i]] = nodeBlLevel[i];
      }
    for (auto& t : tris)
      for (int k = 0; k < 3; ++k) t[k] = remap[t[k]];

    const size_t nc = tris.size(), nn = mesh.nodes.size();
    mesh.cells = tris;
    mesh.cc.resize(nc);
    mesh.vol.resize(nc);
    mesh.cellNodeFlat.resize(3 * nc);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long long i = 0; i < static_cast<long long>(nc); ++i) {
      const V2& a = mesh.nodes[tris[i][0]];
      const V2& b = mesh.nodes[tris[i][1]];
      const V2& c = mesh.nodes[tris[i][2]];
      mesh.cc[i] = (a + b + c) / 3.0;
      mesh.vol[i] = triArea(a, b, c);
      mesh.cellNodeFlat[3 * i + 0] = tris[i][0];
      mesh.cellNodeFlat[3 * i + 1] = tris[i][1];
      mesh.cellNodeFlat[3 * i + 2] = tris[i][2];
    }

    // faces from the sorted directed edge list
    struct ERec {
      uint64_t key;
      int cell;
      int slot;
    };
    std::vector<ERec> erecs;
    erecs.resize(3 * nc);
    for (size_t c = 0; c < nc; ++c)
      for (int k = 0; k < 3; ++k) {
        const int a = tris[c][k], b = tris[c][(k + 1) % 3];
        erecs[3 * c + k] = ERec{ukey(a, b), static_cast<int>(c), (a < b) ? k : k + 3};
      }
    std::sort(erecs.begin(), erecs.end(), [](const ERec& x, const ERec& y) { return x.key < y.key; });

    // patch table
    mesh.patchNames.clear();
    mesh.patchBc.clear();
    std::map<std::string, int> patchIndex;
    auto ensurePatch = [&](const std::string& name, const std::string& bc) {
      auto it = patchIndex.find(name);
      if (it != patchIndex.end()) return it->second;
      const int id = static_cast<int>(mesh.patchNames.size());
      mesh.patchNames.push_back(name);
      mesh.patchBc.push_back(bc);
      patchIndex[name] = id;
      return id;
    };
    const Real diag = norm(spec.domain.size());
    auto matchRule = [&](const V2& p, int holeFlag, int loopId) {
      const Real tol = 1e-6 * diag;
      for (size_t ri = 0; ri < fs.rules.size(); ++ri) {
        const BoundaryRule& r = fs.rules[ri];
        const Real t2 = r.radius > 0 ? std::max(tol * 100, r.radius * 1e-3) : tol * 100;
        if (r.where == "all") return ri;
        if (r.where == "xmin" && std::fabs(p.x - spec.domain.lo.x) < t2) return ri;
        if (r.where == "xmax" && std::fabs(p.x - spec.domain.hi.x) < t2) return ri;
        if (r.where == "ymin" && std::fabs(p.y - spec.domain.lo.y) < t2) return ri;
        if (r.where == "ymax" && std::fabs(p.y - spec.domain.hi.y) < t2) return ri;
        if (r.where == "xrange" && p.x >= r.a.x - tol && p.x <= r.b.x + tol) return ri;
        if (r.where == "yrange" && p.y >= r.a.y - tol && p.y <= r.b.y + tol) return ri;
        if (r.where == "circle" && std::fabs(norm(p - r.a) - r.radius) < t2) return ri;
        if ((r.where == "hole" || r.where == "body") && holeFlag == 1) return ri;
        if (r.where == "outer" && holeFlag == 0) return ri;
        if (r.where == "loop" && loopId == static_cast<int>(r.radius)) return ri;
      }
      return static_cast<size_t>(-1);
    };

    std::vector<Face> faces;
    faces.reserve(erecs.size());
    for (size_t i = 0; i < erecs.size();) {
      size_t j = i;
      while (j < erecs.size() && erecs[j].key == erecs[i].key) ++j;
      const int a = static_cast<int>(erecs[i].key >> 32), b = static_cast<int>(erecs[i].key & 0xffffffffu);
      Face f;
      const V2 pa = mesh.nodes[a], pb = mesh.nodes[b];
      f.c = (pa + pb) * 0.5;
      f.len = norm(pb - pa);
      V2 nrm = normalized(perp(pb - pa));
      if (j - i >= 2) {
        const int c0 = erecs[i].cell, c1 = erecs[i + 1].cell;
        int owner = c0, nbcell = c1;
        // Sf must point from owner to neighbour: swapping the pair reverses
        // delta, so the normal itself must NOT be flipped as well.
        if (dot(nrm, mesh.cc[nbcell] - mesh.cc[owner]) < 0) std::swap(owner, nbcell);
        f.owner = owner;
        f.neighbour = nbcell;
        f.patch = -1;
        f.delta = mesh.cc[nbcell] - mesh.cc[owner];
        mesh.interiorFaces.push_back(static_cast<int>(faces.size()));
      } else {
        const int owner = erecs[i].cell;
        // outward from the domain (cc - Cf points inward)
        if (dot(nrm, mesh.cc[owner] - f.c) > 0) nrm = nrm * -1.0;
        f.owner = owner;
        f.neighbour = -1;
        const int loopA = remapLoop[a], loopB = remapLoop[b];
        const int loopId = loopA >= 0 ? loopA : loopB;
        const int holeFlag = loopId >= 0 && loopId < static_cast<int>(loopHole.size()) ? loopHole[loopId] : 0;
        const size_t ri = matchRule(f.c, holeFlag, loopId);
        std::string pname = "wall", pbc = "wall";
        if (ri < fs.rules.size()) {
          pname = fs.rules[ri].name;
          pbc = fs.rules[ri].bc;
        } else if (holeFlag == 1) {
          pname = "body";
          pbc = "wall";
        }
        f.patch = ensurePatch(pname, pbc);
        f.delta = f.c - mesh.cc[owner];
        mesh.boundaryFaces.push_back(static_cast<int>(faces.size()));
      }
      f.sf = nrm * f.len;
      const Real dn = dot(f.delta, nrm);
      f.deltaCoeff = dn > 1e-14 ? 1.0 / dn : 0.0;
      f.nonOrtho = (dn > 1e-14) ? std::acos(std::clamp(dot(normalized(f.sf), normalized(f.delta)), -1.0, 1.0)) * 180.0 / kPi : 90.0;
      const Real dO = norm(mesh.cc[f.owner] - f.c);
      const Real dN = f.neighbour >= 0 ? norm(mesh.cc[f.neighbour] - f.c) : dO;
      f.weight = (dO + dN) > 1e-14 ? dN / (dO + dN) : 0.5;
      const V2 dhat = normalized(f.delta);
      f.corr = f.sf - dhat * dot(f.sf, dhat);
      faces.push_back(f);
      i = j;
    }
    mesh.faces = std::move(faces);
    mesh.patchFaceCount.assign(mesh.patchNames.size(), 0);
    for (const int bi : mesh.boundaryFaces) mesh.patchFaceCount[mesh.faces[bi].patch]++;

    // CSR: cell -> faces, node -> cells
    const size_t nf = mesh.faces.size();
    mesh.cellFacePtr.assign(nc + 1, 0);
    for (const auto& f : mesh.faces) {
      mesh.cellFacePtr[f.owner + 1]++;
      if (f.neighbour >= 0) mesh.cellFacePtr[f.neighbour + 1]++;
    }
    for (size_t i = 0; i < nc; ++i) mesh.cellFacePtr[i + 1] += mesh.cellFacePtr[i];
    mesh.cellFaceIdx.resize(mesh.cellFacePtr[nc]);
    {
      std::vector<int> cur = mesh.cellFacePtr;
      cur.resize(nc);
      for (size_t f = 0; f < nf; ++f) {
        mesh.cellFaceIdx[cur[mesh.faces[f].owner]++] = static_cast<int>(f);
        if (mesh.faces[f].neighbour >= 0) mesh.cellFaceIdx[cur[mesh.faces[f].neighbour]++] = static_cast<int>(f);
      }
    }
    mesh.nodeCellPtr.assign(nn + 1, 0);
    for (size_t c = 0; c < nc; ++c)
      for (int k = 0; k < 3; ++k) mesh.nodeCellPtr[tris[c][k] + 1]++;
    for (size_t i = 0; i < nn; ++i) mesh.nodeCellPtr[i + 1] += mesh.nodeCellPtr[i];
    mesh.nodeCellIdx.resize(mesh.nodeCellPtr[nn]);
    {
      std::vector<int> cur = mesh.nodeCellPtr;
      cur.resize(nn);
      for (size_t c = 0; c < nc; ++c)
        for (int k = 0; k < 3; ++k) mesh.nodeCellIdx[cur[tris[c][k]]++] = static_cast<int>(c);
    }
    mesh.patchNodeLoop.assign(nn, 0);
    for (size_t i = 0; i < nn; ++i) mesh.patchNodeLoop[i] = (remapLoop[i] >= 0) ? 1 : 0;

    // flat graph arrays
    mesh.gOwner.resize(nf);
    mesh.gNeighbour.resize(nf);
    mesh.gPatch.resize(nf);
    mesh.gSf.resize(2 * nf);
    mesh.gCf.resize(2 * nf);
    mesh.gMagSf.resize(nf);
    mesh.gDeltaCoeffs.resize(nf);
    mesh.gWeights.resize(nf);
    mesh.gNonOrtho.resize(nf);
    mesh.gCorr.resize(2 * nf);
    mesh.nodeX.resize(2 * nn);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long long f = 0; f < static_cast<long long>(nf); ++f) {
      const Face& fc = mesh.faces[f];
      mesh.gOwner[f] = fc.owner;
      mesh.gNeighbour[f] = fc.neighbour;
      mesh.gPatch[f] = fc.patch;
      mesh.gSf[2 * f] = fc.sf.x;
      mesh.gSf[2 * f + 1] = fc.sf.y;
      mesh.gCf[2 * f] = fc.c.x;
      mesh.gCf[2 * f + 1] = fc.c.y;
      mesh.gMagSf[f] = fc.len;
      mesh.gDeltaCoeffs[f] = fc.deltaCoeff;
      mesh.gWeights[f] = fc.weight;
      mesh.gNonOrtho[f] = fc.nonOrtho;
      mesh.gCorr[2 * f] = fc.corr.x;
      mesh.gCorr[2 * f + 1] = fc.corr.y;
    }
    for (size_t i = 0; i < nn; ++i) {
      mesh.nodeX[2 * i] = mesh.nodes[i].x;
      mesh.nodeX[2 * i + 1] = mesh.nodes[i].y;
    }
    if (fs.vertexDual) {
      mesh.dualVol.assign(nn, 0.0);
      for (size_t c = 0; c < nc; ++c)
        for (int k = 0; k < 3; ++k) mesh.dualVol[tris[c][k]] += mesh.vol[c] / 3.0;
    }

    // ---- quality metrics -------------------------------------------------
    Real maxSkew = 0, sumSkew = 0;
    Real minAsp = kInf, maxAsp = 0, sumAsp = 0;
    Real minVol = kInf, maxVol = 0, totVol = 0, minEdge = kInf, maxEdge = 0;
    int negVol = 0;
    std::vector<Real> nonOrthos;
    nonOrthos.reserve(nf);
    for (size_t c = 0; c < nc; ++c) {
      const V2& a = mesh.nodes[tris[c][0]];
      const V2& b = mesh.nodes[tris[c][1]];
      const V2& d = mesh.nodes[tris[c][2]];
      const Real e0 = norm(b - a), e1 = norm(d - b), e2 = norm(a - d);
      const Real area = 0.5 * orient2d(a, b, d);
      if (area <= 0) negVol++;
      totVol += area;
      minVol = std::min(minVol, area);
      maxVol = std::max(maxVol, area);
      minEdge = std::min({minEdge, e0, e1, e2});
      maxEdge = std::max({maxEdge, e0, e1, e2});
      const Real s = 0.5 * (e0 + e1 + e2);
      const Real rin = area > 1e-30 ? area / s : 0;
      const Real rc = (e0 * e1 * e2) / std::max(1e-30, 4 * area);
      const Real asp = rin > 1e-30 ? rc / (2 * rin) : 1e3;
      minAsp = std::min(minAsp, asp);
      maxAsp = std::max(maxAsp, asp);
      sumAsp += asp;
      auto angle = [](Real l1, Real l2, Real l3) {
        return std::acos(std::clamp((l1 * l1 + l2 * l2 - l3 * l3) / std::max(1e-30, 2 * l1 * l2), -1.0, 1.0)) *
               180.0 / kPi;
      };
      const Real a0 = angle(e2, e0, e1), a1 = angle(e0, e1, e2), a2 = angle(e1, e2, e0);
      const Real amax = std::max({a0, a1, a2}), amin = std::min({a0, a1, a2});
      const Real skew = std::max((amax - 60.0) / 60.0, (60.0 - amin) / 60.0);
      maxSkew = std::max(maxSkew, skew);
      sumSkew += skew;
      mesh.box.expand(a);
      mesh.box.expand(b);
      mesh.box.expand(d);
    }
    for (const auto& f : mesh.faces) nonOrthos.push_back(f.nonOrtho);
    std::sort(nonOrthos.begin(), nonOrthos.end());
    Real perim = 0;
    for (const int bi : mesh.boundaryFaces) perim += mesh.faces[bi].len;
    mesh.minAspect = nc ? minAsp : 0;
    mesh.maxAspect = nc ? maxAsp : 0;
    mesh.avgAspect = nc ? sumAsp / nc : 0;
    mesh.maxSkew = maxSkew;
    mesh.avgSkew = nc ? sumSkew / nc : 0;
    mesh.maxNonOrtho = nonOrthos.empty() ? 0 : nonOrthos.back();
    mesh.avgNonOrtho = nonOrthos.empty()
                           ? 0
                           : std::accumulate(nonOrthos.begin(), nonOrthos.end(), 0.0) / nonOrthos.size();
    mesh.p99NonOrtho = nonOrthos.empty() ? 0 : nonOrthos[static_cast<size_t>(nonOrthos.size() * 0.99)];
    mesh.minVol = nc ? minVol : 0;
    mesh.maxVol = nc ? maxVol : 0;
    mesh.totalVol = totVol;
    mesh.minEdge = nc ? minEdge : 0;
    mesh.maxEdge = nc ? maxEdge : 0;
    mesh.nNegativeVol = negVol;
    mesh.domainArea = totVol;
    mesh.perimeter = perim;
    mesh.valid = true;
    mesh.dim = 2;
  }

  if (mesh.nNegativeVol > 0)
    warnings.push_back("mesh contains " + std::to_string(mesh.nNegativeVol) + " inverted cells");
  if (mesh.maxNonOrtho > 70.0)
    warnings.push_back("high face non-orthogonality (" + std::to_string((int)mesh.maxNonOrtho) +
                       " deg): enable the over-relaxed correction in the solver");
  return mesh;
}

}  // namespace mesher
