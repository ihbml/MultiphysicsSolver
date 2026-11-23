// bvh_common.hpp — boundary surface mesh, BVH, cone-ray clearance helpers.
#pragma once
#include "geometry.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <array>

// ---------------------------------------------------------------------------
// Boundary surface mesh
// ---------------------------------------------------------------------------
struct BoundaryMesh {
    struct Tri { int a, b, c; int patch; int quad; };
    std::vector<Vec3> verts;
    std::vector<Tri> tris;
    std::vector<int> patchOfTri;
    std::vector<int> quadOfTri;
    std::vector<std::vector<int>> vertTris;   // vertex -> incident triangle ids
    std::vector<std::vector<int>> vertAdj;    // vertex -> graph neighbors

    void build(const CaseGeom& g) {
        std::map<std::string, int> dedup;
        auto vid = [&](const Vec3& p) {
            auto key = std::to_string((long long)std::llround(p.x * 1e6)) + "|" +
                       std::to_string((long long)std::llround(p.y * 1e6)) + "|" +
                       std::to_string((long long)std::llround(p.z * 1e6));
            auto it = dedup.find(key);
            if (it != dedup.end()) return it->second;
            int id = (int)verts.size();
            dedup[key] = id;
            verts.push_back(p);
            vertTris.push_back({});
            vertAdj.push_back({});
            return id;
        };
        for (size_t pi = 0; pi < g.patches.size(); ++pi) {
            const Patch& p = g.patches[pi];
            double du = p.lu / p.nu, dv = p.lv / p.nv;
            std::vector<std::vector<int>> qv(p.nv + 1, std::vector<int>(p.nu + 1, -1));
            for (int j = 0; j <= p.nv; ++j)
                for (int i = 0; i <= p.nu; ++i)
                    qv[j][i] = vid(p.at(i * du, j * dv));
            for (int j = 0; j < p.nv; ++j) {
                for (int i = 0; i < p.nu; ++i) {
                    int a = qv[j][i], b = qv[j][i + 1], c = qv[j + 1][i + 1], d = qv[j + 1][i];
                    int quadId = (int)(quadOfTri.size() / 2);
                    tris.push_back({a, b, c, (int)pi, quadId});
                    patchOfTri.push_back((int)pi); quadOfTri.push_back(quadId);
                    tris.push_back({a, c, d, (int)pi, quadId});
                    patchOfTri.push_back((int)pi); quadOfTri.push_back(quadId);
                }
            }
        }
        for (int t = 0; t < (int)tris.size(); ++t) {
            vertTris[tris[t].a].push_back(t);
            vertTris[tris[t].b].push_back(t);
            vertTris[tris[t].c].push_back(t);
        }
        std::set<std::pair<int, int>> edgeSet;
        auto edge = [&](int x, int y) {
            if (x == y) return;
            if (x > y) std::swap(x, y);
            edgeSet.insert({x, y});
        };
        for (const auto& t : tris) { edge(t.a, t.b); edge(t.b, t.c); edge(t.c, t.a); }
        std::vector<std::set<int>> adj(verts.size());
        for (auto& e : edgeSet) { adj[e.first].insert(e.second); adj[e.second].insert(e.first); }
        for (size_t i = 0; i < verts.size(); ++i) vertAdj[i] = std::vector<int>(adj[i].begin(), adj[i].end());
    }
};

// ---------------------------------------------------------------------------
// BVH
// ---------------------------------------------------------------------------
static bool rayTri(const Vec3& p, const Vec3& d, const Vec3& v0, const Vec3& v1,
                   const Vec3& v2, double& tOut) {
    Vec3 e1 = v1 - v0, e2 = v2 - v0;
    Vec3 pv = d.cross(e2);
    double det = e1.dot(pv);
    if (std::abs(det) < 1e-14) return false;
    double inv = 1.0 / det;
    Vec3 tv = p - v0;
    double u = tv.dot(pv) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return false;
    Vec3 q = tv.cross(e1);
    double v = d.dot(q) * inv;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) return false;
    double t = e2.dot(q) * inv;
    if (t <= 1e-12) return false;
    tOut = t;
    return true;
}

struct BVH {
    struct Tri { Vec3 pts[3]; int triId; };
    struct Node { int lo = 0, hi = 0, left = -1, right = -1; Vec3 mn = {0, 0, 0}, mx = {0, 0, 0}; };
    std::vector<Tri> tris;
    std::vector<Node> nodes;

    void buildFrom(const std::vector<std::array<Vec3, 3>>& in) {
        tris.clear(); nodes.clear();
        for (size_t i = 0; i < in.size(); ++i) {
            Tri t{{in[i][0], in[i][1], in[i][2]}, (int)i};
            tris.push_back(t);
        }
        if (tris.empty()) return;
        buildNode(0, (int)tris.size());
    }
    int buildNode(int lo, int hi) {
        int nodeId = (int)nodes.size();
        Vec3 mn{1e300, 1e300, 1e300}, mx{-1e300, -1e300, -1e300};
        for (int i = lo; i < hi; ++i)
            for (int k = 0; k < 3; ++k) {
                mn.x = std::min(mn.x, tris[i].pts[k].x); mn.y = std::min(mn.y, tris[i].pts[k].y); mn.z = std::min(mn.z, tris[i].pts[k].z);
                mx.x = std::max(mx.x, tris[i].pts[k].x); mx.y = std::max(mx.y, tris[i].pts[k].y); mx.z = std::max(mx.z, tris[i].pts[k].z);
            }
        nodes.push_back(Node{lo, hi, -1, -1, mn, mx});
        int count = hi - lo;
        if (count <= 3) return nodeId;
        double ex = mx.x - mn.x, ey = mx.y - mn.y, ez = mx.z - mn.z;
        int axis = 0; double best = ex;
        if (ey > best) { axis = 1; best = ey; }
        if (ez > best) { axis = 2; best = ez; }
        if (best < 1e-12) return nodeId;
        auto mid = [&](const Tri& t) {
            double c0 = (t.pts[0].x + t.pts[1].x + t.pts[2].x) / 3.0;
            double c1 = (t.pts[0].y + t.pts[1].y + t.pts[2].y) / 3.0;
            double c2 = (t.pts[0].z + t.pts[1].z + t.pts[2].z) / 3.0;
            return axis == 0 ? c0 : axis == 1 ? c1 : c2;
        };
        std::vector<Tri> seg(tris.begin() + lo, tris.begin() + hi);
        std::sort(seg.begin(), seg.end(), [&](const Tri& a, const Tri& b) { return mid(a) < mid(b); });
        std::copy(seg.begin(), seg.end(), tris.begin() + lo);
        int nth = lo + count / 2;
        nodes[nodeId].left = buildNode(lo, nth);
        nodes[nodeId].right = buildNode(nth, hi);
        return nodeId;
    }
    struct Hit { double t = 1e300; int triId = -1; };
    Hit rayHit(const Vec3& p, const Vec3& d, double tMin) const {
        Hit r;
        if (nodes.empty()) return r;
        rayNode(p, d, tMin, 0, r);
        return r;
    }
    void rayNode(const Vec3& p, const Vec3& d, double tMin, int nodeIndex, Hit& r) const {
        if (nodeIndex < 0 || nodeIndex >= (int)nodes.size()) return;
        const Node& nd = nodes[nodeIndex];
        double t1 = -1e300, t2 = 1e300;
        for (int axis = 0; axis < 3; ++axis) {
            double pd = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
            double dd = axis == 0 ? d.x : axis == 1 ? d.y : d.z;
            double mn = axis == 0 ? nd.mn.x : axis == 1 ? nd.mn.y : nd.mn.z;
            double mx = axis == 0 ? nd.mx.x : axis == 1 ? nd.mx.y : nd.mx.z;
            if (std::abs(dd) < 1e-12) {
                if (pd < mn || pd > mx) return;
            } else {
                double a = (mn - pd) / dd, b = (mx - pd) / dd;
                if (a > b) std::swap(a, b);
                t1 = std::max(t1, a); t2 = std::min(t2, b);
                if (t1 > t2) return;
            }
        }
        if (t1 > r.t) return;
        if (nd.left < 0) {
            for (int i = nd.lo; i < nd.hi; ++i) {
                double t;
                if (rayTri(p, d, tris[i].pts[0], tris[i].pts[1], tris[i].pts[2], t)) {
                    if (t > tMin && t < r.t) { r.t = t; r.triId = tris[i].triId; }
                }
            }
            return;
        }
        Hit left = r; rayNode(p, d, tMin, nd.left, left); if (left.t < r.t) r = left;
        Hit right = r; rayNode(p, d, tMin, nd.right, right); if (right.t < r.t) r = right;
    }
};
