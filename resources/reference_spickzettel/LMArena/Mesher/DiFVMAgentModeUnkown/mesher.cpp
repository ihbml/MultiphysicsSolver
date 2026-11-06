// Copyright 2026. A small graph-FVM mesher; see README.md for scope and limits.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
struct Vec {
  double x = 0, y = 0, z = 0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec operator/(Vec a, double s) { return a * (1 / s); }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double norm(Vec a) { return std::sqrt(dot(a, a)); }
Vec unit(Vec a) {
  double n = norm(a);
  if (n == 0) throw std::runtime_error("zero vector");
  return a / n;
}
double component(Vec a, int k) { return k == 0 ? a.x : k == 1 ? a.y : a.z; }

// 64-byte-aligned arrays for predictable CPU SIMD and downstream bulk transfers.
template <class T>
struct AlignedAllocator {
  using value_type = T;
  AlignedAllocator() = default;
  template <class U>
  AlignedAllocator(const AlignedAllocator<U>&) noexcept {}
  T* allocate(std::size_t n) {
    if (n > SIZE_MAX / sizeof(T)) throw std::bad_alloc();
    void* p = nullptr;
    if (posix_memalign(&p, 64, std::max(size_t(64), n * sizeof(T)))) throw std::bad_alloc();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, std::size_t) noexcept { free(p); }
  template <class U>
  bool operator==(const AlignedAllocator<U>&) const noexcept {
    return true;
  }
};
template <class T>
using Aligned = std::vector<T, AlignedAllocator<T>>;
using Id = std::uint64_t;
struct Tri {
  Vec a, b, c;
};
struct Aabb {
  Vec lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
  void add(Vec p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  void add(Aabb b) {
    add(b.lo);
    add(b.hi);
  }
};
Aabb bounds(Tri t) {
  Aabb b;
  b.add(t.a);
  b.add(t.b);
  b.add(t.c);
  return b;
}
double boxdist2(Aabb b, Vec p) {
  double s = 0;
  for (int k = 0; k < 3; ++k) {
    double q = component(p, k), v = std::max({component(b.lo, k) - q, 0.0, q - component(b.hi, k)});
    s += v * v;
  }
  return s;
}
double point_tri2(Vec p, Tri t) {  // Ericson's closest-point triangle regions.
  Vec ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
  double d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return dot(ap, ap);
  Vec bp = p - t.b;
  double d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return dot(bp, bp);
  double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    Vec q = t.a + ab * (d1 / (d1 - d3));
    return dot(p - q, p - q);
  }
  Vec cp = p - t.c;
  double d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return dot(cp, cp);
  double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    Vec q = t.a + ac * (d2 / (d2 - d6));
    return dot(p - q, p - q);
  }
  double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    Vec q = t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    return dot(p - q, p - q);
  }
  double den = 1 / (va + vb + vc);
  Vec q = t.a + ab * (vb * den) + ac * (vc * den);
  return dot(p - q, p - q);
}
bool raybox(Aabb b, Vec p, Vec d) {
  double lo = 0, hi = INFINITY;
  for (int k = 0; k < 3; ++k) {
    double a = (component(b.lo, k) - component(p, k)) / component(d, k),
           c = (component(b.hi, k) - component(p, k)) / component(d, k);
    if (a > c) std::swap(a, c);
    lo = std::max(lo, a);
    hi = std::min(hi, c);
    if (lo > hi) return false;
  }
  return true;
}
bool raytri(Vec p, Vec d, Tri t) {
  Vec e1 = t.b - t.a, e2 = t.c - t.a, h = cross(d, e2);
  double a = dot(e1, h);
  if (std::abs(a) < 1e-14 * std::max(1.0, norm(e1) * norm(e2))) return false;
  double f = 1 / a;
  Vec s = p - t.a;
  double u = f * dot(s, h);
  if (u < 0 || u > 1) return false;
  Vec q = cross(s, e1);
  double v = f * dot(d, q);
  if (v < 0 || u + v > 1) return false;
  return f * dot(e2, q) > 1e-12;
}
struct Bvh {
  struct Node {
    Aabb b;
    int left = -1, right = -1;
    size_t begin = 0, end = 0;
  };
  std::vector<Tri> tris;
  std::vector<Node> nodes;
  int build(size_t begin, size_t end) {
    int id = static_cast<int>(nodes.size());
    nodes.push_back({});
    Aabb box, cent;
    for (size_t i = begin; i < end; ++i) {
      box.add(bounds(tris[i]));
      cent.add((tris[i].a + tris[i].b + tris[i].c) / 3);
    }
    nodes[id].b = box;
    nodes[id].begin = begin;
    nodes[id].end = end;
    if (end - begin > 8) {
      Vec ext = cent.hi - cent.lo;
      int k = ext.y > ext.x ? 1 : 0;
      if (component(ext, 2) > component(ext, k)) k = 2;
      size_t mid = (begin + end) / 2;
      std::nth_element(tris.begin() + begin, tris.begin() + mid, tris.begin() + end,
                       [k](Tri a, Tri b) {
                         return component(a.a + a.b + a.c, k) < component(b.a + b.b + b.c, k);
                       });
      int l = build(begin, mid), r = build(mid, end);
      nodes[id].left = l;
      nodes[id].right = r;
    }
    return id;
  }
  double nearest(int id, Vec p, double best) const {
    const Node& n = nodes[id];
    if (boxdist2(n.b, p) >= best) return best;
    if (n.left < 0) {
      for (size_t i = n.begin; i < n.end; ++i) best = std::min(best, point_tri2(p, tris[i]));
      return best;
    }
    double a = boxdist2(nodes[n.left].b, p), b = boxdist2(nodes[n.right].b, p);
    int x = n.left, y = n.right;
    if (a > b) std::swap(x, y);
    return nearest(y, p, nearest(x, p, best));
  }
  int hits(int id, Vec p, Vec d) const {
    const Node& n = nodes[id];
    if (!raybox(n.b, p, d)) return 0;
    if (n.left < 0) {
      int c = 0;
      for (size_t i = n.begin; i < n.end; ++i) c += raytri(p, d, tris[i]);
      return c;
    }
    return hits(n.left, p, d) + hits(n.right, p, d);
  }
  double sdf(Vec p) const {
    Vec d = unit({1., 0.3713906763541037, 0.694173824159});
    double distance = std::sqrt(nearest(0, p, INFINITY));
    return (hits(0, p, d) % 2 ? -1 : 1) * distance;
  }
};
float readf(const unsigned char* p) {
  float f;
  std::memcpy(&f, p, 4);
  return f;
}
Bvh load_stl(const std::string& name) {
  std::ifstream in(name, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open STL: " + name);
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), {});
  Bvh v;
  bool binary = false;
  uint32_t count = 0;
  if (b.size() >= 84) {
    std::memcpy(&count, b.data() + 80, 4);
    binary = (84ull + 50ull * count == b.size());
  }
  if (binary) {
    for (uint32_t i = 0; i < count; ++i) {
      const auto* p = b.data() + 84 + 50ull * i + 12;
      Tri t{{readf(p), readf(p + 4), readf(p + 8)},
            {readf(p + 12), readf(p + 16), readf(p + 20)},
            {readf(p + 24), readf(p + 28), readf(p + 32)}};
      v.tris.push_back(t);
    }
  } else {
    std::istringstream s(std::string(b.begin(), b.end()));
    std::string tok;
    std::vector<Vec> vert;
    while (s >> tok)
      if (tok == "vertex") {
        Vec p;
        if (!(s >> p.x >> p.y >> p.z)) throw std::runtime_error("invalid ASCII STL vertex");
        vert.push_back(p);
      }
    if (vert.size() % 3) throw std::runtime_error("invalid ASCII STL");
    for (size_t i = 0; i < vert.size(); i += 3)
      v.tris.push_back({vert[i], vert[i + 1], vert[i + 2]});
  }
  if (v.tris.empty()) throw std::runtime_error("STL has no triangles");
  for (auto t : v.tris)
    if (!std::isfinite(t.a.x + t.a.y + t.a.z + t.b.x + t.b.y + t.b.z + t.c.x + t.c.y + t.c.z) ||
        norm(cross(t.b - t.a, t.c - t.a)) < 1e-20)
      throw std::runtime_error("STL has nonfinite or degenerate triangles");
  v.build(0, v.tris.size());
  return v;
}
struct Key2 {
  Id a, b;
  bool operator==(const Key2&) const = default;
};
struct Key3 {
  Id a, b, c;
  bool operator==(const Key3&) const = default;
};
struct Hash {
  size_t operator()(Key2 k) const { return std::hash<Id>{}(k.a * 0x9e3779b97f4a7c15ULL ^ k.b); }
  size_t operator()(Key3 k) const {
    return std::hash<Id>{}((k.a * 0x9e3779b97f4a7c15ULL) ^ (k.b * 0xc2b2ae3d27d4eb4fULL) ^ k.c);
  }
};
Key2 edge(Id a, Id b) {
  if (a > b) std::swap(a, b);
  return {a, b};
}
Key3 facekey(std::array<Id, 3> t) {
  std::sort(t.begin(), t.end());
  return {t[0], t[1], t[2]};
}
struct Face {
  std::array<Id, 3> v;
  Id owner;
  int64_t neighbour = -1;
  int patch = 0;
  Vec s, center;
};
struct Cell {
  Vec center;
  double volume, volume_fraction;
  std::vector<std::array<Id, 3>> faces;
};
struct Mesh {
  std::vector<Vec> pts;
  std::vector<double> sdf;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::unordered_map<Key2, Id, Hash> edges;
  std::unordered_map<Key3, Id, Hash> face_index;
  Id intersection(Id a, Id b) {
    Key2 k = edge(a, b);
    auto it = edges.find(k);
    if (it != edges.end()) return it->second;
    double t = sdf[a] / (sdf[a] - sdf[b]);
    Vec p = pts[a] + (pts[b] - pts[a]) * t;
    Id id = pts.size();
    pts.push_back(p);
    sdf.push_back(0);
    edges.emplace(k, id);
    return id;
  }
  std::vector<Id> clip(std::array<Id, 3> f) {
    std::vector<Id> out;
    for (int i = 0; i < 3; ++i) {
      Id a = f[i], b = f[(i + 1) % 3];
      bool ia = sdf[a] > 0, ib = sdf[b] > 0;
      if (ia) out.push_back(a);
      if (ia != ib) out.push_back(intersection(a, b));
    }
    return out;
  }
  void triangles(std::vector<Id> poly, Vec inside, std::vector<std::array<Id, 3>>& result) {
    if (poly.size() < 3) return;
    auto min = std::min_element(poly.begin(), poly.end());
    std::rotate(poly.begin(), min, poly.end());
    for (size_t i = 1; i + 1 < poly.size(); ++i) {
      std::array<Id, 3> t{poly[0], poly[i], poly[i + 1]};
      Vec a = pts[t[0]], b = pts[t[1]], c = pts[t[2]], n = cross(b - a, c - a);
      if (norm(n) < 1e-28) throw std::runtime_error("degenerate face; perturb geometry or grid");
      if (dot(n, (a + b + c) / 3 - inside) < 0) std::swap(t[1], t[2]);
      result.push_back(t);
    }
  }
  void add_tet(std::array<Id, 4> ids, double full_volume) {
    int positive = 0;
    for (Id i : ids) positive += sdf[i] > 0;
    if (positive == 0) return;
    std::vector<Id> all;
    for (Id i : ids)
      if (sdf[i] > 0) all.push_back(i);
    std::vector<Id> cut;
    for (int i = 0; i < 4; ++i)
      for (int j = i + 1; j < 4; ++j)
        if ((sdf[ids[i]] > 0) != (sdf[ids[j]] > 0)) {
          Id p = intersection(ids[i], ids[j]);
          all.push_back(p);
          cut.push_back(p);
        }
    Vec inside{};
    for (Id i : all) inside = inside + pts[i];
    inside = inside / static_cast<double>(all.size());
    std::vector<std::array<Id, 3>> tf;
    const int face[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    for (auto& f : face) triangles(clip({ids[f[0]], ids[f[1]], ids[f[2]]}), inside, tf);
    if (cut.size() >= 3) {
      Vec c{};
      for (Id i : cut) c = c + pts[i];
      c = c / static_cast<double>(cut.size());
      Vec u = unit(pts[cut[0]] - c);
      Vec best{};
      for (Id i : cut) {
        Vec candidate = cross(u, pts[i] - c);
        if (norm(candidate) > norm(best)) best = candidate;
      }
      Vec n = unit(best), v = cross(n, u);
      std::sort(cut.begin(), cut.end(), [&](Id a, Id b) {
        Vec x = pts[a] - c, y = pts[b] - c;
        return std::atan2(dot(x, v), dot(x, u)) < std::atan2(dot(y, v), dot(y, u));
      });
      triangles(cut, inside, tf);
    }
    double vol = 0;
    Vec centroid{};
    for (auto t : tf) {
      Vec a = pts[t[0]], b = pts[t[1]], c = pts[t[2]];
      double w = dot(a - inside, cross(b - inside, c - inside)) / 6;
      if (w <= 0) throw std::runtime_error("invalid tetra clipping orientation");
      vol += w;
      centroid = centroid + (inside + a + b + c) * (w / 4);
    }
    if (!(vol > 0)) throw std::runtime_error("nonpositive cell volume");
    Id owner = cells.size();
    cells.push_back({centroid / vol, vol, vol / full_volume, tf});
    for (auto t : tf) {
      Key3 key = facekey(t);
      auto it = face_index.find(key);
      if (it == face_index.end()) {
        Vec a = pts[t[0]], b = pts[t[1]], c = pts[t[2]];
        Id f = faces.size();
        faces.push_back({t, owner, -1, 0, cross(b - a, c - a) / 2, (a + b + c) / 3});
        face_index.emplace(key, f);
      } else {
        Face& f = faces[it->second];
        if (f.neighbour != -1) throw std::runtime_error("nonmanifold face");
        f.neighbour = static_cast<int64_t>(owner);
      }
    }
  }
};
struct Config {
  int n = 8;
  double radius = .5;
  Vec center{};
  std::string stl, out = "mesh";
  Vec lo{-1, -1, -1}, hi{1, 1, 1};
  bool vtk = true;
};
Config parse(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    auto next = [&]() {
      if (++i >= argc) throw std::runtime_error("missing value for " + k);
      return std::string(argv[i]);
    };
    auto number = [&]() {
      std::string s = next();
      size_t pos = 0;
      double v = std::stod(s, &pos);
      if (pos != s.size() || !std::isfinite(v)) throw std::runtime_error("invalid number: " + s);
      return v;
    };
    if (k == "--n") {
      double v = number();
      if (v != std::floor(v) || v < 1 || v > 256)
        throw std::runtime_error("--n must be integer 1..256");
      c.n = static_cast<int>(v);
    } else if (k == "--radius")
      c.radius = number();
    else if (k == "--center")
      c.center = {number(), number(), number()};
    else if (k == "--bounds")
      c.lo = {number(), number(), number()}, c.hi = {number(), number(), number()};
    else if (k == "--stl")
      c.stl = next();
    else if (k == "--out")
      c.out = next();
    else if (k == "--no-vtk")
      c.vtk = false;
    else if (k == "--help") {
      std::cout << "Usage: mesher [--n N] [--radius R] [--center X Y Z] [--stl PATH] [--bounds "
                   "XMIN YMIN ZMIN XMAX YMAX ZMAX] [--out DIR] [--no-vtk]\nFluid is outside the "
                   "solid. Defaults: sphere radius 0.5 in [-1,1]^3, n=8.\n";
      std::exit(0);
    } else
      throw std::runtime_error("unknown argument: " + k);
  }
  if (c.radius <= 0 || c.hi.x <= c.lo.x || c.hi.y <= c.lo.y || c.hi.z <= c.lo.z)
    throw std::runtime_error("radius and box extents must be positive");
  return c;
}

template <class T>
void save(fs::path p, const Aligned<T>& a) {
  std::ofstream o(p, std::ios::binary);
  if (!o) throw std::runtime_error("cannot write " + p.string());
  o.write(reinterpret_cast<const char*>(a.data()),
          static_cast<std::streamsize>(a.size() * sizeof(T)));
  if (!o) throw std::runtime_error("write failed: " + p.string());
}
struct Soa {
  Aligned<int64_t> owner, neighbour;
  Aligned<int32_t> patch;
  Aligned<double> sx, sy, sz, fx, fy, fz, dx, dy, dz, kx, ky, kz, angle, skx, sky, skz, beta, cx,
      cy, cz, volume, fraction, omega;
};
Soa assemble(Mesh& m, const Config& c) {
  Soa a;
  size_t n = m.cells.size();
  std::vector<double> max_angle(n, 0);
  for (auto& cell : m.cells) {
    a.cx.push_back(cell.center.x);
    a.cy.push_back(cell.center.y);
    a.cz.push_back(cell.center.z);
    a.volume.push_back(cell.volume);
    a.fraction.push_back(cell.volume_fraction);
  }
  for (auto& f : m.faces) {
    Vec xi = m.cells[f.owner].center;
    Vec d = f.neighbour < 0 ? f.center - xi : m.cells[f.neighbour].center - xi;
    double l = norm(d), area = norm(f.s), den = dot(f.s, d);
    if (!(l > 0 && area > 0 && den > 1e-14 * area * l))
      throw std::runtime_error("invalid face orientation/orthogonality");
    Vec delta = d * (area * area / den), k = f.s - delta;
    double theta = std::acos(std::clamp(den / (area * l), -1., 1.)) * 180 / std::numbers::pi;
    Vec skew = f.center - (xi + d * (dot(f.s, f.center - xi) / den));
    double beta = theta <= 65 ? 0.1 : 0.1 + 0.9 * std::tanh((theta - 65) / 10);
    max_angle[f.owner] = std::max(max_angle[f.owner], theta);
    if (f.neighbour >= 0) max_angle[f.neighbour] = std::max(max_angle[f.neighbour], theta);
    if (f.neighbour < 0) {
      double eps = 1e-9 * std::max({c.hi.x - c.lo.x, c.hi.y - c.lo.y, c.hi.z - c.lo.z});
      bool domain = false;
      for (int axis = 0; axis < 3; ++axis) {
        bool low = true, high = true;
        for (Id p : f.v) {
          low &= std::abs(component(m.pts[p], axis) - component(c.lo, axis)) < eps;
          high &= std::abs(component(m.pts[p], axis) - component(c.hi, axis)) < eps;
        }
        domain |= low || high;
      }
      if (domain)
        f.patch = 1;
      else {
        bool wall = true;
        for (Id p : f.v) wall &= m.sdf[p] == 0;
        if (!wall) throw std::runtime_error("unmatched interior face: invalid mesh");
        f.patch = 2;
      }
    }
    a.owner.push_back(static_cast<int64_t>(f.owner));
    a.neighbour.push_back(f.neighbour);
    a.patch.push_back(f.patch);
    a.sx.push_back(f.s.x);
    a.sy.push_back(f.s.y);
    a.sz.push_back(f.s.z);
    a.fx.push_back(f.center.x);
    a.fy.push_back(f.center.y);
    a.fz.push_back(f.center.z);
    a.dx.push_back(delta.x);
    a.dy.push_back(delta.y);
    a.dz.push_back(delta.z);
    a.kx.push_back(k.x);
    a.ky.push_back(k.y);
    a.kz.push_back(k.z);
    a.angle.push_back(theta);
    a.skx.push_back(skew.x);
    a.sky.push_back(skew.y);
    a.skz.push_back(skew.z);
    a.beta.push_back(beta);
  }
  for (size_t i = 0; i < n; ++i) {
    double angle_penalty = std::max(0., max_angle[i] - 45.) / 45.;
    double cut_penalty = 4 * std::max(0., .1 - m.cells[i].volume_fraction) / .1;
    a.omega.push_back(1 / (1 + angle_penalty + cut_penalty));
  }
  return a;
}
void write_vtk(const Mesh& m, const fs::path& name) {
  std::ofstream o(name);
  if (!o) throw std::runtime_error("cannot write VTK");
  o << std::setprecision(17)
    << "# vtk DataFile Version 3.0\nGraph FVM clipped tetrahedra\nASCII\nDATASET "
       "UNSTRUCTURED_GRID\nPOINTS "
    << m.pts.size() << " double\n";
  for (Vec p : m.pts) o << p.x << ' ' << p.y << ' ' << p.z << '\n';
  size_t total = 0;
  for (auto& c : m.cells) total += 2 + 4 * c.faces.size();
  o << "CELLS " << m.cells.size() << ' ' << total << '\n';
  for (auto& c : m.cells) {
    o << 1 + 4 * c.faces.size() << ' ' << c.faces.size();
    for (auto t : c.faces) o << " 3 " << t[0] << ' ' << t[1] << ' ' << t[2];
    o << '\n';
  }
  o << "CELL_TYPES " << m.cells.size() << '\n';
  for (size_t i = 0; i < m.cells.size(); ++i) o << "42\n";
  o << "CELL_DATA " << m.cells.size() << "\nSCALARS volume double 1\nLOOKUP_TABLE default\n";
  for (auto& c : m.cells) o << c.volume << '\n';
  if (!o) throw std::runtime_error("VTK write failed");
}
void write_data(const Soa& a, const Config& c) {
  fs::create_directories(c.out);
  fs::path p = c.out;
#define SAVE(name) save(p / (#name ".bin"), a.name)
  SAVE(owner);
  SAVE(neighbour);
  SAVE(patch);
  SAVE(sx);
  SAVE(sy);
  SAVE(sz);
  SAVE(fx);
  SAVE(fy);
  SAVE(fz);
  SAVE(dx);
  SAVE(dy);
  SAVE(dz);
  SAVE(kx);
  SAVE(ky);
  SAVE(kz);
  SAVE(angle);
  SAVE(skx);
  SAVE(sky);
  SAVE(skz);
  SAVE(beta);
  SAVE(cx);
  SAVE(cy);
  SAVE(cz);
  SAVE(volume);
  SAVE(fraction);
  SAVE(omega);
#undef SAVE
  std::ofstream o(p / "manifest.json");
  o << "{\n  \"format\": \"difvm-mesher-soa-v1\",\n  \"byte_order\": \"little\",\n  \"cells\": "
    << a.volume.size() << ",\n  \"faces\": " << a.owner.size()
    << ",\n  \"types\": {\"owner\": \"int64\", \"neighbour\": \"int64\", \"patch\": \"int32\", "
       "\"all_other_arrays\": \"float64\"},\n  \"patches\": {\"0\": \"internal\", \"1\": "
       "\"domain\", \"2\": \"solid_wall\"},\n  \"boundary_neighbour\": -1\n}\n";
  if (!o) throw std::runtime_error("manifest write failed");
}
int main(int argc, char** argv) {
  try {
    if (std::endian::native != std::endian::little)
      throw std::runtime_error("only little-endian CPUs supported");
    Config c = parse(argc, argv);
    Bvh b;
    if (!c.stl.empty()) b = load_stl(c.stl);
    Mesh m;
    int n = c.n;
    Id n1 = static_cast<Id>(n) + 1;
    Vec step = (c.hi - c.lo) / n;
    size_t grid = n1 * n1 * n1;
    m.pts.reserve(grid);
    m.sdf.reserve(grid);
    auto id = [n1](int i, int j, int k) -> Id { return (static_cast<Id>(k) * n1 + j) * n1 + i; };
    double eps = 1e-5 * std::min({step.x, step.y, step.z});
    for (int k = 0; k <= n; ++k)
      for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
          Vec p{c.lo.x + i * step.x, c.lo.y + j * step.y, c.lo.z + k * step.z};
          double s = c.stl.empty() ? norm(p - c.center) - c.radius : b.sdf(p);
          if (std::abs(s) < eps) s = eps;
          m.pts.push_back(p);
          m.sdf.push_back(s);
        }
    const int tets[6][4] = {{0, 1, 3, 7}, {0, 3, 2, 7}, {0, 2, 6, 7},
                            {0, 6, 4, 7}, {0, 4, 5, 7}, {0, 5, 1, 7}};
    double tv = step.x * step.y * step.z / 6;
    for (int k = 0; k < n; ++k)
      for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
          std::array<Id, 8> q{id(i, j, k),         id(i + 1, j, k),        id(i, j + 1, k),
                              id(i + 1, j + 1, k), id(i, j, k + 1),        id(i + 1, j, k + 1),
                              id(i, j + 1, k + 1), id(i + 1, j + 1, k + 1)};
          for (auto& t : tets) m.add_tet({q[t[0]], q[t[1]], q[t[2]], q[t[3]]}, tv);
        }
    if (m.cells.empty()) throw std::runtime_error("no fluid cells");
    Soa a = assemble(m, c);
    write_data(a, c);
    if (c.vtk) write_vtk(m, fs::path(c.out) / "mesh.vtk");
    double volume = std::accumulate(a.volume.begin(), a.volume.end(), 0.);
    std::cout << std::setprecision(12) << "cells=" << m.cells.size() << " faces=" << m.faces.size()
              << " fluid_volume=" << volume << " output=" << c.out << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
