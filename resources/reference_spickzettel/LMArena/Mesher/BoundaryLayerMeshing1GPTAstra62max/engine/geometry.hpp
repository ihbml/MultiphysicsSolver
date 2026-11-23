#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Vec {
    double x = 0, y = 0, z = 0;
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    Vec operator+(Vec b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec operator*(double f) const { return {x * f, y * f, z * f}; }
    Vec operator/(double f) const { return *this * (1 / f); }
    double dot(Vec b) const { return x * b.x + y * b.y + z * b.z; }
    Vec cross(Vec b) const { return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x}; }
    double norm() const { return std::sqrt(dot(*this)); }
    Vec unit() const {
        double l = norm();
        if (!(l > 1e-30))
            throw std::runtime_error("Degenerate direction");
        return *this / l;
    }
};
using Ids = std::vector<int>;
using Faces = std::vector<Ids>;
using Points = std::vector<Vec>;
struct Cell {
    Ids v;
    int layer = 0;
    double quality = 0, volume = 0, closure = 0;
};
struct Boundary {
    Ids v;
    int owner = -1, patch = -1;
};
inline double det(Vec a, Vec b, Vec c) { return a.cross(b).dot(c); }
inline Vec center(const Points &p, const Ids &f) {
    Vec c{};
    for (int i : f)
        c = c + p[i];
    return c / double(f.size());
}
inline Ids key(Ids v) {
    std::sort(v.begin(), v.end());
    return v;
}
inline const Faces &localFaces(int n) {
    static const Faces tet = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
    static const Faces pyr = {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    static const Faces pri = {{0, 2, 1}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
    static const Faces hex = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                              {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    if (n == 4)
        return tet;
    if (n == 5)
        return pyr;
    if (n == 6)
        return pri;
    if (n == 8)
        return hex;
    throw std::runtime_error("Unsupported element topology");
}
inline Faces cellFaces(const Ids &v) {
    Faces fs = localFaces(v.size());
    for (auto &f : fs)
        for (auto &i : f)
            i = v[i];
    return fs;
}
inline Vec areaVector(const Points &p, const Ids &f) {
    Vec a{};
    for (size_t i = 1; i + 1 < f.size(); ++i)
        a = a + (p[f[i]] - p[f[0]]).cross(p[f[i + 1]] - p[f[0]]) * .5;
    return a;
}
inline bool reversedCycle(const Ids &a, const Ids &b) {
    auto at = std::find(b.begin(), b.end(), a[0]);
    if (at == b.end() || a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[(at - b.begin() + a.size() - i) % a.size()])
            return false;
    return true;
}
inline double scaled(Vec x, Vec y, Vec z) {
    double d = x.norm() * y.norm() * z.norm();
    return d > 1e-30 ? std::clamp(det(x, y, z) / d, -1., 1.) : -1.;
}
inline double normalizedSJ(double s, double opt) {
    if (s > opt)
        return 1 + opt - s;
    if (s < -opt)
        return -1 - opt - s;
    return s / opt;
}
inline double jens(const Points &p, const Ids &v) {
    double q = 1;
    int n = int(v.size());
    double opt = n == 8 ? 1 : n == 6 ? std::sqrt(3.) / 2 : std::sqrt(.5);
    auto corner = [&](int a, int b, int c, int d) {
        q = std::min(
            q, normalizedSJ(scaled(p[v[b]] - p[v[a]], p[v[c]] - p[v[a]], p[v[d]] - p[v[a]]), opt));
    };
    if (n == 4) {
        corner(0, 1, 2, 3);
        corner(1, 0, 3, 2);
        corner(2, 0, 1, 3);
        corner(3, 0, 2, 1);
    }
    if (n == 6 || n == 8) {
        int m = n / 2;
        for (int i = 0; i < m; i++) {
            int a = (i + 1) % m, b = (i + m - 1) % m;
            corner(i, a, b, i + m);
            corner(i + m, b + m, a + m, i);
        }
    }
    if (n == 5) {
        Vec normal = areaVector(p, {v[0], v[1], v[2], v[3]});
        if (normal.norm() < 1e-20)
            return -1;
        double height = 0;
        for (int i = 0; i < 4; i++)
            height += (p[v[(i + 1) % 4]] - p[v[i]]).norm() * .25;
        Vec up = normal.unit() * height;
        // Extruding the base by its mean edge length gives the fictitious basis hex.
        for (int i = 0; i < 4; i++) {
            int a = (i + 1) % 4, b = (i + 3) % 4;
            corner(i, a, b, 4);
            corner(4, i, b, (i + 2) % 4);
            q = std::min(q, scaled(p[v[a]] - p[v[i]], p[v[b]] - p[v[i]], up));
        }
    }
    return q;
}
struct Integral {
    double volume = 0, closure = 0, minStar = 1e100;
};
inline Integral integrate(const Points &p, const Ids &v) {
    Integral r;
    Vec c = center(p, v), sum{};
    for (auto f : cellFaces(v)) {
        // Both owners of a warped quad must use precisely the same diagonal.
        std::rotate(f.begin(), std::min_element(f.begin(), f.end()), f.end());
        Vec sf{};
        for (size_t j = 1; j + 1 < f.size(); j++) {
            Vec a = p[f[0]], b = p[f[j]], d = p[f[j + 1]];
            sf = sf + (b - a).cross(d - a) * .5;
            double tv = det(a - c, b - c, d - c) / 6.;
            r.volume += tv;
            r.minStar = std::min(r.minStar, tv);
        }
        sum = sum + sf;
    }
    r.closure = sum.norm() / std::pow(std::max(std::abs(r.volume), 1e-300), 2. / 3.);
    return r;
}
inline void orient(const Points &p, Ids &v) {
    if (integrate(p, v).volume >= 0)
        return;
    if (v.size() == 4)
        std::swap(v[1], v[2]);
    else if (v.size() == 5)
        std::swap(v[1], v[3]);
    else {
        int m = v.size() / 2;
        std::reverse(v.begin() + 1, v.begin() + m);
        std::reverse(v.begin() + m + 1, v.end());
    }
}
// For a linear wedge, det J is affine on every reference triangle and quadratic
// through its thickness. Endpoints and an interior stationary point suffice.
inline double prismJacobianBound(const Points &p, const Ids &v) {
    Vec a = p[v[1]] - p[v[0]], b = p[v[2]] - p[v[0]], at = p[v[4]] - p[v[3]],
        bt = p[v[5]] - p[v[3]];
    double result = 1.;
    for (int i = 0; i < 3; i++) {
        Vec d = p[v[i + 3]] - p[v[i]], da = at - a, db = bt - b;
        double A = det(da, db, d), B = det(da, b, d) + det(a, db, d), C = det(a, b, d);
        double m = std::min(C, A + B + C);
        if (A > 0) {
            double t = -B / (2 * A);
            if (t > 0 && t < 1)
                m = std::min(m, (A * t + B) * t + C);
        }
        double denom = std::max(a.norm(), at.norm()) * std::max(b.norm(), bt.norm()) * d.norm();
        result = std::min(result, denom > 0 ? m / denom : -1.);
    }
    return result;
}

struct Triangle {
    std::array<int, 3> v;
    Vec n, c;
};
struct BVH {
    struct Node {
        Vec lo, hi;
        int begin, end, left = -1, right = -1;
    };
    const Points &p;
    const std::vector<Triangle> &t;
    Ids order;
    std::vector<Node> nodes;
    BVH(const Points &ps, const std::vector<Triangle> &ts) : p(ps), t(ts), order(ts.size()) {
        std::iota(order.begin(), order.end(), 0);
        build(0, order.size());
    }
    int build(int a, int b) {
        Vec lo{1e100, 1e100, 1e100}, hi = lo * (-1);
        for (int k = a; k < b; k++)
            for (int i : t[order[k]].v) {
                Vec q = p[i];
                lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
                hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
            }
        int id = nodes.size();
        nodes.push_back({lo, hi, a, b});
        if (b - a <= 8)
            return id;
        Vec s = hi - lo;
        int k = s.x > s.y ? (s.x > s.z ? 0 : 2) : (s.y > s.z ? 1 : 2);
        int m = (a + b) / 2;
        std::nth_element(order.begin() + a, order.begin() + m, order.begin() + b,
                         [&](int i, int j) { return t[i].c[k] < t[j].c[k]; });
        int left = build(a, m), right = build(m, b);
        nodes[id].left = left;
        nodes[id].right = right;
        return id;
    }
    void cast(int id, Vec o, Vec d, int skip, double &best) const {
        auto &n = nodes[id];
        double lo = 0, hi = best;
        for (int k = 0; k < 3; k++) {
            if (std::abs(d[k]) < 1e-15) {
                if (o[k] < n.lo[k] - 1e-11 || o[k] > n.hi[k] + 1e-11)
                    return;
                continue;
            }
            double a = (n.lo[k] - o[k]) / d[k], b = (n.hi[k] - o[k]) / d[k];
            if (a > b)
                std::swap(a, b);
            lo = std::max(lo, a);
            hi = std::min(hi, b);
            if (lo > hi + 1e-11)
                return;
        }
        if (n.left >= 0) {
            cast(n.left, o, d, skip, best);
            cast(n.right, o, d, skip, best);
            return;
        }
        for (int i = n.begin; i < n.end; i++) {
            auto v = t[order[i]].v;
            if (std::find(v.begin(), v.end(), skip) != v.end())
                continue;
            Vec a = p[v[0]], e = p[v[1]] - a, f = p[v[2]] - a, h = d.cross(f);
            double den = e.dot(h);
            if (std::abs(den) < 1e-14)
                continue;
            Vec s = o - a;
            double u = s.dot(h) / den;
            if (u < -1e-10 || u > 1 + 1e-10)
                continue;
            Vec q = s.cross(e);
            double w = d.dot(q) / den;
            if (w < -1e-10 || u + w > 1 + 1e-10)
                continue;
            double hit = f.dot(q) / den;
            if (hit > 1e-9)
                best = std::min(best, hit);
        }
    }
    double clearance(int v, Vec d) const {
        Vec a = d.cross(std::abs(d.z) < .8 ? Vec{0, 0, 1} : Vec{0, 1, 0}).unit(), b = d.cross(a);
        double best = 1e100;
        for (Vec ray : {d, (d + a * .08).unit(), (d - a * .08).unit(), (d + b * .08).unit(),
                        (d - b * .08).unit()})
            cast(0, p[v], ray, v, best);
        if (best >= 1e99)
            throw std::runtime_error("Open boundary: opposing wall not found");
        return best;
    }
};