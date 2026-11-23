// octree_hexcore.cpp — Poly-Hexcore / Cut-Cell mesher core (C++20).
//
// Pipeline:
//   1. Exact planar surface build (union-of-boxes domains)
//   2. Angle-weighted pseudormals + constrained Laplacian smoothing (cos 65° visibility)
//   3. Cone-ray medial-axis clearance via BVH ray casting
//   4. Per-vertex bottom-up prism layers with 0.40*d_medial() squeezing and jacobian floor 0.15
//   5. Wedge cells (triangular prisms) to a flat per-connected-component interface plane
//   6. Core: 2:1-balanced-ish Cartesian hexcore, clipped to domain-minus-slabs
//   7. Exact per-cell volume / closure / JENS, mesh face bookkeeping, verification
//   8. TS/JSON emission for the inspector
#include "geometry.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <numeric>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <numeric>
#include <set>


constexpr double ENGINE_PI = 3.14159265358979323846;
constexpr double COS_VIS = 0.42261826174069944;   // cos(65 degrees)

// ---------------- math ----------------

// ---------------- AABB helper ----------------
struct AABB { double x0, y0, z0, x1, y1, z1; };
static inline AABB slabUnion(const AABB& a, const AABB& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::min(a.z0, b.z0),
            std::max(a.x1, b.x1), std::max(a.y1, b.y1), std::max(a.z1, b.z1)};
}

// ---------------- cell structure ----------------
enum class CellKind { Wedge, Hex };

struct RawCell {
    int id = -1;
    CellKind kind = CellKind::Wedge;
    int patch = -1;      // patch id, -1 for core
    int layer = 0;       // layer index, 0 = core
    int surfaceTri = -1; // surface triangle id for wedges
    std::vector<Vec3> verts;                 // polyhedron vertices
    std::vector<std::array<int,3>> tris;     // unoriented triangles
};

static double polyVolume(const RawCell& c) {
    KahanScalar vol{0, 0};
    for (const auto& t : c.tris) {
        const Vec3& a = c.verts[t[0]], & b = c.verts[t[1]], & cc = c.verts[t[2]];
        Vec3 S = (b - a).cross(cc - a) * 0.5;
        Vec3 centroid = (a + b + cc) / 3.0;
        vol.add(centroid.dot(S) / 3.0);
    }
    return std::abs(vol.value());
}

static double polyClosure(const RawCell& c, double vol) {
    KahanVec3 S{};
    for (const auto& t : c.tris) {
        const Vec3& a = c.verts[t[0]], & b = c.verts[t[1]], & cc = c.verts[t[2]];
        S.add((b - a).cross(cc - a) * 0.5);
    }
    double norm = S.value().len();
    double href = std::pow(vol, 2.0 / 3.0);
    if (href <= 0) return 1e99;
    return norm / href;
}

// ---------------- BVH ----------------
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
    struct Node { int lo = 0, hi = 0, left = -1, right = -1; Vec3 mn = {0,0,0}, mx = {0,0,0}; };
    std::vector<Tri> tris; std::vector<Node> nodes;
    void buildFrom(const std::vector<std::array<Vec3,3>>& in) {
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
        Vec3 mn{1e300,1e300,1e300}, mx{-1e300,-1e300,-1e300};
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
        Hit hl = r; rayNode(p, d, tMin, nd.left, hl); r = hl;
        Hit hr = r; rayNode(p, d, tMin, nd.right, hr); r = hr;
    }
};

// ---------------- boundary surface mesh ----------------
struct BoundaryMesh {
    struct Tri { int a, b, c; int patch; int quad; };
    std::vector<Vec3> verts;
    std::vector<Tri> tris;
    std::vector<int> patchOfTri;
    std::vector<int> quadOfTri;
    std::vector<std::vector<int>> vertTris;
    std::vector<std::vector<int>> vertAdj;

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
            vertTris.push_back({}); vertAdj.push_back({});
            return id;
        };
        for (size_t pi = 0; pi < g.patches.size(); ++pi) {
            const Patch& p = g.patches[pi];
            double du = p.lu / p.nu, dv = p.lv / p.nv;
            std::vector<std::vector<int>> qv(p.nv + 1, std::vector<int>(p.nu + 1, -1));
            for (int j = 0; j <= p.nv; ++j)
                for (int i = 0; i <= p.nu; ++i)
                    qv[j][i] = vid(p.at(i * du, j * dv));
            for (int j = 0; j < p.nv; ++j)
                for (int i = 0; i < p.nu; ++i) {
                    int a = qv[j][i], b = qv[j][i + 1], c = qv[j + 1][i + 1], d = qv[j + 1][i];
                    int quadId = (int)(quadOfTri.size() / 2);
                    tris.push_back({a, b, c, (int)pi, quadId});
                    patchOfTri.push_back((int)pi); quadOfTri.push_back(quadId);
                    tris.push_back({a, c, d, (int)pi, quadId});
                    patchOfTri.push_back((int)pi); quadOfTri.push_back(quadId);
                }
        }
        for (int t = 0; t < (int)tris.size(); ++t) {
            vertTris[tris[t].a].push_back(t);
            vertTris[tris[t].b].push_back(t);
            vertTris[tris[t].c].push_back(t);
        }
        std::set<std::pair<int, int>> edgeSet;
        auto e = [&](int x, int y) {
            if (x == y) return;
            if (x > y) std::swap(x, y);
            edgeSet.insert({x, y});
        };
        for (const auto& t : tris) { e(t.a, t.b); e(t.b, t.c); e(t.c, t.a); }
        std::vector<std::set<int>> adj(verts.size());
        for (auto& ed : edgeSet) { adj[ed.first].insert(ed.second); adj[ed.second].insert(ed.first); }
        for (size_t i = 0; i < verts.size(); ++i) vertAdj[i] = std::vector<int>(adj[i].begin(), adj[i].end());
    }
};

// ---------- pseudormals ----------
static Vec3 triNormal(const BoundaryMesh& bm, int t) {
    const auto& tri = bm.tris[t];
    Vec3 e1 = bm.verts[tri.b] - bm.verts[tri.a];
    Vec3 e2 = bm.verts[tri.c] - bm.verts[tri.a];
    return e1.cross(e2).normalized();
}
static double triSubtendedAngle(const BoundaryMesh& bm, int t, int v) {
    const auto& tri = bm.tris[t];
    const Vec3& pv = bm.verts[v];
    Vec3 e1{0,0,0}, e2{0,0,0};
    if (tri.a == v)      { e1 = bm.verts[tri.b] - pv; e2 = bm.verts[tri.c] - pv; }
    else if (tri.b == v) { e1 = bm.verts[tri.a] - pv; e2 = bm.verts[tri.c] - pv; }
    else                 { e1 = bm.verts[tri.a] - pv; e2 = bm.verts[tri.b] - pv; }
    double cosA = e1.dot(e2) / (e1.len() * e2.len() + 1e-300);
    cosA = std::max(-1.0, std::min(1.0, cosA));
    return std::acos(cosA);
}
static Vec3 initialPseudormal(const BoundaryMesh& bm, int v) {
    Vec3 acc{0,0,0};
    for (int t : bm.vertTris[v]) acc = acc + triNormal(bm, t) * triSubtendedAngle(bm, t, v);
    if (acc.len() < 1e-12) return {0,1,0};
    return acc.normalized();
}
static Vec3 constrainedInitial(const BoundaryMesh& bm, int v) {
    Vec3 n = initialPseudormal(bm, v);
    for (int iter = 0; iter < 24; ++iter) {
        Vec3 tg{0,0,0}; int tot = 0;
        for (int t : bm.vertTris[v]) {
            Vec3 nt = triNormal(bm, t);
            if (n.dot(nt) < COS_VIS) { tg = tg + nt; tot++; }
        }
        if (tot == 0) break;
        n = (n + tg * (0.35 / tot)).normalized();
    }
    for (int t : bm.vertTris[v]) {
        Vec3 nt = triNormal(bm, t);
        if (n.dot(nt) < COS_VIS) n = (n + nt * 0.4).normalized();
    }
    return n;
}
static void constrainedSmooth(BoundaryMesh& bm, std::vector<Vec3>& pseudorm, int iters = 24) {
    for (int it = 0; it < iters; ++it) {
        std::vector<Vec3> old = pseudorm;
        for (int v = 0; v < (int)bm.verts.size(); ++v) {
            Vec3 sum{0,0,0}; int cnt = 0;
            for (int u : bm.vertAdj[v]) { sum = sum + pseudorm[u]; cnt++; }
            if (cnt == 0) continue;
            Vec3 cand = (pseudorm[v] * 0.5 + (sum / cnt) * 0.5).normalized();
            bool ok = true;
            for (int t : bm.vertTris[v]) {
                if (cand.dot(triNormal(bm, t)) < COS_VIS) { ok = false; break; }
            }
            if (ok) pseudorm[v] = cand;
        }
    }
    for (int v = 0; v < (int)bm.verts.size(); ++v) {
        Vec3 n = pseudorm[v];
        for (int t : bm.vertTris[v]) {
            Vec3 nt = triNormal(bm, t);
            if (n.dot(nt) < COS_VIS) n = (n + nt * 0.4).normalized();
        }
        pseudorm[v] = n;
    }
}

// ---------- clearance ----------
static std::vector<double> estimateClearance(const BoundaryMesh& bm,
                                             const std::vector<Vec3>& pseudorm,
                                             const BVH& bvh) {
    int nv = (int)bm.verts.size();
    std::vector<double> dm(nv, 1e2);
    const double cosHalf = std::cos(35.0 * ENGINE_PI / 180.0);
    const double sinHalf = std::sin(35.0 * ENGINE_PI / 180.0);
    for (int v = 0; v < nv; ++v) {
        Vec3 n = pseudorm[v];
        Vec3 t1 = (std::abs(n.x) < 0.9) ? Vec3{1,0,0}.cross(n) : Vec3{0,1,0}.cross(n);
        t1 = t1.normalized();
        Vec3 t2 = n.cross(t1).normalized();
        std::vector<Vec3> dirs{n};
        for (int q = 0; q < 4; ++q) {
            double phi = q * ENGINE_PI / 2.0;
            dirs.push_back((n * cosHalf + (t1 * std::cos(phi) + t2 * std::sin(phi)) * sinHalf).normalized());
        }
        std::set<int> incident(bm.vertTris[v].begin(), bm.vertTris[v].end());
        double best = 1e300;
        for (const auto& d : dirs) {
            auto r = bvh.rayHit(bm.verts[v], d, 1e-3);
            if (r.t < 9e292 && !incident.count(r.triId)) {
                if (r.t < best) best = r.t;
            }
        }
        if (best < 9e292) dm[v] = best;
        else dm[v] = 0.5;  // no hit: effectively infinite (hard-capped by caller)
    }
    return dm;
}



// ---------------- BL params ----------------
struct BLParams {
    double dY1 = 0.06;
    double r = 1.35;
    int maxLayers = 8;
    double capFactor = 0.40;   // critical fraction of medial clearance
    double hardLimit = 0.45;   // absolute per-vertex height cap
    double jFloor = 0.15;      // Jacobian determinant floor
    double minSlice = 1e-3;    // minimum useful layer slice for a vertex
};

// ------------------- flat slab (connected component) -------------------
struct ConnComp {
    int id;
    std::set<int> verts;
    std::set<int> patches;
};

static std::vector<ConnComp> connectedComponents(const BoundaryMesh& bm) {
    int nv = (int)bm.verts.size();
    std::vector<int> comp(nv, -1);
    std::vector<ConnComp> out;
    int cid = 0;
    for (int v = 0; v < nv; ++v) {
        if (comp[v] >= 0) continue;
        comp[v] = cid;
        ConnComp cp{cid, {}, {}};
        std::vector<int> frontier{v};
        while (!frontier.empty()) {
            int u = frontier.back(); frontier.pop_back();
            cp.verts.insert(u);
            for (int t : bm.vertTris[u]) cp.patches.insert(bm.patchOfTri[t]);
            for (int w : bm.vertAdj[u])
                if (comp[w] < 0) { comp[w] = cid; frontier.push_back(w); }
        }
        out.push_back(cp);
        cid++;
    }
    return out;
}

// per-vertex label: patch containing the surface triangle (wedge)
static inline int patchOfTri(const BoundaryMesh& bm, int t) { return bm.patchOfTri[t]; }

// ---------------- slab box ----------------
static AABB slabBox(const Patch& p, double depth) {
    Vec3 a0 = p.at(0, 0), a1 = p.at(p.lu, 0), a2 = p.at(0, p.lv), a3 = p.at(p.lu, p.lv);
    Vec3 b0 = a0 + p.normal * depth, b1 = a1 + p.normal * depth, b2 = a2 + p.normal * depth, b3 = a3 + p.normal * depth;
    double mnX = std::min(std::min(std::min(a0.x, a1.x), std::min(a2.x, a3.x)),
                          std::min(std::min(b0.x, b1.x), std::min(b2.x, b3.x)));
    double mxX = std::max(std::max(std::max(a0.x, a1.x), std::max(a2.x, a3.x)),
                          std::max(std::max(b0.x, b1.x), std::max(b2.x, b3.x)));
    double mnY = std::min(std::min(std::min(a0.y, a1.y), std::min(a2.y, a3.y)),
                          std::min(std::min(b0.y, b1.y), std::min(b2.y, b3.y)));
    double mxY = std::max(std::max(std::max(a0.y, a1.y), std::max(a2.y, a3.y)),
                          std::max(std::max(b0.y, b1.y), std::max(b2.y, b3.y)));
    double mnZ = std::min(std::min(std::min(a0.z, a1.z), std::min(a2.z, a3.z)),
                          std::min(std::min(b0.z, b1.z), std::min(b2.z, b3.z)));
    double mxZ = std::max(std::max(std::max(a0.z, a1.z), std::max(a2.z, a3.z)),
                          std::max(std::max(b0.z, b1.z), std::max(b2.z, b3.z)));
    return {mnX - 1e-12, mnY - 1e-12, mnZ - 1e-12, mxX + 1e-12, mxY + 1e-12, mxZ + 1e-12};
}

// ------------------- core cells (octree grid) -------------------
// Rectangular prism cell: 8 vertexes, 6 quad faces (each as 2 triangles)
static RawCell makeRectPris(const AABB& b, int id, int layer) {
    RawCell c;
    c.id = id; c.kind = CellKind::Hex; c.layer = 0; c.patch = -1;
    Vec3 verts[8] = {
        {b.x0, b.y0, b.z0}, {b.x1, b.y0, b.z0}, {b.x1, b.y1, b.z0}, {b.x0, b.y1, b.z0},
        {b.x0, b.y0, b.z1}, {b.x1, b.y0, b.z1}, {b.x1, b.y1, b.z1}, {b.x0, b.y1, b.z1}
    };
    c.verts = {verts[0], verts[1], verts[2], verts[3], verts[4], verts[5], verts[6], verts[7]};
    auto quad = [&](int a, int b, int d, int e) {
        c.tris.push_back({a, b, d});
        c.tris.push_back({a, d, e});
    };
    // bottom (z0): 0,1,2,3
    quad(0, 1, 2, 3);
    // top (z1): 4,5,6,7
    quad(4, 5, 6, 7);
    // left x0: 0,3,7,4
    quad(0, 3, 7, 4);
    // right x1: 1,2,6,5
    quad(1, 2, 6, 5);
    // back y0: 0,1,5,4
    quad(0, 1, 5, 4);
    // front y1: 3,2,6,7
    quad(3, 2, 6, 7);
    return c;
}

// ---------- wedge cell geometry ----------
static RawCell makeWedge(int id, int layer, int patch, int surfaceTri,
                         const Vec3& b0, const Vec3& b1, const Vec3& b2,
                         const Vec3& t0, const Vec3& t1, const Vec3& t2) {
    RawCell c;
    c.id = id; c.kind = CellKind::Wedge; c.layer = layer; c.patch = patch; c.surfaceTri = surfaceTri;
    c.verts = {b0, b1, b2, t0, t1, t2};
    // faces:
    // bottom (0,1,2), top (3,4,5)
    c.tris.push_back({0, 1, 2});
    c.tris.push_back({3, 4, 5});
    // sides: (b0,b1,t1,t0), (b1,b2,t2,t1), (b2,b0,t0,t2)
    c.tris.push_back({0, 1, 4}); c.tris.push_back({0, 4, 3});
    c.tris.push_back({1, 2, 5}); c.tris.push_back({1, 5, 4});
    c.tris.push_back({2, 0, 3}); c.tris.push_back({2, 3, 5});
    return c;
}

// ---------------- box subtraction (axis-aligned) ----------------
static std::vector<AABB> subtractBox(const AABB& B, const AABB& S) {
    std::vector<AABB> out;
    double eps = 1e-9;
    if (S.x1 <= B.x0 + eps || S.x0 >= B.x1 - eps || S.y1 <= B.y0 + eps || S.y0 >= B.y1 - eps ||
        S.z1 <= B.z0 + eps || S.z0 >= B.z1 - eps) {
        out.push_back(B);
        return out;
    }
    auto exclInt = [&](double a, double b, double s0, double s1) -> std::vector<std::pair<double,double>> {
        std::vector<std::pair<double,double>> r;
        if (s1 <= a + eps || s0 >= b - eps) { r.emplace_back(a, b); return r; }
        if (s0 > a + eps) r.emplace_back(a, std::min(s0, b));
        if (s1 < b - eps) r.emplace_back(std::max(s1, a), b);
        return r;
    };
    // x-outer slabs (full y/z)
    for (auto& iv : exclInt(B.x0, B.x1, S.x0, S.x1)) {
        if (iv.second - iv.first > eps)
            out.push_back({iv.first, B.y0, B.z0, iv.second, B.y1, B.z1});
    }
    // x-middle: stubboxes that avoid slab x-range but not y/z
    double xo = std::max(B.x0, S.x0), xi = std::min(B.x1, S.x1);
    if (xi - xo > eps) {
        // y-split
        for (auto& iv : exclInt(B.y0, B.y1, S.y0, S.y1)) {
            if (iv.second - iv.first > eps)
                out.push_back({xo, iv.first, B.z0, xi, iv.second, B.z1});
        }
        // y-middle: z-split
        double yo = std::max(B.y0, S.y0), yi = std::min(B.y1, S.y1);
        if (yi - yo > eps) {
            for (auto& iv : exclInt(B.z0, B.z1, S.z0, S.z1)) {
                if (iv.second - iv.first > eps)
                    out.push_back({xo, yo, iv.first, xi, yi, iv.second});
            }
        }
    }
    return out;
}

static bool boxContainsBox(const AABB& outer, const AABB& inner, double eps = 1e-9) {
    return inner.x0 >= outer.x0 - eps && inner.x1 <= outer.x1 + eps &&
           inner.y0 >= outer.y0 - eps && inner.y1 <= outer.y1 + eps &&
           inner.z0 >= outer.z0 - eps && inner.z1 <= outer.z1 + eps;
}

// ---------------- verification helpers ----------------
// ---------------- JSON/TS emitter ----------------


// ---------------- JENS ----------------
struct JeansStat { double raw = 0, jOpt = 1.0, value = 0; };
static JeansStat jeansOf(const RawCell& c, double vol, double hs) {
    double Vref; double jOpt;
    if (c.kind == CellKind::Hex) { Vref = hs * hs * hs; jOpt = 1.0; }
    else { Vref = 0.5 * hs * hs * hs; jOpt = 3.0 / (2.0 * std::sqrt(2.0)); }
    double raw = vol / Vref;
    return {raw, jOpt, raw / jOpt};
}

// ---------------- VERIFICATION STRUCT ----------------
struct Verification {
    int nWedges = 0, nHex = 0;
    double totalVolume = 0, domainVolume = 0, volumeErrorRel = 0;
    double minVol = 1e300, minJeans = 1e300, maxClosure = 0;
    int faultFaces = 0, boundaryFaces = 0, interiorFaces = 0;
    int skippedQuads = 0;
    double totalWedgeVolume = 0, totalHexVolume = 0;
    int nSurfaceTri = 0, nSurfaceVert = 0;
    double minClearance = 1e300;
};

// ---------------- orient cell (outward faces) ----------------
static void orientCell(RawCell& c) {
    if (c.verts.empty()) return;
    Vec3 cellCentroid{0,0,0};
    for (const auto& v : c.verts) cellCentroid = cellCentroid + v;
    cellCentroid = cellCentroid / (int)c.verts.size();
    for (auto& t : c.tris) {
        const Vec3& a = c.verts[t[0]], & b = c.verts[t[1]], & cc = c.verts[t[2]];
        Vec3 S = (b - a).cross(cc - a);
        Vec3 fc = (a + b + cc) / 3.0;
        Vec3 dir = fc - cellCentroid;
        if (S.dot(dir) < 0) std::swap(t[1], t[2]);
    }
}

static std::string faceKey(const Vec3& a, const Vec3& b, const Vec3& c) {
    std::vector<Vec3> pts{a, b, c};
    std::sort(pts.begin(), pts.end(), [](const Vec3& x, const Vec3& y) {
        if (std::abs(x.x - y.x) > 1e-6) return x.x < y.x;
        if (std::abs(x.y - y.y) > 1e-6) return x.y < y.y;
        return x.z < y.z;
    });
    auto fmt = [](double q) { return std::to_string((long long)std::llround(q * 1e7)); };
    std::ostringstream os;
    os << fmt(pts[0].x) << "," << fmt(pts[0].y) << "," << fmt(pts[0].z) << "|"
       << fmt(pts[1].x) << "," << fmt(pts[1].y) << "," << fmt(pts[1].z) << "|"
       << fmt(pts[2].x) << "," << fmt(pts[2].y) << "," << fmt(pts[2].z);
    return os.str();
}

// ---------------- connected components + per-vertex caps ----------------
struct PrepOut {
    std::vector<ConnComp> comps;
    std::vector<int> vertComp;
    std::vector<int> triPatch;
    std::vector<double> cap;
    std::vector<double> slabDepth;
};

static PrepOut prepareComponents(const CaseGeom& g, const BoundaryMesh& bm,
                                 const std::vector<double>& dMedial, const BLParams& p) {
    PrepOut out;
    auto comps = connectedComponents(bm);
    out.comps = comps;
    int nv = (int)bm.verts.size();
    out.vertComp.assign(nv, -1);
    for (const auto& cp : comps)
        for (int v : cp.verts) out.vertComp[v] = cp.id;
    out.cap.assign(nv, 0.0);
    out.slabDepth.assign(nv, 0.5);
    for (int v = 0; v < nv; ++v) {
        double c = std::min(p.capFactor * dMedial[v], p.hardLimit);
        if (c <= 0.0) c = 1e-3;
        out.cap[v] = c;
    }
    // per connected component slab depth = min over the vertices of cap
    std::vector<double> compSlab(comps.size(), 1e300);
    for (int v = 0; v < nv; ++v) {
        int c = out.vertComp[v];
        if (c >= 0 && out.cap[v] < compSlab[c]) compSlab[c] = out.cap[v];
    }
    for (int v = 0; v < nv; ++v) {
        int c = out.vertComp[v];
        out.slabDepth[v] = (c >= 0 && compSlab[c] < 9e299) ? compSlab[c] : 0.3;
    }
    return out;
}

static std::vector<std::vector<double>> growHeights(const std::vector<double>& cap,
                                                    const std::vector<double>& slab,
                                                    int maxLayers, double dY1, double r,
                                                    double minSlice,
                                                    std::vector<int>& layerCount) {
    int nv = (int)cap.size();
    std::vector<std::vector<double>> H(nv, std::vector<double>(maxLayers + 1, 0.0));
    layerCount.assign(nv, 0);
    for (int k = 1; k <= maxLayers; ++k) {
        for (int v = 0; v < nv; ++v) {
            double prev = H[v][k - 1];
            double target = std::min(cap[v], slab[v]);
            if (prev >= target - 1e-9) { H[v][k] = prev; continue; }
            double grow = dY1 * std::pow(r, k - 1);
            double d = std::min(grow, target - prev);
            if (d < minSlice) { H[v][k] = prev; continue; }
            H[v][k] = prev + d;
            layerCount[v] = k;
        }
    }
    return H;
}

// ---------------- wedge building ----------------
static void buildWedges(const CaseGeom& g, const BoundaryMesh& bm,
                        const std::vector<Vec3>& pseudorm,
                        const std::vector<double>& slabDepth,
                        const std::vector<std::vector<double>>& H,
                        const std::vector<int>& vertexLayerCount,
                        int maxLayers,
                        std::vector<RawCell>& outCells,
                        int& skippedQuads,
                        const std::vector<AABB>& patchSlabBoxes) {
    int nTri = (int)bm.tris.size();
    std::set<int> skipQuadsSet;
    std::set<int> processed;
    // 1) skip rule
    for (int t = 0; t < nTri; ++t) {
        int patch = bm.patchOfTri[t];
        int quad = bm.quadOfTri[t];
        if (processed.count(quad)) continue;
        processed.insert(quad);
        // gather quad vertices (all triangles sharing quad)
        std::set<int> qvert;
        for (int tt = 0; tt < nTri; ++tt) {
            if (bm.quadOfTri[tt] != quad) continue;
            qvert.insert(bm.tris[tt].a); qvert.insert(bm.tris[tt].b); qvert.insert(bm.tris[tt].c);
        }
        Vec3 mn{1e300,1e300,1e300}, mx{-1e300,-1e300,-1e300};
        for (int v : qvert) {
            const Vec3& p = bm.verts[v];
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        double slab = 1e300;
        for (int v : qvert) if (slabDepth[v] < slab) slab = slabDepth[v];
        Vec3 n = g.patches[patch].normal;
        Vec3 e0 = mn + n * slab, e1 = mx + n * slab;
        AABB quadSlabBox{std::min(mn.x, e0.x), std::min(mn.y, e0.y), std::min(mn.z, e0.z),
                         std::max(mx.x, e1.x), std::max(mx.y, e1.y), std::max(mx.z, e1.z)};
        bool skip = false;
        for (int pi = 0; pi < (int)g.patches.size(); ++pi) {
            if (pi == patch) continue;
            if (boxContainsBox(patchSlabBoxes[pi], quadSlabBox, 1e-9)) { skip = true; break; }
        }
        if (skip) {
            skipQuadsSet.insert(quad);
            skippedQuads++;
        }
    }
    // 2) build wedges
    for (int t = 0; t < nTri; ++t) {
        int patch = bm.patchOfTri[t];
        int quad = bm.quadOfTri[t];
        if (skipQuadsSet.count(quad)) continue;
        const auto& tri = bm.tris[t];
        int vs[3] = {tri.a, tri.b, tri.c};
        auto pos = [&](int v, int k) -> Vec3 {
            if (k <= 0) return bm.verts[v];
            int K = (int)H[v].size() - 1;
            if (k > K) k = K;
            return bm.verts[v] + pseudorm[v] * H[v][k];
        };
        int Kmax = std::min(maxLayers, (int)H[tri.a].size() - 1);
        for (int k = 1; k <= Kmax; ++k) {
            bool any = false;
            for (int i = 0; i < 3; ++i) {
                double dv = (pos(vs[i], k) - pos(vs[i], k - 1)).len();
                if (dv > 1e-9) { any = true; break; }
            }
            if (!any) continue;
            auto w = makeWedge((int)outCells.size(), k, patch, t,
                               pos(tri.a, k - 1), pos(tri.b, k - 1), pos(tri.c, k - 1),
                               pos(tri.a, k), pos(tri.b, k), pos(tri.c, k));
            outCells.push_back(w);
        }
        // equalization layer -> flat interface
        auto bp = [&](int v) -> Vec3 {
            int kk = vertexLayerCount[v];
            if (kk <= 0) return bm.verts[v];
            if (kk > (int)H[v].size() - 1) kk = (int)H[v].size() - 1;
            return bm.verts[v] + pseudorm[v] * H[v][kk];
        };
        auto tp = [&](int v) -> Vec3 {
            return bm.verts[v] + g.patches[patch].normal * slabDepth[v];
        };
        bool any = false;
        for (int i = 0; i < 3; ++i) {
            int v = vs[i];
            if ((tp(v) - bp(v)).len() > 1e-9) { any = true; break; }
        }
        if (any) {
            auto w = makeWedge((int)outCells.size(), 0, patch, t,
                               bp(tri.a), bp(tri.b), bp(tri.c),
                               tp(tri.a), tp(tri.b), tp(tri.c));
            outCells.push_back(w);
        }
    }
}

// ---------------- core build ----------------
static std::vector<RawCell> buildCore(const CaseGeom& g,
                                      const std::vector<AABB>& slabBoxes,
                                      double hs,
                                      int& nCellsOut) {
    std::vector<AABB> coreRegion;
    for (const auto& b : g.boxes) coreRegion.push_back({b.x0, b.y0, b.z0, b.x1, b.y1, b.z1});
    for (const auto& s : slabBoxes) {
        std::vector<AABB> next;
        for (const auto& b : coreRegion) {
            auto sub = subtractBox(b, s);
            for (const auto& x : sub) next.push_back(x);
        }
        coreRegion = next;
    }
    std::vector<AABB> kept;
    for (const auto& b : coreRegion) {
        if ((b.x1 - b.x0) > 1e-9 && (b.y1 - b.y0) > 1e-9 && (b.z1 - b.z0) > 1e-9)
            kept.push_back(b);
    }
    coreRegion = kept;
    std::vector<RawCell> out;
    int cellId = 0;
    int nx = std::max(1, (int)std::lround((g.bboxMax.x - g.bboxMin.x) / hs));
    int ny = std::max(1, (int)std::lround((g.bboxMax.y - g.bboxMin.y) / hs));
    int nz = std::max(1, (int)std::lround((g.bboxMax.z - g.bboxMin.z) / hs));
    double dx = (g.bboxMax.x - g.bboxMin.x) / nx;
    double dy = (g.bboxMax.y - g.bboxMin.y) / ny;
    double dz = (g.bboxMax.z - g.bboxMin.z) / nz;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k) {
                double x0 = g.bboxMin.x + i * dx, x1 = x0 + dx;
                double y0 = g.bboxMin.y + j * dy, y1 = y0 + dy;
                double z0 = g.bboxMin.z + k * dz, z1 = z0 + dz;
                AABB cell{x0, y0, z0, x1, y1, z1};
                for (const auto& r : coreRegion) {
                    double ix0 = std::max(cell.x0, r.x0), iy0 = std::max(cell.y0, r.y0), iz0 = std::max(cell.z0, r.z0);
                    double ix1 = std::min(cell.x1, r.x1), iy1 = std::min(cell.y1, r.y1), iz1 = std::min(cell.z1, r.z1);
                    if (ix1 - ix0 > 1e-9 && iy1 - iy0 > 1e-9 && iz1 - iz0 > 1e-9) {
                        out.push_back(makeRectPris({ix0, iy0, iz0, ix1, iy1, iz1}, cellId++, 0));
                    }
                }
            }
    nCellsOut = nx * ny * nz;
    return out;
}

// ---------------- main driver ----------------

// ---------------- main driver ----------------
static int runCase(const CaseGeom& g, double hs, const BLParams& p,
                   std::vector<RawCell>& cells,
                   std::vector<double>& cellVolumes,
                   std::vector<double>& cellClosures,
                   std::vector<double>& cellJeansV,
                   std::vector<double>& cellJeansRaw,
                   std::vector<double>& cellJeansOpt,
                   Verification& verification,
                   BoundaryMesh& bmOut,
                   std::vector<Vec3>& pseudormOut,
                   std::vector<double>& dMedialOut,
                   std::vector<double>& slabDepthOut) {
    // step 1: surface build
    bmOut.build(g);
    int nv = (int)bmOut.verts.size();

    // step 2: pseudormals + smoothing
    std::vector<Vec3> pseudorm(nv);
    for (int v = 0; v < nv; ++v) pseudorm[v] = constrainedInitial(bmOut, v);
    constrainedSmooth(bmOut, pseudorm, 24);
    pseudormOut = pseudorm;

    // step 3: clearance (cone rays vs boundary BVH)
    std::vector<std::array<Vec3,3>> triPts(bmOut.tris.size());
    for (size_t t = 0; t < bmOut.tris.size(); ++t) {
        triPts[t][0] = bmOut.verts[bmOut.tris[t].a];
        triPts[t][1] = bmOut.verts[bmOut.tris[t].b];
        triPts[t][2] = bmOut.verts[bmOut.tris[t].c];
    }
    BVH bvh;
    bvh.buildFrom(triPts);
    auto dMedial = estimateClearance(bmOut, pseudorm, bvh);
    dMedialOut = dMedial;

    // DEBUG pseudormals
    {
        for (int v : {0, 1, 50, 150, 250, 330}) {
            if (v < nv) {
                std::cout << "  v" << v << " pos=(" << bmOut.verts[v].x << "," << bmOut.verts[v].y << "," << bmOut.verts[v].z << ")";
                std::cout << " n=(" << pseudorm[v].x << "," << pseudorm[v].y << "," << pseudorm[v].z << ")";
                std::cout << " dMed=" << dMedial[v] << "\n";
            }
        }
    }
    // step 4: per-vertex caps + connected-component slab depth
    auto prep = prepareComponents(g, bmOut, dMedial, p);
    slabDepthOut.assign(prep.slabDepth.begin(), prep.slabDepth.end());

    // step 5: growth + layer positions
    std::vector<int> vertexLayerCount(nv, 0);
    auto H = growHeights(prep.cap, prep.slabDepth, p.maxLayers, p.dY1, p.r, p.minSlice, vertexLayerCount);

    // DEBUG
    {
        std::cout << "DEBUG verts=" << nv << " dMedial min=" << *std::min_element(dMedial.begin(), dMedial.end())
                  << " med=" << *std::max_element(dMedial.begin(), dMedial.end()) << "\n";
        double minSlab = 1e300, maxSlab = 0;
        for (double v : slabDepthOut) { minSlab = std::min(minSlab, v); maxSlab = std::max(maxSlab, v); }
        std::cout << "DEBUG slab min=" << minSlab << " max=" << maxSlab << "\n";
        double hSum = 0; for (int v = 0; v < nv; ++v) hSum += H[v][std::min((int)H[v].size() - 1, p.maxLayers)];
        std::cout << "DEBUG sum H top per vertex =" << hSum << "\n";
    }
    // step 6: per-patch slab box for the skip rule
    std::vector<AABB> patchSlabBoxes(g.patches.size());
    for (size_t i = 0; i < g.patches.size(); ++i) {
        double d = 1e300;
        for (int v = 0; v < nv; ++v) {
            for (int t : bmOut.vertTris[v]) {
                if (bmOut.patchOfTri[t] == (int)i) { if (slabDepthOut[v] < d) d = slabDepthOut[v]; break; }
            }
        }
        if (d > 1e299) d = 0.3;
        patchSlabBoxes[i] = slabBox(g.patches[i], d);
    }

    // step 7: wedge cells
    int skippedQuads = 0;
    std::vector<RawCell> wedges;
    buildWedges(g, bmOut, pseudorm, slabDepthOut, H, vertexLayerCount, p.maxLayers,
                wedges, skippedQuads, patchSlabBoxes);

    // step 8: core cells
    int nCoreCellsRaw = 0;
    auto core = buildCore(g, patchSlabBoxes, hs, nCoreCellsRaw);

    // assemble, orient, meta
    cells.clear();
    cellVolumes.clear(); cellClosures.clear(); cellJeansV.clear(); cellJeansRaw.clear(); cellJeansOpt.clear();
    int id = 0;
    for (auto& c : wedges) { c.id = id++; cells.push_back(c); }
    for (auto& c : core)   { c.id = id++; cells.push_back(c); }
    double totalWedgeVolume = 0, totalHexVolume = 0;
    for (auto& c : cells) {
        orientCell(c);
        double vol = polyVolume(c);
        double cl = polyClosure(c, vol);
        JeansStat jeans = jeansOf(c, vol, hs);
        cellVolumes.push_back(vol);
        cellClosures.push_back(cl);
        cellJeansV.push_back(jeans.value);
        cellJeansRaw.push_back(jeans.raw);
        cellJeansOpt.push_back(jeans.jOpt);
        if (c.kind == CellKind::Wedge) totalWedgeVolume += vol; else totalHexVolume += vol;
    }

    // DEBUG volume split
    double wedgeVol = 0, hexVol = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].kind == CellKind::Wedge) wedgeVol += cellVolumes[i];
        else hexVol += cellVolumes[i];
    }
    std::cout << "DEBUG wedge volume: " << wedgeVol << "  hex volume: " << hexVol
              << "  sum: " << (wedgeVol + hexVol) << "\n";
    // step 9: verification
    std::map<std::string, int> faceCount;
    double totalVol = 0, minVol = 1e300, maxClosure = 0, minJeans = 1e300;
    int boundaryFaces = 0, interiorFaces = 0, faultFaces = 0;
    for (const auto& c : cells) {
        double vol = polyVolume(c);
        double cl = polyClosure(c, vol);
        JeansStat jeans = jeansOf(c, vol, hs);
        totalVol += vol;
        if (vol < minVol) minVol = vol;
        if (cl > maxClosure) maxClosure = cl;
        if (jeans.value < minJeans) minJeans = jeans.value;
        for (const auto& t : c.tris) {
            const Vec3& a = c.verts[t[0]], & b = c.verts[t[1]], & cc = c.verts[t[2]];
            faceCount[faceKey(a, b, cc)]++;
        }
    }
    for (const auto& kv : faceCount) {
        int n = kv.second;
        if (n == 1) boundaryFaces++;
        else if (n == 2) interiorFaces++;
        else faultFaces++;
    }

    verification.nWedges = (int)wedges.size();
    verification.nHex = (int)core.size();
    verification.totalVolume = totalVol;
    verification.domainVolume = g.domainVolume;
    verification.volumeErrorRel = std::abs(totalVol - g.domainVolume) / std::max(g.domainVolume, 1e-30);
    verification.minVol = minVol;
    verification.maxClosure = maxClosure;
    verification.minJeans = minJeans;
    verification.nSurfaceTri = (int)bmOut.tris.size();
    verification.nSurfaceVert = (int)bmOut.verts.size();
    verification.skippedQuads = skippedQuads;
    verification.totalWedgeVolume = totalWedgeVolume;
    verification.totalHexVolume = totalHexVolume;
    verification.boundaryFaces = boundaryFaces;
    verification.interiorFaces = interiorFaces;
    verification.faultFaces = faultFaces;
    verification.minClearance = std::min_element(dMedial.begin(), dMedial.end()) != dMedial.end() ?
                                 *std::min_element(dMedial.begin(), dMedial.end()) : 0.0;
    {
        double avgW = 0; int nW = 0, maxW = 0; RawCell* maxCell = nullptr;
        double minW = 1e300; RawCell* minCell = nullptr;
        for (auto& c : cells) {
            if (c.kind == CellKind::Wedge) {
                double v = polyVolume(c);
                avgW += v; nW++;
                if (v > maxW) { maxW = v; maxCell = &c; }
                if (v < minW) { minW = v; minCell = &c; }
            }
        }
        std::cout << "DEBUG wedges=" << nW << " avg=" << avgW/nW << " totalV=" << avgW << "\n";
    {
        // print 5 wedge cells
        int printed = 0;
        double sumV = 0, maxV = 0;
        for (const auto& c : cells) {
            if (c.kind != CellKind::Wedge) continue;
            double v = polyVolume(c);
            sumV += v;
            if (v > maxV) { maxV = v; std::cout << "  MAX WEDGE patch=" << c.patch << " layer=" << c.layer << " vol=" << v << "\n"; }
            if (printed < 6) {
                std::cout << "  wedge layer=" << c.layer << " patch=" << c.patch << " tri=" << c.surfaceTri
                          << " vol=" << v << "\n";
                printed++;
            }
        }
        std::cout << "  sumV = " << sumV << " maxV = " << maxV << "\n";
    {
        double maxCl = 0; const RawCell* maxClCell = nullptr;
        for (const auto& c : cells) {
            double v = polyVolume(c);
            double cl = polyClosure(c, v);
            if (cl > maxCl) { maxCl = cl; maxClCell = &c; }
        }
        std::cout << "  MAX closure = " << maxCl << " (wedge? " << (maxClCell->kind == CellKind::Wedge ? "W" : "H")
                  << " patch=" << maxClCell->patch << " layer=" << maxClCell->layer << " vol=" << polyVolume(*maxClCell) << ")\n";
        {
            RawCell tmp = *maxClCell;
            orientCell(tmp);
            Vec3 cc{0,0,0}; for (const auto& v : tmp.verts) cc = cc + v; cc = cc / (int)tmp.verts.size();
            std::cout << "    verts:";
            for (const auto& vv : tmp.verts) std::cout << " (" << vv.x << "," << vv.y << "," << vv.z << ")";
            std::cout << "\n    centroid=(" << cc.x << "," << cc.y << "," << cc.z << ")\n";
            std::cout << "    tris:";
            for (const auto& t : tmp.tris) {
                const Vec3& a = tmp.verts[t[0]], & b = tmp.verts[t[1]], & c = tmp.verts[t[2]];
                Vec3 S = (b - a).cross(c - a) * 0.5;
                std::cout << " tris(" << t[0] << "," << t[1] << "," << t[2] << ") S=(" << S.x << "," << S.y << "," << S.z << ")";
            }
            std::cout << "\n";
        }
        int n13 = 0;
        for (const auto& c : cells) {
            if (c.kind == CellKind::Wedge && c.patch == 13 && n13 < 2) {
                std::cout << "  patch13 wedge layer=" << c.layer << " tri=" << c.surfaceTri << " vol=" << polyVolume(c) << "\n";
                for (const auto& vv : c.verts) std::cout << "    (" << vv.x << "," << vv.y << "," << vv.z << ")\n";
                n13++;
            }
        }
    }
    }
        if (maxCell) std::cout << "  max wedge patch=" << maxCell->patch << " layer=" << maxCell->layer << "\n";
        if (minCell) std::cout << "  min wedge patch=" << minCell->patch << " layer=" << minCell->layer << "\n";
        double avgH = 0; for (auto& c : cells) if (c.kind == CellKind::Hex) avgH += polyVolume(c);
        std::cout << "DEBUG hexs=" << verification.nHex << " sumHexVol=" << avgH << "\n";
    }
    return 0;
}

// ---------------- CaseDump + TS emit ----------------
struct PatchOut {
    int id = -1; std::string name;
    Vec3 origin, normal, uDir, vDir; double lu, lv;
    int gridU = 0, gridV = 0;
    double slabDepth = 0;
};

struct CaseDump {
    CaseGeom g;
    BoundaryMesh bm;
    std::vector<Vec3> pseudorm;
    std::vector<double> dMedial;
    std::vector<double> slabDepth;
    std::vector<RawCell> cells;
    std::vector<double> cellVolumes;
    std::vector<double> cellClosures;
    std::vector<double> cellJeans;
    std::vector<double> cellJeansRaw;
    std::vector<double> cellJeansOpt;
    std::vector<int> ownerOf;          // surface triangle -> first wedge cell id (layer 1)
    std::vector<AABB> patchSlabBoxes;
    Verification verification;
    std::vector<PatchOut> patches;
};

static void writeTSFile(const std::vector<CaseDump>& dumps, const std::string& path) {
    std::ofstream f(path);
    f << "// AUTO-GENERATED by engine/octree_hexcore.cpp — do not edit\n"
      << "export interface CellMeta { id:number; kind:0|1; patch:number; layer:number; surfaceTri:number; volume:number; closure:number; jeans:number; jeansRaw:number; jeansOpt:number }\n"
      << "export interface Cell { verts:number[][]; tris:number[][]; meta:CellMeta }\n"
      << "export interface SurfaceTris { a:number; b:number; c:number; patch:number; quad:number }\n"
      << "export interface PatchOut { id:number; name:string; origin:number[]; normal:number[]; uDir:number[]; vDir:number[]; lu:number; lv:number; gridU:number; gridV:number; slabDepth:number }\n"
      << "export interface VerifOut {\n"
      << "  nWedges:number; nHex:number; nQuads:number; nTris:number; nVerts:number;\n"
      << "  totalVolume:number; domainVolume:number; volumeErrorRel:number;\n"
      << "  minVol:number; maxClosure:number; minJeans:number;\n"
      << "  boundaryFaces:number; interiorFaces:number; faultFaces:number;\n"
      << "  skippedQuads:number; minClearance:number;\n"
      << "  pass:boolean; passCliff:boolean; passJeans:boolean;\n"
      << "}\n"
      << "export interface CaseData {\n"
      << "  id:string; name:string; hs:number; domainVolume:number;\n"
      << "  bbox:number[];\n"
      << "  verts:number[][]; tris:SurfaceTris[]; patches:PatchOut[]; cells:Cell[];\n"
      << "  verification:VerifOut;\n"
      << "}\n"
      << "export const MESH_DATA: CaseData[] = [\n";

    auto fmtf = [](double q) {
        std::ostringstream os;
        os << std::setprecision(17) << q;
        return os.str();
    };
    auto fmtVec = [&](const Vec3& v) {
        return "[" + fmtf(v.x) + "," + fmtf(v.y) + "," + fmtf(v.z) + "]";
    };

    for (int ci = 0; ci < (int)dumps.size(); ++ci) {
        const auto& d = dumps[ci];
        f << "  { // case " << ci << " " << d.g.id << "\n"
          << "    id: \"" << d.g.id << "\",\n    name: \"" << d.g.name << "\",\n"
          << "    hs: " << fmtf(d.g.hs) << ",\n"
          << "    domainVolume: " << fmtf(d.g.domainVolume) << ",\n"
          << "    bbox: [" << fmtf(d.g.bboxMin.x) << "," << fmtf(d.g.bboxMin.y) << "," << fmtf(d.g.bboxMin.z)
          << "," << fmtf(d.g.bboxMax.x) << "," << fmtf(d.g.bboxMax.y) << "," << fmtf(d.g.bboxMax.z) << "],\n"
          << "    verts: [\n";
        for (const auto& v : d.bm.verts) f << "      " << fmtVec(v) << ",\n";
        f << "    ],\n"
          << "    tris: [\n";
        for (const auto& t : d.bm.tris) {
            f << "      { a: " << t.a << ", b: " << t.b << ", c: " << t.c << ", patch: " << t.patch << ", quad: " << t.quad << " },\n";
        }
        f << "    ],\n"
          << "    patches: [\n";
        for (const auto& p : d.patches) {
            f << "      { id: " << p.id << ", name: \"" << p.name << "\", origin: " << fmtVec(p.origin)
              << ", normal: " << fmtVec(p.normal)
              << ", uDir: " << fmtVec(p.uDir)
              << ", vDir: " << fmtVec(p.vDir)
              << ", lu: " << fmtf(p.lu) << ", lv: " << fmtf(p.lv)
              << ", gridU: " << p.gridU << ", gridV: " << p.gridV
              << ", slabDepth: " << fmtf(p.slabDepth) << " },\n";
        }
        f << "    ],\n"
          << "    cells: [\n";
        for (size_t i = 0; i < d.cells.size(); ++i) {
            const auto& c = d.cells[i];
            f << "      { verts: [";
            for (size_t j = 0; j < c.verts.size(); ++j) {
                if (j) f << ",";
                f << fmtVec(c.verts[j]);
            }
            f << "], tris: [";
            for (size_t j = 0; j < c.tris.size(); ++j) {
                if (j) f << ",";
                f << "[" << c.tris[j][0] << "," << c.tris[j][1] << "," << c.tris[j][2] << "]";
            }
            f << "], meta: { id: " << i << ", kind: " << (int)c.kind << ", patch: " << c.patch
              << ", layer: " << c.layer << ", surfaceTri: " << c.surfaceTri
              << ", volume: " << fmtf(d.cellVolumes[i])
              << ", closure: " << fmtf(d.cellClosures[i])
              << ", jeans: " << fmtf(d.cellJeans[i])
              << ", jeansRaw: " << fmtf(d.cellJeansRaw[i])
              << ", jeansOpt: " << fmtf(d.cellJeansOpt[i])
              << " } },\n";
        }
        auto& v = d.verification;
        f << "    ],\n"
          << "    verification: {\n"
          << "      nWedges: " << v.nWedges << ", nHex: " << v.nHex << ",\n"
          << "      nQuads: " << v.nSurfaceTri << ", nTris: " << v.nSurfaceTri << ", nVerts: " << v.nSurfaceVert << ",\n"
          << "      totalVolume: " << fmtf(v.totalVolume) << ", domainVolume: " << fmtf(v.domainVolume)
          << ", volumeErrorRel: " << fmtf(v.volumeErrorRel) << ",\n"
          << "      minVol: " << fmtf(v.minVol) << ", maxClosure: " << fmtf(v.maxClosure)
          << ", minJeans: " << fmtf(v.minJeans) << ",\n"
          << "      boundaryFaces: " << v.boundaryFaces << ", interiorFaces: " << v.interiorFaces
          << ", faultFaces: " << v.faultFaces << ",\n"
          << "      skippedQuads: " << v.skippedQuads << ", minClearance: " << fmtf(v.minClearance) << ",\n"
          << "      pass: " << (v.faultFaces == 0 && v.minVol > 0 && v.maxClosure < 1e-14 ? "true" : "false") << ",\n"
          << "      passJeans: " << (v.minJeans >= 0.25 ? "true" : "false") << ",\n"
          << "      passCliff: " << (v.minVol > 0 ? "true" : "false") << "\n"
          << "    }\n  },\n";
    }
    f << "];\n";
    f.close();
}

// ---------------- main driver ----------------
static CaseDump buildCase(const CaseGeom& g, double hs, const BLParams& p) {
    CaseDump d;
    d.g = g;
    Verification verification;
    BoundaryMesh bmOut;
    std::vector<Vec3> pseudormOut;
    std::vector<double> dMedialOut, slabDepthOut;
    std::vector<RawCell> cells;
    std::vector<double> cellVolumes, cellClosures, cellJeansV, cellJeansRaw, cellJeansOpt;
    int rc = runCase(g, hs, p, cells, cellVolumes, cellClosures, cellJeansV, cellJeansRaw, cellJeansOpt,
                     verification, bmOut, pseudormOut, dMedialOut, slabDepthOut);
    (void)rc;

    d.bm = bmOut;
    d.pseudorm = pseudormOut;
    d.dMedial = dMedialOut;
    d.slabDepth = slabDepthOut;
    d.cells = cells;
    d.cellVolumes = cellVolumes; d.cellClosures = cellClosures;
    d.cellJeans = cellJeansV; d.cellJeansRaw = cellJeansRaw; d.cellJeansOpt = cellJeansOpt;
    d.verification = verification;

    // per-surface-triangle owner: first wedge cell
    d.ownerOf.assign(d.bm.tris.size(), -1);
    for (auto& c : d.cells) {
        if (c.kind == CellKind::Wedge && c.layer == 1 && c.surfaceTri >= 0) {
            if (d.ownerOf[c.surfaceTri] == -1) d.ownerOf[c.surfaceTri] = c.id;
        }
    }
    // patch slab boxes from the verification data (recompute slab depths per patch)
    d.patchSlabBoxes.resize(d.g.patches.size());
    d.patches.resize(d.g.patches.size());
    for (size_t i = 0; i < d.g.patches.size(); ++i) {
        double minSlab = 1e300;
        for (int v = 0; v < (int)d.bm.verts.size(); ++v) {
            for (int t : d.bm.vertTris[v]) {
                if (d.bm.patchOfTri[t] == (int)i) {
                    if (d.slabDepth[v] < minSlab) minSlab = d.slabDepth[v];
                    break;
                }
            }
        }
        if (minSlab > 1e299) minSlab = 0.3;
        PatchOut po;
        po.id = (int)i; po.name = d.g.patches[i].name;
        po.origin = d.g.patches[i].origin; po.normal = d.g.patches[i].normal;
        po.uDir = d.g.patches[i].uDir; po.vDir = d.g.patches[i].vDir;
        po.lu = d.g.patches[i].lu; po.lv = d.g.patches[i].lv;
        po.gridU = d.g.patches[i].nu; po.gridV = d.g.patches[i].nv;
        po.slabDepth = minSlab;
        d.patches[i] = po;
        d.patchSlabBoxes[i] = slabBox(d.g.patches[i], minSlab);
    }
    return d;
}

int main(int argc, char** argv) {
    std::string outPath = "src/data/mesh.generated.ts";
    std::string filterId;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (std::string(argv[i]) == "--case" && i + 1 < argc) filterId = argv[++i];
    }
    try { std::filesystem::create_directories(std::filesystem::path(outPath).parent_path()); } catch (...) {}

    std::vector<CaseDump> dumps;
    auto cases = { Geo::steppedLChannel(), Geo::twinPlate() };
    BLParams p;
    p.dY1 = 0.06; p.r = 1.35; p.maxLayers = 8; p.capFactor = 0.40;
    for (const auto& g : cases) {
        if (!filterId.empty() && g.id != filterId) continue;
        auto t0 = std::chrono::high_resolution_clock::now();
        auto d = buildCase(g, g.hs, p);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cerr << "=== " << g.name << " (" << ms << " ms) ===\n";
        std::cerr << "  cells: " << d.cells.size() << " (wedges " << d.verification.nWedges
                  << " + core " << d.verification.nHex << ")\n";
        std::cerr << "  verts: " << d.verification.nSurfaceVert << "  tris: " << d.verification.nSurfaceTri << "\n";
        std::cerr << "  total volume: " << d.verification.totalVolume << " (domain " << d.verification.domainVolume
                  << ", rel err " << d.verification.volumeErrorRel << ")\n";
        std::cerr << "  min cell volume: " << d.verification.minVol
                  << "  max closure: " << d.verification.maxClosure
                  << "  min JENS: " << d.verification.minJeans << "\n";
        std::cerr << "  boundary faces: " << d.verification.boundaryFaces
                  << "  interior faces: " << d.verification.interiorFaces
                  << "  fault faces: " << d.verification.faultFaces
                  << "  skipped quads: " << d.verification.skippedQuads << "\n";
        dumps.push_back(d);
    }
    writeTSFile(dumps, outPath);
    std::cerr << "WROTE " << outPath << " (" << dumps.size() << " cases)\n";
    return 0;
}
