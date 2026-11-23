// quality.h — Element Normalized Scaled Jacobian (J_ENS), volumes, discrete Gauss closure.
// Conventions (normalized so the ideal element maps to +1, range [-1,1]):
//   Hex8   : J_S,opt = 1
//   Prism6 : J_S,opt = sqrt(3)/2
//   Tet4   : J_S,opt = sqrt(2)/2 = 1/sqrt(2)   (reported as "2/sqrt(2)" multiplier)
//   Pyr5   : J_S,opt = sqrt(2)/2, plus the fictitious basis-hexahedron check over the
//            4-node quad base (folded / non-convex bases are flagged J_ENS <= 0).
#pragma once
#include "core.h"

// corner -> ordered neighbour triples giving positive det on the ideal element
static const int HEX_CORNER[8][3] = {
    {1, 3, 4}, {2, 0, 5}, {3, 1, 6}, {0, 2, 7}, {7, 5, 0}, {4, 6, 1}, {5, 7, 2}, {6, 4, 3}};
static const int PRISM_CORNER[6][3] = {
    {1, 2, 3}, {2, 0, 4}, {0, 1, 5}, {4, 0, 5}, {5, 1, 3}, {3, 2, 4}};
static const int TET_CORNER[4][3] = {{1, 2, 3}, {2, 0, 3}, {0, 1, 3}, {1, 0, 2}};
static const int PYR_CORNER[5][3] = {{1, 3, 4}, {2, 0, 4}, {3, 1, 4}, {0, 2, 4}, {1, 2, 0}};
// note: PYR apex corner uses 3 base edges (coplanar -> det 0 on a planar base); the
// fictitious-hex check below supplies the apex-side/fold test instead.

inline double cornerScaledJac(const std::vector<Vec3> &P, const std::vector<int> &n,
                              int c, const int nbr[3]) {
  const Vec3 &p = P[n[c]];
  Vec3 e0 = P[n[nbr[0]]] - p, e1 = P[n[nbr[1]]] - p, e2 = P[n[nbr[2]]] - p;
  double d = det3(e0, e1, e2);
  double s = e0.norm() * e1.norm() * e2.norm();
  return s > 0 ? d / s : 0.0;
}

// J_ENS in [-1,1] for a cell with canonical node ordering.
// s_opt: hex 1, prism sqrt(3)/2, tet sqrt(2)/2, pyr sqrt(2)/2.
inline double jensCell(int type, const std::vector<Vec3> &P, const std::vector<int> &n) {
  double smin = 1e300;
  if (type == 0) {
    for (int c = 0; c < 8; c++) smin = std::min(smin, cornerScaledJac(P, n, c, HEX_CORNER[c]));
    return smin; // opt = 1
  }
  if (type == 1) {
    for (int c = 0; c < 6; c++) smin = std::min(smin, cornerScaledJac(P, n, c, PRISM_CORNER[c]));
    return smin / (std::sqrt(3.0) / 2.0);
  }
  if (type == 2) {
    for (int c = 0; c < 4; c++) smin = std::min(smin, cornerScaledJac(P, n, c, TET_CORNER[c]));
    return smin / (std::sqrt(2.0) / 2.0);
  }
  // Pyr5: the 4 base corners carry the scaled Jacobian; the apex corner of the
  // fictitious basis hexahedron [b0,b1,b2,b3,a,a,a,a] is degenerate (coplanar edges),
  // so the apex-side / fold test is done explicitly on the quad base: both base-split
  // tets must be positive AND the base quad must be convex (all consecutive-edge
  // cross products same sense) — otherwise the base is folded / non-convex.
  for (int c = 0; c < 4; c++) smin = std::min(smin, cornerScaledJac(P, n, c, PYR_CORNER[c]));
  double j = smin / (std::sqrt(2.0) / 2.0);
  const Vec3 &b0 = P[n[0]], &b1 = P[n[1]], &b2 = P[n[2]], &b3 = P[n[3]], &a = P[n[4]];
  double v1 = det3(b1 - b0, b2 - b0, a - b0); // tet (b0,b1,b2,a)
  double v2 = det3(b2 - b0, b3 - b0, a - b0); // tet (b0,b2,b3,a)
  Vec3 nb = (b1 - b0).cross(b3 - b0);        // base normal (toward apex side if >0)
  Vec3 c1 = (b1 - b0).cross(b2 - b1), c2 = (b2 - b1).cross(b3 - b2),
       c3 = (b3 - b2).cross(b0 - b3), c4 = (b0 - b3).cross(b1 - b0);
  bool convex = (c1.dot(nb) >= 0 && c2.dot(nb) >= 0 && c3.dot(nb) >= 0 && c4.dot(nb) >= 0) ||
                (c1.dot(nb) <= 0 && c2.dot(nb) <= 0 && c3.dot(nb) <= 0 && c4.dot(nb) <= 0);
  bool folded = (v1 <= 0) || (v2 <= 0) || !convex; // folded / non-convex base or bad apex
  if (folded) j = std::min(j, -std::abs(j) - 1.0);  // hard flag: J_ENS <= 0
  return j;
}

// Signed volume via divergence theorem over oriented boundary triangles (long double).
// Node ordering must be the canonical one; sign returned tells orientation.
inline long double signedVolume(const std::vector<Vec3> &P, const std::vector<int> &n, int type) {
  long double V = 0;
  for (auto &f : cellFaces(type))
    for (size_t i = 1; i + 1 < f.size(); i++) {
      const Vec3 &p0 = P[n[f[0]]], &p1 = P[n[f[i]]], &p2 = P[n[f[i + 1]]];
      V += (long double)p0.x * ((long double)p1.y * p2.z - (long double)p1.z * p2.y)
         + (long double)p0.y * ((long double)p1.z * p2.x - (long double)p1.x * p2.z)
         + (long double)p0.z * ((long double)p1.x * p2.y - (long double)p1.y * p2.x);
    }
  return V / 6.0L;
}

// Force canonical orientation (positive volume) by swapping node pairs if needed.
inline void orientCell(Mesh &m, Mesh::Cell &c) {
  if (signedVolume(m.V, c.n, c.type) < 0) {
    if (c.type == 0) { std::swap(c.n[1], c.n[3]); std::swap(c.n[5], c.n[7]); }
    else if (c.type == 1) { std::swap(c.n[1], c.n[2]); std::swap(c.n[4], c.n[5]); }
    else if (c.type == 2) { std::swap(c.n[1], c.n[2]); }
    else if (c.type == 3) { std::swap(c.n[1], c.n[3]); }
  }
}

// Outward-oriented boundary triangles (global vertex ids) of a cell.
inline std::vector<std::array<int, 3>> cellTris(const std::vector<Vec3> &P, const std::vector<int> &n,
                                                 int type, const Vec3 &centroid) {
  std::vector<std::array<int, 3>> out;
  for (auto &f : cellFaces(type)) {
    for (size_t i = 1; i + 1 < f.size(); i++) {
      int a = n[f[0]], b = n[f[i]], c = n[f[i + 1]];
      Vec3 fc = (P[a] + P[b] + P[c]) / 3.0;
      Vec3 sn = (P[b] - P[a]).cross(P[c] - P[a]);
      if (sn.dot(fc - centroid) < 0) std::swap(b, c); // make outward
      out.push_back({a, b, c});
    }
  }
  return out;
}

inline Vec3 cellCentroid(const std::vector<Vec3> &P, const std::vector<int> &n) {
  Vec3 c;
  for (int id : n) c += P[id];
  return c / (double)n.size();
}

// Fill vol (positive), jens, gauss = ||sum_f S_f|| / V^(2/3)  (machine-precision Gauss closure)
inline void evaluateCell(Mesh &m, Mesh::Cell &c) {
  orientCell(m, c);
  c.vol = (double)std::abs(signedVolume(m.V, c.n, c.type));
  c.jens = jensCell(c.type, m.V, c.n);
  // canonical outward orientation (orientCell above guarantees positive volume)
  long double sx = 0, sy = 0, sz = 0;
  for (auto &f : cellFaces(c.type))
    for (size_t i = 1; i + 1 < f.size(); i++) {
      const Vec3 &a = m.V[c.n[f[0]]], &b = m.V[c.n[f[i]]], &cc = m.V[c.n[f[i + 1]]];
      Vec3 s = (b - a).cross(cc - a) * 0.5L;
      sx += s.x; sy += s.y; sz += s.z;
    }
  long double g = std::sqrt(sx * sx + sy * sy + sz * sz);
  double v23 = std::pow(c.vol, 2.0 / 3.0);
  c.gauss = v23 > 0 ? (double)(g / (long double)v23) : 0.0;
}
