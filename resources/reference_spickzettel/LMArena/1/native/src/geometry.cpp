// Geometry kernel: signed distance fields (CSG composable), the mesh size
// field, and boundary contour extraction via marching squares + loop chaining.
#include <cmath>
#include <unordered_map>

#include "core.hpp"

namespace mesher {

namespace {

Real sdSegment(const V2& p, const V2& a, const V2& b) {
  const V2 pa = p - a, ba = b - a;
  const Real h = std::clamp(dot(pa, ba) / std::max(dot(ba, ba), 1e-30), 0.0, 1.0);
  return norm(pa - ba * h);
}

Real sdBox(const V2& p, const V2& lo, const V2& hi) {
  const V2 c = (lo + hi) * 0.5, b = (hi - lo) * 0.5;
  const V2 d(std::fabs(p.x - c.x) - b.x, std::fabs(p.y - c.y) - b.y);
  const V2 dm(std::max(d.x, 0.0), std::max(d.y, 0.0));
  return norm(dm) + std::min(std::max(d.x, d.y), 0.0);
}

// Signed distance of a simple polygon (positive outside). Works for concave
// polygons; the sign comes from the winding-consistent edge test.
Real sdPolygon(const V2& p, const std::vector<V2>& poly) {
  Real d = kInf;
  int s = 1;
  const size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const V2& a = poly[j];
    const V2& b = poly[i];
    const V2 e = b - a;
    const V2 w = p - a;
    const V2 bq(0, 0);
    (void)bq;
    // inside/outside accumulate (even-odd via edge crossing)
    if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-300) + a.x))
      s = -s;
    const Real h = std::clamp(dot(w, e) / std::max(dot(e, e), 1e-30), 0.0, 1.0);
    d = std::min(d, norm(w - e * h));
  }
  return static_cast<Real>(s) * d;
}

Real sdEllipse(const V2& p, const V2& c, const V2& r) {
  V2 q(std::fabs(p.x - c.x), std::fabs(p.y - c.y));
  const Real k0 = norm(V2(q.x / r.x, q.y / r.y));
  const Real k1 = norm(V2(q.x / (r.x * r.x), q.y / (r.y * r.y)));
  if (k1 < 1e-30) return -std::min(r.x, r.y);
  return k0 * (k0 - 1.0) / k1;
}

// Radial star (n tips). Approximate (radial) distance field: adequate for
// contour extraction and normal projection since we normalise the gradient.
Real sdStar(const V2& p, const V2& c, Real ro, Real ri, int n) {
  const V2 q = p - c;
  const Real r = norm(q);
  Real th = std::atan2(q.y, q.x);
  const Real seg = 2 * kPi / std::max(1, n);
  th = std::fmod(th, seg);
  if (th < 0) th += seg;
  const Real u = std::fabs(th - seg * 0.5) / (seg * 0.5);  // 0 at valley, 1 at tip
  const Real rb = ri + (ro - ri) * u;
  return r - rb;
}

std::vector<V2> naca4(const std::string& code, Real chord, const V2& origin, Real angleDeg,
                      int n, bool closedTE) {
  const Real m = (code.size() > 0 ? (code[0] - '0') : 0) / 100.0;
  const Real pp = (code.size() > 1 ? (code[1] - '0') : 0) / 10.0;
  const Real t = (code.size() > 3 ? std::stod(code.substr(2, 2)) : 12.0) / 100.0;
  std::vector<V2> up, lo;
  for (int i = 0; i <= n; ++i) {
    // cosine spacing, leading edge clustered
    const Real beta = kPi * (1.0 - static_cast<Real>(i) / n);
    const Real xc = 0.5 * (1.0 - std::cos(beta));
    const Real yt = 5.0 * t * (0.2969 * std::sqrt(xc) - 0.1260 * xc - 0.3516 * xc * xc +
                               0.2843 * xc * xc * xc - (closedTE ? 0.1036 : 0.1015) * xc * xc * xc * xc);
    Real yc = 0, dyc = 0;
    if (pp > 1e-9) {
      if (xc < pp) {
        yc = m / (pp * pp) * (2 * pp * xc - xc * xc);
        dyc = 2 * m / (pp * pp) * (pp - xc);
      } else {
        yc = m / ((1 - pp) * (1 - pp)) * ((1 - 2 * pp) + 2 * pp * xc - xc * xc);
        dyc = 2 * m / ((1 - pp) * (1 - pp)) * (pp - xc);
      }
    }
    const Real th = std::atan(dyc);
    up.push_back(V2(xc - yt * std::sin(th), yc + yt * std::cos(th)));
    lo.push_back(V2(xc + yt * std::sin(th), yc - yt * std::cos(th)));
  }
  std::vector<V2> poly;
  for (size_t i = 0; i < up.size(); ++i) poly.push_back(up[i]);
  for (size_t i = lo.size(); i-- > 1;) poly.push_back(lo[i]);
  const Real ca = std::cos(angleDeg * kPi / 180.0), sa = std::sin(angleDeg * kPi / 180.0);
  for (auto& q : poly) {
    const V2 s(q.x * chord, q.y * chord);
    q = V2(origin.x + s.x * ca - s.y * sa, origin.y + s.x * sa + s.y * ca);
  }
  return poly;
}

Real smoothMin(Real a, Real b, Real k) {
  if (k <= 1e-12) return std::min(a, b);
  const Real h = std::clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return b * (1 - h) + a * h - k * h * (1 - h);
}
Real smoothMax(Real a, Real b, Real k) { return -smoothMin(-a, -b, k); }

V2 applyXf(const V2& p, const V2& t, Real ca, Real sa, const V2& sc) {
  return V2(t.x + p.x * ca - p.y * sa, t.y + p.x * sa + p.y * ca);
}
V2 invXf(const V2& p, const V2& t, Real ca, Real sa, const V2& sc) {
  const V2 q(p.x - t.x, p.y - t.y);
  const V2 r(q.x * ca + q.y * sa, -q.x * sa + q.y * ca);
  return V2(r.x / std::max(sc.x, 1e-12), r.y / std::max(sc.y, 1e-12));
}

V2 vecOf(const mj::Value& v, const V2& def) {
  if (!v.isArr() || v.size() < 2) return def;
  return V2(v.vecElem(0, def.x), v.vecElem(1, def.y));
}

struct Node {
  using Fn = std::function<Real(const V2&)>;
  Fn fn;
  AABB bounds;
};

Node build(const mj::Value& n) {
  Node out;
  const std::string kind = n.at("kind").asString("box");
  if (kind == "box" || kind == "rect") {
    const V2 lo = vecOf(n.at("min"), V2(0, 0));
    const V2 hi = vecOf(n.at("max"), V2(1, 1));
    out.bounds.lo = lo;
    out.bounds.hi = hi;
    out.fn = [lo, hi](const V2& p) { return sdBox(p, lo, hi); };
  } else if (kind == "circle" || kind == "disc") {
    const V2 c = vecOf(n.at("center"), V2(0, 0));
    const Real r = n.at("radius").asDouble(0.5);
    out.bounds.lo = c - V2(r, r);
    out.bounds.hi = c + V2(r, r);
    out.fn = [c, r](const V2& p) { return norm(p - c) - r; };
  } else if (kind == "ellipse") {
    const V2 c = vecOf(n.at("center"), V2(0, 0));
    const V2 r = vecOf(n.at("radii"), V2(0.5, 0.25));
    out.bounds.lo = c - r;
    out.bounds.hi = c + r;
    out.fn = [c, r](const V2& p) { return sdEllipse(p, c, r); };
  } else if (kind == "annulus" || kind == "ring") {
    const V2 c = vecOf(n.at("center"), V2(0, 0));
    const Real r = n.at("radius").asDouble(0.5);
    const Real th = n.at("thickness").asDouble(0.1);
    out.bounds.lo = c - V2(r + th, r + th);
    out.bounds.hi = c + V2(r + th, r + th);
    out.fn = [c, r, th](const V2& p) { return std::fabs(norm(p - c) - r) - 0.5 * th; };
  } else if (kind == "star") {
    const V2 c = vecOf(n.at("center"), V2(0, 0));
    const Real ro = n.at("rOuter").asDouble(0.5);
    const Real ri = n.at("rInner").asDouble(0.25);
    const int np = n.at("points").asInt(5);
    out.bounds.lo = c - V2(ro, ro);
    out.bounds.hi = c + V2(ro, ro);
    out.fn = [c, ro, ri, np](const V2& p) { return sdStar(p, c, ro, ri, np); };
  } else if (kind == "polygon") {
    std::vector<V2> poly;
    if (const auto* a = n.at("points").asArray())
      for (const auto& q : *a) poly.push_back(vecOf(q, V2(0, 0)));
    if (poly.size() < 3) poly = {V2(0, 0), V2(1, 0), V2(1, 1), V2(0, 1)};
    // enforce CCW so the sign convention matches "negative inside"
    Real sa = 0;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) sa += cross(poly[j], poly[i]);
    if (sa < 0) std::reverse(poly.begin(), poly.end());
    AABB bb;
    for (auto& q : poly) bb.expand(q);
    out.bounds = bb;
    out.fn = [poly](const V2& p) { return sdPolygon(p, poly); };
  } else if (kind == "airfoil") {
    const std::string code = n.at("naca").asString("2412");
    const Real chord = n.at("chord").asDouble(1.0);
    const V2 o = vecOf(n.at("origin"), V2(0, 0));
    const Real ang = n.at("angle").asDouble(0.0);
    const int ns = std::max(24, n.at("samples").asInt(120));
    const bool cte = n.at("closedTE").asBool(true);
    std::vector<V2> poly = naca4(code, chord, o, ang, ns, cte);
    AABB bb;
    for (auto& q : poly) bb.expand(q);
    out.bounds = bb;
    out.fn = [poly](const V2& p) { return sdPolygon(p, poly); };
  } else if (kind == "halfplane") {
    const V2 a = vecOf(n.at("point"), V2(0, 0));
    const V2 nn = normalized(vecOf(n.at("normal"), V2(0, 1)));
    out.bounds = AABB{V2(-1e30, -1e30), V2(1e30, 1e30)};
    // negative on the side the normal points away from
    out.fn = [a, nn](const V2& p) { return dot(p - a, nn); };
  } else if (kind == "union" || kind == "intersection" || kind == "difference") {
    Node a = build(n.at("a"));
    Node b = build(n.at("b"));
    const Real k = n.at("smooth").asDouble(0.0);
    AABB bb;
    if (kind == "intersection") {
      bb.lo = V2(std::max(a.bounds.lo.x, b.bounds.lo.x), std::max(a.bounds.lo.y, b.bounds.lo.y));
      bb.hi = V2(std::min(a.bounds.hi.x, b.bounds.hi.x), std::min(a.bounds.hi.y, b.bounds.hi.y));
      if (bb.hi.x < bb.lo.x || bb.hi.y < bb.lo.y) bb = AABB{};
    } else if (kind == "difference") {
      bb = a.bounds;
    } else {
      bb.lo = V2(std::min(a.bounds.lo.x, b.bounds.lo.x), std::min(a.bounds.lo.y, b.bounds.lo.y));
      bb.hi = V2(std::max(a.bounds.hi.x, b.bounds.hi.x), std::max(a.bounds.hi.y, b.bounds.hi.y));
    }
    out.bounds = bb;
    out.fn = [a, b, k, kind](const V2& p) {
      const Real da = a.fn(p), db = b.fn(p);
      if (kind == "union") return smoothMin(da, db, k);
      if (kind == "intersection") return smoothMax(da, db, k);
      return smoothMax(da, -db, k);
    };
  } else if (kind == "transform") {
    Node c = build(n.at("child"));
    const V2 t = vecOf(n.at("translate"), V2(0, 0));
    const Real ang = n.at("rotate").asDouble(0.0) * kPi / 180.0;
    const V2 sc = vecOf(n.at("scale"), V2(1, 1));
    const Real ca = std::cos(ang), sa = std::sin(ang);
    AABB bb;
    const V2 corners[4] = {c.bounds.lo, V2(c.bounds.hi.x, c.bounds.lo.y), c.bounds.hi,
                           V2(c.bounds.lo.x, c.bounds.hi.y)};
    for (auto& q : corners) bb.expand(applyXf(V2(q.x * sc.x, q.y * sc.y), t, ca, sa, sc));
    out.bounds = bb;
    out.fn = [c, t, ca, sa, sc](const V2& p) {
      const V2 q = invXf(p, t, ca, sa, sc);
      Real d = c.fn(q);
      const Real s = 0.5 * (sc.x + sc.y);
      return d * s;
    };
  } else if (kind == "array" || kind == "repeat") {
    Node c = build(n.at("child"));
    const V2 sp = vecOf(n.at("spacing"), V2(1, 1));
    const int nx = std::max(1, n.at("count").at(0).asInt(3));
    const int ny = std::max(1, n.at("count").at(1).asInt(1));
    const V2 o = vecOf(n.at("origin"), c.bounds.lo);
    AABB bb;
    bb.expand(V2(o.x + c.bounds.lo.x, o.y + c.bounds.lo.y));
    bb.expand(V2(o.x + c.bounds.hi.x + sp.x * (nx - 1), o.y + c.bounds.hi.y + sp.y * (ny - 1)));
    out.bounds = bb;
    out.fn = [c, sp, nx, ny, o](const V2& p) {
      Real d = kInf;
      for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) d = std::min(d, c.fn(p - V2(o.x + sp.x * i, o.y + sp.y * j)));
      return d;
    };
  } else {
    throw std::runtime_error("unknown geometry kind: " + kind);
  }
  return out;
}

}  // namespace

Sdf buildSdf(const mj::Value& node) {
  Node root = build(node);
  Sdf s;
  s.f = [root](const V2& p) { return root.fn(p); };
  return s;
}

AABB sdfBounds(const mj::Value& node) {
  Node root = build(node);
  return root.bounds;
}

// ------------------------------------------------------------- size field ----
namespace {
Real targetDistance(const RefineTarget& t, const V2& p) {
  switch (t.kind) {
    case 1:
      return std::fabs(norm(p - t.c) - t.r);
    case 2:
      return sdBox(p, t.lo, t.hi);
    default:
      return norm(p - t.c);
  }
}
inline Real smoothstep01(Real x) {
  x = std::clamp(x, 0.0, 1.0);
  return x * x * (3 - 2 * x);
}
}  // namespace

Real sizeField(const FvmSpec& f, const V2& p, const Sdf& s) {
  Real h = f.base;
  for (const auto& t : f.targets) {
    const Real d = std::fabs(targetDistance(t, p));
    const Real w = std::max(t.width, 1e-9);
    const Real hn = t.hNear > 0 ? t.hNear : f.base * 0.25;
    const Real blend = smoothstep01(d / w);
    h = std::min(h, hn + (f.base - hn) * blend);
  }
  // Boundary-layer gradation: make the size just outside the last BL row
  // comparable with the row height so the transition stays smooth.
  if (f.blCount > 0 && f.blFirst > 0) {
    const Real wall = s(p);
    if (wall < 0) {
      Real height = f.blFirst, total = 0;
      for (int i = 0; i < f.blCount; ++i) {
        total += height;
        height *= f.blGrowth;
      }
      const Real band = total * 4.0;
      const Real d = -wall;
      if (d < band) {
        // local BL row height at this wall distance
        Real hh = f.blFirst, acc = 0, local = f.blFirst;
        for (int i = 0; i < f.blCount; ++i) {
          acc += hh;
          local = hh;
          if (acc > d) break;
          hh *= f.blGrowth;
        }
        const Real target = std::max(local * f.blGrowth, f.base * 0.15);
        const Real blend = smoothstep01(d / band);
        h = std::min(h, target + (h - target) * blend);
      }
    }
  }
  const Real hmin = f.minSize > 0 ? f.minSize : f.base * 0.05;
  return std::max(h, hmin);
}

Real sizeFieldMin(const FvmSpec& f, const Sdf& s) {
  (void)s;
  Real h = f.base;
  for (const auto& t : f.targets) h = std::min(h, t.hNear > 0 ? t.hNear : f.base * 0.25);
  const Real hmin = f.minSize > 0 ? f.minSize : f.base * 0.05;
  return std::max(h, hmin);
}

// ------------------------------------------------- contour extraction --------
namespace {

bool pointInLoop(const V2& p, const std::vector<V2>& loop) {
  bool inside = false;
  for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
    const V2& a = loop[i];
    const V2& b = loop[j];
    if (((a.y > p.y) != (b.y > p.y)) &&
        (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-300) + a.x))
      inside = !inside;
  }
  return inside;
}

Real loopArea(const std::vector<V2>& l) {
  Real a = 0;
  for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++) a += cross(l[j], l[i]);
  return 0.5 * a;
}

}  // namespace

ContourLoops extractContours(const Sdf& s, const AABB& searchBox, Real sampleSpacing) {
  ContourLoops res;
  const Real sp = std::max(sampleSpacing, 1e-9);
  const V2 margin(sp * 3.0, sp * 3.0);
  const V2 lo = searchBox.lo - margin, hi = searchBox.hi + margin;
  int nx = static_cast<int>(std::ceil((hi.x - lo.x) / sp)) + 1;
  int ny = static_cast<int>(std::ceil((hi.y - lo.y) / sp)) + 1;
  const long long maxCells = 12000000;
  while (static_cast<long long>(nx) * ny > maxCells) {
    nx = static_cast<int>(nx * 0.8);
    ny = static_cast<int>(ny * 0.8);
    if (nx < 8 || ny < 8) break;
  }
  nx = std::max(nx, 4);
  ny = std::max(ny, 4);
  const Real dx = (hi.x - lo.x) / (nx - 1), dy = (hi.y - lo.y) / (ny - 1);
  std::vector<Real> field(static_cast<size_t>(nx) * ny);
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i)
      field[static_cast<size_t>(j) * nx + i] = s(V2(lo.x + i * dx, lo.y + j * dy));

  // marching squares -> segments keyed by the grid edge they cross
  struct Seg {
    V2 a, b;
    long long ka, kb;
  };
  std::vector<Seg> segs;
  segs.reserve(static_cast<size_t>(nx) * 4);
  auto hkey = [&](int i, int j) { return 2LL * (static_cast<long long>(j) * nx + i); };
  auto vkey = [&](int i, int j) { return 2LL * (static_cast<long long>(j) * nx + i) + 1; };
  auto interp = [&](Real f0, Real f1, const V2& p0, const V2& p1) {
    const Real t = std::clamp(f0 / (f0 - f1 + 1e-300), 0.0, 1.0);
    return p0 + (p1 - p0) * t;
  };
  for (int j = 0; j + 1 < ny; ++j) {
    for (int i = 0; i + 1 < nx; ++i) {
      const Real f00 = field[static_cast<size_t>(j) * nx + i];
      const Real f10 = field[static_cast<size_t>(j) * nx + i + 1];
      const Real f01 = field[static_cast<size_t>(j + 1) * nx + i];
      const Real f11 = field[static_cast<size_t>(j + 1) * nx + i + 1];
      int idx = 0;
      if (f00 < 0) idx |= 1;
      if (f10 < 0) idx |= 2;
      if (f11 < 0) idx |= 4;
      if (f01 < 0) idx |= 8;
      if (idx == 0 || idx == 15) continue;
      const V2 p00(lo.x + i * dx, lo.y + j * dy);
      const V2 p10(lo.x + (i + 1) * dx, lo.y + j * dy);
      const V2 p11(lo.x + (i + 1) * dx, lo.y + (j + 1) * dy);
      const V2 p01(lo.x + i * dx, lo.y + (j + 1) * dy);
      // crossings: 0 bottom, 1 right, 2 top, 3 left
      V2 cp[4];
      long long ck[4];
      bool has[4] = {false, false, false, false};
      if ((f00 < 0) != (f10 < 0)) {
        cp[0] = interp(f00, f10, p00, p10);
        ck[0] = hkey(i, j);
        has[0] = true;
      }
      if ((f10 < 0) != (f11 < 0)) {
        cp[1] = interp(f10, f11, p10, p11);
        ck[1] = vkey(i + 1, j);
        has[1] = true;
      }
      if ((f01 < 0) != (f11 < 0)) {
        cp[2] = interp(f01, f11, p01, p11);
        ck[2] = hkey(i, j + 1);
        has[2] = true;
      }
      if ((f00 < 0) != (f01 < 0)) {
        cp[3] = interp(f00, f01, p00, p01);
        ck[3] = vkey(i, j);
        has[3] = true;
      }
      std::vector<int> list;
      for (int k = 0; k < 4; ++k)
        if (has[k]) list.push_back(k);
      if (list.size() < 2) continue;
      if (list.size() == 4) {
        // saddle: disambiguate with the cell centre value
        const Real fc = 0.25 * (f00 + f10 + f01 + f11);
        const bool centerInside = fc < 0;
        const bool c0 = (idx & 1) != 0;  // corner 00 inside
        if (centerInside == c0) {
          segs.push_back({cp[0], cp[3], ck[0], ck[3]});
          segs.push_back({cp[1], cp[2], ck[1], ck[2]});
        } else {
          segs.push_back({cp[0], cp[1], ck[0], ck[1]});
          segs.push_back({cp[2], cp[3], ck[2], ck[3]});
        }
      } else {
        segs.push_back({cp[list[0]], cp[list[1]], ck[list[0]], ck[list[1]]});
      }
    }
  }

  // chain segments into closed loops
  std::unordered_multimap<long long, int> keymap;
  keymap.reserve(segs.size() * 2);
  for (size_t i = 0; i < segs.size(); ++i) {
    keymap.emplace(segs[i].ka, static_cast<int>(i));
    keymap.emplace(segs[i].kb, static_cast<int>(i));
  }
  std::vector<char> used(segs.size(), 0);
  for (size_t start = 0; start < segs.size(); ++start) {
    if (used[start]) continue;
    used[start] = 1;
    std::vector<V2> loop;
    loop.push_back(segs[start].a);
    loop.push_back(segs[start].b);
    long long cur = segs[start].kb;
    const long long firstKey = segs[start].ka;
    for (size_t guard = 0; guard < segs.size() + 1; ++guard) {
      if (cur == firstKey) break;
      int nxt = -1;
      auto range = keymap.equal_range(cur);
      for (auto it = range.first; it != range.second; ++it) {
        if (!used[it->second]) {
          nxt = it->second;
          break;
        }
      }
      if (nxt < 0) break;
      used[nxt] = 1;
      const Seg& sg = segs[nxt];
      if (sg.ka == cur) {
        loop.push_back(sg.b);
        cur = sg.kb;
      } else {
        loop.push_back(sg.a);
        cur = sg.ka;
      }
    }
    if (loop.size() >= 4) {
      if (norm(loop.front() - loop.back()) < 1e-12) loop.pop_back();
      if (loop.size() >= 3) res.loops.push_back(std::move(loop));
    }
  }

  // nesting depth -> outer vs hole, then canonical orientation
  const size_t L = res.loops.size();
  std::vector<int> depth(L, 0);
  for (size_t i = 0; i < L; ++i) {
    for (size_t j = 0; j < L; ++j) {
      if (i == j) continue;
      if (pointInLoop(res.loops[i][0], res.loops[j])) depth[i]++;
    }
  }
  res.isHole.resize(L, 0);
  Real sp2 = 0;
  for (size_t i = 0; i < L; ++i) {
    const bool hole = (depth[i] % 2) == 1;
    res.isHole[i] = hole ? 1 : 0;
    const Real a = loopArea(res.loops[i]);
    const bool ccw = a > 0;
    if (hole == ccw) std::reverse(res.loops[i].begin(), res.loops[i].end());
    for (size_t k = 0, m = res.loops[i].size(); k < m; ++k)
      sp2 += norm(res.loops[i][k] - res.loops[i][(k + 1) % m]);
    res.area += std::fabs(a) * (hole ? -1.0 : 1.0);
  }
  res.perimeter = sp2;
  for (auto& l : res.loops)
    for (auto& q : l) res.box.expand(q);
  return res;
}

bool pointInDomainLoops(const V2& p, const ContourLoops& c) {
  int crossings = 0;
  for (const auto& l : c.loops)
    if (pointInLoop(p, l)) crossings++;
  return (crossings % 2) == 1;
}

}  // namespace mesher
