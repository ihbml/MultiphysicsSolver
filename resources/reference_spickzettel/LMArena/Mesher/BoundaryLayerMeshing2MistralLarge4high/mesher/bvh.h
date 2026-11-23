// bvh.h — median-split BVH over surface triangles + Moeller–Trumbore ray casting.
// Used for narrow-band medial-axis / opposing-wall clearance estimation.
#pragma once
#include "core.h"
#include <unordered_set>

struct BVH {
  struct Tri { Vec3 a, b, c; int id; Vec3 cen; Vec3 n; };
  std::vector<Tri> tris;
  struct Node { Vec3 lo, hi; int left = -1, right = -1; int start = 0, count = 0; };
  std::vector<Node> nodes;
  std::vector<int> order;

  void build(const std::vector<Vec3> &V, const std::vector<std::array<int, 3>> &T) {
    tris.clear(); nodes.clear(); order.resize(T.size());
    for (size_t i = 0; i < T.size(); i++) {
      Tri t; t.a = V[T[i][0]]; t.b = V[T[i][1]]; t.c = V[T[i][2]]; t.id = (int)i;
      t.cen = (t.a + t.b + t.c) / 3.0;
      t.n = (t.b - t.a).cross(t.c - t.a).normalized();
      tris.push_back(t);
      order[i] = (int)i;
    }
    if (!tris.empty()) buildNode(0, (int)tris.size());
  }
  int buildNode(int start, int count) {
    Node nd;
    nd.start = start; nd.count = count;
    nd.lo = nd.hi = tris[order[start]].cen;
    for (int i = start; i < start + count; i++) {
      const Tri &t = tris[order[i]];
      auto upd = [&](const Vec3 &p) {
        nd.lo.x = std::min(nd.lo.x, p.x); nd.lo.y = std::min(nd.lo.y, p.y); nd.lo.z = std::min(nd.lo.z, p.z);
        nd.hi.x = std::max(nd.hi.x, p.x); nd.hi.y = std::max(nd.hi.y, p.y); nd.hi.z = std::max(nd.hi.z, p.z);
      };
      upd(t.a); upd(t.b); upd(t.c);
    }
    int idx = (int)nodes.size();
    nodes.push_back(nd);
    if (count > 4) {
      Vec3 ext = nd.hi - nd.lo;
      int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
      int mid = start + count / 2;
      std::nth_element(order.begin() + start, order.begin() + mid, order.begin() + start + count,
                       [&](int a, int b) {
                         const Vec3 &ca = tris[a].cen, &cb = tris[b].cen;
                         return axis == 0 ? ca.x < cb.x : axis == 1 ? ca.y < cb.y : ca.z < cb.z;
                       });
      nodes[idx].left = buildNode(start, mid - start);
      nodes[idx].right = buildNode(mid, start + count - mid);
    }
    return idx;
  }
  static bool slab(const Vec3 &o, const Vec3 &d, const Vec3 &lo, const Vec3 &hi, double tmin, double &t0, double &t1) {
    t0 = tmin; t1 = 1e300;
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const double l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int i = 0; i < 3; i++) {
      if (std::fabs(dd[i]) < 1e-300) {
        if (oo[i] < l[i] || oo[i] > h[i]) return false;
      } else {
        double a = (l[i] - oo[i]) / dd[i], b = (h[i] - oo[i]) / dd[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) return false;
      }
    }
    return true;
  }
  // returns nearest hit distance (> tmin) or -1
  double ray(const Vec3 &o, const Vec3 &d, double tmin, int *hitId = nullptr,
             const std::unordered_set<int> *ignore = nullptr, double ignoreBelow = 0.0) const {
    if (nodes.empty()) return -1;
    double best = 1e300; int bestId = -1;
    int stack[128]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
      int ni = stack[--sp];
      const Node &nd = nodes[ni];
      double t0, t1;
      if (!slab(o, d, nd.lo, nd.hi, tmin, t0, t1) || t0 > best) continue;
      if (nd.left < 0) {
        for (int i = nd.start; i < nd.start + nd.count; i++) {
          const Tri &t = tris[order[i]];
          Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
          Vec3 p = d.cross(e2);
          double det = e1.dot(p);
          if (std::fabs(det) < 1e-300) continue;
          double inv = 1.0 / det;
          Vec3 tv = o - t.a;
          double u = tv.dot(p) * inv;
          if (u < -1e-12 || u > 1 + 1e-12) continue;
          Vec3 q = tv.cross(e1);
          double v = d.dot(q) * inv;
          if (v < -1e-12 || u + v > 1 + 1e-12) continue;
          double tt = e2.dot(q) * inv;
          if (tt > tmin && tt < best) {
            if (ignore && tt < ignoreBelow && ignore->count(t.id)) continue;
            best = tt; bestId = t.id;
          }
        }
      } else {
        stack[sp++] = nd.left; stack[sp++] = nd.right;
      }
    }
    if (hitId) *hitId = bestId;
    return bestId >= 0 ? best : -1;
  }
};
