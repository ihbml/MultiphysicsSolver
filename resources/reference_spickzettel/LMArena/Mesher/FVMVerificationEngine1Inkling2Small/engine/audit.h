// ============================================================================
//  PolyVV-20 :: Thingi10k-style geometric pathology torture harness
//  Deterministic auditor + FVM face classifier. Every defect is a synthetic,
//  constructed pathology with exact 3D coordinates. Zero mocks: the auditor
//  either finds the defect or reports it honestly.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <map>
#include <set>
#include <algorithm>
#include "mesh.h"

namespace vv {

struct DefectResult {
  std::string id, name, classification;
  double coord[3];
  bool detected = false;
  std::string detector;
  std::string detail;
};

struct AuditSummary {
  std::vector<DefectResult> defects;
  int nanCount = 0;
  int total = 0, detected = 0;
  bool auditCompleted = true;
  int segfaults = 0;   // process survived == 0 by definition here
};

constexpr double PI_CONST = 3.14159265358979323846;

struct PolyCheck {
  bool zeroArea = false, selfIntersect = false, nonPlanar = false, needle = false, nan = false;
  double maxDihedralDeg = 0.0, maxAngleDeg = 0.0, minFanSign = 0.0;
  double minArea = 0.0;
};

// 2D signed fan test helper used for self-intersection double-check
inline bool polygonFanSignChange(const std::vector<Vec3>& p) {
  Vec3 S = newellNormal(p);
  if (S.norm() < 1e-27) S = Vec3{0, 0, 1};  // degenerate Newell: judge winding vs +z
  double neg = 0, pos = 0;
  for (size_t i = 1; i + 1 < p.size(); ++i) {
    Vec3 cr = (p[i] - p[0]).cross(p[i + 1] - p[0]);
    double d = cr.dot(S);
    if (d < 0) neg += 1; else if (d > 0) pos += 1;
  }
  return neg > 0 && pos > 0;
}
// Projected planar segment-intersection test (all non-adjacent edge pairs)
inline bool segmentsIntersect2D(const std::array<double, 2>& p1, const std::array<double, 2>& p2,
                                const std::array<double, 2>& p3, const std::array<double, 2>& p4) {
  auto cross = [](double ax, double ay, double bx, double by) { return ax * by - ay * bx; };
  double d1 = cross(p4[0] - p3[0], p4[1] - p3[1], p1[0] - p3[0], p1[1] - p3[1]);
  double d2 = cross(p4[0] - p3[0], p4[1] - p3[1], p2[0] - p3[0], p2[1] - p3[1]);
  double d3 = cross(p2[0] - p1[0], p2[1] - p1[1], p3[0] - p1[0], p3[1] - p1[1]);
  double d4 = cross(p2[0] - p1[0], p2[1] - p1[1], p4[0] - p1[0], p4[1] - p1[1]);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
inline bool polygonSelfIntersects(const std::vector<Vec3>& p) {
  const size_t n = p.size();
  if (n < 4) return false;
  // airtight for planar polygons: proper segment crossing found in ANY of the 3 projections
  for (int axis = 0; axis < 3; ++axis) {
    auto proj = [&](const Vec3& v) {
      if (axis == 0) return std::array<double, 2>{v.y, v.z};
      if (axis == 1) return std::array<double, 2>{v.x, v.z};
      return std::array<double, 2>{v.x, v.y};
    };
    for (size_t i = 0; i < n; ++i) {
      size_t i2 = (i + 1) % n;
      for (size_t j = i + 1; j < n; ++j) {
        size_t j2 = (j + 1) % n;
        if (i == j || i2 == j || i == j2 || i2 == j2) continue;
        if (segmentsIntersect2D(proj(p[i]), proj(p[i2]), proj(p[j]), proj(p[j2]))) return true;
      }
    }
  }
  return false;
}

// Check a polygon face (planarity, self-intersection, needle angles, zero area, NaN)
inline PolyCheck checkPolygon(const std::vector<Vec3>& p) {
  PolyCheck r;
  for (auto& v : p) if (!v.finite()) r.nan = true;
  if (p.size() < 3) { r.zeroArea = true; return r; }
  Vec3 S = newellNormal(p);
  r.minArea = S.norm();
  if (!std::isfinite(r.minArea)) { r.nan = true; }
  if (r.minArea < 1e-27) r.zeroArea = true;
  // fan triangulation signed areas + triangle dihedral angles
  double maxAbsDist = 0.0;
  {
    Vec3 n = S;
    double nl = n.norm();
    if (nl > 0) n = n / nl;
    Vec3 c; for (auto& v : p) c += v; c = c * (1.0 / p.size());
    for (auto& v : p) maxAbsDist = std::max(maxAbsDist, std::fabs((v - c).dot(n)));
  }
  double fanSigns[3] = {0, 0, 0};
  int signChanges = 0;
  double minSign = 1e30, maxSign = -1e30;
  std::vector<Vec3> triNormals;
  for (size_t i = 1; i + 1 < p.size(); ++i) {
    Vec3 e1 = p[i] - p[0], e2 = p[i + 1] - p[0];
    Vec3 cr = e1.cross(e2);
    double s = cr.norm() * 0.5;
    double signedArea = cr.dot(S) >= 0 ? s : -s;
    if (i == 1) { minSign = signedArea; maxSign = signedArea; }
    else { minSign = std::min(minSign, signedArea); maxSign = std::max(maxSign, signedArea); }
    if (cr.norm() > 0) triNormals.push_back(cr / cr.norm());
  }
  r.minFanSign = minSign;
  if ((minSign < 0.0 && maxSign > 0.0) || polygonSelfIntersects(p)) r.selfIntersect = true;
  (void)fanSigns; (void)signChanges;
  // dihedral fold angle between consecutive fan triangles
  for (size_t i = 0; i + 1 < triNormals.size(); ++i) {
    double c = std::clamp(triNormals[i].dot(triNormals[i + 1]), -1.0, 1.0);
    double ang = std::acos(c) * 180.0 / M_PI;
    r.maxDihedralDeg = std::max(r.maxDihedralDeg, ang);
  }
  if (maxAbsDist > 1e-2) r.nonPlanar = true;  // 1 cm fold threshold on unit domain
  if (r.maxDihedralDeg > 25.0) r.nonPlanar = true;
  // needle: max interior angle
  const size_t n = p.size();
  for (size_t i = 0; i < n; ++i) {
    Vec3 a = p[(i + n - 1) % n] - p[i];
    Vec3 b = p[(i + 1) % n] - p[i];
    double la = a.norm(), lb = b.norm();
    if (la < 1e-30 || lb < 1e-30) { r.zeroArea = true; continue; }
    double c = std::clamp(a.dot(b) / (la * lb), -1.0, 1.0);
    double interior = std::acos(c) * 180.0 / M_PI;  // angle at vertex i between the two emanating edges
    r.maxAngleDeg = std::max(r.maxAngleDeg, interior);
  }
  if (r.maxAngleDeg > 179.0) r.needle = true;
  return r;
}


// Edge incidence audit: edges shared by != 2 faces are non-manifold
struct EdgeAudit {
  int nonManifoldEdges = 0;
  Vec3 worstPoint{0, 0, 0};
};
inline int quantize(double v) { return (int)std::llround(v * 1e10); }
inline EdgeAudit auditEdges(const std::vector<std::vector<Vec3>>& polys) {
  std::map<std::pair<std::array<int, 3>, std::array<int, 3>>, int> incidence;
  std::map<std::pair<std::array<int, 3>, std::array<int, 3>>, Vec3> mid;
  for (const auto& p : polys) {
    size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
      const Vec3& a = p[i];
      const Vec3& b = p[(i + 1) % n];
      std::array<int, 3> ka{quantize(a.x), quantize(a.y), quantize(a.z)};
      std::array<int, 3> kb{quantize(b.x), quantize(b.y), quantize(b.z)};
      if (kb < ka) std::swap(ka, kb);
      incidence[{ka, kb}]++;
      mid[{ka, kb}] = (a + b) * 0.5;
    }
  }
  EdgeAudit out;
  for (auto& [edge, cnt] : incidence) {
    if (cnt > 2) {  // non-manifold = MORE than 2 faces on one edge (T-junction)
      out.nonManifoldEdges++;
      if (out.nonManifoldEdges == 1) out.worstPoint = mid[edge];
    }
  }
  return out;
}
// T-junction: a vertex lying strictly inside (within eps) a foreign edge
inline Vec3 findTJunction(const std::vector<std::vector<Vec3>>& polys, double eps = 1e-9) {
  // collect all edges, all vertices
  std::vector<std::pair<Vec3, Vec3>> edges;
  std::vector<Vec3> verts;
  for (const auto& p : polys) {
    size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
      edges.emplace_back(p[i], p[(i + 1) % n]);
      verts.push_back(p[i]);
    }
  }
  for (const auto& v : verts) {
    for (const auto& [a, b] : edges) {
      Vec3 ab = b - a;
      double len2 = ab.norm2();
      if (len2 < 1e-30) continue;
      double t = (v - a).dot(ab) / len2;
      if (t <= 1e-9 || t >= 1 - 1e-9) continue;  // endpoints are legitimate
      Vec3 proj = a + ab * t;
      if ((v - proj).norm() < eps) return proj;
    }
  }
  return Vec3{-1, -1, -1};
}
// Cell adjacency connected components (disconnected island detection)
inline int countCellComponents(const std::vector<std::vector<int>>& adj) {
  int n = (int)adj.size();
  std::vector<char> seen(n, 0);
  int comps = 0;
  for (int s = 0; s < n; ++s) {
    if (seen[s]) continue;
    ++comps;
    std::vector<int> stack = {s};
    seen[s] = 1;
    while (!stack.empty()) {
      int c = stack.back(); stack.pop_back();
      for (int nb : adj[c]) if (!seen[nb]) { seen[nb] = 1; stack.push_back(nb); }
    }
  }
  return comps;
}

// ---------------- the torture suite ----------------
inline AuditSummary runTortureSuite() {
  AuditSummary rep;
  auto fail = [&rep](const char* id, const char* name, const char* cls, Vec3 c,
                     const std::string& detector, const std::string& detail, bool detected) {
    DefectResult d;
    d.id = id; d.name = name; d.classification = cls;
    d.coord[0] = c.x; d.coord[1] = c.y; d.coord[2] = c.z;
    d.detected = detected; d.detector = detector; d.detail = detail;
    rep.defects.push_back(d);
    rep.total++;
    if (detected) rep.detected++;
  };
  auto fin = [&rep](const Vec3& v) { if (!v.finite()) rep.nanCount++; };

  // T1 — self-intersecting (bowtie) facet: A(0,0,0) B(1,1,0) C(1,0,0) D(0,1,0)
  {
    std::vector<Vec3> p{{0, 0, 0}, {1, 1, 0}, {1, 0, 0}, {0, 1, 0}};
    for (auto& v : p) fin(v);
    PolyCheck r = checkPolygon(p);
    Vec3 crossPoint{0.5, 0.5, 0.0};  // analytic edge/edge intersection A-B x C-D
    bool det = r.selfIntersect || polygonFanSignChange(p);
    fail("T1", "Self-intersecting bowtie facet", "SELF_INTERSECTING_FACET", crossPoint,
         "newell_fan_sign_consistency",
         "fan-triangulated signed areas change sign (mixed +/- winding)", det);
  }
  // T2 — non-manifold edge: three quads share edge (0,0,0)-(0,1,0)
  {
    std::vector<std::vector<Vec3>> polys = {
      {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
      {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
      {{0, 0, 0}, {0, 1, 0}, {-1, 1, 0.5}, {-1, 0, 0.5}}};
    for (auto& p : polys) for (auto& v : p) fin(v);
    EdgeAudit ea = auditEdges(polys);
    Vec3 mid{0, 0.5, 0};
    fail("T2", "Non-manifold T-junction edge (3 faces on shared edge)", "NONMANIFOLD_EDGE", mid,
         "edge_incidence_count > 2",
         "shared edge has incidence 3 (conforming volume meshes require exactly 2)",
         ea.nonManifoldEdges >= 1);
  }
  // T2b — T-junction vertex lying on a foreign edge
  {
    std::vector<std::vector<Vec3>> polys = {
      {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}},
      {{1, 0, 0}, {1, 0, 1.5}, {1.5, 1, 1.5}, {1.5, 1, 0}}};  // vertex (1,0,0) lies on edge (0,0,0)-(2,0,0)
    for (auto& p : polys) for (auto& v : p) fin(v);
    Vec3 tj = findTJunction(polys, 1e-9);
    bool det = tj.x >= 0;
    fail("T2b", "Non-manifold T-junction (vertex on foreign edge interior)", "T_JUNCTION",
         det ? tj : Vec3{1, 0, 0}, "vertex_on_edge_interior", "edge projection parameter 0<t<1 with distance < 1e-9", det);
  }
  // T3 — needle/cap triangle, apex angle 179.999 deg
  {
    // closed form: apex half-angle = 89.9995 deg; tan(half) = 1 / h  =>  h = 1/tan(89.9995 deg)
    double h = 1.0 / std::tan(89.9995 * PI_CONST / 180.0);
    std::vector<Vec3> p{{0, 0, 0}, {2, 0, 0}, {1, h, 0}};
    for (auto& v : p) fin(v);
    PolyCheck r = checkPolygon(p);
    Vec3 apex{1, h, 0};
    fail("T3", "179.999 deg needle / cap triangle", "NEEDLE_FACET", apex,
         "max_interior_angle > 179 deg",
         "apex interior angle 179.999 deg between cap triangle edges", r.needle);
  }
  // T4 — zero-area sliver (collinear vertices)
  {
    std::vector<Vec3> p{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    for (auto& v : p) fin(v);
    PolyCheck r = checkPolygon(p);
    Vec3 mid{1, 0, 0};
    fail("T4", "Zero-area sliver facet (collinear vertices)", "ZERO_AREA_SLIVER", mid,
         "|Newell vector| < 1e-27", "area vector vanishes: face carries no flux", r.zeroArea);
  }
  // T5 — disconnected inner void island (cell adjacency components > 1)
  {
    // 2x2x2 mother block (cells 0..7) + 1-cell island at (5,5,5) sharing no faces
    std::vector<std::vector<int>> adj(9);
    // ring adjacency of the 2x2x2 block
    for (int i = 0; i < 8; ++i)
      for (int j = i + 1; j < 8; ++j) {
        // neighbors if differ by one in exactly one coordinate (3-bit gray check)
        int x = (i & 1) != (j & 1), y = (i & 2) != (j & 2), z = (i & 4) != (j & 4);
        if (x + y + z == 1) { adj[i].push_back(j); adj[j].push_back(i); }
      }
    // island cell 8: no adjacency entries
    int comps = countCellComponents(adj);
    Vec3 island{5.5, 5.5, 5.5};
    fail("T5", "Disconnected inner void island (unreachable cells)", "DISCONNECTED_ISLAND", island,
         "cell_adjacency_connected_components > 1",
         "BFS from cell 0 reaches only 8 of 9 control volumes; island is flux-invisible", comps == 2);
  }
  // T6 — folded polyhedral face (non-planar quad, dihedral fold > 25 deg)
  {
    std::vector<Vec3> p{{0, 0, 0}, {2, 0, 0}, {2, 2, 1.2}, {0, 2, 0}};
    for (auto& v : p) fin(v);
    PolyCheck r = checkPolygon(p);
    Vec3 foldMid{2, 1, 0.6};
    fail("T6", "Folded polyhedral face (dihedral fold 42.6 deg)", "FOLDED_POLYHEDRAL_FACE", foldMid,
         "max_dihedral_angle > 25 deg",
         "non-planar quad: fan triangles make a 42.6 deg dihedral fold", r.nonPlanar);
  }
  // T7 — flipped face normal (winding reversed vs neighbour direction)
  {
    // genuine quad with reversed winding; neighbour direction d has positive z
    std::vector<Vec3> p{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}};  // winding reversed
    for (auto& v : p) fin(v);
    Vec3 S = newellNormal(p);
    Vec3 xP{0.5, 0.5, -0.5}, xN{0.5, 0.5, 0.5};
    Vec3 d = xN - xP;
    double cosT = d.dot(S) / (d.norm() * S.norm());
    FaceClass cls = classifyCosTheta(cosT);
    Vec3 c{0.5, 0.5, 0};
    fail("T7", "Flipped face normal (reversed winding)", "INVERTED_NORMAL", c,
         "cos(theta_f) <= 1e-8  =>  FaceClass::INVALID",
         "winding yields S . d < 0; face classifier rejects it before flux assembly",
         cls == FaceClass::INVALID);
  }
  return rep;
}

}  // namespace vv
