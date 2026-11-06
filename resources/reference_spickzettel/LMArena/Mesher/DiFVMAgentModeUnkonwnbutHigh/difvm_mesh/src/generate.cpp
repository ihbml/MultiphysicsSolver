// DiFVM Mesher — structured box grid, STL I/O and cut-cell meshing.
#include "difvm/generate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace difvm {
namespace {

struct GridIdx {
  int nx, ny, nz;
  long cell(int i, int j, int k) const { return i + static_cast<long>(nx) * (j + static_cast<long>(ny) * k); }
  long point(int i, int j, int k) const {
    return i + static_cast<long>(nx + 1) * (j + static_cast<long>(ny + 1) * k);
  }
};

struct Key3Hash {
  std::size_t operator()(const std::array<std::int64_t, 3>& k) const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (auto c : k) { h ^= static_cast<std::uint64_t>(c); h *= 1099511628211ull; h ^= h >> 31; }
    return static_cast<std::size_t>(h);
  }
};

// --- convex polygon clipping (Sutherland-Hodgman) --------------------------
struct HalfSpace {
  Vec3 n{1, 0, 0};
  double d = 0.0;  // keep dot(n,x) >= d
};

double polygon_area(const std::vector<Vec3>& p) {
  Vec3 s{0, 0, 0};
  for (std::size_t i = 0; i < p.size(); ++i) s += cross(p[i], p[(i + 1) % p.size()]);
  return 0.5 * norm(s);
}

std::vector<Vec3> clip_polygon(const std::vector<Vec3>& poly, const HalfSpace& hs, double tol) {
  std::vector<Vec3> out;
  if (poly.empty()) return out;
  const std::size_t n = poly.size();
  out.reserve(n + 4);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3& a = poly[i];
    const Vec3& b = poly[(i + 1) % n];
    const double da = dot(hs.n, a) - hs.d;
    const double db = dot(hs.n, b) - hs.d;
    const bool ina = da >= -tol;
    const bool inb = db >= -tol;
    if (ina) out.push_back(a);
    if (ina != inb) {
      const double t = da / (da - db);
      out.push_back(a + (b - a) * t);
    }
  }
  return out;
}

// Polygon of a plane clipped by a list of half-spaces (skipping index skip).
bool plane_polygon(const Vec3& n, double d, const std::vector<HalfSpace>& hs, std::size_t skip, double R,
                   double tol, const Vec3& seed_centre, std::vector<Vec3>& poly) {
  const Vec3 nn = normalized(n);
  // tangent basis
  Vec3 helper = (std::fabs(nn.x) < 0.9) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  const Vec3 u = normalized(cross(nn, helper));
  const Vec3 v = cross(nn, u);
  // closest point of the plane to the cell centre = seed of the square
  const Vec3 c = seed_centre - nn * (dot(nn, seed_centre) - d);
  poly.clear();
  poly.push_back(c + u * R + v * R);
  poly.push_back(c - u * R + v * R);
  poly.push_back(c - u * R - v * R);
  poly.push_back(c + u * R - v * R);
  for (std::size_t i = 0; i < hs.size(); ++i) {
    if (i == skip) continue;
    poly = clip_polygon(poly, hs[i], tol);
    if (poly.size() < 3) return false;
  }
  return poly.size() >= 3 && polygon_area(poly) > 1e-18;
}


// Exact triangle / axis-aligned-box overlap test (separating-axis theorem,
// Akenine-Moller). Needed so that a triangle is binned only into the cells it
// really touches -- over-binning would mark the whole grid as "cut".
bool tri_box_overlap(const Vec3& centre, const Vec3& half, const Vec3 v[3]) {
  Vec3 p[3] = {v[0] - centre, v[1] - centre, v[2] - centre};
  // box axes
  for (int a = 0; a < 3; ++a) {
    const double lo = std::min({(a == 0 ? p[0].x : (a == 1 ? p[0].y : p[0].z)),
                                (a == 0 ? p[1].x : (a == 1 ? p[1].y : p[1].z)),
                                (a == 0 ? p[2].x : (a == 1 ? p[2].y : p[2].z))});
    const double hi = std::max({(a == 0 ? p[0].x : (a == 1 ? p[0].y : p[0].z)),
                                (a == 0 ? p[1].x : (a == 1 ? p[1].y : p[1].z)),
                                (a == 0 ? p[2].x : (a == 1 ? p[2].y : p[2].z))});
    const double h = (a == 0) ? half.x : (a == 1 ? half.y : half.z);
    if (lo > h || hi < -h) return false;
  }
  // plane / box overlap
  const Vec3 e0 = p[1] - p[0], e1 = p[2] - p[1], e2 = p[0] - p[2];
  const Vec3 nrm = cross(e0, -e2);
  const double d = dot(nrm, p[0]);
  const double r = half.x * std::fabs(nrm.x) + half.y * std::fabs(nrm.y) + half.z * std::fabs(nrm.z);
  if (std::fabs(d) > r) return false;
  // 9 cross-product axes
  const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const Vec3 edges[3] = {e0, e1, e2};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const Vec3 ax = cross(axes[i], edges[j]);
      const double axn = norm(ax);
      if (axn < 1e-15) continue;
      const Vec3 a = ax / axn;
      const double p0 = dot(a, p[0]), p1 = dot(a, p[1]), p2 = dot(a, p[2]);
      const double mn = std::min({p0, p1, p2}), mx = std::max({p0, p1, p2});
      const double rr = half.x * std::fabs(a.x) + half.y * std::fabs(a.y) + half.z * std::fabs(a.z);
      if (mn > rr || mx < -rr) return false;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Structured box grid
// ---------------------------------------------------------------------------
PolyMeshRaw make_box_grid(const BoxGridSpec& spec) {
  if (spec.nx < 1 || spec.ny < 1 || spec.nz < 1) fail("make_box_grid: need nx,ny,nz >= 1");
  PolyMeshRaw raw;
  const GridIdx gi{spec.nx, spec.ny, spec.nz};

  // graded coordinates (geometric stretching towards +axis)
  auto axis = [&](double a, double b, int n) {
    std::vector<double> c(static_cast<std::size_t>(n) + 1);
    if (std::fabs(spec.grading - 1.0) < 1e-12) {
      for (int i = 0; i <= n; ++i) c[static_cast<std::size_t>(i)] = a + (b - a) * i / n;
      return c;
    }
    const double q = spec.grading;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += std::pow(q, i);
    double s = 0.0;
    c[0] = a;
    for (int i = 0; i < n; ++i) {
      s += (b - a) * std::pow(q, i) / sum;
      c[static_cast<std::size_t>(i + 1)] = a + s;
    }
    c[static_cast<std::size_t>(n)] = b;
    return c;
  };
  const std::vector<double> xs = axis(spec.lo.x, spec.hi.x, spec.nx);
  const std::vector<double> ys = axis(spec.lo.y, spec.hi.y, spec.ny);
  const std::vector<double> zs = axis(spec.lo.z, spec.hi.z, spec.nz);

  raw.points.resize(static_cast<std::size_t>(gi.point(spec.nx, spec.ny, spec.nz)) + 1);
  for (int k = 0; k <= spec.nz; ++k)
    for (int j = 0; j <= spec.ny; ++j)
      for (int i = 0; i <= spec.nx; ++i)
        raw.points[static_cast<std::size_t>(gi.point(i, j, k))] =
            Vec3{xs[static_cast<std::size_t>(i)], ys[static_cast<std::size_t>(j)], zs[static_cast<std::size_t>(k)]};

  raw.n_cells = static_cast<gidx>(gi.cell(spec.nx - 1, spec.ny - 1, spec.nz - 1)) + 1;

  struct BF {
    idx v[4];
    idx owner;
    idx nb;   // -1 for boundary faces
  };
  std::vector<BF> internal_faces;
  std::vector<std::vector<BF>> boundary(6);
  internal_faces.reserve(static_cast<std::size_t>(3 * spec.nx * spec.ny * spec.nz));

  // x-normal faces: cells (i-1,j,k) | (i,j,k)
  for (int k = 0; k < spec.nz; ++k)
    for (int j = 0; j < spec.ny; ++j)
      for (int i = 0; i <= spec.nx; ++i) {
        BF f{{static_cast<idx>(gi.point(i, j, k)), static_cast<idx>(gi.point(i, j + 1, k)),
              static_cast<idx>(gi.point(i, j + 1, k + 1)), static_cast<idx>(gi.point(i, j, k + 1))},
             static_cast<idx>(gi.cell(std::max(i - 1, 0), j, k)), -1};
        if (i == 0) {
          std::swap(f.v[0], f.v[3]);
          std::swap(f.v[1], f.v[2]);
          boundary[0].push_back(f);
        } else if (i == spec.nx) {
          boundary[1].push_back(f);
        } else {
          f.nb = static_cast<idx>(gi.cell(i, j, k));
          internal_faces.push_back(f);
        }
      }
  // y-normal faces: cells (i,j-1,k) | (i,j,k)
  for (int k = 0; k < spec.nz; ++k)
    for (int j = 0; j <= spec.ny; ++j)
      for (int i = 0; i < spec.nx; ++i) {
        BF f{{static_cast<idx>(gi.point(i, j, k)), static_cast<idx>(gi.point(i, j, k + 1)),
              static_cast<idx>(gi.point(i + 1, j, k + 1)), static_cast<idx>(gi.point(i + 1, j, k))},
             static_cast<idx>(gi.cell(i, std::max(j - 1, 0), k)), -1};
        if (j == 0) {
          std::swap(f.v[0], f.v[3]);
          std::swap(f.v[1], f.v[2]);
          boundary[2].push_back(f);
        } else if (j == spec.ny) {
          boundary[3].push_back(f);
        } else {
          f.nb = static_cast<idx>(gi.cell(i, j, k));
          internal_faces.push_back(f);
        }
      }
  // z-normal faces: cells (i,j,k-1) | (i,j,k)
  for (int k = 0; k <= spec.nz; ++k)
    for (int j = 0; j < spec.ny; ++j)
      for (int i = 0; i < spec.nx; ++i) {
        BF f{{static_cast<idx>(gi.point(i, j, k)), static_cast<idx>(gi.point(i + 1, j, k)),
              static_cast<idx>(gi.point(i + 1, j + 1, k)), static_cast<idx>(gi.point(i, j + 1, k))},
             static_cast<idx>(gi.cell(i, j, std::max(k - 1, 0))), -1};
        if (k == 0) {
          std::swap(f.v[0], f.v[3]);
          std::swap(f.v[1], f.v[2]);
          boundary[4].push_back(f);
        } else if (k == spec.nz) {
          boundary[5].push_back(f);
        } else {
          f.nb = static_cast<idx>(gi.cell(i, j, k));
          internal_faces.push_back(f);
        }
      }

  // owner < neighbour, sorted by owner for solver locality
  std::sort(internal_faces.begin(), internal_faces.end(), [](const BF& a, const BF& b) {
    if (a.owner != b.owner) return a.owner < b.owner;
    return a.nb < b.nb;
  });
  for (const BF& f : internal_faces) {
    raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
    for (int k = 0; k < 4; ++k) raw.face_verts.push_back(f.v[k]);
    raw.owner.push_back(f.owner);
    raw.neighbour.push_back(f.nb);
  }
  std::vector<idx> patch_start(6, 0);
  for (int s = 0; s < 6; ++s) {
    patch_start[static_cast<std::size_t>(s)] = static_cast<idx>(raw.owner.size());
    std::sort(boundary[static_cast<std::size_t>(s)].begin(), boundary[static_cast<std::size_t>(s)].end(),
              [](const BF& a, const BF& b) { return a.owner < b.owner; });
    for (const BF& f : boundary[static_cast<std::size_t>(s)]) {
      raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
      for (int k = 0; k < 4; ++k) raw.face_verts.push_back(f.v[k]);
      raw.owner.push_back(f.owner);
    }
  }
  raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));

  idx cursor = static_cast<idx>(raw.n_internal());
  for (int s = 0; s < 6; ++s) {
    PatchInfo p;
    p.name = (spec.patch_names.size() == 6) ? spec.patch_names[static_cast<std::size_t>(s)]
                                            : ("patch" + std::to_string(s));
    p.type = (spec.patch_types.size() == 6) ? spec.patch_types[static_cast<std::size_t>(s)] : "patch";
    p.role = infer_patch_role(p.name, p.type);
    p.start = cursor;
    p.size = static_cast<idx>(boundary[static_cast<std::size_t>(s)].size());
    cursor += p.size;
    if (p.size > 0) raw.patches.push_back(std::move(p));
  }
  return raw;
}

// ---------------------------------------------------------------------------
// STL
// ---------------------------------------------------------------------------
void TriangleSoup::finalise() {
  const std::size_t nt = n_tris();
  lo = Vec3{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
  hi = -lo;
  for (const Vec3& v : verts) {
    lo = vmin(lo, v);
    hi = vmax(hi, v);
  }
  // ---- repair winding: make every triangle agree with its neighbours -------
  {
    const double tol = std::max(1e-9 * norm(hi - lo), 1e-300);
    const double inv = 1.0 / tol;
    std::unordered_map<std::array<std::int64_t, 3>, int, Key3Hash> vmap;
    std::vector<int> vid(3 * nt, -1);
    for (std::size_t i = 0; i < 3 * nt; ++i) {
      const std::array<std::int64_t, 3> key{static_cast<std::int64_t>(std::llround(verts[i].x * inv)),
                                            static_cast<std::int64_t>(std::llround(verts[i].y * inv)),
                                            static_cast<std::int64_t>(std::llround(verts[i].z * inv))};
      auto it = vmap.find(key);
      if (it == vmap.end()) {
        const int id = static_cast<int>(vmap.size());
        vmap.emplace(key, id);
        vid[i] = id;
      } else {
        vid[i] = it->second;
      }
    }
    // edge -> incident (triangle, direction) pairs
    struct Inc { int tri; int dir; };
    std::unordered_map<std::int64_t, std::vector<Inc>> edges;
    edges.reserve(3 * nt * 2);
    auto ekey = [](int a, int b) {
      return (static_cast<std::int64_t>(std::min(a, b)) << 32) | static_cast<std::int64_t>(std::max(a, b));
    };
    for (std::size_t t = 0; t < nt; ++t)
      for (int k = 0; k < 3; ++k) {
        const int a = vid[3 * t + static_cast<std::size_t>(k)];
        const int b = vid[3 * t + static_cast<std::size_t>((k + 1) % 3)];
        if (a == b) continue;
        edges[ekey(a, b)].push_back(Inc{static_cast<int>(t), a < b ? 1 : -1});
      }
    std::vector<char> flip(nt, 0), visited(nt, 0);
    std::vector<std::size_t> stack;
    for (std::size_t seed = 0; seed < nt; ++seed) {
      if (visited[seed]) continue;
      visited[seed] = 1;
      stack.push_back(seed);
      while (!stack.empty()) {
        const std::size_t t = stack.back();
        stack.pop_back();
        for (int k = 0; k < 3; ++k) {
          const int a = vid[3 * t + static_cast<std::size_t>(k)];
          const int b = vid[3 * t + static_cast<std::size_t>((k + 1) % 3)];
          if (a == b) continue;
          auto it = edges.find(ekey(a, b));
          if (it == edges.end()) continue;
          const int my_dir = (flip[t] ? -1 : 1) * (a < b ? 1 : -1);
          for (const Inc& inc : it->second) {
            if (inc.tri == static_cast<int>(t) || visited[static_cast<std::size_t>(inc.tri)]) continue;
            visited[static_cast<std::size_t>(inc.tri)] = 1;
            // neighbours must traverse the shared edge in opposite directions
            const int their_dir = inc.dir;
            if (their_dir == my_dir) flip[static_cast<std::size_t>(inc.tri)] = 1;
            stack.push_back(static_cast<std::size_t>(inc.tri));
          }
        }
      }
    }
    std::size_t n_flipped = 0;
    for (std::size_t t = 0; t < nt; ++t) {
      if (!flip[t]) continue;
      std::swap(verts[3 * t + 1], verts[3 * t + 2]);
      ++n_flipped;
    }
    if (n_flipped && n_flipped < nt) log_verbose("STL winding repaired on " + std::to_string(n_flipped) + " triangles");
  }
  // ---- global outward orientation + normals -------------------------------
  normals.resize(nt);
  double vol = 0.0;
  for (std::size_t t = 0; t < nt; ++t) {
    const Vec3 a = verts[3 * t], b = verts[3 * t + 1], c = verts[3 * t + 2];
    normals[t] = normalized(cross(b - a, c - a));
    vol += dot(a, cross(b, c)) / 6.0;
  }
  if (vol < 0.0) {
    // the whole soup was wound inwards: flip the geometry, not just the normals
    for (std::size_t t = 0; t < nt; ++t) {
      std::swap(verts[3 * t + 1], verts[3 * t + 2]);
      const Vec3 a = verts[3 * t], b = verts[3 * t + 1], c = verts[3 * t + 2];
      normals[t] = normalized(cross(b - a, c - a));
    }
  }
}

double TriangleSoup::area() const {
  double s = 0.0;
  for (std::size_t t = 0; t < n_tris(); ++t) {
    const Vec3 a = verts[3 * t], b = verts[3 * t + 1], c = verts[3 * t + 2];
    s += 0.5 * norm(cross(b - a, c - a));
  }
  return s;
}

TriangleSoup read_stl(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot open STL '" + path + "'");
  in.seekg(0, std::ios::end);
  const std::streamoff fsize = in.tellg();
  in.seekg(0);
  if (fsize < 15) fail("STL: file too short: " + path);

  std::string head(80, '\0');
  in.read(head.data(), 80);
  std::uint32_t ntri = 0;
  in.read(reinterpret_cast<char*>(&ntri), sizeof(ntri));

  TriangleSoup soup;
  // Binary STL layout: 80 byte header + uint32 count + 50 bytes per triangle.
  // Trust the size check first; fall back to the "solid ... facet" text probe.
  const bool size_says_binary =
      static_cast<std::streamoff>(84) + static_cast<std::streamoff>(ntri) * 50 == fsize;
  bool is_ascii = false;
  if (!size_says_binary) {
    in.clear();
    in.seekg(0);
    std::string probe(512, '\0');
    in.read(probe.data(), 512);
    probe.resize(static_cast<std::size_t>(in.gcount()));
    is_ascii = probe.rfind("solid", 0) == 0 && probe.find("facet") != std::string::npos;
  }
  in.clear();
  in.seekg(is_ascii ? 0 : 84);
  if (!is_ascii) {
    if (ntri == 0 || ntri > 200000000u) fail("STL: implausible triangle count in " + path);
    soup.verts.resize(static_cast<std::size_t>(ntri) * 3);
    in.seekg(84);
    // binary STL: 12B normal (3 x float32) + 36B vertices (9 x float32) + 2B attr
    unsigned char buf[50];
    for (std::uint32_t t = 0; t < ntri; ++t) {
      in.read(reinterpret_cast<char*>(buf), 50);
      if (!in) fail("STL: truncated binary file");
      for (int v = 0; v < 3; ++v) {
        float c[3];
        std::memcpy(c, buf + 12 + 12 * v, 12);
        soup.verts[3 * static_cast<std::size_t>(t) + static_cast<std::size_t>(v)] =
            Vec3{static_cast<double>(c[0]), static_cast<double>(c[1]), static_cast<double>(c[2])};
      }
    }
  } else {
    std::string tok;
    in >> tok;  // solid name
    while (in >> tok) {
      if (tok == "facet") {
        in >> tok >> tok >> tok;  // normal nx ny nz
        in >> tok;                // outer
        in >> tok;                // loop
        Vec3 v[3];
        for (int k = 0; k < 3; ++k) {
          in >> tok;  // vertex
          in >> v[k].x >> v[k].y >> v[k].z;
        }
        soup.verts.push_back(v[0]);
        soup.verts.push_back(v[1]);
        soup.verts.push_back(v[2]);
        in >> tok >> tok >> tok;  // endloop endfacet
      } else if (tok == "endsolid") {
        break;
      }
    }
  }
  if (soup.verts.empty()) fail("STL: no triangles in " + path);
  soup.verts.shrink_to_fit();
  soup.finalise();
  return soup;
}

void write_stl(const std::string& path, const TriangleSoup& soup) {
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write " + path);
  char head[80] = "DiFVM Mesher binary STL";
  out.write(head, 80);
  const std::uint32_t n = static_cast<std::uint32_t>(soup.n_tris());
  out.write(reinterpret_cast<const char*>(&n), 4);
  auto put3 = [&](const Vec3& v) { out.write(reinterpret_cast<const char*>(&v.x), 8); out.write(reinterpret_cast<const char*>(&v.y), 8); out.write(reinterpret_cast<const char*>(&v.z), 8); };
  for (std::size_t t = 0; t < soup.n_tris(); ++t) {
    put3(soup.normals[t]);
    put3(soup.verts[3 * t]);
    put3(soup.verts[3 * t + 1]);
    put3(soup.verts[3 * t + 2]);
    const std::uint16_t attr = 0;
    out.write(reinterpret_cast<const char*>(&attr), 2);
  }
}

// ---------------------------------------------------------------------------
// Cut-cell mesher
// ---------------------------------------------------------------------------
namespace {

inline bool ray_triangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3 e1 = b - a, e2 = c - a;
  const Vec3 pv = cross(d, e2);
  const double det = dot(e1, pv);
  if (std::fabs(det) < 1e-14) return false;
  const double inv = 1.0 / det;
  const Vec3 tv = o - a;
  const double u = dot(tv, pv) * inv;
  if (u < 0.0 || u > 1.0) return false;
  const Vec3 qv = cross(tv, e1);
  const double v = dot(d, qv) * inv;
  if (v < 0.0 || u + v > 1.0) return false;
  return dot(e2, qv) * inv > 1e-12;
}

inline Vec3 polygon_area_vector(const std::vector<Vec3>& p) {
  Vec3 s{0, 0, 0};
  for (std::size_t i = 0; i < p.size(); ++i) s += cross(p[i], p[(i + 1) % p.size()]);
  return s * 0.5;
}

}  // namespace

std::string CutCellStats::to_string() const {
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "cut-cell grid %dx%dx%d: cut=%ld solid=%ld dropped=%ld tris=%ld\n"
                "  grid vol=%.6g  fluid vol=%.6g  exact(STL) vol=%.6g  rel err=%.3e\n"
                "  wetted area=%.6g  time=%.1f ms",
                nx, ny, nz, n_cut_cells, n_solid_cells, n_dropped_slivers, n_triangles, grid_volume,
                fluid_volume, exact_volume,
                (exact_volume > 0.0 ? std::fabs(fluid_volume - exact_volume) / exact_volume : 0.0),
                surface_area, time_ms);
  return std::string(buf);
}

PolyMeshRaw make_cutcell_mesh(const TriangleSoup& soup, const CutCellOptions& opts, CutCellStats* stats) {
  Timer timer;
  if (soup.n_tris() == 0) fail("cut-cell mesher: empty triangle soup");
  const double h = opts.h;
  if (h <= 0.0) fail("cut-cell mesher: h must be > 0");

  Vec3 lo = soup.lo, hi = soup.hi;
  // pad + a small irrational-ish offset: geometry planes must never coincide
  // with grid planes (degenerate cut cells with zero thickness)
  // Irrational-ish jitter of the grid origin, ~1/3 cell: keeps geometry planes
  // (and surface tangency points) away from grid planes, which would otherwise
  // produce degenerate zero-thickness cut cells.
  const double jitter = h * 0.31721;
  const Vec3 pad{h * opts.pad_cells + jitter, h * opts.pad_cells + jitter * 1.7, h * opts.pad_cells + jitter * 2.3};
  lo -= pad;
  hi += pad;
  const int nx = std::max(1, static_cast<int>(std::ceil((hi.x - lo.x) / h)));
  const int ny = std::max(1, static_cast<int>(std::ceil((hi.y - lo.y) / h)));
  const int nz = std::max(1, static_cast<int>(std::ceil((hi.z - lo.z) / h)));
  const double dx = (hi.x - lo.x) / nx, dy = (hi.y - lo.y) / ny, dz = (hi.z - lo.z) / nz;
  const double cell_vol = dx * dy * dz;
  const long n_grid_cells = static_cast<long>(nx) * ny * nz;

  auto cell_of = [&](int i, int j, int k) { return i + static_cast<long>(nx) * (j + static_cast<long>(ny) * k); };
  auto cell_ijk = [&](long c, int& i, int& j, int& k) {
    i = static_cast<int>(c % nx);
    j = static_cast<int>((c / nx) % ny);
    k = static_cast<int>(c / (static_cast<long>(nx) * ny));
  };
  auto node = [&](int i, int j, int k) { return Vec3{lo.x + i * dx, lo.y + j * dy, lo.z + k * dz}; };

  // ---- 1. bin triangles into cells ---------------------------------------
  std::vector<std::vector<int>> cell_tris(static_cast<std::size_t>(n_grid_cells));
  for (std::size_t t = 0; t < soup.n_tris(); ++t) {
    Vec3 tlo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec3 thi = -tlo;
    for (int k = 0; k < 3; ++k) {
      tlo = vmin(tlo, soup.verts[3 * t + static_cast<std::size_t>(k)]);
      thi = vmax(thi, soup.verts[3 * t + static_cast<std::size_t>(k)]);
    }
    const int i0 = std::max(0, static_cast<int>(std::floor((tlo.x - lo.x) / dx)));
    const int i1 = std::min(nx - 1, static_cast<int>(std::floor((thi.x - lo.x) / dx)));
    const int j0 = std::max(0, static_cast<int>(std::floor((tlo.y - lo.y) / dy)));
    const int j1 = std::min(ny - 1, static_cast<int>(std::floor((thi.y - lo.y) / dy)));
    const int k0 = std::max(0, static_cast<int>(std::floor((tlo.z - lo.z) / dz)));
    const int k1 = std::min(nz - 1, static_cast<int>(std::floor((thi.z - lo.z) / dz)));
    const Vec3 tri[3] = {soup.verts[3 * t], soup.verts[3 * t + 1], soup.verts[3 * t + 2]};
    for (int k = k0; k <= k1; ++k)
      for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
          const Vec3 centre{lo.x + (i + 0.5) * dx, lo.y + (j + 0.5) * dy, lo.z + (k + 0.5) * dz};
          const Vec3 half{0.5 * dx, 0.5 * dy, 0.5 * dz};
          if (!tri_box_overlap(centre, half, tri)) continue;
          cell_tris[static_cast<std::size_t>(cell_of(i, j, k))].push_back(static_cast<int>(t));
        }
  }
  std::vector<char> cut(static_cast<std::size_t>(n_grid_cells), 0);
  for (long c = 0; c < n_grid_cells; ++c)
    cut[static_cast<std::size_t>(c)] = cell_tris[static_cast<std::size_t>(c)].empty() ? 0 : 1;

  // ---- 2. flood fill (outside region) + ray-parity for enclosed cavities --
  std::vector<char> fluid(static_cast<std::size_t>(n_grid_cells), 0);
  {
    std::vector<long> stack;
    auto push = [&](long c) {
      if (!cut[static_cast<std::size_t>(c)] && !fluid[static_cast<std::size_t>(c)]) {
        fluid[static_cast<std::size_t>(c)] = 1;
        stack.push_back(c);
      }
    };
    for (int k = 0; k < nz; ++k)
      for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
          if (i == 0 || j == 0 || k == 0 || i == nx - 1 || j == ny - 1 || k == nz - 1) push(cell_of(i, j, k));
    while (!stack.empty()) {
      const long c = stack.back();
      stack.pop_back();
      int i, j, k;
      cell_ijk(c, i, j, k);
      if (i > 0) push(cell_of(i - 1, j, k));
      if (i < nx - 1) push(cell_of(i + 1, j, k));
      if (j > 0) push(cell_of(i, j - 1, k));
      if (j < ny - 1) push(cell_of(i, j + 1, k));
      if (k > 0) push(cell_of(i, j, k - 1));
      if (k < nz - 1) push(cell_of(i, j, k + 1));
    }
    // unvisited non-cut cells: decide by ray parity (handles internal voids)
    std::vector<int> stamp(soup.n_tris(), -1);
    const Vec3 dir = normalized(Vec3{1.0, 0.00317, 0.00191});
    for (long c = 0; c < n_grid_cells; ++c) {
      if (cut[static_cast<std::size_t>(c)] || fluid[static_cast<std::size_t>(c)]) continue;
      int i0, j0, k0;
      cell_ijk(c, i0, j0, k0);
      const Vec3 o{lo.x + (i0 + 0.5) * dx, lo.y + (j0 + 0.5) * dy, lo.z + (k0 + 0.5) * dz};
      int crossings = 0;
      for (int i = i0; i < nx; ++i) {
        const double xm = lo.x + (i + 0.5) * dx;
        const double tt = (xm - o.x) / dir.x;
        if (tt < 0.0) continue;
        const Vec3 q = o + dir * tt;
        if (q.y < lo.y || q.y > hi.y || q.z < lo.z || q.z > hi.z) break;
        const int jj = std::min(ny - 1, std::max(0, static_cast<int>((q.y - lo.y) / dy)));
        const int kk = std::min(nz - 1, std::max(0, static_cast<int>((q.z - lo.z) / dz)));
        for (int t : cell_tris[static_cast<std::size_t>(cell_of(i, jj, kk))]) {
          if (stamp[static_cast<std::size_t>(t)] == static_cast<int>(c)) continue;
          stamp[static_cast<std::size_t>(t)] = static_cast<int>(c);
          if (ray_triangle(o, dir, soup.verts[3 * static_cast<std::size_t>(t)],
                           soup.verts[3 * static_cast<std::size_t>(t) + 1],
                           soup.verts[3 * static_cast<std::size_t>(t) + 2]))
            ++crossings;
        }
      }
      if (std::getenv("DIFVM_CUTCELL_PARITY") && (crossings != 0 || (j0 == 2 && k0 == 2)))
        std::fprintf(stderr, "parity cell (%d,%d,%d) crossings=%d o=(%+.4f %+.4f %+.4f)\n", i0, j0, k0, crossings, o.x, o.y, o.z);
      if (crossings % 2 == 0) fluid[static_cast<std::size_t>(c)] = 1;  // outside the solid
    }
  }

  if (std::getenv("DIFVM_CUTCELL_PARITY")) {
    int n_flood = 0, n_cut_only = 0, n_neither = 0;
    for (long c = 0; c < n_grid_cells; ++c) {
      if (cut[static_cast<std::size_t>(c)]) ++n_cut_only;
      else if (fluid[static_cast<std::size_t>(c)]) ++n_flood;
      else ++n_neither;
    }
    std::fprintf(stderr, "CLASSIFY cut=%d flooded=%d unreached=%d (grid %dx%dx%d)\n", n_cut_only, n_flood, n_neither, nx, ny, nz);
    for (int kk2 = 2; kk2 <= 2; ++kk2)
      for (int jj2 = 2; jj2 <= 2; ++jj2)
        for (int ii2 = 0; ii2 < nx; ++ii2) {
          const long c = cell_of(ii2, jj2, kk2);
          std::fprintf(stderr, "  cell (%d,%d,%d) cut=%d fluid=%d tris=%zu\n", ii2, jj2, kk2,
                       (int)cut[static_cast<std::size_t>(c)], (int)fluid[static_cast<std::size_t>(c)],
                       cell_tris[static_cast<std::size_t>(c)].size());
          if (ii2 == 2) {
            for (int t : cell_tris[static_cast<std::size_t>(c)])
              std::fprintf(stderr, "    tri %d: (%+.4f %+.4f %+.4f) (%+.4f %+.4f %+.4f) (%+.4f %+.4f %+.4f)\n", t,
                           soup.verts[3*t].x, soup.verts[3*t].y, soup.verts[3*t].z,
                           soup.verts[3*t+1].x, soup.verts[3*t+1].y, soup.verts[3*t+1].z,
                           soup.verts[3*t+2].x, soup.verts[3*t+2].y, soup.verts[3*t+2].z);
          }
        }
  }

  // ---- 3. node level set (signed distance, positive in the fluid) --------
  // The cut geometry is driven by a single scalar field sampled at the grid
  // *nodes*.  Both cells sharing a face interpolate the same four node values,
  // so their clipped polygons are identical -> the cut-cell layer is
  // conforming by construction (no per-cell plane that could disagree).
  const int nxn = nx + 1, nyn = ny + 1, nzn = nz + 1;
  auto nid = [&](int i, int j, int k) -> std::size_t {
    return (static_cast<std::size_t>(i) * static_cast<std::size_t>(nyn) + static_cast<std::size_t>(j)) *
               static_cast<std::size_t>(nzn) +
           static_cast<std::size_t>(k);
  };
  std::vector<double> phi(static_cast<std::size_t>(nxn) * static_cast<std::size_t>(nyn) *
                              static_cast<std::size_t>(nzn),
                          0.0);

  {
    // uniform bucket grid over the padded domain, ~3h cells, triangles
    // inserted into every bucket their AABB touches (plus one ring)
    const double bs = 3.0 * std::max({dx, dy, dz});
    const int bx = std::max(1, static_cast<int>(std::ceil((hi.x - lo.x) / bs)));
    const int by = std::max(1, static_cast<int>(std::ceil((hi.y - lo.y) / bs)));
    const int bz = std::max(1, static_cast<int>(std::ceil((hi.z - lo.z) / bs)));
    auto bkey = [&](int i, int j, int k) -> std::size_t {
      return (static_cast<std::size_t>(i) * static_cast<std::size_t>(by) + static_cast<std::size_t>(j)) *
                 static_cast<std::size_t>(bz) +
             static_cast<std::size_t>(k);
    };
    std::vector<std::vector<int>> buckets(static_cast<std::size_t>(bx) * static_cast<std::size_t>(by) *
                                          static_cast<std::size_t>(bz));
    auto bucket_range = [&](double v_lo, double v_hi, double o, double st, int n, int& i0, int& i1) {
      i0 = static_cast<int>(std::floor((v_lo - o) / st)) - 1;
      i1 = static_cast<int>(std::floor((v_hi - o) / st)) + 1;
      i0 = std::max(0, std::min(n - 1, i0));
      i1 = std::max(0, std::min(n - 1, i1));
    };
    for (std::size_t t = 0; t < soup.n_tris(); ++t) {
      const Vec3& a = soup.verts[3 * t];
      const Vec3& b = soup.verts[3 * t + 1];
      const Vec3& c = soup.verts[3 * t + 2];
      const Vec3 tlo{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})};
      const Vec3 thi{std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})};
      int i0, i1, j0, j1, k0, k1;
      bucket_range(tlo.x, thi.x, lo.x, (hi.x - lo.x) / bx, bx, i0, i1);
      bucket_range(tlo.y, thi.y, lo.y, (hi.y - lo.y) / by, by, j0, j1);
      bucket_range(tlo.z, thi.z, lo.z, (hi.z - lo.z) / bz, bz, k0, k1);
      for (int i = i0; i <= i1; ++i)
        for (int j = j0; j <= j1; ++j)
          for (int k = k0; k <= k1; ++k) buckets[bkey(i, j, k)].push_back(static_cast<int>(t));
    }
    // nearest-triangle distance + sign
    const double big = 1e9;
    for (int k = 0; k < nzn; ++k)
      for (int j = 0; j < nyn; ++j)
        for (int i = 0; i < nxn; ++i) {
          const Vec3 p{lo.x + i * dx, lo.y + j * dy, lo.z + k * dz};
          const int bi = std::max(0, std::min(bx - 1, static_cast<int>((p.x - lo.x) / ((hi.x - lo.x) / bx))));
          const int bj = std::max(0, std::min(by - 1, static_cast<int>((p.y - lo.y) / ((hi.y - lo.y) / by))));
          const int bk = std::max(0, std::min(bz - 1, static_cast<int>((p.z - lo.z) / ((hi.z - lo.z) / bz))));
          double best = big;
          Vec3 best_n{0, 0, 1}, best_q = p;
          for (int ring = 1; ring <= 3 && best >= big; ++ring)
          for (int di = -ring; di <= ring; ++di)
            for (int dj = -ring; dj <= ring; ++dj)
              for (int dk = -ring; dk <= ring; ++dk) {
                if (ring > 1 && std::abs(di) < ring && std::abs(dj) < ring && std::abs(dk) < ring) continue;
                const int ii = bi + di, jj = bj + dj, kk = bk + dk;
                if (ii < 0 || jj < 0 || kk < 0 || ii >= bx || jj >= by || kk >= bz) continue;
                for (int t : buckets[bkey(ii, jj, kk)]) {
                  const Vec3& a = soup.verts[3 * static_cast<std::size_t>(t)];
                  const Vec3& b = soup.verts[3 * static_cast<std::size_t>(t) + 1];
                  const Vec3& c = soup.verts[3 * static_cast<std::size_t>(t) + 2];
                  // closest point on the triangle (Ericson, Real-Time Collision Detection)
                  const Vec3 ab = b - a, ac = c - a, ap = p - a;
                  const double d1 = dot(ab, ap), d2 = dot(ac, ap);
                  Vec3 q;
                  if (d1 <= 0.0 && d2 <= 0.0) {
                    q = a;
                  } else {
                    const Vec3 bp = p - b;
                    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
                    if (d3 >= 0.0 && d4 <= d3) {
                      q = b;
                    } else {
                      const double vc = d1 * d4 - d3 * d2;
                      if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                        q = a + ab * (d1 / (d1 - d3));
                      } else {
                        const Vec3 cp = p - c;
                        const double d5 = dot(ab, cp), d6 = dot(ac, cp);
                        if (d6 >= 0.0 && d5 <= d6) {
                          q = c;
                        } else {
                          const double vb = d5 * d2 - d1 * d6;
                          if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                            q = a + ac * (d2 / (d2 - d6));
                          } else {
                            const double va = d3 * d6 - d5 * d4;
                            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
                              q = b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
                            } else {
                              const double den = 1.0 / (va + vb + vc);
                              q = a + ab * (vb * den) + ac * (vc * den);
                            }
                          }
                        }
                      }
                    }
                  }
                  const double d2sq = dot(p - q, p - q);
                  if (d2sq < best) {
                    best = d2sq;
                    best_q = q;
                    best_n = soup.normals[static_cast<std::size_t>(t)];
                  }
                }
              }
          // Magnitude is always the true distance; the *sign* comes from the
          // closest triangle close to the surface (where its plane is a
          // reliable inside/outside test) and from the fluid/solid
          // classification of the containing cell further away.
          const int ci = std::min(nx - 1, i), cj = std::min(ny - 1, j), ck = std::min(nz - 1, k);
          const long cc = cell_of(ci, cj, ck);
          const double cls_sign = fluid[static_cast<std::size_t>(cc)] ? 1.0 : -1.0;
          double val = big;
          double sgn = cls_sign;
          if (best < big) {
            val = std::sqrt(best);
            if (val <= 2.0 * std::max({dx, dy, dz})) sgn = (dot(best_n, p - best_q) < 0.0) ? -1.0 : 1.0;
          }
          phi[nid(i, j, k)] = sgn * val;
        }
  }

  // ---- 4. build the (conforming) cut cells -------------------------------
  PolyMeshRaw raw;
  std::unordered_map<std::array<std::int64_t, 3>, idx, Key3Hash> pmap;
  const double q = h * opts.point_quantum;
  const double invq = 1.0 / q;
  auto get_point = [&](const Vec3& p) -> idx {
    const std::array<std::int64_t, 3> key{static_cast<std::int64_t>(std::llround(p.x * invq)),
                                          static_cast<std::int64_t>(std::llround(p.y * invq)),
                                          static_cast<std::int64_t>(std::llround(p.z * invq))};
    auto it = pmap.find(key);
    if (it != pmap.end()) return it->second;
    const idx id = static_cast<idx>(raw.points.size());
    raw.points.push_back(Vec3{static_cast<double>(key[0]) * q, static_cast<double>(key[1]) * q,
                              static_cast<double>(key[2]) * q});
    pmap.emplace(key, id);
    return id;
  };

  CutCellStats st;
  st.nx = nx; st.ny = ny; st.nz = nz;
  st.n_triangles = static_cast<long>(soup.n_tris());
  st.grid_volume = 0.0;
  for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i) st.grid_volume += dx * dy * dz;
  st.exact_volume = 0.0;
  for (std::size_t t = 0; t < soup.n_tris(); ++t) {
    const Vec3 a = soup.verts[3 * t], b = soup.verts[3 * t + 1], c = soup.verts[3 * t + 2];
    st.exact_volume += dot(a, cross(b, c)) / 6.0;
  }
  st.exact_volume = std::fabs(st.exact_volume);

  const Vec3 axis_out[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  // corner offsets (node ids) of the six faces of a cell, in the same order as
  // the quad used for the geometry
  const int face_nodes[6][4][3] = {
      {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},   // -x
      {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},   // +x
      {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},   // -y
      {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},   // +y
      {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},   // -z
      {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};  // +z

  gidx cell_count = 0;
  std::vector<std::vector<Vec3>> polys;
  std::vector<int> poly_dir;
  std::vector<char> poly_is_cap;
  polys.reserve(12);
  poly_dir.reserve(12);
  poly_is_cap.reserve(12);

  for (int k = 0; k < nz; ++k)
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i) {
        const long c = cell_of(i, j, k);
        // trivially solid cells (all eight corners inside the solid)
        bool any_fluid = false;
        for (int dk = 0; dk <= 1; ++dk)
          for (int dj = 0; dj <= 1; ++dj)
            for (int di = 0; di <= 1; ++di)
              if (phi[nid(i + di, j + dj, k + dk)] > 0.0) any_fluid = true;
        if (!any_fluid) {
          ++st.n_solid_cells;
          continue;
        }
        const bool is_cut = cut[static_cast<std::size_t>(c)] != 0;
        if (!is_cut && !fluid[static_cast<std::size_t>(c)]) {
          ++st.n_solid_cells;
          continue;
        }

        polys.clear();
        poly_dir.clear();
        poly_is_cap.clear();
        std::vector<Vec3> chords;   // crossing points on the cell faces
        std::vector<std::pair<Vec3, Vec3>> segments;   // chord of each cut face
        for (int dir = 0; dir < 6; ++dir) {
          Vec3 quad[4];
          double f[4];
          for (int e = 0; e < 4; ++e) {
            const int ni = i + face_nodes[dir][e][0];
            const int nj = j + face_nodes[dir][e][1];
            const int nk = k + face_nodes[dir][e][2];
            quad[e] = Vec3{lo.x + ni * dx, lo.y + nj * dy, lo.z + nk * dz};
            double fv = phi[nid(ni, nj, nk)];
            // a node exactly on the surface would put two crossings on the same
            // point and break the chord bookkeeping; nudge it off
            const double eps_zero = 1e-7 * static_cast<double>(std::max({dx, dy, dz}));
            if (std::fabs(fv) < eps_zero) fv = (fv < 0.0) ? -eps_zero : eps_zero;
            f[e] = fv;
          }
          std::vector<Vec3> poly;
          std::vector<Vec3> local_chords;
          for (int e = 0; e < 4; ++e) {
            const Vec3& p0 = quad[e];
            const Vec3& p1 = quad[(e + 1) % 4];
            const double f0 = f[e], f1 = f[(e + 1) % 4];
            const bool in0 = f0 >= 0.0, in1 = f1 >= 0.0;
            if (in0) poly.push_back(p0);
            if (in0 != in1) {
              const double t = f0 / (f0 - f1);
              const Vec3 x = p0 + (p1 - p0) * t;
              poly.push_back(x);
              local_chords.push_back(x);
            }
          }
          if (poly.size() < 3) continue;
          // collinear / degenerate faces are dropped
          if (polygon_area(poly) <= 1e-24) continue;
          polys.push_back(std::move(poly));
          poly_dir.push_back(dir);
          poly_is_cap.push_back(0);
          for (std::size_t q2 = 0; q2 + 1 < local_chords.size(); q2 += 2)
            segments.emplace_back(local_chords[q2], local_chords[q2 + 1]);
          for (const Vec3& x : local_chords) {
            bool dup = false;
            for (const Vec3& y : chords)
              if (norm(x - y) <= 1e-12 * h) { dup = true; break; }
            if (!dup) chords.push_back(x);
          }
        }
        if (polys.empty()) { ++st.n_solid_cells; continue; }

        // ---- surface patch: cone from the chord-loop to its centroid ------
        std::vector<Vec3> cap_tris;   // stored as triangles (3 points each)
        if (!chords.empty() && chords.size() >= 3) {
          Vec3 mid{0, 0, 0};
          for (const Vec3& x : chords) mid += x;
          mid = mid / static_cast<double>(chords.size());
          // rebuild the chords per face (ordered pairs) to fan the patch
          for (std::size_t fi = 0; fi < poly_is_cap.size(); ++fi) {
            if (poly_is_cap[fi]) continue;
          }
          // Chain the face chords into closed loops (a cell can be cut by more
          // than one surface patch).  Each loop is fanned from its centroid;
          // the fan closes exactly, because the sum of its triangle area
          // vectors is the loop's area vector whatever the apex is.
          std::vector<std::vector<Vec3>> loops;
          {
            std::vector<char> used(segments.size(), 0);
            const double eps = 1e-9 * h;
            for (std::size_t s0 = 0; s0 < segments.size(); ++s0) {
              if (used[s0]) continue;
              used[s0] = 1;
              std::vector<Vec3> loop{segments[s0].first, segments[s0].second};
              bool progress = true;
              while (progress) {
                progress = false;
                for (std::size_t q2 = 0; q2 < segments.size(); ++q2) {
                  if (used[q2]) continue;
                  if (norm(segments[q2].first - loop.back()) <= eps) {
                    loop.push_back(segments[q2].second);
                    used[q2] = 1;
                    progress = true;
                  } else if (norm(segments[q2].second - loop.back()) <= eps) {
                    loop.push_back(segments[q2].first);
                    used[q2] = 1;
                    progress = true;
                  }
                }
              }
              if (loop.size() > 1 && norm(loop.front() - loop.back()) <= eps) loop.pop_back();
              if (loop.size() >= 3) loops.push_back(std::move(loop));
            }
          }
          if (loops.empty()) {
            // degenerate chord bookkeeping: order everything by angle instead
            Vec3 nref{0, 0, 0};
            for (int t : cell_tris[static_cast<std::size_t>(c)]) {
              const Vec3& va = soup.verts[3 * static_cast<std::size_t>(t)];
              const Vec3& vb = soup.verts[3 * static_cast<std::size_t>(t) + 1];
              const Vec3& vc = soup.verts[3 * static_cast<std::size_t>(t) + 2];
              nref += cross(vb - va, vc - va) * 0.5;
            }
            if (norm(nref) < 1e-18) nref = Vec3{0, 0, 1};
            const Vec3 nrm = normalized(nref);
            const Vec3 helper = (std::fabs(nrm.x) < 0.9) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            const Vec3 uu = normalized(cross(nrm, helper));
            const Vec3 vv = cross(nrm, uu);
            std::vector<std::pair<double, Vec3>> ordered;
            for (const Vec3& x : chords) {
              const Vec3 d = x - mid;
              ordered.emplace_back(std::atan2(dot(d, vv), dot(d, uu)), x);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [](const std::pair<double, Vec3>& a, const std::pair<double, Vec3>& b) { return a.first < b.first; });
            std::vector<Vec3> loop;
            for (const auto& pr : ordered) loop.push_back(pr.second);
            if (loop.size() >= 3) loops.push_back(std::move(loop));
          }
          for (const auto& loop : loops) {
            Vec3 apex{0, 0, 0};
            for (const Vec3& x : loop) apex += x;
            apex = apex / static_cast<double>(loop.size());
            for (std::size_t a2 = 0; a2 < loop.size(); ++a2) {
              const Vec3& x0 = loop[a2];
              const Vec3& x1 = loop[(a2 + 1) % loop.size()];
              if (norm(x1 - x0) <= 1e-12 * h) continue;
              cap_tris.push_back(apex);
              cap_tris.push_back(x0);
              cap_tris.push_back(x1);
            }
          }
        }

        // outward direction of the surface patch: into the solid = -grad(phi)
        const Vec3 cap_grad{
            (phi[nid(i + 1, j, k)] - phi[nid(i, j, k)] + phi[nid(i + 1, j + 1, k)] - phi[nid(i, j + 1, k)] +
             phi[nid(i + 1, j, k + 1)] - phi[nid(i, j, k + 1)] + phi[nid(i + 1, j + 1, k + 1)] - phi[nid(i, j + 1, k + 1)]) / (8.0 * dx),
            (phi[nid(i, j + 1, k)] - phi[nid(i, j, k)] + phi[nid(i + 1, j + 1, k)] - phi[nid(i + 1, j, k)] +
             phi[nid(i, j + 1, k + 1)] - phi[nid(i, j, k + 1)] + phi[nid(i + 1, j + 1, k + 1)] - phi[nid(i + 1, j, k + 1)]) / (8.0 * dy),
            (phi[nid(i, j, k + 1)] - phi[nid(i, j, k)] + phi[nid(i + 1, j, k + 1)] - phi[nid(i + 1, j, k)] +
             phi[nid(i, j + 1, k + 1)] - phi[nid(i, j + 1, k)] + phi[nid(i + 1, j + 1, k + 1)] - phi[nid(i + 1, j + 1, k)]) / (8.0 * dz)};
        const Vec3 cap_outward = (norm(cap_grad) > 1e-12) ? Vec3{-cap_grad.x, -cap_grad.y, -cap_grad.z} : Vec3{0, 0, -1};

        // ---- assemble the cell --------------------------------------------
        Vec3 closure{0, 0, 0};
        double abs_area = 0.0, vol = 0.0;
        std::vector<std::vector<idx>> cell_faces;
        std::vector<int> cell_face_dir;
        for (std::size_t fi = 0; fi < polys.size(); ++fi) {
          const auto& poly = polys[fi];
          Vec3 S{0, 0, 0}, xc{0, 0, 0};
          for (std::size_t e = 0; e < poly.size(); ++e) {
            S += cross(poly[e], poly[(e + 1) % poly.size()]);
            xc += poly[e];
          }
          S = S * 0.5;
          xc = xc / static_cast<double>(poly.size());
          if (dot(S, axis_out[poly_dir[fi]]) < 0.0) S = -S;
          vol += dot(xc, S) / 3.0;
          closure += S;
          abs_area += norm(S);
          std::vector<idx> ids;
          ids.reserve(poly.size());
          for (const Vec3& p : poly) {
            const idx id = get_point(p);
            if (ids.empty() || ids.back() != id) ids.push_back(id);
          }
          while (ids.size() > 1 && ids.front() == ids.back()) ids.pop_back();
          if (ids.size() < 3) continue;
          cell_faces.push_back(std::move(ids));
          cell_face_dir.push_back(poly_dir[fi]);
        }
        // cap (surface patch)
        std::vector<std::vector<idx>> cap_faces;
        bool degenerate_cap = false;
        if (!cap_tris.empty()) {
          const Vec3 outward = cap_outward;
          Vec3 cap_S{0, 0, 0};
          for (std::size_t t = 0; t < cap_tris.size(); t += 3)
            cap_S += cross(cap_tris[t + 1] - cap_tris[t], cap_tris[t + 2] - cap_tris[t]) * 0.5;
          const bool flip = dot(cap_S, outward) < 0.0;
          for (std::size_t t = 0; t < cap_tris.size(); t += 3) {
            const Vec3& p0 = cap_tris[t];
            const Vec3& p1 = flip ? cap_tris[t + 2] : cap_tris[t + 1];
            const Vec3& p2 = flip ? cap_tris[t + 1] : cap_tris[t + 2];
            std::vector<idx> ids;
            for (const Vec3& p : {p0, p1, p2}) {
              const idx id = get_point(p);
              if (ids.empty() || ids.back() != id) ids.push_back(id);
            }
            if (ids.size() != 3) { degenerate_cap = true; continue; }
            {
              // two cap triangles must never collapse onto the same vertices
              std::array<idx, 3> key{ids[0], ids[1], ids[2]};
              std::sort(key.begin(), key.end());
              for (const auto& other : cap_faces) {
                std::array<idx, 3> okey{other[0], other[1], other[2]};
                std::sort(okey.begin(), okey.end());
                if (okey == key) { degenerate_cap = true; break; }
              }
              for (const auto& other : cell_faces) {
                if (other.size() != 3) continue;
                std::array<idx, 3> okey{other[0], other[1], other[2]};
                std::sort(okey.begin(), okey.end());
                if (okey == key) { degenerate_cap = true; break; }
              }
            }
            Vec3 S = cross(raw.points[static_cast<std::size_t>(ids[1])] - raw.points[static_cast<std::size_t>(ids[0])],
                           raw.points[static_cast<std::size_t>(ids[2])] - raw.points[static_cast<std::size_t>(ids[0])]) * 0.5;
            Vec3 xc = (raw.points[static_cast<std::size_t>(ids[0])] + raw.points[static_cast<std::size_t>(ids[1])] +
                       raw.points[static_cast<std::size_t>(ids[2])]) / 3.0;
            vol += dot(xc, S) / 3.0;
            closure += S;
            abs_area += norm(S);
            st.surface_area += norm(S);
            cap_faces.push_back(std::move(ids));
          }
        }


        // If the chord loops did not close the cell (degenerate/tangency
        // configurations), retry with an angular ordering over all crossing
        // points: that fan is closed by construction.
        double cell_ref = dx * dy * dz;
        double closed = (abs_area > 0.0) ? norm(closure) / abs_area : 1.0;
        if (!(closed < 1e-6) && chords.size() >= 3) {
          std::vector<Vec3> cap2;
          Vec3 nref{0, 0, 0};
          for (int t : cell_tris[static_cast<std::size_t>(c)]) {
            const Vec3& va = soup.verts[3 * static_cast<std::size_t>(t)];
            const Vec3& vb = soup.verts[3 * static_cast<std::size_t>(t) + 1];
            const Vec3& vc = soup.verts[3 * static_cast<std::size_t>(t) + 2];
            nref += cross(vb - va, vc - va) * 0.5;
          }
          if (norm(nref) < 1e-18) nref = Vec3{0, 0, 1};
          const Vec3 nrm = normalized(nref);
          const Vec3 helper = (std::fabs(nrm.x) < 0.9) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
          const Vec3 uu = normalized(cross(nrm, helper));
          const Vec3 vv = cross(nrm, uu);
          Vec3 mid2{0, 0, 0};
          for (const Vec3& x : chords) mid2 += x;
          mid2 = mid2 / static_cast<double>(chords.size());
          std::vector<std::pair<double, Vec3>> ordered;
          for (const Vec3& x : chords) {
            const Vec3 d = x - mid2;
            ordered.emplace_back(std::atan2(dot(d, vv), dot(d, uu)), x);
          }
          std::sort(ordered.begin(), ordered.end(),
                    [](const std::pair<double, Vec3>& a, const std::pair<double, Vec3>& b) { return a.first < b.first; });
          std::vector<Vec3> loop;
          for (const auto& pr : ordered) loop.push_back(pr.second);
          {
            Vec3 apex{0, 0, 0};
            for (const Vec3& x : loop) apex += x;
            apex = apex / static_cast<double>(loop.size());
            for (std::size_t a2 = 0; a2 < loop.size(); ++a2) {
              const Vec3& x0 = loop[a2];
              const Vec3& x1 = loop[(a2 + 1) % loop.size()];
              if (norm(x1 - x0) <= 1e-12 * h) continue;
              cap2.push_back(apex);
              cap2.push_back(x0);
              cap2.push_back(x1);
            }
          }
          if (!cap2.empty()) {
            // recompute volume / closure with the alternative cap
            double vol2 = 0.0, abs2 = 0.0;
            Vec3 cl2{0, 0, 0};
            for (std::size_t fi = 0; fi < polys.size(); ++fi) {
              const auto& poly = polys[fi];
              Vec3 S{0, 0, 0}, xc{0, 0, 0};
              for (std::size_t e = 0; e < poly.size(); ++e) {
                S += cross(poly[e], poly[(e + 1) % poly.size()]);
                xc += poly[e];
              }
              S = S * 0.5;
              xc = xc / static_cast<double>(poly.size());
              if (dot(S, axis_out[poly_dir[fi]]) < 0.0) S = -S;
              vol2 += dot(xc, S) / 3.0;
              cl2 += S;
              abs2 += norm(S);
            }
            Vec3 cap_S2{0, 0, 0};
            for (std::size_t t = 0; t < cap2.size(); t += 3)
              cap_S2 += cross(cap2[t + 1] - cap2[t], cap2[t + 2] - cap2[t]) * 0.5;
            const Vec3 outward2 = cap_outward;
            if (dot(cap_S2, outward2) < 0.0) cap_S2 = cap_S2 * (-1.0);
            double cap_vol2 = 0.0;
            for (std::size_t t = 0; t < cap2.size(); t += 3) {
              Vec3 S2 = cross(cap2[t + 1] - cap2[t], cap2[t + 2] - cap2[t]) * 0.5;
              if (dot(cap_S2, outward2) < 0.0) S2 = S2 * (-1.0);
              const Vec3 xc2 = (cap2[t] + cap2[t + 1] + cap2[t + 2]) / 3.0;
              cap_vol2 += dot(xc2, S2) / 3.0;
            }
            const double closed2 = (abs2 + norm(cap_S2) > 0.0) ? norm(cl2 + cap_S2) / (abs2 + norm(cap_S2)) : 1.0;
            if (closed2 < closed) {
              cap_tris.swap(cap2);
              // redo the full accumulation from scratch
              closure = cl2 + cap_S2;
              abs_area = abs2 + norm(cap_S2);
              vol = vol2 + cap_vol2;
              closed = closed2;
            }
          }
        }
        if (!std::isfinite(vol) || vol < opts.min_volume_fraction * cell_ref || !(closed < 1e-6)) {
          ++st.n_dropped_slivers;
          if (std::getenv("DIFVM_CUTCELL_DEBUG2") && st.n_dropped_slivers <= 2) {
            std::fprintf(stderr, "PHI8:");
            for (int dk = 0; dk <= 1; ++dk)
              for (int dj = 0; dj <= 1; ++dj)
                for (int di = 0; di <= 1; ++di)
                  std::fprintf(stderr, " %+.4g", phi[nid(i + di, j + dj, k + dk)]);
            std::fprintf(stderr, "\n");
            std::fprintf(stderr, "CELL (%d,%d,%d) ==\n", i, j, k);
            for (std::size_t q3 = 0; q3 < polys.size(); ++q3) {
              Vec3 S{0, 0, 0};
              for (std::size_t e = 0; e < polys[q3].size(); ++e) S += cross(polys[q3][e], polys[q3][(e + 1) % polys[q3].size()]);
              S = S * 0.5;
              std::fprintf(stderr, "  face dir=%d np=%zu S=(%+.3e %+.3e %+.3e) n=(%+.2f %+.2f %+.2f)\n",
                           poly_dir[q3], polys[q3].size(), S.x, S.y, S.z, polys[q3][0].x, polys[q3][0].y, polys[q3][0].z);
            }
            std::fprintf(stderr, "  cap tris=%zu\n", cap_faces.size());
            for (std::size_t t3 = 0; t3 + 2 < cap_tris.size(); t3 += 3)
              std::fprintf(stderr, "    tri (%+.3f %+.3f %+.3f) (%+.3f %+.3f %+.3f) (%+.3f %+.3f %+.3f)\n",
                           cap_tris[t3].x, cap_tris[t3].y, cap_tris[t3].z,
                           cap_tris[t3+1].x, cap_tris[t3+1].y, cap_tris[t3+1].z,
                           cap_tris[t3+2].x, cap_tris[t3+2].y, cap_tris[t3+2].z);
          }
          if (std::getenv("DIFVM_CUTCELL_DEBUG") && st.n_dropped_slivers <= 5)
            std::fprintf(stderr, "DROP ijk=(%d,%d,%d) vol=%.6e (ref %.3e) closed=%.3e nf=%zu cap=%zu chords=%zu\n",
                         i, j, k, vol, cell_ref, closed, cell_faces.size(), cap_faces.size(), chords.size());
          continue;
        }
        if (is_cut) ++st.n_cut_cells;
        st.fluid_volume += vol;

        const idx cid = static_cast<idx>(cell_count++);
        for (std::size_t fi = 0; fi < cell_faces.size(); ++fi) {
          const int dir = cell_face_dir[fi];
          const bool on_boundary = (dir == 0 && i == 0) || (dir == 1 && i == nx - 1) ||
                                   (dir == 2 && j == 0) || (dir == 3 && j == ny - 1) ||
                                   (dir == 4 && k == 0) || (dir == 5 && k == nz - 1);
          (void)on_boundary;
          raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
          for (idx id : cell_faces[fi]) raw.face_verts.push_back(id);
          raw.owner.push_back(cid);
        }
        for (const auto& fv : cap_faces) {
          raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
          for (idx id : fv) raw.face_verts.push_back(id);
          raw.owner.push_back(cid);
        }
      }

  raw.n_cells = cell_count;
  raw.face_offsets.resize(raw.owner.size());
  raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
  raw.neighbour.clear();

  st.time_ms = timer.seconds() * 1e3;
  if (stats) *stats = st;
  return raw;
}

}  // namespace difvm
