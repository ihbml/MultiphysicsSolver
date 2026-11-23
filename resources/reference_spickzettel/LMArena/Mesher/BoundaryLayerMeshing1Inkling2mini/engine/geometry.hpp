// geometry.hpp — geometry definitions: L-channel + twin-plate cooling gap.
// Axis-aligned planar wall patches (exact rectangles) + union-of-boxes domain.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <sstream>

// ---------------- math primitives ----------------
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double len() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        double l = len();
        if (l < 1e-300) return {0, 0, 0};
        return *this / l;
    }
};

// Kahan compensated scalar sum
struct KahanScalar {
    double s = 0, c = 0;
    void add(double x) {
        double y = x - c;
        double t = s + y;
        c = (t - s) - y;
        s = t;
    }
    double value() const { return s; }
};
struct KahanVec3 {
    KahanScalar sx{}, sy{}, sz{};
    void add(const Vec3& v) { sx.add(v.x); sy.add(v.y); sz.add(v.z); }
    Vec3 value() const { return {sx.value(), sy.value(), sz.value()}; }
};

// ---------------- domain pieces ----------------
struct Box3 {
    double x0, x1, y0, y1, z0, z1;
    double vol() const { return (x1 - x0) * (y1 - y0) * (z1 - z0); }
    bool valid() const { return x0 < x1 && y0 < y1 && z0 < z1; }
};

struct Patch {
    int id = -1;
    std::string name;
    Vec3 origin;                 // (u=0,v=0) corner on the plane
    Vec3 uDir, vDir;             // unit in-plane vectors (cross(uDir,vDir)=normal)
    Vec3 normal;                 // unit, INTO domain
    double lu = 0, lv = 0;       // extents
    int nu = 1, nv = 1;          // quads in u/v
    Vec3 at(double u, double v) const { return origin + uDir * u + vDir * v; }
    // AABB of the flat patch rectangle (world)
    Box3 localBox() const;
};

struct CaseGeom {
    int caseIndex = 0;
    std::string id;
    std::string name;
    double hs;                   // nominal core grid cell size
    double dY1;                  // first layer height
    double rgrowth;              // growth ratio
    double hardLimit;            // global cap on H_total (0.4*dmedial)
    Vec3 bboxMin, bboxMax;
    std::vector<Box3> boxes;     // domain (union)
    std::vector<Patch> patches;
    double domainVolume;
};

namespace Geo {

inline Box3 patchBox(const Patch& p) {
    Vec3 mn = p.origin, mx = p.origin;
    auto walk = [&](double u, double v) {
        Vec3 q = p.at(u, v);
        mn.x = std::min(mn.x, q.x); mn.y = std::min(mn.y, q.y); mn.z = std::min(mn.z, q.z);
        mx.x = std::max(mx.x, q.x); mx.y = std::max(mx.y, q.y); mx.z = std::max(mx.z, q.z);
    };
    walk(0, 0); walk(p.lu, 0); walk(0, p.lv); walk(p.lu, p.lv);
    return {mn.x, mx.x, mn.y, mx.y, mn.z, mx.z};
}

inline Patch makePatch(std::string name, Vec3 origin, Vec3 uDir, Vec3 vDir, Vec3 normal,
                       double lu, double lv, double hs) {
    Patch p;
    p.name = name; p.origin = origin; p.uDir = uDir; p.vDir = vDir; p.normal = normal;
    p.lu = lu; p.lv = lv;
    p.nu = std::max(1, (int)std::round(lu / hs));
    p.nv = std::max(1, (int)std::round(lv / hs));
    return p;
}

inline CaseGeom steppedLChannel() {
    CaseGeom g{0, "L-channel", "Stepped L-Channel", 0.5, 0.06, 1.35, 0.45, {0,0,0}, {2,2,10}, {}, {}, 0};
    g.boxes = {
        {0.5, 2.0, 0.0, 0.5, 0.0, 4.0},      // A1: horizontal arm z<4
        {0.5, 2.0, 0.3, 0.5, 4.0, 10.0},     // A2: horizontal arm z>=4
        {0.0, 0.5, 0.0, 2.0, 0.0, 10.0},     // B : vertical arm
    };
    for (const auto& b : g.boxes) g.domainVolume += b.vol();

    auto lp = [&](std::string name, Vec3 origin, Vec3 uDir, Vec3 vDir, Vec3 normal,
                  double lu, double lv) {
        return makePatch(name, origin, uDir, vDir, normal, lu, lv, g.hs);
    };
    // floor of vertical arm: y=0, x in [0,0.5], z in [0,10]
    g.patches.push_back(lp("floor-vert", Vec3{0.0, 0.0, 0.0}, Vec3{0,0,1}, Vec3{1,0,0}, Vec3{0,1,0}, 10, 0.5));
    // floor of horizontal arm low z: y=0, x in [0.5,2], z in [0,4]
    g.patches.push_back(lp("floor-h-low", Vec3{0.5, 0.0, 0.0}, Vec3{0,0,1}, Vec3{1,0,0}, Vec3{0,1,0}, 4, 1.5));
    // floor of horizontal arm high z: y=0.3, x in [0.5,2], z in [4,10]
    g.patches.push_back(lp("floor-h-high", Vec3{0.5, 0.3, 4.0}, Vec3{0,0,1}, Vec3{1,0,0}, Vec3{0,1,0}, 6, 1.5));
    // riser at z=4: x in [0.5,2], y in [0,0.3], inward normal (0,0,-1)
    g.patches.push_back(lp("riser", Vec3{0.5, 0.3, 4.0}, Vec3{1,0,0}, Vec3{0,-1,0}, Vec3{0,0,-1}, 1.5, 0.3));
    // step side at x=0.5: y in [0,0.3], z in [4,10], inward normal (-1,0,0)
    g.patches.push_back(lp("step-side-v", Vec3{0.5, 0.0, 10.0}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 0.3, 6.0));
    // top of vertical arm: y=2, x in [0,0.5]
    g.patches.push_back(lp("top-vert", Vec3{0.5, 2.0, 0.0}, Vec3{0,0,1}, Vec3{-1,0,0}, Vec3{0,-1,0}, 10, 0.5));
    // right wall of vertical arm: x=0.5, y in [0.5,2]
    g.patches.push_back(lp("right-vert", Vec3{0.5, 0.5, 10.0}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 1.5, 10));
    // ceiling of horizontal arm: y=0.5, x in [0.5,2]
    g.patches.push_back(lp("top-h", Vec3{2.0, 0.5, 0.0}, Vec3{0,0,1}, Vec3{-1,0,0}, Vec3{0,-1,0}, 10, 1.5));
    // right wall horizontal arm low: x=2, y in [0,0.5], z in [0,4]
    g.patches.push_back(lp("right-h-low", Vec3{2.0, 0.0, 4.0}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 0.5, 4));
    // right wall horizontal arm high: x=2, y in [0.3,0.5], z in [4,10]
    g.patches.push_back(lp("right-h-high", Vec3{2.0, 0.3, 10.0}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 0.2, 6));
    // left wall of vertical arm: x=0, y in [0,2]
    g.patches.push_back(lp("left-vert", Vec3{0.0, 0.0, 0.0}, Vec3{0,1,0}, Vec3{0,0,1}, Vec3{1,0,0}, 2.0, 10));
    // end0 at z=0: vertical arm x in [0,0.5], y in [0,2]
    g.patches.push_back(lp("end0-vert", Vec3{0.0, 0.0, 0.0}, Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}, 0.5, 2.0));
    // end0 at z=0: horizontal arm x in [0.5,2], y in [0,0.5]
    g.patches.push_back(lp("end0-h", Vec3{0.5, 0.0, 0.0}, Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}, 1.5, 0.5));
    // endN at z=10: vertical arm
    g.patches.push_back(lp("endN-vert", Vec3{0.0, 2.0, 10.0}, Vec3{1,0,0}, Vec3{0,-1,0}, Vec3{0,0,-1}, 0.5, 2.0));
    // endN at z=10: horizontal arm (high floor)
    g.patches.push_back(lp("endN-h", Vec3{0.5, 0.5, 10.0}, Vec3{1,0,0}, Vec3{0,-1,0}, Vec3{0,0,-1}, 1.5, 0.2));

    for (int i = 0; i < (int)g.patches.size(); ++i) g.patches[i].id = i;
    return g;
}

inline CaseGeom twinPlate() {
    CaseGeom g{1, "twin-plate", "Twin-Plate Cooling Gap", 0.5, 0.05, 1.3, 0.45, {0,0,0}, {12,8,0.5}, {}, {}, 0};
    // Domain = box(0,12 x 0,8 x 0,0.5) minus rib box (x in [3,9], y in [0,8], z in [0,0.18])
    g.boxes = {
        {0.0, 3.0, 0.0, 8.0, 0.0, 0.5},    // left side
        {9.0, 12.0, 0.0, 8.0, 0.0, 0.5},   // right side
        {3.0, 9.0, 0.0, 8.0, 0.18, 0.5},   // rib region (floor at 0.18)
    };
    for (const auto& b : g.boxes) g.domainVolume += b.vol();

    auto lp = [&](std::string name, Vec3 origin, Vec3 uDir, Vec3 vDir, Vec3 normal,
                  double lu, double lv) {
        return makePatch(name, origin, uDir, vDir, normal, lu, lv, g.hs);
    };
    // floors at z=0 (left/right of rib)
    g.patches.push_back(lp("floor-lo", Vec3{0.0, 0.0, 0.0}, Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}, 3, 8));
    g.patches.push_back(lp("floor-hi", Vec3{9.0, 0.0, 0.0}, Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}, 3, 8));
    // ceiling: z=0.5
    g.patches.push_back(lp("top", Vec3{0.0, 8.0, 0.5}, Vec3{1,0,0}, Vec3{0,-1,0}, Vec3{0,0,-1}, 12, 8));
    // rib top: z=0.18, x in [3,9]
    g.patches.push_back(lp("rib-top", Vec3{3.0, 0.0, 0.18}, Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}, 6, 8));
    // rib sides: x=3 (lo) and x=9 (hi)
    g.patches.push_back(lp("rib-side-lo", Vec3{3.0, 0.0, 0.18}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 8, 0.18));
    g.patches.push_back(lp("rib-side-hi", Vec3{9.0, 0.0, 0.0}, Vec3{0,1,0}, Vec3{0,0,1}, Vec3{1,0,0}, 8, 0.18));
    // end walls x=0 and x=12
    g.patches.push_back(lp("end-x0", Vec3{0.0, 0.0, 0.0}, Vec3{0,1,0}, Vec3{0,0,1}, Vec3{1,0,0}, 8, 0.5));
    g.patches.push_back(lp("end-x1", Vec3{12.0, 0.0, 0.5}, Vec3{0,1,0}, Vec3{0,0,-1}, Vec3{-1,0,0}, 8, 0.5));
    // y=0 side: 3 rectangular sections
    g.patches.push_back(lp("y0-lo", Vec3{0.0, 0.0, 0.5}, Vec3{1,0,0}, Vec3{0,0,-1}, Vec3{0,1,0}, 3, 0.5));
    g.patches.push_back(lp("y0-mid", Vec3{3.0, 0.0, 0.5}, Vec3{1,0,0}, Vec3{0,0,-1}, Vec3{0,1,0}, 6, 0.32));
    g.patches.push_back(lp("y0-hi", Vec3{9.0, 0.0, 0.5}, Vec3{1,0,0}, Vec3{0,0,-1}, Vec3{0,1,0}, 3, 0.5));
    // y=8 side: 3 rectangular sections
    g.patches.push_back(lp("yN-lo", Vec3{0.0, 8.0, 0.0}, Vec3{1,0,0}, Vec3{0,0,1}, Vec3{0,-1,0}, 3, 0.5));
    g.patches.push_back(lp("yN-mid", Vec3{3.0, 8.0, 0.18}, Vec3{1,0,0}, Vec3{0,0,1}, Vec3{0,-1,0}, 6, 0.32));
    g.patches.push_back(lp("yN-hi", Vec3{9.0, 8.0, 0.0}, Vec3{1,0,0}, Vec3{0,0,1}, Vec3{0,-1,0}, 3, 0.5));

    for (int i = 0; i < (int)g.patches.size(); ++i) g.patches[i].id = i;
    return g;
}

} // namespace Geo
